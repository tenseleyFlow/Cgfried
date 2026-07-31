#!/bin/sh
# Sprint 27 (DoD 6): the CGF_LD=1 -static corpus lane. Twelve corpus
# fixtures link twice — system ld and bundled afs-ld — and must behave
# IDENTICALLY (stdout + exit code; never a byte compare, layout
# legitimately differs). readelf structural checks per afs-ld product:
# ET_EXEC, no PT_INTERP (static), entry resolving to _start,
# .eh_frame_hdr present, no writable+executable segment. Plus the
# LD-ELF-001 hint check: a dynamic CGF_LD=1 link touching a data import
# must fail with afs-ld's COPY-reloc diagnostic AND the driver's
# retry-with--static hint. HARNESS_SKIP (loudly) when afs-ld is unbuilt.
set -u
LC_ALL=C
export LC_ALL

CGF=${1:-build/cgfried}
WORK=${CGF_AFSLD_WORK:-build/afsld-lane}

if [ ! -x afs-ld/target/release/afs-ld ]; then
    echo "HARNESS_SKIP suite=afsld test=all count=1" \
        "reason=\"afs-ld not built (make tools); lane not exercised\""
    exit 0
fi
if ! command -v readelf >/dev/null 2>&1; then
    echo "HARNESS_SKIP suite=afsld test=all count=1" \
        "reason=\"no readelf on this host\""
    exit 0
fi

FIXTURES="int/hello int/fib_rec int/queens8 int/duff int/promo_traps
int/struct_ret int/varargs_sum int/sieve int/collatz int/switch_dense
fp/varargs_mixed fp/long_double_basic"

rm -rf "$WORK"
mkdir -p "$WORK"
fails=0
n=0

for name in $FIXTURES; do
    f="tests/corpus/x86_64/$name.c"
    base=$(echo "$name" | tr / _)
    n=$((n + 1))
    if [ ! -f "$f" ]; then
        echo "afsld lane FAIL: missing fixture $f" >&2
        fails=$((fails + 1))
        continue
    fi
    # System-ld static product (the behavioral oracle).
    if ! "$CGF" -static "$f" -o "$WORK/$base.sys" 2> "$WORK/$base.sys.err"; then
        echo "afsld lane FAIL: system-ld static link of $name" >&2
        cat "$WORK/$base.sys.err" >&2
        fails=$((fails + 1))
        continue
    fi
    # afs-ld product.
    if ! CGF_LD=1 "$CGF" -static "$f" -o "$WORK/$base.afs" \
        2> "$WORK/$base.afs.err"; then
        echo "afsld lane FAIL: afs-ld static link of $name" >&2
        cat "$WORK/$base.afs.err" >&2
        fails=$((fails + 1))
        continue
    fi
    "$WORK/$base.sys" > "$WORK/$base.sys.out" 2>&1
    sys_rc=$?
    "$WORK/$base.afs" > "$WORK/$base.afs.out" 2>&1
    afs_rc=$?
    if [ "$sys_rc" -ne "$afs_rc" ] ||
        ! cmp -s "$WORK/$base.sys.out" "$WORK/$base.afs.out"; then
        echo "afsld lane FAIL: $name behavior differs" \
            "(sys=$sys_rc afs=$afs_rc)" >&2
        fails=$((fails + 1))
        continue
    fi
    # Structural checks on the afs-ld product.
    re=$(readelf -hl "$WORK/$base.afs" 2>/dev/null)
    echo "$re" | grep -q 'Type:[[:space:]]*EXEC' || {
        echo "afsld lane FAIL: $name not ET_EXEC" >&2
        fails=$((fails + 1))
        continue
    }
    if echo "$re" | grep -q 'INTERP'; then
        echo "afsld lane FAIL: $name static product has PT_INTERP" >&2
        fails=$((fails + 1))
        continue
    fi
    if echo "$re" | grep -E 'LOAD' | grep -q 'RWE'; then
        echo "afsld lane FAIL: $name has a writable+executable LOAD" >&2
        fails=$((fails + 1))
        continue
    fi
    readelf -S "$WORK/$base.afs" 2>/dev/null | grep -q '\.eh_frame_hdr' || {
        echo "afsld lane FAIL: $name missing .eh_frame_hdr" >&2
        fails=$((fails + 1))
        continue
    }
    # afs-ld's ELF executables carry no .symtab, so "entry == _start"
    # cannot be checked by name; the structural equivalent is that the
    # entry point lands inside an R-E LOAD segment (crt1.o's _start is
    # the first text contribution).
    entry=$(echo "$re" | grep 'Entry point' | grep -o '0x[0-9a-f]*')
    in_exec=no
    for l in $(readelf -lW "$WORK/$base.afs" 2>/dev/null |
        awk '$1 == "LOAD" { print $3 ":" $6 ":" $7 $8 }'); do
        va=${l%%:*}
        rest=${l#*:}
        sz=${rest%%:*}
        fl=${rest#*:}
        case "$fl" in
        *E*)
            if [ "$((entry))" -ge "$((va))" ] &&
                [ "$((entry))" -lt "$((va + sz))" ]; then
                in_exec=yes
            fi
            ;;
        esac
    done
    if [ "$in_exec" != "yes" ]; then
        echo "afsld lane FAIL: $name entry $entry outside any R-E LOAD" >&2
        fails=$((fails + 1))
        continue
    fi
done

# LD-ELF-001: dynamic + data import under CGF_LD=1 -> COPY-reloc error
# from afs-ld AND the driver's experimental-lane hint.
cat > "$WORK/copyhint.c" << 'EOF'
extern int getchar(void);
extern void *stdin;
void *grab(void) { return stdin; }
int main(void) { return grab() != 0 ? 0 : 1; }
EOF
if CGF_LD=1 "$CGF" "$WORK/copyhint.c" -o "$WORK/copyhint" \
    2> "$WORK/copyhint.err"; then
    echo "afsld lane FAIL: dynamic data-import link unexpectedly ok" \
        "(LD-ELF-001 may have landed upstream: retire the hint test)" >&2
    fails=$((fails + 1))
else
    grep -qi 'COPY relocation' "$WORK/copyhint.err" || {
        echo "afsld lane FAIL: afs-ld COPY-reloc diagnostic missing" >&2
        cat "$WORK/copyhint.err" >&2
        fails=$((fails + 1))
    }
    grep -q 'retry with -static or unset CGF_LD' "$WORK/copyhint.err" || {
        echo "afsld lane FAIL: driver hint missing" >&2
        fails=$((fails + 1))
    }
fi

[ "$fails" -eq 0 ] || exit 1
echo "afsld lane: $n fixtures behave identically under afs-ld -static;" \
    "structure + LD-ELF-001 hint verified"
