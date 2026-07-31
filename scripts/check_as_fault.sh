#!/bin/sh
# Sprint 24 DoD 5: an assembler rejecting cgfried-GENERATED assembly is
# an ICE (exit 4) quoting the offending .s line — injected here via a
# fake assembler that rejects line 3 of whatever it is fed.
set -u
LC_ALL=C
export LC_ALL

CGF=${1:-build/cgf}
WORK=${CGF_ASFAULT_WORK:-build/asfault-work}
mkdir -p "$WORK"

cat > "$WORK/fake-as.sh" << 'FAKE'
#!/bin/sh
# consume args; reject with a caret diagnostic naming line 3
for a in "$@"; do
    case $a in
    *.s) echo "$a:3:1: error: injected fault" >&2 ;;
    esac
done
exit 1
FAKE
chmod +x "$WORK/fake-as.sh"

printf 'int f(void) { return 42; }\n' > "$WORK/t.c"
CGF_AS_PATH="$WORK/fake-as.sh" "$CGF" -c "$WORK/t.c" -o "$WORK/t.o" \
    > "$WORK/out.log" 2>&1
rc=$?
if [ "$rc" -ne 4 ]; then
    echo "check_as_fault: expected exit 4 (ICE), got $rc" >&2
    cat "$WORK/out.log" >&2
    exit 1
fi
if ! grep -q "line 3" "$WORK/out.log"; then
    echo "check_as_fault: ICE did not name the assembler's line" >&2
    cat "$WORK/out.log" >&2
    exit 1
fi
if ! grep -q 'p2align' "$WORK/out.log"; then
    echo "check_as_fault: ICE did not QUOTE the offending .s line" >&2
    cat "$WORK/out.log" >&2
    exit 1
fi
echo "check_as_fault: assembler rejection ICEs with the line quoted"
