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
 * csql_lexer.l - SQL lexer file
 */






%{
#include "csql_grammar.h"
#include "parse_tree.h"
#include "system_parameter.h"


#define CSQL_MAXNAME	256

static int parser_yyinput_single_line (char *buff, int max_size);
static int parser_yyinput_multi_line (char *buff, int max_size);
static int parser_yyinput (char *buff, int max_size);
static char *parser_c_hint (void);
static char *parser_line_hint (void);
static void begin_token (char *token);

extern int yyline;
extern int yycolumn;
extern int yycolumn_end;






#undef YY_INPUT
#define YY_INPUT(buffer, result, max_size) { result = parser_yyinput(buffer, max_size); result == 0 ? result = YY_NULL : result; }



%}

%x QUOTED_NCHAR_STRING
%x QUOTED_BIT_STRING
%x QUOTED_HEX_STRING
%x QUOTED_CHAR_STRING
%x DOUBLY_QUOTED_CHAR_STRING
%x DELIMITED_ID_NAME
%x BRACKET_ID_NAME
%x BACKTICK_ID_NAME




%%

[ \t\r]+								{ begin_token(yytext); }
\n									{
										begin_token(yytext); 
										this_parser->line = yyline++;
										this_parser->column = yycolumn = yycolumn_end = 1;
									}



[aA][bB][sS][oO][lL][uU][tT][eE]					{ begin_token(yytext);   return ABSOLUTE_; }
[aA][cC][tT][iI][oO][nN]						{ begin_token(yytext);   return ACTION; }
[aA][cC][tT][iI][vV][eE]						{ begin_token(yytext);  
										csql_yylval.cptr = pt_makename(yytext);
										return ACTIVE; }
[aA][dD][dD]								{ begin_token(yytext);   return ADD; }
[aA][dD][dD][dD][aA][tT][eE]						{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return ADDDATE; }
[aA][dD][dD]_[mM][oO][nN][tT][hH][sS]					{ begin_token(yytext);   return ADD_MONTHS; }
[aA][fF][tT][eE][rR]							{ begin_token(yytext);   return AFTER; }
[aA][lL][iI][aA][sS]							{ begin_token(yytext);   return ALIAS; }
[aA][lL][lL]								{ begin_token(yytext);   return ALL; }
[aA][lL][lL][oO][cC][aA][tT][eE]					{ begin_token(yytext);   return ALLOCATE; }
[aA][lL][tT][eE][rR]							{ begin_token(yytext);   return ALTER; }
[aA][nN][aA][lL][yY][zZ][eE]						{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return ANALYZE; }
[aA][nN][dD]								{ begin_token(yytext);   return AND; }
[aA][nN][yY]								{ begin_token(yytext);   return ANY; }
[aA][rR][eE]								{ begin_token(yytext);   return ARE; }
[aA][sS]								{ begin_token(yytext);   return AS; }
[aA][sS][cC]								{ begin_token(yytext);   return ASC; }
[aA][sS][sS][eE][rR][tT][iI][oO][nN]					{ begin_token(yytext);   return ASSERTION; }
[aA][sS][yY][nN][cC]							{ begin_token(yytext);   return ASYNC; }
[aA][tT]								{ begin_token(yytext);   return AT; }
[aA][tT][tT][aA][cC][hH]						{ begin_token(yytext);   return ATTACH; }
[aA][tT][tT][rR][iI][bB][uU][tT][eE]					{ begin_token(yytext);   return ATTRIBUTE; }
[aA][uU][tT][oO]_[iI][nN][cC][rR][eE][mM][eE][nN][tT]			{ begin_token(yytext);  
										csql_yylval.cptr = pt_makename(yytext);
										return AUTO_INCREMENT; }
[aA][vV][gG]								{ begin_token(yytext);   return AVG; }
[bB][eE][fF][oO][rR][eE]						{ begin_token(yytext);   return BEFORE; }
[bB][eE][tT][wW][eE][eE][nN]						{ begin_token(yytext);   return BETWEEN; }
[bB][iI][gG][iI][nN][tT]						{ begin_token(yytext);   return BIGINT;}
[bB][iI][nN][aA][rR][yY]						{ begin_token(yytext);   return BINARY;}
[bB][iI][tT]								{ begin_token(yytext);   return BIT; }
[bB][iI][tT]_[lL][eE][nN][gG][tT][hH]					{ begin_token(yytext);   return BIT_LENGTH; }
[bB][iI][tT]_[aA][nN][dD]						{ begin_token(yytext);	 
										csql_yylval.cptr = pt_makename(yytext);
										return BIT_AND; }
[bB][iI][tT]_[oO][rR]							{ begin_token(yytext);	 
										csql_yylval.cptr = pt_makename(yytext);
										return BIT_OR; }
[bB][iI][tT]_[xX][oO][rR]						{ begin_token(yytext);	 
										csql_yylval.cptr = pt_makename(yytext);
										return BIT_XOR; }
[bB][lL][oO][bB]							{ begin_token(yytext);	 return BLOB_; }
[bB][oO][oO][lL][eE][aA][nN]						{ begin_token(yytext);   return BOOLEAN_; }
[bB][oO][tT][hH]							{ begin_token(yytext);   return BOTH_; }
[bB][rR][eE][aA][dD][tT][hH]						{ begin_token(yytext);   return BREADTH; }
[bB][yY]								{ begin_token(yytext);   return BY; }
[cC][aA][lL][lL]							{ begin_token(yytext);   return CALL; }
[cC][aA][cC][hH][eE]							{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return CACHE; }
[cC][aA][sS][cC][aA][dD][eE]						{ begin_token(yytext);   return CASCADE; }
[cC][aA][sS][cC][aA][dD][eE][dD]					{ begin_token(yytext);   return CASCADED; }
[cC][aA][sS][eE]							{ begin_token(yytext);   return CASE; }
[cC][aA][sS][tT]							{ begin_token(yytext);   return CAST; }
[cC][aA][tT][aA][lL][oO][gG]						{ begin_token(yytext);   return CATALOG; }
[cC][hH][aA][nN][gG][eE]						{ begin_token(yytext);   return CHANGE; }
[cC][hH][aA][rR][aA][cC][tT][eE][rR]					{ begin_token(yytext);   return CHAR_; }
[cC][hH][eE][cC][kK]							{ begin_token(yytext);   return CHECK; }
[cC][lL][aA][sS][sS]							{ begin_token(yytext);   return CLASS; }
[cC][lL][aA][sS][sS][eE][sS]						{ begin_token(yytext);   return CLASSES; }
[cC][lL][oO][bB]							{ begin_token(yytext);   return CLOB_; }
[cC][lL][oO][sS][eE]							{ begin_token(yytext);   return CLOSE; }
[cC][lL][uU][sS][tT][eE][rR]						{ begin_token(yytext);   return CLUSTER; }
[cC][oO][aA][lL][eE][sS][cC][eE]					{ begin_token(yytext);   return COALESCE; }
[cC][oO][lL][lL][aA][tT][eE]						{ begin_token(yytext);   return COLLATE; }
[cC][oO][lL][lL][aA][tT][iI][oO][nN]					{ begin_token(yytext);   return COLLATION; }
[cC][oO][lL][uU][mM][nN]						{ begin_token(yytext);   return COLUMN; }
[cC][oO][lL][uU][mM][nN][sS]						{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return COLUMNS; }
[cC][oO][mM][mM][iI][tT]						{ begin_token(yytext);   return COMMIT; }
[cC][oO][mM][mM][iI][tT][tT][eE][dD]					{ begin_token(yytext); 
										csql_yylval.cptr = pt_makename(yytext);
										return COMMITTED; }
[cC][oO][mM][pP][lL][eE][tT][iI][oO][nN]				{ begin_token(yytext);   return COMPLETION; }
[cC][oO][nN][nN][eE][cC][tT]						{ begin_token(yytext);   return CONNECT; }
[cC][oO][nN][nN][eE][cC][tT][_][bB][yY][_][iI][sS][cC][yY][cC][lL][eE]	{ begin_token(yytext);   return CONNECT_BY_ISCYCLE; }
[cC][oO][nN][nN][eE][cC][tT][_][bB][yY][_][iI][sS][lL][eE][aA][fF]	{ begin_token(yytext);   return CONNECT_BY_ISLEAF; }
[cC][oO][nN][nN][eE][cC][tT][_][bB][yY][_][rR][oO][oO][tT]		{ begin_token(yytext);   return CONNECT_BY_ROOT; }
[cC][oO][nN][nN][eE][cC][tT][iI][oO][nN]				{ begin_token(yytext);   return CONNECTION; }
[cC][oO][nN][sS][tT][rR][aA][iI][nN][tT]				{ begin_token(yytext);   return CONSTRAINT; }
[cC][oO][nN][sS][tT][rR][aA][iI][nN][tT][sS]				{ begin_token(yytext);   return CONSTRAINTS; }
[cC][oO][nN][tT][iI][nN][uU][eE]					{ begin_token(yytext);   return CONTINUE; }
[cC][oO][nN][vV][eE][rR][tT]						{ begin_token(yytext);   return CONVERT; }
[cC][oO][rR][rR][eE][sS][pP][oO][nN][dD][iI][nN][gG]			{ begin_token(yytext);   return CORRESPONDING; }
[cC][oO][sS][tT]							{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return COST; 
									}
