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
 * esql_grammar.y : esql grammar file
 */



%{

#ifndef ESQL_GRAMMAR
#define ESQL_GRAMMAR

#define START 		0
#define CSQL_mode	1
#define C_mode		2
#define EXPR_mode	3
#define VAR_mode	4
#define HV_mode		5
#define BUFFER_mode	6
#define COMMENT_mode	7

/* #define PARSER_DEBUG */


#ifdef PARSER_DEBUG

#define DBG_PRINT printf("rule: %d\n", __LINE__);
#define PARSER_NEW_NODE(a, b) __make_node(a, b, __LINE__)
#define PARSER_FREE_NODE(a, b) parser_free_node(a, b)
#define PRINT_(a) printf(a)
#define PRINT_1(a, b) printf(a, b)

#else

#define DBG_PRINT
#define PARSER_NEW_NODE(a, b) parser_new_node(a, b)
#define PARSER_FREE_NODE(a, b) parser_free_node(a, b)
#define PRINT_(a)
#define PRINT_1(a, b)

#endif

#if defined(WINDOWS)
#define inline
#endif

extern char *esql_yytext;
extern int esql_yylineno;

#include "config.h"
#define ECHO_mode START
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>

#if defined(WINDOWS)
#include <io.h>
#endif

#ifdef HAVE_GETOPT_H
#include <getopt.h>
#else
#include "getopt.h"
#endif

#include "language_support.h"
#include "message_catalog.h"
#include "variable_string.h"
#include "parser.h"
#include "esql_translate.h"
#include "memory_alloc.h"
#include "misc_string.h"
#include "esql_misc.h"
#include "environment_variable.h"
#include "utility.h"
#include "esql_scanner_support.h"
#define ESQLMSG(x) pp_get_msg(EX_ESQLM_SET, (x))

#define NO_CURSOR          0
#define CURSOR_DELETE      1
#define CURSOR_UPDATE      2

#ifndef __DEF_ESQL__
#define __DEF_ESQL__
enum
{
  MSG_DONT_KNOW = 1,
  MSG_CHECK_CORRECTNESS,
  MSG_USING_NOT_PERMITTED,
  MSG_CURSOR_UNDEFINED,
  MSG_PTR_TO_DESCR,
  MSG_PTR_TO_DB_OBJECT,
  MSG_CHAR_STRING,
  MSG_INDICATOR_NOT_ALLOWED,
  MSG_NOT_DECLARED,
  MSG_MUST_BE_SHORT,
  MSG_INCOMPLETE_DEF,
  MSG_NOT_VALID,
  MSG_TYPE_NOT_ACCEPTABLE,
  MSG_UNKNOWN_HV_TYPE,
  MSG_BAD_ADDRESS,
  MSG_DEREF_NOT_ALLOWED,
  MSG_NOT_POINTER,
  MSG_NOT_POINTER_TO_STRUCT,
  MSG_NOT_STRUCT,
  MSG_NO_FIELD,

  ESQL_MSG_STD_ERR = 52,
  ESQL_MSG_LEX_ERROR = 53,
  ESQL_MSG_SYNTAX_ERR1 = 54,
  ESQL_MSG_SYNTAX_ERR2 = 55
};
#endif

static SYMBOL *g_identifier_symbol;
static STRUCTDEF *g_sdef2;
static int g_su;
static SYMBOL *g_psym;
static SYMBOL *g_struct_declaration_sym;
static SYMBOL *g_last;
static HOST_VAR *g_href;
static int g_cursor_type;

static bool repeat_option;
static PTR_VEC id_list;
static HOST_LOD *input_refs, *output_refs;
static bool already_translated;
static bool structs_allowed;
static bool hvs_allowed;
static int i;
static int n_refs;

static void reset_globals (void);
static void ignore_repeat (void);
static char *ofile_name (char *fname);
#if defined (ENABLE_UNUSED_FUNCTION)
static int validate_input_file (void *fname, FILE * outfp);
#endif
void pt_print_errors (PARSER_CONTEXT * parser_in);
enum scanner_mode esql_yy_mode (void);

char *mm_malloc (int size);
char *mm_strdup (char *str);
void mm_free (void);

extern PARSER_CONTEXT *parser;

varstring *esql_yy_get_buf (void);
void esql_yy_set_buf (varstring * vstr);


extern char g_delay[];
extern bool g_indicator;
extern varstring *g_varstring;
extern varstring *g_subscript;
extern bool need_line_directive;

#endif

%}

%glr-parser
%error_verbose

%union {
  SYMBOL *p_sym;
  void *ptr;
  int number;
}

%token<ptr> ADD
%token<ptr> ALL
%token<ptr> ALTER
%token<ptr> AND
%token<ptr> AS
%token<ptr> ASC
%token<ptr> ATTRIBUTE
%token<ptr> ATTACH
%token<ptr> AUTO_
%token<ptr> AVG
%token<ptr> BAD_TOKEN
%token<ptr> BOGUS_ECHO
%token<ptr> BEGIN_
%token<ptr> BETWEEN
%token<ptr> BIT_
%token<ptr> BY
%token<ptr> CALL_
%token<ptr> CHANGE
%token<ptr> CHAR_
%token<ptr> CHARACTER
%token<ptr> CHECK_
%token<ptr> CLASS
%token<ptr> CLOSE
%token<ptr> COMMIT
%token<ptr> CONNECT
%token<ptr> CONST_
%token<ptr> CONTINUE_
%token<ptr> COUNT
%token<ptr> CREATE
%token<ptr> CURRENT
%token<ptr> CURSOR_
%token<ptr> DATE_
%token<ptr> DATE_LIT
%token<ptr> DECLARE
%token<ptr> DEFAULT
%token<ptr> DELETE_
%token<ptr> DESC
%token<ptr> DESCRIBE
%token<ptr> DESCRIPTOR
%token<ptr> DIFFERENCE_
%token<ptr> DISCONNECT
%token<ptr> DISTINCT
%token<ptr> DOUBLE_
%token<ptr> DROP
%token<ptr> ELLIPSIS
%token<ptr> END
%token<ptr> ENUM
%token<ptr> EQ
%token<ptr> ESCAPE
%token<ptr> EVALUATE
%token<ptr> EXCEPT
%token<ptr> EXCLUDE
%token<ptr> EXECUTE
%token<ptr> EXISTS
%token<ptr> EXTERN_
%token<ptr> FETCH
%token<ptr> FILE_
%token<ptr> FILE_PATH_LIT
%token<ptr> FLOAT_
%token<ptr> FOR
%token<ptr> FOUND
%token<ptr> FROM
%token<ptr> FUNCTION_
%token<ptr> GENERIC_TOKEN
%token<ptr> GEQ
%token<ptr> GET
%token<ptr> GO
%token<ptr> GOTO_
%token<ptr> GRANT
%token<ptr> GROUP_
%token<ptr> GT
%token<ptr> HAVING
%token<ptr> IDENTIFIED
%token<ptr> IMMEDIATE
%token<ptr> IN_
%token<ptr> INCLUDE
%token<ptr> INDEX
%token<ptr> INDICATOR
%token<ptr> INHERIT
%token<ptr> INSERT
%token<ptr> INTEGER
%token<ptr> INTERSECTION
%token<ptr> INTO
%token<ptr> INT_
%token<ptr> INT_LIT
%token<ptr> IS
%token<ptr> LDBVCLASS
%token<ptr> LEQ
%token<ptr> LIKE
%token<ptr> LONG_
%token<ptr> LT
%token<ptr> MAX_
%token<ptr> METHOD
%token<ptr> MIN_
%token<ptr> MINUS
%token<ptr> MONEY_LIT
%token<ptr> MULTISET_OF
%token<ptr> NEQ
%token<ptr> NOT
%token<ptr> NULL_
%token<ptr> NUMERIC
%token<ptr> OBJECT
%token<ptr> OF
%token<ptr> OID_
%token<ptr> ON_
%token<ptr> ONLY
%token<ptr> OPEN
%token<ptr> OPTION
%token<ptr> OR
%token<ptr> ORDER
%token<ptr> PLUS
%token<ptr> PRECISION
%token<ptr> PREPARE
%token<ptr> PRIVILEGES
%token<ptr> PTR_OP
%token<ptr> PUBLIC_
%token<ptr> READ
%token<ptr> READ_ONLY
%token<ptr> REAL
%token<ptr> REAL_LIT
%token<ptr> REGISTER_
%token<ptr> RENAME
%token<ptr> REPEATED
%token<ptr> REVOKE
%token<ptr> ROLLBACK
%token<ptr> SECTION
%token<ptr> SELECT
%token<ptr> SEQUENCE_OF
%token<ptr> SET
%token<ptr> SETEQ
%token<ptr> SETNEQ
%token<ptr> SET_OF
%token<ptr> SHARED
%token<ptr> SHORT_
%token<ptr> SIGNED_
%token<ptr> SLASH
%token<ptr> SMALLINT
%token<ptr> SOME
%token<ptr> SQLCA
%token<ptr> SQLDA
%token<ptr> SQLERROR_
%token<ptr> SQLWARNING_
%token<ptr> STATIC_
%token<ptr> STATISTICS
%token<ptr> STOP_
%token<ptr> STRING
%token<ptr> STRUCT_
%token<ptr> SUBCLASS
%token<ptr> SUBSET
%token<ptr> SUBSETEQ
%token<ptr> SUM
%token<ptr> SUPERCLASS
%token<ptr> SUPERSET
%token<ptr> SUPERSETEQ
%token<ptr> TABLE
%token<ptr> TIME
%token<ptr> TIMESTAMP
%token<ptr> TIME_LIT
%token<ptr> TO
%token<ptr> TRIGGER
%token<ptr> TYPEDEF_
%token<ptr> UNION_
%token<ptr> UNIQUE
%token<ptr> UNSIGNED_
%token<ptr> UPDATE
%token<ptr> USE
%token<ptr> USER
%token<ptr> USING
%token<ptr> UTIME
%token<ptr> VALUES
%token<ptr> VARBIT_
%token<ptr> VARCHAR_
%token<ptr> VARYING
%token<ptr> VCLASS
%token<ptr> VIEW
%token<ptr> VOID_
%token<ptr> VOLATILE_
%token<ptr> WHENEVER
%token<ptr> WHERE
%token<ptr> WITH
%token<ptr> WORK

%token<ptr> ENUMERATION_CONSTANT
%token<ptr> TYPEDEF_NAME
%token<ptr> STRING_LIT
%token<ptr> IDENTIFIER

%token<ptr> EXEC
%token<ptr> SQLX
%token<ptr> UNION
%token<ptr> STRUCT

%type<number> of_whenever_state
%type<number> opt_for_read_only
%type<number> csql_statement_head
%type<number> opt_varying
%type<number> of_struct_union

%type<ptr> opt_id_by_quasi
%type<ptr> opt_with_quasi
%type<ptr> dynamic_statement
%type<ptr> quasi_string_const
%type<ptr> host_variable_wo_indicator
%type<ptr> id_list_
%type<ptr> opt_specifier_list
%type<ptr> specifier_list
%type<ptr> optional_declarators
%type<ptr> struct_specifier
%type<ptr> opt_struct_specifier_body
%type<ptr> struct_specifier_body
%type<ptr> specifier_qualifier_list
%type<ptr> optional_struct_declarator_list
%type<ptr> struct_declarator_list
%type<ptr> struct_declarator
%type<ptr> init_declarator_list
%type<ptr> init_declarator
%type<ptr> declarator_
%type<ptr> direct_declarator
%type<ptr> param_spec
%type<ptr> param_type_list
%type<ptr> parameter_list
%type<ptr> trailing_params
%type<ptr> param_decl_tail_list
%type<ptr> parameter_declaration
%type<ptr> opt_abstract_declarator
%type<ptr> abstract_declarator
%type<ptr> direct_abstract_declarator
%type<ptr> opt_param_type_list
%type<ptr> identifier
%type<ptr> c_subscript
%type<ptr> indicator_var
%type<ptr> host_var_w_opt_indicator
%type<ptr> hostvar
%type<ptr> host_ref
%type<ptr> opt_host_ref_tail
%type<ptr> host_ref_tail
%type<ptr> host_var_subscript
%type<ptr> host_var_subscript_body
%type<ptr> cursor_query_statement
%type<ptr> cursor
%type<ptr> descriptor_name
%type<ptr> struct_tag
%type<ptr> struct_declaration_list
%type<ptr> struct_declaration
%type<ptr> host_variable

