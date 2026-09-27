# `src/mcp/` -- Model Context Protocol Server

An LLM agent asks the **running** system about alarms, production and history,
over stdio, in JSON-RPC. It is not handed an export, a copy or a summary: every
answer comes from the same presenters the GTK dashboard is driven by.

Built only when `-DBUILD_MCP_SERVER=ON`, which is OFF by default. The binary is
`industrial-hmi-mcp`.

## Why this module exists separately

It is the fourth consumer of the same Model plus Presenter pair (ADR-0023),
after the GTK view, the Qt view and the console. That is the point of it.

A view layer that can be swapped is a claim. Three front-ends plus a machine
consumer that never draws anything is evidence, because an agent has no
widgets, no event loop and no human in the loop, so anything view-shaped left
in the presenters would have shown up here as friction.

## The write path is the interesting half

Three of the four tools read. The fourth, `equipment_command`, starts, stops or
resets equipment, which is a state change requested by a language model.

ADR-0024 decides how that is allowed to work, and the shape matters more than
the feature:

- **`mcp.write_enabled` defaults to `false`** (`ConfigManager.cpp`). A
  read-only deployment is the default, not a configuration somebody remembers
  to apply.
- **When writes are off the tool is neither advertised nor callable.**
  `tools/list` does not name it, and `tools/call` answers `MethodNotFound`, so
  it is indistinguishable from a tool that was never written. A deployment that
  must not write is *provably* unable to, rather than merely configured not to.
- **When writes are on, the agent carries its own identity.** The command runs
  behind an explicit authorization pre-check against the agent session, before
  the presenter is touched. A session with no authenticated agent is refused
  with `Unauthorized`, which closes the null-session pass-through that the
  presenter's own role gate allows for the auth-disabled case.
- **A refused command is audited as a failure**, through the same human audit
  path, then reported as a structured JSON-RPC error. It is never a silent
  no-op reported as success.

That last point is the one worth reading twice. The presenter's role gate
refuses a permitted-but-unauthorised action with a silent `void` return, which
is fine for a button that simply does not respond. A write tool must not
inherit it, because an agent that receives success for an action that did not
happen will build on the lie.

## Architecture

| File | Responsibility |
| --- | --- |
| `McpServer.{h,cpp}` | The loop. Reads newline-delimited JSON-RPC from an input stream, dispatches, writes one response line per request. Single-threaded and synchronous: one request is fully handled before the next is read. |
| `McpProtocol.{h,cpp}` | The MCP surface: the `initialize` handshake, `tools/list`, and `tools/call` dispatch on `params["name"]`. |
| `McpError.h` | The error codes as values. Errors are returned at the boundary rather than thrown (ADR-0014). |
| `McpInitRoot.{h,cpp}` | The composition root. Builds the model, the presenters, the historian reader and the agent session, then hands them to the server. |
| `tools/` | One file pair per tool. |

The stream parameters on the server are a test seam: the loop runs against
string streams in tests rather than real stdio, so the protocol is exercised
without a process.

## The four tools

| Tool | Reads or writes | What it answers |
| --- | --- | --- |
| `alarms_snapshot` | read | The live alarm list with its ISA-18.2 state, priority and acknowledgement status |
| `production_metrics` | read | OEE, throughput, quality rate, equipment state |
| `historian_query` | read | A time range out of the local history store |
| `equipment_command` | **write** | `start`, `stop` or `reset` on one equipment station. Off by default, see above |

## Threading

None. The server is deliberately single-threaded and synchronous. The model
behind it runs its own Boost.Asio thread, which the presenters already guard,
so the server adds no concurrency of its own and needs no lock.

## Testing

The protocol handlers are tested against string streams, so a test drives a
whole session without spawning a process. The write path carries its own cases
for the three outcomes that matter: writes disabled, an unauthenticated
session, and a role without the permission.

## Reading order

1. `docs/adr/0023-mcp-server-fourth-mvp-consumer.md`, why an agent consumer at all
2. `docs/adr/0024-mcp-agent-identity-for-writes.md`, why the write path looks like this
3. `McpProtocol.cpp`, the dispatch, which is where both decisions become code
