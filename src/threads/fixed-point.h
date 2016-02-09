#ifndef THREADS_FIXED_POINT_H
#define THREADS_FIXED_POINT_H

#include <stdint.h>

/* Signed 32-bit integer type used for real arithmetic. */
typedef int32_t real;

/* Magic number F used to avoid recalculation of "1 << 14".*/
#define _MAGIC_F_ 16384

/* Convert N to fixed point. */
#define fixed_point(N)         (N * _MAGIC_F_)
/* Convert X to integer (rounding toward zero). */
#define int_rnd_zero(X)        (X / _MAGIC_F_)
/* Convert X to integer (rounding to nearest). */
#define int_rnd_nearest(X)     (X > 0 ? ((X + _MAGIC_F_ / 2) / _MAGIC_F_) : \
                                        ((X - _MAGIC_F_ / 2) / _MAGIC_F_))

/* Fixed-point arithmetic between two real numbers. */
#define add_fixed_ps(X, Y)     (X + Y)
#define sub_fixed_ps(X, Y)     (X - Y)
#define mul_fixed_ps(X, Y)     ((int32_t) (((int64_t) X) * Y / _MAGIC_F_))
#define div_fixed_ps(X, Y)     ((int32_t) (((int64_t) X) * _MAGIC_F_ / Y))
/* Fixed-point arithmetic between a real number and an integer. */
#define add_fixed_p_int(X, N)  (X + fixed_point(N))
#define sub_fixed_p_int(X, N)  (X - fixed_point(N))
#define mul_fixed_p_int(X, N)  (X * N)
#define div_fixed_p_int(X, N)  (X / N)

#endif /* threads/fixed-point.h */
