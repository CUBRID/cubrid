/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

/*
 * loader_old_lexer.l - loader lexer file
 */

%{
#include "loader.h"
#include "loader_old_grammar.h"

/*#define LEXER_DEBUG*/

#ifdef LEXER_DEBUG
#define PRINT printf("lex: ");printf
#else
#define PRINT(a, b)
#endif

int in_instance_line = 1;

extern void loader_yyerror(char* s);

static char string_buffer[1024 * 1024];
static int str_idx;

static void string_append(char c);
%}

%x DELIMITED_ID DQS SQS COMMENT

%%
[ \t]+ 						;
\r?\n						{ return NL; }

[Nn][Uu][Ll][Ll]				{ PRINT("NULL_ %s\n", yytext);  return NULL_; }
[Cc][Ll][Aa][Ss][Ss]				{ PRINT("CLASS %s\n", yytext);  return CLASS; }
[Ss][Hh][Aa][Rr][Ee][Dd]			{ PRINT("SHARED %s\n", yytext);  return SHARED; }
[Dd][Ee][Ff][Aa][Uu][Ll][Tt]			{ PRINT("DEFAULT%s\n", yytext);  return DEFAULT; }

[Dd][Aa][Tt][Ee]				{ PRINT("DATE %s\n", yytext);  return DATE; }
[Tt][Ii][Mm][Ee]				{ PRINT("TIME %s\n", yytext);  return TIME; }
[Uu][Tt][Ii][Mm][Ee]				{ PRINT("UTIME %s\n", yytext);  return UTIME; }
[Tt][Ii][Mm][Ee][Ss][Tt][Aa][Mm][Pp]		{ PRINT("TIMESTAMP %s\n", yytext);  return TIMESTAMP; }
[Dd][Aa][Tt][Ee][Tt][Ii][Mm][Ee]		{ PRINT("DATETIME %s\n", yytext);  return DATETIME; }

\%[Ii][Dd] 					{ PRINT("CMD_ID %s\n", yytext);  in_instance_line = 0; return CMD_ID; }
\%[Cc][Ll][Aa][Ss][Ss]				{ PRINT("CMD_CLASS %s\n", yytext);  in_instance_line = 0; return CMD_CLASS; }
\%[Cc][Oo][Nn][Ss][Tt][Rr][Uu][Cc][Tt][Oo][Rr]	{ PRINT("CMD_CONSTRUCTOR %s\n", yytext);  return CMD_CONSTRUCTOR; }

\^[Ii]						{ PRINT("REF_ELO_INT %s\n", yytext);  return REF_ELO_INT; }
\^[Ee]						{ PRINT("REF_ELO_EXT %s\n", yytext);  return REF_ELO_EXT; }
\^[Uu]						{ PRINT("REF_USER %s\n", yytext);  return REF_USER; }
\^[Cc]						{ PRINT("REF_CLASS %s\n", yytext);  return REF_CLASS; }

\@						{ PRINT("OBJECT_REFERENCE %s\n", yytext);  return OBJECT_REFERENCE; }
\|						{ PRINT("OID_DELIMETER %s\n", yytext);  return OID_DELIMETER; }

\{						{ PRINT("SET_START_BRACE %s\n", yytext);  return SET_START_BRACE; }
\}						{ PRINT("SET_END_BRACE %s\n", yytext);  return SET_END_BRACE; }

\(						{ PRINT("START_PAREN %s\n", yytext);  return START_PAREN; }
\)						{ PRINT("END_PAREN %s\n", yytext);  return END_PAREN; }

[\+\-]?(([0-9]+[Ee][\+\-]?[0-9]+[fFlL]?)|([0-9]*\.[0-9]+([Ee][\+\-]?[0-9]+)?[fFlL]?)|([0-9]+\.[0-9]*([Ee][\+\-]?[0-9]+)?[fFlL]?)) { 
							PRINT("REAL_LIT %s\n", yytext);
							loader_yylval.cptr = strdup(yytext);
							return REAL_LIT; 
						}
						
[\+\-]?[0-9]+					{ 
							PRINT("INT_LIT %s\n", yytext); 
							loader_yylval.cptr = strdup(yytext);
							return INT_LIT;
						}
						
[0-9]+:						{
							PRINT("OID %s\n", yytext);
							loader_yylval.intval = atoi(yytext);
							return OID_;
						}

[0-9]+:[0-9]+:[0-9]+[\ \t]*[aApP][mM]		{ 
							PRINT("TIME_LIT4 %s\n", yytext);
							loader_yylval.cptr = strdup(yytext);
							return TIME_LIT4;
						}
						
[0-9]+:[0-9]+:[0-9]+[\ \t]*			{ 
							PRINT("TIME_LIT42 %s\n", yytext);
							loader_yylval.cptr = strdup(yytext);
							return TIME_LIT42;
						}
						
[0-9]+:[0-9]+[\ \t]*[aApP][mM]			{ 
							PRINT("TIME_LIT3 %s\n", yytext);
							loader_yylval.cptr = strdup(yytext);
							return TIME_LIT3;
						}
						
[0-9]+:[0-9]+[\ \t]*				{ 
							PRINT("TIME_LIT31 %s\n", yytext);
							loader_yylval.cptr = strdup(yytext);
							return TIME_LIT31;
						}
						
