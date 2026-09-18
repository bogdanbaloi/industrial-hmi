// [utest->req~integration-010~1]
// Covers REQ-INTEGRATION-010 (TLS for the HTTP/REST backend) at the loader:
// the cert / key / client-CA material is proven usable BEFORE a socket
// exists, and every way it can be unusable is a structured, fatal
// TlsMaterialError naming the config key and the path (ADR-0030).
//
// Hermetic: file I/O plus an OpenSSL parse against the committed fixtures in
// tests/fixtures/tls. No sockets, no Xvfb, no server.

#include "src/integration/HttpTlsMaterial.h"

#include "src/core/StartupErrors.h"
#include "src/integration/HttpTlsOptions.h"

#include <gtest/gtest.h>

#include <string>
#include <string_view>

using app::core::StartupErrorCode;
using app::core::TlsMaterialError;
using app::integration::HttpTlsMaterial;
using app::integration::HttpTlsOptions;

namespace {

// The fixture directory is passed in by CMake so the test finds the
// committed PEMs regardless of the working directory ctest runs it from.
constexpr std::string_view kFixtureDir = INDUSTRIAL_HMI_TLS_FIXTURE_DIR;

std::string fixture(std::string_view name) {
    return std::string(kFixtureDir) + "/" + std::string(name);
}

/// Server-only TLS material that is expected to load cleanly.
HttpTlsOptions validServerOptions() {
    HttpTlsOptions options;
    options.certPath = fixture("server.crt");
    options.keyPath  = fixture("server.key");
    return options;
}

/// Build the material and return the message of the TlsMaterialError it
/// must throw. Fails the test if it throws nothing, or throws something
/// else -- a plain std::runtime_error would not reach the startup dialog's
/// TLS branch.
std::string rejectionMessage(const HttpTlsOptions& options) {
    try {
        HttpTlsMaterial material{options};
    } catch (const TlsMaterialError& e) {
        EXPECT_EQ(e.code(), StartupErrorCode::TlsMaterialInvalid);
        return e.what();
    } catch (const std::exception& e) {
        ADD_FAILURE() << "expected TlsMaterialError, got: " << e.what();
        return {};
    }
    ADD_FAILURE() << "expected TlsMaterialError, nothing was thrown";
    return {};
}

bool mentions(const std::string& message, std::string_view needle) {
    return message.find(needle) != std::string::npos;
}

}  // namespace

TEST(HttpTlsMaterialTest, LoadsValidCertAndKey) {
    EXPECT_NO_THROW({ HttpTlsMaterial material{validServerOptions()}; });
}

TEST(HttpTlsMaterialTest, KeepsTheOptionsItValidated) {
    // The server must open exactly the files that were proven, so the
    // material hands back its own options rather than re-reading config.
    const HttpTlsMaterial material{validServerOptions()};
    EXPECT_EQ(material.options().certPath, fixture("server.crt"));
    EXPECT_EQ(material.options().keyPath, fixture("server.key"));
    EXPECT_FALSE(material.verifiesPeer());
}

TEST(HttpTlsMaterialTest, RejectsMissingCertFile) {
    HttpTlsOptions options = validServerOptions();
    options.certPath       = fixture("no-such-server.crt");

    const std::string message = rejectionMessage(options);
    EXPECT_TRUE(mentions(message, "network.http.tls.cert_path")) << message;
    EXPECT_TRUE(mentions(message, "no-such-server.crt")) << message;
}

TEST(HttpTlsMaterialTest, RejectsEmptyCertPath) {
    HttpTlsOptions options = validServerOptions();
    options.certPath.clear();

    const std::string message = rejectionMessage(options);
    EXPECT_TRUE(mentions(message, "network.http.tls.cert_path")) << message;
}

TEST(HttpTlsMaterialTest, RejectsMissingKeyFile) {
    HttpTlsOptions options = validServerOptions();
    options.keyPath        = fixture("no-such-server.key");

    const std::string message = rejectionMessage(options);
    EXPECT_TRUE(mentions(message, "network.http.tls.key_path")) << message;
    EXPECT_TRUE(mentions(message, "no-such-server.key")) << message;
}

TEST(HttpTlsMaterialTest, RejectsMalformedCert) {
    // Garbage where a PEM object should be. OpenSSL's error paths are a
    // classic crash surface; this must come back as a message, not a signal.
    HttpTlsOptions options = validServerOptions();
    options.certPath       = fixture("malformed.pem");

    const std::string message = rejectionMessage(options);
    EXPECT_TRUE(mentions(message, "network.http.tls.cert_path")) << message;
    EXPECT_TRUE(mentions(message, "malformed.pem")) << message;
}

TEST(HttpTlsMaterialTest, RejectsMalformedKey) {
    HttpTlsOptions options = validServerOptions();
    options.keyPath        = fixture("malformed.pem");

    const std::string message = rejectionMessage(options);
    EXPECT_TRUE(mentions(message, "network.http.tls.key_path")) << message;
}

TEST(HttpTlsMaterialTest, RejectsMismatchedKeyForCert) {
    // client.key is a perfectly valid key -- it just belongs to a different
    // certificate. Every handshake would fail at run time, so it is caught
    // here, before any socket is opened.
    HttpTlsOptions options = validServerOptions();
    options.keyPath        = fixture("client.key");

    const std::string message = rejectionMessage(options);
    EXPECT_TRUE(mentions(message, "does not match the certificate")) << message;
}

TEST(HttpTlsMaterialTest, VerifyPeerRequiresClientCa) {
    HttpTlsOptions options = validServerOptions();
    options.verifyPeer     = true;  // and no clientCaPath

    const std::string message = rejectionMessage(options);
    EXPECT_TRUE(mentions(message, "network.http.tls.client_ca_path"))
        << message;
    EXPECT_TRUE(mentions(message, "verify_peer")) << message;
}

TEST(HttpTlsMaterialTest, RejectsMalformedClientCa) {
    HttpTlsOptions options = validServerOptions();
    options.verifyPeer     = true;
    options.clientCaPath   = fixture("malformed.pem");

    const std::string message = rejectionMessage(options);
    EXPECT_TRUE(mentions(message, "network.http.tls.client_ca_path"))
        << message;
}

TEST(HttpTlsMaterialTest, LoadsMutualTlsMaterial) {
    HttpTlsOptions options = validServerOptions();
    options.verifyPeer     = true;
    options.clientCaPath   = fixture("client-ca.crt");

    EXPECT_NO_THROW({ HttpTlsMaterial material{options}; });

    const HttpTlsMaterial material{options};
    EXPECT_TRUE(material.verifiesPeer());
}
