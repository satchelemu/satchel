#!/usr/bin/env python3
"""Check native output with the host parser and Apple's linker as independent oracles."""
from pathlib import Path
import hashlib, os, shlex, subprocess, sys
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'NativeHost'))
from package import ENGINE, MODULES, image_sections, compare_sections
work=Path(sys.argv[1]); native=work/'synthetic.dylib'
sections=image_sections(native); compare_sections(native,sections,work)
flags=shlex.split(subprocess.check_output(['python3',str(ENGINE/'dm/ios/tools/gen_embed.py'),str(work),str(work/'embed.S'),*MODULES],text=True))
(work/'marker.c').write_text('__attribute__((visibility("default"))) const char satchel_game_container_v1[] = "Satchel container 1";\n')
sdk=subprocess.check_output(['xcrun','--sdk','iphoneos','--show-sdk-path'],text=True).strip()
subprocess.run(['nice','-n','15','xcrun','clang','-target','arm64-apple-ios26.0','-isysroot',sdk,'-dynamiclib','-O1','-Wl,-install_name,@rpath/SatchelPreparedGame.dylib','-Wl,-no_fixup_chains',*flags,str(work/'marker.c'),str(work/'embed.S'),'-o',str(work/'game.dylib')],env=dict(os.environ,TMPDIR=str(work)),check=True)
assert native.read_bytes()==(work/'game.dylib').read_bytes(), 'Native synthetic dylib differs from clang/ld oracle'
print('PASS: normal/front/back/padding, protections and complete unsigned Mach-O including UUID match clang/ld')
# The independent host checker must still reject a changed payload byte.
changed=work/'changed.dylib';changed.write_bytes(native.read_bytes())
with changed.open('r+b') as f:
    f.seek(sections[('__TEXT','__dmmainx0')]['offset']);f.write(b'\xff')
try: compare_sections(changed,image_sections(changed),work)
except AssertionError: print('PASS: independent host checker rejects changed module byte')
else: raise AssertionError('Changed module byte accepted')
