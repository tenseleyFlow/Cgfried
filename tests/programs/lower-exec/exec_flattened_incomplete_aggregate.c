// EXIT_CODE: 0
// An incomplete outer array is sized by initialized ELEMENTS, not by the raw
// number of scalar items consumed under brace elision. Static declarations,
// automatic declarations and incomplete array compound literals share the
// same current-object walk.
typedef struct Row {
    long pair[2];
    long tail;
} Row;

static Row static_rows[] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
static Row designated_rows[] = {[2] = {{10, 11}, 12}};

int main(void)
{
    Row local_rows[] = {13, 14, 15, 16, 17, 18};

    if (sizeof static_rows / sizeof static_rows[0] != 3)
        return 1;
    if (static_rows[0].pair[0] != 1 || static_rows[1].pair[1] != 5 ||
        static_rows[2].tail != 9)
        return 2;
    if (sizeof designated_rows / sizeof designated_rows[0] != 3 ||
        designated_rows[0].tail != 0 || designated_rows[2].pair[1] != 11 ||
        designated_rows[2].tail != 12)
        return 3;
    if (sizeof local_rows / sizeof local_rows[0] != 2 ||
        local_rows[0].tail != 15 || local_rows[1].pair[0] != 16 ||
        local_rows[1].tail != 18)
        return 4;
    if (sizeof((Row[]){19, 20, 21, 22, 23, 24}) != 2 * sizeof(Row))
        return 5;
    return 0;
}
