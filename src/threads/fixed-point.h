#ifndef THREADS_FIXED_POINT_H
#define THREADS_FIXED_POINT_H

#include <stdint.h>

typedef int32_t real;

#define Q 14
#define F (1 << Q)

#define fixed_point(N)         (N * F)

#define int_rnd_zero(X)        (X / F)
#define int_rnd_nearest(X)     ((X >= 0) ? ((X + F / 2) / F) : ((X - F / 2) / F))

#define add_fixed_ps(X, Y)     (X + Y)
#define add_fixed_p_int(X, N)  (X + (fixed_point(N)))
#define sub_fixed_ps(X, Y)     (X - Y)
#define sub_fixed_p_int(X, N)  (X - (fixed_point(N)))
#define mul_fixed_ps(X, Y)     ((int32_t) (((int64_t) X) * Y / F))
#define mul_fixed_p_int(X, N)  (X * N)
#define div_fixed_ps(X, Y)     ((int32_t) (((int64_t) X) * F / Y))
#define div_fixed_p_int(X, N)  (X / N)

#endif /* threads/fixed-point.h */
