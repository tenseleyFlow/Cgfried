#!/bin/sh
# Fake compiler for the runner's meta-suite, injected via CGF_TEST_CC.
# Invoked like the real thing: fake-cc.sh <src> -o <out>. Behavior is keyed
# on the test source's basename; the "binary" it produces is a shell script.
set -eu

# argv shape mirrors the real driver: [flags...] <src> [-o <out>]. The
# source is the first non-option argument.
src=""
out=""
while [ $# -gt 0 ]; do
    case $1 in
    -o)
        out=$2
        shift 2
        ;;
    -*) shift ;;
    *)
        [ -n "$src" ] || src=$1
        shift
        ;;
    esac
done

emit() {
    printf '%s' "$1" >"$out"
    chmod +x "$out"
}

case $(basename "$src") in
check_pass.c | check_order.c)
    emit '#!/bin/sh
echo one
echo two
'
    ;;
exit_code_pass.c | exit_code_fail.c | xfail_fail.c)
    emit '#!/bin/sh
exit 3
'
    ;;
err_expected_pass.c)
    echo "error: expected ';'" >&2
    exit 1
    ;;
flags_env_pp.c)
    # Exercises FLAGS -E (compiler stdout is the result) + ENV injection.
    if [ "${CGF_META_MODE:-}" != "ppcheck" ]; then
        echo "fake-cc: CGF_META_MODE not injected" >&2
        exit 1
    fi
    printf 'alpha\nbeta\n'
    exit 0
    ;;
err_expected_fail.c)
    echo "error: unrelated diagnostic" >&2
    exit 1
    ;;
timeout.c)
    emit '#!/bin/sh
sleep 30
'
    ;;
signal.c)
    emit '#!/bin/sh
kill -s SEGV $$
'
    ;;
big_stderr.c)
    # 1 MiB on stderr, silence on stdout: the pipe-deadlock regression case.
    emit '#!/bin/sh
dd if=/dev/zero bs=1024 count=1024 2>/dev/null | tr "\0" x >&2
exit 0
'
    ;;
binary_out.c)
    emit '#!/bin/sh
printf "\000\001\002"
'
    ;;
*)
    emit '#!/bin/sh
exit 0
'
    ;;
esac
