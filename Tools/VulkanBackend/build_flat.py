#!/usr/bin/env python3
"""Reproduce bounded typed flat-pair GPU admission with installed pinned inputs."""
from pathlib import Path
import argparse,concurrent.futures,hashlib,json,os,platform,re,subprocess,tarfile
ROOT=Path(__file__).resolve().parents[2];VK=ROOT/'Tools/VulkanBackend';MVK=Path('/tmp/satchel-vulkan-backend')
HASHES={'macos':'f95765a6229cb7b915990a2890ce12ebe36a730b021545d3d52ae69ce4c4024e','ios':'b5d947b1660e6e9fed40b9cd2387e160aaab9e80b775c0cef7e14059405178c1'}
def h(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def run(a,**kw):return subprocess.run([str(x) for x in a],check=True,**kw)
def text(a):return subprocess.check_output([str(x) for x in a],text=True).strip()
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--ios',action='store_true');ap.add_argument('--output',type=Path,default=Path('/tmp/satchel-flat-build'));args=ap.parse_args();out=args.output.resolve();tmp=Path('/tmp').resolve()
 if out==tmp or not out.is_relative_to(tmp) or out.is_relative_to(ROOT.resolve()):raise SystemExit('Output must be an owned child of /tmp outside source.')
 marker=out/'.satchel-flat-build'
 if out.exists() and any(out.iterdir()) and (not marker.exists() or marker.read_text()!='1\n'):raise SystemExit('Refusing an unowned nonempty output directory.')
 lock_path=VK/'flat-source-lock.json';lock=json.loads(lock_path.read_text())
 for rel,want in lock.items():
  if h(ROOT/rel)!=want:raise SystemExit('Flat source lock mismatch: '+rel)
 if platform.system()!='Darwin' or platform.machine()!='arm64':raise SystemExit('Apple Silicon macOS required.')
 if text(['git','-C',MVK/'MoltenVK-source','rev-parse','HEAD'])!='db66022459ffb663aa2b50f6b018bc2e124f5edf':raise SystemExit('MoltenVK source pin mismatch.')
 packages={};dependencies={}
 for plat in ['macos']+(['ios'] if args.ios else []):
  archive=MVK/f'MoltenVK-{plat}.tar'
  if h(archive)!=HASHES[plat]:raise SystemExit('Official MoltenVK package mismatch.')
  package=MVK/plat/'MoltenVK/MoltenVK';lib=package/'static/MoltenVK.xcframework'/('macos-arm64_x86_64' if plat=='macos' else 'ios-arm64')/'libMoltenVK.a'
  with tarfile.open(archive) as tar:
   for entry in tar.getmembers():
    rel=entry.name.removeprefix('./')
    if entry.isfile() and (rel==str(lib.relative_to(MVK/plat)) or rel.startswith('MoltenVK/MoltenVK/include/')):
     if hashlib.sha256(tar.extractfile(entry).read()).hexdigest()!=h(MVK/plat/rel):raise SystemExit('Consumed MoltenVK file differs from official archive.')
  packages[plat]=(package,lib);dependencies[str(lib)]=h(lib)
 provenance=json.loads((VK/'FlatFixtures/provenance.json').read_text())
 for name,want in provenance['modules'].items():
  if h(VK/'FlatFixtures'/name)!=want:raise SystemExit('Original fixture provenance mismatch.')
 out.mkdir(parents=True,exist_ok=True);marker.write_text('1\n');before={str(ROOT/p):h(ROOT/p) for p in lock};before.update(dependencies)
 san=['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-Wall','-Wextra','-Werror','-Wno-missing-field-initializers'];package,lib=packages['macos'];inc=[x for p in [VK,ROOT/'Tools/GuestGraphics',ROOT/'Tools/RasterCompiler',package/'include'] for x in ['-I',p]];fw=[x for n in ['Foundation','Metal','QuartzCore','CoreGraphics','IOSurface','IOKit','AppKit'] for x in ['-framework',n]]
 def compile(name,extra=()):
  obj=out/(name+'.o');run(['xcrun','clang++','-std=c++17',*san,*inc,*extra,'-c',VK/'VulkanBackend.cpp','-o',obj]);return obj
 backend=compile('backend-faults',['-include',VK/'FlatFaults.h','-DSATCHEL_RASTER_CREATE_PIPELINES=flat_test_pipelines','-DSATCHEL_RASTER_QUEUE_SUBMIT=flat_test_submit']);caps=compile('backend-caps',['-include',VK/'FlatCapabilitiesFaults.h','-DSATCHEL_FLAT_FORMAT_PROPERTIES=flat_caps_properties']);production=compile('backend-production')
 env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1');gates={}
 def test(name,source,obj,arguments=()):
  exe=out/name;run(['xcrun','clang++','-std=c++17',*san,*inc,VK/source,obj,lib,*fw,'-o',exe]);r=run([exe,*arguments],env=env,capture_output=True,text=True);gates[name]=r.stdout.strip();print(r.stdout.strip())
 mutations=out/'mutations';mutations.mkdir(exist_ok=True)
 for old in mutations.glob('*.spv'):old.unlink()
 test('preflight','FlatPreflightTest.cpp',backend);test('typed-pair','FlatTest.cpp',backend,[VK/'FlatFixtures',mutations]);test('capabilities','FlatCapabilitiesTest.cpp',caps)
 def validate(p):run(['spirv-val','--target-env','vulkan1.1',p],capture_output=True);return 1
 with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:validated=sum(pool.map(validate,list(mutations.glob('*.spv'))+list((VK/'FlatFixtures').glob('*.spv'))))
 print('Independent spirv-val modules:',validated);artifacts={'backend-production.o':h(production)};exports=[]
 if args.ios:
  sdk=text(['xcrun','--sdk','iphoneos','--show-sdk-path']);package,_=packages['ios'];iosinc=[x for p in [VK,ROOT/'Tools/GuestGraphics',ROOT/'Tools/RasterCompiler',package/'include'] for x in ['-I',p]];obj=out/'backend-ios.o'
  run(['xcrun','clang++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-Wno-missing-field-initializers','-target','arm64-apple-ios17.0','-isysroot',sdk,*iosinc,'-c',VK/'VulkanBackend.cpp','-o',obj]);symbols=text(['xcrun','nm','-gU',obj]);exports=sorted(re.findall(r'^\S+ T _(satchel_vulkan_\w+)$',symbols,re.M));expected=json.loads((ROOT/'Tools/GuestGraphics/raster-source-lock.json').read_text())['framework_exports'];assert ['_'+n for n in exports]==sorted(expected);assert not re.search(r'^\S+ T _flat_',symbols,re.M);assert re.search(r'platform IOS\s+minos 17\.0\s',text(['xcrun','vtool','-show-build',obj]));artifacts[obj.name]=h(obj)
 assert before=={p:h(p) for p in before}
 (out/'validation-manifest.json').write_text(json.dumps({'status':'host GPU gates passed; iOS17 compiled' if args.ios else 'host GPU gates passed','builder_sha256':h(__file__),'source_lock_sha256':h(lock_path),'inputs':before,'gates':gates,'spirv_val_modules':validated,'exports':exports,'artifacts':artifacts,'tools':{'clang':text(['xcrun','clang','--version']),'spirv_val':text(['spirv-val','--version'])},'scope':'Original compiled fixtures only; no captured C facade/GPU or game/window/display claim; counts include byte and mutation assertions'},indent=2)+'\n')
if __name__=='__main__':main()
