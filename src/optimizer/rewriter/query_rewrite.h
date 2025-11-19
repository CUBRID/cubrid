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
 * query_rewrite.h - don't include this except files in this folder
 */

#ifndef _QUERY_REWRITE_H_
#define _QUERY_REWRITE_H_

#ident "$Id$"

#include "dbtype.h"
#include "object_primitive.h"
#include "object_representation.h"
#include "object_domain.h"
#include "optimizer.h"
#include "parser.h"
#include "parser_message.h"
#include "parse_tree.h"
#include "semantic_check.h"
#include "view_transform.h"

#define DB_MAX_LITERAL_PRECISION 255

typedef struct spec_id_info SPEC_ID_INFO;
typedef struct spec_cnt_info SPEC_CNT_INFO;
typedef struct to_dot_info TO_DOT_INFO;
typedef struct pt_name_spec_info PT_NAME_SPEC_INFO;
typedef struct qo_reset_location_info RESET_LOCATION_INFO;
typedef struct qo_reduce_reference_info QO_REDUCE_REFERENCE_INFO;

struct spec_id_info
{
  UINTPTR id;
  bool appears;
  bool nullable;
};

struct spec_cnt_info
{
  PT_NODE *spec;
  int my_spec_cnt;
  int other_spec_cnt;
  PT_NODE *my_spec_node;
};

struct to_dot_info
{
  PT_NODE *old_spec;
  PT_NODE *new_spec;
};

struct pt_name_spec_info
{
  PT_NODE *c_name;		/* attr name which will be reduced to constant */
  int c_name_num;
  int query_serial_num;		/* query, serial number */
  PT_NODE *s_point_list;	/* list of other specs name. these are joined with spec of c_name */
};

struct qo_reset_location_info
{
  PT_NODE *start_spec;
  short start;
  short end;
  bool found_outerjoin;
};

struct qo_reduce_reference_info
{
  PT_NODE *pk_spec;
  MOP pk_mop;
  SM_CLASS_CONSTRAINT *pk_cons;
  PT_NODE *fk_spec;
  SM_CLASS_CONSTRAINT *fk_cons;
  PT_NODE *exclude_pk_spec_point_list;
  PT_NODE *exclude_fk_spec_point_list;
  PT_NODE *join_pred_point_list;
  PT_NODE *parent_pred_point_list;
  PT_NODE *append_not_null_pred_list;
};

/* result of CompDBValueWithOpType() function */
enum comp_dbvalue_with_optye_result
{
  CompResultLess = -2,		/* less than */
  CompResultLessAdj = -1,	/* less than and adjacent to */
  CompResultEqual = 0,		/* equal */
  CompResultGreaterAdj = 1,	/* greater than and adjacent to */
  CompResultGreater = 2,	/* greater than */
  CompResultError = 3		/* error */
};
typedef enum comp_dbvalue_with_optye_result COMP_DBVALUE_WITH_OPTYPE_RESULT;

enum dnf_merge_range_result
{
  DNF_RANGE_VALID = 0,
  DNF_RANGE_ALWAYS_FALSE = 1,
  DNF_RANGE_ALWAYS_TRUE = 2
};

typedef enum dnf_merge_range_result DNF_MERGE_RANGE_RESULT;

/* optimize subqueries */
PT_NODE *qo_rewrite_subqueries (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk);
PT_NODE *qo_rewrite_hidden_col_as_derived (PARSER_CONTEXT * parser, PT_NODE * node, PT_NODE * parent_node);
void qo_add_limit_clause (PARSER_CONTEXT * parser, PT_NODE * node);

/* optimize terms */
void qo_rewrite_terms (PARSER_CONTEXT * parser, PT_NODE ** terms);
void qo_reduce_equality_terms (PARSER_CONTEXT * parser, PT_NODE * node, PT_NODE ** wherep);
PT_NODE *qo_reduce_equality_terms_post (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk);
int qo_is_reduceable_const (PT_NODE * expr);
PT_NODE *qo_get_name_by_spec_id (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk);
PT_NODE *qo_check_nullable_expr_with_spec (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk);
bool qo_check_condition_null (PARSER_CONTEXT * parser, PT_NODE * path_spec, PT_NODE * query_where);

/* optimize set */
bool qo_check_distinct_union (PARSER_CONTEXT * parser, PT_NODE * node);
bool qo_check_hint_union (PARSER_CONTEXT * parser, PT_NODE * node, PT_HINT_ENUM hint);
PT_NODE *qo_push_limit_to_union (PARSER_CONTEXT * parser, PT_NODE * node, PT_NODE * limit);

/* optimize select queries */
PT_NODE *qo_analyze_path_join_pre (PARSER_CONTEXT * parser, PT_NODE * spec, void *arg, int *continue_walk);
PT_NODE *qo_analyze_path_join (PARSER_CONTEXT * parser, PT_NODE * path_spec, void *arg, int *continue_walk);
bool qo_check_generate_single_tbl_connect_by (PARSER_CONTEXT * parser, PT_NODE * node);
bool qo_rewrite_select_queries (PARSER_CONTEXT * parser, PT_NODE ** nodep, PT_NODE ** wherep, int *seqno);
void qo_move_on_of_explicit_join_to_where (PARSER_CONTEXT * parser, PT_NODE ** fromp, PT_NODE ** wherep);
void qo_rewrite_index_hints (PARSER_CONTEXT * parser, PT_NODE * statement);
void qo_rewrite_nonnull_count_select_list (PARSER_CONTEXT * parser, PT_NODE * select);

/* qo_auto_parameterize is defined in parser.h */
void qo_auto_parameterize_limit_clause (PARSER_CONTEXT * parser, PT_NODE * node);
void qo_auto_parameterize_keylimit_clause (PARSER_CONTEXT * parser, PT_NODE * node);

/* macros */
#define QO_CHECK_AND_REDUCE_EQUALITY_TERMS(parser, node, where) \
  do { \
      if (!node->flag.done_reduce_equality_terms) \
      { \
          node->flag.done_reduce_equality_terms = true; \
          qo_reduce_equality_terms (parser, node, where); \
      } \
  } while (0)


#define PROCESS_IF_EXISTS(parser, condition, func) \
	do                                                 \
	{                                                  \
		if (*(condition))                                \
		{                                                \
			func(parser, *condition);                      \
		}                                                \
	} while (0)

#endif /* _QUERY_REWRITER_H_ */
