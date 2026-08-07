# GNU C extensions in Cgfried

Real C does not stop at ISO C. musl, glibc's headers, curl, sqlite and lua all
reach for GNU extensions, and a compiler that rejects them cannot build the
programs it exists to build. This document is the contract for which of those
extensions Cgfried accepts, and — just as importantly — which it refuses and
why.

Every extension sits in exactly one of three tiers.

| tier | meaning |
|---|---|
| **implemented** | the semantics are real, and there is at least one fixture proving it |
| **parsed-ignored** | accepted so surrounding code compiles, but has NO effect; always warns under `-Wcgf-ignored-attribute` |
| **refused** | a hard error at parse time, naming what it would take |

There is no fourth tier, and in particular there is no *silently* ignored
extension. An attribute that changes layout or linkage and is quietly dropped
produces a program that links and misbehaves, which is the one failure mode no
test distinguishes from success. `parsed-ignored` therefore always warns.

`scripts/check_gnu_tiers.sh` (in `make test`) enforces that every row in the
implemented table names a fixture that exists, and that no refused row's
message has gone missing. The table is executable documentation: change a
tier, and the same commit changes the code and the fixture.

## Why `__GNUC__` is defined only in GNU modes

`-std=gnu*` defines `__GNUC__` 8, `__GNUC_MINOR__` 3, `__GNUC_PATCHLEVEL__` 0
— impersonating the parity baseline the warning and format work already
measure against. `-std=c*` does not define it at all.

That split is load-bearing rather than cosmetic. Defining `__GNUC__` is a
PROMISE: glibc's `sys/cdefs.h` gates dozens of declarations on it, and taking
those paths obligates us to implement what they use. The implemented table
below is that obligation list. Where a header asks "are you gcc 8?", answering
yes and then rejecting one of gcc 8's extensions is worse than answering no.

Apple's `sys/cdefs.h` does not offer the choice: it uses `__attribute__`
unconditionally, with no `__GNUC__` gate at all, which is why hosted
compilation on arm64-macos waits on the attribute work rather than on a
predefine.

## Implemented

Empty for now. Rows land here as the work does, each with its fixture, and
`check_gnu_tiers.sh` fails if a row names a fixture that is not there.

| extension | fixture | consumer |
|---|---|---|

## Parsed and ignored

Accepted so the surrounding declaration compiles; no semantic effect. Each
warns once per occurrence under `-Wcgf-ignored-attribute`, which `-Wextra`
enables.

| extension | why ignoring is safe | consumer |
|---|---|---|

## Refused

A hard error at parse time. Refusing is deliberate: each of these changes
observable behaviour, so parsing and then ignoring one would miscompile
silently rather than fail loudly.

| extension | why | who actually needs it |
|---|---|---|
| `asm goto` | control flow out of an asm block needs edges the IR verifier would have to trust rather than check | the Linux kernel; none of our targets |
| `mode(...)` attribute | selects a machine mode independent of the C type; our type system has no such axis | glibc `__int128` corners only |
| `vector_size(...)` | would create vector types with no AAPCS64 or SysV parameter contract — Sprint 36 declined to invent one | none of our corpora |
| nested functions | requires executable trampolines on the stack | none of our targets |
| computed goto (`&&label`, `goto *p`) | out of the v0.1.0 scope contract | interpreters; not our corpora |

## Notes that are easy to get wrong

**`packed` and atomics.** On arm64, ordinary unaligned loads and stores are
fine, but the exclusive instructions that implement `_Atomic` are not. An
`_Atomic` member of a packed struct is therefore a hard error rather than a
slow path — the alternative is an atomic that silently is not one.

**`cleanup` and `longjmp`.** A `cleanup` function runs on every ordinary exit
edge from its scope: fallthrough, `return`, `break`, `continue`, `goto` out.
It does NOT run when a `longjmp` unwinds past it. That is gcc's behaviour and
the C standard's, and it is worth stating because it surprises people who
expect destructor semantics.

**Statement expressions and lifetime.** The value of `({ ... })` is the value
of its last *expression statement*; if the last item is a declaration or a
non-expression statement the whole thing has type `void`. Temporaries created
inside live until the end of the enclosing full expression, not the end of the
statement expression.
