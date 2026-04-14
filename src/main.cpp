#include "src/core/Application.h"
#include <cstdlib>

int main(int argc, char* argv[]) {
#ifdef _WIN32
    // Use Cairo renderer on Windows to avoid GL flicker
    _putenv_s("GSK_RENDERER", "cairo");
#endif

    auto& app = app::core::Application::instance();

    if (!app.initialize(argc, argv)) {
        return 1;
    }

    int result = app.run(argc, argv);
    app.shutdown();
    return result;
}
