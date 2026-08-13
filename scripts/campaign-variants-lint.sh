#!/bin/sh
set -eu

LC_ALL=C
export LC_ALL

usage() {
    echo "usage: $0 [--production WORKFLOW] MANIFEST" >&2
    exit 2
}

fail() {
    echo "campaign-variants-lint: $*" >&2
    exit 1
}

production=0
workflow=
if [ "${1:-}" = --production ]; then
    [ "$#" -eq 3 ] || usage
    production=1
    workflow=$2
    shift 2
fi
[ "$#" -eq 1 ] || usage
manifest=$1
[ -f "$manifest" ] && [ -r "$manifest" ] && [ ! -L "$manifest" ] ||
    fail "manifest is not a readable regular file: $manifest"

tmp=$(mktemp -d "${TMPDIR:-/tmp}/cgf-campaign-variants-lint.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

if ! awk '
    BEGIN { FS = "\t" }
    /^[[:space:]]*($|#)/ { next }
    NF != 4 || $1 !~ /^[a-z0-9][a-z0-9-]*$/ ||
        $2 !~ /^[a-z0-9][a-z0-9._-]*$/ ||
        $3 !~ /^ci\/campaigns\/[a-z0-9-]+[.]expected$/ ||
        $4 !~ /^campaign-results-[a-z0-9][a-z0-9._-]*$/ { bad = 1; next }
    seen[$2]++ { bad = 1 }
    { count++; print $1 "\t" $2 "\t" $3 "\t" $4 }
    END { exit bad || count == 0 }
' "$manifest" >"$tmp/rows"; then
    fail "manifest is malformed: $manifest"
fi
LC_ALL=C sort -t "$(printf '\t')" -k2,2 "$tmp/rows" >"$tmp/sorted"
cmp -s "$tmp/rows" "$tmp/sorted" ||
    fail "manifest is not sorted by variant: $manifest"

if [ "$production" -eq 1 ]; then
    [ -f "$workflow" ] && [ -r "$workflow" ] && [ ! -L "$workflow" ] ||
        fail "workflow is not a readable regular file: $workflow"
    {
        printf '%s\n' \
            'chibicc\tchibicc-x86_64\tci/campaigns/chibicc.expected\tcampaign-results-chibicc-x86_64' \
            'curl\tcurl-arm64\tci/campaigns/curl.expected\tcampaign-results-curl-arm64' \
            'curl\tcurl-x86_64\tci/campaigns/curl.expected\tcampaign-results-curl-x86_64' \
            'lua\tlua-arm64\tci/campaigns/lua.expected\tcampaign-results-lua-arm64' \
            'lua\tlua-musl-x86_64\tci/campaigns/lua-musl.expected\tcampaign-results-musl-static-x86_64' \
            'lua\tlua-x86_64\tci/campaigns/lua.expected\tcampaign-results-lua-x86_64' \
            'musl\tmusl-x86_64\tci/campaigns/musl.expected\tcampaign-results-musl-static-x86_64' \
            'qbe\tqbe-arm64\tci/campaigns/qbe-arm64.expected\tcampaign-results-qbe-arm64' \
            'qbe\tqbe-x86_64\tci/campaigns/qbe.expected\tcampaign-results-qbe-x86_64' \
            'sqlite\tsqlite-arm64\tci/campaigns/sqlite.expected\tcampaign-results-sqlite-arm64' \
            'sqlite\tsqlite-x86_64\tci/campaigns/sqlite.expected\tcampaign-results-sqlite-x86_64' \
            'tinycc\ttinycc-x86_64\tci/campaigns/tinycc.expected\tcampaign-results-tinycc-x86_64' \
            'zlib\tzlib-arm64\tci/campaigns/zlib.expected\tcampaign-results-zlib-arm64' \
            'zlib\tzlib-musl-x86_64\tci/campaigns/zlib.expected\tcampaign-results-musl-static-x86_64' \
            'zlib\tzlib-x86_64\tci/campaigns/zlib.expected\tcampaign-results-zlib-x86_64'
    } | sed 's/\\t/'"$(printf '\t')"'/g' >"$tmp/production-rows"
    cmp -s "$tmp/production-rows" "$tmp/rows" ||
        fail "production manifest must contain the exact 15-variant topology"

    awk '
        /uses: actions\/upload-artifact@/ { want = 1; next }
        want && /^[[:space:]]*name:[[:space:]]*/ {
            value = $0
            sub(/^.*name:[[:space:]]*/, "", value)
            if (value ~ /^campaign-results-/) print value
            want = 0
        }
    ' "$workflow" | LC_ALL=C sort >"$tmp/workflow-artifacts"
    printf '%s\n' \
        'campaign-results-${{ matrix.project }}-${{ matrix.arch }}' \
        'campaign-results-chibicc-x86_64' \
        'campaign-results-musl-static-x86_64' \
        'campaign-results-qbe-${{ matrix.arch }}' \
        'campaign-results-tinycc-x86_64' |
        LC_ALL=C sort >"$tmp/production-artifacts"
    cmp -s "$tmp/production-artifacts" "$tmp/workflow-artifacts" ||
        fail "workflow campaign result producers do not match the manifest"
    awk '
        /uses: actions\/download-artifact@/ {
            active = 1
            name = path = ""
            next
        }
        active && /^[[:space:]]*name:[[:space:]]*/ {
            name = $0
            sub(/^.*name:[[:space:]]*/, "", name)
            next
        }
        active && /^[[:space:]]*path:[[:space:]]*/ {
            path = $0
            sub(/^.*path:[[:space:]]*/, "", path)
            if (name ~ /^campaign-results-/) print name "\t" path
            active = 0
        }
    ' "$workflow" | LC_ALL=C sort >"$tmp/workflow-download-pairs"
    cut -f 4 "$tmp/rows" | LC_ALL=C sort -u |
        awk '{ print $0 "\tbuild/nightly-campaign-artifacts/" $0 }' \
        >"$tmp/production-download-pairs"
    cmp -s "$tmp/production-download-pairs" "$tmp/workflow-download-pairs" ||
        fail "workflow campaign result download pairs do not match the manifest"
    if grep -E '^[[:space:]]*merge-multiple:[[:space:]]*true([[:space:]]|$)' \
        "$workflow" >/dev/null; then
        fail "workflow must preserve campaign result artifact namespaces"
    fi
    grep -E '^[[:space:]]*queue:[[:space:]]*max([[:space:]]|$)' \
        "$workflow" >/dev/null ||
        fail "workflow campaign reporter must retain every pending run"
    grep -E '^[[:space:]]*cancel-in-progress:[[:space:]]*false([[:space:]]|$)' \
        "$workflow" >/dev/null ||
        fail "workflow campaign reporter must not cancel an active run"
    grep -F 'CGF_CAMPAIGN_FAILURE_REPORT_ROOT: build/nightly-campaign-evidence/failures' \
        "$workflow" >/dev/null ||
        fail "workflow must retain deterministic campaign failure reports"
    grep -F 'build/nightly-campaign-evidence/results \' "$workflow" >/dev/null ||
        fail "workflow publisher must populate the retained campaign evidence"
    grep -F '>build/nightly-campaign-evidence/publisher.txt' \
        "$workflow" >/dev/null ||
        fail "workflow must retain the campaign publication summary"
    awk '
        /^[[:space:]]*- name:[[:space:]]*/ {
            if (active) {
                print condition "\t" action "\t" artifact "\t" path "\t" missing
            }
            active = ($0 == "      - name: retain aggregate results and deterministic failure reports")
            condition = action = artifact = path = missing = ""
            next
        }
        active && /^[[:space:]]*if:[[:space:]]*/ {
            condition = $0
            sub(/^.*if:[[:space:]]*/, "", condition)
            next
        }
        active && /^[[:space:]]*uses:[[:space:]]*/ {
            action = $0
            sub(/^.*uses:[[:space:]]*/, "", action)
            next
        }
        active && /^[[:space:]]*name:[[:space:]]*/ {
            artifact = $0
            sub(/^.*name:[[:space:]]*/, "", artifact)
            next
        }
        active && /^[[:space:]]*path:[[:space:]]*/ {
            path = $0
            sub(/^.*path:[[:space:]]*/, "", path)
            next
        }
        active && /^[[:space:]]*if-no-files-found:[[:space:]]*/ {
            missing = $0
            sub(/^.*if-no-files-found:[[:space:]]*/, "", missing)
            next
        }
        END {
            if (active) {
                print condition "\t" action "\t" artifact "\t" path "\t" missing
            }
        }
    ' "$workflow" >"$tmp/workflow-ledger-evidence"
    printf 'always()\tactions/upload-artifact@v7\tcampaign-ledger-evidence\tbuild/nightly-campaign-evidence\terror\n' \
        >"$tmp/production-ledger-evidence"
    cmp -s "$tmp/production-ledger-evidence" \
        "$tmp/workflow-ledger-evidence" ||
        fail "workflow must publish the aggregate campaign evidence"

    awk '
        /^  campaign-ladder-s59-native:/ { active = 1; next }
        active && /^  [a-zA-Z0-9_-]+:/ { active = 0 }
        active && /^[[:space:]]*- project: / {
            project = $0
            sub(/^.*- project: /, "", project)
        }
        active && /^[[:space:]]*runner: / {
            runner = $0
            sub(/^.*runner: /, "", runner)
        }
        active && /^[[:space:]]*arch: / {
            arch = $0
            sub(/^.*arch: /, "", arch)
            print project "\t" runner "\t" arch
        }
    ' "$workflow" | LC_ALL=C sort >"$tmp/native-matrix"
    printf '%s\n' \
        'curl\tubuntu-24.04-arm\tarm64' 'curl\tubuntu-latest\tx86_64' \
        'lua\tubuntu-24.04-arm\tarm64' 'lua\tubuntu-latest\tx86_64' \
        'sqlite\tubuntu-24.04-arm\tarm64' 'sqlite\tubuntu-latest\tx86_64' \
        'zlib\tubuntu-24.04-arm\tarm64' 'zlib\tubuntu-latest\tx86_64' |
        sed 's/\\t/'"$(printf '\t')"'/g' | LC_ALL=C sort \
        >"$tmp/production-native-matrix"
    cmp -s "$tmp/production-native-matrix" "$tmp/native-matrix" ||
        fail "workflow native matrix does not match the manifest"

    awk '
        /^  campaign-qbe:/ { active = 1; next }
        active && /^  [a-zA-Z0-9_-]+:/ { active = 0 }
        active && /^[[:space:]]*- runner: / {
            runner = $0
            sub(/^.*- runner: /, "", runner)
        }
        active && /^[[:space:]]*arch: / {
            arch = $0
            sub(/^.*arch: /, "", arch)
            print runner "\t" arch
        }
    ' "$workflow" | LC_ALL=C sort >"$tmp/qbe-matrix"
    printf '%s\n' 'ubuntu-24.04-arm\tarm64' 'ubuntu-latest\tx86_64' |
        sed 's/\\t/'"$(printf '\t')"'/g' | LC_ALL=C sort \
        >"$tmp/production-qbe-matrix"
    cmp -s "$tmp/production-qbe-matrix" "$tmp/qbe-matrix" ||
        fail "workflow QBE matrix must contain exact x86-64 and ARM64 lanes"

    for capture in \
        'scripts/campaign-capture-result.sh musl musl-x86_64' \
        'scripts/campaign-capture-result.sh zlib zlib-musl-x86_64' \
        'scripts/campaign-capture-result.sh lua lua-musl-x86_64' \
        'scripts/campaign-capture-result.sh chibicc chibicc-x86_64' \
        'scripts/campaign-capture-result.sh tinycc tinycc-x86_64' \
        'scripts/campaign-capture-result.sh qbe "qbe-$ARCH" "$expected"' \
        'scripts/campaign-capture-result.sh "$PROJECT" "$PROJECT-$ARCH"'
    do
        grep -F "$capture" "$workflow" >/dev/null ||
            fail "workflow is missing campaign capture route: $capture"
    done
fi

cat "$tmp/rows"
