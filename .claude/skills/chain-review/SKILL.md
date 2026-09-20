---
name: chain-review
description: Coordinate exactly one persistent read-only reviewer across an epic chain. Use when an epic CONSTITUTION.md explicitly invokes $chain-review to start, register, or reattach the reviewer, submit a frozen commit for review, wait for its response, complete a response from the reviewer pane, inspect status, or guard a chain handoff.
---

# Chain Review

Keep one reviewer session alive while implementation moves through fresh chain
links. The reviewer never owns the worktree. Requests and responses live under
Git's private directory. Tmux is UI only: it opens the reviewer's window and
types a one-line notification into its pane. It carries no protocol — `await`
polls the durable response file, so neither side depends on a tmux channel
outliving it.

The coordinator also appends one NDJSON line per action to `events.log` beside
its state: a timestamp, a kind, a phase, a status, a span id and a few
attributes — request ids, checkpoint ids and verdicts, never a claim, a focus or
a response body. Nothing reads it back and nothing waits on it, and a write that
fails is swallowed. Do not make a review decision depend on it.

Read [references/protocol.md](references/protocol.md) when explaining this
mechanism or adapting it to another project. It records the file-backed state,
tmux handshake, fail-closed invariants and reboot recovery independently of the
the project epic that first used them.

## Preconditions

1. Identify the governing `epics/<name>/CONSTITUTION.md`.
2. Require it to name `$chain-review` and have `REVIEWER.md` beside it.
3. Use the repository script at
   `.claude/skills/chain-review/scripts/review.sh`.
4. Fail closed when reviewer state, the target commit or the role is ambiguous.

## Start the reviewer

Start once, before the first implementation checkpoint:

```bash
.claude/skills/chain-review/scripts/review.sh start \
  --constitution epics/<name>/CONSTITUTION.md \
  --provider <codex|claude> \
  --model <model> \
  --effort <effort>
```

The reviewer window is named from the `window_prefix` in `chain.toml` beside the
constitution; `--window-prefix` overrides it.

Codex defaults to `gpt-5.6-sol` at `xhigh`. Inherited
`CHAIN_PROVIDER`, `CHAIN_MODEL` and `CHAIN_EFFORT` are
binding. Never replace a live or stale reviewer state with a second reviewer.

The launched reviewer reads the epic's `REVIEWER.md`. From that pane, register
with:

```bash
.claude/skills/chain-review/scripts/review.sh register \
    --constitution epics/<name>/CONSTITUTION.md
```

## Recover the reviewer after a reboot

Keep the existing reviewer session and its private request history. From the
recreated reviewer window, inspect `status` and reattach with the exact saved
profile. A manually recreated process normally needs the launch markers supplied
to this command:

```bash
env CHAIN_ROLE=reviewer \
  CHAIN_CONSTITUTION=epics/<name>/CONSTITUTION.md \
  CHAIN_PROVIDER=<provider> \
  CHAIN_MODEL=<model> \
  CHAIN_EFFORT=<effort> \
  .claude/skills/chain-review/scripts/review.sh reattach \
    --constitution epics/<name>/CONSTITUTION.md
```

Reattachment fails unless the stored pane is gone, no review is pending, the
command runs inside the sole pane of the stored reviewer window, and the
role, constitution, provider, model and effort match the saved state. Repeating
the command is allowed only from that same attached pane, so older state can gain
the recovery marker without changing identity. Reattachment changes only the
pane, thread, readiness and recovery marker. The exact pane then becomes the
recovered reviewer's identity for `complete`; absent launch markers are allowed,
but any inherited mismatch is refused. Never edit the private state by hand or
create a replacement reviewer to recover from a reboot.

## Submit a checkpoint

Commit the complete candidate, including task and journal records, and require a
clean worktree. Then run:

```bash
.claude/skills/chain-review/scripts/review.sh request \
  --constitution epics/<name>/CONSTITUTION.md \
  --checkpoint <short-id> \
  --claim '<what this commit establishes>' \
  --validation '<commands and results>' \
  --focus '<specific risks>'
```

Stop editing. The command prints the immutable request id. Wait with:

```bash
.claude/skills/chain-review/scripts/review.sh await \
  --constitution epics/<name>/CONSTITUTION.md \
  --request <request-id>
```

`ACCEPT` exits 0. `FINDINGS` exits 3 and `BLOCKED` exits 4, both after printing
the response. `await` polls for the response and honours `--timeout` (default
1800s); it is safe to run before, during or after the reviewer's reply. Address every finding or rebut it with evidence in a new frozen
request. Any new commit invalidates the old acceptance.

## Respond from the reviewer pane

Review only the request's exact commit. Do not edit the repository or run the
shared acceptance suite. Write the response markdown in `/tmp` with the schema
from the epic's `REVIEWER.md`, then run:

```bash
.claude/skills/chain-review/scripts/review.sh complete \
  --constitution epics/<name>/CONSTITUTION.md \
  --request <request-id> \
  --verdict <ACCEPT|FINDINGS|BLOCKED> \
  --reviewed-head <full-commit> \
  --response-file /tmp/<response>.md
```

Return to an idle prompt after completion. Do not launch another reviewer or a
successor.

## Inspect and guard

Use `status` to inspect the singleton and `guard` immediately before a handoff:

```bash
.claude/skills/chain-review/scripts/review.sh status \
  --constitution epics/<name>/CONSTITUTION.md

.claude/skills/chain-review/scripts/review.sh guard \
  --constitution epics/<name>/CONSTITUTION.md
```

The guard requires a live, ready reviewer, no pending request, `ACCEPT` for exact
`HEAD`, **and a clean worktree** — an accepted commit is only meaningful if it is
what would actually be handed off. `complete` applies the same worktree rule to
`ACCEPT` and `FINDINGS`; `BLOCKED` remains available when the implementer did not
in fact freeze. The successor skill runs it automatically when the constitution
invokes this skill.

Use `--dry-run` on `start` and `request` to inspect paths and commands without
creating state, changing tmux or notifying a pane.

Every action appends one NDJSON line to `events.log` beside the reviewer state.
Nothing waits on it and no failure writing it can change a verdict.
