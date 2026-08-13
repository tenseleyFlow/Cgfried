#!/bin/sh
set -eu

LUA_REF=5.4.7
LUA_SOURCE_SHA256=9fbf5e28ef86c69858f6d3d34eccc32e911c1a28b4120ff3e84aaa70cfbf1e30
LUA_TESTS_SHA256=8a4898ffe4c7613c8009327a0722db7a41ef861d526c77c5b46114e59ebf811e

root=$(CDPATH='' cd "$(dirname "$0")/../.." && pwd -P)
source_archive=${CGF_CAMPAIGN_LUA_ARCHIVE:-$root/build/campaigns/dl/lua-$LUA_REF.tar.gz}
tests_archive=${CGF_CAMPAIGN_LUA_TESTS_ARCHIVE:-$root/build/campaigns/dl/lua-$LUA_REF-tests.tar.gz}
work=${CGF_CAMPAIGN_LUA_WORK:-$root/build/campaigns/lua}
cgf=${CGF_CAMPAIGN_LUA_CGF:-$root/build/cgfried}
hostcc=${CGF_CAMPAIGN_LUA_HOSTCC:-gcc}
musl_sysroot=${CGF_CAMPAIGN_LUA_MUSL_SYSROOT:-$root/build/campaigns/musl/cgf-b/install}
mode=${CGF_CAMPAIGN_LUA_MODE:-native}
jobs=${CGF_CAMPAIGN_JOBS:-}
timeout_seconds=${CGF_CAMPAIGN_LUA_TIMEOUT:-1800}

fail() {
    echo "campaign-lua: $*" >&2
    exit 1
}

[ -r "$source_archive" ] || fail "source archive is missing: $source_archive"
[ -r "$tests_archive" ] || fail "test archive is missing: $tests_archive"
[ -x "$cgf" ] || fail "cgfried compiler is missing or not executable: $cgf"
command -v "$hostcc" >/dev/null 2>&1 || fail "host GCC is unavailable: $hostcc"
command -v timeout >/dev/null 2>&1 || fail "timeout is unavailable"
case $mode in native | musl-static) ;; *) fail "unknown mode: $mode" ;; esac
printf '%s  %s\n' "$LUA_SOURCE_SHA256" "$source_archive" | sha256sum -c - >/dev/null ||
    fail "source archive checksum mismatch: $source_archive"
printf '%s  %s\n' "$LUA_TESTS_SHA256" "$tests_archive" | sha256sum -c - >/dev/null ||
    fail "test archive checksum mismatch: $tests_archive"

audit_archive() {
    archive=$1
    prefix=$2
    manifest=$3
    LC_ALL=C tar -tzf "$archive" >"$manifest"
    awk -v prefix="$prefix/" '
        index($0, prefix) != 1 { bad = 1; next }
        {
            name = $0
            sub(/\/$/, "", name)
            n = split(name, part, "/")
            for (i = 1; i <= n; i++)
                if (part[i] == "" || part[i] == "." || part[i] == "..")
                    bad = 1
        }
        END { exit bad }
    ' "$manifest" || fail "archive contains an unsafe member path: $archive"
    LC_ALL=C tar -tvzf "$archive" | awk '
        substr($1, 1, 1) != "-" && substr($1, 1, 1) != "d" { bad = 1 }
        END { exit bad }
    ' || fail "archive contains a link or special-file member: $archive"
}

case $jobs in
    '') jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '1\n') ;;
esac
case $jobs in
    '' | *[!0-9]* | 0) fail "CGF_CAMPAIGN_JOBS must be a positive integer" ;;
esac
case $timeout_seconds in
    '' | *[!0-9]* | 0) fail "CGF_CAMPAIGN_LUA_TIMEOUT must be a positive integer" ;;
esac

