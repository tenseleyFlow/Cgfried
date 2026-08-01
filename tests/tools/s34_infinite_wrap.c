int main(void)
{
    unsigned char i;

    for (i = 0; i != 255; i = (unsigned char)(i + 2))
        ;
    return 0;
}
