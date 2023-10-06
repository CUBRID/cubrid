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
#include "system_parameter.h"
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

#define SET_PWD_LENGTH(s, e)             ((e) - (s))
#define SET_PWD_ADDINFO(comma, pwd)      (((comma) ? (0x01 << 30) : 0) | (((pwd) & 0x03) << 28))
#define SET_PWD_LENGTH_N_ADDINFO(s, e, comma, pwd)  (SET_PWD_ADDINFO((comma), (pwd)) | SET_PWD_LENGTH((s), (e)))
#define IS_PWD_NEED_COMMA(offset_ptr)    (((offset_ptr)[1] >> 30) & 0x01)
#define IS_PWD_NEED_PASSWORD(offset_ptr) (((offset_ptr)[1] >> 28) & 0x03)
#define GET_END_PWD_OFFSET(offset_ptr)   ((offset_ptr)[0] + ((offset_ptr)[1] & 0x0FFFFFFF))

class CHidePassword
{
  private:
    char *skip_comment_string (char *query);
    char *get_quot_string (char *ps);
    char *get_token (char *&in, int &len);
    char *get_method_passowrd_start_position (char *ps, char *method_name, int *method_password_len);
    char *get_passowrd_pos_n_len (char *query, bool is_create, bool is_server, int *password_len,
				  bool *is_found_pwd_keyword);
    char *skip_one_query (char *query);
    bool check_lead_string_in_query (char **query, char **method_name, bool *is_create, bool *is_server);
    void fprintf_replace_newline (FILE *fp, char *query, int (*cas_fprintf) (FILE *, const char *, ...));
    bool check_capatalized_create (char *query);

    bool  m_use_backslash_escape;

