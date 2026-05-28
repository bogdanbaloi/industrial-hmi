# ADR-0012: Requirements Traceability — Lightweight, Not DOORS

- **Status:** Superseded by [ADR-0013](0013-openfasttrace-adoption.md)
  (2026-05-27). This ADR is preserved for the historical reasoning;
  the lightweight markdown approach was the right *first* step but
  was upgraded to OpenFastTrace within the same day to give the CI
  gate industry-recognised tooling. The artefacts described here
  (REQUIREMENTS.md, in-source tags) survive in upgraded form; the
  `check-traceability.sh` script was removed.
- **Date:** 2026-05-27
- **Deciders:** Bogdan Baloi (maintainer)
- **Context:** Phase 8 dashboard work is in main. Recording demo
  + recruiter outreach starting. CV claims DOORS + Polarion as
  skills, but the portfolio has no visible traceability artefact.

## Context

Industrial / safety-critical software lives or dies by being able
to answer "show me the requirement, the code that implements it,
and the test that verifies it" -- automotive ISO 26262, medical
IEC 62304, avionics DO-178C all formalise this as a hard process
gate. The same discipline applies even outside regulated
contexts: it prevents code from drifting away from intent and
gives a structured way to discuss scope changes.

This portfolio is targeted at industrial C++ contractor roles
(automotive Tier 2/3, defence, energy). Without traceability the
codebase reads as "a senior C++ dev did a clean job"; *with*
traceability, it reads as "a senior C++ dev with industrial
process discipline did a clean job". The signal is bigger than
the effort.

## Decision

Adopt a **lightweight, in-tree, markdown-based traceability
approach** rather than:

- A real requirements-management tool (DOORS, Polarion, Jama).
  Heavy licensing, structured-data format unreadable in a GitHub
  browser, and a full DOORS round-trip is far beyond what a
  portfolio project warrants.
- An empty `// TODO: trace this requirement` placeholder. Worse
  than nothing -- looks like cargo-cult.
- A spreadsheet checked into the repo. Diff-hostile, harder to
  review than markdown.

The chosen approach has three artefacts and one tooling hook:

### 1. `docs/requirements/REQUIREMENTS.md`

Single source of truth for **what the system must do**. Lists
all REQ-IDs with priority (MUST/SHOULD/NICE), behaviour
description (EARS-flavoured), verification reference, and
optional ADR pointer. Categorised by domain prefix
(REQ-ARCH, REQ-AUTH, REQ-DASHBOARD, etc.).

### 2. `docs/requirements/TRACEABILITY.md`

Bi-directional matrix. Each row maps one REQ-ID to:
- the source file(s) that implement it (primary entry point)
- the test target(s) that verify it
- the ADR explaining the trade-off, if any

Coverage summary at the bottom counts MUST / SHOULD / NICE
verification status across all categories.

### 3. `// Implements: REQ-XXX` comments

Tests that verify a requirement carry a comment near the top of
the file or fixture naming the REQ-IDs they cover. Lets a reader
trace backwards from a green test to the requirement.

Example:
```cpp
// Implements: REQ-MULTISTATION-003 (equipment supply forward),
//             REQ-MULTISTATION-004 (quality pass-rate forward),
//             REQ-MULTISTATION-005 (bridge metrics).
TEST(PrimaryToSecondaryBridgeTest, ForwardsEquipmentSupplyLevel) {
    ...
}
```

### 4. `scripts/check-traceability.sh` (optional CI hook)

Greps source for `// Implements: REQ-XXX` comments and cross-
references them against REQUIREMENTS.md. Fails the build if a
REQ-ID referenced in a test is not declared in REQUIREMENTS.md,
or if a MUST/SHOULD REQ-ID in REQUIREMENTS.md has no implementing
test. (Initial pass ships without enforcement; switch to
warnings-as-errors once the existing test suite is fully
annotated.)

## Consequences

### Positive

- **Signal to industrial recruiters.** A defence / automotive
  recruiter scrolling the repo sees `docs/requirements/` and
  immediately classifies the developer as process-aware. CV
  bullet ("Polarion + DOORS") is now backed by visible practice.
- **Discipline forcing function.** Adding a feature now starts
  with "add the REQ-ID, then the test, then the code". This is
  how industrial teams already work; doing it solo here builds
  the habit.
- **Demo recording win.** The Malt demo can pause on the
  REQUIREMENTS.md / TRACEABILITY.md tab and walk
  "requirement -> code -> test" for one feature. Few open-source
  C++ portfolios show this.
- **No new tooling.** Markdown + a 50-line shell script. Renders
  natively on GitHub.

### Negative

- **Manual upkeep.** Adding a feature without updating the
  matrix is easy. Mitigated by the CI check (eventually) +
  PR description template (eventually).
- **Not certifiable.** This wouldn't satisfy an actual
  ISO 26262 / DO-178C audit -- the trace links aren't
  cryptographically signed, requirements aren't immutable, no
  approval workflow. That's fine: this is portfolio signal, not
  a regulatory submission.
- **Backfill cost.** ~50 existing requirements need to be
  authored once, plus existing tests get annotated. ~6h one-off,
  then ~5 min per new feature.

## Alternatives considered

### A. Real DOORS / Polarion instance

Rejected. Cost, licensing, and the entire portfolio audience
would need a DOORS reader to inspect. Defeats the "GitHub-first"
visibility goal.

### B. Inline `@requirement` Doxygen tag

Doxygen has `\req{}` / `@req` extensions used in some automotive
toolchains. Considered, but:
- Requires running Doxygen to extract the matrix (loses GitHub-
  native browseability).
- Couples requirements documentation to API doc generation.
- Doxygen output is a clunky HTML page that no recruiter will
  click into.

### C. RST + Sphinx + sphinx-needs

Considered. `sphinx-needs` is a real requirements-traceability
plugin used in industrial Sphinx-based docs sites. Rejected
because:
- The project doesn't already use Sphinx for docs (currently
  README + ADRs + per-module READMEs). Introducing Sphinx just
  for requirements is overkill.
- GitHub renders RST less faithfully than markdown.

### D. Annotate code with `// REQ-XXX` (not tests)

Considered. Decided against because:
- A REQ-ID in the source becomes stale when the file is refactored.
- Tests are the authoritative "this works" gate, so anchoring the
  trace at the test is more durable.
- A reader chasing "what does REQ-DASHBOARD-003 actually do"
  benefits more from "open BigNumberCardTest.cpp + see the
  scenarios" than from "open DashboardPage.cpp and find the line".

## References

- [EARS notation for requirements](https://alistairmavin.com/ears/)
- [ISO 26262 traceability overview](https://www.iso.org/standard/68383.html)
  (concept reference, not a compliance source)
- `docs/requirements/REQUIREMENTS.md` -- the catalogue
- `docs/requirements/TRACEABILITY.md` -- the matrix
- `scripts/check-traceability.sh` -- the CI hook (to be added)
