#pragma once

#include <QFrame>
#include <QString>

class QLabel;

namespace app::view {

/// A single KPI tile: a large value over a small caption, in a bordered card.
/// The value is set live from aggregated view-model data; the tile itself holds
/// no logic, so the dashboard owns one per metric and pushes formatted strings.
class QtKpiTile : public QFrame {
public:
    explicit QtKpiTile(const QString& caption, QWidget* parent = nullptr);

    void setValue(const QString& value);

    /// Colour the big value by state (green ok / amber warn / red alarm), so the
    /// tile reads at a glance. Pass a theme colour string.
    void setValueColor(const char* color);

    /// Re-set the caption (used on a live language change so the tile label
    /// re-reads the catalog; the value is data-driven and refreshes on its own).
    void setCaption(const QString& caption);

private:
    QLabel* value_{nullptr};
    QLabel* caption_{nullptr};
};

}  // namespace app::view
