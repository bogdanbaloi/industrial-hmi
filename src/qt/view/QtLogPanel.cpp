#include "src/qt/view/QtLogPanel.h"

#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QPlainTextEdit>
#include <QTimer>
#include <QVBoxLayout>

#include <utility>

namespace app::view {

namespace {
constexpr int kRefreshMs   = 500;
constexpr int kPanelHeight = 160;
constexpr int kMaxLines    = 500;
}  // namespace

QtLogPanel::QtLogPanel(QString logFilePath, QWidget* parent)
    : QWidget(parent), logFilePath_(std::move(logFilePath)) {
    setObjectName("logPanel");
    setMaximumHeight(kPanelHeight);

    auto* root = new QVBoxLayout(this);

    auto* header = new QLabel(tr("Log"), this);
    header->setObjectName("logHeader");
    root->addWidget(header);

    view_ = new QPlainTextEdit(this);
    view_->setObjectName("logView");
    view_->setReadOnly(true);
    view_->setMaximumBlockCount(kMaxLines);
    root->addWidget(view_);

    // Start tailing from the current end, like the GTK log panel does.
    const QFileInfo info(logFilePath_);
    lastSize_ = info.exists() ? info.size() : 0;

    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, [this] { poll(); });
    timer_->start(kRefreshMs);
}

QtLogPanel::~QtLogPanel() = default;

void QtLogPanel::poll() {
    QFile file(logFilePath_);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    const qint64 size = file.size();
    if (size <= lastSize_) {
        // No growth, or the file was rotated/truncated: resync to the new end.
        lastSize_ = size;
        return;
    }

    if (!file.seek(lastSize_)) {
        return;
    }
    const QString chunk = QString::fromUtf8(file.readAll()).trimmed();
    lastSize_ = size;

    if (!chunk.isEmpty()) {
        view_->appendPlainText(chunk);
    }
}

}  // namespace app::view
