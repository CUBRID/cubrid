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
 * esql_grammar.g - String conversion functions
 */

#header
<<
#ident "$Id$"
#include "config.h"
#define ZZA_STACKSIZE       20000
#define ZZLEXBUFSIZE        17000
#define ZZERRSTD_SUPPLIED   1
#define ZZ_PREFIX           es_
#define ZZSYNSUPPLIED       1
#define ECHO_mode START
#include <ctype.h>
#include <stdlib.h>
#include "int.h"
#include "language_support.h"
#include "message_catalog.h"
#include "esql_misc.h"
#include "misc_string.h"
#include "variable_string.h"
#include "parser.h"
#include "parser.h"
#include "esql_translate.h"
#if (defined(AIX) || defined(HPUX)) && defined(STRING)
#undef STRING
#endif
#include "memory_alloc.h"
#define ESQLMSG(x) pp_get_msg(EX_ESQLM_SET, (x))
#if defined(YYDEBUG)
#define es_zzTRACE(str) printf("\t...calling %s...\n", str)
#else
#define es_zzTRACE(str)
#endif
  >><<
#include "esql_main.h"
  enum
{
  MSG_DONT_KNOW = 1,
  MSG_CHECK_CORRECTNESS = 2,
  MSG_USING_NOT_PERMITTED = 3,
  MSG_CURSOR_UNDEFINED = 4,
  MSG_PTR_TO_DESCR = 5,
  MSG_PTR_TO_DB_OBJECT = 6,
  MSG_CHAR_STRING = 7,
  MSG_INDICATOR_NOT_ALLOWED = 8,
  MSG_NOT_DECLARED = 9,

  ESQL_MSG_STD_ERR = 52,
  ESQL_MSG_LEX_ERROR = 53,
  ESQL_MSG_SYNTAX_ERR1 = 54,
  ESQL_MSG_SYNTAX_ERR2 = 55
};

#define NO_CURSOR          0
#define CURSOR_DELETE      1
#define CURSOR_UPDATE      2

#define zzEOF_TOKEN 1

/*
 * zzsyn() - issues a syntax error message to stderr.
 * return : void
 * text(in): printname of the offending (error) token
 * tok(in): integer code of the offending (error) token
 * egroup(in): user-supplied printname of nonterminal (the lhs
 *             of the production where the error was detected)
 * eset(in): bit encoded FOLLOW set
 * etok(in): a one-element FOLLOW set
 */
void
zzsyn (const char *text, long tok, const char *egroup,
       unsigned long *eset, long etok)
{
  const char *follow = NULL;

  follow = etok ? zztokens[etok] : zzedecode (eset);
  if (tok == zzEOF_TOKEN)
    text = "EOF";

  if (strlen (egroup) > 0)
    yyverror (ESQLMSG (ESQL_MSG_SYNTAX_ERR1), text, follow, egroup);
  else
    yyverror (ESQLMSG (ESQL_MSG_SYNTAX_ERR2), text, follow);
}

/*
 * zzerrstd() - issues a lexical error message to stderr.
 * return : void
 * s(in): a brief characterization of the lexical error detected
 */
void
zzerrstd (const char *s)
{
  long length;
  char buffer[DB_MAX_IDENTIFIER_LENGTH * 2];

  if (LANG_VARIABLE_CHARSET (lang_charset ()))
    {
      length = strlen (zzlextext);
      if (length > 0)
	{
	  if (zzlextext[length - 1] & 0x80 != 0)
	    {
	      strcpy (buffer, zzlextext);
	      buffer[length] = (unsigned char) zzchar;
	      buffer[length + 1] = '\0';
	      zzreplstr (buffer);
	    }
	}
    }
  strcpy (buffer, ESQLMSG (ESQL_MSG_LEX_ERROR));
  yyverror (ESQLMSG (ESQL_MSG_STD_ERR),
	    ((s == NULL) ? buffer : s), zzlextext);
}

/*
 * pt_print_errors() - print to stderr all parsing error messages associated
 *    with current statement
 * return : void
 * parser_in(in) : the parser context
 */
static void
pt_print_errors (PARSER_CONTEXT * parser_in)
{
  int statement_no, line_no, col_no;
  const char *msg = NULL;
  PT_NODE *err = NULL;

  err = pt_get_errors (parser);
  do
    {
      err = pt_get_next_error (err, &statement_no, &line_no, &col_no, &msg);
      if (msg)
	yyverror (msg);
    }
  while (err);

  /* sleazy way to clear errors */
  parser = parser_create_parser ();
}

>>
#lexaction
<<static char exec_echo[10] = "";
#include "esql_scanner_support.h"
extern void zzerrstd (const char *s);

/*
 * consume_C_macro() -
 * return:
 */
void
consume_C_macro ()
{
  ECHO;
  count_embedded_newlines ();
  zzskip ();
}

>>
#lexclass START			/* ECHO_mode */
#token ADD
#token ALL
#token ALTER
#token AMPERSAND
#token AND
#token AS
#token ASC
#token ATTRIBUTE
#token ATTACH
#token AUTO_
#token AVG
#token BAD_TOKEN
#token BOGUS_ECHO
#token BEGIN
#token BETWEEN
#token BIT_
#token BY
#token CALL_
#token CHANGE
#token CHAR_
#token CHARACTER
#token CHECK_
#token CLASS
#token CLOSE
#token COLON
#token COLON_INDICATOR
#token COMMA
#token COMMIT
#token CONNECT
#token CONST_
#token CONTINUE_
#token COUNT
#token CREATE
#token CURRENT
#token CURSOR_
#token DATE
#token DATE_LIT
#token DECLARE
#token DEFAULT
#token DELETE
#token DESC
#token DESCRIBE
#token DESCRIPTOR
#token DIFFERENCE
#token DISCONNECT
#token DISTINCT
#token DOT
#token DOUBLE_
#token DROP
#token ELLIPSIS
#token END
#token ENUM
#token ENUMERATION_CONSTANT
#token EQ
#token ESCAPE
#token EVALUATE
#token EXCEPT
#token EXCLUDE
#token EXECUTE
#token EXISTS
#token EXTERN_
#token FETCH
#token FILE_
#token FILE_PATH_LIT
#token FLOAT_
#token FOR
#token FOUND
#token FROM
#token FUNCTION_
#token GENERIC_TOKEN
#token GEQ
#token GET
#token GO
#token GOTO_
#token GRANT
#token GROUP
#token GT
#token HAVING
#token IDENTIFIED
#token IDENTIFIER
#token IMMEDIATE
#token IN
#token INCLUDE
#token INDEX
#token INDICATOR
#token INHERIT
#token INSERT
#token INTEGER
#token INTERSECTION
#token INTO
#token INT_
#token INT_LIT
#token IS
#token LB
#token LC
#token LDBVCLASS
#token LEQ
#token LIKE
#token LONG_
#token LP
#token LT
#token MAX_
#token METHOD
#token MIN_
#token MINUS
#token MONEY_LIT
#token MULTISET_OF
#token NEQ
#token NOT
#token NULL_
#token NUMERIC
#token OBJECT
#token OF
#token OID
#token ON_
#token ONLY
#token OPEN
#token OPTION
#token OR
#token ORDER
#token PLUS
#token PRECISION
#token PREPARE
#token PRIVILEGES
#token PTR_OP
#token PUBLIC_
#token RB
#token RC
#token READ
#token READ_ONLY
#token REAL
#token REAL_LIT
#token REGISTER_
#token RENAME
#token REPEATED
#token REVOKE
#token ROLLBACK
#token RP
#token SECTION
#token SELECT
#token SEMI
#token SEQUENCE_OF
#token SET
#token SETEQ
#token SETNEQ
#token SET_OF
#token SHARED
#token SHORT_
#token SIGNED_
#token SLASH
#token SMALLINT
#token SOME
#token SQLCA
#token SQLDA
#token SQLERROR_
#token SQLWARNING_
#token SQLX
#token STAR
#token STATIC_
#token STATISTICS
#token STOP_
#token STRING
#token STRING_LIT
#token STRUCT_
#token SUBCLASS
#token SUBSET
#token SUBSETEQ
#token SUM
#token SUPERCLASS
#token SUPERSET
#token SUPERSETEQ
#token TABLE
#token TIME
#token TIMESTAMP
#token TIME_LIT
#token TO
#token TRIGGER
#token TYPEDEF_
#token TYPEDEF_NAME
#token UNION_
#token UNIQUE
#token UNSIGNED_
#token UPDATE
#token USE
#token USER
#token USING
#token UTIME
#token VALUES
#token VARBIT_
#token VARCHAR_
#token VARYING
#token VCLASS
#token VIEW
#token VOID_
#token VOLATILE_
#token WHENEVER
#token WHERE
#token WITH
#token WORK
#token "[\ \t]*#(\\~[]|\\\n|~[\\\n])*\n"  <<consume_C_macro();>>
#token "/\*"  <<yy_push_mode(COMMENT_mode); ECHO; zzskip();>>
#token "[\t\ ]+" <<ECHO; zzskip();>>
#token "[\n]"    <<ECHO; zzline++; zzendcol = 0; CHECK_LINENO; zzskip();>>
/* dlg insists on trying to match the longest token prefix, thus the need for */
/* the following silly regular expressions that try to distinguish between an */
/* EXEC SQLX token and non EXEC SQLX tokens like: EXECUTING, EXECUTABLE, etc. */
/* Without these silly NON_EXEC_SQLX regular exprs, dlg is apt to find false  */
/* lexical errors.  When you see an error message like this:		      */
/*   'invalid token near line n (zzlextext was ' e')                          */
/* chances are dlg tripped again in its attempt to match a complex reg expr.  */
/* So, the following reg exprs look like EXEC SQLX token prefixes but are not.*/
#token "[Ee]~[Xx]"					<<ECHO; zzskip();>>
#token "[Ee][Xx]~[Ee]"					<<ECHO; zzskip();>>
#token "[Ee][Xx][Ee]~[Cc]"				<<ECHO; zzskip();>>
#token "[Ee][Xx][Ee][Cc]~[\ \t\n]" 			<<ECHO; zzskip();>>
#token EXEC "[Ee][Xx][Ee][Cc][\ \t\n]+"
  <<count_embedded_newlines ();
