#!/usr/bin/env python3
"""Check Xcode and install Satchel's pinned build tools into one folder.

Runs with the Python that ships with Xcode (3.9), so a new Mac needs nothing else first.
Every download comes from the URL in scripts/deps/inputs.lock.json ("tools") and is
promoted only after its pinned hash matches. Nothing is installed outside --tools;
nothing in Homebrew, /usr/local or your home folder is read.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import runpy
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
LOCK = ROOT / 'scripts/deps/inputs.lock.json'
XCODE = '27.2'
SYSTEM_PATH = '/usr/bin:/bin:/usr/sbin:/sbin'
# Host commands the build uses besides Xcode's own: all ship with macOS or Xcode.
SYSTEM_TOOLS = ['git', 'curl', 'tar', 'ditto', 'make', 'patch', 'ruby', 'xcrun', 'codesign', 'sandbox-exec']
GIB = 1024 ** 3


def log(text):
    print(text, flush=True)


def hashes(path):
    sha256, sha512 = hashlib.sha256(), hashlib.sha512()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1 << 20), b''):
            sha256.update(block)
            sha512.update(block)
    return sha256.hexdigest(), sha512.hexdigest()


def verified(path, row):
    sha256, sha512 = hashes(path)
    if row.get('sha256') and sha256 != row['sha256']:
        return False
    if row.get('sha512') and sha512 != row['sha512']:
        return False
    return bool(row.get('sha256') or row.get('sha512'))


def run(command, env, cwd=None, log_file=None):
    command = [str(part) for part in command]
    if log_file is None:
        subprocess.run(command, env=env, cwd=cwd, check=True)
        return
    with open(str(log_file), 'a') as stream:
        stream.write('$ ' + ' '.join(command) + '\n')
        stream.flush()
        result = subprocess.run(command, env=env, cwd=cwd, stdout=stream, stderr=subprocess.STDOUT)
    if result.returncode:
        raise RuntimeError('Command failed (exit %d), see %s' % (result.returncode, log_file))


def output(command, env):
    return subprocess.run([str(part) for part in command], env=env, check=True,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT, universal_newlines=True).stdout.strip()


def obtain(tools, row, cache, env):
    """Copy a verified cached file or download the pinned URL; never promote unverified bytes."""
    path = tools / row['destination']
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists():
        if not verified(path, row):
            raise RuntimeError('Pinned hash mismatch for existing file: ' + str(path))
        return path
    for folder in cache:
        candidate = folder / Path(row['destination']).name
        if candidate.is_file() and verified(candidate, row):
            subprocess.run(['cp', '-c', str(candidate), str(path)], check=True)
            return path
    partial = path.with_name(path.name + '.partial')
    try:
        log('Downloading ' + row['url'])
        run(['curl', '--fail', '--location', '--proto', '=https', '--proto-redir', '=https', '--retry', '2',
             '--max-time', '1800', '--silent', '--show-error', row['url'], '--output', partial], env)
        if not verified(partial, row):
            raise RuntimeError('Pinned hash mismatch for download: ' + row['url'])
        partial.replace(path)
    finally:
        if partial.exists():
            partial.unlink()
    return path


def check_host(env):
    """The parts of a Mac that cannot be pinned here: hardware, macOS tools and Xcode."""
    facts = {}
    if platform.system() != 'Darwin' or platform.machine() != 'arm64':
        raise SystemExit('Satchel builds on an Apple Silicon Mac only.')
    facts['macos'] = platform.mac_ver()[0]
    missing = [name for name in SYSTEM_TOOLS if shutil.which(name, path=SYSTEM_PATH) is None]
    if missing:
        raise SystemExit('Missing macOS tools: ' + ', '.join(missing) + '. Install Xcode and run it once.')
    try:
        version = output(['xcodebuild', '-version'], env)
    except (OSError, subprocess.CalledProcessError):
        raise SystemExit('xcodebuild failed. Install Xcode %s, open it once, then run '
                         '`sudo xcode-select -s /Applications/Xcode.app`.' % XCODE)
    facts['xcode'] = ' '.join(version.split())
    if version.splitlines()[0] != 'Xcode ' + XCODE:
        raise SystemExit('Xcode %s is required (found: %s). Select it with xcode-select or DEVELOPER_DIR.'
                         % (XCODE, facts['xcode']))
    facts['developer_dir'] = output(['xcode-select', '-p'], env) if 'DEVELOPER_DIR' not in env else env['DEVELOPER_DIR']
    facts['ios_sdk'] = output(['xcrun', '--sdk', 'iphoneos', '--show-sdk-version'], env)
    facts['macos_sdk'] = output(['xcrun', '--sdk', 'macosx', '--show-sdk-version'], env)
    if facts['ios_sdk'] != XCODE:
        raise SystemExit('The iOS %s SDK is required (found %s).' % (XCODE, facts['ios_sdk']))
    try:
        facts['metal'] = output(['xcrun', '--sdk', 'iphoneos', 'metal', '--version'], env).splitlines()[0]
    except (OSError, subprocess.CalledProcessError, IndexError):
        raise SystemExit('The Metal toolchain is missing. Run `xcodebuild -downloadComponent MetalToolchain`.')
    return facts


def link(tools, target, name):
    destination = tools / 'bin' / name
    destination.parent.mkdir(exist_ok=True)
    if destination.is_symlink() or destination.exists():
        destination.unlink()
    destination.symlink_to(os.path.relpath(str(target), str(destination.parent)))


def fresh(path):
    if path.exists():
        shutil.rmtree(str(path))
    path.mkdir(parents=True)
    return path


def install_cmake(tools, archive, env, logs):
    target = fresh(tools / 'cmake')
    run(['tar', '-xzf', archive, '-C', target, '--strip-components', '1'], env, log_file=logs / 'cmake.log')
    binary = target / 'CMake.app/Contents/bin'
    for name in ['cmake', 'ctest', 'cpack']:
        link(tools, binary / name, name)
    return output([tools / 'bin/cmake', '--version'], env).splitlines()[0]


def install_xcodegen(tools, archive, env, logs):
    target = fresh(tools / 'xcodegen')
    run(['ditto', '-x', '-k', archive, target], env, log_file=logs / 'xcodegen.log')
    link(tools, target / 'xcodegen/bin/xcodegen', 'xcodegen')
    return output([tools / 'bin/xcodegen', '--version'], env)


def install_dotnet(tools, archive, env, logs):
    target = fresh(tools / 'dotnet')
    run(['tar', '-xzf', archive, '-C', target], env, log_file=logs / 'dotnet.log')
    link(tools, target / 'dotnet', 'dotnet')
    sdk_env = dict(env, DOTNET_CLI_HOME=str(tools / 'dotnet-home'), DOTNET_CLI_TELEMETRY_OPTOUT='1',
                   DOTNET_SKIP_FIRST_TIME_EXPERIENCE='1', DOTNET_NOLOGO='1', DOTNET_GENERATE_ASPNET_CERTIFICATE='false')
    return output([target / 'dotnet', '--version'], sdk_env)


def build_python(tools, archive, env, logs, wait):
    """Build the pinned CPython source with Xcode's compiler and macOS SDK libraries only."""
    work = fresh(tools / 'work/python')
    run(['tar', '-xzf', archive, '-C', work, '--strip-components', '1'], env, log_file=logs / 'python.log')
    prefix = tools / 'python'
    if prefix.exists():
        shutil.rmtree(str(prefix))
    sdk = output(['xcrun', '--sdk', 'macosx', '--show-sdk-path'], env)
    build_env = dict(env, SDKROOT=sdk, CC='clang', CXX='clang++')
    wait()
    run(['./configure', '--prefix=' + str(prefix), '--with-ensurepip=no', '--disable-test-modules'],
        build_env, cwd=work, log_file=logs / 'python.log')
    wait()
    run(['nice', '-n', '10', 'make', '-j2'], build_env, cwd=work, log_file=logs / 'python.log')
    run(['make', 'install'], build_env, cwd=work, log_file=logs / 'python.log')
    shutil.rmtree(str(work))
    python = prefix / 'bin/python3'
    # The build needs these stdlib parts; ssl is not needed (downloads use curl).
    run([python, '-I', '-c', 'import zlib, hashlib, json, plistlib, tarfile, zipfile, venv, ctypes, '
         'xml.etree.ElementTree; hashlib.sha256(b"x"); hashlib.file_digest; tarfile.data_filter'],
        env, log_file=logs / 'python.log')
    link(tools, python, 'python3')
    return output([python, '--version'], env)


