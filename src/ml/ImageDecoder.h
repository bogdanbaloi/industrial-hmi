#pragma once

#include "src/ml/Image.h"

#include <cstdint>
#include <filesystem>
#include <span>

namespace app::ml {

/// Decodes encoded image bytes (PNG, JPEG, BMP, ...) into a raw RGB
/// `Image`. Wraps the public-domain `stb_image` single-header library
/// so the rest of the project never sees its symbols.
///
/// The decoder always returns 3-channel RGB regardless of source format
/// (grayscale gets duplicated across channels; RGBA loses its alpha). This
/// matches what an ImageNet-trained classifier expects and removes the
/// channel-count branching from every downstream stage.
///
/// SOLID:
///   * S -- the only responsibility is "encoded bytes -> raw RGB pixels".
///         No resizing, no normalisation, no caching.
///   * O -- additional input modes (network stream, camera buffer) slot in
///         as new methods that fill the same `Image` value type; nothing
///         downstream needs to change.
///   * D -- the preprocessor depends on `Image`, never on the decoder. A
///         test can build an `Image` literal and skip decoding entirely.
class ImageDecoder {
public:
    ImageDecoder() = default;
    ~ImageDecoder() = default;

    ImageDecoder(const ImageDecoder&) = delete;
    ImageDecoder& operator=(const ImageDecoder&) = delete;
    ImageDecoder(ImageDecoder&&) = delete;
    ImageDecoder& operator=(ImageDecoder&&) = delete;

    /// Decode a file from disk into an RGB image. Throws
    /// `std::runtime_error` on missing file, unsupported format, or
    /// truncated payload -- the exact message comes from `stbi_failure_reason`.
    [[nodiscard]] Image decodeFile(const std::filesystem::path& path) const;

    /// Decode a buffer already in memory (network response, embedded
    /// resource, test fixture). Same error contract as `decodeFile`.
    [[nodiscard]] Image decodeMemory(std::span<const std::uint8_t> bytes) const;
};

}  // namespace app::ml
