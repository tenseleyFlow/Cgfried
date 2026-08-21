// OPT-H-01 control: disabling the default-on memory client must remain an
// independent way to compile the same valid self-referential pointer update.
// FLAGS: -O0 -Wno-mem -fsyntax-only

int object, *cursor;

int *advance(void)
{
    cursor = &object;
    return cursor++;
}
