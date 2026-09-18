#include "src/integration/HttpTlsMaterial.h"

#include "src/core/StartupErrors.h"

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include <array>
#include <cstddef>
#include <format>
#include <memory>
#include <string>
#include <utility>

namespace app::integration {

namespace {

// OpenSSL writes its human-readable diagnostic into a caller-supplied
// buffer; 256 bytes is the size its own documentation uses for
// ERR_error_string_n.
inline constexpr std::size_t kOpenSslErrorTextSize = 256;

// OpenSSL's "call succeeded" return for the boolean-style C functions used
// here. Named so the check reads as intent rather than as a bare 1.
inline constexpr int kOpenSslOk = 1;

// The config key each piece of material comes from. A failure names the KEY
// the operator has to fix, not just the file, so the startup error is
// actionable without grepping the schema.
namespace material {
inline constexpr const char* kCertificate = "network.http.tls.cert_path";
inline constexpr const char* kPrivateKey  = "network.http.tls.key_path";
inline constexpr const char* kClientCa    = "network.http.tls.client_ca_path";
}  // namespace material

// Shown in place of a path when the configured value is the empty string,
// so the message cannot read as "'': cannot be opened".
inline constexpr const char* kUnsetPath = "<not set>";

// RAII for the three OpenSSL handles this file touches. Foreign C API --
// the free functions are theirs, the ownership is ours.
struct BioDeleter {
    void operator()(BIO* bio) const noexcept { BIO_free_all(bio); }
};
struct X509Deleter {
    void operator()(X509* cert) const noexcept { X509_free(cert); }
};
struct PrivateKeyDeleter {
    void operator()(EVP_PKEY* key) const noexcept { EVP_PKEY_free(key); }
};

using BioPtr         = std::unique_ptr<BIO, BioDeleter>;
using CertificatePtr = std::unique_ptr<X509, X509Deleter>;
using PrivateKeyPtr  = std::unique_ptr<EVP_PKEY, PrivateKeyDeleter>;

/// Abort the load with a message naming the config key, the path and the
/// reason. Never returns -- a TLS deployment that cannot have TLS is fatal
/// (ADR-0030), so there is no degraded path to fall back to.
[[noreturn]] void refuse(const char* configKey,
                         const std::string& path,
                         const std::string& reason) {
    throw core::TlsMaterialError(
        std::format("{} ({}): {}", configKey,
                    path.empty() ? kUnsetPath : path.c_str(), reason));
}

/// Pop the most recent OpenSSL error and render it. Empty queue means the
/// call failed without queueing a reason (a file that simply holds no PEM
/// object), so we say that rather than inventing a code.
std::string openSslReason() {
    const auto code = ERR_get_error();
    if (code == 0) {
        return "does not contain a PEM object of the expected type";
    }
    std::array<char, kOpenSslErrorTextSize> text{};
    ERR_error_string_n(code, text.data(), text.size());
    ERR_clear_error();
    return {text.data()};
}

/// Open a configured PEM file for reading, refusing an unset path and an
/// unreadable one with distinct messages.
BioPtr openPemFile(const char* configKey, const std::string& path) {
    if (path.empty()) {
        refuse(configKey, path, "is required when network.http.tls.enabled "
                                "is true");
    }
    BioPtr bio{BIO_new_file(path.c_str(), "r")};
    if (!bio) {
        ERR_clear_error();
        refuse(configKey, path, "cannot be opened for reading");
    }
    return bio;
}

/// Read one PEM certificate, refusing anything that is not parseable as
/// X.509 (a truncated file, a DER blob, plain garbage).
CertificatePtr readCertificate(const char* configKey, const std::string& path) {
    const BioPtr bio = openPemFile(configKey, path);
    CertificatePtr cert{
        PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr)};
    if (!cert) {
        refuse(configKey, path, openSslReason());
    }
    return cert;
}

}  // namespace

HttpTlsMaterial::HttpTlsMaterial(HttpTlsOptions options)
    : options_(std::move(options)) {
    // Start from a clean queue so a reason reported below is ours and not a
    // leftover from some earlier OpenSSL user in this process.
    ERR_clear_error();

    const CertificatePtr certificate =
        readCertificate(material::kCertificate, options_.certPath);

    const BioPtr keyBio =
        openPemFile(material::kPrivateKey, options_.keyPath);
    const PrivateKeyPtr privateKey{
        PEM_read_bio_PrivateKey(keyBio.get(), nullptr, nullptr, nullptr)};
    if (!privateKey) {
        refuse(material::kPrivateKey, options_.keyPath, openSslReason());
    }

    // A syntactically fine key that belongs to a DIFFERENT certificate is
    // the failure that would otherwise survive startup and break every
    // handshake at run time. Catch it here, before a socket exists.
    if (X509_check_private_key(certificate.get(), privateKey.get()) !=
        kOpenSslOk) {
        ERR_clear_error();
        refuse(material::kPrivateKey, options_.keyPath,
               "does not match the certificate in " + options_.certPath);
    }

    if (!options_.verifyPeer) {
        return;
    }

    // Mutual TLS: without a CA there is nothing to verify a client
    // certificate against, so verify_peer would be a promise the server
    // cannot keep. Refuse it at the loader, not only at the validator.
    if (options_.clientCaPath.empty()) {
        refuse(material::kClientCa, options_.clientCaPath,
               "is required when network.http.tls.verify_peer is true");
    }
    // Parsed purely to prove it is a readable PEM CA; the server opens the
    // file itself by path, so the parsed object is dropped right here.
    readCertificate(material::kClientCa, options_.clientCaPath);
}

}  // namespace app::integration
