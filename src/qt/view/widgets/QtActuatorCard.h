#pragma once

#include "src/presenter/modelview/ActuatorCardViewModel.h"

#include <QGroupBox>

#include <cstdint>

class QLabel;

namespace app::view {

/// One actuator card. Read-only: it renders an ActuatorCardViewModel. Actuator
/// state is driven by the model, so there is no operator back-channel here.
class QtActuatorCard : public QGroupBox {
public:
    explicit QtActuatorCard(std::uint32_t actuatorId, QWidget* parent = nullptr);

    void applyViewModel(const presenter::ActuatorCardViewModel& viewModel);

private:
    std::uint32_t actuatorId_;

    QLabel* statusLabel_{nullptr};
    QLabel* messageLabel_{nullptr};
    QLabel* flagsLabel_{nullptr};
};

}  // namespace app::view
