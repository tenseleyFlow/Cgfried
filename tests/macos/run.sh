#!/bin/sh
# arm64-macos ABI mixed-link lane. Run ON a Mac (nomad-1); there is no
# automated CI runner yet -- deliverable D6 owns that.
#
#   sh tests/macos/run.sh [path/to/cgfried]
#
# Every program pairs a Cgfried translation unit with a clang one, because an
# ABI bug both halves share is invisible. The expected values are computed
# from the C semantics independently of any compiler, so a shared bug cannot
# make them agree.
#
# This script exists because landing divergence-table row 3 (natural-size
# stack packing) silently regressed row 1 (anonymous arguments keep their
# eightbyte), and nothing caught it: the programs were verified by hand, once
# each, at the moment they were written.
set -eu
LC_ALL=C
export LC_ALL

CGF=${1:-build/cgfried}
here=$(dirname "$0")
work=${CGF_MACOS_WORK:-build/macos-lane}

case $(uname -s):$(uname -m) in
Darwin:arm64) ;;
*)
    echo "HARNESS_SKIP suite=macos test=abi count=6 reason=\"not arm64 Darwin\""
    exit 0
    ;;
esac

mkdir -p "$work"
pass=0

# Each program is built TWICE, once per linker: the system ld64 lane and the
# bundled afs-ld lane. The assembler is the bundled afs-as in both, which is
# the default routing, so this exercises our own toolchain end to end rather
# than borrowing Apple's for the parts we ship ourselves.
#
# run <name> <ours.c> <theirs.c-or-> <expected output on stdin>
run() {
    name=$1
    ours=$2
    theirs=$3
    shift 3
    want=$(cat)

    partner=
    if [ "$theirs" != "-" ]; then
        # The partner TU is clang's on purpose: an ABI bug both halves share
        # is invisible, so the two must disagree for the test to mean
        # anything. Several partners also include system headers, which we
        # cannot preprocess until Sprint 55.
        partner=$work/$name.their.o
        clang -O1 -c -o "$partner" "$here/$theirs"
    fi

    for ld in system afs; do
        out=$work/$name.$ld
        if [ "$ld" = afs ]; then
            CGF_LD=1 "$CGF" -o "$out" "$here/$ours" $partner
        else
            "$CGF" -o "$out" "$here/$ours" $partner
        fi
        got=$("$out")
        if [ "$got" != "$want" ]; then
            echo "macos_lane: $name disagreed with its reference ($ld ld)" >&2
            echo "--- want:" >&2
            echo "$want" >&2
            echo "--- got:" >&2
            echo "$got" >&2
            exit 1
        fi
        # D4: every product must be signed, on both lanes.
        if ! codesign --verify "$out" 2>/dev/null; then
            echo "macos_lane: $name ($ld ld) is not validly signed" >&2
            exit 1
        fi
        pass=$((pass + 1))
    done
}

# Row 1: our CALLER puts every anonymous argument on the stack. Also runs
# through the system libc's printf, so it is a second, independent callee.
run varargs_caller vacall.c vadefs.c <<'EOF'
sum9=45
avg3=3.5000
mix=306
many=1 2 3 4 5 6 7 8 9 10
mixed=1 2.5 3 4.5 tail
EOF

# Row 1: our CALLEE reads them back with a plain cursor.
run varargs_callee rev_callee.c rev_caller.c <<'EOF'
rsum=45
rmix=7.00
EOF

# Row 1: both halves ours.
run varargs_both vaboth.c - <<'EOF'
tally=55
dsum=4.00
EOF

# Rows 2 and 3: our CALLER widens sub-32-bit arguments and packs the named
# stack tail at natural size.
run ext_pack_caller rows23.c rows23_defs.c <<'EOF'
ext4=65280
pack5=4556199
EOF

# Rows 2 and 3: our CALLEE reads that packed tail at the same offsets.
run ext_pack_callee rev23.c rev23_main.c <<'EOF'
ext4x=65280
pack5x=4556199
EOF

# Rows 4, 5 and 7. No partner TU: every claim is a _Static_assert, so the
# compile itself is most of the test. clang --target=aarch64-linux-gnu
# rejects the same file at row 4, which is what makes it anti-vacuous.
run rows47 rows47.c - <<'EOF'
row4 char=-1
row5 sizeof=8 third=0.33333333333333331
row7 wchar=4
row5 pct_Lf=3.1415926536 -0.5000000000
EOF

# AAPCS64 passes and returns a homogeneous floating aggregate in v0-v3 with
# no hidden pointer. Both halves were classified since Sprint 14 and consumed
# nowhere until Sprint 51 (ABI-001). The result is the EXIT CODE rather than
# printf output, because <stdio.h> here still needs Sprint 55 -- which is
# also why our CALLER half could only be checked this way.
run hfa_abi hfa_check.c hfa_lib.c <<'EOF'
EOF

# Mach-O routes every UNDEFINED symbol through the GOT even non-PIC, for a
# function's address as much as for data. A direct adrp/add is not a slower
# program, it is a link error or a wrong address.
run got_extern gotlink.c gotlink_defs.c <<'EOF'
ext_data=41 via_ptr=41
arr3=30 s.c=33
fn=99 same=1
def=1000 stat=2000
EOF

echo "macos_lane: $pass builds agree with their references and pass codesign" \
    "($((pass / 2)) programs x system ld + afs-ld)"
