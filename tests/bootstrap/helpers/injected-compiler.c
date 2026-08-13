#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef FAULT_KIND
#define FAULT_KIND 0
#endif

#ifndef FAULT_INJECTED
#define FAULT_INJECTED 0
#endif

enum {
    FAULT_READDIR = 1,
    FAULT_PADDING = 2,
    FAULT_POINTER = 3,
};

struct PaddedRecord {
    unsigned char tag;
    unsigned long value;
};

static void die(const char *message)
{
    fprintf(stderr, "injected-compiler: %s\n", message);
    exit(2);
}

static void sort_names(char **names, size_t count)
{
    size_t i;

    for (i = 1; i < count; ++i) {
        char *name = names[i];
        size_t j = i;

        while (j != 0 && strcmp(names[j - 1], name) > 0) {
            names[j] = names[j - 1];
            --j;
        }
        names[j] = name;
    }
}

static void emit_directory_order(FILE *output)
{
    const char *path = getenv("CGF_BOOTSTRAP_FAULT_DIR");
    DIR *directory;
    struct dirent *entry;
    char *names[32];
    size_t count = 0;
    size_t i;

    if (path == NULL || path[0] == '\0')
        die("CGF_BOOTSTRAP_FAULT_DIR is required");
    directory = opendir(path);
    if (directory == NULL)
        die("cannot open seeded directory");
    while ((entry = readdir(directory)) != NULL) {
        size_t length;

        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0)
            continue;
        if (count == sizeof(names) / sizeof(names[0]))
            die("too many seeded directory entries");
        length = strlen(entry->d_name);
        names[count] = malloc(length + 1);
        if (names[count] == NULL)
            die("out of memory");
        memcpy(names[count], entry->d_name, length + 1);
        ++count;
    }
    if (closedir(directory) != 0)
        die("cannot close seeded directory");
    if (count < 3)
        die("seeded directory has too few entries");

    if (!FAULT_INJECTED)
        sort_names(names, count);
    fputs("sema", output);
    for (i = 0; i < count; ++i) {
        fprintf(output, " %s", names[i]);
        free(names[i]);
    }
    fputc('\n', output);
}

static void emit_object_representation(FILE *output)
{
    struct PaddedRecord record;

    memset(&record, FAULT_INJECTED ? 0xa5 : 0, sizeof(record));
    record.tag = 7;
    record.value = 58;
    if (fwrite(&record, sizeof(record), 1, output) != 1)
        die("cannot write seeded object representation");
}

static void emit_pointer(FILE *output)
{
    int process_local = 58;

    if (FAULT_INJECTED)
        fprintf(output, "asm %p\n", (void *)&process_local);
    else
        fputs("asm stable-address\n", output);
}

static int mode_is_fault(const char *mode)
{
    if (FAULT_KIND == FAULT_READDIR)
        return strcmp(mode, "sema") == 0 || strcmp(mode, "ir") == 0 ||
               strcmp(mode, "mir") == 0 || strcmp(mode, "asm") == 0;
    if (FAULT_KIND == FAULT_PADDING)
        return strcmp(mode, "ir") == 0 || strcmp(mode, "mir") == 0 ||
               strcmp(mode, "asm") == 0;
    return FAULT_KIND == FAULT_POINTER && strcmp(mode, "asm") == 0;
}

static void emit_mode(FILE *output, const char *mode)
{
    if (!mode_is_fault(mode))
        fprintf(output, "%s same\n", mode);
    else if (FAULT_KIND == FAULT_READDIR)
        emit_directory_order(output);
    else if (FAULT_KIND == FAULT_PADDING)
        emit_object_representation(output);
    else
        emit_pointer(output);
}

static void emit_phase_dump(const char *dir, const char *name,
                            const char *mode)
{
    char path[4096];
    FILE *output;
    int n = snprintf(path, sizeof(path), "%s/%s", dir, name);

    if (n < 0 || (size_t)n >= sizeof(path))
        die("phase dump path is too long");
    output = fopen(path, "wb");
    if (output == NULL)
        die("cannot create phase dump");
    emit_mode(output, mode);
    if (fclose(output) != 0)
        die("cannot close phase dump");
}

static void emit_phase_tree(void)
{
    const char *mode = getenv("CGF_DUMP_IR");
    const char *dir = getenv("CGF_DUMP_IR_DIR");

    if (mode == NULL || strcmp(mode, "all") != 0)
        return;
    if (dir == NULL || dir[0] == '\0')
        die("CGF_DUMP_IR_DIR is required");
    emit_phase_dump(dir, "100000-parse-ast.txt", "ast");
    emit_phase_dump(dir, "200000-sema.txt", "sema");
    emit_phase_dump(dir, "300000-ir-post-lowering.cgfir", "ir");
    emit_phase_dump(dir, "400001-ir-fp01-i01-p00-seeded.cgfir", "ir");
    emit_phase_dump(dir, "700000-ir-post-opt-legalized.cgfir", "ir");
    emit_phase_dump(dir, "800000-mir.txt", "mir");
    emit_phase_dump(dir, "900000-asm.s", "asm");
}

int main(int argc, char **argv)
{
    const char *mode = "unknown";
    const char *output_path = NULL;
    FILE *output = stdout;
    int dumpmachine = 0;
    int i;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-dumpmachine") == 0)
            dumpmachine = 1;
        else if (strcmp(argv[i], "-E") == 0)
            mode = "pp";
        else if (strcmp(argv[i], "--dump-ast") == 0)
            mode = "ast";
        else if (strcmp(argv[i], "-fdump-sema") == 0)
            mode = "sema";
        else if (strcmp(argv[i], "-emit-ir") == 0)
            mode = "ir";
        else if (strcmp(argv[i], "-emit-mir") == 0)
            mode = "mir";
        else if (strcmp(argv[i], "-S") == 0)
            mode = "asm";
        else if (strcmp(argv[i], "-o") == 0) {
            if (++i == argc)
                die("missing -o argument");
            output_path = argv[i];
        }
    }

    if (dumpmachine) {
        fputs("x86_64-linux-gnu\n", stdout);
        return 0;
    }

    if (strcmp(mode, "asm") == 0) {
        if (output_path == NULL)
            die("assembly mode requires -o");
        output = fopen(output_path, "wb");
        if (output == NULL)
            die("cannot open assembly output");
    }

    emit_mode(output, mode);

    if (output != stdout && fclose(output) != 0)
        die("cannot close assembly output");
    if (strcmp(mode, "asm") == 0)
        emit_phase_tree();
    return 0;
}
