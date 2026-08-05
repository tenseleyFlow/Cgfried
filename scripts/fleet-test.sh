#!/bin/sh
# Sprint 49 DoD 7: run the suite on real hardware across the tailnet.
#
# qemu-user is an emulator, not a CPU. It does not reproduce weak memory
# ordering, its thread scheduling is far tamer than a real core's, and it
# implements the architecture as documented rather than as any particular
# part behaves. Anything that depends on those -- the ll/sc atomics from D6
# above all -- is only really tested on hardware, which is what this is for.
#
#   scripts/fleet-test.sh arm64-linux
#
# The ritual is deliberately explicit about finding NOTHING. A fleet with no
# matching hosts prints a counted HARNESS_SKIP, never silence: "the atomics
# hammer did not run" and "the atomics hammer passed" must never look alike.
set -u
LC_ALL=C
export LC_ALL

TARGET=${1:-arm64-linux}
TAILSCALE=${CGF_TAILSCALE:-tailscale}
REMOTE_DIR=${CGF_FLEET_DIR:-/tmp/cgfried-fleet}
WORK=${CGF_FLEET_WORK:-build/fleet}
SSH=${CGF_FLEET_SSH:-ssh}
RSYNC=${CGF_FLEET_RSYNC:-rsync}

case "$TARGET" in
arm64-linux)
    want_machine="aarch64"
    want_kernel="Linux"
    ;;
x86_64-linux)
    want_machine="x86_64"
    want_kernel="Linux"
    ;;
*)
    echo "fleet-test: unknown target '$TARGET'" >&2
    echo "fleet-test: known targets: arm64-linux, x86_64-linux" >&2
    exit 2
    ;;
esac

skip() {
    # The runner's skip grammar, so ci/check_skips.sh can hold this to an
    # exact expected count exactly like every other lane.
    echo "HARNESS_SKIP suite=fleet test=$TARGET count=1 reason=\"$1\""
    exit 0
}

command -v "$TAILSCALE" >/dev/null 2>&1 || skip "tailscale not installed"
command -v "$SSH" >/dev/null 2>&1 || skip "ssh not installed"
command -v "$RSYNC" >/dev/null 2>&1 || skip "rsync not installed"

mkdir -p "$WORK"

if ! "$TAILSCALE" status >"$WORK/status.txt" 2>"$WORK/status.err"; then
    skip "tailscale status failed: $(head -1 "$WORK/status.err")"
fi

# `tailscale status` lists one host per line, name in the second field. The
# machine type is NOT in that output, so each candidate is probed over ssh --
# a name is not evidence of an architecture.
hosts=$(awk '$2 != "" && $1 !~ /^#/ { print $2 }' "$WORK/status.txt" | sort -u)
[ -n "$hosts" ] || skip "tailnet has no hosts"

matched=""
for host in $hosts; do
    probe=$("$SSH" -o BatchMode=yes -o ConnectTimeout=5 "$host" \
        'uname -s; uname -m' 2>/dev/null) || continue
    kernel=$(echo "$probe" | sed -n 1p)
    machine=$(echo "$probe" | sed -n 2p)
    if [ "$kernel" = "$want_kernel" ] && [ "$machine" = "$want_machine" ]; then
        matched="$matched $host"
    fi
done

[ -n "$matched" ] || skip "no reachable $TARGET host on the tailnet"

fails=0
ran=0
for host in $matched; do
    echo "fleet-test: $host ($TARGET)"
    "$SSH" -o BatchMode=yes "$host" "mkdir -p '$REMOTE_DIR'" || {
        echo "fleet-test: $host: cannot create $REMOTE_DIR" >&2
        fails=$((fails + 1))
        continue
    }
    # --delete so a stale object from an older tree cannot link into a
    # result; the build directory is excluded because it is host-specific.
    if ! "$RSYNC" -az --delete \
        --exclude 'build*/' --exclude '.git/' --exclude 'target/' \
        ./ "$host:$REMOTE_DIR/" >"$WORK/$host.rsync.log" 2>&1; then
        echo "fleet-test: $host: rsync failed" >&2
        tail -5 "$WORK/$host.rsync.log" >&2
        fails=$((fails + 1))
        continue
    fi
    if "$SSH" -o BatchMode=yes "$host" \
        "cd '$REMOTE_DIR' && make -j\"\$(nproc)\" test" \
        >"$WORK/$host.test.log" 2>&1; then
        echo "fleet-test: $host: PASS"
    else
        echo "fleet-test: $host: FAIL (log: $WORK/$host.test.log)" >&2
        tail -20 "$WORK/$host.test.log" >&2
        fails=$((fails + 1))
    fi
    ran=$((ran + 1))
done

if [ "$fails" -ne 0 ]; then
    echo "fleet-test: $fails of $ran $TARGET host(s) failed" >&2
    exit 1
fi
echo "fleet-test: $ran $TARGET host(s) green"
