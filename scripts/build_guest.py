#!/usr/bin/env python3
"""Compile our freestanding guest and extract relocation-free Mach-O __text.

No executable guest pages are ever mapped by the iPhone app. The resulting
file is read as bytes by the software interpreter.
"""
import pathlib
import struct
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="satchel-guest-") as tmp:
    obj = pathlib.Path(tmp) / "brick.o"
    subprocess.run(["xcrun", "clang", "-target", "arm64-apple-macos11", "-O1",
                    "-ffreestanding", "-fno-builtin", "-fno-stack-protector",
                    "-fno-vectorize", "-fno-slp-vectorize", "-fno-jump-tables",
                    "-fno-unwind-tables", "-fno-asynchronous-unwind-tables",
                    "-c", str(ROOT / "Guest/brick.c"), "-o", str(obj)], check=True)
    data = obj.read_bytes()
    assert struct.unpack_from("<I", data)[0] == 0xFEEDFACF
    ncmds = struct.unpack_from("<I", data, 16)[0]
    cursor = 32
    text = None
    for _ in range(ncmds):
        cmd, size = struct.unpack_from("<II", data, cursor)
        if cmd == 0x19:
            count = struct.unpack_from("<I", data, cursor + 64)[0]
            for index in range(count):
                at = cursor + 72 + index * 80
                name = data[at:at + 16].rstrip(b"\0")
                addr, length, offset, align, reloff, nreloc = struct.unpack_from("<QQIIII", data, at + 32)
                if name == b"__text":
                    assert nreloc == 0, "Guest __text has unresolved relocations"
                    text = data[offset:offset + length]
        cursor += size
    assert text and len(text) % 4 == 0
    destination = ROOT / "App/Resources/brick.arm64"
    destination.write_bytes(text)
    listing = subprocess.check_output(["xcrun", "llvm-objdump", "--disassemble", str(obj)], text=True)
    (ROOT / "Guest/brick.disassembly.txt").write_text(listing.replace(str(obj), "brick.o"))
    print(f"Guest: {len(text)} bytes / {len(text) // 4} AArch64 instruction words")
