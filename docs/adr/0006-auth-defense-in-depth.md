# 0006. Auth defense-in-depth at presenter + view

## Status

Accepted (2026-03)

## Context

The HMI ships an opt-in auth + RBAC stack: three roles (Operator,
Maintenance, Admin) backed by an `SqliteUserRepository` with
Argon2id password hashing via libsodium. The audit log records
every privilege-sensitive action (login, logout, user create /
disable / role change, password reset).

Where do we enforce "this user can / cannot do X"? Naive options:

- Only at the view: hide the button. But UIs are not security
  boundaries — anyone with the codebase can build a binary that
  unhides everything.
- Only at the presenter / service: never show the button to
  unauthorized users either, but rely on the service to refuse.
  Safer, but a UI that silently shows a button that always errors
  is a worse UX than one that hides it.

## Decision

Defense in depth — check **both** at the view (for UX) and at the
presenter / service (for safety):

- **View layer**: pages / menu items / buttons that need a role gate
  read `Session::currentUser().role` at construction time and either
  hide themselves or disable the action. Example: the `UsersPage`
  tab is not registered with the notebook unless
  `currentUser().role == Admin`; `AuditLogPage` likewise.

- **Presenter layer**: every privileged command in `UsersPresenter`
  (createUser, disableUser, changeRole, resetPassword) checks the
  current session's role via `AuthService` before performing the
  mutation. Unauthorized calls return an error result and emit an
  audit log entry. The view's role gate is treated as defense in
  depth, never as the only check.

- **Service layer**: `AuthService::login` collapses the
  "user-not-found" and "wrong-password" cases into a single
  `InvalidCredentials` result to prevent user-enumeration, regardless
  of what the dialog shows.

## Alternatives

- **View-only enforcement** — rejected. Trivial to bypass via direct
  presenter calls in a custom binary; a malicious LDAP shadow could
  also expose the gap. Security boundaries must live below the UI.

- **Presenter / service-only enforcement** — rejected for UX
  reasons. Showing every operator the "Manage Users" tab and then
  popping "Access denied" on click reads as a bug, not a security
  posture.

- **A central authorization middleware** — considered, deferred. With
  three roles and ~12 privileged actions, an explicit per-command
  check is clearer than a generic "permissions table" the reviewer
  has to mentally evaluate. Worth revisiting if the count grows.

## Consequences

+ A code reviewer of `UsersPresenter` can see the role check inline
  with each command — no out-of-line policy table to cross-check.
+ Audit log captures denied attempts the same way it captures
  successes, so a brute-force-by-UI-tampering scenario is visible
  in the log.
+ The view rule is "if you can't do it, you can't see it" — which
  matches how operators expect industrial HMIs to behave.
- Two checks for one rule = two places to keep in sync. Mitigated
  by both reading from the same `Session::currentUser().role` and
  by `tests/auth/` exercising the negative cases (operator trying
  admin command) end-to-end through the presenter.
- The seeded default users (operator/operator, maintenance/maintenance,
  admin/admin) are convenient for demo / first run but obviously not
  production credentials. The seeder is idempotent — a populated
  user table is left alone — but the README documents this and
  recommends rotating the admin password on first deployment.
