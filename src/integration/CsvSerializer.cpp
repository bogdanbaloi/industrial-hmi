#include "src/integration/CsvSerializer.h"

#include <charconv>
#include <format>
#include <stdexcept>
#include <string>
#include <system_error>

namespace app::integration {

namespace {
const std::vector<std::string>& defaultHeader() {
    // English column labels matching the Product schema. Stable -- not
    // localised. UI callers that want translated headers pass them via
    // the constructor.
    static const std::vector<std::string> kHeader{
        "Product Code", "Name", "Status", "Stock", "Quality %"};
    return kHeader;
}

// UTF-8 BOM byte sequence (0xEF 0xBB 0xBF). Named because clang-tidy
// (cppcoreguidelines-avoid-magic-numbers) rightly flags raw 0xEF in
// the BOM detection branch otherwise.
constexpr unsigned char kUtf8BomByte0 = 0xEF;
constexpr unsigned char kUtf8BomByte1 = 0xBB;
constexpr unsigned char kUtf8BomByte2 = 0xBF;
}  // namespace

CsvSerializer::CsvSerializer() : header_(defaultHeader()) {}

CsvSerializer::CsvSerializer(std::vector<std::string> translatedHeader)
    : header_(std::move(translatedHeader)) {}

void CsvSerializer::writeProducts(std::ostream& out,
                                  const std::vector<model::Product>& products) {
    // UTF-8 BOM so Excel auto-detects encoding instead of guessing
    // codepage 1252 and mangling non-ASCII column labels.
    out << "\xEF\xBB\xBF";

    for (size_t i = 0; i < header_.size(); ++i) {
        if (i > 0) out << ',';
        out << escapeField(header_[i]);
    }
    out << "\r\n";

    for (const auto& p : products) {
        out << escapeField(p.productCode) << ','
            << escapeField(p.name)        << ','
            << escapeField(p.status)      << ','
            << p.stock                    << ','
            << fmtRate(p.qualityRate)
            << "\r\n";
    }
}

std::vector<model::Product>
CsvSerializer::readProducts(std::istream& in) {
    // Strip optional UTF-8 BOM that writeProducts emits, so a CSV
    // round-trips cleanly.
    if (in.peek() == kUtf8BomByte0) {
        char bom[3]{};
        in.read(bom, 3);
        const bool isBom =
            static_cast<unsigned char>(bom[0]) == kUtf8BomByte0 &&
            static_cast<unsigned char>(bom[1]) == kUtf8BomByte1 &&
            static_cast<unsigned char>(bom[2]) == kUtf8BomByte2;
        if (!isBom) {
            // Not a real BOM -- put the bytes back.
            for (int i = 2; i >= 0; --i) in.putback(bom[i]);
        }
    }

    // Discard the header row -- we don't validate label contents,
    // only the column count via the data rows below.
    (void)parseRecord(in);

    std::vector<model::Product> out;
    while (in.good()) {
        auto fields = parseRecord(in);
        if (fields.empty()) break;          // EOF after a clean record
        if (fields.size() != 5) {
            throw std::runtime_error(
                std::format("CSV row has {} fields, expected 5",
                            fields.size()));
        }

        model::Product p;
        p.productCode = std::move(fields[0]);
        p.name        = std::move(fields[1]);
        p.status      = std::move(fields[2]);

        const auto& stockStr = fields[3];
        if (auto [ptr, ec] = std::from_chars(
                stockStr.data(), stockStr.data() + stockStr.size(), p.stock);
            ec != std::errc{}) {
            throw std::runtime_error("CSV stock not an integer: " + stockStr);
        }

        try {
            p.qualityRate = std::stof(fields[4]);
        } catch (const std::exception&) {
            throw std::runtime_error("CSV quality not a float: " + fields[4]);
        }

        out.push_back(std::move(p));
    }
    return out;
}

std::string CsvSerializer::escapeField(const std::string& field) {
    bool needsQuoting = false;
    for (char c : field) {
        if (c == ',' || c == '"' || c == '\n' || c == '\r') {
            needsQuoting = true;
            break;
        }
    }
    if (!needsQuoting) return field;

    std::string escaped = "\"";
    for (char c : field) {
        if (c == '"') escaped += "\"\"";
        else escaped += c;
    }
    escaped += '"';
    return escaped;
}

std::string CsvSerializer::fmtRate(float rate) {
    return std::vformat("{:.1f}", std::make_format_args(rate));
}

std::vector<std::string> CsvSerializer::parseRecord(std::istream& in) {
    std::vector<std::string> fields;
    std::string current;
    bool inQuotes = false;
    bool sawAnyChar = false;

    while (true) {
        const int next = in.get();
        if (next == EOF) {
            // Trailing data without a final newline -- emit the
            // accumulated field if we saw anything.
            if (sawAnyChar || !fields.empty()) fields.push_back(current);
            return fields;
        }
        sawAnyChar = true;
        const char c = static_cast<char>(next);

        if (inQuotes) {
            if (c == '"') {
                // Doubled quote inside a quoted field -- emit one quote.
                if (in.peek() == '"') {
                    current += '"';
                    in.get();
                } else {
                    inQuotes = false;
                }
            } else {
                current += c;
            }
            continue;
        }

        if (c == ',') {
            fields.push_back(std::move(current));
            current.clear();
        } else if (c == '\r') {
            // Eat optional \n that completes a CRLF terminator.
            if (in.peek() == '\n') in.get();
            fields.push_back(std::move(current));
            return fields;
        } else if (c == '\n') {
            fields.push_back(std::move(current));
            return fields;
        } else if (c == '"' && current.empty()) {
            // Quote at field start opens a quoted field.
            inQuotes = true;
        } else {
            current += c;
        }
    }
}

}  // namespace app::integration
