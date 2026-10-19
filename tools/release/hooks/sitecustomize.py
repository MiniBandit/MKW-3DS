"""Builder steps on macOS: directory listings skip '._' AppleDouble files.

macOS writes a '._name' file beside every file it creates on exFAT/FAT
volumes (USB sticks, SD cards). A converter that unpacks an archive and then
lists '*.brres' would read those 4 KB metadata files as game data. The
builder puts this folder on PYTHONPATH for its steps (tools/release/build_cia.py
run()); other sitecustomize modules (Homebrew's) still run afterwards.
"""
import importlib.util
import os
import sys

if sys.platform == 'darwin':
    _scandir, _listdir = os.scandir, os.listdir

    def _hidden(name):
        return name.startswith(b'._' if isinstance(name, bytes) else '._')

    class _Entries:
        def __init__(self, it):
            self._it = it

        def __iter__(self):
            return self

        def __next__(self):
            while True:
                entry = next(self._it)
                if not _hidden(entry.name):
                    return entry

        def __enter__(self):
            return self

        def __exit__(self, *exc):
            self.close()

        def close(self):
            self._it.close()

    def scandir(path='.'):
        return _Entries(_scandir(path))

    def listdir(path='.'):
        return [name for name in _listdir(path) if not _hidden(name)]

    os.scandir, os.listdir = scandir, listdir

_here = os.path.dirname(os.path.abspath(__file__))
for _folder in sys.path:
    if os.path.abspath(_folder or '.') == _here:
        continue
    _file = os.path.join(_folder or '.', 'sitecustomize.py')
    if os.path.isfile(_file):
        _spec = importlib.util.spec_from_file_location('_next_sitecustomize', _file)
        _spec.loader.exec_module(importlib.util.module_from_spec(_spec))
        break
