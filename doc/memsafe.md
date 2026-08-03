# Memory-safety architecture

Cgfried's memory-safety work is a six-layer ladder. Each layer adds one
mechanism and keeps its claim narrow enough to test. A layer's guarantee does
not imply the guarantees of later layers. Sprints 41 and 42 now ship the
shared foundation and the first user-visible layer: intraprocedural `-Wmem`
warnings with ordered proof traces.

## The six layers

The guarantees below are the contract for each layer once that layer ships.
The non-guarantee column is part of the contract, not an implementation note.

| Layer | Sprint | Mechanism | Guarantee | Does not guarantee |
| --- | --- | --- | --- | --- |
| L0 | 41 | Syntactic checks without control-flow reasoning | Once reporting is enabled, flags direct misuse that is visible at one operation, such as freeing a string literal. | Any property that requires following values or control flow. Sprint 41 itself emits no user-visible memory diagnostic. |
| L1 | 42 | Intraprocedural flow analysis under `-Wmem` | Within one function, reports proven use-after-free, double-free, leak-without-escape, out-of-bounds, and uninitialized-memory cases at the default zero-false-positive tier. | Cross-function bugs, all leaks, or general symbolic bounds. Escaped or imprecise state is not proof of a bug. |
| L2 | 43 | Interprocedural summaries and ownership annotations | Follows supported ownership effects across calls in the same translation unit and call graph. | Path-sensitive reasoning across calls or complete modeling of unannotated foreign code. |
| L3 | 44 | Runtime checks under `-fcgf-safe` | Dynamically detects supported violations that static analysis could not discharge. | Temporal safety beyond the quarantine depth, foreign allocations, or ASan's full shadow-memory coverage. |
| L4 | 45 | Fix-its and explicit transforms | Produces machine-applicable suggestions and never rewrites source silently. | Semantic preservation after a user applies a suggestion; the user must review the resulting change. |
| L5 | 46 | Composed `-fsafe` contract | Enforces the documented per-translation-unit contract and rejects constructs outside it. | Safety of linked translation units that were not compiled with `-fsafe`, or of foreign code outside the contract. |

The L1 leak claim is deliberately limited to allocations that analysis can
prove neither escape nor reach a release. It is not a claim to find every
leak. Likewise, an unknown state suppresses a static diagnostic; it does not
prove that the program is safe.

## Interprocedural ownership contracts

L2 summarizes each function once and applies that summary at its call sites.
It does not inline callees into the memory-safety analysis. Acyclic calls are
processed bottom-up; recursive strongly connected components and indirect
calls deliberately receive conservative summaries. This bounds analysis cost
and makes loss of precision suppress a claim rather than manufacture one.

Include `<cgfried/memsafe.h>` to use the five portable ownership macros. They
expand to Cgfried attributes under Cgfried and to nothing under GCC, Clang,
and other compilers, so the same public header remains portable. Parameter
numbers are one-based.

### Returning new ownership

`CGF_RETURNS_OWNED` says the caller receives a new ownership obligation.

```c
#include <cgfried/memsafe.h>

CGF_RETURNS_OWNED void *buffer_new(unsigned long size);

void example(void)
{
    void *p = buffer_new(64);
    /* ... */
    free(p);
}
```

### Transferring ownership to a callee

`CGF_TAKES_OWNERSHIP(n)` says the caller relinquishes argument `n`. The
callee may release it immediately or retain it as owned state.

```c
#include <cgfried/memsafe.h>

void queue_submit(void *item) CGF_TAKES_OWNERSHIP(1);

void send(void *item)
{
    queue_submit(item);
    /* item is no longer owned here. */
}
```

This is a forward contract. A definition that does not currently release or
store the argument is not an annotation mismatch: it may consume the argument
in a future compatible implementation. Callers must obey the contract now.

### Borrowing during a call

`CGF_BORROWS(n)` says argument `n` is used only for the duration of the call:
the callee neither releases it nor makes it escape.

```c
#include <cgfried/memsafe.h>

void checksum(const void *data) CGF_BORROWS(1);

void inspect(void *data)
{
    checksum(data);
    free(data); /* ownership remained with the caller */
}
```

### Returning a borrowed alias

`CGF_RETURNS_BORROWED(n)` says the result aliases argument `n` and creates no
new ownership obligation.

