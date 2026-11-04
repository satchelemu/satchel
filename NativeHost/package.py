#!/usr/bin/env python3
"""Pack private Mac-prepared artifacts. Never copies game content into the worktree."""
from pathlib import Path
import argparse, hashlib, json, os, re, runpy, shlex, shutil, struct, subprocess, sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "NativeGames"))
from registry import profile as reviewed_profile
from heavy import wait_for_slot
from build import ENGINE, GAME, OUT, PIN, ZELDA, heavy_guard, run

CHAIN = 'e65a4ebd3322'
MODULES = ['main', 'subsdk0', 'sdk']
IDENTITY = runpy.run_path(str(Path(__file__).resolve().parents[1] / 'NativeApp/build_support.py'))['mac_signing_identity'](Path(__file__).resolve().parents[1])
PROFILE = reviewed_profile(GAME, '1.4.3')
PROFILE_DIR = ZELDA / 'profiles' / GAME
REWRITTEN = ZELDA / 'rewritten'
ROMFS = ZELDA / 'work/extract/v720896/RomFS.img'
SHADERS = ZELDA / f'work/shader/ios/b-{CHAIN}'
SHADER_REPORT = ZELDA / f'work/shader/cache/b-{CHAIN}/_b_build.json'


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda: f.read(4 * 1024 * 1024), b''): h.update(chunk)
    return h.hexdigest()

def clone(src, dst):
    dst.parent.mkdir(parents=True, exist_ok=True)
    if dst.exists(): raise ValueError('Refusing to overwrite ' + str(dst))
    run(['cp', '-c', src, dst])

def image_sections(path):
    """Validate the thin dylib's file bounds, constructor absence and section protections."""
    with path.open('rb') as f:
        header = f.read(32)
        magic, cpu, sub, kind, count, size, flags, reserved = struct.unpack('<8I', header)
        assert magic == 0xfeedfacf and cpu == 0x100000c and kind == 6
        assert count < 4096 and size <= path.stat().st_size - 32
        commands = f.read(size)
    at, sections = 0, {}
    for _ in range(count):
        cmd, length = struct.unpack_from('<II', commands, at)
        assert length >= 8 and at + length <= len(commands)
        assert cmd not in (0x11, 0x1a, 0x8000001c), 'Constructor or rpath in dylib'
        if cmd in (0xc, 0x80000018, 0x8000001f, 0x80000023):
            name_at = struct.unpack_from('<I', commands, at + 8)[0]
            assert name_at < length
            name = commands[at+name_at:at+length].split(b'\0')[0]
            assert name == b'/usr/lib/libSystem.B.dylib', 'Unexpected dependency'
        if cmd == 0x19:
            _, _, seg, vmaddr, vmsize, offset, filesize, maxprot, prot, nsect, sgflags = struct.unpack_from('<II16s4Q4I', commands, at)
            assert prot & 6 != 6 and offset + filesize <= path.stat().st_size
            assert 72 + 80 * nsect <= length
            for i in range(nsect):
                row = struct.unpack_from('<16s16sQQ8I', commands, at + 72 + i * 80)
                name, segname, addr, section_size, file_offset, align, reloff, nreloc, sflags, *_ = row
                assert sflags & 255 not in (9, 10, 0x15, 0x16), 'Initializer/destructor section'
                name = name.rstrip(b'\0').decode(); segname = segname.rstrip(b'\0').decode()
                assert addr >= vmaddr and addr + section_size <= vmaddr + vmsize
                if sflags & 255 not in (1, 0xc, 0x12): assert file_offset + section_size <= offset + filesize
                sections[(segname, name)] = {'offset': file_offset, 'bytes': section_size, 'address': addr, 'protection': prot}
        at += length
    assert at == len(commands)
    return sections

