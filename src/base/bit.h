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
*  bit.h - Bit operations
*/

#ifndef __BIT_H_
#define __BIT_H_

#ident "$Id$"

#include "system.h"

#define BIT8_FULL               ((UINT8) 0xFF)
#define BIT16_FULL              ((UINT16) 0xFFFF)
#define BIT32_FULL              ((UINT32) 0xFFFFFFFF)
#define BIT64_FULL              ((UINT64) 0xFFFFFFFFFFFFFFFF)

#define BIT8_HEXA_PRINT_FORMAT    "0x%02x"
#define BIT16_HEXA_PRINT_FORMAT   "0x%04x"
#define BIT32_HEXA_PRINT_FORMAT   "0x%08x"
#define BIT64_HEXA_PRINT_FORMAT   "0x%016llx"

extern int bit8_count_ones (UINT8 i);
extern int bit8_count_zeros (UINT8 i);
extern int bit8_count_trailing_ones (UINT8 i);
extern int bit8_count_trailing_zeros (UINT8 i);
extern int bit8_count_leading_ones (UINT8 i);
extern int bit8_count_leading_zeros (UINT8 i);
extern bool bit8_is_set (UINT8 i, int off);
extern UINT8 bit8_set (UINT8 i, int off);
extern UINT8 bit8_clear (UINT8 i, int off);
extern UINT8 bit8_set_trailing_bits (UINT8 i, int n);

extern int bit16_count_ones (UINT16 i);
extern int bit16_count_zeros (UINT16 i);
extern int bit16_count_trailing_ones (UINT16 i);
extern int bit16_count_trailing_zeros (UINT16 i);
extern int bit16_count_leading_ones (UINT16 i);
extern int bit16_count_leading_zeros (UINT16 i);
extern bool bit16_is_set (UINT16 i, int off);
extern UINT16 bit16_set (UINT16 i, int off);
extern UINT16 bit16_clear (UINT16 i, int off);
extern UINT16 bit16_set_trailing_bits (UINT16 i, int n);

extern int bit32_count_ones (UINT32 i);
extern int bit32_count_zeros (UINT32 i);
extern int bit32_count_trailing_ones (UINT32 i);
extern int bit32_count_trailing_zeros (UINT32 i);
extern int bit32_count_leading_ones (UINT32 i);
extern int bit32_count_leading_zeros (UINT32 i);
extern bool bit32_is_set (UINT32 i, int off);
extern UINT32 bit32_set (UINT32 i, int off);
extern UINT32 bit32_clear (UINT32 i, int off);
extern UINT32 bit32_set_trailing_bits (UINT32 i, int n);

extern int bit64_count_ones (UINT64 i);
extern int bit64_count_zeros (UINT64 i);
extern int bit64_count_trailing_ones (UINT64 i);
extern int bit64_count_trailing_zeros (UINT64 i);
extern int bit64_count_leading_ones (UINT64 i);
extern int bit64_count_leading_zeros (UINT64 i);
extern bool bit64_is_set (UINT64 i, int off);
extern UINT64 bit64_set (UINT64 i, int off);
extern UINT64 bit64_clear (UINT64 i, int off);
extern UINT64 bit64_set_trailing_bits (UINT64 i, int n);

#endif
