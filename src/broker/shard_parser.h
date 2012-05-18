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
 * shard_parser.h -
 */

#ifndef _SHARD_PARSER_H_
#define _SHARD_PARSER_H_

#ident "$Id$"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "system.h"
#include "error_code.h"

#define SP_VALUE_INIT_SIZE 50


enum sp_error_code
{
  ER_SP_OUT_OF_MEMORY = -1,
  ER_SP_NOT_FOUND_HINT = -2,
  ER_SP_INVALID_HINT = -3,
  ER_SP_INVALID_SYNTAX = -4,
  ER_SP_DUPLICATED_HINT = -5
};

typedef enum sp_hint_type SP_HINT_TYPE;
enum sp_hint_type
{
  HT_INVAL = -1,
  HT_NONE,
  HT_KEY,
  HT_VAL,
  HT_ALL,
  HT_ID,
  HT_EOF
};

typedef enum sp_bind_type SP_BIND_TYPE;
enum sp_bind_type
{
  BT_STATIC,
  BT_DYNAMIC
};

typedef enum sp_value_type SP_VALUE_TYPE;
enum sp_value_type
{
  VT_INTEGER,
  VT_STRING
};

typedef enum sp_token SP_TOKEN;
enum sp_token
{
  TT_NONE,
  TT_SINGLE_QUOTED,
  TT_DOUBLE_QUOTED,
  TT_CSQL_COMMENT,
  TT_CPP_COMMENT,
  TT_WHITESPACE,
  TT_NEWLINE,
  TT_C_COMMENT,
  TT_C_COMMENT_END,
  TT_HINT,
  TT_SHARD_HINT,
  TT_ASSIGN_OP,
  TT_IN_OP,
  TT_LEFT_BRAKET,
  TT_RIGHT_BRAKET,
  TT_BIND_CHAR
};

typedef struct sp_cursor SP_CURSOR;
struct sp_cursor
{
  const char *pos;
  SP_TOKEN token;
};

typedef struct sp_value SP_VALUE;
struct sp_value
{
  SP_VALUE_TYPE type;
  struct
  {
    int length;
    char *value;
    char value_arr[SP_VALUE_INIT_SIZE];
    char *value_ex;
  } string;
  int integer;
};

typedef struct sp_parser_hint SP_PARSER_HINT;
struct sp_parser_hint
{
  SP_PARSER_HINT *next_a;
  SP_PARSER_HINT *next_t[2];
  SP_HINT_TYPE hint_type;
  SP_BIND_TYPE bind_type;
  int bind_position;
  SP_VALUE arg;
  SP_VALUE value;
};

typedef struct sp_parser_hint_list SP_PARSER_HINT_LIST;
struct sp_parser_hint_list
{
  SP_PARSER_HINT *head;
  SP_PARSER_HINT *tail;
  int size;
};

typedef struct sp_parser_ctx SP_PARSER_CTX;
struct sp_parser_ctx
{
  char *sql_stmt;
  bool is_select;
  int bind_count;
  SP_CURSOR cursor;
  SP_CURSOR prv_cursor;
  SP_TOKEN operator;
  SP_PARSER_HINT_LIST list_a;
  SP_PARSER_HINT_LIST list_t[2];
};

extern SP_PARSER_CTX *sp_create_parser (const char *sql_stmt);
extern int sp_parse_sql (SP_PARSER_CTX * parser_p);
extern void sp_destroy_parser (SP_PARSER_CTX * parser_p);
extern bool sp_is_hint_static (SP_PARSER_CTX * parser_p);
extern int sp_get_total_hint_count (SP_PARSER_CTX * parser_p);
extern int sp_get_static_hint_count (SP_PARSER_CTX * parser_p);
extern int sp_get_dynamic_hint_count (SP_PARSER_CTX * parser_p);
extern SP_PARSER_HINT *sp_get_first_hint (SP_PARSER_CTX * parser_p);
extern SP_PARSER_HINT *sp_get_next_hint (SP_PARSER_HINT * hint_p);
extern SP_PARSER_HINT *sp_get_first_static_hint (SP_PARSER_CTX * parser_p);
extern SP_PARSER_HINT *sp_get_next_static_hint (SP_PARSER_HINT * hint_p);
extern SP_PARSER_HINT *sp_get_first_dynamic_hint (SP_PARSER_CTX * parser_p);
extern SP_PARSER_HINT *sp_get_next_dynamic_hint (SP_PARSER_HINT * hint_p);
extern char *sp_get_hint_key (SP_PARSER_HINT * hint_p);
extern const char *sp_get_sql_stmt (SP_PARSER_CTX * parser_p);
extern const char *sp_get_token_type (const char *sql, SP_TOKEN * token);
extern bool sp_is_pair_token (SP_TOKEN start_token, SP_TOKEN end_token);
extern bool sp_is_exist_pair_token (SP_TOKEN token);
extern SP_PARSER_HINT *sp_create_parser_hint (void);
extern void sp_free_parser_hint (SP_PARSER_HINT * hint_p);
extern const char *sp_get_hint_type (const char *sql,
				     SP_HINT_TYPE * hint_type);
extern const char *sp_get_hint_arg (const char *sql, SP_PARSER_HINT * hint_p,
				    int *error);
extern const char *sp_check_end_of_hint (const char *sql, int *error);

#endif
