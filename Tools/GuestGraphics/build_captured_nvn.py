#!/usr/bin/env python3
"""Interpreted captured NVN admission boundary; no App/device/promotion actions."""
from pathlib import Path
import argparse,hashlib,json,os,platform,re,subprocess
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1]
p=argparse.ArgumentParser();p.add_argument('--source-root',type=Path,default=ROOT);p.add_argument('--compiler-build',type=Path,required=True);p.add_argument('--owner-build',type=Path,required=True);p.add_argument('--renderer-build',type=Path,required=True);p.add_argument('--cpu-archive',type=Path,required=True);p.add_argument('--cpu-header',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--ios',action='store_true');a=p.parse_args();repo=a.source_root.resolve();out=a.out.resolve();CF=repo/'Tools/CapturedFlatCompiler';GG=repo/'Tools/GuestGraphics';VK=repo/'Tools/VulkanBackend'
if platform.system()!='Darwin' or platform.machine()!='arm64':raise SystemExit('Requires installed Apple Silicon macOS/Xcode toolchain')
if out==Path('/tmp').resolve() or not out.is_relative_to(Path('/tmp').resolve()) or out.is_relative_to(ROOT.resolve()) or out.is_relative_to(repo):raise SystemExit('Scratch output under /tmp outside source is required')
if out.exists() and any(out.iterdir()):raise SystemExit('Output must be fresh')
out.mkdir(parents=True,exist_ok=True);h=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest();inputs={};lockpath=HERE/'captured-nvn-source-lock.json';lock=json.loads(lockpath.read_text())
def bind(path,expected):
 if h(path)!=expected:raise RuntimeError('Source/artifact mismatch '+str(path))
 inputs[str(Path(path).resolve())]=expected
for rel,want in lock['local_sources'].items():bind(HERE/rel,want)
for rel,want in lock['repo_sources'].items():bind(repo/rel,want)
bind(a.cpu_header,lock['cpu_header_sha256']);inputs[str(a.cpu_archive.resolve())]=h(a.cpu_archive)
cb=a.compiler_build.resolve();cm_path=cb/'validation-manifest.json';cm=json.loads(cm_path.read_text());dy=cb/'host-publish/SatchelCapturedFlat.dylib';bind(dy,cm['artifacts']['host_dylib']);bind(CF/'source-lock.json',cm['source_lock_sha256'])
for n,want in cm['sources'].items():bind(CF/n,want)
if cm['ios_exports']!=['DotNetRuntimeDebugHeader','satchel_captured_flat_admit','satchel_captured_flat_compile','satchel_captured_flat_compile_v2']:raise RuntimeError('Captured compiler export mismatch')
ob=a.owner_build.resolve();om_path=ob/'validation-manifest.json';om=json.loads(om_path.read_text());owner=ob/'libSatchelGuestCapturedPrograms-macos.a';bind(owner,om['artifacts'][owner.name])
for n in ['GuestCapturedPrograms.c','GuestCapturedPrograms.h']:
 matches=[path for path in om['inputs'] if path.endswith('/Tools/GuestGraphics/'+n)]
 if len(matches)!=1:raise RuntimeError('Missing owner source provenance')
 bind(GG/n,om['inputs'][matches[0]])
rb=a.renderer_build.resolve();rm_path=rb/'validation-manifest.json';rm=json.loads(rm_path.read_text());backend=rb/'backend-production.o';bind(backend,rm['artifacts'][backend.name]);bind(VK/'flat-source-lock.json',rm['source_lock_sha256'])
for rel,want in json.loads((VK/'flat-source-lock.json').read_text()).items():
 bind(repo/rel,want);matches=[path for path in rm['inputs'] if path.endswith('/'+rel)]
 if len(matches)!=1 or rm['inputs'][matches[0]]!=want:raise RuntimeError('Renderer input provenance mismatch')
scalar=Path('/tmp/satchel-scalar-compiler/macos/libSatchelScalarCompiler.a');sm_path=scalar.parent.parent/'validation-manifest.json';sm=json.loads(sm_path.read_text());bind(scalar,sm['archives']['macos']['sha256'])
for n in ['ScalarCompiler.c','ScalarCompiler.h']:bind(repo/'Tools/ScalarCompiler'/n,sm['sources'][n])
mvk=Path('/tmp/satchel-vulkan-backend/macos/MoltenVK/MoltenVK/static/MoltenVK.xcframework/macos-arm64_x86_64/libMoltenVK.a');matches=[path for path in rm['inputs'] if path.endswith('/macos-arm64_x86_64/libMoltenVK.a')]
if len(matches)!=1:raise RuntimeError('Missing MoltenVK provenance')
bind(mvk,rm['inputs'][matches[0]])
for path in [lockpath,cm_path,om_path,rm_path,sm_path]:inputs[str(path.resolve())]=h(path)
env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1',DYLD_LIBRARY_PATH=str(dy.parent))
def run(args,label):
 r=subprocess.run(list(map(str,args)),env=env,text=True,capture_output=True);(out/(label+'.log')).write_text(r.stdout+r.stderr)
 if r.returncode:raise RuntimeError('Gate failed '+str(out/(label+'.log')))
 return r.stdout.strip()
inc=sum([['-I',str(p)] for p in [HERE,GG,VK,CF,repo/'Tools/RasterCompiler',a.cpu_header.parent,repo/'Tools/ScalarCompiler']],[])+['-include',str(HERE/'GuestDispatch.h'),'-include',str(HERE/'GuestTransfers.h')]
san=['-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer']
def obj(p):
 inputs[str(p.resolve())]=h(p);dest=out/(p.stem+'.o');run(['xcrun','clang','-std=c11',*san,*inc,'-c',p,'-o',dest],p.stem+'-compile');return dest
names='GuestPools GuestPrograms GuestTextureLayout GuestTextures GuestRasterRun GuestRasterRecord GuestRasterState GuestCompute GuestTransfers GuestDispatch GuestVulkanRaster GuestVulkanCompute GuestVulkanTransfers GuestVulkanPools GuestRasterPrograms'.split()
objects=[obj((HERE if n in ['GuestDispatch'] else GG)/(n+'.c')) for n in names];objects += [obj(repo/'Tools/RasterCompiler/RasterCompiler.c'),obj(CF/'CapturedHostAdapter.c'),obj(HERE/'GuestVulkanCapturedRaster.c')]
frameworks=sum([['-framework',n] for n in ['Foundation','Metal','QuartzCore','CoreGraphics','IOSurface','IOKit','AppKit']],[])
t=obj(HERE/'CapturedNVNProgramTest.c');run(['xcrun','clang++',*san,*objects,t,owner,backend,a.cpu_archive,scalar,dy,mvk,*frameworks,'-Wl,-rpath,'+str(dy.parent),'-o',out/'test'],'link');print(run([out/'test'],'test'),flush=True)
# Unchanged compute fixture assertions through the new composite opt-in.
ct=obj(HERE/'CapturedNVNComputeRegression.c');compute_parts=[]
for sourceobj in objects:
 name=sourceobj.stem
 if name in ['GuestTransfers','GuestCompute']:
  target=out/(name+'-compute-fault.o');defs=['-Dsatchel_cpu_probe_writev=nvn_test_writev'] if name=='GuestTransfers' else ['-Dsatchel_vulkan_scalar_submit=nvn_test_submit','-Dsatchel_vulkan_buffer_destroy=nvn_test_destroy'];run(['xcrun','clang','-std=c11',*san,*inc,*defs,'-c',GG/(name+'.c'),'-o',target],name+'-compute-fault');compute_parts.append(target)
 else:compute_parts.append(sourceobj)
run(['xcrun','clang++',*san,*compute_parts,ct,owner,backend,a.cpu_archive,scalar,dy,mvk,*frameworks,'-Wl,-rpath,'+str(dy.parent),'-o',out/'compute'],'compute-link');print(run([out/'compute'],'compute'),flush=True)
# Test-only owner observation/cleanup failure; production objects remain unchanged.
fo=out/'GuestVulkanCapturedRaster-fault.o';run(['xcrun','clang','-std=c11',*san,*inc,'-Dsatchel_guest_captured_programs_capture=capture_test_call','-Dsatchel_guest_captured_programs_forget=forget_test_call','-c',HERE/'GuestVulkanCapturedRaster.c','-o',fo],'fault-owner');ft=obj(HERE/'CapturedNVNProgramFaultTest.c');parts=[fo if p.name=='GuestVulkanCapturedRaster.o' else p for p in objects];run(['xcrun','clang++',*san,*parts,ft,owner,backend,a.cpu_archive,scalar,dy,mvk,*frameworks,'-Wl,-rpath,'+str(dy.parent),'-o',out/'fault'],'fault-link');print(run([out/'fault'],'fault'),flush=True)
# Legacy opt-in links without captured owner/adapter/NativeAOT dependency.
legacy=obj(GG/'RasterDispatchTest.c');legacy_parts=[p for p in objects if p.stem not in ['GuestVulkanCapturedRaster','CapturedHostAdapter']];run(['xcrun','clang++',*san,*legacy_parts,legacy,backend,a.cpu_archive,scalar,mvk,*frameworks,'-o',out/'legacy-no-captured'],'legacy-link');print(run([out/'legacy-no-captured'],'legacy-no-captured'),flush=True)
artifacts={}
for name in ['GuestDispatch','GuestVulkanCapturedRaster']:
 archive=out/('libSatchel'+name+'-macos.a');run(['xcrun','libtool','-static','-o',archive,out/(name+'.o')],name+'-host-archive');artifacts[archive.name]=h(archive)
exports=run(['xcrun','nm','-gU',out/'GuestVulkanCapturedRaster.o'],'captured-host-exports')
actual=re.findall(r'^\S+ \S (_\S+)$',exports,re.M)
if sorted(actual)!=sorted(['_satchel_guest_dispatch_enable_vulkan_captured_raster','____asan_globals_registered']):raise RuntimeError('Unexpected captured host exports')
legacy_imports=run(['xcrun','nm','-u',out/'GuestVulkanRaster.o'],'legacy-imports')
if re.search(r'captured|satchel_captured',legacy_imports):raise RuntimeError('Legacy composite acquired captured dependency')
if a.ios:
 sdk=run(['xcrun','--sdk','iphoneos','--show-sdk-path'],'sdk').strip()
 for n in ['GuestDispatch','GuestVulkanCapturedRaster']:
  p=HERE/(n+'.c');ios_object=out/(n+'-ios.o');run(['xcrun','clang','-std=c11','-O2','-Wall','-Wextra','-Werror','-target','arm64-apple-ios17.0','-isysroot',sdk,*inc,'-c',p,'-o',ios_object],n+'-ios');archive=out/('libSatchel'+n+'.a');run(['xcrun','libtool','-static','-o',archive,ios_object],n+'-archive');artifacts[archive.name]=h(archive)
 defined=run(['xcrun','nm','-gU',out/'GuestVulkanCapturedRaster-ios.o'],'captured-ios-exports')
 if re.findall(r'^\S+ \S (_\S+)$',defined,re.M)!=['_satchel_guest_dispatch_enable_vulkan_captured_raster']:raise RuntimeError('Unexpected iOS composite export')
 version=run(['xcrun','vtool','-show-build',out/'GuestVulkanCapturedRaster-ios.o'],'ios-version')
 if not re.search(r'platform IOS\s+minos 17\.0\s',version):raise RuntimeError('Expected iOS17')
for path,want in inputs.items():
 if h(path)!=want:raise RuntimeError('Input changed during gate '+path)
gates={n:(out/(n+'.log')).read_text().strip() for n in ['test','compute','fault','legacy-no-captured']}
(out/'validation-manifest.json').write_text(json.dumps({'status':'interpreted captured admission and preserved legacy GPU paths passed','gates':gates,'inputs':inputs,'builder_sha256':h(__file__),'source_lock_sha256':h(lockpath),'artifacts':artifacts,'captured_export':'_satchel_guest_dispatch_enable_vulkan_captured_raster','captured_draw_or_game_claim':False,'legacy_captured_dependencies':False},indent=2)+'\n')
