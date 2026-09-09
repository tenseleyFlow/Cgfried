// EXIT_CODE: 0
// A block-scope static is still a file-lifetime linker symbol. Its explicit
// asm label must replace the compiler's ordinary internal `name.N` spelling,
// even when another declaration legitimately owns that spelling.
// ASM_CHECK: {{^block_local[.]0:}}
// ASM_CHECK: {{^block_local:}}

int block_local_global __asm__("block_local.0") = 9;

static int next_block_local(void)
{
    static int block_local __asm__("block_local") = 8;

    return block_local++;
}

int main(void)
{
    if (next_block_local() != 8)
        return 1;
    if (next_block_local() != 9)
        return 2;
    return block_local_global != 9;
}
