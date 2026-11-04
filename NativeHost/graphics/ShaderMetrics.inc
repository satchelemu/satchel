// Payload-free, opt-in diagnostic events. Paired with SatchelShaderMetrics.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
static uint64_t satchel_shader_now(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + t.tv_nsec;
}
static void satchel_shader_event(const char *kind, uint64_t start) {
    const char *path = getenv("DM_SHADER_EVENTS"); if (!path) return;
    uint64_t end = satchel_shader_now(), tid = 0; pthread_threadid_np(NULL, &tid);
    char row[160]; int n = snprintf(row, sizeof row, "%llu\t%llu\t%llu\t%s\n",
        (unsigned long long)start, (unsigned long long)(end - start), (unsigned long long)tid, kind);
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd >= 0) { if (n > 0 && n < sizeof row) (void)write(fd, row, n); close(fd); }
}
#define SATCHEL_SHADER_TIME(kind, expr) ({ \
    int satchel_enabled = getenv("DM_SHADER_EVENTS") != NULL; \
    uint64_t satchel_start = satchel_enabled ? satchel_shader_now() : 0; \
    __auto_type satchel_result = (expr); \
    if (satchel_enabled) satchel_shader_event(kind, satchel_start); \
    satchel_result; })
