# Tasks: the Cubase 11 crash on Windows

## T1: The Cubase sequence in the Linux VST3 host test

**Done** (2026-09-26): `kcf_vst3_host` plays the content-scale and negotiation sequence; 2 cycles
on the staged 1.5.3 module: 16 scale factors applied, 64 negotiations, no runaway, no crash, PASS.
`./run memory` pass A green with it (`artifacts/logs/memory/20260926T094529Z-passA`: 36 scale factors, 144
negotiations across the cycles, no leak). Finding: not reproduced through JUCE's
wrapper on Linux.

## T2: editorhost on the CI runners

`tools/fetch-validators.sh editorhost`, `tools/editorhost.sh`, `./run fetch-editorhost`,
`./run editorhost`, the two workflow steps on macOS and Windows with the cache, the documents.
Acceptance: the probe's alive and dead paths self-tested locally with fake hosts, `./run check`,
`./run package-controls`, `./run preflight` green, a manual run of the Build workflow green on
macOS and Windows with the editorhost logs read (or the failure read: the finding).

## T3: The diagnostic build

The `KCF_CRASH_DUMP` option and the minidump writer (Windows only), documented for the tester in
one paragraph the owner can paste; built by a manual workflow run with the option on (a
`workflow_dispatch` input), the archive and its `.pdb` handed to the tester. Acceptance: a
deliberate crash in a test build produces a readable dump on the Windows runner.

## T4: Any size accepted, no `%` menu

Per "Also decided". Acceptance: `./run validate`, the editor tests updated (no scale menu, the
letterboxed layout at off-aspect sizes), captures read.
