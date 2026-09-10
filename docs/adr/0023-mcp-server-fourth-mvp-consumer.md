# 0023. MCP server as a fourth, LLM-facing consumer of the MVP core

## Status

Accepted (2026-09). Extends ADR-0020 (a third frontend proving the MVP boundary
is toolkit-independent) to a fourth consumer whose "view" is not a GUI toolkit
at all but an LLM agent. Builds on ADR-0001 (MVP layer boundaries), ADR-0003
(Observer via ViewObserver) and ADR-0014 (Result at boundaries).

## Context

ADR-0020 showed the presenter/model core is independent of any one GUI toolkit
by rendering the same presenters through GTK, Qt and a console view. All three
consumers are still humans driving a screen.

A stronger claim is that the boundary is agnostic to the *kind* of consumer, not
just the toolkit. An LLM agent driving the same read APIs the human frontends
use would demonstrate that the seam holds for a non-human, programmatic
consumer. It is also directly useful: exposing a tool to an LLM agent over the
Model Context Protocol (MCP) is a concrete, in-demand capability, and designing
tool interfaces that behave predictably when an LLM is at the controls is the
same discipline that makes a good interface for a person.

MCP is a JSON-RPC 2.0 protocol. A client (e.g. Claude Desktop) launches a server
as a subprocess and talks to it over stdio: `initialize`, then `tools/list` to
discover the callable tools, then `tools/call` to invoke one.

## Decision

Add an opt-in executable `industrial-hmi-mcp`, gated by `BUILD_MCP_SERVER`
(default OFF), built as a sibling of `industrial-hmi-console`: it links the
shared model, historian and presenter object libraries and adds only a new
`src/mcp` layer plus its own composition root. There is no View toolkit: like
the console binary it links no GUI widget library (GTK or Qt). It does use
sigc++ -- the signal library `AlertCenter`'s change signal is built on -- which
comes header-only through `objectsModel`, exactly as the console binary gets it.

- **Two read-only tools in v1.** `alarms_snapshot` wraps
  `presenter::AlertCenter::snapshot()`; `historian_query` wraps
  `historian::HistoryReader::query()`. Both are existing, thread-safe,
  pull-style `[[nodiscard]]` APIs the human frontends already call, so the tool
  handlers reuse the seam rather than forking any logic.
- **No state-changing tool in this version.** A write tool (start/stop a line,
  load a recipe) would let a non-deterministic agent change plant state, so it
  is deferred until there is an authorization story: such a tool belongs behind
  `auth::Session`, role-gated and audited, because an agent must not hold more
  authority than the role it represents, and because prompt-injection risk
  concentrates in the ability to *act*, not to *read*.
- **Hand-rolled JSON-RPC over stdio, in C++.** For the three MCP methods this
  surface needs, the wire format is a handful of JSON-RPC messages, and
  `nlohmann::json` is already a repo dependency (ADR-0015). An official
  Python/TypeScript SDK would pull a second language and build system in and
  relegate the C++ core to a subprocess behind it. Keeping the shim in C++ makes
  the whole path one toolchain and keeps the core the source of truth.
- **Errors are values at the boundary.** Tool handlers return
  `Result<nlohmann::json, McpErrorCode>` (ADR-0014), not exceptions thrown across
  the process boundary. The server maps an `McpErrorCode` to the spec-mandated
  JSON-RPC error object (`-32700` parse error, `-32601` method not found,
  `-32602` invalid params).

## Consequences

- The toolkit-independence proof now spans human GUI toolkits (GTK, Qt), a
  headless human view (console) and a programmatic agent (MCP) over one
  unchanged core. The independence is verifiable at build time: the MCP target
  links no GUI widget library, so a presenter that depended on one would not
  link.
- The read-only boundary is a deliberate, documented safety posture, not a
  limitation to apologise for. Extending to a write tool is a future REQ with its
  own authorization and audit design.
- Hand-rolling the protocol means MCP capability-negotiation edge cases are
  verified against the spec by hand, not guaranteed by an SDK. The tiny surface
  (three methods) keeps that tractable.

## Alternatives rejected

- **A thin Python/TypeScript MCP server shelling out to a C++ query CLI.** Faster
  to build and spec-compliant for free, but it adds a second build system and
  makes the C++ core a subprocess behind another language, which is a weaker
  demonstration for a C++ codebase and adds a process boundary the rest of the
  architecture does not have.
- **A write tool in v1.** Rejected for the safety reasons above; an agent should
  not change plant state before the read path is proven and an authorization
  story exists.
- **A `dashboard_kpis` tool in v1.** `DashboardPresenter` has no pull/snapshot
  API today, only push callbacks via `ViewObserver`; adding a push-to-pull
  adapter is real new surface, not reuse, so it is deferred to its own REQ.
