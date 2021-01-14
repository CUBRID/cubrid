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
 * variable_string.h : Variable-length string
 *
 */

#ifndef _VARIABLE_STRING_H_
#define _VARIABLE_STRING_H_

#ident "$Id$"

#include <sys/types.h>

#ifdef __cplusplus
extern "C"
{
#endif


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
    char *base;
    char *limit;
    char *start;
    char *end;
    int heap_allocated;
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

#ifdef __cplusplus
}
#endif

#endif				/* _VARIABLE_STRING_H_ */
