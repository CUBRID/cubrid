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
 * parse_tree.h - Parse tree structures and types
 */

#ifndef _PARSE_TREE_H_
#define _PARSE_TREE_H_

#if defined (SERVER_MODE)
#error Does not belong to server module
#endif /* SERVER_MODE */

#ident "$Id$"

#include <setjmp.h>
#include <assert.h>

#include "class_object.h"
#include "compile_context.h"
#include "config.h"
#include "cursor.h"
#include "json_table_def.h"
#include "message_catalog.h"
#include "string_opfunc.h"
#include "system_parameter.h"

// forward definitions
struct json_t;

#define MAX_PRINT_ERROR_CONTEXT_LENGTH 64

//this could be a variadic template function; directly use pt_frob_error() for formatted messages without catalog
#define pt_cat_error(parser, node, setNo, msgNo, ...) \
    pt_frob_error(parser, node, msgcat_message(MSGCAT_CATALOG_CUBRID, setNo, msgNo), ##__VA_ARGS__)

#if 1				//not necessary anymore thanks to new pt_cat_error() and existing pt_frob_error()
#define PT_ERROR(parser, node, msg) pt_frob_error(parser, node, msg)
#define PT_ERRORc(parser, node, msg) pt_frob_error( parser, node, "%s", msg)

#define PT_ERRORf(parser, node, msg, arg1) pt_frob_error(parser, node, msg, arg1)
#define PT_ERRORf2(parser, node, msg, arg1, arg2) pt_frob_error(parser, node, msg, arg1, arg2)
#define PT_ERRORf3(parser, node, msg, arg1, arg2, arg3) pt_frob_error(parser, node, msg, arg1, arg2, arg3)
#define PT_ERRORf4(parser, node, msg, arg1, arg2, arg3, arg4) pt_frob_error(parser, node, msg, arg1, arg2, arg3, arg4)
#define PT_ERRORf5(parser, node, msg, arg1, arg2, arg3, arg4, arg5) pt_frob_error(parser, node, msg, arg1, arg2, arg3, arg4, arg5)

#define PT_ERRORm(parser, node, setNo, msgNo) pt_cat_error(parser, node, setNo, msgNo)
#define PT_ERRORmf(parser, node, setNo, msgNo, arg1) pt_cat_error(parser, node, setNo, msgNo, arg1)
#define PT_ERRORmf2(parser, node, setNo, msgNo, arg1, arg2) pt_cat_error(parser, node, setNo, msgNo, arg1, arg2)
#define PT_ERRORmf3(parser, node, setNo, msgNo, arg1, arg2, arg3) pt_cat_error(parser, node, setNo, msgNo, arg1, arg2, arg3)
#define PT_ERRORmf4(parser, node, setNo, msgNo, arg1, arg2, arg3, arg4) pt_cat_error(parser, node, setNo, msgNo, arg1, arg2, arg3, arg4)
#define PT_ERRORmf5(parser, node, setNo, msgNo, arg1, arg2, arg3, arg4 , arg5) pt_cat_error(parser, node, setNo, msgNo, arg1, arg2, arg3, arg4, arg5)
#endif

//this could be a variadic template function; directly use pt_frob_warning() for formatted messages without catalog
#define pt_cat_warning(parser, node, setNo, msgNo, ...) \
    pt_frob_warning(parser, node, msgcat_message(MSGCAT_CATALOG_CUBRID, setNo, msgNo), ##__VA_ARGS__)

#if 1				//not necessary anymore thanks to pt_cat_warning() and existing pt_frob_warning()
#define PT_WARNING( parser, node, msg ) pt_frob_warning(parser, node, msg)
#define PT_WARNINGm(parser, node, setNo, msgNo) pt_cat_warning(parser, node, setNo, msgNo)
#define PT_WARNINGc( parser, node, msg ) pt_frob_warning(parser, node, msg)

#define PT_WARNINGf( parser, node, msg, arg1) pt_cat_warning(parser, node, msg, arg1)
#define PT_WARNINGf2( parser, node, msg, arg1, arg2) pt_cat_warning(parser, node, msg, arg1, arg2)
#define PT_WARNINGf3( parser, node, msg, arg1, arg2, arg3) pt_cat_warning(parser, node, msg, arg1, arg2, arg3)

#define PT_WARNINGmf(parser, node, setNo, msgNo, arg1) pt_cat_warning(parser, node, setNo, msgNo, arg1)
#define PT_WARNINGmf2(parser, node, setNo, msgNo, arg1, arg2) pt_cat_warning(parser, node, setNo, msgNo, arg1, arg2)
#define PT_WARNINGmf3(parser, node, setNo, msgNo, arg1, arg2, arg3) pt_cat_warning(parser, node, setNo, msgNo, arg1, arg2, arg3)
#endif

#define PT_SET_JMP_ENV(parser) \
    do { \
      if (setjmp((parser)->jmp_env) != 0) { \
	pt_record_error((parser), (parser)->statement_number, \
			(parser)->line, \
			(parser)->column, \
			msgcat_message (MSGCAT_CATALOG_CUBRID, \
			                MSGCAT_SET_PARSER_SEMANTIC, \
				        MSGCAT_SEMANTIC_OUT_OF_MEMORY), \
					NULL); \
	(parser)->jmp_env_active = 0; \
	if ((parser)->au_save) \
	    AU_ENABLE((parser)->au_save); \
	return NULL; \
      } \
        else (parser)->jmp_env_active = 1; \
    } while(0)

#define PT_CLEAR_JMP_ENV(parser) \
    do { \
      (parser)->jmp_env_active = 0; \
    } while(0)

#define PT_INTERNAL_ERROR(parser, what) \
	pt_internal_error((parser), __FILE__, __LINE__, (what))

#define PT_IS_QUERY_NODE_TYPE(x) \
    (  (x) == PT_SELECT     || (x) == PT_UNION \
    || (x) == PT_DIFFERENCE || (x) == PT_INTERSECTION)

#define PT_IS_CLASSOID_NAME(x) \
    (  (x)->info.name.meta_class == PT_CLASSOID_ATTR)

#define PT_IS_NULL_NODE(e)     ((e)->type_enum==PT_TYPE_NA \
				|| (e)->type_enum==PT_TYPE_NULL)

#define PT_IS_NUMERIC_TYPE(t) \
        ( ((t) == PT_TYPE_INTEGER)  || \
	  ((t) == PT_TYPE_BIGINT)   || \
	  ((t) == PT_TYPE_FLOAT)    || \
	  ((t) == PT_TYPE_DOUBLE)   || \
	  ((t) == PT_TYPE_SMALLINT) || \
	  ((t) == PT_TYPE_MONETARY) || \
	  ((t) == PT_TYPE_LOGICAL)  || \
	  ((t) == PT_TYPE_NUMERIC))

#define PT_IS_DISCRETE_NUMBER_TYPE(t) \
        ( ((t) == PT_TYPE_INTEGER)  || \
          ((t) == PT_TYPE_BIGINT)   || \
	  ((t) == PT_TYPE_SMALLINT))

#define PT_IS_COUNTER_TYPE(t) \
		PT_IS_DISCRETE_NUMBER_TYPE(t)

#define PT_IS_COLLECTION_TYPE(t) \
        ( ((t) == PT_TYPE_SET)       || \
	  ((t) == PT_TYPE_MULTISET)  || \
	  ((t) == PT_TYPE_SEQUENCE))

#define PT_IS_STRING_TYPE(t) \
        ( ((t) == PT_TYPE_CHAR)     || \
	  ((t) == PT_TYPE_VARCHAR)  || \
	  ((t) == PT_TYPE_NCHAR)    || \
	  ((t) == PT_TYPE_VARNCHAR) || \
	  ((t) == PT_TYPE_BIT)      || \
	  ((t) == PT_TYPE_VARBIT))

#define PT_IS_NATIONAL_CHAR_STRING_TYPE(t) \
        ( ((t) == PT_TYPE_NCHAR)      || \
	  ((t) == PT_TYPE_VARNCHAR))

#define PT_IS_SIMPLE_CHAR_STRING_TYPE(t) \
        ( ((t) == PT_TYPE_CHAR)      || \
	  ((t) == PT_TYPE_VARCHAR))

#define PT_IS_CHAR_STRING_TYPE(t) \
        ( ((t) == PT_TYPE_CHAR)      || \
	  ((t) == PT_TYPE_VARCHAR)   || \
	  ((t) == PT_TYPE_NCHAR)     || \
	  ((t) == PT_TYPE_VARNCHAR))

#define PT_IS_BIT_STRING_TYPE(t) \
        ( ((t) == PT_TYPE_BIT)      || \
	  ((t) == PT_TYPE_VARBIT))

#define PT_IS_COMPLEX_TYPE(t) \
        ( ((t) == PT_TYPE_MONETARY)  || \
	  ((t) == PT_TYPE_NUMERIC)   || \
	  ((t) == PT_TYPE_CHAR)      || \
	  ((t) == PT_TYPE_VARCHAR)   || \
	  ((t) == PT_TYPE_NCHAR)     || \
	  ((t) == PT_TYPE_VARNCHAR)  || \
	  ((t) == PT_TYPE_BIT)       || \
	  ((t) == PT_TYPE_VARBIT)    || \
	  ((t) == PT_TYPE_OBJECT)    || \
	  ((t) == PT_TYPE_SET)       || \
	  ((t) == PT_TYPE_MULTISET)  || \
	  ((t) == PT_TYPE_SEQUENCE)  || \
	  ((t) == PT_TYPE_ENUMERATION))

#define PT_IS_DATE_TIME_WITH_TZ_TYPE(t) \
        ( (t) == PT_TYPE_TIMESTAMPTZ  || \
	  (t) == PT_TYPE_TIMESTAMPLTZ || \
	  (t) == PT_TYPE_DATETIMETZ   || \
	  (t) == PT_TYPE_DATETIMELTZ)

#define PT_IS_DATE_TIME_TYPE(t) \
        ( ((t) == PT_TYPE_DATE)         || \
	  ((t) == PT_TYPE_TIME)         || \
	  ((t) == PT_TYPE_TIMESTAMP)    || \
	  ((t) == PT_TYPE_DATETIME)     || \
	  ((t) == PT_TYPE_DATETIMETZ)   || \
	  ((t) == PT_TYPE_DATETIMELTZ)  || \
	  ((t) == PT_TYPE_TIMESTAMPTZ)  || \
	  ((t) == PT_TYPE_TIMESTAMPLTZ))

#define PT_HAS_DATE_PART(t) \
        ( ((t) == PT_TYPE_DATE)         || \
	  ((t) == PT_TYPE_TIMESTAMP)    || \
	  ((t) == PT_TYPE_DATETIME)     || \
	  ((t) == PT_TYPE_DATETIMETZ)   || \
	  ((t) == PT_TYPE_DATETIMELTZ)  || \
	  ((t) == PT_TYPE_TIMESTAMPTZ)  || \
	  ((t) == PT_TYPE_TIMESTAMPLTZ))

#define PT_HAS_TIME_PART(t) \
        ( ((t) == PT_TYPE_TIME)         || \
	  ((t) == PT_TYPE_TIMESTAMP)    || \
	  ((t) == PT_TYPE_TIMESTAMPTZ)  || \
	  ((t) == PT_TYPE_TIMESTAMPLTZ) || \
	  ((t) == PT_TYPE_DATETIME)	|| \
	  ((t) == PT_TYPE_DATETIMETZ)	|| \
	  ((t) == PT_TYPE_DATETIMELTZ))

#define PT_IS_LTZ_TYPE(t) \
  ((t) == PT_TYPE_TIMESTAMPLTZ || (t) == PT_TYPE_DATETIMELTZ)

#define PT_IS_PRIMITIVE_TYPE(t) \
        ( ((t) != PT_TYPE_OBJECT) && ((t) != PT_TYPE_NONE))

#define PT_IS_PARAMETERIZED_TYPE(t) \
        ( ((t) == PT_TYPE_NUMERIC)  || \
	  ((t) == PT_TYPE_VARCHAR)  || \
	  ((t) == PT_TYPE_CHAR)     || \
	  ((t) == PT_TYPE_VARNCHAR) || \
	  ((t) == PT_TYPE_NCHAR)    || \
	  ((t) == PT_TYPE_VARBIT)   || \
	  ((t) == PT_TYPE_BIT)	    || \
	  ((t) == PT_TYPE_ENUMERATION))

#define PT_IS_LOB_TYPE(t) \
        ( ((t) == PT_TYPE_BLOB)  || \
	  ((t) == PT_TYPE_CLOB))

#define PT_HAS_COLLATION(t) \
        ( ((t) == PT_TYPE_CHAR)     || \
	  ((t) == PT_TYPE_VARCHAR)  || \
	  ((t) == PT_TYPE_NCHAR)    || \
	  ((t) == PT_TYPE_VARNCHAR) || \
	  ((t) == PT_TYPE_ENUMERATION))

#define PT_VALUE_GET_BYTES(node) \
  ((node) == NULL ? NULL : \
   (node)->info.value.data_value.str->bytes)

#define pt_is_select(n) PT_IS_SELECT(n)
#define pt_is_union(n) PT_IS_UNION(n)
#define pt_is_intersection(n) PT_IS_INTERSECTION(n)
#define pt_is_difference(n) PT_IS_DIFFERENCE(n)
#define pt_is_query(n) PT_IS_QUERY(n)
#define pt_is_correlated_subquery(n) PT_IS_CORRELATED_SUBQUERY(n)
#define pt_is_dot_node(n) PT_IS_DOT_NODE(n)
#define pt_is_expr_node(n) PT_IS_EXPR_NODE(n)
#define pt_is_function(n) PT_IS_FUNCTION(n)
#define pt_is_multi_col_term(n) PT_IS_MULTI_COL_TERM(n)
#define pt_is_name_node(n) PT_IS_NAME_NODE(n)
#define pt_is_oid_name(n) PT_IS_OID_NAME(n)
#define pt_is_value_node(n) PT_IS_VALUE_NODE(n)
#define pt_is_set_type(n) PT_IS_SET_TYPE(n)
#define pt_is_hostvar(n) PT_IS_HOSTVAR(n)
#define pt_is_input_hostvar(n) PT_IS_INPUT_HOSTVAR(n)
#define pt_is_output_hostvar(n) PT_IS_OUTPUT_HOSTVAR(n)
#define pt_is_const(n) PT_IS_CONST(n)
#define pt_is_parameter(n) PT_IS_PARAMETER(n)
#define pt_is_input_parameter(n) PT_IS_INPUT_PARAMETER(n)
#define pt_is_const_not_hostvar(n) PT_IS_CONST_NOT_HOSTVAR(n)
#define pt_is_const_input_hostvar(n) PT_IS_CONST_INPUT_HOSTVAR(n)
#define pt_is_cast_const_input_hostvar(n) PT_IS_CAST_CONST_INPUT_HOSTVAR(n)
#define pt_is_instnum(n) PT_IS_INSTNUM(n)
#define pt_is_orderbynum(n) PT_IS_ORDERBYNUM(n)
#define pt_is_distinct(n) PT_IS_DISTINCT(n)
#define pt_is_meta(n) PT_IS_META(n)
#define pt_is_update_object(n) PT_IS_UPDATE_OBJECT(n)
#define pt_is_unary(op) PT_IS_UNARY(op)

#define PT_IS_SELECT(n) \
        ( (n) && ((n)->node_type == PT_SELECT) )

#define PT_IS_UNION(n) \
        ( (n) && ((n)->node_type == PT_UNION) )

#define PT_IS_INTERSECTION(n) \
        ( (n) && ((n)->node_type == PT_INTERSECTION) )

#define PT_IS_DIFFERENCE(n) \
        ( (n) && ((n)->node_type == PT_DIFFERENCE) )

#define PT_IS_QUERY(n) \
        ( (n) && (PT_IS_QUERY_NODE_TYPE((n)->node_type)) )

#define PT_IS_CORRELATED_SUBQUERY(n) \
        ( PT_IS_QUERY((n)) && ((n)->info.query.correlation_level > 0) )

#define PT_IS_DOT_NODE(n) \
        ( (n) && ((n)->node_type == PT_DOT_) )

#define PT_IS_EXPR_NODE(n) \
        ( (n) && ((n)->node_type == PT_EXPR) )

#define PT_IS_ASSIGN_NODE(n) \
        ( (n) && ((n)->node_type == PT_EXPR && (n)->info.expr.op == PT_ASSIGN) )

#define PT_IS_FUNCTION(n) \
        ( (n) && ((n)->node_type == PT_FUNCTION) )

#define PT_IS_MULTI_COL_TERM(n) \
	( (n) && \
	  PT_IS_FUNCTION((n)) && \
	  PT_IS_SET_TYPE ((n)) && \
	  (n)->info.function.function_type == F_SEQUENCE)

#define PT_IS_NAME_NODE(n) \
        ( (n) && ((n)->node_type == PT_NAME) )

#define PT_IS_OID_NAME(n) \
        ( (n) && \
          ((n)->node_type == PT_NAME && \
             ((n)->info.name.meta_class == PT_OID_ATTR || \
              (n)->info.name.meta_class == PT_VID_ATTR) ) \
        )

#define PT_IS_VALUE_NODE(n) \
        ( (n) && ((n)->node_type == PT_VALUE) )

#define PT_IS_INSERT_VALUE_NODE(n) \
	( (n) && ((n)->node_type == PT_INSERT_VALUE) )

#define PT_IS_SET_TYPE(n) \
        ( (n) && ((n)->type_enum == PT_TYPE_SET || \
                  (n)->type_enum == PT_TYPE_MULTISET || \
                  (n)->type_enum == PT_TYPE_SEQUENCE) \
        )

#define PT_IS_HOSTVAR(n) \
        ( (n) && ((n)->node_type == PT_HOST_VAR) )

#define PT_IS_INPUT_HOSTVAR(n) \
        ( (n) && ((n)->node_type == PT_HOST_VAR && \
                  (n)->info.host_var.var_type == PT_HOST_IN) \
        )

#define PT_IS_OUTPUT_HOSTVAR(n) \
        ( (n) && ((n)->node_type == PT_HOST_VAR && \
                  (n)->info.host_var.var_type == PT_HOST_OUT) \
        )

#define PT_IS_PARAMETER(n) \
        ( (n) && ((n)->node_type == PT_NAME && \
                  (n)->info.name.meta_class == PT_PARAMETER) )

#define PT_IS_INPUT_PARAMETER(n) \
        ( (n) && ((n)->node_type == PT_NAME && \
                  (n)->info.name.meta_class == PT_PARAMETER && \
                  (n)->info.name.resolved == NULL) )

#define PT_IS_CONST(n) \
        ( (n) && ((n)->node_type == PT_VALUE || \
                  (n)->node_type == PT_HOST_VAR || \
                  ((n)->node_type == PT_NAME && (n)->info.name.meta_class == PT_PARAMETER) ) \
        )

#define PT_IS_CONST_NOT_HOSTVAR(n) \
        ( (n) && ((n)->node_type == PT_VALUE || \
                  ((n)->node_type == PT_NAME && (n)->info.name.meta_class == PT_PARAMETER) ) \
        )

#define PT_IS_CONST_INPUT_HOSTVAR(n) \
        ( (n) && ((n)->node_type == PT_VALUE || \
                  ((n)->node_type == PT_NAME && (n)->info.name.meta_class == PT_PARAMETER) || \
                  ((n)->node_type == PT_HOST_VAR && (n)->info.host_var.var_type == PT_HOST_IN)) \
        )

#define PT_IS_CAST_CONST_INPUT_HOSTVAR(n) \
        ( (n) && \
          (n)->node_type == PT_EXPR && \
          (n)->info.expr.op == PT_CAST && \
          PT_IS_CONST_INPUT_HOSTVAR((n)->info.expr.arg1) )

#define PT_IS_INSTNUM(n) \
        ( (n) && ((n)->node_type == PT_EXPR && \
                  ((n)->info.expr.op == PT_INST_NUM || (n)->info.expr.op == PT_ROWNUM)) \
        )

#define PT_IS_ORDERBYNUM(n) \
        ( (n) && ((n)->node_type == PT_EXPR && ((n)->info.expr.op == PT_ORDERBY_NUM)) )

#define PT_IS_DISTINCT(n) \
        ( (n) && PT_IS_QUERY_NODE_TYPE((n)->node_type) && (n)->info.query.all_distinct != PT_ALL )

#define PT_IS_META(n) \
        ( ((n) ? ((n)->node_type == PT_NAME ? \
                  ((n)->info.name.meta_class == PT_META_CLASS || \
                   (n)->info.name.meta_class == PT_META_ATTR || \
                   (n)->info.name.meta_class == PT_CLASSOID_ATTR || \
                   (n)->info.name.meta_class == PT_OID_ATTR) : \
                  ((n)->node_type == PT_SPEC && ((n)->info.spec.meta_class == PT_META_CLASS))) \
              : false) )
#define PT_IS_HINT_NODE(n) \
        ( (n) && ((n)->node_type == PT_NAME && (n)->info.name.meta_class == PT_HINT_NAME) )

#define PT_IS_UPDATE_OBJECT(n) \
        ( (n) && (n)->node_type == PT_UPDATE && (n)->info.update.spec == NULL )

#define PT_IS_UNARY(op) \
        ( (op) == PT_NOT || \
          (op) == PT_IS_NULL || \
          (op) == PT_IS_NOT_NULL || \
          (op) == PT_EXISTS || \
          (op) == PT_PRIOR || \
          (op) == PT_CONNECT_BY_ROOT || \
	  (op) == PT_QPRIOR || \
          (op) == PT_UNARY_MINUS) )

