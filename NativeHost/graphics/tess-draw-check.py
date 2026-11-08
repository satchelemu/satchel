#!/usr/bin/env python3
"""Mac GPU check of NVN patch draws through the host's tessellation route (tessellation_draw.py).

Builds the overlaid nvn_mtl.m (display and graphics overlays, as build-pipeline-mac.py does) into
tess-draw-check.m, translates public-synthetic vertex, hull and domain shaders with the repository's
translator (NativeShaders/public_corpus.py's generators; no game data), and replays synthetic NVN command
streams with patch draws against a CPU reference draw. Outputs stay under $SATCHEL_OUT/tess-mac; only
counts and pass or fail are printed.
"""
import hashlib
import json
import os
from pathlib import Path
import runpy
import shlex
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
DEPENDENCY_PATHS = runpy.run_path(str(ROOT / 'scripts/dependency_paths.py'))
sys.path.insert(0, str(ROOT / 'NativeShaders'))
from public_corpus import corpus, domain, graphics, hull  # noqa: E402

OUT = Path(os.environ['SATCHEL_OUT']).resolve() / 'tess-mac'
GAME = '0100F2C0115B6000'
SPACINGS = {'equal': 0, 'fractional_even': 1, 'fractional_odd': 2}
DOMAINS = {'isolines': 0, 'triangles': 1, 'quads': 2}


def key(name):
    return hashlib.sha256(name.encode()).hexdigest()[:16]


def job(name, stage, **facts):
    j = dict(key=key(name), stage=stage, c1='', tex={}, img={}, attr={}, fout={}, debug=True, reduced=True,
             source='public-synthetic', pc_paired='')
    j.update(facts)
    return j


def corners(domain_name, cw, back, patch):
    """Control points in clip space: a triangle (u P0 + v P1 + w P2) or a parallelogram (P0 + u (P1 - P0) + v (P3 - P0)),
    facing the viewer after the tessellator's winding unless the case is the back-facing one."""
    x0, y0 = (-0.913, -0.877) if patch == 0 else (0.071, 0.043)
    a, b = 0.807, 0.789
    if domain_name == 'triangles':
        p0, p1, p2 = (x0 + a, y0), (x0, y0 + b), (x0, y0)
        if cw != back: p0, p1 = p1, p0
        return [p0, p1, p2]
    s, t = 0.0731, 0.1137
    p0, p1, p3 = (x0, y0), (x0 + a, y0 + s), (x0 + t, y0 + b)
    if cw != back: p1, p3 = p3, p1
    p2 = (p1[0] + p3[0] - p0[0], p1[1] + p3[1] - p0[1])
    return [p0, p1, p2, p3]


CASES = [
    # name, domain, spacing, cw, outer, inner, extra
    ('tri-equal', 'triangles', 'equal', False, (3, 5, 2, 1), (4, 1), dict(firstVertex=5)),
    ('tri-equal-ones', 'triangles', 'equal', False, (1, 1, 1, 1), (1, 1), {}),
    ('tri-even-indexed', 'triangles', 'fractional_even', False, (2.5, 5.25, 7, 1), (3.5, 1), dict(indexed=True, baseVertex=3)),
    ('tri-odd-cw-instanced', 'triangles', 'fractional_odd', True, (3.3, 1.7, 6, 1), (4.6, 1), dict(instances=2)),
    ('tri-equal-64', 'triangles', 'equal', False, (64, 64, 64, 1), (64, 1), {}),
    ('tri-back-facing', 'triangles', 'equal', False, (3, 5, 2, 1), (4, 1), dict(back=True)),
    ('quad-equal', 'quads', 'equal', False, (3, 5, 2, 7), (3, 5), {}),
    ('quad-ones', 'quads', 'equal', False, (1, 1, 1, 1), (1, 1), {}),
    ('quad-odd-cw-indexed', 'quads', 'fractional_odd', True, (2.5, 4.25, 1.5, 6.75), (3.5, 2.2), dict(indexed=True, baseVertex=2)),
    ('quad-even', 'quads', 'fractional_even', False, (2, 3.5, 5, 8.5), (4.5, 6), {}),
    ('quad-equal-16', 'quads', 'equal', False, (16, 16, 16, 16), (16, 16), {}),
    ('isolines-odd', 'isolines', 'fractional_odd', False, (3, 4.5, 0, 0), (0, 0), dict(pixels=False)),
    # Refused: no SetPatchSize in the stream. The patch draw draws nothing and says so; the plain draw after it is unchanged.
    ('tri-no-patch-size', 'triangles', 'equal', False, (3, 5, 2, 1), (4, 1), dict(noPatchSize=True)),
]