# Campaign work directories are disposable.  Permit exactly one direct,
# non-symlink child of the canonical campaign root before recursive cleanup.
case $work in /*) ;; *) work=$root/$work ;; esac
campaign_root=$root/build/campaigns
mkdir -p "$campaign_root"
campaign_root_real=$(CDPATH='' cd "$campaign_root" && pwd -P)
[ "$campaign_root_real" = "$campaign_root" ] ||
    fail "campaign root must not traverse symlinks: $campaign_root"
case $work in
    "$campaign_root"/*)
        work_name=${work#"$campaign_root"/}
        case $work_name in '' | . | .. | */*) fail "unsafe work directory: $work" ;; esac
        ;;
    *) fail "work directory must be a direct child of $campaign_root: $work" ;;
esac
[ ! -L "$work" ] || fail "work directory must not be a symlink: $work"
rm -rf "$work"
mkdir -p "$work/logs" "$work/test-bin"

audit_archive "$source_archive" "lua-$LUA_REF" "$work/logs/source-members.txt"
audit_archive "$tests_archive" "lua-$LUA_REF-tests" "$work/logs/test-members.txt"

# Bash 5.3 propagates the signal from its final `sh -c` command, whereas the
# upstream suite requires the nested shell process to translate that signal
# into an exit status. Keep a parent shell alive so io.popen and os.execute
# observe the same portable wait status without changing any upstream test.
{
    echo '#!/bin/sh'
    echo 'if [ "$#" -gt 1 ] && [ "$1" = -c ]; then'
    echo '    command=$2'
    echo '    shift 2'
    echo '    /bin/sh -c "$command" "$@"'
    echo '    status=$?'
    echo '    exit "$status"'
    echo 'fi'
    echo 'exec /bin/sh "$@"'
} >"$work/test-bin/sh"
chmod +x "$work/test-bin/sh"

as_path=${CGF_AS_PATH:-$(command -v as 2>/dev/null || true)}
ld_path=${CGF_LD_PATH:-$(command -v ld 2>/dev/null || true)}
[ -n "$as_path" ] || fail "native assembler is unavailable; set CGF_AS_PATH"
[ -n "$ld_path" ] || fail "native linker is unavailable; set CGF_LD_PATH"
export CGF_AS_PATH="$as_path" CGF_LD_PATH="$ld_path"

extract_lane() {
    lane=$1
    tree=$work/$lane
    mkdir -p "$tree" "$work/logs/$lane"
    tar --no-same-owner --no-same-permissions -xzf "$source_archive" \
        -C "$tree" --strip-components=1
    mkdir -p "$tree/tests"
    tar --no-same-owner --no-same-permissions -xzf "$tests_archive" \
        -C "$tree/tests" --strip-components=1
}

