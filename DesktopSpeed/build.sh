#!/usr/bin/env bash
# Builds a single-file DesktopSpeed executable for the current OS
# (Linux or macOS). Output: ../out/DesktopSpeed.
#
# For Windows, run build.bat instead on a Windows machine — PyInstaller
# builds for whatever OS it's run on, it doesn't cross-compile.
set -e
cd "$(dirname "$0")"

if [ ! -d .buildenv ]; then
    python3 -m venv .buildenv
fi

mkdir -p ../out
.buildenv/bin/pip install --quiet --upgrade pip pyinstaller
.buildenv/bin/pyinstaller --noconfirm --distpath ../out DesktopSpeed.spec

echo ""
echo "Built: $(cd ../out && pwd)/DesktopSpeed"
