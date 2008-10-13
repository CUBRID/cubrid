/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * vtrans2.h - Header file for translation of virtual queries
 */

#ifndef _VTRANS2_H_
#define _VTRANS2_H_

#ident "$Id$"

#include "parser.h"
#include "schema_manager_3.h"

extern int mq_mget_exprs (DB_OBJECT ** objects, int rows,
			  char **exprs, int cols, int qOnErr,
			  DB_VALUE * values, int *results, char *emsg);

extern PT_NODE *mq_make_derived_spec (PARSER_CONTEXT * parser, PT_NODE * node,
				      PT_NODE * subquery, int *idx,
				      PT_NODE ** spec_ptr,
				      PT_NODE ** attr_list_ptr);
extern PT_NODE *mq_oid (PARSER_CONTEXT * parser, PT_NODE * spec);

extern void mq_insert_symbol (PARSER_CONTEXT * parser, PT_NODE ** listhead,
			      PT_NODE * attr);

extern PT_NODE *mq_get_references (PARSER_CONTEXT * parser,
				   PT_NODE * statement, PT_NODE * spec);
extern PT_NODE *mq_get_references_helper (PARSER_CONTEXT * parser,
					  PT_NODE * statement,
					  PT_NODE * spec,
					  bool get_spec_referenced_attr);
extern PT_NODE *mq_reset_paths (PARSER_CONTEXT * parser,
				PT_NODE * statement, PT_NODE * root_spec);
extern PT_NODE *mq_reset_ids (PARSER_CONTEXT * parser,
			      PT_NODE * statement, PT_NODE * spec);

extern PT_NODE *mq_set_references (PARSER_CONTEXT * parser,
				   PT_NODE * statement, PT_NODE * spec);

extern bool mq_is_updatable (DB_OBJECT * vclass_object);

extern bool mq_is_updatable_attribute (DB_OBJECT * vclass,
				       const char *attr_name,
				       DB_OBJECT * base_class);

extern bool mq_is_updatable_att (PARSER_CONTEXT * parser, DB_OBJECT * vmop,
				 const char *attr_name, DB_OBJECT * rmop);

extern int mq_get_attribute (DB_OBJECT * vclass,
			     const char *attr_name,
			     DB_OBJECT * base_class,
			     DB_VALUE * virtual_value,
			     DB_OBJECT * base_instance);

extern int mq_update_attribute (DB_OBJECT * vclass,
				const char *attr_name,
				DB_OBJECT * base_class,
				DB_VALUE * virtual_value,
				DB_VALUE * base_value,
				const char **base_name, int db_auth);

extern DB_OBJECT **mq_fetch_real_classes (DB_OBJECT * vclass);

extern DB_OBJECT *mq_fetch_one_real_class (DB_OBJECT * vclass);


extern int mq_evaluate_expression (PARSER_CONTEXT * parser, PT_NODE * expr,
				   DB_VALUE * value, DB_OBJECT * object,
				   UINTPTR spec_id);

extern int mq_evaluate_expression_having_serial (PARSER_CONTEXT * parser,
						 PT_NODE * expr,
						 DB_VALUE * value,
						 DB_OBJECT * object,
						 UINTPTR spec_id);

extern int mq_evaluate_check_option (PARSER_CONTEXT * parser,
				     PT_NODE * expr, DB_OBJECT * object,
				     PT_NODE * view_class);

extern int mq_get_expression (DB_OBJECT * object, const char *expr,
			      DB_VALUE * value);

extern PT_NODE *mq_reset_ids_in_statement (PARSER_CONTEXT * parser,
					   PT_NODE * statement);


#endif /* _VTRANS2_H_ */
