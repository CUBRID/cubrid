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
     ALL = 263,
     ALLOCATE = 264,
     ALTER = 265,
     AND = 266,
     ANY = 267,
     ARE = 268,
     AS = 269,
     ASC = 270,
     ASSERTION = 271,
     ASYNC = 272,
     AT = 273,
     ATTACH = 274,
     ATTRIBUTE = 275,
     AVG = 276,
     BEFORE = 277,
     BEGIN_ = 278,
     BETWEEN = 279,
     BIGINT = 280,
     BINARY = 281,
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
     COALESCE = 304,
     COLLATE = 305,
     COLUMN = 306,
     COMMIT = 307,
     COMP_NULLSAFE_EQ = 308,
     CONNECT = 309,
     CONNECT_BY_ISCYCLE = 310,
     CONNECT_BY_ISLEAF = 311,
     CONNECT_BY_ROOT = 312,
     CONNECTION = 313,
     CONSTRAINT = 314,
     CONSTRAINTS = 315,
     CONTINUE = 316,
     CONVERT = 317,
     CORRESPONDING = 318,
     COUNT = 319,
     CREATE = 320,
     CROSS = 321,
     CURRENT = 322,
     CURRENT_DATE = 323,
     CURRENT_DATETIME = 324,
     CURRENT_TIME = 325,
     CURRENT_TIMESTAMP = 326,
     CURRENT_USER = 327,
     CURSOR = 328,
     CYCLE = 329,
     DATA = 330,
     DATABASE = 331,
     DATA_TYPE_ = 332,
     Date = 333,
     DATETIME = 334,
     DAY_ = 335,
     DAY_MILLISECOND = 336,
     DAY_SECOND = 337,
     DAY_MINUTE = 338,
     DAY_HOUR = 339,
     DEALLOCATE = 340,
     DECLARE = 341,
     DEFAULT = 342,
     DEFERRABLE = 343,
     DEFERRED = 344,
     DELETE_ = 345,
     DEPTH = 346,
     DESC = 347,
     DESCRIBE = 348,
     DESCRIPTOR = 349,
     DIAGNOSTICS = 350,
     DIFFERENCE_ = 351,
     DISCONNECT = 352,
     DISTINCT = 353,
     DIV = 354,
     DO = 355,
     Domain = 356,
     Double = 357,
     DROP = 358,
     DUPLICATE_ = 359,
     EACH = 360,
     ELSE = 361,
     ELSEIF = 362,
     END = 363,
     ENUM = 364,
     EQUALS = 365,
     ESCAPE = 366,
     EVALUATE = 367,
     EXCEPT = 368,
     EXCEPTION = 369,
     EXEC = 370,
     EXECUTE = 371,
     EXISTS = 372,
     EXTERNAL = 373,
     EXTRACT = 374,
     False = 375,
     FETCH = 376,
     File = 377,
     FIRST = 378,
     FLOAT_ = 379,
     For = 380,
     FORCE = 381,
     FOREIGN = 382,
     FOUND = 383,
     FROM = 384,
     FULL = 385,
     FUNCTION = 386,
     GENERAL = 387,
     GET = 388,
     GLOBAL = 389,
     GO = 390,
     GOTO = 391,
     GRANT = 392,
     GROUP_ = 393,
     HAVING = 394,
     HOUR_ = 395,
     HOUR_MILLISECOND = 396,
     HOUR_SECOND = 397,
     HOUR_MINUTE = 398,
     IDENTITY = 399,
     IF = 400,
     IGNORE_ = 401,
     IMMEDIATE = 402,
     IN_ = 403,
     INDEX = 404,
     INDICATOR = 405,
     INHERIT = 406,
     INITIALLY = 407,
     INNER = 408,
     INOUT = 409,
     INPUT_ = 410,
     INSERT = 411,
     INTEGER = 412,
     INTERNAL = 413,
     INTERSECT = 414,
     INTERSECTION = 415,
     INTERVAL = 416,
     INTO = 417,
     IS = 418,
     ISOLATION = 419,
     JOIN = 420,
     KEY = 421,
     KEYLIMIT = 422,
     LANGUAGE = 423,
     LAST = 424,
     LEADING_ = 425,
     LEAVE = 426,
     LEFT = 427,
     LESS = 428,
     LEVEL = 429,
     LIKE = 430,
     LIMIT = 431,
     LIST = 432,
     LOCAL = 433,
     LOCAL_TRANSACTION_ID = 434,
     LOCALTIME = 435,
     LOCALTIMESTAMP = 436,
     LOOP = 437,
     LOWER = 438,
     MATCH = 439,
     MATCHED = 440,
     Max = 441,
     MERGE = 442,
     METHOD = 443,
     MILLISECOND_ = 444,
     Min = 445,
     MINUTE_ = 446,
     MINUTE_MILLISECOND = 447,
     MINUTE_SECOND = 448,
     MOD = 449,
     MODIFY = 450,
     MODULE = 451,
     Monetary = 452,
     MONTH_ = 453,
     MULTISET = 454,
     MULTISET_OF = 455,
     NA = 456,
     NAMES = 457,
     NATIONAL = 458,
     NATURAL = 459,
     NCHAR = 460,
     NEXT = 461,
     NO = 462,
     NONE = 463,
     NOT = 464,
     Null = 465,
     NULLIF = 466,
     NULLS = 467,
     NUMERIC = 468,
     OBJECT = 469,
     OCTET_LENGTH = 470,
     OF = 471,
     OFF_ = 472,
     ON_ = 473,
     ONLY = 474,
     OPEN = 475,
     OPTIMIZATION = 476,
     OPTION = 477,
     OR = 478,
     ORDER = 479,
     OUT_ = 480,
     OUTER = 481,
     OUTPUT = 482,
     OVER = 483,
     OVERLAPS = 484,
     PARAMETERS = 485,
     PARTIAL = 486,
     PARTITION = 487,
     POSITION = 488,
     PRECISION = 489,
     PREPARE = 490,
     PRESERVE = 491,
     PRIMARY = 492,
     PRIOR = 493,
     PRIVILEGES = 494,
     PROCEDURE = 495,
     PROMOTE = 496,
     QUERY = 497,
     READ = 498,
     REBUILD = 499,
     RECURSIVE = 500,
     REF = 501,
     REFERENCES = 502,
     REFERENCING = 503,
     REGEXP = 504,
     RELATIVE_ = 505,
     RENAME = 506,
     REPLACE = 507,
     RESIGNAL = 508,
     RESPECT = 509,
     RESTRICT = 510,
     RETURN = 511,
     RETURNS = 512,
     REVOKE = 513,
     RIGHT = 514,
     RLIKE = 515,
     ROLE = 516,
     ROLLBACK = 517,
     ROLLUP = 518,
     ROUTINE = 519,
     ROW = 520,
     ROWNUM = 521,
     ROWS = 522,
     SAVEPOINT = 523,
     SCHEMA = 524,
     SCOPE = 525,
     SCROLL = 526,
     SEARCH = 527,
     SECOND_ = 528,
     SECOND_MILLISECOND = 529,
     SECTION = 530,
     SELECT = 531,
     SENSITIVE = 532,
     SEQUENCE = 533,
     SEQUENCE_OF = 534,
     SERIALIZABLE = 535,
     SESSION = 536,
     SESSION_USER = 537,
     SET = 538,
     SET_OF = 539,
     SETEQ = 540,
     SETNEQ = 541,
     SHARED = 542,
     SIBLINGS = 543,
     SIGNAL = 544,
     SIMILAR = 545,
     SIZE_ = 546,
     SmallInt = 547,
     SOME = 548,
     SQL = 549,
     SQLCODE = 550,
     SQLERROR = 551,
     SQLEXCEPTION = 552,
     SQLSTATE = 553,
     SQLWARNING = 554,
     STATISTICS = 555,
     String = 556,
     SUBCLASS = 557,
     SUBSET = 558,
     SUBSETEQ = 559,
     SUBSTRING_ = 560,
     SUM = 561,
     SUPERCLASS = 562,
     SUPERSET = 563,
     SUPERSETEQ = 564,
     SYS_CONNECT_BY_PATH = 565,
     SYS_DATE = 566,
     SYS_DATETIME = 567,
     SYS_TIME_ = 568,
     SYS_TIMESTAMP = 569,
     SYSTEM_USER = 570,
     TABLE = 571,
     TEMPORARY = 572,
     THEN = 573,
     Time = 574,
     TIMESTAMP = 575,
     TIMEZONE_HOUR = 576,
     TIMEZONE_MINUTE = 577,
     TO = 578,
     TRAILING_ = 579,
     TRANSACTION = 580,
     TRANSLATE = 581,
     TRANSLATION = 582,
     TRIGGER = 583,
     TRIM = 584,
     True = 585,
     TRUNCATE = 586,
     UNDER = 587,
     Union = 588,
     UNIQUE = 589,
     UNKNOWN = 590,
     UNTERMINATED_STRING = 591,
     UNTERMINATED_IDENTIFIER = 592,
     UPDATE = 593,
     UPPER = 594,
     USAGE = 595,
     USE = 596,
     USER = 597,
     USING = 598,
     Utime = 599,
     VALUE = 600,
     VALUES = 601,
     VAR_ASSIGN = 602,
     VARCHAR = 603,
     VARIABLE_ = 604,
     VARYING = 605,
     VCLASS = 606,
     VIEW = 607,
     WHEN = 608,
     WHENEVER = 609,
     WHERE = 610,
     WHILE = 611,
     WITH = 612,
     WITHIN = 613,
     WITHOUT = 614,
     WORK = 615,
     WRITE = 616,
     XOR = 617,
     YEAR_ = 618,
     YEAR_MONTH = 619,
     ZONE = 620,
     YEN_SIGN = 621,
     DOLLAR_SIGN = 622,
     WON_SIGN = 623,
     TURKISH_LIRA_SIGN = 624,
     BRITISH_POUND_SIGN = 625,
     CAMBODIAN_RIEL_SIGN = 626,
     CHINESE_RENMINBI_SIGN = 627,
     INDIAN_RUPEE_SIGN = 628,
     RUSSIAN_RUBLE_SIGN = 629,
     AUSTRALIAN_DOLLAR_SIGN = 630,
     CANADIAN_DOLLAR_SIGN = 631,
     BRASILIAN_REAL_SIGN = 632,
     ROMANIAN_LEU_SIGN = 633,
     EURO_SIGN = 634,
     SWISS_FRANC_SIGN = 635,
     DANISH_KRONE_SIGN = 636,
     NORWEGIAN_KRONE_SIGN = 637,
     BULGARIAN_LEV_SIGN = 638,
     VIETNAMESE_DONG_SIGN = 639,
     CZECH_KORUNA_SIGN = 640,
     POLISH_ZLOTY_SIGN = 641,
     SWEDISH_KRONA_SIGN = 642,
     CROATIAN_KUNA_SIGN = 643,
     SERBIAN_DINAR_SIGN = 644,
     DOT = 645,
     RIGHT_ARROW = 646,
     STRCAT = 647,
     COMP_NOT_EQ = 648,
     COMP_GE = 649,
     COMP_LE = 650,
     PARAM_HEADER = 651,
     ACTIVE = 652,
     ADDDATE = 653,
     ANALYZE = 654,
     AUTO_INCREMENT = 655,
     BIT_AND = 656,
     BIT_OR = 657,
     BIT_XOR = 658,
     CACHE = 659,
     CHARACTER_SET_ = 660,
     CHARSET = 661,
     CHR = 662,
     CLOB_TO_CHAR = 663,
     COLLATION = 664,
     COLUMNS = 665,
     COMMITTED = 666,
     COST = 667,
     CUME_DIST = 668,
     DATE_ADD = 669,
     DATE_SUB = 670,
     DECREMENT = 671,
     DENSE_RANK = 672,
     ELT = 673,
     EXPLAIN = 674,
     FIRST_VALUE = 675,
     GE_INF_ = 676,
     GE_LE_ = 677,
     GE_LT_ = 678,
     GRANTS = 679,
     GROUP_CONCAT = 680,
     GROUPS = 681,
     GT_INF_ = 682,
     GT_LE_ = 683,
     GT_LT_ = 684,
     HASH = 685,
     IFNULL = 686,
     INACTIVE = 687,
     INCREMENT = 688,
     INDEXES = 689,
     INF_LE_ = 690,
     INF_LT_ = 691,
     INFINITE_ = 692,
     INSTANCES = 693,
     INVALIDATE = 694,
     ISNULL = 695,
     KEYS = 696,
     JAVA = 697,
     JSON = 698,
     LAG = 699,
     LAST_VALUE = 700,
     LCASE = 701,
     LEAD = 702,
     LOCK_ = 703,
     MAXIMUM = 704,
     MAXVALUE = 705,
     MEDIAN = 706,
     MEMBERS = 707,
     MINVALUE = 708,
     NAME = 709,
     NOCYCLE = 710,
     NOCACHE = 711,
     NOMAXVALUE = 712,
     NOMINVALUE = 713,
     NTH_VALUE = 714,
     NTILE = 715,
     OFFSET = 716,
     OWNER = 717,
     PARTITIONING = 718,
     PARTITIONS = 719,
     PASSWORD = 720,
     PERCENT_RANK = 721,
     PRINT = 722,
     PRIORITY = 723,
     QUARTER = 724,
     RANGE_ = 725,
     RANK = 726,
     REJECT_ = 727,
     REMOVE = 728,
     REORGANIZE = 729,
     REPEATABLE = 730,
     RETAIN = 731,
     REUSE_OID = 732,
     REVERSE = 733,
     ROW_NUMBER = 734,
     SEPARATOR = 735,
     SERIAL = 736,
     SHOW = 737,
     STABILITY = 738,
     START_ = 739,
     STATEMENT = 740,
     STATUS = 741,
     STDDEV = 742,
     STDDEV_POP = 743,
     STDDEV_SAMP = 744,
     STR_TO_DATE = 745,
     SUBDATE = 746,
     SYSTEM = 747,
     TABLES = 748,
     TEXT = 749,
     THAN = 750,
     TIMEOUT = 751,
     TRACE = 752,
     TRIGGERS = 753,
     UCASE = 754,
     UNCOMMITTED = 755,
     VAR_POP = 756,
     VAR_SAMP = 757,
     VARIANCE = 758,
     WEEK = 759,
     WORKSPACE = 760,
     IdName = 761,
     BracketDelimitedIdName = 762,
     BacktickDelimitedIdName = 763,
     DelimitedIdName = 764,
     UNSIGNED_INTEGER = 765,
     UNSIGNED_REAL = 766,
     CHAR_STRING = 767,
     NCHAR_STRING = 768,
     BIT_STRING = 769,
     HEX_STRING = 770,
     CPP_STYLE_HINT = 771,
     C_STYLE_HINT = 772,
     SQL_STYLE_HINT = 773,
     EUCKR_STRING = 774,
     ISO_STRING = 775,
     UTF8_STRING = 776
   };
