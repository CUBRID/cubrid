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
 * schema_template.h - Definitions for the schema template interface
 */

#ifndef _SCHEMA_TEMPLATE_H_
#define _SCHEMA_TEMPLATE_H_

#ident "$Id$"

#if defined (SERVER_MODE)
#error Does not belong to server module
#endif /* defined (SERVER_MODE) */

#include "work_space.h"
#include "class_object.h"

/* Template creation */
extern SM_TEMPLATE *smt_def_class (const char *name);
extern SM_TEMPLATE *smt_edit_class_mop (MOP class_, DB_AUTH db_auth_type);
#if defined(ENABLE_UNUSED_FUNCTION)
extern SM_TEMPLATE *smt_edit_class (const char *name);
#endif /* ENABLE_UNUSED_FUNCTION */
extern SM_TEMPLATE *smt_copy_class_mop (const char *name, MOP op, SM_CLASS ** class_);
extern SM_TEMPLATE *smt_copy_class (const char *new_name, const char *existing_name, SM_CLASS ** class_);
extern int smt_quit (SM_TEMPLATE * template_);

/* Virtual class support */
extern SM_TEMPLATE *smt_def_typed_class (const char *name, SM_CLASS_TYPE ct);
extern SM_CLASS_TYPE smt_get_class_type (SM_TEMPLATE * template_);

/* Attribute definition */
extern int smt_add_attribute_w_dflt (DB_CTMPL * def, const char *name, const char *domain_string, DB_DOMAIN * domain,
				     DB_VALUE * default_value, const SM_NAME_SPACE name_space,
				     DB_DEFAULT_EXPR * default_expr, DB_DEFAULT_EXPR_TYPE * on_update,
				     const char *comment);

extern int smt_add_attribute_w_dflt_w_order (DB_CTMPL * def, const char *name, const char *domain_string,
					     DB_DOMAIN * domain, DB_VALUE * default_value,
					     const SM_NAME_SPACE name_space, const bool add_first,
					     const char *add_after_attribute, DB_DEFAULT_EXPR * default_expr,
					     DB_DEFAULT_EXPR_TYPE * on_update, const char *comment);

extern int smt_add_attribute_any (SM_TEMPLATE * template_, const char *name, const char *domain_string,
				  DB_DOMAIN * domain, const SM_NAME_SPACE name_space, const bool add_first,
				  const char *add_after_attribute, const char *comment);

extern int smt_add_attribute (SM_TEMPLATE * template_, const char *name, const char *domain_string, DB_DOMAIN * domain);

extern int smt_add_set_attribute_domain (SM_TEMPLATE * template_, const char *name, int class_attribute,
					 const char *domain_string, DB_DOMAIN * domain);

extern int smt_delete_set_attribute_domain (SM_TEMPLATE * template_, const char *name, int class_attribute,
					    const char *domain_string, DB_DOMAIN * domain);

extern int smt_reset_attribute_domain (SM_TEMPLATE * template_, const char *name, int class_attribute);

extern int smt_set_attribute_default (SM_TEMPLATE * template_, const char *name, int class_attribute, DB_VALUE * value,
				      DB_DEFAULT_EXPR * default_expr);

extern int smt_set_attribute_on_update (SM_TEMPLATE * template_, const char *name, int class_attribute,
					DB_DEFAULT_EXPR_TYPE on_update);

extern int smt_add_constraint (SM_TEMPLATE * template_, DB_CONSTRAINT_TYPE constraint_type, const char *constraint_name,
			       const char **att_names, const int *asc_desc, const int *attr_prefix_length,
			       int class_attribute, SM_FOREIGN_KEY_INFO * fk_info, SM_PREDICATE_INFO * filter_index,
			       SM_FUNCTION_INFO * function_index, const char *comment, SM_INDEX_STATUS index_status);

extern int smt_drop_constraint (SM_TEMPLATE * template_, const char **att_names, const char *constraint_name,
				int class_attribute, SM_ATTRIBUTE_FLAG constraint);

extern int smt_find_attribute (SM_TEMPLATE * template_, const char *name, int class_attribute, SM_ATTRIBUTE ** attp);

/* Method definition */
extern int smt_add_method_any (SM_TEMPLATE * template_, const char *name, const char *implementation,
			       SM_NAME_SPACE name_space);

extern int smt_add_method (SM_TEMPLATE * template_, const char *name, const char *implementation);
extern int smt_add_class_method (SM_TEMPLATE * template_, const char *name, const char *implementation);

