/* A Bison parser, made by GNU Bison 2.3.  */

/* Skeleton implementation for Bison GLR parsers in C

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

/* C GLR parser skeleton written by Paul Hilfinger.  */

/* Identify Bison output.  */
#define YYBISON 1

/* Bison version.  */
#define YYBISON_VERSION "2.3"

/* Skeleton name.  */
#define YYSKELETON_NAME "glr.c"

/* Pure parsers.  */
#define YYPURE 0

/* Using locations.  */
#define YYLSP_NEEDED 0


/* Substitute the variable and function names.  */
#define yyparse esql_yyparse
#define yylex   esql_yylex
#define yyerror esql_yyerror
#define yylval  esql_yylval
#define yychar  esql_yychar
#define yydebug esql_yydebug
#define yynerrs esql_yynerrs
#define yylloc  esql_yylloc



#include "esql_grammar.h"

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

/* Default (constant) value used for initialization for null
   right-hand sides.  Unlike the standard yacc.c template,
   here we set the default value of $$ to a zeroed-out value.
   Since the default value is undefined, this behavior is
   technically correct.  */
static YYSTYPE yyval_default;

/* Copy the second part of user declarations.  */


/* Line 234 of glr.c.  */
#line 97 "../../src/executables/esql_grammar.c"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

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

#ifndef YYFREE
# define YYFREE free
#endif
#ifndef YYMALLOC
# define YYMALLOC malloc
#endif
#ifndef YYREALLOC
# define YYREALLOC realloc
#endif

#define YYSIZEMAX ((size_t) -1)

#ifdef __cplusplus
   typedef bool yybool;
#else
   typedef unsigned char yybool;
#endif
#define yytrue 1
#define yyfalse 0

#ifndef YYSETJMP
# include <setjmp.h>
# define YYJMP_BUF jmp_buf
# define YYSETJMP(env) setjmp (env)
# define YYLONGJMP(env, val) longjmp (env, val)
#endif

/*-----------------.
| GCC extensions.  |
`-----------------*/

#ifndef __attribute__
/* This feature is available in gcc versions 2.5 and later.  */
# if (! defined __GNUC__ || __GNUC__ < 2 \
      || (__GNUC__ == 2 && __GNUC_MINOR__ < 5) || __STRICT_ANSI__)
#  define __attribute__(Spec) /* empty */
# endif
#endif


#ifdef __cplusplus
# define YYOPTIONAL_LOC(Name) /* empty */
#else
# define YYOPTIONAL_LOC(Name) Name __attribute__ ((__unused__))
#endif

#ifndef YYASSERT
# define YYASSERT(condition) ((void) ((condition) || (abort (), 0)))
#endif

/* YYFINAL -- State number of the termination state.  */
#define YYFINAL  10
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   487

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  206
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  173
/* YYNRULES -- Number of rules.  */
#define YYNRULES  346
/* YYNRULES -- Number of states.  */
#define YYNSTATES  480
/* YYMAXRHS -- Maximum number of symbols on right-hand side of rule.  */
#define YYMAXRHS 12
/* YYMAXLEFT -- Maximum number of symbols to the left of a handle
   accessed by $0, $-1, etc., in any rule.  */
#define YYMAXLEFT 0

/* YYTRANSLATE(X) -- Bison symbol number corresponding to X.  */
#define YYUNDEFTOK  2
#define YYMAXUTOK   446

#define YYTRANSLATE(YYX)						\
  ((YYX <= 0) ? YYEOF :							\
   (unsigned int) (YYX) <= YYMAXUTOK ? yytranslate[YYX] : YYUNDEFTOK)

/* YYTRANSLATE[YYLEX] -- Bison symbol number corresponding to YYLEX.  */
static const unsigned char yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,   203,     2,     2,   204,     2,
     198,   199,   200,     2,   196,     2,   205,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,   195,   194,
       2,   197,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,   201,     2,   202,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,   192,     2,   193,     2,     2,     2,     2,
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
      65,    66,    67,    68,    69,    70,    71,    72,    73,    74,
      75,    76,    77,    78,    79,    80,    81,    82,    83,    84,
      85,    86,    87,    88,    89,    90,    91,    92,    93,    94,
      95,    96,    97,    98,    99,   100,   101,   102,   103,   104,
     105,   106,   107,   108,   109,   110,   111,   112,   113,   114,
     115,   116,   117,   118,   119,   120,   121,   122,   123,   124,
     125,   126,   127,   128,   129,   130,   131,   132,   133,   134,
     135,   136,   137,   138,   139,   140,   141,   142,   143,   144,
     145,   146,   147,   148,   149,   150,   151,   152,   153,   154,
     155,   156,   157,   158,   159,   160,   161,   162,   163,   164,
     165,   166,   167,   168,   169,   170,   171,   172,   173,   174,
     175,   176,   177,   178,   179,   180,   181,   182,   183,   184,
     185,   186,   187,   188,   189,   190,   191
};

#if YYDEBUG
/* YYPRHS[YYN] -- Index of the first RHS symbol of rule number YYN in
   YYRHS.  */
static const unsigned short int yyprhs[] =
{
       0,     0,     3,     5,     6,     8,    11,    13,    14,    18,
      19,    20,    26,    29,    32,    35,    37,    39,    40,    41,
      54,    57,    60,    61,    65,    66,    68,    70,    72,    74,
      76,    78,    80,    82,    84,    86,    89,    92,    94,    96,
      99,   101,   103,   106,   109,   112,   117,   123,   127,   128,
     130,   134,   135,   140,   141,   144,   146,   152,   158,   160,
     165,   166,   170,   171,   174,   176,   179,   181,   186,   194,
     199,   200,   204,   206,   208,   210,   213,   215,   217,   218,
     221,   222,   225,   229,   231,   236,   240,   241,   243,   247,
     254,   257,   258,   260,   263,   265,   267,   269,   271,   273,
     275,   277,   279,   281,   283,   285,   287,   289,   291,   293,
     295,   297,   299,   301,   303,   305,   307,   308,   309,   314,
     316,   317,   319,   322,   324,   325,   326,   332,   333,   335,
     336,   338,   341,   344,   346,   348,   350,   352,   354,   356,
     358,   360,   362,   365,   367,   369,   371,   373,   375,   377,
     379,   381,   383,   385,   388,   389,   391,   393,   395,   397,
     399,   401,   403,   404,   409,   410,   416,   418,   420,   421,
     423,   424,   425,   432,   434,   437,   439,   440,   445,   446,
     449,   450,   452,   456,   458,   459,   463,   464,   469,   471,
     476,   480,   481,   485,   489,   491,   493,   494,   499,   503,
     505,   507,   508,   515,   517,   520,   521,   525,   526,   532,
     533,   535,   538,   541,   543,   545,   548,   549,   551,   554,
     556,   559,   565,   571,   572,   573,   574,   575,   579,   582,
     584,   586,   589,   593,   596,   597,   602,   603,   605,   608,
     610,   611,   617,   618,   624,   625,   629,   630,   634,   635,
     637,   640,   645,   647,   651,   652,   654,   655,   656,   660,
     662,   664,   665,   670,   671,   673,   675,   677,   680,   682,
     683,   685,   688,   690,   692,   694,   696,   698,   702,   706,
     710,   714,   716,   717,   720,   724,   727,   728,   732,   733,
     737,   739,   743,   747,   750,   753,   754,   758,   759,   765,
     766,   768,   771,   775,   779,   781,   784,   787,   788,   792,
     795,   796,   801,   802,   807,   808,   813,   814,   816,   819,
     821,   823,   824,   826,   829,   831,   833,   836,   838,   840,
     842,   844,   846,   848,   850,   852,   854,   856,   858,   860,
     862,   864,   866,   868,   869,   873,   875
};

/* YYRHS -- A `-1'-separated list of the rules' RHS.  */
static const short int yyrhs[] =
{
     207,     0,    -1,   208,    -1,    -1,   209,    -1,   209,   210,
      -1,   210,    -1,    -1,   214,   211,   215,    -1,    -1,    -1,
     192,   212,   208,   213,   193,    -1,   216,   217,    -1,   218,
     194,    -1,   221,   194,    -1,   188,    -1,   189,    -1,    -1,
      -1,    15,    36,   128,   194,   219,   263,   220,   216,   217,
      48,    36,   128,    -1,   223,   224,    -1,   223,   233,    -1,
      -1,   223,   222,   260,    -1,    -1,   125,    -1,   225,    -1,
     229,    -1,   232,    -1,   235,    -1,   239,    -1,   241,    -1,
     242,    -1,   253,    -1,   255,    -1,    78,   141,    -1,    78,
     142,    -1,   256,    -1,   258,    -1,   180,   227,    -1,   144,
      -1,   143,    -1,   101,    63,    -1,   226,    29,    -1,   226,
     147,    -1,   226,    70,   228,   187,    -1,   226,    69,   161,
     228,   187,    -1,   226,    19,   187,    -1,    -1,   195,    -1,
      27,   247,   230,    -1,    -1,    75,    18,   247,   231,    -1,
      -1,   182,   247,    -1,    43,    -1,    36,   187,    33,    62,
     376,    -1,    36,   187,    33,    62,   234,    -1,   187,    -1,
     109,   240,   236,   237,    -1,    -1,    62,   119,   108,    -1,
      -1,   170,   246,    -1,   250,    -1,    25,   240,    -1,   187,
      -1,    40,   234,    85,   238,    -1,    40,   104,   250,   107,
     252,    85,   238,    -1,   115,   234,    64,   247,    -1,    -1,
     245,   244,   246,    -1,    85,    -1,   161,    -1,   248,    -1,
      41,   238,    -1,   186,    -1,   250,    -1,    -1,   249,   350,
      -1,    -1,   251,   352,    -1,   252,   196,   187,    -1,   187,
      -1,    55,   234,   237,   254,    -1,    55,    76,   247,    -1,
      -1,   243,    -1,    58,   240,   243,    -1,    58,   104,   250,
     107,   252,   243,    -1,    26,   257,    -1,    -1,   183,    -1,
     127,   257,    -1,     5,    -1,    10,    -1,    19,    -1,    31,
      -1,    46,    -1,    48,    -1,    52,    -1,    54,    -1,    62,
      -1,    68,    -1,    71,    -1,    82,    -1,   107,    -1,   123,
      -1,   124,    -1,   126,    -1,   129,    -1,   131,    -1,   162,
      -1,   168,    -1,    38,    -1,   167,    -1,    -1,    -1,   261,
     259,   262,   371,    -1,   264,    -1,    -1,   265,    -1,   265,
     266,    -1,   266,    -1,    -1,    -1,   267,   269,   270,   268,
     271,    -1,    -1,   272,    -1,    -1,   304,    -1,   338,   194,
      -1,   272,   273,    -1,   273,    -1,   274,    -1,   275,    -1,
     279,    -1,   163,    -1,    57,    -1,   145,    -1,    11,    -1,
     123,    -1,   337,   276,    -1,   278,    -1,   280,    -1,   299,
      -1,   178,    -1,    21,    -1,    86,    -1,    61,    -1,    45,
      -1,   185,    -1,   174,    -1,    17,   277,    -1,    -1,   175,
      -1,   136,    -1,    92,    -1,   137,    -1,   166,    -1,    28,
      -1,   179,    -1,    -1,   337,   283,   281,   285,    -1,    -1,
     337,   283,   288,   282,   284,    -1,   149,    -1,   164,    -1,
      -1,   285,    -1,    -1,    -1,   338,   192,   286,   289,   287,
     193,    -1,   187,    -1,   289,   290,    -1,   290,    -1,    -1,
     292,   294,   291,   271,    -1,    -1,   293,   272,    -1,    -1,
     295,    -1,   295,   196,   296,    -1,   296,    -1,    -1,   195,
     297,   343,    -1,    -1,   307,   195,   298,   343,    -1,   307,
      -1,    49,   192,   301,   193,    -1,    49,   339,   300,    -1,
      -1,   192,   301,   193,    -1,   301,   196,   302,    -1,   302,
      -1,   339,    -1,    -1,   339,   197,   303,   343,    -1,   304,
     196,   305,    -1,   305,    -1,   307,    -1,    -1,   307,   338,
     197,   306,   343,   337,    -1,   308,    -1,   313,   307,    -1,
      -1,   187,   309,   311,    -1,    -1,   198,   307,   199,   310,
     311,    -1,    -1,   312,    -1,   312,   316,    -1,   312,   340,
      -1,   316,    -1,   340,    -1,   200,   314,    -1,    -1,   315,
      -1,   315,   279,    -1,   279,    -1,   198,   199,    -1,   198,
     317,   321,   318,   199,    -1,   198,   317,   252,   318,   199,
      -1,    -1,    -1,    -1,    -1,   319,   322,   320,    -1,   325,
     323,    -1,   325,    -1,   324,    -1,   196,    47,    -1,   324,
     196,   325,    -1,   196,   325,    -1,    -1,   326,   272,   327,
     338,    -1,    -1,   328,    -1,   313,   327,    -1,   329,    -1,
      -1,   198,   328,   199,   330,   334,    -1,    -1,   198,   336,
     199,   331,   334,    -1,    -1,   187,   332,   334,    -1,    -1,
     340,   333,   334,    -1,    -1,   335,    -1,   335,   340,    -1,
     335,   198,   336,   199,    -1,   340,    -1,   198,   336,   199,
      -1,    -1,   321,    -1,    -1,    -1,   337,   185,   338,    -1,
     187,    -1,   184,    -1,    -1,   201,   341,   342,   202,    -1,
      -1,   344,    -1,   344,    -1,   345,    -1,   345,   349,    -1,
     349,    -1,    -1,   347,    -1,   347,   348,    -1,   348,    -1,
     349,    -1,   196,    -1,   194,    -1,    66,    -1,   198,   346,
     199,    -1,   201,   346,   202,    -1,   192,   346,   193,    -1,
     350,   196,   352,    -1,   352,    -1,    -1,   203,   356,    -1,
      80,   203,   356,    -1,    80,   356,    -1,    -1,   353,   354,
     351,    -1,    -1,   195,   355,   356,    -1,   357,    -1,   200,
      28,   356,    -1,   200,   179,   356,    -1,   200,   356,    -1,
     204,   357,    -1,    -1,   187,   358,   360,    -1,    -1,   198,
     356,   199,   359,   360,    -1,    -1,   361,    -1,   361,   362,
      -1,   361,   205,   187,    -1,   361,   117,   187,    -1,   362,
      -1,   205,   187,    -1,   117,   187,    -1,    -1,   201,   363,
     364,    -1,   369,   202,    -1,    -1,   198,   366,   369,   199,
      -1,    -1,   201,   367,   369,   202,    -1,    -1,   192,   368,
     369,   193,    -1,    -1,   370,    -1,   370,   365,    -1,   365,
      -1,   372,    -1,    -1,   373,    -1,   373,   374,    -1,   374,
      -1,   352,    -1,    41,   250,    -1,   375,    -1,   200,    -1,
     196,    -1,   198,    -1,   199,    -1,   192,    -1,   193,    -1,
      66,    -1,   187,    -1,    85,    -1,   161,    -1,    64,    -1,
     129,    -1,   172,    -1,   107,    -1,   182,    -1,    -1,   377,
     378,   372,    -1,   129,    -1,   198,    -1
};

/* YYRLINE[YYN] -- source line where rule number YYN was defined.  */
static const unsigned short int yyrline[] =
{
       0,   445,   445,   458,   459,   464,   466,   472,   471,   476,
     482,   475,   492,   497,   504,   514,   524,   531,   538,   530,
     544,   552,   559,   558,   571,   576,   585,   587,   589,   591,
     593,   595,   597,   599,   601,   603,   605,   607,   609,   614,
     619,   625,   631,   640,   646,   652,   658,   664,   674,   675,
     680,   700,   705,   722,   727,   737,   747,   757,   771,   781,
     829,   834,   844,   845,   850,   866,   878,   891,   902,   929,
     945,   944,   961,   962,   966,   968,   973,   979,   997,   997,
    1014,  1014,  1039,  1047,  1060,  1080,  1092,  1094,  1098,  1113,
    1144,  1153,  1155,  1159,  1169,  1176,  1183,  1190,  1197,  1204,
    1211,  1218,  1225,  1232,  1239,  1246,  1253,  1260,  1267,  1274,
    1281,  1288,  1295,  1302,  1309,  1316,  1327,  1334,  1327,  1348,
    1354,  1355,  1361,  1363,  1369,  1375,  1369,  1420,  1425,  1435,
    1440,  1449,  1464,  1470,  1479,  1481,  1483,  1488,  1494,  1500,
    1506,  1512,  1521,  1523,  1525,  1531,  1541,  1547,  1553,  1559,
    1565,  1571,  1577,  1594,  1604,  1609,  1618,  1624,  1630,  1636,
    1645,  1651,  1662,  1660,  1678,  1675,  1696,  1702,  1712,  1717,
    1728,  1734,  1726,  1776,  1797,  1811,  1821,  1820,  1862,  1862,
    1878,  1883,  1892,  1904,  1914,  1913,  1928,  1926,  1939,  1948,
    1949,  1967,  1968,  1973,  1975,  1980,  1989,  1987,  2004,  2018,
    2030,  2039,  2036,  2056,  2062,  2075,  2074,  2088,  2087,  2104,
    2105,  2110,  2121,  2132,  2143,  2157,  2163,  2164,  2169,  2171,
    2176,  2182,  2188,  2198,  2207,  2216,  2225,  2233,  2242,  2250,
    2259,  2265,  2274,  2285,  2295,  2295,  2322,  2327,  2336,  2347,
    2357,  2356,  2369,  2368,  2384,  2383,  2396,  2395,  2412,  2414,
    2418,  2426,  2434,  2442,  2454,  2459,  2469,  2478,  2486,  2493,
    2505,  2516,  2515,  2571,  2572,  2577,  2586,  2591,  2593,  2599,
    2600,  2605,  2607,  2612,  2614,  2616,  2621,  2623,  2625,  2627,
    2635,  2641,  2647,  2653,  2660,  2667,  2679,  2679,  2715,  2714,
    2729,  2735,  2748,  2761,  2774,  2792,  2791,  2817,  2816,  2840,
    2847,  2856,  2862,  2874,  2886,  2892,  2904,  2920,  2919,  2936,
    2955,  2954,  2968,  2967,  2981,  2980,  2997,  2998,  3003,  3005,
    3015,  3118,  3119,  3124,  3126,  3131,  3133,  3143,  3148,  3150,
    3152,  3154,  3156,  3158,  3160,  3162,  3164,  3170,  3176,  3182,
    3188,  3194,  3196,  3202,  3202,  3244,  3250
};
#endif

#if YYDEBUG || YYERROR_VERBOSE || YYTOKEN_TABLE
/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "$end", "error", "$undefined", "ADD", "ALL", "ALTER", "AND", "AS",
  "ASC", "ATTRIBUTE", "ATTACH", "AUTO_", "AVG", "BAD_TOKEN", "BOGUS_ECHO",
  "BEGIN_", "BETWEEN", "BIT_", "BY", "CALL_", "CHANGE", "CHAR_",
  "CHARACTER", "CHECK_", "CLASS", "CLOSE", "COMMIT", "CONNECT", "CONST_",
  "CONTINUE_", "COUNT", "CREATE", "CURRENT", "CURSOR_", "DATE_",
  "DATE_LIT", "DECLARE", "DEFAULT", "DELETE_", "DESC", "DESCRIBE",
  "DESCRIPTOR", "DIFFERENCE_", "DISCONNECT", "DISTINCT", "DOUBLE_", "DROP",
  "ELLIPSIS", "END", "ENUM", "EQ", "ESCAPE", "EVALUATE", "EXCEPT",
  "EXCLUDE", "EXECUTE", "EXISTS", "EXTERN_", "FETCH", "FILE_",
  "FILE_PATH_LIT", "FLOAT_", "FOR", "FOUND", "FROM", "FUNCTION_",
  "GENERIC_TOKEN", "GEQ", "GET", "GO", "GOTO_", "GRANT", "GROUP_", "GT",
  "HAVING", "IDENTIFIED", "IMMEDIATE", "IN_", "INCLUDE", "INDEX",
  "INDICATOR", "INHERIT", "INSERT", "INTEGER", "INTERSECTION", "INTO",
  "INT_", "INT_LIT", "IS", "LDBVCLASS", "LEQ", "LIKE", "LONG_", "LT",
  "MAX_", "METHOD", "MIN_", "MINUS", "MONEY_LIT", "MULTISET_OF", "NEQ",
  "NOT", "NULL_", "NUMERIC", "OBJECT", "OF", "OID_", "ON_", "ONLY", "OPEN",
  "OPTION", "OR", "ORDER", "PLUS", "PRECISION", "PREPARE", "PRIVILEGES",
  "PTR_OP", "PUBLIC_", "READ", "READ_ONLY", "REAL", "REAL_LIT",
  "REGISTER_", "RENAME", "REPEATED", "REVOKE", "ROLLBACK", "SECTION",
  "SELECT", "SEQUENCE_OF", "SET", "SETEQ", "SETNEQ", "SET_OF", "SHARED",
  "SHORT_", "SIGNED_", "SLASH", "SMALLINT", "SOME", "SQLCA", "SQLDA",
  "SQLERROR_", "SQLWARNING_", "STATIC_", "STATISTICS", "STOP_", "STRING",
  "STRUCT_", "SUBCLASS", "SUBSET", "SUBSETEQ", "SUM", "SUPERCLASS",
  "SUPERSET", "SUPERSETEQ", "TABLE", "TIME", "TIMESTAMP", "TIME_LIT", "TO",
  "TRIGGER", "TYPEDEF_", "UNION_", "UNIQUE", "UNSIGNED_", "UPDATE", "USE",
  "USER", "USING", "UTIME", "VALUES", "VARBIT_", "VARCHAR_", "VARYING",
  "VCLASS", "VIEW", "VOID_", "VOLATILE_", "WHENEVER", "WHERE", "WITH",
  "WORK", "ENUMERATION_CONSTANT", "TYPEDEF_NAME", "STRING_LIT",
  "IDENTIFIER", "EXEC", "SQLX", "UNION", "STRUCT", "'{'", "'}'", "';'",
  "':'", "','", "'='", "'('", "')'", "'*'", "'['", "']'", "'#'", "'&'",
  "'.'", "$accept", "translation_unit", "opt_embedded_chunk_list",
  "embedded_chunk_list", "embedded_chunk", "@1", "@2", "@3",
  "chunk_header", "chunk_body", "exec", "sql", "declare_section", "@4",
  "@5", "esql_statement", "@6", "opt_repeated", "embedded_statement",
  "whenever_statement", "of_whenever_state", "whenever_action",
  "opt_COLON", "connect_statement", "opt_id_by_quasi", "opt_with_quasi",
  "disconnect_statement", "declare_cursor_statement", "dynamic_statement",
  "open_statement", "opt_for_read_only", "using_part", "descriptor_name",
  "close_statement", "cursor", "describe_statement", "prepare_statement",
  "into_result", "@7", "of_into_to", "host_variable_lod",
  "quasi_string_const", "host_variable_list", "@8",
  "host_variable_wo_indicator", "@9", "id_list_", "execute_statement",
  "opt_into_result", "fetch_statement", "commit_statement", "opt_work",
  "rollback_statement", "csql_statement_head", "csql_statement", "@10",
  "@11", "declare_section_body", "opt_ext_decl_list", "ext_decl_list",
  "external_declaration", "@12", "@13", "opt_specifier_list",
  "optional_declarators", "end_declarator_list", "specifier_list",
  "specifier_", "storage_class_specifier", "type_specifier",
  "base_type_specifier", "opt_varying", "type_adjective", "type_qualifier",
  "struct_specifier", "@14", "@15", "of_struct_union",
  "opt_struct_specifier_body", "struct_specifier_body", "@16", "@17",
  "struct_tag", "struct_declaration_list", "struct_declaration", "@18",
  "specifier_qualifier_list", "@19", "optional_struct_declarator_list",
  "struct_declarator_list", "struct_declarator", "@20", "@21",
  "enum_specifier", "optional_enumerator_list", "enumerator_list",
  "enumerator", "@22", "init_declarator_list", "init_declarator", "@23",
  "declarator_", "direct_declarator", "@24", "@25", "opt_param_spec_list",
  "param_spec_list", "pointer", "opt_type_qualifier_list",
  "type_qualifier_list", "param_spec", "push_name_scope", "pop_name_scope",
  "push_spec_scope", "pop_spec_scope", "param_type_list", "parameter_list",
  "trailing_params", "param_decl_tail_list", "parameter_declaration",
  "@26", "opt_abstract_declarator", "abstract_declarator",
  "direct_abstract_declarator", "@27", "@28", "@29", "@30",
  "opt_direct_adecl_tail", "direct_adecl_tail", "opt_param_type_list",
  "nS_ntd", "nS_td", "identifier", "c_subscript", "@31",
  "c_subscript_body", "c_expression", "c_expr", "any_list", "any_expr",
  "acs_list", "of_any_comma_semi", "any", "host_variable_list_tail",
  "indicator_var", "host_var_w_opt_indicator", "@32", "host_variable",
  "@33", "hostvar", "host_ref", "@34", "@35", "opt_host_ref_tail",
  "host_ref_tail", "host_var_subscript", "@36", "host_var_subscript_body",
  "hv_sub", "@37", "@38", "@39", "opt_hv_sub_list", "hv_sub_list",
  "csql_statement_tail", "opt_buffer_sql_list", "buffer_sql_list",
  "buffer_sql", "var_mode_follow_set", "cursor_query_statement", "@40",
  "of_select_lp", 0
};
#endif

/* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const unsigned short int yyr1[] =
{
       0,   206,   207,   208,   208,   209,   209,   211,   210,   212,
     213,   210,   214,   215,   215,   216,   217,   219,   220,   218,
     221,   221,   222,   221,   223,   223,   224,   224,   224,   224,
     224,   224,   224,   224,   224,   224,   224,   224,   224,   225,
     226,   226,   226,   227,   227,   227,   227,   227,   228,   228,
     229,   230,   230,   231,   231,   232,   233,   233,   234,   235,
     236,   236,   237,   237,   238,   239,   240,   241,   241,   242,
     244,   243,   245,   245,   246,   246,   247,   247,   249,   248,
     251,   250,   252,   252,   253,   253,   254,   254,   255,   255,
     256,   257,   257,   258,   259,   259,   259,   259,   259,   259,
     259,   259,   259,   259,   259,   259,   259,   259,   259,   259,
     259,   259,   259,   259,   259,   259,   261,   262,   260,   263,
     264,   264,   265,   265,   267,   268,   266,   269,   269,   270,
     270,   271,   272,   272,   273,   273,   273,   274,   274,   274,
     274,   274,   275,   275,   275,   275,   276,   276,   276,   276,
     276,   276,   276,   276,   277,   277,   278,   278,   278,   278,
     279,   279,   281,   280,   282,   280,   283,   283,   284,   284,
     286,   287,   285,   288,   289,   289,   291,   290,   293,   292,
     294,   294,   295,   295,   297,   296,   298,   296,   296,   299,
     299,   300,   300,   301,   301,   302,   303,   302,   304,   304,
     305,   306,   305,   307,   307,   309,   308,   310,   308,   311,
     311,   312,   312,   312,   312,   313,   314,   314,   315,   315,
     316,   316,   316,   317,   318,   319,   320,   321,   322,   322,
     323,   323,   324,   324,   326,   325,   327,   327,   328,   328,
     330,   329,   331,   329,   332,   329,   333,   329,   334,   334,
     335,   335,   335,   335,   336,   336,   337,   338,   339,   339,
     339,   341,   340,   342,   342,   343,   344,   345,   345,   346,
     346,   347,   347,   348,   348,   348,   349,   349,   349,   349,
     350,   350,   351,   351,   351,   351,   353,   352,   355,   354,
     356,   356,   356,   356,   356,   358,   357,   359,   357,   360,
     360,   361,   361,   361,   361,   361,   361,   363,   362,   364,
     366,   365,   367,   365,   368,   365,   369,   369,   370,   370,
     371,   372,   372,   373,   373,   374,   374,   374,   375,   375,
     375,   375,   375,   375,   375,   375,   375,   375,   375,   375,
     375,   375,   375,   377,   376,   378,   378
};

/* YYR2[YYN] -- Number of symbols composing right hand side of rule YYN.  */
static const unsigned char yyr2[] =
{
       0,     2,     1,     0,     1,     2,     1,     0,     3,     0,
       0,     5,     2,     2,     2,     1,     1,     0,     0,    12,
       2,     2,     0,     3,     0,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     2,     2,     1,     1,     2,
       1,     1,     2,     2,     2,     4,     5,     3,     0,     1,
       3,     0,     4,     0,     2,     1,     5,     5,     1,     4,
       0,     3,     0,     2,     1,     2,     1,     4,     7,     4,
       0,     3,     1,     1,     1,     2,     1,     1,     0,     2,
       0,     2,     3,     1,     4,     3,     0,     1,     3,     6,
       2,     0,     1,     2,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     0,     0,     4,     1,
       0,     1,     2,     1,     0,     0,     5,     0,     1,     0,
       1,     2,     2,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     2,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     2,     0,     1,     1,     1,     1,     1,
       1,     1,     0,     4,     0,     5,     1,     1,     0,     1,
       0,     0,     6,     1,     2,     1,     0,     4,     0,     2,
       0,     1,     3,     1,     0,     3,     0,     4,     1,     4,
       3,     0,     3,     3,     1,     1,     0,     4,     3,     1,
       1,     0,     6,     1,     2,     0,     3,     0,     5,     0,
       1,     2,     2,     1,     1,     2,     0,     1,     2,     1,
       2,     5,     5,     0,     0,     0,     0,     3,     2,     1,
       1,     2,     3,     2,     0,     4,     0,     1,     2,     1,
       0,     5,     0,     5,     0,     3,     0,     3,     0,     1,
       2,     4,     1,     3,     0,     1,     0,     0,     3,     1,
       1,     0,     4,     0,     1,     1,     1,     2,     1,     0,
       1,     2,     1,     1,     1,     1,     1,     3,     3,     3,
       3,     1,     0,     2,     3,     2,     0,     3,     0,     3,
       1,     3,     3,     2,     2,     0,     3,     0,     5,     0,
       1,     2,     3,     3,     1,     2,     2,     0,     3,     2,
       0,     4,     0,     4,     0,     4,     0,     1,     2,     1,
       1,     0,     1,     2,     1,     1,     2,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     0,     3,     1,     1
};

