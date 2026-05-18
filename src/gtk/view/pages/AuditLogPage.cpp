#include "src/gtk/view/pages/AuditLogPage.h"

#include "src/auth/AuditEvent.h"
#include "src/core/TimeFormat.h"
#include "src/core/i18n.h"

#include <format>
#include <string>

namespace app::view {

namespace {

constexpr int kPageSpacingPx     = 12;
constexpr int kToolbarSpacingPx  = 12;
constexpr int kPageMarginPx      = 16;
constexpr int kRowSpacingPx      = 8;
constexpr int kRowPaddingPx      = 4;
constexpr int kEntryWidthChars   = 16;

constexpr unsigned kAutoRefreshIntervalMs = 5'000;
constexpr std::size_t kQueryLimit         = 500;

// Column widths in characters. Sized so a typical audit row fits
// without horizontal scrolling at the default window width.
constexpr int kColTimestamp = 20;
constexpr int kColUser      = 14;
constexpr int kColRole      = 12;
constexpr int kColCategory  = 11;
constexpr int kColAction    = 10;
constexpr int kColResult    = 8;

/// Cell label.
///   widthChars > 0  -> fixed-width column with ellipsised overflow.
///   widthChars == 0 -> natural-width "flex" cell that absorbs the
///                      remaining horizontal space; used by the
///                      Details column on the right edge. Calling
///                      set_width_chars(0) + ellipsize(END) on a
///                      label collapses it to literal "..." (the
///                      cell tries to fit in 0 chars and shows the
///                      ellipsis), so we branch here.
Gtk::Label* makeCell(const std::string& text, int widthChars) {
    auto* l = Gtk::make_managed<Gtk::Label>(text);
    l->set_xalign(0.0F);
    if (widthChars > 0) {
        l->set_width_chars(widthChars);
        l->set_max_width_chars(widthChars);
        l->set_ellipsize(Pango::EllipsizeMode::END);
    } else {
        l->set_hexpand(true);
        l->set_ellipsize(Pango::EllipsizeMode::END);
    }
    return l;
}

}  // namespace

AuditLogPage::AuditLogPage(DialogManager& dialogManager,
                           app::auth::AuditLogger& reader)
    : Page(dialogManager), reader_(reader) {
    buildUi();
    refresh();
    // Live refresh -- same pattern as HistoryPage. The query runs on
    // the GTK main loop directly (no thread marshalling needed).
    autoRefreshConn_ = Glib::signal_timeout().connect(
        [this]() { refresh(); return true; }, kAutoRefreshIntervalMs);
}

AuditLogPage::~AuditLogPage() {
    autoRefreshConn_.disconnect();
}

Glib::ustring AuditLogPage::pageTitle() const {
    return _("Audit Log");
}

void AuditLogPage::buildUi() {
    set_orientation(Gtk::Orientation::VERTICAL);
    set_spacing(kPageSpacingPx);
    set_margin(kPageMarginPx);

    // Filter toolbar.
    auto* toolbar = Gtk::make_managed<Gtk::Box>(
        Gtk::Orientation::HORIZONTAL, kToolbarSpacingPx);

    toolbar->append(*Gtk::make_managed<Gtk::Label>(_("Category:")));
    categoryFilter_ = Gtk::make_managed<Gtk::ComboBoxText>();
    categoryFilter_->append(_("All"));
    categoryFilter_->append("AUTH");
    categoryFilter_->append("PRODUCTION");
    categoryFilter_->append("EQUIPMENT");
    categoryFilter_->append("PRODUCT");
    categoryFilter_->append("USER");
    categoryFilter_->append("ALERT");
    categoryFilter_->set_active(0);
    categoryFilter_->signal_changed().connect(
        sigc::mem_fun(*this, &AuditLogPage::onFilterChanged));
    toolbar->append(*categoryFilter_);

    toolbar->append(*Gtk::make_managed<Gtk::Label>(_("User:")));
    usernameFilter_ = Gtk::make_managed<Gtk::Entry>();
    usernameFilter_->set_width_chars(kEntryWidthChars);
    usernameFilter_->set_placeholder_text(_("(all)"));
    // Filter on Enter rather than every keystroke -- typing "admin"
    // letter-by-letter would otherwise issue 5 SQL queries.
    usernameFilter_->signal_activate().connect(
        sigc::mem_fun(*this, &AuditLogPage::onFilterChanged));
    toolbar->append(*usernameFilter_);

    refreshButton_ = Gtk::make_managed<Gtk::Button>(_("Refresh"));
    refreshButton_->signal_clicked().connect(
        sigc::mem_fun(*this, &AuditLogPage::onRefreshClicked));
    toolbar->append(*refreshButton_);

    append(*toolbar);

    // Scrolling grid area. Gtk::Grid (rather than a stack of Boxes)
    // because columns must line up between the bold heading row and
    // the regular-weight data rows; with Boxes + set_width_chars the
    // bold characters consume more pixels per char and the
    // misalignment accumulates left-to-right.
    scroller_ = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroller_->set_vexpand(true);
    grid_ = Gtk::make_managed<Gtk::Grid>();
    grid_->set_column_spacing(kRowSpacingPx);
    grid_->set_row_spacing(kRowPaddingPx);
    scroller_->set_child(*grid_);
    append(*scroller_);

    // Header row at grid row 0. Each header cell uses the same
    // makeCell() so width-chars logic matches the data rows exactly.
    auto attachHeader = [&](const std::string& text, int col,
                            int widthChars) {
        auto* l = makeCell(text, widthChars);
        l->add_css_class("heading");
        grid_->attach(*l, col, 0, 1, 1);
    };
    attachHeader(_("Timestamp"), 0, kColTimestamp);
    attachHeader(_("User"),      1, kColUser);
    attachHeader(_("Role"),      2, kColRole);
    attachHeader(_("Category"),  3, kColCategory);
    attachHeader(_("Action"),    4, kColAction);
    attachHeader(_("Result"),    5, kColResult);
    attachHeader(_("Details"),   6, 0);

    footerLabel_ = Gtk::make_managed<Gtk::Label>("");
    footerLabel_->set_xalign(0.0F);
    append(*footerLabel_);
}

void AuditLogPage::onRefreshClicked() { refresh(); }
void AuditLogPage::onFilterChanged()  { refresh(); }

void AuditLogPage::refresh() {
    // Wipe data rows + re-fetch. Grid row 0 is the header (sticky);
    // remove rows from 1 upward, then reset the next-row counter.
    // Walk by cell coordinates rather than child iteration so an
    // already-cleared row doesn't trip up.
    constexpr int kColumnCount = 7;
    for (int r = nextRow_ - 1; r >= 1; --r) {
        for (int c = 0; c < kColumnCount; ++c) {
            if (auto* child = grid_->get_child_at(c, r)) {
                grid_->remove(*child);
            }
        }
    }
    nextRow_ = 1;

    app::auth::AuditQuery q;
    q.limit = kQueryLimit;
    if (categoryFilter_ != nullptr) {
        const auto idx = categoryFilter_->get_active_row_number();
        // Index 0 == "All" -> leave the field empty.
        if (idx > 0) {
            q.category = categoryFilter_->get_active_text().raw();
        }
    }
    if (usernameFilter_ != nullptr) {
        const auto raw = usernameFilter_->get_text().raw();
        if (!raw.empty()) q.username = raw;
    }

    const auto rows = reader_.query(q);
    for (const auto& e : rows) {
        appendRow(e);
    }

    if (footerLabel_ != nullptr) {
        footerLabel_->set_text(std::format(
            "Showing {} of {} total events", rows.size(),
            reader_.totalEvents()));
    }
}

void AuditLogPage::appendRow(const app::auth::AuditEvent& e) {
    const int r = nextRow_++;
    grid_->attach(*makeCell(app::core::formatIso8601Local(e.timestamp),
                            kColTimestamp), 0, r, 1, 1);
    grid_->attach(*makeCell(e.username,  kColUser),      1, r, 1, 1);
    grid_->attach(*makeCell(e.role,      kColRole),      2, r, 1, 1);
    grid_->attach(*makeCell(e.category,  kColCategory),  3, r, 1, 1);
    grid_->attach(*makeCell(e.action,    kColAction),    4, r, 1, 1);

    auto* resultCell = makeCell(e.result, kColResult);
    // Success/failure colouring as a quick visual scan-aid; the CSS
    // classes come from the existing palette stylesheet (success +
    // error are standard semantic classes in the project's theme).
    if (e.result == "FAILURE") {
        resultCell->add_css_class("error");
    } else {
        resultCell->add_css_class("success");
    }
    grid_->attach(*resultCell, 5, r, 1, 1);

    // Details column gets the remaining width (widthChars=0 in
    // makeCell sets hexpand=true so the cell flows into whatever
    // horizontal space remains in the grid).
    grid_->attach(*makeCell(e.details, 0), 6, r, 1, 1);
}

}  // namespace app::view
