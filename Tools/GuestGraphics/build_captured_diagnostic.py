#!/usr/bin/env python3
"""Bounded original captured-owner diagnostic; no App/device/promotion actions."""
from pathlib import Path
import argparse,hashlib,json,os,platform,re,subprocess
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1]
p=argparse.ArgumentParser();p.add_argument('--source-root',type=Path,default=ROOT);p.add_argument('--compiler-build',type=Path,required=True);p.add_argument('--owner-build',type=Path,required=True);p.add_argument('--renderer-build',type=Path,required=True);p.add_argument('--cpu-archive',type=Path,required=True);p.add_argument('--cpu-header',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--ios',action='store_true');a=p.parse_args();repo=a.source_root.resolve();out=a.out.resolve();CF=repo/'Tools/CapturedFlatCompiler';GG=repo/'Tools/GuestGraphics';VK=repo/'Tools/VulkanBackend'
if platform.system()!='Darwin' or platform.machine()!='arm64':raise SystemExit('Requires installed Apple Silicon macOS/Xcode toolchain')
if out==Path('/tmp').resolve() or not out.is_relative_to(Path('/tmp').resolve()) or out.is_relative_to(ROOT.resolve()) or out.is_relative_to(repo):raise SystemExit('Scratch output under /tmp outside source is required')
if out.exists() and any(out.iterdir()):raise SystemExit('Output must be fresh')
out.mkdir(parents=True,exist_ok=True);h=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest();inputs={};lockpath=HERE/'captured-diagnostic-source-lock.json';lock=json.loads(lockpath.read_text())
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
inc=sum([['-I',str(p)] for p in [HERE,GG,CF,VK,repo/'Tools/RasterCompiler',repo/'Tools/ScalarCompiler',a.cpu_header.parent]],[]);san=['-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer'];frameworks=sum([['-framework',n] for n in ['Foundation','Metal','QuartzCore','CoreGraphics','IOSurface','IOKit','AppKit']],[])
def obj(source,name=None,extra=()):
 dest=out/((name or source.stem)+'.o');run(['xcrun','clang','-std=c11',*san,*inc,*extra,'-c',source,'-o',dest],dest.stem+'-compile');return dest
production=obj(HERE/'GuestCapturedProgramDiagnostic.c');test=obj(HERE/'CapturedProgramDiagnosticTest.c');common=[obj(GG/'GuestPools.c'),obj(GG/'GuestPrograms.c'),obj(CF/'CapturedHostAdapter.c')];libraries=[owner,backend,a.cpu_archive,scalar,dy,mvk];gates={}
run(['xcrun','clang++',*san,production,test,*common,*libraries,*frameworks,'-Wl,-rpath,'+str(dy.parent),'-o',out/'test'],'test-link');gates['normal']=run([out/'test'],'test');print(gates['normal'],flush=True)
fault=obj(HERE/'GuestCapturedProgramDiagnostic.c','fault-diagnostic',['-Dclock_gettime=diagnostic_test_clock_gettime','-Dsatchel_vulkan_submission_wait=diagnostic_test_wait','-Dsatchel_guest_pools_try_destroy=diagnostic_test_pool_destroy']);ft=obj(HERE/'CapturedProgramDiagnosticFaultTest.c');run(['xcrun','clang++',*san,fault,ft,*common,*libraries,*frameworks,'-Wl,-rpath,'+str(dy.parent),'-o',out/'fault-test'],'fault-link');gates['pending_cleanup_clock']=run([out/'fault-test'],'fault-test');print(gates['pending_cleanup_clock'],flush=True)
exports=['_satchel_guest_captured_program_diagnostic']
def checked_export(p,label):
 defined=run(['xcrun','nm','-gU',p],label+'-exports');actual=re.findall(r'^\S+ \S (_\S+)$',defined,re.M)
 expected=exports+(['____asan_globals_registered'] if label=='host' else [])
 if sorted(actual)!=sorted(expected):raise RuntimeError('Unexpected production diagnostic exports')
 undefined=run(['xcrun','nm','-u',p],label+'-imports')
 if re.search(r'test_|fault_|raster_test',undefined):raise RuntimeError('Test seam in production object')
checked_export(production,'host');artifacts={};hostarchive=out/'libSatchelGuestCapturedProgramDiagnostic-macos.a';run(['xcrun','libtool','-static','-o',hostarchive,production],'host-archive');artifacts[hostarchive.name]=h(hostarchive)
if a.ios:
 sdk=run(['xcrun','--sdk','iphoneos','--show-sdk-path'],'sdk');ios=out/'GuestCapturedProgramDiagnostic-ios.o';run(['xcrun','clang','-std=c11','-O2','-Wall','-Wextra','-Werror','-target','arm64-apple-ios17.0','-isysroot',sdk,*inc,'-c',HERE/'GuestCapturedProgramDiagnostic.c','-o',ios],'ios-compile');checked_export(ios,'ios');version=run(['xcrun','vtool','-show-build',ios],'ios-version')
 if not re.search(r'platform IOS\s+minos 17\.0\s',version):raise RuntimeError('Expected iOS17 deployment')
 archive=out/'libSatchelGuestCapturedProgramDiagnostic.a';run(['xcrun','libtool','-static','-o',archive,ios],'ios-archive');artifacts[archive.name]=h(archive)
for path,want in inputs.items():
 if h(path)!=want:raise RuntimeError('Input changed during gate '+path)
(out/'validation-manifest.json').write_text(json.dumps({'status':'original checked-memory captured-owner GPU diagnostic passed; iOS17 compiled' if a.ios else 'original checked-memory captured-owner GPU diagnostic passed','gates':gates,'inputs':inputs,'builder_sha256':h(__file__),'source_lock_sha256':h(lockpath),'exports':exports,'artifacts':artifacts,'phone_tested':False,'game_or_display_claim':False,'production_fault_seams':False,'scope':'Fixed original sources and complete original state; actual live host facts and real GPU outputs. Pending/cleanup/clock injections exist only in a separate test object.'},indent=2)+'\n')
