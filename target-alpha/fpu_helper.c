/*
 *  Helpers for floating point instructions.
 *
 *  Copyright (c) 2007 Jocelyn Mayer
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "cpu.h"
#include "exec/helper-proto.h"
#include "fpu/softfloat.h"

#define FP_STATUS (env->fp_status)


void helper_setroundmode(CPUAlphaState *env, uint32_t val)
{
    set_float_rounding_mode(val, &FP_STATUS);
}

void helper_setflushzero(CPUAlphaState *env, uint32_t val)
{
    set_flush_to_zero(val, &FP_STATUS);
}

void helper_fp_exc_clear(CPUAlphaState *env)
{
    set_float_exception_flags(0, &FP_STATUS);
}

uint32_t helper_fp_exc_get(CPUAlphaState *env)
{
    return get_float_exception_flags(&FP_STATUS);
}

static inline void inline_fp_exc_raise(CPUAlphaState *env, uintptr_t retaddr,
                                       uint32_t exc, uint32_t regno)
{
    if (exc) {
        uint32_t hw_exc = 0;

        if (exc & float_flag_invalid) {
            hw_exc |= EXC_M_INV;
        }
        if (exc & float_flag_divbyzero) {
            hw_exc |= EXC_M_DZE;
        }
        if (exc & float_flag_overflow) {
            hw_exc |= EXC_M_FOV;
        }
        if (exc & float_flag_underflow) {
            hw_exc |= EXC_M_UNF;
        }
        if (exc & float_flag_inexact) {
            hw_exc |= EXC_M_INE;
        }

        arith_excp(env, retaddr, hw_exc, 1ull << regno);
    }
}

/* Raise exceptions for ieee fp insns without software completion.
   In that case there are no exceptions that don't trap; the mask
   doesn't apply.  */
void helper_fp_exc_raise(CPUAlphaState *env, uint32_t exc, uint32_t regno)
{
    inline_fp_exc_raise(env, GETPC(), exc, regno);
}

/* Raise exceptions for ieee fp insns with software completion.  */
void helper_fp_exc_raise_s(CPUAlphaState *env, uint32_t exc, uint32_t regno)
{
    if (exc) {
        env->fpcr_exc_status |= exc;
        exc &= ~env->fpcr_exc_mask;
        inline_fp_exc_raise(env, GETPC(), exc, regno);
    }
}

/* Input handing without software completion.  Trap for all
   non-finite numbers.  */
void helper_ieee_input(CPUAlphaState *env, uint64_t val)
{
    uint32_t exp = (uint32_t)(val >> 52) & 0x7ff;
    uint64_t frac = val & 0xfffffffffffffull;

    if (exp == 0) {
        /* Denormals without DNZ set raise an exception.  */
        if (frac != 0 && !env->fp_status.flush_inputs_to_zero) {
            arith_excp(env, GETPC(), EXC_M_UNF, 0);
        }
    } else if (exp == 0x7ff) {
        /* Infinity or NaN.  */
        /* ??? I'm not sure these exception bit flags are correct.  I do
           know that the Linux kernel, at least, doesn't rely on them and
           just emulates the insn to figure out what exception to use.  */
        arith_excp(env, GETPC(), frac ? EXC_M_INV : EXC_M_FOV, 0);
    }
}

/* Similar, but does not trap for infinities.  Used for comparisons.  */
void helper_ieee_input_cmp(CPUAlphaState *env, uint64_t val)
{
    uint32_t exp = (uint32_t)(val >> 52) & 0x7ff;
    uint64_t frac = val & 0xfffffffffffffull;

    if (exp == 0) {
        /* Denormals without DNZ set raise an exception.  */
        if (frac != 0 && !env->fp_status.flush_inputs_to_zero) {
            arith_excp(env, GETPC(), EXC_M_UNF, 0);
        }
    } else if (exp == 0x7ff && frac) {
        /* NaN.  */
        arith_excp(env, GETPC(), EXC_M_INV, 0);
    }
}


/* S floating (single) */

/* Taken from linux/arch/alpha/kernel/traps.c, s_mem_to_reg.  */
static inline uint64_t float32_to_s_int(uint32_t fi)
{
    uint32_t frac = fi & 0x7fffff;
    uint32_t sign = fi >> 31;
    uint32_t exp_msb = (fi >> 30) & 1;
    uint32_t exp_low = (fi >> 23) & 0x7f;
    uint32_t exp;

    exp = (exp_msb << 10) | exp_low;
    if (exp_msb) {
        if (exp_low == 0x7f) {
            exp = 0x7ff;
        }
    } else {
        if (exp_low != 0x00) {
            exp |= 0x380;
        }
    }

    return (((uint64_t)sign << 63)
            | ((uint64_t)exp << 52)
            | ((uint64_t)frac << 29));
}

