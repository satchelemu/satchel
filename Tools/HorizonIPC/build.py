#!/usr/bin/env python3
"""Bounded IPC checks, pinned public codec comparison and iOS archive."""
import hashlib
import argparse
import os
from pathlib import Path
import subprocess
import urllib.request

HERE = Path(__file__).resolve().parent
BUILD = Path("/tmp/satchel-horizon-ipc")
PIN = "b156b4774fb2df7b93402169509266da7547fc4c"
HEADERS = {"result.h": "54078840d113829fb8b7c6b1b2888c3378f7dd162d0ffeed30cc41fa32239fde",
           "sf/hipc.h": "38bd18f76b120e75a07d6639761d23f21f289202982ca14dd0246be7974394a0",
           "sf/cmif.h": "8d9d032c06ce4bbbb0052ab0952f95828bb5b032ba381c485e2a777a61a06b74",
           "sf/service.h": "f774751e638d9025f89a796655266a24908801a2f14be46421fe816c757bbe94",
           "sf/tipc.h": "da99514e6f3e508bc7c3f0ca0485db26309a04a116350f685b2ac0429427a380"}

def run(args, **kwargs):
    return subprocess.run([str(a) for a in args], check=True, **kwargs)

def output(args):
    return subprocess.check_output(args, text=True).strip()

def main():
    global BUILD
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=BUILD,
                        help="isolated output directory; defaults to canonical scratch archives")
    BUILD = parser.parse_args().build_dir.resolve()
    reference = BUILD / "reference"
    reference.mkdir(parents=True, exist_ok=True)
    sources = []
    for name, expected in HEADERS.items():
        path = reference / name
        if not path.exists():
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(urllib.request.urlopen(f"https://raw.githubusercontent.com/switchbrew/libnx/{PIN}/nx/include/switch/{name}", timeout=30).read())
        if hashlib.sha256(path.read_bytes()).hexdigest() != expected:
            raise SystemExit("Pinned libnx reference header checksum mismatch")
        text = path.read_text()
        if name.endswith("cmif.h"):
            text = text[:text.index("NX_CONSTEXPR void cmifRequestInBuffer")] + text[
                text.index("NX_INLINE Result cmifParseResponse"):text.index("NX_INLINE Result cmifConvertCurrentObjectToDomain")]
        elif name.endswith("service.h"):
            text = text[text.index("enum {"):text.index("typedef struct SfDispatchParams")]
        elif name.endswith("tipc.h"):
            # Keep unchanged public builders/decoder; omit functions that send
            # kernel requests. armGetTls below targets an original test buffer.
            text = text[:text.index("NX_INLINE void tipcClose")] + text[
                text.index("NX_CONSTEXPR void tipcRequestInBuffer"):text.index("NX_INLINE Result tipcDispatchImpl")]
        sources.append("\n".join(line for line in text.splitlines() if not line.startswith(("#include", "#pragma once"))))
    license_path = reference / "libnx-LICENSE"
    if not license_path.exists():
        license_path.write_bytes(urllib.request.urlopen(f"https://raw.githubusercontent.com/switchbrew/libnx/{PIN}/LICENSE.md", timeout=30).read())
    if hashlib.sha256(license_path.read_bytes()).hexdigest() != "5b6675f85016009624d29c9222201d3c0fc6a2d5c59253367f27d3e15011cdb2":
        raise SystemExit("Pinned libnx reference license checksum mismatch")
    preamble = """#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef u32 Handle; typedef u32 Result;
#define NX_INLINE static inline
#define NX_CONSTEXPR static inline
#define BIT(n) (1U << (n))
static _Alignas(16) u8 reference_tls[256];
static inline void* armGetTls(void) { return reference_tls; }
"""
    # Codec function bodies are unchanged. Test TLS is a local fixed buffer;
    # no reference function compiled here sends a host/Switch kernel request.
    (reference / "libnx_reference.h").write_text("/* " + license_path.read_text() + " */\n" + preamble + "\n".join(sources))
    common = ["xcrun", "clang", "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-I", HERE]
    sanitized = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    host = BUILD / "macos"
    host.mkdir(exist_ok=True)
    run([*common, *sanitized, "-c", HERE / "HorizonIPC.c", "-o", host / "HorizonIPC.o"])
    run(["xcrun", "libtool", "-static", "-o", host / "libSatchelHorizonIPC.a", host / "HorizonIPC.o"])
    run([*common, *sanitized, "-I", reference, HERE / "test.c", host / "libSatchelHorizonIPC.a", "-o", host / "ipc-tests"])
    run([host / "ipc-tests"], env=dict(os.environ, ASAN_OPTIONS="halt_on_error=1", UBSAN_OPTIONS="halt_on_error=1"))
    ios = BUILD / "ios"
    ios.mkdir(exist_ok=True)
    sdk = output(["xcrun", "--sdk", "iphoneos", "--show-sdk-path"])
    run([*common, "-target", "arm64-apple-ios17.0", "-isysroot", sdk, "-c", HERE / "HorizonIPC.c", "-o", ios / "HorizonIPC.o"])
    run(["xcrun", "libtool", "-static", "-o", ios / "libSatchelHorizonIPC.a", ios / "HorizonIPC.o"])
    print("iOS arm64 bounded IPC archive:", ios / "libSatchelHorizonIPC.a")

if __name__ == "__main__":
    main()
