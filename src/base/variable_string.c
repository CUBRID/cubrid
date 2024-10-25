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
 * variable_string.c : Flexible strings that allow unlimited appending,
 *                     prepending, etc.
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "porting.h"
#include "variable_string.h"
#include "error_code.h"
#include "error_manager.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#define FUDGE		16
#define PREFIX_CUSHION	 8
#define VS_INC		32

static const char *EMPTY_STRING = "";

static int vs_cannot_hold (varstring * vstr, int n);
static int vs_grow (varstring * vstr, int n);
static int vs_do_sprintf (varstring * vstr, const char *fmt, va_list args);
static int vs_itoa (varstring * vstr, int n);

/*
 * vs_cannot_hold() - Check vstr can hold or not
 *   return: true if cannot hold, false if can hold
 *   vstr(in/out): the varstring to be grown
 *   n(in): the size
 */
static int
vs_cannot_hold (varstring * vstr, int n)
{
  return ((vstr->base == NULL) || (vstr->limit - vstr->end) < n);
}

/*
 * vs_grow() - Enlarge the buffer area of the given varstring by the
 *             given amount.
 *   return: -1 if failure, 0 if success
 *   vstr(in/out): the varstring to be grown
 *   n(in): the size to grow it by.
 */
static int
vs_grow (varstring * vstr, int n)
{
  if (vstr == NULL)
    {
      return ER_FAILED;
    }

  if (vstr->base)
    {
      int size = CAST_STRLEN (vstr->limit - vstr->base);
      int length = CAST_STRLEN (vstr->end - vstr->start);
      int offset = CAST_STRLEN (vstr->start - vstr->base);
      char *new_buf = (char *) malloc (sizeof (char) * (size + n));
      if (new_buf == NULL)
	{
	  return ER_FAILED;
	}

      /*
       * Don't use strcpy here; vs_grow() can be invoked in the middle
       * of string processing while the string isn't properly
       * null-terminated.
       */
      memcpy (new_buf, vstr->base, size);
      free (vstr->base);

      vstr->base = new_buf;
      vstr->limit = new_buf + size + n;
      vstr->start = new_buf + offset;
      vstr->end = vstr->start + length;
    }
  else
    {
      char *new_buf = (char *) malloc (sizeof (char) * n);

      if (new_buf == NULL)
	{
	  return ER_FAILED;
	}

      vstr->base = new_buf;
      vstr->limit = new_buf + n;
      vstr->start = new_buf + (n > PREFIX_CUSHION ? PREFIX_CUSHION : 0);
      vstr->end = vstr->start;
      *vstr->start = '\0';
    }

  return NO_ERROR;
}

/*
 * vs_do_sprintf() - Format the arguments into vstr according to fmt
 *   return: -1 if failure, 0 if success
 *   vstr(in/out): pointer to a varstring.
 *   fmt(in) : printf-style format string.
 *   args(in): stdarg-style package of arguments to be formatted.
 *
 * Note: Only understands %s and %d right now.
 */
static int
vs_do_sprintf (varstring * vstr, const char *fmt, va_list args)
{
  int c;
  char *p, *limit;

  if (vstr->base == NULL && vs_grow (vstr, VS_INC))
    {
      return ER_FAILED;
    }

  while (1)
    {
    restart:
      for (p = vstr->end, limit = vstr->limit; p < limit;)
	{
	  switch (c = *fmt++)
	    {
	    case '%':
	      switch (c = *fmt++)
		{
		case '%':
		  *p++ = '%';
		  continue;

		case 's':
		  vstr->end = p;
		  if (vs_strcat (vstr, va_arg (args, char *)))
		    {
		      return 1;
		    }
		  goto restart;

		case 'd':
		  vstr->end = p;
		  if (vs_itoa (vstr, va_arg (args, int)))
		    {
		      return 1;
		    }
		  goto restart;

		default:
		  break;
		}
	      /* Fall through */

	    case '\0':
	      *p = '\0';
	      vstr->end = p;
	      return NO_ERROR;

	    default:
	      *p++ = (char) c;
	      continue;
	    }
	}

      vstr->end = p;
      if (vs_grow (vstr, VS_INC))
	{
	  return ER_FAILED;
	}
    }

  return NO_ERROR;
}

