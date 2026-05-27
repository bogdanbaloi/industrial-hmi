// Implements: REQ-AUTH-001 (password hashing via Argon2id).
//
// Tests for Argon2PasswordHasher.
//
// Argon2 is intentionally slow (~50 ms per hash on commodity hardware
// at the INTERACTIVE profile), so each test case below executes one
// hash + one verify at most -- a tighter loop would spin up the suite
// time without exercising additional behaviour. The cases together
// cover: success round-trip, wrong-password rejection, distinct salts
// per hash (no determinism), and unicode tolerance.

#include "src/auth/Argon2PasswordHasher.h"

#include <gtest/gtest.h>

#include <string>

namespace {
using app::auth::Argon2PasswordHasher;
}

TEST(Argon2PasswordHasherTest, RoundTripSuccess) {
    Argon2PasswordHasher hasher;
    const auto h = hasher.hash("hunter2");
    EXPECT_FALSE(h.empty());
    EXPECT_TRUE(hasher.verify("hunter2", h));
}

TEST(Argon2PasswordHasherTest, WrongPasswordRejected) {
    Argon2PasswordHasher hasher;
    const auto h = hasher.hash("correct horse battery staple");
    EXPECT_FALSE(hasher.verify("not the password", h));
}

TEST(Argon2PasswordHasherTest, HashesAreSalted) {
    // Two hashes of the same plaintext must differ -- the encoded
    // string embeds a fresh random salt per call. Without unique
    // salts a stolen hash database could be reversed via a single
    // rainbow table.
    Argon2PasswordHasher hasher;
    const auto h1 = hasher.hash("pa55w0rd");
    const auto h2 = hasher.hash("pa55w0rd");
    EXPECT_NE(h1, h2);
    // Both still verify against the original plaintext.
    EXPECT_TRUE(hasher.verify("pa55w0rd", h1));
    EXPECT_TRUE(hasher.verify("pa55w0rd", h2));
}

TEST(Argon2PasswordHasherTest, EmptyPasswordRoundTrips) {
    // Edge case -- not recommended for production but the hasher
    // contract must not depend on a minimum length.
    Argon2PasswordHasher hasher;
    const auto h = hasher.hash("");
    EXPECT_TRUE(hasher.verify("", h));
    EXPECT_FALSE(hasher.verify(" ", h));
}

TEST(Argon2PasswordHasherTest, UnicodePassword) {
    // UTF-8 multibyte sequences must pass through unchanged.
    Argon2PasswordHasher hasher;
    const std::string pwd = "p\xc3\xa1\xc8\x99wd\xc3\xae";  // pášwdî
    const auto h = hasher.hash(pwd);
    EXPECT_TRUE(hasher.verify(pwd, h));
    EXPECT_FALSE(hasher.verify(pwd + "X", h));
}

TEST(Argon2PasswordHasherTest, MalformedReferenceRejected) {
    // A reference that didn't come out of `hash()` (e.g. plain text
    // that survived a botched migration) must verify as false rather
    // than throwing.
    Argon2PasswordHasher hasher;
    EXPECT_FALSE(hasher.verify("anything", "not-a-real-hash"));
}
