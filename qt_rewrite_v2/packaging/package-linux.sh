#!/usr/bin/env bash
# Packs ColorizingDMD into an AppImage with linuxdeploy and linuxdeploy-plugin-qt, and
# smoke-tests it.
#
# Usage: package-linux.sh <build dir> <output .AppImage>
#
# The build must be configured with -DCOLORIZINGDMD_QT_V2_PACKAGE=ON. Environment:
#   LINUXDEPLOY  linuxdeploy-<arch>.AppImage (required); linuxdeploy-plugin-qt-<arch>.AppImage
#                has to lie beside it or be on PATH
#   QMAKE        qmake of the Qt the build used (default: qmake6, then qmake)

set -euo pipefail

if [ "$#" -ne 2 ]; then
    echo "Usage: $0 <build dir> <output .AppImage>" >&2
    exit 2
fi

here=$(cd "$(dirname "$0")" && pwd -P)
build_dir=$(cd "$1" && pwd -P)
output=$(cd "$(dirname "$2")" && pwd -P)/$(basename "$2")
executable="${build_dir}/app/ColorizingDMD"
: "${LINUXDEPLOY:?set LINUXDEPLOY to linuxdeploy-<arch>.AppImage}"

if [ ! -x "${executable}" ]; then
    echo "No ${executable}: configure the build with -DCOLORIZINGDMD_QT_V2_PACKAGE=ON." >&2
    exit 1
fi
if [ -z "${QMAKE:-}" ]; then
    QMAKE=$(command -v qmake6 || command -v qmake || true)
fi
export QMAKE
# AppImages normally mount themselves with FUSE, which containers and the GitHub runners
# lack (Ubuntu 24.04 has no libfuse2). Extracting works everywhere.
export APPIMAGE_EXTRACT_AND_RUN=1
# The plugin is looked up beside linuxdeploy and on PATH.
tools_dir=$(cd "$(dirname "${LINUXDEPLOY}")" && pwd -P)
export PATH="${tools_dir}:${PATH}"
# Shipped as well, so the package can be started without a display (the check below).
export EXTRA_PLATFORM_PLUGINS="libqoffscreen.so"

# Qt's own installation must not stand in for anything missing from the package when it is
# started below (install-qt-action, for one, sets QT_PLUGIN_PATH).
unset QT_PLUGIN_PATH QT_QPA_PLATFORM_PLUGIN_PATH QML2_IMPORT_PATH

stage=$(mktemp -d)
trap 'rm -rf "${stage}"' EXIT
# linuxdeploy names the icon after the file; the desktop file asks for "colorizingdmd".
cp "${here}/../../cRom256.png" "${stage}/colorizingdmd.png"
mkdir -p "${stage}/AppDir/usr/share/doc/colorizingdmd"
cp "${here}/../../LICENSE" "${stage}/AppDir/usr/share/doc/colorizingdmd/LICENSE"

# A build with DMD output: linuxdeploy finds libdmdutil through the build's rpath, and the
# libraries libdmdutil loads through this path (packaging/build-libdmdutil.sh stages them).
if [ -n "${DMDUTIL_LIB_DIR:-}" ]; then
    export LD_LIBRARY_PATH="$(cd "${DMDUTIL_LIB_DIR}" && pwd -P)${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
fi

(
    cd "${stage}"
    OUTPUT="${output}" "${LINUXDEPLOY}" \
        --appdir "${stage}/AppDir" \
        --executable "${executable}" \
        --desktop-file "${here}/linux/ColorizingDMD.desktop" \
        --icon-file "${stage}/colorizingdmd.png" \
        --plugin qt \
        --output appimage
)

# The app starts and keeps running. A missing library or plugin ends it at once. It runs from
# the extracted image, so that the process checked and stopped here is the app itself.
(cd "${stage}" && "${output}" --appimage-extract >/dev/null)
set +e
QT_QPA_PLATFORM=offscreen "${stage}/squashfs-root/AppRun" &
pid=$!
sleep 5
kill -0 "${pid}" 2>/dev/null
running=$?
kill "${pid}" 2>/dev/null
wait "${pid}" 2>/dev/null
set -e
if [ "${running}" -ne 0 ]; then
    echo "The AppImage did not keep running." >&2
    exit 1
fi
echo "Wrote ${output}"
