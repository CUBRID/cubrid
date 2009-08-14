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
     ABSOLUTE_ = 258,
     ACTION = 259,
     ADD = 260,
     ADD_MONTHS = 261,
     AFTER = 262,
     ALIAS = 263,
     ALL = 264,
     ALLOCATE = 265,
     ALTER = 266,
     AND = 267,
     ANY = 268,
     ARE = 269,
     AS = 270,
     ASC = 271,
     ASSERTION = 272,
     ASYNC = 273,
     AT = 274,
     ATTACH = 275,
     ATTRIBUTE = 276,
     AVG = 277,
     BEFORE = 278,
     BEGIN_ = 279,
     BETWEEN = 280,
     BIGINT = 281,
     BIT = 282,
     BIT_LENGTH = 283,
     BOOLEAN_ = 284,
     BOTH_ = 285,
     BREADTH = 286,
     BY = 287,
     CALL = 288,
     CASCADE = 289,
     CASCADED = 290,
     CASE = 291,
     CAST = 292,
     CATALOG = 293,
     CHANGE = 294,
     CHAR_ = 295,
     CHARACTER_LENGTH = 296,
     CHECK = 297,
     CLASS = 298,
     CLASSES = 299,
     CLOSE = 300,
     CLUSTER = 301,
     COALESCE = 302,
     COLLATE = 303,
     COLLATION = 304,
     COLUMN = 305,
     COMMIT = 306,
     COMPLETION = 307,
     CONNECT = 308,
     CONNECTION = 309,
     CONSTRAINT = 310,
     CONSTRAINTS = 311,
     CONTINUE = 312,
     CONVERT = 313,
     CORRESPONDING = 314,
     COUNT = 315,
     CREATE = 316,
     CROSS = 317,
     CURRENT = 318,
     CURRENT_DATE = 319,
     CURRENT_DATETIME = 320,
     CURRENT_TIME = 321,
     CURRENT_TIMESTAMP = 322,
     CURRENT_USER = 323,
     CURSOR = 324,
     CYCLE = 325,
     DATA = 326,
     DATA_TYPE = 327,
     Date = 328,
     DAY_ = 329,
     DEALLOCATE = 330,
     DECLARE = 331,
     DEFAULT = 332,
     DEFERRABLE = 333,
     DEFERRED = 334,
     DELETE_ = 335,
     DEPTH = 336,
     DESC = 337,
     DESCRIBE = 338,
     DESCRIPTOR = 339,
     DIAGNOSTICS = 340,
     DICTIONARY = 341,
     DIFFERENCE_ = 342,
     DISCONNECT = 343,
     DISTINCT = 344,
     Domain = 345,
     Double = 346,
     DROP = 347,
     EACH = 348,
     ELSE = 349,
     ELSEIF = 350,
     END = 351,
     EQUALS = 352,
     ESCAPE = 353,
     EVALUATE = 354,
     EXCEPT = 355,
     EXCEPTION = 356,
     EXCLUDE = 357,
     EXEC = 358,
     EXECUTE = 359,
     EXISTS = 360,
     EXTERNAL = 361,
     EXTRACT = 362,
     False = 363,
     FETCH = 364,
     File = 365,
     FIRST = 366,
     FLOAT_ = 367,
     For = 368,
     FOREIGN = 369,
     FOUND = 370,
     FROM = 371,
     FULL = 372,
     FUNCTION = 373,
     GENERAL = 374,
     GET = 375,
     GLOBAL = 376,
     GO = 377,
     GOTO = 378,
     GRANT = 379,
     GROUP_ = 380,
     HAVING = 381,
     HOUR_ = 382,
     IDENTITY = 383,
     IF = 384,
     IGNORE_ = 385,
     IMMEDIATE = 386,
     IN_ = 387,
     INDEX = 388,
     INDICATOR = 389,
     INHERIT = 390,
     INITIALLY = 391,
     INNER = 392,
     INOUT = 393,
     INPUT_ = 394,
     INSERT = 395,
     INTEGER = 396,
     INTERSECT = 397,
     INTERSECTION = 398,
     INTERVAL = 399,
     INTO = 400,
     IS = 401,
     ISOLATION = 402,
     JOIN = 403,
     KEY = 404,
     LANGUAGE = 405,
     LAST = 406,
     LDB = 407,
     LEADING_ = 408,
     LEAVE = 409,
     LEFT = 410,
     LESS = 411,
     LEVEL = 412,
     LIKE = 413,
     LIMIT = 414,
     LIST = 415,
     LOCAL = 416,
     LOCAL_TRANSACTION_ID = 417,
     LOOP = 418,
     LOWER = 419,
     MATCH = 420,
     Max = 421,
     METHOD = 422,
     Min = 423,
     MINUTE_ = 424,
     MODIFY = 425,
     MODULE = 426,
     Monetary = 427,
     MONTH_ = 428,
     MULTISET = 429,
     MULTISET_OF = 430,
     NA = 431,
     NAMES = 432,
     NATIONAL = 433,
     NATURAL = 434,
     NCHAR = 435,
     NEXT = 436,
     NO = 437,
     NONE = 438,
     NOT = 439,
     NULLIF = 440,
     NUMERIC = 441,
     Null = 442,
     OBJECT = 443,
     OCTET_LENGTH = 444,
     OF = 445,
     OFF_ = 446,
     OID_ = 447,
     ON_ = 448,
     ONLY = 449,
     OPEN = 450,
     OPERATION = 451,
     OPERATORS = 452,
     OPTIMIZATION = 453,
     OPTION = 454,
     OR = 455,
     ORDER = 456,
     OTHERS = 457,
     OUT_ = 458,
     OUTER = 459,
     OUTPUT = 460,
     OVERLAPS = 461,
     PARAMETERS = 462,
     PARTIAL = 463,
     PENDANT = 464,
     POSITION = 465,
     PRECISION = 466,
     PREORDER = 467,
     PREPARE = 468,
     PRESERVE = 469,
     PRIMARY = 470,
     PRIOR = 471,
     Private = 472,
     PRIVILEGES = 473,
     PROXY = 474,
     PROCEDURE = 475,
     PROTECTED = 476,
     QUERY = 477,
     READ = 478,
     REBUILD = 479,
     RECURSIVE = 480,
     REF = 481,
     REFERENCES = 482,
     REFERENCING = 483,
     REGISTER = 484,
     RELATIVE_ = 485,
     RENAME = 486,
     REPLACE = 487,
     RESIGNAL = 488,
     RESTRICT = 489,
     RETURN = 490,
     RETURNS = 491,
     REVOKE = 492,
     RIGHT = 493,
     ROLE = 494,
     ROLLBACK = 495,
     ROUTINE = 496,
     ROW = 497,
     ROWNUM = 498,
     ROWS = 499,
     SAVEPOINT = 500,
     SCHEMA = 501,
     SCOPE = 502,
     SCROLL = 503,
     SEARCH = 504,
     SECOND_ = 505,
     MILLISECOND_ = 506,
     SECTION = 507,
     SELECT = 508,
     SENSITIVE = 509,
     SEQUENCE = 510,
     SEQUENCE_OF = 511,
     SERIALIZABLE = 512,
     SESSION = 513,
     SESSION_USER = 514,
     SET = 515,
     SETEQ = 516,
     SETNEQ = 517,
     SET_OF = 518,
     SHARED = 519,
     SIGNAL = 520,
     SIMILAR = 521,
     SIZE_ = 522,
     SmallInt = 523,
     SOME = 524,
     SQL = 525,
     SQLCODE = 526,
     SQLERROR = 527,
     SQLEXCEPTION = 528,
     SQLSTATE = 529,
     SQLWARNING = 530,
     STATISTICS = 531,
     String = 532,
     STRUCTURE = 533,
     SUBCLASS = 534,
     SUBSET = 535,
     SUBSETEQ = 536,
     SUBSTRING_ = 537,
     SUM = 538,
     SUPERCLASS = 539,
     SUPERSET = 540,
     SUPERSETEQ = 541,
     SYSTEM_USER = 542,
     SYS_DATE = 543,
     SYS_DATETIME = 544,
     SYS_TIME_ = 545,
     SYS_TIMESTAMP = 546,
     SYS_USER = 547,
     TABLE = 548,
     TEMPORARY = 549,
     TEST = 550,
     THEN = 551,
     THERE = 552,
     Time = 553,
     TIMESTAMP = 554,
     DATETIME = 555,
     TIMEZONE_HOUR = 556,
     TIMEZONE_MINUTE = 557,
     TO = 558,
     TRAILING_ = 559,
     TRANSACTION = 560,
     TRANSLATE = 561,
     TRANSLATION = 562,
     TRIGGER = 563,
     TRIM = 564,
     True = 565,
     TYPE = 566,
     UNDER = 567,
     Union = 568,
     UNIQUE = 569,
     UNKNOWN = 570,
     UPDATE = 571,
     UPPER = 572,
     USAGE = 573,
     USE = 574,
     USER = 575,
     USING = 576,
     Utime = 577,
     VALUE = 578,
     VALUES = 579,
     VARCHAR = 580,
     VARIABLE_ = 581,
     VARYING = 582,
     VCLASS = 583,
     VIEW = 584,
     VIRTUAL = 585,
     VISIBLE = 586,
     WAIT = 587,
     WHEN = 588,
     WHENEVER = 589,
     WHERE = 590,
     WHILE = 591,
     WITH = 592,
     WITHOUT = 593,
     WORK = 594,
     WRITE = 595,
     YEAR_ = 596,
     ZONE = 597,
     YEN_SIGN = 598,
     DOLLAR_SIGN = 599,
     WON_SIGN = 600,
     RIGHT_ARROW = 601,
     STRCAT = 602,
     COMP_NOT_EQ = 603,
     COMP_GE = 604,
     COMP_LE = 605,
     PARAM_HEADER = 606,
     ACTIVE = 607,
     ANALYZE = 608,
     AUTO_INCREMENT = 609,
     COST = 610,
     COMMITTED = 611,
     CACHE = 612,
     DECREMENT = 613,
     GROUPS = 614,
     GE_INF_ = 615,
     GE_LE_ = 616,
     GE_LT_ = 617,
     GT_INF_ = 618,
     GT_LE_ = 619,
     GT_LT_ = 620,
     HASH = 621,
     INVALIDATE = 622,
     INSTANCES = 623,
     INF_LE_ = 624,
     INF_LT_ = 625,
     INFINITE_ = 626,
     INACTIVE = 627,
     INCREMENT = 628,
     JAVA = 629,
     LOCK_ = 630,
     MAXIMUM = 631,
     MAXVALUE = 632,
     MEMBERS = 633,
     MINVALUE = 634,
     NAME = 635,
     NOCYCLE = 636,
     NOMAXVALUE = 637,
     NOMINVALUE = 638,
     PARTITION = 639,
     PARTITIONING = 640,
     PARTITIONS = 641,
     PASSWORD = 642,
     PRINT = 643,
     PRIORITY = 644,
     RANGE_ = 645,
     REJECT_ = 646,
     REMOVE = 647,
     REORGANIZE = 648,
     REPEATABLE = 649,
     RETAIN = 650,
     REVERSE = 651,
     SERIAL = 652,
     STABILITY = 653,
     START_ = 654,
     STATEMENT = 655,
     STATUS = 656,
     STDDEV = 657,
     SYSTEM = 658,
     THAN = 659,
     TIMEOUT = 660,
     TRACE = 661,
     TRIGGERS = 662,
     UNCOMMITTED = 663,
     VARIANCE = 664,
     WORKSPACE = 665,
     IdName = 666,
     BracketDelimitedIdName = 667,
     DelimitedIdName = 668,
     UNSIGNED_INTEGER = 669,
     UNSIGNED_REAL = 670,
     CHAR_STRING = 671,
     NCHAR_STRING = 672,
     BIT_STRING = 673,
     HEX_STRING = 674,
     CPP_STYLE_HINT = 675,
     C_STYLE_HINT = 676,
     SQL_STYLE_HINT = 677
   };
