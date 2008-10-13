/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * vstr.h : Variable-length string
 *
 */

#ifndef _VSTR_H_
#define _VSTR_H_

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
/* TODO: esql정리하면서 varstirng이름 같이 변경  */

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

#endif /* _VSTR_H_ */
