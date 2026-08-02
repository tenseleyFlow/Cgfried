#!/bin/sh
# Sprint 37 policy boundary: warning names and -W grammar are interpreted in
# src/warn/warn.c. The driver may tokenize the generic option family and the
# preprocessor may hand a pragma string to the public policy API, but neither
# may inspect registry metadata or parse a literal -W spelling itself.
set -eu
LC_ALL=C
export LC_ALL

status=0

hits=$(grep -rnE 'warn_info_for_flag[[:space:]]*\(' src \
    --include='*.c' --include='*.h' |
    grep -v '^src/warn/warn\.c:' |
    grep -v '^src/warn/warn\.h:' || true)
if [ -n "$hits" ]; then
    echo "check_warn_seams: registry lookup outside warn.c:" >&2
    printf '%s\n' "$hits" >&2
    status=1
fi

hits=$(grep -rnE \
    '(strcmp|strncmp|memcmp|strstr|sscanf)[[:space:]]*\([^;]*"-W' src \
    --include='*.c' --include='*.h' |
    grep -v '^src/warn/warn\.c:' || true)
if [ -n "$hits" ]; then
    echo "check_warn_seams: literal -W parsing outside warn.c:" >&2
    printf '%s\n' "$hits" >&2
    status=1
fi

if [ "$status" -eq 0 ]; then
    echo "check_warn_seams: warning option grammar is centralized"
fi
exit "$status"
