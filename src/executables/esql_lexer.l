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
 * esql_lexer.l : esql lexer file
 */

%{

#include "esql_grammar.h"
#include "esql_scanner_support.h"

//#define LEXER_DEBUG


#ifdef LEXER_DEBUG
#define PRINT(a, b) printf(a, b)
#else
#define PRINT(a, b)
#endif


static void parser_c_comment(void);
static void parser_line_comment(void);
static void parser_ignore_comment(void);
static char* parser_quoted_string(char end);
static int esql_yyinput(char* buff, int max_size);

#define JP_MAXNAME	256


#undef YY_INPUT
#define YY_INPUT(buffer, result, max_size) (result = esql_yyinput(buffer, max_size))


%}


%%

[ \t\r]+		{{
			
				if (esql_yy_mode () != BUFFER_mode)
				  ECHO;
				else
				  ECHO_STR (" ", strlen (" "));
				
			}}

\n			{{
			
				int mode = esql_yy_mode ();
				if (mode != BUFFER_mode)
				  ECHO;
				else
				  ECHO_STR (" ", strlen (" "));
				
				esql_yylineno++;
				
				if (mode == ECHO_mode || mode == C_mode)
				  {
				    CHECK_LINENO;
				  }
				
			}}

"//"			{	
				PRINT("tok: %s\n", yytext); 
				ECHO;
				parser_line_comment(); 

			}


"/*"			{	
				PRINT("tok: %s\n", yytext); 
				ECHO;
				parser_c_comment(); 
			}

"#"			{	
				PRINT("tok: %s\n", yytext); 
				ECHO;
				parser_line_comment(); 
			}

"--"			{{
				int mode = esql_yy_mode();
				PRINT ("tok: %s\n", yytext);

				if (mode == C_mode)
				  {
				    ECHO;
				    return GENERIC_TOKEN;
				  }
				else if (mode == ECHO_mode)
				  {
				    ECHO;
				  }
				else if (mode == BUFFER_mode)
				  {
				    parser_ignore_comment ();
				
				  }
				else
				  {
				    ECHO;
				    parser_line_comment ();
				  }
				
			}}

\"			{{
				const char *quote = "\"";
				char *buff = parser_quoted_string ('\"');
				PRINT ("tok: %s\n", yytext);

				ECHO_STR (quote, strlen (quote));
				ECHO_STR (buff, strlen (buff));
				ECHO_STR (quote, strlen (quote));
				
				if (esql_yy_mode () == EXPR_mode)
				  {
				    return GENERIC_TOKEN;
				
				  }
				
			}}


EXEC[ \t]		{	
				PRINT("tok: %s\n", yytext);
				return EXEC; 
			}

SQLX[ \t\r\n]		{	
				PRINT("tok: %s\n", yytext);
				return SQLX; 
			}

SQL[ \t\r\n]		{	
				PRINT("tok: %s\n", yytext);
				return SQLX; 
			}

[0-9]+			{{
				int mode = esql_yy_mode();
				PRINT ("tok: %s\n", yytext);

				if (mode == ECHO_mode)
				  {
				    ECHO;
				  }
				else if (mode == C_mode)
				  {
				    ECHO;
				    return GENERIC_TOKEN;
				  }
				else if (mode == EXPR_mode)
				  {
				    ECHO;
				    return GENERIC_TOKEN;
				  }
				else if (mode == HV_mode)
				  {
				    vs_strcat (&pp_subscript_buf, yytext);
				
				    ECHO;
				  }
				else
				  {
				    ECHO;
				  }
				
			}}

