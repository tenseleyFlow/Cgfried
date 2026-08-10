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
| `aligned` | `tests/programs/gnu/attr_aligned_layout.c` | glibc (17 uses in /usr/include), cache-line and SIMD code |
| `alias` | `tests/corpus/x86_64/int/attr_alias.c` | musl's `weak_alias`, glibc's versioned symbols |
| `used` | `tests/programs/gnu/attr_used.c` | version stamps, kept-alive tables, musl |
| `__asm__("name")` labels | `tests/programs/gnu/asm_label.c` | Apple's `__DARWIN_ALIAS`, glibc symbol versioning |
| `section` | `tests/programs/gnu/attr_section.c` | kernel-style tables, init arrays, embedded layouts |
| `constructor` / `destructor` | `tests/corpus/x86_64/int/attr_ctor_dtor.c` | glibc, library self-registration, test frameworks |
| `cleanup` | `tests/corpus/x86_64/int/attr_cleanup.c` | systemd's `_cleanup_` idiom, glibc, scope-guard patterns in C |
| `deprecated`, `deprecated("msg")` | `tests/programs/gnu/attr_deprecated.c` | glibc, every library retiring an API |
| `warn_unused_result` | `tests/programs/gnu/attr_warn_unused_result.c` | glibc (`read`, `write`, `realloc`), curl, anything whose result must be checked |
| `format(archetype, m, n)` | `tests/programs/gnu/attr_format.c` | every project with its own `printf` wrapper; glibc, curl, systemd |
| `nonnull(...)`, bare `nonnull` | `tests/programs/gnu/attr_nonnull.c` | glibc (`memcpy`, `strlen`, most of `string.h`) |
| `noreturn` | `tests/programs/gnu/attr_noreturn.c` | glibc's `__assert_fail`/`abort`, musl, every fatal-error helper |
| basic `asm` (no operands), statement and file-scope | `tests/corpus/x86_64/int/asm_basic.c` | musl `crt`, tinycc, `nop`/`mfence`/`cli` one-liners |
| extended `asm` — operands, constraints, clobbers | `tests/corpus/x86_64/int/asm_operands.c` | musl syscall wrappers and atomics; every libc's `arch/` |
| statement expressions `({ ... })` | `tests/corpus/x86_64/int/stmt_expr.c` | musl and glibc internal headers, Linux, every safe-macro idiom |
| `typeof` / `__typeof__` / `__typeof`, `__auto_type` | `tests/corpus/x86_64/int/typeof_auto_type.c` | every generic macro in musl, glibc and Linux |
| `__builtin_types_compatible_p`, `__builtin_choose_expr` | `tests/corpus/x86_64/int/builtin_type_query.c` | glibc's type-dispatch macros, Linux's `__same_type` |
| `__thread`, `__extension__` | `tests/corpus/x86_64/int/gnu_thread_extension.c` | musl and glibc write `__thread`; `__extension__` guards every pedwarn-provoking header construct |
| case ranges `case lo ... hi:` | `tests/corpus/x86_64/int/gnu_case_range.c` | character classification, Linux, any dense dispatch over a span |
| `a ?: b` (omitted middle operand) | `tests/corpus/x86_64/int/gnu_cond_omitted.c` | default-value idioms in glibc and Linux, where the left operand is a call |
| integer `mode(M)` — `QI`/`HI`/`SI`/`DI`/`byte`/`word`/`pointer` | `tests/corpus/x86_64/int/gnu_mode.c` | glibc's `register_t` in `<sys/types.h>`, which blocks `<stdlib.h>` and most of a hosted TU once `__GNUC__` is defined |
| `__builtin_bswap16/32/64` | `tests/corpus/x86_64/int/gnu_bswap.c` | glibc's `<bits/byteswap.h>`, so every `htonl`/`be32toh`; Linux, musl |

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

`aligned` is the inverse of `_Alignas` in the one way that matters:

- **It only ever RAISES.** A request weaker than the natural alignment is
  silently declined, where `_Alignas` makes the same request a constraint
  violation (6.7.5p4). So **`aligned(1)` is not a spelling of `packed`** — the
  member stays at its natural offset. Every consumer stores into an
  `align_override` field read with `>`, which is exactly that rule.
- All four positions work: record (trailing, and between the keyword and the
  tag — a LEADING attribute gcc ignores, same as `packed`), member, object and
  function. The function position aligns the CODE and both emitters take the
  max against their own default padding.
- The argument is a constant EXPRESSION, not a literal. `aligned(4 * 8)` and
  `aligned(sizeof(long))` appear in real headers, so it is folded in sema by
  the evaluator `_Alignas` already uses. The no-argument form is the target's
  biggest alignment, measured as 16 on x86-64 and arm64-linux.