```c
#include <cgfried/memsafe.h>

CGF_RETURNS_BORROWED(1) char *skip_prefix(char *text);

void trim(char *text)
{
    char *view = skip_prefix(text);
    use(view);
    free(text); /* free the owner, not the borrowed view */
}
```

### Preventing escape

`CGF_NO_ESCAPE(n)` says argument `n` is not retained beyond the call. Unlike
`CGF_BORROWS`, it does not by itself prohibit release; it composes with
`CGF_TAKES_OWNERSHIP` when both facts are part of an API contract.

```c
#include <cgfried/memsafe.h>

void run_now(void *context) CGF_NO_ESCAPE(1);

void dispatch(void *context)
{
    run_now(context);
    free(context);
}
```

Unknown names in the `cgf_` attribute namespace and invalid parameter indices
are hard errors. Other GNU attributes retain the pre-Sprint-55 behavior and
remain hard errors until the full attribute surface lands. Definitions are
also checked against their annotations by default under
`-Wmem-annotation-mismatch`. A body that frees a borrowed argument, publishes
a no-escape argument, returns fresh ownership as borrowed, or returns a
borrowed object as owned is diagnosed; the declared contract is still applied
to callers.

## FILE ownership and resource scope

The same ownership lattice models `FILE *`: `fopen`, `fdopen`, and successful
`freopen` acquire a resource; `fclose` releases it; and operations such as
`fread`, `fwrite`, `fflush`, and `fprintf` borrow it. Consequently an
unclosed stream is a `-Wmem-leak` and closing the same stream twice is a
`-Wmem-double-free`, with resource-specific wording.

Acquire/release APIs for sockets, mutexes, descriptors, and application
handles fit the same state machine. Version 0.1.0 intentionally ships built-in
models only for memory and `FILE *`; user-defined resource pairs and arbitrary
resource kinds remain future work.

## L1 warning surface

`-Wmem` is enabled by default, independently of `-Wall`. `-Wno-mem` disables
the default group, and an exact per-check option outranks the group regardless
of command-line order. `-Werror=mem` promotes the group. The same per-check
names work with `#pragma GCC diagnostic` push, ignored, warning, and error.

| Warning | Default | Proven condition |
| --- | --- | --- |
| `-Wmem-use-after-free` | on | A tracked freed pointer is dereferenced or its value is used. Comparison with null is explicitly silent. Known dereferencing libc calls are modeled. |
| `-Wmem-double-free` | on | A must-alias freed allocation is released again. `free(NULL)` is silent. |
| `-Wmem-leak` | on | A tracked allocation reaches a real function return without release or escape. Returning, publishing, or passing ownership to an unknown callee suppresses the claim; noreturn paths are not exits. |
| `-Wmem-out-of-bounds` | on | A constant-size allocation has a constant or provable affine access outside its extent. Forming a one-past pointer is legal; dereferencing it is not. |
| `-Wmem-uninit-read` | on | A must-uninitialized byte range from a malloc-family site is read. `calloc`, fully written allocators, stores, and modeled libc writers update initialization state. |
| `-Wmem-free-nonheap` | on | Every possible target is non-heap storage, or a heap pointer has a proven nonzero interior offset. Mixed heap/nonheap and unknown-offset cases are silent. |
| `-Wmem-realloc-zero` | off | A constant zero size reaches `realloc` or `reallocarray`; C17 leaves this implementation-defined. Enable this check explicitly. |

### Use after free

The default check requires one exact tracked allocation and a direct value use,
dereference, or modeled dereferencing library call after release. Null
comparisons stay silent. Passing a freed pointer to an unknown call is kept in
the opt-in strict tier because “may dereference” is not default-tier proof.

### Double free

A second release is reported only for a must-alias freed site. Null releases,
may-alias sets, paths joined after a precision cap, and ambiguous conditional
ownership stay silent. Releasing an invalid interior pointer is handled by the
nonheap warning instead of also changing the site to freed.

### Leaks

A leak requires an allocated site at an ordinary return with no release,
escape, or unresolved allocation status. Unknown calls, returned ownership,
published aggregates, globals, and noreturn paths suppress the claim. Fallible
out-parameter allocators are correlated with their status convention:
`posix_memalign` succeeds at zero, while `asprintf` and `vasprintf` succeed at
a nonnegative result.

