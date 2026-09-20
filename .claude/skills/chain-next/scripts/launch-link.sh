#!/usr/bin/env bash

set -euo pipefail

usage() {
    cat <<'EOF'
Usage: launch-link.sh --provider <codex|claude> \
                      --constitution <path> \
                      --window-prefix <prefix> \
                      --max-links <count> \
                      [--model <model>] [--effort <effort>] [--dry-run]

Launch exactly one fresh the project chain link with the inherited agent profile.
EOF
}

die() {
    printf 'chain-next: %s\n' "$*" >&2
    exit 1
}

normalize_codex_model() {
    case "$1" in
        sol)   printf '%s\n' gpt-5.6-sol ;;
        terra) printf '%s\n' gpt-5.6-terra ;;
        luna)  printf '%s\n' gpt-5.6-luna ;;
        *)     printf '%s\n' "$1" ;;
    esac
}

# Machine-readable chain parameters, so the window prefix and the cap are not
# parsed out of an English sentence by an agent. Only these keys are read.
chain_conf() {
    local key=$1 file=$epic_dir/chain.toml
    [[ -f $file ]] || return 0
    sed -n "s/^[[:space:]]*${key}[[:space:]]*=[[:space:]]*//p" "$file" \
        | head -1 | tr -d '"'\''' | tr -d '\r'
}

provider=
constitution_arg=
window_prefix=
max_links=
model=${CHAIN_MODEL:-}
effort=${CHAIN_EFFORT:-}
dry_run=0

while (($#)); do
    case "$1" in
        --provider)
            (($# >= 2)) || die "--provider needs a value"
            provider=$2
            shift 2
            ;;
        --constitution)
            (($# >= 2)) || die "--constitution needs a value"
            constitution_arg=$2
            shift 2
            ;;
        --window-prefix)
            (($# >= 2)) || die "--window-prefix needs a value"
            window_prefix=$2
            shift 2
            ;;
        --max-links)
            (($# >= 2)) || die "--max-links needs a value"
            max_links=$2
            shift 2
            ;;
        --model)
            (($# >= 2)) || die "--model needs a value"
            model=$2
            shift 2
            ;;
        --effort)
            (($# >= 2)) || die "--effort needs a value"
            effort=$2
            shift 2
            ;;
        --dry-run)
            dry_run=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            die "unknown argument: $1"
            ;;
    esac
done

[[ $provider == codex || $provider == claude ]] || die "provider must be codex or claude"
[[ -n $constitution_arg ]] || die "--constitution is required"
[[ -n ${TMUX:-} || $dry_run == 1 ]] || die "not running inside tmux"
# The window prefix and the cap are validated after epic_dir is known, so they
# can fall back to chain.toml.

inherited_provider=${CHAIN_PROVIDER:-}
inherited_model=${CHAIN_MODEL:-}
inherited_effort=${CHAIN_EFFORT:-}

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
    [[ $model == "$inherited_model_normalized" ]] || die "model switch refused: inherited $inherited_model_normalized, requested ${model:--}"
fi
if [[ -n $inherited_effort && $effort != "$inherited_effort" ]]; then
    die "effort switch refused: inherited $inherited_effort, requested $effort"
fi

command -v git >/dev/null || die "git is not available"
command -v tmux >/dev/null || die "tmux is not available"
command -v "$provider" >/dev/null || die "$provider is not available"

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
grep -Fq '$chain-next' "$constitution" || die "constitution does not invoke \$chain-next"

constitution_rel=${constitution#"$repo_root"/}
epic_dir=$(dirname "$constitution")
epic_name=$(basename "$epic_dir")

[[ -n $window_prefix ]] || window_prefix=$(chain_conf window_prefix)
[[ -n $max_links ]] || max_links=$(chain_conf cap)
[[ -n $window_prefix ]] || die "no --window-prefix and no window_prefix in $epic_dir/chain.toml"
[[ -n $max_links ]] || die "no --max-links and no cap in $epic_dir/chain.toml"
[[ $window_prefix =~ ^[a-z][a-z0-9-]*$ ]] || die "window prefix must match [a-z][a-z0-9-]*"
[[ $max_links =~ ^[1-9][0-9]*$ ]] || die "max links must be a positive integer"
counter=$epic_dir/.chain
done_file=$epic_dir/DONE

events_log=$epic_dir/.chain-events.log

# Observability observes; it does not live inside the mechanism. One NDJSON line
# per action, beside the chain counter. Nothing waits on it, and no failure here
# can reach a handoff decision.
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

refuse() {
    local message=$1 reason=$2
    chain_event --kind chain.handoff --point --status error \
        --name 'Successor handoff refused' --attribute reason="$reason"
    die "$message"
}

[[ ! -e $done_file ]] || refuse "chain is complete: $done_file exists" complete

if ((!dry_run)); then
    status=$(git -C "$repo_root" status --porcelain --untracked-files=all)
    [[ -z $status ]] || refuse "worktree is not clean; commit or stop before handoff" dirty

    if grep -Fq '$chain-review' "$constitution"; then
        review_script=$repo_root/.claude/skills/chain-review/scripts/review.sh
        [[ -x $review_script ]] || die "review guard is unavailable: $review_script"
        if ! "$review_script" guard --constitution "$constitution_rel"; then
            refuse "review guard refused the handoff" review-guard
        fi
    fi
fi

counter_existed=0
current=0
if [[ -e $counter ]]; then
    counter_existed=1
    current=$(<"$counter")
    [[ $current =~ ^[0-9]+$ ]] || die "counter is not a non-negative integer: $counter"
fi

next=$((current + 1))
((next <= max_links)) || refuse "chain cap reached: $next exceeds $max_links" chain-cap

window_name=$window_prefix-$next
if tmux list-windows -a -F '#{window_name}' 2>/dev/null | grep -Fxq "$window_name"; then
    refuse "tmux window already exists: $window_name" window-exists
fi

prompt="Read $constitution_rel completely and follow it."
handoff_span=handoff:$epic_name:$next:$(date +%s):$$
common_env=(env CHAIN_PROVIDER="$provider" CHAIN_MODEL="$model"
            CHAIN_EFFORT="$effort" CHAIN_ROLE=implementer
            CHAIN_CONSTITUTION="$constitution_rel"
            MERLIN_TIMELINE_HANDOFF_ID="$handoff_span"
            MERLIN_TIMELINE_ROLE=Implementer MERLIN_PROVIDER="$provider"
            MERLIN_MODEL="$model" MERLIN_EFFORT="$effort")

case "$provider" in
    codex)
        agent=("${common_env[@]}" codex --yolo --model "$model" -c "model_reasoning_effort=\"$effort\"" "$prompt")
        ;;
    claude)
        agent=("${common_env[@]}" claude --dangerously-skip-permissions --effort "$effort")
        [[ -z $model ]] || agent+=(--model "$model")
        agent+=("$prompt")
        ;;
esac

printf -v shell_command '%q ' "${agent[@]}"
shell_command+='|| exec zsh'

if ((dry_run)); then
    printf 'provider=%s\n' "$provider"
    printf 'model=%s\n' "${model:--}"
    printf 'effort=%s\n' "$effort"
    printf 'constitution=%s\n' "$constitution_rel"
    printf 'review-guard=%s\n' "$(grep -Fq '$chain-review' "$constitution" && printf required || printf absent)"
    printf 'counter=%s -> %s\n' "$current" "$next"
    printf 'window=%s\n' "$window_name"
    printf 'command=%s\n' "$shell_command"
    exit 0
fi

chain_event --kind chain.handoff --start --span "$handoff_span" \
    --status running --name "Launch $window_name" \
    --attribute outcome=requested --attribute link="$next"
printf '%s\n' "$next" > "$counter"

if ! pane=$(tmux new-window -P -F '#{pane_id}' -n "$window_name" -c "$repo_root" "$shell_command"); then
    if ((counter_existed)); then
        printf '%s\n' "$current" > "$counter"
    else
        unlink "$counter"
    fi
    chain_event --kind chain.handoff --finish --span "$handoff_span" \
        --status error --name "Launch $window_name failed" \
        --attribute outcome=failed --attribute link="$next"
    die "tmux failed to create $window_name; restored counter to $current"
fi

chain_event --kind chain.handoff --finish --span "$handoff_span" \
    --status ok --name "Launch $window_name complete" \
    --attribute outcome=launched --attribute link="$next"
chain_event --kind chain.handoff --point --parent "$handoff_span" \
    --status ok --name "Successor $window_name launched" \
    --attribute outcome=launched --attribute link="$next"
printf 'launched link=%s window=%s pane=%s provider=%s model=%s effort=%s\n' \
       "$next" "$window_name" "$pane" "$provider" "${model:--}" "$effort"
