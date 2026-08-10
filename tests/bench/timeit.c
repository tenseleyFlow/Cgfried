#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#else
#define _GNU_SOURCE
#endif

#include "timeit.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    double wall_ms;
    double user_ms;
    double sys_ms;
    long maxrss_kb;
} TimeitSample;

static void sort_doubles(double *values, size_t count)
{
    size_t i;

    for (i = 1; i < count; i++) {
        double value = values[i];
        size_t j = i;

        while (j > 0 && values[j - 1] > value) {
            values[j] = values[j - 1];
            j--;
        }
        values[j] = value;
    }
}

double cgf_timeit_median(const double *samples, size_t count)
{
    double *sorted;
    double result;

    if (!samples || count == 0 || count > SIZE_MAX / sizeof(*sorted))
        return -1.0;
    sorted = malloc(count * sizeof(*sorted));
    if (!sorted)
        return -1.0;
    memcpy(sorted, samples, count * sizeof(*sorted));
    sort_doubles(sorted, count);
    if (count % 2 != 0)
        result = sorted[count / 2];
    else
        result = sorted[count / 2 - 1] / 2.0 + sorted[count / 2] / 2.0;
    free(sorted);
    return result;
}

double cgf_timeit_mad(const double *samples, size_t count)
{
    double median;
    double *deviations;
    double result;
    size_t i;

    if (!samples || count == 0 || count > SIZE_MAX / sizeof(*deviations))
        return -1.0;
    median = cgf_timeit_median(samples, count);
    if (median < 0.0)
        return -1.0;
    deviations = malloc(count * sizeof(*deviations));
    if (!deviations)
        return -1.0;
    for (i = 0; i < count; i++)
        deviations[i] =
            samples[i] < median ? median - samples[i] : samples[i] - median;
    result = cgf_timeit_median(deviations, count);
    free(deviations);
    return result;
}

long cgf_timeit_maxrss_kb(long raw_maxrss, int raw_is_bytes)
{
    return raw_is_bytes ? raw_maxrss / 1024 : raw_maxrss;
}

#ifndef CGF_TIMEIT_NO_MAIN

static int child_signal_pipe[2] = {-1, -1};

static void handle_sigchld(int signal_number)
{
    unsigned char byte = 0;
    int saved_errno = errno;

    (void)signal_number;
    if (child_signal_pipe[1] >= 0)
        (void)write(child_signal_pipe[1], &byte, 1);
    errno = saved_errno;
}

static void usage(const char *program)
{
    fprintf(stderr,
            "usage: %s [-n RUNS] [-w WARMUP] [-t SECONDS] "
            "[-o OUTFILE] -- cmd args...\n",
            program);
}

static int parse_count(const char *text, int allow_zero, size_t *result)
{
    unsigned long long value;
    char *end;

    if (!text || text[0] == '\0' || text[0] == '-')
        return 0;
    errno = 0;
    value = strtoull(text, &end, 10);
    if (errno == ERANGE || *end != '\0' || (!allow_zero && value == 0) ||
        value > SIZE_MAX)
        return 0;
    *result = (size_t)value;
    return 1;
}

static double timespec_ms(struct timespec start, struct timespec end)
{
    return (double)(end.tv_sec - start.tv_sec) * 1000.0 +
           (double)(end.tv_nsec - start.tv_nsec) / 1000000.0;
}

static double timeval_ms(struct timeval value)
{
    return (double)value.tv_sec * 1000.0 + (double)value.tv_usec / 1000.0;
}

static clockid_t timeit_clock_id(void)
{
#ifdef CLOCK_MONOTONIC_RAW
    return CLOCK_MONOTONIC_RAW;
#else
    return CLOCK_MONOTONIC;
#endif
}

static int set_pipe_flags(int fd)
{
    int flags;

    flags = fcntl(fd, F_GETFL);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0)
        return 0;
    flags = fcntl(fd, F_GETFD);
    if (flags < 0 || fcntl(fd, F_SETFD, flags | FD_CLOEXEC) != 0)
        return 0;
    return 1;
}

