from pathlib import Path
import os,subprocess,json,hashlib,argparse
H=Path(__file__).resolve().parent
parser=argparse.ArgumentParser();parser.add_argument('--build',type=Path,required=True);args=parser.parse_args();B=args.build.resolve()
m=json.loads((B/'build-manifest.json').read_text());R=Path(m['source_root']);S=B
h=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
for p,v in (m['inputs']|m['objects']).items():assert h(Path(p))==v
assert m['nvn_enabled']
incs=[H,R/'Tools/GuestGraphics',R/'Tools/CPUProbe',R/'Tools/VulkanBackend',R/'Tools/RasterCompiler',R/'Tools/ScalarCompiler',Path(m['libraries'][2]).parents[3]/'include']
flags=['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer']+[x for p in incs for x in ['-I',str(p)]]
objects=[B/(n+'.o') for n in 'GuestPools GuestPrograms GuestTextureLayout GuestTextures GuestRasterRun GuestRasterRecord GuestRasterState GuestCompute GuestTransfers GuestDispatch GuestVulkanRaster GuestVulkanCompute GuestVulkanTransfers GuestVulkanPools GuestRasterPrograms RasterCompiler VulkanBackend HostNVN'.split()]
libs=[Path(p) for p in m['libraries']];frameworks=[x for n in ['Foundation','Metal','QuartzCore','CoreGraphics','IOSurface','IOKit','AppKit'] for x in ['-framework',n]]
run=lambda a:subprocess.run([str(x) for x in a],check=True)
for name,srcs in [('glue',[H/'HostNVNTest.c']),('raster-glue',[H/'HostNVNRasterFixture.c',R/'Tools/GuestGraphics/RasterDiagnosticTest.c'])]:
 own=[]
 for src in srcs:
  obj=B/(src.stem+'-test.o');run(['xcrun','clang','-std=c11',*flags,'-c',src,'-o',obj]);own.append(obj)
 exe=B/name;run(['xcrun','clang++',*flags,*objects,*own,*libs,*frameworks,'-o',exe])
 with (S/(name+'-gate.log')).open('w') as log:
  subprocess.run([str(exe)],check=True,stdout=log,stderr=log,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
 print((S/(name+'-gate.log')).read_text())
