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

static KEYWORD_REC c_keywords[] = {
  {"auto", AUTO_, 0},
  {"BIT", BIT_, 1},
  {"bit", BIT_, 1},
  {"break", GENERIC_TOKEN, 0},
  {"case", GENERIC_TOKEN, 0},
  {"char", CHAR_, 0},
  {"const", CONST_, 0},
  {"continue", GENERIC_TOKEN, 0},
  {"default", GENERIC_TOKEN, 0},
  {"do", GENERIC_TOKEN, 0},
  {"double", DOUBLE_, 0},
  {"else", GENERIC_TOKEN, 0},
  {"enum", ENUM, 0},
  {"extern", EXTERN_, 0},
  {"float", FLOAT_, 0},
  {"for", GENERIC_TOKEN, 0},
  {"go", GENERIC_TOKEN, 0},
  {"goto", GENERIC_TOKEN, 0},
  {"if", GENERIC_TOKEN, 0},
  {"int", INT_, 0},
  {"long", LONG_, 0},
  {"register", REGISTER_, 0},
  {"return", GENERIC_TOKEN, 0},
  {"short", SHORT_, 0},
  {"signed", SIGNED_, 0},
  {"sizeof", GENERIC_TOKEN, 0},
  {"static", STATIC_, 0},
  {"struct", STRUCT_, 0},
  {"switch", GENERIC_TOKEN, 0},
  {"typedef", TYPEDEF_, 0},
  {"union", UNION_, 0},
  {"unsigned", UNSIGNED_, 0},
  {"VARCHAR", VARCHAR_, 1},
  {"varchar", VARCHAR_, 1},
  {"VARYING", VARYING, 0},
  {"varying", VARYING, 0},
  {"void", VOID_, 0},
  {"volatile", VOLATILE_, 0},
  {"while", GENERIC_TOKEN, 0},
};

