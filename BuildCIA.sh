#!/usr/bin/env bash
# Linux: run ./build-cia.sh and drag your Mario Kart Wii (USA) ISO or WBFS into the
# terminal when prompted (or pass the path as the first argument).
# Needs devkitPro (3DS tools), Python 3.11+ (with venv), Node.js LTS and Git; see README.md.
set -uo pipefail
cd "$(dirname "$(readlink -f "$0")")" || exit 1

fail() { echo "$@" >&2; exit 1; }

nopython() {
    fail "Python 3.11 or newer is needed (with the venv module, e.g. 'sudo apt install python3 python3-venv')."
}

# --- Locate Python 3.11+ ---
PY=""
for cand in python3 python; do
if command -v "$cand" >/dev/null 2>&1; then
PY="$cand"
break
fi
done
[ -n "$PY" ] || nopython
"$PY" -c "import sys; sys.exit(sys.version_info < (3, 11))" || nopython

command -v git  >/dev/null 2>&1 || fail "Git is needed: install it with your package manager (e.g. 'sudo apt install git')."
command -v node >/dev/null 2>&1 || fail "Node.js LTS is needed: https://nodejs.org"

# --- Get the disc image path ---
disc="${1:-}"
if [ -z "$disc" ]; then
read -r -e -p "Drag your Mario Kart Wii (USA) disc image here and press Enter: " disc
fi
[ -n "$disc" ] || fail "No disc image given."

# Drag-and-drop into a terminal gives quoted or backslash-escaped paths; unwrap them.
if [ -z "${1:-}" ]; then
disc="$(printf '%s' "$disc" | "$PY" -c "import shlex,sys; p=shlex.split(sys.stdin.read()); print(p[0] if p else '')")"
fi
disc="${disc/#\~/$HOME}"
[ -f "$disc" ] || fail "No disc image at: $disc"

# --- Bootstrap vendor deps and virtualenv ---
if [ ! -d "vendor/gclib" ]; then
"$PY" tools/bootstrap.py || exit 1
fi
if [ ! -x ".venv/bin/python" ]; then
"$PY" -m venv .venv || exit 1
.venv/bin/python -m pip install -r requirements.txt || exit 1
fi

# --- Build, showing output and keeping a UTF-8 copy in build-cia-log.txt ---
export PYTHONUTF8=1
export PYTHONUNBUFFERED=1
export MKW_DISC="$disc"

.venv/bin/python tools/release/build_cia.py "$MKW_DISC" 2>&1 | tee build-cia-log.txt
status=${PIPESTATUS[0]}

echo "Log: $PWD/build-cia-log.txt"
exit "$status"