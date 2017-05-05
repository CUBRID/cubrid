/* A Bison parser, made by GNU Bison 3.0.4.  */

/* Bison implementation for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015 Free Software Foundation, Inc.

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
#define YYBISON_VERSION "3.0.4"

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 0

/* Push parsers.  */
#define YYPUSH 0

/* Pull parsers.  */
#define YYPULL 1


/* Substitute the variable and function names.  */
#define yyparse         loader_yyparse
#define yylex           loader_yylex
#define yyerror         loader_yyerror
#define yydebug         loader_yydebug
#define yynerrs         loader_yynerrs

#define yylval          loader_yylval
#define yychar          loader_yychar

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



# ifndef YY_NULLPTR
#  if defined __cplusplus && 201103L <= __cplusplus
#   define YY_NULLPTR nullptr
#  else
#   define YY_NULLPTR 0
#  endif
# endif

/* Enabling verbose error messages.  */
#ifdef YYERROR_VERBOSE
# undef YYERROR_VERBOSE
# define YYERROR_VERBOSE 1
#else
# define YYERROR_VERBOSE 1
#endif

/* In a future release of Bison, this section will be replaced
   by #include "loader_grammar.h".  */
#ifndef YY_LOADER_YY_HOME_VALENTINP_WORK_CUBRID_SRC_EXECUTABLES_LOADER_GRAMMAR_H_INCLUDED
# define YY_LOADER_YY_HOME_VALENTINP_WORK_CUBRID_SRC_EXECUTABLES_LOADER_GRAMMAR_H_INCLUDED
/* Debug traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif
#if YYDEBUG
extern int loader_yydebug;
#endif

/* Token type.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
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

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED

union YYSTYPE
{


	int 	intval;
	LDR_STRING	*string;
	LDR_CLASS_COMMAND_SPEC *cmd_spec;
	LDR_CONSTRUCTOR_SPEC *ctor_spec;
	LDR_CONSTANT *constant;
	LDR_OBJECT_REF *obj_ref;


};

typedef union YYSTYPE YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define YYSTYPE_IS_DECLARED 1
#endif


extern YYSTYPE loader_yylval;

int loader_yyparse (void);

#endif /* !YY_LOADER_YY_HOME_VALENTINP_WORK_CUBRID_SRC_EXECUTABLES_LOADER_GRAMMAR_H_INCLUDED  */

/* Copy the second part of user declarations.  */



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
#else
typedef signed char yytype_int8;
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
# elif ! defined YYSIZE_T
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# else
#  define YYSIZE_T unsigned int
# endif
#endif

#define YYSIZE_MAXIMUM ((YYSIZE_T) -1)

#ifndef YY_
# if defined YYENABLE_NLS && YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> /* INFRINGES ON USER NAME SPACE */
#   define YY_(Msgid) dgettext ("bison-runtime", Msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(Msgid) Msgid
# endif
#endif

#ifndef YY_ATTRIBUTE
# if (defined __GNUC__                                               \
      && (2 < __GNUC__ || (__GNUC__ == 2 && 96 <= __GNUC_MINOR__)))  \
     || defined __SUNPRO_C && 0x5110 <= __SUNPRO_C
#  define YY_ATTRIBUTE(Spec) __attribute__(Spec)
# else
#  define YY_ATTRIBUTE(Spec) /* empty */
# endif
#endif

#ifndef YY_ATTRIBUTE_PURE
# define YY_ATTRIBUTE_PURE   YY_ATTRIBUTE ((__pure__))
#endif

#ifndef YY_ATTRIBUTE_UNUSED
# define YY_ATTRIBUTE_UNUSED YY_ATTRIBUTE ((__unused__))
#endif

#if !defined _Noreturn \
     && (!defined __STDC_VERSION__ || __STDC_VERSION__ < 201112)
# if defined _MSC_VER && 1200 <= _MSC_VER
#  define _Noreturn __declspec (noreturn)
# else
#  define _Noreturn YY_ATTRIBUTE ((__noreturn__))
# endif
#endif

/* Suppress unused-variable warnings by "using" E.  */
#if ! defined lint || defined __GNUC__
# define YYUSE(E) ((void) (E))
#else
# define YYUSE(E) /* empty */
#endif

#if defined __GNUC__ && 407 <= __GNUC__ * 100 + __GNUC_MINOR__
/* Suppress an incorrect diagnostic about yylval being uninitialized.  */
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN \
    _Pragma ("GCC diagnostic push") \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")\
    _Pragma ("GCC diagnostic ignored \"-Wmaybe-uninitialized\"")
# define YY_IGNORE_MAYBE_UNINITIALIZED_END \
    _Pragma ("GCC diagnostic pop")
#else
# define YY_INITIAL_VALUE(Value) Value
#endif
#ifndef YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_END
#endif
#ifndef YY_INITIAL_VALUE
# define YY_INITIAL_VALUE(Value) /* Nothing. */
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
#    if ! defined _ALLOCA_H && ! defined EXIT_SUCCESS
#     include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
      /* Use EXIT_SUCCESS as a witness for stdlib.h.  */
#     ifndef EXIT_SUCCESS
#      define EXIT_SUCCESS 0
#     endif
#    endif
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's 'empty if-body' warning.  */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (0)
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
#  if (defined __cplusplus && ! defined EXIT_SUCCESS \
       && ! ((defined YYMALLOC || defined malloc) \
             && (defined YYFREE || defined free)))
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   ifndef EXIT_SUCCESS
#    define EXIT_SUCCESS 0
#   endif
#  endif
#  ifndef YYMALLOC
#   define YYMALLOC malloc
#   if ! defined malloc && ! defined EXIT_SUCCESS
void *malloc (YYSIZE_T); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifndef YYFREE
#   define YYFREE free
#   if ! defined free && ! defined EXIT_SUCCESS
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
  yytype_int16 yyss_alloc;
  YYSTYPE yyvs_alloc;
};

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (sizeof (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (sizeof (yytype_int16) + sizeof (YYSTYPE)) \
      + YYSTACK_GAP_MAXIMUM)

# define YYCOPY_NEEDED 1

/* Relocate STACK from its old location to the new one.  The
   local variables YYSIZE and YYSTACKSIZE give the old and new number of
   elements in the stack, and YYPTR gives the new location of the
   stack.  Advance YYPTR to a properly aligned location for the next
   stack.  */
# define YYSTACK_RELOCATE(Stack_alloc, Stack)                           \
    do                                                                  \
      {                                                                 \
        YYSIZE_T yynewbytes;                                            \
        YYCOPY (&yyptr->Stack_alloc, Stack, yysize);                    \
        Stack = &yyptr->Stack_alloc;                                    \
        yynewbytes = yystacksize * sizeof (*Stack) + YYSTACK_GAP_MAXIMUM; \
        yyptr += yynewbytes / sizeof (*yyptr);                          \
      }                                                                 \
    while (0)

#endif

#if defined YYCOPY_NEEDED && YYCOPY_NEEDED
/* Copy COUNT objects from SRC to DST.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if defined __GNUC__ && 1 < __GNUC__
#   define YYCOPY(Dst, Src, Count) \
      __builtin_memcpy (Dst, Src, (Count) * sizeof (*(Src)))
#  else
#   define YYCOPY(Dst, Src, Count)              \
      do                                        \
        {                                       \
          YYSIZE_T yyi;                         \
          for (yyi = 0; yyi < (Count); yyi++)   \
            (Dst)[yyi] = (Src)[yyi];            \
        }                                       \
      while (0)
#  endif
# endif
#endif /* !YYCOPY_NEEDED */

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
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  195

/* YYTRANSLATE[YYX] -- Symbol number corresponding to YYX as returned
   by yylex, with out-of-bounds checking.  */
#define YYUNDEFTOK  2
#define YYMAXUTOK   328

#define YYTRANSLATE(YYX)                                                \
  ((unsigned int) (YYX) <= YYMAXUTOK ? yytranslate[YYX] : YYUNDEFTOK)

/* YYTRANSLATE[TOKEN-NUM] -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex, without out-of-bounds checking.  */
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
      65,    66,    67,    68,    69,    70,    71,    72,    73
};

