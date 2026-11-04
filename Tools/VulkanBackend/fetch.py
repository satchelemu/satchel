#!/usr/bin/env python3
"""Fetch pinned public runtime/source into task scratch; no package-manager install."""
from pathlib import Path
import hashlib,subprocess,tarfile,urllib.request
WORK=Path('/tmp/satchel-vulkan-backend')
PIN='db66022459ffb663aa2b50f6b018bc2e124f5edf'
HASHES={'ios':'b5d947b1660e6e9fed40b9cd2387e160aaab9e80b775c0cef7e14059405178c1','macos':'f95765a6229cb7b915990a2890ce12ebe36a730b021545d3d52ae69ce4c4024e'}
WORK.mkdir(parents=True,exist_ok=True)
source=WORK/'MoltenVK-source'
if not source.exists():
 subprocess.run(['git','-c','advice.detachedHead=false','clone','--quiet','--depth','1','--branch','v1.4.2','https://github.com/KhronosGroup/MoltenVK.git',str(source)],check=True)
if subprocess.check_output(['git','-C',str(source),'rev-parse','HEAD'],text=True).strip()!=PIN:
 raise SystemExit('Unexpected MoltenVK source revision; existing files preserved.')
for platform,digest in HASHES.items():
 target=WORK/f'MoltenVK-{platform}.tar'
 if not target.exists():
  urllib.request.urlretrieve('https://github.com/KhronosGroup/MoltenVK/releases/download/v1.4.2/'+target.name,target)
 if hashlib.file_digest(target.open('rb'),'sha256').hexdigest()!=digest:
  raise SystemExit('Runtime checksum mismatch; no extraction performed.')
 destination=WORK/platform;destination.mkdir(exist_ok=True)
 with tarfile.open(target) as archive:archive.extractall(destination,filter='data')
 print(platform,'official runtime checksum verified')