def compare_sections(dylib, sections, rewritten):
    """Compare every packed byte with gen_embed's split, including zero tail padding."""
    result = {}
    with dylib.open('rb') as packed:
        for mod in MODULES:
            layout = dict(line[2:].split() for line in (rewritten / (mod + '.fixups.tsv')).read_text().splitlines())
            r16 = lambda n: (n + 0x3fff) & ~0x3fff
            code = rewritten / (mod + '.patched.bin'); size = code.stat().st_size
            text_end = r16(int(layout['TEXT_END'], 16)); patch = int(layout['PATCH_OFF'], 16)
            if patch < 0:
                file_end = int(layout['FILE_IMAGE_LEN'], 16); back = int(layout.get('BACK_LEN', '0'), 16)
                parts = [(0, text_end), (text_end, file_end), (file_end, r16(size) - back)]
                if back: parts.append((r16(size) - back, r16(size)))
            else: parts = [(0, text_end), (text_end, patch), (patch, r16(size))]
            names = [('__TEXT', f'__dm{mod}x0'), (f'__DM{mod}W', '__img'), ('__TEXT', f'__dm{mod}x1'), ('__TEXT', f'__dm{mod}x2')]
            result[mod] = {'layout': layout, 'pieces': []}
            with code.open('rb') as original:
                for i, ((lo, hi), name) in enumerate(zip(parts, names)):
                    section = sections[name]
                    assert section['bytes'] == hi - lo and section['address'] % 0x4000 == 0
                    assert section['protection'] == (3 if i == 1 else 5)
                    original.seek(lo); packed.seek(section['offset']); left = hi - lo; position = lo
                    while left:
                        n = min(left, 4 * 1024 * 1024)
                        expected = original.read(min(n, max(0, size - position)))
                        expected += bytes(n - len(expected))
                        assert packed.read(n) == expected, 'Packed section mismatch: ' + mod
                        left -= n; position += n
                    result[mod]['pieces'].append({'segment': name[0], 'section': name[1], **section})
    return result

def reviewed_nros(rewritten):
    """Bind the loader's complete dynamic inventory to the reviewed profile."""
    table = rewritten / 'nro.tsv'
    expected = PROFILE.get('nroInventorySHA256')
    builds = PROFILE.get('nroBuildIDs', {})
    if not expected:
        assert not table.exists() and not builds, 'NRO inventory needs explicit package support before accepting this content'
        return []
    assert table.is_file() and digest(table) == expected, 'NRO inventory differs from reviewed profile'
    rows = []
    for line in table.read_text().splitlines():
        if not line or line.startswith('#'): continue
        fields = line.split()
        assert len(fields) >= 3 and re.fullmatch(r'[0-9a-f]{64}', fields[1])
        assert 0 < int(fields[2]) <= 0xffffffff
        name = fields[0]
        assert re.fullmatch(r'nro[0-9]+', name) and len(name) < 16, 'Unsupported NRO inventory row'
        assert name not in rows and name not in MODULES
        layout = dict(line[2:].split() for line in (rewritten / (name + '.fixups.tsv')).read_text().splitlines())
        assert layout['BUILD_ID'].lower() == builds.get(name, '').lower(), 'NRO build is not reviewed'
        rows.append(name)
    assert rows and set(rows) == set(builds), 'Incomplete reviewed NRO inventory'
    return rows


def compare_nro_sections(dylib, sections, rewritten):
    """Verify the packed NRO table and every executable and writable byte."""
    names = reviewed_nros(rewritten)
    if not names: return {}
    table = sections[('__DATA', '__dm_nro_tab')]
    assert table['bytes'] == len(names) * 80 and table['protection'] == 3
    section_names = [('__TEXT', '__dm_nro_x0'), ('__DM_NRO_W', '__img'), ('__TEXT', '__dm_nro_x1')]
    offsets = [0, 0, 0]
    result = {}
    with dylib.open('rb') as packed:
        for number, name in enumerate(names):
            layout = dict(line[2:].split() for line in (rewritten / (name + '.fixups.tsv')).read_text().splitlines())
            code = rewritten / (name + '.patched.bin'); size = code.stat().st_size
            r16 = lambda n: (n + 0x3fff) & ~0x3fff
            text_end = r16(int(layout['TEXT_END'], 16)); patch = int(layout['PATCH_OFF'], 16)
            assert int(layout.get('BACK_LEN', '0'), 16) == 0, 'Packed NRO cannot have a back patch area'
            if patch < 0:
                file_end = int(layout['FILE_IMAGE_LEN'], 16)
                parts = [(0, text_end), (text_end, file_end), (file_end, r16(size))]
            else: parts = [(0, text_end), (text_end, patch), (patch, r16(size))]
            packed.seek(table['offset'] + number * 80)
            row = struct.unpack('<32s6Q', packed.read(80))
            assert row[0] == name.encode().ljust(32, b'\0'), 'NRO table name mismatch'
            pieces = []
            with code.open('rb') as original:
                for i, ((lo, hi), key) in enumerate(zip(parts, section_names)):
                    section = sections[key]; length = hi - lo
                    assert row[1 + i * 2:3 + i * 2] == (offsets[i], length), 'NRO table range mismatch'
                    assert lo >= 0 and length >= 0 and offsets[i] + length <= section['bytes']
                    assert section['address'] % 0x4000 == 0 and section['protection'] == (3 if i == 1 else 5)
                    original.seek(lo); packed.seek(section['offset'] + offsets[i]); left = length; position = lo
                    while left:
                        n = min(left, 4 * 1024 * 1024)
                        expected = original.read(min(n, max(0, size - position)))
                        expected += bytes(n - len(expected))
                        assert packed.read(n) == expected, 'Packed NRO section mismatch: ' + name
                        left -= n; position += n
                    pieces.append({'segment': key[0], 'section': key[1], 'offset': offsets[i], 'bytes': length})
                    offsets[i] += length
            result[name] = {'layout': layout, 'pieces': pieces}
    assert all(sections[key]['bytes'] == offsets[i] for i, key in enumerate(section_names)), 'Unbound NRO section bytes'
    return result


