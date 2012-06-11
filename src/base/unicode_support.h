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
 * unicode_support.c : Unicode characters support
 */
#ifndef _UNICODE_SUPPORT_H_
#define _UNICODE_SUPPORT_H_

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>


typedef unsigned int uint32;
typedef unsigned short int uint16;
typedef unsigned char uchar;

#ifdef __cplusplus
extern "C"
{
#endif
  extern int unicode_process_alphabet (LOCALE_DATA * ld, bool is_verbose);
  extern int unicode_process_normalization (LOCALE_DATA * ld,
					    bool is_verbose);
  extern void unicode_free_data (void);
#if !defined (SERVER_MODE)
  extern bool unicode_string_need_compose (const char *str_in,
					   const int size_in, int *size_out,
					   const UNICODE_NORMALIZATION *
					   norm);
  extern void unicode_compose_string (const char *str_in, const int size_in,
				      char *str_out, int *size_out,
				      bool * is_composed,
				      const UNICODE_NORMALIZATION * norm);
  extern bool unicode_string_need_decompose (char *str_in, const int size_in,
					     int *decomp_size,
					     const UNICODE_NORMALIZATION *
					     norm);
  extern void unicode_decompose_string (char *str_in, const int size_in,
					char *str_out, int *size_out,
					const UNICODE_NORMALIZATION * norm);
#endif
  extern int string_to_int_array (char *s, uint32 * cp_list,
				  const int cp_list_size, const char *delims);

#ifdef __cplusplus
}
#endif

#endif				/* _UNICODE_SUPPORT_H_ */
