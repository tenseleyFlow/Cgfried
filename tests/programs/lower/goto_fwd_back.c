// Forward and backward goto, and a goto OVER a declaration — legal C:
// the jumped-over object is simply uninitialized (undef on read).
// FLAGS: -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: L.fwd
// IR_CHECK: L.back
int f(int n)
{
    int acc = 0;
    if (n > 5)
        goto fwd;
    acc = 100;
back:
    acc += n;
fwd:
    if (acc == 0)
        goto back;
    {
        goto over;
        int dead = 42;
        acc += dead;
    over:
        acc += 1;
    }
    goto inner_fixed;
    {
        int entered;
    inner_fixed:
        entered = 7;
        acc += entered;
    }
    return acc;
}
