#!/bin/sh
# Deterministic clustering and exact-pass-set ratchet for Sprint 56 torture
# matrix result streams.  Inputs are deliberately flat TSV so malformed
# harness output is refused before it can weaken passing.txt.
set -eu
LC_ALL=C
export LC_ALL
repo=$(CDPATH='' cd "$(dirname "$0")/.." && pwd -P)
policy=${CGF_TORTURE_TRIAGE_POLICY:-$repo/tests/torture-triage-policy.tsv}

usage()
{
    echo "usage: triage-torture.sh [--emit-passing FILE] [--output FILE] RESULTS..." >&2
    echo "       triage-torture.sh --gate PASSING RESULTS..." >&2
    exit 3
}

die()
{
    echo "triage-torture: $*" >&2
    exit 3
}

canonical_destination()
{
    destination=$1
    kind=$2
    case $destination in
    /* | */*) absolute=$destination ;;
    *) absolute=./$destination ;;
    esac
    parent=$(dirname "$absolute")
    base=$(basename "$absolute")
    [ -d "$parent" ] || die "$kind output directory does not exist: $parent"
    physical_parent=$(CDPATH='' cd "$parent" && pwd -P) ||
        die "cannot resolve $kind output directory: $parent"
    if [ "$physical_parent" = / ]; then
        printf '/%s\n' "$base"
    else
        printf '%s/%s\n' "$physical_parent" "$base"
    fi
}

emit=
output=
gate=
while [ "$#" -gt 0 ]; do
    case $1 in
    --emit-passing)
        [ "$#" -ge 2 ] || usage
        [ -z "$emit" ] || die "--emit-passing specified more than once"
        emit=$2
        shift 2
        ;;
    --output)
        [ "$#" -ge 2 ] || usage
        [ -z "$output" ] || die "--output specified more than once"
        output=$2
        shift 2
        ;;
    --gate)
        [ "$#" -ge 2 ] || usage
        [ -z "$gate" ] || die "--gate specified more than once"
        gate=$2
        shift 2
        ;;
    --)
        shift
        break
        ;;
    -*) usage ;;
    *) break ;;
    esac
done

[ "$#" -gt 0 ] || die "no result streams supplied"
if [ -n "$gate" ] && { [ -n "$emit" ] || [ -n "$output" ]; }; then
    die "--gate cannot be combined with output modes"
fi
if [ -z "$gate" ] && [ -z "$emit" ] && [ -z "$output" ]; then
    usage
fi
if [ -n "$emit" ]; then
    emit=$(canonical_destination "$emit" passing)
fi
if [ -n "$output" ]; then
    output=$(canonical_destination "$output" report)
fi
if [ -n "$emit" ] && [ -n "$output" ]; then
    if [ "$emit" = "$output" ] ||
        { [ -e "$emit" ] && [ -e "$output" ] && [ "$emit" -ef "$output" ]; }; then
        die "--emit-passing and --output require distinct paths"
    fi
fi

tmp=$(mktemp -d "${TMPDIR:-/tmp}/cgf-torture-triage.XXXXXX") ||
    die "cannot create temporary directory"
output_stage=
passing_stage=
output_final=
output_backup=
output_had_previous=0
output_published=0
cleanup()
{
    if [ "$output_published" -eq 1 ]; then
        if [ "$output_had_previous" -eq 1 ] && [ -n "$output_backup" ] &&
            [ -e "$output_backup" ]; then
            mv -f "$output_backup" "$output_final" ||
                echo "triage-torture: failed to roll back report output: $output_final" >&2
        else
            rm -f "$output_final" ||
                echo "triage-torture: failed to remove uncommitted report output: $output_final" >&2
        fi
        output_published=0
    fi
    [ -z "$output_backup" ] || rm -f "$output_backup"
    [ -z "$output_stage" ] || rm -f "$output_stage"
    [ -z "$passing_stage" ] || rm -f "$passing_stage"
    rm -rf "$tmp"
}
trap cleanup EXIT
trap 'exit 3' HUP INT TERM
rows=$tmp/rows.tsv
: >"$rows"
common_provenance=$tmp/common-provenance
input_number=0

