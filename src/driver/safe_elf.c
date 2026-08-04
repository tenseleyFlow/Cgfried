#include "driver/safe_elf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Tiny, bounds-checked ELF64LE reader. The safe-link rule is a compiler
 * contract, so it must not depend on readelf being installed and must not
 * accept a coincidental byte substring outside a real SHT_NOTE section. */

static u16 get16(const u8 *p)
{
    return (u16)p[0] | (u16)((u16)p[1] << 8);
}

static u32 get32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static u64 get64(const u8 *p)
{
    return (u64)get32(p) | ((u64)get32(p + 4) << 32);
}

static bool range_ok(size_t size, u64 off, u64 len)
{
    return off <= (u64)size && len <= (u64)size - off;
}

static bool note_payload_ok(const u8 *data, size_t size, u64 off, u64 len)
{
    u64 end;

    if (!range_ok(size, off, len))
        return false;
    end = off + len;
    while (off + 12 <= end) {
        u32 namesz = get32(data + off);
        u32 descsz = get32(data + off + 4);
        u32 type = get32(data + off + 8);
        u64 name_off = off + 12;
        u64 desc_off = name_off + (((u64)namesz + 3u) & ~(u64)3u);
        u64 next = desc_off + (((u64)descsz + 3u) & ~(u64)3u);

        if (next > end)
            return false;
        if (namesz == 4 && descsz == 4 && type == 1 &&
            memcmp(data + name_off, "CGF\0", 4) == 0 &&
            get32(data + desc_off) == 1)
            return true;
        off = next;
    }
    return false;
}

static SafeElfResult inspect_elf(const u8 *data, size_t size)
{
    u64 shoff, str_off, str_size;
    u16 shentsize, shnum, shstrndx;
    const u8 *str_sh;
    u16 i;

    if (size < 64 || memcmp(data, "\177ELF", 4) != 0 || data[4] != 2 ||
        data[5] != 1 || data[6] != 1)
        return SAFE_ELF_INVALID;
    shoff = get64(data + 40);
    shentsize = get16(data + 58);
    shnum = get16(data + 60);
    shstrndx = get16(data + 62);
    if (shentsize < 64 || shnum == 0 || shstrndx >= shnum ||
        !range_ok(size, shoff, (u64)shentsize * shnum))
        return SAFE_ELF_INVALID;
    str_sh = data + shoff + (u64)shentsize * shstrndx;
    str_off = get64(str_sh + 24);
    str_size = get64(str_sh + 32);
    if (!range_ok(size, str_off, str_size))
        return SAFE_ELF_INVALID;

    for (i = 0; i < shnum; i++) {
        const u8 *sh = data + shoff + (u64)shentsize * i;
        u32 name = get32(sh);
        u32 type = get32(sh + 4);
        u64 sec_off = get64(sh + 24);
        u64 sec_size = get64(sh + 32);
        const char *sname;
        size_t remain;

        if ((u64)name >= str_size)
            return SAFE_ELF_INVALID;
        sname = (const char *)data + str_off + name;
        remain = (size_t)(str_size - name);
        if (!memchr(sname, '\0', remain))
            return SAFE_ELF_INVALID;
        if (strcmp(sname, ".note.cgf.safe") == 0) {
            if (type != 7 || !note_payload_ok(data, size, sec_off, sec_size))
                return SAFE_ELF_INVALID;
            return SAFE_ELF_NOTE_PRESENT;
        }
    }
    return SAFE_ELF_NOTE_MISSING;
}

SafeElfResult safe_elf_note_status(const char *path)
{
    FILE *f;
    long end;
    u8 *data;
    size_t got;
    SafeElfResult result;

    f = fopen(path, "rb");
    if (!f)
        return SAFE_ELF_IO_ERROR;
    if (fseek(f, 0, SEEK_END) != 0 || (end = ftell(f)) < 0 ||
        fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return SAFE_ELF_IO_ERROR;
    }
    data = malloc((size_t)end ? (size_t)end : 1);
    if (!data) {
        fclose(f);
        return SAFE_ELF_IO_ERROR;
    }
    got = fread(data, 1, (size_t)end, f);
    if (fclose(f) != 0 || got != (size_t)end) {
        free(data);
        return SAFE_ELF_IO_ERROR;
    }
    result = inspect_elf(data, got);
    free(data);
    return result;
}

static bool explicitly_allowed(const DriverArgs *args, const char *path)
{
    size_t i;

    for (i = 0; i < args->fsafe_allow_unsafe.len; i++)
        if (strcmp(args->fsafe_allow_unsafe.data[i], path) == 0)
            return true;
    return false;
}

bool safe_link_inputs_ok(const DriverArgs *args)
{
    size_t i;

    for (i = 0; i < args->link_inputs.len; i++) {
        const LinkInput *input = &args->link_inputs.data[i];
        SafeElfResult result;

        /* A raw linker argument can name an object, archive, response file,
         * or linker script.  The driver cannot attest the files it causes
         * ld to open, so a safe link rejects the whole user-controlled raw
         * channel.  Driver-generated linker controls never enter this list. */
        if (input->kind == LINK_RAW) {
            fprintf(stderr,
                    "cgfried: error: -fsafe rejects user-supplied linker "
                    "option '%s'; pass safe objects directly or perform "
                    "the custom link in a non-safe link step\n",
                    input->val ? input->val : "");
            return false;
        }
        /* -l libraries are an explicit system/toolchain boundary. Every
         * directly named object/archive remains a user input. */
        if (input->kind != LINK_OBJ || !input->val ||
            explicitly_allowed(args, input->val))
            continue;
        result = safe_elf_note_status(input->val);
        if (result == SAFE_ELF_NOTE_PRESENT)
            continue;
        if (result == SAFE_ELF_NOTE_MISSING)
            fprintf(stderr,
                    "cgfried: error: -fsafe link input '%s' has no "
                    ".note.cgf.safe; rebuild it with -fsafe or pass "
                    "-fsafe-allow-unsafe=%s\n",
                    input->val, input->val);
        else if (result == SAFE_ELF_INVALID)
            fprintf(stderr,
                    "cgfried: error: -fsafe link input '%s' is not a "
                    "supported safe ELF object; rebuild it with -fsafe or "
                    "allow it explicitly\n",
                    input->val);
        else
            fprintf(stderr,
                    "cgfried: error: cannot read -fsafe link input '%s'\n",
                    input->val);
        return false;
    }
    return true;
}
