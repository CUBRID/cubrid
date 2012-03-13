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
     BINARY = 282,
     BIT = 283,
     BIT_LENGTH = 284,
     BITSHIFT_LEFT = 285,
     BITSHIFT_RIGHT = 286,
     BLOB_ = 287,
     BOOLEAN_ = 288,
     BOTH_ = 289,
     BREADTH = 290,
     BY = 291,
     CALL = 292,
     CASCADE = 293,
     CASCADED = 294,
     CASE = 295,
     CAST = 296,
     CATALOG = 297,
     CHANGE = 298,
     CHAR_ = 299,
     CHECK = 300,
     CLASS = 301,
     CLASSES = 302,
     CLOB_ = 303,
     CLOSE = 304,
     CLUSTER = 305,
     COALESCE = 306,
     COLLATE = 307,
     COLLATION = 308,
     COLUMN = 309,
     COMMIT = 310,
     COMP_NULLSAFE_EQ = 311,
     COMPLETION = 312,
     CONNECT = 313,
     CONNECT_BY_ISCYCLE = 314,
     CONNECT_BY_ISLEAF = 315,
     CONNECT_BY_ROOT = 316,
     CONNECTION = 317,
     CONSTRAINT = 318,
     CONSTRAINTS = 319,
     CONTINUE = 320,
     CONVERT = 321,
     CORRESPONDING = 322,
     COUNT = 323,
     CREATE = 324,
     CROSS = 325,
     CURRENT = 326,
     CURRENT_DATE = 327,
     CURRENT_DATETIME = 328,
     CURRENT_TIME = 329,
     CURRENT_TIMESTAMP = 330,
     CURRENT_USER = 331,
     CURSOR = 332,
     CYCLE = 333,
     DATA = 334,
     DATABASE = 335,
     DATA_TYPE = 336,
     Date = 337,
     DATETIME = 338,
     DAY_ = 339,
     DAY_MILLISECOND = 340,
     DAY_SECOND = 341,
     DAY_MINUTE = 342,
     DAY_HOUR = 343,
     DEALLOCATE = 344,
     DECLARE = 345,
     DEFAULT = 346,
     DEFERRABLE = 347,
     DEFERRED = 348,
     DELETE_ = 349,
     DEPTH = 350,
     DESC = 351,
     DESCRIBE = 352,
     DESCRIPTOR = 353,
     DIAGNOSTICS = 354,
     DICTIONARY = 355,
     DIFFERENCE_ = 356,
     DISCONNECT = 357,
     DISTINCT = 358,
     DISTINCTROW = 359,
     DIV = 360,
     DO = 361,
     Domain = 362,
     Double = 363,
     DROP = 364,
     DUPLICATE_ = 365,
     EACH = 366,
     ELSE = 367,
     ELSEIF = 368,
     END = 369,
     ENUM = 370,
     EQUALS = 371,
     ESCAPE = 372,
     EVALUATE = 373,
     EXCEPT = 374,
     EXCEPTION = 375,
     EXCLUDE = 376,
     EXEC = 377,
     EXECUTE = 378,
     EXISTS = 379,
     EXTERNAL = 380,
     EXTRACT = 381,
     False = 382,
     FETCH = 383,
     File = 384,
     FIRST = 385,
     FLOAT_ = 386,
     For = 387,
     FORCE = 388,
     FOREIGN = 389,
     FOUND = 390,
     FROM = 391,
     FULL = 392,
     FUNCTION = 393,
     GENERAL = 394,
     GET = 395,
     GLOBAL = 396,
     GO = 397,
     GOTO = 398,
     GRANT = 399,
     GROUP_ = 400,
     HAVING = 401,
     HOUR_ = 402,
     HOUR_MILLISECOND = 403,
     HOUR_SECOND = 404,
     HOUR_MINUTE = 405,
     IDENTITY = 406,
     IF = 407,
     IGNORE_ = 408,
     IMMEDIATE = 409,
     IN_ = 410,
     INDEX = 411,
     INDICATOR = 412,
     INHERIT = 413,
     INITIALLY = 414,
     INNER = 415,
     INOUT = 416,
     INPUT_ = 417,
     INSERT = 418,
     INTEGER = 419,
     INTERNAL = 420,
     INTERSECT = 421,
     INTERSECTION = 422,
     INTERVAL = 423,
     INTO = 424,
     IS = 425,
     ISOLATION = 426,
     JOIN = 427,
     KEY = 428,
     KEYLIMIT = 429,
     LANGUAGE = 430,
     LAST = 431,
     LDB = 432,
     LEADING_ = 433,
     LEAVE = 434,
     LEFT = 435,
     LESS = 436,
     LEVEL = 437,
     LIKE = 438,
     LIMIT = 439,
     LIST = 440,
     LOCAL = 441,
     LOCAL_TRANSACTION_ID = 442,
     LOCALTIME = 443,
     LOCALTIMESTAMP = 444,
     LOOP = 445,
     LOWER = 446,
     MATCH = 447,
     MATCHED = 448,
     Max = 449,
     MERGE = 450,
     METHOD = 451,
     MILLISECOND_ = 452,
     Min = 453,
     MINUTE_ = 454,
     MINUTE_MILLISECOND = 455,
     MINUTE_SECOND = 456,
     MOD = 457,
     MODIFY = 458,
     MODULE = 459,
     Monetary = 460,
     MONTH_ = 461,
     MULTISET = 462,
     MULTISET_OF = 463,
     NA = 464,
     NAMES = 465,
     NATIONAL = 466,
     NATURAL = 467,
     NCHAR = 468,
     NEXT = 469,
     NO = 470,
     NONE = 471,
     NOT = 472,
     Null = 473,
     NULLIF = 474,
     NUMERIC = 475,
     OBJECT = 476,
     OCTET_LENGTH = 477,
     OF = 478,
     OFF_ = 479,
     OID_ = 480,
     ON_ = 481,
     ONLY = 482,
     OPEN = 483,
     OPERATION = 484,
     OPERATORS = 485,
     OPTIMIZATION = 486,
     OPTION = 487,
     OR = 488,
     ORDER = 489,
     OTHERS = 490,
     OUT_ = 491,
     OUTER = 492,
     OUTPUT = 493,
     OVER = 494,
     OVERLAPS = 495,
     PARAMETERS = 496,
     PARTIAL = 497,
     PENDANT = 498,
     POSITION = 499,
     PRECISION = 500,
     PREORDER = 501,
     PREPARE = 502,
     PRESERVE = 503,
     PRIMARY = 504,
     PRIOR = 505,
     Private = 506,
     PRIVILEGES = 507,
     PROCEDURE = 508,
     PROTECTED = 509,
     PROXY = 510,
     QUERY = 511,
     READ = 512,
     REBUILD = 513,
     RECURSIVE = 514,
     REF = 515,
     REFERENCES = 516,
     REFERENCING = 517,
     REGEXP = 518,
     REGISTER = 519,
     RELATIVE_ = 520,
     RENAME = 521,
     REPLACE = 522,
     RESIGNAL = 523,
     RESTRICT = 524,
     RETURN = 525,
     RETURNS = 526,
     REVOKE = 527,
     RIGHT = 528,
     RLIKE = 529,
     ROLE = 530,
     ROLLBACK = 531,
     ROLLUP = 532,
     ROUTINE = 533,
     ROW = 534,
     ROWNUM = 535,
     ROWS = 536,
     SAVEPOINT = 537,
     SCHEMA = 538,
     SCOPE = 539,
     SCROLL = 540,
     SEARCH = 541,
     SECOND_ = 542,
     SECOND_MILLISECOND = 543,
     SECTION = 544,
     SELECT = 545,
     SENSITIVE = 546,
     SEQUENCE = 547,
     SEQUENCE_OF = 548,
     SERIALIZABLE = 549,
     SESSION = 550,
     SESSION_USER = 551,
     SET = 552,
     SET_OF = 553,
     SETEQ = 554,
     SETNEQ = 555,
     SHARED = 556,
     SIBLINGS = 557,
     SIGNAL = 558,
     SIMILAR = 559,
     SIZE_ = 560,
     SmallInt = 561,
     SOME = 562,
     SQL = 563,
     SQLCODE = 564,
     SQLERROR = 565,
     SQLEXCEPTION = 566,
     SQLSTATE = 567,
     SQLWARNING = 568,
     STATISTICS = 569,
     String = 570,
     STRUCTURE = 571,
     SUBCLASS = 572,
     SUBSET = 573,
     SUBSETEQ = 574,
     SUBSTRING_ = 575,
     SUM = 576,
     SUPERCLASS = 577,
     SUPERSET = 578,
     SUPERSETEQ = 579,
     SYS_CONNECT_BY_PATH = 580,
     SYS_DATE = 581,
     SYS_DATETIME = 582,
     SYS_TIME_ = 583,
     SYS_TIMESTAMP = 584,
     SYS_USER = 585,
     SYSTEM_USER = 586,
     TABLE = 587,
     TEMPORARY = 588,
     TEST = 589,
     THEN = 590,
     THERE = 591,
     Time = 592,
     TIMESTAMP = 593,
     TIMEZONE_HOUR = 594,
     TIMEZONE_MINUTE = 595,
     TO = 596,
     TRAILING_ = 597,
     TRANSACTION = 598,
     TRANSLATE = 599,
     TRANSLATION = 600,
     TRIGGER = 601,
     TRIM = 602,
     True = 603,
     TRUNCATE = 604,
     TYPE = 605,
     UNDER = 606,
     Union = 607,
     UNIQUE = 608,
     UNKNOWN = 609,
     UNTERMINATED_STRING = 610,
     UNTERMINATED_IDENTIFIER = 611,
     UPDATE = 612,
     UPPER = 613,
     USAGE = 614,
     USE = 615,
     USER = 616,
     USING = 617,
     Utime = 618,
     VALUE = 619,
     VALUES = 620,
     VAR_ASSIGN = 621,
     VARCHAR = 622,
     VARIABLE_ = 623,
     VARYING = 624,
     VCLASS = 625,
     VIEW = 626,
     VIRTUAL = 627,
     VISIBLE = 628,
     WAIT = 629,
     WHEN = 630,
     WHENEVER = 631,
     WHERE = 632,
     WHILE = 633,
     WITH = 634,
     WITHOUT = 635,
     WORK = 636,
     WRITE = 637,
     XOR = 638,
     YEAR_ = 639,
     YEAR_MONTH = 640,
     ZONE = 641,
     YEN_SIGN = 642,
     DOLLAR_SIGN = 643,
     WON_SIGN = 644,
     TURKISH_LIRA_SIGN = 645,
     BRITISH_POUND_SIGN = 646,
     CAMBODIAN_RIEL_SIGN = 647,
     CHINESE_RENMINBI_SIGN = 648,
     INDIAN_RUPEE_SIGN = 649,
     RUSSIAN_RUBLE_SIGN = 650,
     AUSTRALIAN_DOLLAR_SIGN = 651,
     CANADIAN_DOLLAR_SIGN = 652,
     BRASILIAN_REAL_SIGN = 653,
     ROMANIAN_LEU_SIGN = 654,
     EURO_SIGN = 655,
     SWISS_FRANC_SIGN = 656,
     DANISH_KRONE_SIGN = 657,
     NORWEGIAN_KRONE_SIGN = 658,
     BULGARIAN_LEV_SIGN = 659,
     VIETNAMESE_DONG_SIGN = 660,
     CZECH_KORUNA_SIGN = 661,
     POLISH_ZLOTY_SIGN = 662,
     SWEDISH_KRONA_SIGN = 663,
     CROATIAN_KUNA_SIGN = 664,
     SERBIAN_DINAR_SIGN = 665,
     RIGHT_ARROW = 666,
     STRCAT = 667,
     COMP_NOT_EQ = 668,
     COMP_GE = 669,
     COMP_LE = 670,
     PARAM_HEADER = 671,
     ACTIVE = 672,
     ADDDATE = 673,
     ANALYZE = 674,
     AUTO_INCREMENT = 675,
     BIT_AND = 676,
     BIT_OR = 677,
     BIT_XOR = 678,
     CACHE = 679,
     COLUMNS = 680,
     COMMITTED = 681,
     COST = 682,
     DATE_ADD = 683,
     DATE_SUB = 684,
     DECREMENT = 685,
     DENSE_RANK = 686,
     ELT = 687,
     EXPLAIN = 688,
     GE_INF_ = 689,
     GE_LE_ = 690,
     GE_LT_ = 691,
     GRANTS = 692,
     GROUP_CONCAT = 693,
     GROUPS = 694,
     GT_INF_ = 695,
     GT_LE_ = 696,
     GT_LT_ = 697,
     HASH = 698,
     IFNULL = 699,
     INACTIVE = 700,
     INCREMENT = 701,
     INDEXES = 702,
     INF_LE_ = 703,
     INF_LT_ = 704,
     INFINITE_ = 705,
     INSTANCES = 706,
     INVALIDATE = 707,
     ISNULL = 708,
     KEYS = 709,
     JAVA = 710,
     LCASE = 711,
     LOCK_ = 712,
     MAXIMUM = 713,
     MAXVALUE = 714,
     MEMBERS = 715,
     MINVALUE = 716,
     NAME = 717,
     NOCYCLE = 718,
     NOCACHE = 719,
     NOMAXVALUE = 720,
     NOMINVALUE = 721,
     PARTITION = 722,
     PARTITIONING = 723,
     PARTITIONS = 724,
     PASSWORD = 725,
     PRINT = 726,
     PRIORITY = 727,
     QUARTER = 728,
     RANGE_ = 729,
     RANK = 730,
     REJECT_ = 731,
     REMOVE = 732,
     REORGANIZE = 733,
     REPEATABLE = 734,
     RETAIN = 735,
     REUSE_OID = 736,
     REVERSE = 737,
     ROW_NUMBER = 738,
     SEPARATOR = 739,
     SERIAL = 740,
     SHOW = 741,
     STABILITY = 742,
     START_ = 743,
     STATEMENT = 744,
     STATUS = 745,
     STDDEV = 746,
     STDDEV_POP = 747,
     STDDEV_SAMP = 748,
     STR_TO_DATE = 749,
     SUBDATE = 750,
     SYSTEM = 751,
     TABLES = 752,
     THAN = 753,
     TIMEOUT = 754,
     TRACE = 755,
     TRIGGERS = 756,
     UCASE = 757,
     UNCOMMITTED = 758,
     VAR_POP = 759,
     VAR_SAMP = 760,
     VARIANCE = 761,
     WEEK = 762,
     WORKSPACE = 763,
     IdName = 764,
     BracketDelimitedIdName = 765,
     BacktickDelimitedIdName = 766,
     DelimitedIdName = 767,
     UNSIGNED_INTEGER = 768,
     UNSIGNED_REAL = 769,
     CHAR_STRING = 770,
     NCHAR_STRING = 771,
     BIT_STRING = 772,
     HEX_STRING = 773,
     CPP_STYLE_HINT = 774,
     C_STYLE_HINT = 775,
     SQL_STYLE_HINT = 776
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

