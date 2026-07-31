#ifndef CGF_DRIVER_DEPS_H
#define CGF_DRIVER_DEPS_H

#include "driver/args.h"
#include "pp/pp.h"
#include "util/buf.h"

struct Arena;

/* Sprint 26: the -M depfile writer, gcc mkdeps-shaped byte for byte
 * (72-column " \\\n " wrapping, Make-quoted paths, phony rules under
 * -MP). Prerequisites are pp->files in load order — the main file first
 * — with pseudo-files ("<...>") always skipped, system headers skipped
 * under -MM, and duplicate opens (a guard-less header included twice)
 * deduped. default_target is used only when no -MT/-MQ was given. */
void cgf_deps_write(Buf *out, const DriverArgs *a, struct Arena *ar,
                    const char *default_target, const Preprocessor *pp);

#endif
