// OPT-H-01: self-referential pointer-content offsets must converge without
// licensing an optimizer to change postfix increment/decrement semantics.
// ENV: CGF_VERIFY_AFTER_EACH=1
// OPT_EQ: all
// EXIT_CODE: 0

static int objects[3] = {11, 22, 33};
static int *cursor;

static int advance(void)
{
    int *old;

    cursor = &objects[1];
    old = cursor++;
    return *old + *cursor;
}

static int retreat(void)
{
    int *old;

    cursor = &objects[1];
    old = cursor--;
    return *old + *cursor;
}

int main(void)
{
    return advance() != 55 || retreat() != 33;
}
