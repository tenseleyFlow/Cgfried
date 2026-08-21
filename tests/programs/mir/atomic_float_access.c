// X64-C-01: floating atomic accesses must not fall through the ordinary
// SSE/x87 load-store paths, which neither guarantee indivisibility nor retain
// seq_cst ordering.
// FLAGS: -emit-mir
// MIR_CHECK: = xchg.l
// MIR_CHECK: = load.l
// MIR_CHECK: = xchg.q
// MIR_CHECK: = load.q
// MIR_CHECK: call [rip @__atomic_store_16]
// MIR_CHECK: call [rip @__atomic_load_16]
// MIR_CHECK: mir @store_plain_double
// MIR_CHECK: fstore.q
// MIR_CHECK: mir @load_plain_double
// MIR_CHECK: fload.q
_Atomic float atomic_float;
_Atomic double atomic_double;
_Atomic long double atomic_long_double;
double plain_double;

void store_float(float value)
{
    atomic_float = value;
}
float load_float(void)
{
    return atomic_float;
}
void store_double(double value)
{
    atomic_double = value;
}
double load_double(void)
{
    return atomic_double;
}
void store_long_double(long double value)
{
    atomic_long_double = value;
}
long double load_long_double(void)
{
    return atomic_long_double;
}
void store_plain_double(double value)
{
    plain_double = value;
}
double load_plain_double(void)
{
    return plain_double;
}
