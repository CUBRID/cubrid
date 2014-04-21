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
   enum yytokentype {
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


#ifndef ESQL_GRAMMAR
#define ESQL_GRAMMAR

#define START 		0
#define CSQL_mode	1
#define C_mode		2
#define EXPR_mode	3
#define VAR_mode	4
#define HV_mode		5
#define BUFFER_mode	6
#define COMMENT_mode	7

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
#define ECHO_mode START
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>

#if defined(WINDOWS)
#include <io.h>
#endif

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

#ifndef __DEF_ESQL__
#define __DEF_ESQL__
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
#endif

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
enum scanner_mode esql_yy_mode (void);

char *mm_malloc (int size);
char *mm_strdup (char *str);
void mm_free (void);

extern PARSER_CONTEXT *parser;

varstring *esql_yy_get_buf (void);
void esql_yy_set_buf (varstring * vstr);
int esql_yylex(void);


extern char g_delay[];
extern bool g_indicator;
extern varstring *g_varstring;
extern varstring *g_subscript;
extern bool need_line_directive;

#endif



#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
typedef union YYSTYPE 
#line 188 "../../src/executables/esql_grammar.y"
{
  SYMBOL *p_sym;
  void *ptr;
  int number;
}
/* Line 2604 of glr.c.  */
#line 405 "../../src/executables/esql_grammar.h"
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



