# Host-independent validation in CI, and Windows symbols

Status: done (2026-09-25, runs 36176223559 and 36177513623). Decisions taken with the owner on 2026-09-25 after a tester
reported a crash of the Windows VST3 in a host; the alternatives that lost are recorded under each
decision.

Today CI compiles the plug-in on Linux, macOS and Windows and runs the JUCE-free engine tests:
nothing loads the bundle, instantiates the plug-in or opens its editor outside the development
machine, so a Windows or macOS crash can ship without any signal. After this epic every CI build
runs the checks that need no host and no harness: the Steinberg VST3 validator (load, class
factory, instantiation, processing, state), pluginval (the editor, the parameters, block-size and
sample-rate changes, state, threads), `auval` for the Audio Unit on macOS; and the Windows build
publishes its symbols (`.pdb`) so a crash dump from a tester can be read with a real stack. The
REAPER harness and the memory gate stay on the development machine.

This file is self-sufficient: read it, then `CLAUDE.md`, `docs/development.md` (sections
"Setting up", "Continuous integration and releases", "Conventions"), `run`,
`tools/validate-bundle.sh`, `tools/run-logged.sh`, `tools/dist.sh`, `.github/workflows/build.yml`
and `CMakeLists.txt`.

## Scope

In: `tools/fetch-validators.sh` (the SDK validator built from a pinned tag, pluginval downloaded
at a pinned version, both under `external/`, idempotent, the same on the three platforms);
`tools/validate-bundle.sh` made platform-aware; `tools/pluginval.sh` with the same logging as the
validator; `./run fetch-validator`, `./run fetch-pluginval`, `./run pluginval`; the CI steps with
a cache for the built validator; the `auval` step on macOS; the Windows `.pdb` as a CI artifact,
never inside the bundle; the documents and the amended rule in `CLAUDE.md`.

Out: running the plug-in tests, the REAPER harness or the memory gate off Linux (the tests are
X11-bound by design); Windows code signing; a Windows VM or Wine on the development machine (a
reproduction tool, not a gate, to be decided if the CI checks do not reproduce the crash); a
version bump (nothing user-visible changes; the next release's CHANGELOG entry mentions the
published symbols).

## Settled decisions

1. **CI runs the host-independent checks; the rule changes.** `CLAUDE.md`'s closed decision "CI
   builds, it does not validate" becomes "CI builds and runs the checks that need no host and no
   display harness (Steinberg validator, pluginval, auval); the REAPER harness and the memory
   gate stay local". The reasoning behind the old rule was where REAPER, the displays and the
   validator lived; the validator and pluginval build or download in minutes on a runner, and
   for Windows and macOS there is no other machine. Nothing else about CI changes: it still runs
   only on a version tag or by hand (no Actions minutes on ordinary pushes).

2. **The Steinberg validator, built from the SDK at a pinned tag, on the three platforms.** Tag
   `v3.8.1_build_84` of `https://github.com/steinbergmedia/vst3sdk` (commit `3cdf9ca5d1`), the
   generation the development machine uses (VST 3.8.1). `tools/fetch-validators.sh validator`
   clones it shallowly with its submodules into `external/vst3sdk/`, configures with Ninja in
   Release, builds the `validator` target only, copies the executable to
   `external/validator/validator` (`.exe` on Windows) and prints that path; when the executable
   already exists it prints the path and does nothing (so a CI cache of `external/validator/`
   keyed by OS and tag skips the build). `tools/validate-bundle.sh` finds the validator through
   `KCF_VALIDATOR`, then `validator` on `PATH`, then `external/validator/`; it takes the bundle
   path as its argument (default: the staged `artifacts/vst3/KickCrafter.vst3`, which only the
   Linux build chain produces; CI passes the bundle under `build/`) and derives the module path
   by platform: `Contents/x86_64-linux/KickCrafter.so`, `Contents/MacOS/KickCrafter`,
   `Contents/x86_64-win/KickCrafter.vst3`. Its log keeps the same header (module hash, tool hash,
   command, real exit code) and works with bash 3.2 and Git Bash (no `sha256sum`, no `%N`).
   Alternative rejected: a prebuilt validator (Steinberg ships none).

