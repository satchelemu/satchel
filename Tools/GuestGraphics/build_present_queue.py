#!/usr/bin/env python3
from pathlib import Path
import hashlib,json,subprocess,os,sys
import argparse,platform
ROOT=Path(__file__).resolve().parents[2]
ap=argparse.ArgumentParser();ap.add_argument('--source-root',type=Path,default=ROOT);ap.add_argument('--output',type=Path,required=True);ap.add_argument('--cpu-archive',type=Path,required=True);ap.add_argument('--cpu-header',type=Path,required=True);ap.add_argument('--backend-build',type=Path,default=Path('/tmp/satchel-provoking-flat-build'));ap.add_argument('--compiler-build',type=Path,default=Path('/tmp/satchel-guest-captured-compiler-final'));ap.add_argument('--scalar-build',type=Path,default=Path('/tmp/satchel-scalar-compiler'));ap.add_argument('--moltenvk-root',type=Path,default=Path('/tmp/satchel-vulkan-backend'));ap.add_argument('--ios',action='store_true');args=ap.parse_args()
D=args.source_root.resolve();S=D/'Tools/GuestGraphics';O=args.output.resolve()
if platform.system()!='Darwin' or platform.machine()!='arm64':raise SystemExit('Apple Silicon macOS required')
if O.exists() or not O.is_relative_to(Path('/tmp').resolve()) or O.is_relative_to(D):raise SystemExit('Fresh external task-owned output under /tmp required')
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
lockpath=S/'PresentQueueSourceLock.json';lock=json.loads(lockpath.read_text());inputs={str(lockpath):sha(lockpath),str(Path(__file__)):sha(__file__)}
def check(p,h):
 if sha(p)!=h:raise SystemExit('Hash mismatch '+str(p))
 inputs[str(p)]=h
for n,h in lock['sources'].items():check(D/n,h)
check(args.cpu_header,lock['cpu_header_sha256']);inputs[str(args.cpu_archive.resolve())]=sha(args.cpu_archive)
backend=args.backend_build/'backend-production.o';bm_path=args.backend_build/'validation-manifest.json';bm=json.loads(bm_path.read_text());check(backend,bm['artifacts'][backend.name]);inputs[str(bm_path)]=sha(bm_path)
for n,h in bm['inputs'].items():
 if '/Tools/' in n and Path(n).suffix in ('.c','.cpp','.h','.inc'):check(D/('Tools/'+n.split('/Tools/',1)[1]),h)
compiler=args.compiler_build/'host-publish/SatchelCapturedFlat.dylib';cm_path=args.compiler_build/'validation-manifest.json';cm=json.loads(cm_path.read_text());check(compiler,cm['artifacts']['host_dylib']);inputs[str(cm_path)]=sha(cm_path)
for n,h in cm['sources'].items():check(D/'Tools/CapturedFlatCompiler'/n,h)
scalar=args.scalar_build/'macos/libSatchelScalarCompiler.a';sm_path=args.scalar_build/'validation-manifest.json';sm=json.loads(sm_path.read_text());check(scalar,sm['archives']['macos']['sha256']);inputs[str(sm_path)]=sha(sm_path)
for n,h in sm['sources'].items():check(D/'Tools/ScalarCompiler'/n,h)
mvk=args.moltenvk_root/'macos/MoltenVK/MoltenVK/static/MoltenVK.xcframework/macos-arm64_x86_64/libMoltenVK.a';check(mvk,lock['moltenvk_macos_sha256'])
O.mkdir();cpu=O/'inputs/libSatchelCPUProbe.a';cpu.parent.mkdir();cpu.write_bytes(args.cpu_archive.read_bytes());(cpu.parent/'CPUProbe.h').write_bytes(args.cpu_header.read_bytes());env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1');commands=[];results={}
def run(cmd,name):
 commands.append(list(map(str,cmd)));r=subprocess.run(commands[-1],text=True,capture_output=True,env=env);(O/(name+'.log')).write_text(r.stdout+r.stderr)
 if r.returncode:raise SystemExit('Failed '+name+'; see '+str(O/(name+'.log')))
 return r.stdout.strip()
G=D/'Tools/GuestGraphics';incs=[S,cpu.parent,G,D/'Tools/VulkanBackend',D/'Tools/RasterCompiler',D/'Tools/ScalarCompiler',D/'Tools/CapturedFlatCompiler'];inc=[x for p in incs for x in ['-I',p]];san=['-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer']
def compile(src,name,defs=()):
 obj=O/(name+'.o');run(['xcrun','clang','-std=c11',*san,*inc,*defs,'-c',src,'-o',obj],name+'-compile');return obj
