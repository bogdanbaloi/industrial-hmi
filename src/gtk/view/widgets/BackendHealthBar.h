#pragma once

#include "src/gtk/view/ui_sizes.h"
#include "src/integration/IntegrationBackend.h"
#include "src/presenter/modelview/BackendHealthViewModel.h"

#include <gtkmm.h>

#include <optional>
#include <string>
#include <vector>

namespace app::view {

/// Status badges for every registered integration backend, driven by
/// `BackendHealthPresenter` via `update()`.
///
/// @design Pure view -- the bar holds no state beyond what's needed to
///         repaint on the next ViewModel update. CSS-class swap pattern
///         (mirrors `SystemStatusBadge`) lets palette stylesheets theme
///         the four states independently.
///
///         Two layouts are supported through the same widget so the
///         caller picks the one that fits the host slot:
///
///         - `Layout::Sidebar` (default): vertical card with section
///           header "I/O" + one row per backend (name on the left,
///           status pill on the right). Used in the regular sidebar.
///
///         - `Layout::Compact`: horizontal strip of name + colored dot
///           pairs, no card chrome, no header. Used in the Blueprint
///           top bar where vertical real estate is zero.
///
///         The widget rebuilds its children whenever the ViewModel
///         entry list shape changes (entry count or order). Steady-
///         state updates only flip CSS classes + tooltips.
class BackendHealthBar : public Gtk::Box {
public:
    enum class Layout { Sidebar, Compact };

    explicit BackendHealthBar(Layout layout = Layout::Sidebar)
        : Gtk::Box(layout == Layout::Sidebar ? Gtk::Orientation::VERTICAL
                                             : Gtk::Orientation::HORIZONTAL,
                   sizes::kSpacingSmall),
          layout_(layout) {
        if (layout_ == Layout::Sidebar) {
            // Outer margins live on the parent container in main-window.ui;
            // the bar itself only owns its internal padding via the
            // .backend-health-bar CSS rule.
            add_css_class("backend-health-bar");

            // Section header. Industrial vernacular -- "I/O" reads as
            // "the data interfaces" without sounding like programmer-
            // jargon ("backends", "endpoints"...).
            header_ = Gtk::make_managed<Gtk::Label>("I/O");
            header_->set_xalign(0);
            header_->add_css_class("backend-health-header");
            append(*header_);
        } else {
            // Compact: just the row of dot+name pairs. No card border,
            // no header -- the host top bar already provides the chrome.
            add_css_class("backend-health-strip");
            set_valign(Gtk::Align::CENTER);
            set_spacing(sizes::kSpacingMedium);
        }
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
        // Sidebar layout: row with name label + state pill on the right.
        // Compact layout: row with colored dot + name label inline.
        // Both layouts share the same struct to keep the update path
        // uniform; `pill` doubles as the state-color carrier in Compact
        // (it just renders as a small dot rather than a pill there).
        Gtk::Box*   row{nullptr};
        Gtk::Label* nameLabel{nullptr};
        Gtk::Label* pill{nullptr};
        std::string name;
        // Cache of the last applied state + tooltip so we can skip
        // GTK CSS / tooltip mutations on ticks where nothing changed.
        // Without this guard the 1Hz poll re-applies the same class
        // every second, which GTK invalidates -> visible flicker.
        std::optional<integration::BackendState> lastState;
        std::string                              lastMetrics;
    };

    [[nodiscard]] bool sameNames(
        const presenter::BackendHealthViewModel& vm) const {
        for (std::size_t i = 0; i < entries_.size(); ++i) {
            if (entries_[i].name != vm.entries[i].name) return false;
        }
        return true;
    }

