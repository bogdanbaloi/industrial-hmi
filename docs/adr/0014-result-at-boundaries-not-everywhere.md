# 0014. `Result<T, E>` at boundaries, not everywhere

## Status

Accepted (2026-05). Supersedes the speculative `docs/INTEGRATION_ROADMAP.md`
(deleted) which proposed pushing `Result<T, E>` through every
DatabaseManager method plus a retry / circuit-breaker / metrics stack.

## Context

`src/core/Result.h` provides a Rust-style `Result<T, E>` with monadic
operations + typed error categories (`DatabaseError`, `ValidationError`,
`IOError`, `UIError`). It was introduced for the startup path:
`Bootstrap` and `DatabaseManager::initialize()` return typed errors so
`main()` can route them through the native reporter (ADR 0010), and
exit codes carry the failure category.

The original integration roadmap proposed extending this everywhere:

1. Convert every DatabaseManager CRUD method to
   `Result<T, DatabaseError>`.
2. Convert every presenter callback to consume `Result`.
3. Add `retryWithBackoff(...)` around DB ops.
4. Add a `CircuitBreaker` that opens after N failures and rejects
   further calls until a cooldown elapses.
5. Add metrics (query count, error rate, slow-query count).

Implementing 1 mechanically requires touching every CRUD entry point
plus every presenter + async callback consumer. Implementing 3 + 4
requires every consumer to handle `CircuitOpen` + `Timeout` cases.

The question this ADR answers: **does that effort buy us anything for
THIS application?**

## Decision

Keep `Result<T, E>` at the **startup + lifecycle boundary** only. For
the in-process CRUD path keep the existing `std::function<void(bool)>`
async callback. Reject the retry / circuit-breaker / metrics stack
outright.

Concretely:

| Surface | Returns | Why |
|---|---|---|
| `Bootstrap::run()` | `Result<void, StartupError>` | typed failure -> native reporter + exit code |
| `DatabaseManager::initialize()` | `Result<void, DatabaseError>` | fail-fast on schema/migration; routes through Bootstrap |
| `DatabaseManager::addProductAsync(..., cb)` | callback `void(bool)` | succeeded yes/no is all the UI does with it |
| `DatabaseManager::upsertRecipe(recipe)` | `bool` | one small transaction; presenter shows error toast on false |
| Presenter async ops | `void(bool)` callbacks | same |

Failure-mode rationale for rejecting (3) and (4):

- The HMI runs against an **in-memory SQLite** (`":memory:"`) by default;
  the file-based path is opt-in. The failure modes retry/circuit-breaker
  patterns mitigate (network timeouts, disk full, connection pool
  exhaustion) do not apply -- `SQLITE_BUSY` cannot occur on a single
  connection, `SQLITE_FULL` cannot occur in `:memory:`, and we have no
  remote DB to circuit-break against.
- Validation errors (duplicate product code, missing recipe, FK
  violation) are user-input failures, not transient. Retrying them
  just delays the same error.
- The application's failure narrative is already explicit: fail-fast
  at startup (ADR 0010) + degraded-open per opt-in feature
  (ADR 0006 auth, ADR 0007 historian, ADR 0009 ML, ADR 0005 integration
  backends). That covers every cross-feature failure boundary the HMI
  has. Layering retry/circuit-breaker on top adds machinery for cases
  that don't exist.
- Metrics + health endpoints are useful for a server; an operator-
  facing terminal already surfaces health via the I/O panel pills, the
  alerts log, and the audit log.

## Alternatives considered

- **Adopt the full roadmap (Result everywhere + retry + circuit-breaker
  + metrics).** Rejected. Heavy machinery with no failure modes to
  mitigate in a single-process in-memory-default HMI. The code would
  read as cargo-culted server-side patterns dropped into a control
  panel.

- **Convert just CRUD to `Result<T, DatabaseError>` (skip retry /
  circuit-breaker / metrics).** Rejected as net-neutral: bool already
  conveys success/failure, and `dialogManager_.showError(...)` already
  routes the user-facing message. Adding `Result` adds noise (every call
  site grows a switch on the error enum) without changing behaviour.

- **Convert just the synchronous read path (e.g. `getRecipeByProductCode`)
  to `Result`.** Rejected. The existing `std::optional<Recipe>` return
  already encodes the "not found" case the presenter actually handles.
  A `Result<optional<Recipe>, DatabaseError>` is over-modelled.

## Consequences

+ The error-handling surface has exactly two shapes -- typed `Result`
  at lifecycle boundaries, `bool` at the in-process CRUD seam. A new
  contributor only needs to learn the boundary rule, not five layers
  of patterns.
+ Tests stay light: `EXPECT_TRUE(result.isOk())` at startup, plain
  bool assertions on CRUD.
+ The decision is documented (this ADR) so a reviewer asking "why no
  retry?" or "why not Result everywhere?" has a written answer.
- We don't get the *visual* of Rust-style Result threaded through every
  method. A reviewer who counts that as a maturity signal will
  under-rate the codebase. The mitigation is this ADR plus the
  `Result.h` machinery itself, which IS exercised at the boundary
  (`StartupError` round-trip in `ExceptionHandlerTest`,
  `ResultTest` 22 cases pinning the monad).
- Adopting any of the rejected items later means revisiting every
  call site at that time. The pattern is small enough that the cost
  is bounded; the ADR captures the trigger ("if we ever move to a
  remote DB or run multiple writers, reopen Phase 3-5").
