#include "src/ml/OnnxImageClassifier.h"

#include "src/ml/Classification.h"
#include "src/ml/Image.h"
#include "src/ml/ImageClassifier.h"

#include <array>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

namespace app::ml {

namespace {

/// Symbols exported by the plugin (`industrial_ml_ort.{dll,so,dylib}`).
/// Kept C-linkage so the names are stable across compilers.
constexpr const char* kCreateSymbol  =
    "industrial_ml_create_onnx_classifier";
constexpr const char* kDestroySymbol =
    "industrial_ml_destroy_classifier";

using CreateFn = ImageClassifier* (*)(const char* modelPath,
                                      const char* labelsPath,
                                      char* errorBuffer,
                                      std::size_t errorBufferSize);
using DestroyFn = void (*)(ImageClassifier*);

/// Plugin filenames probed in order. On Windows the dynamic loader
/// includes the current working directory in its search path by
/// default, so a bare filename is enough. On Linux / macOS
/// `dlopen("name")` only searches the system paths + `LD_LIBRARY_PATH`,
/// so we also try `./name` to pick up the plugin sitting next to the
/// binary at run time.
const std::array<const char*, 2> kPluginFilenames = {
#if defined(_WIN32)
    "industrial_ml_ort.dll",
    ".\\industrial_ml_ort.dll",
#elif defined(__APPLE__)
    "libindustrial_ml_ort.dylib",
    "./libindustrial_ml_ort.dylib",
#else
    "libindustrial_ml_ort.so",
    "./libindustrial_ml_ort.so",
#endif
};

#ifdef _WIN32
using PluginHandle = HMODULE;

PluginHandle openPluginByName(const char* path) {
    return ::LoadLibraryA(path);
}

void* lookupSymbol(PluginHandle handle, const char* name) {
    return reinterpret_cast<void*>(::GetProcAddress(handle, name));
}

std::string lastOpenError() {
    return "LoadLibrary failed (Win32 error " +
           std::to_string(static_cast<unsigned long>(::GetLastError())) +
           ")";
}
#else
using PluginHandle = void*;

PluginHandle openPluginByName(const char* path) {
    return ::dlopen(path, RTLD_NOW | RTLD_LOCAL);
}

void* lookupSymbol(PluginHandle handle, const char* name) {
    return ::dlsym(handle, name);
}

std::string lastOpenError() {
    const char* msg = ::dlerror();
    return msg != nullptr ? msg : "unknown dlopen error";
}
#endif

/// Process-wide cached plugin state. Loaded once on first
/// `OnnxImageClassifier` construction; kept resident for the rest of
/// the process lifetime so subsequent constructions are cheap and so
/// the OS does not unload + reload the multi-megabyte ONNX Runtime
/// shared library on every inspection.
struct PluginState {
    PluginHandle handle = nullptr;
    CreateFn createFn   = nullptr;
    DestroyFn destroyFn = nullptr;
};

PluginState& sharedState() {
    static PluginState state;
    return state;
}

std::mutex& sharedStateMutex() {
    static std::mutex mutex;
    return mutex;
}

/// Resolve the plugin and cache its entry points. Throws
/// `std::runtime_error` with a message that lists the filenames we
/// tried so deployments can diagnose missing artefacts at a glance.
void ensurePluginLoaded() {
    const std::scoped_lock lock(sharedStateMutex());
    auto& state = sharedState();
    if (state.createFn != nullptr) {
        return;
    }

    std::string failures;
    for (const char* name : kPluginFilenames) {
        state.handle = openPluginByName(name);
        if (state.handle != nullptr) {
            break;
        }
        if (!failures.empty()) {
            failures += "; ";
        }
        failures += name;
        failures += " (";
        failures += lastOpenError();
        failures += ")";
    }

    if (state.handle == nullptr) {
        throw std::runtime_error(
            "OnnxImageClassifier: plugin not found. Searched: " +
            failures);
    }

    state.createFn = reinterpret_cast<CreateFn>(
        lookupSymbol(state.handle, kCreateSymbol));
    state.destroyFn = reinterpret_cast<DestroyFn>(
        lookupSymbol(state.handle, kDestroySymbol));

    if (state.createFn == nullptr || state.destroyFn == nullptr) {
        throw std::runtime_error(
            std::string("OnnxImageClassifier: plugin missing entry points "
                        "(") + kCreateSymbol + " / " + kDestroySymbol + ")");
    }
}

}  // namespace

OnnxImageClassifier::OnnxImageClassifier(
    const std::filesystem::path& modelPath,
    const std::filesystem::path& labelsPath) {
    if (!std::filesystem::exists(modelPath)) {
        throw std::runtime_error(
            "OnnxImageClassifier: model file not found: " +
            modelPath.string());
    }
    if (!std::filesystem::exists(labelsPath)) {
        throw std::runtime_error(
            "OnnxImageClassifier: labels file not found: " +
            labelsPath.string());
    }

    ensurePluginLoaded();
    const auto& state = sharedState();

    constexpr std::size_t kErrorBufferSize = 512;
    std::array<char, kErrorBufferSize> errorBuffer{};
    pluginImpl_ = state.createFn(
        modelPath.string().c_str(),
        labelsPath.string().c_str(),
        errorBuffer.data(),
        errorBuffer.size());
    pluginDestroy_ = state.destroyFn;

    if (pluginImpl_ == nullptr) {
        const std::string detail =
            errorBuffer[0] != '\0' ? errorBuffer.data() : "plugin returned null";
        throw std::runtime_error(
            "OnnxImageClassifier: plugin failed to instantiate session: " +
            detail);
    }

    name_ = "ONNX (" + modelPath.filename().string() + ")";
}

OnnxImageClassifier::~OnnxImageClassifier() {
    if (pluginImpl_ != nullptr && pluginDestroy_ != nullptr) {
        pluginDestroy_(pluginImpl_);
    }
}

std::vector<Classification>
OnnxImageClassifier::classifyTopK(const Image& image, int k) const {
    if (pluginImpl_ == nullptr) {
        throw std::runtime_error(
            "OnnxImageClassifier: plugin instance is null.");
    }
    return pluginImpl_->classifyTopK(image, k);
}

std::string OnnxImageClassifier::name() const {
    return name_;
}

}  // namespace app::ml
