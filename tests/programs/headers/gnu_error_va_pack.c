// FLAGS: -std=gnu17 -emit-ir
// glibc's <bits/error.h> defines extern-always-inline wrappers whose final
// call operand is __va_arg_pack(). The wrapper must disappear before IR while
// the caller's anonymous pointer reaches libc's real `error` symbol.
// IR_CHECK: call void @error
// IR_CHECK: anon) va
// IR_CHECK-NOT: __builtin_va_arg_pack
#include <error.h>

void probe(const char *text)
{
    error(0, 0, "%s", text);
}
