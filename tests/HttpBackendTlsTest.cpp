// [utest->req~integration-010~1]
// Covers REQ-INTEGRATION-010 (TLS for the HTTP/REST backend) end to end:
// the backend really terminates TLS on its own socket, really refuses a
// plaintext client, really enforces a client certificate when verify_peer is
// set. It really refuses to start on bad material instead of falling back to
// plaintext (ADR-0030).
//
// Boots on an OS-assigned port (port=0) and drives it with cpp-httplib's
// SSLClient against the committed self-signed fixtures in
// tests/fixtures/tls. Hermetic: one loopback socket, no Xvfb, no broker.

#include "src/integration/HttpBackend.h"

#include "src/core/LoggerImpl.h"
#include "src/core/StartupErrors.h"
#include "src/integration/HttpTlsOptions.h"
#include "src/presenter/AlertCenter.h"
#include "tests/mocks/MockProductionModel.h"
#include "tests/mocks/MockProductsRepository.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

// Match HttpBackend.cpp's Windows include hygiene so the client header
// behaves. CPPHTTPLIB_OPENSSL_SUPPORT arrives from the cpp_httplib_headers
// target, so this TU sees the same httplib::Server layout the backend does.
#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#endif
#include <httplib.h>
#ifdef ERROR
#  undef ERROR
#endif

#include <memory>
#include <string>
#include <string_view>

using ::testing::NiceMock;

using app::core::TlsMaterialError;
using app::integration::HttpBackend;
using app::integration::HttpTlsOptions;
using app::test::MockProductionModel;
using app::test::MockProductsRepository;

namespace {

constexpr const char* kLoopback     = "127.0.0.1";
constexpr const char* kHealthRoute  = "/health";
constexpr int         kOkStatus     = 200;
constexpr int         kTimeoutSecs  = 2;
constexpr int         kTimeoutUsecs = 0;

// The fixture directory is passed in by CMake so the test finds the
// committed PEMs regardless of the working directory ctest runs it from.
constexpr std::string_view kFixtureDir = INDUSTRIAL_HMI_TLS_FIXTURE_DIR;

std::string fixture(std::string_view name) {
    return std::string(kFixtureDir) + "/" + std::string(name);
}

HttpTlsOptions serverOnlyTls() {
    HttpTlsOptions options;
    options.certPath = fixture("server.crt");
    options.keyPath  = fixture("server.key");
    return options;
}

HttpTlsOptions mutualTls() {
    HttpTlsOptions options = serverOnlyTls();
    options.verifyPeer     = true;
    options.clientCaPath   = fixture("client-ca.crt");
    return options;
}

/// RAII fixture: starts a TLS backend on a random port. The routes are the
/// same ones HttpBackendTest covers, so these tests only ever ask for
/// /health. What is under test is the transport, not the payload.
class TlsFixture {
public:
    explicit TlsFixture(HttpTlsOptions options)
        : logger_(std::make_unique<app::core::ConsoleLogger>()),
          backend_(0, kLoopback, model_, repo_, alerts_, logger_,
                   std::move(options)) {
        backend_.start();
    }

    ~TlsFixture() { backend_.stop(); }

    TlsFixture(const TlsFixture&)            = delete;
    TlsFixture& operator=(const TlsFixture&) = delete;

    [[nodiscard]] HttpBackend& backend() { return backend_; }
    [[nodiscard]] int port() const { return backend_.boundPort(); }

private:
    NiceMock<MockProductionModel>    model_;
    NiceMock<MockProductsRepository> repo_;
    app::presenter::AlertCenter      alerts_;
    app::core::Logger                logger_;
    HttpBackend                      backend_;
};

/// The self-signed fixtures have no chain a client could verify, so peer
/// verification is switched off on the CLIENT side. The server side is what
/// these tests are proving.
void configureClient(httplib::SSLClient& client) {
    client.enable_server_certificate_verification(false);
    client.set_connection_timeout(kTimeoutSecs, kTimeoutUsecs);
    client.set_read_timeout(kTimeoutSecs, kTimeoutUsecs);
}

}  // namespace

