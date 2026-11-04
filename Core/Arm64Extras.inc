/* ARMv8-A exclusives and ordering for this single-threaded guest interpreter.
 * Memory is private to a VM and all guest operations execute in program order.
 * access_ok(..., true) invalidates the local exclusive monitor on guest writes;
 * SVC/context-switch paths must clear it as well. No host atomic is necessary.
 */
static bool execute_extra(SatchelVM *vm, uint32_t op) {
    unsigned rt = op & 31, rn = (op >> 5) & 31;
    unsigned rm = (op >> 16) & 31, bits = (op >> 31) ? 64 : 32;
    if ((op & 0xFFFFFFE0) == 0xD53B0020) { /* CTR_EL0 */
        // 64-byte I/D lines, 64-byte reservation/writeback granules, PIPT.
        // IDC/DIC advertise coherent instruction/data views; bit31 is RES1.
        put(vm, rt, UINT64_C(0xB444C004), 64, false);
        return true;
    }
    if ((op & 0xFFFFFFE0) == 0xD50B7E20 || (op & 0xFFFFFFE0) == 0xD50B7A20) { /* DC CIVAC/CVAC */
        // Guest memory is already coherent with the scanout backend. Translation
        // and access still matter; no native host cache instruction is executed.
        (void)access_ok(vm, zr(vm, rt), 1, false);
        vm->exclusive_valid = false;
        return true;
    }
    if ((op & 0xFFFFFFE0) == 0xD50B7420) { /* DC ZVA */
        fault(vm, "DC ZVA prohibited by guest DCZID_EL0", zr(vm, rt));
        return true;
    }
    if ((op & 0xFFC00000) == 0xF9800000 || /* PRFM unsigned immediate */
        (op & 0xFFE00C00) == 0xF8800000 || /* PRFUM */
        (op & 0xFFE00C00) == 0xF8A00800 || /* PRFM register */
        (op & 0xFF000000) == 0xD8000000) { /* PRFM literal */
        return true; /* Architectural hints do not fault or change guest state. */
    }
    if ((op & 0x9FF80C00) == 0x0F000400 && ((op >> 12) & 15) != 15) { /* SIMD modified immediate */
        unsigned mode = (op >> 12) & 15, active = (op & 0x40000000) ? 16 : 8;
        bool invert = (op >> 29) & 1;
        uint64_t imm = (((op >> 16) & 7) << 5) | ((op >> 5) & 31), value;
        unsigned width;
        if (mode < 8) { width = 4; value = imm << (8 * (mode >> 1)); }
        else if (mode < 12) { width = 2; value = imm << (8 * ((mode >> 1) & 1)); }
        else if (mode == 12) { width = 4; value = (imm << 8) | 255; }
        else if (mode == 13) { width = 4; value = (imm << 16) | 65535; }
        else if (!invert) { width = 1; value = imm; }
        else {
            width = 8; value = 0;
            for (unsigned byte = 0; byte < 8; byte++) if (imm & (1u << byte)) value |= UINT64_C(255) << (byte * 8);
        }
        if (invert && mode != 14) value = ~value;
        uint8_t bytes[8]; memcpy(bytes, &value, 8);
        bool merge = mode < 12 && (mode & 1);
        for (unsigned byte = 0; byte < active; byte++) {
            if (!merge) vm->vectors[rt][byte] = bytes[byte % width];
            else if (invert) vm->vectors[rt][byte] &= bytes[byte % width];
            else vm->vectors[rt][byte] |= bytes[byte % width];
        }
        memset(vm->vectors[rt] + active, 0, 16 - active);
        return true;
    }
    if ((op & 0x9F20FC00) == 0x0E201C00) { /* SIMD AND/BIC/ORR/ORN/EOR/BSL/BIT/BIF */
        unsigned kind = ((op >> 29) & 1) * 4 + ((op >> 22) & 3);
        unsigned active = (op & 0x40000000) ? 16 : 8;
        for (unsigned byte = 0; byte < active; byte++) {
            uint8_t a = vm->vectors[rn][byte], b = vm->vectors[rm][byte], d = vm->vectors[rt][byte], value;
            switch (kind) {
                case 0: value = a & b; break;
                case 1: value = a & ~b; break;
                case 2: value = a | b; break;
                case 3: value = a | ~b; break;
                case 4: value = a ^ b; break;
                case 5: value = (d & a) | (~d & b); break;
                case 6: value = (d & ~b) | (a & b); break;
                default: value = (d & b) | (a & ~b); break;
            }
            vm->vectors[rt][byte] = value;
        }
        memset(vm->vectors[rt] + active, 0, 16 - active);
        return true;
    }
    if ((op & 0x9F20FC00) == 0x0E208400) { /* SIMD ADD / SUB, integer lanes */
        unsigned width = 1u << ((op >> 22) & 3), active = (op & 0x40000000) ? 16 : 8;
        bool subtract = (op >> 29) & 1;
        if (width == 8 && active == 8) { fault(vm, "Invalid SIMD arithmetic arrangement", op); return true; }
        uint8_t result[16] = {0};
        for (unsigned lane = 0; lane < active / width; lane++) {
            uint64_t a = 0, b = 0;
            memcpy(&a, vm->vectors[rn] + lane * width, width);
            memcpy(&b, vm->vectors[rm] + lane * width, width);
            uint64_t value = subtract ? a - b : a + b;
            memcpy(result + lane * width, &value, width);
        }
        memcpy(vm->vectors[rt], result, 16);
        return true;
    }
    bool compare_vector = (op & 0xBF20FC00) == 0x2E208C00;
    bool compare_zero = (op & 0xBF3FFC00) == 0x0E209800;
    bool add_pairs = (op & 0xBF20FC00) == 0x0E20BC00;
    if (compare_vector || compare_zero || add_pairs) {
        unsigned width = 1u << ((op >> 22) & 3), active = (op & 0x40000000) ? 16 : 8;
        if (width == 8 && active == 8) { fault(vm, "Invalid SIMD lane arrangement", op); return true; }
        uint8_t result[16] = {0};
        for (unsigned lane = 0; lane < active / width; lane++) {
            uint64_t a = 0, b = 0, value;
            if (add_pairs) {
                unsigned half = active / (2 * width), source = lane < half ? rn : rm;
                unsigned offset = (lane % half) * 2 * width;
                memcpy(&a, vm->vectors[source] + offset, width);
                memcpy(&b, vm->vectors[source] + offset + width, width);
                value = a + b;
            } else {
                memcpy(&a, vm->vectors[rn] + lane * width, width);
                if (compare_vector) memcpy(&b, vm->vectors[rm] + lane * width, width);
                value = a == b ? UINT64_MAX : 0;
            }
            memcpy(result + lane * width, &value, width);
        }
        memcpy(vm->vectors[rt], result, 16);
        return true;
    }
    if ((op & 0xBFE0FC00) == 0x0E003C00) { /* UMOV (MOV vector lane to general register) */
        unsigned imm = (op >> 16) & 31, element = 0;
        while (element < 5 && !(imm & (1u << element))) element++;
        if (element > 3 || (element == 3) != ((op & 0x40000000) != 0)) {
            fault(vm, "Invalid UMOV arrangement", op); return true;
        }
        unsigned width = 1u << element, lane = imm >> (element + 1);
        uint64_t value = 0; memcpy(&value, vm->vectors[rn] + lane * width, width);
        put(vm, rt, value, element == 3 ? 64 : 32, false);
        return true;
    }
    if ((op & 0xBF200000) == 0x0C000000) { /* LD1/ST1 multiple whole registers */
        unsigned code = (op >> 12) & 15;
        unsigned count = code == 7 ? 1 : code == 10 ? 2 : code == 6 ? 3 : code == 2 ? 4 : 0;
        if (!count) return false; /* Interleaved LD2/3/4 are different operations. */
        bool load = (op >> 22) & 1, post = (op >> 23) & 1;
        unsigned active = (op & 0x40000000) ? 16 : 8, total = active * count;
        if (!post && rm) { fault(vm, "Invalid SIMD structure address", op); return true; }
        uint64_t address = vm->x[rn], advance = rm == 31 ? total : zr(vm, rm);
        if (access_ok(vm, address, total, !load)) {
            for (unsigned i = 0; i < count; i++) {
                unsigned reg = (rt + i) & 31;
                if (load) { memset(vm->vectors[reg], 0, 16); memcpy(vm->vectors[reg], vm->memory + address + i * active, active); }
                else memcpy(vm->memory + address + i * active, vm->vectors[reg], active);
            }
            if (post) vm->x[rn] = address + advance;
        }
        return true;
    }
    if ((op & 0x7F800000) == 0x33000000) { /* BFM, including BFI/BFXIL */
        unsigned r = (op >> 16) & 63, top = (op >> 10) & 63;
        if (r >= bits || top >= bits || ((op >> 22) & 1) != (bits == 64)) {
            fault(vm, "Invalid BFM bitfield", op); return true;
        }
        unsigned width = top >= r ? top - r + 1 : top + 1;
        unsigned dest = top >= r ? 0 : bits - r;
        uint64_t field = width == 64 ? UINT64_MAX : (UINT64_C(1) << width) - 1;
        uint64_t mask = field << dest;
        uint64_t value = ((zr(vm, rn) >> (top >= r ? r : 0)) & field) << dest;
        put(vm, rt, (zr(vm, rt) & ~mask) | value, bits, false);
        return true;
    }
    if ((op & 0x7FA00000) == 0x13800000) { /* EXTR */
        unsigned amount = (op >> 10) & 63;
        if (amount >= bits || ((op >> 22) & 1) != (bits == 64)) fault(vm, "Invalid EXTR", op);
        else {
            uint64_t low = zr(vm, rm) & mask_for(bits), high = zr(vm, rn) & mask_for(bits);
            put(vm, rt, amount ? (low >> amount) | (high << (bits - amount)) : low, bits, false);
        }
        return true;
    }
    if ((op & 0x7FE0FC00) == 0x1AC00800 || (op & 0x7FE0FC00) == 0x1AC00C00) { /* UDIV/SDIV */
        uint64_t a = zr(vm, rn) & mask_for(bits), b = zr(vm, rm) & mask_for(bits), result = 0;
        if (b) {
            if (op & 0x400) {
                int64_t sa = sext(a, bits), sb = sext(b, bits);
                // AArch64 wraps MIN/-1; avoid C's undefined signed division overflow.
                result = sa == INT64_MIN && sb == -1 ? a : (uint64_t)(sa / sb);
            } else result = a / b;
        }
        put(vm, rt, result, bits, false);
        return true;
    }
    if ((op & 0xFFE00000) == 0x9B200000 || (op & 0xFFE00000) == 0x9BA00000) { /* SMADDL/SMSUBL/UMADDL/UMSUBL */
        uint64_t a = zr(vm, (op >> 10) & 31), product;
        if (op & 0x800000) product = (uint64_t)(uint32_t)zr(vm, rn) * (uint32_t)zr(vm, rm);
        else product = (uint64_t)((int64_t)(int32_t)zr(vm, rn) * (int64_t)(int32_t)zr(vm, rm));
        put(vm, rt, (op & 0x8000) ? a - product : a + product, 64, false);
        return true;
    }
    if ((op & 0xFFE0FC00) == 0x9B407C00) { /* SMULH */
        __int128 product = (__int128)(int64_t)zr(vm, rn) * (int64_t)zr(vm, rm);
        put(vm, rt, (uint64_t)((unsigned __int128)product >> 64), 64, false);
        return true;
    }
    if ((op & 0x7FFFFC00) == 0x5AC00400 || (op & 0x7FFFFC00) == 0x5AC00800 ||
        (op & 0xFFFFFC00) == 0xDAC00C00) { /* REV16 / REV32 / REV64 */
        unsigned block = 1u << ((op >> 10) & 3);
        uint64_t value = zr(vm, rn), result = 0;
        for (unsigned byte = 0; byte < bits / 8; byte++) {
            unsigned source = (byte & ~(block - 1)) | (block - 1 - (byte & (block - 1)));
            result |= ((value >> (source * 8)) & 255) << (byte * 8);
        }
        put(vm, rt, result, bits, false);
        return true;
    }
    if ((op & 0xFFFFF0FF) == 0xD503305F) { /* CLREX #imm4 */
        vm->exclusive_valid = false;
        return true;
    }
    if ((op & 0xFFFFF0FF) == 0xD503309F || /* DSB */
        (op & 0xFFFFF0FF) == 0xD50330BF || /* DMB */
        op == 0xD5033FDF) { /* ISB SY: no decoded instruction cache */
        return true;
    }
    if ((op & 0xFFFFFFE0) == 0xD53BD040) { /* MRS TPIDR_EL0 */
        put(vm, rt, vm->tpidr_el0, 64, false);
        return true;
    }
    if ((op & 0xFFFFFFE0) == 0xD51BD040) { /* MSR TPIDR_EL0 */
        vm->tpidr_el0 = zr(vm, rt);
        return true;
    }
    if ((op & 0x3FFFFC00) == 0x08DFFC00 || /* LDAR/B/H */
        (op & 0x3FFFFC00) == 0x089FFC00) { /* STLR/B/H */
        unsigned size = 1u << (op >> 30);
        uint64_t address = vm->x[rn];
        if (address & (size - 1)) { fault(vm, "Unaligned acquire/release access", address); return true; }
        if ((op >> 22) & 1) put(vm, rt, read_memory(vm, address, size), size == 8 ? 64 : 32, false);
        else write_memory(vm, address, size, zr(vm, rt));
        return true;
    }
    if ((op & 0x3F800000) == 0x08000000) { /* LDXR/STXR/LDXP/STXP, acquire/release variants */
        bool load = (op >> 22) & 1, pair = (op >> 21) & 1;
        unsigned rs = (op >> 16) & 31, rt2 = (op >> 10) & 31;
        unsigned size = 1u << (op >> 30), total = size * (pair ? 2 : 1);
        if ((!pair && rt2 != 31) || (pair && size < 4) || (load && rs != 31) ||
            (load && pair && rt == rt2) ||
            (!load && rs != 31 && (rs == rt || (pair && rs == rt2) || (rs == rn && rn != 31)))) {
            fault(vm, "Invalid exclusive instruction operands", op); return true;
        }
        uint64_t address = vm->x[rn];
        if (address & (total - 1)) { fault(vm, "Unaligned exclusive access", address); return true; }
        if (load) {
            vm->exclusive_valid = false;
            if (!access_ok(vm, address, total, false)) return true;
            uint64_t first = read_memory(vm, address, size);
            uint64_t second = pair ? read_memory(vm, address + size, size) : 0;
            put(vm, rt, first, size == 8 ? 64 : 32, false);
            if (pair) put(vm, rt2, second, size == 8 ? 64 : 32, false);
            vm->exclusive_address = address; vm->exclusive_size = total; vm->exclusive_valid = true;
        } else {
            bool success = vm->exclusive_valid && vm->exclusive_address == address && vm->exclusive_size == total;
            vm->exclusive_valid = false;
            if (!access_ok(vm, address, total, true)) return true;
            if (success) {
                write_memory(vm, address, size, zr(vm, rt));
                if (pair) write_memory(vm, address + size, size, zr(vm, rt2));
            }
            put(vm, rs, success ? 0 : 1, 32, false);
        }
        return true;
    }
    return false;
}
