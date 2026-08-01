// OPT_EQ: all
// EXIT_CODE: 0

int *escaped;

static void keep(int *p)
{
    escaped = p;
}

int main(void)
{
    int x = 37;
    keep(&x);
    return *escaped != 37;
}
