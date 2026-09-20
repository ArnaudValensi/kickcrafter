#!/usr/bin/env bash
# Pack what build/ holds into the per-platform archive under artifacts/dist/ (no publication):
#   kickcrafter-fable-<version>-linux-x86_64.tar.gz    KickCrafter Fable.vst3/
#   kickcrafter-fable-<version>-macos-universal.zip    KickCrafter Fable.vst3/ and KickCrafter Fable.component/
#   kickcrafter-fable-<version>-windows-x86_64.zip     KickCrafter Fable.vst3/
# Every archive also holds README.md, CHANGELOG.md, LICENSE, THIRD_PARTY_NOTICES.md, INSTALL.txt
# (the platform's install path) and licenses/ (the full licence text of every shipped third-party
# component; the list lives here and nowhere else). The version is project(... VERSION ...) of
# CMakeLists.txt; a CI build that is not on a tag appends -<short git sha> so two pushes never
# produce the same file name.
#
# usage: tools/dist.sh                        stage build/ and archive it into artifacts/dist/ (./run dist, CI)
#        tools/dist.sh name                   print the archive base name (kickcrafter-fable-<version>-<platform>)
#        tools/dist.sh version                print <version> (with the CI sha suffix when it applies)
#        tools/dist.sh stage <dir> [<from>]   fill <dir> with the bundles found under <from> and the documents
#                                             (<from> defaults to build/KickCrafterFable_artefacts/Release)
#        tools/dist.sh archive <dir> <out>    archive a staged <dir> into <out>/<basename of dir>.<tar.gz|zip>
# tools/package.sh uses stage and archive so it can add BUILD-RECORD.txt and its validation lines.
#
# Runs on Linux, on macOS with its bash 3.2 and on Git Bash under Windows: no bash 4 constructs, no
# sha256sum called directly (the sha256 helper), no realpath or readlink -f.
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
cd "$here"

sha256() { if command -v sha256sum >/dev/null 2>&1; then sha256sum "$@"; else shasum -a 256 "$@"; fi; }
die() { echo "dist: $*" >&2; exit 1; }

case "$(uname -s)" in
    Linux)        platform="linux-$(uname -m)";   archive_format=tar.gz; platform_label="Linux $(uname -m) VST3" ;;
    Darwin)       platform="macos-universal";     archive_format=zip;    platform_label="macOS universal (Apple Silicon and Intel) VST3 and AU" ;;
    MINGW*|MSYS*) platform="windows-$(uname -m)"; archive_format=zip;    platform_label="Windows $(uname -m) VST3" ;;
    *) die "unsupported platform: $(uname -s)" ;;
esac
version="$(sed -n 's/^project(.*VERSION \([0-9][0-9.]*\).*/\1/p' CMakeLists.txt | head -1)"
[ -n "$version" ] || die "no project(... VERSION ...) in CMakeLists.txt"
rev="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
if [ "${GITHUB_ACTIONS:-}" = "true" ] && [ "${GITHUB_REF_TYPE:-}" != "tag" ]; then
    version="$version-$rev"
fi
name="kickcrafter-fable-$version-$platform"

# The licence texts of every shipped third-party component (THIRD_PARTY_NOTICES.md names them).
copy_licences() {   # copy_licences <licenses dir>
    local L="$1" J=external/JUCE/modules
    cp resources/fonts/IBM-Plex-OFL.txt "$L/IBM-Plex-OFL.txt"
    cp external/JUCE/LICENSE.md "$L/JUCE-LICENSE.md"
    cp "$J/juce_audio_processors/format_types/VST3_SDK/LICENSE.txt" "$L/VST3-SDK-LICENSE.txt"
    cp "$J/juce_graphics/fonts/harfbuzz/COPYING" "$L/HarfBuzz-COPYING.txt"
    cp "$J/juce_graphics/unicode/sheenbidi/LICENSE" "$L/SheenBidi-LICENSE.txt"
    cp "$J/juce_core/zip/zlib/README" "$L/zlib-README.txt"
    cp "$J/juce_graphics/image_formats/pnglib/LICENSE" "$L/pnglib-LICENSE.txt"
    cp "$J/juce_graphics/image_formats/jpglib/README" "$L/jpglib-README.txt"
    cp "$J/juce_audio_formats/codecs/flac/Flac Licence.txt" "$L/FLAC-Licence.txt"
    cp "$J/juce_audio_formats/codecs/oggvorbis/Ogg Vorbis Licence.txt" "$L/OggVorbis-Licence.txt"
    cp "$J/juce_audio_processors/format_types/LV2_SDK/lv2/COPYING" "$L/LV2-COPYING.txt"
}

