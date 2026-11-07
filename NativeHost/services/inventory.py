#!/usr/bin/env python3
"""Reproduce the source inventory from the two local MIT references, without game reads."""
import os
from pathlib import Path
import csv
import hashlib
import json
import re
import subprocess
import argparse

HERE = Path(__file__).resolve().parent
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--engine', type=Path, default=Path(os.environ.get('SATCHEL_DEPS_ROOT', Path.home() / 'SatchelDeps')) / 'NX2Apple/engine')
p.add_argument('--ryujinx', type=Path, default=Path.home() / 'SatchelDeps/reference/ryujinx-1.3.3')
p.add_argument('--output', type=Path, default=HERE / 'coverage')
a = p.parse_args(); a.output.mkdir(parents=True, exist_ok=True)
PIN = 'f8a5618d5f505a4c0a0bcac7c413872d3a53c009'
assert subprocess.check_output(['git', '-C', str(a.engine), 'rev-parse', 'HEAD'], text=True).strip() == PIN

def balanced(text, start, opening='(', closing=')'):
    depth = 0
    token = re.compile(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[(){}]')
    for m in token.finditer(text, start):
        if m[0] == opening: depth += 1
        elif m[0] == closing:
            depth -= 1
            if not depth: return m.end()
    raise ValueError('Unbalanced source at ' + str(start))

def strings(s):
    return re.findall(r'"((?:\\.|[^"\\])*)"', s)

def fields(s):
    result = []; start = 0; depth = 0; quote = False; escape = False
    for i, c in enumerate(s):
        if quote:
            if escape: escape = False
            elif c == '\\': escape = True
            elif c == '"': quote = False
        elif c == '"': quote = True
        elif c == '(': depth += 1
        elif c == ')': depth -= 1
        elif c == ',' and not depth: result.append(s[start:i].strip()); start = i + 1
    return result + [s[start:].strip()]

engine = []; hashes = {}
macros = r'HLE_FORCE_BIG|HLE_FORCE|HLE|SKF|SSF|SSC|PALMA_FAIL|IKB_IGNORE_CB|IKB_IGNORE'
for file in sorted((a.engine / 'dm').rglob('hle*')):
    if file.suffix not in ('.c', '.m'): continue
    text = file.read_text(); rel = str(file.relative_to(a.engine)); hashes[rel] = hashlib.sha256(file.read_bytes()).hexdigest()
    defines = dict(re.findall(r'^#define (\w+) ("[^\n]+)', text, re.M))
    for m in re.finditer(r'\b(' + macros + r')\s*\(\s*"', text):
        end = balanced(text, text.index('(', m.start())); args = fields(text[text.index('(', m.start()) + 1:end-1])
        symbol = ''.join(strings(args[0])); why = ''.join(strings(re.sub(r'\b[A-Z][A-Z_]+\b', lambda m: defines.get(m[0], m[0]), args[1]))) if m[1].startswith('HLE_FORCE') or m[1] == 'PALMA_FAIL' else ''
        if not why:
            why = {'SKF':'bsd networking cut, ENETDOWN', 'SSF':'ssl cut, failure', 'SSC':'ssl cut, failure',
                   'IKB_IGNORE':'keyboard setting ignored', 'IKB_IGNORE_CB':'keyboard callback ignored'}.get(m[1], '')
        body = ''
        after = text[end:].lstrip()
        if after.startswith('{'):
            begin = end + len(text[end:]) - len(after); stop = balanced(text, begin, '{', '}'); body = text[begin:stop]
        evidence = (why + ' ' + body).lower()
        status = 'host behavior or partial implementation'
        if re.search(r'cut|stub|ignored|out of scope|never |always |no-op|host (?:has|keeps|provides|does) no|returns? (?:0|zero)|none connected|nothing', evidence): status = 'stub or fixed policy (review source)'
        elif 'res_fail' in evidence or 'die(' in evidence: status = 'implementation with error or abort path'
        engine.append(dict(symbol=symbol, macro=m[1], status=status, reason=why, source=rel, line=text.count('\n', 0, m.start())+1))
# Demangling is presentation only, no guest image or symbol table is consulted.
proc = subprocess.run(['c++filt', '-n'], input='\n'.join(e['symbol'] for e in engine)+'\n', capture_output=True, text=True, check=True)
for row, demangled in zip(engine, proc.stdout.splitlines()): row['sdk_command'] = demangled
assert len(proc.stdout.splitlines()) == len(engine)

def write(name, rows):
    with (a.output / name).open('w') as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0]), delimiter='\t', lineterminator='\n'); w.writeheader(); w.writerows(rows)
