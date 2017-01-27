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
enum yytokentype
{
  NL = 258,
  NULL_ = 259,
  CLASS = 260,
  SHARED = 261,
  DEFAULT = 262,
  DATE_ = 263,
  TIME = 264,
  UTIME = 265,
  TIMESTAMP = 266,
  TIMESTAMPLTZ = 267,
  TIMESTAMPTZ = 268,
  DATETIME = 269,
  DATETIMELTZ = 270,
  DATETIMETZ = 271,
  CMD_ID = 272,
  CMD_CLASS = 273,
  CMD_CONSTRUCTOR = 274,
  REF_ELO_INT = 275,
  REF_ELO_EXT = 276,
  REF_USER = 277,
  REF_CLASS = 278,
  OBJECT_REFERENCE = 279,
  OID_DELIMETER = 280,
  SET_START_BRACE = 281,
  SET_END_BRACE = 282,
  START_PAREN = 283,
  END_PAREN = 284,
  REAL_LIT = 285,
  INT_LIT = 286,
  OID_ = 287,
  TIME_LIT4 = 288,
  TIME_LIT42 = 289,
  TIME_LIT3 = 290,
  TIME_LIT31 = 291,
  TIME_LIT2 = 292,
  TIME_LIT1 = 293,
  DATE_LIT2 = 294,
  YEN_SYMBOL = 295,
  WON_SYMBOL = 296,
  BACKSLASH = 297,
  DOLLAR_SYMBOL = 298,
  TURKISH_LIRA_CURRENCY = 299,
  BRITISH_POUND_SYMBOL = 300,
  CAMBODIAN_RIEL_SYMBOL = 301,
  CHINESE_RENMINBI_SYMBOL = 302,
  INDIAN_RUPEE_SYMBOL = 303,
  RUSSIAN_RUBLE_SYMBOL = 304,
  AUSTRALIAN_DOLLAR_SYMBOL = 305,
  CANADIAN_DOLLAR_SYMBOL = 306,
  BRASILIAN_REAL_SYMBOL = 307,
  ROMANIAN_LEU_SYMBOL = 308,
  EURO_SYMBOL = 309,
  SWISS_FRANC_SYMBOL = 310,
  DANISH_KRONE_SYMBOL = 311,
  NORWEGIAN_KRONE_SYMBOL = 312,
  BULGARIAN_LEV_SYMBOL = 313,
  VIETNAMESE_DONG_SYMBOL = 314,
  CZECH_KORUNA_SYMBOL = 315,
  POLISH_ZLOTY_SYMBOL = 316,
  SWEDISH_KRONA_SYMBOL = 317,
  CROATIAN_KUNA_SYMBOL = 318,
  SERBIAN_DINAR_SYMBOL = 319,
  IDENTIFIER = 320,
  Quote = 321,
  DQuote = 322,
  NQuote = 323,
  BQuote = 324,
  XQuote = 325,
  SQS_String_Body = 326,
  DQS_String_Body = 327,
  COMMA = 328
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
#define TIMESTAMPLTZ 267
#define TIMESTAMPTZ 268
#define DATETIME 269
#define DATETIMELTZ 270
#define DATETIMETZ 271
#define CMD_ID 272
#define CMD_CLASS 273
#define CMD_CONSTRUCTOR 274
#define REF_ELO_INT 275
#define REF_ELO_EXT 276
#define REF_USER 277
#define REF_CLASS 278
#define OBJECT_REFERENCE 279
#define OID_DELIMETER 280
#define SET_START_BRACE 281
#define SET_END_BRACE 282
#define START_PAREN 283
#define END_PAREN 284
#define REAL_LIT 285
#define INT_LIT 286
#define OID_ 287
#define TIME_LIT4 288
#define TIME_LIT42 289
#define TIME_LIT3 290
#define TIME_LIT31 291
#define TIME_LIT2 292
#define TIME_LIT1 293
#define DATE_LIT2 294
#define YEN_SYMBOL 295
#define WON_SYMBOL 296
#define BACKSLASH 297
#define DOLLAR_SYMBOL 298
#define TURKISH_LIRA_CURRENCY 299
#define BRITISH_POUND_SYMBOL 300
#define CAMBODIAN_RIEL_SYMBOL 301
#define CHINESE_RENMINBI_SYMBOL 302
#define INDIAN_RUPEE_SYMBOL 303
#define RUSSIAN_RUBLE_SYMBOL 304
#define AUSTRALIAN_DOLLAR_SYMBOL 305
#define CANADIAN_DOLLAR_SYMBOL 306
#define BRASILIAN_REAL_SYMBOL 307
#define ROMANIAN_LEU_SYMBOL 308
#define EURO_SYMBOL 309
#define SWISS_FRANC_SYMBOL 310
#define DANISH_KRONE_SYMBOL 311
#define NORWEGIAN_KRONE_SYMBOL 312
#define BULGARIAN_LEV_SYMBOL 313
#define VIETNAMESE_DONG_SYMBOL 314
#define CZECH_KORUNA_SYMBOL 315
#define POLISH_ZLOTY_SYMBOL 316
#define SWEDISH_KRONA_SYMBOL 317
#define CROATIAN_KUNA_SYMBOL 318
#define SERBIAN_DINAR_SYMBOL 319
#define IDENTIFIER 320
#define Quote 321
#define DQuote 322
#define NQuote 323
#define BQuote 324
#define XQuote 325
#define SQS_String_Body 326
#define DQS_String_Body 327
#define COMMA 328




/* Copy the first part of user declarations.  */


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

extern int loader_yylex (void);
extern void loader_yyerror (char *s);
extern void loader_reset_string_pool (void);
extern void loader_initialize_lexer (void);
extern void do_loader_parse (FILE * fp);

static LDR_CONSTANT constant_Pool[CONSTANT_POOL_SIZE];
static int constant_Pool_idx = 0;

static LDR_STRING *loader_append_string_list (LDR_STRING * head, LDR_STRING * str);
static LDR_CLASS_COMMAND_SPEC *loader_make_class_command_spec (int qualifier, LDR_STRING * attr_list,
							       LDR_CONSTRUCTOR_SPEC * ctor_spec);
static LDR_CONSTANT *loader_make_constant (int type, void *val);
static LDR_MONETARY_VALUE *loader_make_monetary_value (int currency_type, LDR_STRING * amount);
static LDR_CONSTANT *loader_append_constant_list (LDR_CONSTANT * head, LDR_CONSTANT * tail);

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
{
  int intval;
  LDR_STRING *string;
  LDR_CLASS_COMMAND_SPEC *cmd_spec;
  LDR_CONSTRUCTOR_SPEC *ctor_spec;
  LDR_CONSTANT *constant;
  LDR_OBJECT_REF *obj_ref;
}
/* Line 193 of yacc.c.  */

YYSTYPE;
# define yystype YYSTYPE	/* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
# define YYSTYPE_IS_TRIVIAL 1
#endif



/* Copy the second part of user declarations.  */


/* Line 216 of yacc.c.  */


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
#  include <stddef.h>		/* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# else
#  define YYSIZE_T unsigned int
# endif
#endif

#define YYSIZE_MAXIMUM ((YYSIZE_T) -1)

#ifndef YY_
# if YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h>		/* INFRINGES ON USER NAME SPACE */
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
# define YYUSE(e)		/* empty */
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
#    include <alloca.h>		/* INFRINGES ON USER NAME SPACE */
#   elif defined _AIX
#    define YYSTACK_ALLOC __alloca
#   elif defined _MSC_VER
#    include <malloc.h>		/* INFRINGES ON USER NAME SPACE */
#    define alloca _alloca
#   else
#    define YYSTACK_ALLOC alloca
#    if ! defined _ALLOCA_H && ! defined _STDLIB_H && (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
#     include <stdlib.h>	/* INFRINGES ON USER NAME SPACE */
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
#   define YYSTACK_ALLOC_MAXIMUM 4032	/* reasonable circa 2006 */
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
#   include <stdlib.h>		/* INFRINGES ON USER NAME SPACE */
#   ifndef _STDLIB_H
#    define _STDLIB_H 1
#   endif
#  endif
#  ifndef YYMALLOC
#   define YYMALLOC malloc
#   if ! defined malloc && ! defined _STDLIB_H && (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
void *malloc (YYSIZE_T);	/* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifndef YYFREE
#   define YYFREE free
#   if ! defined free && ! defined _STDLIB_H && (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
void free (void *);		/* INFRINGES ON USER NAME SPACE */
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
#define YYLAST   407

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  74
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  43
/* YYNRULES -- Number of rules.  */
#define YYNRULES  122
/* YYNRULES -- Number of states.  */
#define YYNSTATES  195

/* YYTRANSLATE(YYLEX) -- Bison symbol number corresponding to YYLEX.  */
#define YYUNDEFTOK  2
#define YYMAXUTOK   328

#define YYTRANSLATE(YYX)						\
  ((unsigned int) (YYX) <= YYMAXUTOK ? yytranslate[YYX] : YYUNDEFTOK)

/* YYTRANSLATE[YYLEX] -- Bison symbol number corresponding to YYLEX.  */
static const yytype_uint8 yytranslate[] = {
  0, 2, 2, 2, 2, 2, 2, 2, 2, 2,
  2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
  2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
  2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
  2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
  2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
  2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
  2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
  2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
  2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
  2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
  2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
  2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
  2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
  2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
  2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
  2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
  2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
  2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
  2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
  2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
  2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
  2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
  2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
  2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
  2, 2, 2, 2, 2, 2, 1, 2, 3, 4,
  5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
  15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
  25, 26, 27, 28, 29, 30, 31, 32, 33, 34,
  35, 36, 37, 38, 39, 40, 41, 42, 43, 44,
  45, 46, 47, 48, 49, 50, 51, 52, 53, 54,
  55, 56, 57, 58, 59, 60, 61, 62, 63, 64,
  65, 66, 67, 68, 69, 70, 71, 72, 73
};

#if YYDEBUG
/* YYPRHS[YYN] -- Index of the first RHS symbol of rule number YYN in
   YYRHS.  */
static const yytype_uint16 yyprhs[] = {
  0, 0, 3, 4, 7, 9, 12, 15, 17, 19,
  21, 23, 25, 29, 33, 35, 38, 41, 45, 47,
  49, 51, 54, 58, 60, 63, 67, 69, 73, 76,
  80, 82, 85, 89, 91, 93, 96, 98, 100, 102,
  105, 107, 109, 111, 113, 115, 117, 119, 121, 123,
  125, 127, 129, 131, 133, 135, 137, 139, 141, 143,
  145, 147, 149, 151, 153, 155, 157, 159, 162, 165,
  168, 172, 176, 180, 184, 188, 192, 196, 200, 204,
  207, 210, 213, 217, 219, 221, 224, 227, 231, 233,
  236, 240, 244, 249, 253, 255, 257, 259, 261, 264,
  267, 270, 273, 276, 279, 282, 285, 288, 291, 294,
  297, 300, 303, 306, 309, 312, 315, 318, 321, 324,
  327, 330, 333
};

/* YYRHS -- A `-1'-separated list of the rules' RHS.  */
static const yytype_int8 yyrhs[] = {
  75, 0, -1, -1, 76, 77, -1, 78, -1, 77,
  78, -1, 79, 3, -1, 3, -1, 80, -1, 92,
  -1, 82, -1, 81, -1, 17, 65, 31, -1, 18,
  65, 83, -1, 85, -1, 85, 88, -1, 84, 85,
  -1, 84, 85, 88, -1, 5, -1, 6, -1, 7,
  -1, 28, 29, -1, 28, 86, 29, -1, 87, -1,
  86, 87, -1, 86, 73, 87, -1, 65, -1, 19,
  65, 89, -1, 28, 29, -1, 28, 90, 29, -1,
  91, -1, 90, 91, -1, 90, 73, 91, -1, 65,
  -1, 93, -1, 93, 94, -1, 94, -1, 32, -1,
  95, -1, 94, 95, -1, 96, -1, 98, -1, 97,
  -1, 108, -1, 99, -1, 100, -1, 101, -1, 102,
  -1, 103, -1, 104, -1, 105, -1, 106, -1, 107,
  -1, 4, -1, 33, -1, 34, -1, 35, -1, 36,
  -1, 37, -1, 38, -1, 31, -1, 30, -1, 39,
  -1, 116, -1, 109, -1, 112, -1, 114, -1, 66,
  71, -1, 68, 71, -1, 67, 72, -1, 8, 66,
  71, -1, 9, 66, 71, -1, 11, 66, 71, -1,
  12, 66, 71, -1, 13, 66, 71, -1, 10, 66,
  71, -1, 14, 66, 71, -1, 15, 66, 71, -1,
  16, 66, 71, -1, 69, 71, -1, 70, 71, -1,
  24, 110, -1, 24, 110, 111, -1, 31, -1, 65,
  -1, 25, 31, -1, 26, 27, -1, 26, 113, 27,
  -1, 95, -1, 113, 95, -1, 113, 73, 95, -1,
  113, 3, 95, -1, 113, 73, 3, 95, -1, 115,
  66, 71, -1, 20, -1, 21, -1, 22, -1, 23,
  -1, 43, 30, -1, 40, 30, -1, 41, 30, -1,
  44, 30, -1, 42, 30, -1, 45, 30, -1, 46,
  30, -1, 47, 30, -1, 48, 30, -1, 49, 30,
  -1, 50, 30, -1, 51, 30, -1, 52, 30, -1,
  53, 30, -1, 54, 30, -1, 55, 30, -1, 56,
  30, -1, 57, 30, -1, 58, 30, -1, 59, 30,
  -1, 60, 30, -1, 61, 30, -1, 62, 30, -1,
  63, 30, -1, 64, 30, -1
};

/* YYRLINE[YYN] -- source line where rule number YYN was defined.  */
static const yytype_uint16 yyrline[] = {
  0, 202, 202, 202, 213, 218, 225, 231, 238, 245,
  255, 260, 267, 280, 339, 345, 351, 357, 365, 371,
  377, 385, 390, 397, 403, 409, 417, 424, 443, 448,
  455, 461, 467, 475, 482, 488, 495, 504, 511, 517,
  525, 526, 527, 528, 529, 530, 531, 532, 533, 534,
  535, 536, 537, 538, 539, 540, 541, 542, 543, 544,
  545, 546, 561, 562, 563, 564, 565, 569, 576, 583,
  590, 597, 604, 611, 618, 625, 632, 639, 646, 654,
  659, 666, 671, 679, 698, 719, 726, 731, 738, 744,
  750, 756, 762, 770, 777, 779, 781, 783, 787, 794,
  801, 808, 815, 822, 829, 836, 843, 850, 857, 864,
  871, 878, 885, 892, 899, 906, 913, 920, 927, 934,
  941, 948, 955
};
#endif

#if YYDEBUG || YYERROR_VERBOSE || YYTOKEN_TABLE
/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] = {
  "$end", "error", "$undefined", "NL", "NULL_", "CLASS", "SHARED",
  "DEFAULT", "DATE_", "TIME", "UTIME", "TIMESTAMP", "TIMESTAMPLTZ",
  "TIMESTAMPTZ", "DATETIME", "DATETIMELTZ", "DATETIMETZ", "CMD_ID",
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
  "sql2_date", "sql2_time", "sql2_timestamp", "sql2_timestampltz",
  "sql2_timestamptz", "utime", "sql2_datetime", "sql2_datetimeltz",
  "sql2_datetimetz", "bit_string", "object_reference", "class_identifier",
  "instance_number", "set_constant", "set_elements",
  "system_object_reference", "ref_type", "monetary", 0
};
#endif