  public:
    CHidePassword ()
    {
      m_use_backslash_escape = ! prm_get_bool_value (PRM_ID_NO_BACKSLASH_ESCAPES);
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
      if (*ps == '\\' && m_use_backslash_escape)
	{
	  ps++;
	}
      else if (*ps == quot_char)
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
      in = get_quot_string (in);
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
	case '\'':
	case '"':
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
CHidePassword::get_passowrd_pos_n_len (char *query, bool is_create, bool is_server, int *password_len,
				       bool *is_found_pwd_keyword)
{
  char *ps = query;
  char *token, *prev;
  int len, tlen;
  bool is_open = false;

  /* pattern cases to consider)
  * CREATE SERVER srv1 (HOST='localhost', PORT=3300, DBNAME=demodb, USER=dev1, PASSWORD='password_string')
  * CREATE SERVER srv1 (HOST='localhost', PORT=3300, DBNAME=demodb, USER=dev1, PASSWORD=)
  * CREATE SERVER srv1 (HOST='localhost', PORT=3300, DBNAME=demodb, USER=dev1, PASSWORD='')
  * CREATE SERVER srv1 (HOST='localhost', PORT=3300, DBNAME=demodb, PASSWORD=, USER=dev1)
  * CREATE SERVER srv1 (HOST='localhost', PORT=3300, DBNAME=demodb, USER=dev1)
  *
  * CREATE USER user_name
  * CREATE USER user_name PASSWORD 'password_string'
  * CREATE USER user_name PASSWORD _utf8'password_string'
  * CREATE USER user_name PASSWORD _euckr'password_string'
  * ALTER USER user_name PASSWORD 'password_string'
  * ALTER SERVER srv1 CHANGE PASSWORD='password string'
  */

  *password_len = 0;
  *is_found_pwd_keyword = false;
  while (*ps)
    {
      token = get_token (ps, len);
      if (*token == ';' || *token == ')')
	{
	  return token;
	}

      if (len == 8 && strncasecmp (token, "password", 8) == 0)
	{
	  *is_found_pwd_keyword = true;
	  break;
	}
    }

  if (*ps == '\0')
    {
      // check case "CREATE USER user_name"
      return (is_create && !is_server) ? ps : NULL;
    }

  prev = NULL;
  token = get_token (ps, len);
  if (is_server)
    {
      /* pattern cases)
         *    PASSWORD = 'string'   PASSWORD = ''    PASSWORD = )
         *    PASSWORD = ,          PASSWORD = ;     PASSWORD = \0
        */
      if (*token != '=')
	{
	  return NULL;
	}

      token = get_token (ps, len);
      if (*token == ';' || *token == ')' || *token == ',' )
	{
	  return token;
	}
    }
  else if (*token == '_')
    {
      prev = token;
      tlen = len;
      if ((len == 5 && strncasecmp (token + 1, "utf8'", len) == 0)
	  || (len == 6 && strncasecmp (token + 1, "euckr'", len) == 0)
	  || (len == 9 && strncasecmp (token + 1, "iso88591'", len) == 0)
	  || (len == 7 && strncasecmp (token + 1, "binary'", len) == 0))
	{
	  token = get_token (ps, len);
	}
      else
	{
	  return NULL;
	}
    }

  if (len >= 2 && (*token == '\'' || *token == '"'))
    {
      *password_len = (prev ? (len + tlen) : len);
      return (prev ? prev : token);
    }

  return NULL;
}

char *
CHidePassword::skip_one_query (char *query)
{
  char *ps;

  for (ps = query; *ps; ps++)
    {
      if (*ps == '\'' || *ps == '"')
	{
	  ps = get_quot_string (ps);
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

bool CHidePassword::check_lead_string_in_query (char **query, char **method_name, bool *is_create, bool *is_server)
{
  char   *ps;
  const char *first_cmd_str[] = { "call", "create", "alter", NULL };
  int  first_cmd_len[] = { 4, 6, 5, -1 };
  const char *second_cmd_str[] = { "server", "user", NULL };
  int  second_cmd_len[] = { 6, 4, -1 };
  static const char *method_name_str[] = { "add_user", "set_password", "login", NULL };
  int method_name_len[] = { 8, 12, 5, -1 };
  int *len_ptr = NULL;
  const char **str_ptr = NULL;
  bool  is_call_stmt = false;

  char *token;
  int len;

  *is_create = false;
  *is_server = false;

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
	  if (str_ptr == first_cmd_str)
	    {
	      is_call_stmt = true;
	    }
	  else
	    {
	      *is_create = (str_ptr == &first_cmd_str[1]) ? true : false;
	    }
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
	      if (is_call_stmt)
		{
		  *method_name = (char *) *str_ptr;
		}
	      else
		{
		  *is_server = (str_ptr == &second_cmd_str[0]) ? true : false;
		}
	      return true;
	    }
	}
    }

  return false;
}

int
CHidePassword::find_password_positions (int **fixed_pwd_offset_ptr, char *query)
{
  int start, end;
  bool is_add_comma;
  int alloc_szie, used_size;
  int *offset_ptr, *pwd_offset_ptr;
  bool is_create, is_server, has_password_keyword;
  char *newptr = query;
  EN_ADD_PWD_STRING en_add_pwd_string = en_none_password;

  assert (fixed_pwd_offset_ptr != NULL);
  assert (*fixed_pwd_offset_ptr != NULL);

  pwd_offset_ptr = *fixed_pwd_offset_ptr;
  alloc_szie = pwd_offset_ptr[0];
  used_size = pwd_offset_ptr[1];
  offset_ptr = pwd_offset_ptr;

  while (*newptr)
    {
      char *tp, *ps;
      int password_len;
      char *method_name = NULL;

      if (check_lead_string_in_query (&newptr, &method_name, &is_create, &is_server))
	{
	  if (method_name)
	    {
	      if ((ps = get_method_passowrd_start_position (newptr, method_name, &password_len)) == NULL)
		{
		  newptr = skip_one_query (newptr);
		  continue;
		}

	      start = (int) (ps - query);
	      end = (int) (ps - query) + password_len;
	      is_add_comma = (bool) (password_len == 0);
	      en_add_pwd_string = en_none_password;
	      newptr = ps + password_len;
	    }
	  else
	    {
	      if ((ps = get_passowrd_pos_n_len (newptr, is_create, is_server, &password_len, &has_password_keyword)) == NULL)
		{
		  newptr = skip_one_query (newptr);
		  continue;
		}

	      start = (int) (ps - query);
	      end = (int) (ps - query) + password_len;
	      en_add_pwd_string = (is_create
				   && !has_password_keyword) ? (is_server ? en_server_password : en_user_password) : en_none_password;
	      is_add_comma = (bool) (is_create && is_server && !has_password_keyword);

	      newptr = ps + password_len;
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
	  offset_ptr[used_size++] = SET_PWD_LENGTH_N_ADDINFO (start, end, is_add_comma, en_add_pwd_string);
	  offset_ptr[1] = used_size;
	}

      newptr = skip_one_query (newptr);
    }

  *fixed_pwd_offset_ptr = pwd_offset_ptr;
  return 0;
}

bool
CHidePassword::check_capatalized_create (char *query)
{
  char *ps = query;
  int len;
  char *token;

  // "create user ~" or " ~; create user ~"
  token = get_token (ps, len);
  if ((len == 6) &&  (strncmp (token, "CREATE", len) == 0))
    {
      return true;
    }
  return false;
}

int
CHidePassword::snprint_password (char *msg, int size, char *query, int *offset_ptr)
{
  char *qryptr = query;
  char chbk;
  int pos;
  int length = 0;
  EN_ADD_PWD_STRING en_pwd_string;
  const char *pwd_str;
  // offset_ptr[0] : size of offset_ptr
  // offset_ptr[1] : used count
  for (int x = 2; x < offset_ptr[1]; x += 2)
    {
      pos = offset_ptr[x];
      chbk = query[pos];
      query[pos] = '\0';
      length += snprintf (msg + length, size - length, "%s", qryptr);

      en_pwd_string = (EN_ADD_PWD_STRING)IS_PWD_NEED_PASSWORD (offset_ptr + x);
      if (en_pwd_string == en_none_password)
	{
	  pwd_str = (IS_PWD_NEED_COMMA (offset_ptr + x) ? ", '****'" : " '****'");
	}
      else
	{
	  bool is_capatalized = check_capatalized_create (qryptr);

	  if (en_pwd_string == en_user_password)
	    {
	      pwd_str = is_capatalized ? " PASSWORD '****'" : " password '****'";
	    }
	  else /* if(en_pwd_string == en_server_password) */
	    {
	      pwd_str = is_capatalized ? ", PASSWORD='****'" : " password='****'";
	    }
	}

      length += snprintf (msg + length, size - length, "%s", pwd_str);

      query[pos] = chbk;
      qryptr = query + GET_END_PWD_OFFSET (offset_ptr + x);
    }

  if (*qryptr)
    {
      length += snprintf (msg + length, size - length, "%s", qryptr);
    }

  return length;
}

void
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
  EN_ADD_PWD_STRING en_pwd_string;
  const char *pwd_str;

  // offset_ptr[0] : size of offset_ptr
  // offset_ptr[1] : used count
  for (int x = 2; x < offset_ptr[1]; x += 2)
    {
      pos = offset_ptr[x];
      chbk = query[pos];
      query[pos] = '\0';
      fprintf_replace_newline (fp, qryptr, cas_fprintf);

      en_pwd_string = (EN_ADD_PWD_STRING)IS_PWD_NEED_PASSWORD (offset_ptr + x);
      if (en_pwd_string == en_none_password)
	{
	  pwd_str = (IS_PWD_NEED_COMMA (offset_ptr + x) ? ", '****'" : " '****'");
	}
      else
	{
	  bool is_capatalized = check_capatalized_create (qryptr);
	  if (en_pwd_string == en_user_password)
	    {
	      pwd_str = is_capatalized ? " PASSWORD '****'" : " password '****'";
	    }
	  else /* if(en_pwd_string == en_server_password) */
	    {
	      pwd_str = is_capatalized ? ", PASSWORD='****'" : " password='****'";
	    }
	}
      cas_fprintf (fp, "%s",  pwd_str);

      query[pos] = chbk;
      qryptr = query + GET_END_PWD_OFFSET (offset_ptr + x);
    }

  if (*qryptr)
    {
      fprintf_replace_newline (fp, qryptr, cas_fprintf);
    }
}

#ifndef DEFAULT_PWD_OFFSET_CNT
#define DEFAULT_PWD_OFFSET_CNT (10)
#endif

void
password_fprintf (FILE *fp, char *query, int *pwd_offset_ptr, int (*cas_fprintf) (FILE *, const char *, ...))
{
  CHidePassword chp;

  if (pwd_offset_ptr)
    {
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

      chp.fprintf_password (fp, query, offset_ptr, cas_fprintf);

      QUIT_PASSWORD_OFFSET (offset, offset_ptr, DEFAULT_PWD_OFFSET_CNT);
    }
}

int
password_snprint (char *msg, int size, char *query, int *pwd_offset_ptr)
{
  CHidePassword chp;
  int ret;

  if (pwd_offset_ptr)
    {
      ret = chp.snprint_password (msg, size, query, pwd_offset_ptr);
    }
  else
    {
      int offset[DEFAULT_PWD_OFFSET_CNT];
      int *offset_ptr;

      INIT_PASSWORD_OFFSET (offset, offset_ptr, DEFAULT_PWD_OFFSET_CNT);

      chp.find_password_positions (&offset_ptr, query);

      ret = chp.snprint_password (msg, size, query, offset_ptr);
      QUIT_PASSWORD_OFFSET (offset, offset_ptr, DEFAULT_PWD_OFFSET_CNT);
    }

  return ret;
}

#ifndef DEFAULT_PWD_OFFSET_CNT
#undef DEFAULT_PWD_OFFSET_CNT
#endif

/*
 * password_add_offset () -
 *   fixed_array(in):
 *   pwd_offset_ptr(in/out)
 *   start(in)
 *   end(in)
 *   is_add_comma(in)
 *   return:
 */
// count, {start offset, end offset, is_add_comma}, ...
int
password_add_offset (int *fixed_array, int **pwd_offset_ptr, int start, int end, bool is_add_comma,
		     EN_ADD_PWD_STRING en_add_pwd_string)
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
  assert (used_size >= 2);

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
  offset_ptr[used_size++] = SET_PWD_LENGTH_N_ADDINFO (start, end, is_add_comma, en_add_pwd_string);
  offset_ptr[1] = used_size;

  return 0;
}

bool
password_mk_offset_for_one_query (int *new_offset_arr, int *orig_offset_ptr, int start_pos, int end_pos)
{
  if (orig_offset_ptr)
    {
      // offset_ptr[0] : size of offset_ptr
      // offset_ptr[1] : used count
      assert (new_offset_arr[0] == 4);
      assert (new_offset_arr[1] == 2);

      for (int x = 2; x < orig_offset_ptr[1]; x += 2)
	{
	  if (orig_offset_ptr[x] > end_pos)
	    {
	      break;
	    }

	  if (orig_offset_ptr[x] >= start_pos && orig_offset_ptr[x] <= end_pos)
	    {
	      new_offset_arr[1] = 4;
	      new_offset_arr[2] = orig_offset_ptr[x] - start_pos;
	      new_offset_arr[3] = orig_offset_ptr[x + 1];
	      return true;
	    }
	}
    }
  return false;
}

