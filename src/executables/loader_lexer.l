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
 * loader_lexer.l - loader lexer file
 */

%{
#include "loader.h"
#include "loader_grammar.h"

#include "message_catalog.h"
#include "error_manager.h"
#include "memory_alloc.h"
#include "utility.h"
#include "intl_support.h"
#include "language_support.h"

/*#define LEXER_DEBUG*/

#ifdef LEXER_DEBUG
#define PRINT printf("lex: ");printf
#else
#define PRINT(a, b)
#endif

#define STRING_POOL_SIZE (1024)
#define COPY_BUF_POOL_SIZE (512)
#define QUOTED_STR_BUF_POOL_SIZE (512)
#define MAX_QUOTED_STR_BUF_SIZE (32 * 1024)
#define MAX_COPY_BUF_SIZE (256)

bool loader_In_instance_line = true;

extern void loader_yyerror (const char *s);
extern void loader_reset_string_pool (void);
extern void loader_initialize_lexer (void);

/* pool for string assigned to yyval */
static LDR_STRING string_Pool[STRING_POOL_SIZE];
static int string_Pool_idx = 0;

/* buffer pool for copying yytext and qstr_Malloc_buffer */
static char copy_Buf_pool[COPY_BUF_POOL_SIZE][MAX_COPY_BUF_SIZE];
static int copy_Buf_pool_idx = 0;

/* quoted string buffer pool */
static char qstr_Buf_pool[QUOTED_STR_BUF_POOL_SIZE][MAX_QUOTED_STR_BUF_SIZE];
static int qstr_Buf_pool_idx = 0;
static char *qstr_Malloc_buffer = NULL; /* using when pool overflow */
static int qstr_Malloc_buffer_size = 0;
static bool use_Qstr_malloc_buffer = false;
static char *qstr_Buf_p = NULL;
static int qstr_Buf_idx = 0;

static void loader_set_quoted_string_buffer (void);
static void loader_append_string (char c);
static LDR_STRING *loader_get_string_container (void);
static LDR_STRING *loader_make_string_by_yytext (void);
static LDR_STRING *loader_make_string_by_buffer (void);

extern int loader_yyline;
%}

%x BRACKET_ID DELIMITED_ID DQS SQS COMMENT

%%
[ \t]+	;

\r?\n {
    yylineno = loader_yyline++;
    return NL;
}

[Nn][Uu][Ll][Ll] {
    PRINT ("NULL_ %s\n", yytext);
    return NULL_;
}

[Cc][Ll][Aa][Ss][Ss] {
    PRINT ("CLASS %s\n", yytext);
    return CLASS;
}

[Ss][Hh][Aa][Rr][Ee][Dd] {
    PRINT ("SHARED %s\n", yytext);
    return SHARED;
}

[Dd][Ee][Ff][Aa][Uu][Ll][Tt] {
    PRINT ("DEFAULT%s\n", yytext);
    return DEFAULT;
}

[Dd][Aa][Tt][Ee] {
    PRINT ("DATE %s\n", yytext);
    return DATE_;
}

[Tt][Ii][Mm][Ee] {
    PRINT ("TIME %s\n", yytext);
    return TIME;
}

[Uu][Tt][Ii][Mm][Ee] {
    PRINT ("UTIME %s\n", yytext);
    return UTIME;
}

[Tt][Ii][Mm][Ee][Ss][Tt][Aa][Mm][Pp] {
    PRINT ("TIMESTAMP %s\n", yytext);
    return TIMESTAMP;
}

[Dd][Aa][Tt][Ee][Tt][Ii][Mm][Ee] {
    PRINT ("DATETIME %s\n", yytext);
    return DATETIME;
}

\%[Ii][Dd] {
    PRINT ("CMD_ID %s\n", yytext);
    loader_In_instance_line = false;
    return CMD_ID;
}

\%[Cc][Ll][Aa][Ss][Ss] {
    PRINT ("CMD_CLASS %s\n", yytext);
    loader_In_instance_line = false;
    return CMD_CLASS;
}

