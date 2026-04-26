#include "src/integration/JsonSerializer.h"

#include <cctype>
#include <charconv>
#include <cstdint>
#include <format>
#include <stdexcept>
#include <string>
#include <system_error>

namespace app::integration {

namespace {

/// Skip whitespace per RFC 8259 (space, tab, LF, CR).
void skipWs(std::istream& in) {
    while (in.good()) {
        const int c = in.peek();
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            in.get();
        } else {
            return;
        }
    }
}

/// Consume an exact literal (e.g. "true", "null"). Throws on mismatch.
void expect(std::istream& in, char c) {
    skipWs(in);
    const int got = in.get();
    if (got != c) {
        throw std::runtime_error(
            std::format("JSON: expected '{}' at offset {}, got {}",
                        c, static_cast<std::int64_t>(in.tellg()),
                        got == EOF ? std::string{"EOF"}
                                   : std::string(1, static_cast<char>(got))));
    }
}

/// Parse a JSON string literal (starts at the opening quote).
[[nodiscard]] std::string parseString(std::istream& in) {
    expect(in, '"');
    std::string out;
    while (true) {
        const int c = in.get();
        if (c == EOF) {
            throw std::runtime_error("JSON: unexpected EOF inside string");
        }
        if (c == '"') return out;
        if (c == '\\') {
            const int esc = in.get();
            switch (esc) {
                case '"':  out += '"';  break;
                case '\\': out += '\\'; break;
                case '/':  out += '/';  break;
                case 'b':  out += '\b'; break;
                case 'f':  out += '\f'; break;
                case 'n':  out += '\n'; break;
                case 'r':  out += '\r'; break;
                case 't':  out += '\t'; break;
                // \uXXXX is allowed by spec but our domain doesn't
                // produce escapes (UTF-8 passes through), so we treat
                // it as malformed input rather than carry a unicode
                // decoder we never exercise.
                case 'u':
                    throw std::runtime_error(
                        "JSON: \\u escapes not supported in this serializer");
                default:
                    throw std::runtime_error(
                        std::format("JSON: bad escape \\{}",
                                    static_cast<char>(esc)));
            }
            continue;
        }
        out += static_cast<char>(c);
    }
}

/// Parse a JSON number into a string (lets the caller decide int vs float).
[[nodiscard]] std::string parseNumber(std::istream& in) {
    skipWs(in);
    std::string out;
    while (in.good()) {
        const int c = in.peek();
        if (c == EOF) break;
        const char ch = static_cast<char>(c);
        if (std::isdigit(static_cast<unsigned char>(ch)) ||
            ch == '-' || ch == '+' || ch == '.' ||
            ch == 'e' || ch == 'E') {
            out += ch;
            in.get();
        } else {
            break;
        }
    }
    if (out.empty()) {
        throw std::runtime_error("JSON: expected number");
    }
    return out;
}

}  // namespace

void JsonSerializer::writeProducts(std::ostream& out,
                                   const std::vector<model::Product>& products) {
    if (products.empty()) {
        out << "[]\n";
        return;
    }

    out << "[\n";
    for (size_t i = 0; i < products.size(); ++i) {
        const auto& p = products[i];
        // Raw string literals avoid escape-sequence noise in the
        // pretty-printed JSON skeleton (modernize-raw-string-literal
        // would flag the escaped-quote form otherwise).
        out << R"(  {)" << "\n"
            << R"(    "productCode": ")" << escapeString(p.productCode) << R"(",)" << "\n"
            << R"(    "name": ")"        << escapeString(p.name)        << R"(",)" << "\n"
            << R"(    "status": ")"      << escapeString(p.status)      << R"(",)" << "\n"
            << R"(    "stock": )"        << p.stock                     << ",\n"
            << R"(    "qualityRate": )"  << fmtRate(p.qualityRate)      << "\n"
            << R"(  })";
        if (i + 1 < products.size()) out << ',';
        out << '\n';
    }
    out << "]\n";
}

std::vector<model::Product>
JsonSerializer::readProducts(std::istream& in) {
    std::vector<model::Product> result;

    skipWs(in);
    expect(in, '[');
    skipWs(in);

    // Empty array shortcut.
    if (in.peek() == ']') {
        in.get();
        return result;
    }

    while (true) {
        skipWs(in);
        expect(in, '{');

        model::Product p;
        bool first = true;
        while (true) {
            skipWs(in);
            if (in.peek() == '}') {
                in.get();
                break;
            }
            if (!first) {
                expect(in, ',');
                skipWs(in);
            }
            first = false;

            const std::string key = parseString(in);
            skipWs(in);
            expect(in, ':');
            skipWs(in);

            if (key == "productCode")      p.productCode = parseString(in);
            else if (key == "name")        p.name        = parseString(in);
            else if (key == "status")      p.status      = parseString(in);
            else if (key == "stock") {
                const std::string s = parseNumber(in);
                if (auto [ptr, ec] = std::from_chars(
                        s.data(), s.data() + s.size(), p.stock);
                    ec != std::errc{}) {
                    throw std::runtime_error("JSON stock not an integer: " + s);
                }
            }
            else if (key == "qualityRate") {
                const std::string s = parseNumber(in);
                p.qualityRate = std::stof(s);
            }
            else {
                // Unknown key -- skip the value rather than fail, so
                // callers can extend the schema additively.
                if (in.peek() == '"') {
                    (void)parseString(in);
                } else {
                    (void)parseNumber(in);
                }
            }
        }
        result.push_back(std::move(p));

        skipWs(in);
        const int after = in.get();
        if (after == ']') return result;
        if (after != ',') {
            throw std::runtime_error(
                std::format("JSON: expected ',' or ']' between objects, got {}",
                            after == EOF ? std::string{"EOF"}
                                         : std::string(1, static_cast<char>(after))));
        }
    }
}

std::string JsonSerializer::escapeString(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    out += std::format("\\u{:04x}",
                                       static_cast<unsigned char>(c));
                } else {
                    out += c;
                }
        }
    }
    return out;
}

std::string JsonSerializer::fmtRate(float rate) {
    return std::vformat("{:.1f}", std::make_format_args(rate));
}

}  // namespace app::integration
