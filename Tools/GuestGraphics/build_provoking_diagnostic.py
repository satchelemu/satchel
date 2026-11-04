#!/usr/bin/env python3
"""Original source->FIRST/LAST Vulkan diagnostic; scratch outputs only."""
from pathlib import Path
import argparse,hashlib,json,os,platform,re,subprocess
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1]
def h(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--source-root',type=Path,default=ROOT);ap.add_argument('--renderer-source-root',type=Path);ap.add_argument('--compiler-build',type=Path,required=True);ap.add_argument('--renderer-build',type=Path,required=True);ap.add_argument('--adapter-build',type=Path,required=True);ap.add_argument('--out',type=Path,required=True);ap.add_argument('--ios',action='store_true');a=ap.parse_args();repo=a.source_root.resolve();rr=(a.renderer_source_root or repo).resolve();cf=repo/'Tools/CapturedFlatCompiler';vk=rr/'Tools/VulkanBackend';o=a.out.resolve();tmp=Path('/tmp').resolve()
 if platform.system()!='Darwin' or platform.machine()!='arm64':raise SystemExit('Apple Silicon macOS required.')
 if o==tmp or not o.is_relative_to(tmp) or any(o.is_relative_to(p) for p in [ROOT.resolve(),repo,rr]):raise SystemExit('Fresh task output under /tmp outside source required.')
 if o.exists() and any(o.iterdir()):raise SystemExit('Output must be fresh.')
 lockpath=HERE/'provoking-diagnostic-source-lock.json';lock=json.loads(lockpath.read_text());inputs={}
 def bind(p,want):
  if h(p)!=want:raise SystemExit('Source/artifact mismatch: '+str(p))
  inputs[str(Path(p).resolve())]=want
 for rel,want in lock['local'].items():bind(HERE/rel,want)
 for rel,want in lock['compiler_contract'].items():bind(cf/rel,want)
 for rel,want in lock['renderer_contract'].items():bind(vk/rel,want)
 cb=a.compiler_build.resolve();cp=cb/'validation-manifest.json';cm=json.loads(cp.read_text());dy=cb/'host-publish/SatchelCapturedFlat.dylib';bind(dy,cm['artifacts']['host_dylib']);bind(cf/'source-lock.json',cm['source_lock_sha256'])
 for rel,want in cm['sources'].items():bind(cf/rel,want)
 if cm['ios_exports']!=['DotNetRuntimeDebugHeader','satchel_captured_flat_admit','satchel_captured_flat_compile','satchel_captured_flat_compile_v2']:raise SystemExit('Unexpected compiler export profile.')
 rb=a.renderer_build.resolve();rp=rb/'validation-manifest.json';rm=json.loads(rp.read_text());backend=rb/'backend-production.o';bind(backend,rm['artifacts'][backend.name]);bind(vk/'flat-source-lock.json',rm['source_lock_sha256'])
 if len(rm['exports'])!=52:raise SystemExit('Validated52-export backend required.')
 for rel,want in json.loads((vk/'flat-source-lock.json').read_text()).items():
  bind(rr/rel,want);matches=[p for p in rm['inputs'] if p.endswith('/'+rel)]
  if len(matches)!=1 or rm['inputs'][matches[0]]!=want:raise SystemExit('Renderer provenance mismatch.')
 ab=a.adapter_build.resolve();am_path=ab/'validation-manifest.json';am=json.loads(am_path.read_text());adapter=ab/'adapter.o';bind(adapter,am['artifacts'][adapter.name])
 for n in ['CapturedHostAdapter.c','CapturedHostAdapter.h']:
  matches=[p for p in am['inputs'] if p.endswith('/Tools/CapturedFlatCompiler/'+n)]
  if len(matches)!=1:raise SystemExit('Adapter provenance missing.')
  bind(cf/n,am['inputs'][matches[0]])
 mvk=Path('/tmp/satchel-vulkan-backend/macos/MoltenVK/MoltenVK/static/MoltenVK.xcframework/macos-arm64_x86_64/libMoltenVK.a');bind(mvk,rm['inputs'][str(mvk)])
 for p in [cp,rp,am_path,lockpath,Path(__file__)]:inputs[str(p)]=h(p)
 o.mkdir(parents=True,exist_ok=True);env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1');san=['-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer'];inc=[x for p in [HERE,cf,vk,rr/'Tools/GuestGraphics',rr/'Tools/RasterCompiler'] for x in ['-I',p]];fw=[x for n in ['Foundation','Metal','QuartzCore','CoreGraphics','IOSurface','IOKit','AppKit'] for x in ['-framework',n]]
 def run(args,label):
  result=subprocess.run(list(map(str,args)),env=env,text=True,capture_output=True);(o/(label+'.log')).write_text(result.stdout+result.stderr)
  if result.returncode:raise SystemExit(label+' failed; see '+str(o/(label+'.log')))
  return result.stdout.strip()
 prod=o/'diagnostic.o';run(['xcrun','clang','-std=c11',*san,*inc,'-c',HERE/'GuestProvokingDiagnostic.c','-o',prod],'production')
 fault=o/'diagnostic-fault.o';defs=['-Dclock_gettime=provoking_diag_clock','-Dsatchel_vulkan_submission_wait=provoking_diag_wait','-Dsatchel_vulkan_raster_destroy=provoking_diag_destroy'];run(['xcrun','clang','-std=c11',*san,*inc,*defs,'-c',HERE/'GuestProvokingDiagnostic.c','-o',fault],'fault-production');gates={}
 for n,obj,source in [('normal',prod,'ProvokingDiagnosticTest.c'),('fault',fault,'ProvokingDiagnosticFaultTest.c')]:
  caller=o/(n+'.o');run(['xcrun','clang','-std=c11',*san,*inc,'-c',HERE/source,'-o',caller],n+'-compile');run(['xcrun','clang++',*san,obj,caller,adapter,backend,dy,mvk,*fw,'-Wl,-rpath,'+str(dy.parent),'-o',o/n],n+'-link');gates[n]=run([o/n],n);print(gates[n],flush=True)
 archive=o/'libSatchelGuestProvokingDiagnostic-macos.a';run(['xcrun','libtool','-static','-o',archive,prod],'host-archive');artifacts={archive.name:h(archive)};exports=[]
 if a.ios:
  sdk=run(['xcrun','--sdk','iphoneos','--show-sdk-path'],'sdk');obj=o/'diagnostic-ios.o';run(['xcrun','clang','-std=c11','-O2','-Wall','-Wextra','-Werror','-target','arm64-apple-ios17.0','-isysroot',sdk,*inc,'-c',HERE/'GuestProvokingDiagnostic.c','-o',obj],'ios-compile');archive=o/'libSatchelGuestProvokingDiagnostic.a';run(['xcrun','libtool','-static','-o',archive,obj],'ios-archive');text=run(['xcrun','nm','-gU',obj],'ios-exports');exports=re.findall(r'^\S+ T _(\w+)$',text,re.M)
  if exports!=['satchel_guest_provoking_diagnostic']:raise SystemExit('Unexpected production exports.')
  imports=run(['xcrun','nm','-u',obj],'ios-imports')
  if re.search(r'satchel_cpu_|provoking_diag_(clock|wait|destroy)|test_',imports):raise SystemExit('Unexpected CPU/test-only dependency.')
  if not re.search(r'platform IOS\s+minos 17\.0\s',run(['xcrun','vtool','-show-build',obj],'ios-platform')):raise SystemExit('Wrong deployment target.')
  artifacts[archive.name]=h(archive)
 if inputs!={p:h(p) for p in inputs}:raise SystemExit('Input changed during gate.')
 (o/'validation-manifest.json').write_text(json.dumps({'status':'original live compiler->FIRST/LAST GPU diagnostic passed; iOS17 compiled' if a.ios else 'original host diagnostic passed','inputs':inputs,'source_lock_sha256':h(lockpath),'builder_sha256':h(__file__),'gates':gates,'artifacts':artifacts,'ios_exports':exports,'scope':'Original encoded sources and explicit synthetic compiler-FIRST state. Independent renderer-LAST transform only. Counts include byte assertions. No NVN enum/state relaxation, guest/game/window/phone evidence.'},indent=2)+'\n')
if __name__=='__main__':main()
