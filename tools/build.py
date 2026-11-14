#!/usr/bin/env python3
"""Build the standard (release) game from your own Mario Kart Wii USA disc.

    py -3.12 tools/build.py --disc <RMCE01.iso|.wbfs>

It sets up .venv from requirements.txt, fetches the pinned converters in
dependencies.json (noclip, gclib, cfl-tool, pycgfx), extracts the disc, runs
every cook and generated-header stage, compiles libcitro3d, libCFL and the
game (MKW_RELEASE), builds the HOME banner and packages the release CIA and
the SD folder. See README.md, "Building".

Everything it writes goes under the work directory (default build/standard):

  source/   a copy of this tree where the cook tools run; their historical
            data/, assets/, build/ and sd-card/ folders fill up here
  compile/  shaders, libraries, generated headers, objects, the ELF
  output/   the CIA, the 3DSX and sd/ (copy sd/3ds to the SD card root)

A stopped or failed build resumes at the first unfinished stage. After a git
pull, a cook stage whose script, the tools/ modules it imports or the tracked
files it names changed runs again, with every stage after it. --from STAGE
runs that stage and every later one again (--list-stages shows the order).
The disc is only read.
"""
import argparse, ast, fnmatch, hashlib, inspect, json, os, platform, posixpath, re, shutil, struct, subprocess, sys, textwrap, time
import urllib.request, zipfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
VENV = REPO / '.venv'
VENV_PYTHON = VENV / ('Scripts/python.exe' if os.name == 'nt' else 'bin/python')
BUILD_PYTHON = (3, 12)
# Packages needed beyond requirements.txt are listed there too (gltflib for the banner).
DEPENDENCIES = [('noclip', 'noclip'), ('gclib', 'gclib'), ('cfl_tool', 'cfl-tool'), ('pycgfx', 'pycgfx')]
TITLE_ID = '000400000F4B6000'
GAME_3DSX = 'MarioKartWiiDS.3dsx'  # shipped name; the compile stage's own file stays mkw-3ds.3dsx
BUILT_3DSX = 'mkw-3ds.3dsx'  # that file's name in resource_manifest.py's listing
CXX_STD = 'c++23'


class BuildError(Exception):
    pass


def log(text):
    print(text, flush=True)


def digest(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(4 << 20), b''):
            h.update(block)
    return h.hexdigest()


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_suffix(path.suffix + '.tmp')
    temp.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')
    os.replace(temp, path)


def read_json(path, default=None):
    return json.loads(path.read_text(encoding='utf-8')) if path.is_file() else default


def platform_key():
    machine = platform.machine().lower()
    arch = 'arm64' if machine in ('arm64', 'aarch64') else 'x86_64'
    system = {'win32': 'windows', 'darwin': 'darwin'}.get(sys.platform, 'linux')
    return f'{system}-{arch}'


def title_version():
    """makerom's -ver: major << 10 | minor << 4 | micro (0.9.4 -> 148)."""
    major, minor, micro = (int(x) for x in (REPO / 'VERSION').read_text().strip().split('.'))
    return major << 10 | minor << 4 | micro


# ---------------------------------------------------------------------------
# Inputs and host tools (run with the Python that started the build)

def check_disc(path):
    if not path.is_file():
        raise BuildError(f'Disc image not found: {path}')
    with path.open('rb') as f:
        header = f.read(512)
        if header[:4] == b'WBFS':
            if not 9 <= header[8] <= 20:
                raise BuildError('The WBFS header is invalid.')
            slot = next((x for x in header[12:] if x), None)
            if slot is None:
                raise BuildError('The WBFS file holds no game.')
            f.seek(slot * (1 << header[8]))
            header = f.read(256)
    if header[:6] != b'RMCE01':
        raise BuildError('This needs Mario Kart Wii USA (RMCE01) as an ISO or WBFS. Convert RVZ or NKit images first.')
    if header[7] != 0 or header[24:28] != bytes.fromhex('5d1c9ea3'):
        raise BuildError('This needs the normal RMCE01 revision 0 disc image.')


def check_cfl(path):
    if not path.is_file():
        raise BuildError(f'CFL_Res.dat not found: {path}')
    data = path.read_bytes()
    if not 84 <= len(data) <= 1083400 or struct.unpack_from('<H', data)[0] != 20:
        raise BuildError(f'{path} is not a 3DS CFL_Res.dat (it is not CFL_DB.dat or a Mii file).')


def devkitpro():
    sys.path.insert(0, str(REPO / 'tools'))
    import devkitpro as dkp
    sys.path.pop(0)
    root = dkp.root()
    # Git Bash rewrites DEVKITPRO=/opt/devkitpro to a path under its own install.
    for fallback in (os.environ.get('DEVKITPRO_WIN'), 'C:/devkitPro', '/opt/devkitpro'):
        if not (root / 'devkitARM').is_dir() and fallback and (Path(fallback) / 'devkitARM').is_dir():
            root = Path(fallback)
    dkp.root = lambda: root
    needed = [dkp.arm('arm-none-eabi-g++'), dkp.arm('arm-none-eabi-gcc'), dkp.arm('arm-none-eabi-ar'),
              dkp.tool('picasso'), dkp.tool('tex3ds'), dkp.tool('3dsxtool'), root / 'libctru/lib/libctru.a']
    missing = [str(p) for p in needed if not p.is_file()]
    if missing:
        raise BuildError('devkitPro is incomplete (install devkitARM, libctru and 3ds-tools). Missing:\n  ' + '\n  '.join(missing))
    return root


def host_cxx(sdk):
    """A host C++ compiler for the offline helpers (menu measurement, Mii heads, banner lighting)."""
    candidates = [os.environ.get('CXX')]
    if os.name == 'nt':
        candidates.append(str(sdk / 'msys2/usr/bin/g++.exe'))
    candidates += [shutil.which(n) for n in ('g++', 'clang++', 'c++')]
    for c in candidates:
        if c and (Path(c).is_file() or shutil.which(c)):
            return str(Path(c) if Path(c).is_file() else shutil.which(c))
    raise BuildError('No host C++ compiler found. On Windows run "pacman -S gcc" in devkitPro\'s MSYS2 shell, '
                     'or set CXX to a C++23 compiler.')


def download(url, sha256, cache):
    cache.mkdir(parents=True, exist_ok=True)
    target = cache / url.rsplit('/', 1)[1]
    if target.is_file() and digest(target) == sha256:
        return target
    log(f'Downloading {url}')
    part = target.with_suffix(target.suffix + '.part')
    request = urllib.request.Request(url, headers={'User-Agent': 'mkw-3ds-build'})
    with urllib.request.urlopen(request, timeout=60) as response, part.open('wb') as out:
        shutil.copyfileobj(response, out, 1 << 20)
    if digest(part) != sha256:
        part.unlink()
        raise BuildError(f'{url} does not match its pinned SHA-256 (tools/release/toolchain.lock.json).')
    os.replace(part, target)
    return target


def unpack_archive(archive, folder, member=None):
    """Unpack a downloaded tool archive, refusing paths that leave the folder.
    A .7z (only the Windows ffmpeg) goes through Windows' own tar, which
    reads 7-Zip; just its one member is extracted."""
    folder = folder.resolve()
    stamp = folder / '.archive-sha256'
    if stamp.is_file() and stamp.read_text() == digest(archive):
        return folder
    if folder.exists():
        shutil.rmtree(folder)
    folder.mkdir(parents=True)
    if archive.suffix == '.zip':
        with zipfile.ZipFile(archive) as z:
            for item in z.infolist():
                target = (folder / item.filename).resolve()
                if not target.is_relative_to(folder):
                    raise BuildError(f'Unsafe path in {archive.name}: {item.filename}')
            z.extractall(folder)
    else:
        tar = Path(os.environ.get('SystemRoot', 'C:/Windows')) / 'System32/tar.exe'
        done = subprocess.run([str(tar), '-xf', str(archive), '-C', str(folder), member], capture_output=True, text=True)
        if done.returncode or not (folder / member).is_file():
            raise BuildError(f'Could not unpack {archive.name} ({done.stderr.strip()}). '
                             'Install ffmpeg yourself (winget install Gyan.FFmpeg) and run the build again.')
    stamp.write_text(digest(archive))
    return folder


def has_soxr(ffmpeg):
    """The banner sound is resampled with soxr, which minimal ffmpeg builds leave out."""
    conf = subprocess.run([ffmpeg, '-hide_banner', '-buildconf'], capture_output=True, text=True)
    return 'libsoxr' in conf.stdout + conf.stderr


def provision(work, sdk):
    """Resolve every host tool once; missing ones come from the pinned downloads."""
    lock = json.loads((REPO / 'tools/release/toolchain.lock.json').read_text())
    key = platform_key()
    cache, tools = work / 'downloads', work / 'tools'
    found = {'devkitpro': str(sdk), 'cxx': host_cxx(sdk)}
    found['git'] = shutil.which('git')
    if not found['git']:
        raise BuildError('git is required (https://git-scm.com/).')

    def pinned(name, entry, member):
        if not entry:
            raise BuildError(f'{name} is not on PATH and has no pinned download for {key}; install it.')
        folder = unpack_archive(download(entry['url'], entry['sha256'], cache), tools / name, member)
        path = folder / member
        if os.name != 'nt':
            path.chmod(0o755)
        return str(path)

    for name in ('makerom', 'bannertool', 'ffmpeg', 'node'):
        exe = os.environ.get(name.upper()) or shutil.which(name)
        if exe and name == 'ffmpeg' and not has_soxr(exe):
            if key != 'windows-x86_64':
                raise BuildError(f'{exe} was built without libsoxr. Install an ffmpeg that has it and set FFMPEG to its path '
                                 '(on macOS: brew install ffmpeg-full, see README.md).')
            log(f'{exe} has no soxr resampler; using the pinned ffmpeg instead')
            exe = None
        if exe:
            found[name] = exe
        elif name == 'bannertool':
            found[name] = pinned(name, lock['bannertool'], lock['bannertool']['members'].get(key, ''))
        else:
            entry = lock[name]['platforms'].get(key)
            found[name] = pinned(name, entry, entry and entry['member'])
    node = Path(found['node'])
    npm = [p for p in (node.parent / 'node_modules/npm/bin/npm-cli.js', node.parent.parent / 'lib/node_modules/npm/bin/npm-cli.js') if p.is_file()]
    found['npm'] = [str(node), str(npm[0])] if npm else [shutil.which('npm') or 'npm']
    return found


