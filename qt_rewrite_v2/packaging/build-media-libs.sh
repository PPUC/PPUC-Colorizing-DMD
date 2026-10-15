#!/usr/bin/env bash
# Builds the media libraries of VPX's PUP plugin (the PuP tester) and the editor's movie clip
# import, at the versions vpinball pins (its platforms/config.sh), and installs them into one
# prefix:
#   <prefix>/include/  SDL3, SDL3_image, SDL3_ttf, libavcodec, libavformat, libavutil,
#                      libswresample, libswscale
#   <prefix>/lib/      the shared libraries (macOS, Linux; on Windows the import libraries)
#   <prefix>/bin/      the DLLs (Windows)
#
# Usage: build-media-libs.sh <vpinball checkout> <platform> <arch> <prefix>
#   platform: macos, linux or win-mingw (in an MSYS2 UCRT64 shell)
#   arch:     arm64 (macos), x64 (linux, win-mingw), aarch64 (linux)
#
# Sources are fetched and built in <prefix>-work, kept for the next run: a prefix built from
# the same pins is left as it is. ffmpeg is built without its device and filter libraries,
# which neither PUP nor the import uses. On Windows the ffmpeg DLLs get the suffix 64
# (avcodec64-63.dll), the names PUP's LibAv.h loads, as in vpinball's own Windows builds.
# Needs cmake, ninja or make, curl, git (SDL_image's and SDL_ttf's vendored sources) and, for
# ffmpeg, make, nasm (not on arm64) and zlib's headers.

set -euo pipefail

if [ "$#" -ne 4 ]; then
    echo "Usage: $0 <vpinball checkout> <platform> <arch> <prefix>" >&2
    exit 2
fi
vpinball=$(cd "$1" && pwd -P)
platform=$2
arch=$3
mkdir -p "$4"
prefix=$(cd "$4" && pwd -P)
work="${prefix}-work"
mkdir -p "${work}"

pin() {
    local value
    value=$(sed -n "s/^$1=\\(.*\\)$/\\1/p" "${vpinball}/platforms/config.sh" | head -1)
    if [ -z "${value}" ]; then
        echo "No $1 in ${vpinball}/platforms/config.sh" >&2
        exit 1
    fi
    echo "${value}"
}
sdl_sha=$(pin SDL_SHA)
sdl_image_sha=$(pin SDL_IMAGE_SHA)
sdl_ttf_sha=$(pin SDL_TTF_SHA)
ffmpeg_sha=$(pin FFMPEG_SHA)

jobs=$( (nproc || sysctl -n hw.ncpu) 2>/dev/null || echo 4)
cmake_args=(-DCMAKE_BUILD_TYPE=Release "-DCMAKE_INSTALL_PREFIX=${prefix}"
    "-DCMAKE_PREFIX_PATH=${prefix}")
case "${platform}" in
    macos)
        export MACOSX_DEPLOYMENT_TARGET=13.3
        cmake_args+=("-DCMAKE_OSX_ARCHITECTURES=${arch}"
            "-DCMAKE_OSX_DEPLOYMENT_TARGET=${MACOSX_DEPLOYMENT_TARGET}"
            # Found by @rpath beside the plugin and the editor.
            -DCMAKE_INSTALL_NAME_DIR=@rpath)
        ;;
    linux)
        cmake_args+=("-DCMAKE_INSTALL_RPATH=\$ORIGIN")
        ;;
    win-mingw)
        cmake_args+=(-G "MSYS Makefiles")
        ;;
    *)
        echo "Unknown platform ${platform}" >&2
        exit 2
        ;;
esac
if command -v ninja >/dev/null 2>&1 && [ "${platform}" != "win-mingw" ]; then
    cmake_args+=(-G Ninja)
fi

fetch() {
    local repository=$1 sha=$2 directory=$3
    if [ ! -d "${work}/${directory}" ]; then
        curl -fsSL "https://github.com/${repository}/archive/${sha}.tar.gz" \
            -o "${work}/${directory}.tar.gz"
        # Without the Xcode projects, as vpinball unpacks them: their framework symbolic links
        # cannot be made on Windows, and no build here uses them. A folder left by an earlier
        # failed unpack goes first.
        rm -rf "${work}/$(basename "${repository}")-${sha}"
        tar xzf "${work}/${directory}.tar.gz" -C "${work}" --exclude='*/Xcode/*'
        mv "${work}/$(basename "${repository}")-${sha}" "${work}/${directory}"
        rm "${work}/${directory}.tar.gz"
    fi
}