\%[Cc][Oo][Nn][Ss][Tt][Rr][Uu][Cc][Tt][Oo][Rr] {
    PRINT ("CMD_CONSTRUCTOR %s\n", yytext);
    return CMD_CONSTRUCTOR;
}

\^[Ii] {
    PRINT ("REF_ELO_INT %s\n", yytext);
    return REF_ELO_INT;
}

\^[Ee] {
    PRINT ("REF_ELO_EXT %s\n", yytext);
    return REF_ELO_EXT;
}

\^[Uu] {
    PRINT ("REF_USER %s\n", yytext);
    return REF_USER;
}

\^[Cc] {
    PRINT ("REF_CLASS %s\n", yytext);
    return REF_CLASS;
}

\@ {
    PRINT ("OBJECT_REFERENCE %s\n", yytext);
    return OBJECT_REFERENCE;
}

\| {
    PRINT ("OID_DELIMETER %s\n", yytext);
    return OID_DELIMETER;
}

\{ {
    PRINT ("SET_START_BRACE %s\n", yytext);
    return SET_START_BRACE;
}

\} {
    PRINT ("SET_END_BRACE %s\n", yytext);
    return SET_END_BRACE;
}

\( {
    PRINT ("START_PAREN %s\n", yytext);
    return START_PAREN;
}

\) {
    PRINT ("END_PAREN %s\n", yytext);
    return END_PAREN;
}

[\+\-]?(([0-9]+[Ee][\+\-]?[0-9]+[fFlL]?)|([0-9]*\.[0-9]+([Ee][\+\-]?[0-9]+)?[fFlL]?)|([0-9]+\.[0-9]*([Ee][\+\-]?[0-9]+)?[fFlL]?)) {
    PRINT ("REAL_LIT %s\n", yytext);
    loader_yylval.string = loader_make_string_by_yytext ();
    return REAL_LIT;
}

[\+\-]?[0-9]+ {
    PRINT ("INT_LIT %s\n", yytext);
    loader_yylval.string = loader_make_string_by_yytext ();
    return INT_LIT;
}

[0-9]+: {
    PRINT ("OID %s\n", yytext);
    loader_yylval.intval = atoi (yytext);
    return OID_;
}

[0-9]+:[0-9]+:[0-9]+[\ \t]*[aApP][mM] {
    PRINT ("TIME_LIT4 %s\n", yytext);
    loader_yylval.string = loader_make_string_by_yytext ();
    return TIME_LIT4;
}

[0-9]+:[0-9]+:[0-9]+[\ \t]* {
    PRINT ("TIME_LIT42 %s\n", yytext);
    loader_yylval.string = loader_make_string_by_yytext ();
    return TIME_LIT42;
}

[0-9]+:[0-9]+[\ \t]*[aApP][mM] {
    PRINT ("TIME_LIT3 %s\n", yytext);
    loader_yylval.string = loader_make_string_by_yytext ();
    return TIME_LIT3;
}

[0-9]+:[0-9]+[\ \t]* {
    PRINT ("TIME_LIT31 %s\n", yytext);
    loader_yylval.string = loader_make_string_by_yytext ();
    return TIME_LIT31;
}

[0-9]+:[0-9]+:[0-9]+ {
    PRINT ("TIME_LIT2 %s\n", yytext);
    loader_yylval.string = loader_make_string_by_yytext ();
    return TIME_LIT2;
}

[0-9]+:[0-9]+ {
    PRINT ("TIME_LIT1 %s\n", yytext);
    loader_yylval.string = loader_make_string_by_yytext ();
    return TIME_LIT1;
}

[0-9]+\/[0-9]+\/[0-9]+ {
    PRINT ("DATE_LIT2 %s\n", yytext);
    loader_yylval.string = loader_make_string_by_yytext ();
    return DATE_LIT2;
}

\xa1\xef {
    PRINT ("YEN_SYMBOL %s\n", yytext);
    return YEN_SYMBOL;
}

\\[J][P][Y] {
    PRINT ("YEN_SYMBOL %s\n", yytext);
    return YEN_SYMBOL;
}

\\[K][R][W] {
    PRINT ("WON_SYMBOL %s\n", yytext);
    return WON_SYMBOL;
}

