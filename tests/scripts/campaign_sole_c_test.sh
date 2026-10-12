#!/bin/sh
set -eu

LC_ALL=C
export LC_ALL

root=$(CDPATH='' cd "$(dirname "$0")/../.." && pwd -P)
sole=$root/scripts/campaigns/sole-c.sh
hostcc=$(command -v cc || command -v gcc || true)
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cgf-campaign-sole-c-test.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
tmp=$(CDPATH='' cd "$tmp" && pwd -P)
[ -n "$hostcc" ] || {
    echo 'campaign-sole-c-meta: no host C compiler' >&2
    exit 1
}

write_source() {
    directory=$1
    mkdir -p "$directory/dup-one" "$directory/dup-two"
    cat >"$directory/lib.c" <<'EOF'
int answer(void) { return 42; }
EOF
    cat >"$directory/main.c" <<'EOF'
int answer(void);
int main(void) { return answer() != 42; }
EOF
    cat >"$directory/bench.c" <<'EOF'
int answer(void);
int main(void) { return answer() != 42; }
EOF
    cat >"$directory/extra.c" <<'EOF'
int extra(void) { return 7; }
EOF
    cat >"$directory/dup-one/shared.c" <<'EOF'
int dup_one(void) { return 11; }
EOF
    cat >"$directory/dup-two/shared.c" <<'EOF'
int dup_two(void) { return 22; }
EOF
}

write_compiler() {
    directory=$1
    cat >"$directory/designated-cc" <<EOF
#!/bin/sh
exec "$hostcc" "\$@"
EOF
    chmod +x "$directory/designated-cc"
}

write_manifest() {
    directory=$1
    cat >"$directory/closure.tsv" <<EOF
# cgf-sole-c-closure-v1
object	$directory/lib.c	$directory/lib.o
object	$directory/main.c	$directory/main.o
object	$directory/dup-one/shared.c	$directory/dup-one/shared.o
object	$directory/dup-two/shared.c	$directory/dup-two/shared.o
compile-link	$directory/bench.c	$directory/bench
archive	$directory/libanswer.a	lib.o	$directory/lib.o
archive	$directory/libduplicates.a	shared.o	$directory/dup-one/shared.o
archive	$directory/libduplicates.a	shared.o	$directory/dup-two/shared.o
link-input	$directory/app	$directory/main.o
link-input	$directory/app	$directory/libanswer.a
link-input	$directory/app	$directory/libduplicates.a
link-input	$directory/bench	$directory/bench.c
link-input	$directory/bench	$directory/libanswer.a
EOF
}

build_fixture() {
    directory=$1
    mode=${2:-valid}
    mkdir -p "$directory"
    write_source "$directory"
    write_compiler "$directory"
    "$sole" init "$directory/receipts" "$directory/designated-cc"
    "$sole" cc "$directory/receipts" "$directory/designated-cc" \
        -c "$directory/lib.c" -o "$directory/lib.o"
    if [ "$mode" = missing-receipt ]; then
        "$hostcc" -c "$directory/main.c" -o "$directory/main.o"
    else
        "$sole" cc "$directory/receipts" "$directory/designated-cc" \
            -c "$directory/main.c" -o "$directory/main.o"
    fi
    "$sole" cc "$directory/receipts" "$directory/designated-cc" \
        -c "$directory/dup-one/shared.c" -o "$directory/dup-one/shared.o"
    "$sole" cc "$directory/receipts" "$directory/designated-cc" \
        -c "$directory/dup-two/shared.c" -o "$directory/dup-two/shared.o"
    [ "$(wc -c <"$directory/dup-one/shared.o" | tr -d ' ')" -eq \
      "$(wc -c <"$directory/dup-two/shared.o" | tr -d ' ')" ] || {
        echo 'campaign-sole-c-meta: duplicate-name objects have unequal sizes' >&2
        exit 1
    }
    if cmp -s "$directory/dup-one/shared.o" "$directory/dup-two/shared.o"; then
        echo 'campaign-sole-c-meta: duplicate-name objects are unexpectedly identical' >&2
        exit 1
    fi
    ar rc "$directory/libanswer.a" "$directory/lib.o"
    ar rcs "$directory/libduplicates.a" \
        "$directory/dup-one/shared.o" "$directory/dup-two/shared.o"
    "$sole" cc "$directory/receipts" "$directory/designated-cc" \
        "$directory/main.o" "$directory/libanswer.a" \
        "$directory/libduplicates.a" -o "$directory/app"
    "$sole" cc "$directory/receipts" "$directory/designated-cc" \
        "$directory/bench.c" "$directory/libanswer.a" -o "$directory/bench"
    write_manifest "$directory"
}

expect_fail() {
    label=$1
    diagnostic=$2
    shift 2
    if "$@" >"$tmp/$label.out" 2>&1; then
        echo "campaign-sole-c-meta: $label unexpectedly passed" >&2
        exit 1
    fi
    grep -F "$diagnostic" "$tmp/$label.out" >/dev/null || {
        echo "campaign-sole-c-meta: $label emitted the wrong diagnostic" >&2
        cat "$tmp/$label.out" >&2
        exit 1
    }
}

build_fixture "$tmp/valid"
"$sole" verify "$tmp/valid/receipts" "$tmp/valid/designated-cc" \
    "$tmp/valid/closure.tsv" "$tmp/valid/report.txt" >/dev/null
