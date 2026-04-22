#pragma once

#include "src/core/StartupErrors.h"

#include <exception>
#include <string_view>

namespace app::core {

/// Report a fatal startup condition to the user via the most appropriate
/// native channel available at this stage of startup (i.e. before any
/// application main loop exists):
///
///   - Windows GUI mode:   MessageBoxW modal, MB_OK | MB_ICONERROR
///   - Console mode / Linux / fallback: a structured line on stderr
///
/// Never throws. Blocks on Windows until the user dismisses the dialog.
/// Returns immediately on console / Linux so the caller can finish its
/// cleanup and return a non-zero exit code.
///
/// The reporter intentionally does NOT invoke GTK: it must run in the
/// window between main() entry and Application::initialize(), when the
/// GTK application object does not yet exist. Keeping the dependency
/// list to OS natives + libc also means this path never fails for the
/// same reason the startup did.
void reportFatalStartup(const CriticalStartupError& error, bool consoleMode) noexcept;

/// Same surface but for std::exception that isn't a CriticalStartupError
/// (unexpected runtime fault escaping main). The dialog/stderr message
/// is framed as "Unexpected fatal error" rather than a deployment issue.
void reportUnexpectedFatal(std::string_view message, bool consoleMode) noexcept;

}  // namespace app::core
