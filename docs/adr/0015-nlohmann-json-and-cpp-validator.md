# 0015. nlohmann/json for parsing, C++ validator + JSON Schema as spec

## Status

Accepted (2026-06).

## Context

`ConfigManager` historically shipped with a hand-rolled JSON parser
(line-oriented, regex-free, ~90 LOC in the header) sufficient for the
flat key-value shape we needed but with two known correctness bugs:

1. **Brace-in-string desync.** A `{` inside a string value
   (e.g. `"message_template": "Delete \"{product_name}\"?"`) caused
   the parser to push a bogus path frame, corrupting every key that
   followed in the file. A targeted `front() == '{'` guard fixed the
   symptom, but the underlying tokeniser had no string-state machine
   to make this kind of bug impossible.

2. **Key-order sensitivity.** The path-stack was driven by the
   physical order of `{` and `}` tokens, so running a JSON formatter
   that sorts keys alphabetically (Prettier, JSON::PP) reordered
   `dashboard` after `ui` and broke nested path resolution. An
   anonymous-object `{` placeholder push fixed that case, but again
   the underlying parser was the wrong abstraction.

Beyond syntactic parsing, the config also had no semantic contract.
A typo in `logging.level` ("VERBOSE" instead of "DEBUG") silently
degraded to the default, an out-of-range Modbus port was passed to
Boost.Asio which would fail at connect time with an opaque error,
and an empty language code returned bytes that gettext rejected
silently. None of these would crash startup -- they would all
appear as "the app launched but feature X doesn't work."

Two independent decisions are needed:

- **How do we parse JSON?** Keep hand-rolled, or adopt a library?
- **How do we validate values?** Hand-written C++ checks, runtime
  JSON-Schema validator (e.g. valijson, json-schema-validator), or
  no validation at all?

## Decision

### Parsing: nlohmann/json (v3.11.3) via FetchContent

Vendored at `_deps/json-vendor/` with `SYSTEM` include (same routing
trick used for Boost.SML in ADR-0009 / open62541 in CMake) so
clang-tidy's `src/.*` header filter ignores the ~25k-line single
header. The include is confined to `ConfigManager.cpp` via PIMPL --
the public header carries only declarations + a flat
`std::map<std::string, std::string>` so the rest of the codebase
doesn't pay the compile-time cost.

We pass `ignore_comments=true` to keep tolerating `// ...` banners in
existing config samples, and rely on the parser's RFC 8259 conformance
to make both legacy bugs impossible at the tokeniser level.

The library is MIT-licensed, single-header (no extra build target
beyond the vendor source directory), and is the de-facto standard for
C++ JSON in the ecosystem we hire from.

### Validation: C++ validator + JSON Schema as the audited spec

The runtime enforcement lives in `src/config/ConfigValidator.cpp`. It
hits exactly the same accessors production code uses
(`getInt`, `getFloat`, `isXxxBackendEnabled`), so a passing validation
is the strongest signal we can give that production code will see
well-formed inputs.

The auditable spec lives in `schemas/app-config.schema.json`
(JSON Schema draft-07). The schema is the human- and tool-readable
contract -- IDEs that understand JSON Schema can lint
`config/app-config.json` against it, and reviewers can read the
schema without reading C++.

The two artefacts together form the contract:
- The schema documents the rules in a portable, machine-readable form.
- The .cpp enforces the same rules at startup, fails fast, and
  collects every violation in one pass.

Keeping them in sync is a code-review discipline (and the validator's
own unit tests catch obvious drift).

### Why not a runtime JSON-Schema validator?

We considered pulling in `valijson` or `pboettch/json-schema-validator`
to drive runtime validation directly from the schema file. Rejected
because:

1. Each adds a second heavy dependency to a layer that already pays
   the nlohmann compile cost.
2. The rule set is small (~20 rules). A 50-line C++ validator is
   easier to read and review than the equivalent
   schema-driven failure path.
3. Cross-field invariants ("port required when backend enabled")
   need a custom keyword in JSON Schema anyway, which moves complexity
   from C++ into a custom-validator extension.
4. We get error messages tailored to the operator dialog instead of
   the generic schema-validator output.

## Consequences

### Positive

- The two legacy parser bugs are now impossible (RFC-8259 tokeniser).
- Heavy `json.hpp` include is confined to a single TU; build-time cost
  on the rest of the codebase is zero.
- `ConfigManager.cpp` exists for the first time -- enables future
  PIMPL refactors of related logic without re-arguing the boundary.
- Operators see every config error in one round-trip
  (`ConfigInvalidError` carries the joined list).
- The schema is a reusable artefact: deployment docs, devops linting,
  and onboarding all benefit from a single source of truth.

### Negative

- One extra `FetchContent` dep to keep pinned + audited (offset by
  removing custom parser code that had two known bugs).
- The C++ validator and the schema must be kept in sync by hand; a
  drift only surfaces when both are exercised independently.

### Neutral

- The `ConfigManager` public API is unchanged. Every existing call
  site (~100+ in `src/`) compiles and runs without modification.

## Alternatives considered

| Alternative | Why rejected |
|---|---|
| Keep hand-rolled parser, add patches | Two known bugs already required patches; no string-state tokeniser means future formatter-driven bugs are likely |
| nlohmann + runtime schema validator | Extra heavy dep; cross-field invariants still need custom code; operator-facing messages worse |
| nlohmann, no validation | Symptom (silent feature failure) remains; we'd be paying the dep cost without buying the safety |
| RapidJSON instead of nlohmann | Comparable parser, but nlohmann's API is more idiomatic for the flatten-to-map pattern we needed |

## References

- REQ-CORE-004 (syntactic config from JSON)
- REQ-CORE-005 (semantic validation)
- `src/config/ConfigManager.cpp` (parser swap)
- `src/config/ConfigValidator.cpp` (runtime enforcement)
- `schemas/app-config.schema.json` (auditable spec)
- ADR-0009 (FetchContent + vendor-dir routing pattern)
