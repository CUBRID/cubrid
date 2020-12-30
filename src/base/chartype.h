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

  extern int char_islower (int c);
  extern int char_isupper (int c);
  extern int char_isalpha (int c);
  extern int char_isdigit (int c);
  extern int char_isalnum (int c);
  extern int char_isspace (int c);
  extern int char_iseol (int c);
  extern int char_isxdigit (int c);
#if defined (ENABLE_UNUSED_FUNCTION)
  extern int char_isascii (int c);
#endif

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