/*
 * vs_itoa() -  Append the text representation of the given integer
 *   return: -1 if failure, 0 if success
 *   vstr(in/out)
 *   n(in)
 *
 * Note:
 */
static int
vs_itoa (varstring * vstr, int n)
{
  char buf[32];
  sprintf (buf, "%d", n);
  return vs_strcat (vstr, buf);
}

/*
 * vs_new() - Allocate (if necessary) and initialize a varstring.
 *   return: pointer to the vstr if success, NULL if failure
 *   vstr(in): pointer of varstring to be initialized, possibly NULL.
 */
varstring *
vs_new (varstring * vstr)
{
  if (vstr == NULL)
    {
      vstr = (varstring *) malloc (sizeof (varstring));
      if (vstr == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (varstring));
	  return NULL;
	}

      vstr->heap_allocated = 1;
    }
  else
    {
      vstr->heap_allocated = 0;
    }

  vstr->base = NULL;
  vstr->limit = NULL;
  vstr->start = NULL;
  vstr->end = NULL;

  return vstr;
}

/*
 * vs_free() - free varstring if it was heap allocated.
 *   return: none
 *   vstr(in/out)
 */
void
vs_free (varstring * vstr)
{
  if (vstr == NULL)
    {
      return;
    }

  if (vstr->base)
    {
      free (vstr->base);
      vstr->base = NULL;
    }

  if (vstr->heap_allocated)
    {
      free (vstr);
    }
}

/*
 * vs_clear() - Reset the buffer pointers in varstring.
 *   return: none
 *   vstr(in/out)
 */
void
vs_clear (varstring * vstr)
{
  if (vstr == NULL || vstr->base == NULL)
    {
      return;
    }

  vstr->start = vstr->base + (vstr->limit - vstr->base < PREFIX_CUSHION ? 0 : PREFIX_CUSHION);

  vstr->end = vstr->start;
  *vstr->start = '\0';
}


/*
 * vs_append() - synonym for vs_strcat().
 *   return: -1 if failure, 0 if success.
 *   vstr(in/out)
 *   suffix(in)
 */
int
vs_append (varstring * vstr, const char *suffix)
{
  return vs_strcat (vstr, suffix);
}

/*
 * vs_prepend() - Prefix a string onto the varstring.
 *   return: -1 if failure, 0 if success
 *   vstr(in/out)
 *   prefix(in)
 *
 * Note:
 */
int
vs_prepend (varstring * vstr, const char *prefix)
{
  int n, available;

  if (vstr == NULL)
    {
      return ER_FAILED;
    }

  if (prefix == NULL)
    {
      return NO_ERROR;
    }

  n = (int) strlen (prefix);
  if (n == 0)
    {
      return NO_ERROR;
    }

  if (vstr->base == NULL && vs_grow (vstr, n + FUDGE))
    {
      return ER_FAILED;
    }

  available = CAST_STRLEN (vstr->start - vstr->base);
  if (available < n)
    {
      /*
       * Make room at the front of the string for the prefix.  If there
       * is enough slop at the end, shift the current string toward the
       * end without growing the string; if not, grow it and then do
       * the shift.
       */
      char *new_start;
      int length;

      if (vs_cannot_hold (vstr, PREFIX_CUSHION + (n - available)) && vs_grow (vstr, n + PREFIX_CUSHION + FUDGE))
	{
	  return ER_FAILED;
	}

      length = CAST_STRLEN (vstr->end - vstr->start);
      new_start = vstr->base + n + PREFIX_CUSHION;

      memmove (new_start, vstr->start, length);

      vstr->end = new_start + length;
      vstr->start = new_start;
      *vstr->end = '\0';
    }

  vstr->start -= n;
  memcpy (vstr->start, prefix, n);

  return NO_ERROR;
}

