#define _GNU_SOURCE

#include <errno.h>
#include <stdio.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static double seconds_between(struct timespec start, struct timespec end)
{
    return (double)(end.tv_sec - start.tv_sec) +
           (double)(end.tv_nsec - start.tv_nsec) / 1000000000.0;
}

int main(int argc, char **argv)
{
    struct timespec start, end;
    struct rusage usage;
    int status;
    pid_t child, waited;

    if (argc != 2) {
        fprintf(stderr, "usage: %s PROGRAM\n", argv[0]);
        return 2;
    }
    if (clock_gettime(CLOCK_MONOTONIC, &start) != 0) {
        perror("clock_gettime");
        return 2;
    }
    child = fork();
    if (child < 0) {
        perror("fork");
        return 2;
    }
    if (child == 0) {
        execl(argv[1], argv[1], (char *)NULL);
        _exit(127);
    }
    do {
        waited = wait4(child, &status, 0, &usage);
    } while (waited < 0 && errno == EINTR);
    if (waited < 0 || clock_gettime(CLOCK_MONOTONIC, &end) != 0) {
        perror(waited < 0 ? "wait4" : "clock_gettime");
        return 2;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    printf("%.6f %ld\n", seconds_between(start, end), usage.ru_maxrss);
    return 0;
}