for input do
    input_number=$((input_number + 1))
    [ -r "$input" ] || die "cannot read result stream: $input"
    [ -f "$input" ] || die "result stream is not a regular file: $input"
    input=$(canonical_destination "$input" result-stream)
    for destination in "$emit" "$output"; do
        [ -n "$destination" ] || continue
        if [ "$destination" = "$input" ] ||
            { [ -e "$destination" ] && [ "$destination" -ef "$input" ]; }; then
            die "output destination aliases result stream: $input"
        fi
    done
    input_provenance=$tmp/provenance.$input_number
    input_stream=$tmp/stream.$input_number
    if ! awk -v source="$input" -v provenance="$input_provenance" -v stream="$input_stream" '
        BEGIN { FS = "\t"; data = 0; bad = 0 }
        NR == 1 {
            if ($0 != "# cgf-torture-results-v2") {
                print "triage-torture: " source ":" NR ": expected # cgf-torture-results-v2 header" > "/dev/stderr"
                bad = 1
            }
            next
        }
        NR == 2 {
            if ($0 != "# columns=key\tsuite\tfile\tlevel\ttarget\toutcome\tsignal\tfingerprint\tphase\tdetail") {
                print "triage-torture: " source ":" NR ": expected exact tab-separated columns header" > "/dev/stderr"
                bad = 1
            }
            next
        }
        NR >= 3 && NR <= 10 {
            name[3] = "source-revision"
            name[4] = "compiler-source-sha256"
            name[5] = "harness-sha256"
            name[6] = "torture-manifest-sha256"
            name[7] = "ctestsuite-manifest-sha256"
            name[8] = "target"
            name[9] = "compiler-binary-sha256"
            name[10] = "compiler-driver-sha256"
            prefix = "# " name[NR] "="
            if (substr($0, 1, length(prefix)) != prefix) {
                print "triage-torture: " source ":" NR ": expected " prefix "VALUE header" > "/dev/stderr"
                bad = 1
                next
            }
            value = substr($0, length(prefix) + 1)
            if (NR == 3) {
                if (value != "unversioned" &&
                    ((length(value) != 40 && length(value) != 64) || value !~ /^[0-9a-f]+$/)) {
                    print "triage-torture: " source ":" NR ": source-revision must be 40 or 64 lowercase hexadecimal digits, or unversioned" > "/dev/stderr"
                    bad = 1
                }
            } else if (NR >= 4 && NR <= 7 || NR == 9 || NR == 10) {
                if (length(value) != 64 || value !~ /^[0-9a-f]+$/) {
                    print "triage-torture: " source ":" NR ": " name[NR] " must be 64 lowercase hexadecimal digits" > "/dev/stderr"
                    bad = 1
                }
            } else if (value !~ /^(x86_64-linux-gnu|arm64-linux)$/) {
                print "triage-torture: " source ":" NR ": invalid header target: " value > "/dev/stderr"
                bad = 1
            }
            header[NR] = value
            if (NR >= 3 && NR <= 7) print $0 > provenance
            if (NR == 10) print header[8] "\t" header[9] "\t" header[10] > stream
            next
        }
        /^#/ {
            print "triage-torture: " source ":" NR ": comment appears after required preamble" > "/dev/stderr"
            bad = 1
            next
        }
        {
            data = 1
            if (NF != 10) {
                print "triage-torture: " source ":" NR ": expected 10 tab-separated fields, got " NF > "/dev/stderr"
                bad = 1
                next
            }
            for (i = 1; i <= 10; i++) {
                if ($i == "") {
                    print "triage-torture: " source ":" NR ": field " i " is empty" > "/dev/stderr"
                    bad = 1
                    next
                }
                if ($i ~ /\r/) {
                    print "triage-torture: " source ":" NR ": carriage return in field " i > "/dev/stderr"
                    bad = 1
                    next
                }
            }
            if ($2 !~ /^(torture-compile|torture-execute|torture-execute-ieee|ctestsuite)$/) {
                print "triage-torture: " source ":" NR ": invalid suite: " $2 > "/dev/stderr"
                bad = 1
            }
            if ($3 !~ /^[A-Za-z0-9][A-Za-z0-9_.+\/-]*$/ || $3 ~ /(^|\/)\.\.?(\/|$)/) {
                print "triage-torture: " source ":" NR ": invalid file: " $3 > "/dev/stderr"
                bad = 1
            }
            if ($4 !~ /^(O0|O1|O2|O3|Os)$/) {
                print "triage-torture: " source ":" NR ": invalid level: " $4 > "/dev/stderr"
                bad = 1
            }
            if ($5 !~ /^(x86_64-linux-gnu|arm64-linux)$/) {
                print "triage-torture: " source ":" NR ": invalid target: " $5 > "/dev/stderr"
                bad = 1
            }
            if ($5 != header[8]) {
                print "triage-torture: " source ":" NR ": row target does not match header target: " $5 > "/dev/stderr"
                bad = 1
            }
            if ($6 !~ /^(PASS|SKIP|XFAIL|COMPILE_FAIL|OUTPUT_FAIL|WRONG_EXIT|SIGNAL|TIMEOUT|ICE)$/) {
                print "triage-torture: " source ":" NR ": invalid outcome: " $6 > "/dev/stderr"
                bad = 1
            }
            if ($9 !~ /^(pp|parse|sema|ir-verify|opt|cg|as|ld|run|ICE|policy)$/) {
                print "triage-torture: " source ":" NR ": invalid phase: " $9 > "/dev/stderr"
                bad = 1
            }
            if ($6 == "SIGNAL") {
                if ($7 !~ /^[0-9]+$/ || ($7 + 0) < 1 || ($7 + 0) > 127 || $7 != ($7 + 0) "") {
                    print "triage-torture: " source ":" NR ": SIGNAL requires canonical decimal signal 1..127" > "/dev/stderr"
                    bad = 1
                }
            } else if ($7 != "-") {
                print "triage-torture: " source ":" NR ": non-SIGNAL outcome requires signal -" > "/dev/stderr"
                bad = 1
            }
            expected = $2 "/" $3 "@" $4 "@" $5
            if ($1 != expected) {
                print "triage-torture: " source ":" NR ": key does not match row fields: " $1 > "/dev/stderr"
                bad = 1
            }
            if (!bad) print $0
        }
        END {
            if (NR == 0) {
                print "triage-torture: " source ": empty result stream" > "/dev/stderr"
                bad = 1
            } else if (NR < 10) {
                print "triage-torture: " source ": incomplete 10-line result preamble" > "/dev/stderr"
                bad = 1
            }
            exit bad ? 1 : 0
        }
    ' "$input" >>"$rows"; then
        exit 3
    fi
    if [ "$input_number" -eq 1 ]; then
        cp "$input_provenance" "$common_provenance"
    elif ! cmp -s "$common_provenance" "$input_provenance"; then
        die "shared provenance mismatch: $input"
    fi
    sed -n '1p' "$input_stream" >>"$tmp/stream-provenance"
