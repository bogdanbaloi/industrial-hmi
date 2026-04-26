// Tests for app::integration::CsvSerializer.
//
// CsvSerializer is the CSV concrete of the Serializer interface introduced
// alongside JsonSerializer. Tests cover:
//   * Output shape (UTF-8 BOM + header row + data rows, CRLF terminators)
//   * RFC 4180 field escaping (commas, quotes, newlines)
//   * Translated header support via constructor (i18n use case)
//   * Round-trip parse-of-write (new -- the original CsvSerializer was
//     write-only)

#include "src/integration/CsvSerializer.h"

#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <vector>

using app::integration::CsvSerializer;
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

// Split a CSV string into lines (CRLF-delimited).
std::vector<std::string> lines(const std::string& csv) {
    std::vector<std::string> result;
    std::istringstream ss(csv);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        result.push_back(line);
    }
    return result;
}

}  // namespace

// Output format

TEST(CsvSerializerTest, EmptyListProducesHeaderOnly) {
    CsvSerializer s;
    std::ostringstream out;
    s.writeProducts(out, {});
    auto l = lines(out.str());

    ASSERT_GE(l.size(), 1u);
    EXPECT_EQ(out.str().substr(0, 3), "\xEF\xBB\xBF");
    EXPECT_EQ(l[0].substr(3), "Product Code,Name,Status,Stock,Quality %");
}

TEST(CsvSerializerTest, SingleRowSerializesCorrectly) {
    CsvSerializer s;
    std::ostringstream out;
    s.writeProducts(out, {make("PROD-001", "Widget A", "Active", 850, 98.1f)});
    auto l = lines(out.str());

    ASSERT_GE(l.size(), 2u);
    EXPECT_EQ(l[1], "PROD-001,Widget A,Active,850,98.1");
}

TEST(CsvSerializerTest, MultipleRowsInOrder) {
    CsvSerializer s;
    std::ostringstream out;
    s.writeProducts(out,
        {make("A", "Alpha", "Active", 1, 99.0f),
         make("B", "Beta", "Inactive", 0, 0.0f),
         make("C", "Gamma", "Low Stock", 5, 92.3f)});
    auto l = lines(out.str());

    ASSERT_GE(l.size(), 4u);
    EXPECT_EQ(l[1], "A,Alpha,Active,1,99.0");
    EXPECT_EQ(l[2], "B,Beta,Inactive,0,0.0");
    EXPECT_EQ(l[3], "C,Gamma,Low Stock,5,92.3");
}

// RFC 4180 escaping

TEST(CsvSerializerTest, FieldWithCommaIsQuoted) {
    CsvSerializer s;
    std::ostringstream out;
    s.writeProducts(out, {make("X", "Foo, Bar", "Active", 1, 50.0f)});
    auto l = lines(out.str());

    ASSERT_GE(l.size(), 2u);
    EXPECT_NE(l[1].find("\"Foo, Bar\""), std::string::npos);
}

TEST(CsvSerializerTest, FieldWithQuoteIsEscaped) {
    CsvSerializer s;
    std::ostringstream out;
    s.writeProducts(out, {make("X", "Size 2\"", "Active", 1, 50.0f)});
    auto l = lines(out.str());

    ASSERT_GE(l.size(), 2u);
    EXPECT_NE(l[1].find("\"Size 2\"\"\""), std::string::npos);
}

TEST(CsvSerializerTest, FieldWithNewlineIsQuoted) {
    CsvSerializer s;
    std::ostringstream out;
    s.writeProducts(out, {make("X", "Line1\nLine2", "Active", 1, 50.0f)});
    EXPECT_NE(out.str().find("\"Line1\nLine2\""), std::string::npos);
}

// Translated header (i18n)

TEST(CsvSerializerTest, ConstructorTranslatedHeaderAppearsInOutput) {
    CsvSerializer s({"Codice prodotto", "Nome", "Stato", "Scorta", "Qualita %"});
    std::ostringstream out;
    s.writeProducts(out, {make("P", "Test", "Active", 1, 90.0f)});
    auto l = lines(out.str());

    ASSERT_GE(l.size(), 1u);
    EXPECT_NE(l[0].find("Codice prodotto"), std::string::npos);
    EXPECT_NE(l[0].find("Qualita %"),       std::string::npos);
}

// UTF-8 BOM

TEST(CsvSerializerTest, OutputStartsWithUtf8Bom) {
    CsvSerializer s;
    std::ostringstream out;
    s.writeProducts(out, {});
    const auto& body = out.str();
    ASSERT_GE(body.size(), 3u);
    EXPECT_EQ(static_cast<unsigned char>(body[0]), 0xEF);
    EXPECT_EQ(static_cast<unsigned char>(body[1]), 0xBB);
    EXPECT_EQ(static_cast<unsigned char>(body[2]), 0xBF);
}

// Round-trip (new -- old CsvSerializer was write-only)

TEST(CsvSerializerTest, RoundTripPreservesAllFields) {
    CsvSerializer s;
    std::vector<Product> original{
        make("PROD-001", "Widget A",         "Active",    850, 98.1f),
        make("PROD-002", "Beta with, comma", "Low Stock",   5, 72.5f),
        make("PROD-003", "Quote\"Test",      "Inactive",    0,  0.0f),
        make("PROD-004", "Multi\nline",      "Active",      3, 88.8f),
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

TEST(CsvSerializerTest, ReadThrowsOnMalformedRow) {
    CsvSerializer s;
    // Header OK, but data row has only 3 fields instead of 5.
    std::istringstream in(
        "Product Code,Name,Status,Stock,Quality %\r\n"
        "PROD-001,Widget,Active\r\n");
    // void-cast pacifies Windows Clang's -Werror=unused-result
    // (readProducts is [[nodiscard]] but EXPECT_THROW drops the value).
    EXPECT_THROW((void)s.readProducts(in), std::runtime_error);
}

// Metadata

TEST(CsvSerializerTest, MetadataAccessors) {
    CsvSerializer s;
    EXPECT_EQ(s.formatName(),    "CSV");
    EXPECT_EQ(s.fileExtension(), "csv");
    EXPECT_EQ(s.mimeType(),      "text/csv");
}
