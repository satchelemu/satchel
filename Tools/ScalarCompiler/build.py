#!/usr/bin/env python3
"""Build and validate the bounded original scalar compiler; no downloads."""
import argparse
import hashlib
import json
import os
import pathlib
import platform
import re
import shutil
import subprocess

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def capture(args):
    return subprocess.check_output([str(a) for a in args], text=True).strip()


def run(args, **kwargs):
    subprocess.run([str(a) for a in args], check=True, cwd=HERE, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=pathlib.Path, default=pathlib.Path('/tmp/satchel-scalar-compiler'))
    parser.add_argument('--ios', action='store_true', help='Also compile the arm64 iOS static archive; no installation')
    args = parser.parse_args()
    if platform.system() != 'Darwin' or platform.machine() != 'arm64':
        parser.error('This verified native toolchain requires macOS arm64.')
    output = args.output.resolve()
    if output == ROOT or ROOT in output.parents:
        parser.error('Build outputs must remain outside the source repository.')
    lock = json.loads((HERE / 'source-lock.json').read_text())
    if lock.get('schema_version') != 1:
        parser.error('Unsupported source lock schema.')
    for name, expected in lock['source_sha256'].items():
        if digest(HERE / name) != expected:
            parser.error('Source checkpoint mismatch: ' + name)
    compiler = capture(['xcrun', '--find', 'clang'])
    compiler_version = capture([compiler, '--version'])
    if 'Apple clang version' not in compiler_version or 'arm64-' not in compiler_version:
        parser.error('The selected compiler is not the verified Apple arm64 toolchain.')
    validator = shutil.which('spirv-val')
    if not validator:
        parser.error('Installed spirv-val is required; no dependency is downloaded by this build.')
    validator_version = capture([validator, '--version'])
    if 'SPIR-V 1.3 (under Vulkan 1.1 semantics)' not in validator_version:
        parser.error('The installed SPIR-V validator lacks the required Vulkan1.1 target.')
    output.mkdir(parents=True, exist_ok=True)
    fixtures = output / 'fixtures'
    fixtures.mkdir(exist_ok=True)
    common = ['-std=c11', '-Wall', '-Wextra', '-Werror', '-O1', '-g']
    sanitizer = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    manifest = {'schema_version': 1, 'source_lock_sha256': digest(HERE / 'source-lock.json'),
                'builder_sha256': digest(HERE / 'build.py'), 'sources': lock['source_sha256'],
                'toolchain': {'clang': compiler_version, 'spirv_val': validator_version},
                'validation': {}, 'archives': {}}

    def build(name, flags):
        folder = output / name
        folder.mkdir(exist_ok=True)
        obj = folder / 'ScalarCompiler.o'
        archive = folder / 'libSatchelScalarCompiler.a'
        run([compiler, *common, *flags, '-c', 'ScalarCompiler.c', '-o', obj])
        run(['xcrun', 'libtool', '-static', '-o', archive, obj])
        symbols = capture(['xcrun', 'nm', '-gU', archive])
        expected = {'_satchel_scalar_admit', '_satchel_scalar_compile', '_satchel_scalar_compile_program'}
        actual = {line.split()[-1] for line in symbols.splitlines() if line and '_satchel_scalar_' in line}
        if actual != expected:
            raise RuntimeError('Unexpected scalar compiler public exports')
        (folder / 'exports.txt').write_text(symbols + '\n')
        manifest['archives'][name] = {'sha256': digest(archive), 'bytes': archive.stat().st_size,
                                      'exports': sorted(actual), 'flags': common + flags}
        return archive

    host_flags = ['-isysroot', capture(['xcrun', '--sdk', 'macosx', '--show-sdk-path']), *sanitizer]
    manifest['toolchain']['macosx_sdk'] = capture(['xcrun', '--sdk', 'macosx', '--show-sdk-version'])
    host = build('macos', host_flags)
    env = dict(os.environ, ASAN_OPTIONS='halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1')
    actual_checks = {}
    for source, name, params in [('test.c', 'compiler-tests', [fixtures]),
                                  ('admission_test.c', 'admission-tests', [])]:
        binary = output / 'macos' / name
        run([compiler, *common, *host_flags, source, host, '-o', binary])
        result = subprocess.run([str(binary), *map(str, params)], check=True, env=env,
                                text=True, stdout=subprocess.PIPE)
        print(result.stdout, end='')
        (output / (name + '.log')).write_text(result.stdout)
        match = re.search(r': (\d+) ASan/UBSan assertions;', result.stdout)
        key = 'compiler' if source == 'test.c' else 'admission'
        if not match or int(match.group(1)) != lock['expected_checks'][key]:
            raise RuntimeError('Validation count differs from the retained checkpoint: ' + key)
        actual_checks[key] = int(match.group(1))
    for name, expected in lock['original_fixture_sha256'].items():
        if digest(fixtures / name) != expected:
            raise RuntimeError('Original synthetic fixture changed: ' + name)
    for i in range(4):
        run([validator, '--target-env', 'vulkan1.1', fixtures / ('program%d.spv' % i)])
    manifest['validation'] = {'asan_ubsan': True, 'checks': actual_checks,
                              'original_programs': 4, 'fixture_hashes_match': True,
                              'spirv_target': 'vulkan1.1'}
    if args.ios:
        sdk = capture(['xcrun', '--sdk', 'iphoneos', '--show-sdk-path'])
        manifest['toolchain']['iphoneos_sdk'] = capture(['xcrun', '--sdk', 'iphoneos', '--show-sdk-version'])
        build('ios', ['-target', 'arm64-apple-ios17.0', '-isysroot', sdk])
    (output / 'validation-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print('Validated scalar compiler artifacts:', output)


if __name__ == '__main__':
    main()
