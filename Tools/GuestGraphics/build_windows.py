#!/usr/bin/env python3
"""Original checked Window/Process/queue/frame gate. No canonical outputs or device use."""
from pathlib import Path
import argparse,hashlib,json,os,platform,re,subprocess
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--source-root',type=Path,default=ROOT);p.add_argument('--output',type=Path,required=True)
for n in ['cpu-archive','cpu-header','cpu-source','ipc-archive']:p.add_argument('--'+n,type=Path,required=True)
for n,d in [('backend-build','/tmp/satchel-provoking-flat-build'),('compiler-build','/tmp/satchel-guest-captured-compiler-final'),('scalar-build','/tmp/satchel-scalar-compiler'),('moltenvk-root','/tmp/satchel-vulkan-backend')]:p.add_argument('--'+n,type=Path,default=Path(d))
p.add_argument('--ios',action='store_true');a=p.parse_args();D=a.source_root.resolve();S=D/'Tools/GuestGraphics';O=a.output.resolve();sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
if platform.system()!='Darwin' or platform.machine()!='arm64':raise SystemExit('Apple Silicon macOS and real Metal device required')
if O.exists() or not O.is_relative_to(Path('/tmp').resolve()) or O.is_relative_to(D):raise SystemExit('Fresh external /tmp output required')
lockpath=S/'WindowSourceLock.json';lock=json.loads(lockpath.read_text());inputs={str(lockpath):sha(lockpath),str(Path(__file__)):sha(__file__)}
def check(p,h):
 p=Path(p)
 if sha(p)!=h:raise SystemExit('Input provenance mismatch: '+str(p))
 inputs[str(p)]=h
for n,h in lock['sources'].items():check(D/n,h)
check(a.cpu_header,lock['cpu_header_sha256']);check(a.ipc_archive,lock['ipc_archive_sha256'])
# CPU is explicit configurable production input. Copy source/header/archive together
# and report hashes; this does not assert arbitrary archives match a source build.
cpu_info={k:{'path':str(v.resolve()),'sha256':sha(v)} for k,v in [('archive',a.cpu_archive),('header',a.cpu_header),('source',a.cpu_source)]}
for v in [a.cpu_archive,a.cpu_header,a.cpu_source]:inputs[str(v.resolve())]=sha(v)
backend=a.backend_build/'backend-production.o';bm_path=a.backend_build/'validation-manifest.json';bm=json.loads(bm_path.read_text());check(backend,bm['artifacts'][backend.name]);inputs[str(bm_path)]=sha(bm_path)
for n,h in bm['inputs'].items():
 if '/Tools/' in n and Path(n).suffix in ('.c','.cpp','.h','.inc'):check(D/('Tools/'+n.split('/Tools/',1)[1]),h)
compiler=a.compiler_build/'host-publish/SatchelCapturedFlat.dylib';cm_path=a.compiler_build/'validation-manifest.json';cm=json.loads(cm_path.read_text());check(compiler,cm['artifacts']['host_dylib']);inputs[str(cm_path)]=sha(cm_path)
for n,h in cm['sources'].items():check(D/'Tools/CapturedFlatCompiler'/n,h)
scalar=a.scalar_build/'macos/libSatchelScalarCompiler.a';sm_path=a.scalar_build/'validation-manifest.json';sm=json.loads(sm_path.read_text());check(scalar,sm['archives']['macos']['sha256']);inputs[str(sm_path)]=sha(sm_path)
for n,h in sm['sources'].items():check(D/'Tools/ScalarCompiler'/n,h)
mvk=a.moltenvk_root/'macos/MoltenVK/MoltenVK/static/MoltenVK.xcframework/macos-arm64_x86_64/libMoltenVK.a';check(mvk,lock['moltenvk_macos_sha256'])
O.mkdir();private=O/'inputs';private.mkdir();cpu=private/'libSatchelCPUProbe.a';cpu.write_bytes(a.cpu_archive.read_bytes());(private/'CPUProbe.h').write_bytes(a.cpu_header.read_bytes());(private/'CPUProbe.cpp').write_bytes(a.cpu_source.read_bytes());ipc=private/'libSatchelHorizonIPC.a';ipc.write_bytes(a.ipc_archive.read_bytes())
commands=[];results={};env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1')
def run(c,n):
 commands.append(list(map(str,c)));r=subprocess.run(commands[-1],capture_output=True,text=True,env=env);(O/(n+'.log')).write_text(r.stdout+r.stderr)
 if r.returncode:raise SystemExit('Failed '+n+'; see '+str(O/(n+'.log')))
 return r.stdout.strip()
