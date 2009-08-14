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
 * schema_template.h - Definitions for the schema template interface
 */

#ifndef _SCHEMA_TEMPLATE_H_
#define _SCHEMA_TEMPLATE_H_

#ident "$Id$"

#include "work_space.h"
#include "class_object.h"

/* Template creation */
extern SM_TEMPLATE *smt_def_class (const char *name);
extern SM_TEMPLATE *smt_edit_class_mop (MOP class_);
extern SM_TEMPLATE *smt_edit_class (const char *name);
extern int smt_quit (SM_TEMPLATE * template_);

/* Virtual class support */
extern SM_TEMPLATE *smt_def_typed_class (const char *name, SM_CLASS_TYPE ct);
extern SM_CLASS_TYPE smt_get_class_type (SM_TEMPLATE * template_);

/* Attribute definition */
extern int smt_add_attribute_w_dflt (DB_CTMPL * def,
				     const char *name,
				     const char *domain_string,
				     DB_DOMAIN * domain,
				     DB_VALUE * default_value,
				     SM_NAME_SPACE name_space);

extern int smt_add_attribute_any (SM_TEMPLATE * template_,
				  const char *name,
				  const char *domain_string,
				  DB_DOMAIN * domain,
				  SM_NAME_SPACE name_space);

extern int smt_add_attribute (SM_TEMPLATE * template_,
			      const char *name,
			      const char *domain_string, DB_DOMAIN * domain);
extern int smt_add_shared_attribute (SM_TEMPLATE * template_,
				     const char *name,
				     const char *domain_string,
				     DB_DOMAIN * domain);
extern int smt_add_class_attribute (SM_TEMPLATE * template_,
				    const char *name,
				    const char *domain_string,
				    DB_DOMAIN * domain);

extern int smt_add_set_attribute_domain (SM_TEMPLATE * template_,
					 const char *name,
					 int class_attribute,
					 const char *domain_string,
					 DB_DOMAIN * domain);

extern int smt_delete_set_attribute_domain (SM_TEMPLATE * template_,
					    const char *name,
					    int class_attribute,
					    const char *domain_string,
					    DB_DOMAIN * domain);

extern int smt_reset_attribute_domain (SM_TEMPLATE * template_,
				       const char *name, int class_attribute);

extern int smt_set_attribute_default (SM_TEMPLATE * template_,
				      const char *name,
				      int class_attribute, DB_VALUE * value);

extern int smt_add_constraint (SM_TEMPLATE * template_,
			       const char **att_names, const int *asc_desc,
			       const char *name, int class_attribute,
			       SM_ATTRIBUTE_FLAG constraint,
			       SM_FOREIGN_KEY_INFO * fk_info,
			       char *shared_cons_name);

extern int smt_drop_constraint (SM_TEMPLATE * template_,
				const char **att_names,
				const char *constraint_name,
				int class_attribute,
				SM_ATTRIBUTE_FLAG constraint);

extern int smt_add_index (SM_TEMPLATE * template_, const char *name,
			  int on_or_off);

extern int smt_find_attribute (SM_TEMPLATE * template_, const char *name,
			       int class_attribute, SM_ATTRIBUTE ** attp);

/* Method definition */
extern int smt_add_method_any (SM_TEMPLATE * template_, const char *name,
			       const char *implementation,
			       SM_NAME_SPACE name_space);

extern int smt_add_method (SM_TEMPLATE * template_, const char *name,
			   const char *implementation);
extern int smt_add_class_method (SM_TEMPLATE * template_, const char *name,
				 const char *implementation);

extern int smt_change_method_implementation (SM_TEMPLATE * template_,
					     const char *name,
					     int class_method,
					     const char *implementation);

extern int smt_assign_argument_domain (SM_TEMPLATE * template_,
				       const char *name,
				       int class_method,
				       const char *implementation,
				       int index,
				       const char *domain_string,
				       DB_DOMAIN * domain);

extern int smt_add_set_argument_domain (SM_TEMPLATE * template_,
					const char *name,
					int class_method,
					const char *implementation,
					int index,
					const char *domain_string,
					DB_DOMAIN * domain);

/* Rename functions */
extern int smt_rename_any (SM_TEMPLATE * template_, const char *name,
			   int class_namespace, const char *new_name);

/* Deletion functions */
extern int smt_delete_any (SM_TEMPLATE * template_, const char *name,
			   SM_NAME_SPACE name_space);
extern int smt_delete (SM_TEMPLATE * template_, const char *name);
extern int smt_class_delete (SM_TEMPLATE * template_, const char *name);

/* Superclass functions */
extern int smt_add_super (SM_TEMPLATE * template_, MOP super_class);
extern int smt_delete_super (SM_TEMPLATE * template_, MOP super_class);
extern int smt_delete_super_connect (SM_TEMPLATE * template_,
				     MOP super_class);

/* Method file functions */
extern int smt_add_method_file (SM_TEMPLATE * template_,
				const char *filename);
extern int smt_drop_method_file (SM_TEMPLATE * template_,
				 const char *filename);
extern int smt_reset_method_files (SM_TEMPLATE * template_);

extern int smt_rename_method_file (SM_TEMPLATE * template_,
				   const char *old_name,
				   const char *new_name);

extern int smt_set_loader_commands (SM_TEMPLATE * template_,
				    const char *commands);

/* Resolution functions */
extern int smt_add_resolution (SM_TEMPLATE * template_, MOP super_class,
			       const char *name, const char *alias);

extern int smt_add_class_resolution (SM_TEMPLATE * template_,
				     MOP super_class,
				     const char *name, const char *alias);

extern int smt_delete_resolution (SM_TEMPLATE * template_, MOP super_class,
				  const char *name);
extern int smt_delete_class_resolution (SM_TEMPLATE * template_,
					MOP super_class, const char *name);

/* Query_spec functions */
extern int smt_add_query_spec (SM_TEMPLATE * template_,
			       const char *specification);
extern int smt_drop_query_spec (SM_TEMPLATE * template_, const int index);
extern int smt_reset_query_spec (SM_TEMPLATE * template_);
extern int smt_change_query_spec (SM_TEMPLATE * def, const char *query,
				  const int index);

#if defined(ENABLE_UNUSED_FUNCTION)
extern void smt_downcase_all_class_info (void);
#endif

/* Object_id functions */
extern int smt_set_object_id (SM_TEMPLATE * template_, DB_NAMELIST * id_list);

#endif /* _SCHEMA_TEMPLATE_H_ */
