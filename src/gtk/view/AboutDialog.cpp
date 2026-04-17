#include "AboutDialog.h"
#include "src/core/i18n.h"
#include "src/config/config_defaults.h"
#include "src/gtk/view/ui_sizes.h"

namespace app::view {

namespace {
using namespace app::view::sizes;
constexpr int kContentSpacing = kSpacingMedium + kSpacingTiny;   // 16
constexpr int kFrameMargin    = kSpacingXL;                      // 24
}  // namespace

AboutDialog::AboutDialog(Gtk::Window& parent) {
    set_title(_("About"));
    set_transient_for(parent);
    set_modal(true);
    set_default_size(kAboutDialogWidth, kAboutDialogHeight);
    set_resizable(false);

    buildUI();
}

void AboutDialog::buildUI() {
    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, kContentSpacing);
    box->set_margin(kFrameMargin);

    // App name
    auto* titleLabel = Gtk::make_managed<Gtk::Label>();
    titleLabel->set_markup("<b><big>[BB] Industrial HMI</big></b>");
    titleLabel->set_margin_bottom(kSpacingTiny);
    box->append(*titleLabel);

    // Version
    auto* versionLabel = Gtk::make_managed<Gtk::Label>(
        Glib::ustring::compose(_("Version %1"), config::defaults::kAppVersion));
    versionLabel->add_css_class("dim-label");
    box->append(*versionLabel);

    // Separator
    auto* sep1 = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL);
    sep1->set_margin_top(kSpacingSmall);
    sep1->set_margin_bottom(kSpacingSmall);
    box->append(*sep1);

    // Architecture section
    auto* archHeader = Gtk::make_managed<Gtk::Label>();
    archHeader->set_markup(Glib::ustring::compose("<b>%1</b>", _("Architecture")));
    archHeader->set_xalign(0);
    box->append(*archHeader);

    auto* archLabel = Gtk::make_managed<Gtk::Label>(
        "MVP (Model-View-Presenter)\n"
        "Dependency Injection + Observer Pattern\n"
        "C++20 / GTK4 / SQLite / Boost.Asio");
    archLabel->set_xalign(0);
    archLabel->set_margin_start(kSpacingSmall);
    box->append(*archLabel);

    // Build info section
    auto* buildHeader = Gtk::make_managed<Gtk::Label>();
    buildHeader->set_markup(Glib::ustring::compose("<b>%1</b>", _("Build Info")));
    buildHeader->set_xalign(0);
    buildHeader->set_margin_top(kSpacingMedium);
    box->append(*buildHeader);

    auto* buildLabel = Gtk::make_managed<Gtk::Label>(buildInfoText());
    buildLabel->set_xalign(0);
    buildLabel->set_margin_start(kSpacingSmall);
    box->append(*buildLabel);

    // Languages section
    auto* langHeader = Gtk::make_managed<Gtk::Label>();
    langHeader->set_markup(Glib::ustring::compose("<b>%1</b>", _("Supported Languages")));
    langHeader->set_xalign(0);
    langHeader->set_margin_top(kSpacingMedium);
    box->append(*langHeader);

    auto* langLabel = Gtk::make_managed<Gtk::Label>(languageList());
    langLabel->set_xalign(0);
    langLabel->set_margin_start(kSpacingSmall);
    langLabel->set_wrap(true);
    box->append(*langLabel);

    // Testing section
    auto* testHeader = Gtk::make_managed<Gtk::Label>();
    testHeader->set_markup(Glib::ustring::compose("<b>%1</b>", _("Quality Assurance")));
    testHeader->set_xalign(0);
    testHeader->set_margin_top(kSpacingMedium);
    box->append(*testHeader);

    auto* testLabel = Gtk::make_managed<Gtk::Label>(
        _("86+ unit tests (GoogleTest + gmock)\n"
          "9 test binaries, CI/CD with coverage reporting"));
    testLabel->set_xalign(0);
    testLabel->set_margin_start(kSpacingSmall);
    box->append(*testLabel);

    // Separator
    auto* sep2 = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL);
    sep2->set_margin_top(kSpacingMedium);
    sep2->set_margin_bottom(kSpacingTiny);
    box->append(*sep2);

    // License
    auto* licenseLabel = Gtk::make_managed<Gtk::Label>(
        _("Proprietary - All Rights Reserved.\n"
          "For interview evaluation purposes only."));
    licenseLabel->add_css_class("dim-label");
    licenseLabel->set_xalign(0);
    box->append(*licenseLabel);

    // Spacer + Close button
    auto* spacer = Gtk::make_managed<Gtk::Box>();
    spacer->set_vexpand(true);
    box->append(*spacer);

    auto* closeBtn = Gtk::make_managed<Gtk::Button>(_("OK"));
    closeBtn->set_halign(Gtk::Align::END);
    closeBtn->signal_clicked().connect([this]() { close(); });
    box->append(*closeBtn);

    set_child(*box);
}

std::string AboutDialog::buildInfoText() {
    std::string info;
#ifdef __clang__
    info += "Compiler: Clang " + std::to_string(__clang_major__) + "."
            + std::to_string(__clang_minor__) + "\n";
#elif defined(__GNUC__)
    info += "Compiler: GCC " + std::to_string(__GNUC__) + "."
            + std::to_string(__GNUC_MINOR__) + "\n";
#elif defined(_MSC_VER)
    info += "Compiler: MSVC " + std::to_string(_MSC_VER) + "\n";
#endif

    info += "C++ Standard: C++20\n";

#ifdef _WIN32
    info += "Platform: Windows";
#elif defined(__linux__)
    info += "Platform: Linux";
#elif defined(__APPLE__)
    info += "Platform: macOS";
#else
    info += "Platform: Unknown";
#endif

    return info;
}

std::string AboutDialog::languageList() {
    return "English, Deutsch, Espanol, Espanol (Mexico),\n"
           "Suomi, Francais, Gaeilge, Italiano,\n"
           "Portugues, Portugues (Brasil), Svenska";
}

}  // namespace app::view