flags=['-O1','-g','-Wall','-Wextra','-Werror'];san=['-fsanitize=address,undefined','-fno-omit-frame-pointer'];inc=[x for p in [S,O,private,D/'Core',D/'Tools/HorizonIPC',D/'Tools/GuestVI',D/'Tools/VulkanBackend',D/'Tools/RasterCompiler',D/'Tools/ScalarCompiler',D/'Tools/CapturedFlatCompiler'] for x in ['-I',p]]
def cc(src,n,defs=()):
 obj=O/(n+'.o');run(['xcrun','clang','-std=c11',*flags,*san,*inc,*defs,'-c',src,'-o',obj],n+'-compile');return obj
common={Path(n).stem:cc(D/n,Path(n).stem) for n in lock['host_common_c']};frameworks=[x for n in ['Foundation','Metal','QuartzCore','CoreGraphics','IOSurface','IOKit','AppKit'] for x in ['-framework',n]]
def link(parts,test,n,extra=()):
 exe=O/n;run(['xcrun','clang++',*san,*parts,test,backend,cpu,*extra,scalar,compiler,mvk,*frameworks,'-Wl,-rpath,'+str(compiler.parent),'-o',exe],n+'-link');results[n]=run([exe],n);print(results[n],flush=True)
# Owner fault/refusal suite, retaining every original raster queue assertion.
parts=dict(common)
parts['GuestWindows']=cc(S/'GuestWindows.c','owner-fault',['-Dsatchel_cpu_probe_writev=window_test_writev','-Dsatchel_guest_transfers_present_poll=window_test_present_poll','-Dsatchel_guest_textures_retain=window_test_texture_retain','-Dsatchel_guest_texture_frames_poll=window_test_frame_poll','-Dsatchel_guest_texture_frames_try_destroy=window_test_frame_destroy'])
parts['GuestTransfers']=cc(S/'GuestTransfers.c','owner-transfer-fault',['-Dsatchel_cpu_probe_writev=queue_test_writev'])
parts['GuestRasterRun']=cc(S/'GuestRasterRun.c','owner-raster-fault',['-Dmalloc=run_test_malloc','-Dcalloc=run_test_calloc','-Dsatchel_vulkan_buffer_write=run_test_write','-Dsatchel_vulkan_buffer_read=run_test_read','-Dsatchel_vulkan_raster_destroy=run_test_destroy','-Dsatchel_guest_pools_release=run_test_release'])
link(parts.values(),cc(S/'WindowTest.c','owner-test',['-Wno-missing-field-initializers']),'owner')
original=(S/'RasterDispatchTest.c').read_text();assert original.count('int main(void)')==1
(O/'PreservedWindowRaster.c').write_text(original.replace('int main(void)','int original_window_legacy(void)'))
link(common.values(),cc(S/'WindowDispatchTest.c','composite-test'),'composite')
(O/'PreservedWindowRaster.c').write_text(original.replace('int main(void)','int original_window_legacy(void)').replace('ICALL=0x100000,IBOOT=0x100100,ISTUBS=0x200000','ICALL=0x8000000,IBOOT=0x8000100,ISTUBS=0x8200000'))
process=[cc(D/'Core'/(n+'.c'),n) for n in ['SwitchProcess','ProcessMetadata','SwitchStorageIPC','SwitchResources','SwitchEvents']]
link([*common.values(),*process],cc(S/'ProcessWindowDispatchTest.c','process-test'),'process',[ipc])
# Preserve captured/scalar tests, only redirect the opt-in and top-level main.
for n in ['CapturedNVNProgramTest','CapturedNVNProgramFaultTest','CapturedNVNComputeRegression']:
 text=(S/(n+'.c')).read_text();assert text.count('int main(void)')==1;(O/(n+'.c')).write_text(text.replace('int main(void)','int preserved_main(void)'))