\xa3\xdc {
    PRINT ("WON_SYMBOL %s\n", yytext);
    return WON_SYMBOL;
}

\\[T][L] {
    PRINT ("TURKISH_LIRA_CURRENCY %s\n", yytext);
    return TURKISH_LIRA_CURRENCY;
}

\\[T][R][Y] {
    PRINT ("TURKISH_LIRA_CURRENCY %s\n", yytext);
    return TURKISH_LIRA_CURRENCY;
}

\\[G][B][P] {
    PRINT ("BRITISH_POUND_SYMBOL %s\n", yytext);
    return BRITISH_POUND_SYMBOL;
}

\\[K][H][R] {
    PRINT ("CAMBODIAN_RIEL_SYMBOL %s\n", yytext);
    return CAMBODIAN_RIEL_SYMBOL;
}

\\[C][N][Y] {
    PRINT ("CHINESE_RENMINBI_SYMBOL %s\n", yytext);
    return CHINESE_RENMINBI_SYMBOL;
}

\\[I][N][R] {
    PRINT ("INDIAN_RUPEE_SYMBOL %s\n", yytext);
    return INDIAN_RUPEE_SYMBOL;
}

\\[R][U][B] {
    PRINT ("RUSSIAN_RUBLE_SYMBOL %s\n", yytext);
    return RUSSIAN_RUBLE_SYMBOL;
}

\\[A][U][D] {
    PRINT ("AUSTRALIAN_DOLLAR_SYMBOL %s\n", yytext);
    return AUSTRALIAN_DOLLAR_SYMBOL;
}

\\[C][A][D] {
    PRINT ("CANADIAN_DOLLAR_SYMBOL %s\n", yytext);
    return CANADIAN_DOLLAR_SYMBOL;
}

\\[B][R][L] {
    PRINT ("BRASILIAN_REAL_SYMBOL %s\n", yytext);
    return BRASILIAN_REAL_SYMBOL;
}

\\[R][O][N] {
    PRINT ("ROMANIAN_LEU_SYMBOL %s\n", yytext);
    return ROMANIAN_LEU_SYMBOL;
}

\\[E][U][R] {
    PRINT ("EURO_SYMBOL %s\n", yytext);
    return EURO_SYMBOL;
}

\\[C][H][F] {
    PRINT ("SWISS_FRANC_SYMBOL %s\n", yytext);
    return SWISS_FRANC_SYMBOL;
}

\\[D][K][K] {
    PRINT ("DANISH_KRONE_SYMBOL %s\n", yytext);
    return DANISH_KRONE_SYMBOL;
}

\\[N][O][K] {
    PRINT ("NORWEGIAN_KRONE_SYMBOL %s\n", yytext);
    return NORWEGIAN_KRONE_SYMBOL;
}

\\[B][G][N] {
    PRINT ("BULGARIAN_LEV_SYMBOL %s\n", yytext);
    return BULGARIAN_LEV_SYMBOL;
}

\\[V][N][D] {
    PRINT ("VIETNAMESE_DONG_SYMBOL %s\n", yytext);
    return VIETNAMESE_DONG_SYMBOL;
}

\\[C][Z][K] {
    PRINT ("CZECH_KORUNA_SYMBOL %s\n", yytext);
    return CZECH_KORUNA_SYMBOL;
}

\\[P][L][N] {
    PRINT ("POLISH_ZLOTY_SYMBOL %s\n", yytext);
    return POLISH_ZLOTY_SYMBOL;
}

\\[S][E][K] {
    PRINT ("SWEDISH_KRONA_SYMBOL %s\n", yytext);
    return SWEDISH_KRONA_SYMBOL;
}

\\[H][R][K] {
    PRINT ("CROATIAN_KUNA_SYMBOL %s\n", yytext);
    return CROATIAN_KUNA_SYMBOL;
}

\\[R][S][D] {
    PRINT ("SERBIAN_DINAR_SYMBOL %s\n", yytext);
    return SERBIAN_DINAR_SYMBOL;
}

\\ {
    PRINT ("BACKSLASH %s\n", yytext);
    return BACKSLASH;
}

\$ {
    PRINT ("DOLLAR_SYMBOL %s\n", yytext);
    return DOLLAR_SYMBOL;
}

