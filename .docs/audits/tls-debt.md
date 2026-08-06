# Thread-local storage — debt ledger (opened during Sprint 50)

`_Thread_local` parses, types, and records correctly. It has never been
**lowered**. Until Sprint 50 it reached the backend as an ordinary global and
every thread shared one copy — a silent miscompile on every target, with no
diagnostic anywhere.

Measured on x86_64-linux, four threads each incrementing a `_Thread_local int`
one thousand times:

| producer | `main counter=` |
|---|---|
| gcc 16 | `0` |
| cgfried (before) | `1000` |

The generated code is the giveaway: we emitted `tls_v` into `.data` and read
it with `movl tls_v(%rip), %ecx`, where gcc emits `.tdata` and
`movl %fs:tls_v@tpoff, %eax`.

**CLOSED for x86_64 local-exec in Sprint 51 (TLS-001).** What follows
describes the state it was found in; the current state is at the bottom.

**As of Sprint 50 it was a hard error at lowering**, naming Sprint 50, the same
rule Sprint 28 applied to the runtime's unimplemented entry points: a stub
that returns a plausible value is the one failure mode no test distinguishes
from success. `-fsyntax-only` and `-fdump-sema` still accept it, because the
analysis module is not object emission and the sema-level rules (6.7.1
placement constraints, the linkage matrix) are already tested and correct.

## Why this is its own deliverable

Nothing about the gap is target-specific — no target ever had it. Sprint 50
owns it only because its deliverable 5 is the first to name `_Thread_local` at
all. The work is three separate mechanisms plus an upstream assembler change:

| ID | Piece | Notes |
|---|---|---|
| TLS-001 | ELF `.tdata`/`.tbss` sections and the local-exec model on x86_64 | **CLOSED in Sprint 51** |
| TLS-002 | Local-exec on arm64-linux | **CLOSED in Sprint 51** |
| TLS-003 | Mach-O thread-local variables | `__DATA,__thread_vars` descriptors and a call through the descriptor — a completely different mechanism from ELF TLS, not a spelling change |
| TLS-004 | afs-as support for the TLS relocation families on both architectures | The bundled assembler has none today |
| TLS-005 | Initial-exec / general-dynamic models | Only needed once we emit PIC or shared objects, which is Sprint 51. Local-exec is sufficient for the non-PIE executables v0.1.0 produces. |

TLS-005 is deliberately last: local-exec covers everything the current output
model can produce, and implementing the dynamic models before there is a
shared object to test them against would be unverifiable.

## Blast radius of the hard error, measured

Nothing regressed when it landed:

- The five `tests/programs/sema16/tls_*.c` fixtures use `-fsyntax-only` or
  `-fdump-sema` and still pass.
- `__thread` is not a keyword for us, so musl's thread-local translation units
  were already in the pinned deferral set and their counts are unchanged.
- `src/rt/cgf_safe_alloc.c` uses `_Thread_local` for the Sprint 44 range
  cache, but the runtime is built by `RT_CC` (the host compiler), not by cgf.
  **Sprint 58 flips that**, so TLS-001 is a hard prerequisite for the
  self-hosting bootstrap — it cannot slip past Phase 12.

## TLS-001, closed in Sprint 51

`_Thread_local` lowers on x86_64. `IrGlobal.is_tls` carries the fact (it is a
property of the OBJECT, not of any reference, so every backend asks the module
rather than threading it through operands) and round-trips as ` tls`.

Emission: `.tdata,"awT",@progbits` / `.tbss,"awT",@nobits`, `@tls_object`, and
the symbols come out `STT_TLS` exactly as gcc's do. A tentative thread-local
cannot go through `.comm`, which has no way to say thread-local, so it becomes
an ordinary `.tbss` definition — also what gcc does.

Addressing is the local-exec model, and the ADDRESS is materialized rather
than folded into each access:

    movq %fs:0, %rN            # the thread pointer
    leaq sym@tpoff(%rN), %rM   # R_X86_64_TPOFF32

One code path then serves loads, stores and address-of alike, and an addend
rides the `lea` displacement where a relocation could not carry it. `fold_addr`
refuses to fold a thread-local symbol for the same reason it refuses a GOT
symbol: the address takes more than one instruction to build.

**The test that matters is the four-thread one.** Section spellings prove
nothing about semantics; `tests/programs/tls/tls_threads_are_separate.c` runs
four threads incrementing a thread-local a thousand times each and requires
main to still see 0. That is the number that was 1000.

Two honest boundaries, both clean errors rather than guesses:

- An **extern** thread-local emits no global, so nothing downstream can tell
  the symbol is thread-local — and answering "it is not" is the original
  silent miscompile. Local-exec only reaches a definition in this TU; an
  extern one needs initial-exec (TLS-005). Refused by name.
- The **bundled assembler** has neither `%fs:` nor `@tpoff` nor
  `R_X86_64_TPOFF32` (TLS-004). The driver says so and points at `CGF_AS=0`,
  instead of letting afs-as reject correct assembly and reporting it as "a cgf
  emission bug", which blames the wrong component.

## TLS-002, closed in Sprint 51

arm64-linux local-exec. The thread pointer is an architectural register
rather than a segment base, and the offset arrives in two 12-bit halves:

    mrs xN, tpidr_el0
    add xN, xN, #:tprel_hi12:sym, lsl #12
    add xN, xN, #:tprel_lo12_nc:sym

`R_AARCH64_TLSLE_ADD_TPREL_HI12` and `..._LO12_NC`; `_nc` is no-check,
because the pair is only correct together and the low half must not complain
about bits the high half carries. Byte-for-byte the sequence gcc emits.

**An ordering rule, learned the hard way.** gas tracks whether a symbol is
thread-local and rejects a TLS relocation naming one it has not yet seen
DEFINED in a TLS section -- "Accessing `counter' as thread-local object".
Functions are emitted before data here, so thread-locals have to go first,
which is what gcc does too. A `.type ... @tls_object` declaration up front is
NOT enough; the definition itself must precede the reference. x86 gas happens
to accept the other order, so this was arm64-only and would have looked like
a codegen bug rather than an emission-order one.

Verified by running the four-thread program under qemu: `main counter=0`.

Remaining: TLS-003 (Mach-O), TLS-004 (afs-as), TLS-005 (initial-exec /
general-dynamic).