#define PT_IS_N_COLUMN_UPDATE_EXPR(n) \
        ( (n) && \
          (n)->node_type == PT_EXPR && \
          (n)->info.expr.op == PT_PATH_EXPR_SET )

#define PT_DOES_FUNCTION_HAVE_DIFFERENT_ARGS(op) \
        ((op) == PT_MODULUS || (op) == PT_SUBSTRING || \
         (op) == PT_LPAD || (op) == PT_RPAD || (op) == PT_ADD_MONTHS || \
         (op) == PT_TO_CHAR || (op) == PT_TO_NUMBER || \
         (op) == PT_POWER || (op) == PT_ROUND || \
         (op) == PT_TRUNC || (op) == PT_INSTR || \
         (op) == PT_LEAST || (op) == PT_GREATEST || \
	 (op) == PT_FIELD || \
	 (op) == PT_REPEAT || (op) == PT_SUBSTRING_INDEX || \
	 (op) == PT_MAKEDATE || (op) == PT_MAKETIME || (op) == PT_IF || \
	 (op) == PT_STR_TO_DATE)

#define PT_REQUIRES_HIERARCHICAL_QUERY(op) \
        ( (op) == PT_LEVEL || \
          (op) == PT_CONNECT_BY_ISCYCLE || \
          (op) == PT_CONNECT_BY_ISLEAF || \
          (op) == PT_PRIOR || \
          (op) == PT_CONNECT_BY_ROOT  || \
	  (op) == PT_QPRIOR || \
          (op) == PT_SYS_CONNECT_BY_PATH )

#define PT_CHECK_HQ_OP_EXCEPT_PRIOR(op) \
        ( (op) == PT_LEVEL || \
          (op) == PT_CONNECT_BY_ISCYCLE || \
          (op) == PT_CONNECT_BY_ISLEAF || \
          (op) == PT_CONNECT_BY_ROOT  || \
	  (op) == PT_SYS_CONNECT_BY_PATH )

#define PT_IS_NUMBERING_AFTER_EXECUTION(op) \
        ( (op) == PT_INST_NUM || \
          (op) == PT_ROWNUM || \
          /*(int)(op) == (int)PT_GROUPBY_NUM || - TODO: this does not belong here. */ \
          (op) == PT_ORDERBY_NUM )

#define PT_IS_SERIAL(op) \
        ( (op) == PT_CURRENT_VALUE || (op) == PT_NEXT_VALUE )

#define PT_IS_EXPR_NODE_WITH_OPERATOR(n, op_type) \
        ( (PT_IS_EXPR_NODE (n)) && ((n)->info.expr.op == (op_type)) )

#define PT_IS_EXPR_WITH_PRIOR_ARG(x) (PT_IS_EXPR_NODE (x) && \
		PT_IS_EXPR_NODE_WITH_OPERATOR ((x)->info.expr.arg1, PT_PRIOR))

#define PT_NODE_DATA_TYPE(n) \
	( (n) ? (n)->data_type : NULL )

#define PT_IS_SORT_SPEC_NODE(n) \
        ( (n) && ((n)->node_type == PT_SORT_SPEC) )

#define PT_IS_VALUE_QUERY(n) \
          ((n)->flag.is_value_query == 1)

#define PT_SET_VALUE_QUERY(n) \
          ((n)->flag.is_value_query = 1)

#define PT_IS_ORDER_DEPENDENT(n) \
        ( (n) ? \
          (PT_IS_EXPR_NODE (n) ? (n)->info.expr.is_order_dependent \
            : (PT_IS_FUNCTION(n) ? (n)->info.function.is_order_dependent \
            : (PT_IS_QUERY(n) ? (n)->info.query.is_order_dependent : false))) \
          : false)

#define PT_IS_ANALYTIC_NODE(n) \
        ( (n) && (n)->node_type == PT_FUNCTION && \
          (n)->info.function.analytic.is_analytic )

#define PT_IS_POINTER_REF_NODE(n) \
        ( (n) && (n)->node_type == PT_NODE_POINTER && \
          (n)->info.pointer.type == PT_POINTER_REF )

#define PT_IS_VACUUM_NODE(n) \
	( (n) && (n)->node_type == PT_VACUUM )

#define PT_SET_ORDER_DEPENDENT_FLAG(n, f) \
        do { \
          if ((n)) \
            { \
              if (PT_IS_EXPR_NODE (n)) \
                (n)->info.expr.is_order_dependent = (f); \
              else if (PT_IS_FUNCTION (n)) \
                (n)->info.function.is_order_dependent = (f); \
              else if (PT_IS_QUERY (n)) \
                (n)->info.query.is_order_dependent = (f); \
            } \
        } while (0)

#if !defined (SERVER_MODE)
/* the following defines support host variable binding for internal statements.
   internal statements can be generated on TEXT handling, and these statements
   can include host variables derived from original statement. so, to look up
   host variable table by internal statements at parser, keep the parser,
   i.e parent_parser */

#define CLEAR_HOST_VARIABLES(parser_) \
    do { DB_VALUE *hv; int i; \
        for (i = 0, hv = parser_->host_variables; \
             i < (parser_->host_var_count + parser_->auto_param_count); i++, hv++) \
            db_value_clear(hv); \
        free_and_init(parser_->host_variables); \
	free_and_init(parser_->host_var_expected_domains);} while (0)

#define SET_HOST_VARIABLES_IF_INTERNAL_STATEMENT(parser_) \
    do { if (parent_parser) { \
             if (parser_->host_variables != NULL && \
                 parser_->host_variables != parent_parser->host_variables) { \
                 CLEAR_HOST_VARIABLES(parser_); } \
             parser_->host_variables = parent_parser->host_variables; \
	     parser_->host_var_expected_domains = parent_parser->host_var_expected_domains; \
             parser_->host_var_count = parent_parser->host_var_count; \
             parser_->auto_param_count = parent_parser->auto_param_count; \
             parser_->flag.set_host_var = 1; } } while (0)

#define RESET_HOST_VARIABLES_IF_INTERNAL_STATEMENT(parser_) \
    do { if (parent_parser) { \
             parser_->host_variables = NULL; parser_->host_var_count = 0; \
	     parser_->host_var_expected_domains = NULL; \
             parser_->auto_param_count = 0; parser_->flag.set_host_var = 0; } } while (0)

#endif /* !SERVER_MODE */

/* NODE FUNCTION DECLARATIONS */
#define IS_UPDATE_OBJ(node) (node->node_type == PT_UPDATE && node->info.update.object_parameter)

#define PT_NODE_INIT_OUTERLINK(n)                        \
    do {                                                 \
        if ((n)) {                                       \
            (n)->next          = NULL;                   \
            (n)->or_next       = NULL;                   \
            (n)->etc           = NULL;                   \
            (n)->alias_print   = NULL;                   \
        }                                                \
    } while (0)

#define PT_NODE_COPY_NUMBER_OUTERLINK(t, s)              \
    do {                                                 \
        if ((t) && (s)) {                                \
            (t)->line_number   = (s)->line_number;       \
            (t)->column_number = (s)->column_number;     \
            (t)->next          = (s)->next;              \
            (t)->or_next       = (s)->or_next;           \
            (t)->etc           = (s)->etc;               \
            (t)->alias_print   = (s)->alias_print;       \
        }                                                \
    } while (0)

#define PT_NODE_MOVE_NUMBER_OUTERLINK(t, s)              \
    do {                                                 \
        PT_NODE_COPY_NUMBER_OUTERLINK((t), (s));         \
        PT_NODE_INIT_OUTERLINK((s));                     \
    } while (0)

#define PT_NODE_PRINT_TO_ALIAS(p, n, c)                         \
    do {                                                        \
        unsigned int save_custom;                               \
                                                                \
        if (!(p) || !(n) || (n->alias_print))                   \
          break;                                                \
        save_custom = (p)->custom_print;                        \
        (p)->custom_print |= (c);                               \
	if (((p)->custom_print & PT_SHORT_PRINT) != 0)		\
	  {							\
	    (n)->alias_print = pt_short_print ((p), (n));	\
	  }							\
	else							\
	  {							\
	    (n)->alias_print = parser_print_tree ((p), (n));    \
	  }							\
        (p)->custom_print = save_custom;                        \
    } while (0)

#define PT_NODE_PRINT_VALUE_TO_TEXT(p, n)			\
    do {							\
	if (!(p) || !(n) || (n)->node_type != PT_VALUE		\
	    || (n)->info.value.text)				\
	  {							\
	    break;						\
	  }							\
	(n)->info.value.text = parser_print_tree ((p), (n));	\
    } while (0)

#define CAST_POINTER_TO_NODE(p)                             \
    do {                                                    \
        while ((p) && (p)->node_type == PT_NODE_POINTER &&       \
               (p)->info.pointer.type == PT_POINTER_NORMAL) \
        {                                                   \
            (p) = (p)->info.pointer.node;                   \
        }                                                   \
    } while (0)

#define PT_EMPTY	INT_MAX
#define MAX_NUM_PLAN_TRACE        100

#define PT_GET_COLLATION_MODIFIER(p)					     \
  (((p)->node_type == PT_EXPR) ? ((p)->info.expr.coll_modifier - 1) :	     \
  (((p)->node_type == PT_VALUE) ? ((p)->info.value.coll_modifier - 1) :	     \
  (((p)->node_type == PT_NAME) ? ((p)->info.name.coll_modifier - 1) :	     \
  (((p)->node_type == PT_FUNCTION) ? ((p)->info.function.coll_modifier - 1) :\
  (((p)->node_type == PT_DOT_) ? ((p)->info.dot.coll_modifier - 1) : (-1))))))

#define PT_SET_NODE_COLL_MODIFIER(p, coll)                  \
    do {                                                    \
      assert ((p) != NULL);				    \
      if ((p)->node_type == PT_EXPR)			    \
	{						    \
	  (p)->info.expr.coll_modifier = (coll) + 1;	    \
	}						    \
      else if ((p)->node_type == PT_VALUE)		    \
	{						    \
	  (p)->info.value.coll_modifier = (coll) + 1;	    \
	}						    \
      else if ((p)->node_type == PT_NAME)		    \
	{						    \
	  (p)->info.name.coll_modifier = (coll) + 1;	    \
	}						    \
      else if ((p)->node_type == PT_FUNCTION)		    \
	{						    \
	  (p)->info.function.coll_modifier = (coll) + 1;    \
	}						    \
      else						    \
	{						    \
	  assert ((p)->node_type == PT_DOT_);		    \
	  (p)->info.dot.coll_modifier = (coll) + 1;	    \
	}						    \
    } while (0)

/* Check if spec is flagged with any of flags_ */
#define PT_IS_SPEC_FLAG_SET(spec_, flags_)		    \
  (((spec_)->info.spec.flag & (flags_)) != 0)

#define PT_SPEC_SPECIAL_INDEX_SCAN(spec_)			    \
  (((spec_)->info.spec.flag &				    \
    (PT_SPEC_FLAG_KEY_INFO_SCAN | PT_SPEC_FLAG_BTREE_NODE_INFO_SCAN)) != 0)

/* Obtain reserved name type from spec flag */
#define PT_SPEC_GET_RESERVED_NAME_TYPE(spec_)				\
  (((spec_) == NULL || (spec_)->node_type != PT_SPEC			\
    || (spec_)->info.spec.flag == 0) ?					\
   /* is spec is NULL or not a PT_SPEC or flag is 0, return invalid */	\
   RESERVED_NAME_INVALID :						\
   /* else */								\
   ((PT_IS_SPEC_FLAG_SET (spec_, PT_SPEC_FLAG_RECORD_INFO_SCAN)) ?	\
    /* if spec is flagged for record info */				\
    RESERVED_NAME_RECORD_INFO :						\
    /* else */								\
    ((PT_IS_SPEC_FLAG_SET (spec_, PT_SPEC_FLAG_PAGE_INFO_SCAN)) ?	\
     /* if spec is flagged for page info */				\
     RESERVED_NAME_PAGE_INFO :						\
     /* else */								\
     ((PT_IS_SPEC_FLAG_SET (spec_, PT_SPEC_FLAG_KEY_INFO_SCAN)) ?	\
      /* if spec is flagged for key info */				\
      RESERVED_NAME_KEY_INFO :						\
      /* else */							\
      ((PT_IS_SPEC_FLAG_SET(spec_, PT_SPEC_FLAG_BTREE_NODE_INFO_SCAN) ?	\
	/* if spec is flagged for b-tree node info */			\
	RESERVED_NAME_BTREE_NODE_INFO :					\
	/* spec is not flagged for any type of reserved names */	\
	RESERVED_NAME_INVALID))))))

/* Check if according to spec flag should bind names as reserved */
#define PT_SHOULD_BIND_RESERVED_NAME(spec_)				\
  (PT_SPEC_GET_RESERVED_NAME_TYPE (spec_) != RESERVED_NAME_INVALID)

/* PT_SPEC node contains a derived table */
#define PT_SPEC_IS_DERIVED(spec_) \
  ((spec_)->info.spec.derived_table != NULL)

/* PT_SPEC node contains a CTE pointer */
#define PT_SPEC_IS_CTE(spec_) \
  ((spec_)->info.spec.cte_pointer != NULL)

/* PT_SPEC node contains an entity spec */
#define PT_SPEC_IS_ENTITY(spec_) \
  ((spec_)->info.spec.entity_name != NULL)

#define PT_IS_FALSE_WHERE_VALUE(node) \
 (((node) != NULL && (node)->node_type == PT_VALUE \
  && ((node)->type_enum == PT_TYPE_NULL \
       || ((node)->type_enum == PT_TYPE_SET \
           && ((node)->info.value.data_value.set == NULL)))) ? true : false)

#define PT_IS_SPEC_REAL_TABLE(spec_) PT_SPEC_IS_ENTITY(spec_)

/*
 Enumerated types of parse tree statements
  WARNING ------ WARNING ----- WARNING
 Member functions parser_new_node, parser_init_node, parser_print_tree, which take a node as an argument
 are accessed by function tables indexed by node_type. The functions in
 the tables must appear in EXACTLY the same order as the node types
 defined below. If you add a new node type you must create the
 functions to manipulate it and put these in the tables. Else crash and burn.
*/

/* enumeration for parser_walk_tree() */
enum
{
  PT_STOP_WALK = 0,
  PT_CONTINUE_WALK,
  PT_LEAF_WALK,
  PT_LIST_WALK
};

enum
{
  PT_USER_SELECT = 0,
  PT_MERGE_SELECT
};

enum pt_custom_print
{
  PT_SUPPRESS_RESOLVED = 0x1,

  PT_SUPPRESS_META_ATTR_CLASS = (0x1 << 1),

  PT_SUPPRESS_INTO = (0x1 << 2),

  PT_SUPPRESS_SELECTOR = (0x1 << 3),

  PT_SUPPRESS_SELECT_LIST = (0x1 << 4),

  PT_SUPPRESS_QUOTES = (0x1 << 5),

  PT_PRINT_ALIAS = (0x1 << 6),

  PT_PAD_BYTE = (0x1 << 7),

  PT_CONVERT_RANGE = (0x1 << 8),

  PT_PRINT_DB_VALUE = (0x1 << 9),

  PT_SUPPRESS_INDEX = (0x1 << 10),

  PT_SUPPRESS_ORDERING = (0x1 << 11),

  PT_PRINT_QUOTES = (0x1 << 12),

  /* PT_FORCE_ORIGINAL_TABLE_NAME is for PT_NAME nodes.  prints original table name instead of printing resolved NOTE:
   * spec_id must point to original table */
  PT_FORCE_ORIGINAL_TABLE_NAME = (0x1 << 13),

  PT_SUPPRESS_CHARSET_PRINT = (0x1 << 14),

  /* PT_PRINT_DIFFERENT_SYSTEM_PARAMETERS print session parameters */
  PT_PRINT_DIFFERENT_SYSTEM_PARAMETERS = (0x1 << 15),

  /* PT_NODE_PRINT_TO_ALIAS calls pt_short_print instead pt_print_tree */
  PT_SHORT_PRINT = (0x1 << 16),

  PT_SUPPRESS_BIGINT_CAST = (0x1 << 17),

  PT_CHARSET_COLLATE_FULL = (0x1 << 18),

  PT_CHARSET_COLLATE_USER_ONLY = (0x1 << 19),

  PT_PRINT_USER = (0x1 << 20),

  PT_PRINT_ORIGINAL_BEFORE_CONST_FOLDING = (0x1 << 21)
};

/* all statement node types should be assigned their API statement enumeration */
enum pt_node_type
{
  PT_NODE_NONE = CUBRID_STMT_NONE,
  PT_ALTER = CUBRID_STMT_ALTER_CLASS,
  PT_ALTER_INDEX = CUBRID_STMT_ALTER_INDEX,
  PT_ALTER_USER = CUBRID_STMT_ALTER_USER,
  PT_ALTER_SERIAL = CUBRID_STMT_ALTER_SERIAL,
  PT_COMMIT_WORK = CUBRID_STMT_COMMIT_WORK,
  PT_CREATE_ENTITY = CUBRID_STMT_CREATE_CLASS,
  PT_CREATE_INDEX = CUBRID_STMT_CREATE_INDEX,
  PT_CREATE_USER = CUBRID_STMT_CREATE_USER,
  PT_CREATE_TRIGGER = CUBRID_STMT_CREATE_TRIGGER,
  PT_CREATE_SERIAL = CUBRID_STMT_CREATE_SERIAL,
  PT_DROP = CUBRID_STMT_DROP_CLASS,
  PT_DROP_INDEX = CUBRID_STMT_DROP_INDEX,
  PT_DROP_USER = CUBRID_STMT_DROP_USER,
  PT_DROP_VARIABLE = CUBRID_STMT_DROP_LABEL,
  PT_DROP_TRIGGER = CUBRID_STMT_DROP_TRIGGER,
  PT_DROP_SERIAL = CUBRID_STMT_DROP_SERIAL,
  PT_EVALUATE = CUBRID_STMT_EVALUATE,
  PT_RENAME = CUBRID_STMT_RENAME_CLASS,
  PT_ROLLBACK_WORK = CUBRID_STMT_ROLLBACK_WORK,
  PT_GRANT = CUBRID_STMT_GRANT,
  PT_REVOKE = CUBRID_STMT_REVOKE,
  PT_UPDATE_STATS = CUBRID_STMT_UPDATE_STATS,
  PT_GET_STATS = CUBRID_STMT_GET_STATS,
  PT_INSERT = CUBRID_STMT_INSERT,
  PT_SELECT = CUBRID_STMT_SELECT,
  PT_UPDATE = CUBRID_STMT_UPDATE,
  PT_DELETE = CUBRID_STMT_DELETE,
  PT_METHOD_CALL = CUBRID_STMT_CALL,
  PT_GET_XACTION = CUBRID_STMT_GET_ISO_LVL,
  /* should have separate pt node type for CUBRID_STMT_GET_TIMEOUT, It will also be tagged PT_GET_XACTION */
  PT_GET_OPT_LVL = CUBRID_STMT_GET_OPT_LVL,
  PT_SET_OPT_LVL = CUBRID_STMT_SET_OPT_LVL,
  PT_SET_SYS_PARAMS = CUBRID_STMT_SET_SYS_PARAMS,
  PT_SCOPE = CUBRID_STMT_SCOPE,
  PT_SET_TRIGGER = CUBRID_STMT_SET_TRIGGER,
  PT_GET_TRIGGER = CUBRID_STMT_GET_TRIGGER,
  PT_SAVEPOINT = CUBRID_STMT_SAVEPOINT,
  PT_PREPARE_TO_COMMIT = CUBRID_STMT_PREPARE,
  PT_2PC_ATTACH = CUBRID_STMT_ATTACH,
#if defined (ENABLE_UNUSED_FUNCTION)
  PT_USE = CUBRID_STMT_USE,
#endif
  PT_REMOVE_TRIGGER = CUBRID_STMT_REMOVE_TRIGGER,
  PT_RENAME_TRIGGER = CUBRID_STMT_RENAME_TRIGGER,

  PT_CREATE_STORED_PROCEDURE = CUBRID_STMT_CREATE_STORED_PROCEDURE,
  PT_ALTER_STORED_PROCEDURE = CUBRID_STMT_ALTER_STORED_PROCEDURE,
  PT_DROP_STORED_PROCEDURE = CUBRID_STMT_DROP_STORED_PROCEDURE,
  PT_PREPARE_STATEMENT = CUBRID_STMT_PREPARE_STATEMENT,
  PT_EXECUTE_PREPARE = CUBRID_STMT_EXECUTE_PREPARE,
  PT_DEALLOCATE_PREPARE = CUBRID_STMT_DEALLOCATE_PREPARE,
  PT_TRUNCATE = CUBRID_STMT_TRUNCATE,
  PT_DO = CUBRID_STMT_DO,
  PT_SET_SESSION_VARIABLES = CUBRID_STMT_SET_SESSION_VARIABLES,
  PT_DROP_SESSION_VARIABLES = CUBRID_STMT_DROP_SESSION_VARIABLES,
  PT_MERGE = CUBRID_STMT_MERGE,
  PT_SET_NAMES = CUBRID_STMT_SET_NAMES,
  PT_SET_TIMEZONE = CUBRID_STMT_SET_TIMEZONE,

  PT_DIFFERENCE = CUBRID_MAX_STMT_TYPE,	/* these enumerations must be distinct from statements */
  PT_INTERSECTION,		/* difference intersection and union are reported as CUBRID_STMT_SELECT. */
  PT_UNION,

