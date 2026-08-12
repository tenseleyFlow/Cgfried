#!/bin/sh
set -eu

case $#:${1:-} in
0:) action=import ;;
1:--verify) action=--verify ;;
*) exit 2 ;;
esac
echo "ctestsuite|$action" >>"$FAKE_IMPORT_LOG"
echo "ctestsuite-policy|${CGF_CTESTSUITE_POLICY:-}" >>"$FAKE_IMPORT_LOG"
[ "${FAKE_IMPORT_FAIL:-}" != ctestsuite ] || exit 23
