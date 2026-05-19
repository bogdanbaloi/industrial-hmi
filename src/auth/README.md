# `src/auth/` -- Authentication, RBAC, Audit

GTK-free authentication + role-based access control + audit logging core
for the Industrial HMI. Drops into any C++20 project that needs username/
password sign-in, a small role hierarchy, and an append-only audit
trail; depends only on `sqlite3` and `libsodium`. No GUI, no presenter,
no model coupling -- composes via dependency-injected references at the
boundaries.

---

## Why this module exists separately

A typical HMI / SCADA terminal is a **single-operator** station with
three workforce roles (shift operator, maintenance technician,
administrator). The auth + RBAC + audit triad is the spine that makes
the rest of the binary safe to deploy on a shop floor:

- **Auth** keeps the keyboard out of unauthenticated hands.
- **RBAC** stops the operator from accidentally triggering a calibration
  cycle that interrupts production, or from deleting a product master.
- **Audit** records who did what, for the eventual regulatory walk
  (21 CFR Part 11, GMP, EU Annex 11, FDA validation).

These concerns are *cross-cutting* -- every presenter touches them, the
sidebar widget needs them, the login dialog runs before MainWindow
exists. Keeping them in a self-contained GTK-free module means the
console front-end, unit tests, and any future Qt/Web/REST front-end
reuse the same core without dragging GUI dependencies in.

---

## Architecture (SOLID at a glance)

```
                ┌──────────────┐
                │ PasswordHasher│   interface (hash + verify)
                └──────▲───────┘
                       │
        ┌──────────────┴──────────────┐
        │ Argon2PasswordHasher        │   libsodium concrete
        └─────────────────────────────┘

                ┌──────────────┐
                │UserRepository│   interface (CRUD + avatar BLOB)
                └──────▲───────┘
                       │
        ┌──────────────┴──────────────┐
        │ SqliteUserRepository        │   SQLite concrete
        │ PRAGMA user_version migration│
        └─────────────────────────────┘

                ┌──────────────┐
                │ AuditLogger  │   interface (record + query)
                └──────▲───────┘
                       │
        ┌──────────────┴──────────────┐
        │ SqliteAuditLogger           │   SQLite concrete
        │ (category, ts) compound idx │
        └─────────────────────────────┘

      AuthService(UserRepository&, PasswordHasher&, Session&)
         |
         +-- login(user, pw) -> LoginResult  // verifies + sets session
         +-- logout()                        // clears session
         +-- seedDefaultUsersIfEmpty()       // first-launch bootstrap
         +-- setAuditLogger(audit)           // opt-in: emit AUTH events

      UsersPresenter(UserRepository&, PasswordHasher&, Session&, AuditLogger&)
         |
         +-- list / create / update / remove / resetPassword
         +-- changeOwnPassword / setOwnAvatar / clearOwnAvatar / getAvatar
         +-- enforces RBAC + emits USER-category audit rows
         +-- (lives in src/presenter/ but consumes only src/auth/ + Session)
```

**SOLID applied per interface:**

- **S**ingle responsibility -- every class does exactly one thing.
  `PasswordHasher` hashes + verifies. `UserRepository` persists.
  `Session` holds the current user. `AuthService` composes them.
- **O**pen/closed -- adding a new auth backend (LDAP, OAuth claim cache)
  is a new `UserRepository` impl + a `setAuthService(new_impl)` line in
  `main.cpp`. Zero changes to `AuthService`, `LoginDialog`, or any
  presenter.
- **L**iskov -- `MockUserRepository` (in `tests/`) drops in for the real
  one; `AuthServiceTest` runs against either with identical semantics.
- **I**nterface segregation -- `UserRepository` lookups (`findByUsername`,
  `findById`, `listAll`) deliberately omit the avatar BLOB; callers
  that need it issue a follow-up `getAvatar(id)` so the login hot path
  doesn't drag a 256 KiB photo through SQLite.
- **D**ependency inversion -- `AuthService` depends on three interface
  references handed in by the composition root; no globals, no
  singletons in this layer.

---

## API surface -- class-by-class

### `Role.h`

