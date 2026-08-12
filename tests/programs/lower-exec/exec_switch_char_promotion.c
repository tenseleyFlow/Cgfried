// EXIT_CODE: 0
// A switch performs the integer promotions before lowering.  The member load
// shape deliberately leaves a pointer value in the same physical register a
// byte load can reuse; lowering the controlling expression as i8 made the
// jump table retain those dirty upper bits and crash the cgf-built QBE.
struct Entry {
    char tag;
};

static int classify(struct Entry *entry)
{
    switch (entry->tag) {
    case 0:
        return 10;
    case 2:
        return 12;
    case 3:
        return 13;
    case 4:
        return 14;
    case 5:
        return 15;
    case 6:
        return 16;
    case 7:
        return 17;
    default:
        return 99;
    }
}

int main(void)
{
    struct Entry entry = {3};

    return classify(&entry) == 13 ? 0 : 1;
}
