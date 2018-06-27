/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

/*
 * loader_scanner.ll - loader lexer file
 */

/*** C/C++ Declarations ***/
%{
#include "loader.h"
#include "scanner.hpp"

#undef YY_DECL
#define YY_DECL                                                                     \
  int cubloaddb::scanner::yylex (cubloaddb::loader_yyparser::semantic_type* yylval, \
				 cubloaddb::loader_yyparser::location_type* yylloc)

using token = cubloaddb::loader_yyparser::token;

#define LEXER_DEBUG

#ifdef LEXER_DEBUG
#define PRINT printf("lex: ");printf
#else
#define PRINT(a, b)
#endif

static char *qstr_Buf_p = NULL;
bool loader_In_instance_line = true;
%}

/*** Flex Declarations and Options ***/
/* enable c++ scanner class generation */
%option c++

/* enable scanner to generate debug output. disable this for release versions. */
%option debug

/* the manual says "somewhat more optimized" */
%option batch

/* current buffer is considered never-interactive */
%option never-interactive

%option nodefault
%option noyywrap
%option nounput
%option noinput

%option yyclass="cubloaddb::scanner"

%x BRACKET_ID DELIMITED_ID DQS SQS COMMENT

%%
[ \t]+	;

\r?\n {
    //yylineno = loader_yyline++;
    //if (load_fail_flag)
    //  {
	//load_fail_flag = false;
	//loader_load_fail ();
      //}
    return token::NL;
}

[Nn][Uu][Ll][Ll] {
    PRINT ("NULL_ %s\n", yytext);
    return token::NULL_;
}

[Cc][Ll][Aa][Ss][Ss] {
    PRINT ("CLASS %s\n", yytext);
    return token::CLASS;
}

[Ss][Hh][Aa][Rr][Ee][Dd] {
    PRINT ("SHARED %s\n", yytext);
    return token::SHARED;
}

[Dd][Ee][Ff][Aa][Uu][Ll][Tt] {
    PRINT ("DEFAULT%s\n", yytext);
    return token::DEFAULT;
}

[Dd][Aa][Tt][Ee] {
    PRINT ("DATE %s\n", yytext);
    return token::DATE_;
}

[Tt][Ii][Mm][Ee] {
    PRINT ("TIME %s\n", yytext);
    return token::TIME;
}

[Uu][Tt][Ii][Mm][Ee] {
    PRINT ("UTIME %s\n", yytext);
    return token::UTIME;
}

[Tt][Ii][Mm][Ee][Ss][Tt][Aa][Mm][Pp] {
    PRINT ("TIMESTAMP %s\n", yytext);
    return token::TIMESTAMP;
}

[Tt][Ii][Mm][Ee][Ss][Tt][Aa][Mm][Pp][lL][tT][zZ] {
    PRINT ("TIMESTAMPLTZ %s\n", yytext);
    return token::TIMESTAMPLTZ;
}

[Tt][Ii][Mm][Ee][Ss][Tt][Aa][Mm][Pp][tT][zZ] {
    PRINT ("TIMESTAMPTZ %s\n", yytext);
    return token::TIMESTAMPTZ;
}

[Dd][Aa][Tt][Ee][Tt][Ii][Mm][Ee] {
    PRINT ("DATETIME %s\n", yytext);
    return token::DATETIME;
}

[Dd][Aa][Tt][Ee][Tt][Ii][Mm][Ee][lL][tT][zZ] {
    PRINT ("DATETIMELTZ %s\n", yytext);
    return token::DATETIMELTZ;
}

[Dd][Aa][Tt][Ee][Tt][Ii][Mm][Ee][tT][zZ] {
    PRINT ("DATETIMETZ %s\n", yytext);
    return token::DATETIMETZ;
}

\%[Ii][Dd] {
    PRINT ("CMD_ID %s\n", yytext);
    //loader_In_instance_line = false;
    return token::CMD_ID;
}

\%[Cc][Ll][Aa][Ss][Ss] {
    PRINT ("CMD_CLASS %s\n", yytext);
    //loader_In_instance_line = false;
    return token::CMD_CLASS;
}

\%[Cc][Oo][Nn][Ss][Tt][Rr][Uu][Cc][Tt][Oo][Rr] {
    PRINT ("CMD_CONSTRUCTOR %s\n", yytext);
    return token::CMD_CONSTRUCTOR;
}

\^[Ii] {
    PRINT ("REF_ELO_INT %s\n", yytext);
    return token::REF_ELO_INT;
}

\^[Ee] {
    PRINT ("REF_ELO_EXT %s\n", yytext);
    return token::REF_ELO_EXT;
}