#endif


/* Copy the first part of user declarations.  */
#line 28 "../../src/parser/csql_grammar.y"

#define YYMAXDEPTH	1000000

/* #define PARSER_DEBUG */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include "parser.h"
#include "parser_message.h"
#include "dbdef.h"
#include "language_support.h"
#include "environment_variable.h"
#include "transaction_cl.h"
#include "csql_grammar_scan.h"
#define JP_MAXNAME 256
#if defined(WINDOWS)
#define snprintf _snprintf
#endif /* WINDOWS */
#include "memory_alloc.h"



#ifdef PARSER_DEBUG
#define DBG_PRINT printf("rule matched at line: %d\n", __LINE__);
#define PRINT_(a) printf(a)
#define PRINT_1(a, b) printf(a, b)
#define PRINT_2(a, b, c) printf(a, b, c)
#else
#define DBG_PRINT
#define PRINT_(a)
#define PRINT_1(a, b)
#define PRINT_2(a, b, c)
#endif

#define STACK_SIZE	128

typedef struct function_map FUNCTION_MAP;
struct function_map
{
  const char* keyword;
  PT_OP_TYPE op;
};


static FUNCTION_MAP functions[] = {
  {"abs", PT_ABS},
  {"ceil", PT_CEIL},
  {"char_length", PT_CHAR_LENGTH},
  {"chr", PT_CHR},
  {"decode", PT_DECODE},
  {"decr", PT_DECR},
  {"drand", PT_DRAND},
  {"drandom", PT_DRANDOM},
  {"exp", PT_EXP},
  {"floor", PT_FLOOR},
  {"greatest", PT_GREATEST},
  {"groupby_num", PT_GROUPBY_NUM},
  {"incr", PT_INCR},
  {"inst_num", PT_INST_NUM},
  {"instr", PT_INSTR},
  {"instrb", PT_INSTR},
  {"last_day", PT_LAST_DAY},
  {"length", PT_CHAR_LENGTH},
  {"lengthb", PT_CHAR_LENGTH},
  {"least", PT_LEAST},
  {"log", PT_LOG},
  {"lpad", PT_LPAD},
  {"ltrim", PT_LTRIM},
  {"mod", PT_MODULUS},
  {"months_between", PT_MONTHS_BETWEEN},
  {"nvl", PT_NVL},
  {"nvl2", PT_NVL2},
  {"orderby_num", PT_ORDERBY_NUM},
  {"power", PT_POWER},
  {"rand", PT_RAND},
  {"random", PT_RANDOM},
  {"round", PT_ROUND},
  {"rpad", PT_RPAD},
  {"rtrim", PT_RTRIM},
  {"sign", PT_SIGN},
  {"sqrt", PT_SQRT},
  {"substr", PT_SUBSTRING},
  {"substrb", PT_SUBSTRING},
  {"to_char", PT_TO_CHAR},
  {"to_date", PT_TO_DATE},
  {"to_datetime",PT_TO_DATETIME},
  {"to_number", PT_TO_NUMBER},
  {"to_time", PT_TO_TIME},
  {"to_timestamp", PT_TO_TIMESTAMP},
  {"trunc", PT_TRUNC},
};


