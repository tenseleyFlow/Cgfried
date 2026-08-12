#!/bin/sh
set -eu

case $#:${1:-} in
0:) action=import ;;
1:--verify) action=--verify ;;
*) exit 2 ;;
esac
echo "torture|$action" >>"$FAKE_IMPORT_LOG"
echo "torture-policy|${CGF_TORTURE_POLICY:-}" >>"$FAKE_IMPORT_LOG"
[ "${FAKE_IMPORT_FAIL:-}" != torture ] || exit 23
