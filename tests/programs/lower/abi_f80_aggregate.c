// IR-C-01: an exact 16-byte aggregate containing only one x87 long double
// returns in st0, while the same source type remains a memory argument.
// FLAGS: -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: func f80 @bare(f80 %0)
// IR_CHECK: func f80 @ret_struct()
// IR_CHECK: func f80 @ret_union()
// IR_CHECK: func f80 @ret_array()
// IR_CHECK: func f80 @ret_nested()
// IR_CHECK: func void @ret_big(ptr %0) abi(sret)
// IR_CHECK: byval(16)
// IR_CHECK: call f80 @ret_struct()
struct One {
    long double value;
};

union OneUnion {
    long double value;
};

struct OneArray {
    long double value[1];
};

struct Inner {
    long double value;
};

struct Nested {
    struct Inner inner;
};

struct Big {
    long double value;
    char tail;
};

struct One one;
union OneUnion one_union;
struct OneArray one_array;
struct Nested nested;
struct Big big;

long double bare(long double value) { return value; }
struct One ret_struct(void) { return one; }
union OneUnion ret_union(void) { return one_union; }
struct OneArray ret_array(void) { return one_array; }
struct Nested ret_nested(void) { return nested; }
struct Big ret_big(void) { return big; }

void take(struct One, union OneUnion, struct OneArray, struct Nested,
          struct Big);

void calls(void)
{
    take(one, one_union, one_array, nested, big);
    ret_struct();
    ret_union();
    ret_array();
    ret_nested();
    ret_big();
}