strcpy (exec_echo, zzlextext);
>>
#token "[Ss][A-PR-Za-pr-z_0-9][a-zA-Z_0-9]*"
  <<ECHO;
zzskip ();
>>
#token "[Ss][Qq][A-KM-Za-km-z_0-9][a-zA-Z_0-9]*"
  <<ECHO;
zzskip ();
>>
#token SQLX "[Ss][Qq][Ll]{[XxMm]}[\ \t\n]"
  <<count_embedded_newlines ();
if (!exec_echo[0])
  ECHO;
exec_echo[0] = 0;
>>
#token "'(\\~[]|~[\\'])+'"
  <<ECHO;
zzskip ();
>>
#token "\"(\\~[]|\\\n|~[\\\"])*\""
  <<ECHO;
count_embedded_newlines ();
zzskip ();
>>
#token LC "\{" <<ECHO;>>
#token RC "\}" <<ECHO;>>
#token "~[/\*E\ \t\n'\"\{\}]+" <<ECHO; zzskip();>>
#token "~[\n]"  <<ECHO; zzskip();>>
  <<
#define ECHO_YY(s) if (s) fwrite((char *)s, strlen(s), 1, yyout)
>>translation_unit:
(embedded_chunk) * <<pp_finish ();
vs_free (&pt_statement_buf);
  /* verify push/pop modes were matched. */
yy_check_mode ();
>>;

embedded_chunk:
EXEC sql_or_other | LC << pp_push_name_scope ();
>>(embedded_chunk) * <<pp_pop_name_scope ();
>>RC | <<ECHO;			/* Used to be: ECHO_YY(zzlextext); */
>>SQLX;

sql_or_other:
(sql (declare_section | esql_statement) << yy_enter (ECHO_mode);
 pp_suppress_echo (false);
 >>SEMI | /* or anything else */ )
  ;

sql:SQLX << yy_enter (SQLX_mode);
reset_globals ();
>>;


#lexclass SQLX_mode
#token "[\ \t]*#(\\~[]|\\\n|~[\\\n])*\n"  <<consume_C_macro();>>
#token "/\*"  <<yy_push_mode(COMMENT_mode); ECHO; zzskip();>>

#token IDENTIFIER "((([a-zA-Z_%#]|(\0xa1[\0xa2-\0xfe])|([\0xa2-\0xfe][\0xa1-\0xfe])|(\0x8e[\0xa1-\0xfe]))([a-zA-Z_%#0-9]|(\0xa1[\0xa2-\0xfe])|([\0xa2-\0xfe][\0xa1-\0xfe])|(\0x8e[\0xa1-\0xfe]))*)|(\"((\"\")|(~[\"]))*\"))"
<<
{
  long type;
  type = check_identifier (&csql_table);
  ECHO;
  LA (1) = type;
  if (type == IDENTIFIER && zzlextext[0] == '\"')
    {
      /*
       * This thing came in as a quoted identifier.  However, no
       * consumer of this identifier in this mode (lexclass
       * SQLX_mode) knows anything about quoted identifiers, and
       * will in fact go bonkers is confronted with one.  So we
       * have to strip the quotes here.
       *
       * Contrast this with buffer_mode, where no one actually
       * *uses* the identifiers, but just spits them out into a
       * SQL buffer.  In that case we definitely want to keep
       * the quotes on the identifiers, which is what happens
       * naturally.
       */
      int len;
      len = strlen (zzlextext);
      memmove (zzlextext, &zzlextext[1], len - 2);
      zzlextext[len - 2] = '\0';
    }
}

>>
#token STRING_LIT "'(('')|(~[']))*'"
  <<count_embedded_newlines ();
PP_STR_ECHO;
>>
#token "[\ \t]+" <<ECHO_SP; zzskip();>>
#token "[\n]"    <<ECHO_SP; zzline++; zzendcol = 0; zzskip();>>
#token "\-\-~[\n]*\n" <<zzline++; zzendcol = 0; zzskip();>>
#token COMMA      ","   <<ECHO;>>
#token SEMI       ";"   <<ECHO;>>
#token DOT        "."   <<ECHO;>>
#token LP         "\("  <<ECHO;>>
#token RP         "\)"  <<ECHO;>>
#token LB         "\["  <<ECHO;>>
#token RB         "\]"  <<ECHO;>>
#token LC         "\{"  <<ECHO;>>
#token RC         "\}"  <<ECHO;>>
#token PLUS       "\+"  <<ECHO;>>
#token MINUS      "\-"  <<ECHO;>>
#token STAR       "\*"  <<ECHO;>>
#token SLASH      "/"   <<ECHO;>>
#token GT         "\>"  <<ECHO;>>
#token LT         "\<"  <<ECHO;>>
#token EQ         "="   <<ECHO;>>
#token GEQ        "\>=" <<ECHO;>>
#token LEQ        "\<=" <<ECHO;>>
#token NEQ        "(\!=|\<\>)" <<ECHO;>>
#token COLON      ":"   <<ECHO;>>
#token BAD_TOKEN  "~[\n]"
  <<fprintf (stderr, "%d: scanning bad token '%c'\n", zzline, zzlextext[0]);
    fflush (stderr);
    ECHO;
  >>

declare_section:
BEGIN DECLARE SECTION SEMI << yy_enter (C_mode);
pp_suppress_echo (false);
>>declare_section_body
  /* note "sql" does lexclass mode switch */
  EXEC sql END DECLARE SECTION;

/* CSQL statement section. */
esql_statement:
( /* empty */ <<repeat_option = false;
 >>|REPEATED << repeat_option = true;
 >>)
  (embedded_statement
   << ignore_repeat (); yy_sync_lineno (); yy_set_buf (NULL);
   >>|declare_cursor_statement << ignore_repeat ();
   >>|<<vs_clear (&pt_statement_buf); yy_set_buf (&pt_statement_buf);
   >>csql_statement);

embedded_statement:whenever_statement
  | connect_statement
  | disconnect_statement
  | open_statement
  | close_statement
  | describe_statement
  | prepare_statement | execute_statement | fetch_statement | INCLUDE
{
SQLCA | SQLDA_}

|commit_statement | rollback_statement;

whenever_statement:<<long cond;
>>WHENEVER (SQLWARNING_ << cond = SQLWARNING;
	    >>|SQLERROR_ << cond = SQLERROR;
	    >>|NOT FOUND << cond = NOT_FOUND;
	    >>)(CONTINUE_ << pp_add_whenever_to_scope (cond, CONTINUE, NULL);
		>>|STOP_ << pp_add_whenever_to_scope (cond, STOP, NULL);
		>>|GOTO_
		{
		COLON} IDENTIFIER
		<< pp_add_whenever_to_scope (cond, GOTO, zzlextext);
		>>|GO TO
		{
		COLON} IDENTIFIER
		<< pp_add_whenever_to_scope (cond, GOTO, zzlextext);
		>>|CALL_ IDENTIFIER
		<< pp_add_whenever_to_scope (cond, CALL, zzlextext);
		>>);

connect_statement:
<<HOST_REF * href;
HOST_LOD *hvars;
int ok = 1;
>>CONNECT quasi_string_const >[href] << ok &= (href != NULL);
>>
{
  IDENTIFIED BY quasi_string_const >[href] << ok &= (href != NULL);
  >>
  {
    WITH quasi_string_const >[href] << ok &= (href != NULL);
>>}}

<<if (ok)
  {
    hvars = pp_input_refs ();
    esql_Translate_table.tr_connect (CHECK_HOST_REF (hvars, 0),
				     CHECK_HOST_REF (hvars, 1),
				     CHECK_HOST_REF (hvars, 2));
    already_translated = true;
  }
>>;

