// goto out of TWO VLA scopes: ONE restore of the OUTERMOST token
// (restoring outermost subsumes inner).
// FLAGS: -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: stacksave
// IR_CHECK: stacksave
// IR_CHECK: stackrestore
// IR_CHECK: br L.done
int f(int n, int m) {
    {
        int a[n];
        a[0] = 1;
        {
            int b[m];
            b[0] = 2;
            if (n > m) goto done;
            a[0] += b[0];
        }
    }
done:
    return n;
}
