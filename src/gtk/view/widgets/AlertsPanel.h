#pragma once

#include "src/presenter/AlertCenter.h"
#include "src/presenter/modelview/AlertViewModel.h"
#include "src/core/i18n.h"
#include "src/gtk/view/ui_sizes.h"

#include <gtkmm.h>
#include <chrono>
#include <string>

namespace app::view {

/// Sidebar widget that renders the current AlertCenter snapshot.
///
/// @design Subscribes to AlertCenter::signalAlertsChanged and rebuilds
///         its list each time -- alert volume is tiny (handful of rows)
///         so full redraws are simpler and faster than diffing. Listens
///         on whichever thread the signal fires, then marshals back to
///         the GTK main thread via Glib::signal_idle.
///
///         Two view modes controlled by the header toggle:
///           Active  -- current alerts (AlertCenter::snapshot())
///           History -- last N cleared alerts (AlertCenter::history())
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
        // No set_vexpand(true) here -- the internal ScrolledWindow
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
        // `[this]` lambda) so sigc's trackable machinery -- Gtk::Box
        // inherits from sigc::trackable -- auto-disconnects the slot
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

    /// Switch to a compact horizontal-bar variant: hides the scrolling
    /// list, makes the header inline ("ALERTS . N") so the panel fits
    /// in a single row of the multistation bottom-bar sidebar without
    /// dominating. Operator still sees count + can clear; detailed
    /// per-alert cards are only visible in the regular vertical
    /// layout. Idempotent. One-way for v1; reverting requires a fresh
    /// panel instance.
    void setCompact() {
        compact_ = true;
        // Slim the panel's own margins -- horizontal bar has tighter
        // visual budget than the vertical sidebar.
        set_margin_start(8);
        set_margin_end(8);
        set_margin_top(0);
        set_margin_bottom(0);
        if (scroller_ != nullptr) scroller_->set_visible(false);
        // Suppress the history toggle in compact mode -- there's no
        // list to swap into. Operator opens the future Alerts tab for
        // history detail. Clear-all stays so the operator can resolve
        // active alerts inline.
        if (historyToggle_ != nullptr) historyToggle_->set_visible(false);
        updateHeaderLabels();
    }

private:
    // Queue a single GTK-thread refresh. Repeated calls while an idle
    // is already pending collapse into one -- cheaper redraw + avoids
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
            std::string base = showingHistory_ ? _("History") : _("Alerts");
            if (compact_) {
                // Append the active alert count so the operator still
                // sees the badge even though the list is hidden.
                const auto n = alertCenter_.snapshot().size();
                base += " \xC2\xB7 ";              // U+00B7 MIDDLE DOT
                base += std::to_string(n);
            }
            headerLabel_->set_label(base);
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
        // worst case -- ~100px total for the whole panel).
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
        // Compact mode -- header carries the count; we still need to
        // refresh that label every alert change so the badge updates.
        if (compact_) {
            updateHeaderLabels();
            return;
        }

        // Wipe + rebuild. Small lists, no flicker concern in practice.
        while (auto* child = listBox_->get_first_child()) {
            listBox_->remove(*child);
        }

