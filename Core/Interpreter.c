#include "Interpreter.h"
#include "HorizonState.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct SatchelVM {
    SatchelHorizon horizon;
    uint8_t *memory;
    uint64_t x[32], pc, instructions;
    uint8_t vectors[32][16];
    size_t code_size;
    size_t memory_size;
    uint8_t permissions[SATCHEL_NRO_MEMORY_SIZE / 4096];
    uint8_t memory_type[SATCHEL_NRO_MEMORY_SIZE / 4096];
    uint8_t memory_attributes[SATCHEL_NRO_MEMORY_SIZE / 4096];
    bool reprotect[SATCHEL_NRO_MEMORY_SIZE / 4096];
    uint64_t exclusive_address, tpidr_el0;
    unsigned exclusive_size;
    bool exclusive_valid;
    bool n, z, c, v, yielded, fault, nro, exited;
    uint64_t exit_code, sleep_until;
    char output[16385];
    size_t output_size;
    char error[160];
};

static uint64_t mask_for(unsigned bits) { return bits == 64 ? UINT64_MAX : UINT32_MAX; }
static uint64_t zr(const SatchelVM *vm, unsigned r) { return r == 31 ? 0 : vm->x[r]; }
static void put(SatchelVM *vm, unsigned r, uint64_t value, unsigned bits, bool sp) {
    if (r != 31 || sp) vm->x[r] = value & mask_for(bits);
}
static int64_t sext(uint64_t value, unsigned bits) {
    uint64_t sign = UINT64_C(1) << (bits - 1);
    return (int64_t)((value ^ sign) - sign);
}
static void fault(SatchelVM *vm, const char *reason, uint64_t detail) {
    vm->fault = true;
    snprintf(vm->error, sizeof(vm->error), "%s: 0x%llx at PC 0x%llx", reason,
             (unsigned long long)detail, (unsigned long long)vm->pc);
}
static bool access_ok(SatchelVM *vm, uint64_t address, unsigned size, bool write) {
    if (write) vm->exclusive_valid = false;
    if (address > vm->memory_size || size > vm->memory_size - address) {
        fault(vm, "Guest memory outside sandbox", address); return false;
    }
    if (vm->nro) {
        if (size) for (uint64_t page = address / 4096; page <= (address + size - 1) / 4096; page++) {
            if (!(vm->permissions[page] & (write ? 2 : 1))) {
                fault(vm, write ? "Guest write to protected or unmapped page" : "Guest read from unmapped page", address); return false;
            }
        }
        return true;
    }
    if (write && address < SATCHEL_CODE_BASE + vm->code_size && address + size > SATCHEL_CODE_BASE) {
        fault(vm, "Guest write to read-only code", address); return false;
    }
    return true;
}
static uint64_t read_memory(SatchelVM *vm, uint64_t address, unsigned size) {
    uint64_t value = 0;
    if (access_ok(vm, address, size, false)) memcpy(&value, vm->memory + address, size);
    return value;
}
static void write_memory(SatchelVM *vm, uint64_t address, unsigned size, uint64_t value) {
    if (access_ok(vm, address, size, true)) memcpy(vm->memory + address, &value, size);
}
static bool condition(const SatchelVM *vm, unsigned cond) {
    bool result;
    switch (cond >> 1) {
        case 0: result = vm->z; break;
        case 1: result = vm->c; break;
        case 2: result = vm->n; break;
        case 3: result = vm->v; break;
        case 4: result = vm->c && !vm->z; break;
        case 5: result = vm->n == vm->v; break;
        case 6: result = !vm->z && vm->n == vm->v; break;
        default: return true;
    }
    return (cond & 1) ? !result : result;
}
static uint64_t arithmetic(SatchelVM *vm, uint64_t a, uint64_t b, unsigned bits, bool sub, bool flags) {
    uint64_t mask = mask_for(bits), sign = UINT64_C(1) << (bits - 1);
    a &= mask; b &= mask;
    uint64_t result = (sub ? a - b : a + b) & mask;
    if (flags) {
        vm->n = (result & sign) != 0; vm->z = result == 0;
        vm->c = sub ? a >= b : (bits == 64 ? result < a : a + b > mask);
        vm->v = ((sub ? (a ^ b) : ~(a ^ b)) & (a ^ result) & sign) != 0;
    }
    return result;
}
static uint64_t shift(uint64_t value, unsigned kind, unsigned amount, unsigned bits) {
    value &= mask_for(bits); amount &= bits - 1;
    if (!amount) return value;
    switch (kind) {
        case 0: return (value << amount) & mask_for(bits);
        case 1: return value >> amount;
        case 2: return (uint64_t)(sext(value, bits) >> amount) & mask_for(bits);
        default: return ((value >> amount) | (value << (bits - amount))) & mask_for(bits);
    }
}
static uint64_t extend(uint64_t value, unsigned option) {
    unsigned bits = 8u << (option & 3);
    if (bits == 64) return value;
    value &= (UINT64_C(1) << bits) - 1;
    return option & 4 ? (uint64_t)sext(value, bits) : value;
}

