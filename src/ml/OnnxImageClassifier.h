#pragma once

#include "src/ml/Classification.h"
#include "src/ml/Image.h"
#include "src/ml/ImageClassifier.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace app::ml {

/// `ImageClassifier` backed by an ONNX Runtime session, loaded as a
/// runtime plugin.
///
/// Why a plugin and not a direct link?
///
/// ONNX Runtime ships as a large prebuilt shared library that bundles
/// its own copies of common system dependencies (abseil, protobuf,
/// MLAS kernels, custom allocators). When linked directly into a GTK4
/// process the resulting symbol-resolution interactions can corrupt
/// the libc heap before `main()` even runs the GTK loop -- both on
/// WSL2/Linux and on MSYS2/Windows in our environment.
///
/// To keep the GTK binary boot path clean we route the ORT-touching
/// code through a separate shared-module library (`industrial_ml_ort.dll`
/// on Windows / `libindustrial_ml_ort.so` on Linux) that is `dlopen`-ed
/// (or `LoadLibrary`-ed) on first construction of this class. The
/// host binary never holds a `DT_NEEDED` reference to libonnxruntime;
/// the dynamic linker only pulls it in when the plugin module is
/// loaded, which is the moment the user actually requests an
/// inspection.
///
/// Public contract:
///
///   * Construction throws `std::runtime_error` if the plugin module
///     cannot be located OR if the model file fails to load. Failures
///     to find the plugin produce a message that names the search
///     paths so deployments can fix the layout.
///   * `classifyTopK` and `name` forward to the loaded plugin instance.
///   * Destruction tears down the plugin instance + drops the dlopen
///     handle; the library may stay mapped if other instances exist
///     (the loader caches the handle for the process lifetime).
///
/// SOLID: same as before -- presenters depend on `ImageClassifier&`,
/// not on the plugin loader. The plugin pattern is an implementation
/// detail, transparent to upstream code.
class OnnxImageClassifier final : public ImageClassifier {
public:
    /// Construct from on-disk artefact paths.
    ///
    /// `modelPath`  -- exported `.onnx` file (FP32 or INT8).
    /// `labelsPath` -- one-label-per-line text file (UTF-8). The plugin
    ///                 owns its own copy; `ImageNetLabels` instances on
    ///                 the host side are not exchanged across the DLL
    ///                 boundary to keep the C ABI minimal.
    OnnxImageClassifier(const std::filesystem::path& modelPath,
                        const std::filesystem::path& labelsPath);

    ~OnnxImageClassifier() override;

    OnnxImageClassifier(const OnnxImageClassifier&) = delete;
    OnnxImageClassifier& operator=(const OnnxImageClassifier&) = delete;
    OnnxImageClassifier(OnnxImageClassifier&&) = delete;
    OnnxImageClassifier& operator=(OnnxImageClassifier&&) = delete;

    [[nodiscard]] std::vector<Classification>
        classifyTopK(const Image& image, int k) const override;

    [[nodiscard]] std::string name() const override;

private:
    /// Plugin-owned `ImageClassifier` produced by the plugin's create
    /// entry point. Allocated by the plugin, deleted by the plugin --
    /// the destructor calls back through a destroy function pointer
    /// resolved at construction.
    ImageClassifier* pluginImpl_ = nullptr;

    /// Pointer to the plugin's destroy entry point. Cached so the
    /// destructor never pays the cost of another `dlsym`.
    void (*pluginDestroy_)(ImageClassifier*) = nullptr;

    /// Display-friendly name. Cached at construction so `name()` does
    /// not have to cross the plugin boundary on every call.
    std::string name_;
};

}  // namespace app::ml