#define YEN_SIGN_TEXT           "(\0xa1\0xef)"
#define DOLLAR_SIGN_TEXT        "$"
#define WON_SIGN_TEXT           "\\"
#define TURKISH_LIRA_TEXT       "TL"
#define BRITISH_POUND_TEXT      "GBP"
#define CAMBODIAN_RIEL_TEXT     "KHR"
#define CHINESE_RENMINBI_TEXT   "CNY"
#define INDIAN_RUPEE_TEXT       "INR"
#define RUSSIAN_RUBLE_TEXT      "RUB"
#define AUSTRALIAN_DOLLAR_TEXT  "AUD"
#define CANADIAN_DOLLAR_TEXT    "CAD"
#define BRASILIAN_REAL_TEXT     "BRL"
#define ROMANIAN_LEU_TEXT       "RON"
#define EURO_TEXT               "EUR"
#define SWISS_FRANC_TEXT        "CHF"
#define DANISH_KRONE_TEXT       "DKK"
#define NORWEGIAN_KRONE_TEXT    "NOK"
#define BULGARIAN_LEV_TEXT      "BGN"
#define VIETNAMESE_DONG_TEXT    "VND"
#define CZECH_KORUNA_TEXT       "CZK"
#define POLISH_ZLOTY_TEXT       "PLN"
#define SWEDISH_KRONA_TEXT      "SEK"
#define CROATIAN_KUNA_TEXT      "HRK"
#define SERBIAN_DINAR_TEXT      "RSD"