# ---------------------------------------------------------------------------
# Source copy, converters and venv

def sync_source(source, state_path):
    """Copy this tree's tracked and untracked (not ignored) files into source/.

    The cook tools write next to themselves (data/, assets/, build/, sd-card/,
    include/mkw); running them on a copy keeps this checkout clean. Files that
    left the tree since the last copy are removed; everything else there is
    the cooks' own output and stays."""
    files = subprocess.run(['git', '-C', str(REPO), 'ls-files', '-z', '--cached', '--others', '--exclude-standard'], capture_output=True)
    if files.returncode:
        raise BuildError(f'{REPO} is not a git checkout. Get the code with git clone (see README.md); a ZIP download can\'t be built.')
    listing = files.stdout.decode().split('\0')
    names = sorted(n for n in listing if n and (REPO / n).is_file())
    previous = set(read_json(state_path, []))
    copied = 0
    for name in names:
        src, dst = REPO / name, source / name
        if dst.is_file() and dst.stat().st_size == src.stat().st_size and dst.read_bytes() == src.read_bytes():
            continue
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)
        copied += 1
    for name in previous - set(names):
        (source / name).unlink(missing_ok=True)
    write_json(state_path, names)
    log(f'Source copy: {copied} of {len(names)} files updated ({source})')


def git(*args, **kw):
    return subprocess.run(['git', *map(str, args)], check=True, **kw)


def fetch_dependencies(source):
    deps = json.loads((REPO / 'dependencies.json').read_text())
    for key, folder in DEPENDENCIES:
        url, commit, target = deps[key]['url'], deps[key]['commit'], source / 'vendor' / folder
        head = subprocess.run(['git', '-C', str(target), 'rev-parse', 'HEAD'], capture_output=True, text=True)
        if (target / '.git').is_dir() and head.returncode == 0:
            if head.stdout.strip() != commit:
                raise BuildError(f'{target} is at {head.stdout.strip()}, not the pinned {commit}; delete it to fetch again.')
            continue
        log(f'Fetching {folder} {commit[:12]}')
        if target.exists():
            shutil.rmtree(target)
        target.mkdir(parents=True)
        git('init', '-q', target)
        git('-C', target, 'config', 'core.longpaths', 'true')
        git('-C', target, 'remote', 'add', 'origin', url)
        git('-C', target, 'fetch', '-q', '--depth=1', 'origin', commit)
        git('-C', target, 'checkout', '-q', '--detach', 'FETCH_HEAD')
    noclip, patch = source / 'vendor/noclip', source / 'patches/noclip-brres.patch'
    applied = subprocess.run(['git', '-C', str(noclip), 'apply', '--reverse', '--check', '--ignore-whitespace', str(patch)], capture_output=True)
    if applied.returncode:
        git('-C', noclip, 'apply', '--ignore-whitespace', patch)
        log('Applied patches/noclip-brres.patch')


def check_python(version, *, venv=False):
    if tuple(version[:2]) == BUILD_PYTHON:
        return
    required = '.'.join(map(str, BUILD_PYTHON))
    found = '.'.join(map(str, version[:3]))
    command = f'py -{required}' if os.name == 'nt' else f'python{required}'
    location = f'The existing virtual environment {VENV}' if venv else 'This build command'
    recovery = f'Remove only {VENV} and rerun the build. ' if venv else ''
    raise BuildError(f'{location} uses Python {found}; the standard build requires Python {required}. '
                     f'{recovery}Install Python {required} if needed, then use '
                     f'{command} tools/build.py --disc "..." --cfl "..." (see README.md).')


def check_venv():
    if VENV_PYTHON.is_file():
        result = subprocess.run([str(VENV_PYTHON), '-c',
                                 'import json, sys; print(json.dumps(list(sys.version_info[:3])))'],
                                capture_output=True, text=True)
        if result.returncode:
            raise BuildError(f'The existing virtual environment {VENV} cannot start. '
                             'Remove only that directory and rerun the build (see README.md).')
        check_python(json.loads(result.stdout), venv=True)


def prepare_venv(source):
    if not VENV_PYTHON.is_file():
        log(f'Creating {VENV}')
        subprocess.run([sys.executable, '-m', 'venv', str(VENV)], check=True)
    requirements = source / 'requirements.txt'
    stamp = VENV / '.mkw-requirements'
    want = hashlib.sha256(requirements.read_bytes() + str(source).encode()).hexdigest()
    if stamp.is_file() and stamp.read_text() == want:
        return
    log('Installing Python packages from requirements.txt')
    subprocess.run([str(VENV_PYTHON), '-m', 'pip', 'install', '--disable-pip-version-check', '-q', '-r', str(requirements)],
                   cwd=source, check=True)
    stamp.write_text(want)


def bootstrap(args):
    if sys.platform in ('cygwin', 'msys'):
        raise BuildError('Run this with a native Python (on Windows: py -3.12 tools/build.py ...), not the MSYS2 one.')
    check_python(sys.version_info)
    check_venv()
    disc = Path(args.disc).expanduser().resolve()
    cfl = Path(args.cfl).expanduser().resolve() if args.cfl else None
    check_disc(disc)
    if cfl:
        check_cfl(cfl)
    sdk = devkitpro()
    work = Path(args.work).expanduser().resolve()
    work.mkdir(parents=True, exist_ok=True)
    if not (work / 'state.json').is_file() and shutil.disk_usage(work).free < 30 << 30:
        raise BuildError(f'A full build needs about 30 GB free in {work}.')
    tools = provision(work, sdk)
    write_json(work / 'tools.json', tools)
    source = work / 'source'
    sync_source(source, work / 'source-files.json')
    fetch_dependencies(source)
    prepare_venv(source)
    command = [str(VENV_PYTHON), str(Path(__file__).resolve()), '--worker', '--disc', str(disc), *(['--cfl', str(cfl)] if cfl else []),
               '--work', str(work), '--jobs', str(args.jobs)] + (['--from', args.start] if args.start else []) + (
                   ['--delete-converted'] if args.delete_converted else [])
    return subprocess.run(command).returncode


# ---------------------------------------------------------------------------
# Stage fingerprints

class SourceIndex:
    """The files of this tree a stage reads, found from its code.

    A cook stage depends on its script, the tools/ modules that script imports
    (and theirs), the tracked files its string literals name (config JSON,
    host-tool C++ sources, artwork) and the headers those C++ files include.
    Its fingerprint hashes all of them with the stage's arguments. A name built
    at run time (f'{name}.cpp') is not seen; --from STAGE covers that."""

    CPP = ('.c', '.cc', '.cpp', '.h', '.hh', '.hpp', '.inc')
    SCRIPT = ('.ts', '.mjs', '.js')

    def __init__(self, names, read=None):
        # The working tree by default, or `read` for another commit's files.
        if read is None:
            names, read = [n for n in names if (REPO / n).is_file()], lambda n: (REPO / n).read_bytes()
        self.read = read
        self.files = {n for n in names if not n.endswith('.md')}
        self.by_base = {}
        for name in self.files:
            self.by_base.setdefault(name.rsplit('/', 1)[-1], []).append(name)
        self.refs, self.hashes = {}, {}

    def named(self, text):
        """Tracked files a string names, as a path ('tools/x.cpp') or a bare file name."""
        found = set()
        for piece in re.split(r'[\s,;=]+', text.replace('\\', '/')):
            piece = piece.strip('\'"()[]').lstrip('./')
            found.update(n for n in self.by_base.get(piece.rsplit('/', 1)[-1], ()) if n.endswith(piece))
        return found

    def module(self, name, folder):
        path = name.replace('.', '/')
        for root in (folder, 'tools'):
            for candidate in (f'{root}/{path}.py', f'{root}/{path}/__init__.py'):
                if candidate in self.files:
                    return {candidate}
        return set()

    def python(self, text, folder):
        try:
            tree = ast.parse(text)
        except SyntaxError:
            return set()
        # Docstrings and other bare strings are prose, not file names.
        prose = {id(n.value) for n in ast.walk(tree) if isinstance(n, ast.Expr) and isinstance(n.value, ast.Constant)}
        found = set()
        for node in ast.walk(tree):
            if isinstance(node, ast.Import):
                for alias in node.names:
                    found |= self.module(alias.name, folder)
            elif isinstance(node, ast.ImportFrom):
                base = node.module or ''
                if base:
                    found |= self.module(base, folder)
                for alias in node.names:
                    found |= self.module(f'{base}.{alias.name}' if base else alias.name, folder)
            elif isinstance(node, ast.Constant) and isinstance(node.value, str) and id(node) not in prose:
                found |= self.named(node.value)
        return found

    def cpp(self, text):
        found = set()
        for include in re.findall(r'#\s*include\s*["<]([^">]+)[">]', text):
            found.update(n for n in self.by_base.get(include.rsplit('/', 1)[-1], ()) if n.endswith(include))
        return found

    def script(self, text, name):
        found = set()
        for spec in re.findall(r"""(?:from|import|require)\s*\(?\s*['"](\.{1,2}/[^'"]+)['"]""", text):
            path = posixpath.normpath(posixpath.join(posixpath.dirname(name), spec))
            found.update(c for c in (path, path + '.ts', path + '.mjs', path + '.js', path + '/index.ts') if c in self.files)
        return found

    def glob(self, pattern):
        return {n for n in self.files if fnmatch.fnmatchcase(n, pattern)}

    def closure(self, names):
        seen, todo = set(), list(names)
        while todo:
            name = todo.pop()
            if name in seen or name not in self.files:
                continue
            seen.add(name)
            if name not in self.refs:
                data = self.read(name)
                # Line endings depend on the checkout, not the file.
                self.hashes[name] = hashlib.sha256(data.replace(b'\r\n', b'\n')).hexdigest()
                text, suffix = data.decode('utf-8', 'replace'), posixpath.splitext(name)[1]
                self.refs[name] = (self.python(text, posixpath.dirname(name)) if suffix == '.py' else
                                   self.cpp(text) if suffix in self.CPP else
                                   self.script(text, name) if suffix in self.SCRIPT else set())
            todo.extend(self.refs[name])
        return sorted(seen)


# ---------------------------------------------------------------------------
# Stages (run inside .venv, with the source copy's tools on sys.path)

