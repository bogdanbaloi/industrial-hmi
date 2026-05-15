#include "src/gtk/view/pages/AuditLogPage.h"

#include "src/auth/AuditEvent.h"
#include "src/core/i18n.h"

#include <format>

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

    // Column header row. Sticky across refresh; the list below is
    // rebuilt on every query.
    auto* header = Gtk::make_managed<Gtk::Box>(
        Gtk::Orientation::HORIZONTAL, kRowSpacingPx);
    header->add_css_class("heading");
    header->append(*makeCell(_("Timestamp"),  kColTimestamp));
    header->append(*makeCell(_("User"),       kColUser));
    header->append(*makeCell(_("Role"),       kColRole));
    header->append(*makeCell(_("Category"),   kColCategory));
    header->append(*makeCell(_("Action"),     kColAction));
    header->append(*makeCell(_("Result"),     kColResult));
    header->append(*makeCell(_("Details"),    0));
    append(*header);

    // Scrolling list area. The inner Box gets rebuilt on every
    // refresh -- a smarter implementation would use Gtk::ListView +
    // factory model, but for a few hundred rows the rebuild cost is
    // microseconds.
    scroller_ = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroller_->set_vexpand(true);
    listBox_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL,
                                           kRowPaddingPx);
    scroller_->set_child(*listBox_);
    append(*scroller_);

    footerLabel_ = Gtk::make_managed<Gtk::Label>("");
    footerLabel_->set_xalign(0.0F);
    append(*footerLabel_);
}

void AuditLogPage::onRefreshClicked() { refresh(); }
void AuditLogPage::onFilterChanged()  { refresh(); }

void AuditLogPage::refresh() {
    // Wipe + re-fetch. Building rows from scratch every time is
    // simpler than diffing; at 500 rows it's invisible to the user.
    auto* child = listBox_->get_first_child();
    while (child != nullptr) {
        auto* next = child->get_next_sibling();
        listBox_->remove(*child);
        child = next;
    }

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
    auto* row = Gtk::make_managed<Gtk::Box>(
        Gtk::Orientation::HORIZONTAL, kRowSpacingPx);

    row->append(*makeCell(e.timestamp, kColTimestamp));
    row->append(*makeCell(e.username,  kColUser));
    row->append(*makeCell(e.role,      kColRole));
    row->append(*makeCell(e.category,  kColCategory));
    row->append(*makeCell(e.action,    kColAction));

    auto* resultCell = makeCell(e.result, kColResult);
    // Success/failure colouring as a quick visual scan-aid; the CSS
    // classes come from the existing palette stylesheet (success +
    // error are standard semantic classes in the project's theme).
    if (e.result == "FAILURE") {
        resultCell->add_css_class("error");
    } else {
        resultCell->add_css_class("success");
    }
    row->append(*resultCell);

    // Details column gets the remaining width -- 0 means "natural
    // width", which expands within the row's hbox.
    row->append(*makeCell(e.details, 0));

    listBox_->append(*row);
}

}  // namespace app::view