  PT_ZZ_ERROR_MSG,
  PT_ALTER_TRIGGER,
  PT_ATTR_DEF,
  PT_AUTH_CMD,
  PT_AUTO_INCREMENT,
  PT_CHECK_OPTION,
  PT_CONSTRAINT,
  PT_CTE,
  PT_DATA_DEFAULT,
  PT_DATA_TYPE,
  PT_DOT_,
  PT_EVENT_OBJECT,
  PT_EVENT_SPEC,
  PT_EVENT_TARGET,
  PT_EXECUTE_TRIGGER,
  PT_EXPR,
  PT_FILE_PATH,
  PT_FUNCTION,
  PT_HOST_VAR,
  PT_ISOLATION_LVL,
  PT_METHOD_DEF,
  PT_NAME,
  PT_PARTITION,
  PT_PARTS,
  PT_RESOLUTION,
  PT_SET_XACTION,
  PT_SORT_SPEC,
  PT_SP_PARAMETERS,
  PT_SPEC,
  PT_TIMEOUT,
  PT_TRIGGER_ACTION,
  PT_TRIGGER_SPEC_LIST,
  PT_VALUE,
  PT_NODE_POINTER,
  PT_NODE_LIST,
  PT_TABLE_OPTION,
  PT_ATTR_ORDERING,
  PT_TUPLE_VALUE,
  PT_QUERY_TRACE,
  PT_INSERT_VALUE,
  PT_NAMED_ARG,
  PT_SHOWSTMT,
  PT_KILL_STMT,
  PT_VACUUM,
  PT_WITH_CLAUSE,
  PT_JSON_TABLE,
  PT_JSON_TABLE_NODE,
  PT_JSON_TABLE_COLUMN,

  PT_NODE_NUMBER,		/* This is the number of node types */
  PT_LAST_NODE_NUMBER = PT_NODE_NUMBER
};
typedef enum pt_node_type PT_NODE_TYPE;

/* Enumerated Data Types for expressions with a VALUE */
enum pt_type_enum
{
  PT_TYPE_NONE = 1000,		/* type not known yet */
  PT_TYPE_MIN = PT_TYPE_NONE,
  /* primitive types */
  PT_TYPE_INTEGER,
  PT_TYPE_FLOAT,
  PT_TYPE_DOUBLE,
  PT_TYPE_SMALLINT,
  PT_TYPE_DATE,
  PT_TYPE_TIME,
  PT_TYPE_TIMESTAMP,
  PT_TYPE_DATETIME,
  PT_TYPE_MONETARY,
  PT_TYPE_NUMERIC,
  PT_TYPE_CHAR,
  PT_TYPE_VARCHAR,
  PT_TYPE_NCHAR,
  PT_TYPE_VARNCHAR,
  PT_TYPE_BIT,
  PT_TYPE_VARBIT,
  PT_TYPE_LOGICAL,
  PT_TYPE_MAYBE,
  PT_TYPE_JSON,

  /* special values */
  PT_TYPE_NA,			/* in SELECT NA */
  PT_TYPE_NULL,			/* in assignment and defaults */
  PT_TYPE_STAR,			/* select (*), count (*), will be expanded later */

  /* non primitive types */
  PT_TYPE_OBJECT,
  PT_TYPE_SET,
  PT_TYPE_MULTISET,
  PT_TYPE_SEQUENCE,
  PT_TYPE_MIDXKEY,
  PT_TYPE_COMPOUND,

  PT_TYPE_EXPR_SET,		/* type of parentheses expr set, avail for parser only */
  PT_TYPE_RESULTSET,

  PT_TYPE_BIGINT,

  PT_TYPE_BLOB,
  PT_TYPE_CLOB,
  PT_TYPE_ELO,

  PT_TYPE_ENUMERATION,
  PT_TYPE_TIMESTAMPLTZ,
  PT_TYPE_TIMESTAMPTZ,
  PT_TYPE_DATETIMETZ,
  PT_TYPE_DATETIMELTZ,

  PT_TYPE_MAX,
};
typedef enum pt_type_enum PT_TYPE_ENUM;

/* Enumerated priviledges for Grant, Revoke */
typedef enum
{
  PT_NO_PRIV = 2000,		/* this value to initialize the node */
  PT_ADD_PRIV,
  PT_ALL_PRIV,
  PT_ALTER_PRIV,
  PT_DELETE_PRIV,
  PT_DROP_PRIV,
  PT_EXECUTE_PRIV,
  /* PT_GRANT_OPTION_PRIV, avail for revoke only */
  PT_INDEX_PRIV,
  PT_INSERT_PRIV,
  PT_REFERENCES_PRIV,		/* for ANSI compatibility */
  PT_SELECT_PRIV,
  PT_UPDATE_PRIV
} PT_PRIV_TYPE;

/* Enumerated Misc Types */
typedef enum
{
  PT_MISC_NONE = 0,
  PT_MISC_DUMMY = 3000,
  PT_ALL,
  PT_ONLY,
  PT_DISTINCT,
  PT_SHARED,
  PT_DEFAULT,
  PT_ASC,
  PT_DESC,
  PT_GRANT_OPTION,
  PT_NO_GRANT_OPTION,
  PT_CLASS,
  PT_VCLASS,
  PT_VID_ATTR,
  PT_OID_ATTR,
  /* PT_CLASSOID_ATTR is no longer used.  The concept that it used to embody (the OID of the class of an instance is
   * now captured via a first class server function F_CLASS_OF which takes an arbitrary instance valued expression. */
  PT_CLASSOID_ATTR,
  PT_TRIGGER_OID,
  PT_NORMAL,
  /* PT_META_CLASS is used to embody the concept of a class OID reference that is constant at compile time.  (i.e. it
   * does not vary as instance OIDs vary across an inheritance hierarchy).  Contrast this with the F_CLASS_OF function
   * which returns the class OID for any instance valued expression.  F_CLASS_OF is a server side function. */
  PT_META_CLASS,
  PT_META_ATTR,
  PT_PARAMETER,
  PT_HINT_NAME,			/* hint argument name */
  PT_INDEX_NAME,
  PT_RESERVED,			/* reserved names for special attributes */
  PT_IS_SUBQUERY,		/* query is sub-query, not directly producing result */
  PT_IS_UNION_SUBQUERY,		/* in a union sub-query */
  PT_IS_UNION_QUERY,		/* query directly producing result in top level union */
  PT_IS_SET_EXPR,
  PT_IS_CSELECT,		/* query is CSELECT, not directly producing result */
  PT_IS_WHACKED_SPEC,		/* ignore this one in xasl generation, no cross product */
  PT_IS_SUBINSERT,		/* used by value clause of insert */
  PT_IS_VALUE,			/* used by value clause of insert */
  PT_IS_DEFAULT_VALUE,
  PT_ATTRIBUTE,
  PT_METHOD,
  PT_FUNCTION_RENAME,
  PT_FILE_RENAME,
  PT_NO_ISOLATION_LEVEL,	/* value for uninitialized isolation level */
  PT_SERIALIZABLE,
  PT_REPEATABLE_READ,
  PT_READ_COMMITTED,
  PT_ISOLATION_LEVEL,		/* get transaction option */
  PT_LOCK_TIMEOUT,
  PT_HOST_IN,			/* kind of host variable */
  PT_HOST_OUT,
  PT_HOST_OUT_DESCR,
  PT_ACTIVE,			/* trigger status */
  PT_INACTIVE,
  PT_BEFORE,			/* trigger time */
  PT_AFTER,
  PT_DEFERRED,
  PT_REJECT,			/* trigger action */
  PT_INVALIDATE_XACTION,
  PT_PRINT,
  PT_EXPRESSION,
  PT_TRIGGER_TRACE,		/* trigger options */
  PT_TRIGGER_DEPTH,
  PT_IS_CALL_STMT,		/* is the method a call statement */
  PT_IS_MTHD_EXPR,		/* is the method call part of an expr */
  PT_IS_CLASS_MTHD,		/* is the method a class method */
  PT_IS_INST_MTHD,		/* is the method an instance method */
  PT_METHOD_ENTITY,		/* this entity arose from a method call */
  PT_IS_SELECTOR_SPEC,		/* This is the 'real' correspondant of the whacked spec. down in the path entities
				 * portion. */
  PT_PATH_INNER,		/* types of join which may emulate path */
  PT_PATH_OUTER,
  PT_PATH_OUTER_WEASEL,
  PT_LOCAL,			/* local or cascaded view check option */
  PT_CASCADED,
  PT_CURRENT,

  PT_CHAR_STRING,		/* denotes the flavor of a literal string */
  PT_NCHAR_STRING,
  PT_BIT_STRING,
  PT_HEX_STRING,

  PT_MATCH_REGULAR,
  PT_MATCH_FULL,		/* values to support triggered actions for */
  PT_MATCH_PARTIAL,		/* referential integrity constraints */
  PT_RULE_CASCADE,
  PT_RULE_RESTRICT,
  PT_RULE_SET_NULL,
  PT_RULE_SET_DEFAULT,
  PT_RULE_NO_ACTION,

  PT_LEADING,			/* trim operation qualifiers */
  PT_TRAILING,
  PT_BOTH,
  PT_NOPUT,
  PT_INPUT,
  PT_OUTPUT,
  PT_INPUTOUTPUT,

  PT_MILLISECOND,		/* datetime components for extract operation */
  PT_SECOND,
  PT_MINUTE,
  PT_HOUR,
  PT_DAY,
  PT_WEEK,
  PT_MONTH,
  PT_QUARTER,
  PT_YEAR,
  /* mysql units types */
  PT_SECOND_MILLISECOND,
  PT_MINUTE_MILLISECOND,
  PT_MINUTE_SECOND,
  PT_HOUR_MILLISECOND,
  PT_HOUR_SECOND,
  PT_HOUR_MINUTE,
  PT_DAY_MILLISECOND,
  PT_DAY_SECOND,
  PT_DAY_MINUTE,
  PT_DAY_HOUR,
  PT_YEAR_MONTH,

  PT_SIMPLE_CASE,
  PT_SEARCHED_CASE,

  PT_OPT_LVL,			/* Variants of "get/set optimization" statement */
  PT_OPT_COST,

  PT_SUBSTR_ORG,
  PT_SUBSTR,			/* substring qualifier */

  PT_EQ_TORDER,

  PT_SP_PROCEDURE,
  PT_SP_FUNCTION,
  PT_SP_IN,
  PT_SP_OUT,
  PT_SP_INOUT,

  PT_LOB_INTERNAL,
  PT_LOB_EXTERNAL,

  PT_FROM_LAST,
  PT_IGNORE_NULLS,

  PT_NULLS_DEFAULT,
  PT_NULLS_FIRST,
  PT_NULLS_LAST,

  PT_CONSTRAINT_NAME,

  PT_TRACE_ON,
  PT_TRACE_OFF,
  PT_TRACE_FORMAT_TEXT,
  PT_TRACE_FORMAT_JSON,

  PT_IS_SHOWSTMT,		/* query is SHOWSTMT */
  PT_IS_CTE_REC_SUBQUERY,
  PT_IS_CTE_NON_REC_SUBQUERY,

  PT_DERIVED_JSON_TABLE,	// json table spec derivation

  // todo: separate into relevant enumerations
} PT_MISC_TYPE;

/* Enumerated join type */
typedef enum
{
  PT_JOIN_NONE = 0x00,		/* 0000 0000 */
  PT_JOIN_CROSS = 0x01,		/* 0000 0001 */
  PT_JOIN_NATURAL = 0x02,	/* 0000 0010 -- not used */
  PT_JOIN_INNER = 0x04,		/* 0000 0100 */
  PT_JOIN_LEFT_OUTER = 0x08,	/* 0000 1000 */
  PT_JOIN_RIGHT_OUTER = 0x10,	/* 0001 0000 */
  PT_JOIN_FULL_OUTER = 0x20,	/* 0010 0000 -- not used */
  PT_JOIN_UNION = 0x40		/* 0100 0000 -- not used */
} PT_JOIN_TYPE;

typedef enum
{
  PT_HINT_NONE = 0x00,		/* 0000 0000 *//* no hint */
  PT_HINT_ORDERED = 0x01,	/* 0000 0001 *//* force join left-to-right */
  PT_HINT_NO_INDEX_SS = 0x02,	/* 0000 0010 *//* disable index skip scan */
  PT_HINT_INDEX_SS = 0x04,	/* 0000 0100 *//* enable index skip scan */
  PT_HINT_SELECT_BTREE_NODE_INFO = 0x08,	/* temporarily use the unused hint PT_HINT_Y */
  /* SELECT b-tree node information */
  PT_HINT_USE_NL = 0x10,	/* 0001 0000 *//* force nl-join */
  PT_HINT_USE_IDX = 0x20,	/* 0010 0000 *//* force idx-join */
  PT_HINT_USE_MERGE = 0x40,	/* 0100 0000 *//* force m-join */
  PT_HINT_USE_HASH = 0x80,	/* 1000 0000 -- not used */
  PT_HINT_RECOMPILE = 0x0100,	/* 0000 0001 0000 0000 *//* recompile */
  PT_HINT_LK_TIMEOUT = 0x0200,	/* 0000 0010 0000 0000 *//* lock_timeout */
  PT_HINT_NO_LOGGING = 0x0400,	/* 0000 0100 0000 0000 *//* no_logging */
  PT_HINT_NO_HASH_LIST_SCAN = 0x0800,	/* 0000 1000 0000 0000 *//* no hash list scan */
  PT_HINT_QUERY_CACHE = 0x1000,	/* 0001 0000 0000 0000 *//* query_cache */
  PT_HINT_REEXECUTE = 0x2000,	/* 0010 0000 0000 0000 *//* reexecute */
  PT_HINT_JDBC_CACHE = 0x4000,	/* 0100 0000 0000 0000 *//* jdbc_cache */
  PT_HINT_USE_SBR = 0x8000,	/* 1000 0000 0000 0000 *//* statement based replication */
  PT_HINT_USE_IDX_DESC = 0x10000,	/* 0001 0000 0000 0000 0000 *//* descending index scan */
  PT_HINT_NO_COVERING_IDX = 0x20000,	/* 0010 0000 0000 0000 0000 *//* do not use covering index scan */
  PT_HINT_INSERT_MODE = 0x40000,	/* 0100 0000 0000 0000 0000 *//* set insert_executeion_mode */
  PT_HINT_NO_IDX_DESC = 0x80000,	/* 1000 0000 0000 0000 0000 *//* do not use descending index scan */
  PT_HINT_NO_MULTI_RANGE_OPT = 0x100000,	/* 0001 0000 0000 0000 0000 0000 */
  /* do not use multi range optimization */
  PT_HINT_USE_UPDATE_IDX = 0x200000,	/* 0010 0000 0000 0000 0000 0000 */
  /* use index for merge update */
  PT_HINT_USE_INSERT_IDX = 0x400000,	/* 0100 0000 0000 0000 0000 0000 */
  /* do not generate SORT-LIMIT plan */
  PT_HINT_NO_SORT_LIMIT = 0x800000,	/* 1000 0000 0000 0000 0000 0000 */
  PT_HINT_NO_HASH_AGGREGATE = 0x1000000,	/* 0001 0000 0000 0000 0000 0000 0000 */
  /* no hash aggregate evaluation */
  PT_HINT_SKIP_UPDATE_NULL = 0x2000000,	/* 0010 0000 0000 0000 0000 0000 0000 */
  PT_HINT_NO_INDEX_LS = 0x4000000,	/* 0100 0000 0000 0000 0000 0000 0000 *//* enable loose index scan */
  PT_HINT_INDEX_LS = 0x8000000,	/* 1000 0000 0000 0000 0000 0000 0000 *//* disable loose index scan */
  PT_HINT_QUERY_NO_CACHE = 0x10000000,	/* 0001 0000 0000 0000 0000 0000 0000 0000 *//* don't use the query cache */
  PT_HINT_SELECT_RECORD_INFO = 0x20000000,	/* 0010 0000 0000 0000 0000 0000 0000 0000 */
  /* SELECT record info from tuple header instead of data */
  PT_HINT_SELECT_PAGE_INFO = 0x40000000,	/* 0100 0000 0000 0000 0000 0000 0000 0000 */
  /* SELECT page header information from heap file instead of record data */
  PT_HINT_SELECT_KEY_INFO = 0x80000000	/* 1000 0000 0000 0000 0000 0000 0000 0000 */
    /* SELECT key information from index b-tree instead of table record data */
} PT_HINT_ENUM;

/* Codes for error messages */

typedef enum
{
  PT_NO_ERROR = 4000,
  PT_USAGE,
  PT_NODE_TABLE_OVERFLOW,
  PT_NAMES_TABLE_OVERFLOW,
  PT_CANT_OPEN_FILE,
  PT_STACK_OVERFLOW,
  PT_STACK_UNDERFLOW,
  PT_PARSE_ERROR,
  PT_ILLEGAL_TYPE_IN_FUNC,
  PT_NO_ARG_IN_FUNC
} PT_ERROR_CODE;

/* Codes for alter/add */

typedef enum
{
  PT_ADD_QUERY = 5000,
  PT_DROP_QUERY,
  PT_MODIFY_QUERY,
  PT_RESET_QUERY,
  PT_ADD_ATTR_MTHD,
  PT_DROP_ATTR_MTHD,
  PT_MODIFY_ATTR_MTHD,
  PT_RENAME_ATTR_MTHD,
  PT_MODIFY_DEFAULT,
  PT_ADD_SUPCLASS,
  PT_DROP_SUPCLASS,
  PT_DROP_RESOLUTION,
  PT_RENAME_RESOLUTION,
  PT_DROP_CONSTRAINT,
  PT_APPLY_PARTITION,
  PT_DROP_PARTITION,
  PT_REMOVE_PARTITION,
  PT_ADD_PARTITION,
  PT_ADD_HASHPARTITION,
  PT_REORG_PARTITION,
  PT_COALESCE_PARTITION,
  PT_ANALYZE_PARTITION,
  PT_PROMOTE_PARTITION,
  PT_RENAME_ENTITY,
  PT_ALTER_DEFAULT,
  PT_DROP_INDEX_CLAUSE,
  PT_DROP_PRIMARY_CLAUSE,
  PT_DROP_FK_CLAUSE,
  PT_CHANGE_ATTR,
  PT_CHANGE_AUTO_INCREMENT,
  PT_CHANGE_OWNER,
  PT_CHANGE_COLLATION,
#if defined (ENABLE_RENAME_CONSTRAINT)
  PT_RENAME_CONSTRAINT,
  PT_RENAME_INDEX,
#endif
  PT_REBUILD_INDEX,
  PT_ADD_INDEX_CLAUSE,
  PT_CHANGE_TABLE_COMMENT,
  PT_CHANGE_COLUMN_COMMENT,
  PT_CHANGE_INDEX_COMMENT,
  PT_CHANGE_INDEX_STATUS
} PT_ALTER_CODE;

/* Codes for trigger event type */

typedef enum
{
  PT_EV_INSERT = 6000,
  PT_EV_STMT_INSERT,
  PT_EV_DELETE,
  PT_EV_STMT_DELETE,
  PT_EV_UPDATE,
  PT_EV_STMT_UPDATE,
  PT_EV_ALTER,
  PT_EV_DROP,
  PT_EV_COMMIT,
  PT_EV_ROLLBACK,
  PT_EV_ABORT,
  PT_EV_TIMEOUT
} PT_EVENT_TYPE;

/* Codes for constraint types */

typedef enum
{
  PT_CONSTRAIN_UNKNOWN = 7000,
  PT_CONSTRAIN_PRIMARY_KEY,
  PT_CONSTRAIN_FOREIGN_KEY,
  PT_CONSTRAIN_NULL,
  PT_CONSTRAIN_NOT_NULL,
  PT_CONSTRAIN_UNIQUE,
  PT_CONSTRAIN_CHECK
} PT_CONSTRAINT_TYPE;

typedef enum
{
  PT_PARTITION_HASH = 0,
  PT_PARTITION_RANGE,
  PT_PARTITION_LIST
} PT_PARTITION_TYPE;

typedef enum
{
  PT_TABLE_OPTION_NONE = 0,
  PT_TABLE_OPTION_REUSE_OID = 9000,
  PT_TABLE_OPTION_AUTO_INCREMENT,
  PT_TABLE_OPTION_CHARSET,
  PT_TABLE_OPTION_COLLATION,
  PT_TABLE_OPTION_COMMENT,
  PT_TABLE_OPTION_ENCRYPT,
  PT_TABLE_OPTION_DONT_REUSE_OID
} PT_TABLE_OPTION_TYPE;