disconnect_statement:DISCONNECT << esql_Translate_table.tr_disconnect ();
already_translated = true;
>>;

declare_cursor_statement:<<char *c_nam, *qry;
int length;
>>DECLARE IDENTIFIER << c_nam = strdup (zzlextext);
>>CURSOR_ FOR
  (cursor_query_statement >[qry, length]
   <<
   (void) pp_new_cursor (c_nam,
			 qry,
			 length,
			 NULL, pp_detach_host_refs ()); free_and_init (c_nam);
   >>|<<STMT * d_statement;
   >>dynamic_statement >[d_statement]
   <<
   if (d_statement)
   (void) pp_new_cursor (c_nam,
			 NULL,
			 0,
			 d_statement,
			 pp_detach_host_refs ());
   free_and_init (c_nam); yy_sync_lineno (); yy_set_buf (NULL); >>)
  ;


dynamic_statement >[STMT * statement]:<<$statement = NULL;
>>IDENTIFIER << $statement = pp_new_stmt (zzlextext);
>>;

open_statement:<<CURSOR * cur;
long rdonly;
HOST_LOD *usg;
>>OPEN cursor >[cur] ( /* empty */ <<rdonly = 0;
		      >>|FOR READ ONLY << rdonly = 1;
		      >>)using_part << if (cur)
  {
    if (cur->static_stmt)
      {
	if (pp_input_refs ())
	  yyverror (pp_get_msg (EX_ESQLM_SET,
				MSG_USING_NOT_PERMITTED), cur->name);
	else
	  esql_Translate_table.tr_open_cs (cur->cid,
					   cur->static_stmt,
					   cur->stmtLength,
					   -1,
					   rdonly,
					   HOST_N_REFS (cur->host_refs),
					   HOST_REFS (cur->host_refs),
					   HOST_DESC (cur->host_refs));
      }
    else if (cur->dynamic_stmt)
      {
	usg = pp_input_refs ();
	esql_Translate_table.tr_open_cs (cur->cid,
					 NULL,
					 (int) 0,
					 cur->dynamic_stmt->sid,
					 rdonly,
					 HOST_N_REFS (usg), HOST_REFS (usg),
					 HOST_DESC (usg));
      }
    else
      {
	yyverror (pp_get_msg (EX_ESQLM_SET, MSG_CURSOR_UNDEFINED), cur->name);
      }
  }
already_translated = true;
>>;

using_part:			/* empty */
|USING host_variable_lod;

descriptor_name >[char *nam]:<<HOST_REF * href;
>>host_variable_wo_indicator >[href] << $nam = NULL;
if (pp_check_type (href,
		   NEWSET (C_TYPE_SQLDA),
		   pp_get_msg (EX_ESQLM_SET, MSG_PTR_TO_DESCR)))
  $nam = pp_switch_to_descriptor ();
>>;

close_statement:<<CURSOR * cur;
>>CLOSE cursor >[cur] << if (cur)
  esql_Translate_table.tr_close_cs (cur->cid);
already_translated = true;
>>;

cursor >[CURSOR * cur]:<<$cur = NULL;
>>IDENTIFIER << if (($cur = pp_lookup_cursor (zzlextext)) == NULL)
  yyverror (pp_get_msg (EX_ESQLM_SET, MSG_CURSOR_UNDEFINED), zzlextext);
>>;

describe_statement:DESCRIBE (<<STMT * d_statement;
	  char *nam;
	  >>dynamic_statement >[d_statement]
	  INTO descriptor_name >[nam]
	  <<
	  if (d_statement && nam)
	  esql_Translate_table.tr_describe (d_statement->sid, nam);
	  already_translated = true;
	  >>|<<HOST_REF * href; PTR_VEC * pvec;
	  >>OBJECT host_variable_wo_indicator >
	  [href] << (void) pp_copy_host_refs ();
	  >>ON_ id_list_ >[pvec] INTO descriptor_name >[nam] << if (href
								    && pvec
								    && nam
								    &&
								    pp_check_type
								    (href,
								     NEWSET
								     (C_TYPE_OBJECTID),
								     pp_get_msg
								     (EX_ESQLM_SET,
								      MSG_PTR_TO_DB_OBJECT)))
	  {
	  esql_Translate_table.tr_object_describe (href,
						   pp_ptr_vec_n_elems (pvec),
						   (const char **)
						   pp_ptr_vec_elems (pvec),
						   nam);}
	  pp_free_ptr_vec (pvec); already_translated = true; >>)
  ;

prepare_statement:
<<STMT * d_statement;
HOST_REF *href;
>>PREPARE dynamic_statement >[d_statement]
     FROM
quasi_string_const >[href] << if (d_statement && href)
  esql_Translate_table.
  tr_prepare_esql (d_statement->sid, href);
already_translated = true;
>>;

into_result:<<pp_gather_output_refs ();
yy_erase_last_token ();
yy_set_buf (NULL);
>>(INTO | TO) host_variable_lod << pp_gather_input_refs ();
>>;

host_variable_lod		/* list or descriptor */
   :<<const char *
       nam;
>>(host_variable_list | DESCRIPTOR descriptor_name >[nam]);

quasi_string_const >[HOST_REF * href]:<<$href = NULL;
>>(STRING_LIT << $href = pp_add_host_str (zzlextext);
   >>|<<HOST_REF * href2;
   >>host_variable_wo_indicator >[href2]
   <<
   $href = pp_check_type (href2,
			  NEWSET (C_TYPE_CHAR_ARRAY) |
			  NEWSET (C_TYPE_CHAR_POINTER) |
			  NEWSET (C_TYPE_STRING_CONST) |
			  NEWSET (C_TYPE_VARCHAR) |
			  NEWSET (C_TYPE_NCHAR) |
			  NEWSET (C_TYPE_VARNCHAR),
			  pp_get_msg (EX_ESQLM_SET, MSG_CHAR_STRING));
   >>);

host_variable_list:<<yy_erase_last_token ();
structs_allowed = true;
>>host_variable_list_tail << pp_check_host_var_list ();
ECHO;
>>;

host_variable_wo_indicator >[HOST_REF * href]:<<HOST_REF * href2;
yy_erase_last_token ();
>>host_var_w_opt_indicator >[href2] << if (href2 && href2->ind)
  {
    yyverror (pp_get_msg (EX_ESQLM_SET, MSG_INDICATOR_NOT_ALLOWED),
	      pp_get_expr (href2));
    pp_free_host_var (href2->ind);
    href2->ind = NULL;
  }
$href = href2;
>>;

id_list_ >[PTR_VEC * pvec]:<<$pvec = NULL;
>>IDENTIFIER << $pvec = pp_new_ptr_vec (&id_list);
(void) pp_add_ptr (&id_list, strdup (zzlextext));
>>(COMMA IDENTIFIER << $pvec = pp_add_ptr ($pvec, strdup (zzlextext)); >>) *;

execute_statement:EXECUTE (<<STMT * d_statement;
	 >>dynamic_statement >[d_statement] using_part
	 {
	 into_result}

	 <<if (d_statement)
	 {
	 HOST_LOD * inputs, *outputs;
	 inputs = pp_input_refs ();
	 outputs = pp_output_refs ();
	 esql_Translate_table.tr_execute (d_statement->sid,
					  HOST_N_REFS (inputs),
					  HOST_REFS (inputs),
					  HOST_DESC (inputs),
					  HOST_N_REFS (outputs),
					  HOST_REFS (outputs),
					  HOST_DESC (outputs));
	 already_translated = true;}
	 >>|<<HOST_REF * href;
	 >>IMMEDIATE quasi_string_const >
	 [href] << if (href) esql_Translate_table.tr_execute_immediate (href);
	 already_translated = true; >>)
  ;

fetch_statement:FETCH (<<CURSOR * cur;
       HOST_LOD * hlod; >>cursor >[cur] into_result << if (cur)
       {
       hlod = pp_output_refs ();
       esql_Translate_table.tr_fetch_cs (cur->cid,
					 HOST_N_REFS (hlod), HOST_REFS (hlod),
					 HOST_DESC (hlod));}
       already_translated = true; >>|<<HOST_REF * href; PTR_VEC * pvec;
       HOST_LOD * hlod;
       >>OBJECT host_variable_wo_indicator >
       [href] << (void) pp_copy_host_refs ();
       >>ON_ id_list_ >[pvec] into_result << if (href && pvec
						 && (hlod = pp_output_refs ())
						 && pp_check_type (href,
								   NEWSET
								   (C_TYPE_OBJECTID),
								   pp_get_msg
								   (EX_ESQLM_SET,
								    MSG_PTR_TO_DB_OBJECT)))
       {
       esql_Translate_table.tr_object_fetch (href,
					     pp_ptr_vec_n_elems (pvec),
					     (const char **)
					     pp_ptr_vec_elems (pvec),
					     HOST_N_REFS (hlod),
					     HOST_REFS (hlod),
					     HOST_DESC (hlod));}
       pp_free_ptr_vec (pvec); already_translated = true; >>)
  ;

