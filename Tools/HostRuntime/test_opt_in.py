"""Original launcher fixtures only; never opens console keys or a game."""
from pathlib import Path
import hashlib,json,subprocess,sys,tempfile
H=Path(__file__).resolve().parent
count=0
with tempfile.TemporaryDirectory(prefix='satchel-host-nvn-launcher-') as d:
 r=Path(d);b=r/'build';b.mkdir();exe=b/'host-runtime';report=r/'report';marker=r/'ran'
 numbers=['executionTimeoutSeconds','executionElapsedMilliseconds','instructions','cpuReason','schedulerState','ipcRequests','ipcServiced','storageReads','threads','permissionsServiced','permissionChangedBytes','attributesServiced','permissionLockedBytes','nvnWindowMilliseconds','nvnEvents','nvnReturnedCalls','nvnStopStatus','nvnOperationID','nvnElapsedMilliseconds','nvnCleanupStatus','nvnRetiringContexts','nvnPendingDevices','nvnActiveContexts','nvnBackendFailures','nvnLiveDevices','nvnLiveBuffers','nvnLiveSubmissions','nvnLiveTargets','nvnLivePipelines','nvnBufferBytes','nvnTargetBytes','nvnLiveComputePipelines','nvnLiveRasterRuns','nvnRasterHostBytes']
 value={k:0 for k in numbers};value.update(hostOnly=True,graphicsImplemented=False,stopsBeforeNVN=False,nvnEnabled=True,nvnReached=False,nvnDispatcherCreated=False,executionTimeoutSeconds=240,nvnWindowMilliseconds=30000)
 def install(v,enabled=True,argv=True,private=False):
  body='import sys\nfrom pathlib import Path\nPath('+repr(str(marker))+').touch()\n'
  if argv:body+='assert sys.argv[-1]=="dispatch-nvn"\n'
  if private:body+='print("PRIVATE_FIXTURE_ONLY",file=sys.stderr)\nsys.exit(7)\n'
  else:body+='print('+repr(json.dumps(v))+')\n'
  exe.write_text('#!/usr/bin/env python3\n'+body);exe.chmod(0o700);(b/'build-manifest.json').write_text(json.dumps({'executable':hashlib.sha256(exe.read_bytes()).hexdigest(),'nvn_enabled':enabled,'nvn_operation_limit':1000}))
 def run(opt=True):return subprocess.run([sys.executable,str(H/'run.py'),'--build',str(b),'--keys',str(r/'none'),'--package',str(r/'none'),'--title','0100000000000000','--report',str(report)]+(['--dispatch-nvn'] if opt else []),capture_output=True,text=True)
 def reject(v,**kw):
  global count
  install(v,**kw);out=run();assert out.returncode and not report.exists() and 'PRIVATE_FIXTURE_ONLY' not in out.stdout+out.stderr;count+=1
 install(value,enabled=False);out=run();assert out.returncode and not marker.exists();count+=1
 reject(value,private=True)
 for k,v in [('rawGuestName','PRIVATE_FIXTURE_ONLY'),('nvnEvents',True),('nvnEvents',100001),('nvnReturnedCalls',1),('nvnWindowMilliseconds',30001),('nvnStopStatus',19),('nvnCleanupStatus',5),('nvnReached',1),('nvnDispatcherCreated',True),('nvnOperationID',1001),('nvnBufferBytes',-1),('graphicsImplemented',True),('stopsBeforeNVN',True)]:
  candidate=dict(value);candidate[k]=v;reject(candidate)
 candidate=dict(value);del candidate['nvnLiveRasterRuns'];reject(candidate)
 install(value,argv=False);out=run(False);assert out.returncode and not report.exists();count+=1
 install(value);out=run();assert out.returncode==0 and json.loads(report.read_text())==value;count+=1
 before=report.read_bytes();marker.unlink();out=run();assert out.returncode and not marker.exists() and report.read_bytes()==before;count+=1
 print(f'{count} opt-in launcher scenarios passed; strict schema, dual opt-in, private failures, bounds, no overwrite.')
