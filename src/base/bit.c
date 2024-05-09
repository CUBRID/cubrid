/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

/*
 *  bit.c - Bit operations
 *
 */

#ident "$Id$"

#include "bit.h"

#include <assert.h>
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/*
 * 8-bit section
 */

#define BYTE_ONES_2(n)      (n),            (n) + 1,              (n) + 1,              (n) + 2
#define BYTE_ONES_4(n)      BYTE_ONES_2(n), BYTE_ONES_2((n) + 1), BYTE_ONES_2((n) + 1), BYTE_ONES_2((n) + 2)
#define BYTE_ONES_6(n)      BYTE_ONES_4(n), BYTE_ONES_4((n) + 1), BYTE_ONES_4((n) + 1), BYTE_ONES_4((n) + 2)
#define BYTE_ONES_8(n)      BYTE_ONES_6(n), BYTE_ONES_6((n) + 1), BYTE_ONES_6((n) + 1), BYTE_ONES_6((n) + 2)
static const int byte_ones[256] = {
  BYTE_ONES_8 (0)
};

#define BYTE_ZEROS_2(n)      8 - (n),         8 - (n) - 1,           8 - (n) - 1,           8 - (n) - 2
#define BYTE_ZEROS_4(n)      BYTE_ZEROS_2(n), BYTE_ZEROS_2((n) + 1), BYTE_ZEROS_2((n) + 1), BYTE_ZEROS_2((n) + 2)
#define BYTE_ZEROS_6(n)      BYTE_ZEROS_4(n), BYTE_ZEROS_4((n) + 1), BYTE_ZEROS_4((n) + 1), BYTE_ZEROS_4((n) + 2)
#define BYTE_ZEROS_8(n)      BYTE_ZEROS_6(n), BYTE_ZEROS_6((n) + 1), BYTE_ZEROS_6((n) + 1), BYTE_ZEROS_6((n) + 2)
static const int byte_zeros[256] = {
  BYTE_ZEROS_8 (0)
};

int
bit8_count_ones (UINT8 i)
{
  /* use lookup table */
  return byte_ones[i];
}

int
bit8_count_zeros (UINT8 i)
{
  /* use lookup table */
  return byte_zeros[i];
}

int
bit8_count_trailing_zeros (UINT8 i)
{
  /* returns 0 for 1 and 8 for 0 */
  int c = 7;

  if (!i)
    {
      return 8;
    }

  /* leave last trailing 1 */
  i &= -i;

  if (i & 0x0F)			/* 00001111 */
    {
      c -= 4;
    }
  if (i & 0x33)			/* 00110011 */
    {
      c -= 2;
    }
  if (i & 0x55)			/* 01010101 */
    {
      c -= 1;
    }
  return c;
}

int
bit8_count_trailing_ones (UINT8 i)
{
  return bit8_count_trailing_zeros (~i);
}

int
bit8_count_leading_zeros (UINT8 i)
{
  int c;

  if (i == 0)
    {
      return 8;
    }
  c = 7;
  if (i & 0xF0)
    {
      c -= 4;
      i >>= 4;
    }
  if (i & 0x0C)
    {
      c -= 2;
      i >>= 2;
    }
  if (i & 0x02)
    {
      c -= 1;
      i >>= 1;
    }
  if (i & 0x01)
    {
      c -= 1;
    }
  return c;
}

int
bit8_count_leading_ones (UINT8 i)
{
  return bit8_count_leading_zeros (~i);
}

bool
bit8_is_set (UINT8 i, int off)
{
  assert (off >= 0 && off < 8);
  return (i & (((UINT8) 1) << off)) != 0;
}

UINT8
bit8_set (UINT8 i, int off)
{
  assert (off >= 0 && off < 8);
  i |= ((UINT8) 1) << off;
  return i;
}

UINT8
bit8_clear (UINT8 i, int off)
{
  assert (off >= 0 && off < 8);
  i &= ~(((UINT8) 1) << off);
  return i;
}

UINT8
bit8_set_trailing_bits (UINT8 i, int n)
{
  /* do not use it to set all bits */
  assert (n < 64);
  return i | ((((UINT8) 1) << n) - 1);
}

/*
 * 16-bit section
 */

int
bit16_count_ones (UINT16 i)
{
  /* use byte lookup table */
  return byte_ones[i & 0xFF] + byte_ones[i >> 8];
}

int
bit16_count_zeros (UINT16 i)
{
  /* use byte lookup table */
  return byte_zeros[i & 0xFF] + byte_zeros[i >> 8];
}

