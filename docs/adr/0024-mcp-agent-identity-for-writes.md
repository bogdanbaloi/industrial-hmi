# 0024. Agent identity for MCP write tools

## Status

Accepted (2026-09). Extends ADR-0023 (the MCP server as a fourth, LLM-facing
consumer of the MVP core), which shipped read-only tools and explicitly deferred
any state-changing tool "until there is an authorization story". This ADR is that
story. Builds on ADR-0006 (RBAC + audit), ADR-0001 (MVP layer boundaries) and
ADR-0014 (Result at boundaries).

## Context

ADR-0023 shipped `alarms_snapshot` and `historian_query`, both read-only, and
deferred a write tool because "an agent must not hold more authority than the
role it represents" and "prompt-injection risk concentrates in the ability to
act, not to read".

We now add one state-changing tool, `equipment_command` (start / stop /
reset-restart of the line), routed through the exact operator handlers a human
button calls (`DashboardPresenter::onStartClicked` / `onStopClicked` /
`onResetRestartClicked`). The open question is not the mechanics of the call but
*whose authority the agent acts under*. The presenter already role-gates and
audits every operator action through `auth::Session` + `AuditLogger`
(`setAudit`), so the agent needs an identity that plugs into that same gate.

There is a sharp, easy-to-miss hazard in that reuse. `DashboardPresenter`'s
internal `checkRole` returns **true** when no session is wired
(`session == nullptr`), by design, so auth-disabled dev builds keep their full
button surface. A refused action is a *void early return* (audit FAILURE row +
log line, no thrown error). A write tool that simply called the presenter would
therefore:

- run an **ungated write** if the session were ever null (a wiring bug), because
  `checkRole` passes null through; and
- return a misleading JSON-RPC **success** for a role-refused no-op, because the
  handler is `void` and cannot report that it refused.

## Decision

Introduce a synthetic, in-memory agent identity, enabled only when writes are.

- **One synthetic `auth::Session` seeded with an in-memory `auth::User`.** When
  `mcp.write_enabled=true`, the MCP composition root builds a single
  `auth::User{username = "mcp-agent", role = configured}` and sets it on a
  `Session`. No password, no repository row, no login flow: the agent is not a
  human account and must not be able to authenticate through the normal path.
  The role defaults to `Operator` (`mcp.agent_role`, default `OPERATOR`) so the
  least-privilege posture is start/stop only, **not** reset. An operator raises
  it to `MAINTENANCE` deliberately if the agent is meant to reset the line.
- **The same session + a real `AuditLogger` are wired into the presenter**
  (`setAudit`), so every agent write is role-checked and audited through exactly
  the human path. The audit trail does not distinguish "reused code": it records
  `mcp-agent` with its role, like any operator.
- **The write tool performs its own explicit authorization pre-check.** Before
  routing to the presenter, `runEquipmentCommand` reads
  `session.currentUser()`:
  - no authenticated user -> refuse with `McpErrorCode::Unauthorized` **before
    touching the presenter**, closing the null-session pass-through hole; and
  - present but role not permitted -> route through the presenter (which audits
    the FAILURE via the human path) and then return `Unauthorized`, so the wire
    reply is a structured JSON-RPC error, never a false success.
- **Writes are off by default and provably unreachable when off.** With
  `mcp.write_enabled=false` (the default) the tool is absent from `tools/list`
  and a `tools/call` for it returns `MethodNotFound`, so a read-only deployment
  cannot be talked into a write by a crafted request.

## Consequences

- The agent's authority is a first-class, configurable, least-privilege role,
  not an implicit "the process can do anything" capability. Raising it is an
  explicit config change an operator makes and an auditor can see.
- Every agent write lands in the same audit log as a human action, attributed to
  `mcp-agent`, so a forensic walk needs no special "was this the LLM?" join.
- The presenter is not forked or modified: the tool adds an authorization
  pre-gate around the existing handler rather than a second copy of the logic.
  The pre-gate exists precisely because the presenter's gate is permissive on a
  null session and silent on refusal; the tool makes both explicit at the
  boundary.
- A future second write tool reuses the same synthetic session and the same
  pre-check pattern; the identity model does not change per tool.

## Alternatives rejected

- **Credentials in the tool arguments.** The agent would pass a username +
  password (or token) with each `tools/call`. Rejected: it puts secrets on the
  stdio channel, which has no TLS and is logged by MCP clients, and it invites an
  agent (or a prompt injection) to escalate by supplying a stronger identity. The
  server, not the caller, must fix the identity.
- **A config flag without an identity.** A bare `mcp.write_enabled` that let the
  tool call the presenter with no session. Rejected on its own: it hits exactly
  the `checkRole` null-session pass-through above, i.e. ungated writes. The flag
  is kept, but only *with* the synthetic session, never instead of it.
- **A distinct JSON-RPC error code range for authorization.** JSON-RPC 2.0
  defines no standard "unauthorized" code; the reserved codes are transport-level
  (parse / invalid-request / method-not-found / invalid-params / internal). We
  map `Unauthorized` and `InvalidState` onto the invalid-params code (-32602)
  with a distinct human-readable message, rather than invent an application code
  an MCP client would not understand.