[cC][oO][uU][nN][tT]							{ begin_token(yytext);   return COUNT; }
[cC][rR][eE][aA][tT][eE]						{ begin_token(yytext);   return CREATE; }
[cC][rR][oO][sS][sS]							{ begin_token(yytext);   return CROSS; }
[cC][uU][rR][rR][eE][nN][tT]						{ begin_token(yytext);   return CURRENT; }
[cC][uU][rR][rR][eE][nN][tT]_[dD][aA][tT][eE]				{ begin_token(yytext);   return CURRENT_DATE; }
[cC][uU][rR][rR][eE][nN][tT]_[tT][iI][mM][eE]				{ begin_token(yytext);   return CURRENT_TIME; }
[cC][uU][rR][rR][eE][nN][tT]_[tT][iI][mM][eE][sS][tT][aA][mM][pP]	{ begin_token(yytext);   return CURRENT_TIMESTAMP; }
[cC][uU][rR][rR][eE][nN][tT]_[dD][aA][tT][eE][tT][iI][mM][eE] 		{ begin_token(yytext);   return CURRENT_DATETIME; }
[cC][uU][rR][rR][eE][nN][tT]_[uU][sS][eE][rR]				{ begin_token(yytext);   return CURRENT_USER; }
[cC][uU][rR][sS][oO][rR]						{ begin_token(yytext);   return CURSOR; }
[cC][yY][cC][lL][eE]							{ begin_token(yytext);   return CYCLE; }
[cC][hH][aA][rR]							{ begin_token(yytext);   return CHAR_; }
[dD][aA][tT][aA]							{ begin_token(yytext);   return DATA; }
[dD][aA][tT][aA][bB][aA][sS][eE]					{ begin_token(yytext);   return DATABASE; }
[dD][aA][tT][aA]_[tT][yY][pP][eE]___					{ begin_token(yytext);   return DATA_TYPE; }
[dD][aA][tT][eE]_[aA][dD][dD]						{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return DATE_ADD; }
[dD][aA][tT][eE]_[sS][uU][bB]						{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return DATE_SUB; }
[dD][aA][tT][eE][tT][iI][mM][eE]					{ begin_token(yytext);   return DATETIME; }
[dD][aA][yY]								{ begin_token(yytext);   return DAY_; }
[dD][aA][yY]_[mM][iI][lL][lL][iI][sS][eE][cC][oO][nN][dD]		{ begin_token(yytext);   return DAY_MILLISECOND; }
[dD][aA][yY]_[sS][eE][cC][oO][nN][dD]     				{ begin_token(yytext);   return DAY_SECOND; }
[dD][aA][yY]_[mM][iI][nN][uU][tT][eE]     				{ begin_token(yytext);   return DAY_MINUTE; }
[dD][aA][yY]_[hH][oO][uU][rR]	       					{ begin_token(yytext);   return DAY_HOUR; }
[dD][eE][aA][lL][lL][oO][cC][aA][tT][eE]				{ begin_token(yytext);   return DEALLOCATE; }
[dD][eE][cC]								{ begin_token(yytext);   return NUMERIC; }
[dD][eE][cC][iI][mM][aA][lL]						{ begin_token(yytext);   return NUMERIC; }
[dD][eE][cC][lL][aA][rR][eE]						{ begin_token(yytext);   return DECLARE; }
[dD][eE][cC][rR][eE][mM][eE][nN][tT]					{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return DECREMENT; }
[dD][eE][fF][aA][uU][lL][tT]						{ begin_token(yytext);   return DEFAULT; }
[dD][eE][fF][eE][rR][rR][aA][bB][lL][eE]				{ begin_token(yytext);   return DEFERRABLE; }
[dD][eE][fF][eE][rR][rR][eE][dD]					{ begin_token(yytext);   return DEFERRED; }
[dD][eE][lL][eE][tT][eE]						{ begin_token(yytext);   return DELETE_; }
[dD][eE][nN][sS][eE]_[rR][aA][nN][kK]					{ begin_token(yytext);
									  csql_yylval.cptr = pt_makename(yytext);
									  return DENSE_RANK; }
[dD][eE][pP][tT][hH]							{ begin_token(yytext);   return DEPTH; }
[dD][eE][sS][cC]							{ begin_token(yytext);   return DESC; }
[dD][eE][sS][cC][rR][iI][bB][eE]					{ begin_token(yytext);   return DESCRIBE; }
[dD][eE][sS][cC][rR][iI][pP][tT][oO][rR]				{ begin_token(yytext);   return DESCRIPTOR; }
[dD][iI][aA][gG][nN][oO][sS][tT][iI][cC][sS]				{ begin_token(yytext);   return DIAGNOSTICS; }
[dD][iI][cC][tT][iI][oO][nN][aA][rR][yY]				{ begin_token(yytext);   return DICTIONARY; }
[dD][iI][fF][fF][eE][rR][eE][nN][cC][eE]				{ begin_token(yytext);   return DIFFERENCE_; }
[dD][iI][sS][cC][oO][nN][nN][eE][cC][tT]				{ begin_token(yytext);   return DISCONNECT; }
[dD][iI][sS][tT][iI][nN][cC][tT]					{ begin_token(yytext);   return DISTINCT; }
[dD][iI][sS][tT][iI][nN][cC][tT][rR][oO][wW]				{ begin_token(yytext);   return DISTINCTROW; }
[dD][iI][vV]								{ begin_token(yytext);	 return DIV; }
[dD][oO]								{ begin_token(yytext);	 return DO; }
[dD][oO][mM][aA][iI][nN]						{ begin_token(yytext);   return Domain; }
[dD][rR][oO][pP]							{ begin_token(yytext);   return DROP; }
[dD][uU][pP][lL][iI][cC][aA][tT][eE]					{ begin_token(yytext);   return DUPLICATE_; }
[dD][aA][tT][eE]							{ begin_token(yytext);   return Date; }
[dD][oO][uU][bB][lL][eE]						{ begin_token(yytext);   return Double; }
[eE][aA][cC][hH]							{ begin_token(yytext);   return EACH; }
[eE][lL][sS][eE]							{ begin_token(yytext);   return ELSE; }
[eE][lL][sS][eE][iI][fF]						{ begin_token(yytext);   return ELSEIF; }
[eE][lL][tT]								{ 
												begin_token(yytext);
												csql_yylval.cptr = pt_makename(yytext); 
												return ELT; 
											}
[eE][nN][dD]								{ begin_token(yytext);   return END; }
[eE][qQ][uU][aA][lL][sS]						{ begin_token(yytext);   return EQUALS; }
[eE][sS][cC][aA][pP][eE]						{ begin_token(yytext);   return ESCAPE; }
[eE][vV][aA][lL][uU][aA][tT][eE]					{ begin_token(yytext);   return EVALUATE; }
[eE][xX][cC][eE][pP][tT]						{ begin_token(yytext);   return EXCEPT; }
[eE][xX][cC][eE][pP][tT][iI][oO][nN]					{ begin_token(yytext);   return EXCEPTION; }
[eE][xX][cC][lL][uU][dD][eE]						{ begin_token(yytext);   return EXCLUDE; }
[eE][xX][eE][cC]							{ begin_token(yytext);   return EXEC; }
[eE][xX][eE][cC][uU][tT][eE]						{ begin_token(yytext);   return EXECUTE; }
[eE][xX][iI][sS][tT][sS]						{ begin_token(yytext);   return EXISTS; }
[eE][xX][pP][lL][aA][iI][nN]						{ 
												begin_token(yytext);
												csql_yylval.cptr = pt_makename(yytext); 
												return EXPLAIN; 
											}
