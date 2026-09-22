#!/usr/bin/env bash
#
# readme-figures.sh - keep the figures README.md publishes equal to the tree.
#
#   scripts/readme-figures.sh --check <configured-build-dir>
#   scripts/readme-figures.sh --write <configured-build-dir>
#
# WHY THIS EXISTS
# The README publishes counts: ADRs, requirements, ctest targets, lines of C++
# code and files. They were edited by hand, and every feature PR that added an
# ADR, a requirement or a test left them stale. It happened three times in four
# days, and each time a downstream reader quoted the stale value. Refreshing by
# hand fixes one drift and waits for the next one. This script turns the drift
# into a red CI job instead.
#
# HOW IT WORKS
# Every published figure sits between two HTML comments, which GitHub does not
# render:
#
#     <!--fig:adrs-->31<!--/fig-->
#
# --check recomputes each figure from the tree and fails on any mismatch.
# --write rewrites the markers in place, for the PR that legitimately changes
# a figure. Run it, read the diff, commit it.
#
# It also fails when a figure is written WITHOUT a marker. A number outside a
# marker is a number nobody checks, so rewording a sentence must not be a way
# to walk out of the gate.
#
# WHY THE TEST COUNT NEEDS A CONFIGURED BUILD DIR
# Counting add_test lines in CMake is wrong in both directions: a foreach over
# the console scenarios turns one call into several, while option gates hide
# other calls from a default configure. Only a configure knows the real number,
# so the script asks ctest, never the CMake text.
#
# WHAT IT DOES NOT COVER
# The coverage percentage and the instrumented line count need a gcovr run,
# which belongs to the coverage job, not to this one.

set -euo pipefail

usage() {
    echo "usage: $0 --check|--write <configured-build-dir>" >&2
    exit 2
}

[ "$#" -eq 2 ] || usage
mode="$1"
build_dir="$2"
case "$mode" in
    --check|--write) ;;
    *) usage ;;
esac

root="$(cd "$(dirname "$0")/.." && pwd)"
readme="$root/README.md"

readonly MARKER_OPEN_PREFIX="<!--fig:"
readonly MARKER_CLOSE="<!--/fig-->"

[ -f "$build_dir/CTestTestfile.cmake" ] || {
    echo "readme-figures: $build_dir is not a configured build dir with tests" >&2
    exit 2
}
command -v cloc >/dev/null || {
    echo "readme-figures: cloc is required" >&2
    exit 2
}

# Insert thousands separators, so 28647 reads as 28,647 like the prose does.
with_commas() {
    printf '%s' "$1" | sed -e ':a' -e 's/\([0-9]\)\([0-9]\{3\}\)\($\|,\)/\1,\2\3/' -e 'ta'
}

# ---- what the tree actually holds -------------------------------------------
adrs="$(find "$root/docs/adr" -maxdepth 1 -name '[0-9]*.md' | wc -l | tr -d ' ')"
reqs="$(grep -oE 'req~[a-z0-9-]+-[0-9]+~[0-9]+' "$root/docs/requirements/REQUIREMENTS.md" \
        | sort -u | wc -l | tr -d ' ')"
ctest_targets="$(ctest --test-dir "$build_dir" -N | awk '/Total Tests:/ { print $3 }')"
read -r files loc < <(cloc --quiet --csv --include-lang=C++,"C/C++ Header" "$root/src" \
                        | awk -F, '$2 == "SUM" { print $1, $5 }')
loc="$(with_commas "$loc")"

# One table, so --check and --write can never disagree about the names.
declare -A want=(
    [adrs]="$adrs"
    [reqs]="$reqs"
    [ctest]="$ctest_targets"
    [loc]="$loc"
    [files]="$files"
)

if [ "$mode" = "--write" ]; then
    for name in "${!want[@]}"; do
        FIG_NAME="$name" FIG_VAL="${want[$name]}" perl -0pi -e \
            's/(\Q<!--fig:\E\Q$ENV{FIG_NAME}\E-->)[^<]*(\Q<!--\/fig-->\E)/$1$ENV{FIG_VAL}$2/g' \
            "$readme"
    done
    echo "readme-figures: markers rewritten, review the diff before committing"
    exit 0
fi

# ---- --check ------------------------------------------------------------------
fail=0
for name in adrs reqs ctest loc files; do
    open="${MARKER_OPEN_PREFIX}${name}-->"
    mapfile -t found < <(grep -oE "${open}[^<]*${MARKER_CLOSE}" "$readme" \
                         | sed -e "s|^${open}||" -e "s|${MARKER_CLOSE}\$||")
    if [ "${#found[@]}" -eq 0 ]; then
        echo "MISSING  fig:$name has no marker left in README.md"
        fail=1
        continue
    fi
    for value in "${found[@]}"; do
        if [ "$value" != "${want[$name]}" ]; then
            echo "STALE    fig:$name says $value, the tree has ${want[$name]}"
            fail=1
        fi
    done
done

# A figure written outside a marker escapes the check above, so look for the
# published phrases with a bare number once every marker has been stripped out.
stripped="$(sed -E 's/<!--fig:[a-z]+-->[^<]*<!--\/fig-->//g' "$readme")"
unmarked="$(printf '%s\n' "$stripped" | grep -nE \
    '[0-9][0-9,]*[[:space:]]+(ADRs|ctest targets|lines of C\+\+ code)|ADRs \+ [0-9]|across [0-9][0-9,]* files' \
    || true)"
if [ -n "$unmarked" ]; then
    echo "UNMARKED a published figure sits outside a marker:"
    printf '%s\n' "$unmarked" | sed 's/^/         /'
    fail=1
fi

if [ "$fail" -ne 0 ]; then
    echo
    echo "Fix: scripts/readme-figures.sh --write $build_dir, then review and commit."
    exit 1
fi
echo "readme-figures: all ${#want[@]} figures match the tree"