\^[Uu] {
    PRINT ("REF_USER %s\n", yytext);
    return token::REF_USER;
}

\^[Cc] {
    PRINT ("REF_CLASS %s\n", yytext);
    return token::REF_CLASS;
}

\@ {
    PRINT ("OBJECT_REFERENCE %s\n", yytext);
    return token::OBJECT_REFERENCE;
}

\| {
    PRINT ("OID_DELIMETER %s\n", yytext);
    return token::OID_DELIMETER;
}

\{ {
    PRINT ("SET_START_BRACE %s\n", yytext);
    return token::SET_START_BRACE;
}

\} {
    PRINT ("SET_END_BRACE %s\n", yytext);
    return token::SET_END_BRACE;
}

\( {
    PRINT ("START_PAREN %s\n", yytext);
    return token::START_PAREN;
}

\) {
    PRINT ("END_PAREN %s\n", yytext);
    return token::END_PAREN;
}

[\+\-]?(([0-9]+[Ee][\+\-]?[0-9]+[fFlL]?)|([0-9]*\.[0-9]+([Ee][\+\-]?[0-9]+)?[fFlL]?)|([0-9]+\.[0-9]*([Ee][\+\-]?[0-9]+)?[fFlL]?)) {
    PRINT ("REAL_LIT %s\n", yytext);
    //yylval_param->string = loader_make_string_by_yytext (yyscanner);
    return token::REAL_LIT;
}

[\+\-]?[0-9]+ {
    PRINT ("INT_LIT %s\n", yytext);
    //yylval_param->string = loader_make_string_by_yytext (yyscanner);
    return token::INT_LIT;
}

[0-9]+: {
    PRINT ("OID %s\n", yytext);
    //yylval_param->intval = atoi (yytext);
    return token::OID_;
}

[0-9]+:[0-9]+:[0-9]+[\ \t]*[aApP][mM] {
    PRINT ("TIME_LIT4 %s\n", yytext);
    //yylval_param->string = loader_make_string_by_yytext (yyscanner);
    return token::TIME_LIT4;
}

[0-9]+:[0-9]+:[0-9]+[\ \t]* {
    PRINT ("TIME_LIT42 %s\n", yytext);
    //yylval_param->string = loader_make_string_by_yytext (yyscanner);
    return token::TIME_LIT42;
}

[0-9]+:[0-9]+[\ \t]*[aApP][mM] {
    PRINT ("TIME_LIT3 %s\n", yytext);
    //yylval_param->string = loader_make_string_by_yytext (yyscanner);
    return token::TIME_LIT3;
}

[0-9]+:[0-9]+[\ \t]* {
    PRINT ("TIME_LIT31 %s\n", yytext);
    //yylval_param->string = loader_make_string_by_yytext (yyscanner);
    return token::TIME_LIT31;
}

[0-9]+:[0-9]+:[0-9]+ {
    PRINT ("TIME_LIT2 %s\n", yytext);
    //yylval_param->string = loader_make_string_by_yytext (yyscanner);
    return token::TIME_LIT2;
}

[0-9]+:[0-9]+ {
    PRINT ("TIME_LIT1 %s\n", yytext);
    //yylval_param->string = loader_make_string_by_yytext (yyscanner);
    return token::TIME_LIT1;
}

[0-9]+\/[0-9]+\/[0-9]+ {
    PRINT ("DATE_LIT2 %s\n", yytext);
    //yylval_param->string = loader_make_string_by_yytext (yyscanner);
    return token::DATE_LIT2;
}

\xa1\xef {
    PRINT ("YEN_SYMBOL %s\n", yytext);
    return token::YEN_SYMBOL;
}

\\[J][P][Y] {
    PRINT ("YEN_SYMBOL %s\n", yytext);
    return token::YEN_SYMBOL;
}

\\[K][R][W] {
    PRINT ("WON_SYMBOL %s\n", yytext);
    return token::WON_SYMBOL;
}

\xa3\xdc {
    PRINT ("WON_SYMBOL %s\n", yytext);
    return token::WON_SYMBOL;
}

\\[T][L] {
    PRINT ("TURKISH_LIRA_CURRENCY %s\n", yytext);
    return token::TURKISH_LIRA_CURRENCY;
}

\\[T][R][Y] {
    PRINT ("TURKISH_LIRA_CURRENCY %s\n", yytext);
    return token::TURKISH_LIRA_CURRENCY;
}

\\[G][B][P] {
    PRINT ("BRITISH_POUND_SYMBOL %s\n", yytext);
    return token::BRITISH_POUND_SYMBOL;
}