/* YYDPREC[RULE-NUM] -- Dynamic precedence of rule #RULE-NUM (0 if none).  */
static const unsigned char yydprec[] =
{
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     1,
       2,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0
};

/* YYMERGER[RULE-NUM] -- Index of merging function for rule #RULE-NUM.  */
static const unsigned char yymerger[] =
{
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0
};

/* YYDEFACT[S] -- default rule to reduce with in state S when YYTABLE
   doesn't specify something else to do.  Zero means the default is an
   error.  */
static const unsigned short int yydefact[] =
{
       3,    15,     9,     0,     2,     4,     6,     7,     0,     3,
       1,     5,    24,    16,    12,    10,     0,    25,     8,     0,
       0,    22,     0,     0,    13,    14,     0,    91,    80,     0,
       0,    55,     0,     0,     0,     0,     0,    91,     0,   116,
      20,    26,    27,    28,    21,    29,    30,    31,    32,    33,
      34,    37,    38,    11,     0,    66,    65,    92,    90,    76,
      51,    77,   286,     0,    80,    58,     0,    80,    62,    80,
       0,    35,    36,    60,     0,    93,     0,    41,    40,     0,
      39,    23,     0,    17,     0,    50,    81,     0,     0,     0,
      80,    85,    78,    86,     0,    72,    73,    88,    70,     0,
      62,    80,    42,     0,    43,     0,    48,    44,    94,    95,
      96,    97,   114,    98,    99,   100,   101,   102,   103,   104,
     105,   106,   107,   108,   109,   110,   111,   112,   115,   113,
     117,   124,    80,   288,   282,   343,     0,    67,    64,    80,
      63,    74,   286,    87,    84,     0,    78,     0,    59,    69,
      47,    48,    49,     0,   286,    18,   119,   124,   123,   256,
      53,     0,     0,     0,   287,    57,    56,     0,    83,     0,
      75,    79,   281,     0,    71,    61,     0,    45,    80,   338,
     334,   336,   341,   339,   337,   340,   342,   335,   332,   333,
     329,   330,   331,   328,   325,   118,   320,   286,   324,   327,
       0,   122,   140,   160,   256,   138,   157,   141,   156,   158,
     139,   137,   159,   161,   129,   256,   133,   134,   135,   143,
     136,   144,   145,     0,    80,    52,   295,     0,     0,     0,
     289,   290,     0,   285,   283,   345,   346,   286,    80,     0,
     286,    89,    46,   326,   323,     0,   260,   259,   256,     0,
     191,   205,     0,   216,   125,   130,   199,   200,   203,     0,
     132,   154,   147,   150,   149,   148,   166,   167,   152,   146,
     151,   142,   162,    54,   299,     0,     0,     0,   293,   294,
     284,   344,    68,    82,   280,     0,     0,   194,   195,   257,
     256,   190,   209,     0,   219,   215,   217,   257,     0,     0,
     204,   155,   153,   173,   257,   164,     0,   307,     0,   296,
     300,   304,   297,   291,   292,     0,   189,   256,   196,   258,
       0,   223,   261,   206,   210,   213,   214,   207,   218,   126,
       0,   198,   201,   163,     0,   168,   306,   316,   305,     0,
       0,   301,   299,     0,   193,     0,   192,   220,   225,   263,
     211,   212,   209,   131,     0,   170,   165,   169,   314,   310,
     312,   308,   319,     0,   317,   303,   302,   298,    19,   276,
     269,   269,   269,   197,   265,   266,   268,   224,   234,   224,
       0,   264,   208,   256,   178,   316,   316,   316,   309,   318,
     275,   274,     0,   270,   272,   273,     0,     0,   267,     0,
     226,   229,   256,     0,   262,   202,   178,   175,   180,   256,
       0,     0,     0,   279,   271,   277,   278,   222,   227,   234,
     228,   230,   256,   221,     0,   174,   184,   176,   181,   183,
     188,   256,   315,   311,   313,   231,   233,   234,   244,   225,
     236,   257,   237,   239,   246,   172,     0,   257,     0,   186,
     232,   248,   255,     0,     0,   238,   235,   248,   185,   177,
     182,     0,   225,   245,   249,   252,   240,   242,   247,   187,
       0,   225,   250,   248,   248,   253,     0,   241,   243,   251
};

/* YYPDEFGOTO[NTERM-NUM].  */
static const short int yydefgoto[] =
{
      -1,     3,     4,     5,     6,    12,     9,    22,     7,    18,
       8,    14,    19,   131,   200,    20,    39,    21,    40,    41,
      79,    80,   153,    42,    85,   225,    43,    44,    66,    45,
     100,    93,   137,    46,    56,    47,    48,    97,   146,    98,
     140,    60,   141,   142,    61,    62,   169,    49,   144,    50,
      51,    58,    52,   130,    81,    82,   154,   155,   156,   157,
     158,   159,   297,   214,   254,   329,   215,   216,   217,   218,
     271,   302,   219,   220,   221,   304,   335,   272,   356,   333,
     384,   424,   305,   406,   407,   447,   408,   409,   427,   428,
     429,   446,   461,   222,   291,   286,   287,   345,   255,   256,
     354,   257,   258,   292,   352,   323,   324,   259,   295,   296,
     325,   348,   399,   378,   418,   452,   400,   420,   421,   401,
     402,   441,   442,   443,   473,   474,   451,   457,   463,   464,
     454,   223,   330,   288,   465,   349,   380,   373,   374,   375,
     392,   393,   394,   376,   171,   164,   194,    87,   134,   161,
     230,   231,   274,   342,   309,   310,   311,   337,   361,   362,
     386,   387,   385,   363,   364,   195,   196,   197,   198,   199,
     166,   167,   237
};

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
#define YYPACT_NINF -406
static const short int yypact[] =
{
    -112,  -406,  -406,    33,  -406,  -112,  -406,  -406,  -127,  -112,
    -406,  -406,     3,  -406,  -406,  -406,    41,  -406,  -406,   -91,
     -72,   307,   -99,   -35,  -406,  -406,   -89,   -33,   -62,   -61,
     -80,  -406,   -44,   -77,   -86,   -89,   -39,   -33,   -71,  -406,
    -406,  -406,  -406,  -406,  -406,  -406,  -406,  -406,  -406,  -406,
    -406,  -406,  -406,  -406,   -21,  -406,  -406,  -406,  -406,  -406,
      86,  -406,  -406,   135,  -406,  -406,    92,   -62,    12,  -406,
     -50,  -406,  -406,   124,   137,  -406,   125,  -406,  -406,    -6,
    -406,  -406,   230,  -406,   181,  -406,  -406,    19,   156,   113,
    -406,  -406,   184,   -50,   129,  -406,  -406,  -406,  -406,   118,
      12,   -62,  -406,    61,  -406,    94,    62,  -406,  -406,  -406,
    -406,  -406,  -406,  -406,  -406,  -406,  -406,  -406,  -406,  -406,
    -406,  -406,  -406,  -406,  -406,  -406,  -406,  -406,  -406,  -406,
    -406,    44,   -62,  -406,   -68,   -39,   106,  -406,  -406,  -406,
    -406,  -406,  -406,  -406,  -406,   106,   184,   189,  -406,  -406,
    -406,    62,  -406,   112,   209,  -406,  -406,   123,  -406,    60,
     131,  -108,   117,  -108,  -406,  -406,  -406,  -101,  -406,   -26,
    -406,   104,  -406,   -34,  -406,  -406,   132,  -406,  -406,  -406,
    -406,  -406,  -406,  -406,  -406,  -406,  -406,  -406,  -406,  -406,
    -406,  -406,  -406,  -406,  -406,  -406,  -406,   239,  -406,  -406,
     130,  -406,  -406,  -406,    37,  -406,  -406,  -406,  -406,  -406,
    -406,  -406,  -406,  -406,   -85,   108,  -406,  -406,  -406,  -406,
    -406,  -406,  -406,    78,   -62,  -406,  -406,  -108,    30,  -153,
    -406,  -406,  -108,  -406,  -406,  -406,  -406,   209,  -406,   136,
    -406,  -406,  -406,  -406,  -406,  -127,  -406,  -406,    -9,   144,
     147,  -406,   -85,   -13,  -406,   126,  -406,   139,  -406,   -85,
    -406,   165,  -406,  -406,  -406,  -406,  -406,  -406,  -406,  -406,
    -406,  -406,   154,  -406,  -100,   143,  -108,  -108,  -406,  -406,
    -406,  -406,  -406,  -406,  -406,   296,    45,  -406,   148,  -406,
      -9,  -406,    69,   149,  -406,  -406,   -13,  -406,   -85,   152,
    -406,  -406,  -406,  -406,  -406,  -406,   164,  -406,   168,  -406,
     -97,  -406,  -406,  -406,  -406,   316,  -406,    -9,  -406,  -406,
      50,   158,  -406,  -406,    69,  -406,  -406,  -406,  -406,  -406,
     166,  -406,  -406,  -406,   171,   172,  -406,    21,  -406,   179,
     180,  -406,  -100,   241,  -406,   -52,  -406,  -406,   106,   -52,
    -406,  -406,    69,  -406,   -52,  -406,  -406,  -406,  -406,  -406,
    -406,  -406,  -406,   169,    21,  -406,  -406,  -406,  -406,  -406,
      85,    85,    85,  -406,  -406,   -52,  -406,   162,  -406,  -406,
     170,  -406,  -406,  -406,  -406,    21,    21,    21,  -406,  -406,
    -406,  -406,   182,    85,  -406,  -406,   174,   175,  -406,   177,
    -406,   178,    -3,   183,  -406,  -406,   185,  -406,    64,    -3,
     190,   187,   186,  -406,  -406,  -406,  -406,  -406,  -406,   337,
    -406,   191,    -7,  -406,   196,  -406,  -406,  -406,   194,  -406,
     198,     8,  -406,  -406,  -406,  -406,  -406,  -406,  -406,   127,
     109,  -406,  -406,  -406,  -406,  -406,   -52,  -406,    64,  -406,
    -406,    71,  -406,   195,   200,  -406,  -406,    71,  -406,  -406,
    -406,   -52,   205,  -406,    87,  -406,  -406,  -406,  -406,  -406,
     207,   205,  -406,    71,    71,  -406,   211,  -406,  -406,  -406
};

/* YYPGOTO[NTERM-NUM].  */
static const short int yypgoto[] =
{
    -406,  -406,   386,  -406,   407,  -406,  -406,  -406,  -406,  -406,
     213,   173,  -406,  -406,  -406,  -406,  -406,  -406,  -406,  -406,
    -406,  -406,   263,  -406,  -406,  -406,  -406,  -406,    34,  -406,
    -406,   315,  -117,  -406,    79,  -406,  -406,   -67,  -406,  -406,
     271,   -57,  -406,  -406,   -53,  -406,  -144,  -406,  -406,  -406,
    -406,   382,  -406,  -406,  -406,  -406,  -406,  -406,  -406,  -406,
     266,  -406,  -406,  -406,  -406,   -27,  -361,  -215,  -406,  -406,
    -406,  -406,  -406,  -222,  -406,  -406,  -406,  -406,  -406,    89,
    -406,  -406,  -406,  -406,    22,  -406,  -406,  -406,  -406,  -406,
     -23,  -406,  -406,  -406,  -406,   140,   110,  -406,  -406,   138,
    -406,  -250,  -406,  -406,  -406,    77,  -406,  -379,  -406,  -406,
     116,  -406,    63,  -406,  -406,    93,  -406,  -406,  -406,  -390,
    -406,     4,     6,  -406,  -406,  -406,  -406,  -406,  -405,  -406,
    -384,  -199,  -251,   242,  -285,  -406,  -406,  -314,    98,  -406,
    -290,  -406,    55,  -160,  -406,  -406,   -59,  -406,  -406,  -406,
     103,   214,  -406,  -406,   107,  -406,   141,  -406,  -406,    88,
    -406,  -406,  -406,   -96,  -406,  -406,   216,  -406,   253,  -406,
    -406,  -406,  -406
};

/* YYTABLE[YYPACT[STATE-NUM]].  What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule which
   number is the opposite.  If zero, do what YYDEFACT says.
   If YYTABLE_NINF, syntax error.  */
#define YYTABLE_NINF -323
static const short int yytable[] =
{
     260,   173,   293,    86,   202,   249,   299,   326,   202,   300,
      91,    89,   162,   103,   369,   203,    94,   306,    16,   202,
     339,   203,   170,   104,    64,   203,   143,    69,   235,   436,
      76,   294,    67,    10,   226,    95,   203,   138,   319,   351,
     383,   422,   204,   440,   149,   227,   204,   450,   431,   249,
     205,    95,   468,   334,   205,    71,    72,   204,   276,   238,
     440,   440,    13,   105,   106,   205,    68,   326,   477,   478,
      74,   202,    77,    78,   328,   160,     1,    23,   470,   226,
       2,   396,   397,   172,   334,   206,   138,   476,   203,   206,
     227,   249,   228,    54,    53,   261,   229,   236,    55,   262,
     206,   307,   251,    24,   307,   308,   241,    65,   340,   204,
      55,    96,    70,   252,    73,   253,   207,   205,   249,   202,
     207,   282,    25,   263,    59,   243,    63,    96,    17,   208,
     209,   207,   458,   208,   209,   163,   203,   444,   210,   264,
     370,   107,   210,    65,   208,   209,   371,   469,    65,   372,
      57,   369,   206,   210,   444,   444,   211,   204,   430,   212,
     211,    84,   239,   212,   265,   205,   213,   273,    88,   165,
     239,   211,   213,    83,   212,   246,   213,    90,   247,   472,
     438,   284,    92,   207,   405,   138,    99,   213,   102,  -236,
     456,   439,  -236,   253,   322,  -179,   208,   209,   430,   132,
     206,   101,  -179,  -179,   377,   210,  -179,   260,  -179,   277,
     395,   395,   395,   358,   133,   398,   260,   226,   135,   359,
     136,   246,   360,   211,   247,   139,   212,   266,   227,   248,
     228,   207,  -120,   395,   229,   108,   145,   147,   316,   213,
     109,   317,   267,   346,   208,   209,   317,  -127,   150,   110,
     178,   251,   268,   210,  -127,   151,   269,   152,  -127,   426,
    -127,   111,   252,   270,   253,   233,   234,   321,   112,   462,
     322,   211,   322,   179,   212,   180,   113,   370,   114,   390,
     178,   391,   115,   371,   116,   471,   372,   213,   322,   410,
     411,   412,   117,   168,   181,  -128,   438,   175,   118,   177,
     240,   119,  -128,   179,   226,   180,  -128,   439,  -128,   253,
     322,  -121,   120,   224,   438,   227,   182,   228,     1,   242,
     232,   229,   298,   283,   181,   439,  -254,   253,   322,   289,
     275,   278,    26,    27,    28,   280,  -257,   121,   183,   290,
     301,   303,   312,    29,   315,   318,   182,    30,   327,   332,
      31,   336,   343,   122,   123,   338,   124,   347,   239,   125,
     353,   126,    32,   355,  -257,    33,   365,   366,   183,   368,
     184,   388,   404,   415,   419,   413,   417,   416,  -171,   313,
     314,   185,   423,   432,   435,    34,   433,   437,   434,   445,
     448,   186,   127,   449,   466,    15,   187,   128,   129,   467,
     184,   188,   189,  -321,  -254,   190,   475,   191,   192,   193,
     479,   185,    11,   245,   176,   148,    35,   174,   285,    75,
     459,   186,    36,   201,   357,   460,   187,   344,   425,   382,
     320,   188,   189,  -322,    37,   190,   331,   191,   192,   193,
     350,   379,   403,   279,   455,   453,   250,   381,   414,   367,
     244,   341,   389,   281,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,    38
};

/* YYCONFLP[YYPACT[STATE-NUM]] -- Pointer into YYCONFL of start of
   list of conflicting reductions corresponding to action entry for
   state STATE-NUM in yytable.  0 means no conflicts.  The list in
   yyconfl is terminated by a rule number of 0.  */
static const unsigned char yyconflp[] =
{
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0
};

/* YYCONFL[I] -- lists of conflicting rule numbers, each terminated by
   0, pointed into by YYCONFLP.  */
static const short int yyconfl[] =
{
       0
};

static const short int yycheck[] =
{
     215,   145,   252,    62,    11,   204,   257,   292,    11,   259,
      67,    64,    80,    19,    66,    28,    69,   117,    15,    11,
     117,    28,   139,    29,   104,    28,    93,   104,   129,   419,
     101,   253,    76,     0,   187,    85,    28,    90,   289,   324,
     354,   402,    49,   422,   101,   198,    49,   437,   409,   248,
      57,    85,   457,   304,    57,   141,   142,    49,    28,    85,
     439,   440,   189,    69,    70,    57,    32,   352,   473,   474,
      36,    11,   143,   144,   296,   132,   188,    36,   462,   187,
     192,   371,   372,   142,   335,    92,   139,   471,    28,    92,
     198,   290,   200,   128,   193,    17,   204,   198,   187,    21,
      92,   201,   187,   194,   201,   205,   173,   187,   205,    49,
     187,   161,    33,   198,    35,   200,   123,    57,   317,    11,
     123,   238,   194,    45,   186,   178,   187,   161,   125,   136,
     137,   123,   446,   136,   137,   203,    28,   422,   145,    61,
     192,   147,   145,   187,   136,   137,   198,   461,   187,   201,
     183,    66,    92,   145,   439,   440,   163,    49,   408,   166,
     163,    75,   196,   166,    86,    57,   179,   224,    33,   135,
     196,   163,   179,   194,   166,   184,   179,    85,   187,   464,
     187,   240,   170,   123,   383,   238,    62,   179,    63,   196,
     441,   198,   199,   200,   201,   187,   136,   137,   448,    18,
      92,    64,   194,   195,   348,   145,   198,   422,   200,   179,
     370,   371,   372,   192,   195,   375,   431,   187,    62,   198,
     107,   184,   201,   163,   187,    41,   166,   149,   198,   192,
     200,   123,   188,   393,   204,     5,   107,   119,   193,   179,
      10,   196,   164,   193,   136,   137,   196,   187,   187,    19,
      41,   187,   174,   145,   194,   161,   178,   195,   198,   195,
     200,    31,   198,   185,   200,   162,   163,   198,    38,   198,
     201,   163,   201,    64,   166,    66,    46,   192,    48,   194,
      41,   196,    52,   198,    54,   198,   201,   179,   201,   385,
     386,   387,    62,   187,    85,   187,   187,   108,    68,   187,
     196,    71,   194,    64,   187,    66,   198,   198,   200,   200,
     201,   188,    82,   182,   187,   198,   107,   200,   188,   187,
     203,   204,   196,   187,    85,   198,   199,   200,   201,   185,
     227,   228,    25,    26,    27,   232,   197,   107,   129,   192,
     175,   187,   199,    36,    48,   197,   107,    40,   199,   197,
      43,   187,    36,   123,   124,   187,   126,   199,   196,   129,
     194,   131,    55,   192,   192,    58,   187,   187,   129,   128,
     161,   202,   202,   199,   196,   193,   199,   202,   193,   276,
     277,   172,   199,   193,    47,    78,   199,   196,   202,   193,
     196,   182,   162,   195,   199,     9,   187,   167,   168,   199,
     161,   192,   193,   194,   199,   196,   199,   198,   199,   200,
     199,   172,     5,   200,   151,   100,   109,   146,   245,    37,
     447,   182,   115,   157,   335,   448,   187,   317,   406,   352,
     290,   192,   193,   194,   127,   196,   298,   198,   199,   200,
     324,   348,   379,   229,   440,   439,   204,   349,   393,   342,
     197,   310,   364,   237,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,   180
};

/* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
   symbol of state STATE-NUM.  */
static const unsigned short int yystos[] =
{
       0,   188,   192,   207,   208,   209,   210,   214,   216,   212,
       0,   210,   211,   189,   217,   208,    15,   125,   215,   218,
     221,   223,   213,    36,   194,   194,    25,    26,    27,    36,
      40,    43,    55,    58,    78,   109,   115,   127,   180,   222,
     224,   225,   229,   232,   233,   235,   239,   241,   242,   253,
     255,   256,   258,   193,   128,   187,   240,   183,   257,   186,
     247,   250,   251,   187,   104,   187,   234,    76,   234,   104,
     240,   141,   142,   240,   234,   257,   101,   143,   144,   226,
     227,   260,   261,   194,    75,   230,   352,   353,    33,   250,
      85,   247,   170,   237,   250,    85,   161,   243,   245,    62,
     236,    64,    63,    19,    29,    69,    70,   147,     5,    10,
      19,    31,    38,    46,    48,    52,    54,    62,    68,    71,
      82,   107,   123,   124,   126,   129,   131,   162,   167,   168,
     259,   219,    18,   195,   354,    62,   107,   238,   250,    41,
     246,   248,   249,   243,   254,   107,   244,   119,   237,   247,
     187,   161,   195,   228,   262,   263,   264,   265,   266,   267,
     247,   355,    80,   203,   351,   234,   376,   377,   187,   252,
     238,   350,   352,   252,   246,   108,   228,   187,    41,    64,
      66,    85,   107,   129,   161,   172,   182,   187,   192,   193,
     196,   198,   199,   200,   352,   371,   372,   373,   374,   375,
     220,   266,    11,    28,    49,    57,    92,   123,   136,   137,
     145,   163,   166,   179,   269,   272,   273,   274,   275,   278,
     279,   280,   299,   337,   182,   231,   187,   198,   200,   204,
     356,   357,   203,   356,   356,   129,   198,   378,    85,   196,
     196,   243,   187,   250,   374,   216,   184,   187,   192,   337,
     339,   187,   198,   200,   270,   304,   305,   307,   308,   313,
     273,    17,    21,    45,    61,    86,   149,   164,   174,   178,
     185,   276,   283,   247,   358,   356,    28,   179,   356,   357,
     356,   372,   238,   187,   352,   217,   301,   302,   339,   185,
     192,   300,   309,   307,   279,   314,   315,   268,   196,   338,
     307,   175,   277,   187,   281,   288,   117,   201,   205,   360,
     361,   362,   199,   356,   356,    48,   193,   196,   197,   338,
     301,   198,   201,   311,   312,   316,   340,   199,   279,   271,
     338,   305,   197,   285,   338,   282,   187,   363,   187,   117,
     205,   362,   359,    36,   302,   303,   193,   199,   317,   341,
     316,   340,   310,   194,   306,   192,   284,   285,   192,   198,
     201,   364,   365,   369,   370,   187,   187,   360,   128,    66,
     192,   198,   201,   343,   344,   345,   349,   252,   319,   321,
     342,   344,   311,   343,   286,   368,   366,   367,   202,   365,
     194,   196,   346,   347,   348,   349,   346,   346,   349,   318,
     322,   325,   326,   318,   202,   337,   289,   290,   292,   293,
     369,   369,   369,   193,   348,   199,   202,   199,   320,   196,
     323,   324,   272,   199,   287,   290,   195,   294,   295,   296,
     307,   272,   193,   199,   202,    47,   325,   196,   187,   198,
     313,   327,   328,   329,   340,   193,   297,   291,   196,   195,
     325,   332,   321,   328,   336,   327,   338,   333,   343,   271,
     296,   298,   198,   334,   335,   340,   199,   199,   334,   343,
     336,   198,   340,   330,   331,   199,   336,   334,   334,   199
};


/* Prevent warning if -Wmissing-prototypes.  */
int yyparse (void);

/* Error token number */
#define YYTERROR 1

/* YYLLOC_DEFAULT -- Set CURRENT to span from RHS[1] to RHS[N].
   If N is 0, then set CURRENT to the empty location which ends
   the previous symbol: RHS[0] (always defined).  */


#ifndef YYLLOC_DEFAULT
# define YYLLOC_DEFAULT(Current, Rhs, N) ((void) 0)
#endif


#ifndef YY_LOCATION_PRINT
# define YY_LOCATION_PRINT(File, Loc) ((void) 0)
#endif


/* YYLEX -- calling `yylex' with the right arguments.  */
#define YYLEX yylex ()

YYSTYPE yylval;

YYLTYPE yylloc;

int yynerrs;
int yychar;

static const int YYEOF = 0;
static const int YYEMPTY = -2;

typedef enum { yyok, yyaccept, yyabort, yyerr } YYRESULTTAG;

#define YYCHK(YYE)							     \
   do { YYRESULTTAG yyflag = YYE; if (yyflag != yyok) return yyflag; }	     \
   while (YYID (0))

#if YYDEBUG

# ifndef YYFPRINTF
#  define YYFPRINTF fprintf
# endif

# define YYDPRINTF(Args)			\
do {						\
  if (yydebug)					\
    YYFPRINTF Args;				\
} while (YYID (0))


/*--------------------------------.
| Print this symbol on YYOUTPUT.  |
`--------------------------------*/

/*ARGSUSED*/
static void
yy_symbol_value_print (FILE *yyoutput, int yytype, YYSTYPE const * const yyvaluep)
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

static void
yy_symbol_print (FILE *yyoutput, int yytype, YYSTYPE const * const yyvaluep)
{
  if (yytype < YYNTOKENS)
    YYFPRINTF (yyoutput, "token %s (", yytname[yytype]);
  else
    YYFPRINTF (yyoutput, "nterm %s (", yytname[yytype]);

  yy_symbol_value_print (yyoutput, yytype, yyvaluep);
  YYFPRINTF (yyoutput, ")");
}

# define YY_SYMBOL_PRINT(Title, Type, Value, Location)			    \
do {									    \
  if (yydebug)								    \
    {									    \
      YYFPRINTF (stderr, "%s ", Title);					    \
      yy_symbol_print (stderr, Type,					    \
		       Value);  \
      YYFPRINTF (stderr, "\n");						    \
    }									    \
} while (YYID (0))

/* Nonzero means print parse trace.  It is left uninitialized so that
   multiple parsers can coexist.  */
int yydebug;

#else /* !YYDEBUG */

# define YYDPRINTF(Args)
# define YY_SYMBOL_PRINT(Title, Type, Value, Location)

#endif /* !YYDEBUG */

/* YYINITDEPTH -- initial size of the parser's stacks.  */
#ifndef	YYINITDEPTH
# define YYINITDEPTH 200
#endif

/* YYMAXDEPTH -- maximum size the stacks can grow to (effective only
   if the built-in stack extension method is used).

   Do not make this value too large; the results are undefined if
   SIZE_MAX < YYMAXDEPTH * sizeof (GLRStackItem)
   evaluated with infinite-precision integer arithmetic.  */

#ifndef YYMAXDEPTH
# define YYMAXDEPTH 10000
#endif

