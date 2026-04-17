#pragma once

#include <gtkmm.h>
#include <string>

namespace app::view {

/// Professional About dialog showing application metadata.
///
/// Triggered by F1 or a menu action. Displays version, build info,
/// supported languages, architecture summary, and license notice.
/// Uses Gtk::Window (not Gtk::AboutDialog) for full layout control.
class AboutDialog : public Gtk::Window {
public:
    explicit AboutDialog(Gtk::Window& parent);

private:
    void buildUI();

    /// Format build info string from compile-time macros.
    static std::string buildInfoText();

    /// List of supported language endonyms.
    static std::string languageList();
};

}  // namespace app::view
