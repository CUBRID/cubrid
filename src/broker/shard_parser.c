/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 *   This program is free software; you can redistribute it and/or modify 
 *   it under the terms of the GNU General Public License as published by 
 *   the Free Software Foundation; either version 2 of the License, or 
 *   (at your option) any later version. 
 *
 *  This program is distributed in the hope that it will be useful, 
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of 
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the 
 *  GNU General Public License for more details. 
 *
 *  You should have received a copy of the GNU General Public License 
 *  along with this program; if not, write to the Free Software 
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA 
 *
 */


/*
 * shard_parser.c - 
 *
 */

#ident "$Id$"


#include "shard_parser.h"
#include "shard_proxy_log.h"

#if defined(WINDOWS)
static const char *left_trim (const char *str);
#else /* WINDOWS */
static inline const char *left_trim (const char *str);
#endif /* !WINDOWS */

static int sp_init_sp_value (SP_VALUE * value_p);
static void sp_free_sp_value (SP_VALUE * value_p);
static int sp_make_sp_value (SP_VALUE * value_p, const char *pos, int length);
static int sp_make_string_sp_value (SP_VALUE * value_p, const char *pos,
				    int length);
static int sp_make_int_sp_value_from_string (SP_VALUE * value_p, char *pos,
					     int length);


static void sp_init_praser_hint_list (SP_PARSER_HINT_LIST * list);
static void sp_append_parser_hint_to_ctx (SP_PARSER_CTX * parser_p,
					  SP_PARSER_HINT * hint_p);
static int sp_parse_sql_internal (SP_PARSER_CTX * parser_p);

static void sp_free_parser_hint_from_ctx (SP_PARSER_CTX * parser_p);
static int sp_process_token (SP_PARSER_CTX * parser_p, SP_TOKEN token_type);
static int sp_get_bind_type_and_value (SP_PARSER_CTX * parser_p,
				       SP_PARSER_HINT * hint_p);
#if defined(WINDOWS)
static void sp_copy_cursor_to_prv (SP_PARSER_CTX * parser_p);
#else /* WINDOWS */
static inline void sp_copy_cursor_to_prv (SP_PARSER_CTX * parser_p);
#endif /* !WINDOWS */
static int sp_get_string_bind_value (SP_PARSER_CTX * parser_p,
				     SP_PARSER_HINT * hint_p);
static int sp_get_int_bind_value (SP_PARSER_CTX * parser_p,
				  SP_PARSER_HINT * hint_p);
static int sp_is_valid_hint (SP_PARSER_CTX * parser_p,
			     SP_PARSER_HINT * hint_p);
static bool sp_is_start_token (SP_TOKEN token);


SP_PARSER_CTX *
sp_create_parser (const char *sql_stmt)
{
  SP_PARSER_CTX *parser_p = (SP_PARSER_CTX *) malloc (sizeof (SP_PARSER_CTX));
  if (parser_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Not enough virtual memory. failed to malloc parser context. "
		 "(size:%d). \n", sizeof (SP_PARSER_CTX));
      return NULL;
    }

  parser_p->sql_stmt = strdup (sql_stmt);
  if (parser_p->sql_stmt == NULL)
    {
      free (parser_p);
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Not enough virtual memory. failed to strdup sql statement. "
		 "(sql_stmt:[%s]).", sql_stmt);
      return NULL;
    }
  parser_p->is_select = false;
  parser_p->bind_count = 0;
  parser_p->cursor.pos = parser_p->sql_stmt;
  parser_p->cursor.token = TT_NONE;
  parser_p->prv_cursor.pos = parser_p->sql_stmt;
  parser_p->prv_cursor.token = TT_NONE;
  parser_p->operator = TT_NONE;
  sp_init_praser_hint_list (&parser_p->list_a);
  sp_init_praser_hint_list (&parser_p->list_t[BT_STATIC]);
  sp_init_praser_hint_list (&parser_p->list_t[BT_DYNAMIC]);

  return parser_p;
}

