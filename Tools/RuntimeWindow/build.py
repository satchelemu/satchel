"""Original Swift/actual-C Window integration; no device or canonical writes."""
from pathlib import Path
import argparse, hashlib, json, os, shutil, subprocess
P=argparse.ArgumentParser()
for n in ['source-root','dependency-root','cpu-archive','producer-manifest','output']:P.add_argument('--'+n,required=True,type=Path)
a=P.parse_args(); S=a.source_root.resolve();D=a.dependency_root.resolve();O=a.output.resolve();M=a.producer_manifest.resolve();HERE=Path(__file__).resolve().parent;R=HERE.parents[1]
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
if O.exists() or O.is_relative_to(S) or O.is_relative_to(D):raise SystemExit('output must be fresh and outside source/dependency roots')
lock=json.loads((HERE/'SourceLock.json').read_text());m=json.loads(M.read_text());assert m['status']=='passed'
inputs={str(M):sha(M),str(HERE/'SourceLock.json'):sha(HERE/'SourceLock.json'),str(Path(__file__)):sha(Path(__file__))}
for rel,h in lock['fixtures'].items():assert sha(R/rel)==h;inputs[str(R/rel)]=h
for n,h in lock['app_files'].items():assert sha(S/'App'/n)==h;inputs[str(S/'App'/n)]=h
session=S/'App/SwitchRuntimeSession.swift';text=session.read_text();w=lock['worker'];start=text.index(w['start']);end=text.index(w['end'],start);body=text[start:end]
assert hashlib.sha256(body.encode()).hexdigest()==w['sha256'];assert '    private static let queue = DispatchQueue(label: "com.dell.satchel.graphics", qos: .userInitiated)' in text
inputs[str(session)]=sha(session)
p=S/'App/SwitchPackageImport.swift';text=p.read_text();e=lock['error_type'];start=text.index(e['start']);end=text.index(e['end'],start)+len(e['end']);error=text[start:end];assert hashlib.sha256(error.encode()).hexdigest()==e['sha256'];inputs[str(p)]=sha(p)
O.mkdir(parents=True);H=O/'private/headers';H.mkdir(parents=True);L=O/'private/link';L.mkdir();A=O/'private/App';A.mkdir();private={};provenance={}
def copy(p,d,expected=None):
 p=Path(p);h=sha(p);assert expected is None or h==expected,(p,expected,h)
 if d.exists():assert sha(d)==h,(p,d)
 else:shutil.copy2(p,d)
 provenance[str(p)]=h;private[str(d)]=h;return d
for n,h in lock['app_files'].items():copy(S/'App'/n,A/n,h)
(A/'ExtractedWindowWorker.swift').write_text(w['prefix']+body+w['suffix']);(A/'ImportFailure.swift').write_text('import Foundation\n\n'+error)
for p in [A/'ExtractedWindowWorker.swift',A/'ImportFailure.swift']:private[str(p)]=sha(p)
# Every included dependency file must match a producer-bound input of the same name.
bound={}
for p,h in m['inputs'].items():bound.setdefault(Path(p).name,set()).add(h)
for root in [D/'Core',D/'Tools']:
 for p in sorted(root.rglob('*')):
  if p.is_file() and p.suffix in ('.h','.inc') and sha(p) in bound.get(p.name,set()):copy(p,H/p.name)
assert (H/'CPUProbe.h').exists() and (H/'HorizonIPC.h').exists()
copy(m['header'],H/'WindowFrameBridge.h',m['header_sha256'])
links=[];cpu_seen=False
for p,h in m['link_inputs'].items():
 if Path(p).name=='libSatchelCPUProbe.a':p=a.cpu_archive;cpu_seen=True
 links.append(copy(p,L/Path(p).name,h))
assert cpu_seen
fixture=copy(m['artifact'],L/Path(m['artifact']).name,m['artifact_sha256'])
commands=[];env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1')
def run(c,n):
 commands.append(list(map(str,c)));r=subprocess.run(commands[-1],capture_output=True,text=True,env=env);(O/(n+'.log')).write_text(r.stdout+r.stderr)
 if r.returncode:raise SystemExit('failed '+n+': '+str(O/(n+'.log')))
 return r.stdout.strip()
