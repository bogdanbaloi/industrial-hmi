// Plugin source compiled into `industrial_ml_ort.{dll,so,dylib}`.
//
// This is the only translation unit in the project that touches the
// ONNX Runtime headers, and the plugin shared library is the only
// artefact that links libonnxruntime. The host binary
// (`industrial-hmi.exe`) loads this plugin at runtime via `dlopen` /
// `LoadLibrary` so the ORT shared library never enters the process
// address space until the user requests an inspection -- which keeps
// the GTK4 boot path clear of the symbol-resolution conflicts that
// otherwise corrupt the libc heap on both Linux and Windows hosts.
//
// The plugin exports two C-linkage entry points that
// `src/ml/OnnxImageClassifier.cpp` resolves via `GetProcAddress` /
// `dlsym`:
//
//     ImageClassifier* industrial_ml_create_onnx_classifier(
//         const char* modelPath,
//         const char* labelsPath,
//         char* errorBuffer,
//         std::size_t errorBufferSize);
//
//     void industrial_ml_destroy_classifier(ImageClassifier* p);
//
// Returning a `new`-allocated `ImageClassifier*` and freeing it with
// the plugin's own `delete` keeps the allocator on a single side of
// the DLL boundary; both sides build with the same MSYS2 / Clang
// toolchain in our environment, so the libstdc++ runtime is shared,
// but routing destruction through the plugin protects against future
// runtime mismatches without changing the public API.

#include "src/ml/Classification.h"
#include "src/ml/Image.h"
#include "src/ml/ImageClassifier.h"
#include "src/ml/ImageNetLabels.h"
#include "src/ml/ImageNetPreprocessor.h"
#include "src/ml/Preprocessor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <onnxruntime_cxx_api.h>

#if defined(_WIN32)
#  define INDUSTRIAL_ML_API extern "C" __declspec(dllexport)
#else
#  define INDUSTRIAL_ML_API extern "C" __attribute__((visibility("default")))
#endif

namespace app::ml::plugin {

namespace {

constexpr const char* kEnvLoggerId = "industrial-hmi.OnnxImageClassifier";
constexpr int kIntraOpThreads = 1;
constexpr std::size_t kMinLogitCount = 2;

[[nodiscard]] std::vector<float> softmax(const float* logits,
                                         std::size_t count) {
    std::vector<float> probs(count);
    const float maxLogit = *std::max_element(logits, logits + count);
    float denom = 0.0F;
    for (std::size_t i = 0; i < count; ++i) {
        probs[i] = std::exp(logits[i] - maxLogit);
        denom += probs[i];
    }
    if (denom <= 0.0F) {
        throw std::runtime_error(
            "onnx_plugin: softmax denominator non-positive.");
    }
    for (auto& value : probs) {
        value /= denom;
    }
    return probs;
}

[[nodiscard]] std::vector<int>
    topKIndices(const std::vector<float>& probs, int k) {
    std::vector<int> ordering(probs.size());
    std::iota(ordering.begin(), ordering.end(), 0);

    const auto kSize = static_cast<std::size_t>(k);
    std::partial_sort(
        ordering.begin(),
        ordering.begin() + static_cast<std::ptrdiff_t>(kSize),
        ordering.end(),
        [&probs](int lhs, int rhs) {
            return probs[static_cast<std::size_t>(lhs)] >
                   probs[static_cast<std::size_t>(rhs)];
        });

    ordering.resize(kSize);
    return ordering;
}

/// The actual classifier. Lives entirely inside the plugin. The host
/// binary holds a base-class pointer (`ImageClassifier*`) and forwards
/// virtual calls; vtable layout is consistent because both sides share
/// the same `ImageClassifier` header and toolchain.
class OnnxClassifierImpl final : public ImageClassifier {
public:
    OnnxClassifierImpl(const std::filesystem::path& modelPath,
                       const std::filesystem::path& labelsPath)
        : labels_(labelsPath),
          env_(ORT_LOGGING_LEVEL_WARNING, kEnvLoggerId),
          options_(buildOptions()),
          ort_(buildSession(env_, options_, modelPath)),
          allocator_(),
          inputNameOwner_(ort_.GetInputNameAllocated(0, allocator_)),
          outputNameOwner_(ort_.GetOutputNameAllocated(0, allocator_)) {
        inputNames_[0]  = inputNameOwner_.get();
        outputNames_[0] = outputNameOwner_.get();
        name_ = "ONNX plugin (" + modelPath.filename().string() + ")";
    }

    ~OnnxClassifierImpl() override = default;

    OnnxClassifierImpl(const OnnxClassifierImpl&) = delete;
    OnnxClassifierImpl& operator=(const OnnxClassifierImpl&) = delete;
    OnnxClassifierImpl(OnnxClassifierImpl&&) = delete;
    OnnxClassifierImpl& operator=(OnnxClassifierImpl&&) = delete;