int
sp_parse_sql (SP_PARSER_CTX * parser_p)
{
  int error = NO_ERROR;

  error = sp_parse_sql_internal (parser_p);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (parser_p->cursor.token != TT_NONE)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Unexpected token type. "
		 "(token_type:%d).", parser_p->cursor.token);
      return ER_SP_INVALID_SYNTAX;
    }

  return NO_ERROR;
}

void
sp_destroy_parser (SP_PARSER_CTX * parser_p)
{
  if (parser_p != NULL)
    {
      if (parser_p->sql_stmt != NULL)
	{
	  free (parser_p->sql_stmt);
	}

      sp_free_parser_hint_from_ctx (parser_p);
      free (parser_p);
    }
}

bool
sp_is_hint_static (SP_PARSER_CTX * parser_p)
{
  return parser_p->list_a.size == parser_p->list_t[BT_STATIC].size;
}

int
sp_get_total_hint_count (SP_PARSER_CTX * parser_p)
{
  return parser_p->list_a.size;
}

int
sp_get_static_hint_count (SP_PARSER_CTX * parser_p)
{
  return parser_p->list_t[BT_STATIC].size;
}

int
sp_get_dynamic_hint_count (SP_PARSER_CTX * parser_p)
{
  return parser_p->list_t[BT_DYNAMIC].size;
}

SP_PARSER_HINT *
sp_get_first_hint (SP_PARSER_CTX * parser_p)
{
  return parser_p->list_a.head;
}

SP_PARSER_HINT *
sp_get_next_hint (SP_PARSER_HINT * hint_p)
{
  return hint_p->next_a;
}

SP_PARSER_HINT *
sp_get_first_static_hint (SP_PARSER_CTX * parser_p)
{
  return parser_p->list_t[BT_STATIC].head;
}

SP_PARSER_HINT *
sp_get_next_static_hint (SP_PARSER_HINT * hint_p)
{
  return hint_p->next_t[BT_STATIC];
}

SP_PARSER_HINT *
sp_get_first_dynamic_hint (SP_PARSER_CTX * parser_p)
{
  return parser_p->list_t[BT_DYNAMIC].head;
}

SP_PARSER_HINT *
sp_get_next_dynamic_hint (SP_PARSER_HINT * hint_p)
{
  return hint_p->next_t[BT_DYNAMIC];
}

char *
sp_get_hint_key (SP_PARSER_HINT * hint_p)
{
  return hint_p->arg.string.value;
}

const char *
sp_get_sql_stmt (SP_PARSER_CTX * parser_p)
{
  return parser_p->sql_stmt;
}

bool
sp_is_pair_token (SP_TOKEN start_token, SP_TOKEN end_token)
{
  switch (start_token)
    {
    case TT_NONE:
    case TT_WHITESPACE:
    case TT_NEWLINE:
    case TT_C_COMMENT_END:
    case TT_ASSIGN_OP:
    case TT_IN_OP:
    case TT_RIGHT_BRAKET:
    case TT_BIND_CHAR:
    case TT_SINGLE_QUOTED:
    case TT_DOUBLE_QUOTED:
      return start_token == end_token;
    case TT_LEFT_BRAKET:
      return end_token == TT_RIGHT_BRAKET;
    case TT_CSQL_COMMENT:
    case TT_CPP_COMMENT:
      return end_token == TT_NEWLINE;
    case TT_C_COMMENT:
    case TT_HINT:
    case TT_SHARD_HINT:
      return end_token == TT_C_COMMENT_END;
    }
  return false;
}

#if defined(WINDOWS)
static const char *
left_trim (const char *str)
#else /* WINDOWS */
static inline const char *
left_trim (const char *str)
#endif				/* !WINDOWS */
{
  const char *p = str;

  while (*p && isspace (*p))
    {
      p++;
    }

  return p;
}

static int
sp_init_sp_value (SP_VALUE * value_p)
{
  value_p->type = VT_INTEGER;
  value_p->string.length = 0;
  value_p->string.value = value_p->string.value_arr;
  value_p->string.value_arr[0] = '\0';
  value_p->string.value_ex = NULL;
  value_p->integer = 0;
  return NO_ERROR;
}

