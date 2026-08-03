#!/bin/sh
set -eu

CC=${CC:-cc}
BUILD=${BUILD:-build}
WORK_ROOT=${CGF_HEADER_PORTABILITY_WORK:-build/header-portability}

case $WORK_ROOT in
/*) ;;
*) WORK_ROOT=$PWD/$WORK_ROOT ;;
esac

mkdir -p "$WORK_ROOT"
WORK=$(mktemp -d "$WORK_ROOT/run.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
STAGE=$WORK/stage
cat > "$WORK/probe.c" <<'EOF'
#include <cgfried/memsafe.h>

CGF_RETURNS_OWNED void *make_buffer(void);
void consume(void *) CGF_TAKES_OWNERSHIP(1);
void inspect(const void *) CGF_BORROWS(1);
CGF_RETURNS_BORROWED(1) void *alias(void *);
void retain_temporarily(void *) CGF_NO_ESCAPE(1);

int main(void) { return 0; }
EOF

"$CC" -std=c11 -pedantic -Wall -Wextra -Werror -Iinclude \
    -c "$WORK/probe.c" -o "$WORK/probe.o"
make BUILD="$BUILD" DESTDIR="$STAGE" PREFIX=/usr install >/dev/null
test -f "$STAGE/usr/lib/cgfried/include/cgfried/memsafe.h"
cmp include/cgfried/memsafe.h \
    "$STAGE/usr/lib/cgfried/include/cgfried/memsafe.h"
"$STAGE/usr/bin/cgfried" -M "$WORK/probe.c" > "$WORK/deps.out"
grep -F "$STAGE/usr/bin/../lib/cgfried/include/cgfried/memsafe.h" \
    "$WORK/deps.out" >/dev/null
"$STAGE/usr/bin/cgfried" -E "$WORK/probe.c" > "$WORK/cgf.i"
grep -F '__attribute__((cgf_returns_owned))' "$WORK/cgf.i" >/dev/null

printf '%s\n' \
    "header_portability: all five macros compile empty with $CC; staged Cgfried activates them"
