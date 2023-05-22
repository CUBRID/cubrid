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