- It COMPOSES with `packed` rather than conflicting: packed forces the member
  offsets, aligned sets the record's alignment, and the size rounds up to it.

Landing it found two things nothing else had: `_Alignas` on an OBJECT did
nothing at all on any target, and `layout_union` never read `align_override`,
so an alignment on a union MEMBER was ignored for both spellings while working
correctly in a struct. The second was found by the layout differential on its
first run with generated `aligned` attributes — 11 disagreements per 400, every
one a union.

`alias` gives one symbol a second name. The target must be DEFINED in the same
translation unit — gcc's rule, and the reason an alias needs no cross-TU
machinery: the emitter settles it with one `.set` and there is no way to
produce a dangling symbol. It is checked at end of TU, because the target may
be defined after the alias that names it.

`IrAlias` is its own module list rather than a flag on a global or a function,
since an alias is neither: no storage, no initializer, no body. Both
declaration shapes — an object alias is a `SYM_VAR`, a function alias a
`SYM_FUNC` the global loop skips — go through one lowering pass keyed on the
target, which is what stops the difference becoming two half-implementations.

Two bugs it found, both only visible at LINK time:

- **IPO deleted a static function reachable only through its alias.** An alias
  reference is a `.set`, not a relocation, so nothing in the callgraph saw it.
  Alias targets are address-taken roots now. The symptom was `undefined
  reference` at `-O2` and nothing at all at `-O0`.
- **`.weak_definition` is Mach-O's spelling** and ELF's assembler rejects it;
  ELF wants `.weak`. Copied from the Apple path into the shared one, caught by
  the first real arm64 assembly.

Verified against the ELF symbol table rather than the directives: type
(`FUNC`/`OBJECT`) and binding (`GLOBAL`/`LOCAL`/`WEAK`) agree with gcc for
every alias, and the executed fixture proves an alias and its target are the
same object rather than a copy.

The bundled assembler gained `.set` for x86 in upstream PR #28 and for arm64
and Mach-O in PR #29 (AS-SET-002, now closed), so the fixture runs on the real
default path on every target and lives in `tests/corpus`, which the arm64 lane
re-runs — aliases execute on arm64 through the bundled assembler rather than
routing around it.

The two `.set` forms are told apart by whether the right-hand side is a lone
symbol that is not itself a `.set` name, and that decision cannot be made
while parsing, which runs before any label exists. `.set A, 5` / `.set B, A`
still gives B the value 5.

`used` keeps a symbol nothing in the translation unit references. It reaches
the same root set an `alias` target does, from the other direction — one is
said in the source, the other implied by a `.set` the callgraph cannot see — so
they share the code.

Its fixture is built on a NEGATIVE, and that is the point: a positive check
passes whether or not anything is ever dropped, so it would go green on a
compiler that removes nothing. `ASM_CHECK-NOT` was added for it, because an
absence is not expressible with a positive check and "the symbol was DROPPED"
is the claim under test. `-O2` is required — at `-O0` nothing is removed and
both survive.

Adding that directive walked straight into F-S22-MIRCHECK a second time: it
parsed, validated, and was silently dropped, because a new kind must also be
listed in `directive.c`'s `add_dir`. The mutated fixture caught it; nothing
else would have. Meta fixtures now pin both directions.

One honest divergence: we keep an unreferenced static OBJECT that gcc drops.
Nothing dead-strips globals here yet, so `used` on an object is carried through
IR but currently changes nothing. The flag is plumbed anyway, so it is already
right the day global dead-stripping lands rather than being remembered then.

An **asm label** — `__asm__("name")` after a declarator — renames the SYMBOL.
The C identifier is unchanged, so source references and diagnostics still use
what the programmer wrote; only what the linker sees changes. That is why
`lower_link_name` is a separate accessor rather than a rewrite of
`Symbol.name`: an error message must still say the identifier.

It is not an attribute, but it lives in the same structure for the same reason
`weak` does — a property of the symbol, decided at the declaration and consumed
by whatever emits or references it. The name is emitted VERBATIM and is not
validated as an identifier: `_fopen$UNIX2003` is a real Darwin symbol, `$` and
all.

**This is the one that hosted macOS was waiting on.** Apple's `sys/cdefs.h`
uses `__attribute__` with no `__GNUC__` gate to hide behind, and
`__DARWIN_ALIAS` renames `fopen` and friends through exactly this mechanism.
Neither half can be faked, which is why the arm64-macos corpus has been
freestanding-only.

The same keywords open an inline-asm STATEMENT, a different construct that
only POSITION tells apart from a label — and it is implemented, basic and
extended forms both.