SatchelVM *satchel_create(const uint8_t *code, size_t size) {
    if (!code || !size || size % 4 || size > SATCHEL_FRAMEBUFFER - SATCHEL_CODE_BASE) return NULL;
    SatchelVM *vm = calloc(1, sizeof(*vm));
    if (!vm) return NULL;
    vm->memory = calloc(1, SATCHEL_MEMORY_SIZE);
    vm->memory_size = SATCHEL_MEMORY_SIZE;
    if (!vm->memory) { free(vm); return NULL; }
    memcpy(vm->memory + SATCHEL_CODE_BASE, code, size);
    vm->code_size = size; vm->pc = SATCHEL_CODE_BASE; vm->x[31] = SATCHEL_MEMORY_SIZE - 16;
    return vm;
}
void satchel_destroy(SatchelVM *vm) { if (vm) { free(vm->horizon.pixels); free(vm->memory); free(vm); } }

static void map_pages(SatchelVM *vm, uint32_t address, uint32_t length, uint8_t permission) {
    if (length) for (uint32_t page = address / 4096; page <= (address + length - 1) / 4096; page++) { vm->permissions[page] = permission; vm->memory_type[page] = permission ? 2 : 0; vm->reprotect[page] = (permission & 2) != 0; }
}
static void config_entry(SatchelVM *vm, unsigned index, uint32_t key, uint32_t flags, uint64_t a, uint64_t b) {
    uint8_t *p = vm->memory + SATCHEL_NRO_CONFIG + index * 24;
    memcpy(p, &key, 4); memcpy(p + 4, &flags, 4); memcpy(p + 8, &a, 8); memcpy(p + 16, &b, 8);
}
SatchelVM *satchel_create_nro(const uint8_t *bytes, size_t size, char *error, size_t capacity) {
    SatchelNROInfo info;
    if (!satchel_nro_inspect(bytes, size, &info, error, capacity)) return NULL;
    SatchelVM *vm = calloc(1, sizeof(*vm));
    if (!vm) { if (error && capacity) snprintf(error, capacity, "Could not allocate VM."); return NULL; }
    vm->memory = calloc(1, SATCHEL_NRO_MEMORY_SIZE);
    if (!vm->memory) { free(vm); if (error && capacity) snprintf(error, capacity, "Could not allocate 64 MiB guest memory."); return NULL; }
    vm->memory_size = SATCHEL_NRO_MEMORY_SIZE; vm->nro = true;
    memcpy(vm->memory + SATCHEL_NRO_BASE, bytes, info.image_size);
    map_pages(vm, SATCHEL_NRO_BASE, info.text_size, 5);
    map_pages(vm, SATCHEL_NRO_BASE + info.ro_offset, info.ro_size, 1);
    map_pages(vm, SATCHEL_NRO_BASE + info.data_offset, info.data_size + info.bss_size, 3);
    map_pages(vm, SATCHEL_NRO_HEAP, SATCHEL_NRO_HEAP_SIZE, 3);
    map_pages(vm, SATCHEL_NRO_CONFIG, 4096, 1);
    map_pages(vm, SATCHEL_NRO_TLS, 4096, 3);
    map_pages(vm, SATCHEL_NRO_STACK, SATCHEL_NRO_MEMORY_SIZE - SATCHEL_NRO_STACK, 3);
    for (unsigned p=SATCHEL_NRO_BASE/4096;p<(SATCHEL_NRO_BASE+info.image_size+info.bss_size+4095)/4096;p++)
        vm->memory_type[p] = p < (SATCHEL_NRO_BASE+info.data_offset)/4096 ? 8 : 9;
    for (unsigned p=SATCHEL_NRO_HEAP/4096;p<(SATCHEL_NRO_HEAP+SATCHEL_NRO_HEAP_SIZE)/4096;p++) vm->memory_type[p]=5;
    vm->memory_type[SATCHEL_NRO_TLS/4096]=12;
    for (unsigned p=SATCHEL_NRO_STACK/4096;p<SATCHEL_NRO_MEMORY_SIZE/4096;p++) vm->memory_type[p]=7;
    config_entry(vm, 0, 1, 0, 0x80, 0); /* MainThreadHandle */
    config_entry(vm, 1, 3, 1, SATCHEL_NRO_HEAP, SATCHEL_NRO_HEAP_SIZE); /* OverrideHeap */
    config_entry(vm, 2, 7, 0, 0, 0); /* Application */
    uint64_t svc_mask = 0;
    const unsigned services[] = {2,3,6,7,0xB,0x10,0x13,0x14,0x15,0x16,0x17,0x18,0x1E,0x1F,0x21,0x25,0x27,0x29};
    for (unsigned i=0;i<sizeof(services)/sizeof(services[0]);i++) svc_mask |= UINT64_C(1) << services[i];
    config_entry(vm, 3, 6, 0, svc_mask, 0);
    config_entry(vm, 4, 0, 0, 0, 0); /* EndOfList */
    vm->pc = SATCHEL_NRO_BASE; vm->code_size = info.text_size;
    vm->x[0] = SATCHEL_NRO_CONFIG; vm->x[1] = UINT64_MAX;
    vm->x[30] = SATCHEL_NRO_RETURN; vm->x[31] = SATCHEL_NRO_MEMORY_SIZE - 16;
    return vm;
}

