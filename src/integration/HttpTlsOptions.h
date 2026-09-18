#pragma once

#include <string>

namespace app::integration {

/// The TLS settings the HTTP/REST backend needs to serve HTTPS instead of
/// plaintext HTTP (REQ-INTEGRATION-010, ADR-0030).
///
/// @design A plain aggregate, not a `Result`-returning parser: it carries
/// only what the operator configured under `network.http.tls`. It holds no
/// opinion on whether those paths are usable. Proving the material is usable
/// is `HttpTlsMaterial`'s single responsibility, so the value type stays free
/// of both OpenSSL and the config layer and can be built by hand in a test.
///
/// @rationale The struct is deliberately not built from `ConfigManager`
/// here. `objectsHttp` does not link the config layer. Keeping it that
/// way means the backend depends on the four values, not on where they came
/// from. `IntegrationBootstrap` (the composition root) is the one place that
/// reads config and fills this in.
struct HttpTlsOptions {
    /// PEM server certificate presented to clients. Required.
    std::string certPath;

    /// PEM private key matching `certPath`. Required.
    std::string keyPath;

    /// Require and verify a client certificate (mutual TLS). When true,
    /// `clientCaPath` must name the CA the client certificate is checked
    /// against.
    bool verifyPeer{false};

    /// PEM CA bundle client certificates are verified against. Required
    /// only when `verifyPeer` is true.
    std::string clientCaPath;
};

}  // namespace app::integration
