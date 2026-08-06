// FLAGS: --target=arm64-linux -emit-ir
// AAPCS64 passes AND returns a homogeneous floating aggregate of 1-4 leaves
// in v0-v3, with no hidden pointer at runtime. Both halves were classified
// since Sprint 14 and neither was consumed: `hidden` listed only SRET and
// PAIR, so a return built a value into a void function and the verifier
// caught it as an ICE; an argument fell through to byval and travelled as a
// pointer where gcc uses s0-s2.
//
// The IR keeps the sret SHAPE for the return -- a hidden pointer the callee
// builds into -- exactly as the 16-byte pairs do, with abi(hfa_f32,3)
// carrying the register truth. The ARGUMENT becomes N scalars, which is the
// eightbyte path with the leaf width as its stride; using 8 for a float HFA
// would gather every other leaf.
//
// Verified by mixed link against gcc in BOTH directions under qemu. That
// mattered: the callee half was right while the caller still read x0:x1,
// and a same-compiler test agrees with itself either way.
// IR_CHECK: func void @scale(ptr %0, f32 %1, f32 %2, f32 %3, f32 %4) abi(hfa_f32,3)
struct V3 {
    float x, y, z;
};

struct V3 scale(struct V3 v, float k)
{
    struct V3 r;

    r.x = v.x * k;
    r.y = v.y * k;
    r.z = v.z * k;
    return r;
}
