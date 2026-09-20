#!/usr/bin/env bash

set -euo pipefail

usage() {
    cat <<'EOF'
Usage:
  review.sh start --constitution <path> --window-prefix <prefix>
                  [--provider <codex|claude>] [--model <model>]
                  [--effort <effort>] [--dry-run]
  review.sh register --constitution <path> [--pane <pane>] [--thread <id>]
  review.sh reattach --constitution <path> [--pane <pane>] [--thread <id>]
  review.sh request --constitution <path> --checkpoint <id> --claim <text>
                    [--validation <text>] [--focus <text>] [--dry-run]
  review.sh await --constitution <path> --request <id> [--timeout <seconds>]
  review.sh complete --constitution <path> --request <id>
                     --verdict <ACCEPT|FINDINGS|BLOCKED>
                     --reviewed-head <commit> --response-file <path>
  review.sh status --constitution <path>
  review.sh guard --constitution <path>
EOF
}

die() {
    printf 'chain-review: %s\n' "$*" >&2
    exit 1
}

state_get() {
    local key=$1
    sed -n "s/^${key}=//p" "$state_file" | tail -1
}

state_set() {
    local key=$1 value=$2 temp
    temp=$state_file.tmp.$$
    awk -F= -v key="$key" -v value="$value" '
        BEGIN { found = 0 }
        $1 == key { print key "=" value; found = 1; next }
        { print }
        END { if (!found) print key "=" value }
    ' "$state_file" > "$temp"
    chmod 600 "$temp"
    mv "$temp" "$state_file"
}

state_reattach() {
    local new_pane=$1 new_thread=$2 temp
    temp=$state_file.tmp.$$
    awk -F= -v pane="$new_pane" -v thread="$new_thread" '
        BEGIN { found_recovered = 0 }
        $1 == "pane"   { print "pane=" pane; next }
        $1 == "thread" { print "thread=" thread; next }
        $1 == "ready"  { print "ready=1"; next }
        $1 == "recovered" { print "recovered=1"; found_recovered = 1; next }
        { print }
        END { if (!found_recovered) print "recovered=1" }
    ' "$state_file" > "$temp"
    chmod 600 "$temp"
    mv "$temp" "$state_file"
}

pane_alive() {
    local wanted=$1
    [[ -n $wanted ]] || return 1
    tmux list-panes -a -F '#{pane_id}' 2>/dev/null | grep -Fxq "$wanted"
}

notify_reviewer() {
    local target=$1 message=$2 reviewer_provider=$3

    if [[ $reviewer_provider == codex ]]; then
        # Codex treats an immediately followed Escape + first character as one
        # escape sequence. Clear an idle prompt without dropping that character.
        tmux send-keys -t "$target" C-u || return 1
    else
        tmux send-keys -t "$target" Escape || return 1
        sleep 0.1
    fi
    tmux send-keys -t "$target" -l "$message" || return 1
    sleep 0.2
    tmux send-keys -t "$target" Enter
}

normalize_codex_model() {
    case "$1" in
        sol)   printf '%s\n' gpt-5.6-sol ;;
        terra) printf '%s\n' gpt-5.6-terra ;;
        luna)  printf '%s\n' gpt-5.6-luna ;;
        *)     printf '%s\n' "$1" ;;
    esac
}

