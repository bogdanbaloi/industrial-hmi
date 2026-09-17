# ADR-0028: MISRA-C++-aligned coding guidelines via clang-tidy

## Status
Accepted (2026-09-17).

## Context
MISRA C++ is the coding-guideline standard the automotive and functional-safety
industry expects (ISO 26262 development leans on it). A recurring interview and
job-requirement signal for the embedded/automotive lane is "MISRA-compliant" or
"MISRA-aware" C++.

The project already runs a strict clang-tidy profile (`.clang-tidy`,
`WarningsAsErrors: '*'`, gated in CI) whose enabled categories --
`cppcoreguidelines-*`, `hicpp-*`, `cert-*`, `bugprone-*` -- overlap heavily with
MISRA C++ intent, because CppCoreGuidelines and the HIC++ standard derive from
the same defensive-C++ principles as MISRA. The profile also already carries
documented, justified exceptions for the checks that do not fit this codebase.

What was missing was not the enforcement, but the mapping: nothing stated which
MISRA C++ intent each enabled check covers, and nothing recorded the disabled
MISRA-relevant checks as deviations with a rationale.

## Decision
Adopt an explicit MISRA-C++-aligned posture and document it, rather than pursue
certified MISRA compliance.

- **Document the mapping, do not re-enforce.** A new `docs/coding-guidelines.md`
  maps the MISRA C++ intent areas to the clang-tidy checks already enforcing
  them, and records the MISRA-relevant checks that are disabled as deviations,
  each with the rationale reused from `.clang-tidy`.
- **Use MISRA's own deviation mechanism as the honest framing.** MISRA
  compliance is defined as conformance plus a record of documented deviations
  with rationale. The deviations table mirrors that structure, so the posture is
  defensible on MISRA's own terms rather than as a marketing label.
- **Do not enable new checks or force fixes in this change.** The disabled
  checks are off for real, code-shape reasons (gtkmm integer APIs, the sqlite
  `reinterpret_cast`, seeded-RNG demo data). Turning them on would generate
  noise and churn on working code without improving the design.
- **Claim "MISRA-aligned", never "MISRA compliant".** Certified compliance
  requires a qualified checker (Helix QAC, LDRA, Polyspace, Coverity) and a
  safety-critical project context, neither of which applies to a portfolio.

## Alternatives rejected
- **Run cppcheck's `--addon=misra`.** That addon targets MISRA **C**, not
  MISRA C++. On this C++20 codebase most rules are inapplicable (they govern C
  constructs) and it would produce language-mismatched noise, not a meaningful
  compliance signal.
- **Buy a qualified MISRA C++ checker.** Helix QAC / LDRA / Polyspace are
  licensed commercial tools. Cost and scope are unjustified for a portfolio, and
  their output cannot be reproduced by a reader without the same license.
- **Enable every MISRA-relevant clang-tidy check and fix all violations.** This
  would churn working code (narrowing on `Gtk::Grid::attach(int, int)`, the
  sqlite `reinterpret_cast`) to chase a compliance label the project does not
  claim, trading real readability for a checkbox.

## Consequences
- `docs/coding-guidelines.md` gives a reader a single place to see the static-
  analysis posture and the recorded deviations, defensible on MISRA's terms.
- The claim available on a CV is exact: "codebase follows a documented
  MISRA-C++-aligned clang-tidy profile with recorded deviations", not "MISRA
  compliant".
- Honesty rail: this is a coding-discipline posture on a non-safety-critical
  portfolio, not a certified-compliance artifact. It demonstrates familiarity
  with the MISRA model (conformance plus documented deviations), which is the
  transferable signal for the automotive/functional-safety lane. Adopting the
  qualified tooling and a formal deviation-approval process would be the next
  step in a real safety-critical project.
