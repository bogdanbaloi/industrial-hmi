#include "src/ml/ImageDecoder.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <vector>

namespace {

/// Smallest possible 1x1 RGB BMP file. Hand-rolled here so the test
/// suite has a fixture that is independent of the encoder side of
/// stb_image -- if the decoder regresses we still have a known-good
/// byte buffer to throw at it.
///
/// Layout:
///   * 14-byte BITMAPFILEHEADER (signature, total size, offset to pixels)
///   * 40-byte BITMAPINFOHEADER (width=1, height=1, 24 bpp, no compression)
///   *  4-byte pixel row (1 RGB triple + 1 byte padding to 4-byte multiple)
constexpr std::array<std::uint8_t, 58> kOnePixelRedBmp = {
    // BITMAPFILEHEADER -- 14 bytes
    0x42, 0x4D,                  // "BM"
    0x3A, 0x00, 0x00, 0x00,      // file size = 58
    0x00, 0x00,                  // reserved
    0x00, 0x00,                  // reserved
    0x36, 0x00, 0x00, 0x00,      // pixel data offset = 54

    // BITMAPINFOHEADER -- 40 bytes
    0x28, 0x00, 0x00, 0x00,      // header size = 40
    0x01, 0x00, 0x00, 0x00,      // width = 1
    0x01, 0x00, 0x00, 0x00,      // height = 1
    0x01, 0x00,                  // planes = 1
    0x18, 0x00,                  // bits per pixel = 24
    0x00, 0x00, 0x00, 0x00,      // compression = BI_RGB
    0x04, 0x00, 0x00, 0x00,      // image size (may be 0 for BI_RGB)
    0x00, 0x00, 0x00, 0x00,      // x pixels per metre
    0x00, 0x00, 0x00, 0x00,      // y pixels per metre
    0x00, 0x00, 0x00, 0x00,      // colours used
    0x00, 0x00, 0x00, 0x00,      // important colours

    // Pixel data -- 1 BGR triple (red) + 1 byte row padding
    0x00, 0x00, 0xFF,            // B G R = pure red
    0x00,                         // pad to 4-byte row alignment
};

}  // namespace

TEST(ImageDecoderTest, DecodeMemoryEmptyBufferThrows) {
    const app::ml::ImageDecoder decoder;
    const std::vector<std::uint8_t> empty;
    EXPECT_THROW(
        (void)decoder.decodeMemory(std::span<const std::uint8_t>(empty)),
        std::runtime_error);
}

TEST(ImageDecoderTest, DecodeMemoryGarbageThrows) {
    const app::ml::ImageDecoder decoder;
    const std::vector<std::uint8_t> garbage{0x01, 0x02, 0x03, 0x04, 0x05};
    EXPECT_THROW(
        (void)decoder.decodeMemory(std::span<const std::uint8_t>(garbage)),
        std::runtime_error);
}

TEST(ImageDecoderTest, DecodeFileMissingFileThrows) {
    const app::ml::ImageDecoder decoder;
    EXPECT_THROW(
        (void)decoder.decodeFile(
            std::filesystem::path("/this/path/definitely/does/not/exist.png")),
        std::runtime_error);
}

TEST(ImageDecoderTest, DecodeMemoryParsesOnePixelBmp) {
    const app::ml::ImageDecoder decoder;
    const auto image = decoder.decodeMemory(
        std::span<const std::uint8_t>(kOnePixelRedBmp));

    EXPECT_EQ(image.width, 1);
    EXPECT_EQ(image.height, 1);
    EXPECT_EQ(image.channels, 3);
    ASSERT_EQ(image.pixels.size(), 3U);

    // Decoder forces RGB output; BMP stores BGR so the channel order must
    // already be flipped by the time we see it.
    EXPECT_EQ(image.pixels[0], 0xFF);  // R
    EXPECT_EQ(image.pixels[1], 0x00);  // G
    EXPECT_EQ(image.pixels[2], 0x00);  // B
}
