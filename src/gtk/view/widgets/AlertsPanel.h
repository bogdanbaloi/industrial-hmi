#pragma once

#include "src/presenter/AlertCenter.h"
#include "src/presenter/modelview/AlertViewModel.h"
#include "src/core/i18n.h"
#include "src/gtk/view/ui_sizes.h"

#include <gtkmm.h>
#include <string>

namespace app::view {

/// Sidebar widget that renders the current AlertCenter snapshot.
///
/// @design Subscribes to AlertCenter::signalAlertsChanged and rebuilds
///         its list each time — alert volume is tiny (handful of rows)
///         so full redraws are simpler and faster than diffing. Listens
///         on whichever thread the signal fires, then marshals back to
///         the GTK main thread via Glib::signal_idle.
///
///         Two view modes controlled by the header toggle:
///           Active  — current alerts (AlertCenter::snapshot())
///           History — last N cleared alerts (AlertCenter::history())
///         The action button adapts to the active view: Clear-all in
///         active mode, Clear-history in history mode.
///
/// @layout Vertical box: header row (title + toggle + action button),
///         ScrolledWindow with per-entry cards underneath.
class AlertsPanel : public Gtk::Box {
public:
    explicit AlertsPanel(presenter::AlertCenter& alertCenter)
        : Gtk::Box(Gtk::Orientation::VERTICAL)
        , alertCenter_(alertCenter) {
        // No set_vexpand(true) here — the internal ScrolledWindow
        // already carries vexpand, and adding vexpand to the panel
        // itself conflicts with a fixed-height footer (Blueprint
        // layout) where the container's height-request is the
        // authoritative allocation.
        set_margin_start(sizes::kSpacingMedium);
        set_margin_end(sizes::kSpacingMedium);
        set_margin_top(sizes::kSpacingMedium);
        set_margin_bottom(sizes::kSpacingMedium);
        add_css_class("alerts-panel");

        buildHeader();
        buildList();

        // Both signals share a coalesced refresh: any number of
        // rapid-fire raises/clears collapse into a single `refresh()`
        // on the next GTK idle tick. We use sigc::mem_fun (not a bare
        // `[this]` lambda) so sigc's trackable machinery — Gtk::Box
        // inherits from sigc::trackable — auto-disconnects the slot
        // when AlertsPanel is destroyed. A plain lambda captures
        // `this` opaquely and would keep firing on a dangling pointer
        // during a MainWindow relayout.
        alertCenter_.signalAlertsChanged().connect(
            sigc::mem_fun(*this, &AlertsPanel::scheduleRefresh));
        alertCenter_.signalHistoryChanged().connect(
            sigc::mem_fun(*this, &AlertsPanel::scheduleRefresh));

        refresh();
    }

    ~AlertsPanel() override {
        pendingRefresh_.disconnect();
    }

    AlertsPanel(const AlertsPanel&) = delete;
    AlertsPanel& operator=(const AlertsPanel&) = delete;
    AlertsPanel(AlertsPanel&&) = delete;
    AlertsPanel& operator=(AlertsPanel&&) = delete;

    /// Re-read labels from the current gettext catalog. MainWindow calls
    /// this after a live language switch because the panel's static text
    /// ("Alerts", "Clear all", "No alerts") was cached in the header at
    /// construction time.
    void refreshTranslations() {
        updateHeaderLabels();
        refresh();
    }

private:
    // Queue a single GTK-thread refresh. Repeated calls while an idle
    // is already pending collapse into one — cheaper redraw + avoids
    // a thundering herd when many alerts change in one tick.
    // connect() (not connect_once) returns a sigc::connection we can
    // disconnect from ~AlertsPanel() if the widget dies before the
    // idle fires (relayout / language rebuild).
    void scheduleRefresh() {
        if (pendingRefresh_.connected()) return;
        pendingRefresh_ = Glib::signal_idle().connect([this]() {
            pendingRefresh_ = sigc::connection{};
            refresh();
            return false;  // run once
        });
    }

    void buildHeader() {
        auto* header = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);

        headerLabel_ = Gtk::make_managed<Gtk::Label>();
        headerLabel_->set_xalign(0);
        headerLabel_->set_hexpand(true);
        headerLabel_->add_css_class("section-header");
        header->append(*headerLabel_);

        historyToggle_ = Gtk::make_managed<Gtk::ToggleButton>();
        historyToggle_->set_icon_name("document-open-recent-symbolic");
        historyToggle_->add_css_class("flat");
        historyToggle_->set_valign(Gtk::Align::CENTER);
        historyToggle_->signal_toggled().connect(
            [this]() {
                showingHistory_ = historyToggle_->get_active();
                updateHeaderLabels();
                refresh();
            });
        header->append(*historyToggle_);

        clearButton_ = Gtk::make_managed<Gtk::Button>();
        clearButton_->add_css_class("flat");
        clearButton_->signal_clicked().connect(
            [this]() {
                if (showingHistory_) alertCenter_.clearHistory();
                else                 alertCenter_.clearAll();
            });
        header->append(*clearButton_);

        append(*header);
        updateHeaderLabels();
    }

