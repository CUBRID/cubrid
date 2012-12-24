/*
 * Copyright (c) 1993 Martin Birgmeier
 * All rights reserved.
 *
 * You may redistribute unmodified or modified versions of this source
 * code provided that the above copyright notice and this and the
 * following conditions are retained.
 *
 * This software is provided ``as is'', and comes with no warranties
 * of any kind. I shall in no event be liable for anything that happens
 * to anyone/anything when using this software.
 */

/*
 * rand.c - rand48 implementations for Windows
 */

#if defined (WINDOWS)
#include "porting.h"
#define LRAND48_MAX	(2147483648)

#define RAND48_SEED_0   (0x330e)
#define RAND48_SEED_1   (0xabcd)
#define RAND48_SEED_2   (0x1234)
#define RAND48_MULT_0   (0xe66d)
#define RAND48_MULT_1   (0xdeec)
#define RAND48_MULT_2   (0x0005)
#define RAND48_ADD      (0x000b)


unsigned short _rand48_seed[3] = {
  RAND48_SEED_0,
  RAND48_SEED_1,
  RAND48_SEED_2
};
unsigned short _rand48_mult[3] = {
  RAND48_MULT_0,
  RAND48_MULT_1,
  RAND48_MULT_2
};
unsigned short _rand48_add = RAND48_ADD;

static void
_dorand48 (unsigned short xseed[3])
{
  unsigned long accu;
  unsigned short temp[2];

  accu = (unsigned long) _rand48_mult[0] * (unsigned long) xseed[0] +
    (unsigned long) _rand48_add;
  temp[0] = (unsigned short) accu;	/* lower 16 bits */
  accu >>= sizeof (unsigned short) * 8;
  accu += (unsigned long) _rand48_mult[0] * (unsigned long) xseed[1] +
    (unsigned long) _rand48_mult[1] * (unsigned long) xseed[0];
  temp[1] = (unsigned short) accu;	/* middle 16 bits */
  accu >>= sizeof (unsigned short) * 8;
  accu +=
    _rand48_mult[0] * xseed[2] + _rand48_mult[1] * xseed[1] +
    _rand48_mult[2] * xseed[0];
  xseed[0] = temp[0];
  xseed[1] = temp[1];
  xseed[2] = (unsigned short) accu;
}

long
lrand48 (void)
{
  _dorand48 (_rand48_seed);
  return ((long) _rand48_seed[2] << 15) + ((long) _rand48_seed[1] >> 1);
}

void
srand48 (long seed)
{
  _rand48_seed[0] = RAND48_SEED_0;
  _rand48_seed[1] = (unsigned short) seed;
  _rand48_seed[2] = (unsigned short) (seed >> 16);
  _rand48_mult[0] = RAND48_MULT_0;
  _rand48_mult[1] = RAND48_MULT_1;
  _rand48_mult[2] = RAND48_MULT_2;
  _rand48_add = RAND48_ADD;
}

double
drand48 (void)
{
  /* lrand48 returns a number between 0 and 2^31-1. So, we divide it by 2^31 
   * to generate floating-point value uniformly distributed between [0.0, 1.0).
   */
  return (double) ((double) lrand48 () / (double) LRAND48_MAX);
}

int
srand48_r (long int seed, struct drand48_data *buffer)
{
  buffer->_rand48_seed[0] = RAND48_SEED_0;
  buffer->_rand48_seed[1] = (unsigned short) seed;
  buffer->_rand48_seed[2] = (unsigned short) (seed >> 16);
  return 0;
}

int
lrand48_r (struct drand48_data *buffer, long int *result)
{
  _dorand48 (buffer->_rand48_seed);
  if (result)
    {
      *result =
	((long) buffer->_rand48_seed[2] << 15) +
	((long) buffer->_rand48_seed[1] >> 1);
    }
  return 0;
}

int
drand48_r (struct drand48_data *buffer, double *result)
{
  long int r;
  lrand48_r (buffer, &r);
  if (result)
    {
      *result = (double) ((double) r / (double) LRAND48_MAX);
    }
  return 0;
}

int
rand_r (unsigned int *seedp)
{
  int ret;
  if (rand_s (&ret) == 0)
    {
      return ret;
    }
  return 0;
}
#endif /* WINDOWS */
