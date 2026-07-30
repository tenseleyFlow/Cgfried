#ifndef CGF_TEST_SPAWN_H
#define CGF_TEST_SPAWN_H

#include <stdbool.h>

#include "util/buf.h"

typedef struct {
    bool spawned;    /* false: exec itself failed (missing binary...) */
    bool exited;     /* normal exit (vs signal) */
    int exit_code;   /* valid when exited */
    int term_signal; /* valid when !exited */
    bool timed_out;  /* SIGKILLed by us at the deadline */
    Buf out;         /* captured stdout, binary-safe */
    Buf err;         /* captured stderr, binary-safe */
} SpawnResult;

/* Runs argv (PATH-searched) capturing both output streams, draining them
 * concurrently (reading one to EOF first deadlocks once the child fills the
 * other pipe's 64 KiB buffer). Kills with SIGKILL after timeout_secs. */
void spawn_capture(char *const argv[], int timeout_secs, SpawnResult *res);
void spawn_result_free(SpawnResult *res);

#endif
