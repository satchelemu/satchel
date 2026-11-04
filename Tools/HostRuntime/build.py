#!/usr/bin/env python3
"""Build a host-only original-file diagnostic. No downloads or game preparation."""
from pathlib import Path
import argparse, hashlib, json, subprocess, importlib.util

HERE=Path(__file__).resolve().parent
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source-root',type=Path,default=HERE.parents[1])
parser.add_argument('--output',type=Path,default=Path('/tmp/satchel-host-runtime-build'))
parser.add_argument('--enable-nvn',action='store_true')
cpu_mode=parser.add_mutually_exclusive_group(required=True)
cpu_mode.add_argument('--cpu-checkpoint',type=Path)
cpu_mode.add_argument('--development-cpu-checkpoint',type=Path)
parser.add_argument('--cpu-archive',type=Path)
args=parser.parse_args();root=args.source_root.resolve();out=args.output.resolve()
assert out!=root and root not in out.parents and out!=HERE and HERE not in out.parents
scratch=Path('/tmp').resolve();assert out!=scratch and scratch in out.parents
marker=out/'.satchel-host-runtime-build'
if out.exists() and any(out.iterdir()) and (not marker.exists() or marker.read_text()!='1\n'):
 raise SystemExit('Refusing an unowned nonempty build directory.')
out.mkdir(parents=True,exist_ok=True);marker.write_text('1\n')
digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
cpu_checkpoint=args.development_cpu_checkpoint or args.cpu_checkpoint
cpu_inputs=[]
development_inputs={}
cpu_root=root
if args.development_cpu_checkpoint:
 from DevelopmentCPU import validate
 cpu_root,cpu,development_inputs=validate(cpu_checkpoint,root,args.cpu_archive)
 cpu_inputs=[Path(p) for p in development_inputs]
else:
 checkpoint=json.loads(args.cpu_checkpoint.read_text())
 cpu=(args.cpu_archive or Path('/tmp/satchel-vixl-probe/build/macos/libSatchelCPUProbe.a')).resolve()
 validation_path=Path(checkpoint.get('validation_manifest',args.cpu_checkpoint.parent/'validation-manifest.json'))
 assert digest(validation_path)==checkpoint['validation_manifest_sha256']
 validation=json.loads(validation_path.read_text())
 if 'entries' in checkpoint:
  # Current promotions bind changed source/archive destinations to a full
  # validation manifest. A copied archive is permitted only when its bytes
  # match both the promotion and the independently recorded build artifact.
  canonical_cpu=Path('/tmp/satchel-vixl-probe/build/macos/libSatchelCPUProbe.a').resolve()
  entries=[e for e in checkpoint['entries'] if Path(e['destination']).resolve()==canonical_cpu]
  assert len(entries)==1
  entry=entries[0]
  artifacts={Path(p).resolve():h for p,h in validation['artifacts'].items()}
  assert digest(cpu)==entry['sha256']==artifacts[Path(entry['source']).resolve()]
  assert validation['terminal_exit_code']==0 and validation['full_assertions']>0
  source=validation['sources']
  assert {'Tools/CPUProbe/CPUProbe.cpp','Tools/CPUProbe/CPUProbe.h',
          'Tools/CPUProbe/Binary64.cpp','Tools/CPUProbe/Binary64.h',
          'Tools/CPUProbe/build.py','Tools/CPUProbe/vixl-memory-guard.patch'}<=set(source)
  for relative,h in source.items():
   p=Path(relative);assert p.parent==Path('Tools/CPUProbe');assert digest(root/p)==h
  for e in checkpoint['entries']:
   p=Path(e['destination']).resolve()
   if p.parent==(root/'Tools/CPUProbe').resolve():
    assert e['sha256']==source['Tools/CPUProbe/'+p.name]==digest(p)
 else:
  # Preserve the earlier checkpoint contract for historical reproductions.
  assert digest(cpu)==checkpoint['artifacts']['/tmp/satchel-vixl-probe/build/macos/libSatchelCPUProbe.a']
  for name,h in checkpoint['source'].items(): assert digest(root/'Tools/CPUProbe'/name)==h
  for relative,h in validation['source'].items():
   p=Path(relative);assert p.parent==Path('source/Tools/CPUProbe');assert digest(root/'Tools/CPUProbe'/p.name)==h
 cpu_inputs += [validation_path]+list((root/'Tools/CPUProbe').iterdir())
 cpu_inputs=[p for p in cpu_inputs if p.is_file()]
