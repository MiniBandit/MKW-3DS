#!/usr/bin/env bash
set -Eeuo pipefail

shopt -s nullglob nocaseglob
discs=(mkw-rom/*.iso mkw-rom/*.wbfs)
shopt -u nullglob nocaseglob

if (( ${#discs[@]} == 0 )); then
    echo "Error: Put a Mario Kart Wii (USA) ISO or WBFS file inside the mkw-rom folder." >&2
    exit 1
fi

if (( ${#discs[@]} > 1 )); then
    echo "Error: mkw-rom contains multiple ISO/WBFS files." >&2
    exit 1
fi

disc="${discs[0]}"

if [[ ! -d vendor/gclib ]]; then
    python tools/bootstrap.py
fi

if [[ ! -x .venv/bin/python ]]; then
    python -m venv .venv
fi

if [[ ! -f .venv/mkw-dependencies-installed ]]; then
    .venv/bin/python -m pip install -r requirements.txt
    touch .venv/mkw-dependencies-installed
fi

export PYTHONUTF8=1 PYTHONUNBUFFERED=1

set +e
.venv/bin/python tools/release/build_cia.py "$disc" 2>&1 | tee build-cia-log.txt
status=${PIPESTATUS[0]}
set -e

if (( status != 0 )); then
    echo "Build failed. See build-cia-log.txt for details." >&2
    exit "$status"
fi

echo "Build completed successfully."
echo "Log: $PWD/build-cia-log.txt"
