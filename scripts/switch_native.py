#!/usr/bin/env python3
"""Prepare a game-specific iPhone development project using pinned NX2Apple.

This is a Mac build companion, not a generic XCI/NSP emulator or an App Store
import path. CNMT checks establish the base/update relationship; generated code,
derived profiles and gameplay still require review before signing and installation.
"""
import argparse
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
SOURCE_PIN = "8227f80f9b381c680611303319656c4e0c806a50"
DEFAULT_TOOLS = Path("/tmp/satchel-switch-integration")
GIB = 1024 ** 3


class PreflightError(Exception):
    def __init__(self, code, message):
        super().__init__(message)
        self.code = code


def fail(code, message):
    raise PreflightError(code, message)


def capture(command, env, cwd=None, timeout=90):
    return subprocess.run([str(x) for x in command], env=env, cwd=cwd,
                          capture_output=True, text=True, errors="replace", timeout=timeout)


def tool_environment(tools_root):
    # Prevent inherited diagnostic overrides from weakening the pinned checks or
    # silently selecting a different game profile. Never print the environment.
    env = {k: v for k, v in os.environ.items()
           if not k.startswith("DM_") and k not in ("SPIRV_CROSS", "PYTHONPATH")}
    bins = [tools_root / "bin", tools_root / "dotnet", tools_root / "venv/bin"]
    env["PATH"] = os.pathsep.join([*(str(p) for p in bins), env.get("PATH", "/usr/bin:/bin")])
    env["DOTNET_ROOT"] = str(tools_root / "dotnet")
    env["NUGET_PACKAGES"] = str(tools_root / "nuget")
    env["DOTNET_CLI_TELEMETRY_OPTOUT"] = "1"
    env["DOTNET_NOLOGO"] = "1"
    env["PYTHONDONTWRITEBYTECODE"] = "1"
    config = tools_root / "native-toolchain.json"
    if not env.get("SATCHEL_METAL_TOOLCHAIN") and config.is_file():
        try:
            value = json.loads(config.read_text())["metal_toolchain"]
            if not isinstance(value, str) or not value:
                raise ValueError()
            env["SATCHEL_METAL_TOOLCHAIN"] = value
        except (ValueError, KeyError, OSError):
            fail("toolchain_config", "native-toolchain.json must contain a metal_toolchain path.")
    return env


def existing_parent(path):
    while not path.exists() and path != path.parent:
        path = path.parent
    return path


def source_check(source, env):
    if not (source / "scripts/build_ipa_from_nsp.py").is_file():
        fail("source_missing", "Pinned NX2Apple checkout is missing.")
    result = capture(["git", "-C", source, "rev-parse", "HEAD"], env)
    if result.returncode or result.stdout.strip() != SOURCE_PIN:
        fail("source_pin", "NX2Apple must be checked out at " + SOURCE_PIN + ".")
    protected = ["scripts/build_ipa_from_nsp.py", "dm/games", "dm/tools/patch_nso.py",
                 "dm/gfx/tools/tool_lock.py", "dm/gfx/tools/dmsh_chain.lock"]
    result = capture(["git", "-C", source, "diff", "--quiet", "HEAD", "--", *protected], env)
    if result.returncode:
        fail("source_modified", "Pinned pipeline, profiles or tool checks have local modifications.")


