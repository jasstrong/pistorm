/*
 * Native FP acceleration for floatx80 operations.
 * Uses hardware double precision instead of integer-only softfloat.
 * Loses 12 bits of mantissa precision (52 vs 64) but runs on the
 * A72's VFP unit instead of doing 80-bit integer arithmetic.
 *
 * Include AFTER softfloat.h. Define SOFTFLOAT_NATIVE to enable.
 */
#ifndef SOFTFLOAT_NATIVE_H
#define SOFTFLOAT_NATIVE_H

#ifdef SOFTFLOAT_NATIVE

#include <math.h>
#include <float.h>

/* ---- floatx80 ↔ double conversion ---- */

static inline double floatx80_to_double(floatx80 a) {
    /* floatx80: high[15]=sign, high[14:0]=exp (bias 16383)
     *           low[63]=integer bit, low[62:0]=fraction
     * double:   [63]=sign, [62:52]=exp (bias 1023), [51:0]=fraction */

    int sign = (a.high >> 15) & 1;
    int32_t exp = a.high & 0x7FFF;
    uint64_t sig = a.low;

    /* Zero */
    if (exp == 0 && sig == 0)
        return sign ? -0.0 : 0.0;

    /* Infinity */
    if (exp == 0x7FFF && sig == 0x8000000000000000ULL)
        return sign ? -INFINITY : INFINITY;

    /* NaN */
    if (exp == 0x7FFF)
        return NAN;

    /* Normal: convert exponent bias and truncate mantissa */
    int32_t dbl_exp = exp - 16383 + 1023;

    /* Overflow → infinity */
    if (dbl_exp >= 2047)
        return sign ? -INFINITY : INFINITY;

    /* Underflow → zero (flush to zero for speed) */
    if (dbl_exp <= 0)
        return sign ? -0.0 : 0.0;

    /* Mantissa: drop explicit integer bit, take top 52 fraction bits */
    uint64_t dbl_sig = (sig << 1) >> 12;  /* shift out integer bit, take top 52 */

    uint64_t dbl_bits = ((uint64_t)sign << 63) |
                        ((uint64_t)dbl_exp << 52) |
                        (dbl_sig & 0x000FFFFFFFFFFFFFULL);

    double result;
    __builtin_memcpy(&result, &dbl_bits, sizeof(result));
    return result;
}

static inline floatx80 double_to_floatx80(double a) {
    uint64_t dbl_bits;
    __builtin_memcpy(&dbl_bits, &a, sizeof(dbl_bits));

    int sign = (dbl_bits >> 63) & 1;
    int32_t dbl_exp = (dbl_bits >> 52) & 0x7FF;
    uint64_t dbl_sig = dbl_bits & 0x000FFFFFFFFFFFFFULL;

    floatx80 result;

    /* Zero */
    if (dbl_exp == 0 && dbl_sig == 0) {
        result.high = sign << 15;
        result.low = 0;
        return result;
    }

    /* Infinity */
    if (dbl_exp == 2047 && dbl_sig == 0) {
        result.high = (sign << 15) | 0x7FFF;
        result.low = 0x8000000000000000ULL;
        return result;
    }

    /* NaN */
    if (dbl_exp == 2047) {
        result.high = (sign << 15) | 0x7FFF;
        result.low = 0xC000000000000000ULL;
        return result;
    }

    /* Normal */
    int32_t ext_exp = dbl_exp - 1023 + 16383;
    /* Reconstruct mantissa: explicit integer bit + fraction shifted to 64-bit */
    uint64_t ext_sig = 0x8000000000000000ULL | (dbl_sig << 11);

    result.high = (sign << 15) | (ext_exp & 0x7FFF);
    result.low = ext_sig;
    return result;
}

/* ---- Fast arithmetic using hardware double ---- */

static inline floatx80 floatx80_add_native(floatx80 a, floatx80 b, float_status *status) {
    (void)status;
    return double_to_floatx80(floatx80_to_double(a) + floatx80_to_double(b));
}

static inline floatx80 floatx80_sub_native(floatx80 a, floatx80 b, float_status *status) {
    (void)status;
    return double_to_floatx80(floatx80_to_double(a) - floatx80_to_double(b));
}

static inline floatx80 floatx80_mul_native(floatx80 a, floatx80 b, float_status *status) {
    (void)status;
    return double_to_floatx80(floatx80_to_double(a) * floatx80_to_double(b));
}

static inline floatx80 floatx80_div_native(floatx80 a, floatx80 b, float_status *status) {
    (void)status;
    return double_to_floatx80(floatx80_to_double(a) / floatx80_to_double(b));
}

static inline floatx80 floatx80_sqrt_native(floatx80 a, float_status *status) {
    (void)status;
    return double_to_floatx80(sqrt(floatx80_to_double(a)));
}

/* Redirect core operations to native implementations */
#define floatx80_add floatx80_add_native
#define floatx80_sub floatx80_sub_native
#define floatx80_mul floatx80_mul_native
#define floatx80_div floatx80_div_native
#define floatx80_sqrt floatx80_sqrt_native

#endif /* SOFTFLOAT_NATIVE */
#endif /* SOFTFLOAT_NATIVE_H */