[a-zA-Z_][a-zA-Z_0-9]*	{{
				int mode = esql_yy_mode ();
				int code;
				PRINT ("tok: %s\n", yytext);
				esql_yylval.ptr = mm_strdup (yytext);

				if (mode == ECHO_mode)
				  {
				    ECHO;
				  }
				else if (mode == C_mode)
				  {
				    code = check_c_identifier (yytext);
				    ECHO;

				    return code;
				  }
				else if (mode == EXPR_mode)
				  {
				    ECHO;
				    return GENERIC_TOKEN;
				  }
				else if (mode == CSQL_mode)
				  {
				    code = check_identifier (&csql_table, yytext);
				    ECHO;

				    return code;
				  }
				else if (mode == VAR_mode)
				  {
				    if (recognize_keywords)
				      {
					code = check_identifier (&preprocessor_table, yytext);
				      }
				    else
				      {
					code = check_c_identifier (yytext);
				      }
				
				    recognize_keywords = (code != INDICATOR);
				
				    if (code != IDENTIFIER)
				      {
					sprintf (g_delay, "%s", yytext);
				      }
				
				    else if (intl_mbs_casecmp (yytext, "WHERE") == 0)
				      {
					sprintf (g_delay, "%s", yytext);
				
				      }
				    else if (intl_mbs_casecmp (yytext, "AND") == 0)
				      {
					sprintf (g_delay, "%s", yytext);
				
				      }
				    else if (intl_mbs_casecmp (yytext, "OR") == 0)
				      {
					sprintf (g_delay, "%s", yytext);
				
				      }
				    else if (intl_mbs_casecmp (yytext, "ORDER") == 0)
				      {
					sprintf (g_delay, "%s", yytext);
				
				      }
				    else if (intl_mbs_casecmp (yytext, "GROUP") == 0)
				      {
					sprintf (g_delay, "%s", yytext);
				
				      }
				    else if (intl_mbs_casecmp (yytext, "UNION") == 0)
				      {
					sprintf (g_delay, "%s", yytext);
				
				      }
				    else if (intl_mbs_casecmp (yytext, "USING") == 0)
				      {
					sprintf (g_delay, "%s", yytext);
				
				      }
				
				
				    return code;
				  }
				else if (mode == BUFFER_mode)
				  {
				    ECHO;
				    code = IDENTIFIER;
				    if (intl_mbs_casecmp (yytext, "DESCRIPTOR") == 0)
				      {
					code = DESCRIPTOR;
				      }
				    else if (intl_mbs_casecmp (yytext, "INTO") == 0)
				      {
					code = INTO;
				      }
				    else if (intl_mbs_casecmp (yytext, "TO") == 0)
				      {
					code = INTO;
				      }
				    else if (intl_mbs_casecmp (yytext, "VALUES") == 0)
				      {
					code = VALUES;
				      }
				    else if (intl_mbs_casecmp (yytext, "SELECT") == 0)
				      {
					code = SELECT;
				      }

				    return code;
				  }
				else if (mode == HV_mode)
				  {
				    vs_strcat (&pp_subscript_buf, yytext);
				
				    ECHO;
				  }
				else
				  {
				    ECHO;
				    return IDENTIFIER;
				  }
				
			}}

\'((\'\')|([^\']))*\' 	{{
				int mode = esql_yy_mode ();
				PRINT ("tok: %s\n", yytext);

				if (mode == ECHO_mode)
				  {
				    ECHO;
				  }
				else if (mode == CSQL_mode)
				  {
				    ECHO;
				    esql_yylval.ptr = mm_strdup (yytext);
				    return STRING_LIT;
				  }
				else
				  {
				    ECHO;
				    return GENERIC_TOKEN;
				  }
				
			}}

