#pragma once

#include "src/auth/PasswordHasher.h"

namespace app::auth {

/// Argon2id hasher backed by libsodium's `crypto_pwhash_str` family.
///
/// Argon2id is the OWASP-recommended password KDF as of 2024 -- it
/// resists both GPU brute-force (memory-hard) and side-channel attacks
/// (data-independent indexing for the first half of the computation,
/// data-dependent for the second). libsodium ships parameters tuned for
/// "interactive" login UX (~50 ms on commodity hardware) which is the
/// right point on the latency / security curve for a single-operator
/// terminal -- a stronger profile would slow the legitimate login
/// without meaningfully changing the attacker's economics for a
/// 3-user installation.
///
/// The libsodium runtime is initialised on first construction via
/// `sodium_init()`; subsequent constructions are no-ops. Failure to
/// initialise throws `std::runtime_error` so the composition root
/// can surface it as a startup error.
class Argon2PasswordHasher : public PasswordHasher {
public:
    Argon2PasswordHasher();
    ~Argon2PasswordHasher() override = default;

    [[nodiscard]] std::string hash(std::string_view plaintext) override;
    [[nodiscard]] bool        verify(std::string_view plaintext,
                                     std::string_view hashedRef) override;
};

}  // namespace app::auth