static uint64_t system_tick(void) {
    struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * UINT64_C(19200000) + (uint64_t)now.tv_nsec * 192 / 10000;
}

#include "HorizonKernel.inc"
#include "HorizonIPC.inc"
#include "Arm64Extras.inc"

SatchelResult satchel_frame(SatchelVM *vm, int touch, int reset, uint64_t budget) {
    if (!vm || vm->fault) return SATCHEL_FAULT;
    if (vm->exited) return SATCHEL_EXITED;
    if (vm->sleep_until > system_tick()) return SATCHEL_WAIT;
    vm->sleep_until = 0;
    vm->horizon.frame_ready = false;
    if (vm->yielded) { vm->x[0] = (uint32_t)touch; vm->x[1] = (uint32_t)reset; vm->yielded = false; }
    for (uint64_t step = 0; step < budget; step++) {
        if (vm->nro && vm->pc == SATCHEL_NRO_RETURN) { vm->exited = true; vm->exit_code = vm->x[0]; return SATCHEL_EXITED; }
        bool executable = vm->nro ? (vm->pc < vm->memory_size && (vm->permissions[vm->pc / 4096] & 4)) :
            (vm->pc >= SATCHEL_CODE_BASE && vm->pc < SATCHEL_CODE_BASE + vm->code_size);
        if ((vm->pc & 3) || !executable) {
            fault(vm, "Instruction fetch outside code", vm->pc); return SATCHEL_FAULT;
        }
        uint32_t op;
        memcpy(&op, vm->memory + vm->pc, 4);
        uint64_t next = vm->pc + 4;
        unsigned rd = op & 31, rn = (op >> 5) & 31, rm = (op >> 16) & 31;
        unsigned bits = (op >> 31) ? 64 : 32;
        bool sub = (op >> 30) & 1, flags = (op >> 29) & 1;
        vm->instructions++;

        if ((op & 0xFFE0001F) == 0xD4000001) {
            unsigned svc = (op >> 5) & 0xFFFF;
            vm->exclusive_valid = false;
            if (!vm->nro) {
                if (svc != 0xF001) fault(vm, "Unsupported service", svc);
                else { vm->pc = next; vm->yielded = true; return SATCHEL_YIELD; }
            } else if (svc == 7) { vm->pc = next; vm->exited = true; vm->exit_code = 0; return SATCHEL_EXITED;
            } else if (svc == 0xB) { /* SleepThread: host schedules, no blocking inside VM */
                int64_t ns = (int64_t)vm->x[0];
                if (ns > 0) vm->sleep_until = system_tick() + (uint64_t)ns / 10000 * 192 + ((uint64_t)ns % 10000 * 192 + 9999) / 10000;
                vm->pc = next; return SATCHEL_WAIT;
            } else if (svc == 0x1E) { vm->x[0] = system_tick();
            } else if (svc == 0x27) {
                uint64_t address = vm->x[0], length = vm->x[1];
                if (length > vm->memory_size) fault(vm, "Debug string length exceeds guest memory", length);
                else if (access_ok(vm, address, (unsigned)length, false)) {
                    size_t available = sizeof(vm->output) - 1 - vm->output_size;
                    size_t copy = length < available ? (size_t)length : available;
                    memcpy(vm->output + vm->output_size, vm->memory + address, copy);
                    vm->output_size += copy; vm->output[vm->output_size] = 0; vm->x[0] = 0;
                }
            } else if (!horizon_kernel(vm, svc) && !horizon_ipc(vm, svc)) { fault(vm, "Horizon SVC not implemented", svc); }
        } else if ((op & 0xFFC00000) == 0xF9800000 || (op & 0xFFE00C00) == 0xF8800000 ||
                   (op & 0xFFE00C00) == 0xF8A00800 || (op & 0xFF000000) == 0xD8000000) { /* prefetch hints */
        } else if (op == 0xD503201F || (op & 0xFFFFFF3F) == 0xD503241F) { /* NOP / BTI hints */
        } else if ((op & 0x1F000000) == 0x10000000) { /* ADR / ADRP */
            int64_t imm = sext(((uint64_t)((op >> 5) & 0x7FFFF) << 2) | ((op >> 29) & 3), 21);
            put(vm, rd, op >> 31 ? (vm->pc & ~UINT64_C(4095)) + imm * 4096 : vm->pc + imm, 64, false);
        } else if ((op & 0xFFFFFFE0) == 0xD53BD060 && vm->nro) { /* TPIDRRO_EL0 */
            put(vm, rd, SATCHEL_NRO_TLS, 64, false);
        } else if ((op & 0xFFFFFFE0) == 0xD53BE040 && vm->nro) { /* CNTVCT_EL0 */
            put(vm, rd, system_tick(), 64, false);
        } else if ((op & 0xFFFFFFE0) == 0xD53B00E0 && vm->nro) { /* DCZID_EL0: prohibit DC ZVA */
            put(vm, rd, 16, 64, false);
        } else if ((op & 0x7C000000) == 0x14000000) { /* B / BL */
            if (op >> 31) vm->x[30] = next;
            next = vm->pc + sext(op & 0x3FFFFFF, 26) * 4;
        } else if ((op & 0xFF000010) == 0x54000000) {
            if (condition(vm, op & 15)) next = vm->pc + sext((op >> 5) & 0x7FFFF, 19) * 4;
        } else if ((op & 0x7E000000) == 0x34000000) { /* CBZ / CBNZ */
            bool zero = (zr(vm, rd) & mask_for(bits)) == 0;
            if (zero != ((op >> 24) & 1)) next = vm->pc + sext((op >> 5) & 0x7FFFF, 19) * 4;
        } else if ((op & 0x7E000000) == 0x36000000) { /* TBZ / TBNZ */
            unsigned bit = ((op >> 26) & 32) | ((op >> 19) & 31);
            if (((zr(vm, rd) >> bit) & 1) == ((op >> 24) & 1)) next = vm->pc + sext((op >> 5) & 0x3FFF, 14) * 4;
        } else if ((op & 0xFFFFFC1F) == 0xD65F0000 || (op & 0xFFFFFC1F) == 0xD61F0000 || (op & 0xFFFFFC1F) == 0xD63F0000) {
            next = zr(vm, rn);
            if ((op & 0xFFFFFC1F) == 0xD63F0000) vm->x[30] = vm->pc + 4;
        } else if ((op & 0x1F800000) == 0x12800000) { /* MOVN / MOVZ / MOVK */
            unsigned kind = (op >> 29) & 3, amount = ((op >> 21) & 3) * 16;
            uint64_t imm = (uint64_t)((op >> 5) & 0xFFFF) << amount;
            if (amount >= bits || kind == 1) fault(vm, "Invalid move-wide", op);
            else put(vm, rd, kind == 0 ? ~imm : kind == 2 ? imm : (zr(vm, rd) & ~(UINT64_C(0xFFFF) << amount)) | imm, bits, false);
        } else if ((op & 0x1F800000) == 0x11000000) { /* ADD / SUB immediate */
            uint64_t imm = (op >> 10) & 0xFFF;
            if ((op >> 22) & 1) imm <<= 12;
            put(vm, rd, arithmetic(vm, vm->x[rn], imm, bits, sub, flags), bits, !flags);
        } else if ((op & 0x1F200000) == 0x0B000000) { /* ADD / SUB shifted register */
            unsigned kind = (op >> 22) & 3, amount = (op >> 10) & 63;
            if (kind == 3 || amount >= bits) fault(vm, "Invalid arithmetic shift", op);
            else put(vm, rd, arithmetic(vm, zr(vm, rn), shift(zr(vm, rm), kind, amount, bits), bits, sub, flags), bits, false);
        } else if ((op & 0x1FE00000) == 0x0B200000) { /* ADD / SUB extended register */
            unsigned amount = (op >> 10) & 7;
            if (amount > 4) fault(vm, "Invalid extend shift", op);
            else put(vm, rd, arithmetic(vm, vm->x[rn], extend(zr(vm, rm), (op >> 13) & 7) << amount, bits, sub, flags), bits, !flags);
        } else if ((op & 0x1F000000) == 0x0A000000) { /* Logical shifted register */
            unsigned kind = (op >> 29) & 3, amount = (op >> 10) & 63;
            if (amount >= bits) fault(vm, "Invalid logical shift", op);
            else {
                uint64_t a = zr(vm, rn), b = shift(zr(vm, rm), (op >> 22) & 3, amount, bits);
                if ((op >> 21) & 1) b = ~b;
                uint64_t value = (kind == 0 || kind == 3 ? a & b : kind == 1 ? a | b : a ^ b) & mask_for(bits);
                put(vm, rd, value, bits, false);
                if (kind == 3) { vm->n = (value >> (bits - 1)) != 0; vm->z = value == 0; vm->c = vm->v = false; }
            }
        } else if ((op & 0x1FE00800) == 0x1A800000) { /* CSEL / CSINC / CSINV / CSNEG */
            uint64_t value;
            if (condition(vm, (op >> 12) & 15)) value = zr(vm, rn);
            else { value = zr(vm, rm); if (sub) value = ~value; if ((op >> 10) & 1) value++; }
            put(vm, rd, value, bits, false);
        } else if ((op & 0x3FE00410) == 0x3A400000) { /* CCMP / CCMN */
            if (condition(vm, (op >> 12) & 15)) {
                uint64_t b = (op & 0x800) ? rm : zr(vm, rm);
                (void)arithmetic(vm, zr(vm, rn), b, bits, sub, true);
            } else { vm->n = (op >> 3) & 1; vm->z = (op >> 2) & 1; vm->c = (op >> 1) & 1; vm->v = op & 1; }
        } else if ((op & 0x7FFFFC00) == 0x5AC00000) { /* RBIT */
            uint64_t source = zr(vm, rn), reversed = 0;
            for (unsigned bit = 0; bit < bits; bit++) { reversed = (reversed << 1) | (source & 1); source >>= 1; }
            put(vm, rd, reversed, bits, false);
        } else if ((op & 0x7FFFFC00) == 0x5AC01000) { /* CLZ */
            uint64_t value = zr(vm, rn) & mask_for(bits);
            unsigned count = value ? (unsigned)__builtin_clzll(value) - (64 - bits) : bits;
            put(vm, rd, count, bits, false);
        } else if ((op & 0x7FE0F000) == 0x1AC02000) { /* LSLV / LSRV / ASRV / RORV */
            put(vm, rd, shift(zr(vm, rn), (op >> 10) & 3, (unsigned)zr(vm, rm), bits), bits, false);
        } else if ((op & 0xFFE0FC00) == 0x9BC07C00) { /* UMULH */
            unsigned __int128 product = (unsigned __int128)zr(vm, rn) * zr(vm, rm);
            put(vm, rd, (uint64_t)(product >> 64), 64, false);
        } else if ((op & 0x7FE00000) == 0x1B000000) { /* MADD / MSUB */
            uint64_t product = zr(vm, rn) * zr(vm, rm), a = zr(vm, (op >> 10) & 31);
            put(vm, rd, (op & 0x8000) ? a - product : a + product, bits, false);
        } else if ((op & 0x1F800000) == 0x12000000) { /* Logical immediate (ARM DecodeBitMasks) */
            unsigned n = (op >> 22) & 1, r = (op >> 16) & 63, s = (op >> 10) & 63;
            unsigned combined = (n << 6) | ((~s) & 63), length = 0;
            for (unsigned i = 1; i <= 6; i++) if (combined & (1u << i)) length = i;
            unsigned element = 1u << length, levels = element - 1;
            if (!length || (! (op >> 31) && n) || (s & levels) == levels) fault(vm, "Invalid logical immediate", op);
            else {
                unsigned ones = (s & levels) + 1, rotation = r & levels;
                uint64_t chunk = (UINT64_C(1) << ones) - 1;
                uint64_t emask = element == 64 ? UINT64_MAX : (UINT64_C(1) << element) - 1;
                if (rotation) chunk = ((chunk >> rotation) | (chunk << (element - rotation))) & emask;
                uint64_t immediate = 0;
                for (unsigned i = 0; i < bits; i += element) immediate |= chunk << i;
                unsigned kind = (op >> 29) & 3;
                uint64_t a = zr(vm, rn), value = (kind == 0 || kind == 3 ? a & immediate : kind == 1 ? a | immediate : a ^ immediate) & mask_for(bits);
                put(vm, rd, value, bits, false);
                if (kind == 3) { vm->n = (value >> (bits - 1)) != 0; vm->z = value == 0; vm->c = vm->v = false; }
            }
        } else if ((op & 0x7F800000) == 0x53000000 || (op & 0x7F800000) == 0x13000000) { /* UBFM / SBFM */
            unsigned r = (op >> 16) & 63, s = (op >> 10) & 63;
            if (r >= bits || s >= bits || ((op >> 22) & 1) != (bits == 64)) fault(vm, "Invalid bitfield", op);
            else {
                uint64_t value = zr(vm, rn) & mask_for(bits);
                uint64_t m = s == 63 ? UINT64_MAX : (UINT64_C(1) << (s + 1)) - 1;
                bool signed_field = !(op & 0x40000000);
                bool negative = signed_field && ((value >> s) & 1);
                value = s >= r ? (value & m) >> r : (value & m) << (bits - r);
                unsigned top = s >= r ? s - r : bits - r + s;
                if (negative && top < 63) value |= UINT64_MAX << (top + 1);
                put(vm, rd, value, bits, false);
            }
        } else if ((op & 0x3B000000) == 0x18000000 && !(op & 0x04000000)) { /* Literal LDR / LDRSW */
            unsigned kind = op >> 30;
            uint64_t address = vm->pc + sext((op >> 5) & 0x7FFFF, 19) * 4;
            if (kind == 3) fault(vm, "Unsupported prefetch", op);
            else {
                uint64_t value = read_memory(vm, address, kind == 1 ? 8 : 4);
                put(vm, rd, kind == 2 ? (uint64_t)sext(value, 32) : value, kind ? 64 : 32, false);
            }
        } else if ((op & 0x3B200000) == 0x38000000 && !(op & 0x04000000)) { /* Unscaled / pre / post indexed */
            unsigned size = 1u << (op >> 30), kind = (op >> 22) & 3, mode = (op >> 10) & 3;
            int64_t offset = sext((op >> 12) & 0x1FF, 9);
            uint64_t base = vm->x[rn], address = mode == 1 ? base : base + offset;
            if (mode == 2 || (kind >= 2 && (size == 8 || (kind == 3 && size == 4)))) fault(vm, "Unsupported memory operation", op);
            else {
                if (kind == 0) write_memory(vm, address, size, zr(vm, rd));
                else {
                    uint64_t value = read_memory(vm, address, size);
                    put(vm, rd, kind >= 2 ? (uint64_t)sext(value, size * 8) : value, kind == 2 || size == 8 ? 64 : 32, false);
                }
                if (!vm->fault && (mode == 1 || mode == 3)) vm->x[rn] = base + offset;
            }
        } else if ((op & 0xBFE0FC00) == 0x0E000C00) { /* SIMD DUP from general register */
            unsigned imm5 = (op >> 16) & 31, element = 0;
            while (element < 5 && !(imm5 & (1u << element))) element++;
            unsigned active = (op & 0x40000000) ? 16 : 8;
            if (element > 3 || (element == 3 && active == 8)) fault(vm, "Invalid SIMD DUP", op);
            else {
                unsigned width = 1u << element; uint64_t value = zr(vm, rn);
                memset(vm->vectors[rd], 0, 16);
                for (unsigned i = 0; i < active; i += width) memcpy(vm->vectors[rd] + i, &value, width);
            }
        } else if ((op & 0x3B000000) == 0x39000000 && (op & 0x04000000)) { /* SIMD unsigned offset */
            unsigned scale = (op >> 30) + (((op >> 23) & 1) * 4);
            if (scale > 4) fault(vm, "Invalid SIMD transfer size", op);
            else {
                unsigned size = 1u << scale; bool load = (op >> 22) & 1;
                uint64_t address = vm->x[rn] + ((uint64_t)((op >> 10) & 0xFFF) << scale);
                if (access_ok(vm, address, size, !load)) {
                    if (load) { memset(vm->vectors[rd], 0, 16); memcpy(vm->vectors[rd], vm->memory + address, size); }
                    else memcpy(vm->memory + address, vm->vectors[rd], size);
                }
            }
        } else if ((op & 0x3A000000) == 0x28000000 && (op & 0x04000000)) { /* SIMD LDP / STP */
            unsigned kind = op >> 30, mode = (op >> 23) & 3, rt2 = (op >> 10) & 31;
            unsigned size = 4u << kind; bool load = (op >> 22) & 1;
            int64_t offset = sext((op >> 15) & 127, 7) * size;
            uint64_t base = vm->x[rn], address = mode == 1 ? base : base + offset;
            if (kind == 3) fault(vm, "Invalid SIMD pair size", op);
            else if (access_ok(vm, address, size * 2, !load)) {
                if (load) {
                    memset(vm->vectors[rd], 0, 16); memset(vm->vectors[rt2], 0, 16);
                    memcpy(vm->vectors[rd], vm->memory + address, size); memcpy(vm->vectors[rt2], vm->memory + address + size, size);
                } else {
                    memcpy(vm->memory + address, vm->vectors[rd], size); memcpy(vm->memory + address + size, vm->vectors[rt2], size);
                }
                if (mode == 1 || mode == 3) vm->x[rn] = base + offset;
            }
        } else if ((op & 0x3B000000) == 0x39000000 && !(op & 0x04000000)) { /* Unsigned offset load/store */
            unsigned size = 1u << (op >> 30), kind = (op >> 22) & 3;
            uint64_t address = vm->x[rn] + ((uint64_t)((op >> 10) & 0xFFF) * size);
            if (kind == 0) write_memory(vm, address, size, zr(vm, rd));
            else if (kind == 1) put(vm, rd, read_memory(vm, address, size), size == 8 ? 64 : 32, false);
            else if (size < 8 && !(kind == 3 && size == 4)) put(vm, rd, (uint64_t)sext(read_memory(vm, address, size), size * 8), kind == 2 ? 64 : 32, false);
            else fault(vm, "Invalid signed load", op);
        } else if ((op & 0x3B200C00) == 0x38200800 && !(op & 0x04000000)) { /* Register offset load/store */
            unsigned size = 1u << (op >> 30), kind = (op >> 22) & 3, option = (op >> 13) & 7;
            if (!(option & 2)) fault(vm, "Invalid memory extend", op);
            else {
                uint64_t offset = extend(zr(vm, rm), option);
                if ((op >> 12) & 1) offset <<= op >> 30;
                uint64_t address = vm->x[rn] + offset;
                if (kind == 0) write_memory(vm, address, size, zr(vm, rd));
                else if (kind == 1) put(vm, rd, read_memory(vm, address, size), size == 8 ? 64 : 32, false);
                else fault(vm, "Unsupported signed load", op);
            }
        } else if ((op & 0x3A000000) == 0x28000000 && !(op & 0x04000000)) { /* LDP / STP */
            unsigned kind = (op >> 30) & 3, mode = (op >> 23) & 3, rt2 = (op >> 10) & 31;
            unsigned size = kind == 2 ? 8 : 4;
            int64_t offset = sext((op >> 15) & 127, 7) * size;
            uint64_t base = vm->x[rn], address = mode == 1 ? base : base + offset;
            if (kind == 3 || (kind == 1 && !((op >> 22) & 1)) || mode == 0) fault(vm, "Unsupported pair operation", op);
            else if (access_ok(vm, address, size * 2, !((op >> 22) & 1))) {
                if ((op >> 22) & 1) {
                    uint64_t a = read_memory(vm, address, size), b = read_memory(vm, address + size, size);
                    put(vm, rd, kind == 1 ? (uint64_t)sext(a, 32) : a, kind == 1 ? 64 : size * 8, false);
                    put(vm, rt2, kind == 1 ? (uint64_t)sext(b, 32) : b, kind == 1 ? 64 : size * 8, false);
                } else { write_memory(vm, address, size, zr(vm, rd)); write_memory(vm, address + size, size, zr(vm, rt2)); }
                if (mode == 1 || mode == 3) vm->x[rn] = base + offset;
            }
        } else if (!execute_extra(vm, op)) { fault(vm, "Unsupported ARM64 instruction", op); }
        if (vm->fault) return SATCHEL_FAULT;
        vm->pc = next;
        if (vm->horizon.frame_ready) return SATCHEL_YIELD;
    }
    return SATCHEL_BUDGET;
}