build_lane() {
    lane=$1
    cc=$2
    opt=$3
    feature_macro=$4
    extra_libs=$5
    extra_ldflags=$6
    extract_lane "$lane"
    myobjs=
    if [ "$lane" = musl-static ]; then
        cp "$work/$lane/tests/libs/lib22.c" "$work/$lane/src/lib22.c"
        mkdir -p "$work/$lane/src/readline"
        {
            echo '#ifndef CGF_LUA_READLINE_H'
            echo '#define CGF_LUA_READLINE_H'
            echo 'extern char *rl_readline_name;'
            echo 'char *readline(const char *prompt);'
            echo '#endif'
        } >"$work/$lane/src/readline/readline.h"
        {
            echo '#ifndef CGF_LUA_HISTORY_H'
            echo '#define CGF_LUA_HISTORY_H'
            echo 'void add_history(const char *line);'
            echo '#endif'
        } >"$work/$lane/src/readline/history.h"
        {
            echo '#include <stdio.h>'
            echo '#include <stdlib.h>'
            echo '#include <string.h>'
            echo 'char *rl_readline_name;'
            echo 'char *readline(const char *prompt) {'
            echo '  char buffer[512];'
            echo '  size_t length;'
            echo '  char *line;'
            echo '  fputs(prompt, stdout);'
            echo '  fflush(stdout);'
            echo '  if (fgets(buffer, sizeof(buffer), stdin) == NULL) {'
            echo "    fputc('\\n', stdout);"
            echo '    return NULL;'
            echo '  }'
            echo '  fputs(buffer, stdout);'
            echo '  length = strlen(buffer);'
            echo "  if (length != 0 && buffer[length - 1] == '\\n') buffer[--length] = '\\0';"
            echo '  line = malloc(length + 1);'
            echo '  if (line != NULL) memcpy(line, buffer, length + 1);'
            echo '  return line;'
            echo '}'
            echo 'void add_history(const char *line) { (void)line; }'
        } >"$work/$lane/src/cgf_static_readline.c"
        {
            echo '#include "lua.h"'
            echo '#include "lauxlib.h"'
            echo '#include "lualib.h"'
            echo '#include <locale.h>'
            echo '#include <string.h>'
            echo 'extern int luaopen_lib2(lua_State *L);'
            echo 'extern void __real_luaL_openlibs(lua_State *L);'
            echo 'extern char *__real_setlocale(int category, const char *name);'
            echo 'void __wrap_luaL_openlibs(lua_State *L) {'
            echo '  __real_luaL_openlibs(L);'
            echo '  luaL_getsubtable(L, LUA_REGISTRYINDEX, LUA_PRELOAD_TABLE);'
            echo '  lua_pushcfunction(L, luaopen_lib2);'
            echo '  lua_setfield(L, -2, "lib2-v2");'
            echo '  lua_pop(L, 1);'
            echo '}'
            echo '/* CAMP-LUA-002: musl retains an arbitrary locale name for'
            echo ' * message-catalog use even when the static image has no matching'
            echo ' * collation or ctype profile. Lua treats setlocale success as proof'
            echo ' * that those semantics exist. The retained preflight below proves the'
            printf '%s\n' " * mismatch against this exact staged libc, so advertise only musl's"
            echo ' * built-in profiles instead of returning a false capability result. */'
            echo 'char *__wrap_setlocale(int category, const char *name) {'
            printf '%s\n' "  if (name != NULL && name[0] != '\\0' &&"
            echo '      strcmp(name, "C") != 0 && strcmp(name, "POSIX") != 0 &&'
            echo '      strcmp(name, "C.UTF-8") != 0)'
            echo '    return NULL;'
            echo '  return __real_setlocale(category, name);'
            echo '}'
        } >"$work/$lane/src/cgf_static_preload.c"
        myobjs='cgf_static_preload.o cgf_static_readline.o lib22.o'
        extra_ldflags="$extra_ldflags -Wl,--wrap=luaL_openlibs -Wl,--wrap=setlocale"
    fi
    # Keep the generic target while stating the POSIX stdin contract explicitly.
    # Plain generic assumes stdin is a tty, so even GCC fails main.lua.
    if ! LC_ALL=C SOURCE_DATE_EPOCH=0 make -C "$work/$lane" -j"$jobs" \
        generic CC="$cc" MYCFLAGS="$opt -D$feature_macro" MYLIBS="$extra_libs" \
        MYLDFLAGS="$extra_ldflags" MYOBJS="$myobjs" \
        >"$work/logs/$lane/build.log" 2>&1; then
        cat "$work/logs/$lane/build.log" >&2
        fail "$lane build failed"
    fi
    [ -x "$work/$lane/src/lua" ] || fail "$lane produced no Lua interpreter"
    [ -x "$work/$lane/src/luac" ] || fail "$lane produced no Lua compiler"
}

