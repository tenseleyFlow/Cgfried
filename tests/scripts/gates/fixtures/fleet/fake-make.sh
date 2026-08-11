#!/bin/sh
set -eu
printf '%s\n' "$*" >"${FIXTURE_MAKE_LOG:?}"
