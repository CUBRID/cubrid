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
 * csql_lexer.l - SQL lexer file
 */





%{
#include "csql_grammar.h"
#include "csql_grammar_scan.h"
#include "parse_tree.h"
#include "parser_message.h"
#include "system_parameter.h"
#include "message_catalog.h"

#if defined (SUPPRESS_STRLEN_WARNING)
#define strlen(s1)  ((int) strlen(s1))
#endif /* defined (SUPPRESS_STRLEN_WARNING) */

#define CSQL_MAXNAME	(256)
#define NUMBER_LITERAL_MAX_LEN  (254)
#define NUMBER_LITERAL_EXTRA_BYTE (1) /* extra byte for overflow/rounding check in numeric literals */
#define INTEGER_MAX_LITERAL (NUMBER_LITERAL_MAX_LEN + NUMBER_LITERAL_EXTRA_BYTE)
#define FLOAT_MAX_LITERAL (NUMBER_LITERAL_MAX_LEN + NUMBER_LITERAL_EXTRA_BYTE)
#define FLOAT_LITERAL_BUFFER (FLOAT_MAX_LITERAL + 2) /* +2: +1 for trailing '0' (e.g., "3." -> "3.0"), +1 for null terminator */
#define FLOAT_APPENDABLE_TRAILING_ZERO (NUMBER_LITERAL_MAX_LEN)

int parser_yyinput_single_mode = 0;

static void init_plcsql_lexing (void);
static int parser_yyinput (char *buff, int max_size);
static bool parser_check_hint (char* hint_string);
static void parser_yy_user_action(void);
static void csql_yyerror_internal (const char *s, const int line, const int column);

static int plcsql_begin_end_balance;
static int plcsql_nest_level;
static int from_plcsql_text;

static enum {
  ensingle_quoted_string,  
  endouble_quoted_string,
  enbracket_quoted_string,
  enbacktick_quoted_string,
  enline_comment,
  enC_style_comment,
  enhyphen,
  enslash,
  enNone
} g_yyinput_type = enNone;

#undef YY_INPUT
#define YY_INPUT(buffer, result, max_size) { result = parser_yyinput(buffer, max_size); result == 0 ? result = YY_NULL : result; }

/*
 * This action is executed each time a token is matched. Its position in the
 * buffer is saved as part of the location information.
 */

/* Stores the previous state (INITIAL or PL_LANG_SPEC) prior to handling hints/comments */
static int saved_yy_start = 0;  

/* byte offset (in yybuffer_pos units) of the first character of the line that
 * is currently being scanned; column = yybuffer_pos - yyline_start_pos + 1 */
int yyline_start_pos = 0;
static int yyline_start_pos_bk = 0;
int yylineno_prev = 0;

/* Stores the start line and column number of the currently read token.
 * Since first_line and first_column of the yyloc (csql_yylloc) may be forced to 0 upon parsing failure,
 * separate variables are added and managed. */
int yytoken_start_line = 1;
int yytoken_start_column = 0;

#define CSQL_YY_LESS(n)   do {                      \
            yybuffer_pos -= (csql_yyleng - (n));    \
            yyline_start_pos = yyline_start_pos_bk; \
            this_parser->column = parser_column_position(); \
            yyless((n));                            \
            yylineno_prev = yylineno;               \
            this_parser->line = yylineno;           \
        } while(0)

#define YY_USER_ACTION  { parser_yy_user_action(); }

#define IS_BLANK_CHAR(c)    (char_isspace2((c)))

int g_msg[1024];
int msg_ptr;

int yybuffer_pos;
int is_dblink_query_string = 0;
int expecting_pl_lang_spec = 0;

/* TODO: 
 * For now, I've set it to ignore warnings in the automatically generated code. 
 * If possible, let's remove warnings normally instead of setting it to ignore. */
#if defined(__GNUC__) || defined(__clang__)
  #define BEGIN_SUPPRESS_WARNING_BISON_FLEX                \
    _Pragma("GCC diagnostic push")                         \
    _Pragma("GCC diagnostic ignored \"-Wsign-compare\"")   \
    _Pragma("GCC diagnostic ignored \"-Wredundant-decls\"")\
    _Pragma("GCC diagnostic ignored \"-Wimplicit-fallthrough=\"")

  #define END_SUPPRESS_WARNING_BISON_FLEX                  \
    _Pragma("GCC diagnostic pop")
#else
  #define BEGIN_SUPPRESS_WARNING_BISON_FLEX
  #define END_SUPPRESS_WARNING_BISON_FLEX
#endif

BEGIN_SUPPRESS_WARNING_BISON_FLEX
%}

%option yylineno

%x QUOTED_NCHAR_STRING
%x QUOTED_BIT_STRING
%x QUOTED_HEX_STRING
%x QUOTED_CHAR_STRING
%x DOUBLY_QUOTED_CHAR_STRING
%x DELIMITED_ID_NAME
%x BRACKET_ID_NAME
%x BACKTICK_ID_NAME
%x QUOTED_EUCKR_STRING
%x QUOTED_ISO_STRING
%x QUOTED_BINARY_STRING
%x QUOTED_UTF8_STRING
%x POST_DOT_OR_RIGHT_ARROW_STRING
%x PL_LANG_SPEC
%x PLCSQL_TEXT
%x POST_PLCSQL_TEXT
%x LINE_HINT
%x BLOCK_HINT
%x LINE_COMMENT
%x BLOCK_COMMENT

 /* id letter */
IDL       [a-zA-Z0-9_]

%%

[ \t\r]+								{ /* no action */;  }
\n									{ /* no action */;  }

[aA][bB][sS][oO][lL][uU][tT][eE]					{ return ABSOLUTE_; }
[aA][cC][cC][eE][sS][sS]						{ return ACCESS; }
[aA][cC][tT][iI][oO][nN]						{ return ACTION; }
[aA][cC][tT][iI][vV][eE]						{ csql_yylval.cptr = pt_makename(yytext);
									  return ACTIVE; }
[aA][dD][dD]								{ return ADD; }
[aA][dD][dD][dD][aA][tT][eE]						{ csql_yylval.cptr = pt_makename(yytext);
									  return ADDDATE; }
[aA][dD][dD]_[mM][oO][nN][tT][hH][sS]					{ return ADD_MONTHS; }
[aA][eE][sS]                                                            { csql_yylval.cptr = pt_makename(yytext);
                                                                          return AES; }
[aA][fF][tT][eE][rR]							{ return AFTER; }
[aA][lL][lL]								{ return ALL; }
[aA][lL][lL][oO][cC][aA][tT][eE]					{ return ALLOCATE; }
[aA][lL][tT][eE][rR]							{ return ALTER; }
[aA][nN][aA][lL][yY][zZ][eE]						{ csql_yylval.cptr = pt_makename(yytext);
									  return ANALYZE; }
[aA][nN][dD]								{ return AND; }
[aA][nN][yY]								{ return ANY; }
[aA][rR][cC][hH][iI][vV][eE]						{ csql_yylval.cptr = pt_makename(yytext);
									  return ARCHIVE; }
[aA][rR][eE]								{ return ARE; }
[aA][rR][iI][aA]							{ csql_yylval.cptr = pt_makename(yytext);
                                                                          return ARIA; }
[aA][sS]								{ 
                                                                          if (expecting_pl_lang_spec) {
                                                                                expecting_pl_lang_spec = 0;
                                                                                BEGIN(PL_LANG_SPEC);
                                                                            }
                                                                          return AS; }
[aA][sS][cC]								{ return ASC; }
[aA][sS][sS][eE][rR][tT][iI][oO][nN]					{ return ASSERTION; }
[aA][sS][yY][nN][cC]							{ return ASYNC; }
[aA][tT]								{ return AT; }
[aA][tT][tT][aA][cC][hH]						{ return ATTACH; }
[aA][tT][tT][rR][iI][bB][uU][tT][eE]					{ return ATTRIBUTE; }
[aA][uU][tT][oO]_[iI][nN][cC][rR][eE][mM][eE][nN][tT]			{ csql_yylval.cptr = pt_makename(yytext);
									  return AUTO_INCREMENT; }
[aA][vV][gG]								{ return AVG; }
[aA][uU][tT][hH][iI][dD]						{ csql_yylval.cptr = pt_makename(yytext);
                                                                          return AUTHID; }
[bB][eE][fF][oO][rR][eE]						{ return BEFORE; }
[bB][eE][gG][iI][nN]							{ return BEGIN_; }
[bB][eE][nN][cC][hH][mM][aA][rR][kK]					{ csql_yylval.cptr = pt_makename(yytext);
                                                                          return BENCHMARK; }
[bB][eE][tT][wW][eE][eE][nN]						{ return BETWEEN; }
[bB][iI][gG][iI][nN][tT]						{ return BIGINT;}
[bB][iI][nN][aA][rR][yY]						{ return BINARY;}
[bB][iI][tT]								{ return BIT; }
[bB][iI][tT]_[lL][eE][nN][gG][tT][hH]					{ return BIT_LENGTH; }
[bB][iI][tT]_[aA][nN][dD]						{ csql_yylval.cptr = pt_makename(yytext);
									  return BIT_AND; }
[bB][iI][tT]_[oO][rR]							{ csql_yylval.cptr = pt_makename(yytext);
									  return BIT_OR; }
[bB][iI][tT]_[xX][oO][rR]						{ csql_yylval.cptr = pt_makename(yytext);
									  return BIT_XOR; }
[bB][lL][oO][bB]							{ return BLOB_; }
[bB][oO][oO][lL][eE][aA][nN]						{ return BOOLEAN_; }
[bB][oO][tT][hH]							{ return BOTH_; }
[bB][rR][eE][aA][dD][tT][hH]						{ return BREADTH; }
[bB][uU][cC][kK][eE][tT][sS]						{ return BUCKETS; }
[bB][yY]								{ return BY; }
[bB][uU][fF][fF][eE][rR]						{ csql_yylval.cptr = pt_makename(yytext);
									  return BUFFER; }
[cC][aA][lL][lL]							{ return CALL; }
[cC][aA][lL][lL][eE][rR]						{ csql_yylval.cptr = pt_makename(yytext);
                                                                          return CALLER; }
[cC][aA][cC][hH][eE]							{ csql_yylval.cptr = pt_makename(yytext);
									  return CACHE; }
[cC][aA][pP][aA][cC][iI][tT][yY]					{ csql_yylval.cptr = pt_makename(yytext);
									  return CAPACITY; }
[cC][aA][sS][cC][aA][dD][eE]						{ return CASCADE; }
[cC][aA][sS][cC][aA][dD][eE][dD]					{ return CASCADED; }
[cC][aA][sS][eE]							{ return CASE; }
[cC][aA][sS][tT]							{ return CAST; }
[cC][aA][tT][aA][lL][oO][gG]						{ return CATALOG; }
[cC][hH][aA][nN][gG][eE]						{ return CHANGE; }
[cC][hH][aA][rR][aA][cC][tT][eE][rR]					{ return CHAR_; }
[cC][hH][aA][rR][aA][cC][tT][eE][rR]_[sS][eE][tT]			{ csql_yylval.cptr = pt_makename(yytext);
									  return CHARACTER_SET_; }
[cC][hH][aA][rR][sS][eE][tT]						{ csql_yylval.cptr = pt_makename(yytext);
									  return CHARSET; }
[cC][hH][eE][cC][kK]							{ return CHECK; }
[cC][hH][rR]								{ csql_yylval.cptr = pt_makename(yytext);
									  return CHR; }
[cC][lL][aA][sS][sS]							{ return CLASS; }
[cC][lL][aA][sS][sS][eE][sS]						{ return CLASSES; }
[cC][lL][oO][bB]							{ return CLOB_; }
[cC][lL][oO][bB]_[tT][oO]_[cC][hH][aA][rR]				{ csql_yylval.cptr = pt_makename(yytext);
									  return CLOB_TO_CHAR; }
[cC][lL][oO][sS][eE]							{ csql_yylval.cptr = pt_makename(yytext);
									  return CLOSE; }
[cC][oO][aA][lL][eE][sS][cC][eE]					{ return COALESCE; }
[cC][oO][lL][lL][aA][tT][eE]						{ return COLLATE; }
[cC][oO][lL][lL][aA][tT][iI][oO][nN]					{ csql_yylval.cptr = pt_makename(yytext);
									  return COLLATION; }
[cC][oO][lL][uU][mM][nN]						{ return COLUMN; }
[cC][oO][lL][uU][mM][nN][sS]						{ csql_yylval.cptr = pt_makename(yytext);
									  return COLUMNS; }
[cC][oO][mM][mM][eE][nN][tT]					        { csql_yylval.cptr = pt_makename(yytext);
									  return COMMENT; }
[cC][oO][mM][mM][iI][tT]						{ return COMMIT; }
[cC][oO][mM][mM][iI][tT][tT][eE][dD]					{ csql_yylval.cptr = pt_makename(yytext);
									  return COMMITTED; }
[cC][oO][mM][pP][iI][lL][eE]					        { csql_yylval.cptr = pt_makename(yytext);
                                                                          return COMPILE; }
[cC][oO][nN][nN][eE][cC][tT]						{ return CONNECT; }
[cC][oO][nN][nN][eE][cC][tT][_][bB][yY][_][iI][sS][cC][yY][cC][lL][eE]	{ return CONNECT_BY_ISCYCLE; }
[cC][oO][nN][nN][eE][cC][tT][_][bB][yY][_][iI][sS][lL][eE][aA][fF]	{ return CONNECT_BY_ISLEAF; }
[cC][oO][nN][nN][eE][cC][tT][_][bB][yY][_][rR][oO][oO][tT]		{ return CONNECT_BY_ROOT; }
[cC][oO][nN][nN][eE][cC][tT][iI][oO][nN]				{ return CONNECTION; }
[cC][oO][nN][sS][tT][rR][aA][iI][nN][tT]				{ return CONSTRAINT; }
[cC][oO][nN][sS][tT][rR][aA][iI][nN][tT][sS]				{ return CONSTRAINTS; }
[cC][oO][nN][tT][iI][nN][uU][eE]					{ return CONTINUE; }
[cC][oO][nN][vV][eE][rR][tT]						{ return CONVERT; }
[cC][oO][rR][rR][eE][sS][pP][oO][nN][dD][iI][nN][gG]			{ return CORRESPONDING; }
[cC][oO][sS][tT]							{ csql_yylval.cptr = pt_makename(yytext);
									  return COST;
									}
