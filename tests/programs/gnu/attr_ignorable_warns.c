// FLAGS: -fsyntax-only -std=gnu17
// WARNING_EXPECTED: 'unused' attribute directive ignored
// Ignoring an attribute is never SILENT: docs/gnu-extensions.md has no tier
// for that, because an attribute quietly dropped is indistinguishable from
// one honoured until the program misbehaves. The flag is gcc's own
// -Wattributes, default on, so -Wno-attributes turns it off the way a
// reader expects (attr_ignorable.c relies on exactly that).
static int x __attribute__((unused));

int main(void)
{
    return x;
}
