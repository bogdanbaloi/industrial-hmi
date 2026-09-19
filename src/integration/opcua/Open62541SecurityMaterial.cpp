#include "src/integration/opcua/Open62541SecurityMaterial.h"

#include "src/core/StartupErrors.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace app::integration::opcua {

namespace {

// The leaf key under `options.configKeyPrefix` each piece of material comes
// from. A failure names the KEY the operator has to edit, not just the file,
// so the startup error is actionable without grepping the schema.
namespace material {
inline constexpr const char* kCertificate = "cert_path";
inline constexpr const char* kPrivateKey  = "private_key_path";
inline constexpr const char* kTrustList   = "trust_list_dir";
}  // namespace material

// Shown in place of a path when the configured value is the empty string, so
// the message cannot read as "'': cannot be opened".
inline constexpr const char* kUnsetPath = "<not set>";

/// Abort the load with a message naming the full config key, the path and the
/// reason. Never returns. An endpoint that was configured for SignAndEncrypt
/// and cannot have it is fatal (ADR-0031), so there is no degraded path to
/// fall back to.
[[noreturn]] void refuse(const OpcUaSecurityOptions& options,
                         const char* configLeafKey,
                         const std::string& path,
                         const std::string& reason) {
    throw core::TlsMaterialError(
        std::format("{}.{} ({}): {}", options.configKeyPrefix, configLeafKey,
                    path.empty() ? kUnsetPath : path.c_str(), reason));
}

/// Read a whole DER file into a fresh `UA_ByteString`.
///
/// Opened in binary mode on purpose: DER is a binary encoding, and the text
/// translation Windows applies by default would corrupt every 0x0D byte in a
/// certificate. That is the failure that survives a Linux CI run and only
/// shows up on the operator's machine.
UA_ByteString readDerFile(const OpcUaSecurityOptions& options,
                          const char* configLeafKey,
                          const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        refuse(options, configLeafKey, path.string(),
               "cannot be opened for reading");
    }

    const std::streamoff size = file.tellg();
    if (size <= 0) {
        refuse(options, configLeafKey, path.string(),
               "is empty, so it cannot hold a DER certificate");
    }
    file.seekg(0, std::ios::beg);

    UA_ByteString bytes = UA_BYTESTRING_NULL;
    if (UA_ByteString_allocBuffer(&bytes, static_cast<std::size_t>(size)) !=
        UA_STATUSCODE_GOOD) {
        refuse(options, configLeafKey, path.string(),
               "could not be buffered, the stack is out of memory");
    }

    // UA_ByteString stores UA_Byte (unsigned char); istream reads char. The
    // cast is the standard byte-buffer bridge, not a type pun: both are
    // single-byte object representations and aliasing through char is
    // explicitly allowed.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    if (!file.read(reinterpret_cast<char*>(bytes.data), size)) {
        UA_ByteString_clear(&bytes);
        refuse(options, configLeafKey, path.string(),
               "could not be read to the end");
    }
    return bytes;
}

}  // namespace

Open62541SecurityMaterial::Open62541SecurityMaterial(
        OpcUaSecurityOptions options)
    : options_(std::move(options)) {
    try {
        load();
    } catch (...) {
        // A constructor that throws never runs its own destructor, so the
        // buffers already handed to us by open62541's allocator would leak.
        releaseOwnedBuffers();
        throw;
    }
}

Open62541SecurityMaterial::~Open62541SecurityMaterial() {
    releaseOwnedBuffers();
}

void Open62541SecurityMaterial::load() {
    if (options_.certPath.empty()) {
        refuse(options_, material::kCertificate, options_.certPath,
               std::format("is required when {}.enabled is true",
                           options_.configKeyPrefix));
    }
    if (options_.privateKeyPath.empty()) {
        refuse(options_, material::kPrivateKey, options_.privateKeyPath,
               std::format("is required when {}.enabled is true",
                           options_.configKeyPrefix));
    }
    if (options_.trustListDir.empty()) {
        refuse(options_, material::kTrustList, options_.trustListDir,
               std::format("is required when {}.enabled is true",
                           options_.configKeyPrefix));
    }

    certificate_ = readDerFile(options_, material::kCertificate,
                               std::filesystem::path(options_.certPath));
    privateKey_  = readDerFile(options_, material::kPrivateKey,
                               std::filesystem::path(options_.privateKeyPath));

    // The trust list is a DIRECTORY of peer certificates, one per file. The
    // filesystem calls take the error_code overloads so a missing or
    // unreadable directory surfaces as our own structured refusal rather than
    // as a std::filesystem_error nobody up the stack is catching.
    const std::filesystem::path trustDir(options_.trustListDir);
    std::error_code ec;
    if (!std::filesystem::is_directory(trustDir, ec) || ec) {
        refuse(options_, material::kTrustList, options_.trustListDir,
               "is not a readable directory");
    }

    // Sorted so the trust list has a deterministic order across platforms.
    // Directory iteration order is unspecified, and an order that changes per
    // machine turns a reproducible startup into a coin flip when something
    // downstream cares which certificate came first.
    std::vector<std::filesystem::path> peerFiles;
    for (const auto& entry : std::filesystem::directory_iterator(trustDir, ec)) {
        if (entry.is_regular_file(ec) && !ec) {
            peerFiles.push_back(entry.path());
        }
    }
    if (ec) {
        refuse(options_, material::kTrustList, options_.trustListDir,
               "could not be listed");
    }
    std::ranges::sort(peerFiles);

    // An existing but EMPTY directory is valid. It means the deployment
    // trusts nobody yet, which is a state an operator moves through while
    // exchanging certificates, not a configuration error.
    trustList_.reserve(peerFiles.size());
    for (const auto& peerFile : peerFiles) {
        trustList_.push_back(
            readDerFile(options_, material::kTrustList, peerFile));
    }
}

void Open62541SecurityMaterial::releaseOwnedBuffers() noexcept {
    UA_ByteString_clear(&certificate_);
    UA_ByteString_clear(&privateKey_);
    for (auto& peer : trustList_) {
        UA_ByteString_clear(&peer);
    }
    trustList_.clear();
}

}  // namespace app::integration::opcua
