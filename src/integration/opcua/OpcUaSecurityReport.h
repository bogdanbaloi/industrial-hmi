#pragma once

#include <string_view>

namespace app::integration::opcua {

/// Optional capability: an OPC-UA endpoint that can say which message
/// security mode its wire actually speaks (REQ-INTEGRATION-011, ADR-0031).
///
/// @design A separate interface rather than a method on `OpcUaServer`,
/// because `OpcUaServer`'s own contract says so: it keeps itself to what a
/// node map and a command sink need, and states that extension surfaces such
/// as encryption get their own orthogonal interface. Honouring that is what
/// keeps `MockOpcUaServer` untouched by this feature, and it keeps a server
/// that has no security story from being forced to answer a question about
/// one (the I in SOLID, interface segregation: no implementer should have to
/// implement what it does not use).
///
/// Consumers query it with a `dynamic_cast` and omit the field when the cast
/// comes back null. That is the standard optional-capability probe, and the
/// cost is paid once per status poll rather than on any data path.
class OpcUaSecurityReport {
public:
    virtual ~OpcUaSecurityReport() = default;

    OpcUaSecurityReport(const OpcUaSecurityReport&)            = delete;
    OpcUaSecurityReport& operator=(const OpcUaSecurityReport&) = delete;
    OpcUaSecurityReport(OpcUaSecurityReport&&)                 = delete;
    OpcUaSecurityReport& operator=(OpcUaSecurityReport&&)      = delete;

    /// Short operator-facing name of the mode the endpoint offers, e.g.
    /// `none` or `sign+encrypt`. It is the operator's only evidence of what
    /// the port actually speaks, so it must reflect the built configuration
    /// and never the configured intent.
    [[nodiscard]] virtual std::string_view
        securityModeName() const noexcept = 0;

protected:
    OpcUaSecurityReport() = default;
};

}  // namespace app::integration::opcua
