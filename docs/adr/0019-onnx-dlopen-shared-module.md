# 0019. ONNX Runtime isolated behind a dlopen shared module

## Status

Accepted (2026-05). Amends ADR-0009: it keeps the `BUILD_ML_CLASSIFIER`
compile-time gate but reverses 0009's rejection of dynamic library loading.

## Context

With `BUILD_ML_CLASSIFIER=ON` (ADR-0009), `objectsMl` linked ONNX Runtime
directly into the GTK4 binary. That turned out to corrupt the process heap:
having `libonnxruntime` in the same address space collided with gtkmm during
widget registration on boot, surfacing as crashes inside `__libc_malloc`. It
took about a day of `gdb` to narrow it to ORT's allocator and static
initializers interacting with the GTK4 process rather than to any bug in the
inspection code itself.

ADR-0009 had rejected dynamic loading purely on complexity grounds. That
judgment was made before this crash was known. The compile-time gate decides
WHETHER the feature is built. It does nothing about the in-process link
conflict WHEN it is built, so a second isolation at the runtime level was
needed.

## Decision

Keep the compile-time gate from ADR-0009 unchanged: `BUILD_ML_CLASSIFIER`
still decides whether the ML feature exists at all. Change only HOW it is
built when ON.

The host binary no longer links ONNX Runtime. All ORT-touching code moves
into a separate SHARED MODULE library, `industrial_ml_ort.{so,dll,dylib}`,
loaded at runtime via `dlopen` / `LoadLibrary` on the first
`OnnxImageClassifier` construction. The module exposes a tiny C ABI of three
functions (create / classify / destroy) so the host never sees an ORT type at
link time. `objectsMl` now contains only the dlopen facade plus the foundation
pieces the module needs (Image, Preprocessor, Labels) and links no ORT.

## Consequences

+ The GTK4 boot path is clean of ORT symbols. The `__libc_malloc`
  heap-corruption crash is gone.
+ ORT lives behind a stable 3-function C ABI, so the host is insulated from
  ORT version and ABI churn. This is a clean dependency-inversion seam.
+ The module loads lazily on first use, so there is no ORT cost until an
  inspection actually runs.
- The complexity ADR-0009 warned about is real: a plugin loader, module path
  resolution, a C ABI to keep stable. Accepted as the price of a host process
  that cannot be crashed by its ML dependency.
- Two independent gates now stack. The compile-time `BUILD_ML_CLASSIFIER`
  decides existence. A missing module at runtime degrades open in the ADR-0007
  pattern: the inspection tab is skipped with a logged warning, never a crash.

## Follow-up (2026-09-22): the ML code under the sanitizers

The crash above was found with `gdb`, not by the sanitizers. They could
not have found it. The `sanitizers` CI job never configured
`BUILD_ML_CLASSIFIER`, so the ONNX code was not in the build it checked. That
was true when the crash happened and until this follow-up.

The `ml-integration` job now builds the two ML test targets a second time,
Debug with ASan + UBSan. It runs them with the same options as the
`sanitizers` job. Our side is instrumented: the facade, the plugin module and
the tests. The prebuilt ORT library is not, so ASan checks every access our
code makes, including into buffers ORT returns, but not ORT's own internals.
A skipped test fails that step, because a skip would mean the sanitizers
checked nothing while the job stayed green. No sanitizer is switched off and
no suppression is added.