/* Minimum number of free items on the stack allowed after an
   allocation.  This is to allow allocation and initialization
   to be completed by functions that call yyexpandGLRStack before the
   stack is expanded, thus insuring that all necessary pointers get
   properly redirected to new data.  */
#define YYHEADROOM 2

#ifndef YYSTACKEXPANDABLE
# if (! defined __cplusplus \
      || (defined YYSTYPE_IS_TRIVIAL && YYSTYPE_IS_TRIVIAL))
#  define YYSTACKEXPANDABLE 1
# else
#  define YYSTACKEXPANDABLE 0
# endif
#endif

#if YYSTACKEXPANDABLE
# define YY_RESERVE_GLRSTACK(Yystack)			\
  do {							\
    if (Yystack->yyspaceLeft < YYHEADROOM)		\
      yyexpandGLRStack (Yystack);			\
  } while (YYID (0))
#else
# define YY_RESERVE_GLRSTACK(Yystack)			\
  do {							\
    if (Yystack->yyspaceLeft < YYHEADROOM)		\
      yyMemoryExhausted (Yystack);			\
  } while (YYID (0))
#endif


#if YYERROR_VERBOSE

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
static size_t
yytnamerr (char *yyres, const char *yystr)
{
  if (*yystr == '"')
    {
      size_t yyn = 0;
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
    return strlen (yystr);

  return yystpcpy (yyres, yystr) - yyres;
}
# endif

#endif /* !YYERROR_VERBOSE */

/** State numbers, as in LALR(1) machine */
typedef int yyStateNum;

/** Rule numbers, as in LALR(1) machine */
typedef int yyRuleNum;

/** Grammar symbol */
typedef short int yySymbol;

/** Item references, as in LALR(1) machine */
typedef short int yyItemNum;

typedef struct yyGLRState yyGLRState;
typedef struct yyGLRStateSet yyGLRStateSet;
typedef struct yySemanticOption yySemanticOption;
typedef union yyGLRStackItem yyGLRStackItem;
typedef struct yyGLRStack yyGLRStack;

struct yyGLRState {
  /** Type tag: always true.  */
  yybool yyisState;
  /** Type tag for yysemantics.  If true, yysval applies, otherwise
   *  yyfirstVal applies.  */
  yybool yyresolved;
  /** Number of corresponding LALR(1) machine state.  */
  yyStateNum yylrState;
  /** Preceding state in this stack */
  yyGLRState* yypred;
  /** Source position of the first token produced by my symbol */
  size_t yyposn;
  union {
    /** First in a chain of alternative reductions producing the
     *  non-terminal corresponding to this state, threaded through
     *  yynext.  */
    yySemanticOption* yyfirstVal;
    /** Semantic value for this state.  */
    YYSTYPE yysval;
  } yysemantics;
  /** Source location for this state.  */
  YYLTYPE yyloc;
};

struct yyGLRStateSet {
  yyGLRState** yystates;
  /** During nondeterministic operation, yylookaheadNeeds tracks which
   *  stacks have actually needed the current lookahead.  During deterministic
   *  operation, yylookaheadNeeds[0] is not maintained since it would merely
   *  duplicate yychar != YYEMPTY.  */
  yybool* yylookaheadNeeds;
  size_t yysize, yycapacity;
};

struct yySemanticOption {
  /** Type tag: always false.  */
  yybool yyisState;
  /** Rule number for this reduction */
  yyRuleNum yyrule;
  /** The last RHS state in the list of states to be reduced.  */
  yyGLRState* yystate;
  /** The lookahead for this reduction.  */
  int yyrawchar;
  YYSTYPE yyval;
  YYLTYPE yyloc;
  /** Next sibling in chain of options.  To facilitate merging,
   *  options are chained in decreasing order by address.  */
  yySemanticOption* yynext;
};

/** Type of the items in the GLR stack.  The yyisState field
 *  indicates which item of the union is valid.  */
union yyGLRStackItem {
  yyGLRState yystate;
  yySemanticOption yyoption;
};

struct yyGLRStack {
  int yyerrState;


  YYJMP_BUF yyexception_buffer;
  yyGLRStackItem* yyitems;
  yyGLRStackItem* yynextFree;
  size_t yyspaceLeft;
  yyGLRState* yysplitPoint;
  yyGLRState* yylastDeleted;
  yyGLRStateSet yytops;
};

#if YYSTACKEXPANDABLE
static void yyexpandGLRStack (yyGLRStack* yystackp);
#endif

static void yyFail (yyGLRStack* yystackp, const char* yymsg)
  __attribute__ ((__noreturn__));
static void
yyFail (yyGLRStack* yystackp, const char* yymsg)
{
  if (yymsg != NULL)
    yyerror (yymsg);
  YYLONGJMP (yystackp->yyexception_buffer, 1);
}

static void yyMemoryExhausted (yyGLRStack* yystackp)
  __attribute__ ((__noreturn__));
static void
yyMemoryExhausted (yyGLRStack* yystackp)
{
  YYLONGJMP (yystackp->yyexception_buffer, 2);
}

#if YYDEBUG || YYERROR_VERBOSE
/** A printable representation of TOKEN.  */
static inline const char*
yytokenName (yySymbol yytoken)
{
  if (yytoken == YYEMPTY)
    return "";

  return yytname[yytoken];
}
#endif

/** Fill in YYVSP[YYLOW1 .. YYLOW0-1] from the chain of states starting
 *  at YYVSP[YYLOW0].yystate.yypred.  Leaves YYVSP[YYLOW1].yystate.yypred
 *  containing the pointer to the next state in the chain.  */
static void yyfillin (yyGLRStackItem *, int, int) __attribute__ ((__unused__));
static void
yyfillin (yyGLRStackItem *yyvsp, int yylow0, int yylow1)
{
  yyGLRState* s;
  int i;
  s = yyvsp[yylow0].yystate.yypred;
  for (i = yylow0-1; i >= yylow1; i -= 1)
    {
      YYASSERT (s->yyresolved);
      yyvsp[i].yystate.yyresolved = yytrue;
      yyvsp[i].yystate.yysemantics.yysval = s->yysemantics.yysval;
      yyvsp[i].yystate.yyloc = s->yyloc;
      s = yyvsp[i].yystate.yypred = s->yypred;
    }
}

/* Do nothing if YYNORMAL or if *YYLOW <= YYLOW1.  Otherwise, fill in
 * YYVSP[YYLOW1 .. *YYLOW-1] as in yyfillin and set *YYLOW = YYLOW1.
 * For convenience, always return YYLOW1.  */
static inline int yyfill (yyGLRStackItem *, int *, int, yybool)
     __attribute__ ((__unused__));
static inline int
yyfill (yyGLRStackItem *yyvsp, int *yylow, int yylow1, yybool yynormal)
{
  if (!yynormal && yylow1 < *yylow)
    {
      yyfillin (yyvsp, *yylow, yylow1);
      *yylow = yylow1;
    }
  return yylow1;
}

/** Perform user action for rule number YYN, with RHS length YYRHSLEN,
 *  and top stack item YYVSP.  YYLVALP points to place to put semantic
 *  value ($$), and yylocp points to place for location information
 *  (@$).  Returns yyok for normal return, yyaccept for YYACCEPT,
 *  yyerr for YYERROR, yyabort for YYABORT.  */
/*ARGSUSED*/ static YYRESULTTAG
yyuserAction (yyRuleNum yyn, int yyrhslen, yyGLRStackItem* yyvsp,
	      YYSTYPE* yyvalp,
	      YYLTYPE* YYOPTIONAL_LOC (yylocp),
	      yyGLRStack* yystackp
	      )
{
  yybool yynormal __attribute__ ((__unused__)) =
    (yystackp->yysplitPoint == NULL);
  int yylow;
# undef yyerrok
# define yyerrok (yystackp->yyerrState = 0)
# undef YYACCEPT
# define YYACCEPT return yyaccept
# undef YYABORT
# define YYABORT return yyabort
# undef YYERROR
# define YYERROR return yyerrok, yyerr
# undef YYRECOVERING
# define YYRECOVERING() (yystackp->yyerrState != 0)
# undef yyclearin
# define yyclearin (yychar = YYEMPTY)
# undef YYFILL
# define YYFILL(N) yyfill (yyvsp, &yylow, N, yynormal)
# undef YYBACKUP
# define YYBACKUP(Token, Value)						     \
  return yyerror (YY_("syntax error: cannot back up")),     \
	 yyerrok, yyerr

  yylow = 1;
  if (yyrhslen == 0)
    *yyvalp = yyval_default;
  else
    *yyvalp = yyvsp[YYFILL (1-yyrhslen)].yystate.yysemantics.yysval;
  YYLLOC_DEFAULT ((*yylocp), (yyvsp - yyrhslen), yyrhslen);

  switch (yyn)
    {
        case 2:
#line 446 "../../src/executables/esql_grammar.y"
    {{
			
			pp_finish ();
			vs_free (&pt_statement_buf);
						/* verify push/pop modes were matched. */
			esql_yy_check_mode ();
			mm_free();
		DBG_PRINT};}
    break;

  case 3:
#line 458 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 4:
#line 460 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 5:
#line 465 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 6:
#line 467 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 7:
#line 472 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 8:
#line 474 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 9:
#line 476 "../../src/executables/esql_grammar.y"
    {{
			
			pp_push_name_scope ();
			
		DBG_PRINT};}
    break;

  case 10:
#line 482 "../../src/executables/esql_grammar.y"
    {{
			
			pp_pop_name_scope ();
			
		DBG_PRINT};}
    break;

  case 11:
#line 488 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 12:
#line 493 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 13:
#line 498 "../../src/executables/esql_grammar.y"
    {{
			
			esql_yy_enter (ECHO_mode);
			pp_suppress_echo (false);
			
		DBG_PRINT};}
    break;

  case 14:
#line 505 "../../src/executables/esql_grammar.y"
    {{
			
			esql_yy_enter (ECHO_mode);
			pp_suppress_echo (false);
			
		DBG_PRINT};}
    break;

  case 15:
#line 515 "../../src/executables/esql_grammar.y"
    {{
			
			esql_yy_enter (CSQL_mode);
			reset_globals ();
			
		DBG_PRINT};}
    break;

  case 16:
#line 525 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 17:
#line 531 "../../src/executables/esql_grammar.y"
    {{
			
			esql_yy_enter (C_mode);
			pp_suppress_echo (false);
			
		DBG_PRINT};}
    break;

  case 18:
#line 538 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 19:
#line 540 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 20:
#line 545 "../../src/executables/esql_grammar.y"
    {{
			
			ignore_repeat ();
			esql_yy_sync_lineno ();
			esql_yy_set_buf (NULL);
			
		DBG_PRINT};}
    break;

  case 21:
#line 553 "../../src/executables/esql_grammar.y"
    {{
			
			ignore_repeat ();
			
		DBG_PRINT};}
    break;

  case 22:
#line 559 "../../src/executables/esql_grammar.y"
    {{
			
			vs_clear (&pt_statement_buf);
			esql_yy_set_buf (&pt_statement_buf);
			
		DBG_PRINT};}
    break;

  case 23:
#line 566 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 24:
#line 571 "../../src/executables/esql_grammar.y"
    {{
			
			repeat_option = false;
			
		DBG_PRINT};}
    break;

  case 25:
#line 577 "../../src/executables/esql_grammar.y"
    {{
			
			repeat_option = true;
			
		DBG_PRINT};}
    break;

  case 26:
#line 586 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 27:
#line 588 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 28:
#line 590 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 29:
#line 592 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 30:
#line 594 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 31:
#line 596 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 32:
#line 598 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 33:
#line 600 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 34:
#line 602 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 35:
#line 604 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 36:
#line 606 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 37:
#line 608 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 38:
#line 610 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 39:
#line 615 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 40:
#line 620 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).number) = SQLWARNING;
			
		DBG_PRINT};}
    break;

  case 41:
#line 626 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).number) = SQLERROR;
			
		DBG_PRINT};}
    break;

  case 42:
#line 632 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).number) = NOT_FOUND;
			
		DBG_PRINT};}
    break;

  case 43:
#line 641 "../../src/executables/esql_grammar.y"
    {{
			
			pp_add_whenever_to_scope ((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (2))].yystate.yysemantics.yysval.number), CONTINUE, NULL);
			
		DBG_PRINT};}
    break;

  case 44:
#line 647 "../../src/executables/esql_grammar.y"
    {{
			
			pp_add_whenever_to_scope ((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (2))].yystate.yysemantics.yysval.number), STOP, NULL);
			
		DBG_PRINT};}
    break;

  case 45:
#line 653 "../../src/executables/esql_grammar.y"
    {{
			
			pp_add_whenever_to_scope ((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (4))].yystate.yysemantics.yysval.number), GOTO, (((yyGLRStackItem const *)yyvsp)[YYFILL ((4) - (4))].yystate.yysemantics.yysval.ptr));
			
		DBG_PRINT};}
    break;

  case 46:
#line 659 "../../src/executables/esql_grammar.y"
    {{
			
			pp_add_whenever_to_scope ((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (5))].yystate.yysemantics.yysval.number), GOTO, (((yyGLRStackItem const *)yyvsp)[YYFILL ((5) - (5))].yystate.yysemantics.yysval.ptr));
			
		DBG_PRINT};}
    break;

  case 47:
#line 665 "../../src/executables/esql_grammar.y"
    {{
			
			pp_add_whenever_to_scope ((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (3))].yystate.yysemantics.yysval.number), CALL, (((yyGLRStackItem const *)yyvsp)[YYFILL ((3) - (3))].yystate.yysemantics.yysval.ptr));
			
		DBG_PRINT};}
    break;

  case 48:
#line 674 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 49:
#line 676 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 50:
#line 681 "../../src/executables/esql_grammar.y"
    {{
			
			HOST_LOD *hvars;
			HOST_REF *href = (((yyGLRStackItem const *)yyvsp)[YYFILL ((2) - (3))].yystate.yysemantics.yysval.ptr);
			
			if ((((yyGLRStackItem const *)yyvsp)[YYFILL ((3) - (3))].yystate.yysemantics.yysval.ptr))
			  href = (((yyGLRStackItem const *)yyvsp)[YYFILL ((3) - (3))].yystate.yysemantics.yysval.ptr);
			
			hvars = pp_input_refs ();
			esql_Translate_table.tr_connect (CHECK_HOST_REF (hvars, 0),
							 CHECK_HOST_REF (hvars, 1),
							 CHECK_HOST_REF (hvars, 2));
			already_translated = true;
			
		DBG_PRINT};}
    break;

  case 51:
#line 700 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).ptr) = NULL;
			
		DBG_PRINT};}
    break;

  case 52:
#line 706 "../../src/executables/esql_grammar.y"
    {{
			
			if ((((yyGLRStackItem const *)yyvsp)[YYFILL ((4) - (4))].yystate.yysemantics.yysval.ptr))
			  {
			    ((*yyvalp).ptr) = (((yyGLRStackItem const *)yyvsp)[YYFILL ((4) - (4))].yystate.yysemantics.yysval.ptr);
			  }
			else
			  {
			    ((*yyvalp).ptr) = (((yyGLRStackItem const *)yyvsp)[YYFILL ((3) - (4))].yystate.yysemantics.yysval.ptr);
			  }
			
		DBG_PRINT};}
    break;

  case 53:
#line 722 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).ptr) = NULL;
			
		DBG_PRINT};}
    break;

  case 54:
#line 728 "../../src/executables/esql_grammar.y"
    {{
			
			
			((*yyvalp).ptr) = (((yyGLRStackItem const *)yyvsp)[YYFILL ((2) - (2))].yystate.yysemantics.yysval.ptr);
			
		DBG_PRINT};}
    break;

  case 55:
#line 738 "../../src/executables/esql_grammar.y"
    {{
			
			esql_Translate_table.tr_disconnect ();
			already_translated = true;
			
		DBG_PRINT};}
    break;

  case 56:
#line 748 "../../src/executables/esql_grammar.y"
    {{
			
			char **cp = (((yyGLRStackItem const *)yyvsp)[YYFILL ((5) - (5))].yystate.yysemantics.yysval.ptr);
			char *c_nam = strdup((((yyGLRStackItem const *)yyvsp)[YYFILL ((2) - (5))].yystate.yysemantics.yysval.ptr));
			pp_new_cursor (c_nam, cp[0], (int) (long) cp[1], NULL,
				       pp_detach_host_refs ());
			free_and_init(c_nam);
			
		DBG_PRINT};}
    break;

  case 57:
#line 758 "../../src/executables/esql_grammar.y"
    {{
			
			char *c_nam = strdup((((yyGLRStackItem const *)yyvsp)[YYFILL ((2) - (5))].yystate.yysemantics.yysval.ptr));
			pp_new_cursor (c_nam, NULL, 0, (STMT *) (((yyGLRStackItem const *)yyvsp)[YYFILL ((5) - (5))].yystate.yysemantics.yysval.ptr), pp_detach_host_refs ());
			free_and_init(c_nam);
			esql_yy_sync_lineno ();
			esql_yy_set_buf (NULL);
			
		DBG_PRINT};}
    break;

  case 58:
#line 772 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).ptr) = pp_new_stmt ((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr));
			
		DBG_PRINT};}
    break;

  case 59:
#line 782 "../../src/executables/esql_grammar.y"
    {{
			
			CURSOR *cur = (((yyGLRStackItem const *)yyvsp)[YYFILL ((2) - (4))].yystate.yysemantics.yysval.ptr);
			int rdonly = (((yyGLRStackItem const *)yyvsp)[YYFILL ((3) - (4))].yystate.yysemantics.yysval.number);
			HOST_LOD *usg;
			
			if (cur->static_stmt)
			  {
			    if (pp_input_refs ())
			      esql_yyverror (pp_get_msg (EX_ESQLM_SET, MSG_USING_NOT_PERMITTED),
					     cur->name);
			    else
			      esql_Translate_table.tr_open_cs (cur->cid,
							       (const char *) cur->static_stmt,
							       cur->stmtLength,
							       -1,
							       rdonly,
							       HOST_N_REFS (cur->host_refs),
							       HOST_REFS (cur->host_refs),
							       (const char *) HOST_DESC (cur->
											 host_refs));
			  }
			else if (cur->dynamic_stmt)
			  {
			    usg = pp_input_refs ();
			    esql_Translate_table.tr_open_cs (cur->cid,
							     NULL,
							     (int) 0,
							     cur->dynamic_stmt->sid,
							     rdonly,
							     HOST_N_REFS (usg), HOST_REFS (usg),
							     (const char *) HOST_DESC (usg));
			  }
			else
			  {
			    esql_yyverror (pp_get_msg (EX_ESQLM_SET, MSG_CURSOR_UNDEFINED),
					   cur->name);
			  }
			
			
			already_translated = true;
			
		DBG_PRINT};}
    break;

  case 60:
#line 829 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).number) = 0;
			
		DBG_PRINT};}
    break;

  case 61:
#line 835 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).number) = 1;
			
		DBG_PRINT};}
    break;

  case 62:
#line 844 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 63:
#line 846 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 64:
#line 851 "../../src/executables/esql_grammar.y"
    {{
			
			HOST_REF *href = (((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr);
			char *nam = NULL;
			if (pp_check_type (href,
					   NEWSET (C_TYPE_SQLDA),
					   pp_get_msg (EX_ESQLM_SET, MSG_PTR_TO_DESCR)))
			  nam = pp_switch_to_descriptor ();
			
			((*yyvalp).ptr) = nam;
			
		DBG_PRINT};}
    break;

  case 65:
#line 867 "../../src/executables/esql_grammar.y"
    {{
			
			CURSOR *cur = (((yyGLRStackItem const *)yyvsp)[YYFILL ((2) - (2))].yystate.yysemantics.yysval.ptr);
			if (cur)
			  esql_Translate_table.tr_close_cs (cur->cid);
			already_translated = true;
			
		DBG_PRINT};}
    break;

  case 66:
#line 879 "../../src/executables/esql_grammar.y"
    {{
			
			CURSOR *cur = NULL;
			if ((cur = pp_lookup_cursor ((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr))) == NULL)
			  esql_yyverror (pp_get_msg (EX_ESQLM_SET, MSG_CURSOR_UNDEFINED), (((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr));
			
			((*yyvalp).ptr) = cur;
			
		DBG_PRINT};}
    break;

  case 67:
#line 892 "../../src/executables/esql_grammar.y"
    {{
			
			STMT *d_statement = (((yyGLRStackItem const *)yyvsp)[YYFILL ((2) - (4))].yystate.yysemantics.yysval.ptr);
			char *nam = (((yyGLRStackItem const *)yyvsp)[YYFILL ((4) - (4))].yystate.yysemantics.yysval.ptr);
			
			if (d_statement && nam)
			  esql_Translate_table.tr_describe (d_statement->sid, nam);
			already_translated = true;
			
		DBG_PRINT};}
    break;

  case 68:
#line 903 "../../src/executables/esql_grammar.y"
    {{
			
			HOST_REF *href = (((yyGLRStackItem const *)yyvsp)[YYFILL ((3) - (7))].yystate.yysemantics.yysval.ptr);
			PTR_VEC *pvec = (((yyGLRStackItem const *)yyvsp)[YYFILL ((5) - (7))].yystate.yysemantics.yysval.ptr);
			char *nam = (((yyGLRStackItem const *)yyvsp)[YYFILL ((7) - (7))].yystate.yysemantics.yysval.ptr);
			
			pp_copy_host_refs ();
			
			if (href && pvec && nam &&
			    pp_check_type (href,
					   NEWSET (C_TYPE_OBJECTID),
					   pp_get_msg (EX_ESQLM_SET, MSG_PTR_TO_DB_OBJECT)))
			  {
			    esql_Translate_table.tr_object_describe (href,
								     pp_ptr_vec_n_elems (pvec),
								     (const char **)
								     pp_ptr_vec_elems (pvec), nam);
			  }
			
			pp_free_ptr_vec (pvec);
			already_translated = true;
			
		DBG_PRINT};}
    break;

  case 69:
#line 930 "../../src/executables/esql_grammar.y"
    {{
			
			HOST_REF *href = (((yyGLRStackItem const *)yyvsp)[YYFILL ((4) - (4))].yystate.yysemantics.yysval.ptr);
			STMT *d_statement = (((yyGLRStackItem const *)yyvsp)[YYFILL ((2) - (4))].yystate.yysemantics.yysval.ptr);
			
			if (d_statement && href)
			  esql_Translate_table.tr_prepare_esql (d_statement->sid, href);
			
			already_translated = true;
			
		DBG_PRINT};}
    break;

  case 70:
#line 945 "../../src/executables/esql_grammar.y"
    {{
			
			pp_gather_output_refs ();
			esql_yy_erase_last_token ();
			esql_yy_set_buf (NULL);
			
		DBG_PRINT};}
    break;

  case 71:
#line 953 "../../src/executables/esql_grammar.y"
    {{
			
			pp_gather_input_refs ();
			
		DBG_PRINT};}
    break;

  case 74:
#line 967 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 75:
#line 969 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 76:
#line 974 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).ptr) = pp_add_host_str ((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr));
			
		DBG_PRINT};}
    break;

  case 77:
#line 980 "../../src/executables/esql_grammar.y"
    {{
			
			HOST_REF *href = (((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr);
			((*yyvalp).ptr) = pp_check_type (href,
					    NEWSET (C_TYPE_CHAR_ARRAY) |
					    NEWSET (C_TYPE_CHAR_POINTER) |
					    NEWSET (C_TYPE_STRING_CONST) |
					    NEWSET (C_TYPE_VARCHAR) |
					    NEWSET (C_TYPE_NCHAR) |
					    NEWSET (C_TYPE_VARNCHAR),
					    pp_get_msg (EX_ESQLM_SET, MSG_CHAR_STRING));
			
		DBG_PRINT};}
    break;

  case 78:
#line 997 "../../src/executables/esql_grammar.y"
    {{
			
			esql_yy_erase_last_token ();
			structs_allowed = true;
			
		DBG_PRINT};}
    break;

  case 79:
#line 1004 "../../src/executables/esql_grammar.y"
    {{
			
			pp_check_host_var_list ();
			ECHO;
			
		DBG_PRINT};}
    break;

  case 80:
#line 1014 "../../src/executables/esql_grammar.y"
    {{
			
			esql_yy_erase_last_token ();
			
		DBG_PRINT};}
    break;

  case 81:
#line 1020 "../../src/executables/esql_grammar.y"
    {{
			
			HOST_REF *href2;
			
			href2 = (((yyGLRStackItem const *)yyvsp)[YYFILL ((2) - (2))].yystate.yysemantics.yysval.ptr);
			if (href2 && href2->ind)
			  {
			    esql_yyverror (pp_get_msg (EX_ESQLM_SET, MSG_INDICATOR_NOT_ALLOWED),
					   pp_get_expr (href2));
			    pp_free_host_var (href2->ind);
			    href2->ind = NULL;
			  }
			
			((*yyvalp).ptr) = href2;
			
		DBG_PRINT};}
    break;

  case 82:
#line 1040 "../../src/executables/esql_grammar.y"
    {{
			
			PTR_VEC *pvec = NULL;
			pvec = pp_add_ptr (pvec, strdup ((((yyGLRStackItem const *)yyvsp)[YYFILL ((3) - (3))].yystate.yysemantics.yysval.ptr)));
			((*yyvalp).ptr) = pvec;
			
		DBG_PRINT};}
    break;

  case 83:
#line 1048 "../../src/executables/esql_grammar.y"
    {{
			
			PTR_VEC *pvec;
			pvec = pp_new_ptr_vec (&id_list);
			pp_add_ptr (&id_list, strdup ((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr)));
			
			((*yyvalp).ptr) = pvec;
			
		DBG_PRINT};}
    break;

  case 84:
#line 1061 "../../src/executables/esql_grammar.y"
    {{
			
			STMT *d_statement = (((yyGLRStackItem const *)yyvsp)[YYFILL ((2) - (4))].yystate.yysemantics.yysval.ptr);
			if (d_statement)
			  {
			    HOST_LOD *inputs, *outputs;
			    inputs = pp_input_refs ();
			    outputs = pp_output_refs ();
			    esql_Translate_table.tr_execute (d_statement->sid,
							     HOST_N_REFS (inputs),
							     HOST_REFS (inputs),
							     (const char *) HOST_DESC (inputs),
							     HOST_N_REFS (outputs),
							     HOST_REFS (outputs),
							     (const char *) HOST_DESC (outputs));
			    already_translated = true;
			  }
			
		DBG_PRINT};}
    break;

  case 85:
#line 1081 "../../src/executables/esql_grammar.y"
    {{
			
			HOST_REF *href = (((yyGLRStackItem const *)yyvsp)[YYFILL ((3) - (3))].yystate.yysemantics.yysval.ptr);
			
			if (href)
			  esql_Translate_table.tr_execute_immediate (href);
			already_translated = true;
			
		DBG_PRINT};}
    break;

  case 88:
#line 1099 "../../src/executables/esql_grammar.y"
    {{
			
			CURSOR *cur = (((yyGLRStackItem const *)yyvsp)[YYFILL ((2) - (3))].yystate.yysemantics.yysval.ptr);
			if (cur)
			  {
			    HOST_LOD *hlod = pp_output_refs ();
			    esql_Translate_table.tr_fetch_cs (cur->cid,
							      HOST_N_REFS (hlod), HOST_REFS (hlod),
							      (char *) HOST_DESC (hlod));
			  }
			
			already_translated = true;
			
		DBG_PRINT};}
    break;

  case 89:
#line 1114 "../../src/executables/esql_grammar.y"
    {{
			
			HOST_REF *href = (((yyGLRStackItem const *)yyvsp)[YYFILL ((3) - (6))].yystate.yysemantics.yysval.ptr);
			PTR_VEC *pvec = (((yyGLRStackItem const *)yyvsp)[YYFILL ((5) - (6))].yystate.yysemantics.yysval.ptr);
			HOST_LOD *hlod;
			
			
			if (href && pvec
			    && (hlod = pp_output_refs ())
			    && pp_check_type (href,
					      NEWSET
					      (C_TYPE_OBJECTID),
					      pp_get_msg (EX_ESQLM_SET, MSG_PTR_TO_DB_OBJECT)))
			  {
			    esql_Translate_table.tr_object_fetch (href,
								  pp_ptr_vec_n_elems (pvec),
								  (const char **)
								  pp_ptr_vec_elems (pvec),
								  HOST_N_REFS (hlod),
								  HOST_REFS (hlod),
								  (const char *) HOST_DESC (hlod));
			  }
			
			pp_free_ptr_vec (pvec);
			already_translated = true;
			
		DBG_PRINT};}
    break;

  case 90:
#line 1145 "../../src/executables/esql_grammar.y"
    {{
			
			esql_Translate_table.tr_commit ();
			already_translated = true;
			
		DBG_PRINT};}
    break;

  case 93:
#line 1160 "../../src/executables/esql_grammar.y"
    {{
			
			esql_Translate_table.tr_rollback ();
			already_translated = true;
			
		DBG_PRINT};}
    break;

  case 94:
#line 1170 "../../src/executables/esql_grammar.y"
    {{
			
			ECHO_STR ((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr), strlen ((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr)));
			((*yyvalp).number) = NO_CURSOR;
			
		DBG_PRINT};}
    break;

  case 95:
#line 1177 "../../src/executables/esql_grammar.y"
    {{
			
			ECHO_STR ((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr), strlen ((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr)));
			((*yyvalp).number) = NO_CURSOR;
			
		DBG_PRINT};}
    break;

  case 96:
#line 1184 "../../src/executables/esql_grammar.y"
    {{
			
			ECHO_STR ((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr), strlen ((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr)));
			((*yyvalp).number) = NO_CURSOR;
			
		DBG_PRINT};}
    break;

  case 97:
#line 1191 "../../src/executables/esql_grammar.y"
    {{
			
			ECHO_STR ((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr), strlen ((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr)));
			((*yyvalp).number) = NO_CURSOR;
			
		DBG_PRINT};}
    break;

  case 98:
#line 1198 "../../src/executables/esql_grammar.y"
    {{
			
			ECHO_STR ((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr), strlen ((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr)));
			((*yyvalp).number) = NO_CURSOR;
			
		DBG_PRINT};}
    break;

  case 99:
#line 1205 "../../src/executables/esql_grammar.y"
    {{
			
			ECHO_STR ((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr), strlen ((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr)));
			((*yyvalp).number) = NO_CURSOR;
			
		DBG_PRINT};}
    break;

  case 100:
#line 1212 "../../src/executables/esql_grammar.y"
    {{
			
			ECHO_STR ((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr), strlen ((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr)));
			((*yyvalp).number) = NO_CURSOR;
			
		DBG_PRINT};}
    break;

  case 101:
#line 1219 "../../src/executables/esql_grammar.y"
    {{
			
			ECHO_STR ((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr), strlen ((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr)));
			((*yyvalp).number) = NO_CURSOR;
			
		DBG_PRINT};}
    break;

  case 102:
#line 1226 "../../src/executables/esql_grammar.y"
    {{
			
			ECHO_STR ((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr), strlen ((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr)));
			((*yyvalp).number) = NO_CURSOR;
			
		DBG_PRINT};}
    break;

  case 103:
#line 1233 "../../src/executables/esql_grammar.y"
    {{
			
			ECHO_STR ((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr), strlen ((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr)));
			((*yyvalp).number) = NO_CURSOR;
			
		DBG_PRINT};}
    break;

  case 104:
#line 1240 "../../src/executables/esql_grammar.y"
    {{
			
			ECHO_STR ((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr), strlen ((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr)));
			((*yyvalp).number) = NO_CURSOR;
			
		DBG_PRINT};}
    break;

  case 105:
#line 1247 "../../src/executables/esql_grammar.y"
    {{
			
			ECHO_STR ((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr), strlen ((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr)));
			((*yyvalp).number) = NO_CURSOR;
			
		DBG_PRINT};}
    break;

  case 106:
#line 1254 "../../src/executables/esql_grammar.y"
    {{
			
			ECHO_STR ((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr), strlen ((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr)));
			((*yyvalp).number) = NO_CURSOR;
			
		DBG_PRINT};}
    break;

  case 107:
#line 1261 "../../src/executables/esql_grammar.y"
    {{
			
			ECHO_STR ((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr), strlen ((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr)));
			((*yyvalp).number) = NO_CURSOR;
			
		DBG_PRINT};}
    break;

  case 108:
#line 1268 "../../src/executables/esql_grammar.y"
    {{
			
			ECHO_STR ((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr), strlen ((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr)));
			((*yyvalp).number) = NO_CURSOR;
			
		DBG_PRINT};}
    break;

  case 109:
#line 1275 "../../src/executables/esql_grammar.y"
    {{
			
			ECHO_STR ((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr), strlen ((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr)));
			((*yyvalp).number) = NO_CURSOR;
			
		DBG_PRINT};}
    break;

  case 110:
#line 1282 "../../src/executables/esql_grammar.y"
    {{
			
			ECHO_STR ((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr), strlen ((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr)));
			((*yyvalp).number) = NO_CURSOR;
			
		DBG_PRINT};}
    break;

  case 111:
#line 1289 "../../src/executables/esql_grammar.y"
    {{
			
			ECHO_STR ((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr), strlen ((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr)));
			((*yyvalp).number) = NO_CURSOR;
			
		DBG_PRINT};}
    break;

  case 112:
#line 1296 "../../src/executables/esql_grammar.y"
    {{
			
			ECHO_STR ((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr), strlen ((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr)));
			((*yyvalp).number) = NO_CURSOR;
			
		DBG_PRINT};}
    break;

  case 113:
#line 1303 "../../src/executables/esql_grammar.y"
    {{
			
			ECHO_STR ((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr), strlen ((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr)));
			((*yyvalp).number) = NO_CURSOR;
			
		DBG_PRINT};}
    break;

  case 114:
#line 1310 "../../src/executables/esql_grammar.y"
    {{
			
			ECHO_STR ((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr), strlen ((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr)));
			((*yyvalp).number) = CURSOR_DELETE;
			
		DBG_PRINT};}
    break;

  case 115:
#line 1317 "../../src/executables/esql_grammar.y"
    {{
			
			ECHO_STR ((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr), strlen ((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr)));
			((*yyvalp).number) = CURSOR_UPDATE;
			
		DBG_PRINT};}
    break;

  case 116:
#line 1327 "../../src/executables/esql_grammar.y"
    {{
			
			esql_yy_set_buf (&pt_statement_buf);
			esql_yy_push_mode (BUFFER_mode);
			
		DBG_PRINT};}
    break;

  case 117:
#line 1334 "../../src/executables/esql_grammar.y"
    {{
			
			g_cursor_type = (((yyGLRStackItem const *)yyvsp)[YYFILL ((2) - (2))].yystate.yysemantics.yysval.number);
						//esql_yy_push_mode(BUFFER_mode);
			
		DBG_PRINT};}
    break;

  case 118:
#line 1341 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 119:
#line 1349 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 120:
#line 1354 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 121:
#line 1356 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 122:
#line 1362 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 123:
#line 1364 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 124:
#line 1369 "../../src/executables/esql_grammar.y"
    {{
			
			pp_reset_current_type_spec ();
			
		DBG_PRINT};}
    break;

  case 125:
#line 1375 "../../src/executables/esql_grammar.y"
    {{
			
			LINK *plink = (((yyGLRStackItem const *)yyvsp)[YYFILL ((2) - (3))].yystate.yysemantics.yysval.ptr);
			SYMBOL *psym = (((yyGLRStackItem const *)yyvsp)[YYFILL ((3) - (3))].yystate.yysemantics.yysval.ptr);
			
			if (psym)
			  {
			    pp_add_spec_to_decl (plink, psym);
			    /*
			     * If we have a VARCHAR declaration that didn't
			     * come from a typedef, it has been suppressed
			     * (i.e., it hasn't yet been echoed to the output
			     * stream because it needs to be rewritten).  Now
			     * is the time to echo the rewritten decl out.
			     *
			     * If the decl came from a typedef, it's already
			     * been echoed.  That's fine, because it doesn't
			     * need to be rewritten in that case.
			     */
			    if (IS_PSEUDO_TYPE (plink) && !plink->from_tdef)
			      {
				pp_print_decls (psym, true);
				esql_yy_sync_lineno ();
			      }
			    /*
			     * Don't do this until after printing, because
			     * it's going to jack with the 'next' links in
			     * the symbols, which will cause the printer to
			     * repeat declarations.
			     */
			    pp_add_symbols_to_table (psym);
			  }
			else if (plink)
			  {
			    /* pp_print_specs(plink); */
			  }
			if (plink && !plink->tdef)
			  pp_discard_link_chain (plink);
			
		DBG_PRINT};}
    break;

  case 127:
#line 1420 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).ptr) = pp_current_type_spec ();
			
		DBG_PRINT};}
    break;

  case 128:
#line 1426 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).ptr) = (((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr);
			
		DBG_PRINT};}
    break;

  case 129:
