// FLAGS: -fsyntax-only
// WARN_COUNT: 1
// EXIT_CODE: 0
void *malloc(unsigned long);

static void keep_only_locally(void *pointer)
{
    void *local_slots[1];

    local_slots[0] = pointer;
    (void)local_slots[0];
}

void local_store_fire(void)
{
    void *pointer = malloc(8);

    keep_only_locally(pointer);
    // WARN_CHECK: mem-leak allocated memory is not released before this return
    return;
}
