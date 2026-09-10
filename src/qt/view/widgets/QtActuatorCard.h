#pragma once

#include "src/presenter/modelview/ActuatorCardViewModel.h"

#include <QGroupBox>

#include <cstdint>
#include <optional>

class QEvent;
class QLabel;

namespace app::view {

/// One actuator card. Read-only: it renders an ActuatorCardViewModel. Actuator
/// state is driven by the model, so there is no operator back-channel here.
class QtActuatorCard : public QGroupBox {
public:
    explicit QtActuatorCard(std::uint32_t actuatorId, QWidget* parent = nullptr);

    void applyViewModel(const presenter::ActuatorCardViewModel& viewModel);

protected:
    // Re-render the code-set text (title, status frame, flags) in the new
    // language on a live switch; the card otherwise only refreshes on a fresh
    // view model.
    void changeEvent(QEvent* event) override;

private:
    std::uint32_t actuatorId_;

    QLabel* statusLabel_{nullptr};
    QLabel* messageLabel_{nullptr};
    QLabel* flagsLabel_{nullptr};

    // Last rendered model, so a language change can re-render from it.
    std::optional<presenter::ActuatorCardViewModel> lastViewModel_;
};

}  // namespace app::view
