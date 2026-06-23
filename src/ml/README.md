# `src/ml/` -- Edge AI Image Classification

ONNX Runtime-based image classification integrated into the C++
presenter pipeline for visual defect detection on a manufacturing
terminal. Plugin-loaded so the heavy ORT shared library never
participates in the host binary's startup symbol resolution.

---

## Why this module exists separately

A modern industrial HMI sometimes carries a camera staring at the
product on the line. The traditional answer was "ship the frame to a
cloud GPU"; the modern answer is "run inference on the terminal,
report the verdict locally." Edge AI gives the operator the same
sub-second feedback loop they have for the analog quality bars,
without a network dependency.

Constraints we accepted:

- **Stay in C++** -- the rest of the application is C++; piping
  frames through a Python sidecar would add latency, dependency
  weight, and a packaging headache.
- **Pluggable model** -- which model the customer ships is a
  deployment decision (ImageNet-pretrained, fine-tuned per
  product, swapped on a contract). The classifier interface
  hides the runtime so a future PyTorch / TensorRT / OpenVINO
  swap is one new subclass.
- **Robust to ORT's surface** -- ONNX Runtime ships a large
  prebuilt shared library that bundles its own copies of
  abseil + protobuf + MLAS kernels + custom allocators. Linking
  it directly into a GTK4 process can corrupt the libc heap
  before `main()` runs the GTK loop. Workaround: load ORT as a
  **runtime plugin** so the host binary stays clean.
- **Testable headlessly** -- presenter unit tests inject a
  `FakeImageClassifier` returning a deterministic top-K. Zero
  ORT setup needed.

---

## Architecture (SOLID at a glance)

```
   ┌──────────────────┐    ┌──────────────────┐
   │ Camera / file    │───►│  ImageDecoder    │  bytes -> RGBA Image
   │ (PNG / JPEG)     │    └────────┬─────────┘
   └──────────────────┘             │
                                    ▼
                          ┌──────────────────┐
                          │   Preprocessor   │  ImageNet 224x224 norm
                          └────────┬─────────┘
                                   │
                                   ▼
                          ┌──────────────────┐
                          │  ImageClassifier │  interface
                          └────────▲─────────┘
                                   │
                ┌──────────────────┴───────────────────┐
                │                                      │
        FakeImageClassifier              OnnxImageClassifier
        (tests, deterministic)           ──► dlopen-ed ONNX plugin
                                              (onnx_plugin.cpp)
                                              ──► ImageNetLabels
                                              ──► ONNX Runtime session
```

**SOLID applied:**

- **S** -- one job per class. `ImageDecoder` decodes bytes.
  `Preprocessor` resizes + normalises. `ImageClassifier` infers.
  `ImageNetLabels` maps class id to human-readable name. Each
  swappable independently.
- **O** -- a future model that needs different preprocessing
  (e.g. 256x256 + custom mean / std) ships a new `Preprocessor`
  impl; existing decoder + classifier stay untouched.
- **L** -- `OnnxImageClassifier` and `FakeImageClassifier` are
  interchangeable from the presenter's perspective; the same
  `QualityInspectionPresenter` unit tests run against either.
- **I** -- `ImageClassifier` is two methods (classifyTopK, name).
  Threading, batching, GPU policy stay implementation-private.
- **D** -- presenter takes `ImageClassifier&`; tests inject the
  fake, production wires the ONNX concrete.

---

## API surface -- class-by-class

### `Image` + `ImageDecoder`

```cpp
struct Image {
    int                       width;
    int                       height;
    int                       channels;
    std::vector<std::uint8_t> pixels;     // row-major RGB
};

class ImageDecoder {
    Image decodeFile(const std::filesystem::path&) const;
    Image decodeMemory(std::span<const std::uint8_t>) const;
};
```

PNG / JPEG / BMP via stb_image (header-only, public-domain). The
decoder always returns 3-channel **RGB** (grayscale duplicated,
alpha dropped) so downstream stages never branch on channel count.
On a missing file / unsupported format / truncated payload it
throws `std::runtime_error` carrying `stbi_failure_reason`.

### `Preprocessor` + `ImageNetPreprocessor`

```cpp
class Preprocessor {
    virtual std::vector<float>          apply(const Image&) const = 0;
    virtual std::array<std::int64_t, 4> outputShape() const = 0;   // NCHW
    virtual std::string                 name() const = 0;
};
```

