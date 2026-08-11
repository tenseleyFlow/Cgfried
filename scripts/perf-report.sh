#!/bin/sh
# Assemble a deterministic release performance report from committed evidence.
set -u
LC_ALL=C
export LC_ALL

usage()
{
    echo "usage: $0 --version VERSION --output FILE --baseline FILE... --latest FILE... --golden FILE... --dashboard FILE [--previous FILE]" >&2
    exit 3
}

die()
{
    echo "perf-report: $*" >&2
    exit 3
}

version=
output=
dashboard=
previous=
manifest=
while [ "$#" -gt 0 ]; do
    case $1 in
        --version) [ "$#" -ge 2 ] || usage; version=$2; shift 2 ;;
        --output) [ "$#" -ge 2 ] || usage; output=$2; shift 2 ;;
        --baseline | --latest | --golden)
            [ "$#" -ge 2 ] || usage
            kind=${1#--}
            manifest="$manifest
$kind	$2"
            shift 2
            ;;
        --dashboard) [ "$#" -ge 2 ] || usage; dashboard=$2; shift 2 ;;
        --previous) [ "$#" -ge 2 ] || usage; previous=$2; shift 2 ;;
        *) usage ;;
    esac
done
[ -n "$version" ] || die "missing --version"
case $version in *[!A-Za-z0-9._-]* | '') die "invalid version" ;; esac
[ -n "$output" ] || output=.benchmarks/report-$version.md
[ -n "$dashboard" ] || die "missing --dashboard"
[ -r "$dashboard" ] || die "cannot read $dashboard"
[ -z "$previous" ] || [ -r "$previous" ] || die "cannot read $previous"
for required in baseline latest golden; do
    printf '%s\n' "$manifest" | grep -q "^$required	" || die "missing --$required input"
done
[ -d "$(dirname "$output")" ] || die "output directory does not exist"

tmp=$(mktemp -d "${TMPDIR:-/tmp}/cgf-perf-report.XXXXXX") ||
    die "cannot create private temporary directory"
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
: >"$tmp/metrics"
: >"$tmp/provenance"

old_ifs=$IFS
IFS='
'
for entry in $manifest; do
    [ -n "$entry" ] || continue
    IFS='	' read -r kind file <<EOF
$entry
EOF
    IFS='