%%

// start
translation_unit
	: 
	  opt_embedded_chunk_list	
		{{
			
			pp_finish ();
			vs_free (&pt_statement_buf);
						/* verify push/pop modes were matched. */
			esql_yy_check_mode ();
			mm_free();
		DBG_PRINT}}
	;

opt_embedded_chunk_list
	: // empty
		{DBG_PRINT}
	| embedded_chunk_list
		{DBG_PRINT}
	;

embedded_chunk_list
	: embedded_chunk_list embedded_chunk
		{DBG_PRINT}
	| embedded_chunk
		{DBG_PRINT}
	;

embedded_chunk
	: chunk_header
		{DBG_PRINT}
	  chunk_body 
		{DBG_PRINT}
	| '{' 
		{{
			
			pp_push_name_scope ();
			
		DBG_PRINT}} 
	  opt_embedded_chunk_list 
		{{
			
			pp_pop_name_scope ();
			
		DBG_PRINT}} 
	  '}'
		{DBG_PRINT}
	;

chunk_header
	: exec sql
		{DBG_PRINT}
	;

chunk_body
	: declare_section ';'
		{{
			
			esql_yy_enter (ECHO_mode);
			pp_suppress_echo (false);
			
		DBG_PRINT}}
	| esql_statement ';'
		{{
			
			esql_yy_enter (ECHO_mode);
			pp_suppress_echo (false);
			
		DBG_PRINT}}
	;

exec
	: EXEC
		{{
			
			esql_yy_enter (CSQL_mode);
			reset_globals ();
			
		DBG_PRINT}}
	;

sql
	: SQLX
		{DBG_PRINT}
	;

// sqlx mode
declare_section
	: BEGIN_ DECLARE SECTION ';' 
		{{
			
			esql_yy_enter (C_mode);
			pp_suppress_echo (false);
			
		DBG_PRINT}}
	  declare_section_body 
		{DBG_PRINT}
	  exec sql END DECLARE SECTION
		{DBG_PRINT}
	;

esql_statement
	: opt_repeated embedded_statement
		{{
			
			ignore_repeat ();
			esql_yy_sync_lineno ();
			esql_yy_set_buf (NULL);
			
		DBG_PRINT}}
	| opt_repeated declare_cursor_statement
		{{
			
			ignore_repeat ();
			
		DBG_PRINT}}
	| opt_repeated 
		{{
			
			vs_clear (&pt_statement_buf);
			esql_yy_set_buf (&pt_statement_buf);
			
		DBG_PRINT}}
	  csql_statement
		{DBG_PRINT}
	;
	
opt_repeated
	: // empty
		{{
			
			repeat_option = false;
			
		DBG_PRINT}}
	| REPEATED
		{{
			
			repeat_option = true;
			
		DBG_PRINT}}
	;

embedded_statement
	: whenever_statement
		{DBG_PRINT}
 	| connect_statement
		{DBG_PRINT}
 	| disconnect_statement
		{DBG_PRINT}
	| open_statement
		{DBG_PRINT}
	| close_statement
		{DBG_PRINT}
	| describe_statement
		{DBG_PRINT}
	| prepare_statement 
		{DBG_PRINT}
	| execute_statement 
		{DBG_PRINT}
	| fetch_statement 
		{DBG_PRINT}
	| INCLUDE SQLCA
		{DBG_PRINT}
	| INCLUDE SQLDA
		{DBG_PRINT}
	| commit_statement 
		{DBG_PRINT}
	| rollback_statement
		{DBG_PRINT}
	;

whenever_statement
	: WHENEVER whenever_action
		{DBG_PRINT}
	;

of_whenever_state
	: SQLWARNING_
		{{
			
			$$ = SQLWARNING;
			
		DBG_PRINT}}
	| SQLERROR_
		{{
			
			$$ = SQLERROR;
			
		DBG_PRINT}}
	| NOT FOUND
		{{
			
			$$ = NOT_FOUND;
			
		DBG_PRINT}}
	;

whenever_action
	: of_whenever_state CONTINUE_
		{{
			
			pp_add_whenever_to_scope ($1, CONTINUE, NULL);
			
		DBG_PRINT}}
	| of_whenever_state STOP_
		{{
			
			pp_add_whenever_to_scope ($1, STOP, NULL);
			
		DBG_PRINT}}
	| of_whenever_state GOTO_ opt_COLON IDENTIFIER
		{{
			
			pp_add_whenever_to_scope ($1, GOTO, $4);
			
		DBG_PRINT}}
	| of_whenever_state GO TO opt_COLON IDENTIFIER
		{{
			
			pp_add_whenever_to_scope ($1, GOTO, $5);
			
		DBG_PRINT}}
	| of_whenever_state CALL_ IDENTIFIER
		{{
			
			pp_add_whenever_to_scope ($1, CALL, $3);
			
		DBG_PRINT}}
	;

opt_COLON
	: // empty
		{DBG_PRINT}
	| ':'
		{DBG_PRINT}
	;

connect_statement
	: CONNECT quasi_string_const opt_id_by_quasi
		{{
			
			HOST_LOD *hvars;
			HOST_REF *href = $2;
			
			if ($3)
			  href = $3;
			
			hvars = pp_input_refs ();
			esql_Translate_table.tr_connect (CHECK_HOST_REF (hvars, 0),
							 CHECK_HOST_REF (hvars, 1),
							 CHECK_HOST_REF (hvars, 2));
			already_translated = true;
			
		DBG_PRINT}}
	;

opt_id_by_quasi
	: // empty
		{{
			
			$$ = NULL;
			
		DBG_PRINT}}
	| IDENTIFIED BY quasi_string_const opt_with_quasi
		{{
			
			if ($4)
			  {
			    $$ = $4;
			  }
			else
			  {
			    $$ = $3;
			  }
			
		DBG_PRINT}}
	;

opt_with_quasi
	: // empty
		{{
			
			$$ = NULL;
			
		DBG_PRINT}}
	| WITH quasi_string_const
		{{
			
			
			$$ = $2;
			
		DBG_PRINT}}
	;

disconnect_statement
	: DISCONNECT 
		{{
			
			esql_Translate_table.tr_disconnect ();
			already_translated = true;
			
		DBG_PRINT}}
	;

declare_cursor_statement
	: DECLARE IDENTIFIER CURSOR_ FOR cursor_query_statement 
		{{
			
			char **cp = $5;
			char *c_nam = strdup($2);
			pp_new_cursor (c_nam, cp[0], (int) (long) cp[1], NULL,
				       pp_detach_host_refs ());
			free_and_init(c_nam);
			
		DBG_PRINT}}
	| DECLARE IDENTIFIER CURSOR_ FOR dynamic_statement 
		{{
			
			char *c_nam = strdup($2);
			pp_new_cursor (c_nam, NULL, 0, (STMT *) $5, pp_detach_host_refs ());
			free_and_init(c_nam);
			esql_yy_sync_lineno ();
			esql_yy_set_buf (NULL);
			
		DBG_PRINT}}
	;


dynamic_statement 
	: IDENTIFIER 
		{{
			
			$$ = pp_new_stmt ($1);
			
		DBG_PRINT}}
	;


open_statement
	: OPEN cursor opt_for_read_only using_part 
		{{
			
			CURSOR *cur = $2;
			int rdonly = $3;
			HOST_LOD *usg;
			
			if (cur->static_stmt)
			  {
			    if (pp_input_refs ())
			      esql_yyverror (pp_get_msg (EX_ESQLM_SET, MSG_USING_NOT_PERMITTED),
					     cur->name);
			    else
			      esql_Translate_table.tr_open_cs (cur->cid,
							       (const char *) cur->static_stmt,
							       cur->stmtLength,
							       -1,
							       rdonly,
							       HOST_N_REFS (cur->host_refs),
							       HOST_REFS (cur->host_refs),
							       (const char *) HOST_DESC (cur->
											 host_refs));
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
							     (const char *) HOST_DESC (usg));
			  }
			else
			  {
			    esql_yyverror (pp_get_msg (EX_ESQLM_SET, MSG_CURSOR_UNDEFINED),
					   cur->name);
			  }
			
			
			already_translated = true;
			
		DBG_PRINT}}
	;

opt_for_read_only
	: // empty
		{{
			
			$$ = 0;
			
		DBG_PRINT}}
	| FOR READ ONLY
		{{
			
			$$ = 1;
			
		DBG_PRINT}}
	;

using_part
	: // empty
		{DBG_PRINT}
	| USING host_variable_lod
		{DBG_PRINT}
	;

descriptor_name
	: host_variable_wo_indicator 
		{{
			
			HOST_REF *href = $1;
			char *nam = NULL;
			if (pp_check_type (href,
					   NEWSET (C_TYPE_SQLDA),
					   pp_get_msg (EX_ESQLM_SET, MSG_PTR_TO_DESCR)))
			  nam = pp_switch_to_descriptor ();
			
			$$ = nam;
			
		DBG_PRINT}}
	;

close_statement
	: CLOSE cursor
		{{
			
			CURSOR *cur = $2;
			if (cur)
			  esql_Translate_table.tr_close_cs (cur->cid);
			already_translated = true;
			
		DBG_PRINT}}
	;

cursor
	: IDENTIFIER 
		{{
			
			CURSOR *cur = NULL;
			if ((cur = pp_lookup_cursor ($1)) == NULL)
			  esql_yyverror (pp_get_msg (EX_ESQLM_SET, MSG_CURSOR_UNDEFINED), $1);
			
			$$ = cur;
			
		DBG_PRINT}}
	;

describe_statement
	: DESCRIBE dynamic_statement INTO descriptor_name 
		{{
			
			STMT *d_statement = $2;
			char *nam = $4;
			
			if (d_statement && nam)
			  esql_Translate_table.tr_describe (d_statement->sid, nam);
			already_translated = true;
			
		DBG_PRINT}}
	| DESCRIBE OBJECT host_variable_wo_indicator ON_ id_list_ INTO descriptor_name 
		{{
			
			HOST_REF *href = $3;
			PTR_VEC *pvec = $5;
			char *nam = $7;
			
			pp_copy_host_refs ();
			
			if (href && pvec && nam &&
			    pp_check_type (href,
					   NEWSET (C_TYPE_OBJECTID),
					   pp_get_msg (EX_ESQLM_SET, MSG_PTR_TO_DB_OBJECT)))
			  {
			    esql_Translate_table.tr_object_describe (href,
								     pp_ptr_vec_n_elems (pvec),
								     (const char **)
								     pp_ptr_vec_elems (pvec), nam);
			  }
			
			pp_free_ptr_vec (pvec);
			already_translated = true;
			
		DBG_PRINT}}
	;

prepare_statement
	: PREPARE dynamic_statement FROM quasi_string_const 
		{{
			
			HOST_REF *href = $4;
			STMT *d_statement = $2;
			
			if (d_statement && href)
			  esql_Translate_table.tr_prepare_esql (d_statement->sid, href);
			
			already_translated = true;
			
		DBG_PRINT}}
	;

into_result
	: of_into_to 
		{{
			
			pp_gather_output_refs ();
			esql_yy_erase_last_token ();
			esql_yy_set_buf (NULL);
			
		DBG_PRINT}}
	  host_variable_lod 
		{{
			
			pp_gather_input_refs ();
			
		DBG_PRINT}}
	;

of_into_to
	: INTO
	| TO
	;

host_variable_lod		
	: host_variable_list 
		{DBG_PRINT}
	| DESCRIPTOR descriptor_name 
		{DBG_PRINT}
	;

