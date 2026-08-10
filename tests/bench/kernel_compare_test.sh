#!/bin/sh
set -eu

repo=$(CDPATH='' cd "$(dirname "$0")/../.." && pwd -P)
compare=${1:-$repo/scripts/kernel-compare.sh}
tmp=${TMPDIR:-/tmp}/cgf-kernel-compare-test.$$
mkdir -p "$tmp/bin" "$tmp/kernels" "$tmp/work-a" "$tmp/work-b"
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

fail()
{
    echo "kernel_compare_test: $*" >&2
    exit 1
}

cat >"$tmp/kernels/tiny.c" <<'EOF'
static volatile unsigned sink;
__attribute__((noinline)) unsigned kernel_run(void) { return 42; }
int main(void) { unsigned got = kernel_run(); sink = got; return got != 42; }
EOF

cat >"$tmp/bin/fake-cgf" <<'EOF'
#!/bin/sh
set -eu
set -- "$@"
new=
for arg do
    case $arg in
    --target=*) ;;
    *)
        if [ -z "$new" ]; then
            new=$arg
        else
            new="$new
$arg"
        fi
        ;;
    esac
done
set --
old_ifs=$IFS
IFS='
'
for arg in $new; do set -- "$@" "$arg"; done
IFS=$old_ifs
exec gcc "$@"
EOF
chmod +x "$tmp/bin/fake-cgf"

cat >"$tmp/runtime.txt" <<'EOF'
# cgfried kernel runtime metrics v1
date=2026-08-10T12:00:00Z
host=fixture-host
target=x86_64-linux-gnu
x86_64-linux-gnu.tiny.O2.cgf.wall_ms_median=12.000000
x86_64-linux-gnu.tiny.O2.cgf.wall_ms_mad=0.100000
x86_64-linux-gnu.tiny.O2.gcc.wall_ms_median=8.000000
x86_64-linux-gnu.tiny.O2.gcc.wall_ms_mad=0.100000
EOF

run_compare()
{
    run_work=$1
    run_output=$2
    run_runtime=$3
    CGF_KERNEL_CGF=$tmp/bin/fake-cgf \
    CGF_KERNEL_DIR=$tmp/kernels \
    CGF_KERNEL_COMPARE_WORK=$run_work \
    CGF_KERNEL_TARGETS=x86_64-linux-gnu \
    CGF_KERNEL_OPTS=O2 \
    CGF_KERNEL_MIN=1 \
    CGF_KERNEL_RUNTIME_INPUT=$run_runtime \
        "$compare" "$run_output" >"$run_work/stdout.txt"
}

run_compare "$tmp/work-a" "$tmp/a.md" "$tmp/runtime.txt"
run_compare "$tmp/work-b" "$tmp/b.md" "$tmp/runtime.txt"
cmp -s "$tmp/a.md" "$tmp/b.md" || fail "static dashboard is not deterministic"
grep -F '| tiny | -O2 |' "$tmp/a.md" >/dev/null || fail "missing static row"
grep -F '| 1.500x |' "$tmp/a.md" >/dev/null || fail "runtime ratio is wrong"
grep -F 'fixture-host' "$tmp/a.md" >/dev/null || fail "runtime provenance is missing"

sed 's/12[.]000000/12.008000/' "$tmp/runtime.txt" >"$tmp/runtime-hot.txt"
mkdir -p "$tmp/work-hot"
run_compare "$tmp/work-hot" "$tmp/hot.md" "$tmp/runtime-hot.txt"
grep -F '| 1.501x ⚠ |' "$tmp/hot.md" >/dev/null ||
    fail "ratio above 1.5x is not marked for follow-up"

cat >"$tmp/runtime-incomplete.txt" <<'EOF'
date=2026-08-10T12:00:00Z
host=fixture-host
target=x86_64-linux-gnu
x86_64-linux-gnu.tiny.O2.cgf.wall_ms_median=12.000000
EOF
if CGF_KERNEL_CGF=$tmp/bin/fake-cgf \
   CGF_KERNEL_DIR=$tmp/kernels \
   CGF_KERNEL_COMPARE_WORK=$tmp/work-incomplete \
   CGF_KERNEL_TARGETS=x86_64-linux-gnu \
   CGF_KERNEL_OPTS=O2 CGF_KERNEL_MIN=1 \
   CGF_KERNEL_RUNTIME_INPUT=$tmp/runtime-incomplete.txt \
       "$compare" "$tmp/incomplete.md" >"$tmp/incomplete.out" \
       2>"$tmp/incomplete.err"; then
    fail "incomplete runtime pair passed"
fi
grep -F 'incomplete runtime median/MAD pair' "$tmp/incomplete.err" >/dev/null ||
    fail "incomplete runtime diagnostic is missing"

echo "kernel_compare_test: deterministic static columns, provenance, runtime ratio, and fail-closed pair validation passed"
