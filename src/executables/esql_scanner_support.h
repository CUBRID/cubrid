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
 * esql_scanner_support.h -
*/

#ifndef _ESQL_SCANNER_SUPPORT_H_
#define _ESQL_SCANNER_SUPPORT_H_

#ident "$Id$"

#include "esql_misc.h"


#define CHECK_LINENO \
    do {             \
      if (need_line_directive) \
        emit_line_directive(); \
    } while (0)


typedef struct scanner_mode_record SCANNER_MODE_RECORD;
typedef struct keyword_rec KEYWORD_REC;

struct scanner_mode_record
{
  enum scanner_mode previous_mode;
  bool recognize_keywords;
  bool suppress_echo;
  struct scanner_mode_record *previous_record;
};

enum scansup_msg
{
  MSG_EMPTY_STACK = 1,
  MSG_NOT_PERMITTED = 2
};

enum
{
  MSG_DONT_KNOW = 1,
  MSG_CHECK_CORRECTNESS,
  MSG_USING_NOT_PERMITTED,
  MSG_CURSOR_UNDEFINED,
  MSG_PTR_TO_DESCR,
  MSG_PTR_TO_DB_OBJECT,
  MSG_CHAR_STRING,
  MSG_INDICATOR_NOT_ALLOWED,
  MSG_NOT_DECLARED,
  MSG_MUST_BE_SHORT,
  MSG_INCOMPLETE_DEF,
  MSG_NOT_VALID,
  MSG_TYPE_NOT_ACCEPTABLE,
  MSG_UNKNOWN_HV_TYPE,
  MSG_BAD_ADDRESS,
  MSG_DEREF_NOT_ALLOWED,
  MSG_NOT_POINTER,
  MSG_NOT_POINTER_TO_STRUCT,
  MSG_NOT_STRUCT,
  MSG_NO_FIELD,

  ESQL_MSG_STD_ERR = 52,
  ESQL_MSG_LEX_ERROR = 53,
  ESQL_MSG_SYNTAX_ERR1 = 54,
  ESQL_MSG_SYNTAX_ERR2 = 55
};

struct keyword_rec
{
  const char *keyword;
  short value;
  short suppress_echo;		/* Ignored for C keywords */
};

typedef struct keyword_table
{
  KEYWORD_REC *keyword_array;
  int size;
} KEYWORD_TABLE;


static SCANNER_MODE_RECORD *mode_stack;
static enum scanner_mode mode;
static bool recognize_keywords;
static bool suppress_echo = false;

#if defined (ENABLE_UNUSED_FUNCTION)
static void ignore_token (void);
static void count_embedded_newlines (void);
static void echo_string_constant (const char *, int);
#endif

int check_c_identifier (char *name);
int check_identifier (KEYWORD_TABLE * keywords, char *name);

#endif /* _ESQL_SCANNER_SUPPORT_H_ */
