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

## AS-SET-001 — afs-as had no `.set` (x86) — CLOSED

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

CLOSED by upstream PR #28 (merged `fb50d2f`) for the **x86** path. Three things
the gas differential caught while landing it, worth keeping because each is a
way to be wrong while looking right:

- the alias must also be a known LOCAL label before relocations resolve, or a
  call to it keeps a symbol relocation where gas folds the section symbol --
  the object still links and RUNS correctly, so only the relocation list shows
  it;
- gas gives the alias the target's SIZE, not zero;
- but not by copying the size EXPRESSION: `.size f, .-f` is measured from its
  own directive's position and an alias has none. A 5-case synthetic suite
  using a constant `.size` passed while real compiler output failed.

## AS-SET-002 — afs-as has no symbol `.set` on the arm64 path — **CLOSED**

Closed by upstream PR #29 (merged `a6690e2`, submodule bumped). The arm64 and
Mach-O paths are a different parser and assembler from x86, and there `.set`
meant an ABSOLUTE assignment only, so `.set alias, real` was rejected with
"absolute symbol 'alias' must resolve to an absolute value". PR #28 had given
the x86 dialect aliases; #29 is the same semantics for the other pipeline.

The driver's by-name refusal is gone and `tests/programs/gnu/attr_alias.c`
moved to `tests/corpus/x86_64/int/`, so the arm64 lane executes it: 63/63 and
63/63 under `CGF_SPILL_ALL=1`.

Four things worth keeping, all found by measuring gas rather than reading:

1. **The parser cannot decide which form a `.set` is.** It runs before any
   label exists, so a lone-symbol right-hand side is ambiguous between an
   alias and an absolute chain. A non-absolute result is now DEFERRED and the
   assembler decides where `self.labels` is final. `.set A, 5` / `.set B, A`
   must still give B the value 5, and that boundary has its own test.
2. **THE CURSOR TRAP, confirmed.** An alias entry stays in
   `absolute_assignments` and is MARKED rather than removed:
   `activate_absolute_definition` walks that vector with a cursor, one step
   per `.set` in the emission pass, so partitioning it desynchronizes the
   cursor and fails with "missing resolved absolute assignment".
3. **Type and SIZE inherit at emission, not at placement.** `.size real,
   .-real` is measured from its own directive's position and an alias has no
   directive of its own; resolving any earlier reports size 0, which looks
   right in a disassembly and is wrong in the symbol table.
4. **Equal-address symbols break ties on DEFINITION order, not name.** An
   alias shares its target's address exactly and gas emits the target first,
   so sorting those two by name put `alias` before `real`. This changed a
   shared code path, and the 20-object arm64 differential is what proved it
   safe.

One deliberate divergence, documented in the upstream test: gas ACCEPTS
`.set a, undefined` and silently drops `a` from the symbol table; we reject it
and name the missing target. Nothing the compiler emits reaches that case —
an `alias` attribute requires its target defined in the same translation unit.

PROCESS NOTE: the first reproduction used the prebuilt `target/release/afs-as`,
which was STALE, and reported a different error than the source produces. Same
family as `build/a64mir` being a separate make target. Rebuild before
believing a diagnostic.

## SEC-MACHO-001 — `section("name")` has no Mach-O spelling yet

ELF takes a bare section name; Mach-O takes `SEGMENT,SECTION` plus attributes
(`__DATA,__mysec,regular`). One ELF-shaped string cannot serve both, so
arm64-macos refuses `section(...)` by name rather than mangling it into
something that assembles and lands somewhere else.

What it needs: a mapping from a bare name to a segment/section pair, or a
documented rule that the attribute's argument is spelled per-target. gcc on
Darwin requires the author to write the Mach-O form, which is the simpler
answer and matches "the name is emitted verbatim" — the refusal should become
a pass-through once the Mach-O corpus needs it.
