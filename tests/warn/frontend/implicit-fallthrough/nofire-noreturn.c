// FLAGS: -S -Wimplicit-fallthrough=3
// WARN_COUNT: 0

_Noreturn void fallthrough_stop(void);

int fallthrough_noreturn(int x)
{
    switch (x) {
    case 1:
        fallthrough_stop();
    case 2:
        return x;
    }
    return 0;
}
