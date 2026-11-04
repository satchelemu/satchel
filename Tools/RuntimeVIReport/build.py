#!/usr/bin/env python3
"""Compile actual App VI/report code against real C dependencies, without changing inputs."""
import argparse,hashlib,json,os,pathlib,re,shutil,subprocess
P=pathlib.Path;HERE=P(__file__).resolve().parent
H=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 parser=argparse.ArgumentParser(description=__doc__)
 parser.add_argument('--source-root',type=P,default=HERE.parent.parent,help='Project root (default: recipe repository), or overlay root containing App/')
 parser.add_argument('--dependency-root',type=P,help='Project root containing Core/Tools (defaults to source-root)')
 parser.add_argument('--output',type=P,required=True,help='Fresh output directory outside both source roots')
 parser.add_argument('--cpu-archive',type=P,required=True,help='Explicit validated ASan/UBSan host CPU archive')
 parser.add_argument('--cpu-validation',type=P,help='Optional exact CPU producer manifest; verifies artifact and CPU header binding')
 a=parser.parse_args();src=a.source_root.resolve();dep=(a.dependency_root or src).resolve();out=a.output.resolve();cpu=a.cpu_archive.resolve()
 if out.exists() or out==src or src in out.parents or out==dep or dep in out.parents:parser.error('output must be fresh and outside source roots')
 if not cpu.is_file():parser.error('CPU archive must exist')
 out.mkdir(parents=True);snapshot=out/'input';snapshot.mkdir();objects=out/'objects';objects.mkdir();inputs={}
 def copy(original,destination):
  original=original.resolve();destination.parent.mkdir(parents=True,exist_ok=True);before=H(original);shutil.copy2(original,destination);assert H(destination)==before
  inputs[str(original)]={'copy':str(destination.relative_to(out)),'sha256':before};return destination
 app=['SwitchRuntimeSession.swift','SwitchCapturedVulkanReport.swift','SwitchProvokingVulkanReport.swift','SwitchTextureFrameVulkanReport.swift','SwitchRuntimeSurfaceState.swift','SwitchRuntimeVI.swift','SwitchWindowReport.swift','Bridge.h']
 for n in app:copy(src/'App'/n,snapshot/'App'/n)
 dirs=['Core','Tools/CPUProbe','Tools/ShaderBridge','Tools/VulkanBackend','Tools/GuestGraphics','Tools/GuestVI','Tools/HorizonIPC','Tools/ScalarCompiler','Tools/RasterCompiler','Tools/CapturedFlatCompiler']
 for d in dirs:
  for p in sorted((dep/d).glob('*.h')):copy(p,snapshot/d/p.name)
 cfiles=['Core/SwitchProcess.c','Core/NSOLoader.c','Core/ProcessMetadata.c','Core/SwitchStorageIPC.c','Core/SwitchResources.c','Core/SwitchEvents.c','Core/SwitchCrypto.c','Tools/HorizonIPC/HorizonIPC.c','Tools/GuestGraphics/GuestSymbols.c','Tools/GuestGraphics/GuestDispatch.c','Tools/GuestVI/GuestVI.c','Tools/GuestVI/GuestVIDispatch.c']
 for n in cfiles+['Tools/GuestGraphics/NVNNames.inc']:copy(dep/n,snapshot/n)
 for n in ['Test.swift','Fixture.c','Fixture.h','LegacyReport.json','build.py']:copy(HERE/n,snapshot/'fixture'/n)
 library=copy(cpu,snapshot/'libSatchelCPUProbe.a')
 validation=None
 if a.cpu_validation:
  p=copy(a.cpu_validation,snapshot/'cpu-validation.json');v=json.loads(p.read_text());assert H(cpu) in v['artifacts'].values(),'CPU artifact not bound by supplied validation'
  expected=v['source']['source/Tools/CPUProbe/CPUProbe.h'];assert H(snapshot/'Tools/CPUProbe/CPUProbe.h')==expected,'CPU header mismatch'
  validation={'path':str(a.cpu_validation.resolve()),'sha256':H(p)}
 raw=(snapshot/'App/SwitchRuntimeSession.swift').read_bytes();start=raw.index(b'struct SwitchModuleReport: Codable {');end=raw.index(b'private struct DecodedSwitchModule {');assert start<end
 body=raw[start:end];declarations=out/'ReportDeclarations.swift';declarations.write_bytes(b'import Foundation\n\n'+body)
 bridge=out/'TestBridge.h';bridge.write_text('#include "input/App/Bridge.h"\n#include "input/fixture/Fixture.h"\n')
 include=[snapshot/'fixture']+[snapshot/d for d in dirs];flags=['-std=c11','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer']
 for d in include:flags+=['-I',d]
 commands=[];env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1')
 def run(args):
  args=list(map(str,args));commands.append(args);(out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n');subprocess.run(args,check=True,env=env)
 objs=[]
 for n in cfiles+['fixture/Fixture.c']:
  p=snapshot/n;o=objects/(p.stem+'.o');run(['xcrun','clang',*flags,'-c',p,'-o',o]);objs.append(o)
 resource=P(subprocess.check_output(['xcrun','clang','-print-resource-dir'],text=True).strip());ubsan=resource/'lib/darwin/libclang_rt.ubsan_osx_dynamic.dylib';assert ubsan.exists()
 swift=[declarations]+[snapshot/'App'/n for n in app if n.endswith('.swift') and n!='SwitchRuntimeSession.swift']+[snapshot/'fixture/Test.swift']
 command=['xcrun','swiftc','-swift-version','5','-warnings-as-errors','-sanitize=address','-g','-Onone','-parse-as-library','-import-objc-header',bridge]
 for d in include:command+=['-I',d]
 executable=out/'test';command+=swift+objs+[library,'-lc++','-Xlinker',ubsan,'-o',executable];run(command)
 result=subprocess.run([str(executable),str(snapshot/'fixture/LegacyReport.json')],text=True,capture_output=True,env=env);(out/'test.log').write_text(result.stdout+result.stderr)
 print(result.stdout,end='');print(result.stderr,end='');result.check_returncode()
 count=re.search(r'(\d+) VI Swift report and actual C early-refusal assertions passed\.',result.stdout);assert count and int(count.group(1))==49
 for original,item in inputs.items():assert H(P(original))==item['sha256'] and H(out/item['copy'])==item['sha256'],'input changed during gate'
 symbols=subprocess.check_output(['xcrun','nm','-gU',str(executable)],text=True)
 required=['satchel_guest_vi_dispatch','satchel_guest_vi_execute','satchel_switch_process_claim_identity','satchel_switch_process_validate_identity','satchel_guest_dispatch_check_output_range','satchel_guest_symbols_find_function','satchel_cpu_probe_run','satchel_hipc_parse_request'];assert all('_'+n in symbols for n in required)
 manifest={'status':'passed','assertions':int(count.group(1)),'inputs':inputs,'cpu_validation':validation,'source_root':str(src),'dependency_root':str(dep),'cpu_archive_sha256':H(library),'compiled_actual_runtime_vi_unchanged':True,'declaration_prefix':{'start':start,'end':end,'raw_slice_sha256':hashlib.sha256(body).hexdigest(),'output_sha256':H(declarations)},'test_log_sha256':H(out/'test.log'),'executable_sha256':H(executable),'commands_sha256':H(out/'commands.json'),'actual_linked_symbols':required,'original_inputs_unchanged_after_gate':True,'scope':'Sanitized public actual diagnostic report; synthetic modern report and original C CPU fixture. No original game bytes, GPU/device/App execution or rendering claim.'}
 (out/'validation-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n');print('VI report fixture validated; source snapshots and real C dependencies bound.')
if __name__=='__main__':main()
