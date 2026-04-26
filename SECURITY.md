# Security Policy

This document describes how to report security vulnerabilities in
`industrial-hmi` and what to expect once you do. The project is
maintained by a single author (see `LICENSE`) and ships with no
warranty -- but security reports are taken seriously and coordinated
through the channels below rather than ignored.

## Supported versions

Security fixes are provided for the latest minor release line on
`main`. Older lines are not patched.

| Version | Supported          |
| ------- | ------------------ |
| 1.1.x   | yes (current)      |
| 1.0.x   | no -- upgrade to 1.1 |
| 0.x     | no                 |

`main` itself is the development branch. Do not run it in production;
use a tagged release.

## Reporting a vulnerability

**Do not open a public GitHub issue for security bugs.** Public issues
trigger CI, get indexed by search engines, and create a window where
the bug is exploitable but unpatched.

Use one of:

1. **Preferred -- GitHub Security Advisory.** From the repo page click
   `Security` -> `Advisories` -> `New draft security advisory`. This
   creates a private discussion thread that only the maintainer and
   invited collaborators can see.
   <https://github.com/bogdanbaloi/industrial-hmi/security/advisories/new>

2. **Email** -- if you cannot use GitHub Advisories, contact the
   maintainer through the email listed on
   <https://github.com/bogdanbaloi>. Encrypt with the public key
   advertised on the same profile if the report contains exploit
   details.

Please include, at minimum:

- The affected version (`industrial-hmi --version`, or git SHA if
  built from source).
- Reproduction steps. A failing test case or ASan/UBSan stack trace
  is ideal -- the project already runs both sanitizers in CI, so
  attaching the diagnostic that flagged the bug usually pinpoints
  the fault line.
- Expected vs. observed behaviour, and your assessment of impact
  (information disclosure / denial of service / RCE / privilege
  escalation / data corruption).

## Response timeline

The maintainer is a solo developer; the timeline below is best-effort,
not contractual:

| Stage              | Target          |
| ------------------ | --------------- |
| Acknowledge report | within 72 hours |
| Confirm or refute  | within 7 days   |
| Patch in `main`    | within 14 days for high/critical impact |
| Coordinated public disclosure | after a release containing the fix is published, plus 7 days to let downstream users update |

If a report falls outside scope (see below), you'll get a polite
explanation rather than a fix.

## In scope

The following constitute security vulnerabilities for this project:

- Memory-safety bugs in C++ code: use-after-free, heap/stack buffer
  overflow, double-free, uninitialized memory read, type confusion.
  These are caught by ASan in CI, but we accept reports of cases CI
  missed.
- Undefined behaviour with security implications: signed integer
  overflow leading to OOB access, null pointer dereference reachable
  from external input, misaligned access on architectures that fault.
- SQL injection or path traversal in `DatabaseManager` /
  `CsvSerializer` / config loading.
- Crashes triggered by malformed `config/app-config.json`,
  malformed `.po` translation catalogs, or malformed CSV imports
  -- a robust HMI binary should refuse bad input, not crash on it.
- Race conditions between the GTK main thread and the Boost.Asio
  I/O thread that could corrupt model state or crash the binary.
- Logic flaws that let a non-privileged operator perform an action
  the UI was supposed to gate (currently this surface is small;
  RBAC is not implemented).

## Out of scope

The following are **not** security issues for this project. Open a
regular bug or feature request instead:

- Cosmetic UI bugs, theme issues, or layout glitches.
- Performance issues that don't enable a denial of service.
- Crashes triggered only by intentionally hostile builds (custom
  CMake flags, manually edited config schema, instrumented runtime).
- Vulnerabilities in upstream dependencies (gtkmm, glibmm, sqlite,
  Boost) -- report those upstream. We track upstream advisories and
  pin to fixed versions when relevant.
- Findings from automated tools without a working exploit. A
  cppcheck warning or a Snyk advisory is a starting point, not a
  vulnerability report.
- Anything that requires physical access to the operator terminal
  -- the threat model assumes the device is in a controlled
  industrial environment and the OS is trusted.

## What to expect

- **Acknowledgement** -- you will get a reply confirming receipt
  within the timeline above. If you don't, assume the report didn't
  arrive and resend through the alternative channel.
- **Updates** -- expect at least one update per week while the
  report is open. If a fix is going to take longer, you'll be told
  why.
- **Credit** -- valid reports are credited in the `CHANGELOG.md`
  entry that ships the fix and in the GitHub Security Advisory,
  unless you prefer to remain anonymous.
- **No bug bounty** -- this is a portfolio project; there is no
  monetary reward. Reports are still appreciated.

## Project security posture

For context, the existing CI gates that prevent classes of bugs from
landing in `main`:

- AddressSanitizer + UndefinedBehaviorSanitizer on every PR
  (use-after-free, buffer overflow, signed overflow, null deref,
  misaligned access, OOB shift).
- clang-tidy strict (`WarningsAsErrors: '*'`) covering bugprone-*,
  cert-*, clang-analyzer-*, concurrency-*, cppcoreguidelines-*.
- cppcheck warning + style + performance categories.
- Branch protection on `main` requires all of the above to pass
  before merge.

These reduce -- but do not eliminate -- the chance of a vulnerability
shipping. Reports remain welcome.
