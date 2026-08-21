# Cgfried safe mode

`-fsafe` is a translation-unit policy for C17 programs. It composes
`-fcgf-safe`, `-Werror=mem`, `-Werror=uninitialized`, and
`-ftrivial-auto-var-init=zero`, then rejects constructs whose behavior the
current analysis cannot model. Accepted programs keep their ISO C17 meaning;
the compiler does not reinterpret a rejected construct to make it pass.

Safe mode is a bug-finding and mitigation profile, not a security-hardening boundary.
Its guarantees cover accesses compiled in a `-fsafe` translation
unit. Unsafe linked translation units can call safe code, but their own
dereferences are unchecked even when they access allocations created by safe
code.

## Guarantees

| Guarantee | Mechanism | Limit |
|---|---|---|
| Heap spatial safety | Allocation sizes and pointer offsets flow into opaque bounds checks before safe-TU dereferences. | Stack and global spatial instrumentation are deferred; unsafe-TU accesses are unchecked. |
| Heap temporal safety | Freed blocks remain poisoned in a FIFO quarantine and checked accesses trap. | The quarantine retains at most 1024 blocks or 8 MiB; sufficiently old dangling pointers can escape detection. |
| Definite initialization | Default-tier memory-flow warnings and definite-uninitialized reads are errors; automatic scalar storage is zero-initialized after analysis. | Maybe-uninitialized path heuristics remain warnings; union padding, externally initialized memory, and code outside safe TUs remain outside this guarantee. |
| Null safety | Proven-null dereferences in the default memory tier are errors and emitted safe accesses carry runtime checks. | Indirect behavior hidden inside unsafe TUs or unsummarized external code is not inspected. |

## Rejected constructs

Every rejection names a deployment alternative; per-TU granularity is the
deliberate escape hatch.

| Construct | Why rejected | Alternative |
|---|---|---|
| Integer-to-pointer or pointer-to-integer casts | Ordinary integer arithmetic loses pointer provenance. | Use the `uintptr_t` round-trip grammar below, or isolate the boundary in a non-safe TU. |
| A union where pointer-bearing storage overlaps a non-pointer member | Reading the non-pointer view can forge provenance. | Use a tagged struct and explicit accessor functions. |
| Inline assembly | Its memory and control-flow effects are not analyzable. | Move the assembly to a non-safe TU and call it. |
| `setjmp` or `longjmp` families | Nonlocal control flow skips ordinary lifetime transitions and cleanup. | Return error codes, or isolate the jump in a non-safe TU. |
| Variable-size `alloca` | It has neither a scoped language extent nor a statically known site size. | Use a language-scoped VLA, or allocate on the heap; constant `alloca` is allowed. Stack spatial instrumentation remains deferred as stated above. |
| `asprintf` or `vasprintf` | Their returned allocations are not registered with the safe runtime. | Format into storage from a wrapped allocation family, or isolate the call and ownership in a non-safe TU. |
| Provenance-losing casts through `volatile` or device-I/O integers | Volatility orders accesses but does not preserve pointer provenance. | Put the I/O boundary in a non-safe TU. |

## `uintptr_t` round trips

The accepted grammar keeps the integer value syntactically derived from one
pointer until it is cast back:

```text
round_trip := (T *)(uintptr_t_expr)
uintptr_t_expr := ptr_derived | ptr_derived ALIGN_OP const
ptr_derived := (uintptr_t)ptr_expr [ | const ] [ & ~const ]
ALIGN_OP := + | - | & | |
```

The constant adjustments cover alignment masks, fixed byte offsets, and tag
bits. A standalone pointer-to-integer conversion, variable offset, narrower
integer temporary, multiplication, shift, or XOR is rejected. A null pointer
constant keeps its ISO C meaning and does not need this grammar.

## Mixed programs and ELF notes

Every object emitted under `-fsafe` carries a versioned
`.note.cgf.safe` ELF note. A final `cgf -fsafe` link validates every explicit
user object. An object without the note is a link error (exit 2) unless its
exact path appears in `-fsafe-allow-unsafe=<path>`. `-l` system libraries and
driver-injected CRT objects are toolchain boundaries and are exempt. Explicit
archives are user inputs and therefore require a supported note or an explicit
allowance.

User-supplied raw linker controls (`-Xlinker` and `-Wl,`) are rejected during
a safe link because they can hide objects, archives, response files, or linker
scripts from note validation. Pass safe objects directly, or perform the
custom link as an explicitly non-safe link step.

Calling unsafe code from safe code is allowed. Ownership annotations and
summaries describe such callees when available, but this does not extend the
safe-TU guarantee into their implementation.

## Compiler dogfood allowlist

`make safe-dogfood` compiles every compiler source file with `-fsafe`, verifies
every generated object's safety note, links the generated objects, and runs the
driver smoke tier on the result. The current compiler needs zero exemptions:
`ci/safe-mode-allowlist.txt` and its committed upper-bound baseline are empty.
The gate still validates `path:symbol owner justification` syntax and proves
that shrinkage succeeds while unreviewed growth fails, so any future exemption
would be an explicit contract change in review.

## Deferred extensions

- Pointer versioning will replace the finite-quarantine limit with
  deterministic temporal detection.
- Cross-TU summary export will reduce uncertainty at unsafe/external call
  boundaries.
- Stack and global spatial instrumentation will extend bounds coverage beyond
  the heap.
- User-defined acquire/release pairs will extend lifetime checking beyond the
  built-in allocation families.