.			{{
				int mode = esql_yy_mode ();
				PRINT ("tok: %s\n", yytext);

				if (mode == ECHO_mode)
				  {
				    ECHO;
				    switch (yytext[0])
				      {
				      case '{':
				      case '}':
					return yytext[0];
				      }
				  }
				else if (mode == C_mode)
				  {
				    ECHO;
				    return yytext[0];
				  }
				else if (mode == EXPR_mode)
				  {
				    ECHO;
				
				    switch (yytext[0])
				      {
				      case ';':
				      case ':':
				      case '(':
				      case ')':
				      case '[':
				      case ']':
				      case '{':
				      case '}':
					return yytext[0];
				      }
				  }
				else if (mode == VAR_mode)
				  {
				    switch (yytext[0])
				      {
				      case ',':
				      case '.':
				      case ':':
					recognize_keywords = false;
				      }
				
				
				    switch (yytext[0])
				      {
				      case '.':
				      case '&':
				      case '(':
				      case '[':
				      case ']':
				      case ';':
				      case '{':
				      case '}':
					return yytext[0];
				
				      case ')':
				      case ',':
				      case '*':
					sprintf (g_delay, "%s", yytext);
					return yytext[0];
				
				      case ':':
					if (g_indicator)
					  return '#';
					else
					  return yytext[0];
				
				      default:
					sprintf (g_delay, "%s", yytext);
					return GENERIC_TOKEN;
				      }
				  }
				else if (mode == BUFFER_mode)
				  {
				    char c = yytext[0];
				
				    switch (c)
				      {
				      case ',':
				      case '.':
				      case ':':
					recognize_keywords = false;
				      }
				
				
				    if (c == ':' || c == ';')
				      return c;
				
				    ECHO;
				  }
				else
				  {
				    return yytext[0];
				  }
				
			}}


%% 






int
esql_yywrap ()
{
#ifdef LEXER_DEBUG
  printf ("Parsing done.....\n");
#endif
  return 1;
}

static void
parser_c_comment (void)
{
  char c, c1;

loop:
  while ((c = input ()) != '*' && c != 0 && c != -1)
    {
      if (c == '\n')
	esql_yylineno++;
      (*echo_fn) (&c, 1);
    }

  if ((c1 = input ()) != '/' && c != 0 && c != -1)
    {
      if (c == '\n')
	esql_yylineno++;
      (*echo_fn) (&c, 1);
      unput (c1);
      goto loop;
    }

  (*echo_fn) (&c, 1);
  (*echo_fn) (&c1, 1);
}

static void
parser_ignore_comment (void)
{
  char c;

  while ((c = input ()) != '\r' && c != '\n' && c != 0 && c != -1)
    {
      // ignore
    }

  esql_yylineno++;
}

static void
parser_line_comment (void)
{
  char c;

  while ((c = input ()) != '\r' && c != '\n' && c != 0 && c != -1)
    {
      (*echo_fn) (&c, 1);
    }

  c = '\n';
  (*echo_fn) (&c, 1);
  esql_yylineno++;
}


static char *
parser_quoted_string (char end)
{
  char c, c1;
  char buff[8192];
  int i = 0;

  int size = 8192;
  char *bp = buff;

loop:
  while ((c = input ()) != end && c != 0 && c != -1)
    {
      bp[i++] = c;
      if (i >= size)
	{
	  char *bp_new = mm_malloc (size * 2);
	  memcpy (bp_new, bp, size);
	  bp = bp_new;
	  size *= 2;
	}
    }

  if (c == 0 || c == -1)
    {
      bp[i] = 0;
      return (bp == buff) ? mm_strdup (bp) : bp;
    }

  if ((c1 = input ()) == end && c != 0 && c != -1)
    {
      bp[i++] = c1;
      if (i >= size)
	{
	  char *bp_new = mm_malloc (size * 2);
	  memcpy (bp_new, bp, size);
	  bp = bp_new;
	  size *= 2;
	}
      goto loop;
    }

  unput (c1);
  bp[i] = 0;
  return (bp == buff) ? mm_strdup (bp) : bp;
}



int
esql_yyinput (char *buff, int max_size)
{
  int c;
  int i = 0;

  do
    {
      c = fgetc (esql_yyin);
      buff[i++] = c;
      if (i >= max_size)
	{
	  PRINT ("partial input: %s\n", buff);
	  return i;
	}

    }
  while (c != -1);

  if (c == -1)
    buff[--i] = 0;

  buff[i] = 0;
  buff[i + 1] = 0;
  buff[i + 2] = 0;
  buff[i + 3] = 0;
  buff[i + 4] = -1;
  PRINT ("input: %s\n", buff);
  return i;
}