static int setup_child_notifications(struct sigaction *old_action)
{
    struct sigaction action;

    if (pipe(child_signal_pipe) != 0) {
        perror("timeit: pipe");
        return 0;
    }
    if (!set_pipe_flags(child_signal_pipe[0]) ||
        !set_pipe_flags(child_signal_pipe[1])) {
        perror("timeit: fcntl");
        close(child_signal_pipe[0]);
        close(child_signal_pipe[1]);
        child_signal_pipe[0] = -1;
        child_signal_pipe[1] = -1;
        return 0;
    }
    memset(&action, 0, sizeof(action));
    action.sa_handler = handle_sigchld;
    action.sa_flags = SA_NOCLDSTOP;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGCHLD, &action, old_action) != 0) {
        perror("timeit: sigaction");
        close(child_signal_pipe[0]);
        close(child_signal_pipe[1]);
        child_signal_pipe[0] = -1;
        child_signal_pipe[1] = -1;
        return 0;
    }
    return 1;
}

static void drain_child_notifications(void)
{
    unsigned char bytes[64];

    while (read(child_signal_pipe[0], bytes, sizeof(bytes)) > 0)
        ;
}

static int deadline_poll_ms(clockid_t clock_id, const struct timespec *deadline,
                            int *milliseconds)
{
    struct timespec now;
    time_t seconds;
    long nanoseconds;

    if (clock_gettime(clock_id, &now) != 0) {
        perror("timeit: clock_gettime");
        return -1;
    }
    seconds = deadline->tv_sec - now.tv_sec;
    nanoseconds = deadline->tv_nsec - now.tv_nsec;
    if (nanoseconds < 0) {
        seconds--;
        nanoseconds += 1000000000L;
    }
    if (seconds < 0 || (seconds == 0 && nanoseconds == 0))
        return 0;
    if (seconds > INT_MAX / 1000) {
        *milliseconds = INT_MAX;
    } else {
        long rounded_ms = (nanoseconds + 999999L) / 1000000L;
        long total_ms = (long)seconds * 1000L + rounded_ms;

        *milliseconds = total_ms > INT_MAX ? INT_MAX : (int)total_ms;
        if (*milliseconds == 0)
            *milliseconds = 1;
    }
    return 1;
}

static int kill_and_reap(pid_t child, int *status, struct rusage *usage)
{
    pid_t waited;
    int failed = 0;

    if (kill(-child, SIGKILL) != 0 && errno != ESRCH) {
        perror("timeit: kill process group");
        failed = 1;
    }
    if (kill(child, SIGKILL) != 0 && errno != ESRCH) {
        perror("timeit: kill child");
        failed = 1;
    }
    do {
        waited = wait4(child, status, 0, usage);
    } while (waited < 0 && errno == EINTR);
    if (waited < 0) {
        perror("timeit: wait4");
        return 0;
    }
    return !failed;
}

static int wait_timed_child(pid_t child, int *status, struct rusage *usage,
                            clockid_t clock_id, const struct timespec *deadline,
                            int *timed_out)
{
    struct pollfd signal_fd;

    signal_fd.fd = child_signal_pipe[0];
    signal_fd.events = POLLIN;
    for (;;) {
        pid_t waited;
        int poll_ms, deadline_state, poll_result;

        do {
            waited = wait4(child, status, WNOHANG, usage);
        } while (waited < 0 && errno == EINTR);
        if (waited == child)
            return 0;
        if (waited < 0) {
            perror("timeit: wait4");
            return 2;
        }

        deadline_state = deadline_poll_ms(clock_id, deadline, &poll_ms);
        if (deadline_state <= 0) {
            if (!kill_and_reap(child, status, usage))
                return 2;
            if (deadline_state < 0)
                return 2;
            *timed_out = 1;
            return 124;
        }

        signal_fd.revents = 0;
        poll_result = poll(&signal_fd, 1, poll_ms);
        if (poll_result < 0 && errno != EINTR) {
            perror("timeit: poll");
            if (!kill_and_reap(child, status, usage))
                return 2;
            return 2;
        }
        if (poll_result > 0)
            drain_child_notifications();
    }
}

