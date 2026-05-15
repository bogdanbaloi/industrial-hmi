#pragma once

#include <cstdint>
#include <string_view>

namespace app::historian {

/// Which scalar series a sample belongs to. Kept as a small enum so the
/// storage schema can index `(field, entityId)` efficiently and the
/// presentation layer can pick units / colours per series.
///
/// Adding a new series: append a value, extend the `to_string` switch in
/// SqliteHistoryStore, and the bridge can wire it. Existing rows in the
/// archive keep their integer codes (do not reorder).
enum class FieldKind : std::uint8_t {
    /// Quality checkpoint pass rate, 0..100 (%). entityId = checkpoint id.
    QualityPassRate     = 0,

    /// Equipment supply level, 0..100 (%). entityId = equipment slot id.
    EquipmentSupplyLevel = 1,

    /// Top-level system state encoded as int (0=IDLE, 1=RUNNING,
    /// 2=ERROR, 3=CALIBRATION). entityId = 0 (single global series).
    /// Stored as float for schema uniformity even though it's a small
    /// integer set; the History page renders this as a stepped strip
    /// rather than a continuous line.
    SystemState         = 2,
};

/// Stable single-character codes used in the SQLite schema so a human
/// browsing the DB with `sqlite3` can tell what each row is without
/// joining a lookup table. Kept short to keep the schema compact when
/// the archive grows past millions of rows.
[[nodiscard]] constexpr std::string_view fieldCode(FieldKind f) noexcept {
    switch (f) {
        case FieldKind::QualityPassRate:      return "Q";
        case FieldKind::EquipmentSupplyLevel: return "S";
        case FieldKind::SystemState:          return "T";
    }
    return "?";
}

/// A single time-series sample. Plain aggregate -- no invariants, no
/// validation; the writer side trusts callers to produce sane values
/// (clamp at the model, not here).
///
/// Wall-clock timestamps in milliseconds since the Unix epoch. We use
/// integer ms rather than `std::chrono::system_clock::time_point` in the
/// DTO because:
///   * The SQLite schema stores INTEGER for fast range scans and
///     comparison; no codec round-trip needed.
///   * Cross-binary stability: ConsoleView and the GTK app agree on
///     a single representation regardless of which clock source they
///     happened to compile against.
///   * Test fixtures inject explicit ms values without needing a
///     FakeClock to override system_clock.
struct HistoryRecord {
    std::int64_t  timestampMs{0};
    FieldKind     field{FieldKind::QualityPassRate};
    std::uint32_t entityId{0};
    float         value{0.0F};
};

}  // namespace app::historian
