#!/usr/bin/env bash
# Diff two `callgrind_annotate` top-N reports and list functions whose
# % column moved by more than a threshold. Output is a markdown table
# suitable for piping into a PR comment.
#
# Phase 2 of the profiling discipline (ADR-0016 / REQ-PERF-002). The
# committed baselines live alongside this script:
#
#   scripts/perf/baseline.callgrind-annotate.top50.txt
#   scripts/perf/baseline-alarm-storm.callgrind-annotate.top50.txt
#
# Usage:
#   ./scripts/perf/diff-baseline.sh \
#       scripts/perf/baseline.callgrind-annotate.top50.txt \
#       scripts/perf/output/callgrind-baseline-top50.txt
#
#   THRESHOLD=2.0 ./scripts/perf/diff-baseline.sh <old> <new>
#       # only flag functions whose % moved by >= 2.0 (default 1.0)
#
# Output:
#   markdown table with one row per function that moved past threshold,
#   sorted by absolute delta descending. The intended consumer is the
#   `profiling.yml` GitHub Actions workflow's PR-comment step (Phase
#   2.2 -- not wired yet but the script is ready).
#
# What this diff does NOT do:
#   - absolute Ir counts are ignored. They are not portable across
#     machines or workload tweaks; only the relative % share is.
#   - functions outside the top-N (whatever was committed) are not
#     compared. A function that drops out of top-N entirely is
#     reported as "<absent>"; the inverse is reported as "<new>".
#   - cross-process / multi-binary diffs are out of scope. Each
#     baseline is one binary, one workload.

set -euo pipefail

THRESHOLD="${THRESHOLD:-1.0}"

if [[ $# -ne 2 ]]; then
    echo "usage: $0 <old-annotate> <new-annotate>" >&2
    echo "  THRESHOLD=<pct> $0 ...   (default 1.0)" >&2
    exit 2
fi

OLD="$1"
NEW="$2"

if [[ ! -r "$OLD" || ! -r "$NEW" ]]; then
    echo "error: both inputs must be readable files" >&2
    exit 3
fi

# Extract "function -> percent" pairs from a callgrind_annotate file.
# Lines look like:
#   2,012,273 (18.85%)  ./elf/./elf/dl-lookup.c:do_lookup_x [...]
# We grab the function key (everything after the percent + spaces) and
# pair it with the percentage. The function key is normalised to its
# canonical "path:symbol" form so libc/ld.so paths still diff cleanly.
extract_pcts() {
    local file="$1"
    # callgrind_annotate prints percentages as either "(17.82%)" or
    # "( 6.83%)" -- the leading space pads single-digit percents for
    # column alignment. Allow optional whitespace inside the parens so
    # both shapes match.
    grep -E '^\s*[0-9,]+\s+\(' "$file" \
        | sed -E 's/^\s*[0-9,]+\s+\(\s*([0-9.]+)%\)\s+(.*)$/\1|\2/' \
        | awk -F'|' '
            NF == 2 {
                pct = $1
                # Strip "[...]" binary annotations from the function
                # so the same symbol matches across runs from different
                # build dirs.
                fn = $2
                gsub(/ \[[^]]*\]$/, "", fn)
                print fn "\t" pct
            }'
}

# Compute the diff. For every function in OLD or NEW, compare the
# percentages and emit one row when the absolute delta is at least
# THRESHOLD.
diff_pcts() {
    join -t $'\t' -a 1 -a 2 -e '<absent>' -o '0,1.2,2.2' \
        <(extract_pcts "$OLD" | sort -t $'\t' -k1,1) \
        <(extract_pcts "$NEW" | sort -t $'\t' -k1,1) \
        | awk -F'\t' -v thr="$THRESHOLD" '
            function abs(x) { return x < 0 ? -x : x }
            {
                fn = $1
                old = $2
                new = $3
                if (old == "<absent>") {
                    old_p = 0; new_p = new + 0
                    delta = new_p
                } else if (new == "<absent>") {
                    old_p = old + 0; new_p = 0
                    delta = -old_p
                } else {
                    old_p = old + 0; new_p = new + 0
                    delta = new_p - old_p
                }
                if (abs(delta) >= thr) {
                    printf "%.2f\t%.2f\t%+.2f\t%s\n",
                        old_p, new_p, delta, fn
                }
            }'
}

# Sort by absolute delta descending; that's the operator-meaningful
# ordering for "which functions moved most".
DIFF=$(diff_pcts | sort -t $'\t' -k3,3 -g -r)

if [[ -z "$DIFF" ]]; then
    echo "Callgrind profile is stable (no function moved by >= ${THRESHOLD}%)."
    exit 0
fi

cat <<EOF
| Old % | New % | Delta | Function |
|------:|------:|------:|----------|
EOF
# Re-sort by absolute delta (we want big jumps either direction at the
# top of the table). awk pipe above already prints +/-; here we sort by
# the absolute value to surface regressions and improvements equally.
echo "$DIFF" \
    | awk -F'\t' '{
        abs = $3 < 0 ? -$3 : $3
        printf "%s\t%s\n", abs, $0
      }' \
    | sort -g -r \
    | cut -f2- \
    | awk -F'\t' '{
        printf "| %s | %s | %s | `%s` |\n", $1, $2, $3, $4
      }'
