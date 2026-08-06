/* Cgfried compiles this TU; clang defines the externs. Mach-O routes every
 * UNDEFINED symbol through the GOT even non-PIC, so a direct adrp/add here
 * is a link error or a wrong address -- never a slower program. */
extern int ext_data;
extern int ext_arr[8];
extern struct P { int a, b, c; } ext_s;
extern int ext_fn(void);
int printf(const char *, ...);

int def_data = 1000;
static int stat_data = 2000;

int main(void)
{
    int *pe = &ext_data;
    int *pa = &ext_arr[3];
    int *pc = &ext_s.c;
    int (*pf)(void) = ext_fn;

    printf("ext_data=%d via_ptr=%d\n", ext_data, *pe);
    printf("arr3=%d s.c=%d\n", *pa, *pc);
    printf("fn=%d same=%d\n", pf(), pf == ext_fn);
    printf("def=%d stat=%d\n", def_data, stat_data);
    return 0;
}
