#pragma once

#include "src/integration/Serializer.h"

#include <string>
#include <vector>

namespace app::integration {

/// Minimal hand-rolled JSON serializer for the Product schema.
///
/// Output shape:
///   [
///     {"productCode": "PROD-001", "name": "Widget A",
///      "status": "Active", "stock": 850, "qualityRate": 98.1},
///     ...
///   ]
///
/// Why hand-rolled (no nlohmann/json or rapidjson)?
///   * The Product struct is fixed and small (5 fields). The full
///     write+read round-trip fits in ~100 lines.
///   * Adding a third-party JSON dep means another package on
///     Ubuntu + MSYS2 + vcpkg, more CI install time, more attack
///     surface. For a 5-field POD that ships internally, not worth it.
///   * The implementation is portfolio-relevant: it shows the author
///     can write a small, correct parser by hand.
///
/// Format choices:
///   * Pretty-printed with 2-space indent so humans can read the file
///     without piping through `jq`.
///   * Numeric fields stay numeric (no quoting): stock as integer,
///     qualityRate as decimal with one digit of precision.
///   * Strings escape `"`, `\`, control chars per RFC 8259; non-ASCII
///     UTF-8 passes through verbatim (bytes are valid UTF-8 by
///     construction in our domain).
class JsonSerializer final : public Serializer {
public:
    void writeProducts(std::ostream& out,
                       const std::vector<model::Product>& products) override;

    [[nodiscard]] std::vector<model::Product>
        readProducts(std::istream& in) override;

    [[nodiscard]] std::string formatName() const override     { return "JSON"; }
    [[nodiscard]] std::string fileExtension() const override  { return "json"; }
    [[nodiscard]] std::string mimeType() const override       { return "application/json"; }

private:
    /// Escape a string per RFC 8259 (quotes, backslash, control chars,
    /// no ASCII < 0x20). Returns WITHOUT surrounding quotes.
    [[nodiscard]] static std::string escapeString(const std::string& s);

    /// Format float with one decimal of precision (matches CSV).
    [[nodiscard]] static std::string fmtRate(float rate);
};

}  // namespace app::integration