# ifdef YYPRINT
/* YYTOKNUM[YYLEX-NUM] -- Internal token number corresponding to
   token YYLEX-NUM.  */
static const yytype_uint16 yytoknum[] = {
  0, 256, 257, 258, 259, 260, 261, 262, 263, 264,
  265, 266, 267, 268, 269, 270, 271, 272, 273, 274,
  275, 276, 277, 278, 279, 280, 281, 282, 283, 284,
  285, 286, 287, 288, 289, 290, 291, 292, 293, 294,
  295, 296, 297, 298, 299, 300, 301, 302, 303, 304,
  305, 306, 307, 308, 309, 310, 311, 312, 313, 314,
  315, 316, 317, 318, 319, 320, 321, 322, 323, 324,
  325, 326, 327, 328
};
# endif

/* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const yytype_uint8 yyr1[] = {
  0, 74, 76, 75, 77, 77, 78, 78, 79, 79,
  80, 80, 81, 82, 83, 83, 83, 83, 84, 84,
  84, 85, 85, 86, 86, 86, 87, 88, 89, 89,
  90, 90, 90, 91, 92, 92, 92, 93, 94, 94,
  95, 95, 95, 95, 95, 95, 95, 95, 95, 95,
  95, 95, 95, 95, 95, 95, 95, 95, 95, 95,
  95, 95, 95, 95, 95, 95, 95, 96, 97, 98,
  99, 100, 101, 102, 103, 104, 105, 106, 107, 108,
  108, 109, 109, 110, 110, 111, 112, 112, 113, 113,
  113, 113, 113, 114, 115, 115, 115, 115, 116, 116,
  116, 116, 116, 116, 116, 116, 116, 116, 116, 116,
  116, 116, 116, 116, 116, 116, 116, 116, 116, 116,
  116, 116, 116
};

/* YYR2[YYN] -- Number of symbols composing right hand side of rule YYN.  */
static const yytype_uint8 yyr2[] = {
  0, 2, 0, 2, 1, 2, 2, 1, 1, 1,
  1, 1, 3, 3, 1, 2, 2, 3, 1, 1,
  1, 2, 3, 1, 2, 3, 1, 3, 2, 3,
  1, 2, 3, 1, 1, 2, 1, 1, 1, 2,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 2, 2, 2,
  3, 3, 3, 3, 3, 3, 3, 3, 3, 2,
  2, 2, 3, 1, 1, 2, 2, 3, 1, 2,
  3, 3, 4, 3, 1, 1, 1, 1, 2, 2,
  2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
  2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
  2, 2, 2
};

