#pragma once

#include "src/integration/HttpTlsOptions.h"

namespace app::integration {

/// Loads and proves the PEM material named by `HttpTlsOptions` BEFORE any
/// socket is bound (REQ-INTEGRATION-010, ADR-0030).
///
/// @design The only OpenSSL-touching piece of the HTTP backend. It lives in
/// its own translation unit so `HttpBackend.cpp` never hand-rolls cert
/// parsing inline, and so these checks are unit-testable without starting a
/// server: construct it against a fixture directory and assert what it
/// accepts or rejects.
///
/// @rationale Validation runs ONCE, at construction, not per request. A
/// deployment pointed at a missing or mismatched cert/key pair must fail
/// loudly at startup (a `core::TlsMaterialError`, fatal) instead of binding
/// the port and answering every request with a confusing 500 -- and, above
/// all, instead of silently degrading to plaintext on a port the operator
/// believes is encrypted.
///
/// @threading Not thread-safe and not shared: one instance is built and
/// consumed by one backend constructor.
class HttpTlsMaterial {
public:
    /// Load + verify `options`. The certificate and private key are parsed,
    /// the key is checked against the certificate, and, when
    /// `options.verifyPeer` is set, the client CA is parsed too.
    ///
    /// @param options The configured cert / key / CA paths.
    /// @throws core::TlsMaterialError when any of that fails. The message
    ///         names which file and why, so the operator dialog points at a
    ///         path rather than at "TLS error".
    explicit HttpTlsMaterial(HttpTlsOptions options);

    /// The options this material was proven against. Handed to the server
    /// so the paths it opens are exactly the paths that were validated.
    [[nodiscard]] const HttpTlsOptions& options() const noexcept {
        return options_;
    }

    /// True when the material includes a client CA and the server must
    /// therefore demand a client certificate (mutual TLS).
    [[nodiscard]] bool verifiesPeer() const noexcept {
        return options_.verifyPeer;
    }

private:
    HttpTlsOptions options_;
};

}  // namespace app::integration
