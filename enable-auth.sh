#!/bin/bash
# Flip auth + historian flags ON in the build's runtime config and
# ensure the data/ folder exists. Useful for local dev / demo runs
# of the native binary -- the source-tree config keeps these OFF by
# default so unit tests + the no-auth dev path aren't gated by a
# login prompt; Docker uses its own config override.
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

mkdir -p "$BUILD_DIR/data"

echo "Auth + historian enabled in $CONFIG"
grep -A1 '"auth"\|"historian"' "$CONFIG"
