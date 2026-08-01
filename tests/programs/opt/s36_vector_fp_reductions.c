// OPT_EQ: all
// OFAST_DIVERGENCE_OK: fp-reduction-reassoc
// CHECK: fp-reductions 36 2

extern int printf(const char *, ...);

static float add_values[8];
static float mul_values[8];

float reduce_add(void)
{
    long i;
    float sum = 0.0f;

    for (i = 0; i < 8; i++)
        sum += add_values[i];
    return sum;
}

float reduce_mul(void)
{
    long i;
    float product = 1.0f;

    for (i = 0; i < 8; i++)
        product *= mul_values[i];
    return product;
}

int main(void)
{
    long i;

    for (i = 0; i < 8; i++)
        add_values[i] = (float)(i + 1);
    for (i = 0; i < 8; i++)
        mul_values[i] = i == 7 ? 2.0f : 1.0f;
    printf("fp-reductions %.0f %.0f\n", (double)reduce_add(),
           (double)reduce_mul());
    return 0;
}
