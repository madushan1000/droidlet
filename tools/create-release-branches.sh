#!/bin/bash
#
# Create the distributable droidlet17-release branch in every AOSP project.
#
# The branch starts at the AOSP release baseline and receives every change
# from the local `droidlet` branch except meson.build files.  Meson lives in
# the droidlet repository and is linked into place by link-aosp-meson.sh.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
DROIDLET_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)
WORKSPACE=$(cd "$DROIDLET_ROOT/.." && pwd)

if [ ! -f "$WORKSPACE/.repo/project.list" ]; then
    echo "error: $WORKSPACE/.repo/project.list not found" >&2
    exit 1
fi

while IFS= read -r project; do
    [ -n "$project" ] || continue

    repo="$WORKSPACE/$project"
    if [ ! -d "$repo/.git" ] && [ ! -f "$repo/.git" ]; then
        echo "warning: $project is not a git checkout; skipping" >&2
        continue
    fi

    if ! git -C "$repo" rev-parse --verify --quiet aosp/android17-release >/dev/null; then
        echo "warning: $project has no aosp/android17-release; skipping" >&2
        continue
    fi

    if git -C "$repo" rev-parse --verify --quiet refs/heads/droidlet17-release >/dev/null; then
        echo "== $project: droidlet17-release already exists; skipping"
        continue
    fi

    echo "== $project"
    git -C "$repo" checkout -q -b droidlet17-release aosp/android17-release

    tmp=$(mktemp)
    excludes=(':(exclude)*meson.build')
    if [ "$project" = "art" ]; then
        # The duplicate prebuilt aidl binary is not part of the host patch.
        excludes+=(':(exclude)aidl')
    fi
    if ! git -C "$repo" diff --binary aosp/android17-release..droidlet -- . \
            "${excludes[@]}" >"$tmp"; then
        echo "error: could not diff $project" >&2
        rm -f "$tmp"
        exit 1
    fi

    if [ -s "$tmp" ]; then
        if ! git -C "$repo" apply --check "$tmp"; then
            echo "error: patch does not apply cleanly in $project" >&2
            rm -f "$tmp"
            exit 1
        fi
        git -C "$repo" apply --index "$tmp"
        git -C "$repo" commit -q -m "droidlet: host port for android17-release"
        echo "   committed $(git -C "$repo" rev-parse --short HEAD)"
    else
        echo "   no non-meson changes"
    fi
    rm -f "$tmp"
done < "$WORKSPACE/.repo/project.list"

echo "droidlet17-release branches ready"
