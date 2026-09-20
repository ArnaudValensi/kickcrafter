---
name: chain-run
description: Launch and run an autonomous implementation chain over an epic. Use when an epic is ready to be implemented end to end without the user in the loop: it starts the persistent reviewer, launches the first implementation link, and defines the autonomy contract every link runs under. The launching session stays as the monitor.
argument-hint: [epic directory, e.g. epics/multi-platform-build]
---

# Chain run

Run an epic to completion autonomously. A chain of disposable implementer links
moves the work forward, each handing off to a fresh successor at a milestone
boundary or when its context fills. One persistent reviewer guards the whole
chain. The session that launches the chain stays alive as the monitor.

This skill owns the launch and the autonomy contract. The mechanics of launching
a successor and running the reviewer live in [`chain-next`](../chain-next/SKILL.md)
and [`chain-review`](../chain-review/SKILL.md).

## The autonomy contract

Every link runs under this contract. It is the same for every chain, which is why
it lives here and not retyped per epic.

- **Self-validate after every change, in priority order.** (1) Automated checks,
  preferred: add or update tests, run `./run check` while working, and run
  `./run validate` (the full gate: REAPER stages, memory passes, controls) before
  every milestone claim, or the smaller set `docs/development.md` names for what
  changed. (2) Manual verification, only when the model genuinely cannot automate
  it: read the logs under `artifacts/logs/`, read the REAPER captures under
  `artifacts/screenshots/` (a green stage only proves the run finished; the
  judgement is in the picture). (3) Ask the user to test, only when manual
  verification is impossible either. A step is not done until it is validated at
  the highest tier that applies.
- **Never block on uncertainty.** When something is unclear or unsure, take the
  best decision you can and keep going.
- **Report the decisions at the end.** Every judgement call made under
  uncertainty goes in the epic's closing journal entry, so the user can review
  what was decided in their absence.
- **Commit at each green gate**, on the current branch, no attribution trailer of
  any kind. Never push: pushing is the owner's decision (`CLAUDE.md`).
- **Zero-touch.** The goal is that the next time the user looks, the work is
  finished.

## Models (fixed for this project)

- **Implementer links: Claude, `claude-fable-5-1`, effort `high`.**
- **Reviewer: Codex, `gpt-5.6-sol`, effort `xhigh`.** One persistent reviewer for
  the whole chain.

An epic's `CONSTITUTION.md` may assign the roles differently for that epic. Never
switch provider, model or effort inside a running chain.

## Where a KickCrafter chain runs

A chain runs in the main checkout, on the current branch, and **only one chain
runs at a time on a machine**. There is no worktree isolation here, on purpose:
the gates share host resources that a worktree would not separate. The two X
displays (`:104` for the tests and the memory gate, `:102` for REAPER), the
single REAPER instance the harness drives with `-nonewinst`, and the memory of
the 8 GB development machine, where a second JUCE build in parallel is killed by
the OOM killer. Two chains would corrupt each other's runs in ways that look like
flaky tests rather than like a collision.

Before launching, make sure nothing else is building or running a gate
(`./run reaper-stop` if a REAPER instance is left over), then launch the reviewer
and the first link from the repository root. Merging or pushing at the end is the
user's, not the chain's.

## Preconditions

The target epic directory must carry:

- `requirements.md`, the self-sufficient substance (a cold session needs nothing
  else to implement it).
- `tasks.md`, the milestones, one per link.
- `CONSTITUTION.md`, the chain policy: it names `$chain-next` and `$chain-review`,
  the read order, the handoff checklist and the `CHAIN-STOPPED` procedure.
- `REVIEWER.md`, the reviewer's read-only boundary, the invariants it guards, and
  the response schema.
- `chain.toml`, the `window_prefix` and the `cap`.

## Launch

