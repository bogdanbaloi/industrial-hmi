#include "src/core/TimeFormat.h"

#include <array>
#include <charconv>
#include <ctime>
#include <string>
#include <string_view>
#include <system_error>

namespace app::core {

std::string formatIso8601Local(std::string_view iso8601Utc) {
    // ISO 8601 "YYYY-MM-DDTHH:MM:SSZ" is exactly 20 characters; anything
    // shorter cannot carry the expected layout.
    constexpr std::size_t kIso8601Length = 20;
    // std::tm::tm_year is years since 1900 (POSIX convention).
    constexpr int kTmYearEpoch = 1900;

    // Per-field offsets inside "YYYY-MM-DDTHH:MM:SSZ".
    constexpr std::size_t kYearOffset  = 0;
    constexpr std::size_t kYearLength  = 4;
    constexpr std::size_t kMonthOffset = 5;
    constexpr std::size_t kDayOffset   = 8;
    constexpr std::size_t kHourOffset  = 11;
    constexpr std::size_t kMinOffset   = 14;
    constexpr std::size_t kSecOffset   = 17;
    constexpr std::size_t kFieldWidth  = 2;

    if (iso8601Utc.size() < kIso8601Length) {
        return std::string{iso8601Utc};
    }

    // Manual field-by-field parse using std::from_chars. Avoids
    // sscanf (flagged by cert-err34-c) + strptime's locale wobble on
    // %Y, and gives us per-field error codes without throwing.
    //
    // Layout: YYYY-MM-DDTHH:MM:SSZ
    // Index:  0123456789...
    const auto* s = iso8601Utc.data();
    auto parseField = [s](std::size_t offset, std::size_t length,
                          int& out) {
        const auto* first = s + offset;
        const auto* last  = first + length;
        const auto r = std::from_chars(first, last, out);
        return r.ec == std::errc{} && r.ptr == last;
    };

    std::tm tm{};
    if (!parseField(kYearOffset,  kYearLength, tm.tm_year)
        || !parseField(kMonthOffset, kFieldWidth, tm.tm_mon)
        || !parseField(kDayOffset,   kFieldWidth, tm.tm_mday)
        || !parseField(kHourOffset,  kFieldWidth, tm.tm_hour)
        || !parseField(kMinOffset,   kFieldWidth, tm.tm_min)
        || !parseField(kSecOffset,   kFieldWidth, tm.tm_sec)) {
        return std::string{iso8601Utc};
    }
    tm.tm_year -= kTmYearEpoch;
    tm.tm_mon  -= 1;  // tm_mon is 0-based

    // Convert UTC tm to time_t (epoch seconds). timegm is glibc;
    // _mkgmtime is the MSVC/MinGW equivalent.
#if defined(_WIN32)
    const std::time_t epoch = ::_mkgmtime(&tm);
#else
    const std::time_t epoch = ::timegm(&tm);
#endif
    if (epoch == static_cast<std::time_t>(-1)) {
        return std::string{iso8601Utc};
    }

    std::tm local{};
#if defined(_WIN32)
    ::localtime_s(&local, &epoch);
#else
    ::localtime_r(&epoch, &local);
#endif

    std::array<char, 32> buf{};
    if (std::strftime(buf.data(), buf.size(),
                      "%Y-%m-%d %H:%M:%S", &local) == 0) {
        return std::string{iso8601Utc};
    }
    return std::string{buf.data()};
}

}  // namespace app::core