[eE][xX][tT][eE][rR][nN][aA][lL]					{ begin_token(yytext);   return EXTERNAL; }
[eE][xX][tT][rR][aA][cC][tT]						{ begin_token(yytext);   return EXTRACT; }
[fF][eE][tT][cC][hH]							{ begin_token(yytext);   return FETCH; }
[fF][iI][rR][sS][tT]							{ begin_token(yytext);   return FIRST; }
[fF][oO][rR][cC][eE]							{ begin_token(yytext);   return FORCE; }
[fF][oO][rR][eE][iI][gG][nN]						{ begin_token(yytext);   return FOREIGN; }
[fF][oO][uU][nN][dD]							{ begin_token(yytext);   return FOUND; }
[fF][rR][oO][mM]							{ begin_token(yytext);   return FROM; }
[fF][uU][lL][lL]							{ begin_token(yytext);   return FULL; }
[fF][uU][nN][cC][tT][iI][oO][nN]					{ begin_token(yytext);   return FUNCTION; }
[fF][aA][lL][sS][eE]							{ begin_token(yytext);   return False; }
[fF][iI][lL][eE]							{ begin_token(yytext);   return File; }
[fF][lL][oO][aA][tT]							{ begin_token(yytext);   return FLOAT_; }
[fF][oO][rR]								{ begin_token(yytext);   return For; }
[gG][eE][nN][eE][rR][aA][lL]						{ begin_token(yytext);   return GENERAL; }
[gG][eE][tT]								{ begin_token(yytext);   return GET; }
[gG][eE]_[iI][nN][fF]							{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return GE_INF_; }
[gG][eE]_[lL][eE]							{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return GE_LE_; }
[gG][eE]_[lL][tT]							{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return GE_LT_; }
[gG][lL][oO][bB][aA][lL]						{ begin_token(yytext);   return GLOBAL; }
[gG][oO]								{ begin_token(yytext);   return GO; }
[gG][oO][tT][oO]							{ begin_token(yytext);   return GOTO; }
[gG][rR][aA][nN][tT]							{ begin_token(yytext);   return GRANT; }
[gG][rR][aA][nN][tT][sS]						{ begin_token(yytext);
										csql_yylval.cptr = pt_makename(yytext);
										return GRANTS; }
[gG][rR][oO][uU][pP]							{ begin_token(yytext);   return GROUP_; }
[gG][rR][oO][uU][pP][_][cC][oO][nN][cC][aA][tT]				{ begin_token(yytext);
										csql_yylval.cptr = pt_makename(yytext);
										return GROUP_CONCAT; }
[gG][rR][oO][uU][pP][sS]						{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return GROUPS; }
[gG][tT]_[iI][nN][fF]							{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return GT_INF_; }
[gG][tT]_[lL][eE]							{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return GT_LE_; }
[gG][tT]_[lL][tT]							{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return GT_LT_; }
[hH][aA][sS][hH]							{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return HASH; }
[hH][aA][vV][iI][nN][gG]						{ begin_token(yytext);   return HAVING; }
[hH][oO][uU][rR]							{ begin_token(yytext);   return HOUR_; }
[hH][oO][uU][rR]_[mM][iI][lL][lL][iI][sS][eE][cC][oO][nN][dD]		{ begin_token(yytext);   return HOUR_MILLISECOND; }
[hH][oO][uU][rR]_[sS][eE][cC][oO][nN][dD]				{ begin_token(yytext);   return HOUR_SECOND; }
[hH][oO][uU][rR]_[mM][iI][nN][uU][tT][eE]				{ begin_token(yytext);   return HOUR_MINUTE; }
[iI][dD][eE][nN][tT][iI][tT][yY]					{ begin_token(yytext);   return IDENTITY; }
[iI][fF]								{ begin_token(yytext);   return IF; }
[iI][fF][nN][uU][lL][lL]						{ begin_token(yytext);	 
										csql_yylval.cptr = pt_makename(yytext);
										return IFNULL; }
[iI][sS][nN][uU][lL][lL]						{ begin_token(yytext);	 
										csql_yylval.cptr = pt_makename(yytext);
										return ISNULL; }
[iI][gG][nN][oO][rR][eE]						{ begin_token(yytext);   return IGNORE_; }
[iI][mM][mM][eE][dD][iI][aA][tT][eE]					{ begin_token(yytext);   return IMMEDIATE; }
[iI][nN]								{ begin_token(yytext);   return IN_; }
[iI][nN][aA][cC][tT][iI][vV][eE]					{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return INACTIVE; }
[iI][nN][cC][rR][eE][mM][eE][nN][tT]					{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return INCREMENT; }
[iI][nN][dD][eE][xX]							{ begin_token(yytext);   return INDEX; }
[iI][nN][dD][eE][xX][eE][sS]						{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return INDEXES; }
[iI][nN][dD][iI][cC][aA][tT][oO][rR]					{ begin_token(yytext);   return INDICATOR; }
[iI][nN][fF][iI][nN][iI][tT][eE]					{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return INFINITE_; }
[iI][nN][fF]_[lL][eE]							{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return INF_LE_; }
[iI][nN][fF]_[lL][tT]							{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return INF_LT_; }
[iI][nN][hH][eE][rR][iI][tT]						{ begin_token(yytext);   return INHERIT; }
[iI][nN][iI][tT][iI][aA][lL][lL][yY]					{ begin_token(yytext);   return INITIALLY; }
[iI][nN][nN][eE][rR]							{ begin_token(yytext);   return INNER; }
[iI][nN][oO][uU][tT]							{ begin_token(yytext);   return INOUT; }
[iI][nN][pP][uU][tT]							{ begin_token(yytext);   return INPUT_; }
[iI][nN][sS][eE][rR][tT]						{ begin_token(yytext);   return INSERT; }
[iI][nN][sS][tT][aA][nN][cC][eE][sS]					{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return INSTANCES; }
[iI][nN][tT][eE][rR][sS][eE][cC][tT]					{ begin_token(yytext);   return INTERSECT; }
[iI][nN][tT][eE][rR][sS][eE][cC][tT][iI][oO][nN]			{ begin_token(yytext);   return INTERSECTION; }
[iI][nN][tT][eE][rR][vV][aA][lL]					{ begin_token(yytext);   return INTERVAL; }
[iI][nN][tT][oO]							{ begin_token(yytext);   return INTO; }
[iI][nN][vV][aA][lL][iI][dD][aA][tT][eE]				{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return INVALIDATE; }
[iI][sS]								{ begin_token(yytext);   return IS; }
[iI][sS][oO][lL][aA][tT][iI][oO][nN]					{ begin_token(yytext);   return ISOLATION; }
[iI][nN][tT]								{ begin_token(yytext);   return INTEGER; }
[iI][nN][tT][eE][gG][eE][rR]						{ begin_token(yytext);   return INTEGER; }
[jJ][aA][vV][aA]							{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return JAVA; }
[jJ][oO][iI][nN]							{ begin_token(yytext);   return JOIN; }
[kK][eE][yY]								{ begin_token(yytext);   return KEY; }
[kK][eE][yY][sS]							{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return KEYS; }
[lL][aA][nN][gG][uU][aA][gG][eE]					{ begin_token(yytext);   return LANGUAGE; }
[kK][eE][yY][lL][iI][mM][iI][tT]					{ begin_token(yytext);	 return KEYLIMIT; }
[lL][aA][sS][tT]							{ begin_token(yytext);   return LAST; }
[lL][cC][aA][sS][eE]							{ begin_token(yytext);	 
										csql_yylval.cptr = pt_makename(yytext);
										return LCASE; }