write('engine-sdk.tsv', engine)

# IPC service registrations and command attributes, including aliases and inherited methods.
classes = []; registrations = []; routes = []
for family, folder in [('HLE', a.ryujinx / 'src/Ryujinx.HLE/HOS/Services'), ('Horizon', a.ryujinx / 'src/Ryujinx.Horizon')]:
    for file in sorted(folder.rglob('*.cs')):
        text = file.read_text(); rel = str(file.relative_to(a.ryujinx))
        hashes[rel] = hashlib.sha256(file.read_bytes()).hexdigest()
        cls = re.search(r'\b(?:class|struct)\s+(\w+)(?:\s*:\s*([^\n{]+))?', text)
        if not cls: continue
        name = cls[1]; services = re.findall(r'\[Service\("([^"]+)"', text)
        registrations += [dict(service=s, interface=name, source=rel, mapping='direct Service attribute') for s in services]
        if family == 'Horizon':
            for m in re.finditer(r'(RegisterObjectForServer|RegisterServer|RegisterServiceForSelf)\(([^;]+?)ServiceName\.Encode\("([^"]+)"', text):
                expression = m[2].strip().rstrip(',').strip(); interface = None
                namespaces = re.findall(r'using ([\w.]+);', text)
                direct = re.match(r'new (\w+)\(', expression)
                if direct: interface = direct[1]
                elif re.fullmatch(r'\w+', expression):
                    variable = re.search(r'\b(\w+)\s+' + expression + r'\s*=\s*new', text)
                    if variable: interface = variable[1]
                elif 'PortIndex.' in expression:
                    selector = expression.split(')')[-1].strip()
                    manager = file.with_name(file.stem.replace('IpcServer', 'ServerManager') + '.cs')
                    if manager.exists():
                        namespaces += re.findall(r'using ([\w.]+);', manager.read_text())
                        route = re.search(re.escape(selector) + r'\s*=>\s*AcceptImpl\(server, new (\w+)', manager.read_text())
                        if route: interface = route[1]
                registrations.append(dict(service=m[3], interface=interface or 'see server registration', source=rel,
                                          mapping='Horizon object registration' if interface else 'Horizon SM manager registration'))
                if interface:
                    routes.append((m[3], interface, namespaces, rel))
            for service in re.findall(r'ManageNamedPort\([^;]+?"([^"]+)"', text):
                registrations.append(dict(service=service, interface='UserService', source=rel, mapping='Horizon named port, SmServerManager'))
        matches = list(re.finditer(r'\[(?:CommandCmif|CmifCommand)\((\d+)', text)); commands = []
        for m in matches:
            method = re.search(r'\b(?:public|private|internal|protected)\s+(?:(?:static|virtual|override|async)\s+)*[\w<>.?\[\],]+\s+(\w+)\s*\(', text[m.end():])
            if not method: raise ValueError(rel + ':' + m[1])
            start = m.end() + method.start(); brace = text.find('{', start)
            if brace < 0: raise ValueError(rel)
            arrow = text.find('=>', start)
            if 0 <= arrow < brace:
                end = text.index(';', arrow); body = text[arrow:end]
            else:
                end = balanced(text, brace, '{', '}'); body = text[brace:end]
            prefix = text[m.start():start]
            status = 'stub or partial (reference logs stub)' if 'PrintStub' in body or 'Stubbed' in body else 'reference implementation (may delegate)'
            commands.append(dict(command=int(m[1]), method=method[1], reference_status=status, source=rel,
                line=text.count('\n', 0, m.start())+1, notes=' '.join(x.strip('/ ') for x in prefix.splitlines() if x.strip().startswith('//'))))
        classes.append(dict(namespace=re.search(r'namespace ([\w.]+)',text)[1], name=name, family=family, services=services, base=(cls[2] or '').split(',')[0].strip(), commands=commands, source=rel))

# Named SM ports are routed by SmServerManager rather than an encoded registration.
for c in classes:
    if c['namespace']=='Ryujinx.Horizon.Sm.Ipc' and c['name'] in ('UserService','ManagerService'):
        c['services'].append('sm:' if c['name']=='UserService' else 'sm:m')