        if (showingHistory_) {
            renderHistory();
        } else {
            renderActiveAndShelved();
        }
    }

    /// Phase 4b (REQ-ALARM-005): render TWO subsections in the Active
    /// view, top-to-bottom:
    ///   1. Active alarms ordered by ISA-18.2 priority ascending
    ///   2. Shelved inventory ordered by deadline ascending (most-
    ///      imminent expiry first), each row with a countdown widget
    /// The "No alerts" placeholder fires only when BOTH lists are empty;
    /// the Shelved subsection header is suppressed when there is nothing
    /// to put under it, so an operator with active-only alarms sees the
    /// same layout as before Phase 4b.
    void renderActiveAndShelved() {
        const auto active  = alertCenter_.snapshot();
        const auto shelved = alertCenter_.shelvedSnapshot();

        if (active.empty() && shelved.empty()) {
            appendPlaceholder(_("No alerts"));
            return;
        }

        for (const auto& a : active) {
            listBox_->append(*buildCard(a, /*historyMode*/ false, ""));
        }

        if (!shelved.empty()) {
            appendShelvedHeader(shelved.size());
            for (const auto& s : shelved) {
                listBox_->append(*buildShelvedCard(s));
            }
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

        // ISA-18.2 priority badge (P1..P4) -- distinct from severity, so
        // the operator can scan urgency at a glance regardless of colour.
        if (!historyMode) {
            auto* pbadge = Gtk::make_managed<Gtk::Label>(priorityBadge(a.priority));
            pbadge->add_css_class("alert-priority-badge");
            pbadge->set_valign(Gtk::Align::CENTER);
            pbadge->set_tooltip_text(_("ISA-18.2 priority (1 = most urgent)"));
            top->append(*pbadge);
        }

        // ISA-18.2 lifecycle badge (active mode only): UNACK / ACK / RTN / SHELVED.
        if (!historyMode) {
            auto* badge = Gtk::make_managed<Gtk::Label>(stateBadge(a.state));
            badge->add_css_class("alert-state-badge");
            badge->set_valign(Gtk::Align::CENTER);
            badge->set_tooltip_text(stateTooltip(a.state));
            top->append(*badge);
        }

        // In history mode surface when the alert resolved; in active
        // mode show when it was raised.
        auto* ts = Gtk::make_managed<Gtk::Label>(
            historyMode ? resolvedAt : a.timestamp);
        ts->add_css_class("dim-label");
        ts->add_css_class("alert-timestamp");
        top->append(*ts);

        if (!historyMode) {
            const std::string key = a.key;

            // Shelve: only meaningful for an actively-shown alarm. A
            // shelved row offers Unshelve instead so the operator can
            // pull it back before the deadline.
            if (a.state != presenter::AlarmState::Shelved) {
                auto* shelve = Gtk::make_managed<Gtk::Button>();
                shelve->set_icon_name("appointment-soon-symbolic");
                shelve->set_has_frame(false);
                shelve->set_valign(Gtk::Align::CENTER);
                shelve->add_css_class("alert-dismiss");
                shelve->set_tooltip_text(_("Shelve (auto-unshelves after 5 min)"));
                shelve->signal_clicked().connect(
                    [this, key]() {
                        alertCenter_.shelve(key, kDefaultShelveDuration);
                    });
                top->append(*shelve);
            } else {
                auto* unshelve = Gtk::make_managed<Gtk::Button>();
                unshelve->set_icon_name("edit-undo-symbolic");
                unshelve->set_has_frame(false);
                unshelve->set_valign(Gtk::Align::CENTER);
                unshelve->add_css_class("alert-dismiss");
                unshelve->set_tooltip_text(_("Unshelve"));
                unshelve->signal_clicked().connect(
                    [this, key]() { alertCenter_.unshelve(key); });
                top->append(*unshelve);
            }

            // Per-alarm Acknowledge (ISA-18.2). An unacknowledged active
            // alarm becomes acknowledged but stays visible until its
            // condition clears; an alarm that already returned to normal
            // (RtnUnack) is fully resolved and moves to history. The
            // operator can't make a transient fault vanish unseen.
            auto* ack = Gtk::make_managed<Gtk::Button>();
            ack->set_icon_name("emblem-ok-symbolic");
            ack->set_has_frame(false);
            ack->set_valign(Gtk::Align::CENTER);
            ack->add_css_class("alert-dismiss");
            ack->set_tooltip_text(_("Acknowledge"));
            ack->signal_clicked().connect(
                [this, key]() { alertCenter_.acknowledge(key); });
            top->append(*ack);
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

    /// Section divider rendered above the Shelved subsection. Carries the
    /// label "SHELVED (n)" so the operator sees the count at a glance
    /// without scrolling to the bottom.
    void appendShelvedHeader(std::size_t count) {
        // Top margin separates the Shelved section from the active list
        // above. Bottom margin tightens it against the first shelved
        // card so the grouping reads as one block.
        constexpr int kSectionTopMargin    = 12;
        constexpr int kSectionBottomMargin = 4;
        auto* hdr = Gtk::make_managed<Gtk::Label>();
        hdr->set_xalign(0);
        hdr->set_margin_top(kSectionTopMargin);
        hdr->set_margin_bottom(kSectionBottomMargin);
        hdr->add_css_class("alerts-shelved-header");
        hdr->set_label(
            Glib::ustring(_("SHELVED")) + " (" + std::to_string(count) + ")");
        hdr->set_tooltip_text(
            _("Alarms operator-shelved; auto-unshelve at the listed deadline"));
        listBox_->append(*hdr);
    }

    /// Build a card for one shelved alarm. Layout mirrors `buildCard`
    /// but the timestamp slot is replaced by the countdown
    /// ("4:37 left" / "EXPIRED") and the action row offers Unshelve only
    /// (Acknowledge is not meaningful while shelved -- the alarm is
    /// out of the active inventory by operator choice).
    Gtk::Widget* buildShelvedCard(const presenter::AlertCenter::ShelvedView& s) {
        const auto& a = s.vm;
        auto* card = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
        card->add_css_class("alert-card");
        card->add_css_class(cssForSeverity(a.severity));
        card->add_css_class("alert-shelved");
        card->set_margin_bottom(4);

        auto* top = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);

        auto* title = Gtk::make_managed<Gtk::Label>(a.title);
        title->set_xalign(0);
        title->set_hexpand(true);
        title->set_wrap(true);
        title->add_css_class("alert-title");
        top->append(*title);

        // Priority badge stays so the operator sees urgency even on
        // shelved entries (a P1 shelved-and-expiring-in-30s is still
        // operator-attention-worthy).
        auto* pbadge = Gtk::make_managed<Gtk::Label>(priorityBadge(a.priority));
        pbadge->add_css_class("alert-priority-badge");
        pbadge->set_valign(Gtk::Align::CENTER);
        pbadge->set_tooltip_text(_("ISA-18.2 priority (1 = most urgent)"));
        top->append(*pbadge);

        // Countdown replaces the timestamp on shelved cards. The
        // operator's attention metric on a shelved alarm is "how soon
        // does it un-shelve back into the active list?", not "when did
        // it originally fire".
        auto* countdown = Gtk::make_managed<Gtk::Label>(
            formatCountdown(s.secondsRemaining));
        countdown->add_css_class("alert-countdown");
        if (s.secondsRemaining <= std::chrono::seconds{0}) {
            countdown->add_css_class("alert-countdown-expired");
        }
        countdown->set_tooltip_text(_("Time remaining until auto-unshelve"));
        top->append(*countdown);

        // Unshelve action -- pull the alarm back into the active list
        // before its deadline. Acknowledge is not offered because the
        // alarm is out of the active inventory by operator choice; the
        // operator unshelves first, then acknowledges from the active
        // section.
        const std::string key = a.key;
        auto* unshelve = Gtk::make_managed<Gtk::Button>();
        unshelve->set_icon_name("edit-undo-symbolic");
        unshelve->set_has_frame(false);
        unshelve->set_valign(Gtk::Align::CENTER);
        unshelve->add_css_class("alert-dismiss");
        unshelve->set_tooltip_text(_("Unshelve"));
        unshelve->signal_clicked().connect(
            [this, key]() { alertCenter_.unshelve(key); });
        top->append(*unshelve);

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

public:
    /// Format a seconds-remaining countdown for the Shelved subsection
    /// row. Public + static so tests can verify the formatting contract
    /// without standing up the full GTK widget tree.
    ///
    ///   secondsRemaining == 0  -> "EXPIRED" (entry is past deadline
    ///                             and will be swept by the next tick)
    ///   1..59                  -> "0:SS left"
    ///   60+                    -> "M:SS left", zero-padded seconds
    ///
    /// MM:SS is the operator-canonical format for short ISA-18.2 shelf
    /// durations (typical 5 min); we keep the same shape past 60 min so
    /// a long shelve renders as e.g. "65:30 left" -- ugly but
    /// unambiguous. Localisation: "left" / "EXPIRED" go through gettext.
    static Glib::ustring formatCountdown(std::chrono::seconds remaining) {
        if (remaining <= std::chrono::seconds{0}) {
            return _("EXPIRED");
        }
        const auto total = remaining.count();
        const auto mins  = total / 60;
        const auto secs  = total % 60;
        std::string out;
        out.reserve(8);
        out += std::to_string(mins);
        out += ':';
        if (secs < 10) out += '0';
        out += std::to_string(secs);
        out += ' ';
        out += _("left");
        return Glib::ustring(out);
    }

private:

    static const char* cssForSeverity(presenter::AlertSeverity s) {
        switch (s) {
            case presenter::AlertSeverity::Info:     return "alert-info";
            case presenter::AlertSeverity::Warning:  return "alert-warning";
            case presenter::AlertSeverity::Critical: return "alert-critical";
        }
        return "alert-info";
    }

    // Short ISA-18.2 lifecycle badge shown on active alarm cards.
    static Glib::ustring stateBadge(presenter::AlarmState st) {
        switch (st) {
            case presenter::AlarmState::UnackActive: return _("UNACK");
            case presenter::AlarmState::AckActive:   return _("ACK");
            case presenter::AlarmState::RtnUnack:    return _("RTN");
            case presenter::AlarmState::Shelved:     return _("SHELVED");
        }
        return {};
    }

    static Glib::ustring stateTooltip(presenter::AlarmState st) {
        switch (st) {
            case presenter::AlarmState::UnackActive:
                return _("Active, unacknowledged");
            case presenter::AlarmState::AckActive:
                return _("Active, acknowledged");
            case presenter::AlarmState::RtnUnack:
                return _("Returned to normal, awaiting acknowledgement");
            case presenter::AlarmState::Shelved:
                return _("Shelved (auto-unshelves at deadline)");
        }
        return {};
    }

    // ISA-18.2 priority badge text. Lower numbers = more urgent.
    static Glib::ustring priorityBadge(int p) {
        return Glib::ustring("P") + std::to_string(p);
    }

    /// Default shelve duration when the operator clicks the Shelve button.
    /// 5 min is a typical ISA-18.2 "give me a moment" reset window; long
    /// enough that the operator can dispatch the actual fix, short enough
    /// that a forgotten shelf doesn't suppress a real condition for the
    /// whole shift.
    static constexpr std::chrono::seconds kDefaultShelveDuration{300};

    presenter::AlertCenter& alertCenter_;

    Gtk::Label*          headerLabel_{nullptr};
    Gtk::ToggleButton*   historyToggle_{nullptr};
    Gtk::Button*         clearButton_{nullptr};
    Gtk::ScrolledWindow* scroller_{nullptr};
    Gtk::Box*            listBox_{nullptr};
    bool                 showingHistory_{false};
    bool                 compact_{false};
    sigc::connection     pendingRefresh_;
};

}  // namespace app::view
