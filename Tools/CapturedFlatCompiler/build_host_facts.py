#!/usr/bin/env python3
"""Reproduce actual-device ABI2 adapter gates against matched validated builds."""
from pathlib import Path
import argparse,hashlib,json,os,platform,re,subprocess
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1]
def h(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--compiler-build',type=Path,required=True);ap.add_argument('--renderer-build',type=Path,required=True);ap.add_argument('--renderer-source-root',type=Path,default=ROOT);ap.add_argument('--ios',action='store_true');ap.add_argument('--output',type=Path,default=Path('/tmp/satchel-captured-host-build'));a=ap.parse_args();out=a.output.resolve();tmp=Path('/tmp').resolve();rr=a.renderer_source_root.resolve();vk=rr/'Tools/VulkanBackend';cb=a.compiler_build.resolve();rb=a.renderer_build.resolve()
 if platform.system()!='Darwin' or platform.machine()!='arm64':raise SystemExit('Installed Apple Silicon macOS required.')
 if out==tmp or not out.is_relative_to(tmp) or out.is_relative_to(ROOT.resolve()) or out.is_relative_to(rr):raise SystemExit('Owned output under /tmp outside sources required.')
 marker=out/'.satchel-host-facts-build'
 if out.exists() and any(out.iterdir()) and (not marker.exists() or marker.read_text()!='1\n'):raise SystemExit('Refusing unowned output.')
 lock_path=HERE/'host-facts-source-lock.json';lock=json.loads(lock_path.read_text());inputs={}
 for rel,want in lock['sources'].items():
  p=ROOT/rel
  if h(p)!=want:raise SystemExit('Host facts source mismatch: '+rel)
  inputs[str(p)]=want
 cm_path=cb/'validation-manifest.json';cm=json.loads(cm_path.read_text());dy=cb/'host-publish/SatchelCapturedFlat.dylib'
 if h(dy)!=cm['artifacts']['host_dylib'] or h(HERE/'source-lock.json')!=cm['source_lock_sha256']:raise SystemExit('Compiler build/source mismatch.')
 for rel,want in cm['sources'].items():
  p=HERE/rel
  if h(p)!=want:raise SystemExit('Compiler source mismatch: '+rel)
  inputs[str(p)]=want
 for rel,want in lock['compiler_contract'].items():
  if h(HERE/rel)!=want:raise SystemExit('Unreviewed ABI2 compiler contract: '+rel)
 if cm['ios_exports']!=['DotNetRuntimeDebugHeader','satchel_captured_flat_admit','satchel_captured_flat_compile','satchel_captured_flat_compile_v2']:raise SystemExit('Wrong compiler ABI exports.')
 rm_path=rb/'validation-manifest.json';rm=json.loads(rm_path.read_text());backend=rb/'backend-production.o'
 if h(backend)!=rm['artifacts'][backend.name] or h(vk/'flat-source-lock.json')!=rm['source_lock_sha256']:raise SystemExit('Renderer build/source mismatch.')
 rl=json.loads((vk/'flat-source-lock.json').read_text())
 for rel,want in rl.items():
  p=rr/rel;match=[old for old in rm['inputs'] if old.endswith('/'+rel)]
  if h(p)!=want or len(match)!=1 or rm['inputs'][match[0]]!=want:raise SystemExit('Renderer source mismatch: '+rel)
  inputs[str(p)]=want
 for rel,want in lock['renderer_contract'].items():
  if h(rr/rel)!=want:raise SystemExit('Unreviewed host-facts backend: '+rel)
 mv=Path('/tmp/satchel-vulkan-backend/macos/MoltenVK/MoltenVK');lib=mv/'static/MoltenVK.xcframework/macos-arm64_x86_64/libMoltenVK.a'
 if rm['inputs'].get(str(lib))!=h(lib):raise SystemExit('MoltenVK dependency mismatch.')
 for p in [cm_path,rm_path,dy,backend,lib,lock_path]:inputs[str(p)]=h(p)
 out.mkdir(parents=True,exist_ok=True);marker.write_text('1\n');flags=['-O1','-g','-Wall','-Wextra','-Werror','-Wno-missing-field-initializers','-fsanitize=address,undefined','-fno-omit-frame-pointer'];inc=[x for p in [HERE,vk,rr/'Tools/GuestGraphics',rr/'Tools/RasterCompiler',mv/'include'] for x in ['-I',p]];fw=[x for n in ['Foundation','Metal','QuartzCore','CoreGraphics','IOSurface','IOKit','AppKit'] for x in ['-framework',n]];env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1');gates={}
 def run(args,name):
  r=subprocess.run([str(x) for x in args],env=env,capture_output=True,text=True);(out/(name+'.log')).write_text(r.stdout+r.stderr)
  if r.returncode:raise RuntimeError('Failed '+name+'; see '+str(out/(name+'.log')))
  return r.stdout.strip()
 run(['xcrun','clang','-std=c11',*flags,*inc,'-c',HERE/'CapturedHostAdapter.c','-o',out/'adapter.o'],'adapter-build')
 run(['xcrun','clang++','-std=c++17',*flags,*inc,'-include',HERE/'HostFactsFaults.h','-DSATCHEL_CAPTURED_HOST_FORMAT_PROPERTIES=host_test_format','-c',vk/'VulkanBackend.cpp','-o',out/'backend-faults.o'],'facts-backend')
 for source,name,obj in [('HostFactsTest.cpp','facts',out/'backend-faults.o'),('HostFactsV2Test.cpp','abi2',backend),('HostFactsGPUTest.cpp','gpu',backend)]:
  run(['xcrun','clang++','-std=c++17',*flags,*inc,HERE/source,out/'adapter.o',obj,dy,lib,*fw,'-Wl,-rpath,'+str(dy.parent),'-o',out/name],name+'-build');gates[name]=run([out/name],name);print(gates[name],flush=True)
 artifacts={'adapter.o':h(out/'adapter.o')}
 if a.ios:
  sdk=subprocess.check_output(['xcrun','--sdk','iphoneos','--show-sdk-path'],text=True).strip();run(['xcrun','clang','-std=c11','-O2','-Wall','-Wextra','-Werror','-target','arm64-apple-ios17.0','-isysroot',sdk,*inc,'-c',HERE/'CapturedHostAdapter.c','-o',out/'adapter-ios.o'],'adapter-ios');run(['xcrun','libtool','-static','-o',out/'libSatchelCapturedHostAdapter.a',out/'adapter-ios.o'],'adapter-archive')
  symbols=subprocess.check_output(['xcrun','nm','-gU',str(out/'libSatchelCapturedHostAdapter.a')],text=True);assert re.findall(r'^\S+ T _(\w+)$',symbols,re.M)==['satchel_captured_compile_for_target'];assert re.search(r'platform IOS\s+minos 17\.0\s',subprocess.check_output(['xcrun','vtool','-show-build',str(out/'adapter-ios.o')],text=True));artifacts['libSatchelCapturedHostAdapter.a']=h(out/'libSatchelCapturedHostAdapter.a')
 if inputs!={p:h(p) for p in inputs}:raise SystemExit('Input changed during gate.')
 (out/'validation-manifest.json').write_text(json.dumps({'status':'actual-device/target ABI2 and real original GPU gates passed; iOS17 adapter compiled' if a.ios else 'host gates passed','gates':gates,'inputs':inputs,'source_lock_sha256':h(lock_path),'builder_sha256':h(__file__),'artifacts':artifacts,'compiler_build':str(cb),'renderer_build':str(rb),'scope':'Exact admitted profile; queried/policy/source-unused facts distinct. Original source/state only, no game/phone/window/presentation claim. Counts include pixel/byte assertions.'},indent=2)+'\n')
if __name__=='__main__':main()