# Resolve the registered root object through the server's imported namespaces.
for service, interface, namespaces, source in routes:
    candidates = [c for c in classes if c['family']=='Horizon' and c['name']==interface and c['namespace'] in namespaces]
    if len(candidates)==1: candidates[0]['services'].append(service)
    else: raise ValueError('Unresolved Horizon root: ' + service + ' ' + interface)

def inherited(c, seen=()):
    if c['source'] in seen: return []
    parents = [x for x in classes if x['name'] == c['base'] and x['family'] == c['family']]
    if len(parents) > 1:   # Several services name their base IManager or IService: take the one beside it.
        parents = [x for x in parents if x['namespace'] == c['namespace']]
    own = list(c['commands']); ids = {x['command'] for x in own}
    if len(parents) == 1:
        own += [x for x in inherited(parents[0], seen + (c['source'],)) if x['command'] not in ids]
    return sorted(own, key=lambda x:(x['command'],x['line']))

priority = {'Settings':1, 'Account':1, 'Fs':2, 'Am':2, 'Time':2, 'Hid':2, 'Audio':2,
            'Nv':2, 'Vi':2, 'Pctl':3, 'Nifm':3, 'Bcat':3, 'Friends':3, 'Prepo':3, 'Aoc':3, 'Nsd':3}
ports = {'set': {0,1,2,3,4,5,6,8}, 'acc:u0': {0,1,2,3,4,5,50,51,141},
         'time:u': {0,1,2,4,5,50,100,101,102,200}, 'nifm:u': {4,5},
         'aoc:u': {0,1,2,3,4,5,6,7,8,9,10,50}, 'friend:u': {0,1},
         'prepo:u': {10100,10101,10102,10103,10104,10105,10200,10300,10400,40100,40101}}
child_ports = {
    'Account/Acc/AccountService/IProfile.cs': {0,1},
    'Time/StaticService/ISystemClock.cs': {0,1,2,3},
    'Time/StaticService/ISteadyClock.cs': {0,2,3,101,102,200,201},
    'Nifm/StaticService/IGeneralService.cs': {4,5,12,15,18},
    'Nifm/StaticService/IRequest.cs': {0,1,2,3,4,11,21},
    'Sdk/Friends/Detail/Ipc/FriendService.cs': {0,1,10100,10101,10120,10121,10400,10420,10421},
    'Sdk/Friends/Detail/Ipc/NotificationService.cs': {0,1,2},
}
def port_status(service, command, source):
    if command in ports.get(service, set()):
        if service == 'aoc:u': return 'ported, package DLC; prepare/mount-check reference stubs'
        if service == 'prepo:u': return 'ported, reports discarded; per-object state'
        return 'ported, conditional stub policy' if (service, command) in [('set',8),('acc:u0',51)] else 'ported'
    for suffix, commands in child_ports.items():
        if service == '(object)' and source.endswith('/' + suffix) and command in commands:
            parent = 'acc:u0' if suffix.startswith('Account/') else 'time:u' if suffix.startswith('Time/') else 'friend:u' if suffix.startswith('Sdk/Friends/') else 'nifm:u'
            policy = ', fixed local profile; UserData stub' if suffix.startswith('Account/') else ', reference offline stubs and waitable events' if suffix.startswith('Sdk/Friends/') else ', user permissions and offline host policy'
            return 'ported via ' + parent + policy
    return 'unchanged'
def rank_command(category, service, interface, command):
    if service in ('aoc:u', 'friend:u', 'prepo:u'): return 2 if command in ports[service] else 3
    if interface == 'FriendService' and command in child_ports['Sdk/Friends/Detail/Ipc/FriendService.cs']: return 2
    if interface in ('NotificationService', 'IRequest'): return 2
    if service == 'set': return 1 if command in ports['set'] else 3
    if service == 'acc:u0': return 1 if command in ports['acc:u0'] or command == 5 else 3
    if category == 'Account': return 2 if interface == 'IProfile' else 3 if service == '(object)' else 4
    if category == 'Settings':
        return 2 if interface == 'ISystemSettingsServer' and command in (0, 3, 4, 38, 62) else 4
    return priority.get(category, 4)

