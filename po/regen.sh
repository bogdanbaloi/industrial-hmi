#!/usr/bin/env bash
# Regenerate po/industrial-hmi.pot and merge it into every catalog.
#
# Run from the repo root. Needs GNU gettext (xgettext, msgmerge) and a Qt build
# tree so the generated ui_*.h exist. On Windows use WSL: the MSYS2 xgettext
# mishandles the Glade extraction, WSL's does not.
#
#   bash po/regen.sh [BUILD_DIR]   # BUILD_DIR defaults to build-qt
#
# Why three passes rather than one `xgettext -f POTFILES.in`:
#   * C++ frontends mark strings with _() / N_(); Qt marks them with tr().
#   * A `[type: gettext/glade]` line in a -f list switches xgettext into Glade
#     mode for that AND every following entry, so any C++ file after the .ui
#     block would be parsed as Glade and its strings dropped. We extract C++
#     and Glade separately instead.
#   * Qt .ui strings are not in source: uic emits them into ui_*.h as
#     translate(context, text); we take the text (arg 2), context-free, to
#     match QtGettextTranslator which ignores Qt's per-widget context.
set -euo pipefail

BUILD_DIR="${1:-build-qt}"
POT=po/industrial-hmi.pot
UI_HDRS="${BUILD_DIR}/objectsQt_autogen/include"

# 1) C++ sources (both frontends). Explicit list from POTFILES.in.
mapfile -t CPP < <(grep -vE '^[[:space:]]*#|^[[:space:]]*\[|^[[:space:]]*$' po/POTFILES.in | grep -E '\.(cpp|h)$')
xgettext --from-code=UTF-8 \
  --keyword=_ --keyword=N_ --keyword=tr:1 \
  --package-name=industrial-hmi --msgid-bugs-address=dev@example.com \
  -o "$POT" "${CPP[@]}"

# 2) GTK builder .ui (Glade). Needs valid XML (no `--` inside comments).
xgettext --from-code=UTF-8 --join-existing -L Glade \
  assets/ui/main-window.ui assets/ui/dashboard-page.ui \
  assets/ui/products-page.ui assets/ui/settings-page.ui \
  -o "$POT"

# 3) Qt .ui strings from the generated headers.
xgettext --from-code=UTF-8 --join-existing --keyword=translate:2 \
  -o "$POT" "$UI_HDRS"/ui_*.h

# 4) Merge into every catalog listed in LINGUAS.
while read -r lang; do
  case "$lang" in ''|\#*) continue;; esac
  msgmerge --quiet --update --backup=none "po/${lang}.po" "$POT"
  msgfmt -c -o /dev/null "po/${lang}.po"
done < po/LINGUAS

echo "Catalog regenerated: $(grep -c '^msgid ' "$POT") strings across $(grep -cvE '^#|^$' po/LINGUAS) languages."