class Builder:
    def __init__(self, args):
        self.work = Path(args.work)
        self.root = self.work / 'source'
        self.compile_dir = self.work / 'compile'
        self.output = self.work / 'output'
        self.banner_work = self.work / 'home-banner'
        self.logs = self.work / 'logs'
        self.state_path = self.work / 'state.json'
        self.disc, self.jobs = Path(args.disc or '-'), max(1, args.jobs)
        self.cfl = Path(args.cfl) if args.cfl else None
        self.delete_after = args.delete_converted
        self.stages = self.define_stages()

    def prepare(self):
        self.logs.mkdir(parents=True, exist_ok=True)
        self.tools = read_json(self.work / 'tools.json')
        self.sdk = Path(self.tools['devkitpro'])
        self.cxx = self.tools['cxx']
        self.python = sys.executable
        # The host compiler's folder goes before the inherited PATH: MSYS2's
        # cc1plus finds msys-2.0.dll through PATH, and another MSYS runtime
        # (Git Bash's) would give it the wrong /usr. Ours come first so that
        # folder's own python and git never shadow them.
        paths = [Path(self.python).parent, Path(self.tools['node']).parent, Path(self.tools['ffmpeg']).parent,
                 Path(self.tools['git']).parent, self.sdk / 'tools/bin', self.sdk / 'devkitARM/bin', Path(self.cxx).parent]
        env = dict(os.environ)
        env.pop('PYTHONPATH', None)
        env.pop('PYTHONHOME', None)
        env['PATH'] = os.pathsep.join([*map(str, paths), env.get('PATH', '')])
        env.update(DEVKITPRO=str(self.sdk), DEVKITPRO_WIN=str(self.sdk), DEVKITARM=str(self.sdk / 'devkitARM'),
                   PYTHONUNBUFFERED='1', PYTHONIOENCODING='utf-8', GIT_TERMINAL_PROMPT='0',
                   npm_config_cache=str(self.work / 'downloads/npm-cache'))
        # One temp folder for every tool: MSYS2's g++ misreads a temp path
        # inherited from another MSYS shell such as Git Bash.
        temp = self.work / 'tmp'
        # GCC's LTO writes this path into a makefile unquoted, so it can't hold a space.
        if ' ' in str(temp):
            temp = Path(temp.anchor if os.name == 'nt' else '/tmp') / 'mkw-3ds-tmp'
        self.temp = temp
        temp.mkdir(parents=True, exist_ok=True)
        env.update(TMP=temp.as_posix(), TEMP=temp.as_posix(), TMPDIR=temp.as_posix())
        self.env = env
        os.environ.update(env)
        # The cook tools are imported from the source copy, never from this checkout.
        here = Path(__file__).resolve().parent
        sys.path[:] = [p for p in sys.path if Path(p or '.').resolve() != here]
        sys.path.insert(0, str(self.root / 'tools'))
        os.chdir(self.root)

    # -- helpers ----------------------------------------------------------

    def run(self, name, argv, cwd=None, env=None):
        argv = [str(a) for a in argv]
        # Apple's linker spells --gc-sections as -dead_strip. Host compiler only: devkitARM links with GNU ld.
        if sys.platform == 'darwin' and argv[0] == self.cxx:
            argv = ['-Wl,-dead_strip' if a == '-Wl,--gc-sections' else a for a in argv]
        with (self.logs / (name + '.log')).open('w', encoding='utf-8', errors='replace') as stream:
            process = subprocess.run(argv, cwd=cwd or self.root, env=env or self.env,
                                     stdout=stream, stderr=subprocess.STDOUT)
        if process.returncode:
            tail = (self.logs / (name + '.log')).read_text(encoding='utf-8', errors='replace').splitlines()[-25:]
            raise BuildError(f'{name} failed (exit {process.returncode}). Log: {self.logs / (name + ".log")}\n' + '\n'.join(tail))

    def py(self, name, script, *args):
        self.run(name, [self.python, 'tools/' + script, *args])

    def node(self, name, *args):
        self.run(name, [self.tools['node'], *args])

    # -- extraction -------------------------------------------------------

    def dependencies(self):
        self.run('npm-ci', [*self.tools['npm'], 'ci', '--no-audit', '--no-fund'])
        for p in sorted((self.root / 'tools').glob('export_*.ts')):
            self.node('bundle-' + p.stem, 'tools/bundle_cooker.mjs', 'tools/' + p.name)

    def extract(self):
        root = self.root
        self.py('disc-extract', 'extract_disc.py', self.disc, 'data/extracted',
                *[x for p in ('Race/', 'Scene/', 'thp/', 'sound/', 'Boot/', 'rel/', 'Mii/') for x in ('--prefix', p)])
        from extract_disc import Disc
        from cook_roster import data_partition
        disc = Disc(self.disc)
        partition = data_partition(disc)
        offset = struct.unpack_from('>I', partition.read(0, 0x440), 0x420)[0] * 4
        header = partition.read(offset, 0x100)
        size = max(o + n for o, n in zip(struct.unpack_from('>18I', header), struct.unpack_from('>18I', header, 0x90)))
        (root / 'build/original').mkdir(parents=True, exist_ok=True)
        (root / 'build/original/main.dol').write_bytes(partition.read(offset, size))
        disc.f.close()
        shutil.copyfile(root / 'data/extracted/rel/StaticR.rel', root / 'build/itembox-retail.rel')
        from unpack_szs import unpack
        from course_catalog import courses
        (root / 'sd-card/3ds/mkw-3ds/models').mkdir(parents=True, exist_ok=True)
        for folder, out in [('Scene/UI', 'menu-ui'), ('Scene/Model', 'menu-models')]:
            for archive in sorted((root / 'data/extracted' / folder).glob('*.szs')):
                unpack(archive, root / 'assets' / out / archive.stem)
        unpack(root / 'data/extracted/Race/Common.szs', root / 'assets/common')
        wanted = {Path(c['archive']).name for c in courses()}
        for archive in sorted((root / 'data/extracted/Race/Course').glob('*.szs')):
            if archive.name not in wanted:
                (root / 'data/extra-courses').mkdir(parents=True, exist_ok=True)
                os.replace(archive, root / 'data/extra-courses' / archive.name)
        for c in courses():
            unpack(root / 'data/extracted' / c['archive'], root / 'assets/courses' / c['key'])
        shutil.copytree(root / 'assets/courses/beginner_course', root / 'assets/luigi', dirs_exist_ok=True)
        shutil.copytree(root / 'assets/courses/shopping_course', root / 'assets/coconut-mall', dirs_exist_ok=True)
        for name in ('Race', 'Race_U', 'Font'):
            unpack(root / 'data/extracted/Scene/UI' / (name + '.szs'), root / 'assets/race-ui' / name)

    def portraits(self):
        root = self.root
        from unpack_szs import unpack
        self.py('extract-rfl', 'extract_disc.py', self.disc, 'data/extracted', '--file', 'contents/RFLRes01.arc',
                '--to', 'data/extracted/contents/RFLRes01.arc')
        unpack(root / 'data/extracted/contents/RFLRes01.arc', root / 'assets/mii/rfl')
        resource = next(p for p in sorted((root / 'assets/mii/rfl').rglob('*')) if p.is_file() and p.name.lower().endswith('.dat'))
        bank = root / 'sd-card/3ds/mkw-3ds/mii/parts.mpr'
        self.py('mii-bank', 'cook_mii_bank.py', resource, bank)
        exporter = root / 'build/host-tools' / ('export_mii_head' + ('.exe' if os.name == 'nt' else ''))
        exporter.parent.mkdir(parents=True, exist_ok=True)
        self.run('mii-head-host', [self.cxx, '-std=' + CXX_STD, '-O2', '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
                                   '-Iinclude', 'tools/export_mii_head.cpp', 'src/mii_head.cpp', 'src/mii_part_bank.cpp', '-o', exporter])
        base = root / 'assets/mii/course-portraits'
        statues = sorted(p.parent for p in (root / 'assets/courses').glob('*/course.bmm'))
        self.py('mii-portraits', 'cook_default_mii_portraits.py', 'build/original/main.dol', bank, exporter, base, '--statue-courses', *statues)
        shutil.copy2(root / 'config/course-mii-assignments.json', base / 'assignments.json')
        statues = read_json(base / 'defaults.json')['statues']
        write_json(base / 'statue-portraits.json', {'statues': statues})
        shutil.copytree(base, root / 'build/mii-parts/retail-defaults', dirs_exist_ok=True)

    def checkpointed(self, kind, key, action):
        """Per-course and per-vehicle resume inside the two longest stages. A
        checkpoint counts only under the stage fingerprint it was cooked with."""
        marker = self.work / 'checkpoints' / kind / (key + '.json')
        stamp = marker.with_suffix('.key')
        if marker.is_file() and (stamp.read_text() == self.stage_key if stamp.is_file() else self.adopt_checkpoints):
            stamp.write_text(self.stage_key)
            return read_json(marker)
        result = action()
        write_json(marker, result)
        stamp.write_text(self.stage_key)
        return result

    def courses(self):
        root, sd = self.root, self.root / 'sd-card/3ds/mkw-3ds'
        from course_catalog import courses
        from cook_model import cook, COURSE_TEXELS, LARGE_TEXELS
        from import_courses import kmp_database

        def one(c):
            folder, visuals, lists = root / 'assets/courses' / c['key'], [], []
            for kind, resource in [('course', 'course_model.brres'), ('sky', 'vrcorn_model.brres')]:
                source = folder / resource
                if not source.exists():
                    continue
                prefix = 'coconut_sky' if (c['id'], kind) == (5, 'sky') else c['prefix'] + ('_sky' if kind == 'sky' else '')
                settings = {**self.env, 'MKW_FREEZE_NONLINEAR_UV': '1', 'MKW_COURSE_MATERIALS': '1', 'MKW_RETAIL_LIGHTING': '1',
                            'MKW_RETAIL_SKY': '1' if kind == 'sky' else '0', 'MKW_SKY_FOG': '1' if kind == 'sky' else '0'}
                self.run(prefix + '-export', [self.tools['node'], 'build/export_brres.mjs', source, root / 'build/course-exports' / prefix], env=settings)
                parts = cook(root / 'build/course-exports' / prefix, sd, prefix, course_geometry=kind == 'course',
                             texel_budget=LARGE_TEXELS if kind == 'sky' else COURSE_TEXELS)
                detail = read_json(sd / f'models/{prefix}-receipt.json')
                visuals.append({'kind': kind, 'resource': resource, 'parts': parts, 'lighting': detail.get('lighting'), 'limitations': detail['limitations']})
                lists.append('models/' + prefix + '.list')
            receipt = {**c, 'archive_sha256': digest(root / 'data/extracted' / c['archive']),
                       'resources': {p.name: {'sha256': digest(p), 'bytes': p.stat().st_size} for p in folder.iterdir() if p.is_file()},
                       'visuals': visuals, 'kmp': kmp_database(folder / 'course.kmp'), 'sector_h0_verified': True}
            target = sd / f"models/courses/{c['id']:02d}.json"
            write_json(target, receipt)
            log(f"  course {c['id']:2d} {c['name']}")
            return {**c, 'archive_sha256': receipt['archive_sha256'], 'status': 'imported',
                    'receipt': target.relative_to(sd).as_posix(), 'model_lists': lists}
        with ThreadPoolExecutor(max_workers=self.jobs) as pool:
            rows = list(pool.map(lambda c: self.checkpointed('courses', c['prefix'], lambda: one(c)), courses()))
        write_json(sd / 'models/course-catalog.json', {'version': 1, 'courses': rows})
        self.py('catalog-header', 'generate_runtime_tables.py', 'catalog')

    def garage(self):
        root = self.root
        from cook_roster import ROSTER, write_native_roster, sha256
        from cook_garage import VEHICLES, inspect_driver, inspect_kart, cook_choice, write_native_garage
        from unpack_szs import unpack
        display = (root / 'assets/common/kartDriverDispParam.bin').read_bytes()

        def choice(driver, slot, vehicle, name, vstem):
            slug, title, character, stem, size = driver
            original = f'Race/Kart/{vstem}-{stem}.szs'
            compressed, prefix = root / 'data/extracted' / original, f'{slug}_{vstem}_'
            folder = root / 'assets/vehicles' / prefix.rstrip('_')
            unpack(compressed, folder)
            seat_y, seat_z = struct.unpack_from('>2f', display, 8 + (vehicle * 48 + character) * 56)
            row = dict(driverIndex=ROSTER.index(driver), slot=slot, driver=title, driverSlug=slug, name=name, prefix=prefix,
                       character=character, vehicle=vehicle, bike=vehicle >= 18, seatY=seat_y, seatZ=seat_z, archive=original,
                       archive_sha256=sha256(compressed), driver_sha256=sha256(folder / 'driver_model.brres'),
                       kart_sha256=sha256(folder / 'kart_model.brres'), clips=inspect_driver(folder)['chr0'],
                       parts=[m['model'] for m in inspect_kart(folder)['nodes']])
            return cook_choice(row, root / 'build/roster')

        def driver_rows(driver):
            rows = [self.checkpointed('garage', f'{driver[0]}_{vstem}', lambda: choice(driver, slot, vehicle, name, vstem))
                    for slot, (vehicle, name, vstem) in enumerate(VEHICLES[driver[4]])]
            log(f'  driver {driver[1]}')
            return rows
        with ThreadPoolExecutor(max_workers=self.jobs) as pool:
            rows = [r for group in pool.map(driver_rows, ROSTER) for r in group]
        target = root / 'build/roster/models'
        write_json(target / 'garage.json', {'version': 1, 'choices': rows})
        characters = [{'slug': d[0], 'name': d[1], 'character': d[2], 'prefix': d[0] + '_', 'vehicle': VEHICLES[d[4]][0][0]} for d in ROSTER]
        write_json(target / 'roster.json', {'version': 1, 'characters': characters})
        for name in ('garage.json', 'roster.json'):
            shutil.copy2(target / name, root / 'sd-card/3ds/mkw-3ds/models' / name)
        write_json(root / 'assets/vehicles/garage-audit.json', {'version': 1, 'choices': rows})
        write_native_roster(display)
        write_native_garage(rows)

    MENU_SCALE_SOURCES = ['tools/menu_sprite_scales.cpp', *['src/' + n + '.cpp' for n in (
        'menu_ui', 'menu_state', 'menu_transition', 'menu_focus', 'menu_index', 'menu_text_motion')]]

    def fit_menu(self):
        host = self.root / 'build/host-tools'
        host.mkdir(parents=True, exist_ok=True)
        exe = host / ('menu_sprite_scales' + ('.exe' if os.name == 'nt' else ''))
        self.run('menu-scales-host', [self.cxx, '-std=' + CXX_STD, '-O2', '-Iinclude', '-o', exe, *self.MENU_SCALE_SOURCES])
        self.run('menu-scales', [exe, host / 'sprite-scales.json'])
        self.py('menu-fit', 'optimize_menu_textures.py')

    # -- HOME banner ------------------------------------------------------

    def banner(self):
        work, art = self.banner_work, self.banner_work / 'production'
        self.py('banner-extract', 'release/extract_banner_sources.py', '--disc', self.disc, '--output', work)
        for stage in ('export', 'scene', 'cgfx'):
            self.py('banner-' + stage, 'release/build_banner_model.py', stage, '--work', work, '--cxx', self.cxx,
                    '--pycgfx-root', self.root / 'vendor/pycgfx')
        from PIL import Image
        icon = Image.open(self.root / 'tools/release/artwork/icon.png').convert('RGBA')
        for n in (48, 24):
            icon.resize((n, n), Image.Resampling.LANCZOS).save(art / f'icon{n}.png')
        self.run('banner-icon', [self.tools['bannertool'], 'makesmdh', '-s', 'Mario Kart WiiDS', '-l', 'Mario Kart WiiDS',
                                 '-p', 'Epic0522/ethanseagull', '-i', art / 'icon48.png', '-si', art / 'icon24.png', '-o', art / 'icon.icn'])
        self.banner_sound(art)

    def banner_sound(self, art):
        """The HOME Menu sound is the start of the Wii Disc Channel banner sound
        (opening.bnr meta/sound.bin, 32 kHz stereo), resampled to 44.1 kHz and
        cut at the 3.000 s banner limit with a 0.2 s fade. bannertool encodes it
        as a PCM16 CWAV, which replaces the sound makebanner wrote next to the CGFX."""
        import wii_banner_sound, wave
        temp = self.work / 'home-banner/sound'
        temp.mkdir(parents=True, exist_ok=True)
        channels, rate, pcm = wii_banner_sound.decode((self.banner_work / 'archives/opening.bnr').read_bytes())
        with wave.open(str(temp / 'start.wav'), 'wb') as w:
            w.setnchannels(channels); w.setsampwidth(2); w.setframerate(rate); w.writeframes(pcm)
        trimmed = temp / 'banner.wav'
        self.run('banner-sound', [self.tools['ffmpeg'], '-v', 'error', '-y', '-i', temp / 'start.wav', '-af',
                                  'aresample=44100:resampler=soxr,atrim=end_sample=132300,afade=t=out:st=2.8:d=0.2',
                                  '-c:a', 'pcm_s16le', '-ar', '44100', trimmed])
        self.run('banner-cwav', [self.tools['bannertool'], 'makecwav', '-i', trimmed, '-o', temp / 'banner.bcwav'])
        self.run('banner-package', [self.tools['bannertool'], 'makebanner', '-ci', art / 'banner.cgfx', '-a', trimmed, '-o', temp / 'graphics.bnr'])
        sound, release = (temp / 'banner.bcwav').read_bytes(), (temp / 'graphics.bnr').read_bytes()
        at = struct.unpack_from('<I', release, 0x84)[0]
        if release[at:at + 4] != b'CWAV' or sound[:4] != b'CWAV':
            raise BuildError('Unexpected bannertool banner layout.')
        info = sound.find(b'INFO')
        encoding, loop = sound[info + 8], sound[info + 9]
        rate, _, end = struct.unpack_from('<III', sound, info + 12)
        if encoding != 1 or loop or rate != 44100 or end > 132300:
            raise BuildError(f'Unexpected banner CWAV: encoding {encoding} loop {loop} rate {rate} samples {end}')
        (art / 'home-banner.bnr').write_bytes(release[:at] + sound)

    # -- compile ----------------------------------------------------------

    def compile(self):
        out, sdk = self.compile_dir, self.sdk
        gcc, gxx, ar = (sdk / 'devkitARM/bin' / n for n in ('arm-none-eabi-gcc', 'arm-none-eabi-g++', 'arm-none-eabi-ar'))
        shader_dir, native_dir, generated = out / 'shader', out / 'native-mii', out / 'generated'
        for d in (shader_dir, native_dir):
            d.mkdir(parents=True, exist_ok=True)

        def binary_header(data, path, symbol):
            rows = [','.join(f'0x{b:02x}' for b in data[i:i + 16]) + ',' for i in range(0, len(data), 16)]
            text = (f'#pragma once\nstatic const unsigned char {symbol}[] __attribute__((aligned(4))) = {{\n' + '\n'.join(rows)
                    + f'\n}};\nstatic const unsigned int {symbol}_size = {len(data)};\n')
            if not path.is_file() or path.read_text() != text:
                path.write_text(text)

        # Shaders: the scene, effect and Mii programs in that order (the Mii
        # variant keeps every scene uniform at the same register).
        import mii_shader, shader_outputs
        scene = REPO / 'src/mkw.v.pica'
        mii = shader_dir / 'native_mii.v.pica'
        mii.write_bytes(mii_shader.variant(scene.read_bytes().decode().replace('\r\n', '\n')).replace('\n', '\r\n').encode())
        for p in (scene, REPO / 'src/effect.v.pica', mii):
            shader_outputs.check(p.read_text())
        self.run('picasso', [sdk / 'tools/bin/picasso', '-o', shader_dir / 'mkw.shbin', scene, REPO / 'src/effect.v.pica', mii])
        binary_header((shader_dir / 'mkw.shbin').read_bytes(), shader_dir / 'shader_data.hpp', 'mkw_shader')
        self.run('picasso-cfl', [sdk / 'tools/bin/picasso', '-o', native_dir / 'vshader.shbin', REPO / 'vendor/cfl-mii/source/vshader.v.pica'])
        binary_header((native_dir / 'vshader.shbin').read_bytes(), native_dir / 'vshader_shbin.h', 'vshader_shbin')

        # Generated headers: the 67 cooked tables, kept apart from the tree's own headers.
        declared = json.loads((REPO / 'config/generated-inputs.json').read_text())['files']
        missing = [n for n in declared if not (self.root / 'include' / n).is_file()]
        if missing:
            raise BuildError('Cook stages did not produce: ' + ', '.join(missing))
        for name in declared:
            src, dst = self.root / 'include' / name, generated / name
            dst.parent.mkdir(parents=True, exist_ok=True)
            if not dst.is_file() or dst.read_bytes() != src.read_bytes():
                shutil.copyfile(src, dst)

        # Kinoko profile-guided optimization: a Kinoko source with a profile in
        # tools/pgo/kinoko/ (tools/diag/compile.py MKW_KINOKO_PGO=generate makes them) is
        # built with it; MKW_KINOKO_PGO=off ignores them. Same flags as the DIAG build.
        kinoko_source, pgo_dir = REPO / 'vendor/kinoko/source', REPO / 'tools/pgo/kinoko'
        pgo_use = os.environ.get('MKW_KINOKO_PGO', '') != 'off'
        pgo_flags = ['-fprofile-use', '-fprofile-partial-training', '-Wno-missing-profile', '-Wno-coverage-mismatch',
                     '-fno-unroll-loops', '-fno-peel-loops']

        def compile_set(label, compiler, sources, flags, base, objdir):
            def one(src):
                obj = objdir / src.relative_to(base).with_suffix('.o')
                obj.parent.mkdir(parents=True, exist_ok=True)
                f = flags + (['-flto=auto'] if src.suffix == '.cc' else [])
                profile = b''
                if pgo_use and src.suffix == '.cc' and src.is_relative_to(kinoko_source):
                    gcda = (pgo_dir / src.relative_to(kinoko_source)).with_suffix('.gcda')
                    if gcda.is_file():
                        # GCC reads the profile beside the object (its -o path with .gcda).
                        profile = gcda.read_bytes()
                        obj.with_suffix('.gcda').write_bytes(profile)
                        f = f + pgo_flags
                key = hashlib.sha256(src.read_bytes() + signature + ' '.join(f).encode() + profile).hexdigest()
                stamp = obj.with_suffix('.sha256')
                if obj.is_file() and stamp.is_file() and stamp.read_text() == key:
                    return obj, None
                run = subprocess.run([str(compiler), *f, '-c', str(src), '-o', str(obj)], env=self.env, capture_output=True, text=True)
                obj.with_suffix('.log').write_text(run.stdout + run.stderr)
                if run.returncode:
                    return obj, f'{src.relative_to(base)}\n{(run.stdout + run.stderr)[-3000:]}'
                stamp.write_text(key)
                return obj, None
            with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
                results = list(pool.map(one, sources))
            failed = [e for _, e in results if e]
            log(f'  {label}: {len(sources) - len(failed)}/{len(sources)} compiled')
            if failed:
                raise BuildError('\n\n'.join(failed))
            return [o for o, _ in results]

        headers = sorted([*(REPO / 'include').rglob('*.hpp'), *(REPO / 'include').rglob('*.inc'), *(REPO / 'include').rglob('*.h'),
                          *(REPO / 'vendor/kinoko').rglob('*.hh'), *(REPO / 'vendor/citro3d').rglob('*.h'), *(REPO / 'vendor/cfl-mii').rglob('*.h'),
                          shader_dir / 'shader_data.hpp', native_dir / 'vshader_shbin.h', *generated.rglob('*.hpp'), *generated.rglob('*.inc')])
        signature = hashlib.sha256(b''.join(p.read_bytes() for p in headers)).digest()

        library_flags = json.loads((REPO / 'docs/MANUAL-LIBRARY-FLAGS.json').read_text())
        def flags_for(key):
            return [f.replace('<SOURCE_ROOT>', REPO.as_posix()).replace('<DEVKITPRO>', sdk.as_posix()).replace('<EXTERNAL_OUTPUT>', out.as_posix())
                    for f in library_flags[key]['flags']]
        for key, folder, archive in [('hardware_sdk', 'citro3d', 'libcitro3d.a'), ('native_mii', 'cfl-mii', 'libCFL.a')]:
            sources = sorted((REPO / 'vendor' / folder / 'source').rglob('*.c'))
            objects = compile_set(archive, gcc, sources, flags_for(key), REPO, out / 'objects-lib')
            (out / archive).unlink(missing_ok=True)
            self.run('ar-' + folder, [ar, 'rcs', out / archive, *objects])

        arch = ['-march=armv6k', '-mtune=mpcore', '-mfloat-abi=hard', '-mtp=soft']
        flags = arch + ['-O2', '-g', '-mword-relocations', '-ffunction-sections', '-fdata-sections', '-ffp-contract=off',
                        '-std=gnu++23', '-D__3DS__', '-DREVOLUTION', '-DMKW_RELEASE', '-DKINOKO_QUIET',
                        '-fno-exceptions', '-fno-rtti', '-fcheck-new', '-fno-strict-aliasing', '-ffile-prefix-map=' + REPO.as_posix() + '/=']
        flags += ['-I' + p.as_posix() for p in [shader_dir, REPO / 'vendor/kinoko/include', REPO / 'vendor/kinoko/source', REPO / 'include',
                                                REPO / 'vendor/cfl-mii/include', REPO / 'vendor/citro3d/include', sdk / 'libctru/include', generated]]
        sources = [REPO / s.strip() for s in (REPO / 'docs/MANUAL-PROGRAM-SOURCES.txt').read_text().splitlines() if s.strip() and not s.startswith('#')]
        objects = compile_set('game', gxx, sources, flags, REPO, out / 'objects')
        rsp = out / 'objects.rsp'
        rsp.write_text('\n'.join('"' + p.as_posix() + '"' for p in objects))
        wrappers = ['linearAlloc', 'linearMemAlign', 'linearRealloc', 'linearGetSize', 'linearFree', 'linearSpaceFree', 'fopen',
                    'vramAlloc', 'vramAllocAt', 'vramMemAlign', 'vramMemAlignAt', 'vramRealloc', 'vramGetSize', 'vramFree', 'vramSpaceFree']
        elf = out / 'mkw-3ds.elf'
        self.run('link', [gxx, *arch, '-flto=auto', '-specs=3dsx.specs', '-Wl,--gc-sections', '-Wl,-Map=' + str(out / 'mkw-3ds.map'),
                          *['-Wl,--wrap=' + w for w in wrappers], '@' + str(rsp), out / 'libCFL.a', out / 'libcitro3d.a',
                          '-L' + str(sdk / 'libctru/lib'), '-lctru', '-lm', '-o', elf])
        self.run('3dsxtool', [sdk / 'tools/bin/3dsxtool', elf, out / 'mkw-3ds.3dsx', '--smdh=' + str(self.banner_work / 'production/icon.icn')])
        size = subprocess.run([str(sdk / 'devkitARM/bin/arm-none-eabi-size'), str(elf)], env=self.env, capture_output=True, text=True).stdout
        log('  ' + size.strip().replace('\n', '\n  '))

    # -- package ----------------------------------------------------------

    def package(self):
        root, out = self.root, self.output
        stage = self.work / 'package'
        stage.mkdir(parents=True, exist_ok=True)
        shutil.copy2(self.compile_dir / 'mkw-3ds.3dsx', root / 'build/mkw-3ds.3dsx')
        self.py('pause-controls', 'cook_pause_controls.py')
        self.py('pack-menu', 'pack_menu_assets.py')
        listing = json.loads(subprocess.run([self.python, 'tools/resource_manifest.py'], cwd=root, env=self.env,
                                            capture_output=True, text=True, check=True).stdout)
        from menu_asset_inputs import compiled_menu_textures
        absent = sorted(compiled_menu_textures(root) - listing.keys())
        if absent:
            raise BuildError('Compiled menu textures missing from the resource map: ' + ', '.join(absent[:10]))
        missing = [n for n, p in listing.items() if not Path(p).is_file()]
        if missing:
            raise BuildError('Resource map names missing files: ' + ', '.join(missing[:10]))
        write_json(stage / 'runtime-files.json', listing)
        menu = root / 'sd-card/3ds/mkw-3ds/menu.pak'
        self.py('pack-race', 'pack_race_assets.py', stage / 'runtime-files.json', stage / 'race-base.pak', '--exclude-menu', menu)
        self.py('pack-lod', 'pack_racer_lod.py', '--base', stage / 'race-base.pak', '--overlay', root / 'build/racer-lod', '--output', stage / 'race-lod.pak')
        self.py('pack-mii', 'pack_mii_racers.py', '--base', stage / 'race-lod.pak', '--overlay', root / 'build/mii-racers', '--output', stage / 'race-mii.pak')
        self.py('pack-unlit', 'pack_unlit_banks.py', stage / 'race-mii.pak', stage / 'race.pak')

        # Every file goes into a pack but the 3DSX; the game's fopen finds them
        # there (src/asset_pack.cpp openFile). Files neither pack holds yet:
        # streamed ones (movies, sound) into stream.pak, the Mii resources into
        # menu.pak, the rest (course archives, model lists, collision, the fire
        # bar stream) into race.pak.
        held = set(mpk_index(menu)) | set(mpk_index(stage / 'race.pak'))
        loose = {'menu': [], 'race': [], 'stream': []}
        for name, source in sorted(listing.items()):
            if name in held or name in ('menu.pak', 'race.pak', BUILT_3DSX):
                continue
            pack = 'stream' if name.startswith(('movies/', 'audio/')) else 'menu' if name.startswith('mii/') else 'race'
            loose[pack].append(f'{name}={source}')

        # The packs as MPK2 (tools/pakpack.c). menu.pak and race.pak: entries in
        # the order the game reads them (tools/pak-order/, from DIAG pack traces),
        # the rest by name stem, LZ4-compressed where that saves space
        # (lossless); race.pak drops from about 1.8 to 1 GB. stream.pak: stored
        # (its movies are LZ4 inside, its sound already compressed), each file
        # on a 512-byte SD sector, so the game's stream reads stay whole sectors.
        pakpack = stage / ('pakpack.exe' if os.name == 'nt' else 'pakpack')
        self.run('pakpack-host', [self.cxx, '-x', 'c', '-O2', '-o', pakpack, 'tools/pakpack.c'])
        (stage / 'mpk2').mkdir(exist_ok=True)
        archives = []
        for name in ('menu', 'race', 'stream'):
            (stage / f'{name}-loose.txt').write_text(''.join(line + '\n' for line in loose[name]), encoding='utf-8')
            archives.append(stage / 'mpk2' / f'{name}.pak')
            if name == 'stream':
                (stage / 'stream-order.txt').write_text('', encoding='utf-8')
                args = ['--stored', '--align', '512', '-']
            else:
                source = menu if name == 'menu' else stage / 'race.pak'
                order = subprocess.run([self.python, 'tools/pak_order.py', source, '0' if name == 'menu' else '1', f'tools/pak-order/{name}.txt'],
                                       cwd=root, env=self.env, capture_output=True, text=True, check=True).stdout
                (stage / f'{name}-order.txt').write_text(order, encoding='utf-8')
                args = [source]
            self.run('pakpack-' + name, [pakpack, *args, stage / f'{name}-order.txt', archives[-1], '@' + str(stage / f'{name}-loose.txt')])

        # SD folder: the three packs and the 3DSX.
        sd = out / 'sd/3ds/MKWiiDS'
        if (out / 'sd').exists():
            shutil.rmtree(out / 'sd')
        sd.mkdir(parents=True)
        packed = {}
        for archive in archives:
            shutil.copy2(archive, sd / archive.name)
            for name, (at, length) in mpk_index(archive).items():
                if name in packed:
                    raise BuildError(f'{name} is in two packs')
                packed[name] = archive.name
        unpacked = sorted(set(listing) - set(packed) - {'menu.pak', 'race.pak', BUILT_3DSX})
        if unpacked:
            raise BuildError('Files left out of the packs: ' + ', '.join(unpacked[:10]))
        shutil.copy2(listing[BUILT_3DSX], sd / GAME_3DSX)
        manifest = {name: {'storage': 'packed', 'archive': archive} for name, archive in packed.items()}
        for name in ('menu.pak', 'race.pak', 'stream.pak', GAME_3DSX):
            manifest[name] = {'storage': 'loose', 'bytes': (sd / name).stat().st_size, 'sha256': digest(sd / name)}
        write_json(out / 'sd-manifest.json', dict(sorted(manifest.items())))
        self.py('slim-check', 'slim_sd_models.py', sd)
        report = (self.logs / 'slim-check.log').read_text()
        if not report.startswith('0 packed duplicates'):
            raise BuildError('Loose copies of packed files remain in the SD folder: ' + report.splitlines()[0])
        shutil.copy2(self.compile_dir / 'mkw-3ds.3dsx', out / GAME_3DSX)
        (out / 'mkw-3ds.3dsx').unlink(missing_ok=True)  # the shipped name before 1.1.0
        shutil.copy2(self.compile_dir / 'mkw-3ds.elf', out / 'mkw-3ds.elf')
        for name in ('LICENSE', 'NOTICE'):
            shutil.copy2(REPO / name, out / name)
        shutil.copytree(REPO / 'licenses', out / 'licenses', dirs_exist_ok=True)
        self.cia()

    def cia(self):
        out, art = self.output, self.banner_work / 'production'
        version = (REPO / 'VERSION').read_text().strip()
        rsf = (REPO / 'tools/release/mkw-3ds.rsf').read_text()
        block = 'RomFs:\n  RootPath: $(APP_ROMFS)\n'
        if block not in rsf:
            raise BuildError('tools/release/mkw-3ds.rsf no longer has the expected RomFs block.')
        # External-SD title: an empty embedded RomFS would hide the SD resources.
        (self.work / 'package/external-sd.rsf').write_text(rsf.replace(block, ''))
        cia = out / f'MarioKartWiiDS-v{version}.cia'
        want = title_version()
        self.run('makerom', [self.tools['makerom'], '-f', 'cia', '-target', 't', '-exefslogo', '-elf', self.compile_dir / 'mkw-3ds.elf',
                             '-rsf', self.work / 'package/external-sd.rsf', '-icon', art / 'icon.icn', '-banner', art / 'home-banner.bnr',
                             '-ver', want, '-DAPP_REMASTER=0', '-o', cia])
        before = check_cia(cia, TITLE_ID, want)
        commit = subprocess.run(['git', '-C', str(REPO), 'rev-parse', 'HEAD'], capture_output=True, text=True).stdout.strip()
        dirty = bool(subprocess.run(['git', '-C', str(REPO), 'status', '--porcelain', '--untracked-files=no'], capture_output=True, text=True).stdout.strip())
        write_json(out / 'build-info.json', {
            'commit': commit + ('-dirty' if dirty else ''), 'version': version, 'title_id': TITLE_ID, 'title_version': want,
            'ticket_tmd_versions_before_fix': before, 'tmd_hashes_verified': True,
            'defines': ['MKW_RELEASE', 'KINOKO_QUIET'], 'cia_sha256': digest(cia), 'elf_sha256': digest(out / 'mkw-3ds.elf'),
            'banner_sha256': digest(art / 'home-banner.bnr'), 'disc_sha256': read_json(self.state_path)['disc_sha256']})
        self.cia_path = cia

    # -- after the converted data is deleted ------------------------------

    def delete_converted(self, state, key):
        """Frees the cooks' output once the packs are built. Compile needs only
        the generated headers in source/include, so a later code-only update
        still works; anything that changes game data does a full build."""
        for folder in (self.root / 'build', self.root / 'sd-card', self.root / 'data', self.root / 'assets',
                       self.work / 'package', self.work / 'checkpoints'):
            if folder.exists():
                shutil.rmtree(folder)
        state['converted_deleted'] = key
        write_json(self.state_path, state)
        log('Deleted the converted data.')

    def packs_present(self):
        return all((self.output / 'sd/3ds/MKWiiDS' / name).is_file() for name in ('menu.pak', 'race.pak', 'stream.pak'))

    def update_game(self):
        """The game's files after a code-only update; the packs are unchanged."""
        out, sd = self.output, self.output / 'sd/3ds/MKWiiDS'
        (self.work / 'package').mkdir(exist_ok=True)
        shutil.copy2(self.compile_dir / 'mkw-3ds.3dsx', sd / GAME_3DSX)
        manifest = read_json(out / 'sd-manifest.json')
        manifest[GAME_3DSX] = {'storage': 'loose', 'bytes': (sd / GAME_3DSX).stat().st_size, 'sha256': digest(sd / GAME_3DSX)}
        write_json(out / 'sd-manifest.json', manifest)
        shutil.copy2(self.compile_dir / 'mkw-3ds.3dsx', out / GAME_3DSX)
        (out / 'mkw-3ds.3dsx').unlink(missing_ok=True)  # the shipped name before 1.1.0
        shutil.copy2(self.compile_dir / 'mkw-3ds.elf', out / 'mkw-3ds.elf')
        for name in ('LICENSE', 'NOTICE'):
            shutil.copy2(REPO / name, out / name)
        shutil.copytree(REPO / 'licenses', out / 'licenses', dirs_exist_ok=True)
        self.cia()

    def report_done(self, began):
        if self.temp.parent != self.work:
            shutil.rmtree(self.temp, ignore_errors=True)
        log(f'\nDone (this run: {(time.monotonic() - began) / 60:.0f} min):\n'
            f'  CIA:        {self.cia_path}\n'
            f'  SD folder:  {self.output / "sd"}  (copy its 3ds folder to the SD card root)\n'
            f'  3DSX:       {self.output / GAME_3DSX}')

    # -- stage table ------------------------------------------------------

    def define_stages(self):
        stages, self.uses = [], {}
        R = lambda *paths: [self.root / p for p in paths]

        def add(name, action, outputs, args=None, files=()):
            # A stage is described by its arguments (tools) or its method's
            # source (everything else), plus files it reads that its code
            # doesn't name.
            stages.append((name, action, outputs))
            self.uses[name] = (args, list(files))

        def tool(name, args, *outputs, files=()):
            add(name, lambda: self.py(name, *args), R(*outputs), ['tools/' + args[0], *args[1:]], files)

        J, D = str(self.jobs), self.disc
        # The converters and pinned packages feed every later stage.
        add('converters', self.dependencies, R('build/export_brres.mjs', 'build/export_driver.mjs'),
            files=['tools/*.ts', 'tools/*.mjs', 'package.json', 'package-lock.json', 'dependencies.json', 'requirements.txt',
                   'patches/noclip-brres.patch'])
        add('extract', self.extract, R('data/extracted/disc-files.json', 'assets/common/kartParam.bin', 'build/original/main.dol',
                                       'assets/race-ui/Font/kart_kanji_font.brfnt'))
        tool('startline', ['generate_startline_policy.py'], 'tools/course_startline_faces.json')
        add('portraits', self.portraits, R('sd-card/3ds/mkw-3ds/mii/parts.mpr', 'build/mii-parts/retail-defaults/default-faces-atlas.png'))
        add('courses', self.courses, R('sd-card/3ds/mkw-3ds/models/course-catalog.json', 'include/mkw/course_catalog.hpp'))
        add('garage', self.garage, R('build/roster/models/garage.json', 'include/mkw/garage.hpp'))
        tool('objects', ['cook_course_objects.py', '--ids', *map(str, range(42)), '--jobs', J,
                         '--mii-heads', 'assets/mii/course-portraits/assignments.json'], 'build/course-visuals-receipt.json')
        for name, args, out in [
            ('lighting', ['cook_course_lighting.py'], 'include/mkw/course_lighting.hpp'),
            ('bloom', ['cook_course_bloom.py'], 'include/mkw/course_bloom.hpp'),
            ('phases', ['generate_course_phases.py'], 'include/mkw/course_phase_ranges.hpp'),
            ('roads', ['generate_road_policy.py'], 'include/mkw/road_sampler_ranges.hpp'),
            ('math', ['generate_runtime_tables.py', 'math'], 'include/mkw/nw4r_sin_table.hpp'),
            ('billboards', ['generate_runtime_tables.py', 'billboards'], 'include/mkw/course_mii_billboards.hpp'),
            ('gp-times', ['cook_gp_rank.py'], 'include/mkw/gp_rank_times.hpp'),
            ('driver-look', ['cook_driver_look.py'], 'include/mkw/driver_look_data.hpp'),
            ('menu-lighting', ['cook_menu_lighting.py'], 'include/mkw/menu_lighting.hpp'),
            ('menu-animation', ['cook_menu_animations.py'], 'include/mkw/menu_animations.hpp'),
            ('battle-menu', ['cook_battle_menu_text.py'], 'include/mkw/battle_menu_text.inc'),
            ('battle-hud', ['cook_battle_hud.py'], 'include/mkw/battle_hud_assets.hpp'),
            ('battle-motion', ['cook_battle_hud_motion.py'], 'include/mkw/battle_hud_motion_assets.hpp'),
            ('battle-scores', ['cook_battle_bottom_scores.py'], 'include/mkw/battle_bottom_scores.hpp'),
            ('battle-results', ['cook_battle_results.py'], 'include/mkw/battle_result_assets.hpp'),
            ('battle-screens', ['cook_battle_screens.py'], 'build/battle-screen-assets/receipt.json'),
            ('race-intro', ['cook_race_intro.py'], 'include/mkw/race_intro_layout.hpp'),
            ('mii-voice', ['cook_mii_voice.py', 'build/itembox-retail.rel'], 'include/mkw/mii_voice.hpp'),
            ('audio', ['cook_audio.py'], 'build/audio/audio/sound.arc'),
            ('race-items', ['cook_race_items.py', 'sd-card/3ds/mkw-3ds', '--disc', D], 'include/mkw/race_assets.hpp'),
            ('race-flow', ['cook_race_flow.py'], 'include/mkw/race_flow_layout.hpp'),
            ('race-balloon', ['cook_race_balloon.py'], 'include/mkw/race_balloon.hpp'),
            ('menu-mii', ['cook_menu_mii_bodies.py'], 'include/mkw/menu_mii_materials.hpp'),
            ('native-icon', ['generate_native_icon.py'], 'include/mkw/native_mii_icon_body.hpp'),
            ('effects', ['cook_race_effects.py', '--course-only', '--output', 'build/vfx/textures'], 'build/vfx/textures/course-effects.json'),
            ('lakitu', ['cook_lakitu.py'], 'include/mkw/lakitu_assets.hpp'),
            ('wiggler', ['cook_hanachan_joints.py'], 'include/mkw/hanachan_joint_assets.hpp'),
            ('pokey', ['cook_pokey_joints.py'], 'include/mkw/pokey_joint_assets.hpp'),
            ('object-audio', ['cook_object_sounds.py'], 'include/mkw/object_sound_assets.hpp'),
            ('battle-balloons', ['cook_battle_balloons.py'], 'build/battle-balloon-assets/receipt.json'),
            ('battle-coins', ['cook_battle_coins.py'], 'build/battle-coin-assets/receipt.json'),
            ('battle-objects', ['cook_battle_objects.py'], 'include/mkw/battle_map_assets.hpp'),
            ('balloon-effects', ['cook_race_effects.py', '--balloon-only', '--output', 'build/battle-balloon-effects/textures'],
             'build/battle-balloon-effects/textures/balloon-effects.json'),
            ('coin-effects', ['cook_race_effects.py', '--coin-only', '--output', 'build/battle-coin-effects/textures'],
             'build/battle-coin-effects/textures/coin-effects.json'),
            ('coin-hud', ['cook_coin_hud_stack.py'], 'build/battle-coin-hud-assets/receipt.json'),
            ('blast', ['cook_blast.py'], 'include/mkw/bomb_blast.hpp'),
            ('blooper', ['cook_blooper.py'], 'build/blooper/receipt.json'),
            ('lens-flare', ['cook_lens_flare.py'], 'build/lens-flare/receipt.json'),
            ('luma', ['cook_luma.py'], 'build/vfx/models/luma.gmdl'),
            ('coconut', ['cook_coconut_objects.py'], 'sd-card/3ds/mkw-3ds/models/coconut_escalator_000.gmdl'),
            ('firebar', ['prepare_firebar_stream.py'], 'build/course-water-feedback/textures/firebar-indirect.ftan'),
        ]:
            tool(name, args, out)
        tool('native-resource', ['stage_native_mii_resource.py', self.cfl or '--none'], 'build/native-mii-resource/receipt.json')
        add('star-colors', lambda: self.node('star-colors', 'build/export_star.mjs', 'assets/common/driver.brres', 'include/mkw/star_colors.hpp'),
            R('include/mkw/star_colors.hpp'), ['build/export_star.mjs', 'assets/common/driver.brres', 'include/mkw/star_colors.hpp'])
        tool('award-inputs', ['prepare_award_assets.py', D], 'build/award-source/award-sources.json')
        add('award-scenes', lambda: self.node('award-scenes', 'build/export_award_scene.mjs', 'build/award-source'),
            R('build/award-source/scene-bindings.json'), ['build/export_award_scene.mjs', 'build/award-source'])
        tool('award-cameras', ['cook_award_cameras.py'], 'include/mkw/award_camera_data.hpp')
        tool('award-drive', ['cook_award_drive.py'], 'include/mkw/award_drive_data.hpp')
        tool('award-lighting', ['cook_award_lighting.py'], 'include/mkw/award_lighting.hpp')
        tool('award-models', ['cook_award_models.py', '--jobs', J], 'build/award-models/award-models.json')
        tool('award-effects', ['cook_race_effects.py', '--award-source', 'build/award-source', '--output', 'build/vfx/textures'],
             'build/vfx/textures/award-driving-effects.bin')
        tool('staff-camera', ['cook_staff_camera.py'], 'include/mkw/staff_camera_data.hpp')
        tool('staff-crowd', ['generate_staff_crowd.py'], 'include/mkw/staff_crowd_data.hpp')
        tool('staff-ui', ['cook_staff_ui.py', '--retail-rel', 'build/itembox-retail.rel', '--output', 'build/staff'], 'include/mkw/staff_ui_data.hpp')
        # Course maps now; their UI atlas and header after menu-ui (map-ui).
        tool('maps', ['cook_race_map.py', '--jobs', J, '--metadata-only'], 'build/race-map/race-map.json')
        tool('map-objects', ['cook_map_objects.py'], 'include/mkw/map_object_assets.hpp')
        tool('menu-models', ['cook_menu_models.py', '--machines', '--jobs', J], 'include/mkw/menu_model_params.hpp')
        tool('runtime-audio', ['generate_runtime_tables.py', 'audio'], 'include/mkw/race_voice_tables.hpp')
        tool('menu-ui', ['cook_menu_ui.py'], 'include/mkw/menu_assets.hpp')
        # The lower screen's UI atlas: its standings rows take the menus' selected-button frame.
        tool('map-ui', ['cook_race_map.py', '--atlas-only'], 'include/mkw/race_map_assets.hpp', 'build/race-map/textures/race_bottom_ui_256.t3x')
        tool('menu-footer', ['cook_menu_footer.py'], 'include/mkw/menu_footer_text.inc')
        tool('menu-styles', ['cook_menu_styles.py'], 'include/mkw/menu_text_styles.hpp')
        tool('menu-record-text', ['cook_menu_record_text.py'], 'include/mkw/menu_record_text.inc')
        tool('cup-icons', ['cook_cup_record_icons.py'], 'include/mkw/cup_record_icons.hpp')
        tool('menu-movies', ['cook_menu_movies.py', '--jobs', J], 'sd-card/3ds/mkw-3ds/models/menu-movies.json', files=['tools/pakpack.c'])
        tool('menu-chapters', ['cook_menu_movie_chapters.py'], 'include/mkw/menu_movie_chapters.hpp')
        tool('menu-audio', ['cook_menu_audio.py'], 'include/mkw/menu_audio_assets.hpp')
        tool('staff-movies', ['cook_staff_movies.py', '--output', 'build/staff'], 'build/staff/movies/staff_true.gmv', 'build/staff/movies/staff_normal.gmv',
             files=['tools/pakpack.c'])
        tool('mii-racers', ['cook_mii_racers.py', D, 'build/mii-racers', '--jobs', J], 'build/mii-racers/models/mii-garage.json')
        tool('mii-finish', ['finish_mii_racers.py', D, 'build/mii-racers', '--jobs', J], 'build/mii-racers/models/mii-complete.json', 'include/mkw/mii_garage.hpp')
        tool('mii-awards', ['cook_mii_awards.py', 'build/mii-racers'], 'include/mkw/mii_award_materials.hpp')
        for name, args, out in [
            ('vehicle-rest', ['cook_vehicle_rest.py'], 'include/mkw/vehicle_rest_data.hpp'),
            ('item-shadows', ['cook_item_shadows.py'], 'build/item-shadows/models/item-shadows.json'),
            ('driver-effects', ['cook_driver_effects.py', '--jobs', J], 'build/driver-effects/receipt.json'),
            ('vehicle-shadows', ['cook_vehicle_shadows.py', '--jobs', J], 'build/vehicle-shadows/models/vehicle-shadows.json'),
            ('team-vehicles', ['cook_team_vehicles.py', D, '--jobs', J], 'build/team-vehicles/team-vehicles.json'),
            ('driver-lod', ['cook_racer_lod.py', '--output', 'build/racer-lod', '--work', 'build/racer-lod-cook'], 'build/racer-lod-cook/receipt.json'),
            ('vehicle-lod', ['cook_vehicle_lod.py', '--disc', D, '--output', 'build/racer-lod', '--work', 'build/vehicle-lod-cook'], 'build/vehicle-lod-cook/receipt.json'),
            ('water-mipmaps', ['cook_water_mipmaps.py'], 'build/water-mipmaps/receipt.json'),
            ('world-mipmaps', ['cook_world_mipmaps.py', '--jobs', J], 'build/world-mipmaps/receipt.json'),
            ('battle-feedback', ['cook_battle_course_feedback.py'], 'build/battle-course-feedback/receipt.json'),
        ]:
            tool(name, args, out)
        tool('drift', ['cook_effects.py', 'assets/common/Effect/RKRace.breft', 'sd-card/3ds/mkw-3ds/textures'], 'sd-card/3ds/mkw-3ds/textures/drift-effects.bin')
        tool('drift-bike', ['cook_effects.py', 'assets/common/Effect/RKRace.breft', 'sd-card/3ds/mkw-3ds/textures', '--bike'],
             'sd-card/3ds/mkw-3ds/textures/drift-effects-bike.bin')
        add('menu-fit', self.fit_menu, R('build/host-tools/sprite-scales.json', 'sd-card/3ds/mkw-3ds/models/menu-texture-fit.json'),
            files=self.MENU_SCALE_SOURCES)
        add('banner', self.banner, [self.banner_work / 'production/home-banner.bnr', self.banner_work / 'production/icon.icn'])
        # Always rerun: both are incremental and pick up source changes.
        add('compile', self.compile, None)
        add('package', self.package, None, files=['tools/pak-order/*.txt'])
        return stages

    def method_source(self, method, seen):
        """A stage method's source with the Builder methods it calls."""
        seen.add(method.__name__)
        text = textwrap.dedent(inspect.getsource(method))
        for called in re.findall(r'self\.(\w+)\(', text):
            if called not in seen and called not in ('run', 'py', 'node', 'checkpointed') and inspect.isfunction(getattr(Builder, called, None)):
                text += self.method_source(getattr(self, called), seen)
        return text

    def fingerprints(self, index=None):
        """Each cooking stage's fingerprint, chained with every earlier one's: a
        stage whose code or inputs changed runs again with everything after it,
        as --from would, since later stages read earlier stages' output."""
        index = index or SourceIndex(read_json(self.work / 'source-files.json', []))
        keys, chain = {}, ''
        for name, action, outputs in self.stages:
            if outputs is None and name != 'package':
                continue
            args, files = self.uses[name]
            used = set().union(*(index.glob(f) for f in files)) if files else set()
            if args is None:
                described = self.method_source(action, set())
                used |= index.python(described, 'tools')
            else:
                described, skip = [], False
                for arg in args:
                    if skip:
                        skip = False
                    elif arg == '--jobs':
                        skip = True
                    elif isinstance(arg, Path) and arg == self.disc:
                        described.append('<disc>')
                    elif isinstance(arg, Path) and arg == self.cfl:
                        described.append('<cfl> ' + digest(arg))
                    else:
                        described.append(str(arg))
                        used |= index.named(str(arg))
            h = hashlib.sha256(chain.encode())
            h.update(json.dumps([name, described, [(f, index.hashes[f]) for f in index.closure(used)]]).encode())
            chain = keys[name] = h.hexdigest()
        return keys

    def baseline(self):
        """For a work folder from before fingerprints: the fingerprints of the
        commit its last package was built from (build-info.json), so stages
        whose code changed since then cook again. None when that commit is
        unknown or had local changes."""
        commit = read_json(self.output / 'build-info.json', {}).get('commit', '')
        listing = subprocess.run(['git', '-C', str(REPO), 'ls-tree', '-r', '-z', '--name-only', commit],
                                 capture_output=True) if commit and not commit.endswith('-dirty') else None
        if not listing or listing.returncode:
            return None, commit
        with subprocess.Popen(['git', '-C', str(REPO), 'cat-file', '--batch'], stdin=subprocess.PIPE, stdout=subprocess.PIPE) as cat:
            def read(name):
                cat.stdin.write(f'{commit}:{name}\n'.encode())
                cat.stdin.flush()
                size = int(cat.stdout.readline().split()[2])
                data = cat.stdout.read(size)
                cat.stdout.read(1)
                return data
            return self.fingerprints(SourceIndex(listing.stdout.decode().split('\0')[:-1], read)), commit

    def main(self, start):
        self.prepare()
        state = read_json(self.state_path, {'stages': []})
        identity = disc_identity(self.disc, state.get('disc'))
        if state.get('disc') and state['disc']['sha256'] != identity['sha256']:
            raise BuildError(f'{self.work} was built from a different disc image; use a new --work directory.')
        state['disc'], state['disc_sha256'] = identity, identity['sha256']
        names = [s[0] for s in self.stages]
        if start:
            if start not in names:
                raise BuildError(f'Unknown stage {start}; see --list-stages.')
            again = names[names.index(start):]
            state['stages'] = [s for s in state['stages'] if s not in again]
            for kind in ('courses', 'garage'):
                if kind in again and (self.work / 'checkpoints' / kind).is_dir():
                    shutil.rmtree(self.work / 'checkpoints' / kind)
        keys, base = self.fingerprints(), None
        adopting = 'fingerprints' not in state
        if adopting:
            # A work folder from before fingerprints: its finished stages count
            # as cooked from the commit of its last package, or from this tree
            # when that is unknown.
            base, commit = self.baseline() if state['stages'] else (None, '')
            state['fingerprints'] = {s: (base or keys)[s] for s in state['stages'] if s in keys}
            if state['stages']:
                log(f'Recorded stage fingerprints as of {commit[:12]}' if base else 'Recorded stage fingerprints for the finished stages')
        recorded = state['fingerprints']
        write_json(self.state_path, state)
        began = time.monotonic()
        # package's fingerprint chains every cook's, so a match means the packs still hold.
        deleted = state.get('converted_deleted')
        if deleted and not start and deleted == keys['package'] and self.packs_present():
            log('[1/2] compile  (game data is unchanged, so the packs are kept)')
            self.compile()
            log('[2/2] package')
            self.update_game()
            self.report_done(began)
            return
        if deleted:
            log('Game data changed, so this is a full build.')
            del state['converted_deleted']
            write_json(self.state_path, state)
        for index, (name, action, outputs) in enumerate(self.stages, 1):
            done = name in state['stages'] and outputs is not None and all(p.exists() for p in outputs)
            if done and recorded.get(name) == keys[name]:
                continue
            # An unfinished or changed stage invalidates everything after it.
            state['stages'] = [s for s in state['stages'] if names.index(s) < index - 1]
            write_json(self.state_path, state)
            log(f'[{index}/{len(self.stages)}] {name}' + ('  (its code or inputs changed since it was cooked)' if done else ''))
            self.stage_key = keys.get(name, '')
            # Unstamped checkpoints predate fingerprints; they count only if
            # nothing they depend on changed since.
            self.adopt_checkpoints = adopting and (base is None or base.get(name) == keys.get(name))
            started = time.monotonic()
            action()
            absent = [str(p) for p in outputs or [] if not p.exists()]
            if absent:
                raise BuildError(f'{name} did not produce: ' + ', '.join(absent))
            log(f'      {name} done in {time.monotonic() - started:.0f} s')
            if outputs is not None:
                state['stages'].append(name)
                recorded[name] = keys[name]
                write_json(self.state_path, state)
        if self.delete_after:
            self.delete_converted(state, keys['package'])
        self.report_done(began)


