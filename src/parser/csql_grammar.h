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
  FORCE = 386,
  FOREIGN = 387,
  FOUND = 388,
  FROM = 389,
  FULL = 390,
  FUNCTION = 391,
  GENERAL = 392,
  GET = 393,
  GLOBAL = 394,
  GO = 395,
  GOTO = 396,
  GRANT = 397,
  GROUP_ = 398,
  HAVING = 399,
  HOUR_ = 400,
  HOUR_MILLISECOND = 401,
  HOUR_SECOND = 402,
  HOUR_MINUTE = 403,
  IDENTITY = 404,
  IF = 405,
  IGNORE_ = 406,
  IMMEDIATE = 407,
  IN_ = 408,
  INDEX = 409,
  INDICATOR = 410,
  INHERIT = 411,
  INITIALLY = 412,
  INNER = 413,
  INOUT = 414,
  INPUT_ = 415,
  INSERT = 416,
  INTEGER = 417,
  INTERNAL = 418,
  INTERSECT = 419,
  INTERSECTION = 420,
  INTERVAL = 421,
  INTO = 422,
  IS = 423,
  ISOLATION = 424,
  JOIN = 425,
  KEY = 426,
  KEYLIMIT = 427,
  LANGUAGE = 428,
  LAST = 429,
  LDB = 430,
  LEADING_ = 431,
  LEAVE = 432,
  LEFT = 433,
  LESS = 434,
  LEVEL = 435,
  LIKE = 436,
  LIMIT = 437,
  LIST = 438,
  LOCAL = 439,
  LOCAL_TRANSACTION_ID = 440,
  LOCALTIME = 441,
  LOCALTIMESTAMP = 442,
  LOOP = 443,
  LOWER = 444,
  MATCH = 445,
  Max = 446,
  METHOD = 447,
  MILLISECOND_ = 448,
  Min = 449,
  MINUTE_ = 450,
  MINUTE_MILLISECOND = 451,
  MINUTE_SECOND = 452,
  MOD = 453,
  MODIFY = 454,
  MODULE = 455,
  Monetary = 456,
  MONTH_ = 457,
  MULTISET = 458,
  MULTISET_OF = 459,
  NA = 460,
  NAMES = 461,
  NATIONAL = 462,
  NATURAL = 463,
  NCHAR = 464,
  NEXT = 465,
  NO = 466,
  NONE = 467,
  NOT = 468,
  Null = 469,
  NULLIF = 470,
  NUMERIC = 471,
  OBJECT = 472,
  OCTET_LENGTH = 473,
  OF = 474,
  OFF_ = 475,
  OID_ = 476,
  ON_ = 477,
  ONLY = 478,
  OPEN = 479,
  OPERATION = 480,
  OPERATORS = 481,
  OPTIMIZATION = 482,
  OPTION = 483,
  OR = 484,
  ORDER = 485,
  OTHERS = 486,
  OUT_ = 487,
  OUTER = 488,
  OUTPUT = 489,
  OVER = 490,
  OVERLAPS = 491,
  PARAMETERS = 492,
  PARTIAL = 493,
  PENDANT = 494,
  POSITION = 495,
  PRECISION = 496,
  PREORDER = 497,
  PREPARE = 498,
  PRESERVE = 499,
  PRIMARY = 500,
  PRIOR = 501,
  Private = 502,
  PRIVILEGES = 503,
  PROCEDURE = 504,
  PROTECTED = 505,
  PROXY = 506,
  QUERY = 507,
  READ = 508,
  REBUILD = 509,
  RECURSIVE = 510,
  REF = 511,
  REFERENCES = 512,
  REFERENCING = 513,
  REGISTER = 514,
  RELATIVE_ = 515,
  RENAME = 516,
  REPLACE = 517,
  RESIGNAL = 518,
  RESTRICT = 519,
  RETURN = 520,
  RETURNS = 521,
  REVOKE = 522,
  RIGHT = 523,
  ROLE = 524,
  ROLLBACK = 525,
  ROLLUP = 526,
  ROUTINE = 527,
  ROW = 528,
  ROWNUM = 529,
  ROWS = 530,
  SAVEPOINT = 531,
  SCHEMA = 532,
  SCOPE = 533,
  SCROLL = 534,
  SEARCH = 535,
  SECOND_ = 536,
  SECOND_MILLISECOND = 537,
  SECTION = 538,
  SELECT = 539,
  SENSITIVE = 540,
  SEQUENCE = 541,
  SEQUENCE_OF = 542,
  SERIALIZABLE = 543,
  SESSION = 544,
  SESSION_USER = 545,
  SET = 546,
  SET_OF = 547,
  SETEQ = 548,
  SETNEQ = 549,
  SHARED = 550,
  SIBLINGS = 551,
  SIGNAL = 552,
  SIMILAR = 553,
  SIZE_ = 554,
  SmallInt = 555,
  SOME = 556,
  SQL = 557,
  SQLCODE = 558,
  SQLERROR = 559,
  SQLEXCEPTION = 560,
  SQLSTATE = 561,
  SQLWARNING = 562,
  STATISTICS = 563,
  String = 564,
  STRUCTURE = 565,
  SUBCLASS = 566,
  SUBSET = 567,
  SUBSETEQ = 568,
  SUBSTRING_ = 569,
  SUM = 570,
  SUPERCLASS = 571,
  SUPERSET = 572,
  SUPERSETEQ = 573,
  SYS_CONNECT_BY_PATH = 574,
  SYS_DATE = 575,
  SYS_DATETIME = 576,
  SYS_TIME_ = 577,
  SYS_TIMESTAMP = 578,
  SYS_USER = 579,
  SYSTEM_USER = 580,
  TABLE = 581,
  TEMPORARY = 582,
  TEST = 583,
  THEN = 584,
  THERE = 585,
  Time = 586,
  TIMESTAMP = 587,
  TIMEZONE_HOUR = 588,
  TIMEZONE_MINUTE = 589,
  TO = 590,
  TRAILING_ = 591,
  TRANSACTION = 592,
  TRANSLATE = 593,
  TRANSLATION = 594,
  TRIGGER = 595,
  TRIM = 596,
  True = 597,
  TRUNCATE = 598,
  TYPE = 599,
  UNDER = 600,
  Union = 601,
  UNIQUE = 602,
  UNKNOWN = 603,
  UNTERMINATED_STRING = 604,
  UNTERMINATED_IDENTIFIER = 605,
  UPDATE = 606,
  UPPER = 607,
  USAGE = 608,
  USE = 609,
  USER = 610,
  USING = 611,
  Utime = 612,
  VALUE = 613,
  VALUES = 614,
  VAR_ASSIGN = 615,
  VARCHAR = 616,
  VARIABLE_ = 617,
  VARYING = 618,
  VCLASS = 619,
  VIEW = 620,
  VIRTUAL = 621,
  VISIBLE = 622,
  WAIT = 623,
  WHEN = 624,
  WHENEVER = 625,
  WHERE = 626,
  WHILE = 627,
  WITH = 628,
  WITHOUT = 629,
  WORK = 630,
  WRITE = 631,
  XOR = 632,
  YEAR_ = 633,
  YEAR_MONTH = 634,
  ZONE = 635,
  YEN_SIGN = 636,
  DOLLAR_SIGN = 637,
  WON_SIGN = 638,
  TURKISH_LIRA_SIGN = 639,
  RIGHT_ARROW = 640,
  STRCAT = 641,
  COMP_NOT_EQ = 642,
  COMP_GE = 643,
  COMP_LE = 644,
  PARAM_HEADER = 645,
  ACTIVE = 646,
  ADDDATE = 647,
  ANALYZE = 648,
  AUTO_INCREMENT = 649,
  BIT_AND = 650,
  BIT_OR = 651,
  BIT_XOR = 652,
  CACHE = 653,
  COLUMNS = 654,
  COMMITTED = 655,
  COST = 656,
  DATE_ADD = 657,
  DATE_SUB = 658,
  DECREMENT = 659,
  DENSE_RANK = 660,
  ELT = 661,
  EXPLAIN = 662,
  GE_INF_ = 663,
  GE_LE_ = 664,
  GE_LT_ = 665,
  GRANTS = 666,
  GROUP_CONCAT = 667,
  GROUPS = 668,
  GT_INF_ = 669,
  GT_LE_ = 670,
  GT_LT_ = 671,
  HASH = 672,
  IFNULL = 673,
  INACTIVE = 674,
  INCREMENT = 675,
  INDEXES = 676,
  INF_LE_ = 677,
  INF_LT_ = 678,
  INFINITE_ = 679,
  INSTANCES = 680,
  INVALIDATE = 681,
  ISNULL = 682,
  KEYS = 683,
  JAVA = 684,
  LCASE = 685,
  LOCK_ = 686,
  MAXIMUM = 687,
  MAXVALUE = 688,
  MEMBERS = 689,
  MINVALUE = 690,
  NAME = 691,
  NOCYCLE = 692,
  NOCACHE = 693,
  NOMAXVALUE = 694,
  NOMINVALUE = 695,
  PARTITION = 696,
  PARTITIONING = 697,
  PARTITIONS = 698,
  PASSWORD = 699,
  PRINT = 700,
  PRIORITY = 701,
  QUARTER = 702,
  RANGE_ = 703,
  RANK = 704,
  REJECT_ = 705,
  REMOVE = 706,
  REORGANIZE = 707,
  REPEATABLE = 708,
  RETAIN = 709,
  REUSE_OID = 710,
  REVERSE = 711,
  ROW_NUMBER = 712,
  SEPARATOR = 713,
  SERIAL = 714,
  SHOW = 715,
  STABILITY = 716,
  START_ = 717,
  STATEMENT = 718,
  STATUS = 719,
  STDDEV = 720,
  STDDEV_POP = 721,
  STDDEV_SAMP = 722,
  STR_TO_DATE = 723,
  SUBDATE = 724,
  SYSTEM = 725,
  TABLES = 726,
  THAN = 727,
  TIMEOUT = 728,
  TRACE = 729,
  TRIGGERS = 730,
  UCASE = 731,
  UNCOMMITTED = 732,
  VAR_POP = 733,
  VAR_SAMP = 734,
  VARIANCE = 735,
  WEEK = 736,
  WORKSPACE = 737,
  IdName = 738,
  BracketDelimitedIdName = 739,
  BacktickDelimitedIdName = 740,
  DelimitedIdName = 741,
  UNSIGNED_INTEGER = 742,
  UNSIGNED_REAL = 743,
  CHAR_STRING = 744,
  NCHAR_STRING = 745,
  BIT_STRING = 746,
  HEX_STRING = 747,
  CPP_STYLE_HINT = 748,
  C_STYLE_HINT = 749,
  SQL_STYLE_HINT = 750
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
  {"timediff", PT_TIMEDIFF},
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
  {"serial_current_value", PT_CURRENT_VALUE},
  {"serial_next_value", PT_NEXT_VALUE},
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
  {"week", PT_WEEKF},
  {"hex", PT_HEX},
  {"ascii", PT_ASCII},
  {"conv", PT_CONV}
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
#line 458 "../../src/parser/csql_grammar.y"
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
#line 979 "../../src/parser/csql_grammar.h"
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