static void
sp_free_sp_value (SP_VALUE * value_p)
{
  if (value_p != NULL && value_p->string.value_ex != NULL)
    {
      free (value_p->string.value_ex);
    }
}

static int
sp_make_sp_value (SP_VALUE * value_p, const char *pos, int length)
{
  SP_TOKEN token;
  char *p = (char *) left_trim (pos);
  while (isspace (p[length - 1]))
    {
      length--;
    }

  sp_get_token_type (p, &token);
  if (token == TT_SINGLE_QUOTED || token == TT_DOUBLE_QUOTED)
    {
      return sp_make_string_sp_value (value_p, p + 1, length - 2);
    }
  else
    {
      return sp_make_int_sp_value_from_string (value_p, p, length);
    }
}

static int
sp_make_string_sp_value (SP_VALUE * value_p, const char *pos, int length)
{
  if (length <= 0)
    {
      return ER_SP_INVALID_HINT;
    }

  if (length > SP_VALUE_INIT_SIZE)
    {
      value_p->string.value_ex =
	(char *) malloc (sizeof (char) * (length + 1));
      if (value_p->string.value_ex == NULL)
	{
	  return ER_SP_OUT_OF_MEMORY;
	}
      value_p->string.value = value_p->string.value_ex;
    }
  memcpy (value_p->string.value, pos, length);
  value_p->type = VT_STRING;
  value_p->string.length = length;
  value_p->string.value[length] = '\0';
  return NO_ERROR;
}

static int
sp_make_int_sp_value_from_string (SP_VALUE * value_p, char *pos, int length)
{
  char tmp = pos[length];
  char *end;
  pos[length] = '\0';

  errno = 0;
  value_p->integer = strtoll (pos, &end, 10);
  if (errno == ERANGE || *end != '\0')
    {
      return ER_SP_INVALID_HINT;
    }
  pos[length] = tmp;
  value_p->type = VT_INTEGER;
  return NO_ERROR;
}

SP_PARSER_HINT *
sp_create_parser_hint (void)
{
  int error = NO_ERROR;
  SP_PARSER_HINT *hint_p = NULL;

  hint_p = (SP_PARSER_HINT *) malloc (sizeof (SP_PARSER_HINT));
  if (hint_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Not enough virtual memory. failed to malloc parser hint. "
		 "(size:%d).", sizeof (SP_PARSER_HINT));

      return NULL;
    }

  error = sp_init_sp_value (&hint_p->arg);
  if (error != NO_ERROR)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Failed to initialize parser argument. "
		 "(error:%d).", error);
      goto PARSER_ERROR;
    }

  error = sp_init_sp_value (&hint_p->value);
  if (error != NO_ERROR)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to initialize parser value. "
		 "(error:%d). \n", error);

      goto PARSER_ERROR;
    }

  hint_p->next_a = NULL;
  hint_p->next_t[BT_STATIC] = NULL;
  hint_p->next_t[BT_DYNAMIC] = NULL;
  hint_p->hint_type = HT_NONE;
  hint_p->bind_type = BT_STATIC;
  hint_p->bind_position = -1 /* INVALID POSITION */ ;
  return hint_p;
PARSER_ERROR:
  if (hint_p != NULL)
    {
      sp_free_parser_hint (hint_p);
      free (hint_p);
    }
  return NULL;
}

static void
sp_init_praser_hint_list (SP_PARSER_HINT_LIST * list)
{
  list->head = NULL;
  list->tail = NULL;
  list->size = 0;
}

