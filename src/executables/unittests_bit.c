/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

/*
 * unittest_bit.c : unit tests for bit operations
 */

#include "bit.h"
#include "system.h"
#include <stdlib.h>
#include <assert.h>

int
count_bits (char *mem, int nbytes)
{
  int byte, bit;
  int count = 0;

  for (byte = 0; byte < nbytes; byte++)
    {
      for (bit = 0; bit < 8; bit++)
	{
	  if (mem[byte] & (((char) 1) << bit) != 0)
	    {
	      count++;
	    }
	}
    }
  return count;
}

int
ctz8 (UINT8 x)
{
  int i;
  for (i = 0; i < 8; i++)
    {
      if ((x & (((UINT8) 1) << i)) != 0)
	{
	  break;
	}
    }
  return i;
}

int
ctz16 (UINT16 x)
{
  int i;
  for (i = 0; i < 16; i++)
    {
      if ((x & (((UINT16) 1) << i)) != 0)
	{
	  break;
	}
    }
  return i;
}

int
ctz32 (UINT32 x)
{
  int i;
  for (i = 0; i < 32; i++)
    {
      if ((x & (((UINT32) 1) << i)) != 0)
	{
	  break;
	}
    }
  return i;
}

int
ctz64 (UINT64 x)
{
  int i;
  for (i = 0; i < 16; i++)
    {
      if ((x & (((UINT64) 1) << i)) != 0)
	{
	  break;
	}
    }
  return i;
}

int
main (int ignore_argc, char **ignore_argv)
{
  int i;
  UINT8 ub;
  UINT16 us;
  UINT32 ui;
  UINT64 ull;

  int bitset_count;

  int rands[2];

  srand (time (NULL));

  for (i = 0; i < 256; i++)
    {
      rands[0] = rand ();
      rands[1] = rand ();

      /* check bit8 */
      ub = i;
      /* check bit count */
      bitset_count = count_bits (&ub, 8);
      assert (bitset_count == bit8_count_ones (ub));
      assert ((8 - bitset_count) == bit8_count_zeroes (ub));
      /* check count trailing zeros/ones */
      assert (ctz8 (ub) == bit8_count_trailing_zeroes (ub));
      assert (ctz8 (~ub) == bit8_count_trailing_ones (ub));

      /* check bit16 */
      us = *(UINT16 *) rands;
      /* check bit count */
      bitset_count = count_bits (&us, 16);
      assert (bitset_count == bit16_count_ones (us));
      assert ((16 - bitset_count) == bit16_count_zeroes (us));
      /* check count trailing zeros/ones */
      assert (ctz16 (us) == bit16_count_trailing_zeroes (us));
      assert (ctz16 (~us) == bit16_count_trailing_ones (us));

      /* check bit32 */
      ui = *(UINT32 *) rands;
      /* check bit count */
      bitset_count = count_bits (&ui, 32);
      assert (bitset_count == bit32_count_ones (ui));
      assert ((32 - bitset_count) == bit32_count_zeroes (ui));
      /* check count trailing zeros/ones */
      assert (ctz32 (ui) == bit32_count_trailing_zeroes (ui));
      assert (ctz32 (~ui) == bit32_count_trailing_ones (ui));

      /* check bit64 */
      ull = *(UINT64 *) rands;
      /* check bit count */
      bitset_count = count_bits (&ull, 64);
      assert (bitset_count == bit64_count_ones (ull));
      assert ((64 - bitset_count) == bit64_count_zeroes (ull));
      /* check count trailing zeros/ones */
      assert (ctz64 (ull) == bit64_count_trailing_zeroes (ull));
      assert (ctz64 (~ull) == bit64_count_trailing_ones (ull));
    }
}