quasi_string_const 
	: STRING_LIT 
		{{
			
			$$ = pp_add_host_str ($1);
			
		DBG_PRINT}}
	| host_variable_wo_indicator 
		{{
			
			HOST_REF *href = $1;
			$$ = pp_check_type (href,
					    NEWSET (C_TYPE_CHAR_ARRAY) |
					    NEWSET (C_TYPE_CHAR_POINTER) |
					    NEWSET (C_TYPE_STRING_CONST) |
					    NEWSET (C_TYPE_VARCHAR) |
					    NEWSET (C_TYPE_NCHAR) |
					    NEWSET (C_TYPE_VARNCHAR),
					    pp_get_msg (EX_ESQLM_SET, MSG_CHAR_STRING));
			
		DBG_PRINT}}
	;

host_variable_list
	: 
		{{
			
			esql_yy_erase_last_token ();
			structs_allowed = true;
			
		DBG_PRINT}}
	  host_variable_list_tail 
		{{
			
			pp_check_host_var_list ();
			ECHO;
			
		DBG_PRINT}}
	;

host_variable_wo_indicator 
	:
		{{
			
			esql_yy_erase_last_token ();
			
		DBG_PRINT}}
	  host_var_w_opt_indicator
		{{
			
			HOST_REF *href2;
			
			href2 = $2;
			if (href2 && href2->ind)
			  {
			    esql_yyverror (pp_get_msg (EX_ESQLM_SET, MSG_INDICATOR_NOT_ALLOWED),
					   pp_get_expr (href2));
			    pp_free_host_var (href2->ind);
			    href2->ind = NULL;
			  }
			
			$$ = href2;
			
		DBG_PRINT}}
	;

id_list_ 
	: id_list_ ',' IDENTIFIER 
		{{
			
			PTR_VEC *pvec = NULL;
			pvec = pp_add_ptr (pvec, strdup ($3));
			$$ = pvec;
			
		DBG_PRINT}}
	| IDENTIFIER
		{{
			
			PTR_VEC *pvec;
			pvec = pp_new_ptr_vec (&id_list);
			pp_add_ptr (&id_list, strdup ($1));
			
			$$ = pvec;
			
		DBG_PRINT}}
	;

execute_statement
	: EXECUTE dynamic_statement using_part opt_into_result
		{{
			
			STMT *d_statement = $2;
			if (d_statement)
			  {
			    HOST_LOD *inputs, *outputs;
			    inputs = pp_input_refs ();
			    outputs = pp_output_refs ();
			    esql_Translate_table.tr_execute (d_statement->sid,
							     HOST_N_REFS (inputs),
							     HOST_REFS (inputs),
							     (const char *) HOST_DESC (inputs),
							     HOST_N_REFS (outputs),
							     HOST_REFS (outputs),
							     (const char *) HOST_DESC (outputs));
			    already_translated = true;
			  }
			
		DBG_PRINT}}
	| EXECUTE IMMEDIATE quasi_string_const 
		{{
			
			HOST_REF *href = $3;
			
			if (href)
			  esql_Translate_table.tr_execute_immediate (href);
			already_translated = true;
			
		DBG_PRINT}}
	;

opt_into_result
	: // empty
	| into_result
	;

fetch_statement
	: FETCH cursor into_result 
		{{
			
			CURSOR *cur = $2;
			if (cur)
			  {
			    HOST_LOD *hlod = pp_output_refs ();
			    esql_Translate_table.tr_fetch_cs (cur->cid,
							      HOST_N_REFS (hlod), HOST_REFS (hlod),
							      (char *) HOST_DESC (hlod));
			  }
			
			already_translated = true;
			
		DBG_PRINT}}
	| FETCH OBJECT host_variable_wo_indicator ON_ id_list_ into_result 
		{{
			
			HOST_REF *href = $3;
			PTR_VEC *pvec = $5;
			HOST_LOD *hlod;
			
			
			if (href && pvec
			    && (hlod = pp_output_refs ())
			    && pp_check_type (href,
					      NEWSET
					      (C_TYPE_OBJECTID),
					      pp_get_msg (EX_ESQLM_SET, MSG_PTR_TO_DB_OBJECT)))
			  {
			    esql_Translate_table.tr_object_fetch (href,
								  pp_ptr_vec_n_elems (pvec),
								  (const char **)
								  pp_ptr_vec_elems (pvec),
								  HOST_N_REFS (hlod),
								  HOST_REFS (hlod),
								  (const char *) HOST_DESC (hlod));
			  }
			
			pp_free_ptr_vec (pvec);
			already_translated = true;
			
		DBG_PRINT}}
	;

commit_statement
	: COMMIT opt_work
		{{
			
			esql_Translate_table.tr_commit ();
			already_translated = true;
			
		DBG_PRINT}}
	;

opt_work
	: // empty
	| WORK
	; 

rollback_statement
	: ROLLBACK opt_work
		{{
			
			esql_Translate_table.tr_rollback ();
			already_translated = true;
			
		DBG_PRINT}}
	;

csql_statement_head
	: ALTER 
		{{
			
			ECHO_STR ($1, strlen ($1));
			$$ = NO_CURSOR;
			
		DBG_PRINT}}
	| ATTACH 
		{{
			
			ECHO_STR ($1, strlen ($1));
			$$ = NO_CURSOR;
			
		DBG_PRINT}}
	| CALL_ 
		{{
			
			ECHO_STR ($1, strlen ($1));
			$$ = NO_CURSOR;
			
		DBG_PRINT}}
	| CREATE 
		{{
			
			ECHO_STR ($1, strlen ($1));
			$$ = NO_CURSOR;
			
		DBG_PRINT}}
	| DROP 
		{{
			
			ECHO_STR ($1, strlen ($1));
			$$ = NO_CURSOR;
			
		DBG_PRINT}}
	| END 
		{{
			
			ECHO_STR ($1, strlen ($1));
			$$ = NO_CURSOR;
			
		DBG_PRINT}}
	| EVALUATE 
		{{
			
			ECHO_STR ($1, strlen ($1));
			$$ = NO_CURSOR;
			
		DBG_PRINT}}
	| EXCLUDE 
		{{
			
			ECHO_STR ($1, strlen ($1));
			$$ = NO_CURSOR;
			
		DBG_PRINT}}
	| FOR 
		{{
			
			ECHO_STR ($1, strlen ($1));
			$$ = NO_CURSOR;
			
		DBG_PRINT}}
	| GET 
		{{
			
			ECHO_STR ($1, strlen ($1));
			$$ = NO_CURSOR;
			
		DBG_PRINT}}
	| GRANT 
		{{
			
			ECHO_STR ($1, strlen ($1));
			$$ = NO_CURSOR;
			
		DBG_PRINT}}
	| INSERT 
		{{
			
			ECHO_STR ($1, strlen ($1));
			$$ = NO_CURSOR;
			
		DBG_PRINT}}
	| ON_ 
		{{
			
			ECHO_STR ($1, strlen ($1));
			$$ = NO_CURSOR;
			
		DBG_PRINT}}
	| REGISTER_ 
		{{
			
			ECHO_STR ($1, strlen ($1));
			$$ = NO_CURSOR;
			
		DBG_PRINT}}
	| RENAME 
		{{
			
			ECHO_STR ($1, strlen ($1));
			$$ = NO_CURSOR;
			
		DBG_PRINT}}
	| REVOKE 
		{{
			
			ECHO_STR ($1, strlen ($1));
			$$ = NO_CURSOR;
			
		DBG_PRINT}}
	| SELECT 
		{{
			
			ECHO_STR ($1, strlen ($1));
			$$ = NO_CURSOR;
			
		DBG_PRINT}}
	| SET 
		{{
			
			ECHO_STR ($1, strlen ($1));
			$$ = NO_CURSOR;
			
		DBG_PRINT}}
	| TRIGGER 
		{{
			
			ECHO_STR ($1, strlen ($1));
			$$ = NO_CURSOR;
			
		DBG_PRINT}}
	| USE 
		{{
			
			ECHO_STR ($1, strlen ($1));
			$$ = NO_CURSOR;
			
		DBG_PRINT}}
	| DELETE_
		{{
			
			ECHO_STR ($1, strlen ($1));
			$$ = CURSOR_DELETE;
			
		DBG_PRINT}}
	| UPDATE 
		{{
			
			ECHO_STR ($1, strlen ($1));
			$$ = CURSOR_UPDATE;
			
		DBG_PRINT}}
	;

csql_statement
	: 
		{{
			
			esql_yy_set_buf (&pt_statement_buf);
			esql_yy_push_mode (BUFFER_mode);
			
		DBG_PRINT}}
	  csql_statement_head 
		{{
			
			g_cursor_type = $2;
						//esql_yy_push_mode(BUFFER_mode);
			
		DBG_PRINT}}
	  csql_statement_tail
		{DBG_PRINT}
	;


// c mode

declare_section_body
	: opt_ext_decl_list
		{DBG_PRINT}
	;

opt_ext_decl_list
	: // empty
		{DBG_PRINT}
	| ext_decl_list
		{DBG_PRINT}
	;


ext_decl_list
	: ext_decl_list external_declaration
		{DBG_PRINT}
	| external_declaration
		{DBG_PRINT}
	;

external_declaration
	: 
		{{
			
			pp_reset_current_type_spec ();
			
		DBG_PRINT}}
	  opt_specifier_list optional_declarators
		{{
			
			LINK *plink = $2;
			SYMBOL *psym = $3;
			
			if (psym)
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
				esql_yy_sync_lineno ();
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
			
		DBG_PRINT}} 
	  end_declarator_list
	;

opt_specifier_list
	: // empty
		{{
			
			$$ = pp_current_type_spec ();
			
		DBG_PRINT}}
	| specifier_list
		{{
			
			$$ = $1;
			
		DBG_PRINT}}
	;

optional_declarators
	: // empty
		{{
			
			$$ = NULL;
			
		DBG_PRINT}}
	| init_declarator_list
		{{
			
			$$ = $1;
			
		DBG_PRINT}}
	;

end_declarator_list
	: nS_td ';' 
		{{
			
						/*
						 * The specifier for this declarator list may have
						 * been a VARCHAR (or maybe not); if it was, echoing
						 * has been suppressed since we saw that keyword.
						 * Either way, we want to turn echoing back on.
						 */
			pp_suppress_echo (false);
			
		DBG_PRINT}}
	;

specifier_list
	: specifier_list specifier_
		{{
			
			$$ = pp_current_type_spec ();
			
		DBG_PRINT}}
	| specifier_
		{{
			
			$$ = pp_current_type_spec ();
			
		DBG_PRINT}}
	;

specifier_
	: storage_class_specifier 
		{DBG_PRINT}
	| type_specifier 
		{DBG_PRINT}
	| type_qualifier
		{DBG_PRINT}
	;

storage_class_specifier
	: TYPEDEF_ 
		{{
			
			pp_add_storage_class (TYPEDEF_);
			
		DBG_PRINT}}
	| EXTERN_ 
		{{
			
			pp_add_storage_class (EXTERN_);
			
		DBG_PRINT}}
	| STATIC_ 
		{{
			
			pp_add_storage_class (STATIC_);
			
		DBG_PRINT}}
	| AUTO_ 
		{{
			
			pp_add_storage_class (AUTO_);
			
		DBG_PRINT}}
	| REGISTER_ 
		{{
			
			pp_add_storage_class (REGISTER_);
			
		DBG_PRINT}}
	;

type_specifier
	: nS_ntd base_type_specifier 
		{DBG_PRINT}
	| type_adjective 
		{DBG_PRINT}
	| struct_specifier 
		{{
			
			pp_add_struct_spec ($1);
			
		DBG_PRINT}}
	| enum_specifier 
		{{
			
			pp_add_type_noun (INT_);
			pp_add_type_adj (INT_);
			
		DBG_PRINT}}
	;

