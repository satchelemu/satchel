#!/usr/bin/env python3
"""Build original freestanding homebrew with installed clang, no extra SDK.

Assembler emits a relocation-free NRO image in ELF .text. This is a real NRO0
format using published Switch calls, not a wrapper around the private demo ABI.
The standard NRO build ID is a SHA-256 of the image before filling that field.
"""
import hashlib
import pathlib
import struct
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="satchel-nro-") as tmp:
    obj = pathlib.Path(tmp) / "hello.o"
    subprocess.run(["xcrun", "clang", "-target", "aarch64-none-elf", "-c",
                    str(ROOT / "Guest/hello_horizon.S"), "-o", str(obj)], check=True)
    data = obj.read_bytes()
    assert data[:6] == b"\x7fELF\x02\x01"
    shoff = struct.unpack_from("<Q", data, 40)[0]
    entsize, count, string_index = struct.unpack_from("<HHH", data, 58)
    sections = [struct.unpack_from("<IIQQQQIIQQ", data, shoff + i * entsize) for i in range(count)]
    strings = sections[string_index]
    names = data[strings[4]:strings[4] + strings[5]]
    nro = None
    for section in sections:
        assert section[1] not in (4, 9) or section[5] == 0, "Unresolved relocations in NRO"
        if names[section[0]:].split(b"\0", 1)[0] == b".text":
            nro = bytearray(data[section[4]:section[4] + section[5]])
    assert nro and len(nro) == 0x3000 and nro[0x10:0x14] == b"NRO0"
    nro[0x40:0x60] = hashlib.sha256(nro).digest()
    destination = ROOT / "App/Resources/hello-horizon.nro"
    destination.write_bytes(nro)
    print(f"Built {destination.name}: {len(nro)} bytes, SHA-256 {hashlib.sha256(nro).hexdigest()}")
