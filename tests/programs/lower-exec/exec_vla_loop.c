// The restore-discipline stress: a goto exiting two VLA scopes, run
// 100000 times — stack growth means a missed restore.
// EXIT_CODE: 0
int f(int n, int m)
{
    {
        int a[n];
        a[0] = n;
        {
            int b[m];
            b[0] = m;
            if (a[0] + b[0] > 0)
                goto out;
        }
    }
out:
    return 0;
}
int main(void)
{
    int i;
    for (i = 0; i < 100000; i++)
        if (f(17, 9) != 0)
            return 1;
    return 0;
}
