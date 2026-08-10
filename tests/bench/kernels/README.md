# Sprint 53 code-generation kernels

Each source is a standalone program whose hot work is anchored in the global
`noinline` function `kernel_run`. `REPS` controls runtime without changing the
algorithm's checked final result. Every `main` writes the result to a volatile
sink and exits with status 1 if its deterministic checksum is wrong.

The fixed checksums use three strategies: published reference vectors
(`crc32-table`), direct mathematical identities or bounded reference loops
(`float-dot`, `horner`, `alloca-arena`, and repetition-count results), and
literal golden values independently checked with both GCC and Cgfried for the
remaining fixed-input integer kernels. All arithmetic that may wrap uses
unsigned fixed-width types.
