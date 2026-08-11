#!/bin/sh
set -eu
printf '%s\n' "$*" >>"${FIXTURE_SCHEDULER_LOG:?}"
