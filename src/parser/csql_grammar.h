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
     COALESCE = 303,
     COLLATE = 304,
     COLUMN = 305,
     COMMIT = 306,
     COMP_NULLSAFE_EQ = 307,
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
     DATABASE = 330,
     DATA_TYPE_ = 331,
     Date = 332,
     DATETIME = 333,
     DATETIMETZ = 334,
     DATETIMELTZ = 335,
     DAY_ = 336,
     DAY_MILLISECOND = 337,
     DAY_SECOND = 338,
     DAY_MINUTE = 339,
     DAY_HOUR = 340,
     DB_TIMEZONE = 341,
     DEALLOCATE = 342,
     DECLARE = 343,
     DEFAULT = 344,
     DEFERRABLE = 345,
     DEFERRED = 346,
     DELETE_ = 347,
     DEPTH = 348,
     DESC = 349,
     DESCRIBE = 350,
     DESCRIPTOR = 351,
     DIAGNOSTICS = 352,
     DIFFERENCE_ = 353,
     DISCONNECT = 354,
     DISTINCT = 355,
     DIV = 356,
     DO = 357,
     Domain = 358,
     Double = 359,
     DROP = 360,
     DUPLICATE_ = 361,
     EACH = 362,
     ELSE = 363,
     ELSEIF = 364,
     END = 365,
     ENUM = 366,
     EQUALS = 367,
     ESCAPE = 368,
     EVALUATE = 369,
     EXCEPT = 370,
     EXCEPTION = 371,
     EXEC = 372,
     EXECUTE = 373,
     EXISTS = 374,
     EXTERNAL = 375,
     EXTRACT = 376,
     False = 377,
     FETCH = 378,
     File = 379,
     FIRST = 380,
     FLOAT_ = 381,
     For = 382,
     FORCE = 383,
     FOREIGN = 384,
     FOUND = 385,
     FROM = 386,
     FULL = 387,
     FUNCTION = 388,
     GENERAL = 389,
     GET = 390,
     GLOBAL = 391,
     GO = 392,
     GOTO = 393,
     GRANT = 394,
     GROUP_ = 395,
     HAVING = 396,
     HOUR_ = 397,
     HOUR_MILLISECOND = 398,
     HOUR_SECOND = 399,
     HOUR_MINUTE = 400,
     IDENTITY = 401,
     IF = 402,
     IGNORE_ = 403,
     IMMEDIATE = 404,
     IN_ = 405,
     INDEX = 406,
     INDICATOR = 407,
     INHERIT = 408,
     INITIALLY = 409,
     INNER = 410,
     INOUT = 411,
     INPUT_ = 412,
     INSERT = 413,
     INTEGER = 414,
     INTERNAL = 415,
     INTERSECT = 416,
     INTERSECTION = 417,
     INTERVAL = 418,
     INTO = 419,
     IS = 420,
     ISOLATION = 421,
     JOIN = 422,
     KEY = 423,
     KEYLIMIT = 424,
     LANGUAGE = 425,
     LAST = 426,
     LEADING_ = 427,
     LEAVE = 428,
     LEFT = 429,
     LESS = 430,
     LEVEL = 431,
     LIKE = 432,
     LIMIT = 433,
     LIST = 434,
     LOCAL = 435,
     LOCAL_TRANSACTION_ID = 436,
     LOCALTIME = 437,
     LOCALTIMESTAMP = 438,
     LOOP = 439,
     LOWER = 440,
     MATCH = 441,
     MATCHED = 442,
     Max = 443,
     MERGE = 444,
     METHOD = 445,
     MILLISECOND_ = 446,
     Min = 447,
     MINUTE_ = 448,
     MINUTE_MILLISECOND = 449,
     MINUTE_SECOND = 450,
     MOD = 451,
     MODIFY = 452,
     MODULE = 453,
     Monetary = 454,
     MONTH_ = 455,
     MULTISET = 456,
     MULTISET_OF = 457,
     NA = 458,
     NAMES = 459,
     NATIONAL = 460,
     NATURAL = 461,
     NCHAR = 462,
     NEXT = 463,
     NO = 464,
     NOT = 465,
     Null = 466,
     NULLIF = 467,
     NUMERIC = 468,
     OBJECT = 469,
     OCTET_LENGTH = 470,
     OF = 471,
     OFF_ = 472,
     ON_ = 473,
     ONLY = 474,
     OPTIMIZATION = 475,
     OPTION = 476,
     OR = 477,
     ORDER = 478,
     OUT_ = 479,
     OUTER = 480,
     OUTPUT = 481,
     OVER = 482,
     OVERLAPS = 483,
     PARAMETERS = 484,
     PARTIAL = 485,
     PARTITION = 486,
     POSITION = 487,
     PRECISION = 488,
     PREPARE = 489,
     PRESERVE = 490,
     PRIMARY = 491,
     PRIOR = 492,
     PRIVILEGES = 493,
     PROCEDURE = 494,
     PROMOTE = 495,
     QUERY = 496,
     READ = 497,
     REBUILD = 498,
     RECURSIVE = 499,
     REF = 500,
     REFERENCES = 501,
     REFERENCING = 502,
     REGEXP = 503,
     RELATIVE_ = 504,
     RENAME = 505,
     REPLACE = 506,
     RESIGNAL = 507,
     RESTRICT = 508,
     RETURN = 509,
     RETURNS = 510,
     REVOKE = 511,
     RIGHT = 512,
     RLIKE = 513,
     ROLE = 514,
     ROLLBACK = 515,
     ROLLUP = 516,
     ROUTINE = 517,
     ROW = 518,
     ROWNUM = 519,
     ROWS = 520,
     SAVEPOINT = 521,
     SCHEMA = 522,
     SCOPE = 523,
     SCROLL = 524,
     SEARCH = 525,
     SECOND_ = 526,
     SECOND_MILLISECOND = 527,
     SECTION = 528,
     SELECT = 529,
     SENSITIVE = 530,
     SEQUENCE = 531,
     SEQUENCE_OF = 532,
     SERIALIZABLE = 533,
     SESSION = 534,
     SESSION_TIMEZONE = 535,
     SESSION_USER = 536,
     SET = 537,
     SET_OF = 538,
     SETEQ = 539,
     SETNEQ = 540,
     SHARED = 541,
     SIBLINGS = 542,
     SIGNAL = 543,
     SIMILAR = 544,
     SIZE_ = 545,
     SmallInt = 546,
     SOME = 547,
     SQL = 548,
     SQLCODE = 549,
     SQLERROR = 550,
     SQLEXCEPTION = 551,
     SQLSTATE = 552,
     SQLWARNING = 553,
     STATISTICS = 554,
     String = 555,
     SUBCLASS = 556,
     SUBSET = 557,
     SUBSETEQ = 558,
     SUBSTRING_ = 559,
     SUM = 560,
     SUPERCLASS = 561,
     SUPERSET = 562,
     SUPERSETEQ = 563,
     SYS_CONNECT_BY_PATH = 564,
     SYS_DATE = 565,
     SYS_DATETIME = 566,
     SYS_TIME_ = 567,
     SYS_TIMESTAMP = 568,
     SYSTEM_USER = 569,
     TABLE = 570,
     TEMPORARY = 571,
     THEN = 572,
     Time = 573,
     TIMESTAMP = 574,
     TIMESTAMPTZ = 575,
     TIMESTAMPLTZ = 576,
     TIMEZONE = 577,
     TIMEZONE_HOUR = 578,
     TIMEZONE_MINUTE = 579,
     TO = 580,
     TRAILING_ = 581,
     TRANSACTION = 582,
     TRANSLATE = 583,
     TRANSLATION = 584,
     TRIGGER = 585,
     TRIM = 586,
     True = 587,
     TRUNCATE = 588,
     UNDER = 589,
     Union = 590,
     UNIQUE = 591,
     UNKNOWN = 592,
     UNTERMINATED_STRING = 593,
     UNTERMINATED_IDENTIFIER = 594,
     UPDATE = 595,
     UPPER = 596,
     USAGE = 597,
     USE = 598,
     USER = 599,
     USING = 600,
     Utime = 601,
     VACUUM = 602,
     VALUE = 603,
     VALUES = 604,
     VAR_ASSIGN = 605,
     VARCHAR = 606,
     VARIABLE_ = 607,
     VARYING = 608,
     VCLASS = 609,
     VIEW = 610,
     WHEN = 611,
     WHENEVER = 612,
     WHERE = 613,
     WHILE = 614,
     WITH = 615,
     WITHOUT = 616,
     WORK = 617,
     WRITE = 618,
     XOR = 619,
     YEAR_ = 620,
     YEAR_MONTH = 621,
     ZONE = 622,
     JSON = 623,
     YEN_SIGN = 624,
     DOLLAR_SIGN = 625,
     WON_SIGN = 626,
     TURKISH_LIRA_SIGN = 627,
     BRITISH_POUND_SIGN = 628,
     CAMBODIAN_RIEL_SIGN = 629,
     CHINESE_RENMINBI_SIGN = 630,
     INDIAN_RUPEE_SIGN = 631,
     RUSSIAN_RUBLE_SIGN = 632,
     AUSTRALIAN_DOLLAR_SIGN = 633,
     CANADIAN_DOLLAR_SIGN = 634,
     BRASILIAN_REAL_SIGN = 635,
     ROMANIAN_LEU_SIGN = 636,
     EURO_SIGN = 637,
     SWISS_FRANC_SIGN = 638,
     DANISH_KRONE_SIGN = 639,
     NORWEGIAN_KRONE_SIGN = 640,
     BULGARIAN_LEV_SIGN = 641,
     VIETNAMESE_DONG_SIGN = 642,
     CZECH_KORUNA_SIGN = 643,
     POLISH_ZLOTY_SIGN = 644,
     SWEDISH_KRONA_SIGN = 645,
     CROATIAN_KUNA_SIGN = 646,
     SERBIAN_DINAR_SIGN = 647,
     DOT = 648,
     RIGHT_ARROW = 649,
     STRCAT = 650,
     COMP_NOT_EQ = 651,
     COMP_GE = 652,
     COMP_LE = 653,
     PARAM_HEADER = 654,
     ACCESS = 655,
     ACTIVE = 656,
     ADDDATE = 657,
     ANALYZE = 658,
     ARCHIVE = 659,
     AUTO_INCREMENT = 660,
     BIT_AND = 661,
     BIT_OR = 662,
     BIT_XOR = 663,
     CACHE = 664,
     CAPACITY = 665,
     CHARACTER_SET_ = 666,
     CHARSET = 667,
     CHR = 668,
     CLOB_TO_CHAR = 669,
     CLOSE = 670,
     COLLATION = 671,
     COLUMNS = 672,
     COMMENT = 673,
     COMMITTED = 674,
     COST = 675,
     CRITICAL = 676,
     CUME_DIST = 677,
     DATE_ADD = 678,
     DATE_SUB = 679,
     DECREMENT = 680,
     DENSE_RANK = 681,
     ELT = 682,
     EXPLAIN = 683,
     FIRST_VALUE = 684,
     FULLSCAN = 685,
     GE_INF_ = 686,
     GE_LE_ = 687,
     GE_LT_ = 688,
     GRANTS = 689,
     GROUP_CONCAT = 690,
     GROUPS = 691,
     GT_INF_ = 692,
     GT_LE_ = 693,
     GT_LT_ = 694,
     HASH = 695,
     HEADER = 696,
     HEAP = 697,
     IFNULL = 698,
     INACTIVE = 699,
     INCREMENT = 700,
     INDEXES = 701,
     INDEX_PREFIX = 702,
     INF_LE_ = 703,
     INF_LT_ = 704,
     INFINITE_ = 705,
     INSTANCES = 706,
     INVALIDATE = 707,
     ISNULL = 708,
     KEYS = 709,
     KILL = 710,
     JAVA = 711,
     JOB = 712,
     LAG = 713,
     LAST_VALUE = 714,
     LCASE = 715,
     LEAD = 716,
     LOCK_ = 717,
     LOG = 718,
     MAXIMUM = 719,
     MAXVALUE = 720,
     MEDIAN = 721,
     MEMBERS = 722,
     MINVALUE = 723,
     NAME = 724,
     NOCYCLE = 725,
     NOCACHE = 726,
     NOMAXVALUE = 727,
     NOMINVALUE = 728,
     NONE = 729,
     NTH_VALUE = 730,
     NTILE = 731,
     NULLS = 732,
     OFFSET = 733,
     OPEN = 734,
     OWNER = 735,
     PAGE = 736,
     PARTITIONING = 737,
     PARTITIONS = 738,
     PASSWORD = 739,
     PERCENT_RANK = 740,
     PERCENTILE_CONT = 741,
     PERCENTILE_DISC = 742,
     PRINT = 743,
     PRIORITY = 744,
     QUARTER = 745,
     QUEUES = 746,
     RANGE_ = 747,
     RANK = 748,
     REJECT_ = 749,
     REMOVE = 750,
     REORGANIZE = 751,
     REPEATABLE = 752,
     RESPECT = 753,
     RETAIN = 754,
     REUSE_OID = 755,
     REVERSE = 756,
     DISK_SIZE = 757,
     ROW_NUMBER = 758,
     SECTIONS = 759,
     SEPARATOR = 760,
     SERIAL = 761,
     SHOW = 762,
     SLEEP = 763,
     SLOTS = 764,
     SLOTTED = 765,
     STABILITY = 766,
     START_ = 767,
     STATEMENT = 768,
     STATUS = 769,
     STDDEV = 770,
     STDDEV_POP = 771,
     STDDEV_SAMP = 772,
     STR_TO_DATE = 773,
     SUBDATE = 774,
     SYSTEM = 775,
     TABLES = 776,
     TEXT = 777,
     THAN = 778,
     THREADS = 779,
     TIMEOUT = 780,
     TRACE = 781,
     TRAN = 782,
     TRIGGERS = 783,
     UCASE = 784,
     UNCOMMITTED = 785,
     VAR_POP = 786,
     VAR_SAMP = 787,
     VARIANCE = 788,
     VOLUME = 789,
     WEEK = 790,
     WITHIN = 791,
     WORKSPACE = 792,
     TIMEZONES = 793,
     IdName = 794,
     BracketDelimitedIdName = 795,
     BacktickDelimitedIdName = 796,
     DelimitedIdName = 797,
     UNSIGNED_INTEGER = 798,
     UNSIGNED_REAL = 799,
     CHAR_STRING = 800,
     NCHAR_STRING = 801,
     BIT_STRING = 802,
     HEX_STRING = 803,
     CPP_STYLE_HINT = 804,
     C_STYLE_HINT = 805,
     SQL_STYLE_HINT = 806,
     BINARY_STRING = 807,
     EUCKR_STRING = 808,
     ISO_STRING = 809,
     UTF8_STRING = 810
   };
