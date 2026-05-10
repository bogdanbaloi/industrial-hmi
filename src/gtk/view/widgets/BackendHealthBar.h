#pragma once

#include "src/gtk/view/ui_sizes.h"
#include "src/integration/IntegrationBackend.h"
#include "src/presenter/modelview/BackendHealthViewModel.h"

#include <gtkmm.h>

#include <string>
#include <unordered_map>

namespace app::view {

/// Horizontal strip of named status dots, one per registered
/// integration backend.
///
/// @design Pure view -- the bar holds no state beyond what's needed to
///         redraw the dots on the next ViewModel update. Driven by
///         `BackendHealthPresenter` via `update()`. The dots use the
///         same CSS-class swap pattern as `SystemStatusBadge` so the
///         palette stylesheets can theme connected/degraded/etc.
///         independently.
///
///         The widget rebuilds its children whenever the ViewModel
///         entry list shape changes (entry count or order). Steady-
///         state updates (same backends, just changing health) only
///         flip CSS classes + tooltips -- O(N) on the small list, no
///         tree teardown.
class BackendHealthBar : public Gtk::Box {
public:
    BackendHealthBar()
        : Gtk::Box(Gtk::Orientation::HORIZONTAL, sizes::kSpacingSmall) {
        add_css_class("backend-health-bar");
        set_margin_start(sizes::kSpacingMedium);
        set_margin_end(sizes::kSpacingMedium);
    }

    void update(const presenter::BackendHealthViewModel& vm) {
        if (vm.entries.size() != entries_.size() ||
            !sameNames(vm)) {
            rebuildChildren(vm);
        }

        for (std::size_t i = 0; i < vm.entries.size(); ++i) {
            applyState(entries_[i], vm.entries[i]);
        }
    }

private:
    struct Entry {
        Gtk::Box*   box{nullptr};
        Gtk::Label* dot{nullptr};
        Gtk::Label* label{nullptr};
        std::string name;
    };

    [[nodiscard]] bool sameNames(
        const presenter::BackendHealthViewModel& vm) const {
        for (std::size_t i = 0; i < entries_.size(); ++i) {
            if (entries_[i].name != vm.entries[i].name) return false;
        }
        return true;
    }

    void rebuildChildren(const presenter::BackendHealthViewModel& vm) {
        // Drop existing children; Gtk::make_managed instances die with
        // their parent so removing from the box is enough.
        while (auto* child = get_first_child()) {
            remove(*child);
        }
        entries_.clear();
        entries_.reserve(vm.entries.size());

        for (const auto& src : vm.entries) {
            Entry entry;
            entry.name = src.name;

            entry.box = Gtk::make_managed<Gtk::Box>(
                Gtk::Orientation::HORIZONTAL, sizes::kSpacingSmall);
            entry.box->add_css_class("backend-health-entry");

            entry.dot = Gtk::make_managed<Gtk::Label>("●");  // ●
            entry.dot->add_css_class("backend-health-dot");
            entry.box->append(*entry.dot);

            entry.label = Gtk::make_managed<Gtk::Label>(src.name);
            entry.label->add_css_class("backend-health-label");
            entry.box->append(*entry.label);

            append(*entry.box);
            entries_.push_back(entry);
        }
    }

    static void applyState(
        Entry& entry,
        const presenter::BackendHealthViewModel::Entry& src) {
        // Wipe any previous state classes before applying the new one.
        for (const auto* cls : {"state-disconnected", "state-connecting",
                                 "state-connected",   "state-degraded"}) {
            entry.dot->remove_css_class(cls);
        }
        switch (src.state) {
            using enum integration::BackendState;
        case Disconnected:
            entry.dot->add_css_class("state-disconnected"); break;
        case Connecting:
            entry.dot->add_css_class("state-connecting");   break;
        case Connected:
            entry.dot->add_css_class("state-connected");    break;
        case Degraded:
            entry.dot->add_css_class("state-degraded");     break;
        }

        // Tooltip carries the metrics line ("port 4840 | 2 sessions").
        // Empty -> no tooltip; gtkmm hides the popover automatically.
        entry.box->set_tooltip_text(src.metricsLine);
    }

    std::vector<Entry> entries_;
};

}  // namespace app::view
