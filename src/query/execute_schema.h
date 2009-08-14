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
 * execute_schema.h - This file contains do_class and do_partition extern prototypes.
 */

#ifndef _EXECUTE_SCHEMA_H_
#define _EXECUTE_SCHEMA_H_

#ident "$Id$"

#include "dbi.h"
#include "query_executor.h"

#define UNIQUE_PARTITION_SAVEPOINT_GRANT "pARTITIONgRANT"
#define UNIQUE_PARTITION_SAVEPOINT_REVOKE "pARTITIONrEVOKE"
#define UNIQUE_PARTITION_SAVEPOINT_RENAME "pARTITIONrENAME"
#define UNIQUE_PARTITION_SAVEPOINT_DROP "pARTITIONdROP"
#define UNIQUE_PARTITION_SAVEPOINT_OWNER "pARTITIONoWNER"
#define UNIQUE_PARTITION_SAVEPOINT_INDEX "pARTITIONiNDEX"
#define UNIQUE_PARTITION_SAVEPOINT_CREATE "pARTITIONcREATE"
#define UNIQUE_PARTITION_SAVEPOINT_ALTER "pARTITIONaLTER"
#define PARTITION_CATALOG_CLASS "_db_partition"
#define PARTITION_ATT_CLASSOF "class_of"
#define PARTITION_ATT_PNAME "pname"
#define PARTITION_ATT_PTYPE "ptype"
#define PARTITION_ATT_PEXPR "pexpr"
#define PARTITION_ATT_PVALUES "pvalues"
#define PARTITION_VARCHAR_LEN (DB_MAX_IDENTIFIER_LENGTH)
#define CLASS_ATT_NAME "class_name"
#define CLASS_IS_PARTITION "partition_of"
#define MAX_PARTITIONS 1024

#define CHECK_PARTITION_NONE    0x0000
#define CHECK_PARTITION_PARENT  0x0001
#define CHECK_PARTITION_SUBS    0x0010

enum
{
  NOT_PARTITION_CLASS = 0, PARTITIONED_CLASS = 1, PARTITION_CLASS = 2
};

typedef struct partition_insert_cache PARTITION_INSERT_CACHE;
struct partition_insert_cache
{
  PT_NODE *attr;
  DB_VALUE *val;
  DB_ATTDESC *desc;
  struct partition_insert_cache *next;
};

typedef struct partition_select_info PARTITION_SELECT_INFO;
struct partition_select_info
{
  DB_VALUE *ptype;
  DB_VALUE *pexpr;
  DB_VALUE *pattr;
  SM_CLASS *smclass;
};

extern int do_create_partition (PARSER_CONTEXT * parser, PT_NODE * node,
				DB_OBJECT * class_obj, DB_CTMPL * clstmpl);
extern void do_apply_partition_pruning (PARSER_CONTEXT * parser,
					PT_NODE * stmt);
extern int do_insert_partition_cache (PARTITION_INSERT_CACHE ** pic,
				      PT_NODE * attr, DB_ATTDESC * desc,
				      DB_VALUE * val);
extern void do_clear_partition_cache (PARTITION_INSERT_CACHE * pic);
extern int do_init_partition_select (MOP classobj,
				     PARTITION_SELECT_INFO ** psi);
extern int do_select_partition (PARTITION_SELECT_INFO * psi, DB_VALUE * val,
				MOP * retobj);
extern void do_clear_partition_select (PARTITION_SELECT_INFO * psi);
extern int do_build_partition_xasl (PARSER_CONTEXT * parser, XASL_NODE * xasl,
				    MOP class_obj, int idx);
extern int do_drop_partition (MOP class_, int drop_sub_flag);
extern MOP do_is_partition_changed (PARSER_CONTEXT * parser,
				    SM_CLASS * smclass, MOP editobj,
				    PT_NODE * list_column_names,
				    DB_VALUE * list_values,
				    PT_NODE * const_column_names,
				    DB_VALUE * const_values);
extern int do_is_partitioned_subclass (int *is_partitioned,
				       const char *classname, char *keyattr);
extern int do_is_partitioned_classobj (int *is_partition, DB_OBJECT * classop,
				       char *keyattr, MOP ** sub_partitions);
extern int do_rename_partition (MOP old_class, const char *newname);
extern int do_update_partition_newly (const char *classname,
				      const char *keyname);
extern int do_remove_partition_post (PARSER_CONTEXT * parser,
				     const char *classname,
				     const char *keyname);
extern int do_remove_partition_pre (DB_CTMPL * clstmpl, char *keyattr,
				    const char *magic_word);
extern int do_check_partitioned_class (DB_OBJECT * classop, int check_map,
				       char *keyattr);
extern int do_get_partition_keycol (char *keycol, MOP class_);
extern int do_get_partition_size (MOP class_);
extern int do_drop_partition_list (MOP class_, PT_NODE * name_list);

extern int do_add_queries (PARSER_CONTEXT * parser,
			   DB_CTMPL * ctemplate, const PT_NODE * queries);

extern int do_add_attributes (PARSER_CONTEXT * parser,
			      DB_CTMPL * ctemplate, PT_NODE * atts);

extern int do_add_constraints (DB_CTMPL * ctemplate, PT_NODE * constraints);

extern int do_add_methods (PARSER_CONTEXT * parser,
			   DB_CTMPL * ctemplate, PT_NODE * methods);

extern int do_add_method_files (const PARSER_CONTEXT * parser,
				DB_CTMPL * ctemplate, PT_NODE * method_files);

extern int do_add_resolutions (const PARSER_CONTEXT * parser,
			       DB_CTMPL * ctemplate,
			       const PT_NODE * resolution);

extern int do_add_supers (const PARSER_CONTEXT * parser,
			  DB_CTMPL * ctemplate, const PT_NODE * supers);

extern int do_add_foreign_key_objcache_attr (DB_CTMPL * ctemplate,
					     PT_NODE * constraints);

extern int do_set_object_id (const PARSER_CONTEXT * parser,
			     DB_CTMPL * ctemplate, PT_NODE * object_id_list);

extern int do_create_local (PARSER_CONTEXT * parser, DB_CTMPL * ctemplate,
			    PT_NODE * pt_node);

extern int do_create_entity (PARSER_CONTEXT * parser, PT_NODE * node);


#endif /* _EXECUTE_SCHEMA_H_ */
