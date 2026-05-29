#pragma once

#include <cctype>
#include <string>

namespace app::view::avatarmime {

/// Guess an avatar's MIME type from its file-path extension. The user
/// repository accepts only PNG / JPEG; anything else returns empty and
/// the upload is refused at the presenter boundary. Extracted from
/// ProfileDialog's anonymous namespace so the extension->MIME mapping
/// is unit-testable without a file picker.
///
/// Case-insensitive on the extension; a path with no '.' returns empty.
[[nodiscard]] inline std::string mimeFromPath(const std::string& path) {
    const auto dot = path.find_last_of('.');
    if (dot == std::string::npos) {
        return {};
    }
    std::string ext = path.substr(dot + 1);
    for (auto& c : ext) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (ext == "png") {
        return "image/png";
    }
    if (ext == "jpg" || ext == "jpeg") {
        return "image/jpeg";
    }
    return {};
}

}  // namespace app::view::avatarmime
