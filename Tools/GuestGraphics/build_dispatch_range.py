#!/usr/bin/env python3
"""Build checked dispatcher output-range tests with an explicit frozen CPU."""
from pathlib import Path
import argparse,hashlib,json,os,platform,re,subprocess

def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--source-root',type=Path,default=Path(__file__).resolve().parents[2])
 p.add_argument('--cpu',type=Path,required=True)
 p.add_argument('--cpu-sha256',required=True)
 p.add_argument('--output',type=Path,required=True)
 a=p.parse_args();root=a.source_root.resolve();out=a.output.resolve();cpu=a.cpu.resolve()
 if platform.system()!='Darwin' or platform.machine()!='arm64':raise SystemExit('Requires the Apple arm64 toolchain.')
 if out==root or root in out.parents or out.exists():raise SystemExit('Use a fresh output directory outside the source tree.')
 def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
 if not re.fullmatch('[0-9a-f]{64}',a.cpu_sha256) or sha(cpu)!=a.cpu_sha256:raise SystemExit('CPU archive provenance mismatch.')
 graphics=root/'Tools/GuestGraphics'
 dirs=[graphics,*[root/'Tools'/n for n in ['CPUProbe','ScalarCompiler','RasterCompiler','VulkanBackend']]]
 sources=[graphics/n for n in ['GuestDispatch.c','DispatchRangeTest.c','DispatchTest.c']]
 pending=list(sources);consumed=set()
 while pending:
  path=pending.pop().resolve()
  if path in consumed:continue
  consumed.add(path)
  for name in re.findall(r'^#include "([^"]+)"',path.read_text(),re.M):
   found=next((d/name for d in [path.parent,*dirs] if (d/name).is_file()),None)
   if found is None:raise SystemExit('Missing included header: '+name)
   pending.append(found)
 consumed.update([Path(__file__).resolve(),cpu]);before={str(p):sha(p) for p in sorted(consumed)}
 out.mkdir(parents=True)
 flags=['-std=c11','-O1','-g','-Wall','-Wextra','-Werror'];san=['-fsanitize=address,undefined','-fno-omit-frame-pointer'];inc=[x for d in dirs for x in ['-I',d]]
 def run(args,name):
  result=subprocess.run(list(map(str,args)),capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
  (out/(name+'.log')).write_text(result.stdout+result.stderr)
  if result.returncode:raise SystemExit(name+' failed; inspect '+str(out/(name+'.log')))
  return result.stdout
 run(['xcrun','clang',*flags,*san,*inc,'-c',sources[0],'-o',out/'dispatch.o'],'dispatch-compile')
 results={}
 for name in ['DispatchRangeTest','DispatchTest']:
  run(['xcrun','clang',*flags,*san,*inc,'-c',graphics/(name+'.c'),'-o',out/(name+'.o')],name+'-compile')
  run(['xcrun','clang++',*san,out/'dispatch.o',out/(name+'.o'),cpu,'-o',out/name],name+'-link')
  results[name]=run([out/name],name);print(results[name],end='')
 sdk=subprocess.check_output(['xcrun','--sdk','iphoneos','--show-sdk-path'],text=True).strip()
 run(['xcrun','clang','-std=c11','-O2','-Wall','-Wextra','-Werror',*inc,'-target','arm64-apple-ios17.0','-isysroot',sdk,'-c',sources[0],'-o',out/'GuestDispatch-ios.o'],'ios-compile')
 run(['xcrun','libtool','-static','-o',out/'libSatchelGuestDispatch.a',out/'GuestDispatch-ios.o'],'ios-archive')
 exports=run(['xcrun','nm','-gUj',out/'libSatchelGuestDispatch.a'],'ios-exports').splitlines()
 actual=sorted(v for v in exports if v.startswith('_'))
 expected=sorted('_satchel_guest_dispatch_'+n for n in ['attach_checked_pool_services','attach_pool_services','attach_transfer_services','breakpoints','build_stubs','check_output_range','create','destroy','device_capabilities','device_count','handle','handoff','name','resolve_pool','retire','retirement_poll','retirement_stats','set_device_backend','set_device_backend_v2'])
 if actual!=expected:raise SystemExit('Unexpected production iOS exports; retain evidence.')
 if before!={str(p):sha(p) for p in sorted(consumed)}:raise SystemExit('An input changed during validation.')
 manifest={'inputs':before,'results':results,'sdk':sdk,'ios_exports':actual,'artifacts':{f.name:sha(f) for f in out.iterdir() if f.is_file()},'scope':'Synthetic backend/service guard tests and existing dispatcher regression; no actual GPU or phone test.'}
 (out/'validation-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
if __name__=='__main__':main()
