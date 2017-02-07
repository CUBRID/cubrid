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

static int
count_bits (const unsigned char *mem, int nbits)
{
  int byte, bit;
  int count = 0;

  for (byte = 0; byte < nbits / 8; byte++)
    {
      for (bit = 0; bit < 8; bit++)
	{
	  if ((mem[byte] & (((char) 1) << bit)) != 0)
	    {
	      count++;
	    }
	}
    }
  return count;
}

static int
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

static int
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

static int
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

static int
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

static void
bitset_string (const unsigned char *mem, int nbits, char *bitset)
{
  int byte, bit;
  int count = 0;

  for (byte = 0; byte < nbits / 8; byte++)
    {
      for (bit = 0; bit < 8; bit++)
	{
	  bitset[count++] = ((mem[byte] & (((char) 1) << bit)) != 0) ? '1' : '0';
	}
      bitset[count++] = ' ';
    }
  bitset[count] = '\0';
}

int
main (int ignore_argc, char **ignore_argv)
{
  int i;
  UINT8 ub;
  UINT16 us;
  UINT32 ui;
  UINT64 ull;

  int unittest_result;
  int bit_result;

  char bitset_buf[100];

  int rands[2];

  printf ("start test\n");

  srand (time (NULL));

  for (i = 0; i < 256; i++)
    {
      rands[0] = rand ();
      rands[1] = rand ();

      /* check bit8 */
      ub = i;
      /* check bit count */
      unittest_result = count_bits (&ub, 8);
      bit_result = bit8_count_ones (ub);
      if (unittest_result != bit_result)
	{
	  bitset_string (&ub, 8, bitset_buf);
	  printf ("error bit8_count_ones: \n"
		  "bitset: %s\n unit test: %d\n bit8: %d\n", bitset_buf, unittest_result, bit_result);
	  abort ();
	}
      unittest_result = 8 - unittest_result;
      bit_result = bit8_count_zeros (ub);
      if (unittest_result != bit_result)
	{
	  bitset_string (&ub, 8, bitset_buf);
	  printf ("error bit8_count_zeros: \n"
		  "bitset: %s\n unit test: %d\n bit8: %d\n", bitset_buf, unittest_result, bit_result);
	  abort ();
	}
      /* check count trailing zeros/ones */
      unittest_result = ctz8 (ub);
      bit_result = bit8_count_trailing_zeros (ub);
      if (unittest_result != bit_result)
	{
	  bitset_string (&ub, 8, bitset_buf);
	  printf ("error bit8_count_trailing_zeros: \n"
		  "bitset: %s\n unit test: %d\n bit8: %d\n", bitset_buf, unittest_result, bit_result);
	  abort ();
	}
      unittest_result = ctz8 (~ub);
      bit_result = bit8_count_trailing_ones (ub);
      if (unittest_result != bit_result)
	{
	  bitset_string (&ub, 8, bitset_buf);
	  printf ("error bit8_count_trailing_ones: \n"
		  "bitset: %s\n unit test: %d\n bit8: %d\n", bitset_buf, unittest_result, bit_result);
	  abort ();
	}

      /* check bit16 */
      us = *(UINT16 *) rands;
      /* check bit count */
      unittest_result = count_bits ((unsigned char *) &us, 16);
      bit_result = bit16_count_ones (us);
      if (unittest_result != bit_result)
	{
	  bitset_string ((unsigned char *) &us, 16, bitset_buf);
	  printf ("error bit16_count_ones: \n"
		  "bitset: %s\n unit test: %d\n bit16: %d\n", bitset_buf, unittest_result, bit_result);
	  abort ();
	}
      unittest_result = 16 - unittest_result;
      bit_result = bit16_count_zeros (us);
      if (unittest_result != bit_result)
	{
	  bitset_string ((unsigned char *) &us, 16, bitset_buf);
	  printf ("error bit16_count_zeros: \n"
		  "bitset: %s\n unit test: %d\n bit16: %d\n", bitset_buf, unittest_result, bit_result);
	  abort ();
	}
      /* check count trailing zeros/ones */
      unittest_result = ctz16 (us);
      bit_result = bit16_count_trailing_zeros (us);
      if (unittest_result != bit_result)
	{
	  bitset_string ((unsigned char *) &us, 16, bitset_buf);
	  printf ("error bit16_count_trailing_zeros: \n"
		  "bitset: %s\n unit test: %d\n bit16: %d\n", bitset_buf, unittest_result, bit_result);
	  abort ();
	}
      unittest_result = ctz16 (~us);
      bit_result = bit16_count_trailing_ones (us);
      if (unittest_result != bit_result)
	{
	  bitset_string ((unsigned char *) &us, 16, bitset_buf);
	  printf ("error bit16_count_trailing_ones: \n"
		  "bitset: %s\n unit test: %d\n bit16: %d\n", bitset_buf, unittest_result, bit_result);
	  abort ();
	}

      /* check bit32 */
      ui = *(UINT32 *) rands;
      /* check bit count */
      unittest_result = count_bits ((unsigned char *) &ui, 32);
      bit_result = bit32_count_ones (ui);
      if (unittest_result != bit_result)
	{
	  bitset_string ((unsigned char *) &ui, 32, bitset_buf);
	  printf ("error bit32_count_ones: \n"
		  "bitset: %s\n unit test: %d\n bit32: %d\n", bitset_buf, unittest_result, bit_result);
	  abort ();
	}
      unittest_result = 32 - unittest_result;
      bit_result = bit32_count_zeros (ui);
      if (unittest_result != bit_result)
	{
	  bitset_string ((unsigned char *) &ui, 32, bitset_buf);
	  printf ("error bit32_count_zeros: \n"
		  "bitset: %s\n unit test: %d\n bit32: %d\n", bitset_buf, unittest_result, bit_result);
	  abort ();
	}
      /* check count trailing zeros/ones */
      unittest_result = ctz32 (ui);
      bit_result = bit32_count_trailing_zeros (ui);
      if (unittest_result != bit_result)
	{
	  bitset_string ((unsigned char *) &ui, 32, bitset_buf);
	  printf ("error bit32_count_trailing_zeros: \n"
		  "bitset: %s\n unit test: %d\n bit32: %d\n", bitset_buf, unittest_result, bit_result);
	  abort ();
	}
      unittest_result = ctz32 (~ui);
      bit_result = bit32_count_trailing_ones (ui);
      if (unittest_result != bit_result)
	{
	  bitset_string ((unsigned char *) &ui, 32, bitset_buf);
	  printf ("error bit32_count_trailing_ones: \n"
		  "bitset: %s\n unit test: %d\n bit32: %d\n", bitset_buf, unittest_result, bit_result);
	  abort ();
	}

      /* check bit64 */
      ull = *(UINT64 *) rands;
      /* check bit count */
      unittest_result = count_bits ((unsigned char *) &ull, 64);
      bit_result = bit64_count_ones (ull);
      if (unittest_result != bit_result)
	{
	  bitset_string ((unsigned char *) &ull, 64, bitset_buf);
	  printf ("error bit64_count_ones: \n"
		  "bitset: %s\n unit test: %d\n bit64: %d\n", bitset_buf, unittest_result, bit_result);
	  abort ();
	}
      unittest_result = 64 - unittest_result;
      bit_result = bit64_count_zeros (ull);
      if (unittest_result != bit_result)
	{
	  bitset_string ((unsigned char *) &ull, 64, bitset_buf);
	  printf ("error bit64_count_zeros: \n"
		  "bitset: %s\n unit test: %d\n bit64: %d\n", bitset_buf, unittest_result, bit_result);
	  abort ();
	}
      /* check count trailing zeros/ones */
      unittest_result = ctz64 (ull);
      bit_result = bit64_count_trailing_zeros (ull);
      if (unittest_result != bit_result)
	{
	  bitset_string ((unsigned char *) &ull, 64, bitset_buf);
	  printf ("error bit64_count_trailing_zeros: \n"
		  "bitset: %s\n unit test: %d\n bit64: %d\n", bitset_buf, unittest_result, bit_result);
	  abort ();
	}
      unittest_result = ctz64 (~ull);
      bit_result = bit64_count_trailing_ones (ull);
      if (unittest_result != bit_result)
	{
	  bitset_string ((unsigned char *) &ull, 64, bitset_buf);
	  printf ("error bit64_count_trailing_ones: \n"
		  "bitset: %s\n unit test: %d\n bit64: %d\n", bitset_buf, unittest_result, bit_result);
	  abort ();
	}
    }
  printf ("success\n");
  return 0;
}
