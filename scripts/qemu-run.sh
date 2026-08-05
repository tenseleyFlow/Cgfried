#!/bin/sh
# Sprint 49 DoD 7: run an arm64-linux binary on a non-arm64 host.
#
# Wraps qemu-user with the cross sysroot the dynamic loader needs. Nothing
# here is arm64-specific beyond the emulator and sysroot names: it is the one
# place that knows how to execute a foreign binary, so the test harness and
# the corpus lanes can stay target-agnostic.
#
# On an arm64 host this is a no-op passthrough, which is what lets the same
# lane script serve the native runner and the cross host without branching.
#
# Overrides, in the Sprint 2 spirit that every tool is routable:
#   CGF_QEMU          emulator binary (default: probed)
#   CGF_QEMU_SYSROOT  -L sysroot      (default: probed)
#
# Exit status is the emulated program's own, so `// EXIT_CODE:` assertions
# work through the wrapper unchanged. A missing emulator exits 125 -- distinct
# from any exit code a corpus program produces -- so "could not run" can never
# be mistaken for "ran and failed".
set -u

host=$(uname -m 2>/dev/null || echo unknown)

if [ "$#" -eq 0 ]; then
    echo "usage: qemu-run.sh <binary> [args...]" >&2
    exit 125
fi

# Native: exec directly. qemu-user on a matching arch would work too, but it
# is slower and its signal and thread behaviour is not the real thing.
case "$host" in
aarch64 | arm64)
    exec "$@"
    ;;
esac

qemu=${CGF_QEMU:-}
if [ -z "$qemu" ]; then
    for candidate in qemu-aarch64-static qemu-aarch64; do
        if command -v "$candidate" >/dev/null 2>&1; then
            qemu=$candidate
            break
        fi
    done
fi
if [ -z "$qemu" ] || ! command -v "$qemu" >/dev/null 2>&1; then
    echo "qemu-run: no aarch64 emulator (set CGF_QEMU)" >&2
    exit 125
fi

# The sysroot supplies ld-linux-aarch64.so.1 and the shared libc. Arch puts it
# at /usr/aarch64-linux-gnu; Debian and Ubuntu at /usr/aarch64-linux-gnu too,
# with the libraries under a multiarch subdirectory the loader finds itself.
sysroot=${CGF_QEMU_SYSROOT:-}
if [ -z "$sysroot" ]; then
    for candidate in /usr/aarch64-linux-gnu /usr/aarch64-linux-gnu/sys-root; do
        if [ -d "$candidate" ]; then
            sysroot=$candidate
            break
        fi
    done
fi
if [ -z "$sysroot" ] || [ ! -d "$sysroot" ]; then
    echo "qemu-run: no aarch64 sysroot (set CGF_QEMU_SYSROOT)" >&2
    exit 125
fi

exec "$qemu" -L "$sysroot" "$@"
