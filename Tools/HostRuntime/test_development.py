#!/usr/bin/env python3
"""Original fixtures for focused-only provenance and explicit launch admission."""
from pathlib import Path
import hashlib,json,os,subprocess,sys,tempfile
from DevelopmentCPU import NAMES,validate

HERE=Path(__file__).resolve().parent
digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
with tempfile.TemporaryDirectory(prefix='satchel-development-test-') as scratch:
 root=Path(scratch).resolve();source=root/'source';directory=source/'Tools/CPUProbe';directory.mkdir(parents=True)
 for name in NAMES:(directory/name).write_text('original fixture '+name)
 archive=root/'cpu.a';archive.write_text('original archive fixture')
 def save(name,value):
  path=root/name;path.write_text(json.dumps(value));return {'path':str(path),'sha256':digest(path)}
 sources={'Tools/CPUProbe/'+name:digest(directory/name) for name in NAMES}
 checkpoint=save('source-checkpoint.json',{'files':{'source/'+p:h for p,h in sources.items()}})
 focused=save('focused.json',{'exit_code':0,'checks':1,'source_checkpoint_sha256':checkpoint['sha256'],'archive_sha256':digest(archive)})
 review=save('review.json',{'decision':'clear','source_checkpoint_sha256':checkpoint['sha256']})
 record={'kind':'focused-development','cpu_source_root':str(source),'sources':sources,'host_archive':str(archive),'artifacts':{str(archive):digest(archive)},'source_checkpoint':checkpoint,'focused':focused,'review':review}
 path=root/'development.json';path.write_text(json.dumps(record))
 assert validate(path,source)[:2]==(source,archive)
 subset=save('subset.json',{'files':{'source/Tools/CPUProbe/CPUProbe.cpp':sources['Tools/CPUProbe/CPUProbe.cpp']}})
 altered=dict(record,source_checkpoint=subset)
 altered['focused']=save('subset-focused.json',{'exit_code':0,'checks':1,'source_checkpoint_sha256':subset['sha256'],'archive_sha256':digest(archive)})
 altered['review']=save('subset-review.json',{'decision':'clear','source_checkpoint_sha256':subset['sha256']})
 path.write_text(json.dumps(altered))
 try:validate(path,source)
 except AssertionError:pass
 else:raise AssertionError('incomplete reviewed source set accepted')
 path.write_text(json.dumps(record))
 (directory/'CPUProbe.cpp').write_text('changed source fixture')
 try:validate(path,source)
 except AssertionError:pass
 else:raise AssertionError('stale source accepted')
 (directory/'CPUProbe.cpp').write_text('original fixture CPUProbe.cpp')
 archive.write_text('different archive fixture')
 try:validate(path,source)
 except AssertionError:pass
 else:raise AssertionError('different archive accepted')
 archive.write_text('original archive fixture')
 (root/'focused.json').write_text('{}')
 try:validate(path,source)
 except AssertionError:pass
 else:raise AssertionError('changed focused proof accepted')

 build=root/'build';build.mkdir();exe=build/'host-runtime';marker=root/'executed';report=root/'report.json'
 value={k:0 for k in ['instructions','cpuReason','schedulerState','ipcRequests','ipcServiced','storageReads','threads','permissionsServiced','permissionChangedBytes','attributesServiced','permissionLockedBytes','executionElapsedMilliseconds']}
 value.update(hostOnly=True,graphicsImplemented=False,stopsBeforeNVN=True,executionTimeoutSeconds=240,cpuDevelopment=True)
 def install(mode,payload):
  exe.write_text('#!/usr/bin/env python3\nimport os,sys\nif os.environ.get("SATCHEL_HOST_CPU_DEVELOPMENT")!="1":sys.exit(2)\nfrom pathlib import Path\nPath('+repr(str(marker))+').touch()\nprint('+repr(json.dumps(payload))+')\n');exe.chmod(0o700)
  (build/'build-manifest.json').write_text(json.dumps({'executable':digest(exe),'cpu_validation':mode}))
 def run(opt=False):
  return subprocess.run([sys.executable,str(HERE/'run.py'),'--build',str(build),'--keys',str(root/'no-key'),'--package',str(root/'no-game'),'--title','0100000000000000','--report',str(report)]+(['--allow-development-cpu'] if opt else []),capture_output=True,text=True,env=dict(os.environ,SATCHEL_HOST_CPU_DEVELOPMENT='1'))
 install('focused-development',value)
 assert run().returncode!=0 and not marker.exists() and not report.exists()
 assert run(True).returncode==0 and json.loads(report.read_text())==value
 report.unlink();marker.unlink()
 install('unknown',value)
 assert run(True).returncode!=0 and not marker.exists()
 install('full',value)
 assert run(True).returncode!=0 and not report.exists() and not marker.exists()
 install('focused-development',{k:v for k,v in value.items() if k!='cpuDevelopment'})
 assert run(True).returncode!=0 and not report.exists()
 print('10 development scenarios passed: complete reviewed inputs, scrubbed environment, dual opt-in, explicit labeling, unknown mode and relabel refusal.')
