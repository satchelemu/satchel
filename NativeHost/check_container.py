#!/usr/bin/env python3
"""Synthetic layout and rejection checks. No user game data is read."""
from pathlib import Path
import subprocess
from package import ENGINE, OUT, MODULES, image_sections, compare_sections

work = OUT / 'synthetic-container'
work.mkdir(parents=True, exist_ok=True)
layouts = [
    {'TEXT_END': '0x4000', 'PATCH_OFF': '0x8000', 'KEY_PAGE_OFF': '0x4000', 'KEY_PAGE_SIZE': '0x4000'},
    {'TEXT_END': '0x4000', 'PATCH_OFF': '-0x8000', 'KEY_PAGE_OFF': '0xc000', 'KEY_PAGE_SIZE': '0x4000', 'FILE_IMAGE_LEN': '0x8000', 'IMAGE_SIZE': '0x10000'},
    {'TEXT_END': '0x4000', 'PATCH_OFF': '-0x8000', 'KEY_PAGE_OFF': '0xc000', 'KEY_PAGE_SIZE': '0x4000', 'FILE_IMAGE_LEN': '0x8000', 'IMAGE_SIZE': '0x14000', 'BACK_OFF': '0x10000', 'BACK_LEN': '0x4000'},
]
for index, (name, layout) in enumerate(zip(MODULES, layouts)):
    (work / f'{name}.fixups.tsv').write_text(''.join(f'# {k} {v}\n' for k,v in layout.items()))
    size = 0x10000 if index == 2 else 0x8100
    (work / f'{name}.patched.bin').write_bytes(bytes([index + 1]) * size)
flags = subprocess.check_output(['python3', ENGINE / 'dm/ios/tools/gen_embed.py', work, work / 'embed.S', *MODULES], text=True).split()
sdk = subprocess.check_output(['xcrun', '--sdk', 'iphoneos', '--show-sdk-path'], text=True).strip()
base = ['xcrun', 'clang', '-target', 'arm64-apple-ios26.0', '-isysroot', sdk, '-dynamiclib']
subprocess.run([*base, *flags, work / 'embed.S', '-o', work / 'valid.dylib'], check=True)
sections = image_sections(work / 'valid.dylib')
compare_sections(work / 'valid.dylib', sections, work)
print('PASS: normal, front, back and zero-tail splits match every synthetic byte')
(work / 'constructor.c').write_text('__attribute__((constructor)) static void initializer(void) { __asm__ volatile("nop"); }\n')
subprocess.run([*base, work / 'constructor.c', '-o', work / 'constructor.dylib'], check=True)
try:
    image_sections(work / 'constructor.dylib')
except AssertionError:
    print('PASS: constructor rejected')
else:
    raise AssertionError('Constructor was accepted')
with (work / 'valid.dylib').open('r+b') as f:
    f.seek(sections[('__TEXT', '__dmmainx0')]['offset']); f.write(b'\xff')
try:
    compare_sections(work / 'valid.dylib', sections, work)
except AssertionError:
    print('PASS: altered module byte rejected')
else:
    raise AssertionError('Altered module byte was accepted')
