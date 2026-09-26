# Development

Everything below runs from the repository root. All tool scripts write their outputs under
`artifacts/` (git-ignored) and keep every run's log with the real exit code, so evidence is never
overwritten by a later run.

## The run script

`./run <command>` is the single entry point for every development command; `./run help` lists
them. It calls the scripts under `tools/` described in this document, so a command typed by hand
and one run through `./run` (or by CI) do exactly the same thing. Two commands are gates:

```sh
./run check       # the daily gate: build chain, engine tests, Steinberg validator (minutes once JUCE is built)
./run validate    # the full gate: check, ASan engine tests, REAPER stages, harness controls, package controls (about 20 minutes)
./run memory      # the leak gate, pass A; ./run memory --diag builds build-leak/ and runs pass B. On demand, local only, both before a release
```

The memory gate is outside `validate` on purpose: its two passes cost as much as the rest of the
gate together (pass B rebuilds JUCE instrumented), so they run when a developer asks for them and
before every release, never in CI.

A gate stops at its first failing step and records every step's exit code under
`artifacts/logs/gates/` (a fresh file per run, plus `artifacts/logs/gate-<name>.txt` for the last
run). Machine-specific values (validator path, REAPER path, job count) live in `.env` at the
repository root, copied from `.env.example` and git-ignored; a variable set in the real environment
wins over `.env`.

## Setting up a development machine

Nothing here assumes a particular machine: every external tool is found on `PATH` or named by an
environment variable, and every script creates the folders it needs.

### 1. Build tools

Linux x86_64, CMake 3.22+, Ninja, GCC 12+ (or a recent Clang), pkg-config, git, and the development
packages of FreeType, fontconfig, ALSA and X11 (`libx11`, `libxext`, `libxrandr`, `libxinerama`,
`libxcursor`). Debian/Ubuntu: `apt install build-essential cmake ninja-build pkg-config git
libfreetype-dev libfontconfig-dev libasound2-dev libx11-dev libxext-dev libxrandr-dev
libxinerama-dev libxcursor-dev`. Arch: `pacman -S base-devel cmake ninja pkgconf git freetype2
fontconfig alsa-lib libx11 libxext libxrandr libxinerama libxcursor`.

JUCE 8.0.9 is pinned by commit and fetched once into `external/JUCE` (not vendored):

```sh
tools/fetch-juce.sh
```

### 2. uv, for every Python script