def build_dylib():
    wait_for_slot(OUT)
    work = OUT / 'container'; work.mkdir(exist_ok=True)
    rewritten = REWRITTEN
    reviewed_nros(rewritten)
    flags = subprocess.check_output(['python3', ENGINE / 'dm/ios/tools/gen_embed.py', rewritten, work / 'embed.S', *MODULES], text=True).strip()
    (work / 'marker.c').write_text('__attribute__((visibility("default"))) const char satchel_game_container_v1[] = "Satchel container 1";\n')
    sdk = subprocess.check_output(['xcrun', '--sdk', 'iphoneos', '--show-sdk-path'], text=True).strip()
    dylib = work / 'game.dylib'
    run(['nice', '-n', '10', 'xcrun', 'clang', '-target', 'arm64-apple-ios26.0', '-isysroot', sdk, '-dynamiclib', '-O1',
         '-Wl,-install_name,@rpath/SatchelPreparedGame.dylib', '-Wl,-no_fixup_chains',
         *shlex.split(flags), work / 'marker.c', work / 'embed.S', '-o', dylib])
    run(['codesign', '--force', '--sign', IDENTITY, '--identifier', 'com.dell.satchel.dev.game', '--timestamp=none', dylib])
    run(['codesign', '--verify', '--strict', '--verbose=2', dylib])
    signing = subprocess.run(['codesign', '-dvvv', dylib], capture_output=True, text=True, check=True).stderr
    team = re.search(r'TeamIdentifier=(\w+)', signing).group(1)
    expected = runpy.run_path(str(Path(__file__).resolve().parents[1] / 'NativeApp/build_support.py'))['signing_team'](Path(__file__).resolve().parents[1], required=False)
    assert expected is None or team == expected, 'dylib signed by team ' + team + ', expected ' + expected
    sections = image_sections(dylib)
    layout = compare_sections(dylib, sections, rewritten)
    layout.update(compare_nro_sections(dylib, sections, rewritten))
    (work / 'layout.json').write_text(json.dumps(layout, indent=2) + '\n')
    print('Signed constructor-free dylib: all module sections match the rewritten inputs.')
    return dylib

