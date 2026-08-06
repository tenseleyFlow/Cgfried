// FLAGS: -S --target=arm64-linux
// ERROR_EXPECTED: returning a homogeneous floating-point aggregate (AAPCS64 v0-v3 return) is not lowered yet: lands in Sprint 51
// AAPCS64 returns 1-4 homogeneous FP leaves in v0-v3 and passes NO hidden
// pointer. abi_classify_ret has recognized that since Sprint 14, and nothing
// consumed it: `hidden` was computed in exactly two places and both listed
// only SRET and PAIR, so lowering built a value return into a void function
// and the verifier caught it as an ICE.
//
// It cannot simply borrow the sret shape -- sret passes a hidden pointer in
// x8 and an HFA passes nothing -- so the backend needs a distinct IrAbiRet
// to tell them apart. Until that lands this is a clean error rather than a
// crash. `struct { float x, y, z; }` is common in graphics and math code,
// not an exotic corner. See .docs/audits/abi-debt.md, ABI-001.
//
// Found by the Sprint 51 cross-determinism probe, which compiles one TU for
// every target in the closed set. It reproduces natively on arm64 too: the
// gap is the ABI, not the cross path.
struct Vec3 {
    float x, y, z;
};

struct Vec3 scale(struct Vec3 v, float k)
{
    struct Vec3 r;

    r.x = v.x * k;
    r.y = v.y * k;
    r.z = v.z * k;
    return r;
}