\\[K][H][R] {
    PRINT ("CAMBODIAN_RIEL_SYMBOL %s\n", yytext);
    return token::CAMBODIAN_RIEL_SYMBOL;
}

\\[C][N][Y] {
    PRINT ("CHINESE_RENMINBI_SYMBOL %s\n", yytext);
    return token::CHINESE_RENMINBI_SYMBOL;
}

\\[I][N][R] {
    PRINT ("INDIAN_RUPEE_SYMBOL %s\n", yytext);
    return token::INDIAN_RUPEE_SYMBOL;
}

\\[R][U][B] {
    PRINT ("RUSSIAN_RUBLE_SYMBOL %s\n", yytext);
    return token::RUSSIAN_RUBLE_SYMBOL;
}

\\[A][U][D] {
    PRINT ("AUSTRALIAN_DOLLAR_SYMBOL %s\n", yytext);
    return token::AUSTRALIAN_DOLLAR_SYMBOL;
}

\\[C][A][D] {
    PRINT ("CANADIAN_DOLLAR_SYMBOL %s\n", yytext);
    return token::CANADIAN_DOLLAR_SYMBOL;
}

\\[B][R][L] {
    PRINT ("BRASILIAN_REAL_SYMBOL %s\n", yytext);
    return token::BRASILIAN_REAL_SYMBOL;
}

\\[R][O][N] {
    PRINT ("ROMANIAN_LEU_SYMBOL %s\n", yytext);
    return token::ROMANIAN_LEU_SYMBOL;
}

\\[E][U][R] {
    PRINT ("EURO_SYMBOL %s\n", yytext);
    return token::EURO_SYMBOL;
}

\\[C][H][F] {
    PRINT ("SWISS_FRANC_SYMBOL %s\n", yytext);
    return token::SWISS_FRANC_SYMBOL;
}

\\[D][K][K] {
    PRINT ("DANISH_KRONE_SYMBOL %s\n", yytext);
    return token::DANISH_KRONE_SYMBOL;
}

\\[N][O][K] {
    PRINT ("NORWEGIAN_KRONE_SYMBOL %s\n", yytext);
    return token::NORWEGIAN_KRONE_SYMBOL;
}

\\[B][G][N] {
    PRINT ("BULGARIAN_LEV_SYMBOL %s\n", yytext);
    return token::BULGARIAN_LEV_SYMBOL;
}

\\[V][N][D] {
    PRINT ("VIETNAMESE_DONG_SYMBOL %s\n", yytext);
    return token::VIETNAMESE_DONG_SYMBOL;
}

\\[C][Z][K] {
    PRINT ("CZECH_KORUNA_SYMBOL %s\n", yytext);
    return token::CZECH_KORUNA_SYMBOL;
}

\\[P][L][N] {
    PRINT ("POLISH_ZLOTY_SYMBOL %s\n", yytext);
    return token::POLISH_ZLOTY_SYMBOL;
}

\\[S][E][K] {
    PRINT ("SWEDISH_KRONA_SYMBOL %s\n", yytext);
    return token::SWEDISH_KRONA_SYMBOL;
}

\\[H][R][K] {
    PRINT ("CROATIAN_KUNA_SYMBOL %s\n", yytext);
    return token::CROATIAN_KUNA_SYMBOL;
}

\\[R][S][D] {
    PRINT ("SERBIAN_DINAR_SYMBOL %s\n", yytext);
    return token::SERBIAN_DINAR_SYMBOL;
}

\\ {
    PRINT ("BACKSLASH %s\n", yytext);
    return token::BACKSLASH;
}

\$ {
    PRINT ("DOLLAR_SYMBOL %s\n", yytext);
    return token::DOLLAR_SYMBOL;
}

\\[U][S][D] {
    PRINT ("DOLLAR_SYMBOL %s\n", yytext);
    return token::DOLLAR_SYMBOL;
}

([a-zA-Z_%#]|(\xa1[\xa2-\xee\xf3-\xfe])|([\xa2-\xfe][\xa1-\xfe])|(\x8e[\xa1-\xfe]))([a-zA-Z_%#0-9]|(\xa1[\xa2-\xfe])|([\xa1-\xfe])|(\x8e[\xa1-\xfe]))* {
    PRINT ("IDENTIFIER %s\n", yytext);
    //yylval_param->string = loader_make_string_by_yytext (yyscanner);
    return token::IDENTIFIER;
}

[\'] {
    PRINT ("Quote %s\n", yytext);
    BEGIN SQS;
    //loader_set_quoted_string_buffer (yyscanner);
    return token::Quote;
}

[nN][\'] {
    PRINT ("NQuote %s\n", yytext);
    BEGIN SQS;
    //loader_set_quoted_string_buffer (yyscanner);
    return token::NQuote;
}

[bB][\'] {
    PRINT ("BQuote %s\n", yytext);
    BEGIN SQS;
    //loader_set_quoted_string_buffer (yyscanner);
    return token::BQuote;
}

[xX][\'] {
    PRINT ("XQuote %s\n", yytext);
    BEGIN SQS;
    //loader_set_quoted_string_buffer (yyscanner);
    return token::XQuote;
}

\" {
    //loader_set_quoted_string_buffer (yyscanner);
    if (/*loader_In_instance_line == */true)
      {
	BEGIN DQS;
	return token::DQuote;
      }
    else
      {
	BEGIN DELIMITED_ID;
      }
}

