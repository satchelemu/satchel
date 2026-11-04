#!/usr/bin/env python3
"""Live original source->production compiler->typed Vulkan->GPU; no promotion."""
from pathlib import Path
import argparse,hashlib,json,os,platform,re,subprocess
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1]
def h(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--compiler-build',type=Path,required=True);ap.add_argument('--renderer-build',type=Path,required=True);ap.add_argument('--renderer-source-root',type=Path,default=ROOT);ap.add_argument('--output',type=Path,default=Path('/tmp/satchel-captured-flat-gpu-build'));a=ap.parse_args()
 out=a.output.resolve();tmp=Path('/tmp').resolve();renderer=a.renderer_source_root.resolve();vk=renderer/'Tools/VulkanBackend';cb=a.compiler_build.resolve();rb=a.renderer_build.resolve()
 if platform.system()!='Darwin' or platform.machine()!='arm64':raise SystemExit('Installed Apple Silicon macOS toolchain required.')
 if out==tmp or not out.is_relative_to(tmp) or out.is_relative_to(ROOT.resolve()) or out.is_relative_to(renderer):raise SystemExit('Output must be an owned child under /tmp outside source.')
 marker=out/'.captured-flat-gpu'
 if out.exists() and any(out.iterdir()) and (not marker.exists() or marker.read_text()!='1\n'):raise SystemExit('Refusing unowned nonempty output.')
 lock=HERE/'gpu-source-lock.json';glock=json.loads(lock.read_text())
 for rel,want in glock['files'].items():
  if h(ROOT/rel)!=want:raise SystemExit('GPU fixture source mismatch: '+rel)
 # Bind the loaded production dylib to every source from its validated compiler build.
 cm_path=cb/'validation-manifest.json';cm=json.loads(cm_path.read_text());dylib=cb/'host-publish/SatchelCapturedFlat.dylib'
 if h(dylib)!=cm['artifacts']['host_dylib']:raise SystemExit('Compiler dylib/build mismatch.')
 if h(HERE/'source-lock.json')!=cm['source_lock_sha256']:raise SystemExit('Compiler source-lock/build mismatch.')
 for rel,want in cm['sources'].items():
  if h(HERE/rel)!=want:raise SystemExit('Compiler source/build mismatch: '+rel)
 # This profile gate pins reviewed compiler implementation, not arbitrary new .NET code.
 for rel,want in glock['compiler_contract'].items():
  if h(HERE/rel)!=want:raise SystemExit('Unreviewed compiler contract: '+rel)
 rm_path=rb/'validation-manifest.json';rm=json.loads(rm_path.read_text());backend=rb/'backend-production.o'
 if h(backend)!=rm['artifacts'][backend.name]:raise SystemExit('Renderer production object/build mismatch.')
 if h(vk/'flat-source-lock.json')!=rm['source_lock_sha256']:raise SystemExit('Renderer source-lock/build mismatch.')
 rlock=json.loads((vk/'flat-source-lock.json').read_text())
 for rel,want in rlock.items():
  if h(renderer/rel)!=want:raise SystemExit('Renderer source lock mismatch: '+rel)
  matching=[p for p in rm['inputs'] if p.endswith('/'+rel)]
  if len(matching)!=1 or rm['inputs'][matching[0]]!=want:raise SystemExit('Renderer manifest/source mismatch: '+rel)
 for rel,want in glock['renderer_contract'].items():
  if h(renderer/rel)!=want:raise SystemExit('Unreviewed renderer contract: '+rel)
 # Official MoltenVK file was already byte-checked against pinned archive in renderer gate.
 mv=Path('/tmp/satchel-vulkan-backend/macos/MoltenVK/MoltenVK');lib=mv/'static/MoltenVK.xcframework/macos-arm64_x86_64/libMoltenVK.a'
 if rm['inputs'].get(str(lib))!=h(lib):raise SystemExit('MoltenVK differs from validated renderer dependency.')
 symbols=subprocess.check_output(['xcrun','nm','-gU',str(backend)],text=True)
 if re.search(r'^\S+ T _flat_',symbols,re.M):raise SystemExit('Test-hook backend is not a production object.')
 out.mkdir(parents=True,exist_ok=True);marker.write_text('1\n')
 paths=[ROOT/p for p in glock['files']]+[HERE/p for p in cm['sources']]+[renderer/p for p in rlock]+[dylib,backend,lib,cm_path,rm_path,lock,HERE/'source-lock.json',vk/'flat-source-lock.json'];before={str(p):h(p) for p in paths}
 flags=['-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-Wno-missing-field-initializers','-fsanitize=address,undefined','-fno-omit-frame-pointer'];inc=[x for p in [vk,renderer/'Tools/GuestGraphics',renderer/'Tools/RasterCompiler',HERE,mv/'include'] for x in ['-I',str(p)]];fw=[x for n in ['Foundation','Metal','QuartzCore','CoreGraphics','IOSurface','IOKit','AppKit'] for x in ['-framework',n]]
 subprocess.run(['xcrun','clang++',*flags,*inc,str(HERE/'GPUTest.cpp'),str(backend),str(dylib),str(lib),*fw,'-Wl,-rpath,'+str(dylib.parent),'-o',str(out/'test')],check=True)
 result=subprocess.run([str(out/'test')],check=True,capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'));print(result.stdout)
 if before!={p:h(p) for p in before}:raise SystemExit('Input mutated during GPU gate.')
 (out/'validation-manifest.json').write_text(json.dumps({'status':'live original compiler->typed Vulkan->GPU passed','gate':result.stdout.strip(),'builder_sha256':h(__file__),'source_lock_sha256':h(lock),'compiler_build':str(cb),'renderer_build':str(rb),'renderer_source_root':str(renderer),'inputs':before,'artifacts':{'test':h(out/'test')},'scope':'Original source/state profile; actual physical BGRA/alignment; remaining host flags explicit original-test assumptions, not a complete captured-game adapter; no precompiled SPIR-V input, phone use or canonical promotion'},indent=2)+'\n')
if __name__=='__main__':main()
