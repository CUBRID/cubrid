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
/* Line 1529 of yacc.c.  */

YYSTYPE;
# define yystype YYSTYPE	/* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
# define YYSTYPE_IS_TRIVIAL 1
#endif

extern YYSTYPE loader_yylval;