static KEYWORD_REC csql_keywords[] = {
  /* Make sure that they are in alphabetical order */
  {"ADD", ADD, 0},
  {"ALL", ALL, 0},
  {"ALTER", ALTER, 0},
  {"AND", AND, 0},
  {"AS", AS, 0},
  {"ASC", ASC, 0},
  {"ATTACH", ATTACH, 0},
  {"ATTRIBUTE", ATTRIBUTE, 0},
  {"AVG", AVG, 0},
  {"BEGIN", BEGIN_, 1},
  {"BETWEEN", BETWEEN, 0},
  {"BY", BY, 0},
  {"CALL", CALL_, 0},
  {"CHANGE", CHANGE, 0},
  {"CHAR", CHAR_, 0},
  {"CHARACTER", CHARACTER, 0},
  {"CHECK", CHECK_, 0},
  {"CLASS", CLASS, 0},
  {"CLOSE", CLOSE, 0},
  {"COMMIT", COMMIT, 0},
  {"CONNECT", CONNECT, 1},
  {"CONTINUE", CONTINUE_, 1},
  {"COUNT", COUNT, 0},
  {"CREATE", CREATE, 0},
  {"CURRENT", CURRENT, 1},
  {"CURSOR", CURSOR_, 1},
  {"DATE", DATE_, 0},
  {"DEC", NUMERIC, 0},
  {"DECIMAL", NUMERIC, 0},
  {"DECLARE", DECLARE, 1},
  {"DEFAULT", DEFAULT, 0},
  {"DELETE", DELETE_, 0},
  {"DESC", DESC, 0},
  {"DESCRIBE", DESCRIBE, 1},
  {"DESCRIPTOR", DESCRIPTOR, 1},
  {"DIFFERENCE", DIFFERENCE_, 0},
  {"DISCONNECT", DISCONNECT, 1},
  {"DISTINCT", DISTINCT, 0},
  {"DOUBLE", DOUBLE_, 0},
  {"DROP", DROP, 0},
  {"END", END, 1},
  {"ESCAPE", ESCAPE, 0},
  {"EVALUATE", EVALUATE, 0},
  {"EXCEPT", EXCEPT, 0},
  {"EXCLUDE", EXCLUDE, 0},
  {"EXECUTE", EXECUTE, 1},
  {"EXISTS", EXISTS, 0},
  {"FETCH", FETCH, 1},
  {"FILE", FILE_, 0},
  {"FLOAT", FLOAT_, 0},
  {"FOR", FOR, 0},
  {"FOUND", FOUND, 0},
  {"FROM", FROM, 0},
  {"FUNCTION", FUNCTION_, 0},
  {"GET", GET, 0},
  {"GO", GO, 1},
  {"GOTO", GOTO_, 1},
  {"GRANT", GRANT, 0},
  {"GROUP", GROUP_, 0},
  {"HAVING", HAVING, 0},
  {"IDENTIFIED", IDENTIFIED, 1},
  {"IMMEDIATE", IMMEDIATE, 1},
  {"IN", IN_, 0},
  {"INCLUDE", INCLUDE, 1},
  {"INDEX", INDEX, 0},
  {"INDICATOR", INDICATOR, 1},
  {"INHERIT", INHERIT, 0},
  {"INSERT", INSERT, 0},
  {"INT", INT_, 0},
  {"INTEGER", INTEGER, 0},
  {"INTERSECTION", INTERSECTION, 0},
  {"INTO", INTO, 0},
  {"IS", IS, 0},
  {"LIKE", LIKE, 0},
  {"MAX", MAX_, 0},
  {"METHOD", METHOD, 0},
  {"MIN", MIN_, 0},
  {"MULTISET_OF", MULTISET_OF, 0},
  {"NOT", NOT, 0},
  {"NULL", NULL_, 0},
  {"NUMBER", NUMERIC, 0},
  {"NUMERIC", NUMERIC, 0},
  {"OBJECT", OBJECT, 0},
  {"OF", OF, 0},
  {"OID", OID_, 0},
  {"ON", ON_, 0},
  {"ONLY", ONLY, 0},
  {"OPEN", OPEN, 0},
  {"OPTION", OPTION, 0},
  {"OR", OR, 0},
  {"ORDER", ORDER, 0},
  {"PRECISION", PRECISION, 0},
  {"PREPARE", PREPARE, 1},
  {"PRIVILEGES", PRIVILEGES, 0},
  {"PUBLIC", PUBLIC_, 0},
  {"READ", READ, 0},
  {"REAL", REAL, 0},
  {"REGISTER", REGISTER_, 0},
  {"RENAME", RENAME, 0},
  {"REPEATED", REPEATED, 1},
  {"REVOKE", REVOKE, 0},
  {"ROLLBACK", ROLLBACK, 0},
  {"SECTION", SECTION, 1},
  {"SELECT", SELECT, 0},
  {"SEQUENCE_OF", SEQUENCE_OF, 0},
  {"SET", SET, 0},
  {"SETEQ", SETEQ, 0},
  {"SETNEQ", SETNEQ, 0},
  {"SET_OF", SET_OF, 0},
  {"SHARED", SHARED, 0},
  {"SMALLINT", SMALLINT, 0},
  {"SOME", SOME, 0},
  {"SQLCA", SQLCA, 1},
  {"SQLDA", SQLDA, 1},
  {"SQLERROR", SQLERROR_, 1},
  {"SQLM", SQLX, 1},
  {"SQLWARNING", SQLWARNING_, 1},
  {"STATISTICS", STATISTICS, 0},
  {"STOP", STOP_, 1},
  {"STRING", STRING, 0},
  {"SUBCLASS", SUBCLASS, 0},
  {"SUBSET", SUBSET, 0},
  {"SUBSETEQ", SUBSETEQ, 0},
  {"SUM", SUM, 0},
  {"SUPERCLASS", SUPERCLASS, 0},
  {"SUPERSET", SUPERSET, 0},
  {"SUPERSETEQ", SUPERSETEQ, 0},
  {"TABLE", TABLE, 0},
  {"TIME", TIME, 0},
  {"TIMESTAMP", TIMESTAMP, 0},
  {"TO", TO, 0},
  {"TRIGGER", TRIGGER, 0},
  {"UNION", UNION_, 0},
  {"UNIQUE", UNIQUE, 0},
  {"UPDATE", UPDATE, 0},
  {"USE", USE, 0},
  {"USER", USER, 0},
  {"USING", USING, 0},
  {"UTIME", UTIME, 0},
  {"VALUES", VALUES, 0},
  {"VIEW", VIEW, 0},
  {"WHENEVER", WHENEVER, 1},
  {"WHERE", WHERE, 0},
  {"WITH", WITH, 0},
  {"WORK", WORK, 0},
};

static KEYWORD_REC preprocessor_keywords[] = {
  /* Make sure that they are in alphabetical order */
  {"FROM", FROM, 0},
  {"IDENTIFIED", IDENTIFIED, 0},
  {"INDICATOR", INDICATOR, 1},
  {"INTO", INTO, 0},
  {"ON", ON_, 0},
  {"SELECT", SELECT, 0},
  {"TO", TO, 0},
  {"VALUES", VALUES, 0},
  {"WITH", WITH, 0},
};

static KEYWORD_TABLE csql_table = { csql_keywords, DIM (csql_keywords) };
static KEYWORD_TABLE preprocessor_table = { preprocessor_keywords,
  DIM (preprocessor_keywords)
};


static void ignore_token (void);
static void count_embedded_newlines (void);
static void echo_string_constant (const char *, int);

int check_c_identifier (char *name);
int check_identifier (KEYWORD_TABLE * keywords, char *name);

#endif /* _ESQL_SCANNER_SUPPORT_H_ */