done

[ -s "$rows" ] || die "result streams contain zero TSV rows"

cut -f1 "$rows" | sort >"$tmp/keys"
duplicate=$(uniq -d "$tmp/keys" | sed -n '1p')
[ -z "$duplicate" ] || die "duplicate result key: $duplicate"

# Structured metadata is a leading sequence of [class=...] and [tags=...]
# tokens in detail.  Strip it once into stable auxiliary columns so bucket
# diagnostics remain human-readable and every consumer sees the same parse.
if ! awk -F '\t' -v OFS='\t' '
    BEGIN { bad = 0 }
    {
        detail = $10
        class = "-"
        tags = "-"
        requirement = "-"
        xfail = "-"
        if (detail ~ /^xfail:TORT-[0-9][0-9][0-9]( |$)/) {
            xfail = substr(detail, 7, 8)
            detail = substr(detail, 15)
            sub(/^ /, "", detail)
            if ($6 != "XFAIL") {
                print "triage-torture: xfail metadata on non-XFAIL row " $1 > "/dev/stderr"
                bad = 1
            }
        }
        while (detail ~ /^\[(class|tags|requirement|xfail|disposition)=/) {
            close_at = index(detail, "]")
            if (!close_at) {
                print "triage-torture: malformed detail metadata for " $1 > "/dev/stderr"
                bad = 1
                next
            }
            token = substr(detail, 2, close_at - 2)
            detail = substr(detail, close_at + 1)
            sub(/^ /, "", detail)
            if (token ~ /^class=/) {
                value = substr(token, 7)
                if (class != "-") {
                    print "triage-torture: duplicate class metadata for " $1 > "/dev/stderr"
                    bad = 1
                }
                if (value !~ /^(gcc-builtin|nested-functions|complex|computed-goto|asm-goto|vector-mode-attribute)$/) {
                    print "triage-torture: invalid class metadata for " $1 ": " value > "/dev/stderr"
                    bad = 1
                }
                class = value
            } else if (token ~ /^tags=/) {
                value = substr(token, 6)
                if (tags != "-") {
                    print "triage-torture: duplicate tags metadata for " $1 > "/dev/stderr"
                    bad = 1
                }
                if ($2 != "ctestsuite") {
                    print "triage-torture: tags metadata is only valid for ctestsuite: " $1 > "/dev/stderr"
                    bad = 1
                }
                n = split(value, tag, ",")
                previous = ""
                if (value == "-") n = 0
                for (i = 1; i <= n; i++) {
                    if (tag[i] !~ /^[a-z0-9][a-z0-9_-]*$/) {
                        print "triage-torture: invalid tag metadata for " $1 ": " tag[i] > "/dev/stderr"
                        bad = 1
                    }
                    if (previous != "" && tag[i] <= previous) {
                        print "triage-torture: tags metadata is not sorted unique for " $1 > "/dev/stderr"
                        bad = 1
                    }
                    previous = tag[i]
                }
                tags = value
            } else if (token ~ /^requirement=/) {
                value = substr(token, 13)
                if (requirement != "-" || value !~ /^[a-z0-9][a-z0-9_+-]*$/ || $6 != "SKIP") {
                    print "triage-torture: invalid or duplicate requirement metadata for " $1 > "/dev/stderr"
                    bad = 1
                }
                requirement = value
            } else {
                if (token ~ /^xfail=/) value = substr(token, 7)
                else if (token ~ /^disposition=xfail:/) value = substr(token, 19)
                else value = ""
                if (xfail != "-" || value !~ /^TORT-[0-9][0-9][0-9]$/ || $6 != "XFAIL") {
                    print "triage-torture: invalid or duplicate xfail metadata for " $1 > "/dev/stderr"
                    bad = 1
                }
                xfail = value
            }
        }
        if (detail ~ /\[(class|tags|requirement|xfail|disposition)=/) {
            print "triage-torture: detail metadata must precede diagnostic text for " $1 > "/dev/stderr"
            bad = 1
        }
        if ($6 == "SKIP" && requirement == "-" && detail ~ /^skip-unless:[a-z0-9][a-z0-9_+-]*$/)
            requirement = substr(detail, 13)
        if ($6 == "XFAIL" && xfail == "-") {
            print "triage-torture: XFAIL row requires exactly one TORT-NNN metadata ID: " $1 > "/dev/stderr"
            bad = 1
        }
        if (detail == "") detail = "-"
        if (!bad) print $1, $2, $3, $4, $5, $6, $7, $8, $9, detail, class, tags, requirement, xfail
    }
    END { exit bad ? 1 : 0 }
' "$rows" >"$tmp/enriched.tsv"; then
    exit 3
fi

validate_policy()
{
    source=$1
    destination=$2
    [ -r "$source" ] || die "cannot read triage policy: $source"
    [ -f "$source" ] || die "triage policy is not a regular file: $source"
    if ! awk -F '\t' -v OFS='\t' -v source="$source" '
        BEGIN { bad = 0; previous = "" }
        NR == 1 {
            if ($0 != "# cgf-torture-triage-policy-v1") {
                print "triage-torture: " source ":" NR ": expected # cgf-torture-triage-policy-v1 header" > "/dev/stderr"
                bad = 1
            }
            next
        }
        NR == 2 {
            if ($0 != "# columns=signal\tfingerprint\tphase\tvariant\thypothesis\tdisposition") {
                print "triage-torture: " source ":" NR ": expected exact policy columns header" > "/dev/stderr"
                bad = 1
            }
            next
        }
        /^#/ {
            print "triage-torture: " source ":" NR ": comment appears after policy preamble" > "/dev/stderr"
            bad = 1
            next
        }
        {
            if (NF != 6) {
                print "triage-torture: " source ":" NR ": expected 6 policy fields, got " NF > "/dev/stderr"
                bad = 1
                next
            }
            for (i = 1; i <= 6; i++) {
                if ($i == "" || $i ~ /[[:cntrl:]]/) {
                    print "triage-torture: " source ":" NR ": invalid policy field " i > "/dev/stderr"
                    bad = 1
                }
            }
            if ($1 != "-" && ($1 !~ /^[0-9]+$/ || ($1 + 0) < 1 || ($1 + 0) > 127 || $1 != ($1 + 0) "")) {
                print "triage-torture: " source ":" NR ": invalid policy signal: " $1 > "/dev/stderr"
                bad = 1
            }
            if ($3 !~ /^(pp|parse|sema|ir-verify|opt|cg|as|ld|run|ICE|policy)$/) {
                print "triage-torture: " source ":" NR ": invalid policy phase: " $3 > "/dev/stderr"
                bad = 1
            }
            if ($4 !~ /^(all|optdiv|non-optdiv)$/) {
                print "triage-torture: " source ":" NR ": invalid policy variant: " $4 > "/dev/stderr"
                bad = 1
            }
            if ($6 !~ /^(fix-sprint:[A-Za-z0-9][A-Za-z0-9._-]*|xfail:TORT-[0-9][0-9][0-9]|wontfix-0.1.0|out-of-scope)$/) {
                print "triage-torture: " source ":" NR ": invalid policy disposition: " $6 > "/dev/stderr"
                bad = 1
            }
            key = $1 "\t" $2 "\t" $3 "\t" $4
            if (previous != "" && key <= previous) {
                if (key == previous)
                    print "triage-torture: " source ":" NR ": duplicate policy bucket: " key > "/dev/stderr"
                else
                    print "triage-torture: " source ":" NR ": policy buckets are not LC_ALL=C sorted" > "/dev/stderr"
                bad = 1
            }
            previous = key
            if (!bad) print
        }
        END {
            if (NR < 2) {
                print "triage-torture: " source ": incomplete policy preamble" > "/dev/stderr"
                bad = 1
            }
            exit bad ? 1 : 0
        }
    ' "$source" >"$destination"; then
        exit 3
    fi
}

stage_passing()
{
    destination=$1
    case $destination in
    /* | */*) final=$destination ;;
    *) final=./$destination ;;
    esac
    destination_dir=$(dirname "$final")
    destination_base=$(basename "$final")
    [ -d "$destination_dir" ] || die "passing output directory does not exist: $destination_dir"
    awk -F '\t' '$6 == "PASS" { print $1 }' "$rows" | sort -u \
        >"$tmp/emit-observed"
    : >"$tmp/emit-preserved"
    if [ -e "$final" ]; then
        validate_passing "$final" "$tmp/emit-existing"
        awk -F '\t' '{ seen[$5] = 1 } END { for (target in seen) print target }' \
            "$rows" | sort >"$tmp/emit-targets"
        awk 'NR == FNR { target[$0] = 1; next }
            { n = split($0, part, "@"); if (target[part[n]]) print }' \
            "$tmp/emit-targets" "$tmp/emit-existing" >"$tmp/emit-existing-scope"
        awk 'NR == FNR { target[$0] = 1; next }
            { n = split($0, part, "@"); if (!target[part[n]]) print }' \
            "$tmp/emit-targets" "$tmp/emit-existing" >"$tmp/emit-preserved"
        comm -23 "$tmp/emit-existing-scope" "$tmp/emit-observed" \
            >"$tmp/emit-regressions"
        emit_status=0
        while IFS= read -r key; do
            [ -n "$key" ] || continue
            echo "triage-torture: regression: existing PASS key is not observed PASS: $key" >&2
            emit_status=1
        done <"$tmp/emit-regressions"
        [ "$emit_status" -eq 0 ] || return 1
    fi
    sort -u "$tmp/emit-preserved" "$tmp/emit-observed" >"$tmp/emit-merged"
    passing_stage=$(mktemp "$destination_dir/.$destination_base.tmp.XXXXXX") ||
        die "cannot stage passing output in: $destination_dir"
    if ! {
        echo '# cgf-torture-passing-v1'
        echo '# Exact PASS set; entries are full matrix-cell keys.'
        echo '# Weekly ritual: re-run the full matrix, fold new passes in, and review the top 3 buckets (30 minutes).'
        sed -n '1,$p' "$tmp/emit-merged"
    } >"$passing_stage"; then
        rm -f "$passing_stage"
        passing_stage=
        die "cannot stage passing output: $destination"
    fi
    if ! chmod 644 "$passing_stage"; then
        rm -f "$passing_stage"
        passing_stage=
        die "cannot set passing output permissions: $destination"
    fi
    passing_final=$final
}