static inline uint64_t float32_to_s(float32 fa)
{
    CPU_FloatU a;
    a.f = fa;
    return float32_to_s_int(a.l);
}

static inline uint32_t s_to_float32_int(uint64_t a)
{
    return ((a >> 32) & 0xc0000000) | ((a >> 29) & 0x3fffffff);
}

static inline float32 s_to_float32(uint64_t a)
{
    CPU_FloatU r;
    r.l = s_to_float32_int(a);
    return r.f;
}

uint32_t helper_s_to_memory(uint64_t a)
{
    return s_to_float32_int(a);
}

uint64_t helper_memory_to_s(uint32_t a)
{
    return float32_to_s_int(a);
}

uint64_t helper_adds(CPUAlphaState *env, uint64_t a, uint64_t b)
{
    float32 fa, fb, fr;

    fa = s_to_float32(a);
    fb = s_to_float32(b);
    fr = float32_add(fa, fb, &FP_STATUS);
    return float32_to_s(fr);
}

uint64_t helper_subs(CPUAlphaState *env, uint64_t a, uint64_t b)
{
    float32 fa, fb, fr;

    fa = s_to_float32(a);
    fb = s_to_float32(b);
    fr = float32_sub(fa, fb, &FP_STATUS);
    return float32_to_s(fr);
}

uint64_t helper_muls(CPUAlphaState *env, uint64_t a, uint64_t b)
{
    float32 fa, fb, fr;

    fa = s_to_float32(a);
    fb = s_to_float32(b);
    fr = float32_mul(fa, fb, &FP_STATUS);
    return float32_to_s(fr);
}

uint64_t helper_divs(CPUAlphaState *env, uint64_t a, uint64_t b)
{
    float32 fa, fb, fr;

    fa = s_to_float32(a);
    fb = s_to_float32(b);
    fr = float32_div(fa, fb, &FP_STATUS);
    return float32_to_s(fr);
}

uint64_t helper_sqrts(CPUAlphaState *env, uint64_t a)
{
    float32 fa, fr;

    fa = s_to_float32(a);
    fr = float32_sqrt(fa, &FP_STATUS);
    return float32_to_s(fr);
}


/* T floating (double) */
static inline float64 t_to_float64(uint64_t a)
{
    /* Memory format is the same as float64 */
    CPU_DoubleU r;
    r.ll = a;
    return r.d;
}

static inline uint64_t float64_to_t(float64 fa)
{
    /* Memory format is the same as float64 */
    CPU_DoubleU r;
    r.d = fa;
    return r.ll;
}

uint64_t helper_addt(CPUAlphaState *env, uint64_t a, uint64_t b)
{
    float64 fa, fb, fr;

    fa = t_to_float64(a);
    fb = t_to_float64(b);
    fr = float64_add(fa, fb, &FP_STATUS);
    return float64_to_t(fr);
}

uint64_t helper_subt(CPUAlphaState *env, uint64_t a, uint64_t b)
{
    float64 fa, fb, fr;

    fa = t_to_float64(a);
    fb = t_to_float64(b);
    fr = float64_sub(fa, fb, &FP_STATUS);
    return float64_to_t(fr);
}

uint64_t helper_mult(CPUAlphaState *env, uint64_t a, uint64_t b)
{
    float64 fa, fb, fr;

    fa = t_to_float64(a);
    fb = t_to_float64(b);
    fr = float64_mul(fa, fb, &FP_STATUS);
    return float64_to_t(fr);
}

uint64_t helper_divt(CPUAlphaState *env, uint64_t a, uint64_t b)
{
    float64 fa, fb, fr;

    fa = t_to_float64(a);
    fb = t_to_float64(b);
    fr = float64_div(fa, fb, &FP_STATUS);
    return float64_to_t(fr);
}

uint64_t helper_sqrtt(CPUAlphaState *env, uint64_t a)
{
    float64 fa, fr;

    fa = t_to_float64(a);
    fr = float64_sqrt(fa, &FP_STATUS);
    return float64_to_t(fr);
}

/* Comparisons */
uint64_t helper_cmptun(CPUAlphaState *env, uint64_t a, uint64_t b)
{
    float64 fa, fb;

    fa = t_to_float64(a);
    fb = t_to_float64(b);

    if (float64_unordered_quiet(fa, fb, &FP_STATUS)) {
        return 0x4000000000000000ULL;
    } else {
        return 0;
    }
}

uint64_t helper_cmpteq(CPUAlphaState *env, uint64_t a, uint64_t b)
{
    float64 fa, fb;

    fa = t_to_float64(a);
    fb = t_to_float64(b);

    if (float64_eq_quiet(fa, fb, &FP_STATUS)) {
        return 0x4000000000000000ULL;
    } else {
        return 0;
    }
}

