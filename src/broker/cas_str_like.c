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

static int str_eval_like (const unsigned char *tar, const unsigned char *expr, unsigned char escape);
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

  ALLOC_COPY_STRLEN (low_src, src);
  ALLOC_COPY_STRLEN (low_pattern, pattern);

  if (low_src == NULL || low_pattern == NULL)
    {
      FREE_MEM (low_src);
      FREE_MEM (low_pattern);
      return B_FALSE;
    }

  ut_tolower (low_src);
  ut_tolower (low_pattern);

  result =
    str_eval_like ((const unsigned char *) low_src, (const unsigned char *) low_pattern, (unsigned char) esc_char);

  FREE_MEM (low_src);
  FREE_MEM (low_pattern);

  return result;
}


static int
str_eval_like (const unsigned char *tar, const unsigned char *expr, unsigned char escape)
{
  const int IN_CHECK = 0;
  const int IN_PERCENT = 1;
  const int IN_PERCENT_UNDERSCORE = 2;

  int status = IN_CHECK;
  const unsigned char *tarstack[STK_SIZE], *exprstack[STK_SIZE];
  int stackp = -1;
  int inescape = 0;

  if (escape == 0)
    {
      escape = 2;
    }
  while (1)
    {
      if (status == IN_CHECK)
	{
	  if (*expr == escape)
	    {
	      expr++;
	      if (*expr == '%' || *expr == '_')
		{
		  inescape = 1;
		  continue;
		}
	      else if (*tar
		       && ((!is_korean (*tar) && *tar == *expr)
			   || (is_korean (*tar) && *tar == *expr && *(tar + 1) == *(expr + 1))))
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
		  continue;
		}
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
		  if (stackp >= 0 && stackp < STK_SIZE)
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
		  if (stackp >= 0 && stackp < STK_SIZE)
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
	  else if ((*expr == '_') || (!is_korean (*tar) && *tar == *expr)
		   || (is_korean (*tar) && *tar == *expr && *(tar + 1) == *(expr + 1)))
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
	  else if (stackp >= 0 && stackp < STK_SIZE)
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
	  else if (stackp >= STK_SIZE)
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
	      if (stackp >= STK_SIZE - 1)
		{
		  return B_ERROR;
		}
	      tarstack[++stackp] = tar;
	      exprstack[stackp] = expr;
	      expr++;

	      inescape = 0;
	      status = IN_PERCENT_UNDERSCORE;
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
	      if (stackp >= STK_SIZE - 1)
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

	      inescape = 0;
	      status = IN_CHECK;
	    }
	}
      if (status == IN_PERCENT_UNDERSCORE)
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
		  if (stackp >= 0 && stackp < STK_SIZE)
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
		  if (stackp >= 0 && stackp < STK_SIZE)
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
	  else if ((*expr == '_') || (!is_korean (*tar) && *tar == *expr)
		   || (is_korean (*tar) && *tar == *expr && *(tar + 1) == *(expr + 1)))
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
	  else if (stackp >= 0 && stackp < STK_SIZE)
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
	  else if (stackp >= STK_SIZE)
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