[lL][dD][bB]								{ begin_token(yytext);   return LDB; }
[lL][eE][aA][dD][iI][nN][gG]						{ begin_token(yytext);   return LEADING_; }
[lL][eE][aA][vV][eE]							{ begin_token(yytext);   return LEAVE; }
[lL][eE][fF][tT]							{ begin_token(yytext);   return LEFT; }
[lL][eE][sS][sS]							{ begin_token(yytext);   return LESS; }
[lL][eE][vV][eE][lL]							{ begin_token(yytext);   return LEVEL; }
[lL][iI][kK][eE]							{ begin_token(yytext);   return LIKE; }
[lL][iI][mM][iI][tT]							{ begin_token(yytext);   return LIMIT; }
[lL][iI][sS][tT]							{ begin_token(yytext);   return LIST; }
[lL][oO][cC][aA][lL]							{ begin_token(yytext);   return LOCAL; }
[lL][oO][cC][aA][lL]_[tT][rR][aA][nN][sS][aA][cC][tT][iI][oO][nN]_[iI][dD] { begin_token(yytext);   return LOCAL_TRANSACTION_ID; }
[lL][oO][cC][aA][lL][tT][iI][mM][eE]					{ begin_token(yytext);   return LOCALTIME; }
[lL][oO][cC][aA][lL][tT][iI][mM][eE][sS][tT][aA][mM][pP]		{ begin_token(yytext);   return LOCALTIMESTAMP; }
[lL][oO][cC][kK]							{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return LOCK_; }
[lL][oO][oO][pP]							{ begin_token(yytext);   return LOOP; }
[lL][oO][wW][eE][rR]							{ begin_token(yytext);   return LOWER; }
[mM][aA][tT][cC][hH]							{ begin_token(yytext);   return MATCH; }
[mM][aA][xX][iI][mM][uU][mM]						{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return MAXIMUM; }
[mM][aA][xX][vV][aA][lL][uU][eE]					{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return MAXVALUE; }
[mM][eE][dD][iI][uU][mM][iI][nN][tT]				{ begin_token(yytext);   return INTEGER; }
[mM][eE][mM][bB][eE][rR][sS]						{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return MEMBERS; }
[mM][eE][tT][hH][oO][dD]						{ begin_token(yytext);   return METHOD; }
[mM][iI][lL][lL][iI][sS][eE][cC][oO][nN][dD]				{ begin_token(yytext);   return MILLISECOND_; }
[mM][iI][nN][uU][tT][eE]						{ begin_token(yytext);   return MINUTE_; }
[mM][iI][nN][uU][tT][eE]_[mM][iI][lL][lL][iI][sS][eE][cC][oO][nN][dD]	{ begin_token(yytext);   return MINUTE_MILLISECOND; }
[mM][iI][nN][uU][tT][eE]_[sS][eE][cC][oO][nN][dD]     			{ begin_token(yytext);   return MINUTE_SECOND; }
[mM][iI][nN][vV][aA][lL][uU][eE]					{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return MINVALUE; }
[mM][oO][dD]								{ begin_token(yytext);	 return MOD; }
[mM][oO][dD][iI][fF][yY]						{ begin_token(yytext);   return MODIFY; }
[mM][oO][dD][uU][lL][eE]						{ begin_token(yytext);   return MODULE; }
[mM][oO][nN][tT][hH]							{ begin_token(yytext);   return MONTH_; }
[mM][uU][lL][tT][iI][sS][eE][tT]					{ begin_token(yytext);   return MULTISET; }
[mM][uU][lL][tT][iI][sS][eE][tT]_[oO][fF]				{ begin_token(yytext);   return MULTISET_OF; }
[mM][aA][xX]								{ begin_token(yytext);   return Max; }
[mM][iI][nN]								{ begin_token(yytext);   return Min; }
[mM][oO][nN][eE][tT][aA][rR][yY]					{ begin_token(yytext);   return Monetary; }
[nN][aA]								{ begin_token(yytext);   return NA; }
[nN][aA][mM][eE]							{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return NAME; }
[nN][aA][mM][eE][sS]							{ begin_token(yytext);   return NAMES; }
[nN][aA][tT][iI][oO][nN][aA][lL]					{ begin_token(yytext);   return NATIONAL; }
[nN][aA][tT][uU][rR][aA][lL]						{ begin_token(yytext);   return NATURAL; }
[nN][cC][hH][aA][rR]							{ begin_token(yytext);   return NCHAR; }
[nN][eE][xX][tT]							{ begin_token(yytext);   return NEXT; }
[nN][oO]								{ begin_token(yytext);   return NO; }
[nN][oO][cC][aA][cC][hH][eE]						{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return NOCACHE; }
[nN][oO][cC][yY][cC][lL][eE]						{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return NOCYCLE; }
[nN][oO][mM][aA][xX][vV][aA][lL][uU][eE]				{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return NOMAXVALUE; }
[nN][oO][mM][iI][nN][vV][aA][lL][uU][eE]				{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return NOMINVALUE; }
[nN][oO][nN][eE]							{ begin_token(yytext);   return NONE; }
[nN][oO][tT]								{ begin_token(yytext);   return NOT; }
[nN][uU][lL][lL][iI][fF]						{ begin_token(yytext);   return NULLIF; }
[nN][uU][mM][eE][rR][iI][cC]						{ begin_token(yytext);   return NUMERIC; }
[nN][uU][lL][lL]							{ begin_token(yytext);   return Null; }
[oO][bB][jJ][eE][cC][tT]						{ begin_token(yytext);   return OBJECT; }
[oO][cC][tT][eE][tT]_[lL][eE][nN][gG][tT][hH]				{ begin_token(yytext);   return OCTET_LENGTH; }
[oO][fF]								{ begin_token(yytext);   return OF; }
[oO][fF][fF]								{ begin_token(yytext);   return OFF_; }
[oO][iI][dD]								{ begin_token(yytext);   return OID_; }
[oO][nN]								{ begin_token(yytext);   return ON_; }
[oO][nN][lL][yY]							{ begin_token(yytext);   return ONLY; }
[oO][pP][eE][nN]							{ begin_token(yytext);   return OPEN; }
[oO][pP][eE][rR][aA][tT][iI][oO][nN]					{ begin_token(yytext);   return OPERATION; }
[oO][pP][eE][rR][aA][tT][oO][rR][sS]					{ begin_token(yytext);   return OPERATORS; }
[oO][pP][tT][iI][mM][iI][zZ][aA][tT][iI][oO][nN]			{ begin_token(yytext);   return OPTIMIZATION; }
[oO][pP][tT][iI][oO][nN]						{ begin_token(yytext);   return OPTION; }
[oO][rR]								{ begin_token(yytext);   return OR; }
[oO][rR][dD][eE][rR]							{ begin_token(yytext);   return ORDER; }
[oO][tT][hH][eE][rR][sS]						{ begin_token(yytext);   return OTHERS; }
[oO][uU][tT]								{ begin_token(yytext);   return OUT_; }
[oO][uU][tT][eE][rR]							{ begin_token(yytext);   return OUTER; }
[oO][uU][tT][pP][uU][tT]						{ begin_token(yytext);   return OUTPUT; }
[oO][vV][eE][rR]							{ begin_token(yytext);	 return OVER; }
[oO][vV][eE][rR][lL][aA][pP][sS]					{ begin_token(yytext);   return OVERLAPS; }
[pP][aA][rR][aA][mM][eE][tT][eE][rR][sS]				{ begin_token(yytext);   return PARAMETERS; }
[pP][aA][rR][tT][iI][aA][lL]						{ begin_token(yytext);   return PARTIAL; }
[pP][aA][rR][tT][iI][tT][iI][oO][nN]					{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return PARTITION; }
[pP][aA][rR][tT][iI][tT][iI][oO][nN][iI][nN][gG]			{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return PARTITIONING; }
[pP][aA][rR][tT][iI][tT][iI][oO][nN][sS]				{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return PARTITIONS; }
[pP][aA][sS][sS][wW][oO][rR][dD]					{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return PASSWORD; }
[pP][eE][nN][dD][aA][nN][tT]						{ begin_token(yytext);   return PENDANT; }
[pP][rR][oO][xX][yY]							{ begin_token(yytext);   return PROXY; }
[pP][oO][sS][iI][tT][iI][oO][nN]					{ begin_token(yytext);   return POSITION; }
[pP][rR][eE][cC][iI][sS][iI][oO][nN]					{ begin_token(yytext);   return PRECISION; }
[pP][rR][eE][oO][rR][dD][eE][rR]					{ begin_token(yytext);   return PREORDER; }
[pP][rR][eE][pP][aA][rR][eE]						{ begin_token(yytext);   return PREPARE; }
[pP][rR][eE][sS][eE][rR][vV][eE]					{ begin_token(yytext);   return PRESERVE; }
[pP][rR][iI][mM][aA][rR][yY]						{ begin_token(yytext);   return PRIMARY; }
[pP][rR][iI][nN][tT]							{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return PRINT; }
[pP][rR][iI][oO][rR]							{ begin_token(yytext);   return PRIOR; }
[pP][rR][iI][oO][rR][iI][tT][yY]					{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return PRIORITY; }
[pP][rR][iI][vV][iI][lL][eE][gG][eE][sS]				{ begin_token(yytext);   return PRIVILEGES; }
[pP][rR][oO][cC][eE][dD][uU][rR][eE]					{ begin_token(yytext);   return PROCEDURE; }
[pP][rR][oO][tT][eE][cC][tT][eE][dD]					{ begin_token(yytext);   return PROTECTED; }
[pP][rR][iI][vV][aA][tT][eE]						{ begin_token(yytext);   return Private; }
[qQ][uU][eE][rR][yY]							{ begin_token(yytext);   return QUERY; }
[qQ][uU][aA][rR][tT][eE][rR]						{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return QUARTER; }
[rR][aA][nN][kK]							{ begin_token(yytext);
									  csql_yylval.cptr = pt_makename(yytext);
									  return RANK; }
