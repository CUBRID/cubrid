/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * chartype.h : character type checking functions
 *
 *  Note : Functions defined in "ctypes.h" may work incorrectly
 *         on multi-byte characters.
 */

#ifndef _CHARTYPE_H_
#define _CHARTYPE_H_

#ident "$Id$"

#ifdef __cplusplus
extern "C"
{
#endif

  extern int char_islower (int c);
  extern int char_isupper (int c);
  extern int char_isalpha (int c);
  extern int char_isdigit (int c);
  extern int char_isxdigit (int c);
  extern int char_isalnum (int c);
  extern int char_isspace (int c);
  extern int char_iseol (int c);
  extern int char_isascii (int c);

  extern int char_tolower (int c);
  extern int char_toupper (int c);

  extern int char_isupper_iso8859 (int c);
  extern int char_islower_iso8859 (int c);
  extern int char_tolower_iso8859 (int c);
  extern int char_toupper_iso8859 (int c);

#ifdef __cplusplus
}
#endif

#endif				/* _CHARTYPE_H_ */
