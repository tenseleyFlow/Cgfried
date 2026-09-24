static unsigned char code_bytes[16];
static int begin_calls;
static int end_calls;

static void *begin_address(void)
{
    begin_calls++;
    return code_bytes;
}

static void *end_address(void)
{
    end_calls++;
    return code_bytes + sizeof(code_bytes);
}

int main(void)
{
    __builtin___clear_cache(begin_address(), end_address());
    return begin_calls != 1 || end_calls != 1;
}
