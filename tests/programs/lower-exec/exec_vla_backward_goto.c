// IR-C-04 executable stress: same-compound and nested backward gotos must
// reclaim every VLA declared after the destination label.
// EXIT_CODE: 0
static int same_compound(int n)
{
    int count = 0;

again:;
    int values[n];
    values[0] = count++;
    if (count < 4096)
        goto again;
    return values[0] != 4095;
}

static int multiple_vlas(int n)
{
    int count = 0;

outer:;
    int retained[n];
middle:;
    int reclaimed[n + 1];
    retained[0] = 19;
    reclaimed[0] = count++;
    if (count < 2048)
        goto middle;
    if (count == 2048)
        goto outer;
    return retained[0] != 19 || reclaimed[0] != 2048;
}

static int nested(int n)
{
    int count = 0;

again:;
    int outer[n];
    {
        int inner[n + 1];

        outer[0] = 7;
        inner[0] = count++;
        if (count < 2048)
            goto again;
        return outer[0] != 7 || inner[0] != 2047;
    }
}

static int label_after_declaration(int n)
{
    int values[n];
    int count = 0;

again:
    values[0] = count++;
    if (count < 100)
        goto again;
    return values[0] != 99;
}

int main(void)
{
    return same_compound(1024) || multiple_vlas(1024) || nested(1024) ||
           label_after_declaration(1024);
}
