#!/bin/bash
#
# Link the droidlet-owned meson build files into the AOSP source trees.
#
# The real files live in droidlet/ (the root meson.build) and
# droidlet/build/aosp/ (the per-project fragments).  This script creates
# relative symlinks at the paths meson expects, so the AOSP trees can stay
# pristine apart from the source patches.
#
# Run this after `repo sync` and after applying the source patches.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
DROIDLET_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)
WORKSPACE=$(cd "$DROIDLET_ROOT/.." && pwd)
BUILD_ROOT="$DROIDLET_ROOT/build/aosp"

ignore_untracked_meson() {
    local repo=$1
    local exclude

    exclude=$(git -C "$repo" rev-parse --git-path info/exclude 2>/dev/null) || return 0
    case "$exclude" in
        /*) ;;
        *) exclude="$repo/$exclude" ;;
    esac

    mkdir -p "$(dirname "$exclude")"
    if ! grep -qxF 'meson.build' "$exclude" 2>/dev/null; then
        {
            echo ''
            echo '# droidlet: generated meson symlinks'
            echo 'meson.build'
        } >>"$exclude"
    fi
}

link_file() {
    local src=$1
    local dst=$2
    local dir
    dir=$(dirname "$dst")
    mkdir -p "$dir"

    if [ ! -f "$src" ]; then
        echo "missing source: $src" >&2
        return 1
    fi

    local rel
    rel=$(realpath --relative-to="$dir" "$src")
    ln -sfn "$rel" "$dst"
    printf 'link %s -> %s\n' "${dst#"$WORKSPACE"/}" "$rel"
}

if [ ! -f "$DROIDLET_ROOT/meson.build" ]; then
    echo "missing $DROIDLET_ROOT/meson.build" >&2
    exit 1
fi

link_file "$DROIDLET_ROOT/meson.build" "$WORKSPACE/meson.build"
if [ -f "$DROIDLET_ROOT/meson.options" ]; then
    link_file "$DROIDLET_ROOT/meson.options" "$WORKSPACE/meson.options"
fi

if [ -f "$WORKSPACE/.repo/project.list" ]; then
    while IFS= read -r project; do
        [ -n "$project" ] || continue

        [ -d "$WORKSPACE/$project" ] || continue
        ignore_untracked_meson "$WORKSPACE/$project"

        project_build_root="$BUILD_ROOT/$project"
        [ -d "$project_build_root" ] || continue

        while IFS= read -r -d '' src; do
            relpath=${src#"$project_build_root"/}
            link_file "$src" "$WORKSPACE/$project/$relpath"
        done < <(find "$project_build_root" -name meson.build -type f -print0)
    done < "$WORKSPACE/.repo/project.list"
else
    echo "warning: $WORKSPACE/.repo/project.list not found; skipping project meson files" >&2
fi

rm $WORKSPACE/droidlet/.git/info/exclude

echo "meson files linked"
