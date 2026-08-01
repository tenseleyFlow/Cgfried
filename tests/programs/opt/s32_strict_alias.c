/* Used by scripts/strict_alias_diff.sh.  Passing the same union member
 * storage through float* and int* invokes undefined behavior under strict
 * aliasing deliberately: it makes the mode switch observable. */
int f(float *fp, int *ip)
{
    *fp = 1.0f;
    *ip = 2;
    return (int)*fp;
}

int main(void)
{
    union {
        float f;
        int i;
    } value;

    return f(&value.f, &value.i);
}