static int parser_groupby_exception = 0;




/* xxxnum_check: 0 not allowed, no compatibility check
		 1 allowed, compatibility check (search_condition)
		 2 allowed, no compatibility check (select__list) */
static int parser_instnum_check = 0;
static int parser_groupbynum_check = 0;
static int parser_orderbynum_check = 0;
static int parser_within_join_condition = 0;

/* check Oracle style outer-join operatior: '(+)' */
static bool parser_found_Oracle_outer = false;

/* check sys_date, sys_time, sys_timestamp, sys_datetime local_transaction_id */
static bool parser_si_datetime = false;
static bool parser_si_tran_id = false;

/* check the condition that the statment is not able to be prepared */
static bool parser_cannot_prepare = false;

/* check the condition that the result of a query is not able to be cached */
static bool parser_cannot_cache = false;

/* check if INCR is used legally */
static int parser_select_level = -1;

/* handle inner increment exprs in select list */
static PT_NODE *parser_hidden_incr_list = NULL;

typedef struct {
	PT_NODE* c1;
	PT_NODE* c2;
} container_2;

typedef struct {
	PT_NODE* c1;
	PT_NODE* c2;
	PT_NODE* c3;
} container_3;

typedef struct {
	PT_NODE* c1;
	PT_NODE* c2;
	PT_NODE* c3;
	PT_NODE* c4;
} container_4;