def run(v):subprocess.run([str(x) for x in v],check=True)
def tool(*v):return subprocess.check_output(['xcrun',*v],text=True).strip()
includes=[HERE,root/'Core',cpu_root/'Tools/CPUProbe',root/'Tools/HorizonIPC',root/'Tools/GuestGraphics']
libraries=[]
graphics_provenance=[]
if args.enable_nvn:
 # Use the existing pinned verifier: installed source pin, official distribution
 # archive digest and every consumed MoltenVK library/header byte are checked.
 graphics_builder=root/'Tools/GuestGraphics/build_raster.py'
 spec=importlib.util.spec_from_file_location('host_nvn_raster_dependencies',graphics_builder)
 module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
 package,mvk=module.validate_dependencies(False)['macos']
 for lock_path in [root/'Tools/GuestGraphics/raster-source-lock.json',root/'Tools/VulkanBackend/flat-source-lock.json']:
  graphics_provenance.append(lock_path)
  for rel,want in json.loads(lock_path.read_text()).get('sources',{}).items():assert digest(root/rel)==want
 scalar=Path('/tmp/satchel-scalar-compiler/macos/libSatchelScalarCompiler.a')
 scalar_manifest_path=scalar.parent.parent/'validation-manifest.json'
 scalar_manifest=json.loads(scalar_manifest_path.read_text())
 assert digest(scalar)==scalar_manifest['archives']['macos']['sha256']
 for name in ['ScalarCompiler.c','ScalarCompiler.h']:assert digest(root/'Tools/ScalarCompiler'/name)==scalar_manifest['sources'][name]
 for name,directory in [('ScalarCompiler.h','ScalarCompiler'),('RasterCompiler.h','RasterCompiler')]:assert (root/'Tools/VulkanBackend'/name).read_bytes()==(root/'Tools'/directory/name).read_bytes()
 libraries=[scalar,mvk]
 graphics_provenance += [graphics_builder,scalar_manifest_path]
 includes += [root/'Tools/VulkanBackend',root/'Tools/RasterCompiler',root/'Tools/ScalarCompiler',package/'include']
flags=[p for d in includes for p in ['-I',str(d)]]
# Compile the exact canonical stub generator and static bounds, without pulling
# in NVN dispatch/graphics. No private or guest executable bytes are generated.
dispatch=(root/'Tools/GuestGraphics/GuestDispatch.c').read_text()
def section(start,end):
 assert dispatch.count(start)==1 and dispatch.count(end)==1
 return dispatch[dispatch.index(start):dispatch.index(end)]
stubs='#include "GuestDispatch.h"\n'+section('static const char *const names[]','typedef struct {uint64_t address,guest_token')
stubs+=section('static void put32(', 'static uint32_t get32(')
stubs+=section('int satchel_guest_dispatch_build_stubs(', 'SatchelGuestDispatch *satchel_guest_dispatch_create(')
(out/'Stubs.c').write_text(stubs)
(out/'Bridge.h').write_text('\n'.join('#include "'+n+'"' for n in (['SwitchCrypto.h','SwitchProcess.h','GuestSymbols.h','GuestDispatch.h']+(['HostNVN.h'] if args.enable_nvn else [])))+'\n')
cfiles=[root/'Core'/n for n in ['SwitchCrypto.c','NSOLoader.c','ProcessMetadata.c','SwitchProcess.c','SwitchStorageIPC.c','SwitchResources.c','SwitchEvents.c']]+[root/'Tools/GuestGraphics/GuestSymbols.c',root/'Tools/HorizonIPC/HorizonIPC.c',out/'Stubs.c']
if args.enable_nvn:
 cfiles.remove(out/'Stubs.c')
 cfiles += [root/'Tools/GuestGraphics'/(n+'.c') for n in 'GuestPools GuestPrograms GuestTextureLayout GuestTextures GuestRasterRun GuestRasterRecord GuestRasterState GuestCompute GuestTransfers GuestDispatch GuestVulkanRaster GuestVulkanCompute GuestVulkanTransfers GuestVulkanPools GuestRasterPrograms'.split()]
 cfiles += [root/'Tools/RasterCompiler/RasterCompiler.c',HERE/'HostNVN.c']
