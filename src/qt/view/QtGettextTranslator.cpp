#include "src/qt/view/QtGettextTranslator.h"

#include <libintl.h>

#include <cstring>

namespace app::view {

QtGettextTranslator::QtGettextTranslator(QObject* parent)
    : QTranslator(parent) {}

QString QtGettextTranslator::translate(const char* /*context*/,
                                       const char* sourceText,
                                       const char* /*disambiguation*/,
                                       int /*n*/) const {
    if (sourceText == nullptr || *sourceText == '\0') {
        return {};
    }
    // gettext() returns the source pointer unchanged when the active catalog has
    // no entry for it; signal "no translation" to Qt with an empty QString so it
    // falls back to sourceText.
    const char* translated = gettext(sourceText);
    if (translated == sourceText || std::strcmp(translated, sourceText) == 0) {
        return {};
    }
    return QString::fromUtf8(translated);
}

}  // namespace app::view