'
    [ -r "$file" ] || die "cannot read $file"
    source_name=$(basename "$file")
    awk -v kind="$kind" -v source="$source_name" -v file="$file" \
        -v metrics="$tmp/metrics" -v provenance="$tmp/provenance" '
        function'" "'trim(s) {
            sub(/^[[:space:]]+/, "", s); sub(/[[:space:]]+$/, "", s); return s
        }
        function'" "'fail(message) {
            print "perf-report: " file ":" FNR ": " message > "/dev/stderr"; bad = 1
        }
        {
            raw = $0
            line = raw
            sub(/^[[:space:]]*#[[:space:]]*/, "", line)
            line = trim(line)
            count = split(line, token, /[[:space:]]+/)
            for (part = 1; part <= count; part++) {
                token_equals = index(token[part], "=")
                if (!token_equals) continue
                provenance_key = substr(token[part], 1, token_equals - 1)
                provenance_value = substr(token[part], token_equals + 1)
                if (provenance_key == "target") target = provenance_value
                if (provenance_key ~ /^(host|host_class|date|cgf_rev|cgf_tree|protocol|sysroot_include|sysroot_crt)$/)
                    prov[provenance_key] = provenance_value
                else if (provenance_key == "date_utc")
                    prov["date"] = provenance_value
                else if (provenance_key == "timeit_protocol")
                    prov["protocol"] = provenance_value
            }
            equals = index(line, "=")
            if (!equals) {
                if (raw !~ /^[[:space:]]*#/ && line != "")
                    fail("expected metric=value")
                next
            }
            key = trim(substr(line, 1, equals - 1))
            value = trim(substr(line, equals + 1))
            if (raw ~ /^[[:space:]]*#/)
                next
            if (key !~ /^[A-Za-z0-9_.:-]+$/) {
                fail("invalid metric name " key); next
            }
            if (key in seen) {
                fail("duplicate metric " key); next
            }
            seen[key] = 1
            values[key] = value
        }
        END {
            if (target == "") {
                print "perf-report: " file ": missing unique target provenance" > "/dev/stderr"
                bad = 1
            }
            if (target !~ /^[A-Za-z0-9_.:-]+$/) {
                print "perf-report: " file ": invalid target provenance" > "/dev/stderr"
                bad = 1
            }
            scope = (prov["host_class"] != "" ? prov["host_class"] : prov["host"])
            if ((kind == "baseline" || kind == "latest") && scope == "") {
                print "perf-report: " file ": baseline/latest input lacks host_class or host provenance" > "/dev/stderr"
                bad = 1
            }
            if (kind == "baseline" || kind == "latest") {
                split("date cgf_rev cgf_tree protocol", required_provenance, " ")
                for (required_index = 1; required_index <= 4; required_index++) {
                    required_key = required_provenance[required_index]
                    if (prov[required_key] == "") {
                        print "perf-report: " file ": baseline/latest input lacks " \
                              required_key " provenance" > "/dev/stderr"
                        bad = 1
                    }
                }
            }
            if ((prov["sysroot_include"] == "") != (prov["sysroot_crt"] == "")) {
                print "perf-report: " file ": incomplete sysroot provenance" > "/dev/stderr"
                bad = 1
            }
            if (scope == "") scope = "static"
            if (scope !~ /^[A-Za-z0-9_.:-]+$/) {
                print "perf-report: " file ": invalid provenance scope" > "/dev/stderr"
                bad = 1
            }
            if (bad) exit 3
            for (key in values) {
                if (values[key] ~ /^[0-9]+([.][0-9]+)?$/ &&
                    (key ~ /(wall_ms_median|user_ms_median|sys_ms_median|maxrss_kb_max|runtime_ns_median|runtime_ms_median|size|size_stripped|size_unstripped|icount|text)$/ ||
                     key ~ /[.]stat[.](arena|intern)[.]/ ||
                     (kind == "golden" && key ~ /padding$/)))
                    print kind "\t" target "\t" scope "\t" key "\t" values[key] "\t" source >> metrics
            }
            printf "%s\t%s\t%s\t%s", kind, target, scope, source >> provenance
            for (i = 1; i <= 8; i++) {
                k = (i == 1 ? "host" : i == 2 ? "host_class" : i == 3 ? "date" :
                     i == 4 ? "cgf_rev" : i == 5 ? "cgf_tree" :
                     i == 6 ? "protocol" : i == 7 ? "sysroot_include" :
                     "sysroot_crt")
                printf "\t%s", (prov[k] != "" ? prov[k] : "n/a") >> provenance
            }
            print "" >> provenance
        }
    ' "$file" || exit $?
done
IFS=$old_ifs

sort -t '	' -k1,1 -k2,2 -k3,3 -k4,4 "$tmp/metrics" >"$tmp/metrics.sorted"
awk -F '	' '
    { id=$1 SUBSEP $2 SUBSEP $3 SUBSEP $4; if (id in seen) { print "perf-report: duplicate " $1 " metric " $2 "/" $3 "/" $4 > "/dev/stderr"; bad=1 } seen[id]=1 }
    END { if (bad) exit 3 }
' "$tmp/metrics.sorted" || exit $?

if [ -n "$previous" ]; then
    sed -n 's/^<!--[[:space:]]*perf-metric[[:space:]]\([^[:space:]]*\)[[:space:]]\([^[:space:]]*\)[[:space:]]\([^[:space:]]*\)[[:space:]]\([^[:space:]]*\)[[:space:]]*-->$/\1\t\2\t\3\t\4/p' \
        "$previous" | sort -t '	' -k1,1 -k2,2 -k3,3 >"$tmp/previous"
else
    : >"$tmp/previous"
fi

awk -F '	' '$1=="baseline" { print $2 "|" $3 "|" $4 "\t" $5 "\t" $6 }' "$tmp/metrics.sorted" >"$tmp/base"
awk -F '	' '$1=="latest" { print $2 "|" $3 "|" $4 "\t" $5 "\t" $6 }' "$tmp/metrics.sorted" >"$tmp/latest"
join -t '	' -a 1 -a 2 -e '@missing@' -o 0,1.2,1.3,2.2,2.3 \
    "$tmp/base" "$tmp/latest" >"$tmp/paired"
cut -f2 "$tmp/metrics.sorted" | sort -u >"$tmp/targets"

{
    echo "# Cgfried performance report — $version"
    echo
    echo "Generated from committed baselines, latest fleet results, static kernel goldens, and the Sprint 53 compiler comparison dashboard."
    while IFS= read -r target; do
        echo
        echo "## $target"
        echo
        echo '| scope | metric | baseline | latest | delta | since prior report | provenance |'
        echo '|---|---|---:|---:|---:|---:|---|'
        awk -F '	' -v target="$target" -v previous="$tmp/previous" '
            BEGIN {
                while ((getline line < previous) > 0) {
                    split(line, p, "\t"); old[p[1] SUBSEP p[2] SUBSEP p[3]] = p[4]
                }
                close(previous)
            }
            function'" "'pct(a,b) { return a+0==0 ? (b+0==0 ? "+0.0%" : "+inf") : sprintf("%+.1f%%", (b-a)*100/a) }
            {
                split($1, id, "|")
                if (id[1] != target) next
                if ($2 == "@missing@" || $4 == "@missing@") {
                    print "perf-report: unmatched baseline/latest metric " id[1] "/" id[2] "/" id[3] > "/dev/stderr"
                    bad=1; next
                }
                prior_id = id[1] SUBSEP id[2] SUBSEP id[3]
                prior = (prior_id in old ? pct(old[prior_id], $4) : "n/a")
                printf "| %s | %s | %s | %s | %s | %s | baseline:%s; latest:%s |\n", id[2],id[3],$2,$4,pct($2,$4),prior,$3,$5
                printf "<!-- perf-metric %s %s %s %s -->\n", id[1],id[2],id[3],$4
            }
            END { if (bad) exit 3 }
        ' "$tmp/paired" || exit $?
        echo
        echo '### Kernel static golden'
        echo
        echo '| metric | value | provenance |'
        echo '|---|---:|---|'
        awk -F '	' -v target="$target" '$1=="golden" && $2==target { printf "| %s | %s | golden:%s |\n",$4,$5,$6 }' "$tmp/metrics.sorted"
    done <"$tmp/targets"
    echo
    echo '## Provenance'
    echo
    echo '| kind | target | scope | source | host | host class | date | revision | tree | protocol | sysroot include | sysroot CRT |'
    echo '|---|---|---|---|---|---|---|---|---|---|---|---|'
    sort -t '	' -k1,1 -k2,2 -k3,3 -k4,4 "$tmp/provenance" | awk -F '	' '{ printf "| %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s |\n",$1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12 }'
    echo
    echo '## Sprint 53 compiler comparison dashboard'
    echo
    echo "Source: $(basename "$dashboard")"
    echo
    awk '{ print length ? "> " $0 : ">" }' "$dashboard"
} >"$tmp/report" || exit $?

cp "$tmp/report" "$output" || die "cannot write $output"
echo "perf-report: wrote $output"
