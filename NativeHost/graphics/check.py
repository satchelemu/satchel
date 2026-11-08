#!/usr/bin/env python3
"""Exercise the pipeline overlay with pinned engine tests and real Metal fixtures."""
from pathlib import Path
import argparse, os, runpy, shutil, subprocess
DEPENDENCY_PATHS = runpy.run_path(str(Path(__file__).resolve().parents[2] / 'scripts/dependency_paths.py'))

p = argparse.ArgumentParser()
p.add_argument('--engine', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
a.output.mkdir(parents=True, exist_ok=True)
engine = a.output / 'engine'
if not engine.exists():
    shutil.copytree(a.engine, engine, ignore=shutil.ignore_patterns('.git', '__pycache__', 'gen'))
nvn = engine / 'dm/gfx/nvn'
runpy.run_path(str(Path(__file__).with_name('overlay.py')))['prepare'](a.engine / 'dm/gfx/nvn/nvn_mtl.m', a.output / 'overlay')
shutil.copyfile(a.output / 'overlay/nvn_mtl.m', nvn / 'nvn_mtl.m')
shutil.copyfile(a.output / 'overlay/ShaderMetrics.inc', nvn / 'ShaderMetrics.inc')
shutil.copyfile(a.output / 'overlay/ShaderBuildQueue.inc', nvn / 'ShaderBuildQueue.inc')
shutil.copyfile(a.output / 'overlay/AsyncPipelines.inc', nvn / 'AsyncPipelines.inc')
shutil.copyfile(a.output / 'overlay/SkipPipelines.inc', nvn / 'SkipPipelines.inc')
tests = engine / 'dm/gfx/tests'
source = '#define main original_main\n' + (a.engine / 'dm/gfx/tests/t_pso_warm.m').read_text() + '\n#undef main\n'
source += r'''
int main(void) { @autoreleasepool {
    setenv("DM_PSO_ARCHIVE", "off", 1);
    setenv("DM_PSO_PREPARE", "1", 1);
    setenv("DM_PSO_PREPARE_THREADS", "2", 1);
    setenv("DM_PSO_PREBUILD", "0", 1);
    setenv("DM_NVN_PREDICT", "0", 1);
    unsetenv("DM_PSO_MANIFEST"); unsetenv("DM_PSO_RECORD");
    NSString *dir = [@(getenv("TMPDIR")) stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
    EXPECT("fixture directory", 1, [NSFileManager.defaultManager createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil]);
    mtl_init();
    if (!fixtures(dir)) return 1;
    g_shmap_path = [dir stringByAppendingPathComponent:@"shader_map.tsv"];
    pso_key key = {0}; key.cfmt[0] = MTLPixelFormatRGBA8Unorm; key.samples = 1; key.chmask = 15;
    NSString *user = [dir stringByAppendingPathComponent:@"pso_user.tsv"];
    EXPECT("user manifest written", 1, [row(&key, 0) writeToFile:user atomically:YES encoding:NSUTF8StringEncoding error:nil]);
    setenv("DM_PSO_USER", user.fileSystemRepresentation, 1);
    EXPECT("seed absent", 0, [NSFileManager.defaultManager fileExistsAtPath:[dir stringByAppendingPathComponent:@"pso_manifest.tsv"]]);
    warm_start(); warm_claim();
    EXPECT("recorded pipeline warmed without seed", 1, g_warm_claim_rps);
    const uint8_t expected[4] = {128, 64, 255, 128};
    check_draw(&key, 0, expected);
    [NSFileManager.defaultManager removeItemAtPath:dir error:nil];
    printf("t_satchel_seed: %d checks, %d failed\n", checks, failed);
    return failed != 0;
} }
'''
(tests / 't_satchel_seed.m').write_text(source)
(tests / 't_satchel_seed_base.m').write_text(source.replace('#include "../nvn/nvn_mtl.m"',
    '#include "' + str(a.engine / 'dm/gfx/nvn/nvn_mtl.m') + '"'))
live = '#define main original_main\n' + (a.engine / 'dm/gfx/tests/t_pso_warm.m').read_text() + '\n' + Path(__file__).with_name('archive-live-test.inc').read_text()
(tests / 't_satchel_archive_live.m').write_text(live)
(tests / 't_satchel_archive_live_base.m').write_text(live.replace('#include "../nvn/nvn_mtl.m"',
    '#include "' + str(a.engine / 'dm/gfx/nvn/nvn_mtl.m') + '"'))
queue = '#define main original_main\n' + (a.engine / 'dm/gfx/tests/t_pso_warm.m').read_text() + '\n#undef main\n'
queue += r'''int main(void) { @autoreleasepool {
    pthread_t caller = pthread_self(); __block int elsewhere = 0, joined = 0, nested = 0;
    satchel_pipeline_work(^{
        elsewhere = !pthread_equal(caller, pthread_self());
        pthread_t worker = pthread_self();
        satchel_pipeline_work(^{ nested = pthread_equal(worker, pthread_self()); });
        joined = 1;
    });
    EXPECT("pipeline work leaves requesting thread", 1, elsewhere);
    EXPECT("first use joins completion", 1, joined);
    EXPECT("nested work stays on its lane", 1, nested);
    __block _Atomic int active = 0, peak = 0;
    dispatch_group_t group = dispatch_group_create();
    for (int i = 0; i < 12; i++) dispatch_group_async(group, dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        satchel_pipeline_work(^{
            int n = atomic_fetch_add(&active, 1) + 1, old = atomic_load(&peak);
            while (n > old && !atomic_compare_exchange_weak(&peak, &old, n)) { }
            usleep(1000); atomic_fetch_sub(&active, 1);
        });
    });
    dispatch_group_wait(group, DISPATCH_TIME_FOREVER);
    EXPECT("two bounded compiler lanes", 1, atomic_load(&peak) <= 2);
    EXPECT("all compiler jobs joined", 0, atomic_load(&active));
    printf("t_satchel_shader_queue: %d checks, %d failed\n", checks, failed);
    return failed != 0;
} }
'''
(tests / 't_satchel_shader_queue.m').write_text(queue)
(tests / 't_satchel_async_pixels.m').write_text('#define main original_main\n' + (a.engine / 'dm/gfx/tests/t_pso_warm.m').read_text() + '\n' + Path(__file__).with_name('async-pixels-test.inc').read_text())
# The texture-write watch: the overlay's nvn_mtl.m and a copy of hle_nvn.c with texture-writes.patch in one unit, as t_cpu_tex.
watch = a.output / 'texture-writes'; watch.mkdir(exist_ok=True)
shutil.copyfile(a.engine / 'dm/gfx/nvn/hle_nvn.c', watch / 'hle_nvn.c')
runpy.run_path(str(Path(__file__).with_name('texture_writes.py')))['host'](watch / 'hle_nvn.c')
shutil.copyfile(watch / 'hle_nvn.c', nvn / 'hle_nvn_tw.c')
(tests / 't_satchel_texture_writes.m').write_text('#define nvn_game_fps nvn_game_fps_mtl_default\n#include "../nvn/nvn_mtl.m"\n#undef nvn_game_fps\n'
    '#define g_ft g_ft_hle\n#include "../nvn/hle_nvn_tw.c"\n#undef g_ft\n' + Path(__file__).with_name('texture-writes-test.m').read_text())
(tests / 'satchel.mk').write_text('include Makefile\n' + ''.join(
    name + ': ' + name + '.m $(MTL_T_DEP)\n\tclang $(CFLAGS) -fobjc-arc -o $@ ' + name + '.m $(MTL_T_SRC) $(FW)\n'
    for name in ['t_satchel_seed', 't_satchel_seed_base', 't_satchel_archive_live', 't_satchel_archive_live_base', 't_satchel_shader_queue', 't_satchel_async', 't_satchel_async_pixels']) +
    't_satchel_texture_writes: t_satchel_texture_writes.m ../nvn/hle_nvn_tw.c $(MTL_T_DEP)\n\tclang $(CFLAGS) -fobjc-arc -o $@ t_satchel_texture_writes.m $(filter-out ../nvn/hle_nvn.c,$(MTL_T_SRC)) $(FW)\n')
tools = DEPENDENCY_PATHS['deps_root']() / 'tmp-snapshot-2026-11-01/tmp/satchel-switch-integration'
env = runpy.run_path(str(root / 'scripts/switch_native.py'))['tool_environment'](tools)
import json
env.update(SATCHEL_METAL_TOOLCHAIN=json.loads((DEPENDENCY_PATHS['build_inputs']() / 'native-toolchain.json').read_text())['metal_toolchain'],
           GAME='0100BA4026E94000', DM_GAME='0100BA4026E94000', DM_GAMES_FIRST='1',
           DM_GAMES_DIR=str(Path.home() / 'SatchelBuild/castlevania/profile'),
           TMPDIR=str(a.output / 'tmp'), PYTHONDONTWRITEBYTECODE='1', MAKEFLAGS='-j2')
Path(env['TMPDIR']).mkdir(exist_ok=True)
# Keep upstream test fixtures in this task's scratch directory.
for name in ['t_pso_pending.m', 't_pso_archive.m', 't_pso_warm.m', 't_pull.m', 't_pull_inv.m', 't_pso_stall.m']:
    f = tests / name
    f.write_text((a.engine / 'dm/gfx/tests' / name).read_text().replace('/tmp/', env['TMPDIR'] + '/'))
# Demand now joins a worker instead of invoking Metal on the submitter. Retain
# every upstream queue/result assertion and check prediction ordering inside the
# fake compiler regardless of its thread. The reference source stays untouched.
pending = tests / 't_pso_pending.m'
text = pending.read_text()
replacements = {
    'BOOL check_absent = on_submitter && n == _check_absent_call;':
        'BOOL check_absent = n == _check_absent_call;',
    'EXPECT_TRUE("demand compiler call ran on submitter thread", [dev callWasOnSubmitter:2]);':
        'EXPECT_TRUE("demand compiler call ran on worker thread", ![dev callWasOnSubmitter:2]);',
    'EXPECT_EQ("fake compiler observed one synchronous and two async calls", 1, [dev syncCalls]);':
        'EXPECT_EQ("fake compiler observed no submitter compiler calls", 0, [dev syncCalls]);',
    'EXPECT_EQ("fake compiler observed speculative and predicted async calls", 2, [dev asyncCalls]);':
        'EXPECT_EQ("fake compiler observed all three worker compiler calls", 3, [dev asyncCalls]);',
}
for before, after in replacements.items():
    assert text.count(before) == 1, before
    text = text.replace(before, after)
pending.write_text(text)
(tests / 't_satchel_async.m').write_text('#define main original_main\n' + pending.read_text() + '\n' + Path(__file__).with_name('async-test.inc').read_text())
targets = ['t_satchel_seed', 't_satchel_seed_base', 't_satchel_archive_live', 't_satchel_archive_live_base', 't_pso_pending', 't_pso_archive', 't_pso_warm', 't_pull', 't_pull_inv', 't_pso_stall', 't_satchel_shader_queue', 't_satchel_async', 't_satchel_async_pixels', 't_satchel_texture_writes']
# Both prerequisites resolve the same profile symlink, so prepare them serially.
subprocess.run(['make', '-j1', 'game_profile', 'host_msl'], cwd=engine / 'dm/host', env=env, check=True)
subprocess.run(['make', '-j2', '-f', 'satchel.mk', *targets], cwd=tests, env=env, check=True)
idle = runpy.run_path(str(root / 'NativeApp/build_support.py'))['wait']
def executable(name):
    # Shared guards recognize shader-harness while each Metal fixture is running.
    idle()
    runner = a.output / 'runners' / name / 'shader-harness'
    runner.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(tests / name, runner)
    return runner.resolve()

baseline = subprocess.run([executable('t_satchel_seed_base')], cwd=tests,
    env=dict(env, MTL_DEBUG_LAYER='1', DM_PSO_MTL4='1'), capture_output=True, text=True)
print(baseline.stdout, end=''); print(baseline.stderr, end='')
# Until the engine pin move of 2026-11-05 the unmodified engine failed this fixture and only the
# overlay warmed a recorded pipeline without a seed list. Upstream c4b0618c (in the pin) does the
# same, so the control now pins that fact: the unmodified engine passes every assertion and still
# reports the absent list as an error, which the overlay words as a notice. If a later pin loses
# the upstream fix, this fails and the overlay's own handling has to come back.
if baseline.returncode != 0 or 't_satchel_seed: 12 checks, 0 failed' not in baseline.stdout or \
        '[RED] pipeline prewarm: seed list does not exist' not in baseline.stdout + baseline.stderr:
    raise RuntimeError('The unmodified engine no longer warms recorded pipelines without a seed list as upstream c4b0618c does')
print('Control: the pinned engine warms a recorded pipeline without a seed list (upstream c4b0618c) and logs the absent list in red.')
baseline = subprocess.run([executable('t_satchel_archive_live_base')], cwd=tests,
    env=dict(env, MTL_DEBUG_LAYER='1'), capture_output=True, text=True)
print(baseline.stdout, end=''); print(baseline.stderr, end='')
if baseline.returncode != 1 or 'live archive supplies prebuilt base without restart: expected 1, got 0' not in baseline.stderr:
    raise RuntimeError('The unmodified engine did not reproduce the unpublished-archive regression')
print('Expected negative control: unmodified engine cannot use a freshly flushed archive.')
subprocess.run([executable('t_satchel_archive_live')], cwd=tests, env=dict(env, MTL_DEBUG_LAYER='1'), check=True)

for mode in ['0', '1']:
    for test in ['t_satchel_seed', 't_pso_warm']:
        subprocess.run([executable(test)], cwd=tests, env=dict(env, MTL_DEBUG_LAYER='1', DM_PSO_MTL4=mode), check=True)
for test in ['t_pso_pending', 't_pull', 't_pull_inv', 't_pso_stall', 't_satchel_shader_queue', 't_satchel_async', 't_satchel_async_pixels', 't_satchel_texture_writes']:
    subprocess.run([executable(test)], cwd=tests, env=dict(env, MTL_DEBUG_LAYER='1'), check=True)
# Run both commands from check_t_pso_archive with the recognized executable name.
archive = executable('t_pso_archive')
subprocess.run([archive], cwd=tests, env=dict(env, MTL_DEBUG_LAYER='1'), check=True)
idle()
subprocess.run(['python3', tests / 'test_pso_archive.py', archive], cwd=tests,
               env=dict(env, MTL_DEBUG_LAYER='1'), check=True)
