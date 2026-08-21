// EXIT_CODE: 0
// OPT_EQ: all
// ENV: CGF_VERIFY_AFTER_EACH=1

struct pair {
    int first;
    int second;
};

static volatile struct pair source = {11, 31};
static volatile struct pair sink;
static int source_calls;
static int sink_calls;

static volatile struct pair *get_source(void)
{
    source_calls++;
    return &source;
}

static volatile struct pair *get_sink(void)
{
    sink_calls++;
    return &sink;
}

int main(void)
{
    struct pair local = *get_source();

    *get_sink() = local;
    if (source_calls != 1 || sink_calls != 1)
        return 1;
    if (local.first != 11 || local.second != 31)
        return 2;
    if (sink.first != 11 || sink.second != 31)
        return 3;
    return 0;
}