extern int smt_change_method_implementation (SM_TEMPLATE * template_, const char *name, int class_method,
					     const char *implementation);

extern int smt_assign_argument_domain (SM_TEMPLATE * template_, const char *name, int class_method,
				       const char *implementation, int index, const char *domain_string,
				       DB_DOMAIN * domain);

extern int smt_add_set_argument_domain (SM_TEMPLATE * template_, const char *name, int class_method,
					const char *implementation, int index, const char *domain_string,
					DB_DOMAIN * domain);

/* Rename functions */
extern int smt_rename_any (SM_TEMPLATE * template_, const char *name, const bool class_namespace, const char *new_name);

#if defined (ENABLE_RENAME_CONSTRAINT)
extern int smt_rename_constraint (SM_TEMPLATE * ctemplate, const char *old_name, const char *new_name,
				  SM_CONSTRAINT_FAMILY element_type);
#endif

/* Change comment function */
extern int smt_change_constraint_comment (SM_TEMPLATE * ctemplate, const char *index_name, const char *comment);

/* Change index status function */
extern int smt_change_constraint_status (SM_TEMPLATE * ctemplate, const char *index_name, SM_INDEX_STATUS index_status);

/* Deletion functions */
extern int smt_delete_any (SM_TEMPLATE * template_, const char *name, SM_NAME_SPACE name_space);
#if defined(ENABLE_UNUSED_FUNCTION)
extern int smt_delete (SM_TEMPLATE * template_, const char *name);
extern int smt_class_delete (SM_TEMPLATE * template_, const char *name);
#endif

/* Superclass functions */
extern int smt_add_super (SM_TEMPLATE * template_, MOP super_class);
extern int smt_delete_super (SM_TEMPLATE * template_, MOP super_class);
extern int smt_delete_super_connect (SM_TEMPLATE * template_, MOP super_class);

/* Method file functions */
extern int smt_add_method_file (SM_TEMPLATE * template_, const char *filename);
extern int smt_drop_method_file (SM_TEMPLATE * template_, const char *filename);
extern int smt_reset_method_files (SM_TEMPLATE * template_);

extern int smt_rename_method_file (SM_TEMPLATE * template_, const char *old_name, const char *new_name);

extern int smt_set_loader_commands (SM_TEMPLATE * template_, const char *commands);

/* Resolution functions */
extern int smt_add_resolution (SM_TEMPLATE * template_, MOP super_class, const char *name, const char *alias);

extern int smt_add_class_resolution (SM_TEMPLATE * template_, MOP super_class, const char *name, const char *alias);

extern int smt_delete_resolution (SM_TEMPLATE * template_, MOP super_class, const char *name);
extern int smt_delete_class_resolution (SM_TEMPLATE * template_, MOP super_class, const char *name);

/* Query_spec functions */
extern int smt_add_query_spec (SM_TEMPLATE * template_, const char *specification);
extern int smt_drop_query_spec (SM_TEMPLATE * template_, const int index);
extern int smt_reset_query_spec (SM_TEMPLATE * template_);
extern int smt_change_query_spec (SM_TEMPLATE * def, const char *query, const int index);
extern int smt_change_attribute_w_dflt_w_order (DB_CTMPL * def, const char *name, const char *new_name,
						const char *new_domain_string, DB_DOMAIN * new_domain,
						const SM_NAME_SPACE name_space, DB_VALUE * new_default_value,
						DB_DEFAULT_EXPR * new_def_expr, DB_DEFAULT_EXPR_TYPE new_on_update_expr,
						const bool change_first, const char *change_after_attribute,
						SM_ATTRIBUTE ** found_att);

extern int smt_check_index_exist (SM_TEMPLATE * template_, char **out_shared_cons_name,
				  DB_CONSTRAINT_TYPE constraint_type, const char *constraint_name,
				  const char **att_names, const int *asc_desc, const SM_PREDICATE_INFO * filter_index,
				  const SM_FUNCTION_INFO * function_index);

#if defined(ENABLE_UNUSED_FUNCTION)
extern void smt_downcase_all_class_info (void);
#endif

#if defined(SUPPORT_KEY_DUP_LEVEL)	// ctshim
extern int smt_make_hidden_attribute (SM_TEMPLATE * template_, MOP mop, SM_ATTRIBUTE ** attp);
#endif

#endif /* _SCHEMA_TEMPLATE_H_ */
