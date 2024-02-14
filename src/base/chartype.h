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

#define CHAR_PROP_NONE        (0x00)
#define CHAR_PROP_UPPER       (0x01)	/* uppercase.  */
#define CHAR_PROP_LOWER       (0x02)	/* lowercase.  */
#define CHAR_PROP_DIGIT       (0x04)	/* Numeric.    */
#define CHAR_PROP_SPACE       (0x08)	/* space, \t \n \r \f \v */
#define CHAR_PROP_HEXNUM      (0x10)	/* 0~9, a~f, A~F  */
#define CHAR_PROP_EOL         (0x20)	/* \r \n  */
#define CHAR_PROP_ISO8859_UPPER (0x40)
#define CHAR_PROP_ISO8859_LOWER (0x80)

#define CHAR_PROP_ALPHA       (CHAR_PROP_UPPER | CHAR_PROP_LOWER)	/* Alphabetic.  */
#define CHAR_PROP_ALPHA_NUM   (CHAR_PROP_ALPHA | CHAR_PROP_DIGIT)	/* Alpha-Numeric */

  typedef unsigned char char_type_prop;
  extern const char_type_prop *char_properties_ptr;
  extern const unsigned char *char_lower_mapper_ptr;
  extern const unsigned char *char_upper_mapper_ptr;
  extern const unsigned char *iso8859_lower_mapper_ptr;
  extern const unsigned char *iso8859_upper_mapper_ptr;

#ifndef NDEBUG
#define CHECK_OVER_CODE_VALUE(c)  { if ((c) >= 256) return (c); }
#else
#define CHECK_OVER_CODE_VALUE(c)
#endif

  inline int char_isspace (int c)
  {
    CHECK_OVER_CODE_VALUE (c);
    return (char_properties_ptr[c] & CHAR_PROP_SPACE);
  }
  inline int char_isupper (int c)
  {
    CHECK_OVER_CODE_VALUE (c);
    return (char_properties_ptr[c] & CHAR_PROP_UPPER);
  }
  inline int char_tolower (int c)
  {
    CHECK_OVER_CODE_VALUE (c);
    return ((int) char_lower_mapper_ptr[c]);
  }
  inline int char_islower (int c)
  {
    CHECK_OVER_CODE_VALUE (c);
    return (char_properties_ptr[c] & CHAR_PROP_LOWER);
  }
  inline int char_isalpha (int c)
  {
    CHECK_OVER_CODE_VALUE (c);
    return (char_properties_ptr[c] & CHAR_PROP_ALPHA);
  }
  inline int char_isdigit (int c)
  {
    CHECK_OVER_CODE_VALUE (c);
    return (char_properties_ptr[c] & CHAR_PROP_DIGIT);
  }
  inline int char_isalnum (int c)
  {
    CHECK_OVER_CODE_VALUE (c);
    return (char_properties_ptr[c] & CHAR_PROP_ALPHA_NUM);
  }
  inline int char_iseol (int c)
  {
    CHECK_OVER_CODE_VALUE (c);
    return (char_properties_ptr[c] & CHAR_PROP_EOL);
  }
  inline int char_isxdigit (int c)
  {
    CHECK_OVER_CODE_VALUE (c);
    return (char_properties_ptr[c] & CHAR_PROP_HEXNUM);
  }
  inline int char_toupper (int c)
  {
    CHECK_OVER_CODE_VALUE (c);
    return ((int) char_upper_mapper_ptr[c]);
  }

  inline int char_tolower_iso8859 (int c)
  {
    CHECK_OVER_CODE_VALUE (c);
    return ((int) iso8859_lower_mapper_ptr[c]);
  }
  inline int char_toupper_iso8859 (int c)
  {
    CHECK_OVER_CODE_VALUE (c);
    return ((int) iso8859_upper_mapper_ptr[c]);
  }
  inline int char_islower_iso8859 (int c)
  {
    CHECK_OVER_CODE_VALUE (c);
    return (char_properties_ptr[c] & (CHAR_PROP_LOWER | CHAR_PROP_ISO8859_LOWER));
  }
  inline int char_isupper_iso8859 (int c)
  {
    CHECK_OVER_CODE_VALUE (c);
    return (char_properties_ptr[c] & (CHAR_PROP_UPPER | CHAR_PROP_ISO8859_UPPER));
  }

#define char_isspace2  char_isspace	// ' ', '\t', '\n', '\r
  extern char *trim (char *str);

#ifdef __cplusplus
}
#endif

#endif /* _CHARTYPE_H_ */
