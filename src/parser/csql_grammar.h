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
     BITSHIFT_LEFT = 284,
     BITSHIFT_RIGHT = 285,
     BLOB_ = 286,
     BOOLEAN_ = 287,
     BOTH_ = 288,
     BREADTH = 289,
     BY = 290,
     CALL = 291,
     CASCADE = 292,
     CASCADED = 293,
     CASE = 294,
     CAST = 295,
     CATALOG = 296,
     CHANGE = 297,
     CHAR_ = 298,
     CHECK = 299,
     CLASS = 300,
     CLASSES = 301,
     CLOB_ = 302,
     CLOSE = 303,
     CLUSTER = 304,
     COALESCE = 305,
     COLLATE = 306,
     COLLATION = 307,
     COLUMN = 308,
     COMMIT = 309,
     COMP_NULLSAFE_EQ = 310,
     COMPLETION = 311,
     CONNECT = 312,
     CONNECT_BY_ISCYCLE = 313,
     CONNECT_BY_ISLEAF = 314,
     CONNECT_BY_ROOT = 315,
     CONNECTION = 316,
     CONSTRAINT = 317,
     CONSTRAINTS = 318,
     CONTINUE = 319,
     CONVERT = 320,
     CORRESPONDING = 321,
     COUNT = 322,
     CREATE = 323,
     CROSS = 324,
     CURRENT = 325,
     CURRENT_DATE = 326,
     CURRENT_DATETIME = 327,
     CURRENT_TIME = 328,
     CURRENT_TIMESTAMP = 329,
     CURRENT_USER = 330,
     CURSOR = 331,
     CYCLE = 332,
     DATA = 333,
     DATABASE = 334,
     DATA_TYPE = 335,
     Date = 336,
     DATETIME = 337,
     DAY_ = 338,
     DAY_MILLISECOND = 339,
     DAY_SECOND = 340,
     DAY_MINUTE = 341,
     DAY_HOUR = 342,
     DEALLOCATE = 343,
     DECLARE = 344,
     DEFAULT = 345,
     DEFERRABLE = 346,
     DEFERRED = 347,
     DELETE_ = 348,
     DEPTH = 349,
     DESC = 350,
     DESCRIBE = 351,
     DESCRIPTOR = 352,
     DIAGNOSTICS = 353,
     DICTIONARY = 354,
     DIFFERENCE_ = 355,
     DISCONNECT = 356,
     DISTINCT = 357,
     DISTINCTROW = 358,
     DIV = 359,
     DO = 360,
     Domain = 361,
     Double = 362,
     DROP = 363,
     DUPLICATE_ = 364,
     EACH = 365,
     ELSE = 366,
     ELSEIF = 367,
     END = 368,
     EQUALS = 369,
     ESCAPE = 370,
     EVALUATE = 371,
     EXCEPT = 372,
     EXCEPTION = 373,
     EXCLUDE = 374,
     EXEC = 375,
     EXECUTE = 376,
     EXISTS = 377,
     EXTERNAL = 378,
     EXTRACT = 379,
     False = 380,
     FETCH = 381,
     File = 382,
     FIRST = 383,
     FLOAT_ = 384,
     For = 385,
     FOREIGN = 386,
     FOUND = 387,
     FROM = 388,
     FULL = 389,
     FUNCTION = 390,
     GENERAL = 391,
     GET = 392,
     GLOBAL = 393,
     GO = 394,
     GOTO = 395,
     GRANT = 396,
     GROUP_ = 397,
     HAVING = 398,
     HOUR_ = 399,
     HOUR_MILLISECOND = 400,
     HOUR_SECOND = 401,
     HOUR_MINUTE = 402,
     IDENTITY = 403,
     IF = 404,
     IGNORE_ = 405,
     IMMEDIATE = 406,
     IN_ = 407,
     INDEX = 408,
     INDICATOR = 409,
     INHERIT = 410,
     INITIALLY = 411,
     INNER = 412,
     INOUT = 413,
     INPUT_ = 414,
     INSERT = 415,
     INTEGER = 416,
     INTERNAL = 417,
     INTERSECT = 418,
     INTERSECTION = 419,
     INTERVAL = 420,
     INTO = 421,
     IS = 422,
     ISOLATION = 423,
     JOIN = 424,
     KEY = 425,
     KEYLIMIT = 426,
     LANGUAGE = 427,
     LAST = 428,
     LDB = 429,
     LEADING_ = 430,
     LEAVE = 431,
     LEFT = 432,
     LESS = 433,
     LEVEL = 434,
     LIKE = 435,
     LIMIT = 436,
     LIST = 437,
     LOCAL = 438,
     LOCAL_TRANSACTION_ID = 439,
     LOCALTIME = 440,
     LOCALTIMESTAMP = 441,
     LOOP = 442,
     LOWER = 443,
     MATCH = 444,
     Max = 445,
     METHOD = 446,
     MILLISECOND_ = 447,
     Min = 448,
     MINUTE_ = 449,
     MINUTE_MILLISECOND = 450,
     MINUTE_SECOND = 451,
     MOD = 452,
     MODIFY = 453,
     MODULE = 454,
     Monetary = 455,
     MONTH_ = 456,
     MULTISET = 457,
     MULTISET_OF = 458,
     NA = 459,
     NAMES = 460,
     NATIONAL = 461,
     NATURAL = 462,
     NCHAR = 463,
     NEXT = 464,
     NO = 465,
     NONE = 466,
     NOT = 467,
     Null = 468,
     NULLIF = 469,
     NUMERIC = 470,
     OBJECT = 471,
     OCTET_LENGTH = 472,
     OF = 473,
     OFF_ = 474,
     OID_ = 475,
     ON_ = 476,
     ONLY = 477,
     OPEN = 478,
     OPERATION = 479,
     OPERATORS = 480,
     OPTIMIZATION = 481,
     OPTION = 482,
     OR = 483,
     ORDER = 484,
     OTHERS = 485,
     OUT_ = 486,
     OUTER = 487,
     OUTPUT = 488,
     OVER = 489,
     OVERLAPS = 490,
     PARAMETERS = 491,
     PARTIAL = 492,
     PENDANT = 493,
     POSITION = 494,
     PRECISION = 495,
     PREORDER = 496,
     PREPARE = 497,
     PRESERVE = 498,
     PRIMARY = 499,
     PRIOR = 500,
     Private = 501,
     PRIVILEGES = 502,
     PROCEDURE = 503,
     PROTECTED = 504,
     PROXY = 505,
     QUERY = 506,
     READ = 507,
     REBUILD = 508,
     RECURSIVE = 509,
     REF = 510,
     REFERENCES = 511,
     REFERENCING = 512,
     REGISTER = 513,
     RELATIVE_ = 514,
     RENAME = 515,
     REPLACE = 516,
     RESIGNAL = 517,
     RESTRICT = 518,
     RETURN = 519,
     RETURNS = 520,
     REVOKE = 521,
     RIGHT = 522,
     ROLE = 523,
     ROLLBACK = 524,
     ROLLUP = 525,
     ROUTINE = 526,
     ROW = 527,
     ROWNUM = 528,
     ROWS = 529,
     SAVEPOINT = 530,
     SCHEMA = 531,
     SCOPE = 532,
     SCROLL = 533,
     SEARCH = 534,
     SECOND_ = 535,
     SECOND_MILLISECOND = 536,
     SECTION = 537,
     SELECT = 538,
     SENSITIVE = 539,
     SEQUENCE = 540,
     SEQUENCE_OF = 541,
     SERIALIZABLE = 542,
     SESSION = 543,
     SESSION_USER = 544,
     SET = 545,
     SET_OF = 546,
     SETEQ = 547,
     SETNEQ = 548,
     SHARED = 549,
     SIBLINGS = 550,
     SIGNAL = 551,
     SIMILAR = 552,
     SIZE_ = 553,
     SmallInt = 554,
     SOME = 555,
     SQL = 556,
     SQLCODE = 557,
     SQLERROR = 558,
     SQLEXCEPTION = 559,
     SQLSTATE = 560,
     SQLWARNING = 561,
     STATISTICS = 562,
     String = 563,
     STRUCTURE = 564,
     SUBCLASS = 565,
     SUBSET = 566,
     SUBSETEQ = 567,
     SUBSTRING_ = 568,
     SUM = 569,
     SUPERCLASS = 570,
     SUPERSET = 571,
     SUPERSETEQ = 572,
     SYS_CONNECT_BY_PATH = 573,
     SYS_DATE = 574,
     SYS_DATETIME = 575,
     SYS_TIME_ = 576,
     SYS_TIMESTAMP = 577,
     SYS_USER = 578,
     SYSTEM_USER = 579,
     TABLE = 580,
     TEMPORARY = 581,
     TEST = 582,
     THEN = 583,
     THERE = 584,
     Time = 585,
     TIMESTAMP = 586,
     TIMEZONE_HOUR = 587,
     TIMEZONE_MINUTE = 588,
     TO = 589,
     TRAILING_ = 590,
     TRANSACTION = 591,
     TRANSLATE = 592,
     TRANSLATION = 593,
     TRIGGER = 594,
     TRIM = 595,
     True = 596,
     TRUNCATE = 597,
     TYPE = 598,
     UNDER = 599,
     Union = 600,
     UNIQUE = 601,
     UNKNOWN = 602,
     UNTERMINATED_STRING = 603,
     UNTERMINATED_IDENTIFIER = 604,
     UPDATE = 605,
     UPPER = 606,
     USAGE = 607,
     USE = 608,
     USER = 609,
     USING = 610,
     Utime = 611,
     VALUE = 612,
     VALUES = 613,
     VAR_ASSIGN = 614,
     VARCHAR = 615,
     VARIABLE_ = 616,
     VARYING = 617,
     VCLASS = 618,
     VIEW = 619,
     VIRTUAL = 620,
     VISIBLE = 621,
     WAIT = 622,
     WHEN = 623,
     WHENEVER = 624,
     WHERE = 625,
     WHILE = 626,
     WITH = 627,
     WITHOUT = 628,
     WORK = 629,
     WRITE = 630,
     XOR = 631,
     YEAR_ = 632,
     YEAR_MONTH = 633,
     ZONE = 634,
     YEN_SIGN = 635,
     DOLLAR_SIGN = 636,
     WON_SIGN = 637,
     TURKISH_LIRA_SIGN = 638,
     RIGHT_ARROW = 639,
     STRCAT = 640,
     COMP_NOT_EQ = 641,
     COMP_GE = 642,
     COMP_LE = 643,
     PARAM_HEADER = 644,
     ACTIVE = 645,
     ADDDATE = 646,
     ANALYZE = 647,
     AUTO_INCREMENT = 648,
     BIT_AND = 649,
     BIT_OR = 650,
     BIT_XOR = 651,
     CACHE = 652,
     COLUMNS = 653,
     COMMITTED = 654,
     COST = 655,
     DATE_ADD = 656,
     DATE_SUB = 657,
     DECREMENT = 658,
     DENSE_RANK = 659,
     ELT = 660,
     EXPLAIN = 661,
     GE_INF_ = 662,
     GE_LE_ = 663,
     GE_LT_ = 664,
     GRANTS = 665,
     GROUP_CONCAT = 666,
     GROUPS = 667,
     GT_INF_ = 668,
     GT_LE_ = 669,
     GT_LT_ = 670,
     HASH = 671,
     IFNULL = 672,
     INACTIVE = 673,
     INCREMENT = 674,
     INDEXES = 675,
     INF_LE_ = 676,
     INF_LT_ = 677,
     INFINITE_ = 678,
     INSTANCES = 679,
     INVALIDATE = 680,
     ISNULL = 681,
     KEYS = 682,
     JAVA = 683,
     LCASE = 684,
     LOCK_ = 685,
     MAXIMUM = 686,
     MAXVALUE = 687,
     MEMBERS = 688,
     MINVALUE = 689,
     NAME = 690,
     NOCYCLE = 691,
     NOCACHE = 692,
     NOMAXVALUE = 693,
     NOMINVALUE = 694,
     PARTITION = 695,
     PARTITIONING = 696,
     PARTITIONS = 697,
     PASSWORD = 698,
     PRINT = 699,
     PRIORITY = 700,
     QUARTER = 701,
     RANGE_ = 702,
     RANK = 703,
     REJECT_ = 704,
     REMOVE = 705,
     REORGANIZE = 706,
     REPEATABLE = 707,
     RETAIN = 708,
     REUSE_OID = 709,
     REVERSE = 710,
     ROW_NUMBER = 711,
     SEPARATOR = 712,
     SERIAL = 713,
     SHOW = 714,
     STABILITY = 715,
     START_ = 716,
     STATEMENT = 717,
     STATUS = 718,
     STDDEV = 719,
     STDDEV_POP = 720,
     STDDEV_SAMP = 721,
     STR_TO_DATE = 722,
     SUBDATE = 723,
     SYSTEM = 724,
     TABLES = 725,
     THAN = 726,
     TIMEOUT = 727,
     TRACE = 728,
     TRIGGERS = 729,
     UCASE = 730,
     UNCOMMITTED = 731,
     VAR_POP = 732,
     VAR_SAMP = 733,
     VARIANCE = 734,
     WEEK = 735,
     WORKSPACE = 736,
     IdName = 737,
     BracketDelimitedIdName = 738,
     BacktickDelimitedIdName = 739,
     DelimitedIdName = 740,
     UNSIGNED_INTEGER = 741,
     UNSIGNED_REAL = 742,
     CHAR_STRING = 743,
     NCHAR_STRING = 744,
     BIT_STRING = 745,
     HEX_STRING = 746,
     CPP_STYLE_HINT = 747,
     C_STYLE_HINT = 748,
     SQL_STYLE_HINT = 749
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
#include "system_parameter.h"
#define JP_MAXNAME 256
#if defined(WINDOWS)
#define snprintf _snprintf
#endif /* WINDOWS */
#include "memory_alloc.h"
#include "db_elo.h"