\\[U][S][D] {
    PRINT ("DOLLAR_SYMBOL %s\n", yytext);
    return DOLLAR_SYMBOL;
}

([a-zA-Z_%#]|(\xa1[\xa2-\xee\xf3-\xfe])|([\xa2-\xfe][\xa1-\xfe])|(\x8e[\xa1-\xfe]))([a-zA-Z_%#0-9]|(\xa1[\xa2-\xfe])|([\xa1-\xfe])|(\x8e[\xa1-\xfe]))* {
    PRINT ("IDENTIFIER %s\n", yytext);
    loader_yylval.string = loader_make_string_by_yytext ();
    return IDENTIFIER;
}

[\'] {
    PRINT ("Quote %s\n", yytext);
    BEGIN SQS;
    loader_set_quoted_string_buffer ();
    return Quote;
}

[nN][\'] {
    PRINT ("NQuote %s\n", yytext);
    BEGIN SQS;
    loader_set_quoted_string_buffer ();
    return NQuote;
}

[bB][\'] {
    PRINT ("BQuote %s\n", yytext);
    BEGIN SQS;
    loader_set_quoted_string_buffer ();
    return BQuote;
}

[xX][\'] {
    PRINT ("XQuote %s\n", yytext);
    BEGIN SQS;
    loader_set_quoted_string_buffer ();
    return XQuote;
}

\" {
    loader_set_quoted_string_buffer ();
    if (loader_In_instance_line == true)
      {
        BEGIN DQS;
        return DQuote;
      }
    else
      {
        BEGIN DELIMITED_ID;
      }
}

"[" {
    loader_set_quoted_string_buffer ();
    BEGIN BRACKET_ID;
}

\\\n {
    yylineno = loader_yyline++;
    /* continue line */ ; 
}

"," {
    PRINT ("COMMA %s\n", yytext);
    return COMMA;
}

\/\/[^\r\n]*\r?\n {
    yylineno = loader_yyline++;
    /* C++ comments */
}

\-\-[^\r\n]*\r?\n {
    yylineno = loader_yyline++;
    /* SQL comments */
}

"/*" {
    BEGIN COMMENT;	/* C comments */
}

<COMMENT>.  |
<COMMENT>\n {
    yylineno = loader_yyline++;
}

<COMMENT>"*/" {
    BEGIN INITIAL;
}

<DELIMITED_ID>\"\" {
    loader_append_string ('"');
}

<DELIMITED_ID>[^\"] {
    loader_append_string (yytext[0]);
}

<DELIMITED_ID>\" {
    loader_append_string ('\0');
    PRINT ("IDENTIFIER %s\n", qstr_Buf_p);
    loader_yylval.string = loader_make_string_by_buffer ();
    BEGIN INITIAL;
    return IDENTIFIER;
}


<BRACKET_ID>[^\]] {
    loader_append_string (yytext[0]);
}

<BRACKET_ID>"]" {
    loader_append_string ('\0');
    PRINT ("IDENTIFIER %s\n", qstr_Buf_p);
    loader_yylval.string = loader_make_string_by_buffer ();
    BEGIN INITIAL;
    return IDENTIFIER;
}

<DQS>\\n {
    loader_append_string ('\n');
}

<DQS>\\t {
    loader_append_string ('\t');
}

<DQS>\\f {
    loader_append_string ('\f');
}

<DQS>\\r {
    loader_append_string ('\r');
}

<DQS>\\[0-7]([0-7][0-7]?)?  {
    loader_append_string ((char) strtol (&yytext[1], NULL, 8));
}

<DQS>\\x[0-9a-fA-F][0-9a-fA-F]?  {
    loader_append_string ((char) strtol (&yytext[2], NULL, 16));
}

<DQS>[^\"] {
    loader_append_string (yytext[0]);
}

<DQS>\\ {
    /* ignore */ ;
}

<DQS>\" {
    loader_append_string ('\0');
    PRINT ("DQS_String_Body %s\n", qstr_Buf_p);
    loader_yylval.string = loader_make_string_by_buffer ();
    BEGIN INITIAL;
    return DQS_String_Body;
}

