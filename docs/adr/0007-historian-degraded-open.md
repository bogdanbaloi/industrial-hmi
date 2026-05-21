# 0007. Historian degraded-open on store failure

## Status

Accepted (2026-02)

## Context

The historian persists time-series samples (equipment levels,
quality check rates, system state transitions) to a local SQLite
file for the History page's trend viewer. It's an opt-in feature
gated by `historian.enabled` in config.

Failure modes on the path to a working historian:

- The configured `historian.db_path` parent directory doesn't exist.
- The SQLite file is locked by another process or is corrupt.
- The migration runner fails to apply schema changes.
- The disk is full or the volume is read-only.

If any of these aborts the application, the operator loses every
working feature of the HMI — equipment monitoring, alerts, e-stop —
because a *historical reporting* feature couldn't open its file.
That's the wrong trade-off for a production-floor terminal.

## Decision

Degraded-open: the historian initialization path catches every
recoverable error, logs a warning through the configured logger,
and sets the `historyReader` pointer to null. `MainWindow` then
checks the pointer at construction time and **silently skips
registering the History tab** if it's null. The rest of the app
proceeds unchanged.

The same pattern is applied throughout startup: auth (ADR 0006),
audit log, ML plugin (ADR 0009), and every individual integration
backend (ADR 0005). One opt-in feature failing never cascades into
a broken HMI.

A user looking at the operator log sees:

```
[warn] Historian disabled: failed to open 'data/historian.sqlite'
       (sqlite error 14: unable to open database file)
[info] History page not registered (no historian reader)
```

## Alternatives

- **Fail-stop on historian error** — rejected. The operator can't
  start production because the trend viewer can't open its database.
  Production-floor priorities don't accept that trade-off.

- **Retry in the background with a "history coming up" placeholder
  tab** — rejected. Adds complexity for a marginal UX gain; a
  historian that fails at startup is almost always going to keep
  failing without operator intervention (disk full, permissions).

- **Lazy open on first History tab click** — rejected. The tab would
  appear and then error mid-interaction. A tab that's "there
  sometimes" is worse than one that's clearly absent.

## Consequences

+ The HMI's critical path (equipment + alerts + e-stop) has zero
  cross-feature dependencies. A failure in any opt-in feature
  fails closed locally, never globally.
+ Operator + reviewer can see in the log exactly which features are
  disabled and why — diagnostic latency stays low.
+ The pattern is consistent across the codebase, so the failure
  semantics are easy to reason about.
- The tabs the operator sees depend on which opt-in features are
  active at this run — a slight UX inconsistency if a historian
  failure recovers later. Worth a status hint in Settings showing
  "historian: enabled / disabled / failed to open" so the operator
  knows why a tab is missing without reading the log.
- A reviewer must trust that every degradation path was explicitly
  designed and not just "swallowed an error". The audit lives in
  one place per feature (the registration site in `main.cpp`); a
  follow-up to centralize feature-status reporting would help.