uint64_t helper_cmptle(CPUAlphaState *env, uint64_t a, uint64_t b)
{
    float64 fa, fb;

    fa = t_to_float64(a);
    fb = t_to_float64(b);

    if (float64_le(fa, fb, &FP_STATUS)) {
        return 0x4000000000000000ULL;
    } else {
        return 0;
    }
}

uint64_t helper_cmptlt(CPUAlphaState *env, uint64_t a, uint64_t b)
{
    float64 fa, fb;

    fa = t_to_float64(a);
    fb = t_to_float64(b);

    if (float64_lt(fa, fb, &FP_STATUS)) {
        return 0x4000000000000000ULL;
    } else {
        return 0;
    }
}

/* Floating point format conversion */
uint64_t helper_cvtts(CPUAlphaState *env, uint64_t a)
{
    float64 fa;
    float32 fr;

    fa = t_to_float64(a);
    fr = float64_to_float32(fa, &FP_STATUS);
    return float32_to_s(fr);
}

uint64_t helper_cvtst(CPUAlphaState *env, uint64_t a)
{
    float32 fa;
    float64 fr;

    fa = s_to_float32(a);
    fr = float32_to_float64(fa, &FP_STATUS);
    return float64_to_t(fr);
}

uint64_t helper_cvtqs(CPUAlphaState *env, uint64_t a)
{
    float32 fr = int64_to_float32(a, &FP_STATUS);
    return float32_to_s(fr);
}

/* Implement float64 to uint64 conversion without saturation -- we must
   supply the truncated result.  This behaviour is used by the compiler
   to get unsigned conversion for free with the same instruction.

   The VI flag is set when overflow or inexact exceptions should be raised.  */

static inline uint64_t inline_cvttq(CPUAlphaState *env, uint64_t a,
                                    int roundmode, int VI)
{
    uint64_t frac, ret = 0;
    uint32_t exp, sign, exc = 0;
    int shift;

    sign = (a >> 63);
    exp = (uint32_t)(a >> 52) & 0x7ff;
    frac = a & 0xfffffffffffffull;

    if (exp == 0) {
        if (unlikely(frac != 0)) {
            goto do_underflow;
        }
    } else if (exp == 0x7ff) {
        exc = (frac ? float_flag_invalid : VI ? float_flag_overflow : 0);
    } else {
        /* Restore implicit bit.  */
        frac |= 0x10000000000000ull;

        shift = exp - 1023 - 52;
        if (shift >= 0) {
            /* In this case the number is so large that we must shift
               the fraction left.  There is no rounding to do.  */
            if (shift < 63) {
                ret = frac << shift;
                if (VI && (ret >> shift) != frac) {
                    exc = float_flag_overflow;
                }
            }
        } else {
            uint64_t round;

            /* In this case the number is smaller than the fraction as
               represented by the 52 bit number.  Here we must think
               about rounding the result.  Handle this by shifting the
               fractional part of the number into the high bits of ROUND.
               This will let us efficiently handle round-to-nearest.  */
            shift = -shift;
            if (shift < 63) {
                ret = frac >> shift;
                round = frac << (64 - shift);
            } else {
                /* The exponent is so small we shift out everything.
                   Leave a sticky bit for proper rounding below.  */
            do_underflow:
                round = 1;
            }

            if (round) {
                exc = (VI ? float_flag_inexact : 0);
                switch (roundmode) {
                case float_round_nearest_even:
                    if (round == (1ull << 63)) {
                        /* Fraction is exactly 0.5; round to even.  */
                        ret += (ret & 1);
                    } else if (round > (1ull << 63)) {
                        ret += 1;
                    }
                    break;
                case float_round_to_zero:
                    break;
                case float_round_up:
                    ret += 1 - sign;
                    break;
                case float_round_down:
                    ret += sign;
                    break;
                }
            }
        }
        if (sign) {
            ret = -ret;
        }
    }
    if (unlikely(exc)) {
        float_raise(exc, &FP_STATUS);
    }

    return ret;
}

uint64_t helper_cvttq(CPUAlphaState *env, uint64_t a)
{
    return inline_cvttq(env, a, FP_STATUS.float_rounding_mode, 1);
}

uint64_t helper_cvttq_c(CPUAlphaState *env, uint64_t a)
{
    return inline_cvttq(env, a, float_round_to_zero, 0);
}

uint64_t helper_cvttq_svic(CPUAlphaState *env, uint64_t a)
{
    return inline_cvttq(env, a, float_round_to_zero, 1);
}

uint64_t helper_cvtqt(CPUAlphaState *env, uint64_t a)
{
    float64 fr = int64_to_float64(a, &FP_STATUS);
    return float64_to_t(fr);
}

void helper_cvtql_v_input(CPUAlphaState *env, uint64_t val)
{
    if (val != (int32_t)val) {
        arith_excp(env, GETPC(), EXC_M_IOV, 0);
    }
}