commit_statement:COMMIT
{
WORK}

<<esql_Translate_table.tr_commit ();
already_translated = true;
>>;

rollback_statement:ROLLBACK
{
WORK}

<<esql_Translate_table.tr_rollback ();
already_translated = true;
>>;

csql_statement:
<<yy_set_buf (&pt_statement_buf);
ECHO;				/* copy_lookahead */
yy_push_mode (BUFFER_mode);
>>(ALTER
   | ATTACH
   | CALL_
   | CREATE
   | DROP
   | END
   | EVALUATE
   | EXCLUDE
   | FOR
   | GET
   | GRANT
   | INSERT
   | ON_
   | REGISTER_
   | RENAME
   | REVOKE
   | SELECT
   | SET
   | TRIGGER
   | USE)
  csql_statement_tail[NO_CURSOR]
  | DELETE
  csql_statement_tail[CURSOR_DELETE]
  | UPDATE csql_statement_tail[CURSOR_UPDATE];


#lexclass C_mode
#token "[\ \t]*#(\\~[]|\\\n|~[\\\n])*\n"  <<consume_C_macro();>>
#token "/\*"  <<yy_push_mode(COMMENT_mode); ECHO; zzskip();>>

#token "[\t\ ]+" <<ECHO; zzskip();>>
#token "[\n]"    <<ECHO; zzline++; zzendcol = 0; CHECK_LINENO; zzskip();>>

/* dlg insists on trying to match the longest token prefix, thus the need for */
/* the following C identifier regular expressions.  Without these reg exprs,  */
/* dlg is apt to find false lexical errors when given input like 'enum e00'.  */
/* When you see an error message like this:                                   */
/*   'invalid token near line n (zzlextext was ' e')                          */
/* chances are dlg tripped again in its attempt to match a complex reg expr.  */

/*
 * The order of the ECHO macro is important in those productions that
 * call check_c_identifier(), because that function may choose to
 * suppress echoing (for example, if it sees a VARCHAR_ token).  If the
 * ECHO has happened before that call, the token will have already
 * escaped, and we'll be out of luck.  For other productions it doesn't
 * matter, since the production won't alter the echoing behavior.
 */

#token "[Ee][A-WYZa-wyz_0-9][a-zA-Z_0-9]*" 	<<LA(1) = check_c_identifier(); ECHO;>>
#token "[Ee][Xx][A-DF-Za-df-z_0-9][a-zA-Z_0-9]*" <<LA(1) = check_c_identifier(); ECHO;>>
#token "[Ee][Xx][Ee][ABD-Zabd-z_0-9][a-zA-Z_0-9]*" <<LA(1) = check_c_identifier(); ECHO;>>
#token "[Ee][Xx][Ee][Cc][a-zA-Z_0-9]*" 		<<LA(1) = check_c_identifier(); ECHO;>>

#token EXEC "[Ee][Xx][Ee][Cc][\ \t\n]" << count_embedded_newlines(); >>

#token "[Ss][A-PR-Za-pr-z_0-9][a-zA-Z_0-9]*" <<LA(1) = check_c_identifier(); ECHO;>>
#token "[Ss][Qq][A-KM-Za-km-z_0-9][a-zA-Z_0-9]*" <<LA(1) = check_c_identifier(); ECHO;>>
#token SQLX "[Ss][Qq][Ll]{[XxMm]}[\ \t\n]" << count_embedded_newlines();>>

#token IDENTIFIER "[a-zA-Z_][a-zA-Z_0-9]*"
<<LA (1) = check_c_identifier ();
ECHO;
>>
#token "0[xX][a-fA-F0-9]+{u|U|l|L}"
  <<ECHO;
LA (1) = GENERIC_TOKEN;
>>
#token "0[0-7]+{u|U|l|L}"
  <<ECHO;
LA (1) = GENERIC_TOKEN;
>>
#token "[0-9]+{u|U|l|L}"
  <<ECHO;
LA (1) = GENERIC_TOKEN;
>>
#token "([0-9]+[Ee]{[\+\-]}[0-9]+{[fFlL]})|([0-9]*.[0-9]+{[Ee]{[\+\-]}[0-9]+}{[fFlL]})|([0-9]+.[0-9]*{[Ee]{[\+\-]}[0-9]+}{[fFlL]})"
  <<ECHO;
LA (1) = GENERIC_TOKEN;
>>
#token "'(\\~[]|~[\\'])+'"
  <<ECHO;
LA (1) = GENERIC_TOKEN;
>>
#token "\"(\\~[]|\\\n|~[\\\"])*\""
  <<ECHO;
LA (1) = GENERIC_TOKEN;
>>
#token "\-\>"  <<ECHO; LA(1) = GENERIC_TOKEN;>>
#token "\+\+"  <<ECHO; LA(1) = GENERIC_TOKEN;>>
#token "\-\-"  <<ECHO; LA(1) = GENERIC_TOKEN;>>
#token "=="    <<ECHO; LA(1) = GENERIC_TOKEN;>>
#token "\>="   <<ECHO; LA(1) = GENERIC_TOKEN;>>
#token "\<="   <<ECHO; LA(1) = GENERIC_TOKEN;>>
#token "\!="   <<ECHO; LA(1) = GENERIC_TOKEN;>>
#token "&&"    <<ECHO; LA(1) = GENERIC_TOKEN;>>
#token "\|\|"  <<ECHO; LA(1) = GENERIC_TOKEN;>>
#token "\>\>"  <<ECHO; LA(1) = GENERIC_TOKEN;>>
#token "\<\<"  <<ECHO; LA(1) = GENERIC_TOKEN;>>
#token "\+="   <<ECHO; LA(1) = GENERIC_TOKEN;>>
#token "\-="   <<ECHO; LA(1) = GENERIC_TOKEN;>>
#token "\*="   <<ECHO; LA(1) = GENERIC_TOKEN;>>
#token "/="    <<ECHO; LA(1) = GENERIC_TOKEN;>>
#token "\%="   <<ECHO; LA(1) = GENERIC_TOKEN;>>
#token "\>\>=" <<ECHO; LA(1) = GENERIC_TOKEN;>>
#token "\<\<=" <<ECHO; LA(1) = GENERIC_TOKEN;>>
#token "&="    <<ECHO; LA(1) = GENERIC_TOKEN;>>
#token "^="    <<ECHO; LA(1) = GENERIC_TOKEN;>>
#token "\|="   <<ECHO; LA(1) = GENERIC_TOKEN;>>
#token ELLIPSIS  "..."	<<ECHO;>>
#token COLON     ":"	<<ECHO;>>
#token SEMI      ";"	<<ECHO;>>
#token COMMA     ","	<<ECHO;>>
#token DOT       "."	<<ECHO;>>
#token AMPERSAND "&"	<<ECHO;>>
#token STAR      "\*"	<<ECHO;>>
#token EQUAL     "="	<<ECHO;>>
#token LP        "\("	<<ECHO;>>
#token RP        "\)"	<<ECHO;>>
#token LB        "\["	<<ECHO;>>
#token RB        "\]"	<<ECHO;>>
#token LC        "\{"	<<ECHO;>>
#token RC        "\}"	<<ECHO;>>
#token "~[\n]" <<ECHO; LA(1) = GENERIC_TOKEN;>>
/* C declaration section. */
declare_section_body:(external_declaration) *;

external_declaration:<<LINK * plink;
SYMBOL *psym;
pp_reset_current_type_spec ();
>>(specifiers >[plink] | <<plink = pp_current_type_spec ();
   >>)optional_declarators >[psym] << if (psym)
  {
    pp_add_spec_to_decl (plink, psym);
    /*
     * If we have a VARCHAR declaration that didn't
     * come from a typedef, it has been suppressed
     * (i.e., it hasn't yet been echoed to the output
     * stream because it needs to be rewritten).  Now
     * is the time to echo the rewritten decl out.
     *
     * If the decl came from a typedef, it's already
     * been echoed.  That's fine, because it doesn't
     * need to be rewritten in that case.
     */
    if (IS_PSEUDO_TYPE (plink) && !plink->from_tdef)
      {
	pp_print_decls (psym, true);
	yy_sync_lineno ();
      }
    /*
     * Don't do this until after printing, because
     * it's going to jack with the 'next' links in
     * the symbols, which will cause the printer to
     * repeat declarations.
     */
    pp_add_symbols_to_table (psym);
  }
else if (plink)
  {
    /* pp_print_specs(plink); */
  }
if (plink && !plink->tdef)
  pp_discard_link_chain (plink);
>>end_declarator_list;

optional_declarators >[SYMBOL * psym]:	/* empty */ <<$psym = NULL;
>>|init_declarator_list >[$psym];

end_declarator_list:nS_td SEMI <<
  /*
   * The specifier for this declarator list may have
   * been a VARCHAR (or maybe not); if it was, echoing
   * has been suppressed since we saw that keyword.
   * Either way, we want to turn echoing back on.
   */
  pp_suppress_echo (false);
