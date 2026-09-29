// FLAGS: -fsyntax-only -std=gnu17
// ERROR_EXPECTED: currently supports only a 16-byte vector whose element type
// Sprint 56.65 admits exactly the release-blocking one-lane TI vector. This
// ordinary four-lane int shape pins the surrounding refusal boundary.
typedef int v4si __attribute__((vector_size(16)));
