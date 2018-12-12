#ifndef _H8300_BARRIER_H
#define _H8300_BARRIER_H

#define nop()  asm volatile ("nop"::)

/*
 * Force strict CPU ordering.
 * Not really required on H8...
 */
#define mb()   asm volatile (""   : : :"memory")
#define rmb()  asm volatile (""   : : :"memory")
#define wmb()  asm volatile (""   : : :"memory")
#define set_mb(var, value) do { xchg(&var, value); } while (0)

#define smp_mb__before_atomic()		barrier()
#define smp_mb__after_atomic()		barrier()

#include <asm-generic/barrier.h>

#endif /* _H8300_BARRIER_H */
