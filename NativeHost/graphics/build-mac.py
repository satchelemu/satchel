#!/usr/bin/env python3
"""Build a diagnostic Mac host from the pinned engine and graphics overlay."""
from pathlib import Path
import os,sys,runpy,json,subprocess,importlib.util,shutil,time
root=Path(__file__).resolve().parents[2]
out=Path(os.environ['SATCHEL_OUT'])/'gpu-mac';engine=out/'engine'
out.mkdir(parents=True,exist_ok=True)
deps=Path(os.environ.get('SATCHEL_DEPS_ROOT', Path.home()/'SatchelDeps'))
build=Path(os.environ.get('SATCHEL_BUILD_ROOT', Path.home()/'SatchelBuild'))
source=deps/'NX2Apple/engine'
assert subprocess.check_output(['git','-C',source,'rev-parse','HEAD'],text=True).strip()=='f8a5618d5f505a4c0a0bcac7c413872d3a53c009'
if not engine.exists():
 shutil.copytree(source,engine,ignore=shutil.ignore_patterns('.git','gen','__pycache__','*.dSYM'))
 gen=engine/'dm/host/gen';gen.mkdir()
 for name in ['vpx','msl-macosx']:shutil.copytree(build/'graphics/mac-engine/dm/host/gen'/name,gen/name)
# Refresh task-owned copies before applying the graphics and pause overlay.
for name in ['hle_thread.c','hle_fs.c','hle_svc.c','romfs_image.h']:shutil.copyfile(source/'dm/host'/name,engine/'dm/host'/name)
groups={'SatchelNativeHost':[{'path':str(engine/'dm/host'/name)} for name in ['hle_thread.c','hle_fs.c','hle_svc.c']], 'SatchelNativeGraphics':[{'path':str(source/'dm/gfx/nvn'/name)} for name in ['nvn_mtl.m','hle_nvn.c']]}
runpy.run_path(str(root/'NativeHost/graphics/overlay.py'))['apply'](root,engine,out/'mac',groups)
for row in groups['SatchelNativeHost']:
 p=Path(row['path'])
 if p.suffix=='.c':shutil.copyfile(p,engine/'dm/host'/p.name)
for p in (out/'mac/overlays/graphics-host').glob('*.h'):shutil.copyfile(p,engine/'dm/host'/p.name)
p=Path(groups['SatchelNativeGraphics'][0]['path']);s=p.read_text()
# Private harness: build only libraries actually used by the route. No shader
# translation changes. Acceptance runs forbid a missing cached Mac library.
s=s.replace('lib_from_file(NSString *path, NSError **err) {', 'lib_from_file(NSString *inputPath, NSError **err) {\n    NSString *__strong path = inputPath;', 1)
s=s.replace('    int fd = open(path.fileSystemRepresentation, O_RDONLY); struct stat st;', '''    const char *cache = getenv("DM_MAC_LIB_CACHE");
    if (cache) {
        NSString *lib = [@(cache) stringByAppendingPathComponent:path.lastPathComponent];
        if (access(lib.fileSystemRepresentation, R_OK)) {
            if (!getenv("DM_MAC_COMPILE") || strcmp(getenv("DM_MAC_COMPILE"), "1")) MDIE("Mac measurement cache miss");
            NSTask *task = [NSTask new];
            task.executableURL = [NSURL fileURLWithPath:@(getenv("DM_MAC_PYTHON"))];
            task.arguments = @[@"-B", @(getenv("DM_MAC_COMPILER")), path, lib];
            task.standardError = NSFileHandle.fileHandleWithNullDevice;
            if (![task launchAndReturnError:nil]) MDIE("Mac shader compiler launch failed");
            [task waitUntilExit]; if (task.terminationStatus) MDIE("Mac shader compiler failed status=%d", task.terminationStatus);
            MLOG("[mac-cache] compiled library");
        }
        path = lib;
    }
    int fd = open(path.fileSystemRepresentation, O_RDONLY); struct stat st;''',1)
if os.environ.get('SATCHEL_UPSCALE_DIAGNOSTICS') == '1':
 s=runpy.run_path(str(root/'NativeHost/graphics/upscale-diagnostics.py'))['apply'](s)
(engine/'dm/gfx/nvn/nvn_mtl.m').write_text(s)
for include in p.parent.glob('*.inc'):
 shutil.copyfile(include, engine/'dm/gfx/nvn'/include.name)
shutil.copyfile(root/'NativeHost/graphics/FrameTiming.h', engine/'dm/gfx/nvn/FrameTiming.h')
shutil.copyfile(groups['SatchelNativeGraphics'][1]['path'], engine/'dm/gfx/nvn/hle_nvn.c')
sys.path[:0]=[str(engine/'dm/games'),str(engine/'dm/tools')]
import game,profile_guess
spec=importlib.util.spec_from_file_location('nsp_build',engine/'scripts/build_ipa_from_nsp.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);m.pg=profile_guess
os.cpu_count=lambda:2
tools=deps/'tmp-snapshot-2026-11-01/tmp/satchel-switch-integration'
env=runpy.run_path(str(root/'scripts/switch_native.py'))['tool_environment'](tools)
env.update(J='2',DM_REWRITE_J='1',MAKEFLAGS='-j2',TMPDIR=str(out/'tmp'),PYTHONDONTWRITEBYTECODE='1',DM_GAME='0100F2C0115B6000',GAME='0100F2C0115B6000',DM_GAMES_DIR=str(build/'zelda/profiles'),DM_GAMES_FIRST='1')
(out/'tmp').mkdir(exist_ok=True);(out/'logs').mkdir(exist_ok=True)
env['SATCHEL_METAL_TOOLCHAIN']=json.loads((build/'zelda/native-toolchain.json').read_text())['metal_toolchain']
os.environ.update(env);g=game.load('0100F2C0115B6000');start=time.monotonic()
m.build_mac_host(engine,out/'tools/dmhost',env,out/'logs',g)
if os.environ.get('SATCHEL_GRAPHICS_REPLAY') == '1':
 import shlex
 tests=engine/'dm/gfx/tests'
 # The host loads either allowed game's runtime profile. Keep replay's title
 # check equally strict against that same profile, rather than its build default.
 replay=(source/'dm/gfx/tests/nvn_replay.m').read_text()
 title_check='    if (strcmp(title, DM_GAME_ID)) RFAIL("the capture is of %s, this replayer is built for %s (make GAME=%s nvn_replay)", title, DM_GAME_ID, title);'
 if replay.count(title_check) != 1:
  raise RuntimeError('Replay title check anchor changed')
 (tests/'nvn_replay.m').write_text(replay.replace(title_check,title_check.replace('DM_GAME_ID','dm_profile()->id'),1))
 dry=subprocess.check_output(['make','-n','-B','GAME=0100F2C0115B6000','nvn_replay'],cwd=tests,env=dict(os.environ),text=True)
 command=next(shlex.split(line) for line in dry.splitlines() if ' -o nvn_replay ' in line)
 command[command.index('-o')+1]=str(out/'tools/nvn-replay')
 subprocess.run(command,cwd=tests,env=dict(os.environ),check=True)
print('Mac host build seconds',round(time.monotonic()-start,2),flush=True)
