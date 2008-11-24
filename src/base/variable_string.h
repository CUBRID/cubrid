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
 * variable_string.h : Variable-length string
 *
 */

#ifndef _VARIABLE_STRING_H_
#define _VARIABLE_STRING_H_

#ident "$Id$"

#include <sys/types.h>

/*
 * Variable-length string gadgets.  vs_new() takes the address of a
 * varstring or NULL; if NULL, it heap-allocates a new one and returns
 * its address. vs_free() should be used both for stack-allocated and
 * heap-allocated varstrings; it will free all internal structures,
 * and then free the varstring structure itself if it was heap
 * allocated.
 */

typedef struct
{
  int heap_allocated;
  char *base;
  char *limit;
  char *start;
  char *end;
} varstring;

extern varstring *vs_new (varstring * vstr);
extern void vs_free (varstring * vstr);
extern void vs_clear (varstring * vstr);
extern int vs_append (varstring * vstr, const char *suffix);
extern int vs_prepend (varstring * vstr, const char *prefix);
extern int vs_sprintf (varstring * vstr, const char *fmt, ...);
extern int vs_strcat (varstring * vstr, const char *str);
extern int vs_strcatn (varstring * vstr, const char *str, int length);
extern int vs_strcpy (varstring * vstr, const char *str);
extern int vs_putc (varstring * vstr, int);
extern char *vs_str (varstring * vstr);
extern int vs_strlen (const varstring * vstr);

#endif /* _VARIABLE_STRING_H_ */
