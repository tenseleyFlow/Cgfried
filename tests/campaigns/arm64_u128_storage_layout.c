struct _aarch64_ctx {
    unsigned int magic;
    unsigned int size;
};

struct fpsimd_context {
    struct _aarch64_ctx head;
    unsigned int fpsr;
    unsigned int fpcr;
    __uint128_t vregs[32];
};

struct user_fpsimd_struct {
    __uint128_t vregs[32];
    unsigned int fpsr;
    unsigned int fpcr;
};

_Static_assert(sizeof(__uint128_t) == 16, "storage width");
_Static_assert(_Alignof(__uint128_t) == 16, "storage alignment");
_Static_assert(sizeof(struct fpsimd_context) == 528, "signal context size");
_Static_assert(_Alignof(struct fpsimd_context) == 16,
               "signal context alignment");
_Static_assert(__builtin_offsetof(struct fpsimd_context, vregs) == 16,
               "signal vector offset");
_Static_assert(sizeof(struct user_fpsimd_struct) == 528,
               "ptrace context size");
_Static_assert(_Alignof(struct user_fpsimd_struct) == 16,
               "ptrace context alignment");
_Static_assert(__builtin_offsetof(struct user_fpsimd_struct, vregs) == 0,
               "ptrace vector offset");

int main(void)
{
    return 0;
}
