#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace app::ml {

/// In-memory mapping from ImageNet class index (0-999) to human-readable
/// label. Loaded once from a plain-text file (one label per line) and
/// queried by integer class id at inference time.
///
/// SOLID:
///   * S -- holds and serves labels, nothing else. No image processing,
///         no inference, no formatting beyond returning the stored string.
///   * O -- alternative label sets (CIFAR-10, custom domain) plug in by
///         pointing the constructor at a different file.
///   * D -- the classifier owns a `const ImageNetLabels&`, so a unit
///         test can pass an in-memory instance with two synthetic labels.
class ImageNetLabels {
public:
    /// Load labels from a UTF-8 text file (one entry per line, blank
    /// lines and a leading UTF-8 BOM are tolerated). Throws
    /// `std::runtime_error` if the file is missing or empty.
    explicit ImageNetLabels(const std::filesystem::path& labelsFile);

    /// Build directly from an in-memory list. Tests use this overload to
    /// avoid touching the filesystem; the file-based constructor
    /// delegates to it.
    explicit ImageNetLabels(std::vector<std::string> labels);

    ~ImageNetLabels() = default;

    ImageNetLabels(const ImageNetLabels&) = default;
    ImageNetLabels& operator=(const ImageNetLabels&) = default;
    ImageNetLabels(ImageNetLabels&&) noexcept = default;
    ImageNetLabels& operator=(ImageNetLabels&&) noexcept = default;

    /// Bounds-checked lookup. Throws `std::out_of_range` when `classId`
    /// is negative or >= `size()`. The classifier always validates the
    /// argmax index before this call, so a throw here is a real bug,
    /// not a normal control-flow path.
    [[nodiscard]] std::string_view at(int classId) const;

    /// Number of labels held. For ImageNet1K this is 1000; we do not
    /// hardcode that anywhere because alternative label sets reuse the
    /// same loader.
    [[nodiscard]] std::size_t size() const noexcept;

private:
    std::vector<std::string> labels_;
};

}  // namespace app::ml
