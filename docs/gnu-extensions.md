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
| **parsed-ignored** | accepted so surrounding code compiles, but has NO effect; always warns under `-Wattributes` |
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

| extension | fixture | consumer |
|---|---|---|
| `weak` | `tests/programs/gnu/attr_weak_overridden.c` | musl `weak_alias`, glibc |
| `visibility("...")` | `tests/programs/gnu/attr_symbol_binding.c` | glibc headers (88 uses in /usr/include) |
| `packed` | `tests/programs/gnu/attr_packed_layout.c` | musl, glibc, on-disk and wire structs everywhere |

The two symbol-property rows are verified against the ELF symbol table rather
than the emitted directive: `readelf -sW` agrees with gcc on binding and visibility for every
symbol, on x86_64 AND arm64, and `weak`'s fixture links a strong definition
over the weak one so the linker's choice — not the directive — is what
passes. A compiler can emit `.weak` and still get the binding wrong; only the
symbol table says otherwise.

`packed` is proved differently, because layout is not a symbol property. Its
numbers were measured off gcc BEFORE any code was written
(`.docs/audits/packed-layout.md`), the generated layout differential now emits
packed structs and packed members so 2,000 random records per run are compared
against gcc, and a corpus program executes misaligned reads and writes on both
targets at every optimization level.

Three things about `packed` are worth stating because each is a way to get it
wrong while looking right:

- **It drops the RECORD's alignment as well as its members'.** Force the member
  offsets alone and every offset a reader checks is correct while `sizeof`
  keeps its tail padding. Injecting exactly that bug drops the layout
  differential from 400/400 to 277/400, and it fails on `_Alignof`, not on an
  offset.
- **Position decides what it binds to.** A trailing attribute packs the record,
  and so does one between the keyword and the tag -- but a LEADING one, before
  the keyword, gcc silently ignores. Reading all three the same way packs types
  nobody asked to pack.
- **On x86-64 it changes how the struct is PASSED.** The psABI puts an
  aggregate with unaligned fields in MEMORY however small it is, and the test
  is the field OFFSET rather than the record's alignment: `struct { int b; }
  packed` is 1-aligned, has every field at its natural offset, and gcc passes
  it in a register. Mixed links against gcc agree in both directions on
  x86_64 and arm64-linux.

Packed BIT-FIELDS are rule 5 of that audit and are NOT implemented; a bit-field
in a packed struct is a hard error naming the gap, because half of packed
applied silently is the failure mode this document exists to prevent. An
`_Atomic` member of a packed struct is refused for good: arm64's exclusive
instructions require natural alignment, and an atomic that quietly is not one
is worse than a diagnostic.

## Parsed and ignored

Accepted so the surrounding declaration compiles; no semantic effect. Each
warns under **`-Wattributes`** — gcc's own flag for this, on by default, so a
build system that already passes `-Wno-attributes` gets silence from us too.

Membership is decided by ONE question, and `src/parse/gnu_attrs.def` is the
table: *what happens if we ignore it?* If the cost is a diagnostic or a
missed optimization, it belongs here. If it changes layout, linkage or
observable behaviour, it does not — it is implemented or it is a hard error.

An attribute this compiler has never heard of is also accepted and warned,
exactly as gcc does. A compiler that rejects a name it does not know cannot
read next year's headers.

| extension | why ignoring is safe | consumer |
|---|---|---|
| `unused`, `used`-adjacent hints | affect diagnostics only | everywhere |
| `format`, `format_arg` | Sprint 39's builtin table already covers the libc functions that matter | glibc, musl |
| `pure`, `const`, `malloc`, `leaf`, `noclone`, `flatten` | optimization licenses; declining one is always conservative | glibc, curl |
| `noreturn` | we merely fail to learn a call does not return: costs flow precision, cannot make a correct program wrong | everywhere |
| `deprecated`, `warn_unused_result`, `nonnull`, `sentinel`, `nonstring`, `diagnose_if`, `access`, `alloc_size`, `alloc_align` | diagnostics only | glibc |
| `always_inline`, `noinline`, `hot`, `cold`, `artificial`, `no_instrument_function` | inliner and placement hints | glibc, musl |
| `nothrow` | C has no exceptions | glibc |

### Not yet implemented, and therefore refused rather than ignored

These change layout, linkage or behaviour. Until their semantics land they
are a hard error naming the attribute — which is what makes implementing them
incrementally safe: at every point the compiler either does the right thing or
refuses, never quietly the wrong one.

`alias`, `aligned`, `cleanup`, `constructor`, `destructor`, `gnu_inline`,
`may_alias`, `returns_twice`, `section`, `transparent_union`, `used`.

Three of those sit here against this sprint's original tiering, because the
ignore-safety question overruled it:

- **`transparent_union`** changes how the union is *passed* — the first
  member's convention, not the union's. Ignoring it is an ABI mismatch. It
  does not appear in musl, so refusing costs nothing.
- **`may_alias`** switches *off* type-based aliasing for a type. Ignoring it
  leaves the optimizer applying TBAA exactly where the author said it must
  not, which is a subtle miscompile.
- **`used`** keeps a symbol the IPO pass would otherwise delete.

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
