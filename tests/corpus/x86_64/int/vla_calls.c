// OPT_EQ: all
// A VLA in a function that ALSO passes arguments on the stack.
//
// This is the shape that catches the overlap, and it needs both halves at
// once. A dynamic alloca moves SP and hands back the new SP as the object's
// base; outgoing stack arguments are stored at [SP + k] against that same
// SP. So the argument list lands on top of the first bytes of the VLA, and
// the program computes a wrong answer with no diagnostic anywhere. Both
// backends did this -- x86 silently, arm64 behind an ICE that meant nobody
// ever got far enough to see it.
//
// `sink` takes ten arguments so four of them (two on AAPCS64) go to the
// stack, and the VLA is read AFTER the call so a clobber survives to the
// result. A VLA with no call, or a call with <= 6 arguments, passes either
// way -- which is why no existing corpus program found this.
//
// The loop VLA exercises the other half: an alloca inside a loop body must
// be released each iteration via stacksave/stackrestore, or SP walks down
// once per trip and a long-running loop exhausts the stack.
// CHECK: 270 736 500

#include <stdio.h>

static int sink(int a, int b, int c, int d, int e, int f, int g, int h, int i,
                int j)
{
    return a + b + c + d + e + f + g + h + i + j;
}

/* VLA written, then a stack-argument call, then the VLA read back. */
static int vla_across_call(int n)
{
    int a[n];
    int i, s = 0;

    for (i = 0; i < n; i++)
        a[i] = i * 3;
    s = sink(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9]);
    for (i = 0; i < n; i++)
        s += a[i];
    return s;
}

/* Two live VLAs plus a stack-argument call between them: the second alloca
 * must not land on the first, and neither may collide with the outgoing
 * area. */
static int two_vlas(int n)
{
    int a[n];
    int i, s;
    int b[n + 4];

    for (i = 0; i < n; i++)
        a[i] = i + 1;
    for (i = 0; i < n + 4; i++)
        b[i] = (i + 1) * 10;
    s = sink(a[0], b[0], a[1], b[1], a[2], b[2], a[3], b[3], a[4], b[4]);
    for (i = 0; i < n; i++)
        s += a[i];
    for (i = 0; i < n + 4; i++)
        s += b[i];
    return s;
}

/* An alloca inside a loop body must be released at the end of each
 * iteration, via the stacksave/stackrestore pair the scope emits. The VLA is
 * deliberately LARGE and the trip count high: at up to 64 KiB a trip, 1000
 * leaked iterations overrun any default stack and the program dies, whereas
 * a few-hundred-byte VLA could leak all day inside 8 MiB and the test would
 * pass with the release missing. Only the first and last elements are
 * touched, so the size costs nothing to run. */
static int vla_in_loop(int trips)
{
    int t, s = 0;

    for (t = 0; t < trips; t++) {
        int n = (t % 8 + 1) * 2048;
        int a[n];

        a[0] = t % 8;
        a[n - 1] = 1;
        s += a[0] + a[n - 1];
        s %= 1000;
    }
    return s;
}

int main(void)
{
    printf("%d %d %d\n", vla_across_call(10), two_vlas(6), vla_in_loop(1000));
    return 0;
}
