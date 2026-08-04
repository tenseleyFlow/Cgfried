#!/bin/sh
# Sprint 46: -fsafe composition, ELF-note, mixed-link, and ratchet gates.
set -eu
LC_ALL=C
export LC_ALL

CGF=${1:-build/cgfried}
BUILD=${2:-build}
WORK=${CGF_SAFE_MODE_WORK:-$BUILD/safe-mode}
ROOT=tests/memsafe/safe-link

: "${CGF_AS:=0}"
: "${CGF_LD:=0}"
export CGF_AS CGF_LD

case $WORK in
*/safe-mode) ;;
*)
    echo "safe_mode: refusing unsafe work directory: $WORK" >&2
    exit 2
    ;;
esac
rm -rf "$WORK"
mkdir -p "$WORK"

expect_exit()
{
    want=$1
    shift
    set +e
    "$@" >"$WORK/cmd.out" 2>"$WORK/cmd.err"
    got=$?
    set -e
    if [ "$got" -ne "$want" ]; then
        echo "safe_mode: expected exit $want, got $got: $*" >&2
        sed -n '1,16p' "$WORK/cmd.err" >&2
        exit 1
    fi
}

# Composition is order-independent and visible in the dry-run contract.
"$CGF" -### -fsafe -c "$ROOT/main.c" -o "$WORK/dry.o" \
    >"$WORK/dry.out" 2>"$WORK/dry.err"
grep -Fq 'effective -fsafe options: -fcgf-safe -Werror=mem -Werror=uninitialized -ftrivial-auto-var-init=zero' \
    "$WORK/dry.err"
expect_exit 1 "$CGF" -fsafe -fno-cgf-safe -fsyntax-only "$ROOT/main.c"
grep -Fq -- '-fsafe requires -fcgf-safe' "$WORK/cmd.err"
expect_exit 1 "$CGF" -fno-cgf-safe -fsafe -fsyntax-only "$ROOT/main.c"
grep -Fq -- '-fsafe requires -fcgf-safe' "$WORK/cmd.err"
expect_exit 1 "$CGF" -fsafe -w -fsyntax-only "$ROOT/main.c"
grep -Fq -- '-fsafe requires memory diagnostics' "$WORK/cmd.err"
expect_exit 1 "$CGF" -fsafe -Wno-error=uninitialized -fsyntax-only \
    tests/warn/flow/uninitialized/definite-return.c
grep -Fq "error: 'x' is used uninitialized" "$WORK/cmd.err"

"$CGF" -fsafe -c "$ROOT/main.c" -o "$WORK/main.safe.o"
"$CGF" -fsafe -c "$ROOT/helper.c" -o "$WORK/helper.safe.o"
"$CGF" -c "$ROOT/helper.c" -o "$WORK/helper.unsafe.o"

readelf -S "$WORK/main.safe.o" | grep -Fq '.note.cgf.safe'
readelf -S "$WORK/helper.safe.o" | grep -Fq '.note.cgf.safe'
if readelf -S "$WORK/helper.unsafe.o" | grep -Fq '.note.cgf.safe'; then
    echo 'safe_mode: unsafe object unexpectedly carries the safe note' >&2
    exit 1
fi

"$CGF" -fsafe "$WORK/main.safe.o" "$WORK/helper.safe.o" \
    -o "$WORK/all-safe"
"$WORK/all-safe"

expect_exit 2 "$CGF" -fsafe "$WORK/main.safe.o" \
    "$WORK/helper.unsafe.o" -o "$WORK/mixed-rejected"
grep -Fq '.note.cgf.safe' "$WORK/cmd.err"

expect_exit 2 "$CGF" -fsafe \
    -fsafe-allow-unsafe="$WORK/not-the-helper.o" \
    "$WORK/main.safe.o" "$WORK/helper.unsafe.o" \
    -o "$WORK/wrong-allowance"
grep -Fq '.note.cgf.safe' "$WORK/cmd.err"

"$CGF" -fsafe -fsafe-allow-unsafe="$WORK/helper.unsafe.o" \
    "$WORK/main.safe.o" "$WORK/helper.unsafe.o" -o "$WORK/mixed-allowed"
"$WORK/mixed-allowed"

# Raw linker options are not attestable: each can cause ld to open files
# hidden from the driver's object-note validation.
expect_exit 2 "$CGF" -fsafe "$WORK/main.safe.o" \
    -Xlinker "$WORK/helper.unsafe.o" -o "$WORK/xlinker-object"
