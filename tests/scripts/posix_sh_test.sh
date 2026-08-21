#!/bin/sh
# The shell-policy gate must distinguish shell syntax from POSIX awk programs
# embedded in shell strings.
set -eu

LC_ALL=C
export LC_ALL

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
gate=$root/scripts/check_posix_sh.sh
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cgf-posix-sh-test.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

mkdir -p "$tmp/scripts"
cat >"$tmp/scripts/valid-awk.sh" <<'EOF'
#!/bin/sh
awk '
function trim(value) {
    sub(/^[[:space:]]+/, "", value)
    return value
}
{ print trim($0) }
'
EOF

(cd "$tmp" && sh "$gate") >"$tmp/valid.out" 2>"$tmp/valid.err"
grep -Fq 'all harness scripts parse under a POSIX shell' "$tmp/valid.out"

cat >"$tmp/scripts/invalid-function.sh" <<'EOF'
#!/bin/sh
function invalid() { :; }
EOF
if (cd "$tmp" && sh "$gate") >"$tmp/function.out" 2>"$tmp/function.err"; then
    echo 'posix-sh-meta: shell function syntax unexpectedly passed' >&2
    exit 1
fi
grep -Fq 'is not POSIX-parseable' "$tmp/function.err"

mv "$tmp/scripts/invalid-function.sh" "$tmp/invalid-function.fixture"
printf '%s\n' '#!/bin/sh' >"$tmp/scripts/invalid-substitution.sh"
# Split the planted spelling so the production gate can scan this meta-test
# without mistaking its fixture generator for a live bashism.
printf '%s%s\n' 'value=${value' '/foo/bar}' \
    >>"$tmp/scripts/invalid-substitution.sh"
if (cd "$tmp" && sh "$gate") >"$tmp/substitution.out" 2>"$tmp/substitution.err"; then
    echo 'posix-sh-meta: shell substitution bashism unexpectedly passed' >&2
    exit 1
fi
grep -Fq 'uses a bashism' "$tmp/substitution.err"

echo 'posix-sh-meta: PASS'