#if YYDEBUG
  /* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_uint16 yyrline[] =
{
       0,   202,   202,   202,   213,   218,   225,   231,   238,   245,
     255,   260,   267,   280,   339,   345,   351,   357,   365,   371,
     377,   385,   390,   397,   403,   409,   417,   424,   443,   448,
     455,   461,   467,   475,   482,   488,   495,   504,   511,   517,
     525,   526,   527,   528,   529,   530,   531,   532,   533,   534,
     535,   536,   537,   538,   539,   540,   541,   542,   543,   544,
     545,   546,   561,   562,   563,   564,   565,   569,   576,   583,
     590,   597,   604,   611,   618,   625,   632,   639,   646,   654,
     659,   666,   671,   679,   698,   719,   726,   731,   738,   744,
     750,   756,   762,   770,   777,   779,   781,   783,   787,   794,
     801,   808,   815,   822,   829,   836,   843,   850,   857,   864,
     871,   878,   885,   892,   899,   906,   913,   920,   927,   934,
     941,   948,   955
};
#endif

#if YYDEBUG || YYERROR_VERBOSE || 1
/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
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
  "$accept", "loader_start", "$@1", "loader_lines", "line", "one_line",
  "command_line", "id_command", "class_command", "class_commamd_spec",
  "attribute_list_qualifier", "attribute_list", "attribute_names",
  "attribute_name", "constructor_spec", "constructor_argument_list",
  "argument_names", "argument_name", "instance_line", "object_id",
  "constant_list", "constant", "ansi_string", "nchar_string", "dq_string",
  "sql2_date", "sql2_time", "sql2_timestamp", "sql2_timestampltz",
  "sql2_timestamptz", "utime", "sql2_datetime", "sql2_datetimeltz",
  "sql2_datetimetz", "bit_string", "object_reference", "class_identifier",
  "instance_number", "set_constant", "set_elements",
  "system_object_reference", "ref_type", "monetary", YY_NULLPTR
};
#endif

# ifdef YYPRINT
/* YYTOKNUM[NUM] -- (External) token number corresponding to the
   (internal) symbol number NUM (which must be that of a token).  */
static const yytype_uint16 yytoknum[] =
{
       0,   256,   257,   258,   259,   260,   261,   262,   263,   264,
     265,   266,   267,   268,   269,   270,   271,   272,   273,   274,
     275,   276,   277,   278,   279,   280,   281,   282,   283,   284,
     285,   286,   287,   288,   289,   290,   291,   292,   293,   294,
     295,   296,   297,   298,   299,   300,   301,   302,   303,   304,
     305,   306,   307,   308,   309,   310,   311,   312,   313,   314,
     315,   316,   317,   318,   319,   320,   321,   322,   323,   324,
     325,   326,   327,   328
};
# endif

#define YYPACT_NINF -182

#define yypact_value_is_default(Yystate) \
  (!!((Yystate) == (-182)))

#define YYTABLE_NINF -1

#define yytable_value_is_error(Yytable_value) \
  0

  /* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
     STATE-NUM.  */
static const yytype_int16 yypact[] =
{
    -182,     9,   135,  -182,  -182,  -182,   -56,   -54,   -53,   -51,
     -50,   -48,   -47,   -46,   -45,   -43,   -40,  -182,  -182,  -182,
    -182,   -17,   270,  -182,  -182,  -182,  -182,  -182,  -182,  -182,
    -182,  -182,  -182,    -4,    22,    64,    65,    66,    67,    68,
      69,    70,    71,    72,    73,    74,    75,    76,    77,    78,
      79,    80,    81,    82,    83,    84,    85,    86,    21,    45,
      48,    49,    50,   135,  -182,   119,  -182,  -182,  -182,  -182,
     337,   337,  -182,  -182,  -182,  -182,  -182,  -182,  -182,  -182,
    -182,  -182,  -182,  -182,  -182,  -182,  -182,  -182,  -182,    57,
    -182,    53,    54,    55,    58,    59,    60,    61,    63,    89,
      97,    -1,  -182,  -182,   108,  -182,  -182,    20,  -182,  -182,
    -182,  -182,  -182,  -182,  -182,  -182,  -182,  -182,  -182,  -182,
    -182,  -182,  -182,  -182,  -182,  -182,  -182,  -182,  -182,  -182,
    -182,  -182,  -182,  -182,  -182,  -182,  -182,  -182,  -182,  -182,
     337,  -182,    91,  -182,  -182,  -182,  -182,  -182,  -182,  -182,
    -182,  -182,  -182,  -182,  -182,  -182,   -27,  -182,   109,   117,
     110,  -182,   337,  -182,   203,  -182,  -182,  -182,  -182,   -28,
    -182,   117,    98,  -182,  -182,  -182,   337,  -182,  -182,    99,
    -182,  -182,   172,  -182,  -182,   -26,  -182,  -182,  -182,    62,
    -182,  -182,   143,  -182,  -182
};

  /* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
     Performed when YYTABLE does not specify something else to do.  Zero
     means the default is an error.  */
