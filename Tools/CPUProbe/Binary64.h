#ifndef SATCHEL_CPU_BINARY64_H
#define SATCHEL_CPU_BINARY64_H
#include <cstdint>
namespace satchel_cpu {
struct Binary64Result { uint64_t bits; uint32_t fpsr; };
struct Binary32Result { uint32_t bits; uint32_t fpsr; };
struct UInt64Result { uint64_t bits; uint32_t fpsr; };
// Private FCVTZU binary64->unsigned64, truncation before saturation.
// FPCR0/DN only; cumulative IOC/IXC, unchanged output on invalid controls/status.
bool binary64_to_uint64(uint64_t input, uint32_t fpcr, uint32_t fpsr,
                        UInt64Result &output);
// Private scalar FDIV D, nearest-even gradual underflow, FPCR0/DN only.
// Exact integer quotient/remainder; invalid control/status preserves output.
bool divide_binary64(uint64_t a, uint64_t b, uint32_t fpcr, uint32_t fpsr,
                     Binary64Result &output);
// Private UCVTF W/X->D, nearest-even FPCR0/DN and modeled cumulative FPSR.
// W is already zero-extended; invalid controls/status preserve output.
bool uint_to_binary64(uint64_t value, uint32_t fpcr, uint32_t fpsr,
                      Binary64Result &output);
// Private UCVTF integer model, directly rounded to binary32 (no double step).
// Only nearest-even FPCR0/DN and modeled cumulative FPSR are admitted.
// Input is already zero-extended for W; invalid controls/status preserve output.
bool uint_to_binary32(uint64_t value, uint32_t fpcr, uint32_t fpsr,
                      Binary32Result &output);
// Private exact binary32->binary64 widening; only signaling NaNs add IOC.
// Same FPCR0/DN/FPSR admission and unchanged-output failure contract.
bool binary32_to_binary64(uint32_t input, uint32_t fpcr, uint32_t fpsr,
                         Binary64Result &output);
// Private FMUL S model, exact integer product and nearest-even binary32 round.
// Same control/status contract as binary64; no host FP transport/arithmetic.
bool multiply_binary32(uint32_t a, uint32_t b, uint32_t fpcr, uint32_t fpsr,
                       Binary32Result &output);
// Private exact FADD/FMUL D model. Only nearest-even, gradual underflow,
// ordinary Arm NaN propagation or DN are admitted. No host FP operations/state.
// Invalid control/status leaves output unchanged; cumulative flags preserved.
bool binary64(bool multiply, uint64_t a, uint64_t b, uint32_t fpcr,
              uint32_t fpsr, Binary64Result &output);
}
#endif
