// OPT_EQ: all
// Hand-rolled binary search over a sorted array.
// EXIT_CODE: 0
static int find(const int *v, int n, int key)
{
    int lo = 0, hi = n - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if (v[mid] == key)
            return mid;
        if (v[mid] < key)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return -1;
}
int main(void)
{
    int v[8] = {2, 3, 5, 7, 11, 13, 17, 19};
    if (find(v, 8, 11) != 4)
        return 1;
    if (find(v, 8, 2) != 0)
        return 2;
    if (find(v, 8, 19) != 7)
        return 3;
    if (find(v, 8, 4) != -1)
        return 4;
    return 0;
}
