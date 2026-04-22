#include "src/core/StartupDialog.h"

#include <cstdio>
#include <string>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <cstdlib>  // mbstowcs_s
#endif

namespace app::core {

namespace {

#ifdef _WIN32
/// Convert UTF-8 to UTF-16 for Win32 Unicode APIs. Best-effort: if the
/// conversion fails we fall through with an empty string so the caller
/// can still show *something* (the tag) to the user.
std::wstring utf8ToWide(std::string_view s) {
    if (s.empty()) return {};
    const int len = MultiByteToWideChar(
        CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (len <= 0) return {};
    std::wstring out(static_cast<std::size_t>(len), L'\0');
    MultiByteToWideChar(
        CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), len);
    return out;
}

void showNativeDialog(std::string_view title, std::string_view body) noexcept {
    const std::wstring wTitle = utf8ToWide(title);
    const std::wstring wBody  = utf8ToWide(body);
    ::MessageBoxW(nullptr,
                  wBody.empty()  ? L""                   : wBody.c_str(),
                  wTitle.empty() ? L"Industrial HMI"     : wTitle.c_str(),
                  MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST);
}
#endif

void writeStderr(std::string_view tag,
                 std::string_view body) noexcept {
    // Deliberately use the C API — std::format / iostreams could
    // themselves throw, and this function must never throw.
    std::fprintf(stderr, "\n*** %.*s ***\n%.*s\n\n",
                 static_cast<int>(tag.size()),  tag.data(),
                 static_cast<int>(body.size()), body.data());
    std::fflush(stderr);
}

}  // namespace

void reportFatalStartup(const CriticalStartupError& error,
                        bool consoleMode) noexcept {
    const std::string_view tag  = toTag(error.code());
    const std::string_view body = error.what();

    if (consoleMode) {
        writeStderr(tag, body);
        return;
    }

#ifdef _WIN32
    // Windows GUI mode: native message box. stderr is usually invisible
    // when the binary is launched from Explorer / a shortcut.
    const std::string title = "Industrial HMI - " + std::string(tag);
    showNativeDialog(title, body);
#else
    // Linux GUI mode: prefer stderr (always visible when launched from
    // a terminal; matches `systemd` journal behaviour for services).
    // A future refinement can shell out to `zenity --error` when it's
    // on PATH, but the plain stderr route is 100% portable.
    writeStderr(tag, body);
#endif
}

void reportUnexpectedFatal(std::string_view message,
                           bool consoleMode) noexcept {
    constexpr std::string_view kTag = "UNEXPECTED FATAL ERROR";

    if (consoleMode) {
        writeStderr(kTag, message);
        return;
    }

#ifdef _WIN32
    const std::string title = "Industrial HMI - Unexpected Fatal Error";
    showNativeDialog(title, message);
#else
    writeStderr(kTag, message);
#endif
}

}  // namespace app::core
