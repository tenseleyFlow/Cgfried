#!/bin/sh
set -eu
: "${FIXTURE_GIT_LOG:?}"
printf '%s\n' "$*" >>"$FIXTURE_GIT_LOG"
if [ "${1:-}" = -C ]; then
    shift 2
fi
while [ "${1:-}" = -c ]; do
    [ "$#" -ge 2 ] || exit 2
    shift 2
done
command_name=${1:-}
shift || true
case $command_name in
status | switch | pull | submodule | config | rebase) exit 0 ;;
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
clone) exit 2 ;;
*) exit 2 ;;
esac