validate_passing()
{
    passing=$1
    destination=$2
    [ -r "$passing" ] || die "cannot read passing file: $passing"
    [ -f "$passing" ] || die "passing path is not a regular file: $passing"
    if ! awk -v source="$passing" '
        BEGIN { bad = 0; previous = "" }
        NR == 1 {
            if ($0 != "# cgf-torture-passing-v1") {
                print "triage-torture: " source ":" NR ": expected # cgf-torture-passing-v1 header" > "/dev/stderr"
                bad = 1
            }
            next
        }
        NR == 2 {
            if ($0 != "# Exact PASS set; entries are full matrix-cell keys.") {
                print "triage-torture: " source ":" NR ": expected exact-pass-set contract comment" > "/dev/stderr"
                bad = 1
            }
            next
        }
        NR == 3 {
            if ($0 != "# Weekly ritual: re-run the full matrix, fold new passes in, and review the top 3 buckets (30 minutes).") {
                print "triage-torture: " source ":" NR ": expected weekly ritual comment" > "/dev/stderr"
                bad = 1
            }
            next
        }
        /^#/ {
            print "triage-torture: " source ":" NR ": comment appears after passing preamble" > "/dev/stderr"
            bad = 1
            next
        }
        {
            if ($0 !~ /^(torture-compile|torture-execute|torture-execute-ieee|ctestsuite)\/[A-Za-z0-9][A-Za-z0-9_.+\/-]*@(O0|O1|O2|O3|Os)@(x86_64-linux-gnu|arm64-linux)$/) {
                print "triage-torture: " source ":" NR ": invalid passing key: " $0 > "/dev/stderr"
                bad = 1
            }
            if ($0 ~ /(^|\/)\.\.?(\/|$)/) {
                print "triage-torture: " source ":" NR ": invalid passing key path: " $0 > "/dev/stderr"
                bad = 1
            }
            if (previous != "" && $0 <= previous) {
                if ($0 == previous)
                    print "triage-torture: " source ":" NR ": duplicate passing key: " $0 > "/dev/stderr"
                else
                    print "triage-torture: " source ":" NR ": passing keys are not LC_ALL=C sorted" > "/dev/stderr"
                bad = 1
            }
            previous = $0
            if (!bad) print $0
        }
        END {
            if (NR == 0) {
                print "triage-torture: " source ": empty passing file" > "/dev/stderr"
                bad = 1
            }
            exit bad ? 1 : 0
        }
    ' "$passing" >"$destination"; then
        exit 3
    fi
}

