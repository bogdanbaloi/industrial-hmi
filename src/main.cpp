#include "src/core/Application.h"

int main(int argc, char* argv[]) {
    auto& app = app::core::Application::instance();

    if (!app.initialize(argc, argv)) {
        return 1;
    }

    int result = app.run(argc, argv);
    app.shutdown();
    return result;
}
