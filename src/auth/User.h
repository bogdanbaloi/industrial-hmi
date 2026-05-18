#pragma once

#include "src/auth/Role.h"

#include <cstdint>
#include <string>

namespace app::auth {

/// Domain DTO -- one record per active or disabled account. Plain
/// aggregate with no invariants of its own; validation lives in the
/// service / repository layer so callers can build partial Users for
/// CRUD intents without faking required fields.
///
/// `passwordHash` is the Argon2id encoded string (algorithm + params +
/// salt + digest all in one). It is NEVER the plaintext; the password
/// hasher converts at the service boundary and discards the original
/// immediately. Storing the encoded string (rather than splitting
/// algo / salt / digest into separate columns) lets us migrate hash
/// parameters without a schema change -- libsodium reads the header
/// and routes to the right verifier.
///
/// Avatar payload is intentionally NOT carried on this struct -- the
/// repository exposes separate `getAvatar(id)` / `setAvatar(...)` calls
/// so the login hot path (findByUsername) doesn't drag a 256KB BLOB
/// through every query. Callers that need the bytes (UsersPage row
/// renderer, badge widget) fetch them lazily; `avatarMime` here is the
/// presence flag -- empty means "no avatar uploaded, render initials".
struct User {
    /// Auto-increment from the SQLite schema; -1 for not-yet-persisted
    /// users (the repository fills it in after INSERT).
    std::int64_t id{-1};

    /// Lowercased, ASCII-only, unique. The UI lowercases at submit so
    /// "Admin" and "admin" can't collide accidentally.
    std::string  username;

    /// Argon2id encoded hash. Never plaintext.
    std::string  passwordHash;

    Role         role{Role::Operator};

    /// `false` disables login without deleting the audit trail. A
    /// removed user would lose the foreign-key reference from old
    /// audit rows; soft-disable preserves the chain.
    bool         enabled{true};

    /// Human-readable label shown in the UI ("Alice Bloomberg") when
    /// the operator prefers something friendlier than `username`. Empty
    /// means "fall back to the username at render time" -- the column
    /// is nullable in the schema for the same reason.
    std::string  displayName;

    /// MIME type of the avatar blob, empty when no avatar is uploaded.
    /// Presence flag for the UI: render the BLOB on the next
    /// `getAvatar(id)` call when non-empty, otherwise draw generated
    /// initials. Restricted to "image/png" / "image/jpeg" by the
    /// repository on write.
    std::string  avatarMime;

    /// ISO 8601 wall-clock timestamp the row was inserted. Populated
    /// server-side at INSERT time so the audit trail's "user created"
    /// event can quote a single canonical value.
    std::string  createdAt;

    /// ISO 8601 timestamp the row was last mutated (password change,
    /// role change, avatar swap, enable/disable). Updated by the
    /// repository on every successful UPDATE / setAvatar call so the
    /// admin UI can sort users by "recently modified" without joining
    /// the audit log.
    std::string  updatedAt;
};

}  // namespace app::auth
