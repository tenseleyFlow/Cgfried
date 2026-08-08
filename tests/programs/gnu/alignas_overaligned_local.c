// FLAGS: -fsyntax-only
// ERROR_EXPECTED: an automatic object aligned more strictly than 16 bytes
// The frame base is 16-aligned, so aligning an offset within it cannot
// deliver more than 16 -- it would produce a 64-aligned OFFSET from a
// 16-aligned base and call the object 64-aligned. A realigned frame is
// Sprint 53, so this is refused by name.
//
// It is refused in LOWERING rather than in a backend because this is valid C
// meeting an unimplemented feature: the backend's ICE says "this is a bug in
// cgfried", which is the wrong thing to tell someone who wrote a correct
// program. The backend checks remain as unreachable backstops -- and one of
// them had to be ADDED, because arm64 had refused over-aligned stack objects
// since Sprint 48 while x86_64 silently under-aligned them.
int main(void)
{
    _Alignas(64) int loc = 2;

    return loc;
}
