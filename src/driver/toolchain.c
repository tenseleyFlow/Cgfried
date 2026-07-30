#include "driver/toolchain.h"

#include <errno.h>
#include <spawn.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "diag.h"
#include "driver/driver.h"

extern char **environ;

static const char *g_argv0;

void cgf_toolchain_set_argv0(const char *argv0)
{
    g_argv0 = argv0;
}

/* The ONLY place any CGF_* toolchain variable is read (grep-enforced).
 * Trims nothing; empty string is unset. */
static const char *env_override(ToolchainGetenv fn, void *ctx, const char *name,
                                const char *defval)
{
    const char *v = fn(name, ctx);

    if (!v || v[0] == '\0')
        return defval;
    return v;
}

static const char *real_getenv(const char *name, void *ctx)
{
    (void)ctx;
    return getenv(name);
}

ToolchainConfig cgf_toolchain_resolve_from(ToolchainGetenv fn, void *ctx,
                                           TargetSpec t)
{
    ToolchainConfig c;
    const char *v;

    (void)t; /* per-target tool selection arrives with cross-compilation */
    memset(&c, 0, sizeof(c));

    /* Assembler: explicit path > mode > default (bundled). */
    v = env_override(fn, ctx, "CGF_AS_PATH", NULL);
    if (v) {
        c.as_path = v;
        c.use_afs_as = false;
    } else if (strcmp(env_override(fn, ctx, "CGF_AS", "1"), "0") == 0) {
        c.as_path = "as";
        c.use_afs_as = false;
    } else {
        c.use_afs_as = true;
        c.as_path = NULL; /* located by cgf_toolchain_resolve */
    }

    /* Linker: explicit path > mode > default (system ld). */
    v = env_override(fn, ctx, "CGF_LD_PATH", NULL);
    if (v) {
        c.ld_path = v;
        c.use_afs_ld = false;
    } else if (strcmp(env_override(fn, ctx, "CGF_LD", "0"), "1") == 0) {
        c.use_afs_ld = true;
        c.error = "afs-ld linking is not yet supported: Sprint 27";
        c.error_is_io = false;
    } else {
        c.ld_path = "ld";
        c.use_afs_ld = false;
    }

    c.crt_dir = env_override(fn, ctx, "CGF_CRT_DIR", NULL);
    return c;
}

/* Directory containing our own binary, via /proc/self/exe with an argv[0]
 * fallback (macOS/FreeBSD variants arrive with their targets, Sprints
 * 50/51). Never baked in at build time: a -D absolute path would break the
 * reproducible-release and bootstrap-diff invariants. */
static const char *exe_dir(void)
{
    static char dir[4096];
    static bool computed;
    ssize_t n;

    if (computed)
        return dir[0] ? dir : NULL;
    computed = true;

    n = readlink("/proc/self/exe", dir, sizeof(dir) - 1);
    if (n <= 0) {
        const char *slash;

        if (!g_argv0 || !(slash = strrchr(g_argv0, '/'))) {
            dir[0] = '\0';
            return NULL;
        }
        if ((size_t)(slash - g_argv0) >= sizeof(dir) - 1) {
            dir[0] = '\0';
            return NULL;
        }
        memcpy(dir, g_argv0, (size_t)(slash - g_argv0));
        dir[slash - g_argv0] = '\0';
        return dir;
    }
    dir[n] = '\0';
    {
        char *slash = strrchr(dir, '/');
        if (!slash) {
            dir[0] = '\0';
            return NULL;
        }
        *slash = '\0';
    }
    return dir;
}

/* Bundled afs-as discovery: installed layout first, then the dev tree. */
static const char *locate_bundled_as(void)
{
    static char path[4096];
    static const char *const suffixes[] = {
        "/afs-as",                          /* make install layout */
        "/../afs-as/target/release/afs-as", /* dev tree (build/cgfried) */
    };
    const char *dir = exe_dir();
    size_t i;

    if (!dir)
        return NULL;
    for (i = 0; i < CGF_ARRAY_LEN(suffixes); i++) {
        int n = snprintf(path, sizeof(path), "%s%s", dir, suffixes[i]);
        if (n < 0 || (size_t)n >= sizeof(path))
            continue;
        if (access(path, X_OK) == 0)
            return path;
    }
    return NULL;
}

ToolchainConfig cgf_toolchain_resolve(TargetSpec t)
{
    static ToolchainConfig cached;
    static bool have_cached;

    if (have_cached)
        return cached;
    cached = cgf_toolchain_resolve_from(real_getenv, NULL, t);
    if (!cached.error && cached.use_afs_as) {
        cached.as_path = locate_bundled_as();
        if (!cached.as_path) {
            cached.error = "bundled afs-as not built; run 'make tools' or "
                           "set CGF_AS=0 to use the system assembler";
            cached.error_is_io = true;
        }
    }
    have_cached = true;
    return cached;
}

ToolResult cgf_run_tool(const char *const argv[])
{
    ToolResult res;
    pid_t pid;
    int rc, status;

    memset(&res, 0, sizeof(res));
    /* No file actions: stdout/stderr inherited on purpose (see header). */
    rc = posix_spawnp(&pid, argv[0], NULL, NULL, (char *const *)argv, environ);
    if (rc != 0) {
        res.kind = TOOL_SPAWN_FAILED;
        res.spawn_errno = rc;
        return res;
    }
    for (;;) {
        if (waitpid(pid, &status, 0) >= 0)
            break;
        if (errno != EINTR)
            CGF_ICE("waitpid on tool '%s' failed: %s", argv[0],
                    strerror(errno));
    }
    if (WIFEXITED(status)) {
        res.kind = TOOL_EXITED;
        res.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        /* Signal death is reported as a signal, never as an exit code. */
        res.kind = TOOL_SIGNALED;
        res.term_signal = WTERMSIG(status);
    } else {
        CGF_ICE("waitpid on tool '%s': unexpected status 0x%x", argv[0],
                (unsigned)status);
    }
    return res;
}

int cgf_tool_exit_code(ToolKind which, const ToolResult *res, bool user_input)
{
    if (res->kind == TOOL_EXITED && res->exit_code == 0)
        return CGF_EXIT_OK;
    if (res->kind == TOOL_SPAWN_FAILED)
        return CGF_EXIT_IO;
    switch (which) {
    case TOOL_AS:
        /* User-provided .s: the assembler's own diagnostic already printed,
         * this is a compile-side error. cgfried-GENERATED .s failing to
         * assemble means OUR emission was invalid — that is an ICE, and
         * Sprint 24 owns wiring that path through cgf_ice. */
        return user_input ? CGF_EXIT_COMPILE : CGF_EXIT_ICE;
    case TOOL_LD:
        return CGF_EXIT_LINK;
    }
    CGF_ICE("cgf_tool_exit_code: bad tool kind %d", (int)which);
}

bool cgf_exe_relative(const char *suffix, char *out, size_t out_size)
{
    const char *dir = exe_dir();
    int n;

    if (!dir)
        return false;
    n = snprintf(out, out_size, "%s%s", dir, suffix);
    return n > 0 && (size_t)n < out_size;
}

const char *cgf_env(const char *name)
{
    return env_override(real_getenv, NULL, name, NULL);
}

const char *cgf_tool_missing_hint(ToolKind which)
{
    switch (which) {
    case TOOL_AS:
        return "assembler not found; install binutils or set CGF_AS_PATH";
    case TOOL_LD:
        return "linker not found; install binutils or set CGF_LD_PATH";
    }
    CGF_ICE("cgf_tool_missing_hint: bad tool kind %d", (int)which);
}
