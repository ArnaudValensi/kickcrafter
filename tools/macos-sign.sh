#!/usr/bin/env bash
# Sign, notarize and staple the macOS bundles of a dist archive, verify them, and archive them again
# (decision 10 of epics/multi-platform-build, reproducing the owner's reference workflows).
#
# usage: tools/macos-sign.sh <kickcrafter-fable-<version>-macos-universal.zip> [<out dir>]
#        writes <out dir>/<same file name> (default: next to the input, replacing it)
#
# Environment (the six repository secrets of release.yml; nothing is ever printed, there is no set -x):
#   MACOS_SIGN_IDENTITY     "Developer ID Application: <name> (<team>)". Empty or unset: the bundles are
#                           signed AD HOC, nothing is notarized, and the summary and INSTALL.txt say so.
#   MACOS_CERT_P12_BASE64   the exported Developer ID certificate and key (.p12), base64
#   MACOS_CERT_PASSWORD     the password of that .p12
#   MACOS_NOTARY_APPLE_ID   the Apple ID used for notarization
#   MACOS_NOTARY_TEAM_ID    its team id
#   MACOS_NOTARY_PASSWORD   an app-specific password of that Apple ID
#
# Steps, in this order and no other: unpack; import the certificate into a throwaway keychain;
# codesign --force --deep --options runtime --timestamp both bundles; zip; notarytool submit --wait;
# stapler staple each bundle; rewrite the Signing line of INSTALL.txt; zip again (the final archive
# must contain the stapled bundles); codesign --verify --deep --strict and spctl, printed in the job
# summary. The keychain is deleted and the .p12 removed on exit. Fails on the first error.
# Runs under the macOS runner's bash 3.2 (no bash 4 constructs).
set -euo pipefail
die() { echo "macos-sign: $*" >&2; exit 1; }
archive="${1:-}"
[ -n "$archive" ] || die "usage: tools/macos-sign.sh <archive.zip> [<out dir>]"
[ -f "$archive" ] || die "no such archive: $archive"
[ "$(uname -s)" = "Darwin" ] || die "macOS only (codesign, notarytool, stapler)"
out="${2:-$(dirname "$archive")}"
identity="${MACOS_SIGN_IDENTITY:-}"
summary="${GITHUB_STEP_SUMMARY:-}"
note() { printf '%s\n' "$*"; [ -z "$summary" ] || printf '%s\n' "$*" >> "$summary"; }

work="$(mktemp -d)"
keychain=""
cleanup() {
    [ -z "$keychain" ] || security delete-keychain "$keychain" >/dev/null 2>&1 || true
    rm -rf "$work"
}
trap cleanup EXIT

# 1. Unpack: one folder holding both bundles, the documents and INSTALL.txt.
ditto -x -k "$archive" "$work/unpacked"
folder="$(find "$work/unpacked" -mindepth 1 -maxdepth 1 -type d | head -1)"
[ -n "$folder" ] || die "the archive holds no folder"
vst3="$folder/KickCrafter Fable.vst3"
component="$folder/KickCrafter Fable.component"
[ -d "$vst3" ] || die "missing $vst3"
[ -d "$component" ] || die "missing $component"
[ -f "$folder/INSTALL.txt" ] || die "missing INSTALL.txt"

# 2. The identity: a throwaway keychain holding the Developer ID certificate, as the reference
#    workflows do it; the .p12 is written, imported and removed at once.
if [ -n "$identity" ]; then
    for v in MACOS_CERT_P12_BASE64 MACOS_CERT_PASSWORD MACOS_NOTARY_APPLE_ID MACOS_NOTARY_TEAM_ID MACOS_NOTARY_PASSWORD; do
        eval "[ -n \"\${$v:-}\" ]" || die "MACOS_SIGN_IDENTITY is set but $v is empty"
    done
    keychain="${RUNNER_TEMP:-$work}/kcf-sign.keychain-db"
    keychain_password="$(uuidgen)"
    security create-keychain -p "$keychain_password" "$keychain"
    security set-keychain-settings -lut 3600 "$keychain"
    security unlock-keychain -p "$keychain_password" "$keychain"
    printf '%s' "$MACOS_CERT_P12_BASE64" | base64 --decode > "$work/cert.p12"
    security import "$work/cert.p12" -k "$keychain" -P "$MACOS_CERT_PASSWORD" -T /usr/bin/codesign >/dev/null
    rm -f "$work/cert.p12"
    security set-key-partition-list -S apple-tool:,apple:,codesign: -s -k "$keychain_password" "$keychain" >/dev/null
    # Keep the login keychain in the search list so default lookups still work.
    security list-keychains -d user -s "$keychain" $(security list-keychains -d user | tr -d '"')
    note "Signing with a Developer ID identity (hardened runtime, timestamp), then notarizing."
