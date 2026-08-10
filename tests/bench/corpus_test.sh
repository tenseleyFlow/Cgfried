#!/bin/sh
set -eu
LC_ALL=C
export LC_ALL

ROOT=$(CDPATH='' cd "$(dirname "$0")/../.." && pwd -P)
WORK_ROOT=${CGF_BENCH_TEST_WORK:-$ROOT/build/bench-test}
mkdir -p "$WORK_ROOT"
WORK=$(mktemp -d "$WORK_ROOT/run.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM

mkdir "$WORK/unowned"
printf 'must survive\n' >"$WORK/unowned/tu_keep.c"
if CGF_BENCH_TU_COUNT=1 CGF_BENCH_TU_LINES=32 \
    "$ROOT/scripts/gen-tu-corpus.sh" "$WORK/unowned" >/dev/null 2>&1; then
    echo 'corpus_test: generator accepted an unowned nonempty directory' >&2
    exit 1
fi
[ "$(cat "$WORK/unowned/tu_keep.c")" = 'must survive' ] || {
    echo 'corpus_test: generator damaged an unowned directory' >&2
    exit 1
}

sqlite_cksum=$(cksum "$ROOT/tests/bench/corpus/sqlite3/sqlite3.c" |
    awk '{ print $1 ":" $2 }')
[ "$sqlite_cksum" = 2703132855:9282866 ] || {
    echo 'corpus_test: vendored SQLite 3.50.4 bytes do not match the pin' >&2
    exit 1
}

CGF_BENCH_TU_COUNT=500 CGF_BENCH_TU_LINES=200 \
    "$ROOT/scripts/gen-tu-corpus.sh" "$WORK/a" >/dev/null
CGF_BENCH_TU_COUNT=500 CGF_BENCH_TU_LINES=200 \
    "$ROOT/scripts/gen-tu-corpus.sh" "$WORK/b" >/dev/null

[ "$(find "$WORK/a" -maxdepth 1 -type f -name 'tu_*.c' | wc -l)" -eq 500 ] || {
    echo 'corpus_test: generator did not create exactly 500 TUs' >&2
    exit 1
}
diff -ru "$WORK/a" "$WORK/b" >/dev/null || {
    echo 'corpus_test: fixed-seed corpora differ' >&2
    exit 1
}
for source in "$WORK/a"/tu_*.c; do
    [ "$(wc -l < "$source")" -eq 200 ] || {
        echo "corpus_test: $source is not 200 lines" >&2
        exit 1
    }
done

echo 'corpus_test: pinned SQLite 3.50.4; deterministic 500-TU x 200-line corpus'
