"""Shared paths for the explicit, external-output banner build stages."""
import os
from pathlib import Path
ROOT = Path(__file__).resolve().parents[3]
SCRIPTS = Path(__file__).resolve().parent
if not os.environ.get('MKW_BANNER_WORK'):
    raise SystemExit('Set MKW_BANNER_WORK to the extracted banner directory.')
WORK = Path(os.environ['MKW_BANNER_WORK']).resolve()
if WORK == ROOT or ROOT in WORK.parents:
    raise SystemExit('Banner output must be outside the publication repository.')
OUTPUT = WORK / 'production'
PYCGFX = Path(os.environ.get('MKW_PYCGFX_ROOT', '/missing-pycgfx')).resolve()
