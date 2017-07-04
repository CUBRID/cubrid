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
 * view_transform.h - view (virtual class) transformation
 */

#ifndef _VIEW_TRANSFORM_H_
#define _VIEW_TRANSFORM_H_

#ident "$Id$"

#include "parser.h"
#include "schema_manager.h"

typedef enum PT_FETCH_AS
{ PT_NORMAL_SELECT, PT_INVERTED_ASSIGNMENTS, PT_PARTIAL_SELECT }
PT_FETCH_AS;

enum pt_updatability
{
  PT_NOT_UPDATABLE = 0x0,	/* non-updatable query */
  PT_PARTIALLY_UPDATABLE = 0x1,	/* partially updatable query (i.e. vclass whose definition contains joins, but is
				 * otherwise updatable) */
  PT_UPDATABLE = 0x3		/* fully updatable query */
};
typedef enum pt_updatability PT_UPDATABILITY;

extern PT_NODE *mq_bump_correlation_level (PARSER_CONTEXT * parser, PT_NODE * node, int increment, int match);

extern PT_UPDATABILITY mq_updatable (PARSER_CONTEXT * parser, PT_NODE * statement);

extern PT_NODE *mq_translate (PARSER_CONTEXT * parser, PT_NODE * volatile node);

extern PT_NODE *mq_make_derived_spec (PARSER_CONTEXT * parser, PT_NODE * node, PT_NODE * subquery, int *idx,
				      PT_NODE ** spec_ptr, PT_NODE ** attr_list_ptr);
extern PT_NODE *mq_oid (PARSER_CONTEXT * parser, PT_NODE * spec);

extern PT_NODE *mq_get_references (PARSER_CONTEXT * parser, PT_NODE * statement, PT_NODE * spec);
extern PT_NODE *mq_get_references_helper (PARSER_CONTEXT * parser, PT_NODE * statement, PT_NODE * spec,
					  bool get_spec_referenced_attr);
extern PT_NODE *mq_reset_paths (PARSER_CONTEXT * parser, PT_NODE * statement, PT_NODE * root_spec);
extern PT_NODE *mq_reset_ids (PARSER_CONTEXT * parser, PT_NODE * statement, PT_NODE * spec);
extern PT_NODE *mq_clear_ids (PARSER_CONTEXT * parser, PT_NODE * node, PT_NODE * spec);

extern PT_NODE *mq_set_references (PARSER_CONTEXT * parser, PT_NODE * statement, PT_NODE * spec);

extern bool mq_is_outer_join_spec (PARSER_CONTEXT * parser, PT_NODE * spec);

extern bool mq_is_updatable (DB_OBJECT * vclass_object);
extern bool mq_is_updatable_strict (DB_OBJECT * vclass_object);

extern bool mq_is_updatable_attribute (DB_OBJECT * vclass, const char *attr_name, DB_OBJECT * base_class);

extern bool mq_is_updatable_att (PARSER_CONTEXT * parser, DB_OBJECT * vmop, const char *attr_name, DB_OBJECT * rmop);

extern int mq_get_attribute (DB_OBJECT * vclass, const char *attr_name, DB_OBJECT * base_class,
			     DB_VALUE * virtual_value, DB_OBJECT * base_instance);

extern int mq_update_attribute (DB_OBJECT * vclass, const char *attr_name, DB_OBJECT * base_class,
				DB_VALUE * virtual_value, DB_VALUE * base_value, const char **base_name, int db_auth);

extern DB_OBJECT *mq_fetch_one_real_class (DB_OBJECT * vclass);


extern int mq_evaluate_expression (PARSER_CONTEXT * parser, PT_NODE * expr, DB_VALUE * value, DB_OBJECT * object,
				   UINTPTR spec_id);

extern int mq_evaluate_expression_having_serial (PARSER_CONTEXT * parser, PT_NODE * expr, DB_VALUE * values,
						 int values_count, DB_OBJECT * object, UINTPTR spec_id);

extern int mq_evaluate_check_option (PARSER_CONTEXT * parser, PT_NODE * expr, DB_OBJECT * object, PT_NODE * view_class);

extern int mq_get_expression (DB_OBJECT * object, const char *expr, DB_VALUE * value);

extern PT_NODE *mq_reset_ids_in_statement (PARSER_CONTEXT * parser, PT_NODE * statement);

extern PT_NODE *mq_reset_ids_in_methods (PARSER_CONTEXT * parser, PT_NODE * statement);

extern PT_NODE *mq_rewrite_aggregate_as_derived (PARSER_CONTEXT * parser, PT_NODE * agg_sel);

extern PT_NODE *mq_rewrite_query_as_derived (PARSER_CONTEXT * parser, PT_NODE * query);
#endif /* _VIEW_TRANSFORM_H_ */