/* YYDEFACT[STATE-NAME] -- Default rule to reduce with in state
   STATE-NUM when YYTABLE doesn't specify something else to do.  Zero
   means the default is an error.  */
static const yytype_uint8 yydefact[] = {
  2, 0, 0, 1, 7, 53, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 94, 95, 96,
  97, 0, 0, 61, 60, 37, 54, 55, 56, 57,
  58, 59, 62, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 3, 4, 0, 8, 11, 10, 9,
  34, 36, 38, 40, 42, 41, 44, 45, 46, 47,
  48, 49, 50, 51, 52, 43, 64, 65, 66, 0,
  63, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 83, 84, 81, 86, 88, 0, 99, 100,
  102, 98, 101, 103, 104, 105, 106, 107, 108, 109,
  110, 111, 112, 113, 114, 115, 116, 117, 118, 119,
  120, 121, 122, 67, 69, 68, 79, 80, 5, 6,
  35, 39, 0, 70, 71, 75, 72, 73, 74, 76,
  77, 78, 12, 18, 19, 20, 0, 13, 0, 14,
  0, 82, 0, 87, 0, 89, 93, 21, 26, 0,
  23, 16, 0, 15, 85, 91, 0, 90, 22, 0,
  24, 17, 0, 92, 25, 0, 27, 28, 33, 0,
  30, 29, 0, 31, 32
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] = {
  -1, 1, 2, 63, 64, 65, 66, 67, 68, 157,
  158, 159, 169, 170, 173, 186, 189, 190, 69, 70,
  71, 72, 73, 74, 75, 76, 77, 78, 79, 80,
  81, 82, 83, 84, 85, 86, 104, 161, 87, 107,
  88, 89, 90
};

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
#define YYPACT_NINF -182
static const yytype_int16 yypact[] = {
  -182, 9, 135, -182, -182, -182, -56, -54, -53, -51,
  -50, -48, -47, -46, -45, -43, -40, -182, -182, -182,
  -182, -17, 270, -182, -182, -182, -182, -182, -182, -182,
  -182, -182, -182, -4, 22, 64, 65, 66, 67, 68,
  69, 70, 71, 72, 73, 74, 75, 76, 77, 78,
  79, 80, 81, 82, 83, 84, 85, 86, 21, 45,
  48, 49, 50, 135, -182, 119, -182, -182, -182, -182,
  337, 337, -182, -182, -182, -182, -182, -182, -182, -182,
  -182, -182, -182, -182, -182, -182, -182, -182, -182, 57,
  -182, 53, 54, 55, 58, 59, 60, 61, 63, 89,
  97, -1, -182, -182, 108, -182, -182, 20, -182, -182,
  -182, -182, -182, -182, -182, -182, -182, -182, -182, -182,
  -182, -182, -182, -182, -182, -182, -182, -182, -182, -182,
  -182, -182, -182, -182, -182, -182, -182, -182, -182, -182,
  337, -182, 91, -182, -182, -182, -182, -182, -182, -182,
  -182, -182, -182, -182, -182, -182, -27, -182, 109, 117,
  110, -182, 337, -182, 203, -182, -182, -182, -182, -28,
  -182, 117, 98, -182, -182, -182, 337, -182, -182, 99,
  -182, -182, 172, -182, -182, -26, -182, -182, -182, 62,
  -182, -182, 143, -182, -182
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] = {
  -182, -182, -182, -182, 146, -182, -182, -182, -182, -182,
  -182, 52, -182, -162, 51, -182, -182, -181, -182, -182,
  150, -22, -182, -182, -182, -182, -182, -182, -182, -182,
  -182, -182, -182, -182, -182, -182, -182, -182, -182, -182,
  -182, -182, -182
};

/* YYTABLE[YYPACT[STATE-NUM]].  What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule which
   number is the opposite.  If zero, do what YYDEFACT says.
   If YYTABLE_NINF, syntax error.  */