>>;

specifiers >[LINK * plink]:specifier_ (specifier_) *
  <<$plink = pp_current_type_spec ();
>>;

specifier_:storage_class_specifier | type_specifier | type_qualifier;

storage_class_specifier:TYPEDEF_ << pp_add_storage_class (TYPEDEF_);
>>|EXTERN_ << pp_add_storage_class (EXTERN_);
>>|STATIC_ << pp_add_storage_class (STATIC_);
>>|AUTO_ << pp_add_storage_class (AUTO_);
>>|REGISTER_ << pp_add_storage_class (REGISTER_);
>>;

/* Once an actual type-specifier is seen, it acts as a "trigger" to
 * turn typedef-recognition off while scanning declarators, etc.
 */
type_specifier:nS_ntd base_type_specifier
  | type_adjective | <<STRUCTDEF * sdef;
>>struct_specifier >[sdef] << pp_add_struct_spec (sdef);
>>|enum_specifier << pp_add_type_noun (INT_);
pp_add_type_adj (INT_);
>>;

   base_type_specifier:<<int bit_decl = BIT_;
>>VOID_ << pp_add_type_noun (VOID_);
>>|CHAR_ << pp_add_type_noun (CHAR_);
>>|INT_ << pp_add_type_noun (INT_);
>>|FLOAT_ << pp_add_type_noun (FLOAT_);
>>|DOUBLE_ << pp_add_type_noun (DOUBLE_);
>>|TYPEDEF_NAME << pp_add_typedefed_spec (yylval.p_sym->type);
>>|VARCHAR_ <<			/*
				 * The VARCHAR_ token turns OFF echoing (the magic
				 * happens in check_c_identifier()) because we're
				 * going to have to rewrite the declaration.  This is
				 * slimy, but doing the right thing (parsing
				 * everything and then regurgitating it) is too
				 * expensive because we allow arbitrary initializers,
				 * etc.  Echoing will be re-enabled again when we see
				 * the end of the declarator list following the
				 * current specifier.
				 */
  pp_add_type_noun (VARCHAR_);
>>|BIT_
{
  VARYING << bit_decl = VARBIT_;
>>}

<<				/*
				 * The same things that apply for VARCHAR stuff apply
				 * here, too.
				 */
  pp_add_type_noun (bit_decl);
>>;

type_adjective:SHORT_ << pp_add_type_adj (SHORT_);
>>|LONG_ << pp_add_type_adj (LONG_);
>>|SIGNED_ << pp_add_type_adj (SIGNED_);
>>|UNSIGNED_ << pp_add_type_adj (UNSIGNED_);
>>;

type_qualifier			/* We ignore qualifers, so there are no actions. */
:CONST_ << pp_add_type_adj (CONST_);
>>|VOLATILE_ << pp_add_type_adj (VOLATILE_);
>>;

struct_specifier >[STRUCTDEF * sdef]:<<long su;
STRUCTDEF *sdef2;
$sdef = NULL;
>>
  /*
   * Turn off typedef recognition here.  We have to do it here or
   * we get burned by lookahead buffering.  If we do this *after*
   * eating the STRUCT_ or UNION_ keyword, the lookahead for the
   * optional tag happens before we disable typedef checking, and
   * if the name of the struct tag is the same as some typedef we
   * wind up with a TYPEDEF_NAME rather than an IDENTIFIER, and
   * we lose.  Check out the nS_td in struct_specifier_body, too.
   */
  nS_ntd (STRUCT_ << su = 1; >>|UNION_ << su = 0; >>)(struct_specifier_body[su, NULL] >[$sdef] | struct_tag >[sdef2] (struct_specifier_body[su, sdef2] >[$sdef] |	/*empty */
														      <<$sdef
														      =
														      sdef2;
														      $sdef->
														      by_name
														      =
														      true;
														      >>));

struct_specifier_body[long su,
		      STRUCTDEF * i_sdef] >[STRUCTDEF * sdef]:<<SYMBOL * psym;
$sdef = $i_sdef;
>>
  /*
   * Turn typedef recognition back on, and do it before we can
   * get burned by lookahead.  This probably ought to be combined
   * with the push_spec_scope() stuff, but making everything
   * happen at just the right moment is damned hard to coordinate
   * with the lookahead machinery.
   */
  nS_td LC << pp_push_spec_scope ();
>>struct_declaration_list >[psym] << pp_pop_spec_scope ();
>>RC << if ($i_sdef == NULL)
  {
    $i_sdef = pp_new_structdef (NULL);
    pp_Struct_table->add_symbol (pp_Struct_table, $i_sdef);
  }
if (psym == NULL)
  {
    pp_Struct_table->remove_symbol (pp_Struct_table, $i_sdef);
    pp_discard_structdef ($i_sdef);
  }
else if ($i_sdef->fields)
  {
    varstring gack;
    vs_new (&gack);
    vs_sprintf (&gack, "%s %s", $i_sdef->type_string, $i_sdef->tag);
    yyredef (vs_str (&gack));
    vs_free (&gack);
    pp_discard_symbol_chain (psym);
  }
else
  {
    $i_sdef->type = $su;
    $i_sdef->type_string = $su ? "struct" : "union";
    $i_sdef->fields = psym;
  }

$sdef = $i_sdef;
>>;

struct_tag >[STRUCTDEF * sdef]:<<SYMBOL dummy;
$sdef = NULL;
>>IDENTIFIER << dummy.name = zzlextext;
if (!
    ($sdef =
     (STRUCTDEF *) pp_Struct_table->find_symbol (pp_Struct_table, &dummy)))
  {
    $sdef = pp_new_structdef (zzlextext);
    pp_Struct_table->add_symbol (pp_Struct_table, $sdef);
  }
>>;

struct_declaration_list >[SYMBOL * psym]:<<SYMBOL * last_field, *psym2;
>>struct_declaration >[$psym] << last_field = $psym;
>>
  /*
   * Make this an iterative body instead of a recursive one in
   * order to avoid an antlr attribute stack overflow.  The only
   * trick is making sure that we continue to tack new members
   * onto the tail of the list we're accumulating, so that
   * they're retained in declaration order.  (Actually, I can't
   * think of a reason why the preprocessor would care about
   * declaration order, but what the heck.)
   */
  (struct_declaration >[psym2]
   << if (last_field) last_field->next = psym2; last_field = psym2; >>)
  *;

struct_declaration >[SYMBOL * sym]:<<LINK * plink;
SYMBOL *psym;
$sym = NULL;
>>specifier_qualifier_list >[plink]
  optional_struct_declarator_list >[psym] << if (psym)
  {
    pp_add_spec_to_decl (plink, psym);
    /*
     * Same deal as with an upper-level decl: this
     * may have been a VARCHAR decl that needs to be
     * rewritten.  If so, it has not yet been echoed;
     * do so now.
     */
    if (IS_PSEUDO_TYPE (plink) && !plink->from_tdef)
      {
	pp_print_decls (psym, true);
	yy_sync_lineno ();
      }
    $sym = psym;
  }
else
  $sym = NULL;
if (!plink->tdef)
  pp_discard_link_chain (plink);
>>end_declarator_list;

specifier_qualifier_list >[LINK * plink]:<<pp_reset_current_type_spec ();
pp_disallow_storage_classes ();
>>specifiers >[$plink];

optional_struct_declarator_list >[SYMBOL * psym]:	/* empty */ <<$psym = NULL;
>>|struct_declarator_list >[$psym];

struct_declarator_list >[SYMBOL * psym]:struct_declarator >[$psym]
{
  <<SYMBOL * psym3;
  >>COMMA struct_declarator_list >[psym3] << if ($psym)
    {
      $psym->next = psym3;
    }
  else
    $psym = psym3;
>>}

;

struct_declarator >[SYMBOL * psym]:<<$psym = NULL;
>>(COLON << yy_push_mode (EXPR_mode);
   >>c_expression << $psym = NULL; >>|declarator_ >[$psym]
   {
   COLON << yy_push_mode (EXPR_mode); >>c_expression}

);

enum_specifier:ENUM (LC enumerator_list RC | <<SYMBOL * d1;
      >>identifier >[d1]
      optional_enumerator_list << if (d1->type) yyredef (d1->name);
      else
      pp_discard_symbol (d1); >>)
  ;

optional_enumerator_list:{
LC enumerator_list RC}
 ;

enumerator_list:enumerator (COMMA enumerator) *;

enumerator:<<SYMBOL * d1;
>>identifier >[d1]
{
  EQUAL << yy_push_mode (EXPR_mode);
>>c_expression}

<<pp_do_enum (d1);
>>;

init_declarator_list >[SYMBOL * psym]:<<SYMBOL * psym3;
>>init_declarator >[$psym] << if ($psym)
  $psym->next = NULL;
>>(COMMA init_declarator >[psym3] << if (psym3)
   {
   psym3->next = $psym; $psym = psym3;}

   >>)
  *;

