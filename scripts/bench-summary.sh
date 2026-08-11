#!/bin/sh
# Render the Sprint 54 gate lattice for one target/runner class.
set -u
LC_ALL=C
export LC_ALL

usage()
{
    echo "usage: $0 --target TARGET --class CLASS [--bench BASE CURRENT] [--size BASE CURRENT] [--static BASE CURRENT]" >&2
    exit 3
}

die()
{
    echo "bench-summary: $*" >&2
    exit 3
}

target=
runner_class=
pairs=
while [ "$#" -gt 0 ]; do
    case $1 in
        --target) [ "$#" -ge 2 ] || usage; target=$2; shift 2 ;;
        --class) [ "$#" -ge 2 ] || usage; runner_class=$2; shift 2 ;;
        --bench | --size | --static)
            [ "$#" -ge 3 ] || usage
            pairs="$pairs
${1#--}	$2	$3"
            shift 3
            ;;
        --) shift; break ;;
        -*) usage ;;
        *) break ;;
    esac
done

# Backward-compatible compact form: TARGET CLASS BASE CURRENT.
if [ -z "$target" ] && [ "$#" -eq 4 ]; then
    target=$1
    runner_class=$2
    pairs="bench	$3	$4"
    shift 4
fi
[ "$#" -eq 0 ] || usage
[ -n "$target" ] || die "missing --target"
[ -n "$runner_class" ] || die "missing --class"
[ -n "$pairs" ] || die "at least one metric pair is required"
case $target:$runner_class in *'|'* | *'`'* | *'<'* | *'>'*) die "unsafe target or class name" ;; esac

tmp=${TMPDIR:-/tmp}/cgf-bench-summary.$$
mkdir "$tmp" 2>/dev/null || die "cannot create temporary directory"
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
: >"$tmp/all"

