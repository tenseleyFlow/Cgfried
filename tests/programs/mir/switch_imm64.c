// A qword cmp/sub has only a sign-extended imm32 encoding. The sparse switch
// therefore materializes 0x80000000 with movl's free zero extension, while
// the dense table materializes its true 64-bit minimum with movabs before
// subtracting it from the table index.
// FLAGS: --target=x86_64-linux-gnu -emit-mir
// MIR_CHECK: mir @sparse
// MIR_CHECK: = mov.l $2147483648
// MIR_CHECK: cmp.q r
// MIR_CHECK: mir @dense
// MIR_CHECK: = movabs.q $4294967296
// MIR_CHECK: = sub.q r
// MIR_CHECK: jmptbl r
int sparse(long n)
{
    switch (n) {
    case 0x40000:
    case 0x40001:
    case 0x80000000:
        return 1;
    default:
        return 0;
    }
}

int dense(long n)
{
    switch (n) {
    case 0x100000000:
        return 1;
    case 0x100000001:
        return 2;
    case 0x100000002:
        return 3;
    case 0x100000003:
        return 4;
    case 0x100000004:
        return 5;
    default:
        return 0;
    }
}