#define YYTABLE_NINF -1
static const yytype_uint8 yytable[] = {
  106, 178, 167, 187, 153, 154, 155, 180, 193, 3,
  91, 194, 92, 93, 102, 94, 95, 184, 96, 97,
  98, 99, 100, 162, 5, 101, 108, 156, 6, 7,
  8, 9, 10, 11, 12, 13, 14, 168, 168, 188,
  17, 18, 19, 20, 21, 179, 22, 163, 103, 141,
  23, 24, 109, 26, 27, 28, 29, 30, 31, 32,
  33, 34, 35, 36, 37, 38, 39, 40, 41, 42,
  43, 44, 45, 46, 47, 48, 49, 50, 51, 52,
  53, 54, 55, 56, 57, 165, 58, 59, 60, 61,
  62, 191, 133, 164, 110, 111, 112, 113, 114, 115,
  116, 117, 118, 119, 120, 121, 122, 123, 124, 125,
  126, 127, 128, 129, 130, 131, 132, 134, 141, 135,
  136, 137, 139, 142, 143, 144, 145, 188, 152, 146,
  147, 148, 149, 160, 150, 192, 172, 156, 4, 5,
  175, 174, 177, 6, 7, 8, 9, 10, 11, 12,
  13, 14, 15, 16, 183, 17, 18, 19, 20, 21,
  151, 22, 166, 182, 168, 23, 24, 25, 26, 27,
  28, 29, 30, 31, 32, 33, 34, 35, 36, 37,
  38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
  48, 49, 50, 51, 52, 53, 54, 55, 56, 57,
  185, 58, 59, 60, 61, 62, 176, 5, 188, 138,
  171, 6, 7, 8, 9, 10, 11, 12, 13, 14,
  140, 0, 181, 17, 18, 19, 20, 21, 0, 22,
  0, 0, 0, 23, 24, 0, 26, 27, 28, 29,
  30, 31, 32, 33, 34, 35, 36, 37, 38, 39,
  40, 41, 42, 43, 44, 45, 46, 47, 48, 49,
  50, 51, 52, 53, 54, 55, 56, 57, 0, 58,
  59, 60, 61, 62, 5, 0, 0, 0, 6, 7,
  8, 9, 10, 11, 12, 13, 14, 0, 0, 0,
  17, 18, 19, 20, 21, 0, 22, 105, 0, 0,
  23, 24, 0, 26, 27, 28, 29, 30, 31, 32,
  33, 34, 35, 36, 37, 38, 39, 40, 41, 42,
  43, 44, 45, 46, 47, 48, 49, 50, 51, 52,
  53, 54, 55, 56, 57, 0, 58, 59, 60, 61,
  62, 5, 0, 0, 0, 6, 7, 8, 9, 10,
  11, 12, 13, 14, 0, 0, 0, 17, 18, 19,
  20, 21, 0, 22, 0, 0, 0, 23, 24, 0,
  26, 27, 28, 29, 30, 31, 32, 33, 34, 35,
  36, 37, 38, 39, 40, 41, 42, 43, 44, 45,
  46, 47, 48, 49, 50, 51, 52, 53, 54, 55,
  56, 57, 0, 58, 59, 60, 61, 62
};

static const yytype_int16 yycheck[] = {
  22, 29, 29, 29, 5, 6, 7, 169, 189, 0,
  66, 192, 66, 66, 31, 66, 66, 179, 66, 66,
  66, 66, 65, 3, 4, 65, 30, 28, 8, 9,
  10, 11, 12, 13, 14, 15, 16, 65, 65, 65,
  20, 21, 22, 23, 24, 73, 26, 27, 65, 71,
  30, 31, 30, 33, 34, 35, 36, 37, 38, 39,
  40, 41, 42, 43, 44, 45, 46, 47, 48, 49,
  50, 51, 52, 53, 54, 55, 56, 57, 58, 59,
  60, 61, 62, 63, 64, 107, 66, 67, 68, 69,
  70, 29, 71, 73, 30, 30, 30, 30, 30, 30,
  30, 30, 30, 30, 30, 30, 30, 30, 30, 30,
  30, 30, 30, 30, 30, 30, 30, 72, 140, 71,
  71, 71, 3, 66, 71, 71, 71, 65, 31, 71,
  71, 71, 71, 25, 71, 73, 19, 28, 3, 4,
  162, 31, 164, 8, 9, 10, 11, 12, 13, 14,
  15, 16, 17, 18, 176, 20, 21, 22, 23, 24,
  71, 26, 71, 65, 65, 30, 31, 32, 33, 34,
  35, 36, 37, 38, 39, 40, 41, 42, 43, 44,
  45, 46, 47, 48, 49, 50, 51, 52, 53, 54,
  55, 56, 57, 58, 59, 60, 61, 62, 63, 64,
  28, 66, 67, 68, 69, 70, 3, 4, 65, 63,
  158, 8, 9, 10, 11, 12, 13, 14, 15, 16,
  70, -1, 171, 20, 21, 22, 23, 24, -1, 26,
  -1, -1, -1, 30, 31, -1, 33, 34, 35, 36,
  37, 38, 39, 40, 41, 42, 43, 44, 45, 46,
  47, 48, 49, 50, 51, 52, 53, 54, 55, 56,
  57, 58, 59, 60, 61, 62, 63, 64, -1, 66,
  67, 68, 69, 70, 4, -1, -1, -1, 8, 9,
  10, 11, 12, 13, 14, 15, 16, -1, -1, -1,
  20, 21, 22, 23, 24, -1, 26, 27, -1, -1,
  30, 31, -1, 33, 34, 35, 36, 37, 38, 39,
  40, 41, 42, 43, 44, 45, 46, 47, 48, 49,
  50, 51, 52, 53, 54, 55, 56, 57, 58, 59,
  60, 61, 62, 63, 64, -1, 66, 67, 68, 69,
  70, 4, -1, -1, -1, 8, 9, 10, 11, 12,
  13, 14, 15, 16, -1, -1, -1, 20, 21, 22,
  23, 24, -1, 26, -1, -1, -1, 30, 31, -1,
  33, 34, 35, 36, 37, 38, 39, 40, 41, 42,
  43, 44, 45, 46, 47, 48, 49, 50, 51, 52,
  53, 54, 55, 56, 57, 58, 59, 60, 61, 62,
  63, 64, -1, 66, 67, 68, 69, 70
};

/* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
   symbol of state STATE-NUM.  */
