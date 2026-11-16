import argparse
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import build


class BuildPythonTests(unittest.TestCase):
    def test_python_312_is_accepted(self):
        build.check_python((3, 12, 10))

    def test_wrong_python_stops_before_setup(self):
        with patch.object(build.sys, 'version_info', (3, 15, 2)), \
                patch.object(build, 'check_venv') as check_venv, \
                patch.object(build, 'check_disc') as check_disc, \
                patch.object(build, 'provision') as provision:
            with self.assertRaisesRegex(build.BuildError, r'requires Python 3\.12'):
                build.bootstrap(argparse.Namespace())
            check_venv.assert_not_called()
            check_disc.assert_not_called()
            provision.assert_not_called()

    def test_existing_environment_is_checked_before_setup(self):
        with tempfile.TemporaryDirectory() as folder:
            venv = Path(folder) / '.venv'
            python = venv / 'Scripts/python.exe'
            python.parent.mkdir(parents=True)
            python.touch()
            (venv / '.mkw-requirements').write_text('previously installed')
            result = subprocess.CompletedProcess([], 0, stdout='[3, 15, 2]')
            with patch.object(build, 'VENV', venv), \
                    patch.object(build, 'VENV_PYTHON', python), \
                    patch.object(build.sys, 'version_info', (3, 12, 10)), \
                    patch.object(build.subprocess, 'run', return_value=result), \
                    patch.object(build, 'check_disc') as check_disc, \
                    patch.object(build, 'provision') as provision:
                with self.assertRaisesRegex(build.BuildError, r'Remove only .*\.venv'):
                    build.bootstrap(argparse.Namespace())
                check_disc.assert_not_called()
                provision.assert_not_called()
            self.assertTrue(python.is_file())

    def test_matching_environment_keeps_installed_packages(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            venv, source = root / '.venv', root / 'source'
            venv.mkdir()
            source.mkdir()
            python = venv / 'python'
            python.touch()
            requirements = b'Pillow==12.3.0\n'
            (source / 'requirements.txt').write_bytes(requirements)
            stamp = hashlib.sha256(requirements + str(source).encode()).hexdigest()
            (venv / '.mkw-requirements').write_text(stamp)
            result = subprocess.CompletedProcess([], 0, stdout='[3, 12, 10]')
            with patch.object(build, 'VENV', venv), \
                    patch.object(build, 'VENV_PYTHON', python), \
                    patch.object(build.subprocess, 'run', return_value=result) as run:
                build.check_venv()
                build.prepare_venv(source)
                self.assertEqual(run.call_count, 1)
                self.assertIn('sys.version_info', run.call_args.args[0][-1])

    def test_broken_environment_has_recovery_instructions(self):
        with patch.object(Path, 'is_file', return_value=True), \
                patch.object(build.subprocess, 'run', return_value=subprocess.CompletedProcess([], 1)):
            with self.assertRaisesRegex(build.BuildError, 'cannot start.*Remove only'):
                build.check_venv()

    def test_missing_environment_needs_no_version_probe(self):
        with patch.object(Path, 'is_file', return_value=False), \
                patch.object(build.subprocess, 'run') as run:
            build.check_venv()
            run.assert_not_called()


if __name__ == '__main__':
    unittest.main()
