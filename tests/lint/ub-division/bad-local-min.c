// OPT_EQ: all
// EXIT_CODE: 0
#include <limits.h>
int main(void)
{
    const int minimum = INT_MIN;
    const int minus_one = -1;
    return minimum / minus_one;
}
