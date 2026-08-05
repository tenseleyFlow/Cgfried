#include "driver/toolchain.h"

#include <dirent.h>
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

    /* Linker: explicit path > mode > default (system ld). Sprint 27:
     * CGF_LD=1 routes to bundled afs-ld (its supported ELF lane is
     * -static x86_64; dynamic is experimental — see --help); location
     * is layered on by cgf_toolchain_resolve, same as afs-as. */
    v = env_override(fn, ctx, "CGF_LD_PATH", NULL);
    if (v) {
        c.ld_path = v;
        c.use_afs_ld = false;
    } else if (strcmp(env_override(fn, ctx, "CGF_LD", "0"), "1") == 0) {
        c.use_afs_ld = true;
        c.ld_path = NULL; /* located by cgf_toolchain_resolve */
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

/* Bundled tool discovery: installed layout first, then the dev tree. */
static const char *locate_bundled(char *path, size_t path_sz,
                                  const char *const suffixes[], size_t nsuf)
{
    const char *dir = exe_dir();
    size_t i;

    if (!dir)
        return NULL;
    for (i = 0; i < nsuf; i++) {
        int n = snprintf(path, path_sz, "%s%s", dir, suffixes[i]);
        if (n < 0 || (size_t)n >= path_sz)
            continue;
        if (access(path, X_OK) == 0)
            return path;
    }
    return NULL;
}

static const char *locate_bundled_as(void)
{
    static char path[4096];
    static const char *const suffixes[] = {
        "/afs-as",                          /* make install layout */
        "/../afs-as/target/release/afs-as", /* dev tree (build/cgfried) */
    };

    return locate_bundled(path, sizeof(path), suffixes,
                          CGF_ARRAY_LEN(suffixes));
}

static const char *locate_bundled_ld(void)
{
    static char path[4096];
    static const char *const suffixes[] = {
        "/afs-ld",                          /* make install layout */
        "/../afs-ld/target/release/afs-ld", /* dev tree (build/cgfried) */
    };

    return locate_bundled(path, sizeof(path), suffixes,
                          CGF_ARRAY_LEN(suffixes));
}

ToolchainConfig cgf_toolchain_resolve(TargetSpec t)
{
    static ToolchainConfig cached;
    static bool have_cached;

    if (have_cached)
        return cached;
    cached = cgf_toolchain_resolve_from(real_getenv, NULL, t);
    /* The two tools resolve INDEPENDENTLY: a missing bundled assembler
     * must not make the link path unusable (F-S27-ASERRLINK — CI's
     * Rust-free jobs have no afs-as, and the link-argv builder bailed
     * on `error` regardless of which tool it described). Each consumer
     * checks the path it actually needs; `error` carries the first
     * problem for the diagnostics that want one string. */
    if (cached.use_afs_as) {
        cached.as_path = locate_bundled_as();
        if (!cached.as_path) {
            cached.error = "bundled afs-as not built; run 'make tools' or "
                           "set CGF_AS=0 to use the system assembler";
            cached.error_is_io = true;
        }
    }
    if (cached.use_afs_ld) {
        cached.ld_path = locate_bundled_ld();
        if (!cached.ld_path && !cached.error) {
            cached.error = "bundled afs-ld not built; run 'make tools' or "
                           "unset CGF_LD to use the system linker";
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

/* Sprint 28: the shipped freestanding-header dir. CGF_INCLUDE_DIR wins
 * (a miss there is honored, not silently ignored: the user named it);
 * then the installed layout, then the dev tree. Probed for stddef.h so
 * a stale empty dir cannot win. */
const char *cgf_shipped_include_dir(void)
{
    static char path[4096];
    static bool computed;
    static const char *result;
    static const char *const suffixes[] = {
        "/../lib/cgfried/include", /* make install layout */
        "/../include",             /* dev tree (build/cgfried) */
    };
    const char *override;
    char probe[4200];
    size_t i;

    if (computed)
        return result;
    computed = true;
    override = cgf_env("CGF_INCLUDE_DIR");
    if (override) {
        result = override;
        return result;
    }
    for (i = 0; i < CGF_ARRAY_LEN(suffixes); i++) {
        if (!cgf_exe_relative(suffixes[i], path, sizeof(path)))
            continue;
        snprintf(probe, sizeof(probe), "%s/stddef.h", path);
        if (access(probe, R_OK) == 0) {
            result = path;
            return result;
        }
    }
    result = NULL;
    return result;
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

/* Which arm of afs-as to select. It is a multi-target assembler whose
 * DEFAULT is arm64 Mach-O, so every other target has to be named
 * explicitly -- an omitted flag assembles silently for the wrong one. */
const char *cgf_afs_as_target_flag(TargetSpec t)
{
    switch (t.kind) {
    case CGF_TARGET_ARM64_LINUX:
        return "--target=aarch64-elf";
    case CGF_TARGET_ARM64_MACOS:
        /* afs-as's default arm needs no flag. Unreachable today: arm64-macos
         * codegen hard-errors naming Sprint 50 long before this. */
        return NULL;
    default:
        return "--64";
    }
}

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
    if (!tc.as_path) {
        /* Only the ASSEMBLER's resolution matters here (F-S27-ASERRLINK
         * in reverse: a missing bundled afs-ld must not break -c). */
        res.kind = TOOL_SPAWN_FAILED;
        res.spawn_errno = ENOENT;
        return res;
    }
    argv[an++] = tc.as_path;
    /* afs-as defaults to its ARM64 Mach-O arm; --64 selects x86_64 AT&T
     * (exactly how the Sprint 2 toolchain smoke invokes it) and
     * --target=aarch64-elf selects the Sprint 49 arm64 ELF arm. GNU as may
     * be configured to compress .debug_* by default. afs-ld deliberately
     * does not decode SHF_COMPRESSED inputs, and reloc offsets name the
     * uncompressed image, so keep compiler-produced debug sections plain. */
    if (tc.use_afs_as) {
        const char *arm = cgf_afs_as_target_flag(cgf_target_host());

        if (arm)
            argv[an++] = arm;
    } else {
        argv[an++] = "--nocompress-debug-sections";
    }
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

/* --- Sprints 25-27: the link recipe -----------------------------------------
 *
 * Dynamic non-PIE ELF executable against system crt/libc. crtbegin.o /
 * crtend.o are GCC-installation files needed only for ctors/dtors/C++ —
 * plain C links fine without them (do NOT "add them for correctness":
 * that drags in a GCC-version-specific path; if a link ever fails on
 * __dso_handle, that is THIS gap — document, don't silently add).
 * Non-PIE only in v0.1.0: always crt1.o, never Scrt1.o (Sprint 51). */

/* Sprint 27 probe table, first stat() hit for crt1.o wins. Order is the
 * distro-layout ladder from the sprint file; overrides (CGF_CRT_DIR,
 * then each -B in flag order) probe first. */
static const char *const crt_default_dirs[] = {
    "/usr/lib/x86_64-linux-gnu", /* Debian/Ubuntu multiarch */
    "/usr/lib64",                /* Fedora/RHEL */
    "/usr/lib",                  /* Arch/generic */
    "/lib",                      /* fallback */
};

/* Injectable core (units point `table` at temp dirs): returns the first
 * dir whose crt1.o exists; every probed path is appended to `searched`
 * one per line — the failure diagnostic is a debuggability CONTRACT
 * (name everything tried). */
const char *cgf_probe_crt_dir_in(const char *override, const char *const *bdirs,
                                 size_t nb, const char *const *table,
                                 size_t ntable, Buf *searched)
{
    char path[1024];
    size_t i;

    if (override) {
        snprintf(path, sizeof(path), "%s/crt1.o", override);
        if (access(path, R_OK) == 0)
            return override;
        if (searched)
            buf_printf(searched, "  %s (CGF_CRT_DIR)\n", path);
        /* An explicit override that misses does NOT fall through: the
         * user asked for exactly this dir, so failing loudly beats
         * silently linking against some other libc's crt. */
        return NULL;
    }
    for (i = 0; i < nb; i++) {
        snprintf(path, sizeof(path), "%s/crt1.o", bdirs[i]);
        if (access(path, R_OK) == 0)
            return bdirs[i];
        if (searched)
            buf_printf(searched, "  %s (-B)\n", path);
    }
    for (i = 0; i < ntable; i++) {
        snprintf(path, sizeof(path), "%s/crt1.o", table[i]);
        if (access(path, R_OK) == 0)
            return table[i];
        if (searched)
            buf_printf(searched, "  %s\n", path);
    }
    return NULL;
}

/* Compatibility probe (print-file-name/-search-dirs): no -B dirs, no
 * search transcript. diag gets a comma-joined summary. */
const char *cgf_probe_crt_dir(char *diag, size_t diag_sz)
{
    ToolchainConfig tc = cgf_toolchain_resolve(cgf_target_host());
    const char *dir =
        cgf_probe_crt_dir_in(tc.crt_dir, NULL, 0, crt_default_dirs,
                             CGF_ARRAY_LEN(crt_default_dirs), NULL);

    if (!dir && diag_sz) {
        size_t i, used = 0;

        diag[0] = '\0';
        for (i = 0; i < CGF_ARRAY_LEN(crt_default_dirs); i++) {
            used += (size_t)snprintf(diag + used, diag_sz - used, "%s%s",
                                     used ? ", " : "", crt_default_dirs[i]);
            if (used >= diag_sz)
                break;
        }
    }
    return dir;
}

/* F-S27-STATICLIBGCC: the sprint file claimed --start-group rt -lc
 * --end-group suffices for static glibc; empirically its libc.a .cold
 * paths reference _Unwind_Resume/__gcc_personality_v0 (libgcc_eh), so
 * gcc's real static line adds -lgcc -lgcc_eh from its PRIVATE dir.
 * Probe the filesystem for it (highest version wins) — never spawn gcc,
 * never bake a version path. Absent (musl, freestanding): proceed
 * without; ld's error stays visible. */
static const char *locate_libgcc_dir(void)
{
    static char best[1024];
    DIR *top = opendir("/usr/lib/gcc");
    struct dirent *tri;

    best[0] = '\0';
    if (!top)
        return NULL;
    while ((tri = readdir(top)) != NULL) {
        char tridir[512];
        DIR *vers;
        struct dirent *ver;

        if (tri->d_name[0] == '.')
            continue;
        snprintf(tridir, sizeof(tridir), "/usr/lib/gcc/%s", tri->d_name);
        vers = opendir(tridir);
        if (!vers)
            continue;
        while ((ver = readdir(vers)) != NULL) {
            char probe[1024];

            if (ver->d_name[0] == '.')
                continue;
            snprintf(probe, sizeof(probe), "%s/%s/libgcc.a", tridir,
                     ver->d_name);
            if (access(probe, R_OK) != 0)
                continue;
            snprintf(probe, sizeof(probe), "%s/%s", tridir, ver->d_name);
            if (best[0] == '\0' || strcmp(probe, best) > 0)
                snprintf(best, sizeof(best), "%s", probe);
        }
        closedir(vers);
    }
    closedir(top);
    return best[0] ? best : NULL;
}

/* The libcgf_rt.a slot (content is Sprint 28; this sprint reserves its
 * place in the canonical sequence). Dev tree: build/<target>/ beside
 * the compiler; installed: <prefix>/lib/cgfried/<target>/. Absent file
 * = slot silently empty until Sprint 28 ships it. */
static const char *locate_rt_archive(TargetSpec t)
{
    static char path[4096];
    char suffix[256];

    snprintf(suffix, sizeof(suffix), "/%s/libcgf_rt.a", cgf_target_name(t));
    if (cgf_exe_relative(suffix, path, sizeof(path)) && access(path, R_OK) == 0)
        return path;
    snprintf(suffix, sizeof(suffix), "/../lib/cgfried/%s/libcgf_rt.a",
             cgf_target_name(t));
    if (cgf_exe_relative(suffix, path, sizeof(path)) && access(path, R_OK) == 0)
        return path;
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

/* Sprint 27, the canonical link line — verbatim from the sprint file:
 *
 *   ld -dynamic-linker <per-target> -o <out>
 *      <crtdir>/crt1.o <crtdir>/crti.o
 *      <user objects, -l libs, -Wl args IN COMMAND-LINE ORDER>
 *      libcgf_rt.a -lc
 *      <crtdir>/crtn.o
 *
 * -static drops -dynamic-linker and groups the tail
 * (--start-group rt -lc --end-group: glibc's libc.a has internal
 * cycles — the gcc-parity fix). Subtraction: -nostartfiles removes the
 * crts, -nodefaultlibs removes rt/-lc, -nostdlib removes both;
 * user -l flags ALWAYS survive. -L dirs hoist (GNU ld applies all -L
 * to all -l regardless of position); objects/libs NEVER reorder.
 * False = failure already reported on stderr (exit 2 at the driver). */
bool toolchain_build_link_argv(const DriverArgs *da, TargetSpec t,
                               struct Arena *ar, VecStr *out)
{
    static const char *const safe_wraps[] = {
        "--wrap=malloc",  "--wrap=calloc",        "--wrap=realloc",
        "--wrap=free",    "--wrap=reallocarray",  "--wrap=strdup",
        "--wrap=strndup", "--wrap=aligned_alloc", "--wrap=posix_memalign",
    };
    ToolchainConfig tc = cgf_toolchain_resolve(t);
    const char *crtdir = NULL;
    const char *dl = cgf_target_dynamic_linker(t);
    const char *rt;
    const char *outname = da->output ? da->output : "a.out";
    bool want_crts = !da->nostdlib && !da->nostartfiles;
    bool want_libs = !da->nostdlib && !da->nodefaultlibs;
    size_t i;

    if (!tc.ld_path) {
        /* Only the LINKER's own resolution matters here; a missing
         * bundled assembler is the compile step's problem
         * (F-S27-ASERRLINK). */
        fprintf(stderr, "cgfried: error: bundled afs-ld not built; run 'make "
                        "tools' or unset CGF_LD to use the system linker\n");
        return false;
    }
    if (want_crts || want_libs) {
        Buf searched;

        buf_init(&searched);
        crtdir = cgf_probe_crt_dir_in(
            tc.crt_dir, da->prefix_dirs.data, da->prefix_dirs.len,
            crt_default_dirs, CGF_ARRAY_LEN(crt_default_dirs), &searched);
        if (!crtdir && want_crts) {
            fprintf(stderr, "cgfried: error: cannot find crt1.o; searched:\n");
            fwrite(searched.data, 1, searched.len, stderr);
            fprintf(stderr, "set CGF_CRT_DIR or pass -B <dir>\n");
            buf_free(&searched);
            return false;
        }
        buf_free(&searched);
    }

    VecStr_push(out, tc.ld_path);
    if (tc.use_afs_ld)
        VecStr_push(out, "-melf_x86_64"); /* select its ELF arm */
    if (da->static_link)
        VecStr_push(out, "-static");
    else if (dl) {
        VecStr_push(out, "-dynamic-linker");
        VecStr_push(out, dl);
    }
    VecStr_push(out, "-o");
    VecStr_push(out, outname);
    /* gcc always passes --eh-frame-hdr; the unwind index costs nothing
     * for plain C and readelf-structural parity wants it. */
    VecStr_push(out, "--eh-frame-hdr");
    /* Sprint 44: the runtime archive supplies the corresponding
     * __wrap_* definitions. Keep these option-like arguments before every
     * position-sensitive user input. When default libraries are subtracted,
     * the runtime is absent too, so emitting wrappers would manufacture
     * unresolved __wrap_* references and violate -nodefaultlibs/-nostdlib. */
    if (da->fcgf_safe && want_libs)
        for (i = 0; i < CGF_ARRAY_LEN(safe_wraps); i++)
            VecStr_push(out, safe_wraps[i]);
    for (i = 0; i < da->lib_dirs.len; i++)
        VecStr_push(out, joined2(ar, "-L", da->lib_dirs.data[i]));
    if (crtdir)
        VecStr_push(out, joined2(ar, "-L", crtdir));
    if (want_crts) {
        VecStr_push(out, joined2(ar, crtdir, "/crt1.o"));
        VecStr_push(out, joined2(ar, crtdir, "/crti.o"));
    }
    for (i = 0; i < da->link_inputs.len; i++) {
        const LinkInput *li = &da->link_inputs.data[i];

        if (!li->val)
            continue; /* a TU that never produced its object */
        if (li->kind == LINK_LIB)
            VecStr_push(out, joined2(ar, "-l", li->val));
        else
            VecStr_push(out, li->val);
    }
    if (want_libs) {
        rt = locate_rt_archive(t);
        if (da->static_link) {
            /* F-S27-STATICLIBGCC: glibc's libc.a needs libgcc_eh's
             * unwind symbols; mirror gcc's static group when the
             * private libgcc dir exists. */
            const char *gccdir = locate_libgcc_dir();

            if (gccdir)
                VecStr_push(out, joined2(ar, "-L", gccdir));
            VecStr_push(out, "--start-group");
            if (rt)
                VecStr_push(out, rt);
            if (gccdir) {
                VecStr_push(out, "-lgcc");
                VecStr_push(out, "-lgcc_eh");
            }
            VecStr_push(out, "-lc");
            VecStr_push(out, "--end-group");
        } else {
            if (rt)
                VecStr_push(out, rt);
            VecStr_push(out, "-lc");
        }
    }
    if (want_crts)
        VecStr_push(out, joined2(ar, crtdir, "/crtn.o"));
    VecStr_push(out, NULL); /* argv terminator */
    return true;
}

void cgf_toolchain_echo_argv(const char *const argv[])
{
    echo_argv_line(argv);
}

void cgf_echo_as_plan(const char *s_path, const char *o_path)
{
    ToolchainConfig tc = cgf_toolchain_resolve(cgf_target_host());
    const char *argv[6];
    int n = 0;

    argv[n++] = tc.as_path ? tc.as_path : "as";
    if (tc.as_path && tc.use_afs_as) {
        const char *arm = cgf_afs_as_target_flag(cgf_target_host());

        if (arm)
            argv[n++] = arm;
    } else {
        argv[n++] = "--nocompress-debug-sections";
    }
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
