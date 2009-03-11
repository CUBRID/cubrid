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
 * cm_dstring.h - 
 */

#ifndef _CM_DSTRING_H_
#define _CM_DSTRING_H_

#ident "$Id$"

typedef struct dstring_t
{
  int dsize;			/* allocated dbuf size */
  int dlen;			/* string length stored in dbuf */
  char *dbuf;
} dstring;

dstring *dst_create (void);
void dst_destroy (dstring * dstr);
void dst_reset (dstring * dstr);
int dst_append (dstring * dstr, const char *str, int slen);
int dst_length (dstring * dstr);
int dst_size (dstring * dstr);
char *dst_buffer (dstring * dstr);

#endif /* _CM_DSTRING_H_ */
