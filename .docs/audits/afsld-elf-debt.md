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
