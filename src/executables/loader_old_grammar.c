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
     DATE = 263,
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
     IDENTIFIER = 295,
     Quote = 296,
     DQuote = 297,
     NQuote = 298,
     BQuote = 299,
     XQuote = 300,
     SQS_String_Body = 301,
     DQS_String_Body = 302,
     COMMA = 303
   };
#endif
/* Tokens.  */
#define NL 258
#define NULL_ 259
#define CLASS 260
#define SHARED 261
#define DEFAULT 262
#define DATE 263
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
#define IDENTIFIER 295
#define Quote 296
#define DQuote 297
#define NQuote 298
#define BQuote 299
#define XQuote 300
#define SQS_String_Body 301
#define DQS_String_Body 302
#define COMMA 303




/* Copy the first part of user declarations.  */
#line 23 "../../src/executables/loader_old_grammar.y"

#ident "$Id$"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utility.h"
#include "dbtype.h"
#include "language_support.h"
#include "message_catalog.h"
#include "memory_alloc.h"
#include "loader.h"
#include "loader_action.h"

/*#define PARSER_DEBUG*/
#ifdef PARSER_DEBUG
#define DBG_PRINT(s) printf("rule: %s\n", (s));
#else
#define DBG_PRINT(s)
#endif

extern int in_instance_line;
extern FILE *loader_yyin;
extern int loader_yylex(void);
extern void loader_yyerror(char* s);

extern void do_loader_parse(FILE *fp);

static STRING_LIST *append_string_list(STRING_LIST *list, char *str);
static CLASS_COMMAND_SPEC *make_class_command_spec(int qualifier, STRING_LIST *attr_list, CONSTRUCTOR_SPEC *ctor_spec);
static CONSTANT* make_constant(int type, void *val);
static CONSTANT_LIST *append_constant_list(CONSTANT_LIST *list, CONSTANT *cons);
static void process_constant(CONSTANT *c);
static void process_constant_list(CONSTANT_LIST *cons);


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
#line 62 "../../src/executables/loader_old_grammar.y"
{
	int 	intval;
	char	*cptr;
	STRING_LIST	*strlist;
	CLASS_COMMAND_SPEC *cmd_spec;
	CONSTRUCTOR_SPEC *ctor_spec;
	CONSTANT *constant;
	CONSTANT_LIST *const_list;
	OBJECT_REF *obj_ref;	
}
/* Line 193 of yacc.c.  */
#line 248 "../../src/executables/loader_old_grammar.c"
	YYSTYPE;
# define yystype YYSTYPE /* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
# define YYSTYPE_IS_TRIVIAL 1
#endif



/* Copy the second part of user declarations.  */


/* Line 216 of yacc.c.  */
#line 261 "../../src/executables/loader_old_grammar.c"

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
#define YYLAST   256

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  49
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  41
/* YYNRULES -- Number of rules.  */
#define YYNRULES  95
/* YYNRULES -- Number of states.  */
#define YYNSTATES  136

/* YYTRANSLATE(YYLEX) -- Bison symbol number corresponding to YYLEX.  */
#define YYUNDEFTOK  2
#define YYMAXUTOK   303

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
      45,    46,    47,    48
};

#if YYDEBUG
/* YYPRHS[YYN] -- Index of the first RHS symbol of rule number YYN in
   YYRHS.  */
static const yytype_uint8 yyprhs[] =
{
       0,     0,     3,     4,     7,     9,    12,    13,    17,    19,
      21,    23,    25,    27,    31,    35,    37,    40,    43,    47,
      49,    51,    53,    56,    60,    62,    65,    69,    71,    75,
      78,    82,    84,    87,    91,    93,    95,    98,   100,   102,
     104,   107,   109,   111,   113,   115,   117,   119,   121,   123,
     125,   127,   129,   131,   133,   135,   137,   139,   141,   143,
     145,   147,   149,   151,   153,   156,   159,   162,   166,   170,
     174,   178,   182,   185,   188,   191,   195,   197,   199,   202,
     205,   209,   211,   214,   218,   222,   227,   231,   233,   235,
     237,   239,   241,   243,   245,   247
};

/* YYRHS -- A `-1'-separated list of the rules' RHS.  */
static const yytype_int8 yyrhs[] =
{
      50,     0,    -1,    -1,    51,    52,    -1,    53,    -1,    52,
      53,    -1,    -1,    55,    54,     3,    -1,     3,    -1,    56,
      -1,    68,    -1,    58,    -1,    57,    -1,    13,    40,    27,
      -1,    14,    40,    59,    -1,    61,    -1,    61,    64,    -1,
      60,    61,    -1,    60,    61,    64,    -1,     5,    -1,     6,
      -1,     7,    -1,    24,    25,    -1,    24,    62,    25,    -1,
      63,    -1,    62,    63,    -1,    62,    48,    63,    -1,    40,
      -1,    15,    40,    65,    -1,    24,    25,    -1,    24,    66,
      25,    -1,    67,    -1,    66,    67,    -1,    66,    48,    67,
      -1,    40,    -1,    69,    -1,    69,    70,    -1,    70,    -1,
      28,    -1,    71,    -1,    70,    71,    -1,    72,    -1,    74,
      -1,    73,    -1,    80,    -1,    75,    -1,    76,    -1,    77,
      -1,    78,    -1,    79,    -1,     4,    -1,    29,    -1,    30,
      -1,    31,    -1,    32,    -1,    33,    -1,    34,    -1,    27,
      -1,    26,    -1,    35,    -1,    89,    -1,    81,    -1,    84,
      -1,    86,    -1,    41,    46,    -1,    43,    46,    -1,    42,
      47,    -1,     8,    41,    46,    -1,     9,    41,    46,    -1,
      11,    41,    46,    -1,    10,    41,    46,    -1,    12,    41,
      46,    -1,    44,    46,    -1,    45,    46,    -1,    20,    82,
      -1,    20,    82,    83,    -1,    27,    -1,    40,    -1,    21,
      27,    -1,    22,    23,    -1,    22,    85,    23,    -1,    71,
      -1,    85,    71,    -1,    85,    48,    71,    -1,    85,     3,
      71,    -1,    85,    48,     3,    71,    -1,    87,    41,    46,
      -1,    16,    -1,    17,    -1,    18,    -1,    19,    -1,    39,
      -1,    36,    -1,    37,    -1,    38,    -1,    88,    26,    -1
};

