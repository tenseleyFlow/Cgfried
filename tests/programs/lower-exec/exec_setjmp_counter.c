// Returning through setjmp three times; the memory-pinned locals keep
// their values across the longjmps.
// OPT_EQ: all
// EXIT_CODE: 3
typedef char jbuf[256];
int setjmp(char *);
void longjmp(char *, int);
jbuf buf;
int main(void)
{
    volatile int count = 0;
    if (setjmp(buf) < 3) {
        count = count + 1;
        if (count < 3)
            longjmp(buf, count);
    }
    return count;
}
