#!/usr/bin/env bash
# Builds libdmdutil (DMD output: ZeDMD, Pixelcade, PIN2DMD, dmdserver) and stages what the
# editor links and ships into one prefix:
#   <prefix>/include/DMDUtil/   the headers (and pupdmd.h, libpupdmd's, for the PUP plugin)
#   <prefix>/lib/               libdmdutil and every shared library it loads
#
# Usage: build-libdmdutil.sh [--stage-only] <libdmdutil checkout> <platform> <arch> <prefix>
#   platform: macos, linux or win-mingw (in an MSYS2 UCRT64 shell)
#   arch:     arm64 (macos), x64 (linux, win-mingw), aarch64 (linux)
#   --stage-only: the checkout is built already (build/ or build-<arch>/); only stage it.
#
# libdmdutil's own platforms/<platform>/<arch>/external.sh fetches and builds its dependencies
# (libzedmd, libserum, libpupdmd, libvni, sockpp, libusb, libserialport, cargs) into
# third-party/runtime-libs/<platform>/<arch>.

set -euo pipefail

stage_only=0
if [ "${1:-}" = "--stage-only" ]; then
    stage_only=1
    shift
fi
if [ "$#" -ne 4 ]; then
    echo "Usage: $0 [--stage-only] <libdmdutil checkout> <platform> <arch> <prefix>" >&2
    exit 2
fi
source_dir=$(cd "$1" && pwd -P)
platform=$2
arch=$3
mkdir -p "$4"
prefix=$(cd "$4" && pwd -P)

jobs=$( (nproc || sysctl -n hw.ncpu) 2>/dev/null || echo 4)

# build <arch>: external dependencies and libdmdutil itself, into build-<arch>.
build() {
    local a=$1
    (
        cd "${source_dir}"
        "./platforms/${platform}/${a}/external.sh"
        cmake -DCMAKE_BUILD_TYPE=Release -DPLATFORM="${platform}" -DARCH="${a}" \
            -DBUILD_STATIC=OFF -DPOST_BUILD_COPY_EXT_LIBS=OFF -B "build-${a}"
        cmake --build "build-${a}" --target dmdutil_shared -- -j"${jobs}"
    )
}

# The build directory of an arch: build-<arch>, or build/ of a checkout built by hand.
build_dir() {
    if [ -d "${source_dir}/build-$1" ]; then
        echo "${source_dir}/build-$1"
    else
        echo "${source_dir}/build"
    fi
}

# stage_libs <arch> <target dir>: libdmdutil and its dependencies' shared libraries.
stage_libs() {
    local a=$1 target=$2 dir
    dir=$(build_dir "${a}")
    mkdir -p "${target}"
    case "${platform}" in
        macos) cp -a "${dir}"/libdmdutil*.dylib "${target}/" ;;
        linux) cp -a "${dir}"/libdmdutil.so* "${target}/" ;;
        win-mingw)
            cp -a "${dir}"/*dmdutil*.dll "${target}/"
            cp -a "${dir}"/*dmdutil*.dll.a "${target}/" 2>/dev/null || true
            ;;
        *) echo "Unknown platform ${platform}" >&2; exit 2 ;;
    esac
    cp -a "${source_dir}/third-party/runtime-libs/${platform}/${a}/." "${target}/"
    # The import libraries of the Windows DLLs, for what links them (the PUP plugin: libpupdmd).
    if [ "${platform}" = "win-mingw" ] && [ -d "${source_dir}/third-party/build-libs/${platform}/${a}" ]; then
        cp -a "${source_dir}/third-party/build-libs/${platform}/${a}/." "${target}/"
    fi
}

rm -rf "${prefix}/include/DMDUtil" "${prefix}/lib"
mkdir -p "${prefix}/include" "${prefix}/lib"
cp -a "${source_dir}/include/DMDUtil" "${prefix}/include/"
# libpupdmd's header: VPX's PUP plugin, built with the editor, links the libpupdmd staged here.
cp -a "${source_dir}/third-party/include/pupdmd.h" "${prefix}/include/"

if [ "${stage_only}" -eq 0 ]; then
    build "${arch}"
fi
stage_libs "${arch}" "${prefix}/lib"

echo "Staged libdmdutil into ${prefix}:"
ls "${prefix}/lib"
