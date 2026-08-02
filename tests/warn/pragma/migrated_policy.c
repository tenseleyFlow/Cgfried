// FLAGS: -fsyntax-only -std=c99
// WARN_COUNT: 8
struct sprint37_outer {
    // WARN_CHECK: c11-extensions anonymous struct/union members are a C11 feature
    struct { int member; };
};

// WARN_CHECK: c23-extensions ISO C forbids empty initializer braces before C23
int sprint37_empty_initializer[1] = {};

// WARN_CHECK: empty-declaration declaration does not declare anything
int;

// WARN_CHECK: invalid-function-specifier variable 'sprint37_not_function' declared 'inline'
inline int sprint37_not_function;

// WARN_CHECK: tentative-definition-array array 'sprint37_tentative' assumed to have one element
int sprint37_tentative[];

typedef int sprint37_typedef;
// WARN_CHECK: typedef-redefinition redefinition of typedef 'sprint37_typedef' is a C11 feature
typedef int sprint37_typedef;

// WARN_CHECK: visibility declared inside parameter list will not be visible
void sprint37_proto(struct sprint37_hidden *);

// WARN_CHECK: zero-length-array ISO C forbids zero-size arrays
int sprint37_zero[0];