/*
 * vs_sprintf() - Perform a sprintf-style formatting into vstr
 *   return: -1 if failure, 0 if success.
 *   vstr(in/out)
 *   fmt(in)
 *
 * Note: only the %s and %d codes are supported.
 */
int
vs_sprintf (varstring * vstr, const char *fmt, ...)
{
  int status;
  va_list args;

  if (vstr == NULL || fmt == NULL)
    {
      return ER_FAILED;
    }

  va_start (args, fmt);
  status = vs_do_sprintf (vstr, fmt, args);
  va_end (args);

  return status;
}

/*
 * vs_strcat() - Concatenate string onto the buffer in varstring
 *   return: -1 if failure, 0 if success.
 *   vstr(in/out)
 *   str(in)
 */
int
vs_strcat (varstring * vstr, const char *str)
{
  int n;

  if (vstr == NULL)
    {
      return ER_FAILED;
    }

  if (str == NULL || (n = (int) strlen (str)) == 0)
    {
      return NO_ERROR;
    }

  if (vs_cannot_hold (vstr, n) && vs_grow (vstr, n + FUDGE))
    {
      return ER_FAILED;
    }

  memcpy (vstr->end, str, n);
  vstr->end += n;

  return NO_ERROR;

}

/*
 * vs_strcatn() - Concatenate str onto the buffer in varstring for
 *                a given length
 *   return: -1 if failure, 0 if success.
 *   vstr(in/out)
 *   str(in)
 *   length(in)
 */
int
vs_strcatn (varstring * vstr, const char *str, int length)
{
  if (vstr == NULL)
    {
      return ER_FAILED;
    }

  if (str == NULL || length == 0)
    {
      return NO_ERROR;
    }

  if (vs_cannot_hold (vstr, length) && vs_grow (vstr, length + FUDGE))
    {
      return ER_FAILED;
    }

  memcpy (vstr->end, str, length);
  vstr->end += length;

  return NO_ERROR;
}

/*
 * vs_strcpy() - Initialize varstring with the contents of string.
 *   return: -1 if failure, 0 if success.
 *   vstr(in/out)
 *   str(in)
 */
int
vs_strcpy (varstring * vstr, const char *str)
{
  vs_clear (vstr);
  return vs_strcat (vstr, str);
}

/*
 * vs_putc() - Put a single character in varstring
 *   return: -1 if failure, 0 if success.
 *   vstr(in/out)
 *   ch(in)
 */
int
vs_putc (varstring * vstr, int ch)
{
  if (vstr == NULL)
    {
      return ER_FAILED;
    }

  if (vs_cannot_hold (vstr, 1) && vs_grow (vstr, FUDGE))
    {
      return ER_FAILED;
    }

  *vstr->end++ = (char) ch;

  return NO_ERROR;

}

/*
 * vs_str() - Return the prepared character string within varstring.
 *   return: the start pointer of varstring
 *   vstr(in/out)
 */
char *
vs_str (varstring * vstr)
{
  if (vstr == NULL || vstr->base == NULL)
    {
      return (char *) EMPTY_STRING;
    }
  else
    {
      /*
       * Make sure it's null-terminated by emitting a null character
       * and then backing up the end pointer.
       */
      if (vs_cannot_hold (vstr, 1) && vs_grow (vstr, FUDGE))
	{
	  return NULL;
	}

      *vstr->end = '\0';
      return vstr->start;
    }
}

/*
 * vs_strlen() - Return the length of the string managed by varstring.
 *   return: length of the varstring
 *   vstr(in)
 */
int
vs_strlen (const varstring * vstr)
{
  if (vstr == NULL || vstr->base == NULL)
    {
      return 0;
    }

  return CAST_STRLEN (vstr->end - vstr->start);
}
