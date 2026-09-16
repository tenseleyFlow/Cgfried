#!/bin/sh
set -eu

LC_ALL=C
export LC_ALL

fail() {
    echo "campaign-sole-c: $*" >&2
    exit 1
}

sha256_file() {
    sha256sum "$1" | awk '{print $1}'
}

canonical_file() {
    path=$1
    case $path in
        /*) ;;
        *) path=$(pwd -P)/$path ;;
    esac
    directory=$(dirname "$path")
    basename=$(basename "$path")
    [ -d "$directory" ] || fail "path has no directory: $path"
    directory=$(CDPATH='' cd "$directory" && pwd -P)
    path=$directory/$basename
    [ -f "$path" ] || fail "path is not a regular file: $path"
    printf '%s\n' "$path"
}

read_one() {
    key=$1
    receipt=$2
    awk -v key="$key" '
        index($0, key "=") == 1 {
            count++
            value = substr($0, length(key) + 2)
        }
        END {
            if (count != 1) exit 1
            print value
        }
    ' "$receipt"
}

init_receipts() {
    [ "$#" -eq 2 ] || fail "usage: $0 init RECEIPTS COMPILER"
    receipts=$1
    compiler=$(canonical_file "$2")
    [ -x "$compiler" ] || fail "compiler is not executable: $compiler"
    [ ! -e "$receipts" ] || fail "receipt root already exists: $receipts"
    mkdir -p "$receipts/invocations"
    printf '%s\n' "$compiler" >"$receipts/compiler.path"
    sha256_file "$compiler" >"$receipts/compiler.sha256"
}

record_invocation() {
    [ "$#" -ge 2 ] || fail "usage: $0 cc RECEIPTS COMPILER [ARG ...]"
    receipts=$1
    compiler=$(canonical_file "$2")
    shift 2
    [ -d "$receipts/invocations" ] ||
        fail "receipt root has not been initialized: $receipts"
    recorded_compiler=$(cat "$receipts/compiler.path")
    recorded_digest=$(cat "$receipts/compiler.sha256")
    [ "$compiler" = "$recorded_compiler" ] ||
        fail "compiler path changed: expected $recorded_compiler, got $compiler"
    compiler_digest=$(sha256_file "$compiler")
    [ "$compiler_digest" = "$recorded_digest" ] ||
        fail "compiler digest changed before invocation"

    compile_only=0
    preprocess_only=0
    output=
    next_output=0
    source_count=0
    source=
    for argument in "$@"; do
        case $argument in
            *"	"* | *"
"*) fail "compiler arguments must not contain tabs or newlines" ;;
        esac
        if [ "$next_output" -eq 1 ]; then
            output=$argument
            next_output=0
            continue
        fi
        case $argument in
            -o) next_output=1 ;;
            -o?*) output=${argument#-o} ;;
            -c) compile_only=1 ;;
            -E) preprocess_only=1 ;;
            *.c)
                source_count=$((source_count + 1))
                source=$argument
                ;;
        esac
    done
    [ "$next_output" -eq 0 ] || fail "-o has no output path"

    if "$compiler" "$@"; then
        status=0
    else
        status=$?
    fi
    [ "$status" -eq 0 ] || exit "$status"
    [ "$preprocess_only" -eq 0 ] || exit 0

    has_link_input=0
    for argument in "$@"; do
        case $argument in
            *.o | *.a)
                [ -f "$argument" ] && has_link_input=1
                ;;
        esac
    done
    if [ "$source_count" -eq 0 ] && [ "$has_link_input" -eq 0 ]; then
        exit 0
    fi
    [ "$source_count" -le 1 ] ||
        fail "one compiler invocation translated multiple C sources"
    [ -n "$output" ] || fail "tracked compiler invocation omitted -o"
    output=$(canonical_file "$output")

    if [ "$source_count" -eq 1 ]; then
        source=$(canonical_file "$source")
        source_digest=$(sha256_file "$source")
        if [ "$compile_only" -eq 1 ]; then
            kind=object
        else
            kind=compile-link
        fi
    else
        source=-
        source_digest=-
        kind=link
    fi

    receipt_dir=$receipts/invocations/$$
    mkdir "$receipt_dir" || fail "receipt process identifier was reused: $$"
    receipt=$receipt_dir/receipt.txt
    {
        echo '# cgf-sole-c-receipt-v1'
        printf 'compiler=%s\n' "$compiler"
        printf 'compiler_sha256=%s\n' "$compiler_digest"
        printf 'cwd=%s\n' "$(pwd -P)"
        printf 'kind=%s\n' "$kind"
        printf 'source=%s\n' "$source"
        printf 'source_sha256=%s\n' "$source_digest"
        printf 'output=%s\n' "$output"
        printf 'output_sha256=%s\n' "$(sha256_file "$output")"
        argument_number=0
        for argument in "$@"; do
            argument_number=$((argument_number + 1))
            printf 'arg.%s=%s\n' "$argument_number" "$argument"
            case $argument in
                *.c | *.o | *.a)
                    if [ -f "$argument" ]; then
                        printf 'input=%s\n' "$(canonical_file "$argument")"
                    fi
                    ;;
            esac
        done
        printf 'arg_count=%s\n' "$argument_number"
    } >"$receipt"
}

verify_receipts() {
    [ "$#" -eq 4 ] ||
        fail "usage: $0 verify RECEIPTS COMPILER MANIFEST REPORT"
    receipts=$1
    compiler=$(canonical_file "$2")
    manifest=$3
    report=$4
    [ -d "$receipts/invocations" ] || fail "missing receipt root: $receipts"
    [ -f "$manifest" ] || fail "missing closure manifest: $manifest"
    [ "$(sed -n '1p' "$manifest")" = '# cgf-sole-c-closure-v1' ] ||
        fail "invalid closure manifest schema: $manifest"

    recorded_compiler=$(cat "$receipts/compiler.path")
    recorded_digest=$(cat "$receipts/compiler.sha256")
    compiler_digest=$(sha256_file "$compiler")
    [ "$compiler" = "$recorded_compiler" ] ||
        fail "verification compiler path differs from initialized compiler"
    [ "$compiler_digest" = "$recorded_digest" ] ||
        fail "verification compiler digest differs from initialized compiler"

    tmp=$(mktemp -d "${TMPDIR:-/tmp}/cgf-sole-c-verify.XXXXXX")
    trap 'rm -rf "$tmp"' EXIT HUP INT TERM
    if ! awk -F '\t' -v identities="$tmp/expected-identities" \
        -v inputs="$tmp/expected-link-inputs" \
        -v archives="$tmp/expected-archives" '
        NR == 1 { next }
        /^#/ || NF == 0 { next }
        $1 == "object" || $1 == "compile-link" {
            if (NF != 3 || $2 !~ /^\// || $3 !~ /^\//) bad = 1
            print $1 "\t" $2 "\t" $3 > identities
            if ($1 == "compile-link") compile_links[$3] = 1
            next
        }
        $1 == "archive" {
            if (NF != 4 || $2 !~ /^\// || $4 !~ /^\// ||
                $3 == "" || $3 ~ /\//) bad = 1
            print $2 "\t" $3 "\t" $4 > archives
            next
        }
        $1 == "link-input" {
            if (NF != 3 || $2 !~ /^\// || $3 !~ /^\//) bad = 1
            print $2 "\t" $3 > inputs
            products[$2] = 1
            next
        }
        { bad = 1 }
        END {
            for (product in products)
                if (!compile_links[product])
                    print "link\t-\t" product > identities
            exit bad
        }
    ' "$manifest"; then
        fail "malformed closure manifest: $manifest"
    fi
    : >"$tmp/actual-identities"
    : >"$tmp/actual-link-inputs"

    for receipt in "$receipts"/invocations/*/receipt.txt; do
        [ -f "$receipt" ] || fail "no compiler receipts were recorded"
        [ "$(sed -n '1p' "$receipt")" = '# cgf-sole-c-receipt-v1' ] ||
            fail "invalid receipt schema: $receipt"
        receipt_compiler=$(read_one compiler "$receipt") ||
            fail "receipt has invalid compiler field: $receipt"
        receipt_compiler_digest=$(read_one compiler_sha256 "$receipt") ||
            fail "receipt has invalid compiler digest field: $receipt"
        kind=$(read_one kind "$receipt") ||
            fail "receipt has invalid kind field: $receipt"
        source=$(read_one source "$receipt") ||
            fail "receipt has invalid source field: $receipt"
        source_digest=$(read_one source_sha256 "$receipt") ||
            fail "receipt has invalid source digest field: $receipt"
        output=$(read_one output "$receipt") ||
            fail "receipt has invalid output field: $receipt"
        output_digest=$(read_one output_sha256 "$receipt") ||
            fail "receipt has invalid output digest field: $receipt"
        [ "$receipt_compiler" = "$compiler" ] ||
            fail "receipt names another compiler: $receipt"
        [ "$receipt_compiler_digest" = "$compiler_digest" ] ||
            fail "receipt has another compiler digest: $receipt"
        [ -f "$output" ] || fail "receipt output disappeared: $output"
        [ "$(sha256_file "$output")" = "$output_digest" ] ||
            fail "receipt output digest changed: $output"
        case $kind in
            object | compile-link)
                [ -f "$source" ] || fail "receipt source disappeared: $source"
                [ "$(sha256_file "$source")" = "$source_digest" ] ||
                    fail "receipt source digest changed: $source"
                ;;
            link)
                [ "$source" = - ] && [ "$source_digest" = - ] ||
                    fail "link receipt unexpectedly names a C source: $receipt"
                ;;
            *) fail "receipt has invalid kind: $receipt" ;;
        esac
        printf '%s\t%s\t%s\n' "$kind" "$source" "$output" \
            >>"$tmp/actual-identities"
        if [ "$kind" = link ] || [ "$kind" = compile-link ]; then
            awk -v product="$output" '
                index($0, "input=") == 1 {
                    print product "\t" substr($0, 7)
                }
            ' "$receipt" >>"$tmp/actual-link-inputs"
        fi
    done

    LC_ALL=C sort "$tmp/expected-identities" >"$tmp/expected-identities.sorted"
    LC_ALL=C sort "$tmp/actual-identities" >"$tmp/actual-identities.sorted"
    if ! cmp -s "$tmp/expected-identities.sorted" "$tmp/actual-identities.sorted"; then
        diff -u "$tmp/expected-identities.sorted" "$tmp/actual-identities.sorted" >&2 || true
        fail "recorded translation/link closure differs from the manifest"
    fi
    LC_ALL=C sort "$tmp/expected-link-inputs" >"$tmp/expected-link-inputs.sorted"
    LC_ALL=C sort "$tmp/actual-link-inputs" >"$tmp/actual-link-inputs.sorted"
    if ! cmp -s "$tmp/expected-link-inputs.sorted" "$tmp/actual-link-inputs.sorted"; then
        diff -u "$tmp/expected-link-inputs.sorted" "$tmp/actual-link-inputs.sorted" >&2 || true
        fail "recorded explicit link inputs differ from the manifest"
    fi

    while IFS="$(printf '\t')" read -r product input; do
        [ -n "$product" ] || continue
        case $input in
            *.o)
                awk -F '\t' -v input="$input" \
                    '$1 == "object" && $3 == input { found = 1 }
                     END { exit !found }' "$tmp/expected-identities" ||
                    fail "link input is not a certified object: $input"
                ;;
            *.a)
                awk -F '\t' -v input="$input" \
                    '$1 == input { found = 1 } END { exit !found }' \
                    "$tmp/expected-archives" ||
                    fail "link input is not a certified archive: $input"
                ;;
            *.c)
                awk -F '\t' -v product="$product" -v input="$input" \
                    '$1 == "compile-link" && $2 == input && $3 == product { found = 1 }
                     END { exit !found }' "$tmp/expected-identities" ||
                    fail "compile-link source is not certified: $input"
                ;;
            *) fail "unsupported project-local link input: $input" ;;
        esac
    done <"$tmp/expected-link-inputs"

    awk -F '\t' '{ if (!seen[$1]++) print $1 }' "$tmp/expected-archives" \
        >"$tmp/archive-paths"
    while IFS= read -r archive; do
        [ -n "$archive" ] || continue
        [ -f "$archive" ] || fail "certified archive disappeared: $archive"
        awk -F '\t' -v archive="$archive" '$1 == archive { print $2 }' \
            "$tmp/expected-archives" >"$tmp/expected-members"
        ar t "$archive" | awk '$0 !~ /^__.SYMDEF( SORTED)?$/' \
            >"$tmp/actual-members"
        cmp -s "$tmp/expected-members" "$tmp/actual-members" ||
            fail "archive member inventory differs from the manifest: $archive"
        while IFS="$(printf '\t')" read -r row_archive member object; do
            [ "$row_archive" = "$archive" ] || continue
            [ -f "$object" ] || fail "certified object disappeared: $object"
            ar p "$archive" "$member" >"$tmp/archive-member"
            cmp -s "$object" "$tmp/archive-member" ||
                fail "archive member differs from certified object: $archive($member)"
        done <"$tmp/expected-archives"
    done <"$tmp/archive-paths"

    project_objects=$(awk -F '\t' '$1 == "object" { count++ } END { print count + 0 }' \
        "$tmp/expected-identities")
    translations=$(awk -F '\t' \
        '$1 == "object" || $1 == "compile-link" { count++ }
         END { print count + 0 }' "$tmp/expected-identities")
    archive_members=$(wc -l <"$tmp/expected-archives" | tr -d ' ')
    linked_products=$(awk -F '\t' \
        '($1 == "link" || $1 == "compile-link") && !seen[$3]++ { count++ }
         END { print count + 0 }' "$tmp/expected-identities")
    {
        echo '# cgf-sole-c-report-v1'
        printf 'compiler=%s\n' "$compiler"
        printf 'compiler_sha256=%s\n' "$compiler_digest"
        printf 'project_objects=%s\n' "$project_objects"
        printf 'archive_members=%s\n' "$archive_members"
        printf 'linked_products=%s\n' "$linked_products"
        printf 'translations=%s\n' "$translations"
        echo 'status=PASS'
    } >"$report"
    printf 'campaign-sole-c: PASS project-objects=%s archive-members=%s linked-products=%s translations=%s\n' \
        "$project_objects" "$archive_members" "$linked_products" "$translations"
}

[ "$#" -ge 1 ] || fail "usage: $0 init|cc|verify ..."
mode=$1
shift
case $mode in
    init) init_receipts "$@" ;;
    cc) record_invocation "$@" ;;
    verify) verify_receipts "$@" ;;
    *) fail "unknown mode: $mode" ;;
esac
