// FLAGS: --dump-tokens
// CHECK: FLOAT_CONST 1.5 double
// CHECK: FLOAT_CONST .5f float
// CHECK: FLOAT_CONST 0x1.8p3 double
// CHECK: FLOAT_CONST 1.0L long double
// Tokens keep the EXACT spelling; correctly-rounded conversion is
// Sprint 15's job (debt XD-S08-FPHOST).
1.5 .5f 0x1.8p3 1.0L
