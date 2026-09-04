#pragma once

#include "src/presenter/modelview/QualityCheckpointViewModel.h"

#include <QGroupBox>

#include <cstdint>

class QLabel;

namespace app::view {

/// One quality-checkpoint card. Read-only: it renders a
/// QualityCheckpointViewModel (pass rate, inspected count, defects, last
/// defect). Checkpoint state is driven by the model, so there is no operator
/// back-channel here.
class QtQualityCard : public QGroupBox {
public:
    explicit QtQualityCard(std::uint32_t checkpointId, QWidget* parent = nullptr);

    void applyViewModel(const presenter::QualityCheckpointViewModel& viewModel);

private:
    std::uint32_t checkpointId_;

    QLabel* statusLabel_{nullptr};
    QLabel* passRateLabel_{nullptr};
    QLabel* statsLabel_{nullptr};
    QLabel* lastDefectLabel_{nullptr};
};

}  // namespace app::view
