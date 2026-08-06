// FLAGS: -emit-ir
// IR integer types are signless, so a backend that must widen an argument
// cannot recover the signedness. Only Apple's arm64 ABI makes the CALLER
// widen -- its callee reads w0 raw -- but the FACT is target-independent and
// is recorded on every target, which is what makes this fixture testable on
// x86_64. The consumer is marshal_call in src/cg/arm64/regalloc.c.
//
// Nothing 32 bits or wider carries a marker: there is no widening duty when
// the value already fills the unit an argument register holds. The verifier
// rejects that combination, so a stray flag is loud rather than silent.
int narrow(signed char a, unsigned char b, short c, unsigned short d,
           _Bool e, int f, unsigned g, long h);

// IR_CHECK: call i32 @narrow(i8 %16 sext, i8 %17 zext, i16 %18 sext, i16 %19 zext, i8 %20 zext, i32 %21, i32 %22, i64 %23)
int pass(signed char a, unsigned char b, short c, unsigned short d, _Bool e,
         int f, unsigned g, long h)
{
    return narrow(a, b, c, d, e, f, g, h);
}

// A variadic call promotes its anonymous arguments, so `anon` and the
// widening flags never meet there.
int printf(const char *, ...);

// IR_CHECK: call i32 @printf(ptr @.Lstr.0, i32 %3 anon)
int promoted(signed char a)
{
    return printf("%d", a);
}
