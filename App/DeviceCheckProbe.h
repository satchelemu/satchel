#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    int increased_memory;
    int extended_addressing;
    uint64_t available_memory;
    uint64_t footprint;
    uint64_t process_limit;
    uint64_t address_span;
    uint64_t reserved_bytes;
    bool has_limit;
    bool has_footprint;
    bool has_address_span;
} SatchelDeviceMeasurements;

SatchelDeviceMeasurements SatchelMeasureDevice(void);
