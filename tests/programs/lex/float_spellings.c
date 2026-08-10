// FLAGS: --dump-tokens
// CHECK: FLOAT_CONST 1.5 double
// CHECK: FLOAT_CONST .5f float
// CHECK: FLOAT_CONST 0x1.8p3 double
// CHECK: FLOAT_CONST 1.0L long double
// CHECK: FLOAT_CONST 1.0F32 _Float32
// CHECK: FLOAT_CONST 1.0F64 _Float64
// CHECK: FLOAT_CONST 1.0F32x _Float32x
// CHECK: FLOAT_CONST 1.0F64x _Float64x
// CHECK: FLOAT_CONST 1.0F128 _Float128
// Tokens keep the EXACT spelling while the dump reports every classified
// FloatConstType without indexing past a partial name table.
1.5 .5f 0x1.8p3 1.0L 1.0F32 1.0F64 1.0F32x 1.0F64x 1.0F128
