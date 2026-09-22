// [utest->req~core-005~1]
// Covers REQ-CORE-005 (semantic validation of app-config.json).
//
// Tests for app::config::ConfigValidator. Each test writes a minimal
// JSON config to a unique temp file, initialises the ConfigManager
// singleton against it, and asserts the validator either accepts or
// rejects the specific value under test. We rely on ConfigManager's
// own initialize() to populate the in-memory map -- so a failing
// validator test that points at JSON the manager couldn't parse is
// telling us about loadConfig, not the rule.

#include "src/config/ConfigManager.h"
#include "src/config/ConfigValidator.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
using app::config::ConfigManager;
using app::config::ConfigValidator;

namespace {

void writeFile(const fs::path& p, const std::string& content) {
    std::ofstream out(p, std::ios::trunc);
    out << content;
}

/// Wrap a body of `"section": { ... }` blocks in the top-level braces
/// expected by ConfigManager. Saves on string concatenation noise in
/// every test.
std::string wrap(const std::string& body) {
    std::ostringstream os;
    os << "{\n" << body << "\n}\n";
    return os.str();
}

class ConfigValidatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto* info =
            ::testing::UnitTest::GetInstance()->current_test_info();
        std::string name = info ? info->name() : "unnamed";
        tmpPath_ = fs::temp_directory_path() /
                   ("industrial-hmi-validator-" + name + ".json");
        ConfigManager::instance().clear();
    }

    void TearDown() override {
        ConfigManager::instance().clear();
        std::error_code ec;
        fs::remove(tmpPath_, ec);
    }

    fs::path tmpPath_;
};

}  // namespace

TEST_F(ConfigValidatorTest, AcceptsMinimalValidConfig) {
    // A bare config relies on every default in ConfigManager. Defaults
    // are deliberately chosen to pass validation -- a fresh install
    // must never trip ConfigInvalid.
    writeFile(tmpPath_, "{}\n");
    ASSERT_TRUE(ConfigManager::instance().initialize(tmpPath_.string()));

    auto r = ConfigValidator::validate(ConfigManager::instance());
    EXPECT_TRUE(r.ok) << "first error: "
                      << (r.errors.empty() ? "<none>" : r.errors.front());
}

TEST_F(ConfigValidatorTest, RejectsUnknownLogLevel) {
    writeFile(tmpPath_, wrap(R"(  "logging": { "level": "VERBOSE" })"));
    ASSERT_TRUE(ConfigManager::instance().initialize(tmpPath_.string()));

    auto r = ConfigValidator::validate(ConfigManager::instance());
    EXPECT_FALSE(r.ok);
    // We don't require an exact string match -- the prefix is part of
    // the contract the operator dialog displays. The rest is wording.
    ASSERT_FALSE(r.errors.empty());
    EXPECT_NE(r.errors.front().find("logging.level"), std::string::npos);
}

TEST_F(ConfigValidatorTest, RejectsUnknownLanguageCode) {
    writeFile(tmpPath_, wrap(R"(  "i18n": { "language": "klingon" })"));
    ASSERT_TRUE(ConfigManager::instance().initialize(tmpPath_.string()));

    auto r = ConfigValidator::validate(ConfigManager::instance());
    EXPECT_FALSE(r.ok);
    ASSERT_FALSE(r.errors.empty());
    EXPECT_NE(r.errors.front().find("i18n.language"), std::string::npos);
}

TEST_F(ConfigValidatorTest, AcceptsAutoLanguage) {
    writeFile(tmpPath_, wrap(R"(  "i18n": { "language": "auto" })"));
    ASSERT_TRUE(ConfigManager::instance().initialize(tmpPath_.string()));

    auto r = ConfigValidator::validate(ConfigManager::instance());
    EXPECT_TRUE(r.ok);
}

TEST_F(ConfigValidatorTest, RejectsNonPositiveWindowDimensions) {
    writeFile(tmpPath_, wrap(R"(  "window": { "default_width": 0, "default_height": -1 })"));
    ASSERT_TRUE(ConfigManager::instance().initialize(tmpPath_.string()));

    auto r = ConfigValidator::validate(ConfigManager::instance());
    EXPECT_FALSE(r.ok);
    // Two rules tripped (width AND height) -- validator must collect
    // every violation, not bail on the first.
    EXPECT_EQ(r.errors.size(), 2u);
}

TEST_F(ConfigValidatorTest, RejectsOutOfRangeTcpPortWhenEnabled) {
    writeFile(tmpPath_, wrap(R"(  "network": { "tcp": { "enabled": true, "port": 70000 } })"));
    ASSERT_TRUE(ConfigManager::instance().initialize(tmpPath_.string()));

    auto r = ConfigValidator::validate(ConfigManager::instance());
    EXPECT_FALSE(r.ok);
    ASSERT_FALSE(r.errors.empty());
    EXPECT_NE(r.errors.front().find("network.tcp.port"), std::string::npos);
}

