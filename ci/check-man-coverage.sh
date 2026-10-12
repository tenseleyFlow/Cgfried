#!/bin/sh
# Keep the handwritten manual, the table-driven parser, the warning registry,
# and the executable help surface mutually complete.
set -eu

LC_ALL=C
export LC_ALL

root=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}
compiler=${2:-$root/build/cgfried}
manpage=${CGF_MAN_FILE:-$root/doc/cgfried.1}
args=$root/src/driver/args.c
warnings=$root/src/warn/warnings.def
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cgf-man-coverage.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

fail()
{
    echo "check-man-coverage: $*" >&2
    exit 1
}

[ -f "$manpage" ] || fail "missing manual: $manpage"
[ -f "$args" ] || fail "missing driver flag table"
[ -f "$warnings" ] || fail "missing warning registry"
[ -x "$compiler" ] || fail "compiler is unavailable at $compiler"

# Make every troff minus/hyphen searchable as its command-line byte.
sed 's/\\-/-/g' "$manpage" >"$tmp/man.txt"

sed -n '/static const FlagSpec args_flag_table\[\] = {/,/^};/ {
    s/^[	 ]*{"\([^"]*\)".*/\1/p
}' "$args" >"$tmp/driver-options"
sed -n 's/^W([^,]*, "\([^"]*\)".*/-W\1/p' "$warnings" \
    >"$tmp/warning-options"
{
    printf '%s\n' -Wall -Wextra -Wunused -Wformat -Wimplicit
    printf '%s\n' -Wpedantic -Wcgf-ext -Wmem -Wmem-strict
} >"$tmp/warning-groups"
cat "$tmp/driver-options" "$tmp/warning-options" "$tmp/warning-groups" |
    sort -u >"$tmp/required"

while IFS= read -r option; do
    grep -Fq -- "$option" "$tmp/man.txt" ||
        fail "man page omits driver option $option"
done <"$tmp/required"

"$compiler" --help >"$tmp/help" 2>"$tmp/help.err" ||
    fail "--help did not exit successfully"
"$compiler" --help=warnings >"$tmp/warnings-help" \
    2>"$tmp/warnings-help.err" ||
    fail "--help=warnings did not exit successfully"
[ ! -s "$tmp/help.err" ] || fail "--help wrote to stderr"
[ ! -s "$tmp/warnings-help.err" ] || fail "--help=warnings wrote to stderr"
cat "$tmp/help" "$tmp/warnings-help" >"$tmp/all-help"

# The table-to-man half above is the completeness proof. This second pass
# prevents the manual from inventing options that users cannot discover in
# executable help. Joined example values normalize to their table prefix.
tr '	 ' '\n\n' <"$tmp/man.txt" |
    sed -n '/^-/ { s/^["(]*//; s/[",.;:)]*$//; p; }' |
    sort -u >"$tmp/man-option-tokens"
while IFS= read -r option; do
    lookup=$option
    case $lookup in
        --target=*) lookup=--target= ;;
        --sysroot=*) lookup=--sysroot= ;;
        -std=*) lookup=-std= ;;
        -print-prog-name=*) lookup=-print-prog-name= ;;
        -print-file-name=*) lookup=-print-file-name= ;;
        -fmax-errors=*) lookup=-fmax-errors= ;;
        -ferror-limit=*) lookup=-ferror-limit= ;;
        -fsafe-allow-unsafe=*) lookup=-fsafe-allow-unsafe= ;;
        -fdiagnostics-apply-fixits=*) lookup=-fdiagnostics-apply-fixits= ;;
        -ftrivial-auto-var-init=*) lookup=-ftrivial-auto-var-init= ;;
        -ffp-contract=*) lookup=-ffp-contract= ;;
        -Werror=*) lookup=-Werror= ;;
        -Wno-*) lookup=-W ;;
    esac
    grep -Fq -- "$lookup" "$tmp/all-help" ||
        fail "manual option $option is absent from executable help"
done <"$tmp/man-option-tokens"

if command -v mandoc >/dev/null 2>&1; then
    mandoc -T lint "$manpage" >/dev/null
fi
if command -v groff >/dev/null 2>&1; then
    groff -man -z -ww "$manpage" >/dev/null
fi

driver_count=$(wc -l <"$tmp/driver-options" | tr -d '[:space:]')
warning_count=$(wc -l <"$tmp/warning-options" | tr -d '[:space:]')
echo "check-man-coverage: PASS ($driver_count driver options, $warning_count warnings)"
