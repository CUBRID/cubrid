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
 * csql_grammar.y - SQL grammar file
 */



%{/*%CODE_REQUIRES_START%*/
#include "json_table_def.h"
#include "parser.h"

/*
 * The default YYLTYPE structure is extended so that locations can hold
 * context information
 */
typedef struct YYLTYPE
{

  int first_line;
  int first_column;
  int last_line;
  int last_column;
  int buffer_pos; /* position in the buffer being parsed */

} YYLTYPE;
#define YYLTYPE_IS_DECLARED 1

typedef struct
{
  PT_NODE *c1;
  PT_NODE *c2;
} container_2;

typedef struct
{
  PT_NODE *c1;
  PT_NODE *c2;
  PT_NODE *c3;
} container_3;

typedef struct
{
  PT_NODE *c1;
  PT_NODE *c2;
  PT_NODE *c3;
  PT_NODE *c4;
} container_4;

typedef struct
{
  PT_NODE *c1;
  PT_NODE *c2;
  PT_NODE *c3;
  PT_NODE *c4;
  PT_NODE *c5;
  PT_NODE *c6;
  PT_NODE *c7;
  PT_NODE *c8;
  PT_NODE *c9;
  PT_NODE *c10;
} container_10;

void csql_yyerror_explicit (int line, int column);
void csql_yyerror (const char *s);

extern int g_msg[1024];
extern int msg_ptr;
extern int yybuffer_pos;

#if defined(SA_MODE)
     /*
     ** DBG_TRACE_LEVEL is specified by referring to the following.
     ** 0: No displayed on the screen
     ** 1: Words extracted from csql_lexer are displayed on the screen
     ** 2: Display the contents of application of grammar rules in csql_grammar on the screen
     ** 3: Expression of both 1 and 2 above
     */
#    define DBG_TRACE_LEVEL      0
#endif

#if (DBG_TRACE_LEVEL == 1 || DBG_TRACE_LEVEL == 3) 
#    define DBG_PRINT_TOKEN(token)            fprintf(stdout, " *** Token) [%s]\n", token);
#    define DBG_PRINT_STRING(pre, str, post)  fprintf(stdout, " *** Token) [%s%s%s]\n", pre, str, post);
#    define DBG_PRINT_TOKEN_END()             fprintf(stdout, "\n");
#else
#    define DBG_PRINT_TOKEN(token)
#    define DBG_PRINT_STRING(pre, str, post)
#    define DBG_PRINT_TOKEN_END()
#endif

#if (DBG_TRACE_LEVEL == 2 || DBG_TRACE_LEVEL == 3) 
#    define DBG_TRACE_GRAMMAR(rule_head, rule_components) do {                            \
                fprintf(stdout, " *** Rule) " #rule_head "::= "  #rule_components "\n");  \
                fflush(stdout);                                                           \
        } while(0)
#else        
#    define DBG_TRACE_GRAMMAR(rule_head, rule_components)
#endif

static void pt_fill_conn_info_container(PARSER_CONTEXT *parser, int buffer_pos, container_10 *ctn, container_2 info);
/*%CODE_END%*/%}

%{
#define YYMAXDEPTH	1000000

/* #define PARSER_DEBUG */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <errno.h>
#include <float.h>

#include "chartype.h"
#include "parser.h"
#include "parser_message.h"
#include "language_support.h"
#include "unicode_support.h"
#include "environment_variable.h"
#include "dbtype.h"
#include "transaction_cl.h"
#include "csql_grammar_scan.h"
#include "system_parameter.h"
#define JP_MAXNAME 256
#if defined(WINDOWS)
#define snprintf _sprintf_p
#endif /* WINDOWS */
#include "memory_alloc.h"
#include "db_elo.h"
#include "storage_common.h"

#if defined (SUPPRESS_STRLEN_WARNING)
#define strlen(s1)  ((int) strlen(s1))
#endif /* defined (SUPPRESS_STRLEN_WARNING) */

/* Bit mask to be used to check constraints of a column.
 * COLUMN_CONSTRAINT_SHARED_DEFAULT_AI is special-purpose mask
 * to identify duplication of SHARED, DEFAULT and AUTO_INCREMENT.
 */
#define COLUMN_CONSTRAINT_UNIQUE		(0x01)
#define COLUMN_CONSTRAINT_PRIMARY_KEY		(0x02)
#define COLUMN_CONSTRAINT_NULL			(0x04)
#define COLUMN_CONSTRAINT_OTHERS		(0x08)
#define COLUMN_CONSTRAINT_SHARED		(0x10)
#define COLUMN_CONSTRAINT_DEFAULT		(0x20)
#define COLUMN_CONSTRAINT_AUTO_INCREMENT	(0x40)
#define COLUMN_CONSTRAINT_SHARED_DEFAULT_AI	(0x70)
#define COLUMN_CONSTRAINT_COMMENT       (0x80)
#define COLUMN_CONSTRAINT_ON_UPDATE     (0x100)

#ifdef PARSER_DEBUG
#define DBG_PRINT printf("rule matched at line: %d\n", __LINE__);
#define PRINT_(a) printf(a)
#define PRINT_1(a, b) printf(a, b)
#define PRINT_2(a, b, c) printf(a, b, c)
#else
#define DBG_PRINT
#define PRINT_(a)
#define PRINT_1(a, b)
#define PRINT_2(a, b, c)
#endif

#define STACK_SIZE	128


static int parser_groupby_exception = 0;




/* xxxnum_check: 0 not allowed, no compatibility check
		 1 allowed, compatibility check (search_condition)
		 2 allowed, no compatibility check (select_list) */
static int parser_instnum_check = 0;
static int parser_groupbynum_check = 0;
static int parser_orderbynum_check = 0;
static int parser_within_join_condition = 0;

/* xxx_check: 0 not allowed
              1 allowed */
static int parser_sysconnectbypath_check = 0;
static int parser_prior_check = 0;
static int parser_connectbyroot_check = 0;
static int parser_serial_check = 1;
static int parser_pseudocolumn_check = 1;
static int parser_subquery_check = 1;
static int parser_hostvar_check = 1;

/* check Oracle style outer-join operator: '(+)' */
static bool parser_found_Oracle_outer = false;

/* check sys_date, sys_time, sys_timestamp, sys_datetime local_transaction_id */
static bool parser_si_datetime = false;
static bool parser_si_tran_id = false;

/* check the condition that the statment is not able to be prepared */
static bool parser_cannot_prepare = false;

/* check the condition that the result of a query is not able to be cached */
static bool parser_cannot_cache = false;

/* check if INCR is used legally */
static int parser_select_level = -1;

/* handle inner increment exprs in select list */
static PT_NODE *parser_hidden_incr_list = NULL;

/* for opt_over_analytic_partition_by */
static bool is_analytic_function = false;

#define PT_EMPTY INT_MAX


#define TO_NUMBER(a)			((UINTPTR)(a))
#define FROM_NUMBER(a)			((PT_NODE*)(UINTPTR)(a))


#define SET_CONTAINER_2(a, i, j)		a.c1 = i, a.c2 = j
#define SET_CONTAINER_3(a, i, j, k)		a.c1 = i, a.c2 = j, a.c3 = k
#define SET_CONTAINER_4(a, i, j, k, l)		a.c1 = i, a.c2 = j, a.c3 = k, a.c4 = l

#define CONTAINER_AT_0(a)			(a).c1
#define CONTAINER_AT_1(a)			(a).c2
#define CONTAINER_AT_2(a)			(a).c3
#define CONTAINER_AT_3(a)			(a).c4
#define CONTAINER_AT_4(a)			(a).c5
#define CONTAINER_AT_5(a)			(a).c6
#define CONTAINER_AT_6(a)			(a).c7
#define CONTAINER_AT_7(a)			(a).c8
#define CONTAINER_AT_8(a)			(a).c9
#define CONTAINER_AT_9(a)			(a).c10

#define YEN_SIGN_TEXT           "(\0xa1\0xef)"
#define DOLLAR_SIGN_TEXT        "$"
#define WON_SIGN_TEXT           "\\"
#define TURKISH_LIRA_TEXT       "TL"
#define BRITISH_POUND_TEXT      "GBP"
#define CAMBODIAN_RIEL_TEXT     "KHR"
#define CHINESE_RENMINBI_TEXT   "CNY"
#define INDIAN_RUPEE_TEXT       "INR"
#define RUSSIAN_RUBLE_TEXT      "RUB"
#define AUSTRALIAN_DOLLAR_TEXT  "AUD"
#define CANADIAN_DOLLAR_TEXT    "CAD"
#define BRASILIAN_REAL_TEXT     "BRL"
#define ROMANIAN_LEU_TEXT       "RON"
#define EURO_TEXT               "EUR"
#define SWISS_FRANC_TEXT        "CHF"
#define DANISH_KRONE_TEXT       "DKK"
#define NORWEGIAN_KRONE_TEXT    "NOK"
#define BULGARIAN_LEV_TEXT      "BGN"
#define VIETNAMESE_DONG_TEXT    "VND"
#define CZECH_KORUNA_TEXT       "CZK"
#define POLISH_ZLOTY_TEXT       "PLN"
#define SWEDISH_KRONA_TEXT      "SEK"
#define CROATIAN_KUNA_TEXT      "HRK"
#define SERBIAN_DINAR_TEXT      "RSD"

#define PARSER_SAVE_ERR_CONTEXT(node, context) \
  if ((node) && (node)->buffer_pos == -1) \
    { \
     (node)->buffer_pos = context; \
    }

typedef enum
{
  SERIAL_START,
  SERIAL_INC,
  SERIAL_MAX,
  SERIAL_MIN,
  SERIAL_CYCLE,
  SERIAL_CACHE,
} SERIAL_DEFINE;

typedef enum
{
  CONN_INFO_HOST = 0,
  CONN_INFO_PORT,
  CONN_INFO_DBNAME,
  CONN_INFO_USER,
  CONN_INFO_PASSWORD,
  CONN_INFO_PROPERTIES,
  CONN_INFO_COMMENT,
  CONN_INFO_OWNER,
} CONN_INFO_DEFINE;

FUNCTION_MAP *keyword_offset (const char *name);

static PT_NODE *parser_make_expr_with_func (PARSER_CONTEXT * parser, FUNC_TYPE func_code, PT_NODE * args_list);
static PT_NODE *parser_make_func_with_arg_count (PARSER_CONTEXT * parser, FUNC_TYPE func_code, PT_NODE * args_list,
                                                 size_t min_args, size_t max_args);
static PT_NODE *parser_make_func_with_arg_count_mod2 (PARSER_CONTEXT * parser, FUNC_TYPE func_code, PT_NODE * args_list,
                                                      size_t min_args, size_t max_args, size_t mod2);

static PT_NODE *parser_make_link (PT_NODE * list, PT_NODE * node);
static PT_NODE *parser_make_link_or (PT_NODE * list, PT_NODE * node);

static void parser_save_and_set_cannot_cache (bool value);
static void parser_restore_cannot_cache (void);

static void parser_save_and_set_si_datetime (int value);
static void parser_restore_si_datetime (void);

static void parser_save_and_set_si_tran_id (int value);
static void parser_restore_si_tran_id (void);

static void parser_save_and_set_cannot_prepare (bool value);
static void parser_restore_cannot_prepare (void);

static void parser_save_and_set_wjc (int value);
static void parser_restore_wjc (void);

static void parser_save_and_set_ic (int value);
static void parser_restore_ic (void);

static void parser_save_and_set_gc (int value);
static void parser_restore_gc (void);

static void parser_save_and_set_oc (int value);
static void parser_restore_oc (void);

static void parser_save_and_set_sysc (int value);
static void parser_restore_sysc (void);

static void parser_save_and_set_prc (int value);
static void parser_restore_prc (void);

static void parser_save_and_set_cbrc (int value);
static void parser_restore_cbrc (void);

static void parser_save_and_set_serc (int value);
static void parser_restore_serc (void);

static void parser_save_and_set_pseudoc (int value);
static void parser_restore_pseudoc (void);

static void parser_save_and_set_sqc (int value);
static void parser_restore_sqc (void);

static void parser_save_and_set_hvar (int value);
static void parser_restore_hvar (void);

static void parser_save_found_Oracle_outer (void);
static void parser_restore_found_Oracle_outer (void);

static void parser_save_alter_node (PT_NODE * node);
static PT_NODE *parser_get_alter_node (void);

static void parser_save_attr_def_one (PT_NODE * node);
static PT_NODE *parser_get_attr_def_one (void);

static void parser_push_orderby_node (PT_NODE * node);
static PT_NODE *parser_top_orderby_node (void);
static PT_NODE *parser_pop_orderby_node (void);

static void parser_push_select_stmt_node (PT_NODE * node);
static PT_NODE *parser_top_select_stmt_node (void);
static PT_NODE *parser_pop_select_stmt_node (void);
static bool parser_is_select_stmt_node_empty (void);

static void parser_push_hint_node (PT_NODE * node);
static PT_NODE *parser_top_hint_node (void);
static PT_NODE *parser_pop_hint_node (void);
static bool parser_is_hint_node_empty (void);

static void parser_push_join_type (int v);
static int parser_top_join_type (void);
static int parser_pop_join_type (void);

static void parser_save_is_reverse (bool v);
static bool parser_get_is_reverse (void);

static void parser_initialize_parser_context (void);
static PT_NODE *parser_make_date_lang (int arg_cnt, PT_NODE * arg3);
static PT_NODE *parser_make_number_lang (const int argc);
static void parser_remove_dummy_select (PT_NODE ** node);
static int parser_count_list (PT_NODE * list);
static int parser_count_prefix_columns (PT_NODE * list, int * arg_count);

static void resolve_alias_in_expr_node (PT_NODE * node, PT_NODE * list);
static void resolve_alias_in_name_node (PT_NODE ** node, PT_NODE * list);
static char * pt_check_identifier (PARSER_CONTEXT *parser, PT_NODE *p,
				   const char *str, const int str_size);
static PT_NODE * pt_create_char_string_literal (PARSER_CONTEXT *parser,
						const PT_TYPE_ENUM char_type,
						const char *str,
						const INTL_CODESET codeset);
static PT_NODE * pt_create_date_value (PARSER_CONTEXT *parser,
				       const PT_TYPE_ENUM type,
				       const char *str);
static PT_NODE * pt_create_json_value (PARSER_CONTEXT *parser,
				       const char *str);
static void pt_jt_append_column_or_nested_node (PT_NODE * jt_node, PT_NODE * jt_col_or_nested);

#define DBLINK_CONN_PARAM_CNT   (6)
static bool pt_ct_check_fill_connection_info (char *pIn, char *pInfo[], char *perr_msg );
static bool pt_ct_check_select (char* p, char *perr_msg);

static PT_NODE* pt_mk_spec_drived_dblink_table(PT_SPEC_INFO* class_spec, PT_NODE* select_col_list);
static void pt_convert_dblink_query(PT_NODE* query_stmt);

static void pt_value_set_charset_coll (PARSER_CONTEXT *parser,
				       PT_NODE *node,
				       const int codeset_id,
				       const int collation_id, bool force);
static void pt_value_set_collation_info (PARSER_CONTEXT *parser,
					 PT_NODE *node,
					 PT_NODE *coll_node);
static void pt_value_set_monetary (PARSER_CONTEXT *parser, PT_NODE *node,
                   const char *str, const char *txt, DB_CURRENCY type);
static PT_NODE * pt_create_paren_expr_list (PT_NODE * exp);
static PT_MISC_TYPE parser_attr_type;

static bool allow_attribute_ordering;

int parse_one_statement (int state);
static PT_NODE *pt_set_collation_modifier (PARSER_CONTEXT *parser,
					   PT_NODE *node, PT_NODE *coll_node);

static PT_NODE * pt_check_non_logical_expr (PARSER_CONTEXT * parser, PT_NODE * node);


#define push_msg(a) _push_msg(a, __LINE__)

void _push_msg (int code, int line);
void pop_msg (void);

char *g_query_string;
int g_query_string_len;
int g_original_buffer_len;


/*
 * The behavior of location propagation when a rule is matched must
 * take into account the context information. The left-side symbol in a rule
 * will have the same context information as the last symbol from its
 * right side
 */
#define YYLLOC_DEFAULT(Current, Rhs, N)				        \
    do									\
      if (N)								\
	{								\
	  (Current).first_line   = YYRHSLOC (Rhs, 1).first_line;	\
	  (Current).first_column = YYRHSLOC (Rhs, 1).first_column;	\
	  (Current).last_line    = YYRHSLOC (Rhs, N).last_line;		\
	  (Current).last_column  = YYRHSLOC (Rhs, N).last_column;	\
	  (Current).buffer_pos   = YYRHSLOC (Rhs, N).buffer_pos;	\
	}								\
      else								\
	{								\
	  (Current).first_line   = (Current).last_line   =		\
	    YYRHSLOC (Rhs, 0).last_line;				\
	  (Current).first_column = (Current).last_column =		\
	    YYRHSLOC (Rhs, 0).last_column;				\
	  (Current).buffer_pos   = YYRHSLOC (Rhs, 0).buffer_pos;	\
	}								\
    while (0)

/*
 * YY_LOCATION_PRINT -- Print the location on the stream.
 * This macro was not mandated originally: define only if we know
 * we won't break user code: when these are the locations we know.
 */

#define YY_LOCATION_PRINT(File, Loc)			\
    fprintf (File, "%d.%d-%d.%d",			\
	     (Loc).first_line, (Loc).first_column,	\
	     (Loc).last_line,  (Loc).last_column)

#define SET_CPTR_2_PTNAME(rv, iv, b_p) do {             \
   (rv) = parser_new_node (this_parser, PT_NAME);       \
   if ((rv))                                            \
     {                                                  \
             (rv)->info.name.original = (iv);           \
     }                                                  \
   PARSER_SAVE_ERR_CONTEXT ((rv), (b_p))                \
   DBG_PRINT                                            \
} while (0)

#define MAKE_MONETARY_LITERAL(rv, iv, b_p, sign_text, currency_type) do {    \
        char *str, *txt;                                                        \
	PT_NODE *val = parser_new_node (this_parser, PT_VALUE);                 \
        str = pt_append_string (this_parser, NULL, (sign_text));                \
	txt = (iv);                                                             \
	if (val)                                                                \
	  {                                                                     \
	    pt_value_set_monetary (this_parser, val, str, txt, (currency_type));\
	  }                                                                     \
	(rv) = val;                                                             \
	PARSER_SAVE_ERR_CONTEXT ((rv), (b_p))                                   \
} while (0)

%}

%initial-action {yybuffer_pos = 0;}
%locations
%glr-parser
%define parse.error verbose


%union
{
  int number;
  bool boolean;
  PT_NODE *node;
  char *cptr;
  container_2 c2;
  container_3 c3;
  container_4 c4;
  container_10 c10;
  struct json_table_column_behavior jtcb;
}


/* define rule type (number) */
/*{{{*/
%type <boolean> opt_reverse
%type <boolean> opt_unique
%type <boolean> opt_cascade
%type <boolean> opt_cascade_constraints
%type <number> opt_replace
%type <number> opt_of_inner_left_right
%type <number> opt_class_type
%type <number> opt_of_attr_column_method
%type <number> opt_class
%type <number> isolation_level_name
%type <number> opt_status
%type <number> trigger_status
%type <number> trigger_time
%type <number> opt_trigger_action_time
%type <number> event_type
%type <number> opt_of_data_type_cursor
%type <number> all_distinct
%type <number> of_avg_max_etc
%type <number> of_leading_trailing_both
%type <number> datetime_field
%type <boolean> opt_invisible
%type <number> opt_paren_plus
%type <number> opt_with_fullscan
%type <number> opt_with_online
%type <number> comp_op
%type <number> opt_of_all_some_any
%type <number> set_op
%type <number> char_bit_type
%type <number> opt_internal_external
%type <number> opt_identity
%type <number> set_type
%type <number> opt_of_container
%type <number> of_container
%type <number> opt_with_levels_clause
%type <number> of_class_table_type
%type <number> opt_with_grant_option
%type <number> opt_sp_in_out
%type <number> opt_in_out
%type <number> like_op
%type <number> rlike_op
%type <number> null_op
%type <number> is_op
%type <number> in_op
%type <number> between_op
%type <number> opt_varying
%type <number> nested_set
%type <number> opt_asc_or_desc
%type <number> opt_with_rollup
%type <number> opt_table_type
%type <number> opt_or_replace
%type <number> column_constraint_and_comment_def
%type <number> constraint_list_and_column_comment
%type <number> opt_constraint_list_and_opt_column_comment
%type <number> opt_full
%type <number> of_analytic
%type <number> of_analytic_first_last
%type <number> of_analytic_nth_value
%type <number> of_analytic_lead_lag
%type <number> of_percentile
%type <number> of_analytic_no_args
%type <number> of_cume_dist_percent_rank_function
%type <number> negative_prec_cast_type
%type <number> opt_nulls_first_or_last
%type <number> query_trace_spec
%type <number> opt_trace_output_format
%type <number> opt_if_not_exists
%type <number> opt_if_exists
%type <number> opt_recursive
%type <number> show_type
%type <number> show_type_of_like
%type <number> show_type_of_where
%type <number> show_type_arg1
%type <number> show_type_arg1_opt
%type <number> show_type_arg_named
%type <number> show_type_id
%type <number> show_type_id_dot_id
%type <number> kill_type
%type <number> procedure_or_function
%type <boolean> opt_analytic_from_last
%type <boolean> opt_analytic_ignore_nulls
%type <number> opt_encrypt_algorithm
/*}}}*/

/* define rule type (node) */
/*{{{*/
%type <node> stmt
%type <node> stmt_
%type <node> create_stmt
%type <node> set_stmt
%type <node> get_stmt
%type <node> auth_stmt
%type <node> transaction_stmt
%type <node> alter_stmt
%type <node> alter_clause_list
%type <node> rename_stmt
%type <node> rename_class_list
%type <node> rename_class_pair
%type <node> drop_stmt
%type <node> opt_index_column_name_list
%type <node> index_column_name_list
%type <node> update_statistics_stmt
%type <node> only_class_name_list
%type <node> opt_level_spec
%type <node> char_string_literal_list
%type <node> table_spec_list
%type <node> join_table_spec
%type <node> table_spec
%type <node> original_table_spec
%type <node> join_condition
%type <node> class_spec_list
%type <node> class_spec
%type <node> only_all_class_spec_list
%type <node> meta_class_spec
%type <node> only_all_class_spec
%type <node> object_name
%type <node> user_specified_name_without_dot
%type <node> user_specified_name
%type <node> class_name_without_dot
%type <node> class_name
%type <node> class_name_with_server_name
%type <node> class_name_list
%type <node> trigger_name_without_dot
%type <node> trigger_name
%type <node> trigger_name_list
%type <node> serial_name_without_dot
%type <node> serial_name
%type <node> opt_identifier
%type <node> normal_or_class_attr_list_with_commas
%type <node> normal_or_class_attr
%type <node> normal_column_or_class_attribute
%type <node> query_number_list
%type <node> insert_or_replace_stmt
%type <node> insert_set_stmt
%type <node> replace_set_stmt
%type <node> insert_set_stmt_header
%type <node> insert_expression
%type <node> opt_attr_list
%type <node> opt_path_attr_list
%type <node> into_clause_opt
%type <node> insert_value_clause
%type <node> insert_value_clause_list
%type <node> insert_stmt_value_clause
%type <node> select_or_subquery_without_values_query_no_with_clause
%type <node> csql_query_without_values_query_no_with_clause
%type <node> select_expression_without_values_query_no_with_clause
%type <node> csql_query_without_subquery_and_with_clause
%type <node> select_expression_without_subquery
%type <node> select_or_values_query
%type <node> subquery_without_subquery_and_with_clause
%type <node> select_or_nested_values_query
%type <node> csql_query_without_values_and_single_subquery
%type <node> select_expression_without_values_and_single_subquery
%type <node> insert_expression_value_clause
%type <node> insert_value_list
%type <node> insert_value
%type <node> update_stmt
%type <node> of_class_spec_meta_class_spec
%type <node> opt_as_identifier
%type <node> update_assignment_list
%type <node> update_assignment
%type <node> paren_path_expression_set
%type <node> path_expression_list
%type <node> delete_stmt
%type <node> author_cmd_list
%type <node> authorized_cmd
%type <node> opt_password
%type <node> opt_groups
%type <node> opt_members
%type <node> call_stmt
%type <node> opt_class_or_normal_attr_def_list
%type <node> opt_method_def_list
%type <node> opt_method_files
%type <node> opt_inherit_resolution_list
%type <node> opt_partition_clause
%type <node> opt_paren_view_attr_def_list
%type <node> opt_as_query_list
%type <node> opt_group_concat_separator
%type <node> opt_agg_order_by
%type <node> query_list
%type <node> inherit_resolution_list
%type <node> inherit_resolution
%type <node> opt_table_option_list
%type <node> table_option_list
%type <node> table_option
%type <node> opt_subtable_clause
%type <node> opt_constraint_id
%type <node> opt_constraint_opt_id
%type <node> of_unique_foreign_check
%type <node> unique_constraint
%type <node> foreign_key_constraint
%type <node> opt_paren_attr_list
%type <node> check_constraint
%type <node> method_def_list
%type <node> method_def
%type <node> opt_method_def_arg_list
%type <node> arg_type_list
%type <node> inout_data_type
%type <node> opt_function_identifier
%type <node> opt_class_attr_def_list
%type <node> class_or_normal_attr_def_list
%type <node> view_attr_def_list
%type <node> attr_def_comment_list
%type <node> attr_def_comment
%type <node> attr_def_list
%type <node> attr_def_list_with_commas
%type <node> attr_def
%type <node> attr_constraint_def
%type <node> attr_index_def
%type <node> attr_def_one
%type <node> view_attr_def
%type <node> transaction_mode_list
%type <node> transaction_mode
%type <node> timeout_spec
%type <node> evaluate_stmt
%type <node> prepare_stmt
%type <node> execute_stmt
%type <node> opt_using
%type <node> execute_using_list;
%type <node> opt_priority
%type <node> opt_if_trigger_condition
%type <node> event_spec
%type <node> event_target
%type <node> trigger_condition
%type <node> trigger_action
%type <node> trigger_spec_list
%type <node> trace_spec
%type <node> depth_spec
%type <node> serial_start
%type <node> serial_increment
%type <node> opt_sp_param_list
%type <node> sp_param_list
%type <node> sp_param_def
%type <node> esql_query_stmt
%type <node> csql_query
%type <node> csql_query_select_has_no_with_clause
%type <node> csql_query_without_values_query
%type <node> select_expression_opt_with
%type <node> select_expression
%type <node> select_expression_without_values_query
%type <node> table_op
%type <node> select_or_subquery
%type <node> select_or_subquery_without_values_query
%type <node> select_stmt
%type <node> opt_with_clause
%type <node> opt_select_param_list
%type <node> opt_from_clause
%type <node> select_list
%type <node> alias_enabled_expression_list_top
%type <node> alias_enabled_expression_list
%type <node> alias_enabled_expression_
%type <node> expression_list
%type <node> to_param_list
%type <node> to_param
%type <node> from_param
%type <node> host_param_input
%type <node> host_param_output
%type <node> param_
%type <node> opt_where_clause
%type <node> startwith_clause
%type <node> connectby_clause
%type <node> opt_groupby_clause
%type <node> group_spec_list
%type <node> group_spec
%type <node> opt_having_clause
%type <node> index_name
%type <node> index_hint_name
%type <node> index_hint_name_keylimit
%type <node> opt_using_index_clause
%type <node> index_hint_name_list
%type <node> index_hint_name_keylimit_list
%type <node> opt_with_increment_clause
%type <node> opt_update_orderby_clause
%type <node> opt_orderby_clause
%type <node> sort_spec_list
%type <node> expression_
%type <node> normal_expression
%type <node> expression_strcat
%type <node> expression_add_sub
%type <node> expression_bitshift
%type <node> expression_bitand
%type <node> expression_bitor
%type <node> term
%type <node> factor
%type <node> factor_
%type <node> primary
%type <node> primary_w_collate
%type <node> boolean
%type <node> case_expr
%type <node> opt_else_expr
%type <node> simple_when_clause_list
%type <node> simple_when_clause
%type <node> searched_when_clause_list
%type <node> searched_when_clause
%type <node> extract_expr
%type <node> opt_expression_list
%type <node> table_set_function_call
%type <node> search_condition
%type <node> boolean_term
%type <node> boolean_term_is
%type <node> boolean_term_xor
%type <node> boolean_factor
%type <node> predicate
%type <node> predicate_expression
%type <node> predicate_expr_sub
%type <node> range_list
%type <node> range_
%type <node> subquery
%type <node> path_expression
%type <node> data_type_list
%type <node> opt_prec_1
%type <node> opt_padding
%type <node> signed_literal_
%type <node> literal_
%type <node> literal_w_o_param
%type <node> constant_set
%type <node> file_path_name
%type <node> identifier_list
%type <node> opt_bracketed_identifier_list
%type <node> index_column_identifier_list
%type <node> identifier_without_dot
%type <node> identifier
%type <node> index_column_identifier
%type <node> string_literal_or_input_hv
%type <node> escape_literal
%type <node> char_string_literal
%type <node> char_string
%type <node> bit_string_literal
%type <node> bit_string
%type <node> unsigned_integer
%type <node> unsigned_int32
%type <node> unsigned_real
%type <node> monetary_literal
%type <node> date_or_time_literal
%type <node> json_literal
%type <node> partition_clause
%type <node> partition_def_list
%type <node> partition_def
%type <node> signed_literal_list
%type <node> insert_name_clause
%type <node> replace_name_clause
%type <node> insert_name_clause_header
%type <node> opt_for_search_condition
%type <node> path_header
%type <node> path_id_list
%type <node> path_id
%type <node> simple_path_id
%type <node> simple_path_id_list
%type <node> generic_function
%type <node> opt_on_target
%type <node> generic_function_id
%type <node> pred_lhs
%type <node> pseudo_column
%type <node> reserved_func
%type <node> sort_spec
%type <node> trigger_priority
%type <node> class_or_normal_attr_def
%type <node> on_class_list
%type <node> from_id_list
%type <node> to_id_list
%type <node> only_class_name
%type <node> grant_head
%type <node> grant_cmd
%type <node> revoke_cmd
%type <node> opt_from_table_spec_list
%type <node> method_file_list
%type <node> incr_arg_name_list__inc
%type <node> incr_arg_name__inc
%type <node> incr_arg_name_list__dec
%type <node> incr_arg_name__dec
%type <node> search_condition_query
%type <node> search_condition_expression
%type <node> opt_select_limit_clause
%type <node> limit_options
%type <node> opt_upd_del_limit_clause
%type <node> truncate_stmt
%type <node> do_stmt
%type <node> on_duplicate_key_update
%type <node> opt_attr_ordering_info
%type <node> show_stmt
%type <node> session_variable;
%type <node> session_variable_assignment_list
%type <node> session_variable_assignment
%type <node> session_variable_definition
%type <node> session_variable_expression
%type <node> session_variable_list
%type <node> opt_analytic_partition_by
%type <node> opt_over_analytic_partition_by
%type <node> opt_analytic_order_by
%type <node> opt_table_spec_index_hint
%type <node> opt_table_spec_index_hint_list
%type <node> merge_stmt
%type <node> merge_update_insert_clause
%type <node> merge_update_clause
%type <node> merge_insert_clause
%type <node> opt_merge_delete_clause
%type <node> delete_name
%type <node> delete_name_list
%type <node> collation_spec
%type <node> charset_spec
%type <node> class_comment_spec
%type <node> class_encrypt_spec
%type <node> opt_vclass_comment_spec
%type <node> comment_value
%type <node> opt_comment_spec
%type <node> opt_collation
%type <node> opt_charset
%type <node> opt_using_charset
%type <node> values_query
%type <node> values_expression
%type <node> values_expr_item
%type <node> opt_partition_spec
%type <node> opt_for_update_clause
%type <node> of_or_where
%type <node> named_arg
%type <node> named_args
%type <node> opt_arg_value
%type <node> arg_value
%type <node> arg_value_list
%type <node> kill_stmt
%type <node> vacuum_stmt
%type <node> opt_owner_clause
%type <node> cte_definition_list
%type <node> cte_definition
%type <node> cte_query_list
%type <node> limit_expr
%type <node> limit_term
%type <node> limit_factor
%type <node> json_table_rule
%type <node> json_table_node_rule
%type <node> json_table_column_rule
%type <node> json_table_column_list_rule

%type <node> dblink_server_name
%type <node> server_identifier
%type <node> dblink_expr
%type <node> dblink_conn
%type <node> dblink_conn_str
%type <c2> dblink_identifier_col_attrs  
%type <node> dblink_column_definition_list
%type <node> dblink_column_definition
%type <c10> connect_info
%type <c2>  connect_item
%type <c10> alter_server_list
%type <c2>  alter_server_item
/*}}}*/

/* define rule type (cptr) */
/*{{{*/
%type <cptr> uint_text
%type <cptr> of_integer_real_literal
%type <cptr> integer_text
%type <cptr> json_schema
/*}}}*/

/* define rule type (container) */
/*{{{*/
%type <c10> opt_serial_option_list
%type <c10> serial_option_list

%type <c4> isolation_level_spec
%type <c4> opt_constraint_attr_list
%type <c4> constraint_attr_list
%type <c4> constraint_attr

%type <c3> ref_rule_list
%type <c3> opt_ref_rule_list
%type <c3> of_serial_option
%type <c3> delete_from_using
%type <c3> trigger_status_or_priority_or_change_owner

%type <c2> extended_table_spec_list
%type <c2> opt_startwith_connectby_clause
%type <c2> opt_of_where_cursor
%type <c2> opt_data_type
%type <c2> opt_create_as_clause
%type <c2> create_as_clause
%type <c2> serial_min
%type <c2> serial_max
%type <c2> of_cached_num
%type <c2> of_cycle_nocycle
%type <c2> data_type
%type <c2> primitive_type
%type <c2> opt_prec_2
%type <c2> in_pred_operand
%type <c2> opt_as_identifier_attr_name
%type <c2> insert_assignment_list
%type <c2> expression_queue
%type <c2> of_cast_data_type
/*}}}*/

/* define rule type (json_table_column_behavior) */
/*{{{*/
%type <jtcb> json_table_column_behavior_rule
%type <jtcb> json_table_on_error_rule_optional
%type <jtcb> json_table_on_empty_rule_optional
/*}}}*/

/* Token define */
/*{{{*/
%token ABSOLUTE_
%token ACTION
%token ADD
%token ADD_MONTHS
%token AFTER
%token ALL
%token ALLOCATE
%token ALTER
%token AND
%token ANY
%token ARE
%token AS
%token ASC
%token ASSERTION
%token ASYNC
%token AT
%token ATTACH
%token ATTRIBUTE
%token AVG
%token BEFORE
%token BEGIN_
%token BETWEEN
%token BIGINT
%token BINARY
%token BIT
%token BIT_LENGTH
%token BITSHIFT_LEFT
%token BITSHIFT_RIGHT
%token BLOB_
%token BOOLEAN_
%token BOTH_
%token BREADTH
%token BY
%token CALL
%token CASCADE
%token CASCADED
%token CASE
%token CAST
%token CATALOG
%token CHANGE
%token CHAR_
%token CHECK
%token CLASS
%token CLASSES
%token CLOB_
%token COALESCE
%token COLLATE
%token COLUMN
%token COMMIT
%token COMP_NULLSAFE_EQ
%token CONNECT
%token CONNECT_BY_ISCYCLE
%token CONNECT_BY_ISLEAF
%token CONNECT_BY_ROOT
%token CONNECTION
%token CONSTRAINT
%token CONSTRAINTS
%token CONTINUE
%token CONVERT
%token CORRESPONDING
%token COUNT
%token CREATE
%token CROSS
%token CURRENT
%token CURRENT_DATE
%token CURRENT_DATETIME
%token CURRENT_TIME
%token CURRENT_TIMESTAMP
%token CURRENT_USER
%token CURSOR
%token CYCLE
%token DATA
%token DATABASE
%token DATA_TYPE___
%token Date
%token DATETIME
%token DATETIMETZ
%token DATETIMELTZ
%token DAY_
%token DAY_MILLISECOND
%token DAY_SECOND
%token DAY_MINUTE
%token DAY_HOUR
%token DB_TIMEZONE
%token DEALLOCATE
%token DECLARE
%token DEFAULT
%token ON_UPDATE
%token DEFERRABLE
%token DEFERRED
%token DELETE_
%token DEPTH
%token DESC
%token DESCRIBE
%token DESCRIPTOR
%token DIAGNOSTICS
%token DIFFERENCE_
%token DISCONNECT
%token DISTINCT
%token DIV
%token DO
%token Domain
%token Double
%token DROP
%token DUPLICATE_
%token EACH
%token ELSE
%token ELSEIF
%token END
%token ENUM
%token EQUALS
%token ESCAPE
%token EVALUATE
%token EXCEPT
%token EXCEPTION
%token EXEC
%token EXECUTE
%token EXISTS
%token EXTERNAL
%token EXTRACT
%token False
%token FETCH
%token File
%token FIRST
%token FLOAT_
%token For
%token FORCE
%token FOREIGN
%token FOUND
%token FROM
%token FULL
%token FUNCTION
%token GENERAL
%token GET
%token GLOBAL
%token GO
%token GOTO
%token GRANT
%token GROUP_
%token HAVING
%token HOUR_
%token HOUR_MILLISECOND
%token HOUR_SECOND
%token HOUR_MINUTE
%token IDENTITY
%token IF
%token IGNORE_
%token IMMEDIATE
%token IN_
%token INDEX
%token INDICATOR
%token INHERIT
%token INITIALLY
%token INNER
%token INOUT
%token INPUT_
%token INSERT
%token INTEGER
%token INTERNAL
%token INTERSECT
%token INTERSECTION
%token INTERVAL
%token INTO
%token IS
%token ISOLATION
%token JOIN
%token JSON
%token KEY
%token KEYLIMIT
%token LANGUAGE
%token LAST
%token LEADING_
%token LEAVE
%token LEFT
%token LESS
%token LEVEL
%token LIKE
%token LIMIT
%token LIST
%token LOCAL
%token LOCAL_TRANSACTION_ID
%token LOCALTIME
%token LOCALTIMESTAMP
%token LOOP
%token LOWER
%token MATCH
%token MATCHED
%token Max
%token MERGE
%token METHOD
%token MILLISECOND_
%token Min
%token MINUTE_
%token MINUTE_MILLISECOND
%token MINUTE_SECOND
%token MOD
%token MODIFY
%token MODULE
%token Monetary
%token MONTH_
%token MULTISET
%token MULTISET_OF
%token NA
%token NAMES
%token NATIONAL
%token NATURAL
%token NCHAR
%token NEXT
%token NO
%token NOT
%token Null
%token NULLIF
%token NUMERIC
%token OBJECT
%token OCTET_LENGTH
%token OF
%token OFF_
%token ON_
%token ONLY
%token OPTIMIZATION
%token OPTION
%token OR
%token ORDER
%token OUT_
%token OUTER
%token OUTPUT
%token OVER
%token OVERLAPS
%token PARAMETERS
%token PARTIAL
%token PARTITION
%token POSITION
%token PRECISION
%token PREPARE
%token PRESERVE
%token PRIMARY
%token PRIOR
%token PRIVILEGES
%token PROCEDURE
%token PROMOTE
%token QUERY
%token READ
%token REBUILD
%token RECURSIVE
%token REF
%token REFERENCES
%token REFERENCING
%token REGEXP
%token RELATIVE_
%token RENAME
%token REPLACE
%token RESIGNAL
%token RESTRICT
%token RETURN
%token RETURNS
%token REVOKE
%token RIGHT
%token RLIKE
%token ROLE
%token ROLLBACK
%token ROLLUP
%token ROUTINE
%token ROW
%token ROWNUM
%token ROWS
%token SAVEPOINT
%token SCHEMA
%token SCOPE
%token SCROLL
%token SEARCH
%token SECOND_
%token SECOND_MILLISECOND
%token SECTION
%token SELECT
%token SENSITIVE
%token SEQUENCE
%token SEQUENCE_OF
%token SERIALIZABLE
%token SESSION
%token SESSION_TIMEZONE
%token SESSION_USER
%token SET
%token SET_OF
%token SETEQ
%token SETNEQ
%token SHARED
%token SIBLINGS
%token SIGNAL
%token SIMILAR
%token SIZE_
%token SmallInt
%token SOME
%token SQL
%token SQLCODE
%token SQLERROR
%token SQLEXCEPTION
%token SQLSTATE
%token SQLWARNING
%token STATISTICS
%token String
%token SUBCLASS
%token SUBSET
%token SUBSETEQ
%token SUBSTRING_
%token SUM
%token SUPERCLASS
%token SUPERSET
%token SUPERSETEQ
%token SYS_CONNECT_BY_PATH
%token SYS_DATE
%token SYS_DATETIME
%token SYS_TIME_
%token SYS_TIMESTAMP
%token SYSTEM_USER
%token TABLE
%token TEMPORARY
%token THEN
%token Time
%token TIMESTAMP
%token TIMESTAMPTZ
%token TIMESTAMPLTZ
%token TIMEZONES
%token TIMEZONE_HOUR
%token TIMEZONE_MINUTE
%token TO
%token TRAILING_
%token TRANSACTION
%token TRANSLATE
%token TRANSLATION
%token TRIGGER
%token TRIM
%token True
%token TRUNCATE
%token UNDER
%token Union
%token UNIQUE
%token UNKNOWN
%token UNTERMINATED_STRING
%token UNTERMINATED_IDENTIFIER
%token UPDATE
%token UPPER
%token USAGE
%token USE
%token USER
%token USING
%token Utime
%token VACUUM
%token VALUE
%token VALUES
%token VAR_ASSIGN
%token VARCHAR
%token VARIABLE_
%token VARYING
%token VCLASS
%token VIEW
%token WHEN
%token WHENEVER
%token WHERE
%token WHILE
%token WITH
%token WITHOUT
%token WORK
%token WRITE
%token XOR
%token YEAR_
%token YEAR_MONTH
%token ZONE

%token YEN_SIGN
%token DOLLAR_SIGN
%token WON_SIGN
%token TURKISH_LIRA_SIGN
%token BRITISH_POUND_SIGN
%token CAMBODIAN_RIEL_SIGN
%token CHINESE_RENMINBI_SIGN
%token INDIAN_RUPEE_SIGN
%token RUSSIAN_RUBLE_SIGN
%token AUSTRALIAN_DOLLAR_SIGN
%token CANADIAN_DOLLAR_SIGN
%token BRASILIAN_REAL_SIGN
%token ROMANIAN_LEU_SIGN
%token EURO_SIGN
%token SWISS_FRANC_SIGN
%token DANISH_KRONE_SIGN
%token NORWEGIAN_KRONE_SIGN
%token BULGARIAN_LEV_SIGN
%token VIETNAMESE_DONG_SIGN
%token CZECH_KORUNA_SIGN
%token POLISH_ZLOTY_SIGN
%token SWEDISH_KRONA_SIGN
%token CROATIAN_KUNA_SIGN
%token SERBIAN_DINAR_SIGN

%token DOT
%token RIGHT_ARROW
%token DOUBLE_RIGHT_ARROW
%token STRCAT
%token COMP_NOT_EQ
%token COMP_GE
%token COMP_LE
%token PARAM_HEADER

%token <cptr> ACCESS
%token <cptr> ACTIVE
%token <cptr> ADDDATE
%token <cptr> AES
%token <cptr> ANALYZE
%token <cptr> ARCHIVE
%token <cptr> ARIA
%token <cptr> AUTO_INCREMENT
%token <cptr> BENCHMARK
%token <cptr> BIT_AND
%token <cptr> BIT_OR
%token <cptr> BIT_XOR
%token <cptr> BUFFER
%token <cptr> CACHE
%token <cptr> CAPACITY
%token <cptr> CHARACTER_SET_
%token <cptr> CHARSET
%token <cptr> CHR
%token <cptr> CLOB_TO_CHAR
%token <cptr> CLOSE
%token <cptr> COLLATION
%token <cptr> COLUMNS
%token <cptr> COMMENT
%token <cptr> COMMITTED
%token <cptr> COST
%token <cptr> CRITICAL
%token <cptr> CUME_DIST
%token <cptr> DATE_ADD
%token <cptr> DATE_SUB
%token <cptr> DBLINK
%token <cptr> DBNAME
%token <cptr> DECREMENT
%token <cptr> DENSE_RANK
%token <cptr> DONT_REUSE_OID
%token <cptr> ELT
%token <cptr> EMPTY
%token <cptr> ENCRYPT
%token <cptr> ERROR_
%token <cptr> EXPLAIN
%token <cptr> FIRST_VALUE
%token <cptr> FULLSCAN
%token <cptr> GE_INF_
%token <cptr> GE_LE_
%token <cptr> GE_LT_
%token <cptr> GRANTS
%token <cptr> GROUP_CONCAT
%token <cptr> GROUPS
%token <cptr> GT_INF_
%token <cptr> GT_LE_
%token <cptr> GT_LT_
%token <cptr> HASH
%token <cptr> HEADER
%token <cptr> HEAP
%token <cptr> HOST
%token <cptr> IFNULL
%token <cptr> INACTIVE
%token <cptr> INCREMENT
%token <cptr> INDEXES
%token <cptr> INDEX_PREFIX
%token <cptr> INF_LE_
%token <cptr> INF_LT_
%token <cptr> INFINITE_
%token <cptr> INSTANCES
%token <cptr> INVALIDATE
%token <cptr> INVISIBLE
%token <cptr> ISNULL
%token <cptr> KEYS
%token <cptr> KILL
%token <cptr> JAVA
%token <cptr> JSON_ARRAYAGG
%token <cptr> JSON_ARRAY_LEX
%token <cptr> JSON_ARRAY_APPEND
%token <cptr> JSON_ARRAY_INSERT
%token <cptr> JSON_CONTAINS
%token <cptr> JSON_CONTAINS_PATH
%token <cptr> JSON_DEPTH
%token <cptr> JSON_EXTRACT
%token <cptr> JSON_GET_ALL_PATHS
%token <cptr> JSON_INSERT
%token <cptr> JSON_KEYS
%token <cptr> JSON_LENGTH
%token <cptr> JSON_MERGE
%token <cptr> JSON_MERGE_PATCH
%token <cptr> JSON_MERGE_PRESERVE
%token <cptr> JSON_OBJECTAGG
%token <cptr> JSON_OBJECT_LEX
%token <cptr> JSON_PRETTY
%token <cptr> JSON_QUOTE
%token <cptr> JSON_REMOVE
%token <cptr> JSON_REPLACE
%token <cptr> JSON_SEARCH
%token <cptr> JSON_SET
%token <cptr> JSON_TABLE
%token <cptr> JSON_TYPE
%token <cptr> JSON_UNQUOTE
%token <cptr> JSON_VALID
%token <cptr> JOB
%token <cptr> LAG
%token <cptr> LAST_VALUE
%token <cptr> LCASE
%token <cptr> LEAD
%token <cptr> LOCK_
%token <cptr> LOG
%token <cptr> MAXIMUM
%token <cptr> MAXVALUE
%token <cptr> MEDIAN
%token <cptr> MEMBERS
%token <cptr> MINVALUE
%token <cptr> NAME
%token <cptr> NESTED
%token <cptr> NOCYCLE
%token <cptr> NOCACHE
%token <cptr> NOMAXVALUE
%token <cptr> NOMINVALUE
%token <cptr> NONE
%token <cptr> NTH_VALUE
%token <cptr> NTILE
%token <cptr> NULLS
%token <cptr> OFFSET
%token <cptr> ONLINE
%token <cptr> OPEN
%token <cptr> ORDINALITY
%token <cptr> PATH
%token <cptr> OWNER
%token <cptr> PAGE
%token <cptr> PARALLEL
%token <cptr> PARTITIONING
%token <cptr> PARTITIONS
%token <cptr> PASSWORD
%token <cptr> PERCENT_RANK
%token <cptr> PERCENTILE_CONT
%token <cptr> PERCENTILE_DISC
%token <cptr> PORT
%token <cptr> PRINT
%token <cptr> PRIORITY
%token <cptr> PROPERTIES
%token <cptr> QUARTER
%token <cptr> QUEUES
%token <cptr> RANGE_
%token <cptr> RANK
%token <cptr> REGEXP_COUNT
%token <cptr> REGEXP_INSTR
%token <cptr> REGEXP_LIKE
%token <cptr> REGEXP_REPLACE
%token <cptr> REGEXP_SUBSTR
%token <cptr> REJECT_
%token <cptr> REMOVE
%token <cptr> REORGANIZE
%token <cptr> REPEATABLE
%token <cptr> RESPECT
%token <cptr> RETAIN
%token <cptr> REUSE_OID
%token <cptr> REVERSE
%token <cptr> DISK_SIZE
%token <cptr> ROW_NUMBER
%token <cptr> SECTIONS
%token <cptr> SEPARATOR
%token <cptr> SERIAL
%token <cptr> SERVER
%token <cptr> SHOW
%token <cptr> SLOTS
%token <cptr> SLOTTED
%token <cptr> STABILITY
%token <cptr> START_
%token <cptr> STATEMENT
%token <cptr> STATUS
%token <cptr> STDDEV
%token <cptr> STDDEV_POP
%token <cptr> STDDEV_SAMP
%token <cptr> STR_TO_DATE
%token <cptr> SUBDATE
%token <cptr> SYSTEM
%token <cptr> TABLES
%token <cptr> TEXT
%token <cptr> THAN
%token <cptr> THREADS
%token <cptr> TIMEOUT
%token <cptr> TIMEZONE
%token <cptr> TRACE
%token <cptr> TRAN
%token <cptr> TRIGGERS
%token <cptr> UCASE
%token <cptr> UNCOMMITTED
%token <cptr> VAR_POP
%token <cptr> VAR_SAMP
%token <cptr> VARIANCE
%token <cptr> VISIBLE
%token <cptr> VOLUME
%token <cptr> WEEK
%token <cptr> WITHIN
%token <cptr> WORKSPACE


%token <cptr> IdName
%token <cptr> BracketDelimitedIdName
%token <cptr> BacktickDelimitedIdName
%token <cptr> DelimitedIdName
%token <cptr> UNSIGNED_INTEGER
%token <cptr> UNSIGNED_REAL
%token <cptr> CHAR_STRING
%token <cptr> NCHAR_STRING
%token <cptr> BIT_STRING
%token <cptr> HEX_STRING
%token <cptr> CPP_STYLE_HINT
%token <cptr> C_STYLE_HINT
%token <cptr> SQL_STYLE_HINT
%token <cptr> BINARY_STRING
%token <cptr> EUCKR_STRING
%token <cptr> ISO_STRING
%token <cptr> UTF8_STRING
%token <cptr> IPV4_ADDRESS

/*}}}*/

%%

stmt_done
	: stmt_list { DBG_PRINT_TOKEN_END();}
	| /* empty */
	;

stmt_list
	: stmt_list ';' %dprec 1
                {{ /* empty line*/ }}
        | stmt_list ';' stmt %dprec 2
		{{

			if ($3 != NULL)
			  {
			    if (parser_statement_OK)
			      {
			        this_parser->statement_number++;
			      }
			    else
			      {
			        parser_statement_OK = 1;
			      }

			    pt_push (this_parser, $3);

			#ifdef PARSER_DEBUG
			    printf ("node: %s\n", parser_print_tree (this_parser, $3));
			#endif
			  }

		DBG_PRINT}}
	| stmt %dprec 3
		{{

			if ($1 != NULL)
			  {
			    if (parser_statement_OK)
			      {
				this_parser->statement_number++;
			      }
			    else
			      {
			        parser_statement_OK = 1;
			      }

			    pt_push (this_parser, $1);

			#ifdef PARSER_DEBUG
			    printf ("node: %s\n", parser_print_tree (this_parser, $1));
			#endif
                        printf ("node: %s\n", parser_print_tree (this_parser, $1));
			  }

		DBG_PRINT}}
        | ';'
                {{ /* empty line*/ }}
	;



stmt
	:
		{{ DBG_TRACE_GRAMMAR(stmt, : );
			msg_ptr = 0;

			if (this_parser->original_buffer)
			  {
			    int pos = @$.buffer_pos;
			    int stmt_n = this_parser->statement_number;

			    if (g_original_buffer_len == 0)
			      {
				g_original_buffer_len = strlen (this_parser->original_buffer);
			      }

			    /* below assert & defence code is for yybuffer_pos mismatch
			     * (like unput in lexer and do not modify yybuffer_pos)
			     */
			    assert (pos <= g_original_buffer_len);

			    if (pos > g_original_buffer_len)
			      {
				pos = g_original_buffer_len;
			      }

			    g_query_string = (char*) (this_parser->original_buffer + pos);

			    while (char_isspace (*g_query_string))
			      {
			        g_query_string++;
			      }
			  }

		DBG_PRINT}}
		{{

			parser_initialize_parser_context ();

			parser_statement_OK = 1;
			parser_instnum_check = 0;
			parser_groupbynum_check = 0;
			parser_orderbynum_check = 0;

			parser_sysconnectbypath_check = 0;
			parser_prior_check = 0;
			parser_connectbyroot_check = 0;
			parser_serial_check = 1;
			parser_subquery_check = 1;
			parser_pseudocolumn_check = 1;
			parser_hostvar_check = 1;

			parser_select_level = -1;

			parser_within_join_condition = 0;
			parser_found_Oracle_outer = false;

			parser_save_and_set_si_datetime (false);
			parser_save_and_set_si_tran_id (false);
			parser_save_and_set_cannot_prepare (false);

			parser_attr_type = PT_NORMAL;
			allow_attribute_ordering = false;
			parser_hidden_incr_list = NULL;

		DBG_PRINT}}
	stmt_
		{{ DBG_TRACE_GRAMMAR(stmt, stmt_ );

			#ifdef PARSER_DEBUG
			if (msg_ptr == 0)
			  printf ("Good!!!\n");
			#endif

			if (msg_ptr > 0)
			  {
			    csql_yyerror (NULL);
			  }

			/* set query length of last statement (but do it every time)
                         * Not last statement's length will be updated later.
                         */
			if (this_parser->original_buffer)
			  {
			    int pos = @$.buffer_pos;
			    PT_NODE *node = $3;

			    if (g_original_buffer_len == 0)
			      {
				g_original_buffer_len = strlen (this_parser->original_buffer);
			      }

			    /* below assert & defence code is for yybuffer_pos mismatch
			     * (like unput in lexer and do not modify yybuffer_pos)
			     */
			    assert (pos <= g_original_buffer_len);

			    if (pos > g_original_buffer_len)
			      {
				pos = g_original_buffer_len;
			      }

			    if (node)
			      {
				const char *curr_ptr = this_parser->original_buffer + pos;
				int len = (int) (curr_ptr - g_query_string);
				node->sql_user_text_len = len;
				g_query_string_len = len;
			      }
			  }

		DBG_PRINT}}
		{{

			PT_NODE *node = $3;

			if (node)
			  {
			    node->flag.si_datetime = (parser_si_datetime == true) ? 1 : 0;
			    node->flag.si_tran_id = (parser_si_tran_id == true) ? 1 : 0;
			    node->flag.cannot_prepare = (parser_cannot_prepare == true) ? 1 : 0;
			  }

			parser_restore_si_datetime ();
			parser_restore_si_tran_id ();
			parser_restore_cannot_prepare ();

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;
stmt_
	: create_stmt
		{ DBG_TRACE_GRAMMAR(stmt_, : create_stmt);
                  $$ = $1; }
	| alter_stmt
		{ DBG_TRACE_GRAMMAR(stmt_, | alter_stmt);
                  $$ = $1; }
	| rename_stmt
		{ DBG_TRACE_GRAMMAR(stmt_, | rename_stmt);
                  $$ = $1; }
	| update_statistics_stmt
		{ DBG_TRACE_GRAMMAR(stmt_, | update_statstics_stmt);
                  $$ = $1; }
	| drop_stmt
		{ DBG_TRACE_GRAMMAR(stmt_, | drop_stmt);
                  $$ = $1; }
	| do_stmt
		{ DBG_TRACE_GRAMMAR(stmt_, | do_stmt);
                  $$ = $1; }
	| opt_with_clause
	  esql_query_stmt
		{{ DBG_TRACE_GRAMMAR(stmt_, | opt_with_clause esql_query_stmt);
			PT_NODE *with_clause = $1;
			PT_NODE *stmt = $2;
			if (stmt && with_clause)
			  {
			    stmt->info.query.with = with_clause;
			  }
			$$ = stmt;
	  DBG_PRINT}}
	| evaluate_stmt
		{ DBG_TRACE_GRAMMAR(stmt_, | evaluate_stmt);
                  $$ = $1; }
	| prepare_stmt
		{ DBG_TRACE_GRAMMAR(stmt_, | prepare_stmt);
                  $$ = $1; }
	| execute_stmt
		{ DBG_TRACE_GRAMMAR(stmt_, | execute_stmt);
                  $$ = $1; }
	| insert_or_replace_stmt
		{ DBG_TRACE_GRAMMAR(stmt_, | insert_or_replace_stmt);
                  $$ = $1; }
	| opt_with_clause
	  update_stmt
		{{ DBG_TRACE_GRAMMAR(stmt_, | opt_with_clause update_stmt);
			PT_NODE *with_clause = $1;
			PT_NODE *stmt = $2;
			if (stmt && with_clause)
			  {
			    stmt->info.update.with = with_clause;
			  }
			$$ = stmt;
	  DBG_PRINT}}
	| opt_with_clause
	  delete_stmt
		{{ DBG_TRACE_GRAMMAR(stmt_, | opt_with_clause delete_stmt);
			PT_NODE *with_clause = $1;
			PT_NODE *stmt = $2;
			if (stmt && with_clause)
			  {
			    stmt->info.delete_.with = with_clause;
			  }
			$$ = stmt;
	  DBG_PRINT}}
	| show_stmt
		{ DBG_TRACE_GRAMMAR(stmt_, | show_stmt);
                  $$ = $1; }
	| call_stmt
		{ DBG_TRACE_GRAMMAR(stmt_, | call_stmt);
                  $$ = $1; }
	| auth_stmt
		{ DBG_TRACE_GRAMMAR(stmt_, | auth_stmt);
                  $$ = $1; }
	| transaction_stmt
		{ DBG_TRACE_GRAMMAR(stmt_, | transaction_stmt);
                  $$ = $1; }
	| truncate_stmt
		{ DBG_TRACE_GRAMMAR(stmt_, | truncate_stmt);
                  $$ = $1; }
	| merge_stmt
		{ DBG_TRACE_GRAMMAR(stmt_, | merge_stmt);
                  $$ = $1; }
	| set_stmt
		{ DBG_TRACE_GRAMMAR(stmt_, | set_stmt);
                  $$ = $1; }
	| get_stmt
		{ DBG_TRACE_GRAMMAR(stmt_, | get_stmt);
                  $$ = $1; }
	| kill_stmt
		{ DBG_TRACE_GRAMMAR(stmt_, | kill_stmt);
                  $$ = $1; }
	| DATA_TYPE___ data_type
		{{ DBG_TRACE_GRAMMAR(stmt_, | DATA_TYPE___ data_type);

			PT_NODE *dt, *set_dt;
			PT_TYPE_ENUM typ;

			typ = (PT_TYPE_ENUM) TO_NUMBER (CONTAINER_AT_0 ($2));
			dt = CONTAINER_AT_1 ($2);

			if (!dt)
			  {
			    dt = parser_new_node (this_parser, PT_DATA_TYPE);
			    if (dt)
			      {
				dt->type_enum = typ;
				dt->data_type = NULL;
			      }
			  }
			else
			  {
			    if ((typ == PT_TYPE_SET) ||
				(typ == PT_TYPE_MULTISET) || (typ == PT_TYPE_SEQUENCE))
			      {
				set_dt = parser_new_node (this_parser, PT_DATA_TYPE);
				if (set_dt)
				  {
				    set_dt->type_enum = typ;
				    set_dt->data_type = dt;
				    dt = set_dt;
				  }
			      }
			  }

			if (PT_HAS_COLLATION (typ))
			  {
			    dt->info.data_type.units = LANG_SYS_CODESET;
			    dt->info.data_type.collation_id = LANG_SYS_COLLATION;
			  }

			$$ = dt;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| ATTACH
		{ push_msg(MSGCAT_SYNTAX_INVALID_ATTACH); }
	  unsigned_integer
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(stmt_, | ATTACH unsigned_integer);

			PT_NODE *node = parser_new_node (this_parser, PT_2PC_ATTACH);

			if (node)
			  {
			    node->info.attach.trans_id = $3->info.value.data_value.i;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| PREPARE
		{ push_msg(MSGCAT_SYNTAX_INVALID_PREPARE); }
	  opt_to COMMIT unsigned_integer
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(stmt_, | PREPARE opt_to COMMIT unsigned_integer);

			PT_NODE *node = parser_new_node (this_parser, PT_PREPARE_TO_COMMIT);

			if (node)
			  {
			    node->info.prepare_to_commit.trans_id = $5->info.value.data_value.i;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| EXECUTE
		{ push_msg(MSGCAT_SYNTAX_INVALID_EXECUTE); }
	  DEFERRED TRIGGER trigger_spec_list
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(stmt_, | EXECUTE DEFERRED TRIGGER trigger_spec_list);

			PT_NODE *node = parser_new_node (this_parser, PT_EXECUTE_TRIGGER);

			if (node)
			  {
			    node->info.execute_trigger.trigger_spec_list = $5;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| SCOPE
		{ push_msg(MSGCAT_SYNTAX_INVALID_SCOPE); }
	  trigger_action opt_from_table_spec_list
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(stmt_, | SCOPE trigger_action opt_from_table_spec_list);

			PT_NODE *node = parser_new_node (this_parser, PT_SCOPE);

			if (node)
			  {
			    node->info.scope.stmt = $3;
			    node->info.scope.from = $4;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| vacuum_stmt
		{ DBG_TRACE_GRAMMAR(stmt_, | vacuum_stmt);
                  $$ = $1; }

	| SET TIMEZONE
		{ push_msg(MSGCAT_SYNTAX_INVALID_SET_TIMEZONE); }
	  char_string_literal
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(stmt_, | SET TIMEZONE char_string_literal);

			PT_NODE *node = parser_new_node (this_parser, PT_SET_TIMEZONE);
			if (node)
			  {
			    node->info.set_timezone.timezone_node = $4;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| SET Time ZONE
		{ push_msg(MSGCAT_SYNTAX_INVALID_SET_TIMEZONE); }
	  char_string_literal
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(stmt_, | SET Time ZONE char_string_literal);
			PT_NODE *node = parser_new_node (this_parser, PT_SET_TIMEZONE);
			if (node)
			  {
			    node->info.set_timezone.timezone_node = $5;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;


opt_from_table_spec_list
	: /* empty */
		{ DBG_TRACE_GRAMMAR(opt_from_table_spec_list, : );
                  $$ = NULL; }
	| FROM ON_ table_spec_list
		{ DBG_TRACE_GRAMMAR(opt_from_table_spec_list, | FROM ON_ table_spec_list);
                  $$ = $3; }
	;


set_stmt
	: SET OPTIMIZATION
		{ push_msg(MSGCAT_SYNTAX_INVALID_SET_OPT_LEVEL); }
	  LEVEL opt_of_to_eq opt_level_spec
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(set_stmt, : SET OPTIMIZATION LEVEL opt_of_to_eq opt_level_spec);
			PT_NODE *node = parser_new_node (this_parser, PT_SET_OPT_LVL);
			if (node)
			  {
			    node->info.set_opt_lvl.option = PT_OPT_LVL;
			    node->info.set_opt_lvl.val = $6;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| SET OPTIMIZATION
		{ push_msg(MSGCAT_SYNTAX_INVALID_SET_OPT_COST); }
	  COST opt_of char_string_literal opt_of_to_eq literal_
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(set_stmt, | SET OPTIMIZATION COST opt_of char_string_literal opt_of_to_eq literal_);

			PT_NODE *node = parser_new_node (this_parser, PT_SET_OPT_LVL);
			if (node)
			  {
			    node->info.set_opt_lvl.option = PT_OPT_COST;
			    if ($6)
			      ($6)->next = $8;
			    node->info.set_opt_lvl.val = $6;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| SET
		{ push_msg(MSGCAT_SYNTAX_INVALID_SET_SYS_PARAM); }
	  SYSTEM PARAMETERS char_string_literal_list
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(set_stmt, | SET SYSTEM PARAMETERS char_string_literal_list);

			PT_NODE *node = parser_new_node (this_parser, PT_SET_SYS_PARAMS);
			if (node)
			  node->info.set_sys_params.val = $5;
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| SET
		{ push_msg(MSGCAT_SYNTAX_INVALID_SET_TRAN); }
	  TRANSACTION transaction_mode_list
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(set_stmt, | SET TRANSACTION transaction_mode_list);

			PT_NODE *node = parser_new_node (this_parser, PT_SET_XACTION);

			if (node)
			  {
			    node->info.set_xaction.xaction_modes = $4;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| SET TRIGGER
		{ push_msg(MSGCAT_SYNTAX_INVALID_SET_TRIGGER_TRACE); }
	  TRACE trace_spec
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(set_stmt, | SET TRIGGER TRACE trace_spec);

			PT_NODE *node = parser_new_node (this_parser, PT_SET_TRIGGER);

			if (node)
			  {
			    node->info.set_trigger.option = PT_TRIGGER_TRACE;
			    node->info.set_trigger.val = $5;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| SET TRIGGER
		{ push_msg(MSGCAT_SYNTAX_INVALID_SET_TRIGGER_DEPTH); }
	  opt_maximum DEPTH depth_spec
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(set_stmt, | SET TRIGGER opt_maximum DEPTH depth_spec);

			PT_NODE *node = parser_new_node (this_parser, PT_SET_TRIGGER);

			if (node)
			  {
			    node->info.set_trigger.option = PT_TRIGGER_DEPTH;
			    node->info.set_trigger.val = $6;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| SET session_variable_assignment_list
		{{ DBG_TRACE_GRAMMAR(set_stmt, | SET session_variable_assignment_list);

			PT_NODE *node =
				parser_new_node (this_parser, PT_SET_SESSION_VARIABLES);
			if (node)
			  {
				node->info.set_variables.assignments = $2;
			  }
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| SET NAMES BINARY
		{{ DBG_TRACE_GRAMMAR(set_stmt, | SET NAMES BINARY);

			PT_NODE *node = NULL;
			PT_NODE *charset_node = NULL;

			charset_node = parser_new_node (this_parser, PT_VALUE);

			if (charset_node)
			  {
			    charset_node->type_enum = PT_TYPE_CHAR;
			    charset_node->info.value.string_type = ' ';
			    charset_node->info.value.data_value.str =
			      pt_append_bytes (this_parser, NULL, "binary", strlen ("binary"));
			    PT_NODE_PRINT_VALUE_TO_TEXT (this_parser, charset_node);
			  }

			node = parser_new_node (this_parser, PT_SET_NAMES);
			if (node)
			  {
			    node->info.set_names.charset_node = charset_node;
			    node->info.set_names.collation_node = NULL;
			  }
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| SET
		{ push_msg(MSGCAT_SYNTAX_INVALID_SET_NAMES); }
	  NAMES char_string_literal
	  opt_collation
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(set_stmt, | SET NAMES char_string_literal opt_collation);

			PT_NODE *node = parser_new_node (this_parser, PT_SET_NAMES);
			if (node)
			  {
			    node->info.set_names.charset_node = $4;
			    node->info.set_names.collation_node = $5;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| SET
		{ push_msg(MSGCAT_SYNTAX_INVALID_SET_NAMES); }
	  NAMES IdName
	  opt_collation
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(set_stmt, | SET NAMES IdName opt_collation);

			PT_NODE *node = NULL;
			PT_NODE *charset_node = NULL;

			charset_node = parser_new_node (this_parser, PT_VALUE);

			if (charset_node)
			  {
			    charset_node->type_enum = PT_TYPE_CHAR;
			    charset_node->info.value.string_type = ' ';
			    charset_node->info.value.data_value.str =
			      pt_append_bytes (this_parser, NULL, $4, strlen ($4));
			    PT_NODE_PRINT_VALUE_TO_TEXT (this_parser, charset_node);
			  }

			node = parser_new_node (this_parser, PT_SET_NAMES);
			if (node)
			  {
			    node->info.set_names.charset_node = charset_node;
			    node->info.set_names.collation_node = $5;
			  }
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| SET TRACE query_trace_spec opt_trace_output_format
		{{ DBG_TRACE_GRAMMAR(set_stmt, | SET TRACE query_trace_spec opt_trace_output_format);

			PT_NODE *node = parser_new_node (this_parser, PT_QUERY_TRACE);

			if (node)
			  {
			    node->info.trace.on_off = $3;
			    node->info.trace.format = $4;
			  }

			$$ = node;

		DBG_PRINT}}
	;

query_trace_spec
	: ON_
		{{ DBG_TRACE_GRAMMAR(query_trace_spec, : ON_);
			$$ = PT_TRACE_ON;
		DBG_PRINT}}
	| OFF_
		{{ DBG_TRACE_GRAMMAR(query_trace_spec, | OFF_);
			$$ = PT_TRACE_OFF;
		DBG_PRINT}}
	;

opt_trace_output_format
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_trace_output_format, : );
			$$ = PT_TRACE_FORMAT_TEXT;
		DBG_PRINT}}
	| OUTPUT TEXT
		{{ DBG_TRACE_GRAMMAR(opt_trace_output_format, | OUTPUT TEXT);
			$$ = PT_TRACE_FORMAT_TEXT;
		DBG_PRINT}}
	| OUTPUT JSON
		{{ DBG_TRACE_GRAMMAR(opt_trace_output_format, | OUTPUT JSON);
			$$ = PT_TRACE_FORMAT_JSON;
		DBG_PRINT}}
	;

session_variable_assignment_list
	: session_variable_assignment_list ',' session_variable_assignment
		{{ DBG_TRACE_GRAMMAR(session_variable_assignment_list, : session_variable_assignment_list ',' session_variable_assignment);

			$$ = parser_make_link ($1, $3);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}

	| session_variable_assignment
		{{ DBG_TRACE_GRAMMAR(session_variable_assignment_list, | session_variable_assignment);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

session_variable_assignment
	: session_variable '=' expression_
		{{ DBG_TRACE_GRAMMAR(session_variable_assignment, : session_variable '=' expression_);

			PT_NODE* expr =
				parser_make_expression (this_parser, PT_DEFINE_VARIABLE, $1, $3, NULL);
			expr->flag.do_not_fold = 1;
			$$ = expr;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| session_variable_definition
		{{ DBG_TRACE_GRAMMAR(session_variable_assignment, | session_variable_definition);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

session_variable_definition
	: session_variable VAR_ASSIGN expression_
		{{ DBG_TRACE_GRAMMAR(session_variable_definition, : session_variable VAR_ASSIGN expression_);

			PT_NODE* expr =
				parser_make_expression (this_parser, PT_DEFINE_VARIABLE, $1, $3, NULL);
			expr->flag.do_not_fold = 1;
			$$ = expr;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

session_variable_expression
	: session_variable
		{{ DBG_TRACE_GRAMMAR(session_variable_expression, : session_variable);

			PT_NODE *expr = NULL;
			expr = parser_make_expression (this_parser, PT_EVALUATE_VARIABLE, $1, NULL,
										   NULL);
			expr->flag.do_not_fold = 1;
			$$ = expr;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

session_variable_list
	: session_variable_list ',' session_variable
		{{ DBG_TRACE_GRAMMAR(session_variable_list, : session_variable_list ',' session_variable);

			$$ = parser_make_link ($1, $3);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| session_variable
		{{ DBG_TRACE_GRAMMAR(session_variable_list, | session_variable);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

session_variable
	: '@' identifier
		{{ DBG_TRACE_GRAMMAR(session_variable, : '@' identifier);

			PT_NODE *node = parser_new_node (this_parser, PT_VALUE);
			PT_NODE *id = $2;

			if (node != NULL && id != NULL)
			  {
			    node->type_enum = PT_TYPE_CHAR;
			    node->info.value.string_type = ' ';
			    node->info.value.data_value.str =
			      pt_append_bytes (this_parser, NULL,
			      				   id->info.name.original,
			      				   strlen (id->info.name.original));
			    node->info.value.text = (const char *) node->info.value.data_value.str->bytes;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

get_stmt
	: GET
		{ push_msg(MSGCAT_SYNTAX_INVALID_GET_STAT); }
	  STATISTICS char_string_literal  OF class_name into_clause_opt
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(get_stmt, : GET STATISTICS char_string_literal  OF class_name into_clause_opt);

			PT_NODE *node = parser_new_node (this_parser, PT_GET_STATS);
			if (node)
			  {
			    node->info.get_stats.into_var = $7;
			    node->info.get_stats.class_ = $6;
			    node->info.get_stats.args = $4;
			  }
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| GET OPTIMIZATION
		{ push_msg(MSGCAT_SYNTAX_INVALID_GET_OPT_LEVEL); }
	  LEVEL into_clause_opt
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(get_stmt, | GET OPTIMIZATION LEVEL into_clause_opt);

			PT_NODE *node = parser_new_node (this_parser, PT_GET_OPT_LVL);
			if (node)
			  {
			    node->info.get_opt_lvl.into_var = $5;
			    node->info.get_opt_lvl.option = PT_OPT_LVL;
			    node->info.get_opt_lvl.args = NULL;
			  }
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| GET OPTIMIZATION
		{ push_msg(MSGCAT_SYNTAX_INVALID_GET_OPT_COST); }
	  COST opt_of char_string_literal into_clause_opt
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(get_stmt, | GET OPTIMIZATION COST opt_of char_string_literal into_clause_opt);

			PT_NODE *node = parser_new_node (this_parser, PT_GET_OPT_LVL);
			if (node)
			  {
			    node->info.get_opt_lvl.into_var = $7;
			    node->info.get_opt_lvl.option = PT_OPT_COST;
			    node->info.get_opt_lvl.args = $6;
			  }
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| GET TRANSACTION
		{ push_msg(MSGCAT_SYNTAX_INVALID_GET_TRAN_ISOL); }
	  ISOLATION LEVEL into_clause_opt
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(get_stmt, | GET TRANSACTION ISOLATION LEVEL into_clause_opt);

			PT_NODE *node = parser_new_node (this_parser, PT_GET_XACTION);

			if (node)
			  {
			    node->info.get_xaction.into_var = $6;
			    node->info.get_xaction.option = PT_ISOLATION_LEVEL;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| GET TRANSACTION
		{ push_msg(MSGCAT_SYNTAX_INVALID_GET_TRAN_LOCK); }
	  LOCK_ TIMEOUT into_clause_opt
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(get_stmt, | GET TRANSACTION LOCK_ TIMEOUT into_clause_opt);

			PT_NODE *node = parser_new_node (this_parser, PT_GET_XACTION);

			if (node)
			  {
			    node->info.get_xaction.into_var = $6;
			    node->info.get_xaction.option = PT_LOCK_TIMEOUT;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| GET TRIGGER
		{ push_msg(MSGCAT_SYNTAX_INVALID_GET_TRIGGER_TRACE); }
	  TRACE into_clause_opt
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(get_stmt, | GET TRIGGER TRACE into_clause_opt);

			PT_NODE *node = parser_new_node (this_parser, PT_GET_TRIGGER);

			if (node)
			  {
			    node->info.get_trigger.into_var = $5;
			    node->info.get_trigger.option = PT_TRIGGER_TRACE;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| GET TRIGGER
		{ push_msg(MSGCAT_SYNTAX_INVALID_GET_TRIGGER_DEPTH); }
	  opt_maximum DEPTH into_clause_opt
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(get_stmt, | GET TRIGGER opt_maximum DEPTH into_clause_opt);

			PT_NODE *node = parser_new_node (this_parser, PT_GET_TRIGGER);

			if (node)
			  {
			    node->info.get_trigger.into_var = $6;
			    node->info.get_trigger.option = PT_TRIGGER_DEPTH;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;




create_stmt
	: CREATE					/* 1 */
		{ DBG_TRACE_GRAMMAR(create_stmt, : CREATE);	/* 2 */
			PT_NODE* qc = parser_new_node(this_parser, PT_CREATE_ENTITY);
			parser_push_hint_node(qc);
		}
	  opt_hint_list					/* 3 */
	  of_class_table_type				/* 4 */
	  opt_if_not_exists				/* 5 */
	  class_name_without_dot			/* 6 */
	  opt_subtable_clause 				/* 7 */
	  opt_class_attr_def_list			/* 8 */
	  opt_class_or_normal_attr_def_list		/* 9 */
	  opt_table_option_list				/* 10 */
	  opt_method_def_list 				/* 11 */
	  opt_method_files 				/* 12 */
	  opt_inherit_resolution_list			/* 13 */
	  opt_partition_clause 				/* 14 */
          opt_create_as_clause				/* 15 */
		{{ DBG_TRACE_GRAMMAR(create_stmt, : CREATE ~ of_class_table_type ~ class_name_without_dot ~);

			PT_NODE *qc = parser_pop_hint_node ();
			PARSER_SAVE_ERR_CONTEXT (qc, @$.buffer_pos)

			if (CONTAINER_AT_1 ($15) != NULL)
			  {
			    if ($7 != NULL		/* opt_subtable_clause */
				|| $8 != NULL		/* opt_class_attr_def_list */
				|| $11 != NULL		/* opt_method_def_list */
				|| $12 != NULL		/* opt_method_files */
				|| $13 != NULL)		/* opt_inherit_resolution_list */
			      {
				PT_ERRORf (this_parser, qc,
					   "check syntax at %s",
					   parser_print_tree (this_parser, qc));
			      }
			  }

			if (qc)
			  {
			    qc->info.create_entity.entity_type = (PT_MISC_TYPE) $4;
			    qc->info.create_entity.if_not_exists = $5;
			    qc->info.create_entity.entity_name = $6;
			    qc->info.create_entity.supclass_list = $7;
			    qc->info.create_entity.class_attr_def_list = $8;
			    qc->info.create_entity.attr_def_list = $9;
			    qc->info.create_entity.table_option_list = $10;
			    qc->info.create_entity.method_def_list = $11;
			    qc->info.create_entity.method_file_list = $12;
			    qc->info.create_entity.resolution_list = $13;
			    qc->info.create_entity.partition_info = $14;
                            if (CONTAINER_AT_1 ($15) != NULL)
			      {
				qc->info.create_entity.create_select_action = TO_NUMBER(CONTAINER_AT_0 ($15));
				qc->info.create_entity.create_select = CONTAINER_AT_1 ($15);
			      }

			    pt_gather_constraints (this_parser, qc);
			  }

			$$ = qc;

		DBG_PRINT}}
	| CREATE 					/* 1 */
	  opt_or_replace				/* 2 */
	  of_view_vclass 				/* 3 */
	  class_name_without_dot			/* 4 */
	  opt_subtable_clause 				/* 5 */
	  opt_class_attr_def_list 			/* 6 */
	  opt_paren_view_attr_def_list 			/* 7 */
	  opt_method_def_list 				/* 8 */
	  opt_method_files				/* 9 */
	  opt_inherit_resolution_list			/* 10 */
	  opt_as_query_list				/* 11 */
	  opt_with_levels_clause			/* 12 */
	  opt_vclass_comment_spec           		/* 13 */
		{{ DBG_TRACE_GRAMMAR(create_stmt, | CREATE ~ of_view_vclass class_name_without_dot ~ );

			PT_NODE *qc = parser_new_node (this_parser, PT_CREATE_ENTITY);

			if (qc)
			  {
			    qc->info.create_entity.or_replace = $2;

			    qc->info.create_entity.entity_name = $4;
			    qc->info.create_entity.entity_type = PT_VCLASS;

			    qc->info.create_entity.supclass_list = $5;
			    qc->info.create_entity.class_attr_def_list = $6;
			    qc->info.create_entity.attr_def_list = $7;
			    qc->info.create_entity.method_def_list = $8;
			    qc->info.create_entity.method_file_list = $9;
			    qc->info.create_entity.resolution_list = $10;
			    qc->info.create_entity.as_query_list = $11;
			    qc->info.create_entity.with_check_option = $12;
			    qc->info.create_entity.vclass_comment = $13;

			    pt_gather_constraints (this_parser, qc);
			  }


			$$ = qc;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| CREATE					/* 1 */
		{					/* 2 */
                        DBG_TRACE_GRAMMAR(create_stmt, | CREATE);
			PT_NODE* node = parser_new_node (this_parser, PT_CREATE_INDEX);
			parser_push_hint_node (node);
			push_msg (MSGCAT_SYNTAX_INVALID_CREATE_INDEX);
		}
	  opt_hint_list					/* 3 */
	  opt_reverse					/* 4 */
	  opt_unique					/* 5 */
	  INDEX						/* 6 */
		{ pop_msg(); }  			/* 7 */
	  identifier					/* 8 */
	  ON_						/* 9 */
	  only_class_name				/* 10 */
	  index_column_name_list			/* 11 */
	  opt_where_clause				/* 12 */
	  opt_comment_spec				/* 13 */
	  opt_with_online				/* 14 */
	  opt_invisible					/* 15 */
		{{ DBG_TRACE_GRAMMAR(create_stmt,  CREATE ~ INDEX identifier ON_ ~);

			PT_NODE *node = parser_pop_hint_node ();
			PT_NODE *ocs = parser_new_node(this_parser, PT_SPEC);
			PARSER_SAVE_ERR_CONTEXT (node, @$.buffer_pos)

		        if ($5 && $12)
			  {
			    /* Currently, not allowed unique with filter/function index.
			       However, may be introduced later, if it will be usefull.
			       Unique filter/function index code is removed from
			       grammar module only. It is kept yet in the others modules.
			       This will allow us to easily support this feature later by
			       adding in grammar only. If no need such feature,
			       filter/function code must be removed from all modules. */
			    PT_ERRORm (this_parser, node,
			               MSGCAT_SET_PARSER_SYNTAX,
			               MSGCAT_SYNTAX_INVALID_CREATE_INDEX);
			  }
			if (node && ocs)
			  {
			    PT_NODE *col, *temp;
			    int arg_count = 0, prefix_col_count = 0;

			    ocs->info.spec.entity_name = $10;
			    ocs->info.spec.only_all = PT_ONLY;
			    ocs->info.spec.meta_class = PT_CLASS;

			    PARSER_SAVE_ERR_CONTEXT (ocs, @10.buffer_pos)

			    node->info.index.indexed_class = ocs;
			    node->info.index.reverse = $4;
			    node->info.index.unique = $5;
			    node->info.index.index_name = $8;
			    if (node->info.index.index_name)
			      {
				node->info.index.index_name->info.name.meta_class = PT_INDEX_NAME;
			      }

			    col = $11;
			    if (node->info.index.unique)
			      {
			        for (temp = col; temp != NULL; temp = temp->next)
			          {
			            if (temp->info.sort_spec.expr->node_type == PT_EXPR)
			              {
			                /* Currently, not allowed unique with
			                   filter/function index. However, may be
			                   introduced later, if it will be usefull.
			                   Unique filter/function index code is removed
			                   from grammar module only. It is kept yet in
			                   the others modules. This will allow us to
			                   easily support this feature later by adding in
			                   grammar only. If no need such feature,
			                   filter/function code must be removed from all
			                   modules. */
			                PT_ERRORm (this_parser, node,
			                           MSGCAT_SET_PARSER_SYNTAX,
			                           MSGCAT_SYNTAX_INVALID_CREATE_INDEX);
			              }
			          }
			      }

			    prefix_col_count =
				parser_count_prefix_columns (col, &arg_count);

			    if (prefix_col_count > 1 ||
				(prefix_col_count == 1 && arg_count > 1))
			      {
				PT_ERRORm (this_parser, node,
					   MSGCAT_SET_PARSER_SEMANTIC,
					   MSGCAT_SEMANTIC_MULTICOL_PREFIX_INDX_NOT_ALLOWED);
			      }
			    else
			      {
				if (arg_count == 1 && (prefix_col_count == 1
				    || col->info.sort_spec.expr->node_type == PT_FUNCTION))
				  {
				    PT_NODE *expr = col->info.sort_spec.expr;
				    PT_NODE *arg_list = expr->info.function.arg_list;
				    if ((arg_list != NULL)
					&& (arg_list->next == NULL)
					&& (arg_list->node_type == PT_VALUE))
				      {
					if (node->info.index.reverse
					    || node->info.index.unique)
					  {
					    PT_ERRORm (this_parser, node,
						       MSGCAT_SET_PARSER_SYNTAX,
						       MSGCAT_SYNTAX_INVALID_CREATE_INDEX);
					  }
					else
					  {
					    PT_NODE *p = parser_new_node (this_parser,
									  PT_NAME);
					    if (p)
					      {
						p->info.name.original =
						     expr->info.function.generic_name;
					      }
					    node->info.index.prefix_length =
							 expr->info.function.arg_list;
					    col->info.sort_spec.expr = p;
					  }
				      }
				    else
				      {
					PT_ERRORmf (this_parser, col->info.sort_spec.expr,
						    MSGCAT_SET_PARSER_SEMANTIC,
						    MSGCAT_SEMANTIC_FUNCTION_CANNOT_BE_USED_FOR_INDEX,
						    fcode_get_lowercase_name (col->info.sort_spec.expr->info.function.
                                                                              function_type));
				      }
				  }
			      }
			    node->info.index.where = $12;
			    node->info.index.column_names = col;
			    node->info.index.comment = $13;

                            int with_online_ret = $14;  // 0 for normal, 1 for online no parallel,
                                                        // thread_count + 1 for parallel
                            bool is_online = with_online_ret > 0;
                            bool is_invisible = $15;

                            if (is_online && is_invisible)
                              {
                                /* We do not allow invisible and online index at the same time. */
                                PT_ERRORm (this_parser, node, MSGCAT_SET_PARSER_SYNTAX,
                                           MSGCAT_SYNTAX_INVALID_CREATE_INDEX);
                              }
                            node->info.index.index_status = SM_NORMAL_INDEX;
                            if (is_invisible)
                              {
                                /* Invisible index. */
                                node->info.index.index_status = SM_INVISIBLE_INDEX;
                              }
                            else if (is_online)
                              {
                                /* Online index. */
                                node->info.index.index_status = SM_ONLINE_INDEX_BUILDING_IN_PROGRESS;
                                node->info.index.ib_threads = with_online_ret - 1;
                              }
			  }
		      $$ = node;

		DBG_PRINT}}
	| CREATE			/* 1 */
		{ push_msg(MSGCAT_SYNTAX_INVALID_CREATE_USER); }	/* 2 */
	  USER				/* 3 */
	  identifier_without_dot	/* 4 */
	  opt_password			/* 5 */
	  opt_groups			/* 6 */
	  opt_members			/* 7 */
	  opt_comment_spec		/* 8 */
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(create_stmt, | CREATE USER identifier ~);

			PT_NODE *node = parser_new_node (this_parser, PT_CREATE_USER);

			if (node)
			  {
			    node->info.create_user.user_name = $4;
			    node->info.create_user.password = $5;
			    node->info.create_user.groups = $6;
			    node->info.create_user.members = $7;
			    node->info.create_user.comment = $8;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| CREATE					/* 1 */
		{ push_msg(MSGCAT_SYNTAX_INVALID_CREATE_TRIGGER); }	/* 2 */
	  TRIGGER 					/* 3 */
	  trigger_name_without_dot			/* 4 */
	  opt_status					/* 5 */
	  opt_priority					/* 6 */
	  trigger_time 					/* 7 */
		{ pop_msg(); }				/* 8 */
	  event_spec 					/* 9 */
	  opt_if_trigger_condition			/* 10 */
	  EXECUTE					/* 11 */
	  opt_trigger_action_time 			/* 12 */
	  trigger_action				/* 13 */
	  opt_comment_spec				/* 14 */
		{{ DBG_TRACE_GRAMMAR(create_stmt, | CREATE TRIGGER trigger_name_without_dot ~);

			PT_NODE *node = parser_new_node (this_parser, PT_CREATE_TRIGGER);

			if (node)
			  {
			    node->info.create_trigger.trigger_name = $4;
			    node->info.create_trigger.trigger_status = $5;
			    node->info.create_trigger.trigger_priority = $6;
			    node->info.create_trigger.condition_time = $7;
			    node->info.create_trigger.trigger_event = $9;
			    node->info.create_trigger.trigger_reference = NULL;
			    node->info.create_trigger.trigger_condition = $10;
			    node->info.create_trigger.action_time = $12;
			    node->info.create_trigger.trigger_action = $13;
			    node->info.create_trigger.comment = $14;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| CREATE					/* 1 */
		{ push_msg(MSGCAT_SYNTAX_INVALID_CREATE_SERIAL); }	/* 2 */
	  SERIAL 					/* 3 */
		{ pop_msg(); }				/* 4 */
	  serial_name_without_dot 			/* 5 */
	  opt_serial_option_list			/* 6 */
	  opt_comment_spec				/* 7 */
		{{ DBG_TRACE_GRAMMAR(create_stmt, | CREATE SERIAL serial_name_without_dot ~);

			PT_NODE *node = parser_new_node (this_parser, PT_CREATE_SERIAL);

			if (node)
			  {
			    node->info.serial.serial_name = $5;

			    /* container order
			     * 0: start_val
			     * 1: increment_val,
			     * 2: max_val,
			     * 3: no_max,
			     * 4: min_val,
			     * 5: no_min,
			     * 6: cyclic,
			     * 7: no_cyclic,
			     * 8: cached_num_val,
			     * 9: no_cache,
			     */

			    node->info.serial.start_val = CONTAINER_AT_0($6);
			    node->info.serial.increment_val = CONTAINER_AT_1($6);
			    node->info.serial.max_val = CONTAINER_AT_2 ($6);
			    node->info.serial.no_max = (int) TO_NUMBER (CONTAINER_AT_3 ($6));
			    node->info.serial.min_val = CONTAINER_AT_4 ($6);
			    node->info.serial.no_min = (int) TO_NUMBER (CONTAINER_AT_5 ($6));
			    node->info.serial.cyclic = (int) TO_NUMBER (CONTAINER_AT_6 ($6));
			    node->info.serial.no_cyclic = (int) TO_NUMBER (CONTAINER_AT_7 ($6));
			    node->info.serial.cached_num_val = CONTAINER_AT_8 ($6);
			    node->info.serial.no_cache = (int) TO_NUMBER (CONTAINER_AT_9 ($6));
			    node->info.serial.comment = $7;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| CREATE					/* 1 */
	  opt_or_replace                		/* 2 */
		{ push_msg(MSGCAT_SYNTAX_INVALID_CREATE_PROCEDURE); }	/* 3 */
	  PROCEDURE					/* 4 */
	  identifier '(' opt_sp_param_list  ')'		/* 5, 6, 7, 8 */
	  opt_of_is_as LANGUAGE JAVA			/* 9, 10, 11 */
	  NAME char_string_literal			/* 12, 13 */
	  opt_comment_spec				/* 14 */
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(create_stmt, | CREATE opt_or_replace PROCEDURE~);

			PT_NODE *node = parser_new_node (this_parser, PT_CREATE_STORED_PROCEDURE);
			if (node)
			  {
			    node->info.sp.or_replace = $2;
			    node->info.sp.name = $5;
			    node->info.sp.type = PT_SP_PROCEDURE;
			    node->info.sp.param_list = $7;
			    node->info.sp.ret_type = PT_TYPE_NONE;
			    node->info.sp.java_method = $13;
			    node->info.sp.comment = $14;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| CREATE                        		/* 1 */
	  opt_or_replace                		/* 2 */
		{ push_msg(MSGCAT_SYNTAX_INVALID_CREATE_FUNCTION); }	/* 3 */
	  FUNCTION					/* 4 */
	  identifier '('  opt_sp_param_list  ')'	/* 5, 6, 7, 8 */
	  RETURN opt_of_data_type_cursor		/* 9, 10 */
	  opt_of_is_as LANGUAGE JAVA			/* 11, 12, 13 */
	  NAME char_string_literal			/* 14, 15 */
	  opt_comment_spec				/* 16 */
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(create_stmt, | CREATE opt_or_replace FUNCTION~);

			PT_NODE *node = parser_new_node (this_parser, PT_CREATE_STORED_PROCEDURE);
			if (node)
			  {
			    node->info.sp.or_replace = $2;
			    node->info.sp.name = $5;
			    node->info.sp.type = PT_SP_FUNCTION;
			    node->info.sp.param_list = $7;
			    node->info.sp.ret_type = $10;
			    node->info.sp.java_method = $15;
			    node->info.sp.comment = $16;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| CREATE IdName
		{{ DBG_TRACE_GRAMMAR(create_stmt, | CREATE IdName);

			push_msg (MSGCAT_SYNTAX_INVALID_CREATE);
			csql_yyerror_explicit (@2.first_line, @2.first_column);

		DBG_PRINT}}
	| CREATE					/* 1 */
		{					/* 2 */
			PT_NODE* qc = parser_new_node(this_parser, PT_CREATE_ENTITY);
			parser_push_hint_node(qc);
		}
	  opt_hint_list					/* 3 */
	  of_class_table_type				/* 4 */
	  opt_if_not_exists				/* 5 */
	  class_name_without_dot			/* 6 */
	  LIKE						/* 7 */
	  class_name					/* 8 */
		{{ DBG_TRACE_GRAMMAR(create_stmt, | CREATE ~ class_name_without_dot LIKE class_name ~);

			PT_NODE *qc = parser_pop_hint_node ();

			if (qc)
			  {
			    qc->info.create_entity.if_not_exists = $5;
			    qc->info.create_entity.entity_name = $6;
			    qc->info.create_entity.entity_type = PT_CLASS;
			    qc->info.create_entity.create_like = $8;
			  }

			$$ = qc;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| CREATE					/* 1 */
		{					/* 2 */
			PT_NODE* qc = parser_new_node(this_parser, PT_CREATE_ENTITY);
			parser_push_hint_node(qc);
		}
	  opt_hint_list					/* 3 */
	  of_class_table_type				/* 4 */
	  opt_if_not_exists				/* 5 */
	  class_name_without_dot			/* 6 */
	  '('						/* 7 */
	  LIKE						/* 8 */
	  class_name					/* 9 */
	  ')'						/* 10 */
		{{ DBG_TRACE_GRAMMAR(create_stmt, | CREATE ~ class_name_without_dot '(' LIKE class_name ')' );

			PT_NODE *qc = parser_pop_hint_node ();

			if (qc)
			  {
			    qc->info.create_entity.if_not_exists = $5;
			    qc->info.create_entity.entity_name = $6;
			    qc->info.create_entity.entity_type = PT_CLASS;
			    qc->info.create_entity.create_like = $9;
			  }

			$$ = qc;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}

	| CREATE SERVER dblink_server_name '(' connect_info ')'
		{{ DBG_TRACE_GRAMMAR(create_stmt, | CREATE SERVER dblink_server_name '(' connect_info ')' );
                        PT_NODE *node = parser_new_node (this_parser, PT_CREATE_SERVER);
			if (node)
			  {
                                node->info.create_server.server_name = $3;
                                if ($3->next)
                                  {
                                     node->info.create_server.owner_name = $3->next;
                                     $3->next = NULL;
                                  }

                                PT_CREATE_SERVER_INFO *si = &node->info.create_server;
                                /* container order
                                * 1: HOST(IP/NAME)
                                * 2: PORT
                                * 3: DBNMAE
                                * 4: USER
                                * 5: PASSWORD
                                * 6: PROPERTIES
                                * 7: COMMENT
                                */
                                si->host = CONTAINER_AT_0($5);
                                si->port = CONTAINER_AT_1($5);
                                si->dbname = CONTAINER_AT_2($5);
                                si->user = CONTAINER_AT_3($5);
                                si->pwd = CONTAINER_AT_4($5);
                                if(si->pwd == NULL)
                                {
                                   PT_NODE *val = parser_new_node (this_parser, PT_VALUE);
                                   if (val)                    
                                     {
                                        char    cipher[512];

                                        val->type_enum = PT_TYPE_CHAR;
                                        val->info.value.string_type = ' ';

                                        if (pt_check_dblink_password(this_parser, NULL, cipher, sizeof(cipher)) == NO_ERROR)
                                        {
                                          val->info.value.data_value.str =
                                                pt_append_bytes (this_parser, NULL, cipher, strlen (cipher));
                                        }
                                        PT_NODE_PRINT_VALUE_TO_TEXT (this_parser, val);
                                     }
                                     si->pwd = val;
                                }

                                if( !si->host || !si->port || !si->dbname || !si->user || !si->pwd)
                                  { 
                                      PT_ERRORm (this_parser, node, MSGCAT_SET_PARSER_SEMANTIC,
					     MSGCAT_SEMANTIC_SERVER_MISSING_REQUIRED);
                                  }
                                si->prop = CONTAINER_AT_5($5);
                                si->comment = CONTAINER_AT_6($5);
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

opt_serial_option_list
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_serial_option_list, : );
			container_10 ctn;
			memset(&ctn, 0x00, sizeof(container_10));
			$$ = ctn;
		}}
	| serial_option_list
		{{ DBG_TRACE_GRAMMAR(opt_serial_option_list, | serial_option_list);
			$$ = $1;
		}}
	;

serial_option_list
	: serial_option_list of_serial_option
		{{ DBG_TRACE_GRAMMAR(serial_option_list, : serial_option_list of_serial_option);
			/* container order
			 * 1: start_val
			 *
			 * 2: increment_val,
			 *
			 * 3: max_val,
			 * 4: no_max,
			 *
			 * 5: min_val,
			 * 6: no_min,
			 *
			 * 7: cyclic,
			 * 8: no_cyclic,
			 *
			 * 9: cached_num_val,
			 * 10: no_cache,
			 */

			container_10 ctn = $1;

			PT_NODE* node = pt_top(this_parser);
			PARSER_SAVE_ERR_CONTEXT (node, @$.buffer_pos)

			switch(TO_NUMBER (CONTAINER_AT_0($2)))
			  {
			  case SERIAL_START:
				if (ctn.c1 != NULL)
				  {
				    PT_ERRORmf (this_parser, node, MSGCAT_SET_PARSER_SEMANTIC,
						MSGCAT_SEMANTIC_SERIAL_DUPLICATE_ATTR, "start");
				  }

				ctn.c1 = CONTAINER_AT_1($2);
				break;

			  case SERIAL_INC:
				if (ctn.c2 != NULL)
				  {
				    PT_ERRORmf (this_parser, node, MSGCAT_SET_PARSER_SEMANTIC,
						MSGCAT_SEMANTIC_SERIAL_DUPLICATE_ATTR, "increment");
				  }

				ctn.c2 = CONTAINER_AT_1($2);
				break;

			  case SERIAL_MAX:
				if (ctn.c3 != NULL || TO_NUMBER(ctn.c4) != 0)
				  {
				    PT_ERRORmf (this_parser, node, MSGCAT_SET_PARSER_SEMANTIC,
						MSGCAT_SEMANTIC_SERIAL_DUPLICATE_ATTR, "max");
				  }

				ctn.c3 = CONTAINER_AT_1($2);
				ctn.c4 = CONTAINER_AT_2($2);
				break;

			  case SERIAL_MIN:
				if (ctn.c5 != NULL || TO_NUMBER(ctn.c6) != 0)
				  {
				    PT_ERRORmf (this_parser, node, MSGCAT_SET_PARSER_SEMANTIC,
						MSGCAT_SEMANTIC_SERIAL_DUPLICATE_ATTR, "min");
				  }

				ctn.c5 = CONTAINER_AT_1($2);
				ctn.c6 = CONTAINER_AT_2($2);
				break;

			  case SERIAL_CYCLE:
				if (TO_NUMBER(ctn.c7) != 0 || TO_NUMBER(ctn.c8) != 0)
				  {
				    PT_ERRORmf (this_parser, node, MSGCAT_SET_PARSER_SEMANTIC,
						MSGCAT_SEMANTIC_SERIAL_DUPLICATE_ATTR, "cycle");
				  }

				ctn.c7 = CONTAINER_AT_1($2);
				ctn.c8 = CONTAINER_AT_2($2);
				break;

			  case SERIAL_CACHE:
				if (TO_NUMBER(ctn.c9) != 0 || TO_NUMBER(ctn.c10) != 0)
				  {
				    PT_ERRORmf (this_parser, node, MSGCAT_SET_PARSER_SEMANTIC,
						MSGCAT_SEMANTIC_SERIAL_DUPLICATE_ATTR, "cache");
				  }

				ctn.c9 = CONTAINER_AT_1($2);
				ctn.c10 = CONTAINER_AT_2($2);
				break;
			  }

			$$ = ctn;

		DBG_PRINT}}
	| of_serial_option
		{{ DBG_TRACE_GRAMMAR(serial_option_list, | of_serial_option);
			/* container order
			 * 1: start_val
			 *
			 * 2: increment_val,
			 *
			 * 3: max_val,
			 * 4: no_max,
			 *
			 * 5: min_val,
			 * 6: no_min,
			 *
			 * 7: cyclic,
			 * 8: no_cyclic,
			 *
			 * 9: cached_num_val,
			 * 10: no_cache,
			 */

			container_10 ctn;
			memset(&ctn, 0x00, sizeof(container_10));

			switch(TO_NUMBER (CONTAINER_AT_0($1)))
			  {
			  case SERIAL_START:
				ctn.c1 = CONTAINER_AT_1($1);
				break;

			  case SERIAL_INC:
				ctn.c2 = CONTAINER_AT_1($1);
				break;

			  case SERIAL_MAX:
				ctn.c3 = CONTAINER_AT_1($1);
				ctn.c4 = CONTAINER_AT_2($1);
				break;

			  case SERIAL_MIN:
				ctn.c5 = CONTAINER_AT_1($1);
				ctn.c6 = CONTAINER_AT_2($1);
				break;

			  case SERIAL_CYCLE:
				ctn.c7 = CONTAINER_AT_1($1);
				ctn.c8 = CONTAINER_AT_2($1);
				break;

			  case SERIAL_CACHE:
				ctn.c9 = CONTAINER_AT_1($1);
				ctn.c10 = CONTAINER_AT_2($1);
				break;
			  }

			$$ = ctn;

		DBG_PRINT}}
	;

of_serial_option
	: serial_start
		{{ DBG_TRACE_GRAMMAR(of_serial_option, : serial_start);
			container_3 ctn;
			SET_CONTAINER_3(ctn, FROM_NUMBER(SERIAL_START), $1, NULL);
			$$ = ctn;
		DBG_PRINT}}
	| serial_increment
		{{ DBG_TRACE_GRAMMAR(of_serial_option, | serial_increment);
			container_3 ctn;
			SET_CONTAINER_3(ctn, FROM_NUMBER(SERIAL_INC), $1, NULL);
			$$ = ctn;
		DBG_PRINT}}
	| serial_min
		{{ DBG_TRACE_GRAMMAR(of_serial_option, | serial_min);
			container_3 ctn;
			SET_CONTAINER_3(ctn, FROM_NUMBER(SERIAL_MIN), CONTAINER_AT_0($1), CONTAINER_AT_1($1));
			$$ = ctn;
		DBG_PRINT}}
	| serial_max
		{{ DBG_TRACE_GRAMMAR(of_serial_option, | serial_max);
			container_3 ctn;
			SET_CONTAINER_3(ctn, FROM_NUMBER(SERIAL_MAX), CONTAINER_AT_0($1), CONTAINER_AT_1($1));
			$$ = ctn;
		DBG_PRINT}}
	| of_cycle_nocycle
		{{ DBG_TRACE_GRAMMAR(of_serial_option, | of_cycle_nocycle);
			container_3 ctn;
			SET_CONTAINER_3(ctn, FROM_NUMBER(SERIAL_CYCLE), CONTAINER_AT_0($1), CONTAINER_AT_1($1));
			$$ = ctn;
		DBG_PRINT}}
	| of_cached_num
		{{ DBG_TRACE_GRAMMAR(of_serial_option, | of_cached_num);
			container_3 ctn;
			SET_CONTAINER_3(ctn, FROM_NUMBER(SERIAL_CACHE), CONTAINER_AT_0($1), CONTAINER_AT_1($1));
			$$ = ctn;
		DBG_PRINT}}
	;


opt_replace
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_replace, : );

			$$ = PT_CREATE_SELECT_NO_ACTION;

		DBG_PRINT}}
	| REPLACE
		{{ DBG_TRACE_GRAMMAR(opt_replace, | REPLACE);

			$$ = PT_CREATE_SELECT_REPLACE;

		DBG_PRINT}}
	;

alter_stmt
	: ALTER						/* 1 */
		{					/* 2 */
			PT_NODE* node = parser_new_node(this_parser, PT_ALTER);
			parser_push_hint_node(node);
		}
	  opt_hint_list					/* 3 */
	  opt_class_type				/* 4 */
	  only_class_name				/* 5 */
		{{ DBG_TRACE_GRAMMAR(alter_stmt, : ALTER opt_hint_list opt_class_type only_class_name);

			PT_NODE *node = parser_pop_hint_node ();
			int entity_type = ($4 == PT_EMPTY ? PT_MISC_DUMMY : $4);

			if (node)
			  {
			    node->info.alter.entity_type = entity_type;
			    node->info.alter.entity_name = $5;
			  }

			parser_save_alter_node (node);

		DBG_PRINT}}
	  alter_clause_cubrid_specific
		{{ DBG_TRACE_GRAMMAR(alter_stmt,  ~ alter_clause_cubrid_specific);

			PT_NODE *node = parser_get_alter_node ();

			if (node)
			  {
			    pt_gather_constraints (this_parser, node);
			  }
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| ALTER						/* 1 */
		{					/* 2 */
			PT_NODE* node = parser_new_node(this_parser, PT_ALTER);
			parser_push_hint_node(node);
		}
	  opt_hint_list					/* 3 */
	  opt_class_type				/* 4 */
	  only_class_name				/* 5 */
	  alter_clause_list				/* 6 */		%dprec 2
		{{ DBG_TRACE_GRAMMAR(alter_stmt, | ALTER opt_hint_list opt_class_type only_class_name alter_clause_list);

			PT_NODE *node = NULL;
			int entity_type = ($4 == PT_EMPTY ? PT_MISC_DUMMY : $4);

			for (node = $6; node != NULL; node = node->next)
			  {
			    node->info.alter.entity_type = entity_type;
			    node->info.alter.entity_name = parser_copy_tree (this_parser, $5);
			    pt_gather_constraints (this_parser, node);
			    if (node->info.alter.code == PT_RENAME_ENTITY)
			      {
				node->info.alter.alter_clause.rename.element_type = entity_type;
				/* We can get the original name from info.alter.entity_name */
				node->info.alter.alter_clause.rename.old_name = NULL;
			      }
			    else
			      {
				if (node->info.alter.code == PT_ADD_ATTR_MTHD)
				  {
				    PT_NODE *p = node->info.alter.create_index;
				    if (p)
				      {
					node->info.alter.code = PT_ADD_INDEX_CLAUSE;
				      }
				    /* add the table spec to each ALTER TABLE ADD INDEX
				     * clause in the ALTER statement */
				    while (p)
				      {
					PT_NODE *ocs = parser_new_node(this_parser, PT_SPEC);

					if (ocs)
					  {
					    ocs->info.spec.entity_name = parser_copy_tree(this_parser, $5);
					    ocs->info.spec.only_all = PT_ONLY;
					    ocs->info.spec.meta_class = PT_CLASS;

					    p->info.index.indexed_class = ocs;
					    p = p->next;
					  }
					else
					  {
					    break;
					  }
				      }
				  }
			      }
			  }
			parser_free_tree (this_parser, $5);

			$$ = $6;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| ALTER
	  USER
	  identifier
	  opt_password
	  opt_comment_spec
		{{ DBG_TRACE_GRAMMAR(alter_stmt, | ALTER USER identifier opt_password opt_comment_spec);

			PT_NODE *node = parser_new_node (this_parser, PT_ALTER_USER);

			if (node)
			  {
			    node->info.alter_user.user_name = $3;
			    node->info.alter_user.password = $4;
			    node->info.alter_user.comment = $5;
			    if (node->info.alter_user.password == NULL
			        && node->info.alter_user.comment == NULL)
			      {
			        PT_ERRORm (this_parser, node, MSGCAT_SET_PARSER_SYNTAX,
                               MSGCAT_SYNTAX_INVALID_ALTER);
			      }
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| ALTER						/* 1 */
	  TRIGGER					/* 2 */
	  trigger_name_list				/* 3 */
	  trigger_status_or_priority_or_change_owner	/* 4 */
	  opt_comment_spec				/* 5 */
		{{ DBG_TRACE_GRAMMAR(alter_stmt, | ALTER TRIGGER trigger_name_list trigger_status_or_priority_or_change_owner opt_comment_spec);

			PT_NODE *node = parser_new_node (this_parser, PT_ALTER_TRIGGER);

			PT_NODE *list = parser_new_node (this_parser, PT_TRIGGER_SPEC_LIST);
			if (list)
			  {
			    list->info.trigger_spec_list.trigger_name_list = $3;
			  }

			if (node)
			  {
			    node->info.alter_trigger.trigger_spec_list = list;
			    node->info.alter_trigger.trigger_status = TO_NUMBER (CONTAINER_AT_0 ($4));
			    node->info.alter_trigger.trigger_priority = CONTAINER_AT_1 ($4);
			    node->info.alter_trigger.trigger_owner = CONTAINER_AT_2 ($4);
			    node->info.alter_trigger.comment = $5;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| ALTER				/* 1 */
	  TRIGGER			/* 2 */
	  trigger_name			/* 3 */
	  COMMENT comment_value		/* 4, 5 */
		{{ DBG_TRACE_GRAMMAR(alter_stmt, | ALTER TRIGGER identifier COMMENT comment_value);

			PT_NODE *node = parser_new_node (this_parser, PT_ALTER_TRIGGER);

			PT_NODE *list = parser_new_node (this_parser, PT_TRIGGER_SPEC_LIST);
			if (list)
			  {
			    list->info.trigger_spec_list.trigger_name_list = $3;
			  }

			if (node)
			  {
			    node->info.alter_trigger.trigger_spec_list = list;
			    node->info.alter_trigger.comment = $5;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| ALTER						/* 1 */
	  SERIAL					/* 2 */
	  serial_name					/* 3 */
	  opt_serial_option_list			/* 4 */
	  opt_comment_spec				/* 5 */
		{{ DBG_TRACE_GRAMMAR(alter_stmt, | ALTER SERIAL serial_name opt_serial_option_list opt_comment_spec);
			/* container order
			 * 0: start_val
			 * 1: increment_val,
			 * 2: max_val,
			 * 3: no_max,
			 * 4: min_val,
			 * 5: no_min,
			 * 6: cyclic,
			 * 7: no_cyclic,
			 * 8: cached_num_val,
			 * 9: no_cache,
			 */

			PT_NODE *serial_name = $3;
			PT_NODE *start_val = CONTAINER_AT_0 ($4);
			PT_NODE *increment_val = CONTAINER_AT_1 ($4);
			PT_NODE *max_val = CONTAINER_AT_2 ($4);
			int no_max = (int) TO_NUMBER (CONTAINER_AT_3 ($4));
			PT_NODE *min_val = CONTAINER_AT_4 ($4);
			int no_min = (int) TO_NUMBER (CONTAINER_AT_5 ($4));
			int cyclic = (int) TO_NUMBER (CONTAINER_AT_6 ($4));
			int no_cyclic = (int) TO_NUMBER (CONTAINER_AT_7 ($4));
			PT_NODE *cached_num_val = CONTAINER_AT_8 ($4);
			int no_cache = (int) TO_NUMBER (CONTAINER_AT_9 ($4));
			PT_NODE *comment = $5;

			PT_NODE *node = parser_new_node (this_parser, PT_ALTER_SERIAL);
			if (node)
			  {
			    node->info.serial.serial_name = serial_name;
			    node->info.serial.increment_val = increment_val;
			    node->info.serial.start_val = start_val;
			    node->info.serial.max_val = max_val;
			    node->info.serial.no_max = no_max;
			    node->info.serial.min_val = min_val;
			    node->info.serial.no_min = no_min;
			    node->info.serial.cyclic = cyclic;
			    node->info.serial.no_cyclic = no_cyclic;
			    node->info.serial.cached_num_val = cached_num_val;
			    node->info.serial.no_cache = no_cache;
			    node->info.serial.comment = comment;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

			if (!start_val && !increment_val && !max_val && !min_val
			    && cyclic == 0 && no_max == 0 && no_min == 0
			    && no_cyclic == 0 && !cached_num_val && no_cache == 0
			    && comment == NULL)
			  {
			    PT_ERRORmf (this_parser, node, MSGCAT_SET_PARSER_SEMANTIC,
					MSGCAT_SEMANTIC_SERIAL_ALTER_NO_OPTION, 0);
			  }

		DBG_PRINT}}
	| ALTER						/* 1 */
		{					/* 2 */
			PT_NODE* node = parser_new_node(this_parser, PT_ALTER_INDEX);
			parser_push_hint_node(node);
		}
	  opt_hint_list					/* 3 */
	  opt_reverse					/* 4 */
	  opt_unique					/* 5 */
	  INDEX						/* 6 */
	  identifier					/* 7 */
	  ON_						/* 8 */
	  only_class_name				/* 9 */
	  opt_index_column_name_list			/* 10 */
	  opt_where_clause				/* 11 */
	  opt_comment_spec				/* 12 */
	  REBUILD					/* 13 */
		{{ DBG_TRACE_GRAMMAR(alter_stmt, | ALTER ~ INDEX ~ REBUILD);

			PT_NODE *node = parser_pop_hint_node ();
			PT_NODE *ocs = parser_new_node(this_parser, PT_SPEC);

			if ($5 && $11)
			  {
			    /* Currently, not allowed unique with filter/function index.
			       However, may be introduced later, if it will be usefull.
			       Unique filter/function index code is removed from
			       grammar module only. It is kept yet in the others modules.
			       This will allow us to easily support this feature later by
			       adding in grammar only. If no need such feature,
			       filter/function code must be removed from all modules. */
			    PT_ERRORm (this_parser, node, MSGCAT_SET_PARSER_SYNTAX,
			               MSGCAT_SYNTAX_INVALID_CREATE_INDEX);
			  }
			if (node && ocs)
			  {
			    PT_NODE *col, *temp;
			    node->info.index.code = PT_REBUILD_INDEX;
			    node->info.index.reverse = $4;
			    node->info.index.unique = $5;
			    node->info.index.index_name = $7;
			    if (node->info.index.index_name)
			      {
				node->info.index.index_name->info.name.meta_class = PT_INDEX_NAME;
			      }

			    ocs->info.spec.entity_name = $9;
			    ocs->info.spec.only_all = PT_ONLY;
			    ocs->info.spec.meta_class = PT_CLASS;

			    node->info.index.indexed_class = ocs;
			    col = $10;
			    if (node->info.index.unique)
			      {
			        for (temp = col; temp != NULL; temp = temp->next)
			          {
			            if (temp->info.sort_spec.expr->node_type == PT_EXPR)
			              {
			                /* Currently, not allowed unique with
			                   filter/function index. However, may be
			                   introduced later, if it will be usefull.
			                   Unique filter/function index code is removed
			                   from grammar module only. It is kept yet in
			                   the others modules. This will allow us to
			                   easily support this feature later by adding in
			                   grammar only. If no need such feature,
			                   filter/function code must be removed from all
			                   modules. */
			                PT_ERRORm (this_parser, node,
			                           MSGCAT_SET_PARSER_SYNTAX,
			                           MSGCAT_SYNTAX_INVALID_CREATE_INDEX);
			              }
			          }
			      }

			    node->info.index.column_names = col;
			    node->info.index.where = $11;
			    node->info.index.comment = $12;

			    $$ = node;
			    PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
			  }

		DBG_PRINT}}
	| ALTER						/* 1 */
	  INDEX						/* 2 */
	  identifier					/* 3 */
	  ON_						/* 4 */
	  class_name					/* 5 */
	  COMMENT comment_value 			/* 6, 7 */
		{{ DBG_TRACE_GRAMMAR(alter_stmt, | ALTER INDEX identifier ON_ class_name COMMENT comment_value);
			PT_NODE* node = parser_new_node(this_parser, PT_ALTER_INDEX);

			if (node)
			  {
			    node->info.index.code = PT_CHANGE_INDEX_COMMENT;
			    node->info.index.index_name = $3;
			    node->info.index.comment = $7;

			    if (node->info.index.index_name)
			      {
			        node->info.index.index_name->info.name.meta_class = PT_INDEX_NAME;
			      }

			    if ($5 != NULL)
			      {
			        PT_NODE *ocs = parser_new_node(this_parser, PT_SPEC);
			        ocs->info.spec.entity_name = $5;
			        ocs->info.spec.only_all = PT_ONLY;
			        ocs->info.spec.meta_class = PT_CLASS;

			        node->info.index.indexed_class = ocs;
			      }
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| ALTER				/* 1 */
	  INDEX				/* 2 */
	  identifier			/* 3 */
	  ON_				/* 4 */
	  class_name			/* 5 */
	  INVISIBLE			/* 6 */
		{{ DBG_TRACE_GRAMMAR(alter_stmt, | ALTER INDEX identifier ON_ class_name INVISIBLE);
			PT_NODE* node = parser_new_node(this_parser, PT_ALTER_INDEX);

			if (node)
			  {
			    node->info.index.code = PT_CHANGE_INDEX_STATUS;
			    node->info.index.index_name = $3;
			    node->info.index.index_status = SM_INVISIBLE_INDEX;

			    if (node->info.index.index_name)
			      {
			        node->info.index.index_name->info.name.meta_class = PT_INDEX_NAME;
			      }

			    if ($5 != NULL)
			      {
			        PT_NODE *ocs = parser_new_node(this_parser, PT_SPEC);
			        ocs->info.spec.entity_name = $5;
			        ocs->info.spec.only_all = PT_ONLY;
			        ocs->info.spec.meta_class = PT_CLASS;

			        node->info.index.indexed_class = ocs;
			      }
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| ALTER				/* 1 */
	  INDEX				/* 2 */
	  identifier			/* 3 */
	  ON_				/* 4 */
	  class_name			/* 5 */
	  VISIBLE			/* 6 */
		{{ DBG_TRACE_GRAMMAR(alter_stmt, | ALTER INDEX identifier ON_ class_name VISIBLE);
			PT_NODE* node = parser_new_node(this_parser, PT_ALTER_INDEX);

			if (node)
			  {
			    node->info.index.code = PT_CHANGE_INDEX_STATUS;
			    node->info.index.index_name = $3;
			    node->info.index.index_status = SM_NORMAL_INDEX;

			    if (node->info.index.index_name)
			      {
			        node->info.index.index_name->info.name.meta_class = PT_INDEX_NAME;
			      }

			    if ($5 != NULL)
			      {
			        PT_NODE *ocs = parser_new_node(this_parser, PT_SPEC);
			        ocs->info.spec.entity_name = $5;
			        ocs->info.spec.only_all = PT_ONLY;
			        ocs->info.spec.meta_class = PT_CLASS;

			        node->info.index.indexed_class = ocs;
			      }
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| ALTER						/* 1 */
	  view_or_vclass				/* 2 */
	  class_name					/* 3 */
	  AS						/* 4 */
	  csql_query					/* 5 */
	  opt_vclass_comment_spec			/* 6 */
		{{ DBG_TRACE_GRAMMAR(alter_stmt, | ALTER view_or_vclass class_name AS csql_query opt_vclass_comment_spec);

			PT_NODE *node = parser_new_node (this_parser, PT_ALTER);
			if (node)
			  {
			    node->info.alter.entity_type = PT_VCLASS;
			    node->info.alter.entity_name = $3;
			    node->info.alter.code = PT_RESET_QUERY;
			    node->info.alter.alter_clause.query.query = $5;
			    node->info.alter.alter_clause.query.query_no_list = NULL;
			    node->info.alter.alter_clause.query.attr_def_list = NULL;
			    node->info.alter.alter_clause.query.view_comment = $6;

			    pt_gather_constraints (this_parser, node);
			  }
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| ALTER
	  view_or_vclass
	  class_name
	  class_comment_spec		%dprec 1
		{{ DBG_TRACE_GRAMMAR(alter_stmt, | ALTER view_or_vclass class_name class_comment_spec);

			PT_NODE *node = parser_new_node (this_parser, PT_ALTER);
			if (node)
			  {
			    node->info.alter.entity_type = PT_VCLASS;
			    node->info.alter.entity_name = $3;
			    node->info.alter.code = PT_CHANGE_TABLE_COMMENT;
			    node->info.alter.alter_clause.comment.tbl_comment = $4;

			    pt_gather_constraints (this_parser, node);
			  }
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| ALTER				/* 1 */
	  procedure_or_function		/* 2 */
	  identifier				/* 3 */
	  opt_owner_clause			/* 4 */
	  opt_comment_spec			/* 5 */
		{{ DBG_TRACE_GRAMMAR(alter_stmt, | ALTER procedure_or_function identifier opt_owner_clause opt_comment_spec);

			PT_NODE *node = parser_new_node (this_parser, PT_ALTER_STORED_PROCEDURE);

			if (node != NULL)
			  {
			    node->info.sp.name = $3;
			    node->info.sp.type = ($2 == 1) ? PT_SP_PROCEDURE : PT_SP_FUNCTION;
			    node->info.sp.ret_type = PT_TYPE_NONE;
			    node->info.sp.owner = $4;
			    node->info.sp.comment = $5;
			    if ($4 == NULL && $5 == NULL)
			      {
			        PT_ERRORm (this_parser, node,
			                   MSGCAT_SET_PARSER_SYNTAX,
			                   MSGCAT_SYNTAX_INVALID_ALTER);
			      }
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| ALTER  SERVER  dblink_server_name alter_server_list
                {{ DBG_TRACE_GRAMMAR(alter_stmt, | ALTER  SERVER  dblink_server_name alter_server_list);
                        PT_NODE *node = parser_new_node (this_parser, PT_ALTER_SERVER);
			if (node)
			  {
                                char *str;  
                                bool is_not_allowed = false;  
                                int  item_bits = (int)TO_NUMBER(CONTAINER_AT_9($4));
                                PT_ALTER_SERVER_INFO *server = &node->info.alter_server;

                                node->info.alter_server.server_name = $3;
                                if ($3->next)
                                  {
                                     node->info.alter_server.current_owner_name = $3->next;
                                     $3->next = NULL;
                                  }                                
                                is_not_allowed = (bool)(item_bits == 0);

                                /* container order
                                * 1: HOST(IP/NAME)
                                * 2: PORT
                                * 3: DBNMAE
                                * 4: USER
                                * 5: PASSWORD
                                * 6: PROPERTIES
                                * 7: COMMENT
                                * 8: OWNER
                                * 9: --
                                *10: bits
                                */
                                server->host = CONTAINER_AT_0($4);
                                server->port = CONTAINER_AT_1($4);
                                server->dbname = CONTAINER_AT_2($4);
                                server->user = CONTAINER_AT_3($4);
                                server->pwd = CONTAINER_AT_4($4);
                                server->prop = CONTAINER_AT_5($4);
                                server->comment = CONTAINER_AT_6($4);
                                server->owner_name = CONTAINER_AT_7($4);

                                if (item_bits & (0x01 << CONN_INFO_HOST))
                                {
                                    server->xbits.bit_host = 1;
                                    str = (char *) PT_VALUE_GET_BYTES (server->host);                                                                       
                                    is_not_allowed |= (!str || str[0] == '\0');
                                }
                                if (item_bits & (0x01 << CONN_INFO_PORT))
                                {
                                    server->xbits.bit_port = 1;
                                    is_not_allowed |= (!server->port);
                                }
                                if (item_bits & (0x01 << CONN_INFO_DBNAME))
                                {
                                    server->xbits.bit_dbname = 1;
                                    str = server->dbname->info.name.original;
                                    is_not_allowed |= (!str || str[0] == '\0');
                                }
                                if (item_bits & (0x01 << CONN_INFO_USER))
                                {
                                    server->xbits.bit_user = 1;
                                    str = server->user->info.name.original;
                                    is_not_allowed |= (!str || str[0] == '\0');
                                }  
                                if (item_bits & (0x01 << CONN_INFO_PASSWORD))
                                {
                                    server->xbits.bit_pwd = 1;
                                }
                                if (item_bits & (0x01 << CONN_INFO_PROPERTIES))
                                {
                                    server->xbits.bit_prop = 1;
                                }
                                if (item_bits & (0x01 << CONN_INFO_COMMENT))
                                {
                                    server->xbits.bit_comment = 1;
                                }  
                                if (item_bits & (0x01 << CONN_INFO_OWNER))
                                {
                                    server->xbits.bit_owner = 1;
                                    str = server->owner_name->info.name.original;
                                    is_not_allowed |= (!str || str[0] == '\0');
                                }

                                if(is_not_allowed)
                                  {                                       
                                        PT_ERRORm (this_parser, node, MSGCAT_SET_PARSER_SEMANTIC,
					     MSGCAT_SEMANTIC_SERVER_MISSING_REQUIRED);
                                  }                 
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

view_or_vclass
	: VIEW   { DBG_TRACE_GRAMMAR(view_or_vclass, : VIEW ); }
	| VCLASS { DBG_TRACE_GRAMMAR(view_or_vclass, | VCLASS ); }
	;

alter_clause_list
	: alter_clause_list ',' prepare_alter_node alter_clause_for_alter_list
		{{ DBG_TRACE_GRAMMAR(alter_clause_list, : alter_clause_list ',' prepare_alter_node alter_clause_for_alter_list);

			$$ = parser_make_link ($1, parser_get_alter_node ());
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| /* The first node in the list is the one that was pushed for hints. */
		{

			PT_NODE *node = parser_pop_hint_node ();
			parser_save_alter_node (node);
		}
	 alter_clause_for_alter_list
		{{ DBG_TRACE_GRAMMAR(alter_clause_list, | alter_clause_for_alter_list);

			$$ = parser_get_alter_node ();
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

prepare_alter_node
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(prepare_alter_node, : );

			PT_NODE *node = parser_new_node (this_parser, PT_ALTER);
			parser_save_alter_node (node);

		DBG_PRINT}}
	;

only_class_name
	: ONLY user_specified_name
		{ DBG_TRACE_GRAMMAR(only_class_name, : ONLY user_specified_name);
                  $$ = $2; }
	| user_specified_name
		{ DBG_TRACE_GRAMMAR(only_class_name, | user_specified_name);
                  $$ = $1; }
	;

rename_stmt
	: RENAME opt_class_type rename_class_list
		{{ DBG_TRACE_GRAMMAR(rename_stmt, : RENAME opt_class_type rename_class_list);

			PT_NODE *node = NULL;
			int entity_type = ($2 == PT_EMPTY ? PT_CLASS : $2);

			for (node = $3; node != NULL; node = node->next)
			  {
			    node->info.rename.entity_type = entity_type;
			  }

			$$ = $3;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| RENAME TRIGGER trigger_name as_or_to trigger_name
		{{ DBG_TRACE_GRAMMAR(rename_stmt, | RENAME TRIGGER trigger_name as_or_to trigger_name);

			PT_NODE *node = parser_new_node (this_parser, PT_RENAME_TRIGGER);

			if (node)
			  {
			    node->info.rename_trigger.old_name = $3;
			    node->info.rename_trigger.new_name = $5;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| RENAME SERVER dblink_server_name as_or_to identifier_without_dot
		{{ DBG_TRACE_GRAMMAR(rename_stmt, | RENAME SERVER dblink_server_name as_or_to identifier_without_dot);
			PT_NODE *node = parser_new_node (this_parser, PT_RENAME_SERVER);
			if (node)
			  {
			    node->info.rename_server.old_name = $3;
                            node->info.rename_server.owner_name = $3->next;
                            $3->next = NULL;
                            node->info.rename_server.new_name = $5;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
		DBG_PRINT}}
	;

rename_class_list
	: rename_class_list ',' rename_class_pair
		{{ DBG_TRACE_GRAMMAR(rename_class_list, : rename_class_list ',' rename_class_pair);

			$$ = parser_make_link ($1, $3);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| rename_class_pair
		{{ DBG_TRACE_GRAMMAR(rename_class_list, | rename_class_pair);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

rename_class_pair
	: only_class_name as_or_to only_class_name
		{{ DBG_TRACE_GRAMMAR(rename_class_pair, : only_class_name as_or_to only_class_name);

			PT_NODE *node = parser_new_node (this_parser, PT_RENAME);
			if (node)
			  {
			    node->info.rename.old_name = $1;
			    node->info.rename.new_name = $3;
			    node->info.rename.entity_type = PT_CLASS;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

procedure_or_function
	: PROCEDURE
		{{ DBG_TRACE_GRAMMAR(procedure_or_function, : PROCEDURE);

			$$ = 1;

		DBG_PRINT}}
	| FUNCTION
		{{ DBG_TRACE_GRAMMAR(procedure_or_function, | FUNCTION);

			$$ = 2;

		DBG_PRINT}}
	;

opt_owner_clause
	: /* empty */
		{ DBG_TRACE_GRAMMAR(opt_owner_clause, : );
                  $$ = NULL; }
	| OWNER TO identifier
		{ DBG_TRACE_GRAMMAR(opt_owner_clause, | OWNER TO identifier);
                  $$ = $3; }
	;

as_or_to
	: AS
	| TO
	;

truncate_stmt
	: TRUNCATE opt_table_type class_spec opt_cascade
		{{ DBG_TRACE_GRAMMAR(truncate_stmt, : TRUNCATE opt_table_type class_spec opt_cascade);

			PT_NODE *node = parser_new_node (this_parser, PT_TRUNCATE);
			if (node)
			  {
			    node->info.truncate.spec = $3;
			    node->info.truncate.is_cascade = $4;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

do_stmt
	: DO expression_
		{{ DBG_TRACE_GRAMMAR(do_stmt, :  DO expression_);

			PT_NODE *node = parser_new_node (this_parser, PT_DO);
			if (node)
			  {
			    PT_NODE *expr = $2, *subq = NULL;

			    if (expr && PT_IS_QUERY_NODE_TYPE (expr->node_type))
			      {
				expr->info.query.flag.single_tuple = 1;

				if ((subq = pt_get_subquery_list (expr)) && subq->next)
				{
				  /* illegal multi-column subquery */
				  PT_ERRORm (this_parser, expr, MSGCAT_SET_PARSER_SEMANTIC,
					     MSGCAT_SEMANTIC_NOT_SINGLE_COL);
				}
			      }

			    node->info.do_.expr = expr;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

drop_stmt
	: DROP opt_class_type opt_if_exists class_spec_list opt_cascade_constraints
		{{ DBG_TRACE_GRAMMAR(drop_stmt, : DROP opt_class_type opt_if_exists class_spec_list opt_cascade_constraints);

			PT_NODE *node = parser_new_node (this_parser, PT_DROP);
			if (node)
			  {
			    node->info.drop.if_exists = ($3 == 1);
			    node->info.drop.spec_list = $4;
			    node->info.drop.is_cascade_constraints = $5;

			    if ($2 == PT_EMPTY)
			      node->info.drop.entity_type = PT_MISC_DUMMY;
			    else
			      node->info.drop.entity_type = $2;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| DROP					/* 1 */
		{				/* 2 */
			PT_NODE* node = parser_new_node(this_parser, PT_DROP_INDEX);
			parser_push_hint_node(node);
		}
	  opt_hint_list					/* 3 */
	  opt_reverse					/* 4 */
	  opt_unique					/* 5 */
	  INDEX						/* 6 */
	  identifier					/* 7 */
	  ON_						/* 8 */
	  only_class_name				/* 9 */
	  opt_index_column_name_list			/* 10 */
		{{ DBG_TRACE_GRAMMAR(drop_stmt, | DROP ~ INDEX ~);

			PT_NODE *node = parser_pop_hint_node ();
			PT_NODE *ocs = parser_new_node(this_parser, PT_SPEC);

			if (node && ocs)
			  {
			    PT_NODE *col, *temp;
			    node->info.index.reverse = $4;
			    node->info.index.unique = $5;
			    node->info.index.index_name = $7;
			    if (node->info.index.index_name)
			      {
				node->info.index.index_name->info.name.meta_class = PT_INDEX_NAME;
			      }

			    ocs->info.spec.entity_name = $9;
			    ocs->info.spec.only_all = PT_ONLY;
			    ocs->info.spec.meta_class = PT_CLASS;
			    PARSER_SAVE_ERR_CONTEXT (ocs, @9.buffer_pos)
			    node->info.index.indexed_class = ocs;

			    col = $10;
			    if (node->info.index.unique)
			      {
			        for (temp = col; temp != NULL; temp = temp->next)
			          {
			            if (temp->info.sort_spec.expr->node_type == PT_EXPR)
			              {
			                /* Currently, not allowed unique with
			                   filter/function index. However, may be
			                   introduced later, if it will be usefull.
			                   Unique filter/function index code is removed
			                   from grammar module only. It is kept yet in
			                   the others modules. This will allow us to
			                   easily support this feature later by adding in
			                   grammar only. If no need such feature,
			                   filter/function code must be removed from all
			                   modules. */
			                PT_ERRORm (this_parser, node,
			                           MSGCAT_SET_PARSER_SYNTAX,
			                           MSGCAT_SYNTAX_INVALID_CREATE_INDEX);
			              }
			          }
			      }
			    node->info.index.column_names = col;

			    $$ = node;
			    PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
			  }

		DBG_PRINT}}
	| DROP USER identifier
		{{ DBG_TRACE_GRAMMAR(drop_stmt, | DROP USER identifier);

			PT_NODE *node = parser_new_node (this_parser, PT_DROP_USER);

			if (node)
			  {
			    node->info.drop_user.user_name = $3;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| DROP TRIGGER trigger_name_list
		{{ DBG_TRACE_GRAMMAR(drop_stmt, | DROP TRIGGER trigger_name_list);

			PT_NODE *node = parser_new_node (this_parser, PT_DROP_TRIGGER);

			PT_NODE *list = parser_new_node (this_parser, PT_TRIGGER_SPEC_LIST);
			if (list)
			  {
			    list->info.trigger_spec_list.trigger_name_list = $3;
			  }

			if (node)
			  {
			    node->info.drop_trigger.trigger_spec_list = list;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| DROP DEFERRED TRIGGER trigger_spec_list
		{{ DBG_TRACE_GRAMMAR(drop_stmt, | DROP DEFERRED TRIGGER trigger_spec_list);

			PT_NODE *node = parser_new_node (this_parser, PT_REMOVE_TRIGGER);

			if (node)
			  {
			    node->info.remove_trigger.trigger_spec_list = $4;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| DROP VARIABLE_ identifier_list
		{{ DBG_TRACE_GRAMMAR(drop_stmt, | DROP VARIABLE_ identifier_list);

			PT_NODE *node = parser_new_node (this_parser, PT_DROP_VARIABLE);
			if (node)
			  node->info.drop_variable.var_names = $3;
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| DROP SERIAL opt_if_exists serial_name
		{{ DBG_TRACE_GRAMMAR(drop_stmt, | DROP SERIAL opt_if_exists serial_name);

			PT_NODE *node = parser_new_node (this_parser, PT_DROP_SERIAL);
			if (node)
			  {
			    node->info.serial.if_exists = $3;
			    node->info.serial.serial_name = $4;
			  }
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| DROP PROCEDURE identifier_list
		{{ DBG_TRACE_GRAMMAR(drop_stmt, | DROP PROCEDURE identifier_list);

			PT_NODE *node = parser_new_node (this_parser, PT_DROP_STORED_PROCEDURE);

			if (node)
			  {
			    node->info.sp.name = $3;
			    node->info.sp.type = PT_SP_PROCEDURE;
			  }


			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| DROP FUNCTION identifier_list
		{{ DBG_TRACE_GRAMMAR(drop_stmt, | DROP FUNCTION identifier_list);

			PT_NODE *node = parser_new_node (this_parser, PT_DROP_STORED_PROCEDURE);

			if (node)
			  {
			    node->info.sp.name = $3;
			    node->info.sp.type = PT_SP_FUNCTION;
			  }


			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| DROP SERVER opt_if_exists dblink_server_name
		{{ DBG_TRACE_GRAMMAR(drop_stmt, | DROP SERVER opt_if_exists dblink_server_name);
                        PT_NODE *node = parser_new_node (this_parser, PT_DROP_SERVER);

			if (node)
			  {
                            node->info.drop_server.if_exists = $3;
			    node->info.drop_server.server_name = $4;
                            if ($4->next)
                              {
                                node->info.drop_server.owner_name = $4->next;
                                $4->next = NULL;
                              }
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}                
	| deallocate_or_drop PREPARE identifier
		{{ DBG_TRACE_GRAMMAR(drop_stmt, | deallocate_or_drop PREPARE identifier);

			PT_NODE *node = parser_new_node (this_parser, PT_DEALLOCATE_PREPARE);

			if (node)
			  {
			    node->info.prepare.name = $3;
			  }


			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| deallocate_or_drop VARIABLE_ session_variable_list
		{{ DBG_TRACE_GRAMMAR(drop_stmt, | deallocate_or_drop VARIABLE_ session_variable_list);

			PT_NODE *node =
			  parser_new_node (this_parser, PT_DROP_SESSION_VARIABLES);
			if (node)
			  {
			    node->info.prepare.name = $3;
			  }
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

deallocate_or_drop
	: DEALLOCATE
	| DROP
	;

opt_reverse
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_reverse, : );

			parser_save_is_reverse (false);
			$$ = false;

		DBG_PRINT}}
	| REVERSE
		{{ DBG_TRACE_GRAMMAR(opt_reverse, | REVERSE);

			parser_save_is_reverse (true);
			$$ = true;

		DBG_PRINT}}
	;

opt_unique
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_unique, : );

			$$ = false;

		DBG_PRINT}}
	| UNIQUE
		{{ DBG_TRACE_GRAMMAR(opt_unique, | UNIQUE);

			$$ = true;

		DBG_PRINT}}
	;

opt_index_column_name_list
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_index_column_name_list, : );

			$$ = NULL;

		DBG_PRINT}}
	| index_column_name_list
		{{ DBG_TRACE_GRAMMAR(opt_index_column_name_list, | index_column_name_list);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

index_column_name_list
	: '(' sort_spec_list ')'
		{{ DBG_TRACE_GRAMMAR(index_column_name_list, '(' sort_spec_list ')');
			if (parser_get_is_reverse())
			{
			  PT_NODE *node;
			  for (node = $2; node != NULL; node = node->next)
			  {
			     node->info.sort_spec.asc_or_desc = PT_DESC;
			  }
			}

		      $$ = $2;
		      PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

update_statistics_stmt
	: UPDATE STATISTICS ON_ only_class_name_list opt_with_fullscan
		{{ DBG_TRACE_GRAMMAR(update_statistics_stmt, : UPDATE STATISTICS ON_ only_class_name_list opt_with_fullscan);

			PT_NODE *ups = parser_new_node (this_parser, PT_UPDATE_STATS);
			if (ups)
			  {
			    ups->info.update_stats.class_list = $4;
			    ups->info.update_stats.all_classes = 0;
			    ups->info.update_stats.with_fullscan = $5;
			  }
			$$ = ups;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| UPDATE STATISTICS ON_ ALL CLASSES opt_with_fullscan
		{{ DBG_TRACE_GRAMMAR(update_statistics_stmt, | UPDATE STATISTICS ON_ ALL CLASSES opt_with_fullscan);

			PT_NODE *ups = parser_new_node (this_parser, PT_UPDATE_STATS);
			if (ups)
			  {
			    ups->info.update_stats.class_list = NULL;
			    ups->info.update_stats.all_classes = 1;
			    ups->info.update_stats.with_fullscan = $6;
			  }
			$$ = ups;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| UPDATE STATISTICS ON_ CATALOG CLASSES opt_with_fullscan
		{{ DBG_TRACE_GRAMMAR(update_statistics_stmt, | UPDATE STATISTICS ON_ CATALOG CLASSES opt_with_fullscan);

			PT_NODE *ups = parser_new_node (this_parser, PT_UPDATE_STATS);
			if (ups)
			  {
			    ups->info.update_stats.class_list = NULL;
			    ups->info.update_stats.all_classes = -1;
			    ups->info.update_stats.with_fullscan = $6;
			  }
			$$ = ups;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

only_class_name_list
	: only_class_name_list ',' only_class_name
		{{ DBG_TRACE_GRAMMAR(only_class_name_list, : only_class_name_list ',' only_class_name);

			$$ = parser_make_link ($1, $3);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| only_class_name
		{{ DBG_TRACE_GRAMMAR(only_class_name_list, | only_class_name);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

opt_invisible
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_invisible, : );
 			$$ = false;

		DBG_PRINT}}
	| INVISIBLE
		{{ DBG_TRACE_GRAMMAR(opt_invisible, | INVISIBLE);

			$$ = true;

		DBG_PRINT}}
	;


opt_with_fullscan
        : /* empty */
                {{ DBG_TRACE_GRAMMAR(opt_with_fullscan, : );

                        $$ = 0;

                DBG_PRINT}}
        | WITH FULLSCAN
                {{ DBG_TRACE_GRAMMAR(opt_with_fullscan, |  WITH FULLSCAN);

                        $$ = 1;

                DBG_PRINT}}
        ;

opt_with_online
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_with_online, : );
			$$ = 0;

		DBG_PRINT}}
	| WITH ONLINE
		{{ DBG_TRACE_GRAMMAR(opt_with_online, | WITH ONLINE);

			$$ = 1;  // thread count is 0

		DBG_PRINT}}
	| WITH ONLINE PARALLEL unsigned_integer
		{{ DBG_TRACE_GRAMMAR(opt_with_online, |  WITH ONLINE PARALLEL unsigned_integer);
                        const int MIN_COUNT = 1;
                        const int MAX_COUNT = 16;
                        int thread_count = $4->info.value.data_value.i;
                        if (thread_count < MIN_COUNT || thread_count > MAX_COUNT)
                          {
                            // todo - might be better to have a node here
                            pt_cat_error (this_parser, NULL, MSGCAT_SET_PARSER_SYNTAX,
                                          MSGCAT_SYNTAX_INVALID_PARALLEL_ARGUMENT, MIN_COUNT, MAX_COUNT);
                          }
			$$ = thread_count + 1;
		DBG_PRINT}}
	;

opt_of_to_eq
	: /* empty */
	| TO
	| '='
	;

opt_level_spec
	: ON_
		{{ DBG_TRACE_GRAMMAR(opt_level_spec, : ON_ );

			PT_NODE *val = parser_new_node (this_parser, PT_VALUE);
			if (val)
			  val->info.value.data_value.i = -1;
			$$ = val;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| OFF_
		{{ DBG_TRACE_GRAMMAR(opt_level_spec, | OFF_ );

			PT_NODE *val = parser_new_node (this_parser, PT_VALUE);
			if (val)
			  val->info.value.data_value.i = 0;
			$$ = val;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| unsigned_integer
		{{ DBG_TRACE_GRAMMAR(opt_level_spec, |  unsigned_integer);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| param_
		{{ DBG_TRACE_GRAMMAR(opt_level_spec, |  param_ );

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| host_param_input
		{{ DBG_TRACE_GRAMMAR(opt_level_spec, |  host_param_input);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

char_string_literal_list
	: char_string_literal_list ',' char_string_literal
		{{ DBG_TRACE_GRAMMAR(char_string_literal_list, : char_string_literal_list ',' char_string_literal );

			$$ = parser_make_link ($1, $3);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| char_string_literal
		{{ DBG_TRACE_GRAMMAR(char_string_literal_list, | char_string_literal);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

table_spec_list
	: table_spec_list  ',' table_spec
		{{ DBG_TRACE_GRAMMAR(table_spec_list, : table_spec_list  ',' table_spec);

			$$ = parser_make_link ($1, $3);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| table_spec
		{{ DBG_TRACE_GRAMMAR(table_spec_list, |  table_spec);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

extended_table_spec_list
	: extended_table_spec_list ',' table_spec
		{{ DBG_TRACE_GRAMMAR(extended_table_spec_list, : extended_table_spec_list ',' table_spec);

			container_2 ctn;
			PT_NODE *n1 = CONTAINER_AT_0 ($1);
			PT_NODE *n2 = $3;
			int number = (int) TO_NUMBER (CONTAINER_AT_1 ($1));
			SET_CONTAINER_2 (ctn, parser_make_link (n1, n2), FROM_NUMBER (number));
			$$ = ctn;

		DBG_PRINT}}
	| extended_table_spec_list join_table_spec
		{{ DBG_TRACE_GRAMMAR(extended_table_spec_list, | extended_table_spec_list join_table_spec);

			container_2 ctn;
			PT_NODE *n1 = CONTAINER_AT_0 ($1);
			PT_NODE *n2 = $2;
			SET_CONTAINER_2 (ctn, parser_make_link (n1, n2), FROM_NUMBER (1));
			$$ = ctn;

		DBG_PRINT}}
	| '(' extended_table_spec_list join_table_spec ')'
		{{ DBG_TRACE_GRAMMAR(extended_table_spec_list, | '(' extended_table_spec_list join_table_spec ')');

			container_2 ctn;
			PT_NODE *n1 = CONTAINER_AT_0 ($2);
			PT_NODE *n2 = $3;
			SET_CONTAINER_2 (ctn, parser_make_link (n1, n2), FROM_NUMBER (1));
			$$ = ctn;

		DBG_PRINT}}
	| table_spec
		{{ DBG_TRACE_GRAMMAR(extended_table_spec_list, | table_spec);

			container_2 ctn;
			SET_CONTAINER_2 (ctn, $1, FROM_NUMBER (0));
			$$ = ctn;

		DBG_PRINT}}
	;

join_table_spec
	: CROSS JOIN table_spec
		{{ DBG_TRACE_GRAMMAR(join_table_spec, : CROSS JOIN table_spec);

			PT_NODE *sopt = $3;
			if (sopt)
			  sopt->info.spec.join_type = PT_JOIN_CROSS;
			$$ = sopt;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| opt_of_inner_left_right JOIN table_spec join_condition
		{{ DBG_TRACE_GRAMMAR(join_table_spec, | opt_of_inner_left_right JOIN table_spec join_condition);

			PT_NODE *sopt = $3;
			bool natural = false;

			if ($4 == NULL)
			  {
				/* Not exists ON condition, if it is outer join, report error */
				if ($1 == PT_JOIN_LEFT_OUTER
				    || $1 == PT_JOIN_RIGHT_OUTER
				    || $1 == PT_JOIN_FULL_OUTER)
				  {
					PT_ERRORm(this_parser, sopt,
						  MSGCAT_SET_PARSER_SYNTAX,
						  MSGCAT_SYNTAX_OUTER_JOIN_REQUIRES_JOIN_COND);
				  }
			  }
			if (sopt)
			  {
			    sopt->info.spec.natural = natural;
			    sopt->info.spec.join_type = $1;
			    sopt->info.spec.on_cond = $4;
			  }
			$$ = sopt;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
			parser_restore_pseudoc ();

		DBG_PRINT}}
	| NATURAL opt_of_inner_left_right JOIN table_spec
		{{ DBG_TRACE_GRAMMAR(join_table_spec, | NATURAL opt_of_inner_left_right JOIN table_spec );

			PT_NODE *sopt = $4;
			bool natural = true;

			if (sopt)
			  {
			    sopt->info.spec.natural = natural;
			    sopt->info.spec.join_type = $2;
			    sopt->info.spec.on_cond = NULL;
			  }
			$$ = sopt;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
		DBG_PRINT}}
	;

join_condition
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(join_condition, : );
			parser_save_and_set_pseudoc (0);
			$$ = NULL;   /* just return NULL */
		DBG_PRINT}}
	| ON_
		{{
			parser_save_and_set_pseudoc (0);
			parser_save_and_set_wjc (1);
			parser_save_and_set_ic (1);
		DBG_PRINT}}
	  search_condition
		{{ DBG_TRACE_GRAMMAR(join_condition, | ON_ search_condition);
			PT_NODE *condition = $3;
			bool instnum_flag = false;

			parser_restore_wjc ();
			parser_restore_ic ();

			(void) parser_walk_tree (this_parser, condition,
									 pt_check_instnum_pre, NULL,
									 pt_check_instnum_post, &instnum_flag);
			if (instnum_flag)
			  {
			    PT_ERRORmf(this_parser, condition, MSGCAT_SET_PARSER_SEMANTIC,
					       MSGCAT_SEMANTIC_EXPR_NOT_ALLOWED_IN_JOIN_COND,
					       "INST_NUM()/ROWNUM");
			  }

			$$ = condition;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
		DBG_PRINT}}


	;

opt_of_inner_left_right
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_of_inner_left_right, : );
			$$ = PT_JOIN_INNER;

		DBG_PRINT}}
	| INNER opt_outer
		{{ DBG_TRACE_GRAMMAR(opt_of_inner_left_right, | INNER opt_outer );

			$$ = PT_JOIN_INNER;

		DBG_PRINT}}
	| LEFT opt_outer
		{{ DBG_TRACE_GRAMMAR(opt_of_inner_left_right, | LEFT opt_outer );

			$$ = PT_JOIN_LEFT_OUTER;

		DBG_PRINT}}
	| RIGHT opt_outer
		{{ DBG_TRACE_GRAMMAR(opt_of_inner_left_right, |  RIGHT opt_outer );

			$$ = PT_JOIN_RIGHT_OUTER;

		DBG_PRINT}}
	;

opt_outer
	: /* empty */
	| OUTER
	;

table_spec
	: '(' table_spec ')' %dprec 1
		{{ DBG_TRACE_GRAMMAR(table_spec, : '(' table_spec ')' );

			$$ = $2;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| original_table_spec %dprec 2
		{{ DBG_TRACE_GRAMMAR(table_spec, | original_table_spec);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}

original_table_spec
	: class_spec opt_as_identifier_attr_name opt_table_spec_index_hint_list
		{{ DBG_TRACE_GRAMMAR(original_table_spec, : class_spec opt_as_identifier_attr_name opt_table_spec_index_hint_list );
			PT_NODE *range_var = NULL;
			PT_NODE *ent = $1;
			if (ent)
			  {
			    PT_NODE *stmt = parser_is_hint_node_empty () ? NULL : parser_pop_hint_node ();

			    if (stmt)
			      {
				if (stmt->node_type == PT_MERGE
				    && ent->info.spec.only_all != PT_ONLY)
				  {
				    PT_ERRORm (this_parser, ent,
					       MSGCAT_SET_PARSER_SEMANTIC,
					       MSGCAT_SEMANTIC_MERGE_HIERARCHY_NOT_ALLOWED);
				  }
#if 1 // ctshim 
 //                             parser_top_select_stmt_node()->node_type != PT_SELECT ?
                                if(stmt->node_type != PT_SELECT && ent->info.spec.ct_server_name != NULL)
                                {                                   
                                    PT_ERROR (this_parser, ent, "Oops! Sorr,. only SELECT statements are supported yet."); // ctshim
                                }
#endif                                
			      }

			    range_var = CONTAINER_AT_0 ($2);
			    if (range_var != NULL)
			      {
					if (ent->info.spec.range_var != NULL)
					  {
						parser_free_tree (this_parser,
										  ent->info.spec.range_var);
					  }
					ent->info.spec.range_var = CONTAINER_AT_0 ($2);
			      }
			    ent->info.spec.as_attr_list = CONTAINER_AT_1 ($2);

			    if ($3)
			      {
				PT_NODE *hint = NULL, *alias = NULL;
				const char *qualifier_name = NULL;

				/* Get qualifier */
				alias = CONTAINER_AT_0 ($2);
				if (alias)
				  {
				    qualifier_name = alias->info.name.original;
				  }
				else if (ent->info.spec.entity_name != NULL)
				  {
				    qualifier_name = ent->info.spec.entity_name->info.name.original;
				  }

				/* Resolve table spec index hint names */
				hint = $3;
				while (hint && qualifier_name)
				  {
				    hint->info.name.resolved =
				      pt_append_string (this_parser, NULL, qualifier_name);

				    hint = hint->next;
				  }

				/* This is an index hint inside a table_spec. Copy index
				   name list to USING INDEX clause */
				if (stmt)
				  {
				    /* copy to using_index */
				    switch (stmt->node_type)
				      {
					case PT_SELECT:
					  stmt->info.query.q.select.using_index =
					    parser_make_link ($3, stmt->info.query.q.select.using_index);
					break;

					case PT_DELETE:
					  stmt->info.delete_.using_index =
					    parser_make_link ($3, stmt->info.delete_.using_index);
					break;

					case PT_UPDATE:
					  stmt->info.update.using_index =
					    parser_make_link ($3, stmt->info.update.using_index);
					  break;

					default:
					  /* if index hint has been specified in
					   * table_spec clause and statement is not
					   * SELECT, UPDATE or DELETE raise error
					   */
					  PT_ERRORm (this_parser, ent,
					    MSGCAT_SET_PARSER_SYNTAX,
					    MSGCAT_SYNTAX_INVALID_INDEX_HINT);
					break;
				      }
				  }
			      }

			    if (stmt)
			      {
				/* push back node */
				parser_push_hint_node (stmt);
			      }
			  }

			$$ = ent;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| meta_class_spec opt_as_identifier_attr_name
		{{ DBG_TRACE_GRAMMAR(original_table_spec, | meta_class_spec opt_as_identifier_attr_name);

			PT_NODE *ent = $1;
			if (ent)
			  {
			    PT_NODE *stmt = parser_pop_hint_node ();
			    if (stmt && (stmt->node_type == PT_DELETE || stmt->node_type == PT_UPDATE
					 || stmt->node_type == PT_MERGE))
			      {
			        PT_NODE *sel = $1;
			        if (sel && sel->node_type == PT_SELECT)
			          {
				    PT_SELECT_INFO_CLEAR_FLAG (sel, PT_SELECT_INFO_DUMMY);
				  }
			      }
			    parser_push_hint_node (stmt);

			    ent->info.spec.range_var = CONTAINER_AT_0 ($2);
			    ent->info.spec.as_attr_list = CONTAINER_AT_1 ($2);

			    parser_remove_dummy_select (&ent);
			  }
			$$ = ent;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| subquery opt_as_identifier_attr_name
		{{ DBG_TRACE_GRAMMAR(original_table_spec, | subquery opt_as_identifier_attr_name);

			PT_NODE *ent = parser_new_node (this_parser, PT_SPEC);
			if (ent)
			  {
			    PT_NODE *stmt = parser_pop_hint_node ();
			    if (stmt && (stmt->node_type == PT_DELETE || stmt->node_type == PT_UPDATE
					 || stmt->node_type == PT_MERGE))
			      {
			        PT_NODE *sel = $1;
				if (sel && sel->node_type == PT_SELECT)
				  {
				    PT_SELECT_INFO_CLEAR_FLAG (sel, PT_SELECT_INFO_DUMMY);
				  }
			      }
			    parser_push_hint_node (stmt);

			    ent->info.spec.derived_table = $1;
			    ent->info.spec.derived_table_type = PT_IS_SUBQUERY;

			    ent->info.spec.range_var = CONTAINER_AT_0 ($2);
			    ent->info.spec.as_attr_list = CONTAINER_AT_1 ($2);

			    parser_remove_dummy_select (&ent);
			  }
			$$ = ent;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| TABLE '(' expression_ ')' opt_as_identifier_attr_name
		{{ DBG_TRACE_GRAMMAR(original_table_spec, | TABLE '(' expression_ ')' opt_as_identifier_attr_name);

			PT_NODE *ent = parser_new_node (this_parser, PT_SPEC);
			if (ent)
			  {
			    ent->info.spec.derived_table = $3;
			    ent->info.spec.derived_table_type = PT_IS_SET_EXPR;

			    ent->info.spec.range_var = CONTAINER_AT_0 ($5);
			    ent->info.spec.as_attr_list = CONTAINER_AT_1 ($5);

			    parser_remove_dummy_select (&ent);
			  }
			$$ = ent;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| JSON_TABLE json_table_rule opt_as identifier
		{{ DBG_TRACE_GRAMMAR(original_table_spec, | JSON_TABLE json_table_rule opt_as identifier);
			PT_NODE *ent = parser_new_node (this_parser, PT_SPEC);
			if (ent)
			  {
			    ent->info.spec.derived_table = $2;  // json_table_rule
			    ent->info.spec.derived_table_type = PT_DERIVED_JSON_TABLE;
			    ent->info.spec.range_var = $4;      // identifier
			  }
			$$ = ent;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
        | DBLINK  '('  dblink_expr ')'   dblink_identifier_col_attrs 
                {{ DBG_TRACE_GRAMMAR(original_table_spec, | DBLINK  '('  dblink_expr ')' dblink_identifier_col_attrs );                       
			PT_NODE *ent = parser_new_node (this_parser, PT_SPEC);
			if (ent)
			  {
			    ent->info.spec.derived_table = $3;  // dblink_expr
			    ent->info.spec.derived_table_type = PT_DERIVED_DBLINK_TABLE;                            
			    ent->info.spec.range_var = CONTAINER_AT_0 ($5); // table name                                                        
			    ent->info.spec.derived_table->info.dblink_table.cols = CONTAINER_AT_1 ($5); // def. columns 
			  }
			$$ = ent;                        
		        PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
		DBG_PRINT}}
	;

opt_table_spec_index_hint
	: USE index_or_key '(' identifier_list ')'
		{{ DBG_TRACE_GRAMMAR(opt_table_spec_index_hint, : USE index_or_key '(' identifier_list ')' );

			PT_NODE *list = $4;
			while (list)
			  {
			    list->info.name.meta_class = PT_INDEX_NAME;
			    list = list->next;
			  }

			$$ = $4;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| FORCE index_or_key '(' identifier_list ')'
		{{ DBG_TRACE_GRAMMAR(opt_table_spec_index_hint, | FORCE index_or_key '(' identifier_list ')' );

			PT_NODE *list = $4;
			while (list)
			  {
			    list->info.name.meta_class = PT_INDEX_NAME;
			    list->etc = (void *) PT_IDX_HINT_FORCE;
			    list = list->next;
			  }

			$$ = $4;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| IGNORE_ index_or_key '(' identifier_list ')'
		{{ DBG_TRACE_GRAMMAR(opt_table_spec_index_hint, | IGNORE_ index_or_key '(' identifier_list ')' );

			PT_NODE *list = $4;
			while (list)
			  {
			    list->info.name.meta_class = PT_INDEX_NAME;
			    list->etc = (void *) PT_IDX_HINT_IGNORE;
			    list = list->next;
			  }

			  $$ = $4;
			  PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

opt_table_spec_index_hint_list
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_table_spec_index_hint_list, :);

			$$ = 0;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| opt_table_spec_index_hint_list ',' opt_table_spec_index_hint
		{{ DBG_TRACE_GRAMMAR(opt_table_spec_index_hint_list, | opt_table_spec_index_hint_list ',' opt_table_spec_index_hint );

			$$ = parser_make_link($1, $3);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos);

		DBG_PRINT}}
	| opt_table_spec_index_hint
		{{ DBG_TRACE_GRAMMAR(opt_table_spec_index_hint_list, | opt_table_spec_index_hint );

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos);

		DBG_PRINT}}
	;

opt_as_identifier_attr_name
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_as_identifier_attr_name, : );

			container_2 ctn;
			SET_CONTAINER_2 (ctn, NULL, NULL);
			$$ = ctn;

		DBG_PRINT}}
	| opt_as identifier '(' identifier_list ')'
		{{ DBG_TRACE_GRAMMAR(opt_as_identifier_attr_name, | opt_as identifier '(' identifier_list ')' );

			container_2 ctn;
			SET_CONTAINER_2 (ctn, $2, $4);
			$$ = ctn;

		DBG_PRINT}}
	| opt_as identifier
		{{ DBG_TRACE_GRAMMAR(opt_as_identifier_attr_name, | opt_as identifier);

			container_2 ctn;
			SET_CONTAINER_2 (ctn, $2, NULL);
			$$ = ctn;

		DBG_PRINT}}
	;

opt_as
	: /* empty */
	| AS
	;

class_spec_list
	: class_spec_list  ',' class_spec
		{{ DBG_TRACE_GRAMMAR(class_spec_list, : class_spec_list  ',' class_spec );

			$$ = parser_make_link ($1, $3);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| class_spec
		{{ DBG_TRACE_GRAMMAR(class_spec_list, | class_spec );

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

class_spec
	: only_all_class_spec
		{{ DBG_TRACE_GRAMMAR(class_spec, : only_all_class_spec );

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| '(' only_all_class_spec_list ')'
		{{ DBG_TRACE_GRAMMAR(class_spec, | '(' only_all_class_spec_list ')' );

			$$ = $2;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

only_all_class_spec_list
	: only_all_class_spec_list ',' only_all_class_spec
		{{ DBG_TRACE_GRAMMAR(only_all_class_spec_list, : only_all_class_spec_list ',' only_all_class_spec );

			PT_NODE *result = parser_make_link ($1, $3);
			PT_NODE *p = parser_new_node (this_parser, PT_SPEC);
			if (p)
			  p->info.spec.entity_name = result;
			$$ = p;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| only_all_class_spec
		{{ DBG_TRACE_GRAMMAR(only_all_class_spec_list, | only_all_class_spec );

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

meta_class_spec
	: CLASS only_class_name
		{{ DBG_TRACE_GRAMMAR(meta_class_spec, : CLASS only_class_name );

			PT_NODE *ocs = parser_new_node (this_parser, PT_SPEC);
			if (ocs)
			  {
			    ocs->info.spec.entity_name = $2;
			    ocs->info.spec.only_all = PT_ONLY;
			    ocs->info.spec.meta_class = PT_CLASS;
			  }

			if (ocs)
			  ocs->info.spec.meta_class = PT_META_CLASS;
			$$ = ocs;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

only_all_class_spec
	: only_class_name opt_partition_spec
		{{ DBG_TRACE_GRAMMAR(only_all_class_spec, : only_class_name opt_partition_spec );

			PT_NODE *ocs = parser_new_node (this_parser, PT_SPEC);
			if (ocs)
			  {
			    ocs->info.spec.entity_name = $1;
			    ocs->info.spec.only_all = PT_ONLY;
			    ocs->info.spec.meta_class = PT_CLASS;
			    if ($2)
			      {
				ocs->info.spec.partition = $2;
			      }
			  }

			$$ = ocs;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| ALL class_name '(' EXCEPT class_spec_list ')'
		{{ DBG_TRACE_GRAMMAR(only_all_class_spec, | ALL class_name '(' EXCEPT class_spec_list ')' );

			PT_NODE *acs = parser_new_node (this_parser, PT_SPEC);
			if (acs)
			  {
			    acs->info.spec.entity_name = $2;
			    acs->info.spec.only_all = PT_ALL;
			    acs->info.spec.meta_class = PT_CLASS;

			    acs->info.spec.except_list = $5;
			  }
			$$ = acs;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| ALL class_name
		{{ DBG_TRACE_GRAMMAR(only_all_class_spec, | class_name);

			PT_NODE *acs = parser_new_node (this_parser, PT_SPEC);
			if (acs)
			  {
			    acs->info.spec.entity_name = $2;
			    acs->info.spec.only_all = PT_ALL;
			    acs->info.spec.meta_class = PT_CLASS;
			  }

			$$ = acs;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
        | class_name_with_server_name  opt_partition_spec 
                {{ DBG_TRACE_GRAMMAR(only_all_class_spec, | class_name_with_server_name opt_partition_spec);
#if 1 // ctshim                    
                        PT_NODE *scs = parser_new_node (this_parser, PT_SPEC);
			if (scs)
			  {
                            // $1 : class_name -> server_name    
			    scs->info.spec.entity_name = $1;
                            scs->info.spec.ct_server_name = $1->next;
                            $1->next = NULL;

			    scs->info.spec.only_all = PT_ONLY;
			    scs->info.spec.meta_class = PT_CLASS;
			    if ($2)
			      {
				scs->info.spec.partition = $2;
			      }
			  }
                    
                        $$ = scs;
#endif                        
                        
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
                DBG_PRINT}}       
	;

class_name_with_server_name
        : class_name '@' dblink_server_name
                {{DBG_TRACE_GRAMMAR(class_name_with_server_name, : class_name '@' dblink_server_name);
                assert($1 != NULL && $3 != NULL);
#if 1 // ctshim

                PT_NODE *name_node =  $3;
		PT_NODE *owner_node = $3->next;

                //if (owner_node != NULL)
                //  {
                //     name_node->info.name.resolved = pt_append_string (this_parser, NULL,
                //                                                    owner_node->info.name.original);
                //     name_node->next = NULL;
                //     parser_free_tree (this_parser, owner_node);
                //  }

                $1->next = name_node;
                $$ = $1;
#endif                
                DBG_PRINT}}
        ;

object_name
	: identifier DOT identifier
		{{ DBG_TRACE_GRAMMAR(object_name, : identifier DOT identifier);

			PT_NODE *qualifier = $1;
			PT_NODE *node = $3;

			if (qualifier && node)
			  {
			    node->info.name.resolved = pt_append_string (this_parser, NULL, qualifier->info.name.original);
			    parser_free_tree (this_parser, qualifier);
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| identifier
		{{ DBG_TRACE_GRAMMAR(object_name, | identifier);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

user_specified_name_without_dot
	: identifier_without_dot DOT identifier_without_dot
		{{

			PT_NODE *user = $1;
			PT_NODE *name = $3;

			if (user && name)
			  {
			    name->info.name.resolved = pt_append_string (this_parser, NULL, user->info.name.original);
			    parser_free_tree (this_parser, user);

			    PT_NAME_INFO_SET_FLAG (name, PT_NAME_INFO_USER_SPECIFIED);
			  }

			$$ = name;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| identifier_without_dot
		{{

			PT_NODE *name = $1;

			if (name)
			  {
			    PT_NAME_INFO_SET_FLAG (name, PT_NAME_INFO_USER_SPECIFIED);
			  }

			$$ = name;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

user_specified_name
	: object_name
		{{

			PT_NODE *name = $1;

			if (name)
			  {
			    PT_NAME_INFO_SET_FLAG (name, PT_NAME_INFO_USER_SPECIFIED);
			  }

			$$ = name;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

class_name_without_dot
	: user_specified_name_without_dot
		{ $$ = $1; }
	;

class_name
	: user_specified_name
		{ $$ = $1; }
	;

class_name_list
	: class_name_list ',' class_name
		{{ DBG_TRACE_GRAMMAR(class_name_list, : class_name_list ',' class_name);

			$$ = parser_make_link($1, $3);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| class_name
		{{ DBG_TRACE_GRAMMAR(class_name_list, | class_name);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

index_name
	: object_name
		{{

			PT_NODE *node = $1;

			if (node)
			  {
				node->info.name.meta_class = PT_INDEX_NAME;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

trigger_name_without_dot
	: user_specified_name_without_dot
		{ $$ = $1; }
	;

trigger_name
	: user_specified_name
		{ $$ = $1; }
	;

trigger_name_list
	: trigger_name_list ',' trigger_name
		{{

			$$ = parser_make_link($1, $3);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| trigger_name
		{{

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

serial_name_without_dot
	: user_specified_name_without_dot
		{ $$ = $1; }
	;

serial_name
	: user_specified_name
		{ $$ = $1; }
	;

opt_partition_spec
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_partition_spec, : );
		    $$ = NULL;
		}}
	| PARTITION '(' identifier ')'
		{{ DBG_TRACE_GRAMMAR(opt_partition_spec, | PARTITION '(' identifier ')' );
			$$ = $3;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
		DBG_PRINT}}
	;

opt_class_type
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_class_type, :);

			$$ = PT_EMPTY;

		DBG_PRINT}}
	| VCLASS
		{{ DBG_TRACE_GRAMMAR(opt_class_type, | VCLASS);

			$$ = PT_VCLASS;

		DBG_PRINT}}
	| VIEW
		{{ DBG_TRACE_GRAMMAR(opt_class_type, | VIEW );

			$$ = PT_VCLASS;

		DBG_PRINT}}
	| CLASS
		{{ DBG_TRACE_GRAMMAR(opt_class_type, | CLASS );

			$$ = PT_CLASS;

		DBG_PRINT}}
	| TABLE
		{{ DBG_TRACE_GRAMMAR(opt_class_type, | TABLE );

			$$ = PT_CLASS;

		DBG_PRINT}}
	;

opt_table_type
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_table_type, : );

			$$ = PT_EMPTY;

		DBG_PRINT}}
	| CLASS
		{{ DBG_TRACE_GRAMMAR(opt_table_type, | CLASS );

			$$ = PT_CLASS;

		DBG_PRINT}}
	| TABLE
		{{ DBG_TRACE_GRAMMAR(opt_table_type, | TABLE );

			$$ = PT_CLASS;

		DBG_PRINT}}
	;

opt_cascade
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_cascade, : );

			$$ = false;

		DBG_PRINT}}
	| CASCADE
		{{ DBG_TRACE_GRAMMAR(opt_cascade, | CASCADE );

			$$ = true;

		DBG_PRINT}}
	;

opt_cascade_constraints
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_cascade_constraints, : );

			$$ = false;

		DBG_PRINT}}
	| CASCADE CONSTRAINTS
		{{ DBG_TRACE_GRAMMAR(opt_cascade_constraints, | CASCADE CONSTRAINTS );

			$$ = true;

		DBG_PRINT}}
	;

alter_clause_for_alter_list
	: ADD       alter_add_clause_for_alter_list
	| DROP     alter_drop_clause_for_alter_list
	| DROP     alter_drop_clause_mysql_specific
	| RENAME alter_rename_clause_mysql_specific
	| RENAME alter_rename_clause_allow_multiple opt_resolution_list_for_alter
	| ALTER  alter_column_clause_mysql_specific
	|        alter_partition_clause_for_alter_list
	|        alter_auto_increment_mysql_specific
	| MODIFY alter_modify_clause_for_alter_list
	| CHANGE alter_change_clause_for_alter_list
	| OWNER TO identifier
		{{ DBG_TRACE_GRAMMAR(alter_clause_for_alter_list, | OWNER TO identifier );
			PT_NODE *alt = parser_get_alter_node();

			if (alt)
			  {
			    alt->info.alter.code = PT_CHANGE_OWNER;
			    alt->info.alter.alter_clause.user.user_name = $3;
			  }
		DBG_PRINT}}
	| charset_spec opt_collation
		{{ DBG_TRACE_GRAMMAR(alter_clause_for_alter_list, | charset_spec opt_collation );
			PT_NODE *node = parser_get_alter_node();
			PT_NODE *cs_node, *coll_node;
			int charset, coll_id;

			cs_node = $1;
			coll_node = $2;

			if (node)
			  {
			    node->info.alter.alter_clause.collation.charset = -1;
			    node->info.alter.alter_clause.collation.collation_id = -1;
			  }

			if (pt_check_grammar_charset_collation (this_parser, cs_node,
								coll_node, &charset, &coll_id) == NO_ERROR)
			  {
			    if (node)
			      {
				node->info.alter.code = PT_CHANGE_COLLATION;
				node->info.alter.alter_clause.collation.charset = charset;

				if (coll_node)
				  {
				    node->info.alter.alter_clause.collation.collation_id = coll_id;
				  }
			      }
			  }
		DBG_PRINT}}
	| collation_spec
		{{ DBG_TRACE_GRAMMAR(alter_clause_for_alter_list, | collation_spec );
			PT_NODE *node = parser_get_alter_node();
			PT_NODE *coll_node;
			int charset, coll_id;

			coll_node = $1;

			if (node)
			  {
			    node->info.alter.alter_clause.collation.charset = -1;
			    node->info.alter.alter_clause.collation.collation_id = -1;
			  }

			if (pt_check_grammar_charset_collation (this_parser, NULL,
								coll_node, &charset, &coll_id) == NO_ERROR)
			  {
			    if (node)
			      {
				node->info.alter.code = PT_CHANGE_COLLATION;
				node->info.alter.alter_clause.collation.collation_id = coll_id;
			      }
			  }
		DBG_PRINT}}
	| class_comment_spec
		{{ DBG_TRACE_GRAMMAR(alter_clause_for_alter_list, | class_comment_spec );
			PT_NODE *alter_node = parser_get_alter_node();

			if (alter_node != NULL && alter_node->info.alter.code != PT_CHANGE_COLUMN_COMMENT)
			  {
				alter_node->info.alter.code = PT_CHANGE_TABLE_COMMENT;
				alter_node->info.alter.alter_clause.comment.tbl_comment = $1;
			  }
		DBG_PRINT}}
	;

alter_clause_cubrid_specific
	: ADD       alter_add_clause_cubrid_specific opt_resolution_list_for_alter
	| DROP     alter_drop_clause_cubrid_specific opt_resolution_list_for_alter
	| RENAME alter_rename_clause_cubrid_specific opt_resolution_list_for_alter
	| CHANGE alter_change_clause_cubrid_specific
	| ADD       alter_add_clause_for_alter_list      resolution_list_for_alter
	| DROP     alter_drop_clause_for_alter_list      resolution_list_for_alter
	| inherit_resolution_list
		{{ DBG_TRACE_GRAMMAR(alter_clause_cubrid_specific, | inherit_resolution_list);

			PT_NODE *alt = parser_get_alter_node ();

			if (alt)
			  {
			    alt->info.alter.code = PT_RENAME_RESOLUTION;
			    alt->info.alter.super.resolution_list = $1;
			  }

		DBG_PRINT}}
	;

opt_resolution_list_for_alter
	: /* [empty] */
	| resolution_list_for_alter
	;

resolution_list_for_alter
	: inherit_resolution_list
		{{ DBG_TRACE_GRAMMAR(resolution_list_for_alter, : inherit_resolution_list );

			PT_NODE *alt = parser_get_alter_node ();

			if (alt)
			  {
			    alt->info.alter.super.resolution_list = $1;
			  }

		DBG_PRINT}}
	;

alter_rename_clause_mysql_specific
	: opt_to only_class_name
		{{ DBG_TRACE_GRAMMAR(alter_rename_clause_mysql_specific, : opt_to only_class_name );

			PT_NODE *node = parser_get_alter_node ();

			if (node)
			  {
			    node->info.alter.code = PT_RENAME_ENTITY;
			    node->info.alter.alter_clause.rename.new_name = $2;
			  }

		DBG_PRINT}}
	;

alter_auto_increment_mysql_specific
	: AUTO_INCREMENT '=' UNSIGNED_INTEGER
	      {{ DBG_TRACE_GRAMMAR(alter_auto_increment_mysql_specific, : AUTO_INCREMENT '=' UNSIGNED_INTEGER );
			PT_NODE *node = parser_get_alter_node ();
    			PT_NODE *val = parser_new_node (this_parser, PT_VALUE);

			if (node)
			  {
			    if (val)
			      {
				val->info.value.data_value.str =
				  pt_append_bytes (this_parser, NULL, $3,
						   strlen ($3));
				val->type_enum = PT_TYPE_NUMERIC;
				PT_NODE_PRINT_VALUE_TO_TEXT (this_parser, val);
			      }

			    node->info.alter.code = PT_CHANGE_AUTO_INCREMENT;
			    node->info.alter.alter_clause.auto_increment.start_value = val;
			  }

	      DBG_PRINT}}
	;

alter_rename_clause_allow_multiple
	: opt_of_attr_column_method opt_class identifier as_or_to identifier
		{{ DBG_TRACE_GRAMMAR(alter_rename_clause_allow_multiple, : opt_of_attr_column_method opt_class identifier as_or_to identifier);

			PT_NODE *node = parser_get_alter_node ();
			PT_MISC_TYPE etyp = $1;

			if (node)
			  {
			    node->info.alter.code = PT_RENAME_ATTR_MTHD;

			    if (etyp == PT_EMPTY)
			      etyp = PT_ATTRIBUTE;

			    node->info.alter.alter_clause.rename.element_type = etyp;
			    if ($2)
			      node->info.alter.alter_clause.rename.meta = PT_META_ATTR;
			    else
			      node->info.alter.alter_clause.rename.meta = PT_NORMAL;

			    node->info.alter.alter_clause.rename.new_name = $5;
			    node->info.alter.alter_clause.rename.old_name = $3;
			  }

		DBG_PRINT}}
	;

alter_rename_clause_cubrid_specific
	: FUNCTION opt_identifier OF opt_class identifier AS identifier
		{{ DBG_TRACE_GRAMMAR(alter_rename_clause_cubrid_specific, : FUNCTION opt_identifier OF opt_class identifier AS identifier);

			PT_NODE *node = parser_get_alter_node ();

			if (node)
			  {
			    node->info.alter.code = PT_RENAME_ATTR_MTHD;
			    node->info.alter.alter_clause.rename.element_type = PT_FUNCTION_RENAME;
			    if ($4)
			      node->info.alter.alter_clause.rename.meta = PT_META_ATTR;
			    else
			      node->info.alter.alter_clause.rename.meta = PT_NORMAL;

			    node->info.alter.alter_clause.rename.new_name = $7;
			    node->info.alter.alter_clause.rename.mthd_name = $5;
			    node->info.alter.alter_clause.rename.old_name = $2;
			  }

		DBG_PRINT}}
	| File file_path_name AS file_path_name
		{{ DBG_TRACE_GRAMMAR(alter_rename_clause_cubrid_specific, | File file_path_name AS file_path_name);

			PT_NODE *node = parser_get_alter_node ();

			if (node)
			  {
			    node->info.alter.code = PT_RENAME_ATTR_MTHD;
			    node->info.alter.alter_clause.rename.element_type = PT_FILE_RENAME;
			    node->info.alter.alter_clause.rename.new_name = $4;
			    node->info.alter.alter_clause.rename.old_name = $2;
			  }

		DBG_PRINT}}
	;

opt_of_attr_column_method
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_of_attr_column_method,  : );

			$$ = PT_EMPTY;

		DBG_PRINT}}
	| ATTRIBUTE
		{{ DBG_TRACE_GRAMMAR(opt_of_attr_column_method, | ATTRIBUTE );

			$$ = PT_ATTRIBUTE;

		DBG_PRINT}}
	| COLUMN
		{{ DBG_TRACE_GRAMMAR(opt_of_attr_column_method, | COLUMN );

			$$ = PT_ATTRIBUTE;

		DBG_PRINT}}
	| METHOD
		{{ DBG_TRACE_GRAMMAR(opt_of_attr_column_method, | METHOD );

			$$ = PT_METHOD;
		DBG_PRINT}}
	;

opt_class
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_class, : );

			$$ = 0;

		DBG_PRINT}}
	| CLASS
		{{ DBG_TRACE_GRAMMAR(opt_class, | CLASS );

			$$ = 1;

		DBG_PRINT}}
	;

opt_identifier
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_identifier, :);

			$$ = NULL;

		DBG_PRINT}}
	| identifier
		{{ DBG_TRACE_GRAMMAR(opt_identifier, | identifier );

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

alter_add_clause_for_alter_list
	: PARTITION add_partition_clause
	| CLASS ATTRIBUTE
		{ parser_attr_type = PT_META_ATTR; }
	  '(' attr_def_list ')'
		{ parser_attr_type = PT_NORMAL; }
		{{ DBG_TRACE_GRAMMAR(alter_add_clause_for_alter_list, | CLASS ATTRIBUTE '(' attr_def_list ')' );
			PT_NODE *node = parser_get_alter_node ();
			if (node)
			  {
			    node->info.alter.code = PT_ADD_ATTR_MTHD;
			    node->info.alter.alter_clause.attr_mthd.attr_def_list = $5;
			  }

		DBG_PRINT}}
	| CLASS ATTRIBUTE
		{ parser_attr_type = PT_META_ATTR; }
	  attr_def
		{ parser_attr_type = PT_NORMAL; }
		{{ DBG_TRACE_GRAMMAR(alter_add_clause_for_alter_list, | CLASS ATTRIBUTE attr_def );

			PT_NODE *node = parser_get_alter_node ();
			if (node)
			  {
			    node->info.alter.code = PT_ADD_ATTR_MTHD;
			    node->info.alter.alter_clause.attr_mthd.attr_def_list = $4;
			  }

		DBG_PRINT}}
	| opt_of_column_attribute
	  		{ allow_attribute_ordering = true; }
	  '(' attr_def_list ')'
	  		{ allow_attribute_ordering = false; }
		{{ DBG_TRACE_GRAMMAR(alter_add_clause_for_alter_list, | opt_of_column_attribute  '(' attr_def_list ')' );

			PT_NODE *node = parser_get_alter_node ();
			if (node)
			  {
			    node->info.alter.code = PT_ADD_ATTR_MTHD;
			    node->info.alter.alter_clause.attr_mthd.attr_def_list = $4;
			  }

		DBG_PRINT}}
	| opt_of_column_attribute
			{ allow_attribute_ordering = true; }
	  attr_def
	  		{ allow_attribute_ordering = false; }
		{{ DBG_TRACE_GRAMMAR(alter_add_clause_for_alter_list, | opt_of_column_attribute attr_def );

			PT_NODE *node = parser_get_alter_node ();
			if (node)
			  {
			    node->info.alter.code = PT_ADD_ATTR_MTHD;
			    node->info.alter.alter_clause.attr_mthd.attr_def_list = $3;
			  }

		DBG_PRINT}}
	;

alter_add_clause_cubrid_specific
	: File method_file_list
		{{ DBG_TRACE_GRAMMAR(alter_add_clause_cubrid_specific, : File method_file_list );

			PT_NODE *node = parser_get_alter_node ();

			if (node)
			  {
			    node->info.alter.code = PT_ADD_ATTR_MTHD;
			    node->info.alter.alter_clause.attr_mthd.mthd_file_list = $2;
			  }

		DBG_PRINT}}
	| METHOD method_def_list
		{{ DBG_TRACE_GRAMMAR(alter_add_clause_cubrid_specific, | METHOD method_def_list);

			PT_NODE *node = parser_get_alter_node ();
			if (node)
			  {
			    node->info.alter.code = PT_ADD_ATTR_MTHD;
			    node->info.alter.alter_clause.attr_mthd.mthd_def_list = $2;
			  }

		DBG_PRINT}}
	| METHOD method_def_list File method_file_list
		{{ DBG_TRACE_GRAMMAR(alter_add_clause_cubrid_specific, | METHOD method_def_list File method_file_list);

			PT_NODE *node = parser_get_alter_node ();

			if (node)
			  {
			    node->info.alter.code = PT_ADD_ATTR_MTHD;
			    node->info.alter.alter_clause.attr_mthd.mthd_file_list = $4;
			    node->info.alter.alter_clause.attr_mthd.mthd_def_list = $2;
			  }

		DBG_PRINT}}
	| SUPERCLASS only_class_name_list
		{{ DBG_TRACE_GRAMMAR(alter_add_clause_cubrid_specific, | SUPERCLASS only_class_name_list );

			PT_NODE *node = parser_get_alter_node ();

			if (node)
			  {
			    node->info.alter.code = PT_ADD_SUPCLASS;
			    node->info.alter.super.sup_class_list = $2;
			  }

		DBG_PRINT}}
	| QUERY csql_query opt_vclass_comment_spec
		{{ DBG_TRACE_GRAMMAR(alter_add_clause_cubrid_specific, | QUERY csql_query opt_vclass_comment_spec );

			PT_NODE *node = parser_get_alter_node ();

			if (node)
			  {
			    node->info.alter.code = PT_ADD_QUERY;
			    node->info.alter.alter_clause.query.query = $2;
			    node->info.alter.alter_clause.query.view_comment = $3;
			  }

		DBG_PRINT}}
	| CLASS ATTRIBUTE
		{ parser_attr_type = PT_META_ATTR; }
	  attr_def_list_with_commas
		{ parser_attr_type = PT_NORMAL; }
		{{ DBG_TRACE_GRAMMAR(alter_add_clause_cubrid_specific, | CLASS ATTRIBUTE attr_def_list_with_commas );

			PT_NODE *node = parser_get_alter_node ();
			if (node)
			  {
			    node->info.alter.code = PT_ADD_ATTR_MTHD;
			    node->info.alter.alter_clause.attr_mthd.attr_def_list = $4;
			  }

		DBG_PRINT}}
	| opt_of_column_attribute attr_def_list_with_commas
		{{ DBG_TRACE_GRAMMAR(alter_add_clause_cubrid_specific, | opt_of_column_attribute attr_def_list_with_commas );

			PT_NODE *node = parser_get_alter_node ();
			if (node)
			  {
			    node->info.alter.code = PT_ADD_ATTR_MTHD;
			    node->info.alter.alter_clause.attr_mthd.attr_def_list = $2;
			  }

		DBG_PRINT}}
	;

opt_of_column_attribute
	: /* empty */
	| COLUMN
	| ATTRIBUTE
	;

add_partition_clause
	: PARTITIONS literal_w_o_param
		{{ DBG_TRACE_GRAMMAR(add_partition_clause, : PARTITIONS literal_w_o_param );

			PT_NODE *node = parser_get_alter_node ();
			node->info.alter.code = PT_ADD_HASHPARTITION;
			node->info.alter.alter_clause.partition.size = $2;

		DBG_PRINT}}
	| '(' partition_def_list ')'
		{{ DBG_TRACE_GRAMMAR(add_partition_clause, | '(' partition_def_list ')' );

			PT_NODE *node = parser_get_alter_node ();
			node->info.alter.code = PT_ADD_PARTITION;
			node->info.alter.alter_clause.partition.parts = $2;

		DBG_PRINT}}
	;

alter_drop_clause_mysql_specific
	: opt_reverse opt_unique index_or_key identifier
		{{ DBG_TRACE_GRAMMAR(alter_drop_clause_mysql_specific, : opt_reverse opt_unique index_or_key identifier );

			PT_NODE *node = parser_get_alter_node ();

			if (node)
			  {
			    node->info.alter.code = PT_DROP_INDEX_CLAUSE;
			    node->info.alter.alter_clause.index.reverse = $1;
			    node->info.alter.alter_clause.index.unique = $2;
			    node->info.alter.constraint_list = $4;
			  }

		DBG_PRINT}}
	| PRIMARY KEY
		{{ DBG_TRACE_GRAMMAR(alter_drop_clause_mysql_specific, | PRIMARY KEY );

			PT_NODE *node = parser_get_alter_node ();

			if (node)
			  {
			    node->info.alter.code = PT_DROP_PRIMARY_CLAUSE;
			  }

		DBG_PRINT}}
	| FOREIGN KEY identifier
		{{ DBG_TRACE_GRAMMAR(alter_drop_clause_mysql_specific, | FOREIGN KEY identifier );

			PT_NODE *node = parser_get_alter_node ();

			if (node)
			  {
			    node->info.alter.code = PT_DROP_FK_CLAUSE;
			    node->info.alter.constraint_list = $3;
			  }

		DBG_PRINT}}
	;

alter_drop_clause_for_alter_list
	: opt_of_attr_column_method normal_or_class_attr
		{{ DBG_TRACE_GRAMMAR(alter_drop_clause_for_alter_list, : opt_of_attr_column_method normal_or_class_attr );

			PT_NODE *node = parser_get_alter_node ();

			if (node)
			  {
			    node->info.alter.code = PT_DROP_ATTR_MTHD;
			    node->info.alter.alter_clause.attr_mthd.attr_mthd_name_list = $2;
			  }

		DBG_PRINT}}
	| CONSTRAINT identifier
		{{ DBG_TRACE_GRAMMAR(alter_drop_clause_for_alter_list, | CONSTRAINT identifier );

			PT_NODE *node = parser_get_alter_node ();

			if (node)
			  {
			    node->info.alter.code = PT_DROP_CONSTRAINT;
			    node->info.alter.constraint_list = $2;
			  }

		DBG_PRINT}}
	| PARTITION identifier_list
		{{ DBG_TRACE_GRAMMAR(alter_drop_clause_for_alter_list, | PARTITION identifier_list );

			PT_NODE *node = parser_get_alter_node ();

			if (node)
			  {
			    node->info.alter.code = PT_DROP_PARTITION;
			    node->info.alter.alter_clause.partition.name_list = $2;
			  }

		DBG_PRINT}}
	;

alter_drop_clause_cubrid_specific
	: opt_of_attr_column_method normal_or_class_attr_list_with_commas
		{{ DBG_TRACE_GRAMMAR(alter_drop_clause_cubrid_specific, : opt_of_attr_column_method normal_or_class_attr_list_with_commas);

			PT_NODE *node = parser_get_alter_node ();

			if (node)
			  {
			    node->info.alter.code = PT_DROP_ATTR_MTHD;
			    node->info.alter.alter_clause.attr_mthd.attr_mthd_name_list = $2;
			  }

		DBG_PRINT}}
	| File method_file_list
		{{ DBG_TRACE_GRAMMAR(alter_drop_clause_cubrid_specific, | File method_file_list);

			PT_NODE *node = parser_get_alter_node ();

			if (node)
			  {
			    node->info.alter.code = PT_DROP_ATTR_MTHD;
			    node->info.alter.alter_clause.attr_mthd.mthd_file_list = $2;
			  }

		DBG_PRINT}}
	| SUPERCLASS only_class_name_list
		{{ DBG_TRACE_GRAMMAR(alter_drop_clause_cubrid_specific, | SUPERCLASS only_class_name_list);

			PT_NODE *node = parser_get_alter_node ();

			if (node)
			  {
			    node->info.alter.code = PT_DROP_SUPCLASS;
			    node->info.alter.super.sup_class_list = $2;
			  }

		DBG_PRINT}}
	| QUERY query_number_list
		{{ DBG_TRACE_GRAMMAR(alter_drop_clause_cubrid_specific, | QUERY query_number_list);

			PT_NODE *node = parser_get_alter_node ();

			if (node)
			  {
			    node->info.alter.code = PT_DROP_QUERY;
			    node->info.alter.alter_clause.query.query_no_list = $2;
			  }

		DBG_PRINT}}
	| QUERY
		{{ DBG_TRACE_GRAMMAR(alter_drop_clause_cubrid_specific, | QUERY );

			PT_NODE *node = parser_get_alter_node ();

			if (node)
			  {
			    node->info.alter.code = PT_DROP_QUERY;
			    node->info.alter.alter_clause.query.query_no_list = NULL;
			  }

		DBG_PRINT}}
	;

normal_or_class_attr_list_with_commas
	: normal_or_class_attr_list_with_commas ',' normal_or_class_attr
		{{ DBG_TRACE_GRAMMAR(normal_or_class_attr_list_with_commas, : normal_or_class_attr_list_with_commas ',' normal_or_class_attr);

			$$ = parser_make_link ($1, $3);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| normal_or_class_attr ',' normal_or_class_attr
		{{ DBG_TRACE_GRAMMAR(normal_or_class_attr_list_with_commas, | normal_or_class_attr ',' normal_or_class_attr);

			$$ = parser_make_link ($1, $3);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

alter_modify_clause_for_alter_list
	: opt_of_column_attribute
	  { allow_attribute_ordering = true; }
	  attr_def_one
	  { allow_attribute_ordering = false; }
		{{ DBG_TRACE_GRAMMAR(alter_modify_clause_for_alter_list, : opt_of_column_attribute attr_def_one);

			PT_NODE *node = parser_get_alter_node ();

			if (node)
			  {
			    node->info.alter.code = PT_CHANGE_ATTR;
			    node->info.alter.alter_clause.attr_mthd.attr_def_list = $3;
			    /* no name change for MODIFY */
			  }

		DBG_PRINT}}
	| CLASS ATTRIBUTE
	  { allow_attribute_ordering = true; }
	  attr_def_one
	  { allow_attribute_ordering = false; }
		{{ DBG_TRACE_GRAMMAR(alter_modify_clause_for_alter_list, | CLASS ATTRIBUTE attr_def_one);

			PT_NODE *node = parser_get_alter_node ();

			if (node)
			  {
			    node->info.alter.code = PT_CHANGE_ATTR;
			    node->info.alter.alter_clause.attr_mthd.attr_def_list = $4;
			    /* no name change for MODIFY */

			    node->info.alter.alter_clause.attr_mthd.attr_def_list->
				info.attr_def.attr_type = PT_META_ATTR;
			  }

		DBG_PRINT}}
	;

alter_change_clause_for_alter_list
	: normal_column_or_class_attribute
	  { allow_attribute_ordering = true; }
	  attr_def_one
	  { allow_attribute_ordering = false; }
		{{ DBG_TRACE_GRAMMAR(alter_change_clause_for_alter_list, : normal_column_or_class_attribute attr_def_one);

			PT_NODE *node = parser_get_alter_node ();

			if (node)
			  {
			    PT_NODE *att = NULL;

			    node->info.alter.code = PT_CHANGE_ATTR;
			    node->info.alter.alter_clause.attr_mthd.attr_def_list = $3;
			    node->info.alter.alter_clause.attr_mthd.attr_old_name = $1;

			    att = node->info.alter.alter_clause.attr_mthd.attr_def_list;
			    att->info.attr_def.attr_type =
			      node->info.alter.alter_clause.attr_mthd.attr_old_name->info.name.meta_class;
			  }

		DBG_PRINT}}
	;

alter_change_clause_cubrid_specific
	: METHOD method_def_list
		{{ DBG_TRACE_GRAMMAR(alter_change_clause_cubrid_specific, : METHOD method_def_list);

			PT_NODE *node = parser_get_alter_node ();

			if (node)
			  {
			    node->info.alter.code = PT_MODIFY_ATTR_MTHD;
			    node->info.alter.alter_clause.attr_mthd.mthd_def_list = $2;
			  }

		DBG_PRINT}}
	| QUERY unsigned_integer csql_query opt_vclass_comment_spec
		{{ DBG_TRACE_GRAMMAR(alter_change_clause_cubrid_specific, | QUERY unsigned_integer csql_query opt_vclass_comment_spec);

			PT_NODE *node = parser_get_alter_node ();

			if (node)
			  {
			    node->info.alter.code = PT_MODIFY_QUERY;
			    node->info.alter.alter_clause.query.query = $3;
			    node->info.alter.alter_clause.query.query_no_list = $2;
			    node->info.alter.alter_clause.query.view_comment = $4;
			  }

		DBG_PRINT}}
	| QUERY csql_query opt_vclass_comment_spec
		{{ DBG_TRACE_GRAMMAR(alter_change_clause_cubrid_specific, | QUERY csql_query opt_vclass_comment_spec);

			PT_NODE *node = parser_get_alter_node ();

			if (node)
			  {
			    node->info.alter.code = PT_MODIFY_QUERY;
			    node->info.alter.alter_clause.query.query = $2;
			    node->info.alter.alter_clause.query.query_no_list = NULL;
			    node->info.alter.alter_clause.query.view_comment = $3;
			  }

		DBG_PRINT}}
	| File file_path_name AS file_path_name
		{{ DBG_TRACE_GRAMMAR(alter_change_clause_cubrid_specific, | File file_path_name AS file_path_name);

			PT_NODE *node = parser_get_alter_node ();

			if (node)
			  {
			    node->info.alter.code = PT_RENAME_ATTR_MTHD;
			    node->info.alter.alter_clause.rename.element_type = PT_FILE_RENAME;
			    node->info.alter.alter_clause.rename.new_name = $4;
			    node->info.alter.alter_clause.rename.old_name = $2;
			  }

		DBG_PRINT}}
	;

normal_or_class_attr
	: opt_class identifier
		{{ DBG_TRACE_GRAMMAR(normal_or_class_attr, : opt_class identifier);

			if ($1)
			  $2->info.name.meta_class = PT_META_ATTR;
			else
			  $2->info.name.meta_class = PT_NORMAL;

			$$ = $2;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

query_number_list
	: query_number_list ',' unsigned_integer
		{{ DBG_TRACE_GRAMMAR(query_number_list, : query_number_list ',' unsigned_integer);

			$$ = parser_make_link ($1, $3);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| unsigned_integer
		{{ DBG_TRACE_GRAMMAR(query_number_list, | unsigned_integer);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

alter_column_clause_mysql_specific
	: normal_column_or_class_attribute SET DEFAULT expression_
		{{ DBG_TRACE_GRAMMAR(alter_column_clause_mysql_specific, : normal_column_or_class_attribute SET DEFAULT expression_);

			PT_NODE *alter_node = parser_get_alter_node ();

			if (alter_node)
			  {
			    PT_NODE *node = parser_new_node (this_parser, PT_DATA_DEFAULT);

			    if (node)
			      {
				PT_NODE *def;
				node->info.data_default.default_value = $4;
				node->info.data_default.shared = PT_DEFAULT;
				PARSER_SAVE_ERR_CONTEXT (node, @4.buffer_pos)

				def = node->info.data_default.default_value;
				if (def && def->node_type == PT_EXPR)
				  {
					if (def->info.expr.op == PT_TO_CHAR)
					  {
						if (def->info.expr.arg3)
						  {
						    bool dummy;
						    bool has_user_lang = false;
						    assert (def->info.expr.arg3->node_type == PT_VALUE);
							(void) lang_get_lang_id_from_flag (def->info.expr.arg3->info.value.data_value.i, &dummy, &has_user_lang);
							if (has_user_lang)
							  {
								PT_ERROR (this_parser, def->info.expr.arg3, "do not allow lang format in default to_char");
							  }
						  }

						if (def->info.expr.arg1 && def->info.expr.arg1->node_type == PT_EXPR)
						  {
						    def = def->info.expr.arg1;
						  }
					  }

				    switch (def->info.expr.op)
				      {
				      case PT_SYS_TIME:
					node->info.data_default.default_expr_type = DB_DEFAULT_SYSTIME;
					break;
				      case PT_SYS_DATE:
					node->info.data_default.default_expr_type = DB_DEFAULT_SYSDATE;
					break;
				      case PT_SYS_DATETIME:
					node->info.data_default.default_expr_type = DB_DEFAULT_SYSDATETIME;
					break;
				      case PT_SYS_TIMESTAMP:
					node->info.data_default.default_expr_type = DB_DEFAULT_SYSTIMESTAMP;
					break;
				      case PT_CURRENT_TIME:
					node->info.data_default.default_expr_type = DB_DEFAULT_CURRENTTIME;
					break;
				      case PT_CURRENT_DATE:
					node->info.data_default.default_expr_type = DB_DEFAULT_CURRENTDATE;
					break;
				      case PT_CURRENT_DATETIME:
					node->info.data_default.default_expr_type = DB_DEFAULT_CURRENTDATETIME;
					break;
				      case PT_CURRENT_TIMESTAMP:
					node->info.data_default.default_expr_type = DB_DEFAULT_CURRENTTIMESTAMP;
					break;
				      case PT_USER:
					node->info.data_default.default_expr_type = DB_DEFAULT_USER;
					break;
				      case PT_CURRENT_USER:
					node->info.data_default.default_expr_type = DB_DEFAULT_CURR_USER;
					break;
				      case PT_UNIX_TIMESTAMP:
					node->info.data_default.default_expr_type = DB_DEFAULT_UNIX_TIMESTAMP;
					break;
				      default:
					node->info.data_default.default_expr_type = DB_DEFAULT_NONE;
					break;
				      }
				  }
				else
				  {
				    node->info.data_default.default_expr_type = DB_DEFAULT_NONE;
				  }
			      }

			    alter_node->info.alter.code = PT_ALTER_DEFAULT;
			    alter_node->info.alter.alter_clause.ch_attr_def.attr_name_list = $1;
			    alter_node->info.alter.alter_clause.ch_attr_def.data_default_list = node;
			  }

		DBG_PRINT}}
	;

normal_column_or_class_attribute
	: opt_of_column_attribute identifier
		{{ DBG_TRACE_GRAMMAR(normal_column_or_class_attribute, : opt_of_column_attribute identifier);

			PT_NODE * node = $2;
			if (node)
			  {
			    node->info.name.meta_class = PT_NORMAL;
			  }
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| CLASS ATTRIBUTE identifier
		{{ DBG_TRACE_GRAMMAR(normal_column_or_class_attribute, | CLASS ATTRIBUTE identifier);

			PT_NODE * node = $3;
			if (node)
			  {
			    node->info.name.meta_class = PT_META_ATTR;
			  }
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

insert_or_replace_stmt
	: insert_name_clause insert_stmt_value_clause on_duplicate_key_update
		{{ DBG_TRACE_GRAMMAR(insert_or_replace_stmt, : insert_name_clause insert_stmt_value_clause on_duplicate_key_update);

			PT_NODE *ins = $1;

			if (ins)
			  {
			    ins->info.insert.odku_assignments = $3;
			    ins->info.insert.value_clauses = $2;
			  }

			$$ = ins;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| insert_name_clause insert_stmt_value_clause into_clause_opt
		{{ DBG_TRACE_GRAMMAR(insert_or_replace_stmt, | insert_name_clause insert_stmt_value_clause into_clause_opt);

			PT_NODE *ins = $1;

			if (ins)
			  {
			    ins->info.insert.value_clauses = $2;
			    ins->info.insert.into_var = $3;
			  }

			$$ = ins;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| replace_name_clause insert_stmt_value_clause into_clause_opt
		{{ DBG_TRACE_GRAMMAR(insert_or_replace_stmt, | replace_name_clause insert_stmt_value_clause into_clause_opt);

			PT_NODE *ins = $1;

			if (ins)
			  {
			    ins->info.insert.value_clauses = $2;
			    ins->info.insert.into_var = $3;
			  }

			$$ = ins;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| insert_set_stmt on_duplicate_key_update
		{{ DBG_TRACE_GRAMMAR(insert_or_replace_stmt, | insert_set_stmt on_duplicate_key_update);

			PT_NODE *ins = $1;
			if (ins)
			  {
			    ins->info.insert.odku_assignments = $2;
			  }

			$$ = ins;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| insert_set_stmt into_clause_opt
		{{ DBG_TRACE_GRAMMAR(insert_or_replace_stmt, | insert_set_stmt into_clause_opt);

			PT_NODE *ins = $1;

			if (ins)
			  {
			    ins->info.insert.into_var = $2;
			  }

			$$ = ins;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| replace_set_stmt into_clause_opt
		{{ DBG_TRACE_GRAMMAR(insert_or_replace_stmt, | replace_set_stmt into_clause_opt);

			PT_NODE *ins = $1;

			if (ins)
			  {
			    ins->info.insert.into_var = $2;
			  }

			$$ = ins;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

insert_set_stmt
	: insert_stmt_keyword
	  insert_set_stmt_header
		{{ DBG_TRACE_GRAMMAR(insert_set_stmt, : insert_stmt_keyword insert_set_stmt_header);
			$$ = $2;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
		}}
	;

replace_set_stmt
	: replace_stmt_keyword
	  insert_set_stmt_header
		{{ DBG_TRACE_GRAMMAR(replace_set_stmt, : replace_stmt_keyword insert_set_stmt_header);
			$$ = $2;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
		}}
	;

insert_stmt_keyword
	: INSERT
		{ DBG_TRACE_GRAMMAR(insert_stmt_keyword, : INSERT);
			PT_NODE* ins = parser_new_node (this_parser, PT_INSERT);
			parser_push_hint_node (ins);
		}
	;

replace_stmt_keyword
	: REPLACE
		{ DBG_TRACE_GRAMMAR(replace_stmt_keyword, : REPLACE);
			PT_NODE* ins = parser_new_node (this_parser, PT_INSERT);
			if (ins)
			  {
			    ins->info.insert.do_replace = true;
			  }
			parser_push_hint_node (ins);
		}
	;

insert_set_stmt_header
	: opt_hint_list
	  opt_into
	  only_class_name
	  SET
	  insert_assignment_list
		{{ DBG_TRACE_GRAMMAR(insert_set_stmt_header, : opt_hint_list opt_into only_class_name SET insert_assignment_list);

			PT_NODE *ins = parser_pop_hint_node ();
			PT_NODE *ocs = parser_new_node (this_parser, PT_SPEC);
			PT_NODE *nls = pt_node_list (this_parser, PT_IS_VALUE, CONTAINER_AT_1 ($5));

			if (ocs)
			  {
			    ocs->info.spec.entity_name = $3;
			    ocs->info.spec.only_all = PT_ONLY;
			    ocs->info.spec.meta_class = PT_CLASS;
			  }

			if (ins)
			  {
			    ins->info.insert.spec = ocs;
			    ins->info.insert.attr_list = CONTAINER_AT_0 ($5);
			    ins->info.insert.value_clauses = nls;
			  }

			$$ = ins;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

insert_assignment_list
	: insert_assignment_list ',' identifier '=' expression_
		{{ DBG_TRACE_GRAMMAR(insert_assignment_list, : insert_assignment_list ',' identifier '=' expression_);

			parser_make_link (CONTAINER_AT_0 ($1), $3);
			parser_make_link (CONTAINER_AT_1 ($1), $5);

			$$ = $1;

		DBG_PRINT}}
	| insert_assignment_list ',' identifier '=' DEFAULT
		{{ DBG_TRACE_GRAMMAR(insert_assignment_list, | insert_assignment_list ',' identifier '=' DEFAULT );
			PT_NODE *arg = parser_copy_tree (this_parser, $3);

			if (arg)
			  {
			    pt_set_fill_default_in_path_expression (arg);
			  }
			parser_make_link (CONTAINER_AT_0 ($1), $3);
			parser_make_link (CONTAINER_AT_1 ($1),
					  parser_make_expression (this_parser, PT_DEFAULTF, arg, NULL, NULL));

			$$ = $1;

		DBG_PRINT}}
	| identifier '=' expression_
		{{ DBG_TRACE_GRAMMAR(insert_assignment_list, | identifier '=' expression_ );

			container_2 ctn;
			SET_CONTAINER_2 (ctn, $1, $3);

			$$ = ctn;

		DBG_PRINT}}
	| identifier '=' DEFAULT
		{{ DBG_TRACE_GRAMMAR(insert_assignment_list, | identifier '=' DEFAULT);

			container_2 ctn;
			PT_NODE *arg = parser_copy_tree (this_parser, $1);

			if (arg)
			  {
			    pt_set_fill_default_in_path_expression (arg);
			  }
			SET_CONTAINER_2 (ctn, $1,
			  parser_make_expression (this_parser, PT_DEFAULTF, arg, NULL, NULL));

			$$ = ctn;

		DBG_PRINT}}
	;

on_duplicate_key_update
	: ON_ DUPLICATE_ KEY UPDATE
	  update_assignment_list
		{{ DBG_TRACE_GRAMMAR(on_duplicate_key_update, : ON_ DUPLICATE_ KEY UPDATE update_assignment_list);

			$$ = $5;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

insert_expression
	: insert_name_clause insert_expression_value_clause
		{{ DBG_TRACE_GRAMMAR(insert_expression, : insert_name_clause insert_expression_value_clause);

			PT_NODE *ins = $1;

			if (ins)
			  {
			    ins->info.insert.value_clauses = $2;
			  }

			$$ = ins;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| '(' insert_name_clause insert_expression_value_clause into_clause_opt ')'
		{{ DBG_TRACE_GRAMMAR(insert_expression, | '(' insert_name_clause insert_expression_value_clause into_clause_opt ')');

			PT_NODE *ins = $2;

			if (ins)
			  {
			    ins->info.insert.value_clauses = $3;
			    ins->info.insert.into_var = $4;
			  }

			$$ = ins;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

insert_name_clause
	: insert_stmt_keyword
	  insert_name_clause_header
		{{ DBG_TRACE_GRAMMAR(insert_name_clause, : insert_stmt_keyword insert_name_clause_header);
			$$ = $2;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
		}}
	;

replace_name_clause
	: replace_stmt_keyword
	  insert_name_clause_header
		{{ DBG_TRACE_GRAMMAR(replace_name_clause, : replace_stmt_keyword insert_name_clause_header);
			$$ = $2;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
		}}
	;

insert_name_clause_header
	: opt_hint_list
	  opt_into
	  only_class_name
	  opt_partition_spec
	  opt_attr_list
		{{ DBG_TRACE_GRAMMAR(insert_name_clause_header, : opt_hint_list opt_into only_class_name opt_partition_spec opt_attr_list);

			PT_NODE *ins = parser_pop_hint_node ();
			PT_NODE *ocs = parser_new_node (this_parser, PT_SPEC);

			if (ocs)
			  {
			    ocs->info.spec.entity_name = $3;
			    ocs->info.spec.only_all = PT_ONLY;
			    ocs->info.spec.meta_class = PT_CLASS;
			    if ($4)
			      {
			        ocs->info.spec.partition = $4;
			      }
			  }

			if (ins)
			  {
			    ins->info.insert.spec = ocs;
			    ins->info.insert.attr_list = $5;
			  }

			$$ = ins;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

opt_attr_list
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_attr_list, :);

			$$ = NULL;

		DBG_PRINT}}
	| '(' ')'
		{{ DBG_TRACE_GRAMMAR(opt_attr_list, | '(' ')' );

			$$ = NULL;

		DBG_PRINT}}
	| '(' identifier_list ')'
		{{ DBG_TRACE_GRAMMAR(opt_attr_list, | '(' identifier_list ')' );

			$$ = $2;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

opt_path_attr_list
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_path_attr_list, : );

			$$ = NULL;

		DBG_PRINT}}
	| '(' ')'
		{{ DBG_TRACE_GRAMMAR(opt_path_attr_list, |  '(' ')' );

			$$ = NULL;

		DBG_PRINT}}
	| '(' simple_path_id_list ')'
		{{ DBG_TRACE_GRAMMAR(opt_path_attr_list, | '(' simple_path_id_list ')' );

			$$ = $2;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

insert_stmt_value_clause
	: insert_expression_value_clause
		{{ DBG_TRACE_GRAMMAR(insert_stmt_value_clause, : insert_expression_value_clause );

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| opt_with_clause
	  csql_query_without_values_and_single_subquery
		{{ DBG_TRACE_GRAMMAR(insert_stmt_value_clause, |  opt_with_clause csql_query_without_values_and_single_subquery);

			PT_NODE *with_clause = $1;
			PT_NODE *select_node = $2;
			select_node->info.query.with = with_clause;
			PT_NODE *nls = pt_node_list (this_parser, PT_IS_SUBQUERY, select_node);

			$$ = nls;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| '(' opt_with_clause
	  csql_query_without_values_query_no_with_clause ')'
		{{ DBG_TRACE_GRAMMAR(insert_stmt_value_clause, |  '(' opt_with_clause csql_query_without_values_query_no_with_clause ')');
			PT_NODE *with_clause = $2;
			PT_NODE *select_node = $3;
			select_node->info.query.with = with_clause;
			PT_NODE *nls = pt_node_list (this_parser, PT_IS_SUBQUERY, select_node);

			$$ = nls;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

insert_expression_value_clause
	: of_value_values insert_value_clause_list
		{{ DBG_TRACE_GRAMMAR(insert_expression_value_clause, : of_value_values insert_value_clause_list);

			$$ = $2;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| DEFAULT opt_values
		{{ DBG_TRACE_GRAMMAR(insert_expression_value_clause, | DEFAULT opt_values);

			PT_NODE *nls = pt_node_list (this_parser, PT_IS_DEFAULT_VALUE, NULL);
			$$ = nls;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

of_value_values
	: VALUE
	| VALUES
	;

opt_values
	: /* [empty] */
	| VALUES
	;

opt_into
	: /* [empty] */
	| INTO
	;

into_clause_opt
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(into_clause_opt, : );
			$$ = NULL;

		DBG_PRINT}}
	| INTO to_param
		{{ DBG_TRACE_GRAMMAR(into_clause_opt, | INTO to_param );

			$$ = $2;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| TO to_param
		{{ DBG_TRACE_GRAMMAR(into_clause_opt, | TO to_param );

			$$ = $2;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

insert_value_clause_list
	: insert_value_clause_list ',' insert_value_clause
		{{ DBG_TRACE_GRAMMAR(insert_value_clause_list, : insert_value_clause_list ',' insert_value_clause);

			$$ = parser_make_link ($1, $3);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| insert_value_clause
		{{ DBG_TRACE_GRAMMAR(insert_value_clause_list, | insert_value_clause);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

insert_value_clause
	: '(' insert_value_list ')'
		{{ DBG_TRACE_GRAMMAR(insert_value_clause, : '(' insert_value_list ')' );

			PT_NODE *nls = pt_node_list (this_parser, PT_IS_VALUE, $2);
			$$ = nls;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| '('')'
		{{ DBG_TRACE_GRAMMAR(insert_value_clause, | '('')' );

			PT_NODE *nls = NULL;

			if (prm_get_integer_value (PRM_ID_COMPAT_MODE) == COMPAT_MYSQL)
			  {
			    nls = pt_node_list (this_parser, PT_IS_DEFAULT_VALUE, NULL);
			  }
			else
			  {
			    nls = pt_node_list (this_parser, PT_IS_VALUE, NULL);
			  }

			$$ = nls;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| DEFAULT opt_values
		{{ DBG_TRACE_GRAMMAR(insert_value_clause, | DEFAULT opt_values);

			PT_NODE *nls = pt_node_list (this_parser, PT_IS_DEFAULT_VALUE, NULL);
			$$ = nls;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

insert_value_list
	: insert_value_list ',' insert_value
		{{ DBG_TRACE_GRAMMAR(insert_value_list, : insert_value_list ',' insert_value);

			$$ = parser_make_link ($1, $3);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| insert_value
		{{ DBG_TRACE_GRAMMAR(insert_value_list, | insert_value);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

insert_value
	: select_stmt
		{{ DBG_TRACE_GRAMMAR(insert_value, : select_stmt);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| normal_expression
		{{ DBG_TRACE_GRAMMAR(insert_value, | normal_expression);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| DEFAULT
		{{ DBG_TRACE_GRAMMAR(insert_value, | DEFAULT);

			/* The argument will be filled in later, when the
			   corresponding column name is known.
			   See fill_in_insert_default_function_arguments(). */
			$$ = parser_make_expression (this_parser, PT_DEFAULTF, NULL, NULL, NULL);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

show_stmt
	: SHOW
	  opt_full
	  TABLES
		{{ DBG_TRACE_GRAMMAR(show_stmt, : SHOW opt_full TABLES);

			const bool is_full_syntax = ($2 == 1);
			const int like_where_syntax = 0;  /* neither LIKE nor WHERE */
			PT_NODE *node = NULL;

			node = pt_make_query_show_table (this_parser, is_full_syntax, like_where_syntax, NULL);

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| SHOW
	  opt_full
	  TABLES
	  LIKE
	  expression_
		{{ DBG_TRACE_GRAMMAR(show_stmt, | SHOW opt_full TABLES LIKE expression_);

			const bool is_full_syntax = ($2 == 1);
			const int like_where_syntax = 1;  /* is LIKE */
			PT_NODE *node = NULL;
			PT_NODE *like_rhs = $5;

			node = pt_make_query_show_table (this_parser, is_full_syntax, like_where_syntax, like_rhs);

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| SHOW
	  opt_full
	  TABLES
	  WHERE
	  search_condition
		{{ DBG_TRACE_GRAMMAR(show_stmt, | SHOW opt_full TABLES WHERE search_condition);

			const bool is_full_syntax = ($2 == 1);
			const int like_where_syntax = 2;  /* is WHERE */
			PT_NODE *node = NULL;
			PT_NODE *where_cond = $5;

			node = pt_make_query_show_table (this_parser, is_full_syntax, like_where_syntax, where_cond);

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| SHOW
	  opt_full
	  COLUMNS
	  of_from_in
	  class_name
		{{ DBG_TRACE_GRAMMAR(show_stmt, | SHOW opt_full COLUMNS of_from_in class_name);

			const bool is_full_syntax = ($2 == 1);
			const int like_where_syntax = 0;  /* neither LIKE nor WHERE */
			PT_NODE *node = NULL;
			PT_NODE *original_cls_id = $5;

			node = pt_make_query_show_columns (this_parser, original_cls_id,
							   like_where_syntax, NULL, is_full_syntax);

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| SHOW
	  opt_full
	  COLUMNS
	  of_from_in
	  class_name
	  LIKE
	  expression_
		{{ DBG_TRACE_GRAMMAR(show_stmt, | SHOW opt_full COLUMNS  of_from_in identifier LIKE expression_);

			const bool is_full_syntax = ($2 == 1);
			const int like_where_syntax = 1;  /* is LIKE */
			PT_NODE *node = NULL;
			PT_NODE *original_cls_id = $5;
			PT_NODE *like_rhs = $7;

			node = pt_make_query_show_columns (this_parser, original_cls_id,
							   like_where_syntax, like_rhs, is_full_syntax);

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| SHOW
	  opt_full
	  COLUMNS
	  of_from_in
	  class_name
	  WHERE
	  search_condition
		{{ DBG_TRACE_GRAMMAR(show_stmt, | SHOW opt_full COLUMNS of_from_in identifier WHERE search_condition);

			const bool is_full_syntax = ($2 == 1);
			const int like_where_syntax = 2;  /* is WHERE */
			PT_NODE *node = NULL;
			PT_NODE *original_cls_id = $5;
			PT_NODE *where_cond = $7;

			node = pt_make_query_show_columns (this_parser, original_cls_id,
							   like_where_syntax, where_cond, is_full_syntax);

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| of_describe_desc_explain
	  class_name
		{{ DBG_TRACE_GRAMMAR(show_stmt, | of_describe_desc_explain class_name);

			PT_NODE *node = NULL;
			PT_NODE *original_cls_id = $2;

			node = pt_make_query_show_columns (this_parser, original_cls_id,
							   0, NULL, 0);

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| of_describe_desc_explain
	  class_name
	  identifier
		{{ DBG_TRACE_GRAMMAR(show_stmt, | of_describe_desc_explain identifier identifier);

			PT_NODE *node = NULL;
			PT_NODE *original_cls_id = $2;
			PT_NODE *attr = $3;

			node = pt_make_query_describe_w_identifier (this_parser, original_cls_id, attr);

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| of_describe_desc_explain
	  class_name
	  char_string_literal
		{{ DBG_TRACE_GRAMMAR(show_stmt, | of_describe_desc_explain identifier char_string_literal);

			int like_where_syntax = 0;
			PT_NODE *node = NULL;
			PT_NODE *original_cls_id = $2;
			PT_NODE *like_rhs = $3;

			if (like_rhs != NULL)
			  {
			    like_where_syntax = 1;   /* is LIKE */
			  }
			node = pt_make_query_show_columns (this_parser, original_cls_id,
							   like_where_syntax, like_rhs, 0);

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| SHOW
	  COLLATION
		{{ DBG_TRACE_GRAMMAR(show_stmt, | SHOW COLLATION);

			PT_NODE *node = NULL;

			node = pt_make_query_show_collation (this_parser, 0, NULL);

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| SHOW
	  COLLATION
	  LIKE
	  expression_
		{{ DBG_TRACE_GRAMMAR(show_stmt, | SHOW COLLATION LIKE expression_);

			const int like_where_syntax = 1;  /* is LIKE */
			PT_NODE *node = NULL;
			PT_NODE *like_rhs = $4;

			node = pt_make_query_show_collation (this_parser,
							     like_where_syntax,
							     like_rhs);

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| SHOW
	  COLLATION
	  WHERE
	  search_condition
		{{ DBG_TRACE_GRAMMAR(show_stmt, | SHOW COLLATION WHERE search_condition);
			const int like_where_syntax = 2;  /* is WHERE */
			PT_NODE *node = NULL;
			PT_NODE *where_cond = $4;

			node = pt_make_query_show_collation (this_parser,
							     like_where_syntax,
							     where_cond);

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| SHOW
	  CREATE
	  TABLE
	  class_name
		{{ DBG_TRACE_GRAMMAR(show_stmt, | SHOW CREATE TABLE class_name);

			PT_NODE *node = NULL;
			node = pt_make_query_show_create_table (this_parser, $4);

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| SHOW
	  CREATE
	  VIEW
	  class_name
		{{ DBG_TRACE_GRAMMAR(show_stmt, | SHOW CREATE VIEW class_name);

			PT_NODE *node = NULL;
			PT_NODE *view_id = $4;

			node = pt_make_query_show_create_view (this_parser, view_id);

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| SHOW
	  GRANTS
	  For
	  identifier
		{{ DBG_TRACE_GRAMMAR(show_stmt, | SHOW GRANTS For identifier);

			PT_NODE *node = NULL;
			PT_NODE *user_id = $4;

			assert (user_id != NULL);
			assert (user_id->node_type == PT_NAME);

			node = pt_make_query_show_grants (this_parser, user_id->info.name.original);

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| SHOW
	  GRANTS
	  opt_for_current_user
		{{ DBG_TRACE_GRAMMAR(show_stmt, | SHOW GRANTS opt_for_current_user);

			PT_NODE *node = NULL;

			node = pt_make_query_show_grants_curr_usr (this_parser);

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| SHOW
	  of_index_indexes_keys
	  of_from_in
	  class_name
		{{ DBG_TRACE_GRAMMAR(show_stmt, | SHOW of_index_indexes_keys of_from_in class_name);

			PT_NODE *node = NULL;
			PT_NODE *table_id = $4;

			node = pt_make_query_show_index (this_parser, table_id);

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
		DBG_PRINT}}
	| SHOW EXEC STATISTICS ALL
		{{ DBG_TRACE_GRAMMAR(show_stmt, | SHOW EXEC STATISTICS ALL);
			PT_NODE *node = NULL;

			node = pt_make_query_show_exec_stats_all (this_parser);

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| SHOW EXEC STATISTICS
		{{ DBG_TRACE_GRAMMAR(show_stmt, | SHOW EXEC STATISTICS);
			PT_NODE *node = NULL;

			node = pt_make_query_show_exec_stats (this_parser);

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| SHOW TRACE
		{{ DBG_TRACE_GRAMMAR(show_stmt, | SHOW TRACE);
			PT_NODE *node = NULL;

			node = pt_make_query_show_trace (this_parser);

			$$ = node;

		DBG_PRINT}}
	| SHOW show_type
		{{ DBG_TRACE_GRAMMAR(show_stmt, | SHOW show_type);
			int type = $2;
			PT_NODE *node = pt_make_query_showstmt (this_parser, type, NULL, 0, NULL);

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| SHOW show_type_of_like LIKE expression_
		{{ DBG_TRACE_GRAMMAR(show_stmt, | SHOW show_type_of_like LIKE expression_);

			const int like_where_syntax = 1;  /* is LIKE */
			int type = $2;
			PT_NODE *like_rhs = $4;
			PT_NODE *node = pt_make_query_showstmt (this_parser, type, NULL,
                                                                like_where_syntax, like_rhs);

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| SHOW show_type_of_where WHERE search_condition
		{{ DBG_TRACE_GRAMMAR(show_stmt, | SHOW show_type_of_where WHERE search_condition);
			const int like_where_syntax = 2;  /* is WHERE */
			int type = $2;
			PT_NODE *where_cond = $4;
			PT_NODE *node = pt_make_query_showstmt (this_parser, type, NULL,
                                                               like_where_syntax, where_cond);

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| SHOW show_type_arg1 OF arg_value
		{{ DBG_TRACE_GRAMMAR(show_stmt, | SHOW show_type_arg1 OF arg_value);
			int type = $2;
			PT_NODE *args = $4;
			PT_NODE *node = pt_make_query_showstmt (this_parser, type, args, 0, NULL);

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| SHOW show_type_arg1_opt opt_arg_value
		{{ DBG_TRACE_GRAMMAR(show_stmt, | SHOW show_type_arg1_opt opt_arg_value);
			PT_NODE *node = NULL;
			int type = $2;
			PT_NODE *args = $3;

			node = pt_make_query_showstmt (this_parser, type, args, 0, NULL);

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| SHOW show_type_arg_named of_or_where named_args
		{{ DBG_TRACE_GRAMMAR(show_stmt, | SHOW show_type_arg_named of_or_where named_args);
			PT_NODE *node = NULL;
			int type = $2;
			PT_NODE *args = $4;

			node = pt_make_query_showstmt (this_parser, type, args, 0, NULL);

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| SHOW show_type_id OF class_name
		{{
			int type = $2;
			PT_NODE *node, *args = $4;

			node = pt_make_query_showstmt (this_parser, type, args, 0, NULL);

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| SHOW show_type_id_dot_id OF class_name DOT identifier
		{{ DBG_TRACE_GRAMMAR(show_stmt, | SHOW show_type_id_dot_id OF class_name DOT identifier);
			int type = $2;
			PT_NODE *node, *args = $4;

			args->next = $6;
			node = pt_make_query_showstmt (this_parser, type, args, 0, NULL);

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

kill_stmt
	: KILL arg_value_list
		{{ DBG_TRACE_GRAMMAR(kill_stmt, | KILL arg_value_list);
			PT_NODE *node = parser_new_node (this_parser, PT_KILL_STMT);

			if (node)
			  {
			    node->info.killstmt.tran_id_list = $2;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| KILL kill_type arg_value_list
		{{ DBG_TRACE_GRAMMAR(kill_stmt, | KILL kill_type arg_value_list);
			int type = $2;
			PT_NODE *node = parser_new_node (this_parser, PT_KILL_STMT);

			if (node)
			  {
			    node->info.killstmt.tran_id_list = $3;
			    node->info.killstmt.kill_type = type;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

show_type
	: ACCESS STATUS
		{{
			$$ = SHOWSTMT_ACCESS_STATUS;
		}}
	| CRITICAL SECTIONS
		{{
			$$ = SHOWSTMT_GLOBAL_CRITICAL_SECTIONS;
		}}
	| JOB QUEUES
		{{
			$$ = SHOWSTMT_JOB_QUEUES;
		}}
	| PAGE BUFFER STATUS
		{{
			$$ = SHOWSTMT_PAGE_BUFFER_STATUS;
		}}
	| TIMEZONES
		{{
			$$ = SHOWSTMT_TIMEZONES;
		}}
	| FULL TIMEZONES
		{{
			$$ = SHOWSTMT_FULL_TIMEZONES;
		}}
	| TRANSACTION TABLES
		{{
			$$ = SHOWSTMT_TRAN_TABLES;
		}}
	| TRAN TABLES
		{{
			$$ = SHOWSTMT_TRAN_TABLES;
		}}
	| THREADS
		{{
			$$ = SHOWSTMT_THREADS;
		}}
	;

show_type_of_like
	: ACCESS STATUS
		{{
			$$ = SHOWSTMT_ACCESS_STATUS;
		}}
	| TIMEZONES
		{{
			$$ = SHOWSTMT_TIMEZONES;
		}}
	| FULL TIMEZONES
		{{
			$$ = SHOWSTMT_FULL_TIMEZONES;
		}}
	;

show_type_of_where
	: ACCESS STATUS
		{{
			$$ = SHOWSTMT_ACCESS_STATUS;
		}}
	| TIMEZONES
		{{
			$$ = SHOWSTMT_TIMEZONES;
		}}
	| FULL TIMEZONES
		{{
			$$ = SHOWSTMT_FULL_TIMEZONES;
		}}
	| TRANSACTION TABLES
		{{
			$$ = SHOWSTMT_TRAN_TABLES;
		}}
	| TRAN TABLES
		{{
			$$ = SHOWSTMT_TRAN_TABLES;
		}}
	| THREADS
		{{
			$$ = SHOWSTMT_THREADS;
		}}
	;

show_type_arg1
	: VOLUME HEADER
		{{
			$$ = SHOWSTMT_VOLUME_HEADER;
		}}
	| ARCHIVE LOG HEADER
		{{
			$$ = SHOWSTMT_ARCHIVE_LOG_HEADER;
		}}
	;

show_type_arg1_opt
	: LOG HEADER
		{{
			$$ = SHOWSTMT_ACTIVE_LOG_HEADER;
		}}
	;

show_type_arg_named
	: SLOTTED PAGE HEADER
		{{
			$$ = SHOWSTMT_SLOTTED_PAGE_HEADER;
		}}
	| SLOTTED PAGE SLOTS
		{{
			$$ = SHOWSTMT_SLOTTED_PAGE_SLOTS;
		}}
	;

show_type_id
	: HEAP HEADER
		{{
			$$ = SHOWSTMT_HEAP_HEADER;
		}}
	| ALL HEAP HEADER
		{{
			$$ = SHOWSTMT_ALL_HEAP_HEADER;
		}}
	| HEAP CAPACITY
		{{
			$$ = SHOWSTMT_HEAP_CAPACITY;
		}}
	| ALL HEAP CAPACITY
		{{
			$$ = SHOWSTMT_ALL_HEAP_CAPACITY;
		}}
	| ALL INDEXES HEADER
		{{
			$$ = SHOWSTMT_ALL_INDEXES_HEADER;
		}}
	| ALL INDEXES CAPACITY
		{{
			$$ = SHOWSTMT_ALL_INDEXES_CAPACITY;
		}}
	;

show_type_id_dot_id
	: INDEX HEADER
		{{
			$$ = SHOWSTMT_INDEX_HEADER;
		}}
	| INDEX CAPACITY
		{{
			$$ = SHOWSTMT_INDEX_CAPACITY;
		}}
	;

kill_type
	: QUERY
		{{
			$$ = KILLSTMT_QUERY;
		}}
	| TRANSACTION
		{{
			$$ = KILLSTMT_TRAN;
		}}
	;

of_or_where
	: OF
		{{ DBG_TRACE_GRAMMAR(of_or_where, : OF );
			$$ = NULL;
		}}
	| WHERE
		{{ DBG_TRACE_GRAMMAR(of_or_where, | WHERE);
			$$ = NULL;
		}}
	;

named_args
	: named_arg
		{{ DBG_TRACE_GRAMMAR(named_args, | named_arg);
			$$ = $1;
		DBG_PRINT}}
	| named_args AND named_arg
		{{ DBG_TRACE_GRAMMAR(named_args, | named_args AND named_arg);
			$$ = parser_make_link ($1, $3);
		DBG_PRINT}}
	;

named_arg
	: identifier '=' arg_value
		{{ DBG_TRACE_GRAMMAR(named_arg, | identifier '=' arg_value);
			PT_NODE * node = parser_new_node (this_parser, PT_NAMED_ARG);
			node->info.named_arg.name = $1;
			node->info.named_arg.value = $3;
			$$ = node;
		DBG_PRINT}}
	;

opt_arg_value
	:
		{{ DBG_TRACE_GRAMMAR(opt_arg_value, : );
			PT_NODE *node = parser_new_node (this_parser, PT_VALUE);
			if (node)
			  node->type_enum = PT_TYPE_NULL;

			$$ = node;
		}}
	| OF arg_value
		{{ DBG_TRACE_GRAMMAR(opt_arg_value, | OF arg_value);
			$$ = $2;
		}}
	;

arg_value_list
	: arg_value_list ','  arg_value
		{{ DBG_TRACE_GRAMMAR(arg_value_list, : arg_value_list ','  arg_value);

			$$ = parser_make_link ($1, $3);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| arg_value
		{{ DBG_TRACE_GRAMMAR(arg_value_list, | arg_value);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

arg_value
	: char_string_literal
		{{ DBG_TRACE_GRAMMAR(arg_value, : char_string_literal);
			$$ = $1;
		DBG_PRINT}}
	| unsigned_integer
		{{ DBG_TRACE_GRAMMAR(arg_value, | unsigned_integer);
			$$ = $1;
		DBG_PRINT}}
	| identifier
		{{ DBG_TRACE_GRAMMAR(arg_value, | identifier);
			$$ = $1;
		DBG_PRINT}}
	;

opt_full
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_full, : );

			$$ = 0;

		DBG_PRINT}}
	| FULL
		{{ DBG_TRACE_GRAMMAR(arg_vopt_fullalue, | FULL);

			$$ = 1;

		DBG_PRINT}}
	;

of_from_in
	: FROM
	| IN_
	;

opt_for_current_user
	: /* empty */
	| For CURRENT_USER
	| For CURRENT_USER '(' ')'
	;

of_describe_desc_explain
	: DESCRIBE
	| DESC
	| EXPLAIN
	;

of_index_indexes_keys
	: INDEX
	| INDEXES
	| KEYS
	;

update_head
	: UPDATE
		{ DBG_TRACE_GRAMMAR(update_head, : UPDATE);
			PT_NODE* node = parser_new_node(this_parser, PT_UPDATE);
			parser_push_hint_node(node);
		}
	  opt_hint_list
	;

update_stmt
	: update_head
		{ DBG_TRACE_GRAMMAR(update_stmt, : update_head);
			PT_NODE * node = parser_pop_hint_node();
			parser_push_orderby_node (node);
			parser_push_hint_node (node);
		}
	  extended_table_spec_list
	  SET
	  update_assignment_list
	  opt_of_where_cursor
	  opt_using_index_clause
	  opt_update_orderby_clause
	  opt_upd_del_limit_clause
		{{ DBG_TRACE_GRAMMAR(update_stmt, extended_table_spec_list SET update_assignment_list ~ );

			PT_NODE *node = parser_pop_hint_node ();

			node->info.update.spec = CONTAINER_AT_0 ($3);
			node->info.update.assignment = $5;

			if (CONTAINER_AT_0 ($6))
			  {
			    node->info.update.search_cond = CONTAINER_AT_1 ($6);
			  }
			else
			  {
			    node->info.update.cursor_name = CONTAINER_AT_1 ($6);
			  }

			node->info.update.using_index =
			  (node->info.update.using_index ?
			   parser_make_link (node->info.update.using_index, $7) : $7);

			/* set LIMIT node */
			node->info.update.limit = $9;
			node->info.update.rewrite_limit = 1;

			if (node->info.update.spec->next)
			{
			  /* Multi-table update */

			  /* Multi-table update cannot have ORDER BY or LIMIT */
			  if (node->info.update.order_by)
			    {
			      PT_ERRORmf(this_parser, node->info.update.order_by,
			  	       MSGCAT_SET_PARSER_SEMANTIC,
			  	       MSGCAT_SEMANTIC_NOT_ALLOWED_HERE, "ORDER BY");
			    }
			  else if (node->info.update.limit)
			    {
			      PT_ERRORmf(this_parser, node->info.update.limit,
			  	       MSGCAT_SET_PARSER_SEMANTIC,
			  	       MSGCAT_SEMANTIC_NOT_ALLOWED_HERE, "LIMIT");
			    }
			}
		      else
			{
			  /* Single-table update */

			  if (node->info.update.limit
			      && node->info.update.search_cond)
			    {
			      /* For UPDATE statements that have LIMIT clause don't allow
			       * inst_num in search condition
			       */
			      bool instnum_flag = false;
			      (void) parser_walk_tree (this_parser, node->info.update.search_cond,
						       pt_check_instnum_pre, NULL,
						       pt_check_instnum_post, &instnum_flag);
			      if (instnum_flag)
				{
				  PT_ERRORmf(this_parser, node->info.update.search_cond,
					     MSGCAT_SET_PARSER_SEMANTIC,
					     MSGCAT_SEMANTIC_NOT_ALLOWED_IN_LIMIT_CLAUSE, "INST_NUM()/ROWNUM");
				}
			    }
			  else if (node->info.update.limit
				   && node->info.update.cursor_name)
			    {
			      /* It makes no sense to allow LIMIT for UPDATE statements
			       * that use cursor
			       */
			      PT_ERRORmf(this_parser, node->info.update.search_cond,
					 MSGCAT_SET_PARSER_SEMANTIC,
					 MSGCAT_SEMANTIC_NOT_ALLOWED_HERE, "LIMIT");
			    }
			  else if (node->info.update.limit
				   && node->info.update.orderby_for)
			    {
			      bool ordbynum_flag = false;
			       /* check for a ORDERBY_NUM it the orderby_for tree */
			      (void) parser_walk_tree (this_parser, node->info.update.orderby_for,
						       pt_check_orderbynum_pre, NULL,
						       pt_check_orderbynum_post, &ordbynum_flag);

			      if (ordbynum_flag)
				{
				  PT_ERRORmf(this_parser, node->info.update.orderby_for,
					     MSGCAT_SET_PARSER_SEMANTIC,
					     MSGCAT_SEMANTIC_NOT_ALLOWED_IN_LIMIT_CLAUSE, "ORDERBY_NUM()");
				}
			    }
			}

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| update_head
	  OBJECT
	  from_param
	  SET
	  update_assignment_list
		{{ DBG_TRACE_GRAMMAR(update_stmt, | update_head OBJECT from_param SET update_assignment_list);

			PT_NODE *node = parser_pop_hint_node ();
			if (node)
			  {
			    node->info.update.object_parameter = $3;
			    node->info.update.assignment = $5;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;


opt_of_where_cursor
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_of_where_cursor, : );

			container_2 ctn;
			SET_CONTAINER_2 (ctn, 0, NULL);
			$$ = ctn;

		DBG_PRINT}}
	| WHERE
		{ DBG_TRACE_GRAMMAR(opt_of_where_cursor, | WHERE );
			parser_save_and_set_ic(1);
			DBG_PRINT
		}
	  search_condition
	  	{
			parser_restore_ic();
			DBG_PRINT
		}
		{{ DBG_TRACE_GRAMMAR(opt_of_where_cursor, search_condition);

			container_2 ctn;
			SET_CONTAINER_2 (ctn, FROM_NUMBER (1), $3);
			$$ = ctn;

		DBG_PRINT}}
	| WHERE CURRENT OF identifier
		{{ DBG_TRACE_GRAMMAR(opt_of_where_cursor, | WHERE CURRENT OF identifier);

			container_2 ctn;
			SET_CONTAINER_2 (ctn, FROM_NUMBER (0), $4);
			$$ = ctn;

		DBG_PRINT}}
	;


of_class_spec_meta_class_spec
	: class_spec
		{{ DBG_TRACE_GRAMMAR(of_class_spec_meta_class_spec, : class_spec);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| meta_class_spec
		{{ DBG_TRACE_GRAMMAR(of_class_spec_meta_class_spec, | meta_class_spec);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

opt_as_identifier
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_as_identifier, : );

			$$ = NULL;

		DBG_PRINT}}
	| AS identifier
		{{ DBG_TRACE_GRAMMAR(opt_as_identifier, | AS identifier);

			$$ = $2;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| identifier
		{{ DBG_TRACE_GRAMMAR(opt_as_identifier, | identifier);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

update_assignment_list
	: update_assignment_list ',' update_assignment
		{{ DBG_TRACE_GRAMMAR(update_assignment_list, : update_assignment_list ',' update_assignment);

			$$ = parser_make_link ($1, $3);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| update_assignment
		{{ DBG_TRACE_GRAMMAR(update_assignment_list, | update_assignment);

			$$ = $1;

		DBG_PRINT}}
	;

update_assignment
	: path_expression '=' expression_
		{{ DBG_TRACE_GRAMMAR(update_assignment, : path_expression '=' expression_);

			$$ = parser_make_expression (this_parser, PT_ASSIGN, $1, $3, NULL);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| simple_path_id '=' DEFAULT
		{{ DBG_TRACE_GRAMMAR("update_assignment", "| simple_path_id '=' DEFAULT ");

			PT_NODE *node, *node_df = NULL;
			node = parser_copy_tree (this_parser, $1);
			if (node)
			  {
			    pt_set_fill_default_in_path_expression (node);
			    node_df = parser_make_expression (this_parser, PT_DEFAULTF, node, NULL, NULL);
			  }
			$$ = parser_make_expression (this_parser, PT_ASSIGN, $1, node_df, NULL);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| paren_path_expression_set '=' primary_w_collate
		{{ DBG_TRACE_GRAMMAR(update_assignment, | paren_path_expression_set '=' primary_w_collate);

			PT_NODE *exp = parser_make_expression (this_parser, PT_ASSIGN, $1, NULL, NULL);
			PT_NODE *arg1, *arg2, *list, *tmp;
			PT_NODE *e1, *e2 = NULL, *e1_next, *e2_next;
			bool is_subquery = false;
			arg1 = $1;
			arg2 = $3;

						/* primary is parentheses expr set value */
			if (arg2->node_type == PT_VALUE &&
			    (arg2->type_enum == PT_TYPE_NULL || arg2->type_enum == PT_TYPE_EXPR_SET))
			  {

			    /* flatten multi-column assignment expr */
			    if (arg1->node_type == PT_EXPR)
			      {
				/* get elements and free set node */
				e1 = arg1->info.expr.arg1;
				arg1->info.expr.arg1 = NULL;	/* cut-off link */
				parser_free_node (this_parser, exp);	/* free exp, arg1 */

				if (arg2->type_enum == PT_TYPE_NULL)
				  {
				    ;			/* nop */
				  }
				else
				  {
				    e2 = arg2->info.value.data_value.set;
				    arg2->info.value.data_value.set = NULL;	/* cut-off link */
				  }
				parser_free_node (this_parser, arg2);

				list = NULL;		/* init */
				for (; e1; e1 = e1_next)
				  {
				    e1_next = e1->next;
				    e1->next = NULL;
				    if (arg2->type_enum == PT_TYPE_NULL)
				      {
					if ((e2 = parser_new_node (this_parser, PT_VALUE)) == NULL)
					  break;	/* error */
					e2->type_enum = PT_TYPE_NULL;
					e2->flag.is_added_by_parser = 1;
				      }
				    else
				      {
					if (e2 == NULL)
					  break;	/* error */
				      }
				    e2_next = e2->next;
				    e2->next = NULL;

				    tmp = parser_new_node (this_parser, PT_EXPR);
				    if (tmp)
				      {
					tmp->info.expr.op = PT_ASSIGN;
					tmp->info.expr.arg1 = e1;
					tmp->info.expr.arg2 = e2;
				      }
				    list = parser_make_link (tmp, list);

				    e2 = e2_next;
				  }

				PARSER_SAVE_ERR_CONTEXT (list, @$.buffer_pos)
				/* expression number check */
				if (e1 || e2)
				  {
				    PT_ERRORf (this_parser, list,
					       "check syntax at %s, different number of elements in each expression.",
					       pt_show_binopcode (PT_ASSIGN));
				  }

				$$ = list;
			      }
			    else
			      {
				/* something wrong */
				exp->info.expr.arg2 = arg2;
				$$ = exp;
				PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
			      }
			  }
			else
			  {
			    if (pt_is_query (arg2))
			      {
				/* primary is subquery. go ahead */
				is_subquery = true;
			      }

			    exp->info.expr.arg1 = arg1;
			    exp->info.expr.arg2 = arg2;

			    $$ = exp;
			    PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
			    PICE (exp);

			    /* unknown error check */
			    if (is_subquery == false)
			      {
				PT_ERRORf (this_parser, exp, "check syntax at %s",
					   pt_show_binopcode (PT_ASSIGN));
			      }
			  }

		DBG_PRINT}}
	;

paren_path_expression_set
	: '(' path_expression_list ')'
		{{ DBG_TRACE_GRAMMAR(paren_path_expression_set, : '(' path_expression_list ')');

			PT_NODE *p = parser_new_node (this_parser, PT_EXPR);

			if (p)
			  {
			    p->info.expr.op = PT_PATH_EXPR_SET;
			    p->info.expr.paren_type = 1;
			    p->info.expr.arg1 = $2;
			  }

			$$ = p;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

path_expression_list
	: path_expression_list ',' path_expression
		{{ DBG_TRACE_GRAMMAR(path_expression_list, : path_expression_list ',' path_expression);

			$$ = parser_make_link ($1, $3);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| path_expression
		{{ DBG_TRACE_GRAMMAR(path_expression_list, | path_expression);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
		DBG_PRINT}}
	;

delete_name
	: identifier
		{{ DBG_TRACE_GRAMMAR(delete_name, : identifier);

			PT_NODE *node = $1;
			node->info.name.meta_class = PT_CLASS;

			$$ = node;

		DBG_PRINT}}
	| identifier DOT '*'
		{{ DBG_TRACE_GRAMMAR(delete_name, | identifier DOT '*');

			PT_NODE *node = $1;
			node->info.name.meta_class = PT_CLASS;

			$$ = node;

		DBG_PRINT}}
	;

delete_name_list
	: delete_name_list ',' delete_name
		{{ DBG_TRACE_GRAMMAR(delete_name_list, : delete_name_list ',' delete_name);

			$$ = parser_make_link ($1, $3);

		DBG_PRINT}}
	| delete_name
		{{ DBG_TRACE_GRAMMAR(delete_name_list, | delete_name);

			$$ = $1;

		DBG_PRINT}}
	;

delete_from_using
	: delete_name_list FROM extended_table_spec_list
		{{ DBG_TRACE_GRAMMAR(delete_from_using, : delete_name_list FROM extended_table_spec_list);

			container_3 ctn;
			SET_CONTAINER_3(ctn, $1, CONTAINER_AT_0 ($3), CONTAINER_AT_1 ($3));

			$$ = ctn;

		DBG_PRINT}}
	| FROM delete_name_list USING extended_table_spec_list
		{{ DBG_TRACE_GRAMMAR(delete_from_using, | FROM delete_name_list USING extended_table_spec_list);

			container_3 ctn;
			SET_CONTAINER_3(ctn, $2, CONTAINER_AT_0 ($4), CONTAINER_AT_1 ($4));

			$$ = ctn;

		DBG_PRINT}}
	| FROM table_spec
		{{ DBG_TRACE_GRAMMAR(delete_from_using, | FROM table_spec);

			container_3 ctn;
			SET_CONTAINER_3(ctn, NULL, $2, FROM_NUMBER(0));

			$$ = ctn;

		DBG_PRINT}}
	| table_spec
		{{ DBG_TRACE_GRAMMAR(delete_from_using, | table_spec);

			container_3 ctn;
			SET_CONTAINER_3(ctn, NULL, $1, FROM_NUMBER(0));

			$$ = ctn;

		DBG_PRINT}}
	;

delete_stmt
	: DELETE_				/* $1 */
		{ DBG_TRACE_GRAMMAR(delete_stmt, : DELETE_); /* $2 */
			PT_NODE* node = parser_new_node(this_parser, PT_DELETE);
			parser_push_hint_node(node);
		}
	  opt_hint_list 			/* $3 */
	  delete_from_using			/* $4 */
	  opt_of_where_cursor 			/* $5 */
	  opt_using_index_clause 		/* $6 */
	  opt_upd_del_limit_clause		/* $7 */
		{{ DBG_TRACE_GRAMMAR(delete_stmt, opt_hint_list delete_from_using opt_of_where_cursor opt_using_index_clause  opt_upd_del_limit_clause);

			PT_NODE *del = parser_pop_hint_node ();

			if (del)
			  {
			    PT_NODE *node = NULL;
			    del->info.delete_.target_classes = CONTAINER_AT_0 ($4);
			    del->info.delete_.spec = CONTAINER_AT_1 ($4);

			    pt_check_unique_names (this_parser,
						   del->info.delete_.spec);

			    if (TO_NUMBER (CONTAINER_AT_0 ($5)))
			      {
				del->info.delete_.search_cond = CONTAINER_AT_1 ($5);
			      }
			    else
			      {
				del->info.delete_.cursor_name = CONTAINER_AT_1 ($5);
			      }

			    del->info.delete_.using_index =
			      (del->info.delete_.using_index ?
			       parser_make_link (del->info.delete_.using_index, $6) : $6);

			    del->info.delete_.limit = $7;
			    del->info.delete_.rewrite_limit = 1;

			    /* In a multi-table case the LIMIT clauses is not allowed. */
   			    if (del->info.delete_.spec->next)
   			      {
   				if (del->info.delete_.limit)
   				  {
   				    PT_ERRORmf(this_parser, del->info.delete_.limit,
   					       MSGCAT_SET_PARSER_SEMANTIC,
   					       MSGCAT_SEMANTIC_NOT_ALLOWED_HERE, "LIMIT");
   				  }
			      }
			    else
			      {
				/* if the delete is single-table no need to specify
				 * the delete table. In this case add the name of supplied
				 * spec to the target_classes list. */
				if (!del->info.delete_.target_classes)
				  {
				    if (del->info.delete_.spec->info.spec.range_var)
				      {
					del->info.delete_.target_classes =
					  parser_copy_tree(this_parser, del->info.delete_.spec->info.spec.range_var);
				      }
				    else
				      {
					del->info.delete_.target_classes =
					  parser_copy_tree(this_parser, del->info.delete_.spec->info.spec.entity_name);
				      }
				  }

				/* set LIMIT node */
				if (del->info.delete_.limit && del->info.delete_.search_cond)
				  {
				    /* For DELETE statements that have LIMIT clause don't
				     * allow inst_num in search condition */
				    bool instnum_flag = false;
				    (void) parser_walk_tree (this_parser, del->info.delete_.search_cond,
							     pt_check_instnum_pre, NULL,
							     pt_check_instnum_post, &instnum_flag);
				    if (instnum_flag)
				      {
					PT_ERRORmf(this_parser, del->info.delete_.search_cond,
						   MSGCAT_SET_PARSER_SEMANTIC,
						   MSGCAT_SEMANTIC_NOT_ALLOWED_IN_LIMIT_CLAUSE, "INST_NUM()/ROWNUM");
				      }
				  }
				else if (del->info.delete_.limit && del->info.delete_.cursor_name)
				  {
				    /* It makes no sense to allow LIMIT for DELETE statements
				     * that use (Oracle style) cursor */
				    PT_ERRORmf(this_parser, del->info.delete_.search_cond,
					       MSGCAT_SET_PARSER_SEMANTIC,
					       MSGCAT_SEMANTIC_NOT_ALLOWED_HERE, "LIMIT");
				  }
			      }

			    node = del->info.delete_.target_classes;
			    while (node)
			      {
				PT_NAME_INFO_SET_FLAG(node, PT_NAME_ALLOW_REUSABLE_OID);
				node = node->next;
			      }
			  }
			$$ = del;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

merge_stmt
	: MERGE					/* $1 */
		{ DBG_TRACE_GRAMMAR(merge_stmt, : MERGE); /* $2 */
			PT_NODE *merge = parser_new_node (this_parser, PT_MERGE);
			parser_push_hint_node (merge);
		}
	  opt_hint_list				/* $3 */
	  INTO					/* $4 */
	  table_spec				/* $5 */
	  USING					/* $6 */
	  table_spec				/* $7 */
	  ON_					/* $8 */
	  search_condition			/* $9 */
	  merge_update_insert_clause		/* $10 */
		{{ DBG_TRACE_GRAMMAR(merge_stmt, opt_hint_list INTO  table_spec USING table_spec ON_ search_condition merge_update_insert_clause);

			PT_NODE *merge = parser_pop_hint_node ();
			if (merge)
			  {
			    merge->info.merge.into = $5;
			    merge->info.merge.using_clause = $7;
			    merge->info.merge.search_cond = $9;
			  }

			$$ = merge;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

merge_update_insert_clause
	: merge_update_clause
		{{ DBG_TRACE_GRAMMAR(merge_update_insert_clause, : merge_update_clause);
		DBG_PRINT}}
	| merge_insert_clause
		{{ DBG_TRACE_GRAMMAR(merge_update_insert_clause, | merge_insert_clause);
		DBG_PRINT}}
	| merge_update_clause
	  merge_insert_clause
		{{ DBG_TRACE_GRAMMAR(merge_update_insert_clause, | merge_update_clause merge_insert_clause);
		DBG_PRINT}}
	| merge_insert_clause
	  merge_update_clause
		{{ DBG_TRACE_GRAMMAR(merge_update_insert_clause, | merge_insert_clause merge_update_clause);
		DBG_PRINT}}
	;

merge_update_clause
	: WHEN MATCHED THEN UPDATE SET
	  update_assignment_list		/* $6 */
	  opt_where_clause			/* $7 */
	  opt_merge_delete_clause		/* $8 */
		{{ DBG_TRACE_GRAMMAR(merge_update_clause, : WHEN MATCHED THEN UPDATE SET update_assignment_list ~);

			PT_NODE *merge = parser_top_hint_node ();
			if (merge)
			  {
			    merge->info.merge.update.assignment = $6;
			    merge->info.merge.update.search_cond = $7;
			    merge->info.merge.update.del_search_cond = $8;
			    if ($8)
			      {
				merge->info.merge.update.has_delete = true;
			      }
			  }

		DBG_PRINT}}
	;

merge_insert_clause
	: WHEN NOT MATCHED THEN INSERT
	  opt_path_attr_list			/* $6 */
	  insert_expression_value_clause	/* $7 */
	  opt_where_clause			/* $8 */
		{{ DBG_TRACE_GRAMMAR(merge_insert_clause, : WHEN NOT MATCHED THEN INSERT ~ insert_expression_value_clause ~);

			PT_NODE *merge = parser_top_hint_node ();
			if (merge)
			  {
			    merge->info.merge.insert.attr_list = $6;
			    merge->info.merge.insert.value_clauses = $7;
			    merge->info.merge.insert.search_cond = $8;
			  }

		DBG_PRINT}}
	;

opt_merge_delete_clause
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_merge_delete_clause, : );

			$$ = NULL;

		DBG_PRINT}}
	| DELETE_ WHERE search_condition
		{{ DBG_TRACE_GRAMMAR(opt_merge_delete_clause, | DELETE_ WHERE search_condition);

			$$ = $3;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

auth_stmt
	 : grant_head opt_with_grant_option
		{{ DBG_TRACE_GRAMMAR(auth_stmt, : grant_head opt_with_grant_option);

			PT_NODE *node = $1;
			PT_MISC_TYPE w = PT_NO_GRANT_OPTION;
			if ($2)
			  w = PT_GRANT_OPTION;

			if (node)
			  {
			    node->info.grant.grant_option = w;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| revoke_cmd on_class_list from_id_list
		{{ DBG_TRACE_GRAMMAR(auth_stmt, | revoke_cmd on_class_list from_id_list);

			PT_NODE *node = parser_new_node (this_parser, PT_REVOKE);

			if (node)
			  {
			    node->info.revoke.user_list = $3;
			    node->info.revoke.spec_list = $2;
			    node->info.revoke.auth_cmd_list = $1;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| revoke_cmd from_id_list on_class_list
		{{ DBG_TRACE_GRAMMAR(auth_stmt, | revoke_cmd from_id_list on_class_list);

			PT_NODE *node = parser_new_node (this_parser, PT_REVOKE);

			if (node)
			  {
			    node->info.revoke.user_list = $2;
			    node->info.revoke.spec_list = $3;
			    node->info.revoke.auth_cmd_list = $1;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

revoke_cmd
	: REVOKE
		{ push_msg(MSGCAT_SYNTAX_MISSING_AUTH_COMMAND_LIST); }
	  author_cmd_list
		{ pop_msg(); }
		{ DBG_TRACE_GRAMMAR(revoke_cmd, : REVOKE author_cmd_list);
                  $$ = $3; }
	;

grant_cmd
	: GRANT
		{ push_msg(MSGCAT_SYNTAX_MISSING_AUTH_COMMAND_LIST); }
	  author_cmd_list
		{ pop_msg(); }
		{ DBG_TRACE_GRAMMAR(grant_cmd, : GRANT author_cmd_list);
                  $$ = $3; }
	;

grant_head
	: grant_cmd on_class_list to_id_list
		{{ DBG_TRACE_GRAMMAR(grant_head, : grant_cmd on_class_list to_id_list);

			PT_NODE *node = parser_new_node (this_parser, PT_GRANT);

			if (node)
			  {
			    node->info.grant.user_list = $3;
			    node->info.grant.spec_list = $2;
			    node->info.grant.auth_cmd_list = $1;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| grant_cmd to_id_list on_class_list
		{{ DBG_TRACE_GRAMMAR(grant_head, | grant_cmd to_id_list on_class_list);

			PT_NODE *node = parser_new_node (this_parser, PT_GRANT);

			if (node)
			  {
			    node->info.grant.user_list = $2;
			    node->info.grant.spec_list = $3;
			    node->info.grant.auth_cmd_list = $1;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

opt_with_grant_option
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_with_grant_option, : );

			$$ = 0;

		DBG_PRINT}}
	| WITH GRANT OPTION
		{{ DBG_TRACE_GRAMMAR(opt_with_grant_option, | WITH GRANT OPTION);

			$$ = 1;

		DBG_PRINT}}
	;

on_class_list
	: ON_
		{ push_msg(MSGCAT_SYNTAX_MISSING_CLASS_SPEC_LIST); }
	  class_spec_list
		{ pop_msg(); }
		{ DBG_TRACE_GRAMMAR(on_class_list, : ON_ class_spec_list);
                  $$ = $3; }
	;

to_id_list
	: TO
		{ push_msg(MSGCAT_SYNTAX_MISSING_IDENTIFIER_LIST); }
	  identifier_list
		{ pop_msg(); }
		{ DBG_TRACE_GRAMMAR(to_id_list, : TO identifier_list);
                  $$ = $3; }
	;

from_id_list
	: FROM
		{ push_msg(MSGCAT_SYNTAX_MISSING_IDENTIFIER_LIST); }
	  identifier_list
		{ pop_msg(); }
		{ DBG_TRACE_GRAMMAR(from_id_list, : FROM identifier_list);
                  $$ = $3; }
	;

author_cmd_list
	: author_cmd_list ',' authorized_cmd
		{{ DBG_TRACE_GRAMMAR(author_cmd_list, : author_cmd_list ',' authorized_cmd);

			$$ = parser_make_link ($1, $3);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| authorized_cmd
		{{ DBG_TRACE_GRAMMAR(author_cmd_list, | authorized_cmd);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

authorized_cmd
	: SELECT
		{{ DBG_TRACE_GRAMMAR(authorized_cmd, : SELECT);

			PT_NODE *node = parser_new_node (this_parser, PT_AUTH_CMD);
			node->info.auth_cmd.auth_cmd = PT_SELECT_PRIV;
			node->info.auth_cmd.attr_mthd_list = NULL;
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| INSERT
		{{ DBG_TRACE_GRAMMAR(authorized_cmd, | INSERT);

			PT_NODE *node = parser_new_node (this_parser, PT_AUTH_CMD);

			if (node)
			  {
			    node->info.auth_cmd.auth_cmd = PT_INSERT_PRIV;
			    node->info.auth_cmd.attr_mthd_list = NULL;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| INDEX
		{{ DBG_TRACE_GRAMMAR(authorized_cmd, | INDEX);

			PT_NODE *node = parser_new_node (this_parser, PT_AUTH_CMD);

			if (node)
			  {
			    node->info.auth_cmd.auth_cmd = PT_INDEX_PRIV;
			    node->info.auth_cmd.attr_mthd_list = NULL;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| DELETE_
		{{ DBG_TRACE_GRAMMAR(authorized_cmd, | DELETE_);

			PT_NODE *node = parser_new_node (this_parser, PT_AUTH_CMD);

			if (node)
			  {
			    node->info.auth_cmd.auth_cmd = PT_DELETE_PRIV;
			    node->info.auth_cmd.attr_mthd_list = NULL;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}

	| UPDATE '(' identifier_list ')'
		{{ DBG_TRACE_GRAMMAR(authorized_cmd, | UPDATE '(' identifier_list ')');

			PT_NODE *node = parser_new_node (this_parser, PT_AUTH_CMD);
			PARSER_SAVE_ERR_CONTEXT (node, @$.buffer_pos)
			if (node)
			  {
			    node->info.auth_cmd.auth_cmd = PT_UPDATE_PRIV;
			    PT_ERRORmf (this_parser, node, MSGCAT_SET_PARSER_SYNTAX,
					MSGCAT_SYNTAX_ATTR_IN_PRIVILEGE,
					parser_print_tree_list (this_parser, $3));

			    node->info.auth_cmd.attr_mthd_list = $3;
			  }

			$$ = node;

		DBG_PRINT}}
	| UPDATE
		{{ DBG_TRACE_GRAMMAR(authorized_cmd, | UPDATE);

			PT_NODE *node = parser_new_node (this_parser, PT_AUTH_CMD);

			if (node)
			  {
			    node->info.auth_cmd.auth_cmd = PT_UPDATE_PRIV;
			    node->info.auth_cmd.attr_mthd_list = NULL;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| ALTER
		{{ DBG_TRACE_GRAMMAR(authorized_cmd, | ALTER);

			PT_NODE *node = parser_new_node (this_parser, PT_AUTH_CMD);

			if (node)
			  {
			    node->info.auth_cmd.auth_cmd = PT_ALTER_PRIV;
			    node->info.auth_cmd.attr_mthd_list = NULL;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| ADD
		{{ DBG_TRACE_GRAMMAR(authorized_cmd, | ADD);

			PT_NODE *node = parser_new_node (this_parser, PT_AUTH_CMD);

			if (node)
			  {
			    node->info.auth_cmd.auth_cmd = PT_ADD_PRIV;
			    node->info.auth_cmd.attr_mthd_list = NULL;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| DROP
		{{ DBG_TRACE_GRAMMAR(authorized_cmd, | DROP);

			PT_NODE *node = parser_new_node (this_parser, PT_AUTH_CMD);

			if (node)
			  {
			    node->info.auth_cmd.auth_cmd = PT_DROP_PRIV;
			    node->info.auth_cmd.attr_mthd_list = NULL;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| EXECUTE
		{{ DBG_TRACE_GRAMMAR(authorized_cmd, | EXECUTE);

			PT_NODE *node = parser_new_node (this_parser, PT_AUTH_CMD);

			if (node)
			  {
			    node->info.auth_cmd.auth_cmd = PT_EXECUTE_PRIV;
			    node->info.auth_cmd.attr_mthd_list = NULL;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| REFERENCES
		{{ DBG_TRACE_GRAMMAR(authorized_cmd, | REFERENCES);

			PT_NODE *node = parser_new_node (this_parser, PT_AUTH_CMD);

			if (node)
			  {
			    node->info.auth_cmd.auth_cmd = PT_REFERENCES_PRIV;
			    node->info.auth_cmd.attr_mthd_list = NULL;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| ALL PRIVILEGES
		{{ DBG_TRACE_GRAMMAR(authorized_cmd, | ALL PRIVILEGES);

			PT_NODE *node = parser_new_node (this_parser, PT_AUTH_CMD);

			if (node)
			  {
			    node->info.auth_cmd.auth_cmd = PT_ALL_PRIV;
			    node->info.auth_cmd.attr_mthd_list = NULL;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| ALL
		{{ DBG_TRACE_GRAMMAR(authorized_cmd, | ALL);

			PT_NODE *node = parser_new_node (this_parser, PT_AUTH_CMD);

			if (node)
			  {
			    node->info.auth_cmd.auth_cmd = PT_ALL_PRIV;
			    node->info.auth_cmd.attr_mthd_list = NULL;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

opt_password
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_password, : );

			$$ = NULL;

		DBG_PRINT}}
	| PASSWORD
		{ push_msg(MSGCAT_SYNTAX_INVALID_PASSWORD); }
	  char_string_literal
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(opt_password, | PASSWORD char_string_literal);

			$$ = $3;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

opt_groups
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_groups, : );

			$$ = NULL;

		DBG_PRINT}}
	| GROUPS
		{ push_msg(MSGCAT_SYNTAX_INVALID_GROUPS); }
	  identifier_list
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(opt_groups, | GROUPS identifier_list);

			$$ = $3;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

opt_members
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_members, : );

			$$ = NULL;

		DBG_PRINT}}
	| MEMBERS
		{ push_msg(MSGCAT_SYNTAX_INVALID_MEMBERS); }
	  identifier_list
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(opt_members, | MEMBERS identifier_list);

			$$ = $3;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

call_stmt
	: CALL generic_function into_clause_opt
		{{ DBG_TRACE_GRAMMAR(call_stmt, : CALL generic_function into_clause_opt);

			PT_NODE *node = $2;
			if (node)
			  {
			    node->info.method_call.call_or_expr = PT_IS_CALL_STMT;
			    node->info.method_call.to_return_var = $3;
			  }

			parser_cannot_prepare = true;
			parser_cannot_cache = true;

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

opt_class_or_normal_attr_def_list
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_class_or_normal_attr_def_list, : );

			$$ = NULL;

		DBG_PRINT}}
	| '(' class_or_normal_attr_def_list ')'
		{{ DBG_TRACE_GRAMMAR(opt_class_or_normal_attr_def_list, | '(' class_or_normal_attr_def_list ')');

			$$ = $2;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

opt_method_def_list
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_method_def_list, : );

			$$ = NULL;

		DBG_PRINT}}
	| METHOD method_def_list
		{{ DBG_TRACE_GRAMMAR(opt_method_def_list, | METHOD method_def_list);

			$$ = $2;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

opt_method_files
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_method_files, : );

			$$ = NULL;

		DBG_PRINT}}
	| File method_file_list
		{{ DBG_TRACE_GRAMMAR(opt_method_files, | File method_file_list);

			$$ = $2;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

opt_inherit_resolution_list
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_inherit_resolution_list, : );

			$$ = NULL;

		DBG_PRINT}}
	| inherit_resolution_list
		{{ DBG_TRACE_GRAMMAR(opt_inherit_resolution_list, | inherit_resolution_list);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

opt_table_option_list
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_table_option_list, : );


			$$ = NULL;

		DBG_PRINT}}
	| table_option_list
		{{ DBG_TRACE_GRAMMAR(opt_table_option_list, | table_option_list);


			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

opt_partition_clause
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_partition_clause, : );

			$$ = NULL;

		DBG_PRINT}}
	| partition_clause
		{{ DBG_TRACE_GRAMMAR(opt_partition_clause, | partition_clause);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

opt_create_as_clause
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_create_as_clause, : );

			container_2 ctn;
			SET_CONTAINER_2(ctn, NULL, NULL);
			$$ = ctn;

		DBG_PRINT}}
	| create_as_clause
		{{ DBG_TRACE_GRAMMAR(opt_create_as_clause, | create_as_clause);

			$$ = $1;

		DBG_PRINT}}
	;

of_class_table_type
	: CLASS
		{{ DBG_TRACE_GRAMMAR(of_class_table_type, : CLASS);

			$$ = PT_CLASS;

		DBG_PRINT}}
	| TABLE
		{{ DBG_TRACE_GRAMMAR(of_class_table_type, | TABLE);

			$$ = PT_CLASS;

		DBG_PRINT}}
	;

of_view_vclass
	: VIEW
	| VCLASS
	;

opt_or_replace
	: /*empty*/
		{{ DBG_TRACE_GRAMMAR(opt_or_replace, : );

			$$ = 0;

		DBG_PRINT}}
	| OR REPLACE
		{{ DBG_TRACE_GRAMMAR(opt_or_replace, | OR REPLACE);

			$$ = 1;

		DBG_PRINT}}
	;

opt_if_not_exists
	: /*empty*/
		{{ DBG_TRACE_GRAMMAR(opt_if_not_exists, : );

			$$ = 0;

		DBG_PRINT}}
	| IF NOT EXISTS
		{{ DBG_TRACE_GRAMMAR(opt_if_not_exists, |  IF NOT EXISTS);

			$$ = 1;

		DBG_PRINT}}
	;

opt_if_exists
	: /*empty*/
		{{ DBG_TRACE_GRAMMAR(opt_if_exists, : );

			$$ = 0;

		DBG_PRINT}}
	| IF EXISTS
		{{ DBG_TRACE_GRAMMAR(opt_if_exists, | IF EXISTS);

			$$ = 1;

		DBG_PRINT}}
	;

opt_paren_view_attr_def_list
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_paren_view_attr_def_list, : );

			$$ = NULL;

		DBG_PRINT}}
	| '(' view_attr_def_list ')'
		{{ DBG_TRACE_GRAMMAR(opt_opt_paren_view_attr_def_listif_exists, | '(' view_attr_def_list ')');

			$$ = $2;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

opt_as_query_list
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_as_query_list, : );

			$$ = NULL;

		DBG_PRINT}}
	| AS query_list
		{{ DBG_TRACE_GRAMMAR(opt_as_query_list, | AS query_list);

			$$ = $2;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

opt_with_levels_clause
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_with_levels_clause, : );

			$$ = PT_EMPTY;

		DBG_PRINT}}
	| WITH LOCAL CHECK OPTION
		{{ DBG_TRACE_GRAMMAR(opt_with_levels_clause, | WITH LOCAL CHECK OPTION);

			$$ = PT_LOCAL;

		DBG_PRINT}}
	| WITH CASCADED CHECK OPTION
		{{ DBG_TRACE_GRAMMAR(opt_with_levels_clause, | WITH CASCADED CHECK OPTION);

			$$ = PT_CASCADED;

		DBG_PRINT}}
	| WITH CHECK OPTION
		{{ DBG_TRACE_GRAMMAR(opt_with_levels_clause, | WITH CHECK OPTION);

			$$ = PT_CASCADED;

		DBG_PRINT}}
	;

query_list
	: query_list ',' csql_query
		{{ DBG_TRACE_GRAMMAR(query_list, : query_list ',' csql_query);

			$$ = parser_make_link ($1, $3);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| csql_query
		{{ DBG_TRACE_GRAMMAR(query_list, | csql_query);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

inherit_resolution_list
	: inherit_resolution_list  ',' inherit_resolution
		{{ DBG_TRACE_GRAMMAR(inherit_resolution_list, : inherit_resolution_list  ',' inherit_resolution);

			$$ = parser_make_link ($1, $3);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| INHERIT inherit_resolution
		{{ DBG_TRACE_GRAMMAR(inherit_resolution_list, | INHERIT inherit_resolution);

			$$ = $2;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

inherit_resolution
	: opt_class identifier OF class_name AS identifier
		{{ DBG_TRACE_GRAMMAR(inherit_resolution, : opt_class identifier OF class_name AS identifier);

			PT_NODE *node = parser_new_node (this_parser, PT_RESOLUTION);

			if (node)
			  {
			    PT_MISC_TYPE t = PT_NORMAL;

			    if ($1)
			      t = PT_META_ATTR;
			    node->info.resolution.of_sup_class_name = $4;
			    node->info.resolution.attr_mthd_name = $2;
			    node->info.resolution.attr_type = t;
			    node->info.resolution.as_attr_mthd_name = $6;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| opt_class identifier OF class_name
		{{ DBG_TRACE_GRAMMAR(inherit_resolution, | opt_class identifier OF class_name);

			PT_NODE *node = parser_new_node (this_parser, PT_RESOLUTION);

			if (node)
			  {
			    PT_MISC_TYPE t = PT_NORMAL;

			    if ($1)
			      t = PT_META_ATTR;
			    node->info.resolution.of_sup_class_name = $4;
			    node->info.resolution.attr_mthd_name = $2;
			    node->info.resolution.attr_type = t;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

table_option_list
	: table_option_list ',' table_option
		{{ DBG_TRACE_GRAMMAR(table_option_list, : table_option_list ',' table_option);

			$$ = parser_make_link ($1, $3);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| table_option_list table_option
		{{ DBG_TRACE_GRAMMAR(table_option_list, | table_option_list table_option);

			$$ = parser_make_link ($1, $2);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| table_option
		{{ DBG_TRACE_GRAMMAR(table_option_list, | table_option);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

table_option
	: REUSE_OID
		{{ DBG_TRACE_GRAMMAR(table_option, : REUSE_OID);

			$$ = pt_table_option (this_parser, PT_TABLE_OPTION_REUSE_OID, NULL);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| DONT_REUSE_OID
		{{ DBG_TRACE_GRAMMAR(table_option, | DONT_REUSE_OID);

			$$ = pt_table_option (this_parser, PT_TABLE_OPTION_DONT_REUSE_OID, NULL);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| AUTO_INCREMENT '=' UNSIGNED_INTEGER
		{{ DBG_TRACE_GRAMMAR(table_option, | AUTO_INCREMENT '=' UNSIGNED_INTEGER);

			PT_NODE *val = parser_new_node (this_parser, PT_VALUE);
			if (val)
			{
			  val->info.value.data_value.str =
			    pt_append_bytes (this_parser, NULL, $3,
					     strlen ($3));
			  val->type_enum = PT_TYPE_NUMERIC;
			  PT_NODE_PRINT_VALUE_TO_TEXT (this_parser, val);
			}

			$$ = pt_table_option (this_parser, PT_TABLE_OPTION_AUTO_INCREMENT, val);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		  DBG_PRINT}}
	| class_encrypt_spec
                {{ DBG_TRACE_GRAMMAR(table_option, | class_encrypt_spec);
	
                        $$ = pt_table_option (this_parser, PT_TABLE_OPTION_ENCRYPT, $1);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		  DBG_PRINT}}
	| charset_spec
		{{ DBG_TRACE_GRAMMAR(table_option, | charset_spec);

			$$ = pt_table_option (this_parser, PT_TABLE_OPTION_CHARSET, $1);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		  DBG_PRINT}}
	| collation_spec
		{{ DBG_TRACE_GRAMMAR(table_option, | collation_spec);

			$$ = pt_table_option (this_parser, PT_TABLE_OPTION_COLLATION, $1);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		  DBG_PRINT}}
	| class_comment_spec
		{{ DBG_TRACE_GRAMMAR(table_option, | class_comment_spec);

			$$ = pt_table_option (this_parser, PT_TABLE_OPTION_COMMENT, $1);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

	      DBG_PRINT}}
	;

opt_subtable_clause
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_subtable_clause, : );

			$$ = NULL;

		DBG_PRINT}}
	| UNDER only_class_name_list
		{{ DBG_TRACE_GRAMMAR(opt_subtable_clause, | UNDER only_class_name_list);

			$$ = $2;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| AS SUBCLASS OF only_class_name_list
		{{ DBG_TRACE_GRAMMAR(opt_subtable_clause, | AS SUBCLASS OF only_class_name_list);

			$$ = $4;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

opt_constraint_id
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_constraint_id, : );

			$$ = NULL;

		DBG_PRINT}}
	| CONSTRAINT identifier
		{{ DBG_TRACE_GRAMMAR(opt_constraint_id, | CONSTRAINT identifier);

			$$ = $2;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

opt_constraint_opt_id
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_constraint_opt_id, : );

			$$ = NULL;

		DBG_PRINT}}
	| CONSTRAINT opt_identifier
		{{ DBG_TRACE_GRAMMAR(opt_constraint_opt_id, | CONSTRAINT opt_identifier);

			$$ = $2;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

of_unique_foreign_check
	: unique_constraint
		{{ DBG_TRACE_GRAMMAR(of_unique_foreign_check, : unique_constraint);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| foreign_key_constraint
		{{ DBG_TRACE_GRAMMAR(of_unique_foreign_check, | foreign_key_constraint);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| check_constraint
		{{ DBG_TRACE_GRAMMAR(of_unique_foreign_check, | check_constraint);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

opt_constraint_attr_list
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_constraint_attr_list, : );

			container_4 ctn;
			SET_CONTAINER_4 (ctn, FROM_NUMBER (0), FROM_NUMBER (0), FROM_NUMBER (0),
					 FROM_NUMBER (0));
			$$ = ctn;

		DBG_PRINT}}
	| constraint_attr_list
		{{ DBG_TRACE_GRAMMAR(opt_constraint_attr_list, | constraint_attr_list);

			$$ = $1;

		DBG_PRINT}}
	;

constraint_attr_list
	: constraint_attr_list ',' constraint_attr
		{{ DBG_TRACE_GRAMMAR(constraint_attr_list, : constraint_attr_list ',' constraint_attr);

			container_4 ctn = $1;
			container_4 ctn_new = $3;

			if (TO_NUMBER (ctn_new.c1))
			  {
			    ctn.c1 = ctn_new.c1;
			    ctn.c2 = ctn_new.c2;
			  }

			if (TO_NUMBER (ctn_new.c3))
			  {
			    ctn.c3 = ctn_new.c3;
			    ctn.c4 = ctn_new.c4;
			  }

			$$ = ctn;

		DBG_PRINT}}
	| constraint_attr
		{{ DBG_TRACE_GRAMMAR(constraint_attr_list, | constraint_attr);

			$$ = $1;

		DBG_PRINT}}
	;

unique_constraint
	: PRIMARY KEY opt_identifier '(' index_column_identifier_list ')'
		{{ DBG_TRACE_GRAMMAR(unique_constraint, : PRIMARY KEY opt_identifier '(' index_column_identifier_list ')');

			PT_NODE *node = parser_new_node (this_parser, PT_CONSTRAINT);

			if (node)
			  {
			    node->info.constraint.type = PT_CONSTRAIN_PRIMARY_KEY;
			    node->info.constraint.name = $3;
			    node->info.constraint.un.unique.attrs = $5;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| UNIQUE opt_of_index_key opt_identifier index_column_name_list
		{{ DBG_TRACE_GRAMMAR(unique_constraint, | UNIQUE opt_of_index_key opt_identifier index_column_name_list);

			PT_NODE *node = NULL;
			PT_NODE *sort_spec_cols = $4, *name_cols = NULL, *temp;

			for (temp = sort_spec_cols; temp != NULL; temp = temp->next)
			  {
			    if (temp->info.sort_spec.expr->node_type == PT_EXPR)
			      {
			        /* Currently, not allowed unique with filter/function
			           index. However, may be introduced later, if it will be
			           usefull. Unique filter/function index code is removed
			           from grammar module only. It is kept yet in the others
			           modules. This will allow us to easily support this
			           feature later by adding in grammar only. If no need
			           such feature, filter/function code must be removed
			           from all modules. */
			         PT_ERRORm (this_parser, node, MSGCAT_SET_PARSER_SYNTAX,
			                    MSGCAT_SYNTAX_INVALID_CREATE_INDEX);
			      }
			  }
			name_cols = pt_sort_spec_list_to_name_node_list (this_parser, sort_spec_cols);
			if (name_cols)
			  {
			    /* create constraint node */
			    node = parser_new_node (this_parser, PT_CONSTRAINT);
			    PARSER_SAVE_ERR_CONTEXT (node, @$.buffer_pos)
			    if (node)
			      {
				node->info.constraint.type = PT_CONSTRAIN_UNIQUE;
				node->info.constraint.name = $3;
				node->info.constraint.un.unique.attrs = name_cols;
			      }
			    parser_free_tree (this_parser, sort_spec_cols);
			  }
			else
			  {
			    /* create index node */

			    if (parser_count_list (sort_spec_cols) == 1
				&& (sort_spec_cols->info.sort_spec.expr->node_type != PT_EXPR))
			      {
				/* unique index with prefix length not allowed */
				PT_ERRORm (this_parser, node, MSGCAT_SET_PARSER_SYNTAX,
					   MSGCAT_SYNTAX_INVALID_CREATE_INDEX);
			      }
			    else
			      {
				node = parser_new_node (this_parser, PT_CREATE_INDEX);
				PARSER_SAVE_ERR_CONTEXT (node, @$.buffer_pos)
				if (node)
				  {
				    node->info.index.index_name = $3;
				    if (node->info.index.index_name)
				      {
					node->info.index.index_name->info.name.meta_class = PT_INDEX_NAME;
				      }

				    node->info.index.indexed_class = NULL;
				    node->info.index.column_names = sort_spec_cols;
				    node->info.index.unique = 1;
				    node->info.index.index_status = SM_NORMAL_INDEX;
				  }
			      }
			  }

			$$ = node;

		DBG_PRINT}}
	;

foreign_key_constraint
	: FOREIGN 					/* 1 */
	  KEY 						/* 2 */
	  opt_identifier				/* 3 */
	  '(' index_column_identifier_list ')'		/* 4, 5, 6 */
	  REFERENCES					/* 7 */
	  user_specified_name				/* 8 */
	  opt_paren_attr_list				/* 9 */
	  opt_ref_rule_list				/* 10 */
		{{ DBG_TRACE_GRAMMAR(foreign_key_constraint, : FOREIGN KEY ~ '(' index_column_identifier_list ')' REFERENCES ~);

			PT_NODE *node = parser_new_node (this_parser, PT_CONSTRAINT);

			if (node)
			  {
			    node->info.constraint.name = $3;
			    node->info.constraint.type = PT_CONSTRAIN_FOREIGN_KEY;
			    node->info.constraint.un.foreign_key.attrs = $5;

			    node->info.constraint.un.foreign_key.referenced_attrs = $9;
			    node->info.constraint.un.foreign_key.match_type = PT_MATCH_REGULAR;
			    node->info.constraint.un.foreign_key.delete_action = TO_NUMBER (CONTAINER_AT_0 ($10));	/* delete_action */
			    node->info.constraint.un.foreign_key.update_action = TO_NUMBER (CONTAINER_AT_1 ($10));	/* update_action */
			    node->info.constraint.un.foreign_key.referenced_class = $8;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

index_column_identifier_list
	: index_column_identifier_list ',' index_column_identifier
		{{ DBG_TRACE_GRAMMAR(index_column_identifier_list, : index_column_identifier_list ',' index_column_identifier);

			$$ = parser_make_link ($1, $3);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| index_column_identifier
		{{ DBG_TRACE_GRAMMAR(index_column_identifier_list, | index_column_identifier);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

index_column_identifier
	: identifier opt_asc_or_desc
		{{ DBG_TRACE_GRAMMAR(index_column_identifier, : identifier opt_asc_or_desc);

			if ($2)
			  {
			    PT_NAME_INFO_SET_FLAG ($1, PT_NAME_INFO_DESC);
			  }
			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

opt_asc_or_desc
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_asc_or_desc, : );

			$$ = 0;

		DBG_PRINT}}
	| ASC
		{{ DBG_TRACE_GRAMMAR(opt_asc_or_desc, | ASC);

			$$ = 0;

		DBG_PRINT}}
	| DESC
		{{ DBG_TRACE_GRAMMAR(opt_asc_or_desc, | DESC);

			$$ = 1;

		DBG_PRINT}}
	;

opt_paren_attr_list
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_paren_attr_list, : );

			$$ = NULL;

		DBG_PRINT}}
	| '(' identifier_list ')'
		{{ DBG_TRACE_GRAMMAR(opt_paren_attr_list, | '(' identifier_list ')');

			$$ = $2;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

opt_ref_rule_list
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_ref_rule_list, : );

			container_3 ctn;
			SET_CONTAINER_3 (ctn, FROM_NUMBER (PT_RULE_RESTRICT),
					 FROM_NUMBER (PT_RULE_RESTRICT), NULL);
			$$ = ctn;

		DBG_PRINT}}
	| ref_rule_list
		{{ DBG_TRACE_GRAMMAR(opt_ref_rule_list, | ref_rule_list);

			container_3 ctn = $1;
			if (ctn.c1 == NULL)
			  ctn.c1 = FROM_NUMBER (PT_RULE_RESTRICT);
			if (ctn.c2 == NULL)
			  ctn.c2 = FROM_NUMBER (PT_RULE_RESTRICT);
			$$ = ctn;

		DBG_PRINT}}
	;

ref_rule_list
	: ref_rule_list ON_ DELETE_ CASCADE
		{{ DBG_TRACE_GRAMMAR(ref_rule_list, : ref_rule_list ON_ DELETE_ CASCADE);

			container_3 ctn = $1;
			if (ctn.c1 != NULL)
			  {
			    push_msg (MSGCAT_SYNTAX_DUPLICATED_REF_RULE);
			    csql_yyerror_explicit (@2.first_line, @2.first_column);
			  }

			ctn.c1 = FROM_NUMBER (PT_RULE_CASCADE);
			$$ = ctn;

		DBG_PRINT}}
	| ref_rule_list ON_ DELETE_ NO ACTION
		{{ DBG_TRACE_GRAMMAR(ref_rule_list, | ref_rule_list ON_ DELETE_ NO ACTION);

			container_3 ctn = $1;
			if (ctn.c1 != NULL)
			  {
			    push_msg (MSGCAT_SYNTAX_DUPLICATED_REF_RULE);
			    csql_yyerror_explicit (@2.first_line, @2.first_column);
			  }

			ctn.c1 = FROM_NUMBER (PT_RULE_NO_ACTION);
			$$ = ctn;

		DBG_PRINT}}
	| ref_rule_list ON_ DELETE_ RESTRICT
		{{ DBG_TRACE_GRAMMAR(ref_rule_list, | ref_rule_list ON_ DELETE_ RESTRICT);

			container_3 ctn = $1;
			if (ctn.c1 != NULL)
			  {
			    push_msg (MSGCAT_SYNTAX_DUPLICATED_REF_RULE);
			    csql_yyerror_explicit (@2.first_line, @2.first_column);
			  }

			ctn.c1 = FROM_NUMBER (PT_RULE_RESTRICT);
			$$ = ctn;

		DBG_PRINT}}
	| ref_rule_list ON_ DELETE_ SET Null
		{{ DBG_TRACE_GRAMMAR(ref_rule_list, | ref_rule_list ON_ DELETE_ SET Null);

			container_3 ctn = $1;
			if (ctn.c1 != NULL)
			  {
			    push_msg (MSGCAT_SYNTAX_DUPLICATED_REF_RULE);
			    csql_yyerror_explicit (@2.first_line, @2.first_column);
			  }

			ctn.c1 = FROM_NUMBER (PT_RULE_SET_NULL);
			$$ = ctn;

		DBG_PRINT}}
	| ref_rule_list ON_ UPDATE NO ACTION
		{{ DBG_TRACE_GRAMMAR(ref_rule_list, | ref_rule_list ON_ UPDATE NO ACTION);

			container_3 ctn = $1;
			if (ctn.c2 != NULL)
			  {
			    push_msg (MSGCAT_SYNTAX_DUPLICATED_REF_RULE);
			    csql_yyerror_explicit (@2.first_line, @2.first_column);
			  }

			ctn.c2 = FROM_NUMBER (PT_RULE_NO_ACTION);
			$$ = ctn;

		DBG_PRINT}}
	| ref_rule_list ON_ UPDATE RESTRICT
		{{ DBG_TRACE_GRAMMAR(ref_rule_list, | ref_rule_list ON_ UPDATE RESTRICT);

			container_3 ctn = $1;
			if (ctn.c2 != NULL)
			  {
			    push_msg (MSGCAT_SYNTAX_DUPLICATED_REF_RULE);
			    csql_yyerror_explicit (@2.first_line, @2.first_column);
			  }

			ctn.c2 = FROM_NUMBER (PT_RULE_RESTRICT);
			$$ = ctn;

		DBG_PRINT}}
	| ref_rule_list ON_ UPDATE SET Null
		{{ DBG_TRACE_GRAMMAR(ref_rule_list, | ref_rule_list ON_ UPDATE SET Null);

			container_3 ctn = $1;
			if (ctn.c2 != NULL)
			  {
			    push_msg (MSGCAT_SYNTAX_DUPLICATED_REF_RULE);
			    csql_yyerror_explicit (@2.first_line, @2.first_column);
			  }

			ctn.c2 = FROM_NUMBER (PT_RULE_SET_NULL);
			$$ = ctn;

		DBG_PRINT}}
	| ON_ DELETE_ CASCADE
		{{ DBG_TRACE_GRAMMAR(ref_rule_list, | ON_ DELETE_ CASCADE);

			container_3 ctn;
			SET_CONTAINER_3 (ctn, FROM_NUMBER (PT_RULE_CASCADE), NULL, NULL);
			$$ = ctn;

		DBG_PRINT}}
	| ON_ DELETE_ NO ACTION
		{{ DBG_TRACE_GRAMMAR(ref_rule_list, | ON_ DELETE_ NO ACTION);

			container_3 ctn;
			SET_CONTAINER_3 (ctn, FROM_NUMBER (PT_RULE_NO_ACTION), NULL, NULL);
			$$ = ctn;

		DBG_PRINT}}
	| ON_ DELETE_ RESTRICT
		{{ DBG_TRACE_GRAMMAR(ref_rule_list, | ON_ DELETE_ RESTRICT);

			container_3 ctn;
			SET_CONTAINER_3 (ctn, FROM_NUMBER (PT_RULE_RESTRICT), NULL, NULL);
			$$ = ctn;

		DBG_PRINT}}
	| ON_ DELETE_ SET Null
		{{ DBG_TRACE_GRAMMAR(ref_rule_list, | ON_ DELETE_ SET Null);

			container_3 ctn;
			SET_CONTAINER_3 (ctn, FROM_NUMBER (PT_RULE_SET_NULL), NULL, NULL);
			$$ = ctn;

		DBG_PRINT}}
	| ON_ UPDATE NO ACTION
		{{ DBG_TRACE_GRAMMAR(ref_rule_list, | ON_ UPDATE NO ACTION);

			container_3 ctn;
			SET_CONTAINER_3 (ctn, NULL, FROM_NUMBER (PT_RULE_NO_ACTION), NULL);
			$$ = ctn;

		DBG_PRINT}}
	| ON_ UPDATE RESTRICT
		{{ DBG_TRACE_GRAMMAR(ref_rule_list, | ON_ UPDATE RESTRICT);

			container_3 ctn;
			SET_CONTAINER_3 (ctn, NULL, FROM_NUMBER (PT_RULE_RESTRICT), NULL);
			$$ = ctn;

		DBG_PRINT}}
	| ON_ UPDATE SET Null
		{{ DBG_TRACE_GRAMMAR(ref_rule_list, | ON_ UPDATE SET Null);

			container_3 ctn;
			SET_CONTAINER_3 (ctn, NULL, FROM_NUMBER (PT_RULE_SET_NULL), NULL);
			$$ = ctn;

		DBG_PRINT}}
	;


check_constraint
	: CHECK '(' search_condition ')'
		{{ DBG_TRACE_GRAMMAR(check_constraint, : CHECK '(' search_condition ')');

			PT_NODE *node = parser_new_node (this_parser, PT_CONSTRAINT);

			if (node)
			  {
			    node->info.constraint.type = PT_CONSTRAIN_CHECK;
			    node->info.constraint.un.check.expr = $3;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;


/* bool_deferrable, deferrable value, bool_initially_deferred, initially_deferred value */
constraint_attr
	: NOT DEFERRABLE
		{{ DBG_TRACE_GRAMMAR(constraint_attr, : NOT DEFERRABLE);

			container_4 ctn;
			ctn.c1 = FROM_NUMBER (1);
			ctn.c2 = FROM_NUMBER (0);
			ctn.c3 = FROM_NUMBER (0);
			ctn.c4 = FROM_NUMBER (0);
			$$ = ctn;

		DBG_PRINT}}
	| DEFERRABLE
		{{ DBG_TRACE_GRAMMAR(constraint_attr, | DEFERRABLE);

			container_4 ctn;
			ctn.c1 = FROM_NUMBER (1);
			ctn.c2 = FROM_NUMBER (1);
			ctn.c3 = FROM_NUMBER (0);
			ctn.c4 = FROM_NUMBER (0);
			$$ = ctn;

		DBG_PRINT}}
	| INITIALLY DEFERRED
		{{ DBG_TRACE_GRAMMAR(constraint_attr, | INITIALLY DEFERRED);

			container_4 ctn;
			ctn.c1 = FROM_NUMBER (0);
			ctn.c2 = FROM_NUMBER (0);
			ctn.c3 = FROM_NUMBER (1);
			ctn.c4 = FROM_NUMBER (1);
			$$ = ctn;

		DBG_PRINT}}
	| INITIALLY IMMEDIATE
		{{ DBG_TRACE_GRAMMAR(constraint_attr, | INITIALLY IMMEDIATE);

			container_4 ctn;
			ctn.c1 = FROM_NUMBER (0);
			ctn.c2 = FROM_NUMBER (0);
			ctn.c3 = FROM_NUMBER (1);
			ctn.c4 = FROM_NUMBER (0);
			$$ = ctn;

		DBG_PRINT}}
	;

method_def_list
	: method_def_list ',' method_def
		{{ DBG_TRACE_GRAMMAR(method_def_list, : method_def_list ',' method_def);

			$$ = parser_make_link ($1, $3);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| method_def
		{{ DBG_TRACE_GRAMMAR(method_def_list, | method_def);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

method_def
	: opt_class
	  identifier
	  opt_method_def_arg_list
	  opt_data_type
	  opt_function_identifier
		{{ DBG_TRACE_GRAMMAR(method_def, : opt_class identifier opt_method_def_arg_list opt_data_type opt_function_identifier);

			PT_NODE *node = parser_new_node (this_parser, PT_METHOD_DEF);
			PT_MISC_TYPE t = PT_NORMAL;
			if ($1)
			  t = PT_META_ATTR;

			if (node)
			  {
			    node->info.method_def.method_name = $2;
			    node->info.method_def.mthd_type = t;
			    node->info.method_def.method_args_list = $3;
			    node->type_enum = TO_NUMBER (CONTAINER_AT_0 ($4));
			    node->data_type = CONTAINER_AT_1 ($4);
			    node->info.method_def.function_name = $5;
			  }
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

opt_method_def_arg_list
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_method_def_arg_list, : );

			$$ = NULL;

		DBG_PRINT}}
	| '(' arg_type_list ')'
		{{ DBG_TRACE_GRAMMAR(opt_method_def_arg_list, | '(' arg_type_list ')');

			$$ = $2;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| '(' ')'
		{{ DBG_TRACE_GRAMMAR(opt_method_def_arg_list, | '(' ')');

			$$ = NULL;

		DBG_PRINT}}
	;

arg_type_list
	: arg_type_list ',' inout_data_type
		{{ DBG_TRACE_GRAMMAR(arg_type_list, : arg_type_list ',' inout_data_type);

			$$ = parser_make_link ($1, $3);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| inout_data_type
		{{ DBG_TRACE_GRAMMAR(arg_type_list, | inout_data_type);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

inout_data_type
	: opt_in_out data_type
		{{ DBG_TRACE_GRAMMAR(inout_data_type, : opt_in_out data_type);

			PT_NODE *at = parser_new_node (this_parser, PT_DATA_TYPE);

			if (at)
			  {
			    at->type_enum = TO_NUMBER (CONTAINER_AT_0 ($2));
			    at->data_type = CONTAINER_AT_1 ($2);
			    at->info.data_type.inout = $1;
			  }

			$$ = at;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

opt_data_type
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_data_type, : );

			container_2 ctn;
			SET_CONTAINER_2 (ctn, FROM_NUMBER (PT_TYPE_NONE), NULL);
			$$ = ctn;

		DBG_PRINT}}
	| data_type
		{{ DBG_TRACE_GRAMMAR(opt_data_type, | data_type);

			$$ = $1;

		DBG_PRINT}}
	;

opt_function_identifier
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_function_identifier, : );

			$$ = NULL;

		DBG_PRINT}}
	| FUNCTION identifier
		{{ DBG_TRACE_GRAMMAR(opt_function_identifier, | FUNCTION identifier);

			$$ = $2;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

method_file_list
	: method_file_list ',' file_path_name
		{{ DBG_TRACE_GRAMMAR(method_file_list, : method_file_list ',' file_path_name);

			$$ = parser_make_link ($1, $3);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| file_path_name
		{{ DBG_TRACE_GRAMMAR(method_file_list, | file_path_name);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

file_path_name
	: char_string_literal
		{{ DBG_TRACE_GRAMMAR(file_path_name, : char_string_literal);

			PT_NODE *node = parser_new_node (this_parser, PT_FILE_PATH);
			if (node)
			  node->info.file_path.string = $1;
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

opt_class_attr_def_list
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_class_attr_def_list, : );

			$$ = NULL;

		DBG_PRINT}}
	| CLASS
	  ATTRIBUTE
		{ parser_attr_type = PT_META_ATTR; }
	 '(' attr_def_list ')'
		{ parser_attr_type = PT_NORMAL; }
		{{ DBG_TRACE_GRAMMAR(opt_class_attr_def_list, | CLASS ATTRIBUTE '(' attr_def_list ')');

			$$ = $5;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

class_or_normal_attr_def_list
	: class_or_normal_attr_def_list  ',' class_or_normal_attr_def
		{{ DBG_TRACE_GRAMMAR(class_or_normal_attr_def_list, : class_or_normal_attr_def_list  ',' class_or_normal_attr_def);

			$$ = parser_make_link ($1, $3);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| class_or_normal_attr_def
		{{ DBG_TRACE_GRAMMAR(class_or_normal_attr_def_list, | class_or_normal_attr_def);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

class_or_normal_attr_def
	: CLASS { parser_attr_type = PT_META_ATTR; } attr_def { parser_attr_type = PT_NORMAL; }
		{{ DBG_TRACE_GRAMMAR(class_or_normal_attr_def, : CLASS attr_def);

			$$ = $3;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| attr_def
		{{ DBG_TRACE_GRAMMAR(class_or_normal_attr_def, | attr_def);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

view_attr_def_list
	: view_attr_def_list ',' view_attr_def
		{{ DBG_TRACE_GRAMMAR(class_or_normal_attr_def, : view_attr_def_list ',' view_attr_def);

			$$ = parser_make_link ($1, $3);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| view_attr_def
		{{ DBG_TRACE_GRAMMAR(class_or_normal_attr_def, | view_attr_def);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

view_attr_def
	: attr_def
		{{ DBG_TRACE_GRAMMAR(view_attr_def, : attr_def);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| identifier opt_comment_spec
		{{ DBG_TRACE_GRAMMAR(view_attr_def, | identifier opt_comment_spec);

			PT_NODE *node = parser_new_node (this_parser, PT_ATTR_DEF);

			if (node)
			  {
			    node->data_type = NULL;
			    node->info.attr_def.attr_name = $1;
			    node->info.attr_def.comment = $2;
			    node->info.attr_def.attr_type = PT_NORMAL;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

attr_def_list_with_commas
	: attr_def_list_with_commas ','  attr_def
		{{ DBG_TRACE_GRAMMAR(attr_def_list_with_commas, : attr_def_list_with_commas ','  attr_def);

			$$ = parser_make_link ($1, $3);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| attr_def ','  attr_def
		{{ DBG_TRACE_GRAMMAR(attr_def_list_with_commas, | attr_def ','  attr_def);

			$$ = parser_make_link ($1, $3);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

attr_def_list
	: attr_def_list ','  attr_def
		{{ DBG_TRACE_GRAMMAR(attr_def_list, : attr_def_list ','  attr_def);

			$$ = parser_make_link ($1, $3);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| attr_def
		{{ DBG_TRACE_GRAMMAR(attr_def_list, | attr_def);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

attr_def
	: attr_constraint_def
		{{ DBG_TRACE_GRAMMAR(attr_def, : attr_constraint_def);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| attr_index_def
		{{ DBG_TRACE_GRAMMAR(attr_def, | attr_index_def);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| attr_def_one
		{{ DBG_TRACE_GRAMMAR(attr_def, | attr_def_one);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

attr_constraint_def
	: opt_constraint_opt_id
	  of_unique_foreign_check
	  opt_constraint_attr_list
	  opt_comment_spec
		{{ DBG_TRACE_GRAMMAR(attr_constraint_def, : opt_constraint_opt_id of_unique_foreign_check opt_constraint_attr_list opt_comment_spec);

			PT_NODE *name = $1;
			PT_NODE *constraint = $2;

			if (constraint)
			  {
			    if (constraint->node_type == PT_CONSTRAINT)
			      {
				/* If both the constraint name and the index name are
				   given we ignore the constraint name because that is
				   what MySQL does for UNIQUE constraints. */
				if (constraint->info.constraint.name == NULL)
				  {
				    constraint->info.constraint.name = name;
				  }
				constraint->info.constraint.comment = $4;
				if (TO_NUMBER (CONTAINER_AT_0 ($3)))
				  {
				    constraint->info.constraint.deferrable = (short)TO_NUMBER (CONTAINER_AT_1 ($3));
				  }
				if (TO_NUMBER (CONTAINER_AT_2 ($3)))
				  {
				    constraint->info.constraint.initially_deferred =
				      (short)TO_NUMBER (CONTAINER_AT_3 ($3));
				  }
			      }
			    else
			      {
				/* UNIQUE - constraint->node_type = PT_CREATE_INDEX */
				if (TO_NUMBER (CONTAINER_AT_0 ($3)) || TO_NUMBER (CONTAINER_AT_2 ($3)))
				  {
				    PT_ERRORm (this_parser, constraint, MSGCAT_SET_PARSER_SYNTAX,
					       MSGCAT_SYNTAX_INVALID_CREATE_INDEX);
				  }
				else
				  {
				    constraint->info.index.comment = $4;
				    if (constraint->info.index.index_name == NULL)
				      {
					constraint->info.index.index_name = name;
					if (name == NULL)
					  {
					    PT_ERRORm (this_parser, constraint,
						       MSGCAT_SET_PARSER_SYNTAX,
					               MSGCAT_SYNTAX_INVALID_CREATE_INDEX);
					  }
				      }
				  }
			      }
			  }

			$$ = constraint;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

attr_index_def
	: index_or_key              /* 1 */
	  identifier                /* 2 */
	  index_column_name_list    /* 3 */
	  opt_where_clause          /* 4 */
	  opt_comment_spec          /* 5 */
	  opt_invisible             /* 6 */
		{{ DBG_TRACE_GRAMMAR(attr_index_def, : index_or_key identifier index_column_name_list opt_where_clause opt_comment_spec opt_invisible);
			int arg_count = 0, prefix_col_count = 0;
			PT_NODE* node = parser_new_node(this_parser,
							PT_CREATE_INDEX);
			PT_NODE* col = $3;

			PARSER_SAVE_ERR_CONTEXT (node, @$.buffer_pos)
			node->info.index.index_name = $2;
			if (node->info.index.index_name)
			  {
			    node->info.index.index_name->info.name.meta_class = PT_INDEX_NAME;
			  }
			node->info.index.indexed_class = NULL;
			node->info.index.where = $4;
			node->info.index.comment = $5;
			node->info.index.index_status = SM_NORMAL_INDEX;

			prefix_col_count =
				parser_count_prefix_columns (col, &arg_count);

			if (prefix_col_count > 1 ||
			    (prefix_col_count == 1 && arg_count > 1))
			  {
			    PT_ERRORm (this_parser, node,
				       MSGCAT_SET_PARSER_SEMANTIC,
				       MSGCAT_SEMANTIC_MULTICOL_PREFIX_INDX_NOT_ALLOWED);
			  }
			else
			  {
			    if (arg_count == 1 && (prefix_col_count == 1
			        || col->info.sort_spec.expr->node_type == PT_FUNCTION))
			      {
				PT_NODE *expr = col->info.sort_spec.expr;
				PT_NODE *arg_list = expr->info.function.arg_list;
				if ((arg_list != NULL)
				    && (arg_list->next == NULL)
				    && (arg_list->node_type == PT_VALUE))
				  {
				    PT_NODE *p = parser_new_node (this_parser, PT_NAME);
				    if (p)
				      {
					p->info.name.original = expr->info.function.generic_name;
				      }
				    node->info.index.prefix_length =
				    expr->info.function.arg_list;
				    col->info.sort_spec.expr = p;
				  }
				else
				  {
				    PT_ERRORm (this_parser, node,
					       MSGCAT_SET_PARSER_SYNTAX,
					       MSGCAT_SYNTAX_INVALID_CREATE_INDEX);
				  }
			      }
			  }
			node->info.index.column_names = col;
			node->info.index.index_status = SM_NORMAL_INDEX;
			if ($6)
				{
					node->info.index.index_status = SM_INVISIBLE_INDEX;
				}
			$$ = node;

		DBG_PRINT}}
	;

attr_def_one
	: identifier
	  data_type
		{{ DBG_TRACE_GRAMMAR(attr_def_one, : identifier data_type);

			PT_NODE *dt;
			PT_TYPE_ENUM typ;
			PT_NODE *node = parser_new_node (this_parser, PT_ATTR_DEF);

			if (node)
			  {
			    node->type_enum = typ = TO_NUMBER (CONTAINER_AT_0 ($2));
			    node->data_type = dt = CONTAINER_AT_1 ($2);
			    node->info.attr_def.attr_name = $1;
			    if (typ == PT_TYPE_CHAR && dt)
			      node->info.attr_def.size_constraint = dt->info.data_type.precision;
			    if (typ == PT_TYPE_OBJECT && dt && dt->type_enum == PT_TYPE_VARCHAR)
			      {
				node->type_enum = dt->type_enum;
				PT_NAME_INFO_SET_FLAG (node->info.attr_def.attr_name,
						       PT_NAME_INFO_EXTERNAL);
			      }
			  }

			parser_save_attr_def_one (node);

		DBG_PRINT}}
	  opt_constraint_list_and_opt_column_comment
	  opt_attr_ordering_info
		{{ DBG_TRACE_GRAMMAR(attr_def_one, opt_constraint_list_and_opt_column_comment opt_attr_ordering_info);

			PT_NODE *node = parser_get_attr_def_one ();
			if (node != NULL && node->info.attr_def.attr_type != PT_SHARED)
			  {
			    node->info.attr_def.attr_type = parser_attr_type;
			  }
			if (node != NULL)
			  {
			    node->info.attr_def.ordering_info = $5;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

opt_attr_ordering_info
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_attr_ordering_info, : );

			$$ = NULL;

		DBG_PRINT}}
	| FIRST
		{{ DBG_TRACE_GRAMMAR(opt_attr_ordering_info, | FIRST);

			PT_NODE *ord = parser_new_node (this_parser, PT_ATTR_ORDERING);
			PARSER_SAVE_ERR_CONTEXT (ord, @$.buffer_pos)
			if (ord)
			  {
			    ord->info.attr_ordering.first = true;
			    if (!allow_attribute_ordering)
			      {
				PT_ERRORmf(this_parser, ord,
					   MSGCAT_SET_PARSER_SEMANTIC,
					   MSGCAT_SEMANTIC_NOT_ALLOWED_HERE, "FIRST");
			      }
			  }

			$$ = ord;

		DBG_PRINT}}
	| AFTER identifier
		{{ DBG_TRACE_GRAMMAR(opt_attr_ordering_info, | AFTER identifier);

			PT_NODE *ord = parser_new_node (this_parser, PT_ATTR_ORDERING);
			PARSER_SAVE_ERR_CONTEXT (ord, @$.buffer_pos)
			if (ord)
			  {
			    ord->info.attr_ordering.after = $2;
			    if (!allow_attribute_ordering)
			      {
				PT_ERRORmf(this_parser, ord,
					   MSGCAT_SET_PARSER_SEMANTIC,
					   MSGCAT_SEMANTIC_NOT_ALLOWED_HERE, "AFTER column");
			      }
			  }

			$$ = ord;

		DBG_PRINT}}
	;

opt_constraint_list_and_opt_column_comment
	: /* empty */
		{ DBG_TRACE_GRAMMAR(opt_constraint_list_and_opt_column_comment, : );
                   $$ = 0; }
	| constraint_list_and_column_comment
		{ DBG_TRACE_GRAMMAR(opt_constraint_list_and_opt_column_comment, | constraint_list_and_column_comment);
                   $$ = $1; }
	;

constraint_list_and_column_comment
	: constraint_list_and_column_comment column_constraint_and_comment_def
		{{ DBG_TRACE_GRAMMAR(constraint_list_and_column_comment, : constraint_list_and_column_comment column_constraint_and_comment_def);
			unsigned int mask = $1;
			unsigned int new_bit = $2;
			unsigned int merged = mask | new_bit;

			/* Check the constraints according to the following rules:
			 *   1. A constraint should be specified once.
			 *   2. Only one of SHARED, DEFAULT or AI can be specified.
			 *   3. SHARED constraint cannot be defined with UNIQUE or PK constraint.
			 */
			if (((mask & new_bit) ^ new_bit) == 0)
			  {
			    PT_ERROR (this_parser, pt_top(this_parser),
				      "Multiple definitions exist for a constraint or comment");
			  }
			else if ((new_bit & COLUMN_CONSTRAINT_SHARED_DEFAULT_AI)
				  && ((merged & COLUMN_CONSTRAINT_SHARED_DEFAULT_AI)
				       ^ (new_bit & COLUMN_CONSTRAINT_SHARED_DEFAULT_AI)) != 0)
			  {
			    PT_ERRORm (this_parser, pt_top(this_parser),
				       MSGCAT_SET_PARSER_SEMANTIC,
				       MSGCAT_SEMANTIC_INVALID_AUTO_INCREMENT_ON_DEFAULT_SHARED);
			  }
			else if ((merged & COLUMN_CONSTRAINT_SHARED)
			          && ((merged & COLUMN_CONSTRAINT_UNIQUE)
				       || (merged & COLUMN_CONSTRAINT_PRIMARY_KEY)))
			  {
			    PT_ERROR (this_parser, pt_top(this_parser),
				      "SHARED cannot be defined with PRIMARY KEY or UNIQUE constraint");
			  }

			$$ = merged;
		}}
	| column_constraint_and_comment_def
		{{ DBG_TRACE_GRAMMAR(constraint_list_and_column_comment, | column_constraint_and_comment_def);
			$$ = $1;
		}}
	;

column_constraint_and_comment_def
	: column_unique_constraint_def
		{{ DBG_TRACE_GRAMMAR(column_constraint_and_comment_def, : column_unique_constraint_def);
			$$ = COLUMN_CONSTRAINT_UNIQUE;
		}}
	| column_primary_constraint_def
		{{ DBG_TRACE_GRAMMAR(column_constraint_and_comment_def, | column_primary_constraint_def);
			$$ = COLUMN_CONSTRAINT_PRIMARY_KEY;
		}}
	| column_null_constraint_def
		{{ DBG_TRACE_GRAMMAR(column_constraint_and_comment_def, | column_null_constraint_def);
			$$ = COLUMN_CONSTRAINT_NULL;
		}}
	| column_other_constraint_def
		{{ DBG_TRACE_GRAMMAR(column_constraint_and_comment_def, | column_other_constraint_def);
			$$ = COLUMN_CONSTRAINT_OTHERS;
		}}
	| column_shared_constraint_def
		{{ DBG_TRACE_GRAMMAR(column_constraint_and_comment_def, | column_shared_constraint_def);
			$$ = COLUMN_CONSTRAINT_SHARED;
		}}
	| column_default_constraint_def
		{{ DBG_TRACE_GRAMMAR(column_constraint_and_comment_def, | column_default_constraint_def);
			$$ = COLUMN_CONSTRAINT_DEFAULT;
		}}
	| column_ai_constraint_def
		{{ DBG_TRACE_GRAMMAR(column_constraint_and_comment_def, | column_ai_constraint_def);
			$$ = COLUMN_CONSTRAINT_AUTO_INCREMENT;
		}}
	| column_comment_def
		{{ DBG_TRACE_GRAMMAR(column_constraint_and_comment_def, | column_comment_def);
			$$ = COLUMN_CONSTRAINT_COMMENT;
		}}
	| column_on_update_def
		{{ DBG_TRACE_GRAMMAR(column_constraint_and_comment_def, | column_on_update_def);
			$$ = COLUMN_CONSTRAINT_ON_UPDATE;
		}}
	;

column_unique_constraint_def
	: opt_constraint_id UNIQUE opt_key opt_constraint_attr_list
		{{ DBG_TRACE_GRAMMAR(column_unique_constraint_def, : opt_constraint_id UNIQUE opt_key opt_constraint_attr_list);

			PT_NODE *node = parser_get_attr_def_one ();
			PT_NODE *constrant_name = $1;
			PT_NODE *constraint = parser_new_node (this_parser, PT_CONSTRAINT);

			if (constraint)
			  {
			    constraint->info.constraint.type = PT_CONSTRAIN_UNIQUE;
			    constraint->info.constraint.un.unique.attrs
			      = parser_copy_tree (this_parser, node->info.attr_def.attr_name);

			    if (node->info.attr_def.attr_type == PT_SHARED)
			      constraint->info.constraint.un.unique.attrs->info.name.meta_class
				= PT_SHARED;

			    else
			      constraint->info.constraint.un.unique.attrs->info.name.meta_class
				= parser_attr_type;

			    constraint->info.constraint.name = $1;

			    if (TO_NUMBER (CONTAINER_AT_0 ($4)))
			      {
				constraint->info.constraint.deferrable =
				  (short)TO_NUMBER (CONTAINER_AT_1 ($4));
			      }

			    if (TO_NUMBER (CONTAINER_AT_2 ($4)))
			      {
				constraint->info.constraint.initially_deferred =
				  (short)TO_NUMBER (CONTAINER_AT_3 ($4));
			      }
			  }

			parser_make_link (node, constraint);

		DBG_PRINT}}
	;

column_primary_constraint_def
	: opt_constraint_id PRIMARY KEY opt_constraint_attr_list
		{{ DBG_TRACE_GRAMMAR(column_primary_constraint_def, : opt_constraint_id PRIMARY KEY opt_constraint_attr_list);

			PT_NODE *node = parser_get_attr_def_one ();
			PT_NODE *constrant_name = $1;
			PT_NODE *constraint = parser_new_node (this_parser, PT_CONSTRAINT);

			if (constraint)
			  {
			    constraint->info.constraint.type = PT_CONSTRAIN_PRIMARY_KEY;
			    constraint->info.constraint.un.unique.attrs
			      = parser_copy_tree (this_parser, node->info.attr_def.attr_name);

			    constraint->info.constraint.name = $1;

			    if (TO_NUMBER (CONTAINER_AT_0 ($4)))
			      {
				constraint->info.constraint.deferrable =
				  (short)TO_NUMBER (CONTAINER_AT_1 ($4));
			      }

			    if (TO_NUMBER (CONTAINER_AT_2 ($4)))
			      {
				constraint->info.constraint.initially_deferred =
				  (short)TO_NUMBER (CONTAINER_AT_3 ($4));
			      }
			  }

			parser_make_link (node, constraint);

		DBG_PRINT}}
	;

column_null_constraint_def
	: opt_constraint_id Null opt_constraint_attr_list
		{{ DBG_TRACE_GRAMMAR(column_null_constraint_def, : opt_constraint_id Null opt_constraint_attr_list);

			PT_NODE *node = parser_get_attr_def_one ();
			PT_NODE *constrant_name = $1;
			PT_NODE *constraint = parser_new_node (this_parser, PT_CONSTRAINT);

						/* to support null in ODBC-DDL, ignore it */
			if (constraint)
			  {
			    constraint->info.constraint.type = PT_CONSTRAIN_NULL;
			    constraint->info.constraint.name = $1;

			    if (TO_NUMBER (CONTAINER_AT_0 ($3)))
			      {
				constraint->info.constraint.deferrable =
				  (short)TO_NUMBER (CONTAINER_AT_1 ($3));
			      }

			    if (TO_NUMBER (CONTAINER_AT_2 ($3)))
			      {
				constraint->info.constraint.initially_deferred =
				  (short)TO_NUMBER (CONTAINER_AT_3 ($3));
			      }
			  }

			parser_make_link (node, constraint);

		DBG_PRINT}}
	| opt_constraint_id NOT Null opt_constraint_attr_list
		{{ DBG_TRACE_GRAMMAR(column_null_constraint_def, | opt_constraint_id NOT Null opt_constraint_attr_list);

			PT_NODE *node = parser_get_attr_def_one ();
			PT_NODE *constrant_name = $1;
			PT_NODE *constraint = parser_new_node (this_parser, PT_CONSTRAINT);


			if (constraint)
			  {
			    constraint->info.constraint.type = PT_CONSTRAIN_NOT_NULL;
			    constraint->info.constraint.un.not_null.attr
			      = parser_copy_tree (this_parser, node->info.attr_def.attr_name);
			    /*
			     * This should probably be deferred until semantic
			     * analysis time; leave it this way for now.
			     */
			    node->info.attr_def.constrain_not_null = 1;

			    constraint->info.constraint.name = $1;

			    if (TO_NUMBER (CONTAINER_AT_0 ($4)))
			      {
				constraint->info.constraint.deferrable =
				  (short)TO_NUMBER (CONTAINER_AT_1 ($4));
			      }

			    if (TO_NUMBER (CONTAINER_AT_2 ($4)))
			      {
				constraint->info.constraint.initially_deferred =
				  (short)TO_NUMBER (CONTAINER_AT_3 ($4));
			      }
			  }

			parser_make_link (node, constraint);

		DBG_PRINT}}
	;

column_other_constraint_def
	: opt_constraint_id CHECK '(' search_condition ')' opt_constraint_attr_list
		{{ DBG_TRACE_GRAMMAR(column_other_constraint_def, : opt_constraint_id CHECK '(' search_condition ')' opt_constraint_attr_list);

			PT_NODE *node = parser_get_attr_def_one ();
			PT_NODE *constrant_name = $1;
			PT_NODE *constraint = parser_new_node (this_parser, PT_CONSTRAINT);

			if (constraint)
			  {
			    constraint->info.constraint.type = PT_CONSTRAIN_CHECK;
			    constraint->info.constraint.un.check.expr = $4;

			    constraint->info.constraint.name = $1;

			    if (TO_NUMBER (CONTAINER_AT_0 ($6)))
			      {
				constraint->info.constraint.deferrable =
				  (short)TO_NUMBER (CONTAINER_AT_1 ($6));
			      }

			    if (TO_NUMBER (CONTAINER_AT_2 ($6)))
			      {
				constraint->info.constraint.initially_deferred =
				  (short)TO_NUMBER (CONTAINER_AT_3 ($6));
			      }
			  }

			parser_make_link (node, constraint);

		DBG_PRINT}}
	| opt_constraint_id			/* 1 */
	  opt_foreign_key			/* 2 */
	  REFERENCES				/* 3 */
	  class_name				/* 4 */
	  opt_paren_attr_list			/* 5 */
	  opt_ref_rule_list			/* 6 */
	  opt_constraint_attr_list		/* 7 */
		{{ DBG_TRACE_GRAMMAR(column_other_constraint_def, | opt_constraint_id opt_foreign_key REFERENCES class_name ~);

			PT_NODE *node = parser_get_attr_def_one ();
			PT_NODE *constrant_name = $1;
			PT_NODE *constraint = parser_new_node (this_parser, PT_CONSTRAINT);

			if (constraint)
			  {
			    constraint->info.constraint.un.foreign_key.referenced_attrs = $5;
			    constraint->info.constraint.un.foreign_key.match_type = PT_MATCH_REGULAR;
			    constraint->info.constraint.un.foreign_key.delete_action = TO_NUMBER (CONTAINER_AT_0 ($6));	/* delete_action */
			    constraint->info.constraint.un.foreign_key.update_action = TO_NUMBER (CONTAINER_AT_1 ($6));	/* update_action */
			    constraint->info.constraint.un.foreign_key.referenced_class = $4;
			  }

			if (constraint)
			  {
			    constraint->info.constraint.type = PT_CONSTRAIN_FOREIGN_KEY;
			    constraint->info.constraint.un.foreign_key.attrs
			      = parser_copy_tree (this_parser, node->info.attr_def.attr_name);

			    constraint->info.constraint.name = $1;

			    if (TO_NUMBER (CONTAINER_AT_0 ($7)))
			      {
				constraint->info.constraint.deferrable =
				  (short)TO_NUMBER (CONTAINER_AT_1 ($7));
			      }

			    if (TO_NUMBER (CONTAINER_AT_2 ($7)))
			      {
				constraint->info.constraint.initially_deferred =
				  (short)TO_NUMBER (CONTAINER_AT_3 ($7));
			      }
			  }

			parser_make_link (node, constraint);

		DBG_PRINT}}
	;

index_or_key
	: INDEX
	| KEY
	;

opt_of_index_key
	: /* empty */
	| INDEX
	| KEY
	;

opt_key
	: /* empty */
	| KEY
	;

opt_foreign_key
	: /* empty */
	| FOREIGN KEY
	;

column_ai_constraint_def
	: AUTO_INCREMENT '(' integer_text ',' integer_text ')'
		{{ DBG_TRACE_GRAMMAR(column_ai_constraint_def, : AUTO_INCREMENT '(' integer_text ',' integer_text ')');

			PT_NODE *node = parser_get_attr_def_one ();
			PT_NODE *start_val = parser_new_node (this_parser, PT_VALUE);
			PT_NODE *increment_val = parser_new_node (this_parser, PT_VALUE);
			PT_NODE *ai_node;

			if (start_val)
			  {
			    start_val->info.value.data_value.str =
			      pt_append_bytes (this_parser, NULL, $3,
					       strlen ($3));
			    start_val->type_enum = PT_TYPE_NUMERIC;
			    PT_NODE_PRINT_VALUE_TO_TEXT (this_parser,
							 start_val);
			  }

			if (increment_val)
			  {
			    increment_val->info.value.data_value.str =
			      pt_append_bytes (this_parser, NULL, $5,
					       strlen ($5));
			    increment_val->type_enum = PT_TYPE_NUMERIC;
			    PT_NODE_PRINT_VALUE_TO_TEXT (this_parser,
							 increment_val);
			  }

			ai_node = parser_new_node (this_parser, PT_AUTO_INCREMENT);
			ai_node->info.auto_increment.start_val = start_val;
			ai_node->info.auto_increment.increment_val = increment_val;
			node->info.attr_def.auto_increment = ai_node;

			if (parser_attr_type == PT_META_ATTR)
			  {
			    PT_ERRORm (this_parser, node, MSGCAT_SET_PARSER_SEMANTIC,
				       MSGCAT_SEMANTIC_CLASS_ATT_CANT_BE_AUTOINC);
			  }

		DBG_PRINT}}
	| AUTO_INCREMENT
		{{ DBG_TRACE_GRAMMAR(column_ai_constraint_def, | AUTO_INCREMENT);

			PT_NODE *node = parser_get_attr_def_one ();

			PT_NODE *ai_node = parser_new_node (this_parser, PT_AUTO_INCREMENT);
			ai_node->info.auto_increment.start_val = NULL;
			ai_node->info.auto_increment.increment_val = NULL;
			node->info.attr_def.auto_increment = ai_node;

			if (parser_attr_type == PT_META_ATTR)
			  {
			    PT_ERRORm (this_parser, node, MSGCAT_SET_PARSER_SEMANTIC,
				       MSGCAT_SEMANTIC_CLASS_ATT_CANT_BE_AUTOINC);
			  }

		DBG_PRINT}}
	;

column_shared_constraint_def
	: SHARED expression_
		{{ DBG_TRACE_GRAMMAR(column_shared_constraint_def, : SHARED expression_);
			PT_NODE *attr_node;
			PT_NODE *node = parser_new_node (this_parser, PT_DATA_DEFAULT);

			if (node)
			  {
			    node->info.data_default.default_value = $2;
			    node->info.data_default.shared = PT_SHARED;
			    PARSER_SAVE_ERR_CONTEXT (node, @2.buffer_pos)
			  }

			attr_node = parser_get_attr_def_one ();
			attr_node->info.attr_def.data_default = node;
			attr_node->info.attr_def.attr_type = PT_SHARED;

		DBG_PRINT}}
	;

column_on_update_def
	: ON_ UPDATE expression_
		{{ DBG_TRACE_GRAMMAR(column_on_update_def, : ON_ UPDATE expression_);
			DB_DEFAULT_EXPR_TYPE default_expr_type = DB_DEFAULT_NONE;
			PT_NODE *attr_node = parser_get_attr_def_one ();
			PT_NODE *on_update_default_value = $3;
			PARSER_SAVE_ERR_CONTEXT (attr_node, @3.buffer_pos)

			if (on_update_default_value && on_update_default_value->node_type == PT_EXPR)
			  {
			    switch (on_update_default_value->info.expr.op)
			      {
			      case PT_CURRENT_TIMESTAMP:
			        default_expr_type = DB_DEFAULT_CURRENTTIMESTAMP;
			        break;
			      case PT_CURRENT_DATE:
			        default_expr_type = DB_DEFAULT_CURRENTDATE;
			        break;
			      case PT_CURRENT_DATETIME:
			        default_expr_type = DB_DEFAULT_CURRENTDATETIME;
			        break;
			      case PT_SYS_TIMESTAMP:
			        default_expr_type = DB_DEFAULT_SYSTIMESTAMP;
			        break;
			      case PT_UNIX_TIMESTAMP:
			        default_expr_type = DB_DEFAULT_UNIX_TIMESTAMP;
			        break;
			      case PT_SYS_DATE:
			        default_expr_type = DB_DEFAULT_SYSDATE;
			        break;
			      case PT_SYS_DATETIME:
			        default_expr_type = DB_DEFAULT_SYSDATETIME;
			        break;
			      case PT_SYS_TIME:
			        default_expr_type = DB_DEFAULT_SYSTIME;
			        break;
			      default:
			        PT_ERROR (this_parser, attr_node, "invalid expression type");
			        break;
			      }
			  }
			else
			  {
			    PT_ERROR (this_parser, attr_node, "on update must be an expression");
			  }

			attr_node->info.attr_def.on_update = default_expr_type;

		DBG_PRINT}}
	;

column_default_constraint_def
	: DEFAULT expression_
		{{ DBG_TRACE_GRAMMAR(column_default_constraint_def, : DEFAULT expression_);

			PT_NODE *attr_node;
			PT_NODE *node = parser_new_node (this_parser, PT_DATA_DEFAULT);

			if (node)
			  {
			    PT_NODE *def;
			    node->info.data_default.default_value = $2;
			    node->info.data_default.shared = PT_DEFAULT;
			    PARSER_SAVE_ERR_CONTEXT (node, @2.buffer_pos)

			    def = node->info.data_default.default_value;
			    if (def && def->node_type == PT_EXPR)
			      {
					if (def->info.expr.op == PT_TO_CHAR)
					  {
						if (def->info.expr.arg3)
						  {
							bool has_user_lang = false;
							bool dummy;

							assert (def->info.expr.arg3->node_type == PT_VALUE);
							(void) lang_get_lang_id_from_flag (def->info.expr.arg3->info.value.data_value.i, &dummy, &has_user_lang);
							 if (has_user_lang)
							   {
								 PT_ERROR (this_parser, def->info.expr.arg3, "do not allow lang format in default to_char");
							   }
							}

						if (def->info.expr.arg1  && def->info.expr.arg1->node_type == PT_EXPR)
						  {
							def = def->info.expr.arg1;
						  }
					  }

				switch (def->info.expr.op)
				  {
				  case PT_SYS_TIME:
				    node->info.data_default.default_expr_type = DB_DEFAULT_SYSTIME;
				    break;
				  case PT_SYS_DATE:
				    node->info.data_default.default_expr_type = DB_DEFAULT_SYSDATE;
				    break;
				  case PT_SYS_DATETIME:
				    node->info.data_default.default_expr_type = DB_DEFAULT_SYSDATETIME;
				    break;
				  case PT_SYS_TIMESTAMP:
				    node->info.data_default.default_expr_type = DB_DEFAULT_SYSTIMESTAMP;
				    break;
				  case PT_CURRENT_TIME:
				    node->info.data_default.default_expr_type = DB_DEFAULT_CURRENTTIME;
				    break;
				  case PT_CURRENT_DATE:
				    node->info.data_default.default_expr_type = DB_DEFAULT_CURRENTDATE;
				    break;
				  case PT_CURRENT_DATETIME:
				    node->info.data_default.default_expr_type = DB_DEFAULT_CURRENTDATETIME;
				    break;
				  case PT_CURRENT_TIMESTAMP:
				    node->info.data_default.default_expr_type = DB_DEFAULT_CURRENTTIMESTAMP;
				    break;
				  case PT_USER:
				    node->info.data_default.default_expr_type = DB_DEFAULT_USER;
				    break;
				  case PT_CURRENT_USER:
				    node->info.data_default.default_expr_type = DB_DEFAULT_CURR_USER;
				    break;
				  case PT_UNIX_TIMESTAMP:
				    node->info.data_default.default_expr_type = DB_DEFAULT_UNIX_TIMESTAMP;
				    break;
				  default:
				    node->info.data_default.default_expr_type = DB_DEFAULT_NONE;
				    break;
				  }
			      }
			    else
			      {
				node->info.data_default.default_expr_type = DB_DEFAULT_NONE;
			      }
			  }

			attr_node = parser_get_attr_def_one ();
			attr_node->info.attr_def.data_default = node;

		DBG_PRINT}}
	;

attr_def_comment_list
	: attr_def_comment_list ',' attr_def_comment
		{{ DBG_TRACE_GRAMMAR(attr_def_comment_list, : attr_def_comment_list ',' attr_def_comment);

			$$ = parser_make_link ($1, $3);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos);

		DBG_PRINT}}
	| attr_def_comment
		{{ DBG_TRACE_GRAMMAR(attr_def_comment_list, | attr_def_comment);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos);

		DBG_PRINT}}
	;

attr_def_comment
	: identifier opt_equalsign comment_value
		{{ DBG_TRACE_GRAMMAR(attr_def_comment, : identifier opt_equalsign comment_value);

			PT_NODE *attr_node = parser_new_node (this_parser, PT_ATTR_DEF);

			if (attr_node)
			  {
				attr_node->info.attr_def.attr_name = $1;
				attr_node->info.attr_def.comment = $3;
				attr_node->info.attr_def.attr_type = parser_attr_type;
			  }

			$$ = attr_node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos);

		DBG_PRINT}}
	;

column_comment_def
	: COMMENT comment_value
		{{ DBG_TRACE_GRAMMAR(column_comment_def, : COMMENT comment_value);

			PT_NODE *attr_node;
			attr_node = parser_get_attr_def_one ();
			attr_node->info.attr_def.comment = $2;

		DBG_PRINT}}
	;

transaction_mode_list
	: transaction_mode_list ',' transaction_mode			%dprec 1
		{{ DBG_TRACE_GRAMMAR(transaction_mode_list, : transaction_mode_list ',' transaction_mode);

			$$ = parser_make_link ($1, $3);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| transaction_mode						%dprec 2
		{{ DBG_TRACE_GRAMMAR(transaction_mode_list, | transaction_mode);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

transaction_mode
	: ISOLATION LEVEL isolation_level_spec ',' isolation_level_spec		%dprec 1
		{{ DBG_TRACE_GRAMMAR(transaction_mode, : ISOLATION LEVEL isolation_level_spec ',' isolation_level_spec);

			PT_NODE *tm = parser_new_node (this_parser, PT_ISOLATION_LVL);
			PT_NODE *is = parser_new_node (this_parser, PT_ISOLATION_LVL);
			int async_ws_or_error;

			if (tm && is)
			  {
			    PARSER_SAVE_ERR_CONTEXT (tm, @$.buffer_pos)
			    async_ws_or_error = (int) TO_NUMBER (CONTAINER_AT_3 ($3));
			    if (async_ws_or_error < 0)
			      {
				PT_ERRORm(this_parser, tm, MSGCAT_SET_PARSER_SYNTAX,
					  MSGCAT_SYNTAX_READ_UNCOMMIT);
				async_ws_or_error = 0;
			      }
			    tm->info.isolation_lvl.level = CONTAINER_AT_0 ($3);
			    tm->info.isolation_lvl.schema = TO_NUMBER (CONTAINER_AT_1 ($3));
			    tm->info.isolation_lvl.instances = TO_NUMBER (CONTAINER_AT_2 ($3));
			    tm->info.isolation_lvl.async_ws = async_ws_or_error;


			    async_ws_or_error = (int) TO_NUMBER (CONTAINER_AT_3 ($5));
			    if (async_ws_or_error < 0)
			      {
				PT_ERRORm(this_parser, is, MSGCAT_SET_PARSER_SYNTAX,
					  MSGCAT_SYNTAX_READ_UNCOMMIT);
				async_ws_or_error = 0;
			      }
			    is->info.isolation_lvl.level = CONTAINER_AT_0 ($5);
			    is->info.isolation_lvl.schema = TO_NUMBER (CONTAINER_AT_1 ($5));
			    is->info.isolation_lvl.instances = TO_NUMBER (CONTAINER_AT_2 ($5));
			    is->info.isolation_lvl.async_ws = async_ws_or_error;

			    if (tm->info.isolation_lvl.async_ws)
			      {
				if (is->info.isolation_lvl.async_ws)
				  {
				    /* async_ws, async_ws */
				  }
				else
				  {
				    /* async_ws, iso_lvl */
				    tm->info.isolation_lvl.schema = is->info.isolation_lvl.schema;
				    tm->info.isolation_lvl.instances =
				      is->info.isolation_lvl.instances;
				    tm->info.isolation_lvl.level = is->info.isolation_lvl.level;
				  }
			      }
			    else
			      {
				if (is->info.isolation_lvl.async_ws)
				  {
				    /* iso_lvl, async_ws */
				    tm->info.isolation_lvl.async_ws = 1;
				  }
				else
				  {
				    /* iso_lvl, iso_lvl */
				    if (tm->info.isolation_lvl.level != NULL
					|| is->info.isolation_lvl.level != NULL)
				      PT_ERRORm (this_parser, tm, MSGCAT_SET_PARSER_SEMANTIC,
						 MSGCAT_SEMANTIC_GT_1_ISOLATION_LVL);
				    else if (tm->info.isolation_lvl.schema !=
					     is->info.isolation_lvl.schema
					     || tm->info.isolation_lvl.instances !=
					     is->info.isolation_lvl.instances)
				      PT_ERRORm (this_parser, tm, MSGCAT_SET_PARSER_SEMANTIC,
						 MSGCAT_SEMANTIC_GT_1_ISOLATION_LVL);
				  }
			      }

			    is->info.isolation_lvl.level = NULL;
			    parser_free_node (this_parser, is);
			  }

			$$ = tm;

		DBG_PRINT}}
	| ISOLATION LEVEL isolation_level_spec			%dprec 2
		{{ DBG_TRACE_GRAMMAR(transaction_mode, | ISOLATION LEVEL isolation_level_spec);

			PT_NODE *tm = parser_new_node (this_parser, PT_ISOLATION_LVL);
			int async_ws_or_error = (int) TO_NUMBER (CONTAINER_AT_3 ($3));

			PARSER_SAVE_ERR_CONTEXT (tm, @$.buffer_pos)

			if (async_ws_or_error < 0)
			  {
			    PT_ERRORm(this_parser, tm, MSGCAT_SET_PARSER_SYNTAX,
				      MSGCAT_SYNTAX_READ_UNCOMMIT);
			    async_ws_or_error = 0;
			  }

			if (tm)
			  {
			    tm->info.isolation_lvl.level = CONTAINER_AT_0 ($3);
			    tm->info.isolation_lvl.schema = TO_NUMBER (CONTAINER_AT_1 ($3));
			    tm->info.isolation_lvl.instances = TO_NUMBER (CONTAINER_AT_2 ($3));
			    tm->info.isolation_lvl.async_ws = async_ws_or_error;
			  }

			$$ = tm;

		DBG_PRINT}}
	| LOCK_ TIMEOUT timeout_spec
		{{ DBG_TRACE_GRAMMAR(transaction_mode, | LOCK_ TIMEOUT timeout_spec);

			PT_NODE *tm = parser_new_node (this_parser, PT_TIMEOUT);

			if (tm)
			  {
			    tm->info.timeout.val = $3;
			  }

			$$ = tm;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;


/* container order : level, schema, instances, async_ws */
isolation_level_spec
	: expression_
		{{ DBG_TRACE_GRAMMAR(isolation_level_spec, : expression_);

			container_4 ctn;
			SET_CONTAINER_4 (ctn, $1, FROM_NUMBER (PT_NO_ISOLATION_LEVEL),
					 FROM_NUMBER (PT_NO_ISOLATION_LEVEL), FROM_NUMBER (0));
			$$ = ctn;

		DBG_PRINT}}
	| ASYNC WORKSPACE
		{{ DBG_TRACE_GRAMMAR(isolation_level_spec, | ASYNC WORKSPACE);

			container_4 ctn;
			SET_CONTAINER_4 (ctn, NULL, FROM_NUMBER (PT_NO_ISOLATION_LEVEL),
					 FROM_NUMBER (PT_NO_ISOLATION_LEVEL), FROM_NUMBER (1));
			$$ = ctn;

		DBG_PRINT}}
	| SERIALIZABLE
		{{ DBG_TRACE_GRAMMAR(isolation_level_spec, | SERIALIZABLE);

			container_4 ctn;
			SET_CONTAINER_4 (ctn, NULL, FROM_NUMBER (PT_SERIALIZABLE),
					 FROM_NUMBER (PT_SERIALIZABLE), FROM_NUMBER (0));
			$$ = ctn;

		DBG_PRINT}}
	| CURSOR STABILITY
		{{ DBG_TRACE_GRAMMAR(isolation_level_spec, | CURSOR STABILITY);

			container_4 ctn;
			SET_CONTAINER_4 (ctn, NULL, FROM_NUMBER (PT_NO_ISOLATION_LEVEL),
					 FROM_NUMBER (PT_READ_COMMITTED), FROM_NUMBER (0));
			$$ = ctn;

		DBG_PRINT}}
	| isolation_level_name								%dprec 1
		{{ DBG_TRACE_GRAMMAR(isolation_level_spec, | isolation_level_name);

			container_4 ctn;
			PT_MISC_TYPE schema = PT_REPEATABLE_READ;
			PT_MISC_TYPE level = 0;
			level = $1;

			SET_CONTAINER_4 (ctn, NULL, FROM_NUMBER (schema),
					 FROM_NUMBER (level), FROM_NUMBER (0));
			$$ = ctn;

		DBG_PRINT}}
	| isolation_level_name of_schema_class 						%dprec 1
		{{ DBG_TRACE_GRAMMAR(isolation_level_spec, | isolation_level_name of_schema_class);

			container_4 ctn;
			PT_MISC_TYPE schema = 0;
			PT_MISC_TYPE level = 0;
			int error = 0;

			schema = $1;

			level = PT_NO_ISOLATION_LEVEL;

			SET_CONTAINER_4 (ctn, NULL, FROM_NUMBER (schema), FROM_NUMBER (level),
					 FROM_NUMBER (error));
			$$ = ctn;

		DBG_PRINT}}
	| isolation_level_name INSTANCES						%dprec 1
		{{ DBG_TRACE_GRAMMAR(isolation_level_spec, | isolation_level_name INSTANCES);

			container_4 ctn;
			PT_MISC_TYPE schema = 0;
			PT_MISC_TYPE level = 0;

			schema = PT_NO_ISOLATION_LEVEL;
			level = $1;

			SET_CONTAINER_4 (ctn, NULL, FROM_NUMBER (schema), FROM_NUMBER (level),
					 FROM_NUMBER (0));
			$$ = ctn;

		DBG_PRINT}}
	| isolation_level_name of_schema_class ',' isolation_level_name INSTANCES	%dprec 10
		{{ DBG_TRACE_GRAMMAR(isolation_level_spec, | isolation_level_name of_schema_class ',' isolation_level_name INSTANCES);

			container_4 ctn;
			PT_MISC_TYPE schema = 0;
			PT_MISC_TYPE level = 0;
			int error = 0;

			level = $4;
			schema = $1;

			SET_CONTAINER_4 (ctn, NULL, FROM_NUMBER (schema), FROM_NUMBER (level),
					 FROM_NUMBER (error));
			$$ = ctn;

		DBG_PRINT}}
	| isolation_level_name INSTANCES ',' isolation_level_name of_schema_class	%dprec 10
		{{ DBG_TRACE_GRAMMAR(isolation_level_spec, | isolation_level_name INSTANCES ',' isolation_level_name of_schema_class);
			container_4 ctn;
			PT_MISC_TYPE schema = 0;
			PT_MISC_TYPE level = 0;
			int error = 0;

			level = $1;
			schema = $4;

			SET_CONTAINER_4 (ctn, NULL, FROM_NUMBER (schema), FROM_NUMBER (level),
					 FROM_NUMBER (error));
			$$ = ctn;

		DBG_PRINT}}
	;

of_schema_class
	: SCHEMA
	| CLASS
	;

isolation_level_name
	: REPEATABLE READ
		{{ DBG_TRACE_GRAMMAR(isolation_level_name, : REPEATABLE READ); 

			$$ = PT_REPEATABLE_READ;

		DBG_PRINT}}
	| READ COMMITTED
		{{ DBG_TRACE_GRAMMAR(isolation_level_name, | READ COMMITTED); 

			$$ = PT_READ_COMMITTED;

		DBG_PRINT}}
	;

timeout_spec
	: INFINITE_
		{{ DBG_TRACE_GRAMMAR(timeout_spec, : INFINITE_);

			PT_NODE *val = parser_new_node (this_parser, PT_VALUE);

			if (val)
			  {
			    val->type_enum = PT_TYPE_INTEGER;
			    val->info.value.data_value.i = -1;
			  }

			$$ = val;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| OFF_
		{{ DBG_TRACE_GRAMMAR(timeout_spec, | OFF_);

			PT_NODE *val = parser_new_node (this_parser, PT_VALUE);

			if (val)
			  {
			    val->type_enum = PT_TYPE_INTEGER;
			    val->info.value.data_value.i = 0;
			  }

			$$ = val;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| unsigned_integer
		{{ DBG_TRACE_GRAMMAR(timeout_spec, | unsigned_integer);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| unsigned_real
		{{ DBG_TRACE_GRAMMAR(timeout_spec, | unsigned_real);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| param_
		{{ DBG_TRACE_GRAMMAR(timeout_spec, | param_);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| host_param_input
		{{ DBG_TRACE_GRAMMAR(timeout_spec, | host_param_input);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;


transaction_stmt
	: COMMIT opt_work RETAIN LOCK_
		{{ DBG_TRACE_GRAMMAR(transaction_stmt, : COMMIT opt_work RETAIN LOCK_);

			PT_NODE *comm = parser_new_node (this_parser, PT_COMMIT_WORK);

			if (comm)
			  {
			    comm->info.commit_work.retain_lock = 1;
			  }

			$$ = comm;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| COMMIT opt_work
		{{ DBG_TRACE_GRAMMAR(timeout_spec, | COMMIT opt_work);

			PT_NODE *comm = parser_new_node (this_parser, PT_COMMIT_WORK);
			$$ = comm;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| ROLLBACK opt_work TO opt_savepoint expression_
		{{ DBG_TRACE_GRAMMAR(timeout_spec, | ROLLBACK opt_work TO opt_savepoint expression_);

			PT_NODE *roll = parser_new_node (this_parser, PT_ROLLBACK_WORK);

			if (roll)
			  {
			    roll->info.rollback_work.save_name = $5;
			  }

			$$ = roll;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| ROLLBACK opt_work
		{{ DBG_TRACE_GRAMMAR(timeout_spec, | ROLLBACK opt_work);

			PT_NODE *roll = parser_new_node (this_parser, PT_ROLLBACK_WORK);
			$$ = roll;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| SAVEPOINT expression_
		{{ DBG_TRACE_GRAMMAR(timeout_spec, | expression_);

			PT_NODE *svpt = parser_new_node (this_parser, PT_SAVEPOINT);

			if (svpt)
			  {
			    svpt->info.savepoint.save_name = $2;
			  }

			$$ = svpt;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;


opt_savepoint
	: /* empty */
	| SAVEPOINT
	;

opt_work
	: /* empty */
	| WORK
	;

opt_to
	: /* empty */
	| TO
	;

evaluate_stmt
	: EVALUATE expression_ into_clause_opt
		{{ DBG_TRACE_GRAMMAR(evaluate_stmt, : EVALUATE expression_ into_clause_opt);

			PT_NODE *node = parser_new_node (this_parser, PT_EVALUATE);

			if (node)
			  {
			    node->info.evaluate.expression = $2;
			    node->info.evaluate.into_var = $3;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

prepare_stmt
	: PREPARE identifier FROM char_string
		{{ DBG_TRACE_GRAMMAR(prepare_stmt, : PREPARE identifier FROM char_string);

			PT_NODE *node = parser_new_node (this_parser, PT_PREPARE_STATEMENT);

			if (node)
			  {
			    node->info.prepare.name = $2;
			    node->info.prepare.statement = $4;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

execute_stmt
	: EXECUTE identifier opt_using
		{{ DBG_TRACE_GRAMMAR(execute_stmt, : EXECUTE identifier opt_using);

			PT_NODE *node = parser_new_node (this_parser, PT_EXECUTE_PREPARE);

			if (node)
			  {
			    node->info.execute.name = $2;
			    node->info.execute.using_list = $3;
			    node->info.execute.into_list = NULL;
			    node->info.execute.query = NULL;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

opt_using
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_using, : );


			$$ = NULL;

		DBG_PRINT}}
	| USING
		{ parser_save_and_set_hvar (0); }
		execute_using_list
		{ parser_restore_hvar (); }
		{{ DBG_TRACE_GRAMMAR(opt_using, | USING execute_using_list);

			$$ = $3;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

opt_status
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_status, : );

			$$ = PT_MISC_DUMMY;

		DBG_PRINT}}
	| trigger_status
		{{ DBG_TRACE_GRAMMAR(opt_status, | trigger_status);

			$$ = $1;

		DBG_PRINT}}
	;

trigger_status
	: STATUS ACTIVE
		{{ DBG_TRACE_GRAMMAR(trigger_status, : STATUS ACTIVE);

			$$ = PT_ACTIVE;

		DBG_PRINT}}
	| STATUS INACTIVE
		{{ DBG_TRACE_GRAMMAR(trigger_status, | STATUS INACTIVE);

			$$ = PT_INACTIVE;

		DBG_PRINT}}
	;

opt_priority
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_priority, : );

			$$ = NULL;

		DBG_PRINT}}
	| trigger_priority
		{{ DBG_TRACE_GRAMMAR(opt_priority, | trigger_priority);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

trigger_priority
	: PRIORITY unsigned_real
		{{ DBG_TRACE_GRAMMAR(trigger_priority, : PRIORITY unsigned_real);

			$$ = $2;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

opt_if_trigger_condition
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_if_trigger_condition, : );

			$$ = NULL;

		DBG_PRINT}}
	| IF trigger_condition
		{{ DBG_TRACE_GRAMMAR(opt_if_trigger_condition, |   IF trigger_condition);

			$$ = $2;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

trigger_time
	: BEFORE
		{{ DBG_TRACE_GRAMMAR(trigger_time, : BEFORE);

			$$ = PT_BEFORE;

		DBG_PRINT}}
	| AFTER
		{{ DBG_TRACE_GRAMMAR(trigger_time, | AFTER);

			$$ = PT_AFTER;

		DBG_PRINT}}
	| DEFERRED
		{{ DBG_TRACE_GRAMMAR(trigger_time, | DEFERRED);

			$$ = PT_DEFERRED;

		DBG_PRINT}}
	;

opt_trigger_action_time
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_trigger_action_time, : );

			$$ = PT_MISC_DUMMY;

		DBG_PRINT}}
	| AFTER
		{{ DBG_TRACE_GRAMMAR(opt_trigger_action_time, | AFTER);

			$$ = PT_AFTER;

		DBG_PRINT}}
	| DEFERRED
		{{ DBG_TRACE_GRAMMAR(opt_trigger_action_time, | DEFERRED);

			$$ = PT_DEFERRED;

		DBG_PRINT}}
	;

event_spec
	: event_type
		{{ DBG_TRACE_GRAMMAR(event_spec, : event_type);

			PT_NODE *node = parser_new_node (this_parser, PT_EVENT_SPEC);

			if (node)
			  {
			    node->info.event_spec.event_type = $1;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| event_type event_target
		{{ DBG_TRACE_GRAMMAR(opt_trigger_aevent_specction_time, | event_type event_target);

			PT_NODE *node = parser_new_node (this_parser, PT_EVENT_SPEC);

			if (node)
			  {
			    node->info.event_spec.event_type = $1;
			    node->info.event_spec.event_target = $2;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

event_type
	: INSERT
		{{ DBG_TRACE_GRAMMAR(event_type, : INSERT);

			$$ = PT_EV_INSERT;

		DBG_PRINT}}
	| STATEMENT INSERT
		{{ DBG_TRACE_GRAMMAR(event_type, | STATEMENT INSERT);

			$$ = PT_EV_STMT_INSERT;

		DBG_PRINT}}
	| DELETE_
		{{ DBG_TRACE_GRAMMAR(event_type, | DELETE_);

			$$ = PT_EV_DELETE;

		DBG_PRINT}}
	| STATEMENT DELETE_
		{{ DBG_TRACE_GRAMMAR(event_type, | STATEMENT DELETE_);

			$$ = PT_EV_STMT_DELETE;

		DBG_PRINT}}
	| UPDATE
		{{ DBG_TRACE_GRAMMAR(event_type, | UPDATE);

			$$ = PT_EV_UPDATE;

		DBG_PRINT}}
	| STATEMENT UPDATE
		{{ DBG_TRACE_GRAMMAR(event_type, | STATEMENT UPDATE);

			$$ = PT_EV_STMT_UPDATE;

		DBG_PRINT}}
	| COMMIT
		{{ DBG_TRACE_GRAMMAR(event_type, | COMMIT);

			$$ = PT_EV_COMMIT;

		DBG_PRINT}}
	| ROLLBACK
		{{ DBG_TRACE_GRAMMAR(event_type, | ROLLBACK);

			$$ = PT_EV_ROLLBACK;

		DBG_PRINT}}
	;

event_target
	: ON_ class_name '(' identifier ')'
		{{ DBG_TRACE_GRAMMAR(event_target, : ON_ class_name '(' identifier ')');

			PT_NODE *node = parser_new_node (this_parser, PT_EVENT_TARGET);

			if (node)
			  {
			    node->info.event_target.class_name = $2;
			    node->info.event_target.attribute = $4;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| ON_ class_name
		{{ DBG_TRACE_GRAMMAR(event_target, | ON_ class_name);

			PT_NODE *node = parser_new_node (this_parser, PT_EVENT_TARGET);

			if (node)
			  {
			    node->info.event_target.class_name = $2;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

trigger_condition
	: search_condition
		{{ DBG_TRACE_GRAMMAR(trigger_condition, : search_condition);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| call_stmt
		{{ DBG_TRACE_GRAMMAR(trigger_condition, | call_stmt);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

trigger_action
	: REJECT_
		{{ DBG_TRACE_GRAMMAR(trigger_action, : REJECT_);

			PT_NODE *node = parser_new_node (this_parser, PT_TRIGGER_ACTION);

			if (node)
			  {
			    node->info.trigger_action.action_type = PT_REJECT;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| INVALIDATE TRANSACTION
		{{ DBG_TRACE_GRAMMAR(trigger_action, | INVALIDATE TRANSACTION);

			PT_NODE *node = parser_new_node (this_parser, PT_TRIGGER_ACTION);

			if (node)
			  {
			    node->info.trigger_action.action_type = PT_INVALIDATE_XACTION;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| PRINT char_string_literal
		{{ DBG_TRACE_GRAMMAR(trigger_action, | PRINT char_string_literal);

			PT_NODE *node = parser_new_node (this_parser, PT_TRIGGER_ACTION);

			if (node)
			  {
			    node->info.trigger_action.action_type = PT_PRINT;
			    node->info.trigger_action.string = $2;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| evaluate_stmt
		{{ DBG_TRACE_GRAMMAR(trigger_action, | evaluate_stmt);

			PT_NODE *node = parser_new_node (this_parser, PT_TRIGGER_ACTION);

			if (node)
			  {
			    node->info.trigger_action.action_type = PT_EXPRESSION;
			    node->info.trigger_action.expression = $1;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| insert_or_replace_stmt
		{{ DBG_TRACE_GRAMMAR(trigger_action, | insert_or_replace_stmt);

			PT_NODE *node = parser_new_node (this_parser, PT_TRIGGER_ACTION);

			if (node)
			  {
			    node->info.trigger_action.action_type = PT_EXPRESSION;
			    node->info.trigger_action.expression = $1;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| update_stmt
		{{ DBG_TRACE_GRAMMAR(trigger_action, | update_stmt);

			PT_NODE *node = parser_new_node (this_parser, PT_TRIGGER_ACTION);

			if (node)
			  {
			    node->info.trigger_action.action_type = PT_EXPRESSION;
			    node->info.trigger_action.expression = $1;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| delete_stmt
		{{ DBG_TRACE_GRAMMAR(trigger_action, | delete_stmt);

			PT_NODE *node = parser_new_node (this_parser, PT_TRIGGER_ACTION);

			if (node)
			  {
			    node->info.trigger_action.action_type = PT_EXPRESSION;
			    node->info.trigger_action.expression = $1;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| call_stmt
		{{ DBG_TRACE_GRAMMAR(trigger_action, | call_stmt);

			PT_NODE *node = parser_new_node (this_parser, PT_TRIGGER_ACTION);

			if (node)
			  {
			    node->info.trigger_action.action_type = PT_EXPRESSION;
			    node->info.trigger_action.expression = $1;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| merge_stmt
		{{ DBG_TRACE_GRAMMAR(trigger_action, | merge_stmt);

			PT_NODE *node = parser_new_node (this_parser, PT_TRIGGER_ACTION);

			if (node)
			  {
			    node->info.trigger_action.action_type = PT_EXPRESSION;
			    node->info.trigger_action.expression = $1;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

trigger_spec_list
	: trigger_name_list
		{{ DBG_TRACE_GRAMMAR(trigger_spec_list, : trigger_name_list);


			PT_NODE *node = parser_new_node (this_parser, PT_TRIGGER_SPEC_LIST);

			if (node)
			  {
			    node->info.trigger_spec_list.trigger_name_list = $1;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| ALL TRIGGERS
		{{ DBG_TRACE_GRAMMAR(trigger_spec_list, | ALL TRIGGERS);

			PT_NODE *node = parser_new_node (this_parser, PT_TRIGGER_SPEC_LIST);

			if (node)
			  {
			    node->info.trigger_spec_list.all_triggers = 1;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

trigger_status_or_priority_or_change_owner
	: trigger_status
		{{ DBG_TRACE_GRAMMAR(trigger_status_or_priority_or_change_owner, : trigger_status);

			container_3 ctn;
			SET_CONTAINER_3 (ctn, FROM_NUMBER ($1), NULL, NULL);
			$$ = ctn;

		DBG_PRINT}}
	| trigger_priority
		{{ DBG_TRACE_GRAMMAR(trigger_status_or_priority_or_change_owner, | trigger_priority);

			container_3 ctn;
			SET_CONTAINER_3 (ctn, FROM_NUMBER (PT_MISC_DUMMY), $1, NULL);
			$$ = ctn;

		DBG_PRINT}}
	| OWNER TO identifier
		{{ DBG_TRACE_GRAMMAR(trigger_status_or_priority_or_change_owner, | OWNER TO identifier);

			container_3 ctn;
			SET_CONTAINER_3 (ctn, FROM_NUMBER (PT_MISC_DUMMY), NULL, $3);
			$$ = ctn;

		DBG_PRINT}}
	;

opt_maximum
	: /* empty */
	| MAXIMUM
	;

trace_spec
	: ON_
		{{ DBG_TRACE_GRAMMAR(trace_spec, : ON_);

			PT_NODE *node = parser_new_node (this_parser, PT_VALUE);

			if (node)
			  {
			    node->info.value.data_value.i = -1;
			    node->type_enum = PT_TYPE_INTEGER;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| OFF_
		{{ DBG_TRACE_GRAMMAR(trace_spec, | OFF_);

			PT_NODE *node = parser_new_node (this_parser, PT_VALUE);

			if (node)
			  {
			    node->info.value.data_value.i = 0;
			    node->type_enum = PT_TYPE_INTEGER;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| unsigned_integer
		{{ DBG_TRACE_GRAMMAR(trace_spec, | unsigned_integer);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| param_
		{{ DBG_TRACE_GRAMMAR(trace_spec, | param_);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| host_param_input
		{{ DBG_TRACE_GRAMMAR(trace_spec, | host_param_input);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

depth_spec
	: unsigned_integer
		{{ DBG_TRACE_GRAMMAR(depth_spec, : unsigned_integer);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| param_
		{{ DBG_TRACE_GRAMMAR(depth_spec, | param_);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| host_param_input
		{{ DBG_TRACE_GRAMMAR(depth_spec, | host_param_input);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

serial_start
	: START_ WITH integer_text
		{{ DBG_TRACE_GRAMMAR(serial_start, : START_ WITH integer_text);

			PT_NODE *node = parser_new_node (this_parser, PT_VALUE);
			if (node)
			  {
			    node->info.value.data_value.str =
			      pt_append_bytes (this_parser, NULL, $3,
					       strlen ($3));
			    node->type_enum = PT_TYPE_NUMERIC;
			    PT_NODE_PRINT_VALUE_TO_TEXT (this_parser, node);
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

serial_increment
	: INCREMENT BY integer_text
		{{ DBG_TRACE_GRAMMAR(serial_increment, : INCREMENT BY integer_text);

			PT_NODE *node = parser_new_node (this_parser, PT_VALUE);
			if (node)
			  {
			    node->info.value.data_value.str =
			      pt_append_bytes (this_parser, NULL, $3,
					       strlen ($3));
			    node->type_enum = PT_TYPE_NUMERIC;
			    PT_NODE_PRINT_VALUE_TO_TEXT (this_parser, node);
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;


serial_min
	: MINVALUE integer_text
		{{ DBG_TRACE_GRAMMAR(serial_min, : MINVALUE integer_text);

			container_2 ctn;
			PT_NODE *node = parser_new_node (this_parser, PT_VALUE);
			if (node)
			  {
			    node->info.value.data_value.str =
			      pt_append_bytes (this_parser, NULL, $2,
					       strlen ($2));
			    node->type_enum = PT_TYPE_NUMERIC;
			    PT_NODE_PRINT_VALUE_TO_TEXT (this_parser, node);
			  }

			SET_CONTAINER_2 (ctn, node, FROM_NUMBER (0));
			$$ = ctn;

		DBG_PRINT}}
	| NOMINVALUE
		{{ DBG_TRACE_GRAMMAR(serial_min, | NOMINVALUE);

			container_2 ctn;
			SET_CONTAINER_2 (ctn, NULL, FROM_NUMBER (1));
			$$ = ctn;

		DBG_PRINT}}
	;

serial_max
	: MAXVALUE integer_text
		{{ DBG_TRACE_GRAMMAR(serial_max, : MAXVALUE integer_text);

			container_2 ctn;
			PT_NODE *node = parser_new_node (this_parser, PT_VALUE);
			if (node)
			  {
			    node->info.value.data_value.str =
			      pt_append_bytes (this_parser, NULL, $2,
					       strlen ($2));
			    node->type_enum = PT_TYPE_NUMERIC;
			    PT_NODE_PRINT_VALUE_TO_TEXT (this_parser, node);
			  }

			SET_CONTAINER_2 (ctn, node, FROM_NUMBER (0));
			$$ = ctn;

		DBG_PRINT}}
	| NOMAXVALUE
		{{ DBG_TRACE_GRAMMAR(serial_max, | NOMAXVALUE);

			container_2 ctn;
			SET_CONTAINER_2 (ctn, NULL, FROM_NUMBER (1));
			$$ = ctn;

		DBG_PRINT}}
	;

of_cycle_nocycle
	: CYCLE
		{{ DBG_TRACE_GRAMMAR(of_cycle_nocycle, : CYCLE);

			container_2 ctn;
			SET_CONTAINER_2 (ctn, FROM_NUMBER (1), FROM_NUMBER (0));
			$$ = ctn;

		DBG_PRINT}}
	| NOCYCLE
		{{ DBG_TRACE_GRAMMAR(of_cycle_nocycle, | NOCYCLE);

			container_2 ctn;
			SET_CONTAINER_2 (ctn, FROM_NUMBER (0), FROM_NUMBER (1));
			$$ = ctn;

		DBG_PRINT}}
	;

of_cached_num
	: CACHE unsigned_int32
		{{ DBG_TRACE_GRAMMAR(of_cached_num, : CACHE unsigned_int32);

			container_2 ctn;
			SET_CONTAINER_2 (ctn, $2, FROM_NUMBER (0));
			$$ = ctn;

		DBG_PRINT}}
	| NOCACHE
		{{ DBG_TRACE_GRAMMAR(of_cached_num, | NOCACHE);
			container_2 ctn;
			SET_CONTAINER_2 (ctn, NULL, FROM_NUMBER (1));
			$$ = ctn;

		DBG_PRINT}}
	;

integer_text
	: opt_plus UNSIGNED_INTEGER
		{{ DBG_TRACE_GRAMMAR(integer_text, : opt_plus UNSIGNED_INTEGER);

			$$ = $2;

		DBG_PRINT}}
	| '-' UNSIGNED_INTEGER
		{{ DBG_TRACE_GRAMMAR(integer_text, | '-' UNSIGNED_INTEGER);

			$$ = pt_append_string (this_parser, (char *) "-", $2);

		DBG_PRINT}}
	;

uint_text
	: UNSIGNED_INTEGER
		{{ DBG_TRACE_GRAMMAR(uint_text, : UNSIGNED_INTEGER);

			$$ = $1;

		DBG_PRINT}}
	;

opt_plus
	: /* empty */
	| '+'
	;

opt_of_data_type_cursor
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_of_data_type_cursor, : );

			$$ = PT_TYPE_NONE;

		DBG_PRINT}}
	| data_type
		{{ DBG_TRACE_GRAMMAR(opt_of_data_type_cursor, | data_type);

			$$ = (int) TO_NUMBER (CONTAINER_AT_0 ($1));

		DBG_PRINT}}
	| CURSOR
		{{ DBG_TRACE_GRAMMAR(opt_of_data_type_cursor, | CURSOR);

			$$ = PT_TYPE_RESULTSET;

		DBG_PRINT}}
	;

opt_of_is_as
	: /* empty */
	| IS
	| AS
	;

opt_sp_param_list
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_sp_param_list, : );

			$$ = NULL;

		DBG_PRINT}}
	| sp_param_list
		{{ DBG_TRACE_GRAMMAR(opt_sp_param_list, | sp_param_list);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

sp_param_list
	: sp_param_list ',' sp_param_def
		{{ DBG_TRACE_GRAMMAR(sp_param_list, : sp_param_list ',' sp_param_def); 

			$$ = parser_make_link ($1, $3);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| sp_param_def
		{{ DBG_TRACE_GRAMMAR(sp_param_list, | sp_param_def); 

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

sp_param_def
	: identifier
	  opt_sp_in_out
	  data_type
	  opt_comment_spec
		{{ DBG_TRACE_GRAMMAR(sp_param_def, : identifier opt_sp_in_out data_type opt_comment_spec); 

			PT_NODE *node = parser_new_node (this_parser, PT_SP_PARAMETERS);

			if (node)
			  {
			    node->type_enum = TO_NUMBER (CONTAINER_AT_0 ($3));
			    node->data_type = CONTAINER_AT_1 ($3);
			    node->info.sp_param.name = $1;
			    node->info.sp_param.mode = $2;
			    node->info.sp_param.comment = $4;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| identifier
	  opt_sp_in_out
	  CURSOR
	  opt_comment_spec
		{{ DBG_TRACE_GRAMMAR(sp_param_def, | identifier opt_sp_in_out CURSOR opt_comment_spec); 

			PT_NODE *node = parser_new_node (this_parser, PT_SP_PARAMETERS);

			if (node)
			  {
			    node->type_enum = PT_TYPE_RESULTSET;
			    node->data_type = NULL;
			    node->info.sp_param.name = $1;
			    node->info.sp_param.mode = $2;
			    node->info.sp_param.comment = $4;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

opt_sp_in_out
	: opt_in_out
		{{ DBG_TRACE_GRAMMAR(opt_sp_in_out, : opt_in_out); 

			$$ = $1;

		DBG_PRINT}}
	| IN_ OUT_
		{{ DBG_TRACE_GRAMMAR(opt_sp_in_out, | IN_ OUT_); 

			$$ = PT_INPUTOUTPUT;

		DBG_PRINT}}
	;

esql_query_stmt
	: 	{ parser_select_level++; }
	  csql_query_select_has_no_with_clause
		{{ DBG_TRACE_GRAMMAR(esql_query_stmt, : csql_query_select_has_no_with_clause); 
			$$ = $2;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
			parser_select_level--;

		DBG_PRINT}}
	;

csql_query
	:
		{{

			parser_save_and_set_cannot_cache (false);
			parser_save_and_set_ic (0);
			parser_save_and_set_gc (0);
			parser_save_and_set_oc (0);
			parser_save_and_set_wjc (0);
			parser_save_and_set_sysc (0);
			parser_save_and_set_prc (0);
			parser_save_and_set_cbrc (0);
			parser_save_and_set_serc (1);
			parser_save_and_set_sqc (1);
			parser_save_and_set_pseudoc (1);

		DBG_PRINT}}
	  select_expression_opt_with
		{{ DBG_TRACE_GRAMMAR(csql_query, : select_expression_opt_with);

			PT_NODE *node = $2;
			parser_push_orderby_node (node);

		DBG_PRINT}}
	  opt_orderby_clause
		{{ DBG_TRACE_GRAMMAR(csql_query, opt_orderby_clause);

			PT_NODE *node = parser_pop_orderby_node ();

			if (node && parser_cannot_cache)
			  {
			    node->info.query.flag.reexecute = 1;
			    node->info.query.flag.do_cache = 0;
			    node->info.query.flag.do_not_cache = 1;
			  }

			parser_restore_cannot_cache ();
			parser_restore_ic ();
			parser_restore_gc ();
			parser_restore_oc ();
			parser_restore_wjc ();
			parser_restore_sysc ();
			parser_restore_prc ();
			parser_restore_cbrc ();
			parser_restore_serc ();
			parser_restore_sqc ();
			parser_restore_pseudoc ();

			if (parser_subquery_check == 0)
			    PT_ERRORmf(this_parser, pt_top(this_parser),
				MSGCAT_SET_PARSER_SEMANTIC,
				MSGCAT_SEMANTIC_NOT_ALLOWED_HERE, "Subquery");

			if (node)
			  {
			    /* handle ORDER BY NULL */
			    PT_NODE *order = node->info.query.order_by;
			    if (order && order->info.sort_spec.expr
				&& order->info.sort_spec.expr->node_type == PT_VALUE
				&& order->info.sort_spec.expr->type_enum == PT_TYPE_NULL)
			      {
				if (!node->info.query.q.select.group_by)
				  {
				    PT_ERRORm (this_parser, node, MSGCAT_SET_PARSER_SEMANTIC,
					       MSGCAT_SEMANTIC_ORDERBYNULL_REQUIRES_GROUPBY);
				  }
				else
				  {
				    parser_free_tree (this_parser, node->info.query.order_by);
				    node->info.query.order_by = NULL;
				  }
			      }
			  }

			parser_push_orderby_node (node);

		DBG_PRINT}}
	opt_select_limit_clause
	opt_for_update_clause
		{{ DBG_TRACE_GRAMMAR(csql_query, opt_select_limit_clause opt_for_update_clause);

			PT_NODE *node = parser_pop_orderby_node ();
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

csql_query_select_has_no_with_clause
	:
		{{

			parser_save_and_set_cannot_cache (false);
			parser_save_and_set_ic (0);
			parser_save_and_set_gc (0);
			parser_save_and_set_oc (0);
			parser_save_and_set_wjc (0);
			parser_save_and_set_sysc (0);
			parser_save_and_set_prc (0);
			parser_save_and_set_cbrc (0);
			parser_save_and_set_serc (1);
			parser_save_and_set_sqc (1);
			parser_save_and_set_pseudoc (1);

		DBG_PRINT}}
	  select_expression
		{{ DBG_TRACE_GRAMMAR(csql_query_select_has_no_with_clause, : select_expression);

			PT_NODE *node = $2;
			parser_push_orderby_node (node);

		DBG_PRINT}}
	  opt_orderby_clause
		{{ DBG_TRACE_GRAMMAR(csql_query_select_has_no_with_clause, opt_orderby_clause);

			PT_NODE *node = parser_pop_orderby_node ();

			if (node && parser_cannot_cache)
			  {
			    node->info.query.flag.reexecute = 1;
			    node->info.query.flag.do_cache = 0;
			    node->info.query.flag.do_not_cache = 1;
			  }

			parser_restore_cannot_cache ();
			parser_restore_ic ();
			parser_restore_gc ();
			parser_restore_oc ();
			parser_restore_wjc ();
			parser_restore_sysc ();
			parser_restore_prc ();
			parser_restore_cbrc ();
			parser_restore_serc ();
			parser_restore_sqc ();
			parser_restore_pseudoc ();

			if (parser_subquery_check == 0)
			    PT_ERRORmf(this_parser, pt_top(this_parser),
				MSGCAT_SET_PARSER_SEMANTIC,
				MSGCAT_SEMANTIC_NOT_ALLOWED_HERE, "Subquery");

			if (node)
			  {
			    /* handle ORDER BY NULL */
			    PT_NODE *order = node->info.query.order_by;
			    if (order && order->info.sort_spec.expr
				&& order->info.sort_spec.expr->node_type == PT_VALUE
				&& order->info.sort_spec.expr->type_enum == PT_TYPE_NULL)
			      {
				if (!node->info.query.q.select.group_by)
				  {
				    PT_ERRORm (this_parser, node, MSGCAT_SET_PARSER_SEMANTIC,
					       MSGCAT_SEMANTIC_ORDERBYNULL_REQUIRES_GROUPBY);
				  }
				else
				  {
				    parser_free_tree (this_parser, node->info.query.order_by);
				    node->info.query.order_by = NULL;
				  }
			      }
			  }

			parser_push_orderby_node (node);

		DBG_PRINT}}
	opt_select_limit_clause
	opt_for_update_clause
		{{ DBG_TRACE_GRAMMAR(csql_query_select_has_no_with_clause, opt_select_limit_clause opt_for_update_clause);

			PT_NODE *node = parser_pop_orderby_node ();
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

csql_query_without_subquery_and_with_clause
	:
		{{

			parser_save_and_set_cannot_cache (false);
			parser_save_and_set_ic (0);
			parser_save_and_set_gc (0);
			parser_save_and_set_oc (0);
			parser_save_and_set_wjc (0);
			parser_save_and_set_sysc (0);
			parser_save_and_set_prc (0);
			parser_save_and_set_cbrc (0);
			parser_save_and_set_serc (1);
			parser_save_and_set_sqc (1);
			parser_save_and_set_pseudoc (1);

		DBG_PRINT}}
	  select_expression_without_subquery
		{{ DBG_TRACE_GRAMMAR(csql_query_without_subquery_and_with_clause, : select_expression_without_subquery);

			PT_NODE *node = $2;
			parser_push_orderby_node (node);

		DBG_PRINT}}
	  opt_orderby_clause
		{{ DBG_TRACE_GRAMMAR(csql_query_without_subquery_and_with_clause, opt_orderby_clause);

			PT_NODE *node = parser_pop_orderby_node ();

			if (node && parser_cannot_cache)
			  {
			    node->info.query.flag.reexecute = 1;
			    node->info.query.flag.do_cache = 0;
			    node->info.query.flag.do_not_cache = 1;
			  }

			parser_restore_cannot_cache ();
			parser_restore_ic ();
			parser_restore_gc ();
			parser_restore_oc ();
			parser_restore_wjc ();
			parser_restore_sysc ();
			parser_restore_prc ();
			parser_restore_cbrc ();
			parser_restore_serc ();
			parser_restore_sqc ();
			parser_restore_pseudoc ();

			if (parser_subquery_check == 0)
			    PT_ERRORmf(this_parser, pt_top(this_parser),
				MSGCAT_SET_PARSER_SEMANTIC,
				MSGCAT_SEMANTIC_NOT_ALLOWED_HERE, "Subquery");

			if (node)
			  {
			    /* handle ORDER BY NULL */
			    PT_NODE *order = node->info.query.order_by;
			    if (order && order->info.sort_spec.expr
				&& order->info.sort_spec.expr->node_type == PT_VALUE
				&& order->info.sort_spec.expr->type_enum == PT_TYPE_NULL)
			      {
				if (!node->info.query.q.select.group_by)
				  {
				    PT_ERRORm (this_parser, node, MSGCAT_SET_PARSER_SEMANTIC,
					       MSGCAT_SEMANTIC_ORDERBYNULL_REQUIRES_GROUPBY);
				  }
				else
				  {
				    parser_free_tree (this_parser, node->info.query.order_by);
				    node->info.query.order_by = NULL;
				  }
			      }
			  }

			parser_push_orderby_node (node);

		DBG_PRINT}}
	opt_select_limit_clause
	opt_for_update_clause
		{{ DBG_TRACE_GRAMMAR(csql_query_without_subquery_and_with_clause, opt_select_limit_clause opt_for_update_clause);

			PT_NODE *node = parser_pop_orderby_node ();
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

csql_query_without_values_query
	:
		{{

			parser_save_and_set_cannot_cache (false);
			parser_save_and_set_ic (0);
			parser_save_and_set_gc (0);
			parser_save_and_set_oc (0);
			parser_save_and_set_wjc (0);
			parser_save_and_set_sysc (0);
			parser_save_and_set_prc (0);
			parser_save_and_set_cbrc (0);
			parser_save_and_set_serc (1);
			parser_save_and_set_sqc (1);
			parser_save_and_set_pseudoc (1);

		DBG_PRINT}}
	  select_expression_without_values_query
		{{ DBG_TRACE_GRAMMAR(csql_query_without_values_query, : select_expression_without_values_query);

			PT_NODE *node = $2;
			parser_push_orderby_node (node);

		DBG_PRINT}}
	  opt_orderby_clause
		{{ DBG_TRACE_GRAMMAR(csql_query_without_values_query, opt_orderby_clause);

			PT_NODE *node = parser_pop_orderby_node ();

			if (node && parser_cannot_cache)
			  {
			    node->info.query.flag.reexecute = 1;
			    node->info.query.flag.do_cache = 0;
			    node->info.query.flag.do_not_cache = 1;
			  }

			parser_restore_cannot_cache ();
			parser_restore_ic ();
			parser_restore_gc ();
			parser_restore_oc ();
			parser_restore_wjc ();
			parser_restore_sysc ();
			parser_restore_prc ();
			parser_restore_cbrc ();
			parser_restore_serc ();
			parser_restore_sqc ();
			parser_restore_pseudoc ();

			if (parser_subquery_check == 0)
			    PT_ERRORmf(this_parser, pt_top(this_parser),
				MSGCAT_SET_PARSER_SEMANTIC,
				MSGCAT_SEMANTIC_NOT_ALLOWED_HERE, "Subquery");

			if (node)
			  {
			    /* handle ORDER BY NULL */
			    PT_NODE *order = node->info.query.order_by;
			    if (order && order->info.sort_spec.expr
				&& order->info.sort_spec.expr->node_type == PT_VALUE
				&& order->info.sort_spec.expr->type_enum == PT_TYPE_NULL)
			      {
				if (!node->info.query.q.select.group_by)
				  {
				    PT_ERRORm (this_parser, node, MSGCAT_SET_PARSER_SEMANTIC,
					       MSGCAT_SEMANTIC_ORDERBYNULL_REQUIRES_GROUPBY);
				  }
				else
				  {
				    parser_free_tree (this_parser, node->info.query.order_by);
				    node->info.query.order_by = NULL;
				  }
			      }
			  }

			parser_push_orderby_node (node);

		DBG_PRINT}}
	opt_select_limit_clause
	opt_for_update_clause
		{{ DBG_TRACE_GRAMMAR(csql_query_without_values_query, opt_select_limit_clause	opt_for_update_clause);

			PT_NODE *node = parser_pop_orderby_node ();
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

csql_query_without_values_query_no_with_clause
	:
		{{

			parser_save_and_set_cannot_cache (false);
			parser_save_and_set_ic (0);
			parser_save_and_set_gc (0);
			parser_save_and_set_oc (0);
			parser_save_and_set_wjc (0);
			parser_save_and_set_sysc (0);
			parser_save_and_set_prc (0);
			parser_save_and_set_cbrc (0);
			parser_save_and_set_serc (1);
			parser_save_and_set_sqc (1);
			parser_save_and_set_pseudoc (1);

		DBG_PRINT}}
	  select_expression_without_values_query_no_with_clause
		{{ DBG_TRACE_GRAMMAR(csql_query_without_values_query_no_with_clause, : select_expression_without_values_query_no_with_clause);

			PT_NODE *node = $2;
			parser_push_orderby_node (node);

		DBG_PRINT}}
	  opt_orderby_clause
		{{ DBG_TRACE_GRAMMAR(csql_query_without_values_query_no_with_clause, opt_orderby_clause);

			PT_NODE *node = parser_pop_orderby_node ();

			if (node && parser_cannot_cache)
			  {
			    node->info.query.flag.reexecute = 1;
			    node->info.query.flag.do_cache = 0;
			    node->info.query.flag.do_not_cache = 1;
			  }

			parser_restore_cannot_cache ();
			parser_restore_ic ();
			parser_restore_gc ();
			parser_restore_oc ();
			parser_restore_wjc ();
			parser_restore_sysc ();
			parser_restore_prc ();
			parser_restore_cbrc ();
			parser_restore_serc ();
			parser_restore_sqc ();
			parser_restore_pseudoc ();

			if (parser_subquery_check == 0)
			    PT_ERRORmf(this_parser, pt_top(this_parser),
				MSGCAT_SET_PARSER_SEMANTIC,
				MSGCAT_SEMANTIC_NOT_ALLOWED_HERE, "Subquery");

			if (node)
			  {
			    /* handle ORDER BY NULL */
			    PT_NODE *order = node->info.query.order_by;
			    if (order && order->info.sort_spec.expr
				&& order->info.sort_spec.expr->node_type == PT_VALUE
				&& order->info.sort_spec.expr->type_enum == PT_TYPE_NULL)
			      {
				if (!node->info.query.q.select.group_by)
				  {
				    PT_ERRORm (this_parser, node, MSGCAT_SET_PARSER_SEMANTIC,
					       MSGCAT_SEMANTIC_ORDERBYNULL_REQUIRES_GROUPBY);
				  }
				else
				  {
				    parser_free_tree (this_parser, node->info.query.order_by);
				    node->info.query.order_by = NULL;
				  }
			      }
			  }

			parser_push_orderby_node (node);

		DBG_PRINT}}
	  opt_select_limit_clause
	  opt_for_update_clause
		{{ DBG_TRACE_GRAMMAR(csql_query_without_values_query_no_with_clause, opt_select_limit_clause opt_for_update_clause);

			PT_NODE *node = parser_pop_orderby_node ();
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

csql_query_without_values_and_single_subquery
	:
		{{

			parser_save_and_set_cannot_cache (false);
			parser_save_and_set_ic (0);
			parser_save_and_set_gc (0);
			parser_save_and_set_oc (0);
			parser_save_and_set_wjc (0);
			parser_save_and_set_sysc (0);
			parser_save_and_set_prc (0);
			parser_save_and_set_cbrc (0);
			parser_save_and_set_serc (1);
			parser_save_and_set_sqc (1);
			parser_save_and_set_pseudoc (1);

		DBG_PRINT}}
	  select_expression_without_values_and_single_subquery
		{{ DBG_TRACE_GRAMMAR(csql_query_without_values_and_single_subquery, : select_expression_without_values_and_single_subquery);

			PT_NODE *node = $2;
			parser_push_orderby_node (node);

		DBG_PRINT}}
	  opt_orderby_clause
		{{ DBG_TRACE_GRAMMAR(csql_query_without_values_and_single_subquery,  opt_orderby_clause);

			PT_NODE *node = parser_pop_orderby_node ();

			if (node && parser_cannot_cache)
			  {
			    node->info.query.flag.reexecute = 1;
			    node->info.query.flag.do_cache = 0;
			    node->info.query.flag.do_not_cache = 1;
			  }

			parser_restore_cannot_cache ();
			parser_restore_ic ();
			parser_restore_gc ();
			parser_restore_oc ();
			parser_restore_wjc ();
			parser_restore_sysc ();
			parser_restore_prc ();
			parser_restore_cbrc ();
			parser_restore_serc ();
			parser_restore_sqc ();
			parser_restore_pseudoc ();

			if (parser_subquery_check == 0)
			    PT_ERRORmf (this_parser, pt_top(this_parser), MSGCAT_SET_PARSER_SEMANTIC,
				        MSGCAT_SEMANTIC_NOT_ALLOWED_HERE, "Subquery");

			if (node)
			  {
			    /* handle ORDER BY NULL */
			    PT_NODE *order = node->info.query.order_by;
			    if (order && order->info.sort_spec.expr
				&& order->info.sort_spec.expr->node_type == PT_VALUE
				&& order->info.sort_spec.expr->type_enum == PT_TYPE_NULL)
			      {
				if (!node->info.query.q.select.group_by)
				  {
				    PT_ERRORm (this_parser, node, MSGCAT_SET_PARSER_SEMANTIC,
					       MSGCAT_SEMANTIC_ORDERBYNULL_REQUIRES_GROUPBY);
				  }
				else
				  {
				    parser_free_tree (this_parser, node->info.query.order_by);
				    node->info.query.order_by = NULL;
				  }
			      }
			  }

			parser_push_orderby_node (node);

		DBG_PRINT}}
	  opt_select_limit_clause
	  opt_for_update_clause
		{{ DBG_TRACE_GRAMMAR(csql_query_without_values_and_single_subquery,  opt_select_limit_clause opt_for_update_clause);

			PT_NODE *node = parser_pop_orderby_node ();
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;


select_expression_opt_with
	: opt_with_clause
	  select_expression
		{{ DBG_TRACE_GRAMMAR(select_expression_opt_with,  opt_with_clause select_expression);

			PT_NODE *with_clause = $1;
			PT_NODE *stmt = $2;
			if (stmt && with_clause)
			  {
			    stmt->info.query.with = with_clause;
			  }

			$$ = stmt;

		DBG_PRINT}}
	;

select_expression_without_subquery
	: select_expression_without_subquery
		{{ DBG_TRACE_GRAMMAR(select_expression_without_subquery, : select_expression_without_subquery);
        		PT_NODE *node = $1;
			parser_push_orderby_node (node);
	        }}

	  opt_orderby_clause
	        {{ DBG_TRACE_GRAMMAR(select_expression_without_subquery,  opt_orderby_clause);

			PT_NODE *node = parser_pop_orderby_node ();

			if (node && parser_cannot_cache)
			  {
			    node->info.query.flag.reexecute = 1;
			    node->info.query.flag.do_cache = 0;
			    node->info.query.flag.do_not_cache = 1;
			  }


			if (parser_subquery_check == 0)
			  PT_ERRORmf (this_parser, pt_top(this_parser), MSGCAT_SET_PARSER_SEMANTIC,
				      MSGCAT_SEMANTIC_NOT_ALLOWED_HERE, "Subquery");

			if (node)
			  {
			    PT_NODE *order = node->info.query.order_by;
			    if (order && order->info.sort_spec.expr
				&& order->info.sort_spec.expr->node_type == PT_VALUE
				&& order->info.sort_spec.expr->type_enum == PT_TYPE_NULL)
			      {
				if (!node->info.query.q.select.group_by)
				  {
				    PT_ERRORm (this_parser, node, MSGCAT_SET_PARSER_SEMANTIC,
					       MSGCAT_SEMANTIC_ORDERBYNULL_REQUIRES_GROUPBY);
				  }
				else
				  {
				    parser_free_tree (this_parser, node->info.query.order_by);
				    node->info.query.order_by = NULL;
				  }
			     }
			  }

			parser_push_orderby_node (node);

		DBG_PRINT}}

          opt_select_limit_clause
          opt_for_update_clause
		{{ DBG_TRACE_GRAMMAR(select_expression_without_subquery,  opt_select_limit_clause opt_for_update_clause);

			PT_NODE *node = parser_pop_orderby_node ();
			$<node>$ = node;
			PARSER_SAVE_ERR_CONTEXT ($<node>$, @$.buffer_pos)

                DBG_PRINT}}

          table_op select_or_values_query
                {{ DBG_TRACE_GRAMMAR(select_expression_without_subquery,  select_or_values_query);

			PT_NODE *stmt = $8;
			PT_NODE *arg1 = $1;

			if (stmt)
			  {
			    stmt->info.query.id = (UINTPTR) stmt;
			    stmt->info.query.q.union_.arg1 = $1;
			    stmt->info.query.q.union_.arg2 = $9;
                            stmt->flag.recompile = $1->flag.recompile | $9->flag.recompile;

			    if (arg1 != NULL
			        && arg1->info.query.is_subquery != PT_IS_SUBQUERY
			        && arg1->info.query.order_by != NULL)
			      {
			        PT_ERRORm (this_parser, stmt,
			               MSGCAT_SET_PARSER_SYNTAX,
			               MSGCAT_SYNTAX_INVALID_UNION_ORDERBY);
			      }
			  }


			$$ = stmt;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| select_or_values_query
		{{ DBG_TRACE_GRAMMAR(select_expression_without_subquery, | select_or_values_query);
			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

select_expression
	: select_expression
		{{ DBG_TRACE_GRAMMAR(select_expression, : select_expression);
			PT_NODE *node = $1;
			parser_push_orderby_node (node);
		}}
	  opt_orderby_clause
		{{ DBG_TRACE_GRAMMAR(select_expression, opt_orderby_clause);

			PT_NODE *node = parser_pop_orderby_node ();

			if (node && parser_cannot_cache)
			  {
			    node->info.query.flag.reexecute = 1;
			    node->info.query.flag.do_cache = 0;
			    node->info.query.flag.do_not_cache = 1;
			  }


			if (parser_subquery_check == 0)
			  PT_ERRORmf (this_parser, pt_top(this_parser), MSGCAT_SET_PARSER_SEMANTIC,
				      MSGCAT_SEMANTIC_NOT_ALLOWED_HERE, "Subquery");

			if (node)
			  {

			    PT_NODE *order = node->info.query.order_by;
			    if (order && order->info.sort_spec.expr
				&& order->info.sort_spec.expr->node_type == PT_VALUE
				&& order->info.sort_spec.expr->type_enum == PT_TYPE_NULL)
			      {
				if (!node->info.query.q.select.group_by)
				  {
				    PT_ERRORm (this_parser, node, MSGCAT_SET_PARSER_SEMANTIC,
					       MSGCAT_SEMANTIC_ORDERBYNULL_REQUIRES_GROUPBY);
				  }
				else
				  {
				    parser_free_tree (this_parser, node->info.query.order_by);
				    node->info.query.order_by = NULL;
				  }
			      }
			  }

			parser_push_orderby_node (node);

		DBG_PRINT}}
	  opt_select_limit_clause
	  opt_for_update_clause
		{{ DBG_TRACE_GRAMMAR(select_expression, opt_select_limit_clause opt_for_update_clause);

			PT_NODE *node = parser_pop_orderby_node ();
			$<node>$ = node;
			PARSER_SAVE_ERR_CONTEXT ($<node>$, @$.buffer_pos)

		DBG_PRINT}}
	  table_op select_or_subquery
		{{ DBG_TRACE_GRAMMAR(select_expression, table_op select_or_subquery);

			PT_NODE *stmt = $8;
			PT_NODE *arg1 = $1;

			if (stmt)
			  {
			    stmt->info.query.id = (UINTPTR) stmt;
			    stmt->info.query.q.union_.arg1 = $1;
			    stmt->info.query.q.union_.arg2 = $9;
                            stmt->flag.recompile = $1->flag.recompile | $9->flag.recompile;

			    if (arg1 != NULL
			        && arg1->info.query.is_subquery != PT_IS_SUBQUERY
			        && arg1->info.query.order_by != NULL)
			      {
			        PT_ERRORm (this_parser, stmt, MSGCAT_SET_PARSER_SYNTAX,
			                   MSGCAT_SYNTAX_INVALID_UNION_ORDERBY);
			      }
			  }


			$$ = stmt;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| select_or_subquery
		{{ DBG_TRACE_GRAMMAR(select_expression, | select_or_subquery);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

select_expression_without_values_query
	: select_expression_without_values_query
		{{ DBG_TRACE_GRAMMAR(select_expression_without_values_query, : select_expression_without_values_query);
			PT_NODE *node = $1;
			parser_push_orderby_node (node);
		}}
	  opt_orderby_clause
		{{ DBG_TRACE_GRAMMAR(select_expression_without_values_query, opt_orderby_clause);

			PT_NODE *node = parser_pop_orderby_node ();

			if (node && parser_cannot_cache)
			  {
			    node->info.query.flag.reexecute = 1;
			    node->info.query.flag.do_cache = 0;
			    node->info.query.flag.do_not_cache = 1;
			  }

			if (parser_subquery_check == 0)
			  PT_ERRORmf (this_parser, pt_top(this_parser), MSGCAT_SET_PARSER_SEMANTIC,
				      MSGCAT_SEMANTIC_NOT_ALLOWED_HERE, "Subquery");

			if (node)
			  {

			    PT_NODE *order = node->info.query.order_by;
			    if (order && order->info.sort_spec.expr
				&& order->info.sort_spec.expr->node_type == PT_VALUE
				&& order->info.sort_spec.expr->type_enum == PT_TYPE_NULL)
			      {
				if (!node->info.query.q.select.group_by)
				  {
				    PT_ERRORm (this_parser, node, MSGCAT_SET_PARSER_SEMANTIC,
					       MSGCAT_SEMANTIC_ORDERBYNULL_REQUIRES_GROUPBY);
				  }
				else
				  {
				    parser_free_tree (this_parser, node->info.query.order_by);
				    node->info.query.order_by = NULL;
				  }
			      }
			  }

			parser_push_orderby_node (node);

		DBG_PRINT}}
	  opt_select_limit_clause
	  opt_for_update_clause
		{{ DBG_TRACE_GRAMMAR(select_expression_without_values_query, opt_select_limit_clause opt_for_update_clause);

			PT_NODE *node = parser_pop_orderby_node ();
			$<node>$ = node;
			PARSER_SAVE_ERR_CONTEXT ($<node>$, @$.buffer_pos)

		DBG_PRINT}}
	  table_op select_or_subquery_without_values_query
		{{ DBG_TRACE_GRAMMAR(select_expression_without_values_query, table_op select_or_subquery_without_values_query);

			PT_NODE *stmt = $8;
			PT_NODE *arg1 = $1;

			if (stmt)
			  {
			    stmt->info.query.id = (UINTPTR) stmt;
			    stmt->info.query.q.union_.arg1 = $1;
			    stmt->info.query.q.union_.arg2 = $9;
                            stmt->flag.recompile = $1->flag.recompile | $9->flag.recompile;

			    if (arg1 != NULL
			        && arg1->info.query.is_subquery != PT_IS_SUBQUERY
			        && arg1->info.query.order_by != NULL)
			      {
			        PT_ERRORm (this_parser, stmt, MSGCAT_SET_PARSER_SYNTAX,
			                   MSGCAT_SYNTAX_INVALID_UNION_ORDERBY);
			      }
			   }


			$$ = stmt;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| select_or_subquery_without_values_query
		{{ DBG_TRACE_GRAMMAR(select_expression_without_values_query, | select_or_subquery_without_values_query);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

select_expression_without_values_query_no_with_clause
	: select_expression_without_values_query_no_with_clause
		{{ DBG_TRACE_GRAMMAR(select_expression_without_values_query_no_with_clause, : select_expression_without_values_query_no_with_clause);
			PT_NODE *node = $1;
			parser_push_orderby_node (node);
		}}
	  opt_orderby_clause
		{{ DBG_TRACE_GRAMMAR(select_expression_without_values_query_no_with_clause, opt_orderby_clause);

			PT_NODE *node = parser_pop_orderby_node ();

			if (node && parser_cannot_cache)
			  {
			    node->info.query.flag.reexecute = 1;
			    node->info.query.flag.do_cache = 0;
			    node->info.query.flag.do_not_cache = 1;
			  }

			if (parser_subquery_check == 0)
			  PT_ERRORmf (this_parser, pt_top(this_parser), MSGCAT_SET_PARSER_SEMANTIC,
				      MSGCAT_SEMANTIC_NOT_ALLOWED_HERE, "Subquery");

			if (node)
			  {

			    PT_NODE *order = node->info.query.order_by;
			    if (order && order->info.sort_spec.expr
				&& order->info.sort_spec.expr->node_type == PT_VALUE
				&& order->info.sort_spec.expr->type_enum == PT_TYPE_NULL)
			      {
				if (!node->info.query.q.select.group_by)
				  {
				    PT_ERRORm (this_parser, node, MSGCAT_SET_PARSER_SEMANTIC,
					       MSGCAT_SEMANTIC_ORDERBYNULL_REQUIRES_GROUPBY);
				  }
				else
				  {
				    parser_free_tree (this_parser, node->info.query.order_by);
				    node->info.query.order_by = NULL;
				  }
			      }
			  }

			parser_push_orderby_node (node);

		DBG_PRINT}}
	  opt_select_limit_clause
	  opt_for_update_clause
		{{ DBG_TRACE_GRAMMAR(select_expression_without_values_query_no_with_clause, opt_select_limit_clause opt_for_update_clause);

			PT_NODE *node = parser_pop_orderby_node ();
			$<node>$ = node;
			PARSER_SAVE_ERR_CONTEXT ($<node>$, @$.buffer_pos)

		DBG_PRINT}}
	  table_op select_or_subquery_without_values_query_no_with_clause
		{{ DBG_TRACE_GRAMMAR(select_expression_without_values_query_no_with_clause, table_op select_or_subquery_without_values_query_no_with_clause);

			PT_NODE *stmt = $8;
			PT_NODE *arg1 = $1;

			if (stmt)
			  {
			    stmt->info.query.id = (UINTPTR) stmt;
			    stmt->info.query.q.union_.arg1 = $1;
			    stmt->info.query.q.union_.arg2 = $9;
                            stmt->flag.recompile = $1->flag.recompile | $9->flag.recompile;

			    if (arg1 != NULL
			        && arg1->info.query.is_subquery != PT_IS_SUBQUERY
			        && arg1->info.query.order_by != NULL)
			      {
			        PT_ERRORm (this_parser, stmt, MSGCAT_SET_PARSER_SYNTAX,
			                   MSGCAT_SYNTAX_INVALID_UNION_ORDERBY);
			      }
			  }


			$$ = stmt;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| select_or_subquery_without_values_query_no_with_clause
		{{ DBG_TRACE_GRAMMAR(select_expression_without_values_query_no_with_clause, | select_or_subquery_without_values_query_no_with_clause);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

select_expression_without_values_and_single_subquery
	: select_expression_without_values_query_no_with_clause
		{{ DBG_TRACE_GRAMMAR(select_expression_without_values_and_single_subquery, : select_expression_without_values_query_no_with_clause);
			PT_NODE *node = $1;
			parser_push_orderby_node (node);
		}}
	  opt_orderby_clause
		{{ DBG_TRACE_GRAMMAR(select_expression_without_values_and_single_subquery, opt_orderby_clause);

			PT_NODE *node = parser_pop_orderby_node ();

			if (node && parser_cannot_cache)
			  {
			    node->info.query.flag.reexecute = 1;
			    node->info.query.flag.do_cache = 0;
			    node->info.query.flag.do_not_cache = 1;
			  }


			if (parser_subquery_check == 0)
			  PT_ERRORmf (this_parser, pt_top(this_parser), MSGCAT_SET_PARSER_SEMANTIC,
				      MSGCAT_SEMANTIC_NOT_ALLOWED_HERE, "Subquery");

			if (node)
			  {

			    PT_NODE *order = node->info.query.order_by;
			    if (order && order->info.sort_spec.expr
				&& order->info.sort_spec.expr->node_type == PT_VALUE
				&& order->info.sort_spec.expr->type_enum == PT_TYPE_NULL)
			      {
				if (!node->info.query.q.select.group_by)
				  {
				    PT_ERRORm (this_parser, node, MSGCAT_SET_PARSER_SEMANTIC,
					       MSGCAT_SEMANTIC_ORDERBYNULL_REQUIRES_GROUPBY);
				  }
				else
				  {
				    parser_free_tree (this_parser, node->info.query.order_by);
				    node->info.query.order_by = NULL;
				  }
			      }
			  }

			parser_push_orderby_node (node);

		DBG_PRINT}}
	  opt_select_limit_clause
	  opt_for_update_clause
		{{ DBG_TRACE_GRAMMAR(select_expression_without_values_and_single_subquery, opt_select_limit_clause opt_for_update_clause);

			PT_NODE *node = parser_pop_orderby_node ();
			$<node>$ = node;
			PARSER_SAVE_ERR_CONTEXT ($<node>$, @$.buffer_pos)

		DBG_PRINT}}
	  table_op select_or_subquery_without_values_query_no_with_clause
		{{ DBG_TRACE_GRAMMAR(select_expression_without_values_and_single_subquery, table_op select_or_subquery_without_values_query_no_with_clause);

			PT_NODE *stmt = $8;
			PT_NODE *arg1 = $1;

			if (stmt)
			  {
			     stmt->info.query.id = (UINTPTR) stmt;
			     stmt->info.query.q.union_.arg1 = $1;
			     stmt->info.query.q.union_.arg2 = $9;
                             stmt->flag.recompile = $1->flag.recompile | $9->flag.recompile;

			     if (arg1 != NULL
				 && arg1->info.query.is_subquery != PT_IS_SUBQUERY
				 && arg1->info.query.order_by != NULL)
			       {
				 PT_ERRORm (this_parser, stmt, MSGCAT_SET_PARSER_SYNTAX,
					    MSGCAT_SYNTAX_INVALID_UNION_ORDERBY);
			       }
			  }


			$$ = stmt;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| select_or_nested_values_query
		{{ DBG_TRACE_GRAMMAR(select_expression_without_values_and_single_subquery, | select_or_nested_values_query);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

table_op
	: Union all_distinct
		{{ DBG_TRACE_GRAMMAR(table_op, : Union all_distinct);
			PT_MISC_TYPE isAll = $2;
			PT_NODE *node = parser_new_node (this_parser, PT_UNION);
			if (node)
			  {
			    if (isAll == PT_EMPTY)
			      isAll = PT_DISTINCT;
			    node->info.query.all_distinct = isAll;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| DIFFERENCE_ all_distinct
		{{ DBG_TRACE_GRAMMAR(table_op, | DIFFERENCE_ all_distinct);

			PT_MISC_TYPE isAll = $2;
			PT_NODE *node = parser_new_node (this_parser, PT_DIFFERENCE);
			if (node)
			  {
			    if (isAll == PT_EMPTY)
			      isAll = PT_DISTINCT;
			    node->info.query.all_distinct = isAll;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| EXCEPT all_distinct
		{{ DBG_TRACE_GRAMMAR(table_op, | EXCEPT all_distinct);

			PT_MISC_TYPE isAll = $2;
			PT_NODE *node = parser_new_node (this_parser, PT_DIFFERENCE);
			if (node)
			  {
			    if (isAll == PT_EMPTY)
			      isAll = PT_DISTINCT;
			    node->info.query.all_distinct = isAll;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| INTERSECTION all_distinct
		{{ DBG_TRACE_GRAMMAR(table_op, | INTERSECTION all_distinct);

			PT_MISC_TYPE isAll = $2;
			PT_NODE *node = parser_new_node (this_parser, PT_INTERSECTION);
			if (node)
			  {
			    if (isAll == PT_EMPTY)
			      isAll = PT_DISTINCT;
			    node->info.query.all_distinct = isAll;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| INTERSECT all_distinct
		{{ DBG_TRACE_GRAMMAR(table_op, | INTERSECT all_distinct);

			PT_MISC_TYPE isAll = $2;
			PT_NODE *node = parser_new_node (this_parser, PT_INTERSECTION);
			if (node)
			  {
			    if (isAll == PT_EMPTY)
			      isAll = PT_DISTINCT;
			    node->info.query.all_distinct = isAll;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

select_or_subquery
	: select_stmt
		{{ DBG_TRACE_GRAMMAR(select_or_subquery, : select_stmt);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| subquery
		{{ DBG_TRACE_GRAMMAR(select_or_subquery, | subquery);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| values_query
		{{ DBG_TRACE_GRAMMAR(select_or_subquery, | values_query);
			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
		DBG_PRINT}}
	;

select_or_values_query
	: values_query
		{{ DBG_TRACE_GRAMMAR(select_or_values_query, : values_query);
			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
		DBG_PRINT}}
	| select_stmt
		{{ DBG_TRACE_GRAMMAR(select_or_values_query, | select_stmt);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

select_or_subquery_without_values_query
	: select_stmt
		{{ DBG_TRACE_GRAMMAR(select_or_subquery_without_values_query, : select_stmt);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| subquery
		{{ DBG_TRACE_GRAMMAR(select_or_subquery_without_values_query, | subquery);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

select_or_subquery_without_values_query_no_with_clause
	: select_stmt
		{{ DBG_TRACE_GRAMMAR(select_or_subquery_without_values_query_no_with_clause, : select_stmt);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| subquery_without_subquery_and_with_clause
		{{ DBG_TRACE_GRAMMAR(select_or_subquery_without_values_query_no_with_clause, | subquery_without_subquery_and_with_clause);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

select_or_nested_values_query
	: select_stmt
		{{ DBG_TRACE_GRAMMAR(select_or_nested_values_query, : select_stmt);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| '(' values_query ')'
		{{ DBG_TRACE_GRAMMAR(select_or_nested_values_query, | '(' values_query ')');
			$$ = $2;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
		DBG_PRINT}}
	;

values_query
	: of_value_values
		{{ DBG_TRACE_GRAMMAR(values_query, : of_value_values);
			PT_NODE *node;
			parser_save_found_Oracle_outer ();
			if (parser_select_level >= 0)
			  parser_select_level++;
			parser_hidden_incr_list = NULL;

			node = parser_new_node (this_parser, PT_SELECT);

			if (node)
			  {
			    PT_SET_VALUE_QUERY(node); /* generated by "values" */
			    node->info.query.q.select.flavor = PT_USER_SELECT;
			    node->info.query.q.select.hint = PT_HINT_NONE;
			  }

			  parser_push_select_stmt_node (node);
			  parser_push_hint_node (node);
		DBG_PRINT}}
		values_expression
			{{ DBG_TRACE_GRAMMAR(values_query, values_expression);
				/* $3 node of type PT_NODE_LIST */
				PT_NODE *n;
				PT_NODE *node = parser_pop_select_stmt_node ();
				parser_found_Oracle_outer = false;
				parser_pop_hint_node ();

				if (node)
				  {
				    n = $3;
				    node->info.query.q.select.list = n;
				    node->info.query.into_list = NULL;
				    node->info.query.id = (UINTPTR)node;
				    node->info.query.all_distinct = PT_ALL;
				  }

				/* We don't want to allow subqueries for VALUES query */
				if (PT_IS_VALUE_QUERY (node))
				  {
				    PT_NODE *node_list, *attrs;

				    for (node_list = node->info.query.q.select.list; node_list;
					 node_list = node_list->next)
				      {
					/* node_list->node_type should be PT_NODE_LIST */
					for (attrs = node_list->info.node_list.list; attrs; attrs = attrs->next)
					  {
					    if (PT_IS_QUERY (attrs))
					      {
						PT_ERRORmf (this_parser, node, MSGCAT_SET_PARSER_SEMANTIC,
							    MSGCAT_SEMANTIC_NOT_ALLOWED_HERE, "Subquery");
						break;
					      }
					  }
				      }
				  }

				parser_restore_found_Oracle_outer ();
				if (parser_select_level >= 0)
				  parser_select_level--;

				$$ = node;
			DBG_PRINT}}
	;

values_expression
	: values_expression ',' values_expr_item
		{{ DBG_TRACE_GRAMMAR(values_expression, : values_expression ',' values_expr_item);
			PT_NODE *node1 = $1;
			PT_NODE *node2 = $3;
			parser_make_link (node1, node2);

			$$ = node1;
		DBG_PRINT}}
	| values_expr_item
		{{ DBG_TRACE_GRAMMAR(values_expression, | values_expr_item);
			$$ = $1;
		DBG_PRINT}}
	;

values_expr_item
	: '(' alias_enabled_expression_list_top ')'
		{{ DBG_TRACE_GRAMMAR(values_expr_item, : '(' alias_enabled_expression_list_top ')');
			PT_NODE *node_value = $2;
			PT_NODE *node_tmp = $2;
			PT_NODE *node = NULL;

			while (node_tmp)
			  {
			    PT_SET_VALUE_QUERY(node_tmp);
			    node_tmp = node_tmp->next;
			  }

			node = pt_node_list(this_parser, PT_IS_VALUE, node_value);
			PT_SET_VALUE_QUERY(node);

			$$ = node;
		DBG_PRINT}}
	;

select_stmt
	:
	SELECT			/* $1 */
		{{ DBG_TRACE_GRAMMAR(select_stmt, : SELECT);
				/* $2 */
			PT_NODE *node;
			parser_save_found_Oracle_outer ();
			if (parser_select_level >= 0)
			  parser_select_level++;
			parser_hidden_incr_list = NULL;

			node = parser_new_node (this_parser, PT_SELECT);

			if (node)
			  {
			    node->info.query.q.select.flavor = PT_USER_SELECT;
			    node->info.query.q.select.hint = PT_HINT_NONE;
			  }

			parser_push_select_stmt_node (node);
			parser_push_hint_node (node);

		DBG_PRINT}}
	opt_hint_list 		/* $3 */
	all_distinct            /* $4 */
	select_list 		/* $5 */
	opt_select_param_list	/* $6 */
		{{ DBG_TRACE_GRAMMAR(select_stmt, opt_hint_list all_distinct select_list opt_select_param_list);
				/* $7 */
			PT_MISC_TYPE isAll = $4;
			PT_NODE *node = parser_top_select_stmt_node ();
			if (node)
			  {
			    if (isAll == PT_EMPTY)
			      isAll = PT_ALL;
			    node->info.query.all_distinct = isAll;
			    node->info.query.q.select.list = $5;
			    node->info.query.into_list = $6;
			    if (parser_hidden_incr_list)
			      {
				(void) parser_append_node (parser_hidden_incr_list,
							   node->info.query.q.select.list);
				parser_hidden_incr_list = NULL;
			      }
			  }

		}}
	opt_from_clause  	/* $8 */
		{{ DBG_TRACE_GRAMMAR(select_stmt, opt_from_clause);
			$$ = $8;
		}}
	;

opt_with_clause
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_with_clause, : );

			$$ = NULL;

		DBG_PRINT}}
	| WITH            /* $1 */
	  opt_recursive   /* $2 */
	  cte_definition_list   /* $3 */
		{{ DBG_TRACE_GRAMMAR(opt_with_clause, | WITH opt_recursive cte_definition_list);

			PT_NODE *node = parser_new_node (this_parser, PT_WITH_CLAUSE);
			if (node)
			  {
			    PARSER_SAVE_ERR_CONTEXT (node, @$.buffer_pos)
			    node->info.with_clause.recursive = $2;
			    if ($3)
			      {
			        node->info.with_clause.cte_definition_list = $3;
			      }
			  }

			$$ = node;

		DBG_PRINT}}
	;

opt_recursive
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_recursive, : );
			$$ = 0;

		DBG_PRINT}}
	| RECURSIVE
		{{ DBG_TRACE_GRAMMAR(opt_recursive, | RECURSIVE);
			$$ = 1;

		DBG_PRINT}}
	;

cte_definition_list
	: cte_definition_list ',' cte_definition
		{{ DBG_TRACE_GRAMMAR(cte_definition_list, : cte_definition_list ',' cte_definition);

			$$ = parser_make_link($1, $3);
			PARSER_SAVE_ERR_CONTEXT($$, @$.buffer_pos)

		DBG_PRINT}}
	| cte_definition
		{{ DBG_TRACE_GRAMMAR(cte_definition_list, | cte_definition);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT($$, @$.buffer_pos)

		DBG_PRINT}}
  	;

cte_definition
	: identifier   /* $1 */
	opt_bracketed_identifier_list  /* $2 */
	AS               /* $3 */
	cte_query_list	/* $4 */
		{{ DBG_TRACE_GRAMMAR(cte_definition, : identifier opt_bracketed_identifier_list AS cte_query_list);
			PT_NODE *node = parser_new_node (this_parser, PT_CTE);
			if (node)
			  {
			    PARSER_SAVE_ERR_CONTEXT (node, @$.buffer_pos)
			    node->info.cte.name = $1;
			    if ($2)
			      {
			        node->info.cte.as_attr_list = $2;
			      }
			    node->info.cte.non_recursive_part = $4;
			    node->info.cte.recursive_part = NULL;
			  }

			$$ = node;

		DBG_PRINT}}
  	;

cte_query_list
	: cte_query_list
	table_op
	select_or_subquery
		{{ DBG_TRACE_GRAMMAR(cte_query_list, : cte_query_list table_op select_or_subquery);

			PT_NODE *stmt = $2;
			PT_NODE *arg1 = $1, *arg2 = $3;
			if (stmt)
			  {
			    stmt->info.query.id = (UINTPTR) stmt;
			    stmt->info.query.q.union_.arg1 = arg1;
		            stmt->info.query.q.union_.arg2 = arg2;
                            stmt->flag.recompile = arg1->flag.recompile | arg2->flag.recompile;
			  }

			$$ = stmt;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| select_or_subquery
		{{ DBG_TRACE_GRAMMAR(cte_query_list, | select_or_subquery);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT($$, @$.buffer_pos);

		DBG_PRINT}}
	;

opt_from_clause
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_from_clause, : );

			PT_NODE *n;
			PT_NODE *node = parser_pop_select_stmt_node ();
			parser_found_Oracle_outer = false;
			parser_pop_hint_node ();

			if (node)
			  {
			    PARSER_SAVE_ERR_CONTEXT (node, @$.buffer_pos)
			    n = node->info.query.q.select.list;
			    if (n && n->type_enum == PT_TYPE_STAR)
			      {
				/* "select *" is not valid, raise an error */
				PT_ERRORm (this_parser, node, MSGCAT_SET_PARSER_SEMANTIC,
					   MSGCAT_SEMANTIC_NO_TABLES_USED);
			      }

			    node->info.query.id = (UINTPTR) node;
			    node->info.query.all_distinct = PT_ALL;
			  }

			if (parser_hidden_incr_list)
			  {
			    /* if not handle hidden expressions, raise an error */
			    PT_ERRORf (this_parser, node,
				       "%s can be used at select or with increment clause only.",
				       pt_short_print (this_parser, parser_hidden_incr_list));
			  }

			parser_restore_found_Oracle_outer ();	/* restore */
			if (parser_select_level >= 0)
			  parser_select_level--;

			$$ = node;

		DBG_PRINT}}
	| FROM				/* $1 */
	  extended_table_spec_list	/* $2 */
		{{			/* $3 */
                    DBG_TRACE_GRAMMAR(opt_from_clause, | FROM extended_table_spec_list);
			parser_found_Oracle_outer = false;

		DBG_PRINT}}
	  opt_where_clause		/* $4 */
	  opt_startwith_connectby_clause/* $5 */
	  opt_groupby_clause		/* $6 */
	  opt_with_rollup		/* $7 */
	  opt_having_clause 		/* $8 */
	  opt_using_index_clause	/* $9 */
	  opt_with_increment_clause	/* $10 */
		{{ DBG_TRACE_GRAMMAR(opt_from_clause, opt_where_clause opt_startwith_connectby_clause ~ opt_with_increment_clause);

			PT_NODE *n;
			bool is_dummy_select;
			PT_NODE *node = parser_pop_select_stmt_node ();
			int with_rollup = $7;
			parser_pop_hint_node ();

			is_dummy_select = false;

			if (node)
			  {
			    PARSER_SAVE_ERR_CONTEXT (node, @$.buffer_pos)
			    n = node->info.query.q.select.list;
			    if (n && n->next == NULL && n->node_type == PT_VALUE
			        && n->type_enum == PT_TYPE_STAR)
			      {
				/* select * from ... */
				is_dummy_select = true;	/* Here, guess as TRUE */
			      }
			    else if (n && n->next == NULL && n->node_type == PT_NAME
				     && n->type_enum == PT_TYPE_STAR)
			      {
				/* select A.* from */
				is_dummy_select = true;	/* Here, guess as TRUE */
			      }
			    else
			      {
				is_dummy_select = false;	/* not dummy */
			      }

			    if (node->info.query.into_list)
			      {
				is_dummy_select = false;	/* not dummy */
			      }

			    node->info.query.q.select.from = n = CONTAINER_AT_0 ($2);
			    if (n && n->next)
			      is_dummy_select = false;	/* not dummy */
			    if (TO_NUMBER (CONTAINER_AT_1 ($2)) == 1)
			      {
				PT_SELECT_INFO_SET_FLAG (node, PT_SELECT_INFO_ANSI_JOIN);
			      }

                        // ctshim ================                        
#if 1        
                            pt_convert_dblink_query(node);
#endif
			    node->info.query.q.select.where = n = $4;
			    if (n)
			      is_dummy_select = false;	/* not dummy */
			    if (parser_found_Oracle_outer == true)
			      PT_SELECT_INFO_SET_FLAG (node, PT_SELECT_INFO_ORACLE_OUTER);

			    node->info.query.q.select.start_with = n = CONTAINER_AT_0 ($5);
			    if (n)
			      is_dummy_select = false;	/* not dummy */

			    node->info.query.q.select.connect_by = n = CONTAINER_AT_1 ($5);
			    if (n)
			      is_dummy_select = false;	/* not dummy */

			    node->info.query.q.select.group_by = n = $6;
			    if (n)
			      is_dummy_select = false;	/* not dummy */

			    if (with_rollup)
			      {
				if (!node->info.query.q.select.group_by)
				  {
				    PT_ERRORm (this_parser, node, MSGCAT_SET_PARSER_SEMANTIC,
					       MSGCAT_SEMANTIC_WITHROLLUP_REQUIRES_GROUPBY);
				  }
				else
				  {
				    node->info.query.q.select.group_by->flag.with_rollup = 1;
				  }
			      }

			    node->info.query.q.select.having = n = $8;
			    if (n)
			      is_dummy_select = false;	/* not dummy */

			    node->info.query.q.select.using_index =
			      (node->info.query.q.select.using_index ?
			       parser_make_link (node->info.query.q.select.using_index, $9) : $9);

			    node->info.query.q.select.with_increment = $10;
			    node->info.query.id = (UINTPTR) node;

                            if (node->info.query.all_distinct != PT_ALL)
			        is_dummy_select = false;	/* not dummy */
			  }

			if (is_dummy_select == true)
			  {
			    /* mark as dummy */
			    PT_SELECT_INFO_SET_FLAG (node, PT_SELECT_INFO_DUMMY);
			  }

			if (parser_hidden_incr_list)
			  {
			    /* if not handle hidden expressions, raise an error */
			    PT_ERRORf (this_parser, node,
				       "%s can be used at select or with increment clause only.",
				       pt_short_print (this_parser, parser_hidden_incr_list));
			  }

			parser_restore_found_Oracle_outer ();	/* restore */
			if (parser_select_level >= 0)
			  parser_select_level--;

			$$ = node;

		DBG_PRINT}}
	;

opt_select_param_list
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_select_param_list, : );

			$$ = NULL;

		DBG_PRINT}}
	| INTO to_param_list
		{{ DBG_TRACE_GRAMMAR(opt_select_param_list, | INTO to_param_list);

			$$ = $2;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| TO to_param_list
		{{ DBG_TRACE_GRAMMAR(opt_select_param_list, | TO to_param_list);

			$$ = $2;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

opt_hint_list
	: /* empty */
	| hint_list
	;

hint_list
	: hint_list CPP_STYLE_HINT
		{{ DBG_TRACE_GRAMMAR(hint_list, | hint_list CPP_STYLE_HINT);

			PT_NODE *node = parser_top_hint_node ();
			char *hint_comment = $2;
			(void) pt_get_hint (hint_comment, parser_hint_table, node);

		DBG_PRINT}}
	| hint_list SQL_STYLE_HINT
		{{ DBG_TRACE_GRAMMAR(hint_list, | hint_list SQL_STYLE_HINT);

			PT_NODE *node = parser_top_hint_node ();
			char *hint_comment = $2;
			(void) pt_get_hint (hint_comment, parser_hint_table, node);

		DBG_PRINT}}
	| hint_list C_STYLE_HINT
		{{ DBG_TRACE_GRAMMAR(hint_list, | hint_list C_STYLE_HINT);

			PT_NODE *node = parser_top_hint_node ();
			char *hint_comment = $2;
			(void) pt_get_hint (hint_comment, parser_hint_table, node);

		DBG_PRINT}}
	| CPP_STYLE_HINT
		{{ DBG_TRACE_GRAMMAR(hint_list, | CPP_STYLE_HINT);

			PT_NODE *node = parser_top_hint_node ();
			char *hint_comment = $1;
			(void) pt_get_hint (hint_comment, parser_hint_table, node);

		DBG_PRINT}}
	| SQL_STYLE_HINT
		{{ DBG_TRACE_GRAMMAR(hint_list, | SQL_STYLE_HINT);

			PT_NODE *node = parser_top_hint_node ();
			char *hint_comment = $1;
			(void) pt_get_hint (hint_comment, parser_hint_table, node);

		DBG_PRINT}}
	| C_STYLE_HINT
		{{ DBG_TRACE_GRAMMAR(hint_list, | C_STYLE_HINT);

			PT_NODE *node = parser_top_hint_node ();
			char *hint_comment = $1;
			(void) pt_get_hint (hint_comment, parser_hint_table, node);

		DBG_PRINT}}
	;

all_distinct
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(all_distinct, : );

			$$ = PT_EMPTY;

		DBG_PRINT}}
	| ALL
		{{ DBG_TRACE_GRAMMAR(all_distinct, | ALL);

			$$ = PT_ALL;

		DBG_PRINT}}
	| DISTINCT
		{{ DBG_TRACE_GRAMMAR(all_distinct, | DISTINCT);

			$$ = PT_DISTINCT;

		DBG_PRINT}}
	| UNIQUE
		{{ DBG_TRACE_GRAMMAR(all_distinct, | UNIQUE);

			$$ = PT_DISTINCT;

		DBG_PRINT}}
	;

select_list
	: '*'
		{{ DBG_TRACE_GRAMMAR(select_list,  : '*');

			PT_NODE *node = parser_new_node (this_parser, PT_VALUE);
			if (node)
			  node->type_enum = PT_TYPE_STAR;
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}

	| '*' ',' alias_enabled_expression_list_top
		{{ DBG_TRACE_GRAMMAR(select_list, | '*' ',' alias_enabled_expression_list_top);

			PT_NODE *node = parser_new_node (this_parser, PT_VALUE);
			if (node)
			  node->type_enum = PT_TYPE_STAR;

			$$ = parser_make_link (node, $3);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}

	| alias_enabled_expression_list_top
		{{ DBG_TRACE_GRAMMAR(select_list, | alias_enabled_expression_list_top);
			 $$ = $1;
			 PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
		DBG_PRINT}}
	;

alias_enabled_expression_list_top
	:
		{{ DBG_TRACE_GRAMMAR(alias_enabled_expression_list_top, : );

			parser_save_and_set_ic (2);
			parser_save_and_set_gc (2);
			parser_save_and_set_oc (2);
			parser_save_and_set_sysc (1);
			parser_save_and_set_prc (1);
			parser_save_and_set_cbrc (1);

		DBG_PRINT}}
	  alias_enabled_expression_list
		{{ DBG_TRACE_GRAMMAR(alias_enabled_expression_list_top, alias_enabled_expression_list);

			$$ = $2;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
			parser_restore_ic ();
			parser_restore_gc ();
			parser_restore_oc ();
			parser_restore_sysc ();
			parser_restore_prc ();
			parser_restore_cbrc ();

		DBG_PRINT}}
	;

alias_enabled_expression_list
	: alias_enabled_expression_list  ',' alias_enabled_expression_
		{{ DBG_TRACE_GRAMMAR(alias_enabled_expression_list, : alias_enabled_expression_list  ',' alias_enabled_expression_);

			$$ = parser_make_link ($1, $3);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| alias_enabled_expression_
		{{ DBG_TRACE_GRAMMAR(alias_enabled_expression_list, | alias_enabled_expression_);

			PT_NODE *node = $1;
			if (node != NULL)
			  {
			    node->flag.is_alias_enabled_expr = 1;
			  }
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

alias_enabled_expression_
	: normal_expression opt_as_identifier %dprec 2
		{{ DBG_TRACE_GRAMMAR(alias_enabled_expression_, : normal_expression opt_as_identifier);

			PT_NODE *subq, *id;
			PT_NODE *node = $1;
			if (node->node_type == PT_VALUE && node->type_enum == PT_TYPE_EXPR_SET)
			  {
			    node->type_enum = PT_TYPE_SEQUENCE;	/* for print out */
			    PT_ERRORf (this_parser, node,
				       "check syntax at %s, illegal parentheses set expression.",
				       pt_short_print (this_parser, node));
			  }
			else if (PT_IS_QUERY_NODE_TYPE (node->node_type))
			  {
			    /* mark as single tuple query */
			    node->info.query.flag.single_tuple = 1;

			    if ((subq = pt_get_subquery_list (node)) && subq->next)
			      {
				/* illegal multi-column subquery */
				PT_ERRORm (this_parser, node, MSGCAT_SET_PARSER_SEMANTIC,
					   MSGCAT_SEMANTIC_NOT_SINGLE_COL);
			      }
			  }


			id = $2;
			if (id && id->node_type == PT_NAME)
			  {
			    if (node->type_enum == PT_TYPE_STAR)
			      {
				PT_ERROR (this_parser, id,
					  "please check syntax after '*', expecting ',' or FROM in select statement.");
			      }
			    else
			      {
				node->alias_print = pt_makename (id->info.name.original);
				parser_free_node (this_parser, id);
			      }
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| predicate_expression opt_as_identifier %dprec 1
		{{ DBG_TRACE_GRAMMAR(alias_enabled_expression_, | predicate_expression opt_as_identifier);

			PT_NODE *id;
			PT_NODE *node = $1;

			id = $2;
			if (id && id->node_type == PT_NAME)
			  {
			    node->alias_print = pt_makename (id->info.name.original);
			    parser_free_node (this_parser, id);
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

expression_list
	: expression_queue
		{{ DBG_TRACE_GRAMMAR(expression_list, : expression_queue);

			$$ = CONTAINER_AT_0($1);

		DBG_PRINT}}
	;

expression_queue
	: expression_queue  ',' expression_
		{{ DBG_TRACE_GRAMMAR(expression_queue, : expression_queue  ',' expression_);
			container_2 new_q;

			PT_NODE* q_head = CONTAINER_AT_0($1);
			PT_NODE* q_tail = CONTAINER_AT_1($1);
			q_tail->next = $3;

			SET_CONTAINER_2(new_q, q_head, $3);
			$$ = new_q;
			PARSER_SAVE_ERR_CONTEXT (q_head, @$.buffer_pos)

		DBG_PRINT}}
	| expression_
		{{ DBG_TRACE_GRAMMAR(expression_queue, | expression_);
			container_2 new_q;

			SET_CONTAINER_2(new_q, $1, $1);
			$$ = new_q;
			PARSER_SAVE_ERR_CONTEXT ($1, @$.buffer_pos)

		DBG_PRINT}}
	;

to_param_list
	: to_param_list ',' to_param
		{{ DBG_TRACE_GRAMMAR(to_param_list, : to_param_list ',' to_param);

			$$ = parser_make_link ($1, $3);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| to_param
		{{ DBG_TRACE_GRAMMAR(to_param_list, | to_param);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

to_param
	: host_param_output
		{{ DBG_TRACE_GRAMMAR(to_param, : host_param_output);

			$1->info.host_var.var_type = PT_HOST_OUT;
			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| param_
		{{ DBG_TRACE_GRAMMAR(to_param, | param_);

			PT_NODE *val = $1;

			if (val)
			  {
			    val->info.name.meta_class = PT_PARAMETER;
			    val->info.name.spec_id = (UINTPTR) val;
			    val->info.name.resolved = pt_makename ("out parameter");
			  }

			$$ = val;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| identifier
		{{ DBG_TRACE_GRAMMAR(to_param, | identifier);

			PT_NODE *val = $1;

			if (val)
			  {
			    val->info.name.meta_class = PT_PARAMETER;
			    val->info.name.spec_id = (UINTPTR) val;
			    val->info.name.resolved = pt_makename ("out parameter");
			  }

			$$ = val;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

from_param
	: host_param_input
		{{ DBG_TRACE_GRAMMAR(from_param, : host_param_input);

			PT_NODE *val = $1;

			if (val)
			  {
			    val->info.name.meta_class = PT_PARAMETER;
			    val->data_type = parser_new_node (this_parser, PT_DATA_TYPE);
			  }

			$$ = val;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| param_
		{{ DBG_TRACE_GRAMMAR(from_param, | param_);

			PT_NODE *val = $1;

			if (val)
			  {
			    val->info.name.meta_class = PT_PARAMETER;
			    val->data_type = parser_new_node (this_parser, PT_DATA_TYPE);
			  }

			$$ = val;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	/*
	 * It must be changed to "CLASS class_name" by User Schema support.
	 * However, it is difficult to change to "CLASS class_name"
	 * because it is a syntax not supported by AS-IS.
	 * Even on AS-IS, PT_INTERNAL_ERROR (parser, "resolution") occurs on pt_resolve_object.
	 */
	| CLASS identifier
		{{ DBG_TRACE_GRAMMAR(from_param, | CLASS identifier);

			PT_NODE *val = $2;

			if (val)
			  {
			    val->info.name.meta_class = PT_META_CLASS;
			    val->data_type = parser_new_node (this_parser, PT_DATA_TYPE);
			  }

			$$ = val;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| identifier
		{{ DBG_TRACE_GRAMMAR(from_param, | identifier);

			PT_NODE *val = $1;

			if (val)
			  {
			    val->info.name.meta_class = PT_PARAMETER;
			    val->data_type = parser_new_node (this_parser, PT_DATA_TYPE);
			  }

			$$ = val;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;


host_param_input
	: '?'
		{{ DBG_TRACE_GRAMMAR(host_param_input, : '?');

			PT_NODE *node = parser_new_node (this_parser, PT_HOST_VAR);

			if (node)
			  {
				PARSER_SAVE_ERR_CONTEXT (node, @$.buffer_pos)
			    node->info.host_var.var_type = PT_HOST_IN;
			    node->info.host_var.str = pt_makename ("?");
			    node->info.host_var.index = parser_input_host_index++;
			  }
			if (parser_hostvar_check == 0)
			  {
			    PT_ERRORmf(this_parser, node, MSGCAT_SET_PARSER_SEMANTIC,
				       MSGCAT_SEMANTIC_NOT_ALLOWED_HERE, "host variable");
			  }

			$$ = node;

		DBG_PRINT}}
	| PARAM_HEADER uint_text
		{{ DBG_TRACE_GRAMMAR(host_param_input, | PARAM_HEADER uint_text);

			PT_NODE *node = parser_new_node (this_parser, PT_HOST_VAR);

			if (node)
			  {
				PARSER_SAVE_ERR_CONTEXT (node, @$.buffer_pos)
			    node->info.host_var.var_type = PT_HOST_IN;
			    node->info.host_var.str = pt_makename ("?");
			    node->info.host_var.index = atol ($2);
			    if (node->info.host_var.index >= parser_input_host_index)
			      {
				parser_input_host_index = node->info.host_var.index + 1;
			      }
			  }
			if (parser_hostvar_check == 0)
			  {
			    PT_ERRORmf(this_parser, node, MSGCAT_SET_PARSER_SEMANTIC,
				       MSGCAT_SEMANTIC_NOT_ALLOWED_HERE, "host variable");
			  }

			$$ = node;

		DBG_PRINT}}
	;

host_param_output
	: '?'
		{{ DBG_TRACE_GRAMMAR(host_param_output, : '?');

			PT_NODE *node = parser_new_node (this_parser, PT_HOST_VAR);

			if (node)
			  {
			    node->info.host_var.var_type = PT_HOST_IN;
			    node->info.host_var.str = pt_makename ("?");
			    node->info.host_var.index = parser_output_host_index++;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| PARAM_HEADER uint_text
		{{ DBG_TRACE_GRAMMAR(host_param_output, | PARAM_HEADER uint_text);

			PT_NODE *node = parser_new_node (this_parser, PT_HOST_VAR);

			if (parent_parser == NULL)
			  {
			    /* This syntax is only permitted at internal statement parsing */
			    PT_ERRORf (this_parser, node, "check syntax at %s",
				       parser_print_tree (this_parser, node));
			  }
			else if (node)
			  {
				PARSER_SAVE_ERR_CONTEXT (node, @$.buffer_pos)
			    node->info.host_var.var_type = PT_HOST_IN;
			    node->info.host_var.str = pt_makename ("?");
			    node->info.host_var.index = atol ($2);
			    if (node->info.host_var.index >= parser_output_host_index)
			      {
				parser_output_host_index = node->info.host_var.index + 1;
			      }
			  }

			$$ = node;

		DBG_PRINT}}
	;

param_
	: ':' identifier
		{{ DBG_TRACE_GRAMMAR(param_, : ':' identifier);

			$2->info.name.meta_class = PT_PARAMETER;
			$$ = $2;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

opt_where_clause
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_where_clause, : );

			$$ = NULL;

		DBG_PRINT}}
	|	{ DBG_TRACE_GRAMMAR(opt_where_clause, | );
			parser_save_and_set_ic (1);
			assert (parser_prior_check == 0);
			assert (parser_connectbyroot_check == 0);
			parser_save_and_set_sysc (1);
			parser_save_and_set_prc (1);
			parser_save_and_set_cbrc (1);
		}
	  WHERE search_condition
		{{ DBG_TRACE_GRAMMAR(opt_where_clause, WHERE search_condition);

			parser_restore_ic ();
			parser_restore_sysc ();
			parser_restore_prc ();
			parser_restore_cbrc ();
			$$ = $3;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

opt_startwith_connectby_clause
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_startwith_connectby_clause, : );

			container_2 ctn;
			SET_CONTAINER_2 (ctn, NULL, NULL);
			$$ = ctn;

		DBG_PRINT}}
	| startwith_clause connectby_clause
		{{ DBG_TRACE_GRAMMAR(opt_startwith_connectby_clause, |  startwith_clause connectby_clause);

			container_2 ctn;
			SET_CONTAINER_2 (ctn, $1, $2);
			$$ = ctn;

		DBG_PRINT}}
	| connectby_clause startwith_clause
		{{ DBG_TRACE_GRAMMAR(opt_startwith_connectby_clause, |  connectby_clause startwith_clause);

			container_2 ctn;
			SET_CONTAINER_2 (ctn, $2, $1);
			$$ = ctn;

		DBG_PRINT}}
	| connectby_clause
		{{ DBG_TRACE_GRAMMAR(opt_startwith_connectby_clause, |  connectby_clause);

			container_2 ctn;
			SET_CONTAINER_2 (ctn, NULL, $1);
			$$ = ctn;

		DBG_PRINT}}

startwith_clause
	:	{ DBG_TRACE_GRAMMAR(startwith_clause, : );
			parser_save_and_set_pseudoc (0);
		}
	  START_ WITH search_condition
		{{ DBG_TRACE_GRAMMAR(startwith_clause, START_ WITH search_condition);

			parser_restore_pseudoc ();
			$$ = $4;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

connectby_clause
	:	{ DBG_TRACE_GRAMMAR(connectby_clause, : );
			parser_save_and_set_prc (1);
			parser_save_and_set_serc (0);
			parser_save_and_set_pseudoc (1);
			parser_save_and_set_sqc (0);
		}
	  CONNECT BY opt_nocycle search_condition
		{{ DBG_TRACE_GRAMMAR(connectby_clause, CONNECT BY opt_nocycle search_condition);

			parser_restore_prc ();
			parser_restore_serc ();
			parser_restore_pseudoc ();
			parser_restore_sqc ();
			$$ = $5;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

opt_nocycle
	: /* empty */
	| NOCYCLE
		{{ DBG_TRACE_GRAMMAR(opt_nocycle, |  NOCYCLE);

			PT_NODE *node = parser_top_select_stmt_node ();
			if (node)
			  {
			    node->info.query.q.select.check_cycles =
			      CONNECT_BY_CYCLES_NONE;
			  }

		DBG_PRINT}}
	;

opt_groupby_clause
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_groupby_clause, : );

			$$ = NULL;

		DBG_PRINT}}
	| GROUP_ BY group_spec_list
		{{ DBG_TRACE_GRAMMAR(opt_groupby_clause, |  GROUP_ BY group_spec_list);

			$$ = $3;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

opt_with_rollup
	: /*empty*/
		{{ DBG_TRACE_GRAMMAR(opt_with_rollup, : );

			$$ = 0;

		DBG_PRINT}}
	| WITH ROLLUP
		{{ DBG_TRACE_GRAMMAR(opt_with_rollup, |  WITH ROLLUP);

			$$ = 1;

		DBG_PRINT}}
	;

group_spec_list
	: group_spec_list ',' group_spec
		{{ DBG_TRACE_GRAMMAR(group_spec_list, : group_spec_list ',' group_spec);

			$$ = parser_make_link ($1, $3);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| group_spec
		{{ DBG_TRACE_GRAMMAR(group_spec_list, |  group_spec);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;


// SR +3
group_spec
	:
		{ DBG_TRACE_GRAMMAR(group_spec, : );
			parser_groupby_exception = 0;
		}
	  expression_
	  opt_asc_or_desc 	  /* $3 */
		{{ DBG_TRACE_GRAMMAR(group_spec, expression_ opt_asc_or_desc);

			PT_NODE *node = parser_new_node (this_parser, PT_SORT_SPEC);

			PARSER_SAVE_ERR_CONTEXT (node, @$.buffer_pos)

			switch (parser_groupby_exception)
			  {
			  case PT_COUNT:
			  case PT_OID_ATTR:
			  case PT_INST_NUM:
			  case PT_ORDERBY_NUM:
			  case PT_ROWNUM:
			    PT_ERROR (this_parser, node,
				      "expression is not allowed as group by spec");
			    break;

			  case PT_IS_SUBINSERT:
			    PT_ERROR (this_parser, node,
				      "insert expression is not allowed as group by spec");
			    break;

			  case PT_IS_SUBQUERY:
			    PT_ERROR (this_parser, node,
				      "subquery is not allowed to group as spec");
			    break;

			  case PT_EXPR:
			    PT_ERROR (this_parser, node,
				      "search condition is not allowed as group by spec");
			    break;
			  }

			if (node)
			  {
			    node->info.sort_spec.asc_or_desc = PT_ASC;
			    node->info.sort_spec.expr = $2;
			    if ($3)
			      {
				node->info.sort_spec.asc_or_desc = PT_DESC;
			      }
			  }

			$$ = node;

		DBG_PRINT}}
	;


opt_having_clause
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_having_clause, : );

			$$ = NULL;

		DBG_PRINT}}
	|	{ parser_save_and_set_gc(1); }
	  HAVING search_condition
		{{ DBG_TRACE_GRAMMAR(opt_having_clause, | HAVING search_condition);

			parser_restore_gc ();
			$$ = $3;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

opt_using_index_clause
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_using_index_clause, : );

			$$ = NULL;

		DBG_PRINT}}
	| USING INDEX index_hint_name_keylimit_list
		{{ DBG_TRACE_GRAMMAR(opt_using_index_clause, | USING INDEX index_hint_name_keylimit_list);

			$$ = $3;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| USING INDEX NONE
		{{ DBG_TRACE_GRAMMAR(opt_using_index_clause, | USING INDEX NONE);

			PT_NODE *node = parser_new_node (this_parser, PT_NAME);

			if (node)
			  {
			    node->info.name.original = NULL;
			    node->info.name.resolved = NULL;
			    node->info.name.meta_class = PT_INDEX_NAME;
			    node->etc = (void *) PT_IDX_HINT_NONE;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| USING INDEX ALL EXCEPT index_hint_name_list
		{{ DBG_TRACE_GRAMMAR(opt_using_index_clause, | USING INDEX ALL EXCEPT index_hint_name_list);
			PT_NODE *curr;
			PT_NODE *node = parser_new_node (this_parser, PT_NAME);

			if (node)
			  {
			    node->info.name.original = NULL;
			    node->info.name.resolved = "*";
			    node->info.name.meta_class = PT_INDEX_NAME;
			    node->etc = (void *) PT_IDX_HINT_ALL_EXCEPT;
			  }

			node->next = $5;
			curr = node;
			for (curr = node; curr; curr = curr->next)
			  {
			    if (curr->etc == (void *) PT_IDX_HINT_FORCE)
			      {
				PT_ERRORmf(this_parser, curr, MSGCAT_SET_PARSER_SEMANTIC,
					   MSGCAT_SEMANTIC_NOT_ALLOWED_HERE, "(+)");
				break;
			      }
			    else if (curr->etc == (void *) PT_IDX_HINT_IGNORE)
			      {
				PT_ERRORmf(this_parser, curr, MSGCAT_SET_PARSER_SEMANTIC,
					   MSGCAT_SEMANTIC_NOT_ALLOWED_HERE, "(-)");
				break;
			      }
			    curr->etc = (void *) PT_IDX_HINT_ALL_EXCEPT;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

index_hint_name_keylimit_list
	: index_hint_name_keylimit_list ',' index_hint_name_keylimit
		{{ DBG_TRACE_GRAMMAR(index_hint_name_keylimit_list, : index_hint_name_keylimit_list ',' index_hint_name_keylimit);

			$$ = parser_make_link ($1, $3);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| index_hint_name_keylimit
		{{ DBG_TRACE_GRAMMAR(index_hint_name_keylimit_list, | index_hint_name_keylimit);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

index_hint_name_list
	: index_hint_name_list ',' index_hint_name
		{{ DBG_TRACE_GRAMMAR(index_name_list, : index_hint_name_list ',' index_hint_name);

			$$ = parser_make_link ($1, $3);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| index_hint_name
		{{ DBG_TRACE_GRAMMAR(index_hint_name_list, | index_hint_name);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

index_hint_name_keylimit
	: index_hint_name KEYLIMIT limit_expr
		{{ DBG_TRACE_GRAMMAR(index_hint_name_keylimit, : index_hint_name KEYLIMIT limit_expr);

			PT_NODE *node = $1;
			PARSER_SAVE_ERR_CONTEXT (node, @$.buffer_pos)

			if (node)
			  {
			    if (node->etc == (void *) PT_IDX_HINT_NONE
				|| node->etc == (void *) PT_IDX_HINT_CLASS_NONE)
			      {
				PT_ERRORm(this_parser, node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_KEYLIMIT_INDEX_NONE);
			      }
			    else if (node->etc == (void *) PT_IDX_HINT_IGNORE)
			      {
				PT_ERRORmf(this_parser, node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_NOT_ALLOWED_HERE, "KEYLIMIT");
			      }
			    else
			      {
				/* set key limit */
				node->info.name.indx_key_limit = $3;
			      }
			  }
			$$ = node;

		DBG_PRINT}}
	| index_hint_name KEYLIMIT limit_expr ',' limit_expr
		{{ DBG_TRACE_GRAMMAR(index_hint_name_keylimit, | index_hint_name KEYLIMIT limit_expr ',' limit_expr);

			PT_NODE *node = $1;
			if (node)
			  {
			    if (node->etc == (void *) PT_IDX_HINT_NONE
				|| node->etc == (void *) PT_IDX_HINT_CLASS_NONE)
			      {
				PT_ERRORm(this_parser, node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_KEYLIMIT_INDEX_NONE);
			      }
			    else
			      {
				/* set key limits */
				node->info.name.indx_key_limit = $5;
				if ($5)
				  {
				    ($5)->next = $3;
				  }
			      }
			  }
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| index_hint_name
		{{ DBG_TRACE_GRAMMAR(index_hint_name_keylimit, | index_hint_name);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

index_hint_name
	: index_name paren_plus
		{{ DBG_TRACE_GRAMMAR(index_hint_name, : index_name paren_plus);

			PT_NODE *node = $1;
			node->etc = (void *) PT_IDX_HINT_FORCE;
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| index_name paren_minus
		{{ DBG_TRACE_GRAMMAR(index_name, | index_name paren_minus);

			PT_NODE *node = $1;
			node->etc = (void *) PT_IDX_HINT_IGNORE;
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| index_name
		{{ DBG_TRACE_GRAMMAR(index_hint_name, | index_name);

			PT_NODE *node = $1;
			node->etc = (void *) PT_IDX_HINT_USE;
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| identifier DOT NONE
		{{ DBG_TRACE_GRAMMAR(index_name, | identifier DOT NONE);

			PT_NODE *node = $1;
			node->info.name.meta_class = PT_INDEX_NAME;
			node->info.name.resolved = node->info.name.original;
			node->info.name.original = NULL;
			node->etc = (void *) PT_IDX_HINT_CLASS_NONE;
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

opt_with_increment_clause
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_with_increment_clause, : );

			$$ = NULL;

		DBG_PRINT}}
	| WITH INCREMENT For incr_arg_name_list__inc
		{{ DBG_TRACE_GRAMMAR(opt_with_increment_clause, | WITH INCREMENT For incr_arg_name_list__inc);

			$$ = $4;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| WITH DECREMENT For incr_arg_name_list__dec
		{{ DBG_TRACE_GRAMMAR(opt_with_increment_clause, | WITH DECREMENT For incr_arg_name_list__dec);

			$$ = $4;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

opt_for_update_clause
	: /* empty */ { DBG_TRACE_GRAMMAR(opt_for_update_clause, : ); }
	| For UPDATE
		{{ DBG_TRACE_GRAMMAR(opt_for_update_clause, | For UPDATE);

			PT_NODE *node = parser_top_orderby_node ();

			if (node != NULL && node->node_type == PT_SELECT)
			  {
			    PT_SELECT_INFO_SET_FLAG(node, PT_SELECT_INFO_FOR_UPDATE);
			  }
			else
			  {
			    PT_ERRORm (this_parser, node,
				       MSGCAT_SET_PARSER_SEMANTIC,
				       MSGCAT_SEMANTIC_INVALID_USE_FOR_UPDATE_CLAUSE);
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| For UPDATE OF class_name_list
		{{ DBG_TRACE_GRAMMAR(opt_for_update_clause, | For UPDATE OF class_name_list);

			PT_NODE *node = parser_top_orderby_node ();
			PT_NODE *names_list = $4;
			PT_NODE *node1 = names_list, *node2 = NULL;

			if (node != NULL && node->node_type == PT_SELECT)
			  {
			    PT_SELECT_INFO_SET_FLAG(node, PT_SELECT_INFO_FOR_UPDATE);
			    node->info.query.q.select.for_update = names_list;
			    for (; node1 != NULL && node2 == NULL;  node1 = node1->next)
			      {
				for (node2 = node1->next; node2 != NULL; node2 = node2->next)
				  {
				    /* check if search is duplicate of table */
				    if (!pt_str_compare (node1->info.name.original,
					 node2->info.name.original, CASE_INSENSITIVE))
				      {
					/* same class found twice in table list */
					PT_ERRORmf (this_parser, node2,
						    MSGCAT_SET_PARSER_SEMANTIC,
						    MSGCAT_SEMANTIC_DUPLICATE_CLASS_OR_ALIAS,
						    node2->info.name.original);
						    break;
				      }
				  }
				  PT_NAME_INFO_SET_FLAG(node1, PT_NAME_FOR_UPDATE);
			      }
			  }
			else
			  {
			    PT_ERRORm (this_parser, node,
					MSGCAT_SET_PARSER_SEMANTIC,
					MSGCAT_SEMANTIC_INVALID_USE_FOR_UPDATE_CLAUSE);
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

incr_arg_name_list__inc
	: incr_arg_name_list__inc ',' incr_arg_name__inc
		{{ DBG_TRACE_GRAMMAR(incr_arg_name_list__inc, : incr_arg_name_list__inc ',' incr_arg_name__inc);

			$$ = parser_make_link ($1, $3);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| incr_arg_name__inc
		{{ DBG_TRACE_GRAMMAR(incr_arg_name_list__inc, | incr_arg_name__inc);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

incr_arg_name__inc
	: path_expression
		{{ DBG_TRACE_GRAMMAR(incr_arg_name__inc, : path_expression);
			PT_NODE *node = $1;

			if (node->node_type == PT_EXPR && node->info.expr.op == PT_INCR)
			  {
			    /* do nothing */
			  }
			else if (node->node_type == PT_EXPR && node->info.expr.op == PT_DECR)
			  {
			    PT_ERRORf2 (this_parser, node, "%s can be used at 'with %s for'.",
					pt_short_print (this_parser, node), "increment");
			  }
			else
			  {
			    node = parser_make_expression (this_parser, PT_INCR, $1, NULL, NULL);
                            node->flag.is_hidden_column = 1;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
		DBG_PRINT}}
	;

incr_arg_name_list__dec
	: incr_arg_name_list__dec ',' incr_arg_name__dec
		{{ DBG_TRACE_GRAMMAR(incr_arg_name_list__dec, : incr_arg_name_list__dec ',' incr_arg_name__dec);

			$$ = parser_make_link ($1, $3);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| incr_arg_name__dec
		{{ DBG_TRACE_GRAMMAR(incr_arg_name_list__dec, | incr_arg_name__dec);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

incr_arg_name__dec
	: path_expression
		{{ DBG_TRACE_GRAMMAR(incr_arg_name__dec, : path_expression);
			PT_NODE *node = $1;

			if (node->node_type == PT_EXPR && node->info.expr.op == PT_INCR)
			  {
			    PT_ERRORf2 (this_parser, node, "%s can be used at 'with %s for'.",
					pt_short_print (this_parser, node), "increment");
			  }
			else if (node->node_type == PT_EXPR && node->info.expr.op == PT_DECR)
			  {
			    /* do nothing */
			  }
			else
			  {
			    node = parser_make_expression (this_parser, PT_DECR, $1, NULL, NULL);
                            node->flag.is_hidden_column = 1;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
		DBG_PRINT}}
	;

opt_update_orderby_clause
	: /* empty */
		{ DBG_TRACE_GRAMMAR(opt_update_orderby_clause, : );
                  $$ = NULL; }
	| ORDER BY
	  sort_spec_list
		{  parser_save_and_set_oc (1); }
	  opt_for_search_condition
		{{ DBG_TRACE_GRAMMAR(opt_update_orderby_clause, | ORDER BY sort_spec_listopt_for_search_condition);
			PT_NODE *stmt = parser_pop_orderby_node ();

			parser_restore_oc ();

			stmt->info.update.order_by = $3;
			stmt->info.update.orderby_for = $5;

			$$ = stmt;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
		DBG_PRINT}}
	;

opt_orderby_clause
	: /* empty */
		{ DBG_TRACE_GRAMMAR(opt_orderby_clause, : );  $$ = NULL; }
	| ORDER
	  opt_siblings
	  BY
		{{ DBG_TRACE_GRAMMAR("opt_orderby_clause", "| ORDER opt_siblings BY");
			PT_NODE *stmt = parser_top_orderby_node ();

			if (!stmt->info.query.flag.order_siblings)
			  {
				parser_save_and_set_sysc (1);
				parser_save_and_set_prc (1);
				parser_save_and_set_cbrc (1);
				parser_save_and_set_pseudoc (1);
			  }
			else
			  {
				parser_save_and_set_sysc (0);
				parser_save_and_set_prc (0);
				parser_save_and_set_cbrc (0);
				parser_save_and_set_pseudoc (0);
			  }

			if (stmt && !stmt->info.query.q.select.from)
			    PT_ERRORmf(this_parser, pt_top(this_parser),
				MSGCAT_SET_PARSER_SEMANTIC,
				MSGCAT_SEMANTIC_NOT_ALLOWED_HERE, "ORDER BY");

		DBG_PRINT}}
	  sort_spec_list
		{{ DBG_TRACE_GRAMMAR(opt_orderby_clause, sort_spec_list);

			PT_NODE *stmt = parser_top_orderby_node ();

			parser_restore_sysc ();
			parser_restore_prc ();
			parser_restore_cbrc ();
			parser_restore_pseudoc ();
			parser_save_and_set_oc (1);

		DBG_PRINT}}
	  opt_for_search_condition
		{{ DBG_TRACE_GRAMMAR(opt_orderby_clause, opt_for_search_condition);

			PT_NODE *col, *order, *n, *temp, *list = NULL;
			PT_NODE *stmt = parser_top_orderby_node ();
			bool found_star;
			int index_of_col;
			char *n_str, *c_str;
			bool is_col, is_alias;
			unsigned int save_custom;

			parser_restore_oc ();
			if (stmt)
			  {
			    stmt->info.query.orderby_for = $7;
			    /* support for alias in FOR */
			    n = stmt->info.query.orderby_for;
			    while (n)
			      {
				resolve_alias_in_expr_node (n, stmt->info.query.q.select.list);
				n = n->next;
			      }
			  }

			if (stmt)
			  {
			    stmt->info.query.order_by = order = $5;
			    if (order)
			      {				/* not dummy */
				PT_SELECT_INFO_CLEAR_FLAG (stmt, PT_SELECT_INFO_DUMMY);
				if (pt_is_query (stmt))
				  {
				    /* UNION, INTERSECT, DIFFERENCE, SELECT */
				    temp = stmt;
				    while (temp)
				      {
					switch (temp->node_type)
					  {
					  case PT_SELECT:
					    goto start_check;
					    break;
					  case PT_UNION:
					  case PT_INTERSECTION:
					  case PT_DIFFERENCE:
					    temp = temp->info.query.q.union_.arg1;
					    break;
					  default:
					    temp = NULL;
					    break;
					  }
				      }

				  start_check:
				    if (temp)
				      {
				        list = temp->info.query.q.select.list;
				      }
				    found_star = false;

				    if (list && list->node_type == PT_VALUE
					&& list->type_enum == PT_TYPE_STAR)
				      {
					/* found "*" */
					found_star = true;
				      }
				    else
				      {
					for (col = list; col; col = col->next)
					  {
					    if (col->node_type == PT_NAME
						&& col->type_enum == PT_TYPE_STAR)
					      {
						/* found "classname.*" */
						found_star = true;
						break;
					      }
					  }
				      }

				    for (; order; order = order->next)
				      {
					is_alias = false;
					is_col = false;

					n = order->info.sort_spec.expr;
					if (n == NULL)
					  {
					    break;
					  }

					if (n->node_type == PT_VALUE)
					  {
					    continue;
					  }

					save_custom = this_parser->custom_print;
					this_parser->custom_print |= PT_SUPPRESS_QUOTES;
					n_str = parser_print_tree (this_parser, n);
					this_parser->custom_print = save_custom;
					if (n_str == NULL)
					  {
					    continue;
					  }

					for (col = list, index_of_col = 1; col;
					     col = col->next, index_of_col++)
					  {
					    save_custom = this_parser->custom_print;
					    this_parser->custom_print |= PT_SUPPRESS_QUOTES;
					    c_str = parser_print_tree (this_parser, col);
					    this_parser->custom_print = save_custom;
					    if (c_str == NULL)
					      {
					        continue;
					      }

					    if ((col->alias_print
						 && intl_identifier_casecmp (n_str, col->alias_print) == 0
						 && (is_alias = true))
						|| (intl_identifier_casecmp (n_str, c_str) == 0
						    && (is_col = true)))
					      {
						if (found_star)
						  {
						    temp = parser_copy_tree (this_parser, col);
						    temp->next = NULL;
						  }
						else
						  {
						    temp = parser_new_node (this_parser, PT_VALUE);
						    if (temp == NULL)
						      {
						        break;
						      }

						    temp->type_enum = PT_TYPE_INTEGER;
						    temp->info.value.data_value.i = index_of_col;
						  }

						parser_free_node (this_parser, order->info.sort_spec.expr);
						order->info.sort_spec.expr = temp;

						if (is_col == true && is_alias == true)
						  {
						    /* alias/col name ambiguity, raise error */
						    PT_ERRORmf (this_parser, order, MSGCAT_SET_PARSER_SEMANTIC,
								MSGCAT_SEMANTIC_AMBIGUOUS_COLUMN_IN_ORDERING,
								n_str);
						    break;
						  }
					      }
					  }
				      }
				  }
			      }
			  }

			$$ = stmt;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

opt_siblings
	: /* empty */
	| SIBLINGS
		{{ DBG_TRACE_GRAMMAR(opt_siblings, | SIBLINGS);

			PT_NODE *stmt = parser_top_orderby_node ();
			stmt->info.query.flag.order_siblings = true;
			if (stmt->info.query.q.select.connect_by == NULL)
			    {
				PT_ERRORmf(this_parser, stmt,
				    MSGCAT_SET_PARSER_SEMANTIC,
				    MSGCAT_SEMANTIC_NOT_HIERACHICAL_QUERY,
				    "SIBLINGS");
			    }

		DBG_PRINT}}
	;

limit_expr
        : limit_expr '+' limit_term
                {{ DBG_TRACE_GRAMMAR(limit_expr, : limit_expr '+' limit_term);
                        $$ = parser_make_expression (this_parser, PT_PLUS, $1, $3, NULL);
                        PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

                DBG_PRINT}}
        | limit_expr '-' limit_term
                {{ DBG_TRACE_GRAMMAR(limit_expr, | limit_expr '-' limit_term);
                        $$ = parser_make_expression (this_parser, PT_MINUS, $1, $3, NULL);
                        PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

                DBG_PRINT}}
        | limit_term
                {{ DBG_TRACE_GRAMMAR(limit_expr, | limit_term);
                        $$ = $1;
                        PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

                DBG_PRINT}}
        ;

limit_term
        : limit_term '*' limit_factor
                {{ DBG_TRACE_GRAMMAR(limit_term, : limit_term '*' limit_factor);
                        $$ = parser_make_expression (this_parser, PT_TIMES, $1, $3, NULL);
                        PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

                DBG_PRINT}}
        | limit_term '/' limit_factor
                {{ DBG_TRACE_GRAMMAR(limit_term, | limit_term '/' limit_factor);
                        $$ = parser_make_expression (this_parser, PT_DIVIDE, $1, $3, NULL);
                        PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

                DBG_PRINT}}
        | limit_factor
                {{ DBG_TRACE_GRAMMAR(limit_term, | limit_factor);
                        $$ = $1;
                        PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

                DBG_PRINT}}
          ;

limit_factor
        : host_param_input
                {{ DBG_TRACE_GRAMMAR(limit_factor, : host_param_input);

                        $$ = $1;
                        PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

                DBG_PRINT}}
        | unsigned_integer
                {{ DBG_TRACE_GRAMMAR(limit_factor, | unsigned_integer);

                        $$ = $1;
                        PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

                DBG_PRINT}}

        | '(' limit_expr ')'
                {{ DBG_TRACE_GRAMMAR(limit_factor, | '(' limit_expr ')');
			PT_NODE *exp = $2;

			if (exp && exp->node_type == PT_EXPR)
			  {
			    exp->info.expr.paren_type = 1;
			  }

                        $$ = exp;
                        PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

                DBG_PRINT}}
        ;

opt_select_limit_clause
	: /* empty */
	| LIMIT limit_options
	        {{ DBG_TRACE_GRAMMAR(opt_select_limit_clause, | LIMIT limit_options);

			PT_NODE *node = $2;
			if (node)
			  {
			    if (node->node_type == PT_SELECT)
			      {
				if (!node->info.query.q.select.from)
				  {
				    PT_ERRORm (this_parser, node, MSGCAT_SET_PARSER_SEMANTIC,
				    	   MSGCAT_SEMANTIC_NO_TABLES_USED);
				  }

				/* not dummy */
				PT_SELECT_INFO_CLEAR_FLAG (node, PT_SELECT_INFO_DUMMY);

				/* For queries that have LIMIT clause don't allow
				 * inst_num, groupby_num, orderby_num in where, having, for
				 * respectively.
				 */
				if (node->info.query.q.select.having)
				  {
				    bool grbynum_flag = false;
				    (void) parser_walk_tree (this_parser, node->info.query.q.select.having,
				    			 pt_check_groupbynum_pre, NULL,
				    			 pt_check_groupbynum_post, &grbynum_flag);
				    if (grbynum_flag)
				      {
				        PT_ERRORmf(this_parser, node->info.query.q.select.having,
				    	       MSGCAT_SET_PARSER_SEMANTIC,
				    	       MSGCAT_SEMANTIC_NOT_ALLOWED_IN_LIMIT_CLAUSE, "GROUPBY_NUM()");
				      }
				  }
				if (node->info.query.q.select.where)
				  {
				    bool instnum_flag = false;
				    (void) parser_walk_tree (this_parser, node->info.query.q.select.where,
				    			 pt_check_instnum_pre, NULL,
				    			 pt_check_instnum_post, &instnum_flag);
				    if (instnum_flag)
				      {
				        PT_ERRORmf(this_parser, node->info.query.q.select.where,
				    	       MSGCAT_SET_PARSER_SEMANTIC,
				    	       MSGCAT_SEMANTIC_NOT_ALLOWED_IN_LIMIT_CLAUSE, "INST_NUM()/ROWNUM");
				      }
				  }
			      }

			    if (node->info.query.orderby_for)
			      {
				bool ordbynum_flag = false;
				(void) parser_walk_tree (this_parser, node->info.query.orderby_for,
							 pt_check_orderbynum_pre, NULL,
							 pt_check_orderbynum_post, &ordbynum_flag);
				if (ordbynum_flag)
				  {
				    PT_ERRORmf(this_parser, node->info.query.orderby_for,
					       MSGCAT_SET_PARSER_SEMANTIC,
					       MSGCAT_SEMANTIC_NOT_ALLOWED_IN_LIMIT_CLAUSE, "ORDERBY_NUM()");
				  }
			      }
			  }
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

limit_options
	: limit_expr
		{{ DBG_TRACE_GRAMMAR(limit_options, : limit_expr);

			PT_NODE *node = parser_top_orderby_node ();
			if (node)
			  {
			    node->info.query.limit = $1;
			    node->info.query.flag.rewrite_limit = 1;
			  }
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| limit_expr ',' limit_expr
		{{ DBG_TRACE_GRAMMAR(limit_options, | limit_expr ',' limit_expr);

			PT_NODE *node = parser_top_orderby_node ();
			if (node)
			  {
			    PT_NODE *limit1 = $1;
			    PT_NODE *limit2 = $3;
			    if (limit1)
			      {
				limit1->next = limit2;
			      }
			    node->info.query.limit = limit1;
			    node->info.query.flag.rewrite_limit = 1;
			  }
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| limit_expr OFFSET limit_expr
		{{ DBG_TRACE_GRAMMAR(limit_options, | limit_expr OFFSET limit_expr);

			PT_NODE *node = parser_top_orderby_node ();
			if (node)
			  {
			    PT_NODE *limit1 = $3;
			    PT_NODE *limit2 = $1;
			    if (limit1)
			      {
				limit1->next = limit2;
			      }
			    node->info.query.limit = limit1;
			    node->info.query.flag.rewrite_limit = 1;
			  }
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

opt_upd_del_limit_clause
	: /* empty */
		{ DBG_TRACE_GRAMMAR(opt_upd_del_limit_clause, : );
                  $$ = NULL; }
	| LIMIT limit_expr
		{{ DBG_TRACE_GRAMMAR(opt_upd_del_limit_clause, | LIMIT limit_expr);

			  $$ = $2;
			  PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

opt_for_search_condition
	: /* empty */
		{ DBG_TRACE_GRAMMAR(opt_for_search_condition, : );
                  $$ = NULL; }
	| For search_condition
		{{ DBG_TRACE_GRAMMAR(opt_for_search_condition, | For search_condition);
			PT_NODE *node = $2;
			bool subquery_flag = false;
			if (node)
			  {
			    (void) parser_walk_tree (this_parser, node,
						     pt_check_subquery_pre, NULL,
						     pt_check_subquery_post,
						     &subquery_flag);
			    if (subquery_flag)
			      {
				PT_ERRORm(this_parser, node,
					  MSGCAT_SET_PARSER_SEMANTIC,
					  MSGCAT_SEMANTIC_SUBQUERY_NOT_ALLOWED_IN_ORDERBY_FOR_CLAUSE);
			     }
			  }
			$$ = $2;
		DBG_PRINT}}
	;

sort_spec_list
	: sort_spec_list ',' sort_spec
		{{ DBG_TRACE_GRAMMAR(sort_spec_list, : sort_spec_list ',' sort_spec);

			$$ = parser_make_link ($1, $3);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| sort_spec
		{{ DBG_TRACE_GRAMMAR(sort_spec_list, | sort_spec);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

sort_spec
	: expression_ ASC opt_nulls_first_or_last
		{{ DBG_TRACE_GRAMMAR(sort_spec, : expression_ ASC opt_nulls_first_or_last);
			PT_NODE *node = parser_new_node (this_parser, PT_SORT_SPEC);

			if (node)
			  {
			    node->info.sort_spec.asc_or_desc = PT_ASC;
			    node->info.sort_spec.expr = $1;
			    node->info.sort_spec.nulls_first_or_last = $3;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| expression_ DESC opt_nulls_first_or_last
		{{ DBG_TRACE_GRAMMAR(sort_spec, | expression_ DESC opt_nulls_first_or_last);

			PT_NODE *node = parser_new_node (this_parser, PT_SORT_SPEC);

			if (node)
			  {
			    node->info.sort_spec.asc_or_desc = PT_DESC;
			    node->info.sort_spec.expr = $1;
			    node->info.sort_spec.nulls_first_or_last = $3;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| expression_ opt_nulls_first_or_last
		{{ DBG_TRACE_GRAMMAR(sort_spec, | expression_ opt_nulls_first_or_last);

			PT_NODE *node = parser_new_node (this_parser, PT_SORT_SPEC);

			if (node)
			  {
			    node->info.sort_spec.asc_or_desc = PT_ASC;
			    node->info.sort_spec.expr = $1;
			    node->info.sort_spec.nulls_first_or_last = $2;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

opt_nulls_first_or_last
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_nulls_first_or_last, : );

			$$ = PT_NULLS_DEFAULT;

		DBG_PRINT}}
	| NULLS FIRST
		{{ DBG_TRACE_GRAMMAR(opt_nulls_first_or_last, | NULLS FIRST);

			$$ = PT_NULLS_FIRST;

		DBG_PRINT}}
	| NULLS LAST
		{{ DBG_TRACE_GRAMMAR(opt_nulls_first_or_last, | NULLS LAST);

			$$ = PT_NULLS_LAST;

		DBG_PRINT}}
	;

expression_
	: normal_expression
		{{DBG_TRACE_GRAMMAR(expression_, : normal_expression);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| predicate_expression
		{{DBG_TRACE_GRAMMAR(expression_, | predicate_expression);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;


normal_expression
	: session_variable_definition
		{{ DBG_TRACE_GRAMMAR(normal_expression, : session_variable_definition);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| expression_strcat
		{{ DBG_TRACE_GRAMMAR(normal_expression, | expression_strcat);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

expression_strcat
	: expression_strcat STRCAT expression_bitor
		{{ DBG_TRACE_GRAMMAR(expression_strcat, : expression_strcat STRCAT expression_bitor);

			$$ = parser_make_expression (this_parser, PT_STRCAT, $1, $3, NULL);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| expression_bitor
		{{ DBG_TRACE_GRAMMAR(expression_strcat, | expression_bitor);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

expression_bitor
	: expression_bitor '|' expression_bitand
		{{ DBG_TRACE_GRAMMAR(expression_bitor, : expression_bitor '|' expression_bitand);

			$$ = parser_make_expression (this_parser, PT_BIT_OR, $1, $3, NULL);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| expression_bitand
		{{ DBG_TRACE_GRAMMAR(expression_bitor, | expression_bitand);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

expression_bitand
	: expression_bitand '&' expression_bitshift
		{{ DBG_TRACE_GRAMMAR(expression_bitand, : expression_bitand '&' expression_bitshift);

			$$ = parser_make_expression (this_parser, PT_BIT_AND, $1, $3, NULL);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| expression_bitshift
		{{ DBG_TRACE_GRAMMAR(expression_bitand, | expression_bitshift);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

expression_bitshift
	: expression_bitshift BITSHIFT_LEFT expression_add_sub
		{{ DBG_TRACE_GRAMMAR(expression_bitshift, : expression_bitshift BITSHIFT_LEFT expression_add_sub);

			$$ = parser_make_expression (this_parser, PT_BITSHIFT_LEFT, $1, $3, NULL);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| expression_bitshift BITSHIFT_RIGHT expression_add_sub
		{{ DBG_TRACE_GRAMMAR(expression_bitshift, | expression_bitshift BITSHIFT_RIGHT expression_add_sub);

			$$ = parser_make_expression (this_parser, PT_BITSHIFT_RIGHT, $1, $3, NULL);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| expression_add_sub
		{{ DBG_TRACE_GRAMMAR(expression_bitshift, | expression_add_sub);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

expression_add_sub
	: expression_add_sub '+' term
		{{ DBG_TRACE_GRAMMAR(expression_add_sub, : expression_add_sub '+' term);

			$$ = parser_make_expression (this_parser, PT_PLUS, $1, $3, NULL);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| expression_add_sub '-' term
		{{ DBG_TRACE_GRAMMAR(expression_add_sub, | expression_add_sub '-' term);

			$$ = parser_make_expression (this_parser, PT_MINUS, $1, $3, NULL);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| term
		{{ DBG_TRACE_GRAMMAR(expression_add_sub, | term);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

term
	: term '*' factor
		{{ DBG_TRACE_GRAMMAR(term, : term '*' factor);

			$$ = parser_make_expression (this_parser, PT_TIMES, $1, $3, NULL);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| term '/' factor
		{{ DBG_TRACE_GRAMMAR(term, | term '/' factor);

			$$ = parser_make_expression (this_parser, PT_DIVIDE, $1, $3, NULL);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| term DIV factor
		{{ DBG_TRACE_GRAMMAR(term, | term DIV factor);

			$$ = parser_make_expression (this_parser, PT_DIV, $1, $3, NULL);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| term MOD factor
		{{ DBG_TRACE_GRAMMAR(term, | term MOD factor);

			$$ = parser_make_expression (this_parser, PT_MOD, $1, $3, NULL);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| factor
		{{ DBG_TRACE_GRAMMAR(term, | factor);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

factor
	: factor '^' factor_
		{{ DBG_TRACE_GRAMMAR(factor, : factor '^' factor_);

			$$ = parser_make_expression (this_parser, PT_BIT_XOR, $1, $3, NULL);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| factor_
		{{ DBG_TRACE_GRAMMAR(factor, | factor_);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

factor_
	: primary_w_collate
		{{ DBG_TRACE_GRAMMAR(factor_, : primary_w_collate);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| '-' factor_
		{{ DBG_TRACE_GRAMMAR(factor_, | '-' factor_);

			$$ = parser_make_expression (this_parser, PT_UNARY_MINUS, $2, NULL, NULL);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| '+' factor_
		{{ DBG_TRACE_GRAMMAR(factor_, | '+' factor_);

			$$ = $2;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| '~' primary
		{{ DBG_TRACE_GRAMMAR(factor_, | '~' primary);

			$$ = parser_make_expression (this_parser, PT_BIT_NOT, $2, NULL, NULL);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| PRIOR
		{{ DBG_TRACE_GRAMMAR(factor_, | PRIOR);

			parser_save_and_set_sysc (0);
			parser_save_and_set_prc (0);
			parser_save_and_set_cbrc (0);
			parser_save_and_set_pseudoc (0);

		DBG_PRINT}}
	  primary_w_collate
		{{ DBG_TRACE_GRAMMAR(factor_, primary_w_collate);

			PT_NODE *node = parser_make_expression (this_parser, PT_PRIOR, $3, NULL, NULL);
			PARSER_SAVE_ERR_CONTEXT (node, @$.buffer_pos)

			parser_restore_sysc ();
			parser_restore_prc ();
			parser_restore_cbrc ();
			parser_restore_pseudoc ();

			if (parser_prior_check == 0)
			  {
				PT_ERRORmf(this_parser, node, MSGCAT_SET_PARSER_SEMANTIC,
				  MSGCAT_SEMANTIC_NOT_ALLOWED_HERE, "PRIOR");
			  }

			$$ = node;

		DBG_PRINT}}
	| CONNECT_BY_ROOT
		{{ DBG_TRACE_GRAMMAR(factor_, | CONNECT_BY_ROOT);

			parser_save_and_set_sysc (0);
			parser_save_and_set_prc (0);
			parser_save_and_set_cbrc (0);
			parser_save_and_set_pseudoc (0);

		DBG_PRINT}}
	  primary_w_collate
		{{ DBG_TRACE_GRAMMAR(factor_, primary_w_collate);

			PT_NODE *node = parser_make_expression (this_parser, PT_CONNECT_BY_ROOT, $3, NULL, NULL);
			PARSER_SAVE_ERR_CONTEXT (node, @$.buffer_pos)

			parser_restore_sysc ();
			parser_restore_prc ();
			parser_restore_cbrc ();
			parser_restore_pseudoc ();

			if (parser_connectbyroot_check == 0)
			  {
				PT_ERRORmf(this_parser, node, MSGCAT_SET_PARSER_SEMANTIC,
				  MSGCAT_SEMANTIC_NOT_ALLOWED_HERE, "CONNECT_BY_ROOT");
			  }

			$$ = node;

		DBG_PRINT}}
	;

primary_w_collate
	: primary opt_collation
		{{ DBG_TRACE_GRAMMAR(primary_w_collate, : primary opt_collation);
			PT_NODE *node = $1;
			PT_NODE *coll_node = $2;

  			if (node != NULL && coll_node != NULL)
  			  {
  			    node = pt_set_collation_modifier (this_parser, node, coll_node);
  			  }

			if (node->node_type == PT_VALUE)
  			  {
  			    node->info.value.is_collate_allowed = true;
  			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}

primary
	: pseudo_column		%dprec 11
		{{ DBG_TRACE_GRAMMAR(primary, : pseudo_column);

			if (parser_pseudocolumn_check == 0)
			  PT_ERRORmf (this_parser, $1, MSGCAT_SET_PARSER_SEMANTIC,
				MSGCAT_SEMANTIC_NOT_ALLOWED_HERE, "Pseudo-column");

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| reserved_func		%dprec 10
		{{ DBG_TRACE_GRAMMAR(primary, | reserved_func);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| case_expr		%dprec 9
		{{ DBG_TRACE_GRAMMAR(primary, | case_expr);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| extract_expr		%dprec 8
		{{ DBG_TRACE_GRAMMAR(primary, | extract_expr);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| literal_w_o_param	%dprec 7
		{{ DBG_TRACE_GRAMMAR(primary, | literal_w_o_param);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| insert_expression	%dprec 6
		{{ DBG_TRACE_GRAMMAR(primary, | insert_expression);

			$1->info.insert.is_subinsert = PT_IS_SUBINSERT;
			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
			parser_groupby_exception = PT_IS_SUBINSERT;

		DBG_PRINT}}
	| path_expression	%dprec 5
		{{ DBG_TRACE_GRAMMAR(primary, | path_expression);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| '(' expression_list ')' %dprec 4
		{{ DBG_TRACE_GRAMMAR(primary, | '(' expression_list ')');
			PT_NODE *exp = $2;
			exp = pt_create_paren_expr_list (exp);
			$$ = exp;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| ROW '(' expression_list ')' %dprec 4
		{{ DBG_TRACE_GRAMMAR(primary, | ROW '(' expression_list ')');
			PT_NODE *exp = $3;
			exp = pt_create_paren_expr_list (exp);
			$$ = exp;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| '(' search_condition_query ')' %dprec 2
		{{ DBG_TRACE_GRAMMAR(primary, | '(' search_condition_query ')');

			PT_NODE *exp = $2;

			if (exp && exp->node_type == PT_EXPR)
			  {
			    exp->info.expr.paren_type = 1;
			  }

			$$ = exp;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
			parser_groupby_exception = PT_EXPR;

		DBG_PRINT}}
	| subquery    %dprec 1
		{{ DBG_TRACE_GRAMMAR(primary, | subquery);
			parser_groupby_exception = PT_IS_SUBQUERY;
			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
		DBG_PRINT}}
	| session_variable_expression
		{{ DBG_TRACE_GRAMMAR(primary, | session_variable_expression);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		}}
	;

search_condition_query
	: search_condition_expression
		{{ DBG_TRACE_GRAMMAR(search_condition_query, : search_condition_expression);

			PT_NODE *node = $1;
			parser_push_orderby_node (node);

		DBG_PRINT}}
	  opt_orderby_clause
		{{ DBG_TRACE_GRAMMAR(search_condition_query, opt_orderby_clause);

			PT_NODE *node = parser_pop_orderby_node ();
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

search_condition_expression
	: search_condition
		{{ DBG_TRACE_GRAMMAR(search_condition_expression, : search_condition);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

pseudo_column
	: CONNECT_BY_ISCYCLE
		{{ DBG_TRACE_GRAMMAR(pseudo_column, : CONNECT_BY_ISCYCLE);

			$$ = parser_make_expression (this_parser, PT_CONNECT_BY_ISCYCLE, NULL, NULL, NULL);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| CONNECT_BY_ISLEAF
		{{ DBG_TRACE_GRAMMAR(pseudo_column, | CONNECT_BY_ISLEAF);

			$$ = parser_make_expression (this_parser, PT_CONNECT_BY_ISLEAF, NULL, NULL, NULL);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| LEVEL
		{{ DBG_TRACE_GRAMMAR(pseudo_column, | LEVEL);

			$$ = parser_make_expression (this_parser, PT_LEVEL, NULL, NULL, NULL);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;


reserved_func
        : COUNT '(' '*' ')'
		{{ DBG_TRACE_GRAMMAR(reserved_func, : COUNT '(' '*' ')');

			PT_NODE *node = parser_new_node (this_parser, PT_FUNCTION);

			if (node)
			  {
			    node->info.function.arg_list = NULL;
			    node->info.function.function_type = PT_COUNT_STAR;
			  }


			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
			parser_groupby_exception = PT_COUNT;

		DBG_PRINT}}
	| COUNT '(' '*' ')' OVER '(' opt_analytic_partition_by opt_analytic_order_by ')'
		{{ DBG_TRACE_GRAMMAR(reserved_func, | COUNT '(' '*' ')' OVER '(' opt_analytic_partition_by opt_analytic_order_by ')');

			PT_NODE *node = parser_new_node (this_parser, PT_FUNCTION);

			if (node)
			  {
			    node->info.function.function_type = PT_COUNT_STAR;
			    node->info.function.all_or_distinct = PT_ALL;
			    node->info.function.arg_list = NULL;
			    node->info.function.analytic.is_analytic = true;
			    node->info.function.analytic.partition_by = $7;
			    node->info.function.analytic.order_by = $8;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| COUNT '(' of_distinct_unique expression_ ')'
		{{ DBG_TRACE_GRAMMAR(reserved_func, | COUNT '(' of_distinct_unique expression_ ')');

			PT_NODE *node = parser_new_node (this_parser, PT_FUNCTION);

			if (node)
			  {
			    node->info.function.all_or_distinct = PT_DISTINCT;
			    node->info.function.function_type = PT_COUNT;
			    node->info.function.arg_list = $4;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
			parser_groupby_exception = PT_COUNT;

		DBG_PRINT}}
	| COUNT '(' of_distinct_unique expression_ ')' OVER '(' opt_analytic_partition_by opt_analytic_order_by ')'
		{{ DBG_TRACE_GRAMMAR(reserved_func, | COUNT '(' of_distinct_unique expression_ ')' OVER '(' ~ ')');

			PT_NODE *node = parser_new_node (this_parser, PT_FUNCTION);

			if (node)
			  {
			    node->info.function.all_or_distinct = PT_DISTINCT;
			    node->info.function.function_type = PT_COUNT;
			    node->info.function.arg_list = $4;
			    node->info.function.analytic.is_analytic = true;
			    node->info.function.analytic.partition_by = $8;
			    node->info.function.analytic.order_by = $9;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| COUNT '(' opt_all expression_ ')'
		{{ DBG_TRACE_GRAMMAR(reserved_func, | COUNT '(' opt_all expression_ ')');

			PT_NODE *node = parser_new_node (this_parser, PT_FUNCTION);

			if (node)
			  {
			    node->info.function.all_or_distinct = PT_ALL;
			    node->info.function.function_type = PT_COUNT;
			    node->info.function.arg_list = $4;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
			parser_groupby_exception = PT_COUNT;

		DBG_PRINT}}
	| COUNT '(' opt_all expression_ ')' OVER '(' opt_analytic_partition_by opt_analytic_order_by ')'
		{{ DBG_TRACE_GRAMMAR(reserved_func, | COUNT '(' opt_all expression_ ')' OVER '(' ~ ')');

			PT_NODE *node = parser_new_node (this_parser, PT_FUNCTION);

			if (node)
			  {
			    node->info.function.all_or_distinct = PT_ALL;
			    node->info.function.function_type = PT_COUNT;
			    node->info.function.arg_list = $4;
			    node->info.function.analytic.is_analytic = true;
			    node->info.function.analytic.partition_by = $8;
			    node->info.function.analytic.order_by = $9;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| of_avg_max_etc '(' of_distinct_unique expression_ ')'
		{{ DBG_TRACE_GRAMMAR(reserved_func, | of_avg_max_etc '(' of_distinct_unique expression_ ')');

			PT_NODE *node = parser_new_node (this_parser, PT_FUNCTION);

			if (node != NULL)
			  {
			    node->info.function.function_type = $1;

			    if ($1 == PT_MAX || $1 == PT_MIN)
			      node->info.function.all_or_distinct = PT_ALL;
			    else
			      node->info.function.all_or_distinct = PT_DISTINCT;

			    node->info.function.arg_list = $4;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
			parser_groupby_exception = PT_COUNT;

		DBG_PRINT}}
	| of_avg_max_etc '(' opt_all expression_ ')'
		{{ DBG_TRACE_GRAMMAR(reserved_func, | of_avg_max_etc '(' opt_all expression_ ')');

			PT_NODE *node = parser_new_node (this_parser, PT_FUNCTION);

			if (node != NULL)
			  {
			    node->info.function.function_type = $1;
			    node->info.function.all_or_distinct = PT_ALL;
			    node->info.function.arg_list = $4;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
			parser_groupby_exception = PT_COUNT;

		DBG_PRINT}}
	| of_analytic '(' of_distinct_unique expression_ ')' OVER '(' opt_analytic_partition_by opt_analytic_order_by ')'
		{{ DBG_TRACE_GRAMMAR(reserved_func, | of_analytic '(' of_distinct_unique expression_ ')' OVER '(' ~ ')');

			PT_NODE *node = parser_new_node (this_parser, PT_FUNCTION);

			if (node)
			  {
			    node->info.function.function_type = $1;
			    if ($1 == PT_MAX || $1 == PT_MIN)
			      node->info.function.all_or_distinct = PT_ALL;
			    else
			      node->info.function.all_or_distinct = PT_DISTINCT;
			    node->info.function.arg_list = $4;
			    node->info.function.analytic.is_analytic = true;
			    node->info.function.analytic.partition_by = $8;
			    node->info.function.analytic.order_by = $9;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| of_analytic '(' opt_all expression_ ')' OVER '(' opt_analytic_partition_by opt_analytic_order_by ')'
		{{ DBG_TRACE_GRAMMAR(reserved_func, | of_analytic '(' opt_all expression_ ')' OVER '(' ~ ')');

			PT_NODE *node = parser_new_node (this_parser, PT_FUNCTION);

			if (node)
			  {
			    node->info.function.function_type = $1;
			    node->info.function.all_or_distinct = PT_ALL;
			    node->info.function.arg_list = $4;
			    node->info.function.analytic.is_analytic = true;
			    node->info.function.analytic.partition_by = $8;
			    node->info.function.analytic.order_by = $9;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| of_analytic_first_last '(' expression_ ')' opt_analytic_ignore_nulls OVER '(' opt_analytic_partition_by opt_analytic_order_by ')'
		{{ DBG_TRACE_GRAMMAR(reserved_func, | of_analytic_first_last '(' expression_ ')' opt_analytic_ignore_nulls OVER '(' ~ ')');

			PT_NODE *node = parser_new_node (this_parser, PT_FUNCTION);

			if (node)
			  {
			    node->info.function.function_type = $1;
			    node->info.function.all_or_distinct = PT_ALL;
			    node->info.function.arg_list = $3;

			    node->info.function.analytic.ignore_nulls = $5;

			    node->info.function.analytic.is_analytic = true;
			    node->info.function.analytic.partition_by = $8;
			    node->info.function.analytic.order_by = $9;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| of_analytic_nth_value '(' expression_ ',' expression_ ')' opt_analytic_from_last opt_analytic_ignore_nulls OVER '(' opt_analytic_partition_by opt_analytic_order_by ')'
		{{ DBG_TRACE_GRAMMAR(reserved_func, | of_analytic_nth_value '(' expression_ ',' expression_ ')' opt_analytic_from_last opt_analytic_ignore_nulls OVER '(' ~ ')');

			PT_NODE *node = parser_new_node (this_parser, PT_FUNCTION);

			if (node)
			  {
			    node->info.function.function_type = $1;
			    node->info.function.all_or_distinct = PT_ALL;
			    node->info.function.arg_list = $3;

			    node->info.function.analytic.offset = $5;

			    node->info.function.analytic.default_value = parser_new_node (this_parser, PT_VALUE);
			    if (node->info.function.analytic.default_value != NULL)
			      {
				    node->info.function.analytic.default_value->type_enum = PT_TYPE_NULL;
			      }

			    node->info.function.analytic.from_last = $7;
			    node->info.function.analytic.ignore_nulls = $8;

			    node->info.function.analytic.is_analytic = true;
			    node->info.function.analytic.partition_by = $11;
			    node->info.function.analytic.order_by = $12;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| of_analytic_lead_lag '(' expression_ ')' OVER '(' opt_analytic_partition_by opt_analytic_order_by ')'
		{{ DBG_TRACE_GRAMMAR(reserved_func, | of_analytic_lead_lag '(' expression_ ')' OVER '(' ~ ')');

			PT_NODE *node = parser_new_node (this_parser, PT_FUNCTION);

			if (node)
			  {
			    node->info.function.function_type = $1;
			    node->info.function.all_or_distinct = PT_ALL;
			    node->info.function.arg_list = $3;

			    node->info.function.analytic.offset = parser_new_node (this_parser, PT_VALUE);
			    if (node->info.function.analytic.offset != NULL)
			      {
				node->info.function.analytic.offset->type_enum = PT_TYPE_INTEGER;
				node->info.function.analytic.offset->info.value.data_value.i = 1;
			      }

			    node->info.function.analytic.default_value = parser_new_node (this_parser, PT_VALUE);
			    if (node->info.function.analytic.default_value != NULL)
			      {
				node->info.function.analytic.default_value->type_enum = PT_TYPE_NULL;
			      }

			    node->info.function.analytic.partition_by = $7;
			    node->info.function.analytic.order_by = $8;
			    node->info.function.analytic.is_analytic = true;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| of_analytic_lead_lag '(' expression_ ',' expression_ ')' OVER '(' opt_analytic_partition_by opt_analytic_order_by ')'
		{{ DBG_TRACE_GRAMMAR(reserved_func, | of_analytic_lead_lag '(' expression_ ',' expression_ ')' OVER '(' ~ ')');

			PT_NODE *node = parser_new_node (this_parser, PT_FUNCTION);

			if (node)
			  {
			    node->info.function.function_type = $1;
			    node->info.function.all_or_distinct = PT_ALL;
			    node->info.function.arg_list = $3;
			    node->info.function.analytic.offset = $5;

			    node->info.function.analytic.default_value = parser_new_node (this_parser, PT_VALUE);
			    if (node->info.function.analytic.default_value != NULL)
			      {
				node->info.function.analytic.default_value->type_enum = PT_TYPE_NULL;
			      }

			    node->info.function.analytic.partition_by = $9;
			    node->info.function.analytic.order_by = $10;
			    node->info.function.analytic.is_analytic = true;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| of_analytic_lead_lag '(' expression_ ',' expression_ ',' expression_ ')' OVER '(' opt_analytic_partition_by opt_analytic_order_by ')'
		{{ DBG_TRACE_GRAMMAR(reserved_func, | of_analytic_lead_lag '(' expression_ ',' expression_ ',' expression_ ')' OVER '(' ~ ')');

			PT_NODE *node = parser_new_node (this_parser, PT_FUNCTION);

			if (node)
			  {
			    node->info.function.function_type = $1;
			    node->info.function.all_or_distinct = PT_ALL;
			    node->info.function.arg_list = $3;
			    node->info.function.analytic.offset = $5;
			    node->info.function.analytic.default_value = $7;
			    node->info.function.analytic.partition_by = $11;
			    node->info.function.analytic.order_by = $12;
			    node->info.function.analytic.is_analytic = true;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| of_analytic_no_args '(' ')' OVER '(' opt_analytic_partition_by opt_analytic_order_by ')'
		{{ DBG_TRACE_GRAMMAR(reserved_func, | of_analytic_no_args '(' ')' OVER '(' ~ ')');

			PT_NODE *node = parser_new_node (this_parser, PT_FUNCTION);

			if (node)
			  {
			    node->info.function.function_type = $1;
			    node->info.function.all_or_distinct = PT_ALL;
			    node->info.function.arg_list = NULL;
			    node->info.function.analytic.is_analytic = true;
			    node->info.function.analytic.partition_by = $6;
			    node->info.function.analytic.order_by = $7;
				if ($7 == NULL)
				  {
					if ($1 == PT_CUME_DIST || $1 == PT_PERCENT_RANK)
					  {
						PT_ERRORmf (this_parser, node,
									MSGCAT_SET_PARSER_SEMANTIC,
									MSGCAT_SEMANTIC_NULL_ORDER_BY,
									fcode_get_lowercase_name ($1));
					  }
				  }
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| GROUP_CONCAT
		{ push_msg(MSGCAT_SYNTAX_INVALID_GROUP_CONCAT); }
		'(' of_distinct_unique expression_ opt_agg_order_by opt_group_concat_separator ')'
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(reserved_func, | GROUP_CONCAT '(' of_distinct_unique expression_ ~ ')' );

			PT_NODE *node = parser_new_node (this_parser, PT_FUNCTION);

			if (node)
			  {
			    node->info.function.function_type = PT_GROUP_CONCAT;
			    node->info.function.all_or_distinct = PT_DISTINCT;
			    node->info.function.arg_list = parser_make_link ($5, $7);
			    node->info.function.order_by = $6;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| GROUP_CONCAT '(' opt_all opt_expression_list opt_agg_order_by opt_group_concat_separator ')'
		{{ DBG_TRACE_GRAMMAR(reserved_func, | GROUP_CONCAT '(' opt_all opt_expression_list opt_agg_order_by opt_group_concat_separator ')' );

			PT_NODE *node = parser_new_node (this_parser, PT_FUNCTION);

			PT_NODE *args_list = $4;

			if (parser_count_list(args_list) != 1)
		    {
			  PT_ERRORm (this_parser, node, MSGCAT_SET_PARSER_SYNTAX,
					      MSGCAT_SYNTAX_INVALID_GROUP_CONCAT);
		    }

			if (node)
			  {
			    node->info.function.function_type = PT_GROUP_CONCAT;
			    node->info.function.all_or_distinct = PT_ALL;
			    node->info.function.arg_list = parser_make_link ($4, $6);
			    node->info.function.order_by = $5;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| JSON_ARRAYAGG '(' expression_ ')'
		{{ DBG_TRACE_GRAMMAR(reserved_func, | JSON_ARRAYAGG '(' expression_ ')' );

			PT_NODE *node = parser_new_node (this_parser, PT_FUNCTION);

			if (node)
			  {
			    node->info.function.function_type = PT_JSON_ARRAYAGG;
			    node->info.function.all_or_distinct = PT_ALL;
			    node->info.function.arg_list = parser_make_link ($3, NULL);
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| JSON_OBJECTAGG '(' expression_list ')'
		{{ DBG_TRACE_GRAMMAR(reserved_func, | JSON_OBJECTAGG '(' expression_list ')' );
 			PT_NODE *node = parser_new_node (this_parser, PT_FUNCTION);
			PT_NODE *args_list = $3;

			if (parser_count_list(args_list) != 2)
		    {
			  PT_ERRORm (this_parser, node, MSGCAT_SET_PARSER_SYNTAX,
					      MSGCAT_SYNTAX_INVALID_JSON_OBJECTAGG);
		    }

 			if (node)
			  {
			    node->info.function.function_type = PT_JSON_OBJECTAGG;
			    node->info.function.all_or_distinct = PT_ALL;
			    node->info.function.arg_list = args_list;
			  }

 			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
 		DBG_PRINT}}
	| of_percentile '(' expression_ ')' WITHIN GROUP_ '(' ORDER BY sort_spec ')' opt_over_analytic_partition_by
		{{ DBG_TRACE_GRAMMAR(reserved_func, | of_percentile '(' expression_ ')' WITHIN GROUP_ '(' ORDER BY sort_spec ')' ~ );

			PT_NODE *node = parser_new_node (this_parser, PT_FUNCTION);

			if (node != NULL)
			  {
			    node->info.function.function_type = $1;
			    node->info.function.all_or_distinct = PT_ALL;

			    if ($3 != NULL)
			      {
			        node->info.function.percentile =
			          pt_wrap_with_cast_op (this_parser, $3,
			                                PT_TYPE_DOUBLE, 0, 0, NULL);
			      }

			    if ($10 != NULL)
			      {
			      	if (pt_is_const ($10->info.sort_spec.expr))
			      	  {
			      	    node->info.function.arg_list =
			      	    					$10->info.sort_spec.expr;
			      	  }
			      	else
			      	  {
			      	    if (is_analytic_function)
			      	      {
			      	        node->info.function.analytic.order_by = $10;
			      	      }
			      	    else
			      	      {
			      	        node->info.function.order_by = $10;
			      	      }

				        node->info.function.arg_list =
						          parser_copy_tree (this_parser,
						          					$10->info.sort_spec.expr);
			          }
			      }

			    if (is_analytic_function)
			      {
			        node->info.function.analytic.is_analytic = is_analytic_function;
			        node->info.function.analytic.partition_by = $12;
			      }
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos);

		DBG_PRINT}}
	| INSERT
	  { push_msg(MSGCAT_SYNTAX_INVALID_INSERT_SUBSTRING); }
	  '(' expression_list ')'
	  { pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(reserved_func, | INSERT '(' expression_list ')' );
                    $$ = parser_make_func_with_arg_count (this_parser, F_INSERT_SUBSTRING, $4, 4, 4);
                    PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
		DBG_PRINT}}
	| ELT '(' opt_expression_list ')'
		{{ DBG_TRACE_GRAMMAR(reserved_func, | ELT '(' opt_expression_list ')' );
                    $$ = parser_make_func_with_arg_count (this_parser, F_ELT, $3, 1, 0);
                    PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
		DBG_PRINT}}
	| POSITION '(' expression_ IN_ expression_ ')'
		{{ DBG_TRACE_GRAMMAR(reserved_func, | POSITION '(' expression_ IN_ expression_ ')' );

			$$ = parser_make_expression (this_parser, PT_POSITION, $3, $5, NULL);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| SUBSTRING_
		{ push_msg(MSGCAT_SYNTAX_INVALID_SUBSTRING); }
	  '(' expression_ FROM expression_ For expression_ ')'
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(reserved_func, | SUBSTRING_  '(' expression_ FROM expression_ For expression_ ')' );

			PT_NODE *node = parser_make_expression (this_parser, PT_SUBSTRING, $4, $6, $8);
			node->info.expr.qualifier = PT_SUBSTR_ORG;
			PICE (node);
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| SUBSTRING_
		{ push_msg(MSGCAT_SYNTAX_INVALID_SUBSTRING); }
	  '(' expression_ FROM expression_ ')'
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(reserved_func, | SUBSTRING_  '(' expression_ FROM expression_ ')' );

			PT_NODE *node = parser_make_expression (this_parser, PT_SUBSTRING, $4, $6, NULL);
			node->info.expr.qualifier = PT_SUBSTR_ORG;
			PICE (node);
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| SUBSTRING_
		{ push_msg(MSGCAT_SYNTAX_INVALID_SUBSTRING); }
	  '(' expression_ ',' expression_ ',' expression_ ')'
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(reserved_func, | SUBSTRING_   '(' expression_ ',' expression_ ',' expression_ ')' );

			PT_NODE *node = parser_make_expression (this_parser, PT_SUBSTRING, $4, $6, $8);
			node->info.expr.qualifier = PT_SUBSTR_ORG;
			PICE (node);
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| SUBSTRING_
		{ push_msg(MSGCAT_SYNTAX_INVALID_SUBSTRING); }
	  '(' expression_ ',' expression_ ')'
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(reserved_func, | SUBSTRING_    '(' expression_ ',' expression_ ')' );

			PT_NODE *node = parser_make_expression (this_parser, PT_SUBSTRING, $4, $6, NULL);
			node->info.expr.qualifier = PT_SUBSTR_ORG;
			PICE (node);
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| Date
		{ push_msg(MSGCAT_SYNTAX_INVALID_DATE); }
	  '(' expression_ ')'
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(reserved_func, | Date  '(' expression_ ')' );

			PT_NODE *node = parser_make_expression (this_parser, PT_DATEF, $4, NULL, NULL);
			PICE (node);
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| Time
		{ push_msg(MSGCAT_SYNTAX_INVALID_TIME); }
	  '(' expression_ ')'
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(reserved_func, | Time '(' expression_ ')' );

			PT_NODE *node = parser_make_expression (this_parser, PT_TIMEF, $4, NULL, NULL);
			PICE (node);
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| ADDDATE
		{ push_msg(MSGCAT_SYNTAX_INVALID_ADDDATE); }
	  '(' expression_ ',' expression_ ')'
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(reserved_func, | ADDDATE '(' expression_ ',' expression_ ')' );

			PT_NODE *node = parser_make_expression (this_parser, PT_ADDDATE, $4, $6, NULL);
			PICE (node);
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| adddate_name
		{ push_msg(MSGCAT_SYNTAX_INVALID_DATE_ADD); }
	  '(' expression_ ',' INTERVAL expression_ datetime_field ')'
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(reserved_func, | adddate_name '(' expression_ ',' INTERVAL expression_ datetime_field ')' );

			PT_NODE *node;
			PT_NODE *node_unit = parser_new_node (this_parser, PT_VALUE);

			if (node_unit)
			  {
			    node_unit->info.expr.qualifier = $8;
			    node_unit->type_enum = PT_TYPE_INTEGER;
			  }

			node = parser_make_expression (this_parser, PT_DATE_ADD, $4, $7, node_unit);
			PICE (node);
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| SUBDATE
		{ push_msg(MSGCAT_SYNTAX_INVALID_SUBDATE); }
	  '(' expression_ ',' expression_ ')'
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(reserved_func, | SUBDATE '(' expression_ ',' expression_ ')' );

			PT_NODE *node = parser_make_expression (this_parser, PT_SUBDATE, $4, $6, NULL);
			PICE (node);
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| subdate_name
		{ push_msg(MSGCAT_SYNTAX_INVALID_DATE_SUB); }
	  '(' expression_ ',' INTERVAL expression_ datetime_field ')'
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(reserved_func, | subdate_name  '(' expression_ ',' INTERVAL expression_ datetime_field ')' );

			PT_NODE *node;
			PT_NODE *node_unit = parser_new_node (this_parser, PT_VALUE);

			if (node_unit)
			  {
			    node_unit->info.expr.qualifier = $8;
			    node_unit->type_enum = PT_TYPE_INTEGER;
			  }

			node = parser_make_expression (this_parser, PT_DATE_SUB, $4, $7, node_unit);
			PICE (node);
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| TIMESTAMP
		{ push_msg(MSGCAT_SYNTAX_INVALID_TIMESTAMP); }
		'(' expression_ ')'
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(reserved_func, | TIMESTAMP  '(' expression_ ')' );
			PT_NODE *arg2 = NULL;
			PT_NODE *node = NULL;
			arg2 = parser_new_node(this_parser, PT_VALUE);
			if (arg2)
			  {
			    db_make_int(&arg2->info.value.db_value, 0);
			    arg2->type_enum = PT_TYPE_INTEGER;
			  }
			node = parser_make_expression (this_parser, PT_TIMESTAMP, $4, arg2, NULL); /* will call timestamp(arg1, 0) */
			PICE (node);
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| TIMESTAMP
		{ push_msg(MSGCAT_SYNTAX_INVALID_TIMESTAMP); }
		'(' expression_ ',' expression_ ')'
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(reserved_func, | TIMESTAMP  '(' expression_ ',' expression_ ')' );

			PT_NODE *node = parser_make_expression (this_parser, PT_TIMESTAMP, $4, $6, NULL); /* 2 parameters */
			PICE (node);
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| YEAR_
		{ push_msg(MSGCAT_SYNTAX_INVALID_YEAR); }
		'(' expression_ ')'
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(reserved_func, | YEAR_  '(' expression_ ')' );

			PT_NODE *node = parser_make_expression (this_parser, PT_YEARF, $4, NULL, NULL); /* 1 parameter */
			PICE (node);
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| MONTH_
		{ push_msg(MSGCAT_SYNTAX_INVALID_MONTH); }
		'(' expression_ ')'
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(reserved_func, | MONTH_  '(' expression_ ')' );

			PT_NODE *node = parser_make_expression (this_parser, PT_MONTHF, $4, NULL, NULL); /* 1 parameter */
			PICE (node);
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| DAY_
		{ push_msg(MSGCAT_SYNTAX_INVALID_DAY); }
		'(' expression_ ')'
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(reserved_func, | DAY_  '(' expression_ ')' );

			PT_NODE *node = parser_make_expression (this_parser, PT_DAYF, $4, NULL, NULL); /* 1 parameter */
			PICE (node);
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| HOUR_
		{ push_msg(MSGCAT_SYNTAX_INVALID_HOUR); }
		'(' expression_ ')'
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(reserved_func, | HOUR_  '(' expression_ ')' );

			PT_NODE *node = parser_make_expression (this_parser, PT_HOURF, $4, NULL, NULL); /* 1 parameter */
			PICE (node);
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| MINUTE_
		{ push_msg(MSGCAT_SYNTAX_INVALID_MINUTE); }
		'(' expression_ ')'
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(reserved_func, | MINUTE_  '(' expression_ ')' );

			PT_NODE *node = parser_make_expression (this_parser, PT_MINUTEF, $4, NULL, NULL); /* 1 parameter */
			PICE (node);
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| SECOND_
		{ push_msg(MSGCAT_SYNTAX_INVALID_SECOND); }
		'(' expression_ ')'
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(reserved_func, | SECOND_  '(' expression_ ')' );

			PT_NODE *node = parser_make_expression (this_parser, PT_SECONDF, $4, NULL, NULL); /* 1 parameter */
			PICE (node);
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| DATABASE
		{ push_msg(MSGCAT_SYNTAX_INVALID_DATABASE); }
	  '(' ')'
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(reserved_func, | DATABASE  '(' ')' );

			PT_NODE *node = parser_make_expression (this_parser, PT_DATABASE, NULL, NULL, NULL);
			PICE (node);
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| SCHEMA
		{ push_msg(MSGCAT_SYNTAX_INVALID_SCHEMA); }
	  '(' ')'
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(reserved_func, | SCHEMA  '(' ')' );

			PT_NODE *node = parser_make_expression (this_parser, PT_SCHEMA, NULL, NULL, NULL);
			PICE (node);
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| TRIM
		{ push_msg(MSGCAT_SYNTAX_INVALID_TRIM); }
	  '(' of_leading_trailing_both expression_ FROM expression_ ')'
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(reserved_func, | TRIM  '(' of_leading_trailing_both expression_ FROM expression_ ')' );

			PT_NODE *node = parser_make_expression (this_parser, PT_TRIM, $7, $5, NULL);
			node->info.expr.qualifier = $4;
			PICE (node);
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| TRIM
		{ push_msg(MSGCAT_SYNTAX_INVALID_TRIM); }
	  '(' of_leading_trailing_both FROM expression_ ')'
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(reserved_func, | TRIM  '(' of_leading_trailing_both FROM expression_ ')' );

			PT_NODE *node = parser_make_expression (this_parser, PT_TRIM, $6, NULL, NULL);
			node->info.expr.qualifier = $4;
			PICE (node);
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| TRIM
		{ push_msg(MSGCAT_SYNTAX_INVALID_TRIM); }
	  '(' expression_ FROM  expression_ ')'
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(reserved_func, | TRIM '(' expression_ FROM  expression_ ')' );

			PT_NODE *node = parser_make_expression (this_parser, PT_TRIM, $6, $4, NULL);
			node->info.expr.qualifier = PT_BOTH;
			PICE (node);
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| TRIM
		{ push_msg(MSGCAT_SYNTAX_INVALID_TRIM); }
	  '(' expression_ ')'
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(reserved_func, | TRIM '(' expression_ ')' );

			PT_NODE *node = parser_make_expression (this_parser, PT_TRIM, $4, NULL, NULL);
			node->info.expr.qualifier = PT_BOTH;
			PICE (node);
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| CHR
		{ push_msg(MSGCAT_SYNTAX_INVALID_CHR); }
	  '(' expression_ opt_using_charset ')'
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(reserved_func, | CHR '(' expression_ opt_using_charset ')' );

			PT_NODE *node = parser_make_expression (this_parser, PT_CHR, $4, $5, NULL);
			PICE (node);
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| CLOB_TO_CHAR
		{ push_msg(MSGCAT_SYNTAX_INVALID_CLOB_TO_CHAR); }
	  '(' expression_ opt_using_charset ')'
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(reserved_func, | CLOB_TO_CHAR '(' expression_ opt_using_charset ')' );

			PT_NODE *node = parser_make_expression (this_parser, PT_CLOB_TO_CHAR, $4, $5, NULL);
			PICE (node);
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| CAST
		{ push_msg(MSGCAT_SYNTAX_INVALID_CAST); }
	  '(' expression_ AS of_cast_data_type ')'
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(reserved_func, | CAST '(' expression_ AS of_cast_data_type ')' );

			PT_NODE *expr = parser_make_expression (this_parser, PT_CAST, $4, NULL, NULL);
			PT_TYPE_ENUM typ = TO_NUMBER (CONTAINER_AT_0 ($6));
			PT_NODE *dt = CONTAINER_AT_1 ($6);
			PT_NODE *set_dt;

			if (!dt)
			  {
			    dt = parser_new_node (this_parser, PT_DATA_TYPE);
			    dt->type_enum = TO_NUMBER (CONTAINER_AT_0 ($6));
			    dt->data_type = NULL;
			  }
			else if (typ == PT_TYPE_SET || typ == PT_TYPE_MULTISET
				 || typ == PT_TYPE_SEQUENCE)
			  {
			    set_dt = parser_new_node (this_parser, PT_DATA_TYPE);
			    set_dt->type_enum = typ;
			    set_dt->data_type = dt;
			    dt = set_dt;
			  }
			if (dt->type_enum == PT_TYPE_ENUMERATION)
			  {
			    (void) pt_check_enum_data_type(this_parser, dt);
			  }

			expr->info.expr.cast_type = dt;
			$$ = expr;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| CLASS '(' identifier ')'
		{{ DBG_TRACE_GRAMMAR(reserved_func, | CLASS '(' identifier ')' );

			$3->info.name.meta_class = PT_OID_ATTR;
			$$ = $3;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
			parser_groupby_exception = PT_OID_ATTR;

		DBG_PRINT}}
	| SYS_DATE
		{{ DBG_TRACE_GRAMMAR(reserved_func, | SYS_DATE );

			PT_NODE *expr = parser_make_expression (this_parser, PT_SYS_DATE, NULL, NULL, NULL);
			$$ = expr;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| of_current_date
		{{ DBG_TRACE_GRAMMAR(reserved_func, | of_current_date );

			PT_NODE *expr = parser_make_expression (this_parser, PT_CURRENT_DATE, NULL, NULL, NULL);
			$$ = expr;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| SYS_TIME_
		{{ DBG_TRACE_GRAMMAR(reserved_func, | SYS_TIME_ );

			PT_NODE *expr = parser_make_expression (this_parser, PT_SYS_TIME, NULL, NULL, NULL);
			$$ = expr;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| of_current_time
		{{ DBG_TRACE_GRAMMAR(reserved_func, | of_current_time );

			PT_NODE *expr = parser_make_expression (this_parser, PT_CURRENT_TIME, NULL, NULL, NULL);
			$$ = expr;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| of_db_timezone_
		{{ DBG_TRACE_GRAMMAR(reserved_func, | of_db_timezone_ );

			PT_NODE *expr = parser_make_expression (this_parser, PT_DBTIMEZONE, NULL, NULL, NULL);
			$$ = expr;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| of_session_timezone_
		{{ DBG_TRACE_GRAMMAR(reserved_func, | of_session_timezone_ );

			PT_NODE *expr = parser_make_expression (this_parser, PT_SESSIONTIMEZONE, NULL, NULL, NULL);
			$$ = expr;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| SYS_TIMESTAMP
		{{ DBG_TRACE_GRAMMAR(reserved_func, | SYS_TIMESTAMP );

			PT_NODE *expr = parser_make_expression (this_parser, PT_SYS_TIMESTAMP, NULL, NULL, NULL);
			$$ = expr;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| of_current_timestamps
		{{ DBG_TRACE_GRAMMAR(reserved_func, | of_current_timestamps );

			PT_NODE *expr = parser_make_expression (this_parser, PT_CURRENT_TIMESTAMP, NULL, NULL, NULL);
			$$ = expr;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| SYS_DATETIME
		{{ DBG_TRACE_GRAMMAR(reserved_func, | SYS_DATETIME );

			PT_NODE *expr = parser_make_expression (this_parser, PT_SYS_DATETIME, NULL, NULL, NULL);
			$$ = expr;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| of_current_datetime
		{{ DBG_TRACE_GRAMMAR(reserved_func, | of_current_datetime );

			PT_NODE *expr = parser_make_expression (this_parser, PT_CURRENT_DATETIME, NULL, NULL, NULL);
			$$ = expr;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| of_users
		{{ DBG_TRACE_GRAMMAR(reserved_func, | of_users );

			PT_NODE *node = parser_new_node (this_parser, PT_EXPR);
			if (node)
			  node->info.expr.op = PT_CURRENT_USER;

			parser_cannot_cache = true;
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| of_users
		{ push_msg(MSGCAT_SYNTAX_INVALID_SYSTEM_USER); }
	  '(' ')'
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(reserved_func, | of_users '(' ')' );

			PT_NODE *node = parser_make_expression (this_parser, PT_USER, NULL, NULL, NULL);
			PICE (node);
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| DEFAULT '('
		{ push_msg(MSGCAT_SYNTAX_INVALID_DEFAULT); }
	  simple_path_id ')'
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(reserved_func, | DEFAULT '(' simple_path_id ')');

			PT_NODE *node = NULL;
			PT_NODE *path = $4;

			if (path != NULL)
			  {
			    pt_set_fill_default_in_path_expression (path);
			    node = parser_make_expression (this_parser, PT_DEFAULTF, path, NULL, NULL);
			    PICE (node);
			  }
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| LOCAL_TRANSACTION_ID
		{{ DBG_TRACE_GRAMMAR(reserved_func, | LOCAL_TRANSACTION_ID);

			PT_NODE *node = parser_new_node (this_parser, PT_EXPR);
			if (node)
			  node->info.expr.op = PT_LOCAL_TRANSACTION_ID;

			parser_si_tran_id = true;
			parser_cannot_cache = true;
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| ROWNUM
		{{ DBG_TRACE_GRAMMAR(reserved_func, | ROWNUM);

			PT_NODE *node = parser_new_node (this_parser, PT_EXPR);

			if (node)
			  {
			    node->info.expr.op = PT_ROWNUM;
			    PT_EXPR_INFO_SET_FLAG (node, PT_EXPR_INFO_INSTNUM_C);
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
			parser_groupby_exception = PT_ROWNUM;

			if (parser_instnum_check == 0)
			  PT_ERRORmf2 (this_parser, node, MSGCAT_SET_PARSER_SEMANTIC,
				       MSGCAT_SEMANTIC_INSTNUM_COMPATIBILITY_ERR,
				       "INST_NUM() or ROWNUM", "INST_NUM() or ROWNUM");

		DBG_PRINT}}
	| ADD_MONTHS
		{ push_msg(MSGCAT_SYNTAX_INVALID_ADD_MONTHS); }
	  '(' expression_ ',' expression_ ')'
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(reserved_func, | ADD_MONTHS  '(' expression_ ',' expression_ ')');

			$$ = parser_make_expression (this_parser, PT_ADD_MONTHS, $4, $6, NULL);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| OCTET_LENGTH
		{ push_msg(MSGCAT_SYNTAX_INVALID_OCTET_LENGTH); }
	  '(' expression_ ')'
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(reserved_func, | OCTET_LENGTH  '(' expression_ ')');

			$$ = parser_make_expression (this_parser, PT_OCTET_LENGTH, $4, NULL, NULL);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| BIT_LENGTH
		{ push_msg(MSGCAT_SYNTAX_INVALID_BIT_LENGTH); }
	  '(' expression_ ')'
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(reserved_func, | BIT_LENGTH  '(' expression_ ')');

			$$ = parser_make_expression (this_parser, PT_BIT_LENGTH, $4, NULL, NULL);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| LOWER
		{ push_msg(MSGCAT_SYNTAX_INVALID_LOWER); }
	  '(' expression_ ')'
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(reserved_func, | BIT_LENGTH '(' expression_ ')' );

			$$ = parser_make_expression (this_parser, PT_LOWER, $4, NULL, NULL);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| LCASE
		{ push_msg(MSGCAT_SYNTAX_INVALID_LOWER); }
	  '(' expression_ ')'
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(reserved_func, | LCASE '(' expression_ ')' );

			$$ = parser_make_expression (this_parser, PT_LOWER, $4, NULL, NULL);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| UPPER
		{ push_msg(MSGCAT_SYNTAX_INVALID_UPPER); }
	  '(' expression_ ')'
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(reserved_func, | UPPER '(' expression_ ')' );

			$$ = parser_make_expression (this_parser, PT_UPPER, $4, NULL, NULL);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| UCASE
		{ push_msg(MSGCAT_SYNTAX_INVALID_UPPER); }
	  '(' expression_ ')'
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(reserved_func, | UCASE '(' expression_ ')' );

			$$ = parser_make_expression (this_parser, PT_UPPER, $4, NULL, NULL);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| SYS_CONNECT_BY_PATH
		{{ DBG_TRACE_GRAMMAR(reserved_func, | SYS_CONNECT_BY_PATH );

			push_msg(MSGCAT_SYNTAX_INVALID_SYS_CONNECT_BY_PATH);

			parser_save_and_set_sysc (0);
			parser_save_and_set_prc (0);
			parser_save_and_set_cbrc (0);
			parser_save_and_set_pseudoc (0);

		}}
	  '(' expression_ ',' char_string_literal ')'
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(reserved_func,  '(' expression_ ',' char_string_literal ')' );

			PT_NODE *node = parser_make_expression (this_parser, PT_SYS_CONNECT_BY_PATH, $4, $6, NULL);
			PT_NODE *char_string_node = $6;

			pt_value_set_collation_info (this_parser, char_string_node, NULL);

			PARSER_SAVE_ERR_CONTEXT (node, @$.buffer_pos)

			parser_restore_sysc ();
			parser_restore_prc ();
			parser_restore_cbrc ();
			parser_restore_pseudoc ();
			if (parser_sysconnectbypath_check == 0)
			  {
				PT_ERRORmf(this_parser, node, MSGCAT_SET_PARSER_SEMANTIC,
				  MSGCAT_SEMANTIC_NOT_ALLOWED_HERE, "SYS_CONNECT_BY_PATH");
			  }
			$$ = node;

		DBG_PRINT}}
	| IF
		{ push_msg (MSGCAT_SYNTAX_INVALID_IF); }
	  '(' search_condition ',' expression_ ',' expression_ ')'
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(reserved_func,  | IF '(' search_condition ',' expression_ ',' expression_ ')' );

			$$ = parser_make_expression (this_parser, PT_IF, $4, $6, $8);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| IFNULL
		{ push_msg (MSGCAT_SYNTAX_INVALID_IFNULL); }
	  '(' expression_ ',' expression_ ')'
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(reserved_func,  | IFNULL  '(' expression_ ',' expression_ ')' );

			$$ = parser_make_expression (this_parser, PT_IFNULL, $4, $6, NULL);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| ISNULL
		{ push_msg (MSGCAT_SYNTAX_INVALID_ISNULL); }
	  '(' expression_ ')'
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(reserved_func,  | ISNULL  '(' expression_ ')' );

			$$ = parser_make_expression (this_parser, PT_ISNULL, $4, NULL, NULL);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| LEFT
		{ push_msg(MSGCAT_SYNTAX_INVALID_LEFT); }
	  '(' expression_ ',' expression_ ')'
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(reserved_func,  | LEFT  '(' expression_ ',' expression_ ')' );
			PT_NODE *node =
			  parser_make_expression (this_parser, PT_LEFT, $4, $6, NULL);
			PICE (node);
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| RIGHT
		{ push_msg(MSGCAT_SYNTAX_INVALID_RIGHT); }
	  '(' expression_ ',' expression_ ')'
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(reserved_func,  | RIGHT  '(' expression_ ',' expression_ ')' );
			PT_NODE *node =
			  parser_make_expression (this_parser, PT_RIGHT, $4, $6, NULL);
			PICE (node);
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| MOD
		{ push_msg(MSGCAT_SYNTAX_INVALID_MODULUS); }
	  '(' expression_ ',' expression_ ')'
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(reserved_func,  | MOD  '(' expression_ ',' expression_ ')' );
			PT_NODE *node =
			  parser_make_expression (this_parser, PT_MODULUS, $4, $6, NULL);
			PICE (node);
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| TRUNCATE
		{ push_msg(MSGCAT_SYNTAX_INVALID_TRUNCATE); }
	  '(' expression_ ',' expression_ ')'
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(reserved_func,  | TRUNCATE  '(' expression_ ',' expression_ ')' );

			$$ = parser_make_expression (this_parser, PT_TRUNC, $4, $6, NULL);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| TRANSLATE
		{ push_msg(MSGCAT_SYNTAX_INVALID_TRANSLATE); }
	  '(' expression_  ',' expression_ ',' expression_ ')'
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(reserved_func,  | TRANSLATE  '(' expression_  ',' expression_ ',' expression_ ')' );

			$$ = parser_make_expression (this_parser, PT_TRANSLATE, $4, $6, $8);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| REPLACE
		{ push_msg(MSGCAT_SYNTAX_INVALID_TRANSLATE); }
	  '(' expression_  ',' expression_ ',' expression_ ')'
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(reserved_func,  | REPLACE  '(' expression_  ',' expression_ ',' expression_ ')' );

			$$ = parser_make_expression (this_parser, PT_REPLACE, $4, $6, $8);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| REPLACE
		{ push_msg(MSGCAT_SYNTAX_INVALID_REPLACE); }
	  '(' expression_  ',' expression_ ')'
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(reserved_func,  | REPLACE '(' expression_  ',' expression_ ')' );

			$$ = parser_make_expression (this_parser, PT_REPLACE, $4, $6, NULL);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| STR_TO_DATE
		{ push_msg(MSGCAT_SYNTAX_INVALID_STRTODATE); }
	  '(' expression_  ',' string_literal_or_input_hv ')'
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(reserved_func,  | STR_TO_DATE '(' expression_  ',' string_literal_or_input_hv ')' );

			$$ = parser_make_expression (this_parser, PT_STR_TO_DATE, $4, $6, parser_make_date_lang (2, NULL));
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| STR_TO_DATE
		{ push_msg(MSGCAT_SYNTAX_INVALID_STRTODATE); }
	  '(' expression_  ',' Null ')'
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(reserved_func,  | STR_TO_DATE '(' expression_  ',' Null ')' );
			PT_NODE *node = parser_new_node (this_parser, PT_VALUE);
			if (node)
			  {
			    node->type_enum = PT_TYPE_NULL;
			    node->flag.is_added_by_parser = 1;
			  }

			$$ = parser_make_expression (this_parser, PT_STR_TO_DATE, $4, node, parser_make_date_lang (2, NULL));
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| CHARSET
		{ push_msg(MSGCAT_SYNTAX_INVALID_CHARSET); }
	  '(' expression_ ')'
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(reserved_func,  | CHARSET  '(' expression_ ')' );
			PT_NODE *node =
			  parser_make_expression (this_parser, PT_CHARSET, $4, NULL, NULL);
			PICE (node);
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| COLLATION
		{ push_msg(MSGCAT_SYNTAX_INVALID_COLLATION); }
	  '(' expression_ ')'
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(reserved_func,  | COLLATION  '(' expression_ ')' );
			PT_NODE *node =
			  parser_make_expression (this_parser, PT_COLLATION, $4, NULL, NULL);
			PICE (node);
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| of_cume_dist_percent_rank_function '(' expression_list ')' WITHIN GROUP_ '('ORDER BY sort_spec_list')'
		{{ DBG_TRACE_GRAMMAR(reserved_func, | of_cume_dist_percent_rank_function '(' expression_list ')' WITHIN GROUP_ '('ORDER BY sort_spec_list')' );

			PT_NODE *node = parser_new_node (this_parser, PT_FUNCTION);
			if (node)
			  {
				node->info.function.function_type = $1;
				node->info.function.all_or_distinct = PT_ALL;
				node->info.function.arg_list = $3;
				node->info.function.analytic.is_analytic = false;
				node->info.function.order_by = $10;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
        DBG_PRINT}}
	| INDEX_PREFIX
		{ push_msg(MSGCAT_SYNTAX_INVALID_INDEX_PREFIX); }
	  '(' expression_  ',' expression_ ',' expression_ ')'
		{ pop_msg(); }
		{{ DBG_TRACE_GRAMMAR(reserved_func, | INDEX_PREFIX  '(' expression_  ',' expression_ ',' expression_ ')' );

			$$ = parser_make_expression (this_parser, PT_INDEX_PREFIX, $4, $6, $8);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
        | JSON_ARRAY_APPEND '(' expression_list ')'
		{{ DBG_TRACE_GRAMMAR(reserved_func, | JSON_ARRAY_APPEND '(' expression_list ')' );
                    $$ = parser_make_func_with_arg_count_mod2 (this_parser, F_JSON_ARRAY_APPEND, $3, 3, 0, 1);
		    PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
		DBG_PRINT}}
        | JSON_ARRAY_INSERT '(' expression_list ')'
		{{ DBG_TRACE_GRAMMAR(reserved_func, | JSON_ARRAY_INSERT '(' expression_list ')' );
                    $$ = parser_make_func_with_arg_count_mod2 (this_parser, F_JSON_ARRAY_INSERT, $3, 3, 0, 1);
		    PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
		DBG_PRINT}}
        | JSON_ARRAY_LEX '(' opt_expression_list ')'
		{{ DBG_TRACE_GRAMMAR(reserved_func, | JSON_ARRAY_LEX '(' opt_expression_list ')' );
                    $$ = parser_make_func_with_arg_count (this_parser, F_JSON_ARRAY, $3, 0, 0);
		    PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
		DBG_PRINT}}
	| JSON_CONTAINS '(' expression_list ')'
		{{ DBG_TRACE_GRAMMAR(reserved_func, | JSON_CONTAINS '(' expression_list ')' );
                    $$ = parser_make_func_with_arg_count (this_parser, F_JSON_CONTAINS, $3, 2, 3);
                    PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
	      DBG_PRINT}}
	| JSON_CONTAINS_PATH '(' expression_list ')'
		{{ DBG_TRACE_GRAMMAR(reserved_func, | JSON_CONTAINS_PATH '(' expression_list ')' );
                    $$ = parser_make_func_with_arg_count (this_parser, F_JSON_CONTAINS_PATH, $3, 3, 0);
		    PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
		DBG_PRINT}}
        | JSON_DEPTH '(' expression_list ')'
		{{ DBG_TRACE_GRAMMAR(reserved_func, | JSON_DEPTH '(' expression_list ')' );
                    $$ = parser_make_func_with_arg_count (this_parser, F_JSON_DEPTH, $3, 1, 1);
		    PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
		DBG_PRINT}}
        | JSON_EXTRACT '(' expression_list ')'
		{{ DBG_TRACE_GRAMMAR(reserved_func, | JSON_EXTRACT '(' expression_list ')' );
                    $$ = parser_make_func_with_arg_count (this_parser, F_JSON_EXTRACT, $3, 2, 0);
                    PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
		DBG_PRINT}}
        | JSON_GET_ALL_PATHS '(' expression_list ')'
		{{ DBG_TRACE_GRAMMAR(reserved_func, | JSON_GET_ALL_PATHS '(' expression_list ')' );
                    $$ = parser_make_func_with_arg_count (this_parser, F_JSON_GET_ALL_PATHS, $3, 1, 1);
		    PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
		DBG_PRINT}}
        | JSON_INSERT '(' expression_list ')'
		{{ DBG_TRACE_GRAMMAR(reserved_func, | JSON_INSERT '(' expression_list ')' );
                    $$ = parser_make_func_with_arg_count_mod2 (this_parser, F_JSON_INSERT, $3, 3, 0, 1);
		    PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
		DBG_PRINT}}
        | JSON_KEYS '(' expression_list ')'
		{{ DBG_TRACE_GRAMMAR(reserved_func, | JSON_KEYS '(' expression_list ')' );
                    $$ = parser_make_func_with_arg_count (this_parser, F_JSON_KEYS, $3, 0, 2);
		    PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
		DBG_PRINT}}
        | JSON_LENGTH '(' expression_list ')'
		{{ DBG_TRACE_GRAMMAR(reserved_func, | JSON_LENGTH '(' expression_list ')' );
                    $$ = parser_make_func_with_arg_count (this_parser, F_JSON_LENGTH, $3, 1, 2);
                    PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
		DBG_PRINT}}
        | JSON_MERGE '(' expression_list ')'
		{{ DBG_TRACE_GRAMMAR(reserved_func, | JSON_MERGE '(' expression_list ')' );
                    $$ = parser_make_func_with_arg_count (this_parser, F_JSON_MERGE, $3, 2, 0);
		    PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
		DBG_PRINT}}
        | JSON_MERGE_PATCH '(' expression_list ')'
		{{ DBG_TRACE_GRAMMAR(reserved_func, | JSON_MERGE_PATCH '(' expression_list ')' );
                    $$ = parser_make_func_with_arg_count (this_parser, F_JSON_MERGE_PATCH, $3, 2, 0);
		    PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
		DBG_PRINT}}
        | JSON_MERGE_PRESERVE '(' expression_list ')'
		{{ DBG_TRACE_GRAMMAR(reserved_func, | JSON_MERGE_PRESERVE '(' expression_list ')' );
                    $$ = parser_make_func_with_arg_count (this_parser, F_JSON_MERGE, $3, 2, 0);
		    PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
		DBG_PRINT}}
        | JSON_OBJECT_LEX '(' opt_expression_list ')'
		{{ DBG_TRACE_GRAMMAR(reserved_func, | JSON_OBJECT_LEX '(' opt_expression_list ')');
                    $$ = parser_make_func_with_arg_count_mod2 (this_parser, F_JSON_OBJECT, $3, 0, 0, 0);
		    PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
		DBG_PRINT}}
        | JSON_PRETTY '(' expression_list ')'
		{{ DBG_TRACE_GRAMMAR(reserved_func, | JSON_PRETTY '(' expression_list ')');
                    $$ = parser_make_func_with_arg_count (this_parser, F_JSON_PRETTY, $3, 1, 1);
		    PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
		DBG_PRINT}}
        | JSON_QUOTE '(' expression_list ')'
		{{ DBG_TRACE_GRAMMAR(reserved_func, | JSON_QUOTE '(' expression_list ')');
                    $$ = parser_make_func_with_arg_count (this_parser, F_JSON_QUOTE, $3, 1, 1);
		    PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
		DBG_PRINT}}
        | JSON_REMOVE '(' expression_list ')'
		{{ DBG_TRACE_GRAMMAR(reserved_func, | JSON_REMOVE '(' expression_list ')');
                    $$ = parser_make_func_with_arg_count (this_parser, F_JSON_REMOVE, $3, 2, 0);
		    PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
		DBG_PRINT}}
        | JSON_REPLACE '(' expression_list ')'
		{{ DBG_TRACE_GRAMMAR(reserved_func, | JSON_REPLACE '(' expression_list ')');
                    $$ = parser_make_func_with_arg_count_mod2 (this_parser, F_JSON_REPLACE, $3, 3, 0, 1);
		    PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
		DBG_PRINT}}
        | JSON_SET '(' expression_list ')'
		{{ DBG_TRACE_GRAMMAR(reserved_func, | JSON_SET '(' expression_list ')');
                    $$ = parser_make_func_with_arg_count_mod2 (this_parser, F_JSON_SET, $3, 3, 0, 1);
		    PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
		DBG_PRINT}}
        | JSON_SEARCH '(' expression_list ')'
		{{ DBG_TRACE_GRAMMAR(reserved_func, | JSON_SEARCH '(' expression_list ')');
                    $$ = parser_make_func_with_arg_count (this_parser, F_JSON_SEARCH, $3, 3, 0);
		    PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
		DBG_PRINT}}
        | JSON_TYPE '(' expression_list ')'
		{{ DBG_TRACE_GRAMMAR(reserved_func, | JSON_TYPE '(' expression_list ')');
                    $$ = parser_make_func_with_arg_count (this_parser, F_JSON_TYPE, $3, 1, 1);
		    PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
		DBG_PRINT}}
        | JSON_UNQUOTE '(' expression_list ')'
		{{ DBG_TRACE_GRAMMAR(reserved_func, | JSON_UNQUOTE '(' expression_list ')' );
                    $$ = parser_make_func_with_arg_count (this_parser, F_JSON_UNQUOTE, $3, 1, 1);
		    PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
		DBG_PRINT}}
        | JSON_VALID '(' expression_list ')'
		{{ DBG_TRACE_GRAMMAR(reserved_func, | JSON_VALID '(' expression_list ')' );
                    $$ = parser_make_func_with_arg_count (this_parser, F_JSON_VALID, $3, 1, 1);
		    PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
		DBG_PRINT}}
        | simple_path_id RIGHT_ARROW CHAR_STRING
		{{ DBG_TRACE_GRAMMAR(reserved_func, | simple_path_id RIGHT_ARROW CHAR_STRING);
		    PT_NODE *matcher = parser_new_node (this_parser, PT_VALUE);
                    if (matcher != NULL)
                      {
                        matcher->type_enum = PT_TYPE_CHAR;
                        matcher->info.value.string_type = ' ';
                        matcher->info.value.data_value.str = pt_append_bytes (this_parser, NULL, $3, strlen ($3));
                        PT_NODE_PRINT_VALUE_TO_TEXT (this_parser, matcher);
                      }
                    PT_NODE *first_arg = $1;
                    first_arg->next = matcher;
		    $$ = parser_make_expr_with_func (this_parser, F_JSON_EXTRACT, first_arg);
                    PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
                DBG_PRINT}}
        | simple_path_id DOUBLE_RIGHT_ARROW CHAR_STRING
		{{ DBG_TRACE_GRAMMAR(reserved_func, | simple_path_id DOUBLE_RIGHT_ARROW CHAR_STRING);
                    PT_NODE *matcher = parser_new_node (this_parser, PT_VALUE);
                    if (matcher != NULL)
                      {
                        matcher->type_enum = PT_TYPE_CHAR;
                        matcher->info.value.string_type = ' ';
                        matcher->info.value.data_value.str = pt_append_bytes (this_parser, NULL, $3, strlen ($3));
                        PT_NODE_PRINT_VALUE_TO_TEXT (this_parser, matcher);
                      }
                    PT_NODE *first_arg = $1;
                    first_arg->next = matcher;
		    PT_NODE *extract_expr = parser_make_expr_with_func (this_parser, F_JSON_EXTRACT, first_arg);
		    $$ = parser_make_expr_with_func (this_parser, F_JSON_UNQUOTE, extract_expr);
                    PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
		DBG_PRINT}}
        | BENCHMARK '(' expression_list ')'
		{{ DBG_TRACE_GRAMMAR(reserved_func, | BENCHMARK '(' expression_list ')');
                    $$ = parser_make_func_with_arg_count (this_parser, F_BENCHMARK, $3, 2, 2);
		    PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
		DBG_PRINT}}
	| REGEXP_COUNT '(' expression_list ')'
		{{ DBG_TRACE_GRAMMAR(reserved_func, | REGEXP_COUNT '(' expression_list ')');
			$$ = parser_make_func_with_arg_count (this_parser, F_REGEXP_COUNT, $3, 2, 4);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
		DBG_PRINT}}
	| REGEXP_INSTR '(' expression_list ')'
		{{ DBG_TRACE_GRAMMAR(reserved_func, | REGEXP_INSTR '(' expression_list ')');
			$$ = parser_make_func_with_arg_count (this_parser, F_REGEXP_INSTR, $3, 2, 6);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
		DBG_PRINT}}
	| REGEXP_LIKE '(' expression_list ')'
		{{ DBG_TRACE_GRAMMAR(reserved_func, | REGEXP_LIKE '(' expression_list ')');
			$$ = parser_make_func_with_arg_count (this_parser, F_REGEXP_LIKE, $3, 2, 3);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
		DBG_PRINT}}
	| REGEXP_REPLACE '(' expression_list ')'
		{{ DBG_TRACE_GRAMMAR(reserved_func, | REGEXP_REPLACE '(' expression_list ')');
			$$ = parser_make_func_with_arg_count (this_parser, F_REGEXP_REPLACE, $3, 3, 6);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
		DBG_PRINT}}
	| REGEXP_SUBSTR '(' expression_list ')'
		{{ DBG_TRACE_GRAMMAR(reserved_func, | REGEXP_SUBSTR '(' expression_list ')');
			$$ = parser_make_func_with_arg_count (this_parser, F_REGEXP_SUBSTR, $3, 2, 5);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
		DBG_PRINT}}
	;

of_cume_dist_percent_rank_function
	: CUME_DIST
		{{ DBG_TRACE_GRAMMAR(of_cume_dist_percent_rank_function, : CUME_DIST);
			$$ = PT_CUME_DIST;
		DBG_PRINT}}

	| PERCENT_RANK
		{{ DBG_TRACE_GRAMMAR(of_cume_dist_percent_rank_function, | PERCENT_RANK);
			$$ = PT_PERCENT_RANK;
		DBG_PRINT}}
    ;

of_current_date
	: CURRENT_DATE
	| CURRENT_DATE
		{ push_msg(MSGCAT_SYNTAX_INVALID_CURRENT_DATE); }
	  '(' ')'
		{ pop_msg(); }
	;

of_current_time
	: CURRENT_TIME
	| CURRENT_TIME
		{ push_msg(MSGCAT_SYNTAX_INVALID_CURRENT_TIME); }
	  '(' ')'
		{ pop_msg(); }
	;

of_db_timezone_
	: DB_TIMEZONE
	| DB_TIMEZONE
	      { push_msg(MSGCAT_SYNTAX_INVALID_DB_TIMEZONE); }
	  '(' ')'
	      { pop_msg(); }
	;

of_session_timezone_
	: SESSION_TIMEZONE
	| SESSION_TIMEZONE
	      { push_msg(MSGCAT_SYNTAX_INVALID_SESSION_TIMEZONE); }
	  '('')'
	      { pop_msg(); }
	;

of_current_timestamps
	: CURRENT_TIMESTAMP
	| CURRENT_TIMESTAMP
		{ push_msg(MSGCAT_SYNTAX_INVALID_CURRENT_TIMESTAMP); }
	  '(' ')'
		{ pop_msg(); }
	| LOCALTIMESTAMP
	| LOCALTIMESTAMP
		{ push_msg(MSGCAT_SYNTAX_INVALID_LOCALTIMESTAMP); }
	  '(' ')'
		{ pop_msg(); }
	| LOCALTIME
	| LOCALTIME
		{ push_msg(MSGCAT_SYNTAX_INVALID_LOCALTIME); }
	  '(' ')'
		{ pop_msg(); }
	;

of_current_datetime
	: CURRENT_DATETIME
	| CURRENT_DATETIME
		{ push_msg(MSGCAT_SYNTAX_INVALID_CURRENT_DATETIME); }
	  '(' ')'
		{ pop_msg(); }
	;
of_users
	: CURRENT_USER
	| SYSTEM_USER
	| USER
	;

of_avg_max_etc
	: AVG
		{{

			$$ = PT_AVG;

		DBG_PRINT}}
	| Max
		{{

			$$ = PT_MAX;

		DBG_PRINT}}
	| Min
		{{

			$$ = PT_MIN;

		DBG_PRINT}}
	| SUM
		{{

			$$ = PT_SUM;

		DBG_PRINT}}
	| STDDEV
		{{

			$$ = PT_STDDEV;

		DBG_PRINT}}
	| STDDEV_POP
		{{

			$$ = PT_STDDEV_POP;

		DBG_PRINT}}
	| STDDEV_SAMP
		{{

			$$ = PT_STDDEV_SAMP;

		DBG_PRINT}}
	| VAR_POP
		{{

			$$ = PT_VAR_POP;

		DBG_PRINT}}
	| VAR_SAMP
		{{

			$$ = PT_VAR_SAMP;

		DBG_PRINT}}
	| VARIANCE
		{{

			$$ = PT_VARIANCE;

		DBG_PRINT}}
	| BIT_AND
		{{

			$$ = PT_AGG_BIT_AND;

		DBG_PRINT}}
	| BIT_OR
		{{

			$$ = PT_AGG_BIT_OR;

		DBG_PRINT}}
	| BIT_XOR
		{{

			$$ = PT_AGG_BIT_XOR;

		DBG_PRINT}}
	| MEDIAN
		{{

			$$ = PT_MEDIAN;

		DBG_PRINT}}
	;

of_analytic
	: AVG
		{{

			$$ = PT_AVG;

		DBG_PRINT}}
	| Max
		{{

			$$ = PT_MAX;

		DBG_PRINT}}
	| Min
		{{

			$$ = PT_MIN;

		DBG_PRINT}}
	| SUM
		{{

			$$ = PT_SUM;

		DBG_PRINT}}
	| STDDEV
		{{

			$$ = PT_STDDEV;

		DBG_PRINT}}
	| STDDEV_POP
		{{

			$$ = PT_STDDEV_POP;

		DBG_PRINT}}
	| STDDEV_SAMP
		{{

			$$ = PT_STDDEV_SAMP;

		DBG_PRINT}}
	| VAR_POP
		{{

			$$ = PT_VAR_POP;

		DBG_PRINT}}
	| VAR_SAMP
		{{

			$$ = PT_VAR_SAMP;

		DBG_PRINT}}
	| VARIANCE
		{{

			$$ = PT_VARIANCE;

		DBG_PRINT}}
	| NTILE
		{{

			$$ = PT_NTILE;

		DBG_PRINT}}
	| MEDIAN
		{{

			$$ = PT_MEDIAN;

		DBG_PRINT}}
	/* add other analytic functions here */
	;

of_analytic_first_last
	: FIRST_VALUE
		{{

			$$ = PT_FIRST_VALUE;

		DBG_PRINT}}
	| LAST_VALUE
		{{

			$$ = PT_LAST_VALUE;

		DBG_PRINT}}
	;

of_analytic_nth_value
	: NTH_VALUE
		{{

			$$ = PT_NTH_VALUE;

		DBG_PRINT}}
	;

of_analytic_lead_lag
	: LEAD
		{{

			$$ = PT_LEAD;

		DBG_PRINT}}
	| LAG
		{{

			$$ = PT_LAG;

		DBG_PRINT}}
	/* functions that use other row values */
	;

of_percentile
	: PERCENTILE_CONT
		{{

			$$ = PT_PERCENTILE_CONT;

		DBG_PRINT}}
	| PERCENTILE_DISC
		{{

			$$ = PT_PERCENTILE_DISC;

		DBG_PRINT}}
	;

of_analytic_no_args
	: ROW_NUMBER
		{{

			$$ = PT_ROW_NUMBER;

		DBG_PRINT}}
	| RANK
		{{

			$$ = PT_RANK;

		DBG_PRINT}}
	| DENSE_RANK
		{{

			$$ = PT_DENSE_RANK;

		DBG_PRINT}}
	| CUME_DIST
		{{
			$$ = PT_CUME_DIST;

		DBG_PRINT}}
	| PERCENT_RANK
		{{
			$$ = PT_PERCENT_RANK;

		DBG_PRINT}}
	/* add other analytic functions here */
	;

of_distinct_unique
	: DISTINCT
	| UNIQUE
	;

opt_group_concat_separator
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_group_concat_separator, : );

			$$ = NULL;

		DBG_PRINT}}
	| SEPARATOR char_string
		{{ DBG_TRACE_GRAMMAR(opt_group_concat_separator, | SEPARATOR char_string);

			$$ = $2;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| SEPARATOR bit_string_literal
		{{ DBG_TRACE_GRAMMAR(opt_group_concat_separator, | SEPARATOR bit_string_literal);

			$$ = $2;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

opt_agg_order_by
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_agg_order_by, : );

			$$ = NULL;

		DBG_PRINT}}
	| ORDER BY sort_spec
		{{ DBG_TRACE_GRAMMAR(opt_agg_order_by, | ORDER BY sort_spec);

			$$ = $3;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

opt_analytic_from_last
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_analytic_from_last, : );

			$$ = false;

		DBG_PRINT}}
	| FROM FIRST
		{{ DBG_TRACE_GRAMMAR(opt_analytic_from_last, | FROM FIRST);

			$$ = false;

		DBG_PRINT}}
	| FROM LAST
		{{ DBG_TRACE_GRAMMAR(opt_analytic_from_last, | FROM LAST);

			$$ = true;

		DBG_PRINT}}
	;

opt_analytic_ignore_nulls
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_analytic_ignore_nulls, : );

			$$ = false;

		DBG_PRINT}}
	| RESPECT NULLS
		{{ DBG_TRACE_GRAMMAR(opt_analytic_ignore_nulls, | RESPECT NULLS);

			$$ = false;

		DBG_PRINT}}
	| IGNORE_ NULLS
		{{ DBG_TRACE_GRAMMAR(opt_analytic_ignore_nulls, | IGNORE_ NULLS);

			$$ = true;

		DBG_PRINT}}
	;

opt_analytic_partition_by
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_analytic_partition_by, : );

			$$ = NULL;

		DBG_PRINT}}
	| PARTITION BY sort_spec_list
		{{ DBG_TRACE_GRAMMAR(opt_analytic_partition_by, | PARTITION BY sort_spec_list);

			PT_NODE *list;
			$$ = $3;

			for (list = $3; list != NULL; list = list->next)
			  {
			    if (list->info.sort_spec.expr != NULL)
			      {
				list->info.sort_spec.expr->flag.do_not_fold = true;
			      }
			  }

			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

opt_over_analytic_partition_by
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_over_analytic_partition_by, : );

			is_analytic_function = false;
			$$ = NULL;

		DBG_PRINT}}
	| OVER '(' opt_analytic_partition_by ')'
		{{ DBG_TRACE_GRAMMAR(opt_over_analytic_partition_by, | OVER '(' opt_analytic_partition_by ')' );

			is_analytic_function = true;
			$$= $3;

		DBG_PRINT}}
	;

opt_analytic_order_by
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_analytic_order_by, : );

			$$ = NULL;

		DBG_PRINT}}
	| ORDER BY sort_spec_list
		{{ DBG_TRACE_GRAMMAR(opt_analytic_order_by, | ORDER BY sort_spec_list );

			PT_NODE *list;
			$$ = $3;

			for (list = $3; list != NULL; list = list->next)
			  {
			    if (list->info.sort_spec.expr != NULL)
			      {
				list->info.sort_spec.expr->flag.do_not_fold = true;
			      }
			  }
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

of_leading_trailing_both
	: LEADING_
		{{ DBG_TRACE_GRAMMAR(of_leading_trailing_both, : LEADING_ );

			$$ = PT_LEADING;

		DBG_PRINT}}
	| TRAILING_
		{{ DBG_TRACE_GRAMMAR(of_leading_trailing_both, | TRAILING_);

			$$ = PT_TRAILING;

		DBG_PRINT}}
	| BOTH_
		{{ DBG_TRACE_GRAMMAR(of_leading_trailing_both, | BOTH_ );

			$$ = PT_BOTH;

		DBG_PRINT}}
	;

case_expr
	: NULLIF '(' expression_ ',' expression_ ')'
		{{ DBG_TRACE_GRAMMAR(case_expr, : NULLIF '(' expression_ ',' expression_ ')' );
			$$ = parser_make_expression (this_parser, PT_NULLIF, $3, $5, NULL);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
		DBG_PRINT}}
	| COALESCE '(' expression_list ')'
		{{ DBG_TRACE_GRAMMAR(case_expr, | COALESCE '(' expression_list ')' );
			PT_NODE *prev, *expr, *arg, *tmp;
			int count = parser_count_list ($3);
			int i;
			arg = $3;

			expr = parser_new_node (this_parser, PT_EXPR);
			if (expr)
			  {
			    expr->info.expr.op = PT_COALESCE;
			    expr->info.expr.arg1 = arg;
			    expr->info.expr.arg2 = NULL;
			    expr->info.expr.arg3 = NULL;
			    expr->info.expr.continued_case = 1;
			  }

			PICE (expr);
			prev = expr;

			if (count > 1)
			  {
			    tmp = arg;
			    arg = arg->next;
			    tmp->next = NULL;
			    if (prev)
			      prev->info.expr.arg2 = arg;
			    PICE (prev);
			  }
			for (i = 3; i <= count; i++)
			  {
			    tmp = arg;
			    arg = arg->next;
			    tmp->next = NULL;

			    expr = parser_new_node (this_parser, PT_EXPR);
			    if (expr)
			      {
				expr->info.expr.op = PT_COALESCE;
				expr->info.expr.arg1 = prev;
				expr->info.expr.arg2 = arg;
				expr->info.expr.arg3 = NULL;
				expr->info.expr.continued_case = 1;
			      }
			    if (prev && prev->info.expr.continued_case >= 1)
			      prev->info.expr.continued_case++;
			    PICE (expr);
			    prev = expr;
			  }

			if (expr->info.expr.arg2 == NULL)
			  {
			    expr->info.expr.arg2 = parser_new_node (this_parser, PT_VALUE);

			    if (expr->info.expr.arg2)
			      {
				    expr->info.expr.arg2->type_enum = PT_TYPE_NULL;
				    expr->info.expr.arg2->flag.is_hidden_column = 1;
				    expr->info.expr.arg2->flag.is_added_by_parser = 1;
			      }
			  }

			$$ = expr;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| CASE expression_ simple_when_clause_list opt_else_expr END
		{{ DBG_TRACE_GRAMMAR(case_expr, | CASE expression_ simple_when_clause_list opt_else_expr END );

			int i;
			PT_NODE *case_oper = $2;
			PT_NODE *node, *prev, *tmp, *curr, *p;

			int count = parser_count_list ($3);
			node = prev = $3;
			if (node)
			  node->info.expr.continued_case = 0;

			tmp = $3;
			do
			  {
			    (tmp->info.expr.arg3)->info.expr.arg1 =
			      parser_copy_tree_list (this_parser, case_oper);
			  }
			while ((tmp = tmp->next))
			  ;

			curr = node;
			for (i = 2; i <= count; i++)
			  {
			    curr = curr->next;
			    if (curr)
			      curr->info.expr.continued_case = 1;
			    if (prev)
			      prev->info.expr.arg2 = curr;	/* else res */
			    PICE (prev);
			    prev->next = NULL;
			    prev = curr;
			  }

			p = $4;
			if (prev)
			  prev->info.expr.arg2 = p;
			PICE (prev);

			if (prev && !prev->info.expr.arg2)
			  {
			    p = parser_new_node (this_parser, PT_VALUE);
			    if (p)
			      {
				    p->type_enum = PT_TYPE_NULL;
				    p->flag.is_added_by_parser = 1;
			      }
			    prev->info.expr.arg2 = p;
			    PICE (prev);
			  }

			if (case_oper)
			  parser_free_node (this_parser, case_oper);

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| CASE searched_when_clause_list opt_else_expr END
		{{ DBG_TRACE_GRAMMAR(case_expr, | CASE searched_when_clause_list opt_else_expr END );

			int i;
			PT_NODE *node, *prev, *curr, *p;

			int count = parser_count_list ($2);
			node = prev = $2;
			if (node)
			  node->info.expr.continued_case = 0;

			curr = node;
			for (i = 2; i <= count; i++)
			  {
			    curr = curr->next;
			    if (curr)
			      curr->info.expr.continued_case = 1;
			    if (prev)
			      prev->info.expr.arg2 = curr;	/* else res */
			    PICE (prev);
			    prev->next = NULL;
			    prev = curr;
			  }

			p = $3;
			if (prev)
			  prev->info.expr.arg2 = p;
			PICE (prev);

			if (prev && !prev->info.expr.arg2)
			  {
			    p = parser_new_node (this_parser, PT_VALUE);
			    if (p)
			      {
				    p->type_enum = PT_TYPE_NULL;
				    p->flag.is_added_by_parser = 1;
			      }
			    prev->info.expr.arg2 = p;
			    PICE (prev);
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

opt_else_expr
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_else_expr, : );

			$$ = NULL;

		DBG_PRINT}}
	| ELSE expression_
		{{ DBG_TRACE_GRAMMAR(opt_else_expr, | ELSE expression_);

			$$ = $2;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

simple_when_clause_list
	: simple_when_clause_list simple_when_clause
		{{ DBG_TRACE_GRAMMAR(simple_when_clause_list, : simple_when_clause_list simple_when_clause);

			$$ = parser_make_link ($1, $2);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| simple_when_clause
		{{ DBG_TRACE_GRAMMAR(simple_when_clause_list, | simple_when_clause);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

simple_when_clause
	: WHEN expression_ THEN expression_
		{{ DBG_TRACE_GRAMMAR(simple_when_clause, : WHEN expression_ THEN expression_);

			PT_NODE *node, *p, *q;
			p = $2;
			node = parser_new_node (this_parser, PT_EXPR);
			if (node)
			  {
			    node->info.expr.op = PT_CASE;
			    node->info.expr.qualifier = PT_SIMPLE_CASE;
			    q = parser_new_node (this_parser, PT_EXPR);
			    if (q)
			      {
				q->info.expr.op = PT_EQ;
				q->info.expr.arg2 = p;
				node->info.expr.arg3 = q;
				PICE (q);
			      }
			  }

			p = $4;
			if (node)
			  node->info.expr.arg1 = p;
			PICE (node);

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

searched_when_clause_list
	: searched_when_clause_list searched_when_clause
		{{ DBG_TRACE_GRAMMAR(searched_when_clause_list, : searched_when_clause_list searched_when_clause);

			$$ = parser_make_link ($1, $2);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| searched_when_clause
		{{ DBG_TRACE_GRAMMAR(searched_when_clause_list, | searched_when_clause);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

searched_when_clause
	: WHEN search_condition THEN expression_
		{{ DBG_TRACE_GRAMMAR(searched_when_clause, : WHEN search_condition THEN expression_);

			PT_NODE *node, *p;
			node = parser_new_node (this_parser, PT_EXPR);
			if (node)
			  {
			    node->info.expr.op = PT_CASE;
			    node->info.expr.qualifier = PT_SEARCHED_CASE;
			  }

			p = $2;
			if (node)
			  node->info.expr.arg3 = p;
			PICE (node);

			p = $4;
			if (node)
			  node->info.expr.arg1 = p;
			PICE (node);

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;


extract_expr
	: EXTRACT '(' datetime_field FROM expression_ ')'
		{{ DBG_TRACE_GRAMMAR(extract_expr, : EXTRACT '(' datetime_field FROM expression_ ')');

			PT_NODE *tmp;
			tmp = parser_make_expression (this_parser, PT_EXTRACT, $5, NULL, NULL);
			if (tmp)
			  tmp->info.expr.qualifier = $3;
			$$ = tmp;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

adddate_name
	: DATE_ADD
	| ADDDATE
	;

subdate_name
	: DATE_SUB
	| SUBDATE
	;

datetime_field
	: YEAR_
		{{

			$$ = PT_YEAR;

		DBG_PRINT}}
	| MONTH_
		{{

			$$ = PT_MONTH;

		DBG_PRINT}}
	| DAY_
		{{

			$$ = PT_DAY;

		DBG_PRINT}}
	| HOUR_
		{{

			$$ = PT_HOUR;

		DBG_PRINT}}
	| MINUTE_
		{{

			$$ = PT_MINUTE;

		DBG_PRINT}}
	| SECOND_
		{{

			$$ = PT_SECOND;

		DBG_PRINT}}
	| MILLISECOND_
		{{

			$$ = PT_MILLISECOND;

		DBG_PRINT}}
	| WEEK
		{{

			$$ = PT_WEEK;

		DBG_PRINT}}
	| QUARTER
		{{

			$$ = PT_QUARTER;

    		DBG_PRINT}}
        | SECOND_MILLISECOND
		{{

			$$ = PT_SECOND_MILLISECOND;

    		DBG_PRINT}}
	| MINUTE_MILLISECOND
		{{

			$$ = PT_MINUTE_MILLISECOND;

    		DBG_PRINT}}
	| MINUTE_SECOND
		{{

			$$ = PT_MINUTE_SECOND;

    		DBG_PRINT}}
	| HOUR_MILLISECOND
		{{

			$$ = PT_HOUR_MILLISECOND;

    		DBG_PRINT}}
	| HOUR_SECOND
		{{

			$$ = PT_HOUR_SECOND;

    		DBG_PRINT}}
	| HOUR_MINUTE
		{{

			$$ = PT_HOUR_MINUTE;

    		DBG_PRINT}}
	| DAY_MILLISECOND
		{{

			$$ = PT_DAY_MILLISECOND;

    		DBG_PRINT}}
	| DAY_SECOND
		{{

			$$ = PT_DAY_SECOND;

    		DBG_PRINT}}
	| DAY_MINUTE
		{{

			$$ = PT_DAY_MINUTE;

    		DBG_PRINT}}
	| DAY_HOUR
		{{

			$$ = PT_DAY_HOUR;

    		DBG_PRINT}}
	| YEAR_MONTH
		{{

			$$ = PT_YEAR_MONTH;

    		DBG_PRINT}}
	;

opt_on_target
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_on_target, : );

			$$ = NULL;

		DBG_PRINT}}
	| ON_ primary
		{{ DBG_TRACE_GRAMMAR(opt_on_target, | ON_ primary);

			$$ = $2;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

generic_function
	: identifier '(' opt_expression_list ')' opt_on_target
		{{ DBG_TRACE_GRAMMAR(generic_function, : identifier '(' opt_expression_list ')' opt_on_target );

			PT_NODE *node = NULL;
			if ($5 == NULL)
			  node = parser_keyword_func ($1->info.name.original, $3);

			if (node == NULL)
			  {
			    node = parser_new_node (this_parser, PT_METHOD_CALL);

			    if (node)
			      {
				node->info.method_call.method_name = $1;
				node->info.method_call.arg_list = $3;
				node->info.method_call.on_call_target = $5;
				node->info.method_call.call_or_expr = PT_IS_MTHD_EXPR;
			      }
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

generic_function_id
	: generic_function
		{{ DBG_TRACE_GRAMMAR(generic_function_id, : generic_function );

			PT_NODE *node = $1;

			if (node && node->node_type == PT_METHOD_CALL)
			  {
			    if (!node->info.method_call.on_call_target)
			      {
				const char *callee;
				PT_NODE *name = node->info.method_call.method_name;
				PT_NODE *func_node = NULL;

				/* create new PT_FUNCTION node */
				func_node = parser_new_node (this_parser, PT_FUNCTION);
				if (func_node)
				  {
				    func_node->info.function.arg_list = node->info.method_call.arg_list;
				    func_node->info.function.function_type = PT_GENERIC;
				    callee = (name ? name->info.name.original : "");
				    func_node->info.function.generic_name = callee;

				    /* free previous PT_METHOD_CALL node */
				    node->info.method_call.arg_list = NULL;
				    node->info.method_call.method_name = NULL;
				    parser_free_node (this_parser, node);

				    node = func_node;
				  }
			      }

			    parser_cannot_prepare = true;
			    parser_cannot_cache = true;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

opt_expression_list
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_expression_list, : );

			$$ = NULL;

		DBG_PRINT}}
	| expression_list
		{{ DBG_TRACE_GRAMMAR(opt_expression_list, | expression_list );

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

table_set_function_call
	: SET subquery
		{{ DBG_TRACE_GRAMMAR(table_set_function_call, : SET subquery );

			PT_NODE *func_node;
			func_node = parser_new_node (this_parser, PT_FUNCTION);
			if (func_node)
			  {
			    func_node->info.function.arg_list = $2;
			    func_node->info.function.function_type = F_TABLE_SET;
			  }
			$$ = func_node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| SEQUENCE subquery
		{{ DBG_TRACE_GRAMMAR(table_set_function_call, | SEQUENCE subquery );

			PT_NODE *func_node;
			func_node = parser_new_node (this_parser, PT_FUNCTION);
			if (func_node)
			  {
			    func_node->info.function.arg_list = $2;
			    func_node->info.function.function_type = F_TABLE_SEQUENCE;
			  }
			$$ = func_node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| LIST subquery
		{{ DBG_TRACE_GRAMMAR(table_set_function_call, | LIST subquery );

			PT_NODE *func_node;
			func_node = parser_new_node (this_parser, PT_FUNCTION);
			if (func_node)
			  {
			    func_node->info.function.arg_list = $2;
			    func_node->info.function.function_type = F_TABLE_SEQUENCE;
			  }
			$$ = func_node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| MULTISET subquery
		{{ DBG_TRACE_GRAMMAR(table_set_function_call, | MULTISET subquery );

			PT_NODE *func_node;
			func_node = parser_new_node (this_parser, PT_FUNCTION);
			if (func_node)
			  {
			    func_node->info.function.arg_list = $2;
			    func_node->info.function.function_type = F_TABLE_MULTISET;
			  }
			$$ = func_node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

search_condition
	: search_condition OR boolean_term_xor
		{{DBG_TRACE_GRAMMAR(search_condition, : search_condition OR boolean_term_xor);
			PT_NODE *arg1 = pt_check_non_logical_expr(this_parser, $1);
			PT_NODE *arg2 = pt_check_non_logical_expr(this_parser, $3);
			$$ = parser_make_expression (this_parser, PT_OR, arg1, arg2, NULL);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| boolean_term_xor
		{{DBG_TRACE_GRAMMAR(search_condition, | boolean_term_xor);
			$$ = pt_check_non_logical_expr(this_parser, $1);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

boolean_term_xor
	: boolean_term_xor XOR boolean_term_is
		{{DBG_TRACE_GRAMMAR(boolean_term_xor, : boolean_term_xor XOR boolean_term_is);
			PT_NODE *arg1 = pt_check_non_logical_expr(this_parser, $1);
			PT_NODE *arg2 = pt_check_non_logical_expr(this_parser, $3);
			$$ = parser_make_expression (this_parser, PT_XOR, arg1, arg2, NULL);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| boolean_term_is
		{{DBG_TRACE_GRAMMAR(boolean_term_xor, | boolean_term_is);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
		DBG_PRINT}}
	;

boolean_term_is
	: boolean_term_is is_op boolean
		{{DBG_TRACE_GRAMMAR(boolean_term_xor, : boolean_term_is);
	                PT_NODE *arg = pt_check_non_logical_expr(this_parser, $1);
			$$ = parser_make_expression (this_parser, $2, arg, $3, NULL);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| boolean_term
		{{DBG_TRACE_GRAMMAR(boolean_term_xor, | boolean_term_is);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

is_op
	: IS NOT
		{{ DBG_TRACE_GRAMMAR(is_op, : IS NOT);

			$$ = PT_IS_NOT;

		DBG_PRINT}}
	| IS
		{{ DBG_TRACE_GRAMMAR(is_op, | IS);

			$$ = PT_IS;

		DBG_PRINT}}
	;

boolean_term
	: boolean_term AND boolean_factor
		{{DBG_TRACE_GRAMMAR(boolean_term, : boolean_term AND boolean_factor);
			PT_NODE *arg1 = pt_check_non_logical_expr(this_parser, $1);
			PT_NODE *arg2 = pt_check_non_logical_expr(this_parser, $3);
			$$ = parser_make_expression (this_parser, PT_AND, arg1, arg2, NULL);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| boolean_factor
		{{DBG_TRACE_GRAMMAR(boolean_term, | boolean_factor);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

boolean_factor
	: NOT predicate
		{{DBG_TRACE_GRAMMAR(boolean_factor, : NOT predicate);

			PT_NODE *arg = pt_check_non_logical_expr(this_parser, $2);
			$$ = parser_make_expression (this_parser, PT_NOT, arg, NULL, NULL);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| '!' predicate
		{{DBG_TRACE_GRAMMAR(boolean_factor, | '!' predicate);

			PT_NODE *arg = pt_check_non_logical_expr(this_parser, $2);
			$$ = parser_make_expression (this_parser, PT_NOT, arg, NULL, NULL);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| predicate
		{{DBG_TRACE_GRAMMAR(boolean_factor, | predicate);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

predicate
	: EXISTS expression_
		{{ DBG_TRACE_GRAMMAR(predicate, : EXISTS expression_);
			$$ = parser_make_expression (this_parser, PT_EXISTS, $2, NULL, NULL);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| expression_
		{{ DBG_TRACE_GRAMMAR(predicate, | expression_);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

predicate_expression
	: predicate_expr_sub
		{{ DBG_TRACE_GRAMMAR(predicate_expression, : predicate_expr_sub);

			PT_JOIN_TYPE join_type = parser_top_join_type ();
			if (join_type == PT_JOIN_RIGHT_OUTER)
			  parser_restore_wjc ();

		DBG_PRINT}}
	  opt_paren_plus
		{{ DBG_TRACE_GRAMMAR(predicate_expression, opt_paren_plus);

			PT_JOIN_TYPE join_type = parser_pop_join_type ();
			PT_NODE *e, *attr;

			if ($3)
			  {
			    if (join_type == PT_JOIN_RIGHT_OUTER)
			      join_type = PT_JOIN_FULL_OUTER;
			    else
			      join_type = PT_JOIN_LEFT_OUTER;
			  }

			/*
			 * marking Oracle style left/right outer join operator
			 *
			 * Oracle style outer join support: convert to ANSI standard style
			 * only permit the following predicate
			 *
			 * 'single_column(+) op expression_'
			 * 'expression_   op single_column(+)'
			 */

			e = $1;

			if (join_type != PT_JOIN_NONE)
			  {
			    if (e && e->node_type == PT_EXPR)
			      {
				switch (join_type)
				  {
				  case PT_JOIN_LEFT_OUTER:
				    attr = e->info.expr.arg2;
				    break;
				  case PT_JOIN_RIGHT_OUTER:
				    attr = e->info.expr.arg1;
				    break;
				  case PT_JOIN_FULL_OUTER:
				    PT_ERROR (this_parser, e,
					      "a predicate may reference only one outer-joined table");
				    attr = NULL;
				    break;
				  default:
				    PT_ERROR (this_parser, e, "check syntax at '(+)'");
				    attr = NULL;
				    break;
				  }

				if (attr)
				  {
				    while (attr->node_type == PT_DOT_)
				      attr = attr->info.dot.arg2;

				    if (attr->node_type == PT_NAME)
				      {
					switch (join_type)
					  {
					  case PT_JOIN_LEFT_OUTER:
					    PT_EXPR_INFO_SET_FLAG (e, PT_EXPR_INFO_LEFT_OUTER);
					    parser_found_Oracle_outer = true;
					    break;
					  case PT_JOIN_RIGHT_OUTER:
					    PT_EXPR_INFO_SET_FLAG (e, PT_EXPR_INFO_RIGHT_OUTER);
					    parser_found_Oracle_outer = true;
					    break;
					  default:
					    break;
					  }
				      }
				    else
				      {
					PT_ERROR (this_parser, e,
						  "'(+)' operator can be applied only to a column, not to an arbitary expression");
				      }
				  }
			      }
			  }				/* if (join_type != PT_JOIN_INNER) */

			$$ = e;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;


predicate_expr_sub
	: pred_lhs comp_op normal_expression
		{{ DBG_TRACE_GRAMMAR(predicate_expr_sub, : pred_lhs comp_op normal_expression);

			PT_NODE *e, *opd1, *opd2, *subq, *t;
			PT_OP_TYPE op;
			bool found_paren_set_expr = false;
			int lhs_cnt, rhs_cnt = 0;
			bool found_match = false;

			opd2 = $3;
			e = parser_make_expression (this_parser, $2, $1, NULL, NULL);

			if (e && !pt_has_error (this_parser))
			  {

			    e->info.expr.arg2 = opd2;
			    opd1 = e->info.expr.arg1;
			    op = e->info.expr.op;

			    /* convert parentheses set expr value into sequence */
			    if (opd1)
			      {
				if (opd1->node_type == PT_VALUE &&
				    opd1->type_enum == PT_TYPE_EXPR_SET)
				  {
				    opd1->type_enum = PT_TYPE_SEQUENCE;
				    found_paren_set_expr = true;
				  }
				else if (PT_IS_QUERY_NODE_TYPE (opd1->node_type))
				  {
				    if ((subq = pt_get_subquery_list (opd1)) && subq->next == NULL)
				      {
					/* single-column subquery */
				      }
				    else
				      {
					if (subq)
					  {
					    /* If not PT_TYPE_STAR */
					    pt_select_list_to_one_col (this_parser, opd1, true);
					  }
					found_paren_set_expr = true;
				      }
				  }
			      }
			    if (opd2)
			      {
				if (opd2->node_type == PT_VALUE &&
				    opd2->type_enum == PT_TYPE_EXPR_SET)
				  {
				    opd2->type_enum = PT_TYPE_SEQUENCE;
				    found_paren_set_expr = true;
				  }
				else if (PT_IS_QUERY_NODE_TYPE (opd2->node_type))
				  {
				    if ((subq = pt_get_subquery_list (opd2)) && subq->next == NULL)
				      {
					/* single-column subquery */
				      }
				    else
				      {
					if (subq)
					  {
					    /* If not PT_TYPE_STAR */
					    pt_select_list_to_one_col (this_parser, opd2, true);
					  }
					found_paren_set_expr = true;
				      }
				  }
			      }
                              if (found_paren_set_expr == true)
                              {
                                /* expression number check */
                                if ((lhs_cnt = pt_get_expression_count (opd1)) < 0)
                                  {
                                    found_match = true;
                                  }
                                else
                                  {
                                    for (t = opd2; t; t = t->next)
                                      {
                                        rhs_cnt = pt_get_expression_count (t);
                                        if ((rhs_cnt < 0) || (lhs_cnt == rhs_cnt))
                                          {
                                            /* can not check negative rhs_cnt. go ahead */
                                            found_match = true;
                                            break;
                                          }
                                      }
                                  }

                                if (found_match == false)
                                  {
                                    PT_ERRORmf2 (this_parser, e, MSGCAT_SET_PARSER_SEMANTIC,
                                                 MSGCAT_SEMANTIC_ATT_CNT_COL_CNT_NE,
                                                 lhs_cnt, rhs_cnt);
                                  }
                              }
			    if (op == PT_EQ || op == PT_NE || op == PT_GT || op == PT_GE || op == PT_LT || op == PT_LE)
			      {
				/* expression number check */
				if (found_paren_set_expr == true &&
				    pt_check_set_count_set (this_parser, opd1, opd2))
				  {
				    /* rewrite parentheses set expr equi-comparions predicate
				     * as equi-comparison predicates tree of each elements.
				     * for example, (a, b) = (x, y) -> a = x and b = y
				     */
				    if (op == PT_EQ && pt_is_set_type (opd1) && pt_is_set_type (opd2))
				      {
					e = pt_rewrite_set_eq_set (this_parser, e);
				      }
				  }
				/* mark as single tuple list */
				if (PT_IS_QUERY_NODE_TYPE (opd1->node_type))
				  {
				    opd1->info.query.flag.single_tuple = 1;
				  }
				if (PT_IS_QUERY_NODE_TYPE (opd2->node_type))
				  {
				    opd2->info.query.flag.single_tuple = 1;
				  }
			      }
			    else
			      {
				if (found_paren_set_expr == true)
				  {			/* operator check */
				    PT_ERRORf (this_parser, e,
					       "check syntax at %s, illegal operator.",
					       pt_show_binopcode (op));
				  }
			      }
			  }				/* if (e) */
			PICE (e);

			$$ = e;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| pred_lhs like_op normal_expression ESCAPE escape_literal
		{{ DBG_TRACE_GRAMMAR(predicate_expr_sub, | pred_lhs like_op normal_expression ESCAPE escape_literal);

			PT_NODE *esc = parser_make_expression (this_parser, PT_LIKE_ESCAPE, $3, $5, NULL);
			PT_NODE *node = parser_make_expression (this_parser, $2, $1, esc, NULL);
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| pred_lhs like_op normal_expression
		{{ DBG_TRACE_GRAMMAR(predicate_expr_sub, | pred_lhs like_op normal_expression);

 			if (prm_get_bool_value (PRM_ID_REQUIRE_LIKE_ESCAPE_CHARACTER)
 			    && prm_get_bool_value (PRM_ID_NO_BACKSLASH_ESCAPES))
 			  {
 			    PT_ERRORmf2 (this_parser, $1, MSGCAT_SET_PARSER_SEMANTIC,
 			                 MSGCAT_SEMANTIC_ESCAPE_CHAR_REQUIRED,
 			                 prm_get_name (PRM_ID_REQUIRE_LIKE_ESCAPE_CHARACTER),
 			                 prm_get_name (PRM_ID_NO_BACKSLASH_ESCAPES));
 			  }
			$$ = parser_make_expression (this_parser, $2, $1, $3, NULL);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| pred_lhs rlike_op normal_expression
		{{ DBG_TRACE_GRAMMAR(predicate_expr_sub, | pred_lhs rlike_op normal_expression);

			/* case sensitivity flag */
			PT_NODE *node = parser_new_node (this_parser, PT_VALUE);
			if (node)
			  {
			    node->type_enum = PT_TYPE_INTEGER;
			    node->info.value.data_value.i =
			      ($2 == PT_RLIKE_BINARY || $2 == PT_NOT_RLIKE_BINARY ? 1 : 0);

			    $$ = parser_make_expression (this_parser, $2, $1, $3, node);
			  }
			else
			  {
			    $$ = NULL;
			  }
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| pred_lhs null_op
		{{ DBG_TRACE_GRAMMAR(predicate_expr_sub, | pred_lhs null_op);

			$$ = parser_make_expression (this_parser, $2, $1, NULL, NULL);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| pred_lhs set_op normal_expression
		{{ DBG_TRACE_GRAMMAR(predicate_expr_sub, | pred_lhs set_op normal_expression);

			$$ = parser_make_expression (this_parser, $2, $1, $3, NULL);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| pred_lhs between_op normal_expression AND normal_expression
		{{ DBG_TRACE_GRAMMAR(predicate_expr_sub, | pred_lhs between_op normal_expression AND normal_expression);

			PT_NODE *node = parser_make_expression (this_parser, PT_BETWEEN_AND, $3, $5, NULL);
			$$ = parser_make_expression (this_parser, $2, $1, node, NULL);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| pred_lhs in_op in_pred_operand
		{{ DBG_TRACE_GRAMMAR(predicate_expr_sub, | pred_lhs in_op in_pred_operand);

			PT_NODE *node = parser_make_expression (this_parser, $2, $1, NULL, NULL);
			PT_NODE *t = CONTAINER_AT_1 ($3);
			bool is_paren = (bool)TO_NUMBER (CONTAINER_AT_0 ($3));
			int lhs_cnt, rhs_cnt = 0;
			PT_NODE *v, *lhs, *rhs, *subq;
			bool found_match = false;
			bool found_paren_set_expr = false;

			PARSER_SAVE_ERR_CONTEXT (node, @$.buffer_pos)
			if (node)
			  {
			    lhs = node->info.expr.arg1;
			    /* convert lhs parentheses set expr value into
			     * sequence value */
			    if (lhs)
			      {
				if (lhs->node_type == PT_VALUE && lhs->type_enum == PT_TYPE_EXPR_SET)
				  {
				    lhs->type_enum = PT_TYPE_SEQUENCE;
				    found_paren_set_expr = true;
				  }
				else if (PT_IS_QUERY_NODE_TYPE (lhs->node_type))
				  {
				    if ((subq = pt_get_subquery_list (lhs)) && subq->next == NULL)
				      {
					/* single column subquery */
				      }
				    else
				      {
					if (subq)
                                          {
                                            /* If not PT_TYPE_STAR */
                                            pt_select_list_to_one_col (this_parser, lhs, true);     
                                          }                                        
                                        found_paren_set_expr = true;
				      }
				  }
			      }

			    if (is_paren == true)
			      {				/* convert to multi-set */
				v = parser_new_node (this_parser, PT_VALUE);
				if (v)
				  {
				    v->info.value.data_value.set = t;
				    v->type_enum = PT_TYPE_MULTISET;
				  }			/* if (v) */
				node->info.expr.arg2 = v;
			      }
			    else
			      {
				/* convert subquery-starting parentheses set expr
				 * ( i.e., {subquery, x, y, ...} ) into multi-set */
				if (t->node_type == PT_VALUE && t->type_enum == PT_TYPE_EXPR_SET)
				  {
				    is_paren = true;	/* mark as parentheses set expr */
				    t->type_enum = PT_TYPE_MULTISET;
				  }
				node->info.expr.arg2 = t;
			      }

			    rhs = node->info.expr.arg2;
			    if (is_paren == true)
			      {
				rhs = rhs->info.value.data_value.set;
			      }
			    else if (rhs->node_type == PT_VALUE
				     && !(PT_IS_COLLECTION_TYPE (rhs->type_enum)
					  || rhs->type_enum == PT_TYPE_EXPR_SET))
			      {
				PT_ERRORmf2 (this_parser, rhs, MSGCAT_SET_PARSER_SYNTAX,
					     MSGCAT_SYNTAX_ERROR_MSG1,
					     pt_show_binopcode ($2),
					     "SELECT or '('");
			      }

			    /* for each rhs elements, convert parentheses
			     * set expr value into sequence value */
			    for (t = rhs; t; t = t->next)
			      {
				if (t->node_type == PT_VALUE && t->type_enum == PT_TYPE_EXPR_SET)
				  {
				    t->type_enum = PT_TYPE_SEQUENCE;
				    found_paren_set_expr = true;
				  }
				else if (PT_IS_QUERY_NODE_TYPE (t->node_type))
				  {
				    if ((subq = pt_get_subquery_list (t)) && subq->next == NULL)
				      {
					/* single column subquery */
				      }
				    else
				      {
                                        if (subq)
                                          {
                                            /* If not PT_TYPE_STAR */
                                            pt_select_list_to_one_col (this_parser, t, true);
                                          }
					found_paren_set_expr = true;
				      }
				  }
			      }
			    if (found_paren_set_expr == true)
			      {
				/* expression number check */
				if ((lhs_cnt = pt_get_expression_count (lhs)) < 0)
				  {
				    found_match = true;
				  }
				else
				  {
				    for (t = rhs; t; t = t->next)
				      {
					if (!PT_IS_QUERY_NODE_TYPE (t->node_type))
					  {
					    if (pt_is_set_type (t))
					      {
						if (pt_is_set_type (t->info.value.data_value.set))
						  {
						    /* syntax error case : (a,b) in ((1,1),((2,2),(3,3)) */
						    found_match = false;
						    rhs_cnt = 0;
						    break;
						  }
					      }
					    else
					      {
						/* syntax error case : (a,b) in ((1,1),2) */
						found_match = false;
						rhs_cnt = 0;
						break;
					      }
					  }
					rhs_cnt = pt_get_expression_count (t);
					if (rhs_cnt < 0)
					  {
					    /* can not check negative rhs_cnt. go ahead */
					    found_match = true;
					    break;
					  }
					else if (lhs_cnt != rhs_cnt)
					  {
					    found_match = false;
					    break;
					  }
					else
					  {
					    found_match = true;
					  }
				      }
				  }

				if (found_match == false)
				  {
				    PT_ERRORmf2 (this_parser, node, MSGCAT_SET_PARSER_SEMANTIC,
						 MSGCAT_SEMANTIC_ATT_CNT_COL_CNT_NE,
						 lhs_cnt, rhs_cnt);
				  }
			      }
			  }
			rhs = node->info.expr.arg2;
			if (PT_IS_COLLECTION_TYPE (rhs->type_enum) && rhs->info.value.data_value.set
			    && rhs->info.value.data_value.set->next == NULL)
			  {
			    /* only one element in set. convert expr as EQ/NE expr. */
			    PT_NODE *new_arg2;

			    new_arg2 = rhs->info.value.data_value.set;

			    /* free arg2 */
			    rhs->info.value.data_value.set = NULL;
			    parser_free_tree (this_parser, node->info.expr.arg2);

			    /* rewrite arg2 */
			    node->info.expr.arg2 = new_arg2;
			    node->info.expr.op = (node->info.expr.op == PT_IS_IN) ? PT_EQ : PT_NE;
			    if (node->info.expr.op == PT_EQ)
			      {
				node = pt_rewrite_set_eq_set (this_parser, node);
			      }
			  }

			$$ = node;

		DBG_PRINT}}
	;
	| pred_lhs RANGE_ '(' range_list ')'
		{{ DBG_TRACE_GRAMMAR(predicate_expr_sub, | pred_lhs RANGE_ '(' range_list ')');

			$$ = parser_make_expression (this_parser, PT_RANGE, $1, $4, NULL);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| pred_lhs IdName
		{{ DBG_TRACE_GRAMMAR(predicate_expr_sub, | pred_lhs IdName);
			PT_ERRORm (this_parser, $1, MSGCAT_SET_PARSER_SYNTAX,
				    MSGCAT_SYNTAX_INVALID_RELATIONAL_OP);

		DBG_PRINT}}
	;

pred_lhs
	: normal_expression opt_paren_plus
		{{ DBG_TRACE_GRAMMAR(pred_lhs, : normal_expression opt_paren_plus);

			PT_JOIN_TYPE join_type = PT_JOIN_NONE;

			if ($2)
			  {
			    join_type = PT_JOIN_RIGHT_OUTER;
			    parser_save_and_set_wjc (1);
			  }
			parser_push_join_type (join_type);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

opt_paren_plus
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_paren_plus, : );

			$$ = 0;

		DBG_PRINT}}
	| paren_plus
		{{ DBG_TRACE_GRAMMAR(opt_paren_plus, | paren_plus);

			$$ = 1;

		DBG_PRINT}}
	;

comp_op
	:  '=' opt_of_all_some_any
		{{ DBG_TRACE_GRAMMAR(comp_op, :  '=' opt_of_all_some_any);

			switch ($2)
			  {
			  case 0:
			    $$ = PT_EQ;
			    break;
			  case 1:
			    $$ = PT_EQ_ALL;
			    break;
			  case 2:
			    $$ = PT_EQ_SOME;
			    break;
			  case 3:
			    $$ = PT_EQ_SOME;
			    break;
			  }

		DBG_PRINT}}
	| COMP_NOT_EQ opt_of_all_some_any
		{{ DBG_TRACE_GRAMMAR(comp_op, | COMP_NOT_EQ opt_of_all_some_any);

			switch ($2)
			  {
			  case 0:
			    $$ = PT_NE;
			    break;
			  case 1:
			    $$ = PT_NE_ALL;
			    break;
			  case 2:
			    $$ = PT_NE_SOME;
			    break;
			  case 3:
			    $$ = PT_NE_SOME;
			    break;
			  }

		DBG_PRINT}}
	| '>' opt_of_all_some_any
		{{ DBG_TRACE_GRAMMAR(comp_op, | '>' opt_of_all_some_any);

			switch ($2)
			  {
			  case 0:
			    $$ = PT_GT;
			    break;
			  case 1:
			    $$ = PT_GT_ALL;
			    break;
			  case 2:
			    $$ = PT_GT_SOME;
			    break;
			  case 3:
			    $$ = PT_GT_SOME;
			    break;
			  }

		DBG_PRINT}}
	| COMP_GE opt_of_all_some_any
		{{ DBG_TRACE_GRAMMAR(comp_op, | COMP_GE opt_of_all_some_any);

			switch ($2)
			  {
			  case 0:
			    $$ = PT_GE;
			    break;
			  case 1:
			    $$ = PT_GE_ALL;
			    break;
			  case 2:
			    $$ = PT_GE_SOME;
			    break;
			  case 3:
			    $$ = PT_GE_SOME;
			    break;
			  }

		DBG_PRINT}}
	| '<'  opt_of_all_some_any
		{{ DBG_TRACE_GRAMMAR(comp_op, | '<'  opt_of_all_some_any);

			switch ($2)
			  {
			  case 0:
			    $$ = PT_LT;
			    break;
			  case 1:
			    $$ = PT_LT_ALL;
			    break;
			  case 2:
			    $$ = PT_LT_SOME;
			    break;
			  case 3:
			    $$ = PT_LT_SOME;
			    break;
			  }

		DBG_PRINT}}
	| COMP_LE opt_of_all_some_any
		{{ DBG_TRACE_GRAMMAR(comp_op, | COMP_LE opt_of_all_some_any);

			switch ($2)
			  {
			  case 0:
			    $$ = PT_LE;
			    break;
			  case 1:
			    $$ = PT_LE_ALL;
			    break;
			  case 2:
			    $$ = PT_LE_SOME;
			    break;
			  case 3:
			    $$ = PT_LE_SOME;
			    break;
			  }

		DBG_PRINT}}
	| '=''=' opt_of_all_some_any
		{{ DBG_TRACE_GRAMMAR(comp_op, | '=''=' opt_of_all_some_any);

			push_msg (MSGCAT_SYNTAX_INVALID_EQUAL_OP);
			csql_yyerror_explicit (@1.first_line, @1.first_column);

		DBG_PRINT}}
	| '!''=' opt_of_all_some_any
		{{ DBG_TRACE_GRAMMAR(comp_op, | '!''=' opt_of_all_some_any);

			push_msg (MSGCAT_SYNTAX_INVALID_NOT_EQUAL);
			csql_yyerror_explicit (@1.first_line, @1.first_column);

		DBG_PRINT}}
	| COMP_NULLSAFE_EQ opt_of_all_some_any
		{{ DBG_TRACE_GRAMMAR(comp_op, | COMP_NULLSAFE_EQ opt_of_all_some_any);

			$$ = PT_NULLSAFE_EQ;

		DBG_PRINT}}
	;

opt_of_all_some_any
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_of_all_some_any, : );

			$$ = 0;

		DBG_PRINT}}
	| ALL
		{{ DBG_TRACE_GRAMMAR(opt_of_all_some_any, | ALL );

			$$ = 1;

		DBG_PRINT}}
	| SOME
		{{ DBG_TRACE_GRAMMAR(opt_of_all_some_any, | SOME );

			$$ = 2;

		DBG_PRINT}}
	| ANY
		{{ DBG_TRACE_GRAMMAR(opt_of_all_some_any, | ANY );

			$$ = 3;

		DBG_PRINT}}
	;

like_op
	: NOT LIKE
		{{ DBG_TRACE_GRAMMAR(like_op, : NOT LIKE );

			$$ = PT_NOT_LIKE;

		DBG_PRINT}}
	| LIKE
		{{ DBG_TRACE_GRAMMAR(like_op, | LIKE );

			$$ = PT_LIKE;

		DBG_PRINT}}
	;

rlike_op
	: rlike_or_regexp
		{{ DBG_TRACE_GRAMMAR(rlike_op, : rlike_or_regexp);

			$$ = PT_RLIKE;

		DBG_PRINT}}
	| NOT rlike_or_regexp
		{{ DBG_TRACE_GRAMMAR(rlike_op, | NOT rlike_or_regexp);

			$$ = PT_NOT_RLIKE;

		DBG_PRINT}}
	| rlike_or_regexp BINARY
		{{ DBG_TRACE_GRAMMAR(rlike_op, | rlike_or_regexp BINARY );

			$$ = PT_RLIKE_BINARY;

		DBG_PRINT}}
	| NOT rlike_or_regexp BINARY
		{{ DBG_TRACE_GRAMMAR(rlike_op, | NOT rlike_or_regexp BINARY);

			$$ = PT_NOT_RLIKE_BINARY;

		DBG_PRINT}}
	;

rlike_or_regexp
	: RLIKE
	| REGEXP
	;

null_op
	: IS NOT Null
		{{ DBG_TRACE_GRAMMAR(null_op, : IS NOT Null);

			$$ = PT_IS_NOT_NULL;

		DBG_PRINT}}
	| IS Null
		{{ DBG_TRACE_GRAMMAR(null_op, | IS Null);

			$$ = PT_IS_NULL;

		DBG_PRINT}}
	;


between_op
	: NOT BETWEEN
		{{ DBG_TRACE_GRAMMAR(between_op, : NOT BETWEEN);

			$$ = PT_NOT_BETWEEN;

		DBG_PRINT}}
	| BETWEEN
		{{ DBG_TRACE_GRAMMAR(between_op, | BETWEEN);

			$$ = PT_BETWEEN;

		DBG_PRINT}}
	;

in_op
	: IN_
		{{ DBG_TRACE_GRAMMAR(in_op, : IN_);

			$$ = PT_IS_IN;

		DBG_PRINT}}
	| NOT IN_
		{{ DBG_TRACE_GRAMMAR(in_op, | NOT IN_);

			$$ = PT_IS_NOT_IN;

		DBG_PRINT}}
	;

in_pred_operand
	: expression_
		{{ DBG_TRACE_GRAMMAR(in_pred_operand, : expression_);
			container_2 ctn;
			PT_NODE *exp = $1;
			if (exp && exp->flag.is_paren == 0)
			  {
			    SET_CONTAINER_2 (ctn, FROM_NUMBER (0), exp);
			  }
			else
			  {
			    SET_CONTAINER_2 (ctn, FROM_NUMBER (1), exp);
			  }

			$$ = ctn;
		DBG_PRINT}}
	;

range_list
	: range_list OR range_
		{{ DBG_TRACE_GRAMMAR(range_list, : range_list OR range_);

			$$ = parser_make_link_or ($1, $3);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| range_
		{{ DBG_TRACE_GRAMMAR(range_list, | range_);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

range_
	: expression_ GE_LE_ expression_
		{{ DBG_TRACE_GRAMMAR(range_, : expression_ GE_LE_ expression_);

			$$ = parser_make_expression (this_parser, PT_BETWEEN_GE_LE, $1, $3, NULL);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| expression_ GE_LT_ expression_
		{{ DBG_TRACE_GRAMMAR(range_, | expression_ GE_LT_ expression_);

			$$ = parser_make_expression (this_parser, PT_BETWEEN_GE_LT, $1, $3, NULL);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| expression_ GT_LE_ expression_
		{{ DBG_TRACE_GRAMMAR(range_, | expression_ GT_LE_ expression_);

			$$ = parser_make_expression (this_parser, PT_BETWEEN_GT_LE, $1, $3, NULL);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| expression_ GT_LT_ expression_
		{{ DBG_TRACE_GRAMMAR(range_, | expression_ GT_LT_ expression_);

			$$ = parser_make_expression (this_parser, PT_BETWEEN_GT_LT, $1, $3, NULL);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| expression_ '='
		{{ DBG_TRACE_GRAMMAR(range_, | expression_ '=');

			$$ = parser_make_expression (this_parser, PT_BETWEEN_EQ_NA, $1, NULL, NULL);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| expression_ GE_INF_ Max
		{{ DBG_TRACE_GRAMMAR(range_, | expression_ GE_INF_ Max);

			$$ = parser_make_expression (this_parser, PT_BETWEEN_GE_INF, $1, NULL, NULL);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| expression_ GT_INF_ Max
		{{ DBG_TRACE_GRAMMAR(range_, | expression_ GT_INF_ Max);

			$$ = parser_make_expression (this_parser, PT_BETWEEN_GT_INF, $1, NULL, NULL);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| Min INF_LE_ expression_
		{{ DBG_TRACE_GRAMMAR(range_, | Min INF_LE_ expression_);

			$$ = parser_make_expression (this_parser, PT_BETWEEN_INF_LE, $3, NULL, NULL);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| Min INF_LT_ expression_
		{{ DBG_TRACE_GRAMMAR(range_, | Min INF_LT_ expression_);

			$$ = parser_make_expression (this_parser, PT_BETWEEN_INF_LT, $3, NULL, NULL);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

set_op
	: SETEQ
		{{

			$$ = PT_SETEQ;

		DBG_PRINT}}
	| SETNEQ
		{{

			$$ = PT_SETNEQ;

		DBG_PRINT}}
	| SUBSET
		{{

			$$ = PT_SUBSET;

		DBG_PRINT}}
	| SUBSETEQ
		{{

			$$ = PT_SUBSETEQ;

		DBG_PRINT}}
	| SUPERSETEQ
		{{

			$$ = PT_SUPERSETEQ;

		DBG_PRINT}}
	| SUPERSET
		{{

			$$ = PT_SUPERSET;

		DBG_PRINT}}
	;

subquery
	: '(' csql_query ')'
		{{ DBG_TRACE_GRAMMAR(subquery, : '(' csql_query ')');

			PT_NODE *stmt = $2;

			if (parser_within_join_condition)
			  {
			    PT_ERRORm (this_parser, stmt, MSGCAT_SET_PARSER_SYNTAX,
				       MSGCAT_SYNTAX_JOIN_COND_SUBQ);
			  }

			if (stmt)
			  stmt->info.query.is_subquery = PT_IS_SUBQUERY;
			$$ = stmt;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

subquery_without_subquery_and_with_clause
	: '(' csql_query_without_subquery_and_with_clause ')'
		{{ DBG_TRACE_GRAMMAR(subquery_without_subquery_and_with_clause, : '(' csql_query_without_subquery_and_with_clause ')');

			PT_NODE *stmt = $2;

			if (parser_within_join_condition)
			  {
			    PT_ERRORm (this_parser, stmt, MSGCAT_SET_PARSER_SYNTAX,
				       MSGCAT_SYNTAX_JOIN_COND_SUBQ);
			  }

			if (stmt)
			  stmt->info.query.is_subquery = PT_IS_SUBQUERY;
			$$ = stmt;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;


path_expression
	: path_header path_dot NONE		%dprec 6
		{{ DBG_TRACE_GRAMMAR(path_expression, : path_header path_dot NONE);

			PT_NODE *p = parser_new_node (this_parser, PT_NAME);
			if (p)
			  {
			    p->info.name.original = $3;
			  }
			$$ = p;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| path_header path_dot IDENTITY		%dprec 5
		{{ DBG_TRACE_GRAMMAR(path_expression, | path_header path_dot IDENTITY);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| path_header path_dot OBJECT		%dprec 4
		{{ DBG_TRACE_GRAMMAR(path_expression, | path_header path_dot OBJECT);

			PT_NODE *node = $1;
			if (node && node->node_type == PT_NAME)
			  {
			    PT_NAME_INFO_SET_FLAG (node, PT_NAME_INFO_EXTERNAL);
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| path_header DOT '*'			%dprec 3
		{{ DBG_TRACE_GRAMMAR(path_expression, | path_header DOT '*');

			PT_NODE *node = $1;
			if (node && node->node_type == PT_NAME &&
			    node->info.name.meta_class == PT_META_CLASS)
			  {
			    /* don't allow "class class_variable.*" */
			    PT_ERROR (this_parser, node, "check syntax at '*'");
			  }
			else
			  {
			    if (node)
			      node->type_enum = PT_TYPE_STAR;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| path_id_list				%dprec 2
		{{ DBG_TRACE_GRAMMAR(path_expression, | path_id_list);
			PT_NODE *dot;
			PT_NODE *serial_value = NULL;

			dot = $1;
			if (dot
			    && dot->node_type == PT_DOT_
			    && dot->info.dot.arg2 && dot->info.dot.arg2->node_type == PT_NAME)
			  {
			    PT_NODE *name = dot->info.dot.arg2;

			    if (intl_identifier_casecmp (name->info.name.original, "current_value") == 0 ||
				intl_identifier_casecmp (name->info.name.original, "currval") == 0)
			      {
				serial_value = parser_new_node (this_parser, PT_EXPR);
				serial_value->info.expr.op = PT_CURRENT_VALUE;
				serial_value->info.expr.arg1
				  = dot->info.dot.arg1;
				dot->info.dot.arg1 = NULL; /* cut */

				serial_value->info.expr.arg2 = NULL;

				PICE (serial_value);
				if (parser_serial_check == 0)
				    PT_ERRORmf(this_parser, serial_value,
					MSGCAT_SET_PARSER_SEMANTIC,
					MSGCAT_SEMANTIC_NOT_ALLOWED_HERE,
					"serial");

				parser_free_node (this_parser, dot);
				dot = serial_value;

				parser_cannot_cache = true;
			      }
			    else
			      if (intl_identifier_casecmp (name->info.name.original, "next_value") == 0 ||
				  intl_identifier_casecmp (name->info.name.original, "nextval") == 0)
			      {
				serial_value = parser_new_node (this_parser, PT_EXPR);
				serial_value->info.expr.op = PT_NEXT_VALUE;

				serial_value->info.expr.arg1
				  = dot->info.dot.arg1;
				dot->info.dot.arg1 = NULL; /* cut */

				serial_value->info.expr.arg2
				  = parser_new_node (this_parser, PT_VALUE);
				if (serial_value->info.expr.arg2)
				  {
				    PT_NODE *arg2;

				    arg2 = serial_value->info.expr.arg2;
				    arg2->type_enum = PT_TYPE_INTEGER;
				    arg2->info.value.data_value.i = 1;
				  }

				PICE (serial_value);
				if (parser_serial_check == 0)
				    PT_ERRORmf(this_parser, serial_value,
					MSGCAT_SET_PARSER_SEMANTIC,
					MSGCAT_SEMANTIC_NOT_ALLOWED_HERE,
					"serial");

				parser_free_node (this_parser, dot);
				dot = serial_value;

				parser_cannot_cache = true;
			      }
			  }

			$$ = dot;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

path_id_list
	: path_id_list path_dot path_id			%dprec 1
		{{ DBG_TRACE_GRAMMAR(path_id_list, : path_id_list path_dot path_id);

			PT_NODE *dot = parser_new_node (this_parser, PT_DOT_);
			if (dot)
			  {
			    dot->info.dot.arg1 = $1;
			    dot->info.dot.arg2 = $3;
			  }

			$$ = dot;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| path_header					%dprec 2
		{{ DBG_TRACE_GRAMMAR(path_id_list, | path_header);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

path_header
	: param_
		{{ DBG_TRACE_GRAMMAR(path_header, : param_);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| CLASS path_id
		{{ DBG_TRACE_GRAMMAR(path_header, | CLASS path_id);

			PT_NODE *node = $2;
			if (node && node->node_type == PT_NAME)
			  node->info.name.meta_class = PT_META_CLASS;
			$$ = node;

		DBG_PRINT}}
	| path_id
		{{ DBG_TRACE_GRAMMAR(path_header, | path_id);

			PT_NODE *node = $1;
			if (node && node->node_type == PT_NAME)
			  node->info.name.meta_class = PT_NORMAL;
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

path_dot
	: DOT          { DBG_TRACE_GRAMMAR(path_dot, : DOT); }
	| RIGHT_ARROW  { DBG_TRACE_GRAMMAR(path_dot, | RIGHT_ARROW); }
	;

path_id
	: identifier '{' identifier '}'
		{{ DBG_TRACE_GRAMMAR(path_id, : identifier '{' identifier '}');

			PT_NODE *corr = $3;
			PT_NODE *name = $1;

			if (name)
			  name->info.name.path_correlation = corr;
			$$ = name;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| identifier
		{{ DBG_TRACE_GRAMMAR(path_id, | identifier);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| generic_function_id
		{{ DBG_TRACE_GRAMMAR(path_id, | generic_function_id);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| table_set_function_call
		{{ DBG_TRACE_GRAMMAR(path_id, | table_set_function_call);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

simple_path_id
	: identifier DOT identifier
		{{ DBG_TRACE_GRAMMAR(simple_path_id, : identifier DOT identifier);

			PT_NODE *dot = parser_new_node (this_parser, PT_DOT_);
			if (dot)
			  {
			    dot->info.dot.arg1 = $1;
			    dot->info.dot.arg2 = $3;
			  }

			$$ = dot;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| identifier
		{{ DBG_TRACE_GRAMMAR(simple_path_id, | identifier);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

opt_in_out
	: /* empty */
		{{

			$$ = PT_NOPUT;

		DBG_PRINT}}
	| IN_
		{{

			$$ = PT_INPUT;

		DBG_PRINT}}
	| OUT_
		{{

			$$ = PT_OUTPUT;

		DBG_PRINT}}
	| INOUT
		{{

			$$ = PT_INPUTOUTPUT;

		DBG_PRINT}}
	;

negative_prec_cast_type
	: CHAR_
		{{ DBG_TRACE_GRAMMAR(negative_prec_cast_type, : CHAR_);

			$$ = PT_TYPE_CHAR;

		DBG_PRINT}}
	| NATIONAL CHAR_
		{{ DBG_TRACE_GRAMMAR(negative_prec_cast_type, |  NATIONAL CHAR_);

			$$ = PT_TYPE_NCHAR;

		DBG_PRINT}}
	| NCHAR
		{{ DBG_TRACE_GRAMMAR(negative_prec_cast_type, | NCHAR);

			$$ = PT_TYPE_NCHAR;

		DBG_PRINT}}
	;

of_cast_data_type
	: data_type
		{{ DBG_TRACE_GRAMMAR(of_cast_data_type, : data_type);
			$$ = $1;

		DBG_PRINT}}
	| negative_prec_cast_type '(' '-' unsigned_integer ')' opt_charset opt_collation
		{{ DBG_TRACE_GRAMMAR(of_cast_data_type, | negative_prec_cast_type '(' '-' unsigned_integer ')' opt_charset opt_collation);
			container_2 ctn;
			PT_TYPE_ENUM typ = $1;
			PT_NODE *len = NULL, *dt = NULL;
			int l = 0;
			PT_NODE *charset_node = NULL;
			PT_NODE *coll_node = NULL;

			len = $4;
			charset_node = $6;
			coll_node = $7;

			if (len && len->type_enum == PT_TYPE_INTEGER)
			  {
			    l = -len->info.value.data_value.i;
			  }

			if (l != -1)
			  {
			    int maxlen = (typ == PT_TYPE_NCHAR)
			      ? DB_MAX_NCHAR_PRECISION : DB_MAX_CHAR_PRECISION;
			    PT_ERRORmf3 (this_parser, len,
					 MSGCAT_SET_PARSER_SEMANTIC,
					 MSGCAT_SEMANTIC_INV_PREC,
					 l, -1, maxlen);
			  }

			dt = parser_new_node (this_parser, PT_DATA_TYPE);
			if (dt)
			  {
			    int coll_id, charset;

			    dt->type_enum = typ;
			    dt->info.data_type.precision = l;
			    switch (typ)
			      {
			      case PT_TYPE_CHAR:
			      case PT_TYPE_NCHAR:
				if (pt_check_grammar_charset_collation
				    (this_parser, charset_node, coll_node, &charset, &coll_id) == NO_ERROR)
				  {
				    dt->info.data_type.units = charset;
				    dt->info.data_type.collation_id = coll_id;
				  }
				 else
				  {
				    dt->info.data_type.units = -1;
				    dt->info.data_type.collation_id = -1;
				  }
				break;

			      default:
				break;
			      }
			  }

			SET_CONTAINER_2 (ctn, FROM_NUMBER (typ), dt);
			$$ = ctn;
			if (len)
			  {
			    parser_free_node (this_parser, len);
			  }

			if (charset_node)
			  {
			    parser_free_node (this_parser, charset_node);
                          }

			if (coll_node)
			  {
			    parser_free_node (this_parser, coll_node);
			  }

		DBG_PRINT}}
	;

data_type
	: nested_set primitive_type
		{{ DBG_TRACE_GRAMMAR(data_type, : nested_set primitive_type);

			container_2 ctn;
			PT_TYPE_ENUM typ, e;
			PT_NODE *dt;

			typ = $1;
			e = TO_NUMBER (CONTAINER_AT_0 ($2));
			dt = CONTAINER_AT_1 ($2);

			if (!dt)
			  {
			    dt = parser_new_node (this_parser, PT_DATA_TYPE);
			    if (dt)
			      {
				dt->type_enum = e;
				dt->data_type = NULL;
			      }
			  }

			SET_CONTAINER_2 (ctn, FROM_NUMBER (typ), dt);
			$$ = ctn;

		DBG_PRINT}}
	| nested_set '(' data_type_list ')'
		{{ DBG_TRACE_GRAMMAR(data_type, | nested_set '(' data_type_list ')');

			container_2 ctn;
			PT_TYPE_ENUM typ;
			PT_NODE *dt;

			typ = $1;
			dt = $3;

			SET_CONTAINER_2 (ctn, FROM_NUMBER (typ), dt);
			$$ = ctn;

		DBG_PRINT}}
	| nested_set '(' ')'
		{{ DBG_TRACE_GRAMMAR(data_type, | nested_set '(' ')');

			container_2 ctn;
			PT_TYPE_ENUM typ;

			typ = $1;
			SET_CONTAINER_2 (ctn, FROM_NUMBER (typ), NULL);
			$$ = ctn;

		DBG_PRINT}}
	| nested_set set_type
		{{ DBG_TRACE_GRAMMAR(data_type, | nested_set set_type);

			container_2 ctn;
			PT_TYPE_ENUM typ;
			PT_NODE *dt;

			typ = $1;
			dt = parser_new_node (this_parser, PT_DATA_TYPE);
			if (dt)
			  {
			    dt->type_enum = $2;
			    dt->data_type = NULL;
			  }

			SET_CONTAINER_2 (ctn, FROM_NUMBER (typ), dt);
			$$ = ctn;

		DBG_PRINT}}
	| set_type
		{{ DBG_TRACE_GRAMMAR(data_type, | set_type);

			container_2 ctn;
			PT_TYPE_ENUM typ;
			typ = $1;
			SET_CONTAINER_2 (ctn, FROM_NUMBER (typ), NULL);
			$$ = ctn;

		DBG_PRINT}}
	| primitive_type
		{{ DBG_TRACE_GRAMMAR(data_type, | primitive_type);

			$$ = $1;

		DBG_PRINT}}
	;

nested_set
	: nested_set set_type
		{{ DBG_TRACE_GRAMMAR(nested_set, : nested_set set_type);

			$$ = $1;

		DBG_PRINT}}
	| set_type
		{{ DBG_TRACE_GRAMMAR(nested_set, | set_type);

			$$ = $1;

		DBG_PRINT}}
	;

data_type_list
	: data_type_list ',' data_type
		{{ DBG_TRACE_GRAMMAR(data_type_list, : data_type_list ',' data_type);

			PT_NODE *dt;
			PT_TYPE_ENUM e;

			e = TO_NUMBER (CONTAINER_AT_0 ($3));
			dt = CONTAINER_AT_1 ($3);

			if (dt)
			  {
			    if (e == PT_TYPE_SET ||
				e == PT_TYPE_MULTISET ||
				e == PT_TYPE_SEQUENCE)
			      {
				csql_yyerror("nested data type definition");
			      }
			  }

			if (!dt)
			  {
			    dt = parser_new_node (this_parser, PT_DATA_TYPE);
			    if (dt)
			      {
				dt->type_enum = e;
				dt->data_type = NULL;
			      }
			  }

			$$ = parser_make_link ($1, dt);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| data_type
		{{ DBG_TRACE_GRAMMAR(data_type_list, | data_type);

			PT_NODE *dt;
			PT_TYPE_ENUM e;

			e = TO_NUMBER (CONTAINER_AT_0 ($1));
			dt = CONTAINER_AT_1 ($1);

			if (dt)
			  {
			    if (e == PT_TYPE_SET ||
				e == PT_TYPE_MULTISET ||
				e == PT_TYPE_SEQUENCE)
			      {
				csql_yyerror("nested data type definition");
			      }
			  }

			if (!dt)
			  {
			    dt = parser_new_node (this_parser, PT_DATA_TYPE);
			    if (dt)
			      {
				dt->type_enum = e;
				dt->data_type = NULL;
			      }
			  }

			$$ = dt;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

char_bit_type
	: CHAR_ opt_varying
		{{ DBG_TRACE_GRAMMAR(char_bit_type, : CHAR_ opt_varying);

			if ($2)
			  $$ = PT_TYPE_VARCHAR;
			else
			  $$ = PT_TYPE_CHAR;

		DBG_PRINT}}
	| VARCHAR
		{{ DBG_TRACE_GRAMMAR(char_bit_type, | VARCHAR);

			$$ = PT_TYPE_VARCHAR;

		DBG_PRINT}}
	| NATIONAL CHAR_ opt_varying
		{{ DBG_TRACE_GRAMMAR(char_bit_type, | NATIONAL CHAR_ opt_varying);

			if ($3)
			  $$ = PT_TYPE_VARNCHAR;
			else
			  $$ = PT_TYPE_NCHAR;

		DBG_PRINT}}
	| NCHAR	opt_varying
		{{ DBG_TRACE_GRAMMAR(char_bit_type, | NCHAR	opt_varying);

			if ($2)
			  $$ = PT_TYPE_VARNCHAR;
			else
			  $$ = PT_TYPE_NCHAR;

		DBG_PRINT}}
	| BIT opt_varying
		{{ DBG_TRACE_GRAMMAR(char_bit_type, | BIT opt_varying);

			if ($2)
			  $$ = PT_TYPE_VARBIT;
			else
			  $$ = PT_TYPE_BIT;

		DBG_PRINT}}
	;

opt_varying
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_varying, : );

			$$ = 0;

		DBG_PRINT}}
	| VARYING
		{{ DBG_TRACE_GRAMMAR(opt_varying, | VARYING);

			$$ = 1;

		DBG_PRINT}}
	;

json_schema
		: /* empty */
			{{ DBG_TRACE_GRAMMAR(json_schema, : );

				$$ = 0;

			DBG_PRINT}}
		| '(' CHAR_STRING ')'
			{{ DBG_TRACE_GRAMMAR(json_schema, | '(' CHAR_STRING ')' );

				$$ = $2;

			DBG_PRINT}}
		;

primitive_type
	: INTEGER opt_padding
		{{ DBG_TRACE_GRAMMAR(primitive_type, : INTEGER opt_padding );

			container_2 ctn;
			SET_CONTAINER_2 (ctn, FROM_NUMBER (PT_TYPE_INTEGER), NULL);
			$$ = ctn;

		DBG_PRINT}}
	| SmallInt
		{{ DBG_TRACE_GRAMMAR(primitive_type, | SmallInt );

			container_2 ctn;
			SET_CONTAINER_2 (ctn, FROM_NUMBER (PT_TYPE_SMALLINT), NULL);
			$$ = ctn;

		DBG_PRINT}}
	| BIGINT
		{{ DBG_TRACE_GRAMMAR(primitive_type, | BIGINT );

			container_2 ctn;
			SET_CONTAINER_2 (ctn, FROM_NUMBER (PT_TYPE_BIGINT), NULL);
			$$ = ctn;

		DBG_PRINT}}
	| Double PRECISION
		{{ DBG_TRACE_GRAMMAR(primitive_type, | Double PRECISION );

			container_2 ctn;
			SET_CONTAINER_2 (ctn, FROM_NUMBER (PT_TYPE_DOUBLE), NULL);
			$$ = ctn;

		DBG_PRINT}}
	| Double
		{{ DBG_TRACE_GRAMMAR(primitive_type, | Double );

			container_2 ctn;
			SET_CONTAINER_2 (ctn, FROM_NUMBER (PT_TYPE_DOUBLE), NULL);
			$$ = ctn;

		DBG_PRINT}}
	| Date
		{{ DBG_TRACE_GRAMMAR(primitive_type, | Date );

			container_2 ctn;
			SET_CONTAINER_2 (ctn, FROM_NUMBER (PT_TYPE_DATE), NULL);
			$$ = ctn;

		DBG_PRINT}}
	| Time
		{{ DBG_TRACE_GRAMMAR(primitive_type, | Time );

			container_2 ctn;
			SET_CONTAINER_2 (ctn, FROM_NUMBER (PT_TYPE_TIME), NULL);
			$$ = ctn;

		DBG_PRINT}}
	| Utime
		{{ DBG_TRACE_GRAMMAR(primitive_type, | Utime );

			container_2 ctn;
			SET_CONTAINER_2 (ctn, FROM_NUMBER (PT_TYPE_TIMESTAMP), NULL);
			$$ = ctn;

		DBG_PRINT}}
	| TIMESTAMP
		{{ DBG_TRACE_GRAMMAR(primitive_type, | TIMESTAMP );

			container_2 ctn;
			SET_CONTAINER_2 (ctn, FROM_NUMBER (PT_TYPE_TIMESTAMP), NULL);
			$$ = ctn;

		DBG_PRINT}}
	| TIMESTAMP WITH Time ZONE
		{{ DBG_TRACE_GRAMMAR(primitive_type, | TIMESTAMP WITH Time ZONE );

			container_2 ctn;
			SET_CONTAINER_2 (ctn, FROM_NUMBER (PT_TYPE_TIMESTAMPTZ), NULL);
			$$ = ctn;

		DBG_PRINT}}
	| TIMESTAMPTZ
		{{ DBG_TRACE_GRAMMAR(primitive_type, | TIMESTAMPTZ );

			container_2 ctn;
			SET_CONTAINER_2 (ctn, FROM_NUMBER (PT_TYPE_TIMESTAMPTZ), NULL);
			$$ = ctn;

		DBG_PRINT}}
	| TIMESTAMP WITH LOCAL Time ZONE
		{{ DBG_TRACE_GRAMMAR(primitive_type, | TIMESTAMP WITH LOCAL Time ZONE );

			container_2 ctn;
			SET_CONTAINER_2 (ctn, FROM_NUMBER (PT_TYPE_TIMESTAMPLTZ), NULL);
			$$ = ctn;

		DBG_PRINT}}
	| TIMESTAMPLTZ
		{{ DBG_TRACE_GRAMMAR(primitive_type, | TIMESTAMPLTZ );

			container_2 ctn;
			SET_CONTAINER_2 (ctn, FROM_NUMBER (PT_TYPE_TIMESTAMPLTZ), NULL);
			$$ = ctn;

		DBG_PRINT}}
	| DATETIME
		{{ DBG_TRACE_GRAMMAR(primitive_type, | DATETIME );

			container_2 ctn;
			SET_CONTAINER_2 (ctn, FROM_NUMBER (PT_TYPE_DATETIME), NULL);
			$$ = ctn;

		DBG_PRINT}}
	| DATETIME WITH Time ZONE
		{{ DBG_TRACE_GRAMMAR(primitive_type, | DATETIME WITH Time ZONE );

			container_2 ctn;
			SET_CONTAINER_2 (ctn, FROM_NUMBER (PT_TYPE_DATETIMETZ), NULL);
			$$ = ctn;

		DBG_PRINT}}
	| DATETIMETZ
		{{ DBG_TRACE_GRAMMAR(primitive_type, | DATETIMETZ );

			container_2 ctn;
			SET_CONTAINER_2 (ctn, FROM_NUMBER (PT_TYPE_DATETIMETZ), NULL);
			$$ = ctn;

		DBG_PRINT}}
	| DATETIME WITH LOCAL Time ZONE
		{{ DBG_TRACE_GRAMMAR(primitive_type, | DATETIME WITH LOCAL Time ZONE );

			container_2 ctn;
			SET_CONTAINER_2 (ctn, FROM_NUMBER (PT_TYPE_DATETIMELTZ), NULL);
			$$ = ctn;

		DBG_PRINT}}
	| DATETIMELTZ
		{{ DBG_TRACE_GRAMMAR(primitive_type, | DATETIMELTZ );

			container_2 ctn;
			SET_CONTAINER_2 (ctn, FROM_NUMBER (PT_TYPE_DATETIMELTZ), NULL);
			$$ = ctn;

		DBG_PRINT}}
	| Monetary
		{{ DBG_TRACE_GRAMMAR(primitive_type, | Monetary );

			container_2 ctn;
			SET_CONTAINER_2 (ctn, FROM_NUMBER (PT_TYPE_MONETARY), NULL);
			$$ = ctn;

		DBG_PRINT}}
	| JSON json_schema
	    {{ DBG_TRACE_GRAMMAR(primitive_type, | JSON json_schema );
			const char * json_schema_str = $2;
			container_2 ctn;
			PT_TYPE_ENUM type = PT_TYPE_JSON;
			PT_NODE * dt = parser_new_node (this_parser, PT_DATA_TYPE);

			if (dt && json_schema_str)
				{
					dt->type_enum = type;
					dt->info.data_type.json_schema = pt_append_bytes (this_parser,
                                                                                          NULL,
                                                                                          json_schema_str,
                                                                                          strlen (json_schema_str));
					SET_CONTAINER_2 (ctn, FROM_NUMBER (type), dt);
				}
			else
				{
					SET_CONTAINER_2 (ctn, FROM_NUMBER (type), NULL);
				}

			$$ = ctn;
		DBG_PRINT}}
	| OBJECT
		{{ DBG_TRACE_GRAMMAR(primitive_type, | OBJECT );
			container_2 ctn;
			SET_CONTAINER_2 (ctn, FROM_NUMBER (PT_TYPE_OBJECT), NULL);
			$$ = ctn;

		DBG_PRINT}}
	| String
	  opt_charset
	  opt_collation
		{{ DBG_TRACE_GRAMMAR(primitive_type, | String opt_charset opt_collation );

			container_2 ctn;
			PT_TYPE_ENUM typ = PT_TYPE_VARCHAR;
			PT_NODE *dt = parser_new_node (this_parser, PT_DATA_TYPE);
			PT_NODE *charset_node = $2;
			PT_NODE *coll_node = $3;
			if (dt)
			  {
			    int coll_id, charset;

			    dt->type_enum = typ;
			    dt->info.data_type.precision = DB_MAX_VARCHAR_PRECISION;

			    if (pt_check_grammar_charset_collation
				  (this_parser, charset_node,
				   coll_node, &charset, &coll_id) == NO_ERROR)
			      {
				dt->info.data_type.units = charset;
				dt->info.data_type.collation_id = coll_id;
			      }
			    else
			      {
				dt->info.data_type.units = -1;
				dt->info.data_type.collation_id = -1;
			      }

			    if (charset_node)
			      {
				dt->info.data_type.has_cs_spec = true;
			      }
			    else
			      {
				dt->info.data_type.has_cs_spec = false;
			      }

			    if (coll_node)
			      {
				dt->info.data_type.has_coll_spec = true;
			      }
			    else
			      {
			        dt->info.data_type.has_coll_spec = false;
			      }
			  }
			SET_CONTAINER_2 (ctn, FROM_NUMBER (typ), dt);
			$$ = ctn;

			if (charset_node)
			  {
			    parser_free_node (this_parser, charset_node);
			  }

			if (coll_node)
			  {
			    parser_free_node (this_parser, coll_node);
			  }

		DBG_PRINT}}
	| BLOB_ opt_internal_external
		{{ DBG_TRACE_GRAMMAR(primitive_type, | BLOB_ opt_internal_external );

			container_2 ctn;
			SET_CONTAINER_2 (ctn, FROM_NUMBER (PT_TYPE_BLOB), NULL);
			$$ = ctn;

		DBG_PRINT}}
	| CLOB_ opt_internal_external
		{{ DBG_TRACE_GRAMMAR(primitive_type, | CLOB_ opt_internal_external );

			container_2 ctn;
			SET_CONTAINER_2 (ctn, FROM_NUMBER (PT_TYPE_CLOB), NULL);
			$$ = ctn;

		DBG_PRINT}}
	| class_name opt_identity
		{{ DBG_TRACE_GRAMMAR(primitive_type, | class_name opt_identity );

			container_2 ctn;
			PT_TYPE_ENUM typ = PT_TYPE_OBJECT;
			PT_NODE *dt = parser_new_node (this_parser, PT_DATA_TYPE);

			if (dt)
			  {
			    dt->type_enum = typ;
			    dt->info.data_type.entity = $1;
			    dt->info.data_type.units = $2;
			  }

			SET_CONTAINER_2 (ctn, FROM_NUMBER (typ), dt);
			$$ = ctn;

		DBG_PRINT}}
	| char_bit_type
	  opt_prec_1
	  opt_charset
	  opt_collation
		{{ DBG_TRACE_GRAMMAR(primitive_type, | char_bit_type opt_prec_1 opt_charset opt_collation );

			container_2 ctn;
			PT_TYPE_ENUM typ = $1;
			PT_NODE *len = NULL, *dt = NULL;
			int l = 1;
			PT_NODE *charset_node = NULL;
			PT_NODE *coll_node = NULL;

			len = $2;
			charset_node = $3;
			coll_node = $4;

			if (len)
			  {
			    int maxlen = DB_MAX_VARCHAR_PRECISION;
			    l = len->info.value.data_value.i;

			    switch (typ)
			      {
			      case PT_TYPE_CHAR:
				maxlen = DB_MAX_CHAR_PRECISION;
				break;

			      case PT_TYPE_VARCHAR:
				maxlen = DB_MAX_VARCHAR_PRECISION;
				break;

			      case PT_TYPE_NCHAR:
				maxlen = DB_MAX_NCHAR_PRECISION;
				break;

			      case PT_TYPE_VARNCHAR:
				maxlen = DB_MAX_VARNCHAR_PRECISION;
				break;

			      case PT_TYPE_BIT:
				maxlen = DB_MAX_BIT_PRECISION;
				break;

			      case PT_TYPE_VARBIT:
				maxlen = DB_MAX_VARBIT_PRECISION;
				break;

			      default:
				break;
			      }

			    if ((l > maxlen) || (len->type_enum != PT_TYPE_INTEGER))
			      {
				if (typ == PT_TYPE_BIT || typ == PT_TYPE_VARBIT)
				  {
				    PT_ERRORmf (this_parser, len, MSGCAT_SET_PARSER_SYNTAX,
						MSGCAT_SYNTAX_MAX_BITLEN, maxlen);
				  }
				else
				  {
				    PT_ERRORmf (this_parser, len, MSGCAT_SET_PARSER_SYNTAX,
				    		MSGCAT_SYNTAX_MAX_BYTELEN, maxlen);
				  }
			      }

			    l = (l > maxlen ? maxlen : l);
			  }
			else
			  {
			    switch (typ)
			      {
			      case PT_TYPE_CHAR:
			      case PT_TYPE_NCHAR:
			      case PT_TYPE_BIT:
				l = 1;
				break;

			      case PT_TYPE_VARCHAR:
				l = DB_MAX_VARCHAR_PRECISION;
				break;

			      case PT_TYPE_VARNCHAR:
				l = DB_MAX_VARNCHAR_PRECISION;
				break;

			      case PT_TYPE_VARBIT:
				l = DB_MAX_VARBIT_PRECISION;
				break;

			      default:
				break;
			      }
			  }

			dt = parser_new_node (this_parser, PT_DATA_TYPE);
			if (dt)
			  {
			    int coll_id, charset;

			    dt->type_enum = typ;
			    dt->info.data_type.precision = l;
			    switch (typ)
			      {
			      case PT_TYPE_CHAR:
			      case PT_TYPE_VARCHAR:
			      case PT_TYPE_NCHAR:
			      case PT_TYPE_VARNCHAR:
				if (pt_check_grammar_charset_collation
				      (this_parser, charset_node,
				       coll_node, &charset, &coll_id) == NO_ERROR)
				  {
				    dt->info.data_type.units = charset;
				    dt->info.data_type.collation_id = coll_id;
				  }
				else
				  {
				    dt->info.data_type.units = -1;
				    dt->info.data_type.collation_id = -1;
				  }

				if (charset_node)
				  {
				    dt->info.data_type.has_cs_spec = true;
				  }
				else
				  {
				    dt->info.data_type.has_cs_spec = false;
				  }

				if (coll_node)
				  {
				    dt->info.data_type.has_coll_spec = true;
				  }
				else
				  {
				    dt->info.data_type.has_coll_spec = false;
				  }

				break;

			      case PT_TYPE_BIT:
			      case PT_TYPE_VARBIT:
				dt->info.data_type.units = INTL_CODESET_RAW_BITS;
				break;

			      default:
				break;
			      }
			  }

			SET_CONTAINER_2 (ctn, FROM_NUMBER (typ), dt);
			$$ = ctn;
			if (len)
			  parser_free_node (this_parser, len);

			if (charset_node)
			  {
			    parser_free_node (this_parser, charset_node);
                          }

			if (coll_node)
			  {
			    parser_free_node (this_parser, coll_node);
			  }

		DBG_PRINT}}
	| NUMERIC opt_prec_2
		{{ DBG_TRACE_GRAMMAR(primitive_type, | NUMERIC opt_prec_2 );

			container_2 ctn;
			PT_TYPE_ENUM typ;
			PT_NODE *prec, *scale, *dt;
			prec = CONTAINER_AT_0 ($2);
			scale = CONTAINER_AT_1 ($2);

			dt = parser_new_node (this_parser, PT_DATA_TYPE);
			typ = PT_TYPE_NUMERIC;

			if (dt)
			  {
			    dt->type_enum = typ;
			    dt->info.data_type.precision = prec ? prec->info.value.data_value.i : 15;
			    dt->info.data_type.dec_precision =
			      scale ? scale->info.value.data_value.i : 0;

			    if (scale && prec)
			      if (scale->info.value.data_value.i > prec->info.value.data_value.i)
				{
				  PT_ERRORmf2 (this_parser, dt,
					       MSGCAT_SET_PARSER_SEMANTIC,
					       MSGCAT_SEMANTIC_INV_PREC_SCALE,
					       prec->info.value.data_value.i,
					       scale->info.value.data_value.i);
				}
			    if (prec)
			      if (prec->info.value.data_value.i > DB_MAX_NUMERIC_PRECISION)
				{
				  PT_ERRORmf2 (this_parser, dt,
					       MSGCAT_SET_PARSER_SEMANTIC,
					       MSGCAT_SEMANTIC_PREC_TOO_BIG,
					       prec->info.value.data_value.i,
					       DB_MAX_NUMERIC_PRECISION);
				}
			  }

			SET_CONTAINER_2 (ctn, FROM_NUMBER (typ), dt);
			$$ = ctn;

			if (prec)
			  parser_free_node (this_parser, prec);
			if (scale)
			  parser_free_node (this_parser, scale);

		DBG_PRINT}}
   	| FLOAT_ opt_prec_1
		{{ DBG_TRACE_GRAMMAR(primitive_type, | FLOAT_ opt_prec_1);

			container_2 ctn;
			PT_TYPE_ENUM typ;
			PT_NODE *prec, *dt = NULL;
			prec = $2;

			if (prec &&
			    prec->info.value.data_value.i >= 8 &&
			    prec->info.value.data_value.i <= DB_MAX_NUMERIC_PRECISION)
			  {
			    typ = PT_TYPE_DOUBLE;
			  }
			else
			  {
			    dt = parser_new_node (this_parser, PT_DATA_TYPE);
			    typ = PT_TYPE_FLOAT;

			    if (dt)
			      {
				dt->type_enum = typ;
				dt->info.data_type.precision =
				  prec ? prec->info.value.data_value.i : 7;
				dt->info.data_type.dec_precision = 0;

				if (prec)
				  if (prec->info.value.data_value.i > DB_MAX_NUMERIC_PRECISION)
				    {
				      PT_ERRORmf2 (this_parser, dt,
						   MSGCAT_SET_PARSER_SEMANTIC,
						   MSGCAT_SEMANTIC_PREC_TOO_BIG,
						   prec->info.value.data_value.i,
						   DB_MAX_NUMERIC_PRECISION);
				    }
			      }

			  }

			SET_CONTAINER_2 (ctn, FROM_NUMBER (typ), dt);
			$$ = ctn;

			if (prec)
			  parser_free_node (this_parser, prec);

		DBG_PRINT}}
	| ENUM '(' char_string_literal_list ')' opt_charset opt_collation
	  {{ DBG_TRACE_GRAMMAR(primitive_type, | ENUM '(' char_string_literal_list ')' opt_charset opt_collation);
			container_2 ctn;
			int charset = -1;
			int coll_id = -1;

			int elem_cs = -1;
			int list_cs = -1;
			int has_error = 0;

			PT_NODE *charset_node = $5;
			PT_NODE *coll_node = $6;
			PT_NODE *elem_list = $3;
			PT_NODE *dt, *elem;

			elem = elem_list;
			while (elem != NULL)
			  {
			    if (elem->data_type == NULL)
			      {
				elem = elem->next;
				continue;
			      }

			    assert (elem->node_type == PT_VALUE);
			    elem->info.value.print_charset = false;

			    elem_cs = elem->data_type->info.data_type.units;
			    if (elem->info.value.has_cs_introducer)
			      {
				if (list_cs == -1)
				  {
				    list_cs = elem_cs;
				  }
				else if (list_cs != elem_cs)
				  {
				    PT_ERRORm (this_parser, elem,
					       MSGCAT_SET_PARSER_SEMANTIC,
					       MSGCAT_SEMANTIC_INCOMPATIBLE_CS_COLL);
				    has_error = 1;
				    break;
				  }
			      }
			    elem = elem->next;
			  } /* END while */

			dt = parser_new_node (this_parser, PT_DATA_TYPE);

			if (!has_error && dt != NULL)
			  {
			    dt->type_enum = PT_TYPE_ENUMERATION;
			    dt->info.data_type.enumeration = elem_list;

			    if (charset_node == NULL && coll_node == NULL)
			      {
				if (list_cs == -1)
				  {
				    charset = LANG_SYS_CODESET;
				  }
				else
				  {
				    charset = list_cs;
				  }
				coll_id = LANG_GET_BINARY_COLLATION (charset);
				dt->info.data_type.has_cs_spec = false;
				dt->info.data_type.has_coll_spec = false;
			      }
			    else if (pt_check_grammar_charset_collation (
					this_parser, charset_node,
					coll_node, &charset,
					&coll_id) == NO_ERROR)
			      {
				if (charset_node)
				  {
				    dt->info.data_type.has_cs_spec = true;
				  }
				else
				  {
				    dt->info.data_type.has_cs_spec = false;
				  }
				if (coll_node)
				  {
				    dt->info.data_type.has_coll_spec = true;
				  }
				else
				  {
				    dt->info.data_type.has_coll_spec = false;
				  }
			      }
			    else
			      {
				has_error = 1;
				dt->info.data_type.units = -1;
				dt->info.data_type.collation_id = -1;
			      }

			    if (!has_error)
			      {
				dt->info.data_type.units = charset;
				dt->info.data_type.collation_id = coll_id;
			      }
			  }

			PARSER_SAVE_ERR_CONTEXT (dt, @3.buffer_pos)

			SET_CONTAINER_2 (ctn, FROM_NUMBER (PT_TYPE_ENUMERATION), dt);

			$$ = ctn;
	  DBG_PRINT}}
	;

opt_internal_external
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_internal_external, : );

			$$ = 0;

		DBG_PRINT}}
	| INTERNAL
		{{ DBG_TRACE_GRAMMAR(opt_internal_external, | INTERNAL );

			$$ = PT_LOB_INTERNAL;

		DBG_PRINT}}
	| EXTERNAL
		{{ DBG_TRACE_GRAMMAR(opt_internal_external, | EXTERNAL );

			$$ = PT_LOB_EXTERNAL;

		DBG_PRINT}}
	;

opt_identity
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_identity, : );

			$$ = 0;

		DBG_PRINT}}
	| IDENTITY
		{{ DBG_TRACE_GRAMMAR(opt_identity, | IDENTITY );

			$$ = 1;

		DBG_PRINT}}
	;

opt_prec_1
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_prec_1, : );

			$$ = NULL;

		DBG_PRINT}}
	| '(' unsigned_integer ')'
		{{ DBG_TRACE_GRAMMAR(opt_prec_1, | '(' unsigned_integer ')' );

			$$ = $2;

		DBG_PRINT}}
	;

opt_padding
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_padding, : );

			$$ = NULL;

		DBG_PRINT}}
	| '(' unsigned_integer ')'
		{{ DBG_TRACE_GRAMMAR(opt_padding, | '(' unsigned_integer ')' );

			$$ = NULL;

		DBG_PRINT}}
	;

opt_prec_2
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_prec_2, : );

			container_2 ctn;
			SET_CONTAINER_2 (ctn, NULL, NULL);
			$$ = ctn;

		DBG_PRINT}}
	| '(' unsigned_integer ')'
		{{ DBG_TRACE_GRAMMAR(opt_prec_2, | '(' unsigned_integer ')' );

			container_2 ctn;
			SET_CONTAINER_2 (ctn, $2, NULL);
			$$ = ctn;

		DBG_PRINT}}
	| '(' unsigned_integer ',' unsigned_integer ')'
		{{ DBG_TRACE_GRAMMAR(opt_prec_2, | '(' unsigned_integer ',' unsigned_integer ')' );

			container_2 ctn;
			SET_CONTAINER_2 (ctn, $2, $4);
			$$ = ctn;

		DBG_PRINT}}
	;

of_charset
	: CHARACTER_SET_
	| CHARSET
	;

opt_collation
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_collation, : );

			$$ = NULL;

		DBG_PRINT}}
	| collation_spec
		{{ DBG_TRACE_GRAMMAR(opt_collation, | collation_spec );

			$$=$1;

		DBG_PRINT}}
	;

collation_spec
	: COLLATE char_string_literal
		{{ DBG_TRACE_GRAMMAR(collation_spec, : );

			$$ = $2;

		DBG_PRINT}}
	| COLLATE BINARY
		{{ DBG_TRACE_GRAMMAR(collation_spec, | COLLATE BINARY);
			PT_NODE *node;

			node = parser_new_node (this_parser, PT_VALUE);

			if (node)
			  {
			    node->type_enum = PT_TYPE_CHAR;
			    node->info.value.string_type = ' ';
			    node->info.value.data_value.str =
			      pt_append_bytes (this_parser, NULL, "binary", strlen ("binary"));
			    PT_NODE_PRINT_VALUE_TO_TEXT (this_parser, node);
			  }

			$$ = node;
		DBG_PRINT}}
	| COLLATE IdName
		{{ DBG_TRACE_GRAMMAR(collation_spec, | COLLATE IdName);
			PT_NODE *node;

			node = parser_new_node (this_parser, PT_VALUE);

			if (node)
			  {
			    node->type_enum = PT_TYPE_CHAR;
			    node->info.value.string_type = ' ';
			    node->info.value.data_value.str =
			      pt_append_bytes (this_parser, NULL, $2, strlen ($2));
			    PT_NODE_PRINT_VALUE_TO_TEXT (this_parser, node);
			  }

			$$ = node;
		DBG_PRINT}}
	;

class_encrypt_spec
  : ENCRYPT opt_equalsign opt_encrypt_algorithm
		{{ DBG_TRACE_GRAMMAR(class_encrypt_spec, : ENCRYPT opt_equalsign opt_encrypt_algorithm );
			PT_NODE *node = NULL;

      node = parser_new_node (this_parser, PT_VALUE);

			if (node)
			  {
			    node->type_enum = PT_TYPE_INTEGER;
          node->info.value.data_value.i = $3;
			    PT_NODE_PRINT_VALUE_TO_TEXT (this_parser, node);
			  }

			$$ = node;
		DBG_PRINT}}
	;

class_comment_spec
	: COMMENT opt_equalsign char_string_literal
		{{ DBG_TRACE_GRAMMAR(class_comment_spec, : COMMENT opt_equalsign char_string_literal);
			PT_NODE *node = $3;

			if (node)
			  {
			    node->type_enum = PT_TYPE_VARCHAR;

			    if (node->info.value.data_value.str->length >
			        SM_MAX_CLASS_COMMENT_LENGTH)
			      {
			        PT_ERRORmf (this_parser, node, MSGCAT_SET_PARSER_SYNTAX,
                                MSGCAT_SYNTAX_MAX_CLASS_COMMENT_LEN,
                                SM_MAX_CLASS_COMMENT_LENGTH);
			      }
			    PT_NODE_PRINT_VALUE_TO_TEXT (this_parser, node);
			  }

			$$ = node;
		DBG_PRINT}}
	| COMMENT					/* 1 */
	  ON_						/* 2 */
	  opt_of_column_attribute			/* 3 */
		{ DBG_TRACE_GRAMMAR(class_comment_spec, : COMMENT ON_ opt_of_column_attribute);
                  parser_attr_type = PT_NORMAL; }	/* 4 */
	  attr_def_comment_list				/* 5 */
		{{ DBG_TRACE_GRAMMAR(class_comment_spec, attr_def_comment_list);
			PT_NODE *alter_node = parser_get_alter_node();

			if (alter_node != NULL)
			  {
				alter_node->info.alter.code = PT_CHANGE_COLUMN_COMMENT;
				alter_node->info.alter.alter_clause.attr_mthd.attr_def_list = $5;
			  }
		DBG_PRINT}}
	| COMMENT					/* 1 */
	  ON_						/* 2 */
	  CLASS ATTRIBUTE				/* 3, 4 */
		{ DBG_TRACE_GRAMMAR(class_comment_spec, : COMMENT ON_ CLASS ATTRIBUTE);
                   parser_attr_type = PT_META_ATTR; }	/* 5 */
	  attr_def_comment_list 			/* 6 */
		{{ DBG_TRACE_GRAMMAR(class_comment_spec, attr_def_comment_list);
			PT_NODE *alter_node = parser_get_alter_node();

			if (alter_node != NULL)
			  {
				alter_node->info.alter.code = PT_CHANGE_COLUMN_COMMENT;
				alter_node->info.alter.alter_clause.attr_mthd.attr_def_list = $6;
			  }
		DBG_PRINT}}
	;

opt_vclass_comment_spec
	: /* empty */
		{ DBG_TRACE_GRAMMAR(opt_vclass_comment_spec, : );
                  $$ = NULL; }
	| class_comment_spec
		{ DBG_TRACE_GRAMMAR(opt_vclass_comment_spec, | class_comment_spec);
                  $$ = $1; }
	;

opt_equalsign
	: /* empty */
	| '='
	;

opt_encrypt_algorithm
  : /* empty */
    { DBG_TRACE_GRAMMAR(opt_encrypt_algorithm, : );  
      $$ = -1; }  /* default algorithm from the system parameter */
  | AES
    { DBG_TRACE_GRAMMAR(opt_encrypt_algorithm, | AES ); 
      $$ = 1; }   /* TDE_ALGORITHM_AES */ 
  | ARIA
    { DBG_TRACE_GRAMMAR(opt_encrypt_algorithm, | ARIA ); 
      $$ = 2; }   /* TDE_ALGORITHM_ARIA */
  ;

opt_comment_spec
	: /* empty */
		{ DBG_TRACE_GRAMMAR(opt_comment_spec, : );
                  $$ = NULL; }
	| COMMENT comment_value
		{ DBG_TRACE_GRAMMAR(opt_comment_spec, | COMMENT comment_value ); 
                  $$ = $2; }
	;

comment_value
	: char_string_literal
		{{ DBG_TRACE_GRAMMAR(comment_value, : char_string_literal );
			PT_NODE *node = $1;

			if (node)
			  {
			    node->type_enum = PT_TYPE_VARCHAR;

			    if (node->info.value.data_value.str->length >
			        SM_MAX_COMMENT_LENGTH)
			      {
			        PT_ERRORmf (this_parser, node, MSGCAT_SET_PARSER_SYNTAX,
                                MSGCAT_SYNTAX_MAX_COMMENT_LEN,
                                SM_MAX_COMMENT_LENGTH);
			      }
			    PT_NODE_PRINT_VALUE_TO_TEXT (this_parser, node);
			  }

			$$ = node;
		DBG_PRINT}}
	;

opt_charset
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_charset, : );

			$$ = NULL;

		DBG_PRINT}}
	| charset_spec
		{{ DBG_TRACE_GRAMMAR(opt_charset, | charset_spec);

			$$ = $1;

		DBG_PRINT}}
	;

charset_spec
	: of_charset char_string_literal
		{{ DBG_TRACE_GRAMMAR(charset_spec, : of_charset char_string_literal );

			$$ = $2;

		DBG_PRINT}}
	| of_charset BINARY
		{{ DBG_TRACE_GRAMMAR(charset_spec, | of_charset BINARY );
			PT_NODE *node;

			node = parser_new_node (this_parser, PT_VALUE);

			if (node)
			  {
			    node->type_enum = PT_TYPE_CHAR;
			    node->info.value.string_type = ' ';
			    node->info.value.data_value.str =
			      pt_append_bytes (this_parser, NULL, "binary", strlen ("binary"));
			    PT_NODE_PRINT_VALUE_TO_TEXT (this_parser, node);
			  }

			$$ = node;
		DBG_PRINT}}
	| of_charset IdName
		{{ DBG_TRACE_GRAMMAR(charset_spec, | of_charset IdName );
			PT_NODE *node;

			node = parser_new_node (this_parser, PT_VALUE);

			if (node)
			  {
			    node->type_enum = PT_TYPE_CHAR;
			    node->info.value.string_type = ' ';
			    node->info.value.data_value.str =
			      pt_append_bytes (this_parser, NULL, $2, strlen ($2));
			    PT_NODE_PRINT_VALUE_TO_TEXT (this_parser, node);
			  }

			$$ = node;
		DBG_PRINT}}
	;

opt_using_charset
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_using_charset, : );

			int charset = lang_get_client_charset ();
			PT_NODE *node;

			node = parser_new_node (this_parser, PT_VALUE);
			if (node)
			  {
			    node->type_enum = PT_TYPE_INTEGER;
			    node->info.value.data_value.i = charset;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| USING char_string_literal
		{{ DBG_TRACE_GRAMMAR(opt_using_charset, | USING char_string_literal );

			PT_NODE *charset_node = $2;
			int dummy;
			int charset = lang_get_client_charset ();
			PT_NODE *node;

			if (charset_node)
			{
			  if (pt_check_grammar_charset_collation
			      (this_parser, charset_node, NULL, &charset, &dummy) == 0)
			    {
			      parser_free_node (this_parser, charset_node);
			    }
			}
			node = parser_new_node (this_parser, PT_VALUE);
			if (node)
			  {
			    node->type_enum = PT_TYPE_INTEGER;
			    node->info.value.data_value.i = charset;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| USING IdName
		{{ DBG_TRACE_GRAMMAR(opt_using_charset, | USING IdName );

			PT_NODE *temp_node = NULL;
			int dummy;
			int charset = lang_charset ();
			PT_NODE *node;

			temp_node = parser_new_node (this_parser, PT_VALUE);

			if (temp_node)
			  {
			    temp_node->type_enum = PT_TYPE_CHAR;
			    temp_node->info.value.string_type = ' ';
			    temp_node->info.value.data_value.str =
			      pt_append_bytes (this_parser, NULL, $2, strlen ($2));
			    PT_NODE_PRINT_VALUE_TO_TEXT (this_parser, temp_node);
			  }

			if (temp_node)
			{
			  if (pt_check_grammar_charset_collation
				(this_parser, temp_node, NULL, &charset, &dummy) == 0)
			    {
				parser_free_node (this_parser, temp_node);
			    }
			}

			node = parser_new_node (this_parser, PT_VALUE);
			if (node)
			  {
			    node->type_enum = PT_TYPE_INTEGER;
			    node->info.value.data_value.i = charset;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| USING BINARY
		{{ DBG_TRACE_GRAMMAR(opt_using_charset, | USING BINARY );

			PT_NODE *node;

			node = parser_new_node (this_parser, PT_VALUE);
			if (node)
			  {
			    node->type_enum = PT_TYPE_INTEGER;
			    node->info.value.data_value.i = INTL_CODESET_BINARY;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

set_type
	: SET_OF
		{{ DBG_TRACE_GRAMMAR(set_type, : SET_OF );

			$$ = PT_TYPE_SET;

		DBG_PRINT}}
	| MULTISET_OF
		{{ DBG_TRACE_GRAMMAR(set_type, | MULTISET_OF );

			$$ = PT_TYPE_MULTISET;

		DBG_PRINT}}
	| SEQUENCE_OF
		{{ DBG_TRACE_GRAMMAR(set_type, | SEQUENCE_OF );

			$$ = PT_TYPE_SEQUENCE;

		DBG_PRINT}}
	| of_container opt_of
		{{ DBG_TRACE_GRAMMAR(set_type, | of_container opt_of );

			$$ = $1;

		DBG_PRINT}}
	;

opt_of
	: /* empty */
	| OF
	;

signed_literal_
	: literal_
		{{ DBG_TRACE_GRAMMAR(signed_literal_,  : literal_ );

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| '-' unsigned_integer
		{{ DBG_TRACE_GRAMMAR(signed_literal_, | '-' unsigned_integer );

			PT_NODE *node = $2;
			if (node != NULL)
			{
			  if (node->type_enum == PT_TYPE_BIGINT)
			    {
			      node->info.value.data_value.bigint
			        = -node->info.value.data_value.bigint;
			    }
			  else if (node->type_enum == PT_TYPE_NUMERIC)
			    {
			      const char *min_big_int = "9223372036854775808";
			      if (node->info.value.data_value.str->length == 19
				  && (strcmp ((const char *) node->info.value.data_value.str->bytes,
				  	      min_big_int) == 0))
			        {
				  node->info.value.data_value.bigint = DB_BIGINT_MIN;
				  node->type_enum = PT_TYPE_BIGINT;
			        }
			      else
			        {
				  /*add minus:*/
				  char *minus_sign;
				  PARSER_VARCHAR *buf = 0;
				  minus_sign = pt_append_string (this_parser, NULL, "-");
				  buf = pt_append_nulstring (this_parser, buf,
							     minus_sign);
				  buf = pt_append_nulstring (this_parser, buf,
							     (const char *) node->info.value.data_value.str->bytes);
				  node->info.value.data_value.str = buf;
			        }
			    }
			  else
			    {
			      node->info.value.data_value.i = -node->info.value.data_value.i;
			    }

			  node->info.value.text = NULL;
			  PT_NODE_PRINT_VALUE_TO_TEXT (this_parser, node);
			}

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| '-' unsigned_real
		{{ DBG_TRACE_GRAMMAR(signed_literal_,  | '-' unsigned_real);

						/* not allowed partition type */
						/* this will cause semantic error */
			PT_NODE *node = $2;
			if (node != NULL)
			{
			  if (node->type_enum == PT_TYPE_FLOAT)
			    {
			      node->info.value.data_value.f = -node->info.value.data_value.f;
			    }
			  else if (node->type_enum == PT_TYPE_DOUBLE)
			    {
			      node->info.value.data_value.d = -node->info.value.data_value.d;
			  }
			  else
			    {
			      char *minus_sign;
			      PARSER_VARCHAR *buf = 0;

			      assert (node->type_enum == PT_TYPE_NUMERIC);
			      minus_sign = pt_append_string (this_parser, NULL, "-");
			      /*add minus:*/
			      buf = pt_append_nulstring (this_parser, buf,
						       minus_sign);
			      buf = pt_append_nulstring (this_parser, buf,
						         (const char *) node->info.value.data_value.str->bytes);
			      node->info.value.data_value.str = buf;
			    }

			  node->info.value.text = NULL;
			  PT_NODE_PRINT_VALUE_TO_TEXT (this_parser, node);
			}

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| '-' monetary_literal
		{{ DBG_TRACE_GRAMMAR(signed_literal_,  | '-' monetary_literal);

						/* not allowed partition type */
						/* this will cause semantic error */

			PT_NODE *node = $2;

			if (node != NULL)
			{
			  assert (node->type_enum == PT_TYPE_MONETARY);
			  node->info.value.data_value.money.amount =
			    - node->info.value.data_value.money.amount;
			  node->info.value.text = NULL;
			  PT_NODE_PRINT_VALUE_TO_TEXT (this_parser, node);
			}

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

literal_
	: literal_w_o_param
		{{ DBG_TRACE_GRAMMAR(literal_,  : literal_w_o_param);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| param_
		{{ DBG_TRACE_GRAMMAR(literal_,  | param_ );

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

literal_w_o_param
	: unsigned_integer
		{{ DBG_TRACE_GRAMMAR(literal_w_o_param, : unsigned_integer );

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| unsigned_real
		{{ DBG_TRACE_GRAMMAR(literal_w_o_param,  | unsigned_real );

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| monetary_literal
		{{ DBG_TRACE_GRAMMAR(literal_w_o_param,  : unsigned_integer );

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| char_string_literal
		{{ DBG_TRACE_GRAMMAR(literal_w_o_param,  | char_string_literal);

			PT_NODE *node = $1;

			pt_value_set_collation_info (this_parser, node, NULL);

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| bit_string_literal
		{{ DBG_TRACE_GRAMMAR(literal_w_o_param,  | bit_string_literal );

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| host_param_input
		{{ DBG_TRACE_GRAMMAR(literal_w_o_param,  | host_param_input );

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| Null
		{{ DBG_TRACE_GRAMMAR(literal_w_o_param,  | Null );

			PT_NODE *node = parser_new_node (this_parser, PT_VALUE);
			if (node)
			  node->type_enum = PT_TYPE_NULL;
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| constant_set
		{{ DBG_TRACE_GRAMMAR(literal_w_o_param, | constant_set );

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| NA
		{{ DBG_TRACE_GRAMMAR(literal_w_o_param,  | NA );

			PT_NODE *node = parser_new_node (this_parser, PT_VALUE);
			if (node)
			  node->type_enum = PT_TYPE_NA;
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| date_or_time_literal
		{{ DBG_TRACE_GRAMMAR(literal_w_o_param, | date_or_time_literal );

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| boolean
		{{ DBG_TRACE_GRAMMAR(literal_w_o_param,  | boolean );

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| json_literal
		{{ DBG_TRACE_GRAMMAR(literal_w_o_param, | json_literal );

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
		DBG_PRINT}}
	;

boolean
	: True
		{{ DBG_TRACE_GRAMMAR(boolean, : True );

			PT_NODE *node = parser_new_node (this_parser, PT_VALUE);
			if (node)
			  {
			    node->info.value.text = "true";
			    node->info.value.data_value.i = 1;
			    node->type_enum = PT_TYPE_LOGICAL;
			  }
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| False
		{{ DBG_TRACE_GRAMMAR(boolean, | False );

			PT_NODE *node = parser_new_node (this_parser, PT_VALUE);
			if (node)
			  {
			    node->info.value.text = "false";
			    node->info.value.data_value.i = 0;
			    node->type_enum = PT_TYPE_LOGICAL;
			  }
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| UNKNOWN
		{{ DBG_TRACE_GRAMMAR(boolean, | UNKNOWN );

			PT_NODE *node = parser_new_node (this_parser, PT_VALUE);
			if (node)
			  node->type_enum = PT_TYPE_NULL;
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

constant_set
	: opt_of_container '{' expression_list '}'
		{{ DBG_TRACE_GRAMMAR(constant_set, : opt_of_container '{' expression_list '}' );

			PT_NODE *node = parser_new_node (this_parser, PT_VALUE);
			PT_NODE *e;

			if (node)
			  {
			    node->info.value.data_value.set = $3;
			    node->type_enum = $1;

			    for (e = node->info.value.data_value.set; e; e = e->next)
			      {
				if (e->type_enum == PT_TYPE_STAR)
				  {
				    PT_ERRORf (this_parser, e,
					       "check syntax at %s, illegal '*' expression.",
					       pt_short_print (this_parser, e));

				    break;
				  }
			      }
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| opt_of_container '{' '}'
		{{ DBG_TRACE_GRAMMAR(constant_set, | opt_of_container '{' '}' );

			PT_NODE *node = parser_new_node (this_parser, PT_VALUE);

			if (node)
			  {
			    node->info.value.data_value.set = 0;
			    node->type_enum = $1;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

opt_of_container
	: /* empty */
		{{ DBG_TRACE_GRAMMAR(opt_of_container, : );

			$$ = PT_TYPE_SEQUENCE;

		DBG_PRINT}}
	| of_container
		{{ DBG_TRACE_GRAMMAR(opt_of_container, | of_container );

			$$ = $1;

		DBG_PRINT}}
	;

of_container
	: SET
		{{ DBG_TRACE_GRAMMAR(of_container, : SET );

			$$ = PT_TYPE_SET;

		DBG_PRINT}}
	| MULTISET
		{{ DBG_TRACE_GRAMMAR(of_container, | MULTISET );

			$$ = PT_TYPE_MULTISET;

		DBG_PRINT}}
	| SEQUENCE
		{{ DBG_TRACE_GRAMMAR(of_container, | SEQUENCE );

			$$ = PT_TYPE_SEQUENCE;

		DBG_PRINT}}
	| LIST
		{{ DBG_TRACE_GRAMMAR(of_container, | LIST );

			$$ = PT_TYPE_SEQUENCE;

		DBG_PRINT}}
	;

identifier_list
	: identifier_list ',' identifier
		{{ DBG_TRACE_GRAMMAR(identifier_list, : identifier_list ',' identifier );

			$$ = parser_make_link ($1, $3);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| identifier
		{{ DBG_TRACE_GRAMMAR(identifier_list, | identifier );

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

opt_bracketed_identifier_list
	:	/* EMPTY */
		{{ DBG_TRACE_GRAMMAR(opt_bracketed_identifier_list, : );
			$$ = NULL;
		DBG_PRINT}}
	| '(' identifier_list ')'
		{{ DBG_TRACE_GRAMMAR(opt_bracketed_identifier_list, | '(' identifier_list ')' );
			$$ = $2;
		DBG_PRINT}}
	;

simple_path_id_list
	: simple_path_id_list ',' simple_path_id
		{{ DBG_TRACE_GRAMMAR(simple_path_id_list, : simple_path_id_list ',' simple_path_id);

			$$ = parser_make_link ($1, $3);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| simple_path_id
		{{ DBG_TRACE_GRAMMAR(simple_path_id_list, | simple_path_id);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

identifier_without_dot
	: identifier
		{{

			PT_NODE *p = $1;

			if (p)
			  {
			    const char *name = p->info.name.original;

			    if (name && strchr (name, '.'))
			      {
				PT_ERRORf (this_parser, p,
					   "Identifier name [%s] not allowed. It cannot contain dot(.).",
					   name);

				p = NULL;
			      }
			  }

			$$ = p;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

identifier
	: IdName
		{{ DBG_TRACE_GRAMMAR(identifier, : IdName);
			PT_NODE *p = parser_new_node (this_parser, PT_NAME);
			if (p)
			  {
			    int size_in;
			    char *str_name = $1;

			    size_in = strlen(str_name);

			    PARSER_SAVE_ERR_CONTEXT (p, @$.buffer_pos)
			    str_name = pt_check_identifier (this_parser, p,
							    str_name, size_in);
			    p->info.name.original = str_name;
			  }
			$$ = p;
		DBG_PRINT}}
	| BracketDelimitedIdName
		{{ DBG_TRACE_GRAMMAR(identifier, | BracketDelimitedIdName);
			PT_NODE *p = parser_new_node (this_parser, PT_NAME);
			if (p)
			  {
			    int size_in;
			    char *str_name = $1;

			    size_in = strlen(str_name);

			    PARSER_SAVE_ERR_CONTEXT (p, @$.buffer_pos)
			    str_name = pt_check_identifier (this_parser, p,
							    str_name, size_in);
			    p->info.name.original = str_name;
			  }
			$$ = p;
		DBG_PRINT}}
	| BacktickDelimitedIdName
		{{ DBG_TRACE_GRAMMAR(identifier, | BacktickDelimitedIdName);
			PT_NODE *p = parser_new_node (this_parser, PT_NAME);
			if (p)
			  {
			    int size_in;
			    char *str_name = $1;

			    size_in = strlen(str_name);

			    PARSER_SAVE_ERR_CONTEXT (p, @$.buffer_pos)
			    str_name = pt_check_identifier (this_parser, p,
							    str_name, size_in);
			    p->info.name.original = str_name;
			  }
			$$ = p;
		DBG_PRINT}}
	| DelimitedIdName
		{{ DBG_TRACE_GRAMMAR(identifier, | DelimitedIdName);
			PT_NODE *p = parser_new_node (this_parser, PT_NAME);
			if (p)
			  {
			    int size_in;
			    char *str_name = $1;

			    size_in = strlen(str_name);

			    PARSER_SAVE_ERR_CONTEXT (p, @$.buffer_pos)
			    str_name = pt_check_identifier (this_parser, p,
							    str_name, size_in);
			    p->info.name.original = str_name;
			  }
			$$ = p;
		DBG_PRINT}}
/*{{{*/
	| ACTIVE                 {{ DBG_TRACE_GRAMMAR(identifier, | ACTIVE             ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| ADDDATE                {{ DBG_TRACE_GRAMMAR(identifier, | ADDDATE            ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| AES                    {{ DBG_TRACE_GRAMMAR(identifier, | AES                ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| ANALYZE                {{ DBG_TRACE_GRAMMAR(identifier, | ANALYZE            ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| ARCHIVE                {{ DBG_TRACE_GRAMMAR(identifier, | ARCHIVE            ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| ARIA                   {{ DBG_TRACE_GRAMMAR(identifier, | ARIA               ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| AUTO_INCREMENT         {{ DBG_TRACE_GRAMMAR(identifier, | AUTO_INCREMENT     ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| BENCHMARK              {{ DBG_TRACE_GRAMMAR(identifier, | BENCHMARK          ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| BIT_AND                {{ DBG_TRACE_GRAMMAR(identifier, | BIT_AND            ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| BIT_OR                 {{ DBG_TRACE_GRAMMAR(identifier, | BIT_OR             ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| BIT_XOR                {{ DBG_TRACE_GRAMMAR(identifier, | BIT_XOR            ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| BUFFER                 {{ DBG_TRACE_GRAMMAR(identifier, | BUFFER             ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| CACHE                  {{ DBG_TRACE_GRAMMAR(identifier, | CACHE              ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| CAPACITY               {{ DBG_TRACE_GRAMMAR(identifier, | CAPACITY           ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| CHARACTER_SET_         {{ DBG_TRACE_GRAMMAR(identifier, | CHARACTER_SET_     ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| CHARSET                {{ DBG_TRACE_GRAMMAR(identifier, | CHARSET            ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| CHR                    {{ DBG_TRACE_GRAMMAR(identifier, | CHR                ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| CLOB_TO_CHAR           {{ DBG_TRACE_GRAMMAR(identifier, | CLOB_TO_CHAR       ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| CLOSE                  {{ DBG_TRACE_GRAMMAR(identifier, | CLOSE              ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| COLLATION              {{ DBG_TRACE_GRAMMAR(identifier, | COLLATION          ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| COLUMNS                {{ DBG_TRACE_GRAMMAR(identifier, | COLUMNS            ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| COMMENT                {{ DBG_TRACE_GRAMMAR(identifier, | COMMENT            ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| COMMITTED              {{ DBG_TRACE_GRAMMAR(identifier, | COMMITTED          ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| COST                   {{ DBG_TRACE_GRAMMAR(identifier, | COST               ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| CRITICAL               {{ DBG_TRACE_GRAMMAR(identifier, | CRITICAL           ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| CUME_DIST              {{ DBG_TRACE_GRAMMAR(identifier, | CUME_DIST          ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| DATE_ADD               {{ DBG_TRACE_GRAMMAR(identifier, | DATE_ADD           ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| DATE_SUB               {{ DBG_TRACE_GRAMMAR(identifier, | DATE_SUB           ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| DBLINK                 {{ DBG_TRACE_GRAMMAR(identifier, | DBLINK             ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| DBNAME                 {{ DBG_TRACE_GRAMMAR(identifier, | DBNAME             ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| DECREMENT              {{ DBG_TRACE_GRAMMAR(identifier, | DECREMENT          ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| DENSE_RANK             {{ DBG_TRACE_GRAMMAR(identifier, | DENSE_RANK         ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| DISK_SIZE              {{ DBG_TRACE_GRAMMAR(identifier, | DISK_SIZE          ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| DONT_REUSE_OID         {{ DBG_TRACE_GRAMMAR(identifier, | DONT_REUSE_OID     ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| ELT                    {{ DBG_TRACE_GRAMMAR(identifier, | ELT                ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| EMPTY                  {{ DBG_TRACE_GRAMMAR(identifier, | EMPTY              ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| ENCRYPT                {{ DBG_TRACE_GRAMMAR(identifier, | ENCRYPT            ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| ERROR_                 {{ DBG_TRACE_GRAMMAR(identifier, | ERROR_             ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| EXPLAIN                {{ DBG_TRACE_GRAMMAR(identifier, | EXPLAIN            ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| FIRST_VALUE            {{ DBG_TRACE_GRAMMAR(identifier, | FIRST_VALUE        ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| FULLSCAN               {{ DBG_TRACE_GRAMMAR(identifier, | FULLSCAN           ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| GE_INF_                {{ DBG_TRACE_GRAMMAR(identifier, | GE_INF_            ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| GE_LE_                 {{ DBG_TRACE_GRAMMAR(identifier, | GE_LE_             ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| GE_LT_                 {{ DBG_TRACE_GRAMMAR(identifier, | GE_LT_             ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| GRANTS                 {{ DBG_TRACE_GRAMMAR(identifier, | GRANTS             ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| GROUPS                 {{ DBG_TRACE_GRAMMAR(identifier, | GROUPS             ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| GROUP_CONCAT           {{ DBG_TRACE_GRAMMAR(identifier, | GROUP_CONCAT       ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| GT_INF_                {{ DBG_TRACE_GRAMMAR(identifier, | GT_INF_            ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| GT_LE_                 {{ DBG_TRACE_GRAMMAR(identifier, | GT_LE_             ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| GT_LT_                 {{ DBG_TRACE_GRAMMAR(identifier, | GT_LT_             ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| HASH                   {{ DBG_TRACE_GRAMMAR(identifier, | HASH               ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| HEADER                 {{ DBG_TRACE_GRAMMAR(identifier, | HEADER             ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| HEAP                   {{ DBG_TRACE_GRAMMAR(identifier, | HEAP               ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| HOST                   {{ DBG_TRACE_GRAMMAR(identifier, | HOST               ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| IFNULL                 {{ DBG_TRACE_GRAMMAR(identifier, | IFNULL             ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| INACTIVE               {{ DBG_TRACE_GRAMMAR(identifier, | INACTIVE           ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| INCREMENT              {{ DBG_TRACE_GRAMMAR(identifier, | INCREMENT          ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| INDEXES                {{ DBG_TRACE_GRAMMAR(identifier, | INDEXES            ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| INDEX_PREFIX           {{ DBG_TRACE_GRAMMAR(identifier, | INDEX_PREFIX       ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| INFINITE_              {{ DBG_TRACE_GRAMMAR(identifier, | INFINITE_          ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| INF_LE_                {{ DBG_TRACE_GRAMMAR(identifier, | INF_LE_            ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| INF_LT_                {{ DBG_TRACE_GRAMMAR(identifier, | INF_LT_            ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| INSTANCES              {{ DBG_TRACE_GRAMMAR(identifier, | INSTANCES          ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| INVALIDATE             {{ DBG_TRACE_GRAMMAR(identifier, | INVALIDATE         ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| INVISIBLE              {{ DBG_TRACE_GRAMMAR(identifier, | INVISIBLE          ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| ISNULL                 {{ DBG_TRACE_GRAMMAR(identifier, | ISNULL             ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| JAVA                   {{ DBG_TRACE_GRAMMAR(identifier, | JAVA               ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| JOB                    {{ DBG_TRACE_GRAMMAR(identifier, | JOB                ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| JSON_ARRAYAGG          {{ DBG_TRACE_GRAMMAR(identifier, | JSON_ARRAYAGG      ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| JSON_ARRAY_APPEND      {{ DBG_TRACE_GRAMMAR(identifier, | JSON_ARRAY_APPEND  ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| JSON_ARRAY_INSERT      {{ DBG_TRACE_GRAMMAR(identifier, | JSON_ARRAY_INSERT  ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| JSON_ARRAY_LEX         {{ DBG_TRACE_GRAMMAR(identifier, | JSON_ARRAY_LEX     ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| JSON_CONTAINS          {{ DBG_TRACE_GRAMMAR(identifier, | JSON_CONTAINS      ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| JSON_CONTAINS_PATH     {{ DBG_TRACE_GRAMMAR(identifier, | JSON_CONTAINS_PATH ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| JSON_DEPTH             {{ DBG_TRACE_GRAMMAR(identifier, | JSON_DEPTH         ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| JSON_EXTRACT           {{ DBG_TRACE_GRAMMAR(identifier, | JSON_EXTRACT       ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| JSON_GET_ALL_PATHS     {{ DBG_TRACE_GRAMMAR(identifier, | JSON_GET_ALL_PATHS ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| JSON_INSERT            {{ DBG_TRACE_GRAMMAR(identifier, | JSON_INSERT        ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| JSON_KEYS              {{ DBG_TRACE_GRAMMAR(identifier, | JSON_KEYS          ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| JSON_LENGTH            {{ DBG_TRACE_GRAMMAR(identifier, | JSON_LENGTH        ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| JSON_MERGE             {{ DBG_TRACE_GRAMMAR(identifier, | JSON_MERGE         ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| JSON_MERGE_PATCH       {{ DBG_TRACE_GRAMMAR(identifier, | JSON_MERGE_PATCH   ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| JSON_MERGE_PRESERVE    {{ DBG_TRACE_GRAMMAR(identifier, | JSON_MERGE_PRESERVE); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| JSON_OBJECTAGG         {{ DBG_TRACE_GRAMMAR(identifier, | JSON_OBJECTAGG     ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| JSON_OBJECT_LEX        {{ DBG_TRACE_GRAMMAR(identifier, | JSON_OBJECT_LEX    ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| JSON_PRETTY            {{ DBG_TRACE_GRAMMAR(identifier, | JSON_PRETTY        ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| JSON_QUOTE             {{ DBG_TRACE_GRAMMAR(identifier, | JSON_QUOTE         ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| JSON_REMOVE            {{ DBG_TRACE_GRAMMAR(identifier, | JSON_REMOVE        ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| JSON_REPLACE           {{ DBG_TRACE_GRAMMAR(identifier, | JSON_REPLACE       ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| JSON_SEARCH            {{ DBG_TRACE_GRAMMAR(identifier, | JSON_SEARCH        ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| JSON_SET               {{ DBG_TRACE_GRAMMAR(identifier, | JSON_SET           ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| JSON_TABLE             {{ DBG_TRACE_GRAMMAR(identifier, | JSON_TABLE         ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| JSON_TYPE              {{ DBG_TRACE_GRAMMAR(identifier, | JSON_TYPE          ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| JSON_UNQUOTE           {{ DBG_TRACE_GRAMMAR(identifier, | JSON_UNQUOTE       ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| JSON_VALID             {{ DBG_TRACE_GRAMMAR(identifier, | JSON_VALID         ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| KEYS                   {{ DBG_TRACE_GRAMMAR(identifier, | KEYS               ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| LAG                    {{ DBG_TRACE_GRAMMAR(identifier, | LAG                ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| LAST_VALUE             {{ DBG_TRACE_GRAMMAR(identifier, | LAST_VALUE         ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| LCASE                  {{ DBG_TRACE_GRAMMAR(identifier, | LCASE              ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| LEAD                   {{ DBG_TRACE_GRAMMAR(identifier, | LEAD               ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| LOCK_                  {{ DBG_TRACE_GRAMMAR(identifier, | LOCK_              ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| LOG                    {{ DBG_TRACE_GRAMMAR(identifier, | LOG                ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| MAXIMUM                {{ DBG_TRACE_GRAMMAR(identifier, | MAXIMUM            ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| MAXVALUE               {{ DBG_TRACE_GRAMMAR(identifier, | MAXVALUE           ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| MEDIAN                 {{ DBG_TRACE_GRAMMAR(identifier, | MEDIAN             ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| MEMBERS                {{ DBG_TRACE_GRAMMAR(identifier, | MEMBERS            ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| MINVALUE               {{ DBG_TRACE_GRAMMAR(identifier, | MINVALUE           ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| NAME                   {{ DBG_TRACE_GRAMMAR(identifier, | NAME               ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| NESTED                 {{ DBG_TRACE_GRAMMAR(identifier, | NESTED             ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| NOCACHE                {{ DBG_TRACE_GRAMMAR(identifier, | NOCACHE            ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| NOMAXVALUE             {{ DBG_TRACE_GRAMMAR(identifier, | NOMAXVALUE         ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| NOMINVALUE             {{ DBG_TRACE_GRAMMAR(identifier, | NOMINVALUE         ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| NTH_VALUE              {{ DBG_TRACE_GRAMMAR(identifier, | NTH_VALUE          ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| NTILE                  {{ DBG_TRACE_GRAMMAR(identifier, | NTILE              ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| NULLS                  {{ DBG_TRACE_GRAMMAR(identifier, | NULLS              ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| OFFSET                 {{ DBG_TRACE_GRAMMAR(identifier, | OFFSET             ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| ONLINE                 {{ DBG_TRACE_GRAMMAR(identifier, | ONLINE             ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| OPEN                   {{ DBG_TRACE_GRAMMAR(identifier, | OPEN               ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| ORDINALITY             {{ DBG_TRACE_GRAMMAR(identifier, | ORDINALITY         ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| OWNER                  {{ DBG_TRACE_GRAMMAR(identifier, | OWNER              ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| PAGE                   {{ DBG_TRACE_GRAMMAR(identifier, | PAGE               ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| PARALLEL               {{ DBG_TRACE_GRAMMAR(identifier, | PARALLEL           ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| PARTITIONING           {{ DBG_TRACE_GRAMMAR(identifier, | PARTITIONING       ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| PARTITIONS             {{ DBG_TRACE_GRAMMAR(identifier, | PARTITIONS         ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| PASSWORD               {{ DBG_TRACE_GRAMMAR(identifier, | PASSWORD           ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| PATH                   {{ DBG_TRACE_GRAMMAR(identifier, | PATH               ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| PERCENTILE_CONT        {{ DBG_TRACE_GRAMMAR(identifier, | PERCENTILE_CONT    ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| PERCENTILE_DISC        {{ DBG_TRACE_GRAMMAR(identifier, | PERCENTILE_DISC    ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| PERCENT_RANK           {{ DBG_TRACE_GRAMMAR(identifier, | PERCENT_RANK       ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| PORT                   {{ DBG_TRACE_GRAMMAR(identifier, | PORT               ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| PRINT                  {{ DBG_TRACE_GRAMMAR(identifier, | PRINT              ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| PRIORITY               {{ DBG_TRACE_GRAMMAR(identifier, | PRIORITY           ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| PROPERTIES             {{ DBG_TRACE_GRAMMAR(identifier, | PROPERTIES         ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| QUARTER                {{ DBG_TRACE_GRAMMAR(identifier, | QUARTER            ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| QUEUES                 {{ DBG_TRACE_GRAMMAR(identifier, | QUEUES             ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| RANGE_                 {{ DBG_TRACE_GRAMMAR(identifier, | RANGE_             ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| RANK                   {{ DBG_TRACE_GRAMMAR(identifier, | RANK               ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| REGEXP_COUNT           {{ DBG_TRACE_GRAMMAR(identifier, | REGEXP_COUNT       ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| REGEXP_INSTR           {{ DBG_TRACE_GRAMMAR(identifier, | REGEXP_INSTR       ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| REGEXP_LIKE            {{ DBG_TRACE_GRAMMAR(identifier, | REGEXP_LIKE        ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| REGEXP_REPLACE         {{ DBG_TRACE_GRAMMAR(identifier, | REGEXP_REPLACE     ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| REGEXP_SUBSTR          {{ DBG_TRACE_GRAMMAR(identifier, | REGEXP_SUBSTR      ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| REJECT_                {{ DBG_TRACE_GRAMMAR(identifier, | REJECT_            ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| REMOVE                 {{ DBG_TRACE_GRAMMAR(identifier, | REMOVE             ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| REORGANIZE             {{ DBG_TRACE_GRAMMAR(identifier, | REORGANIZE         ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| REPEATABLE             {{ DBG_TRACE_GRAMMAR(identifier, | REPEATABLE         ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| RESPECT                {{ DBG_TRACE_GRAMMAR(identifier, | RESPECT            ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| RETAIN                 {{ DBG_TRACE_GRAMMAR(identifier, | RETAIN             ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| REUSE_OID              {{ DBG_TRACE_GRAMMAR(identifier, | REUSE_OID          ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| REVERSE                {{ DBG_TRACE_GRAMMAR(identifier, | REVERSE            ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| ROW_NUMBER             {{ DBG_TRACE_GRAMMAR(identifier, | ROW_NUMBER         ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| SECTIONS               {{ DBG_TRACE_GRAMMAR(identifier, | SECTIONS           ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| SEPARATOR              {{ DBG_TRACE_GRAMMAR(identifier, | SEPARATOR          ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| SERIAL                 {{ DBG_TRACE_GRAMMAR(identifier, | SERIAL             ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| SERVER                 {{ DBG_TRACE_GRAMMAR(identifier, | SERVER             ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| SHOW                   {{ DBG_TRACE_GRAMMAR(identifier, | SHOW               ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| SLOTS                  {{ DBG_TRACE_GRAMMAR(identifier, | SLOTS              ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| SLOTTED                {{ DBG_TRACE_GRAMMAR(identifier, | SLOTTED            ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| STABILITY              {{ DBG_TRACE_GRAMMAR(identifier, | STABILITY          ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| START_                 {{ DBG_TRACE_GRAMMAR(identifier, | START_             ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| STATEMENT              {{ DBG_TRACE_GRAMMAR(identifier, | STATEMENT          ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| STATUS                 {{ DBG_TRACE_GRAMMAR(identifier, | STATUS             ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| STDDEV                 {{ DBG_TRACE_GRAMMAR(identifier, | STDDEV             ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| STDDEV_POP             {{ DBG_TRACE_GRAMMAR(identifier, | STDDEV_POP         ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| STDDEV_SAMP            {{ DBG_TRACE_GRAMMAR(identifier, | STDDEV_SAMP        ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| STR_TO_DATE            {{ DBG_TRACE_GRAMMAR(identifier, | STR_TO_DATE        ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| SUBDATE                {{ DBG_TRACE_GRAMMAR(identifier, | SUBDATE            ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| SYSTEM                 {{ DBG_TRACE_GRAMMAR(identifier, | SYSTEM             ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| TABLES                 {{ DBG_TRACE_GRAMMAR(identifier, | TABLES             ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| TEXT                   {{ DBG_TRACE_GRAMMAR(identifier, | TEXT               ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| THAN                   {{ DBG_TRACE_GRAMMAR(identifier, | THAN               ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| THREADS                {{ DBG_TRACE_GRAMMAR(identifier, | THREADS            ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| TIMEOUT                {{ DBG_TRACE_GRAMMAR(identifier, | TIMEOUT            ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| TIMEZONE               {{ DBG_TRACE_GRAMMAR(identifier, | TIMEZONE           ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| TRACE                  {{ DBG_TRACE_GRAMMAR(identifier, | TRACE              ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| TRAN                   {{ DBG_TRACE_GRAMMAR(identifier, | TRAN               ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| TRIGGERS               {{ DBG_TRACE_GRAMMAR(identifier, | TRIGGERS           ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| UCASE                  {{ DBG_TRACE_GRAMMAR(identifier, | UCASE              ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| UNCOMMITTED            {{ DBG_TRACE_GRAMMAR(identifier, | UNCOMMITTED        ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| VARIANCE               {{ DBG_TRACE_GRAMMAR(identifier, | VARIANCE           ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| VAR_POP                {{ DBG_TRACE_GRAMMAR(identifier, | VAR_POP            ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| VAR_SAMP               {{ DBG_TRACE_GRAMMAR(identifier, | VAR_SAMP           ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| VISIBLE                {{ DBG_TRACE_GRAMMAR(identifier, | VISIBLE            ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| VOLUME                 {{ DBG_TRACE_GRAMMAR(identifier, | VOLUME             ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| WEEK                   {{ DBG_TRACE_GRAMMAR(identifier, | WEEK               ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| WITHIN                 {{ DBG_TRACE_GRAMMAR(identifier, | WITHIN             ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }}
	| WORKSPACE              {{ DBG_TRACE_GRAMMAR(identifier, | WORKSPACE          ); SET_CPTR_2_PTNAME($$, $1, @$.buffer_pos);  }} 
/*}}}*/
	;

escape_literal
	: string_literal_or_input_hv
		{{ DBG_TRACE_GRAMMAR(escape_literal, : string_literal_or_input_hv);

			PT_NODE *node = $1;
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| Null
		{{ DBG_TRACE_GRAMMAR(escape_literal, | Null);

			PT_NODE *node = parser_new_node (this_parser, PT_VALUE);
			if (node)
			  node->type_enum = PT_TYPE_NULL;
			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

string_literal_or_input_hv
	: char_string_literal
		{{ DBG_TRACE_GRAMMAR(string_literal_or_input_hv, : char_string_literal);

			PT_NODE *node = $1;

			pt_value_set_collation_info (this_parser, node, NULL);

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| host_param_input
		{{ DBG_TRACE_GRAMMAR(string_literal_or_input_hv, | host_param_input);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
        ;


char_string_literal
	: char_string_literal CHAR_STRING
		{{ DBG_TRACE_GRAMMAR(char_string_literal, : char_string_literal CHAR_STRING);

			PT_NODE *str = $1;
			if (str)
			  {
			    str->info.value.data_value.str =
			      pt_append_bytes (this_parser, str->info.value.data_value.str, $2,
					       strlen ($2));
			    str->info.value.text = NULL;
			    PT_NODE_PRINT_VALUE_TO_TEXT (this_parser, str);
			  }

			$$ = str;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| char_string
		{{ DBG_TRACE_GRAMMAR(char_string_literal, | char_string);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

char_string
	: CHAR_STRING
		{{ DBG_TRACE_GRAMMAR(char_string, : CHAR_STRING);

			PT_NODE *node = NULL;
			PT_TYPE_ENUM typ = PT_TYPE_CHAR;
			INTL_CODESET charset;
			int collation_id;
			bool force;

			if (lang_get_parser_use_client_charset ())
			  {
			    charset = lang_get_client_charset ();
			    collation_id = lang_get_client_collation ();
			    force = false;
			  }
			else
			  {
			    charset = LANG_SYS_CODESET;
			    collation_id = LANG_SYS_COLLATION;
			    force = true;
			  }

                        node = pt_create_char_string_literal (this_parser,
							      PT_TYPE_CHAR,
                                                              $1, charset);

			if (node)
			  {
			    pt_value_set_charset_coll (this_parser, node,
						       charset, collation_id,
						       force);
			    node->info.value.has_cs_introducer = force;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| NCHAR_STRING
		{{ DBG_TRACE_GRAMMAR(char_string, | NCHAR_STRING);
			PT_NODE *node = NULL;
			INTL_CODESET charset;
			int collation_id;
			bool force;

			if (lang_get_parser_use_client_charset ())
			  {
			    charset = lang_get_client_charset ();
			    collation_id = lang_get_client_collation ();
			    force = false;
			  }
			else
			  {
			    charset = LANG_SYS_CODESET;
			    collation_id = LANG_SYS_COLLATION;
			    force = true;
			  }

			node = pt_create_char_string_literal (this_parser,
							      PT_TYPE_NCHAR,
							      $1, charset);

			if (node && lang_get_parser_use_client_charset ())
			  {
			    pt_value_set_charset_coll (this_parser, node,
						       charset, collation_id,
						       force);
			    node->info.value.has_cs_introducer = force;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| BINARY_STRING
		{{ DBG_TRACE_GRAMMAR(char_string, | BINARY_STRING);

			PT_NODE *node = NULL;

			node = pt_create_char_string_literal (this_parser, PT_TYPE_CHAR,
							      $1, INTL_CODESET_RAW_BYTES);

			if (node)
			  {
			    pt_value_set_charset_coll (this_parser, node,
						       INTL_CODESET_RAW_BYTES,
						       LANG_COLL_BINARY,
						       true);
			    node->info.value.has_cs_introducer = true;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| EUCKR_STRING
		{{ DBG_TRACE_GRAMMAR(char_string, | EUCKR_STRING);

			PT_NODE *node = NULL;

			node = pt_create_char_string_literal (this_parser, PT_TYPE_CHAR,
							      $1, INTL_CODESET_KSC5601_EUC);

			if (node)
			  {
			    pt_value_set_charset_coll (this_parser, node,
						       INTL_CODESET_KSC5601_EUC,
						       LANG_COLL_EUCKR_BINARY,
						       true);
			    node->info.value.has_cs_introducer = true;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| ISO_STRING
		{{ DBG_TRACE_GRAMMAR(char_string, | ISO_STRING);

			PT_NODE *node = NULL;

			node = pt_create_char_string_literal (this_parser, PT_TYPE_CHAR,
							      $1, INTL_CODESET_ISO88591);

			if (node)
			  {
			    pt_value_set_charset_coll (this_parser, node,
						       INTL_CODESET_ISO88591,
						       LANG_COLL_ISO_BINARY,
						       true);
			    node->info.value.has_cs_introducer = true;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| UTF8_STRING
		{{ DBG_TRACE_GRAMMAR(char_string, | UTF8_STRING);

			PT_NODE *node = NULL;

			node = pt_create_char_string_literal (this_parser, PT_TYPE_CHAR,
							      $1, INTL_CODESET_UTF8);

			if (node)
			  {
			    pt_value_set_charset_coll (this_parser, node,
						       INTL_CODESET_UTF8,
						       LANG_COLL_UTF8_BINARY,
						       true);
			    node->info.value.has_cs_introducer = true;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;


bit_string_literal
	: bit_string_literal CHAR_STRING
		{{ DBG_TRACE_GRAMMAR(bit_string_literal, : bit_string_literal CHAR_STRING);

			PT_NODE *str = $1;
			if (str)
			  {
			    str->info.value.data_value.str =
			      pt_append_bytes (this_parser, str->info.value.data_value.str, $2,
					       strlen ($2));
			    str->info.value.text = NULL;
			    PT_NODE_PRINT_VALUE_TO_TEXT (this_parser, str);
			  }

			$$ = str;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| bit_string
		{{ DBG_TRACE_GRAMMAR(bit_string_literal, | bit_string);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

bit_string
	: BIT_STRING
		{{ DBG_TRACE_GRAMMAR(bit_string, : BIT_STRING);

			PT_NODE *node = parser_new_node (this_parser, PT_VALUE);

			if (node)
			  {
			    node->type_enum = PT_TYPE_BIT;
			    node->info.value.string_type = 'B';
			    node->info.value.data_value.str =
			      pt_append_bytes (this_parser, NULL, $1, strlen ($1));
			    PT_NODE_PRINT_VALUE_TO_TEXT (this_parser, node);
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| HEX_STRING
		{{ DBG_TRACE_GRAMMAR(bit_string, | HEX_STRING);

			PT_NODE *node = parser_new_node (this_parser, PT_VALUE);

			if (node)
			  {
			    node->type_enum = PT_TYPE_BIT;
			    node->info.value.string_type = 'X';
			    node->info.value.data_value.str =
			      pt_append_bytes (this_parser, NULL, $1, strlen ($1));
			    PT_NODE_PRINT_VALUE_TO_TEXT (this_parser, node);
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

unsigned_integer
	: UNSIGNED_INTEGER
		{{ DBG_TRACE_GRAMMAR(unsigned_integer, : UNSIGNED_INTEGER);

			PT_NODE *val = parser_new_node (this_parser, PT_VALUE);
			if (val)
			  {
			    val->info.value.text = $1;

			    if ((strlen (val->info.value.text) <= 9) ||
				(strlen (val->info.value.text) == 10 &&
				 (val->info.value.text[0] == '0' || val->info.value.text[0] == '1')))
			      {
				val->info.value.data_value.i = atol ($1);
				val->type_enum = PT_TYPE_INTEGER;
			      }
			    else if ((strlen (val->info.value.text) <= 18) ||
				     (strlen (val->info.value.text) == 19 &&
				      (val->info.value.text[0] >= '0' &&
				       val->info.value.text[0] <= '8')))
			      {
				val->info.value.data_value.bigint = atoll ($1);
				val->type_enum = PT_TYPE_BIGINT;
			      }
			    else
			      {
				const char *max_big_int = "9223372036854775807";

				if ((strlen (val->info.value.text) == 19) &&
				    (strcmp (val->info.value.text, max_big_int) <= 0))
				  {
				    val->info.value.data_value.bigint = atoll ($1);
				    val->type_enum = PT_TYPE_BIGINT;
				  }
				else
				  {
				    val->type_enum = PT_TYPE_NUMERIC;
				    val->info.value.data_value.str =
				      pt_append_bytes (this_parser, NULL,
						       val->info.value.text,
						       strlen (val->info.
							       value.text));
				  }
			      }
			  }

			$$ = val;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

unsigned_int32
	: UNSIGNED_INTEGER
		{{ DBG_TRACE_GRAMMAR(unsigned_int32, : UNSIGNED_INTEGER);

			PT_NODE *val;
			int result = 0;
			int int_val;

			val = parser_new_node (this_parser, PT_VALUE);
			if (val)
			  {
			    result = parse_int (&int_val, $1, 10);
			    if (result != 0)
			      {
				PT_ERRORmf (this_parser, val, MSGCAT_SET_PARSER_SYNTAX,
				            MSGCAT_SYNTAX_INVALID_UNSIGNED_INT32, $1);
			      }

			    val->info.value.data_value.i = int_val;
			    val->type_enum = PT_TYPE_INTEGER;
			  }
			$$ = val;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

unsigned_real
	: UNSIGNED_REAL
		{{ DBG_TRACE_GRAMMAR(unsigned_real, : UNSIGNED_REAL);

			double dval;
			PT_NODE *val = parser_new_node (this_parser, PT_VALUE);
			if (val)
			  {

			    if (strchr ($1, 'E') != NULL || strchr ($1, 'e') != NULL)
			      {

				errno = 0;
				dval = strtod ($1, NULL);
				if (errno == ERANGE)
				  {
				    PT_ERRORmf2 (this_parser, val, MSGCAT_SET_PARSER_SYNTAX,
				                 MSGCAT_SYNTAX_FLT_DBL_OVERFLOW, $1, pt_show_type_enum (PT_TYPE_DOUBLE));
				  }
				val->info.value.text = $1;
				val->type_enum = PT_TYPE_DOUBLE;
				val->info.value.data_value.d = dval;
			      }
			    else if (strchr ($1, 'F') != NULL || strchr ($1, 'f') != NULL)
			      {

				errno = 0;
				dval = strtod ($1, NULL);
				if (errno == ERANGE || (dval > FLT_MAX || dval < FLT_MIN))
				  {
				    PT_ERRORmf2 (this_parser, val, MSGCAT_SET_PARSER_SYNTAX,
				                 MSGCAT_SYNTAX_FLT_DBL_OVERFLOW, $1, pt_show_type_enum (PT_TYPE_FLOAT));
				  }
				val->info.value.text = $1;
				val->type_enum = PT_TYPE_FLOAT;
				val->info.value.data_value.f = (float) dval;
			      }
			    else
			      {
				val->info.value.text = $1;
				val->type_enum = PT_TYPE_NUMERIC;
				val->info.value.data_value.str =
				  pt_append_bytes (this_parser, NULL,
						   val->info.value.text,
						   strlen (val->info.value.
							   text));
			      }
			  }

			$$ = val;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

monetary_literal
	: YEN_SIGN of_integer_real_literal
		{{ DBG_TRACE_GRAMMAR(monetary_literal, : YEN_SIGN of_integer_real_literal);

			char *str, *txt;
			PT_NODE *val = parser_new_node (this_parser, PT_VALUE);

			str = pt_append_string (this_parser, NULL, YEN_SIGN_TEXT);
			txt = $2;

			if (val)
			  {
			    pt_value_set_monetary (this_parser, val, str, txt, PT_CURRENCY_YEN);
			  }

			$$ = val;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| DOLLAR_SIGN of_integer_real_literal
		{{ DBG_TRACE_GRAMMAR(monetary_literal, | DOLLAR_SIGN of_integer_real_literal);

			char *str, *txt;
			PT_NODE *val = parser_new_node (this_parser, PT_VALUE);

			str = pt_append_string (this_parser, NULL, DOLLAR_SIGN_TEXT);
			txt = $2;

			if (val)
			  {
			    pt_value_set_monetary (this_parser, val, str, txt, PT_CURRENCY_DOLLAR);
			  }

			$$ = val;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| WON_SIGN of_integer_real_literal
		{{ DBG_TRACE_GRAMMAR(monetary_literal, | WON_SIGN of_integer_real_literal);

			char *str, *txt;
			PT_NODE *val = parser_new_node (this_parser, PT_VALUE);

			str = pt_append_string (this_parser, NULL, WON_SIGN_TEXT);
			txt = $2;

			if (val)
			  {
			    pt_value_set_monetary (this_parser, val, str, txt, PT_CURRENCY_WON);
			  }

			$$ = val;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| TURKISH_LIRA_SIGN of_integer_real_literal
		{{ DBG_TRACE_GRAMMAR(monetary_literal, | TURKISH_LIRA_SIGN of_integer_real_literal);

			char *str, *txt;
			PT_NODE *val = parser_new_node (this_parser, PT_VALUE);

			str = pt_append_string (this_parser, NULL, TURKISH_LIRA_TEXT);
			txt = $2;

			if (val)
			  {
			    pt_value_set_monetary (this_parser, val, str, txt, PT_CURRENCY_TL);
			  }

			$$ = val;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| BRITISH_POUND_SIGN of_integer_real_literal
		{{ DBG_TRACE_GRAMMAR(monetary_literal, | BRITISH_POUND_SIGN of_integer_real_literal);

			char *str, *txt;
			PT_NODE *val = parser_new_node (this_parser, PT_VALUE);

			str = pt_append_string (this_parser, NULL, BRITISH_POUND_TEXT);
			txt = $2;

			if (val)
			  {
			    pt_value_set_monetary (this_parser, val, str, txt, PT_CURRENCY_BRITISH_POUND);
			  }

			$$ = val;

		DBG_PRINT}}
	| CAMBODIAN_RIEL_SIGN of_integer_real_literal
		{{ DBG_TRACE_GRAMMAR(monetary_literal, | CAMBODIAN_RIEL_SIGN of_integer_real_literal);

			char *str, *txt;
			PT_NODE *val = parser_new_node (this_parser, PT_VALUE);

			str = pt_append_string (this_parser, NULL, CAMBODIAN_RIEL_TEXT);
			txt = $2;

			if (val)
			  {
			    pt_value_set_monetary (this_parser, val, str, txt, PT_CURRENCY_CAMBODIAN_RIEL);
			  }

			$$ = val;

		DBG_PRINT}}
	| CHINESE_RENMINBI_SIGN of_integer_real_literal
		{{ DBG_TRACE_GRAMMAR(monetary_literal, | CHINESE_RENMINBI_SIGN of_integer_real_literal);

			char *str, *txt;
			PT_NODE *val = parser_new_node (this_parser, PT_VALUE);

			str = pt_append_string (this_parser, NULL, CHINESE_RENMINBI_TEXT);
			txt = $2;

			if (val)
			  {
			    pt_value_set_monetary (this_parser, val, str, txt, PT_CURRENCY_CHINESE_RENMINBI);
			  }

			$$ = val;

		DBG_PRINT}}
	| INDIAN_RUPEE_SIGN of_integer_real_literal
		{{ DBG_TRACE_GRAMMAR(monetary_literal, | INDIAN_RUPEE_SIGN of_integer_real_literal);

			char *str, *txt;
			PT_NODE *val = parser_new_node (this_parser, PT_VALUE);

			str = pt_append_string (this_parser, NULL, INDIAN_RUPEE_TEXT);
			txt = $2;

			if (val)
			  {
			    pt_value_set_monetary (this_parser, val, str, txt, PT_CURRENCY_INDIAN_RUPEE);
			  }

			$$ = val;

		DBG_PRINT}}
	| RUSSIAN_RUBLE_SIGN of_integer_real_literal
		{{ DBG_TRACE_GRAMMAR(monetary_literal, | RUSSIAN_RUBLE_SIGN of_integer_real_literal);

			char *str, *txt;
			PT_NODE *val = parser_new_node (this_parser, PT_VALUE);

			str = pt_append_string (this_parser, NULL, RUSSIAN_RUBLE_TEXT);
			txt = $2;

			if (val)
			  {
			    pt_value_set_monetary (this_parser, val, str, txt, PT_CURRENCY_RUSSIAN_RUBLE);
			  }

			$$ = val;

		DBG_PRINT}}
	| AUSTRALIAN_DOLLAR_SIGN of_integer_real_literal
		{{ DBG_TRACE_GRAMMAR(monetary_literal, | AUSTRALIAN_DOLLAR_SIGN of_integer_real_literal);

			char *str, *txt;
			PT_NODE *val = parser_new_node (this_parser, PT_VALUE);

			str = pt_append_string (this_parser, NULL, AUSTRALIAN_DOLLAR_TEXT);
			txt = $2;

			if (val)
			  {
			    pt_value_set_monetary (this_parser, val, str, txt, PT_CURRENCY_AUSTRALIAN_DOLLAR);
			  }

			$$ = val;

		DBG_PRINT}}
	| CANADIAN_DOLLAR_SIGN of_integer_real_literal
		{{ DBG_TRACE_GRAMMAR(monetary_literal, | CANADIAN_DOLLAR_SIGN of_integer_real_literal);

			char *str, *txt;
			PT_NODE *val = parser_new_node (this_parser, PT_VALUE);

			str = pt_append_string (this_parser, NULL, CANADIAN_DOLLAR_TEXT);
			txt = $2;

			if (val)
			  {
			    pt_value_set_monetary (this_parser, val, str, txt, PT_CURRENCY_CANADIAN_DOLLAR);
			  }

			$$ = val;

		DBG_PRINT}}
	| BRASILIAN_REAL_SIGN of_integer_real_literal
		{{ DBG_TRACE_GRAMMAR(monetary_literal, | BRASILIAN_REAL_SIGN of_integer_real_literal);

			char *str, *txt;
			PT_NODE *val = parser_new_node (this_parser, PT_VALUE);

			str = pt_append_string (this_parser, NULL, BRASILIAN_REAL_TEXT);
			txt = $2;

			if (val)
			  {
			    pt_value_set_monetary (this_parser, val, str, txt, PT_CURRENCY_BRASILIAN_REAL);
			  }

			$$ = val;

		DBG_PRINT}}
	| ROMANIAN_LEU_SIGN of_integer_real_literal
		{{ DBG_TRACE_GRAMMAR(monetary_literal, | ROMANIAN_LEU_SIGN of_integer_real_literal);

			char *str, *txt;
			PT_NODE *val = parser_new_node (this_parser, PT_VALUE);

			str = pt_append_string (this_parser, NULL, ROMANIAN_LEU_TEXT);
			txt = $2;

			if (val)
			  {
			    pt_value_set_monetary (this_parser, val, str, txt, PT_CURRENCY_ROMANIAN_LEU);
			  }

			$$ = val;

		DBG_PRINT}}
	| EURO_SIGN of_integer_real_literal
		{{ DBG_TRACE_GRAMMAR(monetary_literal, | EURO_SIGN of_integer_real_literal);

			char *str, *txt;
			PT_NODE *val = parser_new_node (this_parser, PT_VALUE);

			str = pt_append_string (this_parser, NULL, EURO_TEXT);
			txt = $2;

			if (val)
			  {
			    pt_value_set_monetary (this_parser, val, str, txt, PT_CURRENCY_EURO);
			  }

			$$ = val;

		DBG_PRINT}}
	| SWISS_FRANC_SIGN of_integer_real_literal
		{{ DBG_TRACE_GRAMMAR(monetary_literal, | SWISS_FRANC_SIGN of_integer_real_literal);

			char *str, *txt;
			PT_NODE *val = parser_new_node (this_parser, PT_VALUE);

			str = pt_append_string (this_parser, NULL, SWISS_FRANC_TEXT);
			txt = $2;

			if (val)
			  {
			    pt_value_set_monetary (this_parser, val, str, txt, PT_CURRENCY_SWISS_FRANC);
			  }

			$$ = val;

		DBG_PRINT}}
	| DANISH_KRONE_SIGN of_integer_real_literal
		{{ DBG_TRACE_GRAMMAR(monetary_literal, | DANISH_KRONE_SIGN of_integer_real_literal);

			char *str, *txt;
			PT_NODE *val = parser_new_node (this_parser, PT_VALUE);

			str = pt_append_string (this_parser, NULL, DANISH_KRONE_TEXT);
			txt = $2;

			if (val)
			  {
			    pt_value_set_monetary (this_parser, val, str, txt, PT_CURRENCY_DANISH_KRONE);
			  }

			$$ = val;

		DBG_PRINT}}
	| NORWEGIAN_KRONE_SIGN of_integer_real_literal
		{{ DBG_TRACE_GRAMMAR(monetary_literal, | NORWEGIAN_KRONE_SIGN of_integer_real_literal);

			char *str, *txt;
			PT_NODE *val = parser_new_node (this_parser, PT_VALUE);

			str = pt_append_string (this_parser, NULL, NORWEGIAN_KRONE_TEXT);
			txt = $2;

			if (val)
			  {
			    pt_value_set_monetary (this_parser, val, str, txt, PT_CURRENCY_NORWEGIAN_KRONE);
			  }

			$$ = val;

		DBG_PRINT}}
	| BULGARIAN_LEV_SIGN of_integer_real_literal
		{{ DBG_TRACE_GRAMMAR(monetary_literal, | BULGARIAN_LEV_SIGN of_integer_real_literal);

			char *str, *txt;
			PT_NODE *val = parser_new_node (this_parser, PT_VALUE);

			str = pt_append_string (this_parser, NULL, BULGARIAN_LEV_TEXT);
			txt = $2;

			if (val)
			  {
			    pt_value_set_monetary (this_parser, val, str, txt, PT_CURRENCY_BULGARIAN_LEV);
			  }

			$$ = val;

		DBG_PRINT}}
	| VIETNAMESE_DONG_SIGN of_integer_real_literal
		{{ DBG_TRACE_GRAMMAR(monetary_literal, | VIETNAMESE_DONG_SIGN of_integer_real_literal);

			char *str, *txt;
			PT_NODE *val = parser_new_node (this_parser, PT_VALUE);

			str = pt_append_string (this_parser, NULL, VIETNAMESE_DONG_TEXT);
			txt = $2;

			if (val)
			  {
			    pt_value_set_monetary (this_parser, val, str, txt, PT_CURRENCY_VIETNAMESE_DONG);
			  }

			$$ = val;

		DBG_PRINT}}
	| CZECH_KORUNA_SIGN of_integer_real_literal
		{{ DBG_TRACE_GRAMMAR(monetary_literal, | CZECH_KORUNA_SIGN of_integer_real_literal);

			char *str, *txt;
			PT_NODE *val = parser_new_node (this_parser, PT_VALUE);

			str = pt_append_string (this_parser, NULL, CZECH_KORUNA_TEXT);
			txt = $2;

			if (val)
			  {
			    pt_value_set_monetary (this_parser, val, str, txt, PT_CURRENCY_CZECH_KORUNA);
			  }

			$$ = val;

		DBG_PRINT}}
	| POLISH_ZLOTY_SIGN of_integer_real_literal
		{{ DBG_TRACE_GRAMMAR(monetary_literal, | POLISH_ZLOTY_SIGN of_integer_real_literal);

			char *str, *txt;
			PT_NODE *val = parser_new_node (this_parser, PT_VALUE);

			str = pt_append_string (this_parser, NULL, POLISH_ZLOTY_TEXT);
			txt = $2;

			if (val)
			  {
			    pt_value_set_monetary (this_parser, val, str, txt, PT_CURRENCY_POLISH_ZLOTY);
			  }

			$$ = val;

		DBG_PRINT}}
	| SWEDISH_KRONA_SIGN of_integer_real_literal
		{{ DBG_TRACE_GRAMMAR(monetary_literal, | SWEDISH_KRONA_SIGN of_integer_real_literal);

			char *str, *txt;
			PT_NODE *val = parser_new_node (this_parser, PT_VALUE);

			str = pt_append_string (this_parser, NULL, SWEDISH_KRONA_TEXT);
			txt = $2;

			if (val)
			  {
			    pt_value_set_monetary (this_parser, val, str, txt, PT_CURRENCY_SWEDISH_KRONA);
			  }

			$$ = val;

		DBG_PRINT}}
	| CROATIAN_KUNA_SIGN of_integer_real_literal
		{{ DBG_TRACE_GRAMMAR(monetary_literal, | CROATIAN_KUNA_SIGN of_integer_real_literal);

			char *str, *txt;
			PT_NODE *val = parser_new_node (this_parser, PT_VALUE);

			str = pt_append_string (this_parser, NULL, CROATIAN_KUNA_TEXT);
			txt = $2;

			if (val)
			  {
			    pt_value_set_monetary (this_parser, val, str, txt, PT_CURRENCY_CROATIAN_KUNA);
			  }

			$$ = val;

		DBG_PRINT}}
	| SERBIAN_DINAR_SIGN of_integer_real_literal
		{{ DBG_TRACE_GRAMMAR(monetary_literal, | SERBIAN_DINAR_SIGN of_integer_real_literal);

			char *str, *txt;
			PT_NODE *val = parser_new_node (this_parser, PT_VALUE);

			str = pt_append_string (this_parser, NULL, SERBIAN_DINAR_TEXT);
			txt = $2;

			if (val)
			  {
			    pt_value_set_monetary (this_parser, val, str, txt, PT_CURRENCY_SERBIAN_DINAR);
			  }

			$$ = val;

		DBG_PRINT}}
	;

of_integer_real_literal
	: integer_text
		{{ DBG_TRACE_GRAMMAR(of_integer_real_literal, : integer_text);

			$$ = $1;

		DBG_PRINT}}
	| opt_plus UNSIGNED_REAL
		{{ DBG_TRACE_GRAMMAR(of_integer_real_literal, | opt_plus UNSIGNED_REAL);

			$$ = $2;

		DBG_PRINT}}
	| '-' UNSIGNED_REAL
		{{ DBG_TRACE_GRAMMAR(of_integer_real_literal, | '-' UNSIGNED_REAL);

			$$ = pt_append_string (this_parser, (char *) "-", $2);

		DBG_PRINT}}
	;

date_or_time_literal
	: Date CHAR_STRING
		{{ DBG_TRACE_GRAMMAR(date_or_time_literal, : Date CHAR_STRING);

			PT_NODE *val;
			val = pt_create_date_value (this_parser, PT_TYPE_DATE, $2);
			$$ = val;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| Time CHAR_STRING
		{{ DBG_TRACE_GRAMMAR(date_or_time_literal, | Time CHAR_STRING);

			PT_NODE *val;
			val = pt_create_date_value (this_parser, PT_TYPE_TIME, $2);
			$$ = val;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| TIMESTAMP CHAR_STRING
		{{ DBG_TRACE_GRAMMAR(date_or_time_literal, | TIMESTAMP CHAR_STRING);

			PT_NODE *val;
			val = pt_create_date_value (this_parser, PT_TYPE_TIMESTAMP, $2);
			$$ = val;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| TIMESTAMP WITH Time ZONE CHAR_STRING
		{{ DBG_TRACE_GRAMMAR(date_or_time_literal, | TIMESTAMP WITH Time ZONE CHAR_STRING);

			PT_NODE *val;
			val = pt_create_date_value (this_parser, PT_TYPE_TIMESTAMPTZ, $5);
			$$ = val;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| TIMESTAMPTZ CHAR_STRING
		{{ DBG_TRACE_GRAMMAR(date_or_time_literal, | TIMESTAMPTZ CHAR_STRING);

			PT_NODE *val;
			val = pt_create_date_value (this_parser, PT_TYPE_TIMESTAMPTZ, $2);
			$$ = val;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| TIMESTAMPLTZ CHAR_STRING
		{{ DBG_TRACE_GRAMMAR(date_or_time_literal, | TIMESTAMPLTZ CHAR_STRING);

			PT_NODE *val;
			val = pt_create_date_value (this_parser, PT_TYPE_TIMESTAMPLTZ, $2);
			$$ = val;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| TIMESTAMP WITH LOCAL Time ZONE CHAR_STRING
		{{ DBG_TRACE_GRAMMAR(date_or_time_literal, | TIMESTAMP WITH LOCAL Time ZONE CHAR_STRING);

			PT_NODE *val;
			val = pt_create_date_value (this_parser, PT_TYPE_TIMESTAMPLTZ, $6);
			$$ = val;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| DATETIME CHAR_STRING
		{{ DBG_TRACE_GRAMMAR(date_or_time_literal, | DATETIME CHAR_STRING);

			PT_NODE *val;
			val = pt_create_date_value (this_parser, PT_TYPE_DATETIME, $2);
			$$ = val;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| DATETIME WITH Time ZONE CHAR_STRING
		{{ DBG_TRACE_GRAMMAR(date_or_time_literal, | DATETIME WITH Time ZONE CHAR_STRING);

			PT_NODE *val;
			val = pt_create_date_value (this_parser, PT_TYPE_DATETIMETZ, $5);
			$$ = val;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| DATETIMETZ CHAR_STRING
		{{ DBG_TRACE_GRAMMAR(date_or_time_literal, | DATETIMETZ CHAR_STRING);

			PT_NODE *val;
			val = pt_create_date_value (this_parser, PT_TYPE_DATETIMETZ, $2);
			$$ = val;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| DATETIMELTZ CHAR_STRING
		{{ DBG_TRACE_GRAMMAR(date_or_time_literal, | DATETIMELTZ CHAR_STRING);

			PT_NODE *val;
			val = pt_create_date_value (this_parser, PT_TYPE_DATETIMELTZ, $2);
			$$ = val;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| DATETIME WITH LOCAL Time ZONE CHAR_STRING
		{{ DBG_TRACE_GRAMMAR(date_or_time_literal, | DATETIME WITH LOCAL Time ZONE CHAR_STRING);

			PT_NODE *val;
			val = pt_create_date_value (this_parser, PT_TYPE_DATETIMELTZ, $6);
			$$ = val;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

json_literal
        : JSON CHAR_STRING
                {{ DBG_TRACE_GRAMMAR(json_literal,  : JSON CHAR_STRING);
                	PT_NODE *val;
			val = pt_create_json_value (this_parser, $2);
			$$ = val;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

                DBG_PRINT}}

create_as_clause
	: opt_replace AS csql_query
		{{ DBG_TRACE_GRAMMAR(create_as_clause, : opt_replace AS csql_query);
			container_2 ctn;
			SET_CONTAINER_2(ctn, FROM_NUMBER ($1), $3);
			$$ = ctn;
		DBG_PRINT}}
	;

partition_clause
	: PARTITION opt_by HASH '(' expression_ ')' PARTITIONS literal_w_o_param
		{{ DBG_TRACE_GRAMMAR(partition_clause, : PARTITION opt_by HASH '(' expression_ ')' PARTITIONS literal_w_o_param);

			PT_NODE *qc = parser_new_node (this_parser, PT_PARTITION);
			if (qc)
			  {
			    qc->info.partition.expr = $5;
			    qc->info.partition.type = PT_PARTITION_HASH;
			    qc->info.partition.hashsize = $8;
			  }

			$$ = qc;

		DBG_PRINT}}
	| PARTITION opt_by RANGE_ '(' expression_ ')' '(' partition_def_list ')'
		{{ DBG_TRACE_GRAMMAR(partition_clause, | PARTITION opt_by RANGE_ '(' expression_ ')' '(' partition_def_list ')');

			PT_NODE *qc = parser_new_node (this_parser, PT_PARTITION);
			if (qc)
			  {
			    qc->info.partition.expr = $5;
			    qc->info.partition.type = PT_PARTITION_RANGE;
			    qc->info.partition.parts = $8;
			    qc->info.partition.hashsize = NULL;
			  }

			$$ = qc;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| PARTITION opt_by LIST '(' expression_ ')' '(' partition_def_list ')'
		{{ DBG_TRACE_GRAMMAR(partition_clause, | PARTITION opt_by LIST '(' expression_ ')' '(' partition_def_list ')');

			PT_NODE *qc = parser_new_node (this_parser, PT_PARTITION);
			if (qc)
			  {
			    qc->info.partition.expr = $5;
			    qc->info.partition.type = PT_PARTITION_LIST;
			    qc->info.partition.parts = $8;
			    qc->info.partition.hashsize = NULL;
			  }

			$$ = qc;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

opt_by
	: /* empty */
	| BY
	;

partition_def_list
	: partition_def_list ',' partition_def
		{{ DBG_TRACE_GRAMMAR(partition_def_list, : partition_def_list ',' partition_def);

			$$ = parser_make_link ($1, $3);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| partition_def
		{{ DBG_TRACE_GRAMMAR(partition_def_list, | partition_def);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

partition_def
	: PARTITION identifier VALUES LESS THAN MAXVALUE opt_comment_spec
		{{ DBG_TRACE_GRAMMAR(partition_def, : PARTITION identifier VALUES LESS THAN MAXVALUE opt_comment_spec);

			PT_NODE *node = parser_new_node (this_parser, PT_PARTS);
			if (node)
			  {
			    node->info.parts.name = $2;
			    node->info.parts.type = PT_PARTITION_RANGE;
			    node->info.parts.values = NULL;
			    node->info.parts.comment = $7;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| PARTITION identifier VALUES LESS THAN '(' signed_literal_ ')' opt_comment_spec
		{{ DBG_TRACE_GRAMMAR(partition_def, | PARTITION identifier VALUES LESS THAN '(' signed_literal_ ')' opt_comment_spec);

			PT_NODE *node = parser_new_node (this_parser, PT_PARTS);
			if (node)
			  {
			    node->info.parts.name = $2;
			    node->info.parts.type = PT_PARTITION_RANGE;
			    node->info.parts.values = $7;
			    node->info.parts.comment = $9;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| PARTITION identifier VALUES IN_ '(' signed_literal_list ')' opt_comment_spec
		{{ DBG_TRACE_GRAMMAR(partition_def, | PARTITION identifier VALUES IN_ '(' signed_literal_list ')' opt_comment_spec);

			PT_NODE *node = parser_new_node (this_parser, PT_PARTS);
			if (node)
			  {
			    node->info.parts.name = $2;
			    node->info.parts.type = PT_PARTITION_LIST;
			    node->info.parts.values = $6;
			    node->info.parts.comment = $8;
			  }

			$$ = node;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

alter_partition_clause_for_alter_list
	: partition_clause
		{{ DBG_TRACE_GRAMMAR(alter_partition_clause_for_alter_list, : partition_clause);

			PT_NODE *alt = parser_get_alter_node ();

			if (alt)
			  {
			    alt->info.alter.code = PT_APPLY_PARTITION;
			    alt->info.alter.alter_clause.partition.info = $1;
			  }

		DBG_PRINT}}
	| REMOVE PARTITIONING
		{{ DBG_TRACE_GRAMMAR(alter_partition_clause_for_alter_list, | REMOVE PARTITIONING);

			PT_NODE *alt = parser_get_alter_node ();

			if (alt)
			  alt->info.alter.code = PT_REMOVE_PARTITION;

		DBG_PRINT}}
	| REORGANIZE PARTITION identifier_list INTO '(' partition_def_list ')'
		{{ DBG_TRACE_GRAMMAR(alter_partition_clause_for_alter_list, | REORGANIZE PARTITION identifier_list INTO '(' partition_def_list ')');

			PT_NODE *alt = parser_get_alter_node ();

			if (alt)
			  {
			    alt->info.alter.code = PT_REORG_PARTITION;
			    alt->info.alter.alter_clause.partition.name_list = $3;
			    alt->info.alter.alter_clause.partition.parts = $6;
			  }

		DBG_PRINT}}
	| ANALYZE PARTITION opt_all
		{{ DBG_TRACE_GRAMMAR(alter_partition_clause_for_alter_list, | ANALYZE PARTITION opt_all);

			PT_NODE *alt = parser_get_alter_node ();

			if (alt)
			  {
			    alt->info.alter.code = PT_ANALYZE_PARTITION;
			    alt->info.alter.alter_clause.partition.name_list = NULL;
			  }

		DBG_PRINT}}
	| ANALYZE PARTITION identifier_list
		{{ DBG_TRACE_GRAMMAR(alter_partition_clause_for_alter_list, | ANALYZE PARTITION identifier_list);

			PT_NODE *alt = parser_get_alter_node ();

			if (alt)
			  {
			    alt->info.alter.code = PT_ANALYZE_PARTITION;
			    alt->info.alter.alter_clause.partition.name_list = $3;
			  }

		DBG_PRINT}}
	| COALESCE PARTITION literal_w_o_param
		{{ DBG_TRACE_GRAMMAR(alter_partition_clause_for_alter_list, | COALESCE PARTITION literal_w_o_param);

			PT_NODE *alt = parser_get_alter_node ();

			if (alt)
			  {
			    alt->info.alter.code = PT_COALESCE_PARTITION;
			    alt->info.alter.alter_clause.partition.size = $3;
			  }

		DBG_PRINT}}
	 | PROMOTE PARTITION identifier_list
		{{ DBG_TRACE_GRAMMAR(alter_partition_clause_for_alter_list, | PROMOTE PARTITION identifier_list);

			PT_NODE *alt = parser_get_alter_node ();

			if (alt)
			  {
			    alt->info.alter.code = PT_PROMOTE_PARTITION;
			    alt->info.alter.alter_clause.partition.name_list = $3;
			  }

		DBG_PRINT}}
	;

opt_all
	: /* empty */
	| ALL
	;

execute_using_list
	: execute_using_list ',' session_variable_expression
		{{ DBG_TRACE_GRAMMAR(execute_using_list, : execute_using_list ',' session_variable_expression);

			$$ = parser_make_link ($1, $3);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| execute_using_list ',' signed_literal_
		{{ DBG_TRACE_GRAMMAR(execute_using_list, | execute_using_list ',' signed_literal_);

			$$ = parser_make_link ($1, $3);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| session_variable_expression
		{{ DBG_TRACE_GRAMMAR(execute_using_list, | session_variable_expression);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| signed_literal_
		{{ DBG_TRACE_GRAMMAR(execute_using_list, | signed_literal_);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

signed_literal_list
	: signed_literal_list ',' signed_literal_
		{{ DBG_TRACE_GRAMMAR(signed_literal_list, : signed_literal_list ',' signed_literal_);

			$$ = parser_make_link ($1, $3);
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	| signed_literal_
		{{ DBG_TRACE_GRAMMAR(signed_literal_list, | signed_literal_);

			$$ = $1;
			PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

		DBG_PRINT}}
	;

paren_plus
	: '(' '+' ')'
	;

paren_minus
	: '(' '-' ')'
	;


bad_tokens_for_error_message_only_dont_mind_this_rule
	: '@'
	| ']'
	| '`'
	/*| '^'
	| '&'
	| '~'*/
	;

vacuum_stmt
	: VACUUM
		{{ DBG_TRACE_GRAMMAR(vacuum_stmt, : VACUUM);
			PT_NODE *node =
			  parser_new_node (this_parser, PT_VACUUM);
			if (node == NULL)
			  {
			    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
				    ER_OUT_OF_VIRTUAL_MEMORY, 1,
				    sizeof (PT_NODE));
			  }
			$$ = node;
		DBG_PRINT}}
	;

json_table_column_behavior_rule
    : Null
      {{ DBG_TRACE_GRAMMAR(json_table_column_behavior_rule, : Null);
        $$.m_behavior = JSON_TABLE_RETURN_NULL;
        $$.m_default_value = NULL;
      DBG_PRINT}}
    | ERROR_
      {{ DBG_TRACE_GRAMMAR(json_table_column_behavior_rule, | ERROR_);
        $$.m_behavior = JSON_TABLE_THROW_ERROR;
        $$.m_default_value = NULL;
      DBG_PRINT}}
    | DEFAULT expression_
      {{ DBG_TRACE_GRAMMAR(json_table_column_behavior_rule, | DEFAULT expression_);
        PT_NODE * default_value = $2;
        if (default_value->node_type != PT_VALUE)
          {
            PT_ERROR (this_parser, default_value, "invalid JSON_TABLE default");
          }
        DB_VALUE * temp = pt_value_to_db (this_parser, default_value);
        $$.m_behavior = JSON_TABLE_DEFAULT_VALUE;
        $$.m_default_value = db_value_copy (temp);

        parser_free_node(this_parser, default_value);
      DBG_PRINT}}
    ;

json_table_on_error_rule_optional
    : /* empty */
      {{ DBG_TRACE_GRAMMAR(json_table_on_error_rule_optional, : );
        $$.m_behavior = JSON_TABLE_RETURN_NULL;
        $$.m_default_value = NULL;
      DBG_PRINT}}
    | json_table_column_behavior_rule ON_ ERROR_
      {{ DBG_TRACE_GRAMMAR(json_table_on_error_rule_optional, | json_table_column_behavior_rule ON_ ERROR_);
        $$ = $1;
      DBG_PRINT}}
    ;

json_table_on_empty_rule_optional
    : /* empty */
      {{ DBG_TRACE_GRAMMAR(json_table_on_empty_rule_optional, : );
        $$.m_behavior = JSON_TABLE_RETURN_NULL;
        $$.m_default_value = NULL;
      DBG_PRINT}}
    | json_table_column_behavior_rule ON_ EMPTY
      {{ DBG_TRACE_GRAMMAR(json_table_on_empty_rule_optional, | json_table_column_behavior_rule ON_ EMPTY );
        $$ = $1;
      DBG_PRINT}}
    ;

json_table_column_rule
    : identifier For ORDINALITY
      {{ DBG_TRACE_GRAMMAR(json_table_column_rule, : identifier For ORDINALITY );
        PT_NODE *pt_col = parser_new_node (this_parser, PT_JSON_TABLE_COLUMN);
        pt_col->info.json_table_column_info.name = $1;
        pt_col->info.json_table_column_info.func = JSON_TABLE_ORDINALITY;
        pt_col->type_enum = PT_TYPE_INTEGER;
        $$ = pt_col;
      DBG_PRINT}}
    | identifier data_type PATH CHAR_STRING json_table_on_empty_rule_optional json_table_on_error_rule_optional
    //        $1        $2   $3          $4                                $5                                $6
      {{ DBG_TRACE_GRAMMAR(json_table_column_rule, | identifier data_type PATH CHAR_STRING json_table_on_empty_rule_optional json_table_on_error_rule_optional );
        PT_NODE *pt_col = parser_new_node (this_parser, PT_JSON_TABLE_COLUMN);
        pt_col->info.json_table_column_info.name = $1;
        pt_col->type_enum = TO_NUMBER (CONTAINER_AT_0 ($2));
        pt_col->data_type = CONTAINER_AT_1 ($2);
        pt_col->info.json_table_column_info.path=$4;
        pt_col->info.json_table_column_info.func = JSON_TABLE_EXTRACT;
        pt_col->info.json_table_column_info.on_empty = $5;
        pt_col->info.json_table_column_info.on_error = $6;
        $$ = pt_col;
      DBG_PRINT}}
    | identifier data_type EXISTS PATH CHAR_STRING
      {{ DBG_TRACE_GRAMMAR(json_table_column_rule, | identifier data_type EXISTS PATH CHAR_STRING );
        PT_NODE *pt_col = parser_new_node (this_parser, PT_JSON_TABLE_COLUMN);
        pt_col->info.json_table_column_info.name = $1;
        pt_col->type_enum = TO_NUMBER (CONTAINER_AT_0 ($2));
        pt_col->data_type = CONTAINER_AT_1 ($2);
        pt_col->info.json_table_column_info.path=$5;
        pt_col->info.json_table_column_info.func = JSON_TABLE_EXISTS;
        $$ = pt_col;
      DBG_PRINT}}
    | NESTED json_table_node_rule
      {{ DBG_TRACE_GRAMMAR(json_table_column_rule, | NESTED json_table_node_rule );
        $$ = $2;
      DBG_PRINT}}
    | NESTED PATH json_table_node_rule
      {{ DBG_TRACE_GRAMMAR(json_table_column_rule, | NESTED PATH json_table_node_rule );
        $$ = $3;
      DBG_PRINT}}
    ;

json_table_column_list_rule
    : json_table_column_list_rule ',' json_table_column_rule
      {{ DBG_TRACE_GRAMMAR(json_table_column_list_rule,  : json_table_column_list_rule ',' json_table_column_rule );
        pt_jt_append_column_or_nested_node ($1, $3);
        $$ = $1;
      DBG_PRINT}}
    | json_table_column_rule
      {{ DBG_TRACE_GRAMMAR(json_table_column_list_rule, | json_table_column_rule );
        PT_NODE *pt_jt_node = parser_new_node (this_parser, PT_JSON_TABLE_NODE);
        pt_jt_append_column_or_nested_node (pt_jt_node, $1);
        $$ = pt_jt_node;
      DBG_PRINT}}
    ;

json_table_node_rule
    : CHAR_STRING COLUMNS '(' json_table_column_list_rule ')'
      {{ DBG_TRACE_GRAMMAR(json_table_node_rule,  : CHAR_STRING COLUMNS '(' json_table_column_list_rule ')' );
        PT_NODE *jt_node = $4;
        assert (jt_node != NULL);
        assert (jt_node->node_type == PT_JSON_TABLE_NODE);

        jt_node->info.json_table_node_info.path = $1;

        $$ = jt_node;
      DBG_PRINT}}
    ;

json_table_rule
    : {{ DBG_TRACE_GRAMMAR(json_table_rule,  : );
            json_table_column_count = 0;
      DBG_PRINT}}
      '(' expression_ ',' json_table_node_rule ')'
      {{ DBG_TRACE_GRAMMAR(json_table_rule,  '(' expression_ ',' json_table_node_rule ')' );
        PT_NODE *jt = parser_new_node (this_parser, PT_JSON_TABLE);
        jt->info.json_table_info.expr = $3;
        jt->info.json_table_info.tree = $5;
        json_table_column_count = 0;  // reset for next json table, if I am nested

        $$ = jt;
      DBG_PRINT}}
    ;

connect_info
        : connect_info ',' connect_item
          {{ DBG_TRACE_GRAMMAR(connect_info, : connect_info ',' connect_item );
               container_10 ctn = $1;

               pt_fill_conn_info_container(this_parser, @$.buffer_pos, &ctn, $3);
	        $$ = ctn;
           DBG_PRINT }}
        | connect_item
          {{ DBG_TRACE_GRAMMAR(connect_info, | connect_item );
                container_10 ctn;
                memset(&ctn, 0x00, sizeof(container_10));

                pt_fill_conn_info_container(this_parser, @$.buffer_pos, &ctn, $1);    
	        $$ = ctn;
           DBG_PRINT}}
        ;

connect_item    
        :  HOST '=' CHAR_STRING 
          {{ DBG_TRACE_GRAMMAR(connect_item,  :  HOST '=' CHAR_STRING  );
                container_2 ctn;
                PT_NODE *val = parser_new_node (this_parser, PT_VALUE);
                if (val)
                  {
                     val->info.value.data_value.str =
                               pt_append_bytes (this_parser, NULL, $3, strlen ($3));
                     val->type_enum = PT_TYPE_CHAR;
                     if (!pt_check_ipv4($3) && !pt_check_hostname($3))
                     { 
                        PT_ERROR (this_parser, val, "Incorrect hostname format");
                     }
                     PT_NODE_PRINT_VALUE_TO_TEXT (this_parser, val);
                  }
                SET_CONTAINER_2(ctn, FROM_NUMBER(CONN_INFO_HOST), val);
                $$ = ctn;
            DBG_PRINT}}
        | PORT '=' UNSIGNED_INTEGER 
          {{ DBG_TRACE_GRAMMAR(connect_item, | PORT '=' UNSIGNED_INTEGER );
                container_2 ctn;
                PT_NODE *val = parser_new_node (this_parser, PT_VALUE);
                if (val)
                  {
                        val->info.value.data_value.i = atoi($3);
                        val->type_enum = PT_TYPE_INTEGER;
                        if( val->info.value.data_value.i < 0 || val->info.value.data_value.i > 65535 )
                          {                                
                            PT_ERROR (this_parser, val, "Invalid PORT number.");
                          }
                        PT_NODE_PRINT_VALUE_TO_TEXT (this_parser, val);
                  }

                SET_CONTAINER_2(ctn, FROM_NUMBER(CONN_INFO_PORT), val);
                $$ = ctn;
           DBG_PRINT}}
        | DBNAME '=' identifier 
          {{ DBG_TRACE_GRAMMAR(connect_item,  | DBNAME '=' identifier  );
                container_2 ctn;
                SET_CONTAINER_2(ctn, FROM_NUMBER(CONN_INFO_DBNAME), $3);
                $$ = ctn;
           DBG_PRINT}}
        | USER '=' identifier
          {{ DBG_TRACE_GRAMMAR(connect_item, | USER '=' identifier);
                container_2 ctn;
                SET_CONTAINER_2(ctn, FROM_NUMBER(CONN_INFO_USER), $3);
                $$ = ctn;
            DBG_PRINT}}
        | PASSWORD '=' 
          {{ DBG_TRACE_GRAMMAR(connect_item,   | PASSWORD '='  );
               container_2 ctn;
                PT_NODE *val = parser_new_node (this_parser, PT_VALUE);
	        if (val)                    
		  {
                        char    cipher[512];

                        val->type_enum = PT_TYPE_CHAR;
                        val->info.value.string_type = ' ';

                        if (pt_check_dblink_password(this_parser, NULL, cipher, sizeof(cipher)) == NO_ERROR)
                          {
                             val->info.value.data_value.str =
                                pt_append_bytes (this_parser, NULL, cipher, strlen (cipher));
                          }
                        PT_NODE_PRINT_VALUE_TO_TEXT (this_parser, val);
		   }

                SET_CONTAINER_2(ctn, FROM_NUMBER(CONN_INFO_PASSWORD), val);
                 $$ = ctn;
           DBG_PRINT}}
        | PASSWORD '=' CHAR_STRING
          {{ DBG_TRACE_GRAMMAR(connect_item, | PASSWORD '=' CHAR_STRING );
                container_2 ctn;
                PT_NODE *val = parser_new_node (this_parser, PT_VALUE);
	        if (val)                    
		  {
                        char    cipher[512];

                        val->type_enum = PT_TYPE_CHAR;
                        val->info.value.string_type = ' ';

                        if (pt_check_dblink_password(this_parser, $3, cipher, sizeof(cipher)) == NO_ERROR)
                          {                             
                             val->info.value.data_value.str =
                                pt_append_bytes (this_parser, NULL, cipher, strlen (cipher));
                          }
                        PT_NODE_PRINT_VALUE_TO_TEXT (this_parser, val);
		   }

                SET_CONTAINER_2(ctn, FROM_NUMBER(CONN_INFO_PASSWORD), val);
                $$ = ctn;
           DBG_PRINT}}
        | PROPERTIES '=' 
          {{ DBG_TRACE_GRAMMAR(connect_item, | PROPERTIES '=' );
                container_2 ctn;
                SET_CONTAINER_2(ctn, FROM_NUMBER(CONN_INFO_PROPERTIES), NULL);
                $$ = ctn;
           DBG_PRINT}}
        | PROPERTIES '=' CHAR_STRING 
          {{ DBG_TRACE_GRAMMAR(connect_item,  | PROPERTIES '=' CHAR_STRING );
                container_2 ctn;
                PT_NODE *val = parser_new_node (this_parser, PT_VALUE);
	        if (val)                    
		  {
                        if( $3 && $3[0] &&  $3[0] != '?' )
                        {                           
                           PT_ERROR (this_parser, val, "Invalid properties of connection information for dblink");
                        }
                        val->type_enum = PT_TYPE_CHAR;
                        val->info.value.string_type = ' ';
                        val->info.value.data_value.str =
                                pt_append_bytes (this_parser, NULL, $3, strlen ($3));
                        
                        PT_NODE_PRINT_VALUE_TO_TEXT (this_parser, val);
		   }

                SET_CONTAINER_2(ctn, FROM_NUMBER(CONN_INFO_PROPERTIES), val);
                $$ = ctn;
           DBG_PRINT}}
        | COMMENT '='
          {{ DBG_TRACE_GRAMMAR(connect_item,| COMMENT '=');
                container_2 ctn;
                SET_CONTAINER_2(ctn, FROM_NUMBER(CONN_INFO_COMMENT), NULL);
                $$ = ctn;
           DBG_PRINT }}
        | COMMENT '=' char_string
          {{ DBG_TRACE_GRAMMAR(connect_item, | COMMENT '=' char_string );
                container_2 ctn;
                SET_CONTAINER_2(ctn, FROM_NUMBER(CONN_INFO_COMMENT), $3);
                $$ = ctn;
           DBG_PRINT}}
        ;

alter_server_list
        : alter_server_list ',' alter_server_item
          {{ DBG_TRACE_GRAMMAR(alter_server_list, : alter_server_list ',' alter_server_item);
               container_10 ctn = $1;

               pt_fill_conn_info_container(this_parser, @$.buffer_pos, &ctn, $3);
	        $$ = ctn;
           DBG_PRINT }}
        | alter_server_item
          {{ DBG_TRACE_GRAMMAR(alter_server_list, | alter_server_item);
                container_10 ctn;
                memset(&ctn, 0x00, sizeof(container_10));

                pt_fill_conn_info_container(this_parser, @$.buffer_pos, &ctn, $1);
	        $$ = ctn;
           DBG_PRINT}}
        ;

alter_server_item
        : OWNER TO identifier
          {{ DBG_TRACE_GRAMMAR(alter_server_item, : OWNER TO identifier);
                container_2 ctn;                 

                SET_CONTAINER_2(ctn, FROM_NUMBER(CONN_INFO_OWNER), $3);
                $$ = ctn;
          }}
        | CHANGE connect_item
          {{ DBG_TRACE_GRAMMAR(alter_server_item, | CHANGE connect_item); 
	        $$ = $2;
           DBG_PRINT}}
        ;

dblink_server_name
	: identifier DOT server_identifier
          {{ DBG_TRACE_GRAMMAR(dblink_server_name, : identifier DOT server_identifier);
             if($3)
               {
                  $3->next = $1;                  
               }
             else if($1)
               {
                  parser_free_node (this_parser, $1);
               }  
              $$ = $3;
              PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
          DBG_PRINT}}
        | server_identifier
          {{ DBG_TRACE_GRAMMAR(dblink_server_name, | server_identifier);
              $$ = $1;
          DBG_PRINT}}
        ;

server_identifier
        : identifier
            {{ DBG_TRACE_GRAMMAR(server_identifier, : identifier);
                if ($1)
                  {                
                     if (strchr($1->info.name.original, '.')) 
                       {                 
                          PT_ERRORm (this_parser, $1, MSGCAT_SET_PARSER_SYNTAX, MSGCAT_SYNTAX_INVALID_SERVER_NAME);
                       }
                  }
                $$ = $1;
                PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
                DBG_PRINT}}
         ;
dblink_expr
        :   dblink_conn  ','  CHAR_STRING  
            {{ DBG_TRACE_GRAMMAR(dblink_expr, : dblink_conn  ','  CHAR_STRING);
             PT_NODE *ct = parser_new_node(this_parser, PT_DBLINK_TABLE) ;           
             if(ct)
             {
                PT_NODE *val = parser_new_node (this_parser, PT_VALUE);
	        if (val)                    
		  {                  
                        char err_msg[256]; 
                        
                        if (pt_ct_check_select($3, err_msg) == false)
                        {
                             PT_ERROR (this_parser, val, err_msg);                                
                        }                        

                        val->type_enum = PT_TYPE_CHAR;
                        val->info.value.string_type = ' ';
                        val->info.value.data_value.str =
                                pt_append_bytes (this_parser, NULL, $3, strlen ($3));
                        
                        PT_NODE_PRINT_VALUE_TO_TEXT (this_parser, val);
		   }

                if( $1 )
                {
                        if( $1->node_type == PT_NAME )
                        {
                                ct->info.dblink_table.is_name = true;
                                ct->info.dblink_table.conn = $1;
                                if ($1->next)
                                  {
                                    ct->info.dblink_table.owner_name = $1->next;
                                    $1->next = NULL;
                                  }
                        }
                        else // ( $1->node_type == PT_VALUE )
                        {
                                ct->info.dblink_table.is_name = false;
                                // in the order url, user, password
                                ct->info.dblink_table.conn = 0x00;
                                ct->info.dblink_table.url = $1;
                                ct->info.dblink_table.user = $1->next;
                                ct->info.dblink_table.pwd = $1->next->next;
                                $1->next->next = 0x00;
                                $1->next = 0x00;
                        }
                }
                ct->info.dblink_table.qstr = val;
             }

             $$ = ct;   
             DBG_PRINT}}    
        ;

dblink_conn:
        dblink_server_name      
        {{ DBG_TRACE_GRAMMAR(dblink_conn, : dblink_server_name); 
                $$ = $1;                
                DBG_PRINT}}
        | dblink_conn_str
        {{ DBG_TRACE_GRAMMAR(dblink_conn, | dblink_conn_str);
                $$ = $1;
		DBG_PRINT}}
        ;   

dblink_conn_str:
        CHAR_STRING
        {{ DBG_TRACE_GRAMMAR(dblink_conn_str, : CHAR_STRING);
                char *zInfo[DBLINK_CONN_PARAM_CNT];     
                char err_msg[512]; 
                PT_NODE *node_list = NULL;

                memset(zInfo, 0x00, sizeof(zInfo));
                if ( pt_ct_check_fill_connection_info($1, zInfo, err_msg) == false )
                 {
                       node_list = parser_new_node (this_parser, PT_VALUE);
                       PT_ERROR (this_parser, node_list, err_msg);
                       if (node_list)
                         {
                                 parser_free_node (this_parser, node_list);
                                 node_list = NULL;
                         }
                 }
                 else
                 {
                        PT_NODE *node;
                        int  i;
                        char dblink_url[4096];
                        static const char* dblink_url_fmt = "cci:CUBRID:%s:%s:%s:::%s" ;
                        // cci:CUBRID:<host>:<port>:<db_name>:<db_user>:<db_password>:[?<properties>]

                        sprintf(dblink_url, dblink_url_fmt, 
                                zInfo[0], zInfo[1],zInfo[2], zInfo[DBLINK_CONN_PARAM_CNT-1]);
                        zInfo[2] = dblink_url;

                        for( i = DBLINK_CONN_PARAM_CNT - 2; i >= 2; i--)
                        {                       
                                node = parser_new_node (this_parser, PT_VALUE);
                                if( node == NULL )
                                {
                                        while (node_list)
                                        {
                                                node = node_list;
                                                node_list = node->next;
                                                parser_free_node (this_parser, node);
                                        }                            

                                        break;
                                }
                                                        
                                node->type_enum = PT_TYPE_CHAR;
                                node->info.value.string_type = ' ';
                                node->info.value.data_value.str =
                                        pt_append_bytes (this_parser, NULL, zInfo[i], strlen (zInfo[i]));
                                PT_NODE_PRINT_VALUE_TO_TEXT (this_parser, node);

                                node->next = node_list;
                                node_list = node;
                        } 
                 }

                $$ = node_list;
		DBG_PRINT}}
        ;             

dblink_identifier_col_attrs  
        :  opt_as identifier '('  dblink_column_definition_list ')' 
        {{ DBG_TRACE_GRAMMAR(dblink_identifier_col_attrs,  :  opt_as identifier '('  dblink_column_definition_list ')' );
             container_2 ctn;
             
	     SET_CONTAINER_2 (ctn, $2, $4);
             $$ = ctn;              
             DBG_PRINT}}
        ;

dblink_column_definition_list
        :  dblink_column_definition_list ','  dblink_column_definition
           {{ DBG_TRACE_GRAMMAR(dblink_column_definition_list, :  dblink_column_definition_list ','  dblink_column_definition );  
                $$ = parser_make_link($1, $3);
	        PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos);
                DBG_PRINT}}
        | dblink_column_definition
           {{ DBG_TRACE_GRAMMAR(dblink_column_definition_list, | dblink_column_definition );
               $$ = $1;
               PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)
             DBG_PRINT}}
        ;

dblink_column_definition
        : identifier primitive_type
        {{ DBG_TRACE_GRAMMAR(dblink_column_definition,  : identifier primitive_type );
                PT_NODE *node;              

                node = parser_new_node (this_parser, PT_ATTR_DEF);
                if (node)                            
                {                    
                        PT_NODE *dt;
                        PT_TYPE_ENUM typ;

                        node->type_enum = typ = TO_NUMBER (CONTAINER_AT_0 ($2));
                        node->data_type = dt = CONTAINER_AT_1 ($2);
                        node->info.attr_def.attr_name = $1;

                        if(typ == PT_TYPE_BLOB || typ == PT_TYPE_CLOB || typ == PT_TYPE_OBJECT || typ == PT_TYPE_ENUMERATION)
                          {
                                PT_ERRORmf (this_parser, node, MSGCAT_SET_PARSER_SEMANTIC,
					     MSGCAT_SEMANTIC_DBLINK_NOT_SUPPORTED_TYPE, pt_show_type_enum (typ));
                          }

                        if (typ == PT_TYPE_CHAR && dt)
                        {
                                node->info.attr_def.size_constraint = dt->info.data_type.precision;
                        }
                }

                $$ = node;
		PARSER_SAVE_ERR_CONTEXT ($$, @$.buffer_pos)

        DBG_PRINT}}
        ;

%%


extern FILE *yyin;

void
_push_msg (int code, int line)
{
  PRINT_2 ("push msg called: %d at line %d\n", code, line);
  g_msg[msg_ptr++] = code;
}

void
pop_msg ()
{
  msg_ptr--;
}


extern void csql_yyset_lineno (int line_number);
int yycolumn = 0;
int yycolumn_end = 0;

int parser_function_code = PT_EMPTY;
size_t json_table_column_count = 0;

static PT_NODE *
parser_make_expr_with_func (PARSER_CONTEXT * parser, FUNC_TYPE func_code,
			    PT_NODE * args_list)
{
  PT_NODE *node = NULL;
  PT_NODE *node_function = parser_new_node (parser, PT_FUNCTION);

  if (node_function != NULL)
    {
      node_function->info.function.function_type = func_code;
      node_function->info.function.arg_list = args_list;

      node =
	parser_make_expression (parser, PT_FUNCTION_HOLDER, node_function, NULL,
				NULL);
    }

  return node;
}

static PT_NODE *
parser_make_func_with_arg_count (PARSER_CONTEXT * parser, FUNC_TYPE func_code, PT_NODE * args_list,
                                 size_t min_args, size_t max_args)
{
  size_t count = (size_t) parser_count_list (args_list);
  if (min_args > count || (max_args != 0 && max_args < count))
    {
      // todo - a more clear message
      PT_ERRORmf (parser, args_list, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_INVALID_INTERNAL_FUNCTION,
                  fcode_get_lowercase_name (func_code));
      // todo - return null?
    }
  return parser_make_expr_with_func (parser, func_code, args_list);
}

static PT_NODE *
parser_make_func_with_arg_count_mod2 (PARSER_CONTEXT * parser, FUNC_TYPE func_code, PT_NODE * args_list,
                                      size_t min_args, size_t max_args, size_t mod2)
{
  size_t count = (size_t) parser_count_list (args_list);
  assert (mod2 == 0 || mod2 == 1);
  if (min_args > count || (max_args != 0 && max_args < count) || (count % 2 != mod2))
    {
      // todo - a more clear message
      PT_ERRORmf (parser, args_list, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_INVALID_INTERNAL_FUNCTION,
                  fcode_get_lowercase_name (func_code));
      // todo - return null?
    }
  return parser_make_expr_with_func (parser, func_code, args_list);
}

PT_NODE *
parser_make_expression (PARSER_CONTEXT * parser, PT_OP_TYPE OP, PT_NODE * arg1, PT_NODE * arg2,
			PT_NODE * arg3)
{
  PT_NODE *expr;
  expr = parser_new_node (parser, PT_EXPR);
  if (expr)
    {
      expr->info.expr.op = OP;
      if (pt_is_operator_logical (expr->info.expr.op))
	{
	  expr->type_enum = PT_TYPE_LOGICAL;
	}

      expr->info.expr.arg1 = arg1;
      expr->info.expr.arg2 = arg2;
      expr->info.expr.arg3 = arg3;

      if (parser_instnum_check == 1 && !pt_instnum_compatibility (expr))
	{
	  PT_ERRORmf2 (parser, expr, MSGCAT_SET_PARSER_SEMANTIC,
		       MSGCAT_SEMANTIC_INSTNUM_COMPATIBILITY_ERR,
		       "INST_NUM() or ROWNUM", "INST_NUM() or ROWNUM");
	}

      if (parser_groupbynum_check == 1 && !pt_groupbynum_compatibility (expr))
	{
	  PT_ERRORmf2 (parser, expr, MSGCAT_SET_PARSER_SEMANTIC,
		       MSGCAT_SEMANTIC_INSTNUM_COMPATIBILITY_ERR,
		       "GROUPBY_NUM()", "GROUPBY_NUM()");
	}

      if (parser_orderbynum_check == 1 && !pt_orderbynum_compatibility (expr))
	{
	  PT_ERRORmf2 (parser, expr, MSGCAT_SET_PARSER_SEMANTIC,
		       MSGCAT_SEMANTIC_INSTNUM_COMPATIBILITY_ERR,
		       "ORDERBY_NUM()", "ORDERBY_NUM()");
	}

      if (PT_IS_SERIAL(OP))
	{
	  parser_cannot_cache = true;
	}

      if (OP == PT_SYS_TIME || OP == PT_CURRENT_TIME || OP == PT_SYS_DATE
	  || OP == PT_CURRENT_DATE || OP == PT_SYS_DATETIME
	  || OP == PT_CURRENT_DATETIME || OP == PT_SYS_TIMESTAMP
	  || OP == PT_CURRENT_TIMESTAMP || OP == PT_UTC_TIME
	  || OP == PT_UTC_DATE || OP == PT_UNIX_TIMESTAMP
	  || OP == PT_TZ_OFFSET || OP == PT_UTC_TIMESTAMP)
	{
	  parser_si_datetime = true;
	  parser_cannot_cache = true;
	}
    }

  return expr;
}

static PT_NODE *
parser_make_link (PT_NODE * list, PT_NODE * node)
{
  parser_append_node (node, list);
  return list;
}

static PT_NODE *
parser_make_link_or (PT_NODE * list, PT_NODE * node)
{
  parser_append_node_or (node, list);
  return list;
}

static bool parser_cannot_cache_stack_default[STACK_SIZE];
static bool *parser_cannot_cache_stack = parser_cannot_cache_stack_default;
static int parser_cannot_cache_sp = 0;
static int parser_cannot_cache_limit = STACK_SIZE;

static void
parser_save_and_set_cannot_cache (bool value)
{
  if (parser_cannot_cache_sp >= parser_cannot_cache_limit)
    {
      size_t new_size = parser_cannot_cache_limit * 2 * sizeof (bool);
      bool *new_p = malloc (new_size);
      if (new_p == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_OUT_OF_VIRTUAL_MEMORY, 1, new_size);
	  return;
	}

      memcpy (new_p, parser_cannot_cache_stack, parser_cannot_cache_limit * sizeof (bool));
      if (parser_cannot_cache_stack != parser_cannot_cache_stack_default)
	free (parser_cannot_cache_stack);

      parser_cannot_cache_stack = new_p;
      parser_cannot_cache_limit *= 2;
    }

  assert (parser_cannot_cache_sp >= 0);
  parser_cannot_cache_stack[parser_cannot_cache_sp++] = parser_cannot_cache;
  parser_cannot_cache = value;
}

static void
parser_restore_cannot_cache ()
{
  assert (parser_cannot_cache_sp >= 1);
  parser_cannot_cache = parser_cannot_cache_stack[--parser_cannot_cache_sp];
}

static int parser_si_datetime_saved;

static void
parser_save_and_set_si_datetime (int value)
{
  parser_si_datetime_saved = parser_si_datetime;
  parser_si_datetime = value;
}

static void
parser_restore_si_datetime ()
{
  parser_si_datetime = parser_si_datetime_saved;
}

static int parser_si_tran_id_saved;

static void
parser_save_and_set_si_tran_id (int value)
{
  parser_si_tran_id_saved = parser_si_tran_id;
  parser_si_tran_id = value;
}

static void
parser_restore_si_tran_id ()
{
  parser_si_tran_id = parser_si_tran_id_saved;
}

static int parser_cannot_prepare_saved;

static void
parser_save_and_set_cannot_prepare (bool value)
{
  parser_cannot_prepare_saved = parser_cannot_prepare;
  parser_cannot_prepare = value;
}

static void
parser_restore_cannot_prepare ()
{
  parser_cannot_prepare = parser_cannot_prepare_saved;
}

static int parser_wjc_stack_default[STACK_SIZE];
static int *parser_wjc_stack = parser_wjc_stack_default;
static int parser_wjc_sp = 0;
static int parser_wjc_limit = STACK_SIZE;

static void
parser_save_and_set_wjc (int value)
{
  if (parser_wjc_sp >= parser_wjc_limit)
    {
      size_t new_size = parser_wjc_limit * 2 * sizeof (int);
      int *new_p = malloc (new_size);
      if (new_p == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_OUT_OF_VIRTUAL_MEMORY, 1, new_size);
	  return;
	}

      memcpy (new_p, parser_wjc_stack, parser_wjc_limit * sizeof (int));
      if (parser_wjc_stack != parser_wjc_stack_default)
	free (parser_wjc_stack);

      parser_wjc_stack = new_p;
      parser_wjc_limit *= 2;
    }

  assert (parser_wjc_sp >= 0);
  parser_wjc_stack[parser_wjc_sp++] = parser_within_join_condition;
  parser_within_join_condition = value;
}

static void
parser_restore_wjc ()
{
  assert (parser_wjc_sp >= 1);
  parser_within_join_condition = parser_wjc_stack[--parser_wjc_sp];
}

static int parser_instnum_stack_default[STACK_SIZE];
static int *parser_instnum_stack = parser_instnum_stack_default;
static int parser_instnum_sp = 0;
static int parser_instnum_limit = STACK_SIZE;

static void
parser_save_and_set_ic (int value)
{
  if (parser_instnum_sp >= parser_instnum_limit)
    {
      size_t new_size = parser_instnum_limit * 2 * sizeof (int);
      int *new_p = malloc (new_size);
      if (new_p == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_OUT_OF_VIRTUAL_MEMORY, 1, new_size);
	  return;
	}

      memcpy (new_p, parser_instnum_stack, parser_instnum_limit * sizeof (int));
      if (parser_instnum_stack != parser_instnum_stack_default)
	free (parser_instnum_stack);

      parser_instnum_stack = new_p;
      parser_instnum_limit *= 2;
    }

  assert (parser_instnum_sp >= 0);
  parser_instnum_stack[parser_instnum_sp++] = parser_instnum_check;
  parser_instnum_check = value;
}

static void
parser_restore_ic ()
{
  assert (parser_instnum_sp >= 1);
  parser_instnum_check = parser_instnum_stack[--parser_instnum_sp];
}

static int parser_groupbynum_stack_default[STACK_SIZE];
static int *parser_groupbynum_stack = parser_groupbynum_stack_default;
static int parser_groupbynum_sp = 0;
static int parser_groupbynum_limit = STACK_SIZE;

static void
parser_save_and_set_gc (int value)
{
  if (parser_groupbynum_sp >= parser_groupbynum_limit)
    {
      size_t new_size = parser_groupbynum_limit * 2 * sizeof (int);
      int *new_p = malloc (new_size);
      if (new_p == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_OUT_OF_VIRTUAL_MEMORY, 1, new_size);
	  return;
	}

      memcpy (new_p, parser_groupbynum_stack, parser_groupbynum_limit * sizeof (int));
      if (parser_groupbynum_stack != parser_groupbynum_stack_default)
	free (parser_groupbynum_stack);

      parser_groupbynum_stack = new_p;
      parser_groupbynum_limit *= 2;
    }

  assert (parser_groupbynum_sp >= 0);
  parser_groupbynum_stack[parser_groupbynum_sp++] = parser_groupbynum_check;
  parser_groupbynum_check = value;
}

static void
parser_restore_gc ()
{
  assert (parser_groupbynum_sp >= 1);
  parser_groupbynum_check = parser_groupbynum_stack[--parser_groupbynum_sp];
}

static int parser_orderbynum_stack_default[STACK_SIZE];
static int *parser_orderbynum_stack = parser_orderbynum_stack_default;
static int parser_orderbynum_sp = 0;
static int parser_orderbynum_limit = STACK_SIZE;

static void
parser_save_and_set_oc (int value)
{
  if (parser_orderbynum_sp >= parser_orderbynum_limit)
    {
      size_t new_size = parser_orderbynum_limit * 2 * sizeof (int);
      int *new_p = malloc (new_size);
      if (new_p == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_OUT_OF_VIRTUAL_MEMORY, 1, new_size);
	  return;
	}

      memcpy (new_p, parser_orderbynum_stack, parser_orderbynum_limit * sizeof (int));
      if (parser_orderbynum_stack != parser_orderbynum_stack_default)
	free (parser_orderbynum_stack);

      parser_orderbynum_stack = new_p;
      parser_orderbynum_limit *= 2;
    }

  assert (parser_orderbynum_sp >= 0);
  parser_orderbynum_stack[parser_orderbynum_sp++] = parser_orderbynum_check;
  parser_orderbynum_check = value;
}

static void
parser_restore_oc ()
{
  assert (parser_orderbynum_sp >= 1);
  parser_orderbynum_check = parser_orderbynum_stack[--parser_orderbynum_sp];
}

static int parser_sysc_stack_default[STACK_SIZE];
static int *parser_sysc_stack = parser_sysc_stack_default;
static int parser_sysc_sp = 0;
static int parser_sysc_limit = STACK_SIZE;

static void
parser_save_and_set_sysc (int value)
{
  if (parser_sysc_sp >= parser_sysc_limit)
    {
      size_t new_size = parser_sysc_limit * 2 * sizeof (int);
      int *new_p = malloc (new_size);
      if (new_p == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_OUT_OF_VIRTUAL_MEMORY, 1, new_size);
	  return;
	}

      memcpy (new_p, parser_sysc_stack, parser_sysc_limit * sizeof (int));
      if (parser_sysc_stack != parser_sysc_stack_default)
	free (parser_sysc_stack);

      parser_sysc_stack = new_p;
      parser_sysc_limit *= 2;
    }

  assert (parser_sysc_sp >= 0);
  parser_sysc_stack[parser_sysc_sp++] = parser_sysconnectbypath_check;
  parser_sysconnectbypath_check = value;
}

static void
parser_restore_sysc ()
{
  assert (parser_sysc_sp >= 1);
  parser_sysconnectbypath_check = parser_sysc_stack[--parser_sysc_sp];
}

static int parser_prc_stack_default[STACK_SIZE];
static int *parser_prc_stack = parser_prc_stack_default;
static int parser_prc_sp = 0;
static int parser_prc_limit = STACK_SIZE;

static void
parser_save_and_set_prc (int value)
{
  if (parser_prc_sp >= parser_prc_limit)
    {
      size_t new_size = parser_prc_limit * 2 * sizeof (int);
      int *new_p = malloc (new_size);
      if (new_p == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_OUT_OF_VIRTUAL_MEMORY, 1, new_size);
	  return;
	}

      memcpy (new_p, parser_prc_stack, parser_prc_limit * sizeof (int));
      if (parser_prc_stack != parser_prc_stack_default)
	free (parser_prc_stack);

      parser_prc_stack = new_p;
      parser_prc_limit *= 2;
    }

  assert (parser_prc_sp >= 0);
  parser_prc_stack[parser_prc_sp++] = parser_prior_check;
  parser_prior_check = value;
}

static void
parser_restore_prc ()
{
  assert (parser_prc_sp >= 1);
  parser_prior_check = parser_prc_stack[--parser_prc_sp];
}

static int parser_cbrc_stack_default[STACK_SIZE];
static int *parser_cbrc_stack = parser_cbrc_stack_default;
static int parser_cbrc_sp = 0;
static int parser_cbrc_limit = STACK_SIZE;

static void
parser_save_and_set_cbrc (int value)
{
  if (parser_cbrc_sp >= parser_cbrc_limit)
    {
      size_t new_size = parser_cbrc_limit * 2 * sizeof (int);
      int *new_p = malloc (new_size);
      if (new_p == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_OUT_OF_VIRTUAL_MEMORY, 1, new_size);
	  return;
	}

      memcpy (new_p, parser_cbrc_stack, parser_cbrc_limit * sizeof (int));
      if (parser_cbrc_stack != parser_cbrc_stack_default)
	free (parser_cbrc_stack);

      parser_cbrc_stack = new_p;
      parser_cbrc_limit *= 2;
    }

  assert (parser_cbrc_sp >= 0);
  parser_cbrc_stack[parser_cbrc_sp++] = parser_connectbyroot_check;
  parser_connectbyroot_check = value;
}

static void
parser_restore_cbrc ()
{
  assert (parser_cbrc_sp >= 1);
  parser_connectbyroot_check = parser_cbrc_stack[--parser_cbrc_sp];
}

static int parser_serc_stack_default[STACK_SIZE];
static int *parser_serc_stack = parser_serc_stack_default;
static int parser_serc_sp = 0;
static int parser_serc_limit = STACK_SIZE;

static void
parser_save_and_set_serc (int value)
{
  if (parser_serc_sp >= parser_serc_limit)
    {
      size_t new_size = parser_serc_limit * 2 * sizeof (int);
      int *new_p = malloc (new_size);
      if (new_p == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_OUT_OF_VIRTUAL_MEMORY, 1, new_size);
	  return;
	}

      memcpy (new_p, parser_serc_stack, parser_serc_limit * sizeof (int));
      if (parser_serc_stack != parser_serc_stack_default)
	free (parser_serc_stack);

      parser_serc_stack = new_p;
      parser_serc_limit *= 2;
    }

  assert (parser_serc_sp >= 0);
  parser_serc_stack[parser_serc_sp++] = parser_serial_check;
  parser_serial_check = value;
}

static void
parser_restore_serc ()
{
  assert (parser_serc_sp >= 1);
  parser_serial_check = parser_serc_stack[--parser_serc_sp];
}

static int parser_pseudoc_stack_default[STACK_SIZE];
static int *parser_pseudoc_stack = parser_pseudoc_stack_default;
static int parser_pseudoc_sp = 0;
static int parser_pseudoc_limit = STACK_SIZE;

static void
parser_save_and_set_pseudoc (int value)
{
  if (parser_pseudoc_sp >= parser_pseudoc_limit)
    {
      size_t new_size = parser_pseudoc_limit * 2 * sizeof (int);
      int *new_p = malloc (new_size);
      if (new_p == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_OUT_OF_VIRTUAL_MEMORY, 1, new_size);
	  return;
	}

      memcpy (new_p, parser_pseudoc_stack, parser_pseudoc_limit * sizeof (int));
      if (parser_pseudoc_stack != parser_pseudoc_stack_default)
	free (parser_pseudoc_stack);

      parser_pseudoc_stack = new_p;
      parser_pseudoc_limit *= 2;
    }

  assert (parser_pseudoc_sp >= 0);
  parser_pseudoc_stack[parser_pseudoc_sp++] = parser_pseudocolumn_check;
  parser_pseudocolumn_check = value;
}

static void
parser_restore_pseudoc ()
{
  assert (parser_pseudoc_sp >= 1);
  parser_pseudocolumn_check = parser_pseudoc_stack[--parser_pseudoc_sp];
}

static int parser_sqc_stack_default[STACK_SIZE];
static int *parser_sqc_stack = parser_sqc_stack_default;
static int parser_sqc_sp = 0;
static int parser_sqc_limit = STACK_SIZE;

static void
parser_save_and_set_sqc (int value)
{
  if (parser_sqc_sp >= parser_sqc_limit)
    {
      size_t new_size = parser_sqc_limit * 2 * sizeof (int);
      int *new_p = malloc (new_size);
      if (new_p == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_OUT_OF_VIRTUAL_MEMORY, 1, new_size);
	  return;
	}

      memcpy (new_p, parser_sqc_stack, parser_sqc_limit * sizeof (int));
      if (parser_sqc_stack != parser_sqc_stack_default)
	free (parser_sqc_stack);

      parser_sqc_stack = new_p;
      parser_sqc_limit *= 2;
    }

  assert (parser_sqc_sp >= 0);
  parser_sqc_stack[parser_sqc_sp++] = parser_subquery_check;
  parser_subquery_check = value;
}

static void
parser_restore_sqc ()
{
  assert (parser_sqc_sp >= 1);
  parser_subquery_check = parser_sqc_stack[--parser_sqc_sp];
}

static int parser_hvar_stack_default[STACK_SIZE];
static int *parser_hvar_stack = parser_hvar_stack_default;
static int parser_hvar_sp = 0;
static int parser_hvar_limit = STACK_SIZE;

static void
parser_save_and_set_hvar (int value)
{
  if (parser_hvar_sp >= parser_hvar_limit)
    {
      size_t new_size = parser_hvar_limit * 2 * sizeof (int);
      int *new_p = malloc (new_size);
      if (new_p == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_OUT_OF_VIRTUAL_MEMORY, 1, new_size);
	  return;
	}

      memcpy (new_p, parser_hvar_stack, parser_hvar_limit * sizeof (int));
      if (parser_hvar_stack != parser_hvar_stack_default)
	free (parser_hvar_stack);

      parser_hvar_stack = new_p;
      parser_hvar_limit *= 2;
    }

  assert (parser_hvar_sp >= 0);
  parser_hvar_stack[parser_hvar_sp++] = parser_hostvar_check;
  parser_hostvar_check = value;
}

static void
parser_restore_hvar ()
{
  assert (parser_hvar_sp >= 1);
  parser_hostvar_check = parser_hvar_stack[--parser_hvar_sp];
}

static int parser_oracle_stack_default[STACK_SIZE];
static int *parser_oracle_stack = parser_oracle_stack_default;
static int parser_oracle_sp = 0;
static int parser_oracle_limit = STACK_SIZE;

static void
parser_save_found_Oracle_outer ()
{
  if (parser_oracle_sp >= parser_oracle_limit)
    {
      size_t new_size = parser_oracle_limit * 2 * sizeof (int);
      int *new_p = malloc (new_size);
      if (new_p == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_OUT_OF_VIRTUAL_MEMORY, 1, new_size);
	  return;
	}

      memcpy (new_p, parser_oracle_stack, parser_oracle_limit * sizeof (int));
      if (parser_oracle_stack != parser_oracle_stack_default)
	free (parser_oracle_stack);

      parser_oracle_stack = new_p;
      parser_oracle_limit *= 2;
    }

  assert (parser_oracle_sp >= 0);
  parser_oracle_stack[parser_oracle_sp++] = parser_found_Oracle_outer;
}

static void
parser_restore_found_Oracle_outer ()
{
  assert (parser_oracle_sp >= 1);
  parser_found_Oracle_outer = parser_oracle_stack[--parser_oracle_sp];
}

static PT_NODE *parser_alter_node_saved;

static void
parser_save_alter_node (PT_NODE * node)
{
  parser_alter_node_saved = node;
}

static PT_NODE *
parser_get_alter_node ()
{
  return parser_alter_node_saved;
}

static PT_NODE *parser_attr_def_one_saved;

static void
parser_save_attr_def_one (PT_NODE * node)
{
  parser_attr_def_one_saved = node;
}

static PT_NODE *
parser_get_attr_def_one ()
{
  return parser_attr_def_one_saved;
}

static PT_NODE *parser_orderby_node_stack_default[STACK_SIZE];
static PT_NODE **parser_orderby_node_stack = parser_orderby_node_stack_default;
static int parser_orderby_node_sp = 0;
static int parser_orderby_node_limit = STACK_SIZE;

static void
parser_push_orderby_node (PT_NODE * node)
{
  if (parser_orderby_node_sp >= parser_orderby_node_limit)
    {
      size_t new_size = parser_orderby_node_limit * 2 * sizeof (PT_NODE **);
      PT_NODE **new_p = malloc (new_size);
      if (new_p == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_OUT_OF_VIRTUAL_MEMORY, 1, new_size);
	  return;
	}

      memcpy (new_p, parser_orderby_node_stack, parser_orderby_node_limit * sizeof (PT_NODE *));
      if (parser_orderby_node_stack != parser_orderby_node_stack_default)
	free (parser_orderby_node_stack);

      parser_orderby_node_stack = new_p;
      parser_orderby_node_limit *= 2;
    }

  assert (parser_orderby_node_sp >= 0);
  parser_orderby_node_stack[parser_orderby_node_sp++] = node;
}

static PT_NODE *
parser_top_orderby_node ()
{
  assert (parser_orderby_node_sp >= 1);
  return parser_orderby_node_stack[parser_orderby_node_sp - 1];
}

static PT_NODE *
parser_pop_orderby_node ()
{
  assert (parser_orderby_node_sp >= 1);
  return parser_orderby_node_stack[--parser_orderby_node_sp];
}

static PT_NODE *parser_select_node_stack_default[STACK_SIZE];
static PT_NODE **parser_select_node_stack = parser_select_node_stack_default;
static int parser_select_node_sp = 0;
static int parser_select_node_limit = STACK_SIZE;

static void
parser_push_select_stmt_node (PT_NODE * node)
{
  if (parser_select_node_sp >= parser_select_node_limit)
    {
      size_t new_size = parser_select_node_limit * 2 * sizeof (PT_NODE **);
      PT_NODE **new_p = malloc (new_size);
      if (new_p == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_OUT_OF_VIRTUAL_MEMORY, 1, new_size);
	  return;
	}

      memcpy (new_p, parser_select_node_stack, parser_select_node_limit * sizeof (PT_NODE *));
      if (parser_select_node_stack != parser_select_node_stack_default)
	free (parser_select_node_stack);

      parser_select_node_stack = new_p;
      parser_select_node_limit *= 2;
    }

  assert (parser_select_node_sp >= 0);
  parser_select_node_stack[parser_select_node_sp++] = node;
}

static PT_NODE *
parser_top_select_stmt_node ()
{
  assert (parser_select_node_sp >= 1);
  return parser_select_node_stack[parser_select_node_sp - 1];
}

static PT_NODE *
parser_pop_select_stmt_node ()
{
  assert (parser_select_node_sp >= 1);
  return parser_select_node_stack[--parser_select_node_sp];
}

static bool
parser_is_select_stmt_node_empty ()
{
  return parser_select_node_sp < 1;
}

static PT_NODE *parser_hint_node_stack_default[STACK_SIZE];
static PT_NODE **parser_hint_node_stack = parser_hint_node_stack_default;
static int parser_hint_node_sp = 0;
static int parser_hint_node_limit = STACK_SIZE;

static void
parser_push_hint_node (PT_NODE * node)
{
  if (parser_hint_node_sp >= parser_hint_node_limit)
    {
      size_t new_size = parser_hint_node_limit * 2 * sizeof (PT_NODE **);
      PT_NODE **new_p = malloc (new_size);
      if (new_p == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_OUT_OF_VIRTUAL_MEMORY, 1, new_size);
	  return;
	}

      memcpy (new_p, parser_hint_node_stack, parser_hint_node_limit * sizeof (PT_NODE *));
      if (parser_hint_node_stack != parser_hint_node_stack_default)
	free (parser_hint_node_stack);

      parser_hint_node_stack = new_p;
      parser_hint_node_limit *= 2;
    }

  assert (parser_hint_node_sp >= 0);
  parser_hint_node_stack[parser_hint_node_sp++] = node;
}

static PT_NODE *
parser_top_hint_node ()
{
  assert (parser_hint_node_sp >= 1);
  return parser_hint_node_stack[parser_hint_node_sp - 1];
}

static PT_NODE *
parser_pop_hint_node ()
{
  assert (parser_hint_node_sp >= 1);
  return parser_hint_node_stack[--parser_hint_node_sp];
}

static bool
parser_is_hint_node_empty ()
{
  return parser_hint_node_sp < 1;
}

static int parser_join_type_stack_default[STACK_SIZE];
static int *parser_join_type_stack = parser_join_type_stack_default;
static int parser_join_type_sp = 0;
static int parser_join_type_limit = STACK_SIZE;

static void
parser_push_join_type (int v)
{
  if (parser_join_type_sp >= parser_join_type_limit)
    {
      size_t new_size = parser_join_type_limit * 2 * sizeof (int);
      int *new_p = malloc (new_size);
      if (new_p == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_OUT_OF_VIRTUAL_MEMORY, 1, new_size);
	  return;
	}

      memcpy (new_p, parser_join_type_stack, parser_join_type_limit * sizeof (int));
      if (parser_join_type_stack != parser_join_type_stack_default)
	free (parser_join_type_stack);

      parser_join_type_stack = new_p;
      parser_join_type_limit *= 2;
    }

  assert (parser_join_type_sp >= 0);
  parser_join_type_stack[parser_join_type_sp++] = v;
}

static int
parser_top_join_type ()
{
  assert (parser_join_type_sp >= 1);
  return parser_join_type_stack[parser_join_type_sp - 1];
}

static int
parser_pop_join_type ()
{
  assert (parser_join_type_sp >= 1);
  return parser_join_type_stack[--parser_join_type_sp];
}

static bool parser_is_reverse_saved;

static void
parser_save_is_reverse (bool v)
{
  parser_is_reverse_saved = v;
}

static bool
parser_get_is_reverse ()
{
  return parser_is_reverse_saved;
}

static int
parser_count_list (PT_NODE * list)
{
  int i = 0;
  PT_NODE *p = list;

  while (p)
    {
      p = p->next;
      i++;
    }

  return i;
}

static int
parser_count_prefix_columns (PT_NODE * list, int * arg_count)
{
  int i = 0;
  PT_NODE *p = list;

  *arg_count = 0;

  while (p)
    {
      if (p->node_type == PT_SORT_SPEC)
	{
	  if (p->info.sort_spec.expr->node_type == PT_FUNCTION)
	    {
	      PT_NODE *expr = p->info.sort_spec.expr;
	      PT_NODE *arg_list = expr->info.function.arg_list;
	      if ((arg_list != NULL) && (arg_list->next == NULL)
		  && (arg_list->node_type == PT_VALUE))
		{
		  /* it might be a prefixed column */
		  i++;
		}
	    }
	}

      /* count all arguments */
      *arg_count = (*arg_count) + 1;
      p = p->next;
    }

  return i;
}

static void
parser_initialize_parser_context (void)
{
  parser_select_node_sp = 0;
  parser_orderby_node_sp = 0;
  parser_oracle_sp = 0;
  parser_orderbynum_sp = 0;
  parser_groupbynum_sp = 0;
  parser_instnum_sp = 0;
  parser_wjc_sp = 0;
  parser_cannot_cache_sp = 0;
  parser_hint_node_sp = 0;

  parser_save_is_reverse (false);
}


static void
parser_remove_dummy_select (PT_NODE ** ent_inout)
{
  PT_NODE *ent = *ent_inout;

  if (ent
      && ent->info.spec.derived_table_type == PT_IS_SUBQUERY
      && ent->info.spec.as_attr_list == NULL /* no attr_list */ )
    {

      PT_NODE *subq, *new_ent;

      /* remove dummy select from FROM clause
       *
       * for example:
       * case 1 (simple spec):
       *              FROM (SELECT * FROM x) AS s
       * -> FROM x AS s
       * case 2 (nested spec):
       *              FROM (SELECT * FROM (SELECT a, b FROM bas) y(p, q)) x
       * -> FROM (SELECT a, b FROM bas) x(p, q)
       *
       * Note: Subqueries with CTEs are not removed
       */
      if ((subq = ent->info.spec.derived_table)
	        && subq->node_type == PT_SELECT
	        && PT_SELECT_INFO_IS_FLAGED (subq, PT_SELECT_INFO_DUMMY))
        {
          if (subq->info.query.q.select.from && subq->info.query.with == NULL)
      	   {
          	  if (PT_SELECT_INFO_IS_FLAGED (subq, PT_SELECT_INFO_FOR_UPDATE))
            	    {
            	      /* the FOR UPDATE clause cannot be used in subqueries */
            	      PT_ERRORm (this_parser, subq, MSGCAT_SET_PARSER_SEMANTIC,
          			 MSGCAT_SEMANTIC_INVALID_USE_FOR_UPDATE_CLAUSE);
          	      }
          	  else
          	      {
          	        new_ent = subq->info.query.q.select.from;
          	        subq->info.query.q.select.from = NULL;

          	        /* free, reset new_spec's range_var, as_attr_list */
          	        if (new_ent->info.spec.range_var)
              		    {
              		    parser_free_node (this_parser, new_ent->info.spec.range_var);
              		    new_ent->info.spec.range_var = NULL;
              		    }

            	      new_ent->info.spec.range_var = ent->info.spec.range_var;
            	      ent->info.spec.range_var = NULL;

            	      /* free old ent, reset to new_ent */
            	      parser_free_node (this_parser, ent);
            	      *ent_inout = new_ent;
            	    }
	          }
          else
            {
              /* not dummy */
              PT_SELECT_INFO_CLEAR_FLAG (subq, PT_SELECT_INFO_DUMMY);
            }
        }
    }
}


static PT_NODE *
parser_make_date_lang (int arg_cnt, PT_NODE * arg3)
{
  if (arg3 && arg_cnt == 3)
    {
      char *lang_str;
      PT_NODE *date_lang = parser_new_node (this_parser, PT_VALUE);

      if (date_lang)
	{
	  date_lang->type_enum = PT_TYPE_INTEGER;
	  if (arg3->type_enum != PT_TYPE_CHAR
	      && arg3->type_enum != PT_TYPE_NCHAR)
	    {
	      PT_ERROR (this_parser, arg3,
			"argument 3 must be character string");
	    }
	  else if (arg3->info.value.data_value.str != NULL)
	    {
	      int flag = 0;
	      lang_str = (char *) arg3->info.value.data_value.str->bytes;
	      if (lang_set_flag_from_lang (lang_str, 1, 1, &flag))
		{
		  PT_ERROR (this_parser, arg3, "check syntax at 'date_lang'");
		}
	      date_lang->info.value.data_value.i = (long) flag;
	    }
	}
      parser_free_node (this_parser, arg3);

      return date_lang;
    }
  else
    {
      PT_NODE *date_lang = parser_new_node (this_parser, PT_VALUE);
      if (date_lang)
	{
	  const char *lang_str;
	  int flag = 0;

	  date_lang->type_enum = PT_TYPE_INTEGER;
	  lang_str = prm_get_string_value (PRM_ID_INTL_DATE_LANG);

	  lang_set_flag_from_lang (lang_str, (arg_cnt == 1) ? 0 : 1, 0, &flag);

	  date_lang->info.value.data_value.i = (long) flag;
	}

      return date_lang;
    }
}

static PT_NODE *
parser_make_number_lang (const int argc)
{
  PT_NODE *number_lang = parser_new_node (this_parser, PT_VALUE);

  if (number_lang)
    {
      const char *lang_str;
      int flag = 0;

      number_lang->type_enum = PT_TYPE_INTEGER;
      lang_str = prm_get_string_value (PRM_ID_INTL_NUMBER_LANG);

      lang_set_flag_from_lang (lang_str, (argc >= 2) ? 1 : 0, (argc == 3) ? 1 : 0, &flag);

      number_lang->info.value.data_value.i = (long) flag;
    }

  return number_lang;
}

PT_NODE **
parser_main (PARSER_CONTEXT * parser)
{
  long desc_index = 0;
  long i, top;
  int rv;

  PARSER_CONTEXT *this_parser_saved;

  if (!parser)
    return 0;

  parser_output_host_index = parser_input_host_index = desc_index = 0;

  this_parser_saved = this_parser;

  this_parser = parser;

  dbcs_start_input ();

  yycolumn = yycolumn_end = 1;
  yybuffer_pos=0;
  csql_yylloc.buffer_pos=0;

  g_query_string = NULL;
  g_query_string_len = 0;
  g_original_buffer_len = 0;

  pt_initialize_hint(parser, parser_hint_table); 
  rv = yyparse ();
  pt_cleanup_hint (parser, parser_hint_table);

  if (pt_has_error (parser) || parser->stack_top <= 0 || !parser->node_stack)
    {
      parser->statements = NULL;
    }
  else
    {
      /* create array of result statements */
      parser->statements = (PT_NODE **) parser_alloc (parser,
						      (1 +
						       parser->stack_top) *
						      sizeof (PT_NODE *));
      if (parser->statements)
	{
	  for (i = 0, top = parser->stack_top; i < top; i++)
	    {
	      parser->statements[i] = parser->node_stack[i];
	    }
	  parser->statements[top] = NULL;
	}
      /* record parser_input_host_index into parser->host_var_count for later use;
	 e.g. parser_set_host_variables(), auto-parameterized query */
      parser->host_var_count = parser_input_host_index;
      if (parser->host_var_count > 0)
	{
	  /* allocate place holder for host variables */
	  parser->host_variables = (DB_VALUE *)
	      malloc (parser->host_var_count * sizeof (DB_VALUE));
	  if (parser->host_variables)
	    {
	      memset (parser->host_variables, 0,
    			  parser->host_var_count * sizeof (DB_VALUE));
	    }
	  else
	    {
	      parser->statements = NULL;
	      goto end;
	    }

	  parser->host_var_expected_domains = (TP_DOMAIN **)
	      malloc (parser->host_var_count * sizeof (TP_DOMAIN *));
	  if (parser->host_var_expected_domains)
	    {
	      for (i = 0; i < parser->host_var_count; i++)
		{
		  parser->host_var_expected_domains[i] =
		      tp_domain_resolve_default (DB_TYPE_UNKNOWN);
		}
	    }
	  else
	    {
	      free_and_init (parser->host_variables);
	      parser->statements = NULL;
	    }
	  for (i = 0; i < parser->host_var_count; i++)
	    {
	      db_make_null (&parser->host_variables[i]);
	    }
	}
    }

end:
  this_parser = this_parser_saved;
  return parser->statements;
}





extern int parser_yyinput_single_mode;
int
parse_one_statement (int state)
{
  int rv;

  if (state == 0)
    {
      // a new session starts. reset line and column number.
      csql_yyset_lineno (1);
      yycolumn = yycolumn_end = 1;

      return 0;
    }

  this_parser->statement_number = 0;

  parser_yyinput_single_mode = 1;

  yybuffer_pos=0;
  csql_yylloc.buffer_pos=0;

  g_query_string = NULL;
  g_query_string_len = 0;
  g_original_buffer_len = 0;

  pt_initialize_hint(this_parser, parser_hint_table);
  rv = yyparse ();
  pt_cleanup_hint (this_parser, parser_hint_table);

  if (parser_statement_OK)
    this_parser->statement_number = 1;
  else
    parser_statement_OK = 1;

  if (!parser_yyinput_single_mode)	/* eof */
    return 1;

  return 0;
}

/* NOTICE:
  parser_hint_table.tokens must be written in uppercase.
  It must start with an English capital letter.
*/
PT_HINT parser_hint_table[] = {
  {"ORDERED", NULL, PT_HINT_ORDERED}
  ,
  {"NO_INDEX_SS", NULL, PT_HINT_NO_INDEX_SS}
  ,
  {"INDEX_SS", NULL, PT_HINT_INDEX_SS}
  ,
  {"USE_NL", NULL, PT_HINT_USE_NL}
  ,
  {"USE_IDX", NULL, PT_HINT_USE_IDX}
  ,
  {"USE_MERGE", NULL, PT_HINT_USE_MERGE}
  ,
  {"RECOMPILE", NULL, PT_HINT_RECOMPILE}
  ,
  {"LOCK_TIMEOUT", NULL, PT_HINT_LK_TIMEOUT}
  ,
  {"NO_LOGGING", NULL, PT_HINT_NO_LOGGING}
  ,
  {"QUERY_CACHE", NULL, PT_HINT_QUERY_CACHE}
  ,
  {"SQL_CACHE", NULL, PT_HINT_QUERY_CACHE}
    ,
  {"QUERY_NO_CACHE", NULL, PT_HINT_QUERY_NO_CACHE}
  ,
  {"SQL_NO_CACHE", NULL, PT_HINT_QUERY_NO_CACHE}
  ,
  {"REEXECUTE", NULL, PT_HINT_REEXECUTE}
  ,
  {"JDBC_CACHE", NULL, PT_HINT_JDBC_CACHE}
  ,
  {"USE_DESC_IDX", NULL, PT_HINT_USE_IDX_DESC}
  ,
  {"NO_COVERING_IDX", NULL, PT_HINT_NO_COVERING_IDX}
  ,
  {"INSERT_EXECUTION_MODE", NULL, PT_HINT_INSERT_MODE}
  ,
  {"NO_DESC_IDX", NULL, PT_HINT_NO_IDX_DESC}
  ,
  {"NO_MULTI_RANGE_OPT", NULL, PT_HINT_NO_MULTI_RANGE_OPT}
  ,
  {"USE_UPDATE_IDX", NULL, PT_HINT_USE_UPDATE_IDX}
  ,
  {"USE_INSERT_IDX", NULL, PT_HINT_USE_INSERT_IDX}
  ,
  {"NO_SORT_LIMIT", NULL, PT_HINT_NO_SORT_LIMIT}
  ,
  {"NO_HASH_AGGREGATE", NULL, PT_HINT_NO_HASH_AGGREGATE}
  ,
  {"NO_HASH_LIST_SCAN", NULL, PT_HINT_NO_HASH_LIST_SCAN}
  ,
  {"NO_PUSH_PRED", NULL, PT_HINT_NO_PUSH_PRED}
  ,
  {"NO_MERGE", NULL, PT_HINT_NO_MERGE}
  ,
  {"SKIP_UPDATE_NULL", NULL, PT_HINT_SKIP_UPDATE_NULL}
  ,
  {"NO_INDEX_LS", NULL, PT_HINT_NO_INDEX_LS}
  ,
  {"INDEX_LS", NULL, PT_HINT_INDEX_LS}
  ,
  {"SELECT_RECORD_INFO", NULL, PT_HINT_SELECT_RECORD_INFO}
  ,
  {"SELECT_PAGE_INFO", NULL, PT_HINT_SELECT_PAGE_INFO}
  ,
  {"SELECT_KEY_INFO", NULL, PT_HINT_SELECT_KEY_INFO}
  ,
  {"SELECT_BTREE_NODE_INFO", NULL, PT_HINT_SELECT_BTREE_NODE_INFO}
  ,
  {"USE_SBR", NULL, PT_HINT_USE_SBR}
  ,
  {"NO_SUPPLEMENTAL_LOG", NULL, PT_HINT_NO_SUPPLEMENTAL_LOG}
  ,
  {NULL, NULL, -1}		/* mark as end */
};



PT_NODE *
parser_keyword_func (const char *name, PT_NODE * args)
{
  PT_NODE *node;
  PT_NODE *top_node;
  PT_NODE *a1, *a2, *a3;
  FUNCTION_MAP *key;
  int c;
  PT_NODE *val, *between_ge_lt, *between;


  parser_function_code = PT_EMPTY;
  c = parser_count_list (args);
  key = pt_find_function_name (name);
  if (key == NULL)
    return NULL;

  parser_function_code = key->op;

  a1 = a2 = a3 = NULL;
  switch (key->op)
    {
      /* arg 0 */
    case PT_PI:
    case PT_SYS_TIME:
    case PT_SYS_DATE:
    case PT_SYS_DATETIME:
    case PT_SYS_TIMESTAMP:
    case PT_CURRENT_TIME:
    case PT_CURRENT_DATE:
    case PT_CURRENT_DATETIME:
    case PT_CURRENT_TIMESTAMP:
    case PT_UTC_TIME:
    case PT_UTC_DATE:
    case PT_VERSION:
    case PT_UTC_TIMESTAMP:
      if (c != 0)
	{
	  return NULL;
	}
      return parser_make_expression (this_parser, key->op, NULL, NULL, NULL);

    case PT_ROW_COUNT:
    case PT_LAST_INSERT_ID:
    case PT_TRACE_STATS:
      if (c != 0)
	{
	  return NULL;
	}
      parser_cannot_cache = true;
      parser_cannot_prepare = true;
      return parser_make_expression (this_parser, key->op, NULL, NULL, NULL);

    case PT_SYS_GUID:
      {
        PT_NODE *expr;
        if (c != 0)
          {
            return NULL;
          }
        parser_cannot_cache = true;

        expr = parser_make_expression (this_parser, key->op, NULL, NULL, NULL);
        expr->flag.do_not_fold = 1;

        return expr;
      }

      /* arg 0 or 1 */
    case PT_RAND:
    case PT_RANDOM:
    case PT_DRAND:
    case PT_DRANDOM:
      {
	PT_NODE *expr;
	parser_cannot_cache = true;

	if (c < 0 || c > 1)
	  {
	    return NULL;
	  }

	if (c == 1)
	  {
	    a1 = args;
	  }
	expr = parser_make_expression (this_parser, key->op, a1, NULL, NULL);
	expr->flag.do_not_fold = 1;
	return expr;
      }

      /* arg 1 */
    case PT_ABS:
    case PT_BIN:
    case PT_CEIL:
    case PT_CHAR_LENGTH:	/* char_length, length, lengthb */
    case PT_EXP:
    case PT_FLOOR:
    case PT_LAST_DAY:
    case PT_SIGN:
    case PT_SQRT:
    case PT_ACOS:
    case PT_ASIN:
    case PT_COS:
    case PT_SIN:
    case PT_TAN:
    case PT_COT:
    case PT_DEGREES:
    case PT_RADIANS:
    case PT_LN:
    case PT_LOG2:
    case PT_LOG10:
    case PT_TYPEOF:
    case PT_MD5:
    case PT_SPACE:
    case PT_DAYOFMONTH:
    case PT_WEEKDAY:
    case PT_DAYOFWEEK:
    case PT_DAYOFYEAR:
    case PT_TODAYS:
    case PT_FROMDAYS:
    case PT_TIMETOSEC:
    case PT_SECTOTIME:
    case PT_QUARTERF:
    case PT_EXEC_STATS:
    case PT_CURRENT_VALUE:
    case PT_HEX:
    case PT_ASCII:
    case PT_INET_ATON:
    case PT_INET_NTOA:
    case PT_COERCIBILITY:
    case PT_SHA_ONE:
    case PT_TO_BASE64:
    case PT_FROM_BASE64:
    case PT_CRC32:
    case PT_SCHEMA_DEF:
    case PT_DISK_SIZE:
      {
	PT_NODE *expr;
	if (c != 1)
	  {
	    return NULL;
	  }
	a1 = args;
	if (key->op == PT_COERCIBILITY && a1)
	  {
	    a1->flag.do_not_fold = 1;
	  }

	expr = parser_make_expression (this_parser, key->op, a1, NULL, NULL);
	if (key->op == PT_SCHEMA_DEF && a1)
	  {
	    if (!PT_IS_VALUE_NODE (a1)
		|| (a1->type_enum != PT_TYPE_NULL
		    && !PT_IS_STRING_TYPE(a1->type_enum)))
	      {
		PT_ERRORf (this_parser, a1, "%s argument must be "
		    "a string literal", pt_short_print (this_parser, expr));
		return NULL;
	      }
	  }

	return expr;
      }

    case PT_UNIX_TIMESTAMP:
      if (c > 1)
	{
	  return NULL;
	}
      if (c == 1)
	{
	  a1 = args;
	  return parser_make_expression (this_parser, key->op, a1, NULL, NULL);
	}
      else /* no arguments */
	{
	  return parser_make_expression (this_parser, key->op, NULL, NULL, NULL);
	}

      /* arg 2 */
    case PT_DATE_FORMAT:
    case PT_TIME_FORMAT:
    case PT_FORMAT:
    case PT_STR_TO_DATE:
      if (c != 2)
	return NULL;
      a1 = args;
      a2 = a1->next;
      a1->next = NULL;
      return parser_make_expression (this_parser, key->op, a1, a2, parser_make_date_lang (2, NULL));

    case PT_LOG:
    case PT_MONTHS_BETWEEN:
    case PT_NVL:
    case PT_POWER:
    case PT_ATAN2:
    case PT_DATEDIFF:
    case PT_TIMEDIFF:
    case PT_REPEAT:
    case PT_MAKEDATE:
    case PT_ADDTIME:
    case PT_FINDINSET:
    case PT_AES_ENCRYPT:
    case PT_AES_DECRYPT:
    case PT_SHA_TWO:
    case PT_FROM_TZ:
      if (c != 2)
	return NULL;
      a1 = args;
      a2 = a1->next;
      a1->next = NULL;
      return parser_make_expression (this_parser, key->op, a1, a2, NULL);

    case PT_NEXT_VALUE:
      if (c == 1)
	{
	  a1 = args;

	  /* default value for the second argument is 1. */
	  a2 = parser_new_node (this_parser, PT_VALUE);
	  if (a2)
	    {
	      a2->type_enum = PT_TYPE_INTEGER;
	      a2->info.value.data_value.i = 1;
	    }

	  return parser_make_expression (this_parser, key->op, a1, a2, NULL);
	}
      else if (c == 2)
	{
	  a1 = args;
	  a2 = a1->next;
	  a1->next = NULL;

	  return parser_make_expression (this_parser, key->op, a1, a2, NULL);
	}
      else
	{
	  return NULL;
	}

    case PT_ATAN:
      if (c == 1)
	{
	  a1 = args;
	  return parser_make_expression (this_parser, key->op, a1, NULL, NULL);
	}
      else if (c == 2)
	{
	  a1 = args;
	  a2 = a1->next;
	  a1->next = NULL;
	  return parser_make_expression (this_parser, PT_ATAN2, a1, a2, NULL);
	}
      else
	{
	  return NULL;
	}

      /* arg 3 */
    case PT_SUBSTRING_INDEX:
    case PT_NVL2:
    case PT_MAKETIME:
    case PT_INDEX_CARDINALITY:
    case PT_CONV:
    case PT_NEW_TIME:
      if (c != 3)
	return NULL;
      a1 = args;
      a2 = a1->next;
      a3 = a2->next;
      a1->next = a2->next = NULL;
      return parser_make_expression (this_parser, key->op, a1, a2, a3);

      /* arg 4 */
    case PT_WIDTH_BUCKET:
      if (c != 4)
        {
          return NULL;
        }

      a1 = args;
      a2 = a1->next;  /* a2 is the lower bound, a2->next is the upper bound */
      a3 = a2->next->next;
      a1->next = a2->next->next = a3->next = NULL;

      val = parser_new_node (this_parser, PT_VALUE);
      if (val == NULL)
        {
          return NULL;
        }
      val->type_enum = PT_TYPE_NULL;
      val->flag.is_added_by_parser = 1;

      between_ge_lt = parser_make_expression (this_parser, PT_BETWEEN_GE_LT, a2, a2->next, NULL);
      if (between_ge_lt == NULL)
        {
          return NULL;
        }
      a2->next = NULL;

      between = parser_make_expression (this_parser, PT_BETWEEN, val, between_ge_lt, NULL);
      if (between == NULL)
        {
          return NULL;
        }

      between->flag.do_not_fold = 1;

      return parser_make_expression (this_parser, key->op, a1, between, a3);

      /* arg 1 + default */
    case PT_ROUND:
      if (c == 1)
    {
      a1 = args;
      a2 = parser_new_node (this_parser, PT_VALUE);
      if (a2)
        {
          /* default fmt value */
          if(a1->node_type == PT_VALUE
             && PT_IS_DATE_TIME_TYPE(a1->type_enum))
            {
              a2->type_enum = PT_TYPE_CHAR;
              a2->info.value.data_value.str =
                pt_append_bytes(this_parser, NULL, "dd", 2);
            }
          else if(a1->node_type == PT_VALUE
                  && PT_IS_NUMERIC_TYPE(a1->type_enum))
            {
              a2->type_enum = PT_TYPE_INTEGER;
              a2->info.value.data_value.i = 0;
            }
          else
            {
              a2->type_enum = PT_TYPE_CHAR;
              a2->info.value.data_value.str =
                pt_append_bytes(this_parser, NULL, "default", 7);
            }
        }

      return parser_make_expression (this_parser, key->op, a1, a2, NULL);
    }

      if (c != 2)
    return NULL;
      a1 = args;
      a2 = a1->next;
      a1->next = NULL;

      return parser_make_expression (this_parser, key->op, a1, a2, NULL);

    case PT_TRUNC:
      if (c == 1)
    {
      a1 = args;
      a2 = parser_new_node (this_parser, PT_VALUE);
      if (a2)
	    {
	      /* default fmt value */
	      if (a1->node_type == PT_VALUE
	          && PT_IS_NUMERIC_TYPE (a1->type_enum))
	        {
	          a2->type_enum = PT_TYPE_INTEGER;
	          a2->info.value.data_value.i = 0;
	        }
	      else
	        {
	          a2->type_enum = PT_TYPE_CHAR;
	          a2->info.value.data_value.str =
	            pt_append_bytes (this_parser, NULL, "default", 7);
	        }
	    }

	  return parser_make_expression (this_parser, key->op, a1, a2, NULL);
	}

      if (c != 2)
	return NULL;
      a1 = args;
      a2 = a1->next;
      a1->next = NULL;

      return parser_make_expression (this_parser, key->op, a1, a2, NULL);

      /* arg 2 + default */
    case PT_INSTR:		/* instr, instrb */
      if (c == 2)
	{
	  a3 = parser_new_node (this_parser, PT_VALUE);
	  if (a3)
	    {
	      a3->type_enum = PT_TYPE_INTEGER;
	      a3->info.value.data_value.i = 1;
	    }

	  a1 = args;
	  a2 = a1->next;
	  a1->next = NULL;

	  return parser_make_expression (this_parser, key->op, a1, a2, a3);
	}

      if (c != 3)
	return NULL;
      a1 = args;
      a2 = a1->next;
      a3 = a2->next;
      a1->next = a2->next = NULL;
      return parser_make_expression (this_parser, key->op, a1, a2, a3);

      /* arg 1 or 2 */
    case PT_WEEKF:
      if (c < 1 || c > 2)
   	return NULL;
      if (c == 1)
	{
	  a1 = args;
	  a2 = parser_new_node (this_parser, PT_VALUE);
	  if (a2)
	   {
	     a2->info.value.data_value.i =
	      prm_get_integer_value (PRM_ID_DEFAULT_WEEK_FORMAT);
	     a2->type_enum = PT_TYPE_INTEGER;
	   }
	  return parser_make_expression (this_parser, key->op, a1, a2, NULL);
	}
      else
	{
	  a1 = args;
	  a2 = a1->next;
	  a1->next = NULL;
	  return parser_make_expression (this_parser, key->op, a1, a2, NULL);
	}

    case PT_LTRIM:
    case PT_RTRIM:
    case PT_LIKE_LOWER_BOUND:
    case PT_LIKE_UPPER_BOUND:
      if (c < 1 || c > 2)
	return NULL;
      a1 = args;
      if (a1)
	a2 = a1->next;
      a1->next = NULL;
      return parser_make_expression (this_parser, key->op, a1, a2, a3);

    case PT_FROM_UNIXTIME:
      if (c < 1 || c > 2)
	return NULL;
      a1 = args;
      if (a1)
	a2 = a1->next;
      a1->next = NULL;
      a3 = parser_make_date_lang (2, NULL);
      return parser_make_expression (this_parser, key->op, a1, a2, a3);

      /* arg 1 or 2 */
    case PT_TO_NUMBER:
      if (c < 1 || c > 2)
	{
	  push_msg (MSGCAT_SYNTAX_INVALID_TO_NUMBER);
	  csql_yyerror_explicit (10, 10);
	  return NULL;
	}

      if (c == 2)
	{
	  PT_NODE *node = args->next;
	  if (node->node_type != PT_VALUE ||
	      (node->type_enum != PT_TYPE_CHAR &&
	       node->type_enum != PT_TYPE_VARCHAR &&
	       node->type_enum != PT_TYPE_NCHAR &&
	       node->type_enum != PT_TYPE_VARNCHAR))
	    {
	      push_msg (MSGCAT_SYNTAX_INVALID_TO_NUMBER);
	      csql_yyerror_explicit (10, 10);
	      return NULL;
	    }
	}

      a1 = args;
      if (a1)
	a2 = a1->next;
      a1->next = NULL;
      return parser_make_expression (this_parser, key->op, a1, a2, parser_make_number_lang (c));

      /* arg 2 or 3 */
    case PT_LPAD:
    case PT_RPAD:
    case PT_SUBSTRING:		/* substr, substrb */

      if (c < 2 || c > 3)
	return NULL;

      a1 = args;
      a2 = a1->next;
      if (a2)
	{
	  a3 = a2->next;
	  a2->next = NULL;
	}

      a1->next = NULL;

      node = parser_make_expression (this_parser, key->op, a1, a2, a3);
      if (key->op == PT_SUBSTRING)
	{
	  node->info.expr.qualifier = PT_SUBSTR;
	  PICE (node);
	}

      return node;

    case PT_ORDERBY_NUM:
      if (c != 0)
	return NULL;

      node = parser_new_node (this_parser, PT_EXPR);
      if (node)
	{
	  node->info.expr.op = PT_ORDERBY_NUM;
	  PT_EXPR_INFO_SET_FLAG (node, PT_EXPR_INFO_ORDERBYNUM_C);
	}

      if (parser_orderbynum_check == 0)
	PT_ERRORmf2 (this_parser, node, MSGCAT_SET_PARSER_SEMANTIC,
		     MSGCAT_SEMANTIC_INSTNUM_COMPATIBILITY_ERR,
		     "ORDERBY_NUM()", "ORDERBY_NUM()");

      parser_groupby_exception = PT_ORDERBY_NUM;
      return node;

    case PT_INST_NUM:
      if (c != 0)
	return NULL;

      node = parser_new_node (this_parser, PT_EXPR);

      if (node)
	{
	  node->info.expr.op = PT_INST_NUM;
	  PT_EXPR_INFO_SET_FLAG (node, PT_EXPR_INFO_INSTNUM_C);
	}

      if (parser_instnum_check == 0)
	PT_ERRORmf2 (this_parser, node, MSGCAT_SET_PARSER_SEMANTIC,
		     MSGCAT_SEMANTIC_INSTNUM_COMPATIBILITY_ERR,
		     "INST_NUM() or ROWNUM", "INST_NUM() or ROWNUM");

      parser_groupby_exception = PT_INST_NUM;
      return node;

    case PT_INCR:
    case PT_DECR:
      if (c != 1)
	return NULL;
      node = parser_make_expression (this_parser, key->op, args, NULL, NULL);

      if ((args->node_type != PT_NAME && args->node_type != PT_DOT_) ||
	  (args->node_type == PT_NAME && args->info.name.tag_click_counter) ||
	  (args->node_type == PT_DOT_ && args->info.dot.tag_click_counter))
	{
	  PT_ERRORf (this_parser, node,
		     "%s argument must be identifier or dotted identifier(path expression)",
		     pt_short_print (this_parser, node));
	}

      if (parser_select_level != 1)
	{
	  PT_ERRORf (this_parser, node,
		     "%s can be used at top select statement only.",
		     pt_short_print (this_parser, node));
	}
      node->flag.is_hidden_column = 1;
      parser_hidden_incr_list =
	parser_append_node (node, parser_hidden_incr_list);
      if ((a1 = parser_copy_tree (this_parser, node->info.expr.arg1)) == NULL)
	{
	  return NULL;
	}

      parser_cannot_cache = true;

      if (a1->node_type == PT_NAME)
	a1->info.name.tag_click_counter = 1;
      else if (a1->node_type == PT_DOT_)
	a1->info.dot.tag_click_counter = 1;

      top_node = parser_is_select_stmt_node_empty () ? NULL : parser_top_select_stmt_node ();
      if (top_node)
      {
	  top_node->flag.is_click_counter = 1;
      }

      return a1;

    case PT_TO_CHAR:
    case PT_TO_DATE:
    case PT_TO_TIME:
    case PT_TO_TIMESTAMP:
    case PT_TO_DATETIME:
    case PT_TO_DATETIME_TZ:
    case PT_TO_TIMESTAMP_TZ:
      if (c < 1 || c > 3)
	return NULL;
      a1 = args;
      a2 = a1->next;
      a1->next = NULL;

      if (a2)
	{
	  a3 = a2->next;
	}

      if (a2)
	{
	  a2->next = NULL;
	}

      if (c == 1)
	{
	  a2 = parser_new_node (this_parser, PT_VALUE);

	  if (a2)
	    {
	      a2->type_enum = PT_TYPE_NULL;
	      a2->flag.is_added_by_parser = 1;
	    }
	}

      return parser_make_expression (this_parser, key->op, a1, a2,
				     parser_make_date_lang (c, a3));

    case PT_BIT_TO_BLOB:
    case PT_CHAR_TO_BLOB:
    case PT_CHAR_TO_CLOB:
    case PT_BLOB_LENGTH:
    case PT_CLOB_LENGTH:
      if (c != 1)
	{
	  return NULL;
	}

      a1 = args;
      /*
         a2 = parser_new_node (this_parser, PT_VALUE);
         if (a2)
         {
         if (key->op == PT_BIT_TO_BLOB ||
         key->op == PT_CHAR_TO_BLOB)
         {
         a2->type_enum = PT_TYPE_BLOB;
         }
         else if (key->op == PT_CHAR_TO_CLOB)
         {
         a2->type_enum = PT_TYPE_CLOB;
         }
         else if (key->op == PT_BLOB_LENGTH ||
         key->op == PT_CLOB_LENGTH)
         {
         a2->type_enum = PT_TYPE_BIGINT;
         }
         }
       */
      return parser_make_expression (this_parser, key->op, a1, NULL, NULL);

    case PT_BLOB_TO_BIT:
      if (c != 1)
	{
	  return NULL;
	}

      a1 = args;
      return parser_make_expression (this_parser, key->op, a1, NULL, NULL);

    case PT_BLOB_FROM_FILE:
    case PT_CLOB_FROM_FILE:
      if (c != 1)
	{
	  return NULL;
	}
      else
	{
	  PT_NODE *node;

	  a1 = args;
	  a2 = parser_new_node (this_parser, PT_VALUE);
	  if (a2)
	    {
	      if (key->op == PT_BLOB_FROM_FILE)
		{
		  a2->type_enum = PT_TYPE_BLOB;
		}
	      else if (key->op == PT_CLOB_FROM_FILE)
		{
		  a2->type_enum = PT_TYPE_CLOB;
		}
	    }

	  /* Those two functions should be evaluated at the compile time */
	  parser_cannot_cache = true;
	  parser_cannot_prepare = true;
	  node = parser_make_expression (this_parser, key->op, a1, a2, NULL);

	  if (a1->node_type != PT_VALUE || a1->type_enum != PT_TYPE_CHAR)
	    {
	      PT_ERRORf (this_parser, node,
			 "%s argument must be a string liternal",
			 pt_short_print (this_parser, node));
	      return NULL;
	    }
	  return node;
	}

    case PT_DECODE:
      {
	int i;
	PT_NODE *case_oper, *p, *q, *r, *nodep, *node, *curr, *prev;
	int count;

	if (c < 3)
	  return NULL;

	case_oper = args;
	p = args->next;
	args->next = NULL;
	curr = p->next;
	p->next = NULL;

	node = parser_new_node (this_parser, PT_EXPR);
	if (node)
	  {
	    node->info.expr.op = PT_DECODE;
	    q = parser_new_node (this_parser, PT_EXPR);
	    if (q)
	      {
		q->info.expr.op = PT_EQ;
		q->info.expr.qualifier = PT_EQ_TORDER;
		q->info.expr.arg1 =
		  parser_copy_tree_list (this_parser, case_oper);
		q->info.expr.arg2 = p;
		node->info.expr.arg3 = q;
		PICE (q);
	      }

	    p = curr->next;
	    curr->next = NULL;
	    node->info.expr.arg1 = curr;
	    PICE (node);
	  }

	prev = node;
	count = parser_count_list (p);
	for (i = 1; i <= count; i++)
	  {
	    if (i % 2 == 0)
	      {
		r = p->next;
		p->next = NULL;
		nodep = parser_new_node (this_parser, PT_EXPR);
		if (nodep)
		  {
		    nodep->info.expr.op = PT_DECODE;
		    q = parser_new_node (this_parser, PT_EXPR);
		    if (q)
		      {
			q->info.expr.op = PT_EQ;
			q->info.expr.qualifier = PT_EQ_TORDER;
			q->info.expr.arg1 =
			  parser_copy_tree_list (this_parser, case_oper);
			q->info.expr.arg2 = p;
			nodep->info.expr.arg3 = q;
			PICE (nodep);
		      }
		    nodep->info.expr.arg1 = r;
		    nodep->info.expr.continued_case = 1;
		  }
		PICE (nodep);

		if (prev)
		  prev->info.expr.arg2 = nodep;
		PICE (prev);
		prev = nodep;

		p = r->next;
		r->next = NULL;
	      }
	  }

	/* default value */
	if (i % 2 == 0)
	  {
	    if (prev)
	      prev->info.expr.arg2 = p;
	    PICE (prev);
	  }
	else if (prev && prev->info.expr.arg2 == NULL)
	  {
	    p = parser_new_node (this_parser, PT_VALUE);
	    if (p)
	      {
		    p->type_enum = PT_TYPE_NULL;
		    p->flag.is_added_by_parser = 1;
	      }
	    prev->info.expr.arg2 = p;
	    PICE (prev);
	  }

	if (case_oper)
	  parser_free_node (this_parser, case_oper);

	return node;
      }

    case PT_LEAST:
    case PT_GREATEST:
      {
	PT_NODE *prev, *expr, *arg, *tmp;
	int i;
	arg = args;

	if (c < 1)
	  return NULL;

	expr = parser_new_node (this_parser, PT_EXPR);
	if (expr)
	  {
	    expr->info.expr.op = key->op;
	    expr->info.expr.arg1 = arg;
	    expr->info.expr.arg2 = NULL;
	    expr->info.expr.arg3 = NULL;
	    expr->info.expr.continued_case = 1;
	  }

	PICE (expr);
	prev = expr;

	if (c > 1)
	  {
	    tmp = arg;
	    arg = arg->next;
	    tmp->next = NULL;

	    if (prev)
	      prev->info.expr.arg2 = arg;
	    PICE (prev);
	  }
	for (i = 3; i <= c; i++)
	  {
	    tmp = arg;
	    arg = arg->next;
	    tmp->next = NULL;

	    expr = parser_new_node (this_parser, PT_EXPR);
	    if (expr)
	      {
		expr->info.expr.op = key->op;
		expr->info.expr.arg1 = prev;
		expr->info.expr.arg2 = arg;
		expr->info.expr.arg3 = NULL;
		expr->info.expr.continued_case = 1;
	      }

	    if (prev && prev->info.expr.continued_case >= 1)
	      prev->info.expr.continued_case++;
	    PICE (expr);
	    prev = expr;
	  }

	if (expr && expr->info.expr.arg2 == NULL)
	  {
	    expr->info.expr.arg2 =
	      parser_copy_tree_list (this_parser, expr->info.expr.arg1);
	    expr->info.expr.arg2->flag.is_hidden_column = 1;
	  }

	return expr;
      }

    case PT_CONCAT:
    case PT_CONCAT_WS:
    case PT_FIELD:
      {
	PT_NODE *prev, *expr, *arg, *tmp, *sep, *val;
	int i, ws;
	arg = args;

	ws = (key->op != PT_CONCAT) ? 1 : 0;
	if (c < 1 + ws)
	  return NULL;

	if (key->op != PT_CONCAT)
	  {
	    sep = arg;
	    arg = arg->next;
	  }
	else
	  {
	    sep = NULL;
	  }

	expr = parser_new_node (this_parser, PT_EXPR);
	if (expr)
	  {
	    expr->info.expr.op = key->op;
	    expr->info.expr.arg1 = arg;
	    expr->info.expr.arg2 = NULL;
	    if (key->op == PT_FIELD && sep)
	      {
		val = parser_new_node (this_parser, PT_VALUE);
		if (val)
		  {
		    val->type_enum = PT_TYPE_INTEGER;
		    val->info.value.data_value.i = 1;
		    val->flag.is_hidden_column = 1;
		  }
		expr->info.expr.arg3 = parser_copy_tree (this_parser, sep);
		expr->info.expr.arg3->next = val;
	      }
	    else
	      {
		expr->info.expr.arg3 = sep;
	      }
	    expr->info.expr.continued_case = 1;
	  }

	PICE (expr);
	prev = expr;

	if (c > 1 + ws)
	  {
	    tmp = arg;
	    arg = arg->next;
	    tmp->next = NULL;

	    if (prev)
	      prev->info.expr.arg2 = arg;
	    PICE (prev);
	  }
	for (i = 3 + ws; i <= c; i++)
	  {
	    tmp = arg;
	    arg = arg->next;
	    tmp->next = NULL;

	    expr = parser_new_node (this_parser, PT_EXPR);
	    if (expr)
	      {
		expr->info.expr.op = key->op;
		expr->info.expr.arg1 = prev;
		expr->info.expr.arg2 = arg;
		if (sep)
		  {
		    expr->info.expr.arg3 =
		      parser_copy_tree (this_parser, sep);
		    if (key->op == PT_FIELD)
		      {
			val = parser_new_node (this_parser, PT_VALUE);
			if (val)
			  {
			    val->type_enum = PT_TYPE_INTEGER;
			    val->info.value.data_value.i = i - ws;
			    val->flag.is_hidden_column = 1;
			  }
			expr->info.expr.arg3->next = val;
		      }
		  }
		else
		  {
		    expr->info.expr.arg3 = NULL;
		  }
		expr->info.expr.continued_case = 1;
	      }

	    if (prev && prev->info.expr.continued_case >= 1)
	      prev->info.expr.continued_case++;
	    PICE (expr);
	    prev = expr;
	  }

	if (key->op == PT_FIELD && expr && expr->info.expr.arg2 == NULL)
	  {
	    val = parser_new_node (this_parser, PT_VALUE);
	    if (val)
	      {
		    val->type_enum = PT_TYPE_NULL;
		    val->flag.is_hidden_column = 1;
		    val->flag.is_added_by_parser = 1;
	      }
	    expr->info.expr.arg2 = val;
	  }

	return expr;
      }

    case PT_LOCATE:
      if (c < 2 || c > 3)
	return NULL;

      a1 = args;
      a2 = a1->next;
      if (a2)
	{
	  a3 = a2->next;
	  a2->next = NULL;
	}
      a1->next = NULL;

      node = parser_make_expression (this_parser, key->op, a1, a2, a3);
      return node;

    case PT_MID:
      if (c != 3)
	return NULL;

      a1 = args;
      a2 = a1->next;
      a3 = a2->next;
      a1->next = NULL;
      a2->next = NULL;
      a3->next = NULL;

      node = parser_make_expression (this_parser, key->op, a1, a2, a3);
      return node;

    case PT_STRCMP:
      if (c != 2)
	return NULL;

      a1 = args;
      a2 = a1->next;
      a1->next = NULL;

      node = parser_make_expression (this_parser, key->op, a1, a2, a3);
      return node;

    case PT_REVERSE:
    case PT_TZ_OFFSET:
    case PT_CONV_TZ:
      if (c != 1)
	return NULL;

      a1 = args;
      node = parser_make_expression (this_parser, key->op, a1, NULL, NULL);
      return node;

    case PT_BIT_COUNT:
      if (c != 1)
	return NULL;

      a1 = args;
      node = parser_make_expression (this_parser, key->op, a1, NULL, NULL);
      return node;

    case PT_GROUPBY_NUM:
      if (c != 0)
	return NULL;

      node = parser_new_node (this_parser, PT_FUNCTION);

      if (node)
	{
	  node->info.function.function_type = PT_GROUPBY_NUM;
	  node->info.function.arg_list = NULL;
	  node->info.function.all_or_distinct = PT_ALL;
	}

      if (parser_groupbynum_check == 0)
	PT_ERRORmf2 (this_parser, node, MSGCAT_SET_PARSER_SEMANTIC,
		     MSGCAT_SEMANTIC_INSTNUM_COMPATIBILITY_ERR,
		     "GROUPBY_NUM()", "GROUPBY_NUM()");
      return node;

    case PT_LIST_DBS:
      if (c != 0)
	return NULL;
      node = parser_make_expression (this_parser, key->op, NULL, NULL, NULL);
      return node;

    case PT_SLEEP:
      if (c != 1)
        {
      	  return NULL;
        }

      a1 = args;
      node = parser_make_expression (this_parser, key->op, a1, NULL, NULL);
      if (node != NULL)
        {
          node->flag.do_not_fold = 1;
        }

      return node;

    default:
      return NULL;
    }

  return NULL;
}


static void
resolve_alias_in_expr_node (PT_NODE * node, PT_NODE * list)
{
  if (!node)
    {
      return;
    }

  switch (node->node_type)
    {
    case PT_SORT_SPEC:
      if (node->info.sort_spec.expr
	  && node->info.sort_spec.expr->node_type == PT_NAME)
	{
	  resolve_alias_in_name_node (&node->info.sort_spec.expr, list);
	}
      else
	{
	  resolve_alias_in_expr_node (node->info.sort_spec.expr, list);
	}
      break;

    case PT_EXPR:
      if (node->info.expr.arg1 && node->info.expr.arg1->node_type == PT_NAME)
	{
	  resolve_alias_in_name_node (&node->info.expr.arg1, list);
	}
      else
	{
	  resolve_alias_in_expr_node (node->info.expr.arg1, list);
	}
      if (node->info.expr.arg2 && node->info.expr.arg2->node_type == PT_NAME)
	{
	  resolve_alias_in_name_node (&node->info.expr.arg2, list);
	}
      else
	{
	  resolve_alias_in_expr_node (node->info.expr.arg2, list);
	}
      if (node->info.expr.arg3 && node->info.expr.arg3->node_type == PT_NAME)
	{
	  resolve_alias_in_name_node (&node->info.expr.arg3, list);
	}
      else
	{
	  resolve_alias_in_expr_node (node->info.expr.arg3, list);
	}
      break;

    default:;
    }
}


static void
resolve_alias_in_name_node (PT_NODE ** node, PT_NODE * list)
{
  PT_NODE *col;
  char *n_str, *c_str;
  bool resolved = false;

  n_str = parser_print_tree (this_parser, *node);

  for (col = list; col; col = col->next)
    {
      if (col->node_type == PT_NAME)
	{
	  c_str = parser_print_tree (this_parser, col);
	  if (c_str == NULL)
	    {
	      continue;
	    }

	  if (intl_identifier_casecmp (n_str, c_str) == 0)
	    {
	      resolved = true;
	      break;
	    }
	}
    }

  if (resolved != true)
    {
      for (col = list; col; col = col->next)
	{
	  if (col->alias_print
	      && intl_identifier_casecmp (n_str, col->alias_print) == 0)
	    {
	      parser_free_node (this_parser, *node);
	      *node = parser_copy_tree (this_parser, col);
	      (*node)->next = NULL;
	      break;
	    }
	}
    }
}

static char *
pt_check_identifier (PARSER_CONTEXT *parser, PT_NODE *p, const char *str,
		     const int str_size)
{
  char *invalid_pos = NULL;
  int composed_size;

  if (intl_check_string ((char *) str, str_size, &invalid_pos, LANG_SYS_CODESET) == INTL_UTF8_INVALID)
    {
      PT_ERRORmf (parser, NULL, MSGCAT_SET_ERROR, -(ER_INVALID_CHAR),
		  (invalid_pos != NULL) ? invalid_pos - str : 0);
      return NULL;
    }
  else if (intl_identifier_fix ((char *) str, -1, true) != NO_ERROR)
    {
      PT_ERRORf (parser, p, "invalid identifier : %s", str);
      return NULL;
    }

  if (LANG_SYS_CODESET == INTL_CODESET_UTF8
      && unicode_string_need_compose (str, str_size, &composed_size,
				      lang_get_generic_unicode_norm ()))
    {
      char *composed = NULL;
      bool is_composed = false;

      composed = parser_allocate_string_buffer (parser, composed_size + 1,
					        sizeof (char));
      if (composed == NULL)
	{
	  PT_ERRORf (parser, p, "cannot alloc %d bytes", composed_size + 1);
	  return NULL;
	}

      unicode_compose_string (str, str_size, composed, &composed_size,
			      &is_composed, lang_get_generic_unicode_norm ());
      composed[composed_size] = '\0';
      assert (composed_size <= str_size);

      if (is_composed)
	{
	  return composed;
	}
    }

  return (char *)str;
}

static PT_NODE *
pt_create_char_string_literal (PARSER_CONTEXT *parser, const PT_TYPE_ENUM char_type,
			       const char *str, const INTL_CODESET codeset)
{
  int str_size = strlen (str);
  PT_NODE *node = NULL;
  char *invalid_pos = NULL;
  int composed_size;

  if (intl_check_string (str, str_size, &invalid_pos, codeset) != INTL_UTF8_VALID)
    {
      PT_ERRORmf (parser, NULL,
		  MSGCAT_SET_ERROR, -(ER_INVALID_CHAR),
		  (invalid_pos != NULL) ? invalid_pos - str : 0);
    }

  if (codeset == INTL_CODESET_UTF8
      && unicode_string_need_compose (str, str_size, &composed_size,
				      lang_get_generic_unicode_norm ()))
    {
      char *composed = NULL;
      bool is_composed = false;

      composed = parser_allocate_string_buffer (parser, composed_size + 1,
					        sizeof (char));

      if (composed != NULL)
	{
	  unicode_compose_string (str, str_size, composed, &composed_size,
				  &is_composed, lang_get_generic_unicode_norm ());
	  composed[composed_size] = '\0';

	  assert (composed_size <= str_size);

	  if (is_composed)
	    {
	      str = composed;
	      str_size = composed_size;
	    }
	}
      else
	{
	  str = NULL;
	  PT_ERRORf (parser, NULL, "cannot alloc %d bytes", composed_size + 1);
	}
    }

    node = parser_new_node (parser, PT_VALUE);
    if (node)
      {
        unsigned char *string;
        int length;

        node->info.value.data_value.str = pt_append_bytes (parser, NULL, str, str_size);
        string = node->info.value.data_value.str->bytes;
        length = node->info.value.data_value.str->length;

        node->type_enum = char_type;

        if (char_type == PT_TYPE_NCHAR)
          {
            node->info.value.string_type = 'N';
          }
        else
          {
            node->info.value.string_type = ' ';
          }

        PT_NODE_PRINT_VALUE_TO_TEXT (parser, node);
      }

  return node;
}

static PT_NODE *
pt_create_date_value (PARSER_CONTEXT *parser, const PT_TYPE_ENUM type,
		      const char *str)
{
  PT_NODE *node = NULL;

  node = parser_new_node (parser, PT_VALUE);

  if (node)
    {
      node->type_enum = type;

      node->info.value.data_value.str = pt_append_bytes (parser, NULL, str, strlen (str));
      PT_NODE_PRINT_VALUE_TO_TEXT (parser, node);
    }

  return node;
}

static void
pt_value_set_charset_coll (PARSER_CONTEXT *parser, PT_NODE *node,
			   const int codeset_id, const int collation_id,
			   bool force)
{
  PT_NODE *dt = NULL;

  assert (node->node_type == PT_VALUE);
  assert (PT_HAS_COLLATION (node->type_enum));
  assert (node->data_type == NULL);

  if (!force && codeset_id == LANG_SYS_CODESET
      && collation_id == LANG_SYS_COLLATION)
    {
      /* not necessary to add a new node, by default constants get the
       * coercible (system) charset and collation */
      return;
    }

  dt = parser_new_node (parser, PT_DATA_TYPE);
  if (dt)
    {
      dt->type_enum = node->type_enum;
      dt->info.data_type.precision = TP_FLOATING_PRECISION_VALUE;
      dt->info.data_type.units = codeset_id;
      dt->info.data_type.collation_id = collation_id;

      node->data_type = dt;
      if (collation_id != LANG_GET_BINARY_COLLATION (codeset_id))
	{
	  assert (collation_id != LANG_SYS_COLLATION);
	  node->info.value.print_collation = true;
	}
    }
}

static void
pt_value_set_collation_info (PARSER_CONTEXT *parser, PT_NODE *node,
			     PT_NODE *coll_node)
{
  LANG_COLLATION *lang_coll = NULL;

  if (node == NULL)
    {
      return;
    }

  assert (node->node_type == PT_VALUE);

  if (node->data_type != NULL
      && node->data_type->info.data_type.units != LANG_SYS_CODESET)
    {
      assert (node->node_type == PT_VALUE);
      assert (PT_HAS_COLLATION (node->type_enum));

      node->info.value.print_charset = true;
    }

  /* coll_node is the optional collation specified by user with COLLATE */
  if (coll_node == NULL)
    {
      int client_collation = lang_get_client_collation ();

      if (client_collation == LANG_SYS_COLLATION)
	{
	  /* no need for collation info */
	  return;
	}
      else
	{
	  lang_coll = lang_get_collation (client_collation);
	  assert (lang_coll != NULL);
	}
    }
  else
    {
      assert (coll_node->node_type == PT_VALUE);

      assert (coll_node->info.value.data_value.str != NULL);
      lang_coll = lang_get_collation_by_name ((const char *) coll_node->info.value.data_value.str->bytes);
    }

  if (lang_coll != NULL)
    {
      if (node->data_type != NULL)
	{
    	  /* check charset-collation compatibility */
    	  if (lang_coll->codeset != node->data_type->info.data_type.units)
    	    {
    	      /* set an error only when charset and collation given by user
    	         are not compatible */
    	      if (coll_node != NULL)
    		{
    		  if (!node->info.value.has_cs_introducer)
    		    {
    		      node->data_type->info.data_type.units =
    			lang_coll->codeset;
    		      node->data_type->info.data_type.collation_id =
    			lang_coll->coll.coll_id;
    		    }
    		  else
    		    {
    		      PT_ERRORm (parser, coll_node, MSGCAT_SET_PARSER_SEMANTIC,
				 MSGCAT_SEMANTIC_INCOMPATIBLE_CS_COLL);
		      return;
		    }
		}
	      else
		{
		  /* COLLATE was not specified: leave the default collation of
		     charset set by charset introducer */
		  assert (node->info.value.print_collation == false);

		  return;
		}
    	    }
    	  else
    	    {
    	      node->data_type->info.data_type.collation_id = lang_coll->coll.coll_id;

    	      if (LANG_GET_BINARY_COLLATION (node->data_type->info.data_type.units)
    		  == lang_coll->coll.coll_id)
    		{
    		  /* do not print collation */
    		  return;
    		}
    	    }
  	}
      else
	{
  	  pt_value_set_charset_coll (parser, node, lang_coll->codeset,
  				     lang_coll->coll.coll_id, false);
  	}
    }
  else
    {
      PT_ERRORmf (parser, coll_node, MSGCAT_SET_PARSER_SEMANTIC,
		  MSGCAT_SEMANTIC_UNKNOWN_COLL,
		  coll_node->info.value.data_value.str->bytes);
      return;
    }

  node->info.value.print_collation = true;
}

static void
pt_value_set_monetary (PARSER_CONTEXT *parser, PT_NODE *node,
                   const char *currency_str, const char *value, DB_CURRENCY type)
{
  double dval;

  assert (node != NULL);
  assert (node->node_type == PT_VALUE);
  assert (value != NULL);

  errno = 0;
  dval = strtod (value, NULL);
  if (errno == ERANGE)
    {
      PT_ERRORmf2 (parser, node, MSGCAT_SET_PARSER_SYNTAX,
                   MSGCAT_SYNTAX_FLT_DBL_OVERFLOW, value, pt_show_type_enum (PT_TYPE_MONETARY));
    }

  node->info.value.data_value.money.type = type;
  node->info.value.text = pt_append_string (parser, currency_str, value);
  node->type_enum = PT_TYPE_MONETARY;
  node->info.value.data_value.money.amount = dval;

  return;
}

static PT_NODE *
pt_set_collation_modifier (PARSER_CONTEXT *parser, PT_NODE *node,
			   PT_NODE *coll_node)
{
  LANG_COLLATION *lang_coll = NULL;
  bool do_wrap_with_cast = false;

  assert (coll_node != NULL);
  assert (coll_node->node_type == PT_VALUE);

  assert (coll_node->info.value.data_value.str != NULL);
  lang_coll = lang_get_collation_by_name ((const char *) coll_node->info.value.data_value.str->bytes);

  if (lang_coll == NULL)
    {
      PT_ERRORmf (parser, coll_node, MSGCAT_SET_PARSER_SEMANTIC,
		  MSGCAT_SEMANTIC_UNKNOWN_COLL, coll_node->info.value.data_value.str->bytes);
      return node;
    }

  if (node->node_type == PT_VALUE)
    {
      if (!(PT_HAS_COLLATION (node->type_enum)))
	{
	  PT_ERRORm (parser, coll_node, MSGCAT_SET_PARSER_SEMANTIC,
		     MSGCAT_SEMANTIC_COLLATE_NOT_ALLOWED);
	  return node;
	}
      PT_SET_NODE_COLL_MODIFIER (node, lang_coll->coll.coll_id);
      pt_value_set_collation_info (parser, node, coll_node);
    }
  else if (node->node_type == PT_EXPR)
    {
      if (node->info.expr.op == PT_EVALUATE_VARIABLE || node->info.expr.op == PT_DEFINE_VARIABLE)
	{
	  PT_ERRORm (parser, coll_node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_COLLATE_NOT_ALLOWED);
	  return node;
	}
      else if (node->info.expr.op == PT_CAST
	       && PT_EXPR_INFO_IS_FLAGED (node, PT_EXPR_INFO_CAST_COLL_MODIFIER))
	{
	  LANG_COLLATION *lc_node = lang_get_collation (PT_GET_COLLATION_MODIFIER (node));
	  if (lc_node->codeset != lang_coll->codeset)
	    {
	      PT_ERRORmf2 (parser, node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_CS_MATCH_COLLATE,
			   lang_get_codeset_name (lc_node->codeset),
			   lang_get_codeset_name (lang_coll->codeset));
	      return node;
	    }
	}
      PT_SET_NODE_COLL_MODIFIER (node, lang_coll->coll.coll_id);
      if (!pt_is_comp_op (node->info.expr.op))
	{
	  do_wrap_with_cast = true;
	}
    }
  else if (node->node_type == PT_NAME || node->node_type == PT_DOT_ || node->node_type == PT_FUNCTION)
    {
      PT_SET_NODE_COLL_MODIFIER (node, lang_coll->coll.coll_id);
      do_wrap_with_cast = true;
    }
  else
    {
      PT_ERRORm (parser, node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_COLLATE_NOT_ALLOWED);
      assert (do_wrap_with_cast == false);
    }

  if (do_wrap_with_cast)
    {
      PT_NODE *cast_expr = parser_make_expression (parser, PT_CAST, node, NULL, NULL);
      if (cast_expr != NULL)
	{
	  cast_expr->info.expr.cast_type = NULL;
	  PT_EXPR_INFO_SET_FLAG (cast_expr, PT_EXPR_INFO_CAST_COLL_MODIFIER);
	  node = cast_expr;
	  PT_SET_NODE_COLL_MODIFIER (cast_expr, lang_coll->coll.coll_id);
	}
    }

  return node;
}

static PT_NODE *
pt_create_json_value (PARSER_CONTEXT *parser, const char *str)
{
  PT_NODE *node = NULL;

  node = parser_new_node (parser, PT_VALUE);
  if (node)
    {
      node->type_enum = PT_TYPE_JSON;

      node->info.value.data_value.str = pt_append_bytes (parser, NULL, str, strlen (str));
      PT_NODE_PRINT_VALUE_TO_TEXT (parser, node);
    }

  return node;
}

static void
pt_jt_append_column_or_nested_node (PT_NODE * jt_node, PT_NODE * jt_col_or_nested)
{
  assert (jt_node != NULL && jt_node->node_type == PT_JSON_TABLE_NODE);
  assert (jt_col_or_nested != NULL);

  if (jt_col_or_nested->node_type == PT_JSON_TABLE_COLUMN)
    {
      jt_col_or_nested->info.json_table_column_info.index = json_table_column_count++;
      jt_node->info.json_table_node_info.columns =
        parser_append_node (jt_col_or_nested, jt_node->info.json_table_node_info.columns);
    }
  else
    {
      assert (jt_col_or_nested->node_type == PT_JSON_TABLE_NODE);
      jt_node->info.json_table_node_info.nested_paths =
        parser_append_node (jt_col_or_nested, jt_node->info.json_table_node_info.nested_paths);
    }
}

static bool 
pt_ct_check_fill_connection_info (char* p, char *pInfo[], char *perr_msg)
{   
   //  <host>:<port>:<db_name>:<db_user>:<db_password>:[?<properties>]     
   int   nCnt, i;
   char* s;
 
   perr_msg[0] = 0x00;
   if ( *p == ':' )
     {
         sprintf(perr_msg, "Incorrect format of connection information for dblink");
         return false;    
     }
     
   nCnt = 0;
   for (s = p; *p; p++ )
     {
        if (*p == '\\')
        {
           if ( p[1] == ':' || p[1] == '\\' || p[1] == '\'' ) 
             {
                p++;
             }            
        }
        else if (*p == ':')
        {
           pInfo[nCnt++] = s;
           *p = 0x00;
           s = p + 1;
           if (nCnt == (DBLINK_CONN_PARAM_CNT-1))
             {
                pInfo[nCnt] = s;                 
                break;
             }
        }
     }  

   // Check essential items
   for( i = 0; i < (DBLINK_CONN_PARAM_CNT-1); i++ ) 
     {
          if ( !pInfo[i] )
            {
                sprintf(perr_msg, "Incorrect format of connection information for dblink");
                return false;
            }
     }

   //
   if ( pt_check_ipv4(pInfo[0]) == false )
   {
          if( pt_check_hostname(pInfo[0]) == false )
          {
                sprintf(perr_msg, "Incorrect host(IP) format of connection information for dblink");
                return false;
          }
   }

   int port = atoi(pInfo[1]);
   if( port < 0 || port > 65535 )
   {
           sprintf(perr_msg, "Invalid PORT of connection information for dblink");
           return false;
   }

   if( pInfo[DBLINK_CONN_PARAM_CNT-1][0] &&  pInfo[DBLINK_CONN_PARAM_CNT-1][0] != '?' )
     {
           sprintf(perr_msg, "Invalid properties of connection information for dblink");
           return false;
     }

   return true;
}


static PT_NODE* 
pt_mk_spec_drived_dblink_table(PT_SPEC_INFO* class_spec, PT_NODE* select_col_list)
{
   // dblink_expr::= : dblink_conn  ','  CHAR_STRING);
   PT_NODE *dbl_spec = parser_new_node (this_parser, PT_SPEC);
   PT_NODE *dbl_expr = parser_new_node(this_parser, PT_DBLINK_TABLE);
   PT_NODE *val = parser_new_node (this_parser, PT_VALUE);   

   PT_NODE* server_name = class_spec->ct_server_name;
   PT_NODE* entity_name = class_spec->entity_name;
   PT_NODE* range_var = class_spec->range_var;

   //PT_NODE* table_name = class_spec->entity_name;  
   PT_NODE* dbl_col = NULL;
  
   if(!dbl_expr || !val || !dbl_spec)
   {
        if(dbl_expr)   
            parser_free_node (this_parser, dbl_expr);  

        if(val)   
            parser_free_node (this_parser, val);  

        if(dbl_spec)   
            parser_free_node (this_parser, dbl_spec);          

        return NULL;
   }

  dbl_expr->info.dblink_table.remote_table_name =  pt_append_string (this_parser, NULL, class_spec->entity_name->info.name.original);

   assert(server_name->node_type == PT_NAME);
   dbl_expr->info.dblink_table.is_name = true;
   dbl_expr->info.dblink_table.conn = server_name;
   class_spec->ct_server_name = NULL;
   if (server_name->next)
     {
        dbl_expr->info.dblink_table.owner_name = server_name->next;
        server_name->next = NULL;
     }

////////////////////////////////////////////////////////////////////////
   dbl_expr->info.dblink_table.qstr = entity_name;
   class_spec->entity_name = NULL;

   dbl_expr->info.dblink_table.qstr->next = range_var;      
   class_spec->range_var = NULL;  


   // select * from dblink_t1@remote_srv1;
   PARSER_VARCHAR *var_buf = 0;   
     
   // original_table_spec::= | DBLINK  '('  dblink_expr ')' dblink_identifier_col_attrs
   dbl_spec->info.spec.derived_table = dbl_expr; 
   dbl_spec->info.spec.derived_table_type = PT_DERIVED_DBLINK_TABLE;

   dbl_spec->info.spec.range_var = parser_new_node (this_parser, PT_NAME); // alias table_name
   if (dbl_spec->info.spec.range_var == NULL)                                           
     {            
        PT_ERROR (this_parser, dbl_spec, "Oops! Sorry, insufficient memory."); // ctshim
     }
   else
     {
////////////////
        if(range_var)
        {/* alias table name */
            var_buf = pt_print_bytes (this_parser, range_var);
        }
        else
        {
            // 유니크한 이름이어야 한다. <server_name>_<table_naem>도 대안인데 길이에 대한 고민 필요
            var_buf = pt_print_bytes (this_parser, entity_name);
        }  
        dbl_spec->info.spec.range_var->info.name.original = pt_makename ((char*)var_buf->bytes);
     }    

   dbl_spec->info.spec.derived_table->info.dblink_table.cols = NULL; 

   return dbl_spec; 
}


static void pt_get_cols_4_dblink(S_LINK_COLUMNS* plkcol, PT_NODE* select_col_list)
{  
   (void) parser_walk_tree (this_parser, select_col_list, 
                            pt_get_column_name_pre, plkcol, pt_get_column_name_post, plkcol);

    PARSER_VARCHAR *q = 0;
   printf ("*******************************\n"); 
   for(PT_NODE* col = plkcol->col_list; col; col = col->next)
   {        
        q = pt_print_bytes (this_parser, col);
	printf (">>>(%s)<<<\n", (char *) q->bytes);
   }
   printf ("*******************************\n"); 
}

static void pt_convert_dblink_query(PT_NODE* query_stmt)
{
   PT_QUERY_INFO* query = &query_stmt->info.query;
   PT_NODE* list = query->q.select.from;
   PT_NODE* new_list = NULL;      
   PT_NODE* from_tbl;
   PT_NODE* dbl;   
   PT_SPEC_INFO* spec;

   while(list)
     {
        from_tbl = list;
        list = list->next;
        from_tbl->next = NULL;

        spec = &(from_tbl->info.spec);

        if(spec->entity_name && spec->ct_server_name)
          {
             S_LINK_COLUMNS lkcol;

             memset(&lkcol, 0x00, sizeof(lkcol));

             // Do NOT automatically assign a user name. 
             PT_NAME_INFO_CLEAR_FLAG(spec->entity_name, PT_NAME_INFO_USER_SPECIFIED); 
            
             lkcol.tbl_name_node = spec->range_var ? spec->range_var : spec->entity_name;
             pt_get_cols_4_dblink(&lkcol, query->q.select.list);
            // PT_ERROR (this_parser, query_stmt, "777777777777777777777");
             if(lkcol.err != 0)
             {
                     ;
             }           
             else 
             {
                dbl = pt_mk_spec_drived_dblink_table(spec, query->q.select.list);
                if(!dbl)
                {
                        PT_ERROR (this_parser, query_stmt, "Oops! Sorry, insufficient memory."); // ctshim
                }
                else
                {                     
                        dbl->info.spec.derived_table->info.dblink_table.sel_list = lkcol.col_list;                        
                        lkcol.col_list = NULL;
                        parser_free_tree(this_parser, from_tbl);
                }  
                from_tbl = dbl;
             }
              parser_free_tree(this_parser, lkcol.col_list);
              printf ("DEBUG(1): %s\n", parser_print_tree (this_parser, from_tbl));
          }

        new_list = new_list ? parser_make_link(new_list, from_tbl) : from_tbl;        
     }

   query->q.select.from = new_list;
}

static PT_NODE *
pt_create_paren_expr_list (PT_NODE * exp)
{
  PT_NODE *val, *tmp;

  if (exp && exp->next == NULL)
    {
      if (exp->node_type == PT_EXPR)
	{
	  exp->info.expr.paren_type = 1;
	}
      exp->flag.is_paren = 1;
    }
  else
    {
      val = parser_new_node (this_parser, PT_VALUE);
      if (val)
	{
	  for (tmp = exp; tmp; tmp = tmp->next)
	    {
	      if (tmp->node_type == PT_VALUE && tmp->type_enum == PT_TYPE_EXPR_SET)
		{
		  tmp->type_enum = PT_TYPE_SEQUENCE;
		}
	    }

	  val->info.value.data_value.set = exp;
	  val->type_enum = PT_TYPE_EXPR_SET;
	}
      exp = val;
      parser_groupby_exception = PT_EXPR;
    }
  return exp;
}


static PT_NODE *
pt_check_non_logical_expr (PARSER_CONTEXT * parser, PT_NODE * node)
{
   if(node)
     {
        if (node->type_enum != PT_TYPE_LOGICAL)
          {
             PT_ERROR (parser, node, "operand must be logical expression.");
          }
     }

     return node;
}

static void
pt_fill_conn_info_container(PARSER_CONTEXT *parser,  int buffer_pos, container_10 *ctn, container_2 info)
{
  /* container order
  * 1: HOST(IP/NAME)
  * 2: PORT
  * 3: DBNMAE
  * 4: USER
  * 5: PASSWORD
  * 6: PROPERTIES
  * 7: COMMENT
  */                 
   PT_NODE* node = pt_top(parser);
   PARSER_SAVE_ERR_CONTEXT (node, buffer_pos)

   switch(TO_NUMBER (CONTAINER_AT_0(info)))
     {
        case CONN_INFO_HOST:
                if (ctn->c1 != NULL)
                {
                        PT_ERROR (parser, node, "HOST information was duplicated.");
                }
                ctn->c1 = CONTAINER_AT_1(info);
                break;
        case CONN_INFO_PORT:
                if( ctn->c2 != NULL )
                {
                    PT_ERROR (parser, node, "PORT information was duplicated.");
                }
                ctn->c2 = CONTAINER_AT_1(info);
                break;
        case CONN_INFO_DBNAME:
                if( ctn->c3 != NULL )
                {
                    PT_ERROR (parser, node, "DBNAME information was duplicated.");
                }
                ctn->c3 = CONTAINER_AT_1(info);
                break;                
        case CONN_INFO_USER:
                if( ctn->c4 != NULL )
                {
                    PT_ERROR (parser, node, "USER information was duplicated.");
                }
                ctn->c4 = CONTAINER_AT_1(info);
                break;                
        case CONN_INFO_PASSWORD:
                if( ctn->c5 != NULL )
                {
                    PT_ERROR (parser, node, "PASSWORD information was duplicated.");
                }
                ctn->c5 = CONTAINER_AT_1(info);
                break;
        case CONN_INFO_PROPERTIES:
                if( ctn->c6 != NULL )
                {
                    PT_ERROR (parser, node, "PROPERTIES information was duplicated.");
                }
                ctn->c6 = CONTAINER_AT_1(info);
                break;                
        case CONN_INFO_COMMENT:
                if( ctn->c7 != NULL )
                {
                    PT_ERROR (parser, node, "COMMENT information was duplicated.");
                }
                ctn->c7 = CONTAINER_AT_1(info);        
                break;

        case CONN_INFO_OWNER:
                if( ctn->c8 != NULL )
                {
                    PT_ERROR (parser, node, "OWNER information was duplicated.");
                }
                ctn->c8 = CONTAINER_AT_1(info);
                break;

        default:
                assert(0);
                break;
    }

    unsigned int set_bits = (unsigned int)TO_NUMBER(CONTAINER_AT_9(*ctn));
    set_bits |= (0x01 << TO_NUMBER (CONTAINER_AT_0(info)));
    ctn->c10 = FROM_NUMBER(set_bits);
}

static bool
pt_check_one_stmt(char* p)
{
    char end;
    char* t = strchr(p, ';');
    if(!t)
     {
        return true;
     }

    t++;
    while (*t == ' ' || *t == '\t' || *t == '\r' || *t == '\n')
     {
        t++;
     }
    if(*t == '\0')
       return true;

    while(*p)
    {
        if( *p == '[' || *p == '"' || *p == '`' || *p == '\'')
          {
                end = (*p == '[') ? ']' : *p;
                for(p++; *p ; p++)
                {
                    if (*p == end)
                    {
                       p++;
                       break;
                    }
                }
          }
        else if((p[0] == '-' && p[1] == '-') || (p[0] == '/' && p[1] == '/'))
        {
                for(p += 2; *p ; p++)
                {
                    if (*p == '\n')
                    {
                       p++;
                       break;
                    }
                }
        }
        else if(p[0] == '/' && p[1] == '*')
        {
                for(p += 2; *p ; p++)
                {
                    if (p[0] == '*' && p[1] == '/')
                    {
                       p += 2;
                       break;
                    }
                }
        }
        else if(*p == ';')
          {
             p++;

             /* Notice;
             **   Forces not to have multiple statements.
             **   Because of this processing, this function will in fact always return true.
             **   When multiple statements come,
             **   it is a policy that forces only the first statement to be executed without error handling.
             **   For this, a single line of code is added below: "*p = '\0'".
             */
             *p = '\0';

             break;
          }
        else
          {
            p++;
          }
    }

    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
     {
        p++;
     }

    return (*p == '\0');
}

static bool 
pt_ct_check_select (char* p, char *perr_msg)
{  
   perr_msg[0] = 0x00;
   while (*p == ' ' || *p == '(' || *p == '\t' || *p == '\r' || *p == '\n')
     {
        p++;
     }

   if(*p)
   {
        if( strncasecmp(p, "SELECT", 6) == 0 )
        {
            if( p[6] == ' ' || p[6] == '\t' || p[6] == '\r' || p[6] == '\n' )
              {    
                 if (pt_check_one_stmt(p + 6))
                   {
                     return true;
                   }
                  /* sprintf(perr_msg, "Only one statement is allowed."); */
                  return false;
              }
        }
   }

   sprintf(perr_msg, "Only SELECT statements are supported.");
   return false;
}
