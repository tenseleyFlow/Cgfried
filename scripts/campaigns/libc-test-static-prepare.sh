#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "libc-test-static-prepare: usage: TREE" >&2
    exit 2
fi

makefile=$1/Makefile
# This is a literal fragment of the pinned makefile, not shell expansion.
# shellcheck disable=SC2016
needle='$(B)/$(1)-static.err: $(B)/$(1).err'
marker='# cgfried campaign: static-only lane omits the duplicate-run prerequisite'
# This target is injected into the pinned archived tree so the campaign can
# compare the binaries selected by libc-test's own BINS expansion with the
# binaries that were actually produced.
# shellcheck disable=SC2016
manifest_needle='all run: $(B)/REPORT'
manifest_marker='# cgfried campaign: report the exact static-only binary manifest'

[ -f "$makefile" ] || {
    echo "libc-test-static-prepare: missing Makefile: $makefile" >&2
    exit 1
}
[ "$(grep -Fxc "$needle" "$makefile")" -eq 1 ] || {
    echo "libc-test-static-prepare: pinned prerequisite shape changed" >&2
    exit 1
}
[ "$(grep -Fxc "$manifest_needle" "$makefile")" -eq 1 ] || {
    echo "libc-test-static-prepare: pinned manifest insertion point changed" >&2
    exit 1
}

awk -v needle="$needle" -v marker="$marker" \
    -v manifest_needle="$manifest_needle" \
    -v manifest_marker="$manifest_marker" '
    $0 == needle { print marker; next }
    $0 == manifest_needle {
        print manifest_marker
        print ".PHONY: cgfried-static-manifest"
        print "cgfried-static-manifest:"
        print "\t@printf \"%s\\n\" $(filter %-static.exe,$(BINS))"
        print ""
        print
        next
    }
    { print }
' "$makefile" >"$makefile.cgfried.tmp"
mv "$makefile.cgfried.tmp" "$makefile"

[ "$(grep -Fxc "$marker" "$makefile")" -eq 1 ] || {
    echo "libc-test-static-prepare: failed to record the static-lane repair" >&2
    exit 1
}
[ "$(grep -Fxc "$manifest_marker" "$makefile")" -eq 1 ] || {
    echo "libc-test-static-prepare: failed to add the static manifest target" >&2
    exit 1
}
