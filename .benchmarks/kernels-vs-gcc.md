# Cgfried kernel code-generation comparison

Static instruction counts cover only `kernel_run`; alignment nops are excluded. Text is the complete object `.text` size in bytes. GCC is a visibility reference, not a gate. Runtime ratios above 1.5x are marked for issue follow-up; they never fail this generator.

## Provenance

- `cgf`: `cgfried 0.1.0 (x86_64-linux-gnu)`
- `x86_64-linux-gnu.gcc`: `gcc: gcc (GCC) 16.1.1 20260728`
- `x86_64-linux-gnu.objdump`: `objdump: GNU objdump (GNU Binutils) 2.47`
- `x86_64-linux-gnu.readelf`: `readelf: GNU readelf (GNU Binutils) 2.47`
- `x86_64-linux-gnu.as`: `as: GNU assembler (GNU Binutils) 2.47`
- `arm64-linux.gcc`: `aarch64-linux-gnu-gcc: aarch64-linux-gnu-gcc (GCC) 16.1.0`
- `arm64-linux.objdump`: `aarch64-linux-gnu-objdump: GNU objdump (GNU Binutils) 2.47`
- `arm64-linux.readelf`: `aarch64-linux-gnu-readelf: GNU readelf (GNU Binutils) 2.47`
- `arm64-linux.as`: `aarch64-linux-gnu-as: GNU assembler (GNU Binutils) 2.47`
- Runtime: not recorded (fleet-only; static columns remain deterministic)

## x86_64-linux-gnu