base_type_specifier
	: VOID_ 
		{{
			
			pp_add_type_noun (VOID_);
			
		DBG_PRINT}}
	| CHAR_ 
		{{
			
			pp_add_type_noun (CHAR_);
			
		DBG_PRINT}}
	| INT_ 
		{{
			
			pp_add_type_noun (INT_);
			
		DBG_PRINT}}
	| FLOAT_ 
		{{
			
			pp_add_type_noun (FLOAT_);
			
		DBG_PRINT}}
	| DOUBLE_ 
		{{
			
			pp_add_type_noun (DOUBLE_);
			
		DBG_PRINT}}
	| TYPEDEF_NAME 
		{{
			
			pp_add_typedefed_spec (g_identifier_symbol->type);
			
		DBG_PRINT}}
	| VARCHAR_ 
		{{
			
						/*
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
			
		DBG_PRINT}}
	| BIT_ opt_varying
		{{
			
			pp_add_type_noun ($2);
			
		DBG_PRINT}}
	;

opt_varying
	: // empty
		{{
			
			$$ = BIT_;
			
		DBG_PRINT}}
	| VARYING 
		{{
			
			$$ = VARBIT_;
			
		DBG_PRINT}}
	;

type_adjective
	: SHORT_ 
		{{
			
			pp_add_type_adj (SHORT_);
			
		DBG_PRINT}}
	| LONG_ 
		{{
			
			pp_add_type_adj (LONG_);
			
		DBG_PRINT}}
	| SIGNED_ 
		{{
			
			pp_add_type_adj (SIGNED_);
			
		DBG_PRINT}}
	| UNSIGNED_ 
		{{
			
			pp_add_type_adj (UNSIGNED_);
			
		DBG_PRINT}}
	;

type_qualifier			
	: CONST_ 
		{{
			
			pp_add_type_adj (CONST_);
			
		DBG_PRINT}}
	| VOLATILE_ 
		{{
			
			pp_add_type_adj (VOLATILE_);
			
		DBG_PRINT}}
	;

struct_specifier
	: nS_ntd 
	  of_struct_union 
		{{
			
			g_su = $2;
			g_sdef2 = NULL;
			
		DBG_PRINT}}
	  struct_specifier_body 
		{{
			
			STRUCTDEF *sdef = $4;
			$$ = sdef;
			
		DBG_PRINT}}
	| nS_ntd 
	  of_struct_union
	  struct_tag
		{{
			
			g_su = $2;
			g_sdef2 = $3;
			
		DBG_PRINT}}
	  opt_struct_specifier_body
		{{
			
			STRUCTDEF *sdef = $5;
			sdef = g_sdef2;
			sdef->by_name = true;
			$$ = sdef;
			
		DBG_PRINT}}
	;

of_struct_union
	: STRUCT_
		{{
			
			$$ = 1;
			
		DBG_PRINT}}
	| UNION_
		{{
			
			$$ = 0;
			
		DBG_PRINT}}
	;

opt_struct_specifier_body
	: // emtpy
		{{
			
			$$ = NULL;
			
		DBG_PRINT}}
	| struct_specifier_body
		{{
			
			$$ = $1;
			
		DBG_PRINT}}
	;

struct_specifier_body
	: nS_td 
	  '{' 
		{{
			
			pp_push_spec_scope ();
			
		DBG_PRINT}}
	  struct_declaration_list 
		{{
			
			pp_pop_spec_scope ();
			
		DBG_PRINT}}
	  '}' 
		{{
			
			SYMBOL *psym = $4;
			
			if (g_sdef2 == NULL)
			  {
			    g_sdef2 = pp_new_structdef (NULL);
			    pp_Struct_table->add_symbol (pp_Struct_table, g_sdef2);
			  }
			if (psym == NULL)
			  {
			    pp_Struct_table->remove_symbol (pp_Struct_table, g_sdef2);
			    pp_discard_structdef (g_sdef2);
			  }
			else if (g_sdef2->fields)
			  {
			    varstring gack;
			    vs_new (&gack);
			    vs_sprintf (&gack, "%s %s", g_sdef2->type_string, g_sdef2->tag);
			    esql_yyredef (vs_str (&gack));
			    vs_free (&gack);
			    pp_discard_symbol_chain (psym);
			  }
			else
			  {
			    g_sdef2->type = g_su;
			    g_sdef2->type_string = (unsigned char *) (g_su ? "struct" : "union");
			    g_sdef2->fields = psym;
			  }
			
			$$ = g_sdef2;
			
		DBG_PRINT}}
	;

struct_tag
	: IDENTIFIER 
		{{
			
			STRUCTDEF *sdef;
			SYMBOL dummy;
			dummy.name = $1;
			
			if (!
			    (sdef =
			     (STRUCTDEF *) pp_Struct_table->find_symbol (pp_Struct_table, &dummy)))
			  {
			    sdef = pp_new_structdef (strdup($1));
			    pp_Struct_table->add_symbol (pp_Struct_table, sdef);
			  }
			
			$$ = sdef;
			
		DBG_PRINT}}
	;

struct_declaration_list 
	: struct_declaration_list struct_declaration 
		{{
			
			SYMBOL *last_field = $1;
			if (last_field)
			  {
			    SYMBOL *tmp = last_field->next;
			    last_field->next = $2;
			    last_field->next->next = tmp;
			  }
			
			$$ = $1;
			
		DBG_PRINT}}
	| struct_declaration 
		{{
			
			$$ = $1;
			
		DBG_PRINT}}
	;

struct_declaration
	: specifier_qualifier_list optional_struct_declarator_list 
		{{
			
			LINK *plink = $1;
			SYMBOL *psym = $2;
			SYMBOL *sym = NULL;
			
			if (psym)
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
				esql_yy_sync_lineno ();
			      }
			    sym = psym;
			  }
			else
			  sym = NULL;
			
			if (!plink->tdef)
			  pp_discard_link_chain (plink);
			
			g_struct_declaration_sym = sym;
			
		DBG_PRINT}}
	  end_declarator_list
		{{
			
			$$ = g_struct_declaration_sym;
			
		DBG_PRINT}}
	;

specifier_qualifier_list 
	:
		{{
			
			pp_reset_current_type_spec ();
			pp_disallow_storage_classes ();
			
		DBG_PRINT}}
	  specifier_list 
		{{
			
			$$ = $2;
			
		DBG_PRINT}}
	;

optional_struct_declarator_list 
	: // empty
		{{
			
			$$ = NULL;
			
		DBG_PRINT}}
	| struct_declarator_list
		{{
			
			$$ = $1;
			
		DBG_PRINT}}
	;

struct_declarator_list 
	: struct_declarator_list ',' struct_declarator
		{{
			
			SYMBOL *psym = $1;
			if (psym)
			  {
			    psym->next = $3;
			  }
			
			$$ = psym;
			
		DBG_PRINT}}
	| struct_declarator
		{{
			
			$$ = $1;
			
		DBG_PRINT}}
	;

struct_declarator 
	: ':'
		{{
			
			esql_yy_push_mode (EXPR_mode);
			
		DBG_PRINT}}
	  c_expression 
		{{
			
			SYMBOL *psym = NULL;
			$$ = psym;
			
		DBG_PRINT}}
	| declarator_
	  ':' 
		{{
			
			esql_yy_push_mode (EXPR_mode);
			
		DBG_PRINT}}
	  c_expression
		{{
			
			$$ = $1;
			
		DBG_PRINT}}
	| declarator_ 
		{{
			
			$$ = $1;
			
		DBG_PRINT}}
	;

enum_specifier
	: ENUM '{' enumerator_list '}' 
	| ENUM identifier optional_enumerator_list 
		{{
			
			SYMBOL *dl = $2;
			if (dl->type)
			  {
			    esql_yyredef ((char *) dl->name);
			  }
			else
			  {
			    pp_discard_symbol (dl);
			  }
			
		DBG_PRINT}}
	;

optional_enumerator_list
	: // empty
		{DBG_PRINT}
	| '{' enumerator_list '}'
		{DBG_PRINT}
	;

enumerator_list
	: enumerator_list ',' enumerator
		{DBG_PRINT}
	| enumerator
		{DBG_PRINT}
	;

enumerator
	: identifier 
		{{
			
			SYMBOL *dl = $1;
			pp_do_enum (dl);
			
		DBG_PRINT}}
	| identifier 
	  '=' 
		{{
			
			esql_yy_push_mode (EXPR_mode);
			
		DBG_PRINT}}
	  c_expression 
		{{
			
			SYMBOL *dl = $1;
			pp_do_enum (dl);
			
		DBG_PRINT}}
	;

init_declarator_list 
	: init_declarator_list ',' init_declarator 
		{{
			
			SYMBOL *psym = $1;
			SYMBOL *psym3 = $3;
			if (psym3)
			  {
			    psym3->next = psym;
			  }
			
			$$ = psym3;
			
		DBG_PRINT}}
		
	| init_declarator
		{{
			
			SYMBOL *psym = $1;
			if (psym)
			  psym->next = NULL;
			$$ = psym;
			
		DBG_PRINT}}
	;

init_declarator 
	: declarator_ 
		{{
			
			$$ = $1;
			
		DBG_PRINT}}
	| declarator_
	  nS_td
	  '='
		{{
			
			esql_yy_push_mode (EXPR_mode);
			
		DBG_PRINT}}
	  c_expression nS_ntd 
		{{
			
			SYMBOL *psym = $1;
			pp_add_initializer (psym);
			
			$$ = $1;
			
		DBG_PRINT}}
	;

declarator_
	: direct_declarator 
		{{
			
			$$ = $1;
			
		DBG_PRINT}}
	| pointer declarator_ 
		{{
			
			SYMBOL *psym = $2;
			if (psym)
			  pp_add_declarator (psym, D_POINTER);
			$$ = psym;
			
		DBG_PRINT}}
	;

direct_declarator 
	: IDENTIFIER 
		{{
			
			SYMBOL *psym = pp_new_symbol ($1, pp_nesting_level);
			g_psym = psym;
			
		DBG_PRINT}}
	  opt_param_spec_list
		{{
			
			$$ = g_psym;
			
		DBG_PRINT}}
	| '(' declarator_ ')' 
		{{
			
			SYMBOL *psym = pp_new_symbol ($2, pp_nesting_level);
			g_psym = psym;
			
		DBG_PRINT}}
	  opt_param_spec_list
		{{
			
			$$ = g_psym;
			
		DBG_PRINT}}
	;

opt_param_spec_list
	: // empty
		{DBG_PRINT}
	| param_spec_list
		{DBG_PRINT}
	;

param_spec_list
	: param_spec_list param_spec
		{{
			
			SYMBOL *args = $2;
			if (g_psym)
			  {
			    pp_add_declarator (g_psym, D_FUNCTION);
			    g_psym->etype->decl.d.args = args;
			  }
			
		DBG_PRINT}}
	| param_spec_list c_subscript
		{{
			
			const char *sub = $2;
			if (g_psym)
			  {
			    pp_add_declarator (g_psym, D_ARRAY);
			    g_psym->etype->decl.d.num_ele = (char *) sub;
			  }
			
		DBG_PRINT}}
	| param_spec
		{{
			
			SYMBOL *args = $1;
			if (g_psym)
			  {
			    pp_add_declarator (g_psym, D_FUNCTION);
			    g_psym->etype->decl.d.args = args;
			  }
			
		DBG_PRINT}}
	| c_subscript
		{{
			
			const char *sub = $1;
			if (g_psym)
			  {
			    pp_add_declarator (g_psym, D_ARRAY);
			    g_psym->etype->decl.d.num_ele = (char *) sub;
			  }
			
		DBG_PRINT}}
	; 

pointer
	: '*' opt_type_qualifier_list
		{DBG_PRINT}
	;

opt_type_qualifier_list
	: // empty
		{DBG_PRINT}
	| type_qualifier_list
		{DBG_PRINT}
	;

type_qualifier_list
	: type_qualifier_list type_qualifier
		{DBG_PRINT}
	| type_qualifier
		{DBG_PRINT}
	;

param_spec 
	: '(' ')'
		{{
			
			$$ = NULL;
			
		DBG_PRINT}}
	| '(' push_name_scope param_type_list pop_name_scope ')'
		{{
			
			$$ = $3;
			
		DBG_PRINT}}
	| '(' push_name_scope id_list_ pop_name_scope ')'
		{{
			
			$$ = $3;
			
		DBG_PRINT}}
	;

push_name_scope
	: // empty		 
		{{
			
			pp_push_name_scope ();
			
		DBG_PRINT}}
	;

pop_name_scope
	: // empty		 
		{{
			
			pp_pop_name_scope ();
			
		DBG_PRINT}}
	;

push_spec_scope
	: // empty 
		{{
			
			pp_push_spec_scope ();
			
		DBG_PRINT}}
	;

pop_spec_scope
	: // empty
		{{
			
			pp_pop_spec_scope ();
			
		DBG_PRINT}}
	;

param_type_list 
	: push_spec_scope parameter_list pop_spec_scope
		{{
			
			$$ = $2;
			
		DBG_PRINT}}
	;

parameter_list 
	: parameter_declaration trailing_params
		{{
			
			SYMBOL *psym = $1;
			psym->next = $2;
			$$ = psym;
			
		DBG_PRINT}}
	| parameter_declaration 
		{{
			
			$$ = $1;
			
		DBG_PRINT}}
	;

trailing_params 
	: param_decl_tail_list
		{{
			
			$$ = $1;
			
		DBG_PRINT}}
	| ',' ELLIPSIS 
		{{
			
			$$ = NULL;			// ???
			
		DBG_PRINT}}
  	;

param_decl_tail_list
	: param_decl_tail_list ',' parameter_declaration
		{{
			
			SYMBOL *psym = $1;
			SYMBOL *tmp = $3;
			g_last->next = tmp;
			g_last = tmp;
			
			$$ = psym;
			
		DBG_PRINT}}
	| ',' parameter_declaration
		{{
			
			$$ = g_last = $2;
			
		DBG_PRINT}}
	;

parameter_declaration
	: 
		{{
			
			pp_reset_current_type_spec ();
			
		DBG_PRINT}}
	  specifier_list 
	  opt_abstract_declarator 
	  nS_td
		{{
			
			SYMBOL *psym = $3;
			LINK *plink = $2;
			
			if (psym == NULL)
			  psym = pp_new_symbol (NULL, pp_nesting_level);
			
			pp_add_spec_to_decl (plink, psym);
			if (plink && !plink->tdef)
			  pp_discard_link_chain (plink);
			
			$$ = psym;
			
		DBG_PRINT}}
	;

opt_abstract_declarator
	: // empty
		{{
			
			$$ = NULL;
			
		DBG_PRINT}}
	| abstract_declarator
		{{
			
			$$ = $1;
			
		DBG_PRINT}}
	;

abstract_declarator
	: pointer opt_abstract_declarator 
		{{
			
			SYMBOL *psym = $2;
			
			if (psym == NULL)
			  psym = pp_new_symbol (NULL, pp_nesting_level);
			if (psym)
			  pp_add_declarator (psym, D_POINTER);
			
		DBG_PRINT}}
	| direct_abstract_declarator 
		{{
			
			$$ = $1;
			
		DBG_PRINT}}
	;

direct_abstract_declarator 
	: '(' abstract_declarator ')'
		{{
			
			g_psym = $2;
			
		DBG_PRINT}}
	  opt_direct_adecl_tail
		{{
			
			$$ = g_psym;
			
		DBG_PRINT}}
	| '(' opt_param_type_list ')'
		{{
			
			SYMBOL *args = $2;
			g_psym = pp_new_symbol (NULL, pp_nesting_level);
			pp_add_declarator (g_psym, D_FUNCTION);
			g_psym->etype->decl.d.args = args;
			
		DBG_PRINT}} 
	  opt_direct_adecl_tail
		{{
			
			$$ = g_psym;
			
		DBG_PRINT}}
	| IDENTIFIER 
		{{
			
			g_psym = pp_new_symbol ($1, pp_nesting_level);
			
		DBG_PRINT}}
	  opt_direct_adecl_tail
		{{
			
			$$ = g_psym;
			
		DBG_PRINT}}
	| c_subscript 
		{{
			
			const char *sub = $1;
			g_psym = pp_new_symbol (NULL, pp_nesting_level);
			pp_add_declarator (g_psym, D_ARRAY);
			g_psym->etype->decl.d.num_ele = (char *) sub;
			
		DBG_PRINT}} 
	  opt_direct_adecl_tail
		{{
			
			$$ = g_psym;
			
		DBG_PRINT}}
	;

opt_direct_adecl_tail
	: //empty
	| direct_adecl_tail
	;

direct_adecl_tail
	: direct_adecl_tail c_subscript
		{{
			
			const char *sub = $2;
			pp_add_declarator (g_psym, D_ARRAY);
			g_psym->etype->decl.d.num_ele = (char *) sub;
			
		DBG_PRINT}}
	| direct_adecl_tail '(' opt_param_type_list ')'
		{{
			
			SYMBOL *args = $3;
			pp_add_declarator (g_psym, D_FUNCTION);
			g_psym->etype->decl.d.args = args;
			
		DBG_PRINT}}
	| c_subscript
		{{
			
			const char *sub = $1;
			pp_add_declarator (g_psym, D_ARRAY);
			g_psym->etype->decl.d.num_ele = (char *) sub;
			
		DBG_PRINT}}
	| '(' opt_param_type_list ')'
		{{
			
			SYMBOL *args = $2;
			pp_add_declarator (g_psym, D_FUNCTION);
			g_psym->etype->decl.d.args = args;
			
		DBG_PRINT}}
	; 

opt_param_type_list
	: // empty
		{{
			
			$$ = NULL;
			
		DBG_PRINT}}
	| param_type_list
		{{
			
			$$ = $1;
			
		DBG_PRINT}}
	;

nS_ntd
	: // empty
		{{
			
			pp_make_typedef_names_visible (0);
			
		DBG_PRINT}}
	;

nS_td
	: // empty
		{{
			
			pp_make_typedef_names_visible (1);
			
		DBG_PRINT}}
	;

identifier 
	: nS_ntd TYPEDEF_NAME nS_td
		{{
			
			SYMBOL *result = pp_new_symbol ($2, pp_nesting_level);
			$$ = result;
			
		DBG_PRINT}}
   	| IDENTIFIER
		{{
			
			SYMBOL *result;
			if (!g_identifier_symbol || g_identifier_symbol->level != pp_nesting_level)
			  result = pp_new_symbol ($1, pp_nesting_level);
			else
			  result = g_identifier_symbol;
			
			$$ = result;
			
		DBG_PRINT}}
   	| ENUMERATION_CONSTANT
		{{
			
			SYMBOL *result = pp_new_symbol ($1, pp_nesting_level);
			$$ = result;
			
		DBG_PRINT}}
  	;

c_subscript 
	: '[' 
		{{
			
			varstring *subscript_buf = malloc (sizeof (varstring));
			
			esql_yy_push_mode (EXPR_mode);
			if (IS_PSEUDO_TYPE (pp_current_type_spec ()))
			  {
			    /*
			     * There is no need to store the old values of the
			     * echo function and echo suppression here because
			     * we're going to pop right ought of this mode and
			     * clobber them anyway.
			     */
			    vs_new (subscript_buf);
			    (void) esql_yy_set_buf (subscript_buf);
			    (void) pp_set_echo (&echo_vstr);
			    pp_suppress_echo (false);
			  }
			
			g_subscript = subscript_buf;
			
		DBG_PRINT}}
	  c_subscript_body 
	  ']'
		{{
			
			char *subscript = NULL;
			varstring *subscript_buf = g_subscript;
			
			if (IS_PSEUDO_TYPE (pp_current_type_spec ()))
			  {
			    /*
			     * Have to clobber the RB here, since the lookahead
			     * has already put it in our buffer.  Fortunately,
			     * we're in a mode where we can do that.
			     */
			    esql_yy_erase_last_token ();
			    esql_yy_set_buf (NULL);
			    subscript = pp_strdup (vs_str (subscript_buf));
			    vs_free (subscript_buf);
			  }
			
			esql_yy_pop_mode ();
			
			$$ = subscript;
			
		DBG_PRINT}}
	;


