#include "Binary64.h"
#include <algorithm>

namespace satchel_cpu {
namespace {
using Wide = unsigned __int128;
constexpr uint64_t sign_bit=UINT64_C(1)<<63;
constexpr uint64_t fraction_mask=(UINT64_C(1)<<52)-1;
constexpr uint64_t infinity=UINT64_C(0x7ff0000000000000);
constexpr uint64_t quiet_bit=UINT64_C(1)<<51;
constexpr uint64_t default_nan=infinity|quiet_bit;
constexpr uint32_t invalid=1,overflow=4,underflow=8,inexact=16;
bool nan(uint64_t value) { return (value&infinity)==infinity && (value&fraction_mask); }
bool signaling(uint64_t value) { return nan(value) && !(value&quiet_bit); }
bool infinite(uint64_t value) { return (value&~sign_bit)==infinity; }
struct Finite { uint64_t significand; int exponent; bool negative; };
Finite decode(uint64_t bits) {
    const unsigned exponent=unsigned((bits>>52)&2047);
    return {(bits&fraction_mask)|(exponent?(UINT64_C(1)<<52):0),
            exponent?int(exponent)-1075:-1074,bool(bits&sign_bit)};
}
unsigned top_bit(Wide value) {
    const uint64_t high=uint64_t(value>>64);
    return high?127u-unsigned(__builtin_clzll(high)):63u-unsigned(__builtin_clzll(uint64_t(value)));
}
Binary64Result round(Wide significand,int exponent,bool negative,uint32_t fpsr) {
    const uint64_t sign=negative?sign_bit:0;
    if(!significand)return {sign,fpsr};
    const int top=int(top_bit(significand));
    int result_exponent=top+exponent;
    const bool tiny=result_exponent < -1022; // Base Arm tests tininess before rounding.
    const int cut=tiny?-1074-exponent:top-52;
    Wide kept;
    bool lost=false;
    if(cut<=0)kept=significand<<unsigned(-cut);
    else {
        kept=cut<128?significand>>unsigned(cut):0;
        lost=cut>=128 || (significand&((Wide{1}<<unsigned(cut))-1))!=0;
        const bool halfway_bit=cut<=128 && (significand&(Wide{1}<<unsigned(cut-1)));
        const bool below_halfway=cut>128 || (cut>1 && (significand&((Wide{1}<<unsigned(cut-1))-1)));
        if(halfway_bit && (below_halfway || (kept&1)))++kept;
    }
    if(lost)fpsr|=inexact;
    if(tiny) {
        if(lost)fpsr|=underflow;
        // A rounded subnormal may become the smallest normal; bit52 already
        // has exactly that exponent encoding. Underflow remains pre-rounding.
        return {sign|uint64_t(kept),fpsr};
    }
    if(kept==(Wide{1}<<53)){kept>>=1;++result_exponent;}
    if(result_exponent>1023)return {sign|infinity,fpsr|overflow|inexact};
    return {sign|(uint64_t(result_exponent+1023)<<52)|(uint64_t(kept)&fraction_mask),fpsr};
}
}
bool divide_binary64(uint64_t a,uint64_t b,uint32_t fpcr,uint32_t fpsr,Binary64Result&output) {
    if((fpcr&~UINT32_C(0x02000000)) || (fpsr&~UINT32_C(0x0800009f)))return false;
    Binary64Result result{};
    const uint64_t sign=(a^b)&sign_bit;
    if(nan(a)||nan(b)) {
        const uint64_t selected=signaling(a)?a:signaling(b)?b:nan(a)?a:b;
        result={(fpcr&0x02000000)?default_nan:(selected|quiet_bit),
                fpsr|((signaling(a)||signaling(b))?invalid:0)};
    } else {
        const auto x=decode(a),y=decode(b);
        const bool ai=infinite(a),bi=infinite(b);
        const bool az=!ai&&!x.significand,bz=!bi&&!y.significand;
        if((ai&&bi)||(az&&bz))result={default_nan,fpsr|invalid};
        else if(ai||bz)result={sign|infinity,fpsr|(ai?0u:2u)};
        else if(az||bi)result={sign,fpsr};
        else {
            // Normalize both nonzero finite significands to bit52. Original
            // exponents plus top-bit offsets remain in[-1074,1023].
            const unsigned xt=63u-unsigned(__builtin_clzll(x.significand));
            const unsigned yt=63u-unsigned(__builtin_clzll(y.significand));
            uint64_t numerator=x.significand<<(52-xt);
            const uint64_t denominator=y.significand<<(52-yt);
            int exponent=x.exponent+int(xt)-y.exponent-int(yt);
            if(numerator<denominator){numerator<<=1;--exponent;}
            // Ratio now lies in[1,2). Round directly to normal53-bit or the
            // subnormal2^-1074 lattice, never round through another FP type.
            const bool tiny=exponent < -1022;
            if(exponent>1023)result={sign|infinity,fpsr|overflow|inexact};
            else if(exponent < -1075)result={sign,fpsr|underflow|inexact};
            else {
                const int shift=tiny?exponent+1074:52; //[-1,52]
                const Wide n=shift>=0?Wide(numerator)<<unsigned(shift):Wide(numerator);
                const Wide d=shift>=0?Wide(denominator):Wide(denominator)<<1;
                // n fits106bits, d54bits; quotient<=53bits. Twice the
                // remainder fits55bits, including the half-min-subnormal case.
                Wide kept=n/d;const Wide remainder=n%d;
                if(remainder)fpsr|=inexact|(tiny?underflow:0);
                if(remainder*2>d || (remainder*2==d && (kept&1)))++kept;
                if(tiny)result={sign|uint64_t(kept),fpsr};
                else {
                    if(kept==(Wide{1}<<53)){kept>>=1;++exponent;}
                    result=exponent>1023?Binary64Result{sign|infinity,fpsr|overflow|inexact}:
                        Binary64Result{sign|(uint64_t(exponent+1023)<<52)|(uint64_t(kept)&fraction_mask),fpsr};
                }
            }
        }
    }
    output=result;return true;
}

bool uint_to_binary64(uint64_t value,uint32_t fpcr,uint32_t fpsr,Binary64Result&output) {
    if((fpcr&~UINT32_C(0x02000000)) || (fpsr&~UINT32_C(0x0800009f)))return false;
    // Integer input exponent is zero. round handles zero before clz; nonzero
    // top0..63 gives cut-52..11, and at most exponent64 after rounding carry.
    // All results are finite; only cumulative IXC can be added.
    output=round(Wide(value),0,false,fpsr);return true;
}

bool uint_to_binary32(uint64_t value,uint32_t fpcr,uint32_t fpsr,Binary32Result&output) {
    if((fpcr&~UINT32_C(0x02000000)) || (fpsr&~UINT32_C(0x0800009f)))return false;
    Binary32Result result{0,fpsr};
    if(value) { // Never evaluate clz(0).
        unsigned exponent=63u-unsigned(__builtin_clzll(value));
        uint64_t kept;
        if(exponent<=23)kept=value<<(23-exponent);
        else {
            const unsigned cut=exponent-23; // 1..40, so every shift is defined.
            kept=value>>cut;
            const uint64_t remainder=value&((UINT64_C(1)<<cut)-1);
            const uint64_t halfway=UINT64_C(1)<<(cut-1);
            if(remainder)result.fpsr|=inexact;
            if(remainder>halfway || (remainder==halfway && (kept&1)))++kept;
            if(kept==(UINT64_C(1)<<24)){kept>>=1;++exponent;}
        }
        // Nonzero unsigned64 integers round to normal finite binary32 values.
        // Even UINT64_MAX rounds only to2^64; no overflow/underflow/invalid/IDC.
        result.bits=((exponent+127u)<<23)|uint32_t(kept&UINT64_C(0x7fffff));
    }
    output=result;return true;
}

bool binary64_to_uint64(uint64_t input,uint32_t fpcr,uint32_t fpsr,UInt64Result&output) {
    if((fpcr&~UINT32_C(0x02000000)) || (fpsr&~UINT32_C(0x0800009f)))return false;
    const unsigned exponent=unsigned((input>>52)&2047);
    const uint64_t fraction=input&fraction_mask;
    const bool negative=(input&sign_bit)!=0;
    UInt64Result result{0,fpsr};
    if(exponent==2047) {
        // Either NaN produces zero; positive infinity saturates high.
        if(!fraction && !negative)result.bits=UINT64_MAX;
        result.fpsr|=invalid;
    } else if(!exponent && !fraction) {
        // Both signed zeros are exact zero.
    } else if(exponent<1023) {
        // Includes negative fractions: truncation yields valid zero, not
        // unsigned saturation. Gradual subnormals add IXC, never IDC here.
        result.fpsr|=inexact;
    } else if(negative || exponent>=1087) {
        if(!negative)result.bits=UINT64_MAX;
        // Saturation adds IOC instead of a new IXC, preserving prior flags.
        result.fpsr|=invalid;
    } else {
        const unsigned power=exponent-1023; //0..63
        const uint64_t significand=(UINT64_C(1)<<52)|fraction;
        if(power>=52)result.bits=significand<<(power-52); //0..11
        else {
            const unsigned cut=52-power; //1..52
            result.bits=significand>>cut;
            if(significand&((UINT64_C(1)<<cut)-1))result.fpsr|=inexact;
        }
    }
    output=result;return true;
}

bool binary32_to_binary64(uint32_t input,uint32_t fpcr,uint32_t fpsr,Binary64Result&output) {
    if((fpcr&~UINT32_C(0x02000000)) || (fpsr&~UINT32_C(0x0800009f)))return false;
    const uint64_t sign=uint64_t(input&UINT32_C(0x80000000))<<32;
    const unsigned exponent=(input>>23)&255;
    const uint32_t fraction=input&UINT32_C(0x007fffff);
    Binary64Result result{sign,fpsr};
    if(exponent==255) {
        if(!fraction)result.bits|=infinity;
        else {
            if(!(fraction&0x00400000))result.fpsr|=invalid;
            result.bits=(fpcr&0x02000000)?default_nan:sign|infinity|(uint64_t(fraction)<<29)|quiet_bit;
        }
    } else if(exponent) {
        result.bits|=(uint64_t(exponent+896)<<52)|(uint64_t(fraction)<<29);
    } else if(fraction) {
        // Every nonzero binary32 subnormal is exactly a normal binary64.
        // Highest source bit0..22 becomes bit52; no discarded bits or flags.
        const unsigned top=31u-unsigned(__builtin_clz(fraction));
        result.bits|=(uint64_t(top+874)<<52)|((uint64_t(fraction)<<(52-top))&fraction_mask);
    }
    output=result;return true;
}

bool multiply_binary32(uint32_t a,uint32_t b,uint32_t fpcr,uint32_t fpsr,Binary32Result&output) {
    if((fpcr&~UINT32_C(0x02000000)) || (fpsr&~UINT32_C(0x0800009f)))return false;
    constexpr uint32_t sign32=0x80000000,inf32=0x7f800000,frac32=0x007fffff,quiet32=0x00400000;
    const bool an=(a&inf32)==inf32&&(a&frac32),bn=(b&inf32)==inf32&&(b&frac32);
    Binary32Result result{};
    if(an||bn) {
        const bool as=an&&!(a&quiet32),bs=bn&&!(b&quiet32);
        const uint32_t chosen=as?a:bs?b:an?a:b;
        result={(fpcr&0x02000000)?inf32|quiet32:chosen|quiet32,fpsr|((as||bs)?invalid:0)};
    } else {
        const uint32_t sign=(a^b)&sign32,ax=a&~sign32,bx=b&~sign32;
        if(ax==inf32||bx==inf32) {
            result=(!ax||!bx)?Binary32Result{inf32|quiet32,fpsr|invalid}:Binary32Result{sign|inf32,fpsr};
        } else {
            const unsigned ae=(a>>23)&255,be=(b>>23)&255;
            const uint64_t product=uint64_t((a&frac32)|(ae?0x800000:0))*((b&frac32)|(be?0x800000:0));
            if(!product)result={sign,fpsr};
            else {
                // Exact24x24 product fits48 bits; shifts below are bounded even
                // for the product of two minimum subnormals (cut149).
                const int exponent=(ae?int(ae)-150:-149)+(be?int(be)-150:-149);
                const int top=63-__builtin_clzll(product);
                int power=top+exponent;
                const bool tiny=power < -126; // Arm tininess before rounding.
                const int cut=tiny?-149-exponent:top-23;
                uint64_t kept;bool lost=false;
                if(cut<=0)kept=product<<unsigned(-cut);
                else {
                    kept=cut<64?product>>unsigned(cut):0;
                    lost=cut>=64 || (product&((UINT64_C(1)<<unsigned(cut))-1));
                    const bool half=cut<=64 && (product&(UINT64_C(1)<<unsigned(cut-1)));
                    const bool below=cut>64 || (cut>1 && (product&((UINT64_C(1)<<unsigned(cut-1))-1)));
                    if(half&&(below||(kept&1)))++kept;
                }
                if(lost)fpsr|=inexact;
                if(tiny)result={sign|uint32_t(kept),fpsr|(lost?underflow:0)};
                else {
                    if(kept==(UINT64_C(1)<<24)){kept>>=1;++power;}
                    result=power>127?Binary32Result{sign|inf32,fpsr|overflow|inexact}:
                        Binary32Result{sign|(uint32_t(power+127)<<23)|(uint32_t(kept)&frac32),fpsr};
                }
            }
        }
    }
    output=result;return true;
}

bool binary64(bool multiply,uint64_t a,uint64_t b,uint32_t fpcr,uint32_t fpsr,Binary64Result&output) {
    // No traps, alternative FP behavior, unsupported rounding/FZ/AHP or
    // reserved status accepted. QC is preserved, never generated by FADD/MUL.
    if((fpcr&~UINT32_C(0x02000000)) || (fpsr&~UINT32_C(0x0800009f)))return false;
    Binary64Result result{};
    if(nan(a)||nan(b)) {
        if(signaling(a)||signaling(b))fpsr|=invalid;
        const uint64_t selected=signaling(a)?a:signaling(b)?b:nan(a)?a:b;
        result={(fpcr&0x02000000)?default_nan:(selected|quiet_bit),fpsr};
    } else if(multiply) {
        const auto x=decode(a),y=decode(b);
        const bool negative=bool((a^b)&sign_bit);
        if(infinite(a)||infinite(b)) {
            const bool invalid_product=(!infinite(a)&&!x.significand)||(!infinite(b)&&!y.significand);
            result=invalid_product?Binary64Result{default_nan,fpsr|invalid}:
                Binary64Result{(negative?sign_bit:0)|infinity,fpsr};
        } else result=round(Wide{x.significand}*y.significand,x.exponent+y.exponent,negative,fpsr);
    } else if(infinite(a)||infinite(b)) {
        result=(infinite(a)&&infinite(b)&&((a^b)&sign_bit))?
            Binary64Result{default_nan,fpsr|invalid}:Binary64Result{infinite(a)?a:b,fpsr};
    } else {
        auto x=decode(a),y=decode(b);
        if(!x.significand&&!y.significand)result={a&b&sign_bit,fpsr};
        else if(!x.significand)result={b,fpsr};
        else if(!y.significand)result={a,fpsr};
        else {
            if(x.exponent<y.exponent || (x.exponent==y.exponent && x.significand<y.significand)) {
                std::swap(x,y);std::swap(a,b);
            }
            const unsigned gap=unsigned(x.exponent-y.exponent);
            // Beyond74 bits, the nonzero smaller operand is strictly less
            // than half even the lower neighbor spacing of the larger value.
            // It cannot cause underflow/overflow or change nearest-even bits.
            if(gap>74)result={a,fpsr|inexact};
            else {
                const Wide large=Wide{x.significand}<<gap,small=y.significand;
                const Wide magnitude=x.negative==y.negative?large+small:large-small;
                result=round(magnitude,y.exponent,magnitude?x.negative:false,fpsr);
            }
        }
    }
    output=result;return true;
}
}