int
bit16_count_trailing_zeros (UINT16 i)
{
  /* returns 0 for 1 and 16 for 0 */
  int c = 15;

  if (!i)
    {
      return 16;
    }

  /* leave last trailing 1 */
  i &= -i;

  if (i & 0x00FF)		/* 0000000011111111 */
    {
      c -= 8;
    }
  if (i & 0x0F0F)		/* 0000111100001111 */
    {
      c -= 4;
    }
  if (i & 0x3333)		/* 0011001100110011 */
    {
      c -= 2;
    }
  if (i & 0x5555)		/* 0101010101010101 */
    {
      c -= 1;
    }
  return c;
}

int
bit16_count_trailing_ones (UINT16 i)
{
  return bit16_count_trailing_zeros (~i);
}

int
bit16_count_leading_zeros (UINT16 i)
{
  int c;

  if (i == 0)
    {
      return 16;
    }
  c = 15;
  if (i & 0xFF00)
    {
      c -= 8;
      i >>= 8;
    }
  if (i & 0x00F0)
    {
      c -= 4;
      i >>= 4;
    }
  if (i & 0x000C)
    {
      c -= 2;
      i >>= 2;
    }
  if (i & 0x0002)
    {
      c -= 1;
      i >>= 1;
    }
  if (i & 0x0001)
    {
      c -= 1;
    }
  return c;
}

int
bit16_count_leading_ones (UINT16 i)
{
  return bit16_count_leading_zeros (~i);
}

bool
bit16_is_set (UINT16 i, int off)
{
  assert (off >= 0 && off < 16);
  return (i & (((UINT16) 1) << off)) != 0;
}

UINT16
bit16_set (UINT16 i, int off)
{
  assert (off >= 0 && off < 16);
  i |= ((UINT16) 1) << off;
  return i;
}

UINT16
bit16_clear (UINT16 i, int off)
{
  assert (off >= 0 && off < 16);
  i &= ~(((UINT16) 1) << off);
  return i;
}

UINT16
bit16_set_trailing_bits (UINT16 i, int n)
{
  /* do not use it to set all bits */
  assert (n < 16);
  return i | ((((UINT16) 1) << n) - 1);
}

/*
 * 32-bit section
 */

int
bit32_count_ones (UINT32 i)
{
  /* Voodoo SWAR algorithm. One explanation found here: http://www.playingwithpointers.com/swar.html */
  i = i - ((i >> 1) & 0x55555555);
  i = (i & 0x33333333) + ((i >> 2) & 0x33333333);
  return (int) ((((i + (i >> 4)) & 0x0F0F0F0F) * 0x01010101) >> 24);
}

int
bit32_count_zeros (UINT32 i)
{
  return bit32_count_ones (~i);
}

int
bit32_count_trailing_zeros (UINT32 i)
{
  /* returns 0 for 1 and 32 for 0 */
  int c = 31;

  if (!i)
    {
      return 32;
    }

  /* leave last trailing 1 */
#ifdef WINDOWS
#pragma warning(disable:4146)
#endif
  i &= -i;
#ifdef WINDOWS
#pragma warning(default:4146)
#endif

  if (i & 0x0000FFFF)		/* 00000000000000001111111111111111 */
    {
      c -= 16;
    }
  if (i & 0x00FF00FF)		/* 00000000111111110000000011111111 */
    {
      c -= 8;
    }
  if (i & 0x0F0F0F0F)		/* 00001111000011110000111100001111 */
    {
      c -= 4;
    }
  if (i & 0x33333333)		/* 00110011001100110011001100110011 */
    {
      c -= 2;
    }
  if (i & 0x55555555)		/* 01010101010101010101010101010101 */
    {
      c -= 1;
    }
  return c;
}

int
bit32_count_trailing_ones (UINT32 i)
{
  return bit32_count_trailing_zeros (~i);
}

int
bit32_count_leading_zeros (UINT32 i)
{
  int c;

  if (i == 0)
    {
      return 32;
    }
  c = 31;
  if (i & 0xFFFF0000)
    {
      c -= 16;
      i >>= 16;
    }
  if (i & 0x0000FF00)
    {
      c -= 8;
      i >>= 8;
    }
  if (i & 0x000000F0)
    {
      c -= 4;
      i >>= 4;
    }
  if (i & 0x0000000C)
    {
      c -= 2;
      i >>= 2;
    }
  if (i & 0x00000002)
    {
      c -= 1;
      i >>= 1;
    }
  if (i & 0x00000001)
    {
      c -= 1;
    }
  return c;
}

