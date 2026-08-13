// EXIT_CODE: 0
// CAMP-SQLITE-005: a block-scope static pointer table may use addresses of
// nested array members in a file-scope object. The relocation is based at the
// complete object and carries the member-plus-index byte addend.
struct Stream {
    int value;
};

struct Console {
    struct Stream designated[3];
    struct Stream setup[3];
};

static struct Console console = {
    {{10}, {11}, {12}},
    {{20}, {21}, {22}},
};

static struct Stream *known(unsigned index)
{
    static struct Stream *streams[] = {
        &console.designated[1],
        &console.designated[2],
        &console.setup[1],
        &console.setup[2],
    };

    return streams[index];
}

int main(void)
{
    if (known(0) != &console.designated[1] || known(0)->value != 11)
        return 1;
    if (known(1) != &console.designated[2] || known(1)->value != 12)
        return 2;
    if (known(2) != &console.setup[1] || known(2)->value != 21)
        return 3;
    if (known(3) != &console.setup[2] || known(3)->value != 22)
        return 4;
    return 0;
}
