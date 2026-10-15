"""Builder checks: python -m unittest discover tests"""

import os
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
            (root / "data").symlink_to(shared_data, target_is_directory=True)
            with mock.patch.object(build_cia, "ROOT", root):
                with self.assertRaisesRegex(SystemExit, "shared data"):
                    build_cia.stage_disc(None)
            self.assertEqual(list(shared_data.iterdir()), [])


if __name__ == "__main__":
    unittest.main()
