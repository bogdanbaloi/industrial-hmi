#include "src/ml/OnnxImageClassifier.h"

#include "src/ml/Classification.h"
#include "src/ml/Image.h"
#include "src/ml/ImageNetLabels.h"
#include "src/ml/Preprocessor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <onnxruntime_cxx_api.h>

namespace app::ml {

namespace {

/// Logger / instance name passed to the ORT environment. Surfaces in
/// any ORT log line so tail-of-log diagnostics are attributable to
/// this binary rather than a generic "default" tag.
constexpr const char* kEnvLoggerId = "industrial-hmi.OnnxImageClassifier";

/// Number of intra-op threads. CPU EP defaults to all cores; for
/// single-image latency benchmarking and a typical industrial PC
/// (4-8 cores) we cap at the number of physical cores. Setting a
/// fixed value is cheaper than letting ORT autodetect on every Run().
constexpr int kIntraOpThreads = 1;

/// Floor on the number of values in a softmax / argmax pass. The
/// classifier rejects any inference output smaller than this; an
/// ImageNet model has >= 1000 logits so this is a safety net against
/// loading a wrong model file by mistake.
constexpr std::size_t kMinLogitCount = 2;

/// Numerically stable softmax over a flat float buffer. Standard
/// "subtract the max before exponentiating" trick to keep partial sums
/// inside float32 range; the final denominator is the sum of the
/// shifted exponentials.
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
            "OnnxImageClassifier: softmax denominator non-positive.");
    }
    for (auto& value : probs) {
        value /= denom;
    }
    return probs;
}

/// Indices of the `k` largest values in `probs`, sorted descending.
/// Uses partial sort because we only care about the head of the
/// distribution; full sort over 1000 entries is wasted work for k <= 10.
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

}  // namespace

/// All ORT state lives behind this struct. Forward-declared in the
/// header so ORT types don't leak into consumer translation units.
struct OnnxImageClassifier::Session {
    Ort::Env env;
    Ort::SessionOptions options;
    Ort::Session ort;
    Ort::AllocatorWithDefaultOptions allocator;

    /// Cached input / output tensor names. ORT's `GetInputName` /
    /// `GetOutputName` allocate a string each call; we read them once
    /// in the constructor and keep the strings alive for the life of
    /// the classifier.
    Ort::AllocatedStringPtr inputNameOwner;
    Ort::AllocatedStringPtr outputNameOwner;
    std::array<const char*, 1> inputNames{};
    std::array<const char*, 1> outputNames{};

    Session(const std::filesystem::path& modelPath,
            int intraOpThreads)
        : env(ORT_LOGGING_LEVEL_WARNING, kEnvLoggerId),
          options(),
          ort(buildSession(env, options, modelPath, intraOpThreads)),
          allocator(),
          inputNameOwner(ort.GetInputNameAllocated(0, allocator)),
          outputNameOwner(ort.GetOutputNameAllocated(0, allocator)) {
        inputNames[0] = inputNameOwner.get();
        outputNames[0] = outputNameOwner.get();
    }

    /// Build the ORT session. Split out so the `ort` member can be
    /// initialised in the member-initialiser list with the configured
    /// `options` already applied.
    [[nodiscard]] static Ort::Session
        buildSession(Ort::Env& env,
                     Ort::SessionOptions& options,
                     const std::filesystem::path& modelPath,
                     int intraOpThreads) {
        options.SetIntraOpNumThreads(intraOpThreads);
        options.SetGraphOptimizationLevel(
            GraphOptimizationLevel::ORT_ENABLE_ALL);

#ifdef _WIN32
        return Ort::Session(env, modelPath.wstring().c_str(), options);
#else
        return Ort::Session(env, modelPath.string().c_str(), options);
#endif
    }
};

OnnxImageClassifier::OnnxImageClassifier(
    std::filesystem::path modelPath,
    const Preprocessor& preprocessor,
    const ImageNetLabels& labels)
    : preprocessor_(preprocessor),
      labels_(labels),
      name_("ONNX (" + modelPath.filename().string() + ")") {
    if (!std::filesystem::exists(modelPath)) {
        throw std::runtime_error(
            "OnnxImageClassifier: model file not found: " +
            modelPath.string());
    }

    try {
        session_ = std::make_unique<Session>(modelPath, kIntraOpThreads);
    } catch (const Ort::Exception& exc) {
        throw std::runtime_error(
            std::string("OnnxImageClassifier: ORT failed to load ") +
            modelPath.string() + ": " + exc.what());
    }
}

OnnxImageClassifier::~OnnxImageClassifier() = default;

std::vector<Classification>
    OnnxImageClassifier::classifyTopK(const Image& image, int k) const {
    if (k <= 0) {
        throw std::invalid_argument(
            "OnnxImageClassifier: k must be positive.");
    }

    // 1. Preprocess: image -> NCHW float32 tensor.
    std::vector<float> inputBuffer = preprocessor_.apply(image);
    const auto shape = preprocessor_.outputShape();
    const std::vector<std::int64_t> shapeVec(shape.begin(), shape.end());

    // 2. Wrap into an Ort::Value (no copy; ORT reads from inputBuffer).
    Ort::MemoryInfo memInfo = Ort::MemoryInfo::CreateCpu(
        OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
        memInfo,
        inputBuffer.data(),
        inputBuffer.size(),
        shapeVec.data(),
        shapeVec.size());

    // 3. Run the session.
    std::vector<Ort::Value> outputs;
    try {
        outputs = session_->ort.Run(
            Ort::RunOptions{nullptr},
            session_->inputNames.data(),
            &inputTensor, 1U,
            session_->outputNames.data(), 1U);
    } catch (const Ort::Exception& exc) {
        throw std::runtime_error(
            std::string("OnnxImageClassifier: inference failed: ") +
            exc.what());
    }

    if (outputs.empty()) {
        throw std::runtime_error(
            "OnnxImageClassifier: inference produced no outputs.");
    }

    // 4. Extract logits as a flat float buffer.
    const auto* logits = outputs.front().GetTensorData<float>();
    const auto logitInfo = outputs.front().GetTensorTypeAndShapeInfo();
    const std::size_t logitCount = logitInfo.GetElementCount();
    if (logitCount < kMinLogitCount) {
        throw std::runtime_error(
            "OnnxImageClassifier: output tensor too small (" +
            std::to_string(logitCount) + " elements).");
    }

    const auto kClamped = std::min<std::size_t>(
        static_cast<std::size_t>(k), logitCount);

    // 5. Softmax, top-K, label resolution.
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

std::string OnnxImageClassifier::name() const {
    return name_;
}

}  // namespace app::ml