[rR][aA][nN][gG][eE]							{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return RANGE_; }
[rR][eE][aA][dD]							{ begin_token(yytext);   return READ; }
[rR][eE][aA][lL]							{ begin_token(yytext);   return FLOAT_; }
[rR][eE][bB][uU][iI][lL][dD] 						{ begin_token(yytext);   return REBUILD; }
[rR][eE][cC][uU][rR][sS][iI][vV][eE]					{ begin_token(yytext);   return RECURSIVE; }
[rR][eE][fF]								{ begin_token(yytext);   return REF; }
[rR][eE][fF][eE][rR][eE][nN][cC][eE][sS]				{ begin_token(yytext);   return REFERENCES; }
[rR][eE][fF][eE][rR][eE][nN][cC][iI][nN][gG]				{ begin_token(yytext);   return REFERENCING; }
[rR][eE][gG][eE][xX][pP]						{ begin_token(yytext);   return REGEXP; }
[rR][eE][gG][iI][sS][tT][eE][rR]					{ begin_token(yytext);   return REGISTER; }
[rR][eE][jJ][eE][cC][tT]						{ begin_token(yytext);   return REJECT_; }
[rR][eE][lL][aA][tT][iI][vV][eE]					{ begin_token(yytext);   return RELATIVE_; }
[rR][eE][mM][oO][vV][eE]						{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return REMOVE; }
[rR][eE][nN][aA][mM][eE]						{ begin_token(yytext);   return RENAME; }
[rR][eE][oO][rR][gG][aA][nN][iI][zZ][eE]				{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return REORGANIZE; }
[rR][eE][pP][eE][aA][tT][aA][bB][lL][eE]				{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return REPEATABLE; }
[rR][eE][pP][lL][aA][cC][eE]						{ begin_token(yytext);   return REPLACE; }
[rR][eE][sS][iI][gG][nN][aA][lL]					{ begin_token(yytext);   return RESIGNAL; }
[rR][eE][sS][tT][rR][iI][cC][tT]					{ begin_token(yytext);   return RESTRICT; }
[rR][eE][tT][aA][iI][nN]						{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return RETAIN; }
[rR][eE][tT][uU][rR][nN]						{ begin_token(yytext);   return RETURN; }
[rR][eE][tT][uU][rR][nN][sS]						{ begin_token(yytext);   return RETURNS; }
[rR][eE][uU][sS][eE]_[oO][iI][dD]					{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return REUSE_OID; }
[rR][eE][vV][eE][rR][sS][eE]						{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return REVERSE; }
[rR][eE][vV][oO][kK][eE]						{ begin_token(yytext);   return REVOKE; }
[rR][iI][gG][hH][tT]							{ begin_token(yytext);   return RIGHT; }
[rR][lL][iI][kK][eE]							{ begin_token(yytext);   return RLIKE; }
[rR][oO][lL][eE]							{ begin_token(yytext);   return ROLE; }
[rR][oO][lL][lL][bB][aA][cC][kK]					{ begin_token(yytext);   return ROLLBACK; }
[rR][oO][lL][lL][uU][pP]						{ begin_token(yytext);	 return ROLLUP; }
[rR][oO][uU][tT][iI][nN][eE]						{ begin_token(yytext);   return ROUTINE; }
[rR][oO][wW]								{ begin_token(yytext);   return ROW; }
[rR][oO][wW]_[nN][uU][mM][bB][eE][rR]					{ begin_token(yytext);
									  csql_yylval.cptr = pt_makename(yytext);
									  return ROW_NUMBER; }
[rR][oO][wW][nN][uU][mM]						{ begin_token(yytext);   return ROWNUM; }
[rR][oO][wW][sS]							{ begin_token(yytext);   return ROWS; }
[sS][aA][vV][eE][pP][oO][iI][nN][tT]					{ begin_token(yytext);   return SAVEPOINT; }
[sS][cC][hH][eE][mM][aA]						{ begin_token(yytext);   return SCHEMA; }
[sS][cC][oO][pP][eE]___						 	{ begin_token(yytext);   return SCOPE; }
[sS][cC][rR][oO][lL][lL]						{ begin_token(yytext);   return SCROLL; }
[sS][eE][aA][rR][cC][hH]						{ begin_token(yytext);   return SEARCH; }
[sS][eE][cC][oO][nN][dD]						{ begin_token(yytext);   return SECOND_; }
[sS][eE][cC][oO][nN][dD]_[mM][iI][lL][lL][iI][sS][eE][cC][oO][nN][dD]	{ begin_token(yytext);   return SECOND_MILLISECOND; }
[sS][eE][cC][tT][iI][oO][nN]						{ begin_token(yytext);   return SECTION; }
[sS][eE][lL][eE][cC][tT]						{ begin_token(yytext);   return SELECT; }
[sS][eE][nN][sS][iI][tT][iI][vV][eE]					{ begin_token(yytext);   return SENSITIVE; }
[sS][eE][pP][aA][rR][aA][tT][oO][rR]					{ begin_token(yytext);
										csql_yylval.cptr = pt_makename(yytext);
										return SEPARATOR; }
[sS][eE][qQ][uU][eE][nN][cC][eE]					{ begin_token(yytext);   return SEQUENCE; }
[sS][eE][qQ][uU][eE][nN][cC][eE]_[oO][fF]			 	{ begin_token(yytext);   return SEQUENCE_OF; }
[sS][eE][rR][iI][aA][lL]						{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return SERIAL; }
[sS][eE][rR][iI][aA][lL][iI][zZ][aA][bB][lL][eE]			{ begin_token(yytext);   return SERIALIZABLE; }
[sS][eE][sS][sS][iI][oO][nN]						{ begin_token(yytext);   return SESSION; }
[sS][eE][sS][sS][iI][oO][nN]_[uU][sS][eE][rR]				{ begin_token(yytext);   return SESSION_USER; }
[sS][eE][tT]								{ begin_token(yytext);   return SET; }
[sS][eE][tT][eE][qQ]							{ begin_token(yytext);   return SETEQ; }
[sS][eE][tT][nN][eE][qQ]						{ begin_token(yytext);   return SETNEQ; }
[sS][eE][tT]_[oO][fF]							{ begin_token(yytext);   return SET_OF; }
[sS][hH][aA][rR][eE][dD]						{ begin_token(yytext);   return SHARED; }
[sS][hH][oO][wW]							{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return SHOW; }
[sS][iI][bB][lL][iI][nN][gG][sS]					{ begin_token(yytext);   return SIBLINGS; }
[sS][hH][oO][rR][tT]							{ begin_token(yytext);   return SmallInt; }
[sS][iI][mM][iI][lL][aA][rR]						{ begin_token(yytext);   return SIMILAR; }
[sS][iI][zZ][eE]							{ begin_token(yytext);   return SIZE_; }
[sS][oO][mM][eE]							{ begin_token(yytext);   return SOME; }
[sS][qQ][lL]								{ begin_token(yytext);   return SQL; }
[sS][qQ][lL][cC][oO][dD][eE]						{ begin_token(yytext);   return SQLCODE; }
[sS][qQ][lL][eE][rR][rR][oO][rR]					{ begin_token(yytext);   return SQLERROR; }
[sS][qQ][lL][eE][xX][cC][eE][pP][tT][iI][oO][nN]			{ begin_token(yytext);   return SQLEXCEPTION; }
[sS][qQ][lL][sS][tT][aA][tT][eE]					{ begin_token(yytext);   return SQLSTATE; }
[sS][qQ][lL][wW][aA][rR][nN][iI][nN][gG]				{ begin_token(yytext);   return SQLWARNING; }
[sS][tT][aA][bB][iI][lL][iI][tT][yY]					{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return STABILITY; }
[sS][tT][aA][rR][tT]							{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return START_; }
[sS][tT][aA][tT][eE][mM][eE][nN][tT]					{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return STATEMENT; }
[sS][tT][aA][tT][iI][sS][tT][iI][cC][sS]				{ begin_token(yytext);   return STATISTICS; }
[sS][tT][aA][tT][uU][sS]						{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return STATUS; }
[sS][tT][dD][dD][eE][vV]						{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return STDDEV; }
[sS][tT][dD][dD][eE][vV][_][pP][oO][pP]					{ begin_token(yytext);
										csql_yylval.cptr = pt_makename(yytext);
										return STDDEV_POP; }
