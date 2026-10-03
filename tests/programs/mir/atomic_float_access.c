// X64-C-01: floating atomic accesses must not fall through the ordinary
// SSE/x87 load-store paths, which neither guarantee indivisibility nor retain
// seq_cst ordering.
// FLAGS: -emit-mir
// Exact-width integer carriers keep atomic F32/F64 accesses out of the
// floating load/store paths. On x86, seq_cst stores use an indivisible aligned
// integer store followed by the backend's ordering point.
// MIR_CHECK: mir @store_float
// MIR_CHECK: store.l
// MIR_CHECK: mfence.q
// MIR_CHECK: mir @load_float
// MIR_CHECK: = load.l
// MIR_CHECK: mir @store_double
// MIR_CHECK: store.q
// MIR_CHECK: mfence.q
// MIR_CHECK: mir @load_double
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
