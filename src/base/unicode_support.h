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
  extern void unicode_free_data (void);
#ifdef __cplusplus
}
#endif

#endif				/* _UNICODE_SUPPORT_H_ */
