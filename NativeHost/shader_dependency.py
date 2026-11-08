"""Stage the M5 native translator's validated sources and libraries for this build."""
from pathlib import Path
import hashlib
import json
import os
import plistlib
import shutil
import subprocess
import sys
import runpy
DEPENDENCY_PATHS = runpy.run_path(str(Path(__file__).resolve().parents[1] / 'scripts/dependency_paths.py'))

SOURCES = [Path(p).stem for p in json.loads((Path(__file__).resolve().parents[1]/'NativeShaders/integration.json').read_text())['swiftSources']]

def stage(root, output, platform='iphoneos'):
    # The checkout being built carries the translator sources; SATCHEL_SHADER_SOURCE names another one.
    if os.environ.get('SATCHEL_SHADER_SOURCE'):
        source = Path(os.environ['SATCHEL_SHADER_SOURCE'])
    elif (root/'NativeShaders/ShaderAPI.swift').exists():
        source = root
    else:
        raise RuntimeError('No NativeShaders sources in ' + str(root) + ': set SATCHEL_SHADER_SOURCE to a Satchel checkout that has them')
    # The renamed shader products live in the dependency root since the 2026-11-03 rename.
    products = DEPENDENCY_PATHS['shader_deps']()
    sys.path.insert(0, str(root / 'NativeShaders'))
    from public_parity import validate_proof
    # Default: the public proof when this dependency root has one (fresh bootstrap),
    # otherwise the maintainer's private corpus result. One of them must exist.
    default = 'public' if (products / 'public-parity/result.json').is_file() else 'private'
    if default == 'private' and not (products / 'pipeline-full/result.json').is_file():
        raise RuntimeError('No shader parity proof in ' + str(products) + ': run the public parity check (NativeShaders/PublicParity.md)')
    mode = os.environ.get('SATCHEL_SHADER_PARITY', default)
    if mode not in ('public', 'private'):
        raise RuntimeError('SATCHEL_SHADER_PARITY must be public or private')
    validation = validate_proof(products, source) if mode == 'public' else {'kind': 'private-maintainer'}
    # The legacy private corpus remains an explicit maintainer gate. Supplying it
    # in public mode requires both gates to pass, never substitutes for public proof.
    private = os.environ.get('SATCHEL_SHADER_PRIVATE_PROOF')
    # Where the maintainer's private corpus result exists, keep requiring it too.
    if not private and (products / 'pipeline-full/result.json').is_file():
        private = str(products / 'pipeline-full/result.json')
    if mode == 'private' or private:
        proof = json.loads((Path(private) if private else products / 'pipeline-full/result.json').read_text())
        if proof.get('complete') is not True:
            raise RuntimeError('M5 native translation parity is incomplete')
        if proof.get('differences') not in (0, []):
            raise RuntimeError('M5 native translation parity has differences')
        validation['private_maintainer_gate'] = 'passed'
    destination = output/'native-shaders'; destination.mkdir(parents=True, exist_ok=True)
    # The promoted M5 translator now references this helper even when the host
    # requests only vertex/pixel stages. Keep the staged source closure complete.
    if 'ShaderGeometry.' in (source/'NativeShaders/Translation.swift').read_text() and 'Geometry' not in SOURCES:
        SOURCES.append('Geometry')
    files = [source/'NativeShaders'/(name+'.swift') for name in SOURCES]
    files += [source/'Tools/MaxwellShaderCompiler/SatchelGameShader.h', source/'Tools/ShaderBridge/GameShaderBridge.h', source/'Tools/ShaderBridge/ArchiveBridge.h', source/'Tools/ShaderBridge/RuntimeShaderBridge.h']
    hashes = {}
    for file in files:
        shutil.copyfile(file, destination/file.name)
        hashes[str(file.relative_to(source))] = hashlib.sha256(file.read_bytes()).hexdigest()
    library = products/('cross-'+platform)/'libGameShaderBridge.a'
    shutil.copyfile(library, destination/library.name)
    hashes[library.name] = hashlib.sha256(library.read_bytes()).hexdigest()
    archive = products/('archive-'+platform)/'libShaderArchive.a'
    shutil.copyfile(archive, destination/archive.name)
    hashes[archive.name] = hashlib.sha256(archive.read_bytes()).hexdigest()
    if platform == 'iphoneos':
        framework = products/'aot/SatchelShaderProbe.framework'
        # Remove only our obsolete staged copy, which would shadow the original
        # fixture framework through Xcode's framework search path.
        obsolete = destination/'SatchelShaderProbe.framework'
        if obsolete.exists(): shutil.rmtree(obsolete)
        # The existing diagnostic uses the original fixture-only framework.
        # Give the independent game translator its own dyld image identity.
        staged = destination/'SatchelShaderTranslator.framework'
        shutil.copytree(framework, staged, dirs_exist_ok=True)
        (staged/'SatchelShaderProbe').replace(staged/'SatchelShaderTranslator')
        subprocess.run(['install_name_tool', '-id', '@rpath/SatchelShaderTranslator.framework/SatchelShaderTranslator',
                        staged/'SatchelShaderTranslator'], check=True)
        info = plistlib.loads((staged/'Info.plist').read_bytes())
        info.update(CFBundleExecutable='SatchelShaderTranslator', CFBundleName='SatchelShaderTranslator',
                    CFBundleIdentifier='com.dell.satchel.shader-translator')
        (staged/'Info.plist').write_bytes(plistlib.dumps(info))
        module = staged/'Modules/module.modulemap'
        module.write_text(module.read_text().replace('framework module SatchelShaderProbe', 'framework module SatchelShaderTranslator'))
        hashes['SatchelShaderProbe'] = hashlib.sha256((framework/'SatchelShaderProbe').read_bytes()).hexdigest()
        hashes['SatchelShaderTranslator'] = hashlib.sha256((staged/'SatchelShaderTranslator').read_bytes()).hexdigest()
    notices = destination/'Notices'; notices.mkdir(exist_ok=True)
    for folder in [source/'NativeShaders/Notices', source/'Tools/MaxwellShaderCompiler/licenses']:
        if folder.exists():
            for file in folder.iterdir():
                if file.is_file(): shutil.copyfile(file, notices/file.name)
    shutil.copyfile(products/('cross-'+platform)/'SPIRV-Cross.LICENSE', notices/'SPIRV-Cross.LICENSE')
    if mode == 'public':
        # Detect source/product edits during staging as well as before copying.
        if validate_proof(products, source) != {k: v for k, v in validation.items() if k != 'private_maintainer_gate'}:
            raise RuntimeError('Public shader proof changed during staging')
    (destination/'source-manifest.json').write_text(json.dumps({'source': str(source), 'products': str(products), 'files': hashes, 'validation': validation}, indent=2)+'\n')
    (destination/'Bridge.h').write_text('#include "SatchelGameShader.h"\n#include "GameShaderBridge.h"\n#include "ArchiveBridge.h"\n#include "RuntimeShaderBridge.h"\n')
    return destination