const uint8_t *satchel_pixels(const SatchelVM *vm) { return vm->memory + SATCHEL_FRAMEBUFFER; }
int32_t satchel_state(const SatchelVM *vm, unsigned index) {
    int32_t value = 0;
    if (index < 7) memcpy(&value, vm->memory + SATCHEL_STATE + index * 4, 4);
    return value;
}
uint64_t satchel_instructions(const SatchelVM *vm) { return vm->instructions; }
uint64_t satchel_pc(const SatchelVM *vm) { return vm->pc; }
uint64_t satchel_register(const SatchelVM *vm, unsigned index) { return index < 32 ? vm->x[index] : 0; }
const char *satchel_error(const SatchelVM *vm) { return vm->error; }
const char *satchel_output(const SatchelVM *vm) { return vm->output; }
uint64_t satchel_exit_code(const SatchelVM *vm) { return vm->exit_code; }
uint64_t satchel_checksum(const SatchelVM *vm) {
    uint64_t hash = UINT64_C(14695981039346656037);
    for (unsigned i = 0; i < SATCHEL_WIDTH * SATCHEL_HEIGHT * 4; i++) { hash ^= satchel_pixels(vm)[i]; hash *= UINT64_C(1099511628211); }
    return hash;
}

void satchel_set_buttons(SatchelVM *vm, uint64_t buttons) {
    vm->horizon.pendingbuttons |= buttons & ~vm->horizon.inputbuttons;
    vm->horizon.inputbuttons = buttons;
}
void satchel_clear_buttons(SatchelVM *vm) { vm->horizon.inputbuttons = vm->horizon.pendingbuttons = 0; }
const uint8_t *satchel_display_pixels(const SatchelVM *vm) { return vm->horizon.pixels; }
unsigned satchel_display_width(const SatchelVM *vm) { return vm->horizon.width; }
unsigned satchel_display_height(const SatchelVM *vm) { return vm->horizon.height; }
uint64_t satchel_presented_frames(const SatchelVM *vm) { return vm->horizon.frame_count; }
uint64_t satchel_display_checksum(const SatchelVM *vm) {
    uint64_t hash = UINT64_C(14695981039346656037);
    if (!vm->horizon.pixels) return 0;
    size_t pixels = (size_t)vm->horizon.width * vm->horizon.height;
    for (size_t i=0;i<pixels;i++) for(unsigned c=0;c<3;c++) {
        hash ^= vm->horizon.pixels[i*4+c]; hash *= UINT64_C(1099511628211);
    }
    return hash;
}