static const yytype_uint8 yydefact[] =
{
       2,     0,     0,     1,     7,    53,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,    94,    95,    96,
      97,     0,     0,    61,    60,    37,    54,    55,    56,    57,
      58,    59,    62,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     3,     4,     0,     8,    11,    10,     9,
      34,    36,    38,    40,    42,    41,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    43,    64,    65,    66,     0,
      63,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,    83,    84,    81,    86,    88,     0,    99,   100,
     102,    98,   101,   103,   104,   105,   106,   107,   108,   109,
     110,   111,   112,   113,   114,   115,   116,   117,   118,   119,
     120,   121,   122,    67,    69,    68,    79,    80,     5,     6,
      35,    39,     0,    70,    71,    75,    72,    73,    74,    76,
      77,    78,    12,    18,    19,    20,     0,    13,     0,    14,
       0,    82,     0,    87,     0,    89,    93,    21,    26,     0,
      23,    16,     0,    15,    85,    91,     0,    90,    22,     0,
      24,    17,     0,    92,    25,     0,    27,    28,    33,     0,
      30,    29,     0,    31,    32
};

  /* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -182,  -182,  -182,  -182,   146,  -182,  -182,  -182,  -182,  -182,
    -182,    52,  -182,  -162,    51,  -182,  -182,  -181,  -182,  -182,
     150,   -22,  -182,  -182,  -182,  -182,  -182,  -182,  -182,  -182,
    -182,  -182,  -182,  -182,  -182,  -182,  -182,  -182,  -182,  -182,
    -182,  -182,  -182
};

  /* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
      -1,     1,     2,    63,    64,    65,    66,    67,    68,   157,
     158,   159,   169,   170,   173,   186,   189,   190,    69,    70,
      71,    72,    73,    74,    75,    76,    77,    78,    79,    80,
      81,    82,    83,    84,    85,    86,   104,   161,    87,   107,
      88,    89,    90
};

  /* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
     positive, shift that token.  If negative, reduce the rule whose
     number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_uint8 yytable[] =
{
     106,   178,   167,   187,   153,   154,   155,   180,   193,     3,
      91,   194,    92,    93,   102,    94,    95,   184,    96,    97,
      98,    99,   100,   162,     5,   101,   108,   156,     6,     7,
       8,     9,    10,    11,    12,    13,    14,   168,   168,   188,
      17,    18,    19,    20,    21,   179,    22,   163,   103,   141,
      23,    24,   109,    26,    27,    28,    29,    30,    31,    32,
      33,    34,    35,    36,    37,    38,    39,    40,    41,    42,
      43,    44,    45,    46,    47,    48,    49,    50,    51,    52,
      53,    54,    55,    56,    57,   165,    58,    59,    60,    61,
      62,   191,   133,   164,   110,   111,   112,   113,   114,   115,
     116,   117,   118,   119,   120,   121,   122,   123,   124,   125,
     126,   127,   128,   129,   130,   131,   132,   134,   141,   135,
     136,   137,   139,   142,   143,   144,   145,   188,   152,   146,
     147,   148,   149,   160,   150,   192,   172,   156,     4,     5,
     175,   174,   177,     6,     7,     8,     9,    10,    11,    12,
      13,    14,    15,    16,   183,    17,    18,    19,    20,    21,
     151,    22,   166,   182,   168,    23,    24,    25,    26,    27,
      28,    29,    30,    31,    32,    33,    34,    35,    36,    37,
      38,    39,    40,    41,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    53,    54,    55,    56,    57,
     185,    58,    59,    60,    61,    62,   176,     5,   188,   138,
     171,     6,     7,     8,     9,    10,    11,    12,    13,    14,
     140,     0,   181,    17,    18,    19,    20,    21,     0,    22,
       0,     0,     0,    23,    24,     0,    26,    27,    28,    29,
      30,    31,    32,    33,    34,    35,    36,    37,    38,    39,
      40,    41,    42,    43,    44,    45,    46,    47,    48,    49,
      50,    51,    52,    53,    54,    55,    56,    57,     0,    58,
      59,    60,    61,    62,     5,     0,     0,     0,     6,     7,
       8,     9,    10,    11,    12,    13,    14,     0,     0,     0,
      17,    18,    19,    20,    21,     0,    22,   105,     0,     0,
      23,    24,     0,    26,    27,    28,    29,    30,    31,    32,
      33,    34,    35,    36,    37,    38,    39,    40,    41,    42,
      43,    44,    45,    46,    47,    48,    49,    50,    51,    52,
      53,    54,    55,    56,    57,     0,    58,    59,    60,    61,
      62,     5,     0,     0,     0,     6,     7,     8,     9,    10,
      11,    12,    13,    14,     0,     0,     0,    17,    18,    19,
      20,    21,     0,    22,     0,     0,     0,    23,    24,     0,
      26,    27,    28,    29,    30,    31,    32,    33,    34,    35,
      36,    37,    38,    39,    40,    41,    42,    43,    44,    45,
      46,    47,    48,    49,    50,    51,    52,    53,    54,    55,
      56,    57,     0,    58,    59,    60,    61,    62
};

static const yytype_int16 yycheck[] =
{
      22,    29,    29,    29,     5,     6,     7,   169,   189,     0,
      66,   192,    66,    66,    31,    66,    66,   179,    66,    66,
      66,    66,    65,     3,     4,    65,    30,    28,     8,     9,
      10,    11,    12,    13,    14,    15,    16,    65,    65,    65,
      20,    21,    22,    23,    24,    73,    26,    27,    65,    71,
      30,    31,    30,    33,    34,    35,    36,    37,    38,    39,
      40,    41,    42,    43,    44,    45,    46,    47,    48,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    58,    59,
      60,    61,    62,    63,    64,   107,    66,    67,    68,    69,
      70,    29,    71,    73,    30,    30,    30,    30,    30,    30,
      30,    30,    30,    30,    30,    30,    30,    30,    30,    30,
      30,    30,    30,    30,    30,    30,    30,    72,   140,    71,
      71,    71,     3,    66,    71,    71,    71,    65,    31,    71,
      71,    71,    71,    25,    71,    73,    19,    28,     3,     4,
     162,    31,   164,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,   176,    20,    21,    22,    23,    24,
      71,    26,    71,    65,    65,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    60,    61,    62,    63,    64,
      28,    66,    67,    68,    69,    70,     3,     4,    65,    63,
     158,     8,     9,    10,    11,    12,    13,    14,    15,    16,
      70,    -1,   171,    20,    21,    22,    23,    24,    -1,    26,
      -1,    -1,    -1,    30,    31,    -1,    33,    34,    35,    36,
      37,    38,    39,    40,    41,    42,    43,    44,    45,    46,
      47,    48,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    58,    59,    60,    61,    62,    63,    64,    -1,    66,
      67,    68,    69,    70,     4,    -1,    -1,    -1,     8,     9,
      10,    11,    12,    13,    14,    15,    16,    -1,    -1,    -1,
      20,    21,    22,    23,    24,    -1,    26,    27,    -1,    -1,
      30,    31,    -1,    33,    34,    35,    36,    37,    38,    39,
      40,    41,    42,    43,    44,    45,    46,    47,    48,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    58,    59,
      60,    61,    62,    63,    64,    -1,    66,    67,    68,    69,
      70,     4,    -1,    -1,    -1,     8,     9,    10,    11,    12,
      13,    14,    15,    16,    -1,    -1,    -1,    20,    21,    22,
      23,    24,    -1,    26,    -1,    -1,    -1,    30,    31,    -1,
      33,    34,    35,    36,    37,    38,    39,    40,    41,    42,
      43,    44,    45,    46,    47,    48,    49,    50,    51,    52,
      53,    54,    55,    56,    57,    58,    59,    60,    61,    62,
      63,    64,    -1,    66,    67,    68,    69,    70
};

  /* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
     symbol of state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,    75,    76,     0,     3,     4,     8,     9,    10,    11,
      12,    13,    14,    15,    16,    17,    18,    20,    21,    22,
      23,    24,    26,    30,    31,    32,    33,    34,    35,    36,
      37,    38,    39,    40,    41,    42,    43,    44,    45,    46,
      47,    48,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    58,    59,    60,    61,    62,    63,    64,    66,    67,
      68,    69,    70,    77,    78,    79,    80,    81,    82,    92,
      93,    94,    95,    96,    97,    98,    99,   100,   101,   102,
     103,   104,   105,   106,   107,   108,   109,   112,   114,   115,
     116,    66,    66,    66,    66,    66,    66,    66,    66,    66,
      65,    65,    31,    65,   110,    27,    95,   113,    30,    30,
      30,    30,    30,    30,    30,    30,    30,    30,    30,    30,
      30,    30,    30,    30,    30,    30,    30,    30,    30,    30,
      30,    30,    30,    71,    72,    71,    71,    71,    78,     3,
      94,    95,    66,    71,    71,    71,    71,    71,    71,    71,
      71,    71,    31,     5,     6,     7,    28,    83,    84,    85,
      25,   111,     3,    27,    73,    95,    71,    29,    65,    86,
      87,    85,    19,    88,    31,    95,     3,    95,    29,    73,
      87,    88,    65,    95,    87,    28,    89,    29,    65,    90,
      91,    29,    73,    91,    91
};

  /* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const yytype_uint8 yyr1[] =
{
       0,    74,    76,    75,    77,    77,    78,    78,    79,    79,
      80,    80,    81,    82,    83,    83,    83,    83,    84,    84,
      84,    85,    85,    86,    86,    86,    87,    88,    89,    89,
      90,    90,    90,    91,    92,    92,    92,    93,    94,    94,
      95,    95,    95,    95,    95,    95,    95,    95,    95,    95,
      95,    95,    95,    95,    95,    95,    95,    95,    95,    95,
      95,    95,    95,    95,    95,    95,    95,    96,    97,    98,
      99,   100,   101,   102,   103,   104,   105,   106,   107,   108,
     108,   109,   109,   110,   110,   111,   112,   112,   113,   113,
     113,   113,   113,   114,   115,   115,   115,   115,   116,   116,
     116,   116,   116,   116,   116,   116,   116,   116,   116,   116,
     116,   116,   116,   116,   116,   116,   116,   116,   116,   116,
     116,   116,   116
};

  /* YYR2[YYN] -- Number of symbols on the right hand side of rule YYN.  */
