#!/bin/bash
# macOS: double-click, then drag your Mario Kart Wii (USA) ISO or WBFS into the window.
# Works from the top of the builder folder or from tools/release.
cd "$(dirname "$0")" || exit 1
[ -f tools/release/build_cia.py ] || cd ../.. || exit 1
PYTHON=python3
"$PYTHON" -c 'import sys; sys.exit(not ((3, 11) <= sys.version_info[:2] < (3, 15)))' 2>/dev/null || {
    for candidate in python3.14 python3.13 python3.12 python3.11; do
        if command -v "$candidate" >/dev/null 2>&1 && "$candidate" -c 'import sys; sys.exit(not ((3, 11) <= sys.version_info[:2] < (3, 15)))' 2>/dev/null; then
            PYTHON=$candidate
            break
        fi
    done
}
"$PYTHON" -c 'import sys; sys.exit(not ((3, 11) <= sys.version_info[:2] < (3, 15)))' 2>/dev/null || { echo "Python 3.11 through 3.14 is needed; Python 3.15 is not supported yet: https://www.python.org/downloads/"; exit 1; }
# read without -r undoes the backslashes Terminal adds to a dragged path ("\ ", "\(").
read -p "Drag your Mario Kart Wii (USA) disc image here and press Return: " disc
disc="${disc%\"}";disc="${disc#\"}";disc="${disc%\'}";disc="${disc#\'}"
[ -f "$disc" ] || { echo "No disc image at: $disc"; exit 1; }
[ -d vendor/gclib ] || "$PYTHON" tools/bootstrap.py || exit 1
[ -x .venv/bin/python ] || "$PYTHON" -m venv .venv || exit 1
.venv/bin/python -c 'import sys; sys.exit(not ((3, 11) <= sys.version_info[:2] < (3, 15)))' || { echo "The existing .venv uses an unsupported Python version. Remove .venv and rerun with Python 3.11 through 3.14."; exit 1; }
if [ ! -f .venv/mkw-dependencies-installed ]; then
    .venv/bin/python -m pip install -r requirements.txt || exit 1
    touch .venv/mkw-dependencies-installed
fi
PYTHONUNBUFFERED=1 .venv/bin/python tools/release/build_cia.py "$disc" 2>&1 | tee build-cia-log.txt
echo "Log: $(pwd)/build-cia-log.txt"