### Out of bounds

Exact constant accesses report when any byte falls below offset zero or beyond
the allocation extent. Affine loop ranges additionally require the access
block itself to execute on every path to the latch and require a single loop
exit; proving only that the pointer expression executes is insufficient.
Unknown, wrapping, conditional, side-exit, or runtime-trip ranges stay silent,
and merely forming a one-past pointer is legal.

### Uninitialized reads

The checker tracks a bounded normalized set of proven initialized byte ranges
for a unique allocated site, preserving holes between separately written
members. Unknown-size or may-alias writes widen toward silence; `calloc` and
modeled fully-writing libc calls mark bytes initialized. Lowering records the
semantic member-byte ranges for compiler-generated aggregate copies. Their
lifetime and bounds checks cover the full representation, while the
uninitialized-read check examines members and deliberately ignores padding.
Union or over-complex layouts widen toward silence rather than guessing an
active member. Aggregate initialization is transferred source-before-
destination, so self-copy and heap-to-heap propagation preserve missing-member
state.

### Freeing nonheap storage

The warning requires every possible target to be stack, global, function, or
string storage, or an exact heap target with a proven nonzero interior offset.
Unknown parameters, restrict objects without a concrete origin, and mixed
heap/nonheap sets are not proof and remain silent.

### Zero-size reallocation

This check is off by default because C17 leaves zero-size `realloc` behavior
implementation-defined. It fires only when the size or product is constant
zero; the lifetime state widens instead of assuming whether the old object was
released or a new object exists.

`-Wmem-strict` is off by default. Its first member,
`-Wmem-use-after-free-unknown`, warns when a freed pointer is passed to an
unknown function that may dereference it. The default tier instead treats an
unknown call as an ownership escape. Sprint 43 replaces that broad rule with
interprocedural summaries.

### Reallocation correlation

`realloc(p, n)` creates two correlated abstract paths. A non-null result means
success and marks the old allocation freed; a null result means failure and
leaves it allocated. Consequently, use of `p` under `if (q)` is diagnosed,
while use in the failure branch is not. The analysis never infers anything
from equality between the old and new addresses because a successful
reallocation may return the same address.

### Proof traces

Each warning is followed by the ordered `MsTrace` events that establish the
claim: allocation, release/reallocation, relevant branch, escape or return.
Path splits copy persistent traces, so only events from the proof path are
rendered. Warning policy is applied at the primary occurrence; notes follow
only when that warning remains enabled.

## False-positive budget

The default-tier law is: no `-Wmem` diagnostics on the pinned musl corpus
translation units that the current front end can lower to analysis IR. The CI
lane pins musl commit `b306b16af15c89a04d8e0c55cac2dadbeb39c083`, analyzes
732 of 1,361 x86-64 source files, records the 629 files deferred by GNU syntax
scheduled for Sprint 55, requires zero memory diagnostics, and completes in
under 90 seconds. The baseline pins separate SHA-256 digests of the normalized,
sorted analyzed and deferred identity sets in addition to their counts, so a
front-end change cannot silently swap files between the sets.
Only exit status 1 is accepted as an unsupported-syntax deferral. ICE, tool,
signal, and other compiler failures fail the gate; a fake-compiler meta test
pins that distinction.

This is narrower than “all musl TUs are clean”: deferred files do not reach
IR and therefore are not evidence for or against the analyzer. When frontend
support grows, the baseline must be deliberately updated and every newly
analyzable file immediately enters the zero-warning budget.

If a default heuristic produces a false positive, move that heuristic to
`WG_MEM_STRICT` in `src/warn/warnings.def`, add firing and silent regression
fixtures for its new policy, and update this document. Do not add a silent
allowlist. A genuine finding in the pinned corpus requires a reviewed entry
with the source location and musl commit.

## One points-to engine, two clients

Optimization and memory-safety analysis share the points-to service in
`src/opt/alias.{c,h}`. The optimizer and memsafe checker are two clients of
that one service.

Allocation-site identity, points-to sets, byte-offset ranges, and escape facts
belong to the alias service. Code under `src/memsafe/` may attach lifetime and
trace state to an allocation site, but it must not implement a second
points-to or field-offset model. This keeps an aliasing correction visible to
both clients and prevents the optimizer and diagnostics from disagreeing
about object identity.

