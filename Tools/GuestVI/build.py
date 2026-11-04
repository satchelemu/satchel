#!/usr/bin/env python3
"""Build only original VI fixtures and production owner; explicit frozen inputs."""
import argparse, hashlib, json, os, platform, re, subprocess
from pathlib import Path

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def require(condition, message):
    if not condition:
        raise SystemExit(message)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--dependency-root', type=Path, required=True, help='Repository tree containing the locked Process and GuestSymbols sources')
    parser.add_argument('--cpu-root', type=Path, required=True, help='Frozen directory containing CPUProbe.cpp, CPUProbe.h, libSatchelCPUProbe.a')
    parser.add_argument('--ipc-archive', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True, help='Fresh external output directory')
    parser.add_argument('--ios', action='store_true')
    parser.add_argument('--verify-only', action='store_true')
    args = parser.parse_args()
    require(platform.system() == 'Darwin' and platform.machine() == 'arm64', 'Requires installed Apple arm64 toolchain on macOS')
    root, deps, cpu, out = (p.resolve() for p in (args.source_root, args.dependency_root, args.cpu_root, args.output))
    own = root / 'Tools/GuestVI'
    for forbidden in (root, deps, cpu):
        require(out != forbidden and forbidden not in out.parents, 'Output must be outside source/dependency roots')
    require(not out.exists(), 'Output must be a fresh directory')
    source_lock = own / 'source-lock.json'
    lock = json.loads(source_lock.read_text())
    inputs = {}
    def check(path, expected):
        path = path.resolve()
        require(path.is_file() and sha(path) == expected, 'Input hash mismatch: ' + str(path))
        inputs[str(path)] = expected
    for relative, digest in lock['files'].items():
        check(root / relative, digest)
    inputs[str(source_lock)] = sha(source_lock)
    dependencies = json.loads((own / 'dependencies.json').read_text())
    for relative, digest in dependencies['sources'].items():
        check(deps / relative, digest)
    for name, digest in dependencies['cpu'].items():
        check(cpu / name, digest)
    ipc = args.ipc_archive.resolve()
    check(ipc, dependencies['ipc_archive'])
    checkpoint = json.loads((own / 'checkpoint.json').read_text())
    for name, digest in checkpoint['production'].items():
        check(own / name, digest)
    if args.verify_only:
        print('VI locked sources and explicit dependencies verified; no build performed.')
        return
    out.mkdir()
    commands, logs, results = [], {}, {}
    env = dict(os.environ, ASAN_OPTIONS='halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1')
    def run(command, log=None):
        command = [str(value) for value in command]
        commands.append(command)
        result = subprocess.run(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        if log:
            (out / log).write_text(result.stdout)
            logs[log] = sha(out / log)
        if result.stdout:
            print(result.stdout, end='', flush=True)
        require(result.returncode == 0, 'Command failed: ' + command[0])
        return result.stdout
    toolchain = run(['xcrun', 'clang', '--version'])
    flags = ['-std=c11', '-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
             '-I', own, '-I', cpu, '-I', deps / 'Tools/GuestGraphics', '-I', deps / 'Core', '-I', deps / 'Tools/HorizonIPC']
    def compile_c(source, target, defines=()):
        run(['xcrun', 'clang', *flags, *defines, '-c', source, '-o', out / target])
    for name in ('GuestVI', 'GuestVIDispatch'):
        compile_c(own / (name + '.c'), name + '.o')
    compile_c(deps / 'Tools/GuestGraphics/GuestSymbols.c', 'GuestSymbols.o')
    compile_c(own / 'Test.c', 'Test.o')
    def link_run(name, objects, extra, expected):
        run(['xcrun', 'clang++', '-fsanitize=address,undefined', *[out / item for item in objects], *extra, '-o', out / name])
        text = run([out / name], name + '.log')
        # Preserve exact assertion counts, not an inferred zero-exit pass.
        require(re.search(r'\b' + str(expected) + r'\b', text) is not None, 'Unexpected assertion count: ' + name)
        results[name] = {'assertions': expected, 'exit_code': 0}
    link_run('normal', ['GuestVI.o', 'GuestVIDispatch.o', 'GuestSymbols.o', 'Test.o'], [cpu / 'libSatchelCPUProbe.a'], 68705)
    compile_c(own / 'GuestVI.c', 'GuestVI-fault.o', ['-Dsatchel_cpu_probe_writev=vi_test_writev', '-Dclock_gettime=vi_test_clock_gettime', '-Dcalloc=vi_test_calloc'])
    compile_c(own / 'GuestVIDispatch.c', 'GuestVIDispatch-fault.o', ['-Dsatchel_cpu_probe_get_x=vi_test_get_x'])
    compile_c(own / 'Test.c', 'Test-fault.o', ['-DVI_FAULT_TEST=1'])
    link_run('fault', ['GuestVI-fault.o', 'GuestVIDispatch-fault.o', 'GuestSymbols.o', 'Test-fault.o'], [cpu / 'libSatchelCPUProbe.a'], 70292)
    process_objects = []
    for name in ('SwitchProcess', 'ProcessMetadata', 'SwitchStorageIPC', 'SwitchResources', 'SwitchEvents'):
        compile_c(deps / 'Core' / (name + '.c'), name + '.o')
        process_objects.append(name + '.o')
    compile_c(own / 'ProcessTest.c', 'ProcessTest.o')
    link_run('joint', ['GuestVI.o', 'GuestVIDispatch.o', 'GuestSymbols.o', 'ProcessTest.o', *process_objects], [cpu / 'libSatchelCPUProbe.a', ipc], 142)
    mac = out / 'macos'; mac.mkdir()
    run(['xcrun', 'ar', 'rcs', mac / 'libSatchelGuestVI.a', out / 'GuestVI.o', out / 'GuestVIDispatch.o'])
    archives = {'macos': mac / 'libSatchelGuestVI.a'}
    if args.ios:
        sdk = run(['xcrun', '--sdk', 'iphoneos', '--show-sdk-path']).strip()
        ios = out / 'ios'; ios.mkdir()
        for name in ('GuestVI', 'GuestVIDispatch'):
            run(['xcrun', 'clang', '-target', 'arm64-apple-ios17.0', '-isysroot', sdk, '-std=c11', '-O2',
                 '-I', own, '-I', cpu, '-I', deps / 'Tools/GuestGraphics', '-c', own / (name + '.c'), '-o', ios / (name + '.o')])
        run(['xcrun', 'ar', 'rcs', ios / 'libSatchelGuestVI.a', ios / 'GuestVI.o', ios / 'GuestVIDispatch.o'])
        archives['ios'] = ios / 'libSatchelGuestVI.a'
    expected = {'_satchel_guest_vi_' + name for name in ('create', 'execute', 'retain_native', 'release_native', 'detach', 'try_destroy', 'stats', 'entry_name', 'resolve_entries', 'entry_points', 'dispatch')}
    exports = {}
    for system, archive in archives.items():
        actual = {line.strip() for line in run(['xcrun', 'nm', '-gjU', archive]).splitlines() if line.strip() and not line.strip().endswith(':')}
        require(actual == expected | ({'____asan_globals_registered'} if system == 'macos' else set()), 'Production export mismatch: ' + system)
        imports = run(['xcrun', 'nm', '-ju', archive])
        require('vi_test_' not in imports, 'Test seam in production archive')
        exports[system] = sorted(actual)
    for path, digest in inputs.items():
        require(sha(Path(path)) == digest, 'Input changed during build: ' + path)
    artifacts = {str(p.relative_to(out)): sha(p) for p in [*archives.values(), out / 'normal', out / 'fault', out / 'joint']}
    manifest = {'status': 'passed', 'scope': 'Original interpreted VI owner and real Process reservation only; no game/display proof',
                'sources': inputs, 'source_root': str(root), 'dependency_root': str(deps), 'cpu_root': str(cpu),
                'ipc_archive': str(ipc), 'toolchain': toolchain, 'commands': commands, 'results': results, 'logs': logs,
                'exports': exports, 'artifacts': artifacts, 'ios_target': 'arm64-apple-ios17.0' if args.ios else None,
                'assertion_count_note': 'Includes byte/register comparisons; not independent scenario counts.'}
    (out / 'validation-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print('VI normal/fault/joint checks passed; production archive export checks passed.')

if __name__ == '__main__':
    main()
