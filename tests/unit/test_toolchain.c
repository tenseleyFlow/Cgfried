#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "driver/driver.h"
#include "driver/toolchain.h"
#include "unit.h"

/* Fake environment: NULL-terminated KEY=VALUE-style pair array. */
typedef struct {
    const char *names[8];
    const char *values[8];
} FakeEnv;

static const char *fake_getenv(const char *name, void *ctx)
{
    const FakeEnv *e = ctx;
    int i;

    for (i = 0; e->names[i]; i++)
        if (strcmp(e->names[i], name) == 0)
            return e->values[i];
    return NULL;
}

static ToolchainConfig resolve(FakeEnv *e)
{
    return cgf_toolchain_resolve_from(fake_getenv, e, cgf_target_host());
}

void test_toolchain_routing_defaults(TestCtx *t)
{
    FakeEnv e = {{NULL}, {NULL}};
    ToolchainConfig c = resolve(&e);

    T_ASSERT(t, c.use_afs_as); /* bundled by default; located lazily */
    T_ASSERT(t, c.as_path == NULL);
    T_ASSERT(t, !c.use_afs_ld);
    T_ASSERT_EQ_STR(t, c.ld_path, "ld");
    T_ASSERT(t, c.crt_dir == NULL);
    T_ASSERT(t, c.error == NULL);
}

void test_toolchain_routing_as_modes(TestCtx *t)
{
    FakeEnv sys_as = {{"CGF_AS", NULL}, {"0", NULL}};
    FakeEnv afs_as = {{"CGF_AS", NULL}, {"1", NULL}};
    FakeEnv empty_as = {{"CGF_AS", NULL}, {"", NULL}};
    ToolchainConfig c;

    c = resolve(&sys_as);
    T_ASSERT(t, !c.use_afs_as);
    T_ASSERT_EQ_STR(t, c.as_path, "as");

    c = resolve(&afs_as);
    T_ASSERT(t, c.use_afs_as);

    /* Empty string is unset, not "some other value than 0". */
    c = resolve(&empty_as);
    T_ASSERT(t, c.use_afs_as);
}

void test_toolchain_routing_path_wins(TestCtx *t)
{
    FakeEnv e = {{"CGF_AS_PATH", "CGF_AS", NULL}, {"/x/my-as", "0", NULL}};
    FakeEnv ld = {{"CGF_LD_PATH", "CGF_LD", NULL}, {"/y/my-ld", "1", NULL}};
    ToolchainConfig c;

    c = resolve(&e);
    T_ASSERT(t, !c.use_afs_as);
    T_ASSERT_EQ_STR(t, c.as_path, "/x/my-as");

    /* Explicit linker path wins over CGF_LD=1: used, no error. */
    c = resolve(&ld);
    T_ASSERT(t, c.error == NULL);
    T_ASSERT_EQ_STR(t, c.ld_path, "/y/my-ld");
    T_ASSERT(t, !c.use_afs_ld);
}

void test_toolchain_routing_ld_gate(TestCtx *t)
{
    FakeEnv e = {{"CGF_LD", NULL}, {"1", NULL}};
    FakeEnv empty = {{"CGF_LD_PATH", "CGF_LD", NULL}, {"", "0", NULL}};
    ToolchainConfig c;

    /* Sprint 27: CGF_LD=1 ROUTES now (afs-ld's ELF lane) — the pure
     * resolver just records the mode; bundled discovery (and the
     * unbuilt-tool error) layers on in cgf_toolchain_resolve, exactly
     * like afs-as. */
    c = resolve(&e);
    T_ASSERT(t, c.use_afs_ld);
    T_ASSERT(t, c.error == NULL);
    T_ASSERT(t, c.ld_path == NULL); /* located lazily */

    c = resolve(&empty);
    T_ASSERT(t, c.error == NULL);
    T_ASSERT_EQ_STR(t, c.ld_path, "ld");
}

void test_toolchain_routing_crt_dir(TestCtx *t)
{
    FakeEnv e = {{"CGF_CRT_DIR", NULL}, {"/opt/crt", NULL}};
    ToolchainConfig c = resolve(&e);

    T_ASSERT_EQ_STR(t, c.crt_dir, "/opt/crt");
}

