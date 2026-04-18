#pragma once

#include "src/core/i18n.h"
#include "src/gtk/view/ui_sizes.h"

#include <gtkmm.h>

namespace app::view {

/// Small sidebar badge that shows the current system-machine state.
///
/// @design Pure view — driven by MainWindow which forwards state from
///         DashboardPresenter. A colored dot + label ("IDLE" /
///         "RUNNING" / "CALIBRATION" / "ERROR") mimics the status LEDs
///         you'd find on an industrial control panel.
class SystemStatusBadge : public Gtk::Box {
public:
    SystemStatusBadge()
        : Gtk::Box(Gtk::Orientation::HORIZONTAL, sizes::kSpacingSmall) {
        add_css_class("system-status-badge");
        set_margin_start(sizes::kSpacingLarge);
        set_margin_end(sizes::kSpacingLarge);
        set_margin_top(sizes::kSpacingMedium);
        set_margin_bottom(sizes::kSpacingMedium);

        dot_ = Gtk::make_managed<Gtk::Label>("●");
        dot_->add_css_class("system-status-dot");
        append(*dot_);

        label_ = Gtk::make_managed<Gtk::Label>();
        label_->set_xalign(0);
        label_->set_hexpand(true);
        label_->add_css_class("system-status-label");
        append(*label_);

        setState(0);  // start IDLE
    }

    /// Apply a new system state. Accepts the raw int from SystemState
    /// (0=IDLE, 1=RUNNING, 2=ERROR, 3=CALIBRATION) to stay decoupled
    /// from the enum's header.
    void setState(int state) {
        // Reset css classes for the dot — Gtk doesn't have a simple
        // "replace" so we remove the known ones before adding the new.
        for (const auto* cls : {"state-idle", "state-running",
                                 "state-error", "state-calibration"}) {
            dot_->remove_css_class(cls);
        }
        switch (state) {
            case 0: dot_->add_css_class("state-idle");
                    label_->set_label(_("IDLE"));        break;
            case 1: dot_->add_css_class("state-running");
                    label_->set_label(_("RUNNING"));     break;
            case 2: dot_->add_css_class("state-error");
                    label_->set_label(_("ERROR"));       break;
            case 3: dot_->add_css_class("state-calibration");
                    label_->set_label(_("CALIBRATION")); break;
            default: dot_->add_css_class("state-idle");
                     label_->set_label(_("UNKNOWN"));    break;
        }
        lastState_ = state;
    }

    /// Re-apply label text from the current gettext catalog. Called by
    /// MainWindow after a live language switch.
    void refreshTranslations() { setState(lastState_); }

private:
    Gtk::Label* dot_{nullptr};
    Gtk::Label* label_{nullptr};
    int         lastState_{0};
};

}  // namespace app::view
