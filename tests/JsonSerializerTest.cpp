// Tests for app::integration::JsonSerializer.
//
// Pure C++ logic (no GTK, no singletons). Covers:
//   * Empty / single / multi-row write output shape
//   * RFC 8259 string escaping (quotes, backslashes, newlines, controls)
//   * Numeric field rendering (integer stock, 1-decimal qualityRate)
//   * Round-trip parse-of-write
//   * Error paths: malformed input, bad escapes, mismatched braces

#include "src/integration/JsonSerializer.h"

#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <vector>

using app::integration::JsonSerializer;
using app::integration::Serializer;
using app::model::Product;

namespace {

Product make(const std::string& code, const std::string& name,
             const std::string& status, int stock, float quality) {
    Product p;
    p.productCode = code;
    p.name = name;
    p.status = status;
    p.stock = stock;
    p.qualityRate = quality;
    return p;
}

}  // namespace

// Output shape

TEST(JsonSerializerTest, EmptyListProducesEmptyArray) {
    JsonSerializer s;
    std::ostringstream out;
    s.writeProducts(out, {});

    EXPECT_EQ(out.str(), "[]\n");
}

TEST(JsonSerializerTest, SingleRowHasAllSchemaFields) {
    JsonSerializer s;
    std::ostringstream out;
    s.writeProducts(out, {make("PROD-001", "Widget A", "Active", 850, 98.1f)});

    const std::string body = out.str();
    EXPECT_NE(body.find("\"productCode\": \"PROD-001\""), std::string::npos);
    EXPECT_NE(body.find("\"name\": \"Widget A\""),       std::string::npos);
    EXPECT_NE(body.find("\"status\": \"Active\""),       std::string::npos);
    EXPECT_NE(body.find("\"stock\": 850"),               std::string::npos);
    EXPECT_NE(body.find("\"qualityRate\": 98.1"),        std::string::npos);
}

TEST(JsonSerializerTest, MultiRowEntriesAreCommaSeparated) {
    JsonSerializer s;
    std::ostringstream out;
    s.writeProducts(out,
        {make("A", "Alpha", "Active", 1, 99.0f),
         make("B", "Beta",  "Active", 2, 88.0f)});

    const std::string body = out.str();
    // Two opening braces means two object entries.
    size_t opens = 0;
    for (char c : body) if (c == '{') ++opens;
    EXPECT_EQ(opens, 2u);
    // Comma between objects (between } and {).
    EXPECT_NE(body.find("},"), std::string::npos);
}

// Escaping

TEST(JsonSerializerTest, EscapesQuotesInsideStrings) {
    JsonSerializer s;
    std::ostringstream out;
    s.writeProducts(out, {make("X", "Size 2\"", "Active", 1, 50.0f)});

    EXPECT_NE(out.str().find("\"name\": \"Size 2\\\"\""), std::string::npos);
}

TEST(JsonSerializerTest, EscapesBackslash) {
    JsonSerializer s;
    std::ostringstream out;
    s.writeProducts(out, {make("X", "C:\\path", "Active", 1, 50.0f)});

    // \\ in source = single backslash in string literal; in JSON output
    // the backslash itself becomes \\ (two characters).
    EXPECT_NE(out.str().find("\"name\": \"C:\\\\path\""), std::string::npos);
}

TEST(JsonSerializerTest, EscapesNewlineAndTab) {
    JsonSerializer s;
    std::ostringstream out;
    s.writeProducts(out, {make("X", "L1\nL2\tEND", "Active", 1, 50.0f)});

    EXPECT_NE(out.str().find("\"name\": \"L1\\nL2\\tEND\""), std::string::npos);
}

TEST(JsonSerializerTest, EscapesAsciiControlChars) {
    JsonSerializer s;
    std::ostringstream out;
    // 0x01 (SOH) -- below 0x20 so should become \u0001.
    s.writeProducts(out, {make("X", std::string("a\x01""b"), "Active", 1, 50.0f)});

    EXPECT_NE(out.str().find("\\u0001"), std::string::npos);
}

// Round-trip

