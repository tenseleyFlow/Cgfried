// FLAGS: -fsafe
// CHECK: 3
#include <stdio.h>

struct pair {
    int *first;
    int *second;
};

int main(void)
{
    int first = 1;
    int second = 2;
    struct pair values = {&first, &second};

    printf("%d\n", *values.first + *values.second);
    return 0;
}
