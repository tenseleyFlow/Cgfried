#ifndef CGF_DRIVER_TOOLCHAIN_H
#define CGF_DRIVER_TOOLCHAIN_H

#include <stdbool.h>
#include <stddef.h>

#include "driver/args.h"
#include "target.h"
#include "util/base.h"
#include "util/buf.h"

struct Arena;

/* Toolchain routing + subprocess layer. All CGF_* toolchain variables are
 * read in exactly one place (env_override in toolchain.c) — grep-enforced by
 * scripts/check_bans.sh — so the routing table in --help and README stays
 * truthful.
 *
 * Routing (resolution order per tool: explicit path var > mode var > default):
 *   CGF_AS       unset/1: bundled afs-as (default)   0: system `as`
 *   CGF_AS_PATH  <bin>: exactly this assembler (wins over CGF_AS)
 *   CGF_LD       unset/0: system `ld` (default)      1: afs-ld (Sprint 27)
 *   CGF_LD_PATH  <bin>: exactly this linker (wins over CGF_LD)
 *   CGF_CRT_DIR  <dir>: crt discovery override (stored now, used Sprint 27)
 * Empty-string values are treated as unset. */

typedef struct {
    const char *as_path; /* resolved assembler; NULL = bundled, not yet
                            located (cgf_toolchain_resolve fills it) */
    const char *ld_path; /* resolved linker binary */
    const char *crt_dir; /* CGF_CRT_DIR, consumed in Sprint 27 */
    bool use_afs_as;     /* CGF_AS routing result */
    bool use_afs_ld;     /* CGF_LD routing result */
    const char *error;   /* resolution error (exit 1 at the driver), or the
                            bundled-tool-missing message (exit 3); NULL = ok */
    bool error_is_io;    /* true: map error to exit 3, else exit 1 */
} ToolchainConfig;

/* Env-lookup callback so routing decisions unit-test as a pure function. */
typedef const char *(*ToolchainGetenv)(const char *name, void *ctx);

/* Routing decisions only — no filesystem probing (that is what keeps it
 * pure). Bundled-assembler location is layered on top by
 * cgf_toolchain_resolve. */
ToolchainConfig cgf_toolchain_resolve_from(ToolchainGetenv fn, void *ctx,
                                           TargetSpec t);

/* Real environment + bundled-tool discovery, lazily computed and cached. */
ToolchainConfig cgf_toolchain_resolve(TargetSpec t);

/* Must be called once at startup (fallback for /proc/self/exe discovery). */
void cgf_toolchain_set_argv0(const char *argv0);

typedef enum {
    TOOL_EXITED,
    TOOL_SIGNALED,
    TOOL_SPAWN_FAILED,
} ToolRunKind;

typedef struct {
    ToolRunKind kind;
    int exit_code;   /* TOOL_EXITED */
    int term_signal; /* TOOL_SIGNALED */
    int spawn_errno; /* TOOL_SPAWN_FAILED */
} ToolResult;

/* Runs argv[0] (PATH-searched) with stdout/stderr INHERITED — afs-as emits
 * file:line:col caret diagnostics of its own; never wrap, prefix, reflow, or
 * buffer them (inheritance is also why a tool spewing megabytes of stderr
 * cannot deadlock us: there is no pipe to fill). */
ToolResult cgf_run_tool(const char *const argv[]);

typedef enum {
    TOOL_AS,
    TOOL_LD,
} ToolKind;

/* Maps a failed ToolResult onto the exit-code contract:
 * spawn ENOENT -> 3; assembler failure on USER input -> 1 (its diagnostic
 * already printed); assembler failure on cgfried-GENERATED assembly -> 4
 * (our emission was invalid — Sprint 24 owns that path); linker failure -> 2.
 * Success maps to 0. */
int cgf_tool_exit_code(ToolKind which, const ToolResult *res, bool user_input);

/* Sprint 24: run the resolved assembler on s_path -> o_path with
 * stdout/stderr CAPTURED, echoed line-by-line as "[as] ...". On
 * failure, *diag_line holds the first ".s:<line>:" the assembler
 * reported (0 if none) so the driver can quote the offending line. */
ToolResult cgf_run_assembler(const char *s_path, const char *o_path,
                             u32 *diag_line);

/* Sprint 25: crt1.o directory probe (CGF_CRT_DIR override, then the
 * Arch and Debian layouts); NULL = miss, with the probed paths written
 * to diag for the exit-2 link error. */
const char *cgf_probe_crt_dir(char *diag, size_t diag_sz);

/* Sprint 27: the canonical link line, built straight from DriverArgs
 * (position-preserving link_inputs, -L hoisted, -B feeding the crt
 * probe, -static grouping, the subtraction flags). The out vec receives
 * a NULL-terminated argv (VecStr from args.h); false = the failure
 * diagnostic (every probed crt path named, one per line) already
 * printed — exit 2 at the driver. */
bool toolchain_build_link_argv(const DriverArgs *da, TargetSpec t,
                               struct Arena *ar, VecStr *out);

/* Injectable crt probe core (units point `table` at temp dirs): first
 * dir whose crt1.o stats wins; every probed path appends to `searched`
 * one per line. An explicit override that misses does NOT fall through. */
const char *cgf_probe_crt_dir_in(const char *override, const char *const *bdirs,
                                 size_t nb, const char *const *table,
                                 size_t ntable, Buf *searched);

/* -v/-### support: when echo is on, every subprocess argv prints
 * shell-quoted to stderr before running; the plan variants print WITHOUT
 * running (the -### contract). */
void cgf_toolchain_set_echo(bool verbose);
const char *cgf_toolchain_argv0(void);
void cgf_echo_as_plan(const char *s_path, const char *o_path);
void cgf_toolchain_echo_argv(const char *const argv[]);

/* PATH resolution for -print-prog-name= (a name containing '/' probes
 * directly). False when not found; out untouched. */
bool cgf_which(const char *name, char *out, size_t out_size);

/* The guidance string for a missing tool (exit-3 diagnostics). */
const char *cgf_tool_missing_hint(ToolKind which);

/* Builds "<dir-of-our-binary><suffix>" into out (e.g. "/../include" for
 * the shipped freestanding headers). False if the exe dir is unknown or
 * the result would not fit. */
bool cgf_exe_relative(const char *suffix, char *out, size_t out_size);

/* Sprint 28: the shipped freestanding-header directory (exactly the
 * nine compiler-owned headers). CGF_INCLUDE_DIR overrides; otherwise
 * the installed layout then the dev tree, probed for stddef.h. NULL
 * when none exists. */
const char *cgf_shipped_include_dir(void);

/* Non-toolchain CGF_* variables (debug/testing knobs like
 * CGF_PP_DUMP_TOKENS) ALSO route through this translation unit, so the
 * single-getenv-site ban stays absolute. NULL when unset or empty. */
const char *cgf_env(const char *name);

#endif