if [ -n "$gate" ]; then
    validate_passing "$gate" "$tmp/committed-all"
    awk -F '\t' '{ seen[$5] = 1 } END { for (target in seen) print target }' \
        "$rows" | sort >"$tmp/targets"
    awk -F '\t' '$6 == "PASS" { print $1 }' "$rows" | sort -u \
        >"$tmp/observed"
    awk 'NR == FNR { target[$0] = 1; next }
        { n = split($0, part, "@"); if (target[part[n]]) print }' \
        "$tmp/targets" "$tmp/committed-all" >"$tmp/committed"

    comm -23 "$tmp/committed" "$tmp/observed" >"$tmp/regressions"
    comm -13 "$tmp/committed" "$tmp/observed" >"$tmp/new-passes"
    status=0
    while IFS= read -r key; do
        [ -n "$key" ] || continue
        echo "triage-torture: regression: expected PASS key is not PASS: $key" >&2
        status=1
    done <"$tmp/regressions"
    while IFS= read -r key; do
        [ -n "$key" ] || continue
        echo "triage-torture: new pass: observed PASS key is not committed: $key" >&2
        status=1
    done <"$tmp/new-passes"
    exit "$status"
fi

[ -z "$output" ] || validate_policy "$policy" "$tmp/policy.tsv"

