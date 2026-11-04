#!/usr/bin/env python3
"""Fetch the approved pinned inputs once, or verify them offline for apply day."""
import argparse
import hashlib
import json
import os
import shutil
from pathlib import Path
import subprocess
import tarfile
import xml.etree.ElementTree as ET
import zipfile

PACKAGES = [
    'microsoft.netcore.app.runtime.nativeaot.ios-arm64',
    'microsoft.netcore.app.runtime.nativeaot.osx-arm64',
    'runtime.osx-arm64.microsoft.dotnet.ilcompiler',
    'microsoft.dotnet.ilcompiler',
    'microsoft.net.illink.tasks',
    'microsoft.netcore.app.runtime.osx-arm64',
    'microsoft.aspnetcore.app.runtime.osx-arm64',
]
VERSION = '10.0.14'
REVISION = 'db66022459ffb663aa2b50f6b018bc2e124f5edf'
TARS = {
    'ios': 'b5d947b1660e6e9fed40b9cd2387e160aaab9e80b775c0cef7e14059405178c1',
    'macos': 'f95765a6229cb7b915990a2890ce12ebe36a730b021545d3d52ae69ce4c4024e',
}

def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def obtain_pinned(path, url, pin, cached=None, fetch=False):
    """Copy or fetch a file only with a previously recorded SHA-256 pin."""
    if not isinstance(pin, str) or len(pin) != 64 or any(c not in '0123456789abcdef' for c in pin):
        raise RuntimeError('Missing SHA-256 pin: ' + path.name)
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists():
        if cached is not None:
            if digest(cached) != pin:
                raise RuntimeError('Pinned SHA-256 mismatch: ' + path.name)
            shutil.copyfile(cached, path)
        elif fetch:
            temporary = path.with_name(path.name + '.partial')
            try:
                subprocess.run(['curl', '--fail', '--location', '--proto', '=https', '--proto-redir', '=https',
                                '--max-time', '600', '--silent', '--show-error', url, '--output', str(temporary)], check=True)
                if digest(temporary) != pin:
                    raise RuntimeError('Pinned SHA-256 mismatch: ' + path.name)
                temporary.replace(path)
            finally:
                temporary.unlink(missing_ok=True)
        else:
            raise RuntimeError('Missing offline input: ' + path.name)
    if digest(path) != pin:
        raise RuntimeError('Pinned SHA-256 mismatch: ' + path.name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--inputs', type=Path, required=True)
    parser.add_argument('--dotnet', type=Path, required=True)
    parser.add_argument('--fetch', action='store_true', help='Explicitly permit the approved downloads')
    args = parser.parse_args()
    root, out = args.source.resolve(), args.inputs.resolve()
    if root == out or root in out.parents:
        parser.error('Inputs must be outside the repository')
    lock = json.loads((root/'Tools/MaxwellShaderCompiler/source-lock.json').read_text())
    assert lock['runtime_version'] == VERSION
    raster = (root/'Tools/GuestGraphics/build_raster.py').read_text()
    assert REVISION in raster and all(value in raster for value in TARS.values())
    out.mkdir(parents=True, exist_ok=True)
    feed = out/'feed'; feed.mkdir(exist_ok=True)
    logs = out/'verification'; logs.mkdir(exist_ok=True)
    manifest_path = out/'manifest.json'
    manifest = json.loads(manifest_path.read_text()) if manifest_path.exists() else {'files': {}, 'complete': False}
    env = dict(os.environ, DOTNET_CLI_HOME=str(out/'cli'), NUGET_PACKAGES=str(out/'packages'),
               DOTNET_CLI_TELEMETRY_OPTOUT='1', DOTNET_GENERATE_ASPNET_CERTIFICATE='false',
               DOTNET_SKIP_FIRST_TIME_EXPERIENCE='1')
    pins = {row['cache']: row['sha256'] for row in json.loads((root/'scripts/deps/inputs.lock.json').read_text())['inputs'] if row['kind'] == 'file'}
    pins['moltenvk/MoltenVK-source.tar'] = next(row['sha256'] for row in json.loads((root/'scripts/deps/inputs.lock.json').read_text())['inputs'] if row['id'] == 'moltenvk-source')
    manifest['complete'] = False
    def save():
        manifest_path.write_text(json.dumps(manifest, indent=2)+'\n')
    def run(command, log=None):
        result = subprocess.run(list(map(str, command)), env=env, text=True, capture_output=True)
        # Keep verification status, without certificate subject or issuer names.
        diagnostic = '\n'.join('[certificate name omitted]' if 'CN=' in line else line
                               for line in (result.stdout+result.stderr).splitlines()) + '\n'
        if log: (logs/log).write_text(diagnostic)
        if result.returncode:
            raise RuntimeError('Command failed: '+str(command)+'\n'+diagnostic)
        return result.stdout
    def obtain(path, url, pin=None):
        key = path.relative_to(out).as_posix()
        obtain_pinned(path, url, pin or pins.get(key), fetch=args.fetch)
        actual = digest(path)
        previous = manifest['files'].get(key)
        if pin and actual != pin: raise RuntimeError('Pinned SHA-256 mismatch: '+key)
        if previous and previous['sha256'] != actual: raise RuntimeError('Cached input changed: '+key)
        manifest['files'][key] = {'url': url, 'size': path.stat().st_size, 'sha256': actual}
        save()
        return manifest['files'][key]
    sdk = run([args.dotnet,'--version']).strip()
    if sdk != lock['dotnet_sdk']: raise RuntimeError('Installed SDK does not match the existing pin')
    manifest['dotnet_sdk'] = sdk
    for package in PACKAGES:
        name = package+'.'+VERSION+'.nupkg'
        entry = obtain(feed/name, 'https://api.nuget.org/v3-flatcontainer/'+package+'/'+VERSION+'/'+name)
        with zipfile.ZipFile(feed/name) as archive:
            spec = ET.fromstring(archive.read(next(n for n in archive.namelist() if n.endswith('.nuspec'))))
            metadata = {node.tag.split('}')[-1]: node.text for node in spec.iter()}
            assert metadata['id'].lower() == package and metadata['version'] == VERSION
        verification = run([args.dotnet,'nuget','verify',feed/name,'--all','--configfile',root/'Tools/CapturedFlatCompiler/OfflineNuGet.Config'],name+'.log')
        if 'Signature type: Author' not in verification or 'Signature type: Repository' not in verification:
            raise RuntimeError('Required NuGet signatures absent: '+name)
        entry['verification'] = 'dotnet nuget verify --all: exit 0, author and repository signatures'
        entry['signedContentHash'] = next(line.split(': ', 1)[1] for line in verification.splitlines() if line.startswith('Content hash: '))
        entry['versionPin'] = VERSION
        save()
        print('Verified', name, flush=True)
    config = out/'LocalNuGet.Config'
    config.write_text('<configuration><packageSources><clear/><add key="approved-local" value="'+str(feed)+'"/></packageSources></configuration>\n')
    cache_project = out/'CacheInputs.csproj'
    cache_project.write_text('<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net10.0</TargetFramework></PropertyGroup><ItemGroup>'+''.join(
        '<PackageDownload Include="'+name+'" Version="['+VERSION+']"/>' for name in PACKAGES)+'</ItemGroup></Project>\n')
    run([args.dotnet,'restore',cache_project,'--artifacts-path',out/'restore/cache','--configfile',config,
         '-p:NuGetAudit=false','-m:2'],'cache-restore.log')
    # The SDK performs normal package extraction from this approved local feed only.
    for project in ['MaxwellShaderCompiler/MaxwellShaderCompiler.csproj','CapturedFlatCompiler/CapturedFlat.csproj']:
        for rid in ['osx-arm64','ios-arm64']:
            label = project.split('/')[0]+'-'+rid
            run([args.dotnet,'restore',root/'Tools'/project,'--artifacts-path',out/'restore'/label,
                 '-r',rid,'--configfile',config,'-p:_IsPublishing=true','-p:SelfContained=true','-p:NuGetAudit=false','-m:2'],label+'.log')
    def payload(path, expected, url):
        if not path.is_file() or digest(path) != expected:
            raise RuntimeError('Extracted input differs from verified archive: '+str(path))
        key = path.relative_to(out).as_posix()
        previous = manifest['files'].get(key)
        if previous and previous['sha256'] != expected:
            raise RuntimeError('Cached input manifest changed: '+key)
        manifest['files'][key] = {'url': url, 'size': path.stat().st_size, 'sha256': expected,
                                  'verification': 'Exact member of verified archive'}
    for package in PACKAGES:
        name = package+'.'+VERSION+'.nupkg'
        url = manifest['files']['feed/'+name]['url']
        with zipfile.ZipFile(feed/name) as archive:
            for member in archive.namelist():
                if member.endswith('/') or member in ('_rels/.rels','[Content_Types].xml') or member.startswith('package/services/metadata/'):
                    continue
                payload(out/'packages'/package/VERSION/member, hashlib.sha256(archive.read(member)).hexdigest(), url)
    save()
    mvk = out/'moltenvk'; mvk.mkdir(exist_ok=True)
    source = mvk/'MoltenVK-source'
    if not (source/'.git').exists():
        if not args.fetch: raise RuntimeError('Missing offline MoltenVK source')
        run(['git','init','-q',source])
        run(['git','-C',source,'fetch','--depth=1','https://github.com/KhronosGroup/MoltenVK.git',REVISION], 'moltenvk-fetch.log')
        run(['git','-C',source,'checkout','--detach','FETCH_HEAD'])
    if run(['git','-C',source,'rev-parse','HEAD']).strip() != REVISION:
        raise RuntimeError('MoltenVK revision mismatch')
    if run(['git','-C',source,'status','--porcelain']): raise RuntimeError('MoltenVK source has local changes')
    run(['git','-C',source,'fsck','--full'], 'moltenvk-fsck.log')
    source_archive = mvk/'MoltenVK-source.tar'
    if not source_archive.exists():
        run(['git','-C',source,'archive','--format=tar','--output',source_archive,REVISION])
    entry = obtain(source_archive, 'https://github.com/KhronosGroup/MoltenVK.git')
    entry.update(revision=REVISION, verification='Git object integrity, exact HEAD, clean checkout; archive generated locally')
    with tarfile.open(source_archive) as archive:
        for member in archive.getmembers():
            if member.isfile():
                payload(source/member.name, hashlib.sha256(archive.extractfile(member).read()).hexdigest(), entry['url'])
    save()
    for platform, pin in TARS.items():
        name = 'MoltenVK-'+platform+'.tar'
        entry = obtain(mvk/name, 'https://github.com/KhronosGroup/MoltenVK/releases/download/v1.4.2/'+name, pin)
        entry['verification'] = 'Existing project SHA-256 pin matched'
        save()
        destination = mvk/platform; destination.mkdir(exist_ok=True)
        with tarfile.open(mvk/name) as archive:
            if not any(destination.iterdir()): archive.extractall(destination, filter='data')
            for member in archive.getmembers():
                if member.isfile():
                    payload(destination/member.name, hashlib.sha256(archive.extractfile(member).read()).hexdigest(), entry['url'])
        print('Verified', name, flush=True)
    manifest['complete'] = True
    manifest['packages'] = {name: VERSION for name in PACKAGES}
    save()
    print('All approved inputs verified; offline package cache ready.', flush=True)

if __name__ == '__main__': main()
