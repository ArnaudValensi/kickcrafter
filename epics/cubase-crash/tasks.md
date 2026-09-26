# Tasks: the Cubase 11 crash on Windows

## T1: The Cubase sequence in the Linux VST3 host test

**Done** (2026-09-26): `kcf_vst3_host` plays the content-scale and negotiation sequence; 2 cycles
on the staged 1.5.3 module: 16 scale factors applied, 64 negotiations, no runaway, no crash, PASS.
`./run memory` pass A green with it (`artifacts/logs/memory/20260926T094529Z-passA`: 36 scale factors, 144
negotiations across the cycles, no leak). Finding: not reproduced through JUCE's
wrapper on Linux.

## T2: editorhost on the CI runners

**Done** (2026-09-26): run 36233946827 green on the three platforms; editorhost alive after 20 s
with the editor open on macOS and on Windows (exit 0 both). Finding: not reproduced with
Steinberg's own editor host at 100 % screen scale. Details: `journal/2026-09-26.md`.

`tools/fetch-validators.sh editorhost`, `tools/editorhost.sh`, `./run fetch-editorhost`,
`./run editorhost`, the two workflow steps on macOS and Windows with the cache, the documents.
Acceptance: the probe's alive and dead paths self-tested locally with fake hosts, `./run check`,
`./run package-controls`, `./run preflight` green, a manual run of the Build workflow green on
macOS and Windows with the editorhost logs read (or the failure read: the finding).

## T3: The diagnostic build

**Done** (2026-09-26): `KCF_CRASH_DUMP`, `plugin/CrashDump_win32.cpp`, the `crash_dump` input of
the Build workflow, `KCF_CMAKE_OPTIONS`, `KCF_DIST_SUFFIX`, the self-test step; run 36235673464
green with the deliberate crash caught: a 975 KB dump and its note on the runner. Details:
`journal/2026-09-26.md`. What remains is the tester's dump.

The `KCF_CRASH_DUMP` option and the minidump writer (Windows only), documented for the tester in
one paragraph the owner can paste; built by a manual workflow run with the option on (a
`workflow_dispatch` input), the archive and its `.pdb` handed to the tester. Acceptance: a
deliberate crash in a test build produces a readable dump on the Windows runner.

## T4: Any size accepted, no `%` menu

Per "Also decided". Acceptance: `./run validate`, the editor tests updated (no scale menu, the
letterboxed layout at off-aspect sizes), captures read.
