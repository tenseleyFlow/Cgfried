#!/bin/sh
set -eu

: "${CGF_MUSL_CGF:?set CGF_MUSL_CGF to the cgfried compiler}"

hostcc=${CGF_MUSL_HOSTCC:-gcc}
route=cgf
host_assembler=0
has_noexecstack=0
source_path=
output_path=
next_is_output=0
next_is_language=0
language=

for arg do
    if [ "$next_is_output" -eq 1 ]; then
        output_path=$arg
        next_is_output=0
        continue
    fi
    if [ "$next_is_language" -eq 1 ]; then
        language=$arg
        next_is_language=0
        continue
    fi
    case $arg in
        -o) next_is_output=1 ;;
        -x) next_is_language=1 ;;
        -Wa,*--noexecstack*) has_noexecstack=1 ;;
        *.c | *.s | *.S) source_path=$arg ;;
    esac
done

case $source_path in
    */src/complex/*.c | src/complex/*.c)
        route=host
        ;;
    *.s | *.S)
        route=host
        host_assembler=1
        ;;
esac
case $language in
    assembler | assembler-with-cpp)
        route=host
        host_assembler=1
        ;;
esac

# Each parallel compiler invocation owns one sidecar.  The campaign collector
# sorts these files, so compiler counts and provenance never depend on job
# completion order.
if [ -n "${CGF_MUSL_ROUTE_DIR:-}" ] && [ -n "$output_path" ]; then
    case $output_path in
        obj/*)
            route_file=$CGF_MUSL_ROUTE_DIR/${output_path#obj/}.route
            mkdir -p "$(dirname "$route_file")"
            printf '%s\t%s\t%s\n' "$route" "$source_path" "$output_path" \
                > "$route_file"
            ;;
    esac
fi

if [ "$route" = host ]; then
    # The Cgfried configure probe cannot communicate GNU assembler flags to
    # the host-only musl assembly route.  Match musl's pristine GCC lane so
    # every CRT object explicitly opts out of an executable stack.
    if [ "$host_assembler" -eq 1 ] && [ "$has_noexecstack" -eq 0 ]; then
        exec "$hostcc" -Wa,--noexecstack "$@"
    fi
    exec "$hostcc" "$@"
fi

exec "$CGF_MUSL_CGF" --target=x86_64-linux-musl -Wno-attributes "$@"
