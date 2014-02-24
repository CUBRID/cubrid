/* A Bison parser, made by GNU Bison 2.3.  */

/* Skeleton implementation for Bison's Yacc-like parsers in C

   Copyright (C) 1984, 1989, 1990, 2000, 2001, 2002, 2003, 2004, 2005, 2006
   Free Software Foundation, Inc.

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

/* C LALR(1) parser skeleton written by Richard Stallman, by
   simplifying the original so-called "semantic" parser.  */

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

/* Identify Bison output.  */
#define YYBISON 1

/* Bison version.  */
#define YYBISON_VERSION "2.3"

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 0

/* Using locations.  */
#define YYLSP_NEEDED 0

/* Substitute the variable and function names.  */
#define yyparse loader_yyparse
#define yylex   loader_yylex
#define yyerror loader_yyerror
#define yylval  loader_yylval
#define yychar  loader_yychar
#define yydebug loader_yydebug
#define yynerrs loader_yynerrs


/* Tokens.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
   /* Put the tokens into the symbol table, so that GDB and other debuggers
      know about them.  */
   enum yytokentype {
     NL = 258,
     NULL_ = 259,
     CLASS = 260,
     SHARED = 261,
     DEFAULT = 262,
     DATE_ = 263,
     TIME = 264,
     UTIME = 265,
     TIMESTAMP = 266,
     DATETIME = 267,
     CMD_ID = 268,
     CMD_CLASS = 269,
     CMD_CONSTRUCTOR = 270,
     REF_ELO_INT = 271,
     REF_ELO_EXT = 272,
     REF_USER = 273,
     REF_CLASS = 274,
     OBJECT_REFERENCE = 275,
     OID_DELIMETER = 276,
     SET_START_BRACE = 277,
     SET_END_BRACE = 278,
     START_PAREN = 279,
     END_PAREN = 280,
     REAL_LIT = 281,
     INT_LIT = 282,
     OID_ = 283,
     TIME_LIT4 = 284,
     TIME_LIT42 = 285,
     TIME_LIT3 = 286,
     TIME_LIT31 = 287,
     TIME_LIT2 = 288,
     TIME_LIT1 = 289,
     DATE_LIT2 = 290,
     YEN_SYMBOL = 291,
     WON_SYMBOL = 292,
     BACKSLASH = 293,
     DOLLAR_SYMBOL = 294,
     TURKISH_LIRA_CURRENCY = 295,
     BRITISH_POUND_SYMBOL = 296,
     CAMBODIAN_RIEL_SYMBOL = 297,
     CHINESE_RENMINBI_SYMBOL = 298,
     INDIAN_RUPEE_SYMBOL = 299,
     RUSSIAN_RUBLE_SYMBOL = 300,
     AUSTRALIAN_DOLLAR_SYMBOL = 301,
     CANADIAN_DOLLAR_SYMBOL = 302,
     BRASILIAN_REAL_SYMBOL = 303,
     ROMANIAN_LEU_SYMBOL = 304,
     EURO_SYMBOL = 305,
     SWISS_FRANC_SYMBOL = 306,
     DANISH_KRONE_SYMBOL = 307,
     NORWEGIAN_KRONE_SYMBOL = 308,
     BULGARIAN_LEV_SYMBOL = 309,
     VIETNAMESE_DONG_SYMBOL = 310,
     CZECH_KORUNA_SYMBOL = 311,
     POLISH_ZLOTY_SYMBOL = 312,
     SWEDISH_KRONA_SYMBOL = 313,
     CROATIAN_KUNA_SYMBOL = 314,
     SERBIAN_DINAR_SYMBOL = 315,
     IDENTIFIER = 316,
     Quote = 317,
     DQuote = 318,
     NQuote = 319,
     BQuote = 320,
     XQuote = 321,
     SQS_String_Body = 322,
     DQS_String_Body = 323,
     COMMA = 324
   };
#endif
/* Tokens.  */
#define NL 258
#define NULL_ 259
#define CLASS 260
#define SHARED 261
#define DEFAULT 262
#define DATE_ 263
#define TIME 264
#define UTIME 265
#define TIMESTAMP 266
#define DATETIME 267
#define CMD_ID 268
#define CMD_CLASS 269
#define CMD_CONSTRUCTOR 270
#define REF_ELO_INT 271
#define REF_ELO_EXT 272
#define REF_USER 273
#define REF_CLASS 274
#define OBJECT_REFERENCE 275
#define OID_DELIMETER 276
#define SET_START_BRACE 277
#define SET_END_BRACE 278
#define START_PAREN 279
#define END_PAREN 280
#define REAL_LIT 281
#define INT_LIT 282
#define OID_ 283
#define TIME_LIT4 284
#define TIME_LIT42 285
#define TIME_LIT3 286
#define TIME_LIT31 287
#define TIME_LIT2 288
#define TIME_LIT1 289
#define DATE_LIT2 290
#define YEN_SYMBOL 291
#define WON_SYMBOL 292
#define BACKSLASH 293
#define DOLLAR_SYMBOL 294
#define TURKISH_LIRA_CURRENCY 295
#define BRITISH_POUND_SYMBOL 296
#define CAMBODIAN_RIEL_SYMBOL 297
#define CHINESE_RENMINBI_SYMBOL 298
#define INDIAN_RUPEE_SYMBOL 299
#define RUSSIAN_RUBLE_SYMBOL 300
#define AUSTRALIAN_DOLLAR_SYMBOL 301
#define CANADIAN_DOLLAR_SYMBOL 302
#define BRASILIAN_REAL_SYMBOL 303
#define ROMANIAN_LEU_SYMBOL 304
#define EURO_SYMBOL 305
#define SWISS_FRANC_SYMBOL 306
#define DANISH_KRONE_SYMBOL 307
#define NORWEGIAN_KRONE_SYMBOL 308
#define BULGARIAN_LEV_SYMBOL 309
#define VIETNAMESE_DONG_SYMBOL 310
#define CZECH_KORUNA_SYMBOL 311
#define POLISH_ZLOTY_SYMBOL 312
#define SWEDISH_KRONA_SYMBOL 313
#define CROATIAN_KUNA_SYMBOL 314
#define SERBIAN_DINAR_SYMBOL 315
#define IDENTIFIER 316
#define Quote 317
#define DQuote 318
#define NQuote 319
#define BQuote 320
#define XQuote 321
#define SQS_String_Body 322
#define DQS_String_Body 323
#define COMMA 324




/* Copy the first part of user declarations.  */
#line 24 "../../src/executables/loader_grammar.y"

#ident "$Id$"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "dbi.h"
#include "utility.h"
#include "dbtype.h"
#include "language_support.h"
#include "message_catalog.h"
#include "memory_alloc.h"
#include "error_manager.h"
#include "loader.h"

/*#define PARSER_DEBUG*/
#ifdef PARSER_DEBUG
#define DBG_PRINT(s) printf("rule: %s\n", (s));
#else
#define DBG_PRINT(s)
#endif

#define FREE_STRING(s) \
do { \
  if ((s)->need_free_val) free_and_init ((s)->val); \
  if ((s)->need_free_self) free_and_init ((s)); \
} while (0)

#define CONSTANT_POOL_SIZE (1024)

extern bool loader_In_instance_line;
extern FILE *loader_yyin;

extern int loader_yylex(void);
extern void loader_yyerror(char* s);
extern void loader_reset_string_pool (void);
extern void loader_initialize_lexer (void);
extern void do_loader_parse(FILE *fp);

static LDR_CONSTANT constant_Pool[CONSTANT_POOL_SIZE];
static int constant_Pool_idx = 0;

static LDR_STRING *loader_append_string_list(LDR_STRING *head, LDR_STRING *str);
static LDR_CLASS_COMMAND_SPEC *loader_make_class_command_spec(int qualifier, LDR_STRING *attr_list, LDR_CONSTRUCTOR_SPEC *ctor_spec);
static LDR_CONSTANT* loader_make_constant(int type, void *val);
static LDR_MONETARY_VALUE* loader_make_monetary_value (int currency_type, LDR_STRING * amount);
static LDR_CONSTANT *loader_append_constant_list(LDR_CONSTANT *head, LDR_CONSTANT *tail);

int loader_yyline = 1;


/* Enabling traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif

/* Enabling verbose error messages.  */
#ifdef YYERROR_VERBOSE
# undef YYERROR_VERBOSE
# define YYERROR_VERBOSE 1
#else
# define YYERROR_VERBOSE 1
#endif

/* Enabling the token table.  */
#ifndef YYTOKEN_TABLE
# define YYTOKEN_TABLE 0
#endif

#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
typedef union YYSTYPE
#line 79 "../../src/executables/loader_grammar.y"
{
	int 	intval;
	LDR_STRING	*string;
	LDR_CLASS_COMMAND_SPEC *cmd_spec;
	LDR_CONSTRUCTOR_SPEC *ctor_spec;
	LDR_CONSTANT *constant;
	LDR_OBJECT_REF *obj_ref;
}
/* Line 187 of yacc.c.  */
#line 304 "../../src/executables/loader_grammar.c"
	YYSTYPE;
# define yystype YYSTYPE /* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
# define YYSTYPE_IS_TRIVIAL 1
#endif



/* Copy the second part of user declarations.  */


/* Line 216 of yacc.c.  */
#line 317 "../../src/executables/loader_grammar.c"

#ifdef short
# undef short
#endif

#ifdef YYTYPE_UINT8
typedef YYTYPE_UINT8 yytype_uint8;
#else
typedef unsigned char yytype_uint8;
#endif

#ifdef YYTYPE_INT8
typedef YYTYPE_INT8 yytype_int8;
#elif (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
typedef signed char yytype_int8;
#else
typedef short int yytype_int8;
#endif

#ifdef YYTYPE_UINT16
typedef YYTYPE_UINT16 yytype_uint16;
#else
typedef unsigned short int yytype_uint16;
#endif

#ifdef YYTYPE_INT16
typedef YYTYPE_INT16 yytype_int16;
#else
typedef short int yytype_int16;
#endif

#ifndef YYSIZE_T
# ifdef __SIZE_TYPE__
#  define YYSIZE_T __SIZE_TYPE__
# elif defined size_t
#  define YYSIZE_T size_t
# elif ! defined YYSIZE_T && (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# else
#  define YYSIZE_T unsigned int
# endif
#endif

#define YYSIZE_MAXIMUM ((YYSIZE_T) -1)

#ifndef YY_
# if YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> /* INFRINGES ON USER NAME SPACE */
#   define YY_(msgid) dgettext ("bison-runtime", msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(msgid) msgid
# endif
#endif

/* Suppress unused-variable warnings by "using" E.  */
#if ! defined lint || defined __GNUC__
# define YYUSE(e) ((void) (e))
#else
# define YYUSE(e) /* empty */
#endif

/* Identity function, used to suppress warnings about constant conditions.  */
#ifndef lint
# define YYID(n) (n)
#else
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static int
YYID (int i)
#else
static int
YYID (i)
    int i;
#endif
{
  return i;
}
#endif

#if ! defined yyoverflow || YYERROR_VERBOSE

/* The parser invokes alloca or malloc; define the necessary symbols.  */

# ifdef YYSTACK_USE_ALLOCA
#  if YYSTACK_USE_ALLOCA
#   ifdef __GNUC__
#    define YYSTACK_ALLOC __builtin_alloca
#   elif defined __BUILTIN_VA_ARG_INCR
#    include <alloca.h> /* INFRINGES ON USER NAME SPACE */
#   elif defined _AIX
#    define YYSTACK_ALLOC __alloca
#   elif defined _MSC_VER
#    include <malloc.h> /* INFRINGES ON USER NAME SPACE */
#    define alloca _alloca
#   else
#    define YYSTACK_ALLOC alloca
#    if ! defined _ALLOCA_H && ! defined _STDLIB_H && (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
#     include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#     ifndef _STDLIB_H
#      define _STDLIB_H 1
#     endif
#    endif
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's `empty if-body' warning.  */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (YYID (0))
#  ifndef YYSTACK_ALLOC_MAXIMUM
    /* The OS might guarantee only one guard page at the bottom of the stack,
       and a page size can be as small as 4096 bytes.  So we cannot safely
       invoke alloca (N) if N exceeds 4096.  Use a slightly smaller number
       to allow for a few compiler-allocated temporary stack slots.  */
