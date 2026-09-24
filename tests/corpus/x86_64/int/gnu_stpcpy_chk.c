static struct {
    char left_guard;
    char destination[4];
    char right_guard;
} guarded = {'L', {'x', 'x', 'x', 'x'}, 'R'};
static const char source_buffer[] = "abc";
static int destination_calls;
static int source_calls;
static int extent_calls;

_Static_assert(__builtin_object_size(guarded.destination, 1) == 4,
               "exact destination subobject size");

static char *destination(void)
{
    destination_calls++;
    return guarded.destination;
}

static const char *source(void)
{
    source_calls++;
    return source_buffer;
}

static __SIZE_TYPE__ extent(void)
{
    extent_calls++;
    return __builtin_object_size(guarded.destination, 1);
}

int main(void)
{
    char *result =
        __builtin___stpcpy_chk(destination(), source(), extent());

    if (result != guarded.destination + 3 || *result != '\0')
        return 1;
    if (destination_calls != 1 || source_calls != 1 || extent_calls != 1)
        return 2;
    if (guarded.left_guard != 'L' || guarded.destination[0] != 'a' ||
        guarded.destination[1] != 'b' || guarded.destination[2] != 'c' ||
        guarded.destination[3] != '\0' || guarded.right_guard != 'R')
        return 3;
    return 0;
}