#line 1435 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).ptr) = NULL;
			
		DBG_PRINT};}
    break;

  case 130:
#line 1441 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).ptr) = (((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr);
			
		DBG_PRINT};}
    break;

  case 131:
#line 1450 "../../src/executables/esql_grammar.y"
    {{
			
						/*
						 * The specifier for this declarator list may have
						 * been a VARCHAR (or maybe not); if it was, echoing
						 * has been suppressed since we saw that keyword.
						 * Either way, we want to turn echoing back on.
						 */
			pp_suppress_echo (false);
			
		DBG_PRINT};}
    break;

  case 132:
#line 1465 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).ptr) = pp_current_type_spec ();
			
		DBG_PRINT};}
    break;

  case 133:
#line 1471 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).ptr) = pp_current_type_spec ();
			
		DBG_PRINT};}
    break;

  case 134:
#line 1480 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 135:
#line 1482 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 136:
#line 1484 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 137:
#line 1489 "../../src/executables/esql_grammar.y"
    {{
			
			pp_add_storage_class (TYPEDEF_);
			
		DBG_PRINT};}
    break;

  case 138:
#line 1495 "../../src/executables/esql_grammar.y"
    {{
			
			pp_add_storage_class (EXTERN_);
			
		DBG_PRINT};}
    break;

  case 139:
#line 1501 "../../src/executables/esql_grammar.y"
    {{
			
			pp_add_storage_class (STATIC_);
			
		DBG_PRINT};}
    break;

  case 140:
#line 1507 "../../src/executables/esql_grammar.y"
    {{
			
			pp_add_storage_class (AUTO_);
			
		DBG_PRINT};}
    break;

  case 141:
#line 1513 "../../src/executables/esql_grammar.y"
    {{
			
			pp_add_storage_class (REGISTER_);
			
		DBG_PRINT};}
    break;

  case 142:
#line 1522 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 143:
#line 1524 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 144:
#line 1526 "../../src/executables/esql_grammar.y"
    {{
			
			pp_add_struct_spec ((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr));
			
		DBG_PRINT};}
    break;

  case 145:
#line 1532 "../../src/executables/esql_grammar.y"
    {{
			
			pp_add_type_noun (INT_);
			pp_add_type_adj (INT_);
			
		DBG_PRINT};}
    break;

  case 146:
#line 1542 "../../src/executables/esql_grammar.y"
    {{
			
			pp_add_type_noun (VOID_);
			
		DBG_PRINT};}
    break;

  case 147:
#line 1548 "../../src/executables/esql_grammar.y"
    {{
			
			pp_add_type_noun (CHAR_);
			
		DBG_PRINT};}
    break;

  case 148:
#line 1554 "../../src/executables/esql_grammar.y"
    {{
			
			pp_add_type_noun (INT_);
			
		DBG_PRINT};}
    break;

  case 149:
#line 1560 "../../src/executables/esql_grammar.y"
    {{
			
			pp_add_type_noun (FLOAT_);
			
		DBG_PRINT};}
    break;

  case 150:
#line 1566 "../../src/executables/esql_grammar.y"
    {{
			
			pp_add_type_noun (DOUBLE_);
			
		DBG_PRINT};}
    break;

  case 151:
#line 1572 "../../src/executables/esql_grammar.y"
    {{
			
			pp_add_typedefed_spec (g_identifier_symbol->type);
			
		DBG_PRINT};}
    break;

  case 152:
#line 1578 "../../src/executables/esql_grammar.y"
    {{
			
						/*
						 * The VARCHAR_ token turns OFF echoing (the magic
						 * happens in check_c_identifier()) because we're
						 * going to have to rewrite the declaration.  This is
						 * slimy, but doing the right thing (parsing
						 * everything and then regurgitating it) is too
						 * expensive because we allow arbitrary initializers,
						 * etc.  Echoing will be re-enabled again when we see
						 * the end of the declarator list following the
						 * current specifier.
						 */
			pp_add_type_noun (VARCHAR_);
			
		DBG_PRINT};}
    break;

  case 153:
#line 1595 "../../src/executables/esql_grammar.y"
    {{
			
			pp_add_type_noun ((((yyGLRStackItem const *)yyvsp)[YYFILL ((2) - (2))].yystate.yysemantics.yysval.number));
			
		DBG_PRINT};}
    break;

  case 154:
#line 1604 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).number) = BIT_;
			
		DBG_PRINT};}
    break;

  case 155:
#line 1610 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).number) = VARBIT_;
			
		DBG_PRINT};}
    break;

  case 156:
#line 1619 "../../src/executables/esql_grammar.y"
    {{
			
			pp_add_type_adj (SHORT_);
			
		DBG_PRINT};}
    break;

  case 157:
#line 1625 "../../src/executables/esql_grammar.y"
    {{
			
			pp_add_type_adj (LONG_);
			
		DBG_PRINT};}
    break;

  case 158:
#line 1631 "../../src/executables/esql_grammar.y"
    {{
			
			pp_add_type_adj (SIGNED_);
			
		DBG_PRINT};}
    break;

  case 159:
#line 1637 "../../src/executables/esql_grammar.y"
    {{
			
			pp_add_type_adj (UNSIGNED_);
			
		DBG_PRINT};}
    break;

  case 160:
#line 1646 "../../src/executables/esql_grammar.y"
    {{
			
			pp_add_type_adj (CONST_);
			
		DBG_PRINT};}
    break;

  case 161:
#line 1652 "../../src/executables/esql_grammar.y"
    {{
			
			pp_add_type_adj (VOLATILE_);
			
		DBG_PRINT};}
    break;

  case 162:
#line 1662 "../../src/executables/esql_grammar.y"
    {{
			
			g_su = (((yyGLRStackItem const *)yyvsp)[YYFILL ((2) - (2))].yystate.yysemantics.yysval.number);
			g_sdef2 = NULL;
			
		DBG_PRINT};}
    break;

  case 163:
#line 1669 "../../src/executables/esql_grammar.y"
    {{
			
			STRUCTDEF *sdef = (((yyGLRStackItem const *)yyvsp)[YYFILL ((4) - (4))].yystate.yysemantics.yysval.ptr);
			((*yyvalp).ptr) = sdef;
			
		DBG_PRINT};}
    break;

  case 164:
#line 1678 "../../src/executables/esql_grammar.y"
    {{
			
			g_su = (((yyGLRStackItem const *)yyvsp)[YYFILL ((2) - (3))].yystate.yysemantics.yysval.number);
			g_sdef2 = (((yyGLRStackItem const *)yyvsp)[YYFILL ((3) - (3))].yystate.yysemantics.yysval.ptr);
			
		DBG_PRINT};}
    break;

  case 165:
#line 1685 "../../src/executables/esql_grammar.y"
    {{
			
			STRUCTDEF *sdef = (((yyGLRStackItem const *)yyvsp)[YYFILL ((5) - (5))].yystate.yysemantics.yysval.ptr);
			sdef = g_sdef2;
			sdef->by_name = true;
			((*yyvalp).ptr) = sdef;
			
		DBG_PRINT};}
    break;

  case 166:
#line 1697 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).number) = 1;
			
		DBG_PRINT};}
    break;

  case 167:
#line 1703 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).number) = 0;
			
		DBG_PRINT};}
    break;

  case 168:
#line 1712 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).ptr) = NULL;
			
		DBG_PRINT};}
    break;

  case 169:
#line 1718 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).ptr) = (((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr);
			
		DBG_PRINT};}
    break;

  case 170:
#line 1728 "../../src/executables/esql_grammar.y"
    {{
			
			pp_push_spec_scope ();
			
		DBG_PRINT};}
    break;

  case 171:
#line 1734 "../../src/executables/esql_grammar.y"
    {{
			
			pp_pop_spec_scope ();
			
		DBG_PRINT};}
    break;

  case 172:
#line 1740 "../../src/executables/esql_grammar.y"
    {{
			
			SYMBOL *psym = (((yyGLRStackItem const *)yyvsp)[YYFILL ((4) - (6))].yystate.yysemantics.yysval.ptr);
			
			if (g_sdef2 == NULL)
			  {
			    g_sdef2 = pp_new_structdef (NULL);
			    pp_Struct_table->add_symbol (pp_Struct_table, g_sdef2);
			  }
			if (psym == NULL)
			  {
			    pp_Struct_table->remove_symbol (pp_Struct_table, g_sdef2);
			    pp_discard_structdef (g_sdef2);
			  }
			else if (g_sdef2->fields)
			  {
			    varstring gack;
			    vs_new (&gack);
			    vs_sprintf (&gack, "%s %s", g_sdef2->type_string, g_sdef2->tag);
			    esql_yyredef (vs_str (&gack));
			    vs_free (&gack);
			    pp_discard_symbol_chain (psym);
			  }
			else
			  {
			    g_sdef2->type = g_su;
			    g_sdef2->type_string = (unsigned char *) (g_su ? "struct" : "union");
			    g_sdef2->fields = psym;
			  }
			
			((*yyvalp).ptr) = g_sdef2;
			
		DBG_PRINT};}
    break;

  case 173:
#line 1777 "../../src/executables/esql_grammar.y"
    {{
			
			STRUCTDEF *sdef;
			SYMBOL dummy;
			dummy.name = (((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr);
			
			if (!
			    (sdef =
			     (STRUCTDEF *) pp_Struct_table->find_symbol (pp_Struct_table, &dummy)))
			  {
			    sdef = pp_new_structdef (strdup((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr)));
			    pp_Struct_table->add_symbol (pp_Struct_table, sdef);
			  }
			
			((*yyvalp).ptr) = sdef;
			
		DBG_PRINT};}
    break;

  case 174:
#line 1798 "../../src/executables/esql_grammar.y"
    {{
			
			SYMBOL *last_field = (((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (2))].yystate.yysemantics.yysval.ptr);
			if (last_field)
			  {
			    SYMBOL *tmp = last_field->next;
			    last_field->next = (((yyGLRStackItem const *)yyvsp)[YYFILL ((2) - (2))].yystate.yysemantics.yysval.ptr);
			    last_field->next->next = tmp;
			  }
			
			((*yyvalp).ptr) = (((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (2))].yystate.yysemantics.yysval.ptr);
			
		DBG_PRINT};}
    break;

  case 175:
#line 1812 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).ptr) = (((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr);
			
		DBG_PRINT};}
    break;

  case 176:
#line 1821 "../../src/executables/esql_grammar.y"
    {{
			
			LINK *plink = (((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (2))].yystate.yysemantics.yysval.ptr);
			SYMBOL *psym = (((yyGLRStackItem const *)yyvsp)[YYFILL ((2) - (2))].yystate.yysemantics.yysval.ptr);
			SYMBOL *sym = NULL;
			
			if (psym)
			  {
			    pp_add_spec_to_decl (plink, psym);
			    /*
			     * Same deal as with an upper-level decl: this
			     * may have been a VARCHAR decl that needs to be
			     * rewritten.  If so, it has not yet been echoed;
			     * do so now.
			     */
			    if (IS_PSEUDO_TYPE (plink) && !plink->from_tdef)
			      {
				pp_print_decls (psym, true);
				esql_yy_sync_lineno ();
			      }
			    sym = psym;
			  }
			else
			  sym = NULL;
			
			if (!plink->tdef)
			  pp_discard_link_chain (plink);
			
			g_struct_declaration_sym = sym;
			
		DBG_PRINT};}
    break;

  case 177:
#line 1853 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).ptr) = g_struct_declaration_sym;
			
		DBG_PRINT};}
    break;

  case 178:
#line 1862 "../../src/executables/esql_grammar.y"
    {{
			
			pp_reset_current_type_spec ();
			pp_disallow_storage_classes ();
			
		DBG_PRINT};}
    break;

  case 179:
#line 1869 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).ptr) = (((yyGLRStackItem const *)yyvsp)[YYFILL ((2) - (2))].yystate.yysemantics.yysval.ptr);
			
		DBG_PRINT};}
    break;

  case 180:
#line 1878 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).ptr) = NULL;
			
		DBG_PRINT};}
    break;

  case 181:
#line 1884 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).ptr) = (((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr);
			
		DBG_PRINT};}
    break;

  case 182:
#line 1893 "../../src/executables/esql_grammar.y"
    {{
			
			SYMBOL *psym = (((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (3))].yystate.yysemantics.yysval.ptr);
			if (psym)
			  {
			    psym->next = (((yyGLRStackItem const *)yyvsp)[YYFILL ((3) - (3))].yystate.yysemantics.yysval.ptr);
			  }
			
			((*yyvalp).ptr) = psym;
			
		DBG_PRINT};}
    break;

  case 183:
#line 1905 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).ptr) = (((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr);
			
		DBG_PRINT};}
    break;

  case 184:
#line 1914 "../../src/executables/esql_grammar.y"
    {{
			
			esql_yy_push_mode (EXPR_mode);
			
		DBG_PRINT};}
    break;

  case 185:
#line 1920 "../../src/executables/esql_grammar.y"
    {{
			
			SYMBOL *psym = NULL;
			((*yyvalp).ptr) = psym;
			
		DBG_PRINT};}
    break;

  case 186:
#line 1928 "../../src/executables/esql_grammar.y"
    {{
			
			esql_yy_push_mode (EXPR_mode);
			
		DBG_PRINT};}
    break;

  case 187:
#line 1934 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).ptr) = (((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (4))].yystate.yysemantics.yysval.ptr);
			
		DBG_PRINT};}
    break;

  case 188:
#line 1940 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).ptr) = (((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr);
			
		DBG_PRINT};}
    break;

  case 190:
#line 1950 "../../src/executables/esql_grammar.y"
    {{
			
			SYMBOL *dl = (((yyGLRStackItem const *)yyvsp)[YYFILL ((2) - (3))].yystate.yysemantics.yysval.ptr);
			if (dl->type)
			  {
			    esql_yyredef ((char *) dl->name);
			  }
			else
			  {
			    pp_discard_symbol (dl);
			  }
			
		DBG_PRINT};}
    break;

  case 191:
#line 1967 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 192:
#line 1969 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 193:
#line 1974 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 194:
#line 1976 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 195:
#line 1981 "../../src/executables/esql_grammar.y"
    {{
			
			SYMBOL *dl = (((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr);
			pp_do_enum (dl);
			
		DBG_PRINT};}
    break;

  case 196:
#line 1989 "../../src/executables/esql_grammar.y"
    {{
			
			esql_yy_push_mode (EXPR_mode);
			
		DBG_PRINT};}
    break;

  case 197:
#line 1995 "../../src/executables/esql_grammar.y"
    {{
			
			SYMBOL *dl = (((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (4))].yystate.yysemantics.yysval.ptr);
			pp_do_enum (dl);
			
		DBG_PRINT};}
    break;

  case 198:
#line 2005 "../../src/executables/esql_grammar.y"
    {{
			
			SYMBOL *psym = (((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (3))].yystate.yysemantics.yysval.ptr);
			SYMBOL *psym3 = (((yyGLRStackItem const *)yyvsp)[YYFILL ((3) - (3))].yystate.yysemantics.yysval.ptr);
			if (psym3)
			  {
			    psym3->next = psym;
			  }
			
			((*yyvalp).ptr) = psym3;
			
		DBG_PRINT};}
    break;

  case 199:
#line 2019 "../../src/executables/esql_grammar.y"
    {{
			
			SYMBOL *psym = (((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr);
			if (psym)
			  psym->next = NULL;
			((*yyvalp).ptr) = psym;
			
		DBG_PRINT};}
    break;

  case 200:
#line 2031 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).ptr) = (((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr);
			
		DBG_PRINT};}
    break;

  case 201:
#line 2039 "../../src/executables/esql_grammar.y"
    {{
			
			esql_yy_push_mode (EXPR_mode);
			
		DBG_PRINT};}
    break;

  case 202:
#line 2045 "../../src/executables/esql_grammar.y"
    {{
			
			SYMBOL *psym = (((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (6))].yystate.yysemantics.yysval.ptr);
			pp_add_initializer (psym);
			
			((*yyvalp).ptr) = (((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (6))].yystate.yysemantics.yysval.ptr);
			
		DBG_PRINT};}
    break;

  case 203:
#line 2057 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).ptr) = (((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr);
			
		DBG_PRINT};}
    break;

  case 204:
#line 2063 "../../src/executables/esql_grammar.y"
    {{
			
			SYMBOL *psym = (((yyGLRStackItem const *)yyvsp)[YYFILL ((2) - (2))].yystate.yysemantics.yysval.ptr);
			if (psym)
			  pp_add_declarator (psym, D_POINTER);
			((*yyvalp).ptr) = psym;
			
		DBG_PRINT};}
    break;

  case 205:
#line 2075 "../../src/executables/esql_grammar.y"
    {{
			
			SYMBOL *psym = pp_new_symbol ((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr), pp_nesting_level);
			g_psym = psym;
			
		DBG_PRINT};}
    break;

  case 206:
#line 2082 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).ptr) = g_psym;
			
		DBG_PRINT};}
    break;

  case 207:
#line 2088 "../../src/executables/esql_grammar.y"
    {{
			
			SYMBOL *psym = pp_new_symbol ((((yyGLRStackItem const *)yyvsp)[YYFILL ((2) - (3))].yystate.yysemantics.yysval.ptr), pp_nesting_level);
			g_psym = psym;
			
		DBG_PRINT};}
    break;

  case 208:
#line 2095 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).ptr) = g_psym;
			
		DBG_PRINT};}
    break;

  case 209:
#line 2104 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 210:
#line 2106 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 211:
#line 2111 "../../src/executables/esql_grammar.y"
    {{
			
			SYMBOL *args = (((yyGLRStackItem const *)yyvsp)[YYFILL ((2) - (2))].yystate.yysemantics.yysval.ptr);
			if (g_psym)
			  {
			    pp_add_declarator (g_psym, D_FUNCTION);
			    g_psym->etype->decl.d.args = args;
			  }
			
		DBG_PRINT};}
    break;

  case 212:
#line 2122 "../../src/executables/esql_grammar.y"
    {{
			
			const char *sub = (((yyGLRStackItem const *)yyvsp)[YYFILL ((2) - (2))].yystate.yysemantics.yysval.ptr);
			if (g_psym)
			  {
			    pp_add_declarator (g_psym, D_ARRAY);
			    g_psym->etype->decl.d.num_ele = (char *) sub;
			  }
			
		DBG_PRINT};}
    break;

  case 213:
#line 2133 "../../src/executables/esql_grammar.y"
    {{
			
			SYMBOL *args = (((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr);
			if (g_psym)
			  {
			    pp_add_declarator (g_psym, D_FUNCTION);
			    g_psym->etype->decl.d.args = args;
			  }
			
		DBG_PRINT};}
    break;

  case 214:
#line 2144 "../../src/executables/esql_grammar.y"
    {{
			
			const char *sub = (((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr);
			if (g_psym)
			  {
			    pp_add_declarator (g_psym, D_ARRAY);
			    g_psym->etype->decl.d.num_ele = (char *) sub;
			  }
			
		DBG_PRINT};}
    break;

  case 215:
#line 2158 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 216:
#line 2163 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 217:
#line 2165 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 218:
#line 2170 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 219:
#line 2172 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 220:
#line 2177 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).ptr) = NULL;
			
		DBG_PRINT};}
    break;

  case 221:
#line 2183 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).ptr) = (((yyGLRStackItem const *)yyvsp)[YYFILL ((3) - (5))].yystate.yysemantics.yysval.ptr);
			
		DBG_PRINT};}
    break;

  case 222:
#line 2189 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).ptr) = (((yyGLRStackItem const *)yyvsp)[YYFILL ((3) - (5))].yystate.yysemantics.yysval.ptr);
			
		DBG_PRINT};}
    break;

  case 223:
#line 2198 "../../src/executables/esql_grammar.y"
    {{
			
			pp_push_name_scope ();
			
		DBG_PRINT};}
    break;

  case 224:
#line 2207 "../../src/executables/esql_grammar.y"
    {{
			
			pp_pop_name_scope ();
			
		DBG_PRINT};}
    break;

  case 225:
#line 2216 "../../src/executables/esql_grammar.y"
    {{
			
			pp_push_spec_scope ();
			
		DBG_PRINT};}
    break;

  case 226:
