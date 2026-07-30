// The conversion matrix: widen/narrow int, int<->fp, ptr<->int, _Bool.
// FLAGS: -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: sext i8
// IR_CHECK: zext i8
// IR_CHECK: trunc i64
// IR_CHECK: sitofp i32
// IR_CHECK: fptosi f64
// IR_CHECK: fpext f32
// IR_CHECK: fptrunc f64
// IR_CHECK: bitcast ptr
// IR_CHECK: bitcast i64
long casts(signed char sc, unsigned char uc, long l, double d, float fl,
           int *p) {
    int a = sc;
    unsigned b = uc;
    int c = (int)l;
    double e = a;
    int f = (int)d;
    double g = fl;
    float h = (float)d;
    long i = (long)p;
    int *q = (int *)i;
    _Bool bb = l;
    return a + b + c + (long)e + f + (long)g + (long)h + i + (q != 0) + bb;
}