[cC][oO][uU][nN][tT]							{ return COUNT; }
[cC][rR][eE][aA][tT][eE]						{ return CREATE; }
[cC][rR][oO][sS][sS]							{ return CROSS; }
[cC][rR][iI][tT][iI][cC][aA][lL]					{ csql_yylval.cptr = pt_makename(yytext);
									  return CRITICAL; }
[cC][uU][mM][eE][_][dD][iI][sS][tT]					{ csql_yylval.cptr = pt_makename(yytext);
									  return CUME_DIST; }
[cC][uU][rR][rR][eE][nN][tT]						{ return CURRENT; }
[cC][uU][rR][rR][eE][nN][tT]_[dD][aA][tT][eE]				{ return CURRENT_DATE; }
[cC][uU][rR][rR][eE][nN][tT]_[tT][iI][mM][eE]				{ return CURRENT_TIME; }
[cC][uU][rR][rR][eE][nN][tT]_[tT][iI][mM][eE][sS][tT][aA][mM][pP]	{ return CURRENT_TIMESTAMP; }
[cC][uU][rR][rR][eE][nN][tT]_[dD][aA][tT][eE][tT][iI][mM][eE] 		{ return CURRENT_DATETIME; }
[cC][uU][rR][rR][eE][nN][tT]_[uU][sS][eE][rR]				{ return CURRENT_USER; }
[cC][uU][rR][sS][oO][rR]						{ return CURSOR; }
[cC][yY][cC][lL][eE]							{ return CYCLE; }
[cC][hH][aA][rR]							{ return CHAR_; }
[dD][aA][tT][aA]							{ return DATA; }
[dD][aA][tT][aA][bB][aA][sS][eE]					{ return DATABASE; }
[dD][aA][tT][aA]_[tT][yY][pP][eE]___					{ return DATA_TYPE___; }
[dD][aA][tT][eE]_[aA][dD][dD]						{ csql_yylval.cptr = pt_makename(yytext);
									  return DATE_ADD; }
[dD][aA][tT][eE]_[sS][uU][bB]						{ csql_yylval.cptr = pt_makename(yytext);
									  return DATE_SUB; }
[dD][aA][tT][eE][tT][iI][mM][eE]					{ return DATETIME; }
[dD][aA][tT][eE][tT][iI][mM][eE][tT][zZ]				{ return DATETIMETZ; }
[dD][aA][tT][eE][tT][iI][mM][eE][lL][tT][zZ]				{ return DATETIMELTZ; }
[dD][aA][yY]								{ return DAY_; }
[dD][aA][yY]_[mM][iI][lL][lL][iI][sS][eE][cC][oO][nN][dD]		{ return DAY_MILLISECOND; }
[dD][aA][yY]_[sS][eE][cC][oO][nN][dD]     				{ return DAY_SECOND; }
[dD][aA][yY]_[mM][iI][nN][uU][tT][eE]     				{ return DAY_MINUTE; }
[dD][aA][yY]_[hH][oO][uU][rR]	       					{ return DAY_HOUR; }
[dD][bB][lL][iI][nN][kK]					        { csql_yylval.cptr = pt_makename(yytext);
									  return DBLINK; }
[dD][bB][nN][aA][mM][eE]					        { csql_yylval.cptr = pt_makename(yytext);
									  return DBNAME; }
[dD][bB][tT][iI][mM][eE][zZ][oO][nN][eE]                                { csql_yylval.cptr = pt_makename(yytext);
                                                                          return DB_TIMEZONE; }
[dD][eE][aA][lL][lL][oO][cC][aA][tT][eE]				{ return DEALLOCATE; }
[dD][eE][cC]								{ return NUMERIC; }
[dD][eE][cC][iI][mM][aA][lL]						{ return NUMERIC; }
[dD][eE][cC][lL][aA][rR][eE]						{ return DECLARE; }
[dD][eE][cC][rR][eE][mM][eE][nN][tT]					{ csql_yylval.cptr = pt_makename(yytext);
									  return DECREMENT; }
[dD][eE][dD][uU][pP][lL][iI][cC][aA][tT][eE] 	                        { csql_yylval.cptr = pt_makename(yytext);
									  return DEDUPLICATE_; }
[dD][eE][fF][aA][uU][lL][tT]						{ return DEFAULT; }
[dD][eE][fF][eE][rR][rR][aA][bB][lL][eE]				{ return DEFERRABLE; }
[dD][eE][fF][eE][rR][rR][eE][dD]					{ return DEFERRED; }
[dD][eE][fF][iI][nN][eE][rR]    					{ csql_yylval.cptr = pt_makename(yytext);
                                                                          return DEFINER; }
[dD][eE][lL][eE][tT][eE]						{ return DELETE_; }
[dD][eE][nN][sS][eE]_[rR][aA][nN][kK]					{ csql_yylval.cptr = pt_makename(yytext);
									  return DENSE_RANK; }
[dD][eE][pP][tT][hH]							{ return DEPTH; }
[dD][eE][sS][cC]							{ return DESC; }
[dD][eE][sS][cC][rR][iI][bB][eE]					{ return DESCRIBE; }
[dD][eE][sS][cC][rR][iI][pP][tT][oO][rR]				{ return DESCRIPTOR; }
[dD][eE][tT][eE][rR][mM][iI][nN][iI][sS][tT][iI][cC]			{ csql_yylval.cptr = pt_makename(yytext);
									  return DETERMINISTIC; }
[dD][iI][aA][gG][nN][oO][sS][tT][iI][cC][sS]				{ return DIAGNOSTICS; }
[dD][iI][fF][fF][eE][rR][eE][nN][cC][eE]				{ return DIFFERENCE_; }
[dD][iI][sS][cC][oO][nN][nN][eE][cC][tT]				{ return DISCONNECT; }
[dD][iI][sS][tT][iI][nN][cC][tT]					{ return DISTINCT; }
[dD][iI][sS][tT][iI][nN][cC][tT][rR][oO][wW]				{ return DISTINCT; }
[dD][iI][vV]								{ return DIV; }
[dD][oO]								{ return DO; }
[dD][oO][mM][aA][iI][nN]						{ return Domain; }
[dD][rR][oO][pP]							{ return DROP; }
[dD][uU][pP][lL][iI][cC][aA][tT][eE]					{ return DUPLICATE_; }
[dD][aA][tT][eE]							{ return Date; }
[dD][oO][uU][bB][lL][eE]						{ return Double; }
[dD][oO][nN][tT]_[Rr][Ee][uU][sS][eE]_[oO][iI][dD]			{ csql_yylval.cptr = pt_makename(yytext);
									  return DONT_REUSE_OID; }
[eE][aA][cC][hH]							{ return EACH; }
[eE][lL][sS][eE]							{ return ELSE; }
[eE][lL][sS][eE][iI][fF]						{ return ELSEIF; }
[eE][lL][tT]								{ csql_yylval.cptr = pt_makename(yytext);
									  return ELT; }
[eE][mM][pP][tT][yY]                                                    { csql_yylval.cptr = pt_makename(yytext);
                                                                          return EMPTY; }
[eE][nN][cC][rR][yY][pP][tT]                                            { csql_yylval.cptr = pt_makename(yytext);
                                                                          return ENCRYPT; }
[eE][nN][dD]								{ return END; }
[eE][nN][uU][mM]							{ return ENUM; }
[eE][qQ][uU][aA][lL][sS]						{ return EQUALS; }
[eE][rR][rR][oO][rR]     						{ csql_yylval.cptr = pt_makename(yytext);
                                                                          return ERROR_; }
[eE][sS][cC][aA][pP][eE]						{ return ESCAPE; }
[eE][vV][aA][lL][uU][aA][tT][eE]					{ return EVALUATE; }
[eE][xX][cC][eE][pP][tT]						{ return EXCEPT; }
[eE][xX][cC][eE][pP][tT][iI][oO][nN]					{ return EXCEPTION; }
[eE][xX][eE][cC]							{ return EXEC; }
[eE][xX][eE][cC][uU][tT][eE]						{ return EXECUTE; }
[eE][xX][iI][sS][tT][sS]						{ return EXISTS; }
[eE][xX][pP][lL][aA][iI][nN]						{ csql_yylval.cptr = pt_makename(yytext);
									  return EXPLAIN; }
[eE][xX][tT][eE][rR][nN][aA][lL]					{ return EXTERNAL; }
[eE][xX][tT][rR][aA][cC][tT]						{ return EXTRACT; }
[fF][eE][tT][cC][hH]							{ return FETCH; }
[fF][iI][rR][sS][tT]							{ return FIRST; }
[fF][iI][rR][sS][tT]_[vV][aA][lL][uU][eE]				{ csql_yylval.cptr = pt_makename(yytext);
									  return FIRST_VALUE; }
[fF][oO][rR][cC][eE]							{ csql_yylval.cptr = pt_makename(yytext);
                                                                          return FORCE; }
[fF][oO][rR][eE][iI][gG][nN]						{ return FOREIGN; }
[fF][oO][uU][nN][dD]							{ return FOUND; }
[fF][rR][oO][mM]							{ return FROM; }
[fF][uU][lL][lL]							{ return FULL; }
[fF][uU][lL][lL][sS][cC][aA][nN]					{ csql_yylval.cptr = pt_makename(yytext);
                                                                          return FULLSCAN; }
[fF][uU][nN][cC][tT][iI][oO][nN]					{ return FUNCTION; }
[fF][aA][lL][sS][eE]							{ return False; }
[fF][iI][lL][eE]							{ return File; }
[fF][lL][oO][aA][tT]							{ return FLOAT_; }
[fF][oO][rR]								{ return For; }
[gG][eE][nN][eE][rR][aA][lL]						{ return GENERAL; }
[gG][eE][tT]								{ return GET; }
[gG][eE]_[iI][nN][fF]							{ csql_yylval.cptr = pt_makename(yytext);
									  return GE_INF_; }
[gG][eE]_[lL][eE]							{ csql_yylval.cptr = pt_makename(yytext);
									  return GE_LE_; }
[gG][eE]_[lL][tT]							{ csql_yylval.cptr = pt_makename(yytext);
									  return GE_LT_; }
[gG][lL][oO][bB][aA][lL]						{ return GLOBAL; }
[gG][oO]								{ return GO; }
[gG][oO][tT][oO]							{ return GOTO; }
[gG][rR][aA][nN][tT]							{ return GRANT; }
[gG][rR][aA][nN][tT][sS]						{ csql_yylval.cptr = pt_makename(yytext);
									  return GRANTS; }
[gG][rR][oO][uU][pP]							{ return GROUP_; }
[gG][rR][oO][uU][pP][_][cC][oO][nN][cC][aA][tT]				{ csql_yylval.cptr = pt_makename(yytext);
									  return GROUP_CONCAT; }
[gG][rR][oO][uU][pP][sS]						{ csql_yylval.cptr = pt_makename(yytext);
									  return GROUPS; }
[gG][tT]_[iI][nN][fF]							{ csql_yylval.cptr = pt_makename(yytext);
									  return GT_INF_; }
[gG][tT]_[lL][eE]							{ csql_yylval.cptr = pt_makename(yytext);
									  return GT_LE_; }
[gG][tT]_[lL][tT]							{ csql_yylval.cptr = pt_makename(yytext);
									  return GT_LT_; }
[hH][aA][sS][hH]							{ csql_yylval.cptr = pt_makename(yytext);
									  return HASH; }
[hH][aA][vV][iI][nN][gG]						{ return HAVING; }
[hH][eE][aA][dD][eE][rR]						{ csql_yylval.cptr = pt_makename(yytext);
									  return HEADER; }
[hH][eE][aA][pP]							{ csql_yylval.cptr = pt_makename(yytext);
									  return HEAP; }                                                                            
[hH][iI][sS][tT][oO][gG][rR][aA][mM]					{ return HISTOGRAM; }
[hH][oO][sS][tT]					                { csql_yylval.cptr = pt_makename(yytext);
									  return HOST; }
[hH][oO][uU][rR]							{ return HOUR_; }
[hH][oO][uU][rR]_[mM][iI][lL][lL][iI][sS][eE][cC][oO][nN][dD]		{ return HOUR_MILLISECOND; }
[hH][oO][uU][rR]_[sS][eE][cC][oO][nN][dD]				{ return HOUR_SECOND; }
[hH][oO][uU][rR]_[mM][iI][nN][uU][tT][eE]				{ return HOUR_MINUTE; }
[iI][dD][eE][nN][tT][iI][tT][yY]					{ return IDENTITY; }
[iI][fF]								{ return IF; }
[iI][fF][nN][uU][lL][lL]						{ csql_yylval.cptr = pt_makename(yytext);
									  return IFNULL; }
[iI][sS][nN][uU][lL][lL]						{ csql_yylval.cptr = pt_makename(yytext);
									  return ISNULL; }
[iI][gG][nN][oO][rR][eE]						{ return IGNORE_; }
[iI][mM][mM][eE][dD][iI][aA][tT][eE]					{ return IMMEDIATE; }
[iI][nN]								{ return IN_; }
[iI][nN][aA][cC][tT][iI][vV][eE]					{ csql_yylval.cptr = pt_makename(yytext);
									  return INACTIVE; }
[iI][nN][cC][rR][eE][mM][eE][nN][tT]					{ csql_yylval.cptr = pt_makename(yytext);
									  return INCREMENT; }
[iI][nN][dD][eE][xX]							{ return INDEX; }
[iI][nN][dD][eE][xX][_][pP][rR][eE][fF][iI][xX]				{ csql_yylval.cptr = pt_makename(yytext);
                                                                          return INDEX_PREFIX; }
[iI][nN][dD][eE][xX][eE][sS]						{ csql_yylval.cptr = pt_makename(yytext);
									  return INDEXES; }
[iI][nN][dD][iI][cC][aA][tT][oO][rR]					{ return INDICATOR; }
[iI][nN][fF][iI][nN][iI][tT][eE]					{ csql_yylval.cptr = pt_makename(yytext);
									  return INFINITE_; }
[iI][nN][fF]_[lL][eE]							{ csql_yylval.cptr = pt_makename(yytext);
									  return INF_LE_; }
[iI][nN][fF]_[lL][tT]							{ csql_yylval.cptr = pt_makename(yytext);
									  return INF_LT_; }
