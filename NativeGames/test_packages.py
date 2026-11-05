#!/usr/bin/env python3
"""Exercise the compiled app verifier with synthetic payloads and the reviewed profiles."""
from pathlib import Path
import argparse, hashlib, json, subprocess, tempfile
ROOT = Path(__file__).resolve().parents[1]
OUT = Path.home() / 'SatchelBuild/games'
REGISTRY = ROOT / 'App/NativeGameProfiles.json'
EXE = OUT / 'verify/verify-prepared-package'

def digest(data): return hashlib.sha256(data).hexdigest()

def main():
    global OUT, EXE
    parser = argparse.ArgumentParser()
    parser.add_argument('--out-root', type=Path, default=OUT)
    parser.add_argument('--package-root', type=Path, help='Verified package directory containing each reviewed package')
    args = parser.parse_args()
    OUT = args.out_root; OUT.mkdir(parents=True, exist_ok=True)
    EXE = OUT / 'verify/verify-prepared-package'
    profiles = json.loads(REGISTRY.read_text())
    reviewed = json.loads((ROOT / 'App/NativeRuntimeProfiles.json').read_text())
    cases = Path(tempfile.mkdtemp(prefix='package-tests-', dir=OUT))
    count = 0
    def check(package, expected=None):
        nonlocal count
        result = subprocess.run([EXE, package, REGISTRY, OUT / 'verify'], text=True, capture_output=True)
        assert (result.returncode == 0 if expected is None else result.returncode == 1 and expected in result.stdout), result.stdout + result.stderr
        count += 1
    for profile in profiles:
        package = cases / profile['packageName']; package.mkdir()
        files = {name: b'synthetic fixture\n' for name in profile['requiredFiles']}
        # Without --package-root the runtime profile is the reviewed text itself, so the check does
        # not depend on a private package built for one engine pin. With it, each real package's
        # profile.tsv is used, which also shows that the packager wrote the reviewed bytes.
        if args.package_root:
            source = args.package_root / profile['packageName'] / 'profile.tsv'
            files['profile.tsv'] = source.read_bytes()
        else:
            source = Path.home() / 'SatchelBuild/castlevania/package/Castlevania-1.0.0/profile.tsv'
            files['profile.tsv'] = reviewed[profile['titleID'] + ':' + profile['version']].encode()
        nros = profile.get('nroBuildIDs', {})
        if nros:
            files['dm/nro.tsv'] = (source.parent / 'dm/nro.tsv').read_bytes()
        builds = profile['buildIDs'] | nros
        for module in profile['modules'] + list(nros):
            files[f'dm/{module}.fixups.tsv'] = ('# BUILD_ID ' + builds[module] + '\n').encode()
            files[f'dm/{module}.gates.tsv'] = b'# synthetic empty gates\n'
        for name, data in files.items():
            p = package / name; p.parent.mkdir(parents=True, exist_ok=True); p.write_bytes(data)
        manifest = {k: profile[k] for k in ['titleID', 'version', 'enginePin', 'shaderChain', 'shaderInputs', 'modules', 'environment']}
        manifest.update(format=1, files={n: {'bytes': len(d), 'sha256': digest(d)} for n, d in files.items()})
        def write(value): (package / 'manifest.json').write_text(json.dumps(value))
        write(manifest); check(package); check(package)  # Full hash, then cached launch.
        (package / 'unbound.metallib').write_text('synthetic')
        check(package, 'Unbound prepared file'); (package / 'unbound.metallib').unlink()
        for key, value, error in [('titleID', '010000000000FFFF', 'no valid preparation recipe'), ('version', '99', 'no valid preparation recipe'),
                                  ('signingStatus', 'needs signature', 'needs signature'), ('modules', ['sdk', 'main'], 'reviewed profile'),
                                  ('environment', {'DM_SAVE': '/escape'}, 'reviewed profile')]:
            write(dict(manifest, **{key: value})); check(package, error)
        write(manifest)
        code = package / 'game.dylib'; data = code.read_bytes(); code.write_bytes(data.replace(b'fixture', b'changed'))
        check(package, 'digest mismatch'); code.write_bytes(data)
        layout = package / 'dm/main.fixups.tsv'; old = layout.read_bytes()
        layout.write_text('# BUILD_ID ' + '0' * 32 + '\n')
        altered = json.loads(json.dumps(manifest)); altered['files']['dm/main.fixups.tsv'] = {'bytes': layout.stat().st_size, 'sha256': digest(layout.read_bytes())}
        write(altered); check(package, 'Module build'); layout.write_bytes(old)
        altered = json.loads(json.dumps(manifest)); altered['files']['../escape'] = {'bytes': 0, 'sha256': '0' * 64}
        write(altered); check(package, 'path or digest')
        write(manifest); check(package)
        altered = json.loads(json.dumps(manifest)); altered['files']['dm/nro.tsv'] = {'bytes': 0, 'sha256': '0' * 64}
        write(altered); check(package, 'NRO inventory' if nros else 'does not support NRO')
        write(manifest)
        code.unlink(); code.symlink_to(source)
        check(package, 'path escapes')
    print(f'PASS: {count} package acceptance, cache, identity, signing, binding and path checks. Synthetic payloads only.')

if __name__ == '__main__': main()