TEST(JsonSerializerTest, RoundTripPreservesAllFields) {
    JsonSerializer s;
    std::vector<Product> original{
        make("PROD-001", "Widget A", "Active", 850, 98.1f),
        make("PROD-002", "Beta with, comma", "Low Stock", 5, 72.5f),
        make("PROD-003", "Quote\"Test", "Inactive", 0, 0.0f),
    };

    std::ostringstream out;
    s.writeProducts(out, original);

    std::istringstream in(out.str());
    auto parsed = s.readProducts(in);

    ASSERT_EQ(parsed.size(), original.size());
    for (size_t i = 0; i < parsed.size(); ++i) {
        EXPECT_EQ(parsed[i].productCode, original[i].productCode) << "row " << i;
        EXPECT_EQ(parsed[i].name,        original[i].name)        << "row " << i;
        EXPECT_EQ(parsed[i].status,      original[i].status)      << "row " << i;
        EXPECT_EQ(parsed[i].stock,       original[i].stock)       << "row " << i;
        EXPECT_FLOAT_EQ(parsed[i].qualityRate, original[i].qualityRate)
            << "row " << i;
    }
}

TEST(JsonSerializerTest, RoundTripEmptyArray) {
    JsonSerializer s;
    std::ostringstream out;
    s.writeProducts(out, {});

    std::istringstream in(out.str());
    auto parsed = s.readProducts(in);
    EXPECT_TRUE(parsed.empty());
}

// Parse error paths

TEST(JsonSerializerTest, ReadThrowsOnUnclosedArray) {
    JsonSerializer s;
    std::istringstream in("[ {\"productCode\": \"X\"");
    // void-cast pacifies Windows Clang's -Werror=unused-result
    // (readProducts is [[nodiscard]] but EXPECT_THROW drops the value).
    EXPECT_THROW((void)s.readProducts(in), std::runtime_error);
}

TEST(JsonSerializerTest, ReadThrowsOnMissingOpeningBracket) {
    JsonSerializer s;
    std::istringstream in("{\"productCode\": \"X\"}");
    // void-cast pacifies Windows Clang's -Werror=unused-result
    // (readProducts is [[nodiscard]] but EXPECT_THROW drops the value).
    EXPECT_THROW((void)s.readProducts(in), std::runtime_error);
}

TEST(JsonSerializerTest, ReadThrowsOnUnknownEscape) {
    JsonSerializer s;
    std::istringstream in("[{\"name\": \"\\q\"}]");
    // void-cast pacifies Windows Clang's -Werror=unused-result
    // (readProducts is [[nodiscard]] but EXPECT_THROW drops the value).
    EXPECT_THROW((void)s.readProducts(in), std::runtime_error);
}

TEST(JsonSerializerTest, ReadIgnoresUnknownKeys) {
    // Forward-compat: a JSON written by a future version with extra
    // fields should still parse, ignoring fields we don't know.
    JsonSerializer s;
    std::istringstream in(
        "[{\"productCode\": \"X\", \"name\": \"Test\", \"status\": \"Active\","
        " \"stock\": 1, \"qualityRate\": 50.0,"
        " \"futureField\": \"ignored\", \"futureNumber\": 42}]");

    std::vector<Product> parsed;
    EXPECT_NO_THROW(parsed = s.readProducts(in));
    ASSERT_EQ(parsed.size(), 1u);
    EXPECT_EQ(parsed[0].productCode, "X");
}

// Metadata accessors

TEST(JsonSerializerTest, MetadataAccessors) {
    JsonSerializer s;
    EXPECT_EQ(s.formatName(),    "JSON");
    EXPECT_EQ(s.fileExtension(), "json");
    EXPECT_EQ(s.mimeType(),      "application/json");
}

// Substitutability via base reference (Liskov check)

TEST(JsonSerializerTest, UsableThroughSerializerReference) {
    JsonSerializer concrete;
    Serializer& base = concrete;

    std::ostringstream out;
    base.writeProducts(out, {make("X", "Test", "Active", 1, 50.0f)});

    EXPECT_NE(out.str().find("\"productCode\": \"X\""), std::string::npos);
    EXPECT_EQ(base.formatName(), "JSON");
}
