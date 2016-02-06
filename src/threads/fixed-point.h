#ifndef THREADS_FIXED-POINT_H
#define THREADS_FIXED-POINT_H

#include <stdint.h>

#define Q 14
#define F (1 << Q)

#define FIXED_POINT(n)          (n * Q)

#define INT_RND_D(x)            (x / F)
#define INT_RND(x)              (x >= 0 ? ((x + F / 2) / F) : ((x - F / 2) / F))

#define ADD_FIXED_PS(x, y)      (x + y)
#define SUB_FIXED_PS(x, y)      (x - y)
#define ADD_FIXED_P_INT(x, n)   (x + (FIXED_POINT(n)))
#define SUB_FIXED_P_INT(x, n)   (x - (FIXED_POINT(n)))
#define MUL_FIXED_PS(x, y)      (((int64_t) x) * y / F)
#define MUL_FIXED_P_INT(x, n)   (x * n)
#define DIV_FIXED_PS(x, y)      (((int64_t) x) * F / y)
#define DIV_FIXED_P_INT(x, n)   (x / n)

#endif /* threads/fixed-point.h */