#line 2225 "../../src/executables/esql_grammar.y"
    {{
			
			pp_pop_spec_scope ();
			
		DBG_PRINT};}
    break;

  case 227:
#line 2234 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).ptr) = (((yyGLRStackItem const *)yyvsp)[YYFILL ((2) - (3))].yystate.yysemantics.yysval.ptr);
			
		DBG_PRINT};}
    break;

  case 228:
#line 2243 "../../src/executables/esql_grammar.y"
    {{
			
			SYMBOL *psym = (((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (2))].yystate.yysemantics.yysval.ptr);
			psym->next = (((yyGLRStackItem const *)yyvsp)[YYFILL ((2) - (2))].yystate.yysemantics.yysval.ptr);
			((*yyvalp).ptr) = psym;
			
		DBG_PRINT};}
    break;

  case 229:
#line 2251 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).ptr) = (((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr);
			
		DBG_PRINT};}
    break;

  case 230:
#line 2260 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).ptr) = (((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr);
			
		DBG_PRINT};}
    break;

  case 231:
#line 2266 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).ptr) = NULL;			// ???
			
		DBG_PRINT};}
    break;

  case 232:
#line 2275 "../../src/executables/esql_grammar.y"
    {{
			
			SYMBOL *psym = (((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (3))].yystate.yysemantics.yysval.ptr);
			SYMBOL *tmp = (((yyGLRStackItem const *)yyvsp)[YYFILL ((3) - (3))].yystate.yysemantics.yysval.ptr);
			g_last->next = tmp;
			g_last = tmp;
			
			((*yyvalp).ptr) = psym;
			
		DBG_PRINT};}
    break;

  case 233:
#line 2286 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).ptr) = g_last = (((yyGLRStackItem const *)yyvsp)[YYFILL ((2) - (2))].yystate.yysemantics.yysval.ptr);
			
		DBG_PRINT};}
    break;

  case 234:
#line 2295 "../../src/executables/esql_grammar.y"
    {{
			
			pp_reset_current_type_spec ();
			
		DBG_PRINT};}
    break;

  case 235:
#line 2303 "../../src/executables/esql_grammar.y"
    {{
			
			SYMBOL *psym = (((yyGLRStackItem const *)yyvsp)[YYFILL ((3) - (4))].yystate.yysemantics.yysval.ptr);
			LINK *plink = (((yyGLRStackItem const *)yyvsp)[YYFILL ((2) - (4))].yystate.yysemantics.yysval.ptr);
			
			if (psym == NULL)
			  psym = pp_new_symbol (NULL, pp_nesting_level);
			
			pp_add_spec_to_decl (plink, psym);
			if (plink && !plink->tdef)
			  pp_discard_link_chain (plink);
			
			((*yyvalp).ptr) = psym;
			
		DBG_PRINT};}
    break;

  case 236:
#line 2322 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).ptr) = NULL;
			
		DBG_PRINT};}
    break;

  case 237:
#line 2328 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).ptr) = (((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr);
			
		DBG_PRINT};}
    break;

  case 238:
#line 2337 "../../src/executables/esql_grammar.y"
    {{
			
			SYMBOL *psym = (((yyGLRStackItem const *)yyvsp)[YYFILL ((2) - (2))].yystate.yysemantics.yysval.ptr);
			
			if (psym == NULL)
			  psym = pp_new_symbol (NULL, pp_nesting_level);
			if (psym)
			  pp_add_declarator (psym, D_POINTER);
			
		DBG_PRINT};}
    break;

  case 239:
#line 2348 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).ptr) = (((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr);
			
		DBG_PRINT};}
    break;

  case 240:
#line 2357 "../../src/executables/esql_grammar.y"
    {{
			
			g_psym = (((yyGLRStackItem const *)yyvsp)[YYFILL ((2) - (3))].yystate.yysemantics.yysval.ptr);
			
		DBG_PRINT};}
    break;

  case 241:
#line 2363 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).ptr) = g_psym;
			
		DBG_PRINT};}
    break;

  case 242:
#line 2369 "../../src/executables/esql_grammar.y"
    {{
			
			SYMBOL *args = (((yyGLRStackItem const *)yyvsp)[YYFILL ((2) - (3))].yystate.yysemantics.yysval.ptr);
			g_psym = pp_new_symbol (NULL, pp_nesting_level);
			pp_add_declarator (g_psym, D_FUNCTION);
			g_psym->etype->decl.d.args = args;
			
		DBG_PRINT};}
    break;

  case 243:
#line 2378 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).ptr) = g_psym;
			
		DBG_PRINT};}
    break;

  case 244:
#line 2384 "../../src/executables/esql_grammar.y"
    {{
			
			g_psym = pp_new_symbol ((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr), pp_nesting_level);
			
		DBG_PRINT};}
    break;

  case 245:
#line 2390 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).ptr) = g_psym;
			
		DBG_PRINT};}
    break;

  case 246:
#line 2396 "../../src/executables/esql_grammar.y"
    {{
			
			const char *sub = (((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr);
			g_psym = pp_new_symbol (NULL, pp_nesting_level);
			pp_add_declarator (g_psym, D_ARRAY);
			g_psym->etype->decl.d.num_ele = (char *) sub;
			
		DBG_PRINT};}
    break;

  case 247:
#line 2405 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).ptr) = g_psym;
			
		DBG_PRINT};}
    break;

  case 250:
#line 2419 "../../src/executables/esql_grammar.y"
    {{
			
			const char *sub = (((yyGLRStackItem const *)yyvsp)[YYFILL ((2) - (2))].yystate.yysemantics.yysval.ptr);
			pp_add_declarator (g_psym, D_ARRAY);
			g_psym->etype->decl.d.num_ele = (char *) sub;
			
		DBG_PRINT};}
    break;

  case 251:
#line 2427 "../../src/executables/esql_grammar.y"
    {{
			
			SYMBOL *args = (((yyGLRStackItem const *)yyvsp)[YYFILL ((3) - (4))].yystate.yysemantics.yysval.ptr);
			pp_add_declarator (g_psym, D_FUNCTION);
			g_psym->etype->decl.d.args = args;
			
		DBG_PRINT};}
    break;

  case 252:
#line 2435 "../../src/executables/esql_grammar.y"
    {{
			
			const char *sub = (((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr);
			pp_add_declarator (g_psym, D_ARRAY);
			g_psym->etype->decl.d.num_ele = (char *) sub;
			
		DBG_PRINT};}
    break;

  case 253:
#line 2443 "../../src/executables/esql_grammar.y"
    {{
			
			SYMBOL *args = (((yyGLRStackItem const *)yyvsp)[YYFILL ((2) - (3))].yystate.yysemantics.yysval.ptr);
			pp_add_declarator (g_psym, D_FUNCTION);
			g_psym->etype->decl.d.args = args;
			
		DBG_PRINT};}
    break;

  case 254:
#line 2454 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).ptr) = NULL;
			
		DBG_PRINT};}
    break;

  case 255:
#line 2460 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).ptr) = (((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr);
			
		DBG_PRINT};}
    break;

  case 256:
#line 2469 "../../src/executables/esql_grammar.y"
    {{
			
			pp_make_typedef_names_visible (0);
			
		DBG_PRINT};}
    break;

  case 257:
#line 2478 "../../src/executables/esql_grammar.y"
    {{
			
			pp_make_typedef_names_visible (1);
			
		DBG_PRINT};}
    break;

  case 258:
#line 2487 "../../src/executables/esql_grammar.y"
    {{
			
			SYMBOL *result = pp_new_symbol ((((yyGLRStackItem const *)yyvsp)[YYFILL ((2) - (3))].yystate.yysemantics.yysval.ptr), pp_nesting_level);
			((*yyvalp).ptr) = result;
			
		DBG_PRINT};}
    break;

  case 259:
#line 2494 "../../src/executables/esql_grammar.y"
    {{
			
			SYMBOL *result;
			if (!g_identifier_symbol || g_identifier_symbol->level != pp_nesting_level)
			  result = pp_new_symbol ((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr), pp_nesting_level);
			else
			  result = g_identifier_symbol;
			
			((*yyvalp).ptr) = result;
			
		DBG_PRINT};}
    break;

  case 260:
#line 2506 "../../src/executables/esql_grammar.y"
    {{
			
			SYMBOL *result = pp_new_symbol ((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr), pp_nesting_level);
			((*yyvalp).ptr) = result;
			
		DBG_PRINT};}
    break;

  case 261:
#line 2516 "../../src/executables/esql_grammar.y"
    {{
			
			varstring *subscript_buf = malloc (sizeof (varstring));
			
			esql_yy_push_mode (EXPR_mode);
			if (IS_PSEUDO_TYPE (pp_current_type_spec ()))
			  {
			    /*
			     * There is no need to store the old values of the
			     * echo function and echo suppression here because
			     * we're going to pop right ought of this mode and
			     * clobber them anyway.
			     */
			    vs_new (subscript_buf);
			    (void) esql_yy_set_buf (subscript_buf);
			    (void) pp_set_echo (&echo_vstr);
			    pp_suppress_echo (false);
			  }
			
			g_subscript = subscript_buf;
			
		DBG_PRINT};}
    break;

  case 262:
#line 2540 "../../src/executables/esql_grammar.y"
    {{
			
			char *subscript = NULL;
			varstring *subscript_buf = g_subscript;
			
			if (IS_PSEUDO_TYPE (pp_current_type_spec ()))
			  {
			    /*
			     * Have to clobber the RB here, since the lookahead
			     * has already put it in our buffer.  Fortunately,
			     * we're in a mode where we can do that.
			     */
			    esql_yy_erase_last_token ();
			    esql_yy_set_buf (NULL);
			    subscript = pp_strdup (vs_str (subscript_buf));
			    vs_free (subscript_buf);
			  }
			
			esql_yy_pop_mode ();
			
			((*yyvalp).ptr) = subscript;
			
		DBG_PRINT};}
    break;

  case 263:
#line 2571 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 264:
#line 2573 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 265:
#line 2578 "../../src/executables/esql_grammar.y"
    {{
			
			esql_yy_pop_mode ();
			
		DBG_PRINT};}
    break;

  case 266:
#line 2587 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 267:
#line 2592 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 268:
#line 2594 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 269:
#line 2599 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 270:
#line 2601 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 271:
#line 2606 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 272:
#line 2608 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 273:
#line 2613 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 274:
#line 2615 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 275:
#line 2617 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 276:
#line 2622 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 277:
#line 2624 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 278:
#line 2626 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 279:
#line 2628 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 280:
#line 2636 "../../src/executables/esql_grammar.y"
    {{
			
			ECHO_STR (", ", strlen (", "));
			
		DBG_PRINT};}
    break;

  case 281:
#line 2642 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 282:
#line 2647 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).ptr) = NULL;
			g_indicator = false;
			
		DBG_PRINT};}
    break;

  case 283:
#line 2654 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).ptr) = (((yyGLRStackItem const *)yyvsp)[YYFILL ((2) - (2))].yystate.yysemantics.yysval.ptr);
			g_indicator = false;
			
		DBG_PRINT};}
    break;

  case 284:
#line 2661 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).ptr) = (((yyGLRStackItem const *)yyvsp)[YYFILL ((3) - (3))].yystate.yysemantics.yysval.ptr);
			g_indicator = false;
			
		DBG_PRINT};}
    break;

  case 285:
#line 2668 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).ptr) = (((yyGLRStackItem const *)yyvsp)[YYFILL ((2) - (2))].yystate.yysemantics.yysval.ptr);
			g_indicator = false;
			
		DBG_PRINT};}
    break;

  case 286:
#line 2679 "../../src/executables/esql_grammar.y"
    {{
			
			HOST_REF *href = NULL;
			g_delay[0] = 0;
			g_varstring = esql_yy_get_buf ();
			
			esql_yy_push_mode (VAR_mode);
			esql_yy_set_buf (NULL);
			structs_allowed = true;
			
		DBG_PRINT};}
    break;

  case 287:
#line 2691 "../../src/executables/esql_grammar.y"
    {{
			
			HOST_REF *href;
			HOST_VAR *hvar1, *hvar2;
			hvar1 = (((yyGLRStackItem const *)yyvsp)[YYFILL ((2) - (3))].yystate.yysemantics.yysval.ptr);
			hvar2 = (((yyGLRStackItem const *)yyvsp)[YYFILL ((3) - (3))].yystate.yysemantics.yysval.ptr);
			
			esql_yy_pop_mode ();
			esql_yy_set_buf (g_varstring);
			
			href = pp_add_host_ref (hvar1, hvar2, structs_allowed, &n_refs);
			ECHO_STR ("? ", strlen ("? "));
			for (i = 1; i < n_refs; i++)
			  ECHO_STR (", ? ", strlen (", ? "));
			
			ECHO_STR (g_delay, strlen (g_delay));
			
			((*yyvalp).ptr) = href;
			
		DBG_PRINT};}
    break;

  case 288:
#line 2715 "../../src/executables/esql_grammar.y"
    {{
			
			g_indicator = true;
			
		DBG_PRINT};}
    break;

  case 289:
#line 2721 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).ptr) = (((yyGLRStackItem const *)yyvsp)[YYFILL ((3) - (3))].yystate.yysemantics.yysval.ptr);
			
		DBG_PRINT};}
    break;

  case 290:
#line 2730 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).ptr) = (((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr);
			
		DBG_PRINT};}
    break;

  case 291:
#line 2736 "../../src/executables/esql_grammar.y"
    {{
			
			HOST_VAR *hvar = NULL;
			HOST_VAR *hvar2 = (((yyGLRStackItem const *)yyvsp)[YYFILL ((3) - (3))].yystate.yysemantics.yysval.ptr);
			if ((hvar = pp_ptr_deref (hvar2, 0)) == NULL)
			  pp_free_host_var (hvar2);
			else
			  vs_prepend (&hvar->expr, "*");
			
			((*yyvalp).ptr) = hvar;
			
		DBG_PRINT};}
    break;

  case 292:
#line 2749 "../../src/executables/esql_grammar.y"
    {{
			
			HOST_VAR *hvar = NULL;
			HOST_VAR *hvar2 = (((yyGLRStackItem const *)yyvsp)[YYFILL ((3) - (3))].yystate.yysemantics.yysval.ptr);
			if ((hvar = pp_ptr_deref (hvar2, 0)) == NULL)
			  pp_free_host_var (hvar2);
			else
			  vs_prepend (&hvar->expr, "*");
			
			((*yyvalp).ptr) = hvar;
			
		DBG_PRINT};}
    break;

  case 293:
#line 2762 "../../src/executables/esql_grammar.y"
    {{
			
			HOST_VAR *hvar = NULL;
			HOST_VAR *hvar2 = (((yyGLRStackItem const *)yyvsp)[YYFILL ((2) - (2))].yystate.yysemantics.yysval.ptr);
			if ((hvar = pp_ptr_deref (hvar2, 0)) == NULL)
			  pp_free_host_var (hvar2);
			else
			  vs_prepend (&hvar->expr, "*");
			
			((*yyvalp).ptr) = hvar;
			
		DBG_PRINT};}
    break;

  case 294:
#line 2775 "../../src/executables/esql_grammar.y"
    {{
			
			HOST_VAR *hvar;
			HOST_VAR *href2 = (((yyGLRStackItem const *)yyvsp)[YYFILL ((2) - (2))].yystate.yysemantics.yysval.ptr);
			
			if ((hvar = pp_addr_of (href2)) == NULL)
			  pp_free_host_var (href2);
			else
			  vs_prepend (&hvar->expr, "&");
			
			((*yyvalp).ptr) = hvar;
			
		DBG_PRINT};}
    break;

  case 295:
#line 2792 "../../src/executables/esql_grammar.y"
    {{
			
			HOST_VAR *href = NULL;
			SYMBOL *sym = pp_findsym (pp_Symbol_table, (((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr));
			if (sym == NULL)
			  {
			    esql_yyverror (pp_get_msg (EX_ESQLM_SET, MSG_NOT_DECLARED), (((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr));
			    href = NULL;
			  }
			else
			  href = pp_new_host_var (NULL, sym);
			
			g_href = href;
			
		DBG_PRINT};}
    break;

  case 296:
#line 2808 "../../src/executables/esql_grammar.y"
    {{
			
			HOST_VAR *href = (((yyGLRStackItem const *)yyvsp)[YYFILL ((3) - (3))].yystate.yysemantics.yysval.ptr);
			if (href == NULL)
			  href = g_href;
			((*yyvalp).ptr) = href;
			
		DBG_PRINT};}
    break;

  case 297:
#line 2817 "../../src/executables/esql_grammar.y"
    {{
			
			HOST_VAR *href = (((yyGLRStackItem const *)yyvsp)[YYFILL ((2) - (3))].yystate.yysemantics.yysval.ptr);
			if (href)
			  {
			    vs_prepend (&href->expr, "(");
			    vs_strcat (&href->expr, ")");
			  }
			g_href = href;
			
		DBG_PRINT};}
    break;

  case 298:
#line 2829 "../../src/executables/esql_grammar.y"
    {{
			
			HOST_VAR *href = (((yyGLRStackItem const *)yyvsp)[YYFILL ((5) - (5))].yystate.yysemantics.yysval.ptr);
			if (href == NULL)
			  href = g_href;
			((*yyvalp).ptr) = href;
			
		DBG_PRINT};}
    break;

  case 299:
#line 2842 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).ptr) = NULL;
			
		DBG_PRINT};}
    break;

  case 300:
#line 2848 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).ptr) = (((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr);
			
		DBG_PRINT};}
    break;

  case 301:
#line 2857 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).ptr) = (((yyGLRStackItem const *)yyvsp)[YYFILL ((2) - (2))].yystate.yysemantics.yysval.ptr);
			
		DBG_PRINT};}
    break;

  case 302:
#line 2863 "../../src/executables/esql_grammar.y"
    {{
			
			HOST_VAR *href;
			if ((href = pp_struct_deref (g_href, (((yyGLRStackItem const *)yyvsp)[YYFILL ((3) - (3))].yystate.yysemantics.yysval.ptr), 0)) == NULL)
			  pp_free_host_var (g_href);
			else
			  vs_sprintf (&href->expr, ".%s", (((yyGLRStackItem const *)yyvsp)[YYFILL ((3) - (3))].yystate.yysemantics.yysval.ptr));
			
			((*yyvalp).ptr) = href;
			
		DBG_PRINT};}
    break;

  case 303:
#line 2875 "../../src/executables/esql_grammar.y"
    {{
			
			HOST_VAR *href;
			if ((href = pp_struct_deref (g_href, (((yyGLRStackItem const *)yyvsp)[YYFILL ((3) - (3))].yystate.yysemantics.yysval.ptr), 1)) == NULL)
			  pp_free_host_var (g_href);
			else
			  vs_sprintf (&href->expr, "->%s", (((yyGLRStackItem const *)yyvsp)[YYFILL ((3) - (3))].yystate.yysemantics.yysval.ptr));
			
			((*yyvalp).ptr) = href;
			
		DBG_PRINT};}
    break;

  case 304:
#line 2887 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).ptr) = (((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr);
			
		DBG_PRINT};}
    break;

  case 305:
#line 2893 "../../src/executables/esql_grammar.y"
    {{
			
			HOST_VAR *href;
			if ((href = pp_struct_deref (g_href, (((yyGLRStackItem const *)yyvsp)[YYFILL ((2) - (2))].yystate.yysemantics.yysval.ptr), 0)) == NULL)
			  pp_free_host_var (g_href);
			else
			  vs_sprintf (&href->expr, ".%s", (((yyGLRStackItem const *)yyvsp)[YYFILL ((2) - (2))].yystate.yysemantics.yysval.ptr));
			
			((*yyvalp).ptr) = href;
			
		DBG_PRINT};}
    break;

  case 306:
#line 2905 "../../src/executables/esql_grammar.y"
    {{
			
			HOST_VAR *href;
			if ((href = pp_struct_deref (g_href, (((yyGLRStackItem const *)yyvsp)[YYFILL ((2) - (2))].yystate.yysemantics.yysval.ptr), 1)) == NULL)
			  pp_free_host_var (g_href);
			else
			  vs_sprintf (&href->expr, "->%s", (((yyGLRStackItem const *)yyvsp)[YYFILL ((2) - (2))].yystate.yysemantics.yysval.ptr));
			
			((*yyvalp).ptr) = href;
			
		DBG_PRINT};}
    break;

  case 307:
#line 2920 "../../src/executables/esql_grammar.y"
    {{
			
			vs_clear (&pp_subscript_buf);
			esql_yy_push_mode (HV_mode);
			
		DBG_PRINT};}
    break;

  case 308:
#line 2927 "../../src/executables/esql_grammar.y"
    {{
			
			((*yyvalp).ptr) = (((yyGLRStackItem const *)yyvsp)[YYFILL ((3) - (3))].yystate.yysemantics.yysval.ptr);
			
		DBG_PRINT};}
    break;

  case 309:
#line 2937 "../../src/executables/esql_grammar.y"
    {{
			
			HOST_VAR *href;
			
			esql_yy_pop_mode ();
			if ((href = pp_ptr_deref (g_href, 1)) == NULL)
			  pp_free_host_var (g_href);
			else
			  vs_sprintf (&href->expr, "[%s%s", vs_str (&pp_subscript_buf), "]");
			
			((*yyvalp).ptr) = href;
			
		DBG_PRINT};}
    break;

  case 310:
#line 2955 "../../src/executables/esql_grammar.y"
    {{
			
			vs_strcat (&pp_subscript_buf, "(");
			
		DBG_PRINT};}
    break;

  case 311:
#line 2962 "../../src/executables/esql_grammar.y"
    {{
			
			vs_strcat (&pp_subscript_buf, ")");
			
		DBG_PRINT};}
    break;

  case 312:
#line 2968 "../../src/executables/esql_grammar.y"
    {{
			
			vs_strcat (&pp_subscript_buf, "[");
			
		DBG_PRINT};}
    break;

  case 313:
#line 2975 "../../src/executables/esql_grammar.y"
    {{
			
			vs_strcat (&pp_subscript_buf, "]");
			
		DBG_PRINT};}
    break;

  case 314:
#line 2981 "../../src/executables/esql_grammar.y"
    {{
			
			vs_strcat (&pp_subscript_buf, "{");
			
		DBG_PRINT};}
    break;

  case 315:
#line 2988 "../../src/executables/esql_grammar.y"
    {{
			
			vs_strcat (&pp_subscript_buf, "}");
			
		DBG_PRINT};}
    break;

  case 316:
#line 2997 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 317:
#line 2999 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 318:
#line 3004 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 319:
#line 3006 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 320:
#line 3016 "../../src/executables/esql_grammar.y"
    {{
			
			PT_NODE **pt, *cursr;
			char *statement, *text;
			PT_HOST_VARS *result;
			long is_upd_obj;
			HOST_LOD *inputs, *outputs;
			int length;
			PARSER_VARCHAR *varPtr;
			esql_yy_pop_mode ();
			statement = vs_str (&pt_statement_buf);
			length = (int) vs_strlen (&pt_statement_buf);
			
			PRINT_1 ("statement: %s\n", statement);
			pt = parser_parse_string (parser, statement);
			
			if (!pt || !pt[0])
			  {
			    pt_print_errors (parser);
			  }
			else
			  {
			
			    is_upd_obj = pt_is_update_object (pt[0]);
			    result = pt_host_info (parser, pt[0]);
			    inputs = pp_input_refs ();
			    outputs = pp_output_refs ();
			    text = statement;
			    if (is_upd_obj)
			      {
				if (HOST_N_REFS (inputs) > 0
				    && pp_check_type (&(HOST_REFS (inputs))[0],
						      NEWSET (C_TYPE_OBJECTID),
						      pp_get_msg (EX_ESQLM_SET,
								  MSG_PTR_TO_DB_OBJECT)))
				  esql_Translate_table.tr_object_update (text,
									 length,
									 repeat_option,
									 HOST_N_REFS (inputs),
									 HOST_REFS (inputs));
			      }
			    else if ((cursr = pt_get_cursor (result)) != NULL)
			      {
				CURSOR *cur;
				const char *c_nam;
				c_nam = pt_get_name (cursr);
				if ((cur = pp_lookup_cursor ((char *) c_nam)) == NULL)
				  {
				    esql_yyverror (pp_get_msg (EX_ESQLM_SET, MSG_CURSOR_UNDEFINED),
						   c_nam);
				  }
				else
				  {
				    switch (g_cursor_type)
				      {
				      case CURSOR_DELETE:
					esql_Translate_table.tr_delete_cs (cur->cid);
					break;
				      case CURSOR_UPDATE:
					pt_set_update_object (parser, pt[0]);
					varPtr = pt_print_bytes (parser, pt[0]);
					esql_Translate_table.tr_update_cs (cur->cid,
									   (char *)
									   pt_get_varchar_bytes
									   (varPtr),
									   (int)
									   pt_get_varchar_length
									   (varPtr), repeat_option,
									   HOST_N_REFS (inputs),
									   HOST_REFS (inputs));
					break;
				      default:
					break;
				      }
				  }
			      }
			    else
			      {
				repeat_option = (pt[0]->node_type == PT_INSERT && !pt[0]->info.insert.do_replace && pt[0]->info.insert.odku_assignments == NULL
				   && pt[0]->info.insert.value_clauses->info.node_list.list_type != PT_IS_SUBQUERY
				   && pt[0]->info.insert.value_clauses->info.node_list.list->next == NULL);
				esql_Translate_table.tr_static (text,
								length,
								repeat_option,
								HOST_N_REFS (inputs),
								HOST_REFS (inputs),
								(const char *) HOST_DESC (inputs),
								HOST_N_REFS (outputs),
								HOST_REFS (outputs),
								(const char *) HOST_DESC (outputs));
			      }
			    pt_free_host_info (result);
			  }
			
			esql_yy_sync_lineno ();
			esql_yy_set_buf (NULL);
			
		DBG_PRINT};}
    break;

  case 321:
#line 3118 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 322:
#line 3120 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 323:
#line 3125 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 324:
#line 3127 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 325:
#line 3132 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 326:
#line 3134 "../../src/executables/esql_grammar.y"
    {{
			
			HOST_REF *href = (((yyGLRStackItem const *)yyvsp)[YYFILL ((2) - (2))].yystate.yysemantics.yysval.ptr);
			if (pp_check_type (href,
					   NEWSET (C_TYPE_SQLDA),
					   pp_get_msg (EX_ESQLM_SET, MSG_PTR_TO_DESCR)))
			  pp_switch_to_descriptor ();
			
		DBG_PRINT};}
    break;

  case 327:
#line 3144 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 328:
#line 3149 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 329:
#line 3151 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 330:
#line 3153 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 331:
#line 3155 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 332:
#line 3157 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 333:
#line 3159 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 334:
#line 3161 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 335:
#line 3163 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 336:
#line 3165 "../../src/executables/esql_grammar.y"
    {{
			
			pp_gather_output_refs ();
			
		DBG_PRINT};}
    break;

  case 337:
#line 3171 "../../src/executables/esql_grammar.y"
    {{
			
			pp_gather_output_refs ();
			
		DBG_PRINT};}
    break;

  case 338:
#line 3177 "../../src/executables/esql_grammar.y"
    {{
			
			pp_gather_input_refs ();
			
		DBG_PRINT};}
    break;

  case 339:
#line 3183 "../../src/executables/esql_grammar.y"
    {{
			
			pp_gather_input_refs ();
			
		DBG_PRINT};}
    break;

  case 340:
#line 3189 "../../src/executables/esql_grammar.y"
    {{
			
			pp_gather_input_refs ();
			
		DBG_PRINT};}
    break;

  case 341:
#line 3195 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 342:
#line 3197 "../../src/executables/esql_grammar.y"
    {DBG_PRINT;}
    break;

  case 343:
#line 3202 "../../src/executables/esql_grammar.y"
    {{
			
			vs_clear (&pt_statement_buf);
			esql_yy_set_buf (&pt_statement_buf);
			esql_yy_push_mode (BUFFER_mode);
			
		DBG_PRINT};}
    break;

  case 344:
#line 3210 "../../src/executables/esql_grammar.y"
    {{
			
			char *statement;
			char *text = NULL;
			long length;
			PT_NODE **pt;
			char **cp = malloc (sizeof (char *) * 2);
			
			esql_yy_pop_mode ();
			statement = vs_str (&pt_statement_buf);
			length = vs_strlen (&pt_statement_buf);
			
			PRINT_1 ("statement: %s\n", statement);
			pt = parser_parse_string (parser, statement);
			if (!pt || !pt[0])
			  {
			    pt_print_errors (parser);
			  }
			else
			  text = statement;
			
			cp[0] = text;
			cp[1] = (char *) length;
			
			esql_yy_sync_lineno ();
			esql_yy_set_buf (NULL);
			
			((*yyvalp).ptr) = cp;
			
		DBG_PRINT};}
    break;

  case 345:
