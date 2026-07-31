#include "driver/toolchain.h"

#include <errno.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "diag.h"
#include "driver/args.h"
#include "driver/driver.h"

#include "util/arena.h"

extern char **environ;

static const char *g_argv0;
static bool g_echo; /* -v: print each subprocess argv before running */

void cgf_toolchain_set_argv0(const char *argv0)
{
    g_argv0 = argv0;
}

const char *cgf_toolchain_argv0(void)
{
    return g_argv0 ? g_argv0 : "cgfried";
}

void cgf_toolchain_set_echo(bool verbose)
{
    g_echo = verbose;
}

/* Shell-quoted argv on one line to stderr — the -v/-### contract shape
 * (every arg double-quoted, embedded quotes/backslashes escaped). */
static void echo_argv_line(const char *const argv[])
{
    int i;

    for (i = 0; argv[i]; i++) {
        const char *p = argv[i];

        if (i)
            fputc(' ', stderr);
        fputc('"', stderr);
        for (; *p; p++) {
            if (*p == '"' || *p == '\\')
                fputc('\\', stderr);
            fputc(*p, stderr);
        }
        fputc('"', stderr);
    }
    fputc('\n', stderr);
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

    if (g_echo)
        echo_argv_line(argv);
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

/* --- Sprint 24: the assembler subprocess ------------------------------------
 *
 * Unlike cgf_run_tool, the assembler's stderr is CAPTURED: every line
 * is echoed to our stderr prefixed "[as] ", and on failure the first
 * "<file>:<line>:" diagnostic is parsed so the driver can quote the
 * offending .s line in its ICE (our emitter produced text our
 * assembler rejects — a cgf bug by definition). One pipe carries both
 * stdout and stderr; the read loop drains to EOF before waitpid, so a
 * chatty assembler cannot deadlock us. */

ToolResult cgf_run_assembler(const char *s_path, const char *o_path,
                             u32 *diag_line)
{
    ToolchainConfig tc = cgf_toolchain_resolve(cgf_target_host());
    const char *argv[6];
    int an = 0;
    ToolResult res;
    pid_t pid;
    int rc, status;
    int pipefd[2];
    posix_spawn_file_actions_t fa;
    char buf[4096];
    char pending[512];
    size_t npend = 0;
    bool line_found = false;

    memset(&res, 0, sizeof(res));
    if (diag_line)
        *diag_line = 0;
    if (tc.error) {
        res.kind = TOOL_SPAWN_FAILED;
        res.spawn_errno = ENOENT;
        return res;
    }
    argv[an++] = tc.as_path;
    /* afs-as defaults to its ARM64 Mach-O arm; --64 selects x86_64 AT&T
     * (exactly how the Sprint 2 toolchain smoke invokes it). The system
     * assembler needs no selector. */
    if (tc.use_afs_as)
        argv[an++] = "--64";
    argv[an++] = s_path;
    argv[an++] = "-o";
    argv[an++] = o_path;
    argv[an] = NULL;

    if (g_echo)
        echo_argv_line(argv);
    if (pipe(pipefd) != 0)
        CGF_ICE("pipe for assembler capture failed: %s", strerror(errno));
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, pipefd[1], 1);
    posix_spawn_file_actions_adddup2(&fa, pipefd[1], 2);
    posix_spawn_file_actions_addclose(&fa, pipefd[0]);
    posix_spawn_file_actions_addclose(&fa, pipefd[1]);
    rc = posix_spawnp(&pid, argv[0], &fa, NULL, (char *const *)argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    close(pipefd[1]);
    if (rc != 0) {
        close(pipefd[0]);
        res.kind = TOOL_SPAWN_FAILED;
        res.spawn_errno = rc;
        return res;
    }
    for (;;) {
        ssize_t n = read(pipefd[0], buf, sizeof(buf));
        ssize_t i;

        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            break;
        for (i = 0; i < n; i++) {
            char c = buf[i];

            if (c == '\n' || npend + 1 >= sizeof(pending)) {
                pending[npend] = '\0';
                fprintf(stderr, "[as] %s\n", pending);
                /* first "<file>:<line>:" wins */
                if (!line_found && diag_line) {
                    const char *p = strstr(pending, ".s:");

                    if (p) {
                        unsigned long v = strtoul(p + 3, NULL, 10);

                        if (v > 0) {
                            *diag_line = (u32)v;
                            line_found = true;
                        }
                    }
                }
                npend = 0;
            } else {
                pending[npend++] = c;
            }
        }
    }
    close(pipefd[0]);
    if (npend) {
        pending[npend] = '\0';
        fprintf(stderr, "[as] %s\n", pending);
    }
    for (;;) {
        if (waitpid(pid, &status, 0) >= 0)
            break;
        if (errno != EINTR)
            CGF_ICE("waitpid on assembler failed: %s", strerror(errno));
    }
    if (WIFEXITED(status)) {
        res.kind = TOOL_EXITED;
        res.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        res.kind = TOOL_SIGNALED;
        res.term_signal = WTERMSIG(status);
    } else {
        CGF_ICE("waitpid on assembler: unexpected status 0x%x",
                (unsigned)status);
    }
    return res;
}

/* --- Sprint 25: the link recipe ---------------------------------------------
 *
 * Dynamic non-PIE ELF executable against system crt/libc. crtbegin.o /
 * crtend.o are GCC-installation files needed only for ctors/dtors/C++ —
 * plain C links fine without them (do NOT "add them for correctness":
 * that drags in a GCC-version-specific path). The crt probe covers the
 * two big layouts now; the full -B discovery matrix is Sprint 27. */

static const char *const crt_dirs[] = {
    "/usr/lib",                 /* Arch */
    "/usr/lib/x86_64-linux-gnu" /* Debian/Ubuntu */
};

/* Locates crt1.o's directory (CGF_CRT_DIR override first). Returns NULL
 * with all probed paths written into diag when nothing has crt1.o. */
const char *cgf_probe_crt_dir(char *diag, size_t diag_sz)
{
    ToolchainConfig tc = cgf_toolchain_resolve(cgf_target_host());
    char path[512];
    size_t i, used = 0;

    if (tc.crt_dir) {
        snprintf(path, sizeof(path), "%s/crt1.o", tc.crt_dir);
        if (access(path, R_OK) == 0)
            return tc.crt_dir;
        snprintf(diag, diag_sz, "%s (CGF_CRT_DIR)", tc.crt_dir);
        return NULL;
    }
    diag[0] = '\0';
    for (i = 0; i < sizeof(crt_dirs) / sizeof(crt_dirs[0]); i++) {
        snprintf(path, sizeof(path), "%s/crt1.o", crt_dirs[i]);
        if (access(path, R_OK) == 0)
            return crt_dirs[i];
        used += (size_t)snprintf(diag + used, diag_sz - used, "%s%s",
                                 used ? ", " : "", crt_dirs[i]);
        if (used >= diag_sz)
            break;
    }
    return NULL;
}

/* Builds one arena string "<a><b>". */
static const char *joined2(struct Arena *ar, const char *a, const char *b)
{
    size_t la = strlen(a), lb = strlen(b);
    char *s = arena_alloc(ar, la + lb + 1, 1);

    memcpy(s, a, la);
    memcpy(s + la, b, lb + 1);
    return s;
}

/* Builds the full ld argv from a LinkRequest, in the Sprint 25 recipe
 * extended with the Sprint 26 stream: ld [-static] -o out [-L user...]
 * -L crtdir crt1 crti <inputs IN ORDER: objs / -lNAME / raw> -lc crtn
 * [-dynamic-linker ...]. -L is hoisted (GNU ld applies all -L to all -l
 * regardless of position); objects and libs are NOT — position is the
 * user's archive semantics. NULL-terminated; NULL return = resolution
 * failure already reported. */
static const char **build_ld_argv(const LinkRequest *lr)
{
    ToolchainConfig tc = cgf_toolchain_resolve(cgf_target_host());
    char diag[256];
    const char *crtdir = NULL;
    const char **argv;
    size_t cap, n = 0, i;

    if (tc.use_afs_ld) {
        fprintf(stderr, "cgfried: error: CGF_LD=1 (afs-ld) lands in "
                        "Sprint 27; unset it or use CGF_LD_PATH\n");
        return NULL;
    }
    if (!lr->nostdlib && !lr->nostartfiles) {
        crtdir = cgf_probe_crt_dir(diag, sizeof(diag));
        if (!crtdir) {
            fprintf(stderr,
                    "cgfried: error: cannot find crt1.o (probed: %s); "
                    "set CGF_CRT_DIR\n",
                    diag);
            return NULL;
        }
    } else if (!lr->nostdlib && !lr->nodefaultlibs) {
        crtdir = cgf_probe_crt_dir(diag, sizeof(diag));
    }
    cap = 16 + lr->n_inputs + lr->n_lib_dirs;
    argv = arena_alloc(lr->arena, cap * sizeof(*argv), sizeof(void *));
    argv[n++] = tc.ld_path;
    if (lr->static_link)
        argv[n++] = "-static";
    argv[n++] = "-o";
    argv[n++] = lr->out;
    for (i = 0; i < lr->n_lib_dirs; i++)
        argv[n++] = joined2(lr->arena, "-L", lr->lib_dirs[i]);
    if (crtdir)
        argv[n++] = joined2(lr->arena, "-L", crtdir);
    if (!lr->nostdlib && !lr->nostartfiles) {
        argv[n++] = joined2(lr->arena, crtdir, "/crt1.o");
        argv[n++] = joined2(lr->arena, crtdir, "/crti.o");
    }
    for (i = 0; i < lr->n_inputs; i++) {
        const struct LinkInput *li = &lr->inputs[i];

        if (!li->val)
            continue; /* a TU that never produced its object */
        if (li->kind == LINK_LIB)
            argv[n++] = joined2(lr->arena, "-l", li->val);
        else
            argv[n++] = li->val;
    }
    if (!lr->nostdlib && !lr->nodefaultlibs)
        argv[n++] = "-lc";
    if (!lr->nostdlib && !lr->nostartfiles)
        argv[n++] = joined2(lr->arena, crtdir, "/crtn.o");
    if (!lr->static_link) {
        argv[n++] = "-dynamic-linker";
        argv[n++] = "/lib64/ld-linux-x86-64.so.2";
    }
    argv[n] = NULL;
    return argv;
}

ToolResult cgf_run_linker2(const LinkRequest *lr)
{
    const char **argv = build_ld_argv(lr);
    ToolResult res;

    if (!argv) {
        memset(&res, 0, sizeof(res));
        res.kind = TOOL_SPAWN_FAILED;
        res.spawn_errno = ENOENT;
        return res;
    }
    return cgf_run_tool((const char *const *)argv);
}

void cgf_echo_ld_plan(const LinkRequest *lr)
{
    const char **argv = build_ld_argv(lr);

    if (argv)
        echo_argv_line((const char *const *)argv);
}

void cgf_echo_as_plan(const char *s_path, const char *o_path)
{
    ToolchainConfig tc = cgf_toolchain_resolve(cgf_target_host());
    const char *argv[6];
    int n = 0;

    argv[n++] = tc.error ? "as" : tc.as_path;
    if (!tc.error && tc.use_afs_as)
        argv[n++] = "--64";
    argv[n++] = s_path;
    argv[n++] = "-o";
    argv[n++] = o_path;
    argv[n] = NULL;
    echo_argv_line(argv);
}

/* PATH walk for -print-prog-name=; a name containing '/' probes
 * directly. */
bool cgf_which(const char *name, char *out, size_t out_size)
{
    const char *path, *p;

    if (strchr(name, '/')) {
        if (access(name, X_OK) != 0)
            return false;
        if ((size_t)snprintf(out, out_size, "%s", name) >= out_size)
            return false;
        return true;
    }
    path = getenv("PATH");
    if (!path)
        return false;
    p = path;
    for (;;) {
        const char *colon = strchr(p, ':');
        size_t len = colon ? (size_t)(colon - p) : strlen(p);
        int n;

        if (len == 0)
            n = snprintf(out, out_size, "./%s", name);
        else
            n = snprintf(out, out_size, "%.*s/%s", (int)len, p, name);
        if (n > 0 && (size_t)n < out_size && access(out, X_OK) == 0)
            return true;
        if (!colon)
            return false;
        p = colon + 1;
    }
}