TEST(HttpBackendTlsTest, ServerOnlyTlsHandshakeSucceeds) {
    TlsFixture f{serverOnlyTls()};
    EXPECT_TRUE(f.backend().tlsEnabled());

    httplib::SSLClient client(kLoopback, f.port());
    configureClient(client);

    auto res = client.Get(kHealthRoute);
    ASSERT_TRUE(res) << "TLS handshake or request failed";
    EXPECT_EQ(res->status, kOkStatus);
}

TEST(HttpBackendTlsTest, MetricsSummaryReportsTlsMode) {
    TlsFixture f{serverOnlyTls()};
    // The operator's only evidence of what the port speaks.
    EXPECT_NE(f.backend().metricsSummary().find("tls server"),
              std::string::npos)
        << f.backend().metricsSummary();
}

TEST(HttpBackendTlsTest, PlaintextClientRejectedByTlsServer) {
    // If TLS were accidentally optional, a cleartext GET would still be
    // answered. It must not be.
    TlsFixture f{serverOnlyTls()};

    httplib::Client plaintext(kLoopback, f.port());
    plaintext.set_connection_timeout(kTimeoutSecs, kTimeoutUsecs);
    plaintext.set_read_timeout(kTimeoutSecs, kTimeoutUsecs);

    auto res = plaintext.Get(kHealthRoute);
    EXPECT_FALSE(res) << "a plaintext client got a response from a TLS port";
}

TEST(HttpBackendTlsTest, MutualTlsRejectsClientWithoutCert) {
    TlsFixture f{mutualTls()};
    EXPECT_TRUE(f.backend().tlsEnabled());

    httplib::SSLClient client(kLoopback, f.port());
    configureClient(client);

    auto res = client.Get(kHealthRoute);
    EXPECT_FALSE(res) << "an unauthenticated client was served under "
                         "verify_peer";
}

TEST(HttpBackendTlsTest, MutualTlsAcceptsClientWithValidCert) {
    TlsFixture f{mutualTls()};

    httplib::SSLClient client(kLoopback, f.port(), fixture("client.crt"),
                              fixture("client.key"));
    configureClient(client);

    auto res = client.Get(kHealthRoute);
    ASSERT_TRUE(res) << "a client with a CA-issued certificate was refused";
    EXPECT_EQ(res->status, kOkStatus);
}

TEST(HttpBackendTlsTest, ConstructorThrowsOnBadCertRatherThanFallingBackToPlaintext) {
    NiceMock<MockProductionModel>    model;
    NiceMock<MockProductsRepository> repo;
    app::presenter::AlertCenter      alerts;
    app::core::Logger logger{std::make_unique<app::core::ConsoleLogger>()};

    HttpTlsOptions broken = serverOnlyTls();
    broken.certPath       = fixture("no-such-server.crt");

    // Constructing is what fails, so no object exists to have bound a
    // port, which is the point: the failure happens before any listener.
    EXPECT_THROW(
        {
            HttpBackend backend(0, kLoopback, model, repo, alerts, logger,
                                broken);
        },
        TlsMaterialError);
}

TEST(HttpBackendTlsTest, WithoutTlsOptionsTheBackendStaysPlaintext) {
    // The default construction path is unchanged: no TLS options, no TLS,
    // and the existing plaintext behaviour is what REQ-INTEGRATION-007
    // covers.
    NiceMock<MockProductionModel>    model;
    NiceMock<MockProductsRepository> repo;
    app::presenter::AlertCenter      alerts;
    app::core::Logger logger{std::make_unique<app::core::ConsoleLogger>()};

    HttpBackend backend(0, kLoopback, model, repo, alerts, logger);
    EXPECT_FALSE(backend.tlsEnabled());

    backend.start();
    httplib::Client plaintext(kLoopback, backend.boundPort());
    plaintext.set_connection_timeout(kTimeoutSecs, kTimeoutUsecs);
    plaintext.set_read_timeout(kTimeoutSecs, kTimeoutUsecs);

    auto res = plaintext.Get(kHealthRoute);
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, kOkStatus);
    backend.stop();
}