#line 3245 "../../src/executables/esql_grammar.y"
    {{
			
			ECHO_STR ((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr), strlen ((((yyGLRStackItem const *)yyvsp)[YYFILL ((1) - (1))].yystate.yysemantics.yysval.ptr)));
			
		DBG_PRINT};}
    break;

  case 346:
#line 3251 "../../src/executables/esql_grammar.y"
    {{
			
			ECHO_STR ("(", strlen ("("));
			
		DBG_PRINT};}
    break;


/* Line 930 of glr.c.  */
#line 4883 "../../src/executables/esql_grammar.c"
      default: break;
    }

  return yyok;
# undef yyerrok
# undef YYABORT
# undef YYACCEPT
# undef YYERROR
# undef YYBACKUP
# undef yyclearin
# undef YYRECOVERING
}


/*ARGSUSED*/ static void
yyuserMerge (int yyn, YYSTYPE* yy0, YYSTYPE* yy1)
{
  YYUSE (yy0);
  YYUSE (yy1);

  switch (yyn)
    {
      
      default: break;
    }
}

			      /* Bison grammar-table manipulation.  */

/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

/*ARGSUSED*/
static void
yydestruct (const char *yymsg, int yytype, YYSTYPE *yyvaluep)
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

/** Number of symbols composing the right hand side of rule #RULE.  */
static inline int
yyrhsLength (yyRuleNum yyrule)
{
  return yyr2[yyrule];
}

static void
yydestroyGLRState (char const *yymsg, yyGLRState *yys)
{
  if (yys->yyresolved)
    yydestruct (yymsg, yystos[yys->yylrState],
		&yys->yysemantics.yysval);
  else
    {
#if YYDEBUG
      if (yydebug)
	{
	  if (yys->yysemantics.yyfirstVal)
	    YYFPRINTF (stderr, "%s unresolved ", yymsg);
	  else
	    YYFPRINTF (stderr, "%s incomplete ", yymsg);
	  yy_symbol_print (stderr, yystos[yys->yylrState],
			   NULL);
	  YYFPRINTF (stderr, "\n");
	}
#endif

      if (yys->yysemantics.yyfirstVal)
	{
	  yySemanticOption *yyoption = yys->yysemantics.yyfirstVal;
	  yyGLRState *yyrh;
	  int yyn;
	  for (yyrh = yyoption->yystate, yyn = yyrhsLength (yyoption->yyrule);
	       yyn > 0;
	       yyrh = yyrh->yypred, yyn -= 1)
	    yydestroyGLRState (yymsg, yyrh);
	}
    }
}

/** Left-hand-side symbol for rule #RULE.  */
static inline yySymbol
yylhsNonterm (yyRuleNum yyrule)
{
  return yyr1[yyrule];
}

#define yyis_pact_ninf(yystate) \
  ((yystate) == YYPACT_NINF)

/** True iff LR state STATE has only a default reduction (regardless
 *  of token).  */
static inline yybool
yyisDefaultedState (yyStateNum yystate)
{
  return yyis_pact_ninf (yypact[yystate]);
}

/** The default reduction for STATE, assuming it has one.  */
static inline yyRuleNum
yydefaultAction (yyStateNum yystate)
{
  return yydefact[yystate];
}

#define yyis_table_ninf(yytable_value) \
  YYID (0)

/** Set *YYACTION to the action to take in YYSTATE on seeing YYTOKEN.
 *  Result R means
 *    R < 0:  Reduce on rule -R.
 *    R = 0:  Error.
 *    R > 0:  Shift to state R.
 *  Set *CONFLICTS to a pointer into yyconfl to 0-terminated list of
 *  conflicting reductions.
 */
static inline void
yygetLRActions (yyStateNum yystate, int yytoken,
		int* yyaction, const short int** yyconflicts)
{
  int yyindex = yypact[yystate] + yytoken;
  if (yyindex < 0 || YYLAST < yyindex || yycheck[yyindex] != yytoken)
    {
      *yyaction = -yydefact[yystate];
      *yyconflicts = yyconfl;
    }
  else if (! yyis_table_ninf (yytable[yyindex]))
    {
      *yyaction = yytable[yyindex];
      *yyconflicts = yyconfl + yyconflp[yyindex];
    }
  else
    {
      *yyaction = 0;
      *yyconflicts = yyconfl + yyconflp[yyindex];
    }
}

static inline yyStateNum
yyLRgotoState (yyStateNum yystate, yySymbol yylhs)
{
  int yyr;
  yyr = yypgoto[yylhs - YYNTOKENS] + yystate;
  if (0 <= yyr && yyr <= YYLAST && yycheck[yyr] == yystate)
    return yytable[yyr];
  else
    return yydefgoto[yylhs - YYNTOKENS];
}

static inline yybool
yyisShiftAction (int yyaction)
{
  return 0 < yyaction;
}

static inline yybool
yyisErrorAction (int yyaction)
{
  return yyaction == 0;
}

				/* GLRStates */

/** Return a fresh GLRStackItem.  Callers should call
 * YY_RESERVE_GLRSTACK afterwards to make sure there is sufficient
 * headroom.  */

static inline yyGLRStackItem*
yynewGLRStackItem (yyGLRStack* yystackp, yybool yyisState)
{
  yyGLRStackItem* yynewItem = yystackp->yynextFree;
  yystackp->yyspaceLeft -= 1;
  yystackp->yynextFree += 1;
  yynewItem->yystate.yyisState = yyisState;
  return yynewItem;
}

/** Add a new semantic action that will execute the action for rule
 *  RULENUM on the semantic values in RHS to the list of
 *  alternative actions for STATE.  Assumes that RHS comes from
 *  stack #K of *STACKP. */
static void
yyaddDeferredAction (yyGLRStack* yystackp, size_t yyk, yyGLRState* yystate,
		     yyGLRState* rhs, yyRuleNum yyrule)
{
  yySemanticOption* yynewOption =
    &yynewGLRStackItem (yystackp, yyfalse)->yyoption;
  yynewOption->yystate = rhs;
  yynewOption->yyrule = yyrule;
  if (yystackp->yytops.yylookaheadNeeds[yyk])
    {
      yynewOption->yyrawchar = yychar;
      yynewOption->yyval = yylval;
      yynewOption->yyloc = yylloc;
    }
  else
    yynewOption->yyrawchar = YYEMPTY;
  yynewOption->yynext = yystate->yysemantics.yyfirstVal;
  yystate->yysemantics.yyfirstVal = yynewOption;

  YY_RESERVE_GLRSTACK (yystackp);
}

				/* GLRStacks */

/** Initialize SET to a singleton set containing an empty stack.  */
static yybool
yyinitStateSet (yyGLRStateSet* yyset)
{
  yyset->yysize = 1;
  yyset->yycapacity = 16;
  yyset->yystates = (yyGLRState**) YYMALLOC (16 * sizeof yyset->yystates[0]);
  if (! yyset->yystates)
    return yyfalse;
  yyset->yystates[0] = NULL;
  yyset->yylookaheadNeeds =
    (yybool*) YYMALLOC (16 * sizeof yyset->yylookaheadNeeds[0]);
  if (! yyset->yylookaheadNeeds)
    {
      YYFREE (yyset->yystates);
      return yyfalse;
    }
  return yytrue;
}

static void yyfreeStateSet (yyGLRStateSet* yyset)
{
  YYFREE (yyset->yystates);
  YYFREE (yyset->yylookaheadNeeds);
}

/** Initialize STACK to a single empty stack, with total maximum
 *  capacity for all stacks of SIZE.  */
static yybool
yyinitGLRStack (yyGLRStack* yystackp, size_t yysize)
{
  yystackp->yyerrState = 0;
  yynerrs = 0;
  yystackp->yyspaceLeft = yysize;
  yystackp->yyitems =
    (yyGLRStackItem*) YYMALLOC (yysize * sizeof yystackp->yynextFree[0]);
  if (!yystackp->yyitems)
    return yyfalse;
  yystackp->yynextFree = yystackp->yyitems;
  yystackp->yysplitPoint = NULL;
  yystackp->yylastDeleted = NULL;
  return yyinitStateSet (&yystackp->yytops);
}


#if YYSTACKEXPANDABLE
# define YYRELOC(YYFROMITEMS,YYTOITEMS,YYX,YYTYPE) \
  &((YYTOITEMS) - ((YYFROMITEMS) - (yyGLRStackItem*) (YYX)))->YYTYPE

/** If STACK is expandable, extend it.  WARNING: Pointers into the
    stack from outside should be considered invalid after this call.
    We always expand when there are 1 or fewer items left AFTER an
    allocation, so that we can avoid having external pointers exist
    across an allocation.  */
static void
yyexpandGLRStack (yyGLRStack* yystackp)
{
  yyGLRStackItem* yynewItems;
  yyGLRStackItem* yyp0, *yyp1;
  size_t yysize, yynewSize;
  size_t yyn;
  yysize = yystackp->yynextFree - yystackp->yyitems;
  if (YYMAXDEPTH - YYHEADROOM < yysize)
    yyMemoryExhausted (yystackp);
  yynewSize = 2*yysize;
  if (YYMAXDEPTH < yynewSize)
    yynewSize = YYMAXDEPTH;
  yynewItems = (yyGLRStackItem*) YYMALLOC (yynewSize * sizeof yynewItems[0]);
  if (! yynewItems)
    yyMemoryExhausted (yystackp);
  for (yyp0 = yystackp->yyitems, yyp1 = yynewItems, yyn = yysize;
       0 < yyn;
       yyn -= 1, yyp0 += 1, yyp1 += 1)
    {
      *yyp1 = *yyp0;
      if (*(yybool *) yyp0)
	{
	  yyGLRState* yys0 = &yyp0->yystate;
	  yyGLRState* yys1 = &yyp1->yystate;
	  if (yys0->yypred != NULL)
	    yys1->yypred =
	      YYRELOC (yyp0, yyp1, yys0->yypred, yystate);
	  if (! yys0->yyresolved && yys0->yysemantics.yyfirstVal != NULL)
	    yys1->yysemantics.yyfirstVal =
	      YYRELOC(yyp0, yyp1, yys0->yysemantics.yyfirstVal, yyoption);
	}
      else
	{
	  yySemanticOption* yyv0 = &yyp0->yyoption;
	  yySemanticOption* yyv1 = &yyp1->yyoption;
	  if (yyv0->yystate != NULL)
	    yyv1->yystate = YYRELOC (yyp0, yyp1, yyv0->yystate, yystate);
	  if (yyv0->yynext != NULL)
	    yyv1->yynext = YYRELOC (yyp0, yyp1, yyv0->yynext, yyoption);
	}
    }
  if (yystackp->yysplitPoint != NULL)
    yystackp->yysplitPoint = YYRELOC (yystackp->yyitems, yynewItems,
				 yystackp->yysplitPoint, yystate);

  for (yyn = 0; yyn < yystackp->yytops.yysize; yyn += 1)
    if (yystackp->yytops.yystates[yyn] != NULL)
      yystackp->yytops.yystates[yyn] =
	YYRELOC (yystackp->yyitems, yynewItems,
		 yystackp->yytops.yystates[yyn], yystate);
  YYFREE (yystackp->yyitems);
  yystackp->yyitems = yynewItems;
  yystackp->yynextFree = yynewItems + yysize;
  yystackp->yyspaceLeft = yynewSize - yysize;
}
#endif

static void
yyfreeGLRStack (yyGLRStack* yystackp)
{
  YYFREE (yystackp->yyitems);
  yyfreeStateSet (&yystackp->yytops);
}

/** Assuming that S is a GLRState somewhere on STACK, update the
 *  splitpoint of STACK, if needed, so that it is at least as deep as
 *  S.  */
static inline void
yyupdateSplit (yyGLRStack* yystackp, yyGLRState* yys)
{
  if (yystackp->yysplitPoint != NULL && yystackp->yysplitPoint > yys)
    yystackp->yysplitPoint = yys;
}

/** Invalidate stack #K in STACK.  */
static inline void
yymarkStackDeleted (yyGLRStack* yystackp, size_t yyk)
{
  if (yystackp->yytops.yystates[yyk] != NULL)
    yystackp->yylastDeleted = yystackp->yytops.yystates[yyk];
  yystackp->yytops.yystates[yyk] = NULL;
}

/** Undelete the last stack that was marked as deleted.  Can only be
    done once after a deletion, and only when all other stacks have
    been deleted.  */
static void
yyundeleteLastStack (yyGLRStack* yystackp)
{
  if (yystackp->yylastDeleted == NULL || yystackp->yytops.yysize != 0)
    return;
  yystackp->yytops.yystates[0] = yystackp->yylastDeleted;
  yystackp->yytops.yysize = 1;
  YYDPRINTF ((stderr, "Restoring last deleted stack as stack #0.\n"));
  yystackp->yylastDeleted = NULL;
}

static inline void
yyremoveDeletes (yyGLRStack* yystackp)
{
  size_t yyi, yyj;
  yyi = yyj = 0;
  while (yyj < yystackp->yytops.yysize)
    {
      if (yystackp->yytops.yystates[yyi] == NULL)
	{
	  if (yyi == yyj)
	    {
	      YYDPRINTF ((stderr, "Removing dead stacks.\n"));
	    }
	  yystackp->yytops.yysize -= 1;
	}
      else
	{
	  yystackp->yytops.yystates[yyj] = yystackp->yytops.yystates[yyi];
	  /* In the current implementation, it's unnecessary to copy
	     yystackp->yytops.yylookaheadNeeds[yyi] since, after
	     yyremoveDeletes returns, the parser immediately either enters
	     deterministic operation or shifts a token.  However, it doesn't
	     hurt, and the code might evolve to need it.  */
	  yystackp->yytops.yylookaheadNeeds[yyj] =
	    yystackp->yytops.yylookaheadNeeds[yyi];
	  if (yyj != yyi)
	    {
	      YYDPRINTF ((stderr, "Rename stack %lu -> %lu.\n",
			  (unsigned long int) yyi, (unsigned long int) yyj));
	    }
	  yyj += 1;
	}
      yyi += 1;
    }
}

/** Shift to a new state on stack #K of STACK, corresponding to LR state
 * LRSTATE, at input position POSN, with (resolved) semantic value SVAL.  */
static inline void
yyglrShift (yyGLRStack* yystackp, size_t yyk, yyStateNum yylrState,
	    size_t yyposn,
	    YYSTYPE* yyvalp, YYLTYPE* yylocp)
{
  yyGLRState* yynewState = &yynewGLRStackItem (yystackp, yytrue)->yystate;

  yynewState->yylrState = yylrState;
  yynewState->yyposn = yyposn;
  yynewState->yyresolved = yytrue;
  yynewState->yypred = yystackp->yytops.yystates[yyk];
  yynewState->yysemantics.yysval = *yyvalp;
  yynewState->yyloc = *yylocp;
  yystackp->yytops.yystates[yyk] = yynewState;

  YY_RESERVE_GLRSTACK (yystackp);
}

/** Shift stack #K of YYSTACK, to a new state corresponding to LR
 *  state YYLRSTATE, at input position YYPOSN, with the (unresolved)
 *  semantic value of YYRHS under the action for YYRULE.  */
static inline void
yyglrShiftDefer (yyGLRStack* yystackp, size_t yyk, yyStateNum yylrState,
		 size_t yyposn, yyGLRState* rhs, yyRuleNum yyrule)
{
  yyGLRState* yynewState = &yynewGLRStackItem (yystackp, yytrue)->yystate;

  yynewState->yylrState = yylrState;
  yynewState->yyposn = yyposn;
  yynewState->yyresolved = yyfalse;
  yynewState->yypred = yystackp->yytops.yystates[yyk];
  yynewState->yysemantics.yyfirstVal = NULL;
  yystackp->yytops.yystates[yyk] = yynewState;

  /* Invokes YY_RESERVE_GLRSTACK.  */
  yyaddDeferredAction (yystackp, yyk, yynewState, rhs, yyrule);
}

/** Pop the symbols consumed by reduction #RULE from the top of stack
 *  #K of STACK, and perform the appropriate semantic action on their
 *  semantic values.  Assumes that all ambiguities in semantic values
 *  have been previously resolved.  Set *VALP to the resulting value,
 *  and *LOCP to the computed location (if any).  Return value is as
 *  for userAction.  */
static inline YYRESULTTAG
yydoAction (yyGLRStack* yystackp, size_t yyk, yyRuleNum yyrule,
	    YYSTYPE* yyvalp, YYLTYPE* yylocp)
{
  int yynrhs = yyrhsLength (yyrule);

  if (yystackp->yysplitPoint == NULL)
    {
      /* Standard special case: single stack.  */
      yyGLRStackItem* rhs = (yyGLRStackItem*) yystackp->yytops.yystates[yyk];
      YYASSERT (yyk == 0);
      yystackp->yynextFree -= yynrhs;
      yystackp->yyspaceLeft += yynrhs;
      yystackp->yytops.yystates[0] = & yystackp->yynextFree[-1].yystate;
      return yyuserAction (yyrule, yynrhs, rhs,
			   yyvalp, yylocp, yystackp);
    }
  else
    {
      /* At present, doAction is never called in nondeterministic
       * mode, so this branch is never taken.  It is here in
       * anticipation of a future feature that will allow immediate
       * evaluation of selected actions in nondeterministic mode.  */
      int yyi;
      yyGLRState* yys;
      yyGLRStackItem yyrhsVals[YYMAXRHS + YYMAXLEFT + 1];
      yys = yyrhsVals[YYMAXRHS + YYMAXLEFT].yystate.yypred
	= yystackp->yytops.yystates[yyk];
      for (yyi = 0; yyi < yynrhs; yyi += 1)
	{
	  yys = yys->yypred;
	  YYASSERT (yys);
	}
      yyupdateSplit (yystackp, yys);
      yystackp->yytops.yystates[yyk] = yys;
      return yyuserAction (yyrule, yynrhs, yyrhsVals + YYMAXRHS + YYMAXLEFT - 1,
			   yyvalp, yylocp, yystackp);
    }
}

#if !YYDEBUG
# define YY_REDUCE_PRINT(Args)
#else
# define YY_REDUCE_PRINT(Args)		\
do {					\
  if (yydebug)				\
    yy_reduce_print Args;		\
} while (YYID (0))

/*----------------------------------------------------------.
| Report that the RULE is going to be reduced on stack #K.  |
`----------------------------------------------------------*/

/*ARGSUSED*/ static inline void
yy_reduce_print (yyGLRStack* yystackp, size_t yyk, yyRuleNum yyrule,
		 YYSTYPE* yyvalp, YYLTYPE* yylocp)
{
  int yynrhs = yyrhsLength (yyrule);
  yybool yynormal __attribute__ ((__unused__)) =
    (yystackp->yysplitPoint == NULL);
  yyGLRStackItem* yyvsp = (yyGLRStackItem*) yystackp->yytops.yystates[yyk];
  int yylow = 1;
  int yyi;
  YYUSE (yyvalp);
  YYUSE (yylocp);
  YYFPRINTF (stderr, "Reducing stack %lu by rule %d (line %lu):\n",
	     (unsigned long int) yyk, yyrule - 1,
	     (unsigned long int) yyrline[yyrule]);
  /* The symbols being reduced.  */
  for (yyi = 0; yyi < yynrhs; yyi++)
    {
      fprintf (stderr, "   $%d = ", yyi + 1);
      yy_symbol_print (stderr, yyrhs[yyprhs[yyrule] + yyi],
		       &(((yyGLRStackItem const *)yyvsp)[YYFILL ((yyi + 1) - (yynrhs))].yystate.yysemantics.yysval)
		       		       );
      fprintf (stderr, "\n");
    }
}
#endif

/** Pop items off stack #K of STACK according to grammar rule RULE,
 *  and push back on the resulting nonterminal symbol.  Perform the
 *  semantic action associated with RULE and store its value with the
 *  newly pushed state, if FORCEEVAL or if STACK is currently
 *  unambiguous.  Otherwise, store the deferred semantic action with
 *  the new state.  If the new state would have an identical input
 *  position, LR state, and predecessor to an existing state on the stack,
 *  it is identified with that existing state, eliminating stack #K from
 *  the STACK.  In this case, the (necessarily deferred) semantic value is
 *  added to the options for the existing state's semantic value.
 */
static inline YYRESULTTAG
yyglrReduce (yyGLRStack* yystackp, size_t yyk, yyRuleNum yyrule,
	     yybool yyforceEval)
{
  size_t yyposn = yystackp->yytops.yystates[yyk]->yyposn;

  if (yyforceEval || yystackp->yysplitPoint == NULL)
    {
      YYSTYPE yysval;
      YYLTYPE yyloc;

      YY_REDUCE_PRINT ((yystackp, yyk, yyrule, &yysval, &yyloc));
      YYCHK (yydoAction (yystackp, yyk, yyrule, &yysval,
			 &yyloc));
      YY_SYMBOL_PRINT ("-> $$ =", yyr1[yyrule], &yysval, &yyloc);
      yyglrShift (yystackp, yyk,
		  yyLRgotoState (yystackp->yytops.yystates[yyk]->yylrState,
				 yylhsNonterm (yyrule)),
		  yyposn, &yysval, &yyloc);
    }
  else
    {
      size_t yyi;
      int yyn;
      yyGLRState* yys, *yys0 = yystackp->yytops.yystates[yyk];
      yyStateNum yynewLRState;

      for (yys = yystackp->yytops.yystates[yyk], yyn = yyrhsLength (yyrule);
	   0 < yyn; yyn -= 1)
	{
	  yys = yys->yypred;
	  YYASSERT (yys);
	}
      yyupdateSplit (yystackp, yys);
      yynewLRState = yyLRgotoState (yys->yylrState, yylhsNonterm (yyrule));
      YYDPRINTF ((stderr,
		  "Reduced stack %lu by rule #%d; action deferred.  Now in state %d.\n",
		  (unsigned long int) yyk, yyrule - 1, yynewLRState));
      for (yyi = 0; yyi < yystackp->yytops.yysize; yyi += 1)
	if (yyi != yyk && yystackp->yytops.yystates[yyi] != NULL)
	  {
	    yyGLRState* yyp, *yysplit = yystackp->yysplitPoint;
	    yyp = yystackp->yytops.yystates[yyi];
	    while (yyp != yys && yyp != yysplit && yyp->yyposn >= yyposn)
	      {
		if (yyp->yylrState == yynewLRState && yyp->yypred == yys)
		  {
		    yyaddDeferredAction (yystackp, yyk, yyp, yys0, yyrule);
		    yymarkStackDeleted (yystackp, yyk);
		    YYDPRINTF ((stderr, "Merging stack %lu into stack %lu.\n",
				(unsigned long int) yyk,
				(unsigned long int) yyi));
		    return yyok;
		  }
		yyp = yyp->yypred;
	      }
	  }
      yystackp->yytops.yystates[yyk] = yys;
      yyglrShiftDefer (yystackp, yyk, yynewLRState, yyposn, yys0, yyrule);
    }
  return yyok;
}

static size_t
yysplitStack (yyGLRStack* yystackp, size_t yyk)
{
  if (yystackp->yysplitPoint == NULL)
    {
      YYASSERT (yyk == 0);
      yystackp->yysplitPoint = yystackp->yytops.yystates[yyk];
    }
  if (yystackp->yytops.yysize >= yystackp->yytops.yycapacity)
    {
      yyGLRState** yynewStates;
      yybool* yynewLookaheadNeeds;

      yynewStates = NULL;

      if (yystackp->yytops.yycapacity
	  > (YYSIZEMAX / (2 * sizeof yynewStates[0])))
	yyMemoryExhausted (yystackp);
      yystackp->yytops.yycapacity *= 2;

      yynewStates =
	(yyGLRState**) YYREALLOC (yystackp->yytops.yystates,
				  (yystackp->yytops.yycapacity
				   * sizeof yynewStates[0]));
      if (yynewStates == NULL)
	yyMemoryExhausted (yystackp);
      yystackp->yytops.yystates = yynewStates;

      yynewLookaheadNeeds =
	(yybool*) YYREALLOC (yystackp->yytops.yylookaheadNeeds,
			     (yystackp->yytops.yycapacity
			      * sizeof yynewLookaheadNeeds[0]));
      if (yynewLookaheadNeeds == NULL)
	yyMemoryExhausted (yystackp);
      yystackp->yytops.yylookaheadNeeds = yynewLookaheadNeeds;
    }
  yystackp->yytops.yystates[yystackp->yytops.yysize]
    = yystackp->yytops.yystates[yyk];
  yystackp->yytops.yylookaheadNeeds[yystackp->yytops.yysize]
    = yystackp->yytops.yylookaheadNeeds[yyk];
  yystackp->yytops.yysize += 1;
  return yystackp->yytops.yysize-1;
}

/** True iff Y0 and Y1 represent identical options at the top level.
 *  That is, they represent the same rule applied to RHS symbols
 *  that produce the same terminal symbols.  */
static yybool
yyidenticalOptions (yySemanticOption* yyy0, yySemanticOption* yyy1)
{
  if (yyy0->yyrule == yyy1->yyrule)
    {
      yyGLRState *yys0, *yys1;
      int yyn;
      for (yys0 = yyy0->yystate, yys1 = yyy1->yystate,
	   yyn = yyrhsLength (yyy0->yyrule);
	   yyn > 0;
	   yys0 = yys0->yypred, yys1 = yys1->yypred, yyn -= 1)
	if (yys0->yyposn != yys1->yyposn)
	  return yyfalse;
      return yytrue;
    }
  else
    return yyfalse;
}

/** Assuming identicalOptions (Y0,Y1), destructively merge the
 *  alternative semantic values for the RHS-symbols of Y1 and Y0.  */
static void
yymergeOptionSets (yySemanticOption* yyy0, yySemanticOption* yyy1)
{
  yyGLRState *yys0, *yys1;
  int yyn;
  for (yys0 = yyy0->yystate, yys1 = yyy1->yystate,
       yyn = yyrhsLength (yyy0->yyrule);
       yyn > 0;
       yys0 = yys0->yypred, yys1 = yys1->yypred, yyn -= 1)
    {
      if (yys0 == yys1)
	break;
      else if (yys0->yyresolved)
	{
	  yys1->yyresolved = yytrue;
	  yys1->yysemantics.yysval = yys0->yysemantics.yysval;
	}
      else if (yys1->yyresolved)
	{
	  yys0->yyresolved = yytrue;
	  yys0->yysemantics.yysval = yys1->yysemantics.yysval;
	}
      else
	{
	  yySemanticOption** yyz0p;
	  yySemanticOption* yyz1;
	  yyz0p = &yys0->yysemantics.yyfirstVal;
	  yyz1 = yys1->yysemantics.yyfirstVal;
	  while (YYID (yytrue))
	    {
	      if (yyz1 == *yyz0p || yyz1 == NULL)
		break;
	      else if (*yyz0p == NULL)
		{
		  *yyz0p = yyz1;
		  break;
		}
	      else if (*yyz0p < yyz1)
		{
		  yySemanticOption* yyz = *yyz0p;
		  *yyz0p = yyz1;
		  yyz1 = yyz1->yynext;
		  (*yyz0p)->yynext = yyz;
		}
	      yyz0p = &(*yyz0p)->yynext;
	    }
	  yys1->yysemantics.yyfirstVal = yys0->yysemantics.yyfirstVal;
	}
    }
}

/** Y0 and Y1 represent two possible actions to take in a given
 *  parsing state; return 0 if no combination is possible,
 *  1 if user-mergeable, 2 if Y0 is preferred, 3 if Y1 is preferred.  */
static int
yypreference (yySemanticOption* y0, yySemanticOption* y1)
{
  yyRuleNum r0 = y0->yyrule, r1 = y1->yyrule;
  int p0 = yydprec[r0], p1 = yydprec[r1];

  if (p0 == p1)
    {
      if (yymerger[r0] == 0 || yymerger[r0] != yymerger[r1])
	return 0;
      else
	return 1;
    }
  if (p0 == 0 || p1 == 0)
    return 0;
  if (p0 < p1)
    return 3;
  if (p1 < p0)
    return 2;
  return 0;
}

static YYRESULTTAG yyresolveValue (yyGLRState* yys,
				   yyGLRStack* yystackp);


