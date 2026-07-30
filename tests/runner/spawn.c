#include "spawn.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "diag.h"

extern char **environ;

static void cloexec(int fd)
{
    int flags = fcntl(fd, F_GETFD);

    if (flags < 0 || fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0)
        CGF_ICE("fcntl(FD_CLOEXEC) failed: %s", strerror(errno));
}

static i64 now_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        CGF_ICE("clock_gettime failed: %s", strerror(errno));
    return (i64)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void spawn_capture(char *const argv[], int timeout_secs, SpawnResult *res)
{
    int out_pipe[2], err_pipe[2];
    posix_spawn_file_actions_t fa;
    pid_t pid;
    int rc;

    memset(res, 0, sizeof(*res));
    buf_init(&res->out);
    buf_init(&res->err);

    if (pipe(out_pipe) != 0 || pipe(err_pipe) != 0)
        CGF_ICE("pipe failed: %s", strerror(errno));
    /* CLOEXEC on all four: the spawn dup2 below clears it on the child's
     * fds 1/2; everything else must not leak into children. */
    cloexec(out_pipe[0]);
    cloexec(out_pipe[1]);
    cloexec(err_pipe[0]);
    cloexec(err_pipe[1]);

    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, out_pipe[1], 1);
    posix_spawn_file_actions_adddup2(&fa, err_pipe[1], 2);

    rc = posix_spawnp(&pid, argv[0], &fa, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    close(out_pipe[1]);
    close(err_pipe[1]);

    if (rc != 0) {
        close(out_pipe[0]);
        close(err_pipe[0]);
        res->spawned = false;
        return;
    }
    res->spawned = true;

    /* Drain both pipes concurrently until EOF on both. */
    {
        struct pollfd pfd[2];
        bool open_out = true, open_err = true;
        i64 deadline = now_ms() + (i64)timeout_secs * 1000;

        while (open_out || open_err) {
            int n = 0;
            int poll_ms;
            i64 remain = deadline - now_ms();

            if (remain <= 0 && !res->timed_out) {
                kill(pid, SIGKILL);
                res->timed_out = true;
            }
            /* After a kill, pipes still need draining to EOF. */
            poll_ms = res->timed_out ? 1000 : (int)(remain > 0 ? remain : 0);

            if (open_out) {
                pfd[n].fd = out_pipe[0];
                pfd[n].events = POLLIN;
                n++;
            }
            if (open_err) {
                pfd[n].fd = err_pipe[0];
                pfd[n].events = POLLIN;
                n++;
            }
            rc = poll(pfd, (nfds_t)n, poll_ms);
            if (rc < 0) {
                if (errno == EINTR)
                    continue;
                CGF_ICE("poll failed: %s", strerror(errno));
            }
            if (rc == 0)
                continue; /* timeout tick: loop re-checks the deadline */

            {
                int k;
                for (k = 0; k < n; k++) {
                    char chunk[65536];
                    ssize_t got;
                    bool is_out;

                    if (!(pfd[k].revents & (POLLIN | POLLHUP | POLLERR)))
                        continue;
                    is_out = pfd[k].fd == out_pipe[0];
                    got = read(pfd[k].fd, chunk, sizeof(chunk));
                    if (got < 0) {
                        if (errno == EINTR)
                            continue;
                        CGF_ICE("read from child pipe failed: %s",
                                strerror(errno));
                    }
                    if (got == 0) {
                        close(pfd[k].fd);
                        if (is_out)
                            open_out = false;
                        else
                            open_err = false;
                    } else {
                        buf_append(is_out ? &res->out : &res->err, chunk,
                                   (size_t)got);
                    }
                }
            }
        }
    }

    /* Reap, looping on EINTR. Signal deaths are reported as signals, never
     * disguised as exit codes. */
    {
        int status;

        for (;;) {
            if (waitpid(pid, &status, 0) >= 0)
                break;
            if (errno != EINTR)
                CGF_ICE("waitpid failed: %s", strerror(errno));
        }
        if (WIFEXITED(status)) {
            res->exited = true;
            res->exit_code = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            res->exited = false;
            res->term_signal = WTERMSIG(status);
        } else {
            CGF_ICE("waitpid: unexpected status 0x%x", (unsigned)status);
        }
    }
}

void spawn_result_free(SpawnResult *res)
{
    buf_free(&res->out);
    buf_free(&res->err);
}
