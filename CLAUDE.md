# CLAUDE.md — KickCrafter

Entry point for an AI agent (or a human) starting a session on this repository. Read it, then the
documents it points to; do not duplicate their content here.

## What this is

A Linux x86_64 VST3 kick-drum instrument (JUCE 8, C++20, CMake, AGPL-3.0-or-later). One kick per
instance, every Note On freezes all parameters into its voice. The plug-in is named "KickCrafter
Fable" inside hosts (bundle, plug-in code); the project and repository are "KickCrafter".

## Where things are

| Need | Read |
|---|---|
| What the plug-in does, for users | `README.md` (users only: no build or internals there) |
| How the synthesis works | `docs/architecture.md` (one diagram, one table) |
| Build, tests, VST3 validator, REAPER host harness, memory gate, packaging, conventions | `docs/development.md` |
| Regenerating the synthesis diagram, and its style rules | `docs/diagrams/README.md` (`tools/diagrams/synthesis_diagram.py` is the source of the SVG) |
| What changed between versions | `CHANGELOG.md` |
| Third-party licences | `THIRD_PARTY_NOTICES.md` |

Layout: `engine/` (JUCE-free synthesis), `plugin/` and `plugin/ui/` (JUCE integration, editor),
`resources/` (fonts, factory presets), `tests/`, `tools/`, `docs/`. Build trees, `artifacts/` and
`external/JUCE` are generated and git-ignored.

## Rules that are easy to break

- Parameter IDs, the state schema and the preset schema are compatibility contracts: never rename an
  ID; when a value's meaning changes, bump the schema and migrate on load (see `docs/development.md`).
- Nothing allocates, locks or posts in `processBlock`; the editor polls parameters, it never listens.
- Any change under `engine/`, `plugin/`, `resources/` or `CMakeLists.txt` needs the full validation
  described in `docs/development.md` (chain, validator, REAPER stages, memory passes) before it is
  packaged or released; `tests/` and `tools/` changes need the chain and the affected evidence.
- Documentation stays in its lane: user-facing text in `README.md`, developer text in `docs/`, no
  version history outside `CHANGELOG.md`, no internal process notes in the repository.
- Diagrams are generated: edit the generator, render, look at the result, commit script and SVG together.
- Never edit `external/JUCE` (pinned by `tools/fetch-juce.sh`) and never install the plug-in into
  system folders from scripts.

## Working conventions

- Prefer small, self-contained commits with messages that say what changed and why.
- Keep every run log the tools produce; never treat a trailing `echo` as a test status.
- Commit only; pushing, releasing and changing repository settings are the owner's decisions.