if [ -n "$output" ]; then
    case $output in
    /* | */*) output_final=$output ;;
    *) output_final=./$output ;;
    esac
    output_dir=$(dirname "$output_final")
    output_base=$(basename "$output_final")
    [ -d "$output_dir" ] || die "report output directory does not exist: $output_dir"
    output_stage=$(mktemp "$output_dir/.$output_base.tmp.XXXXXX") ||
        die "cannot stage report output in: $output_dir"

    # Baseline rows are generated in a flat intermediate form so the final
    # renderer never relies on awk hash iteration order.
    awk -F '\t' '
        {
            key = $2 SUBSEP $4 SUBSEP $5
            total[key]++
            if ($6 == "PASS") pass[key]++
            else if ($6 == "SKIP") skip[key]++
            else if ($6 == "XFAIL") xfail[key]++
            else failed[key]++
        }
        END {
            for (key in total) {
                split(key, part, SUBSEP)
                applicable = total[key] - skip[key]
                percent = applicable ? sprintf("%.2f%%", 100 * pass[key] / applicable) : "-"
                printf "%s\t%s\t%s\t%d\t%d\t%d\t%d\t%d\t%s\n", part[1], part[2], part[3],
                    total[key], pass[key] + 0, skip[key] + 0, xfail[key] + 0,
                    failed[key] + 0, percent
            }
        }
    ' "$rows" | sort -t '	' -k1,1 -k2,2 -k3,3 >"$tmp/baseline"

    # Mark every failing cell that diverges from a passing O0 cell for the
    # same suite/file/target.  Labels aggregate at bucket scope.
    awk -F '\t' '
        $6 == "PASS" && $4 == "O0" { base[$2 SUBSEP $3 SUBSEP $5] = 1 }
        { row[NR] = $0 }
        END {
            for (i = 1; i <= NR; i++) {
                split(row[i], f, "\t")
                if (f[6] != "PASS" && f[6] != "SKIP" && f[4] != "O0" &&
                    base[f[2] SUBSEP f[3] SUBSEP f[5]]) print f[1]
            }
        }
    ' "$rows" | sort -u >"$tmp/optdiv-keys"

    awk -F '\t' '
        $6 != "PASS" && $6 != "SKIP" && $11 != "-" { count[$11]++ }
        END {
            print "gcc-builtin\t" count["gcc-builtin"] + 0 "\twontfix-0.1.0"
            print "nested-functions\t" count["nested-functions"] + 0 "\twontfix-0.1.0"
            print "complex\t" count["complex"] + 0 "\tout-of-scope"
            print "computed-goto\t" count["computed-goto"] + 0 "\twontfix-0.1.0"
            print "asm-goto\t" count["asm-goto"] + 0 "\twontfix-0.1.0"
            print "vector-mode-attribute\t" count["vector-mode-attribute"] + 0 "\twontfix-0.1.0"
        }
    ' "$tmp/enriched.tsv" >"$tmp/classes"

    awk -F '\t' '
        $6 == "SKIP" { count[$13 SUBSEP $10]++ }
        END {
            for (key in count) {
                split(key, part, SUBSEP)
                print part[1] "\t" part[2] "\t" count[key]
            }
        }
    ' "$tmp/enriched.tsv" | sort -t '	' -k1,1 -k2,2 >"$tmp/skips"

    # Bucket record: count, signal, fingerprint, phase, optdiv member count,
    # pre-triage class/count, disposition.  Sorting supplies a stable tie-break.
    awk -F '\t' -v optfile="$tmp/optdiv-keys" '
        BEGIN {
            while ((getline key < optfile) > 0) optdiv[key] = 1
            close(optfile)
        }
        $6 != "PASS" && $6 != "SKIP" {
            split_kind = "all"
            if ($9 == "run" && $6 ~ /^(WRONG_EXIT|TIMEOUT|SIGNAL)$/) {
                if (optdiv[$1]) split_kind = "optdiv"
                else split_kind = "non-optdiv"
            }
            bucket = $7 SUBSEP $8 SUBSEP $9 SUBSEP split_kind
            count[bucket]++
            if (optdiv[$1]) opt[bucket]++
            class = $11
            if (class != "-") {
                precount[bucket]++
                if (pre[bucket] == "") pre[bucket] = class
                else if (pre[bucket] != class) pre[bucket] = "mixed"
            }
            if ($14 != "-") {
                xcount[bucket]++
                if (xid[bucket] == "") xid[bucket] = $14
                else if (xid[bucket] != $14) xid[bucket] = "mixed"
            }
        }
        END {
            for (bucket in count) {
                split(bucket, part, SUBSEP)
                disposition = "UNRESOLVED"
                if (xcount[bucket] == count[bucket] && xid[bucket] != "" && xid[bucket] != "mixed")
                    disposition = "xfail:" xid[bucket]
                else if (precount[bucket] == count[bucket] && pre[bucket] == "complex")
                    disposition = "out-of-scope"
                else if (precount[bucket] == count[bucket] && pre[bucket] != "" && pre[bucket] != "mixed")
                    disposition = "wontfix-0.1.0"
                preclass = pre[bucket]
                if (preclass == "") preclass = "none"
                printf "%d\t%s\t%s\t%s\t%s\t%d\t%s\t%d\t%s\n", count[bucket],
                    part[1], part[2], part[3], part[4], opt[bucket] + 0, preclass,
                    precount[bucket] + 0, disposition
            }
        }
    ' "$tmp/enriched.tsv" | sort -t '	' -k1,1nr -k2,2 -k3,3 -k4,4 -k5,5 >"$tmp/buckets"

    cut -f2-5 "$tmp/buckets" | sort -u >"$tmp/bucket-keys"
    cut -f1-4 "$tmp/policy.tsv" >"$tmp/policy-keys"
    comm -13 "$tmp/bucket-keys" "$tmp/policy-keys" >"$tmp/stale-policy-keys"

    {
        echo '# Sprint 56 Torture Triage'
        echo
        echo 'Generated deterministically from `# cgf-torture-results-v2` streams.'
        echo
        echo '## Provenance'
        echo
        awk -F '=' '{ label = substr($1, 3); printf "- %s: `%s`\n", label, $2 }' \
            "$common_provenance"
        echo
        echo '| Target | Compiler binary SHA-256 | Compiler driver SHA-256 |'
        echo '|---|---|---|'
        sort -t '	' -k1,1 -k2,2 -k3,3 "$tmp/stream-provenance" | \
            awk -F '\t' '{ printf "| %s | `%s` | `%s` |\n", $1, $2, $3 }'
        echo
        echo '## Baseline'
        echo
        echo '| Suite | Level | Target | Total | PASS | SKIP | XFAIL | Fail | Applicable pass |'
        echo '|---|---:|---|---:|---:|---:|---:|---:|---:|'
        awk -F '\t' '{ printf "| %s | %s | %s | %d | %d | %d | %d | %d | %s |\n", $1, $2, $3, $4, $5, $6, $7, $8, $9 }' "$tmp/baseline"
        echo
        echo '## Known Pre-triaged Classes'
        echo
        echo '| Class | Failed cells | Disposition |'
        echo '|---|---:|---|'
        awk -F '\t' '{ printf "| %s | %d | `%s` |\n", $1, $2, $3 }' "$tmp/classes"
        echo
        echo '## SKIP Policy'
        echo
        echo '| Requirement | Detail | Skipped cells |'
        echo '|---|---|---:|'
        if [ -s "$tmp/skips" ]; then
            awk -F '\t' '{ detail = $2; gsub(/\|/, "\\|", detail); printf "| %s | %s | %d |\n", $1, detail, $3 }' "$tmp/skips"
        else
            echo '| - | - | 0 |'
        fi
        echo
        echo '## Buckets'
        echo
    } >"$output_stage"

    bucket_number=0
    unresolved=0
    while IFS='	' read -r count signal fingerprint phase split_kind opt_count pre pre_count disposition; do
        bucket_number=$((bucket_number + 1))
        # Select and sort members by matching the three cluster fields.
        awk -F '\t' -v signal="$signal" -v fingerprint="$fingerprint" -v phase="$phase" \
            -v split_kind="$split_kind" -v optfile="$tmp/optdiv-keys" '
            BEGIN {
                while ((getline key < optfile) > 0) optdiv[key] = 1
                close(optfile)
            }
            $6 != "PASS" && $6 != "SKIP" && $7 == signal && $8 == fingerprint && $9 == phase &&
                (split_kind == "all" || (split_kind == "optdiv" && optdiv[$1]) ||
                    (split_kind == "non-optdiv" && !optdiv[$1])) {
                print $1 "\t" $10 "\t" $12
            }
        ' "$tmp/enriched.tsv" | sort -t '	' -k1,1 >"$tmp/members"
        exemplars=$(cut -f1 "$tmp/members" | sed -n '1,3p' | paste -sd ',' - | sed 's/,/, /g')
        diagnostic=$(cut -f2 "$tmp/members" | sed -n '1p')
        cut -f1 "$tmp/members" >"$tmp/member-keys"
        comm -12 "$tmp/optdiv-keys" "$tmp/member-keys" >"$tmp/optdiv-members"
        opt_exemplars=$(sed -n '1,3p' "$tmp/optdiv-members" | paste -sd ',' - | sed 's/,/, /g')
        [ -n "$opt_exemplars" ] || opt_exemplars=-
        tags=$(awk -F '\t' '$3 != "-" { n = split($3, tag, ","); for (i = 1; i <= n; i++) print tag[i] }' \
            "$tmp/members" | sort -u | paste -sd ',' - | sed 's/,/, /g')
        [ -n "$tags" ] || tags=-
        labels=
        [ "$opt_count" -eq 0 ] || labels="optdiv=$opt_count/$count"
        if [ "$pre_count" -ne 0 ]; then
            if [ -n "$labels" ]; then labels="$labels, pretriaged=$pre_count/$count"
            else labels="pretriaged=$pre_count/$count"
            fi
        fi
        [ -n "$labels" ] || labels=-
        hypothesis='UNRESOLVED — no durable policy hypothesis exists for this cluster.'
        case $disposition in
        xfail:TORT-*) hypothesis="Expected failure $disposition is recorded by manifest metadata." ;;
        esac
        if [ "$pre_count" -eq "$count" ]; then
            [ "$pre" != complex ] || hypothesis='Uses C complex arithmetic, outside the v0.1.0 language scope.'
            case $pre in
            gcc-builtin | nested-functions | computed-goto | asm-goto | vector-mode-attribute)
                hypothesis='Exercises a GNU extension tiered out in Sprint 55.'
                ;;
            esac
        fi

        policy_hypothesis=$(awk -F '\t' -v signal="$signal" -v fingerprint="$fingerprint" -v phase="$phase" -v variant="$split_kind" \
            '$1 == signal && $2 == fingerprint && $3 == phase && $4 == variant { print $5 }' "$tmp/policy.tsv")
        policy_disposition=$(awk -F '\t' -v signal="$signal" -v fingerprint="$fingerprint" -v phase="$phase" -v variant="$split_kind" \
            '$1 == signal && $2 == fingerprint && $3 == phase && $4 == variant { print $6 }' "$tmp/policy.tsv")
        if [ -n "$policy_hypothesis" ]; then
            hypothesis=$policy_hypothesis
            disposition=$policy_disposition
        fi
        if [ "$disposition" = UNRESOLVED ]; then
            unresolved=$((unresolved + 1))
        fi

        # Markdown table/control punctuation is escaped at the point of use.
        esc_fingerprint=$(printf '%s\n' "$fingerprint" | sed 's/|/\\|/g; s/`/\\`/g')
        esc_diagnostic=$(printf '%s\n' "$diagnostic" | sed 's/|/\\|/g; s/`/\\`/g')
        esc_hypothesis=$(printf '%s\n' "$hypothesis" | sed 's/`/\\`/g')
        {
            echo "### Bucket $bucket_number"
            echo
            echo "- Count: $count"
            echo "- Cluster: signal=\`$signal\`; phase=\`$phase\`"
            [ "$split_kind" = all ] || echo "- Runtime split: \`$split_kind\`"
            echo "- Fingerprint: \`$esc_fingerprint\`"
            echo "- Exemplars: $exemplars"
            echo "- Diagnostic: $esc_diagnostic"
            echo "- Labels: $labels"
            echo "- Tags: $tags"
            echo "- Optdiv members: $opt_count of $count"
            echo "- Optdiv exemplars: $opt_exemplars"
            echo "- Hypothesis: $esc_hypothesis"
            echo "- Disposition: \`$disposition\`"
            echo
        } >>"$output_stage"
    done <"$tmp/buckets"

    stale_count=$(wc -l <"$tmp/stale-policy-keys" | tr -d ' ')
    policy_count=$(wc -l <"$tmp/policy.tsv" | tr -d ' ')
    applied_count=$((policy_count - stale_count))
    {
        echo '## Policy Overlay'
        echo
        echo "- Applied decisions: $applied_count"
        echo "- Stale decisions: $stale_count"
        if [ "$stale_count" -gt 0 ]; then
            echo
            echo '| Signal | Fingerprint | Phase | Variant | Hypothesis | Disposition |'
            echo '|---|---|---|---|---|---|'
            while IFS='	' read -r stale_signal stale_fingerprint stale_phase stale_variant; do
                awk -F '\t' -v signal="$stale_signal" -v fingerprint="$stale_fingerprint" -v phase="$stale_phase" -v variant="$stale_variant" '
                    $1 == signal && $2 == fingerprint && $3 == phase && $4 == variant {
                        hypothesis = $5
                        fingerprint_out = $2
                        gsub(/\|/, "\\|", hypothesis)
                        gsub(/\|/, "\\|", fingerprint_out)
                        gsub(/`/, "\\`", fingerprint_out)
                        printf "| %s | `%s` | %s | %s | %s | `%s` |\n", $1, fingerprint_out, $3, $4, hypothesis, $6
                    }
                ' "$tmp/policy.tsv"
            done <"$tmp/stale-policy-keys"
        fi
        echo
    } >>"$output_stage"

    failed=$(awk -F '\t' '$6 != "PASS" && $6 != "SKIP" { n++ } END { print n + 0 }' "$rows")
    bucketed=$(awk -F '\t' '{ n += $1 + 0 } END { print n + 0 }' "$tmp/buckets")
    unbucketed=$((failed - bucketed))
    [ "$unbucketed" -eq 0 ] || die "internal error: $unbucketed failed cells were not bucketed"
    {
        echo '## Coverage'
        echo
        echo "- Failed cells: $failed"
        echo "- Bucketed cells: $bucketed"
        echo "- Unbucketed cells: $unbucketed"
        echo "- Unresolved buckets: $unresolved"
        echo '- Bucket coverage: 100.00%'
        echo '- Misc bucket share: 0.00% (no misc bucket is emitted)'
        echo '- Phase-end target: at least 95% of applicable execute cells passing.'
    } >>"$output_stage"
    if [ "$unresolved" -ne 0 ]; then
        output_status=1
    else
        output_status=0
    fi
fi

[ -z "$emit" ] || {
    if [ -z "$output" ] || [ "$output_status" -eq 0 ]; then
        stage_passing "$emit" || exit 1
    fi
}
if [ -n "$output" ]; then
    if ! chmod 644 "$output_stage"; then
        die "cannot set report output permissions: $output"
    fi
    if [ -n "$emit" ] && [ "$output_status" -eq 0 ]; then
        output_backup=$(mktemp "$output_dir/.$output_base.backup.XXXXXX") ||
            die "cannot stage report rollback copy in: $output_dir"
        if [ -e "$output_final" ]; then
            cp -p "$output_final" "$output_backup" ||
                die "cannot stage report rollback copy: $output"
            output_had_previous=1
        else
            rm -f "$output_backup"
            output_had_previous=0
        fi
    fi
    if ! mv "$output_stage" "$output_final"; then
        die "cannot publish report output: $output"
    fi
    output_stage=
    if [ -n "$emit" ] && [ "$output_status" -eq 0 ]; then
        output_published=1
    fi
fi
if [ -n "$output" ] && [ "$output_status" -ne 0 ]; then
    echo "triage-torture: $unresolved failure bucket(s) lack durable policy decisions" >&2
    exit "$output_status"
fi
if [ -n "$emit" ]; then
    if ! mv "$passing_stage" "$passing_final"; then
        die "cannot publish passing output: $emit"
    fi
    passing_stage=
    output_published=0
    [ -z "$output_backup" ] || rm -f "$output_backup"
    output_backup=
fi
