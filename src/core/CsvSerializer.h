#pragma once

#include "src/model/Product.h"

#include <ostream>
#include <string>
#include <vector>

namespace app::core {

/// Writes a collection of Product rows as RFC 4180 CSV with a UTF-8 BOM
/// (so Excel opens it correctly). Fields that contain commas, quotes, or
/// newlines are quoted; embedded quotes are escaped as "".
///
/// The serializer is a free function operating on std::ostream so it can
/// target a file, a string stream (for tests), or any other sink.
///
/// Header columns: Product Code, Name, Status, Stock, Quality (%)
class CsvSerializer {
public:
    /// Write UTF-8 BOM + header + one row per product.
    /// @param out  Destination stream (must be opened in binary mode on
    ///             Windows to prevent \r\n double-expansion).
    /// @param products  Rows to serialize.
    /// @param header  Column names (pass translated strings from the UI).
    static void write(std::ostream& out,
                      const std::vector<model::Product>& products,
                      const std::vector<std::string>& header);

private:
    /// Escape a single field value per RFC 4180:
    /// if it contains a comma, double-quote, or newline, wrap it in quotes
    /// and double any existing quotes.
    static std::string escapeField(const std::string& field);

    /// Format a float with one decimal place (e.g. 98.1).
    static std::string fmtRate(float rate);
};

}  // namespace app::core
