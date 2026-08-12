#!/bin/sh
set -u

target=x86_64-linux-gnu
output=
source_file=
compile_only=0
probe=primary
while [ "$#" -gt 0 ]; do
    case $1 in
    --target=*) target=${1#--target=} ;;
    -dumpmachine)
        echo "${FAKE_TARGET:-$target}"
        exit 0
        ;;
    -o)
        shift
        output=$1
        ;;
    -c) compile_only=1 ;;
    -E) probe=pp ;;
    --dump-ast) probe=parse ;;
    -fsyntax-only) probe=sema ;;
    *.c) source_file=$1 ;;
    esac
    shift
done

base=${source_file##*/}
case $base in
spoof-marker.c)
    echo 'timeout: sending signal TERM to command spoof-compiler' >&2
    ;;
pp-fail.c)
    echo "$source_file:2:1: fatal error: included file not found" >&2
    exit 1
    ;;
parse-fail.c)
    [ "$probe" = pp ] && exit 0
    echo "$source_file:3:8: error: expected expression" >&2
    exit 1
    ;;
punct-semicolon.c | punct-rparen.c)
    [ "$probe" = pp ] && exit 0
    case $base in
    punct-semicolon.c) message="expected ';'" ;;
    punct-rparen.c) message="expected ')'" ;;
    esac
    echo "$source_file:3:8: error: $message" >&2
    exit 1
    ;;
compile-fail.c | sema-fail.c | xfail.c)
    case $probe in pp | parse) exit 0 ;; esac
    echo "$source_file:41:9: error: use of undeclared identifier 'fixture_name'" >&2
    exit 1
    ;;
identifier-alpha.c | identifier-beta.c)
    case $probe in pp | parse) exit 0 ;; esac
    case $base in
    identifier-alpha.c) ident=alpha_name ;;
    identifier-beta.c) ident=beta_name ;;
    esac
    echo "$source_file:6:4: error: use of undeclared identifier '$ident'" >&2
    exit 1
    ;;
source-error-line.c)
    case $probe in pp | parse) exit 0 ;; esac
    echo '  int error_code = rendered_source_only;' >&2
    echo '      ^~~~~~~~~~' >&2
    echo "$source_file:9:3: error: expected ';'" >&2
    exit 1
    ;;
link-tentative.c)
    echo "/usr/bin/ld: other.o:(.bss+0x0): multiple definition of 'shared_global'; fixture.o:(.bss+0x0): first defined here" >&2
    exit 2
    ;;
link-support.c)
    echo "/usr/bin/ld: fixture.o: in function 'entry':" >&2
    echo "fixture.c:(.text+0x9): undefined reference to 'main'" >&2
    exit 2
    ;;
link-deliberate.c)
    echo "/usr/bin/ld: fixture.o: in function 'optimized_path':" >&2
    echo "fixture.c:(.text+0x9): undefined reference to 'link_error'" >&2
    exit 2
    ;;
link-undefined.c)
    echo "/usr/bin/ld: fixture.o: in function 'consumer':" >&2
    echo "fixture.c:(.text+0x9): undefined reference to 'sibling_global'" >&2
    exit 2
    ;;
link-undefined-alt.c)
    printf '/usr/bin/ld: fixture.o: in function \140other_consumer\047:\n' >&2
    printf 'fixture.c:(.text+0x2a): undefined reference to \140other_global\047\n' >&2
    exit 2
    ;;
link-libm.c)
    echo "/usr/bin/ld: fixture.o: in function 'calculate':" >&2
    echo "fixture.c:(.text+0x9): undefined reference to 'sqrt'" >&2
    exit 2
    ;;
link-alloca.c)
    echo "/usr/bin/ld: fixture.o: in function 'allocate':" >&2
    echo "fixture.c:(.text+0x9): undefined reference to 'alloca'" >&2
    exit 2
    ;;
cg-fail.c)
    [ "$probe" != primary ] && exit 0
    echo "$source_file:11:4: error: codegen fixture failure" >&2
    exit 1
    ;;
large-pp-cg-fail.c)
    if [ "$probe" = pp ]; then
        dd if=/dev/zero bs=4096 count=1 2>/dev/null
        exit 0
    fi
    [ "$probe" != primary ] && exit 0
    echo "$source_file:12:4: error: codegen after large phase probe" >&2
    exit 1
    ;;
