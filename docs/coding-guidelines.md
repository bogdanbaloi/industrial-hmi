# Coding guidelines: MISRA-C++-aligned static analysis

This project follows a **MISRA-C++-aligned** coding posture, enforced by a strict
`clang-tidy` profile that runs as a hard-error gate in CI. This document maps the
enforcement to MISRA C++ intent and records the deviations.

## Scope and honest disclaimer

This is **not** certified MISRA compliance. Certified compliance requires a
qualified checker (Helix QAC, LDRA, Polyspace, Coverity) and a safety-critical
project context, neither of which applies to a portfolio codebase.

What this *is*: a documented coding discipline that follows MISRA C++ intent
where free tooling can enforce it, and records the gaps as deviations with a
rationale. MISRA compliance is itself defined as *conformance plus a record of
documented deviations with rationale*, so the structure below mirrors how real
MISRA compliance is argued, on MISRA's own terms rather than as a label.

The claim this supports, verbatim: *"the codebase follows a documented
MISRA-C++-aligned clang-tidy profile with recorded deviations."* See ADR-0028.

## How enforcement works

- **One profile.** `.clang-tidy` at the repo root enables `*` (all checks) minus
  a curated disabled list. Enabled categories include `cppcoreguidelines-*`,
  `hicpp-*`, `cert-*`, `bugprone-*`, `modernize-*`, `performance-*` and
  `readability-*`. CppCoreGuidelines and HIC++ derive from the same defensive-C++
  principles as MISRA, so a large share of MISRA C++ intent is covered by
  construction.
- **Every diagnostic is an error.** `WarningsAsErrors: '*'` means any enabled
  check firing fails the build, so violations cannot accumulate unfixed.
- **Gated in CI.** The `Clang-Tidy` job runs the profile over every `src/*.cpp`
  on every push and pull request, exiting non-zero on the first diagnostic.

## MISRA C++ intent covered (enforced)

Representative mapping of MISRA C++ intent areas to the enabled clang-tidy checks
that enforce them. This is a curated sample of the strongest overlaps, not an
exhaustive rule-by-rule cross-reference.

| MISRA C++ intent | Enforced by (enabled check) |
| --- | --- |
| No C-style casts; use named C++ casts | `cppcoreguidelines-pro-type-cstyle-cast` |
| Do not cast away `const` | `cppcoreguidelines-pro-type-const-cast` |
| Use `nullptr`, not `NULL` or `0` | `modernize-use-nullptr` |
| No `goto` | `cppcoreguidelines-avoid-goto`, `hicpp-avoid-goto` |
| No C-style memory management in C++ | `cppcoreguidelines-no-malloc`, `hicpp-no-malloc` |
| No undefined / unsequenced behaviour | `bugprone-*`, `clang-analyzer-*` |
| No implicit fall-through in `switch` handled paths | `bugprone-*` fall-through diagnostics |
| Deterministic resource handling (RAII) | `cppcoreguidelines-*`, `cert-*` resource checks |
| No use of moved-from / dangling objects | `bugprone-use-after-move`, `bugprone-dangling-handle` |
| Portable, well-defined integer / type use | `portability-*`, `bugprone-*` type checks |

## Recorded deviations

MISRA-relevant checks that are **disabled**, each with the rationale (mirrored
from `.clang-tidy`). These are deliberate, code-shape-driven deviations.

| Disabled check | MISRA C++ intent | Deviation rationale |
| --- | --- | --- |
| `cppcoreguidelines-narrowing-conversions`, `bugprone-narrowing-conversions` | No implicit narrowing conversions | `Gtk::Grid::attach(int, int)` and `size_t` loop indices make this too noisy to be actionable without churn on working UI code |
| `cppcoreguidelines-pro-type-reinterpret-cast` | No `reinterpret_cast` | Required for `sqlite3_column_text`, whose C API returns `const unsigned char*` |
| `cppcoreguidelines-pro-bounds-pointer-arithmetic`, `pro-bounds-array-to-pointer-decay`, `pro-bounds-constant-array-index` | No pointer arithmetic; bounds-safe indexing | Incompatible with the code shape; the project does not use the GSL span types these checks assume |
| `cppcoreguidelines-pro-type-vararg`, `hicpp-vararg` | No C-style variadic functions | Incompatible with the code shape (interop with C library signatures) |
| `cppcoreguidelines-avoid-c-arrays`, `hicpp-avoid-c-arrays`, `modernize-avoid-c-arrays` | Prefer `std::array` over C arrays | Incompatible with the code shape at C-API boundaries |
| `cppcoreguidelines-owning-memory` | Ownership is explicit | The project does not use the GSL `owner<>` annotations this check requires |
| `cppcoreguidelines-macro-usage` | Restrict function-like macros | Not treated as actionable; macros are limited and reviewed by hand |
| `cppcoreguidelines-special-member-functions`, `hicpp-special-member-functions` | Rule of Five completeness | Requires all five special members even when a `= default` destructor is the only one needed |
| `cppcoreguidelines-init-variables`, `pro-type-member-init`, `hicpp-member-init` | No use of uninitialized objects | The code default-initializes to capture a GTK widget return value; flagged as a documented exception |
| `bugprone-exception-escape` | No exceptions escaping where forbidden | `ModelContext` / `Application` destructors are `noexcept(false)` by intent so they can log during teardown |
| `cert-msc32-c`, `cert-msc51-cpp` | Properly seeded, non-predictable RNG | `SimulatedModel` uses a seeded RNG for reproducible demo data by design |
| `concurrency-mt-unsafe` | No thread-unsafe library calls | One-shot `setenv` / `setlocale` during single-threaded init only |

## Deviations to revisit

The `modernize-use-override`, `hicpp-use-override` and
`cppcoreguidelines-explicit-virtual-functions` checks (MISRA C++ intent: virtual
overrides marked `override`) are disabled without a rationale recorded in
`.clang-tidy`. Under the MISRA model a deviation needs a documented rationale, so
these are re-enable candidates: either turn them back on (the codebase already
uses `override` by convention) or record why they stay off.

## References

- `.clang-tidy` -- the enforced profile and the full disabled list
- ADR-0028 -- the decision to adopt a MISRA-aligned posture over certified tooling
- `.github/workflows/ci.yml` -- the `Clang-Tidy` CI gate
