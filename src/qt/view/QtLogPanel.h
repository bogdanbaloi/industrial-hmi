#pragma once

#include <QString>
#include <QWidget>

class QPlainTextEdit;
class QTimer;

namespace app::view {

/// Live log panel: tails the application log file and appends new lines to a
/// read-only view, the Qt analog of the GTK bottom log panel (which also tails
/// the file rather than hooking the logger). SRP: it shows the log; it is
/// decoupled from the logger internals, reading only the file path it is given.
class QtLogPanel : public QWidget {
public:
    explicit QtLogPanel(QString logFilePath, QWidget* parent = nullptr);
    ~QtLogPanel() override;

    QtLogPanel(const QtLogPanel&)            = delete;
    QtLogPanel& operator=(const QtLogPanel&) = delete;
    QtLogPanel(QtLogPanel&&)                 = delete;
    QtLogPanel& operator=(QtLogPanel&&)      = delete;

private:
    void poll();

    QString         logFilePath_;
    QPlainTextEdit* view_{nullptr};
    QTimer*         timer_{nullptr};
    qint64          lastSize_{0};
};

}  // namespace app::view