[iI][nN][hH][eE][rR][iI][tT]						{ return INHERIT; }
[iI][nN][iI][tT][iI][aA][lL][lL][yY]					{ return INITIALLY; }
[iI][nN][nN][eE][rR]							{ return INNER; }
[iI][nN][oO][uU][tT]							{ return INOUT; }
[iI][nN][pP][uU][tT]							{ return INPUT_; }
[iI][nN][sS][eE][rR][tT]						{ return INSERT; }
[iI][nN][sS][tT][aA][nN][cC][eE][sS]					{ csql_yylval.cptr = pt_makename(yytext);
									  return INSTANCES; }
[iI][nN][tT][eE][rR][sS][eE][cC][tT]					{ return INTERSECT; }
[iI][nN][tT][eE][rR][sS][eE][cC][tT][iI][oO][nN]			{ return INTERSECTION; }
[iI][nN][tT][eE][rR][vV][aA][lL]					{ return INTERVAL; }
[iI][nN][tT][oO]							{ return INTO; }
[iI][nN][vV][aA][lL][iI][dD][aA][tT][eE]				{ csql_yylval.cptr = pt_makename(yytext);
									  return INVALIDATE; }
[iI][nN][vV][iI][sS][iI][bB][lL][eE]					{ csql_yylval.cptr = pt_makename(yytext);
									  return INVISIBLE; }
[iI][sS]								{
                                                                            if (expecting_pl_lang_spec) {
                                                                                    expecting_pl_lang_spec = 0;
                                                                                    BEGIN(PL_LANG_SPEC);
                                                                            }
                                                                            return IS; }
[iI][sS][oO][lL][aA][tT][iI][oO][nN]					{ return ISOLATION; }
[iI][nN][tT]								{ return INTEGER; }
[iI][nN][tT][eE][gG][eE][rR]						{ return INTEGER; }
[jJ][aA][vV][aA]							{ csql_yylval.cptr = pt_makename(yytext);
									  return JAVA; }
[jJ][oO][iI][nN]							{ return JOIN; }
[jJ][oO][bB]								{ csql_yylval.cptr = pt_makename(yytext);
									  return JOB; }
[jJ][sS][oO][nN]							{ return JSON; }
[jJ][sS][oO][nN][_][aA][rR][rR][aA][yY]	                                { // JSON_ARRAY conflicts with jansson.h
									  csql_yylval.cptr = pt_makename(yytext);
									  return JSON_ARRAY_LEX; }
[jJ][sS][oO][nN][_][aA][rR][rR][aA][yY][_][aA][pP][pP][eE][nN][dD]      { csql_yylval.cptr = pt_makename(yytext);
									  return JSON_ARRAY_APPEND; }
[jJ][sS][oO][nN][_][aA][rR][rR][aA][yY][aA][gG][gG]                     { csql_yylval.cptr = pt_makename(yytext);
									  return JSON_ARRAYAGG; }
[jJ][sS][oO][nN][_][aA][rR][rR][aA][yY][_][iI][nN][sS][eE][rR][tT]      { csql_yylval.cptr = pt_makename(yytext);
									  return JSON_ARRAY_INSERT; }
[jJ][sS][oO][nN][_][cC][oO][nN][tT][aA][iI][nN][sS]                     { csql_yylval.cptr = pt_makename(yytext);
									  return JSON_CONTAINS; }
[jJ][sS][oO][nN][_][cC][oO][nN][tT][aA][iI][nN][sS][_][pP][aA][tT][hH]  { csql_yylval.cptr = pt_makename(yytext);
									  return JSON_CONTAINS_PATH; }
[jJ][sS][oO][nN][_][dD][eE][pP][tT][hH]                                 { csql_yylval.cptr = pt_makename(yytext);
									  return JSON_DEPTH; }
[jJ][sS][oO][nN][_][eE][xX][tT][rR][aA][cC][tT]                         { csql_yylval.cptr = pt_makename(yytext);
									  return JSON_EXTRACT; }
[jJ][sS][oO][nN][_][gG][eE][tT][_][aA][lL][lL][_][pP][aA][tT][hH][sS]	{ csql_yylval.cptr = pt_makename(yytext);
									  return JSON_GET_ALL_PATHS; }
[jJ][sS][oO][nN][_][iI][nN][sS][eE][rR][tT]                             { csql_yylval.cptr = pt_makename(yytext);
									  return JSON_INSERT; }
[jJ][sS][oO][nN][_][kK][eE][yY][sS]					{ csql_yylval.cptr = pt_makename(yytext);
									  return JSON_KEYS; }
[jJ][sS][oO][nN][_][lL][eE][nN][gG][tT][hH]                             { csql_yylval.cptr = pt_makename(yytext);
									  return JSON_LENGTH; }
[jJ][sS][oO][nN][_][mM][eE][rR][gG][eE]                                 { csql_yylval.cptr = pt_makename(yytext);
									  return JSON_MERGE; }
[jJ][sS][oO][nN][_][mM][eE][rR][gG][eE][_][pP][aA][tT][cC][hH]		{ csql_yylval.cptr = pt_makename(yytext);
									  return JSON_MERGE_PATCH; }
[jJ][sS][oO][nN][_][mM][eE][rR][gG][eE][_][pP][rR][eE][sS][eE][rR][vV][eE]	{
										csql_yylval.cptr = pt_makename(yytext);
										return JSON_MERGE_PRESERVE; }
[jJ][sS][oO][nN][_][oO][bB][jJ][eE][cC][tT]                             { csql_yylval.cptr = pt_makename(yytext);
                                                                          // JSON_OBJECT conflicts with jansson.h
                                                                          return JSON_OBJECT_LEX; }
[jJ][sS][oO][nN][_][oO][bB][jJ][eE][cC][tT][aA][gG][gG]			{ csql_yylval.cptr = pt_makename(yytext);
                                                                          return JSON_OBJECTAGG; }
[jJ][sS][oO][nN][_][pP][rR][eE][tT][tT][yY]                             { csql_yylval.cptr = pt_makename(yytext);
									  return JSON_PRETTY; }
[jJ][sS][oO][nN][_][qQ][uU][oO][tT][eE]                                 { csql_yylval.cptr = pt_makename(yytext);
									  return JSON_QUOTE; }
[jJ][sS][oO][nN][_][rR][eE][mM][oO][vV][eE]                             { csql_yylval.cptr = pt_makename(yytext);
									  return JSON_REMOVE; }
[jJ][sS][oO][nN][_][rR][eE][pP][lL][aA][cC][eE]                         { csql_yylval.cptr = pt_makename(yytext);
									  return JSON_REPLACE; }
[jJ][sS][oO][nN][_][sS][eE][tT]                                         { csql_yylval.cptr = pt_makename(yytext);
									  return JSON_SET; }
[jJ][sS][oO][nN][_][sS][eE][aA][rR][cC][hH]                             { csql_yylval.cptr = pt_makename(yytext);
									  return JSON_SEARCH; }
[jJ][sS][oO][nN][_][tT][aA][bB][lL][eE]                                 { csql_yylval.cptr = pt_makename(yytext);
									  return JSON_TABLE; }
[jJ][sS][oO][nN][_][tT][yY][pP][eE]                                     { csql_yylval.cptr = pt_makename(yytext);
									  return JSON_TYPE; }
[jJ][sS][oO][nN][_][uU][nN][qQ][uU][oO][tT][eE]                         { csql_yylval.cptr = pt_makename(yytext);
									  return JSON_UNQUOTE; }
[jJ][sS][oO][nN][_][vV][aA][lL][iI][dD]                                 { csql_yylval.cptr = pt_makename(yytext);
									  return JSON_VALID; }
[kK][eE][yY]								{ return KEY; }
[kK][eE][yY][sS]							{ csql_yylval.cptr = pt_makename(yytext);
									  return KEYS; }
[kK][iI][lL][lL]							{ csql_yylval.cptr = pt_makename(yytext);
                                                                          return KILL; }
[lL][aA][nN][gG][uU][aA][gG][eE]					{ return LANGUAGE; }
[kK][eE][yY][lL][iI][mM][iI][tT]					{ csql_yylval.cptr = pt_makename(yytext);
                                                                          return KEYLIMIT; }
[lL][aA][gG]								{ csql_yylval.cptr = pt_makename(yytext);
									  return LAG; }
[lL][aA][sS][tT]							{ return LAST; }
[lL][aA][sS][tT]_[vV][aA][lL][uU][eE]		                        { csql_yylval.cptr = pt_makename(yytext);
									  return LAST_VALUE; }
[lL][cC][aA][sS][eE]							{ csql_yylval.cptr = pt_makename(yytext);
									  return LCASE; }
[lL][eE][aA][dD]							{ csql_yylval.cptr = pt_makename(yytext);
									  return LEAD; }
[lL][eE][aA][dD][iI][nN][gG]						{ return LEADING_; }
[lL][eE][aA][vV][eE]							{ return LEAVE; }
[lL][eE][fF][tT]							{ return LEFT; }
[lL][eE][sS][sS]							{ return LESS; }
[lL][eE][vV][eE][lL]							{ return LEVEL; }
[lL][iI][kK][eE]							{ return LIKE; }
[lL][iI][mM][iI][tT]							{ return LIMIT; }
[lL][iI][sS][tT]							{ return LIST; }
[lL][oO][cC][aA][lL]							{ return LOCAL; }
[lL][oO][cC][aA][lL]_[tT][rR][aA][nN][sS][aA][cC][tT][iI][oO][nN]_[iI][dD] { return LOCAL_TRANSACTION_ID; }
[lL][oO][cC][aA][lL][tT][iI][mM][eE]					{ return LOCALTIME; }
[lL][oO][cC][aA][lL][tT][iI][mM][eE][sS][tT][aA][mM][pP]		{ return LOCALTIMESTAMP; }
[lL][oO][cC][kK]							{ csql_yylval.cptr = pt_makename(yytext);
									  return LOCK_; }
[lL][oO][gG]								{ csql_yylval.cptr = pt_makename(yytext);
									  return LOG; }
[lL][oO][oO][pP]							{ return LOOP; }
[lL][oO][wW][eE][rR]							{ return LOWER; }
[mM][aA][tT][cC][hH]							{ return MATCH; }
[mM][aA][tT][cC][hH][eE][dD]						{ csql_yylval.cptr = pt_makename(yytext);
                                                                          return MATCHED; }
[mM][aA][xX][iI][mM][uU][mM]						{ csql_yylval.cptr = pt_makename(yytext);
									  return MAXIMUM; }
[mM][aA][xX][vV][aA][lL][uU][eE]					{ csql_yylval.cptr = pt_makename(yytext);
									  return MAXVALUE; }
[mM][eE][dD][iI][uU][mM][iI][nN][tT]				        { return INTEGER; }
[mM][eE][dD][iI][aA][nN]						{ csql_yylval.cptr = pt_makename(yytext);
									  return MEDIAN; }
[mM][eE][mM][bB][eE][rR][sS]						{ csql_yylval.cptr = pt_makename(yytext);
									  return MEMBERS; }
[mM][eE][rR][gG][eE]							{ return MERGE; }
[mM][eE][tT][hH][oO][dD]						{ return METHOD; }
[mM][iI][lL][lL][iI][sS][eE][cC][oO][nN][dD]				{ return MILLISECOND_; }
[mM][iI][nN][uU][tT][eE]						{ return MINUTE_; }
[mM][iI][nN][uU][tT][eE]_[mM][iI][lL][lL][iI][sS][eE][cC][oO][nN][dD]	{ return MINUTE_MILLISECOND; }
[mM][iI][nN][uU][tT][eE]_[sS][eE][cC][oO][nN][dD]     			{ return MINUTE_SECOND; }
[mM][iI][nN][vV][aA][lL][uU][eE]					{ csql_yylval.cptr = pt_makename(yytext);
									  return MINVALUE; }
[mM][oO][dD]								{ return MOD; }
[mM][oO][dD][iI][fF][yY]						{ return MODIFY; }
[mM][oO][dD][uU][lL][eE]						{ return MODULE; }
[mM][oO][nN][tT][hH]							{ return MONTH_; }
[mM][uU][lL][tT][iI][sS][eE][tT]					{ return MULTISET; }
[mM][uU][lL][tT][iI][sS][eE][tT]_[oO][fF]				{ return MULTISET_OF; }
[mM][aA][xX]								{ return Max; }
[mM][iI][nN]								{ return Min; }
[mM][oO][nN][eE][tT][aA][rR][yY]					{ return Monetary; }
[nN][aA]								{ return NA; }
[nN][aA][mM][eE]							{ csql_yylval.cptr = pt_makename(yytext);
									  return NAME; }
[nN][aA][mM][eE][sS]							{ return NAMES; }
[nN][aA][tT][iI][oO][nN][aA][lL]					{ return NATIONAL; }
[nN][aA][tT][uU][rR][aA][lL]						{ return NATURAL; }
[nN][cC][hH][aA][rR]							{ return NCHAR; }
[nN][eE][sS][tT][eE][dD]                                                { csql_yylval.cptr = pt_makename(yytext);
                                                                          return NESTED; }
[nN][eE][xX][tT]							{ return NEXT; }
[nN][oO]								{ return NO; }
[nN][oO][cC][aA][cC][hH][eE]						{ csql_yylval.cptr = pt_makename(yytext);
									  return NOCACHE; }
[nN][oO][cC][yY][cC][lL][eE]						{ csql_yylval.cptr = pt_makename(yytext);
									  return NOCYCLE; }
[nN][oO][mM][aA][xX][vV][aA][lL][uU][eE]				{ csql_yylval.cptr = pt_makename(yytext);
									  return NOMAXVALUE; }
[nN][oO][mM][iI][nN][vV][aA][lL][uU][eE]				{ csql_yylval.cptr = pt_makename(yytext);
									  return NOMINVALUE; }
[nN][oO][nN][eE]							{ csql_yylval.cptr = pt_makename(yytext);
									  return NONE; }
[nN][oO][tT]								{ return NOT; }
[nN][tT][hH]_[vV][aA][lL][uU][eE]					{ csql_yylval.cptr = pt_makename(yytext);
									  return NTH_VALUE; }
[nN][tT][iI][lL][eE]							{ csql_yylval.cptr = pt_makename(yytext);
									  return NTILE; }
