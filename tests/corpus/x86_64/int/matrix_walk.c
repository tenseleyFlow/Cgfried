// 2-D indexing: the lea scale fold on the inner subscript.
// EXIT_CODE: 66
// ASM_CHECK(x86_64-linux-gnu): leaq ({{%r[a-z0-9]+}},{{%r[a-z0-9]+}},4)
int main(void)
{
    int m[3][4];
    int i, j, s = 0;
    for (i = 0; i < 3; i++)
        for (j = 0; j < 4; j++)
            m[i][j] = i * 4 + j;
    for (i = 0; i < 3; i++)
        for (j = 0; j < 4; j++)
            s += m[i][j];
    return s;
}
