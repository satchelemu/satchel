#!/usr/bin/env python3
"""Validate the separate captured-source owner; no App edits or promotion."""
from pathlib import Path
import argparse,hashlib,json,os,platform,re,subprocess
ROOT=Path(__file__).resolve().parents[2];GG=ROOT/'Tools/GuestGraphics';CF=ROOT/'Tools/CapturedFlatCompiler'
p=argparse.ArgumentParser();p.add_argument('--compiler-build',type=Path,required=True);p.add_argument('--renderer-build',type=Path,required=True);p.add_argument('--renderer-source-root',type=Path,default=ROOT);p.add_argument('--cpu-archive',type=Path,default=Path('/tmp/satchel-vixl-probe/build/macos/libSatchelCPUProbe.a'));p.add_argument('--cpu-header',type=Path,default=ROOT/'Tools/CPUProbe/CPUProbe.h');p.add_argument('--ios',action='store_true');p.add_argument('--out',type=Path,required=True);a=p.parse_args()
out=a.out.resolve();rr=a.renderer_source_root.resolve();cb=a.compiler_build.resolve();rb=a.renderer_build.resolve();tmp=Path('/tmp').resolve()
if platform.system()!='Darwin' or platform.machine()!='arm64':raise SystemExit('Apple Silicon macOS/Xcode required')
if out==tmp or not out.is_relative_to(tmp) or out.is_relative_to(ROOT.resolve()) or out.is_relative_to(rr):raise SystemExit('Use a task-owned scratch output outside source')
if out.exists() and any(out.iterdir()):raise SystemExit('Output must be fresh')
out.mkdir(parents=True,exist_ok=True);h=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest();lockpath=GG/'captured-program-source-lock.json';lock=json.loads(lockpath.read_text());inputs={}
def bind(path,want):
 if h(path)!=want:raise RuntimeError('Input mismatch '+str(path))
 inputs[str(Path(path).resolve())]=want
for rel,want in lock['sources'].items():bind(ROOT/rel,want)
for rel,want in lock['compiler_contract'].items():bind(CF/rel,want)
bind(a.cpu_header,lock['cpu_header_sha256']);inputs[str(a.cpu_archive.resolve())]=h(a.cpu_archive)
cm_path=cb/'validation-manifest.json';cm=json.loads(cm_path.read_text());lib=cb/'host-publish/SatchelCapturedFlat.dylib';bind(lib,cm['artifacts']['host_dylib']);bind(CF/'source-lock.json',cm['source_lock_sha256'])
for rel,want in cm['sources'].items():bind(CF/rel,want)
expected=['DotNetRuntimeDebugHeader','satchel_captured_flat_admit','satchel_captured_flat_compile','satchel_captured_flat_compile_v2']
if cm['ios_exports']!=expected:raise RuntimeError('Compiler export mismatch')
rm_path=rb/'validation-manifest.json';rm=json.loads(rm_path.read_text());backend=rb/'backend-production.o';bind(backend,rm['artifacts']['backend-production.o']);vk=rr/'Tools/VulkanBackend';bind(vk/'flat-source-lock.json',rm['source_lock_sha256']);rl=json.loads((vk/'flat-source-lock.json').read_text())
for rel,want in rl.items():
 bind(rr/rel,want);matches=[path for path in rm['inputs'] if path.endswith('/'+rel)]
 if len(matches)!=1 or rm['inputs'][matches[0]]!=want:raise RuntimeError('Renderer input manifest mismatch '+rel)
scalar=Path('/tmp/satchel-scalar-compiler/macos/libSatchelScalarCompiler.a');sm_path=scalar.parent.parent/'validation-manifest.json';sm=json.loads(sm_path.read_text());bind(scalar,sm['archives']['macos']['sha256'])
for name in ['ScalarCompiler.c','ScalarCompiler.h']:bind(ROOT/'Tools/ScalarCompiler'/name,sm['sources'][name])
mvk=Path('/tmp/satchel-vulkan-backend/macos/MoltenVK/MoltenVK/static/MoltenVK.xcframework/macos-arm64_x86_64/libMoltenVK.a');matches=[key for key in rm['inputs'] if key.endswith('/macos-arm64_x86_64/libMoltenVK.a')]
if len(matches)!=1:raise RuntimeError('Missing reviewed MoltenVK dependency')
bind(mvk,rm['inputs'][matches[0]])
for path in [cm_path,rm_path,sm_path,lockpath]:inputs[str(path)]=h(path)
env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1',DYLD_LIBRARY_PATH=str(lib.parent))
def run(args,name):
 r=subprocess.run(list(map(str,args)),env=env,capture_output=True,text=True);(out/(name+'.log')).write_text(r.stdout+r.stderr)
 if r.returncode:raise RuntimeError('Gate failed '+str(out/(name+'.log')))
 return r.stdout.strip()