3. **pluginval 1.0.4, downloaded, strictness 5.** `tools/fetch-validators.sh pluginval`
   downloads `pluginval_<Linux|macOS|Windows>.zip` of release `v1.0.4` of
   `https://github.com/Tracktion/pluginval` into `external/pluginval/`, unzips it and prints the
   executable (`pluginval`, `pluginval.app/Contents/MacOS/pluginval`, `pluginval.exe`);
   idempotent. `tools/pluginval.sh [--level N] [bundle]` runs
   `pluginval --strictness-level N --validate-in-process --output-dir artifacts/logs/pluginval
   --validate <bundle>` with the same log header as the validator, default level 5 (levels above
   5 add timing-sensitive checks that fail on loaded runners; the owner can raise it locally),
   and the real exit code. On Linux the CI step runs it under `xvfb-run` (pluginval opens the
   editor); on macOS and Windows the runners have a desktop. Only the VST3 goes through pluginval
   in this epic; the AU is covered by decision 4. Alternative rejected: building pluginval from
   source (a second JUCE build per job).

4. **`auval` on macOS.** After the build, the CI job copies `KickCrafter.component` into the
   runner's `~/Library/Audio/Plug-Ins/Components/`, resets the component registrar and runs
   `auval -v aumu Kcfb Arnv` through `tools/run-logged.sh` (the AU type is `aumu`, the codes are
   `PLUGIN_CODE` and `PLUGIN_MANUFACTURER_CODE` of `CMakeLists.txt`). This is the one place a
   script installs the plug-in into a plug-in folder: a throwaway runner, in the workflow only,
   never in `tools/`; `CLAUDE.md`'s rule gains that exception in the same sentence.

5. **Windows symbols.** With MSVC, Release compiles with `/Zi` and links with `/DEBUG /OPT:REF
   /OPT:ICF` (so the symbols cost nothing in the binary's size or speed); the linker's `.pdb` of
   the VST3 and the Standalone are directed to `build/symbols/` through the `PDB_OUTPUT_DIRECTORY`
   target property, so `./run dist` (which copies the bundle whole) never ships them. The CI job
   uploads `build/symbols/` as the artifact `kickcrafter-windows-x86_64-symbols`, kept 90 days
   (a tester's dump may arrive weeks after a release). The Linux and macOS builds are untouched:
   the change is under `if(MSVC)`, and the Linux module's hash before and after is recorded in
   the journal as proof.

6. **Placement in the workflow.** After "Engine tests" and before "Archive", in this order:
   restore the validator cache, `./run fetch-validator`, `./run validator <bundle>`,
   `./run fetch-pluginval`, `./run pluginval 5 <bundle>` (under `xvfb-run -a` on Linux), the
   `auval` step (macOS), the symbols upload (Windows). Each check is its own step so the job
   summary names the one that failed, and every one writes its log under `artifacts/logs/`,
   which the existing "Upload the logs" step keeps even on failure. A failing check fails the
   job, and therefore the release: a plug-in the validator refuses is not published.

7. **Local use.** The same commands work on the development machine: `./run fetch-validator`
   is an alternative to building the SDK by hand (`.env`'s `KCF_VALIDATOR` still wins),
   `./run fetch-pluginval` then `./run pluginval [level] [bundle]` runs pluginval on the staged
   bundle. Neither joins `./run check` or `./run validate` in this epic (the owner decides after
   seeing pluginval's runtime and noise on the development machine); `KCF_PLUGINVAL` names the
   executable, documented in the environment variables table and in `.env.example`.

8. **Validation of this epic.** The conventions table covers it: `./run check` then
   `./run package-controls` and `./run preflight` for the tools and `run`; the Linux module hash
   unchanged by the CMake edit; `./run fetch-validator`, `./run validator`, `./run fetch-pluginval`
   and `./run pluginval` run once locally and their logs read. Then a manual run of the Build
   workflow on `main` (`workflow_dispatch`: a rehearsal, no tag, no release) with the three jobs
   green, or, if the Windows checks fail, their logs read and the failure recorded in the
   journal as the epic's first finding about the tester's crash. The owner authorised the push
   of `main` and that manual run on 2026-09-25.

9. **Documents.** `docs/development.md`: section 4 of "Setting up" gains the `fetch-validator`
   shortcut and a section for pluginval; the environment variables table gains `KCF_PLUGINVAL`;
   "Continuous integration and releases" describes the checks, the cache and the symbols
   artifact; the conventions table gains the two new scripts. `CLAUDE.md`: the amended rule
   (decision 1), the `auval` exception (decision 4), the two commands. `.gitignore`:
   `external/vst3sdk/`, `external/validator/`, `external/pluginval/`. `.env.example`:
   `KCF_PLUGINVAL`. No CHANGELOG entry until the next release.
