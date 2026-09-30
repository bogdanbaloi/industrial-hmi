# ADR-0036: Authorisation does not depend on the audit sink opening

## Status
Accepted (2026-09-30).

## Context
`DashboardPresenter` role-gates every operator action through one `checkRole()`
helper, which reads the role from a `auth::Session*` member. That member has
exactly one setter:

```cpp
void setAudit(app::auth::AuditLogger& audit, app::auth::Session& session) {
    audit_   = &audit;
    session_ = &session;
}
```

One call, two concerns. `checkRole()` returns `true` when the session pointer
is null, on purpose, so a build with authentication switched off behaves as it
did before RBAC landed (ADR-0006).

The MCP composition root wires the write path like this:

```cpp
if (auditLogger_->initialize()) {
    dashboardPresenter_->setAudit(*auditLogger_, *agentSession_);
} else {
    logger.warn("MCP audit log failed to open ...");
    auditLogger_.reset();
}
```

The comment above it states the intent plainly: a failed open "downgrades to
writes without a persisted audit row rather than killing the feature". Losing
the audit ROW was a decision. Losing the role GATE was not, but that is what
happened, because the only setter of the session sits inside the branch.

`EquipmentCommandTool` knows about the null-session pass-through and says so in
its own comment, "a write tool must not lean on the presenter alone". It guards
against its OWN session carrying no user. It could not guard against the
presenter's session, which is a different pointer set from a different place,
and it evaluates the role only AFTER routing the call:

```cpp
(presenter.*mapping.invoke)();
if (!mapping.allowed(agent->role)) { return Unauthorized; }
```

So with `mcp.write_enabled` true, an agent role of OPERATOR and an audit
database that will not open, a `reset` was performed by the presenter, answered
with `Unauthorized`, and recorded nowhere. The action happened, the reply denied
it, nothing audited it. REQ-ARCH-019 already forbids this in as many words, "or
a silent ungated write", so the code was in breach of its own requirement rather
than short of one.

Reported by the `pfa` workstream on 2026-09-30 from a read of the sources in
BSAFE capsule Nr. 4, not from a run. Confirmed here against `main` at `148fab0`
by a test that fails without the fix: `resetSystem()` is called once where it
must never be called.

## Decision
Authorisation and auditing are separated, and the write tool stops trusting
that the separation was wired correctly.

1. `DashboardPresenter::setSession(Session&)` wires the authorisation context
   on its own. `setAudit()` keeps its signature so every existing caller is
   untouched.
2. `DashboardPresenter::hasSession()` lets a caller ask whether any
   authorisation context exists, rather than assume it.
3. `McpInitRoot` calls `setSession(*agentSession_)` unconditionally, before the
   audit sink is opened. Whether the database opens no longer decides whether
   the agent is role-checked.
4. `EquipmentCommandTool` refuses with `Unauthorized` when
   `presenter.hasSession()` is false, BEFORE routing the call. Point 3 means
   this should never fire in the shipped wiring. It is there so that a future
   miswiring is a refused call rather than an ungated write.

Point 3 is the fix. Point 4 is the reason the same defect cannot come back
through a different door.

## Alternatives rejected

**Move the role check ahead of `invoke` in the tool.** The obvious reading of
"pre-check", and it does close the hole. It also deletes a property that
REQ-ARCH-019 requires and `AuthorizedOperatorResetIsUnauthorized` asserts: a
refused write is audited through the presenter's human path, producing exactly
one FAILURE row. Refusing before the presenter runs means no row at all. That
trades an authorisation hole for an audit hole.

**Make `checkRole()` refuse on a null session.** Closes it everywhere at once,
and breaks every build that runs with authentication off, which is a deliberate
affordance rather than an accident. The GTK and Qt frontends rely on it.

**Refuse to enable the write tool when the audit sink fails to open.** Safe, and
it contradicts the documented downgrade. "Writes without a persisted audit row"
was a decision taken with reasons. This ADR does not reopen it.

**Let the tool write its own audit row on refusal.** Forks the audit path that
REQ-ARCH-019 says must not be re-implemented, so the agent trail and the human
trail would drift the first time one of them changed.

## Consequences
A failed audit open now loses exactly what the comment always said it loses: the
row. The agent is still role-checked, an unauthorised write is still refused,
and the refusal is still audited whenever there is a sink to audit into.

`hasSession()` adds one public method to the presenter and one branch to the
write tool. The cost is that the presenter now exposes a fact about its own
wiring, which a caller could in principle use for something else. Accepted,
because the alternative is a security gate that depends on a database opening.

Two regression tests carry this: `UnwiredPresenterStillRefusesAnUnauthorisedWrite`
proves the refusal, and `SessionWiredWithoutAuditStillPerformsAllowedWork`
proves the guard did not turn a downgrade into an outage.

Related: ADR-0006 (RBAC plus audit), ADR-0023 (MCP server), ADR-0024 (agent
identity for writes), REQ-ARCH-019.