Every Python script under `tools/` is a self-contained [uv](https://docs.astral.sh/uv/) script
(shebang `#!/usr/bin/env -S uv run --script`, inline PEP 723 metadata with pinned dependencies). Run
a script directly; uv provides the interpreter and the packages on first use. Install uv with your
package manager or `curl -LsSf https://astral.sh/uv/install.sh | sh`.

### 3. Headless displays (GUI tests, memory gate, host harness)

Install `xvfb`, `openbox`, `xdotool` and the X utilities `xprop` / `xwininfo` (package `x11-utils`
on Debian/Ubuntu, `xorg-xprop` and `xorg-xwininfo` on Arch). Then:

```sh
tools/xvfb-display.sh          # :104 (Xvfb + Openbox), used by the plugin tests, the chain and the memory gate
tools/xvfb-display.sh :102     # the REAPER harness display
```

The script is idempotent and disables the X screen saver (a blanked Xvfb gives black captures). A
window manager is required: JUCE on a bare Xvfb can hit X11 BadAtom errors, and the popup-menu
tests need real window focus. Other display numbers work: `DISPLAY` for the tests, `KCF_DISPLAY`
for the harness.

### 4. Steinberg VST3 validator (bundle check)

Build it from the VST3 SDK once, then point `KCF_VALIDATOR` at it or put it on `PATH`:

```sh
git clone --recursive https://github.com/steinbergmedia/vst3sdk.git
cmake -S vst3sdk -B vst3sdk/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build vst3sdk/build --target validator
export KCF_VALIDATOR="$(find "$PWD/vst3sdk/build/bin" -name validator -type f | head -1)"   # the SDK nests it under bin/<Config>/
```

Or let the repository do it: `./run fetch-validator` clones the SDK at the pinned tag
(`v3.8.1_build_84`, in `tools/fetch-validators.sh`) into `external/vst3sdk/`, builds the
`validator` target and copies the executable to `external/validator/`, which `./run validator`
finds when `KCF_VALIDATOR` is unset and no `validator` is on `PATH`. CI does exactly this, with
`external/validator/` cached by OS and tag.

### 4b. pluginval (editor, parameters, state, threads)

Tracktion's pluginval exercises what the Steinberg validator does not: it opens the editor,
sweeps the parameters, changes block sizes and sample rates, saves and restores state, and calls
from several threads. `./run fetch-pluginval` downloads the pinned release (`v1.0.4`) into
`external/pluginval/`; `./run pluginval [level] [bundle]` runs it on the staged bundle (or the one
given) at strictness 1 to 10, default 5 (above 5 the timing-sensitive checks fail on a loaded
machine), logs it like the validator under `artifacts/logs/runs/` and leaves pluginval's own
report under `artifacts/logs/pluginval/`. It opens a window: on a headless machine, `DISPLAY` from
`.env` or `xvfb-run -a ./run pluginval`. `KCF_PLUGINVAL` names another executable. It is not part
of `./run check` or `./run validate`; CI runs it on the three platforms.

### 4c. editorhost (Steinberg's own editor host, macOS and Windows)

The SDK's `editorhost` sample opens a plug-in's editor the way Steinberg's hosts do (its window
code is the closest public relative of Cubase's); a tester's Cubase 11 on Windows 10 crashed at
that moment (2026-09-26). `./run fetch-editorhost` builds it from the same SDK checkout into
`external/editorhost/` (macOS and Windows only: on Linux its window needs gtkmm-3.0);
`./run editorhost [seconds] [bundle]` opens the editor and requires the host to be alive after N
seconds (default 20), then kills it; an earlier exit is a failure with the host's exit code. Logged
like the validator. `KCF_EDITORHOST` names another executable. CI runs it on macOS and Windows at
the runner's 100 % screen scale, so it covers the attach and the size negotiation, not the DPI
scale factor; the Linux VST3 host test (`kcf_vst3_host`, run by `./run memory`) covers that factor
through the same JUCE wrapper code (content scale 1.25, 1.5, 2, then negotiated sizes).

### 4d. Diagnostic build: Windows crash dumps

A tester's crash on Windows is read with a minidump and the `.pdb` of the same build. Because
Windows writes no dump by default and a tester rarely edits the registry, the plug-in can do it
itself in a **diagnostic build**: CMake option `KCF_CRASH_DUMP=ON` (Windows only; `./run build`
passes it through `KCF_CMAKE_OPTIONS`), which compiles `plugin/CrashDump_win32.cpp`: at the first
instantiation the module installs an unhandled-exception filter; when the host crashes it writes
`KickCrafter-crash-<date>-<time>.dmp` and a `.txt` next to it (version, module base, exception
code and address) on the user's Desktop (`%TEMP%` if the Desktop cannot be resolved), shows a
message box naming the file, then lets the crash continue.
Never in a release: `release.yml` cannot set it, and the archive of such a build is named
`kickcrafter-<version>-<sha>-crashdump-windows-x86_64.zip` (`KCF_DIST_SUFFIX`). To make one: run
the Build workflow by hand with the `crash_dump` input on; the Windows job also self-tests the
mechanism (`KCF_CRASH_TEST=1` makes the module raise an access violation at its first
instantiation, the validator must die and the dump must appear) and uploads the archive and the
symbols. Give the tester the archive; when it crashes, the message box names the dump on the
Desktop; open the
`.dmp` in WinDbg with the `.pdb` from the same run's `kickcrafter-windows-x86_64-symbols` artifact
(`.sympath+ <folder with the .pdb>`, `!analyze -v`).

### 5. REAPER (host harness)

Download the Linux x86_64 build from [reaper.fm](https://www.reaper.fm/download.php) (tested with
7.80; the evaluation licence is enough) and make `reaper` reachable: `KCF_REAPER` names the
executable (default `/usr/sbin/reaper`). The harness runs REAPER with its own configuration under
`artifacts/reaper/config/` and never touches `~/.config/REAPER`.

### 6. Environment variables

| Variable | Read by | Meaning (default) |
|---|---|---|
| `KCF_VALIDATOR` | `validate-bundle.sh`, `memory-check.sh` | Steinberg validator executable (`validator` on `PATH`, then `external/validator/`) |
| `KCF_PLUGINVAL` | `pluginval.sh` | pluginval executable (`pluginval` on `PATH`, then `external/pluginval/`) |
| `KCF_EDITORHOST` | `editorhost.sh` | Steinberg editorhost executable (`external/editorhost/`; macOS and Windows) |
| `KCF_CMAKE_OPTIONS` | `run build` | extra CMake options for `build/` (empty; CI sets `-DKCF_CRASH_DUMP=ON` for the diagnostic build) |
| `KCF_DIST_SUFFIX` | `dist.sh` | inserted after the version in the archive name (empty; `-crashdump` for the diagnostic build) |
| `KCF_CRASH_TEST` | the diagnostic module | set: the module raises an access violation at its first instantiation (the self-test of the crash dump) |
| `KCF_CRASH_DUMP_DIR` | the diagnostic module | folder for the dump instead of the Desktop (an escape hatch; a tester leaves it unset) |
| `KCF_REAPER` | REAPER harness | REAPER executable (`/usr/sbin/reaper`) |
| `KCF_DISPLAY` | REAPER harness, `memory-check.sh --display` | X display of the harness (`:102`); the tests use `DISPLAY` (`:104`) |
| `KCF_XDOTOOL` | REAPER harness | xdotool executable (`xdotool`) |
| `KCF_PRESET_DIR` | plug-in, tests, harness | user preset folder; tests and harness always set a private one |
| `KCF_ENGINE_TESTS` | `analyze` stage | engine test binary for the reference hit (`build/tests/kcf_engine_tests`) |
| `KCF_RESULTS`, `KCF_SHOTS` | REAPER harness | output folders (`artifacts/reaper`, `artifacts/screenshots`) |
| `KCF_JOBS` | `run`, `build-and-test.sh` | parallel compiler jobs (`1`: what a machine with 8 GB or less affords with a JUCE unity build and LTO; a CI runner sets its core count) |
| `CMAKE_BUILD_PARALLEL_LEVEL` | CMake, JUCE's `juceaide` | set from `KCF_JOBS` by `run` and the build chain; set it yourself when calling CMake by hand |

`./run` reads these from `.env` at the repository root (see `.env.example`) unless they are already
set in the environment.

Long jobs (the build chain, the host stages, the memory passes) take 5 to 20 minutes each, and
`./run validate` about 20 minutes at one job once JUCE is built: run them in a persistent shell
(tmux, screen) so they survive a closed terminal.

## Building from source

```sh
git clone https://github.com/ArnaudValensi/kickcrafter.git
cd kickcrafter
tools/fetch-juce.sh                                   # once: JUCE 8.0.9, pinned by commit, into external/JUCE
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target KickCrafter_VST3
mkdir -p ~/.vst3 && cp -r "build/KickCrafter_artefacts/Release/VST3/KickCrafter.vst3" ~/.vst3/
```

Requirements: section 1 of "Setting up" above. On macOS: the Xcode Command Line Tools
(`xcode-select --install`), CMake and Ninja (`brew install cmake ninja`); the build is universal
(`arm64;x86_64`, macOS 11 or later) by default, `-DCMAKE_OSX_ARCHITECTURES=arm64` makes it native
only, and the AU component lands next to the VST3 under `build/KickCrafter_artefacts/Release/AU/`.
On Windows: Visual Studio 2022 Build Tools with the "Desktop development with C++" workload, CMake,
Ninja and Git for Windows; run `./run` from Git Bash inside a "x64 Native Tools" developer prompt
(or after `vcvars64.bat`), so that `cl.exe` is on `PATH` for Ninja. The default targets of `./run
build` follow the platform: the plug-in tests, the leak tests and the VST3 host are Linux only (X11,
pkg-config libraries, a window manager), the AU is macOS only; the engine tests build everywhere.
`./run build` configures and builds every target below into `build/`; the equivalent by hand on
Linux:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target KickCrafter_VST3 KickCrafter_Standalone \
                              kcf_engine_tests kcf_plugin_tests kcf_leak_tests kcf_vst3_host
```

Outputs: `build/KickCrafter_artefacts/Release/VST3/KickCrafter.vst3`, the Standalone
application next to it, and the test executables under `build/tests/`. The delivered build uses
LTO; assertions stay enabled in Release (the test harness relies on them).

On a memory-constrained machine (8 GB with a JUCE unity build), configure and build with a single
job: `export CMAKE_BUILD_PARALLEL_LEVEL=1` and `--build build -j1`. JUCE also builds `juceaide`
with a nested Ninja during configure, which is why the environment variable matters there too.

CMake options: `KCF_ENGINE_ONLY=ON` builds only the JUCE-free engine and its tests (fast, no JUCE
needed), `KCF_SANITIZE=ON` compiles the engine tree with ASan/UBSan, `KCF_SANITIZE_PLUGIN=ON` is
the diagnostic build of the memory gate (see below).

## Tests

```sh
./build/tests/kcf_engine_tests                 # JUCE-free engine: 23 cases
DISPLAY=:104 ./build/tests/kcf_plugin_tests    # real processor + editor: 28 cases, needs an X display with a window manager
```

`./run test` runs both through `tools/run-logged.sh` (`./run test-engine`, `./run test-plugin
[filter]` for one of them); `./run asan` is the ASan/UBSan engine build and its tests below.

`tools/xvfb-display.sh` starts the `:104` display with Openbox for the GUI cases (see "Setting up").
Both test binaries point the user preset
folder at a private temporary directory unless `KCF_PRESET_DIR` is set, so they never read or write
`~/.config`. A single test can be selected by a substring of its name:
`kcf_plugin_tests "user preset library"`.

Engine tests under ASan/UBSan:

```sh
cmake -S . -B build-asan -G Ninja -DKCF_ENGINE_ONLY=ON -DKCF_SANITIZE=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-asan && ./build-asan/tests/kcf_engine_tests
```

Expected results on the current tree: engine 23 cases / 1180 checks, plugin 28 cases / 1166 checks,
validator 47 passed / 0 failed, every host stage PASS, both memory passes 0 leaks with all negative
controls detected.

## The build chain and the build record

```sh
tools/build-and-test.sh          # configure, build, plugin tests on :104, layout dump, staging into artifacts/vst3,
                                 # and an identity-bound build record (artifacts/logs/build-record.txt)
tools/validate-bundle.sh         # Steinberg validator on the staged bundle; the log records the module hash and real exit code
```

(`./run build-chain` and `./run validator`; `./run check` runs both plus the engine tests.)

The build record binds the staged module's SHA-256 to a manifest hash of the production sources
(`engine/ plugin/ resources/ CMakeLists.txt`), the git commit and the JUCE commit (the `tests/ tools/`
manifest is recorded for information only). `tools/package-preflight.sh` refuses to package when the
production sources no longer match the record, when the newest test or validator logs do not name the
recorded binaries, or when the archived paths are not committed; `tools/package-negative-controls.sh`
proves those refusals with fixtures in a scratch repository. `tools/package.sh` then builds three archives under
`artifacts/dist/`, named `kickcrafter-<version>-...` after the `project()` version (binary with
all licence texts and `BUILD-RECORD.txt`, source with the pinned JUCE tree, evidence) plus
`SHA256SUMS`.

`tools/dist.sh` (`./run dist`) is the platform-neutral part of that: it packs what `build/` holds
into `artifacts/dist/kickcrafter-<version>-<platform>.tar.gz` on Linux (`.zip` on macOS,
with the AU component, and on Windows) with `README.md`, `CHANGELOG.md`, `LICENSE`,
`THIRD_PARTY_NOTICES.md`, an `INSTALL.txt` naming the platform's install path, and `licenses/`
(the licence list lives there only; `package.sh` calls it). It is CI's last step on every
platform, runs under macOS's bash 3.2 and Git Bash, and, off a tag in CI, appends the short commit
to the version so two pushes never share a file name. It is not a gate: it packs, it proves
nothing.

`tools/run-logged.sh <name> <command...>` runs a command with its output under
`artifacts/logs/runs/<UTC stamp>-<pid>-<name>.log` and a `.exit` sidecar holding the real exit code.

## REAPER host validation

`tools/reaper/run_reaper_validation.sh` (`./run reaper-stage <stage>`) drives a scratch REAPER instance (its own `reaper.ini` with
the dummy audio device at 48 kHz, `vstpath` pointing at `artifacts/vst3`; the user's own REAPER
settings are never touched) on `Xvfb :102` with Openbox, and checks the plug-in from the host's point
of view. `tools/reaper/run_all_stages.sh` (`./run reaper`) runs every stage in order and stops at the first failure
(`artifacts/reaper/stage-status.txt`, `artifacts/reaper/all-stages.log`, a stamped copy of the log
when a run fails):

| Stage | What it proves |
|---|---|
| `setup` | discovery, formatted host values, envelopes, project save/reopen, offline renders (control, automation, velocity switch Off) |
| `analyze` | renders decoded and measured: velocity, overlapping voices keep their pitch, sample identity with the engine reference hit, the snapshot rule under an envelope jump |
| `editor` | the native editor opens at 100 %, knobs and graph handles are driven with real mouse input (xdotool) and change host values |
| `menus` | preset and scale popups by real clicks under the window manager, user presets, the `...` actions menu and its name prompt |
| `touch`, `read` | automation modes: Touch writes gestures, Read stays authoritative over the editor |
| `lifecycle` | renders with the editor closed and open are identical, 44.1 and 96 kHz |
| `panic`, `program` | CC 120/123 behaviour and the absence of a Program parameter |
| `resize`, `captures` | window scaling and the final screenshots (never uniform, inspected by eye) |

`tools/reaper/harness_negative_controls.sh` (`./run reaper-controls`) runs 22 fixtures that make sure the harness itself fails
when it should (a FAIL line beats a DONE line, a missing PASS line fails the stage, stale renders are
rejected, unchanged host values are not counted as changes, xdotool failures propagate).

Things learned the hard way, kept here so nobody rediscovers them:

- Never edit `run` or a script under `tools/` while a gate that runs it is still executing: bash reads a
  script incrementally, so the running gate executes a mix of the old and the new text and dies
  with a syntax error that the file itself does not have (seen 2026-09-26, a `./run memory` killed
  by an edit of `run`). Finish the gate, or wait for it, then edit.

- The dummy audio device needs the full `linux_audio_*` / `dummy_*` key set in `reaper.ini`; a bare
  `audio_driver=4` leaves the engine closed and parameter delivery stalls. ReaScript parameter
  changes reach the plug-in through REAPER's processing side: verify the component chunk before
  saving, settle, and render a discarded pass before comparing renders.
- `reaper -nonewinst` needs the absolute `-cfgfile` path or it starts a second instance. Every
  ReaScript call raises REAPER's main window; scripts inside REAPER cannot see the caller's
  environment, so the harness communicates through marker files.
- Never minimise a REAPER dialog with xdotool: Openbox iconifies every REAPER window. Close dialogs
  with focus + Escape, then `windowquit`.
- Return inside JUCE popup menus is unreliable under the window manager: the harness highlights a
  row with the keyboard and clicks the row found by its exact colour, keeping the search region away
  from hot buttons of the same colour.
- Typed keys do not reach a JUCE dialog that was merely activated; click into the text field first.
- Graph-handle drags are aimed with the layout dump of the default parameters
  (`kcf_plugin_tests --layout <percent>`), so the timing parameters must be at their reference
  values first, and geometry changes must be followed by a host run.
- Never rebuild `kcf_plugin_tests` while host stages run: the layout dump uses that binary.
- On the development machine `sed -i` rewrites a script without its executable bit (the file
  system does not carry the mode over): check `git diff --summary` after editing a script in place.

## Memory-leak gate

```sh
tools/memory-check.sh                     # pass A: LeakSanitizer preloaded into the ordinary binaries   (./run memory)
cmake -S . -B build-leak -G Ninja -DCMAKE_BUILD_TYPE=Release -DKCF_SANITIZE_PLUGIN=ON
ninja -C build-leak KickCrafter_VST3 kcf_plugin_tests kcf_leak_tests kcf_vst3_host
tools/memory-check.sh --diag              # pass B: diagnostic build                                    (./run memory --diag builds build-leak first)
```

Both passes run `tests/leak_tests.cpp` (engine, processor, editor, multi-instance, preset library,
preset dialog and concurrent audio/editor lifecycle cycles) and `tests/vst3_host_lifecycle.cpp` (a
minimal real VST3 host: dlopen, factory, component and controller, processing with events and
automation, state, `IPlugView` in an X11 window under a host run loop, teardown, `ModuleExit`), each
with an intentional-leak negative control that must be detected. Pass A preloads `liblsan.so` into
the ordinary binaries (delivered code paths, process-wide detection). Pass B compiles this project's
own sources with AddressSanitizer instrumentation; JUCE module translation units are compiled `-O1
-g1` without instrumentation because the instrumented `juce_gui_basics` unity build needs more than
2 GB per compiler process. No suppression file exists. Finite cycles cannot prove universal leak
freedom; they do exercise every lifecycle the plug-in has.

## Continuous integration and releases

CI builds and runs the checks that need no host and no display harness; it runs only for a
release: nothing on GitHub Actions is triggered by an ordinary push or a pull request (the
owner's decision, so that no minutes are spent outside a version). `.github/workflows/build.yml` is called by the release workflow, on the
push of a version tag or on its manual rehearsal, and can also be run by hand
(`workflow_dispatch`) for a one-off check of a branch. It has one job per platform on
`ubuntu-latest`, `macos-15` and `windows-latest`. Each job types exactly what a developer would: `./run juce`, `./run build`, `./run test-engine`, `./run dist`, under `bash`
(Git Bash on Windows, bash 3.2 on macOS; the MSVC developer environment comes from
`ilammy/msvc-dev-cmd`, the Linux packages from the apt list of "Setting up", macOS needs nothing
beyond the runner image) with `KCF_JOBS=4`. The macOS job also prints `lipo -archs` for both
bundles, which must read `x86_64 arm64`. The engine tests are JUCE-free, six seconds, and the
only proof that the three binaries make the same sound. After them, each job runs the
host-independent checks (`epics/windows-validation`, 2026-09-25, after a tester's Windows crash
that nothing had caught): the Steinberg validator on the bundle (`./run fetch-validator`, built
from the pinned SDK and cached under `external/validator/` by OS and tag, then `./run validator
<bundle>`), pluginval at strictness 5 (`./run fetch-pluginval`, `./run pluginval 5 <bundle>`,
under `xvfb-run` on Linux), on macOS and Windows Steinberg's `editorhost` on the bundle
(`./run fetch-editorhost`, cached like the validator, then `./run editorhost 20 <bundle>`: the
editor open and the host alive after 20 s), and on macOS `auval -v aumu Kcfb Arnv` on the
component, which the job copies into the runner's `~/Library/Audio/Plug-Ins/Components/` first
(the one place a script installs the plug-in anywhere: a throwaway machine). Each check is its own step, so the job
summary names the one that failed, and a failing check fails the job and therefore the release.
The plug-in tests, the REAPER harness and the memory gate still need this development machine.

The Windows build compiles Release with `/Zi` and links with `/DEBUG /OPT:REF /OPT:ICF`; the
`.pdb` files go to `build/symbols/` (never inside the bundle, so `dist.sh` never ships them) and
the job uploads them as `kickcrafter-windows-x86_64-symbols`, kept 90 days. A crash dump (`.dmp`)
from a tester opens in WinDbg with the `.pdb` of the same version: ask for the host and its
version, the moment of the crash (scan, insert, editor, first note, project load) and a dump
(Windows writes none by default: the `LocalDumps` registry key for the host's executable).

Each job uploads two workflow artifacts kept 14 days: `kickcrafter-<platform>` holds
`artifacts/dist/` (the archive `dist.sh` made, with `-<short sha>` after the version when the run
is not on a tag), and `kickcrafter-<platform>-logs` holds `artifacts/logs/` (the configure,
build, engine-test and dist logs with their `.exit` sidecars, the evidence convention of this
project) and is uploaded even when a step failed, since the console only shows the logs' paths.
A superseded run of the same ref is cancelled. The workflow grants itself `contents: read` only.
Minutes: a macOS minute costs ten Linux minutes on a private repository, which is why nothing
runs outside a release. The price is that a change to `main` is not compiled on macOS or Windows
until the next release or a manual run of `Build`.

### Releases

`.github/workflows/release.yml` runs on a tag `v*` and on `workflow_dispatch`. Its first job checks
that the tag is `v<project version>` (`project()` in `CMakeLists.txt`) and that the top heading of
`CHANGELOG.md` is that version; on dispatch the check runs against `HEAD` and only reports. It then
calls `build.yml` unchanged, signs and notarizes the macOS bundles on a `macos-15` runner through
`tools/macos-sign.sh` (import of the Developer ID certificate into a throwaway keychain,
`codesign --force --deep --options runtime --timestamp` on the VST3 and the component, one zip,
`xcrun notarytool submit --wait`, `xcrun stapler staple` on each bundle, the `Signing` line of
`INSTALL.txt` rewritten, the final zip made after stapling, then the verification printed in the
job summary: `codesign --verify --deep --strict` on each bundle, and, when signed,
`codesign --check-notarization -R="notarized"` and `stapler validate`, all three fatal; `spctl` is
printed as a diagnostic only, because Gatekeeper assesses apps, packages and disk images and a bare
plug-in bundle is none of these), and a last job, the only one with `contents: write`, downloads
the three archives, writes `SHA256SUMS` and publishes the GitHub Release with
`fail_on_unmatched_files` (no draft: the owner decided that the workflow releases). Only the push
of a version tag releases: a `workflow_dispatch`, whether on a branch or on a tag, makes that last
job upload the would-be assets as the workflow artifact `release-assets-rehearsal` instead.

The six repository secrets, named as in the owner's other repositories and listed in the header of
`release.yml`: `MACOS_SIGN_IDENTITY`, `MACOS_CERT_P12_BASE64`, `MACOS_CERT_PASSWORD`,
`MACOS_NOTARY_APPLE_ID`, `MACOS_NOTARY_TEAM_ID`, `MACOS_NOTARY_PASSWORD`. When
`MACOS_SIGN_IDENTITY` is absent the script signs ad hoc, skips notarization, and says so in
capitals in the job summary and in `INSTALL.txt` (the user then has to clear the quarantine
attribute); a release made that way is a rehearsal, not something to publish. Windows code signing
is future work.

The release procedure, in this order:

1. On the commit to release: `./run validate`, `./run memory`, `./run memory --diag`, then
   `./run package`, which writes the locally validated binary, source and evidence archives under
   `artifacts/dist/`. They stay on the development machine, with every log under `artifacts/`:
   the release page is for users, the evidence is for the maintainer.
2. Rehearse once: run the `Release` workflow by hand (`workflow_dispatch` on `main`) and read its
   summary: the version check, `codesign --verify` and `--check-notarization` accepting both bundles, notarization
   `Accepted`, `SHA256SUMS` listing the three archives. Download `release-assets-rehearsal` if you
   want to try the archives.
3. Tag: `git tag v<version> && git push origin v<version>`. The workflow publishes the release
   with the three archives and `SHA256SUMS`.
4. Download the three archives once and check them (the listing, `INSTALL.txt`, the checksums
   against `SHA256SUMS`).

The Linux archive in the release is the CI build, compiled by a different GCC than the machine
that ran the gates: its module hash differs from the locally validated one, which is accepted and
stated in the README. The local evidence archive names the validated hash.

## Preset files

A preset is one XML file; the eleven synthesis values are in the units the knobs display,
out-of-range values are clamped on load and malformed files are skipped (the UI lists how many and
Rescan reports why):

```xml
<KickCrafterPreset version="4" name="Deep Sub">
  <Params startFreq="140" endFreq="55" sweep="38" hold="820" fade="88" attack="0.4"
          curve="2.5" shape="0" drive="1.15" velocity="0" pitchSource="0"/>
</KickCrafterPreset>
```

The factory bank is the set of files under `resources/presets/`, compiled into the binary (the
numeric prefix fixes the order): edit or add files there and rebuild to change it. The user library
is `~/.config/KickCrafter/Presets/`, or the folder named by `KCF_PRESET_DIR`, scanned on
demand; a project remembers its preset by kind and name and shows "(missing)" when the file is gone.

## Conventions

- The parameter IDs, the state schema and the preset schema are compatibility contracts: never
  rename an ID; bump the schema and migrate on load when a value's meaning changes. Only the
  plug-in's own state and preset files are migrated; host-side copies of a value (automation lanes,
  stored parameter values) keep their numbers.
- `processBlock` reads the parameter atomics once, renders straight into the host buffer and
  parses MIDI at sample offsets: nothing may allocate, lock, log, post or touch the file system there.
- The editor polls the parameters from its timer and never installs listeners; its edits go through
  begin / value / end gestures.
- Presets, A/B and state save/restore are message-thread transactions under one lock, so a saved
  project always holds a coherent combination of current values, other slot and preset identity. No
  host "Program" parameter is published: presets live in the editor and in the saved state.
- Validate what a change affects, not the folder it lives in. The table below is a floor for
  changes to the binary and a guide for everything else: use judgement. A change that cannot alter
  behaviour on a path (a comment, a help text, a step removed from a gate) needs a syntax check and
  a run of the changed path, not a rerun of what a green gate proved minutes earlier. What to run
  before committing:

  | Changed | Run |
  |---|---|
  | `engine/`, `plugin/`, `resources/`, `CMakeLists.txt` (the binary changes) | `./run validate` (the build chain, validator, ASan engine tests, REAPER stages, harness controls, package controls); `./run memory` when the change touches a lifecycle (editor, processor, presets, dialogs); then `./run memory`, `./run memory --diag` and `./run package` if a release is due |
  | `tests/engine_tests.cpp`, `tests/plugin_tests.cpp` | `./run build kcf_plugin_tests && ./run test-plugin` (or the engine equivalents) |
  | `tests/leak_tests.cpp`, `tests/vst3_host_lifecycle.cpp` | `./run build kcf_leak_tests kcf_vst3_host && ./run memory` (pass A is enough unless the change is about instrumentation) |
  | `tools/reaper/kcf_*.lua`, `analyze_render.py`, `check_saved_state.py`, `drive_mouse.py`, `find_highlight.py`, `image_check.py`, `run_reaper_validation.sh` | `./run reaper` (a single stage, `./run reaper-stage <stage>`, only when the earlier stages' outputs already exist) and `./run reaper-controls` |
  | `tools/build-and-test.sh`, `tools/package*.sh`, `tools/validate-bundle.sh`, `tools/run-logged.sh`, `run` | `./run check` then `./run package-controls` and `./run preflight` |
  | `tools/fetch-validators.sh`, `tools/pluginval.sh`, `tools/editorhost.sh`, `.github/workflows/` | run the command once locally (`./run fetch-validator`, `./run fetch-pluginval`, `./run pluginval`) and read its log; a manual run of the Build workflow for the workflow itself (it spends Actions minutes: the owner's call) |
  | `tools/dist.sh` | `./run dist` and read the listing it prints; `./run package` if the staging it does for `package.sh` changed |
  | `tools/memory-check.sh` | `./run memory` and `./run memory --diag` |
  | `tools/xvfb-display.sh`, `tools/fetch-juce.sh`, `tools/diagrams/` | run the script once and look at what it produced (`./run displays`, `./run juce`, `./run diagram`) |
  | `README.md`, `docs/`, `CHANGELOG.md`, `CLAUDE.md`, `THIRD_PARTY_NOTICES.md`, `.claude/`, `epics/` | nothing (render a diagram if you changed its generator) |

  When in doubt, run more: a CI run on every commit is the safety net for judgement errors here.
- Keep failed logs; never treat a trailing `echo` as a test status.
