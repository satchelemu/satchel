#!/usr/bin/env python3
"""Rebuild renamed dependencies in an isolated durable root. No downloads or phone actions."""
import argparse
import hashlib
import json
import os
import shutil
from pathlib import Path
import subprocess
import sys
import rename_to_satchel as rename

HERE = Path(__file__).resolve().parent
SNAPSHOT = Path('tmp-snapshot-2026-11-01/tmp')

def sha(path):
    return hashlib.file_digest(path.open('rb'), 'sha256').hexdigest()

def run(command, **kwargs):
    subprocess.run(list(map(str, command)), check=True, **kwargs)

def clone(source, destination):
    if destination.exists(): return
    if not source.exists(): raise FileNotFoundError(source)
    destination.parent.mkdir(parents=True, exist_ok=True)
    run(['cp', '-cR', source, destination])

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True, help='Already renamed repository')
    parser.add_argument('--inputs', type=Path, help='Existing dependency root, read only')
    parser.add_argument('--build-inputs', type=Path, help='Existing build root, read only')
    parser.add_argument('--destination', type=Path, required=True)
    parser.add_argument('--nativeaot-packages', type=Path, help='Existing offline NativeAOT cache')
    parser.add_argument('--moltenvk-inputs', type=Path, help='Existing MoltenVK source, release tar files and extracted releases')
    parser.add_argument('--resume-parity', action='store_true', help='Resume only an unchanged validated pipeline checkpoint')
    parser.add_argument('--framework', action='append', choices=['fixture-aot','game-aot','captured','vulkan'], help='Resume selected framework recipes after a specific fix')
    parser.add_argument('--apply', action='store_true')
    parser.add_argument('--step', choices=['stage', 'cpu', 'archives', 'scalar', 'cross', 'archive', 'shader-bridge', 'frameworks', 'parity', 'all'], default='all')
    parser.add_argument('--prepared-inputs', action='store_true', help='Use freshly staged inputs in destination, without snapshot copies')
    args = parser.parse_args()
    root, dest = args.source.resolve(), args.destination.resolve()
    if not args.prepared_inputs and (args.inputs is None or args.build_inputs is None):
        parser.error('Legacy staging requires --inputs and --build-inputs')
    inputs = args.inputs.resolve() if args.inputs else dest / 'unused-inputs'
    build = args.build_inputs.resolve() if args.build_inputs else dest / 'build'
    if args.prepared_inputs and args.step in ('stage', 'all'):
        parser.error('Prepared inputs require explicit recipe steps')
    if dest == inputs or inputs in dest.parents or dest == root or root in dest.parents:
        parser.error('Destination must be separate from both source and original dependencies')
    if not (root/'project.yml').is_file(): parser.error('Missing Satchel project specification')
    print('Dependency destination:', dest, flush=True)
    if not args.apply:
        print('Dry run: stage third-party inputs and rebuild selected dependencies. Run inside NativePrepare/guard.py.')
        return
    dest.mkdir(parents=True, exist_ok=True)
    snapshot = dest/SNAPSHOT
    logs = dest/'rebuild-logs'; logs.mkdir(exist_ok=True)
    env = dict(os.environ, SATCHEL_BUILD_ROOT=str(build), SATCHEL_DEPS_ROOT=str(dest), SATCHEL_SHADER_DEPS=str(dest/'m5'), PYTHONDONTWRITEBYTECODE='1')
    def execute(label, command):
        print('Building', label, flush=True)
        with (logs/(label+'.log')).open('w') as log:
            result = subprocess.run(list(map(str, command)), cwd=root, env=env, stdout=log, stderr=subprocess.STDOUT)
        if result.returncode:
            print((logs/(label+'.log')).read_text()[-6000:])
            raise RuntimeError(label + ' failed, exit ' + str(result.returncode))
        print('Passed', label, flush=True)
    step = args.step
    if step in ('stage', 'all'):
        for name in ['NX2Apple', 'reference']: clone(inputs/name, dest/name)
        old_snapshot = inputs/SNAPSHOT
        by_name = {rename.transform(p.name): p for p in old_snapshot.iterdir()}
        integration = 'satchel-switch-integration'
        # The old integration directory also contains private extracted game outputs.
        # Copy only these explicit tool and reference directories.
        for name in ['NX2Apple', 'dotnet', 'venv', 'bin', 'nuget']:
            clone(by_name[integration]/name, snapshot/integration/name)
        vixl = snapshot/'satchel-vixl-probe/vixl'
        clone(by_name['satchel-vixl-probe']/'vixl', vixl)
        patch = root/'Tools/CPUProbe/vixl-memory-guard.patch'
        diff = subprocess.check_output(['git', '-C', str(vixl), 'diff', 'HEAD', '--'])
        transformed = rename.transform_file('Tools/CPUProbe/vixl-memory-guard.patch', diff.decode()).encode()
        if transformed != patch.read_bytes(): raise RuntimeError('Unreviewed VIXL source diff')
        if diff != patch.read_bytes():
            run(['git', '-C', vixl, 'apply', '-R', '-'], input=diff)
            run(['git', '-C', vixl, 'apply', patch])
        assert subprocess.check_output(['git','-C',str(vixl),'diff','HEAD','--']) == patch.read_bytes()
        clone(build/'m5/spirv-cross', snapshot/'satchel-shader-bridge/spirv-cross')
        for name in ['spirv-cross', 'zstd']: clone(build/'m5'/name, dest/'m5'/name)
    if step in ('cpu', 'all'):
        execute('cpu', [sys.executable, '-B', root/'Tools/CPUProbe/build.py', '--source', snapshot/'satchel-vixl-probe/vixl', '--output', snapshot/'satchel-vixl-probe/build', '--ios'])
    if step in ('archives', 'all'):
        # Production compilation only. Historical host/GPU gates are not relabelled as rerun.
        # Validate each exported C ABI against the reviewed renamed archive inventory.
        recipes = json.loads((HERE/'archive-recipes.json').read_text())
        sdk = subprocess.check_output(['xcrun', '--sdk', 'iphoneos', '--show-sdk-path'], text=True).strip()
        include = [root/'Core', *[p for p in (root/'Tools').iterdir() if p.is_dir()]]
        incflags = [x for p in include for x in ['-I', str(p)]]
        results = []
        for recipe in recipes:
            library = snapshot/recipe['artifact']; library.parent.mkdir(parents=True, exist_ok=True)
            objects = []; commands = []
            for name in recipe['sources']:
                source = root/name; obj = library.parent/(library.stem+'-'+source.stem+'.o'); objects.append(obj)
                command = ['xcrun','clang','-std=c11','-O2','-Wall','-Wextra','-Werror','-target','arm64-apple-ios17.0','-isysroot',sdk,*incflags,'-c',str(source),'-o',str(obj)]
                execute(library.stem+'-'+source.stem, command); commands.append(command)
            execute(library.stem, ['xcrun','libtool','-static','-o',library,*objects])
            symbols = subprocess.check_output(['nm','-gU',str(library)], text=True)
            actual = sorted({line.split()[-1] for line in symbols.splitlines() if len(line.split())==3 and line.split()[1] in ['T','D','S','B','C']})
            if actual != recipe['exports']:
                raise RuntimeError('Archive ABI mismatch: '+recipe['artifact']+' missing='+str(set(recipe['exports'])-set(actual))+' added='+str(set(actual)-set(recipe['exports'])))
            results.append(dict(artifact=recipe['artifact'],sha256=sha(library),sources={name:sha(root/name) for name in recipe['sources']},commands=commands,exports=actual))
            (logs/'archives.json').write_text(json.dumps(results,indent=2)+'\n')
    if step in ('scalar', 'all'):
        execute('scalar', [sys.executable,'-B',root/'Tools/ScalarCompiler/build.py','--ios','--output',snapshot/'satchel-scalar-compiler'])
    if step in ('cross','archive','all'):
        for name in ['cross','archive']:
            if step not in (name,'all'): continue
            for platform in ['macosx','iphoneos']:
                execute(name+'-'+platform, [sys.executable,'-B',root/'NativeShaders'/('build-'+name+'.py'),platform])
    if step in ('shader-bridge','all'):
        execute('shader-bridge', [sys.executable,'-B','-c', "import runpy, pathlib, sys, os; sys.argv=['build.py','--ios']; d=runpy.run_path('Tools/ShaderBridge/build.py'); d['main'].__globals__['WORK']=pathlib.Path(os.environ['SATCHEL_DEPS_ROOT'])/'tmp-snapshot-2026-11-01/tmp/satchel-shader-bridge'; d['main']()" ])
    if step in ('frameworks', 'all'):
        packages = dest/'m5/aot/packages'
        if args.nativeaot_packages:
            # Resume a partial cache without replacing existing package bytes.
            for package in args.nativeaot_packages.resolve().iterdir():
                if package.is_dir():
                    for version in package.iterdir(): clone(version, packages/package.name/version.name)
        elif (build/'m5/aot/packages').exists(): clone(build/'m5/aot/packages', packages)
        if args.moltenvk_inputs:
            for name in ['MoltenVK-source','MoltenVK-ios.tar','MoltenVK-macos.tar','ios','macos']:
                clone(args.moltenvk_inputs.resolve()/name, snapshot/'satchel-vulkan-backend'/name)
        required = [packages/'microsoft.netcore.app.runtime.nativeaot.ios-arm64/10.0.14',
                    packages/'runtime.osx-arm64.microsoft.dotnet.ilcompiler/10.0.14',
                    snapshot/'satchel-vulkan-backend/MoltenVK-source',
                    snapshot/'satchel-vulkan-backend/MoltenVK-ios.tar',
                    snapshot/'satchel-vulkan-backend/MoltenVK-macos.tar']
        absent = [str(p) for p in required if not p.exists()]
        if absent: raise RuntimeError('Missing offline third-party inputs: ' + ', '.join(absent))
        integration = snapshot/'satchel-switch-integration'
        env['NUGET_PACKAGES'] = str(packages)
        env['SATCHEL_NUGET_CONFIG'] = str(root/'Tools/CapturedFlatCompiler/OfflineNuGet.Config')
        compiler = [sys.executable,'-B',root/'Tools/MaxwellShaderCompiler/build.py','--dotnet',integration/'dotnet/dotnet']
        selected = args.framework or ['fixture-aot','game-aot','captured','vulkan']
        # The original fixture keeps the reviewed NX2Apple checkout; the game translator follows the engine pin.
        if 'fixture-aot' in selected:
            execute('fixture-aot', [*compiler,'--checkout',integration/'NX2Apple','--work',snapshot/'satchel-shader-aot','--original-fixture'])
        if 'game-aot' in selected: execute('game-aot', [*compiler,'--checkout',dest/'NX2Apple/engine','--work',dest/'m5/aot'])
        captured = dest/'rebuild/captured'
        if 'captured' in selected:
            execute('captured', [sys.executable,'-B',root/'Tools/CapturedFlatCompiler/build.py','--renderer-root',root,'--dotnet',integration/'dotnet/dotnet','--packages',packages,'--out',captured])
            shutil.copytree(captured/'SatchelCapturedFlat.framework',snapshot/'satchel-captured-flat/SatchelCapturedFlat.framework',dirs_exist_ok=True)
        if 'vulkan' in selected: execute('vulkan', [sys.executable,'-B',HERE/'build_vulkan_framework.py','--source',root,'--dependencies',dest])
    if step in ('parity', 'all'):
        execute('translation-harness', [sys.executable,'-B',root/'NativeShaders/build-translation.py','macosx'])
        execute('pipeline-parity', [sys.executable,'-B',root/'NativeShaders/guard.py','--light',sys.executable,'-B',root/'NativeShaders/check-pipeline.py','--label','pipeline-full',*(['--resume'] if args.resume_parity else [])])
        proof = json.loads((dest/'m5/pipeline-full/result.json').read_text())
        assert proof.get('complete') is True and proof.get('differences') in (0, []), 'Rebuilt shader parity failed'
    if step == 'all':
        missing = [name for name in json.loads((HERE/'dependency-artifacts.json').read_text()) if not (dest/name).exists()]
        if missing:
            (logs/'missing-artifacts.json').write_text(json.dumps(missing, indent=2)+'\n')
            raise RuntimeError('Dependency rebuild incomplete: ' + ', '.join(missing))
    print('Selected dependency steps complete. Framework and app validation are separate gates.', flush=True)

if __name__ == '__main__': main()
