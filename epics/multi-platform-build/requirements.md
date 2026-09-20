# Multi-platform build and binary releases

Status: planned (2026-09-20). Decisions taken with the owner in the session that wrote this file;
the alternatives that lost are recorded under each decision, not re-litigated.

Make KickCrafter build on the three desktop platforms and let GitHub Actions produce the binary
releases. Today the plug-in is a Linux x86_64 VST3 built and validated on the owner's machine.
After this epic: Linux x86_64 VST3, macOS universal (Intel and Apple Silicon) VST3 and AU, Windows
x64 VST3, each compiled by a GitHub Actions job that also runs the JUCE-free engine tests, packed
into a per-platform archive, and attached to a draft GitHub Release when a version tag is pushed.
The macOS bundles are signed with the owner's Developer ID and notarized. The validation gates
(`./run check`, `./run validate`, `./run memory`) stay on the development machine and are not part
of CI: CI builds, it does not validate (`CLAUDE.md`, "Already decided").

This file is self-sufficient: a session with no memory of the conversation that produced it can
implement the epic from it. Read it, then `CLAUDE.md`, `docs/development.md`, `CMakeLists.txt`,
`tests/CMakeLists.txt`, `run`, `tools/package.sh`, `tools/run-logged.sh`, `plugin/Presets.cpp`
(the preset folder), and the two reference workflows whose macOS signing steps this epic
reproduces: `https://github.com/ArnaudValensi/oob-jai/blob/main/.github/workflows/build.yml` and
`https://github.com/ArnaudValensi/squirrel-whisperer/blob/main/.github/workflows/build.yml`
(private repositories of the owner; `gh api repos/<owner>/<repo>/contents/<path>` reads them).
JUCE's own reference for the CMake options is `external/JUCE/docs/CMake API.md`.

## Scope

In: the CMake project compiles with GCC, Apple Clang and MSVC; the Linux-only test executables
and sanitizer options are guarded so the other platforms build the plug-in and the engine tests
only; AU is added on macOS with a universal binary; the user preset folder follows each
platform's convention; a `dist` command packs a built tree into the per-platform archive; the
`run` script and the tool scripts it calls work under Git Bash on Windows and on macOS for the
commands CI uses; two workflows, one that builds on every push to `main` and every pull request,
one that releases on a version tag; macOS signing and notarization with a fallback when the
secrets are absent; README installation instructions per platform; the version becomes 1.4.0 with
its CHANGELOG entry.

Out, recorded as future work at the end: CLAP, AUv3, LV2, AAX; running the plug-in tests, the
REAPER harness or the memory gate on macOS or Windows; the Steinberg validator or Apple's `auval`
in CI; Windows code signing; installers (`.pkg`, `.msi`); ccache in CI.

## Settled decisions