static int run_child(char **command, TimeitSample *sample,
                     const struct timespec *deadline, int *timed_out)
{
    struct timespec start, end;
    struct rusage usage;
    pid_t child, waited;
    int status, wait_result;
    const clockid_t clock_id = timeit_clock_id();

    *timed_out = 0;
    if (clock_gettime(clock_id, &start) != 0) {
        perror("timeit: clock_gettime");
        return 2;
    }
    child = fork();
    if (child < 0) {
        perror("timeit: fork");
        return 2;
    }
    if (child == 0) {
        (void)setpgid(0, 0);
        if (child_signal_pipe[0] >= 0)
            close(child_signal_pipe[0]);
        if (child_signal_pipe[1] >= 0)
            close(child_signal_pipe[1]);
        execvp(command[0], command);
        perror("timeit: execvp");
        _exit(127);
    }
    (void)setpgid(child, child);
    if (deadline) {
        wait_result = wait_timed_child(child, &status, &usage, clock_id,
                                       deadline, timed_out);
        if (wait_result != 0)
            return wait_result;
    } else {
        do {
            waited = wait4(child, &status, 0, &usage);
        } while (waited < 0 && errno == EINTR);
        if (waited < 0) {
            perror("timeit: wait4");
            return 2;
        }
    }
    if (clock_gettime(clock_id, &end) != 0) {
        perror("timeit: clock_gettime");
        return 2;
    }
    if (WIFSIGNALED(status))
        return 128 + WTERMSIG(status);
    if (!WIFEXITED(status))
        return 2;
    if (WEXITSTATUS(status) != 0)
        return WEXITSTATUS(status);

    if (sample) {
        sample->wall_ms = timespec_ms(start, end);
        sample->user_ms = timeval_ms(usage.ru_utime);
        sample->sys_ms = timeval_ms(usage.ru_stime);
#ifdef __APPLE__
        sample->maxrss_kb = cgf_timeit_maxrss_kb(usage.ru_maxrss, 1);
#else
        sample->maxrss_kb = cgf_timeit_maxrss_kb(usage.ru_maxrss, 0);
#endif
    }
    return 0;
}

static int write_raw_sample(FILE *out, size_t index, const TimeitSample *sample)
{
    return fprintf(out,
                   "sample=%zu wall_ms=%.6f user_ms=%.6f sys_ms=%.6f "
                   "maxrss_kb=%ld\n",
                   index, sample->wall_ms, sample->user_ms, sample->sys_ms,
                   sample->maxrss_kb) >= 0;
}

