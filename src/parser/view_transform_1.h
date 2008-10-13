/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * view_transform.h - view (virtual class) transformation
 * TODO: rename this file to view_transform.h
 */

#ifndef _VIEW_TRANSFORM_H_
#define _VIEW_TRANSFORM_H_

#ident "$Id$"

#include "parser.h"

typedef enum PT_FETCH_AS
{ PT_NORMAL_SELECT, PT_INVERTED_ASSIGNMENTS }
PT_FETCH_AS;

typedef const char *conststring;
static const conststring db_auth_name[DB_AUTH_DELETE * 2] =
  { "", "Select", "Insert", "Insert", "Update", "Update", "Update",
  "Update",
  "Delete", "Delete", "Delete", "Delete", "Delete", "Delete", "Delete",
  "Delete"
};

extern PT_NODE *mq_fetch_attributes (PARSER_CONTEXT * parser,
				     PT_NODE * class_);

extern PT_NODE *mq_lambda (PARSER_CONTEXT * parser, PT_NODE * tree_with_names,
			   PT_NODE * name_node, PT_NODE * corresponding_tree);

extern PT_NODE *mq_class_lambda (PARSER_CONTEXT * parser, PT_NODE * statement,
				 PT_NODE * class_,
				 PT_NODE * corresponding_spec,
				 PT_NODE * class_where_part,
				 PT_NODE * class_check_part,
				 PT_NODE * class_group_by_part,
				 PT_NODE * class_having_part);

extern PT_NODE *mq_fix_derived_in_union (PARSER_CONTEXT * parser,
					 PT_NODE * statement,
					 UINTPTR spec_id);

extern PT_NODE *mq_fetch_subqueries (PARSER_CONTEXT * parser,
				     PT_NODE * class_);

extern PT_NODE *mq_fetch_subqueries_for_update (PARSER_CONTEXT * parser,
						PT_NODE * class_,
						PT_FETCH_AS fetch_as,
						DB_AUTH what_for);

extern PT_NODE *mq_rename_resolved (PARSER_CONTEXT * parser, PT_NODE * spec,
				    PT_NODE * statement, const char *newname);

extern PT_NODE *mq_reset_ids_and_references (PARSER_CONTEXT * parser,
					     PT_NODE * statement,
					     PT_NODE * spec);

extern PT_NODE *mq_reset_ids_and_references_helper (PARSER_CONTEXT * parser,
						    PT_NODE * statement,
						    PT_NODE * spec,
						    bool
						    get_spec_referenced_attr);

extern PT_NODE *mq_push_path (PARSER_CONTEXT * parser, PT_NODE * statement,
			      PT_NODE * spec, PT_NODE * path);

extern PT_NODE *mq_derived_path (PARSER_CONTEXT * parser, PT_NODE * statement,
				 PT_NODE * path);
extern PT_NODE *mq_bump_correlation_level (PARSER_CONTEXT * parser,
					   PT_NODE * node, int increment,
					   int match);

extern int mq_updatable (PARSER_CONTEXT * parser, PT_NODE * statement);

extern PT_NODE *mq_translate (PARSER_CONTEXT * parser, PT_NODE * node);

#endif /* _VIEW_TRANSFORM_H_ */
