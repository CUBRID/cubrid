/* A Bison parser, made by GNU Bison 2.3.  */

/* Skeleton interface for Bison's Yacc-like parsers in C

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
     TIMELTZ = 265,
     TIMETZ = 266,
     UTIME = 267,
     TIMESTAMP = 268,
     TIMESTAMPLTZ = 269,
     TIMESTAMPTZ = 270,
     DATETIME = 271,
     DATETIMELTZ = 272,
     DATETIMETZ = 273,
     CMD_ID = 274,
     CMD_CLASS = 275,
     CMD_CONSTRUCTOR = 276,
     REF_ELO_INT = 277,
     REF_ELO_EXT = 278,
     REF_USER = 279,
     REF_CLASS = 280,
     OBJECT_REFERENCE = 281,
     OID_DELIMETER = 282,
     SET_START_BRACE = 283,
     SET_END_BRACE = 284,
     START_PAREN = 285,
     END_PAREN = 286,
     REAL_LIT = 287,
     INT_LIT = 288,
     OID_ = 289,
     TIME_LIT4 = 290,
     TIME_LIT42 = 291,
     TIME_LIT3 = 292,
     TIME_LIT31 = 293,
     TIME_LIT2 = 294,
     TIME_LIT1 = 295,
     DATE_LIT2 = 296,
     YEN_SYMBOL = 297,
     WON_SYMBOL = 298,
     BACKSLASH = 299,
     DOLLAR_SYMBOL = 300,
     TURKISH_LIRA_CURRENCY = 301,
     BRITISH_POUND_SYMBOL = 302,
     CAMBODIAN_RIEL_SYMBOL = 303,
     CHINESE_RENMINBI_SYMBOL = 304,
     INDIAN_RUPEE_SYMBOL = 305,
     RUSSIAN_RUBLE_SYMBOL = 306,
     AUSTRALIAN_DOLLAR_SYMBOL = 307,
     CANADIAN_DOLLAR_SYMBOL = 308,
     BRASILIAN_REAL_SYMBOL = 309,
     ROMANIAN_LEU_SYMBOL = 310,
     EURO_SYMBOL = 311,
     SWISS_FRANC_SYMBOL = 312,
     DANISH_KRONE_SYMBOL = 313,
     NORWEGIAN_KRONE_SYMBOL = 314,
     BULGARIAN_LEV_SYMBOL = 315,
     VIETNAMESE_DONG_SYMBOL = 316,
     CZECH_KORUNA_SYMBOL = 317,
     POLISH_ZLOTY_SYMBOL = 318,
     SWEDISH_KRONA_SYMBOL = 319,
     CROATIAN_KUNA_SYMBOL = 320,
     SERBIAN_DINAR_SYMBOL = 321,
     IDENTIFIER = 322,
     Quote = 323,
     DQuote = 324,
     NQuote = 325,
     BQuote = 326,
     XQuote = 327,
     SQS_String_Body = 328,
     DQS_String_Body = 329,
     COMMA = 330
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
#define TIMELTZ 265
#define TIMETZ 266
#define UTIME 267
#define TIMESTAMP 268
#define TIMESTAMPLTZ 269
#define TIMESTAMPTZ 270
#define DATETIME 271
#define DATETIMELTZ 272
#define DATETIMETZ 273
#define CMD_ID 274
#define CMD_CLASS 275
#define CMD_CONSTRUCTOR 276
#define REF_ELO_INT 277
#define REF_ELO_EXT 278
#define REF_USER 279
#define REF_CLASS 280
#define OBJECT_REFERENCE 281
#define OID_DELIMETER 282
#define SET_START_BRACE 283
#define SET_END_BRACE 284
#define START_PAREN 285
#define END_PAREN 286
#define REAL_LIT 287
#define INT_LIT 288
#define OID_ 289
#define TIME_LIT4 290
#define TIME_LIT42 291
#define TIME_LIT3 292
#define TIME_LIT31 293
#define TIME_LIT2 294
#define TIME_LIT1 295
#define DATE_LIT2 296
#define YEN_SYMBOL 297
#define WON_SYMBOL 298
#define BACKSLASH 299
#define DOLLAR_SYMBOL 300
#define TURKISH_LIRA_CURRENCY 301
#define BRITISH_POUND_SYMBOL 302
#define CAMBODIAN_RIEL_SYMBOL 303
#define CHINESE_RENMINBI_SYMBOL 304
#define INDIAN_RUPEE_SYMBOL 305
#define RUSSIAN_RUBLE_SYMBOL 306
#define AUSTRALIAN_DOLLAR_SYMBOL 307
#define CANADIAN_DOLLAR_SYMBOL 308
#define BRASILIAN_REAL_SYMBOL 309
#define ROMANIAN_LEU_SYMBOL 310
#define EURO_SYMBOL 311
#define SWISS_FRANC_SYMBOL 312
#define DANISH_KRONE_SYMBOL 313
#define NORWEGIAN_KRONE_SYMBOL 314
#define BULGARIAN_LEV_SYMBOL 315
#define VIETNAMESE_DONG_SYMBOL 316
#define CZECH_KORUNA_SYMBOL 317
#define POLISH_ZLOTY_SYMBOL 318
#define SWEDISH_KRONA_SYMBOL 319
#define CROATIAN_KUNA_SYMBOL 320
#define SERBIAN_DINAR_SYMBOL 321
#define IDENTIFIER 322
#define Quote 323
#define DQuote 324
#define NQuote 325
#define BQuote 326
#define XQuote 327
#define SQS_String_Body 328
#define DQS_String_Body 329
#define COMMA 330




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
/* Line 1489 of yacc.c.  */
#line 208 "../../src/executables/loader_grammar.h"
	YYSTYPE;
# define yystype YYSTYPE /* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
# define YYSTYPE_IS_TRIVIAL 1
#endif

extern YYSTYPE loader_yylval;

