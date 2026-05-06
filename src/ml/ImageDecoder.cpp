#include "src/ml/ImageDecoder.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>

// stb_image is a public-domain single-header decoder. We force RGB output
// (3 channels) for every call so the rest of the pipeline never has to
// branch on channel count. The implementation macro must be defined in
// exactly one translation unit -- this file is that unit.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_PSD
#define STBI_NO_TGA
#define STBI_NO_GIF
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#include <stb_image.h>

namespace app::ml {

namespace {

/// Channel count we always request from stb_image. ImageNet pre-trained
/// models are RGB-only; forcing 3 here removes a branch from every
/// consumer.
constexpr int kForceRgbChannels = 3;

/// Custom deleter for the raw buffer stb_image returns. `stbi_image_free`
/// is just `free`, but routing through stb's symbol keeps the code honest
/// about ownership (the buffer was allocated inside the library).
struct StbiPixelDeleter {
    void operator()(std::uint8_t* ptr) const noexcept {
        if (ptr != nullptr) {
            stbi_image_free(ptr);
        }
    }
};

/// Build an `Image` from the raw buffer stb_image gave us. Copies into a
/// `std::vector<uint8_t>` so the caller does not have to deal with
/// stb-specific deallocation.
[[nodiscard]] Image buildImage(const std::uint8_t* raw,
                               int width,
                               int height) {
    Image image;
    image.width = width;
    image.height = height;
    image.channels = kForceRgbChannels;

    const std::size_t byteCount =
        static_cast<std::size_t>(width) *
        static_cast<std::size_t>(height) *
        static_cast<std::size_t>(kForceRgbChannels);
    image.pixels.assign(raw, raw + byteCount);
    return image;
}

/// Translate the latest stb_image error into a runtime exception. Keeping
/// the message verbatim helps when a real-world image refuses to decode --
/// "unknown image type", "bad PNG sig", "truncated jpeg" are all
/// stb-formatted strings worth surfacing as-is.
[[noreturn]] void throwStbError(const std::filesystem::path& source) {
    const char* reason = stbi_failure_reason();
    std::string message = "ImageDecoder: failed to decode ";
    message += source.string();
    message += ": ";
    message += (reason != nullptr) ? reason : "unknown error";
    throw std::runtime_error(message);
}

[[noreturn]] void throwStbError() {
    const char* reason = stbi_failure_reason();
    std::string message = "ImageDecoder: failed to decode in-memory buffer: ";
    message += (reason != nullptr) ? reason : "unknown error";
    throw std::runtime_error(message);
}

}  // namespace

Image ImageDecoder::decodeFile(const std::filesystem::path& path) const {
    if (!std::filesystem::exists(path)) {
        throw std::runtime_error("ImageDecoder: file not found: " +
                                 path.string());
    }

    int width = 0;
    int height = 0;
    int channelsInFile = 0;
    std::unique_ptr<std::uint8_t, StbiPixelDeleter> raw{
        stbi_load(path.string().c_str(),
                  &width, &height, &channelsInFile,
                  kForceRgbChannels)};

    if (raw == nullptr) {
        throwStbError(path);
    }
    return buildImage(raw.get(), width, height);
}

Image ImageDecoder::decodeMemory(std::span<const std::uint8_t> bytes) const {
    if (bytes.empty()) {
        throw std::runtime_error(
            "ImageDecoder: cannot decode empty buffer.");
    }

    int width = 0;
    int height = 0;
    int channelsInFile = 0;
    std::unique_ptr<std::uint8_t, StbiPixelDeleter> raw{
        stbi_load_from_memory(bytes.data(),
                              static_cast<int>(bytes.size()),
                              &width, &height, &channelsInFile,
                              kForceRgbChannels)};

    if (raw == nullptr) {
        throwStbError();
    }
    return buildImage(raw.get(), width, height);
}

}  // namespace app::ml
