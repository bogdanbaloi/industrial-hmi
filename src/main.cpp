#include "src/core/Bootstrap.h"
#include "src/core/StartupDialog.h"
#include "src/core/StartupErrors.h"
#include "src/config/ConfigManager.h"
#include "src/integration/IntegrationManager.h"
#include "src/integration/MqttPublisher.h"
#include "src/integration/ProductionTelemetryBridge.h"
#include "src/integration/TcpBackend.h"
#include "src/model/DatabaseManager.h"
#include "src/model/SimulatedModel.h"
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <memory>
#include <utility>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>   // SetConsoleOutputCP
#  include <fcntl.h>
#  include <io.h>
#  include <clocale>
#endif

#ifdef CONSOLE_MODE
#  include "src/console/InitConsole.h"
#else
#  include "src/core/Application.h"
#endif

namespace {

// Compile-time tag: true for the console binary, false for the GTK one.
// Drives the fatal-reporter between stderr and MessageBoxW.
constexpr bool kConsoleMode =
#ifdef CONSOLE_MODE
    true;
#else
    false;
#endif

// Process exit codes -- documented so CI / shell scripts can branch on them.
// Marked [[maybe_unused]] because the set used by a given build depends
// on which branch of the CONSOLE_MODE #ifdef is active (the GTK path
// propagates the GTK main-loop's own return code via `app.run(...)`).
[[maybe_unused]] constexpr int kExitOk              = 0;
[[maybe_unused]] constexpr int kExitUnexpectedFatal = 1;
[[maybe_unused]] constexpr int kExitStartupFatal    = 2;
[[maybe_unused]] constexpr int kExitUnknownFatal    = 3;

}  // namespace

int main(int argc, char* argv[]) {
#ifdef _WIN32
    // Use Cairo renderer on Windows to avoid GL flicker in GTK4.
    _putenv_s("GSK_RENDERER", "cairo");

    // Windows UTF-8 setup (three layers all need cooperating):
    //
    //   1. Win32 console codepage -- applies when stdout is attached to
    //      a real cmd.exe console.
    //   2. CRT locale -- controls how the C runtime interprets bytes
    //      in fprintf / wide-conversion paths. ".UTF-8" is supported
    //      from Windows 10 v1803; older systems silently fall back.
    //   3. Binary mode on stdout -- stops the CRT from doing LF->CRLF
    //      and codepage conversion when stdout is a pipe (Git Bash /
    //      mintty). Without this, UTF-8 sequences get mangled into
    //      Latin-1 mojibake even though bytes were written verbatim.
    //
    // All three together cover console + pipe + mintty use cases.
    // Harmless for the GTK build; critical for the console frontend.
    ::SetConsoleOutputCP(CP_UTF8);
    ::SetConsoleCP(CP_UTF8);
    std::setlocale(LC_ALL, ".UTF-8");
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
#endif

    // Top-level exception guard. Every fatal condition at or below
    // Bootstrap / Application / InitConsole surfaces here and is
    // reported via the appropriate native channel.
    //
    //   kExitStartupFatal (2)  -> deployment problem (config / DB)
    //   kExitUnexpectedFatal (1) -> std::exception escaping the app
    //   kExitUnknownFatal (3)  -> non-std exception (shouldn't happen)
    try {
        // Staged startup: logger -> config -> configured logger -> i18n.
        // Throws CriticalStartupError on fatal config issues.
        app::core::Bootstrap bootstrap;
        bootstrap.run();

        // Integration backends. Opt-in per deployment via app-config.json.
        // Both front-ends (GTK + console) get the same network surface so
        // the binary is identical in either build (zero gtkmm leak path
        // even with TCP/MQTT enabled).
        //
        // Lifetime contract: manager + bridge live until the end of main,
        // so backends keep running until the front-end exits and the
        // catch-block / RAII shuts them down via stopAll().
        auto& config = app::config::ConfigManager::instance();
        app::integration::IntegrationManager integration;
        std::unique_ptr<app::integration::ProductionTelemetryBridge>
            productionBridge;

        if (config.isTcpBackendEnabled()) {
            integration.registerBackend(
                std::make_unique<app::integration::TcpBackend>(
                    static_cast<std::uint16_t>(config.getTcpBackendPort()),
                    app::model::SimulatedModel::instance(),
                    app::model::DatabaseManager::instance()));
        }

        if (config.isMqttBackendEnabled()) {
            app::integration::MqttPublisher::Config mqttConfig;
            mqttConfig.brokerHost = config.getMqttBrokerHost();
            mqttConfig.brokerPort =
                static_cast<std::uint16_t>(config.getMqttBrokerPort());
            mqttConfig.clientId = config.getMqttClientId();
            auto publisher =
                std::make_unique<app::integration::MqttPublisher>(
                    std::move(mqttConfig));

            // The bridge subscribes to the production model and pushes
            // through the publisher's TelemetryPublisher interface --
            // it doesn't know or care that the underlying transport is
            // MQTT. Swap in any other publisher (Kafka, AMQP, custom)
            // with a single line change here.
            app::integration::ProductionTelemetryBridge::Config bridgeConfig;
            bridgeConfig.topicPrefix = config.getMqttTopicPrefix();
            productionBridge =
                std::make_unique<app::integration::ProductionTelemetryBridge>(
                    *publisher,
                    app::model::SimulatedModel::instance(),
                    std::move(bridgeConfig));
            productionBridge->wire();

            integration.registerBackend(std::move(publisher));
        }

        integration.startAll();

#ifdef CONSOLE_MODE
        (void)argc; (void)argv;
        app::console::InitConsole console(bootstrap);
        console.run();
        integration.stopAll();
        return kExitOk;
#else
        auto& app = app::core::Application::instance();
        app.initialize(bootstrap, argc, argv);   // throws DatabaseInitError on DB failure

        // Inject the app-wide logger into the SimulatedModel singleton so
        // its state transitions and tick traces show up in the normal log
        // stream. Done here (not in Application::initDatabase) because
        // including SimulatedModel.h there would pull in
        // ProductionTypes::ERROR after gtkmm has already defined the
        // wingdi.h ERROR=0 macro.
        app::model::SimulatedModel::instance().setLogger(app.logger());

        const int result = app.run(argc, argv);
        integration.stopAll();
        app.shutdown();
        return result;
#endif
    } catch (const app::core::CriticalStartupError& e) {
        app::core::reportFatalStartup(e, kConsoleMode);
        return kExitStartupFatal;
    } catch (const std::exception& e) {
        app::core::reportUnexpectedFatal(e.what(), kConsoleMode);
        return kExitUnexpectedFatal;
    } catch (...) {
        app::core::reportUnexpectedFatal(
            "Unknown (non-std::exception) fatal error reached main.",
            kConsoleMode);
        return kExitUnknownFatal;
    }
}
