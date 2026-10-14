#!/usr/bin/env bash
# Packs ColorizingDMD.exe into a zip with everything it loads: Qt's DLLs and plugins through
# windeployqt, then every other DLL of the MSYS2 prefix that the exe, Qt or a plugin needs (the
# GCC runtime, ICU, zstd, freetype, harfbuzz, ...). Checks that nothing outside the folder and
# Windows itself is left to load, and that the app starts.
#
# Usage, in an MSYS2 UCRT64 shell: package-windows.sh <build dir> <output .zip>
#
# The build must be configured with -DCOLORIZINGDMD_QT_V2_PACKAGE=ON. Needs the MSYS2 packages
# qt6-base (windeployqt6), ntldd and zip of the UCRT64 environment.

set -euo pipefail

if [ "$#" -ne 2 ]; then
    echo "Usage: $0 <build dir> <output .zip>" >&2
    exit 2
fi
if [ -z "${MINGW_PREFIX:-}" ]; then
    echo "Run this in an MSYS2 MinGW shell (UCRT64)." >&2
    exit 1
fi

build_dir=$1
output=$(cd "$(dirname "$2")" && pwd -P)/$(basename "$2")
exe="${build_dir}/app/ColorizingDMD.exe"
prefix_bin="${MINGW_PREFIX}/bin"

if [ ! -f "${exe}" ]; then
    echo "No ${exe}: configure the build with -DCOLORIZINGDMD_QT_V2_PACKAGE=ON." >&2
    exit 1
fi

# Qt's own installation must not stand in for anything missing from the package when it is
# started below (install-qt-action, for one, sets QT_PLUGIN_PATH).
unset QT_PLUGIN_PATH QT_QPA_PLATFORM_PLUGIN_PATH QML2_IMPORT_PATH

stage=$(mktemp -d)
trap 'rm -rf "${stage}"' EXIT
package="${stage}/ColorizingDMD"
mkdir -p "${package}"
cp "${exe}" "${package}/"

windeployqt6 --release --no-translations --no-system-d3d-compiler --no-opengl-sw \
    "${package}/ColorizingDMD.exe"

# Prints, for every exe and DLL in the folder, the DLLs it loads that resolve into the MSYS2
# prefix. ntldd reads the import tables; it starts nothing.
prefix_dependencies() {
    local prefix_lower
    prefix_lower=$(echo "${prefix_bin}/" | tr '[:upper:]' '[:lower:]')
    find "${package}" -type f \( -iname '*.exe' -o -iname '*.dll' \) -print0 |
        xargs -0 ntldd -R 2>/dev/null |
        awk '$2 == "=>" && $3 != "not" {print $3}' |
        while IFS= read -r path; do
            unix=$(cygpath -u "${path}")
            case "$(echo "${unix}" | tr '[:upper:]' '[:lower:]')" in
                "${prefix_lower}"*) echo "${unix}" ;;
            esac
        done | sort -u
}

# A DLL copied in can need further ones, so repeat until nothing new turns up.
while :; do
    added=0
    while IFS= read -r dll; do
        if [ ! -e "${package}/$(basename "${dll}")" ]; then
            cp "${dll}" "${package}/"
            added=1
        fi
    done < <(prefix_dependencies)
    [ "${added}" -eq 1 ] || break
done

# With the MSYS2 prefix off PATH, everything the exe loads must resolve inside the folder or in
# Windows. (Plugins are not checked this way: ntldd would not look for their Qt DLLs in the
# folder above them. The start below loads the platform plugin.)
system_path="$(cygpath -u "${SYSTEMROOT}")/System32:$(cygpath -u "${SYSTEMROOT}")"
ntldd_bin=$(command -v ntldd)
timeout_bin=$(command -v timeout)
# API set names (api-ms-*, ext-ms-*) are resolved by the loader, not found as files.
missing=$(cd "${package}" && PATH="${system_path}" "${ntldd_bin}" -R ColorizingDMD.exe 2>&1 |
    grep -i "not found" | grep -viE '^[[:space:]]*(api|ext)-ms-' | sort -u || true)
if [ -n "${missing}" ]; then
    echo "The package lacks DLLs:" >&2
    echo "${missing}" >&2
    exit 1
fi

# The app starts and keeps running, without the MSYS2 prefix on PATH. The offscreen platform
# plugin is put into a copy only for this check; the package itself has the Windows one. A
# missing DLL or plugin ends the app at once; 124 is timeout's status for "still running".
check="${stage}/check"
cp -R "${package}" "${check}"
plugins=$(qtpaths6 --query QT_INSTALL_PLUGINS)
cp "$(cygpath -u "${plugins}")/platforms/qoffscreen.dll" "${check}/platforms/"
set +e
(cd "${check}" && PATH="${system_path}" QT_QPA_PLATFORM=offscreen "${timeout_bin}" 10 ./ColorizingDMD.exe)
status=$?
set -e
if [ "${status}" -ne 124 ]; then
    echo "The packaged app did not keep running (exit status ${status})." >&2
    exit 1
fi

rm -f "${output}"
(cd "${stage}" && zip -qr "${output}" ColorizingDMD)
echo "Wrote ${output}"