round7 = json.loads((HERE / 'round7.json').read_text())
round8 = json.loads((HERE / 'round8.json').read_text())
round8_sdk = json.loads((HERE / 'round8-sdk.json').read_text())
round11 = json.loads((HERE / 'round11.json').read_text())
# Round 11 children, by reference interface: the transport kind each one is served as.
round11_children = {('ISession', 'Apm/ISession.cs'): 'apm:session', ('SessionServer', 'Apm/ISession.cs'): 'apm:session', ('IPsmSession', 'Ptm/Psm/IPsmSession.cs'): 'IPsmSession'}
rows = []
for c in classes:
    # Keep child interfaces even when they have no named port. They are command surfaces too.
    for x in inherited(c):
        category = c['source'].split('/Services/')[-1].split('/')[0] if c['family']=='HLE' else c['namespace'].split('Ryujinx.Horizon.')[1].removeprefix('Sdk.').split('.')[0]
        for service in c['services'] or ['(object)']:
            matches = [e for e in engine if re.search(r'(?:::|^)' + re.escape(x['method']) + r'\(', e['sdk_command'])]
            # Reviewed aliases and SDK takeover paths, beyond an exact method-name hit.
            aliases = {
                'IApplicationFunctions': {30:'BeginBlockingHomeButton',31:'EndBlockingHomeButton',67:'EnableRecording',102:'SetCopyrightVisibility'},
                'ISelfController': {10:'EnableScreenShot',11:'SetOperationModeChangedNotificationEnabled',12:'SetPerformanceModeChangedNotificationEnabled',16:'SetFocusHandlingMode',62:'SetUserInactivityDetectionTimeExtended',63:'GetUserInactivityDetectionTimeExtensionMode',68:'BeginAutoSleepDisabledSection',100:'EnableAlbumImageTakenNotification',130:'MuteRecording'},
                'ICommonStateGetter': {0:'GetNotificationMessageEvent',1:'TryPopNotificationMessage'},
                'AudioDevice': {4:'AcquireAudioDeviceSwitchNotification',6:'ListAudioDeviceName',7:'SetAudioDeviceOutputVolume',8:'GetAudioDeviceOutputVolume',10:'GetActiveAudioDeviceName',11:'AcquireAudioDeviceNotificationForInput',12:'AcquireAudioDeviceNotificationForOutput'},
                'AudioOutManager': {2:'ListAudioOuts',3:'OpenAudioOut'},
                'AudioOut': {4:'OpenAudioOut',5:'GetReleasedAudioOutBuffer',7:'AppendAudioOutBuffer',8:'GetReleasedAudioOutBuffer'},
                'ITimeZoneServiceForGlue': {101:'ToCalendarTime',202:'ToPosixTime'},
                'ITimeZoneServiceForPsc': {101:'ToCalendarTime',202:'ToPosixTime'},
            }
            alias = aliases.get(c['name'], {}).get(x['command'])
            if alias:
                matches += [e for e in engine if re.search(r'::' + alias + r'\(', e['sdk_command']) and e not in matches]
            if c['name'] in ('HardwareOpusDecoder', 'HardwareOpusDecoderManager'):
                matches += [e for e in engine if e['source']=='dm/host/hle_codec.c' and e not in matches]
            if c['name'] == 'AudioRenderer':
                matches += [e for e in engine if e['source']=='dm/host/hle_audren.c' and '::OpenAudioRenderer(' in e['sdk_command'] and e not in matches]
            state = port_status(service, x['command'], x['source'])
            if (x['command'] in round7.get(c['name'], []) and service in ('(object)', 'appletOE', 'appletAE', 'vi:u', 'audren:u', 'pctl', 'time:u')
                and (c['name'] != 'IStorage' or '/Am/' in c['source'])):
                state = 'ported round 7, scoped host policy; see README'
            elif alias or c['name'] in ('AudioRenderer', 'HardwareOpusDecoder', 'HardwareOpusDecoderManager'):
                state = 'skipped round 7, SDK takeover or delegate; raw IPC unchanged'
            # Round 8 aliases are scoped to the actual subsystem, not generic
            # method-name matches from SSL, movie decoders or shop objects.
            in8 = category in ('Fs', 'Hid', 'Time')
            if in8:
                sdk_file = 'hle_fs.c' if category == 'Fs' else 'hle_hid.c' if category == 'Hid' else None
                if sdk_file:
                    matches = [e for e in matches if e['source'].endswith('/' + sdk_file)]
                names = round8_sdk.get(c['name'], {}).get(str(x['command']), [])
                for name in names:
                    matches += [e for e in engine if re.search(r'::' + re.escape(name) + r'\(', e['sdk_command'])
                                and (not sdk_file or e['source'].endswith('/' + sdk_file)) and e not in matches]
                runtime = {'IHidServer':'hid', 'IFileSystemProxy':'fsp-srv'}.get(c['name'], c['name'])
                key = service if service.startswith('time:') else runtime
                if x['command'] in round8.get(key, []):
                    state = 'ported round 8, scoped reference contracts; see README'
                elif c['name'] == 'ISystemClock' and x['command'] in (0,1,2,3):
                    state = 'ported via time:u/a/s/r, reference permissions and shared SDK clocks'
                elif c['name'] == 'ISteadyClock' and x['command'] in (0,2,3,101,102,200,201):
                    state = 'ported via time:u/a/s/r, shared offsets and repair permissions'
                elif c['name'] == 'ITimeZoneServiceForGlue' and x['command'] in (1,2,3):
                    state = 'ported via time:u/a/r, missing firmware and reference permissions'
                elif matches and state == 'unchanged':
                    state = 'skipped round 8, reviewed SDK takeover; raw IPC unchanged'
            kind11 = service if service != '(object)' else next((k for (n, s), k in round11_children.items() if c['name'] == n and x['source'].endswith('/' + s)), None)
            if kind11 and x['command'] in round11.get(kind11, []):
                state = 'ported round 11, reference contracts; see README'
            if category == 'Fs' and c['name'] == 'IFile' and x['command'] == 2:
                state = 'round 9 SDK FlushFile delegates backing flush; raw IPC unchanged'
            rows.append(dict(rank=rank_command(category, service, c['name'], x['command']), service=service, interface=c['name'], command=x['command'], method=x['method'],
                pinned_ipc='zero-success wildcard' if service=='olsc:u' else 'no raw IPC implementation',
                overlay_ipc=state,
                sdk_candidates='; '.join(e['source']+':'+str(e['line'])+' '+e['status'] for e in matches),
                reference_status=x['reference_status'], source=x['source'], line=x['line'], notes=x['notes'] or '(no signature comment)'))