/* --- cgf_run_tool against real (scripted) children --- */

static void write_script(TestCtx *t, const char *path, const char *body)
{
    FILE *f = fopen(path, "w");

    T_ASSERT(t, f != NULL);
    if (!f)
        return;
    fputs(body, f);
    fclose(f);
    T_ASSERT(t, chmod(path, 0755) == 0);
}

void test_toolchain_run_tool(TestCtx *t)
{
    ToolResult r;

    T_ASSERT(t, mkdir("build", 0777) == 0 || errno == EEXIST);
    T_ASSERT(t, mkdir("build/test-work", 0777) == 0 || errno == EEXIST);

    write_script(t, "build/test-work/tool_exit7.sh", "#!/bin/sh\nexit 7\n");
    write_script(t, "build/test-work/tool_segv.sh",
                 "#!/bin/sh\nkill -s SEGV $$\n");

    {
        const char *argv[] = {"build/test-work/tool_exit7.sh", NULL};
        r = cgf_run_tool(argv);
        T_ASSERT(t, r.kind == TOOL_EXITED);
        T_ASSERT_EQ_INT(t, r.exit_code, 7);
    }
    {
        const char *argv[] = {"build/test-work/tool_segv.sh", NULL};
        r = cgf_run_tool(argv);
        T_ASSERT(t, r.kind == TOOL_SIGNALED);
        T_ASSERT_EQ_INT(t, r.term_signal, SIGSEGV);
    }
    {
        const char *argv[] = {"/nonexistent/cgf-no-such-tool", NULL};
        r = cgf_run_tool(argv);
        T_ASSERT(t, r.kind == TOOL_SPAWN_FAILED);
        T_ASSERT_EQ_INT(t, r.spawn_errno, ENOENT);
    }
}

void test_toolchain_exit_mapping(TestCtx *t)
{
    ToolResult ok = {TOOL_EXITED, 0, 0, 0};
    ToolResult fail1 = {TOOL_EXITED, 1, 0, 0};
    ToolResult sig = {TOOL_SIGNALED, 0, SIGSEGV, 0};
    ToolResult enoent = {TOOL_SPAWN_FAILED, 0, 0, ENOENT};

    T_ASSERT_EQ_INT(t, cgf_tool_exit_code(TOOL_AS, &ok, true), CGF_EXIT_OK);
    /* Assembler failure on USER input: compile error, diagnostic theirs. */
    T_ASSERT_EQ_INT(t, cgf_tool_exit_code(TOOL_AS, &fail1, true),
                    CGF_EXIT_COMPILE);
    /* Assembler failure on GENERATED input: our bug — ICE (Sprint 24). */
    T_ASSERT_EQ_INT(t, cgf_tool_exit_code(TOOL_AS, &fail1, false),
                    CGF_EXIT_ICE);
    T_ASSERT_EQ_INT(t, cgf_tool_exit_code(TOOL_LD, &fail1, true),
                    CGF_EXIT_LINK);
    T_ASSERT_EQ_INT(t, cgf_tool_exit_code(TOOL_LD, &sig, true), CGF_EXIT_LINK);
    T_ASSERT_EQ_INT(t, cgf_tool_exit_code(TOOL_AS, &enoent, true), CGF_EXIT_IO);
    T_ASSERT(t, strstr(cgf_tool_missing_hint(TOOL_AS), "CGF_AS_PATH") != NULL);
    T_ASSERT(t, strstr(cgf_tool_missing_hint(TOOL_LD), "CGF_LD_PATH") != NULL);
}

/* Sprint 25: the crt probe. The pure resolver carries CGF_CRT_DIR; the
 * live probe must find crt1.o on any host that can link (CI has gcc),
 * and the directory it names must actually contain crt1.o. */
void test_toolchain_crt_probe(TestCtx *t)
{
    char diag[256];
    const char *dir = cgf_probe_crt_dir(diag, sizeof(diag));

    T_ASSERT(t, dir != NULL);
    if (dir) {
        char path[512];
        FILE *f;

        snprintf(path, sizeof(path), "%s/crt1.o", dir);
        f = fopen(path, "rb");
        T_ASSERT(t, f != NULL);
        if (f)
            fclose(f);
    }
}
