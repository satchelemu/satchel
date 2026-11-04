#!/usr/bin/env python3
"""Reproduce the isolated captured-source compiler gate; no installs or promotion."""
from pathlib import Path
import argparse, concurrent.futures, hashlib, json, os, plistlib, re, shutil, subprocess, tempfile
root=Path(__file__).resolve().parent
parser=argparse.ArgumentParser()
parser.add_argument('--renderer-root',type=Path,required=True)
parser.add_argument('--dotnet',type=Path,default=Path('/tmp/satchel-switch-integration/dotnet/dotnet'))
parser.add_argument('--packages',type=Path,default=Path('/tmp/satchel-shader-aot/packages'))
parser.add_argument('--out',type=Path,default=None)
a=parser.parse_args();out=(a.out.resolve() if a.out else Path(tempfile.mkdtemp(prefix="satchel-captured-flat-build-",dir="/tmp")));out.mkdir(parents=True,exist_ok=True)
h=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def check_lock(path,base):
 data=json.loads(path.read_text())
 for relative,expected in data.items():
  if h(base/relative)!=expected:raise RuntimeError('Source lock mismatch: '+relative)
 return data
sources=check_lock(root/'source-lock.json',root)
renderer=check_lock(root/'renderer-reference-lock.json',a.renderer_root)
# Preserve pinned vendor bytes; rename only the project's budget call sites in scratch.
staged_upstream=out/'renamed-upstream';staged_hashes={};renamed_files=0;renamed_sites=0
for relative in sources:
 if not relative.startswith('upstream/'):continue
 data=(root/relative).read_bytes();count=0
 if relative.endswith('.cs'):
  data,count=re.subn(rb'\b[A-Za-z_]\w*Profile(?=\.(?:WorkBudget|BoundKind)\b)',b'SatchelProfile',data)
 target=staged_upstream/relative.removeprefix('upstream/');target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
 staged_hashes[str(target.relative_to(staged_upstream))]=h(target)
 renamed_files+=bool(count);renamed_sites+=count