#define PT_EMPTY INT_MAX

#if defined(WINDOWS)
#define inline
#endif


#define TO_NUMBER(a)			((UINTPTR)(a))
#define FROM_NUMBER(a)			((PT_NODE*)(UINTPTR)(a))


#define SET_CONTAINER_2(a, i, j)		a.c1 = i, a.c2 = j
#define SET_CONTAINER_3(a, i, j, k)		a.c1 = i, a.c2 = j, a.c3 = k
#define SET_CONTAINER_4(a, i, j, k, l)		a.c1 = i, a.c2 = j, a.c3 = k, a.c4 = l
#define SET_CONTAINER_5(a, i, j, k, l, m)	a.c1 = i, a.c2 = j, a.c3 = k, a.c4 = l, a.c5 = m
#define SET_CONTAINER_6(a, i, j, k, l, m, n)	a.c1 = i, a.c2 = j, a.c3 = k, a.c4 = l, a.c5 = m, a.c6 = n

#define CONTAINER_AT_0(a)			(a).c1
#define CONTAINER_AT_1(a)			(a).c2
#define CONTAINER_AT_2(a)			(a).c3
#define CONTAINER_AT_3(a)			(a).c4

#define YEN_SIGN_TEXT		"(\0xa1\0xef)"
#define DOLLAR_SIGN_TEXT	"$"
#define WON_SIGN_TEXT		"\\"

