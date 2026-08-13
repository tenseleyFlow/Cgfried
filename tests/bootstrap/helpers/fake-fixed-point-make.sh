#!/bin/sh
# Minimal stage graph used to prove that scripts/bootstrap.sh itself rejects
# three executable nondeterminism mechanisms. The real fault compiler still
# produces the stage bytes; this helper only replaces the expensive 113-TU
# build graph in the meta-suite.
set -eu

repo=$(CDPATH='' cd "$(dirname "$0")/../../.." && pwd -P)
build=
stage=
compiler=
for argument in "$@"; do
    case $argument in
    BUILD=*) build=${argument#BUILD=} ;;
    BOOTSTRAP_ROOT=*) stage=${argument#BOOTSTRAP_ROOT=} ;;
    BOOTSTRAP_CGF=*) compiler=${argument#BOOTSTRAP_CGF=} ;;
    esac
done

fault_for_work()
{
    work=$1
    name=${work##*/}
    case $name in
    O0-* | O2-*) printf '%s\n' "${name#*-}" ;;
    *) exit 2 ;;
    esac
}

if [ -n "$build" ]; then
    work=${build%/stage0}
    fault=$(fault_for_work "$work")
    meta_root=${work%/*}
    mkdir -p "$build"
    cp "$meta_root/fault-compilers/$fault-0" "$build/cgfried"
    cp "$repo/tests/bootstrap/helpers/fake-bootstrap-timeit.sh" \
        "$build/timeit"
    chmod +x "$build/cgfried" "$build/timeit"
    exit 0
fi

[ -n "$stage" ] && [ -n "$compiler" ] || exit 2
work=${stage%/*}
fault=$(fault_for_work "$work")
meta_root=${work%/*}
source=$repo/tests/bootstrap/faults/$fault.c
artifact=compiler/tests/bootstrap/faults/$fault
mkdir -p "$stage/${artifact%/*}" "$stage/runtime/src/rt" \
    "$stage/x86_64-linux-gnu"
CGF_BOOTSTRAP_FAULT_DIR=$meta_root/readdir-seed \
    "$compiler" -S "$source" -o "$stage/$artifact.s"
cp "$stage/$artifact.s" "$stage/$artifact.o"
printf '%s\n' 'runtime assembly' >"$stage/runtime/src/rt/fault.s"
printf '%s\n' 'runtime object' >"$stage/runtime/src/rt/fault.o"
printf '%s\n' 'runtime archive' >"$stage/x86_64-linux-gnu/libcgf_rt.a"
cp "$meta_root/fault-compilers/$fault-1" "$stage/cgfried"
chmod +x "$stage/cgfried"