static const yytype_uint8 yyr2[] =
{
       0,     2,     0,     2,     1,     2,     2,     1,     1,     1,
       1,     1,     3,     3,     1,     2,     2,     3,     1,     1,
       1,     2,     3,     1,     2,     3,     1,     3,     2,     3,
       1,     2,     3,     1,     1,     2,     1,     1,     1,     2,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     2,     2,     2,
       3,     3,     3,     3,     3,     3,     3,     3,     3,     2,
       2,     2,     3,     1,     1,     2,     2,     3,     1,     2,
       3,     3,     4,     3,     1,     1,     1,     1,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2
};


#define yyerrok         (yyerrstatus = 0)
#define yyclearin       (yychar = YYEMPTY)
#define YYEMPTY         (-2)
#define YYEOF           0

#define YYACCEPT        goto yyacceptlab
#define YYABORT         goto yyabortlab
#define YYERROR         goto yyerrorlab


#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)                                  \
do                                                              \
  if (yychar == YYEMPTY)                                        \
    {                                                           \
      yychar = (Token);                                         \
      yylval = (Value);                                         \
      YYPOPSTACK (yylen);                                       \
      yystate = *yyssp;                                         \
      goto yybackup;                                            \
    }                                                           \
  else                                                          \
    {                                                           \
      yyerror (YY_("syntax error: cannot back up")); \
      YYERROR;                                                  \
    }                                                           \
while (0)

/* Error token number */
#define YYTERROR        1
#define YYERRCODE       256



/* Enable debugging if requested.  */
#if YYDEBUG

# ifndef YYFPRINTF
#  include <stdio.h> /* INFRINGES ON USER NAME SPACE */
#  define YYFPRINTF fprintf
# endif

# define YYDPRINTF(Args)                        \
do {                                            \
  if (yydebug)                                  \
    YYFPRINTF Args;                             \
} while (0)

/* This macro is provided for backward compatibility. */
#ifndef YY_LOCATION_PRINT
# define YY_LOCATION_PRINT(File, Loc) ((void) 0)
#endif


# define YY_SYMBOL_PRINT(Title, Type, Value, Location)                    \
do {                                                                      \
  if (yydebug)                                                            \
    {                                                                     \
      YYFPRINTF (stderr, "%s ", Title);                                   \
      yy_symbol_print (stderr,                                            \
                  Type, Value); \
      YYFPRINTF (stderr, "\n");                                           \
    }                                                                     \
} while (0)


/*----------------------------------------.
| Print this symbol's value on YYOUTPUT.  |
`----------------------------------------*/

static void
yy_symbol_value_print (FILE *yyoutput, int yytype, YYSTYPE const * const yyvaluep)
{
  FILE *yyo = yyoutput;
  YYUSE (yyo);
  if (!yyvaluep)
    return;
# ifdef YYPRINT
  if (yytype < YYNTOKENS)
    YYPRINT (yyoutput, yytoknum[yytype], *yyvaluep);
# endif
  YYUSE (yytype);
}


/*--------------------------------.
| Print this symbol on YYOUTPUT.  |
`--------------------------------*/

static void
yy_symbol_print (FILE *yyoutput, int yytype, YYSTYPE const * const yyvaluep)
{
  YYFPRINTF (yyoutput, "%s %s (",
             yytype < YYNTOKENS ? "token" : "nterm", yytname[yytype]);

  yy_symbol_value_print (yyoutput, yytype, yyvaluep);
  YYFPRINTF (yyoutput, ")");
}

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (included).                                                   |
`------------------------------------------------------------------*/

static void
yy_stack_print (yytype_int16 *yybottom, yytype_int16 *yytop)
{
  YYFPRINTF (stderr, "Stack now");
  for (; yybottom <= yytop; yybottom++)
    {
      int yybot = *yybottom;
      YYFPRINTF (stderr, " %d", yybot);
    }
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)                            \
do {                                                            \
  if (yydebug)                                                  \
    yy_stack_print ((Bottom), (Top));                           \
} while (0)


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

static void
yy_reduce_print (yytype_int16 *yyssp, YYSTYPE *yyvsp, int yyrule)
{
  unsigned long int yylno = yyrline[yyrule];
  int yynrhs = yyr2[yyrule];
  int yyi;
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %lu):\n",
             yyrule - 1, yylno);
  /* The symbols being reduced.  */
  for (yyi = 0; yyi < yynrhs; yyi++)
    {
      YYFPRINTF (stderr, "   $%d = ", yyi + 1);
      yy_symbol_print (stderr,
                       yystos[yyssp[yyi + 1 - yynrhs]],
                       &(yyvsp[(yyi + 1) - (yynrhs)])
                                              );
      YYFPRINTF (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)          \
do {                                    \
  if (yydebug)                          \
    yy_reduce_print (yyssp, yyvsp, Rule); \
} while (0)

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
#ifndef YYINITDEPTH
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
static YYSIZE_T
yystrlen (const char *yystr)
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
static char *
yystpcpy (char *yydest, const char *yysrc)
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

/* Copy into *YYMSG, which is of size *YYMSG_ALLOC, an error message
   about the unexpected token YYTOKEN for the state stack whose top is
   YYSSP.

   Return 0 if *YYMSG was successfully written.  Return 1 if *YYMSG is
   not large enough to hold the message.  In that case, also set
   *YYMSG_ALLOC to the required number of bytes.  Return 2 if the
   required number of bytes is too large to store.  */
static int
yysyntax_error (YYSIZE_T *yymsg_alloc, char **yymsg,
                yytype_int16 *yyssp, int yytoken)
{
  YYSIZE_T yysize0 = yytnamerr (YY_NULLPTR, yytname[yytoken]);
  YYSIZE_T yysize = yysize0;
  enum { YYERROR_VERBOSE_ARGS_MAXIMUM = 5 };
  /* Internationalized format string. */
  const char *yyformat = YY_NULLPTR;
  /* Arguments of yyformat. */
  char const *yyarg[YYERROR_VERBOSE_ARGS_MAXIMUM];
  /* Number of reported tokens (one for the "unexpected", one per
     "expected"). */
  int yycount = 0;

  /* There are many possibilities here to consider:
     - If this state is a consistent state with a default action, then
       the only way this function was invoked is if the default action
       is an error action.  In that case, don't check for expected
       tokens because there are none.
     - The only way there can be no lookahead present (in yychar) is if
       this state is a consistent state with a default action.  Thus,
       detecting the absence of a lookahead is sufficient to determine
       that there is no unexpected or expected token to report.  In that
       case, just report a simple "syntax error".
     - Don't assume there isn't a lookahead just because this state is a
       consistent state with a default action.  There might have been a
       previous inconsistent state, consistent state with a non-default
       action, or user semantic action that manipulated yychar.
     - Of course, the expected token list depends on states to have
       correct lookahead information, and it depends on the parser not
       to perform extra reductions after fetching a lookahead from the
       scanner and before detecting a syntax error.  Thus, state merging
       (from LALR or IELR) and default reductions corrupt the expected
       token list.  However, the list is correct for canonical LR with
       one exception: it will still contain any token that will not be
       accepted due to an error action in a later state.
  */
  if (yytoken != YYEMPTY)
    {
      int yyn = yypact[*yyssp];
      yyarg[yycount++] = yytname[yytoken];
      if (!yypact_value_is_default (yyn))
        {
          /* Start YYX at -YYN if negative to avoid negative indexes in
             YYCHECK.  In other words, skip the first -YYN actions for
             this state because they are default actions.  */
          int yyxbegin = yyn < 0 ? -yyn : 0;
          /* Stay within bounds of both yycheck and yytname.  */
          int yychecklim = YYLAST - yyn + 1;
          int yyxend = yychecklim < YYNTOKENS ? yychecklim : YYNTOKENS;
          int yyx;

          for (yyx = yyxbegin; yyx < yyxend; ++yyx)
            if (yycheck[yyx + yyn] == yyx && yyx != YYTERROR
                && !yytable_value_is_error (yytable[yyx + yyn]))
              {
                if (yycount == YYERROR_VERBOSE_ARGS_MAXIMUM)
                  {
                    yycount = 1;
                    yysize = yysize0;
                    break;
                  }
                yyarg[yycount++] = yytname[yyx];
                {
                  YYSIZE_T yysize1 = yysize + yytnamerr (YY_NULLPTR, yytname[yyx]);
                  if (! (yysize <= yysize1
                         && yysize1 <= YYSTACK_ALLOC_MAXIMUM))
                    return 2;
                  yysize = yysize1;
                }
              }
        }
    }

  switch (yycount)
    {
# define YYCASE_(N, S)                      \
      case N:                               \
        yyformat = S;                       \
      break
      YYCASE_(0, YY_("syntax error"));
      YYCASE_(1, YY_("syntax error, unexpected %s"));
      YYCASE_(2, YY_("syntax error, unexpected %s, expecting %s"));
      YYCASE_(3, YY_("syntax error, unexpected %s, expecting %s or %s"));
      YYCASE_(4, YY_("syntax error, unexpected %s, expecting %s or %s or %s"));
      YYCASE_(5, YY_("syntax error, unexpected %s, expecting %s or %s or %s or %s"));
# undef YYCASE_
    }

  {
    YYSIZE_T yysize1 = yysize + yystrlen (yyformat);
    if (! (yysize <= yysize1 && yysize1 <= YYSTACK_ALLOC_MAXIMUM))
      return 2;
    yysize = yysize1;
  }

  if (*yymsg_alloc < yysize)
    {
      *yymsg_alloc = 2 * yysize;
      if (! (yysize <= *yymsg_alloc
             && *yymsg_alloc <= YYSTACK_ALLOC_MAXIMUM))
        *yymsg_alloc = YYSTACK_ALLOC_MAXIMUM;
      return 1;
    }

  /* Avoid sprintf, as that infringes on the user's name space.
     Don't have undefined behavior even if the translation
     produced a string with the wrong number of "%s"s.  */
  {
    char *yyp = *yymsg;
    int yyi = 0;
    while ((*yyp = *yyformat) != '\0')
      if (*yyp == '%' && yyformat[1] == 's' && yyi < yycount)
        {
          yyp += yytnamerr (yyp, yyarg[yyi++]);
          yyformat += 2;
        }
      else
        {
          yyp++;
          yyformat++;
        }
  }
  return 0;
}
#endif /* YYERROR_VERBOSE */

/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

static void
yydestruct (const char *yymsg, int yytype, YYSTYPE *yyvaluep)
{
  YYUSE (yyvaluep);
  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yytype, yyvaluep, yylocationp);

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YYUSE (yytype);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}




/* The lookahead symbol.  */
int yychar;

/* The semantic value of the lookahead symbol.  */
YYSTYPE yylval;
/* Number of syntax errors so far.  */
int yynerrs;


/*----------.
| yyparse.  |
`----------*/