# Every numeric attribute must survive alias, inheritance and firmware-variant expansion.
expected = {(c['source'],x['line']) for c in classes for x in c['commands']}
actual = {(x['source'],x['line']) for x in rows}
assert expected == actual, expected - actual
rows.sort(key=lambda x:(x['rank'],x['service'],x['interface'],x['command'],x['source']))
write('ryujinx-commands.tsv', rows)
write('ryujinx-services.tsv', sorted(registrations, key=lambda x:(x['service'],x['source'])))
# Source SVC cases, separate from the user-mode IPC command namespace.
svcs = []
for name in ['hle_svc.c', 'hle_thread.c']:
    text = (a.engine / 'dm/host' / name).read_text()
    for m in re.finditer(r'case (0x[0-9a-fA-F]+)\s*:', text):
        svcs.append(dict(number=m[1], source='dm/host/'+name, line=text.count('\n',0,m.start())+1,
                         note=text[m.start():text.find('\n',m.start())].strip()))
write('engine-svc.tsv', svcs)
# Every SVC Ryujinx 1.3.3 implements, against the pinned engine and Satchel's kernel overlay.
syscall = a.ryujinx / 'src/Ryujinx.HLE/HOS/Kernel/SupervisorCall/Syscall.cs'
syscall_text = syscall.read_text()
overlay_svcs = {int(m[1], 16) for m in re.finditer(r'case (0x[0-9a-fA-F]+)\s*:', (HERE / 'KernelSVC.inc').read_text())}
engine_svcs = {int(x['number'], 16): x for x in svcs}
UNIMPLEMENTED = {
    0x07: 'Ends the process. The app has no path from a guest SVC to its own end of a game (the menu Quit stops the session); not reached by the tested games, the placeholder still returns success',
    0x23: 'Asynchronous IPC needs a reply event; SDK clients use the synchronous forms, which reach the host sessions',
}
for n, why in [((0x40, 0x41, 0x43, 0x44, 0x70, 0x71, 0x72), 'Server side of IPC, for system modules that host services; an application is a client and the host serves IPC itself'),
               ((0x4B, 0x4C), 'nn::jit code memory: runtime JIT is out of scope (AGENTS.md); the pinned hle_jit.c takes nn::jit over at the SDK level and Satchel offers no JIT image'),
               ((0x65, 0x6F, 0x73, 0x74, 0x75, 0x77, 0x78, 0x7B, 0x7D, 0x7E), 'Privileged: loader, process manager and debugger calls that an application NPDM does not grant; the host runs one process')]:
    UNIMPLEMENTED.update(dict.fromkeys(n, why))
