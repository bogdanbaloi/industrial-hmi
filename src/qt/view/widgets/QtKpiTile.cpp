#include "src/qt/view/widgets/QtKpiTile.h"

#include "src/qt/view/QtTheme.h"

#include <QFont>
#include <QLabel>
#include <QVBoxLayout>

namespace app::view {

namespace {
constexpr int kTileSpacing    = 2;
constexpr int kValuePointSize = 20;
}  // namespace

QtKpiTile::QtKpiTile(const QString& caption, QWidget* parent) : QFrame(parent) {
    setObjectName("kpiTile");
    setFrameShape(QFrame::StyledPanel);

    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(kTileSpacing);

    value_ = new QLabel(QStringLiteral("--"));
    QFont valueFont = value_->font();
    valueFont.setPointSize(kValuePointSize);
    valueFont.setBold(true);
    value_->setFont(valueFont);
    layout->addWidget(value_);

    auto* captionLabel = new QLabel(caption);
    captionLabel->setStyleSheet(theme::coloredBold(theme::kColorNeutral));
    layout->addWidget(captionLabel);
}

void QtKpiTile::setValue(const QString& value) { value_->setText(value); }

}  // namespace app::view