TEST_F(ConfigValidatorTest, IgnoresOutOfRangeTcpPortWhenDisabled) {
    // Same bogus port -- but enabled=false. Validator must not block
    // startup over a backend the operator isn't turning on.
    writeFile(tmpPath_, wrap(R"(  "network": { "tcp": { "enabled": false, "port": 70000 } })"));
    ASSERT_TRUE(ConfigManager::instance().initialize(tmpPath_.string()));

    auto r = ConfigValidator::validate(ConfigManager::instance());
    EXPECT_TRUE(r.ok);
}

TEST_F(ConfigValidatorTest, RejectsHistorianBatchSizeZeroWhenEnabled) {
    writeFile(tmpPath_, wrap(R"(  "historian": { "enabled": true, "batch_size": 0, "batch_age_ms": 1000, "sweep_interval_ms": 1000 })"));
    ASSERT_TRUE(ConfigManager::instance().initialize(tmpPath_.string()));

    auto r = ConfigValidator::validate(ConfigManager::instance());
    EXPECT_FALSE(r.ok);
    ASSERT_FALSE(r.errors.empty());
    EXPECT_NE(r.errors.front().find("historian.batch_size"),
              std::string::npos);
}

// [utest->req~integration-010~1]
// TLS for the HTTP/REST backend: the validator's share of REQ-INTEGRATION-010
// is the SHAPE of network.http.tls (which paths the operator's own settings
// oblige them to supply). Whether those files parse is HttpTlsMaterial's job,
// covered by HttpTlsMaterialTest.

TEST_F(ConfigValidatorTest, RejectsHttpTlsEnabledWithoutCertPath) {
    writeFile(tmpPath_, wrap(R"(  "network": { "http": { "enabled": true, "port": 8080, "tls": { "enabled": true, "key_path": "server.key" } } })"));
    ASSERT_TRUE(ConfigManager::instance().initialize(tmpPath_.string()));

    auto r = ConfigValidator::validate(ConfigManager::instance());
    EXPECT_FALSE(r.ok);
    ASSERT_FALSE(r.errors.empty());
    EXPECT_NE(r.errors.front().find("network.http.tls.cert_path"),
              std::string::npos);
}

TEST_F(ConfigValidatorTest, RejectsHttpTlsVerifyPeerWithoutClientCa) {
    writeFile(tmpPath_, wrap(R"(  "network": { "http": { "enabled": true, "port": 8080, "tls": { "enabled": true, "cert_path": "server.crt", "key_path": "server.key", "verify_peer": true } } })"));
    ASSERT_TRUE(ConfigManager::instance().initialize(tmpPath_.string()));

    auto r = ConfigValidator::validate(ConfigManager::instance());
    EXPECT_FALSE(r.ok);
    ASSERT_FALSE(r.errors.empty());
    EXPECT_NE(r.errors.front().find("network.http.tls.client_ca_path"),
              std::string::npos);
}

TEST_F(ConfigValidatorTest, AcceptsHttpTlsFullyConfigured) {
    writeFile(tmpPath_, wrap(R"(  "network": { "http": { "enabled": true, "port": 8443, "tls": { "enabled": true, "cert_path": "server.crt", "key_path": "server.key", "verify_peer": true, "client_ca_path": "client-ca.crt" } } })"));
    ASSERT_TRUE(ConfigManager::instance().initialize(tmpPath_.string()));

    auto r = ConfigValidator::validate(ConfigManager::instance());
    EXPECT_TRUE(r.ok) << "first error: "
                      << (r.errors.empty() ? "<none>" : r.errors.front());
}

TEST_F(ConfigValidatorTest, IgnoresIncompleteHttpTlsWhenTlsDisabled) {
    // Leftover TLS keys with tls.enabled=false must not block startup --
    // same posture as a disabled backend's out-of-range port.
    writeFile(tmpPath_, wrap(R"(  "network": { "http": { "enabled": true, "port": 8080, "tls": { "enabled": false, "verify_peer": true } } })"));
    ASSERT_TRUE(ConfigManager::instance().initialize(tmpPath_.string()));

    auto r = ConfigValidator::validate(ConfigManager::instance());
    EXPECT_TRUE(r.ok) << "first error: "
                      << (r.errors.empty() ? "<none>" : r.errors.front());
}

// [utest->req~integration-011~1]
// Sign&Encrypt for the OPC-UA endpoints: the validator's share of
// REQ-INTEGRATION-011 is the SHAPE of network.opcua.*.security (which paths
// the operator's own settings oblige them to supply). Whether those files
// hold readable DER is Open62541SecurityMaterial's job, covered by
// Open62541SecurityMaterialTest. The validator links neither open62541 nor
// OpenSSL, because it compiles into every build.