/** Resolve the previous N states starting at and including state S.  If result
 *  != yyok, some states may have been left unresolved possibly with empty
 *  semantic option chains.  Regardless of whether result = yyok, each state
 *  has been left with consistent data so that yydestroyGLRState can be invoked
 *  if necessary.  */
static YYRESULTTAG
yyresolveStates (yyGLRState* yys, int yyn,
		 yyGLRStack* yystackp)
{
  if (0 < yyn)
    {
      YYASSERT (yys->yypred);
      YYCHK (yyresolveStates (yys->yypred, yyn-1, yystackp));
      if (! yys->yyresolved)
	YYCHK (yyresolveValue (yys, yystackp));
    }
  return yyok;
}

/** Resolve the states for the RHS of OPT, perform its user action, and return
 *  the semantic value and location.  Regardless of whether result = yyok, all
 *  RHS states have been destroyed (assuming the user action destroys all RHS
 *  semantic values if invoked).  */
static YYRESULTTAG
yyresolveAction (yySemanticOption* yyopt, yyGLRStack* yystackp,
		 YYSTYPE* yyvalp, YYLTYPE* yylocp)
{
  yyGLRStackItem yyrhsVals[YYMAXRHS + YYMAXLEFT + 1];
  int yynrhs;
  int yychar_current;
  YYSTYPE yylval_current;
  YYLTYPE yylloc_current;
  YYRESULTTAG yyflag;

  yynrhs = yyrhsLength (yyopt->yyrule);
  yyflag = yyresolveStates (yyopt->yystate, yynrhs, yystackp);
  if (yyflag != yyok)
    {
      yyGLRState *yys;
      for (yys = yyopt->yystate; yynrhs > 0; yys = yys->yypred, yynrhs -= 1)
	yydestroyGLRState ("Cleanup: popping", yys);
      return yyflag;
    }

  yyrhsVals[YYMAXRHS + YYMAXLEFT].yystate.yypred = yyopt->yystate;
  yychar_current = yychar;
  yylval_current = yylval;
  yylloc_current = yylloc;
  yychar = yyopt->yyrawchar;
  yylval = yyopt->yyval;
  yylloc = yyopt->yyloc;
  yyflag = yyuserAction (yyopt->yyrule, yynrhs,
			   yyrhsVals + YYMAXRHS + YYMAXLEFT - 1,
			   yyvalp, yylocp, yystackp);
  yychar = yychar_current;
  yylval = yylval_current;
  yylloc = yylloc_current;
  return yyflag;
}

#if YYDEBUG
static void
yyreportTree (yySemanticOption* yyx, int yyindent)
{
  int yynrhs = yyrhsLength (yyx->yyrule);
  int yyi;
  yyGLRState* yys;
  yyGLRState* yystates[1 + YYMAXRHS];
  yyGLRState yyleftmost_state;

  for (yyi = yynrhs, yys = yyx->yystate; 0 < yyi; yyi -= 1, yys = yys->yypred)
    yystates[yyi] = yys;
  if (yys == NULL)
    {
      yyleftmost_state.yyposn = 0;
      yystates[0] = &yyleftmost_state;
    }
  else
    yystates[0] = yys;

  if (yyx->yystate->yyposn < yys->yyposn + 1)
    YYFPRINTF (stderr, "%*s%s -> <Rule %d, empty>\n",
	       yyindent, "", yytokenName (yylhsNonterm (yyx->yyrule)),
	       yyx->yyrule - 1);
  else
    YYFPRINTF (stderr, "%*s%s -> <Rule %d, tokens %lu .. %lu>\n",
	       yyindent, "", yytokenName (yylhsNonterm (yyx->yyrule)),
	       yyx->yyrule - 1, (unsigned long int) (yys->yyposn + 1),
	       (unsigned long int) yyx->yystate->yyposn);
  for (yyi = 1; yyi <= yynrhs; yyi += 1)
    {
      if (yystates[yyi]->yyresolved)
	{
	  if (yystates[yyi-1]->yyposn+1 > yystates[yyi]->yyposn)
	    YYFPRINTF (stderr, "%*s%s <empty>\n", yyindent+2, "",
		       yytokenName (yyrhs[yyprhs[yyx->yyrule]+yyi-1]));
	  else
	    YYFPRINTF (stderr, "%*s%s <tokens %lu .. %lu>\n", yyindent+2, "",
		       yytokenName (yyrhs[yyprhs[yyx->yyrule]+yyi-1]),
		       (unsigned long int) (yystates[yyi - 1]->yyposn + 1),
		       (unsigned long int) yystates[yyi]->yyposn);
	}
      else
	yyreportTree (yystates[yyi]->yysemantics.yyfirstVal, yyindent+2);
    }
}
#endif

/*ARGSUSED*/ static YYRESULTTAG
yyreportAmbiguity (yySemanticOption* yyx0,
		   yySemanticOption* yyx1)
{
  YYUSE (yyx0);
  YYUSE (yyx1);

#if YYDEBUG
  YYFPRINTF (stderr, "Ambiguity detected.\n");
  YYFPRINTF (stderr, "Option 1,\n");
  yyreportTree (yyx0, 2);
  YYFPRINTF (stderr, "\nOption 2,\n");
  yyreportTree (yyx1, 2);
  YYFPRINTF (stderr, "\n");
#endif

  yyerror (YY_("syntax is ambiguous"));
  return yyabort;
}

/** Starting at and including state S1, resolve the location for each of the
 *  previous N1 states that is unresolved.  The first semantic option of a state
 *  is always chosen.  */
static void
yyresolveLocations (yyGLRState* yys1, int yyn1,
		    yyGLRStack *yystackp)
{
  if (0 < yyn1)
    {
      yyresolveLocations (yys1->yypred, yyn1 - 1, yystackp);
      if (!yys1->yyresolved)
	{
	  yySemanticOption *yyoption;
	  yyGLRStackItem yyrhsloc[1 + YYMAXRHS];
	  int yynrhs;
	  int yychar_current;
	  YYSTYPE yylval_current;
	  YYLTYPE yylloc_current;
	  yyoption = yys1->yysemantics.yyfirstVal;
	  YYASSERT (yyoption != NULL);
	  yynrhs = yyrhsLength (yyoption->yyrule);
	  if (yynrhs > 0)
	    {
	      yyGLRState *yys;
	      int yyn;
	      yyresolveLocations (yyoption->yystate, yynrhs,
				  yystackp);
	      for (yys = yyoption->yystate, yyn = yynrhs;
		   yyn > 0;
		   yys = yys->yypred, yyn -= 1)
		yyrhsloc[yyn].yystate.yyloc = yys->yyloc;
	    }
	  else
	    {
	      /* Both yyresolveAction and yyresolveLocations traverse the GSS
		 in reverse rightmost order.  It is only necessary to invoke
		 yyresolveLocations on a subforest for which yyresolveAction
		 would have been invoked next had an ambiguity not been
		 detected.  Thus the location of the previous state (but not
		 necessarily the previous state itself) is guaranteed to be
		 resolved already.  */
	      yyGLRState *yyprevious = yyoption->yystate;
	      yyrhsloc[0].yystate.yyloc = yyprevious->yyloc;
	    }
	  yychar_current = yychar;
	  yylval_current = yylval;
	  yylloc_current = yylloc;
	  yychar = yyoption->yyrawchar;
	  yylval = yyoption->yyval;
	  yylloc = yyoption->yyloc;
	  YYLLOC_DEFAULT ((yys1->yyloc), yyrhsloc, yynrhs);
	  yychar = yychar_current;
	  yylval = yylval_current;
	  yylloc = yylloc_current;
	}
    }
}

/** Resolve the ambiguity represented in state S, perform the indicated
 *  actions, and set the semantic value of S.  If result != yyok, the chain of
 *  semantic options in S has been cleared instead or it has been left
 *  unmodified except that redundant options may have been removed.  Regardless
 *  of whether result = yyok, S has been left with consistent data so that
 *  yydestroyGLRState can be invoked if necessary.  */
static YYRESULTTAG
yyresolveValue (yyGLRState* yys, yyGLRStack* yystackp)
{
  yySemanticOption* yyoptionList = yys->yysemantics.yyfirstVal;
  yySemanticOption* yybest;
  yySemanticOption** yypp;
  yybool yymerge;
  YYSTYPE yysval;
  YYRESULTTAG yyflag;
  YYLTYPE *yylocp = &yys->yyloc;

  yybest = yyoptionList;
  yymerge = yyfalse;
  for (yypp = &yyoptionList->yynext; *yypp != NULL; )
    {
      yySemanticOption* yyp = *yypp;

      if (yyidenticalOptions (yybest, yyp))
	{
	  yymergeOptionSets (yybest, yyp);
	  *yypp = yyp->yynext;
	}
      else
	{
	  switch (yypreference (yybest, yyp))
	    {
	    case 0:
	      yyresolveLocations (yys, 1, yystackp);
	      return yyreportAmbiguity (yybest, yyp);
	      break;
	    case 1:
	      yymerge = yytrue;
	      break;
	    case 2:
	      break;
	    case 3:
	      yybest = yyp;
	      yymerge = yyfalse;
	      break;
	    default:
	      /* This cannot happen so it is not worth a YYASSERT (yyfalse),
		 but some compilers complain if the default case is
		 omitted.  */
	      break;
	    }
	  yypp = &yyp->yynext;
	}
    }

  if (yymerge)
    {
      yySemanticOption* yyp;
      int yyprec = yydprec[yybest->yyrule];
      yyflag = yyresolveAction (yybest, yystackp, &yysval,
				yylocp);
      if (yyflag == yyok)
	for (yyp = yybest->yynext; yyp != NULL; yyp = yyp->yynext)
	  {
	    if (yyprec == yydprec[yyp->yyrule])
	      {
		YYSTYPE yysval_other;
		YYLTYPE yydummy;
		yyflag = yyresolveAction (yyp, yystackp, &yysval_other,
					  &yydummy);
		if (yyflag != yyok)
		  {
		    yydestruct ("Cleanup: discarding incompletely merged value for",
				yystos[yys->yylrState],
				&yysval);
		    break;
		  }
		yyuserMerge (yymerger[yyp->yyrule], &yysval, &yysval_other);
	      }
	  }
    }
  else
    yyflag = yyresolveAction (yybest, yystackp, &yysval, yylocp);

  if (yyflag == yyok)
    {
      yys->yyresolved = yytrue;
      yys->yysemantics.yysval = yysval;
    }
  else
    yys->yysemantics.yyfirstVal = NULL;
  return yyflag;
}

static YYRESULTTAG
yyresolveStack (yyGLRStack* yystackp)
{
  if (yystackp->yysplitPoint != NULL)
    {
      yyGLRState* yys;
      int yyn;

      for (yyn = 0, yys = yystackp->yytops.yystates[0];
	   yys != yystackp->yysplitPoint;
	   yys = yys->yypred, yyn += 1)
	continue;
      YYCHK (yyresolveStates (yystackp->yytops.yystates[0], yyn, yystackp
			     ));
    }
  return yyok;
}

static void
yycompressStack (yyGLRStack* yystackp)
{
  yyGLRState* yyp, *yyq, *yyr;

  if (yystackp->yytops.yysize != 1 || yystackp->yysplitPoint == NULL)
    return;

  for (yyp = yystackp->yytops.yystates[0], yyq = yyp->yypred, yyr = NULL;
       yyp != yystackp->yysplitPoint;
       yyr = yyp, yyp = yyq, yyq = yyp->yypred)
    yyp->yypred = yyr;

  yystackp->yyspaceLeft += yystackp->yynextFree - yystackp->yyitems;
  yystackp->yynextFree = ((yyGLRStackItem*) yystackp->yysplitPoint) + 1;
  yystackp->yyspaceLeft -= yystackp->yynextFree - yystackp->yyitems;
  yystackp->yysplitPoint = NULL;
  yystackp->yylastDeleted = NULL;

  while (yyr != NULL)
    {
      yystackp->yynextFree->yystate = *yyr;
      yyr = yyr->yypred;
      yystackp->yynextFree->yystate.yypred = &yystackp->yynextFree[-1].yystate;
      yystackp->yytops.yystates[0] = &yystackp->yynextFree->yystate;
      yystackp->yynextFree += 1;
      yystackp->yyspaceLeft -= 1;
    }
}

static YYRESULTTAG
yyprocessOneStack (yyGLRStack* yystackp, size_t yyk,
		   size_t yyposn)
{
  int yyaction;
  const short int* yyconflicts;
  yyRuleNum yyrule;

  while (yystackp->yytops.yystates[yyk] != NULL)
    {
      yyStateNum yystate = yystackp->yytops.yystates[yyk]->yylrState;
      YYDPRINTF ((stderr, "Stack %lu Entering state %d\n",
		  (unsigned long int) yyk, yystate));

      YYASSERT (yystate != YYFINAL);

      if (yyisDefaultedState (yystate))
	{
	  yyrule = yydefaultAction (yystate);
	  if (yyrule == 0)
	    {
	      YYDPRINTF ((stderr, "Stack %lu dies.\n",
			  (unsigned long int) yyk));
	      yymarkStackDeleted (yystackp, yyk);
	      return yyok;
	    }
	  YYCHK (yyglrReduce (yystackp, yyk, yyrule, yyfalse));
	}
      else
	{
	  yySymbol yytoken;
	  yystackp->yytops.yylookaheadNeeds[yyk] = yytrue;
	  if (yychar == YYEMPTY)
	    {
	      YYDPRINTF ((stderr, "Reading a token: "));
	      yychar = YYLEX;
	      yytoken = YYTRANSLATE (yychar);
	      YY_SYMBOL_PRINT ("Next token is", yytoken, &yylval, &yylloc);
	    }
	  else
	    yytoken = YYTRANSLATE (yychar);
	  yygetLRActions (yystate, yytoken, &yyaction, &yyconflicts);

	  while (*yyconflicts != 0)
	    {
	      size_t yynewStack = yysplitStack (yystackp, yyk);
	      YYDPRINTF ((stderr, "Splitting off stack %lu from %lu.\n",
			  (unsigned long int) yynewStack,
			  (unsigned long int) yyk));
	      YYCHK (yyglrReduce (yystackp, yynewStack,
				  *yyconflicts, yyfalse));
	      YYCHK (yyprocessOneStack (yystackp, yynewStack,
					yyposn));
	      yyconflicts += 1;
	    }

	  if (yyisShiftAction (yyaction))
	    break;
	  else if (yyisErrorAction (yyaction))
	    {
	      YYDPRINTF ((stderr, "Stack %lu dies.\n",
			  (unsigned long int) yyk));
	      yymarkStackDeleted (yystackp, yyk);
	      break;
	    }
	  else
	    YYCHK (yyglrReduce (yystackp, yyk, -yyaction,
				yyfalse));
	}
    }
  return yyok;
}

/*ARGSUSED*/ static void
yyreportSyntaxError (yyGLRStack* yystackp)
{
  if (yystackp->yyerrState == 0)
    {
#if YYERROR_VERBOSE
      int yyn;
      yyn = yypact[yystackp->yytops.yystates[0]->yylrState];
      if (YYPACT_NINF < yyn && yyn <= YYLAST)
	{
	  yySymbol yytoken = YYTRANSLATE (yychar);
	  size_t yysize0 = yytnamerr (NULL, yytokenName (yytoken));
	  size_t yysize = yysize0;
	  size_t yysize1;
	  yybool yysize_overflow = yyfalse;
	  char* yymsg = NULL;
	  enum { YYERROR_VERBOSE_ARGS_MAXIMUM = 5 };
	  char const *yyarg[YYERROR_VERBOSE_ARGS_MAXIMUM];
	  int yyx;
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

	  yyarg[0] = yytokenName (yytoken);
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
		yyarg[yycount++] = yytokenName (yyx);
		yysize1 = yysize + yytnamerr (NULL, yytokenName (yyx));
		yysize_overflow |= yysize1 < yysize;
		yysize = yysize1;
		yyfmt = yystpcpy (yyfmt, yyprefix);
		yyprefix = yyor;
	      }

	  yyf = YY_(yyformat);
	  yysize1 = yysize + strlen (yyf);
	  yysize_overflow |= yysize1 < yysize;
	  yysize = yysize1;

	  if (!yysize_overflow)
	    yymsg = (char *) YYMALLOC (yysize);

	  if (yymsg)
	    {
	      char *yyp = yymsg;
	      int yyi = 0;
	      while ((*yyp = *yyf))
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
	      yyerror (yymsg);
	      YYFREE (yymsg);
	    }
	  else
	    {
	      yyerror (YY_("syntax error"));
	      yyMemoryExhausted (yystackp);
	    }
	}
      else
#endif /* YYERROR_VERBOSE */
	yyerror (YY_("syntax error"));
      yynerrs += 1;
    }
}

/* Recover from a syntax error on *YYSTACKP, assuming that *YYSTACKP->YYTOKENP,
   yylval, and yylloc are the syntactic category, semantic value, and location
   of the look-ahead.  */
/*ARGSUSED*/ static void
yyrecoverSyntaxError (yyGLRStack* yystackp)
{
  size_t yyk;
  int yyj;

  if (yystackp->yyerrState == 3)
    /* We just shifted the error token and (perhaps) took some
       reductions.  Skip tokens until we can proceed.  */
    while (YYID (yytrue))
      {
	yySymbol yytoken;
	if (yychar == YYEOF)
	  yyFail (yystackp, NULL);
	if (yychar != YYEMPTY)
	  {
	    yytoken = YYTRANSLATE (yychar);
	    yydestruct ("Error: discarding",
			yytoken, &yylval);
	  }
	YYDPRINTF ((stderr, "Reading a token: "));
	yychar = YYLEX;
	yytoken = YYTRANSLATE (yychar);
	YY_SYMBOL_PRINT ("Next token is", yytoken, &yylval, &yylloc);
	yyj = yypact[yystackp->yytops.yystates[0]->yylrState];
	if (yyis_pact_ninf (yyj))
	  return;
	yyj += yytoken;
	if (yyj < 0 || YYLAST < yyj || yycheck[yyj] != yytoken)
	  {
	    if (yydefact[yystackp->yytops.yystates[0]->yylrState] != 0)
	      return;
	  }
	else if (yytable[yyj] != 0 && ! yyis_table_ninf (yytable[yyj]))
	  return;
      }

  /* Reduce to one stack.  */
  for (yyk = 0; yyk < yystackp->yytops.yysize; yyk += 1)
    if (yystackp->yytops.yystates[yyk] != NULL)
      break;
  if (yyk >= yystackp->yytops.yysize)
    yyFail (yystackp, NULL);
  for (yyk += 1; yyk < yystackp->yytops.yysize; yyk += 1)
    yymarkStackDeleted (yystackp, yyk);
  yyremoveDeletes (yystackp);
  yycompressStack (yystackp);

  /* Now pop stack until we find a state that shifts the error token.  */
  yystackp->yyerrState = 3;
  while (yystackp->yytops.yystates[0] != NULL)
    {
      yyGLRState *yys = yystackp->yytops.yystates[0];
      yyj = yypact[yys->yylrState];
      if (! yyis_pact_ninf (yyj))
	{
	  yyj += YYTERROR;
	  if (0 <= yyj && yyj <= YYLAST && yycheck[yyj] == YYTERROR
	      && yyisShiftAction (yytable[yyj]))
	    {
	      /* Shift the error token having adjusted its location.  */
	      YYLTYPE yyerrloc;
	      YY_SYMBOL_PRINT ("Shifting", yystos[yytable[yyj]],
			       &yylval, &yyerrloc);
	      yyglrShift (yystackp, 0, yytable[yyj],
			  yys->yyposn, &yylval, &yyerrloc);
	      yys = yystackp->yytops.yystates[0];
	      break;
	    }
	}

      yydestroyGLRState ("Error: popping", yys);
      yystackp->yytops.yystates[0] = yys->yypred;
      yystackp->yynextFree -= 1;
      yystackp->yyspaceLeft += 1;
    }
  if (yystackp->yytops.yystates[0] == NULL)
    yyFail (yystackp, NULL);
}

#define YYCHK1(YYE)							     \
  do {									     \
    switch (YYE) {							     \
    case yyok:								     \
      break;								     \
    case yyabort:							     \
      goto yyabortlab;							     \
    case yyaccept:							     \
      goto yyacceptlab;							     \
    case yyerr:								     \
      goto yyuser_error;						     \
    default:								     \
      goto yybuglab;							     \
    }									     \
  } while (YYID (0))


/*----------.
| yyparse.  |
`----------*/

int
yyparse (void)
{
  int yyresult;
  yyGLRStack yystack;
  yyGLRStack* const yystackp = &yystack;
  size_t yyposn;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yychar = YYEMPTY;
  yylval = yyval_default;


  if (! yyinitGLRStack (yystackp, YYINITDEPTH))
    goto yyexhaustedlab;
  switch (YYSETJMP (yystack.yyexception_buffer))
    {
    case 0: break;
    case 1: goto yyabortlab;
    case 2: goto yyexhaustedlab;
    default: goto yybuglab;
    }
  yyglrShift (&yystack, 0, 0, 0, &yylval, &yylloc);
  yyposn = 0;

  while (YYID (yytrue))
    {
      /* For efficiency, we have two loops, the first of which is
	 specialized to deterministic operation (single stack, no
	 potential ambiguity).  */
      /* Standard mode */
      while (YYID (yytrue))
	{
	  yyRuleNum yyrule;
	  int yyaction;
	  const short int* yyconflicts;

	  yyStateNum yystate = yystack.yytops.yystates[0]->yylrState;
	  YYDPRINTF ((stderr, "Entering state %d\n", yystate));
	  if (yystate == YYFINAL)
	    goto yyacceptlab;
	  if (yyisDefaultedState (yystate))
	    {
	      yyrule = yydefaultAction (yystate);
	      if (yyrule == 0)
		{

		  yyreportSyntaxError (&yystack);
		  goto yyuser_error;
		}
	      YYCHK1 (yyglrReduce (&yystack, 0, yyrule, yytrue));
	    }
	  else
	    {
	      yySymbol yytoken;
	      if (yychar == YYEMPTY)
		{
		  YYDPRINTF ((stderr, "Reading a token: "));
		  yychar = YYLEX;
		  yytoken = YYTRANSLATE (yychar);
		  YY_SYMBOL_PRINT ("Next token is", yytoken, &yylval, &yylloc);
		}
	      else
		yytoken = YYTRANSLATE (yychar);
	      yygetLRActions (yystate, yytoken, &yyaction, &yyconflicts);
	      if (*yyconflicts != 0)
		break;
	      if (yyisShiftAction (yyaction))
		{
		  YY_SYMBOL_PRINT ("Shifting", yytoken, &yylval, &yylloc);
		  if (yychar != YYEOF)
		    yychar = YYEMPTY;
		  yyposn += 1;
		  yyglrShift (&yystack, 0, yyaction, yyposn, &yylval, &yylloc);
		  if (0 < yystack.yyerrState)
		    yystack.yyerrState -= 1;
		}
	      else if (yyisErrorAction (yyaction))
		{

		  yyreportSyntaxError (&yystack);
		  goto yyuser_error;
		}
	      else
		YYCHK1 (yyglrReduce (&yystack, 0, -yyaction, yytrue));
	    }
	}

      while (YYID (yytrue))
	{
	  yySymbol yytoken_to_shift;
	  size_t yys;

	  for (yys = 0; yys < yystack.yytops.yysize; yys += 1)
	    yystackp->yytops.yylookaheadNeeds[yys] = yychar != YYEMPTY;

	  /* yyprocessOneStack returns one of three things:

	      - An error flag.  If the caller is yyprocessOneStack, it
		immediately returns as well.  When the caller is finally
		yyparse, it jumps to an error label via YYCHK1.

	      - yyok, but yyprocessOneStack has invoked yymarkStackDeleted
		(&yystack, yys), which sets the top state of yys to NULL.  Thus,
		yyparse's following invocation of yyremoveDeletes will remove
		the stack.

	      - yyok, when ready to shift a token.

	     Except in the first case, yyparse will invoke yyremoveDeletes and
	     then shift the next token onto all remaining stacks.  This
	     synchronization of the shift (that is, after all preceding
	     reductions on all stacks) helps prevent double destructor calls
	     on yylval in the event of memory exhaustion.  */

	  for (yys = 0; yys < yystack.yytops.yysize; yys += 1)
	    YYCHK1 (yyprocessOneStack (&yystack, yys, yyposn));
	  yyremoveDeletes (&yystack);
	  if (yystack.yytops.yysize == 0)
	    {
	      yyundeleteLastStack (&yystack);
	      if (yystack.yytops.yysize == 0)
		yyFail (&yystack, YY_("syntax error"));
	      YYCHK1 (yyresolveStack (&yystack));
	      YYDPRINTF ((stderr, "Returning to deterministic operation.\n"));

	      yyreportSyntaxError (&yystack);
	      goto yyuser_error;
	    }

	  /* If any yyglrShift call fails, it will fail after shifting.  Thus,
	     a copy of yylval will already be on stack 0 in the event of a
	     failure in the following loop.  Thus, yychar is set to YYEMPTY
	     before the loop to make sure the user destructor for yylval isn't
	     called twice.  */
	  yytoken_to_shift = YYTRANSLATE (yychar);
	  yychar = YYEMPTY;
	  yyposn += 1;
	  for (yys = 0; yys < yystack.yytops.yysize; yys += 1)
	    {
	      int yyaction;
	      const short int* yyconflicts;
	      yyStateNum yystate = yystack.yytops.yystates[yys]->yylrState;
	      yygetLRActions (yystate, yytoken_to_shift, &yyaction,
			      &yyconflicts);
	      /* Note that yyconflicts were handled by yyprocessOneStack.  */
	      YYDPRINTF ((stderr, "On stack %lu, ", (unsigned long int) yys));
	      YY_SYMBOL_PRINT ("shifting", yytoken_to_shift, &yylval, &yylloc);
	      yyglrShift (&yystack, yys, yyaction, yyposn,
			  &yylval, &yylloc);
	      YYDPRINTF ((stderr, "Stack %lu now in state #%d\n",
			  (unsigned long int) yys,
			  yystack.yytops.yystates[yys]->yylrState));
	    }

	  if (yystack.yytops.yysize == 1)
	    {
	      YYCHK1 (yyresolveStack (&yystack));
	      YYDPRINTF ((stderr, "Returning to deterministic operation.\n"));
	      yycompressStack (&yystack);
	      break;
	    }
	}
      continue;
    yyuser_error:
      yyrecoverSyntaxError (&yystack);
      yyposn = yystack.yytops.yystates[0]->yyposn;
    }

 yyacceptlab:
  yyresult = 0;
  goto yyreturn;

 yybuglab:
  YYASSERT (yyfalse);
  goto yyabortlab;

 yyabortlab:
  yyresult = 1;
  goto yyreturn;

 yyexhaustedlab:
  yyerror (YY_("memory exhausted"));
  yyresult = 2;
  goto yyreturn;

 yyreturn:
  if (yychar != YYEOF && yychar != YYEMPTY)
    yydestruct ("Cleanup: discarding lookahead",
		YYTRANSLATE (yychar),
		&yylval);

  /* If the stack is well-formed, pop the stack until it is empty,
     destroying its entries as we go.  But free the stack regardless
     of whether it is well-formed.  */
  if (yystack.yyitems)
    {
      yyGLRState** yystates = yystack.yytops.yystates;
      if (yystates)
	{
	  size_t yysize = yystack.yytops.yysize;
	  size_t yyk;
	  for (yyk = 0; yyk < yysize; yyk += 1)
	    if (yystates[yyk])
	      {
		while (yystates[yyk])
		  {
		    yyGLRState *yys = yystates[yyk];
		    yydestroyGLRState ("Cleanup: popping", yys);
		    yystates[yyk] = yys->yypred;
		    yystack.yynextFree -= 1;
		    yystack.yyspaceLeft += 1;
		  }
		break;
	      }
	}
      yyfreeGLRStack (&yystack);
    }

  /* Make sure YYID is used.  */
  return YYID (yyresult);
}

/* DEBUGGING ONLY */
#ifdef YYDEBUG
static void yypstack (yyGLRStack* yystackp, size_t yyk)
  __attribute__ ((__unused__));
static void yypdumpstack (yyGLRStack* yystackp) __attribute__ ((__unused__));

static void
yy_yypstack (yyGLRState* yys)
{
  if (yys->yypred)
    {
      yy_yypstack (yys->yypred);
      fprintf (stderr, " -> ");
    }
  fprintf (stderr, "%d@%lu", yys->yylrState, (unsigned long int) yys->yyposn);
}

static void
yypstates (yyGLRState* yyst)
{
  if (yyst == NULL)
    fprintf (stderr, "<null>");
  else
    yy_yypstack (yyst);
  fprintf (stderr, "\n");
}

