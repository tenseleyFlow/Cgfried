// FLAGS: -std=c17
// OPT_EQ: all

int main(void)
{
    unsigned char i;
    unsigned sum = 0;

    for (i = 250; i != 6; i = (unsigned char)(i + 3))
        sum += (unsigned char)(i * 7 + 5);
    return sum == 490 ? 0 : 1;
}
