#!/usr/bin/env bash
#
# Build + run the Industrial HMI on Windows MSYS2 CLANG64.
# Run from MSYS2 CLANG64 shell, OR from any shell via:
#
#   "C:/msys64/usr/bin/bash.exe" -lc 'export MSYSTEM=CLANG64 && source /etc/profile && cd "/c/path/to/repo" && scripts/win-build-run.sh [args]'
#
# Build dir is always build/release (the windows-msys2-release preset).
#
# Args:
#   --multistation   Override config to enable multi-station mode before launch
#   --no-run         Build only, do not launch
#   --clean          Wipe build/release before configure
#   --tests          Configure with BUILD_TESTS=ON and run ctest after build

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build/release"
PRESET="windows-msys2-release"

WANT_MULTISTATION=0
WANT_RUN=1
WANT_CLEAN=0
WANT_TESTS=0
for arg in "$@"; do
    case "$arg" in
        --multistation) WANT_MULTISTATION=1 ;;
        --no-run)       WANT_RUN=0 ;;
        --clean)        WANT_CLEAN=1 ;;
        --tests)        WANT_TESTS=1 ;;
        *) echo "unknown flag: $arg"; exit 2 ;;
    esac
done

cd "${REPO_ROOT}"

if [[ ${WANT_CLEAN} -eq 1 ]]; then
    echo ">>> wiping ${BUILD_DIR}"
    rm -rf "${BUILD_DIR}"
fi

if [[ ! -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
    echo ">>> configuring (preset: ${PRESET})"
    EXTRA=()
    [[ ${WANT_TESTS} -eq 1 ]] && EXTRA+=("-DBUILD_TESTS=ON")
    cmake --preset "${PRESET}" "${EXTRA[@]}"
fi

echo ">>> building"
# Ninja defaults to one job per core when -j is omitted, which is
# exactly what we want; passing "-- -j" without a number confuses
# it (it interprets bare -j as a help request).
cmake --build "${BUILD_DIR}"

if [[ ${WANT_TESTS} -eq 1 ]]; then
    echo ">>> running tests"
    (cd "${BUILD_DIR}" && ctest --output-on-failure --no-tests=error)
fi

CONFIG_FILE="${BUILD_DIR}/config/app-config.json"
if [[ ${WANT_MULTISTATION} -eq 1 ]]; then
    if [[ ! -f "${CONFIG_FILE}" ]]; then
        echo "!!! ${CONFIG_FILE} not found -- launching without multistation"
    else
        echo ">>> enabling multistation in ${CONFIG_FILE}"
        if grep -q '"multistation_enabled"' "${CONFIG_FILE}"; then
            # Key exists, flip to true.
            sed -i 's/"multistation_enabled"[[:space:]]*:[[:space:]]*"[^"]*"/"multistation_enabled": "true"/' "${CONFIG_FILE}"
        else
            # Key missing -- inject it as a new line after the "ui": {
            # opening brace. The existing "ui" block holds "palette"
            # so the inserted line keeps valid JSON shape.
            sed -i '/"ui"[[:space:]]*:[[:space:]]*{/a\    "multistation_enabled": "true",' "${CONFIG_FILE}"
        fi
        grep -n "multistation_enabled" "${CONFIG_FILE}" | head -1
    fi
fi

if [[ ${WANT_RUN} -eq 0 ]]; then
    exit 0
fi

echo ">>> launching ${BUILD_DIR}/industrial-hmi.exe"
cd "${BUILD_DIR}"
exec ./industrial-hmi.exe