# The sources SDL_image and SDL_ttf vendor, fetched once (an interrupted fetch starts over).
vendored() {
    local directory="${work}/$1"
    if [ ! -f "${directory}/.vendored" ]; then
        rm -rf "${directory}/external"/*/
        (cd "${directory}" && ./external/download.sh)
        touch "${directory}/.vendored"
    fi
}

expected="${platform}-${arch}-${sdl_sha}-${sdl_image_sha}-${sdl_ttf_sha}-${ffmpeg_sha}"
if [ "$(cat "${prefix}/media-libs.txt" 2>/dev/null || true)" = "${expected}" ]; then
    echo "Media libraries in ${prefix} are built from the current pins."
    exit 0
fi

# SDL3, then SDL_image and SDL_ttf against it, as vpinball builds them.
sdl_args=()
if [ "${platform}" = win-mingw ]; then
    # MSYS2 may have OpenGL ES headers without EGL's: SDL then takes its EGL path and fails
    # on <EGL/egl.h>. SDL draws with OpenGL or Direct3D on Windows; the PUP plugin hosted by
    # the editor does not use SDL's OpenGL ES.
    sdl_args+=(-DSDL_OPENGLES=OFF)
fi
fetch libsdl-org/SDL "${sdl_sha}" SDL
cmake -S "${work}/SDL" -B "${work}/SDL/build" "${cmake_args[@]}" \
    -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST_LIBRARY=OFF ${sdl_args[@]+"${sdl_args[@]}"}
cmake --build "${work}/SDL/build" -j "${jobs}"
cmake --install "${work}/SDL/build"

fetch libsdl-org/SDL_image "${sdl_image_sha}" SDL_image
vendored SDL_image
cmake -S "${work}/SDL_image" -B "${work}/SDL_image/build" "${cmake_args[@]}" \
    -DBUILD_SHARED_LIBS=ON -DSDLIMAGE_SAMPLES=OFF -DSDLIMAGE_DEPS_SHARED=ON \
    -DSDLIMAGE_VENDORED=ON -DSDLIMAGE_AVIF=OFF -DSDLIMAGE_WEBP=OFF
cmake --build "${work}/SDL_image/build" -j "${jobs}"
cmake --install "${work}/SDL_image/build"

fetch libsdl-org/SDL_ttf "${sdl_ttf_sha}" SDL_ttf
vendored SDL_ttf
cmake -S "${work}/SDL_ttf" -B "${work}/SDL_ttf/build" "${cmake_args[@]}" \
    -DBUILD_SHARED_LIBS=ON -DSDLTTF_SAMPLES=OFF -DSDLTTF_VENDORED=ON -DSDLTTF_HARFBUZZ=ON
cmake --build "${work}/SDL_ttf/build" -j "${jobs}"
cmake --install "${work}/SDL_ttf/build"

# ffmpeg: the libraries only, shared.
fetch FFmpeg/FFmpeg "${ffmpeg_sha}" ffmpeg
# Nothing found on the build machine by chance (Homebrew's xz, VA-API, ...): a library the
# package does not ship would fail to load on another computer. zlib is part of every system
# and PNG decoding needs it.
ffmpeg_args=(--prefix="${prefix}" --enable-shared --disable-static --disable-programs
    --disable-doc --disable-avdevice --disable-avfilter --disable-autodetect --enable-zlib)
case "${platform}" in
    macos)
        ffmpeg_args+=(--arch="${arch}" --cc="clang -arch ${arch}"
            --extra-cflags="-mmacosx-version-min=${MACOSX_DEPLOYMENT_TARGET}"
            --extra-ldflags="-mmacosx-version-min=${MACOSX_DEPLOYMENT_TARGET}"
            --install-name-dir=@rpath)
        ;;
    linux)
        ffmpeg_args+=(--extra-ldflags='-Wl,-rpath,$$ORIGIN')
        ;;
    win-mingw)
        ffmpeg_args+=(--arch=x86_64 --build-suffix=64)
        ;;
esac
(
    cd "${work}/ffmpeg"
    ./configure "${ffmpeg_args[@]}"
    make -j "${jobs}"
    make install
)

echo "${expected}" > "${prefix}/media-libs.txt"
echo "Media libraries installed in ${prefix}"
