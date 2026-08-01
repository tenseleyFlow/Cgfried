// OPT_EQ: all
// EXIT_CODE: 0

int setjmp(char *);

int main(void)
{
    char state[256];
    int pinned = 41;

    if (setjmp(state) != 0)
        return 2;
    return pinned != 41;
}
