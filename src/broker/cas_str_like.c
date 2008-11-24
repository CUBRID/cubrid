/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 *   This program is free software; you can redistribute it and/or modify 
 *   it under the terms of the GNU General Public License as published by 
 *   the Free Software Foundation; version 2 of the License. 
 *
 *  This program is distributed in the hope that it will be useful, 
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of 
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the 
 *  GNU General Public License for more details. 
 *
 *  You should have received a copy of the GNU General Public License 
 *  along with this program; if not, write to the Free Software 
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA 
 *
 */


/*
 * cas_str_like.c - 
 */

#ident "$Id$"

/*
 * str_like
 *
 * Arguments:
 *               src: (IN) Source string.
 *           pattern: (IN) Pattern match string.
 *          esc_char: (IN) Pointer to escape character.  This pointer should
 *                         be NULL when an escape character is not used.
 *   case_sensitive : (IN) 1 - case sensitive, 0 - case insensitive
 *
 * Returns: int
 *   B_TRUE(match), B_FALSE(not match), B_ERROR(error)
 *
 * Errors:
 *
 * Description:
 *     Perform a "like" regular expression pattern match between the pattern
 *     string and the source string.  The pattern string may contain the
 *     '%' character to match 0 or more characters, or the '_' character
 *     to match exactly one character.  These special characters may be
 *     interpreted as normal characters my escaping them.  In this case the
 *     escape character is none NULL.  It is assumed that all strings are
 *     of the same codeset.
 *
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cas_common.h"
#include "cas_util.h"
#include "cas_str_like.h"

#define STK_SIZE 100
#define B_ERROR -1
#define B_TRUE	1
#define B_FALSE 0

static int str_eval_like (const unsigned char *tar, const unsigned char *expr,
			  unsigned char escape);
static int is_korean (unsigned char ch);
#if 0
static void str_tolower (char *str);
#endif

int
str_like (char *src, char *pattern, char esc_char)
{
  int result;
  char *low_src;
  char *low_pattern;

  ALLOC_COPY (low_src, src);
  ALLOC_COPY (low_pattern, pattern);

  if (low_src == NULL || low_pattern == NULL)
    return B_FALSE;

  ut_tolower (low_src);
  ut_tolower (low_pattern);

  result = str_eval_like ((const unsigned char *) low_src,
			  (const unsigned char *) low_pattern,
			  (unsigned char) esc_char);

  FREE_MEM (low_src);
  FREE_MEM (low_pattern);

  return result;
}


static int
str_eval_like (const unsigned char *tar,
	       const unsigned char *expr, unsigned char escape)
{
  const int IN_CHECK = 0;
  const int IN_PERCENT = 1;
  const int IN_PERCENT_UNDERLINE = 2;

  int status = IN_CHECK;
  const unsigned char *tarstack[STK_SIZE], *exprstack[STK_SIZE];
  int stackp = -1;
  int inescape = 0;

  if (escape == 0)
    escape = 2;
  while (1)
    {
      if (status == IN_CHECK)
	{
	  if (*expr == escape)
	    {
	      expr++;
	      inescape = 1;
	      if (*expr != '%' && *expr != '_')
		{
		  return B_ERROR;
		}
	      continue;
	    }

	  if (inescape)
	    {
	      if (*tar == *expr)
		{
		  tar++;
		  expr++;
		}
	      else
		{
		  if (stackp >= 0 && stackp <= STK_SIZE)
		    {
		      tar = tarstack[stackp];
		      if (is_korean (*tar))
			{
			  tar += 2;
			}
		      else
			{
			  tar++;
			}
		      expr = exprstack[stackp--];
		    }
		  else
		    {
		      return B_FALSE;
		    }
		}
	      inescape = 0;
	      continue;
	    }

	  /* goto check */
	  if (*expr == 0)
	    {
	      while (*tar == ' ')
		{
		  tar++;
		}

	      if (*tar == 0)
		{
		  return B_TRUE;
		}
	      else
		{
		  if (stackp >= 0 && stackp <= STK_SIZE)
		    {
		      tar = tarstack[stackp];
		      if (is_korean (*tar))
			{
			  tar += 2;
			}
		      else
			{
			  tar++;
			}
		      expr = exprstack[stackp--];
		    }
		  else
		    {
		      return B_FALSE;
		    }
		}
	    }
	  else if (*expr == '%')
	    {
	      status = IN_PERCENT;
	      while (*(expr + 1) == '%')
		{
		  expr++;
		}
	    }
	  else if ((*expr == '_')
		   || (!is_korean (*tar) && *tar == *expr)
		   || (is_korean (*tar) &&
		       *tar == *expr && *(tar + 1) == *(expr + 1)))
	    {
	      if (is_korean (*tar))
		{
		  tar += 2;
		}
	      else
		{
		  tar++;
		}
	      if (is_korean (*expr))
		{
		  expr += 2;
		}
	      else
		{
		  expr++;
		}
	    }
	  else if (stackp >= 0 && stackp <= STK_SIZE)
	    {
	      tar = tarstack[stackp];
	      if (is_korean (*tar))
		{
		  tar += 2;
		}
	      else
		{
		  tar++;
		}

	      expr = exprstack[stackp--];
	    }
	  else if (stackp > STK_SIZE)
	    {
	      return B_ERROR;
	    }
	  else
	    {
	      return B_FALSE;
	    }
	}
      else if (status == IN_PERCENT)
	{
	  if (*(expr + 1) == '_')
	    {
	      if (stackp >= STK_SIZE)
		{
		  return B_ERROR;
		}
	      tarstack[++stackp] = tar;
	      exprstack[stackp] = expr;
	      expr++;

	      if (stackp > STK_SIZE)
		{
		  return B_ERROR;
		}
	      inescape = 0;
	      status = IN_PERCENT_UNDERLINE;
	      continue;
	    }

	  if (*(expr + 1) == escape)
	    {
	      expr++;
	      inescape = 1;
	      if (*(expr + 1) != '%' && *(expr + 1) != '_')
		{
		  return B_ERROR;
		}
	    }

	  while (*tar && *tar != *(expr + 1))
	    {
	      if (is_korean (*tar))
		{
		  tar += 2;
		}
	      else
		{
		  tar++;
		}
	    }

	  if (*tar == *(expr + 1))
	    {
	      if (stackp >= STK_SIZE)
		{
		  return B_ERROR;
		}
	      tarstack[++stackp] = tar;
	      exprstack[stackp] = expr;
	      if (is_korean (*expr))
		{
		  expr += 2;
		}
	      else
		{
		  expr++;
		}

	      if (stackp > STK_SIZE)
		{
		  return B_ERROR;
		}
	      inescape = 0;
	      status = IN_CHECK;
	    }
	}
      if (status == IN_PERCENT_UNDERLINE)
	{
	  if (*expr == escape)
	    {
	      expr++;
	      inescape = 1;
	      if (*expr != '%' && *expr != '_')
		{
		  return B_ERROR;
		}
	      continue;
	    }

	  if (inescape)
	    {
	      if (*tar == *expr)
		{
		  tar++;
		  expr++;
		}
	      else
		{
		  if (stackp >= 0 && stackp <= STK_SIZE)
		    {
		      tar = tarstack[stackp];
		      if (is_korean (*tar))
			{
			  tar += 2;
			}
		      else
			{
			  tar++;
			}
		      expr = exprstack[stackp--];
		    }
		  else
		    {
		      return B_FALSE;
		    }
		}
	      inescape = 0;
	      continue;
	    }

	  /* goto check */
	  if (*expr == 0)
	    {
	      while (*tar == ' ')
		{
		  tar++;
		}

	      if (*tar == 0)
		{
		  return B_TRUE;
		}
	      else
		{
		  if (stackp >= 0 && stackp <= STK_SIZE)
		    {
		      tar = tarstack[stackp];
		      if (is_korean (*tar))
			{
			  tar += 2;
			}
		      else
			{
			  tar++;
			}
		      expr = exprstack[stackp--];
		    }
		  else
		    {
		      return B_FALSE;
		    }
		}
	    }
	  else if (*expr == '%')
	    {
	      status = IN_PERCENT;
	      while (*(expr + 1) == '%')
		{
		  expr++;
		}
	    }
	  else if ((*expr == '_')
		   || (!is_korean (*tar) && *tar == *expr)
		   || (is_korean (*tar) &&
		       *tar == *expr && *(tar + 1) == *(expr + 1)))
	    {
	      if (is_korean (*tar))
		{
		  tar += 2;
		}
	      else
		{
		  tar++;
		}
	      if (is_korean (*expr))
		{
		  expr += 2;
		}
	      else
		{
		  expr++;
		}
	    }
	  else if (stackp >= 0 && stackp <= STK_SIZE)
	    {
	      tar = tarstack[stackp];
	      if (is_korean (*tar))
		{
		  tar += 2;
		}
	      else
		{
		  tar++;
		}

	      expr = exprstack[stackp--];
	    }
	  else if (stackp > STK_SIZE)
	    {
	      return B_ERROR;
	    }
	  else
	    {
	      return B_FALSE;
	    }
	}

      if (*tar == 0)
	{
	  if (*expr)
	    {
	      while (*expr == '%')
		{
		  expr++;
		}
	    }

	  if (*expr == 0)
	    {
	      return B_TRUE;
	    }
	  else
	    {
	      return B_FALSE;
	    }
	}
    }
}

static int
is_korean (unsigned char ch)
{
  return (ch >= 0xb0 && ch <= 0xc8) || (ch >= 0xa1 && ch <= 0xfe);
}

#if 0
static void
str_tolower (char *str)
{
  char *p;
  for (p = str; *p; p++)
    {
      if (*p >= 'A' && *p <= 'Z')
	*p = *p - 'A' + 'a';
    }
}
#endif