//#lexclass EXPR_mode


c_subscript_body
	: // empty
		{DBG_PRINT}
	| c_expr 
		{DBG_PRINT}
	;

c_expression
	: c_expr 
		{{
			
			esql_yy_pop_mode ();
			
		DBG_PRINT}}
	;

c_expr
	: any_list
		{DBG_PRINT}
	;

any_list
	: any_list any
		{DBG_PRINT}
	| any
		{DBG_PRINT}
	;

any_expr
	: // empty
		{DBG_PRINT}
	| acs_list
		{DBG_PRINT}
	;

acs_list
	: acs_list of_any_comma_semi
		{DBG_PRINT}
	| of_any_comma_semi
		{DBG_PRINT}
	;
	
of_any_comma_semi
	: any
		{DBG_PRINT}
	| ','
		{DBG_PRINT}
	| ';'
		{DBG_PRINT}
	;

any
	: GENERIC_TOKEN 
		{DBG_PRINT}
	| '(' any_expr ')' 
		{DBG_PRINT}
	| '[' any_expr ']' 
		{DBG_PRINT}
	| '{' any_expr '}'
		{DBG_PRINT}
	;


//#lexclass VAR_mode

host_variable_list_tail
	: host_variable_list_tail ',' host_var_w_opt_indicator
		{{
			
			ECHO_STR (", ", strlen (", "));
			
		DBG_PRINT}}
	| host_var_w_opt_indicator 
		{DBG_PRINT}
	;

indicator_var 
	: // empty	
		{{
			
			$$ = NULL;
			g_indicator = false;
			
		DBG_PRINT}}
	| '#' hostvar					
		{{
			
			$$ = $2;
			g_indicator = false;
			
		DBG_PRINT}}
	| INDICATOR '#' hostvar
		{{
			
			$$ = $3;
			g_indicator = false;
			
		DBG_PRINT}}
	| INDICATOR hostvar	
		{{
			
			$$ = $2;
			g_indicator = false;
			
		DBG_PRINT}}
	;


host_var_w_opt_indicator 
	: 
		{{
			
			HOST_REF *href = NULL;
			g_delay[0] = 0;
			g_varstring = esql_yy_get_buf ();
			
			esql_yy_push_mode (VAR_mode);
			esql_yy_set_buf (NULL);
			structs_allowed = true;
			
		DBG_PRINT}}
	  host_variable indicator_var
		{{
			
			HOST_REF *href;
			HOST_VAR *hvar1, *hvar2;
			hvar1 = $2;
			hvar2 = $3;
			
			esql_yy_pop_mode ();
			esql_yy_set_buf (g_varstring);
			
			href = pp_add_host_ref (hvar1, hvar2, structs_allowed, &n_refs);
			ECHO_STR ("? ", strlen ("? "));
			for (i = 1; i < n_refs; i++)
			  ECHO_STR (", ? ", strlen (", ? "));
			
			ECHO_STR (g_delay, strlen (g_delay));
			
			$$ = href;
			
		DBG_PRINT}}
	;

host_variable 
	: ':' 
		{{
			
			g_indicator = true;
			
		DBG_PRINT}}
	  hostvar 
		{{
			
			$$ = $3;
			
		DBG_PRINT}}
	;

hostvar 
	: host_ref 
		{{
			
			$$ = $1;
			
		DBG_PRINT}}
	| '*' CONST_ hostvar 
		{{
			
			HOST_VAR *hvar = NULL;
			HOST_VAR *hvar2 = $3;
			if ((hvar = pp_ptr_deref (hvar2, 0)) == NULL)
			  pp_free_host_var (hvar2);
			else
			  vs_prepend (&hvar->expr, "*");
			
			$$ = hvar;
			
		DBG_PRINT}}
	| '*' VOLATILE_ hostvar 
		{{
			
			HOST_VAR *hvar = NULL;
			HOST_VAR *hvar2 = $3;
			if ((hvar = pp_ptr_deref (hvar2, 0)) == NULL)
			  pp_free_host_var (hvar2);
			else
			  vs_prepend (&hvar->expr, "*");
			
			$$ = hvar;
			
		DBG_PRINT}}
	| '*' hostvar 
		{{
			
			HOST_VAR *hvar = NULL;
			HOST_VAR *hvar2 = $2;
			if ((hvar = pp_ptr_deref (hvar2, 0)) == NULL)
			  pp_free_host_var (hvar2);
			else
			  vs_prepend (&hvar->expr, "*");
			
			$$ = hvar;
			
		DBG_PRINT}}
	| '&' host_ref 
		{{
			
			HOST_VAR *hvar;
			HOST_VAR *href2 = $2;
			
			if ((hvar = pp_addr_of (href2)) == NULL)
			  pp_free_host_var (href2);
			else
			  vs_prepend (&hvar->expr, "&");
			
			$$ = hvar;
			
		DBG_PRINT}}
	;