normalize()
{
    side=$1
    kind=$2
    file=$3
    [ -r "$file" ] || die "cannot read $file"
    awk -v side="$side" -v kind="$kind" -v file="$file" '
        function'" "'fail(message) {
            print "bench-summary: " file ":" FNR ": " message > "/dev/stderr"
            bad = 1
        }
        function'" "'trim(s) {
            sub(/^[[:space:]]+/, "", s)
            sub(/[[:space:]]+$/, "", s)
            return s
        }
        {
            line = $0
            sub(/[[:space:]]*#.*/, "", line)
            line = trim(line)
            if (line == "")
                next
            equals = index(line, "=")
            if (!equals) {
                fail("expected metric=value")
                next
            }
            key = trim(substr(line, 1, equals - 1))
            value = trim(substr(line, equals + 1))
            if (key !~ /^[A-Za-z0-9_.:-]+$/) {
                fail("invalid metric name " key)
                next
            }
            if (key in seen) {
                fail("duplicate metric " key)
                next
            }
            seen[key] = 1
            print side "\t" kind "\t" key "\t" value
        }
        END { if (bad) exit 3 }
    ' "$file" >>"$tmp/all" || exit $?
}

old_ifs=$IFS
IFS='
'
for pair in $pairs; do
    [ -n "$pair" ] || continue
    IFS='	' read -r kind base current <<EOF
$pair
EOF
    IFS='
'
    normalize B "$kind" "$base" || exit $?
    normalize C "$kind" "$current" || exit $?
done
IFS=$old_ifs

awk -F '	' -v dir="$tmp" '
    {
        id = $1 SUBSEP $3
        if (id in seen) {
            print "bench-summary: duplicate " ($1 == "B" ? "baseline" : "current") \
                  " metric " $3 > "/dev/stderr"
            bad = 1
        }
        seen[id] = 1
        print $3 "\t" $4 "\t" $2 > (dir "/" $1)
    }
    END { if (bad) exit 3 }
' "$tmp/all" || exit $?
sort -t '	' -k1,1 "$tmp/B" >"$tmp/B.sorted"
sort -t '	' -k1,1 "$tmp/C" >"$tmp/C.sorted"
join -t '	' -a 1 -a 2 -e '@missing@' -o 0,1.2,1.3,2.2,2.3 \
    "$tmp/B.sorted" "$tmp/C.sorted" >"$tmp/joined"

awk_status=0
awk -F '	' -v runner_class="$runner_class" '
    function'" "'recognized(k) {
        return k ~ /(wall_ms_median|user_ms_median|sys_ms_median|maxrss_kb_max|size|size_stripped|size_unstripped|icount|text)$/ ||
               k ~ /[.]stat[.](arena|intern)[.]/
    }
    function'" "'fail(message) {
        print "bench-summary: " message > "/dev/stderr"
        invalid = 1
    }
    function'" "'numeric(v) { return v ~ /^[0-9]+([.][0-9]+)?$/ }
    function'" "'delta(base, cur,    d) {
        if (base + 0 == 0)
            return (cur + 0 == 0 ? "+0.0%" : "+inf")
        d = ((cur + 0) - (base + 0)) * 100 / (base + 0)
        return sprintf("%+.1f%%", d)
    }
    function'" "'emit(rank, key, base, cur, threshold, pass,    verdict) {
        verdict = threshold == "report-only" ? "REPORT (report-only)" :
                  threshold ~ /^n\/a/ ? "N/A (" threshold ")" :
                  (pass ? "PASS (" threshold ")" : "FAIL (" threshold ")")
        if (!pass && threshold != "report-only" && threshold !~ /^n\/a/)
            gate_failed = 1
        printf "%03d\t%s\t| %s | %s | %s | %s | %s |\n", \
               rank, key, key, base, cur, delta(base, cur), verdict
    }
    function'" "'emit_na(rank, key, cur, threshold) {
        printf "%03d\t%s\t| %s | n/a | %s | n/a | N/A (%s) |\n", \
               rank, key, key, cur, threshold
    }
    function'" "'emit_report_na(rank, key, cur) {
        printf "%03d\t%s\t| %s | n/a | %s | n/a | REPORT (report-only) |\n", \
               rank, key, key, cur
    }
    {
        key = $1
        if (!recognized(key))
            next
        shared_time = (runner_class == "ci" || runner_class == "shared-ci" ||
                       runner_class == "arm64-ci") &&
                      key ~ /(wall_ms_median|user_ms_median|sys_ms_median)$/
        current_only_report = key ~ /[.]stat[.](arena|intern)[.]/
        if ($4 == "@missing@" ||
            ($2 == "@missing@" && !shared_time && !current_only_report)) {
            fail("metric " key " is not present in both baseline and current inputs")
            next
        }
        if ($3 != $5) {
            if ($2 == "@missing@" && $3 == "@missing@" &&
                (shared_time || current_only_report))
                $3 = $5
            else {
                fail("metric " key " changed input class between baseline and current")
                next
            }
        }
        if ($2 == "@missing@" && (shared_time || current_only_report)) {
            if (!numeric($4)) {
                fail("metric " key " must be a non-negative decimal number")
                next
            }
            base[key] = "@missing@"
            cur[key] = $4
            kind[key] = $5
            next
        }
        if (!numeric($2) || !numeric($4)) {
            fail("metric " key " must be a non-negative decimal number")
            next
        }
        base[key] = $2
        cur[key] = $4
        kind[key] = $3
        found++
    }
    END {
        for (key in base) {
            if (key ~ /wall_ms_median$/) {
                if (base[key] == "@missing@")
                    emit_na(10, key, cur[key], "shared CI never; fleet +30%")
                else if (runner_class == "ci" || runner_class == "shared-ci" ||
                         runner_class == "arm64-ci")
                    emit(10, key, base[key], cur[key], "n/a: shared CI never; fleet +30%", 1)
                else
                    emit(10, key, base[key], cur[key], "+30%", cur[key] * 100 <= base[key] * 130)
            } else if (key ~ /user_ms_median$/) {
                prefix = key
                sub(/user_ms_median$/, "", prefix)
                sys = prefix "sys_ms_median"
                if (!(sys in base)) {
                    fail("metric " key " requires paired metric " sys)
                    continue
                }
                sumkey = prefix "user+sys_ms_median"
                if (base[key] == "@missing@" || base[sys] == "@missing@")
                    emit_na(20, sumkey, cur[key] + cur[sys],
                            "shared CI never; fleet +30%")
                else if (runner_class == "ci" || runner_class == "shared-ci" ||
                         runner_class == "arm64-ci")
                    emit(20, sumkey, base[key] + base[sys], cur[key] + cur[sys],
                         "n/a: shared CI never; fleet +30%", 1)
                else
                    emit(20, sumkey, base[key] + base[sys], cur[key] + cur[sys],
                         "+30%", (cur[key] + cur[sys]) * 100 <= (base[key] + base[sys]) * 130)
            } else if (key ~ /sys_ms_median$/) {
                prefix = key
                sub(/sys_ms_median$/, "", prefix)
                if (!((prefix "user_ms_median") in base))
                    fail("metric " key " requires paired user_ms_median")
            } else if (key ~ /maxrss_kb_max$/) {
                emit(30, key, base[key], cur[key], "+20%", cur[key] * 100 <= base[key] * 120)
            } else if (key ~ /(size_unstripped)$/) {
                emit(42, key, base[key], cur[key], "report-only", 1)
            } else if (key ~ /(size|size_stripped)$/) {
                emit(40, key, base[key], cur[key], "+15%", cur[key] * 100 <= base[key] * 115)
            } else if (key ~ /icount$/) {
                emit(50, key, base[key], cur[key], "max(+2%, +2 instr)",
                     !((cur[key] - base[key] > 2) && (cur[key] * 100 > base[key] * 102)))
            } else if (key ~ /text$/ && kind[key] == "static") {
                emit(60, key, base[key], cur[key], "+5%", cur[key] * 100 <= base[key] * 105)
            } else if (key ~ /text$/) {
                emit(61, key, base[key], cur[key], "report-only", 1)
            } else if (key ~ /[.]stat[.](arena|intern)[.]/) {
                if (base[key] == "@missing@")
                    emit_report_na(70, key, cur[key])
                else
                    emit(70, key, base[key], cur[key], "report-only", 1)
            }
        }
        if (!found)
            fail("inputs contain no comparable performance metrics")
        if (invalid)
            exit 3
        if (gate_failed)
            exit 1
    }
' "$tmp/joined" >"$tmp/rows" || awk_status=$?
[ "$awk_status" -eq 0 ] || [ "$awk_status" -eq 1 ] || exit "$awk_status"

{
    echo "## Perf summary — $target ($runner_class)"
    echo '| metric | baseline | current | delta | gate |'
    echo '|---|---:|---:|---:|---|'
    sort -t '	' -k1,1n -k2,2 "$tmp/rows" | cut -f3-
} >"$tmp/report"
cat "$tmp/report"
if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
    cat "$tmp/report" >>"$GITHUB_STEP_SUMMARY" || die "cannot append to GITHUB_STEP_SUMMARY"
fi
exit "$awk_status"
