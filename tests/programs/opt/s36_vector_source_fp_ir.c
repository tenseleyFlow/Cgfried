// FLAGS: -Ofast -emit-ir
// IR_CHECK: vreduce_add v4f32
// IR_CHECK: vreduce_mul v4f32

static float add_values[8];
static float mul_values[8];

float reduce_add(void)
{
    long i;
    float value = 0.0f;
    for (i = 0; i < 8; i++)
        value += add_values[i];
    return value;
}

float reduce_mul(void)
{
    long i;
    float value = 1.0f;
    for (i = 0; i < 8; i++)
        value *= mul_values[i];
    return value;
}
