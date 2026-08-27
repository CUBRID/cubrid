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

/* TODO: 
 * For now, I've set it to ignore warnings in the automatically generated code. 
 * If possible, let's remove warnings normally instead of setting it to ignore. */
#if defined(__GNUC__) || defined(__clang__)
  #define BEGIN_SUPPRESS_WARNING_BISON_FLEX             \
    _Pragma("GCC diagnostic push")                      \
    _Pragma("GCC diagnostic ignored \"-Wsign-compare\"")\
    _Pragma("GCC diagnostic ignored \"-Wregister\"")    \
    _Pragma("GCC diagnostic ignored \"-Wimplicit-fallthrough=\"")

  #define END_SUPPRESS_WARNING_BISON_FLEX               \
    _Pragma("GCC diagnostic pop")
#else
  #define BEGIN_SUPPRESS_WARNING_BISON_FLEX
  #define END_SUPPRESS_WARNING_BISON_FLEX
#endif

BEGIN_SUPPRESS_WARNING_BISON_FLEX
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

%x BRACKET_ID DELIMITED_ID DQS SQS COMMENT FORMAT_ARG BINARY_MODE BINARY_BLOCK ABORTED

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

\%[Ff][Oo][Rr][Mm][Aa][Tt] {
    PRINT ("CMD_FORMAT %s\n", yytext);
    m_semantic_helper.set_in_instance_line (false);
    /* the argument of the command is scanned as FORMAT_TYPE, see the FORMAT_ARG rules below */
    BEGIN FORMAT_ARG;
    return token::CMD_FORMAT;
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

[\+\-]?(([0-9]+[Ee][\+\-]?[0-9]+[.fFlL]?)|([0-9]*\.[0-9]+([Ee][\+\-]?[0-9]+)?[fFlL]?)|([0-9]+\.[0-9]*([Ee][\+\-]?[0-9]+)?[fFlL]?)) {
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


  /*
   * The arguments of the "%format" command are unsigned integers, while a plain [0-9]+ rule in the
   * initial start condition would collide with the INT_LIT rule above, which matches the very same
   * text with the same length. They are therefore scanned in a start condition of their own, which
   * is left when the command line ends.
   */
<FORMAT_ARG>[ \t]+ ;

<FORMAT_ARG>[0-9]+ {
    PRINT ("FORMAT_ARG_LIT %s\n", yytext);
    yylval->int_val = atoi (yytext);
    return token::FORMAT_ARG_LIT;
}

<FORMAT_ARG>\r?\n {
    BEGIN INITIAL;
    return token::NL;
}

<FORMAT_ARG>(.|\n) {
    m_error_handler.on_failure_with_line (LOADDB_MSG_INVALID_FORMAT_COMMAND);
    BEGIN ABORTED;
}

  /*
   * Binary mode is entered from the grammar, see format_command in load_grammar.yy, at the moment
   * the "%format <n>" command line has been reduced. The new line which terminates that command
   * line has not been consumed yet, so it is delivered from here before the raw section starts.
   */
<BINARY_MODE>[ \t]+ ;

<BINARY_MODE>\r?\n {
    PRINT ("BINARY_MODE begin %s\n", "");
    BEGIN BINARY_BLOCK;
    return token::NL;
}

<BINARY_MODE>(.|\n) {
    m_error_handler.on_failure_with_line (LOADDB_MSG_INVALID_FORMAT_COMMAND);
    BEGIN ABORTED;
}

  /*
   * A binary section is written by unloaddb as:
   *
   *   %format <n> '\n'
   *   <uint32_t rec_size>        <- host byte order, size of one row, size field excluded
   *   <row bytes ...>            <- per attribute [varint (size + 1)][db type][raw bytes], where
   *                                 a size of zero, a single 0x00 byte, stands for a NULL value
   *                                 and carries no type byte of its own
   *   ... more rows ...
   *   <uint32_t 0>               <- BIN_END_OF_RECORDS, a row is never of size zero
   *   '\n'                       <- ends the section line
   *
   * See process_object () in src/executables/unload_object.c and binary_print () and
   * binary_reserve_row_size () in src/executables/unload_object_file.c
   */
<BINARY_BLOCK>(.|\n){4} {
    BIN_REC_SIZE_TYPE rec_size;
    int k;

    /*
     * The size field is raw data and not text, but %option yylineno has already counted every
     * 0x0A among these bytes. Undo that, a binary section has no lines and one row is counted
     * as one line below instead.
     */
    for (k = 0; k < yyleng; k++)
      {
	if (yytext[k] == '\n')
	  {
	    yylineno--;
	  }
      }

    /*
     * unloaddb memcpy-es the size field as is, no byte order conversion is applied. The value
     * payloads are raw host memory as well, so an object file in binary format is not portable
     * between architectures of different endianness.
     */
    memcpy (&rec_size, yytext, sizeof (rec_size));

    if (rec_size == BIN_END_OF_RECORDS)
      {
	/* the section is over, the new line which follows it is scanned in INITIAL */
	PRINT ("BINARY_BLOCK end %s\n", "");
	BEGIN INITIAL;
      }
    else
      {
	/* make_string_by_reserve_size () either hands out a pooled entry or throws, it never fails */
	string_type *row = m_semantic_helper.make_string_by_reserve_size (rec_size);

	if (read_binary (row->val, rec_size) != rec_size)
	  {
	    m_error_handler.on_failure_with_line (LOADDB_MSG_TRUNCATED_BINARY_SECTION);
	    BEGIN ABORTED;
	  }
	else
	  {
	    yylineno++;
	    yylval->string = row;
	    return token::BINARY_ROW;
	  }
      }
}

<BINARY_BLOCK>(.|\n) {
    /* less than the four bytes of a record size field are left, the object file is truncated */
    m_error_handler.on_failure_with_line (LOADDB_MSG_TRUNCATED_BINARY_SECTION);
    BEGIN ABORTED;
}

  /*
   * Entered once an object file has been found unreadable and the failure has been reported. The
   * rest of the input is raw data the text rules cannot match, scanning it would report a lexical
   * error per byte on top of the failure which is already known, so it is dropped instead.
   *
   * A new line is handed to the grammar so that the line the "%format" command line opened is
   * closed and the parse ends without a syntax error of its own. Scanning goes on in INITIAL,
   * where the end of the input terminates it.
   */
<ABORTED>(.|\n) {
    char scrap[4096];

    while (read_binary (scrap, sizeof (scrap)) == sizeof (scrap))
      {
	;
      }

    PRINT ("ABORTED %s\n", "");
    BEGIN INITIAL;
    return token::NL;
}

<ABORTED><<EOF>> {
    BEGIN INITIAL;
    return token::NL;
}

<BINARY_BLOCK><<EOF>> {
    /*
     * The input ended on a record boundary, which is what a batch cut in the middle of a section
     * looks like, see split () in load_common.cpp. The end of records marker and the new line
     * which closes the section stayed in a later batch, so the line the rows of this one make up
     * is closed from here. Scanning goes on in INITIAL, where the end of input terminates it.
     */
    PRINT ("BINARY_BLOCK batch end %s\n", "");
    BEGIN INITIAL;
    return token::NL;
}

%%

/*** Additional Code ***/
END_SUPPRESS_WARNING_BISON_FLEX

/*
 * The BINARY_BLOCK rules match the record size field with a fixed (.|\n){4} repetition, which
 * flex resolves at scanner generation time and cannot derive from a sizeof expression.
 */
static_assert (sizeof (BIN_REC_SIZE_TYPE) == 4, "the BINARY_BLOCK rule expects a four byte record size field");

/*
 * read_binary () - copy raw bytes of a binary section out of the scanner input
 *   return: number of bytes copied
 *   buf (out): destination
 *   len (in): number of bytes to copy
 *
 * Note: the bytes of a binary section carry no structure a flex rule could match, so they are
 *       taken straight out of the scanner buffer. yyinput () reads the very same buffer one byte
 *       at a time, here it only refills an exhausted buffer and reports the end of the input.
 *
 *       The buffer members touched below, along with the null termination dance around
 *       yy_hold_char, are the ones yyinput () itself maintains. They are internals of the
 *       generated scanner, which is why every access to them is confined to this function.
 */
size_t
cubload::scanner::read_binary (char *buf, size_t len)
{
  size_t read_bytes = 0;

  while (read_bytes < len)
    {
      size_t available;
      int c;

      /* yytext is kept null terminated inside the buffer, put the overwritten character back */
      *yy_c_buf_p = yy_hold_char;

      available = (size_t) ((YY_CURRENT_BUFFER_LVALUE->yy_ch_buf + yy_n_chars) - yy_c_buf_p);
      if (available > len - read_bytes)
	{
	  available = len - read_bytes;
	}

      if (available > 0)
	{
	  memcpy (buf + read_bytes, yy_c_buf_p, available);
	  yy_c_buf_p += available;
	  read_bytes += available;
	}

      /* restore the null termination the rule machinery expects to find */
      yy_hold_char = *yy_c_buf_p;
      *yy_c_buf_p = '\0';

      if (available > 0)
	{
	  continue;
	}

      /*
       * The buffer ran dry. 
       * yyinput () refills it and hands back the first byte of the new content, or 0 at the end of the input.
       * A 0 is also a genuine NUL byte, the two are told apart by the buffer holding data again afterwards.
       */
      c = yyinput ();
      if (c == 0 && yy_c_buf_p >= YY_CURRENT_BUFFER_LVALUE->yy_ch_buf + yy_n_chars)
	{
	  /* end of the input, the caller reports the short count as a truncated section */
	  break;
	}

      buf[read_bytes++] = (char) c;
    }

  return read_bytes;
}

/*
 * set_ldr_data_format () - select the data format of the class being scanned
 *   return : void
 *   data_format (in): data format argument of the "%format" command line
 *   compat_level (in): disk image compatibility level the object file was written with
 *   bit_platform (in): word size the object file was written with
 *   endian (in): byte order the object file was written with
 *
 * Note: the three arguments which follow the data format describe the memory image the payloads
 *       of a binary section were copied from, they are meaningless for a text format.
 *
 * Note: BEGIN expands to an assignment to yy_start, a protected member of yyFlexLexer.
 *       That is why this function has to be defined here, as a member of cubload::scanner.
 */
void
cubload::scanner::set_ldr_data_format (int data_format, int compat_level, int bit_platform, int endian)
{
  if (data_format != OBJECT_FORMAT_TEXT
      && (compat_level != binary_format_compat_level () || bit_platform != binary_format_bit_platform ()
	  || endian != binary_format_endian ()))
    {
      /*
       * A binary section holds raw memory of the DB_VALUE payloads, a build which lays them out
       * differently cannot read it back.
       */
      m_data_format = OBJECT_FORMAT_TEXT;
      BEGIN ABORTED;

      m_error_handler.on_failure_with_line (LOADDB_MSG_INCOMPATIBLE_BINARY_FORMAT, compat_level, bit_platform,
					    endian, binary_format_compat_level (), binary_format_bit_platform (),
					    binary_format_endian ());
      return;
    }

  switch (data_format)
    {
    case OBJECT_FORMAT_BINARY:
      //case OBJECT_FORMAT_BINARY_COMPRESSED:
      m_data_format = (OBJECT_DATA_FORMAT) data_format;
      BEGIN BINARY_MODE;
      break;

    case OBJECT_FORMAT_TEXT:
      m_data_format = (OBJECT_DATA_FORMAT) data_format;
      BEGIN INITIAL;
      break;

    default:
      /*
       * A data format this build does not know about. Reported through the error handler rather
       * than through YY_FATAL_ERROR, whose message text LexerError () discards.
       */
      m_data_format = OBJECT_FORMAT_TEXT;
      BEGIN ABORTED;

      m_error_handler.on_failure_with_line (LOADDB_MSG_UNSUPPORTED_DATA_FORMAT, data_format);
      break;
    }
}
