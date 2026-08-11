#!/bin/sh
case ${1:-} in
-s) echo "${FIXTURE_SYSTEM:-Linux}" ;;
-m) echo "${FIXTURE_MACHINE:-x86_64}" ;;
-n) echo "${FIXTURE_HOST:-fixture-host}" ;;
*) echo "${FIXTURE_SYSTEM:-Linux}" ;;
esac
