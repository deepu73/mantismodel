#ifndef RANDOM_NUMBERS_H
#define RANDOM_NUMBERS_H

/*
 * Function to generate a rand num between 0 and 1.
 *
 * When MT_CLOSED_INTVL is defined, the end points
 *   are inclusive (i.e. returns [0,1]).
 * Otherwise, the return value is half-closed [0,1).
 */
extern double RandDecimal(void);

/*
 * Function to generate a rand number
 *   (integer) between 0 and max, inclusive.
 */
extern int RandInt(int max);

/*
 * Seed the random number generators.
 */
extern void SeedRandInt(long seed);
extern void SeedRandDecimal(long seed);

/* This is for using the Mersenne Twister PRNG */
#define RandDecimal()		MTrandDec()
#define RandInt(A)		MTrandInt(A)
#define SeedRandInt(A)		seedMT(A)
#define SeedRandDecimal(A)	dummyInit(A)

#include "mt-rand.h"

#endif /* RANDOM_NUMBERS_H */