[sS][tT][dD][dD][eE][vV][_][sS][aA][mM][pP]				{ begin_token(yytext);
										csql_yylval.cptr = pt_makename(yytext);
										return STDDEV_SAMP; }
[sS][tT][rR][uU][cC][tT][uU][rR][eE]					{ begin_token(yytext);   return STRUCTURE; }
[sS][tT][rR][_][tT][oO][_][dD][aA][tT][eE]				{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return STR_TO_DATE; }
[sS][uU][bB][cC][lL][aA][sS][sS]					{ begin_token(yytext);   return SUBCLASS; }
[sS][uU][bB][dD][aA][tT][eE]						{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return SUBDATE; }
[sS][uU][bB][sS][eE][tT]						{ begin_token(yytext);   return SUBSET; }
[sS][uU][bB][sS][eE][tT][eE][qQ]					{ begin_token(yytext);   return SUBSETEQ; }
[sS][uU][bB][sS][tT][rR][iI][nN][gG]					{ begin_token(yytext);   return SUBSTRING_; }
[sS][uU][mM]								{ begin_token(yytext);   return SUM; }
[sS][uU][pP][eE][rR][cC][lL][aA][sS][sS]				{ begin_token(yytext);   return SUPERCLASS; }
[sS][uU][pP][eE][rR][sS][eE][tT]					{ begin_token(yytext);   return SUPERSET; }
[sS][uU][pP][eE][rR][sS][eE][tT][eE][qQ]				{ begin_token(yytext);   return SUPERSETEQ; }
[sS][yY][sS][_][cC][oO][nN][nN][eE][cC][tT][_][bB][yY][_][pP][aA][tT][hH]	{ begin_token(yytext);   return SYS_CONNECT_BY_PATH; }
[sS][yY][sS]_[dD][aA][tT][eE]						{ begin_token(yytext);   return SYS_DATE; }
[sS][yY][sS][dD][aA][tT][eE]						{ begin_token(yytext);   return SYS_DATE; }
[sS][yY][sS]_[tT][iI][mM][eE]						{ begin_token(yytext);   return SYS_TIME_; }
[sS][yY][sS][tT][iI][mM][eE]						{ begin_token(yytext);   return SYS_TIME_; }
[sS][yY][sS]_[tT][iI][mM][eE][sS][tT][aA][mM][pP]			{ begin_token(yytext);   return SYS_TIMESTAMP; }
[sS][yY][sS][tT][iI][mM][eE][sS][tT][aA][mM][pP]			{ begin_token(yytext);   return SYS_TIMESTAMP; }
[sS][yY][sS]_[dD][aA][tT][eE][tT][iI][mM][eE] 				{ begin_token(yytext);   return SYS_DATETIME; }
[sS][yY][sS][dD][aA][tT][eE][tT][iI][mM][eE] 				{ begin_token(yytext);   return SYS_DATETIME; }
[sS][yY][sS][tT][eE][mM]						{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return SYSTEM; }
[sS][yY][sS][tT][eE][mM]_[uU][sS][eE][rR]				{ begin_token(yytext);   return SYSTEM_USER; }
[sS][yY][sS]_[uU][sS][eE][rR]						{ begin_token(yytext);   return SYS_USER; }
[sS][mM][aA][lL][lL][iI][nN][tT]					{ begin_token(yytext);   return SmallInt; }
[sS][tT][rR][iI][nN][gG]						{ begin_token(yytext);   return String; }
[tT][aA][bB][lL][eE]							{ begin_token(yytext);   return TABLE; }
[tT][aA][bB][lL][eE][sS]						{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return TABLES; }
[tT][eE][mM][pP][oO][rR][aA][rR][yY]					{ begin_token(yytext);   return TEMPORARY; }
[tT][eE][sS][tT]							{ begin_token(yytext);   return TEST; }
[tT][hH][eE][nN]							{ begin_token(yytext);   return THEN; }
[tT][hH][eE][rR][eE]							{ begin_token(yytext);   return THERE; }
[tT][iI][mM][eE][oO][uU][tT]						{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return TIMEOUT; }
[tT][iI][mM][eE][sS][tT][aA][mM][pP]					{ begin_token(yytext);   return TIMESTAMP; }
[tT][iI][mM][eE][zZ][oO][nN][eE]_[hH][oO][uU][rR]			{ begin_token(yytext);   return TIMEZONE_HOUR; }
[tT][iI][mM][eE][zZ][oO][nN][eE]_[mM][iI][nN][uU][tT][eE]		{ begin_token(yytext);   return TIMEZONE_MINUTE; }
[tT][oO]								{ begin_token(yytext);   return TO; }
[tT][rR][aA][cC][eE]							{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return TRACE; }
[tT][rR][aA][iI][lL][iI][nN][gG]					{ begin_token(yytext);   return TRAILING_; }
[tT][rR][aA][nN][sS][aA][cC][tT][iI][oO][nN]				{ begin_token(yytext);   return TRANSACTION; }
[tT][rR][aA][nN][sS][lL][aA][tT][eE]					{ begin_token(yytext);   return TRANSLATE; }
[tT][rR][aA][nN][sS][lL][aA][tT][iI][oO][nN]				{ begin_token(yytext);   return TRANSLATION; }
[tT][rR][iI][gG][gG][eE][rR]						{ begin_token(yytext);   return TRIGGER; }
[tT][rR][iI][gG][gG][eE][rR][sS]					{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return TRIGGERS; }
[tT][rR][iI][mM]							{ begin_token(yytext);   return TRIM; }
[tT][yY][pP][eE]							{ begin_token(yytext);   return TYPE; }
[tT][hH][aA][nN]							{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return THAN; }
[tT][iI][mM][eE]							{ begin_token(yytext);   return Time; }
[tT][iI][nN][yY][iI][nN][tT]				{ begin_token(yytext);   return SmallInt; }
[tT][rR][uU][eE]							{ begin_token(yytext);   return True; }
[tT][rR][uU][nN][cC][aA][tT][eE]					{ begin_token(yytext);	 return TRUNCATE; }
[uU][cC][aA][sS][eE]							{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return UCASE; }
[uU][nN][cC][oO][mM][mM][iI][tT][tT][eE][dD]				{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return UNCOMMITTED; }
[uU][nN][dD][eE][rR]							{ begin_token(yytext);   return UNDER; }
[uU][nN][iI][qQ][uU][eE]						{ begin_token(yytext);   return UNIQUE; }
[uU][nN][kK][nN][oO][wW][nN]						{ begin_token(yytext);   return UNKNOWN; }
[uU][pP][dD][aA][tT][eE]						{ begin_token(yytext);   return UPDATE; }
[uU][pP][pP][eE][rR]							{ begin_token(yytext);   return UPPER; }
[uU][sS][aA][gG][eE]							{ begin_token(yytext);   return USAGE; }
[uU][sS][eE]								{ begin_token(yytext);   return USE; }
[uU][sS][eE][rR]							{ begin_token(yytext);   return USER; }
[uU][sS][iI][nN][gG]							{ begin_token(yytext);   return USING; }
[uU][nN][iI][oO][nN]							{ begin_token(yytext);   return Union; }
[uU][tT][iI][mM][eE]							{ begin_token(yytext);   return Utime; }
[vV][aA][lL][uU][eE]							{ begin_token(yytext);   return VALUE; }
[vV][aA][lL][uU][eE][sS]						{ begin_token(yytext);   return VALUES; }
[vV][aA][rR][_][pP][oO][pP]						{ begin_token(yytext);
										csql_yylval.cptr = pt_makename(yytext);
										return VAR_POP; }
[vV][aA][rR][_][sS][aA][mM][pP]						{ begin_token(yytext);
										csql_yylval.cptr = pt_makename(yytext);
										return VAR_SAMP; }
