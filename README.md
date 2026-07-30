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
make install    # PREFIX=/usr/local by default
```

## Status

Early scaffolding. The driver answers `--version`/`--help`; compilation of C
sources is under active development. See `.docs/sprints/` (local) for the
roadmap.

## License

GPL-3.0-only. See LICENSE.
