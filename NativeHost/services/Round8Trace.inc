// Opt-in service diagnostics. Never include paths, request buffers or guest data.
#ifndef SATCHEL_ROUND8_TRACE
#define SATCHEL_ROUND8_TRACE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
static void satchel_round8_trace(const char *service, unsigned command, uint32_t result) {
    const char *enabled = getenv("DM_SF_TRACE");
    if (enabled && atoi(enabled)) {
        fprintf(stderr, "[dm] Round8 service=%s command=%u result=0x%x\n", service, command, result);
        fflush(stderr);
    }
}
#endif
