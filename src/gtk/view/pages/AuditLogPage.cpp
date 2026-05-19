#include "src/gtk/view/pages/AuditLogPage.h"

#include "src/auth/AuditEvent.h"
#include "src/core/TimeFormat.h"
#include "src/core/i18n.h"
#include "src/gtk/view/widgets/Toast.h"

#include <giomm/file.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <format>
#include <fstream>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>

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

// Known action verbs across all categories. Listed alphabetically so a
// compliance auditor scanning the dropdown finds what they expect
// quickly. Adding a new verb is one new entry here; the SqliteAuditLogger
// query layer is verb-agnostic so no other change is needed.
constexpr std::array<std::string_view, 17> kActionList = {
    "ADD",
    "CALIBRATE",
    "CHANGE_AVATAR",
    "CHANGE_PASSWORD",
    "CLEAR_AVATAR",
    "CREATE",
    "DELETE",
    "DISABLE",
    "ENABLE",
    "LOGIN",
    "LOGOUT",
    "RESET",
    "RESET_PASSWORD",
    "START",
    "STOP",
    "UPDATE",
    "VIEW",
};

// Range picker presets -- map to a lookback in seconds. Stored as a
// distinct enum-like sentinel (0 = "all time") so the refresh() path
// can decide whether to set q.fromTs at all.
struct RangePreset {
    std::string_view label;
    std::int64_t     lookbackSeconds;  // 0 == no lower bound
};
constexpr std::array<RangePreset, 4> kRangePresets = {{
    {"All time",       0},
    {"Last hour",      60LL * 60},
    {"Last 24 hours",  24LL * 60 * 60},
    {"Last 7 days",    7LL  * 24 * 60 * 60},
}};