    [[nodiscard]] std::vector<Classification>
        classifyTopK(const Image& image, int k) const override {
        if (k <= 0) {
            throw std::invalid_argument(
                "onnx_plugin: k must be positive.");
        }

        std::vector<float> inputBuffer = preprocessor_.apply(image);
        const auto shape = preprocessor_.outputShape();
        const std::vector<std::int64_t> shapeVec(shape.begin(), shape.end());

        Ort::MemoryInfo memInfo = Ort::MemoryInfo::CreateCpu(
            OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
            memInfo,
            inputBuffer.data(),
            inputBuffer.size(),
            shapeVec.data(),
            shapeVec.size());

        std::vector<Ort::Value> outputs = ort_.Run(
            Ort::RunOptions{nullptr},
            inputNames_.data(),
            &inputTensor, 1U,
            outputNames_.data(), 1U);

        if (outputs.empty()) {
            throw std::runtime_error(
                "onnx_plugin: inference produced no outputs.");
        }

        const auto* logits = outputs.front().GetTensorData<float>();
        const auto logitInfo = outputs.front().GetTensorTypeAndShapeInfo();
        const std::size_t logitCount = logitInfo.GetElementCount();
        if (logitCount < kMinLogitCount) {
            throw std::runtime_error(
                "onnx_plugin: output tensor too small (" +
                std::to_string(logitCount) + " elements).");
        }

        const auto kClamped = std::min<std::size_t>(
            static_cast<std::size_t>(k), logitCount);

        const std::vector<float> probs = softmax(logits, logitCount);
        const std::vector<int> topIndices = topKIndices(
            probs, static_cast<int>(kClamped));

        std::vector<Classification> results;
        results.reserve(topIndices.size());
        for (const int classId : topIndices) {
            Classification entry;
            entry.classId = classId;
            entry.label = std::string(labels_.at(classId));
            entry.confidence = probs[static_cast<std::size_t>(classId)];
            results.push_back(std::move(entry));
        }
        return results;
    }

    [[nodiscard]] std::string name() const override { return name_; }

private:
    [[nodiscard]] static Ort::SessionOptions buildOptions() {
        Ort::SessionOptions options;
        options.SetIntraOpNumThreads(kIntraOpThreads);
        options.SetGraphOptimizationLevel(
            GraphOptimizationLevel::ORT_ENABLE_ALL);
        return options;
    }

    [[nodiscard]] static Ort::Session
        buildSession(Ort::Env& env,
                     const Ort::SessionOptions& options,
                     const std::filesystem::path& modelPath) {
#ifdef _WIN32
        return Ort::Session(env, modelPath.wstring().c_str(), options);
#else
        return Ort::Session(env, modelPath.string().c_str(), options);
#endif
    }

    // ORT objects are mutated by `Run` and the `*NameAllocated` calls
    // even when the surrounding `classifyTopK` is logically const --
    // ORT's API is not const-correct. `mutable` keeps the const
    // guarantee at the public surface.
    ImageNetLabels         labels_;
    ImageNetPreprocessor   preprocessor_;
    mutable Ort::Env       env_;
    Ort::SessionOptions    options_;
    mutable Ort::Session   ort_;
    mutable Ort::AllocatorWithDefaultOptions allocator_;
    Ort::AllocatedStringPtr          inputNameOwner_;
    Ort::AllocatedStringPtr          outputNameOwner_;
    std::array<const char*, 1>       inputNames_{};
    std::array<const char*, 1>       outputNames_{};
    std::string                      name_;
};

void copyError(char* buffer, std::size_t bufferSize, const char* msg) {
    if (buffer == nullptr || bufferSize == 0 || msg == nullptr) {
        return;
    }
    const std::size_t len = std::min(bufferSize - 1, std::strlen(msg));
    std::memcpy(buffer, msg, len);
    buffer[len] = '\0';
}

}  // namespace

}  // namespace app::ml::plugin

INDUSTRIAL_ML_API
app::ml::ImageClassifier*
industrial_ml_create_onnx_classifier(const char* modelPath,
                                     const char* labelsPath,
                                     char* errorBuffer,
                                     std::size_t errorBufferSize) {
    using app::ml::plugin::copyError;
    if (modelPath == nullptr || labelsPath == nullptr) {
        copyError(errorBuffer, errorBufferSize,
                  "null path argument");
        return nullptr;
    }
    try {
        return new app::ml::plugin::OnnxClassifierImpl(
            std::filesystem::path(modelPath),
            std::filesystem::path(labelsPath));
    } catch (const std::exception& exc) {
        copyError(errorBuffer, errorBufferSize, exc.what());
        return nullptr;
    } catch (...) {
        copyError(errorBuffer, errorBufferSize, "unknown plugin error");
        return nullptr;
    }
}

INDUSTRIAL_ML_API
void industrial_ml_destroy_classifier(app::ml::ImageClassifier* p) {
    delete p;
}