def doctor(tools_root, source, work_dir):
    env = tool_environment(tools_root)
    checks = []

    def record(name, ok, detail):
        checks.append({"name": name, "status": "ready" if ok else "blocked", "detail": detail})

    record("host", platform.system() == "Darwin" and platform.machine() == "arm64",
           platform.system() + " " + platform.machine() + "; requires an Apple silicon Mac")
    try:
        source_check(source, env)
        record("source", True, SOURCE_PIN)
    except (PreflightError, OSError, subprocess.SubprocessError) as exc:
        record("source", False, str(exc) if isinstance(exc, PreflightError) else "Could not verify source pin.")
    python = tools_root / "venv/bin/python"
    python_probe = (
        "import sys, platform, lz4.block, capstone, fontTools; "
        "assert sys.version_info >= (3,12); assert platform.machine() == 'arm64'; "
        "assert capstone.cs_version()[0] == 5; "
        "__import__('compression.zstd' if sys.version_info >= (3,14) else 'backports.zstd'); "
        "print('Python '+platform.python_version()+'; Capstone '+capstone.__version__)"
    )
    probes = [("python", [python, "-c", python_probe]),
              ("dotnet", [tools_root / "dotnet/dotnet", "--version"]),
              ("ios_sdk", ["xcrun", "--sdk", "iphoneos", "--show-sdk-version"])]
    for name, command in probes:
        try:
            result = capture(command, env)
            ok = result.returncode == 0
            if name == "dotnet":
                ok = ok and result.stdout.strip().startswith("10.")
            record(name, ok, result.stdout.strip()[:300] if ok else "Required tool or version is unavailable.")
        except (OSError, subprocess.SubprocessError):
            record(name, False, "Required tool could not run.")
    for name in ("xcodebuild", "xcodegen", "zstd", "glslangValidator", "make"):
        found = shutil.which(name, path=env["PATH"])
        record(name, bool(found), found or "Not installed or not on the build PATH.")
    inspector = tools_root / "content-inspector/SwitchContent.dll"
    record("content_inspector", inspector.is_file(), "Compiled SwitchContent inspector " + ("available." if inspector.is_file() else "missing."))
    try:
        result = capture([python, source / "dm/gfx/tools/tool_lock.py", "--check-tools"], env, cwd=source)
        record("shader_tool_lock", result.returncode == 0,
               "Pinned translator, SPIRV-Cross and Metal fingerprints match." if result.returncode == 0
               else (result.stdout + result.stderr).strip()[-3000:])
    except (OSError, subprocess.SubprocessError):
        record("shader_tool_lock", False, "Upstream tool fingerprint verification could not run.")
    free = shutil.disk_usage(existing_parent(work_dir)).free
    record("minimum_build_space", free >= 10 * GIB,
           f"{free / GIB:.1f} GiB available; at least 10 GiB plus twice the selected packages is required.")
    return {"schema_version": 1, "mode": "native_game_development",
            "status": "ready" if all(c["status"] == "ready" for c in checks) else "blocked",
            "source_commit": SOURCE_PIN, "checks": checks,
            "disk": {"available_bytes": free, "required_formula": "2 * (base bytes + update bytes) + 10 GiB",
                     "note": "Preparation checks game-specific space before reading package contents."}}


def selected_file(value, label):
    path = Path(value).expanduser().resolve()
    try:
        info = path.stat()
        if not path.is_file() or info.st_size == 0:
            raise OSError()
    except OSError:
        fail("input_unavailable", "The selected " + label + " is missing, empty or unavailable.")
    return path, info.st_size


def check_space(work_dir, total_bytes):
    required = 2 * total_bytes + 10 * GIB
    free = shutil.disk_usage(existing_parent(work_dir)).free
    if free < required:
        fail("disk_space", f"Build needs at least {required / GIB:.1f} GiB free; destination has {free / GIB:.1f} GiB. Choose a larger work volume.")
    return {"required_bytes": required, "available_bytes": free}


def normalized_application_id(value):
    if value is None:
        return None
    if not isinstance(value, str) or not re.fullmatch(r"[0-9a-fA-F]{16}", value) or int(value, 16) == 0:
        fail("application_id_invalid", "Application ID must be exactly 16 hexadecimal digits and must not be zero.")
    return value.lower()


def validate_selection(report, has_update, application_id=None):
    try:
        if report["schema_version"] != 1 or report["status"] != "validated":
            raise ValueError()
        base = report["base"]
        if base["format"] not in ("nsp", "xci") or base["metadata_integrity_checked"] is not True:
            raise ValueError()
        applications = [m for m in base["metadata"] if m["type"] == "Application"]
        if application_id is not None:
            applications = [m for m in applications if m["id"] == application_id]
        if len(applications) != 1:
            raise ValueError()
        application = applications[0]
        selection = report["selection"]
        app_id, base_version = selection["application_id"], selection["base_version"]
        if not isinstance(app_id, str) or not re.fullmatch(r"[0-9a-f]{16}", app_id) or int(app_id, 16) == 0:
            raise ValueError()
        if application_id is not None and app_id != application_id:
            raise ValueError()
        if type(base_version) is not int or not 0 <= base_version <= 0xffffffff:
            raise ValueError()
        if application["id"] != app_id or application["version"] != base_version:
            raise ValueError()
        if has_update:
            update = report["update"]
            if update["format"] != "nsp" or update["metadata_integrity_checked"] is not True:
                raise ValueError()
            patches = [m for m in update["metadata"] if m["type"] == "Patch"]
            if len(patches) != 1 or any(m["type"] in ("Application", "AddOnContent") for m in update["metadata"]):
                raise ValueError()
            patch = patches[0]
            if (patch["application_id"] != app_id or patch["id"] != application["patch_id"]
                    or patch["id"] != selection["patch_id"] or patch["version"] != selection["patch_version"]):
                raise ValueError()
            if not re.fullmatch(r"[0-9a-f]{16}", patch["id"]):
                raise ValueError()
            if type(patch["version"]) is not int or not base_version < patch["version"] <= 0xffffffff:
                raise ValueError()
        elif report["update"] is not None or selection["patch_id"] is not None or selection["patch_version"] is not None:
            raise ValueError()
        return selection
    except (KeyError, TypeError, ValueError):
        fail("metadata_mismatch", "Inspector result is invalid or does not establish the selected base/update relationship.")


