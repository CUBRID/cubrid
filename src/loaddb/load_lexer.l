/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

/*
 * load_lexer.l - loader lexer file
 */

/*** C/C++ Declarations ***/
%{
#include "load_driver.hpp"
#include "load_grammar.hpp"
#include "load_scanner.hpp"
#include "error_manager.h"
#include "error_code.h"

#undef YY_DECL
#define YY_DECL \
  int cubload::scanner::yylex (cubload::parser::semantic_type* yylval, cubload::parser::location_type* yylloc)

/* typedef to make the returns for the tokens shorter */
using token = cubload::parser::token;

/*#define LEXER_DEBUG*/

#ifdef LEXER_DEBUG
#define PRINT printf("lex: ");printf
#else
#define PRINT(a, b)
#endif

#define CHECK_NULL_AND_SET_ERROR(p)     \
  do {                                  \
    if ((p) == NULL) {                    \
        ASSERT_ERROR ();                \
        YY_FATAL_ERROR (er_msg ());     \
      }                                 \
  } while (0)                           \
%}

/*** Flex Declarations and Options ***/
/* enable c++ scanner class generation */
%option c++

/* the manual says "somewhat more optimized" */
%option batch

/* current buffer is considered never-interactive */
%option never-interactive

%option yylineno
%option nodefault
%option noyywrap
%option nounput
%option noinput
%option nounistd

%option yyclass="cubload::scanner"

%x BRACKET_ID DELIMITED_ID DQS SQS COMMENT

%%
[ \t]+	;

\r?\n {
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
    m_semantic_helper.set_in_instance_line (false);
    return token::CMD_ID;
}

\%[Cc][Ll][Aa][Ss][Ss] {
    PRINT ("CMD_CLASS %s\n", yytext);
    m_semantic_helper.set_in_instance_line (false);
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
    yylval->string = m_semantic_helper.make_string_by_yytext (yytext, yyleng);
    return token::REAL_LIT;
}

[\+\-]?[0-9]+ {
    PRINT ("INT_LIT %s\n", yytext);
    yylval->string = m_semantic_helper.make_string_by_yytext (yytext, yyleng);
    return token::INT_LIT;
}

[0-9]+: {
    PRINT ("OID %s\n", yytext);
    yylval->int_val = atoi (yytext);
    return token::OID_;
}

[0-9]+:[0-9]+:[0-9]+[\ \t]*[aApP][mM] {
    PRINT ("TIME_LIT4 %s\n", yytext);
    yylval->string = m_semantic_helper.make_string_by_yytext (yytext, yyleng);
    return token::TIME_LIT4;
}

[0-9]+:[0-9]+:[0-9]+[\ \t]* {
    PRINT ("TIME_LIT42 %s\n", yytext);
    yylval->string = m_semantic_helper.make_string_by_yytext (yytext, yyleng);
    return token::TIME_LIT42;
}

[0-9]+:[0-9]+[\ \t]*[aApP][mM] {
    PRINT ("TIME_LIT3 %s\n", yytext);
    yylval->string = m_semantic_helper.make_string_by_yytext (yytext, yyleng);
    return token::TIME_LIT3;
}

[0-9]+:[0-9]+[\ \t]* {
    PRINT ("TIME_LIT31 %s\n", yytext);
    yylval->string = m_semantic_helper.make_string_by_yytext (yytext, yyleng);
    return token::TIME_LIT31;
}

[0-9]+:[0-9]+:[0-9]+ {
    PRINT ("TIME_LIT2 %s\n", yytext);
    yylval->string = m_semantic_helper.make_string_by_yytext (yytext, yyleng);
    return token::TIME_LIT2;
}

[0-9]+:[0-9]+ {
    PRINT ("TIME_LIT1 %s\n", yytext);
    yylval->string = m_semantic_helper.make_string_by_yytext (yytext, yyleng);
    return token::TIME_LIT1;
}

[0-9]+\/[0-9]+\/[0-9]+ {
    PRINT ("DATE_LIT2 %s\n", yytext);
    yylval->string = m_semantic_helper.make_string_by_yytext (yytext, yyleng);
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
    yylval->string = m_semantic_helper.make_string_by_yytext (yytext, yyleng);
    return token::IDENTIFIER;
}

[\'] {
    PRINT ("Quote %s\n", yytext);
    BEGIN SQS;
    m_semantic_helper.set_quoted_string_buffer ();
    return token::Quote;
}

[nN][\'] {
    PRINT ("NQuote %s\n", yytext);
    BEGIN SQS;
    m_semantic_helper.set_quoted_string_buffer ();
    return token::NQuote;
}

