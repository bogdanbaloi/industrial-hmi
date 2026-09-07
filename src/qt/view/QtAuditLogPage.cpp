#include "src/qt/view/QtAuditLogPage.h"

#include "src/auth/AuditEvent.h"
#include "src/auth/AuditLogger.h"

#include "ui_QtAuditLogPage.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDateTime>
#include <QEvent>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QString>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVariant>
#include <Qt>

#include <array>
#include <string>

namespace app::view {

namespace {

// Table columns (0 named for a self-documenting setItem; 1..6 are in
// clang-tidy's magic-number ignore list).
constexpr int kColTimestamp = 0;
constexpr int kColUser      = 1;
constexpr int kColRole      = 2;
constexpr int kColCategory  = 3;
constexpr int kColAction    = 4;
constexpr int kColResult    = 5;
constexpr int kColDetails   = 6;
constexpr int kColumnCount  = 7;

// Relative-range windows offered in the filter, in seconds (0 == no bound).
constexpr qint64 kRangeAll  = 0;
constexpr qint64 kRangeHour = 60LL * 60LL;
constexpr qint64 kRangeDay  = 24LL * kRangeHour;
constexpr qint64 kRangeWeek = 7LL * kRangeDay;

// Auto-refresh cadence, matching the History page's live feel.
constexpr int kAutoRefreshMs = 5000;

}  // namespace

QtAuditLogPage::QtAuditLogPage(auth::AuditLogger& reader, QWidget* parent)
    : QWidget(parent),
      ui_(std::make_unique<Ui::QtAuditLogPage>()),
      reader_(reader) {
    ui_->setupUi(this);

    auto* table = ui_->auditTable;
    table->setColumnCount(kColumnCount);
    applyHeaderLabels();
    table->horizontalHeader()->setStretchLastSection(true);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);

    populateFilters();

    connect(ui_->refreshButton, &QPushButton::clicked, this,
            [this] { refresh(); });
    connect(ui_->categoryCombo, &QComboBox::currentIndexChanged, this,
            [this] { refresh(); });
    connect(ui_->resultCombo, &QComboBox::currentIndexChanged, this,
            [this] { refresh(); });
    connect(ui_->rangeCombo, &QComboBox::currentIndexChanged, this,
            [this] { refresh(); });
    connect(ui_->userEdit, &QLineEdit::returnPressed, this,
            [this] { refresh(); });

    // Auto-refresh so newly-audited actions appear without an operator click.
    autoRefresh_ = new QTimer(this);
    connect(autoRefresh_, &QTimer::timeout, this, [this] { refresh(); });
    autoRefresh_->start(kAutoRefreshMs);

    refresh();
}

QtAuditLogPage::~QtAuditLogPage() = default;

void QtAuditLogPage::applyHeaderLabels() {
    ui_->auditTable->setHorizontalHeaderLabels(
        {tr("Timestamp"), tr("User"), tr("Role"), tr("Category"), tr("Action"),
         tr("Result"), tr("Details")});
}

void QtAuditLogPage::populateFilters() {
    const QSignalBlocker categoryBlock(ui_->categoryCombo);
    const QSignalBlocker resultBlock(ui_->resultCombo);
    const QSignalBlocker rangeBlock(ui_->rangeCombo);

    ui_->categoryCombo->clear();
    ui_->categoryCombo->addItem(tr("All"), QString());
    for (const auto category :
         {auth::category::kAuth, auth::category::kProduction,
          auth::category::kEquipment, auth::category::kProduct,
          auth::category::kUser, auth::category::kAlert}) {
        const QString code = QString::fromUtf8(std::string(category).c_str());
        ui_->categoryCombo->addItem(code, code);
    }

    ui_->resultCombo->clear();
    ui_->resultCombo->addItem(tr("All"), QString());
    ui_->resultCombo->addItem(
        tr("Success"),
        QString::fromUtf8(std::string(auth::result::kSuccess).c_str()));
    ui_->resultCombo->addItem(
        tr("Failure"),
        QString::fromUtf8(std::string(auth::result::kFailure).c_str()));

    ui_->rangeCombo->clear();
    ui_->rangeCombo->addItem(tr("All time"), QVariant::fromValue(kRangeAll));
    ui_->rangeCombo->addItem(tr("Last hour"), QVariant::fromValue(kRangeHour));
    ui_->rangeCombo->addItem(tr("Last 24 hours"),
                             QVariant::fromValue(kRangeDay));
    ui_->rangeCombo->addItem(tr("Last 7 days"), QVariant::fromValue(kRangeWeek));
}

auth::AuditQuery QtAuditLogPage::buildQuery() const {
    auth::AuditQuery query;
    query.category = ui_->categoryCombo->currentData().toString().toStdString();
    query.result   = ui_->resultCombo->currentData().toString().toStdString();
    query.username = ui_->userEdit->text().trimmed().toStdString();

    const qint64 windowSec = ui_->rangeCombo->currentData().toLongLong();
    if (windowSec > 0) {
        // ISO 8601 UTC lower bound; audit timestamps are ISO strings, so a
        // lexicographic lower bound is a valid time filter.
        query.fromTs = QDateTime::currentDateTimeUtc()
                           .addSecs(-windowSec)
                           .toString(Qt::ISODate)
                           .toStdString();
    }
    return query;
}

void QtAuditLogPage::refresh() {
    const auto events = reader_.query(buildQuery());
    auto* table = ui_->auditTable;
    table->setRowCount(static_cast<int>(events.size()));

    int row = 0;
    for (const auto& event : events) {
        table->setItem(row, kColTimestamp,
                       new QTableWidgetItem(QString::fromStdString(event.timestamp)));
        table->setItem(row, kColUser,
                       new QTableWidgetItem(QString::fromStdString(event.username)));
        table->setItem(row, kColRole,
                       new QTableWidgetItem(QString::fromStdString(event.role)));
        table->setItem(row, kColCategory,
                       new QTableWidgetItem(QString::fromStdString(event.category)));
        table->setItem(row, kColAction,
                       new QTableWidgetItem(QString::fromStdString(event.action)));
        table->setItem(row, kColResult,
                       new QTableWidgetItem(QString::fromStdString(event.result)));
        table->setItem(row, kColDetails,
                       new QTableWidgetItem(QString::fromStdString(event.details)));
        ++row;
    }
    ui_->footerLabel->setText(tr("%1 events shown (of %2 total)")
                                  .arg(events.size())
                                  .arg(reader_.totalEvents()));
}

void QtAuditLogPage::changeEvent(QEvent* event) {
    if (event != nullptr && event->type() == QEvent::LanguageChange) {
        ui_->retranslateUi(this);
        applyHeaderLabels();
        populateFilters();  // re-localise the "All" / result labels
        refresh();
    }
    QWidget::changeEvent(event);
}

}  // namespace app::view