static const yytype_uint8 yystos[] = {
  0, 75, 76, 0, 3, 4, 8, 9, 10, 11,
  12, 13, 14, 15, 16, 17, 18, 20, 21, 22,
  23, 24, 26, 30, 31, 32, 33, 34, 35, 36,
  37, 38, 39, 40, 41, 42, 43, 44, 45, 46,
  47, 48, 49, 50, 51, 52, 53, 54, 55, 56,
  57, 58, 59, 60, 61, 62, 63, 64, 66, 67,
  68, 69, 70, 77, 78, 79, 80, 81, 82, 92,
  93, 94, 95, 96, 97, 98, 99, 100, 101, 102,
  103, 104, 105, 106, 107, 108, 109, 112, 114, 115,
  116, 66, 66, 66, 66, 66, 66, 66, 66, 66,
  65, 65, 31, 65, 110, 27, 95, 113, 30, 30,
  30, 30, 30, 30, 30, 30, 30, 30, 30, 30,
  30, 30, 30, 30, 30, 30, 30, 30, 30, 30,
  30, 30, 30, 71, 72, 71, 71, 71, 78, 3,
  94, 95, 66, 71, 71, 71, 71, 71, 71, 71,
  71, 71, 31, 5, 6, 7, 28, 83, 84, 85,
  25, 111, 3, 27, 73, 95, 71, 29, 65, 86,
  87, 85, 19, 88, 31, 95, 3, 95, 29, 73,
  87, 88, 65, 95, 87, 28, 89, 29, 65, 90,
  91, 29, 73, 91, 91
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
#  include <stdio.h>		/* INFRINGES ON USER NAME SPACE */
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
yy_symbol_value_print (FILE * yyoutput, int yytype, YYSTYPE const *const yyvaluep)
#else
  static void
yy_symbol_value_print (yyoutput, yytype, yyvaluep)
     FILE *yyoutput;
     int yytype;
     YYSTYPE const *const yyvaluep;
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
yy_symbol_print (FILE * yyoutput, int yytype, YYSTYPE const *const yyvaluep)
#else
static void
yy_symbol_print (yyoutput, yytype, yyvaluep)
     FILE *yyoutput;
     int yytype;
     YYSTYPE const *const yyvaluep;
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
yy_stack_print (yytype_int16 * bottom, yytype_int16 * top)
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
yy_reduce_print (YYSTYPE * yyvsp, int yyrule)
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
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %lu):\n", yyrule - 1, yylno);
  /* The symbols being reduced.  */
  for (yyi = 0; yyi < yynrhs; yyi++)
    {
      fprintf (stderr, "   $%d = ", yyi + 1);
      yy_symbol_print (stderr, yyrhs[yyprhs[yyrule] + yyi], &(yyvsp[(yyi + 1) - (yynrhs)]));
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
    do_not_strip_quotes:;
    }

  if (!yyres)
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

  if (!(YYPACT_NINF < yyn && yyn <= YYLAST))
    return 0;
  else
    {
      int yytype = YYTRANSLATE (yychar);
      YYSIZE_T yysize0 = yytnamerr (0, yytname[yytype]);
      YYSIZE_T yysize = yysize0;
      YYSIZE_T yysize1;
      int yysize_overflow = 0;
      enum
      { YYERROR_VERBOSE_ARGS_MAXIMUM = 5 };
      char const *yyarg[YYERROR_VERBOSE_ARGS_MAXIMUM];
      int yyx;

# if 0
      /* This is so xgettext sees the translatable formats that are
         constructed on the fly.  */
      YY_ ("syntax error, unexpected %s");
      YY_ ("syntax error, unexpected %s, expecting %s");
      YY_ ("syntax error, unexpected %s, expecting %s or %s");
      YY_ ("syntax error, unexpected %s, expecting %s or %s or %s");
      YY_ ("syntax error, unexpected %s, expecting %s or %s or %s or %s");
# endif
      char *yyfmt;
      char const *yyf;
      static char const yyunexpected[] = "syntax error, unexpected %s";
      static char const yyexpecting[] = ", expecting %s";
      static char const yyor[] = " or %s";
      char yyformat[sizeof yyunexpected
		    + sizeof yyexpecting - 1 + ((YYERROR_VERBOSE_ARGS_MAXIMUM - 2) * (sizeof yyor - 1))];
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

      yyf = YY_ (yyformat);
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
yydestruct (const char *yymsg, int yytype, YYSTYPE * yyvaluep)
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
	yyoverflow (YY_ ("memory exhausted"),
		    &yyss1, yysize * sizeof (*yyssp), &yyvs1, yysize * sizeof (*yyvsp), &yystacksize);

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
	union yyalloc *yyptr = (union yyalloc *) YYSTACK_ALLOC (YYSTACK_BYTES (yystacksize));
	if (!yyptr)
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


      YYDPRINTF ((stderr, "Stack size increased to %lu\n", (unsigned long int) yystacksize));

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
  yyval = yyvsp[1 - yylen];


  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
    case 2:

      {
	loader_initialize_lexer ();
	constant_Pool_idx = 0;
	;
      }
      break;

    case 3:

      {
	ldr_act_finish (ldr_Current_context, 0);
	;
      }
      break;

    case 4:

      {
	DBG_PRINT ("line");
	;
      }
      break;

    case 5:

      {
	DBG_PRINT ("line_list line");
	;
      }
      break;

    case 6:

      {
	DBG_PRINT ("one_line");
	loader_In_instance_line = true;
	;
      }
      break;

    case 7:

      {
	loader_In_instance_line = true;
	;
      }
      break;

    case 8:

      {
	DBG_PRINT ("command_line");
	loader_reset_string_pool ();
	constant_Pool_idx = 0;
	;
      }
      break;

    case 9:

      {
	DBG_PRINT ("instance_line");
	ldr_act_finish_line (ldr_Current_context);
	loader_reset_string_pool ();
	constant_Pool_idx = 0;
	;
      }
      break;

    case 10:

      {
	DBG_PRINT ("class_command");
	;
      }
      break;

    case 11:

      {
	DBG_PRINT ("id_command");
	;
      }
      break;

    case 12:

      {
	skip_current_class = false;

	ldr_act_start_id (ldr_Current_context, (yyvsp[(2) - (3)].string)->val);
	ldr_act_set_id (ldr_Current_context, atoi ((yyvsp[(3) - (3)].string)->val));

	FREE_STRING ((yyvsp[(2) - (3)].string));
	FREE_STRING ((yyvsp[(3) - (3)].string));
	;
      }
      break;

    case 13:

      {
	LDR_CLASS_COMMAND_SPEC *cmd_spec;
	LDR_STRING *class_name;
	LDR_STRING *attr, *save, *args;

	DBG_PRINT ("class_commamd_spec");

	class_name = (yyvsp[(2) - (3)].string);
	cmd_spec = (yyvsp[(3) - (3)].cmd_spec);

	ldr_act_set_skipCurrentclass (class_name->val, class_name->size);
	ldr_act_init_context (ldr_Current_context, class_name->val, class_name->size);

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
	    ldr_act_set_constructor (ldr_Current_context, cmd_spec->ctor_spec->idname->val);

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
	;
      }
      break;

    case 14:

      {
	DBG_PRINT ("attribute_list");
	(yyval.cmd_spec) = loader_make_class_command_spec (LDR_ATTRIBUTE_ANY, (yyvsp[(1) - (1)].string), NULL);
	;
      }
      break;

    case 15:

      {
	DBG_PRINT ("attribute_list constructor_spec");
	(yyval.cmd_spec) =
	  loader_make_class_command_spec (LDR_ATTRIBUTE_ANY, (yyvsp[(1) - (2)].string), (yyvsp[(2) - (2)].ctor_spec));
	;
      }
      break;

    case 16:

      {
	DBG_PRINT ("attribute_list_qualifier attribute_list");
	(yyval.cmd_spec) = loader_make_class_command_spec ((yyvsp[(1) - (2)].intval), (yyvsp[(2) - (2)].string), NULL);
	;
      }
      break;

    case 17:

      {
	DBG_PRINT ("attribute_list_qualifier attribute_list constructor_spec");
	(yyval.cmd_spec) =
	  loader_make_class_command_spec ((yyvsp[(1) - (3)].intval), (yyvsp[(2) - (3)].string),
					  (yyvsp[(3) - (3)].ctor_spec));
	;
      }
      break;

    case 18:

      {
	DBG_PRINT ("CLASS");
	(yyval.intval) = LDR_ATTRIBUTE_CLASS;
	;
      }
      break;

    case 19:

      {
	DBG_PRINT ("SHARED");
	(yyval.intval) = LDR_ATTRIBUTE_SHARED;
	;
      }
      break;

    case 20:

      {
	DBG_PRINT ("DEFAULT");
	(yyval.intval) = LDR_ATTRIBUTE_DEFAULT;
	;
      }
      break;

    case 21:

      {
	(yyval.string) = NULL;
	;
      }
      break;

    case 22:

      {
	(yyval.string) = (yyvsp[(2) - (3)].string);
	;
      }
      break;

    case 23:

      {
	DBG_PRINT ("attribute_name");
	(yyval.string) = loader_append_string_list (NULL, (yyvsp[(1) - (1)].string));
	;
      }
      break;

    case 24:

      {
	DBG_PRINT ("attribute_names attribute_name");
	(yyval.string) = loader_append_string_list ((yyvsp[(1) - (2)].string), (yyvsp[(2) - (2)].string));
	;
      }
      break;

    case 25:

      {
	DBG_PRINT ("attribute_names COMMA attribute_name");
	(yyval.string) = loader_append_string_list ((yyvsp[(1) - (3)].string), (yyvsp[(3) - (3)].string));
	;
      }
      break;

    case 26:

      {
	(yyval.string) = (yyvsp[(1) - (1)].string);
	;
      }
      break;

    case 27:

      {
	LDR_CONSTRUCTOR_SPEC *spec;

	spec = (LDR_CONSTRUCTOR_SPEC *) malloc (sizeof (LDR_CONSTRUCTOR_SPEC));
	if (spec == NULL)
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (LDR_CONSTRUCTOR_SPEC));
	    YYABORT;
	  }

	spec->idname = (yyvsp[(2) - (3)].string);
	spec->arg_list = (yyvsp[(3) - (3)].string);
	(yyval.ctor_spec) = spec;
	;
      }
      break;

    case 28:

      {
	(yyval.string) = NULL;
	;
      }
      break;

    case 29:

      {
	(yyval.string) = (yyvsp[(2) - (3)].string);
	;
      }
      break;

    case 30:

      {
	DBG_PRINT ("argument_name");
	(yyval.string) = loader_append_string_list (NULL, (yyvsp[(1) - (1)].string));
	;
      }
      break;

    case 31:

      {
	DBG_PRINT ("argument_names argument_name");
	(yyval.string) = loader_append_string_list ((yyvsp[(1) - (2)].string), (yyvsp[(2) - (2)].string));
	;
      }
      break;

    case 32:

      {
	DBG_PRINT ("argument_names COMMA argument_name");
	(yyval.string) = loader_append_string_list ((yyvsp[(1) - (3)].string), (yyvsp[(3) - (3)].string));
	;
      }
      break;

    case 33:

      {
	(yyval.string) = (yyvsp[(1) - (1)].string);
	;
      }
      break;

    case 34:

      {
	skip_current_instance = false;
	ldr_act_start_instance (ldr_Current_context, (yyvsp[(1) - (1)].intval), NULL);
	;
      }
      break;

    case 35:

      {
	skip_current_instance = false;
	ldr_act_start_instance (ldr_Current_context, (yyvsp[(1) - (2)].intval), (yyvsp[(2) - (2)].constant));
	ldr_process_constants ((yyvsp[(2) - (2)].constant));
	;
      }
      break;

    case 36:

      {
	skip_current_instance = false;
	ldr_act_start_instance (ldr_Current_context, -1, (yyvsp[(1) - (1)].constant));
	ldr_process_constants ((yyvsp[(1) - (1)].constant));
	;
      }
      break;

    case 37:

      {
	(yyval.intval) = (yyvsp[(1) - (1)].intval);
	;
      }
      break;

    case 38:

      {
	DBG_PRINT ("constant");
	(yyval.constant) = loader_append_constant_list (NULL, (yyvsp[(1) - (1)].constant));
	;
      }
      break;

    case 39:

      {
	DBG_PRINT ("constant_list constant");
	(yyval.constant) = loader_append_constant_list ((yyvsp[(1) - (2)].constant), (yyvsp[(2) - (2)].constant));
	;
      }
      break;

    case 40:

      {
	(yyval.constant) = (yyvsp[(1) - (1)].constant);;
      }
      break;

    case 41:

      {
	(yyval.constant) = (yyvsp[(1) - (1)].constant);;
      }
      break;

    case 42:

      {
	(yyval.constant) = (yyvsp[(1) - (1)].constant);;
      }
      break;

    case 43:

      {
	(yyval.constant) = (yyvsp[(1) - (1)].constant);;
      }
      break;

    case 44:

      {
	(yyval.constant) = (yyvsp[(1) - (1)].constant);;
      }
      break;

    case 45:

      {
	(yyval.constant) = (yyvsp[(1) - (1)].constant);;
      }
      break;

    case 46:

      {
	(yyval.constant) = (yyvsp[(1) - (1)].constant);;
      }
      break;

    case 47:

      {
	(yyval.constant) = (yyvsp[(1) - (1)].constant);;
      }
      break;

    case 48:

      {
	(yyval.constant) = (yyvsp[(1) - (1)].constant);;
      }
      break;

    case 49:

      {
	(yyval.constant) = (yyvsp[(1) - (1)].constant);;
      }
      break;

    case 50:

      {
	(yyval.constant) = (yyvsp[(1) - (1)].constant);;
      }
      break;

    case 51:

      {
	(yyval.constant) = (yyvsp[(1) - (1)].constant);;
      }
      break;

    case 52:

      {
	(yyval.constant) = (yyvsp[(1) - (1)].constant);;
      }
      break;

    case 53:

      {
	(yyval.constant) = loader_make_constant (LDR_NULL, NULL);;
      }
      break;

    case 54:

      {
	(yyval.constant) = loader_make_constant (LDR_TIME, (yyvsp[(1) - (1)].string));;
      }
      break;

    case 55:

      {
	(yyval.constant) = loader_make_constant (LDR_TIME, (yyvsp[(1) - (1)].string));;
      }
      break;

    case 56:

      {
	(yyval.constant) = loader_make_constant (LDR_TIME, (yyvsp[(1) - (1)].string));;
      }
      break;

    case 57:

      {
	(yyval.constant) = loader_make_constant (LDR_TIME, (yyvsp[(1) - (1)].string));;
      }
      break;

    case 58:

      {
	(yyval.constant) = loader_make_constant (LDR_TIME, (yyvsp[(1) - (1)].string));;
      }
      break;

    case 59:

      {
	(yyval.constant) = loader_make_constant (LDR_TIME, (yyvsp[(1) - (1)].string));;
      }
      break;

    case 60:

      {
	(yyval.constant) = loader_make_constant (LDR_INT, (yyvsp[(1) - (1)].string));;
      }
      break;

    case 61:

      {
	if (strchr ((yyvsp[(1) - (1)].string)->val, 'F') != NULL
	    || strchr ((yyvsp[(1) - (1)].string)->val, 'f') != NULL)
	  {
	    (yyval.constant) = loader_make_constant (LDR_FLOAT, (yyvsp[(1) - (1)].string));
	  }
	else if (strchr ((yyvsp[(1) - (1)].string)->val, 'E') != NULL
		 || strchr ((yyvsp[(1) - (1)].string)->val, 'e') != NULL)
	  {
	    (yyval.constant) = loader_make_constant (LDR_DOUBLE, (yyvsp[(1) - (1)].string));
	  }
	else
	  {
	    (yyval.constant) = loader_make_constant (LDR_NUMERIC, (yyvsp[(1) - (1)].string));
	  }
	;
      }
      break;

    case 62:

      {
	(yyval.constant) = loader_make_constant (LDR_DATE, (yyvsp[(1) - (1)].string));;
      }
      break;

    case 63:

      {
	(yyval.constant) = (yyvsp[(1) - (1)].constant);;
      }
      break;

    case 64:

      {
	(yyval.constant) = (yyvsp[(1) - (1)].constant);;
      }
      break;

    case 65:

      {
	(yyval.constant) = (yyvsp[(1) - (1)].constant);;
      }
      break;

    case 66:

      {
	(yyval.constant) = (yyvsp[(1) - (1)].constant);;
      }
      break;

    case 67:

      {
	(yyval.constant) = loader_make_constant (LDR_STR, (yyvsp[(2) - (2)].string));
	;
      }
      break;

    case 68:

      {
	(yyval.constant) = loader_make_constant (LDR_NSTR, (yyvsp[(2) - (2)].string));
	;
      }
      break;

    case 69:

      {
	(yyval.constant) = loader_make_constant (LDR_STR, (yyvsp[(2) - (2)].string));
	;
      }
      break;

    case 70:

      {
	(yyval.constant) = loader_make_constant (LDR_DATE, (yyvsp[(3) - (3)].string));
	;
      }
      break;

    case 71:

      {
	(yyval.constant) = loader_make_constant (LDR_TIME, (yyvsp[(3) - (3)].string));
	;
      }
      break;

    case 72:

      {
	(yyval.constant) = loader_make_constant (LDR_TIMESTAMP, (yyvsp[(3) - (3)].string));
	;
      }
      break;

    case 73:

      {
	(yyval.constant) = loader_make_constant (LDR_TIMESTAMPLTZ, (yyvsp[(3) - (3)].string));
	;
      }
      break;

    case 74:

      {
	(yyval.constant) = loader_make_constant (LDR_TIMESTAMPTZ, (yyvsp[(3) - (3)].string));
	;
      }
      break;

    case 75:

      {
	(yyval.constant) = loader_make_constant (LDR_TIMESTAMP, (yyvsp[(3) - (3)].string));
	;
      }
      break;

    case 76:

      {
	(yyval.constant) = loader_make_constant (LDR_DATETIME, (yyvsp[(3) - (3)].string));
	;
      }
      break;

    case 77:

      {
	(yyval.constant) = loader_make_constant (LDR_DATETIMELTZ, (yyvsp[(3) - (3)].string));
	;
      }
      break;

    case 78:

      {
	(yyval.constant) = loader_make_constant (LDR_DATETIMETZ, (yyvsp[(3) - (3)].string));
	;
      }
      break;

    case 79:

      {
	(yyval.constant) = loader_make_constant (LDR_BSTR, (yyvsp[(2) - (2)].string));
	;
      }
      break;

    case 80:

      {
	(yyval.constant) = loader_make_constant (LDR_XSTR, (yyvsp[(2) - (2)].string));
	;
      }
      break;

    case 81:

      {
	(yyval.constant) = loader_make_constant (LDR_CLASS_OID, (yyvsp[(2) - (2)].obj_ref));
	;
      }
      break;

    case 82:

      {
	(yyvsp[(2) - (3)].obj_ref)->instance_number = (yyvsp[(3) - (3)].string);
	(yyval.constant) = loader_make_constant (LDR_OID, (yyvsp[(2) - (3)].obj_ref));
	;
      }
      break;

    case 83:

      {
	LDR_OBJECT_REF *ref;

	ref = (LDR_OBJECT_REF *) malloc (sizeof (LDR_OBJECT_REF));
	if (ref == NULL)
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (LDR_OBJECT_REF));
	    YYABORT;
	  }

	ref->class_id = (yyvsp[(1) - (1)].string);
	ref->class_name = NULL;
	ref->instance_number = NULL;

	(yyval.obj_ref) = ref;
	;
      }
      break;

    case 84:

      {
	LDR_OBJECT_REF *ref;

	ref = (LDR_OBJECT_REF *) malloc (sizeof (LDR_OBJECT_REF));
	if (ref == NULL)
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (LDR_OBJECT_REF));
	    YYABORT;
	  }

	ref->class_id = NULL;
	ref->class_name = (yyvsp[(1) - (1)].string);
	ref->instance_number = NULL;

	(yyval.obj_ref) = ref;
	;
      }
      break;

    case 85:

      {
	(yyval.string) = (yyvsp[(2) - (2)].string);
	;
      }
      break;

    case 86:

      {
	(yyval.constant) = loader_make_constant (LDR_COLLECTION, NULL);
	;
      }
      break;

    case 87:

      {
	(yyval.constant) = loader_make_constant (LDR_COLLECTION, (yyvsp[(2) - (3)].constant));
	;
      }
      break;

    case 88:

      {
	DBG_PRINT ("constant");
	(yyval.constant) = loader_append_constant_list (NULL, (yyvsp[(1) - (1)].constant));
	;
      }
      break;

    case 89:

      {
	DBG_PRINT ("set_elements constant");
	(yyval.constant) = loader_append_constant_list ((yyvsp[(1) - (2)].constant), (yyvsp[(2) - (2)].constant));
	;
      }
      break;

    case 90:

      {
	DBG_PRINT ("set_elements COMMA constant");
	(yyval.constant) = loader_append_constant_list ((yyvsp[(1) - (3)].constant), (yyvsp[(3) - (3)].constant));
	;
      }
      break;

    case 91:

      {
	DBG_PRINT ("set_elements NL constant");
	(yyval.constant) = loader_append_constant_list ((yyvsp[(1) - (3)].constant), (yyvsp[(3) - (3)].constant));
	;
      }
      break;

    case 92:

      {
	DBG_PRINT ("set_elements COMMA NL constant");
	(yyval.constant) = loader_append_constant_list ((yyvsp[(1) - (4)].constant), (yyvsp[(4) - (4)].constant));
	;
      }
      break;

    case 93:

      {
	(yyval.constant) = loader_make_constant ((yyvsp[(1) - (3)].intval), (yyvsp[(3) - (3)].string));
	;
      }
      break;

    case 94:

      {
	(yyval.intval) = LDR_ELO_INT;;
      }
      break;

    case 95:

      {
	(yyval.intval) = LDR_ELO_EXT;;
      }
      break;

    case 96:

      {
	(yyval.intval) = LDR_SYS_USER;;
      }
      break;

    case 97:

      {
	(yyval.intval) = LDR_SYS_CLASS;;
      }
      break;

    case 98:

      {
	LDR_MONETARY_VALUE *mon_value = loader_make_monetary_value (DB_CURRENCY_DOLLAR, (yyvsp[(2) - (2)].string));

	(yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
	;
      }
      break;

    case 99:

      {
	LDR_MONETARY_VALUE *mon_value = loader_make_monetary_value (DB_CURRENCY_YEN, (yyvsp[(2) - (2)].string));

	(yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
	;
      }
      break;

    case 100:

      {
	LDR_MONETARY_VALUE *mon_value = loader_make_monetary_value (DB_CURRENCY_WON, (yyvsp[(2) - (2)].string));

	(yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
	;
      }
      break;

    case 101:

      {
	LDR_MONETARY_VALUE *mon_value = loader_make_monetary_value (DB_CURRENCY_TL, (yyvsp[(2) - (2)].string));

	(yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
	;
      }
      break;

    case 102:

      {
	LDR_MONETARY_VALUE *mon_value = loader_make_monetary_value (DB_CURRENCY_WON, (yyvsp[(2) - (2)].string));

	(yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
	;
      }
      break;

    case 103:

      {
	LDR_MONETARY_VALUE *mon_value =
	  loader_make_monetary_value (DB_CURRENCY_BRITISH_POUND, (yyvsp[(2) - (2)].string));

	(yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
	;
      }
      break;

    case 104:

      {
	LDR_MONETARY_VALUE *mon_value =
	  loader_make_monetary_value (DB_CURRENCY_CAMBODIAN_RIEL, (yyvsp[(2) - (2)].string));

	(yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
	;
      }
      break;

    case 105:

      {
	LDR_MONETARY_VALUE *mon_value =
	  loader_make_monetary_value (DB_CURRENCY_CHINESE_RENMINBI, (yyvsp[(2) - (2)].string));

	(yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
	;
      }
      break;

    case 106:

      {
	LDR_MONETARY_VALUE *mon_value =
	  loader_make_monetary_value (DB_CURRENCY_INDIAN_RUPEE, (yyvsp[(2) - (2)].string));

	(yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
	;
      }
      break;

    case 107:

      {
	LDR_MONETARY_VALUE *mon_value =
	  loader_make_monetary_value (DB_CURRENCY_RUSSIAN_RUBLE, (yyvsp[(2) - (2)].string));

	(yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
	;
      }
      break;

    case 108:

      {
	LDR_MONETARY_VALUE *mon_value =
	  loader_make_monetary_value (DB_CURRENCY_AUSTRALIAN_DOLLAR, (yyvsp[(2) - (2)].string));

	(yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
	;
      }
      break;

    case 109:

      {
	LDR_MONETARY_VALUE *mon_value =
	  loader_make_monetary_value (DB_CURRENCY_CANADIAN_DOLLAR, (yyvsp[(2) - (2)].string));

	(yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
	;
      }
      break;

    case 110:

      {
	LDR_MONETARY_VALUE *mon_value =
	  loader_make_monetary_value (DB_CURRENCY_BRASILIAN_REAL, (yyvsp[(2) - (2)].string));

	(yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
	;
      }
      break;

    case 111:

      {
	LDR_MONETARY_VALUE *mon_value =
	  loader_make_monetary_value (DB_CURRENCY_ROMANIAN_LEU, (yyvsp[(2) - (2)].string));

	(yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
	;
      }
      break;

    case 112:

      {
	LDR_MONETARY_VALUE *mon_value = loader_make_monetary_value (DB_CURRENCY_EURO, (yyvsp[(2) - (2)].string));

	(yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
	;
      }
      break;

    case 113:

      {
	LDR_MONETARY_VALUE *mon_value = loader_make_monetary_value (DB_CURRENCY_SWISS_FRANC, (yyvsp[(2) - (2)].string));

	(yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
	;
      }
      break;

    case 114:

      {
	LDR_MONETARY_VALUE *mon_value =
	  loader_make_monetary_value (DB_CURRENCY_DANISH_KRONE, (yyvsp[(2) - (2)].string));

	(yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
	;
      }
      break;

    case 115:

      {
	LDR_MONETARY_VALUE *mon_value =
	  loader_make_monetary_value (DB_CURRENCY_NORWEGIAN_KRONE, (yyvsp[(2) - (2)].string));

	(yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
	;
      }
      break;

    case 116:

      {
	LDR_MONETARY_VALUE *mon_value =
	  loader_make_monetary_value (DB_CURRENCY_BULGARIAN_LEV, (yyvsp[(2) - (2)].string));

	(yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
	;
      }
      break;

    case 117:

      {
	LDR_MONETARY_VALUE *mon_value =
	  loader_make_monetary_value (DB_CURRENCY_VIETNAMESE_DONG, (yyvsp[(2) - (2)].string));

	(yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
	;
      }
      break;

    case 118:

      {
	LDR_MONETARY_VALUE *mon_value =
	  loader_make_monetary_value (DB_CURRENCY_CZECH_KORUNA, (yyvsp[(2) - (2)].string));

	(yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
	;
      }
      break;

    case 119:

      {
	LDR_MONETARY_VALUE *mon_value =
	  loader_make_monetary_value (DB_CURRENCY_POLISH_ZLOTY, (yyvsp[(2) - (2)].string));

	(yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
	;
      }
      break;

    case 120:

      {
	LDR_MONETARY_VALUE *mon_value =
	  loader_make_monetary_value (DB_CURRENCY_SWEDISH_KRONA, (yyvsp[(2) - (2)].string));

	(yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
	;
      }
      break;

    case 121:

      {
	LDR_MONETARY_VALUE *mon_value =
	  loader_make_monetary_value (DB_CURRENCY_CROATIAN_KUNA, (yyvsp[(2) - (2)].string));

	(yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
	;
      }
      break;

    case 122:

      {
	LDR_MONETARY_VALUE *mon_value =
	  loader_make_monetary_value (DB_CURRENCY_SERBIAN_DINAR, (yyvsp[(2) - (2)].string));

	(yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
	;
      }
      break;


/* Line 1267 of yacc.c.  */

    default:
      break;
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
      yyerror (YY_ ("syntax error"));
#else
      {
	YYSIZE_T yysize = yysyntax_error (0, yystate, yychar);
	if (yymsg_alloc < yysize && yymsg_alloc < YYSTACK_ALLOC_MAXIMUM)
	  {
	    YYSIZE_T yyalloc = 2 * yysize;
	    if (!(yysize <= yyalloc && yyalloc <= YYSTACK_ALLOC_MAXIMUM))
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
	    yyerror (YY_ ("syntax error"));
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
	  yydestruct ("Error: discarding", yytoken, &yylval);
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
  if ( /*CONSTCOND*/ 0)
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
  yyerrstatus = 3;		/* Each real token shifted decrements this.  */

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


      yydestruct ("Error: popping", yystos[yystate], yyvsp);
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
  yyerror (YY_ ("memory exhausted"));
  yyresult = 2;
  /* Fall through.  */
#endif

yyreturn:
  if (yychar != YYEOF && yychar != YYEMPTY)
    yydestruct ("Cleanup: discarding lookahead", yytoken, &yylval);
  /* Do not reclaim the symbols of the rule which action triggered
     this YYABORT or YYACCEPT.  */
  YYPOPSTACK (yylen);
  YY_STACK_PRINT (yyss, yyssp);
  while (yyssp != yyss)
    {
      yydestruct ("Cleanup: popping", yystos[*yyssp], yyvsp);
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
loader_make_class_command_spec (int qualifier, LDR_STRING * attr_list, LDR_CONSTRUCTOR_SPEC * ctor_spec)
{
  LDR_CLASS_COMMAND_SPEC *spec;

  spec = (LDR_CLASS_COMMAND_SPEC *) malloc (sizeof (LDR_CLASS_COMMAND_SPEC));
  if (spec == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (LDR_CLASS_COMMAND_SPEC));
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
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (LDR_CONSTANT));
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
  LDR_MONETARY_VALUE *mon_value = NULL;

  mon_value = (LDR_MONETARY_VALUE *) malloc (sizeof (LDR_MONETARY_VALUE));
  if (mon_value == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (LDR_MONETARY_VALUE));
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

void
do_loader_parse (FILE * fp)
{
  loader_In_instance_line = true;

  loader_yyin = fp;
  loader_yyline = 1;
  loader_yyparse ();
}

#ifdef PARSER_DEBUG
/*int main(int argc, char *argv[])
{
	loader_yyparse();
	return 0;
}
*/
#endif
