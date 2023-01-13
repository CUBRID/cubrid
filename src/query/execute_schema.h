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
 * execute_schema.h - This file contains do_class and do_partition extern prototypes.
 */

#ifndef _EXECUTE_SCHEMA_H_
#define _EXECUTE_SCHEMA_H_

#ident "$Id$"

#if defined (SERVER_MODE)
#error Does not belong to server module
#endif /* SERVER_MODE */

#include "dbi.h"
#include "schema_manager.h"

#define UNIQUE_PARTITION_SAVEPOINT_GRANT "pARTITIONgRANT"
#define UNIQUE_PARTITION_SAVEPOINT_REVOKE "pARTITIONrEVOKE"
#define UNIQUE_PARTITION_SAVEPOINT_RENAME "pARTITIONrENAME"
#define UNIQUE_PARTITION_SAVEPOINT_DROP "pARTITIONdROP"
#define UNIQUE_PARTITION_SAVEPOINT_OWNER "pARTITIONoWNER"
#define UNIQUE_PARTITION_SAVEPOINT_INDEX "pARTITIONiNDEX"
#define UNIQUE_PARTITION_SAVEPOINT_ALTER "pARTITIONaLTER"
#define PARTITION_CATALOG_CLASS "_db_partition"
#define PARTITION_VARCHAR_LEN (DB_MAX_IDENTIFIER_LENGTH)
#define CLASS_ATT_NAME "class_name"
#define CLASS_IS_PARTITION "partition_of"

#define CHECK_PARTITION_NONE    0x0000
#define CHECK_PARTITION_PARENT  0x0001
#define CHECK_PARTITION_SUBS    0x0010

typedef struct client_update_info CLIENT_UPDATE_INFO;
typedef struct client_update_class_info CLIENT_UPDATE_CLASS_INFO;
/* Class info structure used in update execution on client */
struct client_update_class_info
{
  PT_NODE *spec;		/* PT_SPEC node of the class */
  DB_VALUE *oid;		/* OID value of the current(last) tuple that is updated */
  PT_NODE *check_where;		/* check option expression or NULL */
  SM_CLASS *smclass;		/* primary class structure */
  DB_OBJECT *class_mop;		/* object associated with this class */
  int pruning_type;		/* pruning type */
  CLIENT_UPDATE_INFO *first_assign;	/* first assignment of this class */
};

/* Assignment info used in update execution on client */
struct client_update_info
{
  PT_NODE *upd_col_name;	/* PT_NAME of attribute to be updated */
  DB_VALUE *db_val;		/* value to be assigned */
  bool is_const;		/* true if value to be assigned is constant (at compilation) */
  DB_ATTDESC *attr_desc;	/* description of attribute to be updated */
  CLIENT_UPDATE_CLASS_INFO *cls_info;	/* attribute owner class info */
  CLIENT_UPDATE_INFO *next;	/* next assignment from the same class */
};

extern int do_drop_partitioned_class (MOP class_, int drop_sub_flag, bool is_cascade_constraints);
extern int do_is_partitioned_subclass (int *is_partitioned, const char *classname, char *keyattr);
extern int do_get_partition_parent (DB_OBJECT * const classop, MOP * const parentop);
extern int do_rename_partition (MOP old_class, const char *newname);
extern int do_check_partitioned_class (DB_OBJECT * classop, int check_map, char *keyattr);
extern int do_get_partition_keycol (char *keycol, MOP class_);
extern int do_get_partition_size (MOP class_);
extern int do_drop_partition_list (MOP class_, PT_NODE * name_list, DB_CTMPL * tmpl);

extern int do_add_queries (PARSER_CONTEXT * parser, DB_CTMPL * ctemplate, const PT_NODE * queries);
extern int do_add_attributes (PARSER_CONTEXT * parser, DB_CTMPL * ctemplate, PT_NODE * atts, PT_NODE * constraints,
			      DB_QUERY_TYPE * create_select_columns);
extern int do_add_constraints (DB_CTMPL * ctemplate, PT_NODE * constraints);
extern int do_check_fk_constraints (DB_CTMPL * ctemplate, PT_NODE * constraints);
extern int do_add_methods (PARSER_CONTEXT * parser, DB_CTMPL * ctemplate, PT_NODE * methods);
extern int do_add_method_files (const PARSER_CONTEXT * parser, DB_CTMPL * ctemplate, PT_NODE * method_files);
extern int do_add_resolutions (const PARSER_CONTEXT * parser, DB_CTMPL * ctemplate, const PT_NODE * resolution);
extern int do_add_supers (const PARSER_CONTEXT * parser, DB_CTMPL * ctemplate, const PT_NODE * supers);
extern int do_set_object_id (const PARSER_CONTEXT * parser, DB_CTMPL * ctemplate, PT_NODE * object_id_list);
extern int do_create_local (PARSER_CONTEXT * parser, DB_CTMPL * ctemplate, PT_NODE * pt_node,
			    DB_QUERY_TYPE * create_select_columns);
extern int do_create_entity (PARSER_CONTEXT * parser, PT_NODE * node);
extern int do_check_rows_for_null (MOP class_mop, const char *att_name, bool * has_nulls);

extern int do_recreate_func_index_constr (PARSER_CONTEXT * parser, SM_CONSTRAINT_INFO * constr,
					  SM_FUNCTION_INFO * func_index_info, PT_NODE * alter, const char *src_cls_name,
					  const char *new_cls_name);
extern int do_recreate_filter_index_constr (PARSER_CONTEXT * parser, SM_PREDICATE_INFO * filter_index_info,
					    PT_NODE * alter, const char *src_cls_name, const char *new_cls_name);

extern int init_update_data (PARSER_CONTEXT * parser, PT_NODE * statement, CLIENT_UPDATE_INFO ** assigns_data,
                               int *assigns_count, CLIENT_UPDATE_CLASS_INFO ** cls_data, int *cls_count,
                               DB_VALUE ** values, int *values_cnt, bool has_delete);

#endif /* _EXECUTE_SCHEMA_H_ */
