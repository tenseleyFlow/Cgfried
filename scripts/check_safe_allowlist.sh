#!/bin/sh
set -eu
LC_ALL=C
export LC_ALL

current=${1:-ci/safe-mode-allowlist.txt}
baseline=${2:-ci/safe-mode-allowlist.baseline.txt}

check_format()
{
    file=$1
    awk '
        /^[[:space:]]*(#|$)/ { next }
        NF < 3 || $1 !~ /^[^[:space:]:]+:[A-Za-z_][A-Za-z0-9_]*$/ {
            print FILENAME ":" NR ": malformed allowlist entry" > "/dev/stderr"
            bad = 1
        }
        { if ($1 !~ /^[[:space:]]*(#|$)/) count[$1]++ }
        END {
            for (key in count)
                if (count[key] > 1) {
                    print FILENAME ": duplicate key " key > "/dev/stderr"
                    bad = 1
                }
            exit bad
        }
    ' "$file"
}

check_format "$current"
check_format "$baseline"

current_keys=$(sed -n 's/^[[:space:]]*\([^#[:space:]][^[:space:]]*\).*/\1/p' \
    "$current" | sort -u)
baseline_keys=$(sed -n 's/^[[:space:]]*\([^#[:space:]][^[:space:]]*\).*/\1/p' \
    "$baseline" | sort -u)

for key in $current_keys; do
    case "
$baseline_keys
" in
    *"
$key
"*) ;;
    *)
        echo "safe allowlist grew with unreviewed key: $key" >&2
        exit 1
        ;;
    esac
done

current_count=$(printf '%s\n' "$current_keys" | sed '/^$/d' | wc -l)
baseline_count=$(printf '%s\n' "$baseline_keys" | sed '/^$/d' | wc -l)
if [ "$current_count" -gt "$baseline_count" ]; then
    echo "safe allowlist grew: $current_count > $baseline_count" >&2
    exit 1
fi

echo "safe allowlist: $current_count active / $baseline_count reviewed entries"
