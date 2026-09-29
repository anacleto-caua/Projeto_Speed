#!/usr/bin/env bash
# Builds a debug APK and copies it to ../out/AndroidSpeed-debug.apk.
#
# Debug-signed via Android's default debug keystore — fine for sideloading
# onto a phone (enable "install from unknown sources" on-device), not
# suitable for Play Store distribution (needs a real release keystore,
# not set up in this repo).
set -e
cd "$(dirname "$0")"

bash gradlew assembleDebug

mkdir -p ../out
cp app/build/outputs/apk/debug/app-debug.apk ../out/AndroidSpeed-debug.apk

echo ""
echo "Built: $(cd ../out && pwd)/AndroidSpeed-debug.apk"
