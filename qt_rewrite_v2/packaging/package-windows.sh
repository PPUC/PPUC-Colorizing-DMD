#!/usr/bin/env bash
# Packs ColorizingDMD.exe into a zip with everything it loads: Qt's DLLs and plugins through
# windeployqt, then every other DLL of the MSYS2 prefix that the exe, Qt or a plugin needs (the
# GCC runtime, ICU, zstd, freetype, harfbuzz, ...). Checks that nothing outside the folder and
# Windows itself is left to load, and that the app starts.
#
# Usage, in an MSYS2 UCRT64 shell: package-windows.sh <build dir> <output .zip>
#
# The build must be configured with -DCOLORIZINGDMD_QT_V2_PACKAGE=ON. Needs the MSYS2 packages
# qt6-base (windeployqt6), ntldd and zip of the UCRT64 environment. The VPX plugins the build
# made (<build dir>/vpx-plugins: DMDUtil with libdmdutil and the DLLs it loads) go into
# plugins\ beside the exe, where the app looks for them.

set -euo pipefail

if [ "$#" -ne 2 ]; then
    echo "Usage: $0 <build dir> <output .zip>" >&2
    exit 2
fi
if [ -z "${MINGW_PREFIX:-}" ]; then
    echo "Run this in an MSYS2 MinGW shell (UCRT64)." >&2
    exit 1
fi

here=$(cd "$(dirname "$0")" && pwd -P)
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
cp "${here}/../../LICENSE" "${package}/LICENSE.txt"

windeployqt6 --release --no-translations --no-system-d3d-compiler --no-opengl-sw \
    "${package}/ColorizingDMD.exe"

# The VPX plugins, each in its folder with the DLLs it loads; the plugin loader adds that
# folder to the DLL search path. The GCC runtime they need is found beside the exe.
vpx_plugins="${build_dir}/vpx-plugins"
if [ -d "${vpx_plugins}" ]; then
    mkdir -p "${package}/plugins"
    cp -R "${vpx_plugins}/." "${package}/plugins/"
fi

# ntldd of one file, with the file's own folder searched first (a VPX plugin's DLLs lie beside
# it), and limited in time: ntldd can hang on a DLL whose imports it cannot resolve, which
# would stop the package without a word. Extra arguments go to ntldd.
ntldd_of() {
    local file=$1 dir status
    shift
    dir=$(dirname "${file}")
    status=0
    PATH="${dir}:${PATH}" timeout 120 ntldd "$@" "${file}" 2>/dev/null || status=$?
    if [ "${status}" -eq 124 ]; then
        echo "ntldd did not finish for ${file#"${package}"/}" >&2
    fi
    return 0
}

# Prints, for every exe and DLL in the folder, the DLLs it loads that resolve into the MSYS2
# prefix. ntldd reads the import tables; it starts nothing.
prefix_dependencies() {
    local prefix_lower
    prefix_lower=$(echo "${prefix_bin}/" | tr '[:upper:]' '[:lower:]')
    # One file at a time, past failures: xargs would stop at the first one (status 255).
    find "${package}" -type f \( -iname '*.exe' -o -iname '*.dll' \) -print0 |
        while IFS= read -r -d '' file; do
            ntldd_of "${file}" -R </dev/null
        done |
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

# Every DLL a shipped file imports must be in the folder or a Windows system DLL. Only the
# shipped files are checked, not what system DLLs import in turn: those pull in optional
# components a given Windows installation may lack, which the loader never asks for here.
system_dirs=("$(cygpath -u "${SYSTEMROOT}")/System32" "$(cygpath -u "${SYSTEMROOT}")")
system_path="${system_dirs[0]}:${system_dirs[1]}"
timeout_bin=$(command -v timeout)
is_available() {
    local name=$1 own_dir=$2 dir
    # API set names (api-ms-*, ext-ms-*) are resolved by the loader, not found as files.
    case "${name,,}" in api-ms-* | ext-ms-*) return 0 ;; esac
    [ -n "$(find "${package}" -maxdepth 1 -iname "${name}" -print -quit)" ] && return 0
    # A plugin's DLLs lie in its own folder.
    [ -n "$(find "${own_dir}" -maxdepth 1 -iname "${name}" -print -quit)" ] && return 0
    for dir in "${system_dirs[@]}"; do
        [ -n "$(find "${dir}" -maxdepth 1 -iname "${name}" -print -quit 2>/dev/null)" ] && return 0
    done
    return 1
}
missing=""
while IFS= read -r -d '' file; do
    while IFS= read -r name; do
        [ -n "${name}" ] || continue
        is_available "${name}" "$(dirname "${file}")" || missing+="${name} (needed by ${file#"${package}"/})"$'\n'
    done < <(ntldd_of "${file}" </dev/null | awk '{print $1}')
done < <(find "${package}" -type f \( -iname '*.exe' -o -iname '*.dll' \) -print0)
missing=$(printf '%s' "${missing}" | sort -u)
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
# Every VPX plugin of the package loads, with the DLLs shipped beside it.
if [ -d "${vpx_plugins}" ]; then
    (cd "${check}" && PATH="${system_path}" QT_QPA_PLATFORM=offscreen \
        "${timeout_bin}" 60 ./ColorizingDMD.exe --check-plugins)
fi

rm -f "${output}"
(cd "${stage}" && zip -qr "${output}" ColorizingDMD)
echo "Wrote ${output}"
