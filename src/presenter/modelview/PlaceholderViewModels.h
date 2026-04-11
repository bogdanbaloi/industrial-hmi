#pragma once
// Placeholder ViewModels for completeness
// These would be fully implemented in the actual project

#include <string>
#include <vector>
#include <cstdint>

namespace app::presenter {

// Simple placeholder ViewModels
struct HeaderStatusViewModel {
    bool networkConnected{false};
    bool databaseConnected{false};
    bool plcConnected{false};
    std::string connectionStatus;
};

struct StatusZoneViewModel {
    enum class Severity { NONE, INFO, WARNING, ERROR };
    Severity severity{Severity::NONE};
    std::string message;
    std::string backgroundColor;
};

struct ProcessStepViewModel {
    enum class Step { STEP_1 = 0, STEP_2, STEP_3, STEP_4, STEP_5 };
    enum class Status { PENDING, COMPLETED, FINISHED };
    
    Step step{Step::STEP_1};
    Status status{Status::PENDING};
    uint16_t coordinateX{0};
    uint16_t coordinateY{0};
    uint32_t equipmentId{0};
};

struct ProcessConfigDialogViewModel {
    std::string productId;
    std::string workUnitId;
    struct ConfigSpec {
        std::string stepName;
        uint16_t x, y;
    };
    std::vector<ConfigSpec> specifications;
};

struct ViewProductDialogViewModel {
    std::string productId;
    std::string description;
    std::string createdDate;
    bool isVerified{false};
    // ... more fields
};

struct ResetProductDialogViewModel {
    std::string productId;
    std::string productName;
    bool allowRestart{false};
    bool allowRelease{false};
};

struct ProductsViewModel {
    struct ProductItem {
        uint32_t id;
        std::string productId;
        std::string description;
        bool isVerified;
    };
    std::vector<ProductItem> products;
};

}  // namespace app::presenter
