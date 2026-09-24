/* libcgf_rt: AArch64 instruction-cache synchronization.
 *
 * GCC and Clang lower __builtin___clear_cache through a compiler-runtime
 * symbol on AArch64. Darwin supplies that symbol in libSystem; Linux does not
 * make it available through Cgfried's freestanding link line, so libcgf_rt
 * provides the standard libgcc-compatible name there. */

#if defined(__aarch64__) && !defined(__APPLE__)

typedef unsigned long cache_addr;

static void clean_data_line(cache_addr address)
{
    __asm__ volatile("dc cvau, %0" : : "r"(address) : "memory");
}

static void invalidate_instruction_line(cache_addr address)
{
    __asm__ volatile("ic ivau, %0" : : "r"(address) : "memory");
}

void __clear_cache(void *begin, void *end)
{
    cache_addr start = (cache_addr)begin;
    cache_addr stop = (cache_addr)end;
    cache_addr ctr;
    cache_addr data_line;
    cache_addr instruction_line;
    cache_addr address;

    if (start >= stop)
        return;

    __asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));
    data_line = 4UL << ((ctr >> 16) & 15UL);
    instruction_line = 4UL << (ctr & 15UL);

    address = start & ~(data_line - 1);
    for (;;) {
        clean_data_line(address);
        if (stop - address <= data_line)
            break;
        address += data_line;
    }
    __asm__ volatile("dsb ish" : : : "memory");

    address = start & ~(instruction_line - 1);
    for (;;) {
        invalidate_instruction_line(address);
        if (stop - address <= instruction_line)
            break;
        address += instruction_line;
    }
    __asm__ volatile("dsb ish\n\tisb" : : : "memory");
}

#endif