init_declarator >[SYMBOL * psym]:declarator_ >[$psym]
{
  nS_td EQUAL << yy_push_mode (EXPR_mode);
  >>c_expression nS_ntd <<
    /*
     * We don't really care what the initializer
     * is, but in some cases it is important to
     * know that there is one.
     */
    pp_add_initializer ($psym);
>>}

;

declarator_ >[SYMBOL * psym]:<<$psym = NULL;
>>(direct_declarator >[$psym]
   | pointer declarator_ >[$psym]
   << if ($psym) pp_add_declarator ($psym, D_POINTER); >>)
  ;

direct_declarator >[SYMBOL * psym]:<<SYMBOL * args = NULL;
$psym = NULL;
>>(IDENTIFIER			/* optional iff derived via */
   /* parameter_declaration    */
   << $psym = pp_new_symbol (zzlextext, pp_nesting_level);
   >>|LP declarator_ >[$psym] RP) (<<const char *sub;
				   >>c_subscript >[sub] << if ($psym)
				   {
				   pp_add_declarator ($psym, D_ARRAY);
				   $psym->etype->decl.d.num_ele = sub;}

				   >>|parameter_spec >[args] << if ($psym)
				   {
				   pp_add_declarator ($psym, D_FUNCTION);
				   $psym->etype->decl.d.args = args;}

				   >>)
  *;

pointer:STAR (type_qualifier) *;

parameter_spec >[SYMBOL * psym]:<<$psym = NULL;
>>LP
{
push_name_scope (parameter_type_list >[$psym] | identifier_list)
    pop_name_scope}
RP;

push_name_scope:		/* empty */ <<pp_push_name_scope ();
>>;

pop_name_scope:		/* empty */ <<pp_pop_name_scope ();
>>;

push_spec_scope:		/* empty */ <<pp_push_spec_scope ();
>>;

pop_spec_scope:		/* empty */ <<pp_pop_spec_scope ();
>>;

parameter_type_list >[SYMBOL * psym]:push_spec_scope parameter_list >
  [$psym] pop_spec_scope;

parameter_list >[SYMBOL * psym]:<<SYMBOL * tmp;
>>parameter_declaration >[$psym]
{
  trailing_params >[tmp] << $psym->next = tmp;
>>}

;

trailing_params >[SYMBOL * psym]:<<SYMBOL * last, *tmp;
>>COMMA (parameter_declaration >[$psym] << last = $psym;
	 >>(COMMA parameter_declaration >[tmp] << if (last)
	    {
	    last->next = tmp; last = tmp;}

	    >>) * |ELLIPSIS << /* What to do in this case? */ >>
  )
  ;

parameter_declaration >[SYMBOL * psym]:<<LINK * plink;
$psym = NULL;
pp_reset_current_type_spec ();
>>specifiers >[plink]
{
abstract_declarator >[$psym]}

<<if ($psym == NULL)
  $psym = pp_new_symbol (NULL, pp_nesting_level);
pp_add_spec_to_decl (plink, $psym);
if (plink && !plink->tdef)
  pp_discard_link_chain (plink);
>>nS_td;

identifier_list:IDENTIFIER (COMMA IDENTIFIER) *;

abstract_declarator >[SYMBOL * psym]:<<$psym = NULL;
>>pointer
{
abstract_declarator >[$psym]}

<<if ($psym == NULL)
  $psym = pp_new_symbol (NULL, pp_nesting_level);
if ($psym)
  pp_add_declarator ($psym, D_POINTER);
>>|direct_abstract_declarator >[$psym];

direct_abstract_declarator >[SYMBOL * psym]:<<SYMBOL * args = NULL;
const char *sub = NULL;
$psym = NULL;
>>(LP (abstract_declarator >[$psym] |
       {
       parameter_type_list >[args]
       << $psym = pp_new_symbol (NULL, pp_nesting_level);
       pp_add_declarator ($psym, D_FUNCTION);
       $psym->etype->decl.d.args = args;
       >>}

   )RP
   | IDENTIFIER
   << $psym = pp_new_symbol (zzlextext, pp_nesting_level);
   >>|c_subscript >[sub]
   << $psym = pp_new_symbol (NULL, pp_nesting_level);
   pp_add_declarator ($psym, D_ARRAY);
   $psym->etype->decl.d.num_ele = sub;
   >>)(c_subscript >[sub] << pp_add_declarator ($psym, D_ARRAY);
       $psym->etype->decl.d.num_ele = sub;
       >>|LP
       {
       parameter_type_list >[args]} RP
       << pp_add_declarator ($psym, D_FUNCTION);
       $psym->etype->decl.d.args = args;
       >>) *;

/* Name-Space and scanner-feedback productions */

/* The occurence of a type_specifier in the input turns off
 * scanner-recognition of typedef-names as such, so that they can
 * be re-defined within a declarator-list. The switch is called
 * "name_space_types".
 *
 * The call to yysync() assures that the switch gets toggled after
 * the next token is pre-fetched as a lookahead.
 */

nS_ntd:<<pp_make_typedef_names_visible (0);
>>;

/* Once the declarators (if any) are parsed, the scanner is returned
 * to the state where typedef-names are recognized.
 */
nS_td:<<pp_make_typedef_names_visible (1);
>>;

/* The scanner must be aware of the name-space which
 * differentiates typedef-names from identifiers. But the
 * distinction is only useful within limited scopes. In other
 * scopes the distinction may be invalid, or in cases where
 * typedef-names are not legal, the semantic-analysis phase
 * may be able to generate a better error message if the parser
 * does not flag a syntax error. We therefore use the following
 * production...
 */

identifier >[SYMBOL * result]:<<$result = NULL;
>>(nS_ntd TYPEDEF_NAME nS_td
   << $result = pp_new_symbol (zzlextext, pp_nesting_level);
   >>|IDENTIFIER
   <<
   if (!yylval.p_sym ||
       yylval.p_sym->level != pp_nesting_level)
   $result = pp_new_symbol (zzlextext, pp_nesting_level);
   else
   $result = yylval.p_sym;
   >>|ENUMERATION_CONSTANT
   << $result = pp_new_symbol (zzlextext, pp_nesting_level); >>)
  ;

c_subscript >[char *subscript]:<<varstring subscript_buf;
$subscript = NULL;
>>LB << yy_push_mode (EXPR_mode);
if (IS_PSEUDO_TYPE (pp_current_type_spec ()))
  {
    /*
     * There is no need to store the old values of the
     * echo function and echo suppression here because
     * we're going to pop right ought of this mode and
     * clobber them anyway.
     */
    vs_new (&subscript_buf);
    (void) yy_set_buf (&subscript_buf);
    (void) pp_set_echo (&echo_vstr);
    pp_suppress_echo (false);
  }
>>c_subscript_body << if (IS_PSEUDO_TYPE (pp_current_type_spec ()))
  {
    /*
     * Have to clobber the RB here, since the lookahead
     * has already put it in our buffer.  Fortunately,
     * we're in a mode where we can do that.
     */
    yy_erase_last_token ();
    yy_set_buf (NULL);
    $subscript = pp_strdup (vs_str (&subscript_buf));
    vs_free (&subscript_buf);
  }
>>RB << yy_pop_mode ();
>>;


#lexclass EXPR_mode
/*
 * EXPR_mode is to C_mode what HV_mode is to VAR_mode: it is designed to
 * accept nearly arbitrary expressions.  It is used primarily when
 * skipping past initializer expressions for variable declarations, as in
 *
 * 	long x = foo() + bar(z[42]);
 *
 * In this mode, we care little about the actual structure of the
 * expressions, except to note that commas, semicolons, and bracketing
 * constructs are important.  This mode echoes the text it encounters, so
 * it needn't employ the buffering trick that HV_mode must use.
 */
#token "[\ \t]*#(\\~[]|\\\n|~[\\\n])*\n"  <<consume_C_macro();>>
#token "/\*"               <<yy_push_mode(COMMENT_mode); ECHO; zzskip();>>
#token "[\t\ ]+"           <<ECHO; zzskip();>>
#token "[\n]"              <<ECHO; zzline++; zzendcol = 0; zzskip();>>
#token IDENTIFIER "[a-zA-Z_][a-zA-Z_0-9]*"
<<ECHO;
LA (1) = GENERIC_TOKEN;
>>
#token "0[xX][a-fA-F0-9]+{u|U|l|L}"
  <<ECHO;
LA (1) = GENERIC_TOKEN;
>>
#token "0[0-7]+{u|U|l|L}"
  <<ECHO;
LA (1) = GENERIC_TOKEN;
>>
#token "[0-9]+{u|U|l|L}"
  <<ECHO;
LA (1) = GENERIC_TOKEN;
>>
#token "([0-9]+[Ee]{[\+\-]}[0-9]+{[fFlL]})|([0-9]*.[0-9]+{[Ee]{[\+\-]}[0-9]+}{[fFlL]})|([0-9]+.[0-9]*{[Ee]{[\+\-]}[0-9]+}{[fFlL]})"
  <<ECHO;
