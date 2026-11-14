"""tex3ds with a content cache.

Many cooks encode the same image again (a driver's textures for every
vehicle, team colours, LOD copies). tex3ds always writes the same bytes for
the same input and options, so run() keeps each result under a key made from
tex3ds itself, the options and the input image's bytes, and later calls copy
it instead of encoding again.

The cache is used only when MKW_TEX3DS_CACHE names a folder (tools/build.py
sets it to <work>/cache/tex3ds); otherwise run() is subprocess.run.
"""
import atexit, hashlib, os, shutil, subprocess, sys, threading
from pathlib import Path

VERSION = b'tex3ds-cache 1\0'
# Options that write a second file or read more than the one input.
UNCACHED = {'-H', '--header', '-d', '--deps', '-a', '--atlas', '-i', '--include'}

_lock = threading.Lock()
_tools = {}
_stats = [0, 0]


def _tool_digest(exe):
    with _lock:
        if exe not in _tools:
            _tools[exe] = hashlib.sha256(Path(exe).read_bytes()).digest()
        return _tools[exe]


def _report():
    if any(_stats):
        print(f'tex3ds cache: {_stats[0]} reused, {_stats[1]} encoded', file=sys.stderr, flush=True)


atexit.register(_report)


def run(argv, **kwargs):
    """subprocess.run(argv, **kwargs) for a tex3ds command whose last argument
    is the input image and whose -o names the output."""
    folder = os.environ.get('MKW_TEX3DS_CACHE')
    argv = [str(a) for a in argv]
    if not folder or '-o' not in argv[1:-1] or UNCACHED.intersection(argv) or not Path(argv[-1]).is_file():
        return subprocess.run(argv, **kwargs)
    at = argv.index('-o', 1)
    output, source = Path(argv[at + 1]), Path(argv[-1])
    h = hashlib.sha256(VERSION + _tool_digest(argv[0]))
    h.update('\0'.join(argv[1:at] + argv[at + 2:-1]).encode() + b'\0')
    h.update(hashlib.sha256(source.read_bytes()).digest())
    key = h.hexdigest()
    cached = Path(folder) / key[:2] / key
    if cached.is_file():
        try:
            shutil.copyfile(cached, output)
            with _lock:
                _stats[0] += 1
            empty = '' if kwargs.get('text') else b''
            return subprocess.CompletedProcess(argv, 0, empty, empty)
        except OSError:
            pass  # unreadable for now: encode instead
    done = subprocess.run(argv, **kwargs)
    if done.returncode == 0 and output.is_file():
        cached.parent.mkdir(parents=True, exist_ok=True)
        temp = cached.with_name(f'{key}.{os.getpid()}.{threading.get_ident()}.tmp')
        shutil.copyfile(output, temp)
        # An entry is created once, whole, and never replaced, so a reader never
        # meets a file being swapped (Windows refuses to open one). A link fails
        # if another job stored it first; keep that one.
        try:
            os.link(temp, cached)
        except FileExistsError:
            pass
        except OSError:
            if not cached.exists():
                try:
                    os.replace(temp, cached)  # no hard links on this volume
                except OSError:
                    pass
        temp.unlink(missing_ok=True)
        with _lock:
            _stats[1] += 1
    return done
