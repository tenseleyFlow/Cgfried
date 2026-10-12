# Cgfried

Cgfried is a self-hosting C compiler written in C11. It defaults to ISO C17,
has x86-64 and ARM64 backends, and emits ELF or Mach-O for a closed set of
five targets. The compiler, test runner, runtime, and release tooling are kept
dependency-light and build with a C11 compiler, GNU Make, and POSIX tools.

The project emphasizes deterministic output, precise diagnostics, explicit
target behavior, and an optional memory-safety ladder. It includes a
freestanding header set and `libcgf_rt`, and can use either system binutils or
the companion `afs-as` and `afs-ld` submodules.

Version 0.1.0 is a deliberately bounded release, not a drop-in GCC
replacement. C23, `_Complex`, `threads.h`, general-dynamic TLS, full DWARF,
LTO, PGO, and `asm goto` remain outside this release and are rejected rather
than silently miscompiled. Target feature macros advertise the implemented
surface, including the applicable `__STDC_NO_*` macros.

## Install

```sh
git clone --recurse-submodules https://github.com/tenseleyFlow/Cgfried.git
cd Cgfried
make
sudo make install
```

`PREFIX` defaults to `/usr/local`; packaging can use, for example,
`make install PREFIX=/usr DESTDIR="$pkgdir"`. The C compiler itself does not
require Rust. Building the optional bundled assembler and linker with
`make tools` additionally requires Cargo.

## Quick start

```sh
CGF_AS=0 cgfried hello.c -o hello        # compile and link
CGF_AS=0 cgfried -c one.c two.c          # compile multiple translation units
cgfried -E -P config.h                    # preprocess
cgfried --target=arm64-linux -S hello.c  # cross-compile to assembly
CGF_AS=0 cgfried -fsafe app.c -o app     # opt into the safe-TU policy
```

`CGF_AS=0` selects the system assembler. Use the bundled assembler instead by
building `make tools`; explicit `CGF_AS_PATH` and `CGF_LD_PATH` settings take
precedence over both modes. Run `cgfried --help` and
`cgfried --help=warnings` for the complete command-line surface.

## Target status

| Target | Status | Caveats |
|---|---|---|
| `x86_64-linux-gnu` | supported, CI-gated | Native GNU userland is the primary hosted lane; cross use needs a matching sysroot and tools. |
| `arm64-linux` | supported, CI-gated | Cross-linked programs need an AArch64 sysroot; `long double` is IEEE binary128 and uses the target `libcgf_rt`. |
| `arm64-macos` | supported, CI-gated | Requires a macOS 11+ SDK; Apple varargs follow the native array carrier; static linking is rejected and produced executables are ad-hoc signed. |
| `x86_64-linux-musl` | supported, CI-gated | The release gate covers the static musl lane; cross use needs the matching musl sysroot and start files. |
| `x86_64-freebsd` | built + tested in CI | PR CI covers smoke tests and the full corpus runs in the FreeBSD nightly VM; non-native use needs a FreeBSD sysroot. |

All five target spellings are closed and exact. `-dumpmachine` reports the
selected target, and `--target=<triple>` rejects unknown triples.

## Memory safety

`-fcgf-safe` instruments heap accesses that static analysis cannot discharge;
`-fsafe` adds the safe-translation-unit policy, memory warnings as errors,
zero initialization, and unsafe-construct rejection. The bounded guarantees,
escape rules, interoperability marker, and limitations are specified in
[the safe-mode contract](doc/safe-mode.md), with the analysis details in
[the memory-safety guide](doc/memsafe.md).

## Compatibility and performance scope

Cgfried's optimizer is tested against the project's GCC 8-class `-O2`
equivalence matrices and named kernel/corpus gates. Its `-Wall -Wextra`
behavior is matched to the committed warning differential matrix. Those are
testable corpus claims, not claims of complete GCC option, extension, ABI, or
diagnostic compatibility. Current performance policy and frozen measurements
are documented in [the performance gates](doc/perf-gates.md) and
`.benchmarks/report-0.1.0.md`.

## Reproducible source archive

`make dist` creates `cgfried-0.1.0.tar.gz` from committed Git objects and the
two pinned submodule commits. Release maintainers need GNU gzip because Apple
gzip produces a different deflate stream for the same byte-identical tar
payload; on macOS, install it with `brew install gzip`. `CGF_DIST_GZIP` can
name an alternate GNU gzip binary. The release gate builds the archive on
x86-64 Linux, ARM64 Linux, and ARM64 macOS and compares all three products
byte-for-byte.

## License

GPL-3.0-only. See [LICENSE](LICENSE).
