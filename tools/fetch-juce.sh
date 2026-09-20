#!/usr/bin/env bash
# Clone the pinned JUCE version into external/JUCE (idempotent).
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
tag=8.0.9
commit=f72bad64d29715216226685810c5196bd0d79d77
dst="$here/external/JUCE"
if [ -d "$dst/.git" ]; then
    actual="$(git -C "$dst" rev-parse HEAD)"
    if [ "$actual" = "$commit" ]; then echo "JUCE already at $commit"; exit 0; fi
    echo "JUCE at $actual, expected $commit" >&2; exit 1
fi
if [ -f "$dst/.kcf-pinned-commit" ]; then     # extracted source archive (tools/package.sh): no .git
    actual="$(cat "$dst/.kcf-pinned-commit")"
    if [ "$actual" = "$commit" ] && [ -f "$dst/CMakeLists.txt" ]; then echo "JUCE pinned tree $commit present (archive)"; exit 0; fi
    echo "JUCE archive marker $actual, expected $commit" >&2; exit 1
fi
git clone --depth 1 --branch "$tag" https://github.com/juce-framework/JUCE.git "$dst"
actual="$(git -C "$dst" rev-parse HEAD)"
[ "$actual" = "$commit" ] || { echo "unexpected JUCE commit $actual" >&2; exit 1; }
echo "JUCE $tag ($commit) ready"
