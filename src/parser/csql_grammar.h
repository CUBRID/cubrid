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
     ENUM = 369,
     EQUALS = 370,
     ESCAPE = 371,
     EVALUATE = 372,
     EXCEPT = 373,
     EXCEPTION = 374,
     EXCLUDE = 375,
     EXEC = 376,
     EXECUTE = 377,
     EXISTS = 378,
     EXTERNAL = 379,
     EXTRACT = 380,
     False = 381,
     FETCH = 382,
     File = 383,
     FIRST = 384,
     FLOAT_ = 385,
     For = 386,
     FORCE = 387,
     FOREIGN = 388,
     FOUND = 389,
     FROM = 390,
     FULL = 391,
     FUNCTION = 392,
     GENERAL = 393,
     GET = 394,
     GLOBAL = 395,
     GO = 396,
     GOTO = 397,
     GRANT = 398,
     GROUP_ = 399,
     HAVING = 400,
     HOUR_ = 401,
     HOUR_MILLISECOND = 402,
     HOUR_SECOND = 403,
     HOUR_MINUTE = 404,
     IDENTITY = 405,
     IF = 406,
     IGNORE_ = 407,
     IMMEDIATE = 408,
     IN_ = 409,
     INDEX = 410,
     INDICATOR = 411,
     INHERIT = 412,
     INITIALLY = 413,
     INNER = 414,
     INOUT = 415,
     INPUT_ = 416,
     INSERT = 417,
     INTEGER = 418,
     INTERNAL = 419,
     INTERSECT = 420,
     INTERSECTION = 421,
     INTERVAL = 422,
     INTO = 423,
     IS = 424,
     ISOLATION = 425,
     JOIN = 426,
     KEY = 427,
     KEYLIMIT = 428,
     LANGUAGE = 429,
     LAST = 430,
     LDB = 431,
     LEADING_ = 432,
     LEAVE = 433,
     LEFT = 434,
     LESS = 435,
     LEVEL = 436,
     LIKE = 437,
     LIMIT = 438,
     LIST = 439,
     LOCAL = 440,
     LOCAL_TRANSACTION_ID = 441,
     LOCALTIME = 442,
     LOCALTIMESTAMP = 443,
     LOOP = 444,
     LOWER = 445,
     MATCH = 446,
     MATCHED = 447,
     Max = 448,
     MERGE = 449,
     METHOD = 450,
     MILLISECOND_ = 451,
     Min = 452,
     MINUTE_ = 453,
     MINUTE_MILLISECOND = 454,
     MINUTE_SECOND = 455,
     MOD = 456,
     MODIFY = 457,
     MODULE = 458,
     Monetary = 459,
     MONTH_ = 460,
     MULTISET = 461,
     MULTISET_OF = 462,
     NA = 463,
     NAMES = 464,
     NATIONAL = 465,
     NATURAL = 466,
     NCHAR = 467,
     NEXT = 468,
     NO = 469,
     NONE = 470,
     NOT = 471,
     Null = 472,
     NULLIF = 473,
     NUMERIC = 474,
     OBJECT = 475,
     OCTET_LENGTH = 476,
     OF = 477,
     OFF_ = 478,
     OID_ = 479,
     ON_ = 480,
     ONLY = 481,
     OPEN = 482,
     OPERATION = 483,
     OPERATORS = 484,
     OPTIMIZATION = 485,
     OPTION = 486,
     OR = 487,
     ORDER = 488,
     OTHERS = 489,
     OUT_ = 490,
     OUTER = 491,
     OUTPUT = 492,
     OVER = 493,
     OVERLAPS = 494,
     PARAMETERS = 495,
     PARTIAL = 496,
     PENDANT = 497,
     POSITION = 498,
     PRECISION = 499,
     PREORDER = 500,
     PREPARE = 501,
     PRESERVE = 502,
     PRIMARY = 503,
     PRIOR = 504,
     Private = 505,
     PRIVILEGES = 506,
     PROCEDURE = 507,
     PROTECTED = 508,
     PROXY = 509,
     QUERY = 510,
     READ = 511,
     REBUILD = 512,
     RECURSIVE = 513,
     REF = 514,
     REFERENCES = 515,
     REFERENCING = 516,
     REGEXP = 517,
     REGISTER = 518,
     RELATIVE_ = 519,
     RENAME = 520,
     REPLACE = 521,
     RESIGNAL = 522,
     RESTRICT = 523,
     RETURN = 524,
     RETURNS = 525,
     REVOKE = 526,
     RIGHT = 527,
     RLIKE = 528,
     ROLE = 529,
     ROLLBACK = 530,
     ROLLUP = 531,
     ROUTINE = 532,
     ROW = 533,
     ROWNUM = 534,
     ROWS = 535,
     SAVEPOINT = 536,
     SCHEMA = 537,
     SCOPE = 538,
     SCROLL = 539,
     SEARCH = 540,
     SECOND_ = 541,
     SECOND_MILLISECOND = 542,
     SECTION = 543,
     SELECT = 544,
     SENSITIVE = 545,
     SEQUENCE = 546,
     SEQUENCE_OF = 547,
     SERIALIZABLE = 548,
     SESSION = 549,
     SESSION_USER = 550,
     SET = 551,
     SET_OF = 552,
     SETEQ = 553,
     SETNEQ = 554,
     SHARED = 555,
     SIBLINGS = 556,
     SIGNAL = 557,
     SIMILAR = 558,
     SIZE_ = 559,
     SmallInt = 560,
     SOME = 561,
     SQL = 562,
     SQLCODE = 563,
     SQLERROR = 564,
     SQLEXCEPTION = 565,
     SQLSTATE = 566,
     SQLWARNING = 567,
     STATISTICS = 568,
     String = 569,
     STRUCTURE = 570,
     SUBCLASS = 571,
     SUBSET = 572,
     SUBSETEQ = 573,
     SUBSTRING_ = 574,
     SUM = 575,
     SUPERCLASS = 576,
     SUPERSET = 577,
     SUPERSETEQ = 578,
     SYS_CONNECT_BY_PATH = 579,
     SYS_DATE = 580,
     SYS_DATETIME = 581,
     SYS_TIME_ = 582,
     SYS_TIMESTAMP = 583,
     SYS_USER = 584,
     SYSTEM_USER = 585,
     TABLE = 586,
     TEMPORARY = 587,
     TEST = 588,
     THEN = 589,
     THERE = 590,
     Time = 591,
     TIMESTAMP = 592,
     TIMEZONE_HOUR = 593,
     TIMEZONE_MINUTE = 594,
     TO = 595,
     TRAILING_ = 596,
     TRANSACTION = 597,
     TRANSLATE = 598,
     TRANSLATION = 599,
     TRIGGER = 600,
     TRIM = 601,
     True = 602,
     TRUNCATE = 603,
     TYPE = 604,
     UNDER = 605,
     Union = 606,
     UNIQUE = 607,
     UNKNOWN = 608,
     UNTERMINATED_STRING = 609,
     UNTERMINATED_IDENTIFIER = 610,
     UPDATE = 611,
     UPPER = 612,
     USAGE = 613,
     USE = 614,
     USER = 615,
     USING = 616,
     Utime = 617,
     VALUE = 618,
     VALUES = 619,
     VAR_ASSIGN = 620,
     VARCHAR = 621,
     VARIABLE_ = 622,
     VARYING = 623,
     VCLASS = 624,
     VIEW = 625,
     VIRTUAL = 626,
     VISIBLE = 627,
     WAIT = 628,
     WHEN = 629,
     WHENEVER = 630,
     WHERE = 631,
     WHILE = 632,
     WITH = 633,
     WITHOUT = 634,
     WORK = 635,
     WRITE = 636,
     XOR = 637,
     YEAR_ = 638,
     YEAR_MONTH = 639,
     ZONE = 640,
     YEN_SIGN = 641,
     DOLLAR_SIGN = 642,
     WON_SIGN = 643,
     TURKISH_LIRA_SIGN = 644,
     BRITISH_POUND_SIGN = 645,
     CAMBODIAN_RIEL_SIGN = 646,
     CHINESE_RENMINBI_SIGN = 647,
     INDIAN_RUPEE_SIGN = 648,
     RUSSIAN_RUBLE_SIGN = 649,
     AUSTRALIAN_DOLLAR_SIGN = 650,
     CANADIAN_DOLLAR_SIGN = 651,
     BRASILIAN_REAL_SIGN = 652,
     ROMANIAN_LEU_SIGN = 653,
     EURO_SIGN = 654,
     SWISS_FRANC_SIGN = 655,
     DANISH_KRONE_SIGN = 656,
     NORWEGIAN_KRONE_SIGN = 657,
     BULGARIAN_LEV_SIGN = 658,
     VIETNAMESE_DONG_SIGN = 659,
     CZECH_KORUNA_SIGN = 660,
     POLISH_ZLOTY_SIGN = 661,
     SWEDISH_KRONA_SIGN = 662,
     CROATIAN_KUNA_SIGN = 663,
     SERBIAN_DINAR_SIGN = 664,
     RIGHT_ARROW = 665,
     STRCAT = 666,
     COMP_NOT_EQ = 667,
     COMP_GE = 668,
     COMP_LE = 669,
     PARAM_HEADER = 670,
     ACTIVE = 671,
     ADDDATE = 672,
     ANALYZE = 673,
     AUTO_INCREMENT = 674,
     BIT_AND = 675,
     BIT_OR = 676,
     BIT_XOR = 677,
     CACHE = 678,
     COLUMNS = 679,
     COMMITTED = 680,
     COST = 681,
     DATE_ADD = 682,
     DATE_SUB = 683,
     DECREMENT = 684,
     DENSE_RANK = 685,
     ELT = 686,
     EXPLAIN = 687,
     GE_INF_ = 688,
     GE_LE_ = 689,
     GE_LT_ = 690,
     GRANTS = 691,
     GROUP_CONCAT = 692,
     GROUPS = 693,
     GT_INF_ = 694,
     GT_LE_ = 695,
     GT_LT_ = 696,
     HASH = 697,
     IFNULL = 698,
     INACTIVE = 699,
     INCREMENT = 700,
     INDEXES = 701,
     INF_LE_ = 702,
     INF_LT_ = 703,
     INFINITE_ = 704,
     INSTANCES = 705,
     INVALIDATE = 706,
     ISNULL = 707,
     KEYS = 708,
     JAVA = 709,
     LCASE = 710,
     LOCK_ = 711,
     MAXIMUM = 712,
     MAXVALUE = 713,
     MEMBERS = 714,
     MINVALUE = 715,
     NAME = 716,
     NOCYCLE = 717,
     NOCACHE = 718,
     NOMAXVALUE = 719,
     NOMINVALUE = 720,
     OFFSET = 721,
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
/* Line 2616 of glr.c.  */
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


