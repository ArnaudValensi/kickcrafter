#!/usr/bin/env bash
# Build the distributable archives of the validated Linux build (no publication):
#   1. binary archive: the staged VST3 bundle packed by tools/dist.sh (documents, INSTALL.txt and the
#      full licence texts of every shipped third-party component) plus BUILD-RECORD.txt
#   2. source archive: production sources + docs from HEAD (no artifacts/) and the exact
#      pinned JUCE tree (no .git, no build caches), with a source manifest hash
#   3. evidence archive: logs, REAPER projects/renders/results, screenshots
# plus SHA256SUMS, all named kickcrafter-fable-<version>-... after the project version. Refuses to
# package when the production sources are not committed or when the staged binary does not
# correspond to the last recorded test run of these sources. Linux only (the validation gates).
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
cd "$here"
rev="$(git rev-parse --short HEAD)"
version="$(tools/dist.sh version)"
base="kickcrafter-fable-$version"
out="artifacts/dist"
bundle="artifacts/vst3/KickCrafter Fable.vst3"
[ -d "$bundle" ] || { echo "staged bundle missing: $bundle" >&2; exit 1; }
juce_commit="$(git -C external/JUCE rev-parse HEAD)"
[ "$juce_commit" = "f72bad64d29715216226685810c5196bd0d79d77" ] || { echo "unexpected JUCE commit $juce_commit" >&2; exit 1; }

# Delivery correspondence: committed sources+docs, build record bound to the
# staged binary and to a successful test run, clean validator run. Details: tools/package-preflight.sh.
tools/package-preflight.sh || exit 1
manifest="$(find engine plugin resources CMakeLists.txt -type f | LC_ALL=C sort | xargs sha256sum | sha256sum | cut -d' ' -f1)"
staged_sha="$(sha256sum "$bundle/Contents/x86_64-linux/KickCrafter Fable.so" | cut -d' ' -f1)"

rm -rf "$out"; mkdir -p "$out"
stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT

# 1. binary archive: staged by tools/dist.sh from the validated bundle (the documents, INSTALL.txt
#    and the licence list live there), plus the validation lines and the build record.
binname="$(tools/dist.sh name)"
bin="$stage/$binname"
tools/dist.sh stage "$bin" artifacts/vst3
cat >> "$bin/INSTALL.txt" <<EOF
Production source manifest $manifest; validated binary SHA-256 $staged_sha (BUILD-RECORD.txt).
Validation gates: docs/development.md in the source archive (logs in the separate evidence archive).
EOF
cp artifacts/logs/build-record.txt "$bin/BUILD-RECORD.txt"
binarchive="$(tools/dist.sh archive "$bin" "$out")"

# 2. source archive
src="$stage/$base-source"
mkdir -p "$src"
git archive --format=tar HEAD -- engine plugin tests tools resources docs run .github CMakeLists.txt README.md CHANGELOG.md LICENSE THIRD_PARTY_NOTICES.md .gitignore | tar -C "$src" -xf -
mkdir -p "$src/external"
git -C external/JUCE archive --format=tar --prefix=JUCE/ HEAD | tar -C "$src/external" -xf -
echo "$juce_commit" > "$src/external/JUCE/.kcf-pinned-commit"
echo "$manifest" > "$src/SOURCE-MANIFEST.sha256"
tar -C "$stage" -czf "$out/$base-source.tar.gz" "$(basename "$src")"

# 3. evidence archive
ev="$stage/$base-evidence"
mkdir -p "$ev/reaper"
cp -r artifacts/logs artifacts/screenshots "$ev/"
for f in artifacts/reaper/*.txt artifacts/reaper/*.rpp artifacts/reaper/*.wav artifacts/reaper/*.f32; do [ -f "$f" ] && cp "$f" "$ev/reaper/"; done
tar -C "$stage" -czf "$out/$base-evidence.tar.gz" "$(basename "$ev")"

( cd "$out" && sha256sum ./*.tar.gz > SHA256SUMS )
echo "validated binary: $staged_sha  KickCrafter Fable.vst3/Contents/x86_64-linux/KickCrafter Fable.so" >> "$out/SHA256SUMS"
echo "production source manifest: $manifest (git $rev)" >> "$out/SHA256SUMS"
echo "build record: $(grep '^build-record' artifacts/logs/build-record.txt)" >> "$out/SHA256SUMS"

# Verify the source archive from its extraction: documented offline JUCE path, no evidence inside.
verify="$(mktemp -d)"
tar -C "$verify" -xzf "$out/$base-source.tar.gz"
( cd "$verify/$base-source" && tools/fetch-juce.sh && [ ! -d artifacts ] && [ ! -d reviews ] && [ -f external/JUCE/CMakeLists.txt ] \
  && [ "$(find engine plugin resources CMakeLists.txt -type f | LC_ALL=C sort | xargs sha256sum | sha256sum | cut -d' ' -f1)" = "$manifest" ] ) \
    || { echo "source archive verification failed" >&2; rm -rf "$verify"; exit 1; }
echo "source archive verified: fetch path accepts the bundled pinned JUCE tree; no artifacts/reviews inside"
rm -rf "$verify"
tar -tzf "$binarchive" | grep -c "licenses/" | sed 's/^/licence files in the binary archive: /'
echo "packaged into $out ($rev, JUCE $juce_commit):"; ls -la "$out"; cat "$out/SHA256SUMS"
