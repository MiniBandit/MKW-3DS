#!/bin/bash
# Linux: double-click or run from terminal, then drag your Mario Kart Wii (USA) ISO or WBFS into the window.
# Works from the top of the builder folder or from tools/release.
cd "$(dirname "$0")" || exit 1
[ -f tools/release/build_cia.py ] || cd ../.. || exit 1

# Check for Python 3.11+
python3 -c 'import sys; sys.exit(sys.version_info < (3, 11))' 2>/dev/null || { echo "Python 3.11 or newer is needed: https://www.python.org/downloads/"; exit 1; }

# Check for Git
which git > /dev/null 2>&1 || { echo "Git is needed: https://git-scm.com/download/linux"; exit 1; }

# Check for Node.js
which node > /dev/null 2>&1 || { echo "Node.js LTS is needed: https://nodejs.org"; exit 1; }

# Prompt for disc image (read without -r undoes the backslashes terminal adds to a dragged path)
read -p "Drag your Mario Kart Wii (USA) disc image here and press Enter: " disc
disc="${disc%\"}";disc="${disc#\"}";disc="${disc%\'}";disc="${disc#\'}"
[ -f "$disc" ] || { echo "No disc image at: $disc"; exit 1; }

# Bootstrap vendor/gclib if needed
[ -d vendor/gclib ] || python3 tools/bootstrap.py || exit 1

# Create virtual environment if needed
[ -x .venv/bin/python ] || { python3 -m venv .venv && .venv/bin/pip install -r requirements.txt; } || exit 1

# Run the build and log output
PYTHONUNBUFFERED=1 .venv/bin/python tools/release/build_cia.py "$disc" 2>&1 | tee build-cia-log.txt
echo "Log: $(pwd)/build-cia-log.txt"
