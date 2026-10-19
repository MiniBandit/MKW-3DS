#!/usr/bin/env bash
set -Eeuo pipefail

LOG_FILE="build-cia-log.txt"

shopt -s nullglob nocaseglob
roms=(mkw-rom/*.iso mkw-rom/*.wbfs)
shopt -u nullglob nocaseglob

if (( ${#roms[@]} == 0 )); then
    echo "Error: Put a Mario Kart Wii (USA) ISO or WBFS file inside the mkw-rom folder." >&2
    exit 1
fi

if (( ${#roms[@]} > 1 )); then
    echo "Error: mkw-rom contains multiple ISO/WBFS files." >&2
    exit 1
fi

rom="${roms[0]}"

if [[ ! -d vendor/gclib ]]; then
    python tools/bootstrap.py
fi

if [[ ! -x .venv/bin/python ]]; then
    python -m venv .venv
fi

if ! .venv/bin/python -c "import numpy, PIL, Crypto" >/dev/null 2>&1; then
    .venv/bin/python -m pip install -r requirements.txt
fi

export PYTHONUTF8=1 PYTHONUNBUFFERED=1

set +e
.venv/bin/python tools/release/build_cia.py "$rom" 2>&1 | tee "$LOG_FILE"
status=${PIPESTATUS[0]}
set -e

if (( status != 0 )); then
    echo "Build failed. See $LOG_FILE for details." >&2
    exit "$status"
fi

echo "Log: $LOG_FILE"