```cpp
enum class Role : std::uint8_t { Operator = 0, Maintenance = 1, Admin = 2 };

constexpr std::string_view roleName(Role) noexcept;
constexpr Role             parseRole(std::string_view) noexcept;

// Permission helpers -- read as English at the call site.
constexpr bool canStartStop(Role)      noexcept;   // Operator+
constexpr bool canCalibrate(Role)      noexcept;   // Maintenance+
constexpr bool canResetSystem(Role)    noexcept;   // Maintenance+
constexpr bool canEditProducts(Role)   noexcept;   // Maintenance+
constexpr bool canManageUsers(Role)    noexcept;   // Admin
constexpr bool canViewAuditLog(Role)   noexcept;   // Admin
constexpr bool canDismissAlerts(Role)  noexcept;   // Operator+
```

**Three roles** is the sweet spot every industrial deployment uses;
finer matrices look good on slides but become an HR nightmare to
administer. Numeric ordering means permission checks read as `role >=
required`. New roles slot in by re-numbering; the schema stores the
integer not the name, so a future "Auditor" between Operator and
Maintenance is a one-line migration.

### `User.h`

Plain DTO. `passwordHash` is the Argon2id encoded string (algorithm +
params + salt + digest all in one); NEVER plaintext. Avatar payload is
*not* on this struct -- the repository exposes separate
`getAvatar(id)` / `setAvatar(...)` calls so reads stay cheap.

### `PasswordHasher.h` + `Argon2PasswordHasher.{h,cpp}`

```cpp
class PasswordHasher {
    virtual std::string hash(std::string_view plaintext) = 0;
    virtual bool        verify(std::string_view plaintext,
                               std::string_view hashedRef) = 0;
};
```

Argon2id concrete uses libsodium's `crypto_pwhash_str`. The encoded
output is **self-describing** (params live in the header), so a future
parameter change doesn't break existing rows -- libsodium reads the
header and routes to the right verifier. INTERACTIVE profile by
default (~50 ms / hash) -- fast enough for a single-operator terminal,
slow enough to deter brute force.

### `Session.h`

Process-wide "who is currently logged in" state. Mutex-guarded;
returns snapshots by value so callers iterate without holding the
lock. Emits `signalChanged` on every `setUser` / `clear` so view
widgets (UserBadge) re-render without polling.

### `UserRepository.h` + `SqliteUserRepository.{h,cpp}`

```cpp
class UserRepository {
    virtual std::optional<User> findByUsername(std::string_view);  // no avatar
    virtual std::optional<User> findById(std::int64_t);             // no avatar
    virtual std::vector<User>   listAll();                          // no avatars
    virtual std::optional<User> create(const User&);
    virtual bool                update(const User&);
    virtual bool                remove(std::int64_t);
    virtual bool                setAvatar(id, bytes, mime);
    virtual bool                clearAvatar(std::int64_t);
    virtual std::optional<Avatar> getAvatar(std::int64_t);
    virtual std::size_t         count() const;
};
```

Schema (current = v2):

```sql
CREATE TABLE users (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    username      TEXT    NOT NULL UNIQUE COLLATE NOCASE,
    password_hash TEXT    NOT NULL,
    role          INTEGER NOT NULL,
    enabled       INTEGER NOT NULL DEFAULT 1,
    created_at    TEXT    NOT NULL,
    display_name  TEXT,                          -- v2
    avatar_mime   TEXT,                          -- v2 ("image/png" | "image/jpeg")
    avatar_blob   BLOB,                          -- v2 (<= 256 KiB)
    updated_at    TEXT                           -- v2
);
```

Migration uses `PRAGMA user_version` -- idempotent + branch-agnostic.
The ladder lives in `applyMigrations()`; new revisions add a `case`,
never edit a shipped one (deployed DB may already have applied it).

**Avatar storage rationale**: BLOB in DB rather than filesystem +
path. Single source of truth for backup (one `.db` file restores
everything), atomic CRUD (delete user + delete avatar = one
transaction), zero filesystem permission issues in Docker containers.
Hard cap at 256 KiB enforced at the repository boundary so an
oversized upload never reaches SQLite.

### `AuthService.{h,cpp}`

```cpp
class AuthService {
    LoginResult login(std::string_view username, std::string_view password);
    void        logout();
    std::size_t seedDefaultUsersIfEmpty();   // 3 demo accounts
};
```

