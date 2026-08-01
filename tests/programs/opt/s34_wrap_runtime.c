// FLAGS: -std=c17
// OPT_EQ: all

int main(void)
{
    unsigned char i = 0;
    unsigned count = 0;
    unsigned checksum = 0;

    while (i < 250) {
        checksum += i;
        count++;
        i = (unsigned char)(i + 100);
        if (count > 256)
            return 1;
    }
    if (count != 23 || i != 252 || checksum != 2772)
        return 2;
    return 0;
}
