"""Builder checks: python -m unittest discover tests"""

import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / "tools"), str(ROOT / "tools" / "release")]
import build
import build_cia


class SourceSignatureTests(unittest.TestCase):
    def test_edit_with_preserved_timestamp_changes_signature(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "unit.cpp"
            source.write_text("int value = 1;\n")
            timestamp = source.stat().st_mtime_ns
            original = build.source_signature("compile flags", source)
            source.write_text("int value = 2;\n")
            os.utime(source, ns=(timestamp, timestamp))
            self.assertNotEqual(
                original, build.source_signature("compile flags", source)
            )

    def test_compile_signature_is_part_of_the_stamp(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "unit.cpp"
            source.write_text("int value = 1;\n")
            self.assertNotEqual(
                build.source_signature("flags a", source),
                build.source_signature("flags b", source),
            )


class StepOutputTests(unittest.TestCase):
    def test_detects_missing_empty_and_empty_directory_outputs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "empty-file").touch()
            (root / "empty-dir").mkdir()
            (root / "ready-dir").mkdir()
            (root / "ready-dir" / "output.bin").write_bytes(b"valid")
            (root / "ready-file").write_bytes(b"valid")
            missing = build_cia.missing_outputs(
                root, ["missing", "empty-file", "empty-dir", "ready-dir", "ready-file"]
            )
            self.assertEqual(missing, ["missing", "empty-file", "empty-dir"])

    def test_every_race_course_object_list_is_an_output(self):
        outputs = next(
            outputs
            for name, _, outputs in build_cia.COOK_STEPS
            if name == "course objects"
        )
        self.assertEqual(len(outputs), 32)

    def test_step_fails_if_successful_command_leaves_output_empty(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "build").mkdir()
            steps = [("test step", ["true"], ["result.bin"])]
            with (
                mock.patch.object(build_cia, "ROOT", root),
                mock.patch.object(build_cia, "COOK_STEPS", steps),
                mock.patch.object(build_cia, "run"),
            ):
                with self.assertRaisesRegex(SystemExit, "did not produce valid outputs"):
                    build_cia.stage_cook(None, 1, force=True)


class SharedExtractionTests(unittest.TestCase):
    def test_does_not_extract_into_a_shared_data_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            shared_data = root / "shared-data"
            shared_data.mkdir()
            # Windows normally requires extra privileges to create symlinks.
            # Mock the link metadata so the shared-data guard is tested there too.
            with (
                mock.patch.object(build_cia, "ROOT", root),
                mock.patch.object(Path, "is_symlink", autospec=True,
                                  side_effect=lambda path: path == root / "data"),
            ):
                with self.assertRaisesRegex(SystemExit, "shared data"):
                    build_cia.stage_disc(None)
            self.assertEqual(list(shared_data.iterdir()), [])


class DependencyPreflightTests(unittest.TestCase):
    def test_missing_unicorn_is_reported_before_conversion(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            sdk = root / 'sdk'
            (sdk / 'devkitARM').mkdir(parents=True)
            types = sdk / 'libctru/include/3ds/types.h'
            types.parent.mkdir(parents=True)
            types.write_text('#define CTR_ALIGN(n)\n')
            tex3ds = sdk / 'tex3ds.exe'
            tex3ds.touch()
            with (
                mock.patch.object(build_cia, 'ROOT', root),
                mock.patch('bootstrap_tools.tools', return_value=(root / 'makerom', root / 'bannertool')),
                mock.patch('devkitpro.root', return_value=sdk),
                mock.patch('devkitpro.tool', return_value=tex3ds),
                mock.patch('importlib.util.find_spec',
                           side_effect=lambda name: None if name == 'unicorn' else object()),
                mock.patch.object(build_cia, 'run') as run,
            ):
                with self.assertRaisesRegex(SystemExit, 'Missing Python packages unicorn'):
                    build_cia.stage_tools()
                run.assert_not_called()


class CourseOverlayTests(unittest.TestCase):
    def overlay_step(self):
        return next(s for s in build_cia.COOK_STEPS if s[0] == 'course material overlay')

    def write_receipt(self, root):
        receipt = root / 'build/course-materials/overlay-receipt.json'
        receipt.parent.mkdir(parents=True, exist_ok=True)
        receipt.write_text(json.dumps({'8': []}) + '\n')

    def test_unchanged_overlay_is_valid_and_resumable(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with (
                mock.patch.object(build_cia, 'ROOT', root),
                mock.patch.object(build_cia, 'COOK_STEPS', [self.overlay_step()]),
                mock.patch.object(build_cia, 'run', side_effect=lambda *args: self.write_receipt(root)) as run,
            ):
                with self.assertRaisesRegex(SystemExit, 'has not run'):
                    build_cia.stage_cook(None, 1, force=False, check=True)
                build_cia.stage_cook(None, 1, force=False)
                build_cia.stage_cook(None, 1, force=False, check=True)
                build_cia.stage_cook(None, 1, force=False)
                self.assertEqual(run.call_count, 1)
            self.assertFalse((root / 'build/course-materials/models').exists())

    def test_changed_overlay_is_valid_too(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            def cook(*args):
                models = root / 'build/course-materials/models'
                models.mkdir(parents=True)
                (models / 'course_01_000.gmdl').write_bytes(b'changed mesh')
                self.write_receipt(root)
            with (
                mock.patch.object(build_cia, 'ROOT', root),
                mock.patch.object(build_cia, 'COOK_STEPS', [self.overlay_step()]),
                mock.patch.object(build_cia, 'run', side_effect=cook) as run,
            ):
                build_cia.stage_cook(None, 1, force=False)
                build_cia.stage_cook(None, 1, force=False, check=True)
                self.assertEqual(run.call_count, 1)

    def test_failed_overlay_does_not_leave_a_success_stamp(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with (
                mock.patch.object(build_cia, 'ROOT', root),
                mock.patch.object(build_cia, 'COOK_STEPS', [self.overlay_step()]),
                mock.patch.object(build_cia, 'run', side_effect=RuntimeError('export failed')),
            ):
                with self.assertRaisesRegex(RuntimeError, 'export failed'):
                    build_cia.stage_cook(None, 1, force=False)
                with self.assertRaisesRegex(SystemExit, 'has not run'):
                    build_cia.stage_cook(None, 1, force=False, check=True)
            self.assertFalse((root / 'build/release/cook/course-material-overlay.done').exists())


@unittest.skipUnless(os.name == 'nt' and shutil.which('powershell'), 'Windows PowerShell required')
class WindowsLauncherTests(unittest.TestCase):
    def test_log_wrapper_preserves_exit_code_when_stderr_is_written(self):
        line = next(line for line in (ROOT / 'Build MKW CIA.bat').read_text().splitlines()
                    if line.startswith('powershell '))
        command = line.split('-Command "', 1)[1][:-1]
        original = "& '.venv\\Scripts\\python.exe' 'tools\\release\\build_cia.py' $env:MKW_DISC"
        self.assertIn(original, command)
        def quote(path):
            return "'" + str(path).replace("'", "''") + "'"
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            probe = root / 'probe.py'
            probe.write_text("import sys\nprint('compiler warning', file=sys.stderr)\n"
                             "print('probe complete')\nsys.exit(int(sys.argv[1]))\n")
            for code in (0, 7):
                with self.subTest(exit_code=code):
                    invocation = f'& {quote(sys.executable)} {quote(probe)} {code}'
                    result = subprocess.run(
                        ['powershell', '-NoProfile', '-Command', command.replace(original, invocation)],
                        cwd=root, capture_output=True, text=True, errors='replace', timeout=30,
                    )
                    self.assertEqual(result.returncode, code, result.stdout + result.stderr)
                    log = (root / 'build-cia-log.txt').read_text(encoding='utf-8-sig')
                    self.assertIn('compiler warning', log)
                    self.assertIn('probe complete', log)


if __name__ == "__main__":
    unittest.main()
