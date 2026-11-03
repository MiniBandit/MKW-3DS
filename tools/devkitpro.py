"""Locate devkitPro and its tools on macOS, Linux and Windows."""
import os, re
from pathlib import Path

EXE = '.exe' if os.name == 'nt' else ''


def root():
    env = os.environ.get('DEVKITPRO', '')
    if os.name == 'nt':
        # The Windows installer sets DEVKITPRO to an MSYS path (/opt/devkitpro);
        # its files live in C:\devkitPro unless DEVKITPRO_WIN says otherwise.
        drive = re.match(r'^/([a-zA-Z])/(.*)$', env)
        if drive:
            return Path(f'{drive[1]}:/{drive[2]}')
        if env and not env.startswith('/'):
            return Path(env)
        return Path(os.environ.get('DEVKITPRO_WIN', 'C:/devkitPro'))
    return Path(env or '/opt/devkitpro')


def tool(name):
    """A devkitPro host tool: tex3ds, picasso, 3dsxtool, smdhtool."""
    return root() / 'tools/bin' / (name + EXE)


def arm(name):
    """A devkitARM binary, e.g. arm-none-eabi-g++."""
    return root() / 'devkitARM/bin' / (name + EXE)