int
bit32_count_leading_ones (UINT32 i)
{
  return bit32_count_leading_zeros (~i);
}

bool
bit32_is_set (UINT32 i, int off)
{
  assert (off >= 0 && off < 32);
  return (i & (((UINT32) 1) << off)) != 0;
}

UINT32
bit32_set (UINT32 i, int off)
{
  assert (off >= 0 && off < 32);
  i |= ((UINT32) 1) << off;
  return i;
}

UINT32
bit32_clear (UINT32 i, int off)
{
  assert (off >= 0 && off < 32);
  i &= ~(((UINT32) 1) << off);
  return i;
}

UINT32
bit32_set_trailing_bits (UINT32 i, int n)
{
  /* do not use it to set all bits */
  assert (n < 32);
  return i | ((((UINT32) 1) << n) - 1);
}

/*
 * 64-bit section
 */

int
bit64_count_ones (UINT64 i)
{
  /* voodoo SWAR algorithm; one explanation found here: http://www.playingwithpointers.com/swar.html */
  i = i - ((i >> 1) & 0x5555555555555555);
  i = (i & 0x3333333333333333) + ((i >> 2) & 0x3333333333333333);
  return (int) ((((i + (i >> 4)) & 0x0F0F0F0F0F0F0F0F) * 0x0101010101010101) >> 56);
}

int
bit64_count_zeros (UINT64 i)
{
  return bit64_count_ones (~i);
}

int
bit64_count_trailing_zeros (UINT64 i)
{
  /* returns 0 for 1 and 64 for 0 */
  int c = 63;

  if (!i)
    {
      return 64;
    }

  /* leave last trailing 1 */
#ifdef WINDOWS
#pragma warning(disable:4146)
#endif
  i &= -i;
#ifdef WINDOWS
#pragma warning(default:4146)
#endif

  if (i & 0x00000000FFFFFFFF)	/* 0000000000000000000000000000000011111111111111111111111111111111 */
    {
      c -= 32;
    }
  if (i & 0x0000FFFF0000FFFF)	/* 0000000000000000111111111111111100000000000000001111111111111111 */
    {
      c -= 16;
    }
  if (i & 0x00FF00FF00FF00FF)	/* 0000000011111111000000001111111100000000111111110000000011111111 */
    {
      c -= 8;
    }
  if (i & 0x0F0F0F0F0F0F0F0F)	/* 0000111100001111000011110000111100001111000011110000111100001111 */
    {
      c -= 4;
    }
  if (i & 0x3333333333333333)	/* 0011001100110011001100110011001100110011001100110011001100110011 */
    {
      c -= 2;
    }
  if (i & 0x5555555555555555)	/* 0101010101010101010101010101010101010101010101010101010101010101 */
    {
      c -= 1;
    }
  return c;
}

int
bit64_count_trailing_ones (UINT64 i)
{
  return bit64_count_trailing_zeros (~i);
}

int
bit64_count_leading_zeros (UINT64 i)
{
  int c;

  if (i == 0)
    {
      return 64;
    }
  c = 63;
  if (i & 0xFFFFFFFF00000000)
    {
      c -= 32;
      i >>= 32;
    }
  if (i & 0x00000000FFFF0000)
    {
      c -= 16;
      i >>= 16;
    }
  if (i & 0x000000000000FF00)
    {
      c -= 8;
      i >>= 8;
    }
  if (i & 0x00000000000000F0)
    {
      c -= 4;
      i >>= 4;
    }
  if (i & 0x000000000000000C)
    {
      c -= 2;
      i >>= 2;
    }
  if (i & 0x0000000000000002)
    {
      c -= 1;
      i >>= 1;
    }
  if (i & 0x0000000000000001)
    {
      c -= 1;
    }
  return c;
}

int
bit64_count_leading_ones (UINT64 i)
{
  return bit64_count_leading_zeros (~i);
}

bool
bit64_is_set (UINT64 i, int off)
{
  assert (off >= 0 && off < 64);
  return (i & (((UINT64) 1) << off)) != 0;
}

UINT64
bit64_set (UINT64 i, int off)
{
  assert (off >= 0 && off < 64);
  i |= ((UINT64) 1) << off;
  return i;
}

UINT64
bit64_clear (UINT64 i, int off)
{
  assert (off >= 0 && off < 64);
  i &= ~(((UINT64) 1) << off);
  return i;
}

UINT64
bit64_set_trailing_bits (UINT64 i, int n)
{
  /* do not use it to set all bits */
  assert (n < 64);
  return i | ((((UINT64) 1) << n) - 1);
}
