#!/usr/bin/env bash
# ship.sh -- local verify gate, then a push the developer approves.
#
#   scripts/ship.sh            run the gate on the current branch, then ask to push
#   scripts/ship.sh --no-push  run the gate only
#
# The gate mirrors the CI jobs that block a merge: build, tests, clang-tidy on
# the changed sources, and the OpenFastTrace requirements gate. If any step
# fails the script stops and nothing is pushed. A change that touches no
# source, test or build file (docs, scripts, .github) skips build, tests and
# lint, but still traces. The gate builds in build/gate, its own directory, so
# a running HMI (for example the MCP server) never locks the files it links.
# The script never pushes to main and never pushes without a "y".

set -euo pipefail

NO_PUSH=0
[ "${1:-}" = "--no-push" ] && NO_PUSH=1

OFT_VERSION="4.1.0"
step() { printf '\n==> %s\n' "$1"; }
fail() { printf '\nSTOP: %s\nNothing was pushed.\n' "$1" >&2; exit 1; }

cd "$(git rev-parse --show-toplevel)"

# 0. Preconditions --------------------------------------------------------
branch="$(git rev-parse --abbrev-ref HEAD)"
[ "$branch" = "main" ] && fail "you are on main. Work on a branch and open a PR."
[ -n "$(git status --porcelain)" ] && fail "uncommitted changes. Commit or stash them first."

git fetch -q origin main || fail "cannot fetch origin/main."
base="$(git merge-base origin/main HEAD)"
changed="$(git diff --name-only "$base" HEAD)"
[ -z "$changed" ] && fail "no commits on $branch compared to main."

printf 'Branch: %s\nChanged files:\n%s\n' "$branch" "$changed"

docs_only=1
printf '%s\n' "$changed" | grep -qE '^(src/|tests/|benchmarks/|fuzzers/|cmake/|rust/|CMakeLists\.txt|CMakePresets\.json|vcpkg\.json)' && docs_only=0

if [ -n "${MSYSTEM:-}" ]; then
  preset="windows-msys2-release"; run_gui=""
else
  preset="release"; run_gui="$(command -v xvfb-run || true)"
fi
build_dir="build/gate"

if [ "$docs_only" -eq 1 ]; then
  echo "No source, test or build file changed: build, tests and lint are skipped."
else
  # 1. Build --------------------------------------------------------------
  step "1/4 Build ($preset)"
  cmake --preset "$preset" -B "$build_dir" -DBUILD_TESTS=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    || fail "configure failed."
  cmake --build "$build_dir" -j || fail "build failed."

  # 2. Tests --------------------------------------------------------------
  step "2/4 Tests"
  $run_gui ctest --test-dir "$build_dir" --output-on-failure --no-tests=error \
    || fail "tests failed."

  # 3. Lint ---------------------------------------------------------------
  step "3/4 clang-tidy"
  command -v clang-tidy >/dev/null || fail "clang-tidy is not installed."
  if printf '%s\n' "$changed" | grep -qE '^src/.*\.(h|hpp|inl)$'; then
    echo "A header changed: analysing the whole src/ tree."
    files="$(find src -name '*.cpp')"
  else
    files="$(printf '%s\n' "$changed" | grep -E '^src/.*\.cpp$' || true)"
  fi
  if [ -z "$files" ]; then
    echo "No source under src/ changed."
  else
    # shellcheck disable=SC2086
    clang-tidy -p "$build_dir" -quiet -header-filter='src/.*' $files \
      || fail "clang-tidy reported problems."
  fi
fi

# 4. Traceability ----------------------------------------------------------
step "4/4 Requirements traceability (OpenFastTrace $OFT_VERSION)"
# OpenFastTrace needs Java 17 or newer. Use the first one found: JAVA_HOME,
# the java on PATH, then JREs that other tools install (Temurin, STM32CubeIDE).
java_major() { "$1" -version 2>&1 | sed -n 's/.*version "\([0-9]*\)\.\{0,1\}\([0-9]*\).*/\1 \2/p' | head -1 |
  awk '{ print ($1 == 1 ? $2 : $1) }'; }
java_bin=""
for j in ${JAVA_HOME:+"$JAVA_HOME/bin/java"} "$(command -v java || true)" \
         /c/Program\ Files/Eclipse\ Adoptium/*/bin/java.exe \
         /c/Program\ Files/Java/*/bin/java.exe \
         /c/ST/STM32CubeIDE_*/STM32CubeIDE/plugins/*jre*/jre/bin/java.exe; do
  [ -n "$j" ] && [ -x "$j" ] || continue
  v="$(java_major "$j")"
  if [ -n "$v" ] && [ "$v" -ge 17 ]; then java_bin="$j"; break; fi
done
[ -n "$java_bin" ] || fail "OpenFastTrace needs Java 17 or newer and none was found.
Install one (winget install EclipseAdoptium.Temurin.17.JDK) or set JAVA_HOME."
echo "Java: $java_bin"
mkdir -p build
oft="build/oft-$OFT_VERSION.jar"
if [ ! -f "$oft" ]; then
  curl -fsSL -o "$oft" \
    "https://github.com/itsallcode/openfasttrace/releases/download/$OFT_VERSION/openfasttrace-$OFT_VERSION.jar" \
    || fail "cannot download OpenFastTrace."
fi
"$java_bin" -jar "$oft" trace docs/requirements tests src benchmarks fuzzers \
  || fail "a MUST/SHOULD requirement is not covered."

# Push ---------------------------------------------------------------------
printf '\nGate green. Commits that would be pushed:\n'
git log --oneline "origin/$branch..HEAD" 2>/dev/null || git log --oneline "$base..HEAD"

[ "$NO_PUSH" -eq 1 ] && { echo "--no-push: stopping here."; exit 0; }

printf '\nPush %s to origin? (y/n) ' "$branch"
read -r answer
[ "$answer" = "y" ] || { echo "Not pushed."; exit 0; }
git push -u origin "$branch"
