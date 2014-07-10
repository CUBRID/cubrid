
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
     VALUE = 597,
     VALUES = 598,
     VAR_ASSIGN = 599,
     VARCHAR = 600,
     VARIABLE_ = 601,
     VARYING = 602,
     VCLASS = 603,
     VIEW = 604,
     WHEN = 605,
     WHENEVER = 606,
     WHERE = 607,
     WHILE = 608,
     WITH = 609,
     WITHIN = 610,
     WITHOUT = 611,
     WORK = 612,
     WRITE = 613,
     XOR = 614,
     YEAR_ = 615,
     YEAR_MONTH = 616,
     ZONE = 617,
     YEN_SIGN = 618,
     DOLLAR_SIGN = 619,
     WON_SIGN = 620,
     TURKISH_LIRA_SIGN = 621,
     BRITISH_POUND_SIGN = 622,
     CAMBODIAN_RIEL_SIGN = 623,
     CHINESE_RENMINBI_SIGN = 624,
     INDIAN_RUPEE_SIGN = 625,
     RUSSIAN_RUBLE_SIGN = 626,
     AUSTRALIAN_DOLLAR_SIGN = 627,
     CANADIAN_DOLLAR_SIGN = 628,
     BRASILIAN_REAL_SIGN = 629,
     ROMANIAN_LEU_SIGN = 630,
     EURO_SIGN = 631,
     SWISS_FRANC_SIGN = 632,
     DANISH_KRONE_SIGN = 633,
     NORWEGIAN_KRONE_SIGN = 634,
     BULGARIAN_LEV_SIGN = 635,
     VIETNAMESE_DONG_SIGN = 636,
     CZECH_KORUNA_SIGN = 637,
     POLISH_ZLOTY_SIGN = 638,
     SWEDISH_KRONA_SIGN = 639,
     CROATIAN_KUNA_SIGN = 640,
     SERBIAN_DINAR_SIGN = 641,
     DOT = 642,
     RIGHT_ARROW = 643,
     STRCAT = 644,
     COMP_NOT_EQ = 645,
     COMP_GE = 646,
     COMP_LE = 647,
     PARAM_HEADER = 648,
     ACCESS = 649,
     ACTIVE = 650,
     ADDDATE = 651,
     ANALYZE = 652,
     ARCHIVE = 653,
     AUTO_INCREMENT = 654,
     BIT_AND = 655,
     BIT_OR = 656,
     BIT_XOR = 657,
     CACHE = 658,
     CAPACITY = 659,
     CHARACTER_SET_ = 660,
     CHARSET = 661,
     CHR = 662,
     CLOB_TO_CHAR = 663,
     COLLATION = 664,
     COLUMNS = 665,
     COMMITTED = 666,
     COST = 667,
     CRITICAL = 668,
     CUME_DIST = 669,
     DATE_ADD = 670,
     DATE_SUB = 671,
     DECREMENT = 672,
     DENSE_RANK = 673,
     ELT = 674,
     EXPLAIN = 675,
     FIRST_VALUE = 676,
     FULLSCAN = 677,
     GE_INF_ = 678,
     GE_LE_ = 679,
     GE_LT_ = 680,
     GRANTS = 681,
     GROUP_CONCAT = 682,
     GROUPS = 683,
     GT_INF_ = 684,
     GT_LE_ = 685,
     GT_LT_ = 686,
     HASH = 687,
     HEADER = 688,
     HEAP = 689,
     IFNULL = 690,
     INACTIVE = 691,
     INCREMENT = 692,
     INDEXES = 693,
     INDEX_PREFIX = 694,
     INF_LE_ = 695,
     INF_LT_ = 696,
     INFINITE_ = 697,
     INSTANCES = 698,
     INVALIDATE = 699,
     ISNULL = 700,
     KEYS = 701,
     KILL = 702,
     JAVA = 703,
     JSON = 704,
     LAG = 705,
     LAST_VALUE = 706,
     LCASE = 707,
     LEAD = 708,
     LOCK_ = 709,
     LOG = 710,
     MAXIMUM = 711,
     MAXVALUE = 712,
     MEDIAN = 713,
     MEMBERS = 714,
     MINVALUE = 715,
     NAME = 716,
     NOCYCLE = 717,
     NOCACHE = 718,
     NOMAXVALUE = 719,
     NOMINVALUE = 720,
     NONE = 721,
     NTH_VALUE = 722,
     NTILE = 723,
     NULLS = 724,
     OFFSET = 725,
     OWNER = 726,
     PAGE = 727,
     PARTITIONING = 728,
     PARTITIONS = 729,
     PASSWORD = 730,
     PERCENT_RANK = 731,
     PRINT = 732,
     PRIORITY = 733,
     QUARTER = 734,
     RANGE_ = 735,
     RANK = 736,
     REJECT_ = 737,
     REMOVE = 738,
     REORGANIZE = 739,
     REPEATABLE = 740,
     RESPECT = 741,
     RETAIN = 742,
     REUSE_OID = 743,
     REVERSE = 744,
     ROW_NUMBER = 745,
     SECTIONS = 746,
     SEPARATOR = 747,
     SERIAL = 748,
     SHOW = 749,
     SLOTS = 750,
     SLOTTED = 751,
     STABILITY = 752,
     START_ = 753,
     STATEMENT = 754,
     STATUS = 755,
     STDDEV = 756,
     STDDEV_POP = 757,
     STDDEV_SAMP = 758,
     STR_TO_DATE = 759,
     SUBDATE = 760,
     SYSTEM = 761,
     TABLES = 762,
     TEXT = 763,
     THAN = 764,
     TIMEOUT = 765,
     TRACE = 766,
     TRIGGERS = 767,
     UCASE = 768,
     UNCOMMITTED = 769,
     VAR_POP = 770,
     VAR_SAMP = 771,
     VARIANCE = 772,
     VOLUME = 773,
     WEEK = 774,
     WORKSPACE = 775,
     IdName = 776,
     BracketDelimitedIdName = 777,
     BacktickDelimitedIdName = 778,
     DelimitedIdName = 779,
     UNSIGNED_INTEGER = 780,
     UNSIGNED_REAL = 781,
     CHAR_STRING = 782,
     NCHAR_STRING = 783,
     BIT_STRING = 784,
     HEX_STRING = 785,
     CPP_STYLE_HINT = 786,
     C_STYLE_HINT = 787,
     SQL_STYLE_HINT = 788,
     EUCKR_STRING = 789,
     ISO_STRING = 790,
     UTF8_STRING = 791
   };
#endif


#ifndef YYSTYPE
typedef union YYSTYPE
{

/* Line 2638 of glr.c  */
#line 587 "csql_grammar.yy"

  int number;
  bool boolean;
  PT_NODE *node;
  char *cptr;
  container_2 c2;
  container_3 c3;
  container_4 c4;
  container_10 c10;



/* Line 2638 of glr.c  */
#line 668 "../../src/parser/csql_grammar.h"
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