`section("name")` places an object or function in a named output section.
Functions take `"ax"`, data `"aw"`, matching gcc.

Two things it is easy to get wrong:

- **A named section forces PROGBITS.** An *uninitialized* object there gets
  real bytes — gcc emits `.section .s,"aw"` then `.zero 4` — rather than a
  `.bss` reservation or a `.comm`, because the section the author named is
  where the bytes must be. Leaving it in `.bss` puts it outside the section
  entirely, which the fixture would catch and a directive check would not.
- **The fixture checks the ADDRESS**, against the linker's
  `__start_NAME`/`__stop_NAME` bounds, not the emitted directive. A
  `.section` line proves what was emitted; where the object *lands* is the
  claim under test, and it is what real consumers (kernel tables, init arrays)
  depend on.

One divergence, and it is pre-existing rather than introduced here: gcc gives a
`const` object's named section `"a"` where we give `"aw"`. We place const
globals in `.data` today and have no `.rodata` for them at all, so this is
consistent with existing behaviour; the missing `.rodata` is a separate gap.

**SEC-MACHO-001**: Mach-O spells a named section `SEGMENT,SECTION` with
attributes, so an ELF-shaped name cannot be reused there. arm64-macos refuses
by name rather than mangling it into something that assembles and lands
somewhere else.

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
| `format_arg` | the returned string's format is the caller's to check, and the caller's own `format` attribute already does it | glibc |
| `pure`, `const`, `malloc`, `leaf`, `noclone`, `flatten` | optimization licenses; declining one is always conservative | glibc, curl |
| `sentinel`, `nonstring`, `diagnose_if`, `access`, `alloc_size`, `alloc_align` | diagnostics only | glibc |
| `always_inline`, `noinline`, `hot`, `cold`, `artificial`, `no_instrument_function` | inliner and placement hints | glibc, musl |
| `nothrow` | C has no exceptions | glibc |

### Not yet implemented, and therefore refused rather than ignored

These change layout, linkage or behaviour. Until their semantics land they
are a hard error naming the attribute — which is what makes implementing them
incrementally safe: at every point the compiler either does the right thing or
refuses, never quietly the wrong one.

`gnu_inline`, `may_alias`, `returns_twice`, `transparent_union`.

Two of those sit here against this sprint's original tiering, because the
ignore-safety question overruled it:

- **`transparent_union`** changes how the union is *passed* — the first
  member's convention, not the union's. Ignoring it is an ABI mismatch. It
  does not appear in musl, so refusing costs nothing.
- **`may_alias`** switches *off* type-based aliasing for a type. Ignoring it
  leaves the optimizer applying TBAA exactly where the author said it must
  not, which is a subtle miscompile.

## Refused

A hard error at parse time. Refusing is deliberate: each of these changes
observable behaviour, so parsing and then ignoring one would miscompile
silently rather than fail loudly.

| extension | why | who actually needs it |
|---|---|---|
| `asm goto` | control flow out of an asm block needs edges the IR verifier would have to trust rather than check | the Linux kernel; none of our targets |
| two REGISTER outputs in one `asm` | one MIR instruction defines one value on both backends, so a second register output means widening `CgMirView` rather than adding a case. Counting musl's sites for our two targets says that is rarely what a second output is: x86_64 has 181 one-output against 27 two-output, aarch64 172 against 19, and the two-output cases are dominated by a MEMORY second output, which consumes no register and IS supported | Linux's `__cmpxchg` shapes; no musl TU we compile |
| non-integer `mode(...)` — `TI`, `SF`/`DF`/`XF`/`TF`, `V*` | each names a type this compiler does not have: a 128-bit integer, a floating type chosen by width (which would silently disagree with the target's own `float`/`double`/`long double` — x86-64's is x87 80-bit, so `TF` is not it), or a vector with no SysV/AAPCS64 parameter contract. The INTEGER modes are implemented; see that row | glibc uses exactly one mode in all of `/usr/include`, and it is an integer one |
| `vector_size(...)` | would create vector types with no AAPCS64 or SysV parameter contract — Sprint 36 declined to invent one | none of our corpora |
| nested functions | requires executable trampolines on the stack | none of our targets |
| computed goto (`&&label`, `goto *p`) | out of the v0.1.0 scope contract | interpreters; not our corpora |
| `__label__` (block-scoped labels) | our labels have FUNCTION scope and are interned by the lexer, with `label_find` comparing pointers, so block scoping means mangling and the parser holds no interner to mangle with. Accepting it as an ordinary label would compile the single-use case and report "duplicate label" on the sibling-block case gcc accepts — rejecting valid code while looking implemented | musl 0 uses, glibc headers 0; only clang's own sources |
| empty struct / union (`struct E {}`, `struct { int :0; }`) | gcc gives these size ZERO, and `struct E arr[3]` then has `&arr[0] == &arr[1]` — measured. That breaks the "distinct objects have distinct addresses" property the shared alias service and the memory-safety lattice are both built on: allocation sites there are separated by byte-offset hulls, which cannot express two objects at one address with zero extent | musl 0, glibc's C headers 0 (every `/usr/include` hit is C++), Linux uapi 1 inside `__DECLARE_FLEX_ARRAY` |