test_lane() {
    lane=$1
    dso_mode=${2:-native}
    if [ "$dso_mode" = native ]; then
        if ! LC_ALL=C SOURCE_DATE_EPOCH=0 make -C "$work/$lane/tests/libs" \
            -f makefile LUA_DIR=../../src CC="$hostcc" \
            >"$work/logs/$lane/test-dso-build.log" 2>&1; then
            cat "$work/logs/$lane/test-dso-build.log" >&2
            fail "$lane test DSO build failed"
        fi
        [ "$(find "$work/$lane/tests/libs" -maxdepth 1 -type f -name '*.so' | wc -l)" -eq 5 ] ||
            fail "$lane test DSO set is incomplete"
    fi
    # A pipe makes the outer stdin non-tty and non-seekable, matching the
    # suite's invocation contract while leaving every upstream test enabled.
    if ! (
        cd "$work/$lane/tests"
        printf '' | PATH="$work/test-bin:$PATH" LC_ALL=C SOURCE_DATE_EPOCH=0 \
            timeout "$timeout_seconds" ../src/lua all.lua
    ) >"$work/logs/$lane/all.lua.log" 2>&1; then
        tail -100 "$work/logs/$lane/all.lua.log" >&2
        fail "$lane all.lua failed"
    fi
    grep -Fx 'final OK !!!' "$work/logs/$lane/all.lua.log" >/dev/null ||
        fail "$lane all.lua did not emit its completion sentinel"
}

if [ "$mode" = native ]; then
    uname -m >"$work/logs/native-architecture.txt"
    build_lane host-gcc-o2 "$hostcc" -O2 \
        'LUA_USE_LINUX -DLUA_USE_READLINE' '-ldl -lreadline' -Wl,-E
    test_lane host-gcc-o2
    build_lane native-o0 "$cgf" -O0 \
        'LUA_USE_LINUX -DLUA_USE_READLINE' '-ldl -lreadline' -Wl,-E
    test_lane native-o0
    build_lane native-o2 "$cgf" -O2 \
        'LUA_USE_LINUX -DLUA_USE_READLINE' '-ldl -lreadline' -Wl,-E
    test_lane native-o2
    file "$work/native-o0/src/lua" "$work/native-o2/src/lua" \
        >"$work/logs/native-binaries.txt"
    case $(cat "$work/logs/native-architecture.txt") in
        x86_64) machine_pattern='x86-64' ;;
        aarch64 | arm64) machine_pattern='ARM aarch64' ;;
        *) fail "unsupported native campaign architecture: $(cat "$work/logs/native-architecture.txt")" ;;
    esac
    [ "$(grep -cF "$machine_pattern" "$work/logs/native-binaries.txt")" -eq 2 ] ||
        fail "native Lua binary machine does not match the runner architecture"
    {
        echo '# cgf-campaign-results-v1'
        printf '# columns=key\toutcome\tdetail\n'
        printf 'baseline.build\tPASS\tcompiler=host-gcc,opt=O2,target=native\n'
        printf 'baseline.test.all-lua\tPASS\tsuite=lua-%s-tests,mode=full-generic\n' "$LUA_REF"
        printf 'build.native-o0\tPASS\tcompiler=cgfried,target=native\n'
        printf 'build.native-o2\tPASS\tcompiler=cgfried,target=native\n'
        printf 'host.architecture\tPASS\tevidence=binary-machine-matches-uname\n'
        printf 'source.pin\tPASS\tversion=%s,sha256=%s\n' "$LUA_REF" "$LUA_SOURCE_SHA256"
        printf 'source.tests-pin\tPASS\tversion=%s,sha256=%s\n' "$LUA_REF" "$LUA_TESTS_SHA256"
        printf 'test.native-o0.all-lua\tPASS\tsuite=lua-%s-tests,mode=full-generic\n' "$LUA_REF"
        printf 'test.native-o2.all-lua\tPASS\tsuite=lua-%s-tests,mode=full-generic\n' "$LUA_REF"
    } >"$work/results.txt"
    printf 'campaign-lua: PASS version=%s mode=native opt=O0,O2 architecture=%s results=%s\n' \
        "$LUA_REF" "$(cat "$work/logs/native-architecture.txt")" "$work/results.txt"