[nN][uU][lL][lL][iI][fF]						{ return NULLIF; }
[nN][uU][mM][eE][rR][iI][cC]						{ return NUMERIC; }
[nN][uU][lL][lL]							{ return Null; }
[nN][uU][lL][lL][sS]							{ csql_yylval.cptr = pt_makename(yytext);
									  return NULLS; }
[oO][bB][jJ][eE][cC][tT]						{ return OBJECT; }
[oO][cC][tT][eE][tT]_[lL][eE][nN][gG][tT][hH]				{ return OCTET_LENGTH; }
[oO][fF]								{ return OF; }
[oO][fF][fF]								{ return OFF_; }
[oO][fF][fF][sS][eE][tT]				                { csql_yylval.cptr = pt_makename(yytext);
									  return OFFSET; }
[oO][nN]								{ return ON_; }                                                                                
[oO][nN][lL][iI][nN][eE]						{ csql_yylval.cptr = pt_makename(yytext);
                                                                          return ONLINE; }
[oO][nN][lL][yY]							{ return ONLY; }
[oO][oO][sS]								{ csql_yylval.cptr = pt_makename(yytext);
									  return OOS; }
[oO][pP][eE][nN]							{ csql_yylval.cptr = pt_makename(yytext);
									  return OPEN; }
[oO][pP][tT][iI][mM][iI][zZ][aA][tT][iI][oO][nN]			{ return OPTIMIZATION; }
[oO][pP][tT][iI][oO][nN]						{ return OPTION; }
[oO][rR]								{ return OR; }
[oO][rR][dD][eE][rR]							{ return ORDER; }
[oO][rR][dD][iI][nN][aA][lL][iI][tT][yY]				{ csql_yylval.cptr = pt_makename(yytext);
                                                                          return ORDINALITY; }
[oO][uU][tT]								{ return OUT_; }
[oO][uU][tT][eE][rR]							{ return OUTER; }
[oO][uU][tT][pP][uU][tT]						{ return OUTPUT; }
[oO][vV][eE][rR]							{ csql_yylval.cptr = pt_makename(yytext);
                                                                          return OVER; }
[oO][vV][eE][rR][lL][aA][pP][sS]					{ return OVERLAPS; }
[oO][wW][nN][eE][rR]							{ csql_yylval.cptr = pt_makename(yytext);
									  return OWNER; }
[pP][aA][gG][eE]							{ csql_yylval.cptr = pt_makename(yytext);
									  return PAGE; }
                                                                                
[pP][aA][rR][aA][lL][lL][eE][lL]					{ csql_yylval.cptr = pt_makename(yytext);
                                                                          return PARALLEL; }										
[pP][aA][rR][aA][mM][eE][tT][eE][rR][sS]				{ return PARAMETERS; }
[pP][aA][rR][tT][iI][aA][lL]						{ return PARTIAL; }
[pP][aA][rR][tT][iI][tT][iI][oO][nN]					{ csql_yylval.cptr = pt_makename(yytext);
									  return PARTITION; }
[pP][aA][rR][tT][iI][tT][iI][oO][nN][iI][nN][gG]			{ csql_yylval.cptr = pt_makename(yytext);
									  return PARTITIONING; }
[pP][aA][rR][tT][iI][tT][iI][oO][nN][sS]				{ csql_yylval.cptr = pt_makename(yytext);
									  return PARTITIONS; }
[pP][aA][sS][sS][wW][oO][rR][dD]					{ csql_yylval.cptr = pt_makename(yytext);
									  return PASSWORD; }
[pP][aA][tT][hH]							{ csql_yylval.cptr = pt_makename(yytext);
									  return PATH; }
[pP][eE][rR][cC][eE][nN][tT][_][rR][aA][nN][kK]				{ csql_yylval.cptr = pt_makename(yytext);
									  return PERCENT_RANK; }
[pP][eE][rR][cC][eE][nN][tT][iI][lL][eE]_[cC][oO][nN][tT]	        { csql_yylval.cptr = pt_makename(yytext);
									  return PERCENTILE_CONT; }
[pP][eE][rR][cC][eE][nN][tT][iI][lL][eE]_[dD][iI][sS][cC]	        { csql_yylval.cptr = pt_makename(yytext);
									  return PERCENTILE_DISC; }
[pP][lL][cC][sS][qQ][lL]				                { csql_yylval.cptr = pt_makename(yytext);
									  return PLCSQL; }
[pP][oO][rR][tT]        					        { csql_yylval.cptr = pt_makename(yytext);
									  return PORT; }
[pP][oO][sS][iI][tT][iI][oO][nN]					{ return POSITION; }
[pP][rR][eE][cC][iI][sS][iI][oO][nN]					{ return PRECISION; }
[pP][rR][eE][fF][eE][rR][_][iI][nN][lL][iI][nN][eE]			{ csql_yylval.cptr = pt_makename(yytext);
										  return PREFER_INLINE; }
[pP][rR][eE][fF][eE][rR][_][oO][uU][tT][lL][iI][nN][eE]			{ csql_yylval.cptr = pt_makename(yytext);
										  return PREFER_OUTLINE; }
[pP][rR][eE][pP][aA][rR][eE]						{ return PREPARE; }
[pP][rR][eE][sS][eE][rR][vV][eE]					{ return PRESERVE; }
[pP][rR][iI][mM][aA][rR][yY]						{ return PRIMARY; }
[pP][rR][iI][nN][tT]							{ csql_yylval.cptr = pt_makename(yytext);
									  return PRINT; }
[pP][rR][iI][oO][rR]							{ return PRIOR; }
[pP][rR][iI][oO][rR][iI][tT][yY]					{ csql_yylval.cptr = pt_makename(yytext);
									  return PRIORITY; }
[pP][rR][iI][vV][aA][tT][eE]						{ csql_yylval.cptr = pt_makename(yytext);
									  return PRIVATE; }
[pP][rR][iI][vV][iI][lL][eE][gG][eE][sS]				{ return PRIVILEGES; }
[pP][rR][oO][cC][eE][dD][uU][rR][eE]					{ return PROCEDURE; }
[pP][rR][oO][mM][oO][tT][eE]						{ csql_yylval.cptr = pt_makename(yytext);
                                                                          return PROMOTE; }
[pP][rR][oO][pP][eE][rR][tT][iI][eE][sS]				{ csql_yylval.cptr = pt_makename(yytext);
									  return PROPERTIES; }
[pP][uU][bB][lL][iI][cC]						{ csql_yylval.cptr = pt_makename(yytext);
									  return PUBLIC; }
[qQ][uU][aA][rR][tT][eE][rR]						{ csql_yylval.cptr = pt_makename(yytext);
									  return QUARTER; }
[qQ][uU][eE][rR][yY]							{ return QUERY; }
[qQ][uU][eE][uU][eE][sS]						{ csql_yylval.cptr = pt_makename(yytext);
									  return QUEUES; }
[rR][aA][nN][kK]							{ csql_yylval.cptr = pt_makename(yytext);
									  return RANK; }
[rR][aA][nN][gG][eE]							{ csql_yylval.cptr = pt_makename(yytext);
									  return RANGE_; }
[rR][eE][aA][dD]							{ return READ; }
[rR][eE][aA][lL]							{ return FLOAT_; }
[rR][eE][bB][uU][iI][lL][dD] 						{ csql_yylval.cptr = pt_makename(yytext);
									  return REBUILD; }
[rR][eE][cC][uU][rR][sS][iI][vV][eE]					{ return RECURSIVE; }
[rR][eE][fF]								{ return REF; }
[rR][eE][fF][eE][rR][eE][nN][cC][eE][sS]				{ return REFERENCES; }
[rR][eE][fF][eE][rR][eE][nN][cC][iI][nN][gG]				{ return REFERENCING; }
[rR][eE][gG][eE][xX][pP]						{ csql_yylval.cptr = pt_makename(yytext);
                                                                          return REGEXP; }
[rR][eE][gG][eE][xX][pP][_][cC][oO][uU][nN][tT]				{ csql_yylval.cptr = pt_makename(yytext);
									  return REGEXP_COUNT; }
[rR][eE][gG][eE][xX][pP][_][iI][nN][sS][tT][rR]				{ csql_yylval.cptr = pt_makename(yytext);
									  return REGEXP_INSTR; }
[rR][eE][gG][eE][xX][pP][_][lL][iI][kK][eE]				{ csql_yylval.cptr = pt_makename(yytext);
									  return REGEXP_LIKE; }
[rR][eE][gG][eE][xX][pP][_][rR][eE][pP][lL][aA][cC][eE]			{ csql_yylval.cptr = pt_makename(yytext);
									  return REGEXP_REPLACE; }
[rR][eE][gG][eE][xX][pP][_][sS][uU][bB][sS][tT][rR]			{ csql_yylval.cptr = pt_makename(yytext);
									  return REGEXP_SUBSTR; }
[rR][eE][jJ][eE][cC][tT]						{ csql_yylval.cptr = pt_makename(yytext);
									  return REJECT_; }
[rR][eE][lL][aA][tT][iI][vV][eE]					{ return RELATIVE_; }
[rR][eE][mM][oO][vV][eE]						{ csql_yylval.cptr = pt_makename(yytext);
									  return REMOVE; }
[rR][eE][nN][aA][mM][eE]						{ return RENAME; }
[rR][eE][oO][rR][gG][aA][nN][iI][zZ][eE]				{ csql_yylval.cptr = pt_makename(yytext);
									  return REORGANIZE; }
[rR][eE][pP][eE][aA][tT][aA][bB][lL][eE]				{ csql_yylval.cptr = pt_makename(yytext);
									  return REPEATABLE; }
[rR][eE][pP][lL][aA][cC][eE]						{ return REPLACE; }
[rR][eE][sS][iI][gG][nN][aA][lL]					{ return RESIGNAL; }
[rR][eE][sS][pP][eE][cC][tT]						{ csql_yylval.cptr = pt_makename(yytext);
									  return RESPECT; }
[rR][eE][sS][tT][rR][iI][cC][tT]					{ return RESTRICT; }
[rR][eE][tT][aA][iI][nN]						{ csql_yylval.cptr = pt_makename(yytext);
									  return RETAIN; }
[rR][eE][tT][uU][rR][nN]						{ return RETURN; }
[rR][eE][tT][uU][rR][nN][sS]						{ return RETURNS; }
[rR][eE][uU][sS][eE]_[oO][iI][dD]					{ csql_yylval.cptr = pt_makename(yytext);
									  return REUSE_OID; }
[rR][eE][vV][eE][rR][sS][eE]						{ csql_yylval.cptr = pt_makename(yytext);
									  return REVERSE; }
[dD][iI][sS][kK][_][sS][iI][zZ][Ee]				        { csql_yylval.cptr = pt_makename(yytext);
									  return DISK_SIZE; }
[rR][eE][vV][oO][kK][eE]						{ return REVOKE; }
[rR][iI][gG][hH][tT]							{ return RIGHT; }
[rR][lL][iI][kK][eE]							{ csql_yylval.cptr = pt_makename(yytext);
                                                                          return RLIKE; }
[rR][oO][lL][eE]							{ return ROLE; }
[rR][oO][lL][lL][bB][aA][cC][kK]					{ return ROLLBACK; }
[rR][oO][lL][lL][uU][pP]						{ return ROLLUP; }
[rR][oO][uU][tT][iI][nN][eE]						{ return ROUTINE; }
[rR][oO][wW]								{ return ROW; }
[rR][oO][wW]_[nN][uU][mM][bB][eE][rR]					{ csql_yylval.cptr = pt_makename(yytext);
									  return ROW_NUMBER; }
[rR][oO][wW][nN][uU][mM]						{ return ROWNUM; }
[rR][oO][wW][sS]							{ return ROWS; }
[sS][aA][vV][eE][pP][oO][iI][nN][tT]					{ return SAVEPOINT; }
[sS][cC][hH][eE][mM][aA]						{ return SCHEMA; }
[sS][cC][oO][pP][eE]___						 	{ return SCOPE; }
[sS][cC][rR][oO][lL][lL]						{ return SCROLL; }
[sS][eE][aA][rR][cC][hH]						{ return SEARCH; }
[sS][eE][cC][oO][nN][dD]						{ return SECOND_; }
[sS][eE][cC][oO][nN][dD]_[mM][iI][lL][lL][iI][sS][eE][cC][oO][nN][dD]	{ return SECOND_MILLISECOND; }
[sS][eE][cC][tT][iI][oO][nN]						{ return SECTION; }
[sS][eE][cC][tT][iI][oO][nN][sS]					{ csql_yylval.cptr = pt_makename(yytext);
									  return SECTIONS; }
[sS][eE][lL][eE][cC][tT]						{ return SELECT; }
[sS][eE][nN][sS][iI][tT][iI][vV][eE]					{ return SENSITIVE; }
[sS][eE][pP][aA][rR][aA][tT][oO][rR]					{ csql_yylval.cptr = pt_makename(yytext);
									  return SEPARATOR; }
[sS][eE][qQ][uU][eE][nN][cC][eE]					{ return SEQUENCE; }
[sS][eE][qQ][uU][eE][nN][cC][eE]_[oO][fF]			 	{ return SEQUENCE_OF; }
[sS][eE][rR][iI][aA][lL]						{ csql_yylval.cptr = pt_makename(yytext);
									  return SERIAL; }
[sS][eE][rR][iI][aA][lL][iI][zZ][aA][bB][lL][eE]			{ return SERIALIZABLE; }
[sS][eE][rR][vV][eE][rR]					        { csql_yylval.cptr = pt_makename(yytext);
									  return SERVER; }
[sS][eE][sS][sS][iI][oO][nN] 						{ return SESSION; }
[sS][eE][sS][sS][iI][oO][nN][tT][iI][mM][eE][zZ][oO][nN][eE]            { csql_yylval.cptr = pt_makename(yytext);
                                                                          return SESSION_TIMEZONE; }
[sS][eE][sS][sS][iI][oO][nN]_[uU][sS][eE][rR]				{ return SESSION_USER; }
[sS][eE][tT]								{ return SET; }
[sS][eE][tT][eE][qQ]							{ return SETEQ; }
[sS][eE][tT][nN][eE][qQ]						{ return SETNEQ; }
[sS][eE][tT]_[oO][fF]							{ return SET_OF; }
[sS][hH][aA][rR][eE][dD]						{ return SHARED; }
[sS][hH][oO][wW]							{ csql_yylval.cptr = pt_makename(yytext);
									  return SHOW; }
