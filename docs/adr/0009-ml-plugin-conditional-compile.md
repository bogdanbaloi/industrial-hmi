# 0009. ML plugin gated by build-time flag

## Status

Accepted (2026-04)

## Context

The edge AI quality inspection module is a recent feature: ONNX
Runtime-based image classification integrated into the C++
presenter pipeline for visual defect detection. It's the marquee
"modern stack" demo on the portfolio, but it carries real costs:

- ONNX Runtime is a ~30 MB native library (`libonnxruntime.so` or
  `onnxruntime.dll`) that must be present at link + runtime.
- The model + labels artifacts (`assets/models/`) add ~25 MB to the
  packaged binary.
- The image decoder pulls `libpng` / `libjpeg-turbo` headers via
  `stb_image`; another dependency surface.

Many target deployments (a railway control panel, a small
manufacturing demo) have no use for image classification and no
appetite for the dependency cost.

## Decision

A CMake option `BUILD_ML_CLASSIFIER` (default ON when ONNX Runtime
is detected, OFF otherwise) controls the entire ML feature:

- When ON: `objectsMl` is built and linked into the GTK binary;
  `INDUSTRIAL_HMI_HAS_ML_PLUGIN` is defined; `MainWindow` constructs
  the inspection decoder + classifier + presenter behind an
  `#ifdef` and registers the `QualityInspectionPage` tab.
- When OFF: every ML symbol is excluded from the build entirely.
  The `unique_ptr<OnnxImageClassifier>` member is hidden under the
  same `#ifdef`. Binary size drops by the ONNX Runtime + model size.
  The Quality Inspection tab simply doesn't exist.

The runtime degradation pattern from ADR 0007 still applies: even
when built ON, if the model file is missing or fails to load, the
tab is skipped silently (logged warning) rather than crashing.

## Alternatives

- **Always link ONNX Runtime** — rejected. Forcing a 30 MB
  dependency on every deployment for a feature 80% of targets
  don't use is a regression on the "minimal core" intent.

- **Runtime-only opt-in, dynamic library load** — rejected. Adds
  a `dlopen` / `LoadLibrary` complexity layer; CI matrix grows to
  test both states; mismatched ONNX versions become a runtime
  surprise. Compile-time gating catches drift at build time.

- **Separate `industrial-hmi-ml` binary** — rejected. Would
  fragment users / docs / release artifacts and prevent the
  inspection feature from sharing the presenter / observer
  infrastructure with the rest of the dashboard.

## Consequences

+ Embedded deployments (e.g. ARM targets without ONNX wheels) build
  successfully with `BUILD_ML_CLASSIFIER=OFF`, no source changes.
+ CI builds both states (`ON` for the main matrix, `OFF` for one
  job per platform) so the no-ML codepath is mechanically
  exercised, not just claimed.
+ Binary size profile is honest: the "with-ML" release ships in a
  larger zip; the "core" build is small. Both options are visible
  in the CHANGELOG.
- `#ifdef INDUSTRIAL_HMI_HAS_ML_PLUGIN` in `MainWindow.cpp`
  scatters the conditional across the file. Mitigated by a single
  `if constexpr`-friendly factory in a follow-up; for now the
  `#ifdef` is documented at each site.
- `objectsMl` is the only object library with an external runtime
  dependency that's not always present, so CMake topology has one
  asymmetric branch. Captured in the build documentation.