grep -Fq 'rejects user-supplied linker option' "$WORK/cmd.err"
expect_exit 2 "$CGF" -fsafe "$WORK/main.safe.o" \
    -Wl,"$WORK/helper.unsafe.o" -o "$WORK/wl-object"
grep -Fq 'rejects user-supplied linker option' "$WORK/cmd.err"
printf '%s\n' "$WORK/helper.unsafe.o" >"$WORK/ld.rsp"
expect_exit 2 "$CGF" -fsafe "$WORK/main.safe.o" \
    -Xlinker "@$WORK/ld.rsp" -o "$WORK/xlinker-response"
grep -Fq 'rejects user-supplied linker option' "$WORK/cmd.err"
printf '%s\n' 'SECTIONS { .text : { *(.text) } }' >"$WORK/link.ld"
expect_exit 2 "$CGF" -fsafe "$WORK/main.safe.o" \
    -Wl,-T,"$WORK/link.ld" -o "$WORK/linker-script"
grep -Fq 'rejects user-supplied linker option' "$WORK/cmd.err"
printf '%s\n' "$WORK/main.safe.o $WORK/helper.unsafe.o" >"$WORK/cgf.rsp"
expect_exit 2 "$CGF" -fsafe "@$WORK/cgf.rsp" -o "$WORK/compiler-response"
grep -Fq '.note.cgf.safe' "$WORK/cmd.err"

sh scripts/check_safe_mode_doc.sh

# Prove the documentation gate rejects malformed contract data, including the
# two columns that keep the claim honest: guarantee limits and alternatives.
awk '
    $0 == "## Guarantees" { in_table = 1; table_rows = 0 }
    in_table && /^## / && $0 != "## Guarantees" { in_table = 0 }
    in_table && /^\|/ {
        table_rows++
        if (table_rows == 3)
            sub(/\|[^|]*\|[[:space:]]*$/, "| |")
    }
    { print }
' doc/safe-mode.md >"$WORK/empty-limit.md"
expect_exit 1 sh scripts/check_safe_mode_doc.sh "$WORK/empty-limit.md"
grep -Fq 'Limit cell is empty' "$WORK/cmd.err"

awk '
    $0 == "## Rejected constructs" { in_table = 1; table_rows = 0 }
    in_table && /^## / && $0 != "## Rejected constructs" { in_table = 0 }
    in_table && /^\|/ {
        table_rows++
        if (table_rows == 3)
            sub(/\|[^|]*\|[[:space:]]*$/, "| |")
    }
    { print }
' doc/safe-mode.md >"$WORK/empty-alternative.md"
expect_exit 1 sh scripts/check_safe_mode_doc.sh "$WORK/empty-alternative.md"
grep -Fq 'Alternative cell is empty' "$WORK/cmd.err"

awk '
    $0 == "## Guarantees" { in_table = 1; table_rows = 0 }
    in_table && /^## / && $0 != "## Guarantees" { in_table = 0 }
    in_table && /^\|/ {
        table_rows++
        if (table_rows == 3)
            sub(/\|[[:space:]]*$/, "| extra |")
    }
    { print }
' doc/safe-mode.md >"$WORK/extra-cell.md"
expect_exit 1 sh scripts/check_safe_mode_doc.sh "$WORK/extra-cell.md"
grep -Fq 'expected exactly 3 cells' "$WORK/cmd.err"

sh scripts/check_safe_allowlist.sh

# Prove the allowlist rule itself accepts shrinkage and rejects growth.
printf '%s\n' '# empty active set: shrinkage is legal' >"$WORK/shrunk.txt"
sh scripts/check_safe_allowlist.sh "$WORK/shrunk.txt" \
    ci/safe-mode-allowlist.baseline.txt >"$WORK/shrink.out"
cp ci/safe-mode-allowlist.txt "$WORK/grown.txt"
printf '%s\n' 'src/fake.c:unsafe_boundary  owner  regression growth probe' \
    >>"$WORK/grown.txt"
expect_exit 1 sh scripts/check_safe_allowlist.sh "$WORK/grown.txt" \
    ci/safe-mode-allowlist.baseline.txt
grep -Fq 'safe allowlist grew with unreviewed key' "$WORK/cmd.err"

echo 'safe_mode: composition, uninitialized promotion, ELF notes, raw-link closure, mixed-link policy, docs, and shrink-only ratchet green'