[vV][aA][rR][cC][hH][aA][rR]						{ begin_token(yytext);   return VARCHAR; }
[vV][aA][rR][iI][aA][bB][lL][eE]					{ begin_token(yytext);   return VARIABLE_; }
[vV][aA][rR][iI][aA][nN][cC][eE]					{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return VARIANCE; }
[vV][aA][rR][yY][iI][nN][gG]						{ begin_token(yytext);   return VARYING; }
[vV][cC][lL][aA][sS][sS]						{ begin_token(yytext);   return VCLASS; }
[vV][iI][eE][wW]							{ begin_token(yytext);   return VIEW; }
[vV][iI][rR][tT][uU][aA][lL]						{ begin_token(yytext);   return VIRTUAL; }
[vV][iI][sS][iI][bB][lL][eE]						{ begin_token(yytext);   return VISIBLE; }
[wW][aA][iI][tT]							{ begin_token(yytext);   return WAIT; }
[wW][eE][eE][kK]							{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return WEEK; }
[wW][hH][eE][nN]							{ begin_token(yytext);   return WHEN; }
[wW][hH][eE][nN][eE][vV][eE][rR]					{ begin_token(yytext);   return WHENEVER; }
[wW][hH][eE][rR][eE]							{ begin_token(yytext);   return WHERE; }
[wW][hH][iI][lL][eE]							{ begin_token(yytext);   return WHILE; }
[wW][iI][tT][hH]							{ begin_token(yytext);   return WITH; }
[wW][iI][tT][hH][oO][uU][tT]						{ begin_token(yytext);   return WITHOUT; }
[wW][oO][rR][kK]							{ begin_token(yytext);   return WORK; }
[wW][oO][rR][kK][sS][pP][aA][cC][eE]					{ begin_token(yytext);   
										csql_yylval.cptr = pt_makename(yytext);
										return WORKSPACE; }
[wW][rR][iI][tT][eE]							{ begin_token(yytext);   return WRITE; }
[xX][oO][rR]								{ begin_token(yytext);	 return XOR; }
[yY][eE][aA][rR]							{ begin_token(yytext);   return YEAR_; }
[yY][eE][aA][rR]_[mM][oO][nN][tT][hH]      				{ begin_token(yytext);   return YEAR_MONTH; }
[zZ][oO][nN][eE]							{ begin_token(yytext);   return ZONE; }


\\N									{ begin_token(yytext);   return Null; }

"->"									{ begin_token(yytext);   return RIGHT_ARROW; }
"||"									{ 
									  begin_token(yytext);   
									  if (!PRM_PIPES_AS_CONCAT)
									    return OR;
									  else
									    return STRCAT; 
									}

"<>"									{ begin_token(yytext);   return COMP_NOT_EQ; }
">="									{ begin_token(yytext);   return COMP_GE; }
"<="									{ begin_token(yytext);   return COMP_LE; }
"?:"									{ begin_token(yytext);   return PARAM_HEADER; }
"!="									{ begin_token(yytext);	 return COMP_NOT_EQ; }
"<<"									{ begin_token(yytext);	 return BITSHIFT_LEFT; }
">>"									{ begin_token(yytext);	 return BITSHIFT_RIGHT; }
"%"									{ begin_token(yytext);	 return MOD; }
"<=>"									{ begin_token(yytext);	 return COMP_NULLSAFE_EQ; }
"&&"									{ begin_token(yytext);	 return AND; }
":="									{ begin_token(yytext);	 return VAR_ASSIGN; }


[0-9]+									{ 
										int i = 0;
										int len = 0;

										begin_token(yytext);   
										len = strlen(yytext);
										if (len >= 254) 
											yytext[254] = 0;
		
										/* loop until len - 1, in case of 0000000000000000000000 */
										for (i = 0; i < len - 1 && yytext[i] == '0'; i++)
										;

										csql_yylval.cptr = pt_makename(&yytext[i]);
										return UNSIGNED_INTEGER; 
									}