/* YYRLINE[YYN] -- source line where rule number YYN was defined.  */
static const yytype_uint16 yyrline[] =
{
       0,   157,   157,   157,   163,   165,   168,   168,   170,   174,
     176,   180,   182,   186,   197,   261,   267,   273,   279,   287,
     289,   291,   295,   300,   307,   313,   319,   327,   330,   343,
     348,   355,   361,   367,   375,   379,   384,   390,   398,   402,
     408,   416,   417,   418,   419,   420,   421,   422,   423,   424,
     425,   426,   427,   428,   429,   430,   431,   432,   433,   434,
     435,   436,   437,   438,   442,   449,   456,   463,   470,   477,
     484,   491,   498,   503,   510,   515,   523,   535,   549,   553,
     558,   565,   571,   577,   583,   589,   597,   603,   605,   607,
     609,   613,   613,   613,   613,   616
};
#endif

#if YYDEBUG || YYERROR_VERBOSE || YYTOKEN_TABLE
/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "$end", "error", "$undefined", "NL", "NULL_", "CLASS", "SHARED",
  "DEFAULT", "DATE", "TIME", "UTIME", "TIMESTAMP", "DATETIME", "CMD_ID",
  "CMD_CLASS", "CMD_CONSTRUCTOR", "REF_ELO_INT", "REF_ELO_EXT", "REF_USER",
  "REF_CLASS", "OBJECT_REFERENCE", "OID_DELIMETER", "SET_START_BRACE",
  "SET_END_BRACE", "START_PAREN", "END_PAREN", "REAL_LIT", "INT_LIT",
  "OID_", "TIME_LIT4", "TIME_LIT42", "TIME_LIT3", "TIME_LIT31",
  "TIME_LIT2", "TIME_LIT1", "DATE_LIT2", "YEN_SYMBOL", "WON_SYMBOL",
  "BACKSLASH", "DOLLAR_SYMBOL", "IDENTIFIER", "Quote", "DQuote", "NQuote",
  "BQuote", "XQuote", "SQS_String_Body", "DQS_String_Body", "COMMA",
  "$accept", "loader_start", "@1", "loader_lines", "line", "@2",
  "one_line", "command_line", "id_command", "class_command",
  "class_commamd_spec", "attribute_list_qualifier", "attribute_list",
  "attribute_names", "attribute_name", "constructor_spec",
  "constructor_argument_list", "argument_names", "argument_name",
  "instance_line", "object_id", "constant_list", "constant", "ansi_string",
  "nchar_string", "dq_string", "sql2_date", "sql2_time", "sql2_timestamp",
  "utime", "sql2_datetime", "bit_string", "object_reference",
  "class_identifier", "instance_number", "set_constant", "set_elements",
  "system_object_reference", "ref_type", "currency", "monetary", 0
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
     295,   296,   297,   298,   299,   300,   301,   302,   303
};
# endif

/* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const yytype_uint8 yyr1[] =
{
       0,    49,    51,    50,    52,    52,    54,    53,    53,    55,
      55,    56,    56,    57,    58,    59,    59,    59,    59,    60,
      60,    60,    61,    61,    62,    62,    62,    63,    64,    65,
      65,    66,    66,    66,    67,    68,    68,    68,    69,    70,
      70,    71,    71,    71,    71,    71,    71,    71,    71,    71,
      71,    71,    71,    71,    71,    71,    71,    71,    71,    71,
      71,    71,    71,    71,    72,    73,    74,    75,    76,    77,
      78,    79,    80,    80,    81,    81,    82,    82,    83,    84,
      84,    85,    85,    85,    85,    85,    86,    87,    87,    87,
      87,    88,    88,    88,    88,    89
};

/* YYR2[YYN] -- Number of symbols composing right hand side of rule YYN.  */
static const yytype_uint8 yyr2[] =
{
       0,     2,     0,     2,     1,     2,     0,     3,     1,     1,
       1,     1,     1,     3,     3,     1,     2,     2,     3,     1,
       1,     1,     2,     3,     1,     2,     3,     1,     3,     2,
       3,     1,     2,     3,     1,     1,     2,     1,     1,     1,
       2,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     2,     2,     2,     3,     3,     3,
       3,     3,     2,     2,     2,     3,     1,     1,     2,     2,
       3,     1,     2,     3,     3,     4,     3,     1,     1,     1,
       1,     1,     1,     1,     1,     2
};

/* YYDEFACT[STATE-NAME] -- Default rule to reduce with in state
   STATE-NUM when YYTABLE doesn't specify something else to do.  Zero
   means the default is an error.  */
