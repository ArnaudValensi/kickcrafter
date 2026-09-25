# Tasks: host-independent validation in CI, and Windows symbols

Two milestones. Each ends with the validation `requirements.md` names (decision 8), a journal entry
in this epic's `journal/` (dated from `date +%F`) and a commit. Mark a milestone **Done** here, with
a short record of what is in place, in the commit that closes it.

## M1: The tools and the workflow

**Done** (2026-09-25): `tools/fetch-validators.sh` (SDK `v3.8.1_build_84` with VSTGUI and the
examples off, pluginval 1.0.4), `tools/validate-bundle.sh` platform-aware with the fallback,
`tools/pluginval.sh`, the four `run` commands, the MSVC symbols to `build/symbols/`, the workflow
steps, `.gitignore`, `.env.example`, the documents. Linux module `a7f6deaad9190337` with and
without the CMake edit; `./run check` and `./run package-controls` green; validator through the
fallback 47/47; pluginval strictness 5 SUCCESS. Details: `journal/2026-09-25.md`.

`tools/fetch-validators.sh` (validator from the SDK at the pinned tag, pluginval at the pinned
version, idempotent, three platforms), `tools/validate-bundle.sh` platform-aware with the fallback
to `external/validator/`, `tools/pluginval.sh`, the three `run` commands and their help rows, the
MSVC symbols in `CMakeLists.txt` with `PDB_OUTPUT_DIRECTORY`, the workflow steps (cache,
validator, pluginval, auval, symbols), `.gitignore`, `.env.example`. Acceptance: the Linux module
hash unchanged (journal), `./run check`, `./run package-controls`, `./run preflight` green, the
four new commands run locally with their logs read.

## M2: The rehearsal and the documents

Push `main`, run the Build workflow by hand, read the three jobs (the validator and pluginval logs
in the uploaded artifacts, the symbols artifact present for Windows). Record the outcome in the
journal: green, or the Windows failure as the first finding about the tester's crash. Documents
per decision 9. Acceptance: the manual run's id and verdict in the journal, the documents
committed.
