#include "src/auth/Argon2PasswordHasher.h"

#include <sodium.h>

#include <stdexcept>
#include <string>

namespace app::auth {

namespace {

/// Process-wide flag so we only call `sodium_init()` once even if
/// multiple Argon2PasswordHasher instances exist (e.g. tests
/// constructing local hashers). libsodium documents sodium_init() as
/// idempotent and thread-safe, but the flag also saves the call.
bool ensureSodiumInitialised() {
    // sodium_init returns:
    //    0 on first successful init
    //    1 if already initialised
    //   -1 on failure
    // Anything < 0 means we cannot guarantee CSPRNG / SIMD-safe
    // primitives; refuse to operate.
    static const bool kOk = []() {
        const int rc = ::sodium_init();
        return rc >= 0;
    }();
    return kOk;
}

}  // namespace

Argon2PasswordHasher::Argon2PasswordHasher() {
    if (!ensureSodiumInitialised()) {
        throw std::runtime_error(
            "libsodium initialisation failed -- cannot hash passwords");
    }
}

std::string Argon2PasswordHasher::hash(std::string_view plaintext) {
    // crypto_pwhash_str writes the algorithm + parameters + salt +
    // digest as a single null-terminated string into the caller's
    // buffer of size STRBYTES. The `_INTERACTIVE` profile is the
    // libsodium-tuned ops/mem cost for ~50 ms latency on a typical
    // x86_64 -- right balance for a login dialog.
    std::string out;
    out.resize(crypto_pwhash_STRBYTES);

    const int rc = ::crypto_pwhash_str(
        out.data(),
        plaintext.data(),
        plaintext.size(),
        crypto_pwhash_OPSLIMIT_INTERACTIVE,
        crypto_pwhash_MEMLIMIT_INTERACTIVE);
    if (rc != 0) {
        // Documented failure mode is OOM (the memory-hard derivation
        // allocates ~64 MiB by default). Surface as a runtime error so
        // the caller can return a typed login result.
        throw std::runtime_error("crypto_pwhash_str failed (OOM?)");
    }

    // Trim to the actual C-string length; libsodium pads with zeros to
    // STRBYTES and the trailing bytes confuse code that compares
    // strings later (the SQL roundtrip via TEXT loses them anyway but
    // the in-memory hash must match what we persist).
    out.resize(std::char_traits<char>::length(out.c_str()));
    return out;
}

bool Argon2PasswordHasher::verify(std::string_view plaintext,
                                  std::string_view hashedRef) {
    // crypto_pwhash_str_verify wants a NUL-terminated string for the
    // reference. The DTO already stores it as a std::string -- here we
    // accept a string_view for the interface uniformity but must
    // materialise a copy to guarantee null termination. The hashed
    // form is ~100 bytes; the allocation cost is dwarfed by the
    // ~50 ms KDF derivation that follows.
    const std::string ref(hashedRef);
    const int rc = ::crypto_pwhash_str_verify(
        ref.c_str(),
        plaintext.data(),
        plaintext.size());
    return rc == 0;
}

}  // namespace app::auth
