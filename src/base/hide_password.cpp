/*
 *
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
 * hide_password.cpp -
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
#include "hide_password.h"
#include "system_parameter.h"
#include "chartype.h"

//==========================================================================
#define SKIP_SPACE_CHARACTERS(p)                \
  do {                                          \
        while (char_isspace((unsigned char) *p))\
	    {                                   \
	      p++;                              \
	    }                                   \
   } while(0)

#define MAX_PWD_LENGTH                     (0x0FFFFFFF)
#define SET_PWD_LENGTH(s, e)               ((e) - (s))
#define SET_PWD_ADDINFO(comma, en_pwd)     (((comma) ? (0x01 << 30) : 0) | (((en_pwd) & 0x03) << 28))
#define SET_PWD_LENGTH_N_ADDINFO(s, e, comma, en_pwd)  (SET_PWD_ADDINFO((comma), (en_pwd)) | SET_PWD_LENGTH((s), (e)))
#define IS_PWD_NEED_COMMA(pwd_info_ptr)    (((pwd_info_ptr)[1] >> 30) & 0x01)
#define IS_PWD_NEED_PASSWORD(pwd_info_ptr) ((EN_ADD_PWD_STRING)(((pwd_info_ptr)[1] >> 28) & 0x03))
#define GET_END_PWD_OFFSET(pwd_info_ptr)   ((pwd_info_ptr)[0] + ((pwd_info_ptr)[1] & MAX_PWD_LENGTH))

class hide_password
{
  private:
    char *skip_comment_string (char *query);
    char *find_end_of_quot (char *ps);
    char *get_token (char *&in, int &len);
    char *get_method_passowrd_start_position (char *ps, char *method_name, int *method_password_len);
    char *get_passowrd_pos_n_len (char *query, bool is_create, bool is_server, int *password_len,
				  bool *is_pwd_keyword_found);
    char *skip_one_query (char *query);
    bool check_lead_string_in_query (char **query, char **method_name, bool *is_create, bool *is_server);
    void fprintf_replace_newline (FILE *fp, char *query, int (*cas_fprintf) (FILE *, const char *, ...));
    bool check_capitalized_keyword_create (char *query);
    const char *get_password_string (char *qryptr, int *pwd_info_ptr);

    bool  m_use_backslash_escape;

  public:
    hide_password ()
    {
      m_use_backslash_escape = ! prm_get_bool_value (PRM_ID_NO_BACKSLASH_ESCAPES);
    };
    ~hide_password ()
    {
    };

    void find_password_positions (char *query, HIDE_PWD_INFO_PTR hide_pwd_ptr);
    int snprint_password (char *msg, int size, char *query, HIDE_PWD_INFO_PTR hide_pwd_ptr);
    void fprintf_password (FILE *fp, char *query, HIDE_PWD_INFO_PTR hide_pwd_ptr,
			   int (*cas_fprintf) (FILE *, const char *, ...));
};

char *
hide_password::skip_comment_string (char *query)
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
hide_password::find_end_of_quot (char *ps)
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
hide_password::get_token (char *&in, int &len)
{
  char *ps, check_char;
  int quoted_single = 0;
  int quoted_double = 0;

  SKIP_SPACE_CHARACTERS (in);
  ps = in;
  if (*in == '\'' || *in == '"')
    {
      in = find_end_of_quot (in);
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
	  if (char_isspace ((unsigned char) *in))
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
hide_password::get_method_passowrd_start_position (char *ps, char *method_name, int *method_password_len)
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

  if (strcasecmp (method_name, "set_password") == 0)
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
hide_password::get_passowrd_pos_n_len (char *query, bool is_create, bool is_server, int *password_len,
				       bool *is_pwd_keyword_found)
{
  char *ps = query;
  char *token, *prev;
  int len, tlen = 0;
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
  *is_pwd_keyword_found = false;
  while (*ps)
    {
      token = get_token (ps, len);
      if (*token == ';' || *token == ')')
	{
	  return token;
	}

      if (len == 8 && strncasecmp (token, "password", 8) == 0)
	{
	  *is_pwd_keyword_found = true;
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
hide_password::skip_one_query (char *query)
{
  char *ps;

  for (ps = query; *ps; ps++)
    {
      if (*ps == '\'' || *ps == '"')
	{
	  ps = find_end_of_quot (ps);
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

bool hide_password::check_lead_string_in_query (char **query, char **method_name, bool *is_create, bool *is_server)
{
#define IDX_CALL_STMT   (0)
#define IDX_CREATE_STMT (1)
#define IDX_SERVER_STMT (0)
  static const char *first_cmd_str[] = { "call", "create", "alter", NULL };  //
  int  first_cmd_len[] = { 4, 6, 5, -1 };  // first_cmd_len[idx] = strlen(first_cmd_str[idx])
  static const char *second_cmd_str[] = { "server", "user", NULL };
  int  second_cmd_len[] = { 6, 4, -1 };    // second_cmd_len[idx] = strlen(second_cmd_str[idx])
  static const char *method_name_str[] = { "add_user", "set_password", "login", NULL };
  int method_name_len[] = { 8, 12, 5, -1 };
  bool  is_call_stmt = false;
  int *len_ptr = NULL;
  char **str_ptr = NULL;

  char *token, *ps;
  int len, i;

  *is_create = false;
  *is_server = false;

  token = get_token (*query, len);
  if (!token)
    {
      return false;
    }

  for ( i = 0; first_cmd_str[i]; i++)
    {
      if (first_cmd_len[i] == len && strncasecmp (token, first_cmd_str[i], first_cmd_len[i]) == 0)
	{
	  if (i == IDX_CALL_STMT)
	    {
	      is_call_stmt = true;
	      len_ptr = method_name_len;
	      str_ptr = (char **)method_name_str;
	    }
	  else
	    {
	      *is_create = (i == IDX_CREATE_STMT) ? true : false;
	      len_ptr = second_cmd_len;
	      str_ptr = (char **)second_cmd_str;
	    }
	  break;
	}
    }

  if (first_cmd_str[i] == NULL || (*query)[0] == '\0')
    {
      return false;
    }

  assert (len_ptr && str_ptr);

  token = get_token (*query, len);
  if (token)
    {
      for ( i = 0; str_ptr[i]; i++)
	{
	  if (len_ptr[i] == len && strncasecmp (token, str_ptr[i], len_ptr[i]) == 0)
	    {
	      if (is_call_stmt)
		{
		  *method_name = (char *) str_ptr[i];
		}
	      else
		{
		  *is_server = (i == IDX_SERVER_STMT) ? true : false;
		}
	      return true;
	    }
	}
    }

  return false;
}

void
hide_password::find_password_positions (char *query, HIDE_PWD_INFO_PTR hide_pwd_ptr)
{
  int start, end;
  bool is_add_comma;
  bool is_create, is_server, has_password_keyword;
  char *newptr = query;
  EN_ADD_PWD_STRING en_add_pwd_string = en_none_password;

  assert (hide_pwd_ptr != NULL);
  assert (hide_pwd_ptr->pwd_info_ptr != NULL);

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

	  if (hide_pwd_ptr->size < (hide_pwd_ptr->used + 2))
	    {
	      int *pwd_info_ptr = NULL;
	      int new_size = hide_pwd_ptr->size + (DEFAULT_PWD_INFO_CNT * 2);

	      pwd_info_ptr = (int *) malloc (new_size * sizeof (int));
	      if (!pwd_info_ptr)
		{
		  assert (0);
		  return;
		}

	      memcpy (pwd_info_ptr, hide_pwd_ptr->pwd_info_ptr, hide_pwd_ptr->used * sizeof (int));
	      if (hide_pwd_ptr->pwd_info_ptr != hide_pwd_ptr->pwd_info)
		{
		  free (hide_pwd_ptr->pwd_info_ptr);
		}

	      hide_pwd_ptr->size = new_size;
	      hide_pwd_ptr->pwd_info_ptr = pwd_info_ptr;
	    }

	  assert (SET_PWD_LENGTH (start, end) < MAX_PWD_LENGTH);
	  hide_pwd_ptr->pwd_info_ptr[hide_pwd_ptr->used++] = start;
	  hide_pwd_ptr->pwd_info_ptr[hide_pwd_ptr->used++] = SET_PWD_LENGTH_N_ADDINFO (start, end, is_add_comma,
	      en_add_pwd_string);
	}

      newptr = skip_one_query (newptr);
    }
}

bool
hide_password::check_capitalized_keyword_create (char *query)
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

const char *
hide_password::get_password_string (char *qryptr, int *pwd_info_ptr)
{
  EN_ADD_PWD_STRING en_pwd_string;
  // *INDENT-OFF*
  static const char* password_string[6] = { 
        ", '****'" ,          " '****'",
        " PASSWORD '****'" ,  " password '****'",
        ", PASSWORD='****'" , " password='****'"
  };
  // *INDENT-ON*

  en_pwd_string = IS_PWD_NEED_PASSWORD (pwd_info_ptr);
  if (en_pwd_string == en_none_password)
    {
      return (IS_PWD_NEED_COMMA (pwd_info_ptr) ? password_string[0] : password_string[1]);
    }
  else
    {
      bool is_capatalized = check_capitalized_keyword_create (qryptr);

      if (en_pwd_string == en_user_password)
	{
	  return (is_capatalized ? password_string[2] : password_string[3]);
	}
      else
	{
	  assert (en_pwd_string == en_server_password);
	  return (is_capatalized ? password_string[4] : password_string[5]);
	}
    }
}

int
hide_password::snprint_password (char *msg, int size, char *query, HIDE_PWD_INFO_PTR hide_pwd_ptr)
{
  char *qryptr = query;
  char chbk;
  int pos;
  int length = 0;

  assert (hide_pwd_ptr);
  int *pwd_info_ptr = hide_pwd_ptr->pwd_info_ptr;

  for (int x = 0; x < hide_pwd_ptr->used; x += 2)
    {
      pos = pwd_info_ptr[x];
      chbk = query[pos];
      query[pos] = '\0';

      length += snprintf (msg + length, size - length, "%s", qryptr);
      length += snprintf (msg + length, size - length, "%s", get_password_string (qryptr, pwd_info_ptr + x));

      query[pos] = chbk;
      qryptr = query + GET_END_PWD_OFFSET (pwd_info_ptr + x);
    }

  if (*qryptr)
    {
      length += snprintf (msg + length, size - length, "%s", qryptr);
    }

  return length;
}

void
hide_password::fprintf_replace_newline (FILE *fp, char *query, int (*cas_fprintf) (FILE *, const char *, ...))
{
  int offset;
  char chbk;

  assert (fp != NULL);

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
	  cas_fprintf (fp, "%s", query);
	  query[offset] = chbk;
	  query += offset;
	}
    }
}

void
hide_password::fprintf_password (FILE *fp, char *query, HIDE_PWD_INFO_PTR hide_pwd_ptr,
				 int (*cas_fprintf) (FILE *, const char *, ...))
{
  char *qryptr = query;
  char chbk;
  int pos;

  assert (hide_pwd_ptr);
  assert (fp != NULL);

  int *pwd_info_ptr = hide_pwd_ptr->pwd_info_ptr;

  for (int x = 0; x < hide_pwd_ptr->used; x += 2)
    {
      pos = pwd_info_ptr[x];
      chbk = query[pos];
      query[pos] = '\0';

      fprintf_replace_newline (fp, qryptr, cas_fprintf);
      cas_fprintf (fp, "%s",  get_password_string (qryptr, pwd_info_ptr + x));

      query[pos] = chbk;
      qryptr = query + GET_END_PWD_OFFSET (pwd_info_ptr + x);
    }

  if (*qryptr)
    {
      fprintf_replace_newline (fp, qryptr, cas_fprintf);
    }
}

void
password_fprintf (FILE *fp, char *query, HIDE_PWD_INFO_PTR hide_pwd_info_ptr,
		  int (*cas_fprintf) (FILE *, const char *, ...))
{
  hide_password chp;

  if (hide_pwd_info_ptr && hide_pwd_info_ptr->pwd_info_ptr)
    {
      chp.fprintf_password (fp, query, hide_pwd_info_ptr, cas_fprintf);
    }
  else
    {
      HIDE_PWD_INFO t_pwd_info;

      INIT_HIDE_PASSWORD_INFO (&t_pwd_info);
      chp.find_password_positions (query, &t_pwd_info);
      chp.fprintf_password (fp, query, &t_pwd_info, cas_fprintf);
      QUIT_HIDE_PASSWORD_INFO (&t_pwd_info);
    }
}

int
password_snprint (char *msg, int size, char *query, HIDE_PWD_INFO_PTR hide_pwd_info_ptr)
{
  hide_password chp;
  int ret;

  if (hide_pwd_info_ptr && hide_pwd_info_ptr->pwd_info_ptr)
    {
      ret = chp.snprint_password (msg, size, query, hide_pwd_info_ptr);
    }
  else
    {
      HIDE_PWD_INFO t_pwd_info;

      INIT_HIDE_PASSWORD_INFO (&t_pwd_info);
      chp.find_password_positions (query, &t_pwd_info);
      ret = chp.snprint_password (msg, size, query, &t_pwd_info);
      QUIT_HIDE_PASSWORD_INFO (&t_pwd_info);
    }

  return ret;
}

/*
 * password_add_offset () -
 *   hide_pwd_info_ptr(in/out):
 *   start(in)
 *   end(in)
 *   is_add_comma(in)
 *   en_add_pwd_string(in)
 *   return:
 */