<SQS>\'\' {
    loader_append_string ('\'');
}

<SQS>[^\'] {
    loader_append_string (yytext[0]);
}

<SQS>\'\+[ \t]*\r?\n[ \t]*\' {
    yylineno = loader_yyline++;
}

<SQS>\'[ \t] {
    loader_append_string ('\0');
    PRINT ("String_Completion %s\n", qstr_Buf_p);
    loader_yylval.string = loader_make_string_by_buffer ();
    BEGIN INITIAL;
    return SQS_String_Body;
}

<SQS>\' {
    loader_append_string ('\0');
    PRINT ("String_Completion2 %s\n", qstr_Buf_p);
    loader_yylval.string = loader_make_string_by_buffer ();
    BEGIN INITIAL;
    return SQS_String_Body;
}

%%

void loader_yyerror(const char *s)
{
  ldr_increment_err_total(ldr_Current_context);
  fprintf(stderr, 
          msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB,
                          LOADDB_MSG_SYNTAX_ERR),
          yylineno, yytext);
}

int
loader_yywrap ()
{
#ifdef LEXER_DEBUG
  printf ("Parsing done.....\n");
#endif
  return 1;
}

static void
loader_set_quoted_string_buffer (void)
{
  if (qstr_Buf_pool_idx < QUOTED_STR_BUF_POOL_SIZE)
    {
      qstr_Buf_p = &(qstr_Buf_pool[qstr_Buf_pool_idx][0]);
      qstr_Buf_pool_idx++;
      use_Qstr_malloc_buffer = false;
    }
  else
    {
      if (qstr_Malloc_buffer == NULL)
	{
	  qstr_Malloc_buffer_size = MAX_QUOTED_STR_BUF_SIZE;
	  qstr_Malloc_buffer = malloc (qstr_Malloc_buffer_size);
	  
	  if (qstr_Malloc_buffer == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	              ER_OUT_OF_VIRTUAL_MEMORY, 1, qstr_Malloc_buffer_size);
	      YY_FATAL_ERROR (er_msg());        
	      return;
	    }
	}

      qstr_Buf_p = qstr_Malloc_buffer;
      use_Qstr_malloc_buffer = true;
    }

  qstr_Buf_idx = 0;
}

static void
loader_append_string (char c)
{
  if (use_Qstr_malloc_buffer == true)
    {
      if (qstr_Buf_idx >= qstr_Malloc_buffer_size)
	{
	  qstr_Malloc_buffer_size *= 2;
	  qstr_Malloc_buffer = realloc (qstr_Malloc_buffer,
	                                qstr_Malloc_buffer_size);
	                                
	  if (qstr_Malloc_buffer == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	              ER_OUT_OF_VIRTUAL_MEMORY, 1, qstr_Malloc_buffer_size);
	      YY_FATAL_ERROR (er_msg());        
	      return;
	    }
	    
	  qstr_Buf_p = qstr_Malloc_buffer;
	}
    }
  else
    {
      if (qstr_Buf_idx >= MAX_QUOTED_STR_BUF_SIZE)
	{
	  if (qstr_Malloc_buffer != NULL
	      && qstr_Malloc_buffer_size <= MAX_QUOTED_STR_BUF_SIZE)
	    {
	      free_and_init (qstr_Malloc_buffer);
	      qstr_Malloc_buffer = NULL;
	    }

          if (qstr_Malloc_buffer == NULL)
            {
	      qstr_Malloc_buffer_size = MAX_QUOTED_STR_BUF_SIZE * 2;
	      qstr_Malloc_buffer = malloc (qstr_Malloc_buffer_size);
	  
	      if (qstr_Malloc_buffer == NULL)
	        {
	          er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	                  ER_OUT_OF_VIRTUAL_MEMORY, 1, 
	                  qstr_Malloc_buffer_size);
	          YY_FATAL_ERROR (er_msg());        
	          return;
	        }
	    }
	    
	  memcpy (qstr_Malloc_buffer, qstr_Buf_p, qstr_Buf_idx);
	  qstr_Buf_p = qstr_Malloc_buffer;
	  qstr_Buf_pool_idx--;
	  use_Qstr_malloc_buffer = true;
	}
    }

  qstr_Buf_p[qstr_Buf_idx] = c;
  qstr_Buf_idx++;
}