svc_rows = []
for m in re.finditer(r'\[Svc\((0x[0-9a-fA-F]+|\d+)\)\]\s*\n\s*public \w+ (\w+)\(', syscall_text):
    number = int(m[1], 0)
    if number in engine_svcs: status, where, why = 'pinned engine', engine_svcs[number]['source'] + ':' + str(engine_svcs[number]['line']), ''
    elif number in overlay_svcs: status, where, why = 'Satchel overlay', 'NativeHost/services/KernelSVC.inc', ''
    else: status, where, why = 'not implemented', '', UNIMPLEMENTED[number]
    svc_rows.append(dict(number='0x%02X' % number, name=m[2], ryujinx='src/Ryujinx.HLE/HOS/Kernel/SupervisorCall/Syscall.cs:' + str(syscall_text.count('\n', 0, m.start()) + 1),
                         status=status, implementation=where, reason=why))
assert len(svc_rows) == 72 and not overlay_svcs & set(engine_svcs), (len(svc_rows), overlay_svcs & set(engine_svcs))
assert not set(UNIMPLEMENTED) & (overlay_svcs | set(engine_svcs))
svc_rows.sort(key=lambda x: int(x['number'], 16))
write('svc-table.tsv', svc_rows)
# Every service Ryujinx 1.3.3 registers, against what a Satchel game reaches: a host session
# (SessionIPC.inc), the engine's or Satchel's takeover of the SDK client, or neither.
served = re.search(r'served\[\] = \{([^}]*)\}', (HERE / 'SessionIPC.inc').read_text())[1]
served = set(re.findall(r'"([^"]+)"', served))
satchel_sdk = []
for name in ['SdkRecording.inc', 'SdkFamilies.inc']:
    text = (HERE / name).read_text()
    satchel_sdk += [dict(symbol=s, source='NativeHost/services/' + name) for s in re.findall(r'"(_ZN2nn[^"]+)"', text)]
