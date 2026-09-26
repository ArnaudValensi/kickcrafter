#!/usr/bin/env bash
# Fetch the two host-independent validators under external/ (git-ignored), the same way on Linux,
# macOS (bash 3.2) and Git Bash on Windows. Idempotent: an existing executable is printed as is,
# so a CI cache of the directory skips the work.
#   tools/fetch-validators.sh validator    Steinberg VST3 SDK validator, built from the SDK at the
#                                          pinned tag into external/validator/validator[.exe]
#   tools/fetch-validators.sh pluginval    Tracktion pluginval, the pinned release's binary for
#                                          this platform, into external/pluginval/
#   tools/fetch-validators.sh editorhost   Steinberg's editorhost sample (the SDK's own editor
#                                          host, the closest thing to Cubase's window code), built
#                                          from the same SDK checkout into external/editorhost/;
#                                          macOS and Windows only (on Linux it needs gtkmm)
# Each prints the executable's path on stdout (everything else goes to stderr) and exits non-zero
# when it could not produce it. docs/development.md, "Setting up", sections 4 and 5.
set -uo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
cd "$here"

VST3SDK_REPO="https://github.com/steinbergmedia/vst3sdk.git"
VST3SDK_TAG="v3.8.1_build_84"          # VST 3.8.1, the generation the development machine validates with
PLUGINVAL_VERSION="v1.0.4"
PLUGINVAL_BASE="https://github.com/Tracktion/pluginval/releases/download/$PLUGINVAL_VERSION"

case "$(uname -s)" in
    Linux)        os=Linux;   exe="" ;;
    Darwin)       os=macOS;   exe="" ;;
    MINGW*|MSYS*) os=Windows; exe=".exe" ;;
    *) echo "unsupported platform: $(uname -s)" >&2; exit 2 ;;
esac
say() { echo "fetch-validators: $*" >&2; }

fetch_validator() {
    local out="external/validator/validator$exe"
    if [ -x "$out" ]; then echo "$out"; return 0; fi
    local sdk="external/vst3sdk"
    if [ ! -d "$sdk/.git" ]; then
        say "cloning $VST3SDK_REPO at $VST3SDK_TAG (shallow, with submodules)"
        git clone --quiet --depth 1 --branch "$VST3SDK_TAG" --recurse-submodules --shallow-submodules "$VST3SDK_REPO" "$sdk" >&2 || return 1
    fi
    # VSTGUI, the plug-in examples and the hosting examples are off: the validator uses none of
    # them, and their configure demands desktop development packages (xcb-cursor for VSTGUI,
    # gtkmm-3.0 for editorhost) a build machine need not have. The validator's own directory is
    # added by the SDK unconditionally, under public.sdk/samples/vst-hosting/validator.
    say "building the validator target (Ninja, Release)"
    cmake -S "$sdk" -B "$sdk/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DSMTG_ENABLE_VSTGUI_SUPPORT=OFF -DSMTG_ENABLE_VST3_PLUGIN_EXAMPLES=OFF \
        -DSMTG_ENABLE_VST3_HOSTING_EXAMPLES=OFF >&2 || return 1
    cmake --build "$sdk/build" --target validator >&2 || return 1
    local built
    built="$(find "$sdk/build/bin" -type f \( -name validator -o -name validator.exe \) 2>/dev/null | head -1)"
    [ -n "$built" ] || { say "no validator executable under $sdk/build/bin"; return 1; }
    mkdir -p external/validator && cp "$built" "$out" && chmod +x "$out" || return 1
    echo "$out"
}

fetch_editorhost() {
    local out
    case "$os" in
        Windows) out="external/editorhost/editorhost.exe" ;;
        macOS)   out="external/editorhost/editorhost.app/Contents/MacOS/editorhost" ;;
        Linux)   say "editorhost is not built on Linux (its window needs gtkmm-3.0); macOS and Windows only"; return 2 ;;
    esac
    if [ -x "$out" ]; then echo "$out"; return 0; fi
    local sdk="external/vst3sdk"
    if [ ! -d "$sdk/.git" ]; then
        say "cloning $VST3SDK_REPO at $VST3SDK_TAG (shallow, with submodules)"
        git clone --quiet --depth 1 --branch "$VST3SDK_TAG" --recurse-submodules --shallow-submodules "$VST3SDK_REPO" "$sdk" >&2 || return 1
    fi
    # Its own build tree: the hosting examples are on here and off for the validator.
    say "building the editorhost target (Ninja, Release)"
    cmake -S "$sdk" -B "$sdk/build-editorhost" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DSMTG_ENABLE_VSTGUI_SUPPORT=OFF -DSMTG_ENABLE_VST3_PLUGIN_EXAMPLES=OFF \
        -DSMTG_ENABLE_VST3_HOSTING_EXAMPLES=ON >&2 || return 1
    cmake --build "$sdk/build-editorhost" --target editorhost >&2 || return 1
    mkdir -p external/editorhost || return 1
    if [ "$os" = macOS ]; then
        local app; app="$(find "$sdk/build-editorhost/bin" -type d -name editorhost.app 2>/dev/null | head -1)"
        [ -n "$app" ] || { say "no editorhost.app under $sdk/build-editorhost/bin"; return 1; }
        rm -rf external/editorhost/editorhost.app && cp -R "$app" external/editorhost/ || return 1
    else
        local built; built="$(find "$sdk/build-editorhost/bin" -type f -name editorhost.exe 2>/dev/null | head -1)"
        [ -n "$built" ] || { say "no editorhost.exe under $sdk/build-editorhost/bin"; return 1; }
        cp "$built" "$out" || return 1
    fi
    chmod +x "$out"
    echo "$out"
}

fetch_pluginval() {
    local dir="external/pluginval" out
    case "$os" in
        Linux)   out="$dir/pluginval" ;;
        macOS)   out="$dir/pluginval.app/Contents/MacOS/pluginval" ;;
        Windows) out="$dir/pluginval.exe" ;;
    esac
    if [ -x "$out" ]; then echo "$out"; return 0; fi
    mkdir -p "$dir"
    local zip="$dir/pluginval_$os.zip"
    say "downloading pluginval $PLUGINVAL_VERSION for $os"
    curl -sSL --fail -o "$zip" "$PLUGINVAL_BASE/pluginval_$os.zip" || { say "download failed"; return 1; }
    if [ "$os" = macOS ]; then ditto -x -k "$zip" "$dir" >&2 || return 1; else unzip -qo "$zip" -d "$dir" >&2 || return 1; fi
    [ -f "$out" ] || { say "no executable at $out after unzip"; return 1; }
    chmod +x "$out"
    echo "$out"
}

case "${1:-}" in
    validator)  fetch_validator ;;
    pluginval)  fetch_pluginval ;;
    editorhost) fetch_editorhost ;;
    *) echo "usage: tools/fetch-validators.sh validator|pluginval|editorhost" >&2; exit 2 ;;
esac
