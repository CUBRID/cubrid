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
 * password_log.c -
 */

#ident "$Id$"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#if defined(WINDOWS)
#include <sys/timeb.h>
#include <process.h>
#include <io.h>
#else
#include <unistd.h>
#include <sys/time.h>
#include <sys/timeb.h>
#include <signal.h>
#endif
#include <assert.h>

#include "porting.h"
#include "cas_common.h"
#include "password_log.h"
//#include "parse_tree.h"
//#include "system_parameter.h"
//#include "environment_variable.h"
//#include "broker_config.h"
//#include "util_func.h"

//extern PARSER_CONTEXT *this_parser;
//==========================================================================

static unsigned char isSpace[0x100] =
{
  // ' '  '\t'  '\r'  '\n'  '\f'  '\v'
  // 0x20 0x09  0x0d  0x0a  0x0c  0x0b
  0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

#define SKIP_SPACE_CHARACTERS(p)                \
  do {                                          \
        while (isSpace[(unsigned char) *p])     \
	    {                                   \
	      p++;                              \
	    }                                   \
   } while(0)

#define SET_PWD_LENGTH_N_COMMA(s, e, add_comma)  ((add_comma) ? ((0x01 << 31) | ((e) - (s))) : ((e) - (s)))
#define GET_END_PWD_OFFSET(offset_ptr)  ((offset_ptr)[0] + ((offset_ptr)[1] & 0x7FFFFFFF))
#define IS_PWD_NEED_COMMA(offset_ptr)  ((offset_ptr)[1] >> 31)


class CHidePassword
{
  private:
    char *skip_comment_string (char *query);
    char *get_quot_string (char *ps);
    char *get_token (char *&in, int &len);
    char *get_method_passowrd_start_position (char *ps, char *method_name, int *method_password_len);
    char *get_passowrd_start_position (char *query, bool *is_server);
    char *get_passowrd_end_position (char *ps, bool is_server);
    char *skip_one_query (char *query);
    bool check_lead_string_in_query (char **query, char **method_name);

    int fprintf_replace_newline (FILE *fp, char *query, int (*cas_fprintf) (FILE *, const char *, ...));


  public:
    CHidePassword ()
    {
    };
    ~CHidePassword ()
    {
    };

    int find_password_positions (int **pplist, char *query);
    int snprint_password (char *msg, int size, char *query, int *offset_ptr);
    void fprintf_password (FILE *fp, char *query, int *offset_ptr, int (*cas_fprintf) (FILE *, const char *, ...));
};

char *
CHidePassword::skip_comment_string (char *query)
{
  query += 2;
  if (query[-1] != '*')
    {
      // case) -- or //
      while (*query)
	{
	  if (*query == '\n')
	    {
	      query++;
	      break;
	    }
	  query++;
	}
    }
  else
    {
      // case)  /*
      while (*query)
	{
	  if (*query == '*' && query[1] == '/')
	    {
	      query += 2;
	      break;
	    }
	  query++;
	}
    }
  return query;
}

char *
CHidePassword::get_quot_string (char *ps)
{
  char quot_char = *ps;

  for (ps++; *ps; ps++)
    {
      if (*ps == quot_char)
	{
	  ps++;
	  return ps;
	}
    }
  return ps;
}

char *
CHidePassword::get_token (char *&in, int &len)
{
  char *ps, check_char;
  int quoted_single = 0;
  int quoted_double = 0;

  SKIP_SPACE_CHARACTERS (in);
  ps = in;
  if (*in == '\'' || *in == '"')
    {
#if 1
      in = get_quot_string (in);
#else
      check_char = *in;
      in++;
      while (*in && *in != check_char)
	{
	  in++;
	}
      if (*in)
	{
	  in++;
	}
#endif
      len = (int) (in - ps);
      return ps;
    }

  while (*in)
    {
      switch (*in)
	{
	case '=':
	case ',':
	case '(':
	case ')':
	case ';':
	  if (ps == in)
	    {
	      in++;
	    }
	  len = (int) (in - ps);
	  return ps;

	case '-':
	case '/':
	  if (in[0] == in[1])
	    {
	      in = skip_comment_string (in);
	    }
	  else if (in[0] == '/' && in[1] == '*')
	    {
	      in = skip_comment_string (in);
	    }
	  break;

	default:
	  if (isSpace[ (unsigned char) *in])
	    {
	      len = (int) (in - ps);
	      return ps;
	    }
	  break;
	}

      in++;
    }

  if (in == ps)
    {
      return NULL;
    }

  len = (int) (in - ps);
  return ps;
}

char *
CHidePassword::get_method_passowrd_start_position (char *ps, char *method_name, int *method_password_len)
{
  /*
     set_password('password_string')
     add_user ('user_name', 'password_string')
     add_user ('user_name')
     login ('user_name')
     login ('user_name', 'password')
   */
  int len;
  char *token;

  *method_password_len = -1;
  token = get_token (ps, len);
  if (len != 1 || *token != '(')
    {
      return NULL;
    }

  if (strcmp (method_name, "set_password") == 0)
    {
      token = get_token (ps, len);	// read user_name
      if (len >= 2 && (*token == '\'' || *token == '"'))
	{
	  *method_password_len = len;
	  return token;
	}
    }
  else
    {
      token = get_token (ps, len);	// read user_name
      if (len < 2 || (*token != '\'' && *token != '"'))
	{
	  return NULL;
	}

      token = get_token (ps, len);
      if (len == 1)
	{
	  if (*token == ')')
	    {
	      *method_password_len = 0;
	      return token;
	    }
	  else if (*token == ',')
	    {
	      token = get_token (ps, len);	// read password
	      if (len >= 2 && (*token == '\'' || *token == '"'))
		{
		  *method_password_len = len;
		  return token;
		}
	    }
	}
    }

  return NULL;
}

char *
CHidePassword::get_passowrd_start_position (char *query, bool *is_server)
{
  char *ps = query;
  char *token;
  int len;

  *is_server = false;
  while (*ps)
    {
#if 0
      if ((ps = stristr (ps, "password")) == NULL)
	{
	  return NULL;
	}
      ps += 8;			/* strlen("password") */
#else
      token = get_token (ps, len);
      if (len != 8 || strncasecmp (token, "password", 8) != 0)
	{
	  continue;
	}
#endif

      SKIP_SPACE_CHARACTERS (ps);
      if (*ps == '=')
	{
	  /* case)
	   *    PASSWORD = 'string'   PASSWORD = ''    PASSWORD = )
	   *    PASSWORD = ,          PASSWORD = ;     PASSWORD = \0
	   */
	  ps++;
	  SKIP_SPACE_CHARACTERS (ps);
	  if (*ps == '\'' || *ps == '"' || *ps == ')' || *ps == ',' || *ps == ';' || *ps == '\0')
	    {
	      *is_server = true;
	      return ps;
	    }
	}
      else if (*ps == '\'' || *ps == '"')
	{
	  return ps;
	}
      else if (*ps == '_' && isSpace[ (unsigned char) ps[-1]])	// check "password_???"
	{
	  if (strncasecmp (ps, "_utf8'", 6) == 0)
	    {
	      return (ps + 5);
	    }
	  else if (strncasecmp (ps, "_euckr'", 7) == 0)
	    {
	      return (ps + 6);
	    }
	  else if (strncasecmp (ps, "_iso88591'", 10) == 0)
	    {
	      return (ps + 9);
	    }
	  else if (strncasecmp (ps, "_binary'", 8) == 0)
	    {
	      return (ps + 7);
	    }
	}
    }

  return ps;
}

char *
CHidePassword::get_passowrd_end_position (char *ps, bool is_server)
{
  // ps is the start delimiter('\'' or '"').
  unsigned char *ret_ptr = NULL;
  unsigned char *tp = (unsigned char *) ps + 1;
  unsigned char delimiter = * (unsigned char *) ps;
  // PASSWORD _utf8'abc' 'd''ef' 'gj000' <=> PASSWORD 'abcd'efgj00'
  // PASSWORD = 'abcd'

  if (*tp == delimiter)
    {
      /* case) ''      ''''      '''s' */
      tp++;
      if (*tp == delimiter)
	{
	  /* case)  ''''      '''s' */
	  tp++;
	}
      else
	{
	  ret_ptr = tp;
	}
    }

  if (ret_ptr == NULL)
    {
      for (; *tp; tp++)
	{
	  // TODO: escape
	  if (*tp == delimiter)
	    {
	      tp++;
	      if (*tp != delimiter)
		{
		  ret_ptr = tp;
		  break;
		}
	    }
	}
    }

  if (ret_ptr && !is_server)
    {
      SKIP_SPACE_CHARACTERS (tp);
      if (*tp == delimiter)
	{
	  ret_ptr = (unsigned char *) get_passowrd_end_position ((char *) tp, false);
	}
    }

  return (ret_ptr) ? (char *) ret_ptr : (char *) tp;
}

char *
CHidePassword::skip_one_query (char *query)
{
  char *ps;

  for (ps = query; *ps; ps++)
    {
      if (*ps == '\'' || *ps == '"')
	{
#if 1
	  ps = get_quot_string (ps);
#else
	  char delimiter = *ps;
	  for (ps++; *ps; ps++)
	    {
	      if (*ps == delimiter)
		{
		  break;
		}
	    }
#endif

	  if (*ps == '\0')
	    {
	      break;
	    }
	}
      else if (*ps == '-')
	{
	  if (ps[1] == '-')
	    {
	      ps = skip_comment_string (ps);
	    }
	}
      else if (*ps == '/')
	{
	  if (ps[1] == '/' || ps[1] == '*')
	    {
	      ps = skip_comment_string (ps);
	    }
	}
      else if (*ps == ';')
	{
	  ps++;
	  break;
	}
    }

  return ps;
}

bool CHidePassword::check_lead_string_in_query (char **query, char **method_name)
{
  char *
  ps;
  const char *
  first_cmd_str[] = { "call", "create", "alter", NULL };
  int
  first_cmd_len[] = { 4, 6, 5, -1 };
  const char *
  second_cmd_str[] = { "server", "user", NULL };
  int
  second_cmd_len[] = { 6, 4, -1 };
  static const char *
  method_name_str[] = { "add_user", "set_password", "login", NULL };
  int
  method_name_len[] = { 8, 12, 5, -1 };
  int *
  len_ptr = NULL;
  const char **
  str_ptr = NULL;
  bool
  is_call_stmt = false;


  char *
  token;
  int
  len;

  token = get_token (*query, len);
  if (!token)
    {
      return false;
    }

  len_ptr = first_cmd_len;
  str_ptr = first_cmd_str;
  for ( /* empty */ ; *str_ptr; str_ptr++, len_ptr++)
    {
      if (*len_ptr == len && strncasecmp (token, *str_ptr, *len_ptr) == 0)
	{
	  is_call_stmt = (str_ptr == first_cmd_str) ? true : false;
	  break;
	}
    }

  if (*str_ptr == NULL || (*query)[0] == '\0')
    {
      return false;
    }

  token = get_token (*query, len);
  if (token)
    {
      if (is_call_stmt)
	{
	  len_ptr = method_name_len;
	  str_ptr = method_name_str;
	}
      else
	{
	  len_ptr = second_cmd_len;
	  str_ptr = second_cmd_str;
	}

      for ( /* empty */ ; *str_ptr; str_ptr++, len_ptr++)
	{
	  if (*len_ptr == len && strncasecmp (token, *str_ptr, *len_ptr) == 0)
	    {
	      *method_name = (char *) (is_call_stmt ? *str_ptr : NULL);
	      return true;
	    }
	}
    }

  return false;
}

int
CHidePassword::find_password_positions (int **fixed_pwd_offset_ptr, char *query)
{
  int start, end, is_add_comma;
  int alloc_szie, used_size;
  int *offset_ptr;
  int *pwd_offset_ptr;
  char *newptr = query;

  assert (fixed_pwd_offset_ptr != NULL);
  assert (*fixed_pwd_offset_ptr != NULL);

  pwd_offset_ptr = *fixed_pwd_offset_ptr;
  alloc_szie = pwd_offset_ptr[0];
  used_size = pwd_offset_ptr[1];
  offset_ptr = pwd_offset_ptr;

  while (*newptr)
    {
      char *tp, *ps;
      bool is_server;
      int password_len;
      char *method_name = NULL;

      if (check_lead_string_in_query (&newptr, &method_name))
	{
	  if (method_name)
	    {
	      if ((ps = get_method_passowrd_start_position (newptr, method_name, &password_len)) == NULL)
		{
		  break;
		}

	      start = (int) (ps - query);
	      end = (int) (ps - query) + password_len;
	      is_add_comma = ((password_len > 0) ? 0 : 1);
	      newptr = ps + password_len;
	    }
	  else
	    {
	      if ((ps = get_passowrd_start_position (newptr, &is_server)) == NULL)
		{
		  break;
		}

	      start = (int) (ps - query);
	      if (*ps == '\'' || *ps == '"')
		{
		  tp = get_passowrd_end_position (ps, is_server);
		  end = (int) (tp - query);
		  newptr = (*tp == *ps) ? (tp + 1) : tp;
		}
	      else
		{
		  end = (int) (ps - query);
		  newptr = ps;
		}
	      is_add_comma = 0;	// no add comma
	    }

	  if (alloc_szie < (used_size + 2))
	    {
	      int new_size = (int) (alloc_szie * 1.5);

	      offset_ptr = (int *) malloc (new_size * sizeof (int));
	      if (!offset_ptr)
		{
		  assert (0);
		  return -1;
		}

	      memcpy (offset_ptr, pwd_offset_ptr, used_size * sizeof (int));
	      if (pwd_offset_ptr != *fixed_pwd_offset_ptr)
		{
		  free (pwd_offset_ptr);
		}

	      offset_ptr[0] = new_size;
	      pwd_offset_ptr = offset_ptr;
	    }

	  offset_ptr[used_size++] = start;
	  offset_ptr[used_size++] = SET_PWD_LENGTH_N_COMMA (start, end, is_add_comma);
	  offset_ptr[1] = used_size;
	}

      newptr = skip_one_query (newptr);
    }

  *fixed_pwd_offset_ptr = pwd_offset_ptr;
  return 0;
}

int
CHidePassword::snprint_password (char *msg, int size, char *query, int *offset_ptr)
{
  char *qryptr = query;
  char chbk;
  int pos;
  int length = 0;
  // offset_ptr[0] : size of offset_ptr
  // offset_ptr[1] : used count
  for (int x = 2; x < offset_ptr[1]; x += 2)
    {
      pos = offset_ptr[x];
      chbk = query[pos];
      query[pos] = '\0';
      length += snprintf (msg + length, size - length, "%s", qryptr);
      query[pos] = chbk;

      length +=
	      snprintf (msg + length, size - length, "%s", (IS_PWD_NEED_COMMA (offset_ptr + x) ? ", '****'" : "'****'"));
      qryptr = query + GET_END_PWD_OFFSET (offset_ptr + x);
    }

  if (*qryptr)
    {
      length += snprintf (msg + length, size - length, "%s", qryptr);
    }

  if (length < (size - 1))
    {
      msg[length++] = '\n';
    }

  return length;
}

int
CHidePassword::fprintf_replace_newline (FILE *fp, char *query, int (*cas_fprintf) (FILE *, const char *, ...))
{
  int offset;
  char chbk;

  while (*query)
    {
      offset = strcspn (query, "\r\n");
      if (offset <= 0)
	{
	  cas_fprintf (fp, " ");
	  query++;
	}
      else
	{
	  chbk = query[offset];
	  query[offset] = '\0';
	  cas_fprintf (fp, "%s ", query);
	  query[offset] = chbk;
	  query += offset;
	}
    }
}

void
CHidePassword::fprintf_password (FILE *fp, char *query, int *offset_ptr,
				 int (*cas_fprintf) (FILE *, const char *, ...))
{
  char *qryptr = query;
  char chbk;
  int pos;

  // offset_ptr[0] : size of offset_ptr
  // offset_ptr[1] : used count
  for (int x = 2; x < offset_ptr[1]; x += 2)
    {
      pos = offset_ptr[x];
      chbk = query[pos];
      query[pos] = '\0';
      fprintf_replace_newline (fp, qryptr, cas_fprintf);
      query[pos] = chbk;

      cas_fprintf (fp, "%s", (IS_PWD_NEED_COMMA (offset_ptr + x) ? ", '****'" : "'****'"));
      qryptr = query + GET_END_PWD_OFFSET (offset_ptr + x);
    }

  if (*qryptr)
    {
      fprintf_replace_newline (fp, qryptr, cas_fprintf);
    }
}

#if 0
void
check_have_password_func (char *query, int *pwd_offset_ptr)
{
  fprintf (stdout, "\nORIG [%s]\n", query);
  fflush (stdout);

  char *bufptr = (char *) query;
  int *offset_ptr = pwd_offset_ptr;
  char *tmp = bufptr;
  char chbk;
  int pos;

  fprintf (stdout, "CVRT [");

  for (int x = 2; x < offset_ptr[1]; x += 2)
    {
      pos = offset_ptr[x];
      chbk = bufptr[pos];
      bufptr[pos] = '\0';
      fprintf (stdout, "%s", tmp);
      bufptr[pos] = chbk;

      fprintf (stdout, "%s", (IS_PWD_NEED_COMMA (offset_ptr + x) ? ", '****'" : "'****'"));

      tmp = bufptr + GET_END_PWD_OFFSET (offset_ptr + x);
    }

  if (*tmp)
    {
      fprintf (stdout, "%s", tmp);
    }

  fprintf (stdout, "]\n****************************************************************\n");
}

#define check_have_password_debug check_have_password_func
#else
#define check_have_password_debug(a, b)
#endif

#ifndef DEFAULT_PWD_OFFSET_CNT
#define DEFAULT_PWD_OFFSET_CNT (10)
#endif

void
fprintf_password (FILE *fp, char *query, int *pwd_offset_ptr, int (*cas_fprintf) (FILE *, const char *, ...))
{
  CHidePassword chp;

  if (pwd_offset_ptr)
    {
      check_have_password_debug (query, pwd_offset_ptr);
      chp.fprintf_password (fp, query, pwd_offset_ptr, cas_fprintf);
    }
  else
    {
      int offset[DEFAULT_PWD_OFFSET_CNT];
      int *offset_ptr;

      INIT_PASSWORD_OFFSET (offset, offset_ptr, DEFAULT_PWD_OFFSET_CNT);

      offset_ptr[0] = DEFAULT_PWD_OFFSET_CNT;
      offset_ptr[1] = 2;

      chp.find_password_positions (&offset_ptr, query);

      check_have_password_debug (query, offset_ptr);
      chp.fprintf_password (fp, query, offset_ptr, cas_fprintf);

      QUIT_PASSWORD_OFFSET (offset, offset_ptr, DEFAULT_PWD_OFFSET_CNT);
    }
}

int
snprint_password (char *msg, int size, char *query, int *pwd_offset_ptr)
{
  CHidePassword chp;
  int ret;

  if (pwd_offset_ptr)
    {
      check_have_password_debug (query, pwd_offset_ptr);
      ret = chp.snprint_password (msg, size, query, pwd_offset_ptr);
    }
  else
    {
      int offset[DEFAULT_PWD_OFFSET_CNT];
      int *offset_ptr;

      INIT_PASSWORD_OFFSET (offset, offset_ptr, DEFAULT_PWD_OFFSET_CNT);

      chp.find_password_positions (&offset_ptr, query);

      check_have_password_debug (query, offset_ptr);
      ret = chp.snprint_password (msg, size, query, offset_ptr);
      QUIT_PASSWORD_OFFSET (offset, offset_ptr, DEFAULT_PWD_OFFSET_CNT);
    }

  return ret;
}

#ifndef DEFAULT_PWD_OFFSET_CNT
#undef DEFAULT_PWD_OFFSET_CNT
#endif

/*
 * add_offset_password () -
 *   fixed_array(in):
 *   pwd_offset_ptr(in/out)
 *   start(in)
 *   end(in)
 *   is_add_comma(in)
 *   return:
 */
// count, {start offset, end offset, is_add_comma}, ...
int
add_offset_password (int *fixed_array, int **pwd_offset_ptr, int start, int end, bool is_add_comma)
{
  /* pwd_offset_ptr:
   * [0] : number of alloced.
   * [1] : used count.
   * [even] : Start offset of password
   * [odd] : Length of the password string(Including the need for comma)
   */
  int alloc_szie, used_size;
  int *offset_ptr = *pwd_offset_ptr;

  alloc_szie = offset_ptr[0];
  used_size = offset_ptr[1];
  assert (used_size >= 4);

  if (alloc_szie < (used_size + 2))
    {
      int new_size = (int) (alloc_szie * 1.5);

      offset_ptr = (int *) malloc (new_size * sizeof (int));
      if (!offset_ptr)
	{
	  assert (0);
	  return -1;
	}

      memcpy (offset_ptr, *pwd_offset_ptr, used_size * sizeof (int));
      if (*pwd_offset_ptr != fixed_array)
	{
	  free (*pwd_offset_ptr);
	}

      offset_ptr[0] = new_size;
      *pwd_offset_ptr = offset_ptr;
    }

  offset_ptr[used_size++] = start;
  offset_ptr[used_size++] = SET_PWD_LENGTH_N_COMMA (start, end, is_add_comma);
  offset_ptr[1] = used_size;

  return 0;
}
