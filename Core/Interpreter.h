#ifndef SATCHEL_INTERPRETER_H
#define SATCHEL_INTERPRETER_H
#include <stddef.h>
#include <stdint.h>
#include "NROLoader.h"

enum { SATCHEL_MEMORY_SIZE = 4 * 1024 * 1024, SATCHEL_CODE_BASE = 0x1000,
       SATCHEL_FRAMEBUFFER = 0x100000, SATCHEL_STATE = 0x200000, SATCHEL_WIDTH = 160, SATCHEL_HEIGHT = 240 };
typedef enum { SATCHEL_YIELD = 0, SATCHEL_BUDGET = 1, SATCHEL_FAULT = 2, SATCHEL_EXITED = 3, SATCHEL_WAIT = 4 } SatchelResult;
typedef struct SatchelVM SatchelVM;
SatchelVM *satchel_create(const uint8_t *code, size_t size);
SatchelVM *satchel_create_nro(const uint8_t *bytes, size_t size, char *error, size_t capacity);
void satchel_destroy(SatchelVM *vm);
SatchelResult satchel_frame(SatchelVM *vm, int touch, int reset, uint64_t budget);
const uint8_t *satchel_pixels(const SatchelVM *vm);
int32_t satchel_state(const SatchelVM *vm, unsigned index);
uint64_t satchel_instructions(const SatchelVM *vm);
uint64_t satchel_pc(const SatchelVM *vm);
uint64_t satchel_register(const SatchelVM *vm, unsigned index);
const char *satchel_error(const SatchelVM *vm);
uint64_t satchel_checksum(const SatchelVM *vm);
const char *satchel_output(const SatchelVM *vm);
uint64_t satchel_exit_code(const SatchelVM *vm);
void satchel_set_buttons(SatchelVM *vm, uint64_t buttons);
void satchel_clear_buttons(SatchelVM *vm);
const uint8_t *satchel_display_pixels(const SatchelVM *vm);
unsigned satchel_display_width(const SatchelVM *vm);
unsigned satchel_display_height(const SatchelVM *vm);
uint64_t satchel_presented_frames(const SatchelVM *vm);
uint64_t satchel_display_checksum(const SatchelVM *vm);
#endif