names = subprocess.run(['c++filt', '-n'], input='\n'.join(s['symbol'] for s in satchel_sdk) + '\n', capture_output=True, text=True, check=True).stdout.splitlines()
for s, demangled in zip(satchel_sdk, names): s['sdk_command'] = demangled
# The client families an application reaches each port through. Reviewed by hand against the
# SDK namespaces the engine hooks (engine-sdk.tsv); a port without a family has no SDK takeover.
FAMILIES = {
    'acc:u0': ['account'], 'aoc:u': ['aoc'], 'apm': ['oe::GetPerformance', 'oe::SetPerformanceConfiguration'], 'appletOE': ['oe', 'applet', 'swkbd', 'err', 'web'],
    'audin:u': ['audio::OpenAudioIn', 'audio::OpenDefaultAudioIn', 'audio::OpenInputStream'], 'audout:u': ['audio'], 'audren:u': ['audio'], 'audrec:u': [],
    'bcat:u': ['bcat'], 'bsd:u': ['socket', 'nnsocket'], 'caps:su': ['album'], 'caps:u': ['album'], 'csrng': ['crypto::GenerateCryptographicallyRandomBytes'],
    'ectx:aw': ['err::InitializeContextWriterApi'], 'fatal:u': [], 'friend:u': ['friends'], 'fsp-srv': ['fs'], 'grc:c': [], 'hid': ['hid'], 'hidbus': [],
    'hwopus': ['codec'], 'irs': ['irsensor'], 'ldn:u': ['ldn'], 'lm': ['nndiagStartup'], 'lp2p:app': [], 'mii:e': ['mii'], 'mm:u': [], 'mnpp:app': [],
    'nfc:user': ['nfc'], 'nfp:user': ['nfp'], 'ngc:u': ['ngc'], 'nifm:u': ['nifm'], 'notif:a': [], 'npns:u': [], 'nsd:u': ['nsd'], 'ntc': [],
    'nvdrv': ['NvOsDrv'], 'olsc:u': [], 'pctl': ['pctl'], 'pl:u': ['pl'], 'prepo:u': ['prepo'], 'psm': [], 'set': ['settings'],
    'sfdnsres': ['nnsocketGetAddrInfo', 'nnsocketGetHostByName', 'nsd::Resolve'], 'ssl': ['ssl', 'nnssl'], 'time:u': ['time'], 'vi:u': ['vi'],
}
OPEN = {
    'audrec:u': 'Final output recorder; no SDK takeover and no host session. Not reached by the tested games',
    'grc:c': 'Game movie recording service; the SDK records through am (EnableGamePlayRecording takeover). No host session',
    'hidbus': 'Joy-Con rail devices (Ring-Con); Ryujinx answers only GetBusHandle. No host session, no SDK takeover',
    'lp2p:app': 'Local peer-to-peer (newer SDKs); Ryujinx registers it without commands. No host session',
    'mnpp:app': 'Mainland China application rules; Ryujinx answers stubs. Not needed outside that region',
    'notif:a': 'Application alarms (newer SDKs). No host session, no SDK takeover',
    'npns:u': 'Push notifications; Ryujinx registers it without commands. A game that connects gets the missing-service report',
    'ntc': 'Network clock correction (nn::time::EnsureNetworkClockAvailability); offline host, no host session yet',
}
# An application's service access list (its NPDM) names only these; the rest serve system
# modules, applets and privileged clients. This is the public application list (switchbrew), not
# read from any game.
APPLICATION = set(FAMILIES)
service_rows = []
for reg in sorted({r['service']: r for r in registrations}.values(), key=lambda r: r['service']):
    port = reg['service']; root = [x for x in rows if x['service'] == port]
    answered = sum(1 for x in root if x['overlay_ipc'].startswith('ported'))
    prefixes = FAMILIES.get(port, [])
    def family(symbol, demangled):
        return any(demangled.startswith('nn::' + f + ('' if '::' in f else '::')) or symbol.startswith(f) for f in prefixes)
    engine_hooks = [e for e in engine if family(e['symbol'], e['sdk_command'])]
    own = [s for s in satchel_sdk if family(s['symbol'], s['sdk_command'])]
    where = sorted({e['source'] for e in engine_hooks} | {s['source'] for s in own})
    if port in served:
        status = 'Satchel host session' + (' and SDK takeover' if engine_hooks or own else '')
        where = ['NativeHost/services/SessionIPC.inc'] + where
        reason = 'Zeroed-success compatibility session (the pinned engine policy), not reference contracts' if port == 'olsc:u' or port.startswith('caps:') else ''
    elif engine_hooks or own:
        status = 'SDK takeover'; reason = 'The client is taken over at the SDK (' + str(len(engine_hooks)) + ' engine and ' + str(len(own)) + ' Satchel functions); no host session'
    elif port in APPLICATION:
        status = 'open'; reason = OPEN.get(port, 'Application service without a host session or SDK takeover')
    else:
        status = 'system only'; reason = 'Not in an application service access list: system modules, applets and privileged clients'
    service_rows.append(dict(service=port, ryujinx=reg['source'] + (' (' + reg['interface'] + ')' if reg['interface'] else ''), status=status,
                             commands=len(root), answered=answered, sdk_functions=len(engine_hooks) + len(own),
                             implementation='; '.join(where[:6]) + ('; ...' if len(where) > 6 else ''), reason=reason))
write('service-table.tsv', service_rows)
summary = dict(engine_pin=PIN, ryujinx='Ryubing 1.3.3, local read-only reference', sdk_hooks=len(engine),
               service_registrations=len(registrations), command_rows=len(rows), source_hashes=hashes)
(a.output / 'sources.json').write_text(json.dumps(summary, indent=2)+'\n')
print(json.dumps({k:v for k,v in summary.items() if k!='source_hashes'}))

# Runtime lookup contains reference labels only, never guest strings or data.
lookup = sorted({(r['service'], r['interface'], r['command'], r['method']) for r in rows
                 if r['interface'] != 'IStorage' or '/Am/' in r['source']})
with (a.output / 'MissingServiceNames.inc').open('w') as f:
    f.write('// Generated by inventory.py from the MIT Ryujinx coverage table.\n')
    f.write('static const struct { const char *service, *interface; uint32_t command; const char *method; } satchel_command_names[] = {\n')
    for service, interface, command, method in lookup:
        f.write('    {"%s", "%s", %du, "%s"},\n' % (service, interface, command, method))
    f.write('};\n')