void
password_add_offset (HIDE_PWD_INFO_PTR hide_pwd_info_ptr, int start, int end, bool is_add_comma,
		     EN_ADD_PWD_STRING en_add_pwd_string)
{
  assert (hide_pwd_info_ptr);

  /* hide_pwd_info_ptr->pwd_info_ptr:
   * [even] : Start offset of password
   * [odd] : Length of the password string(Including the need for comma)
   */

  if (hide_pwd_info_ptr->size < (hide_pwd_info_ptr->used + 2))
    {
      int *pwd_info_ptr = NULL;
      int new_size = hide_pwd_info_ptr->size + (DEFAULT_PWD_INFO_CNT * 2);

      pwd_info_ptr = (int *) malloc (new_size * sizeof (int));
      if (!pwd_info_ptr)
	{
	  assert (0);
	  return;
	}

      memcpy (pwd_info_ptr, hide_pwd_info_ptr->pwd_info_ptr, hide_pwd_info_ptr->used * sizeof (int));
      if (hide_pwd_info_ptr->pwd_info_ptr != hide_pwd_info_ptr->pwd_info)
	{
	  free (hide_pwd_info_ptr->pwd_info_ptr);
	}

      hide_pwd_info_ptr->size = new_size;
      hide_pwd_info_ptr->pwd_info_ptr = pwd_info_ptr;
    }

  assert (SET_PWD_LENGTH (start, end) < MAX_PWD_LENGTH);
  hide_pwd_info_ptr->pwd_info_ptr[hide_pwd_info_ptr->used++] = start;
  hide_pwd_info_ptr->pwd_info_ptr[hide_pwd_info_ptr->used++] = SET_PWD_LENGTH_N_ADDINFO (start, end, is_add_comma,
      en_add_pwd_string);
}

