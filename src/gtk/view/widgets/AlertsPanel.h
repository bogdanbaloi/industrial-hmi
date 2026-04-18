#pragma once

#include "src/presenter/AlertCenter.h"
#include "src/presenter/modelview/AlertViewModel.h"
#include "src/core/i18n.h"

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
/// @layout Vertical box: a small header with "Alerts" + "Clear all"
///         button, a ScrolledWindow with per-alert cards underneath.
class AlertsPanel : public Gtk::Box {
public:
    explicit AlertsPanel(presenter::AlertCenter& alertCenter)
        : Gtk::Box(Gtk::Orientation::VERTICAL)
        , alertCenter_(alertCenter) {
        set_vexpand(true);
        set_margin_start(12);
        set_margin_end(12);
        set_margin_top(12);
        set_margin_bottom(12);
        add_css_class("alerts-panel");

        buildHeader();
        buildList();

        alertCenter_.signalAlertsChanged().connect(
            [this]() {
                Glib::signal_idle().connect_once([this]() { refresh(); });
            });

        refresh();
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
        if (headerLabel_) headerLabel_->set_label(_("Alerts"));
        if (clearButton_) clearButton_->set_label(_("Clear all"));
        refresh();
    }

private:
    void buildHeader() {
        auto* header = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);

        headerLabel_ = Gtk::make_managed<Gtk::Label>(_("Alerts"));
        headerLabel_->set_xalign(0);
        headerLabel_->set_hexpand(true);
        headerLabel_->add_css_class("section-header");
        header->append(*headerLabel_);

        clearButton_ = Gtk::make_managed<Gtk::Button>(_("Clear all"));
        clearButton_->add_css_class("flat");
        clearButton_->signal_clicked().connect(
            [this]() { alertCenter_.clearAll(); });
        header->append(*clearButton_);

        append(*header);
    }

    void buildList() {
        scroller_ = Gtk::make_managed<Gtk::ScrolledWindow>();
        scroller_->set_vexpand(true);
        scroller_->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
        scroller_->set_margin_top(8);

        listBox_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
        scroller_->set_child(*listBox_);

        append(*scroller_);
    }

    void refresh() {
        // Wipe + rebuild. Small lists, no flicker concern in practice.
        while (auto* child = listBox_->get_first_child()) {
            listBox_->remove(*child);
        }

        const auto alerts = alertCenter_.snapshot();
        if (alerts.empty()) {
            auto* empty = Gtk::make_managed<Gtk::Label>(_("No alerts"));
            empty->add_css_class("dim-label");
            empty->set_xalign(0);
            listBox_->append(*empty);
            return;
        }

        for (const auto& a : alerts) {
            listBox_->append(*buildCard(a));
        }
    }

    Gtk::Widget* buildCard(const presenter::AlertViewModel& a) {
        auto* card = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
        card->add_css_class("alert-card");
        card->add_css_class(cssForSeverity(a.severity));
        card->set_margin_bottom(4);

        auto* top = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);

        auto* title = Gtk::make_managed<Gtk::Label>(a.title);
        title->set_xalign(0);
        title->set_hexpand(true);
        title->set_wrap(true);
        title->add_css_class("alert-title");
        top->append(*title);

        auto* ts = Gtk::make_managed<Gtk::Label>(a.timestamp);
        ts->add_css_class("dim-label");
        ts->add_css_class("alert-timestamp");
        top->append(*ts);

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
    Gtk::Button*         clearButton_{nullptr};
    Gtk::ScrolledWindow* scroller_{nullptr};
    Gtk::Box*            listBox_{nullptr};
};

}  // namespace app::view
