/*
 * Copyright (c) 2000-2007 by Nicolas Devillard.
 * MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef _INI_PARSER_H_
#define _INI_PARSER_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(WINDOWS)
#include <unistd.h>
#endif

typedef struct ini_table INI_TABLE;
struct ini_table
{
  int size;			/* storage size */
  int n;			/* number of entries in INI_TABLE */
  int nsec;			/* number of sector in INI_TABLE */
  char **key;			/* list of string keys */
  char **val;			/* list of string values */
  int *lineno;			/* list of lineno values for keys */
  unsigned int *hash;		/* list of hash values for keys */
};

extern INI_TABLE *ini_parser_load (const char *ininame);
extern void ini_parser_free (INI_TABLE * ini);

extern int ini_findsec (INI_TABLE * ini, const char *sec);
extern char *ini_getsecname (INI_TABLE * ini, int n, int *lineno);
extern int ini_hassec (const char *key);
extern int ini_seccmp (const char *key1, const char *key2, bool ignore_case);

extern const char *ini_getstr (INI_TABLE * ini, const char *sec, const char *key, const char *def, int *lineno);
extern int ini_getint (INI_TABLE * ini, const char *sec, const char *key, int def, int *lineno);
extern int ini_getuint (INI_TABLE * ini, const char *sec, const char *key, int def, int *lineno);
#if defined(ENABLE_UNUSED_FUNCTION)
extern int ini_getuint_min (INI_TABLE * ini, const char *sec, const char *key, int def, int min, int *lineno);
#endif
extern int ini_getuint_max (INI_TABLE * ini, const char *sec, const char *key, int def, int max, int *lineno);
extern int ini_gethex (INI_TABLE * ini, const char *sec, const char *key, int def, int *lineno);
extern float ini_getfloat (INI_TABLE * ini, const char *sec, const char *key, float def, int *lineno);
#endif /* _INI_PARSER_H_ */