[sS][iI][bB][lL][iI][nN][gG][sS]					{ return SIBLINGS; }
[sS][hH][oO][rR][tT]							{ return SmallInt; }
[sS][iI][mM][iI][lL][aA][rR]						{ return SIMILAR; }
[sS][iI][zZ][eE]							{ return SIZE_; }
[sS][lL][oO][tT][sS]							{ csql_yylval.cptr = pt_makename(yytext);
									  return SLOTS; }
[sS][lL][oO][tT][tT][eE][dD]						{ csql_yylval.cptr = pt_makename(yytext);
									  return SLOTTED; }
[sS][oO][mM][eE]							{ return SOME; }
[sS][qQ][lL]								{ return SQL; }
[sS][qQ][lL][cC][oO][dD][eE]						{ return SQLCODE; }
[sS][qQ][lL][eE][rR][rR][oO][rR]					{ return SQLERROR; }
[sS][qQ][lL][eE][xX][cC][eE][pP][tT][iI][oO][nN]			{ return SQLEXCEPTION; }
[sS][qQ][lL][sS][tT][aA][tT][eE]					{ return SQLSTATE; }
[sS][qQ][lL][wW][aA][rR][nN][iI][nN][gG]				{ return SQLWARNING; }
[sS][tT][aA][bB][iI][lL][iI][tT][yY]					{ csql_yylval.cptr = pt_makename(yytext);
									  return STABILITY; }
[sS][tT][aA][rR][tT]							{ csql_yylval.cptr = pt_makename(yytext);
									  return START_; }
[sS][tT][aA][tT][eE][mM][eE][nN][tT]					{ csql_yylval.cptr = pt_makename(yytext);
									  return STATEMENT; }
[sS][tT][aA][tT][iI][sS][tT][iI][cC][sS]				{ return STATISTICS; }
[sS][tT][aA][tT][uU][sS]						{ csql_yylval.cptr = pt_makename(yytext);
									  return STATUS; }
[sS][tT][dD][dD][eE][vV]						{ csql_yylval.cptr = pt_makename(yytext);
									  return STDDEV; }
[sS][tT][dD][dD][eE][vV][_][pP][oO][pP]					{ csql_yylval.cptr = pt_makename(yytext);
									  return STDDEV_POP; }
[sS][tT][dD][dD][eE][vV][_][sS][aA][mM][pP]				{ csql_yylval.cptr = pt_makename(yytext);
									  return STDDEV_SAMP; }
[sS][tT][oO][rR][aA][gG][eE]						{ csql_yylval.cptr = pt_makename(yytext);
									  return STORAGE; }
[sS][tT][rR][_][tT][oO][_][dD][aA][tT][eE]				{ csql_yylval.cptr = pt_makename(yytext);
									  return STR_TO_DATE; }
[sS][uU][bB][cC][lL][aA][sS][sS]					{ return SUBCLASS; }
[sS][uU][bB][dD][aA][tT][eE]						{ csql_yylval.cptr = pt_makename(yytext);
									  return SUBDATE; }
[sS][uU][bB][sS][eE][tT]						{ return SUBSET; }
[sS][uU][bB][sS][eE][tT][eE][qQ]					{ return SUBSETEQ; }
[sS][uU][bB][sS][tT][rR][iI][nN][gG]					{ return SUBSTRING_; }
[sS][uU][mM]								{ return SUM; }
[sS][uU][pP][eE][rR][cC][lL][aA][sS][sS]				{ return SUPERCLASS; }
[sS][uU][pP][eE][rR][sS][eE][tT]					{ return SUPERSET; }
[sS][uU][pP][eE][rR][sS][eE][tT][eE][qQ]				{ return SUPERSETEQ; }
[sS][yY][nN][oO][nN][yY][mM]						{ csql_yylval.cptr = pt_makename(yytext);
									  return SYNONYM; }
[sS][yY][sS][_][cC][oO][nN][nN][eE][cC][tT][_][bB][yY][_][pP][aA][tT][hH]	{ return SYS_CONNECT_BY_PATH; }
[sS][yY][sS]_[dD][aA][tT][eE]						{ return SYS_DATE; }
[sS][yY][sS][dD][aA][tT][eE]						{ return SYS_DATE; }
[sS][yY][sS]_[tT][iI][mM][eE]						{ return SYS_TIME_; }
[sS][yY][sS][tT][iI][mM][eE]						{ return SYS_TIME_; }
[sS][yY][sS]_[tT][iI][mM][eE][sS][tT][aA][mM][pP]			{ return SYS_TIMESTAMP; }
[sS][yY][sS][tT][iI][mM][eE][sS][tT][aA][mM][pP]			{ return SYS_TIMESTAMP; }
[sS][yY][sS]_[dD][aA][tT][eE][tT][iI][mM][eE] 				{ return SYS_DATETIME; }
[sS][yY][sS][dD][aA][tT][eE][tT][iI][mM][eE] 				{ return SYS_DATETIME; }
[sS][yY][sS][_][rR][eE][fF][cC][uU][rR][sS][oO][rR]			{ return SYS_REFCURSOR; }
[sS][yY][sS][tT][eE][mM]						{ csql_yylval.cptr = pt_makename(yytext);
									  return SYSTEM; }
[sS][yY][sS][tT][eE][mM]_[uU][sS][eE][rR]				{ return SYSTEM_USER; }
[sS][mM][aA][lL][lL][iI][nN][tT]					{ return SmallInt; }
[sS][tT][rR][iI][nN][gG]						{ return String; }
[tT][aA][bB][lL][eE]							{ return TABLE; }
[tT][aA][bB][lL][eE][sS]						{ csql_yylval.cptr = pt_makename(yytext);
									  return TABLES; }
[tT][eE][mM][pP][oO][rR][aA][rR][yY]					{ return TEMPORARY; }
[tT][eE][xX][tT]							{ csql_yylval.cptr = pt_makename(yytext);
									  return TEXT; }
[tT][hH][eE][nN]							{ return THEN; }
[tT][hH][rR][eE][aA][dD][sS]						{ csql_yylval.cptr = pt_makename(yytext);
									  return THREADS; }
[tT][iI][mM][eE][oO][uU][tT]						{ csql_yylval.cptr = pt_makename(yytext);
									  return TIMEOUT; }
[tT][iI][mM][eE][sS][tT][aA][mM][pP]					{ return TIMESTAMP; }
[tT][iI][mM][eE][sS][tT][aA][mM][pP][tT][zZ]				{ return TIMESTAMPTZ; }
[tT][iI][mM][eE][sS][tT][aA][mM][pP][lL][tT][zZ]			{ return TIMESTAMPLTZ; }
[tT][iI][mM][eE][zZ][oO][nN][eE]					{ csql_yylval.cptr = pt_makename(yytext);
                                                                          return TIMEZONE; }
[tT][iI][mM][eE][zZ][oO][nN][eE][sS]					{ csql_yylval.cptr = pt_makename(yytext);
                                                                          return TIMEZONES; }
[tT][iI][mM][eE][zZ][oO][nN][eE]_[hH][oO][uU][rR]			{ return TIMEZONE_HOUR; }
[tT][iI][mM][eE][zZ][oO][nN][eE]_[mM][iI][nN][uU][tT][eE]		{ return TIMEZONE_MINUTE; }
[tT][oO]								{ return TO; }
[tT][rR][aA][cC][eE]							{ csql_yylval.cptr = pt_makename(yytext);
									  return TRACE; }
[tT][rR][aA][iI][lL][iI][nN][gG]					{ return TRAILING_; }
[tT][rR][aA][nN]							{ csql_yylval.cptr = pt_makename(yytext);
									  return TRAN; }
[tT][rR][aA][nN][sS][aA][cC][tT][iI][oO][nN]				{ return TRANSACTION; }
[tT][rR][aA][nN][sS][lL][aA][tT][eE]					{ return TRANSLATE; }
[tT][rR][aA][nN][sS][lL][aA][tT][iI][oO][nN]				{ return TRANSLATION; }
[tT][rR][iI][gG][gG][eE][rR]						{ return TRIGGER; }
[tT][rR][iI][gG][gG][eE][rR][sS]					{ csql_yylval.cptr = pt_makename(yytext);
									  return TRIGGERS; }
[tT][rR][iI][mM]							{ return TRIM; }
[tT][hH][aA][nN]							{ csql_yylval.cptr = pt_makename(yytext);
									  return THAN; }
[tT][iI][mM][eE]							{ return Time; }
[tT][iI][nN][yY][iI][nN][tT]				                { return SmallInt; }
[tT][rR][uU][eE]							{ return True; }
[tT][rR][uU][nN][cC][aA][tT][eE]					{ return TRUNCATE; }
[tT][yY][pP][eE]                					{ csql_yylval.cptr = pt_makename(yytext);
									  return TYPE; }
[uU][cC][aA][sS][eE]							{ csql_yylval.cptr = pt_makename(yytext);
									  return UCASE; }
[uU][nN][cC][oO][mM][mM][iI][tT][tT][eE][dD]				{ csql_yylval.cptr = pt_makename(yytext);
									  return UNCOMMITTED; }
[uU][nN][dD][eE][rR]							{ return UNDER; }
[uU][nN][iI][qQ][uU][eE]						{ return UNIQUE; }
[uU][nN][kK][nN][oO][wW][nN]						{ return UNKNOWN; }
[uU][pP][dD][aA][tT][eE]						{ return UPDATE; }
[uU][pP][pP][eE][rR]							{ return UPPER; }
[uU][sS][aA][gG][eE]							{ return USAGE; }
[uU][sS][eE]								{ return USE; }
[uU][sS][eE][rR]							{ return USER; }
[uU][sS][iI][nN][gG]							{ return USING; }
[uU][nN][iI][oO][nN]							{ return Union; }
[uU][tT][iI][mM][eE]							{ return Utime; }
[vV][aA][cC][uU][uU][mM]						{ return VACUUM; }
[vV][aA][lL][uU][eE]							{ return VALUE; }
[vV][aA][lL][uU][eE][sS]						{ return VALUES; }
[vV][aA][rR][_][pP][oO][pP]						{ csql_yylval.cptr = pt_makename(yytext);
									  return VAR_POP; }
[vV][aA][rR][_][sS][aA][mM][pP]						{ csql_yylval.cptr = pt_makename(yytext);
									  return VAR_SAMP; }
[vV][aA][rR][cC][hH][aA][rR]						{ return VARCHAR; }
[vV][aA][rR][iI][aA][bB][lL][eE]					{ return VARIABLE_; }
[vV][aA][rR][iI][aA][nN][cC][eE]					{ csql_yylval.cptr = pt_makename(yytext);
									  return VARIANCE; }
[vV][aA][rR][yY][iI][nN][gG]						{ return VARYING; }
[vV][cC][lL][aA][sS][sS]						{ return VCLASS; }
[vV][iI][eE][wW]							{ return VIEW; }
[vV][iI][sS][iI][bB][lL][eE]						{ csql_yylval.cptr = pt_makename(yytext);
									  return VISIBLE; }
[vV][oO][lL][uU][mM][eE]						{ csql_yylval.cptr = pt_makename(yytext);
									  return VOLUME; }
[wW][eE][eE][kK]							{ csql_yylval.cptr = pt_makename(yytext);
									  return WEEK; }
[wW][hH][eE][nN]							{ return WHEN; }
[wW][hH][eE][nN][eE][vV][eE][rR]					{ return WHENEVER; }
[wW][hH][eE][rR][eE]							{ return WHERE; }
[wW][hH][iI][lL][eE]							{ return WHILE; }
[wW][iI][tT][hH]							{ return WITH; }
[wW][iI][tT][hH][iI][nN]					        { csql_yylval.cptr = pt_makename(yytext);
									  return WITHIN; }
[wW][iI][tT][hH][oO][uU][tT]						{ return WITHOUT; }
[wW][oO][rR][kK]							{ return WORK; }
[wW][oO][rR][kK][sS][pP][aA][cC][eE]					{ csql_yylval.cptr = pt_makename(yytext);
									  return WORKSPACE; }
[wW][rR][iI][tT][eE]							{ return WRITE; }
[xX][oO][rR]								{ return XOR; }
[yY][eE][aA][rR]							{ return YEAR_; }
[yY][eE][aA][rR]_[mM][oO][nN][tT][hH]      				{ return YEAR_MONTH; }
[zZ][oO][nN][eE]							{ return ZONE; }

\\N									{ return Null; }

";"                                                                     {
                                                                                if (parser_yyinput_single_mode) {
                                                                                    // Pretend to be an EOF
                                                                                    // to make yyparse() return.
                                                                                    // (parse a single statement at a time)
                                                                                    // NOTE: at the next call to yylex(),
                                                                                    // the work starts at the last state of
                                                                                    // the lexer.
                                                                                    return 0;
                                                                                } else {
                                                                                    return SEMICOLON;
                                                                                }
                                                                         }
"->"									{ return RIGHT_ARROW; }
"."									{ return DOT; }
"->>"									{ return DOUBLE_RIGHT_ARROW; }
"||"									{
									  if (!prm_get_bool_value (PRM_ID_PIPES_AS_CONCAT))
									    return OR;
									  else
									    return STRCAT;
									}

"<>"									{ return COMP_NOT_EQ; }
">="									{ return COMP_GE; }
"<="									{ return COMP_LE; }
"?:"									{ return PARAM_HEADER; }
"!="									{ return COMP_NOT_EQ; }
"<<"									{ return BITSHIFT_LEFT; }
">>"									{ return BITSHIFT_RIGHT; }
"%"									{ return MOD; }
"<=>"									{ return COMP_NULLSAFE_EQ; }
"&&"									{ return AND; }
":="									{ return VAR_ASSIGN; }


[0-9]+									{
										int i = 0;
										int len = 0;

										len = strlen(yytext);
										if (len >= INTEGER_MAX_LITERAL)
											yytext[INTEGER_MAX_LITERAL] = 0;

										/* loop until len - 1, in case of 0000000000000000000000 */
										for (i = 0; i < len - 1 && yytext[i] == '0'; i++)
										;

										csql_yylval.cptr = pt_makename(&yytext[i]);
										return UNSIGNED_INTEGER;
									}


