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

  extern int ansisql_strcasecmp (const char *s, const char *t);
  extern int ansisql_strcmp (const char *s, const char *t);

#ifdef __cplusplus
}
#endif

#endif				/* _MISC_STRING_H_ */