grep -F 'project_objects=4' "$tmp/valid/report.txt" >/dev/null
grep -F 'archive_members=3' "$tmp/valid/report.txt" >/dev/null
grep -F 'linked_products=2' "$tmp/valid/report.txt" >/dev/null
grep -F 'translations=5' "$tmp/valid/report.txt" >/dev/null
grep -F 'status=PASS' "$tmp/valid/report.txt" >/dev/null
[ "$(ar t "$tmp/valid/libduplicates.a" | grep -c '^shared\.o$')" -eq 2 ] || {
    echo 'campaign-sole-c-meta: duplicate archive members were not retained' >&2
    exit 1
}

# A process identifier is not a unique invocation identity: long parallel
# campaigns can reuse one, and a sourced adapter naturally makes repeated
# calls in the same shell.  Both must receive independent atomic receipts.
mkdir "$tmp/same-pid"
write_source "$tmp/same-pid"
write_compiler "$tmp/same-pid"
"$sole" init "$tmp/same-pid/receipts" "$tmp/same-pid/designated-cc"
(
    set -- cc "$tmp/same-pid/receipts" "$tmp/same-pid/designated-cc" \
        -c "$tmp/same-pid/lib.c" -o "$tmp/same-pid/lib.o"
    . "$sole"
    set -- cc "$tmp/same-pid/receipts" "$tmp/same-pid/designated-cc" \
        -c "$tmp/same-pid/main.c" -o "$tmp/same-pid/main.o"
    . "$sole"
)
[ "$(find "$tmp/same-pid/receipts/invocations" -mindepth 2 \
    -maxdepth 2 -name receipt.txt | wc -l | tr -d ' ')" -eq 2 ] || {
    echo 'campaign-sole-c-meta: same-PID invocations lost a receipt' >&2
    exit 1
}

build_fixture "$tmp/stale"
printf 'tamper\n' >>"$tmp/stale/lib.o"
expect_fail stale-object 'receipt output digest changed' \
    "$sole" verify "$tmp/stale/receipts" "$tmp/stale/designated-cc" \
    "$tmp/stale/closure.tsv" "$tmp/stale/report.txt"

build_fixture "$tmp/archive"
mkdir "$tmp/archive/host"
"$hostcc" -c "$tmp/archive/extra.c" -o "$tmp/archive/host/lib.o"
rm "$tmp/archive/libanswer.a"
ar rc "$tmp/archive/libanswer.a" "$tmp/archive/host/lib.o"
expect_fail archive-substitution 'archive member differs from certified object' \
    "$sole" verify "$tmp/archive/receipts" "$tmp/archive/designated-cc" \
    "$tmp/archive/closure.tsv" "$tmp/archive/report.txt"

# Keep the duplicate names and byte sizes unchanged while substituting the
# first object for the second.  Only ordered aggregate payload verification
# can distinguish this archive portably: BSD and GNU ar provide different
# nonportable ordinal-selection interfaces.
build_fixture "$tmp/duplicate-archive"
rm "$tmp/duplicate-archive/libduplicates.a"
mkdir "$tmp/duplicate-archive/dup-copy"
cp "$tmp/duplicate-archive/dup-one/shared.o" \
    "$tmp/duplicate-archive/dup-copy/shared.o"
ar rcs "$tmp/duplicate-archive/libduplicates.a" \
    "$tmp/duplicate-archive/dup-one/shared.o" \
    "$tmp/duplicate-archive/dup-copy/shared.o"
expect_fail duplicate-archive-substitution \
    'archive member differs from certified object' \
    "$sole" verify "$tmp/duplicate-archive/receipts" \
    "$tmp/duplicate-archive/designated-cc" \
    "$tmp/duplicate-archive/closure.tsv" \
    "$tmp/duplicate-archive/report.txt"

build_fixture "$tmp/missing" missing-receipt
expect_fail missing-receipt 'recorded translation/link closure differs from the manifest' \
    "$sole" verify "$tmp/missing/receipts" "$tmp/missing/designated-cc" \
    "$tmp/missing/closure.tsv" "$tmp/missing/report.txt"

build_fixture "$tmp/extra"
"$sole" cc "$tmp/extra/receipts" "$tmp/extra/designated-cc" \
    -c "$tmp/extra/extra.c" -o "$tmp/extra/extra.o"
expect_fail extra-translation 'recorded translation/link closure differs from the manifest' \
    "$sole" verify "$tmp/extra/receipts" "$tmp/extra/designated-cc" \
    "$tmp/extra/closure.tsv" "$tmp/extra/report.txt"

build_fixture "$tmp/compiler"
printf '# changed\n' >>"$tmp/compiler/designated-cc"
expect_fail compiler-substitution \
    'verification compiler digest differs from initialized compiler' \
    "$sole" verify "$tmp/compiler/receipts" "$tmp/compiler/designated-cc" \
    "$tmp/compiler/closure.tsv" "$tmp/compiler/report.txt"

build_fixture "$tmp/link-input"
cp "$tmp/link-input/libanswer.a" "$tmp/link-input/other.a"
sed "s#$tmp/link-input/libanswer.a#$tmp/link-input/other.a#" \
    "$tmp/link-input/closure.tsv" >"$tmp/link-input/wrong-closure.tsv"
expect_fail link-input-substitution 'recorded explicit link inputs differ from the manifest' \
    "$sole" verify "$tmp/link-input/receipts" "$tmp/link-input/designated-cc" \
    "$tmp/link-input/wrong-closure.tsv" "$tmp/link-input/report.txt"

printf 'campaign-sole-c-meta: PASS\n'
