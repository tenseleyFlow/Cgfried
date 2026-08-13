#!/bin/sh
set -eu

root=$(CDPATH='' cd "$(dirname "$0")/../.." && pwd -P)
descriptor=$root/ci/campaigns/lua.mk
runner=$root/scripts/campaigns/lua.sh
expected=$root/ci/campaigns/lua.expected
musl_expected=$root/ci/campaigns/lua-musl.expected

grep -F 'LUA_REF := 5.4.7' "$descriptor" >/dev/null
grep -F 'LUA_SOURCE_SHA256 := 9fbf5e28ef86c69858f6d3d34eccc32e911c1a28b4120ff3e84aaa70cfbf1e30' "$descriptor" >/dev/null
grep -F 'LUA_TESTS_SHA256 := 8a4898ffe4c7613c8009327a0722db7a41ef861d526c77c5b46114e59ebf811e' "$descriptor" >/dev/null
grep -F 'generic CC="$cc" MYCFLAGS="$opt -D$feature_macro"' "$runner" >/dev/null
grep -F 'timeout "$timeout_seconds" ../src/lua all.lua' "$runner" >/dev/null
if grep -F '_port=true' "$runner" >/dev/null; then
    echo 'campaign-lua-meta: reduced upstream suite invocation is forbidden' >&2
    exit 1
fi
grep -F 'audit_archive "$source_archive"' "$runner" >/dev/null
grep -F 'audit_archive "$tests_archive"' "$runner" >/dev/null
grep -F -- '-Wl,--wrap=luaL_openlibs' "$runner" >/dev/null
grep -F -- '-Wl,--wrap=setlocale' "$runner" >/dev/null
grep -F 'CAMP-LUA-002' "$runner" >/dev/null
grep -F 'named-locale=accepted' "$runner" >/dev/null
grep -F 'portuguese-collation=unsupported' "$runner" >/dev/null
grep -F 'static-env=readline+lib2-v2+locale-profile' "$musl_expected" >/dev/null
grep -F 'final OK !!!' "$runner" >/dev/null
grep -F 'test.native-o0.all-lua' "$expected" >/dev/null
grep -F 'test.native-o2.all-lua' "$expected" >/dev/null
grep -F 'host.architecture' "$expected" >/dev/null
grep -F 'test.musl-static.all-lua' "$musl_expected" >/dev/null
tab=$(printf '\t')
if grep -F "${tab}SKIP${tab}" "$expected" "$musl_expected" >/dev/null; then
    echo 'campaign-lua-meta: committed Lua expectations must not skip lanes' >&2
    exit 1
fi

printf 'campaign-lua-meta: PASS\n'
