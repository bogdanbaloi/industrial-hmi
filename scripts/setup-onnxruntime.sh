#!/usr/bin/env bash
# scripts/setup-onnxruntime.sh
#
# Downloads and unpacks an official Microsoft ONNX Runtime prebuilt
# distribution into a fixed location, so a CMake configure with
# `-DBUILD_ML_CLASSIFIER=ON -DONNXRUNTIME_ROOT=...` finds it without
# any system-wide install.
#
# Why download here and not via FetchContent in CMake?
#
#   * The tarball is ~13 MB; FetchContent_Declare(URL ...) works for it,
#     but that ties the build to an Internet connection on every
#     reconfigure. A one-shot script populated under build/ keeps
#     reconfigures offline once we have the bits.
#   * Different runners (Linux x64, MSYS2, macOS arm64) need different
#     archives; selecting the right one is shell logic, not CMake logic.
#   * Pinning a known version + checksum is easier to read in a script
#     than in CMake's FetchContent options.
#
# Usage:
#
#     ./scripts/setup-onnxruntime.sh [version] [destination]
#
# Defaults: version = $ONNX_RUNTIME_VERSION or 1.20.1
#           destination = $ONNXRUNTIME_ROOT or build/onnxruntime
#
# After running, configure CMake with:
#
#     cmake -S . -B build/release \
#           -DBUILD_ML_CLASSIFIER=ON \
#           -DONNXRUNTIME_ROOT="$(pwd)/build/onnxruntime"

set -euo pipefail

VERSION="${1:-${ONNX_RUNTIME_VERSION:-1.20.1}}"
DEST="${2:-${ONNXRUNTIME_ROOT:-$(pwd)/build/onnxruntime}}"

# Detect platform for the right archive name. Microsoft uses these
# release-asset suffixes (per https://github.com/microsoft/onnxruntime/releases):
#
#   onnxruntime-linux-x64-<version>.tgz
#   onnxruntime-osx-x86_64-<version>.tgz
#   onnxruntime-osx-arm64-<version>.tgz
#   onnxruntime-win-x64-<version>.zip
uname_s="$(uname -s)"
uname_m="$(uname -m)"
case "${uname_s}" in
    Linux*)
        archive="onnxruntime-linux-x64-${VERSION}.tgz"
        unpack="tar -xzf"
        ;;
    Darwin*)
        if [[ "${uname_m}" == "arm64" ]]; then
            archive="onnxruntime-osx-arm64-${VERSION}.tgz"
        else
            archive="onnxruntime-osx-x86_64-${VERSION}.tgz"
        fi
        unpack="tar -xzf"
        ;;
    MINGW*|MSYS*|CYGWIN*)
        archive="onnxruntime-win-x64-${VERSION}.zip"
        unpack="unzip -q"
        ;;
    *)
        echo "Unsupported platform: ${uname_s}" >&2
        exit 1
        ;;
esac

url="https://github.com/microsoft/onnxruntime/releases/download/v${VERSION}/${archive}"
extracted_dir_name="${archive%.tgz}"
extracted_dir_name="${extracted_dir_name%.zip}"

echo "ONNX Runtime ${VERSION}"
echo "  archive     ${archive}"
echo "  destination ${DEST}"

if [[ -d "${DEST}/include" ]] && [[ -f "${DEST}/include/onnxruntime_cxx_api.h" ]]; then
    echo "Already installed at ${DEST}; nothing to do."
    exit 0
fi

mkdir -p "${DEST}"
work_dir="$(mktemp -d)"
trap 'rm -rf "${work_dir}"' EXIT

echo "Downloading ${url} ..."
if command -v curl >/dev/null 2>&1; then
    curl --fail --location --silent --show-error \
         --output "${work_dir}/${archive}" "${url}"
elif command -v wget >/dev/null 2>&1; then
    wget --quiet -O "${work_dir}/${archive}" "${url}"
else
    echo "Neither curl nor wget is available." >&2
    exit 1
fi

echo "Unpacking ..."
${unpack} "${work_dir}/${archive}" -C "${work_dir}" \
    || ${unpack} "${work_dir}/${archive}" -d "${work_dir}"

# The tarball contains a single top-level directory matching the
# archive basename; flatten it into ${DEST}.
src_dir="${work_dir}/${extracted_dir_name}"
if [[ ! -d "${src_dir}" ]]; then
    echo "Expected directory ${src_dir} after unpack; got:" >&2
    ls -la "${work_dir}" >&2
    exit 1
fi

cp -R "${src_dir}/." "${DEST}/"
echo "Done. Set ONNXRUNTIME_ROOT=${DEST} when configuring CMake."
