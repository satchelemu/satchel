#!/usr/bin/env python3
"""Original subprocess fixtures for the private-output boundary; no game input."""
from pathlib import Path
import hashlib,json,subprocess,sys,tempfile
HERE=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='satchel-host-launcher-test-') as scratch:
 root=Path(scratch);build=root/'build';build.mkdir();exe=build/'host-runtime';report=root/'report.json';marker=root/'executed'
 def install(body,valid=True):
  exe.write_text('#!/usr/bin/env python3\n'+body);exe.chmod(0o700)
  (build/'build-manifest.json').write_text(json.dumps({'executable':hashlib.sha256(exe.read_bytes()).hexdigest() if valid else '0'*64}))
 def run():return subprocess.run([sys.executable,str(HERE/'run.py'),'--build',str(build),'--keys',str(root/'not-a-key'),'--package',str(root/'not-a-game'),'--title','0100000000000000','--report',str(report)],capture_output=True,text=True)
 install('from pathlib import Path\nPath('+repr(str(marker))+').touch()\n',False)
 result=run();assert result.returncode!=0 and not marker.exists() and not report.exists()
 install('import sys\nprint("ORIGINAL_PRIVATE_ERROR_FIXTURE",file=sys.stderr)\nsys.exit(1)\n')
 result=run();assert result.returncode!=0 and 'ORIGINAL_PRIVATE_ERROR_FIXTURE' not in result.stdout+result.stderr and not report.exists()
 install('print(\'{"unexpected_private_field":"ORIGINAL_FIXTURE"}\')\n')
 result=run();assert result.returncode!=0 and 'ORIGINAL_FIXTURE' not in result.stdout+result.stderr and not report.exists()
 value={k:0 for k in ['instructions','cpuReason','schedulerState','ipcRequests','ipcServiced','storageReads','threads','permissionsServiced','permissionChangedBytes','attributesServiced','permissionLockedBytes','executionElapsedMilliseconds']}
 value.update(hostOnly=True,graphicsImplemented=False,stopsBeforeNVN=True,executionTimeoutSeconds=240)
 install('print('+repr(json.dumps(value))+')\n')
 result=run();assert result.returncode==0 and json.loads(report.read_text())==value and json.loads(result.stdout)==value
 before=report.read_bytes();install('from pathlib import Path\nPath('+repr(str(marker))+').touch()\n')
 result=run();assert result.returncode!=0 and report.read_bytes()==before and not marker.exists()
 print('5 original launcher scenarios passed: binary binding, private-error suppression, strict schema, safe report, no overwrite.')