1. **Platforms and formats.** Linux x86_64: VST3 (the Standalone target keeps building as a
   developer convenience, it is not shipped). macOS: VST3 and AU, one universal binary
   `arm64;x86_64` built on a single runner (`CMAKE_OSX_ARCHITECTURES`), deployment target macOS 11
   (the first release that runs on Apple Silicon; nothing older runs on an ARM Mac, and Intel Macs
   on 10.13 to 10.15 are too few to carry the testing surface). Windows x64: VST3. No AUv3 (a
   different life cycle, for iPad Logic), no CLAP (needs `clap-juce-extensions`, one more pinned
   dependency, and every host that reads CLAP reads VST3; the plug-in freezes everything at Note
   On, so CLAP's per-note modulation buys nothing here). Alternatives rejected: two macOS runners
   plus `lipo` as in the Jai repositories (Jai cannot cross-compile, CMake and JUCE can).

2. **One toolchain per platform, Ninja everywhere.** Linux GCC as today; macOS Apple Clang from
   the Xcode on the `macos-15` runner; Windows MSVC 2022 x64 with `cl.exe` taken from a Visual
   Studio developer environment (`ilammy/msvc-dev-cmd@v1` in CI). Ninja on the three, so `./run
   build` is one code path. Alternative rejected: the Visual Studio generator (multi-config build
   directories would fork every path in `run` and the tools).

3. **Compiler options by compiler, not by platform.** The GCC-style warning options
   (`-Wall -Wextra -Wpedantic -Wshadow -Wconversion`, the `-Wno-*` of the tests) are applied under
   `if(NOT MSVC)`; MSVC gets `/W4`. `KCF_SANITIZE` and `KCF_SANITIZE_PLUGIN` are refused with a
   `message(FATAL_ERROR)` on anything but Linux (LeakSanitizer preload and the diagnostic build are
   the memory gate, which is Linux and local). `juce_recommended_lto_flags` is portable and stays.

4. **Tests per platform.** `kcf_engine_tests` (23 cases, no JUCE) builds and runs everywhere and
   CI runs it on every platform: it is the only proof that every binary makes the same sound.
   `kcf_plugin_tests`, `kcf_leak_tests` and `kcf_vst3_host` are Linux only
   (`if(CMAKE_SYSTEM_NAME STREQUAL "Linux")` around them in `tests/CMakeLists.txt`): they need
   X11, pkg-config libraries, a display with a window manager and LeakSanitizer. Porting them is
   future work.

5. **The user preset folder follows the platform.** `plugin/Presets.cpp` keeps
   `juce::File::userApplicationDataDirectory` and adds, under `JUCE_MAC` only, the
   `Application Support` component that JUCE leaves out (JUCE returns `~/Library`; writing at the
   root of `~/Library` is against Apple's convention). Result: Linux `$XDG_CONFIG_HOME` or
   `~/.config/KickCrafterFable/Presets/` (unchanged), macOS
   `~/Library/Application Support/KickCrafterFable/Presets/`, Windows
   `%APPDATA%\KickCrafterFable\Presets\`. `KCF_PRESET_DIR` overrides on the three. The README's
   preset section names the three paths.

6. **`./run` is the CI's only interface**, in this order per job: `./run juce`, `./run build
   <targets>`, `./run test-engine`, `./run dist`. The script runs under Git Bash on Windows
   (`shell: bash` in the workflow) and under the macOS runner's bash 3.2: no bash 4 features
   (`${var,,}`, associative arrays, `readarray`). The target list `./run build` uses by default is
   platform-aware: the three Linux-only test targets are added only on Linux, `KickCrafterFable_AU`
   only on macOS. `KCF_JOBS` is set by the workflow to `4` (the runners' core count).

7. **`sha256` is a helper, not a command.** `tools/run-logged.sh`, the new `tools/dist.sh` and any
   script CI reaches use one function that calls `sha256sum` when it exists and `shasum -a 256`
   otherwise (macOS has no `sha256sum`). Scripts CI does not reach (`build-and-test.sh`,
   `memory-check.sh`, `validate-bundle.sh`, `package-preflight.sh`, `package.sh`) stay Linux and
   are not touched for this.

8. **The archives.** `./run dist` (new `tools/dist.sh`) packs what `build/` holds into
   `artifacts/dist/`:
   - `kickcrafter-fable-<version>-linux-x86_64.tar.gz`: `KickCrafter Fable.vst3/`
   - `kickcrafter-fable-<version>-macos-universal.zip`: `KickCrafter Fable.vst3/` and
     `KickCrafter Fable.component/`
   - `kickcrafter-fable-<version>-windows-x86_64.zip`: `KickCrafter Fable.vst3/`

   Each archive also holds `README.md`, `CHANGELOG.md`, `LICENSE`, `THIRD_PARTY_NOTICES.md`, an
   `INSTALL.txt` with the platform's install path, and `licenses/` with the same licence texts
   `tools/package.sh` copies today. That list moves out of `package.sh` into `dist.sh`, and
   `package.sh` calls `dist.sh` for its binary archive so the list exists once; `package.sh` keeps
   its own name for the Linux archive today (`-<shortrev>-`) becoming the version-based name above,
   and keeps adding `BUILD-RECORD.txt`, the source archive and the evidence archive. `<version>`
   is read from `project(... VERSION ...)` in `CMakeLists.txt`; a build that is not on a version
   tag appends `-<short git sha>` in CI so two pushes never produce the same file name.

9. **Two workflows.** `.github/workflows/build.yml` runs on push to `main` and on pull requests,
   filtered on the paths that change a binary or the build (`engine/`, `plugin/`, `resources/`,
   `tests/`, `CMakeLists.txt`, `run`, `tools/`, `.github/workflows/`), with `concurrency` cancelling
   superseded runs, one job per platform (`ubuntu-latest`, `macos-15`, `windows-latest`), each:
   checkout, dependencies (apt list from `docs/development.md` on Linux; nothing on macOS; the
   MSVC developer shell on Windows), `./run juce`, `./run build`, `./run test-engine`, `./run dist`,
   `actions/upload-artifact` of `artifacts/dist/` and `artifacts/logs/` (the engine test log with
   its exit sidecar, per the evidence convention), retention 14 days. `permissions: contents: read`.
   The workflow is also callable (`workflow_call`) so the release reuses it unchanged.
   `.github/workflows/release.yml` runs on tags `v*` and on `workflow_dispatch`: a first job checks
   that the tag equals `v<project version>` and that the top `CHANGELOG.md` heading is that version
   (on dispatch, the check runs against `HEAD` and only reports); it calls `build.yml`; a macOS
   job signs and notarizes (decision 10); a final job downloads everything, writes `SHA256SUMS`,
   and creates a **draft** GitHub Release with `softprops/action-gh-release` and
   `fail_on_unmatched_files: true` (on dispatch it uploads the would-be assets as a workflow
   artifact instead of creating a release, so the whole flow can be rehearsed without a tag).
   Publishing the draft is the owner's click. Alternatives rejected: tags-only CI as in the Jai
   repositories (`CLAUDE.md` wants a build on every change to `main`); auto-published releases
   (publishing stays the owner's decision, `CLAUDE.md` "Git").

10. **macOS signing and notarization** happen in the release workflow only, never in `build.yml`,
    with the six secrets already used by oob-jai and squirrel-whisperer, same names:
    `MACOS_SIGN_IDENTITY`, `MACOS_CERT_P12_BASE64`, `MACOS_CERT_PASSWORD`, `MACOS_NOTARY_APPLE_ID`,
    `MACOS_NOTARY_TEAM_ID`, `MACOS_NOTARY_PASSWORD`. Steps, in a `tools/macos-sign.sh` the job
    calls (so the steps are reviewable and rerunnable by hand): import the certificate into a
    throwaway keychain exactly as the reference workflows do; `codesign --force --deep --options
    runtime --timestamp` on the `.vst3` and the `.component`; zip; `xcrun notarytool submit --wait`;
    `xcrun stapler staple` on each bundle; zip again into the final archive; `codesign --verify
    --deep --strict` and `spctl --assess --type install` (or `open`-type as appropriate for a
    plug-in bundle) printed in the job summary. When `MACOS_SIGN_IDENTITY` is empty the script
    signs ad hoc (`codesign -s -`), skips notarization and writes a visible warning in the summary
    and in `INSTALL.txt` (the user will have to clear the quarantine attribute). The owner adds
    the six secrets to the KickCrafter repository before the first release; the workflow header
    lists them as the reference workflows do. Windows signing: future work.

11. **CI builds Linux too**, with GCC on `ubuntu-latest`. The Linux archive in a release is the CI
    one. The binary validated on the development machine (`./run validate`, `./run memory`, the
    REAPER stages) is built by a different compiler version and will not have the same hash; that
    is accepted. The release process is: the owner runs `./run validate` and both memory passes on
    the tagged commit locally, runs `./run package` for the evidence archive, tags, lets CI build
    the three archives, attaches the evidence archive to the draft by hand, publishes. The
    development guide's packaging section says this.

12. **What each platform's binary has been through is stated to users.** The README's
    installation section has one subsection per platform with the install path and, for macOS and
    Windows, one sentence: built and unit-tested by CI, not yet validated in a host by the
    maintainer. Linux keeps its sentence about the validation gates. Honesty over marketing:
    `CLAUDE.md` "Evidence over assertion".

13. **Version 1.4.0.** `project(KickCrafterFable VERSION 1.4.0 ...)` and a `CHANGELOG.md` entry
    dated from `date +%F`: macOS (VST3 and AU, universal) and Windows (VST3) builds, preset folder
    per platform, binary releases on GitHub. The state and preset schemas do not change (no value
    changes meaning), so no migration.

14. **Pushing stays the owner's action** (`CLAUDE.md` "Git", the chain-run contract). Milestones
    M2 and M3 can only be accepted by green runs on GitHub, so at the end of each of them the
    implementer stops at a committed, clean state and asks the owner to push (and, for M3, to add
    the secrets and to trigger the dispatch). If this epic is run as a chain, its `CONSTITUTION.md`
    may grant `git push` to the chain for this epic only; the default is not to.

15. **Private repository minutes.** A macOS minute costs ten Linux minutes and Windows two.
    Decision 9's triggers (push to `main`, pull requests, path filters) are the cost control; the
    repository going public is the owner's open question in `CLAUDE.md` and is not assumed here.

## The seams, by file

- `CMakeLists.txt`: version 1.4.0; `FORMATS VST3 AU Standalone` (JUCE ignores AU off macOS);
  `CMAKE_OSX_ARCHITECTURES` default `arm64;x86_64` and `CMAKE_OSX_DEPLOYMENT_TARGET` default `11.0`
  set before `project()` when `APPLE` and the caller set nothing; warning options under
  `if(NOT MSVC)` with the MSVC branch; the two sanitizer options refused off Linux; the comment
  that still cites `docs/architecture.md` (removed) corrected to the README section.
- `tests/CMakeLists.txt`: the Linux-only block; `-Wall -Wextra` under `if(NOT MSVC)`.
- `plugin/Presets.cpp`: the macOS `Application Support` component.
- `run`: platform-aware default targets; `dist` command; `KCF_JOBS` unchanged; nothing bash 4.
- `tools/dist.sh` (new): version from `CMakeLists.txt`, platform from `uname`, the licence list and
  `INSTALL.txt` text, the archive.
- `tools/package.sh`: calls `dist.sh` for the binary archive, adds `BUILD-RECORD.txt`, new names.
- `tools/package-preflight.sh`, `tools/package-negative-controls.sh`: the archive name change only
  where they read it (check with `grep -n kickcrafter-fable tools/`).
- `tools/run-logged.sh`: the `sha256` helper.
- `tools/macos-sign.sh` (new): decision 10.
- `.github/workflows/build.yml`, `.github/workflows/release.yml` (new): decision 9.
- `README.md`: installation per platform, preset folders, the limitations line.
- `docs/development.md`: a "Continuous integration and releases" section (what CI runs, the
  secrets, the release procedure of decision 11, how to rehearse with dispatch); the "Building
  from source" section mentions macOS and Windows prerequisites (Xcode command line tools; Visual
  Studio 2022 Build Tools with the C++ workload, Ninja, Git for Windows).
- `CHANGELOG.md`: the 1.4.0 entry.
- `CLAUDE.md`: "Linux x86_64 VST3 first" in the closed decisions becomes the three-platform
  statement with the validation asymmetry of decision 12.

## Validation

**Run only the checks the change needs** (owner's instruction, 2026-09-20): the what-to-run table
of `docs/development.md` names what a change can break, and the proof that Linux is unchanged is
the hash, not a rerun. Concretely for M1: rebuild, and compare the staged module's SHA-256 with the
one in `artifacts/logs/build-record.txt` before the change. Guards that only affect other
compilers or platforms (`if(NOT MSVC)`, `if(APPLE)`, the Linux-only test block, the `JUCE_MAC`
branch in `Presets.cpp`) leave the Linux module byte-identical, and an identical hash closes the
question. The version bump changes the module (the version string is compiled in): that earns
`./run check` (plug-in tests, validator) and nothing more, because a version string does not alter
host behaviour. `./run memory` only if a lifecycle path changed on Linux (it does not in this
epic's plan). `./run validate` is for a change that can alter what the host sees: none is planned
here. `./run dist`, `./run package`, `./run package-controls` and `./run preflight` are the checks
for the packaging scripts, which do change. Milestone 1 is proven locally. Milestones 2 and 3 are
proven by green GitHub Actions runs after the owner pushes (decision 14): three green jobs with
downloadable archives for M2; a green `workflow_dispatch` rehearsal of the release with a signed,
notarized macOS archive whose `codesign --verify` and `spctl` output appear in the job summary for
M3, followed by the owner's tag.

## Future work (recorded, not scheduled)

- Steinberg validator and Apple `auval` in the macOS and Windows jobs (the SDK validator builds on
  both; `auval -v aumu Kcfb Arnv` after copying the component to `~/Library/Audio/Plug-Ins/
  Components`). Cheap, and the closest thing to host validation those platforms can get without a
  REAPER harness; excluded now because the owner decided CI builds and does not validate.
- Porting `kcf_plugin_tests` off X11 (JUCE's own message loop works on the three platforms; the
  display and window-manager requirements are Linux artefacts).
- CLAP through `clap-juce-extensions`, on the three platforms at once, if users ask.
- Windows code signing (a certificate the owner does not have today).
- ccache in CI once the JUCE build time on the runners is measured.
