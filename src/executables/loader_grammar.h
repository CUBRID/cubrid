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
#define IDENTIFIER 295
#define Quote 296
#define DQuote 297
#define NQuote 298
#define BQuote 299
#define XQuote 300
#define SQS_String_Body 301
#define DQS_String_Body 302
#define COMMA 303




#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
typedef union YYSTYPE
#line 77 "../../src/executables/loader_grammar.y"
{
  int intval;
  LDR_STRING *string;
  LDR_CLASS_COMMAND_SPEC *cmd_spec;
  LDR_CONSTRUCTOR_SPEC *ctor_spec;
  LDR_CONSTANT *constant;
  LDR_OBJECT_REF *obj_ref;
}
/* Line 1529 of yacc.c.  */
#line 154 "../../src/executables/loader_grammar.h"
YYSTYPE;
# define yystype YYSTYPE	/* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
# define YYSTYPE_IS_TRIVIAL 1
#endif

extern YYSTYPE loader_yylval;