host_ref 
	: IDENTIFIER			
		{{
			
			HOST_VAR *href = NULL;
			SYMBOL *sym = pp_findsym (pp_Symbol_table, $1);
			if (sym == NULL)
			  {
			    esql_yyverror (pp_get_msg (EX_ESQLM_SET, MSG_NOT_DECLARED), $1);
			    href = NULL;
			  }
			else
			  href = pp_new_host_var (NULL, sym);
			
			g_href = href;
			
		DBG_PRINT}} 	
	  opt_host_ref_tail 		
		{{
			
			HOST_VAR *href = $3;
			if (href == NULL)
			  href = g_href;
			$$ = href;
			
		DBG_PRINT}}		
	| '(' hostvar ')' 		
		{{
			
			HOST_VAR *href = $2;
			if (href)
			  {
			    vs_prepend (&href->expr, "(");
			    vs_strcat (&href->expr, ")");
			  }
			g_href = href;
			
		DBG_PRINT}} 
	  opt_host_ref_tail			
		{{
			
			HOST_VAR *href = $5;
			if (href == NULL)
			  href = g_href;
			$$ = href;
			
		DBG_PRINT}}		
	;

opt_host_ref_tail
	: 			%dprec 1
		// empty
		{{
			
			$$ = NULL;
			
		DBG_PRINT}}
	| host_ref_tail		%dprec 2
		{{
			
			$$ = $1;
			
		DBG_PRINT}}
	;

host_ref_tail
	: host_ref_tail host_var_subscript 
		{{
			
			$$ = $2;
			
		DBG_PRINT}}
	| host_ref_tail '.' IDENTIFIER
		{{
			
			HOST_VAR *href;
			if ((href = pp_struct_deref (g_href, $3, 0)) == NULL)
			  pp_free_host_var (g_href);
			else
			  vs_sprintf (&href->expr, ".%s", $3);
			
			$$ = href;
			
		DBG_PRINT}}
	| host_ref_tail PTR_OP IDENTIFIER
		{{
			
			HOST_VAR *href;
			if ((href = pp_struct_deref (g_href, $3, 1)) == NULL)
			  pp_free_host_var (g_href);
			else
			  vs_sprintf (&href->expr, "->%s", $3);
			
			$$ = href;
			
		DBG_PRINT}}
	| host_var_subscript 
		{{
			
			$$ = $1;
			
		DBG_PRINT}}
	| '.' IDENTIFIER
		{{
			
			HOST_VAR *href;
			if ((href = pp_struct_deref (g_href, $2, 0)) == NULL)
			  pp_free_host_var (g_href);
			else
			  vs_sprintf (&href->expr, ".%s", $2);
			
			$$ = href;
			
		DBG_PRINT}}
	| PTR_OP IDENTIFIER
		{{
			
			HOST_VAR *href;
			if ((href = pp_struct_deref (g_href, $2, 1)) == NULL)
			  pp_free_host_var (g_href);
			else
			  vs_sprintf (&href->expr, "->%s", $2);
			
			$$ = href;
			
		DBG_PRINT}}
	;

host_var_subscript
	: '[' 
		{{
			
			vs_clear (&pp_subscript_buf);
			esql_yy_push_mode (HV_mode);
			
		DBG_PRINT}}
	  host_var_subscript_body
		{{
			
			$$ = $3;
			
		DBG_PRINT}}
	;


host_var_subscript_body
	: opt_hv_sub_list ']' 
		{{
			
			HOST_VAR *href;
			
			esql_yy_pop_mode ();
			if ((href = pp_ptr_deref (g_href, 1)) == NULL)
			  pp_free_host_var (g_href);
			else
			  vs_sprintf (&href->expr, "[%s%s", vs_str (&pp_subscript_buf), "]");
			
			$$ = href;
			
		DBG_PRINT}}
	;

// HV_mode
hv_sub
	: '(' 
		{{
			
			vs_strcat (&pp_subscript_buf, "(");
			
		DBG_PRINT}}
	  opt_hv_sub_list 
	  ')' 
		{{
			
			vs_strcat (&pp_subscript_buf, ")");
			
		DBG_PRINT}}
	| '[' 
		{{
			
			vs_strcat (&pp_subscript_buf, "[");
			
		DBG_PRINT}}
	  opt_hv_sub_list 
	  ']' 
		{{
			
			vs_strcat (&pp_subscript_buf, "]");
			
		DBG_PRINT}}
	| '{' 
		{{
			
			vs_strcat (&pp_subscript_buf, "{");
			
		DBG_PRINT}}
	  opt_hv_sub_list 
	  '}' 
		{{
			
			vs_strcat (&pp_subscript_buf, "}");
			
		DBG_PRINT}}
	;

opt_hv_sub_list
	: // empty
		{DBG_PRINT}
	| hv_sub_list
		{DBG_PRINT}
	;

hv_sub_list
	: hv_sub_list hv_sub
		{DBG_PRINT}
	| hv_sub
		{DBG_PRINT}
	;



// BUFFER_mode


csql_statement_tail 
	: opt_buffer_sql_list
		{{
			
			PT_NODE **pt, *cursr;
			char *statement, *text;
			PT_HOST_VARS *result;
			long is_upd_obj;
			HOST_LOD *inputs, *outputs;
			int length;
			PARSER_VARCHAR *varPtr;
			esql_yy_pop_mode ();
			statement = vs_str (&pt_statement_buf);
			length = (int) vs_strlen (&pt_statement_buf);
			
			PRINT_1 ("statement: %s\n", statement);
			pt = parser_parse_string (parser, statement);
			
			if (!pt || !pt[0])
			  {
			    pt_print_errors (parser);
			  }
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
				  {
				    esql_yyverror (pp_get_msg (EX_ESQLM_SET, MSG_CURSOR_UNDEFINED),
						   c_nam);
				  }
				else
				  {
				    switch (g_cursor_type)
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
				repeat_option = (pt[0]->node_type == PT_INSERT && !pt[0]->info.insert.do_replace && pt[0]->info.insert.odku_assignments == NULL
				   && pt[0]->info.insert.value_clauses->info.node_list.list_type != PT_IS_SUBQUERY
				   && pt[0]->info.insert.value_clauses->info.node_list.list->next == NULL);
				esql_Translate_table.tr_static (text,
								length,
								repeat_option,
								HOST_N_REFS (inputs),
								HOST_REFS (inputs),
								(const char *) HOST_DESC (inputs),
								HOST_N_REFS (outputs),
								HOST_REFS (outputs),
								(const char *) HOST_DESC (outputs));
			      }
			    pt_free_host_info (result);
			  }
			
			esql_yy_sync_lineno ();
			esql_yy_set_buf (NULL);
			
		DBG_PRINT}}
	;

opt_buffer_sql_list
	: // empty
		{DBG_PRINT}
	| buffer_sql_list
		{DBG_PRINT}
	;

buffer_sql_list
	: buffer_sql_list buffer_sql		
		{DBG_PRINT}
	| buffer_sql			
		{DBG_PRINT}
	;

buffer_sql
	: host_var_w_opt_indicator 
		{DBG_PRINT}
	| DESCRIPTOR host_variable_wo_indicator 
		{{
			
			HOST_REF *href = $2;
			if (pp_check_type (href,
					   NEWSET (C_TYPE_SQLDA),
					   pp_get_msg (EX_ESQLM_SET, MSG_PTR_TO_DESCR)))
			  pp_switch_to_descriptor ();
			
		DBG_PRINT}}
	| var_mode_follow_set
		{DBG_PRINT}
	;

var_mode_follow_set 
	: '*'
		{DBG_PRINT}
	| ','
		{DBG_PRINT}
	| '('
		{DBG_PRINT}
	| ')'
		{DBG_PRINT}
	| '{'
		{DBG_PRINT}
	| '}'
		{DBG_PRINT}
	| GENERIC_TOKEN 
		{DBG_PRINT}
	| IDENTIFIER 
		{DBG_PRINT}
	| INTO	
		{{
			
			pp_gather_output_refs ();
			
		DBG_PRINT}}
	| TO
		{{
			
			pp_gather_output_refs ();
			
		DBG_PRINT}}
	| FROM 
		{{
			
			pp_gather_input_refs ();
			
		DBG_PRINT}}
	| SELECT 
		{{
			
			pp_gather_input_refs ();
			
		DBG_PRINT}}
	| VALUES 
		{{
			
			pp_gather_input_refs ();
			
		DBG_PRINT}}
	| ON_ 
		{DBG_PRINT}
	| WITH
		{DBG_PRINT}
	;

cursor_query_statement 
	: 
		{{
			
			vs_clear (&pt_statement_buf);
			esql_yy_set_buf (&pt_statement_buf);
			esql_yy_push_mode (BUFFER_mode);
			
		DBG_PRINT}}
	  of_select_lp opt_buffer_sql_list
		{{
			
			char *statement;
			char *text = NULL;
			long length;
			PT_NODE **pt;
			char **cp = malloc (sizeof (char *) * 2);
			
			esql_yy_pop_mode ();
			statement = vs_str (&pt_statement_buf);
			length = vs_strlen (&pt_statement_buf);
			
			PRINT_1 ("statement: %s\n", statement);
			pt = parser_parse_string (parser, statement);
			if (!pt || !pt[0])
			  {
			    pt_print_errors (parser);
			  }
			else
			  text = statement;
			
			cp[0] = text;
			cp[1] = (char *) length;
			
			esql_yy_sync_lineno ();
			esql_yy_set_buf (NULL);
			
			$$ = cp;
			
		DBG_PRINT}}
	;


of_select_lp
	: SELECT
		{{
			
			ECHO_STR ($1, strlen ($1));
			
		DBG_PRINT}}
	| '('
		{{
			
			ECHO_STR ("(", strlen ("("));
			
		DBG_PRINT}}
	;


//#lexclass COMMENT_mode		



%% 

#define UCI_OPT_UNSAFE_NULL     0x0001
#define makestring1(x) #x
#define makestring(x) makestring1(x)
#if !defined(PRODUCT_STRING)
#define PRODUCT_STRING makestring(RELEASE_STRING)
#endif

#define ESQL_MSG_CATALOG        "esql.cat"

char exec_echo[10] = "";
static char *outfile_name = NULL;
bool need_line_directive;
FILE *esql_yyin, *esql_yyout;
char *esql_yyfilename;
int esql_yyendcol = 0;
int errors = 0;
varstring *current_buf;		/* remain PUBLIC for debugging ease */
ECHO_FN echo_fn = &echo_stream;
char g_delay[1024] = { 0, };
bool g_indicator = false;
varstring *g_varstring = NULL;
varstring *g_subscript = NULL;

typedef struct pt_host_refs
{
  unsigned char *descr[2];	/* at most 2 descriptors: 1 in, 1 out */
  int descr_occupied;		/* occupied slots in descr            */
  HOST_REF **host_refs;		/* array of pointers to HOST_REF      */
  int occupied;			/* occupied slots in host_refs        */
  int allocated;		/* allocated slots in host_refs       */
  unsigned char *in_descriptor;	/* NULL or input descriptor name      */
  unsigned char *out_descriptor;	/* NULL or output descriptor name     */
  HOST_REF **in_refs;		/* array of input HOST_REF addresses  */
  int in_refs_occupied;
  int in_refs_allocated;
  HOST_REF **out_refs;		/* array of output HOST_REF addresses */
  int out_refs_occupied;
  int out_refs_allocated;
} PT_HOST_REFS;

enum esqlmain_msg
{
  MSG_REPEATED_IGNORED = 1,
  MSG_VERSION = 2
};

const char *VARCHAR_ARRAY_NAME = "array";
const char *VARCHAR_LENGTH_NAME = "length";
const char *pp_include_path;
const char *pp_include_file = NULL;
const char *prog_name;

unsigned int pp_uci_opt = 0;
int pp_emit_line_directives = 1;
int pp_dump_scope_info = 0;
int pp_dump_malloc_info = 0;
int pp_announce_version = 0;
int pp_enable_uci_trace = 0;
int pp_disable_varchar_length = 0;
int pp_varchar2 = 0;
int pp_unsafe_null = 0;
int pp_internal_ind = 0;
PARSER_CONTEXT *parser;
char *pt_buffer;
varstring pt_statement_buf;


