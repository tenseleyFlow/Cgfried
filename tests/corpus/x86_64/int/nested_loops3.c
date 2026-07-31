// 3-deep loop nest: register pressure across loop-carried counters.
// EXIT_CODE: 60
int main(void)
{
    int s = 0, i, j, k;
    for (i = 0; i < 3; i++)
        for (j = 0; j < 4; j++)
            for (k = 0; k < 5; k++)
                s++;
    return s;
}