def inspect_content(base, update, keys, tools_root, env, application_id=None):
    application_id = normalized_application_id(application_id)
    command = [tools_root / "dotnet/dotnet", tools_root / "content-inspector/SwitchContent.dll",
               "inspect", "--keys", keys, "--base", base]
    if update:
        command += ["--update", update]
    if application_id is not None:
        command += ["--application-id", application_id]
    try:
        result = capture(command, env, timeout=300)
        report = json.loads(result.stdout)
    except (OSError, subprocess.SubprocessError, ValueError):
        fail("inspection_failed", "Content inspection failed. No inspector output or key material was logged.")
    if not isinstance(report, dict):
        fail("inspection_failed", "Content inspector returned an invalid report.")
    if result.returncode or report.get("status") != "validated":
        # Never relay arbitrary child output: crypto parser failures may contain
        # input text. These codes come from our inspector, not game filenames.
        error = report.get("error")
        code = error.get("code", "inspection_failed") if isinstance(error, dict) else "inspection_failed"
        safe_codes = {"keys_missing", "keys_invalid", "keys_incomplete", "content_unreadable",
                      "container_invalid", "metadata_invalid", "base_ambiguous", "update_not_patch",
                      "update_mismatch", "update_not_newer", "package_missing", "content_missing",
                      "application_id_invalid", "application_not_found"}
        fail(code if code in safe_codes else "inspection_failed",
             "Base/update inspection did not validate. Check file availability, matching game/version and your supplied console keys. No key contents were logged.")
    validate_selection(report, update is not None, application_id)
    return report


def matching_profile(source, selection):
    profile = source / "dm/games" / selection["application_id"].upper() / "game.tsv"
    if not profile.is_file():
        fail("profile_missing", "No registered native profile exists for this application ID; no provisional build was started.")
    rows = [line.split("\t") for line in profile.read_text().splitlines()
            if line.strip() and not line.lstrip().startswith("#")]
    values = {r[0]: r[1:] for r in rows}
    if values.get("title_id", [""])[0].lower() != selection["application_id"]:
        fail("profile_mismatch", "Profile title ID does not match validated content.")
    builds = {r[1]: r[2] for r in rows if r[0] == "build" and len(r) >= 3}
    version = "v" + str(selection["patch_version"] if selection["patch_version"] is not None else selection["base_version"])
    if version not in builds:
        fail("profile_version", "Selected content version is not registered by this game's native profile. Registered versions: " + ", ".join(builds))
    available = {"v" + str(selection["base_version"])}
    if selection["patch_version"] is not None:
        available.add("v" + str(selection["patch_version"]))
    needed = set(" ".join(values.get("shader_versions", [])).split())
    if not needed or not needed.issubset(available):
        fail("profile_shader_versions", "The profile requires shader source versions absent from the selected base/update pair: " + ", ".join(sorted(needed - available)))
    return {"application_id": selection["application_id"], "package": version,
            "display_version": builds[version], "shader_versions": sorted(needed),
            "module_build_ids_verified": False}


def upstream_command(tools_root, source, work, keys, profile, team_id):
    return [str(tools_root / "venv/bin/python"), str(source / "scripts/build_ipa_from_nsp.py"),
            str(work / "inputs"), "--keys", str(keys), "--prepare-xcode", "--romfs", "separate",
            "--keep-extracted", "--team-id", team_id,
            "--bundle-id", "com.dell.satchel.native." + profile["application_id"],
            "--work-dir", str(work / "build"), "--xcode-workspace", str(work / "NativeGames.xcworkspace"),
            "--package", profile["package"], "--gpu", "none", "--progress", "lines"]


def run_upstream(command, env, source, work, keys):
    # Upstream's stage logs stay in this private work directory. Its key reader
    # has no progress logger; key values are not emitted. Redact key paths and
    # long hexadecimal strings defensively from the companion's console/log.
    with (work / "prepare.log").open("w") as log:
        with subprocess.Popen(command, cwd=source, env=env, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, text=True, errors="replace") as process:
            for line in process.stdout:
                safe = line.replace(str(keys), "<console keys>")
                safe = re.sub(r"(?i)\b[0-9a-f]{32,}\b", "<hex omitted>", safe)
                log.write(safe)
                print(safe, end="", flush=True)
            return process.wait()


