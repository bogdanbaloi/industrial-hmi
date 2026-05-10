#pragma once

#include <cstdint>
#include <string>

namespace app::integration::opcua {

/// Configuration for an OPC-UA server endpoint.
///
/// Value type: copyable, no special members needed (rule of 5 satisfied
/// implicitly by the compiler). Held by `OpcUaBackend` and passed by
/// const-ref to concrete `OpcUaServer` implementations at construction.
///
/// Defaults track the standard IANA assignment for OPC-UA Binary
/// (port 4840) and a no-namespace application URI; both are overridable
/// from app-config.json so a deployment can use a non-default port to
/// avoid clashes with vendor PLCs already listening on 4840.
struct OpcUaConfig {
    /// Listen port for the OPC-UA Binary endpoint. IANA-assigned
    /// default is 4840 (registered service "opcua-tcp"). Setting to 0
    /// lets the OS pick an ephemeral port (used by integration tests
    /// to run concurrently without port-in-use collisions).
    // NOLINTNEXTLINE(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
    std::uint16_t port = 4840;

    /// URN advertised in the GetEndpoints response. Clients use this
    /// to identify the server in their address books. Must be a
    /// stable, deployment-unique URI.
    std::string applicationUri = "urn:industrial-hmi:server";

    /// Human-readable name shown in OPC-UA browse trees and discovery
    /// responses. Localised display name lives in the node map; this
    /// is the bare protocol-level identifier.
    std::string applicationName = "Industrial HMI OPC-UA Server";

    /// Endpoint URL fragment used to assemble `opc.tcp://host:port/path`.
    /// Empty string means root path (the common case). Set to e.g.
    /// "/ProductionLine" if the deployment routes multiple servers
    /// behind one host.
    std::string endpointPath;
};

}  // namespace app::integration::opcua