Run these from the repository root, from a session with **no `CHAIN_*`
environment variables set** (the orchestrator's own shell). A clean environment is
what lets a Codex reviewer run alongside Claude implementers: the scripts refuse a
provider, model or effort switch when a `CHAIN_*` value is inherited from a
running link.

1. **Start the reviewer** (once):

   ```bash
   .claude/skills/chain-review/scripts/review.sh start \
     --constitution epics/<name>/CONSTITUTION.md \
     --provider codex --model gpt-5.6-sol --effort xhigh
   ```

   The reviewer window reads `REVIEWER.md` and registers itself.

2. **Launch the first implementation link directly.** The successor launcher runs
   a handoff guard that requires a prior reviewer ACCEPT, which does not exist for
   the first link, so link one is opened by hand rather than through
   `launch-link.sh`:

   ```bash
   printf '1\n' > epics/<name>/.chain
   tmux new-window -n <window_prefix>-1 -c "$PWD" \
     "env CHAIN_PROVIDER=claude CHAIN_MODEL=claude-fable-5-1 CHAIN_EFFORT=high \
       CHAIN_ROLE=implementer CHAIN_CONSTITUTION=epics/<name>/CONSTITUTION.md \
       claude --dangerously-skip-permissions --effort high --model claude-fable-5-1 \
       'Read epics/<name>/CONSTITUTION.md completely and follow it.'"
   ```

   Setting `.chain` to 1 makes the first successor `<window_prefix>-2`. Every
   later link is launched by its predecessor through `$chain-next`
   (`launch-link.sh`), which inherits the model and effort and runs the handoff
   guard once the reviewer has accepted a commit.

## Monitor

The launching session does not implement. It stays alive and watches that the
chain does not stall silently, because a dead chain the user discovers hours
later is the failure this whole setup exists to avoid.

Watch the durable state, not the tmux panes (the protocol is file-backed, so the
panes are only UI):

- the `.chain` counter and `DONE` sentinel beside the constitution,
- the chain and reviewer event logs (NDJSON),
- the pending-request and response files under `.git`,
- `git log --oneline` (are links committing),
- the gate files under `artifacts/logs/gates/` (is `./run validate` finishing),
- the reviewer window's liveness.

Catch and act on these stalls:

- **A review was requested but never answered** (a pending request past a
  threshold, an `await` timeout, or a reviewer pane that died and needs
  `reattach`). This is the most common failure: the review launched but the
  handoff never completed.
- **A successor was launched but its window errored on boot.**
- **A link stalled** (no commits, no events for many minutes; a full gate takes
  an hour or more on the development machine, so allow for that before calling
  it a stall).
- **A dirty tree** blocking the handoff guard.

Nudge the stuck session, or escalate to the user with a push notification when it
cannot recover on its own. Do not implement the work yourself: repair the chain
and let it continue.

## Progress report

Whenever the user asks for progress (« progrès », « progress », « où en est la chaîne »),
answer with the same table every time, so successive reports read side by side. Gather the
facts with the read-only script first, never from memory:

```bash
.claude/skills/chain-run/scripts/progress.sh epics/<name>
```

It prints the link counter and cap, the chain's start time, each milestone of `tasks.md` with
its Done marker, the commits since the chain started, the reviewer state and every verdict so
far, and whether each chain window still carries an agent process.

The report has three parts, in this order:

1. **A header line**: epic, time, `link N of cap C`, elapsed time since the first chain commit.
2. **The milestone table**, one row per milestone of `tasks.md`, columns `Milestone`, `Status`,
   `Notes`. Status is one of: `Not started`, `In progress` (the current link's milestone),
   `Done, awaiting review` (Done marker present, no ACCEPT on HEAD yet), `Done, reviewed`
   (ACCEPT for the commit that closed it), `Done` once a later milestone has moved past it.
   Notes hold the one fact worth knowing about that row: the decision it settled, a finding it
   left for a later milestone, the number of review rounds it took.
3. **The chain state table**: commits since the chain started, review requests sent, pending
   and verdicts (with FINDINGS rounds counted), the reviewer's liveness, the current link's
   liveness and what it is doing (read from its pane at report time, one line).

Then at most one short paragraph on anything the user should know now: a decision taken under
uncertainty that changes the plan, a stall that was repaired, or a finding that will need their
input at the end. No prose when there is nothing of that kind.

## Stop

The chain stops on its own when `DONE` exists, when two consecutive links produce
no commits (a `CHAIN-STOPPED` entry), or at the `cap` in `chain.toml`. Only the
user raises the cap.