`ImageNetPreprocessor`: resize to 224×224, normalise to the
ImageNet mean / stdev triple, flatten to an NCHW float tensor.
`outputShape()` (`{batch, channels, height, width}`) lets the
classifier bind the ONNX input port without re-deriving the shape.

A custom model with a different input shape = new `Preprocessor`
subclass. Zero changes to the classifier or to call sites that
already use `Preprocessor&` injection. Note: the production
`OnnxImageClassifier` runs preprocessing internally inside
`classifyTopK(...)`, so callers pass a decoded `Image`, not a
pre-built tensor.

### `ImageClassifier`

```cpp
struct Classification {
    int         classId;       // ImageNet class index, 0..999
    std::string label;         // human-readable, resolved via ImageNetLabels
    float       confidence;    // softmax probability, 0..1
};

class ImageClassifier {
    virtual std::vector<Classification>
        classifyTopK(const Image& image, int k) const = 0;
    virtual std::string name() const = 0;
};
```

`classifyTopK` takes a **decoded `Image`** and does preprocessing
internally, then returns the top-`k` classifications sorted by
descending confidence. Throws on a non-positive `k` or on
inference failure -- callers get a populated result or an
exception, never a silently truncated / empty vector.

Top-K result so the UI can show "Pizza (94%) / Sandwich (3%) /
Burger (2%)" -- a single top-1 readout doesn't surface the
model's confidence margin which matters for quality decisions.

### `OnnxImageClassifier`

Owns a runtime ORT session via the `onnx_plugin` shared module.
Constructor takes the path to an `.onnx` file + a labels file
(one line = one class name). On construction:

1. `LoadLibrary` / `dlopen` the plugin module.
2. Plugin creates an ORT environment + session from the `.onnx`.
3. Each `classifyTopK(...)` call preprocesses the image, binds the
   input tensor + runs inference.

The plugin module exposes a tiny C ABI (3 functions: create /
classify / destroy) so the host binary never sees ORT types at
link time. **This is the workaround for the heap-corruption issue
described in the file header**; it took us a day of `gdb`
catching crashes in `__libc_malloc` before we found the right
isolation pattern.

### `FakeImageClassifier`

```cpp
class FakeImageClassifier final : public ImageClassifier {
public:
    explicit FakeImageClassifier(std::vector<Classification> cannedResults,
                                 std::string displayName = "Fake");

    std::vector<Classification>
        classifyTopK(const Image&, int k) const override;  // replays canned,
                                                           // truncated to k
    std::string name() const override;
};
```

Canned results are passed at construction (auto-sorted by
descending confidence) -- there is no `setNextResult`. `classifyTopK`
replays them, truncated to `k`, and throws on a non-positive `k`
to match the production contract. Used by every presenter test that
needs a classifier without paying the ORT setup cost.

### `ImageNetLabels`

`.cpp` ships a vector of 1000 ImageNet class names compiled in.
Looked up by class id from the model output. A custom model with
its own labels file is loaded the same way -- one text file, one
line per class.

---

## Embedding in another C++ project

Minimum dependencies for the host: only the C++20 standard.
For the plugin: ONNX Runtime headers + shared library at runtime.

### Bootstrap

```cpp
#include "ml/OnnxImageClassifier.h"
#include "ml/ImageDecoder.h"

// Concrete classifier (preprocessing happens internally).
app::ml::OnnxImageClassifier classifier{
    "models/mobilenetv2-int8.onnx",
    "models/imagenet_labels.txt"};

// Run inference -- hand the decoded image straight in.
app::ml::ImageDecoder decoder;
const auto image = decoder.decodeFile("frame.png");   // throws on bad input
const auto top5  = classifier.classifyTopK(image, /*k=*/5);
for (const auto& c : top5) {
    std::cout << c.label << " " << c.confidence << '\n';
}
```

### Drop-in for tests

```cpp
app::ml::FakeImageClassifier classifier{
    {
        { .classId = 42, .label = "tabby", .confidence = 0.94f },
        { .classId = 43, .label = "tiger", .confidence = 0.03f },
    },
    "Fake-2-class"};

// Pass to the presenter (which takes a classifier + a decoder):
app::ml::ImageDecoder decoder;
app::QualityInspectionPresenter pres{classifier, decoder};
// pres inspects a frame -> top hit "tabby" -- deterministic.
```

### Swapping the runtime (TensorRT / OpenVINO / libtorch)