/*
 * check() -
 * return : void
 * cond(in) :
 */
static void
check (int cond)
{
  if (!cond)
    {
      perror (prog_name);
      exit (1);
    }
}

/*
 * check_w_file() -
 * return : void
 * cond(in) :
 * filename(in) :
 */
static void
check_w_file (int cond, const char *filename)
{
  if (!cond)
    {
      char buf[2 * PATH_MAX];
      sprintf (buf, "%s: %s", prog_name, filename);
      perror (buf);
      exit (1);
    }
}

/*
 * reset_globals() -
 * return : void
 */
static void
reset_globals (void)
{
  input_refs = NULL;
  output_refs = NULL;
  repeat_option = false;
  already_translated = false;
  structs_allowed = false;
  hvs_allowed = true;

  esql_yy_set_buf (NULL);
  pp_clear_host_refs ();
  pp_gather_input_refs ();
}

/*
 * ignore_repeat() -
 * return : void
 */
static void
ignore_repeat (void)
{
  if (repeat_option)
    {
      esql_yyvwarn (pp_get_msg (EX_ESQLMMAIN_SET, MSG_REPEATED_IGNORED));
    }
}


#if defined (ENABLE_UNUSED_FUNCTION)
static int
validate_input_file (void *fname, FILE * outfp)
{
  return (fname == NULL) && isatty (fileno (stdin));
}
#endif /* ENABLE_UNUSED_FUNCTION */


static char *
ofile_name (char *fname)
{
  static char buf[PATH_MAX + 1]; /* reserve buffer for '\0' */
  char *p;

  /* Get the last pathname component into buf. */
  p = strrchr (fname, '/');
  strncpy (buf, p ? (p + 1) : fname, sizeof(buf));

  /* Now add the .c suffix, copying over the .ec suffix if present. */
  p = strrchr (buf, '.');
  if (p && !STREQ (p, ".c"))
    {
      strcpy (p, ".c");
    }
  else
    {
      strncat (buf, ".c", PATH_MAX - strnlen(buf, sizeof(buf)));
    }

  return buf;
}

/*
 * copy() -
 * return : void
 * fp(in) :
 * fname(in) :
 */
static void
copy (FILE * fp, const char *fname)
{
  int ifd, ofd;
  int rbytes, wbytes;
  char buf[BUFSIZ];

  rewind (fp);

  ifd = fileno (fp);
  ofd = open (fname, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  check_w_file (ofd >= 0, fname);

  /* Now copy the contents; use read() and write() to try to cut down
     on excess buffering. */
  rbytes = 0;
  wbytes = 0;
  while ((rbytes = read (ifd, buf, sizeof (buf))) > 0)
    {
      wbytes = write (ofd, buf, rbytes);
      if (wbytes != rbytes)
	{
	  break;
	}
    }
  check (rbytes >= 0 && wbytes >= 0);

  close (ofd);
}


static const char *
pp_unsafe_get_msg (int msg_set, int msg_num)
{
  static int complained = 0;
  static MSG_CATD msg_cat = NULL;

  const char *msg;

  if (msg_cat == NULL)
    {
      if (complained)
	{
	  return NULL;
	}

      msg_cat = msgcat_open (ESQL_MSG_CATALOG);
      if (msg_cat == NULL)
	{
	  complained = 1;
	  fprintf (stderr, "%s: %s: %s\n",
		   prog_name, ESQL_MSG_CATALOG, strerror (ENOENT));
	  return NULL;
	}
    }

  msg = msgcat_gets (msg_cat, msg_set, msg_num, NULL);

  return (msg == NULL || msg[0] == '\0') ? NULL : msg;
}

/*
 * pp_get_msg() -
 * return :
 * msg_set(in) :
 * msg_num(in) :
 */
const char *
pp_get_msg (int msg_set, int msg_num)
{
  static const char no_msg[] = "No message available.";
  const char *msg;

  msg = pp_unsafe_get_msg (msg_set, msg_num);
  return msg ? msg : no_msg;
}


static void
usage (void)
{
  fprintf (stderr,
	   "usage: cubrid_esql [OPTION] input-file\n\n"
	   "valid option:\n"
	   "  -l, --suppress-line-directive  suppress #line directives in output file; default: off\n"
	   "  -o, --output-file              output file name; default: stdout\n"
	   "  -h, --include-file             include file containing uci_ prototypes; default: cubrid_esql.h\n"
	   "  -s, --dump-scope-info          dump scope debugging information; default: off\n"
	   "  -m, --dump-malloc-info         dump final malloc debugging information; default: off\n"
	   "  -t, --enable-uci-trace         enable UCI function trace; default: off\n"
	   "  -d, --disable-varchar-length   disable length initialization of VARCHAR host variable; default; off\n"
	   "  -2, --varchar2-style           use different VARCHAR style; default: off\n"
	   "  -u, --unsafe-null              ignore -462(null indicator needed) error: default; off\n"
	   "  -e, --internal-indicator       use internal NULL indicator to prevent -462(null indicator needed) error, default off\n");
}

static int
get_next ()
{
  return fgetc (esql_yyin);
}

/*
 * main() -
 * return :
 * argc(in) :
 * argv(in) :
 */
int
main (int argc, char **argv)
{
  struct option esql_option[] = {
    {"suppress-line-directive", 0, 0, 'l'},
    {"output-file", 1, 0, 'o'},
    {"include-file", 1, 0, 'h'},
    {"dump-scope-info", 0, 0, 's'},
    {"dump-malloc-info", 0, 0, 'm'},
    {"version", 0, 0, 'v'},
    {"enable-uci-trace", 0, 0, 't'},
    {"disable-varchar-length", 0, 0, 'd'},
    {"varchar2-style", 0, 0, '2'},
    {"unsafe-null", 0, 0, 'u'},
    {"internal-indicator", 0, 0, 'e'},
    {0, 0, 0, 0}
  };

  while (1)
    {
      int option_index = 0;
      int option_key;

      option_key = getopt_long (argc, argv, "lo:h:smvtd2ue",
				esql_option, &option_index);
      if (option_key == -1)
	{
	  break;
	}

      switch (option_key)
	{
	case 'l':
	  pp_emit_line_directives = true;
	  break;
	case 'o':
	  if (outfile_name != NULL)
	    {
	      free_and_init(outfile_name);
	    }
	  outfile_name = strdup (optarg);
	  break;
	case 'h':
	  if (pp_include_file != NULL)
	    {
	      free_and_init(pp_include_file);
	    }
	  pp_include_file = strdup (optarg);
	  break;
	case 's':
	  pp_dump_scope_info = true;
	  break;
	case 'm':
	  pp_dump_malloc_info = true;
	  break;
	case 'v':
	  pp_announce_version = true;
	  break;
	case 't':
	  pp_enable_uci_trace = true;
	  break;
	case 'd':
	  pp_disable_varchar_length = true;
	  break;
	case '2':
	  pp_varchar2 = true;
	  break;
	case 'u':
	  pp_unsafe_null = true;
	  break;
	case 'e':
	  pp_internal_ind = true;
	  break;
	default:
	  usage ();
	  return errors;
	}
    }

  if (optind < argc)
    {
      esql_yyfilename = argv[optind];
    }
  else
    {
      usage ();
      return errors;
    }

  prog_name = argv[0];
  if (pp_announce_version)
    {
      printf (pp_get_msg (EX_ESQLMMAIN_SET, MSG_VERSION), argv[0], PRODUCT_STRING);
    }

  if (pp_varchar2)
    {
      VARCHAR_ARRAY_NAME = "arr";
      VARCHAR_LENGTH_NAME = "len";
    }

  if (pp_unsafe_null)
    {
      pp_uci_opt |= UCI_OPT_UNSAFE_NULL;
    }

  pp_include_path = envvar_root ();
  if (pp_include_path == NULL)
    {
      exit (1);
    }

  if (esql_yyfilename == NULL)
    {
      /* No input filename was supplied; use stdin for input and stdout
         for output. */
      esql_yyin = stdin;
    }
  else
    {
      esql_yyin = fopen (esql_yyfilename, "r");
      check_w_file (esql_yyin != NULL, esql_yyfilename);
    }

  if (esql_yyfilename && !outfile_name)
    {
      outfile_name = ofile_name (esql_yyfilename);
    }

  if (outfile_name)
    {
      esql_yyout = tmpfile ();
      check (esql_yyout != NULL);
    }
  else
    {
      esql_yyout = stdout;
    }

  esql_yyinit ();
  pp_startup ();
  vs_new (&pt_statement_buf);
  emit_line_directive ();

  if (utility_initialize () != NO_ERROR)
    {
      exit (1);
    }
  parser = parser_create_parser ();
  pt_buffer = pt_append_string (parser, NULL, NULL);

  esql_yyparse ();

  if (esql_yyout != stdout)
    {
      if (errors == 0)
	{
	  /*
	   * We want to keep the file, so rewind the temp file and copy
	   * it to the named file. It really would be nice if we could
	   * just associate the name with the file resources we've
	   * already created, but there doesn't seem to be a way to do
	   * that.
	   */
	  copy (esql_yyout, outfile_name);
	}
    }

  if (esql_yyin != NULL)
    {
      fclose (esql_yyin);
    }

  if (esql_yyout != NULL)
    {
      fclose (esql_yyout);
    }

  msgcat_final ();
  exit (errors);

  free_and_init (outfile_name);
  free_and_init (pp_include_file);

  return errors;
}

/*
 * pt_print_errors() - print to stderr all parsing error messages associated
 *    with current statement
 * return : void
 * parser_in(in) : the parser context
 */
void
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
	esql_yyverror (msg);
    }
  while (err);

  /* sleazy way to clear errors */
  parser = parser_create_parser ();
}


/*
 * pp_suppress_echo() -
 * return : void
 * suppress(in) :
 */
void
pp_suppress_echo (int suppress)
{
  suppress_echo = suppress;
}

/*
 * echo_stream() -
 * return: void
 * str(in) :
 * length(in) :
 */
void
echo_stream (const char *str, int length)
{
  if (!suppress_echo)
    {
      if (exec_echo[0])
	{
	  fputs (exec_echo, esql_yyout);
	  exec_echo[0] = 0;
	}
      fwrite ((char *) str, length, 1, esql_yyout);
      //YY_FLUSH_ON_DEBUG;
    }
}

/*
 * echo_vstr() -
 * return : void
 * str(in) :
 * length(in) :
 */
