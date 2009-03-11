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
 * misc_string.h : case insensitive string comparison routines for 8-bit
 *             character sets
 *
 */

#ifndef _MISC_STRING_H_
#define _MISC_STRING_H_

#ident "$Id$"

#include <string.h>

#include "intl_support.h"

#ifdef __cplusplus
extern "C"
{
#endif

  extern char *ustr_casestr (const char *s1, const char *s2);
  extern char *ustr_upper (char *s);
  extern char *ustr_lower (char *s);

#ifdef __cplusplus
}
#endif

#endif				/* _MISC_STRING_H_ */
