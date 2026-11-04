#!/usr/bin/env python3
"""Focused current-texture-mirror/frame ownership gate. No canonical writes."""
from pathlib import Path
import argparse,difflib,hashlib,json,os,platform,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def text(c):return subprocess.check_output([str(v) for v in c],text=True).strip()
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--output',type=Path);ap.add_argument('--dependency-root',type=Path,default=ROOT);ap.add_argument('--cpu-archive',type=Path,required=True);ap.add_argument('--cpu-header',type=Path,required=True);ap.add_argument('--cpu-source',type=Path,required=True);ap.add_argument('--backend-build',type=Path,default=Path('/tmp/satchel-provoking-flat-build'));ap.add_argument('--compiler-build',type=Path,default=Path('/tmp/satchel-guest-captured-compiler-final'));ap.add_argument('--scalar-build',type=Path,default=Path('/tmp/satchel-scalar-compiler'));ap.add_argument('--moltenvk-root',type=Path,default=Path('/tmp/satchel-vulkan-backend'));ap.add_argument('--ios',action='store_true');a=ap.parse_args()
 if platform.system()!='Darwin' or platform.machine()!='arm64':raise SystemExit('Requires installed Apple arm64 toolchain and Metal device.')
 D=a.dependency_root.resolve();S=ROOT/'Tools/GuestGraphics';O=(a.output or Path(tempfile.mkdtemp(prefix='satchel-texture-frames-',dir='/tmp'))).resolve()
 if O==ROOT or ROOT in O.parents:raise SystemExit('Build output must remain outside source tree.')
 if a.output and O.exists() and any(O.iterdir()):raise SystemExit('Use a fresh output directory; preserve previous evidence.')
 O.mkdir(parents=True,exist_ok=True);lockpath=S/'TextureFramesSourceLock.json';lock=json.loads(lockpath.read_text());bound={str(lockpath):sha(lockpath),str(Path(__file__)):sha(__file__)}
 def check(p,h):
  p=Path(p)
  if not p.is_file() or sha(p)!=h:raise SystemExit('Input provenance mismatch: '+str(p))
  bound[str(p)]=h
 for n,h in lock['new_sources'].items():check(ROOT/n,h)
 for n,h in lock['baseline_sources'].items():check(D/n,h)
 # Exact old composites remain untouched; new composite is a separate object.
 for n,h in lock['legacy_composites'].items():check(D/n,h)
 derivation=json.loads((S/'TextureFramesDerivation.json').read_text());baseline=(D/derivation['baseline']).read_text();new=(ROOT/derivation['new']).read_text();patch=''.join(difflib.unified_diff(baseline.splitlines(True),new.splitlines(True),fromfile='GuestVulkanCapturedRaster.c',tofile='GuestVulkanTextureFrames.c'))
 if hashlib.sha256(patch.encode()).hexdigest()!=derivation['review_patch_sha256']:raise SystemExit('Composite derivation changed.')
 check(a.cpu_header,lock['cpu_header_sha256']);cpu=O/'inputs/libSatchelCPUProbe.a';cpu.parent.mkdir();cpu.write_bytes(a.cpu_archive.read_bytes());(cpu.parent/'CPUProbe.h').write_bytes(a.cpu_header.read_bytes());(cpu.parent/'CPUProbe.cpp').write_bytes(a.cpu_source.read_bytes());cpu_info={k:{'path':str(p.resolve()),'sha256':sha(p)} for k,p in [('archive',a.cpu_archive),('header',a.cpu_header),('source',a.cpu_source)]}
 backend_manifest=a.backend_build/'validation-manifest.json';bm=json.loads(backend_manifest.read_text());backend=a.backend_build/'backend-production.o';check(backend,bm['artifacts']['backend-production.o']);bound[str(backend_manifest)]=sha(backend_manifest)
 for n,h in bm['inputs'].items():
  if '/Tools/' in n and Path(n).suffix in ('.c','.cpp','.h','.inc'):
   rel='Tools/'+n.split('/Tools/',1)[1];check(D/rel,h)
 compiler_manifest=a.compiler_build/'validation-manifest.json';cm=json.loads(compiler_manifest.read_text());compiler=a.compiler_build/'host-publish/SatchelCapturedFlat.dylib';check(compiler,cm['artifacts']['host_dylib']);bound[str(compiler_manifest)]=sha(compiler_manifest)
 for n,h in cm['sources'].items():check(D/'Tools/CapturedFlatCompiler'/n,h)
 scalar_manifest=a.scalar_build/'validation-manifest.json';sm=json.loads(scalar_manifest.read_text());scalar=a.scalar_build/'macos/libSatchelScalarCompiler.a';check(scalar,sm['archives']['macos']['sha256']);bound[str(scalar_manifest)]=sha(scalar_manifest)
 for n,h in sm['sources'].items():check(D/'Tools/ScalarCompiler'/n,h)
 mvk=a.moltenvk_root/'macos/MoltenVK/MoltenVK/static/MoltenVK.xcframework/macos-arm64_x86_64/libMoltenVK.a';check(mvk,lock['moltenvk_macos_sha256'])
 G=D/'Tools/GuestGraphics';incs=[S,cpu.parent,G,D/'Tools/VulkanBackend',D/'Tools/RasterCompiler',D/'Tools/ScalarCompiler',D/'Tools/CapturedFlatCompiler',O]
 flags=['-std=c11','-O1','-g','-Wall','-Wextra','-Werror'];includes=[v for p in incs for v in ['-I',str(p)]];san=['-fsanitize=address,undefined','-fno-omit-frame-pointer'];env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1')
 commands=[]
 def run(c,name):
  commands.append([str(v) for v in c]);res=subprocess.run(commands[-1],text=True,capture_output=True,env=env);(O/(name+'.log')).write_text(res.stdout+res.stderr)
  if res.returncode:raise SystemExit('Check failed: '+name+'; see '+str(O/(name+'.log')))
  return res.stdout.strip()
 def compile_c(path,dest,extra=()):run(['xcrun','clang',*flags,*san,*includes,*extra,'-c',path,'-o',dest],dest.stem+'-compile')
 common=[]
 for n in lock['host_common_c']:
  p=D/n;obj=O/(p.stem+'.o');compile_c(p,obj);common.append(obj)
 # Test-only mechanical entrypoint/opt-in rename; source bytes and exact recipe locked.
 original=(G/'RasterDispatchTest.c').read_text();assert original.count('int main(void)')==1
 (O/'PreservedRasterDispatch.c').write_text(original.replace('int main(void)','int preserved_raster_main(void)').replace('satchel_guest_dispatch_enable_vulkan_raster','satchel_guest_dispatch_enable_vulkan_texture_frames'))
 artifacts={};results={};frameworks=[v for n in ['Foundation','Metal','QuartzCore','CoreGraphics','IOSurface','IOKit','AppKit'] for v in ['-framework',n]]
 for variant in ['normal','faults']:
  objs=[]
  for n in ['GuestTextureFrames','GuestVulkanTextureFrames','GuestFrames']:
   path=G/(n+'.c') if n=='GuestFrames' else S/(n+'.c');extra=[]
   if variant=='faults' and n=='GuestTextureFrames':extra=['-Dmalloc=tf_malloc','-Dcalloc=tf_calloc','-Dsatchel_vulkan_target_upload=tf_upload','-Dsatchel_vulkan_buffer_destroy=tf_staging_destroy','-Dsatchel_guest_pools_release=tf_pool_release','-Dsatchel_guest_textures_release=tf_texture_release']
   if variant=='faults' and n=='GuestFrames':extra=['-include',S/'TextureFrameFaults.h','-DSATCHEL_FRAME_BUFFER_READ=tf_frame_read','-DSATCHEL_FRAME_BUFFER_DESTROY=tf_frame_destroy','-DSATCHEL_FRAME_SUBMIT=tf_frame_submit']
   obj=O/(n+'-'+variant+'.o');compile_c(path,obj,extra);objs.append(obj)
  testobj=O/('test-'+variant+'.o');compile_c(S/'TextureFramesTest.c',testobj,['-DTEXTURE_FRAME_FAULTS'] if variant=='faults' else []);exe=O/('test-'+variant)
  run(['xcrun','clang++',*san,*common,*objs,testobj,backend,cpu,scalar,compiler,mvk,*frameworks,'-Wl,-rpath,'+str(compiler.parent),'-o',exe],variant+'-link');results[variant]=run([exe],variant);print(results[variant])
  if variant=='normal':
   for n in ['GuestTextureFrames','GuestVulkanTextureFrames']:
    archive=O/('libSatchel'+n+'-macos.a');run(['xcrun','libtool','-static','-o',archive,O/(n+'-normal.o')],n+'-host-archive');artifacts[archive.name]=sha(archive)
 # Route existing captured/compute assertions through ONLY the new opt-in.
 for name in ['CapturedNVNProgramTest','CapturedNVNProgramFaultTest','CapturedNVNComputeRegression']:
  source=(G/(name+'.c')).read_text().replace('"GuestVulkanCapturedRaster.h"','"GuestVulkanTextureFrames.h"').replace('satchel_guest_dispatch_enable_vulkan_captured_raster','satchel_guest_dispatch_enable_vulkan_texture_frames')
  (O/(name+'.c')).write_text(source)
 base_objects=common+[O/'GuestTextureFrames-normal.o',O/'GuestFrames-normal.o']
 for mode,testname in [('captured','CapturedNVNProgramTest'),('captured-fault','CapturedNVNProgramFaultTest'),('compute','CapturedNVNComputeRegression')]:
  testobj=O/(mode+'-test.o');compile_c(O/(testname+'.c'),testobj);parts=list(base_objects);composite=O/'GuestVulkanTextureFrames-normal.o'
  if mode=='captured-fault':
   composite=O/'GuestVulkanTextureFrames-captured-fault.o';compile_c(S/'GuestVulkanTextureFrames.c',composite,['-Dsatchel_guest_captured_programs_capture=capture_test_call','-Dsatchel_guest_captured_programs_forget=forget_test_call'])
  if mode=='compute':
   for name,defs in [('GuestTransfers',['-Dsatchel_cpu_probe_writev=nvn_test_writev']),('GuestCompute',['-Dsatchel_vulkan_scalar_submit=nvn_test_submit','-Dsatchel_vulkan_buffer_destroy=nvn_test_destroy'])]:
    obj=O/(name+'-compute-fault.o');compile_c(G/(name+'.c'),obj,defs);parts=[obj if v.name==name+'.o' else v for v in parts]
  exe=O/('test-'+mode);run(['xcrun','clang++',*san,*parts,composite,testobj,backend,cpu,scalar,compiler,mvk,*frameworks,'-Wl,-rpath,'+str(compiler.parent),'-o',exe],mode+'-link');results[mode]=run([exe],mode);print(results[mode])
 if a.ios:
  sdk=text(['xcrun','--sdk','iphoneos','--show-sdk-path'])
  for n in ['GuestTextureFrames','GuestVulkanTextureFrames']:
   obj=O/(n+'-ios.o');run(['xcrun','clang',*flags,*includes,'-target','arm64-apple-ios17.0','-isysroot',sdk,'-c',S/(n+'.c'),'-o',obj],n+'-ios-compile');archive=O/('libSatchel'+n+'.a');run(['xcrun','libtool','-static','-o',archive,obj],n+'-ios-archive')
   symbols=text(['nm','-gUj',archive]).splitlines();actual=sorted(v for v in symbols if v.startswith('_'))
   if actual!=lock['exports'][n]:raise SystemExit('Unexpected production exports for '+n)
   undefined=text(['nm','-uj',archive]);
   if any(x in undefined for x in ['tf_','test_','run_test_']):raise SystemExit('Production contains a test seam.')
   artifacts[archive.name]=sha(archive);(O/(n+'-exports.json')).write_text(json.dumps(actual,indent=2)+'\n')
 for p,h in bound.items():check(p,h)
 # Original passed gate's immutable sources/artifacts are not rewritten.
 manifest={'status':'focused original host sanitizer gates passed; iOS17 compiled' if a.ios else 'focused original host sanitizer gates passed','builder_sha256':sha(__file__),'source_lock_sha256':sha(lockpath),'sources':bound,'cpu':cpu_info,'backend_manifest':str(backend_manifest.resolve()),'compiler_manifest':str(compiler_manifest.resolve()),'scalar_manifest':str(scalar_manifest.resolve()),'artifacts':artifacts,'tests':results,'commands':commands,'toolchain':text(['xcrun','clang','--version']),'scope':'Original interpreted texture/current GPU-mirror snapshot ownership; no Window/VI/display/game-frame support; no canonical mutation.'}
 (O/'validation-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n');print('Manifest:',O/'validation-manifest.json')
if __name__=='__main__':main()