#   define YYSTACK_ALLOC_MAXIMUM 4032 /* reasonable circa 2006 */
#  endif
# else
#  define YYSTACK_ALLOC YYMALLOC
#  define YYSTACK_FREE YYFREE
#  ifndef YYSTACK_ALLOC_MAXIMUM
#   define YYSTACK_ALLOC_MAXIMUM YYSIZE_MAXIMUM
#  endif
#  if (defined __cplusplus && ! defined _STDLIB_H \
       && ! ((defined YYMALLOC || defined malloc) \
	     && (defined YYFREE || defined free)))
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   ifndef _STDLIB_H
#    define _STDLIB_H 1
#   endif
#  endif
#  ifndef YYMALLOC
#   define YYMALLOC malloc
#   if ! defined malloc && ! defined _STDLIB_H && (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
void *malloc (YYSIZE_T); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifndef YYFREE
#   define YYFREE free
#   if ! defined free && ! defined _STDLIB_H && (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
void free (void *); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
# endif
#endif /* ! defined yyoverflow || YYERROR_VERBOSE */


#if (! defined yyoverflow \
     && (! defined __cplusplus \
	 || (defined YYSTYPE_IS_TRIVIAL && YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  yytype_int16 yyss;
  YYSTYPE yyvs;
  };

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (sizeof (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (sizeof (yytype_int16) + sizeof (YYSTYPE)) \
      + YYSTACK_GAP_MAXIMUM)

/* Copy COUNT objects from FROM to TO.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if defined __GNUC__ && 1 < __GNUC__
#   define YYCOPY(To, From, Count) \
      __builtin_memcpy (To, From, (Count) * sizeof (*(From)))
#  else
#   define YYCOPY(To, From, Count)		\
      do					\
	{					\
	  YYSIZE_T yyi;				\
	  for (yyi = 0; yyi < (Count); yyi++)	\
	    (To)[yyi] = (From)[yyi];		\
	}					\
      while (YYID (0))
#  endif
# endif

/* Relocate STACK from its old location to the new one.  The
   local variables YYSIZE and YYSTACKSIZE give the old and new number of
   elements in the stack, and YYPTR gives the new location of the
   stack.  Advance YYPTR to a properly aligned location for the next
   stack.  */
# define YYSTACK_RELOCATE(Stack)					\
    do									\
      {									\
	YYSIZE_T yynewbytes;						\
	YYCOPY (&yyptr->Stack, Stack, yysize);				\
	Stack = &yyptr->Stack;						\
	yynewbytes = yystacksize * sizeof (*Stack) + YYSTACK_GAP_MAXIMUM; \
	yyptr += yynewbytes / sizeof (*yyptr);				\
      }									\
    while (YYID (0))

#endif

/* YYFINAL -- State number of the termination state.  */
#define YYFINAL  3
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   391

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  70
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  39
/* YYNRULES -- Number of rules.  */
#define YYNRULES  114
/* YYNRULES -- Number of states.  */
#define YYNSTATES  179

/* YYTRANSLATE(YYLEX) -- Bison symbol number corresponding to YYLEX.  */
#define YYUNDEFTOK  2
#define YYMAXUTOK   324

#define YYTRANSLATE(YYX)						\
  ((unsigned int) (YYX) <= YYMAXUTOK ? yytranslate[YYX] : YYUNDEFTOK)

/* YYTRANSLATE[YYLEX] -- Bison symbol number corresponding to YYLEX.  */
static const yytype_uint8 yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     1,     2,     3,     4,
       5,     6,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    60,    61,    62,    63,    64,
      65,    66,    67,    68,    69
};

#if YYDEBUG
/* YYPRHS[YYN] -- Index of the first RHS symbol of rule number YYN in
   YYRHS.  */
static const yytype_uint16 yyprhs[] =
{
       0,     0,     3,     4,     7,     9,    12,    15,    17,    19,
      21,    23,    25,    29,    33,    35,    38,    41,    45,    47,
      49,    51,    54,    58,    60,    63,    67,    69,    73,    76,
      80,    82,    85,    89,    91,    93,    96,    98,   100,   102,
     105,   107,   109,   111,   113,   115,   117,   119,   121,   123,
     125,   127,   129,   131,   133,   135,   137,   139,   141,   143,
     145,   147,   149,   151,   154,   157,   160,   164,   168,   172,
     176,   180,   183,   186,   189,   193,   195,   197,   200,   203,
     207,   209,   212,   216,   220,   225,   229,   231,   233,   235,
     237,   240,   243,   246,   249,   252,   255,   258,   261,   264,
     267,   270,   273,   276,   279,   282,   285,   288,   291,   294,
     297,   300,   303,   306,   309
};

/* YYRHS -- A `-1'-separated list of the rules' RHS.  */
static const yytype_int8 yyrhs[] =
{
      71,     0,    -1,    -1,    72,    73,    -1,    74,    -1,    73,
      74,    -1,    75,     3,    -1,     3,    -1,    76,    -1,    88,
      -1,    78,    -1,    77,    -1,    13,    61,    27,    -1,    14,
      61,    79,    -1,    81,    -1,    81,    84,    -1,    80,    81,
      -1,    80,    81,    84,    -1,     5,    -1,     6,    -1,     7,
      -1,    24,    25,    -1,    24,    82,    25,    -1,    83,    -1,
      82,    83,    -1,    82,    69,    83,    -1,    61,    -1,    15,
      61,    85,    -1,    24,    25,    -1,    24,    86,    25,    -1,
      87,    -1,    86,    87,    -1,    86,    69,    87,    -1,    61,
      -1,    89,    -1,    89,    90,    -1,    90,    -1,    28,    -1,
      91,    -1,    90,    91,    -1,    92,    -1,    94,    -1,    93,
      -1,   100,    -1,    95,    -1,    96,    -1,    97,    -1,    98,
      -1,    99,    -1,     4,    -1,    29,    -1,    30,    -1,    31,
      -1,    32,    -1,    33,    -1,    34,    -1,    27,    -1,    26,
      -1,    35,    -1,   108,    -1,   101,    -1,   104,    -1,   106,
      -1,    62,    67,    -1,    64,    67,    -1,    63,    68,    -1,
       8,    62,    67,    -1,     9,    62,    67,    -1,    11,    62,
      67,    -1,    10,    62,    67,    -1,    12,    62,    67,    -1,
      65,    67,    -1,    66,    67,    -1,    20,   102,    -1,    20,
     102,   103,    -1,    27,    -1,    61,    -1,    21,    27,    -1,
      22,    23,    -1,    22,   105,    23,    -1,    91,    -1,   105,
      91,    -1,   105,    69,    91,    -1,   105,     3,    91,    -1,
     105,    69,     3,    91,    -1,   107,    62,    67,    -1,    16,
      -1,    17,    -1,    18,    -1,    19,    -1,    39,    26,    -1,
      36,    26,    -1,    37,    26,    -1,    40,    26,    -1,    38,
      26,    -1,    41,    26,    -1,    42,    26,    -1,    43,    26,
      -1,    44,    26,    -1,    45,    26,    -1,    46,    26,    -1,
      47,    26,    -1,    48,    26,    -1,    49,    26,    -1,    50,
      26,    -1,    51,    26,    -1,    52,    26,    -1,    53,    26,
      -1,    54,    26,    -1,    55,    26,    -1,    56,    26,    -1,
      57,    26,    -1,    58,    26,    -1,    59,    26,    -1,    60,
      26,    -1
};

/* YYRLINE[YYN] -- source line where rule number YYN was defined.  */
static const yytype_uint16 yyrline[] =
{
       0,   194,   194,   194,   205,   210,   217,   223,   230,   237,
     247,   252,   259,   272,   331,   337,   343,   349,   357,   363,
     369,   377,   382,   389,   395,   401,   409,   416,   435,   440,
     447,   453,   459,   467,   474,   480,   487,   496,   503,   509,
     517,   518,   519,   520,   521,   522,   523,   524,   525,   526,
     527,   528,   529,   530,   531,   532,   533,   534,   549,   550,
     551,   552,   553,   557,   564,   571,   578,   585,   592,   599,
     606,   613,   618,   625,   630,   638,   657,   678,   685,   690,
     697,   703,   709,   715,   721,   729,   736,   738,   740,   742,
     746,   753,   760,   767,   774,   781,   788,   795,   802,   809,
     816,   823,   830,   837,   844,   851,   858,   865,   872,   879,
     886,   893,   900,   907,   914
};
#endif

#if YYDEBUG || YYERROR_VERBOSE || YYTOKEN_TABLE
/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "$end", "error", "$undefined", "NL", "NULL_", "CLASS", "SHARED",
  "DEFAULT", "DATE_", "TIME", "UTIME", "TIMESTAMP", "DATETIME", "CMD_ID",
  "CMD_CLASS", "CMD_CONSTRUCTOR", "REF_ELO_INT", "REF_ELO_EXT", "REF_USER",
  "REF_CLASS", "OBJECT_REFERENCE", "OID_DELIMETER", "SET_START_BRACE",
  "SET_END_BRACE", "START_PAREN", "END_PAREN", "REAL_LIT", "INT_LIT",
  "OID_", "TIME_LIT4", "TIME_LIT42", "TIME_LIT3", "TIME_LIT31",
  "TIME_LIT2", "TIME_LIT1", "DATE_LIT2", "YEN_SYMBOL", "WON_SYMBOL",
  "BACKSLASH", "DOLLAR_SYMBOL", "TURKISH_LIRA_CURRENCY",
  "BRITISH_POUND_SYMBOL", "CAMBODIAN_RIEL_SYMBOL",
  "CHINESE_RENMINBI_SYMBOL", "INDIAN_RUPEE_SYMBOL", "RUSSIAN_RUBLE_SYMBOL",
  "AUSTRALIAN_DOLLAR_SYMBOL", "CANADIAN_DOLLAR_SYMBOL",
  "BRASILIAN_REAL_SYMBOL", "ROMANIAN_LEU_SYMBOL", "EURO_SYMBOL",
  "SWISS_FRANC_SYMBOL", "DANISH_KRONE_SYMBOL", "NORWEGIAN_KRONE_SYMBOL",
  "BULGARIAN_LEV_SYMBOL", "VIETNAMESE_DONG_SYMBOL", "CZECH_KORUNA_SYMBOL",
  "POLISH_ZLOTY_SYMBOL", "SWEDISH_KRONA_SYMBOL", "CROATIAN_KUNA_SYMBOL",
  "SERBIAN_DINAR_SYMBOL", "IDENTIFIER", "Quote", "DQuote", "NQuote",
  "BQuote", "XQuote", "SQS_String_Body", "DQS_String_Body", "COMMA",
  "$accept", "loader_start", "@1", "loader_lines", "line", "one_line",
  "command_line", "id_command", "class_command", "class_commamd_spec",
  "attribute_list_qualifier", "attribute_list", "attribute_names",
  "attribute_name", "constructor_spec", "constructor_argument_list",
  "argument_names", "argument_name", "instance_line", "object_id",
  "constant_list", "constant", "ansi_string", "nchar_string", "dq_string",
  "sql2_date", "sql2_time", "sql2_timestamp", "utime", "sql2_datetime",
  "bit_string", "object_reference", "class_identifier", "instance_number",
  "set_constant", "set_elements", "system_object_reference", "ref_type",
  "monetary", 0
};
#endif

# ifdef YYPRINT
/* YYTOKNUM[YYLEX-NUM] -- Internal token number corresponding to
   token YYLEX-NUM.  */
static const yytype_uint16 yytoknum[] =
{
       0,   256,   257,   258,   259,   260,   261,   262,   263,   264,
     265,   266,   267,   268,   269,   270,   271,   272,   273,   274,
     275,   276,   277,   278,   279,   280,   281,   282,   283,   284,
     285,   286,   287,   288,   289,   290,   291,   292,   293,   294,
     295,   296,   297,   298,   299,   300,   301,   302,   303,   304,
     305,   306,   307,   308,   309,   310,   311,   312,   313,   314,
     315,   316,   317,   318,   319,   320,   321,   322,   323,   324
};
# endif

/* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const yytype_uint8 yyr1[] =
{
       0,    70,    72,    71,    73,    73,    74,    74,    75,    75,
      76,    76,    77,    78,    79,    79,    79,    79,    80,    80,
      80,    81,    81,    82,    82,    82,    83,    84,    85,    85,
      86,    86,    86,    87,    88,    88,    88,    89,    90,    90,
      91,    91,    91,    91,    91,    91,    91,    91,    91,    91,
      91,    91,    91,    91,    91,    91,    91,    91,    91,    91,
      91,    91,    91,    92,    93,    94,    95,    96,    97,    98,
      99,   100,   100,   101,   101,   102,   102,   103,   104,   104,
     105,   105,   105,   105,   105,   106,   107,   107,   107,   107,
     108,   108,   108,   108,   108,   108,   108,   108,   108,   108,
     108,   108,   108,   108,   108,   108,   108,   108,   108,   108,
     108,   108,   108,   108,   108
};

/* YYR2[YYN] -- Number of symbols composing right hand side of rule YYN.  */
static const yytype_uint8 yyr2[] =
{
       0,     2,     0,     2,     1,     2,     2,     1,     1,     1,
       1,     1,     3,     3,     1,     2,     2,     3,     1,     1,
       1,     2,     3,     1,     2,     3,     1,     3,     2,     3,
       1,     2,     3,     1,     1,     2,     1,     1,     1,     2,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     2,     2,     2,     3,     3,     3,     3,
       3,     2,     2,     2,     3,     1,     1,     2,     2,     3,
       1,     2,     3,     3,     4,     3,     1,     1,     1,     1,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2
};

/* YYDEFACT[STATE-NAME] -- Default rule to reduce with in state
   STATE-NUM when YYTABLE doesn't specify something else to do.  Zero
   means the default is an error.  */
static const yytype_uint8 yydefact[] =
{
       2,     0,     0,     1,     7,    49,     0,     0,     0,     0,
       0,     0,     0,    86,    87,    88,    89,     0,     0,    57,
      56,    37,    50,    51,    52,    53,    54,    55,    58,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     3,
       4,     0,     8,    11,    10,     9,    34,    36,    38,    40,
      42,    41,    44,    45,    46,    47,    48,    43,    60,    61,
      62,     0,    59,     0,     0,     0,     0,     0,     0,     0,
      75,    76,    73,    78,    80,     0,    91,    92,    94,    90,
      93,    95,    96,    97,    98,    99,   100,   101,   102,   103,
     104,   105,   106,   107,   108,   109,   110,   111,   112,   113,
     114,    63,    65,    64,    71,    72,     5,     6,    35,    39,
       0,    66,    67,    69,    68,    70,    12,    18,    19,    20,
       0,    13,     0,    14,     0,    74,     0,    79,     0,    81,
      85,    21,    26,     0,    23,    16,     0,    15,    77,    83,
       0,    82,    22,     0,    24,    17,     0,    84,    25,     0,
      27,    28,    33,     0,    30,    29,     0,    31,    32
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
      -1,     1,     2,    59,    60,    61,    62,    63,    64,   141,
     142,   143,   153,   154,   157,   170,   173,   174,    65,    66,
      67,    68,    69,    70,    71,    72,    73,    74,    75,    76,
      77,    78,    92,   145,    79,    95,    80,    81,    82
};

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
#define YYPACT_NINF -163
static const yytype_int16 yypact[] =
{
    -163,     6,   135,  -163,  -163,  -163,   -50,   -49,   -46,   -45,
     -44,   -42,   -41,  -163,  -163,  -163,  -163,   -20,   262,  -163,
    -163,  -163,  -163,  -163,  -163,  -163,  -163,  -163,  -163,    -5,
      -4,    -3,    -2,    -1,     0,     2,     4,     5,     7,     8,
       9,    10,    16,    17,    18,    21,    22,    24,    25,    28,
      29,    30,    36,    37,   -38,   -36,    50,    52,    53,   135,
    -163,    61,  -163,  -163,  -163,  -163,   325,   325,  -163,  -163,
    -163,  -163,  -163,  -163,  -163,  -163,  -163,  -163,  -163,  -163,
    -163,    11,  -163,    54,    55,    56,    57,    58,    43,     3,
    -163,  -163,    95,  -163,  -163,    49,  -163,  -163,  -163,  -163,
    -163,  -163,  -163,  -163,  -163,  -163,  -163,  -163,  -163,  -163,
    -163,  -163,  -163,  -163,  -163,  -163,  -163,  -163,  -163,  -163,
    -163,  -163,  -163,  -163,  -163,  -163,  -163,  -163,   325,  -163,
      59,  -163,  -163,  -163,  -163,  -163,  -163,  -163,  -163,  -163,
     -22,  -163,   103,   114,    47,  -163,   325,  -163,   199,  -163,
    -163,  -163,  -163,   -24,  -163,   114,    70,  -163,  -163,  -163,
     325,  -163,  -163,    71,  -163,  -163,   109,  -163,  -163,   -21,
    -163,  -163,  -163,   -23,  -163,  -163,    73,  -163,  -163
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -163,  -163,  -163,  -163,    76,  -163,  -163,  -163,  -163,  -163,
    -163,    -6,  -163,  -148,   -15,  -163,  -163,  -162,  -163,  -163,
      75,   -18,  -163,  -163,  -163,  -163,  -163,  -163,  -163,  -163,
    -163,  -163,  -163,  -163,  -163,  -163,  -163,  -163,  -163
};

/* YYTABLE[YYPACT[STATE-NUM]].  What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule which
   number is the opposite.  If zero, do what YYDEFACT says.
   If YYTABLE_NINF, syntax error.  */
#define YYTABLE_NINF -1
static const yytype_uint8 yytable[] =
{
      94,   162,   175,   151,   171,   164,     3,    90,   137,   138,
     139,   177,    83,    84,   178,   168,    85,    86,    87,    88,
      89,    96,    97,    98,    99,   100,   101,   140,   102,   121,
     103,   104,   122,   105,   106,   107,   108,   152,   172,   152,
     172,    91,   109,   110,   111,   163,   176,   112,   113,   129,
     114,   115,   146,     5,   116,   117,   118,     6,     7,     8,
       9,    10,   119,   120,   127,    13,    14,    15,    16,    17,
     136,    18,   147,   130,   158,    19,    20,   149,    22,    23,
      24,    25,    26,    27,    28,    29,    30,    31,    32,    33,
      34,    35,    36,    37,    38,    39,    40,    41,    42,    43,
      44,    45,    46,    47,    48,    49,    50,    51,    52,    53,
     129,    54,    55,    56,    57,    58,   144,   123,   148,   124,
     125,   131,   132,   133,   134,   135,   150,   140,   159,   156,
     161,   166,   152,   169,   172,   126,   155,     0,     4,     5,
     165,   128,   167,     6,     7,     8,     9,    10,    11,    12,
       0,    13,    14,    15,    16,    17,     0,    18,     0,     0,
       0,    19,    20,    21,    22,    23,    24,    25,    26,    27,
      28,    29,    30,    31,    32,    33,    34,    35,    36,    37,
      38,    39,    40,    41,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    53,     0,    54,    55,    56,
      57,    58,   160,     5,     0,     0,     0,     6,     7,     8,
       9,    10,     0,     0,     0,    13,    14,    15,    16,    17,
       0,    18,     0,     0,     0,    19,    20,     0,    22,    23,
      24,    25,    26,    27,    28,    29,    30,    31,    32,    33,
      34,    35,    36,    37,    38,    39,    40,    41,    42,    43,
      44,    45,    46,    47,    48,    49,    50,    51,    52,    53,
       0,    54,    55,    56,    57,    58,     5,     0,     0,     0,
       6,     7,     8,     9,    10,     0,     0,     0,    13,    14,
      15,    16,    17,     0,    18,    93,     0,     0,    19,    20,
       0,    22,    23,    24,    25,    26,    27,    28,    29,    30,
      31,    32,    33,    34,    35,    36,    37,    38,    39,    40,
      41,    42,    43,    44,    45,    46,    47,    48,    49,    50,
      51,    52,    53,     0,    54,    55,    56,    57,    58,     5,
       0,     0,     0,     6,     7,     8,     9,    10,     0,     0,
       0,    13,    14,    15,    16,    17,     0,    18,     0,     0,
       0,    19,    20,     0,    22,    23,    24,    25,    26,    27,
      28,    29,    30,    31,    32,    33,    34,    35,    36,    37,
      38,    39,    40,    41,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    53,     0,    54,    55,    56,
      57,    58
};

static const yytype_int16 yycheck[] =
{
      18,    25,    25,    25,    25,   153,     0,    27,     5,     6,
       7,   173,    62,    62,   176,   163,    62,    62,    62,    61,
      61,    26,    26,    26,    26,    26,    26,    24,    26,    67,
      26,    26,    68,    26,    26,    26,    26,    61,    61,    61,
      61,    61,    26,    26,    26,    69,    69,    26,    26,    67,
      26,    26,     3,     4,    26,    26,    26,     8,     9,    10,
      11,    12,    26,    26,     3,    16,    17,    18,    19,    20,
      27,    22,    23,    62,    27,    26,    27,    95,    29,    30,
      31,    32,    33,    34,    35,    36,    37,    38,    39,    40,
      41,    42,    43,    44,    45,    46,    47,    48,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    58,    59,    60,
     128,    62,    63,    64,    65,    66,    21,    67,    69,    67,
      67,    67,    67,    67,    67,    67,    67,    24,   146,    15,
     148,    61,    61,    24,    61,    59,   142,    -1,     3,     4,
     155,    66,   160,     8,     9,    10,    11,    12,    13,    14,
      -1,    16,    17,    18,    19,    20,    -1,    22,    -1,    -1,
      -1,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    60,    -1,    62,    63,    64,
      65,    66,     3,     4,    -1,    -1,    -1,     8,     9,    10,
      11,    12,    -1,    -1,    -1,    16,    17,    18,    19,    20,
      -1,    22,    -1,    -1,    -1,    26,    27,    -1,    29,    30,
      31,    32,    33,    34,    35,    36,    37,    38,    39,    40,
      41,    42,    43,    44,    45,    46,    47,    48,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    58,    59,    60,
      -1,    62,    63,    64,    65,    66,     4,    -1,    -1,    -1,
       8,     9,    10,    11,    12,    -1,    -1,    -1,    16,    17,
      18,    19,    20,    -1,    22,    23,    -1,    -1,    26,    27,
      -1,    29,    30,    31,    32,    33,    34,    35,    36,    37,
      38,    39,    40,    41,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    60,    -1,    62,    63,    64,    65,    66,     4,
      -1,    -1,    -1,     8,     9,    10,    11,    12,    -1,    -1,
      -1,    16,    17,    18,    19,    20,    -1,    22,    -1,    -1,
      -1,    26,    27,    -1,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    60,    -1,    62,    63,    64,
      65,    66
};

/* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
   symbol of state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,    71,    72,     0,     3,     4,     8,     9,    10,    11,
      12,    13,    14,    16,    17,    18,    19,    20,    22,    26,
      27,    28,    29,    30,    31,    32,    33,    34,    35,    36,
      37,    38,    39,    40,    41,    42,    43,    44,    45,    46,
      47,    48,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    58,    59,    60,    62,    63,    64,    65,    66,    73,
      74,    75,    76,    77,    78,    88,    89,    90,    91,    92,
      93,    94,    95,    96,    97,    98,    99,   100,   101,   104,
     106,   107,   108,    62,    62,    62,    62,    62,    61,    61,
      27,    61,   102,    23,    91,   105,    26,    26,    26,    26,
      26,    26,    26,    26,    26,    26,    26,    26,    26,    26,
      26,    26,    26,    26,    26,    26,    26,    26,    26,    26,
      26,    67,    68,    67,    67,    67,    74,     3,    90,    91,
      62,    67,    67,    67,    67,    67,    27,     5,     6,     7,
      24,    79,    80,    81,    21,   103,     3,    23,    69,    91,
      67,    25,    61,    82,    83,    81,    15,    84,    27,    91,
       3,    91,    25,    69,    83,    84,    61,    91,    83,    24,
      85,    25,    61,    86,    87,    25,    69,    87,    87
};

#define yyerrok		(yyerrstatus = 0)
#define yyclearin	(yychar = YYEMPTY)
#define YYEMPTY		(-2)
#define YYEOF		0

#define YYACCEPT	goto yyacceptlab
#define YYABORT		goto yyabortlab
#define YYERROR		goto yyerrorlab


/* Like YYERROR except do call yyerror.  This remains here temporarily
   to ease the transition to the new meaning of YYERROR, for GCC.
   Once GCC version 2 has supplanted version 1, this can go.  */

