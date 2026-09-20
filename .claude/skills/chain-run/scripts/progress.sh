#!/usr/bin/env bash
# Print the facts a progress report is built from, for one chain. Read-only.
#   progress.sh <epic dir relative to the repository root>
# Chains run in the main checkout (no worktree), so the commits shown are those since the epic's
# constitution was committed (or since the first chain event), or the last 15 when neither is known.
# The master turns this into the table the skill's "Progress report" section prescribes.
set -euo pipefail
EP=${1:?epic dir}
W="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
E="$W/$EP"
S="$(git -C "$W" rev-parse --path-format=absolute --git-common-dir)/chain-review/$(basename "$EP")"
prefix=$(sed -n 's/^window_prefix *= *"\(.*\)"/\1/p' "$E/chain.toml")
cap=$(sed -n 's/^cap *= *\(.*\)/\1/p' "$E/chain.toml")
link=$(cat "$E/.chain" 2>/dev/null || echo "-")
echo "now: $(date +%H:%M)   link: $link of cap $cap   done: $([ -f "$E/DONE" ] && echo yes || echo no)"
# The chain starts after the epic (its constitution) was committed; fall back to the first chain event.
since=$(git -C "$W" log --format=%cI --diff-filter=A -- "$EP/CONSTITUTION.md" 2>/dev/null | tail -1)
[ -n "$since" ] || since=$(head -1 "$E/.chain-events.log" 2>/dev/null | sed -n 's/.*"ts": *"\([^"]*\)".*/\1/p')
first=$(git -C "$W" log --format=%ct --reverse ${since:+--since="$since"} 2>/dev/null | head -1 || true)
[ -n "$first" ] && echo "first chain commit: $(date -d @"$first" +%H:%M) ($(( ($(date +%s)-first)/60 )) min ago)"
echo
echo "milestones (from tasks.md):"
awk '/^## /{if(t)print t" | "(d?"Done":"Not done"); t=$0; d=0} /^\*\*Done/{d=1} END{if(t)print t" | "(d?"Done":"Not done")}' "$E/tasks.md" | sed 's/^## //'
echo
echo "commits since the chain started:"
if [ -n "$since" ]; then git -C "$W" log --oneline --since="$since" | cat; else git -C "$W" log --oneline -15 | cat; fi
echo
echo "review state:"
grep -E '^(ready|pending|accepted_head|last_request|last_verdict|sequence)=' "$S/state" 2>/dev/null || echo "no reviewer state"
echo "verdicts so far:"
python3 - "$S/events.log" <<'PY' 2>/dev/null || true
import sys,json
for l in open(sys.argv[1]):
    try: r=json.loads(l)
    except Exception: continue
    a=r.get("attrs",{})
    if a.get("verdict"): print("  ", r.get("ts","")[11:16], a.get("checkpoint",""), a.get("verdict"))
PY
echo
echo "windows:"
tmux list-panes -a -F '#{window_name} #{pane_id} #{pane_pid}' 2>/dev/null | awk -v p="$prefix" 'index($1,p"-")==1' | while read -r name pane pid; do
  alive=$(python3 - "$pid" <<'PY'
import sys,os
root=int(sys.argv[1]); kids={}
for p in os.listdir('/proc'):
    if not p.isdigit(): continue
    try:
        st=open(f'/proc/{p}/stat').read(); kids.setdefault(int(st[st.rindex(')')+2:].split()[1]),[]).append(int(p))
    except Exception: pass
stack=[root]; names=[]
while stack:
    q=stack.pop()
    for c in kids.get(q,[]):
        try: names.append(open(f'/proc/{c}/comm').read().strip())
        except Exception: pass
        stack.append(c)
print("alive" if any(n in ("claude","node","codex") for n in names) else "no agent process")
PY
)
  echo "  $name $pane $alive"
done
