#!/usr/bin/env python3
"""Build the pinned native shader translator or isolated fixture outside the repo."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import plistlib
import platform
import shutil
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'NativeShaders'))
from build_receipt import inputs, record

HERE = Path(__file__).resolve().parent
LOCK = json.loads((HERE / "source-lock.json").read_text())


def source_files(checkout):
    ryu = checkout / "dm/third_party/ryujinx"
    files = []
    for project in ("Ryujinx.Graphics.Shader", "Spv.Generator"):
        source = ryu / "src" / project
        files.extend(p for p in source.rglob("*.cs")
                     if not any(s in ("obj", "bin") or s.startswith("._")
                                for s in p.relative_to(source).parts))
    files.extend((ryu / "src/Ryujinx.Graphics.Shader/CodeGen/Glsl/HelperFunctions").glob("*.glsl"))
    files.extend(ryu / "src/Ryujinx.Common" / name for name in LOCK["common_sources"])
    files.extend((ryu / "LICENSE.txt", ryu / "src/Spv.Generator/LICENSE"))
    return sorted(files, key=lambda p: p.relative_to(checkout).as_posix())


def verify_source(checkout, fixture):
    # The game translator follows the engine pin (game-source-lock.json); the original fixture
    # stays on the reviewed NX2Apple checkout of source-lock.json. Both read the same file list.
    lock = LOCK if fixture else json.loads((HERE / "game-source-lock.json").read_text())
    name = "source-lock.json" if fixture else "game-source-lock.json"
    commit = subprocess.check_output(["git", "-C", str(checkout), "rev-parse", "HEAD"], text=True).strip()
    if commit != lock["commit"]:
        raise RuntimeError("Translator checkout commit does not match " + name + ".")
    if not fixture:
        front = checkout / "dm/gfx/tools/dmsh_xlate/Program.cs"
        if hashlib.sha256(front.read_bytes()).hexdigest() != lock["Program.cs"]:
            raise RuntimeError("Game accessor source differs from its pinned hash.")
    files = source_files(checkout)
    digest = hashlib.sha256()
    for source in files:
        digest.update(source.relative_to(checkout).as_posix().encode() + b"\0")
        digest.update(source.read_bytes())
        digest.update(b"\0")
    if len(files) != lock["source_file_count"] or digest.hexdigest() != lock["source_sha256"]:
        raise RuntimeError("Translator sources or license texts differ from the pinned source hash.")
    print(f"Verified {len(files)} pinned source/resource/license files.", flush=True)


def run(args, env=None, log=None):
    if log:
        with log.open("w") as output:
            result = subprocess.run([str(a) for a in args], env=env, stdout=output, stderr=subprocess.STDOUT)
        if result.returncode:
            print(log.read_text()[-12000:], file=sys.stderr)
            raise RuntimeError(f"Build command failed; complete log: {log}")
    else:
        subprocess.run([str(a) for a in args], env=env, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--checkout", type=Path, required=True, help="Existing pinned checkout (the engine for the game translator, NX2Apple for the fixture); read only")
    parser.add_argument("--dotnet", type=Path, required=True, help="Existing .NET 10.0.403 executable")
    parser.add_argument("--work", type=Path, default=Path("/tmp/satchel-shader-aot"))
    parser.add_argument("--platform", choices=("host", "ios", "all"), default="all")
    parser.add_argument("--original-fixture", action="store_true", help="Build isolated fixed compute fixture facade; never admits game shaders")
    args = parser.parse_args()
    if platform.system() != "Darwin" or platform.machine() != "arm64":
        raise RuntimeError("This tested build route requires an Apple Silicon Mac with Xcode.")
    checkout, dotnet, work = args.checkout.resolve(), args.dotnet.resolve(), args.work.resolve()
    # A build directory must never overwrite project sources or the reference tree.
    project = HERE.parent.parent
    if work == project or project in work.parents or work == checkout or checkout in work.parents:
        raise RuntimeError("Choose a scratch work directory outside both repositories.")
    if args.original_fixture and work == Path("/tmp/satchel-shader-aot").resolve():
        raise RuntimeError("The original fixture must use a separate staging directory, not the canonical framework.")
    validator = shutil.which("spirv-val") if args.original_fixture else None
    if args.original_fixture and args.platform in ("host", "all") and not validator:
        raise RuntimeError("The original fixture host gate requires the already-installed spirv-val tool.")
    before = inputs('aot')
    verify_source(checkout, args.original_fixture)
    work.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env.update(DOTNET_CLI_HOME=str(work / "cli"), NUGET_PACKAGES=os.environ.get("NUGET_PACKAGES", str(work / "packages")),
               DOTNET_GENERATE_ASPNET_CERTIFICATE="false", DOTNET_CLI_TELEMETRY_OPTOUT="1",
               DOTNET_ADD_GLOBAL_TOOLS_TO_PATH="false", DOTNET_NOLOGO="1")
    version = subprocess.check_output([str(dotnet), "--version"], env=env, text=True).strip()
    if version != LOCK["dotnet_sdk"]:
        raise RuntimeError("Installed .NET SDK does not match source-lock.json.")
    build = work / "repro"
    build.mkdir(exist_ok=True)

    def publish(label, rid, native):
        output = build / (label + "-publish")
        fixture_properties = []
        if args.original_fixture:
            fixture_properties = ["-p:SatchelOriginalFixture=true"]
            if label == "host": fixture_properties.append("-p:SatchelFixtureTesting=true")
        run([dotnet, "publish", HERE / "MaxwellShaderCompiler.csproj", "--artifacts-path", build / (label + "-artifacts"),
             "-m:2", "-p:IlcMaxDegreeOfParallelism=2", "-p:UseSharedCompilation=false", "-r", rid, "-c", "Release", "-o", output, "-p:NativeLib=" + native,
             "-p:NX2AppleRoot=" + str(checkout), "-p:RestoreConfigFile=" + os.environ.get("SATCHEL_NUGET_CONFIG", str(HERE / "NuGet.Config")),
             "-p:NuGetAudit=false", "-p:TreatWarningsAsErrors=true", "-p:IlcTreatWarningsAsErrors=true"] + fixture_properties,
            env, build / (label + ".log"))
        print(f"NativeAOT {rid} {native} built.", flush=True)
        return output

    if args.platform in ("host", "all"):
        if args.original_fixture:
            # No testing define or test exports: production compile is the first
            # managed call in a fresh process, independently from the warm suite.
            cold = publish("host-production", "osx-arm64", "Shared")
            symbols = subprocess.check_output(["xcrun", "nm", "-gU", str(cold / "SatchelShaderProbe.dylib")], text=True)
            if "_satchel_shader_fixture_compile" not in symbols or "_satchel_shader_fixture_test_" in symbols:
                raise RuntimeError("Production host fixture exports are incorrect.")
            run(["xcrun", "clang", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                 "-fno-omit-frame-pointer", HERE / "fixture_cold.c", cold / "SatchelShaderProbe.dylib",
                 "-Wl,-rpath," + str(cold), "-o", build / "host-cold-fixture"])
            run([build / "host-cold-fixture", build / "cold-compute.spv"],
                dict(os.environ, ASAN_OPTIONS="halt_on_error=1", UBSAN_OPTIONS="halt_on_error=1"))
            run([validator, "--target-env", "vulkan1.1", build / "cold-compute.spv"])
        host = publish("host", "osx-arm64", "Shared")
        run(["xcrun", "clang", "-Wall", "-Wextra", "-Werror", HERE / "fixture.c",
             host / "SatchelShaderProbe.dylib", "-Wl,-rpath," + str(host), "-o", build / "host-probe"])
        run([build / "host-probe", build / "exit.spv"])
        if args.original_fixture:
            run(["xcrun", "clang", "-Wall", "-Wextra", "-Werror", "-DSATCHEL_SHADER_FIXTURE_TESTING",
                 "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                 HERE / "fixture_compute.c", host / "SatchelShaderProbe.dylib", "-Wl,-rpath," + str(host),
                 "-o", build / "host-compute-fixture"])
            test_env = dict(os.environ, ASAN_OPTIONS="halt_on_error=1", UBSAN_OPTIONS="halt_on_error=1")
            run([build / "host-compute-fixture", build / "original-compute.spv"], test_env)
            run([validator, "--target-env", "vulkan1.1", build / "original-compute.spv"])
            print("Original fixture passed independent Vulkan 1.1 SPIR-V validation; no GPU execution claimed.", flush=True)

    if args.platform in ("ios", "all"):
        static = publish("ios-static", "ios-arm64", "Static")
        shared = publish("ios-shared", "ios-arm64", "Shared")
        runtime = Path(env["NUGET_PACKAGES"]) / "microsoft.netcore.app.runtime.nativeaot.ios-arm64" / LOCK["runtime_version"] / "runtimes/ios-arm64/native"
        # NativeAOT's Static result contains only the translated object. These
        # dependencies are verified against the pinned 10.0.14 Unix targets and
        # the successful Shared link command; bootstrap must be a direct object.
        libraries = ["libSystem.Native.a", "libSystem.Globalization.Native.a", "libSystem.IO.Compression.Native.a",
                     "libSystem.Net.Security.Native.a", "libSystem.Security.Cryptography.Native.Apple.a",
                     "libbootstrapperdll.o", "libRuntime.WorkstationGC.a", "libeventpipe-disabled.a",
                     "libstandalonegc-disabled.a", "libaotminipal.a", "libbrotlienc.a", "libbrotlidec.a", "libbrotlicommon.a"]
        sdk = subprocess.check_output(["xcrun", "--sdk", "iphoneos", "--show-sdk-path"], text=True).strip()
        link = ["xcrun", "clang", HERE / "fixture.c", static / "SatchelShaderProbe.a", "-o", build / "ios-static-link-probe",
                "-Wl,-dead_strip", "-isysroot", sdk, "--target=arm64-apple-ios12.2-macho"]
        link.extend(runtime / name for name in libraries)
        link.extend(["-lstdc++", "-ldl", "-lobjc", "-lswiftCore", "-lswiftFoundation", "-lz", "-licucore", "-lm", "-L/usr/lib/swift"])
        for framework in ("CoreFoundation", "CryptoKit", "Foundation", "Network", "Security", "GSS"):
            link.extend(["-framework", framework])
        run(link, log=build / "ios-static-link.log")
        (build / "ios-static-link-args.json").write_text(json.dumps([str(a) for a in link], indent=2) + "\n")
        framework = work / "SatchelShaderProbe.framework"
        framework.mkdir(exist_ok=True)
        for directory in ("Headers", "Modules", "Notices"):
            (framework / directory).mkdir(exist_ok=True)
        shutil.copy2(shared / "SatchelShaderProbe.dylib", framework / "SatchelShaderProbe")
        run(["install_name_tool", "-id", "@rpath/SatchelShaderProbe.framework/SatchelShaderProbe", framework / "SatchelShaderProbe"])
        shutil.copy2(HERE / "SatchelShaderProbe.h", framework / "Headers/SatchelShaderProbe.h")
        if not args.original_fixture:
            shutil.copy2(HERE / "SatchelGameShader.h", framework / "Headers/SatchelGameShader.h")
        if args.original_fixture:
            shutil.copy2(HERE / "SatchelShaderFixture.h", framework / "Headers/SatchelShaderFixture.h")
        fixture_header = ' header "SatchelShaderFixture.h"\n' if args.original_fixture else ''
        game_header = '' if args.original_fixture else ' header "SatchelGameShader.h"\n'
        (framework / "Modules/module.modulemap").write_text('framework module SatchelShaderProbe {\n umbrella header "SatchelShaderProbe.h"\n' + fixture_header + game_header + ' export *\n}\n')
        with (framework / "Info.plist").open("wb") as output:
            plistlib.dump(dict(CFBundleIdentifier="lab.satchel.shader.probe", CFBundleName="SatchelShaderProbe",
                              CFBundleExecutable="SatchelShaderProbe", CFBundlePackageType="FMWK", CFBundleVersion="1",
                              CFBundleShortVersionString="0.1", MinimumOSVersion="12.2", CFBundleSupportedPlatforms=["iPhoneOS"]), output)
        for notice in (HERE / "licenses").iterdir():
            shutil.copy2(notice, framework / "Notices" / notice.name)
        shutil.copy2(HERE / "source-lock.json", framework / "Notices/source-lock.json")
        symbols = subprocess.check_output(["xcrun", "nm", "-gU", str(framework / "SatchelShaderProbe")], text=True)
        if not args.original_fixture:
            shutil.copy2(HERE / "game-source-lock.json", framework / "Notices/game-source-lock.json")
            if any(name not in symbols for name in ("_satchel_shader_translate_v1", "_satchel_shader_translate_free", "_satchel_shader_runtime_features")):
                raise RuntimeError("Staged iOS game translation exports are missing.")
        if args.original_fixture:
            if "_satchel_shader_fixture_compile" not in symbols or "_satchel_shader_fixture_test_" in symbols or "_satchel_shader_translate_v1" in symbols:
                raise RuntimeError("Staged iOS fixture export check failed.")
            manifest = {"original_fixture_only": True, "game_shader_inputs": False,
                        "source_hashes": {name: hashlib.sha256((HERE / name).read_bytes()).hexdigest()
                                          for name in ("FixtureCompiler.cs", "FixtureSpirv.cs", "SatchelShaderFixture.h", "fixture_compute.c", "fixture_cold.c")}}
            (framework / "Notices/original-fixture.json").write_text(json.dumps(manifest, indent=2) + "\n")
        print(f"iOS static link passed. Unsigned framework: {framework}", flush=True)
    if not args.original_fixture:
        products = []
        if args.platform in ('host', 'all'):
            products.append('repro/host-publish/SatchelShaderProbe.dylib')
        if args.platform in ('ios', 'all'):
            products.append('SatchelShaderProbe.framework/SatchelShaderProbe')
        record('aot', work, products, before)
    print("Build validation uses synthetic shaders; no device execution or graphics claim.")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
