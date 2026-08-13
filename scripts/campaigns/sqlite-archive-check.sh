#!/bin/sh
set -eu

fail() {
    echo "sqlite-archive: $*" >&2
    exit 1
}

[ "$#" -eq 2 ] || fail "usage: $0 ZIP EXPECTED-ROOT"
archive=$1
expected_root=$2
[ -r "$archive" ] || fail "archive is not readable: $archive"
case $expected_root in '' | /* | *..* | *\\*) fail "unsafe expected root: $expected_root" ;; esac
command -v unzip >/dev/null 2>&1 || fail "unzip is required"

members=$(mktemp "${TMPDIR:-/tmp}/cgf-sqlite-members.XXXXXX")
trap 'rm -f "$members"' EXIT HUP INT TERM
unzip -Z1 "$archive" >"$members" || fail "cannot list archive: $archive"
[ -s "$members" ] || fail "archive has no members: $archive"
awk -v root="$expected_root/" '
    {
        original = $0
        if (index(original, "\\") != 0 || substr(original, 1, 1) == "/" ||
            index(original, root) != 1) {
            bad = 1
            print "unsafe member: " original > "/dev/stderr"
            next
        }
        name = original
        sub(/\/$/, "", name)
        count = split(name, component, "/")
        for (i = 1; i <= count; i++) {
            if (component[i] == "" || component[i] == "." ||
                component[i] == "..") {
                bad = 1
                print "unsafe member: " original > "/dev/stderr"
                break
            }
        }
    }
    END { exit bad }
' "$members" || fail "archive contains a member outside $expected_root"
unzip -Z -l "$archive" | awk '
    /^[bcdlps-][rwxStTs-][rwxStTs-][rwxStTs-][rwxStTs-][rwxStTs-][rwxStTs-][rwxStTs-][rwxStTs-][rwxStTs-][[:space:]]/ {
        entries++
        type = substr($1, 1, 1)
        if (type != "-" && type != "d") bad = 1
    }
    END { exit bad || entries == 0 }
' || fail "archive contains a link or special-file member"
printf 'sqlite-archive: PASS members=%s root=%s\n' \
    "$(wc -l <"$members" | tr -d ' ')" "$expected_root"
