# Memory-safety architecture

Cgfried's memory-safety work is a six-layer ladder. Each layer adds one
mechanism and keeps its claim narrow enough to test. A layer's guarantee does
not imply the guarantees of later layers.

Sprint 41 supplies the shared analysis foundation only. It does not enable a
user-visible memory warning, runtime check, source rewrite, or safe-language
mode. In particular, `-Wmem` remains an unrecognized warning option until
Sprint 42.

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

It is deliberately not an optimizer-pass row. It must run at `-O0`, must not
join transform fixpoints, and returns no changed flag. When the private
`CGF_MEMSAFE_DUMP=1` test gate is active, the driver lowers a dedicated
analysis module that retains inline bodies, verifies it, optimizes it, and
then dumps memsafe results. The ordinary emission module remains separate, so
enabling the foundation dump cannot change generated code. Without the gate,
Sprint 41 performs no memsafe work and emits no memory diagnostic.

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
one note per event in program order, for example allocation, release, then
use. A path split copies the trace, and only the trace belonging to the
surviving proof path is rendered. Sprint 41 builds and tests this data model;
Sprint 42 is the first sprint that renders it as a user-visible `-Wmem`
diagnostic.

## Position relative to other tools

This table describes the intended comparison surface. It does not claim that
the future Cgfried layers are already available.

| Tool or mode | Relevant strength | Important limit | Cgfried position |
| --- | --- | --- | --- |
| GCC 8 warnings | Provides scattered compile-time checks including `-Wfree-nonheap-object` and some `-Wuninitialized` diagnostics. | It has no unified path-sensitive memory checker comparable to later GCC analyzer releases. | GCC 8 remains the command-line and diagnostic parity baseline, but the L1/L2 design intentionally goes beyond its memory checks. |
| GCC 10+ `-fanalyzer` | Provides the real static-analysis comparator for intraprocedural and interprocedural memory diagnostics. | Analyzer precision and false positives depend on modeled calls and path budgets. | On overlapping checks, L1/L2 target complete proof-chain event traces and the Sprint 42 musl zero-false-positive gate. These are targets until those layers ship. |
| AddressSanitizer | Dynamically detects a broader set of exercised bugs, including stack and global out-of-bounds accesses through shadow memory, and initialization-order bugs. | It detects only exercised paths and requires its instrumentation and runtime model. | L3 deliberately targets fewer bug classes. Its design goal is production deployment without shadow memory, with no runtime dependency beyond `libcgf_rt.a`, plus static discharge of checks that can be proved unnecessary. L3 does not replace ASan. |

Static analysis and runtime checking are complementary. A successful static
analysis means only that no supported violation was proved under that layer's
contract. A successful runtime execution means only that no supported check
failed on the paths that ran.
