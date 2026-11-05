"""Bind locally built shader products to their checked source inputs."""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def inputs(component, root=ROOT):
    if component == 'aot':
        folder = root / 'Tools/MaxwellShaderCompiler'
        files = [p for p in folder.iterdir() if p.suffix in ('.cs', '.csproj', '.h', '.json', '.py', '.Config')]
    else:
        name = 'GameShaderBridge' if component == 'cross' else 'ArchiveBridge'
        files = list((root / 'Tools/ShaderBridge').glob(name + '.*'))
        files += [root / 'NativeShaders' / ('build-' + component + '.py')]
    files += [root / 'NativeShaders/build_receipt.py']
    return {str(p.relative_to(root)): digest(p) for p in sorted(files)}


def record(component, directory, products, before):
    if inputs(component) != before:
        raise RuntimeError('Shader sources changed during build')
    value = {'format': 1, 'component': component, 'sources': before,
             'products': {name: digest(directory / name) for name in products}}
    (directory / 'build-receipt.json').write_text(json.dumps(value, indent=2) + '\n')


def verify(component, directory, products, root=ROOT):
    value = json.loads((directory / 'build-receipt.json').read_text())
    expected = {'format': 1, 'component': component, 'sources': inputs(component, root),
                'products': {name: digest(directory / name) for name in products}}
    if value != expected:
        raise RuntimeError('Shader build receipt mismatch: ' + component)
