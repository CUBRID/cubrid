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
 *  bit.c - Bit operations
 *
 */

#ident "$Id$"

#include "bit.h"

#include <assert.h>

/*
 * 8-bit section
 */

int
bit8_count_ones (UINT8 i)
{
  /* Voodoo SWAR algorithm. One explanation found here: http://www.playingwithpointers.com/swar.html */
  i = i - ((i >> 1) & 0x55);
  i = (i & 0x33) + ((i >> 2) & 0x33);
  return (int) ((i + (i >> 4)) & 0x0F);
}

int
bit8_count_zeroes (UINT8 i)
{
  return bit8_count_ones (~i);
}

int
bit8_count_trailing_zeroes (UINT8 i)
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
  return bit8_count_trailing_zeroes (~i);
}

bool
bit8_is_set (UINT8 i, int off)
{
  assert (off >= 0 && off < 8);
  return i & (((UINT8) 1) << off);
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

/*
 * 16-bit section
 */

int
bit16_count_ones (UINT16 i)
{
  /* Voodoo SWAR algorithm. One explanation found here: http://www.playingwithpointers.com/swar.html */
  i = i - ((i >> 1) & 0x5555);
  i = (i & 0x3333) + ((i >> 2) & 0x3333);
  return (int) (((((i + (i >> 4)) & 0x0F0F) * 0x0101) & 0xFFFF) >> 8);
}

int
bit16_count_zeroes (UINT16 i)
{
  return bit16_count_ones (~i);
}

int
bit16_count_trailing_zeroes (UINT16 i)
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
  return bit16_count_trailing_zeroes (~i);
}

bool
bit16_is_set (UINT16 i, int off)
{
  assert (off >= 0 && off < 16);
  return i & (((UINT16) 1) << off);
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
bit32_count_zeroes (UINT32 i)
{
  return bit32_count_ones (~i);
}

int
bit32_count_trailing_zeroes (UINT32 i)
{
  /* returns 0 for 1 and 32 for 0 */
  int c = 31;

  if (!i)
    {
      return 32;
    }

  /* leave last trailing 1 */
  i &= -i;

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
  return bit32_count_trailing_zeroes (~i);
}

bool
bit32_is_set (UINT32 i, int off)
{
  assert (off >= 0 && off < 32);
  return i & (((UINT32) 1) << off);
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
bit64_count_zeroes (UINT64 i)
{
  return bit64_count_ones (~i);
}

int
bit64_count_trailing_zeroes (UINT64 i)
{
  /* returns 0 for 1 and 64 for 0 */
  int c = 63;

  if (!i)
    {
      return 64;
    }

  /* leave last trailing 1 */
  i &= -i;

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
  return bit64_count_trailing_zeroes (~i);
}

bool
bit64_is_set (UINT64 i, int off)
{
  assert (off >= 0 && off < 64);
  return i & (((UINT64) 1) << off);
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
