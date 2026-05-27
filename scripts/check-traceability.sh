#!/usr/bin/env bash
# Cross-checks REQ-IDs declared in REQUIREMENTS.md against the
# `// Implements: REQ-XXX` annotations sprinkled through the test
# suite. Reports two kinds of mismatch:
#
#   1. Orphan annotations -- a test claims to implement REQ-XXX
#      but REQUIREMENTS.md doesn't declare it. Usually a typo or
#      a stale REQ-ID survived a rename.
#
#   2. Uncovered MUST/SHOULD requirements -- REQUIREMENTS.md
#      declares the REQ-ID with priority MUST or SHOULD but no
#      test annotates it. Either the test is missing or the
#      annotation is missing.
#
# NICE-tier requirements are *not* flagged when uncovered (they
# may legitimately be manual-only).
#
# Exit code 0 on full match; 1 on any mismatch. Intended to run
# as part of CI once the test suite is fully annotated; meanwhile
# the script is informational -- pass `--warn-only` to downgrade
# any mismatch to a warning.
#
# Usage:
#   scripts/check-traceability.sh              # strict (CI)
#   scripts/check-traceability.sh --warn-only  # informational

set -euo pipefail

WARN_ONLY=0
if [[ "${1:-}" == "--warn-only" ]]; then
    WARN_ONLY=1
fi

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REQS_FILE="$ROOT/docs/requirements/REQUIREMENTS.md"
TESTS_DIR="$ROOT/tests"

if [[ ! -f "$REQS_FILE" ]]; then
    echo "error: $REQS_FILE not found" >&2
    exit 2
fi

# Extract declared REQ-IDs from REQUIREMENTS.md.
# Pattern: lines starting with "### REQ-<CATEGORY>-<NNN>"
DECLARED=$(grep -oE 'REQ-[A-Z]+-[0-9]+' "$REQS_FILE" | sort -u)

# Extract referenced REQ-IDs from test annotations.
# Pattern: "// Implements: REQ-XXX, REQ-YYY" or
#          "//             REQ-ZZZ"
REFERENCED=$(grep -rhoE '//.*REQ-[A-Z]+-[0-9]+' "$TESTS_DIR" 2>/dev/null \
             | grep -oE 'REQ-[A-Z]+-[0-9]+' \
             | sort -u || true)

# Extract priority for each declared REQ.
# We map each REQ-ID to MUST/SHOULD/NICE by reading the next
# "(MUST/SHOULD/NICE)" token after the heading line.
declare -A PRIORITIES
while IFS= read -r line; do
    if [[ "$line" =~ ^###[[:space:]]+(REQ-[A-Z]+-[0-9]+)[[:space:]]+\((MUST|SHOULD|NICE)\) ]]; then
        PRIORITIES["${BASH_REMATCH[1]}"]="${BASH_REMATCH[2]}"
    fi
done < "$REQS_FILE"

# 1. Orphans -- referenced but not declared.
ORPHANS=()
for ref in $REFERENCED; do
    if ! echo "$DECLARED" | grep -qx "$ref"; then
        ORPHANS+=("$ref")
    fi
done

# 2. Uncovered MUST/SHOULD -- declared with priority MUST or
#    SHOULD but no test annotates it.
UNCOVERED=()
for req in $DECLARED; do
    prio="${PRIORITIES[$req]:-}"
    if [[ "$prio" == "MUST" || "$prio" == "SHOULD" ]]; then
        if ! echo "$REFERENCED" | grep -qx "$req"; then
            UNCOVERED+=("$req ($prio)")
        fi
    fi
done

# Report.
EXIT=0
if (( ${#ORPHANS[@]} > 0 )); then
    echo ""
    echo "Traceability: orphan annotations (test references a REQ-ID"
    echo "not declared in REQUIREMENTS.md):"
    for o in "${ORPHANS[@]}"; do
        echo "  - $o"
    done
    EXIT=1
fi

if (( ${#UNCOVERED[@]} > 0 )); then
    echo ""
    echo "Traceability: MUST/SHOULD requirements with no test annotation:"
    for u in "${UNCOVERED[@]}"; do
        echo "  - $u"
    done
    EXIT=1
fi

if (( EXIT == 0 )); then
    echo "Traceability: all REQ-IDs reconcile."
    echo "  declared: $(echo "$DECLARED" | wc -l)"
    echo "  referenced in tests: $(echo "$REFERENCED" | wc -l)"
    exit 0
fi

if (( WARN_ONLY == 1 )); then
    echo ""
    echo "(--warn-only: exiting 0 despite mismatches above)"
    exit 0
fi
exit "$EXIT"
