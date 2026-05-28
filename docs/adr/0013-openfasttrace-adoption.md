# ADR-0013: Adopt OpenFastTrace for Requirements Traceability

- **Status:** Accepted
- **Date:** 2026-05-27
- **Deciders:** Bogdan Baloi (maintainer)
- **Supersedes:** [ADR-0012](0012-requirements-traceability.md)
  (lightweight markdown matrix + custom shell script)

## Context

ADR-0012 introduced a homemade traceability discipline:
`REQUIREMENTS.md` + `TRACEABILITY.md` + `// Implements: REQ-XXX`
comments + a 100-line bash script that cross-checked them. That was
the right first step and proved the discipline was sustainable.

The next gap was tooling credibility. The portfolio is targeted at
**industrial Functional Owner / Software Architect roles** in
automotive (Continental, Bosch, Harman) and industrial automation
(Siemens, ABB, Schneider). The audience for those CVs sees
traceability tools weekly: Polarion, DOORS, Codebeamer, Jama,
sphinx-needs (used by Bosch with >1000 engineers), OpenFastTrace
(used in projects targeting ISO 26262 / IEC 61508 / DO-178C
assessments).

A custom shell script in `scripts/` reads as "this developer
understands the principle but reached for ad-hoc tooling instead
of the industry standard". An off-the-shelf tracer with a CI gate
reads as "this developer has used real lifecycle tooling and knows
which one to reach for".

## Decision

Adopt **OpenFastTrace (OFT)** as the traceability validator,
replacing the custom shell script.

### Why OFT over the alternatives

| Tool | Verdict |
|---|---|
| **OpenFastTrace** | Chosen. Native support for C/C++ source tag comments (`// [impl->req~xxx~1]`, `// [utest->req~xxx~1]`), Markdown specobject format that preserves human-readable REQUIREMENTS.md, Java-based CLI + GitHub Actions integration, HTML report artefact. Used in safety-critical pipelines targeting DO-178C / ISO 26262. |
| sphinx-needs | Strong candidate. Bosch uses it in production at scale and useblocks publishes a Sphinx Classification document covering ISO 26262 + IEC 61508 + EN 50716. Rejected for this project because adopting it means moving all docs to RST + Sphinx, which is a much larger churn than the value adds for a single-developer portfolio. May be revisited if the portfolio grows to multi-document Sphinx output. |
| StrictDoc | Modern, ReqIF-bidirectional, with a DO-178C technical note. Rejected because its SDoc markup is project-proprietary; pinning to it would lose the GitHub-native markdown browse-ability. |
| Doxygen `@req` | Couples requirements to API doc generation; doesn't render in GitHub's file viewer; clunky HTML output. |
| Keep custom shell script | Works mechanically; loses the "industry-recognised tooling" signal that is half the point. |

### Format conversion

| Before (ADR-0012) | After (this ADR) |
|---|---|
| `### REQ-AUTH-005 (NICE) — title` | Same heading + `` `req~auth-005~1` `` marker below |
| `**Verified by:** AvatarPlaceholderTest.` | `Verified by: AvatarPlaceholderTest.` + `Needs: utest` |
| `// Implements: REQ-AUTH-005 ...` | `// [utest->req~auth-005~1]` + `// Covers REQ-AUTH-005 ...` |
| `scripts/check-traceability.sh` | `oft trace` CI job |

Human-readable headings, ADR pointers, and rationale text are
preserved verbatim. The OFT markers are additive — a reader who
doesn't know OFT still sees a clean requirements document.

### CI integration

A new `traceability` job in `.github/workflows/ci.yml`:
1. Pulls Temurin Java 17
2. Downloads `openfasttrace-4.1.0.jar`
3. Generates an HTML coverage report (uploaded as a PR artefact,
   30-day retention)
4. Runs `oft trace` as a hard gate — fails the PR if any
   requirement with `Needs: utest` lacks a matching coverage tag

The HTML artefact is the demo asset. During a recruiter call the
report can be opened from any green PR with two clicks.

## Consequences

### Positive

- **Industry-recognised tooling on the CI graph.** Recruiter
  scanning the Actions tab sees "Requirements Traceability
  (OpenFastTrace)" alongside Sanitizers, Coverage, and clang-tidy.
  No explanation needed for what it does.
- **Same discipline, less custom code.** ~100 lines of bash gone;
  one CI job and one config-by-convention added.
- **Honest claim on CV.** "Requirements traceability via
  OpenFastTrace, ASPICE-style coverage gate on CI" is a true
  sentence backed by visible practice.
- **Migration path to heavier tools.** OFT can export to ReqIF,
  which is the lingua franca for OEM exchange in automotive. If a
  future client uses Polarion or DOORS, this catalogue can be
  exported rather than re-authored.

### Negative

- **Java in CI.** Adds a Java 17 step to the traceability job
  (~15s on a warm runner). Acceptable.
- **Tag noise in source.** `// [utest->req~auth-005~1]` is
  uglier than `// Implements: REQ-AUTH-005`. Mitigated by keeping
  the human-readable second line: `// Covers REQ-AUTH-005 ...`
- **Behind-the-times manual fallback.** If OFT releases a breaking
  change the CI breaks. Pinned version number (`4.1.0`) limits
  blast radius.

## Alternatives considered

See the comparison table above. The two close calls were
**sphinx-needs** and **StrictDoc**. Both are valid; OFT won on
the migration-cost axis (zero documentation churn vs. full RST
conversion or SDoc adoption).

## References

- [OpenFastTrace project](https://github.com/itsallcode/openfasttrace)
- [OpenFastTrace user guide — coverage tags in source code](https://github.com/itsallcode/openfasttrace/blob/main/doc/user_guide.md)
- [Sphinx Classification (ISO 26262, IEC 61508, EN 50716)](https://safety.useblocks.com/)
  — for sphinx-needs context
- [StrictDoc DO-178C technical note](https://strictdoc.readthedocs.io/)
  — for StrictDoc context
- [ADR-0012](0012-requirements-traceability.md) — predecessor
- `docs/requirements/REQUIREMENTS.md` — the catalogue, now in OFT format
- `.github/workflows/ci.yml` — the `traceability` job
