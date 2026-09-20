---
name: epic
description: Conventions for an epic, planned work under epics/. Use when creating a new epic, or when implementing an existing one.
argument-hint: [epic slug or subject]
---

# Epic

This skill is a spec, not a procedure. It states what must be true of an epic and its
implementation; how you get there is your call, judged against these invariants. There is no
template and no prescribed section list: structure the requirements however the work deserves.

1. **An epic is a directory `epics/<slug>/`, and its requirements file is self-sufficient.**
   Everything a session with no memory of this conversation needs to implement the epic lives in
   that file: the decisions and why, the constraints, the seams. If a cold session would have to
   guess, the requirements are incomplete. The slug is two to four kebab-case words naming the
   work. A finished epic moves to `epics/archive/<slug>/`, unchanged.

2. **The requirements hold settled decisions, not open questions.** Anything knowable before
   implementation has been asked of the user and answered before the file is written.

3. **The epic holds the requirements and a task list** covering its phases.

4. **No phase is done until it is validated**, preferring automated checks (`./run check` while
   working, `./run validate` or the smaller set `docs/development.md` names for what changed
   before a phase is claimed), then manual verification (logs, REAPER captures, read by eye),
   then asking the user, in that order.

5. **A decision that only surfaces during implementation is made on best judgment and confirmed
   with the user at the end**, never used to stall.

6. **Implementation is carried to completion in one autonomous pass.**

7. **A finished epic carries a journal entry in its own `journal/` directory**: the reasoning,
   and where the implementation diverged from the plan. Journals live only inside epics, never
   at the repository root. An entry is named by its date, `YYYY-MM-DD.md`, so successive entries
   stay in order, and that date is read from the system clock (`date +%F`), never assumed.
   Prose, not bullets: the value is the reasoning, and bullets strip it out.

8. **Durable knowledge is written where it lives, not only in the journal.** When an epic changes
   something a future reader needs beyond the session trace (a build or validation step, a
   convention, an operational gotcha, a user-visible behaviour), the relevant document is
   updated as part of the epic: `docs/development.md` for developers, `README.md` for users,
   `CHANGELOG.md` for what changed, `CLAUDE.md` for a rule or a closed decision. The journal
   records the session, the documents record the truth. An epic that leaves a real change
   undocumented is not done.
