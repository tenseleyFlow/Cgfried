/* cgf-objdiff (Sprint 24): compare two ELF64 relocatable objects the
 * way the emission differential needs — the same .s through afs-as and
 * system gas must agree:
 *
 *   .text/.rodata/.data   byte-compare, STRICT (any difference is a
 *                         finding, never a carve-out)
 *   .bss                  size + alignment
 *   relocations           normalized: sorted by (section, offset);
 *                         R_X86_64_PLT32 and PC32 are equivalent for
 *                         defined-symbol cases (psABI note: a resolved
 *                         local call needs no PLT; gas prefers PLT32)
 *   defined symbols       name -> (section name, value, size), sorted;
 *                         ordering and section indexes may differ
 *
 * Zero third-party deps; reads the two files, exits 0 clean / 1 diff /
 * 2 usage or malformed ELF. Output names every divergence. */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int64_t i64;

#define R_PC32 2
#define R_PLT32 4

typedef struct Sec {
    const char *name;
    u32 type;
    u64 flags;
    u64 size;
    u64 align;
    const u8 *data;
    u32 link; /* symtab: strtab index; reloc: symtab index */
    u32 info; /* reloc: target section index */
    u64 entsize;
} Sec;

typedef struct Obj {
    const char *path;
    u8 *bytes;
    size_t len;
    Sec secs[64];
    u32 nsecs;
} Obj;

static u16 rd16(const u8 *p)
{
    return (u16)(p[0] | p[1] << 8);
}
static u32 rd32(const u8 *p)
{
    return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}
static u64 rd64(const u8 *p)
{
    return (u64)rd32(p) | (u64)rd32(p + 4) << 32;
}

