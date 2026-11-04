#!/usr/bin/env python3
"""Portable original C producer for the RuntimeWindow Swift consumer test."""
from pathlib import Path
import argparse,hashlib,json,os,platform,subprocess
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--source-root',type=Path,default=ROOT)
for name in ['dependency-root','windows-manifest','cpu-archive','cpu-header','cpu-source','ipc-archive','backend-build','compiler-build','scalar-build','moltenvk-root','output']:p.add_argument('--'+name,type=Path,required=True)
a=p.parse_args();R=a.source_root.resolve()/'Tools/RuntimeWindowFrame';D=a.dependency_root.resolve();O=a.output.resolve();sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
if platform.system()!='Darwin' or platform.machine()!='arm64':raise SystemExit('Requires Apple Silicon macOS, installed Xcode and real Metal device')
if O.exists() or not O.is_relative_to(Path('/tmp').resolve()) or O.is_relative_to(a.source_root.resolve()) or O.is_relative_to(D):raise SystemExit('Use fresh external output below /tmp, outside source/dependency roots')
inputs={};source_lock=R/'source-lock.json'
def check(path,expected):
 path=Path(path).resolve()
 if not path.is_file() or sha(path)!=expected:raise SystemExit('Input provenance mismatch: '+str(path))
 inputs[str(path)]=expected
for path in [source_lock,Path(__file__),a.windows_manifest]:check(path,sha(path))
sl=json.loads(source_lock.read_text())
for rel,digest in sl['sources'].items():check(R/rel,digest)
wm=json.loads(a.windows_manifest.read_text())
if wm.get('status')!='original owner/actual Process/interpreted Window and legacy regressions passed':raise SystemExit('A passed validated Window gate manifest is required')
# Relocate source identities, never reopen prior scratch source paths. Every
# consumed repository source/header must still match the passed Window gate.
relative={}
for path,digest in wm['inputs'].items():
 rel=None
 for marker in ['/Tools/','/Core/']:
  if marker in path:rel=marker[1:]+path.split(marker,1)[1];break
 if rel:
  if rel in relative and relative[rel]!=digest:raise SystemExit('Conflicting Window source identity: '+rel)
  relative[rel]=digest;check(D/rel,digest)
window_lock=D/'Tools/GuestGraphics/WindowSourceLock.json'
if 'Tools/GuestGraphics/WindowSourceLock.json' not in relative:raise SystemExit('Window source-lock identity missing from validated gate')
wl=json.loads(window_lock.read_text())
for rel,digest in wl['sources'].items():check(D/rel,digest)
for role,path in [('archive',a.cpu_archive),('header',a.cpu_header),('source',a.cpu_source)]:check(path,wm['cpu'][role]['sha256'])
check(a.cpu_header,wl['cpu_header_sha256']);check(a.ipc_archive,wl['ipc_archive_sha256'])
# Independently bind installed backend/compiler/scalar binaries to their source
# manifests and relocated repository sources. No downloaded dependency is used.
bm_path=a.backend_build/'validation-manifest.json';bm=json.loads(bm_path.read_text());check(bm_path,sha(bm_path));backend=a.backend_build/'backend-production.o';check(backend,bm['artifacts'][backend.name])
for path,digest in bm['inputs'].items():
 if '/Tools/' in path and Path(path).suffix in ('.c','.cpp','.h','.inc'):check(D/('Tools/'+path.split('/Tools/',1)[1]),digest)
