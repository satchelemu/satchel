#!/usr/bin/env python3
"""Run a built host diagnostic, publishing only its explicit safe JSON fields."""
from pathlib import Path
import argparse, hashlib, json, os, re, resource, subprocess

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build',type=Path,required=True)
parser.add_argument('--keys',type=Path,required=True)
parser.add_argument('--package',type=Path,required=True)
parser.add_argument('--title',required=True)
parser.add_argument('--report',type=Path,required=True)
parser.add_argument('--seconds',type=int,default=240)
parser.add_argument('--dispatch-nvn',action='store_true')
parser.add_argument('--allow-development-cpu',action='store_true')
args=parser.parse_args()
if not 1<=args.seconds<=300:raise SystemExit('Execution time must be1..300seconds.')
if not re.fullmatch('[0-9a-f]{16}',args.title):raise SystemExit('Invalid title identifier.')
if args.report.exists():raise SystemExit('Refusing to overwrite an existing report.')
resource.setrlimit(resource.RLIMIT_CORE,(0,0))
try:
 manifest=json.loads((args.build/'build-manifest.json').read_text())
 cpu_validation=manifest.get('cpu_validation','full')
 if cpu_validation not in ['full','focused-development']:raise ValueError()
 development=cpu_validation=='focused-development'
 if development and not args.allow_development_cpu:raise ValueError()
 if args.dispatch_nvn and manifest.get('nvn_enabled') is not True:raise ValueError()
 executable=args.build/'host-runtime'
 if hashlib.sha256(executable.read_bytes()).hexdigest()!=manifest['executable']:raise ValueError()
 environment=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1')
 environment.pop('SATCHEL_HOST_CPU_DEVELOPMENT',None)
 if development and args.allow_development_cpu:environment['SATCHEL_HOST_CPU_DEVELOPMENT']='1'
 result=subprocess.run([str(args.build/'host-runtime'),str(args.keys),str(args.package),args.title,str(args.seconds)]+(['dispatch-nvn'] if args.dispatch_nvn else []),capture_output=True,text=True,timeout=600,env=environment)
 if result.returncode:raise ValueError()
 value=json.loads(result.stdout)
 flags={'hostOnly':True,'graphicsImplemented':False,'stopsBeforeNVN':not args.dispatch_nvn}
 if development:flags['cpuDevelopment']=True
 numbers={'executionTimeoutSeconds','executionElapsedMilliseconds','instructions','cpuReason','schedulerState','ipcRequests','ipcServiced','storageReads','threads','permissionsServiced','permissionChangedBytes','attributesServiced','permissionLockedBytes'}
 if args.dispatch_nvn:
  flags['nvnEnabled']=True
  numbers |= {'nvnWindowMilliseconds','nvnEvents','nvnReturnedCalls','nvnStopStatus','nvnOperationID','nvnElapsedMilliseconds','nvnCleanupStatus','nvnRetiringContexts','nvnPendingDevices','nvnActiveContexts','nvnBackendFailures','nvnLiveDevices','nvnLiveBuffers','nvnLiveSubmissions','nvnLiveTargets','nvnLivePipelines','nvnBufferBytes','nvnTargetBytes','nvnLiveComputePipelines','nvnLiveRasterRuns','nvnRasterHostBytes'}
 allowed=set(flags)|numbers|{'pc','activeModule','activeModuleOffset','svc'}
 if args.dispatch_nvn:allowed|={'nvnReached','nvnDispatcherCreated'}
 if not isinstance(value,dict) or set(value)-allowed or any(type(value.get(k)) is not bool or value[k]!=v for k,v in flags.items()):raise ValueError()
 if any(type(value.get(k)) is not int or not 0<=value[k]<2**64 for k in numbers):raise ValueError()
 if args.dispatch_nvn:
  if any(type(value.get(k)) is not bool for k in ['nvnReached','nvnDispatcherCreated']):raise ValueError()
  if value['nvnDispatcherCreated'] and not value['nvnReached']:raise ValueError()
  if value['nvnWindowMilliseconds']!=30000 or value['nvnEvents']>100000 or value['nvnReturnedCalls']>value['nvnEvents']:raise ValueError()
  if value['nvnOperationID']>manifest['nvn_operation_limit']:raise ValueError()
  if value['nvnStopStatus']>18 or value['nvnCleanupStatus']>4 or value['nvnRetiringContexts']>4 or value['nvnActiveContexts']>4:raise ValueError()
  if not value['nvnDispatcherCreated'] and (value['nvnEvents'] or value['nvnReturnedCalls']):raise ValueError()
 if value['executionTimeoutSeconds']!=args.seconds:raise ValueError()
 if value['instructions']>100_000_000 or value['threads']>32:raise ValueError()
 for k in ['pc','activeModuleOffset']:
  if k in value and (not isinstance(value[k],str) or not re.fullmatch('0x[0-9a-f]{1,16}',value[k])):raise ValueError()
 if 'activeModule' in value and value['activeModule'] not in ['rtld','main','sdk',*[f'subsdk{i}' for i in range(10)]]:raise ValueError()
 if 'svc' in value and (type(value['svc']) is not int or not 0<=value['svc']<=65535):raise ValueError()
 encoded=json.dumps(value,indent=2,sort_keys=True)+'\n'
 with args.report.open('x') as f:f.write(encoded)
 print(encoded,end='')
except Exception:
 raise SystemExit('Host diagnostic failed; private process output suppressed.')