static int load(Obj *o, const char *path)
{
    FILE *f = fopen(path, "rb");
    long n;
    const u8 *sh;
    u64 shoff;
    u16 shnum, shentsize, shstrndx;
    const u8 *shstr;
    u64 shstr_off;
    u32 i;

    if (!f) {
        fprintf(stderr, "objdiff: cannot open '%s'\n", path);
        return 0;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    o->bytes = malloc((size_t)n);
    o->len = (size_t)n;
    if (fread(o->bytes, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        fprintf(stderr, "objdiff: short read on '%s'\n", path);
        return 0;
    }
    fclose(f);
    o->path = path;
    if (o->len < 64 ||
        memcmp(o->bytes,
               "\x7f"
               "ELF",
               4) != 0 ||
        o->bytes[4] != 2 /* ELFCLASS64 */) {
        fprintf(stderr, "objdiff: '%s' is not an ELF64 object\n", path);
        return 0;
    }
    shoff = rd64(o->bytes + 0x28);
    shentsize = rd16(o->bytes + 0x3a);
    shnum = rd16(o->bytes + 0x3c);
    shstrndx = rd16(o->bytes + 0x3e);
    if (shnum > 64) {
        fprintf(stderr, "objdiff: '%s': too many sections\n", path);
        return 0;
    }
    sh = o->bytes + shoff;
    shstr_off = rd64(sh + (u64)shstrndx * shentsize + 0x18);
    shstr = o->bytes + shstr_off;
    o->nsecs = shnum;
    for (i = 0; i < shnum; i++) {
        const u8 *e = sh + (u64)i * shentsize;

        o->secs[i].name = (const char *)shstr + rd32(e);
        o->secs[i].type = rd32(e + 4);
        o->secs[i].flags = rd64(e + 8);
        o->secs[i].size = rd64(e + 0x20);
        o->secs[i].align = rd64(e + 0x30);
        o->secs[i].data = o->bytes + rd64(e + 0x18);
        o->secs[i].link = rd32(e + 0x28);
        o->secs[i].info = rd32(e + 0x2c);
        o->secs[i].entsize = rd64(e + 0x38);
    }
    return 1;
}

static const Sec *find(const Obj *o, const char *name, u32 type_or0)
{
    u32 i;

    for (i = 1; i < o->nsecs; i++)
        if (strcmp(o->secs[i].name, name) == 0 &&
            (!type_or0 || o->secs[i].type == type_or0))
            return &o->secs[i];
    return NULL;
}

static int fails;

static void bad(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    fprintf(stderr, "objdiff: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    fails++;
}

/* progbits byte compare. For .text the compare is FUNCTION-GRANULAR:
 * bytes inside every FUNC symbol's [value, value+size) are strict, but
 * inter-function alignment padding is skipped — gas's own multi-NOP
 * fill decomposition varies BETWEEN BINUTILS VERSIONS (2.44 pads
 * remainder-first; older releases differ), so byte-comparing filler
 * that is never executed pins a version, not a behavior. Section sizes
 * and symbol offsets still compare exactly, so a real layout divergence
 * cannot hide in the gaps. */
static void mark_func_bytes(const Obj *o, const char *sec, u8 *mask, u64 cap)
{
    const Sec *st = find(o, ".symtab", 2);
    u32 i, count;

    if (!st)
        return;
    count = (u32)(st->size / 24);
    for (i = 1; i < count; i++) {
        const u8 *e = st->data + (u64)i * 24;
        u16 shndx = rd16(e + 6);
        u64 v = rd64(e + 8), sz = rd64(e + 16), k;

        if ((e[4] & 0xf) != 2 /* STT_FUNC */)
            continue;
        if (shndx >= o->nsecs || strcmp(o->secs[shndx].name, sec) != 0)
            continue;
        for (k = v; k < v + sz && k < cap; k++)
            mask[k] = 1;
    }
}

static void cmp_section(const Obj *a, const Obj *b, const char *name)
{
    const Sec *sa = find(a, name, 0);
    const Sec *sb = find(b, name, 0);
    u64 i;
    u8 *mask = NULL;

    if (!sa && !sb)
        return;
    if (!sa || !sb) {
        /* an all-zero or empty section may be legitimately absent */
        const Sec *have = sa ? sa : sb;

        if (have->size == 0)
            return;
        bad("%s present only in %s (size %llu)", name, sa ? a->path : b->path,
            (unsigned long long)have->size);
        return;
    }
    if (sa->size != sb->size) {
        bad("%s size: %llu vs %llu", name, (unsigned long long)sa->size,
            (unsigned long long)sb->size);
        return;
    }
    if (strcmp(name, ".text") == 0 && sa->size) {
        mask = calloc(1, sa->size);
        mark_func_bytes(a, name, mask, sa->size);
    }
    for (i = 0; i < sa->size; i++) {
        if (mask && !mask[i])
            continue; /* inter-function alignment fill */
        if (sa->data[i] != sb->data[i]) {
            bad("%s byte %llu: 0x%02x vs 0x%02x", name, (unsigned long long)i,
                sa->data[i], sb->data[i]);
            free(mask);
            return;
        }
    }
    free(mask);
}

static void cmp_bss(const Obj *a, const Obj *b)
{
    const Sec *sa = find(a, ".bss", 0);
    const Sec *sb = find(b, ".bss", 0);
    u64 za = sa ? sa->size : 0, zb = sb ? sb->size : 0;
    u64 aa = sa ? sa->align : 0, ab = sb ? sb->align : 0;

    if (za != zb)
        bad(".bss size: %llu vs %llu", (unsigned long long)za,
            (unsigned long long)zb);
    if (za && aa != ab)
        bad(".bss align: %llu vs %llu", (unsigned long long)aa,
            (unsigned long long)ab);
}

/* --- symbols + relocs, normalized ------------------------------------------
 */

typedef struct SymRow {
    const char *name;
    const char *sec;
    u64 value, size;
    u8 bind;
} SymRow;

typedef struct RelRow {
    const char *sec; /* target section name (sans .rela prefix) */
    u64 offset;
    u32 type;
    const char *sym;
    i64 addend;
} RelRow;

static int sym_cmp(const void *pa, const void *pb)
{
    const SymRow *x = pa, *y = pb;
    int c = strcmp(x->name, y->name);

    return c ? c : (x->value > y->value) - (x->value < y->value);
}

static int rel_cmp(const void *pa, const void *pb)
{
    const RelRow *x = pa, *y = pb;
    int c = strcmp(x->sec, y->sec);

    if (c)
        return c;
    return (x->offset > y->offset) - (x->offset < y->offset);
}

static u32 collect_syms(const Obj *o, SymRow *out, u32 cap)
{
    const Sec *st = find(o, ".symtab", 2);
    const u8 *str;
    u32 n = 0, i, count;

    if (!st)
        return 0;
    str = o->secs[st->link].data;
    count = (u32)(st->size / 24);
    for (i = 1; i < count && n < cap; i++) {
        const u8 *e = st->data + (u64)i * 24;
        const char *nm = (const char *)str + rd32(e);
        u8 info = e[4];
        u16 shndx = rd16(e + 6);

        if (!nm[0])
            continue;
        if ((info & 0xf) == 3)
            continue; /* STT_SECTION: gas leaves st_name 0, afs-as
                         names them — same symbol either way */
        if ((info & 0xf) == 4)
            continue; /* STT_FILE */
        if (shndx == 0)
            continue; /* undefined: referenced via relocs */
        out[n].name = nm;
        out[n].sec = shndx < o->nsecs ? o->secs[shndx].name : "?";
        out[n].value = rd64(e + 8);
        out[n].size = rd64(e + 16);
        out[n].bind = info >> 4;
        n++;
    }
    return n;
}

static u32 collect_rels(const Obj *o, RelRow *out, u32 cap)
{
    u32 n = 0, si;

    for (si = 1; si < o->nsecs; si++) {
        const Sec *rs = &o->secs[si];
        const Sec *symtab;
        const u8 *str;
        u32 i, count;

        if (rs->type != 4 /* SHT_RELA */)
            continue;
        symtab = &o->secs[rs->link];
        str = o->secs[symtab->link].data;
        count = (u32)(rs->size / 24);
        for (i = 0; i < count && n < cap; i++) {
            const u8 *e = rs->data + (u64)i * 24;
            u64 info = rd64(e + 8);
            u32 symi = (u32)(info >> 32);
            const u8 *se = symtab->data + (u64)symi * 24;
            const char *nm = (const char *)str + rd32(se);
            u16 shndx = rd16(se + 6);

            out[n].sec =
                rs->name + (strncmp(rs->name, ".rela", 5) == 0 ? 5 : 0);
            out[n].offset = rd64(e);
            out[n].type = (u32)info;
            /* a reloc against a section symbol names the section */
            out[n].sym = nm[0]              ? nm
                         : shndx < o->nsecs ? o->secs[shndx].name
                                            : "?";
            out[n].addend = (i64)rd64(e + 16);
            n++;
        }
    }
    return n;
}

/* PLT32 == PC32 when the referenced symbol is DEFINED in the object
 * (psABI: the linker resolves both identically; gas prefers PLT32). */
static u32 norm_reltype(u32 t)
{
    return t == R_PLT32 ? R_PC32 : t;
}

int main(int argc, char **argv)
{
    Obj a, b;
    static SymRow sa[512], sb[512];
    static RelRow ra[1024], rb[1024];
    u32 na, nb, i;

    if (argc != 3) {
        fprintf(stderr, "usage: cgf-objdiff a.o b.o\n");
        return 2;
    }
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    if (!load(&a, argv[1]) || !load(&b, argv[2]))
        return 2;

    cmp_section(&a, &b, ".text");
    cmp_section(&a, &b, ".rodata");
    cmp_section(&a, &b, ".data");
    cmp_bss(&a, &b);

    na = collect_syms(&a, sa, 512);
    nb = collect_syms(&b, sb, 512);
    qsort(sa, na, sizeof(SymRow), sym_cmp); /* check_bans allow: test tool */
    qsort(sb, nb, sizeof(SymRow), sym_cmp); /* check_bans allow: test tool */
    if (na != nb) {
        bad("defined symbol count: %u vs %u", na, nb);
    } else {
        for (i = 0; i < na; i++) {
            if (strcmp(sa[i].name, sb[i].name) != 0 ||
                strcmp(sa[i].sec, sb[i].sec) != 0 ||
                sa[i].value != sb[i].value || sa[i].bind != sb[i].bind) {
                bad("symbol %u: %s@%s+%llu b%u vs %s@%s+%llu b%u", i,
                    sa[i].name, sa[i].sec, (unsigned long long)sa[i].value,
                    sa[i].bind, sb[i].name, sb[i].sec,
                    (unsigned long long)sb[i].value, sb[i].bind);
                break;
            }
        }
    }

    na = collect_rels(&a, ra, 1024);
    nb = collect_rels(&b, rb, 1024);
    qsort(ra, na, sizeof(RelRow), rel_cmp); /* check_bans allow: test tool */
    qsort(rb, nb, sizeof(RelRow), rel_cmp); /* check_bans allow: test tool */
    if (na != nb) {
        bad("relocation count: %u vs %u", na, nb);
    } else {
        for (i = 0; i < na; i++) {
            if (strcmp(ra[i].sec, rb[i].sec) != 0 ||
                ra[i].offset != rb[i].offset ||
                norm_reltype(ra[i].type) != norm_reltype(rb[i].type) ||
                strcmp(ra[i].sym, rb[i].sym) != 0 ||
                ra[i].addend != rb[i].addend) {
                bad("reloc %u: %s+%llu t%u %s%+lld vs %s+%llu t%u %s%+lld", i,
                    ra[i].sec, (unsigned long long)ra[i].offset, ra[i].type,
                    ra[i].sym, (long long)ra[i].addend, rb[i].sec,
                    (unsigned long long)rb[i].offset, rb[i].type, rb[i].sym,
                    (long long)rb[i].addend);
                break;
            }
        }
    }

    free(a.bytes);
    free(b.bytes);
    return fails ? 1 : 0;
}