/// Escape a single CSV field per RFC 4180: wrap in double quotes if
/// the value contains a comma, double quote, CR or LF; double any
/// embedded quotes inside. Plain ASCII fields pass through unchanged.
/// Inline (rather than re-using integration/CsvSerializer which is
/// Product-specific) so the audit export stays self-contained -- a
/// future refactor can lift this into src/core if a third caller
/// wants RFC 4180 too.
std::string escapeCsv(std::string_view field) {
    bool mustQuote = false;
    for (char c : field) {
        if (c == ',' || c == '"' || c == '\n' || c == '\r') {
            mustQuote = true;
            break;
        }
    }
    if (!mustQuote) return std::string{field};
    std::string out;
    out.reserve(field.size() + 2);
    out.push_back('"');
    for (char c : field) {
        if (c == '"') out.push_back('"');   // double embedded quotes
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

/// Format a `time_point` as ISO 8601 UTC -- matches the stamps the
/// writers emit so range queries compare apples to apples. Used by
/// the range picker to translate "Last hour" into a `fromTs` SQL
/// parameter.
std::string isoFromTimePoint(std::chrono::system_clock::time_point tp) {
    const auto tt = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
#if defined(_WIN32)
    ::gmtime_s(&tm, &tt);
#else
    ::gmtime_r(&tt, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
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

    // Action filter. Independent of category -- a compliance walk
    // typically goes "show me every RESET_PASSWORD" across all
    // categories rather than scoping to USER first. The All entry is
    // the row 0 sentinel matching the pattern of categoryFilter_.
    toolbar->append(*Gtk::make_managed<Gtk::Label>(_("Action:")));
    actionFilter_ = Gtk::make_managed<Gtk::ComboBoxText>();
    actionFilter_->append(_("All"));
    for (auto verb : kActionList) {
        actionFilter_->append(std::string{verb});
    }
    actionFilter_->set_active(0);
    actionFilter_->signal_changed().connect(
        sigc::mem_fun(*this, &AuditLogPage::onFilterChanged));
    toolbar->append(*actionFilter_);

    // Result filter -- the most common compliance ask is
    // "FAILURE only" (failed logins, refused RBAC actions,
    // validation rejects), so the three options stay short.
    toolbar->append(*Gtk::make_managed<Gtk::Label>(_("Result:")));
    resultFilter_ = Gtk::make_managed<Gtk::ComboBoxText>();
    resultFilter_->append(_("All"));
    resultFilter_->append("SUCCESS");
    resultFilter_->append("FAILURE");
    resultFilter_->set_active(0);
    resultFilter_->signal_changed().connect(
        sigc::mem_fun(*this, &AuditLogPage::onFilterChanged));
    toolbar->append(*resultFilter_);

    // Range picker. Preset durations (Last hour / 24h / 7 days) rather
    // than calendar pickers -- operators rarely need an exact
    // start-of-day range and the presets translate one-to-one to a
    // `fromTs` parameter at refresh time. "All time" leaves fromTs
    // empty so the SQL WHERE clause skips the lower bound.
    toolbar->append(*Gtk::make_managed<Gtk::Label>(_("Range:")));
    rangeFilter_ = Gtk::make_managed<Gtk::ComboBoxText>();
    for (const auto& preset : kRangePresets) {
        rangeFilter_->append(std::string{preset.label});
    }
    rangeFilter_->set_active(0);
    rangeFilter_->signal_changed().connect(
        sigc::mem_fun(*this, &AuditLogPage::onFilterChanged));
    toolbar->append(*rangeFilter_);

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

    // Export CSV -- compliance walkers (21 CFR Part 11, GMP) need a
    // file they can hand to auditors. The button runs the current
    // filter set against the logger with no row cap, so what the
    // operator sees on screen matches what lands in the spreadsheet.
    exportButton_ = Gtk::make_managed<Gtk::Button>(_("Export CSV..."));
    exportButton_->signal_clicked().connect(
        sigc::mem_fun(*this, &AuditLogPage::onExportClicked));
    toolbar->append(*exportButton_);

    append(*toolbar);

    // Toast under the toolbar -- shows the export result + any
    // future filter feedback. Same widget as UsersPage; .success
    // auto-dismisses, .error stays until acknowledged.
    toast_ = Gtk::make_managed<Toast>();
    append(*toast_);

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

app::auth::AuditQuery
AuditLogPage::buildQuery(std::size_t limit) const {
    app::auth::AuditQuery q;
    q.limit = limit;
    if (categoryFilter_ != nullptr) {
        const auto idx = categoryFilter_->get_active_row_number();
        // Index 0 == "All" -> leave the field empty.
        if (idx > 0) {
            q.category = categoryFilter_->get_active_text().raw();
        }
    }
    if (actionFilter_ != nullptr
            && actionFilter_->get_active_row_number() > 0) {
        q.action = actionFilter_->get_active_text().raw();
    }
    if (resultFilter_ != nullptr
            && resultFilter_->get_active_row_number() > 0) {
        q.result = resultFilter_->get_active_text().raw();
    }
    if (rangeFilter_ != nullptr) {
        const auto idx = static_cast<std::size_t>(
            rangeFilter_->get_active_row_number());
        if (idx < kRangePresets.size()
                && kRangePresets[idx].lookbackSeconds > 0) {
            const auto cutoff = std::chrono::system_clock::now()
                - std::chrono::seconds{
                    kRangePresets[idx].lookbackSeconds};
            q.fromTs = isoFromTimePoint(cutoff);
        }
    }
    if (usernameFilter_ != nullptr) {
        const auto raw = usernameFilter_->get_text().raw();
        if (!raw.empty()) q.username = raw;
    }
    return q;
}

std::int64_t AuditLogPage::writeCsvExport(const std::string& path) {
    // Pull the full match (limit = 0) -- compliance exports must
    // include every row that matches the filter, not just the
    // UI-friendly slice.
    constexpr std::size_t kNoLimit = 0;
    const auto rows = reader_.query(buildQuery(kNoLimit));

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return -1;

    // UTF-8 BOM so Excel detects the encoding and doesn't mangle
    // non-ASCII characters in usernames / details. Same trick the
    // existing products CsvSerializer uses.
    out << "\xEF\xBB\xBF";

    // Header row -- column names are stable (NOT localised) so the
    // CSV stays diffable + parseable across deployments / languages.
    // The on-screen labels are translated; the file isn't.
    out << "Timestamp,Username,Role,Category,Action,Result,Details\r\n";

    std::int64_t written = 0;
    for (const auto& e : rows) {
        out << escapeCsv(e.timestamp) << ','
            << escapeCsv(e.username)  << ','
            << escapeCsv(e.role)      << ','
            << escapeCsv(e.category)  << ','
            << escapeCsv(e.action)    << ','
            << escapeCsv(e.result)    << ','
            << escapeCsv(e.details)
            << "\r\n";
        ++written;
    }
    return out.good() ? written : -1;
}

void AuditLogPage::onExportClicked() {
    // Suggested filename -- matches the audit-log-YYYY-MM-DD-HHMMSS
    // convention compliance auditors expect when archiving exports
    // by date.
    const auto suggested = std::format(
        "audit-log-{}.csv",
        isoFromTimePoint(std::chrono::system_clock::now()));

    auto dialog = Gtk::FileDialog::create();
    dialog->set_title(_("Export audit log"));
    dialog->set_initial_name(suggested);

    auto filter = Gtk::FileFilter::create();
    filter->set_name(_("CSV files"));
    filter->add_mime_type("text/csv");
    filter->add_pattern("*.csv");
    auto filterList = Gio::ListStore<Gtk::FileFilter>::create();
    filterList->append(filter);
    dialog->set_filters(filterList);
    dialog->set_default_filter(filter);

    // get_root() returns the toplevel; cast to Window for the dialog
    // parent. Null falls back to "no parent" which still works.
    auto* parent = dynamic_cast<Gtk::Window*>(get_root());
    dialog->save(
        *parent,
        [this, dialog](const Glib::RefPtr<Gio::AsyncResult>& result) {
            try {
                auto file = dialog->save_finish(result);
                if (!file) return;
                const auto path = file->get_path();
                const std::int64_t written = writeCsvExport(path);
                if (written < 0) {
                    if (toast_ != nullptr) {
                        toast_->showError(_("CSV export failed (I/O error)."));
                    }
                    return;
                }
                if (toast_ != nullptr) {
                    toast_->showSuccess(std::format(
                        "{} {} {} {}",
                        std::string{_("Exported")},
                        written,
                        std::string{_("rows to")},
                        path));
                }
            } catch (const Glib::Error& e) {
                // Operator dismissed the picker, OR a real I/O issue
                // bubbled up from gtkmm. We can't cleanly tell the
                // two apart across gtkmm versions, so always surface
                // the message; cancel reads as a benign status line.
                if (toast_ != nullptr) {
                    toast_->showError(e.what());
                }
            }
        });
}

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

    const auto rows = reader_.query(buildQuery(kQueryLimit));
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
