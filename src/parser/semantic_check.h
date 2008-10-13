/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * pt_seman.h - contains semantic check prototypes
 */

#ifndef _PT_SEMAN_H_
#define _PT_SEMAN_H_

#ident "$Id$"


extern int pt_class_assignable (PARSER_CONTEXT * parser,
				const PT_NODE * d_class,
				const PT_NODE * s_class);

extern PT_NODE *pt_type_cast_vclass_query_spec_column (PARSER_CONTEXT *
						       parser, PT_NODE * attr,
						       PT_NODE * col);

extern PT_NODE *pt_check_union_compatibility (PARSER_CONTEXT * parser,
					      PT_NODE * node,
					      PT_NODE * attrds);

extern PT_NODE *pt_semantic_check (PARSER_CONTEXT * parser,
				   PT_NODE * statement);

extern PT_NODE *pt_invert (PARSER_CONTEXT * parser,
			   PT_NODE * name_expr, PT_NODE * result);

extern PT_NODE *pt_find_attr_def (const PT_NODE * attr_def_list,
				  const PT_NODE * name);
extern PT_NODE *pt_find_cnstr_def (const PT_NODE * cnstr_def_list,
				   const PT_NODE * name);

extern PT_NODE *pt_insert_entity (PARSER_CONTEXT * parser, PT_NODE * node,
				  PT_NODE * prev_entity,
				  PT_NODE * correlation_spec);
extern PT_NODE *pt_find_class_of_index (PARSER_CONTEXT * parser,
					PT_NODE * index,
					DB_CONSTRAINT_TYPE type);
extern int pt_has_text_domain (PARSER_CONTEXT * parser,
			       DB_ATTRIBUTE * attribute);
extern PT_NODE *pt_find_order_value_in_list (PARSER_CONTEXT * parser,
					     const PT_NODE * sort_value,
					     const PT_NODE * order_list);

#endif /* _PT_SEMAN_H_ */
