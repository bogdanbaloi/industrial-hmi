#pragma once

#include "src/presenter/modelview/EquipmentCardViewModel.h"

#include <QGroupBox>

#include <cstdint>
#include <functional>

class QCheckBox;
class QLabel;

namespace app::view {

/// One equipment-station card. Passive: it renders an EquipmentCardViewModel
/// and forwards the operator's enable toggle back through a callback, so the
/// card never needs to know about the presenter directly.
class QtEquipmentCard : public QGroupBox {
public:
    using ToggleCallback = std::function<void(std::uint32_t, bool)>;

    QtEquipmentCard(std::uint32_t equipmentId, ToggleCallback onToggle,
                    QWidget* parent = nullptr);

    void applyViewModel(const presenter::EquipmentCardViewModel& viewModel);

private:
    std::uint32_t  equipmentId_;
    ToggleCallback onToggle_;

    QLabel*    statusLabel_{nullptr};
    QLabel*    consumablesLabel_{nullptr};
    QLabel*    messageLabel_{nullptr};
    QCheckBox* enableCheck_{nullptr};
};

}  // namespace app::view
