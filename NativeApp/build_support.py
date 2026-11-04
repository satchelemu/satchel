"""Shared local build exclusion. Inspect executables, not shell text quoting commands."""
from pathlib import Path
import os
import subprocess
import shlex
import time


def xcode_is_building(pid):
    result = subprocess.run(['ps', '-p', str(pid), '-o', 'args='], text=True, capture_output=True)
    if result.returncode:
        return False
    try:
        arguments = shlex.split(result.stdout)
    except ValueError:
        return True
    actions = {'build', 'test', 'archive', 'analyze', 'build-for-testing', 'clean'}
    # A scheme or option value can be named like an action. Be conservative
    # after every flag, including flags that might actually take no value.
    positional = {value for index, value in enumerate(arguments) if index > 0 and
                  not arguments[index - 1].startswith('-')}
    # This action only runs already-built tests. Actual compiler processes are
    # still checked independently, including any other Xcode invocation.
    return 'test-without-building' not in positional or bool(actions.intersection(arguments))


def wait():
    # The orchestrator can take the slot after pausing (SIGSTOP) the build that held it.
    if os.environ.get('SATCHEL_BUILD_SLOT') == 'orchestrator':
        return
    while True:
        # comm must be the final column on macOS or ps truncates it to 15 characters.
        rows = subprocess.check_output(['ps', '-Ao', 'pid=,comm='], text=True).splitlines()
        python = []
        active = False
        for row in rows:
            pid, executable = row.strip().split(None, 1)
            name = Path(executable).name
            if name in {'xcodebuild', 'clang', 'clang++', 'swift-frontend', 'metal', 'metalfe', 'metallib', 'shader-harness', 'ninja'}:
                active |= name != 'xcodebuild' or xcode_is_building(pid)
            if name.lower().startswith('python'):
                python.append(pid)
        if python:
            args = subprocess.check_output(['ps', '-p', ','.join(python), '-o', 'args='], text=True)
            active |= any(script in args for script in ['own_compilers.py', 'dmsh_build.py', 'rebuild.py'])
        if not active: return
        print('Waiting for another heavy build to finish.', flush=True)
        time.sleep(15)


def signing_team(root=None, required=True):
    """The Apple team a signed build uses, so no team ID lives in the repository.

    Order: the SATCHEL_TEAM environment variable, the untracked file local/signing-team,
    then the one team the Mac's Apple Development identities belong to. With several
    teams in the keychain the caller must choose one explicitly."""
    import re
    root = Path(root) if root else Path(__file__).resolve().parents[1]
    team = os.environ.get('SATCHEL_TEAM', '').strip()
    source = 'SATCHEL_TEAM'
    if not team:
        local = root / 'local/signing-team'
        if local.is_file():
            team = local.read_text().strip()
            source = str(local)
    found = set()
    if not team:
        try:
            listing = subprocess.run(['security', 'find-identity', '-v', '-p', 'codesigning'],
                                     capture_output=True, text=True).stdout
            for name in sorted(set(re.findall(r'"(Apple Development: [^"]+)"', listing))):
                pem = subprocess.run(['security', 'find-certificate', '-c', name, '-p'],
                                     capture_output=True, text=True).stdout
                subject = subprocess.run(['openssl', 'x509', '-noout', '-subject'], input=pem,
                                         capture_output=True, text=True).stdout
                match = re.search(r'OU\s*=\s*([A-Z0-9]{10})', subject)
                if match:
                    found.add(match.group(1))
        except OSError:
            pass
        if len(found) == 1:
            team = found.pop()
            source = 'the Apple Development identity in the login keychain'
    if not team:
        if not required:
            return None
        raise SystemExit('No signing team: set SATCHEL_TEAM, write the ten-character team ID to '
                         + str(root / 'local/signing-team') + ', or keep exactly one Apple '
                         'Development identity in the login keychain (found: '
                         + (', '.join(sorted(found)) or 'none') + '). Xcode shows the team ID '
                         'under Settings, Accounts.')
    if not re.fullmatch(r'[A-Z0-9]{10}', team):
        raise SystemExit('Signing team from ' + source + ' is not a ten-character Apple team ID.')
    return team


def mac_signing_identity(root=None, required=True):
    """The Mac codesign identity (SHA-1 hash or name) for development-only Mac signing.

    Order: SATCHEL_SIGNING_IDENTITY, the untracked file local/signing-identity, then the one
    Apple Development identity in the login keychain."""
    import re
    root = Path(root) if root else Path(__file__).resolve().parents[1]
    identity = os.environ.get('SATCHEL_SIGNING_IDENTITY', '').strip()
    if not identity:
        local = root / 'local/signing-identity'
        if local.is_file():
            identity = local.read_text().strip()
    if not identity:
        try:
            listing = subprocess.run(['security', 'find-identity', '-v', '-p', 'codesigning'],
                                     capture_output=True, text=True).stdout
            hashes = sorted(set(re.findall(r'([A-F0-9]{40}) "Apple Development: ', listing)))
        except OSError:
            hashes = []
        if len(hashes) == 1:
            identity = hashes[0]
    if not identity and required:
        raise SystemExit('No Mac signing identity: set SATCHEL_SIGNING_IDENTITY or write the codesign '
                         'identity hash to ' + str(root / 'local/signing-identity') + '.')
    return identity or None
