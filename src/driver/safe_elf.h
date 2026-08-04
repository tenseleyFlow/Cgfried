#ifndef CGF_DRIVER_SAFE_ELF_H
#define CGF_DRIVER_SAFE_ELF_H

#include <stdbool.h>

#include "driver/args.h"

typedef enum {
    SAFE_ELF_NOTE_PRESENT,
    SAFE_ELF_NOTE_MISSING,
    SAFE_ELF_INVALID,
    SAFE_ELF_IO_ERROR,
} SafeElfResult;

SafeElfResult safe_elf_note_status(const char *path);
bool safe_link_inputs_ok(const DriverArgs *args);

#endif