/* Bit mask to be used to check constraints of a column.
 * COLUMN_CONSTRAINT_SHARED_DEFAULT_AI is special-purpose mask
 * to identify duplication of SHARED, DEFAULT and AUTO_INCREMENT.
 */
#define COLUMN_CONSTRAINT_UNIQUE		(0x01)
#define COLUMN_CONSTRAINT_PRIMARY_KEY		(0x02)
#define COLUMN_CONSTRAINT_NULL			(0x04)
#define COLUMN_CONSTRAINT_OTHERS		(0x08)
#define COLUMN_CONSTRAINT_SHARED		(0x10)
#define COLUMN_CONSTRAINT_DEFAULT		(0x20)
#define COLUMN_CONSTRAINT_AUTO_INCREMENT	(0x40)

#define COLUMN_CONSTRAINT_SHARED_DEFAULT_AI	(0x70)


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
  const char *keyword;
  PT_OP_TYPE op;
};


static FUNCTION_MAP functions[] = {
  {"abs", PT_ABS},
  {"acos", PT_ACOS},
  {"addtime", PT_ADDTIME}, 
  {"asin", PT_ASIN},
  {"atan", PT_ATAN},
  {"atan2", PT_ATAN2},
  {"bin", PT_BIN},
  {"bit_count", PT_BIT_COUNT},
  {"bit_to_blob", PT_BIT_TO_BLOB},
  {"blob_from_file", PT_BLOB_FROM_FILE},
  {"blob_length", PT_BLOB_LENGTH},
  {"blob_to_bit", PT_BLOB_TO_BIT},
  {"ceil", PT_CEIL},
  {"ceiling", PT_CEIL},
  {"char_length", PT_CHAR_LENGTH},
  {"char_to_blob", PT_CHAR_TO_BLOB},
  {"char_to_clob", PT_CHAR_TO_CLOB},
  {"character_length", PT_CHAR_LENGTH},
  {"chr", PT_CHR},
  {"clob_from_file", PT_CLOB_FROM_FILE},
  {"clob_length", PT_CLOB_LENGTH},
  {"clob_to_char", PT_CLOB_TO_CHAR},
  {"concat", PT_CONCAT},
  {"concat_ws", PT_CONCAT_WS},
  {"cos", PT_COS},
  {"cot", PT_COT},
  {"curtime", PT_SYS_TIME},
  {"curdate", PT_SYS_DATE},
  {"utc_time", PT_UTC_TIME},
  {"utc_date", PT_UTC_DATE},
  {"datediff", PT_DATEDIFF},
  {"timediff",PT_TIMEDIFF},
  {"date_format", PT_DATE_FORMAT},
  {"dayofmonth", PT_DAYOFMONTH},
  {"dayofyear", PT_DAYOFYEAR},
  {"decode", PT_DECODE},
  {"decr", PT_DECR},
  {"degrees", PT_DEGREES},
  {"drand", PT_DRAND},
  {"drandom", PT_DRANDOM},
  {"exec_stats", PT_EXEC_STATS},
  {"exp", PT_EXP},
  {"field", PT_FIELD},
  {"floor", PT_FLOOR},
  {"from_days", PT_FROMDAYS},
  {"greatest", PT_GREATEST},
  {"groupby_num", PT_GROUPBY_NUM},
  {"incr", PT_INCR},
  {"index_cardinality", PT_INDEX_CARDINALITY},
  {"inst_num", PT_INST_NUM},
  {"instr", PT_INSTR},
  {"instrb", PT_INSTR},
  {"last_day", PT_LAST_DAY},
  {"length", PT_CHAR_LENGTH},
  {"lengthb", PT_CHAR_LENGTH},
  {"least", PT_LEAST},
  {"like_match_lower_bound", PT_LIKE_LOWER_BOUND},
  {"like_match_upper_bound", PT_LIKE_UPPER_BOUND},
  {"list_dbs", PT_LIST_DBS},
  {"locate", PT_LOCATE},
  {"ln", PT_LN},
  {"log2", PT_LOG2},
  {"log10", PT_LOG10},
  {"log", PT_LOG},
  {"lpad", PT_LPAD},
  {"ltrim", PT_LTRIM},
  {"makedate", PT_MAKEDATE},
  {"maketime", PT_MAKETIME},
  {"mid", PT_MID},
  {"months_between", PT_MONTHS_BETWEEN},
  {"format", PT_FORMAT},
  {"now", PT_SYS_DATETIME},
  {"nvl", PT_NVL},
  {"nvl2", PT_NVL2},
  {"orderby_num", PT_ORDERBY_NUM},
  {"power", PT_POWER},
  {"pow", PT_POWER},
  {"pi", PT_PI},
  {"radians", PT_RADIANS},
  {"rand", PT_RAND},
  {"random", PT_RANDOM},
  {"repeat", PT_REPEAT},
  {"space", PT_SPACE},
  {"reverse", PT_REVERSE},
  {"round", PT_ROUND},
  {"row_count", PT_ROW_COUNT},
  {"last_insert_id", PT_LAST_INSERT_ID},
  {"rpad", PT_RPAD},
  {"rtrim", PT_RTRIM},
  {"sec_to_time", PT_SECTOTIME},
  {"sign", PT_SIGN},
  {"sin", PT_SIN},
  {"sqrt", PT_SQRT},
  {"strcmp", PT_STRCMP},
  {"substr", PT_SUBSTRING},
  {"substring_index", PT_SUBSTRING_INDEX},
  {"find_in_set", PT_FINDINSET},
  {"md5", PT_MD5},
  {"substrb", PT_SUBSTRING},
  {"tan", PT_TAN},
  {"time_format", PT_TIME_FORMAT},
  {"to_char", PT_TO_CHAR},
  {"to_date", PT_TO_DATE},
  {"to_datetime", PT_TO_DATETIME},
  {"to_days", PT_TODAYS},
  {"time_to_sec", PT_TIMETOSEC},
  {"to_number", PT_TO_NUMBER},
  {"to_time", PT_TO_TIME},
  {"to_timestamp", PT_TO_TIMESTAMP},
  {"trunc", PT_TRUNC},
  {"unix_timestamp", PT_UNIX_TIMESTAMP},
  {"typeof", PT_TYPEOF},
  {"from_unixtime", PT_FROM_UNIXTIME},
  {"weekday", PT_WEEKDAY},
  {"dayofweek", PT_DAYOFWEEK},
  {"version", PT_VERSION},
  {"quarter", PT_QUARTERF},
  {"week", PT_WEEKF}
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
static int parser_hostvar_check = 1;

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

typedef struct
{
  PT_NODE *c1;
  PT_NODE *c2;
} container_2;

typedef struct
{
  PT_NODE *c1;
  PT_NODE *c2;
  PT_NODE *c3;
} container_3;

typedef struct
{
  PT_NODE *c1;
  PT_NODE *c2;
  PT_NODE *c3;
  PT_NODE *c4;
} container_4;

typedef struct
{
  PT_NODE *c1;
  PT_NODE *c2;
  PT_NODE *c3;
  PT_NODE *c4;
  PT_NODE *c5;
  PT_NODE *c6;
  PT_NODE *c7;
  PT_NODE *c8;
  PT_NODE *c9;
  PT_NODE *c10;
} container_10;

#define PT_EMPTY INT_MAX

#if defined(WINDOWS)
#define inline
#endif


#define TO_NUMBER(a)			((UINTPTR)(a))
#define FROM_NUMBER(a)			((PT_NODE*)(UINTPTR)(a))


#define SET_CONTAINER_2(a, i, j)		a.c1 = i, a.c2 = j
#define SET_CONTAINER_3(a, i, j, k)		a.c1 = i, a.c2 = j, a.c3 = k
#define SET_CONTAINER_4(a, i, j, k, l)		a.c1 = i, a.c2 = j, a.c3 = k, a.c4 = l

#define CONTAINER_AT_0(a)			(a).c1
#define CONTAINER_AT_1(a)			(a).c2
#define CONTAINER_AT_2(a)			(a).c3
#define CONTAINER_AT_3(a)			(a).c4
#define CONTAINER_AT_4(a)			(a).c5
#define CONTAINER_AT_5(a)			(a).c6
#define CONTAINER_AT_6(a)			(a).c7
#define CONTAINER_AT_7(a)			(a).c8
#define CONTAINER_AT_8(a)			(a).c9
#define CONTAINER_AT_9(a)			(a).c10

#define YEN_SIGN_TEXT		"(\0xa1\0xef)"
#define DOLLAR_SIGN_TEXT	"$"
#define WON_SIGN_TEXT		"\\"
#define TURKISH_LIRA_TEXT	"TL"

typedef enum
{
  SERIAL_START,
  SERIAL_INC,
  SERIAL_MAX,
  SERIAL_MIN,
  SERIAL_CYCLE,
  SERIAL_CACHE,
} SERIAL_DEFINE;

void csql_yyerror_explicit (int line, int column);
void csql_yyerror (const char *s);

FUNCTION_MAP *keyword_offset (const char *name);

static PT_NODE *parser_make_expr_with_func (PARSER_CONTEXT * parser,
					    FUNC_TYPE func_code,
					    PT_NODE * args_list);
static PT_NODE *parser_make_link (PT_NODE * list, PT_NODE * node);
static PT_NODE *parser_make_link_or (PT_NODE * list, PT_NODE * node);



static void parser_save_and_set_cannot_cache (bool value);
static void parser_restore_cannot_cache (void);

static void parser_save_and_set_si_datetime (int value);
static void parser_restore_si_datetime (void);

static void parser_save_and_set_si_tran_id (int value);
static void parser_restore_si_tran_id (void);

static void parser_save_and_set_cannot_prepare (bool value);
static void parser_restore_cannot_prepare (void);

static void parser_save_and_set_wjc (int value);
static void parser_restore_wjc (void);

static void parser_save_and_set_ic (int value);
static void parser_restore_ic (void);

static void parser_save_and_set_gc (int value);
static void parser_restore_gc (void);

static void parser_save_and_set_oc (int value);
static void parser_restore_oc (void);

static void parser_save_and_set_sysc (int value);
static void parser_restore_sysc (void);

static void parser_save_and_set_prc (int value);
static void parser_restore_prc (void);

static void parser_save_and_set_cbrc (int value);
static void parser_restore_cbrc (void);

static void parser_save_and_set_serc (int value);
static void parser_restore_serc (void);

static void parser_save_and_set_pseudoc (int value);
static void parser_restore_pseudoc (void);

static void parser_save_and_set_sqc (int value);
static void parser_restore_sqc (void);

static void parser_save_and_set_hvar (int value);
static void parser_restore_hvar (void);

static void parser_save_found_Oracle_outer (void);
static void parser_restore_found_Oracle_outer (void);

static void parser_save_alter_node (PT_NODE * node);
static PT_NODE *parser_get_alter_node (void);

static void parser_save_attr_def_one (PT_NODE * node);
static PT_NODE *parser_get_attr_def_one (void);

static void parser_push_orderby_node (PT_NODE * node);
static PT_NODE *parser_top_orderby_node (void);
static PT_NODE *parser_pop_orderby_node (void);

static void parser_push_select_stmt_node (PT_NODE * node);
static PT_NODE *parser_top_select_stmt_node (void);
static PT_NODE *parser_pop_select_stmt_node (void);


static void parser_push_hint_node (PT_NODE * node);
static PT_NODE *parser_top_hint_node (void);
static PT_NODE *parser_pop_hint_node (void);

static void parser_push_join_type (int v);
static int parser_top_join_type (void);
static int parser_pop_join_type (void);

static void parser_save_is_reverse (bool v);
static bool parser_get_is_reverse (void);

static void parser_initialize_parser_context (void);
static PT_NODE *parser_make_date_lang (int arg_cnt, PT_NODE * arg3);
static void parser_remove_dummy_select (PT_NODE ** node);
static int parser_count_list (PT_NODE * list);

static void resolve_alias_in_expr_node (PT_NODE * node, PT_NODE * list);
static void resolve_alias_in_name_node (PT_NODE ** node, PT_NODE * list);

static PT_MISC_TYPE parser_attr_type;

static bool allow_attribute_ordering;

int parse_one_statement (int state);


int g_msg[1024];
int msg_ptr;


#define push_msg(a) _push_msg(a, __LINE__)

void _push_msg (int code, int line);
void pop_msg (void);



#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
typedef union YYSTYPE 
#line 453 "../../src/parser/csql_grammar.y"
{
  int number;
  bool boolean;
  PT_NODE *node;
  char *cptr;
  container_2 c2;
  container_3 c3;
  container_4 c4;
  container_10 c10;
}
/* Line 2604 of glr.c.  */
#line 973 "../../src/parser/csql_grammar.h"
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


extern YYSTYPE csql_yylval;

extern YYLTYPE csql_yylloc;