# Machine-readable chain parameters, so an agent does not have to parse them out
# of an English sentence in the constitution. Only these keys are read.
chain_conf() {
    local key=$1 file=$epic_dir/chain.toml
    [[ -f $file ]] || return 0
    sed -n "s/^[[:space:]]*${key}[[:space:]]*=[[:space:]]*//p" "$file" \
        | head -1 | tr -d '"'\''' | tr -d '\r'
}

action=${1:-}
[[ -n $action ]] || { usage; exit 2; }
shift

inherited_provider=${CHAIN_PROVIDER:-}
inherited_model=${CHAIN_MODEL:-}
inherited_effort=${CHAIN_EFFORT:-}

constitution_arg=
window_prefix=
provider=${inherited_provider:-codex}
model=${inherited_model:-}
effort=${inherited_effort:-}
checkpoint=
claim=
validation="not stated"
focus="general correctness and the epic invariants"
request_id=
verdict=
reviewed_head=
response_file=
pane_arg=${TMUX_PANE:-}
thread_arg=${CODEX_THREAD_ID:-${CLAUDE_SESSION_ID:-}}
timeout_seconds=1800
dry_run=0

while (($#)); do
    case "$1" in
        --constitution) constitution_arg=${2:-}; shift 2 ;;
        --window-prefix) window_prefix=${2:-}; shift 2 ;;
        --provider) provider=${2:-}; shift 2 ;;
        --model) model=${2:-}; shift 2 ;;
        --effort) effort=${2:-}; shift 2 ;;
        --checkpoint) checkpoint=${2:-}; shift 2 ;;
        --claim) claim=${2:-}; shift 2 ;;
        --validation) validation=${2:-}; shift 2 ;;
        --focus) focus=${2:-}; shift 2 ;;
        --request) request_id=${2:-}; shift 2 ;;
        --verdict) verdict=${2:-}; shift 2 ;;
        --reviewed-head) reviewed_head=${2:-}; shift 2 ;;
        --response-file) response_file=${2:-}; shift 2 ;;
        --pane) pane_arg=${2:-}; shift 2 ;;
        --thread) thread_arg=${2:-}; shift 2 ;;
        --timeout) timeout_seconds=${2:-}; shift 2 ;;
        --dry-run) dry_run=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) die "unknown argument: $1" ;;
    esac
done

[[ -n $constitution_arg ]] || die "--constitution is required"
[[ $provider == codex || $provider == claude ]] || die "provider must be codex or claude"

command -v git >/dev/null || die "git is not available"
repo_root=$(git rev-parse --show-toplevel 2>/dev/null) || die "not inside a git repository"
repo_root=$(realpath -e "$repo_root")

if [[ $constitution_arg = /* ]]; then
    constitution=$constitution_arg
else
    constitution=$repo_root/$constitution_arg
fi
constitution=$(realpath -e "$constitution" 2>/dev/null) || die "constitution does not exist: $constitution_arg"
[[ $constitution == "$repo_root"/* ]] || die "constitution is outside the repository"
[[ $(basename "$constitution") == CONSTITUTION.md ]] || die "expected a CONSTITUTION.md file"
grep -Fq '$chain-review' "$constitution" || die "constitution does not invoke \$chain-review"

constitution_rel=${constitution#"$repo_root"/}
epic_dir=$(dirname "$constitution")
epic_name=$(basename "$epic_dir")
reviewer_doc=$epic_dir/REVIEWER.md
[[ -f $reviewer_doc ]] || die "reviewer instructions do not exist: $reviewer_doc"
reviewer_rel=${reviewer_doc#"$repo_root"/}

git_private=$(git rev-parse --path-format=absolute --git-common-dir)
state_dir=$git_private/chain-review/$epic_name
state_file=$state_dir/state
requests_dir=$state_dir/requests
responses_dir=$state_dir/responses

events_log=$state_dir/events.log

# Observability observes; it does not live inside the mechanism. One NDJSON line
# per action, appended beside the state it describes. A separate process may ship
# these somewhere; nothing here waits on it, and no failure can reach a verdict.
#
# The argument grammar is the one the call sites already use:
#   chain_event --kind K [--point|--start|--finish] [--span S]
#               --status S --name "text" [--attribute k=v]...
chain_event() {
    ((dry_run)) && return 0
    local kind="" status="" name="" span="" phase="point" attrs=""
    while (($#)); do
        case "$1" in
            --kind)      kind=${2:-}; shift 2 ;;
            --status)    status=${2:-}; shift 2 ;;
            --name)      name=${2:-}; shift 2 ;;
            --span)      span=${2:-}; shift 2 ;;
            --point)     phase=point;  shift ;;
            --start)     phase=start;  shift ;;
            --finish)    phase=finish; shift ;;
            --attribute) attrs+="${attrs:+,}\"${2%%=*}\":\"${2#*=}\""; shift 2 ;;
            *) shift ;;
        esac
    done
    name=${name//\\/\\\\}; name=${name//\"/\\\"}
    printf '{"ts":"%s","kind":"%s","phase":"%s","status":"%s","span":"%s","name":"%s","attrs":{%s}}\n' \
        "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$kind" "$phase" "$status" "$span" "$name" "$attrs" \
        >> "$events_log" 2>/dev/null || true
    return 0
}

if [[ $provider == codex ]]; then
    model=$(normalize_codex_model "${model:-gpt-5.6-sol}")
    effort=${effort:-xhigh}
else
    effort=${effort:-high}
fi

[[ -z $model || $model =~ ^[A-Za-z0-9][A-Za-z0-9._-]*$ ]] || die "invalid model id: $model"
case "$effort" in
    low|medium|high|xhigh|max|ultra) ;;
    *) die "unsupported effort: $effort" ;;
esac

if [[ -n $inherited_provider && $provider != "$inherited_provider" ]]; then
    die "provider switch refused: inherited $inherited_provider, requested $provider"
fi
if [[ -n $inherited_model ]]; then
    inherited_model_normalized=$inherited_model
    [[ $provider != codex ]] || inherited_model_normalized=$(normalize_codex_model "$inherited_model")
    [[ $model == "$inherited_model_normalized" ]] || die "model switch refused: inherited $inherited_model_normalized, requested $model"
fi
if [[ -n $inherited_effort && $effort != "$inherited_effort" ]]; then
    die "effort switch refused: inherited $inherited_effort, requested $effort"
fi


case "$action" in
    start)
        [[ -n $window_prefix ]] || window_prefix=$(chain_conf window_prefix)
        [[ -n $window_prefix ]] || die "no --window-prefix and no window_prefix in $epic_dir/chain.toml"
        [[ $window_prefix =~ ^[a-z][a-z0-9-]*$ ]] || die "window prefix must match [a-z][a-z0-9-]*"
        [[ -n ${TMUX:-} || $dry_run == 1 ]] || die "not running inside tmux"
        command -v tmux >/dev/null || die "tmux is not available"
        command -v "$provider" >/dev/null || die "$provider is not available"

        if ((!dry_run)); then
            status=$(git -C "$repo_root" status --porcelain --untracked-files=all)
            [[ -z $status ]] || die "worktree is not clean; commit before starting the reviewer"
        fi

        reviewer_window=$window_prefix-review
        if [[ -e $state_file ]]; then
            old_pane=$(state_get pane)
            if pane_alive "$old_pane"; then
                die "reviewer already exists: window=$(state_get window) pane=$old_pane"
            fi
            die "reviewer state exists but its pane is gone: $state_file"
        fi
        if tmux list-windows -a -F '#{window_name}' 2>/dev/null | grep -Fxq "$reviewer_window"; then
            die "tmux window already exists: $reviewer_window"
        fi

        prompt="Read $reviewer_rel completely and follow it. Register this reviewer, then wait for review requests."
        common_env=(env CHAIN_PROVIDER="$provider" CHAIN_MODEL="$model"
                    CHAIN_EFFORT="$effort" CHAIN_ROLE=reviewer
                    CHAIN_CONSTITUTION="$constitution_rel"
                    MERLIN_TIMELINE_ROLE=Reviewer MERLIN_PROVIDER="$provider"
                    MERLIN_MODEL="$model" MERLIN_EFFORT="$effort")
        if [[ $provider == codex ]]; then
            agent=("${common_env[@]}" codex --yolo --model "$model" -c "model_reasoning_effort=\"$effort\"" "$prompt")
        else
            agent=("${common_env[@]}" claude --dangerously-skip-permissions --effort "$effort")
            [[ -z $model ]] || agent+=(--model "$model")
            agent+=("$prompt")
        fi
        # The reviewer's shell must not start the agent until its pane id has been
        # written to state, or `register` compares against an empty pane. A file
        # marker does that without a tmux channel: the shell polls, `start` creates
        # it once the state is complete.
        start_marker=$state_dir/.reviewer-go
        printf -v agent_command '%q ' "${agent[@]}"
        printf -v start_marker_quoted '%q' "$start_marker"
        shell_command="while [ ! -e $start_marker_quoted ]; do sleep 0.2; done; "
        shell_command+="rm -f $start_marker_quoted; $agent_command"
        shell_command+='|| exec zsh'

        if ((dry_run)); then
            printf 'action=start\nprovider=%s\nmodel=%s\neffort=%s\n' "$provider" "${model:--}" "$effort"
            printf 'constitution=%s\nreviewer=%s\nwindow=%s\nstate=%s\ncommand=%s\n' \
                   "$constitution_rel" "$reviewer_rel" "$reviewer_window" "$state_file" "$shell_command"
            exit 0
        fi

        mkdir -p "$requests_dir" "$responses_dir"
        chmod 700 "$state_dir" "$requests_dir" "$responses_dir"
        cat > "$state_file" <<EOF
constitution=$constitution_rel
provider=$provider
model=$model
effort=$effort
window=$reviewer_window
pane=
thread=
ready=0
recovered=0
sequence=0
pending=
pending_head=
accepted_head=$(git -C "$repo_root" rev-parse HEAD)
last_request=
last_verdict=
last_response=
EOF
        chmod 600 "$state_file"
        if ! reviewer_pane=$(tmux new-window -P -F '#{pane_id}' -n "$reviewer_window" -c "$repo_root" "$shell_command"); then
            unlink "$state_file"
            rmdir "$requests_dir" "$responses_dir" "$state_dir" 2>/dev/null || true
            chain_event --kind review.guard --point --status error \
                --name 'Reviewer launch failed' --attribute action=start
            die "tmux failed to create $reviewer_window"
        fi
        state_set pane "$reviewer_pane"
        : > "$start_marker" || die "could not release reviewer pane $reviewer_pane"
        chain_event --kind review.guard --point --status ok \
            --name 'Reviewer started' --attribute action=start
        printf 'started reviewer window=%s pane=%s provider=%s model=%s effort=%s state=%s\n' \
               "$reviewer_window" "$reviewer_pane" "$provider" "${model:--}" "$effort" "$state_file"
        ;;

    register)
        [[ -f $state_file ]] || die "reviewer has not been started"
        [[ ${CHAIN_ROLE:-} == reviewer ]] || die "register must run from the reviewer session"
        expected_pane=$(state_get pane)
        [[ -n ${TMUX_PANE:-} && $TMUX_PANE == "$expected_pane" ]] || die "register must run from reviewer pane $expected_pane"
        [[ -n $pane_arg && $pane_arg == "$expected_pane" ]] || die "wrong reviewer pane: expected $expected_pane, got ${pane_arg:--}"
        [[ ${CHAIN_PROVIDER:-} == "$(state_get provider)" ]] || die "reviewer provider does not match its state"
        [[ ${CHAIN_MODEL:-} == "$(state_get model)" ]] || die "reviewer model does not match its state"
        [[ ${CHAIN_EFFORT:-} == "$(state_get effort)" ]] || die "reviewer effort does not match its state"
        state_set thread "$thread_arg"
        state_set ready 1
        chain_event --kind review.guard --point --status ok \
            --name 'Reviewer registered' --attribute action=register
        printf 'registered reviewer pane=%s thread=%s\n' "$pane_arg" "${thread_arg:--}"
        ;;

    reattach)
        [[ -f $state_file ]] || die "reviewer has not been started"
        [[ ${CHAIN_ROLE:-} == reviewer ]] || die "reattach must run from the reviewer session"
        command -v tmux >/dev/null || die "tmux is not available"
        old_pane=$(state_get pane)
        # Reattaching during a pending review used to be refused, because a lost
        # tmux channel made completion state ambiguous. There is no channel now:
        # `await` polls the response file, so a pending review survives a tmux
        # restart and the reviewer can simply resume and complete it.
        [[ -n ${TMUX_PANE:-} ]] || die "reviewer session is not running inside tmux"
        [[ -n $pane_arg && $pane_arg == "$TMUX_PANE" ]] || die "--pane must name the current reviewer pane"
        pane_alive "$TMUX_PANE" || die "current reviewer pane is not alive"
        if pane_alive "$old_pane" && [[ $old_pane != "$TMUX_PANE" ]]; then
            die "stored reviewer pane is still alive in another session: $old_pane"
        fi
        expected_window=$(state_get window)
        current_window=$(tmux display-message -p -t "$TMUX_PANE" '#{window_name}') || die "cannot inspect current reviewer window"
        [[ $current_window == "$expected_window" ]] || die "reviewer window does not match its state: expected $expected_window, got $current_window"
        mapfile -t matching_panes < <(tmux list-panes -a -F '#{pane_id} #{window_name}' | awk -v window="$expected_window" '$2 == window { print $1 }')
        [[ ${#matching_panes[@]} == 1 && ${matching_panes[0]} == "$TMUX_PANE" ]] || die "reviewer window is not a unique single-pane session: $expected_window"
        [[ ${CHAIN_CONSTITUTION:-} == "$(state_get constitution)" ]] || die "reviewer constitution does not match its state"
        [[ ${CHAIN_PROVIDER:-} == "$(state_get provider)" ]] || die "reviewer provider does not match its state"
        [[ ${CHAIN_MODEL:-} == "$(state_get model)" ]] || die "reviewer model does not match its state"
        [[ ${CHAIN_EFFORT:-} == "$(state_get effort)" ]] || die "reviewer effort does not match its state"
        state_reattach "$TMUX_PANE" "$thread_arg"
        chain_event --kind review.guard --point --status ok \
            --name 'Reviewer reattached' --attribute action=reattach
        printf 'reattached reviewer window=%s pane=%s thread=%s\n' "$expected_window" "$TMUX_PANE" "${thread_arg:--}"
        ;;

    request)
        [[ -f $state_file ]] || die "reviewer has not been started"
        [[ ${CHAIN_ROLE:-implementer} != reviewer ]] || die "the reviewer cannot submit its own request"
        [[ $checkpoint =~ ^[A-Za-z0-9][A-Za-z0-9._-]*$ ]] || die "--checkpoint must be a short identifier"
        [[ -n $claim ]] || die "--claim is required"
        [[ $claim != *$'\n'* && $validation != *$'\n'* && $focus != *$'\n'* ]] || die "request fields must be one line each"
        [[ -z $(state_get pending) ]] || die "another review is pending: $(state_get pending)"
        [[ $(state_get ready) == 1 ]] || die "reviewer is not ready"
        reviewer_pane=$(state_get pane)
        pane_alive "$reviewer_pane" || die "reviewer pane is not alive: $reviewer_pane"
        [[ -z $(git -C "$repo_root" status --porcelain --untracked-files=all) ]] || die "worktree is not clean; freeze and commit the checkpoint first"

        head_commit=$(git -C "$repo_root" rev-parse HEAD)
        base_commit=$(state_get accepted_head)
        sequence=$(( $(state_get sequence) + 1 ))
        printf -v sequence_padded '%03d' "$sequence"
        generated_id=$checkpoint-$sequence_padded
        request_path=$requests_dir/$generated_id.md
        [[ ! -e $request_path ]] || die "request already exists: $request_path"

        if ((dry_run)); then
            printf 'action=request\nid=%s\nbase=%s\nhead=%s\nrequest=%s\nreviewer-pane=%s\n' \
                   "$generated_id" "$base_commit" "$head_commit" "$request_path" "$reviewer_pane"
            exit 0
        fi

        cat > "$request_path" <<EOF
# Review request $generated_id

Base: $base_commit
Head: $head_commit
Implementer pane: ${TMUX_PANE:--}

## Claim

$claim

## Validation

$validation

## Focus

$focus

Review this exact commit under $reviewer_rel. Complete the response through
\`\$chain-review\`.
EOF
        chmod 600 "$request_path"
        state_set sequence "$sequence"
        state_set pending "$generated_id"
        state_set pending_head "$head_commit"
        state_set ready 0

        message="Review request $generated_id at $request_path. Read it and follow $reviewer_rel."
        if ! notify_reviewer "$reviewer_pane" "$message" "$(state_get provider)"; then
            state_set pending ""
            state_set pending_head ""
            state_set ready 1
            unlink "$request_path"
            chain_event --kind review.request --point --status error \
                --name "Review $generated_id notification failed" \
                --attribute request_id="$generated_id" --attribute checkpoint="$checkpoint"
            die "could not notify reviewer pane $reviewer_pane"
        fi
        chain_event --kind review.request --point --status ok \
            --name "Review $generated_id requested" \
            --attribute request_id="$generated_id" --attribute checkpoint="$checkpoint"
        printf 'submitted request=%s head=%s reviewer-pane=%s file=%s\n' \
               "$generated_id" "$head_commit" "$reviewer_pane" "$request_path"
        ;;

    complete)
        [[ -f $state_file ]] || die "reviewer has not been started"
        expected_pane=$(state_get pane)
        [[ -n ${TMUX_PANE:-} && $TMUX_PANE == "$expected_pane" ]] || die "complete must run from reviewer pane $expected_pane"
        recovered=$(state_get recovered)
        reviewer_role=${CHAIN_ROLE:-}
        if [[ $recovered == 1 ]]; then
            [[ -z $reviewer_role || $reviewer_role == reviewer ]] || die "complete must run from the reviewer session"
            [[ -z ${CHAIN_CONSTITUTION:-} || ${CHAIN_CONSTITUTION:-} == "$(state_get constitution)" ]] || die "reviewer constitution does not match its state"
            [[ -z ${CHAIN_PROVIDER:-} || ${CHAIN_PROVIDER:-} == "$(state_get provider)" ]] || die "reviewer provider does not match its state"
            [[ -z ${CHAIN_MODEL:-} || ${CHAIN_MODEL:-} == "$(state_get model)" ]] || die "reviewer model does not match its state"
            [[ -z ${CHAIN_EFFORT:-} || ${CHAIN_EFFORT:-} == "$(state_get effort)" ]] || die "reviewer effort does not match its state"
        else
            [[ $reviewer_role == reviewer ]] || die "complete must run from the reviewer session"
            [[ ${CHAIN_CONSTITUTION:-} == "$(state_get constitution)" ]] || die "reviewer constitution does not match its state"
            [[ ${CHAIN_PROVIDER:-} == "$(state_get provider)" ]] || die "reviewer provider does not match its state"
            [[ ${CHAIN_MODEL:-} == "$(state_get model)" ]] || die "reviewer model does not match its state"
            [[ ${CHAIN_EFFORT:-} == "$(state_get effort)" ]] || die "reviewer effort does not match its state"
        fi
        [[ $verdict == ACCEPT || $verdict == FINDINGS || $verdict == BLOCKED ]] || die "verdict must be ACCEPT, FINDINGS or BLOCKED"
        [[ $reviewed_head =~ ^[0-9a-f]{40}$ ]] || die "--reviewed-head must be a full commit"
        [[ -n $response_file && -f $response_file && ! -L $response_file ]] || die "--response-file must be a regular file"
        grep -Fxq "Reviewed head: $reviewed_head" "$response_file" || die "response does not declare the reviewed head"
        grep -Fxq "Verdict: $verdict" "$response_file" || die "response does not declare the verdict"
        pending=$(state_get pending)
        pending_head=$(state_get pending_head)
        [[ -n $pending && $request_id == "$pending" ]] || die "request does not match pending review: ${pending:--}"
        [[ $reviewed_head == "$pending_head" ]] || die "reviewed head does not match request: expected $pending_head"
        if [[ $verdict != BLOCKED ]]; then
            current_head=$(git -C "$repo_root" rev-parse HEAD)
            [[ $current_head == "$reviewed_head" ]] || die "HEAD moved during review; complete it as BLOCKED"
            # "Stop editing while a request is pending" used to be an unenforced
            # promise: pinning the commit detects a moved HEAD, not uncommitted
            # edits. A verdict now requires the worktree to be as frozen as the
            # request claimed. BLOCKED stays available as the escape hatch.
            [[ -z $(git -C "$repo_root" status --porcelain --untracked-files=all) ]] \
                || die "worktree is dirty; the reviewed commit was not frozen — complete it as BLOCKED"
        fi

        response_path=$responses_dir/$request_id.md
        [[ ! -e $response_path ]] || die "response already exists: $response_path"

        # The response file's existence is what `await` polls, so it is the
        # commit point and must be published LAST — after every state_set. Stage
        # it under a dot-name and rename it into place, or an await that wins the
        # race reads a response whose state has not landed yet and dies on the
        # last_request check.
        response_staged=$responses_dir/.$request_id.md.$$
        cp -- "$response_file" "$response_staged"
        chmod 600 "$response_staged"
        state_set last_request "$request_id"
        state_set last_verdict "$verdict"
        state_set last_response "$response_path"
        [[ $verdict != ACCEPT ]] || state_set accepted_head "$reviewed_head"
        state_set pending ""
        state_set pending_head ""
        state_set ready 1
        mv -- "$response_staged" "$response_path"
        chain_event --kind review.complete --point --status ok \
            --name "Review $request_id completed" \
            --attribute request_id="$request_id" --attribute verdict="$verdict"
        printf 'completed request=%s verdict=%s head=%s response=%s\n' \
               "$request_id" "$verdict" "$reviewed_head" "$response_path"
        ;;

    await)
        [[ -f $state_file ]] || die "reviewer has not been started"
        [[ $request_id =~ ^[A-Za-z0-9][A-Za-z0-9._-]*$ ]] || die "--request is required"
        [[ $timeout_seconds =~ ^[1-9][0-9]*$ && $timeout_seconds -le 3600 ]] || die "timeout must be 1..3600 seconds"
        await_span=review-await:$request_id:$$
        response_path=$responses_dir/$request_id.md
        chain_event --kind review.await --start --span "$await_span" \
            --status blocked --name "Await review $request_id" \
            --attribute request_id="$request_id"
        if [[ ! -f $response_path ]]; then
            pending=$(state_get pending)
            [[ $pending == "$request_id" ]] || die "request is neither pending nor complete: $request_id"

            # Poll the durable response rather than blocking on a tmux channel.
            # A review takes minutes; half a second of latency is free, and the
            # file is the thing that actually decides the verdict. This removes
            # the completion/await race, the channel bookkeeping, and every
            # failure mode where the tmux server outlives or predeceases one
            # side of the handshake.
            waited=0   # counted in half-seconds, the poll interval
            while [[ ! -f $response_path ]]; do
                if ((waited >= timeout_seconds * 2)); then
                    chain_event --kind review.await --finish --span "$await_span" \
                        --status timeout --name "Review $request_id timed out" \
                        --attribute request_id="$request_id"
                    die "timed out waiting for $request_id"
                fi
                sleep 0.5
                waited=$((waited + 1))
            done
        fi
        if [[ ! -f $response_path ]]; then
            chain_event --kind review.await --finish --span "$await_span" \
                --status error --name "Review $request_id response missing" \
                --attribute request_id="$request_id"
            die "reviewer signalled without a response: $request_id"
        fi
        cat "$response_path"
        if [[ $(state_get last_request) != "$request_id" ]]; then
            chain_event --kind review.await --finish --span "$await_span" \
                --status error --name "Review $request_id state mismatch" \
                --attribute request_id="$request_id"
            die "response state does not match $request_id"
        fi
        case "$(state_get last_verdict)" in
            ACCEPT)
                chain_event --kind review.await --finish --span "$await_span" \
                    --status ok --name "Review $request_id accepted" \
                    --attribute request_id="$request_id"
                exit 0
                ;;
            FINDINGS)
                chain_event --kind review.await --finish --span "$await_span" \
                    --status error --name "Review $request_id returned findings" \
                    --attribute request_id="$request_id"
                exit 3
                ;;
            BLOCKED)
                chain_event --kind review.await --finish --span "$await_span" \
                    --status interrupted --name "Review $request_id blocked" \
                    --attribute request_id="$request_id"
                exit 4
                ;;
            *)
                chain_event --kind review.await --finish --span "$await_span" \
                    --status error --name "Review $request_id verdict unknown" \
                    --attribute request_id="$request_id"
                die "unknown stored verdict"
                ;;
        esac
        ;;

    status)
        [[ -f $state_file ]] || die "reviewer has not been started"
        reviewer_pane=$(state_get pane)
        alive=0
        pane_alive "$reviewer_pane" && alive=1
        printf 'window=%s pane=%s alive=%s provider=%s model=%s effort=%s ready=%s recovered=%s pending=%s accepted=%s last=%s verdict=%s\n' \
               "$(state_get window)" "$reviewer_pane" "$alive" "$(state_get provider)" \
               "$(state_get model)" "$(state_get effort)" "$(state_get ready)" \
               "$(state_get recovered)" "$(state_get pending)" "$(state_get accepted_head)" \
               "$(state_get last_request)" "$(state_get last_verdict)"
        ;;

    guard)
        [[ -f $state_file ]] || die "reviewer has not been started"
        reviewer_pane=$(state_get pane)
        pane_alive "$reviewer_pane" || die "reviewer pane is not alive: $reviewer_pane"
        [[ $(state_get ready) == 1 ]] || die "reviewer is not ready"
        [[ -z $(state_get pending) ]] || die "review is still pending: $(state_get pending)"
        [[ $(state_get last_verdict) == ACCEPT ]] || die "latest review is not ACCEPT"
        head_commit=$(git -C "$repo_root" rev-parse HEAD)
        [[ $(state_get accepted_head) == "$head_commit" ]] || die "HEAD is not the accepted commit: $head_commit"
        [[ -z $(git -C "$repo_root" status --porcelain --untracked-files=all) ]] \
            || die "worktree is not clean; the accepted commit is not what would be handed off"
        chain_event --kind review.guard --point --status ok \
            --name 'Review handoff guard passed' --attribute action=guard
        printf 'review guard passed head=%s reviewer-pane=%s\n' "$head_commit" "$reviewer_pane"
        ;;

    *) usage; die "unknown action: $action" ;;
esac
