#pragma once

#include "src/integration/opcua/OpcUaSecurityOptions.h"

// open62541 is a C library. Include it after our own headers and keep the
// pedantic diagnostics it trips off our own -Werror build, exactly as
// Open62541Server.cpp does.
#if defined(__clang__) || defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wpedantic"
#endif
#include <open62541/types.h>
#if defined(__clang__) || defined(__GNUC__)
#  pragma GCC diagnostic pop
#endif

#include <cstddef>
#include <vector>

namespace app::integration::opcua {

/// Loads and proves the DER material named by `OpcUaSecurityOptions` BEFORE
/// any `UA_Server` or `UA_Client` exists (REQ-INTEGRATION-011, ADR-0031).
///
/// @design This is the ONE type under `src/integration/opcua/` that does not
/// hide open62541 behind a PIMPL, and the departure is deliberate rather than
/// an oversight. Its entire purpose is to hand `UA_ByteString` values to
/// `UA_ServerConfig_setDefaultWithSecureSecurityPolicies` and
/// `UA_ClientConfig_setDefaultEncryption`, so that type is the return type of
/// all three accessors. A PIMPL here would hide nothing: every caller would
/// still need `UA_ByteString` to use what it handed back, so the wrapper
/// would buy an allocation and cost a reader one indirection. The
/// containment that matters is preserved at the layer above, because
/// `Open62541Server` and `Open62541Client` hold this type inside their own
/// `Impl`, which keeps their headers C-stack-free.
///
/// @design Loading runs ONCE, at construction, not per connection. A
/// deployment pointed at a missing or unreadable file must fail loudly at
/// startup (`core::TlsMaterialError`, fatal) instead of binding the port and
/// then refusing every handshake. Above all it must never silently fall back
/// to `SecurityPolicy#None` on an endpoint the operator believes is
/// encrypted.
///
/// @design Structural validity of the certificate is NOT re-checked here.
/// open62541's own PKI plugin parses the certificate and key when the config
/// is built and returns a status code the caller checks. A second X.509 parse
/// with OpenSSL would be two parsers that can disagree, and the one that
/// decides is the one the stack actually uses (ADR-0031). This class answers
/// the question open62541 cannot: are the files there, readable and
/// non-empty, and which config key names the one that is not.
///
/// @threading Not thread-safe and not shared: one instance is built and
/// consumed by one endpoint constructor.
///
/// Rule of 5: copy and move deleted. The object owns raw C buffers that
/// `UA_ByteString_clear` frees exactly once, and open62541 holds pointers
/// into them for the endpoint's lifetime, so a relocated instance would leave
/// the stack pointing at freed memory.
class Open62541SecurityMaterial {
public:
    /// Load `options`. Every configured path is read into an owned buffer.
    ///
    /// @param options The configured certificate / key / trust-list paths.
    /// @throws core::TlsMaterialError when a required path is unset, names
    ///         something that cannot be read, or names an empty file. The
    ///         message carries the config key, the path and the reason, so
    ///         the operator dialog points at a key rather than at
    ///         "security error".
    explicit Open62541SecurityMaterial(OpcUaSecurityOptions options);

    ~Open62541SecurityMaterial();

    Open62541SecurityMaterial(const Open62541SecurityMaterial&)            = delete;
    Open62541SecurityMaterial& operator=(const Open62541SecurityMaterial&) = delete;
    Open62541SecurityMaterial(Open62541SecurityMaterial&&)                 = delete;
    Open62541SecurityMaterial& operator=(Open62541SecurityMaterial&&)      = delete;

    /// The DER application certificate, owned by this object.
    [[nodiscard]] const UA_ByteString& certificate() const noexcept {
        return certificate_;
    }

    /// The DER private key matching `certificate()`, owned by this object.
    [[nodiscard]] const UA_ByteString& privateKey() const noexcept {
        return privateKey_;
    }

    /// Contiguous view of the trusted peer certificates, in the order the
    /// directory yielded them. Empty (and `nullptr`) when the trust-list
    /// directory holds no files, which open62541 reads as "leave the
    /// previously configured verification in place".
    [[nodiscard]] const UA_ByteString* trustList() const noexcept {
        return trustList_.empty() ? nullptr : trustList_.data();
    }

    /// Number of entries `trustList()` points at.
    [[nodiscard]] std::size_t trustListSize() const noexcept {
        return trustList_.size();
    }

    /// The options this material was loaded from. Handed on so the endpoint
    /// reports exactly the configuration that was proven.
    [[nodiscard]] const OpcUaSecurityOptions& options() const noexcept {
        return options_;
    }

private:
    /// Read every configured path into the owned buffers. Separated from the
    /// constructor so the constructor can wrap it and clean up on a throw.
    void load();

    /// Free every buffer open62541's allocator handed us. Idempotent:
    /// `UA_ByteString_clear` zeroes what it frees, so a second call is a
    /// no-op. Called by the destructor AND by the constructor on failure,
    /// because a constructor that throws never runs its own destructor and
    /// the trust-list entries loaded before the failure would otherwise leak
    /// (`std::vector`'s destructor frees the array, not the bytes each
    /// element points at).
    void releaseOwnedBuffers() noexcept;

    OpcUaSecurityOptions options_;

    // Value-initialised so `releaseOwnedBuffers` is safe at any point: a
    // zeroed UA_ByteString is what UA_ByteString_clear treats as "nothing to
    // free".
    UA_ByteString certificate_{};
    UA_ByteString privateKey_{};

    // std::vector rather than open62541's UA_Array: the element count is
    // discovered by walking a directory, so the growable container is the
    // honest shape. Each element is still cleared with UA_ByteString_clear
    // because the BYTES were allocated by open62541's allocator.
    std::vector<UA_ByteString> trustList_;
};

}  // namespace app::integration::opcua
