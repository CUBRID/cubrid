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

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>

#include "lz4.h"

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
    _Pragma("GCC diagnostic ignored \"-Wimplicit-fallthrough\"")

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
    /*
     * A block of a compressed section holds many rows and is read in one go, see the BINARY_BLOCK rules below. Its
     * rows are handed over one per call from here, before anything else is scanned, as the rows of an uncompressed
     * section are one per match. Code in front of the first rule runs on every entry into yylex ().
     */
    if (m_block_rows_left > 0)
      {
	next_block_row (yylval);
	return token::BINARY_ROW;
      }

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
    /* the arguments of the command are scanned in FORMAT_ARG, see the rules below */
    m_format_arg_count = 0;
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
   *
   * A command line that is not "%format" and FORMAT_ARG_COUNT integers is reported here, as
   * LOADDB_MSG_INVALID_FORMAT_COMMAND, and FORMAT_ERROR is handed to the grammar in place of the
   * token it could not make. The grammar takes that token as the end of the command, see
   * format_command in load_grammar.yy, so the failure is not reported a second time as a syntax
   * error. Scanning goes on in ABORTED, which drops the binary section behind the command.
   *
   * A valid command line never reaches the new line rule below: the grammar reduces format_command
   * on its last argument and set_ldr_data_format () leaves FORMAT_ARG, so the new line is scanned
   * in the start condition it selects.
   */
<FORMAT_ARG>[ \t]+ ;

<FORMAT_ARG>[0-9]+ {
    PRINT ("FORMAT_ARG_LIT %s\n", yytext);
    char *end_ptr = NULL;
    long value;

    errno = 0;
    value = strtol (yytext, &end_ptr, 10);
    if (errno == ERANGE || value > INT_MAX || *end_ptr != '\0' || ++m_format_arg_count > FORMAT_ARG_COUNT)
      {
        /* atoi () would have handed over whatever an overflowing number wraps to */
        fail_binary_section (LOADDB_MSG_INVALID_FORMAT_COMMAND);
        BEGIN ABORTED;
        return token::FORMAT_ERROR;
      }

    yylval->int_val = (int) value;
    return token::FORMAT_ARG_LIT;
}

<FORMAT_ARG>\r?\n {
    /* fewer arguments than the command takes */
    fail_binary_section (LOADDB_MSG_INVALID_FORMAT_COMMAND);
    BEGIN ABORTED;
    return token::FORMAT_ERROR;
}

<FORMAT_ARG>(.|\n) {
    /* not an unsigned integer */
    fail_binary_section (LOADDB_MSG_INVALID_FORMAT_COMMAND);
    BEGIN ABORTED;
    return token::FORMAT_ERROR;
}

  /*
   * Binary mode is entered from the grammar, see format_command in load_grammar.yy, at the moment
   * the "%format <n>" command line has been reduced. The new line which terminates that command
   * line has not been consumed yet, so it is delivered from here before the raw section starts.
   */
<BINARY_MODE>[ \t]+ ;

<BINARY_MODE>\r?\n {
    PRINT ("BINARY_MODE begin %s\n", "");
    /*
     * Each row of the section counts as one line and the end of records marker, together with the
     * new line which follows it, as one more; split () in load_common.cpp counts the same way, so
     * the line a row is reported at is the same in SA and CS mode. The rows are not lines of the
     * file, so the new line of this command line is not counted here: the BINARY_BLOCK rule counts
     * a row when it hands it over, which makes the first row the line after this one.
     */
    yylineno--;
    BEGIN BINARY_BLOCK;
    return token::NL;
}