static LDR_STRING *
loader_get_string_container (void)
{
  LDR_STRING *str;

  if (string_Pool_idx < STRING_POOL_SIZE)
    {
      str = &(string_Pool[string_Pool_idx]);
      str->need_free_self = false;
      string_Pool_idx++;
    }
  else
    {
      str = (LDR_STRING *) malloc (sizeof (LDR_STRING));

      if (str == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	         ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (LDR_STRING));
	  YY_FATAL_ERROR (er_msg());        
	  return NULL;
	}

      str->need_free_self = true;
    }

  return str;
}

static LDR_STRING *
loader_make_string_by_yytext (void)
{
  LDR_STRING *str;
  char *invalid_pos = NULL;

  str = loader_get_string_container ();
  if (str == NULL)
    {
      return NULL;
    }

  str->size = yyleng;

  if (copy_Buf_pool_idx < COPY_BUF_POOL_SIZE 
      && str->size < MAX_COPY_BUF_SIZE)
    {
      str->val = &(copy_Buf_pool[copy_Buf_pool_idx][0]);
      memcpy (str->val, yytext, str->size);
      str->val[str->size] = '\0';
      str->need_free_val = false;
      copy_Buf_pool_idx++;
    }
  else
    {
      str->val = malloc (str->size + 1);
      
      if (str->val == NULL)
        {
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	          ER_OUT_OF_VIRTUAL_MEMORY, 1, str->size + 1);
	  YY_FATAL_ERROR (er_msg());        
	  return NULL;
        }
        
      memcpy (str->val, yytext, str->size);
      str->val[str->size] = '\0';
      str->need_free_val = true;
    }

  if (intl_check_string (str->val, str->size, &invalid_pos,
			 LANG_SYS_CODESET) != 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INVALID_CHAR, 1,
	      invalid_pos - str->val);
      YY_FATAL_ERROR (er_msg());
      return NULL;
    }

  return str;
}

static LDR_STRING *
loader_make_string_by_buffer (void)
{
  LDR_STRING *str;
  char *invalid_pos = NULL;

  str = loader_get_string_container ();
  if (str == NULL)
    {
      return NULL;
    }

  str->size = qstr_Buf_idx - 1;

  if (use_Qstr_malloc_buffer == false)
    {
      str->val = qstr_Buf_p;
      str->need_free_val = false;
    }
  else
    {
      if (copy_Buf_pool_idx < COPY_BUF_POOL_SIZE 
          && str->size < MAX_COPY_BUF_SIZE)
	{
	  str->val = &(copy_Buf_pool[copy_Buf_pool_idx][0]);
	  memcpy (str->val, qstr_Buf_p, qstr_Buf_idx);
	  str->need_free_val = false;
	  copy_Buf_pool_idx++;
	}
      else
	{
	  str->val = (char *) malloc (qstr_Buf_idx);

	  if (str->val == NULL)
	    {
	      if (str->need_free_self)
		{
		  free_and_init (str);
		}
		
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	              ER_OUT_OF_VIRTUAL_MEMORY, 1, qstr_Buf_idx);
	      YY_FATAL_ERROR (er_msg());        
	      return NULL;
	    }

	  memcpy (str->val, qstr_Buf_p, qstr_Buf_idx);
	  str->need_free_val = true;
	}
    }
    
  if (intl_check_string (str->val, str->size, &invalid_pos,
			 LANG_SYS_CODESET) != 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INVALID_CHAR, 1,
	      invalid_pos - str->val);
      YY_FATAL_ERROR (er_msg());
      return NULL;
    }    

  return str;
}

void
loader_reset_string_pool (void)
{
  string_Pool_idx = 0;
  copy_Buf_pool_idx = 0;
  qstr_Buf_pool_idx = 0;
}

void
loader_initialize_lexer (void)
{
  string_Pool_idx = 0;
  copy_Buf_pool_idx = 0;
  qstr_Buf_pool_idx = 0;
  qstr_Malloc_buffer = NULL;
}