int main(int argc, char **argv)
{
    size_t runs = 10, warmups = 1, timeout_seconds = 0, i;
    const char *output_path = NULL;
    TimeitSample *samples = NULL;
    double *wall = NULL, *user = NULL, *sys = NULL;
    double wall_median, wall_mad, user_median, sys_median;
    FILE *raw = NULL;
    struct sigaction old_sigchld;
    struct timespec deadline;
    int timeout_installed = 0;
    long maxrss = 0;
    int argi = 1;
    int result = 2;
    int timed_out = 0;

    while (argi < argc && strcmp(argv[argi], "--") != 0) {
        if (strcmp(argv[argi], "-n") == 0 && argi + 1 < argc) {
            if (!parse_count(argv[argi + 1], 0, &runs))
                goto bad_usage;
            argi += 2;
        } else if (strcmp(argv[argi], "-w") == 0 && argi + 1 < argc) {
            if (!parse_count(argv[argi + 1], 1, &warmups))
                goto bad_usage;
            argi += 2;
        } else if (strcmp(argv[argi], "-t") == 0 && argi + 1 < argc) {
            if (!parse_count(argv[argi + 1], 0, &timeout_seconds) ||
                timeout_seconds > INT_MAX)
                goto bad_usage;
            argi += 2;
        } else if (strcmp(argv[argi], "-o") == 0 && argi + 1 < argc) {
            output_path = argv[argi + 1];
            argi += 2;
        } else {
            goto bad_usage;
        }
    }
    if (argi >= argc || strcmp(argv[argi], "--") != 0 || ++argi >= argc)
        goto bad_usage;
    if (runs > SIZE_MAX / sizeof(*samples)) {
        fprintf(stderr, "timeit: run count is too large\n");
        goto cleanup;
    }

    samples = calloc(runs, sizeof(*samples));
    wall = malloc(runs * sizeof(*wall));
    user = malloc(runs * sizeof(*user));
    sys = malloc(runs * sizeof(*sys));
    if (!samples || !wall || !user || !sys) {
        fprintf(stderr, "timeit: out of memory\n");
        goto cleanup;
    }
    if (output_path) {
        raw = fopen(output_path, "w");
        if (!raw) {
            fprintf(stderr, "timeit: cannot open %s: %s\n", output_path,
                    strerror(errno));
            goto cleanup;
        }
    }

    if (timeout_seconds != 0) {
        if (!setup_child_notifications(&old_sigchld))
            goto cleanup;
        timeout_installed = 1;
        if (clock_gettime(timeit_clock_id(), &deadline) != 0) {
            perror("timeit: clock_gettime");
            goto cleanup;
        }
        deadline.tv_sec += (time_t)timeout_seconds;
    }

    for (i = 0; i < warmups; i++) {
        result = run_child(&argv[argi], NULL,
                           timeout_installed ? &deadline : NULL, &timed_out);
        if (result != 0) {
            if (timed_out)
                fprintf(stderr, "timeit: timeout after %zu seconds\n",
                        timeout_seconds);
            goto cleanup;
        }
    }
    for (i = 0; i < runs; i++) {
        result = run_child(&argv[argi], &samples[i],
                           timeout_installed ? &deadline : NULL, &timed_out);
        if (result != 0) {
            if (timed_out)
                fprintf(stderr, "timeit: timeout after %zu seconds\n",
                        timeout_seconds);
            goto cleanup;
        }
        wall[i] = samples[i].wall_ms;
        user[i] = samples[i].user_ms;
        sys[i] = samples[i].sys_ms;
        if (samples[i].maxrss_kb > maxrss)
            maxrss = samples[i].maxrss_kb;
        if (raw && !write_raw_sample(raw, i + 1, &samples[i])) {
            perror("timeit: write");
            result = 2;
            goto cleanup;
        }
    }
    wall_median = cgf_timeit_median(wall, runs);
    wall_mad = cgf_timeit_mad(wall, runs);
    user_median = cgf_timeit_median(user, runs);
    sys_median = cgf_timeit_median(sys, runs);
    if (wall_median < 0.0 || wall_mad < 0.0 || user_median < 0.0 ||
        sys_median < 0.0) {
        fprintf(stderr, "timeit: out of memory while computing statistics\n");
        result = 2;
        goto cleanup;
    }
    printf("wall_ms_median=%.6f\n", wall_median);
    printf("wall_ms_mad=%.6f\n", wall_mad);
    printf("user_ms_median=%.6f\n", user_median);
    printf("sys_ms_median=%.6f\n", sys_median);
    printf("maxrss_kb_max=%ld\n", maxrss);
    if (ferror(stdout)) {
        perror("timeit: stdout");
        result = 2;
        goto cleanup;
    }
    result = 0;
    goto cleanup;

bad_usage:
    usage(argv[0]);
cleanup:
    if (timeout_installed) {
        if (sigaction(SIGCHLD, &old_sigchld, NULL) != 0 && result == 0) {
            perror("timeit: restore sigaction");
            result = 2;
        }
        close(child_signal_pipe[0]);
        close(child_signal_pipe[1]);
        child_signal_pipe[0] = -1;
        child_signal_pipe[1] = -1;
    }
    if (raw && fclose(raw) != 0 && result == 0) {
        perror("timeit: close");
        result = 2;
    }
    free(sys);
    free(user);
    free(wall);
    free(samples);
    return result;
}

#endif
