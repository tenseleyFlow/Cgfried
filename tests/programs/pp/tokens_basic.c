// FLAGS: -E
// ENV: CGF_PP_DUMP_TOKENS=1
// CHECK: ident int
// CHECK: ident main
// CHECK: punct (
// CHECK: ident void
// CHECK: punct )
// CHECK: punct {
// CHECK: ident return
// CHECK: ppnum 42
// CHECK: punct ;
// CHECK: punct }
int main(void) { return 42; }
