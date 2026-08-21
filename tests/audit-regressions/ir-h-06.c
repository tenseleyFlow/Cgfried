// RESOLVED(audit): IR-H-06 sigsetjmp macro expansion loses the returns-twice marker
// glibc exposes sigsetjmp as a macro over __sigsetjmp. Lowering must recognize
// the actual called symbol so later passes keep the containing function in the
// conservative setjmp policy.
typedef char saved_state[256];
int __sigsetjmp(char *, int);
#define sigsetjmp(env, save_mask) __sigsetjmp((env), (save_mask))

int save(saved_state env)
{
    int local = 7;

    if (sigsetjmp(env, 1))
        return local;
    return 0;
}
