#!/bin/sh
# Exercise the real compiler's dump tree, then perturb one named boundary in
# the "bad" wrapper so the bisector must identify that exact file.
set -eu

: "${CGF_PHASE_REAL_CC:?CGF_PHASE_REAL_CC is required}"
"$CGF_PHASE_REAL_CC" "$@"

case $0 in
*phase-dump-missing-optimizer*)
    if [ "${CGF_DUMP_IR:-}" = all ]; then
        for dump in \
            "$CGF_DUMP_IR_DIR"/[4-6][0-9][0-9][0-9][0-9][0-9]-ir-fp*-i*-p*-*.cgfir; do
            [ -f "$dump" ] || continue
            rm -f "$dump"
        done
    fi
    ;;
*phase-dump-bad*)
    if [ "${CGF_DUMP_IR:-}" = all ]; then
        printf '%s\n' '// seeded phase-tree divergence' >> \
            "$CGF_DUMP_IR_DIR/700000-ir-post-opt-legalized.cgfir"
    fi
    ;;
esac
