#!/bin/sh
# Audit the narrow performance-gate escape hatches introduced in Sprint 54.

set -eu

usage()
{
    cat <<'EOF'
usage: check_bench_policy.sh [--message-file FILE] [--diff-file FILE]
                             [--summary FILE] [--commit ID]
       check_bench_policy.sh --audit [--log-file FILE] [--since WHEN]
                             [--summary FILE]

Normal mode reads the HEAD commit message and changed paths unless fixture
files are supplied.  Audit log fixtures are TAB-separated SHA/message rows.
Exit status: 0 accepted, 1 policy violation, 3 malformed input or I/O error.
EOF
}

die_input()
{
    echo "bench-policy: $*" >&2
    exit 3
}

die_policy()
{
    echo "bench-policy: $*" >&2
    exit 1
}

need_arg()
{
    test "$#" -ge 2 || die_input "missing argument for $1"
}

audit=0
message_file=
diff_file=
summary_file=${GITHUB_STEP_SUMMARY-}
commit_id=
log_file=
since=1.month
tmp_dir=

# Invoked by the traps installed in make_tmp_dir.
# shellcheck disable=SC2329
cleanup()
{
    test -z "$tmp_dir" || rm -rf "$tmp_dir"
}

make_tmp_dir()
{
    if test -z "$tmp_dir"; then
        tmp_dir=$(mktemp -d "${TMPDIR:-/tmp}/cgf-bench-policy.XXXXXX") ||
            die_input "cannot create private temporary directory"
        trap cleanup EXIT HUP INT TERM
    fi
}

while test "$#" -gt 0; do
    case $1 in
        --audit)
            audit=1
            shift
            ;;
        --message-file)
            need_arg "$@"
            message_file=$2
            shift 2
            ;;
        --diff-file)
            need_arg "$@"
            diff_file=$2
            shift 2
            ;;
        --summary)
            need_arg "$@"
            summary_file=$2
            shift 2
            ;;
        --commit)
            need_arg "$@"
            commit_id=$2
            shift 2
            ;;
        --log-file)
            need_arg "$@"
            log_file=$2
            shift 2
            ;;
        --since)
            need_arg "$@"
            since=$2
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            die_input "unknown option: $1"
            ;;
    esac
done

append_report()
{
    if test -n "$summary_file"; then
        test ! -d "$summary_file" || die_input "summary path is a directory: $summary_file"
        if ! printf '%s\n' "$1" >>"$summary_file"; then
            die_input "cannot append summary: $summary_file"
        fi
    else
        printf '%s\n' "$1"
    fi
}

require_file()
{
    test -f "$1" && test -r "$1" || die_input "cannot read $2: $1"
}

