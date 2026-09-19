// [utest->req~integration-011~1]
// Covers REQ-INTEGRATION-011 (OPC-UA Sign&Encrypt) at the loader: the DER
// certificate, private key and trust list are proven readable BEFORE any
// UA_Server or UA_Client exists. Every way they can be unusable is a
// structured, fatal TlsMaterialError naming the config key and the path
// (ADR-0031).
//
// Hermetic: file I/O against the committed DER fixtures in
// tests/fixtures/opcua-security, plus a scratch directory for the cases that
// cannot be committed (an empty directory, a trust list that is a file). No
// sockets, no Xvfb, no server.

#include "src/integration/opcua/Open62541SecurityMaterial.h"

#include "src/core/StartupErrors.h"
#include "src/integration/opcua/OpcUaSecurityOptions.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

using app::core::StartupErrorCode;
using app::core::TlsMaterialError;
using app::integration::opcua::OpcUaSecurityOptions;
using app::integration::opcua::Open62541SecurityMaterial;

namespace {

// The fixture directory is passed in by CMake so the test finds the committed
// DER files regardless of the working directory ctest runs it from.
constexpr std::string_view kFixtureDir = INDUSTRIAL_HMI_OPCUA_FIXTURE_DIR;

// The config subtree the messages must name. The whole point of carrying a
// prefix through the options is that the server and the client have identical
// shapes, so an error has to say WHICH half to edit.
constexpr const char* kServerKeyPrefix = "network.opcua.server.security";

std::string fixture(std::string_view name) {
    return std::string(kFixtureDir) + "/" + std::string(name);
}

/// Server material that is expected to load cleanly: the committed
/// certificate, its key, and a trust list holding one peer.
OpcUaSecurityOptions validServerOptions() {
    OpcUaSecurityOptions options;
    options.enabled         = true;
    options.configKeyPrefix = kServerKeyPrefix;
    options.certPath        = fixture("server.der");
    options.privateKeyPath  = fixture("server-key.der");
    options.trustListDir    = fixture("trustlist");
    return options;
}

/// Build the material and return the message of the TlsMaterialError it must
/// throw. Fails the test if it throws nothing, or throws something else: a
/// plain std::runtime_error would never reach the startup dialog's TLS
/// branch, so the TYPE is part of the contract.
std::string rejectionMessage(const OpcUaSecurityOptions& options) {
    try {
        const Open62541SecurityMaterial material{options};
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

/// A scratch directory unique to one test, removed when the test leaves.
/// Used for the states that cannot be committed to git: a directory with no
/// files in it, and a trust-list path that is a regular file.
class ScratchDir {
public:
    explicit ScratchDir(std::string_view name)
        : path_(std::filesystem::temp_directory_path() /
                ("industrial-hmi-opcua-" + std::string(name))) {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
        std::filesystem::create_directories(path_, ec);
    }

    ~ScratchDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    ScratchDir(const ScratchDir&)            = delete;
    ScratchDir& operator=(const ScratchDir&) = delete;
    ScratchDir(ScratchDir&&)                 = delete;
    ScratchDir& operator=(ScratchDir&&)      = delete;

    [[nodiscard]] std::string path() const { return path_.string(); }

    [[nodiscard]] std::string child(std::string_view name) const {
        return (path_ / name).string();
    }

private:
    std::filesystem::path path_;
};

}  // namespace

TEST(Open62541SecurityMaterialTest, LoadsValidCertKeyAndTrustList) {
    ASSERT_NO_THROW({
        const Open62541SecurityMaterial material{validServerOptions()};
    });

    const Open62541SecurityMaterial material{validServerOptions()};
    EXPECT_GT(material.certificate().length, 0U);
    EXPECT_NE(material.certificate().data, nullptr);
    EXPECT_GT(material.privateKey().length, 0U);
    EXPECT_NE(material.privateKey().data, nullptr);

    // The committed trust list holds exactly one peer certificate. Asserting
    // the count and not just "non-empty" is what would catch a loader that
    // silently skipped files.
    EXPECT_EQ(material.trustListSize(), 1U);
    ASSERT_NE(material.trustList(), nullptr);
    EXPECT_GT(material.trustList()[0].length, 0U);
}

TEST(Open62541SecurityMaterialTest, RejectsMissingCertFile) {
    OpcUaSecurityOptions options = validServerOptions();
    options.certPath = fixture("no-such-certificate.der");

    const std::string message = rejectionMessage(options);
    // The operator gets the KEY to edit, the PATH that failed and the REASON.
    // All three, because any one alone sends them grepping.
    EXPECT_TRUE(mentions(message, "network.opcua.server.security.cert_path"))
        << message;
    EXPECT_TRUE(mentions(message, "no-such-certificate.der")) << message;
    EXPECT_TRUE(mentions(message, "cannot be opened")) << message;
}

TEST(Open62541SecurityMaterialTest, RejectsEmptyCertPath) {
    OpcUaSecurityOptions options = validServerOptions();
    options.certPath.clear();

    const std::string message = rejectionMessage(options);
    EXPECT_TRUE(mentions(message, "network.opcua.server.security.cert_path"))
        << message;
    // An unset path must not render as "'': cannot be opened", which reads
    // like a filesystem fault rather than a missing setting.
    EXPECT_TRUE(mentions(message, "<not set>")) << message;
    EXPECT_TRUE(mentions(message, "is required when")) << message;
}

TEST(Open62541SecurityMaterialTest, RejectsMissingKeyFile) {
    OpcUaSecurityOptions options = validServerOptions();
    options.privateKeyPath = fixture("no-such-key.der");

    const std::string message = rejectionMessage(options);
    EXPECT_TRUE(
        mentions(message, "network.opcua.server.security.private_key_path"))
        << message;
    EXPECT_TRUE(mentions(message, "no-such-key.der")) << message;
}

TEST(Open62541SecurityMaterialTest, RejectsUnreadableTrustListDir) {
    OpcUaSecurityOptions options = validServerOptions();
    options.trustListDir = fixture("no-such-directory");

    const std::string message = rejectionMessage(options);
    EXPECT_TRUE(
        mentions(message, "network.opcua.server.security.trust_list_dir"))
        << message;
    EXPECT_TRUE(mentions(message, "is not a readable directory")) << message;
}

TEST(Open62541SecurityMaterialTest, RejectsTrustListDirThatIsAFile) {
    // The near-miss of the case above: the path exists and is readable, it is
    // just not a directory. Pointing trust_list_dir at a single certificate
    // is the mistake an operator actually makes.
    const ScratchDir scratch{"trustlist-is-a-file"};
    const std::string notADirectory = scratch.child("peer.der");
    {
        std::ofstream file(notADirectory, std::ios::binary);
        file << "not a directory";
    }

    OpcUaSecurityOptions options = validServerOptions();
    options.trustListDir = notADirectory;

    const std::string message = rejectionMessage(options);
    EXPECT_TRUE(
        mentions(message, "network.opcua.server.security.trust_list_dir"))
        << message;
    EXPECT_TRUE(mentions(message, "is not a readable directory")) << message;
}

TEST(Open62541SecurityMaterialTest, RejectsEmptyCertFile) {
    // A zero-byte certificate passes every exists / is-readable check and
    // then fails inside open62541 with a status code that names no file. The
    // loader is the only place that can still say WHICH path was empty.
    const ScratchDir scratch{"empty-cert"};
    const std::string emptyCert = scratch.child("server.der");
    { const std::ofstream file(emptyCert, std::ios::binary); }

    OpcUaSecurityOptions options = validServerOptions();
    options.certPath = emptyCert;

    const std::string message = rejectionMessage(options);
    EXPECT_TRUE(mentions(message, "network.opcua.server.security.cert_path"))
        << message;
    EXPECT_TRUE(mentions(message, "is empty")) << message;
}

TEST(Open62541SecurityMaterialTest, LoadsEmptyTrustListDirWithoutThrowing) {
    // An existing but empty trust list is a deployment STATE, not an error:
    // it is what an operator has while certificates are still being
    // exchanged. Refusing to start over it would make the honest
    // intermediate step impossible.
    const ScratchDir scratch{"empty-trustlist"};

    OpcUaSecurityOptions options = validServerOptions();
    options.trustListDir = scratch.path();

    ASSERT_NO_THROW({ const Open62541SecurityMaterial material{options}; });

    const Open62541SecurityMaterial material{options};
    EXPECT_EQ(material.trustListSize(), 0U);
    EXPECT_EQ(material.trustList(), nullptr);
}

TEST(Open62541SecurityMaterialTest, NamesTheClientSubtreeWhenTheClientFails) {
    // The server and the client have identical option shapes. A message that
    // hard-coded one subtree would send an operator to the wrong half of the
    // config file, which is the whole reason the prefix travels with the
    // options.
    OpcUaSecurityOptions options = validServerOptions();
    options.configKeyPrefix = "network.opcua.client.security";
    options.certPath        = fixture("no-such-certificate.der");

    const std::string message = rejectionMessage(options);
    EXPECT_TRUE(mentions(message, "network.opcua.client.security.cert_path"))
        << message;
    EXPECT_FALSE(mentions(message, "network.opcua.server.security"))
        << message;
}

TEST(Open62541SecurityMaterialTest, KeepsTheOptionsItWasProvenAgainst) {
    const Open62541SecurityMaterial material{validServerOptions()};
    EXPECT_EQ(material.options().certPath, fixture("server.der"));
    EXPECT_EQ(material.options().privateKeyPath, fixture("server-key.der"));
    EXPECT_TRUE(material.options().enabled);
}