static void
sp_append_parser_hint_to_ctx (SP_PARSER_CTX * parser_p,
			      SP_PARSER_HINT * hint_p)
{
  SP_BIND_TYPE type = hint_p->bind_type;

  parser_p->list_a.size++;
  parser_p->list_t[hint_p->bind_type].size++;

  if (parser_p->list_a.head == NULL)
    {
      parser_p->list_a.head = hint_p;
    }
  if (parser_p->list_t[type].head == NULL)
    {
      parser_p->list_t[type].head = hint_p;
    }
  if (parser_p->list_a.tail != NULL)
    {
      parser_p->list_a.tail->next_a = hint_p;
    }
  if (parser_p->list_t[type].tail != NULL)
    {
      parser_p->list_t[type].tail->next_t[type] = hint_p;
    }

  parser_p->list_a.tail = hint_p;
  parser_p->list_t[type].tail = hint_p;
}

void
sp_free_parser_hint (SP_PARSER_HINT * hint_p)
{
  if (hint_p != NULL)
    {
      sp_free_sp_value (&hint_p->arg);
      sp_free_sp_value (&hint_p->value);
    }
}

static int
sp_parse_sql_internal (SP_PARSER_CTX * parser_p)
{
  int error = NO_ERROR;
  SP_TOKEN token_type = TT_NONE;

  while (*parser_p->cursor.pos)
    {
      parser_p->cursor.pos =
	sp_get_token_type (parser_p->cursor.pos, &token_type);
      if (token_type == TT_NONE)
	{
	  continue;
	}

      if (parser_p->cursor.token == TT_NONE)
	{
	  if (sp_is_start_token (token_type) == false)
	    {
	      return ER_SP_INVALID_SYNTAX;
	    }

	  parser_p->cursor.token = token_type;

	  error = sp_process_token (parser_p, token_type);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }

	  if (!sp_is_exist_pair_token (parser_p->cursor.token))
	    {
	      sp_copy_cursor_to_prv (parser_p);
	    }
	}
      else if (sp_is_pair_token (parser_p->cursor.token, token_type))
	{
	  sp_copy_cursor_to_prv (parser_p);
	}
    }

  if (parser_p->cursor.token == TT_CSQL_COMMENT
      || parser_p->cursor.token == TT_CPP_COMMENT)
    {
      parser_p->cursor.token = TT_NONE;
    }

  return NO_ERROR;
}

static void
sp_free_parser_hint_from_ctx (SP_PARSER_CTX * parser_p)
{
  SP_PARSER_HINT *hint_p = sp_get_first_hint (parser_p);
  SP_PARSER_HINT *hint_np = NULL;

  while (hint_p != NULL)
    {
      hint_np = sp_get_next_hint (hint_p);
      sp_free_parser_hint (hint_p);
      free (hint_p);
      hint_p = hint_np;
    }
}

const char *
sp_get_token_type (const char *sql, SP_TOKEN * token)
{
  const char *p = sql;

  *token = TT_NONE;

  switch (*(p++))
    {
    case '\'':
      *token = TT_SINGLE_QUOTED;
      break;
    case '"':
      *token = TT_DOUBLE_QUOTED;
      break;
    case '\\':
      /* escape char */
      p += 1;
      break;
    case '/':
      switch (*p)
	{
	case '/':
	  p += 1;
	  *token = TT_CPP_COMMENT;
	  break;
	case '*':
	  if (*(p + 1) == '+')
	    {
	      p += 2;
	      *token = TT_HINT;
	    }
	  else
	    {
	      p += 1;
	      *token = TT_C_COMMENT;
	    }
	  break;
	}
      break;
    case '-':
      if (*p == '-')
	{
	  p += 1;
	  *token = TT_CSQL_COMMENT;
	}
      break;
    case '*':
      if (*p == '/')
	{
	  p += 1;
	  *token = TT_C_COMMENT_END;
	}
      break;
    case '=':
      *token = TT_ASSIGN_OP;
      break;
    case '\n':
      *token = TT_NEWLINE;
      break;
    case ' ':
    case '\f':
    case '\r':
    case '\t':
    case '\v':
      *token = TT_WHITESPACE;
      if (tolower (*p) == 'i' && tolower (*(p + 1)) == 'n'
	  && (isspace (*(p + 2)) || *(p + 2) == '('))
	{
	  p += 3;
	  *token = TT_IN_OP;
	}
      break;
    case '?':
      *token = TT_BIND_CHAR;
      break;
    case '(':
      *token = TT_LEFT_BRAKET;
      break;
    case ')':
      *token = TT_RIGHT_BRAKET;
      break;
    }

  return p;
}