def prepare(args):
    application_id = normalized_application_id(getattr(args, "application_id", None))
    tools_root = Path(args.tools_root).expanduser().resolve()
    source = Path(args.source).expanduser().resolve() if args.source else tools_root / "NX2Apple"
    work = Path(args.work_dir).expanduser().resolve()
    if work.is_relative_to(ROOT) or work.is_relative_to(source) or work.is_relative_to(tools_root):
        fail("work_location", "Use a dedicated build directory outside the app repository and tool checkout.")
    if work.exists() and (not work.is_dir() or any(work.iterdir())):
        fail("work_not_empty", "Work directory is not empty. Choose a new directory; existing files were preserved.")
    if not re.fullmatch(r"[A-Z0-9]{10}", args.team_id):
        fail("team_id", "Supply a ten-character Apple developer team ID.")
    base, base_size = selected_file(args.base, "base package")
    update, update_size = selected_file(args.update, "update package") if args.update else (None, 0)
    keys, _ = selected_file(args.keys, "console key file")
    if update == base or keys in (base, update):
        fail("input_overlap", "Base, update and console key file must be different files.")
    # stat reads metadata only. This check precedes inspection, which can cause
    # iCloud to hydrate the selected package, and precedes any workspace writes.
    space = check_space(work, base_size + update_size)
    readiness = doctor(tools_root, source, work)
    if readiness["status"] != "ready":
        blocked = ", ".join(c["name"] for c in readiness["checks"] if c["status"] == "blocked")
        fail("dependencies", "Build prerequisites are blocked: " + blocked + ". Run doctor for details.")
    env = tool_environment(tools_root)
    report = inspect_content(base, update, keys, tools_root, env, application_id)
    if sum(m["type"] == "Application" for m in report["base"]["metadata"]) > 1:
        # Inspection can select a member, but the pinned native extractor picks
        # the lowest Program title ID. Do not build another title by accident.
        fail("native_multititle_unsupported", "The package contains multiple applications. Metadata selection succeeded, but the pinned native extractor cannot select a specific member. Use a single-application base package; no build was started.")
    profile = matching_profile(source, report["selection"])
    # Inspection can hydrate large cloud files on the same volume.
    check_space(work, base_size + update_size)
    work.mkdir(parents=True, exist_ok=True, mode=0o700)
    inputs = work / "inputs"
    inputs.mkdir(mode=0o700)
    selection = report["selection"]
    (inputs / f"Base [{selection['application_id'].upper()}][v{selection['base_version']}].{report['base']['format']}").symlink_to(base)
    if update:
        (inputs / f"Update [{selection['patch_id'].upper()}][v{selection['patch_version']}].nsp").symlink_to(update)
    manifest = {"schema_version": 1, "mode": "native_game_development", "status": "preparing",
                "source_commit": SOURCE_PIN, "selection": selection, "profile": profile, "disk": space,
                "limitations": ["Game-specific signed development build, not a generic emulator.",
                                "CNMT selection does not verify NSO module build IDs or gameplay.",
                                "Review upstream derived-profile notices before signing or installation."]}
    manifest_path = work / "preparation.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
    print("Validated base/update metadata; preparing native development project. Module compatibility and gameplay remain unverified.", flush=True)
    try:
        code = run_upstream(upstream_command(tools_root, source, work, keys, profile, args.team_id), env, source, work, keys)
    except (OSError, subprocess.SubprocessError):
        code = -1
    manifest["status"] = "prepared_unvalidated" if code == 0 else "build_failed"
    manifest["upstream_exit_code"] = code
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
    if code:
        fail("build_failed", "Native preparation failed. Preserved diagnostic output in the work directory's prepare.log; nothing was installed.")
    return manifest


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    for name in ("doctor", "prepare"):
        command = commands.add_parser(name)
        command.add_argument("--tools-root", default=str(DEFAULT_TOOLS))
        command.add_argument("--source", help="NX2Apple checkout at the pinned commit")
        command.add_argument("--work-dir", required=name == "prepare", default="/tmp/satchel-native-game")
        if name == "prepare":
            command.add_argument("--base", required=True)
            command.add_argument("--update")
            command.add_argument("--application-id", help="Confirm a 16-digit application ID; native preparation currently requires a single-application base package")
            command.add_argument("--keys", required=True, help="Path only; never logged")
            command.add_argument("--team-id", required=True)
    args = parser.parse_args(argv)
    try:
        if args.command == "doctor":
            tools = Path(args.tools_root).expanduser().resolve()
            source = Path(args.source).expanduser().resolve() if args.source else tools / "NX2Apple"
            result = doctor(tools, source, Path(args.work_dir).expanduser().resolve())
        else:
            result = prepare(args)
        print(json.dumps(result, indent=2))
        return 0 if result["status"] in ("ready", "prepared_unvalidated") else 2
    except PreflightError as exc:
        print(json.dumps({"schema_version": 1, "status": "blocked", "error": {"code": exc.code, "message": str(exc)}}, indent=2))
        return 2
    except (OSError, ValueError, subprocess.SubprocessError):
        print(json.dumps({"schema_version": 1, "status": "blocked", "error": {"code": "operation_failed", "message": "Operation failed; no raw exception or key material was logged."}}, indent=2))
        return 2


if __name__ == "__main__":
    sys.exit(main())
