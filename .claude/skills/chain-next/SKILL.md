---
name: chain-next
description: Launch exactly one successor for an epic chain in tmux, preserving its provider, model and reasoning effort. Use only when a project CONSTITUTION.md explicitly invokes $chain-next after a link has been completed, validated, committed, reviewed when required, and journaled; never use for ordinary continuation or when no constitution exists.
---

# Chain Next

Launch one fresh chain link. Do not decide whether the current link is ready to
hand off; the epic constitution owns that decision.

## Preconditions

1. Identify the exact `epics/<name>/CONSTITUTION.md` governing the current link.
2. Require that constitution to name `$chain-next` explicitly. Refuse to
   adapt a legacy `claude` or `codex` launch snippet.
3. Complete every pre-handoff requirement in the constitution: validation,
   task status, journal, commit, and its no-progress rule.
4. Stop without launching when `DONE` exists or another constitution guard says
   to stop.
5. Require a clean worktree. The launcher changes only the epic's `.chain`
   counter.
6. When the constitution invokes `$chain-review`, require its guard to
   report `ACCEPT` for exact `HEAD` before incrementing the counter.

If any precondition is false or ambiguous, fail closed. Follow the constitution's
`CHAIN-STOPPED` procedure when it defines one.

## Launch

Determine these values from the constitution, never from an older epic:

- the tmux window prefix;
- the maximum link count;
- the constitution path.

Carry these values from the current chain environment:

- `CHAIN_PROVIDER`;
- `CHAIN_MODEL`;
- `CHAIN_EFFORT`.

Choose the provider that is running this skill:

- use `codex` from Codex;
- use `claude` from Claude Code.

Never choose by executable availability, account credits, or fallback. Never
switch provider, model or effort inside a running chain. For a first Codex link
with no inherited profile, default to `gpt-5.6-sol` at `xhigh`. Model aliases
`sol`, `terra` and `luna` are accepted. Claude keeps its explicit or inherited
model and defaults only its effort to `high` for compatibility with historical
chains.

Run from the repository root:

```bash
.claude/skills/chain-next/scripts/launch-link.sh \
  --provider <codex|claude> \
  --constitution epics/<name>/CONSTITUTION.md \
  --model <model> \
  --effort <effort>
```

The window prefix and the maximum link count come from `chain.toml` beside the
constitution. Pass `--window-prefix` and `--max-links` only to override it; a
constitution should not restate them in prose, because that is how a cap in one
document drifts from the cap in another.

The script derives `.chain` and `DONE` beside the constitution. It increments the
counter, opens one tmux window in the repository root, and seeds the successor
with `Read <constitution> completely and follow it.` It marks the child with the
provider, model, effort, role and constitution for auditability. Those inherited
values are binding on every later link. Each action also appends one NDJSON line
to `.chain-events.log` beside the counter; nothing waits on that file and no
failure writing it can affect a handoff.

Use `--dry-run` to validate a proposed launch without changing the counter or
opening a window.

## After launch

Report the new link number, window name, pane id, and provider. Do not launch a
second successor from the same link. If the launcher fails, report the failure
and do not try the other provider.
