// FLAGS: -fsyntax-only
// ERROR_EXPECTED: only allowed in a function definition
// FE-H-02: identifier-list syntax is invalid at every nested function-type
// layer, including the typedef-shadowing sibling shape from the audit.
typedef int T;

void audit_shape(void)
{
    T T, (*pointer)(T);
}

int (*file_pointer)(a);
void parameter(int (*callback)(b));

struct Member {
    int (*callback)(c);
};

int (*returns_pointer(void))(d);

int type_name_size = sizeof(int (*)(e));
