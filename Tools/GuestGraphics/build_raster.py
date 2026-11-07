#!/usr/bin/env python3
"""Reproduce the matched bounded NVN raster slice; no canonical promotion."""
from pathlib import Path
import argparse,hashlib,json,os,platform,plistlib,shutil,subprocess,tarfile
ROOT=Path(__file__).resolve().parents[2];GG=ROOT/'Tools/GuestGraphics';VK=ROOT/'Tools/VulkanBackend';RC=ROOT/'Tools/RasterCompiler'
MVK=Path('/tmp/satchel-vulkan-backend');REF=Path('/tmp/satchel-switch-integration/NX2Apple')
PIN='db66022459ffb663aa2b50f6b018bc2e124f5edf';NXPIN='8227f80f9b381c680611303319656c4e0c806a50'
HASHES={'macos':'f95765a6229cb7b915990a2890ce12ebe36a730b021545d3d52ae69ce4c4024e','ios':'b5d947b1660e6e9fed40b9cd2387e160aaab9e80b775c0cef7e14059405178c1'}
PROD='GuestPools GuestPrograms GuestTextureLayout GuestTextures GuestRasterRun GuestRasterRecord GuestRasterState GuestCompute GuestTransfers GuestDispatch GuestVulkanRaster GuestVulkanCompute GuestVulkanTransfers GuestVulkanPools GuestRasterPrograms'.split()
DIAGS='GuestVulkanDiagnostic GuestPoolDiagnostic GuestPoolDispatchDiagnostic GuestTransferDiagnostic GuestNVNComputeDiagnostic GuestNVNRasterDiagnostic'.split()
GROUPS={name:[name] for name in ['GuestDispatch','GuestPools','GuestTransfers','GuestVulkanPools','GuestVulkanTransfers','GuestVulkanCompute','GuestVulkanRaster']+DIAGS}
GROUPS.update(GuestComputeOwner=['GuestCompute','GuestPrograms'],GuestRasterResources=['GuestTextureLayout','GuestTextures','GuestRasterRun','GuestRasterRecord','GuestRasterState'],GuestRasterPrograms=['GuestRasterPrograms','RasterCompiler'])
ENV=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1')
def digest(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def run(a,**kw):subprocess.run([str(x) for x in a],check=True,**kw)
def text(a):return subprocess.check_output([str(x) for x in a],text=True).strip()
def validate_dependencies(ios):
 if platform.system()!='Darwin' or platform.machine()!='arm64':raise SystemExit('Requires installed Apple Silicon macOS/Xcode toolchain.')
 if 'Apple clang version' not in text(['xcrun','clang','--version']):raise SystemExit('Apple clang is required.')
 if text(['git','-C',MVK/'MoltenVK-source','rev-parse','HEAD'])!=PIN:raise SystemExit('Installed MoltenVK source pin mismatch.')
 packages={}
 for plat in ['macos']+(['ios'] if ios else []):
  archive=MVK/f'MoltenVK-{plat}.tar'
  if digest(archive)!=HASHES[plat]:raise SystemExit('Official MoltenVK archive checksum mismatch.')
  package=MVK/plat/'MoltenVK/MoltenVK';slice='macos-arm64_x86_64' if plat=='macos' else 'ios-arm64';lib=package/'static/MoltenVK.xcframework'/slice/'libMoltenVK.a'
  with tarfile.open(archive) as tar:
   for entry in tar.getmembers():
    rel=entry.name.removeprefix('./')
    if entry.isfile() and (rel==str(lib.relative_to(MVK/plat)) or rel.startswith('MoltenVK/MoltenVK/include/')):
     if hashlib.sha256(tar.extractfile(entry).read()).hexdigest()!=digest(MVK/plat/rel):raise SystemExit('Consumed MoltenVK binary/header differs from official archive.')
  packages[plat]=(package,lib)
 if text(['git','-C',REF,'rev-parse','HEAD'])!=NXPIN:raise SystemExit('NVN reference pin mismatch.')
 paths=['dm/gfx/nvn/nvn_obj.h','dm/gfx/nvn/include/nvn.h','dm/gfx/nvn/nvn_tex.c','dm/gfx/tex/nvn_deswizzle.c','dm/gfx/nvn/nvn_tex.h','dm/gfx/tex/nvn_deswizzle.h','dm/gfx/nvn/dmsh_abi.h']
 for name in paths:
  data=subprocess.check_output(['git','-C',str(REF),'show',NXPIN+':'+name])
  if data!=(REF/name).read_bytes():raise SystemExit('Reference test oracle differs from pinned source.')
 return packages

def main():
 parser=argparse.ArgumentParser();parser.add_argument('--ios',action='store_true');parser.add_argument('--output',type=Path,default=Path('/tmp/satchel-nvn-raster-build'));args=parser.parse_args()
 output=args.output.resolve();tmp=Path('/tmp').resolve()
 if output==tmp or not output.is_relative_to(tmp) or output.is_relative_to(ROOT.resolve()):raise SystemExit('Output must be a task-owned child directory under /tmp, outside source.')
 marker=output/'.satchel-raster-build'
 if output.exists() and any(output.iterdir()) and (not marker.exists() or marker.read_text()!='1\n'):raise SystemExit('Refusing an unowned nonempty output directory.')
 lock=json.loads((GG/'raster-source-lock.json').read_text())
 for rel,want in lock['sources'].items():
  if digest(ROOT/rel)!=want:raise SystemExit('Raster source lock mismatch: '+rel)
 # Private framework header and public compiler header are the same ABI.
 if (VK/'RasterCompiler.h').read_bytes()!=(RC/'RasterCompiler.h').read_bytes():raise SystemExit('Raster compiler header copies differ.')
 packages=validate_dependencies(args.ios);output.mkdir(parents=True,exist_ok=True);marker.write_text('1\n')
 cpu=Path(os.environ.get('SATCHEL_CPU_PROBE_ARCHIVE','/tmp/satchel-vixl-probe/build/macos/libSatchelCPUProbe.a'));cpu_hash=digest(cpu)
 scalar=Path('/tmp/satchel-scalar-compiler/macos/libSatchelScalarCompiler.a');scalar_hash=digest(scalar)
 scalar_manifest_path=scalar.parent.parent/'validation-manifest.json';scalar_manifest=json.loads(scalar_manifest_path.read_text())
 if scalar_hash!=scalar_manifest['archives']['macos']['sha256']:raise SystemExit('Scalar archive does not match its validated admission build.')
 for name in ['ScalarCompiler.c','ScalarCompiler.h']:
  if digest(ROOT/'Tools/ScalarCompiler'/name)!=scalar_manifest['sources'][name]:raise SystemExit('Scalar source/header differs from archive build manifest.')
 if (VK/'ScalarCompiler.h').read_bytes()!=(ROOT/'Tools/ScalarCompiler/ScalarCompiler.h').read_bytes():raise SystemExit('Private/public scalar headers differ.')
 host=output/'macos';host.mkdir(exist_ok=True);package,mvk=packages['macos']
 san=['-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer']
 inc=[x for p in [GG,VK,RC,ROOT/'Tools/CPUProbe',ROOT/'Tools/ScalarCompiler',package/'include'] for x in ['-I',p]]
 frameworks=[x for n in ['Foundation','Metal','QuartzCore','CoreGraphics','IOSurface','IOKit','AppKit'] for x in ['-framework',n]]
 def compile(src,name=None,extra=(),cpp=False):
  dest=host/((name or Path(src).stem)+'.o');run(['xcrun','clang++' if cpp else 'clang','-std=c++17' if cpp else '-std=c11',*san,*(['-Wno-missing-field-initializers'] if cpp else []),*inc,*extra,'-c',src,'-o',dest]);return dest
 objects={n:compile(GG/(n+'.c')) for n in PROD+DIAGS};objects['RasterCompiler']=compile(RC/'RasterCompiler.c');backend=compile(VK/'VulkanBackend.cpp','backend',cpp=True)
 common={n:objects[n] for n in PROD+['RasterCompiler']};common['backend']=backend
 logs={}
 def test(name,source,override=None,extra=(),args=(),cpp=False):
  testobj=compile(source,name,extra,cpp);parts=dict(common);parts.update(override or {});exe=host/name
  run(['xcrun','clang++',*san,*parts.values(),testobj,cpu,scalar,mvk,*frameworks,'-o',exe]);result=subprocess.run([str(exe),*[str(x) for x in args]],check=True,env=ENV,text=True,capture_output=True);logs[name]=result.stdout.strip();print(result.stdout.strip());
  if result.stderr:print(result.stderr.strip())
 # Full independently encoded source compiler and public ABI/state oracle gates.
 fixtures=host/'fixtures';fixtures.mkdir(exist_ok=True)
 test('raster-compiler',RC/'test.c',args=[fixtures])
 for module in sorted(fixtures.glob('*.spv')):run(['spirv-val','--target-env','vulkan1.1',module])
 test('raster-nvn-abi',GG/'RasterNVNABITest.c',extra=['-I',REF/'dm/gfx/nvn/include'])
 fault=compile(GG/'GuestRasterPrograms.c','raster-program-faults',['-include',GG/'RasterProgramFaults.h','-Dmalloc=raster_test_malloc','-Dcalloc=raster_test_calloc','-Dsatchel_guest_pools_release=raster_test_release','-Dsatchel_guest_pools_lease_binding=raster_test_binding'])
 test('raster-program',GG/'RasterProgramTest.c',{'GuestRasterPrograms':fault})
 oracle=compile(GG/'RasterStateOracle.c',extra=['-I',REF/'dm/gfx/nvn','-I',REF/'dm/gfx/nvn/include'])
 test('raster-state',GG/'RasterStateTest.c',{'oracle':oracle})
 layoutref=compile(REF/'dm/gfx/nvn/nvn_tex.c','layout-reference');deswizzle=compile(REF/'dm/gfx/tex/nvn_deswizzle.c','deswizzle-reference')
 test('texture-layout',GG/'TextureLayoutTest.c',{'layout-reference':layoutref,'deswizzle-reference':deswizzle})
 test('pool-budget',GG/'PoolBudgetTest.c')
 poolfault=compile(GG/'GuestPools.c','pool-faults',['-Dsatchel_vulkan_buffer_write=satchel_pool_test_buffer_write','-Dsatchel_vulkan_buffer_destroy=satchel_pool_test_buffer_destroy','-Dsatchel_vulkan_buffer_read=satchel_pool_test_buffer_read'])
 test('pool-regression',GG/'PoolTest.c',{'GuestPools':poolfault})
 texturefault=compile(GG/'GuestTextures.c','texture-faults',['-Dsatchel_vulkan_target_destroy=satchel_texture_test_destroy','-Dsatchel_guest_pools_release=satchel_texture_test_release','-Dsatchel_cpu_probe_writev=satchel_texture_test_writev'])
 test('texture-owner',GG/'TextureOwnerTest.c',{'GuestTextures':texturefault})
 batchfault=compile(VK/'VulkanBackend.cpp','batch-faults',['-include',VK/'RasterBatchFaults.h','-DSATCHEL_RASTER_BATCH_SUBMIT=batch_test_submit'],True)
 test('raster-batch',VK/'RasterBatchTest.cpp',{'backend':batchfault},cpp=True)
 runfault=compile(GG/'GuestRasterRun.c','run-faults',['-Dmalloc=run_test_malloc','-Dcalloc=run_test_calloc','-Dsatchel_vulkan_buffer_write=run_test_write','-Dsatchel_vulkan_buffer_read=run_test_read','-Dsatchel_vulkan_raster_destroy=run_test_destroy','-Dsatchel_guest_pools_release=run_test_release'])
 test('raster-run',GG/'RasterRunTest.c',{'GuestRasterRun':runfault})
 test('raster-record',GG/'RasterRecordTest.c',{'GuestRasterRun':runfault})
 queuefault=compile(GG/'GuestTransfers.c','queue-faults',['-Dsatchel_cpu_probe_writev=queue_test_writev'])
 test('raster-queue',GG/'RasterQueueTest.c',{'GuestRasterRun':runfault,'GuestTransfers':queuefault})
 test('raster-dispatch',GG/'RasterDispatchTest.c')
 test('raster-diagnostic',GG/'RasterDiagnosticTest.c',{'diagnostic':objects['GuestNVNRasterDiagnostic']})
 for diagnostic,caller in [('GuestVulkanDiagnostic','DiagnosticTest'),('GuestPoolDiagnostic','PoolDiagnosticTest'),('GuestPoolDispatchDiagnostic','PoolDispatchDiagnosticTest'),('GuestTransferDiagnostic','TransferDiagnosticTest'),('GuestNVNComputeDiagnostic','NVNComputeDiagnosticTest')]:test(caller,GG/(caller+'.c'),{'diagnostic':objects[diagnostic]})
 for diagnostic,caller,old in [('GuestTransferDiagnostic','TransferDiagnosticTest','transfers'),('GuestNVNComputeDiagnostic','NVNComputeDiagnosticTest','compute')]:
  variant=compile(GG/(diagnostic+'.c'),diagnostic+'-raster',['-Dsatchel_guest_dispatch_enable_vulkan_'+old+'=satchel_guest_dispatch_enable_vulkan_raster']);test(caller+'-raster',GG/(caller+'.c'),{'diagnostic':variant})
 artifacts={}
 def archives(directory,parts):
  for name,members in GROUPS.items():
   path=directory/('libSatchel'+name+'.a');run(['xcrun','libtool','-static','-o',path,*[parts[m] for m in members]]);artifacts[str(path.relative_to(output))]=digest(path)
 archives(host,objects)
 if args.ios:
  ios=output/'ios';ios.mkdir(exist_ok=True);sdk=text(['xcrun','--sdk','iphoneos','--show-sdk-path']);iosobjects={}
  for n in PROD+DIAGS+['RasterCompiler']:
   src=(RC if n=='RasterCompiler' else GG)/(n+'.c');obj=ios/(n+'.o');run(['xcrun','clang','-std=c11','-O2','-Wall','-Wextra','-Werror','-target','arm64-apple-ios17.0','-isysroot',sdk,*inc,'-c',src,'-o',obj]);iosobjects[n]=obj
  archives(ios,iosobjects)
  package,mvk=packages['ios'];fw=ios/'SatchelVulkan.framework';headers=fw/'Headers';headers.mkdir(parents=True,exist_ok=True)
  headernames=['VulkanBackend.h','VulkanDevice.h','VulkanBuffer.h','VulkanSubmission.h','VulkanTransferBatch.h','VulkanCompute.h','VulkanScalar.h','VulkanRenderTarget.h','VulkanRaster.h','VulkanRasterBatch.h','VulkanFlat.h','VulkanCapturedHost.h','VulkanProvoking.h','RasterCompiler.h']
  for name in headernames:shutil.copy2(VK/name,headers/name)
  shutil.copy2(ROOT/'Tools/ScalarCompiler/ScalarCompiler.h',headers/'ScalarCompiler.h');shutil.copy2(GG/'GuestDeviceBackend.h',headers/'GuestDeviceBackend.h')
  (fw/'Modules').mkdir(exist_ok=True);(fw/'Modules/module.modulemap').write_text('framework module SatchelVulkan { umbrella header "VulkanBackend.h" header "VulkanFlat.h" header "VulkanCapturedHost.h" header "VulkanProvoking.h" export * }\n')
  api=lock['framework_exports'];exports=ios/'exports.txt';exports.write_text(''.join(n+'\n' for n in sorted(api)))
  flags=['-std=c++17','-O2','-g','-fvisibility=hidden','-Wall','-Wextra','-Werror','-Wno-missing-field-initializers','-target','arm64-apple-ios17.0','-isysroot',sdk,'-I',VK,'-I',GG,'-I',package/'include']
  links=[x for n in ['Foundation','Metal','QuartzCore','CoreGraphics','IOSurface','UIKit'] for x in ['-framework',n]]
  run(['xcrun','clang++',*flags,'-dynamiclib',VK/'VulkanBackend.cpp',mvk,*links,'-Wl,-install_name,@rpath/SatchelVulkan.framework/SatchelVulkan','-Wl,-exported_symbols_list,'+str(exports),'-o',fw/'SatchelVulkan'])
  nested=fw/'SatchelVulkan.dSYM'
  if nested.exists():
   dest=ios/'SatchelVulkan.framework.dSYM'
   if dest.exists():shutil.rmtree(dest)
   shutil.move(nested,dest)
  if list(fw.rglob('*.dSYM')) or 'IOKit' in text(['otool','-L',fw/'SatchelVulkan']):raise SystemExit('Unexpected framework packaging/dependency.')
  if {l.split()[-1] for l in text(['xcrun','nm','-gU',fw/'SatchelVulkan']).splitlines()}!=set(api):raise SystemExit('Framework public ABI differs.')
  shutil.copytree(VK/'Notices',fw/'Notices',dirs_exist_ok=True);shutil.copy2(VK/'ComputeOriginal.json',fw/'Notices/compute-original.json')
  for directory,names in [('ScalarCompiler',['NOTICES.md','Reference-LICENSE.txt','source-lock.json']),('RasterCompiler',['NOTICES.md','Reference-LICENSE.txt','NVN-Header-LICENSE.txt','admission-checkpoint.json'])]:
   notices=fw/'Notices'/directory;notices.mkdir(exist_ok=True)
   for n in names:shutil.copy2(ROOT/'Tools'/directory/n,notices/n)
  info={'CFBundleIdentifier':'com.dell.satchel.vulkan','CFBundleName':'SatchelVulkan','CFBundleExecutable':'SatchelVulkan','CFBundlePackageType':'FMWK','CFBundleVersion':'1','CFBundleShortVersionString':'0.1','MinimumOSVersion':'17.0','CFBundleSupportedPlatforms':['iPhoneOS']};(fw/'Info.plist').write_bytes(plistlib.dumps(info))
  artifacts['ios/SatchelVulkan.framework/SatchelVulkan']=digest(fw/'SatchelVulkan')
 if digest(cpu)!=cpu_hash or digest(scalar)!=scalar_hash:raise SystemExit('Linked dependency changed during build; gate cannot be frozen.')
 # Additive typed flat-pair gate; preserves all preceding legacy gates.
 flat_args=['python3',VK/'build_flat.py','--output',output/'flat']+(['--ios'] if args.ios else [])
 run(flat_args)
 logs['typed-flat-manifest-sha256']=digest(output/'flat/validation-manifest.json')
 manifest={'status':'host gates passed; iOS17 compiled' if args.ios else 'host gates passed','source_lock_sha256':digest(GG/'raster-source-lock.json'),'builder_sha256':digest(Path(__file__)), 'cpu':{'archive':str(cpu),'sha256':cpu_hash,'header_sha256':digest(ROOT/'Tools/CPUProbe/CPUProbe.h'),'configuration':'Explicit external CPU checkpoint; full graphics gates run against this archive/header pair.'},'scalar':{'archive':str(scalar),'sha256':scalar_hash,'admission_manifest':str(scalar_manifest_path),'manifest_sha256':digest(scalar_manifest_path)},'compiler':text(['xcrun','clang','--version']).splitlines()[0],'sdk':text(['xcrun','--sdk','iphoneos' if args.ios else 'macosx','--show-sdk-version']),'gates':logs,'artifacts':artifacts,'no_canonical_promotion':True,'notes':'Original fixtures only. Counts include byte-level assertions, not independent scenarios. No game frame/window/present claim.'}
 (output/'validation-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n');print('Matched raster reproduction gate complete:',output/'validation-manifest.json')
if __name__=='__main__':main()
