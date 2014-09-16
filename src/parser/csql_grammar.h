
/* A Bison parser, made by GNU Bison 2.4.1.  */

/* Skeleton interface for Bison GLR parsers in C
   
      Copyright (C) 2002, 2003, 2004, 2005, 2006 Free Software Foundation, Inc.
   
   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   
   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

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

/* "%code requires" blocks.  */

/* Line 2638 of glr.c  */
#line 26 "csql_grammar.yy"

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



/* Line 2638 of glr.c  */
#line 103 "../../src/parser/csql_grammar.h"

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
     NOT = 463,
     Null = 464,
     NULLIF = 465,
     NUMERIC = 466,
     OBJECT = 467,
     OCTET_LENGTH = 468,
     OF = 469,
     OFF_ = 470,
     ON_ = 471,
     ONLY = 472,
     OPEN = 473,
     OPTIMIZATION = 474,
     OPTION = 475,
     OR = 476,
     ORDER = 477,
     OUT_ = 478,
     OUTER = 479,
     OUTPUT = 480,
     OVER = 481,
     OVERLAPS = 482,
     PARAMETERS = 483,
     PARTIAL = 484,
     PARTITION = 485,
     POSITION = 486,
     PRECISION = 487,
     PREPARE = 488,
     PRESERVE = 489,
     PRIMARY = 490,
     PRIOR = 491,
     PRIVILEGES = 492,
     PROCEDURE = 493,
     PROMOTE = 494,
     QUERY = 495,
     READ = 496,
     REBUILD = 497,
     RECURSIVE = 498,
     REF = 499,
     REFERENCES = 500,
     REFERENCING = 501,
     REGEXP = 502,
     RELATIVE_ = 503,
     RENAME = 504,
     REPLACE = 505,
     RESIGNAL = 506,
     RESTRICT = 507,
     RETURN = 508,
     RETURNS = 509,
     REVOKE = 510,
     RIGHT = 511,
     RLIKE = 512,
     ROLE = 513,
     ROLLBACK = 514,
     ROLLUP = 515,
     ROUTINE = 516,
     ROW = 517,
     ROWNUM = 518,
     ROWS = 519,
     SAVEPOINT = 520,
     SCHEMA = 521,
     SCOPE = 522,
     SCROLL = 523,
     SEARCH = 524,
     SECOND_ = 525,
     SECOND_MILLISECOND = 526,
     SECTION = 527,
     SELECT = 528,
     SENSITIVE = 529,
     SEQUENCE = 530,
     SEQUENCE_OF = 531,
     SERIALIZABLE = 532,
     SESSION = 533,
     SESSION_USER = 534,
     SET = 535,
     SET_OF = 536,
     SETEQ = 537,
     SETNEQ = 538,
     SHARED = 539,
     SIBLINGS = 540,
     SIGNAL = 541,
     SIMILAR = 542,
     SIZE_ = 543,
     SmallInt = 544,
     SOME = 545,
     SQL = 546,
     SQLCODE = 547,
     SQLERROR = 548,
     SQLEXCEPTION = 549,
     SQLSTATE = 550,
     SQLWARNING = 551,
     STATISTICS = 552,
     String = 553,
     SUBCLASS = 554,
     SUBSET = 555,
     SUBSETEQ = 556,
     SUBSTRING_ = 557,
     SUM = 558,
     SUPERCLASS = 559,
     SUPERSET = 560,
     SUPERSETEQ = 561,
     SYS_CONNECT_BY_PATH = 562,
     SYS_DATE = 563,
     SYS_DATETIME = 564,
     SYS_TIME_ = 565,
     SYS_TIMESTAMP = 566,
     SYSTEM_USER = 567,
     TABLE = 568,
     TEMPORARY = 569,
     THEN = 570,
     Time = 571,
     TIMESTAMP = 572,
     TIMEZONE_HOUR = 573,
     TIMEZONE_MINUTE = 574,
     TO = 575,
     TRAILING_ = 576,
     TRANSACTION = 577,
     TRANSLATE = 578,
     TRANSLATION = 579,
     TRIGGER = 580,
     TRIM = 581,
     True = 582,
     TRUNCATE = 583,
     UNDER = 584,
     Union = 585,
     UNIQUE = 586,
     UNKNOWN = 587,
     UNTERMINATED_STRING = 588,
     UNTERMINATED_IDENTIFIER = 589,
     UPDATE = 590,
     UPPER = 591,
     USAGE = 592,
     USE = 593,
     USER = 594,
     USING = 595,
     Utime = 596,
     VACUUM = 597,
     VALUE = 598,
     VALUES = 599,
     VAR_ASSIGN = 600,
     VARCHAR = 601,
     VARIABLE_ = 602,
     VARYING = 603,
     VCLASS = 604,
     VIEW = 605,
     WHEN = 606,
     WHENEVER = 607,
     WHERE = 608,
     WHILE = 609,
     WITH = 610,
     WITHIN = 611,
     WITHOUT = 612,
     WORK = 613,
     WRITE = 614,
     XOR = 615,
     YEAR_ = 616,
     YEAR_MONTH = 617,
     ZONE = 618,
     YEN_SIGN = 619,
     DOLLAR_SIGN = 620,
     WON_SIGN = 621,
     TURKISH_LIRA_SIGN = 622,
     BRITISH_POUND_SIGN = 623,
     CAMBODIAN_RIEL_SIGN = 624,
     CHINESE_RENMINBI_SIGN = 625,
     INDIAN_RUPEE_SIGN = 626,
     RUSSIAN_RUBLE_SIGN = 627,
     AUSTRALIAN_DOLLAR_SIGN = 628,
     CANADIAN_DOLLAR_SIGN = 629,
     BRASILIAN_REAL_SIGN = 630,
     ROMANIAN_LEU_SIGN = 631,
     EURO_SIGN = 632,
     SWISS_FRANC_SIGN = 633,
     DANISH_KRONE_SIGN = 634,
     NORWEGIAN_KRONE_SIGN = 635,
     BULGARIAN_LEV_SIGN = 636,
     VIETNAMESE_DONG_SIGN = 637,
     CZECH_KORUNA_SIGN = 638,
     POLISH_ZLOTY_SIGN = 639,
     SWEDISH_KRONA_SIGN = 640,
     CROATIAN_KUNA_SIGN = 641,
     SERBIAN_DINAR_SIGN = 642,
     DOT = 643,
     RIGHT_ARROW = 644,
     STRCAT = 645,
     COMP_NOT_EQ = 646,
     COMP_GE = 647,
     COMP_LE = 648,
     PARAM_HEADER = 649,
     ACCESS = 650,
     ACTIVE = 651,
     ADDDATE = 652,
     ANALYZE = 653,
     ARCHIVE = 654,
     AUTO_INCREMENT = 655,
     BIT_AND = 656,
     BIT_OR = 657,
     BIT_XOR = 658,
     CACHE = 659,
     CAPACITY = 660,
     CHARACTER_SET_ = 661,
     CHARSET = 662,
     CHR = 663,
     CLOB_TO_CHAR = 664,
     COLLATION = 665,
     COLUMNS = 666,
     COMMITTED = 667,
     COST = 668,
     CRITICAL = 669,
     CUME_DIST = 670,
     DATE_ADD = 671,
     DATE_SUB = 672,
     DECREMENT = 673,
     DENSE_RANK = 674,
     ELT = 675,
     EXPLAIN = 676,
     FIRST_VALUE = 677,
     FULLSCAN = 678,
     GE_INF_ = 679,
     GE_LE_ = 680,
     GE_LT_ = 681,
     GRANTS = 682,
     GROUP_CONCAT = 683,
     GROUPS = 684,
     GT_INF_ = 685,
     GT_LE_ = 686,
     GT_LT_ = 687,
     HASH = 688,
     HEADER = 689,
     HEAP = 690,
     IFNULL = 691,
     INACTIVE = 692,
     INCREMENT = 693,
     INDEXES = 694,
     INDEX_PREFIX = 695,
     INF_LE_ = 696,
     INF_LT_ = 697,
     INFINITE_ = 698,
     INSTANCES = 699,
     INVALIDATE = 700,
     ISNULL = 701,
     KEYS = 702,
     KILL = 703,
     JAVA = 704,
     JOB = 705,
     JSON = 706,
     LAG = 707,
     LAST_VALUE = 708,
     LCASE = 709,
     LEAD = 710,
     LOCK_ = 711,
     LOG = 712,
     MAXIMUM = 713,
     MAXVALUE = 714,
     MEDIAN = 715,
     MEMBERS = 716,
     MINVALUE = 717,
     NAME = 718,
     NOCYCLE = 719,
     NOCACHE = 720,
     NOMAXVALUE = 721,
     NOMINVALUE = 722,
     NONE = 723,
     NTH_VALUE = 724,
     NTILE = 725,
     NULLS = 726,
     OFFSET = 727,
     OWNER = 728,
     PAGE = 729,
     PARTITIONING = 730,
     PARTITIONS = 731,
     PASSWORD = 732,
     PERCENT_RANK = 733,
     PRINT = 734,
     PRIORITY = 735,
     QUARTER = 736,
     QUEUES = 737,
     RANGE_ = 738,
     RANK = 739,
     REJECT_ = 740,
     REMOVE = 741,
     REORGANIZE = 742,
     REPEATABLE = 743,
     RESPECT = 744,
     RETAIN = 745,
     REUSE_OID = 746,
     REVERSE = 747,
     ROW_NUMBER = 748,
     SECTIONS = 749,
     SEPARATOR = 750,
     SERIAL = 751,
     SHOW = 752,
     SLEEP = 753,
     SLOTS = 754,
     SLOTTED = 755,
     STABILITY = 756,
     START_ = 757,
     STATEMENT = 758,
     STATUS = 759,
     STDDEV = 760,
     STDDEV_POP = 761,
     STDDEV_SAMP = 762,
     STR_TO_DATE = 763,
     SUBDATE = 764,
     SYSTEM = 765,
     TABLES = 766,
     TEXT = 767,
     THAN = 768,
     TIMEOUT = 769,
     TRACE = 770,
     TRIGGERS = 771,
     UCASE = 772,
     UNCOMMITTED = 773,
     VAR_POP = 774,
     VAR_SAMP = 775,
     VARIANCE = 776,
     VOLUME = 777,
     WEEK = 778,
     WORKSPACE = 779,
     IdName = 780,
     BracketDelimitedIdName = 781,
     BacktickDelimitedIdName = 782,
     DelimitedIdName = 783,
     UNSIGNED_INTEGER = 784,
     UNSIGNED_REAL = 785,
     CHAR_STRING = 786,
     NCHAR_STRING = 787,
     BIT_STRING = 788,
     HEX_STRING = 789,
     CPP_STYLE_HINT = 790,
     C_STYLE_HINT = 791,
     SQL_STYLE_HINT = 792,
     EUCKR_STRING = 793,
     ISO_STRING = 794,
     UTF8_STRING = 795
   };
#endif


#ifndef YYSTYPE
typedef union YYSTYPE
{

/* Line 2638 of glr.c  */
#line 588 "csql_grammar.yy"

  int number;
  bool boolean;
  PT_NODE *node;
  char *cptr;
  container_2 c2;
  container_3 c3;
  container_4 c4;
  container_10 c10;



/* Line 2638 of glr.c  */
#line 672 "../../src/parser/csql_grammar.h"
} YYSTYPE;
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