san=['-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer'];inc=sum([['-I',str(x)] for x in [GG,CF,vk,ROOT/'Tools/RasterCompiler',ROOT/'Tools/ScalarCompiler',a.cpu_header.parent]],[])
def obj(source,name=None,extra=()):
 target=out/((name or source.stem)+'.o');run(['xcrun','clang','-std=c11',*san,*inc,*extra,'-c',source,'-o',target],target.stem+'-build');return target
pool=obj(GG/'GuestPools.c');common=obj(GG/'GuestPrograms.c');adapter=obj(CF/'CapturedHostAdapter.c');owner=obj(GG/'GuestCapturedPrograms.c','owner-faults',['-include',GG/'RasterProgramFaults.h','-Dmalloc=raster_test_malloc','-Dcalloc=raster_test_calloc','-Dsatchel_guest_pools_release=raster_test_release','-Dsatchel_guest_pools_lease_binding=raster_test_binding']);test=obj(GG/'CapturedProgramTest.c');fw=sum([['-framework',n] for n in ['Foundation','Metal','QuartzCore','CoreGraphics','IOSurface','IOKit','AppKit']],[])
run(['xcrun','clang++',*san,pool,common,adapter,owner,test,backend,a.cpu_archive,scalar,mvk,lib,*fw,'-Wl,-rpath,'+str(lib.parent),'-o',out/'test'],'test-link');gate=run([out/'test'],'test');print(gate,flush=True)
production=obj(GG/'GuestCapturedPrograms.c');archive=out/'libSatchelGuestCapturedPrograms-macos.a';run(['xcrun','libtool','-static','-o',archive,production],'host-archive');artifacts={archive.name:h(archive)}
expectedowner=lock['exports']
def check_exports(obj,label):
 names=sorted(set(re.findall(r'\b(_satchel_guest_captured_programs_\w+)$',run(['xcrun','nm','-gU',obj],label+'-exports'),re.M)))
 if names!=expectedowner:raise RuntimeError('Owner exports differ')
check_exports(production,'host')
if a.ios:
 sdk=run(['xcrun','--sdk','iphoneos','--show-sdk-path'],'sdk');iosobj=out/'GuestCapturedPrograms-ios.o';run(['xcrun','clang','-std=c11','-O2','-Wall','-Wextra','-Werror','-target','arm64-apple-ios17.0','-isysroot',sdk,*inc,'-c',GG/'GuestCapturedPrograms.c','-o',iosobj],'ios-build');check_exports(iosobj,'ios');iosarchive=out/'libSatchelGuestCapturedPrograms.a';run(['xcrun','libtool','-static','-o',iosarchive,iosobj],'ios-archive');artifacts[iosarchive.name]=h(iosarchive)
for path,want in inputs.items():
 if h(path)!=want:raise RuntimeError("Input changed during gate "+path)
manifest={'status':'original captured guest owner host gate passed; iOS17 compiled' if a.ios else 'original captured guest owner host gate passed','gate':gate,'inputs':inputs,'builder_sha256':h(__file__),'source_lock_sha256':h(lockpath),'artifacts':artifacts,'compiler_build':str(cb),'renderer_build':str(rb),'cpu_archive':str(a.cpu_archive.resolve()),'cpu_header':str(a.cpu_header.resolve()),'exports':expectedowner,'scope':'Original checked guest memory/source/state only. Real live host capability query, actual ABI2 modules and8GPUdraws; no actual game opt-in, state defaults, phone or canonical change. Counts include byte comparisons.'}
(out/'validation-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
