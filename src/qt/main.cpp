// SimulatedModel.h first: it defines ProductionError::ERROR before any Qt
// header pulls in wingdi.h, whose ERROR=0 macro would corrupt that enum
// (the same include-order rule the GTK/console main.cpp follows).
#include "src/model/SimulatedModel.h"

#include "src/core/Bootstrap.h"
#include "src/qt/QtInitRoot.h"

#include <QApplication>

#include <exception>
#include <iostream>

// Composition entry point for the Qt frontend. Kept intentionally thin: it
// owns the QApplication and the Bootstrap, then hands off to QtInitRoot,
// which wires the real model + presenter + view exactly the way InitConsole
// does for the console binary.
int main(int argc, char* argv[]) {
    try {
        QApplication app(argc, argv);

        app::core::Bootstrap bootstrap;
        bootstrap.run();

        // Declared after the QApplication so it tears down first: on scope
        // exit (after exec returns) QtInitRoot stops the tick thread and
        // detaches from the presenter before the app object goes away.
        app::qt::QtInitRoot root(bootstrap);
        root.run();

        return app.exec();
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << '\n';
        return 1;
    } catch (...) {
        std::cerr << "Fatal: unknown (non-std::exception) error reached main.\n";
        return 2;
    }
}
