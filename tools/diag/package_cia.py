"""Package the diagnostic ELF (tools/diag/compile.py) as an external-SD CIA.

    python tools/diag/package_cia.py

build/diag/mkw-3ds-diag.elf -> build/diag/MarioKartWiiDS-v<VERSION>-DIAG.cia,
title 000400000F4B5D00 "MKW3DSDIAG". It installs beside the release title and
shares its SD data.

The banner is the standard build's (tools/build.py; MKW_WORK, default
build/standard) home-banner/production/home-banner.bnr unless
MKW_BANNER names another. makerom comes from MAKEROM, PATH or the standard build.
Same makerom invocation and Ticket/TMD checks as the release package: the
title version is forced to 142 and every content hash is verified.
"""
from pathlib import Path
import hashlib, json, os, shutil, struct, subprocess, sys

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
# MKW_DIAG_VARIANT=o3dstest (compile.py): the Old 3DS test build for a New 3DS,
# title 000400000F4B5E00 "Mario Kart WiiDS Old 3DS" (HOME Menu) from package/diag-o3ds-test.rsf, CIA ...-Old3DS.cia.
VARIANT = os.environ.get('MKW_DIAG_VARIANT', '')
OUT = ROOT / ('build/diag-' + VARIANT if VARIANT else 'build/diag')
ELF = OUT / 'mkw-3ds-diag.elf'
TITLE, RSF, SUFFIX = {'': ('000400000F4B5D00', 'diag-external-sd.rsf', 'DIAG'),
                      'o3dstest': ('000400000F4B5E00', 'diag-o3ds-test.rsf', 'Old3DS'),
                      'o3dsrelease': ('000400000F4B5E00', 'diag-o3ds-test.rsf', 'Old3DS-release')}[VARIANT]
WORK = Path(os.environ.get('MKW_WORK', ROOT / 'build/standard'))
BANNER = Path(os.environ.get('MKW_BANNER', WORK / 'home-banner/production/home-banner.bnr'))
# tools/build.py records the makerom it found or downloaded.
MAKEROM = (os.environ.get('MAKEROM') or shutil.which('makerom')
           or ((WORK / 'tools.json').is_file() and json.loads((WORK / 'tools.json').read_text()).get('makerom')) or 'makerom')
VERSION = (ROOT / 'VERSION').read_text().strip()
TITLE_VERSION = 142


def digest(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(4 << 20), b''):
            h.update(block)
    return h.hexdigest()


def main():
    if not ELF.is_file():
        raise SystemExit(f'{ELF} missing: run tools/diag/compile.py first.')
    if not BANNER.is_file():
        raise SystemExit(f'{BANNER} missing: run tools/build.py first or set MKW_BANNER.')
    cia = OUT / f'MarioKartWiiDS-v{VERSION}-{SUFFIX}.cia'
    icon = HERE / 'package/icon-diag.icn'
    if VARIANT in ('o3dstest', 'o3dsrelease'):
        # The Old 3DS build shows on the HOME Menu as "Mario Kart WiiDS Old 3DS" with
        # the game's own icon (the standard build's icon48/24), beside the game.
        art = WORK / 'home-banner/production'
        tools = json.loads((WORK / 'tools.json').read_text()) if (WORK / 'tools.json').is_file() else {}
        icon = OUT / 'icon-old3ds.icn'
        subprocess.run([os.environ.get('BANNERTOOL') or tools.get('bannertool') or 'bannertool', 'makesmdh',
                        '-s', 'Mario Kart WiiDS Old 3DS', '-l', 'Mario Kart WiiDS Old 3DS', '-p', 'Redux',
                        '-i', str(art / 'icon48.png'), '-si', str(art / 'icon24.png'), '-o', str(icon)], check=True)
    subprocess.run([MAKEROM, '-f', 'cia', '-target', 't', '-exefslogo', '-elf', str(ELF),
                    '-rsf', str(HERE / 'package' / RSF), '-icon', str(icon),
                    '-banner', str(BANNER), '-ver', str(TITLE_VERSION), '-DAPP_REMASTER=0', '-o', str(cia)],
                   check=True)
    raw = bytearray(cia.read_bytes())
    align = lambda n: (n + 63) & ~63
    hs, typ, ver, cs, ts, ms, meta, content = struct.unpack_from('<IHHIIIIQ', raw)
    ticket = align(align(hs) + cs)
    tmd = align(ticket + ts)
    body = lambda o: o + {0x10000: 0x240, 0x10001: 0x140, 0x10002: 0x80, 0x10003: 0x240, 0x10004: 0x140, 0x10005: 0x80}[struct.unpack_from('>I', raw, o)[0]]
    tb, mb = body(ticket), body(tmd)
    title = raw[mb + 0x4c:mb + 0x54].hex().upper()
    assert title == TITLE, title
    before = [struct.unpack_from('>H', raw, tb + 0xa6)[0], struct.unpack_from('>H', raw, mb + 0x9c)[0]]
    # Some makerom builds collide with an internal VERSION macro (see docs/MANUAL-BUILD.md).
    if before != [TITLE_VERSION, TITLE_VERSION]:
        struct.pack_into('>H', raw, tb + 0xa6, TITLE_VERSION)
        struct.pack_into('>H', raw, mb + 0x9c, TITLE_VERSION)
        cia.write_bytes(raw)
    offset = align(tmd + ms)
    assert raw[offset + 0x100:offset + 0x104] == b'NCCH'
    # TMD: content info records (64 x 0x24) hash the content chunk records.
    count = struct.unpack_from('>H', raw, mb + 0x9e)[0]
    records = mb + 0xc4 + 64 * 0x24
    info = raw[mb + 0xc4:records]
    assert hashlib.sha256(info).digest() == raw[mb + 0xa4:mb + 0xc4], 'TMD info record hash'
    assert hashlib.sha256(raw[records:records + count * 0x30]).digest() == info[4:0x24], 'TMD chunk record hash'
    for i in range(count):
        rec = records + i * 0x30
        rtype, size = struct.unpack_from('>HQ', raw, rec + 6)
        assert not rtype & 1, 'encrypted content'
        assert hashlib.sha256(raw[offset:offset + size]).digest() == raw[rec + 0x10:rec + 0x30], f'content {i} hash'
        offset = align(offset + size)
    commit = subprocess.run(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], capture_output=True, text=True).stdout.strip()
    dirty = bool(subprocess.run(['git', '-C', str(ROOT), 'status', '--porcelain', '--untracked-files=no'],
                                capture_output=True, text=True).stdout.strip())
    report = {'commit': commit + ('-dirty' if dirty else ''), 'version': VERSION, 'title_id': title,
              'defines': ['MKW_AUTO_PROFILE', 'KINOKO_QUIET', '(MKW_RELEASE omitted)'],
              'ticket_tmd_versions_before_fix': before, 'tmd_hashes_verified': True,
              'cia_bytes': cia.stat().st_size, 'cia_sha256': digest(cia), 'elf_sha256': digest(ELF),
              'banner_sha256': digest(BANNER)}
    (OUT / 'build-info.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
