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
     CONNECT_BY_ISCYCLE = 309,
     CONNECT_BY_ISLEAF = 310,
     CONNECT_BY_ROOT = 311,
     CONNECTION = 312,
     CONSTRAINT = 313,
     CONSTRAINTS = 314,
     CONTINUE = 315,
     CONVERT = 316,
     CORRESPONDING = 317,
     COUNT = 318,
     CREATE = 319,
     CROSS = 320,
     CURRENT = 321,
     CURRENT_DATE = 322,
     CURRENT_DATETIME = 323,
     CURRENT_TIME = 324,
     CURRENT_TIMESTAMP = 325,
     CURRENT_USER = 326,
     CURSOR = 327,
     CYCLE = 328,
     DATA = 329,
     DATA_TYPE = 330,
     Date = 331,
     DATETIME = 332,
     DAY_ = 333,
     DEALLOCATE = 334,
     DECLARE = 335,
     DEFAULT = 336,
     DEFERRABLE = 337,
     DEFERRED = 338,
     DELETE_ = 339,
     DEPTH = 340,
     DESC = 341,
     DESCRIBE = 342,
     DESCRIPTOR = 343,
     DIAGNOSTICS = 344,
     DICTIONARY = 345,
     DIFFERENCE_ = 346,
     DISCONNECT = 347,
     DISTINCT = 348,
     Domain = 349,
     Double = 350,
     DROP = 351,
     EACH = 352,
     ELSE = 353,
     ELSEIF = 354,
     END = 355,
     EQUALS = 356,
     ESCAPE = 357,
     EVALUATE = 358,
     EXCEPT = 359,
     EXCEPTION = 360,
     EXCLUDE = 361,
     EXEC = 362,
     EXECUTE = 363,
     EXISTS = 364,
     EXTERNAL = 365,
     EXTRACT = 366,
     False = 367,
     FETCH = 368,
     File = 369,
     FIRST = 370,
     FLOAT_ = 371,
     For = 372,
     FOREIGN = 373,
     FOUND = 374,
     FROM = 375,
     FULL = 376,
     FUNCTION = 377,
     GENERAL = 378,
     GET = 379,
     GLOBAL = 380,
     GO = 381,
     GOTO = 382,
     GRANT = 383,
     GROUP_ = 384,
     HAVING = 385,
     HOUR_ = 386,
     IDENTITY = 387,
     IF = 388,
     IGNORE_ = 389,
     IMMEDIATE = 390,
     IN_ = 391,
     INDEX = 392,
     INDICATOR = 393,
     INHERIT = 394,
     INITIALLY = 395,
     INNER = 396,
     INOUT = 397,
     INPUT_ = 398,
     INSERT = 399,
     INTEGER = 400,
     INTERSECT = 401,
     INTERSECTION = 402,
     INTERVAL = 403,
     INTO = 404,
     IS = 405,
     ISOLATION = 406,
     JOIN = 407,
     KEY = 408,
     LANGUAGE = 409,
     LAST = 410,
     LDB = 411,
     LEADING_ = 412,
     LEAVE = 413,
     LEFT = 414,
     LESS = 415,
     LEVEL = 416,
     LIKE = 417,
     LIMIT = 418,
     LIST = 419,
     LOCAL = 420,
     LOCAL_TRANSACTION_ID = 421,
     LOOP = 422,
     LOWER = 423,
     MATCH = 424,
     Max = 425,
     METHOD = 426,
     MILLISECOND_ = 427,
     Min = 428,
     MINUTE_ = 429,
     MODIFY = 430,
     MODULE = 431,
     Monetary = 432,
     MONTH_ = 433,
     MULTISET = 434,
     MULTISET_OF = 435,
     NA = 436,
     NAMES = 437,
     NATIONAL = 438,
     NATURAL = 439,
     NCHAR = 440,
     NEXT = 441,
     NO = 442,
     NONE = 443,
     NOT = 444,
     Null = 445,
     NULLIF = 446,
     NUMERIC = 447,
     OBJECT = 448,
     OCTET_LENGTH = 449,
     OF = 450,
     OFF_ = 451,
     OID_ = 452,
     ON_ = 453,
     ONLY = 454,
     OPEN = 455,
     OPERATION = 456,
     OPERATORS = 457,
     OPTIMIZATION = 458,
     OPTION = 459,
     OR = 460,
     ORDER = 461,
     OTHERS = 462,
     OUT_ = 463,
     OUTER = 464,
     OUTPUT = 465,
     OVERLAPS = 466,
     PARAMETERS = 467,
     PARTIAL = 468,
     PENDANT = 469,
     POSITION = 470,
     PRECISION = 471,
     PREORDER = 472,
     PREPARE = 473,
     PRESERVE = 474,
     PRIMARY = 475,
     PRIOR = 476,
     Private = 477,
     PRIVILEGES = 478,
     PROCEDURE = 479,
     PROTECTED = 480,
     PROXY = 481,
     QUERY = 482,
     READ = 483,
     REBUILD = 484,
     RECURSIVE = 485,
     REF = 486,
     REFERENCES = 487,
     REFERENCING = 488,
     REGISTER = 489,
     RELATIVE_ = 490,
     RENAME = 491,
     REPLACE = 492,
     RESIGNAL = 493,
     RESTRICT = 494,
     RETURN = 495,
     RETURNS = 496,
     REVOKE = 497,
     RIGHT = 498,
     ROLE = 499,
     ROLLBACK = 500,
     ROUTINE = 501,
     ROW = 502,
     ROWNUM = 503,
     ROWS = 504,
     SAVEPOINT = 505,
     SCHEMA = 506,
     SCOPE = 507,
     SCROLL = 508,
     SEARCH = 509,
     SECOND_ = 510,
     SECTION = 511,
     SELECT = 512,
     SENSITIVE = 513,
     SEQUENCE = 514,
     SEQUENCE_OF = 515,
     SERIALIZABLE = 516,
     SESSION = 517,
     SESSION_USER = 518,
     SET = 519,
     SET_OF = 520,
     SETEQ = 521,
     SETNEQ = 522,
     SHARED = 523,
     SIBLINGS = 524,
     SIGNAL = 525,
     SIMILAR = 526,
     SIZE_ = 527,
     SmallInt = 528,
     SOME = 529,
     SQL = 530,
     SQLCODE = 531,
     SQLERROR = 532,
     SQLEXCEPTION = 533,
     SQLSTATE = 534,
     SQLWARNING = 535,
     STATISTICS = 536,
     String = 537,
     STRUCTURE = 538,
     SUBCLASS = 539,
     SUBSET = 540,
     SUBSETEQ = 541,
     SUBSTRING_ = 542,
     SUM = 543,
     SUPERCLASS = 544,
     SUPERSET = 545,
     SUPERSETEQ = 546,
     SYS_CONNECT_BY_PATH = 547,
     SYS_DATE = 548,
     SYS_DATETIME = 549,
     SYS_TIME_ = 550,
     SYS_TIMESTAMP = 551,
     SYS_USER = 552,
     SYSTEM_USER = 553,
     TABLE = 554,
     TEMPORARY = 555,
     TEST = 556,
     THEN = 557,
     THERE = 558,
     Time = 559,
     TIMESTAMP = 560,
     TIMEZONE_HOUR = 561,
     TIMEZONE_MINUTE = 562,
     TO = 563,
     TRAILING_ = 564,
     TRANSACTION = 565,
     TRANSLATE = 566,
     TRANSLATION = 567,
     TRIGGER = 568,
     TRIM = 569,
     True = 570,
     TYPE = 571,
     UNDER = 572,
     Union = 573,
     UNIQUE = 574,
     UNKNOWN = 575,
     UPDATE = 576,
     UPPER = 577,
     USAGE = 578,
     USE = 579,
     USER = 580,
     USING = 581,
     Utime = 582,
     VALUE = 583,
     VALUES = 584,
     VARCHAR = 585,
     VARIABLE_ = 586,
     VARYING = 587,
     VCLASS = 588,
     VIEW = 589,
     VIRTUAL = 590,
     VISIBLE = 591,
     WAIT = 592,
     WHEN = 593,
     WHENEVER = 594,
     WHERE = 595,
     WHILE = 596,
     WITH = 597,
     WITHOUT = 598,
     WORK = 599,
     WRITE = 600,
     YEAR_ = 601,
     ZONE = 602,
     YEN_SIGN = 603,
     DOLLAR_SIGN = 604,
     WON_SIGN = 605,
     RIGHT_ARROW = 606,
     STRCAT = 607,
     COMP_NOT_EQ = 608,
     COMP_GE = 609,
     COMP_LE = 610,
     PARAM_HEADER = 611,
     ACTIVE = 612,
     ANALYZE = 613,
     AUTO_INCREMENT = 614,
     CACHE = 615,
     COMMITTED = 616,
     COST = 617,
     DECREMENT = 618,
     GE_INF_ = 619,
     GE_LE_ = 620,
     GE_LT_ = 621,
     GROUPS = 622,
     GT_INF_ = 623,
     GT_LE_ = 624,
     GT_LT_ = 625,
     HASH = 626,
     INACTIVE = 627,
     INCREMENT = 628,
     INF_LE_ = 629,
     INF_LT_ = 630,
     INFINITE_ = 631,
     INSTANCES = 632,
     INVALIDATE = 633,
     JAVA = 634,
     LOCK_ = 635,
     MAXIMUM = 636,
     MAXVALUE = 637,
     MEMBERS = 638,
     MINVALUE = 639,
     NAME = 640,
     NOCYCLE = 641,
     NOCACHE = 642,
     NOMAXVALUE = 643,
     NOMINVALUE = 644,
     PARTITION = 645,
     PARTITIONING = 646,
     PARTITIONS = 647,
     PASSWORD = 648,
     PRINT = 649,
     PRIORITY = 650,
     RANGE_ = 651,
     REJECT_ = 652,
     REMOVE = 653,
     REORGANIZE = 654,
     REPEATABLE = 655,
     RETAIN = 656,
     REUSE_OID = 657,
     REVERSE = 658,
     SERIAL = 659,
     STABILITY = 660,
     START_ = 661,
     STATEMENT = 662,
     STATUS = 663,
     STDDEV = 664,
     SYSTEM = 665,
     THAN = 666,
     TIMEOUT = 667,
     TRACE = 668,
     TRIGGERS = 669,
     UNCOMMITTED = 670,
     VARIANCE = 671,
     WORKSPACE = 672,
     IdName = 673,
     BracketDelimitedIdName = 674,
     DelimitedIdName = 675,
     UNSIGNED_INTEGER = 676,
     UNSIGNED_REAL = 677,
     CHAR_STRING = 678,
     NCHAR_STRING = 679,
     BIT_STRING = 680,
     HEX_STRING = 681,
     CPP_STYLE_HINT = 682,
     C_STYLE_HINT = 683,
     SQL_STYLE_HINT = 684
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
#include <errno.h>

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
		 2 allowed, no compatibility check (select_list) */
