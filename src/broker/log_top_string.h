/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 *   This program is free software; you can redistribute it and/or modify 
 *   it under the terms of the GNU General Public License as published by 
 *   the Free Software Foundation; version 2 of the License. 
 *
 *  This program is distributed in the hope that it will be useful, 
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of 
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the 
 *  GNU General Public License for more details. 
 *
 *  You should have received a copy of the GNU General Public License 
 *  along with this program; if not, write to the Free Software 
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA 
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
};


T_STRING *t_string_make (int init_size);
void t_string_clear (T_STRING * t_str);
int t_string_add (T_STRING * t_str, char *str, int str_len);
void t_string_free (T_STRING * t_str);
char *t_string_str (T_STRING * t_str);
int t_string_len (T_STRING * t_str);

#endif /* _LOG_TOP_STRING_H_ */
