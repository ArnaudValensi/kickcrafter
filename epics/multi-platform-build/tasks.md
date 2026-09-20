# Tasks: multi-platform build and binary releases

Three milestones. Each ends with the validation `requirements.md` names for it, a journal entry in
this epic's `journal/` (dated from `date +%F`), and a commit; milestones 2 and 3 also end with a
request to the owner to push, because only GitHub Actions can prove them (decision 14). Mark a
milestone **Done** here, with a short record of what is in place, in the commit that closes it.

## M1: A portable build, proven on Linux

The CMake project compiles with GCC, Apple Clang and MSVC without changing what the Linux build
does. `CMakeLists.txt`: version 1.4.0, `FORMATS VST3 AU Standalone`, the Apple architecture and
deployment-target defaults, warning options by compiler, the two sanitizer options refused off
Linux, the stale `docs/architecture.md` reference corrected. `tests/CMakeLists.txt`: the three
Linux-only test executables guarded, warning options by compiler. `plugin/Presets.cpp`: the macOS
`Application Support` component. `tools/run-logged.sh`: the `sha256` helper. `run`: platform-aware
default targets, the `dist` command. `tools/dist.sh`: the per-platform archive with the licence
list and `INSTALL.txt`; `tools/package.sh` uses it and the archive names carry the version;
`package-preflight.sh` and `package-negative-controls.sh` follow the names. `CHANGELOG.md` gains the
1.4.0 entry (dated from the clock) and `CLAUDE.md`'s closed decision on platforms is updated.

Acceptance: `./run validate` green (23 engine cases, 28 plug-in cases, 47 validator tests, eleven
REAPER stages PASS), `./run memory` green, `./run dist` produces
`artifacts/dist/kickcrafter-fable-1.4.0-linux-x86_64.tar.gz` whose listing contains the bundle,
the five documents, `INSTALL.txt` and the licence files, `./run package` still produces its three
archives and `SHA256SUMS` on the committed tree, `./run package-controls` and `./run preflight`
green after the commit, the journal entry written.

## M2: The build workflow on the three platforms

`.github/workflows/build.yml` per decision 9: push to `main` and pull requests with the path
filter, concurrency, three jobs, each `./run juce`, `./run build`, `./run test-engine`,
`./run dist`, artifacts uploaded, `workflow_call` exposed. `tools/fetch-juce.sh` and `run` fixed
where Git Bash or bash 3.2 need it. `docs/development.md` gains the "Continuous integration and
releases" section's first half (what CI builds and tests, where the artifacts are) and the macOS
and Windows prerequisites under "Building from source". The README's installation section gets
its per-platform subsections and the preset folders (decision 5 and 12), the limitations line
updated.

Acceptance: `./run validate` green locally (only if `run` or the tools changed in a way that
touches the Linux path; otherwise `./run check`), the commit clean, the owner asked to push; then
three green jobs on GitHub for that commit, each with a downloadable archive; the macOS archive
contains both bundles and `lipo -archs` printed in the job shows `x86_64 arm64` for each; the
engine tests report 23 cases and 0 failures in the three logs; the journal entry written.

## M3: The release workflow, signed macOS bundles, version 1.4.0

`.github/workflows/release.yml` per decision 9 and `tools/macos-sign.sh` per decision 10: the
tag and CHANGELOG check, the reused build, the signing and notarization job with the ad hoc
fallback, `SHA256SUMS`, the draft release, the `workflow_dispatch` rehearsal that uploads instead
of releasing. The workflow header lists the six secrets. `docs/development.md` completes the
section with the release procedure of decision 11 and the rehearsal.

Acceptance: the commit clean, the owner asked to push, to add the six secrets, and to run the
dispatch; then a green rehearsal whose macOS step prints `codesign --verify --deep --strict` and
`spctl` verdicts for the two bundles and a notarization `Accepted`, whose Linux and Windows
archives are present, and whose `SHA256SUMS` lists the three; the journal's closing entry names
every decision taken under uncertainty; the owner tags `v1.4.0` and publishes the draft (their
actions, not the epic's).