[0-9]+:[0-9]+:[0-9]+				{ 
							PRINT("TIME_LIT2 %s\n", yytext);
							loader_yylval.cptr = strdup(yytext);
							return TIME_LIT2;
						}
						
[0-9]+:[0-9]+					{ 
							PRINT("TIME_LIT1 %s\n", yytext);
							loader_yylval.cptr = strdup(yytext);
							return TIME_LIT1;
						}
						
[0-9]+\/[0-9]+\/[0-9]+ 				{ 
							PRINT("DATE_LIT2 %s\n", yytext);
							loader_yylval.cptr = strdup(yytext);
							return DATE_LIT2;
						}

\xa1\xef					{ PRINT("YEN_SYMBOL %s\n", yytext);  return YEN_SYMBOL; }
\xa3\xdc					{ PRINT("WON_SYMBOL %s\n", yytext);  return WON_SYMBOL; }
\\						{ PRINT("BACKSLASH %s\n", yytext);  return BACKSLASH; }
\$						{ PRINT("DOLLAR_SYMBOL %s\n", yytext);  return DOLLAR_SYMBOL; }

([a-zA-Z_%#]|(\xa1[\xa2-\xee\xf3-\xfe])|([\xa2-\xfe][\xa1-\xfe])|(\x8e[\xa1-\xfe]))([a-zA-Z_%#0-9]|(\xa1[\xa2-\xfe])|([\xa1-\xfe])|(\x8e[\xa1-\xfe]))* { 
							PRINT("IDENTIFIER %s\n", yytext);
							loader_yylval.cptr = strdup(yytext);
							return IDENTIFIER;
						}

[\']      					{ PRINT("Quote %s\n", yytext);  BEGIN SQS; str_idx = 0; return Quote; }
[nN][\']  					{ PRINT("NQuote %s\n", yytext);  BEGIN SQS; str_idx = 0; return NQuote; }
[bB][\']  					{ PRINT("BQuote %s\n", yytext);  BEGIN SQS; str_idx = 0; return BQuote; }
[xX][\']  					{ PRINT("XQuote %s\n", yytext);  BEGIN SQS; str_idx = 0; return XQuote; }

\"  						{ str_idx = 0; 
						  if (in_instance_line) { BEGIN DQS; return DQuote;} 
						  else BEGIN DELIMITED_ID;
						}

\\\n  						{ printf("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n"); /* TODO */; }
","						{ PRINT("COMMA %s\n", yytext);  return COMMA; }

\/\/[^\r\n]*\r?\n				{ /* C++ comments */ }
\-\-[^\r\n]*\r?\n				{ /* SQL comments */ }
"/*"						{ BEGIN COMMENT; /* C comments */ }
<COMMENT>.					|
<COMMENT>\n					;
<COMMENT>"*/"					{ BEGIN INITIAL; }

<DELIMITED_ID>\"\"				{ string_append('"'); }
<DELIMITED_ID>[^\"]				string_append(yytext[0]);
<DELIMITED_ID>\"				{ 
							string_append('\0'); 
							str_idx = 0; 
							PRINT("IDENTIFIER %s\n", string_buffer); 
							BEGIN INITIAL; 
							loader_yylval.cptr = strdup(string_buffer);
							return IDENTIFIER;
						}

<DQS>\\n					{ string_append('\n'); }
<DQS>\\t					{ string_append('\t'); }
<DQS>\\f					{ string_append('\f'); }
<DQS>\\r					{ string_append('\r'); }
<DQS>\\[0-7]([0-7][0-7]?)?			{ string_append((char)strtol(&yytext[1], NULL, 8)); }
<DQS>\\x[0-9a-fA-F][0-9a-fA-F]?			{ string_append((char)strtol(&yytext[2], NULL, 16)); }
<DQS>[^\"]					string_append(yytext[0]);
<DQS>\\						{ /* TODO */; }
<DQS>\"						{ 
							string_append('\0');
							str_idx = 0; 
							PRINT("DQS_String_Body %s\n", string_buffer); 
							loader_yylval.cptr = strdup(string_buffer);
							BEGIN INITIAL; 
							return DQS_String_Body;
						}

<SQS>\'\'					{ string_append('\''); }
<SQS>[^\']					string_append(yytext[0]);
<SQS>\'\+[ \t]*\r?\n[ \t]*\'			{ ; /*string_append('\0'); PRINT("String_Prefix %s\n", string_buffer); BEGIN INITIAL; return String_Prefix;*/ }
<SQS>\'[ \t]					{ 
							string_append('\0');
							str_idx = 0; 
							PRINT("String_Completion %s\n", string_buffer); 
							loader_yylval.cptr = strdup(string_buffer);
							BEGIN INITIAL; 
							return SQS_String_Body;
						}
<SQS>\'						{ 
							string_append('\0'); 
							str_idx = 0; 
							PRINT("String_Completion2 %s\n", string_buffer); 
							loader_yylval.cptr = strdup(string_buffer);
							BEGIN INITIAL; 
							return SQS_String_Body;
						}

%%

void loader_yyerror(char* s)
{
}

int loader_yywrap()
{
	#ifdef LEXER_DEBUG
	printf("Parsing done.....\n");
	#endif
	return 1;
}

static void string_append(char c)
{
	string_buffer[str_idx] = c;
	str_idx++;
}
