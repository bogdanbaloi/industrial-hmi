#include "src/gtk/view/widgets/Toast.h"

#include "src/core/i18n.h"

namespace app::view {

namespace {

constexpr int kInnerSpacingPx = 12;
constexpr int kInnerMarginPx  = 10;
constexpr int kTransitionMs   = 220;

/// Translate a logical placement into the (halign, valign) pair the
/// widget needs. `set_valign(START)` keeps the banner pinned to the
/// top of the parent slot (the standard "toast slides in from above"
/// look); BOTTOM pins it to the bottom. Horizontal direction matches
/// the position's left/center/right suffix.
struct Alignment {
    Gtk::Align horizontal;
    Gtk::Align vertical;
    Gtk::RevealerTransitionType transition;
};

Alignment alignmentFor(ToastPosition position) {
    using enum ToastPosition;
    using enum Gtk::Align;
    using enum Gtk::RevealerTransitionType;
    switch (position) {
        case TopLeft:      return {START,  START, SLIDE_DOWN};
        case TopCenter:    return {CENTER, START, SLIDE_DOWN};
        case TopRight:     return {END,    START, SLIDE_DOWN};
        case BottomLeft:   return {START,  END,   SLIDE_UP};
        case BottomCenter: return {CENTER, END,   SLIDE_UP};
        case BottomRight:  return {END,    END,   SLIDE_UP};
    }
    return {CENTER, START, SLIDE_DOWN};
}

}  // namespace

Toast::Toast() : Toast(Options{}) {}

Toast::Toast(const Options& opts) : options_(opts) {
    buildUi();
    applyPosition(options_.position);
}

void Toast::buildUi() {
    set_transition_duration(kTransitionMs);
    set_reveal_child(false);

    // Outer box holds text + close button. CSS classes go on the
    // OUTER widget (not on the Revealer itself) so a future theme
    // tweak can target `.toast.success` without dragging Revealer's
    // own state in.
    auto* row = Gtk::make_managed<Gtk::Box>(
        Gtk::Orientation::HORIZONTAL, kInnerSpacingPx);
    row->set_margin(kInnerMarginPx);
    row->add_css_class("toast");

    textLabel_ = Gtk::make_managed<Gtk::Label>();
    textLabel_->set_xalign(0.0F);
    textLabel_->set_hexpand(true);
    textLabel_->set_wrap(true);
    row->append(*textLabel_);

    closeButton_ = Gtk::make_managed<Gtk::Button>();
    closeButton_->set_label("\xC3\x97");           // U+00D7 multiplication sign
    closeButton_->set_tooltip_text(_("Dismiss"));
    closeButton_->add_css_class("flat");
    closeButton_->signal_clicked().connect(
        sigc::mem_fun(*this, &Toast::dismiss));
    row->append(*closeButton_);

    set_child(*row);
}

void Toast::applyPosition(ToastPosition position) {
    const auto a = alignmentFor(position);
    set_halign(a.horizontal);
    set_valign(a.vertical);
    set_transition_type(a.transition);
}

void Toast::setOptions(const Options& opts) {
    options_ = opts;
    applyPosition(options_.position);
}

void Toast::showSuccess(const Glib::ustring& message) {
    showInternal(message, Tone::Success);
}

void Toast::showError(const Glib::ustring& message) {
    showInternal(message, Tone::Error);
}

void Toast::showInternal(const Glib::ustring& message, Tone tone) {
    // Cancel any pending auto-dismiss so a fresh toast doesn't get
    // wiped after a few hundred ms by the previous one's timer.
    autoDismissConn_.disconnect();

    textLabel_->set_text(message);

    auto* row = dynamic_cast<Gtk::Box*>(get_child());
    if (row != nullptr) {
        // Swap the tone class so the existing palette stylesheet
        // paints the banner. Removing both first keeps the widget
        // idempotent across repeated show calls.
        row->remove_css_class("success");
        row->remove_css_class("error");
        row->add_css_class(tone == Tone::Success ? "success" : "error");
    }

    set_reveal_child(true);

    // Auto-dismiss only when the configured duration is > 0. Zero is
    // the "stay until manually dismissed" sentinel; the default for
    // errors so a failure reason isn't lost to a quick glance away.
    const unsigned durationMs =
        tone == Tone::Success ? options_.successDurationMs
                              : options_.errorDurationMs;
    if (durationMs > 0) {
        autoDismissConn_ = Glib::signal_timeout().connect(
            [this]() {
                set_reveal_child(false);
                return false;   // one-shot
            },
            durationMs);
    }
}

void Toast::dismiss() {
    autoDismissConn_.disconnect();
    set_reveal_child(false);
}

}  // namespace app::view
