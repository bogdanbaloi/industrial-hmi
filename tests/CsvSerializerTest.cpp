// Tests for app::core::CsvSerializer
// Pure C++ logic, no GTK dependency.

#include "src/core/CsvSerializer.h"
#include "src/model/Product.h"

#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <vector>

using app::core::CsvSerializer;
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

std::vector<std::string> defaultHeader() {
    return {"Product Code", "Name", "Status", "Stock", "Quality %"};
}

// Split a CSV string into lines (CRLF-delimited).
std::vector<std::string> lines(const std::string& csv) {
    std::vector<std::string> result;
    std::istringstream ss(csv);
    std::string line;
    while (std::getline(ss, line)) {
        // Remove trailing \r left by CRLF
        if (!line.empty() && line.back() == '\r') line.pop_back();
        result.push_back(line);
    }
    return result;
}

}  // namespace

// Basic output format

TEST(CsvSerializerTest, EmptyListProducesHeaderOnly) {
    std::ostringstream out;
    CsvSerializer::write(out, {}, defaultHeader());
    auto l = lines(out.str());

    // BOM + header line
    ASSERT_GE(l.size(), 1u);
    // UTF-8 BOM is 3 bytes at the start
    EXPECT_EQ(out.str().substr(0, 3), "\xEF\xBB\xBF");
    EXPECT_EQ(l[0].substr(3), "Product Code,Name,Status,Stock,Quality %");
}

TEST(CsvSerializerTest, SingleRowSerializesCorrectly) {
    std::ostringstream out;
    CsvSerializer::write(out, {make("PROD-001", "Widget A", "Active", 850, 98.1f)},
                          defaultHeader());
    auto l = lines(out.str());

    ASSERT_GE(l.size(), 2u);
    EXPECT_EQ(l[1], "PROD-001,Widget A,Active,850,98.1");
}

TEST(CsvSerializerTest, MultipleRowsInOrder) {
    std::ostringstream out;
    CsvSerializer::write(out,
        {make("A", "Alpha", "Active", 1, 99.0f),
         make("B", "Beta", "Inactive", 0, 0.0f),
         make("C", "Gamma", "Low Stock", 5, 92.3f)},
        defaultHeader());
    auto l = lines(out.str());

    ASSERT_GE(l.size(), 4u);
    EXPECT_EQ(l[1], "A,Alpha,Active,1,99.0");
    EXPECT_EQ(l[2], "B,Beta,Inactive,0,0.0");
    EXPECT_EQ(l[3], "C,Gamma,Low Stock,5,92.3");
}

// RFC 4180 field escaping

TEST(CsvSerializerTest, FieldWithCommaIsQuoted) {
    std::ostringstream out;
    CsvSerializer::write(out, {make("X", "Foo, Bar", "Active", 1, 50.0f)},
                          defaultHeader());
    auto l = lines(out.str());

    ASSERT_GE(l.size(), 2u);
    EXPECT_NE(l[1].find("\"Foo, Bar\""), std::string::npos);
}

TEST(CsvSerializerTest, FieldWithQuoteIsEscaped) {
    std::ostringstream out;
    CsvSerializer::write(out, {make("X", "Size 2\"", "Active", 1, 50.0f)},
                          defaultHeader());
    auto l = lines(out.str());

    ASSERT_GE(l.size(), 2u);
    // Embedded quote doubled and the whole field wrapped
    EXPECT_NE(l[1].find("\"Size 2\"\"\""), std::string::npos);
}

TEST(CsvSerializerTest, FieldWithNewlineIsQuoted) {
    std::ostringstream out;
    CsvSerializer::write(out, {make("X", "Line1\nLine2", "Active", 1, 50.0f)},
                          defaultHeader());
    // The escaped field should contain a literal newline inside quotes
    EXPECT_NE(out.str().find("\"Line1\nLine2\""), std::string::npos);
}

// Custom headers (i18n scenario)

TEST(CsvSerializerTest, CustomTranslatedHeaderAppearsInOutput) {
    std::ostringstream out;
    std::vector<std::string> itHeader = {
        "Codice prodotto", "Nome", "Stato", "Scorta", "Qualita %"};
    CsvSerializer::write(out, {make("P", "Test", "Active", 1, 90.0f)}, itHeader);
    auto l = lines(out.str());

    ASSERT_GE(l.size(), 1u);
    EXPECT_NE(l[0].find("Codice prodotto"), std::string::npos);
    EXPECT_NE(l[0].find("Qualita %"), std::string::npos);
}

// UTF-8 BOM present

TEST(CsvSerializerTest, OutputStartsWithUtf8Bom) {
    std::ostringstream out;
    CsvSerializer::write(out, {}, defaultHeader());
    const auto& s = out.str();
    ASSERT_GE(s.size(), 3u);
    EXPECT_EQ(static_cast<unsigned char>(s[0]), 0xEF);
    EXPECT_EQ(static_cast<unsigned char>(s[1]), 0xBB);
    EXPECT_EQ(static_cast<unsigned char>(s[2]), 0xBF);
}