static int parser_instnum_check = 0;
static int parser_groupbynum_check = 0;
static int parser_orderbynum_check = 0;
static int parser_within_join_condition = 0;

/* xxx_check: 0 not allowed
              1 allowed */
static int parser_sysconnectbypath_check = 0;
static int parser_prior_check = 0;
static int parser_connectbyroot_check = 0;
static int parser_serial_check = 1;
static int parser_pseudocolumn_check = 1;
static int parser_subquery_check = 1;

/* check Oracle style outer-join operator: '(+)' */
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

static void parser_save_and_set_sysc(int value);
static void parser_restore_sysc(void);

static void parser_save_and_set_prc(int value);
static void parser_restore_prc(void);

static void parser_save_and_set_cbrc(int value);
static void parser_restore_cbrc(void);

static void parser_save_and_set_serc(int value);
static void parser_restore_serc(void);

static void parser_save_and_set_pseudoc(int value);
static void parser_restore_pseudoc(void);

static void parser_save_and_set_sqc(int value);
static void parser_restore_sqc(void);

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

void _push_msg(int code, int line);
void pop_msg(void);



#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
typedef union YYSTYPE 
#line 322 "../../src/parser/csql_grammar.y"
{
	int number;
	PT_NODE* node;
	char* cptr;
	container_2 c2;
	container_3 c3;
	container_4 c4;
}
/* Line 2616 of glr.c.  */
#line 776 "../../src/parser/csql_grammar.h"
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


