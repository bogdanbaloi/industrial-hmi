#include "src/ml/ImageNetLabels.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace app::ml {

namespace {

/// UTF-8 byte-order-mark bytes. Editors on Windows occasionally save
/// labels.txt with a BOM; we strip it transparently so the first label
/// does not become "\xEF\xBB\xBFtench, Tinca tinca".
constexpr std::uint8_t kUtf8BomByte0 = 0xEFU;
constexpr std::uint8_t kUtf8BomByte1 = 0xBBU;
constexpr std::uint8_t kUtf8BomByte2 = 0xBFU;
constexpr std::size_t kUtf8BomLength = 3;

/// In-place strip of trailing CR / LF / whitespace. Files authored on
/// Windows ship with `\r\n`; trimming once on load is cheaper than at
/// every query.
void rtrim(std::string& s) {
    while (!s.empty()) {
        const char back = s.back();
        if (back == '\r' || back == '\n' || back == ' ' || back == '\t') {
            s.pop_back();
        } else {
            break;
        }
    }
}

/// Strip a UTF-8 BOM from the head of `s` if present.
void stripBom(std::string& s) {
    if (s.size() < kUtf8BomLength) {
        return;
    }
    const bool isBom =
        static_cast<std::uint8_t>(s[0]) == kUtf8BomByte0 &&
        static_cast<std::uint8_t>(s[1]) == kUtf8BomByte1 &&
        static_cast<std::uint8_t>(s[2]) == kUtf8BomByte2;
    if (isBom) {
        s.erase(0, kUtf8BomLength);
    }
}

}  // namespace

ImageNetLabels::ImageNetLabels(std::vector<std::string> labels)
    : labels_(std::move(labels)) {
    if (labels_.empty()) {
        throw std::runtime_error(
            "ImageNetLabels: label list is empty.");
    }
}

ImageNetLabels::ImageNetLabels(const std::filesystem::path& labelsFile) {
    if (!std::filesystem::exists(labelsFile)) {
        throw std::runtime_error(
            "ImageNetLabels: file not found: " + labelsFile.string());
    }

    std::ifstream stream(labelsFile);
    if (!stream.is_open()) {
        throw std::runtime_error(
            "ImageNetLabels: failed to open " + labelsFile.string());
    }

    std::string line;
    bool firstLine = true;
    while (std::getline(stream, line)) {
        if (firstLine) {
            stripBom(line);
            firstLine = false;
        }
        rtrim(line);
        if (!line.empty()) {
            labels_.push_back(line);
        }
    }

    if (labels_.empty()) {
        throw std::runtime_error(
            "ImageNetLabels: no usable labels in " + labelsFile.string());
    }
}

std::string_view ImageNetLabels::at(int classId) const {
    if (classId < 0 ||
        static_cast<std::size_t>(classId) >= labels_.size()) {
        throw std::out_of_range(
            "ImageNetLabels::at: class id " + std::to_string(classId) +
            " is outside [0, " + std::to_string(labels_.size()) + ").");
    }
    return labels_[static_cast<std::size_t>(classId)];
}

std::size_t ImageNetLabels::size() const noexcept {
    return labels_.size();
}

}  // namespace app::ml
