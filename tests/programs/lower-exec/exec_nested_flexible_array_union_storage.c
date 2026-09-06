// FLAGS: -std=gnu17
// EXIT_CODE: 0
// A selected FAM-bearing member may use bytes that belong to its fixed-size
// enclosing union. The union remains the object's semantic and emitted size.
struct Bytes {
    int tag;
    unsigned char data[];
};

union Storage {
    struct Bytes bytes;
    unsigned char backing[16];
};

static const union Storage text = {{11, "wx"}};
static const union Storage sparse = {
    .bytes = {.tag = 12, .data = {[3] = 99}},
};

struct Wrapped {
    int before;
    union Storage storage;
    int after;
};

static const struct Wrapped wrapped = {13, {{14, {21, 22, 23}}}, 15};

_Static_assert(sizeof text == sizeof(union Storage), "FAM changed union size");
_Static_assert(sizeof wrapped == sizeof(struct Wrapped),
               "FAM changed wrapper size");

static int local_static(void)
{
    static const union Storage local = {{16, {'a', 'b'}}};

    return local.bytes.tag == 16 && local.bytes.data[0] == 'a' &&
           local.bytes.data[1] == 'b';
}

int main(void)
{
    if (text.bytes.tag != 11 || text.bytes.data[0] != 'w' ||
        text.bytes.data[1] != 'x' || text.bytes.data[2] != 0)
        return 1;
    if (sparse.bytes.tag != 12 || sparse.bytes.data[0] != 0 ||
        sparse.bytes.data[1] != 0 || sparse.bytes.data[2] != 0 ||
        sparse.bytes.data[3] != 99)
        return 2;
    if (wrapped.before != 13 || wrapped.storage.bytes.tag != 14 ||
        wrapped.storage.bytes.data[0] != 21 ||
        wrapped.storage.bytes.data[1] != 22 ||
        wrapped.storage.bytes.data[2] != 23 || wrapped.after != 15)
        return 3;
    if (!local_static())
        return 4;
    return 0;
}