typedef enum
{
  PT_AND = 400, PT_OR, PT_NOT,
  PT_BETWEEN, PT_NOT_BETWEEN,
  PT_LIKE, PT_NOT_LIKE,
  PT_IS_IN, PT_IS_NOT_IN,
  PT_IS_NULL, PT_IS_NOT_NULL,
  PT_IS, PT_IS_NOT,
  PT_EXISTS,
  PT_EQ_SOME, PT_NE_SOME, PT_GE_SOME, PT_GT_SOME, PT_LT_SOME, PT_LE_SOME,
  PT_EQ_ALL, PT_NE_ALL, PT_GE_ALL, PT_GT_ALL, PT_LT_ALL, PT_LE_ALL,
  PT_EQ, PT_NE, PT_GE, PT_GT, PT_LT, PT_LE, PT_NULLSAFE_EQ,
  PT_GT_INF, PT_LT_INF,		/* internal use only */
  PT_SETEQ, PT_SETNEQ, PT_SUPERSETEQ, PT_SUPERSET, PT_SUBSET, PT_SUBSETEQ,
  PT_PLUS, PT_MINUS,
  PT_TIMES, PT_DIVIDE, PT_UNARY_MINUS, PT_PRIOR, PT_QPRIOR,
  PT_CONNECT_BY_ROOT,
  PT_BIT_NOT, PT_BIT_XOR, PT_BIT_AND, PT_BIT_OR, PT_BIT_COUNT,
  PT_BITSHIFT_LEFT, PT_BITSHIFT_RIGHT, PT_DIV, PT_MOD,
  PT_IF, PT_IFNULL, PT_ISNULL, PT_XOR,
  PT_ASSIGN,			/* as in x=y */
  PT_BETWEEN_AND,
  PT_BETWEEN_GE_LE, PT_BETWEEN_GE_LT, PT_BETWEEN_GT_LE, PT_BETWEEN_GT_LT,
  PT_BETWEEN_EQ_NA,
  PT_BETWEEN_INF_LE, PT_BETWEEN_INF_LT, PT_BETWEEN_GE_INF, PT_BETWEEN_GT_INF,
  PT_RANGE,			/* internal use only */
  PT_MODULUS, PT_RAND, PT_DRAND,
  PT_POSITION, PT_SUBSTRING, PT_OCTET_LENGTH, PT_BIT_LENGTH,
  PT_SUBSTRING_INDEX, PT_MD5, PT_SPACE,
  PT_CHAR_LENGTH, PT_LOWER, PT_UPPER, PT_TRIM,
  PT_LTRIM, PT_RTRIM, PT_LPAD, PT_RPAD, PT_REPLACE, PT_TRANSLATE,
  PT_REPEAT,
  PT_ADD_MONTHS, PT_LAST_DAY, PT_MONTHS_BETWEEN, PT_SYS_DATE,
  PT_TO_CHAR, PT_TO_DATE, PT_TO_NUMBER,
  PT_SYS_TIME, PT_SYS_TIMESTAMP, PT_CURRENT_TIMESTAMP, PT_SYS_DATETIME,
  PT_CURRENT_DATETIME, PT_CURRENT_TIME, PT_CURRENT_DATE, PT_UTC_TIME,
  PT_UTC_DATE, PT_TO_TIME, PT_TO_TIMESTAMP, PT_TO_DATETIME,
  PT_CURRENT_VALUE, PT_NEXT_VALUE,
  PT_INST_NUM, PT_ROWNUM, PT_ORDERBY_NUM,
  PT_CONNECT_BY_ISCYCLE, PT_CONNECT_BY_ISLEAF, PT_LEVEL,
  PT_SYS_CONNECT_BY_PATH,
  PT_EXTRACT,
  PT_LIKE_ESCAPE,
  PT_CAST,
  PT_CASE,
  PT_CURRENT_USER,
  PT_LOCAL_TRANSACTION_ID,

  PT_FLOOR, PT_CEIL, PT_SIGN, PT_POWER, PT_ROUND, PT_ABS, PT_TRUNC,
  PT_CHR, PT_INSTR, PT_LEAST, PT_GREATEST,
  PT_PATH_EXPR_SET,

  PT_ENCRYPT, PT_DECRYPT,

  PT_STRCAT, PT_NULLIF, PT_COALESCE, PT_NVL, PT_NVL2, PT_DECODE,
  PT_RANDOM, PT_DRANDOM,

  PT_INCR, PT_DECR,

  PT_LOG, PT_EXP, PT_SQRT,

  PT_CONCAT, PT_CONCAT_WS, PT_FIELD, PT_LEFT, PT_RIGHT,
  PT_LOCATE, PT_MID, PT_STRCMP, PT_REVERSE,

  PT_ACOS, PT_ASIN, PT_ATAN, PT_ATAN2,
  PT_COS, PT_SIN, PT_COT, PT_TAN,
  PT_DEGREES, PT_RADIANS,
  PT_PI,
  PT_FORMAT,
  PT_DISK_SIZE,
  PT_LN, PT_LOG2, PT_LOG10,
  PT_TIME_FORMAT,
  PT_TIMESTAMP,
  PT_UNIX_TIMESTAMP,
  PT_FROM_UNIXTIME,
  PT_SCHEMA,
  PT_DATABASE,
  PT_VERSION,
  /* datetime */
  PT_ADDDATE,			/* 2 fixed parameter */
  PT_DATE_ADD,			/* INTERVAL variant */
  PT_SUBDATE,
  PT_DATE_SUB,
  PT_DATE_FORMAT,
  PT_DATEF,
  PT_TIMEF, PT_YEARF, PT_MONTHF, PT_DAYF,
  PT_HOURF, PT_MINUTEF, PT_SECONDF,
  PT_DAYOFMONTH,
  PT_WEEKDAY,
  PT_DAYOFWEEK,
  PT_DAYOFYEAR,
  PT_QUARTERF,
  PT_TODAYS,
  PT_FROMDAYS,
  PT_TIMETOSEC,
  PT_SECTOTIME,
  PT_MAKEDATE,
  PT_MAKETIME,
  PT_WEEKF,
  PT_USER,
  PT_ROW_COUNT,
  PT_LAST_INSERT_ID,
  PT_DATEDIFF,
  PT_TIMEDIFF,
  PT_STR_TO_DATE,
  PT_DEFAULTF,
  PT_LIST_DBS,
  PT_OID_OF_DUPLICATE_KEY,
  PT_LIKE_LOWER_BOUND, PT_LIKE_UPPER_BOUND,

  PT_BIT_TO_BLOB, PT_BLOB_FROM_FILE, PT_BLOB_LENGTH, PT_BLOB_TO_BIT,
  PT_CHAR_TO_BLOB, PT_CHAR_TO_CLOB, PT_CLOB_FROM_FILE, PT_CLOB_LENGTH,
  PT_CLOB_TO_CHAR,
  PT_TYPEOF,
  PT_FUNCTION_HOLDER,		/* special operator : wrapper for PT_FUNCTION node */
  PT_INDEX_CARDINALITY,
  PT_DEFINE_VARIABLE,
  PT_EVALUATE_VARIABLE,
  PT_EXEC_STATS,
  PT_ADDTIME,
  PT_BIN,
  PT_FINDINSET,

  PT_HEX,
  PT_ASCII,
  PT_CONV,

  /* rlike operator */
  PT_RLIKE, PT_NOT_RLIKE, PT_RLIKE_BINARY, PT_NOT_RLIKE_BINARY,
  PT_TO_ENUMERATION_VALUE,

  /* inet */
  PT_INET_ATON, PT_INET_NTOA,

  PT_CHARSET, PT_COERCIBILITY, PT_COLLATION,

  /* width_bucket */
  PT_WIDTH_BUCKET,
  PT_TRACE_STATS,
  PT_INDEX_PREFIX,
  PT_AES_ENCRYPT,
  PT_AES_DECRYPT,
  PT_SHA_ONE,
  PT_SHA_TWO,
  PT_TO_BASE64,
  PT_FROM_BASE64,
  PT_SLEEP,

  PT_SYS_GUID,

  PT_DBTIMEZONE,
  PT_SESSIONTIMEZONE,
  PT_TZ_OFFSET,
  PT_NEW_TIME,
  PT_FROM_TZ,
  PT_TO_DATETIME_TZ,
  PT_TO_TIMESTAMP_TZ,
  PT_UTC_TIMESTAMP,
  PT_CRC32,
  PT_SCHEMA_DEF,
  PT_CONV_TZ,

  /* This is the last entry. Please add a new one before it. */
  PT_LAST_OPCODE
} PT_OP_TYPE;

/* the virtual query mechanism needs to put oid columns on non-updatable
 * virtual query guys, hence the "trust me" part.
 */
typedef enum
{
  PT_NO_OID_INCLUDED,
  PT_INCLUDE_OID,
  PT_INCLUDE_OID_TRUSTME
} PT_INCLUDE_OID_TYPE;

typedef enum
{
  PT_RANGE_MERGE,
  PT_RANGE_INTERSECTION,
  PT_REDUCE_COMP_PAIR_TERMS
} PT_COMP_TO_BETWEEN_OP_CODE_TYPE;

typedef enum
{
  PT_SYNTAX,
  PT_SEMANTIC,
  PT_EXECUTION
} PT_ERROR_TYPE;

typedef enum
{
  EXCLUDE_HIDDEN_COLUMNS,
  INCLUDE_HIDDEN_COLUMNS
} PT_INCLUDE_OR_EXCLUDE_HIDDEN_COLUMNS;

/* Flags for spec structures */
typedef enum
{
  PT_SPEC_FLAG_NONE = 0x0,	/* the spec will not be altered */
  PT_SPEC_FLAG_UPDATE = 0x01,	/* the spec will be updated */
  PT_SPEC_FLAG_DELETE = 0x02,	/* the spec will be deleted */
  PT_SPEC_FLAG_HAS_UNIQUE = 0x04,	/* the spec has unique */
  PT_SPEC_FLAG_FROM_VCLASS = 0x08,	/* applicable for derived tables, marks one as a rewritten view */
  PT_SPEC_FLAG_CONTAINS_OID = 0x10,	/* classoid and oid were added in the derived table's select list */
  PT_SPEC_FLAG_FOR_UPDATE_CLAUSE = 0x20,	/* Used with FOR UPDATE clause */
  PT_SPEC_FLAG_RECORD_INFO_SCAN = 0x40,	/* spec will be scanned for record information instead of record data */
  PT_SPEC_FLAG_PAGE_INFO_SCAN = 0x80,	/* spec's heap file will scanned page by page for page information. records
					 * will not be scanned. */
  PT_SPEC_FLAG_KEY_INFO_SCAN = 0x100,	/* one of the spec's indexes will be scanned for key information. */
  PT_SPEC_FLAG_BTREE_NODE_INFO_SCAN = 0x200,	/* one of the spec's indexes will be scanned for b-tree node info */
  PT_SPEC_FLAG_MVCC_COND_REEV = 0x400,	/* the spec is used in mvcc condition reevaluation */
  PT_SPEC_FLAG_MVCC_ASSIGN_REEV = 0x800,	/* the spec is used in UPDATE assignment reevaluation */
  PT_SPEC_FLAG_DOESNT_HAVE_UNIQUE = 0x1000	/* the spec was checked and does not have any uniques */
} PT_SPEC_FLAG;

typedef enum
{
  CONNECT_BY_CYCLES_ERROR = 0,
  CONNECT_BY_CYCLES_NONE,
  CONNECT_BY_CYCLES_IGNORE,
  CONNECT_BY_CYCLES_NONE_IGNORE
} PT_CONNECT_BY_CHECK_CYCLES;

/* Enum used for insert statements. After checking if insert is allowed on
 * server, the result is saved to avoid the same check again.
 */
typedef enum
{
  SERVER_INSERT_NOT_CHECKED = 0,
  SERVER_INSERT_IS_ALLOWED = 1,
  SERVER_INSERT_IS_NOT_ALLOWED = -1
} SERVER_INSERT_ALLOWED;

/*
 * Type definitions
 */

typedef struct parser_varchar PARSER_VARCHAR;

typedef struct parser_context PARSER_CONTEXT;

typedef struct parser_node PT_NODE;
typedef struct pt_alter_info PT_ALTER_INFO;
typedef struct pt_alter_user_info PT_ALTER_USER_INFO;
typedef struct pt_alter_trigger_info PT_ALTER_TRIGGER_INFO;
typedef struct pt_attach_info PT_ATTACH_INFO;
typedef struct pt_attach_info PT_PREPARE_TO_COMMIT_INFO;
typedef struct pt_attr_def_info PT_ATTR_DEF_INFO;
typedef struct pt_attr_ordering_info PT_ATTR_ORDERING_INFO;
typedef struct pt_auth_cmd_info PT_AUTH_CMD_INFO;
typedef struct pt_commit_work_info PT_COMMIT_WORK_INFO;
typedef struct pt_create_entity_info PT_CREATE_ENTITY_INFO;
typedef struct pt_index_info PT_INDEX_INFO;
typedef struct pt_create_user_info PT_CREATE_USER_INFO;
typedef struct pt_create_trigger_info PT_CREATE_TRIGGER_INFO;
typedef struct pt_cte_info PT_CTE_INFO;
typedef struct pt_serial_info PT_SERIAL_INFO;
typedef struct pt_data_default_info PT_DATA_DEFAULT_INFO;
typedef struct pt_auto_increment_info PT_AUTO_INCREMENT_INFO;
typedef struct pt_partition_info PT_PARTITION_INFO;
typedef struct pt_parts_info PT_PARTS_INFO;
typedef struct pt_data_type_info PT_DATA_TYPE_INFO;
typedef struct pt_delete_info PT_DELETE_INFO;
typedef struct pt_dot_info PT_DOT_INFO;
typedef struct pt_drop_info PT_DROP_INFO;
typedef struct pt_drop_user_info PT_DROP_USER_INFO;
typedef struct pt_drop_trigger_info PT_DROP_TRIGGER_INFO;
typedef struct pt_drop_variable_info PT_DROP_VARIABLE_INFO;
typedef struct pt_drop_session_var_info PT_DROP_SESSION_VAR_INFO;
typedef struct pt_spec_info PT_SPEC_INFO;
typedef struct pt_evaluate_info PT_EVALUATE_INFO;
typedef struct pt_event_object_info PT_EVENT_OBJECT_INFO;
typedef struct pt_event_spec_info PT_EVENT_SPEC_INFO;
typedef struct pt_event_target_info PT_EVENT_TARGET_INFO;
typedef struct pt_execute_trigger_info PT_EXECUTE_TRIGGER_INFO;
typedef struct pt_expr_info PT_EXPR_INFO;
typedef struct pt_file_path_info PT_FILE_PATH_INFO;
typedef struct pt_function_info PT_FUNCTION_INFO;
typedef struct pt_get_opt_lvl_info PT_GET_OPT_LVL_INFO;
typedef struct pt_get_trigger_info PT_GET_TRIGGER_INFO;
typedef struct pt_get_xaction_info PT_GET_XACTION_INFO;
typedef struct pt_grant_info PT_GRANT_INFO;
typedef struct pt_host_var_info PT_HOST_VAR_INFO;
typedef struct pt_insert_info PT_INSERT_INFO;
typedef struct pt_isolation_lvl_info PT_ISOLATION_LVL_INFO;
typedef struct pt_merge_info PT_MERGE_INFO;
typedef struct pt_method_call_info PT_METHOD_CALL_INFO;
typedef struct pt_method_def_info PT_METHOD_DEF_INFO;
typedef struct pt_name_info PT_NAME_INFO;
typedef struct pt_named_arg_info PT_NAMED_ARG_INFO;
typedef struct pt_remove_trigger_info PT_REMOVE_TRIGGER_INFO;
typedef struct pt_rename_info PT_RENAME_INFO;
typedef struct pt_rename_trigger_info PT_RENAME_TRIGGER_INFO;
typedef struct pt_resolution_info PT_RESOLUTION_INFO;
typedef struct pt_revoke_info PT_REVOKE_INFO;
typedef struct pt_rollback_work_info PT_ROLLBACK_WORK_INFO;
typedef struct pt_union_info PT_UNION_INFO;
typedef struct pt_savepoint_info PT_SAVEPOINT_INFO;
typedef struct pt_scope_info PT_SCOPE_INFO;
typedef struct pt_select_info PT_SELECT_INFO;
typedef struct pt_query_info PT_QUERY_INFO;
typedef struct pt_set_opt_lvl_info PT_SET_OPT_LVL_INFO;
typedef struct pt_set_sys_params_info PT_SET_SYS_PARAMS_INFO;
typedef struct pt_set_xaction_info PT_SET_XACTION_INFO;
typedef struct pt_set_trigger_info PT_SET_TRIGGER_INFO;
typedef struct pt_showstmt_info PT_SHOWSTMT_INFO;
typedef struct pt_killstmt_info PT_KILLSTMT_INFO;
typedef struct pt_sort_spec_info PT_SORT_SPEC_INFO;
typedef struct pt_timeout_info PT_TIMEOUT_INFO;
typedef struct pt_trigger_action_info PT_TRIGGER_ACTION_INFO;
typedef struct pt_trigger_spec_list_info PT_TRIGGER_SPEC_LIST_INFO;
typedef struct pt_update_info PT_UPDATE_INFO;
typedef struct pt_update_stats_info PT_UPDATE_STATS_INFO;
typedef struct pt_get_stats_info PT_GET_STATS_INFO;
typedef struct pt_set_session_variable_info PT_SET_SESSION_VARIABLE_INFO;
#if defined (ENABLE_UNUSED_FUNCTION)
typedef struct pt_use_info PT_USE_INFO;
#endif
typedef struct pt_monetary_value PT_MONETARY;
typedef struct pt_enum_element_value PT_ENUM_ELEMENT;
typedef union pt_data_value PT_DATA_VALUE;
typedef struct pt_value_info PT_VALUE_INFO;
typedef struct PT_ZZ_ERROR_MSG_INFO PT_ZZ_ERROR_MSG_INFO;
typedef struct pt_foreign_key_info PT_FOREIGN_KEY_INFO;
typedef struct pt_constraint_info PT_CONSTRAINT_INFO;
typedef struct pt_pointer_info PT_POINTER_INFO;
typedef struct pt_stored_proc_info PT_STORED_PROC_INFO;
typedef struct pt_prepare_info PT_PREPARE_INFO;
typedef struct pt_execute_info PT_EXECUTE_INFO;
typedef struct pt_stored_proc_param_info PT_STORED_PROC_PARAM_INFO;
typedef struct pt_truncate_info PT_TRUNCATE_INFO;
typedef struct pt_do_info PT_DO_INFO;
typedef union pt_statement_info PT_STATEMENT_INFO;
typedef struct pt_node_list_info PT_NODE_LIST_INFO;
typedef struct pt_table_option_info PT_TABLE_OPTION_INFO;
typedef struct pt_check_option_info PT_CHECK_OPTION_INFO;

typedef struct pt_agg_check_info PT_AGG_CHECK_INFO;
typedef struct pt_agg_rewrite_info PT_AGG_REWRITE_INFO;
typedef struct pt_agg_find_info PT_AGG_FIND_INFO;
typedef struct pt_agg_name_info PT_AGG_NAME_INFO;
typedef struct pt_filter_index_info PT_FILTER_INDEX_INFO;
typedef struct pt_non_groupby_col_info PT_NON_GROUPBY_COL_INFO;

typedef struct pt_host_vars PT_HOST_VARS;
typedef struct cursor_id PT_CURSOR_ID;
typedef struct qfile_list_id PT_LIST_ID;
typedef struct nested_view_version_info NESTED_VIEW_VERSION_INFO;
typedef struct view_cache_info VIEW_CACHE_INFO;
typedef struct semantic_chk_info SEMANTIC_CHK_INFO;
typedef struct parser_hint PT_HINT;
typedef struct pt_set_names_info PT_SET_NAMES_INFO;
typedef struct pt_trace_info PT_TRACE_INFO;

typedef struct pt_tuple_value_info PT_TUPLE_VALUE_INFO;

typedef struct pt_with_clause_info PT_WITH_CLAUSE_INFO;

typedef struct pt_insert_value_info PT_INSERT_VALUE_INFO;

typedef struct pt_set_timezone_info PT_SET_TIMEZONE_INFO;

typedef struct pt_flat_spec_info PT_FLAT_SPEC_INFO;

typedef struct pt_json_table_info PT_JSON_TABLE_INFO;
typedef struct pt_json_table_node_info PT_JSON_TABLE_NODE_INFO;
typedef struct pt_json_table_column_info PT_JSON_TABLE_COLUMN_INFO;

typedef PT_NODE *(*PT_NODE_WALK_FUNCTION) (PARSER_CONTEXT * p, PT_NODE * tree, void *arg, int *continue_walk);

typedef PARSER_VARCHAR *(*PT_PRINT_VALUE_FUNC) (PARSER_CONTEXT * parser, const PT_NODE * val);

/* This is for loose reference to init node function vector */
typedef void (*PARSER_GENERIC_VOID_FUNCTION) ();

typedef struct must_be_filtering_info MUST_BE_FILTERING_INFO;

struct must_be_filtering_info
{
  UINTPTR first_spec_id;
  bool must_be_filtering;
  bool has_second_spec_id;
};

struct semantic_chk_info
{
  PT_NODE *top_node;		/* top_node_arg */
  PT_NODE *Oracle_outerjoin_spec;	/* Oracle style outer join check */
  int Oracle_outerjoin_attr_num;	/* Oracle style outer join check */
  int Oracle_outerjoin_subq_num;	/* Oracle style outer join check */
  int Oracle_outerjoin_path_num;	/* Oracle style outer join check */
  bool donot_fold;		/* false - off, true - on */
  bool system_class;		/* system class(es) is(are) referenced */
};

struct nested_view_version_info
{
  DB_OBJECT *class_object;	/* the nested view's class object */
  unsigned int virtual_cache_local_schema_id;
  unsigned int virtual_cache_global_schema_id;
  unsigned int virtual_cache_snapshot_version;
  NESTED_VIEW_VERSION_INFO *next;
};

struct view_cache_info
{
  PT_NODE *attrs;
  PT_NODE *vquery_for_query;
  PT_NODE *vquery_for_query_in_gdb;
  PT_NODE *vquery_for_update;
  PT_NODE *vquery_for_update_in_gdb;
  PT_NODE *vquery_for_partial_update;
  PT_NODE *inverted_vquery_for_update;
  PT_NODE *inverted_vquery_for_update_in_gdb;
  char **expressions;
  int number_of_attrs;
  DB_AUTH authorization;
  NESTED_VIEW_VERSION_INFO *nested_views;
};

struct parser_hint
{
  const char *tokens;
  PT_NODE *arg_list;
  PT_HINT_ENUM hint;
};

