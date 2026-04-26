<!--
  Thanks for sending a pull request. Fill in what's relevant; leave the
  sections you don't need empty (or delete them). Branch protection on
  `main` will block merge until all CI checks pass.
-->

## Summary

<!-- One paragraph: what does this PR do and why? Link the issue if any. -->

Closes #

## Changes

<!-- Bullet list of the concrete changes. Group by layer if it helps:
     Model / Presenter / View / Config / CI. -->

-
-

## Test plan

<!-- How was this tested? Be specific. -->

- [ ] `cmake --build build/debug` clean
- [ ] `xvfb-run ctest --output-on-failure` all green locally
- [ ] Manual smoke test on the GTK binary (describe what you did)
- [ ] Manual smoke test on the console binary (if relevant)

## Screenshots / scenario diff

<!-- Drop screenshots here for UI changes, or paste the relevant scenario
     `.expected` diff for output changes. Skip if non-visual / non-output. -->

## Checklist

- [ ] PR title follows the convention used by recent merged PRs
      (`Tests: ...`, `CI: ...`, `Docs: ...`, `Style: ...`, `Fix: ...`)
- [ ] No `// ----` banner separators introduced
- [ ] No emoji or non-ASCII typography (`->`, `--`, `...` not the Unicode forms)
- [ ] CHANGELOG `[Unreleased]` updated if the change is user-visible
- [ ] Coverage didn't regress more than 0.5pp (gcovr reports on each PR)