static void
yypstack (yyGLRStack* yystackp, size_t yyk)
{
  yypstates (yystackp->yytops.yystates[yyk]);
}

#define YYINDEX(YYX)							     \
    ((YYX) == NULL ? -1 : (yyGLRStackItem*) (YYX) - yystackp->yyitems)


static void
yypdumpstack (yyGLRStack* yystackp)
{
  yyGLRStackItem* yyp;
  size_t yyi;
  for (yyp = yystackp->yyitems; yyp < yystackp->yynextFree; yyp += 1)
    {
      fprintf (stderr, "%3lu. ", (unsigned long int) (yyp - yystackp->yyitems));
      if (*(yybool *) yyp)
	{
	  fprintf (stderr, "Res: %d, LR State: %d, posn: %lu, pred: %ld",
		   yyp->yystate.yyresolved, yyp->yystate.yylrState,
		   (unsigned long int) yyp->yystate.yyposn,
		   (long int) YYINDEX (yyp->yystate.yypred));
	  if (! yyp->yystate.yyresolved)
	    fprintf (stderr, ", firstVal: %ld",
		     (long int) YYINDEX (yyp->yystate.yysemantics.yyfirstVal));
	}
      else
	{
	  fprintf (stderr, "Option. rule: %d, state: %ld, next: %ld",
		   yyp->yyoption.yyrule - 1,
		   (long int) YYINDEX (yyp->yyoption.yystate),
		   (long int) YYINDEX (yyp->yyoption.yynext));
	}
      fprintf (stderr, "\n");
    }
  fprintf (stderr, "Tops:");
  for (yyi = 0; yyi < yystackp->yytops.yysize; yyi += 1)
    fprintf (stderr, "%lu: %ld; ", (unsigned long int) yyi,
	     (long int) YYINDEX (yystackp->yytops.yystates[yyi]));
  fprintf (stderr, "\n");
}
#endif


#line 3263 "../../src/executables/esql_grammar.y"
 

#define UCI_OPT_UNSAFE_NULL     0x0001
#define makestring1(x) #x
#define makestring(x) makestring1(x)
#if !defined(PRODUCT_STRING)
#define PRODUCT_STRING makestring(RELEASE_STRING)
#endif

#define ESQL_MSG_CATALOG        "esql.cat"

char exec_echo[10] = "";
static char *outfile_name = NULL;
bool need_line_directive;
FILE *esql_yyin, *esql_yyout;
char *esql_yyfilename;
int esql_yyendcol = 0;
int errors = 0;
varstring *current_buf;		/* remain PUBLIC for debugging ease */
ECHO_FN echo_fn = &echo_stream;
char g_delay[1024] = { 0, };
bool g_indicator = false;
varstring *g_varstring = NULL;
varstring *g_subscript = NULL;

typedef struct pt_host_refs
{
  unsigned char *descr[2];	/* at most 2 descriptors: 1 in, 1 out */
  int descr_occupied;		/* occupied slots in descr            */
  HOST_REF **host_refs;		/* array of pointers to HOST_REF      */
  int occupied;			/* occupied slots in host_refs        */
  int allocated;		/* allocated slots in host_refs       */
  unsigned char *in_descriptor;	/* NULL or input descriptor name      */
  unsigned char *out_descriptor;	/* NULL or output descriptor name     */
  HOST_REF **in_refs;		/* array of input HOST_REF addresses  */
  int in_refs_occupied;
  int in_refs_allocated;
  HOST_REF **out_refs;		/* array of output HOST_REF addresses */
  int out_refs_occupied;
  int out_refs_allocated;
} PT_HOST_REFS;

enum esqlmain_msg
{
  MSG_REPEATED_IGNORED = 1,
  MSG_VERSION = 2
};

const char *VARCHAR_ARRAY_NAME = "array";
const char *VARCHAR_LENGTH_NAME = "length";
const char *pp_include_path;
char *pp_include_file = NULL;
const char *prog_name;

unsigned int pp_uci_opt = 0;
int pp_emit_line_directives = 1;
int pp_dump_scope_info = 0;
int pp_dump_malloc_info = 0;
int pp_announce_version = 0;
int pp_enable_uci_trace = 0;
int pp_disable_varchar_length = 0;
int pp_varchar2 = 0;
int pp_unsafe_null = 0;
int pp_internal_ind = 0;
PARSER_CONTEXT *parser;
char *pt_buffer;
varstring pt_statement_buf;


/*
 * check() -
 * return : void
 * cond(in) :
 */
static void
check (int cond)
{
  if (!cond)
    {
      perror (prog_name);
      exit (1);
    }
}

/*
 * check_w_file() -
 * return : void
 * cond(in) :
 * filename(in) :
 */
static void
check_w_file (int cond, const char *filename)
{
  if (!cond)
    {
      char buf[2 * PATH_MAX];
      sprintf (buf, "%s: %s", prog_name, filename);
      perror (buf);
      exit (1);
    }
}

/*
 * reset_globals() -
 * return : void
 */
static void
reset_globals (void)
{
  input_refs = NULL;
  output_refs = NULL;
  repeat_option = false;
  already_translated = false;
  structs_allowed = false;
  hvs_allowed = true;

  esql_yy_set_buf (NULL);
  pp_clear_host_refs ();
  pp_gather_input_refs ();
}

/*
 * ignore_repeat() -
 * return : void
 */
static void
ignore_repeat (void)
{
  if (repeat_option)
    {
      esql_yyvwarn (pp_get_msg (EX_ESQLMMAIN_SET, MSG_REPEATED_IGNORED));
    }
}


#if defined (ENABLE_UNUSED_FUNCTION)
static int
validate_input_file (void *fname, FILE * outfp)
{
  return (fname == NULL) && isatty (fileno (stdin));
}
#endif /* ENABLE_UNUSED_FUNCTION */


static char *
ofile_name (char *fname)
{
  static char buf[PATH_MAX + 1]; /* reserve buffer for '\0' */
  char *p;

  /* Get the last pathname component into buf. */
  p = strrchr (fname, '/');
  strncpy (buf, p ? (p + 1) : fname, sizeof(buf));

  /* Now add the .c suffix, copying over the .ec suffix if present. */
  p = strrchr (buf, '.');
  if (p && !STREQ (p, ".c"))
    {
      strcpy (p, ".c");
    }
  else
    {
      strncat (buf, ".c", PATH_MAX - strnlen(buf, sizeof(buf)));
    }

  return buf;
}

/*
 * copy() -
 * return : void
 * fp(in) :
 * fname(in) :
 */
static void
copy (FILE * fp, const char *fname)
{
  int ifd, ofd;
  int rbytes, wbytes;
  char buf[BUFSIZ];

  rewind (fp);

  ifd = fileno (fp);
  ofd = open (fname, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  check_w_file (ofd >= 0, fname);

  /* Now copy the contents; use read() and write() to try to cut down
     on excess buffering. */
  rbytes = 0;
  wbytes = 0;
  while ((rbytes = read (ifd, buf, sizeof (buf))) > 0)
    {
      wbytes = write (ofd, buf, rbytes);
      if (wbytes != rbytes)
	{
	  break;
	}
    }
  check (rbytes >= 0 && wbytes >= 0);

  close (ofd);
}


static const char *
pp_unsafe_get_msg (int msg_set, int msg_num)
{
  static int complained = 0;
  static MSG_CATD msg_cat = NULL;

  const char *msg;

  if (msg_cat == NULL)
    {
      if (complained)
	{
	  return NULL;
	}

      msg_cat = msgcat_open (ESQL_MSG_CATALOG);
      if (msg_cat == NULL)
	{
	  complained = 1;
	  fprintf (stderr, "%s: %s: %s\n",
		   prog_name, ESQL_MSG_CATALOG, strerror (ENOENT));
	  return NULL;
	}
    }

  msg = msgcat_gets (msg_cat, msg_set, msg_num, NULL);

  return (msg == NULL || msg[0] == '\0') ? NULL : msg;
}

/*
 * pp_get_msg() -
 * return :
 * msg_set(in) :
 * msg_num(in) :
 */
const char *
pp_get_msg (int msg_set, int msg_num)
{
  static const char no_msg[] = "No message available.";
  const char *msg;

  msg = pp_unsafe_get_msg (msg_set, msg_num);
  return msg ? msg : no_msg;
}


static void
usage (void)
{
  fprintf (stderr,
	   "usage: cubrid_esql [OPTION] input-file\n\n"
	   "valid option:\n"
	   "  -l, --suppress-line-directive  suppress #line directives in output file; default: off\n"
	   "  -o, --output-file              output file name; default: stdout\n"
	   "  -h, --include-file             include file containing uci_ prototypes; default: cubrid_esql.h\n"
	   "  -s, --dump-scope-info          dump scope debugging information; default: off\n"
	   "  -m, --dump-malloc-info         dump final malloc debugging information; default: off\n"
	   "  -t, --enable-uci-trace         enable UCI function trace; default: off\n"
	   "  -d, --disable-varchar-length   disable length initialization of VARCHAR host variable; default; off\n"
	   "  -2, --varchar2-style           use different VARCHAR style; default: off\n"
	   "  -u, --unsafe-null              ignore -462(null indicator needed) error: default; off\n"
	   "  -e, --internal-indicator       use internal NULL indicator to prevent -462(null indicator needed) error, default off\n");
}

static int
get_next ()
{
  return fgetc (esql_yyin);
}

/*
 * main() -
 * return :
 * argc(in) :
 * argv(in) :
 */
int
main (int argc, char **argv)
{
  struct option esql_option[] = {
    {"suppress-line-directive", 0, 0, 'l'},
    {"output-file", 1, 0, 'o'},
    {"include-file", 1, 0, 'h'},
    {"dump-scope-info", 0, 0, 's'},
    {"dump-malloc-info", 0, 0, 'm'},
    {"version", 0, 0, 'v'},
    {"enable-uci-trace", 0, 0, 't'},
    {"disable-varchar-length", 0, 0, 'd'},
    {"varchar2-style", 0, 0, '2'},
    {"unsafe-null", 0, 0, 'u'},
    {"internal-indicator", 0, 0, 'e'},
    {0, 0, 0, 0}
  };

  while (1)
    {
      int option_index = 0;
      int option_key;

      option_key = getopt_long (argc, argv, "lo:h:smvtd2ue",
				esql_option, &option_index);
      if (option_key == -1)
	{
	  break;
	}

      switch (option_key)
	{
	case 'l':
	  pp_emit_line_directives = true;
	  break;
	case 'o':
	  if (outfile_name != NULL)
	    {
	      free_and_init(outfile_name);
	    }
	  outfile_name = strdup (optarg);
	  break;
	case 'h':
	  if (pp_include_file != NULL)
	    {
	      free_and_init(pp_include_file);
	    }
	  pp_include_file = strdup (optarg);
	  break;
	case 's':
	  pp_dump_scope_info = true;
	  break;
	case 'm':
	  pp_dump_malloc_info = true;
	  break;
	case 'v':
	  pp_announce_version = true;
	  break;
	case 't':
	  pp_enable_uci_trace = true;
	  break;
	case 'd':
	  pp_disable_varchar_length = true;
	  break;
	case '2':
	  pp_varchar2 = true;
	  break;
	case 'u':
	  pp_unsafe_null = true;
	  break;
	case 'e':
	  pp_internal_ind = true;
	  break;
	default:
	  usage ();
	  return errors;
	}
    }

  if (optind < argc)
    {
      esql_yyfilename = argv[optind];
    }
  else
    {
      usage ();
      return errors;
    }

  prog_name = argv[0];
  if (pp_announce_version)
    {
      printf (pp_get_msg (EX_ESQLMMAIN_SET, MSG_VERSION), argv[0], PRODUCT_STRING);
    }

  if (pp_varchar2)
    {
      VARCHAR_ARRAY_NAME = "arr";
      VARCHAR_LENGTH_NAME = "len";
    }

  if (pp_unsafe_null)
    {
      pp_uci_opt |= UCI_OPT_UNSAFE_NULL;
    }

  pp_include_path = envvar_root ();
  if (pp_include_path == NULL)
    {
      exit (1);
    }

  if (esql_yyfilename == NULL)
    {
      /* No input filename was supplied; use stdin for input and stdout
         for output. */
      esql_yyin = stdin;
    }
  else
    {
      esql_yyin = fopen (esql_yyfilename, "r");
      check_w_file (esql_yyin != NULL, esql_yyfilename);
    }

  if (esql_yyfilename && !outfile_name)
    {
      outfile_name = ofile_name (esql_yyfilename);
    }

  if (outfile_name)
    {
      esql_yyout = tmpfile ();
      check (esql_yyout != NULL);
    }
  else
    {
      esql_yyout = stdout;
    }

  esql_yyinit ();
  pp_startup ();
  vs_new (&pt_statement_buf);
  emit_line_directive ();

  if (utility_initialize () != NO_ERROR)
    {
      exit (1);
    }
  parser = parser_create_parser ();
  pt_buffer = pt_append_string (parser, NULL, NULL);

  esql_yyparse ();

  if (esql_yyout != stdout)
    {
      if (errors == 0)
	{
	  /*
	   * We want to keep the file, so rewind the temp file and copy
	   * it to the named file. It really would be nice if we could
	   * just associate the name with the file resources we've
	   * already created, but there doesn't seem to be a way to do
	   * that.
	   */
	  copy (esql_yyout, outfile_name);
	}
    }

  if (esql_yyin != NULL)
    {
      fclose (esql_yyin);
    }

  if (esql_yyout != NULL)
    {
      fclose (esql_yyout);
    }

  msgcat_final ();
  exit (errors);

  free_and_init (outfile_name);
  free_and_init (pp_include_file);

  return errors;
}

/*
 * pt_print_errors() - print to stderr all parsing error messages associated
 *    with current statement
 * return : void
 * parser_in(in) : the parser context
 */
void
pt_print_errors (PARSER_CONTEXT * parser_in)
{
  int statement_no, line_no, col_no;
  const char *msg = NULL;
  PT_NODE *err = NULL;

  err = pt_get_errors (parser);
  do
    {
      err = pt_get_next_error (err, &statement_no, &line_no, &col_no, &msg);
      if (msg)
	esql_yyverror (msg);
    }
  while (err);

  /* sleazy way to clear errors */
  parser = parser_create_parser ();
}


/*
 * pp_suppress_echo() -
 * return : void
 * suppress(in) :
 */
void
pp_suppress_echo (int suppress)
{
  suppress_echo = suppress;
}

/*
 * echo_stream() -
 * return: void
 * str(in) :
 * length(in) :
 */
void
echo_stream (const char *str, int length)
{
  if (!suppress_echo)
    {
      if (exec_echo[0])
	{
	  fputs (exec_echo, esql_yyout);
	  exec_echo[0] = 0;
	}
      fwrite ((char *) str, length, 1, esql_yyout);
      //YY_FLUSH_ON_DEBUG;
    }
}

/*
 * echo_vstr() -
 * return : void
 * str(in) :
 * length(in) :
 */
void
echo_vstr (const char *str, int length)
{

  if (!suppress_echo)
    {
      if (current_buf)
	{
	  vs_strcatn (current_buf, str, length);
	}
    }
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * echo_devnull() -
 * return : void
 * str(in) :
 * length(in) :
 */
void
echo_devnull (const char *str, int length)
{
  /* Do nothing */
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * pp_set_echo() -
 * return :
 * new_echo(in) :
 */
ECHO_FN
pp_set_echo (ECHO_FN new_echo)
{
  ECHO_FN old_echo;

  old_echo = echo_fn;
  echo_fn = new_echo;

  return old_echo;
}


/*
 * esql_yyvmsg() -
 * return : void
 * fmt(in) :
 * args(in) :
 */
static void
esql_yyvmsg (const char *fmt, va_list args)
{
  /* Flush esql_yyout just in case stderr and esql_yyout are the same. */
  fflush (esql_yyout);

  if (esql_yyfilename)
    {
      fprintf (stderr, "%s:", esql_yyfilename);
    }

  fprintf (stderr, "%d: ", esql_yylineno);
  vfprintf (stderr, fmt, args);
  fputs ("\n", stderr);
  fflush (stderr);
}

/*
 * esql_yyerror() -
 * return : void
 * s(in) :
 */
void
esql_yyerror (const char *s)
{
  esql_yyverror ("%s before \"%s\"", s, esql_yytext);
}

/*
 * esql_yyverror() -
 * return : void
 * fmt(in) :
 */
void
esql_yyverror (const char *fmt, ...)
{
  va_list args;

  errors++;

  va_start (args, fmt);
  esql_yyvmsg (fmt, args);
  va_end (args);
}

/*
 * esql_yyvwarn() -
 * return : void
 * fmt(in) :
 */
void
esql_yyvwarn (const char *fmt, ...)
{
  va_list args;

  va_start (args, fmt);
  esql_yyvmsg (fmt, args);
  va_end (args);
}

/*
 * esql_yyredef() -
 * return : void
 * name(in) :
 */
void
esql_yyredef (char *name)
{
  /* Eventually we will probably want to turn this off, but it's
     interesting to find out about perceived redefinitions right now. */
  esql_yyverror ("redefinition of symbol \"%s\"", name);
}




/*
 * keyword_cmp() -
 * return :
 * a(in) :
 * b(in) :
 */
static int
keyword_cmp (const void *a, const void *b)
{
  return strcmp (((const KEYWORD_REC *) a)->keyword,
		 ((const KEYWORD_REC *) b)->keyword);
}

/*
 * keyword_case_cmp() -
 * return :
 * a(in) :
 * b(in) :
 */
static int
keyword_case_cmp (const void *a, const void *b)
{
  return intl_mbs_casecmp (((const KEYWORD_REC *) a)->keyword,
			   ((const KEYWORD_REC *) b)->keyword);
}





/*
 * check_c_identifier() -
 * return :
 */
int
check_c_identifier (char *name)
{
  KEYWORD_REC *p;
  KEYWORD_REC dummy;
  SYMBOL *sym;

#ifdef PARSER_DEBUG
  printf ("check c id: %s\n", name);
#endif

  /*
   * Check first to see if we have a keyword.
   */
  dummy.keyword = name;
  p = (KEYWORD_REC *) bsearch (&dummy,
			       c_keywords,
			       sizeof (c_keywords) / sizeof (c_keywords[0]),
			       sizeof (KEYWORD_REC), &keyword_cmp);

  if (p)
    {
      /*
       * Notic that this is "sticky", unlike in check_identifier().
       * Once we've seen a keyword that initiates suppression, we don't
       * want anything echoed until the upper level grammar productions
       * say so.  In particular, we don't want echoing to be re-enabled
       * the very next time we see an ordinary C keyword.
       */
      suppress_echo = suppress_echo || p->suppress_echo;
      return p->value;
    }

  /*
   * If not, see if this is an already-encountered identifier.
   */
  g_identifier_symbol = sym =
    pp_findsym (pp_Symbol_table, (unsigned char *) name);
  if (sym)
    {
      /*
       * Something by this name lives in the symbol table.
       *
       * This needs to be made sensitive to whether typedef names are
       * being recognized or not.
       */
      return sym->type->tdef && pp_recognizing_typedef_names ? TYPEDEF_NAME :
	IS_INT_CONSTANT (sym->type) ? ENUMERATION_CONSTANT : IDENTIFIER;
    }
  else
    {
      /*
       * Symbol wasn't recognized in the symbol table, so this must be
       * a new identifier.
       */
      return IDENTIFIER;
    }
}



/*
 * check_identifier() -
 * return :
 * keywords(in) :
 */
int
check_identifier (KEYWORD_TABLE * keywords, char *name)
{
  KEYWORD_REC *p;
  KEYWORD_REC dummy;

  /*
   * Check first to see if we have a keyword.
   */
  dummy.keyword = name;
  p = (KEYWORD_REC *) bsearch (&dummy,
			       keywords->keyword_array,
			       keywords->size,
			       sizeof (KEYWORD_REC), &keyword_case_cmp);

  if (p)
    {
      suppress_echo = p->suppress_echo;
      return p->value;
    }
  else
    {
      suppress_echo = false;
      return IDENTIFIER;
    }
}

/*
 * esql_yy_push_mode() -
 * return : void
 * new_mode(in) :
 */
void
esql_yy_push_mode (enum scanner_mode new_mode)
{
  SCANNER_MODE_RECORD *next = malloc (sizeof (SCANNER_MODE_RECORD));
  if (next == NULL)
    {
      esql_yyverror (pp_get_msg (EX_MISC_SET, MSG_OUT_OF_MEMORY));
      exit(1);
      return;
    }

  next->previous_mode = mode;
  next->recognize_keywords = recognize_keywords;
  next->previous_record = mode_stack;
  next->suppress_echo = suppress_echo;
  mode_stack = next;

  /*
   * Set up so that the first id-like token that we see in VAR_MODE
   * will be reported as an IDENTIFIER, regardless of whether it
   * strcasecmps the same as some SQL/X keyword.
   */
  recognize_keywords = (new_mode != VAR_MODE);

  esql_yy_enter (new_mode);
}

/*
 * esql_yy_check_mode() -
 * return : void
 */
void
esql_yy_check_mode (void)
{
  if (mode_stack != NULL)
    {
      esql_yyverror
	("(esql_yy_check_mode) internal error: mode stack still full");
      exit (1);
    }
}

/*
 * esql_yy_pop_mode() -
 * return : void
 */
void
esql_yy_pop_mode (void)
{
  SCANNER_MODE_RECORD *prev;
  enum scanner_mode new_mode;

  if (mode_stack == NULL)
    {
      esql_yyverror (pp_get_msg (EX_ESQLMSCANSUPPORT_SET, MSG_EMPTY_STACK));
      exit (1);
    }

  prev = mode_stack->previous_record;
  new_mode = mode_stack->previous_mode;
  recognize_keywords = mode_stack->recognize_keywords;
  suppress_echo = mode_stack->suppress_echo;

  free_and_init (mode_stack);
  mode_stack = prev;

  esql_yy_enter (new_mode);
}

/*
 * esql_yy_mode() -
 * return :
 */
enum scanner_mode
esql_yy_mode (void)
{
  return mode;
}

/*
 * esql_yy_enter() -
 * return : void
 * new_mode(in) :
 */
void
esql_yy_enter (enum scanner_mode new_mode)
{
  static struct
  {
    const char *name;
    int mode;
    void (*echo_fn) (const char *, int);
  }
  mode_info[] =
  {
    {
    "echo", START, &echo_stream},	/* ECHO_MODE 0  */
    {
    "csql", CSQL_mode, &echo_vstr},	/* CSQL_MODE 1  */
    {
    "C", C_mode, &echo_stream},	/* C_MODE    2  */
    {
    "expr", EXPR_mode, &echo_stream},	/* EXPR_MODE 3  */
    {
    "var", VAR_mode, &echo_vstr},	/* VAR_MODE  4  */
    {
    "hostvar", HV_mode, &echo_vstr},	/* HV_MODE   5  */
    {
    "buffer", BUFFER_mode, &echo_vstr},	/* BUFFER_MODE  */
    {
    "comment", COMMENT_mode, NULL}	/* COMMENT      */
  };


  PRINT_1 ("#set mode : %s\n", mode_info[new_mode].name);
  mode = new_mode;
  if (mode_info[new_mode].echo_fn)
    echo_fn = mode_info[new_mode].echo_fn;
}

/*
 * emit_line_directive() -
 * return : void
 */
void
emit_line_directive (void)
{
  if (pp_emit_line_directives)
    {
      if (esql_yyfilename)
	fprintf (esql_yyout, "#line %d \"%s\"\n", esql_yylineno,
		 esql_yyfilename);
      else
	fprintf (esql_yyout, "#line %d\n", esql_yylineno);
    }

  need_line_directive = false;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * ignore_token() -
 * return : void
 */
static void
ignore_token (void)
{
  esql_yyverror (pp_get_msg (EX_ESQLMSCANSUPPORT_SET, MSG_NOT_PERMITTED),
		 esql_yytext);
}

/*
 * count_embedded_newlines() -
 * return : void
 */
static void
count_embedded_newlines (void)
{
  const char *p = esql_yytext;
  for (; *p; ++p)
    {
      if (*p == '\n')
	{
	  ++esql_yylineno;
	  esql_yyendcol = 0;
	}
      ++esql_yyendcol;
    }
}



/*
 * echo_string_constant() -
 * return : void
 * str(in) :
 * length(in) :
 *
 * TODO(M2) : This may be incorrect now!
 * according to ansi, there is no escape syntax for newlines!
 */
static void
echo_string_constant (const char *str, int length)
{
  const char *p;
  char *q, *result;
  int charsLeft = length;

  p = memchr (str, '\n', length);

  if (p == NULL)
    {
      ECHO_STR (str, length);
      return;
    }

  /*
   * Bad luck; there's at least one embedded newline, so we have to do
   * something to get rid of it (if it's escaped).  If it's unescaped
   * then this is a pretty weird string, but we have to let it go.
   *
   * Because we come in here with the opening quote mark still intact,
   * we're always guaranteed that there is a valid character in front
   * of the result of strchr().
   */

  result = q = malloc (length + 1);
  if (q == NULL)
    {
      esql_yyverror (pp_get_msg (EX_MISC_SET, MSG_OUT_OF_MEMORY));
      exit(1);
      return;
    }

  /*
   * Copy the spans between escaped newlines to the new buffer.  If we
   * encounter an unescaped newline we just include it.
   */
  while (p)
    {

      if (*(p - 1) == '\\')
	{
	  int span;

	  span = p - str - 1;	/* exclude size of escape char */
	  memcpy (q, str, span);
	  q += span;
	  str = p + 1;
	  length -= 2;
	  /* Exclude size of skipped escape and newline chars from string */
	  charsLeft -= (span + 2);
	}

      p = memchr (p + 1, '\n', charsLeft);
    }

  memcpy (q, str, charsLeft);

  ECHO_STR (result, length);
  free_and_init (result);
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * esql_yy_sync_lineno() -
 * return : void
 */
void
esql_yy_sync_lineno (void)
{
  need_line_directive = true;
}

/*
 * esql_yy_set_buf() -
 * return : void
 * vstr(in) :
 */
void
esql_yy_set_buf (varstring * vstr)
{
  current_buf = vstr;
}

/*
 * esql_yy_get_buf() -
 * return : void
 */
varstring *
esql_yy_get_buf (void)
{
  return current_buf;
}

/*
 * esql_yy_erase_last_token() -
 * return : void
 */
void
esql_yy_erase_last_token (void)
{
  if (current_buf && esql_yytext)
    current_buf->end -= strlen (esql_yytext);
}

/*
 * esql_yyinit() -
 * return : void
 */
void
esql_yyinit (void)
{
  /*
   * We've been burned too many times by people who can't alphabetize;
   * just sort the bloody thing once to protect ourselves from bozos.
   */
  qsort (c_keywords,
	 sizeof (c_keywords) / sizeof (c_keywords[0]),
	 sizeof (c_keywords[0]), &keyword_cmp);
  qsort (csql_keywords,
	 sizeof (csql_keywords) / sizeof (csql_keywords[0]),
	 sizeof (csql_keywords[0]), &keyword_case_cmp);

  mode_stack = NULL;
  recognize_keywords = true;
  esql_yy_enter (START);
}


char* mm_buff = NULL;
int mm_idx = 0;
int mm_size = 0;

/*
 * mm_malloc() -
 * return : char*
 * size(in) :
 */
char* 
mm_malloc (int size)
{
  char* ret;

  if (mm_buff==NULL) 
    {
      mm_size = 1024*1024;
      mm_buff = pp_malloc(mm_size);
    }

  if (mm_idx + size > mm_size)
    {
      mm_size *= 2;
      ret = pp_malloc(mm_size);
      free(mm_buff);
      mm_buff = ret;
    }

  ret = &mm_buff[mm_idx];
  mm_idx += size;

  return ret;
}


/*
 * mm_strdup() -
 * return : char*
 * str(in) :
 */
char* 
mm_strdup (char* str)
{
  const int max_len = 1024 * 1024 * 10;
  char* buff;
  int len = strnlen(str, max_len);

  if (len == max_len) 
    {
      esql_yyverror("(mm_strdup) internal error: string size is too large");
      exit (1);
    }

  buff = mm_malloc(len + 1);
  strncpy(buff, str, len);

  return buff;
}


/*
 * mm_free() -
 * return : void
 */
void
mm_free(void)
{
  if (mm_buff)
    {
      free(mm_buff);
      mm_buff = NULL;
      mm_size = 0;
      mm_idx = 0;
    }
}