struct pt_alter_info
{
  PT_NODE *entity_name;		/* PT_NAME */
  PT_ALTER_CODE code;		/* value will be PT_ADD_ATTR, PT_DROP_ATTR ... */
  PT_MISC_TYPE entity_type;	/* PT_VCLASS, ... */
  struct
  {
    PT_NODE *sup_class_list;	/* PT_NAME */
    PT_NODE *resolution_list;	/* PT_RESOLUTION */
  } super;
  union
  {
    struct
    {
      PT_NODE *query;		/* PT_SELECT */
      PT_NODE *query_no_list;	/* PT_VALUE(list) */
      PT_NODE *attr_def_list;	/* to be filled in semantic check */
      PT_NODE *view_comment;	/* PT_VALUE */
    } query;
    struct
    {
      PT_NODE *attr_def_list;	/* PT_ATTR_DEF */
      PT_NODE *attr_old_name;	/* PT_NAME used for CHANGE <old> <new> */
      PT_NODE *attr_mthd_name_list;	/* PT_NAME(list) */
      PT_NODE *mthd_def_list;	/* PT_METHOD_DEF */
      PT_NODE *mthd_file_list;	/* PT_FILE_PATH */
      PT_NODE *mthd_name_list;	/* PT_NAME(list) */
      PT_MISC_TYPE attr_type;	/* PT_NORMAL_ATTR, PT_CLASS_ATTR */
    } attr_mthd;
    struct
    {
      PT_NODE *attr_name_list;	/* PT_NAME(list) */
      PT_NODE *data_default_list;	/* PT_DATA_DEFAULT(list) */
    } ch_attr_def;
    struct
    {
      PT_MISC_TYPE element_type;	/* PT_ATTRIBUTE, PT_METHOD */
      PT_MISC_TYPE meta;	/* PT_META, PT_NORMAL */
      PT_NODE *old_name;
      PT_NODE *new_name;
      PT_NODE *mthd_name;
      PT_MISC_TYPE mthd_type;	/* PT_META, PT_NORMAL */
    } rename;
    struct
    {
      PT_NODE *info;		/* PT_PARTITION_INFO */
      PT_NODE *name_list;	/* PT_NAME(list) */
      PT_NODE *parts;		/* PT_PARTS_INFO(list) */
      PT_NODE *size;		/* PT_VALUE */
    } partition;
    struct
    {
      int charset;		/* charset for PT_CHANGE_COLLATION If the alter statement contains a valid charset
				 * spec, it is saved into the corresponding member of the struct(charset) Otherwise,
				 * charset = -1. */
      int collation_id;		/* collation for PT_CHANGE_COLLATION If the alter statement contains a valid collation
				 * spec, it is saved into the corresponding member of the struct, e.g. collation_id.
				 * Otherwise, collation_id = -1. */
    } collation;
    struct
    {
      bool reverse;
      bool unique;
    } index;
    struct
    {
      PT_NODE *start_value;
    } auto_increment;
    struct
    {
      PT_NODE *user_name;	/* user name for PT_CHANGE_OWNER */
    } user;
    struct
    {
      PT_NODE *tbl_comment;	/* PT_VALUE, comment for table/view */
    } comment;
  } alter_clause;
  PT_NODE *constraint_list;	/* constraints from ADD and CHANGE clauses */
  PT_NODE *create_index;	/* PT_CREATE_INDEX from ALTER ADD INDEX */
  PT_NODE *internal_stmts;	/* internally created statements to handle TEXT */
  PT_HINT_ENUM hint;
};

/* ALTER USER INFO */
struct pt_alter_user_info
{
  PT_NODE *user_name;		/* PT_NAME */
  PT_NODE *password;		/* PT_VALUE (string) */
  PT_NODE *comment;		/* PT_VALUE */
};

/* Info for ALTER_TRIGGER */
struct pt_alter_trigger_info
{
  PT_NODE *trigger_spec_list;	/* PT_TRIGGER_SPEC_LIST */
  PT_NODE *trigger_priority;	/* PT_VALUE */
  PT_NODE *trigger_owner;	/* PT_NAME */
  PT_MISC_TYPE trigger_status;	/* ACTIVE, INACTIVE */
  PT_NODE *comment;		/* PT_VALUE */
};

/* Info for ATTACH & PREPARE TO COMMIT statements */
struct pt_attach_info
{
  int trans_id;			/* transaction id */
};

/* Info for ATTR_DEF */
struct pt_attr_def_info
{
  PT_NODE *attr_name;		/* PT_NAME */
  PT_NODE *data_default;	/* PT_DATA_DEFAULT */
  DB_DEFAULT_EXPR_TYPE on_update;
  PT_NODE *auto_increment;	/* PT_AUTO_INCREMENT */
  PT_NODE *ordering_info;	/* PT_ATTR_ORDERING */
  PT_NODE *comment;		/* PT_VALUE */
  PT_MISC_TYPE attr_type;	/* PT_NORMAL or PT_META */
  int size_constraint;		/* max length of STRING */
  short constrain_not_null;
};

/* Info for ALTER TABLE ADD COLUMN [FIRST | AFTER column_name ] */
struct pt_attr_ordering_info
{
  PT_NODE *after;		/* PT_NAME */
  bool first;
};

/* Info for AUTH_CMD */
struct pt_auth_cmd_info
{
  PT_NODE *attr_mthd_list;	/* PT_NAME (list of attr names) */
  PT_PRIV_TYPE auth_cmd;	/* enum PT_SELECT_PRIV, PT_ALL_PRIV,... */
};

/* Info COMMIT WORK  */
struct pt_commit_work_info
{
  unsigned retain_lock:1;	/* 0 = false, 1 = true */
};

typedef enum
{
  PT_CREATE_SELECT_NO_ACTION,
  PT_CREATE_SELECT_REPLACE,
  PT_CREATE_SELECT_IGNORE
} PT_CREATE_SELECT_ACTION;

/* Info for a CREATE ENTITY node */
struct pt_create_entity_info
{
  PT_MISC_TYPE entity_type;	/* enum PT_CLASS, PT_VCLASS .. */
  PT_MISC_TYPE with_check_option;	/* 0, PT_LOCAL, or PT_CASCADED */
  PT_NODE *entity_name;		/* PT_NAME */
  PT_NODE *supclass_list;	/* PT_NAME (list) */
  PT_NODE *class_attr_def_list;	/* PT_ATTR_DEF (list) */
  PT_NODE *attr_def_list;	/* PT_ATTR_DEF (list) */
  PT_NODE *table_option_list;	/* PT_TABLE_OPTION (list) */
  PT_NODE *method_def_list;	/* PT_ATTR_DEF (list) */
  PT_NODE *method_file_list;	/* PT_FILE_PATH (list) */
  PT_NODE *resolution_list;	/* PT_RESOLUTION */
  PT_NODE *as_query_list;	/* PT_SELECT (list) */
  PT_NODE *object_id_list;	/* PT_NAME list */
  PT_NODE *update;		/* PT_EXPR (list ) */
  PT_NODE *constraint_list;	/* PT_CONSTRAINT (list) */
  PT_NODE *create_index;	/* PT_CREATE_INDEX */
  PT_NODE *partition_info;	/* PT_PARTITION_INFO */
  PT_NODE *internal_stmts;	/* internally created statements to handle TEXT */
  PT_NODE *create_like;		/* PT_NAME */
  PT_NODE *create_select;	/* PT_SELECT or another type of select_expression */
  PT_NODE *vclass_comment;	/* PT_VALUE, comment of vclass, see also: table_option_list for comment of class */
  PT_CREATE_SELECT_ACTION create_select_action;	/* nothing | REPLACE | IGNORE for CREATE SELECT */
  unsigned or_replace:1;	/* OR REPLACE clause for create view */
  unsigned if_not_exists:1;	/* IF NOT EXISTS clause for create table | class */
};

/* CREATE/DROP INDEX INFO */
struct pt_index_info
{
  PT_NODE *indexed_class;	/* PT_SPEC */
  PT_NODE *column_names;	/* PT_SORT_SPEC (list) */
  PT_NODE *index_name;		/* PT_NAME */
#if defined (ENABLE_RENAME_CONSTRAINT)
  PT_NODE *new_name;		/* PT_NAME */
#endif
  PT_NODE *prefix_length;	/* PT_NAME */
  PT_NODE *where;		/* PT_EXPR */
  PT_NODE *function_expr;	/* PT_EXPR - expression to be used in a function index */
  PT_NODE *comment;		/* PT_VALUE */
  PT_ALTER_CODE code;

  int func_pos;			/* the position of the expression in the function index's column list */
  int func_no_args;		/* number of arguments in the function index expression */
  bool reverse;			/* REVERSE */
  bool unique;			/* UNIQUE specified? */
  SM_INDEX_STATUS index_status;	/* Index status : NORMAL / ONLINE / INVISIBLE */
  int ib_threads;
};

/* CREATE USER INFO */
struct pt_create_user_info
{
  PT_NODE *user_name;		/* PT_NAME */
  PT_NODE *password;		/* PT_VALUE (string) */
  PT_NODE *groups;		/* PT_NAME list */
  PT_NODE *members;		/* PT_NAME list */
  PT_NODE *comment;		/* PT_VALUE */
};

/* CREATE TRIGGER INFO */
struct pt_create_trigger_info
{
  PT_NODE *trigger_name;	/* PT_NAME */
  PT_NODE *trigger_priority;	/* PT_VALUE */
  PT_MISC_TYPE trigger_status;	/* ACTIVE, INACTIVE */
  PT_MISC_TYPE condition_time;	/* BEFORE, AFTER, DEFERRED */
  PT_NODE *trigger_event;	/* PT_EVENT_SPEC */
  PT_NODE *trigger_reference;	/* PT_EVENT_OBJECT (list) */
  PT_NODE *trigger_condition;	/* PT_EXPR or PT_METHOD_CALL */
  PT_NODE *trigger_action;	/* PT_TRIGGER_ACTION */
  PT_MISC_TYPE action_time;	/* BEFORE, AFTER, DEFERRED */
  PT_NODE *comment;		/* PT_VALUE */
};

/* CTE(Common Table Expression) INFO */
struct pt_cte_info
{
  PT_NODE *name;		/* PT_NAME */
  PT_NODE *as_attr_list;	/* PT_NAME */
  PT_NODE *non_recursive_part;	/* the non-recursive subquery */
  PT_NODE *recursive_part;	/* a recursive subquery */
  PT_MISC_TYPE only_all;	/* Type of UNION between non-recursive and recursive parts */
  void *xasl;			/* xasl proc pointer */
};

/* CREATE SERIAL INFO */
struct pt_serial_info
{
  PT_NODE *serial_name;		/* PT_NAME */
  PT_NODE *start_val;		/* PT_VALUE */
  PT_NODE *increment_val;	/* PT_VALUE */
  PT_NODE *max_val;		/* PT_VALUE */
  PT_NODE *min_val;		/* PT_VALUE */
  PT_NODE *cached_num_val;	/* PT_VALUE */
  PT_NODE *comment;		/* PT_VALUE */
  int cyclic;
  int no_max;
  int no_min;
  int no_cyclic;
  int no_cache;
  unsigned if_exists:1;		/* IF EXISTS clause for drop serial */
};

/* Info for DATA_DEFAULT */
struct pt_data_default_info
{
  PT_NODE *default_value;	/* PT_VALUE (list) */
  PT_MISC_TYPE shared;		/* will PT_SHARED or PT_DEFAULT */
  DB_DEFAULT_EXPR_TYPE default_expr_type;	/* if it is a pseudocolumn, do not evaluate expr */
};

/* Info for the AUTO_INCREMENT node */
struct pt_auto_increment_info
{
  PT_NODE *start_val;		/* PT_VALUE */
  PT_NODE *increment_val;	/* PT_VALUE */
};

/* Info for the PARTITION node */
struct pt_partition_info
{
  PT_NODE *expr;
  PT_NODE *keycol;
  PT_NODE *hashsize;
  PT_NODE *parts;		/* PT_PARTS_INFO list */
  PT_PARTITION_TYPE type;
};

struct pt_parts_info
{
  PT_NODE *name;		/* PT_NAME */
  PT_NODE *values;		/* PT_VALUE (or list) */
  PT_PARTITION_TYPE type;
  PT_NODE *comment;		/* PT_VALUE */
};
#define PARTITIONED_SUB_CLASS_TAG "__p__"

/* Info for DATA_TYPE node */
struct pt_data_type_info
{
  PT_NODE *entity;		/* class PT_NAME list for PT_TYPE_OBJECT */
  PT_NODE *enumeration;		/* values list for PT_TYPE_ENUMERATION */
  DB_OBJECT *virt_object;	/* virt class object if a vclass */
  PT_NODE *virt_data_type;	/* for non-primitive types- sets, etc. */
  PT_TYPE_ENUM virt_type_enum;	/* type enumeration tage PT_TYPE_??? */
  int precision;		/* for float and int, length of char */
  int dec_precision;		/* decimal precision for float */
  int units;			/* for money (or string's codeset) */
  int collation_id;		/* collation identifier (strings) */
  /* how the collation should be taken into account */
  TP_DOMAIN_COLL_ACTION collation_flag;
  bool has_coll_spec;		/* this is used only when defining collatable types: true if collation was explicitly
				 * set, false otherwise (collation defaulted to that of the system) */
  bool has_cs_spec;		/* this is used only when defining collatable types: true if charset was explicitly
				 * set, false otherwise (charset defaulted to that of the system) */
  PT_MISC_TYPE inout;		/* input or output method parameter */
  PARSER_VARCHAR *json_schema;
};

/* DELETE */
struct pt_delete_info
{
  PT_NODE *target_classes;	/* PT_NAME */
  PT_NODE *spec;		/* PT_SPEC (list) */
  PT_NODE *class_specs;		/* PT_SPEC list */
  PT_NODE *search_cond;		/* PT_EXPR */
  PT_NODE *using_index;		/* PT_NAME (list) */
  PT_NODE *cursor_name;		/* PT_NAME */
  PT_NODE *internal_stmts;	/* internally created statements to handle TEXT */
  PT_NODE *waitsecs_hint;	/* lock timeout in seconds */
  PT_NODE *ordered_hint;	/* ORDERED_HINT hint's arguments (PT_NAME list) */
  PT_NODE *use_nl_hint;		/* USE_NL hint's arguments (PT_NAME list) */
  PT_NODE *use_idx_hint;	/* USE_IDX hint's arguments (PT_NAME list) */
  PT_NODE *use_merge_hint;	/* USE_MERGE hint's arguments (PT_NAME list) */
  PT_NODE *limit;		/* PT_VALUE limit clause parameter */
  PT_NODE *del_stmt_list;	/* list of DELETE statements after split */
  PT_HINT_ENUM hint;		/* hint flag */
  PT_NODE *with;		/* PT_WITH_CLAUSE */
  unsigned has_trigger:1;	/* whether it has triggers */
  unsigned server_delete:1;	/* whether it can be server-side deletion */
  unsigned rewrite_limit:1;	/* need to rewrite the limit clause */
  unsigned execute_with_commit_allowed:1;	/* true, if execute with commit allowed. */
};

/* DOT_INFO*/
struct pt_dot_info
{
  PT_NODE *arg1;		/* PT_EXPR etc.  first argument */
  PT_NODE *arg2;		/* PT_EXPR etc.  possible second argument */
  PT_NODE *selector;		/* only set if selector used A[SELECTOR].B */
  short tag_click_counter;	/* 0: normal name, 1: click counter name */
  int coll_modifier;		/* collation modifier = collation + 1 */
};

/* DROP ENTITY
  as in DROP VCLASS X,Y,Z; (different from ALTER .... or DROP VIEW )
 */
struct pt_drop_info
{
  PT_NODE *spec_list;		/* PT_SPEC (list) */
  PT_NODE *internal_stmts;	/* internally created statements to handle TEXT */
  PT_MISC_TYPE entity_type;	/* PT_VCLASS, PT_CLASS */
  bool if_exists;		/* IF EXISTS clause for DROP TABLE */
  bool is_cascade_constraints;	/* whether to drop cascade FK key */
};

/* DROP USER INFO */
struct pt_drop_user_info
{
  PT_NODE *user_name;		/* PT_NAME */
};

/* DROP TRIGGER */
struct pt_drop_trigger_info
{
  PT_NODE *trigger_spec_list;	/* PT_TRIGGER_SPEC_LIST */
};

/* DROP VARIABLE */
struct pt_drop_variable_info
{
  PT_NODE *var_names;		/* PT_NAME (list) */
};

struct pt_drop_session_var_info
{
  PT_NODE *variables;		/* PT_VALUE list */
};

/* Info for a ENTITY spec and spec_list */
struct pt_spec_info
{
  PT_NODE *entity_name;		/* PT_NAME */
  PT_NODE *cte_name;		/* PT_NAME */
  PT_NODE *cte_pointer;		/* PT_POINTER - points to the cte_definition */
  PT_NODE *except_list;		/* PT_SPEC */
  PT_NODE *derived_table;	/* a subquery */
  PT_NODE *range_var;		/* PT_NAME */
  PT_NODE *as_attr_list;	/* PT_NAME */
  PT_NODE *referenced_attrs;	/* PT_NAME list of referenced attrs */
  PT_NODE *path_entities;	/* PT_SPECs implied by path expr's */
  PT_NODE *path_conjuncts;	/* PT_EXPR boolean nodes */
  PT_NODE *flat_entity_list;	/* PT_NAME (list) resolved class's */
  PT_NODE *method_list;		/* PT_METHOD_CALL list with this entity as the target */
  PT_NODE *partition;		/* PT_NAME of the specified partition */
  PT_NODE *json_table;		/* JSON TABLE definition tree */
  UINTPTR id;			/* entity spec unique id # */
  PT_MISC_TYPE only_all;	/* PT_ONLY or PT_ALL */
  PT_MISC_TYPE meta_class;	/* enum 0 or PT_META */
  PT_MISC_TYPE derived_table_type;	/* PT_IS_SUBQUERY, PT_IS_SET_EXPR, or PT_IS_CSELECT, PT_IS_SHOWSTMT */
  PT_MISC_TYPE flavor;		/* enum 0 or PT_METHOD_ENTITY */
  PT_NODE *on_cond;
  PT_NODE *using_cond;		/* -- does not support named columns join */
  PT_JOIN_TYPE join_type;
  short location;		/* n-th position in FROM (start from 0); init val = -1 */
  bool natural;			/* -- does not support natural join */
  DB_AUTH auth_bypass_mask;	/* flag to bypass normal authorization : used only by SHOW statements currently */
  PT_SPEC_FLAG flag;		/* flag wich marks this spec for DELETE or UPDATE operations */
};

/* Info for an EVALUATE object */
struct pt_evaluate_info
{
  PT_NODE *expression;		/* PT_EXPR or PT_METHOD_CALL */
  PT_NODE *into_var;		/* PT_VALUE */
};

/* Info for an EVENT object */
struct pt_event_object_info
{
  PT_NODE *event_object;	/* PT_NAME: current, new, old */
  PT_NODE *correlation_name;	/* PT_NAME */
};

/* Info for an EVENT spec */
struct pt_event_spec_info
{
  PT_NODE *event_target;	/* PT_EVENT_TARGET */
  PT_EVENT_TYPE event_type;
};

/* Info for an EVENT target */
struct pt_event_target_info
{
  PT_NODE *class_name;		/* PT_NAME */
  PT_NODE *attribute;		/* PT_NAME or NULL */
};

/* EXECUTE TRIGGER */
struct pt_execute_trigger_info
{
  PT_NODE *trigger_spec_list;	/* PT_TRIGGER_SPEC_LIST */
};

/* Info for Expressions
   This includes binary and unary operations + * - etc
 */
struct pt_expr_info
{
  PT_NODE *arg1;		/* PT_EXPR etc.  first argument */
  PT_NODE *arg2;		/* PT_EXPR etc.  possible second argument */
  PT_NODE *value;		/* only set if we evaluate it */
  PT_OP_TYPE op;		/* binary or unary op code */
  int paren_type;		/* 0 - none, else - () */
  PT_NODE *arg3;		/* possible third argument (like, between, or case) */
  PT_NODE *cast_type;		/* PT_DATA_TYPE, resultant cast domain */
  PT_MISC_TYPE qualifier;	/* trim qualifier (LEADING, TRAILING, BOTH), datetime extract field specifier (YEAR,
				 * ..., SECOND), or case expr type specifier (NULLIF, COALESCE, SIMPLE_CASE,
				 * SEARCHED_CASE) */
#define PT_EXPR_INFO_CNF_DONE       1	/* CNF conversion has done? */
#define PT_EXPR_INFO_EMPTY_RANGE    2	/* empty RANGE spec? */
#define PT_EXPR_INFO_INSTNUM_C      4	/* compatible with inst_num() */
#define PT_EXPR_INFO_INSTNUM_NC     8	/* not compatible with inst_num() */
#define PT_EXPR_INFO_GROUPBYNUM_C  16	/* compatible with groupby_num() */
#define PT_EXPR_INFO_GROUPBYNUM_NC 32	/* not compatible with groupby_num() */
#define PT_EXPR_INFO_ORDERBYNUM_C \
               PT_EXPR_INFO_INSTNUM_C	/* compatible with orderby_num() */
#define PT_EXPR_INFO_ORDERBYNUM_NC \
              PT_EXPR_INFO_INSTNUM_NC	/* not compatible with orderby_num() */
#define PT_EXPR_INFO_TRANSITIVE    64	/* always true transitive join term ? */
#define PT_EXPR_INFO_LEFT_OUTER   128	/* Oracle's left outer join operator */
#define PT_EXPR_INFO_RIGHT_OUTER  256	/* Oracle's right outer join operator */
#define PT_EXPR_INFO_COPYPUSH     512	/* term which is copy-pushed into the derived subquery ? is removed at the last
					 * rewrite stage of query optimizer */
#if 1				/* unused anymore - DO NOT DELETE ME */
#define PT_EXPR_INFO_FULL_RANGE  1024	/* non-null full RANGE term ? */
#endif
#define	PT_EXPR_INFO_CAST_NOFAIL 2048	/* flag for non failing cast operation; at runtime will return null DB_VALUE
					 * instead of failing */
#define PT_EXPR_INFO_CAST_SHOULD_FOLD 4096	/* flag which controls if a cast expr should be folded */

#define PT_EXPR_INFO_FUNCTION_INDEX 8192	/* function index expression flag */

#define PT_EXPR_INFO_CAST_COLL_MODIFIER 16384	/* CAST is for COLLATION modifier */

#define PT_EXPR_INFO_GROUPBYNUM_LIMIT 32768	/* flag that marks if the expression resulted from a GROUP BY ... LIMIT
						 * statement */
#define PT_EXPR_INFO_DO_NOT_AUTOPARAM 65536	/* don't auto parameterize expr at qo_do_auto_parameterize() */
#define PT_EXPR_INFO_CAST_WRAP 	131072	/* 0x20000, CAST is wrapped by compiling */
  int flag;			/* flags */
#define PT_EXPR_INFO_IS_FLAGED(e, f)    ((e)->info.expr.flag & (int) (f))
#define PT_EXPR_INFO_SET_FLAG(e, f)     (e)->info.expr.flag |= (int) (f)
#define PT_EXPR_INFO_CLEAR_FLAG(e, f)   (e)->info.expr.flag &= (int) ~(f)

