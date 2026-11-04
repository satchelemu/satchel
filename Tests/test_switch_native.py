"""Companion preflight tests use synthetic metadata and a harmless build stub."""
import argparse
from contextlib import redirect_stdout
import importlib.util
import io
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


SPEC = importlib.util.spec_from_file_location("switch_native", Path(__file__).resolve().parents[1] / "scripts/switch_native.py")
native = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(native)
APP = "010015100b514000"
PATCH = "010015100b514800"


def metadata(with_update=True):
    base = {"format": "xci", "metadata_integrity_checked": True, "metadata": [
        {"id": APP, "type": "Application", "version": 0, "patch_id": PATCH}]}
    update = {"format": "nsp", "metadata_integrity_checked": True, "metadata": [
        {"id": PATCH, "type": "Patch", "version": 327680, "application_id": APP}]} if with_update else None
    return {"schema_version": 1, "status": "validated", "base": base, "update": update,
            "selection": {"application_id": APP, "base_version": 0,
                          "patch_id": PATCH if with_update else None,
                          "patch_version": 327680 if with_update else None}}


class NativePreparationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="satchel-native-test-")
        self.root = Path(self.temp.name)
        self.tools = self.root / "tools"
        self.source = self.tools / "NX2Apple"
        self.work = self.root / "output"
        self.profile = self.source / "dm/games" / APP.upper() / "game.tsv"
        self.profile.parent.mkdir(parents=True)
        self.profile.write_text(f"title_id\t{APP.upper()}\nbuild\tv0\t1.0.0\tmain=abc\nbuild\tv327680\t1.2.1\tmain=def\nshader_versions\tv0 v327680\n")
        # Shell syntax and misleading extensions must never affect identity or execute.
        self.base = self.root / "Wrong [1111111111111111][v8] $(touch injected);.nsp"
        self.update = self.root / "Update `touch injected` [v9].xci"
        self.keys = self.root / "private-console-file"
        self.base.write_bytes(b"synthetic base")
        self.update.write_bytes(b"synthetic update")
        self.keys.write_bytes(b"synthetic secret")
        self.args = argparse.Namespace(tools_root=str(self.tools), source=str(self.source),
            work_dir=str(self.work), base=str(self.base), update=str(self.update), keys=str(self.keys),
            team_id="TEAMTEST01", application_id=None)
        self.space = patch.object(native.shutil, "disk_usage", return_value=shutil._ntuple_diskusage(100 * native.GIB, 0, 100 * native.GIB))
        self.ready = patch.object(native, "doctor", return_value={"status": "ready", "checks": []})
        self.space.start()
        self.ready.start()

    def tearDown(self):
        self.ready.stop()
        self.space.stop()
        self.temp.cleanup()

    def prepare_with_report(self, report, run_result=0):
        response = subprocess.CompletedProcess([], 0, json.dumps(report), "")
        with patch.object(native, "capture", return_value=response), \
             patch.object(native, "run_upstream", return_value=run_result) as run, redirect_stdout(io.StringIO()):
            result = native.prepare(self.args)
        return result, run

    def test_real_workspace_uses_verified_metadata_and_preserves_originals(self):
        before = [p.read_bytes() for p in (self.base, self.update, self.keys)]
        result, run = self.prepare_with_report(metadata())
        self.assertEqual(result["status"], "prepared_unvalidated")
        names = sorted(p.name for p in (self.work / "inputs").iterdir())
        self.assertEqual(names, [f"Base [{APP.upper()}][v0].xci", f"Update [{PATCH.upper()}][v327680].nsp"])
        for path, original in zip(sorted((self.work / "inputs").iterdir()), (self.base, self.update)):
            self.assertTrue(path.is_symlink())
            self.assertEqual(path.resolve(), original.resolve())
        self.assertEqual(before, [p.read_bytes() for p in (self.base, self.update, self.keys)])
        command = run.call_args.args[0]
        for flag, value in (("--package", "v327680"), ("--romfs", "separate"),
                            ("--bundle-id", "com.dell.satchel.native." + APP), ("--gpu", "none")):
            self.assertEqual(command[command.index(flag) + 1], value)
        self.assertIn("--prepare-xcode", command)
        self.assertIn("--keep-extracted", command)
        self.assertFalse(any(flag.startswith(("--allow-", "--provisional", "--no-heap")) for flag in command))
        saved = (self.work / "preparation.json").read_text()
        self.assertNotIn(str(self.keys), saved)
        self.assertNotIn("synthetic secret", saved)
        self.assertFalse(result["profile"]["module_build_ids_verified"])
        self.assertFalse((self.root / "injected").exists())

    def test_mismatched_update_cannot_start_builder(self):
        report = metadata()
        report["update"]["metadata"][0]["application_id"] = "0100000000000000"
        with self.assertRaises(native.PreflightError) as exc:
            self.prepare_with_report(report)
        self.assertEqual(exc.exception.code, "metadata_mismatch")
        self.assertFalse(self.work.exists())

    def test_requested_application_is_normalized_and_forwarded_to_inspector(self):
        self.args.application_id = APP.upper()
        response = subprocess.CompletedProcess([], 0, json.dumps(metadata()), "")
        with patch.object(native, "capture", return_value=response) as inspect, \
             patch.object(native, "run_upstream", return_value=0), redirect_stdout(io.StringIO()):
            result = native.prepare(self.args)
        command = inspect.call_args.args[0]
        self.assertEqual(command[command.index("--application-id") + 1], APP)
        self.assertEqual(result["selection"]["application_id"], APP)

    def test_omitted_application_id_preserves_inspector_default(self):
        response = subprocess.CompletedProcess([], 0, json.dumps(metadata()), "")
        with patch.object(native, "capture", return_value=response) as inspect:
            native.inspect_content(self.base, self.update, self.keys, self.tools, {})
        self.assertNotIn("--application-id", inspect.call_args.args[0])

    def test_invalid_application_id_is_rejected_before_input_access(self):
        for value in ("", "0" * 16, "0x" + APP, APP + "0", " " + APP, "g" * 16, "$(touch injected)"):
            with self.subTest(value=value):
                self.args.application_id = value
                with patch.object(native, "selected_file") as selected, \
                     patch.object(native, "inspect_content") as inspect, self.assertRaises(native.PreflightError) as exc:
                    native.prepare(self.args)
                self.assertEqual(exc.exception.code, "application_id_invalid")
                selected.assert_not_called()
                inspect.assert_not_called()
        self.assertFalse(self.work.exists())

    def test_inspector_cannot_return_different_application_than_requested(self):
        self.args.application_id = "0100999900000000"
        with self.assertRaises(native.PreflightError) as exc:
            self.prepare_with_report(metadata())
        self.assertEqual(exc.exception.code, "metadata_mismatch")
        self.assertFalse(self.work.exists())

    def test_multititle_inspection_selects_requested_record_but_native_build_refuses(self):
        report = metadata()
        report["base"]["metadata"].insert(0, {"id": "0100000000000000", "type": "Application",
                                             "version": 0, "patch_id": "0100000000000800"})
        self.assertEqual(native.validate_selection(report, True, APP), report["selection"])
        with self.assertRaises(native.PreflightError):
            native.validate_selection(report, True)
        self.args.application_id = APP
        with self.assertRaises(native.PreflightError) as exc:
            self.prepare_with_report(report)
        self.assertEqual(exc.exception.code, "native_multititle_unsupported")
        self.assertFalse(self.work.exists())

    def test_duplicate_selected_application_metadata_is_ambiguous(self):
        report = metadata()
        report["base"]["metadata"].append(dict(report["base"]["metadata"][0]))
        with self.assertRaises(native.PreflightError) as exc:
            native.validate_selection(report, True, APP)
        self.assertEqual(exc.exception.code, "metadata_mismatch")

    def test_prepare_cli_accepts_optional_application_id(self):
        with patch.object(native, "prepare", return_value={"status": "prepared_unvalidated"}) as prepare, \
             redirect_stdout(io.StringIO()):
            code = native.main(["prepare", "--base", str(self.base), "--keys", str(self.keys),
                                "--work-dir", str(self.work), "--team-id", self.args.team_id,
                                "--application-id", APP.upper()])
        self.assertEqual(code, 0)
        self.assertEqual(prepare.call_args.args[0].application_id, APP.upper())

    def test_unregistered_version_and_missing_shader_source_are_rejected(self):
        report = metadata()
        report["selection"]["patch_version"] = 65536
        report["update"]["metadata"][0]["version"] = 65536
        with self.assertRaises(native.PreflightError) as exc:
            self.prepare_with_report(report)
        self.assertEqual(exc.exception.code, "profile_version")
        self.args.update = None
        with self.assertRaises(native.PreflightError) as exc:
            self.prepare_with_report(metadata(False))
        self.assertEqual(exc.exception.code, "profile_shader_versions")
        self.assertFalse(self.work.exists())

    def test_unknown_game_cannot_trigger_upstream_provisional_profile(self):
        report = metadata(False)
        report["selection"]["application_id"] = "0100999900000000"
        report["base"]["metadata"][0]["id"] = "0100999900000000"
        self.args.update = None
        with self.assertRaises(native.PreflightError) as exc:
            self.prepare_with_report(report)
        self.assertEqual(exc.exception.code, "profile_missing")
        self.assertFalse(self.work.exists())

    def test_low_disk_stops_before_inspection_or_workspace_creation(self):
        with patch.object(native.shutil, "disk_usage", return_value=shutil._ntuple_diskusage(4, 3, 1)), \
             patch.object(native, "inspect_content") as inspect, self.assertRaises(native.PreflightError) as exc:
            native.prepare(self.args)
        self.assertEqual(exc.exception.code, "disk_space")
        inspect.assert_not_called()
        self.assertFalse(self.work.exists())

    def test_existing_work_and_repository_paths_are_not_overwritten(self):
        self.work.mkdir()
        keep = self.work / "keep.txt"
        keep.write_text("existing work")
        with self.assertRaises(native.PreflightError) as exc:
            native.prepare(self.args)
        self.assertEqual(exc.exception.code, "work_not_empty")
        self.assertEqual(keep.read_text(), "existing work")
        self.args.work_dir = str(native.ROOT / "build")
        with self.assertRaises(native.PreflightError) as exc:
            native.prepare(self.args)
        self.assertEqual(exc.exception.code, "work_location")

    def test_failed_upstream_preserves_manifest_and_reports_no_install(self):
        with self.assertRaises(native.PreflightError) as exc:
            self.prepare_with_report(metadata(), run_result=7)
        self.assertEqual(exc.exception.code, "build_failed")
        saved = json.loads((self.work / "preparation.json").read_text())
        self.assertEqual(saved["status"], "build_failed")
        self.assertEqual(saved["upstream_exit_code"], 7)
        self.assertTrue(self.base.exists())

    def test_child_error_never_echoes_key_contents_or_path(self):
        response = subprocess.CompletedProcess([], 2, json.dumps({"status": "error", "error": {
            "code": "keys_invalid", "message": "synthetic secret " + str(self.keys)}}), "synthetic secret")
        with patch.object(native, "capture", return_value=response), self.assertRaises(native.PreflightError) as exc:
            native.inspect_content(self.base, self.update, self.keys, self.tools, {})
        self.assertEqual(exc.exception.code, "keys_invalid")
        self.assertNotIn("synthetic secret", str(exc.exception))
        self.assertNotIn(str(self.keys), str(exc.exception))

    def test_subprocess_arguments_are_literal_and_output_is_redacted(self):
        self.work.mkdir()
        marker = self.root / "injected"
        hostile = "$(touch " + str(marker) + "); `touch " + str(marker) + "`"
        code = "import sys; print(sys.argv[1]); print(sys.argv[2]); print('a'*32)"
        stream = io.StringIO()
        with redirect_stdout(stream):
            result = native.run_upstream([sys.executable, "-c", code, hostile, str(self.keys)], {}, self.root, self.work, self.keys)
        self.assertEqual(result, 0)
        self.assertFalse(marker.exists())
        self.assertIn(hostile, stream.getvalue())
        self.assertNotIn(str(self.keys), stream.getvalue())
        self.assertNotIn("a" * 32, (self.work / "prepare.log").read_text())

    def test_source_pin_and_modified_profile_checks_fail_closed(self):
        (self.source / "scripts").mkdir()
        (self.source / "scripts/build_ipa_from_nsp.py").write_text("# fixture")
        wrong = subprocess.CompletedProcess([], 0, "wrong\n", "")
        with patch.object(native, "capture", return_value=wrong), self.assertRaises(native.PreflightError) as exc:
            native.source_check(self.source, {})
        self.assertEqual(exc.exception.code, "source_pin")
        pinned = subprocess.CompletedProcess([], 0, native.SOURCE_PIN + "\n", "")
        dirty = subprocess.CompletedProcess([], 1, "", "")
        with patch.object(native, "capture", side_effect=[pinned, dirty]), self.assertRaises(native.PreflightError) as exc:
            native.source_check(self.source, {})
        self.assertEqual(exc.exception.code, "source_modified")

    def test_tool_environment_keeps_real_compiler_and_removes_bypasses(self):
        self.tools.mkdir(exist_ok=True)
        (self.tools / "native-toolchain.json").write_text(json.dumps({"metal_toolchain": "/selected/Metal.xctoolchain"}))
        with patch.dict(native.os.environ, {"DM_ALLOW_OTHER_METAL": "1", "DM_GAMES_FIRST": "1", "PATH": "/usr/bin"}, clear=True):
            env = native.tool_environment(self.tools)
        self.assertNotIn("DM_ALLOW_OTHER_METAL", env)
        self.assertNotIn("DM_GAMES_FIRST", env)
        self.assertEqual(env["SATCHEL_METAL_TOOLCHAIN"], "/selected/Metal.xctoolchain")
        self.assertEqual(env["PATH"].split(native.os.pathsep)[0], str(self.tools / "bin"))


if __name__ == "__main__":
    unittest.main()
