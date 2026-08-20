#!/bin/sh
# XFAIL(audit): DET-M-03 four baseline-bump commits omit required old-to-new evidence
# Reproducer contract: 0 = baseline defect reproduced, 1 = remediated/XPASS,
# 2 = malformed checkout or unavailable required tool.
set -u
LC_ALL=C
export LC_ALL

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 2
ROOT=${1:-$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)}

command -v git >/dev/null 2>&1 || exit 2
git -C "$ROOT" rev-parse --git-dir >/dev/null 2>&1 || exit 2

bad=0

# Positive control: the sole compliant replacement states the workload reason
# and all three accepted old-to-new RSS values.
body=$(git -C "$ROOT" show -s --format='%B' \
    7aef30e886eef61714820f6d6fbc02b5f5e76b04) || exit 2
printf '%s\n' "$body" | grep -Fq 'self workload grew from 105 to 106 files' || exit 2
printf '%s\n' "$body" | grep -Fq 'sqlite3 499924 to 500092 KiB' || exit 2
printf '%s\n' "$body" | grep -Fq 'self 1059216 to 1064708 KiB' || exit 2
printf '%s\n' "$body" | grep -Fq 'many-tu 1377672 to 1378504 KiB' || exit 2

# Initial native replacements: subject only, neither old-to-new values nor why.
body=$(git -C "$ROOT" show -s --format='%B' \
    4be79b6e4cd2a35850cdde3e3fc7e6ed34024b7c) || exit 2
[ "$(printf '%s\n' "$body" | sed '/^$/d' | wc -l)" -eq 1 ] && bad=$((bad + 1))

# Controlled Kasumi replacement: why is present, but no old-to-new value.
body=$(git -C "$ROOT" show -s --format='%B' \
    b87a278984ee32b2ef29db452caa9a7ebea6fa85) || exit 2
if ! printf '%s\n' "$body" | grep -Eq '([0-9][[:space:]]*(->|to)[[:space:]]*[0-9])'; then
    bad=$((bad + 1))
fi

# Hasu and Nomad state compile old-to-new values, but replace an entire
# runtime baseline with only an unquantified "stable/improve" assertion.
for commit in \
    787b9cec3619c2ade234e37372448d9730e25079 \
    9c50e6d1ecd0a9ac57067eacec8b3d0be05e401
do
    body=$(git -C "$ROOT" show -s --format='%B' "$commit") || exit 2
    printf '%s\n' "$body" |
        grep -Eq '(runtime|kernel) medians remain stable( or improve)?' || exit 2
    bad=$((bad + 1))
done

if [ "$bad" -eq 0 ]; then
    exit 1
fi
[ "$bad" -eq 4 ] || exit 2

echo 'DET-M-03 reproduced: 4/5 commits that modified an existing baseline omit complete old-to-new metric evidence'
exit 0