#endif


/* Copy the first part of user declarations.  */
#line 26 "../src/parser/csql_grammar.y"

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
#include "unicode_support.h"
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
  {"clob_from_file", PT_CLOB_FROM_FILE},
  {"clob_length", PT_CLOB_LENGTH},
  {"concat", PT_CONCAT},
  {"concat_ws", PT_CONCAT_WS},
  {"cos", PT_COS},
  {"cot", PT_COT},
  {"cume_dist", PT_CUME_DIST},
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
  {"percent_rank", PT_PERCENT_RANK},
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
  {"aes_encrypt", PT_AES_ENCRYPT},
  {"aes_decrypt", PT_AES_DECRYPT},	
  {"sha1", PT_SHA_ONE},	
  {"sha2", PT_SHA_TWO},	
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
  {"conv", PT_CONV},
  {"inet_aton", PT_INET_ATON},
  {"inet_ntoa", PT_INET_NTOA},
  {"coercibility", PT_COERCIBILITY},
  {"width_bucket", PT_WIDTH_BUCKET},
  {"trace_stats", PT_TRACE_STATS}
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
static bool parser_is_select_stmt_node_empty (void);

static void parser_push_hint_node (PT_NODE * node);
static PT_NODE *parser_top_hint_node (void);
static PT_NODE *parser_pop_hint_node (void);
static bool parser_is_hint_node_empty (void);

