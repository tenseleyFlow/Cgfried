// RESOLVED(audit): SEMA-H-05 unary negation of a signed minimum is accepted as constant
enum { NEG_OVERFLOW = -(-2147483647 - 1) };
