#!/usr/bin/env python3
from pathlib import Path
import argparse,hashlib,json,os,platform,re,subprocess,tempfile
if not __debug__:raise RuntimeError('Run without Python optimization; validation assertions are required')
here=Path(__file__).resolve().parent
p=argparse.ArgumentParser(description='Original production NativeAOT coexistence; no framework rebuild or device run.')
p.add_argument('--source-root',type=Path,required=True)
p.add_argument('--old-checkpoint',type=Path,required=True)
p.add_argument('--new-build',type=Path,required=True)
p.add_argument('--new-layout',choices=['captured-v1','host-facts-v2'],default='captured-v1',help='Explicit producer manifest layout; both are tested through their original v1 facade.')
p.add_argument('--compiler-source-root',type=Path,help='V1 producer source root; default repository Tools/CapturedFlatCompiler.')
p.add_argument('--old-framework',type=Path,default=Path('/tmp/satchel-shader-aot/SatchelShaderProbe.framework'))
p.add_argument('--ilcompiler-package',type=Path,default=Path('/tmp/satchel-shader-aot/packages/microsoft.dotnet.ilcompiler/10.0.14'))
p.add_argument('--output',type=Path,help='New output directory outside the source tree; defaults to a fresh /tmp directory.')
a=p.parse_args();root=a.source_root.resolve();old=a.old_checkpoint.resolve();new=a.new_build.resolve()
out=a.output.resolve() if a.output else Path(tempfile.mkdtemp(prefix='satchel-nativeaot-coexist-build-',dir='/tmp'))
if out==root or root in out.parents or out==here or here in out.parents:raise RuntimeError('Output must be outside source trees')
if a.output:out.mkdir(parents=True,exist_ok=False)
if platform.system()!='Darwin' or platform.machine()!='arm64':raise RuntimeError('Requires installed macOS arm64 artifacts')
h=lambda q:hashlib.sha256(q.read_bytes()).hexdigest()
om_path=old/'validation-manifest.json';om=json.loads(om_path.read_text());nm_path=new/'validation-manifest.json';nm=json.loads(nm_path.read_text())
oldlib=old/'repro/host-production-publish/SatchelShaderProbe.dylib'
oldfw=a.old_framework.resolve();oldsrc=root/'Tools/MaxwellShaderCompiler'
if a.new_layout=='captured-v1':
 newlib=new/'host-publish/SatchelCapturedFlat.dylib';newfw=new/'SatchelCapturedFlat.framework'
 newsrc=a.compiler_source_root.resolve() if a.compiler_source_root else root/'Tools/CapturedFlatCompiler'
 assert h(newlib)==nm['artifacts']['host_dylib'] and h(newfw/'SatchelCapturedFlat')==nm['artifacts']['unsigned_framework']
 for n,v in nm['sources'].items():assert h(newsrc/n)==v
 assert h(newsrc/'source-lock.json')==nm['source_lock_sha256']
else:
 if a.compiler_source_root:raise RuntimeError('host-facts-v2 source root is bound by its producer stage')
 newlib=new/'build/host-publish/SatchelCapturedFlat.dylib';newfw=new/'build/SatchelCapturedFlat.framework';newsrc=new/'Tools/CapturedFlatCompiler'
 for n in ['build/host-publish/SatchelCapturedFlat.dylib','build/SatchelCapturedFlat.framework/SatchelCapturedFlat']:assert h(new/n)==nm['artifacts'][n]
 for n,v in nm['sources'].items():assert h(new/n)==v
 assert h(new/'source-lock.json')==nm['source_lock_sha256']
 assert h(new/'source-delta.json')==nm['source_delta_sha256']