(([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])\.){3}([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5]) {
                                                                                
										
										csql_yylval.cptr = pt_makename(yytext);
										return IPV4_ADDRESS;
                                                                        }

([0-9]+[Ee][\+\-]?[0-9]+[fF]?)|([0-9]*\.[0-9]+([Ee][\+\-]?[0-9]+)?[fF]?)|([0-9]+\.[0-9]*([Ee][\+\-]?[0-9]+)?[fF]?) 	{
										char buff[FLOAT_LITERAL_BUFFER];
										int len;

										len = strlen(yytext);
										if (len >= FLOAT_MAX_LITERAL)
											yytext[FLOAT_MAX_LITERAL] = 0;

										if (len < FLOAT_APPENDABLE_TRAILING_ZERO && yytext[len-1] == '.')
										  {
										    strncpy(buff, yytext, sizeof(buff));
										    buff[len] = '0';
										    buff[len+1] = '\0';
										    csql_yylval.cptr = pt_makename(buff);
										  }
										else
										  {
										    csql_yylval.cptr = pt_makename(yytext);
										  }
										return UNSIGNED_REAL;
									}

_[eE][uU][cC][kK][rR][']	{
					BEGIN (QUOTED_EUCKR_STRING);
					csql_yylval.cptr = pt_append_string(this_parser, NULL, "");
				}

_[iI][sS][oO]88591[']		{
					BEGIN (QUOTED_ISO_STRING);
					csql_yylval.cptr = pt_append_string(this_parser, NULL, "");
				}

_[bB][iI][nN][aA][rR][yY][']	{
					BEGIN (QUOTED_BINARY_STRING);
					csql_yylval.cptr = pt_append_string(this_parser, NULL, "");
				}

_[uU][tT][fF]8[']		{
					BEGIN (QUOTED_UTF8_STRING);
					csql_yylval.cptr = pt_append_string(this_parser, NULL, "");
				}