def build_harness(products, tools):
    builder = runpy.run_path(str(ROOT / 'NativeShaders/build-translation.py'))
    names = builder['SOURCES'] + builder['METRICS']
    aot = products / 'aot/repro/host-publish/SatchelShaderProbe.dylib'
    executable = tools / 'translation-harness'
    subprocess.run(['nice', '-n', '10', 'xcrun', '--sdk', 'macosx', 'swiftc', '-O', '-j', '2', '-import-objc-header', ROOT / 'NativeShaders/TranslationBridge.h',
                    *[ROOT / 'NativeShaders' / (n + '.swift') for n in names], products / 'cross-macosx/libGameShaderBridge.a', '-lc++',
                    aot, '-Xlinker', '-rpath', '-Xlinker', aot.parent, '-o', executable], check=True)
    return executable


def fixtures(harness, folder):
    shutil.rmtree(folder, ignore_errors=True); folder.mkdir(parents=True)
    work = folder / 'requests'; work.mkdir()
    requests, outputs = [], []
    def add(name, code, j, **options):
        d = work / name; d.mkdir(); (d / 'code.bin').write_bytes(code)
        requests.append(json.dumps(dict(job=j, input=str(d / 'code.bin'), output=str(d), **options))); outputs.append((j['key'], d))
    vertex = next(r for r in corpus() if r['name'] == 'vertex-0')
    add('vertex', bytes.fromhex(vertex['code']), vertex['job'], wanted=vertex['wanted'])
    cases, domains = [], {}
    for name, dom, spacing, cw, outer, inner, extra in CASES:
        n = 3 if dom == 'triangles' else 4
        add('hull-' + name, hull(n, outer, inner), job('tess-draw hull ' + name, 'hs'))
        dkey = (dom, spacing, cw)
        if dkey not in domains:
            dname = 'tess-draw domain %s %s %s' % dkey
            add('domain-%d' % len(domains), domain(2, 0, 1) if dom == 'triangles' else domain(0, 1, 3),
                job(dname, 'ds', tess_domain=dom, tess_spacing=spacing, tess_cw=cw))
            domains[dkey] = key(dname)
        back = extra.get('back', False)
        cases.append(dict(name=name, hull=key('tess-draw hull ' + name), domain=domains[dkey], domainCode=DOMAINS[dom], spacingCode=SPACINGS[spacing],
                          cw=cw, patchSize=n, patches=2, outer=list(outer), inner=list(inner), indexed=extra.get('indexed', False),
                          baseVertex=extra.get('baseVertex', 0), firstVertex=extra.get('firstVertex', 0), instances=extra.get('instances', 1),
                          pixels=extra.get('pixels', True), expectEmpty=back, noPatchSize=extra.get('noPatchSize', False), positions=[corners(dom, cw, back, p) for p in range(2)]))
    result = subprocess.run([harness], input='\n'.join(requests) + '\n', capture_output=True, text=True, timeout=600)
    replies = [json.loads(line) for line in result.stdout.splitlines() if line.startswith('{')]
    if result.returncode or len(replies) != len(requests) or any(r.get('status') != 'ok' for r in replies):
        raise SystemExit('translation failed: ' + json.dumps([r.get('reason') for r in replies if r.get('status') != 'ok']))
    for k, d in outputs:
        shutil.copyfile(d / 'reflection.json', folder / (k + '_b.json'))
        subprocess.run(['xcrun', '-sdk', 'macosx', 'metal', '-std=metal3.2', '-fpreserve-invariance', '-fmetal-math-mode=relaxed',
                        '-o', folder / (k + '_b.metallib'), d / 'b.metal'], check=True)
    shutil.rmtree(work)
    (folder / 'cases.json').write_text(json.dumps(dict(vertex=vertex['job']['key'], cases=cases), indent=1) + '\n')
    return len(outputs)