names=lock['host_common_c'];common={}
for n in names:
 p=Path(n);src=S/p.name if p.name=='GuestTransfers.c' else D/p;common[p.stem]=compile(src,p.stem)
frameworks=[x for n in ['Foundation','Metal','QuartzCore','CoreGraphics','IOSurface','IOKit','AppKit'] for x in ['-framework',n]]
for mode,fixture in [('transfer','PresentTransferTest'),('raster','PresentRasterTest'),('compute','PresentComputeTest')]:
 parts=dict(common)
 defs=['-Dsatchel_cpu_probe_writev='+{'transfer':'satchel_transfer_test_writev','raster':'queue_test_writev','compute':'nvn_test_writev'}[mode]]
 if mode=='transfer':defs+=['-Dsatchel_vulkan_transfer_batch=satchel_transfer_test_batch']
 parts['GuestTransfers']=compile(S/'GuestTransfers.c',mode+'-transfers',defs)
 if mode=='raster':parts['GuestRasterRun']=compile(G/'GuestRasterRun.c','raster-run-fault',['-Dmalloc=run_test_malloc','-Dcalloc=run_test_calloc','-Dsatchel_vulkan_buffer_write=run_test_write','-Dsatchel_vulkan_buffer_read=run_test_read','-Dsatchel_vulkan_raster_destroy=run_test_destroy','-Dsatchel_guest_pools_release=run_test_release'])
 if mode=='compute':
  parts['GuestCompute']=compile(G/'GuestCompute.c','compute-fault',['-Dsatchel_vulkan_scalar_submit=nvn_test_submit','-Dsatchel_vulkan_buffer_destroy=nvn_test_destroy'])
  parts['GuestVulkanCompute']=compile(G/'GuestVulkanCompute.c','compute-capture',['-Dsatchel_guest_transfers_create_checked=present_test_create_checked'])
 test=compile(S/(fixture+'.c'),fixture,['-Wno-missing-field-initializers']);exe=O/mode
 run(['xcrun','clang++',*san,*parts.values(),test,backend,cpu,scalar,compiler,mvk,*frameworks,'-Wl,-rpath,'+str(compiler.parent),'-o',exe],mode+'-link')
 results[mode]=run([exe],mode);print(results[mode],flush=True)
artifacts={}
run(['xcrun','libtool','-static','-o',O/'libSatchelGuestTransfers-macos.a',common['GuestTransfers']],'host-archive');artifacts['libSatchelGuestTransfers-macos.a']=sha(O/'libSatchelGuestTransfers-macos.a')
if args.ios:
 sdk=run(['xcrun','--sdk','iphoneos','--show-sdk-path'],'ios-sdk');obj=O/'GuestTransfers-ios.o'
 run(['xcrun','clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror',*inc,'-target','arm64-apple-ios17.0','-isysroot',sdk,'-c',S/'GuestTransfers.c','-o',obj],'ios-compile')
 run(['xcrun','libtool','-static','-o',O/'libSatchelGuestTransfers.a',obj],'ios-archive')
 exports=sorted(n for n in run(['nm','-gUj',O/'libSatchelGuestTransfers.a'],'ios-exports').splitlines() if n.startswith('_'))
 if exports!=lock['exports']:raise SystemExit('Unexpected production exports')
 undefined=run(['nm','-uj',O/'libSatchelGuestTransfers.a'],'ios-imports')
 if any(n in undefined for n in ['test_','run_test_','nvn_test_']):raise SystemExit('Test import in production')
 build=run(['xcrun','vtool','-show-build',obj],'ios-build-version')
 import re
 if not re.search(r'platform IOS\s+minos 17\.0\s',build):raise SystemExit('Wrong deployment target')
 artifacts['libSatchelGuestTransfers.a']=sha(O/'libSatchelGuestTransfers.a')
for p,h in inputs.items():check(Path(p),h)
(O/'validation-manifest.json').write_text(json.dumps({'status':'passed','inputs':inputs,'commands':commands,'results':results,'artifacts':artifacts,'executables':{n:sha(O/n) for n in ['transfer','raster','compute']},'ios':args.ios,'cpu_archive_sha256':sha(cpu),'scope':'Original real transfer/compute/raster queue leases; no game Window or display proof'},indent=2)+'\n')