compile-exit124.c)
    [ "$probe" != primary ] && exit 0
    exit 124
    ;;
compile-timeout.c)
    [ "$probe" != primary ] && exit 0
    while :; do :; done
    ;;
compile-ignore-term.c)
    [ "$probe" != primary ] && exit 0
    trap '' TERM
    while :; do :; done
    ;;
compile-output-flood.c)
    [ "$probe" != primary ] && exit 0
    exec yes compiler-output-flood
    ;;
sibling-path.c)
    case $probe in pp | parse) exit 0 ;; esac
    sibling=${source_file%/*}/include/sibling.h
    echo "$sibling:19:2: error: use of undeclared identifier 'root_specific_name'" >&2
    exit 1
    ;;
class-gcc-builtin.c | class-gcc-builtin-2.c | class-implemented-builtin.c | class-nested.c | class-complex.c | class-computed.c | class-asm-goto.c | class-vector.c)
    case $probe in pp | parse) exit 0 ;; esac
    case $base in
    class-gcc-builtin.c) message="'__builtin_prefetch' is not a builtin this compiler implements (see src/builtins.def)" ;;
    class-gcc-builtin-2.c) message="'__builtin_clz' is not a builtin this compiler implements (see src/builtins.def)" ;;
    class-implemented-builtin.c) message="first argument to '__builtin_va_arg' is not a va_list" ;;
    class-nested.c) message="nested function 'inner' is unsupported" ;;
    class-complex.c) message="type '_Complex' is unsupported" ;;
    class-computed.c) message="computed goto through '&&label' is unsupported" ;;
    class-asm-goto.c) message="asm goto is unsupported" ;;
    class-vector.c) message="vector_size mode attribute is unsupported" ;;
    esac
    echo "$source_file:8:5: error: $message" >&2
    exit 1
    ;;
ice.c)
    echo "$source_file:7:3: internal compiler error: fixture ICE" >&2
    exit 4
    ;;
esac

[ -n "$output" ] || exit 1
if [ "$compile_only" -eq 1 ]; then
    : > "$output"
    exit 0
fi

case $base in
spoof-marker.c)
    printf '%s\n' '#!/bin/sh' "echo 'timeout: sending signal TERM to command spoof-program' >&2" 'exit 0' > "$output"
    ;;
pass.c | compile-pass.c)
    printf '%s\n' '#!/bin/sh' 'exit 0' > "$output"
    ;;
output-fail.c)
    printf '%s\n' '#!/bin/sh' 'echo actual' > "$output"
    ;;
output-extra-blank.c)
    printf '%s\n' '#!/bin/sh' "printf 'expected\\n\\n'" > "$output"
    ;;
output-pass.c)
    printf '%s\n' '#!/bin/sh' 'printf "expected"' > "$output"
    ;;
wrong-exit.c)
    printf '%s\n' '#!/bin/sh' 'exit 7' > "$output"
    ;;
exit124.c)
    printf '%s\n' '#!/bin/sh' 'exit 124' > "$output"
    ;;
signal.c | xfail-signal.c)
    printf '%s\n' '#!/bin/sh' 'kill -TERM $$' > "$output"
    ;;
timeout.c)
    printf '%s\n' '#!/bin/sh' 'while :; do :; done' > "$output"
    ;;
ignore-term.c)
    printf '%s\n' '#!/bin/sh' "trap '' TERM" 'while :; do :; done' > "$output"
    ;;
output-flood.c)
    printf '%s\n' '#!/bin/sh' 'exec yes program-output-flood' > "$output"
    ;;
large-binary.c)
    printf '%s\n' '#!/bin/sh' 'exit 0' > "$output"
    padding_line=0
    while [ "$padding_line" -lt 800 ]; do
        printf '%s\n' '# executable padding survives capture-only limits' >> "$output"
        padding_line=$((padding_line + 1))
    done
    ;;
large-created-file.c)
    printf '%s\n' '#!/bin/sh' \
        'dd if=/dev/zero of=created-large.bin bs=8192 count=1 2>/dev/null' > "$output"
    ;;
cwd.c)
    printf '%s\n' '#!/bin/sh' ': > .cgf-torture-runner-cwd-fixture' > "$output"
    ;;
*)
    printf '%s\n' '#!/bin/sh' 'exit 0' > "$output"
    ;;
esac
chmod +x "$output"