int
yyparse (void)
{
    int yystate;
    /* Number of tokens to shift before error messages enabled.  */
    int yyerrstatus;

    /* The stacks and their tools:
       'yyss': related to states.
       'yyvs': related to semantic values.

       Refer to the stacks through separate pointers, to allow yyoverflow
       to reallocate them elsewhere.  */

    /* The state stack.  */
    yytype_int16 yyssa[YYINITDEPTH];
    yytype_int16 *yyss;
    yytype_int16 *yyssp;

    /* The semantic value stack.  */
    YYSTYPE yyvsa[YYINITDEPTH];
    YYSTYPE *yyvs;
    YYSTYPE *yyvsp;

    YYSIZE_T yystacksize;

  int yyn;
  int yyresult;
  /* Lookahead token as an internal (translated) token number.  */
  int yytoken = 0;
  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;

#if YYERROR_VERBOSE
  /* Buffer for error messages, and its allocated size.  */
  char yymsgbuf[128];
  char *yymsg = yymsgbuf;
  YYSIZE_T yymsg_alloc = sizeof yymsgbuf;
#endif

#define YYPOPSTACK(N)   (yyvsp -= (N), yyssp -= (N))

  /* The number of symbols on the RHS of the reduced rule.
     Keep to zero when no symbol should be popped.  */
  int yylen = 0;

  yyssp = yyss = yyssa;
  yyvsp = yyvs = yyvsa;
  yystacksize = YYINITDEPTH;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yystate = 0;
  yyerrstatus = 0;
  yynerrs = 0;
  yychar = YYEMPTY; /* Cause a token to be read.  */
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
        YYSTACK_RELOCATE (yyss_alloc, yyss);
        YYSTACK_RELOCATE (yyvs_alloc, yyvs);
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

  if (yystate == YYFINAL)
    YYACCEPT;

  goto yybackup;

/*-----------.
| yybackup.  |
`-----------*/
yybackup:

  /* Do appropriate processing given the current state.  Read a
     lookahead token if we need one and don't already have one.  */

  /* First try to decide what to do without reference to lookahead token.  */
  yyn = yypact[yystate];
  if (yypact_value_is_default (yyn))
    goto yydefault;

  /* Not known => get a lookahead token if don't already have one.  */

  /* YYCHAR is either YYEMPTY or YYEOF or a valid lookahead symbol.  */
  if (yychar == YYEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token: "));
      yychar = yylex ();
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
      if (yytable_value_is_error (yyn))
        goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }

  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  /* Shift the lookahead token.  */
  YY_SYMBOL_PRINT ("Shifting", yytoken, &yylval, &yylloc);

  /* Discard the shifted token.  */
  yychar = YYEMPTY;

  yystate = yyn;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END

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
     '$$ = $1'.

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

    {
    loader_initialize_lexer ();
    constant_Pool_idx = 0;
  }

    break;

  case 3:

    {
    ldr_act_finish (ldr_Current_context, 0);
  }

    break;

  case 4:

    {
    DBG_PRINT ("line");
  }

    break;

  case 5:

    {
    DBG_PRINT ("line_list line");
  }

    break;

  case 6:

    {
    DBG_PRINT ("one_line");
    loader_In_instance_line = true;
  }

    break;

  case 7:

    {
    loader_In_instance_line = true;
  }

    break;

  case 8:

    {
    DBG_PRINT ("command_line");
    loader_reset_string_pool ();
    constant_Pool_idx = 0;
  }

    break;

  case 9:

    {
    DBG_PRINT ("instance_line");
    ldr_act_finish_line (ldr_Current_context);
    loader_reset_string_pool ();
    constant_Pool_idx = 0;
  }

    break;

  case 10:

    {
    DBG_PRINT ("class_command");
  }

    break;

  case 11:

    {
    DBG_PRINT ("id_command");
  }

    break;

  case 12:

    {
    skip_current_class = false;

    ldr_act_start_id (ldr_Current_context, (yyvsp[-1].string)->val);
    ldr_act_set_id (ldr_Current_context, atoi ((yyvsp[0].string)->val));

    FREE_STRING ((yyvsp[-1].string));
    FREE_STRING ((yyvsp[0].string));
  }

    break;

  case 13:

    {
    LDR_CLASS_COMMAND_SPEC *cmd_spec;
    LDR_STRING *class_name;
    LDR_STRING *attr, *save, *args;

    DBG_PRINT ("class_commamd_spec");

    class_name = (yyvsp[-1].string);
    cmd_spec = (yyvsp[0].cmd_spec);

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
  }

    break;

  case 14:

    {
    DBG_PRINT ("attribute_list");
    (yyval.cmd_spec) = loader_make_class_command_spec (LDR_ATTRIBUTE_ANY, (yyvsp[0].string), NULL);
  }

    break;

  case 15:

    {
    DBG_PRINT ("attribute_list constructor_spec");
    (yyval.cmd_spec) = loader_make_class_command_spec (LDR_ATTRIBUTE_ANY, (yyvsp[-1].string), (yyvsp[0].ctor_spec));
  }

    break;

  case 16:

    {
    DBG_PRINT ("attribute_list_qualifier attribute_list");
    (yyval.cmd_spec) = loader_make_class_command_spec ((yyvsp[-1].intval), (yyvsp[0].string), NULL);
  }

    break;

  case 17:

    {
    DBG_PRINT ("attribute_list_qualifier attribute_list constructor_spec");
    (yyval.cmd_spec) = loader_make_class_command_spec ((yyvsp[-2].intval), (yyvsp[-1].string), (yyvsp[0].ctor_spec));
  }

    break;

  case 18:

    {
    DBG_PRINT ("CLASS");
    (yyval.intval) = LDR_ATTRIBUTE_CLASS;
  }

    break;

  case 19:

    {
    DBG_PRINT ("SHARED");
    (yyval.intval) = LDR_ATTRIBUTE_SHARED;
  }

    break;

  case 20:

    {
    DBG_PRINT ("DEFAULT");
    (yyval.intval) = LDR_ATTRIBUTE_DEFAULT;
  }

    break;

  case 21:

    {
    (yyval.string) = NULL;
  }

    break;

  case 22:

    {
    (yyval.string) = (yyvsp[-1].string);
  }

    break;

  case 23:

    {
    DBG_PRINT ("attribute_name");
    (yyval.string) = loader_append_string_list (NULL, (yyvsp[0].string));
  }

    break;

  case 24:

    {
    DBG_PRINT ("attribute_names attribute_name");
    (yyval.string) = loader_append_string_list ((yyvsp[-1].string), (yyvsp[0].string));
  }

    break;

  case 25:

    {
    DBG_PRINT ("attribute_names COMMA attribute_name");
    (yyval.string) = loader_append_string_list ((yyvsp[-2].string), (yyvsp[0].string));
  }

    break;

  case 26:

    {
    (yyval.string) = (yyvsp[0].string);
  }

    break;

  case 27:

    {
    LDR_CONSTRUCTOR_SPEC *spec;

    spec = (LDR_CONSTRUCTOR_SPEC *) malloc (sizeof (LDR_CONSTRUCTOR_SPEC));
    if (spec == NULL)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	        ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (LDR_CONSTRUCTOR_SPEC));
	YYABORT;
      }

    spec->idname = (yyvsp[-1].string);
    spec->arg_list = (yyvsp[0].string);
    (yyval.ctor_spec) = spec;
  }

    break;

  case 28:

    {
    (yyval.string) = NULL;
  }

    break;

  case 29:

    {
    (yyval.string) = (yyvsp[-1].string);
  }

    break;

  case 30:

    {
    DBG_PRINT ("argument_name");
    (yyval.string) = loader_append_string_list (NULL, (yyvsp[0].string));
  }

    break;

  case 31:

    {
    DBG_PRINT ("argument_names argument_name");
    (yyval.string) = loader_append_string_list ((yyvsp[-1].string), (yyvsp[0].string));
  }

    break;

  case 32:

    {
    DBG_PRINT ("argument_names COMMA argument_name");
    (yyval.string) = loader_append_string_list ((yyvsp[-2].string), (yyvsp[0].string));
  }

    break;

  case 33:

    {
    (yyval.string) = (yyvsp[0].string);
  }

    break;

  case 34:

    {
    skip_current_instance = false;
    ldr_act_start_instance (ldr_Current_context, (yyvsp[0].intval), NULL);
  }

    break;

  case 35:

    {
    skip_current_instance = false;
    ldr_act_start_instance (ldr_Current_context, (yyvsp[-1].intval), (yyvsp[0].constant));
    ldr_process_constants ((yyvsp[0].constant));
  }

    break;

  case 36:

    {
    skip_current_instance = false;
    ldr_act_start_instance (ldr_Current_context, -1, (yyvsp[0].constant));
    ldr_process_constants ((yyvsp[0].constant));
  }

    break;

  case 37:

    {
    (yyval.intval) = (yyvsp[0].intval);
  }

    break;

  case 38:

    {
    DBG_PRINT ("constant");
    (yyval.constant) = loader_append_constant_list (NULL, (yyvsp[0].constant));
  }

    break;

  case 39:

    {
    DBG_PRINT ("constant_list constant");
    (yyval.constant) = loader_append_constant_list ((yyvsp[-1].constant), (yyvsp[0].constant));
  }

    break;

  case 40:

    { (yyval.constant) = (yyvsp[0].constant); }

    break;

  case 41:

    { (yyval.constant) = (yyvsp[0].constant); }

    break;

  case 42:

    { (yyval.constant) = (yyvsp[0].constant); }

    break;

  case 43:

    { (yyval.constant) = (yyvsp[0].constant); }

    break;

  case 44:

    { (yyval.constant) = (yyvsp[0].constant); }

    break;

  case 45:

    { (yyval.constant) = (yyvsp[0].constant); }

    break;

  case 46:

    { (yyval.constant) = (yyvsp[0].constant); }

    break;

  case 47:

    { (yyval.constant) = (yyvsp[0].constant); }

    break;

  case 48:

    { (yyval.constant) = (yyvsp[0].constant); }

    break;

  case 49:

    { (yyval.constant) = (yyvsp[0].constant); }

    break;

  case 50:

    { (yyval.constant) = (yyvsp[0].constant); }

    break;

  case 51:

    { (yyval.constant) = (yyvsp[0].constant); }

    break;

  case 52:

    { (yyval.constant) = (yyvsp[0].constant); }

    break;

  case 53:

    { (yyval.constant) = loader_make_constant(LDR_NULL, NULL); }

    break;

  case 54:

    { (yyval.constant) = loader_make_constant(LDR_TIME, (yyvsp[0].string)); }

    break;

  case 55:

    { (yyval.constant) = loader_make_constant(LDR_TIME, (yyvsp[0].string)); }

    break;

  case 56:

    { (yyval.constant) = loader_make_constant(LDR_TIME, (yyvsp[0].string)); }

    break;

  case 57:

    { (yyval.constant) = loader_make_constant(LDR_TIME, (yyvsp[0].string)); }

    break;

  case 58:

    { (yyval.constant) = loader_make_constant(LDR_TIME, (yyvsp[0].string)); }

    break;

  case 59:

    { (yyval.constant) = loader_make_constant(LDR_TIME, (yyvsp[0].string)); }

    break;

  case 60:

    { (yyval.constant) = loader_make_constant(LDR_INT, (yyvsp[0].string)); }

    break;

  case 61:

    {
    if (strchr ((yyvsp[0].string)->val, 'F') != NULL || strchr ((yyvsp[0].string)->val, 'f') != NULL)
      {
        (yyval.constant) = loader_make_constant (LDR_FLOAT, (yyvsp[0].string));
      }
    else if (strchr ((yyvsp[0].string)->val, 'E') != NULL || strchr ((yyvsp[0].string)->val, 'e') != NULL)
      {
        (yyval.constant) = loader_make_constant (LDR_DOUBLE, (yyvsp[0].string));
      }
    else
      {
        (yyval.constant) = loader_make_constant (LDR_NUMERIC, (yyvsp[0].string));
      }
  }

    break;

  case 62:

    { (yyval.constant) = loader_make_constant(LDR_DATE, (yyvsp[0].string)); }

    break;

  case 63:

    { (yyval.constant) = (yyvsp[0].constant); }

    break;

  case 64:

    { (yyval.constant) = (yyvsp[0].constant); }

    break;

  case 65:

    { (yyval.constant) = (yyvsp[0].constant); }

    break;

  case 66:

    { (yyval.constant) = (yyvsp[0].constant); }

    break;

  case 67:

    {
    (yyval.constant) = loader_make_constant (LDR_STR, (yyvsp[0].string));
  }

    break;

  case 68:

    {
    (yyval.constant) = loader_make_constant (LDR_NSTR, (yyvsp[0].string));
  }

    break;

  case 69:

    {
    (yyval.constant) = loader_make_constant (LDR_STR, (yyvsp[0].string));
  }

    break;

  case 70:

    {
    (yyval.constant) = loader_make_constant (LDR_DATE, (yyvsp[0].string));
  }

    break;

  case 71:

    {
    (yyval.constant) = loader_make_constant (LDR_TIME, (yyvsp[0].string));
  }

    break;

  case 72:

    {
    (yyval.constant) = loader_make_constant (LDR_TIMESTAMP, (yyvsp[0].string));
  }

    break;

  case 73:

    {
    (yyval.constant) = loader_make_constant (LDR_TIMESTAMPLTZ, (yyvsp[0].string));
  }

    break;

  case 74:

    {
    (yyval.constant) = loader_make_constant (LDR_TIMESTAMPTZ, (yyvsp[0].string));
  }

    break;

  case 75:

    {
    (yyval.constant) = loader_make_constant (LDR_TIMESTAMP, (yyvsp[0].string));
  }

    break;

  case 76:

    {
    (yyval.constant) = loader_make_constant (LDR_DATETIME, (yyvsp[0].string));
  }

    break;

  case 77:

    {
    (yyval.constant) = loader_make_constant (LDR_DATETIMELTZ, (yyvsp[0].string));
  }

    break;

  case 78:

    {
    (yyval.constant) = loader_make_constant (LDR_DATETIMETZ, (yyvsp[0].string));
  }

    break;

  case 79:

    {
    (yyval.constant) = loader_make_constant (LDR_BSTR, (yyvsp[0].string));
  }

    break;

  case 80:

    {
    (yyval.constant) = loader_make_constant (LDR_XSTR, (yyvsp[0].string));
  }

    break;

  case 81:

    {
    (yyval.constant) = loader_make_constant (LDR_CLASS_OID, (yyvsp[0].obj_ref));
  }

    break;

  case 82:

    {
    (yyvsp[-1].obj_ref)->instance_number = (yyvsp[0].string);
    (yyval.constant) = loader_make_constant (LDR_OID, (yyvsp[-1].obj_ref));
  }

    break;

  case 83:

    {
    LDR_OBJECT_REF *ref;

    ref = (LDR_OBJECT_REF *) malloc (sizeof (LDR_OBJECT_REF));
    if (ref == NULL)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	        ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (LDR_OBJECT_REF));
	YYABORT;
      }

    ref->class_id = (yyvsp[0].string);
    ref->class_name = NULL;
    ref->instance_number = NULL;
    
    (yyval.obj_ref) = ref;
  }

    break;

  case 84:

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
    ref->class_name = (yyvsp[0].string);
    ref->instance_number = NULL;
    
    (yyval.obj_ref) = ref;
  }

    break;

  case 85:

    {
    (yyval.string) = (yyvsp[0].string);
  }

    break;

  case 86:

    {
    (yyval.constant) = loader_make_constant (LDR_COLLECTION, NULL);
  }

    break;

  case 87:

    {
    (yyval.constant) = loader_make_constant (LDR_COLLECTION, (yyvsp[-1].constant));
  }

    break;

  case 88:

    {
    DBG_PRINT ("constant");
    (yyval.constant) = loader_append_constant_list (NULL, (yyvsp[0].constant));
  }

    break;

  case 89:

    {
    DBG_PRINT ("set_elements constant");
    (yyval.constant) = loader_append_constant_list ((yyvsp[-1].constant), (yyvsp[0].constant));
  }

    break;

  case 90:

    {
    DBG_PRINT ("set_elements COMMA constant");
    (yyval.constant) = loader_append_constant_list ((yyvsp[-2].constant), (yyvsp[0].constant));
  }

    break;

  case 91:

    {
    DBG_PRINT ("set_elements NL constant");
    (yyval.constant) = loader_append_constant_list ((yyvsp[-2].constant), (yyvsp[0].constant));
  }

    break;

  case 92:

    {
    DBG_PRINT ("set_elements COMMA NL constant");
    (yyval.constant) = loader_append_constant_list ((yyvsp[-3].constant), (yyvsp[0].constant));
  }

    break;

  case 93:

    {
    (yyval.constant) = loader_make_constant ((yyvsp[-2].intval), (yyvsp[0].string));
  }

    break;

  case 94:

    { (yyval.intval) = LDR_ELO_INT; }

    break;

  case 95:

    { (yyval.intval) = LDR_ELO_EXT; }

    break;

  case 96:

    { (yyval.intval) = LDR_SYS_USER; }

    break;

  case 97:

    { (yyval.intval) = LDR_SYS_CLASS; }

    break;

  case 98:

    {
    LDR_MONETARY_VALUE *mon_value = loader_make_monetary_value (DB_CURRENCY_DOLLAR, (yyvsp[0].string));
    
    (yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
  }

    break;

  case 99:

    {
    LDR_MONETARY_VALUE *mon_value = loader_make_monetary_value (DB_CURRENCY_YEN, (yyvsp[0].string));
    
    (yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
  }

    break;

  case 100:

    {
    LDR_MONETARY_VALUE *mon_value = loader_make_monetary_value (DB_CURRENCY_WON, (yyvsp[0].string));
    
    (yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
  }

    break;

  case 101:

    {
    LDR_MONETARY_VALUE *mon_value = loader_make_monetary_value (DB_CURRENCY_TL, (yyvsp[0].string));
    
    (yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
  }

    break;

  case 102:

    {
    LDR_MONETARY_VALUE *mon_value = loader_make_monetary_value (DB_CURRENCY_WON, (yyvsp[0].string));
    
    (yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
  }

    break;

  case 103:

    {
    LDR_MONETARY_VALUE *mon_value = loader_make_monetary_value (DB_CURRENCY_BRITISH_POUND, (yyvsp[0].string));
    
    (yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
  }

    break;

  case 104:

    {
    LDR_MONETARY_VALUE *mon_value = loader_make_monetary_value (DB_CURRENCY_CAMBODIAN_RIEL, (yyvsp[0].string));
    
    (yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
  }

    break;

  case 105:

    {
    LDR_MONETARY_VALUE *mon_value = loader_make_monetary_value (DB_CURRENCY_CHINESE_RENMINBI, (yyvsp[0].string));
    
    (yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
  }

    break;

  case 106:

    {
    LDR_MONETARY_VALUE *mon_value = loader_make_monetary_value (DB_CURRENCY_INDIAN_RUPEE, (yyvsp[0].string));
    
    (yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
  }

    break;

  case 107:

    {
    LDR_MONETARY_VALUE *mon_value = loader_make_monetary_value (DB_CURRENCY_RUSSIAN_RUBLE, (yyvsp[0].string));
    
    (yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
  }

    break;

  case 108:

    {
    LDR_MONETARY_VALUE *mon_value = loader_make_monetary_value (DB_CURRENCY_AUSTRALIAN_DOLLAR, (yyvsp[0].string));
    
    (yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
  }

    break;

  case 109:

    {
    LDR_MONETARY_VALUE *mon_value = loader_make_monetary_value (DB_CURRENCY_CANADIAN_DOLLAR, (yyvsp[0].string));
    
    (yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
  }

    break;

  case 110:

    {
    LDR_MONETARY_VALUE *mon_value = loader_make_monetary_value (DB_CURRENCY_BRASILIAN_REAL, (yyvsp[0].string));
    
    (yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
  }

    break;

  case 111:

    {
    LDR_MONETARY_VALUE *mon_value = loader_make_monetary_value (DB_CURRENCY_ROMANIAN_LEU, (yyvsp[0].string));
    
    (yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
  }

    break;

  case 112:

    {
    LDR_MONETARY_VALUE *mon_value = loader_make_monetary_value (DB_CURRENCY_EURO, (yyvsp[0].string));
    
    (yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
  }

    break;

  case 113:

    {
    LDR_MONETARY_VALUE *mon_value = loader_make_monetary_value (DB_CURRENCY_SWISS_FRANC, (yyvsp[0].string));
    
    (yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
  }

    break;

  case 114:

    {
    LDR_MONETARY_VALUE *mon_value = loader_make_monetary_value (DB_CURRENCY_DANISH_KRONE, (yyvsp[0].string));
    
    (yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
  }

    break;

  case 115:

    {
    LDR_MONETARY_VALUE *mon_value = loader_make_monetary_value (DB_CURRENCY_NORWEGIAN_KRONE, (yyvsp[0].string));
    
    (yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
  }

    break;

  case 116:

    {
    LDR_MONETARY_VALUE *mon_value = loader_make_monetary_value (DB_CURRENCY_BULGARIAN_LEV, (yyvsp[0].string));
    
    (yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
  }

    break;

  case 117:

    {
    LDR_MONETARY_VALUE *mon_value = loader_make_monetary_value (DB_CURRENCY_VIETNAMESE_DONG, (yyvsp[0].string));
    
    (yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
  }

    break;

  case 118:

    {
    LDR_MONETARY_VALUE *mon_value = loader_make_monetary_value (DB_CURRENCY_CZECH_KORUNA, (yyvsp[0].string));
    
    (yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
  }

    break;

  case 119:

    {
    LDR_MONETARY_VALUE *mon_value = loader_make_monetary_value (DB_CURRENCY_POLISH_ZLOTY, (yyvsp[0].string));
    
    (yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
  }

    break;

  case 120:

    {
    LDR_MONETARY_VALUE *mon_value = loader_make_monetary_value (DB_CURRENCY_SWEDISH_KRONA, (yyvsp[0].string));
    
    (yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
  }

    break;

  case 121:

    {
    LDR_MONETARY_VALUE *mon_value = loader_make_monetary_value (DB_CURRENCY_CROATIAN_KUNA, (yyvsp[0].string));
    
    (yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
  }

    break;

  case 122:

    {
    LDR_MONETARY_VALUE *mon_value = loader_make_monetary_value (DB_CURRENCY_SERBIAN_DINAR, (yyvsp[0].string));
    
    (yyval.constant) = loader_make_constant (LDR_MONETARY, mon_value);
  }

    break;



      default: break;
    }
  /* User semantic actions sometimes alter yychar, and that requires
     that yytoken be updated with the new translation.  We take the
     approach of translating immediately before every use of yytoken.
     One alternative is translating here after every semantic action,
     but that translation would be missed if the semantic action invokes
     YYABORT, YYACCEPT, or YYERROR immediately after altering yychar or
     if it invokes YYBACKUP.  In the case of YYABORT or YYACCEPT, an
     incorrect destructor might then be invoked immediately.  In the
     case of YYERROR or YYBACKUP, subsequent parser actions might lead
     to an incorrect destructor call or verbose syntax error message
     before the lookahead is translated.  */
  YY_SYMBOL_PRINT ("-> $$ =", yyr1[yyn], &yyval, &yyloc);

  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);

  *++yyvsp = yyval;

  /* Now 'shift' the result of the reduction.  Determine what state
     that goes to, based on the state we popped back to and the rule
     number reduced by.  */

  yyn = yyr1[yyn];

  yystate = yypgoto[yyn - YYNTOKENS] + *yyssp;
  if (0 <= yystate && yystate <= YYLAST && yycheck[yystate] == *yyssp)
    yystate = yytable[yystate];
  else
    yystate = yydefgoto[yyn - YYNTOKENS];

  goto yynewstate;


/*--------------------------------------.
| yyerrlab -- here on detecting error.  |
`--------------------------------------*/
yyerrlab:
  /* Make sure we have latest lookahead translation.  See comments at
     user semantic actions for why this is necessary.  */
  yytoken = yychar == YYEMPTY ? YYEMPTY : YYTRANSLATE (yychar);

  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
#if ! YYERROR_VERBOSE
      yyerror (YY_("syntax error"));
#else
# define YYSYNTAX_ERROR yysyntax_error (&yymsg_alloc, &yymsg, \
                                        yyssp, yytoken)
      {
        char const *yymsgp = YY_("syntax error");
        int yysyntax_error_status;
        yysyntax_error_status = YYSYNTAX_ERROR;
        if (yysyntax_error_status == 0)
          yymsgp = yymsg;
        else if (yysyntax_error_status == 1)
          {
            if (yymsg != yymsgbuf)
              YYSTACK_FREE (yymsg);
            yymsg = (char *) YYSTACK_ALLOC (yymsg_alloc);
            if (!yymsg)
              {
                yymsg = yymsgbuf;
                yymsg_alloc = sizeof yymsgbuf;
                yysyntax_error_status = 2;
              }
            else
              {
                yysyntax_error_status = YYSYNTAX_ERROR;
                yymsgp = yymsg;
              }
          }
        yyerror (yymsgp);
        if (yysyntax_error_status == 2)
          goto yyexhaustedlab;
      }
# undef YYSYNTAX_ERROR
#endif
    }



  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse lookahead token after an
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

  /* Else will try to reuse lookahead token after shifting the error
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

  /* Do not reclaim the symbols of the rule whose action triggered
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
  yyerrstatus = 3;      /* Each real token shifted decrements this.  */

  for (;;)
    {
      yyn = yypact[yystate];
      if (!yypact_value_is_default (yyn))
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

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END


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

#if !defined yyoverflow || YYERROR_VERBOSE
/*-------------------------------------------------.
| yyexhaustedlab -- memory exhaustion comes here.  |
`-------------------------------------------------*/
yyexhaustedlab:
  yyerror (YY_("memory exhausted"));
  yyresult = 2;
  /* Fall through.  */
#endif

yyreturn:
  if (yychar != YYEMPTY)
    {
      /* Make sure we have latest lookahead translation.  See comments at
         user semantic actions for why this is necessary.  */
      yytoken = YYTRANSLATE (yychar);
      yydestruct ("Cleanup: discarding lookahead",
                  yytoken, &yylval);
    }
  /* Do not reclaim the symbols of the rule whose action triggered
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
  return yyresult;
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
