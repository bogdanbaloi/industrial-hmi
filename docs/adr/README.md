# Architecture Decision Records

Short documents capturing the "why" behind structural decisions in this
project. Each ADR pins one decision: the context that forced it, the
option taken, the alternatives considered, and the consequences both
positive and negative.

Reading these top-to-bottom gives a faster picture of the codebase's
shape than reading the code itself — every non-obvious split, layer
boundary, or third-party dependency choice is justified here.

## Format

[MADR](https://adr.github.io/madr/) — Markdown ADR. One file per
decision, numbered chronologically, never edited after acceptance
(if a decision is reversed, a new ADR supersedes it and references
the old one).

Each file has the following sections:

- **Status** — Accepted / Superseded by NNNN. Always dated; references
  the PR or merge commit where the decision landed.
- **Context** — what forced the decision. The problem, the constraints,
  what would break if no decision was made.
- **Decision** — the choice taken, stated plainly. One paragraph or
  short bullet list.
- **Alternatives** — what else was considered, and why each was
  rejected. This is the section reviewers usually care about most.
- **Consequences** — both `+` (intended benefits) and `-` (costs we
  accepted). Honesty here matters more than completeness.

## Index

| #    | Decision                                       | Status   |
|------|------------------------------------------------|----------|
| 0001 | [MVP layer boundaries](0001-mvp-layer-boundaries.md) | Accepted |
| 0002 | [Two front-ends from one main.cpp](0002-two-frontends-one-main.md) | Accepted |
| 0003 | [ViewObserver instead of sigc::signal](0003-observer-pattern-not-sigc.md) | Accepted |
| 0004 | [Staged Bootstrap orchestrator](0004-staged-bootstrap.md) | Accepted |
| 0005 | [IntegrationBackend interface for all protocols](0005-integration-backend-interface.md) | Accepted |
| 0006 | [Auth defense-in-depth at presenter + view](0006-auth-defense-in-depth.md) | Accepted |
| 0007 | [Historian degraded-open on store failure](0007-historian-degraded-open.md) | Accepted |
| 0008 | [Runtime palette + layout swap](0008-runtime-palette-layout-swap.md) | Accepted |
| 0009 | [ML plugin gated by build-time flag](0009-ml-plugin-conditional-compile.md) | Accepted |
| 0010 | [ConfigManager: policy / mechanism split](0010-config-policy-vs-mechanism.md) | Accepted |
| 0011 | [Multi-station support (Primary/Secondary first instance)](0011-multi-station-support.md) | Accepted |
| 0012 | [Requirements traceability — lightweight, not DOORS](0012-requirements-traceability.md) | Superseded by 0013 |
| 0013 | [Adopt OpenFastTrace for requirements traceability](0013-openfasttrace-adoption.md) | Accepted |

## Adding a new ADR

1. Pick the next free number.
2. Copy an existing ADR as a template. Keep it under one printed page.
3. Open a PR titled `ADR-NNNN: <short title>`. The PR discussion is
   where alternatives get challenged before merge; once merged, the
   ADR file itself is frozen.
4. Update the index above in the same PR.
