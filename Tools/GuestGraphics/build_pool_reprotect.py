#!/usr/bin/env python3
"""Focused real Vulkan pool/reprotection regression. No canonical mutation."""
from pathlib import Path
import argparse, hashlib, json, os, platform, subprocess, tarfile
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
MOLTENVK=Path('/tmp/satchel-vulkan-backend')
PACKAGE_SHA='f95765a6229cb7b915990a2890ce12ebe36a730b021545d3d52ae69ce4c4024e'
def digest(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def run(args,**kwargs):return subprocess.run([str(x) for x in args],check=True,**kwargs)
def main():
 parser=argparse.ArgumentParser()
 parser.add_argument('--output',type=Path,default=Path('/tmp/satchel-pool-reprotect-build'))
 parser.add_argument('--source-root',type=Path,default=ROOT,help='Production source root; defaults to this repository.')
 parser.add_argument('--cpu-library',type=Path,default=Path('/tmp/satchel-vixl-probe/build/macos/libSatchelCPUProbe.a'))
 parser.add_argument('--cpu-checkpoint',type=Path,default=HERE/'PoolReprotectCheckpoint.json',help='Reviewed source/archive checkpoint; no bypass of source or archive verification.')
 args=parser.parse_args();root=args.source_root.resolve();out=args.output.resolve();cpu=args.cpu_library.resolve()
 if platform.system()!='Darwin' or platform.machine()!='arm64':raise SystemExit('Requires Apple Silicon macOS/Xcode.')
 if not out.is_relative_to(Path('/tmp').resolve()) or out==Path('/tmp').resolve() or out.is_relative_to(root):raise SystemExit('Output must be an external child of /tmp.')
 marker=out/'.satchel-pool-reprotect'
 if out.exists() and any(out.iterdir()) and not marker.exists():raise SystemExit('Refusing nonempty output without this task marker.')
 checkpoint=json.loads(args.cpu_checkpoint.read_text())
 if digest(cpu)!=checkpoint['cpu_archive_sha256']:raise SystemExit('CPU archive differs from reviewed checkpoint; provide its validated matching checkpoint.')
 for name,expected in checkpoint['cpu_sources'].items():
  if Path(name).name!=name or digest(root/'Tools/CPUProbe'/name)!=expected:raise SystemExit('CPU production source/header checkpoint mismatch.')
 if digest(HERE/'PoolReprotectTest.c')!=checkpoint['original_test_sha256']:raise SystemExit('Original focused fixture differs from checkpoint.')
 archive=MOLTENVK/'MoltenVK-macos.tar'
 if digest(archive)!=PACKAGE_SHA:raise SystemExit('Official MoltenVK1.4.2 package checksum mismatch.')
 package=MOLTENVK/'macos/MoltenVK/MoltenVK';lib=package/'static/MoltenVK.xcframework/macos-arm64_x86_64/libMoltenVK.a'
 with tarfile.open(archive) as tar:
  for entry in tar.getmembers():
   rel=entry.name.removeprefix('./')
   if entry.isfile() and (rel==str(lib.relative_to(MOLTENVK/'macos')) or rel.startswith('MoltenVK/MoltenVK/include/')):
    if hashlib.sha256(tar.extractfile(entry).read()).hexdigest()!=digest(MOLTENVK/'macos'/rel):raise SystemExit('Consumed MoltenVK archive/header differs from official package.')
 vk=root/'Tools/VulkanBackend';gg=root/'Tools/GuestGraphics'
 paths={cpu,args.cpu_checkpoint.resolve(),Path(__file__).resolve(),HERE/'PoolReprotectTest.c',lib,root/'Tools/ShaderBridge/Fixture.h',gg/'GuestPools.c'}
 paths.update((root/'Tools/CPUProbe'/name) for name in checkpoint['cpu_sources'])
 # Record all local headers and included backend implementation fragments; the
 # backend is rebuilt from these inputs, never a stale scratch object.
 paths.update(p for directory in [vk,gg] for p in directory.iterdir() if p.is_file() and p.suffix=='.h')
 paths.update(p for p in vk.iterdir() if p.is_file() and p.suffix in ['.cpp','.inc'])
 before={str(p):digest(p) for p in sorted(paths)}
 out.mkdir(parents=True,exist_ok=True);marker.touch()
 san=['-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer']
 inc=[x for p in [root/'Tools/CPUProbe',gg,vk,package/'include'] for x in ['-I',p]]
 objects=[]
 for src,cpp in [(gg/'GuestPools.c',False),(vk/'VulkanBackend.cpp',True),(HERE/'PoolReprotectTest.c',False)]:
  obj=out/(src.stem+'.o');run(['xcrun','clang++' if cpp else 'clang','-std=c++17' if cpp else '-std=c11',*san,*(['-Wno-missing-field-initializers'] if cpp else []),*inc,'-c',src,'-o',obj]);objects.append(obj)
 frameworks=[x for f in ['Foundation','Metal','QuartzCore','CoreGraphics','IOSurface','IOKit','AppKit'] for x in ['-framework',f]]
 exe=out/'pool-reprotect-tests';run(['xcrun','clang++',*san,*objects,cpu,lib,*frameworks,'-o',exe])
 result=run([exe],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
 if result.stdout.strip()!=checkpoint['original_gate']:raise SystemExit('Focused result/count differs from preserved original gate: '+result.stdout.strip())
 if before!={str(p):digest(p) for p in sorted(paths)}:raise SystemExit('Input changed during focused build/test.')
 manifest={'status':'focused original real-pool reprotection passed','gate':result.stdout.strip(),'source_root':str(root),'inputs':before,'official_moltenvk_package_sha256':PACKAGE_SHA,'compiler':subprocess.check_output(['xcrun','clang','--version'],text=True).strip(),'artifacts':{p.name:digest(p) for p in objects+[exe]},'production_mutated':False,'broad_gpu_gate_run':False}
 (out/'validation-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
 print(result.stdout.strip());print('Validation manifest:',out/'validation-manifest.json')
if __name__=='__main__':main()