LA (1) = GENERIC_TOKEN;
>>
#token "'(\\~[]|~[\\'])+'"
  <<ECHO;
LA (1) = GENERIC_TOKEN;
>>
#token "\"(\\~[]|\\\n|~[\\\"])*\""
  <<ECHO;
count_embedded_newlines ();
LA (1) = GENERIC_TOKEN;
>>
#token SEMI  ";"  <<ECHO;>>
#token COMMA ","  <<ECHO;>>
#token LP    "\(" <<ECHO;>>
#token RP    "\)" <<ECHO;>>
#token LB    "\[" <<ECHO;>>
#token RB    "\]" <<ECHO;>>
#token LC    "\{" <<ECHO;>>
#token RC    "\}" <<ECHO;>>
#token GENERIC_TOKEN "~[\n]" <<ECHO;>>
/* C Expression Section */
c_subscript_body:{
c_expr}

;

c_expression:c_expr << yy_pop_mode ();
>>;

c_expr:any (any) *;

any_expr:(any | COMMA | SEMI) *;

any:GENERIC_TOKEN | LP any_expr RP | LB any_expr RB | LC any_expr RC;


#lexclass VAR_mode
/*
 * VAR_mode is used at the "top level" of host variable processing.  In
 * this mode we care about C identifiers and some of the C operators --
 * specifically those that produce lvalues.  Thus, structure
 * dereferencing operators such as '->' and '.' are relevant, as well as
 * subscripts ('[' and ']').  The appearance of brackets kicks us into
 * another mode (HV_mode), where we accept basically anything as long as
 * it has matching pairs of bracketing tokens (parens, braces, or
 * brackets).
 *
 * VAR_mode also uses a one-shot patch to parsing identifier candidates:
 * the first time we encounter a c_id-looking pattern, we unconditionally
 * accept it as an identifier; subsequent c_id-looking patterns are
 * checked to make sure that they aren't SQL/X keywords.  This allows us
 * to declare useful host variables with names like 'descriptor', 'user',
 * etc. and not get jived by the annoying fact that SQL has appropriated
 * most of the good words in the universe as keywords.
 */
#token "[\ \t]*#(\\~[]|\\\n|~[\\\n])*\n"  <<consume_C_macro();>>
#token "/\*"  		   <<yy_push_mode(COMMENT_mode); ECHO; zzskip();>>
#token "[\t\ ]+" 	   <<zzskip();>>
#token "[\n]"    	   <<zzline++; zzendcol = 0; zzskip();>>
#token "\-\-~[\n]*\n" 	   <<zzline++; zzendcol = 0; zzskip();>>
#token IDENTIFIER "[a-zA-Z_][a-zA-Z_%#0-9]*"
<<
{
  long code = IDENTIFIER;
  if (recognize_keywords)
    {
      /* handle INDICATOR, INTO, etc */
      code = check_identifier (&preprocessor_table);
    }
  else
    {
      code = check_c_identifier ();
    }
  /*
   * Postpone the switch to recognizing-csql-keywords if
   * we just saw the INDICATOR keyword, since that means
   * that a C identifier is coming up.
   */
  recognize_keywords = (code != INDICATOR);
  LA (1) = code;
}

>>
#token COMMA     ","    <<recognize_keywords = false;>>
#token DOT       "."    <<recognize_keywords = false;>>
#token COLON_INDICATOR     ":"    <<recognize_keywords = false;>>
#token PTR_OP    "\-\>" <<recognize_keywords = false;>>
#token STAR      "\*"
#token AMPERSAND "&"
#token LP        "\("
#token RP        "\)"
#token LB        "\["
#token RB        "\]"
#token           "\+\+" <<ignore_token(); zzskip();>>
#token           "\-\-" <<ignore_token(); zzskip();>>
#token SEMI      ";"
#token LC        "\{"
#token RC        "\}"
#token GENERIC_TOKEN "~[\n]" <<ECHO; >>
host_variable_list_tail:<<HOST_REF * href1, *href2;
>>host_var_w_opt_indicator >[href1] (COMMA << ECHO_STR (", ", strlen (", "));
				     >>host_var_w_opt_indicator >[href2]) *;

indicator_var >[HOST_VAR * hvar]:<<$hvar = NULL;
>>(
    /* empty */
    |COLON_INDICATOR hostvar >[$hvar] | INDICATOR
    {
    COLON_INDICATOR}

    hostvar >[$hvar]);

host_var_w_opt_indicator >[HOST_REF * href]:<<HOST_VAR * hvar1, *hvar2 = NULL;
varstring *s = yy_get_buf ();
$href = NULL;
yy_push_mode (VAR_mode);
yy_set_buf (NULL);
structs_allowed = true;
>>host_variable >[hvar1] indicator_var >[hvar2] << yy_pop_mode ();
yy_set_buf (s);

$href = pp_add_host_ref (hvar1, hvar2, structs_allowed, &n_refs);
ECHO_STR ("? ", strlen ("? "));
for (i = 1; i < n_refs; i++)
  ECHO_STR (", ? ", strlen (", ? "));
>>;

host_variable >[HOST_VAR * hvar]:<<$hvar = NULL;
>>COLON hostvar >[$hvar];

hostvar >[HOST_VAR * hvar]:<<$hvar = NULL;
>>(host_ref >[$hvar] | <<HOST_VAR * hvar2; >>STAR
   {
   CONST_ | VOLATILE_}

   hostvar >[hvar2]
   << if (($hvar = pp_ptr_deref (hvar2, 0)) == NULL) pp_free_host_var (hvar2);
   else
   vs_prepend (&$hvar->expr, "*");
   >>|<<HOST_VAR * href2; >>AMPERSAND host_ref >[href2]
   << if (($hvar = pp_addr_of (href2)) == NULL) pp_free_host_var (href2);
   else
   vs_prepend (&$hvar->expr, "&"); >>)
  ;

host_ref >[HOST_VAR * href]:<<$href = NULL;
>>(IDENTIFIER << if (yylval.p_sym == NULL)
   {
   yyverror (pp_get_msg (EX_ESQLM_SET, MSG_NOT_DECLARED),
	     zzlextext); $href = NULL;}
   else
   $href = pp_new_host_var (NULL, yylval.p_sym);
   >>host_ref_tail[$href] >[$href] | LP hostvar >[$href] RP << if ($href)
   {
   vs_prepend (&$href->expr, "("); vs_strcat (&$href->expr, ")");}

   >>host_ref_tail[$href] >[$href])
  ;

host_ref_tail[HOST_VAR * i_href] >[HOST_VAR * href]:<<$href = $i_href;
>>(host_var_subscript[$i_href] >[$href]
   | DOT IDENTIFIER
   <<
   if (($href = pp_struct_deref ($i_href, zzlextext, 0)) == NULL)
   pp_free_host_var ($i_href);
   else
   vs_sprintf (&$href->expr, ".%s", zzlextext);
   >>|PTR_OP IDENTIFIER
   <<
   if (($href = pp_struct_deref ($i_href, zzlextext, 1)) == NULL)
   pp_free_host_var ($i_href);
   else
   vs_sprintf (&$href->expr, "->%s", zzlextext); >>)
  *;

host_var_subscript[HOST_VAR * i_href] >[HOST_VAR * href]:<<$href = $i_href;
>>LB << vs_clear (&pp_subscript_buf);
yy_push_mode (HV_mode);
>>host_var_subscript_body[$i_href] >[$href];


#lexclass HV_mode
/*
 * As mentioned above, HV_mode is used for scanning the innards of
 * subscript expressions in host variable references.  In this mode we
 * don't care one bit about the structure of the expression between
 * brackets (it's assumed that it will yield an integer when finally
 * evaluated), except for the constraint (enforced by the parser) that
 * bracketing constructs match.
 *
 * This mode uses a trick:  it keeps accumulating text in yytext until it
 * encounters one of the tokens that the parser cares about in this mode,
 * such as a parenthesis.  It then reports that token; however, at that
 * point yytext contains all of the text that preceded the paren, as well
 * as the paren itself.  The parser can then just echo that text into
 * whatever buffer it is using to prepare the host variable expressions.
 * This saves us having to report any but the truly significant tokens.
 */
#token "[\ \t]*#(\\~[]|\\\n|~[\\\n])*\n"  <<consume_C_macro();>>
#token "/\*"  <<yy_push_mode(COMMENT_mode); ECHO; zzskip();>>
#token "[\n]" <<zzline++; zzendcol = 0; zzmore();>>
#token "'(\\~[]|~[\\'])+'"
<<zzmore ();
>>
#token "\"(\\~[]|\\\n|~[\\\"])*\""
  <<count_embedded_newlines ();
zzmore ();
>>
#token LP "\("
#token RP "\)"
#token LB "\["
#token RB "\]"
#token LC "\{"
#token RC "\}"
#token    "~[/\#\n'\"\(\)\[\]\{\}]+"  <<zzmore();>>
host_var_subscript_body[HOST_VAR * i_href] >[HOST_VAR * href]:
<<$href = $i_href;
>>(hv_sub) * RB << yy_pop_mode ();
if (($href = pp_ptr_deref ($i_href, 1)) == NULL)
  pp_free_host_var ($i_href);
