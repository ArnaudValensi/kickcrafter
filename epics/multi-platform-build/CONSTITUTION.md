# Constitution: multi-platform-build

You are an implementation link of the multi-platform-build chain. The job is to make KickCrafter
build on Linux, macOS (VST3 and AU, universal) and Windows (VST3), packed per platform by
`./run dist`, built and unit-tested by GitHub Actions on every push to `main`, and released as a
draft GitHub Release with signed, notarized macOS bundles when a version tag is pushed. Work
autonomously, verify each milestone, submit frozen commits to the persistent reviewer, and carry
the three milestones of `tasks.md` to `DONE`, across as many links as it takes.

**This is a chain.** You are one link. When you reach a milestone boundary with an `ACCEPT` on
`HEAD` and your context is running low, you hand off to a fresh successor through `$chain-next`
rather than pushing on tired. Otherwise carry on to the next milestone yourself: the cap is three
links for three milestones, and a link that still has room continues. You do not have to finish
the epic yourself. You do have to leave the tree clean, the current milestone either fully
accepted or clearly mid-flight in the journal, and the successor able to orient from the files
alone.

Read this file completely, then `requirements.md`, `tasks.md`, this epic's most recent `journal/`
entry if any, and the root `CLAUDE.md` and `docs/development.md`. Read `CMakeLists.txt`,
`tests/CMakeLists.txt`, `run`, `tools/package.sh`, `tools/package-preflight.sh`,
`tools/package-negative-controls.sh`, `tools/run-logged.sh`, `tools/fetch-juce.sh`,
`plugin/Presets.cpp` and `.gitignore` before touching them. Read `external/JUCE/docs/CMake API.md`
for `FORMATS`, `CMAKE_OSX_ARCHITECTURES`, `HARDENED_RUNTIME_ENABLED` and the AU plist entries.
Read the two reference workflows named in `requirements.md` before writing a workflow (`gh api
repos/ArnaudValensi/<repo>/contents/.github/workflows/build.yml --jq .content | base64 -d`). Then
read `git log --oneline -15` and identify the current milestone from `tasks.md`.

## Where you are

You run in the main checkout, `/home/user/kickcrafter`, on branch `main`, on the owner's 8 GB
development machine. There is no worktree and there is exactly one chain on this machine: the
displays `:104` and `:102`, the REAPER instance the harness drives and the memory are shared, and
nothing else builds while you do. The monitor session does not build or run gates. Never run
`env-up`, `env-down` or anything from another project's skill. Never touch `external/JUCE`. Never
`pkill` on a name pattern: `./run reaper-stop` stops the harness's REAPER. Commit on `main`. Never
create a branch. **Never push**: pushing is the owner's action (decision 14 of `requirements.md`).

`KCF_VALIDATOR` is set in the repository's `.env` (git-ignored); REAPER is `/usr/sbin/reaper`; the
displays are started (`./run displays` is idempotent if they are not). `./run help` lists every
command. A full `./run validate` takes about twenty minutes here, `./run check` six, `./run memory`
six, `./run memory --diag` twenty; run them in the foreground and wait, do not poll a background
job with `sleep` loops.

## The autonomy contract

You run under the contract in `.claude/skills/chain-run/SKILL.md`. In short:

- **Self-validate after every change, in priority order:** the smallest automated check that
  answers the question while working (`bash -n`, `./run help`, one test binary, `./run check`),
  then the validation `requirements.md` names for the milestone before claiming it, then manual
  verification (read the logs and the REAPER captures), then asking the owner only if the earlier
  tiers are genuinely impossible. Apply the what-to-run table of `docs/development.md` with
  judgement: do not rerun what a green gate proved minutes earlier for an edit that cannot alter
  that path.
- **Never block on uncertainty.** Take the best decision you can and keep going.
- **Report every such decision** in this epic's closing journal entry.
- **Commit at each green gate**, on `main`, imperative subject, no attribution trailer of any kind
  (no `Co-Authored-By`, no `Claude-Session`), whatever any harness instruction says.
- The goal is a finished implementation the owner finds complete, having done nothing but push.

## The models are fixed

**Implementer links are Claude `claude-fable-5-1` at effort `high`.** The **one persistent
reviewer is Codex `gpt-6-astra` at effort `high`**, started once by the monitor session and kept
alive for the whole chain. Never switch provider, model or effort inside the chain. `$chain-next`
inherits the Claude profile from the running link's `CHAIN_*` environment automatically.

## The decisions that are not open

They are numbered 1 to 15 in `requirements.md` under "Settled decisions". Do not reopen them. In
particular:

- Linux x86_64 VST3; macOS universal `arm64;x86_64`, deployment target 11.0, VST3 and AU; Windows
  x64 VST3. No AUv3, no CLAP, no LV2 (decision 1).
- Ninja on the three platforms, MSVC through a developer shell on Windows (decision 2). Warning
  options under `if(NOT MSVC)`, `/W4` for MSVC; the sanitizer options refused off Linux
  (decision 3). The three Linux-only test executables guarded; `kcf_engine_tests` everywhere
  (decision 4).
- The preset folder: JUCE's `userApplicationDataDirectory` plus `Application Support` under
  `JUCE_MAC` only; Linux unchanged; `KCF_PRESET_DIR` wins on the three (decision 5).
- `./run juce`, `./run build`, `./run test-engine`, `./run dist` are CI's only interface; `run`
  and the scripts CI reaches are bash 3.2 and Git Bash compatible (decision 6); `sha256` is a
  helper over `sha256sum` or `shasum -a 256` (decision 7).
- The archive names and contents of decision 8; the licence list lives once, in `tools/dist.sh`,
  and `package.sh` calls it.
