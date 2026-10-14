#!/usr/bin/env bash
# Packs ColorizingDMD.app into a zip: Qt frameworks and plugins deployed with macdeployqt,
# ad-hoc signed (no Developer ID, not notarised), checked for libraries loaded from outside the
# bundle, and started once.
#
# Usage: package-macos.sh <build dir> <output .zip>
#
# The build must be configured with -DCOLORIZINGDMD_QT_V2_PACKAGE=ON. macdeployqt must belong to
# the Qt the build used: it is taken from MACDEPLOYQT, or from PATH. The VPX plugins the build
# made (<build dir>/vpx-plugins: DMDUtil with libdmdutil and the libraries it loads) go into
# Contents/PlugIns/vpx, where the app looks for them.

set -euo pipefail

if [ "$#" -ne 2 ]; then
    echo "Usage: $0 <build dir> <output .zip>" >&2
    exit 2
fi

here=$(cd "$(dirname "$0")" && pwd -P)
build_dir=$1
output=$(cd "$(dirname "$2")" && pwd -P)/$(basename "$2")
app="${build_dir}/app/ColorizingDMD.app"
macdeployqt=${MACDEPLOYQT:-macdeployqt}

if [ ! -d "${app}" ]; then
    echo "No ${app}: configure the build with -DCOLORIZINGDMD_QT_V2_PACKAGE=ON." >&2
    exit 1
fi

# Qt's own installation must not stand in for anything missing from the package when it is
# started below (install-qt-action, for one, sets QT_PLUGIN_PATH).
unset QT_PLUGIN_PATH QT_QPA_PLATFORM_PLUGIN_PATH QML2_IMPORT_PATH

stage=$(mktemp -d)
trap 'rm -rf "${stage}"' EXIT
cp -R "${app}" "${stage}/"
bundle="${stage}/ColorizingDMD.app"
mkdir -p "${bundle}/Contents/Resources"
cp "${here}/../../LICENSE" "${bundle}/Contents/Resources/LICENSE"

"${macdeployqt}" "${bundle}" -verbose=1
# After macdeployqt, which deploys Qt's plugins only. -P keeps the symbolic links, -X leaves
# the extended attributes of the build behind (they would end up as __MACOSX entries in the
# zip). Each plugin finds the libraries beside it through its @loader_path rpath.
vpx_plugins="${build_dir}/vpx-plugins"
if [ -d "${vpx_plugins}" ]; then
    mkdir -p "${bundle}/Contents/PlugIns/vpx"
    cp -R -P -X "${vpx_plugins}/." "${bundle}/Contents/PlugIns/vpx/"
fi
# Without a signature an arm64 binary does not start at all; ad-hoc is enough for that.
codesign --force --deep --sign - "${bundle}"
codesign --verify --deep --strict "${bundle}"

# Everything the app and its plugins load has to be inside the bundle or part of macOS.
leaks=$(find "${bundle}/Contents" -type f \( -perm -u+x -o -name '*.dylib' \) -print0 |
    xargs -0 otool -L 2>/dev/null |
    awk '/^\t/ && $1 !~ /^(@rpath|@executable_path|@loader_path|\/System\/|\/usr\/lib\/)/ {print $1}' |
    sort -u)
if [ -n "${leaks}" ]; then
    echo "The bundle loads libraries from outside itself:" >&2
    echo "${leaks}" >&2
    exit 1
fi

# The app starts and keeps running. A missing framework or plugin ends it at once. It runs
# without a display on the offscreen platform plugin, which is put into a copy of the bundle
# only for this check (the package has the Cocoa one).
qtpaths="$(dirname "$(command -v "${macdeployqt}")")/qtpaths"
check="${stage}/check/ColorizingDMD.app"
mkdir -p "$(dirname "${check}")"
cp -R "${bundle}" "${check}"
cp "$("${qtpaths}" --query QT_INSTALL_PLUGINS)/platforms/libqoffscreen.dylib" \
    "${check}/Contents/PlugIns/platforms/"
codesign --force --deep --sign - "${check}"
set +e
QT_QPA_PLATFORM=offscreen "${check}/Contents/MacOS/ColorizingDMD" &
pid=$!
sleep 5
kill -0 "${pid}" 2>/dev/null
running=$?
kill "${pid}" 2>/dev/null
wait "${pid}" 2>/dev/null
set -e
if [ "${running}" -ne 0 ]; then
    echo "The packaged app did not keep running." >&2
    exit 1
fi
# Every VPX plugin of the package loads, with the libraries shipped beside it.
if [ -d "${vpx_plugins}" ]; then
    QT_QPA_PLATFORM=offscreen "${check}/Contents/MacOS/ColorizingDMD" --check-plugins
fi

rm -f "${output}"
# Without the extended attributes (a build machine's com.apple.provenance and the like would
# become __MACOSX entries); the signatures are inside the binaries.
ditto -c -k --norsrc --keepParent "${bundle}" "${output}"
echo "Wrote ${output}"
