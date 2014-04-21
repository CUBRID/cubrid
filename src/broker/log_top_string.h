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
 * log_top_string.h - 
 */

#ifndef	_LOG_TOP_STRING_H_
#define	_LOG_TOP_STRING_H_

#ident "$Id$"

typedef struct t_string T_STRING;
struct t_string
{
  char *data;
  int data_len;
  int alloc_size;
  int bind_len;
};


T_STRING *t_string_make (int init_size);
void t_string_clear (T_STRING * t_str);
int t_string_add (T_STRING * t_str, char *str, int str_len);
int t_bind_string_add (T_STRING * t_str, char *str, int str_len,
		       int bind_len);
void t_string_free (T_STRING * t_str);
char *t_string_str (T_STRING * t_str);
int t_string_len (T_STRING * t_str);
int t_string_bind_len (T_STRING * t_str);

#endif /* _LOG_TOP_STRING_H_ */