void yyerror_explicit(int line, int column);
void yyerror(const char* s);

FUNCTION_MAP* keyword_offset(const char* name);
PT_NODE* keyword_func(const char* name, PT_NODE* args);

static PT_NODE* parser_make_expression(PT_OP_TYPE OP, PT_NODE* arg1, PT_NODE* arg2, PT_NODE* arg3);
static PT_NODE* parser_make_link(PT_NODE* list, PT_NODE* node);
static PT_NODE* parser_make_link_or(PT_NODE* list, PT_NODE* node);



static void parser_save_and_set_cannot_cache(bool value);
static void parser_restore_cannot_cache(void);

static void parser_save_and_set_si_datetime(int value);
static void parser_restore_si_datetime(void);

static void parser_save_and_set_si_tran_id(int value);
static void parser_restore_si_tran_id(void);

static void parser_save_and_set_cannot_prepare(bool value);
static void parser_restore_cannot_prepare(void);

static void parser_save_and_set_wjc(int value);
static void parser_restore_wjc(void);

static void parser_save_and_set_ic(int value);
static void parser_restore_ic(void);

static void parser_save_and_set_gc(int value);
static void parser_restore_gc(void);

static void parser_save_and_set_oc(int value);
static void parser_restore_oc(void);

static void parser_save_found_Oracle_outer(void);
static void parser_restore_found_Oracle_outer(void);

static void parser_save_alter_node(PT_NODE* node);
static PT_NODE* parser_get_alter_node(void);

static void parser_save_attr_def_one(PT_NODE* node);
static PT_NODE* parser_get_attr_def_one(void);

static void parser_push_orderby_node(PT_NODE* node);
static PT_NODE* parser_top_orderby_node(void);
static PT_NODE* parser_pop_orderby_node(void);

static void parser_push_select_stmt_node(PT_NODE* node);
static PT_NODE* parser_top_select_stmt_node(void);
static PT_NODE* parser_pop_select_stmt_node(void);


static void parser_push_hint_node(PT_NODE* node);
static PT_NODE* parser_top_hint_node(void);
static PT_NODE* parser_pop_hint_node(void);

static void parser_push_join_type(int v);
static int parser_top_join_type(void);
static int parser_pop_join_type(void);

static void parser_save_is_reverse(int v);
static int parser_get_is_reverse(void);

static void parser_stackpointer_init(void);
static PT_NODE* parser_make_date_lang(int arg_cnt, PT_NODE* arg3);
static void parser_remove_dummy_select(PT_NODE** node);
static int parser_count_list(PT_NODE* list);

static PT_MISC_TYPE parser_attr_type;

int parse_one_statement (int state);


int g_msg[1024];
int msg_ptr;


#define push_msg(a) _push_msg(a, __LINE__)

void set_msg(int code);
void _push_msg(int code, int line);
void pop_msg(void);



#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
typedef union YYSTYPE 
#line 295 "../../src/parser/csql_grammar.y"
{
	int number;
	PT_NODE* node;
	char* cptr;
	container_2 c2;
	container_3 c3;
	container_4 c4;
}
/* Line 2616 of glr.c.  */
#line 742 "../../src/parser/csql_grammar.h"
	YYSTYPE;
# define YYSTYPE_IS_DECLARED 1
# define YYSTYPE_IS_TRIVIAL 1
#endif

#if ! defined YYLTYPE && ! defined YYLTYPE_IS_DECLARED
typedef struct YYLTYPE
{

  int first_line;
  int first_column;
  int last_line;
  int last_column;

} YYLTYPE;
# define YYLTYPE_IS_DECLARED 1
# define YYLTYPE_IS_TRIVIAL 1
#endif


extern YYSTYPE yylval;

extern YYLTYPE yylloc;


