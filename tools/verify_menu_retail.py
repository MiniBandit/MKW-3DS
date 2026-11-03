#!/usr/bin/env python3
"""Check menu tables in the user's original RMCU REL, without copying assets.

Addresses are from actual Ghidra MenuAudit, on the analysis image at 0x8050bf60.
These tables contain no relocated pointers and can be checked in original bytes.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

BASE = 0x8050bf60
EXPECTED_HASH = '1168107f8fdef27a356df76036db55afe4fbf7752606dbac991726701133a617'
TABLES = {
    'character_ids_by_weight': (0x808a3108, '24I', [6,12,8,13,1,14,4,5,0,7,10,17,16,18,15,20,11,2,19,23,9,22,3,21]),
    'vehicle_ids_by_weight': (0x808a7358, '36I', [i for w in range(3) for i in range(w,36,3)]),
    'single_player_modes': (0x808a7db0, '5I', [0,2,1,3,4]),
    'single_player_battle_types': (0x808a7dc4, '5I', [5,0,5,5,0]),
    'single_player_racer_counts': (0x808a7dd8, '5B', [12,1,12,12,1]),
    'single_player_next_pages': (0x808a7de0, '5I', [0x6a,0x6b,0x72,0x75,0x7a]),
    'hud_palette_default_players_and_inactive': (0x8088fd18, '72h', [
        255,255,0,255,210,170,0,255,210,170,0,70,
        255,255,0,255,210,170,0,255,210,170,0,70,
        0,111,255,255,19,229,255,255,19,229,255,70,
        255,0,0,255,255,79,255,255,255,79,255,70,
        0,186,0,255,76,255,130,255,76,255,130,70,
        255,255,255,210,170,170,170,255,0,0,0,70]),
}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('rel', type=Path)
    parser.add_argument('--receipt', type=Path)
    args = parser.parse_args()
    data = args.rel.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if digest != EXPECTED_HASH:
        raise ValueError('This address receipt is only valid for the audited RMCU StaticR.rel')
    checks = {}
    for name, (address, fmt, expected) in TABLES.items():
        actual = list(struct.unpack_from('>'+fmt, data, address-BASE))
        if actual != expected:
            raise AssertionError(f'{name}: {actual!r} != {expected!r}')
        checks[name] = {'address': hex(address), 'values': actual}
    receipt = {'source': str(args.rel), 'sha256': digest, 'tables': checks, 'passed': True}
    if args.receipt:
        args.receipt.parent.mkdir(parents=True, exist_ok=True)
        args.receipt.write_text(json.dumps(receipt, indent=2)+'\n')
    print(json.dumps(receipt, indent=2))

if __name__ == '__main__':
    main()