  short continued_case;		/* 0 - false, 1 - true */
  short location;		/* 0 : WHERE; n : join condition of n-th FROM */
  bool is_order_dependent;	/* true if expression is order dependent */
  PT_TYPE_ENUM recursive_type;	/* common type for recursive expression arguments (like PT_GREATEST, PT_LEAST,...) */
  int coll_modifier;		/* collation modifier = collation + 1 */
};

/* FILE PATH INFO */
struct pt_file_path_info
{
  PT_NODE *string;		/* PT_VALUE: a C or ANSI string */
};

/* FUNCTIONS ( COUNT, AVG, ....)  */
struct pt_function_info
{
  PT_NODE *arg_list;		/* PT_EXPR(list) */
  FUNC_TYPE function_type;	/* PT_COUNT, PT_AVG, ... */
  PT_MISC_TYPE all_or_distinct;	/* will be PT_ALL or PT_DISTINCT */
  const char *generic_name;	/* only for type PT_GENERIC */
  char hidden_column;		/* used for updates and deletes for the class OID column */
  PT_NODE *order_by;		/* ordering PT_SORT_SPEC for GROUP_CONCAT */
  PT_NODE *percentile;		/* percentile for PERCENTILE_CONT, PERCENTILE_DISC */
  bool is_order_dependent;	/* true if function is order dependent */
  bool is_type_checked;		/* true if type is already checked, false otherwise... is this safe? */
  int coll_modifier;		/* collation modifier = collation + 1 */
  struct
  {
    PT_NODE *partition_by;	/* partition PT_SORT_SPEC list */
    PT_NODE *order_by;		/* ordering PT_SORT_SPEC list */
    PT_NODE *default_value;	/* LEAD/LAG function default value */
    PT_NODE *offset;		/* LEAD/LAG/NTH_VALUE function offset */
    PT_NODE *expanded_list;	/* reserved list when expand partition_by/order_by */
    bool adjusted;		/* whether the partition_by/order_by be adjusted and expanded */
    bool from_last;		/* determines whether the calculation begins at the last or first row */
    bool ignore_nulls;		/* determines whether the calculation eliminate or includes null values */
    bool is_analytic;		/* is analytic clause */
  } analytic;
};

/* Info for Get Optimization Level statement */
struct pt_get_opt_lvl_info
{
  PT_NODE *args;
  PT_NODE *into_var;		/* PT_VALUE */
  PT_MISC_TYPE option;		/* PT_OPT_LVL, PT_OPT_COST */
};

/* Info for Get Trigger statement */
struct pt_get_trigger_info
{
  PT_NODE *into_var;		/* PT_VALUE */
  PT_MISC_TYPE option;		/* PT_TRIGGER_DEPTH, PT_TRIGGER_TRACE */
};

/* Info for Get Transaction statement */
struct pt_get_xaction_info
{
  PT_NODE *into_var;		/* PT_VALUE */
  PT_MISC_TYPE option;		/* PT_ISOLATION_LEVEL or PT_LOCK_TIMEOUT */
};

/* GRANT INFO */
struct pt_grant_info
{
  PT_NODE *auth_cmd_list;	/* PT_AUTH_CMD(list) */
  PT_NODE *user_list;		/* PT_NAME */
  PT_NODE *spec_list;		/* PT_SPEC */
  PT_MISC_TYPE grant_option;	/* = PT_GRANT_OPTION or PT_NO_GRANT_OPTION */
};

/* Info for Host_Var */
struct pt_host_var_info
{
  const char *str;		/* ??? */
  PT_MISC_TYPE var_type;	/* PT_HOST_IN, PT_HOST_OUT, */
  int index;			/* for PT_HOST_VAR ordering */
};

/* Info for lists of PT_NODE */
struct pt_node_list_info
{
  PT_MISC_TYPE list_type;	/* e.g. PT_IS_VALUE */
  PT_NODE *list;		/* the list of nodes */
};

/* Info for Insert */
struct pt_insert_info
{
  PT_NODE *spec;		/* PT_SPEC */
  PT_NODE *class_specs;		/* PT_SPEC list */
  PT_NODE *attr_list;		/* PT_NAME */
  PT_NODE *value_clauses;	/* PT_NODE_LIST(list) or PT_NODE_LIST(PT_SELECT) */
  PT_NODE *into_var;		/* PT_VALUE */
  PT_MISC_TYPE is_subinsert;	/* 0 or PT_IS_SUBINSERT(for printing) */
  PT_NODE *where;		/* for view with check option checking */
  PT_NODE *internal_stmts;	/* internally created statements to handle TEXT */
  PT_NODE *waitsecs_hint;	/* lock timeout in seconds */
  PT_HINT_ENUM hint;		/* hint flag */
  PT_NODE *odku_assignments;	/* ON DUPLICATE KEY UPDATE assignments */
  bool do_replace;		/* REPLACE statement was given */
  PT_NODE *insert_mode;		/* insert execution mode */
  PT_NODE *non_null_attrs;	/* attributes with not null constraint */
  PT_NODE *odku_non_null_attrs;	/* attributes with not null constraint in odku assignments */
  int has_uniques;		/* class has unique constraints */
  SERVER_INSERT_ALLOWED server_allowed;	/* is insert allowed on server */
  unsigned execute_with_commit_allowed:1;	/* true, if execute with commit allowed. */
};

/* Info for Transaction Isolation Level */
struct pt_isolation_lvl_info
{
  PT_MISC_TYPE schema;
  PT_MISC_TYPE instances;
  PT_NODE *level;		/* PT_VALUE */
  unsigned async_ws:1;		/* 0 = false, 1 = true */
};

/* Info for Method Call */
struct pt_method_call_info
{
  PT_NODE *method_name;		/* PT_NAME or PT_METHOD_DEF */
  PT_NODE *arg_list;		/* PT_EXPR (list ) */
  PT_NODE *on_call_target;	/* PT_NAME */
  PT_NODE *to_return_var;	/* PT_NAME */
  PT_MISC_TYPE call_or_expr;	/* PT_IS_CALL_STMT or PT_IS_MTHD_EXPR */
  PT_MISC_TYPE class_or_inst;	/* PT_IS_CLASS_MTHD or PT_IS_INST_MTHD */
  UINTPTR method_id;		/* unique identifier so when copying we know if two methods are copies of the same
				 * original method call. */
};

/* Info for METHOD DEFs */
struct pt_method_def_info
{
  PT_NODE *method_name;		/* PT_NAME */
  PT_NODE *method_args_list;	/* PT_DATA_TYPE (list) */
  PT_NODE *function_name;	/* PT_VALUE (string) */
  PT_MISC_TYPE mthd_type;	/* PT_NORMAL or ... */
};

/*
 * Reserved names section
 */

/* Enumeration of reserved names categories */
typedef enum
{
  RESERVED_NAME_INVALID = -1,
  RESERVED_NAME_RECORD_INFO = 0,
  RESERVED_NAME_PAGE_INFO,
  RESERVED_NAME_KEY_INFO,
  RESERVED_NAME_BTREE_NODE_INFO
} PT_RESERVED_NAME_TYPE;

/* Enumeration of reserved name ids */
typedef enum
{
  /* Reserved record info names */
  RESERVED_T_PAGEID = 0,
  RESERVED_T_SLOTID,
  RESERVED_T_VOLUMEID,
  RESERVED_T_OFFSET,
  RESERVED_T_LENGTH,
  RESERVED_T_REC_TYPE,
  RESERVED_T_REPRID,
  RESERVED_T_CHN,
  /* leave MVCC attributes at the end of record information */
  RESERVED_T_MVCC_INSID,
  RESERVED_T_MVCC_DELID,
  RESERVED_T_MVCC_FLAGS,
  RESERVED_T_MVCC_PREV_VERSION_LSA,

  /* Reserved page info names */
  RESERVED_P_CLASS_OID,
  RESERVED_P_PREV_PAGEID,
  RESERVED_P_NEXT_PAGEID,
  RESERVED_P_NUM_SLOTS,
  RESERVED_P_NUM_RECORDS,
  RESERVED_P_ANCHOR_TYPE,
  RESERVED_P_ALIGNMENT,
  RESERVED_P_TOTAL_FREE,
  RESERVED_P_CONT_FREE,
  RESERVED_P_OFFSET_TO_FREE_AREA,
  RESERVED_P_IS_SAVING,
  RESERVED_P_UPDATE_BEST,

  /* Reserved key info names */
  RESERVED_KEY_VOLUMEID,
  RESERVED_KEY_PAGEID,
  RESERVED_KEY_SLOTID,
  RESERVED_KEY_KEY,
  RESERVED_KEY_OID_COUNT,
  RESERVED_KEY_FIRST_OID,
  RESERVED_KEY_OVERFLOW_KEY,
  RESERVED_KEY_OVERFLOW_OIDS,

  /* Reserved b-tree node info names */
  RESERVED_BT_NODE_VOLUMEID,
  RESERVED_BT_NODE_PAGEID,
  RESERVED_BT_NODE_TYPE,
  RESERVED_BT_NODE_KEY_COUNT,
  RESERVED_BT_NODE_FIRST_KEY,
  RESERVED_BT_NODE_LAST_KEY,

  /* leave this last to know how many reserved names are in pt_Reserved_name_table */
  RESERVED_ATTR_COUNT,

  /* make sure you update these values when adding or removing items */
  RESERVED_FIRST_RECORD_INFO = RESERVED_T_PAGEID,
  RESERVED_FIRST_MVCC_INFO = RESERVED_T_MVCC_INSID,
  RESERVED_LAST_RECORD_INFO = RESERVED_T_MVCC_PREV_VERSION_LSA,

  RESERVED_FIRST_PAGE_INFO = RESERVED_P_CLASS_OID,
  RESERVED_LAST_PAGE_INFO = RESERVED_P_UPDATE_BEST,

  RESERVED_FIRST_KEY_INFO = RESERVED_KEY_VOLUMEID,
  RESERVED_LAST_KEY_INFO = RESERVED_KEY_OVERFLOW_OIDS,

  RESERVED_FIRST_BT_NODE_INFO = RESERVED_BT_NODE_VOLUMEID,
  RESERVED_LAST_BT_NODE_INFO = RESERVED_BT_NODE_LAST_KEY
} PT_RESERVED_NAME_ID;

/* Reserved name info */
typedef struct pt_reserved_name PT_RESERVED_NAME;
struct pt_reserved_name
{
  const char *name;
  PT_RESERVED_NAME_ID id;
  DB_TYPE type;
};

/* Global reserved name table which stores info for each name */
extern PT_RESERVED_NAME pt_Reserved_name_table[];

/* Obtain reserved name type from id */
#define PT_GET_RESERVED_NAME_TYPE(reserved_id)		      \
  (((reserved_id) >= RESERVED_FIRST_RECORD_INFO		      \
    && (reserved_id) <= RESERVED_LAST_RECORD_INFO) ?	      \
   /* If reserved_id belongs to record info */		      \
   RESERVED_NAME_RECORD_INFO :				      \
   /* else */						      \
   (((reserved_id) >= RESERVED_FIRST_PAGE_INFO		      \
     && (reserved_id) <= RESERVED_LAST_PAGE_INFO) ?	      \
    /* If reserved_id belongs to page_info */		      \
    RESERVED_NAME_PAGE_INFO :				      \
    /* else */						      \
    (((reserved_id) >= RESERVED_FIRST_KEY_INFO		      \
      && (reserved_id) <= RESERVED_LAST_KEY_INFO) ?	      \
     /* If reserved_id belongs to key info */		      \
     RESERVED_NAME_KEY_INFO :				      \
     /* else */						      \
     (((reserved_id) >= RESERVED_FIRST_BT_NODE_INFO	      \
      && (reserved_id) <= RESERVED_LAST_BT_NODE_INFO) ?	      \
      /* If reserved_id belongs to b-tree node info */	      \
      RESERVED_NAME_BTREE_NODE_INFO :			      \
      /* else must be invalid */			      \
      RESERVED_NAME_INVALID))))

/* Get first and last id for reserved name type */
#define PT_GET_RESERVED_NAME_FIRST_AND_LAST(type, first, last)   \
  switch (type)						      \
    {							      \
    case RESERVED_NAME_RECORD_INFO:			      \
      (first) = RESERVED_FIRST_RECORD_INFO;		      \
      (last) = RESERVED_LAST_RECORD_INFO;		      \
      break;						      \
    case RESERVED_NAME_PAGE_INFO:			      \
      (first) = RESERVED_FIRST_PAGE_INFO;		      \
      (last) = RESERVED_LAST_PAGE_INFO;			      \
      break;						      \
    case RESERVED_NAME_KEY_INFO:			      \
      (first) = RESERVED_FIRST_KEY_INFO;		      \
      (last) = RESERVED_LAST_KEY_INFO;			      \
      break;						      \
    case RESERVED_NAME_BTREE_NODE_INFO:			      \
      (first) = RESERVED_FIRST_BT_NODE_INFO;		      \
      (last) = RESERVED_LAST_BT_NODE_INFO;		      \
      break;						      \
    default:						      \
      assert (0);					      \
      break;						      \
    }

/* After resolving to a reserved name, check the binding is correct according
 * to spec flag
 */
#define PT_CHECK_RESERVED_NAME_BIND(spec_, reserved_id)			\
  (PT_SPEC_GET_RESERVED_NAME_TYPE (spec_)				\
   == PT_GET_RESERVED_NAME_TYPE (reserved_id))

/* Info for Names
  This includes identifiers
  */

#define NAME_FROM_PT_DOT 1
#define NAME_FROM_CLASSNAME_DOT_STAR 2	/* classname.* */
#define NAME_FROM_STAR 3	/* * */
#define NAME_IN_PATH_EXPR 4

struct pt_name_info
{
  UINTPTR spec_id;		/* unique identifier for entity specs */
  const char *original;		/* the string of the original name */
  const char *resolved;		/* the string of the resolved name */
  DB_OBJECT *db_object;		/* the object, if this is a class or instance */
  int db_object_chn;
  DB_OBJECT *virt_object;	/* the top level view this this class is being viewed through. */
  SM_PARTITION *partition;	/* partition info reference */
  PT_NODE *path_correlation;	/* as in a.b.c [path_correlation].d.e.f */
  PT_TYPE_ENUM virt_type_enum;	/* type of oid's in ldb for proxies. */
  PT_MISC_TYPE meta_class;	/* 0 or PT_META or PT_PARAMETER or PT_CLASS */
  PT_NODE *default_value;	/* PT_VALUE the default value of the attribute */
  unsigned int custom_print;
  unsigned short correlation_level;	/* for correlated attributes */
  char hidden_column;		/* used for updates and deletes for the class OID column */

#define PT_NAME_INFO_DOT_SPEC        1	/* x, y of x.y.z */
#define PT_NAME_INFO_DOT_NAME        2	/* z of x.y.z */
#define PT_NAME_INFO_STAR            4	/* * */
#define PT_NAME_INFO_DOT_STAR        8	/* classname.* */
#define PT_NAME_INFO_CONSTANT       16
#define PT_NAME_INFO_EXTERNAL       32	/* in case of TEXT type at attr definition or attr.object at attr description */
#define PT_NAME_INFO_DESC           64	/* DESC on an index column name */
#define PT_NAME_INFO_FILL_DEFAULT  128	/* whether default_value should be filled in */
#define PT_NAME_INFO_GENERATED_OID 256	/* set when a PT_NAME node that maps to an OID is generated internally for
					 * statement processing and execution */
#define PT_NAME_ALLOW_REUSABLE_OID 512	/* ignore the REUSABLE_OID restrictions for this name */
#define PT_NAME_GENERATED_DERIVED_SPEC 1024	/* attribute generated from derived spec */
#define PT_NAME_FOR_UPDATE	   2048	/* Table name in FOR UPDATE clause */
#define PT_NAME_DEFAULTF_ACCEPTS   4096	/* name of table/column that default function accepts: real table's, cte's */

  short flag;
#define PT_NAME_INFO_IS_FLAGED(e, f)    ((e)->info.name.flag & (short) (f))
#define PT_NAME_INFO_SET_FLAG(e, f)     (e)->info.name.flag |= (short) (f)
#define PT_NAME_INFO_CLEAR_FLAG(e, f)   (e)->info.name.flag &= (short) ~(f)
  short location;		/* 0: WHERE; n: join condition of n-th FROM */
  short tag_click_counter;	/* 0: normal name, 1: click counter name */
  PT_NODE *indx_key_limit;	/* key limits for index name */
  int coll_modifier;		/* collation modifier = collation + 1 */
  PT_RESERVED_NAME_ID reserved_id;	/* used to identify reserved name */
  size_t json_table_column_index;	/* will be used only for json_table to gather attributes in the correct order */
};

/*
 * information for arguments that has name and value
 */
struct pt_named_arg_info
{
  PT_NODE *name;		/* an identifier node for argument name */
  PT_NODE *value;		/* argument value, may be string, int or identifier */
};

enum
{
  PT_IDX_HINT_FORCE = 1,
  PT_IDX_HINT_USE = 0,
  PT_IDX_HINT_IGNORE = -1,
  PT_IDX_HINT_ALL_EXCEPT = -2,
  PT_IDX_HINT_CLASS_NONE = -3,
  PT_IDX_HINT_NONE = -4
};

/* PT_IDX_HINT_ORDER is used in index hint rewrite, to sort the index hints
 * in the using_index clause in the order implied by this define; this is
 * needed in order to avoid additional loops through the hint list */
#define PT_IDX_HINT_ORDER(hint_node)					  \
  ((hint_node->etc == (void *) PT_IDX_HINT_CLASS_NONE) ? 1 :		  \
    (hint_node->etc == (void *) PT_IDX_HINT_IGNORE ? 2 :		  \
      (hint_node->etc == (void *) PT_IDX_HINT_FORCE ? 3 :		  \
	(hint_node->etc == (void *) PT_IDX_HINT_USE ? 4 : 0))))

/* REMOVE TRIGGER */
struct pt_remove_trigger_info
{
  PT_NODE *trigger_spec_list;	/* PT_TRIGGER_SPEC_LIST */
};

/* Info RENAME  */
struct pt_rename_info
{
  PT_NODE *old_name;		/* PT_NAME */
  PT_NODE *in_class;		/* PT_NAME */
  PT_NODE *new_name;		/* PT_NAME */
  PT_MISC_TYPE meta;
  PT_MISC_TYPE attr_or_mthd;
  PT_MISC_TYPE entity_type;
};

/* Info for RENAME TRIGGER  */
struct pt_rename_trigger_info
{
  PT_NODE *old_name;		/* PT_NAME */
  PT_NODE *new_name;		/* PT_NAME */
};

/* Info for resolution list */
struct pt_resolution_info
{
  PT_NODE *attr_mthd_name;	/* PT_NAME */
  PT_NODE *of_sup_class_name;	/* PT_NAME */
  PT_NODE *as_attr_mthd_name;	/* PT_NAME */
  PT_MISC_TYPE attr_type;	/* enum PT_NORMAL or ... */
};

/* Info REVOKE  */
struct pt_revoke_info
{
  PT_NODE *auth_cmd_list;
  PT_NODE *user_list;
  PT_NODE *spec_list;
};

/* Info ROLLBACK  */
struct pt_rollback_work_info
{
  PT_NODE *save_name;		/* PT_NAME */
};

/* Info for a UNION/DIFFERENCE/INTERSECTION node */
struct pt_union_info
{
  PT_NODE *arg1;		/* PT_SELECT_EXPR 1st argument */
  PT_NODE *arg2;		/* PT_SELECT_EXPR 2nd argument */
  PT_NODE *select_list;		/* select list of UNION query */
  unsigned is_leaf_node:1;
};

/* Info for an SAVEPOINT node */
struct pt_savepoint_info
{
  PT_NODE *save_name;		/* PT_NAME */
};

/* Info for a SCOPE node */
struct pt_scope_info
{
  PT_NODE *from;		/* pt_spec (list) */
  PT_NODE *stmt;		/* pt_trigger_action, etc. */
};

