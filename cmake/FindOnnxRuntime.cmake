# FindOnnxRuntime.cmake
#
# Locates the ONNX Runtime C++ headers and shared library.
#
# Why hand-rolled rather than `find_package(onnxruntime CONFIG)`?
#
#   * Microsoft ships a CMake config (`onnxruntimeConfig.cmake`) only in
#     some distribution channels. The official pre-built tarballs from
#     https://github.com/microsoft/onnxruntime/releases unpack to a flat
#     `include/` + `lib/` layout with no CMake helpers, which is what
#     our `scripts/setup-onnxruntime.sh` produces.
#   * pkg-config is not shipped with the prebuilt either.
#   * Linux distros (Debian, Ubuntu, Arch) and MSYS2 do NOT ship a
#     packaged ONNX Runtime as of late 2025 -- vendors prefer rolling
#     their own to control versions per project.
#
# Resolution order:
#
#   1. CMake / environment variable `ONNXRUNTIME_ROOT` pointing at an
#      extracted prebuilt distribution. Highest priority -- explicit
#      beats inference.
#   2. Common system locations: /usr/local/onnxruntime, /opt/onnxruntime,
#      C:/onnxruntime, MSYS2 mingw64.
#   3. Failure: prints a clear "how to fix this" message that points at
#      the setup script.
#
# On success, defines:
#
#   OnnxRuntime_FOUND         -- TRUE
#   OnnxRuntime_INCLUDE_DIRS  -- path to include/ (contains onnxruntime_cxx_api.h)
#   OnnxRuntime_LIBRARIES     -- absolute path to libonnxruntime.so / .dll / .dylib
#   OnnxRuntime_VERSION       -- detected via VERSION_NUMBER file when present
#
# And an imported target `OnnxRuntime::OnnxRuntime` so callers can do
# `target_link_libraries(my_target PRIVATE OnnxRuntime::OnnxRuntime)`.

include(FindPackageHandleStandardArgs)

# Candidate roots, in priority order.
set(_onnx_search_roots "")
if(DEFINED ONNXRUNTIME_ROOT)
    list(APPEND _onnx_search_roots "${ONNXRUNTIME_ROOT}")
endif()
if(DEFINED ENV{ONNXRUNTIME_ROOT})
    list(APPEND _onnx_search_roots "$ENV{ONNXRUNTIME_ROOT}")
endif()
list(APPEND _onnx_search_roots
    "/usr/local/onnxruntime"
    "/opt/onnxruntime"
    "C:/onnxruntime"
    "C:/Program Files/onnxruntime"
)
if(DEFINED ENV{MSYSTEM_PREFIX})
    list(APPEND _onnx_search_roots "$ENV{MSYSTEM_PREFIX}")
endif()

find_path(OnnxRuntime_INCLUDE_DIR
    NAMES onnxruntime_cxx_api.h
    HINTS ${_onnx_search_roots}
    PATH_SUFFIXES include onnxruntime/include
    DOC "Directory containing onnxruntime_cxx_api.h"
)

find_library(OnnxRuntime_LIBRARY
    NAMES onnxruntime
    HINTS ${_onnx_search_roots}
    PATH_SUFFIXES lib lib64 onnxruntime/lib
    DOC "Path to libonnxruntime.so / onnxruntime.lib / onnxruntime.dylib"
)

# Try to read the version from the prebuilt distribution's VERSION_NUMBER
# file. Microsoft ships this alongside include/ in their tarballs.
set(OnnxRuntime_VERSION "unknown")
if(OnnxRuntime_INCLUDE_DIR)
    set(_version_candidate "${OnnxRuntime_INCLUDE_DIR}/../VERSION_NUMBER")
    if(EXISTS "${_version_candidate}")
        file(READ "${_version_candidate}" OnnxRuntime_VERSION)
        string(STRIP "${OnnxRuntime_VERSION}" OnnxRuntime_VERSION)
    endif()
endif()

find_package_handle_standard_args(OnnxRuntime
    REQUIRED_VARS OnnxRuntime_INCLUDE_DIR OnnxRuntime_LIBRARY
    VERSION_VAR OnnxRuntime_VERSION
    FAIL_MESSAGE "ONNX Runtime not found. Set ONNXRUNTIME_ROOT to an extracted prebuilt distribution, or run scripts/setup-onnxruntime.sh"
)

if(OnnxRuntime_FOUND)
    set(OnnxRuntime_INCLUDE_DIRS "${OnnxRuntime_INCLUDE_DIR}")
    set(OnnxRuntime_LIBRARIES    "${OnnxRuntime_LIBRARY}")

    if(NOT TARGET OnnxRuntime::OnnxRuntime)
        add_library(OnnxRuntime::OnnxRuntime UNKNOWN IMPORTED)
        set_target_properties(OnnxRuntime::OnnxRuntime PROPERTIES
            IMPORTED_LOCATION "${OnnxRuntime_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${OnnxRuntime_INCLUDE_DIR}"
        )
    endif()
endif()

mark_as_advanced(OnnxRuntime_INCLUDE_DIR OnnxRuntime_LIBRARY)
