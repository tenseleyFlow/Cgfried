#!/bin/sh
set -eu
: "${FIXTURE_GIT_LOG:?}"
printf '%s\n' "$*" >>"$FIXTURE_GIT_LOG"
git_dir=
if [ "${1:-}" = -C ]; then
    git_dir=$2
    shift 2
fi
while [ "${1:-}" = -c ]; do
    [ "$#" -ge 2 ] || exit 2
    shift 2
done
command_name=${1:-}
shift || true
case $command_name in
status)
    case $git_dir in
    */build/fleet-refs/musl-*) printf '%s' "${FIXTURE_MUSL_CACHE_STATUS:-}" ;;
    *) printf '%s' "${FIXTURE_GIT_STATUS:-}" ;;
    esac
    exit 0
    ;;
switch | pull | submodule | config | rebase) exit 0 ;;
add)
    [ "${1:-}" = -- ] && shift
    printf '%s\n' "$@" >"${FIXTURE_GIT_STAGED:?}"
    ;;
diff)
    sort "${FIXTURE_GIT_STAGED:?}"
    ;;
commit) exit 0 ;;
push)
    if [ "${FIXTURE_PUSH_FAIL_ONCE:-0}" = 1 ] &&
       [ ! -e "${FIXTURE_PUSH_STATE:?}" ]; then
        : >"$FIXTURE_PUSH_STATE"
        exit 1
    fi
    ;;
clone)
    [ "${FIXTURE_ALLOW_MUSL_CLONE:-0}" = 1 ] || exit 2
    destination=
    for argument in "$@"; do destination=$argument; done
    [ -n "$destination" ] || exit 2
    mkdir -p "$destination"
    [ "${FIXTURE_MUSL_CLONE_STATUS:-0}" -eq 0 ] || exit "${FIXTURE_MUSL_CLONE_STATUS}"
    mkdir -p "$destination/.git"
    ;;
cat-file) [ "${FIXTURE_MUSL_CACHE_HAS_PIN:-1}" = 1 ] ;;
fetch) exit "${FIXTURE_MUSL_CACHE_FETCH_STATUS:-0}" ;;
checkout) exit "${FIXTURE_MUSL_CACHE_CHECKOUT_STATUS:-0}" ;;
rev-parse)
    printf '%s\n' "${FIXTURE_MUSL_CACHE_REVISION:-b306b16af15c89a04d8e0c55cac2dadbeb39c083}"
    ;;
*) exit 2 ;;
esac
