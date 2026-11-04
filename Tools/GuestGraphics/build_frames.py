from pathlib import Path
import argparse,hashlib,json,os,platform,re,subprocess,tarfile
HERE=Path(__file__).resolve().parent
parser=argparse.ArgumentParser();parser.add_argument('--source-root',type=Path,default=HERE.parents[1]);parser.add_argument('--output',type=Path,default=Path('/tmp/satchel-frame-handoff-build'));a=parser.parse_args();root=a.source_root.resolve();out=a.output.resolve()
if platform.system()!='Darwin' or platform.machine()!='arm64':raise SystemExit('Apple Silicon macOS/Xcode required.')
if not out.is_relative_to(Path('/tmp').resolve()) or out==Path('/tmp').resolve() or out.is_relative_to(root):raise SystemExit('External scratch output required.')
marker=out/'.satchel-frame-build'
if out.exists() and any(out.iterdir()) and not marker.exists():raise SystemExit('Unowned nonempty output.')
h=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();mv=Path('/tmp/satchel-vulkan-backend');package=mv/'macos/MoltenVK/MoltenVK';lib=package/'static/MoltenVK.xcframework/macos-arm64_x86_64/libMoltenVK.a';archive=mv/'MoltenVK-macos.tar'
assert h(archive)=='f95765a6229cb7b915990a2890ce12ebe36a730b021545d3d52ae69ce4c4024e'
with tarfile.open(archive) as t:
 for e in t.getmembers():
  rel=e.name.removeprefix('./')
  if e.isfile() and (rel==str(lib.relative_to(mv/'macos')) or rel.startswith('MoltenVK/MoltenVK/include/')):assert hashlib.sha256(t.extractfile(e).read()).hexdigest()==h(mv/'macos'/rel)
vk=root/'Tools/VulkanBackend';rc=root/'Tools/RasterCompiler';gg=root/'Tools/GuestGraphics'
lock_path=HERE/'FrameSourceLock.json';lock=json.loads(lock_path.read_text())
inputs=[]
for group,base in [('overlay',HERE.parents[1]),('reference',root)]:
 for relative,expected in lock[group].items():
  path=base/relative
  if h(path)!=expected:raise SystemExit('Source checkpoint mismatch: '+relative)
  inputs.append(path)
inputs.extend([lock_path,lib,archive])
before={str(p):h(p) for p in inputs};out.mkdir(parents=True,exist_ok=True);marker.touch()
def run(x,**kw):return subprocess.run([str(y) for y in x],check=True,**kw)
san=['-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer'];inc=[x for d in [HERE,gg,vk,rc,package/'include'] for x in ['-I',d]]
objects=[]
for source,cpp,extra in [(vk/'VulkanBackend.cpp',True,[]),(rc/'RasterCompiler.c',False,[]),(HERE/'GuestFrames.c',False,['-include',HERE/'FrameFaults.h','-DSATCHEL_FRAME_BYTES_ALLOC=frame_test_allocate','-DSATCHEL_FRAME_BUFFER_READ=frame_test_read','-DSATCHEL_FRAME_BUFFER_DESTROY=frame_test_destroy','-DSATCHEL_FRAME_SUBMIT=frame_test_submit']),(HERE/'GuestFramesTest.cpp',True,[])]:
 obj=out/(source.stem+'.o');run(['xcrun','clang++' if cpp else 'clang','-std=c++17' if cpp else '-std=c11',*san,*(['-Wno-missing-field-initializers'] if cpp else []),*inc,*extra,'-c',source,'-o',obj]);objects.append(obj)
frameworks=[x for f in ['Foundation','Metal','QuartzCore','CoreGraphics','IOSurface','IOKit','AppKit'] for x in ['-framework',f]];exe=out/'frames-test';run(['xcrun','clang++',*san,*objects,lib,*frameworks,'-o',exe]);result=run([exe],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'));print(result.stdout)
sdk=subprocess.check_output(['xcrun','--sdk','iphoneos','--show-sdk-path'],text=True).strip();ios=out/'ios';ios.mkdir(exist_ok=True)
run(['xcrun','clang','-std=c11','-O2','-Wall','-Wextra','-Werror','-target','arm64-apple-ios17.0','-isysroot',sdk,*inc,'-c',HERE/'GuestFrames.c','-o',ios/'GuestFrames.o']);run(['xcrun','libtool','-static','-o',ios/'libSatchelGuestFrames.a',ios/'GuestFrames.o'])
symbols=subprocess.check_output(['xcrun','nm','-gU',str(ios/'libSatchelGuestFrames.a')],text=True)
exports=sorted(re.findall(r'^\S+ T _(\w+)$',symbols,re.M))
expected=sorted('satchel_guest_frames_'+name for name in ['create','attach','detach','begin','poll','take','copy','release','collect','try_destroy','stats'])
assert exports==expected, 'Unexpected production exports'
build_version=subprocess.check_output(['xcrun','vtool','-show-build',str(ios/'GuestFrames.o')],text=True)
assert re.search(r'platform IOS\s+minos 17\.0\s',build_version), 'Expected iOS17 minimum'
assert before=={str(p):h(p) for p in inputs}
(out/'validation-manifest.json').write_text(json.dumps({'status':'original host handoff passed;production iOS17 compiled','gate':result.stdout.strip(),'exports':exports,'minimum_ios':'17.0','builder_sha256':h(Path(__file__)),'source_lock_sha256':h(lock_path),'tools':{'clang':subprocess.check_output(['xcrun','clang','--version'],text=True).strip(),'iphoneos_sdk':sdk},'inputs':before,'artifacts':{str(p.relative_to(out)):h(p) for p in [exe,ios/'libSatchelGuestFrames.a']},'scope':'scratch only;no App/dispatcher/VI/window/display claim'},indent=2)+'\n')