(O/'CapturedNVNLegacyFixture.c').write_bytes((S/'CapturedNVNLegacyFixture.c').read_bytes())
for mode,n in [('captured','CapturedNVNProgramTest'),('captured-fault','CapturedNVNProgramFaultTest'),('compute','CapturedNVNComputeRegression')]:
 wrapper=O/(mode+'-wrapper.c');wrapper.write_text('#include "WindowRegressionBridge.h"\n#include "'+n+'.c"\nint main(void){int s=preserved_main();if(satchel_guest_vi_try_destroy(&reg_vi))abort();return s;}\n');parts=dict(common)
 if mode=='captured-fault':parts['GuestVulkanWindows']=cc(S/'GuestVulkanWindows.c','captured-composite-fault',['-Dsatchel_guest_captured_programs_capture=capture_test_call','-Dsatchel_guest_captured_programs_forget=forget_test_call'])
 if mode=='compute':
  parts['GuestTransfers']=cc(S/'GuestTransfers.c','compute-transfer-fault',['-Dsatchel_cpu_probe_writev=nvn_test_writev']);parts['GuestCompute']=cc(S/'GuestCompute.c','compute-owner-fault',['-Dsatchel_vulkan_scalar_submit=nvn_test_submit','-Dsatchel_vulkan_buffer_destroy=nvn_test_destroy'])
 link(parts.values(),cc(wrapper,mode+'-test'),mode)
# Existing host-selected snapshots remain distinct from real Window selection.
wrapper=O/'snapshot-wrapper.c';defines=['#define satchel_guest_dispatch_enable_vulkan_texture_frames reg_enable']+['#define satchel_guest_dispatch_texture_frame_'+n+' satchel_guest_dispatch_windows_texture_frame_'+n for n in ['describe','begin','poll','take','copy','release','detach','destroy']]
wrapper.write_text('#include "WindowRegressionBridge.h"\n'+'\n'.join(defines)+'\n#define yes diagnostic_yes\n#include "GuestTextureFrameDiagnostic.c"\n#undef yes\n#define main preserved_main\n#include "TextureFrameDiagnosticTest.c"\n#undef main\nint main(void){int s=preserved_main();if(satchel_guest_vi_try_destroy(&reg_vi))abort();return s;}\n')
link(common.values(),cc(wrapper,'snapshot-test'),'snapshot')
for n in ['DispatchTest','DispatchRangeTest']:
 test=cc(S/(n+'.c'),n);exe=O/n;run(['xcrun','clang++',*san,common['GuestDispatch'],test,cpu,'-o',exe],n+'-link');results[n]=run([exe],n);print(results[n],flush=True)
artifacts={};exports={}
for n in ['GuestWindows','GuestVulkanWindows','GuestDispatch']:
 archive=O/('libSatchel'+n+'-macos.a');run(['xcrun','libtool','-static','-o',archive,common[n]],n+'-host-archive');artifacts[archive.name]=sha(archive)
if a.ios:
 sdk=run(['xcrun','--sdk','iphoneos','--show-sdk-path'],'ios-sdk')
 for n in ['GuestWindows','GuestVulkanWindows','GuestDispatch']:
  obj=O/(n+'-ios.o');run(['xcrun','clang','-std=c11','-O2','-Wall','-Wextra','-Werror',*inc,'-target','arm64-apple-ios17.0','-isysroot',sdk,'-c',S/(n+'.c'),'-o',obj],n+'-ios');archive=O/('libSatchel'+n+'.a');run(['xcrun','libtool','-static','-o',archive,obj],n+'-archive');artifacts[archive.name]=sha(archive)
  exports[n]=sorted(v for v in run(['nm','-gUj',archive],n+'-exports').splitlines() if v.startswith('_'))
  if exports[n]!=lock['exports'][n]:raise SystemExit('Unexpected production exports '+n)
  undefined=run(['nm','-uj',archive],n+'-imports')
  if re.search(r'(test_|_test|asan|ubsan)',undefined):raise SystemExit('Test import in production '+n)
  version=run(['xcrun','vtool','-show-build',obj],n+'-build-version')
  if not re.search(r'platform IOS\s+minos 17\.0\s',version):raise SystemExit('Wrong deployment target')
for path,digest in inputs.items():check(path,digest)
(O/'validation-manifest.json').write_text(json.dumps({'status':'original owner/actual Process/interpreted Window and legacy regressions passed','inputs':inputs,'commands':commands,'results':results,'artifacts':artifacts,'exports':exports,'ios':a.ios,'cpu':cpu_info,'builder_sha256':sha(__file__),'source_lock_sha256':sha(lockpath),'scope':'Checked original Window-selected snapshot ownership; synthetic host timing policy, no game/display or vendor-default claim'},indent=2)+'\n')