const char *
sp_get_hint_type (const char *sql, SP_HINT_TYPE * hint_type)
{
  sql = left_trim (sql);

  *hint_type = HT_NONE;
  if (strncasecmp (sql, "shard_", 6) != 0)
    {
      return sql;
    }

  sql += 6;
  if (strncasecmp (sql, "key", 3) == 0)
    {
      sql += 3;
      *hint_type = HT_KEY;
    }
  else if (strncasecmp (sql, "val", 3) == 0)
    {
      sql += 3;
      *hint_type = HT_VAL;
    }
  else if (strncasecmp (sql, "all", 3) == 0)
    {
      sql += 3;
      *hint_type = HT_ALL;
    }
  else if (strncasecmp (sql, "id", 2) == 0)
    {
      sql += 2;
      *hint_type = HT_ID;
    }
  return sql;
}

const char *
sp_get_hint_arg (const char *sql, SP_PARSER_HINT * hint_p, int *error)
{
  SP_TOKEN start_token;
  SP_TOKEN end_token;
  const char *p, *q;

  *error = ER_SP_INVALID_HINT;

  sql = left_trim (sql);
  sp_get_token_type (sql, &start_token);

  if (start_token != TT_LEFT_BRAKET)
    {
      if (hint_p->hint_type != HT_VAL && hint_p->hint_type != HT_ID)
	{
	  *error = NO_ERROR;
	}
      return sql;
    }

  sql = left_trim (sql + 1);
  q = p = sql;

  while (*p)
    {
      q = sp_get_token_type (p, &end_token);
      if (sp_is_pair_token (start_token, end_token))
	{
	  *error = sp_make_sp_value (&hint_p->arg, sql, p - sql);
	  return q;
	}
      p = q;
    }

  *error = ER_SP_INVALID_HINT;
  return sql;
}

const char *
sp_check_end_of_hint (const char *sql, int *error)
{
  sql = left_trim (sql);

  if (*(sql++) == '*' && *(sql++) == '/')
    {
      *error = NO_ERROR;
    }
  else
    {
      *error = ER_SP_INVALID_HINT;
    }

  return sql;
}

static int
sp_process_token (SP_PARSER_CTX * parser_p, SP_TOKEN token_type)
{
  int error = NO_ERROR;
  SP_PARSER_HINT *hint_p = NULL;
  SP_HINT_TYPE hint_type = HT_NONE;

  switch (token_type)
    {
    case TT_BIND_CHAR:
      parser_p->bind_count++;
      break;
    case TT_HINT:
      parser_p->cursor.pos =
	sp_get_hint_type (parser_p->cursor.pos, &hint_type);
      if (hint_type == HT_NONE)
	{
	  return NO_ERROR;
	}
      parser_p->cursor.token = TT_SHARD_HINT;

      hint_p = sp_create_parser_hint ();
      if (hint_p == NULL)
	{
	  error = ER_SP_OUT_OF_MEMORY;
	  goto PARSER_ERROR;
	}
      hint_p->hint_type = hint_type;

      parser_p->cursor.pos =
	sp_get_hint_arg (parser_p->cursor.pos, hint_p, &error);
      if (error != NO_ERROR)
	{
	  goto PARSER_ERROR;
	}

      parser_p->cursor.pos =
	sp_check_end_of_hint (parser_p->cursor.pos, &error);
      if (error != NO_ERROR)
	{
	  goto PARSER_ERROR;
	}

      error = sp_is_valid_hint (parser_p, hint_p);
      if (error != NO_ERROR)
	{
	  goto PARSER_ERROR;
	}

      if (hint_p->hint_type == HT_KEY)
	{
	  error = sp_get_bind_type_and_value (parser_p, hint_p);
	  if (error != NO_ERROR)
	    {
	      goto PARSER_ERROR;
	    }
	}

      if (parser_p->operator == TT_ASSIGN_OP)
	{
	  parser_p->operator = TT_NONE;
	}

      sp_append_parser_hint_to_ctx (parser_p, hint_p);
      break;
    case TT_IN_OP:
    case TT_ASSIGN_OP:
      parser_p->operator = token_type;
      break;
    case TT_RIGHT_BRAKET:
      parser_p->operator = TT_NONE;
      break;
    default:
      break;
    }

  return error;
PARSER_ERROR:
  if (hint_p != NULL)
    {
      sp_free_parser_hint (hint_p);
      free (hint_p);
    }
  return error;
}