Alias queries are pure for the lifetime of an `AliasCtx`. Any IR mutation
invalidates the context; a client must rebuild it before issuing another
query.

Pointer values keep byte-offset hulls when they move through local memory;
loads do not discard a known allocation base or constant interior offset.
The service also exposes allocation sites transitively reachable through
stored pointer contents. That reachability is how a returned or externally
passed aggregate conservatively escapes the allocations it captures; memsafe
does not recreate the graph walk.

## Pipeline boundary

Memory-safety analysis is a distinct, read-only post-optimization stage:

```text
parse -> sema -> analysis lowering -> optimizations -> memsafe -> codegen
```

It is deliberately not an optimizer-pass row. It runs at every optimization
level, does not join transform fixpoints, and returns no changed flag. When an
enabled memory warning or the private `CGF_MEMSAFE_DUMP=1` test gate needs the
analysis, the driver lowers a dedicated module that retains inline bodies,
verifies it, applies the selected optimization pipeline, and normalizes local
memory to SSA even at `-O0`. The ordinary emission module remains separate,
so analysis and dumps cannot change generated code. `-fsyntax-only` still
runs this stage and therefore reports `-Wmem` findings without codegen.

## Lifetime state and bounded paths

Each abstract allocation site has one lifetime state per tracked path:

- `unallocated`
- `allocated`
- `freed`
- `escaped`
- `unknown`

Different states join to `unknown`. The important `allocated` versus `freed`
distinction remains split only while the path budget permits it. `free(NULL)`
is a no-op and does not change lifetime state.

Path sensitivity is intentionally bounded:

| Budget | Limit | Behavior at the limit |
| --- | ---: | --- |
| States per block | 8 | Join excess states pairwise. |
| Path splits per function | 256 | Stop splitting and join remaining paths. |
| Predicate terms per path | 4 | Drop the eldest predicate. |

Exceeding a budget degrades silently to conservative may-analysis. Loss of
precision may suppress a warning, but a join must never invent an illegal
transition or a path that cannot execute.

## Event traces

Every later memory diagnostic is backed by an ordered event trace. The event
vocabulary covers allocation, release, reallocation, escape, use, branches,
calls, and returns. Events retain their source location and a stable note.

The rendering contract is a warning at the use or violation site followed by
one note per proof event in program order, for example allocation, release,
and a realloc-success branch. A path split copies the trace, and only the
trace belonging to the surviving proof path is rendered. Sprint 41 built the
data model; Sprint 42 renders it as user-visible `-Wmem` diagnostics.

## Position relative to other tools

This table describes the intended comparison surface. It does not claim that
the future Cgfried layers are already available.

| Tool or mode | Relevant strength | Important limit | Cgfried position |
| --- | --- | --- | --- |
| GCC 8 warnings | Provides scattered compile-time checks including `-Wfree-nonheap-object` and some `-Wuninitialized` diagnostics. | It has no unified path-sensitive memory checker comparable to later GCC analyzer releases. | GCC 8 remains the command-line and diagnostic parity baseline, but the L1/L2 design intentionally goes beyond its memory checks. |
| GCC 10+ `-fanalyzer` | Provides the real static-analysis comparator for intraprocedural and interprocedural memory diagnostics. | Analyzer precision and false positives depend on modeled calls and path budgets. | The optional `make test-mem-fanalyzer` target records both verdicts for exactly 20 overlap cases. GCC is a comparator, not an oracle for Cgfried's narrower default policy. |
| AddressSanitizer | Dynamically detects a broader set of exercised bugs, including stack and global out-of-bounds accesses through shadow memory, and initialization-order bugs. | It detects only exercised paths and requires its instrumentation and runtime model. | L3 deliberately targets fewer bug classes. Its design goal is production deployment without shadow memory, with no runtime dependency beyond `libcgf_rt.a`, plus static discharge of checks that can be proved unnecessary. L3 does not replace ASan. |

Static analysis and runtime checking are complementary. A successful static
analysis means only that no supported violation was proved under that layer's
contract. A successful runtime execution means only that no supported check
failed on the paths that ran.