| Kernel | Opt | cgf insns | gcc insns | cgf .text | gcc .text | runtime cgf/gcc |
|---|---:|---:|---:|---:|---:|---:|
| alloca-arena | -Os | 67 | 25 | 326 | 63 | — |
| atomic-counter | -Os | 21 | 17 | 145 | 70 | — |
| bitfield-ops | -Os | 138 | 28 | 498 | 76 | — |
| branchy-parser | -Os | 127 | 41 | 450 | 122 | — |
| crc32-table | -Os | 99 | 39 | 354 | 152 | — |
| fib-recurse | -Os | 24 | 8 | 263 | 71 | — |
| float-dot | -Os | 61 | 30 | 274 | 144 | — |
| hash-fnv | -Os | 42 | 13 | 277 | 45 | — |
| horner | -Os | 44 | 11 | 357 | 45 | — |
| int-div-heavy | -Os | 66 | 33 | 454 | 93 | — |
| matmul-64 | -Os | 155 | 66 | 548 | 218 | — |
| memcpy-loop | -Os | 53 | 27 | 228 | 125 | — |
| memset-loop | -Os | 49 | 2 | 367 | 6 | — |
| pointer-chase | -Os | 78 | 27 | 290 | 88 | — |
| qsort-calls | -Os | 67 | 34 | 324 | 178 | — |
| sieve | -Os | 83 | 40 | 290 | 161 | — |
| snprintf-loop | -Os | 48 | 24 | 360 | 108 | — |
| strlen-loop | -Os | 25 | 2 | 173 | 6 | — |
| struct-copy-heavy | -Os | 127 | 49 | 484 | 212 | — |
| switch-dispatch | -Os | 114 | 49 | 386 | 133 | — |
| varargs-call-loop | -Os | 34 | 19 | 576 | 213 | — |
| alloca-arena | -O2 | 67 | 125 | 631 | 543 | — |
| atomic-counter | -O2 | 21 | 17 | 145 | 95 | — |
| bitfield-ops | -O2 | 138 | 25 | 498 | 126 | — |
| branchy-parser | -O2 | 127 | 44 | 450 | 160 | — |
| crc32-table | -O2 | 99 | 61 | 354 | 318 | — |
| fib-recurse | -O2 | 24 | 15 | 263 | 904 | — |
| float-dot | -O2 | 61 | 57 | 481 | 272 | — |
| hash-fnv | -O2 | 42 | 36 | 277 | 159 | — |
| horner | -O2 | 44 | 18 | 357 | 106 | — |
| int-div-heavy | -O2 | 66 | 72 | 454 | 346 | — |
| matmul-64 | -O2 | 155 | 102 | 548 | 412 | — |
| memcpy-loop | -O2 | 53 | 97 | 394 | 501 | — |
| memset-loop | -O2 | 49 | 2 | 367 | 6 | — |
| pointer-chase | -O2 | 78 | 66 | 530 | 307 | — |
| qsort-calls | -O2 | 67 | 54 | 534 | 297 | — |
| sieve | -O2 | 83 | 52 | 290 | 236 | — |
| snprintf-loop | -O2 | 48 | 24 | 360 | 114 | — |
| strlen-loop | -O2 | 25 | 2 | 173 | 6 | — |
| struct-copy-heavy | -O2 | 127 | 84 | 484 | 435 | — |
| switch-dispatch | -O2 | 114 | 63 | 386 | 324 | — |
| varargs-call-loop | -O2 | 34 | 19 | 576 | 268 | — |
| alloca-arena | -O3 | 67 | 205 | 631 | 811 | — |
| atomic-counter | -O3 | 21 | 17 | 145 | 95 | — |
| bitfield-ops | -O3 | 138 | 25 | 943 | 126 | — |
| branchy-parser | -O3 | 127 | 44 | 847 | 160 | — |
| crc32-table | -O3 | 99 | 218 | 660 | 940 | — |
| fib-recurse | -O3 | 24 | 25 | 263 | 926 | — |
| float-dot | -O3 | 61 | 92 | 481 | 624 | — |
| hash-fnv | -O3 | 42 | 36 | 277 | 159 | — |
| horner | -O3 | 44 | 2 | 357 | 9 | — |
| int-div-heavy | -O3 | 66 | 72 | 454 | 346 | — |
| matmul-64 | -O3 | 155 | 215 | 548 | 1068 | — |
| memcpy-loop | -O3 | 53 | 16 | 394 | 101 | — |
| memset-loop | -O3 | 49 | 2 | 367 | 6 | — |
| pointer-chase | -O3 | 78 | 66 | 530 | 307 | — |
| qsort-calls | -O3 | 67 | 58 | 534 | 396 | — |
| sieve | -O3 | 83 | 107 | 527 | 493 | — |
| snprintf-loop | -O3 | 48 | 24 | 360 | 114 | — |
| strlen-loop | -O3 | 25 | 2 | 173 | 6 | — |
| struct-copy-heavy | -O3 | 127 | 132 | 910 | 735 | — |
| switch-dispatch | -O3 | 114 | 63 | 718 | 324 | — |
| varargs-call-loop | -O3 | 34 | 19 | 576 | 236 | — |

## arm64-linux