assert h(oldlib)==om['artifacts']['repro/host-production-publish/SatchelShaderProbe.dylib']['sha256']
assert h(oldfw/'SatchelShaderProbe')==om['artifacts']['SatchelShaderProbe.framework/SatchelShaderProbe']['sha256']
for n in ['FixtureCompiler.cs','FixtureSpirv.cs','SatchelShaderFixture.h','MaxwellShaderCompiler.csproj','source-lock.json']:assert h(oldsrc/n)==om['source_hashes'][n]
assert (oldfw/'Headers/SatchelShaderFixture.h').read_bytes()==(oldsrc/'SatchelShaderFixture.h').read_bytes()
assert (newfw/'Headers/CapturedFlat.h').read_bytes()==(newsrc/'CapturedFlat.h').read_bytes()
inputs=[here/'Coexist.c',here/'ios/LinkOnly.c',here/'build.py',here/'README.md',oldlib,newlib,oldfw/'SatchelShaderProbe',newfw/'SatchelCapturedFlat',om_path,nm_path,oldsrc/'SatchelShaderFixture.h',oldsrc/'SatchelShaderProbe.h',newsrc/'CapturedFlat.h']
inputs += [oldsrc/n for n in ['FixtureCompiler.cs','FixtureSpirv.cs','SatchelShaderFixture.h','MaxwellShaderCompiler.csproj','source-lock.json']]
inputs += [(newsrc if a.new_layout=='captured-v1' else new)/n for n in nm['sources']]
inputs += [newsrc/'source-lock.json'] if a.new_layout=='captured-v1' else [new/'source-lock.json',new/'source-delta.json']
installed=a.ilcompiler_package.resolve()
inputs += [installed/'microsoft.dotnet.ilcompiler.nuspec',installed/'build/Microsoft.NETCore.Native.Unix.targets',installed/'build/Microsoft.NETCore.Native.targets']
before={str(q):h(q) for q in inputs}
env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1')
def run(cmd,log):
 v=subprocess.run([str(x) for x in cmd],env=env,capture_output=True,text=True,timeout=60)
 (out/log).write_text(v.stdout+v.stderr)
 if v.returncode:raise RuntimeError('Gate failed: '+str(out/log))
 return v.stdout
flags=['-std=c11','-Wall','-Wextra','-Werror','-O1','-g','-fsanitize=address,undefined','-I',oldsrc,'-I',newsrc]
run(['xcrun','clang',*flags,here/'Coexist.c','-o',out/'dynamic-test'],'dynamic-build.log')
run(['xcrun','clang','-DCOEXIST_LINKED',*flags,here/'Coexist.c',oldlib,newlib,'-Wl,-rpath,'+str(oldlib.parent),'-Wl,-rpath,'+str(newlib.parent),'-o',out/'linked-test'],'linked-build.log')
results={};total=0
for style in ('dynamic','linked'):
 for mode in ('old-first','new-first','nested-first','cold-cancel','cold-limit','cold-invalid'):
  key=style+'-'+mode;result=run([out/(style+'-test'),oldlib,newlib,mode],key+'.log').strip();assert 'coexistence assertions passed' in result;results[key]=result;total+=int(result.split()[0]);print(result,flush=True)
ios=out/'ios-link-only'
run(['xcrun','--sdk','iphoneos','clang','-target','arm64-apple-ios17.0','-Wall','-Wextra','-Werror','-F'+str(oldfw.parent),'-F'+str(newfw.parent),'-framework','SatchelShaderProbe','-framework','SatchelCapturedFlat','-Wl,-rpath,@executable_path/Frameworks',here/'ios/LinkOnly.c','-o',ios],'ios-link.log')
load=run(['xcrun','otool','-L',ios],'ios-load.log');assert '@rpath/SatchelShaderProbe.framework/SatchelShaderProbe' in load and '@rpath/SatchelCapturedFlat.framework/SatchelCapturedFlat' in load
version=run(['xcrun','vtool','-show-build',ios],'ios-version.log');assert re.search(r'platform IOS\s+minos 17\.0\s',version)
undefined=run(['xcrun','nm','-u',ios],'ios-imports.log');assert all('_'+name in undefined for name in ['satchel_shader_probe','satchel_shader_fixture_compile','satchel_shader_runtime_features','satchel_captured_flat_compile'])
exports={};headers={}
for label,lib in [('old',oldlib),('new',newlib)]:
 s=run(['xcrun','nm','-gU',lib],label+'-exports.log');exports[label]=sorted(re.findall(r'^\S+ \S _([^\s]+)$',s,re.M));headers[label]=run(['xcrun','otool','-hv',lib],label+'-mach-header.log');assert 'TWOLEVEL' in headers[label]
assert set(exports['old'])&set(exports['new'])=={'DotNetRuntimeDebugHeader'}
for q,v in before.items():assert h(Path(q))==v
manifest={'status':'original macOS coexistence passed; iOS17 link only; no canonical/App/device changes','producer_layout':a.new_layout,'tested_entry_abi':'v1 in both production libraries; additional ABI2 entry is not called','assertions':total,'fresh_processes':12,'concurrent_compiles_per_process':512,'tests':results,'inputs':before,'exports':exports,'host':platform.platform(),'artifacts':{str(q.relative_to(out)):h(q) for q in [out/'dynamic-test',out/'linked-test',ios]},'old_producer_status_note':'historical producer canonical_promoted=false is not rewritten; current canonical framework identity was separately verified','ios_executed':False,'gpu_executed':False,'libraries_unloaded':False,'managed_object_exchange':False,'source_assumptions':'original fixtures with explicit synthetic host feature profile; no game shader or complete live GPU binding'}
(out/'validation-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n');print(f'{total} assertions passed; manifest {out/"validation-manifest.json"}',flush=True)