#define YYFAIL		goto yyerrlab

#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)					\
do								\
  if (yychar == YYEMPTY && yylen == 1)				\
    {								\
      yychar = (Token);						\
      yylval = (Value);						\
      yytoken = YYTRANSLATE (yychar);				\
      YYPOPSTACK (1);						\
      goto yybackup;						\
    }								\
  else								\
    {								\
      yyerror (YY_("syntax error: cannot back up")); \
      YYERROR;							\
    }								\
while (YYID (0))


#define YYTERROR	1
#define YYERRCODE	256


/* YYLLOC_DEFAULT -- Set CURRENT to span from RHS[1] to RHS[N].
   If N is 0, then set CURRENT to the empty location which ends
   the previous symbol: RHS[0] (always defined).  */

#define YYRHSLOC(Rhs, K) ((Rhs)[K])
#ifndef YYLLOC_DEFAULT
# define YYLLOC_DEFAULT(Current, Rhs, N)				\
    do									\
      if (YYID (N))                                                    \
	{								\
	  (Current).first_line   = YYRHSLOC (Rhs, 1).first_line;	\
	  (Current).first_column = YYRHSLOC (Rhs, 1).first_column;	\
	  (Current).last_line    = YYRHSLOC (Rhs, N).last_line;		\
	  (Current).last_column  = YYRHSLOC (Rhs, N).last_column;	\
	}								\
      else								\
	{								\
	  (Current).first_line   = (Current).last_line   =		\
	    YYRHSLOC (Rhs, 0).last_line;				\
	  (Current).first_column = (Current).last_column =		\
	    YYRHSLOC (Rhs, 0).last_column;				\
	}								\
    while (YYID (0))
#endif


/* YY_LOCATION_PRINT -- Print the location on the stream.
   This macro was not mandated originally: define only if we know
   we won't break user code: when these are the locations we know.  */

#ifndef YY_LOCATION_PRINT
# if YYLTYPE_IS_TRIVIAL
#  define YY_LOCATION_PRINT(File, Loc)			\
     fprintf (File, "%d.%d-%d.%d",			\
	      (Loc).first_line, (Loc).first_column,	\
	      (Loc).last_line,  (Loc).last_column)
# else
#  define YY_LOCATION_PRINT(File, Loc) ((void) 0)
# endif
#endif


/* YYLEX -- calling `yylex' with the right arguments.  */

#ifdef YYLEX_PARAM
# define YYLEX yylex (YYLEX_PARAM)
#else
# define YYLEX yylex ()
#endif

/* Enable debugging if requested.  */
#if YYDEBUG

# ifndef YYFPRINTF
#  include <stdio.h> /* INFRINGES ON USER NAME SPACE */
#  define YYFPRINTF fprintf
# endif

# define YYDPRINTF(Args)			\
do {						\
  if (yydebug)					\
    YYFPRINTF Args;				\
} while (YYID (0))

# define YY_SYMBOL_PRINT(Title, Type, Value, Location)			  \
do {									  \
  if (yydebug)								  \
    {									  \
      YYFPRINTF (stderr, "%s ", Title);					  \
      yy_symbol_print (stderr,						  \
		  Type, Value); \
      YYFPRINTF (stderr, "\n");						  \
    }									  \
} while (YYID (0))


/*--------------------------------.
| Print this symbol on YYOUTPUT.  |
`--------------------------------*/

/*ARGSUSED*/
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static void
yy_symbol_value_print (FILE *yyoutput, int yytype, YYSTYPE const * const yyvaluep)
#else
static void
yy_symbol_value_print (yyoutput, yytype, yyvaluep)
    FILE *yyoutput;
    int yytype;
    YYSTYPE const * const yyvaluep;
#endif
{
  if (!yyvaluep)
    return;
# ifdef YYPRINT
  if (yytype < YYNTOKENS)
    YYPRINT (yyoutput, yytoknum[yytype], *yyvaluep);
# else
  YYUSE (yyoutput);
# endif
  switch (yytype)
    {
      default:
	break;
    }
}


/*--------------------------------.
| Print this symbol on YYOUTPUT.  |
`--------------------------------*/

#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static void
yy_symbol_print (FILE *yyoutput, int yytype, YYSTYPE const * const yyvaluep)
#else
static void
yy_symbol_print (yyoutput, yytype, yyvaluep)
    FILE *yyoutput;
    int yytype;
    YYSTYPE const * const yyvaluep;
#endif
{
  if (yytype < YYNTOKENS)
    YYFPRINTF (yyoutput, "token %s (", yytname[yytype]);
  else
    YYFPRINTF (yyoutput, "nterm %s (", yytname[yytype]);

  yy_symbol_value_print (yyoutput, yytype, yyvaluep);
  YYFPRINTF (yyoutput, ")");
}

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (included).                                                   |
`------------------------------------------------------------------*/

#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static void
yy_stack_print (yytype_int16 *bottom, yytype_int16 *top)
#else
static void
yy_stack_print (bottom, top)
    yytype_int16 *bottom;
    yytype_int16 *top;
#endif
{
  YYFPRINTF (stderr, "Stack now");
  for (; bottom <= top; ++bottom)
    YYFPRINTF (stderr, " %d", *bottom);
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)				\
do {								\
  if (yydebug)							\
    yy_stack_print ((Bottom), (Top));				\
} while (YYID (0))


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static void
yy_reduce_print (YYSTYPE *yyvsp, int yyrule)
#else
static void
yy_reduce_print (yyvsp, yyrule)
    YYSTYPE *yyvsp;
    int yyrule;
#endif
{
  int yynrhs = yyr2[yyrule];
  int yyi;
  unsigned long int yylno = yyrline[yyrule];
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %lu):\n",
	     yyrule - 1, yylno);
  /* The symbols being reduced.  */
  for (yyi = 0; yyi < yynrhs; yyi++)
    {
      fprintf (stderr, "   $%d = ", yyi + 1);
      yy_symbol_print (stderr, yyrhs[yyprhs[yyrule] + yyi],
		       &(yyvsp[(yyi + 1) - (yynrhs)])
		       		       );
      fprintf (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)		\
do {					\
  if (yydebug)				\
    yy_reduce_print (yyvsp, Rule); \
} while (YYID (0))

/* Nonzero means print parse trace.  It is left uninitialized so that
   multiple parsers can coexist.  */
int yydebug;
#else /* !YYDEBUG */
# define YYDPRINTF(Args)
# define YY_SYMBOL_PRINT(Title, Type, Value, Location)
# define YY_STACK_PRINT(Bottom, Top)
# define YY_REDUCE_PRINT(Rule)
#endif /* !YYDEBUG */


/* YYINITDEPTH -- initial size of the parser's stacks.  */
#ifndef	YYINITDEPTH
# define YYINITDEPTH 200
#endif

/* YYMAXDEPTH -- maximum size the stacks can grow to (effective only
   if the built-in stack extension method is used).

   Do not make this value too large; the results are undefined if
   YYSTACK_ALLOC_MAXIMUM < YYSTACK_BYTES (YYMAXDEPTH)
   evaluated with infinite-precision integer arithmetic.  */

#ifndef YYMAXDEPTH
# define YYMAXDEPTH 10000
#endif



#if YYERROR_VERBOSE

# ifndef yystrlen
#  if defined __GLIBC__ && defined _STRING_H
#   define yystrlen strlen
#  else
/* Return the length of YYSTR.  */
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static YYSIZE_T
yystrlen (const char *yystr)
#else
static YYSIZE_T
yystrlen (yystr)
    const char *yystr;
#endif
{
  YYSIZE_T yylen;
  for (yylen = 0; yystr[yylen]; yylen++)
    continue;
  return yylen;
}
#  endif
# endif

# ifndef yystpcpy
#  if defined __GLIBC__ && defined _STRING_H && defined _GNU_SOURCE
#   define yystpcpy stpcpy
#  else
/* Copy YYSRC to YYDEST, returning the address of the terminating '\0' in
   YYDEST.  */
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static char *
yystpcpy (char *yydest, const char *yysrc)
#else
static char *
yystpcpy (yydest, yysrc)
    char *yydest;
    const char *yysrc;
#endif
{
  char *yyd = yydest;
  const char *yys = yysrc;

  while ((*yyd++ = *yys++) != '\0')
    continue;

  return yyd - 1;
}
#  endif
# endif

# ifndef yytnamerr
/* Copy to YYRES the contents of YYSTR after stripping away unnecessary
   quotes and backslashes, so that it's suitable for yyerror.  The
   heuristic is that double-quoting is unnecessary unless the string
   contains an apostrophe, a comma, or backslash (other than
   backslash-backslash).  YYSTR is taken from yytname.  If YYRES is
   null, do not copy; instead, return the length of what the result
   would have been.  */
static YYSIZE_T
yytnamerr (char *yyres, const char *yystr)
{
  if (*yystr == '"')
    {
      YYSIZE_T yyn = 0;
      char const *yyp = yystr;

      for (;;)
	switch (*++yyp)
	  {
	  case '\'':
	  case ',':
	    goto do_not_strip_quotes;

	  case '\\':
	    if (*++yyp != '\\')
	      goto do_not_strip_quotes;
	    /* Fall through.  */
	  default:
	    if (yyres)
	      yyres[yyn] = *yyp;
	    yyn++;
	    break;

	  case '"':
	    if (yyres)
	      yyres[yyn] = '\0';
	    return yyn;
	  }
    do_not_strip_quotes: ;
    }

  if (! yyres)
    return yystrlen (yystr);

  return yystpcpy (yyres, yystr) - yyres;
}
# endif

/* Copy into YYRESULT an error message about the unexpected token
   YYCHAR while in state YYSTATE.  Return the number of bytes copied,
   including the terminating null byte.  If YYRESULT is null, do not
   copy anything; just return the number of bytes that would be
   copied.  As a special case, return 0 if an ordinary "syntax error"
   message will do.  Return YYSIZE_MAXIMUM if overflow occurs during
   size calculation.  */
static YYSIZE_T
yysyntax_error (char *yyresult, int yystate, int yychar)
{
  int yyn = yypact[yystate];

  if (! (YYPACT_NINF < yyn && yyn <= YYLAST))
    return 0;
  else
    {
      int yytype = YYTRANSLATE (yychar);
      YYSIZE_T yysize0 = yytnamerr (0, yytname[yytype]);
      YYSIZE_T yysize = yysize0;
      YYSIZE_T yysize1;
      int yysize_overflow = 0;
      enum { YYERROR_VERBOSE_ARGS_MAXIMUM = 5 };
      char const *yyarg[YYERROR_VERBOSE_ARGS_MAXIMUM];
      int yyx;

# if 0
      /* This is so xgettext sees the translatable formats that are
	 constructed on the fly.  */
      YY_("syntax error, unexpected %s");
      YY_("syntax error, unexpected %s, expecting %s");
      YY_("syntax error, unexpected %s, expecting %s or %s");
      YY_("syntax error, unexpected %s, expecting %s or %s or %s");
      YY_("syntax error, unexpected %s, expecting %s or %s or %s or %s");
# endif
      char *yyfmt;
      char const *yyf;
      static char const yyunexpected[] = "syntax error, unexpected %s";
      static char const yyexpecting[] = ", expecting %s";
      static char const yyor[] = " or %s";
      char yyformat[sizeof yyunexpected
		    + sizeof yyexpecting - 1
		    + ((YYERROR_VERBOSE_ARGS_MAXIMUM - 2)
		       * (sizeof yyor - 1))];
      char const *yyprefix = yyexpecting;

      /* Start YYX at -YYN if negative to avoid negative indexes in
	 YYCHECK.  */
      int yyxbegin = yyn < 0 ? -yyn : 0;

      /* Stay within bounds of both yycheck and yytname.  */
      int yychecklim = YYLAST - yyn + 1;
      int yyxend = yychecklim < YYNTOKENS ? yychecklim : YYNTOKENS;
      int yycount = 1;

      yyarg[0] = yytname[yytype];
      yyfmt = yystpcpy (yyformat, yyunexpected);

      for (yyx = yyxbegin; yyx < yyxend; ++yyx)
	if (yycheck[yyx + yyn] == yyx && yyx != YYTERROR)
	  {
	    if (yycount == YYERROR_VERBOSE_ARGS_MAXIMUM)
	      {
		yycount = 1;
		yysize = yysize0;
		yyformat[sizeof yyunexpected - 1] = '\0';
		break;
	      }
	    yyarg[yycount++] = yytname[yyx];
	    yysize1 = yysize + yytnamerr (0, yytname[yyx]);
	    yysize_overflow |= (yysize1 < yysize);
	    yysize = yysize1;
	    yyfmt = yystpcpy (yyfmt, yyprefix);
	    yyprefix = yyor;
	  }

      yyf = YY_(yyformat);
      yysize1 = yysize + yystrlen (yyf);
      yysize_overflow |= (yysize1 < yysize);
      yysize = yysize1;

      if (yysize_overflow)
	return YYSIZE_MAXIMUM;

      if (yyresult)
	{
	  /* Avoid sprintf, as that infringes on the user's name space.
	     Don't have undefined behavior even if the translation
	     produced a string with the wrong number of "%s"s.  */
	  char *yyp = yyresult;
	  int yyi = 0;
	  while ((*yyp = *yyf) != '\0')
	    {
	      if (*yyp == '%' && yyf[1] == 's' && yyi < yycount)
		{
		  yyp += yytnamerr (yyp, yyarg[yyi++]);
		  yyf += 2;
		}
	      else
		{
		  yyp++;
		  yyf++;
		}
	    }
	}
      return yysize;
    }
}
#endif /* YYERROR_VERBOSE */


/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

/*ARGSUSED*/
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static void
yydestruct (const char *yymsg, int yytype, YYSTYPE *yyvaluep)
#else
static void
yydestruct (yymsg, yytype, yyvaluep)
    const char *yymsg;
    int yytype;
    YYSTYPE *yyvaluep;
#endif
{
  YYUSE (yyvaluep);

  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yytype, yyvaluep, yylocationp);

  switch (yytype)
    {

      default:
	break;
    }
}


/* Prevent warnings from -Wmissing-prototypes.  */

#ifdef YYPARSE_PARAM
#if defined __STDC__ || defined __cplusplus
int yyparse (void *YYPARSE_PARAM);
#else
int yyparse ();
#endif
#else /* ! YYPARSE_PARAM */
#if defined __STDC__ || defined __cplusplus
int yyparse (void);
#else
int yyparse ();
#endif
#endif /* ! YYPARSE_PARAM */



/* The look-ahead symbol.  */
int yychar;

/* The semantic value of the look-ahead symbol.  */
YYSTYPE yylval;

/* Number of syntax errors so far.  */
int yynerrs;



/*----------.
| yyparse.  |
`----------*/