`LoginResult` merges *wrong username* and *wrong password* into one
`InvalidCredentials` code -- user-enumeration mitigation. The UI cannot
tell which one was wrong; the audit row records the attempted
username + the merged result.

### `AuditEvent.h` + `AuditLogger.h` + `SqliteAuditLogger.{h,cpp}`

Append-only audit trail. The schema denormalises username + role
strings onto every row -- historical entries remain readable even
after the underlying user is deleted (no JOIN against the live users
table needed by an auditor walking the log six months from now).

```sql
CREATE TABLE audit_log (
    id        INTEGER PRIMARY KEY AUTOINCREMENT,
    ts        TEXT NOT NULL,
    username  TEXT NOT NULL,
    role      TEXT NOT NULL,
    category  TEXT NOT NULL,    -- AUTH / USER / PRODUCTION / EQUIPMENT / PRODUCT / ALERT
    action    TEXT NOT NULL,    -- LOGIN / CREATE / UPDATE / RESET_PASSWORD / ...
    details   TEXT NOT NULL,    -- free-form context
    result    TEXT NOT NULL     -- SUCCESS / FAILURE
);
CREATE INDEX idx_audit_ts       ON audit_log(ts);
CREATE INDEX idx_audit_category ON audit_log(category, ts);
```

`AuditQuery` lets the read side filter on category, action, result,
time range, username -- combined with AND. The compound `(category,
ts)` index keeps a category-scoped range scan O(log n) even at
millions of rows.

### `AvatarPlaceholder.{h,cpp}`

Pure-logic helpers for the fallback "no upload" avatar (small
coloured tile with initials):

- `computeInitials(displayName, username)` -- GitHub/Slack convention.
  Prefers multi-word display name (`"Alice Bloomberg"` -> `"AB"`),
  falls back to the first two ASCII letters of `username`.
- `pickPaletteColor(username)` -- FNV-1a mod 8 over a curated palette
  of 8 high-contrast hues. Deterministic + case-insensitive so the
  badge colour stays stable across sessions.

GTK-free on purpose -- the actual rasterisation lives in
`src/gtk/view/widgets/AvatarWidget.{h,cpp}` and a future Qt/web
front-end can reuse the initials + colour decisions without dragging
GTK in.

---

## Embedding in another C++ project

Minimum dependency footprint: `sqlite3`, `libsodium`, C++20 compiler.
Drop the `src/auth/` folder into your tree (plus `src/core/Logger*.h`
for the optional logger interface).

### Bootstrap (~15 lines)

```cpp
#include "auth/SqliteUserRepository.h"
#include "auth/Argon2PasswordHasher.h"
#include "auth/SqliteAuditLogger.h"
#include "auth/AuthService.h"
#include "auth/Session.h"

app::auth::Session                  session;
app::auth::SqliteUserRepository     users {{ .dbPath = "data/auth.sqlite" }};
app::auth::Argon2PasswordHasher     hasher;
app::auth::SqliteAuditLogger        audit {{ .dbPath = "data/audit.sqlite" }};
app::auth::AuthService              authService(users, hasher, session);

users.initialize();          // creates schema, runs migrations
audit.initialize();
authService.setAuditLogger(audit);
authService.seedDefaultUsersIfEmpty();   // operator / maintenance / admin
```

### Sign in / sign out

```cpp
const auto outcome = authService.login("operator", "operpass");
if (outcome == app::auth::LoginResult::Success) {
    auto user = session.currentUser();
    // ... proceed with authenticated path
}

// Later:
authService.logout();
```

### RBAC at a call site

```cpp
auto current = session.currentUser();
if (!current || !app::auth::canCalibrate(current->role)) {
    return Forbidden;
}
// ... run the calibration cycle
```

### Recording an audit event

```cpp
app::auth::AuditEvent e;
e.category = app::auth::category::kProduction;
e.action   = "START";
e.details  = std::format("workunit={} line={}", wuId, lineId);
e.result   = std::string{app::auth::result::kSuccess};
e.username = session.currentUsername();
e.role     = std::string{app::auth::roleName(current->role)};
audit.record(e);   // timestamp filled in by the logger if empty
```

### Querying the audit log