else
  vs_sprintf (&$href->expr, "[%s%s", vs_str (&pp_subscript_buf), zzlextext);
>>;

hv_sub:LP << vs_strcat (&pp_subscript_buf, zzlextext);
>>(hv_sub) * RP << vs_strcat (&pp_subscript_buf, zzlextext);
>>|LB << vs_strcat (&pp_subscript_buf, zzlextext);
>>(hv_sub) * RB << vs_strcat (&pp_subscript_buf, zzlextext);
>>|LC << vs_strcat (&pp_subscript_buf, zzlextext);
>>(hv_sub) * RC << vs_strcat (&pp_subscript_buf, zzlextext);
>>;



#lexclass BUFFER_mode
/*
 *  In BUFFER_mode, we accumulate the tokens of an ordinary statement into
 *  pt_statement_buf for processing by parser_parse_string.  We scan almost
 *  everything as a GENERIC_TOKEN.  When we encounter the start of a
 *  host variable list or descriptor, we enter VAR_mode.
 *
 *  Synchronizing the scanners means that any 2 scanners that will
 *  receive control in the scanning of a valid token sequence must
 *  assign the same token codes for all transition token strings.
 *  For example, a ";" is a transition token between BUFFER_mode and
 *  SQLX_mode.  A ";" must be scanned as a SEMI in BUFFER_mode and in
 *  SQLX_mode, otherwise ANTLR will find bogus lexical errors at
 *  the ends of all ordinary statements.
 */
/* '#' is a legal csql identifier character, for some odd reason.
 * hence no support for c preprocessor inside exec sql
 */
#token "/\*"  <<yy_push_mode(COMMENT_mode); ECHO; zzskip();>>

#token /*IDENTIFIER*/ "([a-zA-Z_]|(\0xa1[\0xa2-\0xfe])|([\0xa2-\0xfe][\0xa1-\0xfe])|(\0x8e[\0xa1-\0xfe]))([a-zA-Z_%#0-9]|(\0xa1[\0xa2-\0xfe])|([\0xa2-\0xfe][\0xa1-\0xfe])|(\0x8e[\0xa1-\0xfe]))*"
<<
{
  long code = IDENTIFIER;
  if (intl_mbs_casecmp (zzlextext, "DESCRIPTOR") == 0)
    {
      code = DESCRIPTOR;
    }
  else if (intl_mbs_casecmp (zzlextext, "INTO") == 0)
    {
      code = INTO;
    }
  else if (intl_mbs_casecmp (zzlextext, "TO") == 0)
    {
      code = INTO;
    }
  else if (intl_mbs_casecmp (zzlextext, "VALUES") == 0)
    {
      code = VALUES;
    }
  else if (intl_mbs_casecmp (zzlextext, "SELECT") == 0)
    {
      code = SELECT;
    }
  LA (1) = code;
}

>>
#token COLON ":"
#token SEMI ";"
#token "( ('(('')|(\\\n)|~['\n])*') | (\"((\\~[\n])|(\\\n)|~[\"\\\n])*\") )"
  <<count_embedded_newlines ();
echo_string_constant (zzlextext, zzendexpr - zzbegexpr + 1);
zzskip ();
>>
#token "[\ \t]+"      <<ECHO_SP; zzskip();>>
#token "[\n]"         <<zzline++; zzendcol = 0; ECHO_SP; zzskip();>>
#token "\-\-~[\n]*\n" <<zzline++; zzendcol = 0; ECHO_SP; zzskip();>>
#token  "~[\n]"       <<ECHO; zzskip();>>
csql_statement_tail <[long cursor_kind]:<<PT_NODE ** pt, *cursr;
char *statement, *text;
PT_HOST_VARS *result;
long is_upd_obj;
HOST_LOD *inputs, *outputs;
int length;
PARSER_VARCHAR *varPtr;
>>(buffer_sql) * <<yy_pop_mode ();
>><<statement = vs_str (&pt_statement_buf);
length = (int) vs_strlen (&pt_statement_buf);
pt = parser_parse_string (parser, statement);
if (!pt || !pt[0])
  pt_print_errors (parser);
else
  {
    is_upd_obj = pt_is_update_object (pt[0]);
    result = pt_host_info (parser, pt[0]);
    inputs = pp_input_refs ();
    outputs = pp_output_refs ();
    text = statement;
    if (is_upd_obj)
      {
	if (HOST_N_REFS (inputs) > 0
	    && pp_check_type (&(HOST_REFS (inputs))[0],
			      NEWSET (C_TYPE_OBJECTID),
			      pp_get_msg (EX_ESQLM_SET,
					  MSG_PTR_TO_DB_OBJECT)))
	  esql_Translate_table.tr_object_update (text,
						 length,
						 repeat_option,
						 HOST_N_REFS (inputs),
						 HOST_REFS (inputs));
      }
    else if ((cursr = pt_get_cursor (result)) != NULL)
      {
	CURSOR *cur;
	const char *c_nam;
	c_nam = pt_get_name (cursr);
	if ((cur = pp_lookup_cursor ((char *) c_nam)) == NULL)
	  yyverror (pp_get_msg (EX_ESQLM_SET, MSG_CURSOR_UNDEFINED), c_nam);
	else
	  {
	    switch (cursor_kind)
	      {
	      case CURSOR_DELETE:
		esql_Translate_table.tr_delete_cs (cur->cid);
		break;
	      case CURSOR_UPDATE:
		pt_set_update_object (parser, pt[0]);
		varPtr = pt_print_bytes (parser, pt[0]);
		esql_Translate_table.tr_update_cs (cur->cid,
						   (char *)
						   pt_get_varchar_bytes
						   (varPtr),
						   (int)
						   pt_get_varchar_length
						   (varPtr), repeat_option,
						   HOST_N_REFS (inputs),
						   HOST_REFS (inputs));
		break;
	      default:
		break;
	      }
	  }
      }
    else
      {
	repeat_option =
	  (pt[0]->node_type == PT_INSERT
	   && pt[0]->info.insert.is_value != PT_IS_SUBQUERY);
	esql_Translate_table.tr_static (text,
					length,
					repeat_option,
					HOST_N_REFS (inputs),
					HOST_REFS (inputs),
					HOST_DESC (inputs),
					HOST_N_REFS (outputs),
					HOST_REFS (outputs),
					HOST_DESC (outputs));
      }
    pt_free_host_info (result);
  }

yy_sync_lineno ();
yy_set_buf (NULL);
>>;

buffer_sql:<<HOST_REF * href;
>>host_var_w_opt_indicator >[href]
  | DESCRIPTOR host_variable_wo_indicator >[href]
  << if (pp_check_type (href,
			NEWSET (C_TYPE_SQLDA),
			pp_get_msg (EX_ESQLM_SET, MSG_PTR_TO_DESCR)))
  (void) pp_switch_to_descriptor ();
>>|var_mode_follow_set;

var_mode_follow_set "any sql token that can terminate a host variable except ';'":
<<				/* The action for this token must come first
				 * because the lookahead will change the token right after the
				 * match. The lookahead also prevents this from being
				 * entered when there is no match.
				 */
  ECHO;
>>(STAR
   | COMMA
   | LP
   | RP
   | LB
   | RB
   | LC
   | RC
   | GENERIC_TOKEN | IDENTIFIER | (INTO | TO) << pp_gather_output_refs ();
   >>|(FROM | SELECT | VALUES) <<
   /*
    * We need the VALUES and SELECT keywords here to
    * kick the thing back into input mode after
    * mistakenly getting kicked into output mode by the
    * INTO of an INSERT statement.
    */
   pp_gather_input_refs ();
   >>|ON_ | WITH);


cursor_query_statement >[char *text, int length]:<<PT_NODE ** pt;
char *statement;
$text = NULL;
vs_clear (&pt_statement_buf);
yy_set_buf (&pt_statement_buf);
ECHO;
yy_push_mode (BUFFER_mode);
>>(SELECT | LP) (buffer_sql) * <<yy_pop_mode ();
statement = vs_str (&pt_statement_buf);
$length = vs_strlen (&pt_statement_buf);
pt = parser_parse_string (parser, statement);
if (!pt || !pt[0])
  pt_print_errors (parser);
else
  $text = statement;
yy_sync_lineno ();
yy_set_buf (NULL);
>>;



#lexclass COMMENT_mode		/* C-style comments */
#token "[\n]"        <<zzline++; zzendcol = 0; ECHO; zzskip();>>
#token "[\*]+/"      <<yy_pop_mode(); ECHO; zzskip();>>
#token "[\*]+\n"     <<zzline++; zzendcol = 0; ECHO; zzskip();>>
#token "[\*]+~[/\n]" <<ECHO; zzskip();>>
#token "~[\n\*]+"    <<ECHO; zzskip();>>
