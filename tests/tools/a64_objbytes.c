/* Print an AArch64 object's raw text-section bytes as lowercase hex.
 * Supports the ELF64 little-endian objects emitted by GNU/LLVM assemblers
 * and the Mach-O 64 little-endian objects emitted by afs-as. */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void die(const char *path, const char *why)
{
    fprintf(stderr, "a64_objbytes: %s: %s\n", path, why);
    exit(1);
}

static uint32_t rd32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
           (uint32_t)p[3] << 24;
}

static uint64_t rd64(const unsigned char *p)
{
    return (uint64_t)rd32(p) | (uint64_t)rd32(p + 4) << 32;
}

static int inside(size_t size, uint64_t off, uint64_t len)
{
    return off <= size && len <= (uint64_t)size - off;
}

static void print_bytes(const unsigned char *p, uint64_t n)
{
    uint64_t i;
    for (i = 0; i < n; i++)
        printf("%02x", p[i]);
    putchar('\n');
}

static int elf_text(const unsigned char *b, size_t size)
{
    uint64_t shoff, stroff, strsize;
    uint16_t shentsize, shnum, shstrndx, i;
    const unsigned char *str;

    if (size < 64 || memcmp(b, "\177ELF", 4) != 0 || b[4] != 2 || b[5] != 1)
        return 0;
    if (b[18] != 183 || b[19] != 0) /* EM_AARCH64 */
        return -1;
    shoff = rd64(b + 40);
    shentsize = (uint16_t)(b[58] | b[59] << 8);
    shnum = (uint16_t)(b[60] | b[61] << 8);
    shstrndx = (uint16_t)(b[62] | b[63] << 8);
    if (shentsize < 64 || shstrndx >= shnum ||
        !inside(size, shoff, (uint64_t)shentsize * shnum))
        return -1;
    stroff = rd64(b + shoff + (uint64_t)shentsize * shstrndx + 24);
    strsize = rd64(b + shoff + (uint64_t)shentsize * shstrndx + 32);
    if (!inside(size, stroff, strsize))
        return -1;
    str = b + stroff;
    for (i = 0; i < shnum; i++) {
        const unsigned char *sh = b + shoff + (uint64_t)shentsize * i;
        uint32_t name = rd32(sh);
        uint64_t off = rd64(sh + 24), len = rd64(sh + 32);
        if (name < strsize && memchr(str + name, 0, (size_t)(strsize - name)) &&
            strcmp((const char *)str + name, ".text") == 0) {
            if (len == 0 || !inside(size, off, len))
                return -1;
            print_bytes(b + off, len);
            return 1;
        }
    }
    return -1;
}

static int name16_eq(const unsigned char *p, const char *s)
{
    size_t n = strlen(s);
    return n <= 16 && memcmp(p, s, n) == 0 && (n == 16 || p[n] == 0);
}

static int macho_text(const unsigned char *b, size_t size)
{
    uint32_t ncmds, sizeofcmds, i;
    uint64_t pos;

    if (size < 32 || rd32(b) != 0xfeedfacfU)
        return 0;
    if (rd32(b + 4) != 0x0100000cU) /* CPU_TYPE_ARM64 */
        return -1;
    ncmds = rd32(b + 16);
    sizeofcmds = rd32(b + 20);
    if (!inside(size, 32, sizeofcmds))
        return -1;
    pos = 32;
    for (i = 0; i < ncmds; i++) {
        uint32_t cmd, cmdsize;
        if (!inside(size, pos, 8))
            return -1;
        cmd = rd32(b + pos);
        cmdsize = rd32(b + pos + 4);
        if (cmdsize < 8 || !inside(size, pos, cmdsize))
            return -1;
        if (cmd == 0x19U) { /* LC_SEGMENT_64 */
            uint32_t nsects, j;
            uint64_t sec;
            if (cmdsize < 72)
                return -1;
            nsects = rd32(b + pos + 64);
            if ((uint64_t)nsects > ((uint64_t)cmdsize - 72) / 80)
                return -1;
            sec = pos + 72;
            for (j = 0; j < nsects; j++, sec += 80) {
                if (name16_eq(b + sec, "__text")) {
                    uint64_t len = rd64(b + sec + 40);
                    uint64_t off = rd32(b + sec + 48);
                    if (len == 0 || !inside(size, off, len))
                        return -1;
                    print_bytes(b + off, len);
                    return 1;
                }
            }
        }
        pos += cmdsize;
    }
    return -1;
}

int main(int argc, char **argv)
{
    FILE *fp;
    unsigned char *buf;
    long end = 0;
    int found;

    if (argc != 2) {
        fprintf(stderr, "usage: a64_objbytes object\n");
        return 2;
    }
    fp = fopen(argv[1], "rb");
    if (!fp)
        die(argv[1], strerror(errno));
    if (fseek(fp, 0, SEEK_END) != 0 || (end = ftell(fp)) < 0 ||
        fseek(fp, 0, SEEK_SET) != 0)
        die(argv[1], "cannot determine file size");
    buf = malloc(end ? (size_t)end : 1);
    if (!buf)
        die(argv[1], "out of memory");
    if ((size_t)end != fread(buf, 1, (size_t)end, fp) || fclose(fp) != 0)
        die(argv[1], "read failed");

    found = elf_text(buf, (size_t)end);
    if (found == 0)
        found = macho_text(buf, (size_t)end);
    free(buf);
    if (found <= 0)
        die(argv[1], found == 0 ? "unsupported object format"
                                : "malformed object or missing text section");
    return 0;
}