install_text() {
    echo "KickCrafter Fable $version, $platform_label. Built from commit $rev."
    case "$platform" in
        linux-*)
            echo 'Copy "KickCrafter Fable.vst3" into a VST3 folder your host scans (for example ~/.vst3/) and rescan plug-ins.' ;;
        macos-*)
            echo 'Copy "KickCrafter Fable.vst3" into ~/Library/Audio/Plug-Ins/VST3/ and "KickCrafter Fable.component" into'
            echo '~/Library/Audio/Plug-Ins/Components/, then rescan plug-ins (Logic and GarageBand load the AU, most other hosts the VST3).'
            # tools/macos-sign.sh rewrites this line when it signs and notarizes a release.
            echo 'Signing: not signed. macOS refuses a downloaded unsigned bundle until its quarantine attribute is removed:'
            echo '  xattr -dr com.apple.quarantine "<the copied bundle>"' ;;
        windows-*)
            echo 'Copy "KickCrafter Fable.vst3" into C:\Program Files\Common Files\VST3\ and rescan plug-ins.' ;;
    esac
    echo "Licence: AGPL-3.0-or-later (LICENSE). Third-party components: THIRD_PARTY_NOTICES.md and licenses/."
}

stage() {   # stage <dir> [<from>]: the bundle(s) built under <from>, the documents, the licences, INSTALL.txt
    local dir="$1" from="${2:-build/KickCrafterFable_artefacts/Release}" b
    [ -d "$from" ] || die "nothing built under $from"
    mkdir -p "$dir/licenses"
    # The bundles sit under a format folder in build/ and directly under artifacts/vst3 (package.sh).
    for b in "$from/VST3/KickCrafter Fable.vst3" "$from/KickCrafter Fable.vst3" \
             "$from/AU/KickCrafter Fable.component" "$from/KickCrafter Fable.component"; do
        [ -d "$b" ] || continue
        cp -R "$b" "$dir/"
        echo "staged: $b"
    done
    # Every platform ships the VST3; macOS ships the AU component as well. A partial build refuses.
    [ -d "$dir/KickCrafter Fable.vst3" ] || die "no KickCrafter Fable.vst3 under $from"
    case "$platform" in macos-*) [ -d "$dir/KickCrafter Fable.component" ] || die "no KickCrafter Fable.component under $from" ;; esac
    cp README.md CHANGELOG.md LICENSE THIRD_PARTY_NOTICES.md "$dir/"
    copy_licences "$dir/licenses"
    install_text > "$dir/INSTALL.txt"
}

archive() {   # archive <staged dir> <out dir>: <out>/<basename>.tar.gz on Linux, .zip elsewhere; prints the path
    local dir="$1" out="$2" base parent file
    base="$(basename "$dir")"
    parent="$(cd "$(dirname "$dir")" && pwd)"
    mkdir -p "$out"
    file="$out/$base.$archive_format"
    rm -f "$file" "$parent/$base.$archive_format"
    # Archivers run inside the parent with relative paths: no absolute path crosses a Git Bash
    # boundary into a native Windows program.
    case "$platform" in
        linux-*)   tar -C "$parent" -czf "$file" "$base" ;;
        macos-*)   ( cd "$parent" && ditto -c -k --keepParent "$base" "$base.zip" ) && mv "$parent/$base.zip" "$file" ;;   # Apple's archiver for bundles
        windows-*)
            if command -v 7z >/dev/null 2>&1; then ( cd "$parent" && 7z a -tzip -r "$base.zip" "$base" >/dev/null )
            elif command -v zip >/dev/null 2>&1; then ( cd "$parent" && zip -q -r "$base.zip" "$base" )
            else die "neither 7z nor zip is available"; fi
            mv "$parent/$base.zip" "$file" ;;
    esac
    echo "$file"
}

listing() {   # listing <archive>
    case "$1" in
        *.tar.gz) tar -tzf "$1" ;;
        *.zip) if command -v unzip >/dev/null 2>&1; then unzip -Z1 "$1"; elif command -v 7z >/dev/null 2>&1; then 7z l -ba "$1" | awk '{print $NF}'; else echo "(no zip lister on this machine)"; fi ;;
    esac
}

case "${1:-}" in
    "")
        out="artifacts/dist"
        stagedir="$(mktemp -d)"; trap 'rm -rf "$stagedir"' EXIT
        stage "$stagedir/$name"
        file="$(archive "$stagedir/$name" "$out")"
        echo "archive: $file"
        listing "$file" | sed 's/^/  /'
        sha256 "$file" ;;
    name)    echo "$name" ;;
    version) echo "$version" ;;
    stage)   [ $# -ge 2 ] || die "usage: dist.sh stage <dir> [<from>]"; stage "$2" "${3:-build/KickCrafterFable_artefacts/Release}" ;;
    archive) [ $# -eq 3 ] || die "usage: dist.sh archive <staged dir> <out dir>"; archive "$2" "$3" ;;
    *) die "unknown command: $1 (see the header of tools/dist.sh)" ;;
esac