def pack(dylib):
    profile = PROFILE_DIR
    env = dict(os.environ, GAME=GAME, DM_GAME=GAME, DM_GAMES_DIR=str(PROFILE_DIR.parent), DM_GAMES_FIRST='1', PYTHONDONTWRITEBYTECODE='1')
    # Never stamp a partial shader directory as a usable package.
    report = SHADER_REPORT
    if not report.exists(): raise SystemExit('Package blocked: shader completion report is missing: ' + str(report))
    rows = json.loads(report.read_text())
    assert rows and all(r['st'] == 'ok' for r in rows), 'Shader build is incomplete'
    libs = SHADERS
    inputs = subprocess.check_output(['python3', ENGINE / 'dm/gfx/tools/dmsh_chain.py', '--inputs'], env=env, text=True).strip()
    stamp = dict(line.split('\t') for line in (libs / 'inputs.tsv').read_text().splitlines() if line and not line.startswith('#'))
    assert stamp['game'] == GAME and stamp['inputs'] == inputs == PROFILE['shaderInputs']
    for name in ['c1_full.tsv', 'cb_extent.tsv']:
        assert (profile / name).read_text().splitlines()[0].startswith(f'# chain b-{CHAIN} inputs {inputs} ')
    assert len(list(libs.glob('*_b.metallib'))) == len(rows), 'Missing base iOS shader libraries'
    package = OUT / 'package' / PROFILE['packageName']
    if package.exists(): raise SystemExit('Package destination already exists; inspect it before rebuilding: ' + str(package))
    package.mkdir(parents=True)
    clone(dylib, package / 'game.dylib')
    run(['python3', ENGINE / 'dm/games/game.py', 'profile', GAME, package / 'profile.tsv'], env=env)
    nros = reviewed_nros(REWRITTEN)
    if nros: clone(REWRITTEN / 'nro.tsv', package / 'dm/nro.tsv')
    for mod in MODULES + nros:
        for suffix in ['fixups.tsv', 'gates.tsv']:
            clone(REWRITTEN / f'{mod}.{suffix}', package / 'dm' / f'{mod}.{suffix}')
    for name in ['shader_map.tsv', 'pso_manifest.tsv', 'c1_full.tsv', 'cb_extent.tsv', 'shader_pairs.tsv', 'pso_pairs.tsv']:
        if (profile / name).exists(): clone(profile / name, package / 'dm' / name)
    # One recursive clone avoids launching a process per shader artifact.
    assert all(p.is_file() and not p.is_symlink() and (p.suffix in ('.metallib', '.json') or p.name == 'inputs.tsv') for p in libs.iterdir())
    (package / 'msl').mkdir()
    run(['cp', '-cR', libs, package / 'msl'])
    romfs = ROMFS
    with romfs.open('rb') as f: assert f.read(8) == b'DMROMFS1', 'Expected composed DMROMFS1 image'
    clone(romfs, package / 'romfs.img')
    files = {str(p.relative_to(package)): {'bytes': p.stat().st_size, 'sha256': digest(p)} for p in sorted(package.rglob('*')) if p.is_file()}
    manifest = {'format': 1, 'signingStatus': 'signed', 'titleID': GAME, 'version': PROFILE['version'], 'enginePin': PIN, 'forkPatchCount': 34,
                'shaderChain': CHAIN, 'shaderInputs': inputs, 'modules': MODULES,
                'environment': PROFILE['environment'],
                'layout': json.loads((OUT / 'container/layout.json').read_text()), 'files': files}
    assert files['profile.tsv']['sha256'] == PROFILE['profileSHA256'], 'Runtime profile differs from reviewed registry'
    for mod, row in manifest['layout'].items():
        assert row['layout']['BUILD_ID'].lower() == (PROFILE['buildIDs'] | PROFILE.get('nroBuildIDs', {}))[mod].lower(), 'Module build is not reviewed'
    # manifest.json is the last publication marker and the last file transferred to the device.
    (package / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    totals = {}
    for name, row in files.items():
        category = name.split('/')[0]; totals[category] = totals.get(category, 0) + row['bytes']
    totals['manifest.json'] = (package / 'manifest.json').stat().st_size
    (OUT / 'package-sizes.json').write_text(json.dumps(totals, indent=2) + '\n')
    print('Prepared package verified and published:', package)
    print('Total bytes:', sum(totals.values()))

def main():
    global OUT, ENGINE, GAME, MODULES, PROFILE, PROFILE_DIR, REWRITTEN, ROMFS, SHADERS, SHADER_REPORT
    parser = argparse.ArgumentParser()
    parser.add_argument('--container-only', action='store_true')
    parser.add_argument('--reuse-container', action='store_true')
    parser.add_argument('--title', default=GAME)
    parser.add_argument('--version', default='1.4.3')
    for name in ['out-root', 'engine', 'profile-dir', 'rewritten', 'romfs', 'shaders', 'shader-report']:
        parser.add_argument('--' + name, type=Path)
    args = parser.parse_args()
    PROFILE = reviewed_profile(args.title, args.version)
    GAME, MODULES = PROFILE['titleID'], PROFILE['modules']
    if GAME != '0100F2C0115B6000':
        for name in ['out_root', 'engine', 'profile_dir', 'rewritten', 'romfs', 'shaders', 'shader_report']:
            if getattr(args, name) is None: parser.error('A non-default title requires --' + name.replace('_', '-'))
    OUT = args.out_root or OUT
    OUT.mkdir(parents=True, exist_ok=True)
    ENGINE = args.engine or ENGINE
    PROFILE_DIR = args.profile_dir or PROFILE_DIR
    REWRITTEN = args.rewritten or REWRITTEN
    ROMFS = args.romfs or ROMFS
    SHADERS = args.shaders or SHADERS
    SHADER_REPORT = args.shader_report or SHADER_REPORT
    dylib = OUT / 'container/game.dylib' if args.reuse_container else build_dylib()
    if args.reuse_container:
        run(['codesign', '--verify', '--strict', dylib]); compare_sections(dylib, image_sections(dylib), REWRITTEN); compare_nro_sections(dylib, image_sections(dylib), REWRITTEN)
    if not args.container_only: pack(dylib)

if __name__ == '__main__': main()
