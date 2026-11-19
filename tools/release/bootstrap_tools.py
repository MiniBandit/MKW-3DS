#!/usr/bin/env python3
"""Fetch the pinned CIA tools (makerom, bannertool) into .toolchain/release.

Every archive is checked against its SHA-256 in toolchain.lock.json before
anything is unpacked; a mismatch deletes the download and stops. Never
fetches game files.
"""
import hashlib, io, json, os, platform, shutil, stat, subprocess, sys, urllib.error, urllib.request, zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
LOCK=json.loads((Path(__file__).with_name('toolchain.lock.json')).read_text())
TOOLS=ROOT/'.toolchain/release'

def platform_key():
    system={'Darwin':'darwin','Linux':'linux','Windows':'windows'}.get(platform.system())
    machine=platform.machine().lower()
    machine={'amd64':'x86_64','x64':'x86_64','aarch64':'arm64'}.get(machine,machine)
    key=f'{system}-{machine}'
    # Windows on ARM (Snapdragon) runs x64 programs through its built-in
    # emulation: use the pinned x64 tools (#129).
    if key=='windows-arm64':key='windows-x86_64'
    if key not in LOCK['makerom']['platforms']:raise SystemExit(f'No pinned CIA tools for {key}')
    return key

def fetch(url,sha256):
    downloads=TOOLS/'downloads';downloads.mkdir(parents=True,exist_ok=True)
    path=downloads/url.rsplit('/',1)[1]
    if path.is_file() and hashlib.sha256(path.read_bytes()).hexdigest()==sha256:return path.read_bytes()
    print('downloading',url)
    try:
        with urllib.request.urlopen(url,timeout=120) as r:data=r.read()
    except urllib.error.URLError as error:
        # python.org's macOS Python trusts no certificates until its "Install
        # Certificates" step runs; curl uses the system's. The SHA-256 check
        # below verifies the download either way.
        if not shutil.which('curl'):raise
        print('  Python could not download it (%s); trying curl'%getattr(error,'reason',error))
        data=subprocess.run(['curl','-fsSL',url],check=True,capture_output=True,timeout=300).stdout
    if hashlib.sha256(data).hexdigest()!=sha256:raise SystemExit(f'checksum mismatch: {url}')
    path.write_bytes(data);return data

def install(data,member,dest):
    with zipfile.ZipFile(io.BytesIO(data)) as z:blob=z.read(member)
    dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(blob)
    dest.chmod(dest.stat().st_mode|stat.S_IXUSR|stat.S_IXGRP|stat.S_IXOTH)
    return dest

def tools():
    """Paths of verified makerom and bannertool, fetching them if needed."""
    key=platform_key();exe='.exe' if key.startswith('windows') else ''
    m=LOCK['makerom']['platforms'][key];b=LOCK['bannertool']
    makerom=TOOLS/key/('makerom'+exe);bannertool=TOOLS/key/('bannertool'+exe)
    receipt=TOOLS/key/'tools-verified.json'
    want={'makerom':m['sha256'],'bannertool':b['sha256']}
    if not(makerom.is_file() and bannertool.is_file() and receipt.is_file() and json.loads(receipt.read_text())==want):
        install(fetch(m['url'],m['sha256']),m['member'],makerom)
        install(fetch(b['url'],b['sha256']),b['members'][key],bannertool)
        receipt.write_text(json.dumps(want,indent=2)+'\n')
    return makerom,bannertool

if __name__=='__main__':
    for path in tools():print(path)
