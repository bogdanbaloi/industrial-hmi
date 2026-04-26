#pragma once

#include "src/integration/Serializer.h"

#include <string>
#include <vector>

namespace app::integration {

/// RFC 4180 CSV serializer with UTF-8 BOM (so Excel detects encoding).
/// Fields containing commas, quotes, or newlines are quoted; embedded
/// quotes are doubled.
///
/// Header row is taken from the constructor argument so the UI can
/// pass localised column labels via `_()`. Empty / default-constructed
/// instances fall back to English column names; readProducts() ignores
/// the header label content but expects exactly 5 columns per the
/// Product schema.
class CsvSerializer final : public Serializer {
public:
    /// Default constructor uses English column labels. Most code paths
    /// (TCP / MQTT export, scenario tests) want this.
    CsvSerializer();

    /// Construct with a translated header row. Used by ProductsPage when
    /// the user clicks Export and we want the header to match the table
    /// they're looking at. The vector should hold exactly 5 entries
    /// (Product Code, Name, Status, Stock, Quality %); shorter / longer
    /// vectors are tolerated but produce ill-formed CSV.
    explicit CsvSerializer(std::vector<std::string> translatedHeader);

    void writeProducts(std::ostream& out,
                       const std::vector<model::Product>& products) override;

    [[nodiscard]] std::vector<model::Product>
        readProducts(std::istream& in) override;

    [[nodiscard]] std::string formatName() const override     { return "CSV"; }
    [[nodiscard]] std::string fileExtension() const override  { return "csv"; }
    [[nodiscard]] std::string mimeType() const override       { return "text/csv"; }

private:
    /// RFC 4180 field escaping: wrap in quotes if value contains
    /// `,` `"` `\n` or `\r`; double any embedded quotes.
    [[nodiscard]] static std::string escapeField(const std::string& field);

    /// One-decimal float (e.g. `98.1`) for the quality rate column.
    [[nodiscard]] static std::string fmtRate(float rate);

    /// Parse one CSV record (handles quoted fields with embedded
    /// commas / newlines / doubled quotes per RFC 4180). Advances
    /// `in` past the record's terminator. Returns the field vector
    /// or empty vector on EOF.
    [[nodiscard]] static std::vector<std::string>
        parseRecord(std::istream& in);

    std::vector<std::string> header_;
};

}  // namespace app::integration
