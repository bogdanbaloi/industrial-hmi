#include "src/core/CsvSerializer.h"

#include <cstdio>

namespace app::core {

void CsvSerializer::write(std::ostream& out,
                          const std::vector<model::Product>& products,
                          const std::vector<std::string>& header) {
    // UTF-8 BOM so Excel detects encoding automatically
    out << "\xEF\xBB\xBF";

    // Header row
    for (size_t i = 0; i < header.size(); ++i) {
        if (i > 0) out << ',';
        out << escapeField(header[i]);
    }
    out << "\r\n";

    // Data rows
    for (const auto& p : products) {
        out << escapeField(p.productCode) << ','
            << escapeField(p.name) << ','
            << escapeField(p.status) << ','
            << p.stock << ','
            << fmtRate(p.qualityRate)
            << "\r\n";
    }
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
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.1f", rate);
    return buf;
}

}  // namespace app::core