    void rebuildChildren(const presenter::BackendHealthViewModel& vm) {
        // Drop existing entry rows but keep the section header (Sidebar
        // layout). For Compact there is no header, so everything goes.
        while (auto* child = get_last_child()) {
            if (child == header_) break;
            remove(*child);
        }
        entries_.clear();
        entries_.reserve(vm.entries.size());

        for (const auto& src : vm.entries) {
            Entry entry;
            entry.name = src.name;

            if (layout_ == Layout::Sidebar) {
                // Row layout: name on the left fills available width,
                // status pill anchored on the right with its natural size.
                entry.row = Gtk::make_managed<Gtk::Box>(
                    Gtk::Orientation::HORIZONTAL, sizes::kSpacingMedium);
                entry.row->add_css_class("backend-health-entry");

                entry.nameLabel = Gtk::make_managed<Gtk::Label>(src.name);
                entry.nameLabel->set_xalign(0);
                entry.nameLabel->set_hexpand(true);
                entry.nameLabel->add_css_class("backend-health-label");
                entry.row->append(*entry.nameLabel);

                // Pill text gets set in applyState() once the initial
                // state is known.
                entry.pill = Gtk::make_managed<Gtk::Label>("");
                entry.pill->add_css_class("backend-pill");
                entry.pill->set_halign(Gtk::Align::END);
                entry.row->append(*entry.pill);
            } else {
                // Compact: small dot + name, side by side, no chrome.
                entry.row = Gtk::make_managed<Gtk::Box>(
                    Gtk::Orientation::HORIZONTAL, sizes::kSpacingSmall);
                entry.row->add_css_class("backend-health-chip");
                entry.row->set_valign(Gtk::Align::CENTER);

                entry.pill = Gtk::make_managed<Gtk::Label>("●");
                entry.pill->add_css_class("backend-health-dot");
                entry.row->append(*entry.pill);

                entry.nameLabel = Gtk::make_managed<Gtk::Label>(src.name);
                entry.nameLabel->add_css_class("backend-health-label");
                entry.row->append(*entry.nameLabel);
            }

            append(*entry.row);
            entries_.push_back(entry);
        }
    }

    [[nodiscard]] static const char* stateLabel(
        integration::BackendState s) noexcept {
        using enum integration::BackendState;
        switch (s) {
        case Disconnected: return "OFFLINE";
        case Connecting:   return "CONNECTING";
        case Connected:    return "ONLINE";
        case Degraded:     return "DEGRADED";
        }
        return "";
    }

    void applyState(
        Entry& entry,
        const presenter::BackendHealthViewModel::Entry& src) {
        // Skip the CSS swap entirely if the state hasn't changed since
        // the previous tick -- otherwise GTK invalidates and repaints
        // the pill every second, producing visible flicker.
        if (!entry.lastState.has_value() || *entry.lastState != src.state) {
            for (const auto* cls : {"state-disconnected", "state-connecting",
                                     "state-connected",   "state-degraded"}) {
                entry.pill->remove_css_class(cls);
            }
            switch (src.state) {
                using enum integration::BackendState;
            case Disconnected:
                entry.pill->add_css_class("state-disconnected"); break;
            case Connecting:
                entry.pill->add_css_class("state-connecting");   break;
            case Connected:
                entry.pill->add_css_class("state-connected");    break;
            case Degraded:
                entry.pill->add_css_class("state-degraded");     break;
            }
            // Sidebar pills carry the state text; Compact dots stay
            // as the bullet glyph -- color alone tells the story.
            if (layout_ == Layout::Sidebar) {
                entry.pill->set_text(stateLabel(src.state));
            }
            entry.lastState = src.state;
        }

        // Tooltip carries the metrics line ("port 4840 | 2 sessions").
        // Empty -> no tooltip; gtkmm hides the popover automatically.
        // Cached above so we don't rewrite it every tick.
        if (entry.lastMetrics != src.metricsLine) {
            entry.row->set_tooltip_text(src.metricsLine);
            entry.lastMetrics = src.metricsLine;
        }
    }

    Layout             layout_;
    Gtk::Label*        header_{nullptr};
    std::vector<Entry> entries_;
};

}  // namespace app::view
