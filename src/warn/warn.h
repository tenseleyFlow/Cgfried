#ifndef CGF_WARN_H
#define CGF_WARN_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "diag.h"
#include "warn/id.h"

typedef enum WarnGroup {
    WG_NONE = 0,
    WG_ALL = 1u << 0,
    WG_EXTRA = 1u << 1,
    WG_UNUSED = 1u << 2,
    WG_FORMAT = 1u << 3,
    WG_IMPLICIT = 1u << 4,
    WG_PEDANTIC = 1u << 5,
    WG_CGF_EXT = 1u << 6,
    WG_FORMAT2 = 1u << 7,
    WG_MEM = 1u << 8,
    WG_MEM_STRICT = 1u << 9
} WarnGroup;

typedef enum WarnDefault {
    WD_OFF,
    WD_ON,
    WD_WALL,
    WD_WEXTRA,
    WD_PEDANTIC
} WarnDefault;

typedef enum WarnLevel { WL_WARN, WL_PEDWARN } WarnLevel;

typedef struct WarnInfo {
    WarnId id;
    const char *flag;
    unsigned groups;
    WarnDefault default_state;
    WarnLevel level;
} WarnInfo;

typedef enum WarnPragmaClass {
    WARN_PRAGMA_IGNORED,
    WARN_PRAGMA_WARNING,
    WARN_PRAGMA_ERROR
} WarnPragmaClass;

typedef enum WarnEmitFlags {
    WARN_EMIT_NONE = 0,
    WARN_SUPPRESS_IN_MACRO = 1u << 0
} WarnEmitFlags;

typedef enum WarnOptionDisposition {
    WARN_OPTION_KNOWN,
    WARN_OPTION_UNKNOWN_POSITIVE,
    WARN_OPTION_UNKNOWN_NEGATIVE,
    WARN_OPTION_UNKNOWN_PROMOTION,
    WARN_OPTION_BAD_FORMAT_LEVEL,
    WARN_OPTION_BAD_IMPLICIT_FALLTHROUGH_LEVEL,
    WARN_OPTION_BAD_MAYBE_UNINITIALIZED_LEVEL
} WarnOptionDisposition;

typedef struct WarnCtx WarnCtx;
struct Arena;
struct IrModule;

/* Internal driver-policy operation appended after argv warning options. */
#define WARN_FSAFE_REQUIRED_OPTION "\001fsafe-required"

WarnCtx *warn_ctx_new(struct Arena *arena, DiagCtx *diag);
DiagCtx *warn_diag(WarnCtx *w);

size_t warn_info_count(void);
const WarnInfo *warn_info_at(size_t index);
const WarnInfo *warn_info_for_id(WarnId id);
const WarnInfo *warn_info_for_flag(const char *flag);
const char *warn_flag_name(WarnId id);
WarnOptionDisposition warn_option_classify(const char *arg);
bool warn_option_known(const char *arg);
const char *warn_option_bad_value_label(WarnOptionDisposition disposition);
WarnId warn_pragma_option_id(const char *option);
void warn_print_help(FILE *out);

/* Accepts command-line spelling with or without the leading '-W'. */
bool warn_flag(WarnCtx *w, const char *arg);
bool warn_enabled(const WarnCtx *w, WarnId id, Span sp);
bool warn_explicitly_enabled(const WarnCtx *w, WarnId id, Span sp);
unsigned warn_implicit_fallthrough_level(const WarnCtx *w);
bool warn_maybe_uninitialized_strict(const WarnCtx *w);
bool warn_flow_needed(const WarnCtx *w);
bool warn_memsafe_needed(const WarnCtx *w);
bool warn_memsafe_autofix_needed(const WarnCtx *w);
bool warn_mem_strict_enabled(const WarnCtx *w);
void warn_flow_module(WarnCtx *w, const struct IrModule *module);

void warn_at(WarnCtx *w, WarnId id, Span sp, const char *fmt, ...);
/* Emits one occurrence as a pedwarn even when its registry row is also used
 * by ordinary warnings. `-pedantic-errors` may therefore promote this
 * occurrence without promoting ordinary occurrences of the same -W flag. */
void warn_pedwarn_at(WarnCtx *w, WarnId id, Span sp, const char *fmt, ...);
void warn_at_v(WarnCtx *w, WarnId id, Span sp, const char *fmt, va_list ap);
void warn_at_ex(WarnCtx *w, WarnId id, Span sp, unsigned emit_flags,
                const char *fmt, ...);
/* Warning-policy preserving emission with source edits. This is the only
 * supported path for warning fix-its: it honors pragmas, system-header
 * suppression, -w and -Werror before forwarding to the diagnostic layer. */
void warn_at_fixits(WarnCtx *w, WarnId id, Span sp, const DiagFixit *fixits,
                    size_t fixit_count, const char *fmt, ...);

void warn_pragma_push(WarnCtx *w, u32 seq);
bool warn_pragma_pop(WarnCtx *w, u32 seq, Span at);
void warn_pragma_set(WarnCtx *w, u32 seq, WarnId id,
                     WarnPragmaClass classification);
bool warn_pragma_set_flag(WarnCtx *w, u32 seq, const char *flag,
                          WarnPragmaClass classification);

#endif
