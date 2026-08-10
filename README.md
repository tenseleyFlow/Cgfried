# Cgfried

A standards-first, performance-first, warnings-first, tests-first C17
compiler with bespoke tooling, targeting x86_64 and ARM64 (ELF and Mach-O).
Sister project to [ARMFORTAS](https://github.com/FortranGoingOnForty/armfortas)
and its assembler/linker, afs-as and afs-ld.

## Building

Requires any C11 compiler, GNU make, and a POSIX environment.

```sh
make            # builds build/cgfried (and the short alias build/cgf)
make test       # unit tests + driver smoke checks
make bench      # reproducible compile-speed/RSS protocol
make install    # PREFIX=/usr/local by default
```

`-Ofast` and `-ffast-math` use one deliberately bundled policy. Its exact
licenses, compatibility warnings, and IEC 60559 macro contract are documented
in [docs/fast-math.md](docs/fast-math.md).

Warning flag precedence, promotion, diagnostic pragmas, and system-header
suppression are documented in [docs/warnings.md](docs/warnings.md).

Compile-speed/RSS measurement, profiling, and baseline policy are documented
in [docs/performance.md](docs/performance.md).

## Toolchain

The bundled assembler ([afs-as](https://github.com/FortranGoingOnForty/afs-as))
and linker ([afs-ld](https://github.com/FortranGoingOnForty/afs-ld)) are git
submodules built with `make tools`. **Rust/cargo is a build-time dependency
of the bundled assembler/linker only. The compiler is C11 + POSIX; it builds
and runs without a Rust toolchain, using system `as`/`ld`.**

Routing (resolution order per tool: explicit path var > mode var > default;
empty-string values are treated as unset):

| Variable | Values | Effect |
|---|---|---|
| `CGF_AS` | unset / `1` (default) | use bundled afs-as |
| | `0` | use system `as` from PATH |
| `CGF_AS_PATH` | `<bin>` | use exactly this assembler (wins over `CGF_AS`) |
| `CGF_LD` | unset / `0` (default) | use system `ld` from PATH |
| | `1` | use afs-ld (not yet supported: Sprint 27) |
| `CGF_LD_PATH` | `<bin>` | use exactly this linker (wins over `CGF_LD`) |
| `CGF_CRT_DIR` | `<dir>` | crt object discovery override |

Submodule bump ritual (in this order — a parent pin pointing at an unpushed
commit breaks every fresh clone and CI):

1. Commit inside the submodule, on its branch.
2. **Push the submodule first.**
3. `git add afs-as && git commit` in the parent to bump the pin
   (`bump afs-as: <what changed>`).

## Status

Sprints 0–52 and Sprint 55 are complete. C17 preprocessing, parsing, semantic
analysis, optimization, x86-64/ARM64 code generation, ELF/Mach-O targets,
warnings, and the memory-safety ladder are implemented. See `.docs/sprints/`
(local) for the remaining roadmap to v0.1.0.

## License

GPL-3.0-only. See LICENSE.