else
    note "WARNING: MACOS_SIGN_IDENTITY is not set. The bundles are signed AD HOC and NOT notarized: macOS will refuse them until the quarantine attribute is removed (INSTALL.txt says how)."
fi

# 3. Sign both bundles. --deep signs the nested code; the hardened runtime is what notarization requires.
for bundle in "$vst3" "$component"; do
    if [ -n "$identity" ]; then
        codesign --force --deep --options runtime --timestamp --sign "$identity" "$bundle"
    else
        codesign --force --deep --sign - "$bundle"
    fi
done

# 4. Notarize (one zip with both bundles; every bundle inside gets its ticket), then staple each bundle.
if [ -n "$identity" ]; then
    ( cd "$(dirname "$folder")" && ditto -c -k --keepParent "$(basename "$folder")" "$work/notarize.zip" )
    xcrun notarytool submit "$work/notarize.zip" \
        --apple-id "$MACOS_NOTARY_APPLE_ID" --team-id "$MACOS_NOTARY_TEAM_ID" --password "$MACOS_NOTARY_PASSWORD" \
        --wait 2>&1 | tee "$work/notary.log"
    if ! grep -q 'status: Accepted' "$work/notary.log"; then
        submission="$(sed -n 's/^ *id: *//p' "$work/notary.log" | head -1)"
        [ -z "$submission" ] || xcrun notarytool log "$submission" \
            --apple-id "$MACOS_NOTARY_APPLE_ID" --team-id "$MACOS_NOTARY_TEAM_ID" --password "$MACOS_NOTARY_PASSWORD" || true
        die "notarization was not accepted (see the notarytool output above)"
    fi
    note "Notarization: Accepted."
    for bundle in "$vst3" "$component"; do
        xcrun stapler staple "$bundle"
        xcrun stapler validate "$bundle"
    done
    signing_line='Signing: signed with a Developer ID certificate (hardened runtime) and notarized by Apple; the tickets are stapled to both bundles.'
else
    signing_line='Signing: AD HOC ONLY, not notarized (built without the Developer ID secrets). macOS refuses a downloaded unsigned bundle until its quarantine attribute is removed: xattr -dr com.apple.quarantine "<the copied bundle>"'
fi

# 5. INSTALL.txt: replace the Signing line dist.sh wrote.
awk -v line="$signing_line" '/^Signing: /{print line; next}{print}' "$folder/INSTALL.txt" > "$work/INSTALL.txt"
grep -q '^Signing: ' "$work/INSTALL.txt" || die "INSTALL.txt has no Signing line to rewrite"
mv "$work/INSTALL.txt" "$folder/INSTALL.txt"

# 6. The final archive, after stapling (a zip made before stapling would ship bundles without tickets).
mkdir -p "$out"
final="$out/$(basename "$archive")"
( cd "$(dirname "$folder")" && ditto -c -k --keepParent "$(basename "$folder")" "$work/final.zip" )
rm -f "$final"
mv "$work/final.zip" "$final"

# 7. Verify what the final archive holds and print the verdicts in the job summary.
#    Gates (a failure stops the release): codesign --verify --deep --strict in every mode; when
#    signed, codesign --check-notarization against the requirement "notarized" (Apple's own test
#    for a notarized code bundle) and stapler validate (the ticket is in the bundle).
#    Diagnostics (printed, never fatal): spctl. Gatekeeper assesses apps (--type exec),
#    packages (--type install) and disk images (--type open); a bare plug-in bundle is none of
#    these, so its spctl verdict is information for the reader, not a verdict on the release.
ditto -x -k "$final" "$work/verify"
for name in "KickCrafter Fable.vst3" "KickCrafter Fable.component"; do
    bundle="$work/verify/$(basename "$folder")/$name"
    note ""
    note "### $name"
    note '```'
    codesign --verify --deep --strict --verbose=2 "$bundle" 2>&1 | tee -a "${summary:-/dev/null}"
    if [ -n "$identity" ]; then
        codesign --verify --verbose=4 -R="notarized" --check-notarization "$bundle" 2>&1 | tee -a "${summary:-/dev/null}"
        xcrun stapler validate "$bundle" 2>&1 | tee -a "${summary:-/dev/null}"
    fi
    note "spctl (diagnostic only, see above):"
    spctl --assess --type install --verbose=2 "$bundle" 2>&1 | tee -a "${summary:-/dev/null}" || true
    note '```'
done
note ""
note "Archive: $final ($(shasum -a 256 "$final" | cut -d' ' -f1))"
[ -n "$identity" ] || note "WARNING: ad hoc signature, not notarized; INSTALL.txt carries the warning."
