#!/bin/bash
# Flip auth + historian + multistation flags ON in the build's runtime
# config and ensure the data/ folder exists. Useful for local dev /
# demo runs of the native binary -- the source-tree config keeps these
# OFF by default so unit tests + the no-auth dev path aren't gated by
# a login prompt and the dashboard stays single-station; Docker uses
# its own config override.
#
# Run from the worktree root after `./build-windows.sh` or
# `cmake --build build/debug`. Re-run after any rebuild that
# refreshes config/ from the source tree.

set -e

BUILD_DIR="${1:-build/debug}"
CONFIG="$BUILD_DIR/config/app-config.json"

if [[ ! -f "$CONFIG" ]]; then
    echo "error: $CONFIG not found -- build first"
    exit 1
fi

sed -i \
    -e '/"historian":/,/}/ s/"enabled": false/"enabled": true/' \
    -e '/"auth":/,/}/ s/"enabled": false/"enabled": true/' \
    "$CONFIG"

# Multi-station: the key is opt-in and isn't present in the source
# config (unlike historian/auth which have explicit "enabled": false).
# Insert it inside the "ui" block on first run; idempotent thereafter.
if ! grep -q '"multistation_enabled"' "$CONFIG"; then
    sed -i 's/"ui": {/"ui": {\n    "multistation_enabled": true,/' "$CONFIG"
fi

mkdir -p "$BUILD_DIR/data"

echo "Auth + historian + multistation enabled in $CONFIG"
grep -A1 '"auth"\|"historian"\|"multistation_enabled"' "$CONFIG"