static int
sp_get_bind_type_and_value (SP_PARSER_CTX * parser_p, SP_PARSER_HINT * hint_p)
{
  int error = NO_ERROR;

  parser_p->cursor.pos = left_trim (parser_p->cursor.pos);

  switch (*parser_p->cursor.pos)
    {
    case '?':
      hint_p->bind_type = BT_DYNAMIC;
      hint_p->bind_position = parser_p->bind_count;
      parser_p->bind_count++;
      parser_p->cursor.pos++;
      break;
    case '\'':
    case '"':
      error = sp_get_string_bind_value (parser_p, hint_p);
      parser_p->cursor.pos++;
      break;
    default:
      error = sp_get_int_bind_value (parser_p, hint_p);
      break;
    }

  if (error != NO_ERROR)
    {
      return error;
    }

  return error;
}

#if defined(WINDOWS)
static void
sp_copy_cursor_to_prv (SP_PARSER_CTX * parser_p)
#else /* WINDOWS */
static inline void
sp_copy_cursor_to_prv (SP_PARSER_CTX * parser_p)
#endif				/* !WINDOWS */
{
  parser_p->prv_cursor = parser_p->cursor;
  parser_p->cursor.token = TT_NONE;
}

static int
sp_get_string_bind_value (SP_PARSER_CTX * parser_p, SP_PARSER_HINT * hint_p)
{
  char *p;

  if (parser_p->cursor.pos == NULL)
    {
      return ER_SP_INVALID_SYNTAX;
    }
  p = (char *) parser_p->cursor.pos++;

  while (*parser_p->cursor.pos && *parser_p->cursor.pos != *p)
    {
      parser_p->cursor.pos++;
    }

  return sp_make_string_sp_value (&hint_p->value, p + 1,
				  parser_p->cursor.pos - p - 1);
}

static int
sp_get_int_bind_value (SP_PARSER_CTX * parser_p, SP_PARSER_HINT * hint_p)
{
  char *p = (char *) parser_p->cursor.pos;
  char *s = p;
  while (*s && (!isspace (*s) && *s != ',' && *s != ')'))
    {
      s++;
    }

  parser_p->cursor.pos = s;
  return sp_make_int_sp_value_from_string (&hint_p->value, p,
					   parser_p->cursor.pos - p);
}

static int
sp_is_valid_hint (SP_PARSER_CTX * parser_p, SP_PARSER_HINT * hint_p)
{
  if (parser_p->is_select)
    {
      if (hint_p->hint_type == HT_KEY && parser_p->operator != TT_ASSIGN_OP
	  && parser_p->operator != TT_IN_OP)
	{
	  return ER_SP_INVALID_SYNTAX;
	}
    }
  return NO_ERROR;
}

static bool
sp_is_start_token (SP_TOKEN token)
{
  switch (token)
    {
    case TT_C_COMMENT_END:
      return false;
    default:
      return true;
    }
}

bool
sp_is_exist_pair_token (SP_TOKEN token)
{
  switch (token)
    {
    case TT_NONE:
    case TT_WHITESPACE:
    case TT_NEWLINE:
    case TT_C_COMMENT_END:
    case TT_ASSIGN_OP:
    case TT_IN_OP:
    case TT_LEFT_BRAKET:
    case TT_RIGHT_BRAKET:
    case TT_BIND_CHAR:
    case TT_SHARD_HINT:
      return false;
    default:
      return true;
    }
}
