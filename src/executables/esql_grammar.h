/* A Bison parser, made by GNU Bison 2.3.  */

/* Skeleton interface for Bison GLR parsers in C

   Copyright (C) 2002, 2003, 2004, 2005, 2006 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* Tokens.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
   /* Put the tokens into the symbol table, so that GDB and other debuggers
      know about them.  */
enum yytokentype
{
  ADD = 258,
  ALL = 259,
  ALTER = 260,
  AND = 261,
  AS = 262,
  ASC = 263,
  ATTRIBUTE = 264,
  ATTACH = 265,
  AUTO_ = 266,
  AVG = 267,
  BAD_TOKEN = 268,
  BOGUS_ECHO = 269,
  BEGIN_ = 270,
  BETWEEN = 271,
  BIT_ = 272,
  BY = 273,
  CALL_ = 274,
  CHANGE = 275,
  CHAR_ = 276,
  CHARACTER = 277,
  CHECK_ = 278,
  CLASS = 279,
  CLOSE = 280,
  COMMIT = 281,
  CONNECT = 282,
  CONST_ = 283,
  CONTINUE_ = 284,
  COUNT = 285,
  CREATE = 286,
  CURRENT = 287,
  CURSOR_ = 288,
  DATE_ = 289,
  DATE_LIT = 290,
  DECLARE = 291,
  DEFAULT = 292,
  DELETE_ = 293,
  DESC = 294,
  DESCRIBE = 295,
  DESCRIPTOR = 296,
  DIFFERENCE_ = 297,
  DISCONNECT = 298,
  DISTINCT = 299,
  DOUBLE_ = 300,
  DROP = 301,
  ELLIPSIS = 302,
  END = 303,
  ENUM = 304,
  EQ = 305,
  ESCAPE = 306,
  EVALUATE = 307,
  EXCEPT = 308,
  EXCLUDE = 309,
  EXECUTE = 310,
  EXISTS = 311,
  EXTERN_ = 312,
  FETCH = 313,
  FILE_ = 314,
  FILE_PATH_LIT = 315,
  FLOAT_ = 316,
  FOR = 317,
  FOUND = 318,
  FROM = 319,
  FUNCTION_ = 320,
  GENERIC_TOKEN = 321,
  GEQ = 322,
  GET = 323,
  GO = 324,
  GOTO_ = 325,
  GRANT = 326,
  GROUP_ = 327,
  GT = 328,
  HAVING = 329,
  IDENTIFIED = 330,
  IMMEDIATE = 331,
  IN_ = 332,
  INCLUDE = 333,
  INDEX = 334,
  INDICATOR = 335,
  INHERIT = 336,
  INSERT = 337,
  INTEGER = 338,
  INTERSECTION = 339,
  INTO = 340,
  INT_ = 341,
  INT_LIT = 342,
  IS = 343,
  LDBVCLASS = 344,
  LEQ = 345,
  LIKE = 346,
  LONG_ = 347,
  LT = 348,
  MAX_ = 349,
  METHOD = 350,
  MIN_ = 351,
  MINUS = 352,
  MONEY_LIT = 353,
  MULTISET_OF = 354,
  NEQ = 355,
  NOT = 356,
  NULL_ = 357,
  NUMERIC = 358,
  OBJECT = 359,
  OF = 360,
  OID_ = 361,
  ON_ = 362,
  ONLY = 363,
  OPEN = 364,
  OPTION = 365,
  OR = 366,
  ORDER = 367,
  PLUS = 368,
  PRECISION = 369,
  PREPARE = 370,
  PRIVILEGES = 371,
  PTR_OP = 372,
  PUBLIC_ = 373,
  READ = 374,
  READ_ONLY_ = 375,
  REAL = 376,
  REAL_LIT = 377,
  REGISTER_ = 378,
  RENAME = 379,
  REPEATED = 380,
  REVOKE = 381,
  ROLLBACK = 382,
  SECTION = 383,
  SELECT = 384,
  SEQUENCE_OF = 385,
  SET = 386,
  SETEQ = 387,
  SETNEQ = 388,
  SET_OF = 389,
  SHARED = 390,
  SHORT_ = 391,
  SIGNED_ = 392,
  SLASH = 393,
  SMALLINT = 394,
  SOME = 395,
  SQLCA = 396,
  SQLDA = 397,
  SQLERROR_ = 398,
  SQLWARNING_ = 399,
  STATIC_ = 400,
  STATISTICS = 401,
  STOP_ = 402,
  STRING = 403,
  STRUCT_ = 404,
  SUBCLASS = 405,
  SUBSET = 406,
  SUBSETEQ = 407,
  SUM = 408,
  SUPERCLASS = 409,
  SUPERSET = 410,
  SUPERSETEQ = 411,
  TABLE = 412,
  TIME = 413,
  TIMESTAMP = 414,
  TIME_LIT = 415,
  TO = 416,
  TRIGGER = 417,
  TYPEDEF_ = 418,
  UNION_ = 419,
  UNIQUE = 420,
  UNSIGNED_ = 421,
  UPDATE = 422,
  USE = 423,
  USER = 424,
  USING = 425,
  UTIME = 426,
  VALUES = 427,
  VARBIT_ = 428,
  VARCHAR_ = 429,
  VARYING = 430,
  VCLASS = 431,
  VIEW = 432,
  VOID_ = 433,
  VOLATILE_ = 434,
  WHENEVER = 435,
  WHERE = 436,
  WITH = 437,
  WORK = 438,
  ENUMERATION_CONSTANT = 439,
  TYPEDEF_NAME = 440,
  STRING_LIT = 441,
  IDENTIFIER = 442,
  EXEC = 443,
  SQLX = 444,
  UNION = 445,
  STRUCT = 446
};
#endif


