#include "src/ml/ImageNetLabels.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

class TempLabelsFile {
public:
    explicit TempLabelsFile(const std::string& contents) {
        path_ = std::filesystem::temp_directory_path() /
                ("labels-test-" + std::to_string(std::rand()) + ".txt");
        std::ofstream out(path_, std::ios::binary);
        out.write(contents.data(),
                  static_cast<std::streamsize>(contents.size()));
    }

    ~TempLabelsFile() {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }

    TempLabelsFile(const TempLabelsFile&) = delete;
    TempLabelsFile& operator=(const TempLabelsFile&) = delete;
    TempLabelsFile(TempLabelsFile&&) = delete;
    TempLabelsFile& operator=(TempLabelsFile&&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

}  // namespace

TEST(ImageNetLabelsTest, ConstructFromVectorExposesContents) {
    const std::vector<std::string> entries = {"alpha", "beta", "gamma"};
    const app::ml::ImageNetLabels labels(entries);

    EXPECT_EQ(labels.size(), 3U);
    EXPECT_EQ(labels.at(0), "alpha");
    EXPECT_EQ(labels.at(1), "beta");
    EXPECT_EQ(labels.at(2), "gamma");
}

TEST(ImageNetLabelsTest, EmptyVectorThrows) {
    EXPECT_THROW(
        app::ml::ImageNetLabels(std::vector<std::string>{}),
        std::runtime_error);
}

TEST(ImageNetLabelsTest, AtNegativeThrows) {
    const app::ml::ImageNetLabels labels(std::vector<std::string>{"x"});
    EXPECT_THROW((void)labels.at(-1), std::out_of_range);
}

TEST(ImageNetLabelsTest, AtOutOfRangeThrows) {
    const app::ml::ImageNetLabels labels(std::vector<std::string>{"x", "y"});
    EXPECT_THROW((void)labels.at(2), std::out_of_range);
}

TEST(ImageNetLabelsTest, FileBasedLoadStripsTrailingNewlinesAndBlankLines) {
    const TempLabelsFile fixture("tench\nshark\n\ngoldfish\n");
    const app::ml::ImageNetLabels labels(fixture.path());

    EXPECT_EQ(labels.size(), 3U);
    EXPECT_EQ(labels.at(0), "tench");
    EXPECT_EQ(labels.at(1), "shark");
    EXPECT_EQ(labels.at(2), "goldfish");
}

TEST(ImageNetLabelsTest, FileBasedLoadStripsUtf8Bom) {
    // BOM bytes EF BB BF prefixing the first label.
    const std::string contents =
        std::string("\xEF\xBB\xBF") + "tench\nshark\n";
    const TempLabelsFile fixture(contents);

    const app::ml::ImageNetLabels labels(fixture.path());
    EXPECT_EQ(labels.at(0), "tench");
}

TEST(ImageNetLabelsTest, FileBasedLoadStripsCarriageReturn) {
    // Windows CRLF line endings.
    const TempLabelsFile fixture("tench\r\nshark\r\n");
    const app::ml::ImageNetLabels labels(fixture.path());

    EXPECT_EQ(labels.at(0), "tench");
    EXPECT_EQ(labels.at(1), "shark");
}

TEST(ImageNetLabelsTest, MissingFileThrows) {
    EXPECT_THROW(
        app::ml::ImageNetLabels(
            std::filesystem::path("/this/path/does/not/exist.txt")),
        std::runtime_error);
}

TEST(ImageNetLabelsTest, EmptyFileThrows) {
    const TempLabelsFile fixture("");
    EXPECT_THROW(
        app::ml::ImageNetLabels(fixture.path()),
        std::runtime_error);
}
