#!/bin/sh
set -eu
export LC_ALL=C

root=$(CDPATH='' cd "$(dirname "$0")/.." && pwd -P)
check=$root/scripts/campaign-check.sh
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cgf-campaign-lint.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

fail() {
    echo "campaign-lint: $*" >&2
    exit 1
}

validate_evidence_path() {
    path=$1
    evidence_root=$2
    case $path in
        *'{'*'}'*)
            prefix=${path%%\{*}
            choices=${path#*\{}
            choices=${choices%%\}*}
            suffix=${path#*\}}
            old_ifs=$IFS
            IFS=,
            for choice in $choices; do
                if [ -f "$evidence_root/$prefix$choice$suffix" ]; then
                    IFS=$old_ifs
                    return 0
                fi
            done
            IFS=$old_ifs
            return 1
            ;;
        *) [ -f "$evidence_root/$path" ] ;;
    esac
}

lint_findings() {
    findings=$1
    evidence_root=$2
    [ -f "$findings" ] || fail "missing findings ledger: $findings"

    awk '
        $0 == "## Fixed compiler findings" { compiler = 1; next }
        compiler && /^## / { compiler = 0 }
        !compiler || $0 !~ /^\| `CAMP-[A-Z0-9-]+` \|/ { next }
        {
            count = split($0, cell, "|")
            if (count < 7) next
            id = cell[2]
            gsub(/^[[:space:]]*`|`[[:space:]]*$/, "", id)
            evidence = cell[6]
            found = 0
            while (match(evidence, /`[^`]+`/)) {
                path = substr(evidence, RSTART + 1, RLENGTH - 2)
                evidence = substr(evidence, RSTART + RLENGTH)
                if (path ~ /^(tests\/(programs|corpus|unit|warn|cross)\/|scripts\/)/) {
                    print id "\t" path
                    found = 1
                }
            }
            if (!found) {
                print "campaign-lint: " FILENAME ": " id \
                    " has no permanent regression path" > "/dev/stderr"
                bad = 1
            }
        }
        END { exit bad }
    ' "$findings" >"$tmp/finding-evidence" || exit 1

    while IFS="$(printf '\t')" read -r id path; do
        if ! validate_evidence_path "$path" "$evidence_root"; then
            fail "$findings: $id cites missing regression path: $path"
        fi
    done <"$tmp/finding-evidence"
}

lint_expected_findings() {
    expected=$1
    project=${expected##*/}
    project=${project%.expected}
    campaign_dir=$(dirname "$expected")
    descriptor_project=$project
    while [ ! -f "$campaign_dir/$descriptor_project.mk" ]; do
        case $descriptor_project in
            *-*) descriptor_project=${descriptor_project%-*} ;;
            *) fail "$expected: cannot resolve a descriptor namespace" ;;
        esac
    done
    identity=$(printf '%s' "$descriptor_project" |
        tr 'abcdefghijklmnopqrstuvwxyz' 'ABCDEFGHIJKLMNOPQRSTUVWXYZ')
    findings=$campaign_dir/FINDINGS.md
    [ -f "$findings" ] || fail "$expected: missing sibling FINDINGS.md"

    awk '
        BEGIN { FS = sprintf("%c", 9) }
        NR > 2 && ($2 == "SKIP" || $2 == "FAIL") {
            split($3, detail, ";")
            print NR "\t" detail[1]
        }
    ' "$expected" >"$tmp/deviation-ids"

    while IFS="$(printf '\t')" read -r line id; do
        [ -n "$id" ] || continue
        prefix=CAMP-$identity-
        case $id in
            "$prefix"[0-9][0-9][0-9]) ;;
            *) fail "$expected:$line: deviation ID must match $prefix"'NNN' ;;
        esac
        if ! grep -F "| \`$id\` |" "$findings" >/dev/null; then
            fail "$expected:$line: deviation ID is absent from FINDINGS.md: $id"
        fi
    done <"$tmp/deviation-ids"
}

lint_descriptor() {
    descriptor=$1
    [ -f "$descriptor" ] || fail "descriptor is not a file: $descriptor"

    name=${descriptor##*/}
    name=${name%.mk}
    case $name in
        ''|*[!a-z0-9-]*) fail "invalid campaign name: $name" ;;
    esac
    prefix=$(printf '%s' "$name" | tr 'abcdefghijklmnopqrstuvwxyz-' 'ABCDEFGHIJKLMNOPQRSTUVWXYZ_')

    awk -v file="$descriptor" -v ref="${prefix}_REF" -v src="${prefix}_SRC" '
        $0 ~ "^[[:space:]]*" ref "[[:space:]]*[:?+]?=" {
            line = $0
            sub("^[[:space:]]*" ref "[[:space:]]*[:?+]?=[[:space:]]*", "", line)
            sub(/[[:space:]]*#.*$/, "", line)
            if (line != "") have_ref = 1
        }
        $0 ~ "^[[:space:]]*" src "[[:space:]]*[:?+]?=" {
            line = $0
            sub("^[[:space:]]*" src "[[:space:]]*[:?+]?=[[:space:]]*", "", line)
            sub(/[[:space:]]*#.*$/, "", line)
            if (line != "") have_src = 1
        }
        END {
            if (!have_ref) {
                print "campaign-lint: " file ": missing nonempty " ref > "/dev/stderr"
                bad = 1
            }
            if (!have_src) {
                print "campaign-lint: " file ": missing nonempty " src > "/dev/stderr"
                bad = 1
            }
            exit bad
        }
    ' "$descriptor" || exit 1

    for stage in configure build validate expected; do
        if ! awk -v target="$name-$stage" '
            /^[^[:space:]#][^=]*:/ {
                head = $0
                sub(/:.*/, "", head)
                count = split(head, names, /[[:space:]]+/)
                for (i = 1; i <= count; i++)
                    if (names[i] == target) found = 1
            }
            END { exit !found }
        ' "$descriptor"; then
            fail "$descriptor: missing target $name-$stage"
        fi
    done

    expected=${descriptor%.mk}.expected
    [ -f "$expected" ] || fail "$descriptor: missing expected file $expected"
}

lint_ladder() {
    ladder=$1
    [ -f "$ladder" ] || fail "ladder manifest is not a file: $ladder"
    ladder_root=$(CDPATH='' cd "$(dirname "$ladder")/../.." && pwd -P)

    awk '
        funct''ion die(message) {
            print "campaign-lint: " FILENAME ":" NR ": " message > "/dev/stderr"
            bad = 1
        }
        /^[[:space:]]*($|#)/ { next }
        NR == 1 {
            if ($0 != "schema: cgf-campaign-ladder-v1") die("invalid schema")
            next
        }
        $0 == "campaigns:" {
            if (seen_campaigns) die("duplicate campaigns key")
            seen_campaigns = 1
            next
        }
        /^  - name: / {
            if (!seen_campaigns) die("campaign entry precedes campaigns key")
            if (field && field != 7) die("campaign entry does not have seven fields")
            field = 1
            name = substr($0, 11)
            if (name !~ /^[a-z0-9][a-z0-9-]*$/) die("invalid campaign name")
            names[++count] = name
            next
        }
        /^    (descriptor|expected|target|lanes|cadence|bar): / {
            if (!field) { die("campaign field precedes name"); next }
            split(substr($0, 5), pair, ": ")
            expected_key = (field == 1 ? "descriptor" :
                            field == 2 ? "expected" :
                            field == 3 ? "target" :
                            field == 4 ? "lanes" :
                            field == 5 ? "cadence" : "bar")
            if (pair[1] != expected_key) die("expected field " expected_key)
            value = substr($0, index($0, ": ") + 2)
            if (value == "" || value ~ /[[:space:]]/) die(pair[1] " must be a nonempty scalar")
            values[count, pair[1]] = value
            field++
            next
        }
        { die("unsupported YAML shape") }
        END {
            if (!seen_campaigns) die("missing campaigns key")
            if (field && field != 7) die("campaign entry does not have seven fields")
            if (count != 8) die("expected exactly 8 campaigns, got " count)
            expected = "chibicc curl lua musl qbe sqlite tinycc zlib"
            split(expected, inventory, " ")
            for (i = 1; i <= count; i++) {
                if (names[i] != inventory[i])
                    die("campaign inventory must be exact and sorted; entry " i " is " names[i])
                if (values[i, "descriptor"] != "ci/campaigns/" names[i] ".mk")
                    die("descriptor does not match campaign " names[i])
                if (values[i, "expected"] != "ci/campaigns/" names[i] ".expected")
                    die("expected path does not match campaign " names[i])
                if (values[i, "target"] != names[i] "-expected")
                    die("target does not match campaign " names[i])
                print names[i] "\t" values[i, "descriptor"] "\t" \
                    values[i, "expected"] "\t" values[i, "target"]
            }
            exit bad
        }
    ' "$ladder" >"$tmp/ladder-entries" || exit 1

    while IFS="$(printf '\t')" read -r name descriptor expected target; do
        descriptor=$ladder_root/$descriptor
        expected=$ladder_root/$expected
        [ -f "$descriptor" ] || fail "$ladder: missing descriptor for $name: $descriptor"
        [ -f "$expected" ] || fail "$ladder: missing expected file for $name: $expected"
        if ! awk -v target="$target" '
            /^[^[:space:]#][^=]*:/ {
                head = $0
                sub(/:.*/, "", head)
                count = split(head, names, /[[:space:]]+/)
                for (i = 1; i <= count; i++)
                    if (names[i] == target) found = 1
            }
            END { exit !found }
        ' "$descriptor"; then
            fail "$descriptor: missing ladder target $target"
        fi
    done <"$tmp/ladder-entries"
}

validate_ladder=0
bounded_descriptors=0
ladder=$root/ci/campaigns/ladder.yml
if [ "${1:-}" = --ladder ]; then
    [ "$#" -ge 2 ] || fail '--ladder requires a path'
    ladder=$2
    shift 2
    validate_ladder=1
fi

if [ "$#" -gt 0 ]; then
    bounded_descriptors=1
    descriptors=$tmp/descriptors
    : >"$descriptors"
    for descriptor do
        case $descriptor in
            /*) printf '%s\n' "$descriptor" ;;
            *) printf '%s\n' "$root/$descriptor" ;;
        esac
    done >"$descriptors"
else
    validate_ladder=1
    descriptors=$tmp/descriptors
    find "$root/ci/campaigns" -maxdepth 1 -type f -name '*.mk' \
        ! -name common.mk -print | LC_ALL=C sort >"$descriptors"
fi

[ -s "$descriptors" ] || fail 'no campaign descriptors found'

while IFS= read -r descriptor; do
    lint_descriptor "$descriptor"
done <"$descriptors"

[ "$validate_ladder" -eq 0 ] || lint_ladder "$ladder"
if [ "$bounded_descriptors" -eq 0 ]; then
    "$root/scripts/campaign-variants-lint.sh" --production \
        "$root/.github/workflows/torture-nightly.yml" \
        "$root/ci/campaigns/nightly-variants.tsv" >/dev/null
fi

if [ "$validate_ladder" -eq 1 ] && [ "$bounded_descriptors" -eq 1 ]; then
    cut -f 3 "$tmp/ladder-entries" |
        sed "s,^,$ladder_root/," >"$tmp/expected-files"
elif [ "$bounded_descriptors" -eq 1 ]; then
    sed 's/\.mk$/.expected/' "$descriptors" >"$tmp/expected-files"
else
    find "$root/ci/campaigns" -maxdepth 1 -type f -name '*.expected' -print |
        LC_ALL=C sort >"$tmp/expected-files"
fi
[ -s "$tmp/expected-files" ] || fail 'no campaign expected files found'
: >"$tmp/findings-files"
while IFS= read -r expected; do
    "$check" "$expected" "$expected" >/dev/null
    lint_expected_findings "$expected"
    printf '%s/FINDINGS.md\n' "$(dirname "$expected")" >>"$tmp/findings-files"
done <"$tmp/expected-files"

sort -u "$tmp/findings-files" >"$tmp/findings-files.sorted"
while IFS= read -r findings; do
    evidence_root=$(CDPATH='' cd "$(dirname "$findings")/../.." && pwd -P)
    lint_findings "$findings" "$evidence_root"
done <"$tmp/findings-files.sorted"

printf 'campaign-lint: PASS descriptors=%s expected=%s ladder=%s\n' \
    "$(wc -l <"$descriptors" | tr -d ' ')" \
    "$(wc -l <"$tmp/expected-files" | tr -d ' ')" "$validate_ladder"
