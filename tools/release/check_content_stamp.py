#!/usr/bin/env python3
"""Fails when the cook recipe differs from the one last reviewed against the
content stamp (tools/release/content_stamp.txt). Run after editing a cook tool
or a packaging script."""
import sys
from pathlib import Path
sys.path[:0]=[str(Path(__file__).resolve().parents[2]/'tools'),str(Path(__file__).parent)]
from build_cia import check_content_stamp,content_fingerprint
ok,message=check_content_stamp()
print(f'content stamp {content_fingerprint()}: '+('recipe reviewed' if ok else message))
sys.exit(0 if ok else 1)
