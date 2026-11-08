#!/usr/bin/env python3
"""Synthetic Mac checks of title settings, launch policy and display geometry."""
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
OUT = Path(os.environ.get('SATCHEL_SETTINGS_TEST_OUT', Path.home() / 'SatchelBuild/uitests3/settings-tests'))
OUT.mkdir(parents=True, exist_ok=True)
shutil.copyfile(ROOT / 'NativeHost/PlayerProfiles.plist', OUT / 'PlayerProfiles.plist')
base = ['nice', '-n', '10', 'xcrun', 'clang', '-fobjc-arc', '-fmodules', '-framework', 'Foundation',
        '-framework', 'CoreGraphics', '-I', str(ROOT / 'NativeHost'), str(ROOT / 'NativeHost/PlayerSettings.m'),
        str(ROOT / 'App/ControllerProfiles.m'), '-framework', 'GameController']
# Command-line preferences use the executable name. Keep worker domains separate.
prefix = OUT.parent.name
exe = OUT / (prefix + '-player-settings-check')
subprocess.run(base + [str(ROOT / 'NativeHost/display/check.m'), '-o', str(exe)], check=True)
subprocess.run([exe, 'setup'], check=True)

def check(args, fps='0', scale='1.0', mode='fit', pipeline='exact'):
    result = subprocess.run([exe, 'read', *args], env=dict(os.environ, EXPECT_FPS=fps,
        EXPECT_SCALE=scale, EXPECT_MODE=mode, EXPECT_PIPELINE=pipeline), check=True, capture_output=True, text=True)
    print(result.stdout, end='')
    if args == ['-SatchelRenderScale', '0.45']:
        assert 'allowed render scale range: 0.5 through 1.0' in result.stderr

check([])
check(['-SatchelFPSTarget', '30', '-SatchelRenderScale', '1.0', '-SatchelScreenMode', 'stretch'], '30', '1.0', 'stretch')
check(['-SatchelFPSTarget', '60', '-SatchelRenderScale', '0.5', '-SatchelScreenMode', 'zoom'], '60', '0.5', 'fit')
for value in ['NaN', 'inf', '0.45', '0.49', '1.01', '0.75junk', '', '0.5.5', '-0.75']:
    check(['-SatchelRenderScale', value])
check(['-SatchelFPSTarget', '120', '-SatchelScreenMode', 'wide'])
check(['-SatchelScreenMode'])
check(['-SatchelFPSTarget', '-SatchelScreenMode', 'zoom'])
check(['-SatchelFPSTarget', '0'])
check(['-SatchelRenderScale', '0.7', '-SatchelRenderScale', '0.8'], scale='0.8')
check(['-SatchelPipelineMode', 'async'], pipeline='async')
check(['-SatchelPipelineMode', 'skip'], pipeline='skip')
check(['-SatchelPipelineMode', 'hybrid'], pipeline='hybrid')
check(['-SatchelPipelineMode', 'bounded'], pipeline='bounded')
check(['-SatchelPipelineMode', 'legacy'], pipeline='legacy')
check(['-SatchelPipelineMode', 'invalid'])
check([])
pipeline = OUT / (prefix + '-pipeline-check')
subprocess.run(base + ['-framework', 'Metal', str(ROOT / 'NativeHost/graphics/PipelineSettings.m'),
    str(ROOT / 'Tests/PlayerSettings/pipeline.m'), '-o', str(pipeline)], check=True)
subprocess.run([pipeline], check=True)
# Launch prewarm policy, trial list copy and per-launch event files, in a private synthetic runtime folder.
trial = ['-SatchelPipelineTrial', 'check-b', '-SatchelGraphicsProfile', 'YES']
# Scheduling trials: valid lanes, ahead and translation ahead values pass through, invalid ones leave them unset.
for args, expect, lanes, ahead, translation in [([], '', '', '', ''), (['-SatchelPipelinePrewarm', 'YES'], '3000', '', '', ''),
                     (['-SatchelPipelinePrewarm', 'YES', '-SatchelPipelinePrewarmMS', '1500', '-SatchelPipelineTrialListFrom', 'check-a'], '1500', '', '', ''),
                     (['-SatchelPipelinePrewarm', 'YES', '-SatchelPipelinePrewarmMS', '99'], '3000', '', '', ''),
                     (['-SatchelPipelineLanes', '4', '-SatchelPipelineAhead', '128'], '', '4', '128', ''),
                     (['-SatchelPipelineLanes', '4', '-SatchelPipelineAhead', '128', '-SatchelShaderAhead', '4'], '', '4', '128', '4'),
                     (['-SatchelShaderAhead', '9'], '', '', '', ''), (['-SatchelShaderAhead', '0'], '', '', '', ''),
                     (['-SatchelPipelineLanes', '9', '-SatchelPipelineAhead', '0x10'], '', '', '', ''),
                     (['-SatchelMemoryProfile', 'YES'], '', '', '', ''), (['-SatchelMemoryProfile', 'yes'], '', '', '', '')]:
    runtime = OUT / 'pipeline-runtime'
    shutil.rmtree(runtime, ignore_errors=True)
    subprocess.run([pipeline, *trial, *args], env=dict(os.environ, PIPELINE_RUNTIME=str(runtime), EXPECT_PREWARM_MS=expect,
                   EXPECT_LANES=lanes, EXPECT_AHEAD=ahead, EXPECT_SHADER_AHEAD=translation,
                   EXPECT_MEMORY_PROFILE='1' if args == ['-SatchelMemoryProfile', 'YES'] else '0'), check=True)
shutil.rmtree(OUT / 'pipeline-runtime', ignore_errors=True)
print('PASS: 25 settings/geometry processes, 6 pipeline launch policies and 11 launch prewarm, scheduling, memory profile or trial file checks, all synthetic.')
