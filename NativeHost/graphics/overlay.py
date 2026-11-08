"""Apply the graphics overlay after display, without editing the pinned engine."""
from pathlib import Path
import shutil
import shlex
import subprocess
import runpy


def prepare(source, destination):
    destination.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination / 'nvn_mtl.m')
    # Earlier overlays (display, runtime capture, phone shaders) leave quoted includes beside
    # their nvn_mtl.m; they must sit beside this copy too.
    for neighbour in list(source.parent.glob('*.inc')) + list(source.parent.glob('PhoneShaderRuntime.h')):
        shutil.copyfile(neighbour, destination / neighbour.name)
    subprocess.run(['patch', '-p1', '--fuzz=0', '-i', Path(__file__).with_name('pipeline.patch')],
                   cwd=destination, check=True)
    subprocess.run(['patch', '-p1', '--fuzz=0', '-i', Path(__file__).with_name('gpu-cost.patch')],
                   cwd=destination, check=True)
    subprocess.run(['patch', '-p1', '--fuzz=0', '-i', Path(__file__).with_name('upscale.patch')],
                   cwd=destination, check=True)
    metal = destination / 'nvn_mtl.m'
    text = metal.read_text()
    anchors = {}
    anchors['if (!a || !b || b < a) continue;'] = 'if (!a || !b || b < a) {\n            const gp_lab *l = &lab[i];\n            MLOG("[gpuprof-invalid] F%llu %d kind %c size %ux%u colors %u depth %u fmt %u/%u end_site %u writes %u draws %u", (unsigned long long)frame, i, l->kind, l->w, l->h, l->nc, l->depth, l->cfmt, l->dfmt, l->end_site, l->write_stages, l->draws);\n            continue;\n        }'
    for anchor, replacement in anchors.items():
        if text.count(anchor) != 1:
            raise RuntimeError('Invalid-counter diagnostic anchor is not unique')
        text = text.replace(anchor, replacement, 1)
    metal.write_text(text)
    runpy.run_path(str(Path(__file__).with_name('shader_builds.py')))['apply'](Path(__file__).resolve().parents[2], destination / 'nvn_mtl.m')
    runpy.run_path(str(Path(__file__).with_name('async_pipelines.py')))['apply'](Path(__file__).resolve().parents[2], destination / 'nvn_mtl.m')
    runpy.run_path(str(Path(__file__).with_name('shader_metrics.py')))['apply'](Path(__file__).resolve().parents[2], destination / 'nvn_mtl.m')
    # Opt-in translation ahead (-SatchelShaderAhead): after the metrics, so its walk is timed on its own.
    runpy.run_path(str(Path(__file__).with_name('translate_ahead.py')))['apply'](Path(__file__).resolve().parents[2], destination / 'nvn_mtl.m')
    # Opt-in launch prewarm of recorded pipelines with phone-translated shaders (-SatchelPipelinePrewarm YES).
    subprocess.run(['patch', '-p1', '--fuzz=0', '-i', Path(__file__).with_name('prewarm.patch')],
                   cwd=destination, check=True)
    # Opt-in edge smoothing in the present (DM_PRESENT_UPSCALE edge and edge-spatial). Last, so the
    # shader build and metrics instrumentation above count only the engine's own pipeline sites.
    runpy.run_path(str(Path(__file__).with_name('present_filter.py')))['apply'](Path(__file__).resolve().parents[2], destination / 'nvn_mtl.m')
    # Opt-in 3D view resample (DM_PRESENT_UPSCALE view): a game's FSR 1 upscale draw of its 3D view, before its HUD.
    runpy.run_path(str(Path(__file__).with_name('view_upscale.py')))['apply'](Path(__file__).resolve().parents[2], destination / 'nvn_mtl.m')
    # How the replay holds a texture, for the texture-write watch in hle_nvn.c (texture_writes.py). Its own patch file, applied last.
    runpy.run_path(str(Path(__file__).with_name('texture_writes.py')))['metal'](destination / 'nvn_mtl.m')
    return destination / 'nvn_mtl.m'


def apply(root, engine, out, groups):
    for row in groups['SatchelNativeGraphics']:
        if Path(row['path']).name == 'nvn_mtl.m':
            source = Path(row['path'])
            # Earlier overlays keep generated includes beside their Metal source.
            row['compilerFlags'] = (row.get('compilerFlags', '') +
                ' -iquote ' + shlex.quote(str(source.parent))).strip()
            row['path'] = str(prepare(source, out / 'overlays/graphics'))
            runpy.run_path(str(root / 'NativeHost/graphics/frame-timing.py'))['metal'](Path(row['path']))
            break
    else:
        raise RuntimeError('Graphics overlay requires the NVN Metal source')
    for row in groups['SatchelNativeGraphics']:
        if Path(row['path']).name == 'hle_nvn.c':
            source = Path(row['path'])
            destination = out / 'overlays/graphics/hle_nvn.c'
            runpy.run_path(str(root / 'NativeHost/graphics/frame-timing.py'))['host'](source, destination)
            # Counts textures the CPU writes after their first GPU use, for the app's boot feedback.
            runpy.run_path(str(root / 'NativeHost/graphics/texture_writes.py'))['host'](destination)
            row['compilerFlags'] = (row.get('compilerFlags', '') + ' -iquote ' + shlex.quote(str(source.parent))).strip()
            row['path'] = str(destination)
            break
    else:
        raise RuntimeError('Frame timing requires the NVN frame boundary source')
    # Chain on the current host inputs, including the optional session overlay.
    host = out / 'overlays/graphics-host'
    host.mkdir(parents=True, exist_ok=True)
    names = {'hle_thread.c', 'hle_fs.c', 'hle_svc.c'}
    found = {}
    for row in groups['SatchelNativeHost']:
        name = Path(row['path']).name
        if name in names:
            shutil.copyfile(row['path'], host / name)
            found[name] = row
    if set(found) != names:
        raise RuntimeError('Graphics pause diagnostics require thread, file and memory host inputs')
    for name in ['HostPause.h', 'RomFSRead.h']:
        shutil.copyfile(root / 'NativeHost/graphics' / name, host / name)
    shutil.copyfile(engine / 'dm/host/romfs_image.h', host / 'romfs_image.h')
    for patch in ['stalls.patch', 'host-time.patch', 'loading.patch']:
        subprocess.run(['patch', '-p1', '--fuzz=0', '-i', root / 'NativeHost/graphics' / patch],
                       cwd=host, check=True)
    # The include goes in by text: the session overlay rewrites the include block of hle_svc.c.
    svc = host / 'hle_svc.c'
    text = svc.read_text()
    if '#include "HostPause.h"' not in text:
        if '#include "dm.h"\n' not in text:
            raise RuntimeError('Graphics pause diagnostics need the dm.h include in hle_svc.c')
        svc.write_text(text.replace('#include "dm.h"\n', '#include "dm.h"\n#define SATCHEL_PAUSE_WAIT_FILTER 1\n'
                                    '#define SATCHEL_PAUSE_UNIT "svc"\n#define SATCHEL_PAUSE_EPOCH g_t0_ns\n'
                                    '#include "HostPause.h"\n', 1))
    for name, row in found.items():
        row['path'] = str(host / name)
    groups['SatchelNativeHost'].append({'path': str(root / 'NativeHost/graphics/PipelineSettings.m')})
