#!/bin/sh
# Sprint 36 blocking x86-64 ISA ceiling: adversarial checker self-tests plus
# every permanent corpus source at all six optimization levels.
set -eu
LC_ALL=C
export LC_ALL

cc=${1:-build/cgfried}
checker=${2:-ci/check_isa.sh}
host_cc=${CC:-cc}
objdump_tool=${OBJDUMP:-objdump}
work=$(mktemp -d "${TMPDIR:-/tmp}/cgf-s36-isa.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

command -v "$objdump_tool" >/dev/null 2>&1 || {
    echo "s36_isa_driver: GNU objdump is required" >&2
    exit 1
}

assemble()
{
    asm_source=$1
    asm_object=$2
    "$host_cc" -c "$asm_source" -o "$asm_object"
}

expect_reject()
{
    reject_source=$1
    reject_object=$2
    reject_text=$3
    reject_name=$4

    if sh "$checker" "$reject_source" "$reject_object" \
        >"$work/$reject_name.out" 2>"$work/$reject_name.err"; then
        echo "s36_isa_driver: checker accepted $reject_name" >&2
        exit 1
    fi
    grep -Fq "$reject_text" "$work/$reject_name.err"
}

assemble tests/isa/legal_sse2.s "$work/legal_sse2.o"
sh "$checker" tests/isa/unlicensed.c "$work/legal_sse2.o"

assemble tests/isa/haddps.s "$work/haddps.o"
expect_reject tests/isa/unlicensed.c "$work/haddps.o" \
    "outside the closed x86-64/SSE2 baseline" haddps

assemble tests/isa/prefixed_haddps.s "$work/prefixed_haddps.o"
"$objdump_tool" -d --no-show-raw-insn "$work/prefixed_haddps.o" \
    >"$work/prefixed_haddps.dump"
grep -Eq 'cs[[:space:]]+haddps' "$work/prefixed_haddps.dump"
expect_reject tests/isa/unlicensed.c "$work/prefixed_haddps.o" \
    "outside the closed x86-64/SSE2 baseline" prefixed_haddps

assemble tests/isa/monitor_mwait.s "$work/monitor_mwait.o"
expect_reject tests/isa/unlicensed.c "$work/monitor_mwait.o" \
    "monitor" monitor_mwait
grep -Fq "mwait" "$work/monitor_mwait.err"

assemble tests/isa/cet_rd_scalar.s "$work/cet_rd_scalar.o"
expect_reject tests/isa/unlicensed.c "$work/cet_rd_scalar.o" \
    "endbr64" cet_rd_scalar
grep -Fq "rdpid" "$work/cet_rd_scalar.err"
grep -Fq "rdtscp" "$work/cet_rd_scalar.err"

assemble tests/isa/unknown_post_sse2.s "$work/unknown_post_sse2.o"
expect_reject tests/isa/unlicensed.c "$work/unknown_post_sse2.o" \
    "outside the closed x86-64/SSE2 baseline" unknown_post_sse2
for mnemonic in cldemote wbnoinvd clac stac prefetchwt1 invpcid; do
    grep -Fq "$mnemonic" "$work/unknown_post_sse2.err"
done

assemble tests/isa/pextrw_register.s "$work/pextrw_register.o"
sh "$checker" tests/isa/unlicensed.c "$work/pextrw_register.o"
assemble tests/isa/pextrw_memory.s "$work/pextrw_memory.o"
expect_reject tests/isa/unlicensed.c "$work/pextrw_memory.o" \
    "SSE4.1 memory-destination instruction exceeds SSE2 ceiling" \
    pextrw_memory

assemble tests/isa/x87_allowed.s "$work/x87_allowed.o"
# Pin GNU objdump canonical spellings, rather than the assembler input aliases.
"$objdump_tool" -d --no-show-raw-insn "$work/x87_allowed.o" \
    >"$work/x87_allowed.dump"
grep -Eq '(^|[[:space:]])fildll([[:space:]]|$)' "$work/x87_allowed.dump"
grep -Eq '(^|[[:space:]])fistpll([[:space:]]|$)' "$work/x87_allowed.dump"
grep -Eq '(^|[[:space:]])fld([[:space:]]|$)' "$work/x87_allowed.dump"
grep -Eq '(^|[[:space:]])fld1([[:space:]]|$)' "$work/x87_allowed.dump"
grep -Eq '(^|[[:space:]])fldz([[:space:]]|$)' "$work/x87_allowed.dump"
sh "$checker" tests/isa/licensed.c "$work/x87_allowed.o"
expect_reject tests/isa/unlicensed.c "$work/x87_allowed.o" \
    "lacks a source-text long double license" x87_unlicensed

assemble tests/isa/fisttp.s "$work/fisttp.o"
expect_reject tests/isa/licensed.c "$work/fisttp.o" \
    "fisttp is forbidden even for long double" fisttp

assemble tests/isa/labels_comments.s "$work/labels_comments.o"
sh "$checker" tests/isa/unlicensed.c "$work/labels_comments.o"

corpus_list=$work/corpus.list
find tests/corpus -type f -name '*.c' -print | sort >"$corpus_list"
corpus_count=$(wc -l <"$corpus_list" | tr -d ' ')
# Keep this explicit count as a corpus-drift ratchet: adding or removing a
# source must deliberately repin the ISA matrix after the new inventory passes
# all six optimization levels. The VLA record-copy and va_arg fixtures are the
# 109th and 110th permanent execution cases.
expected_corpus_count=110
if [ "$corpus_count" -ne "$expected_corpus_count" ]; then
    echo "s36_isa_driver: expected $expected_corpus_count corpus C files, found $corpus_count" >&2
    exit 1
fi

checks=0
index=0
while IFS= read -r source_path; do
    index=$((index + 1))
    for level in O0 O1 O2 O3 Os Ofast; do
        object_path=$work/corpus.$index.$level.o
        CGF_AS=0 "$cc" "-$level" -c "$source_path" -o "$object_path"
        sh "$checker" "$source_path" "$object_path"
        checks=$((checks + 1))
    done
done <"$corpus_list"

expected_checks=$((expected_corpus_count * 6))
if [ "$checks" -ne "$expected_checks" ]; then
    echo "s36_isa_driver: expected exactly $expected_checks object checks, ran $checks" >&2
    exit 1
fi

echo "s36_isa_driver: ISA self-tests green; exactly $checks corpus objects satisfy the closed x86-64/SSE2 baseline"