validate_message()
{
    # A token is exact and issue numbers start at one.  Reject every use of
    # the keyword that is not such a token, rather than silently ignoring a
    # typo intended to override a red gate.
    if grep -q 'perf-override' "$1"; then
        if ! awk '
            {
                rest = $0
                while (match(rest, /perf-override/)) {
                    before = substr(rest, 1, RSTART - 1)
                    rest = substr(rest, RSTART)
                    if (before ~ /[A-Za-z0-9_-]$/ ||
                        rest !~ /^perf-override: #[1-9][0-9]*([^A-Za-z0-9_]|$)/)
                        bad = 1
                    sub(/^perf-override/, "", rest)
                }
            }
            END { exit bad ? 1 : 0 }
        ' "$1"; then
            die_input "malformed perf-override token (expected 'perf-override: #NNN')"
        fi
    fi
}

extract_baseline_evidence()
{
    evidence_path=$1
    evidence_message=$2
    awk -v path="$evidence_path" '
        index($0, "bench-baseline: " path " ") == 1 {
            mentioned++
            rest = substr($0, length("bench-baseline: " path " ") + 1)
            if (rest ~ /^[A-Za-z0-9_.:-]+ (absent|[0-9]+([.][0-9]+)?) -> (absent|[0-9]+([.][0-9]+)?); reason: .+$/) {
                metric = rest
                sub(/ .*/, "", metric)
                values = rest
                sub(/^[^ ]+ /, "", values)
                old = values
                sub(/ .*/, "", old)
                sub(/^[^ ]+ -> /, "", values)
                new = values
                sub(/;.*/, "", new)
                if (old == "absent" && new == "absent") bad = 1
                else if (seen[metric]++) bad = 1
                else print metric "\t" old "\t" new
            } else {
                bad = 1
            }
        }
        END { if (!mentioned || bad) exit 1 }
    ' "$evidence_message"
}

verify_baseline_replacement()
{
    replacement_commit=$1
    replacement_parent=$2
    replacement_path=$3
    replacement_message=$4

    make_tmp_dir
    evidence_blob=$tmp_dir/baseline-evidence
    extract_baseline_evidence "$replacement_path" "$replacement_message" \
        >"$evidence_blob" ||
        die_policy "baseline '$replacement_path' requires 'bench-baseline: PATH METRIC OLD|absent -> NEW|absent; reason: WHY'"
    old_blob=$tmp_dir/baseline-old
    new_blob=$tmp_dir/baseline-new
    git show "$replacement_parent:$replacement_path" >"$old_blob" 2>/dev/null ||
        die_input "cannot read parent baseline '$replacement_path'"
    git show "$replacement_commit:$replacement_path" >"$new_blob" 2>/dev/null ||
        die_input "cannot read replacement baseline '$replacement_path'"
    evidence_error=$(awk -F= -v old_file="$old_blob" -v new_file="$new_blob" \
        -v evidence_file="$evidence_blob" '
        function policy_metric(key) {
            return key ~ /(^|[.])(wall_ms_(median|mad)|user_ms_(median|mad)|sys_ms_(median|mad)|cpu_ms_(median|mad)|maxrss_kb_(median|mad|max)|size)$/
        }
        function read_metrics(file, values, counts, keys,    line,key,value) {
            while ((getline line < file) > 0) {
                if (line !~ /^[A-Za-z0-9_.:-]+=[0-9]+([.][0-9]+)?$/) continue
                key=line; sub(/=.*/,"",key)
                if (!policy_metric(key)) continue
                value=substr(line,length(key)+2)
                values[key]=value; counts[key]++; keys[key]=1
            }
            close(file)
        }
        BEGIN {
            read_metrics(old_file,old,old_count,keys)
            read_metrics(new_file,new,new_count,keys)
            while ((getline line < evidence_file) > 0) {
                split(line,field,"\t")
                evidence_old[field[1]]=field[2]
                evidence_new[field[1]]=field[3]
                evidence_seen[field[1]]=1
            }
            close(evidence_file)
            for (key in keys) {
                if (old_count[key] > 1 || new_count[key] > 1) {
                    print "metric " key " is not unique in baseline blobs"
                    bad=1
                    continue
                }
                actual_old=(key in old)?old[key]:"absent"
                actual_new=(key in new)?new[key]:"absent"
                if (actual_old == actual_new) continue
                changed[key]=1
                if (!(key in evidence_seen)) {
                    print "missing evidence for changed metric " key
                    bad=1
                } else if (evidence_old[key] != actual_old || evidence_new[key] != actual_new) {
                    print "evidence does not match actual " key " values (" actual_old " -> " actual_new ")"
                    bad=1
                }
            }
            for (key in evidence_seen)
                if (!(key in changed)) {
                    print "evidence metric " key " is not a changed gated metric"
                    bad=1
                }
            exit bad ? 1 : 0
        }
    ') || die_policy "baseline '$replacement_path' $evidence_error"
}

extract_overrides()
{
    awk '
        {
            rest = $0
            while (match(rest, /perf-override: #[1-9][0-9]*/)) {
                token = substr(rest, RSTART, RLENGTH)
                sub(/^perf-override: #/, "", token)
                if (!seen[token]++) {
                    if (out != "") out = out ", "
                    out = out "#" token
                }
                rest = substr(rest, RSTART + RLENGTH)
            }
        }
        END { print out }
    ' "$1"
}

verify_override_issues()
{
    verify=${CGF_PERF_VERIFY_ISSUES:-0}
    case $verify in
        0) return ;;
        1) ;;
        *) die_input "CGF_PERF_VERIFY_ISSUES must be 0 or 1" ;;
    esac
    repository=${GITHUB_REPOSITORY-}
    case $repository in
        */*) repository_owner=${repository%%/*}; repository_name=${repository#*/} ;;
        *) die_input "GITHUB_REPOSITORY must identify owner/repository" ;;
    esac
    case $repository_owner in
        '' | *[!A-Za-z0-9_.-]*)
            die_input "GITHUB_REPOSITORY must identify owner/repository"
            ;;
    esac
    case $repository_name in
        '' | *[!A-Za-z0-9_.-]*)
            die_input "GITHUB_REPOSITORY must identify owner/repository"
            ;;
    esac
    gh_cmd=${CGF_PERF_GH_CMD:-gh}
    command -v "$gh_cmd" >/dev/null 2>&1 || \
        die_input "GitHub issue checker not found: $gh_cmd"
    for issue in $(printf '%s\n' "$1" | tr -d '#,' ); do
        state=$($gh_cmd issue view "$issue" --repo "$repository" \
            --json state --jq .state 2>/dev/null) || \
            die_policy "perf override #$issue does not name a readable tracked issue"
        test "$state" = OPEN || \
            die_policy "perf override #$issue must name an open tracked issue"
    done
}