cm_path=a.compiler_build/'validation-manifest.json';cm=json.loads(cm_path.read_text());check(cm_path,sha(cm_path));compiler=a.compiler_build/'host-publish/SatchelCapturedFlat.dylib';check(compiler,cm['artifacts']['host_dylib'])
for rel,digest in cm['sources'].items():check(D/'Tools/CapturedFlatCompiler'/rel,digest)
sm_path=a.scalar_build/'validation-manifest.json';sm=json.loads(sm_path.read_text());check(sm_path,sha(sm_path));scalar=a.scalar_build/'macos/libSatchelScalarCompiler.a';check(scalar,sm['archives']['macos']['sha256'])
for rel,digest in sm['sources'].items():check(D/'Tools/ScalarCompiler'/rel,digest)
mvk=a.moltenvk_root/'macos/MoltenVK/MoltenVK/static/MoltenVK.xcframework/macos-arm64_x86_64/libMoltenVK.a';check(mvk,wl['moltenvk_macos_sha256'])
O.mkdir();private=O/'inputs';private.mkdir()
for source,name in [(a.cpu_archive,'libSatchelCPUProbe.a'),(a.cpu_header,'CPUProbe.h'),(a.cpu_source,'CPUProbe.cpp'),(a.ipc_archive,'libSatchelHorizonIPC.a')]:
 target=private/name;target.write_bytes(source.read_bytes());check(target,sha(source))
commands=[];env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1')
def run(command,name):
 commands.append(list(map(str,command)));r=subprocess.run(commands[-1],capture_output=True,text=True,env=env);(O/(name+'.log')).write_text(r.stdout+r.stderr)
 if r.returncode:raise SystemExit('Failed '+name+'; see '+str(O/(name+'.log')))
 return r.stdout.strip()
inc=[x for path in [R,D/'Core',D/'Tools/HorizonIPC',private,D/'Tools/GuestGraphics',D/'Tools/GuestVI',D/'Tools/VulkanBackend',D/'Tools/RasterCompiler',D/'Tools/ScalarCompiler',D/'Tools/CapturedFlatCompiler'] for x in ['-I',path]]
flags=['-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer']
def cc(source,name):
 obj=O/(name+'.o');run(['xcrun','clang','-std=c11',*flags,*inc,'-c',source,'-o',obj],name+'-compile');return obj
producer=cc(R/'WindowFrameBridge.c','WindowFrameBridge');test=cc(R/'Test.c','Test');parts=[]
for rel in wl['host_common_c']+['Core/'+n+'.c' for n in ['SwitchProcess','ProcessMetadata','SwitchStorageIPC','SwitchResources','SwitchEvents']]:parts.append(cc(D/rel,Path(rel).stem))
libs=[*parts,backend,private/'libSatchelCPUProbe.a',private/'libSatchelHorizonIPC.a',scalar,compiler,mvk];frameworks=[x for name in ['Foundation','Metal','QuartzCore','CoreGraphics','IOSurface','IOKit','AppKit'] for x in ['-framework',name]]
run(['xcrun','clang++',*flags,producer,test,*libs,*frameworks,'-Wl,-rpath,'+str(compiler.parent),'-o',O/'test'],'link');result=run([O/'test'],'test');print(result,flush=True)
archive=O/'libSatchelWindowFrameBridge.a';run(['xcrun','libtool','-static','-o',archive,producer],'archive');actual=sorted(n for n in run(['nm','-gUj',archive],'exports').splitlines() if n.startswith('_') and n!='____asan_globals_registered')
if actual!=sl['exports']:raise SystemExit('Unexpected C fixture exports')
for path,digest in inputs.items():check(path,digest)
out={'status':'passed','inputs':inputs,'commands':commands,'result':result,'artifact':str(archive),'artifact_sha256':sha(archive),'header':str(R/'WindowFrameBridge.h'),'header_sha256':sha(R/'WindowFrameBridge.h'),'link_inputs':{str(path.resolve()):sha(path) for path in libs},'framework_flags':frameworks,'rpath':str(compiler.parent.resolve()),'sanitizers':['address','undefined'],'exports':actual,'builder_sha256':sha(__file__),'source_lock_sha256':sha(source_lock),'windows_manifest_sha256':sha(a.windows_manifest),'dependency_root':str(D),'scope':'Original actual Process/Window/raster producer; no game or display claim; no canonical changes'}
(O/'validation-manifest.json').write_text(json.dumps(out,indent=2)+'\n')