#define PARSER_SAVE_ERR_CONTEXT(node, context) \
  if ((node) && (node)->buffer_pos == -1) \
    { \
     (node)->buffer_pos = context; \
    }

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

int yybuffer_pos;

#define push_msg(a) _push_msg(a, __LINE__)

void _push_msg (int code, int line);
void pop_msg (void);

/* 
 * The default YYLTYPE structure is extended so that locations can hold
 * context information
 */
typedef struct YYLTYPE
{

  int first_line;
  int first_column;
  int last_line;
  int last_column;
  int buffer_pos; /* position in the buffer being parsed */

} YYLTYPE;
#define YYLTYPE_IS_DECLARED 1

/*
 * The behavior of location propagation when a rule is matched must
 * take into account the context information. The left-side symbol in a rule
 * will have the same context information as the last symbol from its 
 * right side
 */
#define YYLLOC_DEFAULT(Current, Rhs, N)				        \
    do									\
      if (YYID (N))							\
	{								\
	  (Current).first_line   = YYRHSLOC (Rhs, 1).first_line;	\
	  (Current).first_column = YYRHSLOC (Rhs, 1).first_column;	\
	  (Current).last_line    = YYRHSLOC (Rhs, N).last_line;		\
	  (Current).last_column  = YYRHSLOC (Rhs, N).last_column;	\
	  (Current).buffer_pos   = YYRHSLOC (Rhs, N).buffer_pos;	\
	}								\
      else								\
	{								\
	  (Current).first_line   = (Current).last_line   =		\
	    YYRHSLOC (Rhs, 0).last_line;				\
	  (Current).first_column = (Current).last_column =		\
	    YYRHSLOC (Rhs, 0).last_column;				\
	  (Current).buffer_pos   = YYRHSLOC (Rhs, 0).buffer_pos;	\
	}								\
    while (YYID (0))

/* 
 * YY_LOCATION_PRINT -- Print the location on the stream.
 * This macro was not mandated originally: define only if we know
 * we won't break user code: when these are the locations we know.  
 */

#define YY_LOCATION_PRINT(File, Loc)			\
    fprintf (File, "%d.%d-%d.%d",			\
	     (Loc).first_line, (Loc).first_column,	\
	     (Loc).last_line,  (Loc).last_column)



#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
typedef union YYSTYPE 
#line 539 "../../src/parser/csql_grammar.y"
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
#line 1085 "../../src/parser/csql_grammar.h"
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