/* Info for a SELECT node */
struct pt_select_info
{
  PT_NODE *list;		/* PT_EXPR PT_NAME */
  PT_NODE *from;		/* PT_SPEC (list) */
  PT_NODE *where;		/* PT_EXPR */
  PT_NODE *group_by;		/* PT_EXPR (list) */
  PT_NODE *connect_by;		/* PT_EXPR */
  PT_NODE *start_with;		/* PT_EXPR */
  PT_NODE *after_cb_filter;	/* PT_EXPR */
  PT_NODE *having;		/* PT_EXPR */
  PT_NODE *using_index;		/* PT_NAME (list) */
  PT_NODE *with_increment;	/* PT_NAME (list) */
  PT_NODE *ordered;		/* PT_NAME (list) */
  PT_NODE *use_nl;		/* PT_NAME (list) */
  PT_NODE *use_idx;		/* PT_NAME (list) */
  PT_NODE *index_ss;		/* PT_NAME (list) */
  PT_NODE *index_ls;		/* PT_NAME (list) */
  PT_NODE *use_merge;		/* PT_NAME (list) */
  PT_NODE *waitsecs_hint;	/* lock timeout in seconds */
  PT_NODE *jdbc_life_time;	/* jdbc cache life time */
  struct qo_summary *qo_summary;
  PT_NODE *check_where;		/* with check option predicate */
  PT_NODE *for_update;		/* FOR UPDATE clause tables list */
  QFILE_LIST_ID *push_list;	/* list file descriptor pushed to server */
  PT_HINT_ENUM hint;
  int flavor;
  int flag;			/* flags */
  PT_CONNECT_BY_CHECK_CYCLES check_cycles;	/* CONNECT BY CHECK CYCLES */
  unsigned single_table_opt:1;	/* hq optimized for single table */
};

#define PT_SELECT_INFO_ANSI_JOIN	     0x01	/* has ANSI join? */
#define PT_SELECT_INFO_ORACLE_OUTER	     0x02	/* has Oracle's outer join operator? */
#define PT_SELECT_INFO_DUMMY		     0x04	/* is dummy (i.e., 'SELECT * FROM x') ? */
#define PT_SELECT_INFO_HAS_AGG		     0x08	/* has any type of aggregation? */
#define PT_SELECT_INFO_HAS_ANALYTIC	     0x10	/* has analytic functions */
#define PT_SELECT_INFO_MULTI_UPDATE_AGG	     0x20	/* is query for multi-table update using aggregate */
#define PT_SELECT_INFO_IDX_SCHEMA	     0x40	/* is show index query */
#define PT_SELECT_INFO_COLS_SCHEMA	     0x80	/* is show columns query */
#define PT_SELECT_FULL_INFO_COLS_SCHEMA	   0x0100	/* is show columns query */
#define PT_SELECT_INFO_IS_MERGE_QUERY	   0x0200	/* is a query of a merge stmt */
#define	PT_SELECT_INFO_LIST_PUSHER	   0x0400	/* dummy subquery that pushes a list file descriptor to be used at
							 * server as its own result */
#define PT_SELECT_INFO_NO_STRICT_OID_CHECK 0x0800	/* normally, only OIDs of updatable views are allowed in parse
							 * trees; however, for MERGE and UPDATE we sometimes want to
							 * allow OIDs of partially updatable views */
#define PT_SELECT_INFO_IS_UPD_DEL_QUERY	   0x1000	/* set if select was built for an UPDATE or DELETE statement */
#define PT_SELECT_INFO_FOR_UPDATE	   0x2000	/* FOR UPDATE clause is active */
#define PT_SELECT_INFO_DISABLE_LOOSE_SCAN  0x4000	/* loose scan not possible on query */
#define PT_SELECT_INFO_MVCC_LOCK_NEEDED	   0x8000	/* lock returned rows */
#define PT_SELECT_INFO_READ_ONLY         0x010000	/* read-only system generated queries like show statement */

#define PT_SELECT_INFO_IS_FLAGED(s, f)  \
          ((s)->info.query.q.select.flag & (f))
#define PT_SELECT_INFO_SET_FLAG(s, f)   \
          (s)->info.query.q.select.flag |= (f)
#define PT_SELECT_INFO_CLEAR_FLAG(s, f) \
          (s)->info.query.q.select.flag &= ~(f)

/* common with union and select info */
struct pt_query_info
{
  int correlation_level;	/* for correlated subqueries */
  PT_MISC_TYPE all_distinct;	/* enum value is PT_ALL or PT_DISTINCT */
  PT_MISC_TYPE is_subquery;	/* PT_IS_SUB_QUERY, PT_IS_UNION_QUERY, PT_IS_CTE_NON_REC_SUBQUERY,
				 * PT_IS_CTE_REC_SUBQUERY or 0 */
  char is_view_spec;		/* 0 - normal, 1 - view query spec */
  char oids_included;		/* DB_NO_OIDS/0 DB_ROW_OIDS/1 */
  SCAN_OPERATION_TYPE scan_op_type;	/* scan operation type */
  int upd_del_class_cnt;	/* number of classes affected by update or delete in the generated SELECT statement */
  int mvcc_reev_extra_cls_cnt;	/* number of extra OID - CLASS_OID pairs added to the select list for condition and
				 * assignment reevaluation in MVCC */
  struct
  {
    unsigned has_outer_spec:1;	/* has outer join spec ? */
    unsigned is_sort_spec:1;	/* query is a sort spec expression */
    unsigned is_insert_select:1;	/* query is a sub-select for insert statement */
    unsigned single_tuple:1;	/* is single-tuple query ? */
    unsigned vspec_as_derived:1;	/* is derived from vclass spec ? */
    unsigned reexecute:1;	/* should be re-executed; not from the result caceh */
    unsigned do_cache:1;	/* do cache the query result */
    unsigned do_not_cache:1;	/* do not cache the query result */
    unsigned order_siblings:1;	/* flag ORDER SIBLINGS BY */
    unsigned rewrite_limit:1;	/* need to rewrite the limit clause */
    unsigned has_system_class:1;	/* do not cache the query result */
  } flag;
  PT_NODE *order_by;		/* PT_EXPR (list) */
  PT_NODE *orderby_for;		/* PT_EXPR (list) */
  PT_NODE *into_list;		/* PT_VALUE (list) */
  PT_NODE *qcache_hint;		/* enable/disable query cache */
  PT_NODE *limit;		/* PT_VALUE (list) limit clause parameter(s) */
  void *xasl;			/* xasl proc pointer */
  UINTPTR id;			/* query unique id # */
  PT_HINT_ENUM hint;		/* hint flag */
  bool is_order_dependent;	/* true if query is order dependent */
  PT_NODE *with;		/* PT_WITH_CLAUSE */
  union
  {
    PT_SELECT_INFO select;
    PT_UNION_INFO union_;
  } q;
};

/* Info for Set Optimization Level statement */
struct pt_set_opt_lvl_info
{
  PT_NODE *val;			/* PT_VALUE */
  PT_MISC_TYPE option;		/* PT_OPT_LVL, PT_OPT_COST */
};

/* Info for Set Parameters statement */
struct pt_set_sys_params_info
{
  PT_NODE *val;			/* PT_VALUE */
};

/* Info for Set Transaction statement */
struct pt_set_xaction_info
{
  PT_NODE *xaction_modes;	/* PT_ISOLATION_LVL, PT_TIMEOUT (list) */
};

/* Info for Set Trigger statement */
struct pt_set_trigger_info
{
  PT_NODE *val;			/* PT_VALUE */
  PT_MISC_TYPE option;		/* PT_TRIGGER_DEPTH, PT_TRIGGER_TRACE */
};

/* Info for PT_SHOWSTMT node */
struct pt_showstmt_info
{
  SHOWSTMT_TYPE show_type;	/* show statement type */
  PT_NODE *show_args;		/* show statement args */
};

struct pt_killstmt_info
{
  KILLSTMT_TYPE kill_type;
  PT_NODE *tran_id_list;
};

/* Info for OrderBy/GroupBy */
struct pt_sort_spec_info
{
  PT_NODE *expr;		/* PT_EXPR, PT_VALUE, PT_NAME */
  QFILE_TUPLE_VALUE_POSITION pos_descr;	/* Value position descriptor */
  PT_MISC_TYPE asc_or_desc;	/* enum value will be PT_ASC or PT_DESC */
  PT_MISC_TYPE nulls_first_or_last;	/* enum value will be PT_NULLS_DEFAULT, PT_NULLS_FIRST or PT_NULLS_LAST */
};

/* Info for Transaction Timeout */
struct pt_timeout_info
{
  PT_NODE *val;			/* PT_VALUE */
};

/* Info for Trigger Action */
struct pt_trigger_action_info
{
  PT_NODE *expression;		/* parse tree for expression */
  PT_NODE *string;		/* PT_PRINT string */
  PT_MISC_TYPE action_type;	/* REJECT, INVALIDATE_XACTION, etc. */
};

/* Info for Trigger Spec List */
struct pt_trigger_spec_list_info
{
  PT_NODE *trigger_name_list;	/* PT_NAME (list), or */
  PT_NODE *event_list;		/* PT_EVENT_SPEC (list), or */
  int all_triggers;		/* 1 iff ALL TRIGGERS */
};

/* Info for UPDATE node */
struct pt_update_info
{
  PT_NODE *spec;		/* SPEC */
  PT_NODE *class_specs;		/* PT_SPEC list */
  PT_NODE *assignment;		/* EXPR(list) */
  PT_NODE *search_cond;		/* EXPR */
  PT_NODE *using_index;		/* PT_NAME (list) */
  DB_OBJECT *object;		/* for single object up */
  PT_NODE *object_parameter;	/* parameter node */
  PT_NODE *cursor_name;		/* PT_NAME */
  PT_NODE *check_where;		/* with check option predicate */
  PT_NODE *internal_stmts;	/* internally created statements to handle TEXT */
  PT_NODE *waitsecs_hint;	/* lock timeout in seconds */
  PT_NODE *ordered_hint;	/* USE_NL hint's arguments (PT_NAME list) */
  PT_NODE *use_nl_hint;		/* USE_NL hint's arguments (PT_NAME list) */
  PT_NODE *use_idx_hint;	/* USE_IDX hint's arguments (PT_NAME list) */
  PT_NODE *use_merge_hint;	/* USE_MERGE hint's arguments (PT_NAME list) */
  PT_NODE *limit;		/* PT_VALUE limit clause parameter */
  PT_NODE *order_by;		/* PT_EXPR (list) */
  PT_NODE *orderby_for;		/* PT_EXPR */
  PT_HINT_ENUM hint;		/* hint flag */
  PT_NODE *with;		/* PT_WITH_CLAUSE */
  unsigned has_trigger:1;	/* whether it has triggers */
  unsigned has_unique:1;	/* whether there's unique constraint */
  unsigned server_update:1;	/* whether it can be server-side update */
  unsigned do_class_attrs:1;	/* whether it is on class attributes */
  unsigned rewrite_limit:1;	/* need to rewrite the limit clause */
  unsigned execute_with_commit_allowed:1;	/* true, if execute with commit allowed. */
};

/* UPDATE STATISTICS INFO */
struct pt_update_stats_info
{
  PT_NODE *class_list;		/* PT_NAME */
  int all_classes;		/* 1 iff ALL CLASSES */
  int with_fullscan;		/* 1 iff WITH FULLSCAN */
};

/* GET STATISTICS INFO */
struct pt_get_stats_info
{
  PT_NODE *class_;		/* PT_NAME */
  PT_NODE *args;		/* PT_VALUE character_string_literal */
  PT_NODE *into_var;		/* PT_VALUE */
};

#if defined (ENABLE_UNUSED_FUNCTION)
struct pt_use_info
{
  PT_NODE *use_list;
  PT_NODE *exclude_list;
  PT_MISC_TYPE relative;	/* 0 = absolute, PT_CURRENT = relative to current, or PT_DEFAULT = relative to default */
  char as_default;		/* If non zero, change the default, instead of the current setting */
};
#endif

/* Info for table options */
struct pt_table_option_info
{
  PT_TABLE_OPTION_TYPE option;	/* The table option type */
  PT_NODE *val;			/* PT_VALUE */
};

/* Info for MERGE statement */
struct pt_merge_info
{
  PT_NODE *into;		/* INTO PT_SPEC */
  PT_NODE *using_clause;	/* USING PT_SPEC */
  PT_NODE *search_cond;		/* PT_EXPR */
  struct
  {
    PT_NODE *assignment;	/* PT_EXPR (list) */
    PT_NODE *search_cond;	/* PT_EXPR */
    PT_NODE *del_search_cond;	/* PT_EXPR */
    PT_NODE *index_hint;	/* PT_NAME (list) */
    bool do_class_attrs;	/* whether it is on class attributes */
    bool has_delete;		/* whether it has a delete clause */
  } update;
  struct
  {
    PT_NODE *attr_list;		/* PT_NAME */
    PT_NODE *value_clauses;	/* PT_NODE_LIST (list) */
    PT_NODE *search_cond;	/* PT_EXPR */
    PT_NODE *class_where;	/* PT_EXPR */
    PT_NODE *index_hint;	/* PT_NAME (list) */
  } insert;
  PT_NODE *check_where;		/* check option */
  PT_NODE *waitsecs_hint;	/* lock timeout in seconds */
  PT_HINT_ENUM hint;		/* hint flag */
#define PT_MERGE_INFO_HAS_UNIQUE  1	/* has unique constraints */
#define PT_MERGE_INFO_SERVER_OP	  2	/* server side operation */
#define PT_MERGE_INFO_INSERT_ONLY 4	/* merge condition always false */
  short flags;			/* statement specific flags */
};

/* Info for SET NAMES */
struct pt_set_names_info
{
  PT_NODE *charset_node;	/* PT_VALUE */
  PT_NODE *collation_node;	/* PT_VALUE */
};

struct pt_set_timezone_info
{
  PT_NODE *timezone_node;	/* PT_VALUE */
};

/* Info for VALUE nodes
  these are intended to parallel the definitions in dbi.h and be
  identical whenever possible
  */

/* enum Time Zones */
typedef enum pt_time_zones
{
  PT_TIMEZONE_EASTERN,
  PT_TIMEZONE_CENTRAL,
  PT_TIMEZONE_MOUNTAIN,
  PT_TIMEZONE_PACIFIC
} PT_TIMEZONE;

/* typedefs for TIME and DATE */
typedef long PT_TIME;
typedef long PT_UTIME;
typedef DB_TIMESTAMPTZ PT_TIMESTAMPTZ;
typedef long PT_DATE;
typedef DB_DATETIME PT_DATETIME;
typedef DB_DATETIMETZ PT_DATETIMETZ;

/* enum currency types */
typedef enum pt_currency_types
{
  PT_CURRENCY_DOLLAR,
  PT_CURRENCY_YEN,
  PT_CURRENCY_BRITISH_POUND,
  PT_CURRENCY_WON,
  PT_CURRENCY_TL,
  PT_CURRENCY_CAMBODIAN_RIEL,
  PT_CURRENCY_CHINESE_RENMINBI,
  PT_CURRENCY_INDIAN_RUPEE,
  PT_CURRENCY_RUSSIAN_RUBLE,
  PT_CURRENCY_AUSTRALIAN_DOLLAR,
  PT_CURRENCY_CANADIAN_DOLLAR,
  PT_CURRENCY_BRASILIAN_REAL,
  PT_CURRENCY_ROMANIAN_LEU,
  PT_CURRENCY_EURO,
  PT_CURRENCY_SWISS_FRANC,
  PT_CURRENCY_DANISH_KRONE,
  PT_CURRENCY_NORWEGIAN_KRONE,
  PT_CURRENCY_BULGARIAN_LEV,
  PT_CURRENCY_VIETNAMESE_DONG,
  PT_CURRENCY_CZECH_KORUNA,
  PT_CURRENCY_POLISH_ZLOTY,
  PT_CURRENCY_SWEDISH_KRONA,
  PT_CURRENCY_CROATIAN_KUNA,
  PT_CURRENCY_SERBIAN_DINAR,
  PT_CURRENCY_NULL
} PT_CURRENCY;

/* struct for money */
struct pt_monetary_value
{
  double amount;
  PT_CURRENCY type;
};

struct pt_enum_element_value
{
  unsigned short short_val;
  PARSER_VARCHAR *str_val;
};

/* Union of datavalues */
union pt_data_value
{
  long i;
  DB_BIGINT bigint;
  float f;
  double d;
  PARSER_VARCHAR *str;		/* keeps as string different data type: string data types (char, nchar, byte) date and
				 * time data types numeric */
  void *p;			/* what is this */
  DB_OBJECT *op;
  PT_TIME time;
  PT_DATE date;
  PT_UTIME utime;		/* used for TIMESTAMP and TIMESTAMPLTZ */
  PT_TIMESTAMPTZ timestamptz;
  PT_DATETIME datetime;		/* used for DATETIME and DATETIMELTZ */
  PT_DATETIMETZ datetimetz;
  PT_MONETARY money;
  PT_NODE *set;			/* constant sets */
  DB_ELO elo;			/* ??? */
  int b;
  PT_ENUM_ELEMENT enumeration;
};

/* Info for the VALUE node */
struct pt_value_info
{
  const char *text;		/* printed text of a value or of an expression folded to a value. NOTE: this is not the
				 * actual value of the node. Use value in data_value instead. */
  PT_DATA_VALUE data_value;	/* see above UNION defs */
  DB_VALUE db_value;
  short db_value_is_initialized;
  short db_value_is_in_workspace;
  short location;		/* 0 : WHERE; n : join condition of n-th FROM */
  char string_type;		/* ' ', 'N', 'B', or 'X' */
  bool print_charset;
  bool print_collation;
  bool has_cs_introducer;	/* 1 if charset introducer is used for string node e.g. _utf8'a'; 0 otherwise. */
  bool is_collate_allowed;	/* 1 if this is a PT_VALUE allowed to have the COLLATE modifier (the grammar context in
				 * which is created allows it) */
  int coll_modifier;		/* collation modifier = collation + 1 */
  int host_var_index;		/* save the host_var index which it comes from. -1 means it is a normal value. it does
				 * not come from any host_var. */
};

/* Info for the ZZ_ERROR_MSG node */
struct PT_ZZ_ERROR_MSG_INFO
{
  char *error_message;		/* a helpful explanation of the error */
  int statement_number;		/* statement where error was detected */
};

/* Info for the FOREIGN KEY node */
struct pt_foreign_key_info
{
  PT_NODE *attrs;		/* List of attribute names */
  PT_NODE *referenced_class;	/* Class name */
  PT_NODE *referenced_attrs;	/* List of attribute names */
  PT_MISC_TYPE match_type;	/* full or partial */
  PT_MISC_TYPE delete_action;
  PT_MISC_TYPE update_action;
};

/* Info for the CONSTRAINT node */
struct pt_constraint_info
{
  PT_NODE *name;
  PT_CONSTRAINT_TYPE type;
  short deferrable;
  short initially_deferred;
  union
  {
    struct
    {
      PT_NODE *attrs;		/* List of attribute names */
    } primary_key;
    PT_FOREIGN_KEY_INFO foreign_key;
    struct
    {
      PT_NODE *attr;		/* Attribute name */
    } not_null;
    struct
    {
      PT_NODE *attrs;		/* List of attribute names */
    } unique;
    struct
    {
      PT_NODE *expr;		/* Search condition */
    } check;
  } un;
  PT_NODE *comment;
};

/* POINTER node types */
enum pt_pointer_type
{
  PT_POINTER_NORMAL = 0,	/* normal pointer, gets resolved to node */
  PT_POINTER_REF = 1		/* reference pointer - node gets walked by pt_walk_tree */
};
typedef enum pt_pointer_type PT_POINTER_TYPE;

/* Info for the POINTER node */
struct pt_pointer_info
{
  PT_NODE *node;		/* original node pointer */
  PT_POINTER_TYPE type;		/* pointer type (normal pointer/reference) */
  double sel;			/* selectivity factor of the predicate */
  int rank;			/* rank factor for the same selectivity */
  bool do_walk;			/* apply walk on node bool */
};

struct pt_stored_proc_info
{
  PT_NODE *name;
  PT_NODE *param_list;
  PT_NODE *java_method;
  PT_NODE *comment;
  PT_NODE *owner;		/* for ALTER PROCEDURE/FUNCTION name OWNER TO new_owner */
  PT_MISC_TYPE type;
  unsigned or_replace:1;	/* OR REPLACE clause */
  PT_TYPE_ENUM ret_type;
};

struct pt_prepare_info
{
  PT_NODE *name;		/* the name of the prepared statement */
  PT_NODE *statement;		/* the string literal that defines the statement */
  PT_NODE *using_list;		/* the list of values given for statement execution */
};

struct pt_execute_info
{
  PT_NODE *name;		/* the name of the prepared statement */
  PT_NODE *query;		/* the string literal that defines the statement */
  PT_NODE *using_list;		/* the list of values given for statement execution */
  PT_NODE *into_list;		/* for a prepared select using into */
  XASL_ID xasl_id;		/* XASL id */
  CUBRID_STMT_TYPE stmt_type;	/* statement type */
  int recompile;		/* not 0 if this statement should be recompiled */
  int column_count;		/* select list column count */
  int oids_included;		/* OIDs included in select list */
};

struct pt_stored_proc_param_info
{
  PT_NODE *name;
  PT_MISC_TYPE mode;
  PT_NODE *comment;
};

/* TRUNCATE ENTITY INFO */
struct pt_truncate_info
{
  PT_NODE *spec;		/* PT_SPEC */
};

/* DO ENTITY INFO */
struct pt_do_info
{
  PT_NODE *expr;		/* PT_EXPR */
};

struct pt_set_session_variable_info
{
  PT_NODE *assignments;
};

/* Retains expressions for check when 'with check option' option is specified. Used in update statement */
struct pt_check_option_info
{
  UINTPTR spec_id;		/* id of spec for wich to check condition */
  PT_NODE *expr;		/* condition to check */
};