def disc_identity(path, previous):
    stat = path.stat()
    if previous and previous.get('bytes') == stat.st_size and previous.get('mtime') == stat.st_mtime_ns:
        return previous
    log(f'Hashing {path.name}')
    return {'bytes': stat.st_size, 'mtime': stat.st_mtime_ns, 'sha256': digest(path)}


def mpk_index(path):
    """Entry name -> (offset, length) of an MPK1 pack (pack_menu_assets.py layout)
    or an MPK2 pack (tools/pakpack.c; length is the entry's unpacked size)."""
    with path.open('rb') as stream:
        magic, count, names_size = struct.unpack('<4sII', stream.read(12))
        if magic not in (b'MPK1', b'MPK2'):
            raise BuildError(f'{path} is not an MPK1 or MPK2 pack')
        width = 16 if magic == b'MPK2' else 12
        rows = [struct.unpack('<III', stream.read(width)[:12]) for _ in range(count)]
        names = stream.read(names_size)
    return {names[p:names.index(0, p)].decode(): (start, length) for p, start, length in rows}


def check_cia(cia, title, version):
    """Force the Ticket/TMD title version (some makerom builds collide with an
    internal VERSION macro, docs/MANUAL-BUILD.md) and verify every TMD hash.
    Returns the versions makerom wrote."""
    raw = bytearray(cia.read_bytes())
    align = lambda n: (n + 63) & ~63
    hs, _, _, cs, ts, ms, _, _ = struct.unpack_from('<IHHIIIIQ', raw)
    ticket = align(align(hs) + cs)
    tmd = align(ticket + ts)
    body = lambda o: o + {0x10000: 0x240, 0x10001: 0x140, 0x10002: 0x80, 0x10003: 0x240, 0x10004: 0x140, 0x10005: 0x80}[struct.unpack_from('>I', raw, o)[0]]
    tb, mb = body(ticket), body(tmd)
    found = raw[mb + 0x4c:mb + 0x54].hex().upper()
    if found != title:
        raise BuildError(f'CIA title {found}, expected {title}')
    before = [struct.unpack_from('>H', raw, tb + 0xa6)[0], struct.unpack_from('>H', raw, mb + 0x9c)[0]]
    if before != [version, version]:
        struct.pack_into('>H', raw, tb + 0xa6, version)
        struct.pack_into('>H', raw, mb + 0x9c, version)
        cia.write_bytes(raw)
    offset = align(tmd + ms)
    count = struct.unpack_from('>H', raw, mb + 0x9e)[0]
    records = mb + 0xc4 + 64 * 0x24
    info = raw[mb + 0xc4:records]
    ok = raw[offset + 0x100:offset + 0x104] == b'NCCH' and hashlib.sha256(info).digest() == raw[mb + 0xa4:mb + 0xc4] \
        and hashlib.sha256(raw[records:records + count * 0x30]).digest() == info[4:0x24]
    for i in range(count):
        rec = records + i * 0x30
        rtype, size = struct.unpack_from('>HQ', raw, rec + 6)
        ok = ok and not rtype & 1 and hashlib.sha256(raw[offset:offset + size]).digest() == raw[rec + 0x10:rec + 0x30]
        offset = align(offset + size)
    if not ok:
        raise BuildError(f'{cia}: TMD or content hash check failed')
    return before


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--disc', help='your Mario Kart Wii USA (RMCE01) ISO or WBFS')
    parser.add_argument('--cfl', help='optional CFL_Res.dat to package')
    parser.add_argument('--work', default=str(REPO / 'build/standard'), help='work directory (default: build/standard)')
    parser.add_argument('--jobs', type=int, default=min(4, os.cpu_count() or 4), help='parallel converter jobs (default: 4)')
    parser.add_argument('--from', dest='start', metavar='STAGE', help='run this stage and all later ones again')
    parser.add_argument('--delete-converted', action='store_true', help='delete the converted data afterwards (about 20 GB); code-only updates still work')
    parser.add_argument('--list-stages', action='store_true', help='print the stage order and exit')
    parser.add_argument('--worker', action='store_true', help=argparse.SUPPRESS)
    args = parser.parse_args()
    try:
        if args.list_stages:
            print('(setup: host tools, source copy, pinned converters, .venv; every run)')
            print('\n'.join(name for name, _, _ in Builder(args).stages))
            return 0
        if not args.disc:
            parser.error('--disc is required')
        if not args.worker:
            return bootstrap(args)
        Builder(args).main(args.start)
        return 0
    except BuildError as error:
        print(f'\nBuild stopped: {error}', file=sys.stderr, flush=True)
        return 1
    except subprocess.CalledProcessError as error:
        print(f'\nBuild stopped: {error}', file=sys.stderr, flush=True)
        return 1


if __name__ == '__main__':
    sys.exit(main())