/* Copy the first part of user declarations.  */
#line 26 "../../src/executables/esql_grammar.y"
/*%CODE_REQUIRES_START%*/
#include "esql_host_variable.h"
/*%CODE_END%*/
#line 30 "../../src/executables/esql_grammar.y"
/*%CODE_PROVIDES_START%*/
#include "esql_scanner_support.h"

#define START 		0
#define ECHO_mode START
#define CSQL_mode	1
#define C_mode		2
#define EXPR_mode	3
#define VAR_mode	4
#define HV_mode		5
#define BUFFER_mode	6
#define COMMENT_mode	7

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

extern char g_delay[];
extern bool g_indicator;
extern bool need_line_directive;

enum scanner_mode esql_yy_mode (void);
char *mm_malloc (int size);
char *mm_strdup (char *str);
void mm_free (void);
/*%CODE_END%*/
#line 262 "../../src/executables/esql_grammar.y"

#ifndef ESQL_GRAMMAR
#define ESQL_GRAMMAR

/* #define PARSER_DEBUG */

#ifdef PARSER_DEBUG

#define DBG_PRINT printf("rule: %d\n", __LINE__);
#define PARSER_NEW_NODE(a, b) __make_node(a, b, __LINE__)
#define PARSER_FREE_NODE(a, b) parser_free_node(a, b)
#define PRINT_(a) printf(a)
#define PRINT_1(a, b) printf(a, b)

#else

#define DBG_PRINT
#define PARSER_NEW_NODE(a, b) parser_new_node(a, b)
#define PARSER_FREE_NODE(a, b) parser_free_node(a, b)
#define PRINT_(a)
#define PRINT_1(a, b)

#endif

#if defined(WINDOWS)
#define inline
#endif

#if !defined(yytext_ptr)
extern char *esql_yytext;
extern int esql_yylineno;
#endif

#include "config.h"
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>

#ifdef HAVE_GETOPT_H
#include <getopt.h>
#else
#include "getopt.h"
#endif

#include "language_support.h"
#include "message_catalog.h"
#include "variable_string.h"
#include "parser.h"
#include "esql_translate.h"
#include "memory_alloc.h"
#include "misc_string.h"
#include "esql_misc.h"
#include "environment_variable.h"
#include "utility.h"
#include "esql_scanner_support.h"
#define ESQLMSG(x) pp_get_msg(EX_ESQLM_SET, (x))

#define NO_CURSOR          0
#define CURSOR_DELETE      1
#define CURSOR_UPDATE      2

static SYMBOL *g_identifier_symbol;
static STRUCTDEF *g_sdef2;
static int g_su;
static SYMBOL *g_psym;
static SYMBOL *g_struct_declaration_sym;
static SYMBOL *g_last;
static HOST_VAR *g_href;
static int g_cursor_type;

static bool repeat_option;
static PTR_VEC id_list;
static HOST_LOD *input_refs, *output_refs;
static bool already_translated;
static bool structs_allowed;
static bool hvs_allowed;
static int i;
static int n_refs;

static void reset_globals (void);
static void ignore_repeat (void);
static char *ofile_name (char *fname);
#if defined (ENABLE_UNUSED_FUNCTION)
static int validate_input_file (void *fname, FILE * outfp);
#endif
void pt_print_errors (PARSER_CONTEXT * parser_in);

extern PARSER_CONTEXT *parser;

varstring *esql_yy_get_buf (void);
void esql_yy_set_buf (varstring * vstr);
int esql_yylex (void);


extern varstring *g_varstring;
extern varstring *g_subscript;
#endif



#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
typedef union YYSTYPE
#line 367 "../../src/executables/esql_grammar.y"
{
  SYMBOL *p_sym;
  void *ptr;
  int number;
}
/* Line 2604 of glr.c.  */
#line 581 "../../src/executables/esql_grammar.h"
YYSTYPE;
# define YYSTYPE_IS_DECLARED 1
# define YYSTYPE_IS_TRIVIAL 1
#endif

#if ! defined YYLTYPE && ! defined YYLTYPE_IS_DECLARED
typedef struct YYLTYPE
{

  char yydummy;

} YYLTYPE;
# define YYLTYPE_IS_DECLARED 1
# define YYLTYPE_IS_TRIVIAL 1
#endif


extern YYSTYPE esql_yylval;
