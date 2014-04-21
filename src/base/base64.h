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
*  base64.h -
*/

#ifndef __BASE64_H_
#define __BASE64_H_

#ident "$Id$"

/* internal error code only for base64 */
enum
{
  BASE64_EMPTY_INPUT = 1,
  BASE64_INVALID_INPUT = 2
};


extern int
base64_encode (const unsigned char *src, int src_len,
	       unsigned char **dest, int *dest_len);
extern int
base64_decode (const unsigned char *src, int src_len,
	       unsigned char **dest, int *dest_len);

#endif
