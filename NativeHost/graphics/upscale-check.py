#!/usr/bin/env python3
"""Check profile persistence and the real Metal presentation path with synthetic pixels."""
from pathlib import Path
import os, shlex, shutil, subprocess, sys
ROOT = Path(__file__).resolve().parents[2]
OUT = Path(os.environ['SATCHEL_OUT']) / 'gpu-mac'
ENGINE = OUT / 'engine'
checks = OUT / 'checks'
checks.mkdir(exist_ok=True)
sys.path.insert(0, str(ROOT / 'NativeApp'))
from build_support import wait
wait()
shutil.copyfile(ROOT / 'NativeHost/PlayerProfiles.plist', checks / 'PlayerProfiles.plist')
for name, source in [('upscale-settings', 'graphics/upscale-settings-check.m'), ('display-settings', 'display/check.m')]:
    exe = checks / name
    subprocess.run(['nice', '-n', '10', 'xcrun', 'clang', '-fobjc-arc', '-fmodules', '-framework', 'Foundation',
                    '-framework', 'CoreGraphics', '-I', ROOT / 'NativeHost', ROOT / 'NativeHost/PlayerSettings.m',
                    ROOT / 'NativeHost' / source, '-o', exe], check=True)
    subprocess.run([exe, 'setup'], check=True)
    if name == 'upscale-settings':
        subprocess.run([exe, 'override-save', '-SatchelUpscaler', 'spatial'], check=True)
        for arguments, expected in [([], 'off'), (['-SatchelUpscaler', 'spatial'], 'spatial'),
                                    (['-SatchelUpscaler', 'off'], 'off'), (['-SatchelUpscaler', 'temporal'], 'off'),
                                    (['-SatchelUpscaler'], 'off'), ([], 'off')]:
            subprocess.run([exe, 'read', *arguments], env=dict(os.environ, EXPECT_UPSCALE=expected), check=True)
# Include all upstream crop, gamma, orientation, resize and readback assertions unchanged.
tests = ENGINE / 'dm/gfx/tests'
source = '#define main upstream_main\n' + (tests / 't_present_crop.m').read_text() + '\n#undef main\n'
source += r'''
int main(void) { @autoreleasepool {
    int result = upstream_main();
    if (result) return result;
    for (const char **p = (const char *[]){"1", "1.0", "nan", "0.4", "0.65junk", "", NULL}; *p; p++) {
        setenv("DM_RENDER_SCALE", *p, 1); setenv("DM_PRESENT_UPSCALE", "spatial", 1); g_fx_mode = -1;
        EXPECT("full scale or malformed scale keeps current filter", 0, fx_mode());
    }
    setenv("DM_RENDER_SCALE", "0.65", 1); setenv("DM_PRESENT_UPSCALE", "off", 1); g_fx_mode = -1;
    EXPECT("off at reduced scale keeps current filter", 0, fx_mode());
    setenv("DM_PRESENT_UPSCALE", "spatial", 1); g_fx_mode = -1;
    EXPECT("spatial uses device capability", [MTLFXSpatialScalerDescriptor supportsMetal4FX:g_dev], fx_mode());
    // Reject unsupported perceptual formats before a scaler factory or command encoding.
    MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float width:32 height:32 mipmapped:NO];
    id<MTLTexture> hdr = [g_dev newTextureWithDescriptor:td];
    fx_state fx = {0}; NSUInteger rect[4] = {0, 0, 32, 32};
    EXPECT("HDR stays with current filter", 1, fx_encode(&fx, nil, hdr, rect, (MTLViewport){0,0,64,64,0,1}, MTLPixelFormatBGRA8Unorm, 0) == nil);
    EXPECT("unsupported format allocates no scaler", 0, fx.made);
    printf("upscale presentation: %d checks, %d failed\n", cases, failed);
    return failed != 0;
} }
'''
p = tests / 'satchel_upscale_check.m'
p.write_text(source)
dry = subprocess.check_output(['make', '-n', '-B', 'GAME=0100F2C0115B6000', 't_present_crop'], cwd=tests, text=True)
args = next(shlex.split(line) for line in dry.splitlines() if ' -o t_present_crop ' in line)
args[args.index('t_present_crop.m')] = str(p)
args[args.index('-o') + 1] = str(checks / 'upscale-present')
subprocess.run(['nice', '-n', '10', *args], cwd=tests, check=True)
subprocess.run([checks / 'upscale-present'], env=dict(os.environ, MTL_DEBUG_LAYER='1'), check=True)
