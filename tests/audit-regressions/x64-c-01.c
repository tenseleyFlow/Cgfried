// RESOLVED(audit): X64-C-01 floating atomic accesses are emitted as ordinary loads and stores
_Atomic double atomic_double;
_Atomic long double atomic_long_double;

void store_double(double value) {
    atomic_double = value;
}

double load_double(void) {
    return atomic_double;
}

void store_long_double(long double value) {
    atomic_long_double = value;
}

long double load_long_double(void) {
    return atomic_long_double;
}
