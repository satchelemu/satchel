#!/usr/bin/env python3
"""Build the pinned, patched CPU probe outside the repository. No downloads."""
import argparse
import concurrent.futures
import hashlib
import os
import pathlib
import struct
import subprocess

PIN = "7b792dcee6250db405f7f692d3853786b4d2a92d"
HERE = pathlib.Path(__file__).resolve().parent


def run(args, **kwargs):
    return subprocess.run([str(a) for a in args], check=True, **kwargs)


def output(args):
    return subprocess.check_output([str(a) for a in args], text=True).strip()


def extract_text(source, destination):
    data = source.read_bytes()
    assert data[:6] == b"\x7fELF\x02\x01"
    offset = struct.unpack_from("<Q", data, 40)[0]
    size, count, strings = struct.unpack_from("<HHH", data, 58)
    sections = [struct.unpack_from("<IIQQQQIIQQ", data, offset + i * size) for i in range(count)]
    names = sections[strings]
    names = data[names[4]:names[4] + names[5]]
    found = False
    for section in sections:
        assert section[1] not in (4, 9) or section[5] == 0, "Fixture must have no relocations"
        if names[section[0]:].split(b"\0", 1)[0] == b".text":
            destination.write_bytes(data[section[4]:section[4] + section[5]])
            found = True
    assert found


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=pathlib.Path, default=pathlib.Path("/tmp/satchel-vixl-probe/vixl"))
    parser.add_argument("--output", type=pathlib.Path, default=pathlib.Path("/tmp/satchel-vixl-probe/build"))
    parser.add_argument("--ios", action="store_true", help="Also compile an arm64 iOS static library; no installation")
    parser.add_argument("--prefetch-only", action="store_true", help="Development gate for only original prefetch tests; not full validation")
    parser.add_argument("--element-narrow-only", action="store_true", help="Focused original SIMD fixture gate only; not full validation")
    parser.add_argument("--ucvtf-only", action="store_true", help="Focused original conversion tests only; not full validation")
    parser.add_argument("--fmul-indexed-only", action="store_true", help="Focused original FMUL tests; not full validation")
    parser.add_argument("--fcvtl-only", action="store_true", help="Focused original widening tests only; not full validation")
    parser.add_argument("--fcvtzu-only", action="store_true", help="Focused original2D unsigned conversion tests only; not full validation")
    parser.add_argument("--isb-only", action="store_true", help="Focused original instruction-barrier tests; not full validation")
    parser.add_argument("--ucvtf64-only", action="store_true", help="Focused original W/X S/D conversion tests, not full validation")
    parser.add_argument("--fdiv-only", action="store_true", help="Focused original FDIV tests only; not full validation")
    args = parser.parse_args()
    source, build = args.source.resolve(), args.output.resolve()
    repo = HERE.parent.parent
    if build == repo or repo in build.parents or build == source or source in build.parents:
        parser.error("Build output must be outside the repository and source checkout")
    if output(["git", "-C", source, "rev-parse", "HEAD"]) != PIN:
        parser.error("VIXL checkout does not match the pinned revision")
    patch = HERE / "vixl-memory-guard.patch"
    actual = subprocess.check_output(["git", "-C", str(source), "diff", "HEAD", "--"])
    if not actual:
        run(["git", "-C", source, "apply", "--check", patch])
        run(["git", "-C", source, "apply", patch])
        actual = subprocess.check_output(["git", "-C", str(source), "diff", "HEAD", "--"])
    if actual != patch.read_bytes():
        parser.error("VIXL checkout changes do not exactly match the retained memory guard patch")
    build.mkdir(parents=True, exist_ok=True)
    compiler = output(["xcrun", "--find", "clang++"])
    sources = sorted((source / "src").glob("*.cc")) + sorted((source / "src/aarch64").glob("*.cc")) + [HERE / "CPUProbe.cpp", HERE / "Binary64.cpp"]
    common = ["-std=c++17", "-O1", "-g", "-DVIXL_DEBUG", "-DVIXL_NEGATIVE_TESTING",
              "-DSATCHEL_VIXL_MEMORY_GUARD", "-DVIXL_INCLUDE_SIMULATOR_AARCH64", "-DVIXL_CODE_BUFFER_MALLOC",
              "-DVIXL_INCLUDE_TARGET_AARCH64", "-I", str(source / "src"),
              "-Wno-deprecated-literal-operator", "-Wno-format"]

    def build_library(name, flags):
        directory = build / name
        directory.mkdir(exist_ok=True)
        objects = [directory / (s.stem + ".o") for s in sources]
        # Headers and patch participate in the cache, so edits cannot reuse stale objects.
        headers = b"".join(p.read_bytes() for p in sorted((source / "src").rglob("*.h"))) + b"".join(p.read_bytes() for p in sorted(HERE.glob("*.h")))
        fingerprint = hashlib.sha256(headers + repr(common + flags).encode() + output([compiler, "--version"]).encode()).digest()

        def compile_one(pair):
            file, obj = pair
            digest = hashlib.sha256(fingerprint + file.read_bytes()).hexdigest()
            stamp = obj.with_suffix(".sha256")
            if not obj.exists() or not stamp.exists() or stamp.read_text() != digest:
                run([compiler, *common, *flags, "-c", file, "-o", obj])
                stamp.write_text(digest)
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as workers:
            list(workers.map(compile_one, zip(sources, objects)))
        library = directory / "libSatchelCPUProbe.a"
        run(["xcrun", "libtool", "-static", "-o", library, *objects])
        return library

    sanitized = ["-isysroot", output(["xcrun", "--sdk", "macosx", "--show-sdk-path"]),
                 "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    library = build_library("macos", [*sanitized, "-DSATCHEL_CPU_TESTING"])
    fixture_objects = []
    # Native FP status oracle is TEST ONLY: its fixed instructions temporarily
    # change the host floating environment. Never include it in either library.
    fp_oracle = build / "native-fp-oracle.o"
    run(["xcrun", "clang", "-c", HERE / "native-fp-oracle.S", "-o", fp_oracle])
    fixture_objects.append(fp_oracle)
    for name in ("arithmetic", "vector", "floating", "fcvtzu2d", "fcvtl", "fmulindexed", "ucvtf", "ucvtf64", "fdiv", "barrier", "dsb", "isb", "clrex", "exclusive", "acquire", "ordered", "address", "pairs", "wpairs", "qpairs", "dsingle", "dunsigned", "simdsinglewidths", "simdregister", "simdaddsub", "simdcompare", "elementnarrow", "prefetch", "simdcount", "simdwidenreduce", "fmovgpr", "simdlogical", "simdreduce", "simdpermute", "movinteger", "dupgeneral", "bytes", "unscaled", "halfunscaled", "multiply", "highmultiply", "tls", "clock", "breakpoints", "registerloads", "registermemory", "testbranch", "bitcount", "variableshift", "conditional", "division", "immediates", "conditionalcompare", "extended"):
        elf = build / (name + ".elf")
        run(["xcrun", "clang", "-target", "aarch64-none-elf", "-DGUEST_PROBE", "-D" + name.upper(), "-c", HERE / "fixture.S", "-o", elf])
        extract_text(elf, build / (name + ".bin"))
        if name not in ("address", "clock"):
            obj = build / (name + ".o")
            run(["xcrun", "clang", "-D" + name.upper(), "-c", HERE / "fixture.S", "-o", obj])
            fixture_objects.append(obj)
    executable = build / "cpu-probe-tests"
    run([compiler, "-std=c++17", "-O1", "-g", *sanitized, HERE / "test.cpp", library, *fixture_objects, "-o", executable])
    test_env = dict(os.environ, UBSAN_OPTIONS="halt_on_error=1", ASAN_OPTIONS="halt_on_error=1")
    run([executable, build, *(["--fdiv-only"] if args.fdiv_only else ["--ucvtf64-only"] if args.ucvtf64_only else ["--isb-only"] if args.isb_only else ["--fcvtzu-only"] if args.fcvtzu_only else ["--fcvtl-only"] if args.fcvtl_only else ["--fmul-indexed-only"] if args.fmul_indexed_only else ["--ucvtf-only"] if args.ucvtf_only else ["--element-narrow-only"] if args.element_narrow_only else ["--prefetch-only"] if args.prefetch_only else [])], env=test_env)
    if args.ios:
        sdk = output(["xcrun", "--sdk", "iphoneos", "--show-sdk-path"])
        ios = build_library("ios", ["-target", "arm64-apple-ios17.0", "-isysroot", sdk])
        print("iOS arm64 static library (compile only):", ios)


if __name__ == "__main__":
    main()