| Kernel | Opt | cgf insns | gcc insns | cgf .text | gcc .text | runtime cgf/gcc |
|---|---:|---:|---:|---:|---:|---:|
| alloca-arena | -Os | 55 | 22 | 348 | 88 | — |
| atomic-counter | -Os | 23 | 16 | 212 | 64 | — |
| bitfield-ops | -Os | 101 | 24 | 456 | 96 | — |
| branchy-parser | -Os | 107 | 44 | 484 | 176 | — |
| crc32-table | -Os | 77 | 35 | 364 | 140 | — |
| fib-recurse | -Os | 23 | 9 | 304 | 124 | — |
| float-dot | -Os | 54 | 28 | 276 | 112 | — |
| hash-fnv | -Os | 40 | 17 | 348 | 68 | — |
| horner | -Os | 39 | 15 | 344 | 60 | — |
| int-div-heavy | -Os | 45 | 23 | 384 | 92 | — |
| matmul-64 | -Os | 126 | 65 | 560 | 260 | — |
| memcpy-loop | -Os | 49 | 25 | 244 | 100 | — |
| memset-loop | -Os | 48 | 2 | 404 | 8 | — |
| pointer-chase | -Os | 64 | 27 | 308 | 108 | — |
| qsort-calls | -Os | 59 | 35 | 348 | 164 | — |
| sieve | -Os | 71 | 42 | 332 | 168 | — |
| snprintf-loop | -Os | 48 | 25 | 408 | 100 | — |
| strlen-loop | -Os | 26 | 2 | 228 | 8 | — |
| struct-copy-heavy | -Os | 115 | 52 | 512 | 208 | — |
| switch-dispatch | -Os | 138 | 57 | 600 | 228 | — |
| varargs-call-loop | -Os | 43 | 21 | 612 | 216 | — |
| alloca-arena | -O2 | 55 | 23 | 604 | 112 | — |
| atomic-counter | -O2 | 23 | 16 | 212 | 68 | — |
| bitfield-ops | -O2 | 101 | 24 | 456 | 96 | — |
| branchy-parser | -O2 | 107 | 48 | 484 | 192 | — |
| crc32-table | -O2 | 77 | 40 | 364 | 160 | — |
| fib-recurse | -O2 | 23 | 16 | 304 | 896 | — |
| float-dot | -O2 | 54 | 46 | 464 | 192 | — |
| hash-fnv | -O2 | 40 | 20 | 348 | 88 | — |
| horner | -O2 | 39 | 20 | 344 | 88 | — |
| int-div-heavy | -O2 | 45 | 45 | 384 | 184 | — |
| matmul-64 | -O2 | 126 | 70 | 560 | 280 | — |
| memcpy-loop | -O2 | 49 | 50 | 412 | 220 | — |
| memset-loop | -O2 | 48 | 17 | 404 | 80 | — |
| pointer-chase | -O2 | 64 | 29 | 536 | 128 | — |
| qsort-calls | -O2 | 59 | 45 | 556 | 224 | — |
| sieve | -O2 | 71 | 49 | 332 | 200 | — |
| snprintf-loop | -O2 | 48 | 25 | 408 | 104 | — |
| strlen-loop | -O2 | 26 | 2 | 228 | 8 | — |
| struct-copy-heavy | -O2 | 115 | 61 | 512 | 244 | — |
| switch-dispatch | -O2 | 138 | 71 | 600 | 296 | — |
| varargs-call-loop | -O2 | 43 | 21 | 612 | 252 | — |
| alloca-arena | -O3 | 55 | 154 | 604 | 632 | — |
| atomic-counter | -O3 | 23 | 16 | 212 | 68 | — |
| bitfield-ops | -O3 | 101 | 24 | 832 | 96 | — |
| branchy-parser | -O3 | 107 | 48 | 884 | 192 | — |
| crc32-table | -O3 | 77 | 88 | 644 | 352 | — |
| fib-recurse | -O3 | 23 | 23 | 304 | 928 | — |
| float-dot | -O3 | 54 | 65 | 464 | 264 | — |
| hash-fnv | -O3 | 40 | 20 | 348 | 88 | — |
| horner | -O3 | 39 | 4 | 344 | 16 | — |
| int-div-heavy | -O3 | 45 | 45 | 384 | 184 | — |
| matmul-64 | -O3 | 126 | 176 | 560 | 704 | — |
| memcpy-loop | -O3 | 49 | 28 | 412 | 124 | — |
| memset-loop | -O3 | 48 | 17 | 404 | 80 | — |
| pointer-chase | -O3 | 64 | 29 | 536 | 128 | — |
| qsort-calls | -O3 | 59 | 52 | 556 | 240 | — |
| sieve | -O3 | 71 | 135 | 588 | 540 | — |
| snprintf-loop | -O3 | 48 | 25 | 408 | 104 | — |
| strlen-loop | -O3 | 26 | 2 | 228 | 8 | — |
| struct-copy-heavy | -O3 | 115 | 77 | 944 | 308 | — |
| switch-dispatch | -O3 | 138 | 71 | 1124 | 296 | — |
| varargs-call-loop | -O3 | 43 | 21 | 612 | 188 | — |