swift=[root/'App'/n for n in ['SwitchKeys.swift','SwitchControl.swift','SwitchPackage.swift','SwitchExecutable.swift','SwitchRomFS.swift','SwitchStorage.swift','SwitchTime.swift']]+[HERE/'HostRuntime.swift']
sources=graphics_provenance+swift+cfiles+[root/'Tools/GuestGraphics/GuestDispatch.c',root/'Tools/GuestGraphics/NVNNames.inc',HERE/'build.py',HERE/'DevelopmentCPU.py',cpu,cpu_checkpoint,out/'Bridge.h']+cpu_inputs
for directory in includes:
 sources.extend(sorted(directory.rglob('*.h')))
 if args.enable_nvn:sources.extend(sorted(directory.glob('*.inc')))
if args.enable_nvn:sources += [root/'Tools/VulkanBackend/VulkanBackend.cpp',root/'Tools/ShaderBridge/Fixture.h',*libraries]
inputs={str(p):digest(p) for p in sources}
for p,h in development_inputs.items():
 assert inputs[p]==h,'Reviewed CPU input changed before compilation: '+p
inputs.update(development_inputs)
objects=[]
for c in cfiles:
 obj=out/(c.stem+'.o');run(['xcrun','clang','-std=c11','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',*flags,'-c',c,'-o',obj]);objects.append(obj)
clang_resource=Path(subprocess.check_output([tool('--find','clang'),'--print-resource-dir'],text=True).strip())
runtime=clang_resource/'lib/darwin/libclang_rt.ubsan_osx_dynamic.dylib'
assert runtime.exists()
extra=[]
if args.enable_nvn:
 obj=out/'VulkanBackend.o'
 run(['xcrun','clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',*flags,'-c',root/'Tools/VulkanBackend/VulkanBackend.cpp','-o',obj]);objects.append(obj)
 extra=['-D','HOST_NVN',*libraries]+[v for n in ['Foundation','Metal','QuartzCore','CoreGraphics','IOSurface','IOKit','AppKit'] for v in ['-framework',n]]
if args.development_cpu_checkpoint:extra+=['-D','HOST_CPU_DEVELOPMENT']
run(['xcrun','swiftc','-swift-version','6','-O','-g','-sanitize=address','-import-objc-header',out/'Bridge.h',*flags,*swift,*objects,cpu,*extra,runtime,'-Xlinker','-rpath','-Xlinker',runtime.parent,'-lc++','-o',out/'host-runtime'])
for p,h in inputs.items():assert digest(Path(p))==h,'Build input changed during compilation: '+p
manifest={'scope':'Host-only opt-in checked NVN dispatch; default stops before NVN; no phone/game-frame claim','nvn_enabled':args.enable_nvn,'source_root':str(root),'libraries':[str(cpu)]+[str(p) for p in libraries],'nvn_operation_limit':sum(1 for line in (root/'Tools/GuestGraphics/NVNNames.inc').read_text().splitlines() if line.strip().startswith('"')),'cpu_validation':'focused-development' if args.development_cpu_checkpoint else 'full','cpu_checkpoint':str(cpu_checkpoint),'inputs':inputs,'generated_stubs':digest(out/'Stubs.c'),'objects':{str(p):digest(p) for p in objects},'executable':digest(out/'host-runtime')}
(out/'build-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print('Host runtime built; no game file read during build.')