#ifdef YYPARSE_PARAM
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
int
yyparse (void *YYPARSE_PARAM)
#else
int
yyparse (YYPARSE_PARAM)
    void *YYPARSE_PARAM;
#endif
#else /* ! YYPARSE_PARAM */
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
int
yyparse (void)
#else
int
yyparse ()

#endif
#endif
{
  
  int yystate;
  int yyn;
  int yyresult;
  /* Number of tokens to shift before error messages enabled.  */
  int yyerrstatus;
  /* Look-ahead token as an internal (translated) token number.  */
  int yytoken = 0;
#if YYERROR_VERBOSE
  /* Buffer for error messages, and its allocated size.  */
  char yymsgbuf[128];
  char *yymsg = yymsgbuf;
  YYSIZE_T yymsg_alloc = sizeof yymsgbuf;
#endif

  /* Three stacks and their tools:
     `yyss': related to states,
     `yyvs': related to semantic values,
     `yyls': related to locations.

     Refer to the stacks thru separate pointers, to allow yyoverflow
     to reallocate them elsewhere.  */

  /* The state stack.  */
  yytype_int16 yyssa[YYINITDEPTH];
  yytype_int16 *yyss = yyssa;
  yytype_int16 *yyssp;

  /* The semantic value stack.  */
  YYSTYPE yyvsa[YYINITDEPTH];
  YYSTYPE *yyvs = yyvsa;
  YYSTYPE *yyvsp;



#define YYPOPSTACK(N)   (yyvsp -= (N), yyssp -= (N))

  YYSIZE_T yystacksize = YYINITDEPTH;

  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;


  /* The number of symbols on the RHS of the reduced rule.
     Keep to zero when no symbol should be popped.  */
  int yylen = 0;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yystate = 0;
  yyerrstatus = 0;
  yynerrs = 0;
  yychar = YYEMPTY;		/* Cause a token to be read.  */

  /* Initialize stack pointers.
     Waste one element of value and location stack
     so that they stay on the same level as the state stack.
     The wasted elements are never initialized.  */

  yyssp = yyss;
  yyvsp = yyvs;

  goto yysetstate;

/*------------------------------------------------------------.
| yynewstate -- Push a new state, which is found in yystate.  |
`------------------------------------------------------------*/
 yynewstate:
  /* In all cases, when you get here, the value and location stacks
     have just been pushed.  So pushing a state here evens the stacks.  */
  yyssp++;

 yysetstate:
  *yyssp = yystate;

  if (yyss + yystacksize - 1 <= yyssp)
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYSIZE_T yysize = yyssp - yyss + 1;

#ifdef yyoverflow
      {
	/* Give user a chance to reallocate the stack.  Use copies of
	   these so that the &'s don't force the real ones into
	   memory.  */
	YYSTYPE *yyvs1 = yyvs;
	yytype_int16 *yyss1 = yyss;


	/* Each stack pointer address is followed by the size of the
	   data in use in that stack, in bytes.  This used to be a
	   conditional around just the two extra args, but that might
	   be undefined if yyoverflow is a macro.  */
	yyoverflow (YY_("memory exhausted"),
		    &yyss1, yysize * sizeof (*yyssp),
		    &yyvs1, yysize * sizeof (*yyvsp),

		    &yystacksize);

	yyss = yyss1;
	yyvs = yyvs1;
      }
#else /* no yyoverflow */
# ifndef YYSTACK_RELOCATE
      goto yyexhaustedlab;
# else
      /* Extend the stack our own way.  */
      if (YYMAXDEPTH <= yystacksize)
	goto yyexhaustedlab;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
	yystacksize = YYMAXDEPTH;

      {
	yytype_int16 *yyss1 = yyss;
	union yyalloc *yyptr =
	  (union yyalloc *) YYSTACK_ALLOC (YYSTACK_BYTES (yystacksize));
	if (! yyptr)
	  goto yyexhaustedlab;
	YYSTACK_RELOCATE (yyss);
	YYSTACK_RELOCATE (yyvs);

#  undef YYSTACK_RELOCATE
	if (yyss1 != yyssa)
	  YYSTACK_FREE (yyss1);
      }
# endif
#endif /* no yyoverflow */

      yyssp = yyss + yysize - 1;
      yyvsp = yyvs + yysize - 1;


      YYDPRINTF ((stderr, "Stack size increased to %lu\n",
		  (unsigned long int) yystacksize));

      if (yyss + yystacksize - 1 <= yyssp)
	YYABORT;
    }

  YYDPRINTF ((stderr, "Entering state %d\n", yystate));

  goto yybackup;

/*-----------.
| yybackup.  |
`-----------*/
yybackup:

  /* Do appropriate processing given the current state.  Read a
     look-ahead token if we need one and don't already have one.  */

  /* First try to decide what to do without reference to look-ahead token.  */
  yyn = yypact[yystate];
  if (yyn == YYPACT_NINF)
    goto yydefault;

  /* Not known => get a look-ahead token if don't already have one.  */

  /* YYCHAR is either YYEMPTY or YYEOF or a valid look-ahead symbol.  */
  if (yychar == YYEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token: "));
      yychar = YYLEX;
    }

  if (yychar <= YYEOF)
    {
      yychar = yytoken = YYEOF;
      YYDPRINTF ((stderr, "Now at end of input.\n"));
    }
  else
    {
      yytoken = YYTRANSLATE (yychar);
      YY_SYMBOL_PRINT ("Next token is", yytoken, &yylval, &yylloc);
    }

  /* If the proper action on seeing token YYTOKEN is to reduce or to
     detect an error, take that action.  */
  yyn += yytoken;
  if (yyn < 0 || YYLAST < yyn || yycheck[yyn] != yytoken)
    goto yydefault;
  yyn = yytable[yyn];
  if (yyn <= 0)
    {
      if (yyn == 0 || yyn == YYTABLE_NINF)
	goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }

  if (yyn == YYFINAL)
    YYACCEPT;

  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  /* Shift the look-ahead token.  */
  YY_SYMBOL_PRINT ("Shifting", yytoken, &yylval, &yylloc);

  /* Discard the shifted token unless it is eof.  */
  if (yychar != YYEOF)
    yychar = YYEMPTY;

  yystate = yyn;
  *++yyvsp = yylval;

  goto yynewstate;


/*-----------------------------------------------------------.
| yydefault -- do the default action for the current state.  |
`-----------------------------------------------------------*/
yydefault:
  yyn = yydefact[yystate];
  if (yyn == 0)
    goto yyerrlab;
  goto yyreduce;


/*-----------------------------.
| yyreduce -- Do a reduction.  |
`-----------------------------*/
yyreduce:
  /* yyn is the number of a rule to reduce with.  */
  yylen = yyr2[yyn];

  /* If YYLEN is nonzero, implement the default value of the action:
     `$$ = $1'.

     Otherwise, the following line sets YYVAL to garbage.
     This behavior is undocumented and Bison
     users should not rely upon it.  Assigning to YYVAL
     unconditionally makes the parser a bit smaller, and it avoids a
     GCC warning that YYVAL may be used uninitialized.  */
  yyval = yyvsp[1-yylen];


  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
        case 2:
#line 194 "../../src/executables/loader_grammar.y"
    {
    loader_initialize_lexer ();
    constant_Pool_idx = 0;
  ;}
    break;

  case 3:
#line 199 "../../src/executables/loader_grammar.y"
    {
    ldr_act_finish (ldr_Current_context, 0);
  ;}
    break;

  case 4:
#line 206 "../../src/executables/loader_grammar.y"
    {
    DBG_PRINT ("line");
  ;}
    break;

  case 5:
#line 211 "../../src/executables/loader_grammar.y"
    {
    DBG_PRINT ("line_list line");
  ;}
    break;

  case 6:
#line 218 "../../src/executables/loader_grammar.y"
    {
    DBG_PRINT ("one_line");
    loader_In_instance_line = true;
  ;}
    break;

  case 7:
#line 224 "../../src/executables/loader_grammar.y"
    {
    loader_In_instance_line = true;
  ;}
    break;

  case 8:
#line 231 "../../src/executables/loader_grammar.y"
    {
    DBG_PRINT ("command_line");
    loader_reset_string_pool ();
    constant_Pool_idx = 0;
  ;}
    break;

  case 9:
#line 238 "../../src/executables/loader_grammar.y"
    {
    DBG_PRINT ("instance_line");
    ldr_act_finish_line (ldr_Current_context);
    loader_reset_string_pool ();
    constant_Pool_idx = 0;
  ;}
    break;

  case 10:
#line 248 "../../src/executables/loader_grammar.y"
    {
    DBG_PRINT ("class_command");
  ;}
    break;

  case 11:
#line 253 "../../src/executables/loader_grammar.y"
    {
    DBG_PRINT ("id_command");
  ;}
    break;

  case 12:
#line 260 "../../src/executables/loader_grammar.y"
    {
    skip_current_class = false;

    ldr_act_start_id (ldr_Current_context, (yyvsp[(2) - (3)].string)->val);
    ldr_act_set_id (ldr_Current_context, atoi ((yyvsp[(3) - (3)].string)->val));

    FREE_STRING ((yyvsp[(2) - (3)].string));
    FREE_STRING ((yyvsp[(3) - (3)].string));
  ;}
    break;

  case 13:
#line 273 "../../src/executables/loader_grammar.y"
    {
    LDR_CLASS_COMMAND_SPEC *cmd_spec;
    LDR_STRING *class_name;
    LDR_STRING *attr, *save, *args;

    DBG_PRINT ("class_commamd_spec");

    class_name = (yyvsp[(2) - (3)].string);
    cmd_spec = (yyvsp[(3) - (3)].cmd_spec);

    ldr_act_set_skipCurrentclass (class_name->val, class_name->size);
    ldr_act_init_context (ldr_Current_context, class_name->val,
                          class_name->size);

    if (cmd_spec->qualifier != LDR_ATTRIBUTE_ANY)
      {
        ldr_act_restrict_attributes (ldr_Current_context, cmd_spec->qualifier);
      }

    for (attr = cmd_spec->attr_list; attr; attr = attr->next)
      {
        ldr_act_add_attr (ldr_Current_context, attr->val, attr->size);
      }

    ldr_act_check_missing_non_null_attrs (ldr_Current_context);

    if (cmd_spec->ctor_spec)
      {
        ldr_act_set_constructor (ldr_Current_context,
                                 cmd_spec->ctor_spec->idname->val);

        for (args = cmd_spec->ctor_spec->arg_list; args; args = args->next)
          {
            ldr_act_add_argument (ldr_Current_context, args->val);
          }

        for (args = cmd_spec->ctor_spec->arg_list; args; args = save)
          {
            save = args->next;
            FREE_STRING (args);
          }

        FREE_STRING (cmd_spec->ctor_spec->idname);
        free_and_init (cmd_spec->ctor_spec);
      }

    for (attr = cmd_spec->attr_list; attr; attr = save)
      {
        save = attr->next;
        FREE_STRING (attr);
      }

    FREE_STRING (class_name);
    free_and_init (cmd_spec);
  ;}
    break;

  case 14:
#line 332 "../../src/executables/loader_grammar.y"
    {
    DBG_PRINT ("attribute_list");
    (yyval.cmd_spec) = loader_make_class_command_spec (LDR_ATTRIBUTE_ANY, (yyvsp[(1) - (1)].string), NULL);
  ;}
    break;

  case 15:
#line 338 "../../src/executables/loader_grammar.y"
    {
    DBG_PRINT ("attribute_list constructor_spec");
    (yyval.cmd_spec) = loader_make_class_command_spec (LDR_ATTRIBUTE_ANY, (yyvsp[(1) - (2)].string), (yyvsp[(2) - (2)].ctor_spec));
  ;}
    break;

  case 16:
#line 344 "../../src/executables/loader_grammar.y"
    {
    DBG_PRINT ("attribute_list_qualifier attribute_list");
    (yyval.cmd_spec) = loader_make_class_command_spec ((yyvsp[(1) - (2)].intval), (yyvsp[(2) - (2)].string), NULL);
  ;}
    break;

  case 17:
#line 350 "../../src/executables/loader_grammar.y"
    {
    DBG_PRINT ("attribute_list_qualifier attribute_list constructor_spec");
    (yyval.cmd_spec) = loader_make_class_command_spec ((yyvsp[(1) - (3)].intval), (yyvsp[(2) - (3)].string), (yyvsp[(3) - (3)].ctor_spec));
  ;}
    break;

  case 18:
#line 358 "../../src/executables/loader_grammar.y"
    {
    DBG_PRINT ("CLASS");
    (yyval.intval) = LDR_ATTRIBUTE_CLASS;
  ;}
    break;

  case 19:
#line 364 "../../src/executables/loader_grammar.y"
    {
    DBG_PRINT ("SHARED");
    (yyval.intval) = LDR_ATTRIBUTE_SHARED;
  ;}
    break;

  case 20:
#line 370 "../../src/executables/loader_grammar.y"
    {
    DBG_PRINT ("DEFAULT");
    (yyval.intval) = LDR_ATTRIBUTE_DEFAULT;
  ;}
    break;

  case 21:
#line 378 "../../src/executables/loader_grammar.y"
    {
    (yyval.string) = NULL;
  ;}
    break;

  case 22:
#line 383 "../../src/executables/loader_grammar.y"
    {
    (yyval.string) = (yyvsp[(2) - (3)].string);
  ;}
    break;

  case 23:
#line 390 "../../src/executables/loader_grammar.y"
    {
    DBG_PRINT ("attribute_name");
    (yyval.string) = loader_append_string_list (NULL, (yyvsp[(1) - (1)].string));
  ;}
    break;

  case 24:
#line 396 "../../src/executables/loader_grammar.y"
    {
    DBG_PRINT ("attribute_names attribute_name");
    (yyval.string) = loader_append_string_list ((yyvsp[(1) - (2)].string), (yyvsp[(2) - (2)].string));
  ;}
    break;

  case 25:
#line 402 "../../src/executables/loader_grammar.y"
    {
    DBG_PRINT ("attribute_names COMMA attribute_name");
    (yyval.string) = loader_append_string_list ((yyvsp[(1) - (3)].string), (yyvsp[(3) - (3)].string));
  ;}
    break;

  case 26:
#line 410 "../../src/executables/loader_grammar.y"
    {
    (yyval.string) = (yyvsp[(1) - (1)].string);
  ;}
    break;

  case 27:
#line 417 "../../src/executables/loader_grammar.y"
    {
    LDR_CONSTRUCTOR_SPEC *spec;

    spec = (LDR_CONSTRUCTOR_SPEC *) malloc (sizeof (LDR_CONSTRUCTOR_SPEC));
    if (spec == NULL)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	        ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (LDR_CONSTRUCTOR_SPEC));
	YYABORT;
      }

    spec->idname = (yyvsp[(2) - (3)].string);
    spec->arg_list = (yyvsp[(3) - (3)].string);
    (yyval.ctor_spec) = spec;
  ;}
    break;

  case 28:
#line 436 "../../src/executables/loader_grammar.y"
    {
    (yyval.string) = NULL;
  ;}
    break;

  case 29:
#line 441 "../../src/executables/loader_grammar.y"
    {
    (yyval.string) = (yyvsp[(2) - (3)].string);
  ;}
    break;

  case 30:
#line 448 "../../src/executables/loader_grammar.y"
    {
    DBG_PRINT ("argument_name");
    (yyval.string) = loader_append_string_list (NULL, (yyvsp[(1) - (1)].string));
  ;}
    break;

  case 31:
#line 454 "../../src/executables/loader_grammar.y"
    {
    DBG_PRINT ("argument_names argument_name");
    (yyval.string) = loader_append_string_list ((yyvsp[(1) - (2)].string), (yyvsp[(2) - (2)].string));
  ;}
    break;

  case 32:
#line 460 "../../src/executables/loader_grammar.y"
    {
    DBG_PRINT ("argument_names COMMA argument_name");
    (yyval.string) = loader_append_string_list ((yyvsp[(1) - (3)].string), (yyvsp[(3) - (3)].string));
  ;}
    break;

  case 33:
#line 468 "../../src/executables/loader_grammar.y"
    {
    (yyval.string) = (yyvsp[(1) - (1)].string);
  ;}
    break;

  case 34:
#line 475 "../../src/executables/loader_grammar.y"
    {
    skip_current_instance = false;
    ldr_act_start_instance (ldr_Current_context, (yyvsp[(1) - (1)].intval), NULL);
  ;}
    break;

  case 35:
#line 481 "../../src/executables/loader_grammar.y"
    {
    skip_current_instance = false;
    ldr_act_start_instance (ldr_Current_context, (yyvsp[(1) - (2)].intval), (yyvsp[(2) - (2)].constant));
    ldr_process_constants ((yyvsp[(2) - (2)].constant));
  ;}
    break;

  case 36:
#line 488 "../../src/executables/loader_grammar.y"
    {
    skip_current_instance = false;
    ldr_act_start_instance (ldr_Current_context, -1, (yyvsp[(1) - (1)].constant));
    ldr_process_constants ((yyvsp[(1) - (1)].constant));
  ;}
    break;

  case 37:
#line 497 "../../src/executables/loader_grammar.y"
    {
    (yyval.intval) = (yyvsp[(1) - (1)].intval);
  ;}
    break;

  case 38:
#line 504 "../../src/executables/loader_grammar.y"
    {
    DBG_PRINT ("constant");
    (yyval.constant) = loader_append_constant_list (NULL, (yyvsp[(1) - (1)].constant));
  ;}
    break;

  case 39:
#line 510 "../../src/executables/loader_grammar.y"
    {
    DBG_PRINT ("constant_list constant");
    (yyval.constant) = loader_append_constant_list ((yyvsp[(1) - (2)].constant), (yyvsp[(2) - (2)].constant));
  ;}
    break;

  case 40:
#line 517 "../../src/executables/loader_grammar.y"
    { (yyval.constant) = (yyvsp[(1) - (1)].constant); ;}
    break;

  case 41:
#line 518 "../../src/executables/loader_grammar.y"
    { (yyval.constant) = (yyvsp[(1) - (1)].constant); ;}
    break;

  case 42:
#line 519 "../../src/executables/loader_grammar.y"
    { (yyval.constant) = (yyvsp[(1) - (1)].constant); ;}
    break;

  case 43:
#line 520 "../../src/executables/loader_grammar.y"
    { (yyval.constant) = (yyvsp[(1) - (1)].constant); ;}
    break;

  case 44:
#line 521 "../../src/executables/loader_grammar.y"
    { (yyval.constant) = (yyvsp[(1) - (1)].constant); ;}
    break;

  case 45:
#line 522 "../../src/executables/loader_grammar.y"
    { (yyval.constant) = (yyvsp[(1) - (1)].constant); ;}
    break;

  case 46:
#line 523 "../../src/executables/loader_grammar.y"
    { (yyval.constant) = (yyvsp[(1) - (1)].constant); ;}
    break;

  case 47:
#line 524 "../../src/executables/loader_grammar.y"
    { (yyval.constant) = (yyvsp[(1) - (1)].constant); ;}
    break;

  case 48:
#line 525 "../../src/executables/loader_grammar.y"
    { (yyval.constant) = (yyvsp[(1) - (1)].constant); ;}
    break;

  case 49:
#line 526 "../../src/executables/loader_grammar.y"
    { (yyval.constant) = loader_make_constant(LDR_NULL, NULL); ;}
    break;

  case 50:
#line 527 "../../src/executables/loader_grammar.y"
    { (yyval.constant) = loader_make_constant(LDR_TIME, (yyvsp[(1) - (1)].string)); ;}
    break;

  case 51:
#line 528 "../../src/executables/loader_grammar.y"
    { (yyval.constant) = loader_make_constant(LDR_TIME, (yyvsp[(1) - (1)].string)); ;}
    break;

  case 52:
#line 529 "../../src/executables/loader_grammar.y"
    { (yyval.constant) = loader_make_constant(LDR_TIME, (yyvsp[(1) - (1)].string)); ;}
    break;

  case 53:
#line 530 "../../src/executables/loader_grammar.y"
    { (yyval.constant) = loader_make_constant(LDR_TIME, (yyvsp[(1) - (1)].string)); ;}
    break;

  case 54:
#line 531 "../../src/executables/loader_grammar.y"
    { (yyval.constant) = loader_make_constant(LDR_TIME, (yyvsp[(1) - (1)].string)); ;}
    break;

  case 55:
#line 532 "../../src/executables/loader_grammar.y"
    { (yyval.constant) = loader_make_constant(LDR_TIME, (yyvsp[(1) - (1)].string)); ;}
    break;

  case 56:
#line 533 "../../src/executables/loader_grammar.y"
    { (yyval.constant) = loader_make_constant(LDR_INT, (yyvsp[(1) - (1)].string)); ;}
    break;

  case 57:
#line 535 "../../src/executables/loader_grammar.y"
    {
    if (strchr ((yyvsp[(1) - (1)].string)->val, 'F') != NULL || strchr ((yyvsp[(1) - (1)].string)->val, 'f') != NULL)
      {
        (yyval.constant) = loader_make_constant (LDR_FLOAT, (yyvsp[(1) - (1)].string));
      }
    else if (strchr ((yyvsp[(1) - (1)].string)->val, 'E') != NULL || strchr ((yyvsp[(1) - (1)].string)->val, 'e') != NULL)
      {
        (yyval.constant) = loader_make_constant (LDR_DOUBLE, (yyvsp[(1) - (1)].string));
      }
    else
      {
        (yyval.constant) = loader_make_constant (LDR_NUMERIC, (yyvsp[(1) - (1)].string));
      }
  ;}
    break;

  case 58:
#line 549 "../../src/executables/loader_grammar.y"
    { (yyval.constant) = loader_make_constant(LDR_DATE, (yyvsp[(1) - (1)].string)); ;}
    break;

  case 59:
#line 550 "../../src/executables/loader_grammar.y"
    { (yyval.constant) = (yyvsp[(1) - (1)].constant); ;}
    break;

  case 60:
#line 551 "../../src/executables/loader_grammar.y"
    { (yyval.constant) = (yyvsp[(1) - (1)].constant); ;}
    break;

  case 61:
#line 552 "../../src/executables/loader_grammar.y"
    { (yyval.constant) = (yyvsp[(1) - (1)].constant); ;}
    break;

  case 62:
#line 553 "../../src/executables/loader_grammar.y"
    { (yyval.constant) = (yyvsp[(1) - (1)].constant); ;}
    break;

  case 63:
#line 558 "../../src/executables/loader_grammar.y"
    {
    (yyval.constant) = loader_make_constant (LDR_STR, (yyvsp[(2) - (2)].string));
  ;}
    break;

  case 64:
#line 565 "../../src/executables/loader_grammar.y"
    {
    (yyval.constant) = loader_make_constant (LDR_NSTR, (yyvsp[(2) - (2)].string));
  ;}
    break;

  case 65:
#line 572 "../../src/executables/loader_grammar.y"
    {
    (yyval.constant) = loader_make_constant (LDR_STR, (yyvsp[(2) - (2)].string));
  ;}
    break;

  case 66:
#line 579 "../../src/executables/loader_grammar.y"
    {
    (yyval.constant) = loader_make_constant (LDR_DATE, (yyvsp[(3) - (3)].string));
  ;}
    break;

  case 67:
#line 586 "../../src/executables/loader_grammar.y"
    {
    (yyval.constant) = loader_make_constant (LDR_TIME, (yyvsp[(3) - (3)].string));
  ;}
    break;

  case 68:
#line 593 "../../src/executables/loader_grammar.y"
    {
    (yyval.constant) = loader_make_constant (LDR_TIMESTAMP, (yyvsp[(3) - (3)].string));
  ;}
    break;

  case 69:
#line 600 "../../src/executables/loader_grammar.y"
    {
    (yyval.constant) = loader_make_constant (LDR_TIMESTAMP, (yyvsp[(3) - (3)].string));
  ;}
    break;

  case 70:
#line 607 "../../src/executables/loader_grammar.y"
    {
    (yyval.constant) = loader_make_constant (LDR_DATETIME, (yyvsp[(3) - (3)].string));
  ;}
    break;

  case 71:
#line 614 "../../src/executables/loader_grammar.y"
    {
    (yyval.constant) = loader_make_constant (LDR_BSTR, (yyvsp[(2) - (2)].string));
  ;}
    break;

  case 72:
#line 619 "../../src/executables/loader_grammar.y"
    {
    (yyval.constant) = loader_make_constant (LDR_XSTR, (yyvsp[(2) - (2)].string));
  ;}
    break;

  case 73:
#line 626 "../../src/executables/loader_grammar.y"
    {
    (yyval.constant) = loader_make_constant (LDR_CLASS_OID, (yyvsp[(2) - (2)].obj_ref));
  ;}
    break;

  case 74:
#line 631 "../../src/executables/loader_grammar.y"
    {
    (yyvsp[(2) - (3)].obj_ref)->instance_number = (yyvsp[(3) - (3)].string);
    (yyval.constant) = loader_make_constant (LDR_OID, (yyvsp[(2) - (3)].obj_ref));
  ;}
    break;

  case 75:
#line 639 "../../src/executables/loader_grammar.y"
    {
    LDR_OBJECT_REF *ref;

    ref = (LDR_OBJECT_REF *) malloc (sizeof (LDR_OBJECT_REF));
    if (ref == NULL)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	        ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (LDR_OBJECT_REF));
	YYABORT;
      }

    ref->class_id = (yyvsp[(1) - (1)].string);
    ref->class_name = NULL;
    ref->instance_number = NULL;
    
    (yyval.obj_ref) = ref;
  ;}
    break;

  case 76:
#line 658 "../../src/executables/loader_grammar.y"
    {
    LDR_OBJECT_REF *ref;

    ref = (LDR_OBJECT_REF *) malloc (sizeof (LDR_OBJECT_REF));
    if (ref == NULL)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	        ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (LDR_OBJECT_REF));
      	YYABORT;
      }

    ref->class_id = NULL;
    ref->class_name = (yyvsp[(1) - (1)].string);
    ref->instance_number = NULL;
    
    (yyval.obj_ref) = ref;
  ;}
    break;

  case 77:
#line 679 "../../src/executables/loader_grammar.y"
    {
    (yyval.string) = (yyvsp[(2) - (2)].string);
  ;}
    break;

  case 78:
#line 686 "../../src/executables/loader_grammar.y"
    {
    (yyval.constant) = loader_make_constant (LDR_COLLECTION, NULL);
  ;}
    break;

  case 79:
#line 691 "../../src/executables/loader_grammar.y"
    {
    (yyval.constant) = loader_make_constant (LDR_COLLECTION, (yyvsp[(2) - (3)].constant));
  ;}
    break;

  case 80:
#line 698 "../../src/executables/loader_grammar.y"
    {
    DBG_PRINT ("constant");
    (yyval.constant) = loader_append_constant_list (NULL, (yyvsp[(1) - (1)].constant));
  ;}
    break;

  case 81:
#line 704 "../../src/executables/loader_grammar.y"
    {
    DBG_PRINT ("set_elements constant");
    (yyval.constant) = loader_append_constant_list ((yyvsp[(1) - (2)].constant), (yyvsp[(2) - (2)].constant));
  ;}
    break;

  case 82:
#line 710 "../../src/executables/loader_grammar.y"
    {
    DBG_PRINT ("set_elements COMMA constant");
    (yyval.constant) = loader_append_constant_list ((yyvsp[(1) - (3)].constant), (yyvsp[(3) - (3)].constant));
  ;}
    break;

  case 83:
#line 716 "../../src/executables/loader_grammar.y"
    {
    DBG_PRINT ("set_elements NL constant");
    (yyval.constant) = loader_append_constant_list ((yyvsp[(1) - (3)].constant), (yyvsp[(3) - (3)].constant));
  ;}
    break;

  case 84:
#line 722 "../../src/executables/loader_grammar.y"
    {
    DBG_PRINT ("set_elements COMMA NL constant");
    (yyval.constant) = loader_append_constant_list ((yyvsp[(1) - (4)].constant), (yyvsp[(4) - (4)].constant));
  ;}
    break;

  case 85:
#line 730 "../../src/executables/loader_grammar.y"
    {
    (yyval.constant) = loader_make_constant ((yyvsp[(1) - (3)].intval), (yyvsp[(3) - (3)].string));
  ;}
    break;

  case 86:
#line 736 "../../src/executables/loader_grammar.y"
    { (yyval.intval) = LDR_ELO_INT; ;}
    break;

  case 87:
#line 738 "../../src/executables/loader_grammar.y"
    { (yyval.intval) = LDR_ELO_EXT; ;}
    break;

  case 88:
#line 740 "../../src/executables/loader_grammar.y"
    { (yyval.intval) = LDR_SYS_USER; ;}
    break;

  case 89:
#line 742 "../../src/executables/loader_grammar.y"
    { (yyval.intval) = LDR_SYS_CLASS; ;}
    break;

  case 90:
#line 747 "../../src/executables/loader_grammar.y"
    {
    LDR_MONETARY_VALUE *mon_value = loader_make_monetary_value (DB_CURRENCY_DOLLAR, (yyvsp[(2) - (2)].string));
    
    (yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
  ;}
    break;

  case 91:
#line 754 "../../src/executables/loader_grammar.y"
    {
    LDR_MONETARY_VALUE *mon_value = loader_make_monetary_value (DB_CURRENCY_YEN, (yyvsp[(2) - (2)].string));
    
    (yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
  ;}
    break;

  case 92:
#line 761 "../../src/executables/loader_grammar.y"
    {
    LDR_MONETARY_VALUE *mon_value = loader_make_monetary_value (DB_CURRENCY_WON, (yyvsp[(2) - (2)].string));
    
    (yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
  ;}
    break;

  case 93:
#line 768 "../../src/executables/loader_grammar.y"
    {
    LDR_MONETARY_VALUE *mon_value = loader_make_monetary_value (DB_CURRENCY_TL, (yyvsp[(2) - (2)].string));
    
    (yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
  ;}
    break;

  case 94:
#line 775 "../../src/executables/loader_grammar.y"
    {
    LDR_MONETARY_VALUE *mon_value = loader_make_monetary_value (DB_CURRENCY_WON, (yyvsp[(2) - (2)].string));
    
    (yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
  ;}
    break;

  case 95:
#line 782 "../../src/executables/loader_grammar.y"
    {
    LDR_MONETARY_VALUE *mon_value = loader_make_monetary_value (DB_CURRENCY_BRITISH_POUND, (yyvsp[(2) - (2)].string));
    
    (yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
  ;}
    break;

  case 96:
#line 789 "../../src/executables/loader_grammar.y"
    {
    LDR_MONETARY_VALUE *mon_value = loader_make_monetary_value (DB_CURRENCY_CAMBODIAN_RIEL, (yyvsp[(2) - (2)].string));
    
    (yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
  ;}
    break;

  case 97:
#line 796 "../../src/executables/loader_grammar.y"
    {
    LDR_MONETARY_VALUE *mon_value = loader_make_monetary_value (DB_CURRENCY_CHINESE_RENMINBI, (yyvsp[(2) - (2)].string));
    
    (yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
  ;}
    break;

  case 98:
#line 803 "../../src/executables/loader_grammar.y"
    {
    LDR_MONETARY_VALUE *mon_value = loader_make_monetary_value (DB_CURRENCY_INDIAN_RUPEE, (yyvsp[(2) - (2)].string));
    
    (yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
  ;}
    break;

  case 99:
#line 810 "../../src/executables/loader_grammar.y"
    {
    LDR_MONETARY_VALUE *mon_value = loader_make_monetary_value (DB_CURRENCY_RUSSIAN_RUBLE, (yyvsp[(2) - (2)].string));
    
    (yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
  ;}
    break;

  case 100:
#line 817 "../../src/executables/loader_grammar.y"
    {
    LDR_MONETARY_VALUE *mon_value = loader_make_monetary_value (DB_CURRENCY_AUSTRALIAN_DOLLAR, (yyvsp[(2) - (2)].string));
    
    (yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
  ;}
    break;

  case 101:
#line 824 "../../src/executables/loader_grammar.y"
    {
    LDR_MONETARY_VALUE *mon_value = loader_make_monetary_value (DB_CURRENCY_CANADIAN_DOLLAR, (yyvsp[(2) - (2)].string));
    
    (yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
  ;}
    break;

  case 102:
#line 831 "../../src/executables/loader_grammar.y"
    {
    LDR_MONETARY_VALUE *mon_value = loader_make_monetary_value (DB_CURRENCY_BRASILIAN_REAL, (yyvsp[(2) - (2)].string));
    
    (yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
  ;}
    break;

  case 103:
#line 838 "../../src/executables/loader_grammar.y"
    {
    LDR_MONETARY_VALUE *mon_value = loader_make_monetary_value (DB_CURRENCY_ROMANIAN_LEU, (yyvsp[(2) - (2)].string));
    
    (yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
  ;}
    break;

  case 104:
#line 845 "../../src/executables/loader_grammar.y"
    {
    LDR_MONETARY_VALUE *mon_value = loader_make_monetary_value (DB_CURRENCY_EURO, (yyvsp[(2) - (2)].string));
    
    (yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
  ;}
    break;

  case 105:
#line 852 "../../src/executables/loader_grammar.y"
    {
    LDR_MONETARY_VALUE *mon_value = loader_make_monetary_value (DB_CURRENCY_SWISS_FRANC, (yyvsp[(2) - (2)].string));
    
    (yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
  ;}
    break;

  case 106:
#line 859 "../../src/executables/loader_grammar.y"
    {
    LDR_MONETARY_VALUE *mon_value = loader_make_monetary_value (DB_CURRENCY_DANISH_KRONE, (yyvsp[(2) - (2)].string));
    
    (yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
  ;}
    break;

  case 107:
#line 866 "../../src/executables/loader_grammar.y"
    {
    LDR_MONETARY_VALUE *mon_value = loader_make_monetary_value (DB_CURRENCY_NORWEGIAN_KRONE, (yyvsp[(2) - (2)].string));
    
    (yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
  ;}
    break;

  case 108:
#line 873 "../../src/executables/loader_grammar.y"
    {
    LDR_MONETARY_VALUE *mon_value = loader_make_monetary_value (DB_CURRENCY_BULGARIAN_LEV, (yyvsp[(2) - (2)].string));
    
    (yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
  ;}
    break;

  case 109:
#line 880 "../../src/executables/loader_grammar.y"
    {
    LDR_MONETARY_VALUE *mon_value = loader_make_monetary_value (DB_CURRENCY_VIETNAMESE_DONG, (yyvsp[(2) - (2)].string));
    
    (yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
  ;}
    break;

  case 110:
#line 887 "../../src/executables/loader_grammar.y"
    {
    LDR_MONETARY_VALUE *mon_value = loader_make_monetary_value (DB_CURRENCY_CZECH_KORUNA, (yyvsp[(2) - (2)].string));
    
    (yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
  ;}
    break;

  case 111:
#line 894 "../../src/executables/loader_grammar.y"
    {
    LDR_MONETARY_VALUE *mon_value = loader_make_monetary_value (DB_CURRENCY_POLISH_ZLOTY, (yyvsp[(2) - (2)].string));
    
    (yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
  ;}
    break;

  case 112:
#line 901 "../../src/executables/loader_grammar.y"
    {
    LDR_MONETARY_VALUE *mon_value = loader_make_monetary_value (DB_CURRENCY_SWEDISH_KRONA, (yyvsp[(2) - (2)].string));
    
    (yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
  ;}
    break;

  case 113:
#line 908 "../../src/executables/loader_grammar.y"
    {
    LDR_MONETARY_VALUE *mon_value = loader_make_monetary_value (DB_CURRENCY_CROATIAN_KUNA, (yyvsp[(2) - (2)].string));
    
    (yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
  ;}
    break;

  case 114:
#line 915 "../../src/executables/loader_grammar.y"
    {
    LDR_MONETARY_VALUE *mon_value = loader_make_monetary_value (DB_CURRENCY_SERBIAN_DINAR, (yyvsp[(2) - (2)].string));
    
    (yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
  ;}
    break;


/* Line 1267 of yacc.c.  */
#line 2675 "../../src/executables/loader_grammar.c"
      default: break;
    }
  YY_SYMBOL_PRINT ("-> $$ =", yyr1[yyn], &yyval, &yyloc);

  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);

  *++yyvsp = yyval;


  /* Now `shift' the result of the reduction.  Determine what state
     that goes to, based on the state we popped back to and the rule
     number reduced by.  */

  yyn = yyr1[yyn];

  yystate = yypgoto[yyn - YYNTOKENS] + *yyssp;
  if (0 <= yystate && yystate <= YYLAST && yycheck[yystate] == *yyssp)
    yystate = yytable[yystate];
  else
    yystate = yydefgoto[yyn - YYNTOKENS];

  goto yynewstate;


/*------------------------------------.
| yyerrlab -- here on detecting error |
`------------------------------------*/
yyerrlab:
  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
#if ! YYERROR_VERBOSE
      yyerror (YY_("syntax error"));
#else
      {
	YYSIZE_T yysize = yysyntax_error (0, yystate, yychar);
	if (yymsg_alloc < yysize && yymsg_alloc < YYSTACK_ALLOC_MAXIMUM)
	  {
	    YYSIZE_T yyalloc = 2 * yysize;
	    if (! (yysize <= yyalloc && yyalloc <= YYSTACK_ALLOC_MAXIMUM))
	      yyalloc = YYSTACK_ALLOC_MAXIMUM;
	    if (yymsg != yymsgbuf)
	      YYSTACK_FREE (yymsg);
	    yymsg = (char *) YYSTACK_ALLOC (yyalloc);
	    if (yymsg)
	      yymsg_alloc = yyalloc;
	    else
	      {
		yymsg = yymsgbuf;
		yymsg_alloc = sizeof yymsgbuf;
	      }
	  }

	if (0 < yysize && yysize <= yymsg_alloc)
	  {
	    (void) yysyntax_error (yymsg, yystate, yychar);
	    yyerror (yymsg);
	  }
	else
	  {
	    yyerror (YY_("syntax error"));
	    if (yysize != 0)
	      goto yyexhaustedlab;
	  }
      }
#endif
    }



  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse look-ahead token after an
	 error, discard it.  */

      if (yychar <= YYEOF)
	{
	  /* Return failure if at end of input.  */
	  if (yychar == YYEOF)
	    YYABORT;
	}
      else
	{
	  yydestruct ("Error: discarding",
		      yytoken, &yylval);
	  yychar = YYEMPTY;
	}
    }

  /* Else will try to reuse look-ahead token after shifting the error
     token.  */
  goto yyerrlab1;


/*---------------------------------------------------.
| yyerrorlab -- error raised explicitly by YYERROR.  |
`---------------------------------------------------*/
yyerrorlab:

  /* Pacify compilers like GCC when the user code never invokes
     YYERROR and the label yyerrorlab therefore never appears in user
     code.  */
  if (/*CONSTCOND*/ 0)
     goto yyerrorlab;

  /* Do not reclaim the symbols of the rule which action triggered
     this YYERROR.  */
  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);
  yystate = *yyssp;
  goto yyerrlab1;


/*-------------------------------------------------------------.
| yyerrlab1 -- common code for both syntax error and YYERROR.  |
`-------------------------------------------------------------*/
yyerrlab1:
  yyerrstatus = 3;	/* Each real token shifted decrements this.  */

  for (;;)
    {
      yyn = yypact[yystate];
      if (yyn != YYPACT_NINF)
	{
	  yyn += YYTERROR;
	  if (0 <= yyn && yyn <= YYLAST && yycheck[yyn] == YYTERROR)
	    {
	      yyn = yytable[yyn];
	      if (0 < yyn)
		break;
	    }
	}

      /* Pop the current state because it cannot handle the error token.  */
      if (yyssp == yyss)
	YYABORT;


      yydestruct ("Error: popping",
		  yystos[yystate], yyvsp);
      YYPOPSTACK (1);
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  if (yyn == YYFINAL)
    YYACCEPT;

  *++yyvsp = yylval;


  /* Shift the error token.  */
  YY_SYMBOL_PRINT ("Shifting", yystos[yyn], yyvsp, yylsp);

  yystate = yyn;
  goto yynewstate;


/*-------------------------------------.
| yyacceptlab -- YYACCEPT comes here.  |
`-------------------------------------*/
yyacceptlab:
  yyresult = 0;
  goto yyreturn;

/*-----------------------------------.
| yyabortlab -- YYABORT comes here.  |
`-----------------------------------*/
yyabortlab:
  yyresult = 1;
  goto yyreturn;

#ifndef yyoverflow
/*-------------------------------------------------.
| yyexhaustedlab -- memory exhaustion comes here.  |
`-------------------------------------------------*/
yyexhaustedlab:
  yyerror (YY_("memory exhausted"));
  yyresult = 2;
  /* Fall through.  */
#endif

yyreturn:
  if (yychar != YYEOF && yychar != YYEMPTY)
     yydestruct ("Cleanup: discarding lookahead",
		 yytoken, &yylval);
  /* Do not reclaim the symbols of the rule which action triggered
     this YYABORT or YYACCEPT.  */
  YYPOPSTACK (yylen);
  YY_STACK_PRINT (yyss, yyssp);
  while (yyssp != yyss)
    {
      yydestruct ("Cleanup: popping",
		  yystos[*yyssp], yyvsp);
      YYPOPSTACK (1);
    }
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif
#if YYERROR_VERBOSE
  if (yymsg != yymsgbuf)
    YYSTACK_FREE (yymsg);
#endif
  /* Make sure YYID is used.  */
  return YYID (yyresult);
}


#line 921 "../../src/executables/loader_grammar.y"


static LDR_STRING *
loader_append_string_list (LDR_STRING * head, LDR_STRING * tail)
{
  tail->next = NULL;
  tail->last = NULL;

  if (head)
    {
      head->last->next = tail;
    }
  else
    {
      head = tail;
    }

  head->last = tail;
  return head;
}

static LDR_CLASS_COMMAND_SPEC *
loader_make_class_command_spec (int qualifier, LDR_STRING * attr_list,
			        LDR_CONSTRUCTOR_SPEC * ctor_spec)
{
  LDR_CLASS_COMMAND_SPEC *spec;

  spec = (LDR_CLASS_COMMAND_SPEC *) malloc (sizeof (LDR_CLASS_COMMAND_SPEC));
  if (spec == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (LDR_CLASS_COMMAND_SPEC));
      return NULL;
    }

  spec->qualifier = qualifier;
  spec->attr_list = attr_list;
  spec->ctor_spec = ctor_spec;

  return spec;
}

static LDR_CONSTANT *
loader_make_constant (int type, void *val)
{
  LDR_CONSTANT *con;

  if (constant_Pool_idx < CONSTANT_POOL_SIZE)
    {
      con = &(constant_Pool[constant_Pool_idx]);
      constant_Pool_idx++;
      con->need_free = false;
    }
  else
    {
      con = (LDR_CONSTANT *) malloc (sizeof (LDR_CONSTANT));
      if (con == NULL)
	{
          er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	          ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (LDR_CONSTANT));
	  return NULL;
	}
      con->need_free = true;
    }

  con->type = type;
  con->val = val;

  return con;
}

static LDR_MONETARY_VALUE *
loader_make_monetary_value (int currency_type, LDR_STRING * amount)
{
  LDR_MONETARY_VALUE * mon_value = NULL;

  mon_value = (LDR_MONETARY_VALUE *) malloc (sizeof (LDR_MONETARY_VALUE));
  if (mon_value == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (LDR_MONETARY_VALUE));
      return NULL;
    }

  mon_value->amount = amount;
  mon_value->currency_type = currency_type;

  return mon_value;
}

static LDR_CONSTANT *
loader_append_constant_list (LDR_CONSTANT * head, LDR_CONSTANT * tail)
{
  tail->next = NULL;
  tail->last = NULL;

  if (head)
    {
      head->last->next = tail;
    }
  else
    {
      head = tail;
    }

  head->last = tail;
  return head;
}

void do_loader_parse(FILE *fp)
{
  loader_In_instance_line = true;

  loader_yyin = fp;
  loader_yyline = 1;
  loader_yyparse();
}

#ifdef PARSER_DEBUG
/*int main(int argc, char *argv[])
{
	loader_yyparse();
	return 0;
}
*/
#endif

