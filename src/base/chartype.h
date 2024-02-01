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

#define USE_MACRO_CHARTYPE	// ctshim

#if defined(USE_MACRO_CHARTYPE)
//=============================================================================
#define CHAR_PROP_NONE        (0x00)
#define CHAR_PROP_UPPER       (0x01)	/* uppercase.  */
#define CHAR_PROP_LOWER       (0x02)	/* lowercase.  */
#define CHAR_PROP_DIGIT       (0x04)	/* Numeric.    */
#define CHAR_PROP_SPACE       (0x08)	/* space, Tab, newline, carriage return   */
#define CHAR_PROP_HEXNUM      (0x10)	/* 0~9, a~f, A~F  */
#define CHAR_PROP_EOL         (0x20)	/* \r \n  */
#define CHAR_PROP_ISO8859_UPPER (0x40)
#define CHAR_PROP_ISO8859_LOWER (0x80)

#define CHAR_PROP_ALPHA       (CHAR_PROP_UPPER | CHAR_PROP_LOWER)	/* Alphabetic.  */
#define CHAR_PROP_ALPHA_NUM   (CHAR_PROP_ALPHA | CHAR_PROP_DIGIT)

  typedef unsigned char char_type_prop;
  extern const char_type_prop *char_properties_ptr;

#define char_islower(c)   (char_properties_ptr[(u_char) c] & CHAR_PROP_LOWER)
#define char_isupper(c)   (char_properties_ptr[(u_char) c] & CHAR_PROP_UPPER)
#define char_isalpha(c)   (char_properties_ptr[(u_char) c] & CHAR_PROP_ALPHA)
#define char_isdigit(c)   (char_properties_ptr[(u_char) c] & CHAR_PROP_DIGIT)
#define char_isalnum(c)   (char_properties_ptr[(u_char) c] & CHAR_PROP_ALPHA_NUM)
#define char_isspace(c)   (char_properties_ptr[(u_char) c] & CHAR_PROP_SPACE)
#define char_iseol(c)     (char_properties_ptr[(u_char) c] & CHAR_PROP_EOL)
#define char_isxdigit(c)  (char_properties_ptr[(u_char) c] & CHAR_PROP_HEXNUM)

#define char_islower_iso8859(c) (char_properties_ptr[(u_char) c] & (CHAR_PROP_LOWER|CHAR_PROP_ISO8859_LOWER))
#define char_isupper_iso8859(c) (char_properties_ptr[(u_char) c] & (CHAR_PROP_UPPER|CHAR_PROP_ISO8859_UPPER))
//=============================================================================
#else				/* #if defined(USE_MACRO_CHARTYPE) */
//=============================================================================
  extern int char_islower (int c);
  extern int char_isupper (int c);
  extern int char_isalpha (int c);
  extern int char_isdigit (int c);
  extern int char_isalnum (int c);
  extern int char_isspace (int c);
  extern int char_iseol (int c);
  extern int char_isxdigit (int c);

  extern int char_isupper_iso8859 (int c);
  extern int char_islower_iso8859 (int c);
//=============================================================================
#endif				/* #if defined(USE_MACRO_CHARTYPE) */
#if defined (ENABLE_UNUSED_FUNCTION)
  extern int char_isascii (int c);
#endif
  extern int char_tolower (int c);
  extern int char_toupper (int c);

  extern int char_tolower_iso8859 (int c);
  extern int char_toupper_iso8859 (int c);

#ifdef __cplusplus
}
#endif

#endif				/* _CHARTYPE_H_ */
