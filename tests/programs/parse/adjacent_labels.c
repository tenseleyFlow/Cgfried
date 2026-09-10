// FLAGS: --dump-ast
// CHECK: SWITCH x
// CHECK: LABEL_SEQUENCE
// CHECK: CASE 10
// CHECK: LABEL shared
// CHECK: CASE 11
// CHECK: CASE 12
// CHECK: BREAK
void f(int x)
{
    switch (x) {
    case 10:
    shared:
    case 11:
    case 12:
        break;
    }
}