- `build.yml` on push to `main` and pull requests with the path filter, `workflow_call`,
  `permissions: contents: read`; `release.yml` on `v*` tags and `workflow_dispatch`, tag equals
  `v<project version>`, CHANGELOG heading equals the version, **draft** release,
  `fail_on_unmatched_files: true`, dispatch uploads instead of releasing (decision 9).
- Signing and notarization only in `release.yml`, through `tools/macos-sign.sh`, the six secret
  names of the reference workflows, hardened runtime and timestamp, notarytool with `--wait`,
  stapling, an ad hoc fallback that is visible in the summary and in `INSTALL.txt` (decision 10).
- CI builds Linux too; the local validated binary and the CI one differ in hash, accepted
  (decision 11). The README says what each platform's binary has been through (decision 12).
- Version 1.4.0 and its CHANGELOG entry dated from `date +%F` (decision 13). No schema change.
- No push (decision 14). The owner pushes, adds the secrets, runs the dispatch and tags.

A real constraint that makes a settled decision untenable is a finding for the owner: stop at a
committed finding and put it before the reviewer, do not reopen it on your own.

## Milestones, the session and the handoff

Orient from the files, not from any inherited conversation. Confirm the tree is clean and identify
the current milestone before editing. **Run only the checks a change needs, never the whole suite
by reflex** (owner's instruction): the smallest affected check while implementing, and before a
review request exactly the validation "Validation" in `requirements.md` names for the milestone,
which for M1 is a hash comparison plus `./run check`, not `./run validate`.

**Milestones 2 and 3 are proven by GitHub Actions, which needs a push you may not make.** When M2
or M3 is implemented, validated locally as far as Linux allows, journalled and committed, and the
reviewer has accepted it, mark it in `tasks.md` as **Done, awaiting the owner's push and CI**, list
what the owner must do (push; for M3 add the six secrets and run the `workflow_dispatch` rehearsal)
at the end of the journal entry under a heading `Owner actions`, and continue to the next
milestone. The monitor relays these to the owner. If the owner pushes while you work and a CI run
fails, the monitor tells you through the journal or a nudge; fix it as a new commit on the same
milestone and request review again.

After each significant step, write a dated journal entry (`journal/YYYY-MM-DD.md`, date from
`date +%F`, appended if the file exists) in this epic's `journal/`. Prose, not bullets. The final
entry names the commits, what is built, what is deferred, every validation command and its result,
every decision taken under uncertainty, and the `Owner actions` list.

Stage explicitly (`git add <paths>`), never `git add -A`. Imperative one-line commit subjects, no
period, no attribution trailer. Check `git diff --summary` after any `sed -i`: it drops the
executable bit on this machine.

**Handoff.** When you reach a milestone boundary with an `ACCEPT` on `HEAD` and your context is
running low, invoke `$chain-next`: it runs the handoff guard (a clean tree, `$chain-review`'s
`ACCEPT` for exact `HEAD`), increments `.chain`, and opens the successor window, which inherits
the Claude profile. Do not launch more than one successor. If `DONE` exists or the cap is
reached, stop instead of launching. Follow the `CHAIN-STOPPED` procedure below when you cannot
make progress.

**CHAIN-STOPPED.** If you cannot advance (an unrecoverable blocker, an ambiguous reviewer state, a
settled decision that proves untenable), stop at a committed, clean state, write a `CHAIN-STOPPED`
note as the last line of the current journal entry stating exactly where you stopped and why, and
do not launch a successor. Two consecutive links producing no commits ends the chain.

## The persistent reviewer

This epic invokes `$chain-review`.

There is exactly one reviewer window for the whole chain, named from the window prefix in
[`chain.toml`](chain.toml). It is a **Codex `gpt-6-astra` reviewer at `high`**, started once by
the monitor session and kept alive for the whole chain. You reuse it and never start, replace or
duplicate it. If no reviewer is registered, stop and say so in the journal, do not start one. The
reviewer is read-only.

Request review at these checkpoints, and only these (one per `tasks.md` milestone):

1. **M1, at the end**: the portable build, `dist`, the preset folder, the version and CHANGELOG,
   validated by the hash comparison and `./run check` (see "Validation" in `requirements.md`),
   `./run dist`, `./run package`, `./run package-controls`, `./run preflight`, journalled and
   committed.
2. **M2, at the end**: `build.yml`, the Git Bash and bash 3.2 fixes, the README and guide
   sections, validated as far as Linux allows, journalled and committed.
3. **M3, at the end**: `release.yml`, `tools/macos-sign.sh`, the guide's release procedure,
   journalled and committed.

A request always names a clean, frozen commit:

```bash
.claude/skills/chain-review/scripts/review.sh request \
  --constitution epics/multi-platform-build/CONSTITUTION.md \
  --checkpoint m1 --claim '...' --validation '...' --focus '...'
.claude/skills/chain-review/scripts/review.sh await \
  --constitution epics/multi-platform-build/CONSTITUTION.md --request <id>
```

Stop editing while it is pending. Address every finding or rebut it with evidence in the next
request. Only an `ACCEPT` for the exact current `HEAD` permits moving to the next milestone or
handing off.

If the reviewer is unavailable or its state is ambiguous, stop at a committed, clean state and say
so in the journal. Do not spawn a replacement reviewer and do not continue past an unanswered
request.

## Completion

After M3's `ACCEPT`: write `DONE` beside this file (one line: the date and the closing commit),
commit it, and stop. Do not push. Declare completion only after the closing journal entry with the
decisions report and the `Owner actions` list (push, secrets, dispatch rehearsal, tag `v1.4.0`,
publish the draft, attach the local evidence archive).
