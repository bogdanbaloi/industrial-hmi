#pragma once

#include <string>

namespace app::integration::opcua {

/// The certificate material an OPC-UA endpoint needs to speak
/// `SignAndEncrypt` with the `Basic256Sha256` policy instead of
/// `SecurityPolicy#None` (REQ-INTEGRATION-011, ADR-0031).
///
/// @design A plain aggregate, deliberately free of open62541: it carries only
/// what the operator configured under `network.opcua.server.security` or
/// `network.opcua.client.security` and holds no opinion on whether those
/// paths are usable. Proving them is `Open62541SecurityMaterial`'s single
/// responsibility. Keeping the value type C-stack-free is what lets
/// `OpcUaConfig` and `Open62541Client::Config` carry it without dragging
/// `<open62541/...>` into every translation unit that includes them.
///
/// @rationale The struct is not built from `ConfigManager` here, for the same
/// reason `HttpTlsOptions` is not: the endpoints depend on the four values,
/// not on where they came from. `IntegrationBootstrap` (the composition root)
/// is the one place that reads config and fills this in.
///
/// @note Paths name DER files, not PEM. open62541 hands its PKI plugin raw
/// bytes and expects DER there, so PEM would mean a conversion on every load
/// and a second failure mode to report. ADR-0031 records the divergence from
/// the PEM material ADR-0030 uses for the HTTP backend.
struct OpcUaSecurityOptions {
    /// Opt in to `SignAndEncrypt`. False (the default) leaves the endpoint on
    /// `SecurityPolicy#None`, which is what every existing deployment gets.
    bool enabled{false};

    /// DER application certificate this endpoint presents to its peer.
    /// Required when `enabled`. Its `applicationUri` extension must match the
    /// `applicationUri` the endpoint advertises, which OPC-UA checks more
    /// strictly than HTTPS checks a hostname.
    std::string certPath;

    /// DER private key matching `certPath`. Required when `enabled`.
    std::string privateKeyPath;

    /// Directory of trusted peer certificates, one DER file per peer.
    /// Required when `enabled`. An existing but empty directory is valid and
    /// means "trust nobody yet", which is a deployment state rather than a
    /// configuration error.
    std::string trustListDir;

    /// The config subtree these three paths were read from, used ONLY to make
    /// a startup failure name the key the operator has to edit.
    ///
    /// `HttpTlsMaterial` hard-codes its keys because there is exactly one HTTP
    /// backend. Here there are two endpoints with identical shapes under
    /// different subtrees, so a hard-coded key would send the operator to the
    /// wrong half of the file. `IntegrationBootstrap` sets it to
    /// `network.opcua.server.security` or `network.opcua.client.security`; the
    /// default keeps a hand-built options value (a test, a demo) readable.
    std::string configKeyPrefix{"network.opcua.security"};
};

}  // namespace app::integration::opcua
