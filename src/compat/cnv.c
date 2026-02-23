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
 * cnv.c - String conversion functions
 */

#ident "$Id$"

#include "config.h"

#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <limits.h>
#include <float.h>
#include <math.h>
#include <time.h>
#include <stdarg.h>
#include <wchar.h>

#include "porting.h"
#include "dbtype.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#if defined (SUPPRESS_STRLEN_WARNING)
#define strlen(s1)  ((int) strlen(s1))
#endif /* defined (SUPPRESS_STRLEN_WARNING) */

#define BITS_IN_BYTE		8
#define HEX_IN_BYTE		2
#define BITS_IN_HEX		4

/*
 * 2**3.1 ~ 10.  Thus a string with one decimal value per byte will be (8/3.1)
 * times longer than a string with 8 binary places per byte.
 * A 'decimal string' needs to be 3 times longer than a raw numeric string
 * plus a sign and a NULL termination.
 */
#define BYTE_COUNT(bit_cnt)	(((bit_cnt)+BITS_IN_BYTE-1)/BITS_IN_BYTE)
#define BYTE_COUNT_HEX(bit_cnt)	(((bit_cnt)+BITS_IN_HEX-1)/BITS_IN_HEX)


typedef enum bit_string_format_e
{
  BIT_STRING_BINARY = 0,
  BIT_STRING_HEX = 1
} BIT_STRING_FORMAT;

/*
 * bfmt_print() - Change the given string to a representation of the given bit
 *    string value in the given format. if this is not long enough to contain
 *    the new string, then an error is returned.
 * return:
 * bfmt(in) :
 * the_db_bit(in) :
 * string(out) :
 * max_size(in) : the maximum number of chars that can be stored in
 *   the string (including final '\0' char)
 */
static int
bfmt_print (BIT_STRING_FORMAT * bfmt, const DB_VALUE * the_db_bit, char *string, int max_size)
{
  int length = 0;
  int string_index = 0;
  int byte_index;
  int bit_index;
  const char *bstring;
  int error = NO_ERROR;
  static const char digits[16] = {
    '0', '1', '2', '3', '4', '5', '6', '7',
    '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
  };

  /* Get the buffer and the length from the_db_bit */
  bstring = db_get_bit (the_db_bit, &length);

  switch (*bfmt)
    {
    case BIT_STRING_BINARY:
      if (length + 1 > max_size)
	{
	  error = ER_FAILED;
	}
      else
	{
	  for (byte_index = 0; byte_index < BYTE_COUNT (length); byte_index++)
	    {
	      for (bit_index = 7; bit_index >= 0 && string_index < length; bit_index--)
		{
		  *string = digits[((bstring[byte_index] >> bit_index) & 0x1)];
		  string++;
		  string_index++;
		}
	    }
	  *string = '\0';
	}
      break;

    case BIT_STRING_HEX:
      if (BYTE_COUNT_HEX (length) + 1 > max_size)
	{
	  error = ER_FAILED;
	}
      else
	{
	  for (byte_index = 0; byte_index < BYTE_COUNT (length); byte_index++)
	    {
	      *string = digits[((bstring[byte_index] >> BITS_IN_HEX) & 0x0f)];
	      string++;
	      string_index++;
	      if (string_index < BYTE_COUNT_HEX (length))
		{
		  *string = digits[((bstring[byte_index] & 0x0f))];
		  string++;
		  string_index++;
		}
	    }
	  *string = '\0';
	}
      break;

    default:
      assert (!"possible to get here");
      break;
    }

  return error;
}

/*
 * db_bit_string() - Change the given string to a representation of
 *    the given bit value in the given format. If an error occurs, then
 *    the contents of the string are undefined and an error condition is
 *    returned.
 *    if max_size is not long enough to contain the new float string, then an
 *    error is returned.
 * return:
 * the_db_bit(in) :
 * bit_format(in) :
 * string(out) :
 * max_size(in) : the maximum number of chars that can be stored in
 *   the string (including final '\0' char)
 */
int
db_bit_string (const DB_VALUE * the_db_bit, const char *bit_format, char *string, int max_size)
{
  BIT_STRING_FORMAT bfmt;
  int r;

  assert (string != NULL);
  assert (max_size > 0);

  bfmt = BIT_STRING_BINARY;
  if (bit_format && *bit_format)
    {
      char *p = (char *) bit_format;
      while (*p == ' ' || *p == '\t')
	{
	  p++;
	}

      if (p[0] == '%' && (p[2] == '\0' || p[2] == ' ' || p[2] == '\t'))
	{
	  p++;
	  if (*p == 'X' || *p == 'H' || *p == 'x' || *p == 'h')
	    {
	      bfmt = BIT_STRING_HEX;
	    }
	}
    }

  r = bfmt_print (&bfmt, the_db_bit, string, max_size);

  return r;
}
