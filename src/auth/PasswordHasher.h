#pragma once

#include <string>
#include <string_view>

namespace app::auth {

/// Abstraction over the password hashing algorithm.
///
/// One method per direction so callers cannot accidentally store a
/// verified hash (the typical OWASP cheat-sheet anti-pattern). The
/// concrete (Argon2id today via libsodium) decides parameters; the
/// service layer never touches them.
///
/// SOLID:
///   * S -- the hasher knows two things: how to hash, how to verify.
///     Storage, transport, and policy all live elsewhere.
///   * O -- replacing Argon2 with a future algorithm is a new
///     concrete; AuthService stays untouched.
///   * D -- AuthService depends on this interface; tests inject a
///     mock to assert on call patterns without paying the ~50 ms
///     real Argon2 cost per case.
class PasswordHasher {
public:
    virtual ~PasswordHasher() = default;

    PasswordHasher(const PasswordHasher&)            = delete;
    PasswordHasher& operator=(const PasswordHasher&) = delete;
    PasswordHasher(PasswordHasher&&)                 = delete;
    PasswordHasher& operator=(PasswordHasher&&)      = delete;

    /// Hash a plaintext password. Returns an encoded string that
    /// embeds the algorithm header, parameters, salt, and digest --
    /// libsodium's `crypto_pwhash_str` output format. The returned
    /// string is what callers persist; it is self-describing for
    /// `verify` so a parameter change doesn't break existing rows.
    ///
    /// Throws `std::runtime_error` on allocator failure -- the
    /// algorithm itself is deterministic and cannot fail otherwise.
    [[nodiscard]] virtual std::string hash(std::string_view plaintext) = 0;

    /// Verify a plaintext candidate against a previously hashed value.
    /// Constant-time within the digest itself (the underlying library
    /// uses a constant-time compare); the time taken to derive the
    /// candidate digest still leaks parameter info, which is exactly
    /// what the encoded prefix is meant to broadcast.
    [[nodiscard]] virtual bool verify(std::string_view plaintext,
                                      std::string_view hashedRef) = 0;

protected:
    PasswordHasher() = default;
};

}  // namespace app::auth
