/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * ustring.h : case insensitive string comparison routines for 8-bit
 *             character sets
 *
 */

#ifndef _USTRING_H_
#define _USTRING_H_

#ident "$Id$"

#include <string.h>

#include "intl.h"

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

#endif				/* _USTRING_H_ */