```cpp
app::auth::AuditQuery q;
q.category = "USER";
q.action   = "RESET_PASSWORD";
q.fromTs   = "2026-05-01T00:00:00Z";
q.limit    = 0;   // 0 = no cap (CSV export path)
const auto rows = audit.query(q);
```

---

## Threading model

- **Single connection per SQLite store, guarded by an internal mutex.**
  Concurrent reads serialise behind writes; fine for HMI scale (one
  operator). For high-concurrency deployments, swap the storage impl
  for one that uses a connection pool.
- **Session** is mutex-guarded; reads return snapshots so callers
  don't hold the lock past the call.
- **`AuthService`** runs synchronously on the calling thread (GTK main
  thread in the desktop binary, request thread in a hypothetical
  server). No internal threads.
- **`signalChanged`** on Session emits *outside* the mutex so
  observers can re-enter `currentUser()` without deadlocking.

---

## Security notes

- **Password storage** uses Argon2id (memory-hard, OWASP-recommended).
  Plaintext never persists; the encoded hash is self-describing so a
  future parameter change doesn't break existing rows.
- **Username canonicalisation** -- lower-cased at the presenter
  boundary; the SQLite UNIQUE constraint uses `COLLATE NOCASE` as
  defence-in-depth.
- **User enumeration mitigation** -- `LoginResult::InvalidCredentials`
  merges "wrong user" and "wrong password" so the UI cannot leak the
  distinction.
- **Audit is append-only** -- no `UPDATE` or `DELETE` paths exposed.
  An admin can READ + EXPORT; corrupting the trail requires direct DB
  access.
- **Self-mutation refusal** -- `UsersPresenter::remove` and
  `::update(enabled=false)` refuse to act on the currently signed-in
  user so an admin cannot accidentally lock the binary out of admin.
- **Plaintext never in audit details** -- `RESET_PASSWORD` and
  `CHANGE_PASSWORD` rows carry the user id only; tests assert the
  secret value never appears in `details` (defence against
  accidentally turning the audit log into a plaintext password store).
- **Avatar boundary validation** -- `setAvatar` enforces non-empty +
  <= 256 KiB + whitelisted MIME (`image/png`, `image/jpeg`) BEFORE the
  bytes reach SQLite.

---

## Testing

`tests/SqliteUserRepositoryTest.cpp` -- 25 cases covering schema
bootstrap idempotency, the case-insensitive UNIQUE constraint, CRUD
round-trips, avatar BLOB round-trips with MIME + size guards, schema
migration from a v0 (pre-PRAGMA) DB.

`tests/AuthServiceTest.cpp` -- login outcomes (success / wrong
password / wrong user / disabled), seeding idempotency, audit
integration when a logger is wired in.

`tests/SqliteAuditLoggerTest.cpp` -- write + query round-trips,
filter combinations, time-range queries.

`tests/UsersPresenterTest.cpp` -- 40 cases: RBAC matrix
(3 roles x 7 verbs), self-mutation refusals, validation rejects,
password hashing round-trips on reset + change-own, avatar set / clear
flows, **and 12 audit-format tests pinning the canonical row shape
per verb** (category, result, attribution, identifiers in details,
plaintext never present).

`tests/AvatarPlaceholderTest.cpp` -- initials + palette derivation,
including corner cases (non-letter prefixes, ASCII-only extraction
from non-ASCII display names, whitespace-only fallback).

Run isolated:

```bash
cd build/debug
ctest -R '(SqliteUser|Auth|Audit|UsersPresenter|Avatar)' --output-on-failure
```

---

## Out of scope (intentional)

- **Federation / SSO / OAuth / SAML** -- single-tenant HMI; would
  require a different `UserRepository` (one that talks to an IdP)
  but no other interface changes.
- **Rate limiting / lockout** -- `LoginResult::LockedOut` is reserved
  in the enum; the actual policy + persistence sits one layer above.
- **Multi-factor auth** -- same as above; the AuthService surface
  already accepts arbitrary `PasswordHasher` semantics.
- **Audit log retention / rotation** -- the `HistorianMaintenance`
  pattern in `src/historian/` (delete rows older than N days, vacuum
  weekly) is a drop-in template; not wired here because compliance
  policy varies per industry.
- **User self-registration** -- admin-only `create`. Self-service
  signup is an orthogonal flow (email verification, captcha) that
  doesn't belong in the auth core.