is_doc_path()
{
    case $1 in
        src/*|tests/*|Makefile|*/Makefile|*.mk|CMakeLists.txt|*/CMakeLists.txt|meson.build|*/meson.build|configure|configure.*|*/configure|BUILD|BUILD.*|*/BUILD|WORKSPACE|WORKSPACE.*|*/WORKSPACE|.github/*|.gitmodules|Cargo.toml|Cargo.lock|*/Cargo.toml|*/Cargo.lock|rust-toolchain*|*/rust-toolchain*)
            return 1
            ;;
        doc/*|docs/*|.docs/*|README|README.*|LICENSE|LICENSE.*|COPYING|COPYING.*|CHANGELOG|CHANGELOG.*|CONTRIBUTING|CONTRIBUTING.*)
            return 0
            ;;
        */*)
            return 1
            ;;
        *.md)
            return 0
            ;;
        *)
            return 1
            ;;
    esac
}

run_audit()
{
    test -z "$message_file$diff_file$commit_id" || \
        die_input "normal-mode inputs cannot be combined with --audit"

    make_tmp_dir
    audit_tmp=$tmp_dir/audit
    if test -n "$log_file"; then
        require_file "$log_file" "audit log"
        cp "$log_file" "$audit_tmp" || die_input "cannot copy audit log: $log_file"
    else
        hashes_tmp=$tmp_dir/hashes
        message_tmp=$tmp_dir/message
        git log --since="$since" --format='%H' >"$hashes_tmp" 2>/dev/null || \
            die_input "cannot read git log"
        : >"$audit_tmp" || die_input "cannot create audit input"
        while IFS= read -r sha; do
            test -n "$sha" || continue
            git show -s --format=%B "$sha" >"$message_tmp" 2>/dev/null || \
                die_input "cannot read commit message for $sha"
            skip_marker=
            override_marker=
            if grep -Fq '[bench skip]' "$message_tmp"; then
                skip_marker=' [bench skip]'
            fi
            if grep -q 'perf-override' "$message_tmp"; then
                validate_message "$message_tmp"
                audit_overrides=$(extract_overrides "$message_tmp")
                override_marker=" perf-override: $audit_overrides"
            fi
            test -n "$skip_marker$override_marker" || continue
            subject=$(git show -s --format=%s "$sha" 2>/dev/null | tr '\t' ' ') || \
                die_input "cannot read commit subject for $sha"
            printf '%s\t%s%s%s\n' "$sha" "$subject" "$skip_marker" "$override_marker" \
                >>"$audit_tmp" || die_input "cannot create audit input"
        done <"$hashes_tmp"
    fi

    if ! awk -F '\t' 'NF != 2 || $1 == "" || $2 == "" { exit 1 }' "$audit_tmp"; then
        die_input "malformed audit log (expected SHA<TAB>message rows)"
    fi

    audit_text=$(awk -F '\t' '
        BEGIN {
            print "## Monthly performance escape-hatch audit"
            print ""
            print "### [bench skip] commits"
            skips = 0
        }
        index($2, "[bench skip]") {
            print "- `" $1 "` " $2
            skips++
        }
        END {
            if (!skips) print "- none"
        }
    ' "$audit_tmp")
    override_text=$(awk -F '\t' '
        BEGIN {
            print ""
            print "### perf overrides"
            overrides = 0
        }
        index($2, "perf-override") {
            print "- `" $1 "` " $2
            overrides++
        }
        END {
            if (!overrides) print "- none"
        }
    ' "$audit_tmp")
    append_report "$audit_text
$override_text"
    exit 0
}

test "$audit" -eq 0 || run_audit
test -z "$log_file" || die_input "--log-file requires --audit"

policy_commit=${GITHUB_EVENT_COMMIT-}
policy_base=
if test -n "$commit_id"; then
    policy_commit=$commit_id
fi
test -n "$policy_commit" || policy_commit=HEAD

if test -n "$message_file"; then
    require_file "$message_file" "commit message"
    msg_path=$message_file
else
    make_tmp_dir
    msg_path=$tmp_dir/message
    if ! git show -s --format=%B "$policy_commit" >"$msg_path" 2>/dev/null; then
        die_input "cannot read commit message for $policy_commit"
    fi
fi
test -s "$msg_path" || die_input "commit message is empty"

if test -n "$diff_file"; then
    require_file "$diff_file" "diff path list"
    diff_path=$diff_file
else
    make_tmp_dir
    diff_path=$tmp_dir/diff
    event_base=${GITHUB_EVENT_BASE-}
    event_before=${GITHUB_EVENT_BEFORE-}
    zero_sha=0000000000000000000000000000000000000000
    if test -n "$event_base" && test "$event_base" != "$zero_sha"; then
        policy_base=$event_base
    elif test -n "$event_before" && test "$event_before" != "$zero_sha"; then
        policy_base=$event_before
    else
        policy_base=
    fi
    if test -n "$policy_base"; then
        git rev-parse --verify "$policy_base^{commit}" >/dev/null 2>&1 || \
            die_input "cannot resolve CI diff base $policy_base"
        git rev-parse --verify "$policy_commit^{commit}" >/dev/null 2>&1 || \
            die_input "cannot resolve CI diff head $policy_commit"
        git diff --name-only "$policy_base" "$policy_commit" >"$diff_path" 2>/dev/null || \
            die_input "cannot read CI event diff"
    elif git rev-parse --verify "$policy_commit^" >/dev/null 2>&1; then
        git diff-tree --no-commit-id --name-only -r \
            "$policy_commit^" "$policy_commit" >"$diff_path" 2>/dev/null || \
            die_input "cannot read commit diff"
    else
        git diff-tree --root --no-commit-id --name-only -r \
            "$policy_commit" >"$diff_path" 2>/dev/null || \
            die_input "cannot read root commit diff"
    fi
fi

validate_message "$msg_path"
overrides=$(extract_overrides "$msg_path")
test -z "$overrides" || verify_override_issues "$overrides"
if test -z "$commit_id"; then
    commit_id=$(git rev-parse --short "$policy_commit" 2>/dev/null || printf '%s' fixture)
fi

skip_state="not requested"
if grep -Fq '[bench skip]' "$msg_path"; then
    skip_state=accepted
    saw_path=0
    while IFS= read -r path || test -n "$path"; do
        test -n "$path" || die_input "blank path in diff input"
        saw_path=1
        if ! is_doc_path "$path"; then
            die_policy "[bench skip] is docs-only, but diff touches '$path'"
        fi
    done <"$diff_path"
    test "$saw_path" -eq 1 || die_policy "[bench skip] requires a non-empty docs-only diff"
fi

# DET-M-03: every replacement commit carries one locally verifiable numeric
# old-to-new metric and a reason. Initial publication and deletion are not
# replacements. In CI range mode each commit is checked independently, so a
# compliant tip cannot hide an earlier unexplained replacement.
if test -n "$diff_file"; then
    while IFS= read -r path || test -n "$path"; do
        case $path in
            .benchmarks/baseline-*.txt)
                extract_baseline_evidence "$path" "$msg_path" >/dev/null ||
                    die_policy "baseline '$path' requires 'bench-baseline: PATH METRIC OLD|absent -> NEW|absent; reason: WHY'"
                ;;
        esac
    done <"$diff_path"
else
    if test -z "$policy_base"; then
        if git rev-parse --verify "$policy_commit^" >/dev/null 2>&1; then
            policy_base=$policy_commit^
        else
            policy_base=$policy_commit
        fi
    fi
    commits_path=$tmp_dir/commits
    git rev-list --reverse "$policy_base..$policy_commit" >"$commits_path" 2>/dev/null ||
        die_input "cannot enumerate baseline policy range"
    while IFS= read -r replacement_commit; do
        test -n "$replacement_commit" || continue
        replacement_parent=$replacement_commit^
        replacement_message=$tmp_dir/message-$replacement_commit
        git show -s --format=%B "$replacement_commit" >"$replacement_message" 2>/dev/null ||
            die_input "cannot read commit message for $replacement_commit"
        replacement_paths=$tmp_dir/paths-$replacement_commit
        git diff-tree --no-commit-id --name-only --diff-filter=M -r \
            "$replacement_parent" "$replacement_commit" >"$replacement_paths" 2>/dev/null ||
            die_input "cannot read commit diff for $replacement_commit"
        while IFS= read -r path || test -n "$path"; do
            case $path in
                .benchmarks/baseline-*.txt)
                    verify_baseline_replacement "$replacement_commit" \
                        "$replacement_parent" "$path" "$replacement_message"
                    ;;
            esac
        done <"$replacement_paths"
    done <"$commits_path"
fi

test -n "$overrides" || overrides=none
append_report "## Performance policy audit

- commit: \`$commit_id\`
- bench skip: $skip_state
- perf override: $overrides"

exit 0