flags=['-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer']
resource=Path(run(['xcrun','clang','-print-resource-dir'],'clang-resource'));ubsan=copy(resource/'lib/darwin/libclang_rt.ubsan_osx_dynamic.dylib',L/'libclang_rt.ubsan_osx_dynamic.dylib')
common=['xcrun','swiftc','-swift-version','5','-warnings-as-errors','-parse-as-library','-I',H]
linkflags=['-sanitize=address','-g','-Onone','-lc++',*m['framework_flags'],'-Xlinker','-rpath','-Xlinker',L]
# Rebuild only symbol-forwarded test variants; actual production archive is untouched.
objects=[]
for name,macro in [('GuestDispatch','satchel_guest_dispatch_handle=wf_actual_dispatch'),('GuestVulkanWindows','satchel_guest_dispatch_handle_windows=wf_actual_windows')]:
 p=D/'Tools/GuestGraphics'/(name+'.c');assert sha(p) in bound.get(p.name,set());src=copy(p,O/(name+'.c'));obj=O/(name+'.o')
 run(['xcrun','clang','-std=c11',*flags,'-I',H,'-D'+macro,'-c',src,'-o',obj],name);objects.append(obj)
run(['xcrun','clang','-std=c11',*flags,'-I',H,'-I',HERE,'-c',HERE/'Fixture.c','-o',O/'Fixture.o'],'helper-fixture');objects.append(O/'Fixture.o')
helperlinks=[p for p in links if p.name not in ('GuestDispatch.o','GuestVulkanWindows.o')]
assert len(helperlinks)==len(links)-2
run([*common,'-import-objc-header',HERE/'Bridge.h',A/'SwitchRuntimeWindow.swift',A/'SwitchWindowReport.swift',HERE/'Test.swift',*objects,*helperlinks,ubsan,*linkflags,'-o',O/'helper-test'],'helper-link')
helper=run([O/'helper-test'],'helper-test');print(helper,flush=True)
F=R/'Tools/RuntimeWindowFrame'
run(['xcrun','clang','-std=c11',*flags,'-I',H,'-c',F/'ReleaseShim.c','-o',O/'ReleaseShim.o'],'release-shim')
run([*common,'-import-objc-header',F/'TestBridge.h',A/'SwitchRuntimeFrame.swift',A/'SwitchRuntimeSurfaceState.swift',A/'ExtractedWindowWorker.swift',A/'ImportFailure.swift',F/'Test.swift',O/'ReleaseShim.o',fixture,*links,ubsan,*linkflags,'-o',O/'frame-test'],'frame-link')
frame=run([O/'frame-test'],'frame-test');print(frame,flush=True)
sdk=run(['xcrun','--sdk','iphoneos','--show-sdk-path'],'ios-sdk')
run([*common,'-import-objc-header',HERE/'Bridge.h','-target','arm64-apple-ios17.0','-sdk',sdk,'-emit-module','-module-name','RuntimeWindow',A/'SwitchRuntimeWindow.swift',A/'SwitchWindowReport.swift','-emit-module-path',O/'RuntimeWindow.swiftmodule'],'helper-ios')
for p,h in {**inputs,**private}.items():assert sha(p)==h,p
result={'status':'passed','inputs':inputs,'producer_result':m['result'],'source_root':str(S),'dependency_root':str(D),'cpu_archive':str(a.cpu_archive.resolve()),'provenance':provenance,'private_inputs':private,'worker_body_sha256':w['sha256'],'commands':commands,'helper_result':helper,'frame_result':frame,'artifacts':{str(p):sha(p) for p in [O/'helper-test',O/'frame-test',O/'RuntimeWindow.swiftmodule']},'scope':'Original actual-C/helper and real GPU frame→exact Swift consumer/CoreGraphics; pure surface-state attachment, no UIKit display or guest-title frame claim.'}
(O/'validation-manifest.json').write_text(json.dumps(result,indent=2)+'\n')
