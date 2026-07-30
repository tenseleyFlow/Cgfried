// FLAGS: -fsyntax-only
// ERROR_EXPECTED: lands in Sprint 14
// gcc accepts this; we cannot yet, because the operand needs a size and
// layout is Sprint 14's. The deferral NAMES its sprint rather than
// silently folding sizeof to a guess — which is the whole point of the
// no-silent-stubs rule. Moved out of the parse accept corpus (where it
// was written in Sprint 9 to test parsing) once sema began running.
_Static_assert(sizeof(int) >= 2, "int too small");