([0-9]+[Ee][\+\-]?[0-9]+[fF]?)|([0-9]*\.[0-9]+([Ee][\+\-]?[0-9]+)?[fF]?)|([0-9]+\.[0-9]*([Ee][\+\-]?[0-9]+)?[fF]?) 	{
										char buff[256];
										int len;

										begin_token(yytext);   
										len = strlen(yytext);
										if (len >= 254) 
											yytext[254] = 0;

										if (len < 253 && yytext[len-1] == '.')
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

([a-zA-Z_#]|([\xc2-\xfe]))([a-zA-Z_#0-9]|([\x80-\xfe]))* { begin_token(yytext);   
							   if (strlen(yytext) >= 254) 
							     yytext[254] = 0;
							   csql_yylval.cptr = pt_makename(yytext);
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
					if (PRM_ANSI_QUOTES)
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
					BEGIN(INITIAL);
					return CHAR_STRING;
				}

<DOUBLY_QUOTED_CHAR_STRING>["]	{
					BEGIN(INITIAL);
					return CHAR_STRING;
				}

<DELIMITED_ID_NAME>["]		{
					BEGIN(INITIAL);
					if (strlen(csql_yylval.cptr) >= 254)
					  {
					    csql_yylval.cptr[254] = 0;
					  }
					return DelimitedIdName;
				}

<BRACKET_ID_NAME>"]"		{
					BEGIN(INITIAL);
					if (strlen(csql_yylval.cptr) >= 254)
					  {
					    csql_yylval.cptr[254] = 0;
					  }
					return BracketDelimitedIdName;
				}

<BACKTICK_ID_NAME>"`"		{
					BEGIN(INITIAL);
					if (strlen(csql_yylval.cptr) >= 254)
					  {
					    csql_yylval.cptr[254] = 0;
					  }
					return BacktickDelimitedIdName;
				}

<QUOTED_NCHAR_STRING,QUOTED_BIT_STRING,QUOTED_HEX_STRING,QUOTED_CHAR_STRING,DOUBLY_QUOTED_CHAR_STRING,DELIMITED_ID_NAME,BRACKET_ID_NAME,BACKTICK_ID_NAME>\n	{
					this_parser->line = yyline++;
					this_parser->column = yycolumn = yycolumn_end = 0;
					csql_yylval.cptr = pt_append_string(this_parser, csql_yylval.cptr, "\n");
				}

<QUOTED_NCHAR_STRING,QUOTED_BIT_STRING,QUOTED_HEX_STRING,QUOTED_CHAR_STRING,DOUBLY_QUOTED_CHAR_STRING><<EOF>>	{
					BEGIN(INITIAL);
					csql_yyerror("unterminated string");
					return UNTERMINATED_STRING;
				}

<DELIMITED_ID_NAME,BRACKET_ID_NAME,BACKTICK_ID_NAME><<EOF>>	{
					BEGIN(INITIAL);
					csql_yyerror("unterminated identifier");
					return UNTERMINATED_IDENTIFIER;
				}

<QUOTED_NCHAR_STRING,QUOTED_CHAR_STRING>[^'\n\\]+	{
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

<QUOTED_NCHAR_STRING,QUOTED_CHAR_STRING>['][']	{
					csql_yylval.cptr = pt_append_string(this_parser, csql_yylval.cptr, "'");
				}

<DOUBLY_QUOTED_CHAR_STRING,DELIMITED_ID_NAME>["]["]	{
					csql_yylval.cptr = pt_append_string(this_parser, csql_yylval.cptr, "\"");
				}

<BACKTICK_ID_NAME>[`][`]	{
					csql_yylval.cptr = pt_append_string(this_parser, csql_yylval.cptr, "`");
				}

<QUOTED_NCHAR_STRING,QUOTED_CHAR_STRING,DOUBLY_QUOTED_CHAR_STRING>{

"\\n"				{
					csql_yylval.cptr = pt_append_string(this_parser, csql_yylval.cptr,
									    (PRM_NO_BACKSLASH_ESCAPES || this_parser->strings_have_no_escapes) ? "\\n" : "\n");
				}

"\\r"				{
					csql_yylval.cptr = pt_append_string(this_parser, csql_yylval.cptr,
									    (PRM_NO_BACKSLASH_ESCAPES || this_parser->strings_have_no_escapes) ? "\\r" : "\r");
				}

"\\t"				{
					csql_yylval.cptr = pt_append_string(this_parser, csql_yylval.cptr,
									    (PRM_NO_BACKSLASH_ESCAPES || this_parser->strings_have_no_escapes) ? "\\t" : "\t");
				}

"\\%"				{
					csql_yylval.cptr = pt_append_string(this_parser, csql_yylval.cptr, "\\%");
				}

"\\_"				{
					csql_yylval.cptr = pt_append_string(this_parser, csql_yylval.cptr, "\\_");
				}

[\\]['"]			{
					if (PRM_NO_BACKSLASH_ESCAPES || this_parser->strings_have_no_escapes)
					  {
					    csql_yylval.cptr = pt_append_string(this_parser, csql_yylval.cptr, "\\");
					    unput(*(yytext + 1));
					  }
					else
					  {
					    csql_yylval.cptr = pt_append_string(this_parser, csql_yylval.cptr, yytext + 1);
					  }
				}

[\\].				{
					csql_yylval.cptr = pt_append_string(this_parser, csql_yylval.cptr,
							   (PRM_NO_BACKSLASH_ESCAPES || this_parser->strings_have_no_escapes) ? yytext : yytext + 1);
				}

[\\]\n				{
					this_parser->line = yyline++;
					this_parser->column = yycolumn = yycolumn_end = 0;

					if (PRM_NO_BACKSLASH_ESCAPES || this_parser->strings_have_no_escapes)
					  {
					    csql_yylval.cptr = pt_append_string(this_parser, csql_yylval.cptr, "\\\n");
					  }
					else
					  {
					    csql_yylval.cptr = pt_append_string(this_parser, csql_yylval.cptr, "\n");
					  }
				}

}

"//"				{ 
					char* cp = parser_line_hint(); 
					if (cp) 
					  {
					    csql_yylval.cptr = cp;
					    return CPP_STYLE_HINT;
					  }
				}


"/*"				{ 
					char* cp = parser_c_hint(); 
					if (cp) 
					  {
					    csql_yylval.cptr = cp;
					    return C_STYLE_HINT;
					  }
				}



"--"				{ 
					char* cp = parser_line_hint(); 
					if (cp) 
					  {
					    csql_yylval.cptr = cp;
					    return SQL_STYLE_HINT;
					  }
				}

\xa1\xef			{ begin_token(yytext);   return YEN_SIGN; }
\$				{ begin_token(yytext);   return DOLLAR_SIGN; }
\\				{ begin_token(yytext);   return WON_SIGN; }
\\[T][L]			{ begin_token(yytext);   return TURKISH_LIRA_SIGN; }

\\[J][P][Y]			{ begin_token(yytext);   return YEN_SIGN; }
\\[U][S][D]			{ begin_token(yytext);   return DOLLAR_SIGN; }
\\[K][R][W]			{ begin_token(yytext);   return WON_SIGN; }
\\[T][R][Y]			{ begin_token(yytext);   return TURKISH_LIRA_SIGN; }

.				{ begin_token(yytext);   return yytext[0]; }
%%

int parser_yyinput_single_mode = 0;

void
csql_yyerror_explicit (int line, int column)
{
  csql_yyerror (NULL);
  msg_ptr = 0;
}


void
csql_yyerror (const char *s)
{
  YY_FLUSH_BUFFER;
  parser_statement_OK = 0;

  if (msg_ptr)
    {
      PT_NODE *dummy = parser_new_node (this_parser, PT_EXPR);
      PT_ERRORm (this_parser, dummy, MSGCAT_SET_PARSER_SYNTAX,
		 g_msg[msg_ptr - 1]);
      parser_free_tree (this_parser, dummy);
    }
  else
    {
      const char *cmp_str = ", expecting \'{\'";
      const char *cmp_str2 = ", expecting SELECT or \'(\'";
      PT_NODE *dummy;

      if ((strlen (s) > strlen (cmp_str)) &&
	  (strcmp (&s[strlen (s) - strlen (cmp_str)], cmp_str) == 0))
	{
	  *(char *) &s[strlen (s) - strlen (cmp_str)] = 0;
	}
      else if ((strlen (s) > strlen (cmp_str2)) &&
	       (strcmp (&s[strlen (s) - strlen (cmp_str2)], cmp_str2) == 0))
	{
	  *(char *) &s[strlen (s) - strlen (cmp_str2)] = 0;
	}

      dummy = parser_new_node (this_parser, PT_EXPR);
      if (dummy == NULL)
	return;
      PT_ERRORf (this_parser, dummy, "%s", s);
      parser_free_tree (this_parser, dummy);
    }
}

int
yywrap ()
{
  return 1;
}


static int
parser_yyinput_single_line (char *buff, int max_size)
{
  int c, end;
  int i = 0;

  assert (max_size >= 1);
  do
    {
      c = pt_nextchar ();
      if (c == '\'' || c == '\"')	/* string start */
	{
	  buff[i++] = c;
	  if (i >= max_size)
	    {
	      return i;
	    }

	  end = c;
	  do
	    {
	      c = pt_nextchar ();
	      if (c == end)	/* string end */
		{
		  break;
		}

	      buff[i++] = c;
	      if (i >= max_size)
		{
		  return i;
		}

	      if (c == '\\')	/* if escape character */
		{
		  buff[i++] = c;
		  if (i >= max_size)
		    {
		      return i;
		    }

		  c = pt_nextchar ();	/* read one more */
		  buff[i++] = c;
		  if (i >= max_size)
		    {
		      return i;
		    }
		}

	    }
	  while (c != -1);
	}

      if (c == ';')
	{
	  buff[i++] = -1;
	  buff[i] = 0;
	  return i;
	}

      buff[i++] = c;
      if (i >= max_size)
	{
	  return i;
	}

    }
  while (c != -1);

  buff[i] = 0;
  parser_yyinput_single_mode = 0;	/* all done */
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

static int
input_internal ()
{
  int c = input ();
  if (c == '\n')
    {
      this_parser->line = csql_yylloc.last_line = yyline++;
      this_parser->column = csql_yylloc.last_column = yycolumn_end = 1;
    }
  else
    {
      this_parser->column = csql_yylloc.last_column = yycolumn_end++;
    }

  return c;
}

static char *
parser_c_hint ()
{
  int c, c1;
  char buff[CSQL_MAXNAME];
  int i = 0;
  PT_HINT_ENUM hint = PT_HINT_NONE;

  /* column position set */
  yycolumn = yycolumn_end;
  csql_yylloc.first_column = yycolumn;
  csql_yylloc.first_line = yyline;
  csql_yylloc.last_line = yyline;
  csql_yylloc.last_column = yycolumn_end;

  if (i < (CSQL_MAXNAME - 1) && (c = input_internal ()) == '+')
    {				/* hint */
      buff[i++] = '+';
      while ((c = input_internal ()) != '*' && c != 0 && c != -1)
	{
	  if (i >= (CSQL_MAXNAME - 1))
	    {
	      goto consume_comment;
	    }

	  buff[i++] = c;
	}

      if ((c1 = input_internal ()) != '/' && c != 0 && c != -1)
	{
	  unput (c1);
	  goto consume_comment;
	}

      buff[i] = 0;

      pt_check_hint (buff, parser_hint_table, &hint, false);
      if (hint == PT_HINT_NONE)
	{			/* comment */
	  return NULL;
	}

      return pt_makename (buff);
    }
  else
    {
      unput (c);
    }

consume_comment:
  while ((c = input_internal ()) != '*' && c != 0 && c != -1);

  if ((c1 = input_internal ()) != '/' && c != 0 && c != -1)
    {
      unput (c1);
      goto consume_comment;
    }

  return NULL;
}


static char *
parser_line_hint (void)
{
  int c;
  char buff[CSQL_MAXNAME];
  int i = 0;
  PT_HINT_ENUM hint = PT_HINT_NONE;


  /* column position set */
  yycolumn = yycolumn_end;
  csql_yylloc.first_column = yycolumn;
  csql_yylloc.first_line = yyline;
  csql_yylloc.last_line = yyline;
  csql_yylloc.last_column = yycolumn_end;

loop:
  if (i < (CSQL_MAXNAME - 1) && (c = input_internal ()) == '+')
    {				/* hint */
      buff[i++] = '+';
      while ((c = input_internal ()) != '\r' && c != '\n' && c != 0
	     && c != -1)
	{
	  if (i >= (CSQL_MAXNAME - 1))
	    goto loop;
	  buff[i++] = c;
	}
      buff[i] = 0;

      pt_check_hint (buff, parser_hint_table, &hint, false);
      if (hint == PT_HINT_NONE)
	{			/* comment */
	  return NULL;
	}

      return pt_makename (buff);
    }
  else
    {				/* comment */
      unput (c);
      while ((c = input_internal ()) != '\r' && c != '\n' && c != 0
	     && c != -1);

      return NULL;
    }
}


static void
begin_token (char *token)
{
  yycolumn = yycolumn_end;
  yycolumn_end += strlen (token);

  csql_yylloc.first_line = yyline;
  csql_yylloc.first_column = yycolumn;
  csql_yylloc.last_line = yyline;
  csql_yylloc.last_column = yycolumn_end;
}
