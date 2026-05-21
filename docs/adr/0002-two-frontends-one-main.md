# 0002. Two front-ends from one main.cpp

## Status

Accepted (2025-12)

## Context

The MVP layering from ADR 0001 promises that the View is a swappable
seam. That claim needs to be exercised by a second, materially
different View — otherwise "ViewObserver is an abstraction" is just
naming, not a proven boundary.

A headless console front-end also unlocks scenario-based end-to-end
tests in CI without a display server, and gives the demo binary a
remote / scripted entry point that the desktop UI can't provide.

## Decision

A single `src/main.cpp` that branches on `#ifdef CONSOLE_MODE` at
the call site of `Application::run` / `InitConsole::run`:

```cpp
#ifdef CONSOLE_MODE
    app::console::InitConsole console(bootstrap);
    console.run();
#else
    app::core::Application::instance().run(argc, argv);
#endif
```

Two CMake executable targets — `industrial-hmi` (no macro,
`objectsView + objectsAppGtk`) and `industrial-hmi-console`
(`CONSOLE_MODE`, links `objectsConsole`, **zero gtkmm**). Both share
the same `Bootstrap`, `Model`, `Presenter`, and `Integration` object
libraries.

## Alternatives

- **Two main.cpp files** — rejected. Bootstrap wiring would have to
  stay byte-identical between them; the next refactor would silently
  diverge.

- **Runtime `--headless` flag on one binary** — rejected. Would force
  gtkmm into the console binary's link line. The "zero gtkmm" check
  is the proof that the View seam is real; losing it deletes the
  whole point.

- **A toy "dump current state" console for tests only** — rejected.
  A real interactive REPL with start / stop / equipment toggle / etc
  exercises the same presenter commands a human operator uses, so
  the scenarios in `tests/scenarios/` test what the desktop UI tests.

## Consequences

+ `nm -D industrial-hmi-console | grep gtk_` returns empty. The View
  abstraction is mechanically verified at link time, not asserted.
+ `tests/scenarios/*.txt` run under `industrial-hmi-console` in CI
  with no Xvfb, no display server, no Wayland. Cross-platform E2E
  for free.
+ The console binary is also a useful operator/maintenance tool —
  scriptable, sshable, low-footprint.
- `main.cpp` is dual-purpose. Both branches must compile, both must
  stay consistent on bootstrap wiring. The discipline is enforced
  by `Bootstrap` owning the staged startup (ADR 0004) so the
  `#ifdef` block stays small.
- `objectsConsole` target adds CMake topology weight; offset by the
  fact that `objectsAppGtk` is correspondingly slimmer than
  pre-split `objectsCore`.
