// FLAGS: --dump-ast
// CHECK: EMPTY_DECL struct S
// CHECK: DECL a: int
// CHECK: DECL b: char [bitfield]
// CHECK: DECL <abstract>: unsigned int [bitfield]
// CHECK: EMPTY_DECL union U
// CHECK: EMPTY_DECL struct Outer
// CHECK: DECL <abstract>: struct <anon>
// CHECK: DECL y: int
// CHECK: DECL fwd: ptr to struct Fwd
// An UNNAMED bitfield (`unsigned : 0;`) has no declarator at all, and an
// untagged struct member with no declarator is a C11 anonymous member —
// two different no-name cases that must not be confused. `struct Fwd *`
// is legal without a definition: pointer-to-incomplete is fine, and the
// tag it introduces has file scope.
struct S {
    int a;
    char b : 3;
    unsigned : 0;
};
union U { int i; float f; };
struct Outer {
    struct { int x; };
    int y;
};
struct Fwd *fwd;
