# `src/app/` -- Composition Root

The one place the layers are wired together, and the only code allowed to know
about all of them.

Two files. It is small on purpose: everything it does is construction and
ownership, never logic.

## Why a module for two files

Because the alternative is worse. Without a composition root, each front-end
builds its own backends, each backend decides its own lifetime, and the answer
to "who owns this" is spread across three `main` functions.

`IntegrationBootstrap` owns the `IntegrationManager` plus every side object a
backend needs kept alive for the process lifetime:

- the outbound and inbound telemetry bridges
- the OPC-UA command sink, which the node map holds a reference to
- the multi-station mirror model and its `PrimaryToSecondaryBridge`

Backends that own all of their own pieces, TCP and Modbus among them,
contribute no members here at all. That asymmetry is the module's actual
content: it holds exactly what cannot hold itself, and nothing else.

## Ownership order is a decision, not an accident

The members are declared in the order that makes destruction correct. A
backend handed a reference to a bridge must not outlive that bridge, so the
bridge is declared first and destroyed last. Reordering the declarations is a
behavioural change even though it reads as cosmetic, which is why the file says
so next to them.

## What it deliberately does NOT do

- No protocol logic. Every backend implements `IntegrationBackend` in
  `src/integration/`, and this module only decides which of them exist.
- No view knowledge. It is linked by the GTK binary, the Qt binary, the console
  binary and the MCP server alike.
- No configuration policy. It reads decisions out of `ConfigManager` rather
  than making them, so which backends start is a config question rather than a
  code question.

## Compile-time gating

Several backends are behind CMake options that are OFF by default:
`BUILD_OPCUA_BACKEND`, `BUILD_SERIAL_BACKEND`, `BUILD_HTTP_BACKEND`. The
registration for each sits behind the matching `INDUSTRIAL_HMI_HAS_*` macro, so
a build without an option compiles this file with that backend absent rather
than present and disabled.

That means the default build genuinely cannot speak those protocols, which is
the same argument the MCP write flag makes in `src/mcp/`: absent is stronger
than switched off.

## Where to look first

1. `IntegrationBootstrap.h`, the ownership comment above the members
2. `src/integration/IntegrationBackend.h`, the narrow interface this wires
3. `src/core/Bootstrap.{h,cpp}`, the staged startup this plugs into