static const yytype_uint8 yydefact[] =
{
       2,     0,     0,     1,     8,    50,     0,     0,     0,     0,
       0,     0,     0,    87,    88,    89,    90,     0,     0,    58,
      57,    38,    51,    52,    53,    54,    55,    56,    59,    92,
      93,    94,    91,     0,     0,     0,     0,     0,     3,     4,
       6,     9,    12,    11,    10,    35,    37,    39,    41,    43,
      42,    45,    46,    47,    48,    49,    44,    61,    62,    63,
       0,     0,    60,     0,     0,     0,     0,     0,     0,     0,
      76,    77,    74,    79,    81,     0,    64,    66,    65,    72,
      73,     5,     0,    36,    40,     0,    95,    67,    68,    70,
      69,    71,    13,    19,    20,    21,     0,    14,     0,    15,
       0,    75,     0,    80,     0,    82,     7,    86,    22,    27,
       0,    24,    17,     0,    16,    78,    84,     0,    83,    23,
       0,    25,    18,     0,    85,    26,     0,    28,    29,    34,
       0,    31,    30,     0,    32,    33
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
      -1,     1,     2,    38,    39,    82,    40,    41,    42,    43,
      97,    98,    99,   110,   111,   114,   127,   130,   131,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    72,   101,    58,    75,    59,    60,    61,
      62
};

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
#define YYPACT_NINF -106
static const yytype_int16 yypact[] =
{
    -106,     2,    84,  -106,  -106,  -106,   -34,   -28,   -17,   -16,
      11,    14,    18,  -106,  -106,  -106,  -106,   -26,   169,  -106,
    -106,  -106,  -106,  -106,  -106,  -106,  -106,  -106,  -106,  -106,
    -106,  -106,  -106,    13,    17,    20,    24,    25,    84,  -106,
    -106,  -106,  -106,  -106,  -106,   211,   211,  -106,  -106,  -106,
    -106,  -106,  -106,  -106,  -106,  -106,  -106,  -106,  -106,  -106,
      19,    41,  -106,    26,    27,    28,    29,    30,    50,    44,
    -106,  -106,    57,  -106,  -106,     0,  -106,  -106,  -106,  -106,
    -106,  -106,    76,   211,  -106,    34,  -106,  -106,  -106,  -106,
    -106,  -106,  -106,  -106,  -106,  -106,   -19,  -106,    58,    66,
      56,  -106,   211,  -106,   127,  -106,  -106,  -106,  -106,  -106,
      15,  -106,    66,    45,  -106,  -106,  -106,   211,  -106,  -106,
      49,  -106,  -106,    67,  -106,  -106,    22,  -106,  -106,  -106,
      21,  -106,  -106,    65,  -106,  -106
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int8 yypgoto[] =
{
    -106,  -106,  -106,  -106,    52,  -106,  -106,  -106,  -106,  -106,
    -106,  -106,     9,  -106,  -105,    -4,  -106,  -106,   -77,  -106,
    -106,    64,   -18,  -106,  -106,  -106,  -106,  -106,  -106,  -106,
    -106,  -106,  -106,  -106,  -106,  -106,  -106,  -106,  -106,  -106,
    -106
};

/* YYTABLE[YYPACT[STATE-NUM]].  What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule which
   number is the opposite.  If zero, do what YYDEFACT says.
   If YYTABLE_NINF, syntax error.  */
#define YYTABLE_NINF -1
static const yytype_uint8 yytable[] =
{
      74,    70,     3,   102,     5,   121,   108,    63,     6,     7,
       8,     9,    10,    64,    71,   125,    13,    14,    15,    16,
      17,   109,    18,   103,    65,    66,    19,    20,    84,    22,
      23,    24,    25,    26,    27,    28,    29,    30,    31,    32,
     119,    33,    34,    35,    36,    37,   132,   128,   104,    93,
      94,    95,    67,   134,    68,   109,   135,   105,    69,    76,
      85,   129,   129,   120,    77,    84,    78,    86,    96,   133,
      79,    80,    87,    88,    89,    90,    91,    92,   100,   106,
     107,   113,    96,   115,   116,   123,   118,     4,     5,   109,
      81,   126,     6,     7,     8,     9,    10,    11,    12,   124,
      13,    14,    15,    16,    17,   129,    18,   112,   122,    83,
      19,    20,    21,    22,    23,    24,    25,    26,    27,    28,
      29,    30,    31,    32,     0,    33,    34,    35,    36,    37,
     117,     5,     0,     0,     0,     6,     7,     8,     9,    10,
       0,     0,     0,    13,    14,    15,    16,    17,     0,    18,
       0,     0,     0,    19,    20,     0,    22,    23,    24,    25,
      26,    27,    28,    29,    30,    31,    32,     0,    33,    34,
      35,    36,    37,     5,     0,     0,     0,     6,     7,     8,
       9,    10,     0,     0,     0,    13,    14,    15,    16,    17,
       0,    18,    73,     0,     0,    19,    20,     0,    22,    23,
      24,    25,    26,    27,    28,    29,    30,    31,    32,     0,
      33,    34,    35,    36,    37,     5,     0,     0,     0,     6,
       7,     8,     9,    10,     0,     0,     0,    13,    14,    15,
      16,    17,     0,    18,     0,     0,     0,    19,    20,     0,
      22,    23,    24,    25,    26,    27,    28,    29,    30,    31,
      32,     0,    33,    34,    35,    36,    37
};

static const yytype_int16 yycheck[] =
{
      18,    27,     0,     3,     4,   110,    25,    41,     8,     9,
      10,    11,    12,    41,    40,   120,    16,    17,    18,    19,
      20,    40,    22,    23,    41,    41,    26,    27,    46,    29,
      30,    31,    32,    33,    34,    35,    36,    37,    38,    39,
      25,    41,    42,    43,    44,    45,    25,    25,    48,     5,
       6,     7,    41,   130,    40,    40,   133,    75,    40,    46,
      41,    40,    40,    48,    47,    83,    46,    26,    24,    48,
      46,    46,    46,    46,    46,    46,    46,    27,    21,     3,
      46,    15,    24,    27,   102,    40,   104,     3,     4,    40,
      38,    24,     8,     9,    10,    11,    12,    13,    14,   117,
      16,    17,    18,    19,    20,    40,    22,    98,   112,    45,
      26,    27,    28,    29,    30,    31,    32,    33,    34,    35,
      36,    37,    38,    39,    -1,    41,    42,    43,    44,    45,
       3,     4,    -1,    -1,    -1,     8,     9,    10,    11,    12,
      -1,    -1,    -1,    16,    17,    18,    19,    20,    -1,    22,
      -1,    -1,    -1,    26,    27,    -1,    29,    30,    31,    32,
      33,    34,    35,    36,    37,    38,    39,    -1,    41,    42,
      43,    44,    45,     4,    -1,    -1,    -1,     8,     9,    10,
      11,    12,    -1,    -1,    -1,    16,    17,    18,    19,    20,
      -1,    22,    23,    -1,    -1,    26,    27,    -1,    29,    30,
      31,    32,    33,    34,    35,    36,    37,    38,    39,    -1,
      41,    42,    43,    44,    45,     4,    -1,    -1,    -1,     8,
       9,    10,    11,    12,    -1,    -1,    -1,    16,    17,    18,
      19,    20,    -1,    22,    -1,    -1,    -1,    26,    27,    -1,
      29,    30,    31,    32,    33,    34,    35,    36,    37,    38,
      39,    -1,    41,    42,    43,    44,    45
};

/* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
   symbol of state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,    50,    51,     0,     3,     4,     8,     9,    10,    11,
      12,    13,    14,    16,    17,    18,    19,    20,    22,    26,
      27,    28,    29,    30,    31,    32,    33,    34,    35,    36,
      37,    38,    39,    41,    42,    43,    44,    45,    52,    53,
      55,    56,    57,    58,    68,    69,    70,    71,    72,    73,
      74,    75,    76,    77,    78,    79,    80,    81,    84,    86,
      87,    88,    89,    41,    41,    41,    41,    41,    40,    40,
      27,    40,    82,    23,    71,    85,    46,    47,    46,    46,
      46,    53,    54,    70,    71,    41,    26,    46,    46,    46,
      46,    46,    27,     5,     6,     7,    24,    59,    60,    61,
      21,    83,     3,    23,    48,    71,     3,    46,    25,    40,
      62,    63,    61,    15,    64,    27,    71,     3,    71,    25,
      48,    63,    64,    40,    71,    63,    24,    65,    25,    40,
      66,    67,    25,    48,    67,    67
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
#line 157 "../../src/executables/loader_old_grammar.y"
    { act_init(); ;}
    break;

  case 3:
#line 159 "../../src/executables/loader_old_grammar.y"
    { act_finish(0); act_finish(1); ;}
    break;

  case 4:
#line 163 "../../src/executables/loader_old_grammar.y"
    { DBG_PRINT("line"); ;}
    break;

  case 5:
#line 165 "../../src/executables/loader_old_grammar.y"
    { DBG_PRINT("line_list line"); ;}
    break;

  case 6:
#line 168 "../../src/executables/loader_old_grammar.y"
    { DBG_PRINT("one_line"); ;}
    break;

  case 7:
#line 168 "../../src/executables/loader_old_grammar.y"
    { in_instance_line = 1; ;}
    break;

  case 8:
#line 170 "../../src/executables/loader_old_grammar.y"
    { in_instance_line = 1; ;}
    break;

  case 9:
#line 174 "../../src/executables/loader_old_grammar.y"
    { DBG_PRINT("command_line"); ;}
    break;

  case 10:
#line 176 "../../src/executables/loader_old_grammar.y"
    { DBG_PRINT("instance_line"); ;}
    break;

  case 11:
#line 180 "../../src/executables/loader_old_grammar.y"
    { DBG_PRINT("class_command"); ;}
    break;

  case 12:
#line 182 "../../src/executables/loader_old_grammar.y"
    { DBG_PRINT("id_command"); ;}
    break;

  case 13:
#line 187 "../../src/executables/loader_old_grammar.y"
    {
		act_start_id((yyvsp[(2) - (3)].cptr));
		act_set_id(atoi((yyvsp[(3) - (3)].cptr)));
		
		free((yyvsp[(2) - (3)].cptr));
		free((yyvsp[(3) - (3)].cptr));
	;}
    break;

  case 14:
#line 198 "../../src/executables/loader_old_grammar.y"
    { 
		CLASS_COMMAND_SPEC *cmd_spec;
		char *class_name;
		STRING_LIST *attr, *save, *args;
		
		DBG_PRINT("class_commamd_spec");
		
		class_name = (yyvsp[(2) - (3)].cptr);
		cmd_spec = (yyvsp[(3) - (3)].cmd_spec);
		
		act_set_class (class_name);
		
		if (cmd_spec->qualifier == LDR_ATTRIBUTE_CLASS)
		{
			act_class_attributes();
		}
		else if (cmd_spec->qualifier == LDR_ATTRIBUTE_SHARED)
		{
			act_shared_attributes();
		}
		else if (cmd_spec->qualifier == LDR_ATTRIBUTE_DEFAULT)
		{
			act_default_attributes();
		}
		
		for(attr = cmd_spec->attr_list; attr; attr = attr->next)
		{
			act_add_attribute(attr->val);
		}
		
		if (cmd_spec->ctor_spec)
		{
			act_set_constructor(cmd_spec->ctor_spec->idname);
			
			for(args = cmd_spec->ctor_spec->arg_list; args; args = args->next)
			{
				act_add_argument(args->val);
			}
			
			for(args = cmd_spec->ctor_spec->arg_list; args; args = save)
			{
				save = args->next;
				free(args->val);
				free(args);
			}

			free(cmd_spec->ctor_spec->idname);
			free(cmd_spec->ctor_spec);
		}

		for(attr = cmd_spec->attr_list; attr; attr = save)
		{
			save = attr->next;
			free(attr->val);
			free(attr);
		}
		
		free(class_name);
		free(cmd_spec);
	;}
    break;

  case 15:
#line 262 "../../src/executables/loader_old_grammar.y"
    {
        	DBG_PRINT("attribute_list");
        	(yyval.cmd_spec) = make_class_command_spec(LDR_ATTRIBUTE_ANY, (yyvsp[(1) - (1)].strlist), NULL);
        ;}
    break;

  case 16:
#line 268 "../../src/executables/loader_old_grammar.y"
    {
        	DBG_PRINT("attribute_list constructor_spec");
        	(yyval.cmd_spec) = make_class_command_spec(LDR_ATTRIBUTE_ANY, (yyvsp[(1) - (2)].strlist), (yyvsp[(2) - (2)].ctor_spec));
        ;}
    break;

  case 17:
#line 274 "../../src/executables/loader_old_grammar.y"
    {
        	DBG_PRINT("attribute_list_qualifier attribute_list");
        	(yyval.cmd_spec) = make_class_command_spec((yyvsp[(1) - (2)].intval), (yyvsp[(2) - (2)].strlist), NULL);
        ;}
    break;

  case 18:
#line 280 "../../src/executables/loader_old_grammar.y"
    {
        	DBG_PRINT("attribute_list_qualifier attribute_list constructor_spec");
        	(yyval.cmd_spec) = make_class_command_spec((yyvsp[(1) - (3)].intval), (yyvsp[(2) - (3)].strlist), (yyvsp[(3) - (3)].ctor_spec));
        ;}
    break;

  case 19:
#line 287 "../../src/executables/loader_old_grammar.y"
    { DBG_PRINT("CLASS"); (yyval.intval) = LDR_ATTRIBUTE_CLASS; ;}
    break;

  case 20:
#line 289 "../../src/executables/loader_old_grammar.y"
    { DBG_PRINT("SHARED"); (yyval.intval) = LDR_ATTRIBUTE_SHARED; ;}
    break;

  case 21:
#line 291 "../../src/executables/loader_old_grammar.y"
    { DBG_PRINT("DEFAULT"); (yyval.intval) = LDR_ATTRIBUTE_DEFAULT; ;}
    break;

  case 22:
#line 296 "../../src/executables/loader_old_grammar.y"
    {
		(yyval.strlist) = NULL;
	;}
    break;

  case 23:
#line 301 "../../src/executables/loader_old_grammar.y"
    {
		(yyval.strlist) = (yyvsp[(2) - (3)].strlist); 
	;}
    break;

  case 24:
#line 308 "../../src/executables/loader_old_grammar.y"
    {
		DBG_PRINT("attribute_name");
		(yyval.strlist) = append_string_list(NULL, (yyvsp[(1) - (1)].cptr));
	;}
    break;

  case 25:
#line 314 "../../src/executables/loader_old_grammar.y"
    {
		DBG_PRINT("attribute_names attribute_name");
		(yyval.strlist) = append_string_list((yyvsp[(1) - (2)].strlist), (yyvsp[(2) - (2)].cptr));
	;}
    break;

  case 26:
#line 320 "../../src/executables/loader_old_grammar.y"
    {
		DBG_PRINT("attribute_names COMMA attribute_name");
		(yyval.strlist) = append_string_list((yyvsp[(1) - (3)].strlist), (yyvsp[(3) - (3)].cptr));
	;}
    break;

  case 27:
#line 327 "../../src/executables/loader_old_grammar.y"
    { (yyval.cptr) = (yyvsp[(1) - (1)].cptr); ;}
    break;

  case 28:
#line 331 "../../src/executables/loader_old_grammar.y"
    {
		CONSTRUCTOR_SPEC *spec;
		
		spec = (CONSTRUCTOR_SPEC *) malloc(sizeof(CONSTRUCTOR_SPEC));
		spec->idname = (yyvsp[(2) - (3)].cptr);
		spec->arg_list =  (yyvsp[(3) - (3)].strlist);
		
		(yyval.ctor_spec) = spec;
	;}
    break;

  case 29:
#line 344 "../../src/executables/loader_old_grammar.y"
    {
		(yyval.strlist) = NULL;
	;}
    break;

  case 30:
#line 349 "../../src/executables/loader_old_grammar.y"
    {
		(yyval.strlist) = (yyvsp[(2) - (3)].strlist);
	;}
    break;

  case 31:
#line 356 "../../src/executables/loader_old_grammar.y"
    {
		DBG_PRINT("argument_name");
		(yyval.strlist) = append_string_list(NULL, (yyvsp[(1) - (1)].cptr));
	;}
    break;

  case 32:
#line 362 "../../src/executables/loader_old_grammar.y"
    {
		DBG_PRINT("argument_names argument_name");
		(yyval.strlist) = append_string_list((yyvsp[(1) - (2)].strlist), (yyvsp[(2) - (2)].cptr));
	;}
    break;

  case 33:
#line 368 "../../src/executables/loader_old_grammar.y"
    {
		DBG_PRINT("argument_names COMMA argument_name");
		(yyval.strlist) = append_string_list((yyvsp[(1) - (3)].strlist), (yyvsp[(3) - (3)].cptr));
	;}
    break;

  case 34:
#line 375 "../../src/executables/loader_old_grammar.y"
    { (yyval.cptr) = (yyvsp[(1) - (1)].cptr); ;}
    break;

  case 35:
#line 380 "../../src/executables/loader_old_grammar.y"
    {
		act_add_instance((yyvsp[(1) - (1)].intval));
	;}
    break;

  case 36:
#line 385 "../../src/executables/loader_old_grammar.y"
    {
		act_add_instance((yyvsp[(1) - (2)].intval));
		process_constant_list((yyvsp[(2) - (2)].const_list));
	;}
    break;

  case 37:
#line 391 "../../src/executables/loader_old_grammar.y"
    {
		act_add_instance(-1);
		process_constant_list((yyvsp[(1) - (1)].const_list));
	;}
    break;

  case 38:
#line 398 "../../src/executables/loader_old_grammar.y"
    { (yyval.intval) =  (yyvsp[(1) - (1)].intval); ;}
    break;

  case 39:
#line 403 "../../src/executables/loader_old_grammar.y"
    {
		DBG_PRINT("constant");
		(yyval.const_list) = append_constant_list(NULL, (yyvsp[(1) - (1)].constant));
	;}
    break;

  case 40:
#line 409 "../../src/executables/loader_old_grammar.y"
    {
		DBG_PRINT("constant_list constant");
		(yyval.const_list) = append_constant_list((yyvsp[(1) - (2)].const_list), (yyvsp[(2) - (2)].constant));
	;}
    break;

  case 41:
#line 416 "../../src/executables/loader_old_grammar.y"
    { (yyval.constant) = (yyvsp[(1) - (1)].constant); ;}
    break;

  case 42:
#line 417 "../../src/executables/loader_old_grammar.y"
    { (yyval.constant) = (yyvsp[(1) - (1)].constant); ;}
    break;

  case 43:
#line 418 "../../src/executables/loader_old_grammar.y"
    { (yyval.constant) = (yyvsp[(1) - (1)].constant); ;}
    break;

  case 44:
#line 419 "../../src/executables/loader_old_grammar.y"
    { (yyval.constant) = (yyvsp[(1) - (1)].constant); ;}
    break;

  case 45:
#line 420 "../../src/executables/loader_old_grammar.y"
    { (yyval.constant) = (yyvsp[(1) - (1)].constant); ;}
    break;

  case 46:
#line 421 "../../src/executables/loader_old_grammar.y"
    { (yyval.constant) = (yyvsp[(1) - (1)].constant); ;}
    break;

  case 47:
#line 422 "../../src/executables/loader_old_grammar.y"
    { (yyval.constant) = (yyvsp[(1) - (1)].constant); ;}
    break;

  case 48:
#line 423 "../../src/executables/loader_old_grammar.y"
    { (yyval.constant) = (yyvsp[(1) - (1)].constant); ;}
    break;

  case 49:
#line 424 "../../src/executables/loader_old_grammar.y"
    { (yyval.constant) = (yyvsp[(1) - (1)].constant); ;}
    break;

  case 50:
#line 425 "../../src/executables/loader_old_grammar.y"
    { (yyval.constant) = make_constant(LDR_OLD_NULL, NULL); ;}
    break;

  case 51:
#line 426 "../../src/executables/loader_old_grammar.y"
    { (yyval.constant) = make_constant(LDR_OLD_TIME4, (yyvsp[(1) - (1)].cptr)); ;}
    break;

  case 52:
#line 427 "../../src/executables/loader_old_grammar.y"
    { (yyval.constant) = make_constant(LDR_OLD_TIME42, (yyvsp[(1) - (1)].cptr)); ;}
    break;

  case 53:
#line 428 "../../src/executables/loader_old_grammar.y"
    { (yyval.constant) = make_constant(LDR_OLD_TIME3, (yyvsp[(1) - (1)].cptr)); ;}
    break;

  case 54:
#line 429 "../../src/executables/loader_old_grammar.y"
    { (yyval.constant) = make_constant(LDR_OLD_TIME31, (yyvsp[(1) - (1)].cptr)); ;}
    break;

  case 55:
#line 430 "../../src/executables/loader_old_grammar.y"
    { (yyval.constant) = make_constant(LDR_OLD_TIME2, (yyvsp[(1) - (1)].cptr)); ;}
    break;

  case 56:
#line 431 "../../src/executables/loader_old_grammar.y"
    { (yyval.constant) = make_constant(LDR_OLD_TIME1, (yyvsp[(1) - (1)].cptr)); ;}
    break;

  case 57:
#line 432 "../../src/executables/loader_old_grammar.y"
    { (yyval.constant) = make_constant(LDR_OLD_INT, (yyvsp[(1) - (1)].cptr)); ;}
    break;

  case 58:
#line 433 "../../src/executables/loader_old_grammar.y"
    { (yyval.constant) = make_constant(LDR_OLD_REAL, (yyvsp[(1) - (1)].cptr)); ;}
    break;

  case 59:
#line 434 "../../src/executables/loader_old_grammar.y"
    { (yyval.constant) = make_constant(LDR_OLD_DATE2, (yyvsp[(1) - (1)].cptr)); ;}
    break;

  case 60:
#line 435 "../../src/executables/loader_old_grammar.y"
    { (yyval.constant) = (yyvsp[(1) - (1)].constant); ;}
    break;

  case 61:
#line 436 "../../src/executables/loader_old_grammar.y"
    { (yyval.constant) = (yyvsp[(1) - (1)].constant); ;}
    break;

  case 62:
#line 437 "../../src/executables/loader_old_grammar.y"
    { (yyval.constant) = (yyvsp[(1) - (1)].constant); ;}
    break;

  case 63:
#line 438 "../../src/executables/loader_old_grammar.y"
    { (yyval.constant) = (yyvsp[(1) - (1)].constant); ;}
    break;

  case 64:
#line 443 "../../src/executables/loader_old_grammar.y"
    {
		(yyval.constant) = make_constant(LDR_OLD_STR, (yyvsp[(2) - (2)].cptr));
	;}
    break;

  case 65:
#line 450 "../../src/executables/loader_old_grammar.y"
    {
		(yyval.constant) = make_constant(LDR_OLD_NSTR, (yyvsp[(2) - (2)].cptr));
	;}
    break;

  case 66:
#line 457 "../../src/executables/loader_old_grammar.y"
    {
		(yyval.constant) = make_constant(LDR_OLD_STR, (yyvsp[(2) - (2)].cptr));
	;}
    break;

  case 67:
#line 464 "../../src/executables/loader_old_grammar.y"
    {
		(yyval.constant) = make_constant(LDR_OLD_DATE, (yyvsp[(3) - (3)].cptr));
	;}
    break;

  case 68:
#line 471 "../../src/executables/loader_old_grammar.y"
    {
		(yyval.constant) = make_constant(LDR_OLD_TIME, (yyvsp[(3) - (3)].cptr));
	;}
    break;

  case 69:
#line 478 "../../src/executables/loader_old_grammar.y"
    {
		(yyval.constant) = make_constant(LDR_OLD_TIMESTAMP, (yyvsp[(3) - (3)].cptr));
	;}
    break;

  case 70:
#line 485 "../../src/executables/loader_old_grammar.y"
    {
		(yyval.constant) = make_constant(LDR_OLD_TIMESTAMP, (yyvsp[(3) - (3)].cptr));
	;}
    break;

  case 71:
#line 492 "../../src/executables/loader_old_grammar.y"
    {
		(yyval.constant) = make_constant(LDR_OLD_DATETIME, (yyvsp[(3) - (3)].cptr));
	;}
    break;

  case 72:
#line 499 "../../src/executables/loader_old_grammar.y"
    {
		(yyval.constant) = make_constant(LDR_OLD_BSTR, (yyvsp[(2) - (2)].cptr));
	;}
    break;

  case 73:
#line 504 "../../src/executables/loader_old_grammar.y"
    {
		(yyval.constant) = make_constant(LDR_OLD_XSTR, (yyvsp[(2) - (2)].cptr));
	;}
    break;

  case 74:
#line 511 "../../src/executables/loader_old_grammar.y"
    {
		(yyval.constant) = make_constant(LDR_OLD_CLASS_OID, (yyvsp[(2) - (2)].obj_ref));
	;}
    break;

  case 75:
#line 516 "../../src/executables/loader_old_grammar.y"
    {
		(yyvsp[(2) - (3)].obj_ref)->instance_number = (yyvsp[(3) - (3)].cptr);
		(yyval.constant) = make_constant(LDR_OLD_OID, (yyvsp[(2) - (3)].obj_ref));
	;}
    break;

  case 76:
#line 524 "../../src/executables/loader_old_grammar.y"
    {
		OBJECT_REF *ref;
		
		ref = (OBJECT_REF *) malloc(sizeof(OBJECT_REF));
		ref->class_id = (yyvsp[(1) - (1)].cptr);
		ref->class_name = NULL;
		ref->instance_number = NULL;
		
		(yyval.obj_ref) = ref;
	;}
    break;

  case 77:
#line 536 "../../src/executables/loader_old_grammar.y"
    {
		OBJECT_REF *ref;
		
		ref = (OBJECT_REF *) malloc(sizeof(OBJECT_REF));
		ref->class_id = NULL;
		ref->class_name = (yyvsp[(1) - (1)].cptr);
		ref->instance_number = NULL;
		
		(yyval.obj_ref) = ref;
	;}
    break;

  case 78:
#line 549 "../../src/executables/loader_old_grammar.y"
    { (yyval.cptr) = (yyvsp[(2) - (2)].cptr); ;}
    break;

  case 79:
#line 554 "../../src/executables/loader_old_grammar.y"
    {
		(yyval.constant) = make_constant(LDR_OLD_COLLECTION, NULL);
	;}
    break;

  case 80:
#line 559 "../../src/executables/loader_old_grammar.y"
    {
		(yyval.constant) = make_constant(LDR_OLD_COLLECTION, (yyvsp[(2) - (3)].const_list));
	;}
    break;

  case 81:
#line 566 "../../src/executables/loader_old_grammar.y"
    {
		DBG_PRINT("constant");
		(yyval.const_list) = append_constant_list(NULL, (yyvsp[(1) - (1)].constant));
	;}
    break;

  case 82:
#line 572 "../../src/executables/loader_old_grammar.y"
    {
		DBG_PRINT("set_elements constant");
		(yyval.const_list) = append_constant_list((yyvsp[(1) - (2)].const_list), (yyvsp[(2) - (2)].constant));
	;}
    break;

  case 83:
#line 578 "../../src/executables/loader_old_grammar.y"
    {
		DBG_PRINT("set_elements COMMA constant");
		(yyval.const_list) = append_constant_list((yyvsp[(1) - (3)].const_list), (yyvsp[(3) - (3)].constant));
	;}
    break;

  case 84:
#line 584 "../../src/executables/loader_old_grammar.y"
    {
		DBG_PRINT("set_elements NL constant");
		(yyval.const_list) = append_constant_list((yyvsp[(1) - (3)].const_list), (yyvsp[(3) - (3)].constant));
	;}
    break;

  case 85:
#line 590 "../../src/executables/loader_old_grammar.y"
    {
		DBG_PRINT("set_elements COMMA NL constant");
		(yyval.const_list) = append_constant_list((yyvsp[(1) - (4)].const_list), (yyvsp[(4) - (4)].constant));
	;}
    break;

  case 86:
#line 598 "../../src/executables/loader_old_grammar.y"
    {
		 (yyval.constant) = make_constant((yyvsp[(1) - (3)].intval), (yyvsp[(3) - (3)].cptr));
	;}
    break;

  case 87:
#line 603 "../../src/executables/loader_old_grammar.y"
    { (yyval.intval) = LDR_OLD_SYS_ELO_INTERNAL; ;}
    break;

  case 88:
#line 605 "../../src/executables/loader_old_grammar.y"
    { (yyval.intval) = LDR_OLD_SYS_ELO_EXTERNAL; ;}
    break;

  case 89:
#line 607 "../../src/executables/loader_old_grammar.y"
    { (yyval.intval) = LDR_OLD_SYS_USER; ;}
    break;

  case 90:
#line 609 "../../src/executables/loader_old_grammar.y"
    { (yyval.intval) = LDR_OLD_SYS_CLASS; ;}
    break;

  case 95:
#line 617 "../../src/executables/loader_old_grammar.y"
    {
		(yyval.constant) = make_constant(LDR_OLD_MONETARY, (yyvsp[(2) - (2)].cptr));
	;}
    break;


/* Line 1267 of yacc.c.  */
#line 2264 "../../src/executables/loader_old_grammar.c"
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


#line 621 "../../src/executables/loader_old_grammar.y"


static STRING_LIST *append_string_list(STRING_LIST *list, char *str)
{
	STRING_LIST *item, *last, *tmp;
	 
	item = (STRING_LIST *) malloc(sizeof(STRING_LIST));
	item->val = str;
	item->next = NULL;

	if (list)
	{	
		for (tmp = list; tmp; tmp = tmp->next)
		{
		 	last = tmp;
		}
		
		last->next = item;
	}
	else
	{
		list = item;
	}
	
	return list;
}

static CLASS_COMMAND_SPEC *make_class_command_spec(int qualifier, STRING_LIST *attr_list, CONSTRUCTOR_SPEC *ctor_spec)
{
	CLASS_COMMAND_SPEC *spec;
	 
	spec = (CLASS_COMMAND_SPEC *) malloc(sizeof(CLASS_COMMAND_SPEC));
	spec->qualifier = qualifier;
	spec->attr_list = attr_list;
	spec->ctor_spec = ctor_spec; 
	
	return spec;
}

static CONSTANT *make_constant(int type, void *val)
{
	CONSTANT *con;
		
	con = (CONSTANT *) malloc(sizeof(CONSTANT));
	con->type = type;
	con->val = val;
		
	return con;
}

static CONSTANT_LIST *append_constant_list(CONSTANT_LIST *list, CONSTANT *cons)
{
	CONSTANT_LIST *item, *last, *tmp;
	 
	item = (CONSTANT_LIST *) malloc(sizeof(CONSTANT_LIST));
	item->val = cons;
	item->next = NULL;
	
	if (list)
	{	
		for (tmp = list; tmp; tmp = tmp->next)
		{
		 	last = tmp;
		}
		
		last->next = item;
	}
	else
	{
		list = item;
	}
	
	return list;
}

static void process_constant(CONSTANT *c)
{
	switch (c->type)
	{
	case LDR_OLD_NULL:
		act_null();
		break;
		
	case LDR_OLD_INT:
		act_int((char *) c->val);
		free(c->val);
		break;
		
	case LDR_OLD_REAL:
		act_real((char *) c->val);
		free(c->val);
		break;
		
	case LDR_OLD_MONETARY:
		act_monetary((char *) c->val);
		free(c->val);
		break;
		
	case LDR_OLD_DATE2:
		act_date((char *) c->val);
		free(c->val);
		break;
		
	case LDR_OLD_TIME4:
		act_time((char *) c->val, 4);
		free(c->val);
		break;
		
	case LDR_OLD_TIME42:
		act_time((char *) c->val, 2);
		free(c->val);
		break;
		
	case LDR_OLD_TIME3:
		act_time((char *) c->val, 3);
		free(c->val);
		break;
		
	case LDR_OLD_TIME31:
		act_time((char *) c->val, 1);
		free(c->val);
		break;
		
	case LDR_OLD_TIME2:
		act_time((char *) c->val, 2);
		free(c->val);
		break;
		
	case LDR_OLD_TIME1:
		act_time((char *) c->val, 1);
		free(c->val);
		break;
		
	case LDR_OLD_DATE:
	case LDR_OLD_TIME:
	case LDR_OLD_TIMESTAMP:
	case LDR_OLD_DATETIME:
	case LDR_OLD_STR:
		act_string((char *) c->val, strlen((char *) c->val), DB_TYPE_CHAR);
		free(c->val);
		break;
		
	case LDR_OLD_NSTR:
		act_string((char *) c->val, strlen((char *) c->val), DB_TYPE_NCHAR);
		free(c->val);
		break;
		
	case LDR_OLD_BSTR:
		act_bstring((char *) c->val, 1);
		free(c->val);
		break;
		
	case LDR_OLD_XSTR:
		act_bstring((char *) c->val, 2);
		free(c->val);
		break;
		
	case LDR_OLD_SYS_ELO_INTERNAL:
	case LDR_OLD_SYS_ELO_EXTERNAL:
	case LDR_OLD_SYS_USER:
	case LDR_OLD_SYS_CLASS:
		act_system((char *) c->val, c->type);
		free(c->val);
		break;
		
	case LDR_OLD_OID:
	case LDR_OLD_CLASS_OID:
		{
			OBJECT_REF *ref;
			
			ref = (OBJECT_REF *) c->val;
			
			if (ref->class_id)
			{
				act_set_ref_class_id(atoi(ref->class_id));
			}
			else
			{
				act_set_ref_class(ref->class_name);
			}
			
			if (ref->instance_number)
			{
				act_reference(atoi(ref->instance_number));
			}
			else
			{
				act_reference_class();
			}
			
			if (ref->class_id)
			{
				free(ref->class_id);
			}
			
			if (ref->class_name)
			{
				free(ref->class_name);
			}
			
			if (ref->instance_number)
			{
				free(ref->instance_number);
			}
			    
			free(ref);
		}
		break;
	
	case LDR_OLD_COLLECTION:
		act_start_set();
		process_constant_list((CONSTANT_LIST *) c->val);
		act_end_set();
		break;
	
	default:
		break;	
	}
	
	free(c);
}

static void process_constant_list(CONSTANT_LIST *cons)
{
	CONSTANT_LIST *c, *save;
	
	for(c = cons; c; c = save)
	{
		save = c->next;
		process_constant(c->val);
		free(c);
	}
}

void do_loader_parse(FILE *fp)
{
  in_instance_line = 1;

  loader_yyin = fp;
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

