/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * do_partition.h - This file contains do_partition extern prototypes.
 */

#ifndef _DO_PARTITION_H_
#define _DO_PARTITION_H_

#ident "$Id$"

#include "qp_xasl.h"

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
  DB_VALUE *ptype, *pexpr, *pattr;
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

#endif /* _DO_PARTITION_H_ */