[bB][\'] {
    PRINT ("BQuote %s\n", yytext);
    BEGIN SQS;
    m_semantic_helper.set_quoted_string_buffer ();
    return token::BQuote;
}

[xX][\'] {
    PRINT ("XQuote %s\n", yytext);
    BEGIN SQS;
    m_semantic_helper.set_quoted_string_buffer ();
    return token::XQuote;
}

\" {
    m_semantic_helper.set_quoted_string_buffer ();
    if (m_semantic_helper.in_instance_line ())
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
    m_semantic_helper.set_quoted_string_buffer ();
    BEGIN BRACKET_ID;
}

\\\n {
    /* continue line */ ;
}

"," {
    PRINT ("COMMA %s\n", yytext);
    return token::COMMA;
}

"." {
    PRINT ("DOT %s\n", yytext);
    return token::DOT;
}

\/\/[^\r\n]*\r?\n {
    /* C++ comments */
}

\-\-[^\r\n]*\r?\n {
    /* SQL comments */
}

"/*" {
    BEGIN COMMENT;	/* C comments */
}

<COMMENT>. |
<COMMENT>\n {
}

<COMMENT>"*/" {
    BEGIN INITIAL;
}

<DELIMITED_ID>\"\" {
    m_semantic_helper.append_char ('"');
}

<DELIMITED_ID>[^\"] {
    m_semantic_helper.append_char (yytext[0]);
}

<DELIMITED_ID>\" {
    m_semantic_helper.append_char ('\0');
    // PRINT ("IDENTIFIER %s\n", qstr_Buf_p);
    yylval->string = m_semantic_helper.make_string_by_buffer ();
    CHECK_NULL_AND_SET_ERROR (yylval->string);
    BEGIN INITIAL;
    return token::IDENTIFIER;
}

<BRACKET_ID>[^\]] {
    m_semantic_helper.append_char (yytext[0]);
}

<BRACKET_ID>"]" {
    m_semantic_helper.append_char ('\0');
    // PRINT ("IDENTIFIER %s\n", qstr_Buf_p);
    yylval->string = m_semantic_helper.make_string_by_buffer ();
    CHECK_NULL_AND_SET_ERROR (yylval->string);
    BEGIN INITIAL;
    return token::IDENTIFIER;
}

<DQS>\\n {
    m_semantic_helper.append_char ('\n');
}

<DQS>\\t {
    m_semantic_helper.append_char ('\t');
}

<DQS>\\f {
    m_semantic_helper.append_char ('\f');
}

<DQS>\\r {
    m_semantic_helper.append_char ('\r');
}

<DQS>\\[0-7]([0-7][0-7]?)? {
    m_semantic_helper.append_char ((char) strtol (&yytext[1], NULL, 8));
}

<DQS>\\x[0-9a-fA-F][0-9a-fA-F]? {
    m_semantic_helper.append_char ((char) strtol (&yytext[2], NULL, 16));
}

<DQS>[^\"] {
    m_semantic_helper.append_char (yytext[0]);
}

<DQS>\\ {
    /* ignore */ ;
}

<DQS>\" {
    m_semantic_helper.append_char ('\0');
    // PRINT ("DQS_String_Body %s\n", qstr_Buf_p);
    yylval->string = m_semantic_helper.make_string_by_buffer ();
    CHECK_NULL_AND_SET_ERROR (yylval->string);
    BEGIN INITIAL;
    return token::DQS_String_Body;
}

<SQS>\'\' {
    m_semantic_helper.append_char ('\'');
}

<SQS>[^\'] {
    m_semantic_helper.append_char (yytext[0]);
}

<SQS>\'\+[ \t]*\r?\n[ \t]*\' {
}

<SQS>\'[ \t] {
    m_semantic_helper.append_char ('\0');
    // PRINT ("String_Completion %s\n", qstr_Buf_p);
    yylval->string = m_semantic_helper.make_string_by_buffer ();
    CHECK_NULL_AND_SET_ERROR (yylval->string);
    BEGIN INITIAL;
    return token::SQS_String_Body;
}

<SQS>\' {
    m_semantic_helper.append_char ('\0');
    // PRINT ("String_Completion2 %s\n", qstr_Buf_p);
    yylval->string = m_semantic_helper.make_string_by_buffer ();
    CHECK_NULL_AND_SET_ERROR (yylval->string);
    BEGIN INITIAL;
    return token::SQS_String_Body;
}

%%

/*** Additional Code ***/