"[" {
    //loader_set_quoted_string_buffer (yyscanner);
    BEGIN BRACKET_ID;
}

\\\n {
    //yylineno = loader_yyline++;
    /* continue line */ ;
}

"," {
    PRINT ("COMMA %s\n", yytext);
    return token::COMMA;
}

\/\/[^\r\n]*\r?\n {
    //yylineno = loader_yyline++;
    /* C++ comments */
}

\-\-[^\r\n]*\r?\n {
    //yylineno = loader_yyline++;
    /* SQL comments */
}

"/*" {
    BEGIN COMMENT;	/* C comments */
}

<COMMENT>.  |
<COMMENT>\n {
    //yylineno = loader_yyline++;
}

<COMMENT>"*/" {
    BEGIN INITIAL;
}

<DELIMITED_ID>\"\" {
    //loader_append_string ('"', yyscanner);
}

<DELIMITED_ID>[^\"] {
    //loader_append_string (yytext[0], yyscanner);
}

<DELIMITED_ID>\" {
    //loader_append_string ('\0', yyscanner);
    PRINT ("IDENTIFIER %s\n", qstr_Buf_p);
    //yylval_param->string = loader_make_string_by_buffer (yyscanner);
    BEGIN INITIAL;
    return token::IDENTIFIER;
}

<BRACKET_ID>[^\]] {
    //loader_append_string (yytext[0], yyscanner);
}

<BRACKET_ID>"]" {
    //loader_append_string ('\0', yyscanner);
    PRINT ("IDENTIFIER %s\n", qstr_Buf_p);
    //yylval_param->string = loader_make_string_by_buffer (yyscanner);
    BEGIN INITIAL;
    return token::IDENTIFIER;
}

<DQS>\\n {
    //loader_append_string ('\n', yyscanner);
}

<DQS>\\t {
    //loader_append_string ('\t', yyscanner);
}

<DQS>\\f {
    //loader_append_string ('\f', yyscanner);
}

<DQS>\\r {
    //loader_append_string ('\r', yyscanner);
}

<DQS>\\[0-7]([0-7][0-7]?)?  {
    //loader_append_string ((char) strtol (&yytext[1], NULL, 8), yyscanner);
}

<DQS>\\x[0-9a-fA-F][0-9a-fA-F]?  {
    //loader_append_string ((char) strtol (&yytext[2], NULL, 16), yyscanner);
}

<DQS>[^\"] {
    //loader_append_string (yytext[0], yyscanner);
}

<DQS>\\ {
    /* ignore */ ;
}

<DQS>\" {
    //loader_append_string ('\0', yyscanner);
    PRINT ("DQS_String_Body %s\n", qstr_Buf_p);
    //yylval_param->string = loader_make_string_by_buffer (yyscanner);
    BEGIN INITIAL;
    return token::DQS_String_Body;
}

<SQS>\'\' {
    //loader_append_string ('\'', yyscanner);
}

<SQS>[^\'] {
    //loader_append_string (yytext[0], yyscanner);
}

<SQS>\'\+[ \t]*\r?\n[ \t]*\' {
    //yylineno = loader_yyline++;
}

<SQS>\'[ \t] {
    //loader_append_string ('\0', yyscanner);
    PRINT ("String_Completion %s\n", qstr_Buf_p);
    //yylval_param->string = loader_make_string_by_buffer (yyscanner);
    BEGIN INITIAL;
    return token::SQS_String_Body;
}

<SQS>\' {
    //loader_append_string ('\0', yyscanner);
    PRINT ("String_Completion2 %s\n", qstr_Buf_p);
    //yylval_param->string = loader_make_string_by_buffer (yyscanner);
    BEGIN INITIAL;
    return token::SQS_String_Body;
}

%%

/*** Additional Code ***/

void
loader_reset_string_pool (void)
{
}

void
loader_initialize_lexer (void)
{
}