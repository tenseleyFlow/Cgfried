# afs-ld ELF lane — extend-upstream debt ledger (Sprint 27)

Five named gaps in afs-ld's x86_64 ELF arm, verified against its
`src/main.rs` / `src/elf.rs` (x16 arc, rungs 1-2). Rule: file upstream,
never work around in cgfried. Each row keeps its stable ID until the
upstream rung lands and the submodule bump retires it.

| ID | Gap | Effect on cgfried | Upstream disposition |
|---|---|---|---|
| LD-ELF-001 | `R_X86_64_COPY` synthesis missing | dynamic links touching data imports (`stdout`, `stderr`, `environ`) hard-error: "direct reference to data import '…' needs a COPY relocation (later rung)". Driver maps to the retry-with--static hint. | file: COPY reloc synthesis rung |
| LD-ELF-002 | ET_DYN / PIE executable output | `-pie` unsupported; blocks Sprint 51's PIE flip under CGF_LD=1 | file: PIE rung |
| LD-ELF-003 | shared-object output + DT_SONAME | `-shared` under CGF_LD=1 impossible; blocks Sprint 51 via afs-ld | file: dylib-equivalent ELF rung |
| LD-ELF-004 | `.gnu.warning.*` sections not surfaced | static NSS-class links miss the glibc runtime warning gcc-lane users see | file: warning-section surfacing |
| LD-ELF-005 | reloc types beyond rung-2 scope | objects using exotic relocs fail with its own named diagnostic | file: as encountered, cite the diagnostic |

Status 2026-07-31: IDs reserved; upstream issues to be filed on the
FortranGoingOnForty/afs-ld tracker with these IDs in the titles.

## Fixed upstream instead of filed (Sprint 27)

Two gaps surfaced by the CGF_LD=1 static lane were small enough to fix
at the source rather than work around — the standing rule. Both are
merged; the submodule pin carries them.

| Gap | Where it bit | Upstream |
|---|---|---|
| `.eh_frame` CIE augmentation `'S'` (signal frame) rejected | glibc's `libc.a` ships one `"zRS"` CIE — EVERY static link died at the merge | PR #17, merged |
| `R_X86_64_GOTTPOFF` against an undefined WEAK symbol hard-errored | Ubuntu's `libc.a(setlocale.o)` takes the IE path where Arch's does not; the local-exec path already resolved the same case to zero | PR #18, merged |

The second is why this lane is distro-differential in nature: Arch and
Ubuntu glibc take different paths through the same archive. Podman
verification (ubuntu:24.04, glibc 2.39) is the cheap way to check a
static-link change before CI does.

## LD-ELF-006 — debug sections in third-party input objects

afs-ld applies relocations to `.debug_*` sections of its inputs and rejects
Alpine musl's crt objects and `libc.a`:

```
afs-ld: error: crt1.o: section '.debug_info' relocation 17 has invalid
        r_offset 0xe2: 4-byte write exceeds section size 0xc7
```

`readelf -S` shows that section carrying flag `C` — `SHF_COMPRESSED`. The
size is the COMPRESSED one while relocation offsets name the uncompressed
image, so every offset past the compressed length looks out of range. This is
the same fact Sprint 29 recorded when it started passing
`--nocompress-debug-sections` to gas for OUR objects; the difference is that
a prebuilt third-party object cannot be re-assembled.

Isolated exactly: `strip --strip-debug` on the crt objects and `libc.a` makes
afs-ld link a static musl binary that runs. Nothing else in the chain needed
changing, so the linking itself is sound.

Either fix works upstream, and skipping is the cheaper one:

1. **Skip non-alloc sections** that the output will not carry. A static link
   does not need `.debug_info` from `crt1.o` to be correct; worst case the
   product loses that object's debug info, which is what `--strip-debug`
   already implies.
2. Decode `SHF_COMPRESSED` on input.

Until then `scripts/musl_cross_lane.sh` links its afs-ld leg against a
debug-stripped copy of the sysroot, so the lane stays honest about what
afs-ld can and cannot do rather than skipping it.

## AS-SET-001 — afs-as has no `.set`

Filed by the `alias` attribute (Sprint 55). `alias` emits

    .globl  alias_fn
    .set    alias_fn,real_fn

and afs-as rejects `.set` with "unsupported directive — the x86 dialect grows
only with corpus evidence". This IS that evidence: `alias` is musl's
`weak_alias` idiom, so the campaign needs it.

Both targets are affected — the arm64 lane assembles through afs-as too, which
is why `tests/programs/gnu/attr_alias.c` carries `ENV: CGF_AS=0` and lives
outside `tests/corpus`. arm64 EXECUTION coverage for aliases waits on this row.

The driver refuses the afs-as path by name rather than letting the assembler
reject correct assembly and reporting it as a cgf emission bug — the same
treatment TLS-004 already has, and for the same reason: the error must name
the component that is actually missing something.

What it needs: `.set NAME, EXPR` assigning a symbol the value, section, type
and binding of its target. gas resolves a `.set` whose target appears later in
the file, so the directive cannot assume the target is already defined.
