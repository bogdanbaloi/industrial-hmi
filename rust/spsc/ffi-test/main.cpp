// C++ harness that proves the interop: it loads the Rust cdylib at runtime
// (LoadLibrary on Windows, dlopen on Linux, the same seam as the ONNX plugin),
// resolves the C ABI symbols, then drives the Rust SPSC queue from two threads
// (one producer, one consumer) and checks every sample arrives in order.
//
// Build + run: see ffi-test/run.sh (or the CI "ffi" job). Pass the library path
// as argv[1], otherwise a platform default is used.

#include "hmi_spsc.h"

#include <cstdint>
#include <cstdio>
#include <thread>

#ifdef _WIN32
#include <windows.h>
static void* load_lib(const char* p) { return static_cast<void*>(LoadLibraryA(p)); }
static void* sym(void* h, const char* n) {
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(h), n));
}
static const char* kDefaultLib = "hmi_spsc.dll";
#else
#include <dlfcn.h>
static void* load_lib(const char* p) { return dlopen(p, RTLD_NOW); }
static void* sym(void* h, const char* n) { return dlsym(h, n); }
static const char* kDefaultLib = "./libhmi_spsc.so";
#endif

using create_fn = void (*)(SpscProducer**, SpscConsumer**);
using push_fn = bool (*)(SpscProducer*, Sample);
using pop_fn = bool (*)(SpscConsumer*, Sample*);
using pdestroy_fn = void (*)(SpscProducer*);
using cdestroy_fn = void (*)(SpscConsumer*);

int main(int argc, char** argv) {
    const char* lib = (argc > 1) ? argv[1] : kDefaultLib;
    void* handle = load_lib(lib);
    if (handle == nullptr) {
        std::fprintf(stderr, "cannot load %s\n", lib);
        return 1;
    }

    auto create = reinterpret_cast<create_fn>(sym(handle, "hmi_spsc_create"));
    auto push = reinterpret_cast<push_fn>(sym(handle, "hmi_spsc_push"));
    auto pop = reinterpret_cast<pop_fn>(sym(handle, "hmi_spsc_pop"));
    auto pdestroy = reinterpret_cast<pdestroy_fn>(sym(handle, "hmi_spsc_producer_destroy"));
    auto cdestroy = reinterpret_cast<cdestroy_fn>(sym(handle, "hmi_spsc_consumer_destroy"));
    if (create == nullptr || push == nullptr || pop == nullptr || pdestroy == nullptr ||
        cdestroy == nullptr) {
        std::fprintf(stderr, "missing C ABI symbol\n");
        return 1;
    }

    SpscProducer* producer = nullptr;
    SpscConsumer* consumer = nullptr;
    create(&producer, &consumer);

    const uint64_t kCount = 100000;

    std::thread producer_thread([&] {
        for (uint64_t i = 0; i < kCount;) {
            Sample s{i, static_cast<double>(i) * 1.5};
            if (push(producer, s)) {
                ++i;
            } else {
                std::this_thread::yield();
            }
        }
    });

    uint64_t next = 0;
    while (next < kCount) {
        Sample got{};
        if (pop(consumer, &got)) {
            if (got.ts_ms != next || got.value != static_cast<double>(next) * 1.5) {
                std::fprintf(stderr, "MISMATCH at %llu\n", static_cast<unsigned long long>(next));
                producer_thread.join();
                return 2;
            }
            ++next;
        } else {
            std::this_thread::yield();
        }
    }

    producer_thread.join();
    pdestroy(producer);
    cdestroy(consumer);

    std::printf("OK: C++ drove the Rust SPSC across the C ABI, %llu samples in order\n",
                static_cast<unsigned long long>(kCount));
    return 0;
}