static void parser_push_join_type (int v);
static int parser_top_join_type (void);
static int parser_pop_join_type (void);

static void parser_save_is_reverse (bool v);
static bool parser_get_is_reverse (void);

static void parser_initialize_parser_context (void);
static PT_NODE *parser_make_date_lang (int arg_cnt, PT_NODE * arg3);
static PT_NODE *parser_make_number_lang (const int argc);
static void parser_remove_dummy_select (PT_NODE ** node);
static int parser_count_list (PT_NODE * list);
static int parser_count_prefix_columns (PT_NODE * list, int * arg_count);

static void resolve_alias_in_expr_node (PT_NODE * node, PT_NODE * list);
static void resolve_alias_in_name_node (PT_NODE ** node, PT_NODE * list);
static char * pt_check_identifier (PARSER_CONTEXT *parser, PT_NODE *p,
				   const char *str, const int str_size);
static PT_NODE * pt_create_char_string_literal (PARSER_CONTEXT *parser,
						const PT_TYPE_ENUM char_type,
						const char *str,
						const INTL_CODESET codeset);
static PT_NODE * pt_create_date_value (PARSER_CONTEXT *parser,
				       const PT_TYPE_ENUM type,
				       const char *str);
static void pt_value_set_charset_coll (PARSER_CONTEXT *parser,
				       PT_NODE *node,
				       const int codeset_id,
				       const int collation_id, bool force);
static void pt_value_set_collation_info (PARSER_CONTEXT *parser,
					 PT_NODE *node,
					 PT_NODE *coll_node);
static PT_MISC_TYPE parser_attr_type;

static bool allow_attribute_ordering;

int parse_one_statement (int state);
static PT_NODE *pt_set_collation_modifier (PARSER_CONTEXT *parser,
					   PT_NODE *node, PT_NODE *coll_node);


int g_msg[1024];
int msg_ptr;

int yybuffer_pos;

#define push_msg(a) _push_msg(a, __LINE__)

void _push_msg (int code, int line);
void pop_msg (void);

char *g_query_string;
int g_query_string_len;
PT_NODE *g_last_stmt;
int g_original_buffer_len;


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
#line 573 "../src/parser/csql_grammar.y"
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
#line 1121 "../src/parser/csql_grammar.h"
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


