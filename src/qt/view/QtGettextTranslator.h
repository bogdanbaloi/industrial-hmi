#pragma once

#include <QString>
#include <QTranslator>

class QObject;

namespace app::view {

/// A QTranslator that resolves Qt's own `translate()` -- the call behind both
/// `tr(...)` and the uic-generated `.ui` strings -- through the shared gettext
/// catalog (`src/core/i18n`). The Qt frontend therefore reuses the SAME
/// translations as the GTK and console frontends instead of maintaining a
/// parallel Qt `.qm` catalog.
///
/// Design: an Adapter (DIP). Qt's translation port is redirected onto the
/// project's toolkit-agnostic gettext module, so i18n stays a single shared
/// service rather than a per-frontend concern -- the same toolkit-independence
/// argument the presenters, integration layer and historian make. Install one
/// instance on the QApplication; a live language switch is a gettext rebind
/// (`ConfigManager::applyI18n`) followed by a reinstall, which makes Qt
/// broadcast `QEvent::LanguageChange` so every widget retranslates.
class QtGettextTranslator : public QTranslator {
public:
    explicit QtGettextTranslator(QObject* parent = nullptr);

    /// gettext keys on the source string, so the Qt context / disambiguation /
    /// plural count are ignored. Returns an empty QString when the catalog has
    /// no entry, the Qt convention that makes the caller fall back to the
    /// source text (keeping the UI readable in English for untranslated keys).
    [[nodiscard]] QString translate(const char* context, const char* sourceText,
                                    const char* disambiguation = nullptr,
                                    int n = -1) const override;
};

}  // namespace app::view