## Notes that are easy to get wrong

**`packed` and atomics.** On arm64, ordinary unaligned loads and stores are
fine, but the exclusive instructions that implement `_Atomic` are not. An
`_Atomic` member of a packed struct is therefore a hard error rather than a
slow path — the alternative is an atomic that silently is not one.

**`constructor`/`destructor` priorities.** The argument is a constant
expression in `0..65535`; outside that is an error, and `0..100` are reserved
for the implementation and warn under `-Wprio-ctor-dtor`. The top of the range
is also the DEFAULT — gcc emits the same unnumbered `.init_array` for a bare
`constructor` and for `constructor(65535)`, so the two are one request.
Numbered sections sort ahead of the plain one, so a lower number runs *first*;
`.fini_array` is laid out identically and walked in reverse, making the
destructor order the exact mirror. One function may be both, and gets an entry
in each array.

**We keep a priority gcc drops.** When the attribute lands on a definition
whose earlier declaration carried none, gcc keeps the constructor-ness and
silently discards the priority — measured on 8.5 and 16.1, so it is entrenched
rather than a regression; clang gets it right. Attributes here merge as a union
across declarations of one symbol, so ours survives.
`tests/programs/gnu/attr_ctor_prio_survives_decl.c` pins it by *executing*,
because ordering is the only thing a priority is for.

**`destructor` is refused on arm64-macos.** Mach-O has no `.fini_array`: clang
implements the attribute by synthesizing a wrapper that calls
`__cxa_atexit(f, 0, &__dso_handle)` and registering that as an initializer.
Constructors work — a direct entry in `__DATA,__mod_init_func` — but their
priorities order entries within one object only, since ld64 does not sort that
section. clang has the same limitation.

**`cleanup` and `longjmp`.** A `cleanup` function runs on every ordinary exit
edge from its scope: fallthrough, `return`, `break`, `continue`, `goto` out.
It does NOT run when a `longjmp` unwinds past it. That is gcc's behaviour and
the C standard's, and it is worth stating because it surprises people who
expect destructor semantics.

Four more `cleanup` rules, each measured against gcc by execution rather than
read, and each a way to be wrong while the common case still works:

- **The return value is materialized before the cleanups run.** A cleanup
  that overwrites its variable does not change what `return x` returns.
- **A for-init variable belongs to the `for`,** not the enclosing block: its
  cleanup runs when the loop ends.
- **A `goto` out runs only the scopes it actually leaves.** Enclosing scopes
  the label is still inside run later, at their own end.
- **Two `cleanup` attributes on one declaration: the last wins,** silently.
  This is the one field of `GnuDeclAttrs` that does not union across a
  declaration, because unioning would run both and gcc runs one.

Jumping *into* a cleanup scope past the declaration is accepted, and the
cleanup then runs on a never-initialized variable. gcc accepts it silently and
so do we; **clang rejects it** the way C++ rejects jumping past a destructor.
That divergence is recorded rather than resolved — adopting clang's rule would
be a new diagnostic, not a bug fix.

`cleanup` applies to automatic block-scope variables only. On a global, a
static local, a `_Thread_local`, a parameter, a struct member or a typedef
there is no scope exit to hang the call on, and gcc warns and drops it.

**Statement expressions and lifetime.** The value of `({ ... })` is the value
of its last *expression statement*; if the last item is a declaration or a
non-expression statement the whole thing has type `void`, and using it where a
value is wanted is an error (gcc: "void value not ignored as it ought to be").
An empty `({ })` is legal and void. A braced group is refused at file scope,
where gcc refuses it too.

A `cleanup` variable declared inside runs at the statement expression's OWN
closing brace — **after** the value is materialized and **before** the
enclosing full expression continues. Measured against gcc, because the
plausible alternative (defer the cleanups to the end of the enclosing
expression) passes every test that does not observe ordering, and getting it
backwards hands the caller a value read out of storage the cleanup was already
given a chance to clobber. `tests/corpus/x86_64/int/stmt_expr.c` pins the
order as `CTT`.