TEST_F(ConfigValidatorTest, RejectsOpcUaServerSecurityWithoutCertPath) {
    writeFile(tmpPath_, wrap(R"(  "network": { "opcua": { "enabled": true, "server": { "security": { "enabled": true, "private_key_path": "server-key.der", "trust_list_dir": "trustlist" } } } })"));
    ASSERT_TRUE(ConfigManager::instance().initialize(tmpPath_.string()));

    auto r = ConfigValidator::validate(ConfigManager::instance());
    EXPECT_FALSE(r.ok);
    ASSERT_FALSE(r.errors.empty());
    EXPECT_NE(r.errors.front().find("network.opcua.server.security.cert_path"),
              std::string::npos);
}

TEST_F(ConfigValidatorTest, RejectsOpcUaServerSecurityWithoutTrustListDir) {
    writeFile(tmpPath_, wrap(R"(  "network": { "opcua": { "enabled": true, "server": { "security": { "enabled": true, "cert_path": "server.der", "private_key_path": "server-key.der" } } } })"));
    ASSERT_TRUE(ConfigManager::instance().initialize(tmpPath_.string()));

    auto r = ConfigValidator::validate(ConfigManager::instance());
    EXPECT_FALSE(r.ok);
    ASSERT_FALSE(r.errors.empty());
    EXPECT_NE(
        r.errors.front().find("network.opcua.server.security.trust_list_dir"),
        std::string::npos);
}

TEST_F(ConfigValidatorTest, RejectsOpcUaClientSecurityUnderItsOwnSubtree) {
    // The two endpoints have identical option shapes. An error that named the
    // server subtree here would send the operator to the wrong half of the
    // file, so the subtree in the message is part of the contract.
    writeFile(tmpPath_, wrap(R"(  "network": { "opcua": { "enabled": true, "client": { "enabled": true, "security": { "enabled": true, "cert_path": "client.der", "trust_list_dir": "trustlist" } } } })"));
    ASSERT_TRUE(ConfigManager::instance().initialize(tmpPath_.string()));

    auto r = ConfigValidator::validate(ConfigManager::instance());
    EXPECT_FALSE(r.ok);
    ASSERT_FALSE(r.errors.empty());
    EXPECT_NE(
        r.errors.front().find(
            "network.opcua.client.security.private_key_path"),
        std::string::npos);
}

TEST_F(ConfigValidatorTest, AcceptsOpcUaSecurityFullyConfigured) {
    writeFile(tmpPath_, wrap(R"(  "network": { "opcua": { "enabled": true, "server": { "security": { "enabled": true, "cert_path": "server.der", "private_key_path": "server-key.der", "trust_list_dir": "trustlist" } } } })"));
    ASSERT_TRUE(ConfigManager::instance().initialize(tmpPath_.string()));

    auto r = ConfigValidator::validate(ConfigManager::instance());
    EXPECT_TRUE(r.ok) << "first error: "
                      << (r.errors.empty() ? "<none>" : r.errors.front());
}

TEST_F(ConfigValidatorTest, IgnoresIncompleteOpcUaSecurityWhenSecurityDisabled) {
    // Leftover security keys with security.enabled=false must not block
    // startup, the same posture the HTTP TLS block takes.
    writeFile(tmpPath_, wrap(R"(  "network": { "opcua": { "enabled": true, "server": { "security": { "enabled": false, "cert_path": "server.der" } } } })"));
    ASSERT_TRUE(ConfigManager::instance().initialize(tmpPath_.string()));

    auto r = ConfigValidator::validate(ConfigManager::instance());
    EXPECT_TRUE(r.ok) << "first error: "
                      << (r.errors.empty() ? "<none>" : r.errors.front());
}

TEST_F(ConfigValidatorTest, IgnoresOpcUaSecurityWhenTheBackendIsDisabled) {
    // A security block under a backend that never starts describes an
    // endpoint that never listens. Refusing to boot over it would be noise.
    writeFile(tmpPath_, wrap(R"(  "network": { "opcua": { "enabled": false, "server": { "security": { "enabled": true } } } })"));
    ASSERT_TRUE(ConfigManager::instance().initialize(tmpPath_.string()));

    auto r = ConfigValidator::validate(ConfigManager::instance());
    EXPECT_TRUE(r.ok) << "first error: "
                      << (r.errors.empty() ? "<none>" : r.errors.front());
}

TEST_F(ConfigValidatorTest, CollectsAllViolations) {
    // Multiple unrelated bad values -- assert the validator returns
    // every one. Operators get one round-trip instead of a fix-and-retry
    // loop.
    writeFile(tmpPath_, wrap(R"(
      "logging": { "level": "PANIC" },
      "i18n":    { "language": "xx" },
      "window":  { "default_width": -5 }
    )"));
    ASSERT_TRUE(ConfigManager::instance().initialize(tmpPath_.string()));

    auto r = ConfigValidator::validate(ConfigManager::instance());
    EXPECT_FALSE(r.ok);
    EXPECT_GE(r.errors.size(), 3u);
}
