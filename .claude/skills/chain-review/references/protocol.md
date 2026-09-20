# Persistent review handshake

Status: implemented by `scripts/review.sh`. The protocol is generic. the project's
constitution and reviewer instructions supply the project-specific policy.

## Contents

- [Purpose](#purpose)
- [Roles](#roles)
- [One review](#one-review)
- [Verdicts](#verdicts)
- [Required invariants](#required-invariants)
- [Optional activity visibility](#optional-activity-visibility)
- [Reboot recovery](#reboot-recovery)
- [Adapting the mechanism](#adapting-the-mechanism)

## Purpose

Keep one read-only reviewer alive while disposable implementation sessions move
through a chain. Pin every review to an immutable commit. Put every durable thing
in files, and use the terminal multiplexer only to open a window and type a
notification — never to carry protocol state.

## Roles

The implementer owns the worktree. It edits, validates, journals and commits.
It stops editing while a review is pending.

The reviewer owns no project process or file. It reads the request, exact commit,
source and retained validation evidence. It returns `ACCEPT`, `FINDINGS` or
`BLOCKED`.

Tmux is UI. It opens the reviewer's window and types a one-line prompt into its
pane. It carries no review content and no synchronization: an implementer that
depends on a channel in a server which may be restarted, or may outlive one side
of the handshake, has made its liveness a function of its user interface.

Git's private directory is the durable store. Keep state, requests and responses
under a directory such as:

```text
.git/project-review/<epic>/
├── state
├── requests/<request-id>.md
└── responses/<request-id>.md
```

Do not commit these files. They survive agent restarts and preserve the review
history without dirtying the worktree.

## One review

1. The implementer finishes a coherent checkpoint, validates it, commits it and
   requires a clean worktree.
2. `request` records the exact `HEAD`, claim, validation evidence and review
   focus in a new immutable request file.
3. `request` records the pending state and types a short prompt into the
   registered reviewer pane with `tmux send-keys`.
4. The implementer runs `await`, which polls for the response file until it
   appears or the timeout expires.
5. The reviewer reads the request and reviews only its exact commit. It writes a
   response outside the worktree.
6. `complete` verifies the response, commit, reviewer identity, current `HEAD`
   and a clean worktree; updates the verdict and every state field; and only
   then publishes the response file with an atomic rename.
7. The next poll finds it. `await` reads and verifies the response, prints it,
   and exits according to the verdict.

**The response file's existence is the commit point.** It must therefore be
published last, after every state update, by renaming a staged file into place.
Publishing it first — or copying it in place while state is still being
written — lets an `await` that wins the race read a response whose state has not
landed, and fail a consistency check it should have passed.

Polling costs a fixed half second of latency on a review that takes minutes. In
exchange the protocol loses the channel bookkeeping, the completion/await race,
and every failure mode in which the multiplexer outlives or predeceases one side
of the handshake. `await` becomes safe to run before, during or after the reply,
and a pending review survives a multiplexer restart.

## Verdicts

`ACCEPT` records the exact reviewed commit as accepted.

`FINDINGS` requires a correction or evidence-backed rebuttal and a new request.
Create a new commit when repository content changes. A rebuttal that changes no
repository content may request the same frozen commit again. Acceptance never
transfers to a later commit.

`BLOCKED` records that the target moved, evidence is unavailable or the review
cannot complete safely.

## Required invariants

- Register exactly one reviewer pane for the chain.
- Refuse a request when another request is pending.
- Require a clean worktree before freezing a request.
- Store the full commit hash in every request and response.
- Stop implementation edits while a request is pending. The coordinator does not
  lock the worktree, but it does verify at `complete` that the worktree is clean,
  so a broken promise becomes a refused verdict rather than a silent one.
- Refuse `ACCEPT` or `FINDINGS` when `HEAD` moved during review.
- Allow only the registered reviewer pane to complete a request.
- Keep reviewer provider, model, effort and role consistent with saved state.
- Before handoff, require a live ready reviewer, no pending request,
  `accepted_head == HEAD` and a clean worktree.
- Fail closed when identity, state or target is ambiguous.

These checks pin commit identity; they are not a worktree lock. Exact-commit
verification detects a moved `HEAD`; the clean-worktree check at `complete` and
`guard` is what detects uncommitted edits. Review the requested commit through
`git show` and `git diff`, not mutable worktree contents.

## Activity visibility

The coordinator appends one NDJSON line per action to an events log beside its
state: a timestamp, a kind, a phase, a status, a span id, a short name and a few
attributes. It records reviewer start, registration, reattachment, request,
completion, wait and handoff-guard transitions. It never records the claim,
focus, validation text or response body.

Wait attempts use distinct span identifiers, so a response that arrives before
`await` produces a real near-zero wait rather than an invented duration.

The write is appended and its failure is swallowed. A log that cannot be written
cannot change coordinator output, state, signalling, verdict or exit status. A
separate process may ship these lines somewhere; the coordinator does not know
about it and never waits on one.

If the log lives inside the worktree rather than beside private state, it must be
ignored by the version control system. Both the successor launcher and the
handoff guard refuse on an unclean worktree, so a visible log makes the next
handoff refuse itself.

## Reboot recovery

Pane identifiers disappear with the multiplexer server. The private request
history, the pending request and the accepted commit remain in `.git`.

Resume the existing reviewer conversation in the saved reviewer window, then run
an explicit `reattach` operation. Reattachment must require all of the following:

- the stored pane is gone, or it is the exact current pane repeating an earlier
  successful attachment;
- the new pane is the only pane in the saved reviewer window;
- the reviewer role, constitution, provider, model and effort match saved state.

Update only the pane, thread, readiness and recovery marker. Never edit private
state by hand or create a second reviewer. Refuse a different live stored pane,
but allow the already attached pane to repeat the operation idempotently.

**A pending review does not block reattachment.** It did when the handshake
depended on a channel, because losing that channel made completion ambiguous.
With the response file as the rendezvous there is nothing to lose: the request
is still on disk, the reviewer can resume and complete it, and an `await` that
died with its terminal is simply re-run.

## Adapting the mechanism

Keep the coordinator generic and move project policy into two files:

- a constitution that defines checkpoints, validation and handoff conditions;
- reviewer instructions that define read-only boundaries, architectural
  invariants and the response schema.

Parameterize the repository root, constitution path, reviewer window, provider,
model and effort. Keep tmux messages short. Keep all substantive communication
in durable files. Add dry-run modes for startup and request creation. Test the
happy path, findings loop, moved `HEAD`, duplicate request, dead pane, timeout,
early response and reboot reattachment before relying on the protocol.