if (renamed_files,renamed_sites)!=(32,406):raise RuntimeError('Captured budget staging inventory changed')
(out/'staged-upstream.json').write_text(json.dumps({'files':staged_hashes,'renamed_files':renamed_files,'renamed_sites':renamed_sites},indent=2)+'\n')
env=dict(os.environ,DOTNET_GENERATE_ASPNET_CERTIFICATE='false',DOTNET_CLI_HOME=str(out/'cli'),DOTNET_CLI_TELEMETRY_OPTOUT='1',DOTNET_SKIP_FIRST_TIME_EXPERIENCE='1',NUGET_PACKAGES=str(a.packages),ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1')
def run(args,log):
 r=subprocess.run([str(x) for x in args],env=env,capture_output=True,text=True)
 (out/log).write_text(r.stdout+r.stderr)
 if r.returncode:raise RuntimeError('Gate failed: '+str(out/log))
 return r.stdout
version=run([a.dotnet,'--version'],'dotnet-version.log').strip()
if version!='10.0.403':raise RuntimeError('Unexpected installed SDK')
for rid,label in [('osx-arm64','host'),('ios-arm64','ios')]:
 args=[a.dotnet,'publish',root/'CapturedFlat.csproj','-m:2','-p:IlcMaxDegreeOfParallelism=2','--artifacts-path',out/(label+'-artifacts'),'-r',rid,'-c','Release','-o',out/(label+'-publish'),'-p:NX2AppleRoot='+str(staged_upstream),'-p:RestoreConfigFile='+str(root/'OfflineNuGet.Config'),'-p:NuGetAudit=false','-p:TreatWarningsAsErrors=true','-p:IlcTreatWarningsAsErrors=true']
 if label=='ios':args+=['-p:AppleMinOSVersion=17.0']
 run(args,label+'-build.log')
lib=out/'host-publish/SatchelCapturedFlat.dylib'
san=['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-Wall','-Wextra','-Werror','-Wno-unused-parameter']
for source,name in [('Test.c','test'),('MutationTest.c','mutation-test'),('AdmissionTest.c','admission-test')]:
 run(['xcrun','clang','-std=c11',*san,root/source,lib,'-Wl,-rpath,'+str(lib.parent),'-o',out/name],name+'-compile.log')
original=out/'original-output';mutations=out/'mutation-output';original.mkdir(exist_ok=True);mutations.mkdir(exist_ok=True)
# These are this recipe's own generated ORIGINAL output files only.
for directory in (original,mutations):
 for previous in directory.glob('original-*.spv'):previous.unlink()
gates={}
for mode in ('success','deadline','cancel','invalid'):
 gates['admission-'+mode]=run([out/'admission-test',mode],'admission-'+mode+'.log').strip();print(gates['admission-'+mode],flush=True)
for mode in ('cold-success','cold-cancel','cold-limit','cold-deadline'):
 gates[mode]=run([out/'test',mode,original],mode+'.log').strip();print(gates[mode],flush=True)
gates['mutations']=run([out/'mutation-test',mutations],'mutation.log').strip();print(gates['mutations'],flush=True)
includes=[x for p in ['Tools/VulkanBackend','Tools/GuestGraphics','Tools/RasterCompiler'] for x in ['-I',a.renderer_root/p]]
run(['xcrun','clang++','-std=c++17',*san,*includes,root/'SchemaTest.cpp','-o',out/'schema-test'],'schema-build.log')
gates['schema']=run([out/'schema-test',original,mutations],'schema.log').strip();print(gates['schema'],flush=True)
validator=shutil.which('spirv-val')
if validator is None:raise RuntimeError('Installed spirv-val required; no install attempted')
def validate(p):
 r=subprocess.run([validator,'--target-env','vulkan1.1',str(p)],capture_output=True,text=True)
 if r.returncode:raise RuntimeError('Original output SPIR-V rejected: '+str(p)+' '+r.stderr)
 return str(p.relative_to(out)),h(p)
with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:outputs=dict(pool.map(validate,sorted(original.glob('*.spv'))+sorted(mutations.glob('*.spv'))))
print(str(len(outputs))+' original output modules passed spirv-val.',flush=True)
ios=out/'ios-publish/SatchelCapturedFlat.dylib'
exports=run(['xcrun','nm','-gU',ios],'ios-exports.log')
names=sorted(re.findall(r'^\S+ \S _([^\s]+)$',exports,re.M))
if names!=['DotNetRuntimeDebugHeader','satchel_captured_flat_admit','satchel_captured_flat_compile','satchel_captured_flat_compile_v2']:raise RuntimeError('Unexpected iOS exports')
load=run(['xcrun','vtool','-show-build',ios],'ios-version.log')
if not re.search(r'platform IOS\s+minos 17\.0\s',load):raise RuntimeError('iOS17 load command missing')
framework=out/'SatchelCapturedFlat.framework'
for child in ('Headers','Modules','Notices'):(framework/child).mkdir(parents=True,exist_ok=True)
shutil.copy2(ios,framework/'SatchelCapturedFlat')
run(['install_name_tool','-id','@rpath/SatchelCapturedFlat.framework/SatchelCapturedFlat',framework/'SatchelCapturedFlat'],'framework-install-name.log')
shutil.copy2(root/'CapturedFlat.h',framework/'Headers/CapturedFlat.h')
shutil.copy2(root/'CapturedFlatV2.h',framework/'Headers/CapturedFlatV2.h')
shutil.copy2(root/'CapturedFlatAdmission.h',framework/'Headers/CapturedFlatAdmission.h')
(framework/'Modules/module.modulemap').write_text('framework module SatchelCapturedFlat {\n umbrella header "CapturedFlat.h"\n header "CapturedFlatV2.h"\n header "CapturedFlatAdmission.h"\n export *\n}\n')
with (framework/'Info.plist').open('wb') as f:plistlib.dump(dict(CFBundleIdentifier='lab.satchel.shader.captured-flat',CFBundleName='SatchelCapturedFlat',CFBundleExecutable='SatchelCapturedFlat',CFBundlePackageType='FMWK',CFBundleVersion='1',CFBundleShortVersionString='0.1',MinimumOSVersion='17.0',CFBundleSupportedPlatforms=['iPhoneOS']),f)
for notice in (root/'licenses').iterdir():shutil.copy2(notice,framework/'Notices'/notice.name)
shutil.copy2(root/'source-lock.json',framework/'Notices/source-lock.json')
for name in ('ADMISSION.md','admission-review.json'):shutil.copy2(root/name,framework/'Notices'/name)
check_lock(root/'source-lock.json',root);check_lock(root/'renderer-reference-lock.json',a.renderer_root)
for relative,expected in staged_hashes.items():
 if h(staged_upstream/relative)!=expected:raise RuntimeError('Staged upstream changed during build: '+relative)
artifacts={'host_dylib':h(lib),'ios_dylib':h(ios),'unsigned_framework':h(framework/'SatchelCapturedFlat')}
manifest={'status':'original host C API/source/schema tests passed; iOS17 compiled; unsigned scratch only','sdk':version,'runtime':'10.0.14','tests':gates,'spirv_modules':len(outputs),'original_outputs':outputs,'sources':sources,'staged_sources':staged_hashes,'source_lock_sha256':h(root/'source-lock.json'),'renderer_reference_lock_sha256':h(root/'renderer-reference-lock.json'),'renderer_root':str(a.renderer_root.resolve()),'renderer_sources':renderer,'artifacts':artifacts,'ios_exports':names,'dynamic_code_features':0,'game_shaders_tested':False,'device_execution_tested':False,'gpu_draw_tested':False,'canonical_changed':False,'spirv_val_version':run([validator,'--version'],'spirv-val-version.log').strip()}
(out/'validation-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print('Frozen evidence available at '+str(out/'validation-manifest.json'),flush=True)