```cpp
class LibTorchClassifier : public app::ml::ImageClassifier {
    LibTorchClassifier(std::filesystem::path tsModel);
    std::vector<Classification>
        classifyTopK(const app::ml::Image&, int k) const override;
    std::string name() const override { return "libtorch"; }
};

// In main.cpp -- change one line:
std::unique_ptr<app::ml::ImageClassifier> classifier =
    std::make_unique<LibTorchClassifier>("models/mobilenetv2.ts");
```

Zero presenter / preprocessor / decoder changes.

---

## Threading model

- **Decoder + preprocessor are stateless** -- safe to call from any
  thread; cheap copies of `Image` and `vector<float>`.
- **OnnxImageClassifier holds a single ORT session** behind an
  internal mutex. Concurrent `classifyTopK(...)` calls serialise; for
  the HMI workload (one camera, one inference per second) that's
  fine. A high-throughput batching server would spin a session
  pool inside the plugin -- transparent to callers.
- **Plugin handle (`dlopen` result)** loaded lazily on first
  construction, kept alive for the host's lifetime, released
  in the destructor.
- **Presenter callbacks** marshal back to the GTK main thread
  via `Glib::signal_idle` before touching widgets -- the
  classifier itself runs on whatever thread the presenter
  scheduled inference on.

---

## Testing

`tests/QualityInspectionPresenterTest.cpp` -- presenter wired
against `FakeImageClassifier`; asserts the inspection result
ViewModel matches the top class returned by the fake.

`tests/ImageClassifierTest.cpp` -- exercises the `ImageClassifier`
contract against `FakeImageClassifier`: top-K truncation, the
descending-confidence ordering guarantee, and the throwing
semantics for a non-positive `k`.

`tests/OnnxImageClassifierTest.cpp` -- gated behind
`-DBUILD_ML_CLASSIFIER=ON` because it loads a real `.onnx` file.
Skips at runtime if the model file is not present, so the test
binary is safe to run on any CI environment.

`tests/ImageNetPreprocessorTest.cpp` -- pixel-exact round-trip on
a synthetic 256×256 input; verifies the resize + normalise math
against a hand-computed reference.

`tests/ImageNetLabelsTest.cpp` -- the 1000-entry label table loads,
indexes in range, and rejects out-of-range class ids.

`tests/ImageDecoderTest.cpp` -- corrupt / truncated input throws
`std::runtime_error`, supported formats round-trip cleanly to RGB.

Run isolated:

```bash
cd build/debug
ctest -R '(Image|Onnx|QualityInspection)' --output-on-failure
```

---

## Plugin loader details

The `onnx_plugin.cpp` lives in the same folder but compiles into a
**separate shared module** (`industrial_ml_ort.dll` /
`libindustrial_ml_ort.so`). CMake sets up the boundary explicitly:

```cmake
add_library(industrial_ml_ort SHARED onnx_plugin.cpp)
target_link_libraries(industrial_ml_ort PRIVATE onnxruntime::onnxruntime)

# The host binary does NOT link onnxruntime. Only OnnxImageClassifier
# links industrial_ml_ort via the dynamic loader at runtime.
add_library(objectsMl ... OnnxImageClassifier.cpp ...)
# objectsMl deliberately omits any onnxruntime link.
```

`OnnxImageClassifier.cpp` calls `LoadLibrary` / `dlopen` on first
construction, resolves three C-ABI symbols (`classifier_create`,
`classifier_classify`, `classifier_destroy`), and calls them
through function pointers. The host's symbol space stays free
of ORT internals.

This was the difference between a binary that boots and one that
crashes in `__libc_malloc` before `main()` returns.

---

## Out of scope (intentional)

- **Training / fine-tuning** -- this module *runs* a model;
  training stays in PyTorch / TensorFlow / Keras and exports to
  ONNX. The C++ side never sees gradients.
- **GPU inference** -- ORT can use CUDA / DirectML providers, but
  industrial HMI terminals usually ship CPU-only Atom-class boxes.
  The provider list in `onnx_plugin.cpp` is CPU-only by default;
  a build that needs GPU swaps the provider registration line.
- **Streaming / batching** -- one inference per camera frame is
  enough for the HMI surface. A batching server would build on
  top of `ImageClassifier` rather than inside it.
- **Object detection / segmentation** -- the interface is
  classification-only (top-K labels). Detection (bounding boxes,
  YOLO-style) is a new interface alongside `ImageClassifier`, not
  a modification of it.
