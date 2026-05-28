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
#   --multistation     Enable multi-station mode (two dashboards side-by-side)
#   --auth             Enable auth (login dialog, RBAC, Users tab, audit log)
#   --historian        Enable historian (SQLite time-series + History tab)
#   --modbus           Enable Modbus TCP master (default off)
#   --all              Shortcut: --multistation --auth --historian --modbus
#   --no-run           Build only, do not launch
#   --clean            Wipe build/release before configure
#   --tests            Configure with BUILD_TESTS=ON and run ctest after build

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build/release"
PRESET="windows-msys2-release"

WANT_MULTISTATION=0
WANT_AUTH=0
WANT_HISTORIAN=0
WANT_MODBUS=0
WANT_RUN=1
WANT_CLEAN=0
WANT_TESTS=0
for arg in "$@"; do
    case "$arg" in
        --multistation) WANT_MULTISTATION=1 ;;
        --auth)         WANT_AUTH=1 ;;
        --historian)    WANT_HISTORIAN=1 ;;
        --modbus)       WANT_MODBUS=1 ;;
        --all)
            WANT_MULTISTATION=1
            WANT_AUTH=1
            WANT_HISTORIAN=1
            WANT_MODBUS=1
            ;;
        --no-run)       WANT_RUN=0 ;;
        --clean)        WANT_CLEAN=1 ;;
        --tests)        WANT_TESTS=1 ;;
        *) echo "unknown flag: $arg"; exit 2 ;;
    esac
done

cd "${REPO_ROOT}"

if [[ ${WANT_CLEAN} -eq 1 ]]; then
    echo "Wiping ${BUILD_DIR}"
    rm -rf "${BUILD_DIR}"
fi

if [[ ! -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
    echo "Configuring (preset: ${PRESET})"
    EXTRA=()
    [[ ${WANT_TESTS} -eq 1 ]] && EXTRA+=("-DBUILD_TESTS=ON")
    cmake --preset "${PRESET}" "${EXTRA[@]}"
fi

echo "Building"
# Ninja defaults to one job per core when -j is omitted, which is
# exactly what we want; passing "-- -j" without a number confuses
# it (it interprets bare -j as a help request).
cmake --build "${BUILD_DIR}"

if [[ ${WANT_TESTS} -eq 1 ]]; then
    echo "Running tests"
    (cd "${BUILD_DIR}" && ctest --output-on-failure --no-tests=error)
fi

CONFIG_FILE="${BUILD_DIR}/config/app-config.json"

# JSON config flipper -- uses Perl + JSON::PP to walk the tree
# instead of sed (which would mis-target one of the three different
# "enabled" keys nested under auth / historian / network.modbus).
#
# Usage: patch_json '<path.to.key>' '<json-literal-value>'
# Examples:
#   patch_json 'ui.multistation_enabled' '"true"'   # string flag
#   patch_json 'auth.enabled'            'true'     # JSON bool
patch_json() {
    local path="$1"
    local value="$2"
    perl -i -e '
        use strict; use warnings; use JSON::PP;
        # JSON::PP emits UTF-8 -- tell Perl so the print is silent
        # instead of warning "Wide character in print".
        binmode(STDOUT, ":utf8");
        my ($file, $path, $value) = @ARGV;
        local $/;
        open(my $fh, "<", $file) or die "open $file: $!";
        my $json_text = <$fh>; close($fh);
        my $data = decode_json($json_text);
        my @parts = split(/\./, $path);
        my $leaf = pop @parts;
        my $cur = $data;
        for my $p (@parts) {
            $cur->{$p} //= {};
            $cur = $cur->{$p};
        }
        # Parse value as JSON so "true" string vs true bool both work.
        $cur->{$leaf} = decode_json($value);
        open($fh, ">:utf8", $file) or die "write $file: $!";
        print $fh JSON::PP->new->pretty->canonical->encode($data);
        close($fh);
    ' "${CONFIG_FILE}" "${path}" "${value}"
}

if [[ ! -f "${CONFIG_FILE}" ]]; then
    echo "!!! ${CONFIG_FILE} not found -- skipping config flips"
else
    if [[ ${WANT_MULTISTATION} -eq 1 ]]; then
        echo "Enabling multistation"
        patch_json 'ui.multistation_enabled' '"true"'
    fi
    if [[ ${WANT_AUTH} -eq 1 ]]; then
        echo "Enabling auth (login + Users tab + audit log)"
        patch_json 'auth.enabled' 'true'
    fi
    if [[ ${WANT_HISTORIAN} -eq 1 ]]; then
        echo "Enabling historian (SQLite store + History tab)"
        patch_json 'historian.enabled' 'true'
    fi
    if [[ ${WANT_MODBUS} -eq 1 ]]; then
        echo "Enabling Modbus TCP master"
        patch_json 'network.modbus.enabled' 'true'
    fi
fi

if [[ ${WANT_RUN} -eq 0 ]]; then
    exit 0
fi

# The app opens SQLite files under data/ (auth.sqlite, historian.sqlite)
# and writes logs/app.log -- both relative to the working directory.
# sqlite3_open does NOT create missing parent dirs, so without these
# the --auth / --historian features silently disable at runtime with
# "unable to open database file". Create them up front.
mkdir -p "${BUILD_DIR}/data" "${BUILD_DIR}/logs"

echo "Launching ${BUILD_DIR}/industrial-hmi.exe"
cd "${BUILD_DIR}"
exec ./industrial-hmi.exe
