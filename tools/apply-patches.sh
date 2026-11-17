#!/bin/bash
#
# Apply the droidlet source patches to a freshly synced AOSP tree.
#
# The patches are standard `git format-patch` series, one directory per
# project, and are applied with `git am -3`.  Meson files are intentionally
# not part of these patches; they are linked from build/aosp by
# link-aosp-meson.sh.
#
#   repo sync -c -j8 --no-tags
#   droidlet/tools/apply-patches.sh
#   droidlet/tools/link-aosp-meson.sh

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
DROIDLET_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)
WORKSPACE=$(cd "$DROIDLET_ROOT/.." && pwd)
PATCH_ROOT="$DROIDLET_ROOT/patches"

if [ ! -d "$PATCH_ROOT" ]; then
    echo "error: $PATCH_ROOT not found" >&2
    exit 1
fi

while IFS= read -r patch_dir; do
    project=${patch_dir#"$PATCH_ROOT"/}
    repo="$WORKSPACE/$project"

    if [ ! -d "$repo" ]; then
        echo "warning: $project is not present; skipping" >&2
        continue
    fi

    if [ -n "$(git -C "$repo" status --porcelain --untracked-files=no)" ]; then
        echo "error: $project has local changes; apply patches to a clean checkout" >&2
        exit 1
    fi

    shopt -s nullglob
    patches=("$patch_dir"/*.patch)
    shopt -u nullglob
    if [ ${#patches[@]} -eq 0 ]; then
        echo "warning: no patches in $patch_dir" >&2
        continue
    fi

    echo "== $project"
    if ! git -C "$repo" am -3 "${patches[@]}"; then
        echo "error: patch series failed in $project" >&2
        echo "       inspect with: git -C $repo am --show-current-patch" >&2
        echo "       cancel with:  git -C $repo am --abort" >&2
        exit 1
    fi
done < <(find "$PATCH_ROOT" -name '*.patch' -printf '%h\n' | sort -u)

echo "droidlet patches applied"
