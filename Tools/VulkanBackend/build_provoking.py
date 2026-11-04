#!/usr/bin/env python3
"""Reproduce bounded indexed-LAST on the matched, already validated flat backend."""
from pathlib import Path
import argparse,hashlib,json,os,platform,subprocess
ROOT=Path(__file__).resolve().parents[2];V=ROOT/'Tools/VulkanBackend'
def h(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--renderer-build',type=Path,required=True);ap.add_argument('--output',type=Path,default=Path('/tmp/satchel-provoking-build'));a=ap.parse_args();o=a.output.resolve();r=a.renderer_build.resolve();tmp=Path('/tmp').resolve()
 if platform.system()!='Darwin' or platform.machine()!='arm64':raise SystemExit('Apple Silicon macOS required.')
 if o==tmp or not o.is_relative_to(tmp) or o.is_relative_to(ROOT.resolve()):raise SystemExit('Owned output under /tmp outside source required.')
 marker=o/'.satchel-provoking-build'
 if o.exists() and any(o.iterdir()) and (not marker.exists() or marker.read_text()!='1\n'):raise SystemExit('Refusing unowned output.')
 lock=V/'provoking-source-lock.json';entries=json.loads(lock.read_text());inputs={}
 for rel,want in entries.items():
  p=ROOT/rel
  if h(p)!=want:raise SystemExit('Provoking source mismatch: '+rel)
  inputs[str(p)]=want
 manifest=r/'validation-manifest.json';m=json.loads(manifest.read_text());fl=V/'flat-source-lock.json'
 if h(fl)!=m['source_lock_sha256']:raise SystemExit('Validated renderer source-lock mismatch.')
 for rel,want in json.loads(fl.read_text()).items():
  p=ROOT/rel;old=[x for x in m['inputs'] if x.endswith('/'+rel)]
  if h(p)!=want or len(old)!=1 or m['inputs'][old[0]]!=want:raise SystemExit('Validated renderer input mismatch: '+rel)
  inputs[str(p)]=want
 for rel,want in m['artifacts'].items():
  if h(r/rel)!=want:raise SystemExit('Renderer artifact mismatch: '+rel)
  inputs[str(r/rel)]=want
 if len(m['exports'])!=52:raise SystemExit('Validated 52-export iOS backend required.')
 p=Path('/tmp/satchel-vulkan-backend/macos/MoltenVK/MoltenVK');mvk=p/'static/MoltenVK.xcframework/macos-arm64_x86_64/libMoltenVK.a'
 if m['inputs'].get(str(mvk))!=h(mvk):raise SystemExit('Pinned MoltenVK input mismatch.')
 for x in [fl,manifest,lock,mvk,Path(__file__)]:inputs[str(x)]=h(x)
 o.mkdir(parents=True,exist_ok=True);marker.write_text('1\n');env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1')
 def run(args,name):
  result=subprocess.run(list(map(str,args)),text=True,capture_output=True,env=env);(o/(name+'.log')).write_text(result.stdout+result.stderr)
  if result.returncode:raise SystemExit(name+' failed; see '+str(o/(name+'.log')))
  return result.stdout.strip()
 san=['-O1','-g','-Wall','-Wextra','-Werror','-Wno-missing-field-initializers','-fsanitize=address,undefined','-fno-omit-frame-pointer'];inc=[x for d in [V,ROOT/'Tools/GuestGraphics',ROOT/'Tools/RasterCompiler',p/'include'] for x in ['-I',d]]
 fault=['-include',V/'ProvokingFaults.h','-DSATCHEL_PROVOKING_CREATE_BUFFER=provoking_test_create','-DSATCHEL_PROVOKING_REQUIREMENTS=provoking_test_requirements','-DSATCHEL_PROVOKING_ALLOCATE=provoking_test_allocate','-DSATCHEL_RASTER_CREATE_PIPELINES=flat_test_pipelines','-DSATCHEL_RASTER_QUEUE_SUBMIT=flat_test_submit','-DSATCHEL_RASTER_BATCH_SUBMIT=flat_test_submit']
 run(['xcrun','clang++','-std=c++17',*san,*inc,*fault,'-c',V/'VulkanBackend.cpp','-o',o/'backend-fault.o'],'backend-compile');run(['xcrun','clang++','-std=c++17',*san,*inc,'-c',V/'ProvokingTest.cpp','-o',o/'test.o'],'test-compile');fw=[x for n in ['Foundation','Metal','QuartzCore','CoreGraphics','IOSurface','IOKit','AppKit'] for x in ['-framework',n]]
 run(['xcrun','clang++',*san,o/'backend-fault.o',o/'test.o',mvk,*fw,'-o',o/'test'],'link');gate=run([o/'test',V/'FlatFixtures'],'test');print(gate,flush=True)
 if inputs!={p:h(p) for p in inputs}:raise SystemExit('Input changed during gate.')
 (o/'validation-manifest.json').write_text(json.dumps({'status':'original FIRST/LAST GPU and ownership gates passed','builder_sha256':h(__file__),'source_lock_sha256':h(lock),'inputs':inputs,'gate':gate,'artifacts':{'test':h(o/'test'),'backend-fault.o':h(o/'backend-fault.o')},'scope':'Independent typed pure triangle-list profile only. Assertions include pixels/bytes. No NVN provoking enum/default, compiler-state relaxation, game/window/display claim. Production iOS52 exports validated by required renderer input.'},indent=2)+'\n')
if __name__=='__main__':main()