([a-zA-Z_#]|([\xc0-\xfe]))([a-zA-Z_#0-9]|([\x80-\xfe]))* {
							   if (strlen(yytext) >= 254)
							     yytext[254] = 0;
							   csql_yylval.cptr = pt_makename(yytext);
							   return IdName;
						         }

"."[ \t\r\n]*([a-zA-Z_#]|([\xc0-\xfe]))([a-zA-Z_#0-9]|([\x80-\xfe]))* {
                                                            CSQL_YY_LESS(1);
                                                            BEGIN(POST_DOT_OR_RIGHT_ARROW_STRING);
							    return DOT;
							}

"->"[ \t\r\n]*([a-zA-Z_#]|([\xc0-\xfe]))([a-zA-Z_#0-9]|([\x80-\xfe]))* {
                                                           CSQL_YY_LESS(2);
                                                           BEGIN(POST_DOT_OR_RIGHT_ARROW_STRING);
							   return RIGHT_ARROW;
							}

<POST_DOT_OR_RIGHT_ARROW_STRING>[ \t\r\n]*([a-zA-Z_#]|([\xc0-\xfe]))([a-zA-Z_#0-9]|([\x80-\xfe]))* {
				        int i = 0;
				        int len = 0;
				      	  
                                        BEGIN(INITIAL);
                                       
                                        len = strlen(yytext);
                                        if (len >= 254)
                                          {
                                             yytext[254] = 0;
                                          }

                                        for (i = 0; i < len - 1 && IS_BLANK_CHAR (yytext[i]); i++)
                                           ;

                                        csql_yylval.cptr = pt_makename(&yytext[i]);

                                        /* Distinguish some special words/tokens from normal identifiers (IdName).
                                         * Because these special words (companioned with DOT/RIGHT_ARROW) are
                                         * reserved for special use purpose in some clauses' context.
                                         */
                                        if (strcasecmp (csql_yylval.cptr, "NONE") == 0)
                                          {
                                             return NONE;
                                          }
                                        else if (strcasecmp (csql_yylval.cptr, "IDENTITY") == 0)
                                          {
                                             return IDENTITY;
                                          }
                                        else if (strcasecmp (csql_yylval.cptr, "OBJECT") == 0)
                                          {
                                             return OBJECT;
                                          }

                                        return IdName;
				}

0b[0-1]+			{
					csql_yylval.cptr = pt_append_string(this_parser, NULL, yytext + 2);
					return BIT_STRING;
				}

0x[0-9a-fA-F]+			{
					csql_yylval.cptr = pt_append_string(this_parser, NULL, yytext + 2);                                        
					return HEX_STRING;
				}

[nN][']				{
					BEGIN (QUOTED_NCHAR_STRING);
					csql_yylval.cptr = pt_append_string(this_parser, NULL, "");
				}

[bB]['] 			{
					BEGIN (QUOTED_BIT_STRING);
					csql_yylval.cptr = pt_append_string(this_parser, NULL, "");
				}

[xX][']				{
					BEGIN (QUOTED_HEX_STRING);
					csql_yylval.cptr = pt_append_string(this_parser, NULL, "");
				}

['] 				{
					BEGIN (QUOTED_CHAR_STRING);
					csql_yylval.cptr = pt_append_string(this_parser, NULL, "");
				}

["] 				{
					if (prm_get_bool_value (PRM_ID_ANSI_QUOTES) && (is_dblink_query_string == 0))
					  {
					    BEGIN(DELIMITED_ID_NAME);
					  }
					else
					  {
					    BEGIN(DOUBLY_QUOTED_CHAR_STRING);
					  }
					csql_yylval.cptr = pt_append_string(this_parser, NULL, "");
				}

"["				{
					BEGIN(BRACKET_ID_NAME);
					csql_yylval.cptr = pt_append_string(this_parser, NULL, "");
				}

[`] 				{
					BEGIN(BACKTICK_ID_NAME);
					csql_yylval.cptr = pt_append_string(this_parser, NULL, "");
				}

<QUOTED_EUCKR_STRING>[']	{
					BEGIN(INITIAL);
					return EUCKR_STRING;
				}

<QUOTED_ISO_STRING>[']		{
					BEGIN(INITIAL);
					return ISO_STRING;
				}

<QUOTED_BINARY_STRING>[']	{
					BEGIN(INITIAL);
					return BINARY_STRING;
				}

<QUOTED_UTF8_STRING>[']		{
					BEGIN(INITIAL);
					return UTF8_STRING;
				}

<QUOTED_NCHAR_STRING>[']	{
					BEGIN(INITIAL);
					return NCHAR_STRING;
				}

<QUOTED_BIT_STRING>[']		{
					BEGIN(INITIAL);
					return BIT_STRING;
				}

<QUOTED_HEX_STRING>[']		{
					BEGIN(INITIAL);
					return HEX_STRING;
				}

<QUOTED_CHAR_STRING>[']		{
                                        if (from_plcsql_text) {
                                                from_plcsql_text = 0;
                                                BEGIN(PLCSQL_TEXT);
                                        } else {
                                                BEGIN(INITIAL);
                                        }
					return CHAR_STRING;
				}

<DOUBLY_QUOTED_CHAR_STRING>["]	{
					BEGIN(INITIAL);
					return CHAR_STRING;
				}

<DELIMITED_ID_NAME>["]		{
                                        if (from_plcsql_text) {
                                                from_plcsql_text = 0;
                                                BEGIN(PLCSQL_TEXT);
                                        } else {
                                                BEGIN(INITIAL);
                                        }
					if (strlen(csql_yylval.cptr) >= 254)
					  {
					    csql_yylval.cptr[254] = 0;
					  }
					return DelimitedIdName;
				}

<BRACKET_ID_NAME>"]"		{
                                        if (from_plcsql_text) {
                                                from_plcsql_text = 0;
                                                BEGIN(PLCSQL_TEXT);
                                        } else {
                                                BEGIN(INITIAL);
                                        }
					if (strlen(csql_yylval.cptr) >= 254)
					  {
					    csql_yylval.cptr[254] = 0;
					  }
					return BracketDelimitedIdName;
				}

<BACKTICK_ID_NAME>"`"		{
                                        if (from_plcsql_text) {
                                                from_plcsql_text = 0;
                                                BEGIN(PLCSQL_TEXT);
                                        } else {
                                                BEGIN(INITIAL);
                                        }
					if (strlen(csql_yylval.cptr) >= 254)
					  {
					    csql_yylval.cptr[254] = 0;
					  }
					return BacktickDelimitedIdName;
				}

<QUOTED_NCHAR_STRING,QUOTED_BIT_STRING,QUOTED_HEX_STRING,QUOTED_CHAR_STRING,DOUBLY_QUOTED_CHAR_STRING,DELIMITED_ID_NAME,BRACKET_ID_NAME,BACKTICK_ID_NAME,QUOTED_EUCKR_STRING,QUOTED_ISO_STRING,QUOTED_BINARY_STRING,QUOTED_UTF8_STRING>\n	{
					csql_yylval.cptr = pt_append_string(this_parser, csql_yylval.cptr, "\n");
				}

<QUOTED_NCHAR_STRING,QUOTED_BIT_STRING,QUOTED_HEX_STRING,QUOTED_CHAR_STRING,DOUBLY_QUOTED_CHAR_STRING,QUOTED_EUCKR_STRING,QUOTED_ISO_STRING,QUOTED_BINARY_STRING,QUOTED_UTF8_STRING><<EOF>>	{
                                        from_plcsql_text = 0;
					BEGIN(INITIAL);
					csql_yyerror("unterminated string");
					return UNTERMINATED_STRING;
				}

<DELIMITED_ID_NAME,BRACKET_ID_NAME,BACKTICK_ID_NAME><<EOF>>	{
                                        from_plcsql_text = 0;
					BEGIN(INITIAL);
					csql_yyerror("unterminated identifier");
					return UNTERMINATED_IDENTIFIER;
				}

<QUOTED_NCHAR_STRING,QUOTED_CHAR_STRING,QUOTED_EUCKR_STRING,QUOTED_ISO_STRING,QUOTED_BINARY_STRING,QUOTED_UTF8_STRING>[^'\n\\]+	{
					csql_yylval.cptr = pt_append_string(this_parser, csql_yylval.cptr, yytext);
				}

<QUOTED_BIT_STRING,QUOTED_HEX_STRING>[^'\n]+	{
					csql_yylval.cptr = pt_append_string(this_parser, csql_yylval.cptr, yytext);
				}

<DOUBLY_QUOTED_CHAR_STRING>[^"\n\\]+	{
					csql_yylval.cptr = pt_append_string(this_parser, csql_yylval.cptr, yytext);
				}

<DELIMITED_ID_NAME>[^"\n]+	{
					csql_yylval.cptr = pt_append_string(this_parser, csql_yylval.cptr, yytext);
				}

<BRACKET_ID_NAME>[^\]\n]+	{
					csql_yylval.cptr = pt_append_string(this_parser, csql_yylval.cptr, yytext);
				}

<BACKTICK_ID_NAME>[^\`\n]+	{
					csql_yylval.cptr = pt_append_string(this_parser, csql_yylval.cptr, yytext);
				}

<QUOTED_NCHAR_STRING,QUOTED_CHAR_STRING,QUOTED_EUCKR_STRING,QUOTED_ISO_STRING,QUOTED_BINARY_STRING,QUOTED_UTF8_STRING>['][']	{
                                        if (from_plcsql_text) {
                                            csql_yylval.cptr = pt_append_string(this_parser, csql_yylval.cptr, "''");
                                        } else {
                                            csql_yylval.cptr = pt_append_string(this_parser, csql_yylval.cptr, "'");
                                        }
				}

<DOUBLY_QUOTED_CHAR_STRING,DELIMITED_ID_NAME>["]["]	{
                                        if (from_plcsql_text) {
                                            csql_yylval.cptr = pt_append_string(this_parser, csql_yylval.cptr, "\"\"");
                                        } else {
                                            csql_yylval.cptr = pt_append_string(this_parser, csql_yylval.cptr, "\"");
                                        }
				}

<BACKTICK_ID_NAME>[`][`]	{
                                        if (from_plcsql_text) {
                                            csql_yylval.cptr = pt_append_string(this_parser, csql_yylval.cptr, "``");
                                        } else {
                                            csql_yylval.cptr = pt_append_string(this_parser, csql_yylval.cptr, "`");
                                        }
				}

<QUOTED_NCHAR_STRING,QUOTED_CHAR_STRING,DOUBLY_QUOTED_CHAR_STRING,QUOTED_EUCKR_STRING,QUOTED_ISO_STRING,QUOTED_BINARY_STRING,QUOTED_UTF8_STRING>{

"\\n"				{
					csql_yylval.cptr = pt_append_string(this_parser, csql_yylval.cptr,
									    (prm_get_bool_value (PRM_ID_NO_BACKSLASH_ESCAPES) || this_parser->flag.strings_have_no_escapes) ? "\\n" : "\n");
				}

"\\r"				{
					csql_yylval.cptr = pt_append_string(this_parser, csql_yylval.cptr,
									    (prm_get_bool_value (PRM_ID_NO_BACKSLASH_ESCAPES) || this_parser->flag.strings_have_no_escapes) ? "\\r" : "\r");
				}

"\\t"				{
					csql_yylval.cptr = pt_append_string(this_parser, csql_yylval.cptr,
									    (prm_get_bool_value (PRM_ID_NO_BACKSLASH_ESCAPES) || this_parser->flag.strings_have_no_escapes) ? "\\t" : "\t");
				}

"\\%"				{
					csql_yylval.cptr = pt_append_string(this_parser, csql_yylval.cptr, "\\%");
				}

"\\_"				{
					csql_yylval.cptr = pt_append_string(this_parser, csql_yylval.cptr, "\\_");
				}

[\\]['"]			{
					if (prm_get_bool_value (PRM_ID_NO_BACKSLASH_ESCAPES) || this_parser->flag.strings_have_no_escapes)
					  {
					    csql_yylval.cptr = pt_append_string(this_parser, csql_yylval.cptr, "\\");
                                            CSQL_YY_LESS(1);
					  }
					else
					  {
					    csql_yylval.cptr = pt_append_string(this_parser, csql_yylval.cptr, yytext + 1);
					  }
				}

[\\].				{
					csql_yylval.cptr = pt_append_string(this_parser, csql_yylval.cptr,
							   (prm_get_bool_value (PRM_ID_NO_BACKSLASH_ESCAPES) || this_parser->flag.strings_have_no_escapes) ? yytext : yytext + 1);
				}

[\\]\n				{
					if (prm_get_bool_value (PRM_ID_NO_BACKSLASH_ESCAPES) || this_parser->flag.strings_have_no_escapes)
					  {
					    csql_yylval.cptr = pt_append_string(this_parser, csql_yylval.cptr, "\\\n");
					  }
					else
					  {
					    csql_yylval.cptr = pt_append_string(this_parser, csql_yylval.cptr, "\n");
					  }
				}

}


<INITIAL,PL_LANG_SPEC>{
        "/*+"                  {
                                  saved_yy_start = YY_START;
                                  BEGIN(BLOCK_HINT);
                                  csql_yylval.cptr = pt_append_string(this_parser, NULL, "+");
                                }
        "/*"                    {  saved_yy_start = YY_START; 
                                   BEGIN(BLOCK_COMMENT); }
                                   
        "//+"  |	
        "--+"	                {  saved_yy_start = YY_START;
                                   BEGIN(LINE_HINT);
                                   csql_yylval.cptr = pt_append_string(this_parser, NULL, "+");
                                }

        "//"   |
        "--"	                {  saved_yy_start = YY_START;
                                   BEGIN(LINE_COMMENT);  }
                       }

<BLOCK_HINT>{
        [^*\n]+  {   csql_yylval.cptr = pt_append_string(this_parser, csql_yylval.cptr, yytext); }
        "\n"     { 
                     csql_yylval.cptr = pt_append_string(this_parser, csql_yylval.cptr, "\n");
                 }
        "*/"     {
                     BEGIN(saved_yy_start);
                     if (parser_check_hint (csql_yylval.cptr))
                        {
			  return SQL_HINT;
                        }
                 }
        "*"      {   csql_yylval.cptr = pt_append_string(this_parser, csql_yylval.cptr, "*"); }
        <<EOF>>  { 
                     BEGIN(saved_yy_start);
                     return UNEXPECTED_EOF;                     
                 }
              }

<BLOCK_COMMENT>{
        [^*\n]+  {   /* no action */;  }
        "\n"     {   /* no action */;  }
        "*/"     {   BEGIN(saved_yy_start);   }
        "*"      {   /* no action */;  }
        <<EOF>>  {   
                     BEGIN(saved_yy_start); 
                     return UNEXPECTED_EOF;  
                 }
              }

<LINE_HINT>{
        [^\n]+   { csql_yylval.cptr = pt_append_string(this_parser, csql_yylval.cptr, yytext);  }
        "\n"     {
                     csql_yylval.cptr = pt_append_string(this_parser, csql_yylval.cptr, "\n");
                     BEGIN(saved_yy_start);
                     if (parser_check_hint (csql_yylval.cptr))
                        {
			  return SQL_HINT;
                        }
                  }
        <<EOF>>  {
                     BEGIN(saved_yy_start);
                     if (parser_check_hint (csql_yylval.cptr))
                        {
			  return SQL_HINT;
                        }
                  }
          }
<LINE_COMMENT>{
        [^\n]+   { /* no action */; }
        "\n"     { BEGIN(saved_yy_start);  }
        <<EOF>>  { BEGIN(saved_yy_start);  }
             }

\xa1\xef				{ return YEN_SIGN; }
\$   					{ return DOLLAR_SIGN; }
\\   					{ return WON_SIGN; }
\\[T][L]				{ return TURKISH_LIRA_SIGN; }
\\[J][P][Y]				{ return YEN_SIGN; }
\\[U][S][D]				{ return DOLLAR_SIGN; }
\\[K][R][W]				{ return WON_SIGN; }
\\[T][R][Y]				{ return TURKISH_LIRA_SIGN; }
\\[K][H][R]				{ return CAMBODIAN_RIEL_SIGN; }
\\[C][N][Y]				{ return CHINESE_RENMINBI_SIGN; }
\\[I][N][R]				{ return INDIAN_RUPEE_SIGN; }
\\[R][U][B]				{ return RUSSIAN_RUBLE_SIGN; }
\\[A][U][D]				{ return AUSTRALIAN_DOLLAR_SIGN; }
\\[C][A][D]				{ return CANADIAN_DOLLAR_SIGN; }
\\[B][R][L]				{ return BRASILIAN_REAL_SIGN; }
\\[R][O][N]				{ return ROMANIAN_LEU_SIGN; }
\\[E][U][R]				{ return EURO_SIGN; }
\\[C][H][F]				{ return SWISS_FRANC_SIGN; }
\\[D][K][K]				{ return DANISH_KRONE_SIGN; }
\\[N][O][K]				{ return NORWEGIAN_KRONE_SIGN; }
\\[B][G][N]				{ return BULGARIAN_LEV_SIGN; }
\\[G][B][P]				{ return BRITISH_POUND_SIGN; }
\\[V][N][D]				{ return VIETNAMESE_DONG_SIGN; }
\\[C][Z][K]				{ return CZECH_KORUNA_SIGN; }
\\[P][L][N]				{ return POLISH_ZLOTY_SIGN; }
\\[S][E][K]				{ return SWEDISH_KRONA_SIGN; }
\\[H][R][K]				{ return CROATIAN_KUNA_SIGN; }
\\[R][S][D]				{ return SERBIAN_DINAR_SIGN; }

.					{ return yytext[0]; }

 /* mode PL_LANG_SPEC */

<PL_LANG_SPEC>[lL][aA][nN][gG][uU][aA][gG][eE]          { return LANGUAGE; }
<PL_LANG_SPEC>[jJ][aA][vV][aA]                          {
                                                                BEGIN(INITIAL);
							        csql_yylval.cptr = pt_makename(yytext);
                                                                return JAVA; }
<PL_LANG_SPEC>[pP][lL][cC][sS][qQ][lL]                  {
                                                                BEGIN(PLCSQL_TEXT);
                                                                init_plcsql_lexing();
								csql_yylval.cptr = pt_makename(yytext);
                                                                return PLCSQL; }

<PL_LANG_SPEC>[ \t\r]+					{ /* no action */; }
<PL_LANG_SPEC>\n					{ /* no action */; }

<PL_LANG_SPEC><<EOF>> 					{
                                                                BEGIN(INITIAL);
                                                                csql_yyerror("unterminated PL language spec");
                                                                return UNTERMINATED_PL_LANG_SPEC; }

<PL_LANG_SPEC>{IDL}+                                    {
                                                                BEGIN(PLCSQL_TEXT);
                                                                init_plcsql_lexing();
                                                                CSQL_YY_LESS(0);
                                                        }

<PL_LANG_SPEC>.                                         {
                                                                BEGIN(PLCSQL_TEXT);
                                                                init_plcsql_lexing();
                                                                CSQL_YY_LESS(0);
                                                        }

 /* mode PLCSQL_TEXT */

<PLCSQL_TEXT>"/*"([^*]*[*]+[^*/])*[^*]*[*]+"/"	        |
<PLCSQL_TEXT>[ \t\r]+					{
							        csql_yylval.cptr = pt_makename(yytext);
                                                                return PLCSQL_TEXT_SOME; }
<PLCSQL_TEXT>"//".*\n                                   |
<PLCSQL_TEXT>"--".*\n	                                |
<PLCSQL_TEXT>\n					        {
							        csql_yylval.cptr = pt_makename(yytext);
                                                                return PLCSQL_TEXT_SOME; }
<PLCSQL_TEXT><<EOF>> 					{
                                                                BEGIN(INITIAL);
                                                                csql_yyerror("unterminated PL/CSQL text");
                                                                return UNTERMINATED_PL_LANG_SPEC; }
<PLCSQL_TEXT>['] 				        {
                                                                from_plcsql_text = 1;
                                                                BEGIN (QUOTED_CHAR_STRING);
                                                                csql_yylval.cptr = pt_append_string(this_parser, NULL, ""); }
<PLCSQL_TEXT>["] 				        {
                                                                from_plcsql_text = 1;
                                                                BEGIN (DELIMITED_ID_NAME);
                                                                csql_yylval.cptr = pt_append_string(this_parser, NULL, ""); }
<PLCSQL_TEXT>"[" 				        {
                                                                from_plcsql_text = 1;
                                                                BEGIN (BRACKET_ID_NAME);
                                                                csql_yylval.cptr = pt_append_string(this_parser, NULL, ""); }
<PLCSQL_TEXT>"`" 				        {
                                                                from_plcsql_text = 1;
                                                                BEGIN (BACKTICK_ID_NAME);
                                                                csql_yylval.cptr = pt_append_string(this_parser, NULL, ""); }


<PLCSQL_TEXT>[bB][eE][gG][iI][nN]                       |
<PLCSQL_TEXT>[iI][fF]                                   |
<PLCSQL_TEXT>[lL][oO][oO][pP]                           {
                                                                plcsql_begin_end_balance++;
							        csql_yylval.cptr = pt_makename(yytext);
                                                                return PLCSQL_TEXT_SOME; }
<PLCSQL_TEXT>[cC][aA][sS][eE]                           {
                                                                // case can start an expression and can appear in a balance 0 area
                                                                if (plcsql_begin_end_balance == 0) {
                                                                        plcsql_nest_level++;
                                                                }
                                                                plcsql_begin_end_balance++;
							        csql_yylval.cptr = pt_makename(yytext);
                                                                return PLCSQL_TEXT_SOME; }
<PLCSQL_TEXT>[eE][nN][dD][ \t\r\n]+[iI][fF][ \t\r\n]*             |
<PLCSQL_TEXT>[eE][nN][dD][ \t\r\n]+[cC][aA][sS][eE][ \t\r\n]*     |
<PLCSQL_TEXT>[eE][nN][dD][ \t\r\n]+[lL][oO][oO][pP][ \t\r\n]*     |
<PLCSQL_TEXT>[eE][nN][dD][ \t\r\n]*                     {
                                                                plcsql_begin_end_balance--;
                                                                if (plcsql_begin_end_balance < 0) {
                                                                        // syntax error.
                                                                        // ignore this for now and let the PL/CSQL parser raise the error
                                                                        plcsql_begin_end_balance = 0;
                                                                }
                                                                if (plcsql_begin_end_balance == 0) {
                                                                        plcsql_nest_level--;
                                                                        if (plcsql_nest_level < 0) {
                                                                                BEGIN(POST_PLCSQL_TEXT);
                                                                        }
                                                                }
							        csql_yylval.cptr = pt_makename(yytext);
                                                                return PLCSQL_TEXT_SOME; }

<PLCSQL_TEXT>[pP][rR][oO][cC][eE][dD][uU][rR][eE]       {
                                                                if (plcsql_begin_end_balance == 0) {
                                                                        // increase the nest level only when it is not in a block statement
                                                                        plcsql_nest_level++;
                                                                }
							        csql_yylval.cptr = pt_makename(yytext);
                                                                return PLCSQL_TEXT_SOME; }
<PLCSQL_TEXT>[fF][uU][nN][cC][tT][iI][oO][nN]           {
                                                                if (plcsql_begin_end_balance == 0) {
                                                                        // increase the nest level only when it is not in a block statement
                                                                        plcsql_nest_level++;
                                                                }
							        csql_yylval.cptr = pt_makename(yytext);
                                                                return PLCSQL_TEXT_SOME; }

<PLCSQL_TEXT>{IDL}+                                     {
							        csql_yylval.cptr = pt_makename(yytext);
                                                                return PLCSQL_TEXT_SOME; }

<PLCSQL_TEXT>. 					        {
							        csql_yylval.cptr = pt_makename(yytext);
                                                                return PLCSQL_TEXT_SOME; }

 /* mode POST_PLCSQL_TEXT */

<POST_PLCSQL_TEXT>[cC][oO][mM][mM][eE][nN][tT]          { BEGIN(INITIAL);
                                                          CSQL_YY_LESS(0);
                                                        }

<POST_PLCSQL_TEXT>{IDL}+                                { 
                                                           BEGIN(INITIAL);
                                                          
                                                           csql_yylval.cptr = pt_makename(yytext);
                                                           return PLCSQL_TEXT_SOME; 
                                                        }

<POST_PLCSQL_TEXT>(.)                                   {
                                                           BEGIN(INITIAL);
                                                           CSQL_YY_LESS(0);
                                                        }

<POST_PLCSQL_TEXT><<EOF>>                               {
                                                           BEGIN(INITIAL);
                                                           return 0; 
                                                        }

%%

END_SUPPRESS_WARNING_BISON_FLEX

void
csql_yyerror_explicit (int line, int column)
{
  csql_yyerror_internal (NULL, line, column);

  msg_ptr = 0;
}


void
csql_yyerror (const char *s)
{
   csql_yyerror_internal(s, -1, -1); 
}

static void
csql_yyerror_internal (const char *s, const int line, const int column)
{
  char *token_val = NULL;
  char *next_ptr = csql_yyget_text () + csql_yyget_leng () + 1;
  char saved_char = yy_hold_char; /* retain the character following the last
				   * character of the current token */
  char *end_of_stmt_str = NULL;
  char *context_saved_char_str = NULL;
  int end_statement = 0;
  int free_msg = 0;
  int token_len = csql_yyget_leng () + 1;
  int next_ptr_len = 0;
  char *buffer = NULL;

  /* make a copy of the token value before the buffer flush */
  if (CSQL_MAXNAME < csql_yyget_leng () + 1)
    {
      token_len = CSQL_MAXNAME;
    }
  token_val = (char *) csql_yyalloc (token_len);
  if (token_val == NULL)
    {
      YY_FATAL_ERROR( "out of dynamic memory in csql_yyerror()" );
    }
  snprintf (token_val, token_len, "%s", csql_yyget_text ());

  end_of_stmt_str = msgcat_message (MSGCAT_CATALOG_CUBRID,
				    MSGCAT_SET_PARSER_SYNTAX,
				    MSGCAT_SYNTAX_END_OF_STMT);
  context_saved_char_str = msgcat_message (MSGCAT_CATALOG_CUBRID,
					   MSGCAT_SET_PARSER_SYNTAX,
					   MSGCAT_SYNTAX_CONTEXT_SAVED_CHAR);

  next_ptr_len = strlen (next_ptr);
  if (next_ptr_len == 0)
    {
      end_statement = 1;
      next_ptr = end_of_stmt_str;
    }
  if (next_ptr_len > MAX_PRINT_ERROR_CONTEXT_LENGTH)
    {
      buffer = (char *) csql_yyalloc (MAX_PRINT_ERROR_CONTEXT_LENGTH + 1);
      memcpy (buffer, next_ptr, MAX_PRINT_ERROR_CONTEXT_LENGTH - 3);
      strcpy (buffer + MAX_PRINT_ERROR_CONTEXT_LENGTH - 3, "...");
      next_ptr = buffer;
      next_ptr_len = strlen (next_ptr);
    }

  YY_FLUSH_BUFFER;
  parser_statement_OK = 0;

  if (msg_ptr)
    {
      PT_NODE *dummy = parser_new_node (this_parser, PT_EXPR);
      if(dummy && line > 0 && column > 0)
        {
          dummy->line_number = line;
          dummy->column_number = column;
        }
      PT_ERRORm (this_parser, dummy, MSGCAT_SET_PARSER_SYNTAX,
		 g_msg[msg_ptr - 1]);
      parser_free_tree (this_parser, dummy);
    }
  else
    {
      const char *cmp_str = msgcat_message (MSGCAT_CATALOG_CUBRID,
					    MSGCAT_SET_PARSER_SYNTAX,
					    MSGCAT_SYNTAX_EXPECTING_BRACKET);
      const char *cmp_str2 = msgcat_message (MSGCAT_CATALOG_CUBRID,
					     MSGCAT_SET_PARSER_SYNTAX,
					     MSGCAT_SYNTAX_EXPECTING_SELECT);

      char *msg = NULL;
      const char *before_token = msgcat_message (MSGCAT_CATALOG_CUBRID,
						 MSGCAT_SET_PARSER_SYNTAX,
						 MSGCAT_SYNTAX_YYERROR);
      const char *after_token =
	strstr (s, msgcat_message (MSGCAT_CATALOG_CUBRID,
				   MSGCAT_SET_PARSER_SYNTAX,
				   MSGCAT_SYNTAX_EXPECTING));
      const char *token_type =
	strstr (s, msgcat_message (MSGCAT_CATALOG_CUBRID,
				   MSGCAT_SET_PARSER_SYNTAX,
				   MSGCAT_SYNTAX_UNEXPECTED));
      int len = 0;
      int len_s = strlen (s);
      int len_cmp_str = strlen (cmp_str);
      PT_NODE *dummy;
      
      if ((len_s > len_cmp_str) &&
	  (strcmp (&s[len_s - len_cmp_str], cmp_str) == 0))
	{
	  *(char *) &s[len_s - len_cmp_str] = 0;
	}
      else 
        {
          len_cmp_str = strlen (cmp_str2);
          if ((len_s > len_cmp_str) &&
	       (strcmp (&s[len_s - len_cmp_str], cmp_str2) == 0))
	    {
	      *(char *) &s[len_s - len_cmp_str] = 0;
	    }
        }

      if (strlen (token_val) == 0)
	{
	  token_type = NULL;
	}
      if (token_type)
	{
	  char *msg_context = NULL;
	  int fixed_len = 11 * 2; /* maximum size of printed line number and
				     column number */
	  int msg_size = fixed_len + next_ptr_len + strlen (before_token)
			 + strlen (token_val) + 4;
			 /* 4 = 3 more characters when printing before_token
			  * and 'token_val' + the string terminator */

	  if (end_statement == 0)
	    {
	      msg_size += strlen (context_saved_char_str);
	      msg_size += 1; /* one printed char in the message */
	    }
	  else
	    {
	      msg_context = msgcat_message (MSGCAT_CATALOG_CUBRID,
					    MSGCAT_SET_PARSER_SYNTAX,
					    MSGCAT_SYNTAX_CONTEXT);
	      msg_size += strlen (msg_context);
	    }

	  if (after_token)
	    {
              msg_size += strlen (after_token);
              // There is no need to reset the length as we will replace "$end" with "';'".              
	    }

	  msg = (char *) csql_yyalloc (msg_size);
	  if (msg == NULL)
	    {
	      YY_FATAL_ERROR( "out of dynamic memory in csql_yyerror()" );
	      goto end;
	    }
	  free_msg = 1;
	  if (end_statement == 0)
	    {
	      snprintf (msg, msg_size, context_saved_char_str,
		        csql_yylloc.first_line, csql_yylloc.first_column,
		        (saved_char ? saved_char : ' '), next_ptr);
	    }
	  else
	    {
	      assert (msg_context != NULL);
	      snprintf (msg, msg_size, msg_context,
		        csql_yylloc.first_line, csql_yylloc.first_column,
		        next_ptr);
	    }

	  len = strlen (msg);

	  if (msg[len - 3] == '\n')
	    {
	      msg[len - 3] = ' ';
	    }
	  sprintf (msg + len, "%s '%s'", before_token, token_val);

	  if (after_token)
	    {
              char* end_ptr = strstr (after_token, "$end");

              if (!end_ptr)
               {
                 strcat (msg, after_token);
               }
              else
               {
                 *end_ptr = '\0';
                 strcat (msg, after_token);
                 *end_ptr = '$';
                 end_ptr += 4; /* strlen("$end") */
                 strcat (msg, "';'");
                 if(*end_ptr)
                   {
                     strcat (msg, end_ptr);
                   }
               } 
	    }
	}
      else
	{
	  if (strstr (s, "$end"))
	    {
	      int msg_size;

	      next_ptr = end_of_stmt_str;
	      msg_size = strlen (before_token) + strlen (next_ptr) + 2;

	      msg = (char *) csql_yyalloc (msg_size);
	      if (msg == NULL)
		{
		  YY_FATAL_ERROR( "out of dynamic memory in csql_yyerror()" );
		  goto end;
		}
	      snprintf (msg, msg_size, "%s %s", before_token, next_ptr);
	      free_msg = 1;
	    }
	  else
	    {
	      msg = (char *) s;   // suppress warning; string will not be changed
	    }
	}

      dummy = parser_new_node (this_parser, PT_EXPR);
      if (dummy == NULL)
	{
	  if (free_msg && msg != NULL)
	    {
	      csql_yyfree (msg);
	    }
	  goto end;
	}
      if(dummy && line > 0 && column > 0)
        {
          dummy->line_number = line;
          dummy->column_number = column;
        }
      PT_ERRORf (this_parser, dummy, "%s", msg);
      parser_free_tree (this_parser, dummy);
      if (free_msg && msg != NULL)
	{
	  csql_yyfree (msg);
	}
   }

end:
  if (token_val != NULL)
    {
      csql_yyfree (token_val);
    }
  if (buffer)
    {
      csql_yyfree (buffer);
    }

  BEGIN(INITIAL);
}

int
yywrap ()
{
  return 1;
}


static void
init_plcsql_lexing (void)
{
  plcsql_begin_end_balance = 0;
  plcsql_nest_level = 0;
  from_plcsql_text = 0;
}

static bool
parser_yyinput_string_literal (char *buff, int max_size, int end_char, int *pos)
{
  int c;

  while ((*pos) < max_size)
    {
      c = pt_nextchar ();

      if (c == '\\' && (prm_get_bool_value (PRM_ID_NO_BACKSLASH_ESCAPES) == false))
	{
	  if ((*pos) >= max_size - 1)
	    {
	      return false;
	    }
	  else
	    {
	      buff[(*pos)++] = c;
	      /* if escape character */
	      c = pt_nextchar ();	/* read one more */
	      buff[(*pos)++] = c;
	    }
	}
      else
	{
	  buff[(*pos)++] = (char) c;
	  if (c == end_char || c == -1)
	    {
	      g_yyinput_type = enNone;
	      return true;
	    }
	}
    }
  return false;
}

/* Note: 
 * Double quotes "" are usually for identifiers, but they can signify string literals depending on the PRM_ID_ANSI_QUOTES configuration.
 * Also, query strings in a DBLINK should properly be enclosed in single quotes '', but an issue allows double quotes "" as well.
 * Since we cannot verify here if the processing occurs within a DBLINK, it creates an issue where we cannot distinguish whether "" reads an identifier or a string literal.
 * Moreover, PRM_ID_NO_BACKSLASH_ESCAPES matters only for string literals.
 * As a minimum safeguard, query strings in DBLINKs must be enclosed in single quotes within unloaddb
*/
static bool
parser_yyinput_identifier (char *buff, int max_size, int end_char, int *pos)
{
  int c;

  while ((*pos) < max_size)
    {
      c = pt_nextchar ();
      buff[(*pos)++] = (char) c;
      if (c == end_char || c == -1)
	{
	  g_yyinput_type = enNone;
	  return true;
	}
    }
  return false;
}

static bool
parser_yyinput_line_comment (char *buff, int max_size, int *pos)
{
  int c;

  while ((*pos) < max_size)
    {
      c = pt_nextchar ();
      buff[(*pos)++] = (char) c;
      if (c == '\n' || c == -1)
	{
	  g_yyinput_type = enNone;
	  return true;
	}
    }
  return false;
}

static bool
parser_yyinput_c_style_comment (char *buff, int max_size, int *pos)
{
  int c;

  while ((*pos) < max_size)
    {
      c = pt_nextchar ();
      buff[(*pos)++] = (char) c;
      if ((c == '/' && buff[(*pos) - 2] == '*') || (c == -1))
	{
	  g_yyinput_type = enNone;
	  return true;
	}
    }
  return false;
}

static int
parser_yyinput_single_line (char *buff, int max_size)
{
  int c;
  int i = 0;

  assert (max_size >= 1);

  do
    {
      switch (g_yyinput_type)
	{
	case ensingle_quoted_string:
	  if (parser_yyinput_string_literal (buff, max_size, '\'', &i) == false)
	    {
	      buff[i] = '\0';	/* not required but to ease debugging */
	      return i;
	    }
	  break;
	case endouble_quoted_string:
	  if ((prm_get_bool_value (PRM_ID_ANSI_QUOTES)
	       ? parser_yyinput_identifier (buff, max_size, '\"', &i) : 
                 parser_yyinput_string_literal (buff, max_size, '\"', &i)) == false)
	    {
	      buff[i] = '\0';	/* not required but to ease debugging */
	      return i;
	    }
	  break;

	case enbracket_quoted_string:
	  if (parser_yyinput_identifier (buff, max_size, ']', &i) == false)
	    {
	      buff[i] = '\0';	/* not required but to ease debugging */
	      return i;
	    }
	  break;
	case enbacktick_quoted_string:
	  if (parser_yyinput_identifier (buff, max_size, '`', &i) == false)
	    {
	      buff[i] = '\0';	/* not required but to ease debugging */
	      return i;
	    }
	  break;
	case enline_comment:
	  if (parser_yyinput_line_comment (buff, max_size, &i) == false)
	    {
	      buff[i] = '\0';	/* not required but to ease debugging */
	      return i;
	    }
	  break;
	case enC_style_comment:
	  if (parser_yyinput_c_style_comment (buff, max_size, &i) == false)
	    {
	      buff[i] = '\0';	/* not required but to ease debugging */
	      return i;
	    }
	  break;
        case enhyphen:
           c = pt_nextchar ();
           buff[i++] = (char) c;
           g_yyinput_type = ( c == '-') ? enline_comment : enNone;
           break;

        case enslash:
           c = pt_nextchar ();
           buff[i++] = (char) c;
           g_yyinput_type = ( c == '/') ? enline_comment : (( c == '*') ? enC_style_comment : enNone);
           break;

	case enNone:
	default:
	  {
	    c = pt_nextchar ();
	    switch (c)
	      {
	      case '[':
		g_yyinput_type = enbracket_quoted_string;
		break;
	      case '`':
		g_yyinput_type = enbacktick_quoted_string;
		break;
	      case '\'':
		g_yyinput_type = ensingle_quoted_string;
		break;
	      case '\"':
		g_yyinput_type = endouble_quoted_string;
		break;
	      case '-':
                g_yyinput_type = enhyphen;
                break;
	      case '/':
                g_yyinput_type = enslash;
                break;
	      case ';':
		buff[i++] = (char) c;
		buff[i] = '\0';	/* not required but to ease debugging */
		/* stop filling the buffer. see PR 5403 */
		return i;

	      default:
		break;
	      }

	    buff[i++] = (char) c;
	  }
	  break;
	}
    }
  while (buff[i - 1] != -1 && i < max_size);

  buff[i] = '\0';		/* not required but to ease debugging */
  return i;
}

static int
parser_yyinput_multi_line (char *buff, int max_size)
{
  int c;
  int i = 0;

  assert (max_size >= 1);
  for (;;)
    {
      c = pt_nextchar ();
      if (c == -1)
	{
	  buff[i] = 0;
	  return i;
	}

      buff[i++] = c;
      if (i == max_size)
	{
	  buff[i] = 0;
	  return i;
	}
    }
}

static int
parser_yyinput (char *buff, int max_size)
{
  if (!parser_yyinput_single_mode)
    {
      return parser_yyinput_multi_line (buff, max_size);
    }
  else
    {
      return parser_yyinput_single_line (buff, max_size);
    }
}

static bool
parser_check_hint (char *hint_string)
{
  PT_HINT_ENUM hint = PT_HINT_NONE;

  pt_check_hint (hint_string, parser_hint_table, &hint);
  if (hint == PT_HINT_NONE)
    {
      return false;
    }

  return true;
}

static void
parser_yy_user_action (void)
{
  if (yybuffer_pos == 0)
    {
      yyline_start_pos = 0;
      yylineno_prev = 0;
      this_parser->line = yylineno;
      this_parser->column = 0;
      yytoken_start_line = yylineno;
      yytoken_start_column = 0;
    }
    
  yyline_start_pos_bk = yyline_start_pos;

  if (YY_START == INITIAL)	/* token start (not a continuation) */
    {
      yytoken_start_line = csql_yylloc.first_line = yylineno;
      yytoken_start_column = csql_yylloc.first_column = parser_column_position();
    }

  if (yylineno_prev < yylineno)
    {
      char *last_newline = strrchr(yytext, '\n');
      if (last_newline != NULL)
        {
          yyline_start_pos = yybuffer_pos + (last_newline - yytext) + 1;
        }

      this_parser->line = yylineno;
    }

  yylineno_prev = yylineno;

  yybuffer_pos += csql_yyget_leng ();
  csql_yylloc.buffer_pos = yybuffer_pos;

  this_parser->column = parser_column_position();

  csql_yylloc.last_line = yylineno;
  csql_yylloc.last_column = this_parser->column;
}