def build_spirv_val(tools, tools_archive, headers_archive, env, logs, wait):
    """Build only spirv-val from the pinned SPIRV-Tools and matching SPIRV-Headers sources."""
    work = fresh(tools / 'work/spirv')
    source, headers, build = work / 'SPIRV-Tools', work / 'SPIRV-Headers', work / 'build'
    for archive, folder in [(tools_archive, source), (headers_archive, headers)]:
        folder.mkdir()
        run(['tar', '-xzf', archive, '-C', folder, '--strip-components', '1'], env, log_file=logs / 'spirv-val.log')
    wait()
    run([tools / 'bin/cmake', '-S', source, '-B', build, '-DCMAKE_BUILD_TYPE=Release',
         '-DCMAKE_OSX_ARCHITECTURES=arm64', '-DSPIRV-Headers_SOURCE_DIR=' + str(headers),
         '-DSPIRV_SKIP_TESTS=ON', '-DSPIRV_WERROR=OFF', '-DSPIRV_TOOLS_BUILD_STATIC=ON',
         '-DPython3_EXECUTABLE=' + str(tools / 'bin/python3')], env, log_file=logs / 'spirv-val.log')
    wait()
    run(['nice', '-n', '10', tools / 'bin/cmake', '--build', build, '--target', 'spirv-val', '-j2'],
        env, log_file=logs / 'spirv-val.log')
    target = fresh(tools / 'spirv-tools/bin')
    shutil.copy2(str(build / 'tools/spirv-val'), str(target / 'spirv-val'))
    shutil.rmtree(str(work))
    link(tools, target / 'spirv-val', 'spirv-val')
    return output([tools / 'bin/spirv-val', '--version'], env).splitlines()[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--tools', type=Path, required=True, help='Folder for the pinned tools (outside the checkout)')
    parser.add_argument('--cache', type=Path, action='append', default=[],
                        help='Folder with previously downloaded tool archives; each is used only if its hash matches')
    parser.add_argument('--check-only', action='store_true', help='Check Xcode and an existing tools folder; install nothing')
    parser.add_argument('--keep-downloads', action='store_true', help='Keep the verified archives after installing')
    args = parser.parse_args()
    tools = Path(os.path.abspath(str(args.tools.expanduser())))
    if tools == ROOT or ROOT in tools.parents:
        parser.error('The tools folder must be outside the checkout')
    env = {key: value for key, value in os.environ.items() if key in ('HOME', 'USER', 'LOGNAME', 'LANG', 'TMPDIR', 'DEVELOPER_DIR')}
    env['PATH'] = str(tools / 'bin') + ':' + SYSTEM_PATH
    lock = json.loads(LOCK.read_text())
    rows = {row['id']: row for row in lock['tools']}
    manifest_path = tools / 'tools-manifest.json'
    facts = check_host(env)
    log('Xcode: ' + facts['xcode'] + ', iOS SDK ' + facts['ios_sdk'] + ', ' + facts['metal'])
    if args.check_only:
        manifest = json.loads(manifest_path.read_text()) if manifest_path.is_file() else {}
        problems = [name for name in rows if manifest.get('tools', {}).get(name, {}).get('lock') != rows[name]]
        for name in rows:
            log(('PASS ' if name not in problems else 'MISSING ') + name + ' ' + rows[name]['version'])
        return 1 if problems else 0
    tools.mkdir(parents=True, exist_ok=True)
    if shutil.disk_usage(str(tools)).free < 40 * GIB:
        raise SystemExit('Keep at least 40 GiB free before installing tools (about 1.5 GiB is used).')
    logs = tools / 'logs'
    logs.mkdir(exist_ok=True)
    support = runpy.run_path(str(ROOT / 'NativeApp/build_support.py'))
    wait = support['wait']
    cache = [Path(os.path.abspath(str(p.expanduser()))) for p in args.cache]
    archives = {name: obtain(tools, row, cache, env) for name, row in rows.items()}
    installed = {}
    installed['cmake'] = install_cmake(tools, archives['cmake'], env, logs)
    installed['xcodegen'] = install_xcodegen(tools, archives['xcodegen'], env, logs)
    installed['dotnet-sdk'] = install_dotnet(tools, archives['dotnet-sdk'], env, logs)
    installed['python'] = build_python(tools, archives['python'], env, logs, wait)
    installed['spirv-tools'] = build_spirv_val(tools, archives['spirv-tools'], archives['spirv-headers'], env, logs, wait)
    installed['spirv-headers'] = 'used by spirv-tools'
    installed['fonttools'] = 'verified wheel at ' + rows['fonttools']['destination']
    expected = {'cmake': 'cmake version 4.4.4', 'xcodegen': 'Version: 2.45.3', 'dotnet-sdk': '10.0.403',
                'python': 'Python 3.14.4'}
    for name, text in expected.items():
        if installed[name] != text:
            raise RuntimeError('Unexpected %s version: %s' % (name, installed[name]))
    if not args.keep_downloads:
        for name, path in archives.items():
            if rows[name]['kind'] != 'wheel':
                path.unlink()
    manifest = {'host': facts, 'tools': {name: {'lock': rows[name], 'installed': installed[name],
                                                 'archive_kept': args.keep_downloads or rows[name]['kind'] == 'wheel'}
                                          for name in rows},
                'path': str(tools / 'bin')}
    manifest_path.write_text(json.dumps(manifest, indent=2) + '\n')
    log('Pinned tools ready in ' + str(tools / 'bin') + ':')
    for name in rows:
        log('  ' + name + ': ' + installed[name])
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