bool
password_remake_offset_for_one_query (HIDE_PWD_INFO_PTR new_hide_pwd_info_ptr, HIDE_PWD_INFO_PTR orig_hide_pwd_info_ptr,
				      int start_pos, int end_pos)
{
  int *new_pwd_info_ptr = (new_hide_pwd_info_ptr ? new_hide_pwd_info_ptr->pwd_info_ptr : NULL);
  int *orig_pwd_info_ptr = (orig_hide_pwd_info_ptr ? orig_hide_pwd_info_ptr->pwd_info_ptr : NULL);

  assert (new_pwd_info_ptr);
  assert (new_hide_pwd_info_ptr);
  assert (new_hide_pwd_info_ptr->used == 0);

  if (orig_pwd_info_ptr)
    {
      for (int x = 0; x < orig_hide_pwd_info_ptr->used; x += 2)
	{
	  if (orig_pwd_info_ptr[x] > end_pos)
	    {
	      break;
	    }

	  if (orig_pwd_info_ptr[x] >= start_pos && orig_pwd_info_ptr[x] <= end_pos)
	    {
	      new_pwd_info_ptr[new_hide_pwd_info_ptr->used++] = orig_pwd_info_ptr[x] - start_pos;
	      new_pwd_info_ptr[new_hide_pwd_info_ptr->used++] = orig_pwd_info_ptr[x + 1];
	      return true;
	    }
	}
    }
  return false;
}