    // Keep header text in sync with the current view mode + locale.
    void updateHeaderLabels() {
        if (headerLabel_) {
            headerLabel_->set_label(showingHistory_ ? _("History") : _("Alerts"));
        }
        if (clearButton_) {
            clearButton_->set_label(showingHistory_ ? _("Clear history")
                                                    : _("Clear all"));
        }
        if (historyToggle_) {
            historyToggle_->set_tooltip_text(
                showingHistory_ ? _("Show active alerts")
                                : _("Show history"));
        }
    }

    void buildList() {
        // Minimum height reserved for the scroller so the panel
        // always shows at least one card row before internal
        // scrolling kicks in, regardless of how tight the enclosing
        // layout allocates to it (Blueprint's compact footer is the
        // worst case — ~100px total for the whole panel).
        constexpr int kScrollerMinContentHeight = 50;
        constexpr int kScrollerTopMargin        = 8;

        scroller_ = Gtk::make_managed<Gtk::ScrolledWindow>();
        scroller_->set_vexpand(true);
        scroller_->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
        scroller_->set_min_content_height(kScrollerMinContentHeight);
        scroller_->set_margin_top(kScrollerTopMargin);

        listBox_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
        scroller_->set_child(*listBox_);

        append(*scroller_);
    }

    void refresh() {
        // Wipe + rebuild. Small lists, no flicker concern in practice.
        while (auto* child = listBox_->get_first_child()) {
            listBox_->remove(*child);
        }

        if (showingHistory_) {
            renderHistory();
        } else {
            renderActive();
        }
    }

    void renderActive() {
        const auto alerts = alertCenter_.snapshot();
        if (alerts.empty()) {
            appendPlaceholder(_("No alerts"));
            return;
        }
        for (const auto& a : alerts) {
            listBox_->append(*buildCard(a, /*historyMode*/ false, ""));
        }
    }

    void renderHistory() {
        const auto entries = alertCenter_.history();
        if (entries.empty()) {
            appendPlaceholder(_("No history"));
            return;
        }
        for (const auto& e : entries) {
            listBox_->append(*buildCard(e.alert, /*historyMode*/ true, e.resolvedAt));
        }
    }

    void appendPlaceholder(const char* text) {
        auto* empty = Gtk::make_managed<Gtk::Label>(text);
        empty->add_css_class("dim-label");
        empty->set_xalign(0);
        listBox_->append(*empty);
    }

    Gtk::Widget* buildCard(const presenter::AlertViewModel& a,
                           bool historyMode,
                           const std::string& resolvedAt) {
        auto* card = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
        card->add_css_class("alert-card");
        card->add_css_class(cssForSeverity(a.severity));
        if (historyMode) card->add_css_class("alert-resolved");
        card->set_margin_bottom(4);

        auto* top = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);

        auto* title = Gtk::make_managed<Gtk::Label>(a.title);
        title->set_xalign(0);
        title->set_hexpand(true);
        title->set_wrap(true);
        title->add_css_class("alert-title");
        top->append(*title);

        // In history mode surface when the alert resolved; in active
        // mode show when it was raised.
        auto* ts = Gtk::make_managed<Gtk::Label>(
            historyMode ? resolvedAt : a.timestamp);
        ts->add_css_class("dim-label");
        ts->add_css_class("alert-timestamp");
        top->append(*ts);

        if (!historyMode) {
            // Per-alert dismiss. Calls clear(key) — if the underlying
            // condition still holds, the next presenter tick will re-raise
            // the alert, which matches typical HMI "acknowledge" semantics.
            auto* dismiss = Gtk::make_managed<Gtk::Button>();
            dismiss->set_icon_name("window-close-symbolic");
            dismiss->set_has_frame(false);
            dismiss->set_valign(Gtk::Align::CENTER);
            dismiss->add_css_class("alert-dismiss");
            dismiss->set_tooltip_text(_("Dismiss"));
            const std::string key = a.key;
            dismiss->signal_clicked().connect(
                [this, key]() { alertCenter_.clear(key); });
            top->append(*dismiss);
        }

        card->append(*top);

        if (!a.message.empty()) {
            auto* msg = Gtk::make_managed<Gtk::Label>(a.message);
            msg->set_xalign(0);
            msg->set_wrap(true);
            msg->add_css_class("alert-message");
            card->append(*msg);
        }

        return card;
    }

    static const char* cssForSeverity(presenter::AlertSeverity s) {
        switch (s) {
            case presenter::AlertSeverity::Info:     return "alert-info";
            case presenter::AlertSeverity::Warning:  return "alert-warning";
            case presenter::AlertSeverity::Critical: return "alert-critical";
        }
        return "alert-info";
    }

    presenter::AlertCenter& alertCenter_;

    Gtk::Label*          headerLabel_{nullptr};
    Gtk::ToggleButton*   historyToggle_{nullptr};
    Gtk::Button*         clearButton_{nullptr};
    Gtk::ScrolledWindow* scroller_{nullptr};
    Gtk::Box*            listBox_{nullptr};
    bool                 showingHistory_{false};
    sigc::connection     pendingRefresh_;
};

}  // namespace app::view
