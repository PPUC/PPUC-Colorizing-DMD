#!/bin/sh
# UI regression dump for qt_rewrite_v2/app/MainWindow.cpp, see README.md.
#   run.sh baseline [REV]   export REV (default HEAD) with git archive and dump it -> before.txt
#   run.sh after [ARGS]     dump the working tree -> after.txt, then compare with before.txt
#   run.sh compare [ARGS]   compare before.txt and after.txt again
# ARGS go to compare.py, e.g. --allow-removed '^\[some tag\] '.
# Builds and output go to $UI_DUMP_WORK_DIR (default: ${TMPDIR:-/tmp}/colorizingdmd_ui_dump).
# libserum is taken from $COLORIZINGDMD_LIBSERUM_DIR (default: next to the repository).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
TREE=$(cd "$HERE/../.." && pwd)
WORK=${UI_DUMP_WORK_DIR:-${TMPDIR:-/tmp}/colorizingdmd_ui_dump}
case "$WORK" in
/*) ;;
*) WORK=$PWD/$WORK ;;
esac
case "$WORK/" in
"$TREE"/*)
    echo "UI_DUMP_WORK_DIR must be outside $TREE" >&2
    exit 2
    ;;
esac
mkdir -p "$WORK"
# An exported baseline tree has no libserum beside it, so the checkout is named explicitly.
LIBSERUM=${COLORIZINGDMD_LIBSERUM_DIR:-$(cd "$TREE/../.." && pwd)/libserum}

# build NAME TREE: configure and build the dump executable for a qt_rewrite_v2 tree.
build() {
    if ! { cmake -S "$HERE" -B "$WORK/build_$1" -DUI_DUMP_TREE="$2" \
               -DCOLORIZINGDMD_LIBSERUM_DIR="$LIBSERUM" &&
           cmake --build "$WORK/build_$1" --parallel; } > "$WORK/build_$1.log" 2>&1; then
        echo "$1 build failed, see $WORK/build_$1.log:" >&2
        grep -E "error|FAILED" -A6 "$WORK/build_$1.log" >&2 || tail -20 "$WORK/build_$1.log" >&2
        exit 1
    fi
}

# dump NAME OUT: run the dump executable of build NAME.
dump() {
    if ! (cd "$WORK" && QT_QPA_PLATFORM=offscreen "./build_$1/colorizing_qt_v2_ui_dump") \
        > "$WORK/$2.txt" 2> "$WORK/$2.err"; then
        echo "$1 dump failed:" >&2
        grep -v propagateSizeHints "$WORK/$2.err" >&2
        exit 1
    fi
}

case "$1" in
baseline)
    REV=${2:-HEAD}
    REPO_ROOT=$(git -C "$TREE" rev-parse --show-toplevel)
    PREFIX=$(git -C "$TREE" rev-parse --show-prefix)
    PREFIX=${PREFIX%/}
    rm -rf "$WORK/baseline_tree" "$WORK/build_baseline"
    mkdir "$WORK/baseline_tree"
    git -C "$REPO_ROOT" archive "$REV" "$PREFIX" | tar -x -C "$WORK/baseline_tree"
    build baseline "$WORK/baseline_tree/$PREFIX"
    dump baseline before
    dump baseline before2
    if cmp -s "$WORK/before.txt" "$WORK/before2.txt"; then
        echo "baseline of $REV is deterministic: $(grep -c '^=====' "$WORK/before.txt") sections"
        echo "written to $WORK/before.txt"
    else
        echo "baseline is not deterministic: before.txt and before2.txt in $WORK differ" >&2
        exit 1
    fi
    ;;
after)
    shift
    [ -f "$WORK/before.txt" ] || { echo "no baseline, run '$0 baseline' first" >&2; exit 2; }
    build after "$TREE"
    dump after after
    python3 "$HERE/compare.py" "$WORK/before.txt" "$WORK/after.txt" "$@"
    ;;
compare)
    shift
    python3 "$HERE/compare.py" "$WORK/before.txt" "$WORK/after.txt" "$@"
    ;;
*)
    sed -n 2,7p "$0"
    exit 2
    ;;
esac