def engine_copy(original, engine):
    if not engine.exists():
        subprocess.run(['cp', '-c', '-R', str(original), str(engine)], check=True)
        shutil.rmtree(engine / '.git', ignore_errors=True)
    host = engine / 'dm/host'
    for target in ['gen/%s/game_profile.h' % GAME, 'gen/msl-macosx/host_msl_gen.h']:
        if not (host / target).exists():
            subprocess.run(['nice', '-n', '10', 'make', '-j2', 'GAME=' + GAME, target], cwd=host, check=True)
    # The overlays, refreshed from the pinned sources on every run (build-pipeline-mac.py's chain)
    for relative in ['dm/gfx/nvn/nvn_mtl.m', 'dm/gfx/nvn/hle_nvn.c']:
        shutil.copyfile(original / relative, engine / relative)
    runpy.run_path(str(ROOT / 'NativeHost/display_overlay.py'))['apply'](ROOT, original, OUT, {'SatchelNativeHost': [], 'SatchelNativeGraphics': []})
    for source in (OUT / 'overlays/display/dm').rglob('*'):
        if source.is_file(): shutil.copyfile(source, engine / 'dm' / source.relative_to(OUT / 'overlays/display/dm'))
    runpy.run_path(str(ROOT / 'NativeHost/graphics/texture_writes.py'))['host'](engine / 'dm/gfx/nvn/hle_nvn.c')
    runpy.run_path(str(ROOT / 'NativeHost/graphics/tessellation_draw.py'))['host'](engine / 'dm/gfx/nvn/hle_nvn.c')
    metal = runpy.run_path(str(ROOT / 'NativeHost/graphics/overlay.py'))['prepare'](engine / 'dm/gfx/nvn/nvn_mtl.m', OUT / 'graphics')
    shutil.copyfile(metal, engine / 'dm/gfx/nvn/nvn_mtl.m')
    for source in metal.parent.glob('*.inc'): shutil.copyfile(source, engine / 'dm/gfx/nvn' / source.name)
    shutil.copyfile(ROOT / 'NativeHost/graphics/FrameTiming.h', engine / 'dm/gfx/nvn/FrameTiming.h')


def main():
    if OUT.is_relative_to(ROOT): raise SystemExit('Build outputs must be outside the checkout.')
    OUT.mkdir(parents=True, exist_ok=True)
    if shutil.disk_usage(OUT).free < 35.5 * 1024 ** 3: raise SystemExit('Check blocked: preserving the 35 GiB reserve')
    runpy.run_path(str(ROOT / 'NativeApp/build_support.py'))['wait']()
    original = DEPENDENCY_PATHS['deps_root']() / 'NX2Apple/engine'
    assert subprocess.check_output(['git', '-C', str(original), 'rev-parse', 'HEAD'], text=True).strip() == 'f8a5618d5f505a4c0a0bcac7c413872d3a53c009'
    tools = OUT / 'tools'; tools.mkdir(exist_ok=True)
    count = fixtures(build_harness(DEPENDENCY_PATHS['shader_deps'](), tools), OUT / 'fixtures')
    engine = OUT / 'engine'
    engine_copy(original, engine)
    tests = engine / 'dm/gfx/tests'
    shutil.copyfile(Path(__file__).with_name('tess-draw-check.m'), tests / 'tess_draw_check.m')
    env = dict(os.environ, MAKEFLAGS='-j2', PYTHONDONTWRITEBYTECODE='1')
    dry = subprocess.check_output(['make', '-n', '-B', 'GAME=' + GAME, 't_geometry'], cwd=tests, env=env, text=True)
    tokens = next(shlex.split(line) for line in dry.splitlines() if ' -o t_geometry ' in line)
    tokens[tokens.index('-o') + 1] = str(tools / 'tess-draw-check')
    tokens[tokens.index('t_geometry.m')] = 'tess_draw_check.m'
    # The phone shader runtime's entry points are weak imports the Mac check does not provide.
    tokens += ['-Wl,-undefined,dynamic_lookup']
    runpy.run_path(str(ROOT / 'NativeApp/build_support.py'))['wait']()
    subprocess.run(['nice', '-n', '10', *tokens], cwd=tests, env=env, check=True)
    # Both encoding paths: on the submit thread, and deferred to the encode thread (DM_NVN_ENC_THREAD=1, the iOS default)
    replies = {}
    for encode in ['0', '1']:
        run = subprocess.run([tools / 'tess-draw-check', OUT / 'fixtures'], env=dict(env, MTL_DEBUG_LAYER='1', DM_NVN_ENC_THREAD=encode),
                             capture_output=True, text=True, timeout=900)
        try:
            reply = json.loads(run.stdout.strip().splitlines()[-1])
        except (ValueError, IndexError):
            reply = {'passed': False, 'failure': 'no result (exit %d)' % run.returncode}
        if run.returncode or not reply.get('passed'):
            reply['passed'] = False
            reply['stderr'] = [line for line in run.stderr.splitlines() if not line.startswith('[')][-20:]
        replies['encodeThread' + encode] = reply
    result = dict(passed=all(r.get('passed') is True for r in replies.values()), translated=count, **replies)
    (OUT / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))
    if not result['passed']: sys.exit(1)


if __name__ == '__main__':
    main()