<BINARY_MODE>(.|\n) {
    fail_binary_section (LOADDB_MSG_INVALID_FORMAT_COMMAND);
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
   *
   * A section of OBJECT_FORMAT_BINARY_COMPRESSED has blocks of such rows in place of the rows, and the
   * four bytes matched here are the stored size of a block, see BIN_BLOCK_HEADER_SIZE. The end of
   * records marker is the same.
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
	/*
	 * The section is over, the new line which follows it is scanned in INITIAL. The last row ends
	 * here, so the marker is on the next line, and that new line ends the marker's own.
	 */
	PRINT ("BINARY_BLOCK end %s\n", "");
	yylineno++;
	BEGIN INITIAL;
      }
    else if (m_data_format == OBJECT_FORMAT_BINARY_COMPRESSED)
      {
	if (read_binary_block (rec_size))
	  {
	    /* the first row of the block, the others follow from the top of the rules */
	    next_block_row (yylval);
	    return token::BINARY_ROW;
	  }
	/* reported by read_binary_block (), scanning goes on in ABORTED */
      }
    else
      {
	/* make_string_by_reserve_size () either hands out a pooled entry or throws, it never fails */
	string_type *row = m_semantic_helper.make_string_by_reserve_size (rec_size);

	if (read_binary (row->val, rec_size) != rec_size)
	  {
	    /* the row being read is counted when it is handed over, so it is on the line after yylineno */
	    fail_binary_section_at (yylineno + 1, LOADDB_MSG_TRUNCATED_BINARY_SECTION);
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
    /*
     * Less than the four bytes of a record size field are left, the object file is truncated. The
     * record that broke off would have been on the line after yylineno.
     */
    fail_binary_section_at (yylineno + 1, LOADDB_MSG_TRUNCATED_BINARY_SECTION);
    BEGIN ABORTED;
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

    clear_binary_block ();

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

%%

/*** Additional Code ***/
END_SUPPRESS_WARNING_BISON_FLEX

/*
 * The BINARY_BLOCK rules match the record size field with a fixed (.|\n){4} repetition, which
 * flex resolves at scanner generation time and cannot derive from a sizeof expression.
 */
static_assert (sizeof (BIN_REC_SIZE_TYPE) == 4, "the BINARY_BLOCK rule expects a four byte record size field");
static_assert (sizeof (BIN_BLOCK_SIZE_TYPE) == sizeof (BIN_REC_SIZE_TYPE),
	       "the BINARY_BLOCK rule matches the stored size of a block as it matches a record size field");
static_assert (BIN_MAX_BLOCK_RAW_SIZE == LZ4_MAX_INPUT_SIZE, "a raw block is at most what LZ4 takes in");

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
 * read_binary_block () - read a block of a compressed binary section, see BIN_BLOCK_HEADER_SIZE
 *   return: true when the block is in m_block and its rows are ready to be handed over, false when the
 *           section has been reported unreadable and scanning goes on in ABORTED
 *   stored_size (in): stored size of the block, the field the BINARY_BLOCK rule has matched
 *
 * Note: the raw bytes have to come to the checksum of the header, when it has one, see binary_block_checksum ().
 *       The rows of the block are all checked before the first one is handed over: each row size field
 *       has to frame a row inside the block, and they have to come to the row count of the header and to
 *       the end of the block exactly. A block that fails is not a row loaddb could skip and go on with
 *       the next, its rows cannot be told apart, so the section is failed as a truncated one is.
 */
bool
cubload::scanner::read_binary_block (BIN_BLOCK_SIZE_TYPE stored_size)
{
  BIN_BLOCK_SIZE_TYPE header[3];	/* raw size, row count, checksum */
  BIN_BLOCK_SIZE_TYPE raw_size, rows, checksum, found_rows = 0;
  std::size_t pos = 0;
  /* the rows of the block are counted as they are handed over, the first one is on the line after yylineno */
  int lineno = yylineno + 1;

  static_assert (BIN_BLOCK_HEADER_SIZE == sizeof (stored_size) + sizeof (header), "the header of a block");

  if (read_binary ((char *) header, sizeof (header)) != sizeof (header))
    {
      fail_binary_section_at (lineno, LOADDB_MSG_TRUNCATED_BINARY_SECTION);
      BEGIN ABORTED;
      return false;
    }

  raw_size = header[0];
  rows = header[1];
  checksum = header[2];

  /* a block is never empty and is never stored larger than its raw bytes, see binary_compress_output () */
  if (raw_size == 0 || (int64_t) raw_size > BIN_MAX_BLOCK_RAW_SIZE || stored_size > raw_size || rows == 0)
    {
      fail_binary_section_at (lineno, LOADDB_MSG_CORRUPTED_BINARY_BLOCK);
      BEGIN ABORTED;
      return false;
    }

  m_block.resize (raw_size);
  if (stored_size == raw_size)
    {
      /* LZ4 could not make the block smaller, the raw bytes are stored as they are */
      if (read_binary (m_block.data (), raw_size) != raw_size)
	{
	  fail_binary_section_at (lineno, LOADDB_MSG_TRUNCATED_BINARY_SECTION);
	  BEGIN ABORTED;
	  return false;
	}
    }
  else
    {
      m_block_stored.resize (stored_size);
      if (read_binary (m_block_stored.data (), stored_size) != stored_size)
	{
	  fail_binary_section_at (lineno, LOADDB_MSG_TRUNCATED_BINARY_SECTION);
	  BEGIN ABORTED;
	  return false;
	}

      /*
       * LZ4_decompress_safe () itself and not cubcompress::decompress (), which sets an error of its own on a
       * corrupted block on top of the one reported here. It never writes past raw_size, whatever the input.
       */
      if (LZ4_decompress_safe (m_block_stored.data (), m_block.data (), (int) stored_size, (int) raw_size)
	  != (int) raw_size)
	{
	  fail_binary_section_at (lineno, LOADDB_MSG_CORRUPTED_BINARY_BLOCK);
	  BEGIN ABORTED;
	  return false;
	}
    }

  /*
   * The checksum catches what the checks below cannot: a byte of the stored data that decompresses into other values
   * of the same layout. A block without one, written by a build for Windows, is not checked.
   */
  if (!binary_block_checksum_matches (m_block.data (), raw_size, checksum))
    {
      fail_binary_section_at (lineno, LOADDB_MSG_CORRUPTED_BINARY_BLOCK);
      BEGIN ABORTED;
      return false;
    }

  while (pos < raw_size)
    {
      BIN_REC_SIZE_TYPE rec_size;

      if (raw_size - pos < sizeof (rec_size))
	{
	  break;
	}
      memcpy (&rec_size, m_block.data () + pos, sizeof (rec_size));
      pos += sizeof (rec_size);

      /* a row has a column at least, so its size is never BIN_END_OF_RECORDS */
      if (rec_size == BIN_END_OF_RECORDS || rec_size > raw_size - pos)
	{
	  break;
	}
      pos += rec_size;
      found_rows++;
    }

  if (pos != raw_size || found_rows != rows)
    {
      fail_binary_section_at (lineno, LOADDB_MSG_CORRUPTED_BINARY_BLOCK);
      BEGIN ABORTED;
      return false;
    }

  m_block_pos = 0;
  m_block_rows_left = rows;
  return true;
}

/*
 * next_block_row () - hand over the next row of the block read by read_binary_block ()
 *   return: void
 *   yylval (out): the row, as a BINARY_ROW token of an uncompressed section carries it
 *
 * Note: the row is counted as one line, as the BINARY_BLOCK rule counts a row of an uncompressed section.
 */
void
cubload::scanner::next_block_row (cubload::parser::semantic_type *yylval)
{
  BIN_REC_SIZE_TYPE rec_size;
  string_type *row;

  assert (m_block_rows_left > 0 && m_block_pos + sizeof (rec_size) <= m_block.size ());

  memcpy (&rec_size, m_block.data () + m_block_pos, sizeof (rec_size));
  m_block_pos += sizeof (rec_size);

  /* make_string_by_reserve_size () either hands out a pooled entry or throws, it never fails */
  row = m_semantic_helper.make_string_by_reserve_size (rec_size);
  memcpy (row->val, m_block.data () + m_block_pos, rec_size);
  m_block_pos += rec_size;
  m_block_rows_left--;

  yylineno++;
  yylval->string = row;
}

/*
 * set_ldr_data_format () - select the data format of the class being scanned
 *   return : void
 *   data_format (in): data format argument of the "%format" command line
 *   format_version (in): version of the binary encoding the object file was written with
 *   bit_platform (in): word size the object file was written with
 *   endian (in): byte order the object file was written with
 *
 * Note: the three arguments which follow the data format describe the encoding of a binary
 *       section, they are meaningless for a text format. See BIN_FORMAT_VERSION.
 *
 * Note: BEGIN expands to an assignment to yy_start, a protected member of yyFlexLexer.
 *       That is why this function has to be defined here, as a member of cubload::scanner.
 */
void
cubload::scanner::set_ldr_data_format (int data_format, int format_version, int bit_platform, int endian)
{
  if (data_format != OBJECT_FORMAT_TEXT
      && ((format_version != BIN_FORMAT_VERSION && format_version != BIN_FORMAT_VERSION_PRE_11_5)
	  || bit_platform != binary_format_bit_platform () || endian != binary_format_endian ()))
    {
      /*
       * A version this build has no reader for: a newer one written by a later release, or one no
       * release writes. Fixed size values are written in host byte order, so a section of another
       * bit platform or byte order cannot be read either.
       */
      m_data_format = OBJECT_FORMAT_TEXT;
      BEGIN ABORTED;

      fail_binary_section (LOADDB_MSG_INCOMPATIBLE_BINARY_FORMAT, format_version, bit_platform, endian,
			   BIN_FORMAT_VERSION_PRE_11_5, BIN_FORMAT_VERSION, binary_format_bit_platform (),
			   binary_format_endian ());
      return;
    }

  switch (data_format)
    {
    case OBJECT_FORMAT_BINARY:
    case OBJECT_FORMAT_BINARY_COMPRESSED:
      m_data_format = (OBJECT_DATA_FORMAT) data_format;
      m_format_version = format_version;
      clear_binary_block ();
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

      fail_binary_section (LOADDB_MSG_UNSUPPORTED_DATA_FORMAT, data_format);
      break;
    }
}
