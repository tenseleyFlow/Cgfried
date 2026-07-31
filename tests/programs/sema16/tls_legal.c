// FLAGS: -fdump-sema -fcommon
// (explicit -fcommon: the Sprint 26 default is -fno-common)
// CHECK: var a: int [external] [tentative] [tls] [common]
// CHECK: var b: int [internal] [defined] [tls]
// CHECK: var c: int [external] [defined] [tls]
// Every legal combination, with the tls bit visible in the dump: alone at
// file scope, static, extern, and the block-scope forms WITH static or
// extern. The initializer of a thread-local is a static-style constant
// (thread storage duration means no code runs at its birth).
_Thread_local int a;
static _Thread_local int b = 1;
extern _Thread_local int cdecl_only;
_Thread_local int c = 2;
void f(void) {
    static _Thread_local int local_static;
    extern _Thread_local int cdecl_only;
    (void)local_static;
}
