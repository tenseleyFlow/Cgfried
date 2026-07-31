// FP comparisons as VALUES (the setcc recipes) and as branches.
// EXIT_CODE: 0
int main(void)
{
    volatile double a = 1.0, b = 2.0;
    int lt = a<b, le = a <= b, gt = a> b, ge = a >= b;
    int eq = a == a, ne = a != b;
    if (lt + le + eq + ne != 4)
        return 1;
    if (gt + ge != 0)
        return 2;
    if (a < b) {
        if (b < a)
            return 3;
    } else {
        return 4;
    }
    return 0;
}