void
echo_vstr (const char *str, int length)
{

  if (!suppress_echo)
    {
      if (current_buf)
	{
	  vs_strcatn (current_buf, str, length);
	}
    }
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * echo_devnull() -
 * return : void
 * str(in) :
 * length(in) :
 */
void
echo_devnull (const char *str, int length)
{
  /* Do nothing */
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * pp_set_echo() -
 * return :
 * new_echo(in) :
 */
ECHO_FN
pp_set_echo (ECHO_FN new_echo)
{
  ECHO_FN old_echo;

  old_echo = echo_fn;
  echo_fn = new_echo;

  return old_echo;
}


/*
 * esql_yyvmsg() -
 * return : void
 * fmt(in) :
 * args(in) :
 */
static void
esql_yyvmsg (const char *fmt, va_list args)
{
  /* Flush esql_yyout just in case stderr and esql_yyout are the same. */
  fflush (esql_yyout);

  if (esql_yyfilename)
    {
      fprintf (stderr, "%s:", esql_yyfilename);
    }

  fprintf (stderr, "%d: ", esql_yylineno);
  vfprintf (stderr, fmt, args);
  fputs ("\n", stderr);
  fflush (stderr);
}

/*
 * esql_yyerror() -
 * return : void
 * s(in) :
 */
void
esql_yyerror (const char *s)
{
  esql_yyverror ("%s before \"%s\"", s, esql_yytext);
}

/*
 * esql_yyverror() -
 * return : void
 * fmt(in) :
 */
void
esql_yyverror (const char *fmt, ...)
{
  va_list args;

  errors++;

  va_start (args, fmt);
  esql_yyvmsg (fmt, args);
  va_end (args);
}

/*
 * esql_yyvwarn() -
 * return : void
 * fmt(in) :
 */
void
esql_yyvwarn (const char *fmt, ...)
{
  va_list args;

  va_start (args, fmt);
  esql_yyvmsg (fmt, args);
  va_end (args);
}

/*
 * esql_yyredef() -
 * return : void
 * name(in) :
 */
void
esql_yyredef (char *name)
{
  /* Eventually we will probably want to turn this off, but it's
     interesting to find out about perceived redefinitions right now. */
  esql_yyverror ("redefinition of symbol \"%s\"", name);
}




/*
 * keyword_cmp() -
 * return :
 * a(in) :
 * b(in) :
 */
static int
keyword_cmp (const void *a, const void *b)
{
  return strcmp (((const KEYWORD_REC *) a)->keyword,
		 ((const KEYWORD_REC *) b)->keyword);
}

/*
 * keyword_case_cmp() -
 * return :
 * a(in) :
 * b(in) :
 */
static int
keyword_case_cmp (const void *a, const void *b)
{
  return intl_mbs_casecmp (((const KEYWORD_REC *) a)->keyword,
			   ((const KEYWORD_REC *) b)->keyword);
}





/*
 * check_c_identifier() -
 * return :
 */
int
check_c_identifier (char *name)
{
  KEYWORD_REC *p;
  KEYWORD_REC dummy;
  SYMBOL *sym;

#ifdef PARSER_DEBUG
  printf ("check c id: %s\n", name);
#endif

  /*
   * Check first to see if we have a keyword.
   */
  dummy.keyword = name;
  p = (KEYWORD_REC *) bsearch (&dummy,
			       c_keywords,
			       sizeof (c_keywords) / sizeof (c_keywords[0]),
			       sizeof (KEYWORD_REC), &keyword_cmp);

  if (p)
    {
      /*
       * Notic that this is "sticky", unlike in check_identifier().
       * Once we've seen a keyword that initiates suppression, we don't
       * want anything echoed until the upper level grammar productions
       * say so.  In particular, we don't want echoing to be re-enabled
       * the very next time we see an ordinary C keyword.
       */
      suppress_echo = suppress_echo || p->suppress_echo;
      return p->value;
    }

  /*
   * If not, see if this is an already-encountered identifier.
   */
  g_identifier_symbol = sym =
    pp_findsym (pp_Symbol_table, (unsigned char *) name);
  if (sym)
    {
      /*
       * Something by this name lives in the symbol table.
       *
       * This needs to be made sensitive to whether typedef names are
       * being recognized or not.
       */
      return sym->type->tdef && pp_recognizing_typedef_names ? TYPEDEF_NAME :
	IS_INT_CONSTANT (sym->type) ? ENUMERATION_CONSTANT : IDENTIFIER;
    }
  else
    {
      /*
       * Symbol wasn't recognized in the symbol table, so this must be
       * a new identifier.
       */
      return IDENTIFIER;
    }
}



/*
 * check_identifier() -
 * return :
 * keywords(in) :
 */
int
check_identifier (KEYWORD_TABLE * keywords, char *name)
{
  KEYWORD_REC *p;
  KEYWORD_REC dummy;

  /*
   * Check first to see if we have a keyword.
   */
  dummy.keyword = name;
  p = (KEYWORD_REC *) bsearch (&dummy,
			       keywords->keyword_array,
			       keywords->size,
			       sizeof (KEYWORD_REC), &keyword_case_cmp);

  if (p)
    {
      suppress_echo = p->suppress_echo;
      return p->value;
    }
  else
    {
      suppress_echo = false;
      return IDENTIFIER;
    }
}

/*
 * esql_yy_push_mode() -
 * return : void
 * new_mode(in) :
 */
void
esql_yy_push_mode (enum scanner_mode new_mode)
{
  SCANNER_MODE_RECORD *next = malloc (sizeof (SCANNER_MODE_RECORD));
  if (next == NULL)
    {
      esql_yyverror (pp_get_msg (EX_MISC_SET, MSG_OUT_OF_MEMORY));
      exit(1);
      return;
    }

  next->previous_mode = mode;
  next->recognize_keywords = recognize_keywords;
  next->previous_record = mode_stack;
  next->suppress_echo = suppress_echo;
  mode_stack = next;

  /*
   * Set up so that the first id-like token that we see in VAR_MODE
   * will be reported as an IDENTIFIER, regardless of whether it
   * strcasecmps the same as some SQL/X keyword.
   */
  recognize_keywords = (new_mode != VAR_MODE);

  esql_yy_enter (new_mode);
}

/*
 * esql_yy_check_mode() -
 * return : void
 */
void
esql_yy_check_mode (void)
{
  if (mode_stack != NULL)
    {
      esql_yyverror
	("(esql_yy_check_mode) internal error: mode stack still full");
      exit (1);
    }
}

/*
 * esql_yy_pop_mode() -
 * return : void
 */
void
esql_yy_pop_mode (void)
{
  SCANNER_MODE_RECORD *prev;
  enum scanner_mode new_mode;

  if (mode_stack == NULL)
    {
      esql_yyverror (pp_get_msg (EX_ESQLMSCANSUPPORT_SET, MSG_EMPTY_STACK));
      exit (1);
    }

  prev = mode_stack->previous_record;
  new_mode = mode_stack->previous_mode;
  recognize_keywords = mode_stack->recognize_keywords;
  suppress_echo = mode_stack->suppress_echo;

  free_and_init (mode_stack);
  mode_stack = prev;

  esql_yy_enter (new_mode);
}

/*
 * esql_yy_mode() -
 * return :
 */
enum scanner_mode
esql_yy_mode (void)
{
  return mode;
}

/*
 * esql_yy_enter() -
 * return : void
 * new_mode(in) :
 */
void
esql_yy_enter (enum scanner_mode new_mode)
{
  static struct
  {
    const char *name;
    int mode;
    void (*echo_fn) (const char *, int);
  }
  mode_info[] =
  {
    {
    "echo", START, &echo_stream},	/* ECHO_MODE 0  */
    {
    "csql", CSQL_mode, &echo_vstr},	/* CSQL_MODE 1  */
    {
    "C", C_mode, &echo_stream},	/* C_MODE    2  */
    {
    "expr", EXPR_mode, &echo_stream},	/* EXPR_MODE 3  */
    {
    "var", VAR_mode, &echo_vstr},	/* VAR_MODE  4  */
    {
    "hostvar", HV_mode, &echo_vstr},	/* HV_MODE   5  */
    {
    "buffer", BUFFER_mode, &echo_vstr},	/* BUFFER_MODE  */
    {
    "comment", COMMENT_mode, NULL}	/* COMMENT      */
  };


  PRINT_1 ("#set mode : %s\n", mode_info[new_mode].name);
  mode = new_mode;
  if (mode_info[new_mode].echo_fn)
    echo_fn = mode_info[new_mode].echo_fn;
}

/*
 * emit_line_directive() -
 * return : void
 */
void
emit_line_directive (void)
{
  if (pp_emit_line_directives)
    {
      if (esql_yyfilename)
	fprintf (esql_yyout, "#line %d \"%s\"\n", esql_yylineno,
		 esql_yyfilename);
      else
	fprintf (esql_yyout, "#line %d\n", esql_yylineno);
    }

  need_line_directive = false;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * ignore_token() -
 * return : void
 */
static void
ignore_token (void)
{
  esql_yyverror (pp_get_msg (EX_ESQLMSCANSUPPORT_SET, MSG_NOT_PERMITTED),
		 esql_yytext);
}

/*
 * count_embedded_newlines() -
 * return : void
 */
static void
count_embedded_newlines (void)
{
  const char *p = esql_yytext;
  for (; *p; ++p)
    {
      if (*p == '\n')
	{
	  ++esql_yylineno;
	  esql_yyendcol = 0;
	}
      ++esql_yyendcol;
    }
}



/*
 * echo_string_constant() -
 * return : void
 * str(in) :
 * length(in) :
 *
 * TODO(M2) : This may be incorrect now!
 * according to ansi, there is no escape syntax for newlines!
 */
static void
echo_string_constant (const char *str, int length)
{
  const char *p;
  char *q, *result;
  int charsLeft = length;

  p = memchr (str, '\n', length);

  if (p == NULL)
    {
      ECHO_STR (str, length);
      return;
    }

  /*
   * Bad luck; there's at least one embedded newline, so we have to do
   * something to get rid of it (if it's escaped).  If it's unescaped
   * then this is a pretty weird string, but we have to let it go.
   *
   * Because we come in here with the opening quote mark still intact,
   * we're always guaranteed that there is a valid character in front
   * of the result of strchr().
   */

  result = q = malloc (length + 1);
  if (q == NULL)
    {
      esql_yyverror (pp_get_msg (EX_MISC_SET, MSG_OUT_OF_MEMORY));
      exit(1);
      return;
    }

  /*
   * Copy the spans between escaped newlines to the new buffer.  If we
   * encounter an unescaped newline we just include it.
   */
  while (p)
    {

      if (*(p - 1) == '\\')
	{
	  int span;

	  span = p - str - 1;	/* exclude size of escape char */
	  memcpy (q, str, span);
	  q += span;
	  str = p + 1;
	  length -= 2;
	  /* Exclude size of skipped escape and newline chars from string */
	  charsLeft -= (span + 2);
	}

      p = memchr (p + 1, '\n', charsLeft);
    }

  memcpy (q, str, charsLeft);

  ECHO_STR (result, length);
  free_and_init (result);
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * esql_yy_sync_lineno() -
 * return : void
 */
void
esql_yy_sync_lineno (void)
{
  need_line_directive = true;
}

/*
 * esql_yy_set_buf() -
 * return : void
 * vstr(in) :
 */
void
esql_yy_set_buf (varstring * vstr)
{
  current_buf = vstr;
}

/*
 * esql_yy_get_buf() -
 * return : void
 */
varstring *
esql_yy_get_buf (void)
{
  return current_buf;
}

/*
 * esql_yy_erase_last_token() -
 * return : void
 */
void
esql_yy_erase_last_token (void)
{
  if (current_buf && esql_yytext)
    current_buf->end -= strlen (esql_yytext);
}

/*
 * esql_yyinit() -
 * return : void
 */
void
esql_yyinit (void)
{
  /*
   * We've been burned too many times by people who can't alphabetize;
   * just sort the bloody thing once to protect ourselves from bozos.
   */
  qsort (c_keywords,
	 sizeof (c_keywords) / sizeof (c_keywords[0]),
	 sizeof (c_keywords[0]), &keyword_cmp);
  qsort (csql_keywords,
	 sizeof (csql_keywords) / sizeof (csql_keywords[0]),
	 sizeof (csql_keywords[0]), &keyword_case_cmp);

  mode_stack = NULL;
  recognize_keywords = true;
  esql_yy_enter (START);
}


char* mm_buff = NULL;
int mm_idx = 0;
int mm_size = 0;

/*
 * mm_malloc() -
 * return : char*
 * size(in) :
 */
char* 
mm_malloc (int size)
{
  char* ret;

  if (mm_buff==NULL) 
    {
      mm_size = 1024*1024;
      mm_buff = pp_malloc(mm_size);
    }

  if (mm_idx + size > mm_size)
    {
      mm_size *= 2;
      ret = pp_malloc(mm_size);
      free(mm_buff);
      mm_buff = ret;
    }

  ret = &mm_buff[mm_idx];
  mm_idx += size;

  return ret;
}


/*
 * mm_strdup() -
 * return : char*
 * str(in) :
 */
char* 
mm_strdup (char* str)
{
  const int max_len = 1024 * 1024 * 10;
  char* buff;
  int len = strnlen(str, max_len);

  if (len == max_len) 
    {
      esql_yyverror("(mm_strdup) internal error: string size is too large");
      exit (1);
    }

  buff = mm_malloc(len + 1);
  strncpy(buff, str, len);

  return buff;
}


/*
 * mm_free() -
 * return : void
 */
void
mm_free(void)
{
  if (mm_buff)
    {
      free(mm_buff);
      mm_buff = NULL;
      mm_size = 0;
      mm_idx = 0;
    }
}