#endif


/* Copy the first part of user declarations.  */

/*%CODE_REQUIRES_START%*/
#include "parser.h"

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

void csql_yyerror_explicit (int line, int column);
void csql_yyerror (const char *s);

extern int g_msg[1024];
extern int msg_ptr;
extern int yybuffer_pos;
/*%CODE_END%*/

#define YYMAXDEPTH	1000000

/* #define PARSER_DEBUG */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <errno.h>

#include "chartype.h"
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
#define snprintf _sprintf_p
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
#define COLUMN_CONSTRAINT_COMMENT       (0x80)

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
  {"curtime", PT_CURRENT_TIME},
  {"curdate", PT_CURRENT_DATE},
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
  {"new_time", PT_NEW_TIME},
  {"format", PT_FORMAT},
  {"now", PT_CURRENT_DATETIME},
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
  {"disk_size", PT_DISK_SIZE},
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
/*
 * temporarily block aes_encrypt and aes_decrypt functions until binary string charset is available.
 *
 *  {"aes_encrypt", PT_AES_ENCRYPT},
 *  {"aes_decrypt", PT_AES_DECRYPT},
 */	
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
  {"tz_offset", PT_TZ_OFFSET},
  {"unix_timestamp", PT_UNIX_TIMESTAMP},
  {"typeof", PT_TYPEOF},
  {"from_unixtime", PT_FROM_UNIXTIME},
  {"from_tz", PT_FROM_TZ},
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
  {"trace_stats", PT_TRACE_STATS},
  {"str_to_date", PT_STR_TO_DATE},
  {"to_base64", PT_TO_BASE64},
  {"from_base64", PT_FROM_BASE64},
  {"sys_guid", PT_SYS_GUID},
  {"sleep", PT_SLEEP},
  {"to_datetime_tz", PT_TO_DATETIME_TZ},
  {"to_timestamp_tz", PT_TO_TIMESTAMP_TZ},
  {"utc_timestamp", PT_UTC_TIMESTAMP},
  {"crc32", PT_CRC32},
  {"schema_def", PT_SCHEMA_DEF},
  {"conv_tz", PT_CONV_TZ}
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

/* for opt_over_analytic_partition_by */
static bool is_analytic_function = false;

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
static void pt_value_set_monetary (PARSER_CONTEXT *parser, PT_NODE *node,
                   const char *str, const char *txt, DB_CURRENCY type);
static PT_MISC_TYPE parser_attr_type;

static bool allow_attribute_ordering;

int parse_one_statement (int state);
static PT_NODE *pt_set_collation_modifier (PARSER_CONTEXT *parser,
					   PT_NODE *node, PT_NODE *coll_node);


#define push_msg(a) _push_msg(a, __LINE__)

void _push_msg (int code, int line);
void pop_msg (void);

char *g_query_string;
int g_query_string_len;
int g_original_buffer_len;


/*
 * The behavior of location propagation when a rule is matched must
 * take into account the context information. The left-side symbol in a rule
 * will have the same context information as the last symbol from its 
 * right side
 */
#define YYLLOC_DEFAULT(Current, Rhs, N)				        \
    do									\
      if (N)								\
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
    while (0)

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