/* info structure for a node which can be evaluated during constant folding as a value of a tuple from a cursor */
struct pt_tuple_value_info
{
  PT_NODE *name;		/* name alias in the original query */
  CURSOR_ID *cursor_p;		/* cursor from which the value can be read */
  int index;			/* index of the value in cursor */
};

struct pt_trace_info
{
  PT_MISC_TYPE on_off;
  PT_MISC_TYPE format;
};

/* pt_with_clause_info - Parse tree node info which contains CTEs data(used by DML statements) */
struct pt_with_clause_info
{
  int recursive;
  PT_NODE *cte_definition_list;	/* PT_CTE (list) */
};

/* pt_insert_value_info - Parse tree node info used to replace nodes in insert value list after being evaluated. */
struct pt_insert_value_info
{
  PT_NODE *original_node;	/* original node before first evaluation. if this is NULL, it is considered that
				 * evaluated value cannot change on different execution, and reevaluation is not needed */
  DB_VALUE value;		/* evaluated value */
  int is_evaluated;		/* true if value was already evaluated */
  int replace_names;		/* true if names in evaluated node need to be replaced */
};

struct pt_json_table_column_info
{
  PT_NODE *name;
  // domain is stored in parser node
  char *path;
  size_t index;			// will be used to store the columns in the correct order
  enum json_table_column_function func;
  struct json_table_column_behavior on_error;
  struct json_table_column_behavior on_empty;
};

struct pt_json_table_node_info
{
  PT_NODE *columns;
  PT_NODE *nested_paths;
  char *path;
};

struct pt_json_table_info
{
  PT_NODE *expr;
  PT_NODE *tree;
  bool is_correlated;
};

/* Info field of the basic NODE
  If 'xyz' is the name of the field, then the structure type should be
  struct PT_XYZ_INFO xyz;
  List in alphabetical order.
  */

union pt_statement_info
{
  PT_ZZ_ERROR_MSG_INFO error_msg;
  PT_ALTER_INFO alter;
  PT_ALTER_TRIGGER_INFO alter_trigger;
  PT_ALTER_USER_INFO alter_user;
  PT_ATTACH_INFO attach;
  PT_ATTR_DEF_INFO attr_def;
  PT_ATTR_ORDERING_INFO attr_ordering;
  PT_AUTH_CMD_INFO auth_cmd;
  PT_AUTO_INCREMENT_INFO auto_increment;
  PT_CHECK_OPTION_INFO check_option;
  PT_COMMIT_WORK_INFO commit_work;
  PT_CONSTRAINT_INFO constraint;
  PT_CREATE_ENTITY_INFO create_entity;
  PT_CREATE_TRIGGER_INFO create_trigger;
  PT_CREATE_USER_INFO create_user;
  PT_CTE_INFO cte;
  PT_DATA_DEFAULT_INFO data_default;
  PT_DATA_TYPE_INFO data_type;
  PT_DELETE_INFO delete_;
  PT_DO_INFO do_;
  PT_DOT_INFO dot;
  PT_DROP_INFO drop;
  PT_DROP_SESSION_VAR_INFO drop_session_var;
  PT_DROP_TRIGGER_INFO drop_trigger;
  PT_DROP_USER_INFO drop_user;
  PT_DROP_VARIABLE_INFO drop_variable;
  PT_EVALUATE_INFO evaluate;
  PT_EVENT_OBJECT_INFO event_object;
  PT_EVENT_SPEC_INFO event_spec;
  PT_EVENT_TARGET_INFO event_target;
  PT_EXECUTE_INFO execute;
  PT_EXECUTE_TRIGGER_INFO execute_trigger;
  PT_EXPR_INFO expr;
  PT_FILE_PATH_INFO file_path;
  PT_FUNCTION_INFO function;
  PT_GET_OPT_LVL_INFO get_opt_lvl;
  PT_GET_STATS_INFO get_stats;
  PT_GET_TRIGGER_INFO get_trigger;
  PT_GET_XACTION_INFO get_xaction;
  PT_GRANT_INFO grant;
  PT_HOST_VAR_INFO host_var;
  PT_INDEX_INFO index;
  PT_INSERT_INFO insert;
  PT_INSERT_VALUE_INFO insert_value;
  PT_ISOLATION_LVL_INFO isolation_lvl;
  PT_JSON_TABLE_INFO json_table_info;
  PT_JSON_TABLE_NODE_INFO json_table_node_info;
  PT_JSON_TABLE_COLUMN_INFO json_table_column_info;
  PT_MERGE_INFO merge;
  PT_METHOD_CALL_INFO method_call;
  PT_METHOD_DEF_INFO method_def;
  PT_NAME_INFO name;
  PT_NAMED_ARG_INFO named_arg;
  PT_NODE_LIST_INFO node_list;
  PT_PARTITION_INFO partition;
  PT_PARTS_INFO parts;
  PT_PREPARE_INFO prepare;
  PT_ATTACH_INFO prepare_to_commit;
  PT_QUERY_INFO query;
  PT_REMOVE_TRIGGER_INFO remove_trigger;
  PT_RENAME_INFO rename;
  PT_RENAME_TRIGGER_INFO rename_trigger;
  PT_RESOLUTION_INFO resolution;
  PT_REVOKE_INFO revoke;
  PT_ROLLBACK_WORK_INFO rollback_work;
  PT_SAVEPOINT_INFO savepoint;
  PT_SCOPE_INFO scope;
  PT_SERIAL_INFO serial;
  PT_SET_NAMES_INFO set_names;
  PT_SET_TIMEZONE_INFO set_timezone;
  PT_SET_OPT_LVL_INFO set_opt_lvl;
  PT_SET_SYS_PARAMS_INFO set_sys_params;
  PT_SET_TRIGGER_INFO set_trigger;
  PT_SET_SESSION_VARIABLE_INFO set_variables;
  PT_SET_XACTION_INFO set_xaction;
  PT_SHOWSTMT_INFO showstmt;
  PT_SORT_SPEC_INFO sort_spec;
  PT_STORED_PROC_INFO sp;
  PT_STORED_PROC_PARAM_INFO sp_param;
  PT_SPEC_INFO spec;
  PT_TABLE_OPTION_INFO table_option;
  PT_TIMEOUT_INFO timeout;
  PT_TRIGGER_ACTION_INFO trigger_action;
  PT_TRIGGER_SPEC_LIST_INFO trigger_spec_list;
  PT_TRUNCATE_INFO truncate;
  PT_TUPLE_VALUE_INFO tuple_value;
  PT_UPDATE_INFO update;
  PT_UPDATE_STATS_INFO update_stats;
#if defined (ENABLE_UNUSED_FUNCTION)
  PT_USE_INFO use;
#endif
  PT_VALUE_INFO value;
  PT_POINTER_INFO pointer;
  PT_TRACE_INFO trace;
  PT_KILLSTMT_INFO killstmt;
  PT_WITH_CLAUSE_INFO with_clause;
};

/*
 * auxiliary structures for tree walking operations related to aggregates
 */
struct pt_agg_check_info
{
  PT_NODE *from;		/* initial spec list */
  PT_NODE *group_by;		/* group by list */
  int depth;			/* current depth */
};

struct pt_agg_rewrite_info
{
  PT_NODE *select_stack;	/* SELECT statement stack (0 = base) */
  PT_NODE *from;		/* initial spec list */
  PT_NODE *new_from;		/* new spec */
  PT_NODE *derived_select;	/* initial select (that is being derived) */
  int depth;
};

struct pt_agg_find_info
{
  PT_NODE *select_stack;	/* SELECT statement stack (0 = base) */
  int base_count;		/* # of aggregate functions that belong to the statement at the base of the stack */
  int out_of_context_count;	/* # of aggregate functions that do not belong to any statement within the stack */
  bool stop_on_subquery;	/* walk subqueries? */
  bool disable_loose_scan;	/* true if loose index scan cannot be used */
};

struct pt_agg_name_info
{
  PT_NODE *select_stack;	/* SELECT statement stack (0 = base) */
  int max_level;		/* maximum level within the stack that is is referenced by PT_NAMEs */
  int name_count;		/* # of PT_NAME nodes found */
};

struct pt_filter_index_info
{
  PT_NODE *atts;		/* attributes */
  int atts_count;		/* attributes count */
  int depth;			/* expression depth */
  bool *is_null_atts;		/* for each filter index attribute true, when "is null index_attribute" term is
				 * contained in filter index expression */
  bool has_keys_in_expression;	/* true, if an index key appear in expression */
  bool is_constant_expression;	/* true, if expression is constant */
  bool is_valid_expr;		/* true, if invalid filter index expression */
  bool has_not;			/* true, when not operator is found in filter index expression */
};

struct pt_non_groupby_col_info
{
  PT_NODE *groupby;
  bool has_non_groupby_col;
};

struct pt_flat_spec_info
{
  PT_NODE *spec_parent;
  bool for_update;
};

/*
 * variable string for parser
 */
struct parser_varchar
{
  int length;
  unsigned char bytes[1];
};

/*
 * The parser node structure
 */
struct parser_node
{
  PT_NODE_TYPE node_type;	/* the type of SQL statement this represents */
  int parser_id;		/* which parser did I come from */
  int line_number;		/* the user line number originating this */
  int column_number;		/* the user column number originating this */
  int buffer_pos;		/* position in the parse buffer of the string originating this */
  char *sql_user_text;		/* user input sql string */
  int sql_user_text_len;	/* user input sql string length (one statement) */

  PT_NODE *next;		/* forward link for NULL terminated list */
  PT_NODE *or_next;		/* forward link for DNF list */
  PT_NODE *next_row;		/* for PT_VALUE,PT_NAME,PT_EXPR... that belongs to PT_NODE_LIST */
  void *etc;			/* application specific info hook */
  UINTPTR spec_ident;		/* entity spec equivalence class */
  TP_DOMAIN *expected_domain;	/* expected domain for input marker */
  PT_NODE *data_type;		/* for non-primitive types, Sets, objects stec. */
  XASL_ID *xasl_id;		/* XASL_ID for this SQL statement */
  const char *alias_print;	/* the column alias */
  PARSER_VARCHAR *expr_before_const_folding;	/* text before constant folding (used by value, host var nodes) */
  PT_TYPE_ENUM type_enum;	/* type enumeration tag PT_TYPE_??? */
  CACHE_TIME cache_time;	/* client or server cache time */
  struct
  {
    unsigned recompile:1;	/* the statement should be recompiled - used for plan cache */
    unsigned cannot_prepare:1;	/* the statement cannot be prepared - used for plan cache */
    unsigned partition_pruned:1;	/* partition pruning takes place */
    unsigned si_datetime:1;	/* get server info; SYS_DATETIME */
    unsigned si_tran_id:1;	/* get server info; LOCAL_TRANSACTION_ID */
    unsigned clt_cache_check:1;	/* check client cache validity */
    unsigned clt_cache_reusable:1;	/* client cache is reusable */
    unsigned use_plan_cache:1;	/* used for plan cache */
    unsigned use_query_cache:1;
    unsigned is_hidden_column:1;
    unsigned is_paren:1;
    unsigned with_rollup:1;	/* WITH ROLLUP clause for GROUP BY */
    unsigned force_auto_parameterize:1;	/* forces a call to qo_do_auto_parameterize (); this is a special flag used for
					 * processing ON DUPLICATE KEY UPDATE */
    unsigned do_not_fold:1;	/* disables constant folding on the node */
    unsigned is_cnf_start:1;
    unsigned is_click_counter:1;	/* INCR/DECR(click counter) */
    unsigned is_value_query:1;	/* for PT_VALUE,PT_NAME,PT_EXPR... that belongs to PT_NODE_LIST for PT_SELECT that
				 * "values" generated */
    unsigned do_not_replace_orderby:1;	/* when checking query in create/alter view, do not replace order by */
    unsigned is_added_by_parser:1;	/* is added by parser during parsing */
    unsigned is_alias_enabled_expr:1;	/* node allowed to have alias */
    unsigned is_wrapped_res_for_coll:1;	/* is a result node wrapped with CAST by collation inference */
    unsigned is_system_generated_stmt:1;	/* is internally generated by system */
    unsigned use_auto_commit:1;	/* use autocommit */
  } flag;
  PT_STATEMENT_INFO info;	/* depends on 'node_type' field */
};

/* Values that describe the state of a client session across statements
 * execution.
 */
typedef struct execution_state_values EXECUTION_STATE_VALUES;
struct execution_state_values
{
  int row_count;		/* The number of rows that were inserted, updated or deleted by the previously executed
				 * statement. It is set to -1 on certain errors or if the previous statement did not
				 * modify any rows. */
};

/* 20 (for the LOCAL_TRANSACTION_ID keyword) + null char + 3 for expansion */
#define MAX_KEYWORD_SIZE (20 + 1 + 3)

typedef struct keyword_record KEYWORD_RECORD;
struct keyword_record
{
#if defined(ENABLE_UNUSED_FUNCTION)
  short value;
#else
  unsigned short hash_value;
#endif
  char keyword[MAX_KEYWORD_SIZE];
  short unreserved;		/* keyword can be used as an identifier, 0 means it is reserved and cannot be used as
				 * an identifier, nonzero means it can be */
};


typedef struct function_map FUNCTION_MAP;
struct function_map
{
  unsigned short hash_value;
  const char *keyword;
  int op;
};

typedef struct pt_plan_trace_info
{
  QUERY_TRACE_FORMAT format;
  union
  {
    char *text_plan;
    struct json_t *json_plan;
  } trace;
} PT_PLAN_TRACE_INFO;

typedef int (*PT_CASECMP_FUN) (const char *s1, const char *s2);
typedef int (*PT_INT_FUNCTION) (PARSER_CONTEXT * c);

struct parser_context
{
  PT_INT_FUNCTION next_char;	/* the next character function */
  PT_INT_FUNCTION next_byte;	/* the next byte function */
  PT_CASECMP_FUN casecmp;	/* for case insensitive comparisons */

  int id;			/* internal parser id */
  int statement_number;		/* user-initialized, incremented by parser */

  const char *original_buffer;	/* pointer to the original parse buffer */
  const char *buffer;		/* for parse buffer */
  FILE *file;			/* for parse file */
  int stack_top;		/* parser stack top */
  int stack_size;		/* total number of slots in node_stack */
  PT_NODE **node_stack;		/* the parser stack */
  PT_NODE *orphans;		/* list of parse tree fragments freed later */

  char *error_buffer;		/* for parse error messages */

  PT_NODE **statements;		/* array of statement pointers */
  PT_NODE *error_msgs;		/* list of parsing error messages */
  PT_NODE *warnings;		/* list of warning messages */

  PT_PRINT_VALUE_FUNC print_db_value;

  jmp_buf jmp_env;		/* environment for longjumping on out of memory errors. */
  unsigned int custom_print;
  int jmp_env_active;		/* flag to indicate jmp_env status */

  QUERY_ID query_id;		/* id assigned to current query */
  DB_VALUE *host_variables;	/* host variables place holder; DB_VALUE array */
  TP_DOMAIN **host_var_expected_domains;	/* expected domains for host variables */
  EXECUTION_STATE_VALUES execution_values;	/* values kept across the execution of statements during a client
						 * session. */
  int host_var_count;		/* number of input host variables */
  int auto_param_count;		/* number of auto parameterized variables */
  int dbval_cnt;		/* to be assigned to XASL */
  int line, column;		/* current input line and column */

  void *etc;			/* application context */

  VIEW_CACHE_INFO *view_cache;	/* parsing cache using in view transformation */
  struct symbol_info *symbols;	/* a place to keep information used in generating query processing (xasl) procedures. */
  char **lcks_classes;

  size_t input_buffer_length;
  size_t input_buffer_position;

  int au_save;			/* authorization to restore if longjmp while authorization turned off */

  DB_VALUE sys_datetime;
  DB_VALUE sys_epochtime;

  DB_VALUE local_transaction_id;

  int num_lcks_classes;
  PT_INCLUDE_OID_TYPE oid_included;	/* for update cursors */

  long int lrand;		/* integer random value used by rand() */
  double drand;			/* floating-point random value used by drand() */

  COMPILE_CONTEXT context;
  struct xasl_node *parent_proc_xasl;

  bool query_trace;
  int num_plan_trace;
  PT_PLAN_TRACE_INFO plan_trace[MAX_NUM_PLAN_TRACE];

  int max_print_len;		/* for pt_short_print */

  struct
  {
    unsigned has_internal_error:1;	/* 0 or 1 */
    unsigned abort:1;		/* this flag is for aborting a transaction */
    /* if deadlock occurs during query execution */
    unsigned set_host_var:1;	/* 1 if the user has set host variables */
    unsigned dont_prt_long_string:1;	/* make pt_print_value fail if the string is too long to print */
    unsigned long_string_skipped:1;	/* pt_print_value sets it to 1 when it skipped printing a long string */
    unsigned print_type_ambiguity:1;	/* pt_print_value sets it to 1 when it printed a value whose type cannot be
					 * clearly determined from the string representation */
    unsigned strings_have_no_escapes:1;
    unsigned is_in_and_list:1;	/* set to 1 when the caller immediately above is pt_print_and_list(). Used because AND
				 * lists (CNF trees) can be printed via print_and_list or straight via pt_print_expr().
				 * We need to keep print_and_list because it could get called before we get a chance to
				 * mark the CNF start nodes. */
    unsigned is_holdable:1;	/* set to true if result must be available across commits */
    unsigned is_xasl_pinned_reference:1;	/* set to 1 if the prepared xasl cache need to be pinned in server side. To
						 * prevent other thread from preempting the xasl cache again. This will
						 * happen when a jdbc/cci driver retries to prepare/execute a query due to
						 * CAS_ER_STMT_POOLING. */
    unsigned recompile_xasl_pinned:1;	/* set to 1 when recompile again even the xasl cache entry has been pinned */
    unsigned dont_collect_exec_stats:1;
    unsigned return_generated_keys:1;
    unsigned is_system_generated_stmt:1;
    unsigned is_auto_commit:1;	/* set to true, if auto commit. */
  } flag;
};

/* used in assignments enumeration */
typedef struct pt_assignments_helper PT_ASSIGNMENTS_HELPER;
struct pt_assignments_helper
{
  PARSER_CONTEXT *parser;	/* parser context */
  PT_NODE *assignment;		/* current assignment node in the assignments list */
  PT_NODE *lhs;			/* left side of the assignment */
  PT_NODE *rhs;			/* right side of the assignment */
  bool is_rhs_const;		/* true if the right side is a constant */
  bool is_n_column;		/* true if the assignment is a multi-column assignment */
};

/* Collation coercibility levels associated with parse tree nodes */
enum pt_coll_coerc_lev
{
  PT_COLLATION_L0_COERC = 0,	/* expressions with COLLATE modifier */
  /* Columns */
  PT_COLLATION_L1_COERC,	/* non-binary collations */
  PT_COLLATION_L1_ISO_BIN_COERC,	/* with ISO binary coll */
  PT_COLLATION_L1_BIN_COERC,	/* with binary collation */
  /* SELECT values, expressions */
  PT_COLLATION_L2_COERC,	/* non-binary collations */
  PT_COLLATION_L2_BINARY_COERC,	/* with binary collation */
  PT_COLLATION_L2_BIN_COERC,	/* with ISO, UTF8 or EUCKR binary collation */
  /* special operators (USER()) */
  PT_COLLATION_L3_COERC,
  /* constants (string literals) */
  PT_COLLATION_L4_COERC,	/* non-binary collations */
  PT_COLLATION_L4_BINARY_COERC,	/* with binary collation */
  PT_COLLATION_L4_BIN_COERC,	/* with ISO, UTF8 or EUCKR binary collation */
  /* HV, session variables */
  PT_COLLATION_L5_COERC,
  /* nodes not having collation (internal use) */
  PT_COLLATION_L6_COERC,

  PT_COLLATION_NOT_APPLICABLE = PT_COLLATION_L6_COERC,
  PT_COLLATION_NOT_COERC = PT_COLLATION_L0_COERC,
  PT_COLLATION_FULLY_COERC = PT_COLLATION_L5_COERC
};
typedef enum pt_coll_coerc_lev PT_COLL_COERC_LEV;

typedef struct pt_coll_infer PT_COLL_INFER;
struct pt_coll_infer
{
  int coll_id;
  INTL_CODESET codeset;
  PT_COLL_COERC_LEV coerc_level;
  bool can_force_cs;		/* used as a weak-modifier for collation coercibility (when node is a host variable). +
				 * for auto-CAST expressions around numbers: initially the string data type of CAST is
				 * created with system charset by generic type checking but that charset can be forced
				 * to another charset (of another argument) if this flag is set */

#ifdef __cplusplus
  // *INDENT-OFF*
    pt_coll_infer ()
      : coll_id (-1)
      , codeset (INTL_CODESET_NONE)
      , coerc_level (PT_COLLATION_NOT_APPLICABLE)
      , can_force_cs (true)
  {
    //
  }
  // *INDENT-ON*
#endif				// c++
};

void pt_init_node (PT_NODE * node, PT_NODE_TYPE node_type);

#ifdef __cplusplus
extern "C"
{
#endif
  void *parser_allocate_string_buffer (const PARSER_CONTEXT * parser, const int length, const int align);
  bool pt_is_json_value_type (PT_TYPE_ENUM type);
  bool pt_is_json_doc_type (PT_TYPE_ENUM type);
#ifdef __cplusplus
}
#endif

#if !defined (SERVER_MODE)
#ifdef __cplusplus
extern "C"
{
#endif
  extern PARSER_CONTEXT *parent_parser;
#ifdef __cplusplus
}
#endif
#endif

#endif				/* _PARSE_TREE_H_ */
