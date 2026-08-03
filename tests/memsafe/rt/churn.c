void *malloc(unsigned long);
void free(void *);

int main(void)
{
    void *blocks[64];
    int round;
    int i;

    for (round = 0; round < 64; round++) {
        for (i = 0; i < 64; i++) {
            blocks[i] = malloc((unsigned long)(i + 1));
            if (!blocks[i])
                return 2;
        }
        for (i = 0; i < 64; i++)
            free(blocks[i]);
    }
    return 0;
}
