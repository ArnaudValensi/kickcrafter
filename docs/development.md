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
./run validate    # the full gate: check, ASan engine tests, REAPER stages, harness controls, both memory passes, package controls
```

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

### 5. REAPER (host harness)

Download the Linux x86_64 build from [reaper.fm](https://www.reaper.fm/download.php) (tested with
7.80; the evaluation licence is enough) and make `reaper` reachable: `KCF_REAPER` names the
executable (default `/usr/sbin/reaper`). The harness runs REAPER with its own configuration under
`artifacts/reaper/config/` and never touches `~/.config/REAPER`.

### 6. Environment variables

| Variable | Read by | Meaning (default) |
|---|---|---|
| `KCF_VALIDATOR` | `validate-bundle.sh`, `memory-check.sh` | Steinberg validator executable (`validator` on `PATH`) |
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
`./run validate` an hour or more at one job: run them in a persistent shell (tmux, screen) so they
survive a closed terminal.

## Building from source

```sh
git clone https://github.com/ArnaudValensi/kickcrafter.git
cd kickcrafter
tools/fetch-juce.sh                                   # once: JUCE 8.0.9, pinned by commit, into external/JUCE
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target KickCrafterFable_VST3
mkdir -p ~/.vst3 && cp -r "build/KickCrafterFable_artefacts/Release/VST3/KickCrafter Fable.vst3" ~/.vst3/
```

Requirements: section 1 of "Setting up" above. `./run build` configures and builds every target
below into `build/`; the equivalent by hand:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target KickCrafterFable_VST3 KickCrafterFable_Standalone \
                              kcf_engine_tests kcf_plugin_tests kcf_leak_tests kcf_vst3_host
```

Outputs: `build/KickCrafterFable_artefacts/Release/VST3/KickCrafter Fable.vst3`, the Standalone
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
`artifacts/dist/` (binary with all licence texts, source with the pinned JUCE tree, evidence) plus
`SHA256SUMS`.

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
ninja -C build-leak KickCrafterFable_VST3 kcf_plugin_tests kcf_leak_tests kcf_vst3_host
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

## Preset files

A preset is one XML file; the eleven synthesis values are in the units the knobs display,
out-of-range values are clamped on load and malformed files are skipped (the UI lists how many and
Rescan reports why):

```xml
<KickCrafterPreset version="4" name="Deep Sub">
  <Params startFreq="140" endFreq="55" sweep="38" hold="820" fade="88" attack="0.4"
          curve="2.5" shape="0" drive="1.15" velocity="1" pitchSource="0"/>
</KickCrafterPreset>
```

The factory bank is the set of files under `resources/presets/`, compiled into the binary (the
numeric prefix fixes the order): edit or add files there and rebuild to change it. The user library
is `~/.config/KickCrafterFable/Presets/`, or the folder named by `KCF_PRESET_DIR`, scanned on
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
- Validate what a change affects, not the folder it lives in. What to run before committing:

  | Changed | Run |
  |---|---|
  | `engine/`, `plugin/`, `resources/`, `CMakeLists.txt` (the binary changes) | `./run validate` (the build chain, validator, ASan engine tests, REAPER stages, harness controls, both memory passes, package controls); then `./run package` if a release is due |
  | `tests/engine_tests.cpp`, `tests/plugin_tests.cpp` | `./run build kcf_plugin_tests && ./run test-plugin` (or the engine equivalents) |
  | `tests/leak_tests.cpp`, `tests/vst3_host_lifecycle.cpp` | `./run build kcf_leak_tests kcf_vst3_host && ./run memory` (pass A is enough unless the change is about instrumentation) |
  | `tools/reaper/kcf_*.lua`, `analyze_render.py`, `check_saved_state.py`, `drive_mouse.py`, `find_highlight.py`, `image_check.py`, `run_reaper_validation.sh` | `./run reaper` (a single stage, `./run reaper-stage <stage>`, only when the earlier stages' outputs already exist) and `./run reaper-controls` |
  | `tools/build-and-test.sh`, `tools/package*.sh`, `tools/validate-bundle.sh`, `tools/run-logged.sh`, `run` | `./run check` then `./run package-controls` and `./run preflight` |
  | `tools/memory-check.sh` | `./run memory` and `./run memory --diag` |
  | `tools/xvfb-display.sh`, `tools/fetch-juce.sh`, `tools/diagrams/` | run the script once and look at what it produced (`./run displays`, `./run juce`, `./run diagram`) |
  | `README.md`, `docs/`, `CHANGELOG.md`, `CLAUDE.md`, `THIRD_PARTY_NOTICES.md`, `.claude/`, `epics/` | nothing (render a diagram if you changed its generator) |

  When in doubt, run more: a CI run on every commit is the safety net for judgement errors here.
- Keep failed logs; never treat a trailing `echo` as a test status.
