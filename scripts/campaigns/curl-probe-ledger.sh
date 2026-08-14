#!/bin/sh
set -eu

fail() {
    echo "campaign-curl-probes: $*" >&2
    exit 1
}

[ "$#" -eq 3 ] ||
    fail "usage: $0 HOST_ONLY_PROBES CGFRIED_ONLY_PROBES OUTPUT"
host_only=$1
cgf_only=$2
output=$3
[ -r "$host_only" ] || fail "host-GCC probe input is unreadable: $host_only"
[ -r "$cgf_only" ] || fail "Cgfried probe input is unreadable: $cgf_only"
[ ! -e "$output" ] || fail "output already exists: $output"

unsorted=$output.unsorted.$$
sorted=$output.sorted.$$
trap 'rm -f "$unsorted" "$sorted"' EXIT HUP INT TERM

{
    while IFS= read -r probe; do
        case $probe in
            ac_cv_header_stdatomic_h=*) finding=CAMP-CURL-001 ;;
            curl_cv_def___GNUC__=*)
                finding=CAMP-CURL-002
                probe=curl_cv_def___GNUC__=defined
                ;;
            curl_cv_have_def___GNUC__=*) finding=CAMP-CURL-002 ;;
            *) fail "unclassified host-GCC configure deviation: $probe" ;;
        esac
        printf '%s\thost-gcc-only\t%s\n' "$finding" "$probe"
    done <"$host_only"
    while IFS= read -r probe; do
        case $probe in
            ac_cv_header_stdatomic_h=*) finding=CAMP-CURL-001 ;;
            curl_cv_def___GNUC__=* | curl_cv_have_def___GNUC__=*)
                finding=CAMP-CURL-002
                ;;
            *) fail "unclassified Cgfried configure deviation: $probe" ;;
        esac
        printf '%s\tcgfried-only\t%s\n' "$finding" "$probe"
    done <"$cgf_only"
} >"$unsorted"

LC_ALL=C sort "$unsorted" >"$sorted"
mv "$sorted" "$output"
trap - EXIT HUP INT TERM
rm -f "$unsorted"
