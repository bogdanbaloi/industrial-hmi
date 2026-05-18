#pragma once

#include <gtkmm.h>

#include <cstdint>

namespace app::view {

/// Slim banner that slides in from the top of a page to confirm a
/// recent action ("Password reset", "User created", "Calibration
/// failed"). Auto-dismisses after a few seconds; the operator may
/// also dismiss it manually via the close button.
///
/// @design Why a custom widget rather than Gtk::InfoBar:
///   * Gtk::InfoBar is deprecated in GTK 4.10+ -- the recommended
///     replacement (Adw::Toast) requires libadwaita which we don't
///     pull in (extra dependency just for this).
///   * The visual we want is the existing "banner that uses the
///     theme's .success / .error palette colours", which is two
///     lines of CSS + a Revealer.
///
/// @theming Two CSS classes are applied (one at a time) so the theme
/// stylesheet can paint the banner:
///   * `success` -> green-ish (matches the existing dot indicators
///     across DashboardPage / SystemStatusBadge)
///   * `error`   -> red-ish (matches alert highlights)
/// Both classes already exist in the palette stylesheet; no theme
/// changes are needed.
///
/// @i18n Messages flow in already translated -- the widget never
/// concatenates strings or formats numbers. Callers pass the result
/// of `_("...")` directly.
/// Visual placement for the banner. Translates to the widget's
/// halign + valign so a parent Gtk::Overlay can float the toast in
/// the requested corner; in a plain Gtk::Box parent only the
/// horizontal direction has a visible effect (vertical slot is
/// determined by Box order). Default = TopCenter.
enum class ToastPosition : std::uint8_t {
    TopLeft,
    TopCenter,
    TopRight,
    BottomLeft,
    BottomCenter,
    BottomRight,
};

class Toast : public Gtk::Revealer {
public:
    /// Default auto-dismiss for success toasts. 3.5 s is long enough
    /// for an operator's eye to catch the banner after a confirming
    /// glance away (clipboard, neighbour ask) and short enough not
    /// to loiter and overlap with the next action.
    static constexpr unsigned kDefaultSuccessMs = 3'500;
    /// 0 == "stay visible until manually dismissed". Used as the
    /// error-toast default so a failure reason isn't lost to a
    /// quick glance away.
    static constexpr unsigned kStayUntilDismissed = 0;

    /// Runtime knobs. All optional -- the defaults match "success
    /// fades after 3.5 s, error stays until dismissed, top-centre
    /// placement". Override per host page via setOptions().
    struct Options {
        unsigned       successDurationMs = kDefaultSuccessMs;
        unsigned       errorDurationMs   = kStayUntilDismissed;
        ToastPosition  position          = ToastPosition::TopCenter;
    };

    Toast();
    explicit Toast(const Options& opts);
    ~Toast() override = default;

    Toast(const Toast&)            = delete;
    Toast& operator=(const Toast&) = delete;
    Toast(Toast&&)                 = delete;
    Toast& operator=(Toast&&)      = delete;

    /// Update behaviour at runtime. Useful for pages that want to
    /// keep one Toast across themes / configurations and re-apply
    /// (e.g. on a language rebuild). Doesn't affect a currently-
    /// visible banner's pending timer -- only the next show*() call
    /// picks up the new durations.
    void setOptions(const Options& opts);

    /// Show a positive-outcome banner. Replaces any toast currently
    /// visible. Auto-dismisses after `options.successDurationMs`
    /// (skipped when 0).
    void showSuccess(const Glib::ustring& message);

    /// Show a negative-outcome banner. Auto-dismisses after
    /// `options.errorDurationMs` (skipped when 0 -- the default,
    /// because a failure reason that scrolls away unread is a
    /// silent bug).
    void showError(const Glib::ustring& message);

    /// Manually dismiss (used by the X button). No-op when already
    /// hidden -- safe to call from idle callbacks racing the
    /// auto-dismiss timer.
    void dismiss();

private:
    enum class Tone : std::uint8_t { Success, Error };
    void buildUi();
    void applyPosition(ToastPosition position);
    void showInternal(const Glib::ustring& message, Tone tone);

    Options           options_;
    Gtk::Label*       textLabel_{nullptr};
    Gtk::Button*      closeButton_{nullptr};
    sigc::connection  autoDismissConn_;
};

}  // namespace app::view
