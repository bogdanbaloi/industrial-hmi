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

private:
    QLabel* value_{nullptr};
};

}  // namespace app::view