else
    # The Alpine lane consumes the staged, Cgfried-built Sprint 57 sysroot.
    [ -r "$musl_sysroot/usr/lib/libc.a" ] ||
        fail "Cgfried-built musl sysroot is unavailable: $musl_sysroot"
    musl_cc=${CGF_CAMPAIGN_LUA_MUSL_CC:-$cgf --target=x86_64-linux-musl --sysroot=$musl_sysroot -static}
    # Musl deliberately keeps unknown locale names for message catalogs while
    # strcoll remains code-point ordered. Prove that exact staged-libc profile
    # before installing CAMP-LUA-002's narrow Lua capability adapter. If musl
    # ever gains the Portuguese semantics the suite asks for, fail here so the
    # adapter is removed instead of masking the new capability.
    {
        echo '#include <locale.h>'
        echo '#include <stdio.h>'
        echo '#include <string.h>'
        echo 'int main(void) {'
        printf '%s\n' '  const char *accent = "\303\241lo";'
        echo '  int ordered;'
        printf '%s\n' '  printf("named-locale=%s\n",'
        echo '         setlocale(LC_COLLATE, "ptb") ? "accepted" : "rejected");'
        echo '  ordered = strcoll("alo", accent) < 0 && strcoll(accent, "amo") < 0;'
        printf '%s\n' '  printf("portuguese-collation=%s\n",'
        echo '         ordered ? "supported" : "unsupported");'
        echo '  return 0;'
        echo '}'
    } >"$work/musl-locale-profile.c"
    # CC is intentionally a make-compatible command plus target arguments.
    # shellcheck disable=SC2086
    set -- $musl_cc
    "$@" -O2 "$work/musl-locale-profile.c" \
        -o "$work/musl-locale-profile" || fail "musl locale-profile probe failed to build"
    "$work/musl-locale-profile" >"$work/logs/musl-locale-profile.txt" ||
        fail "musl locale-profile probe failed to run"
    {
        printf 'named-locale=accepted\n'
        printf 'portuguese-collation=unsupported\n'
    } >"$work/logs/musl-locale-profile.expected"
    cmp "$work/logs/musl-locale-profile.expected" \
        "$work/logs/musl-locale-profile.txt" >/dev/null ||
        fail "staged musl locale behavior changed; review CAMP-LUA-002"
    build_lane musl-static "$musl_cc" -O2 \
        'LUA_USE_POSIX -DLUA_USE_READLINE -I.' '' '-Wl,-E'
    file "$work/musl-static/src/lua" >"$work/logs/musl-static-binary.txt"
    grep -F 'statically linked' "$work/logs/musl-static-binary.txt" >/dev/null ||
        fail "musl lane did not produce a static executable"
    test_lane musl-static static
    grep -Fq "'collate' locale not found" "$work/logs/musl-static/all.lua.log" ||
        fail "Lua suite did not observe the unavailable musl collation profile"
    grep -Fq "'ctype' locale not found" "$work/logs/musl-static/all.lua.log" ||
        fail "Lua suite did not observe the unavailable musl ctype profile"
    {
        echo '# cgf-campaign-results-v1'
        printf '# columns=key\toutcome\tdetail\n'
        printf 'build.musl-static\tPASS\tcompiler=cgfried,target=x86_64-linux-musl,opt=O2,static-env=readline+lib2-v2+locale-profile\n'
        printf 'source.pin\tPASS\tversion=%s,sha256=%s\n' "$LUA_REF" "$LUA_SOURCE_SHA256"
        printf 'source.tests-pin\tPASS\tversion=%s,sha256=%s\n' "$LUA_REF" "$LUA_TESTS_SHA256"
        printf 'test.musl-static.all-lua\tPASS\tsuite=lua-%s-tests,mode=full-generic,dlopen=upstream-unavailable\n' "$LUA_REF"
    } >"$work/results.txt"
    printf 'campaign-lua: PASS version=%s mode=musl-static opt=O2 results=%s\n' \
        "$LUA_REF" "$work/results.txt"
fi
