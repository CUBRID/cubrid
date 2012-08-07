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
 * object_representation_sr.h - Definitions for the server side functions
 *          that extract class information from disk representations
 */

#ifndef _OBJECT_REPRESENTATION_SR_H_
#define _OBJECT_REPRESENTATION_SR_H_

#ident "$Id$"

#include "storage_common.h"
#include "system_catalog.h"

#define OR_ATT_BTID_PREALLOC 8

typedef struct or_default_value OR_DEFAULT_VALUE;
struct or_default_value
{
  /* could this be converted to a server side DB_VALUE ? */
  void *value;			/* default value */
  int val_length;		/* default value length */
  DB_DEFAULT_EXPR_TYPE default_expr;	/* identifier for the pseudo-column
					 * default expression */
};

/*
 * OR_ATTRIBUTE
 *
 *    Server side memory representation of an attribute definition.
 *    Built from the disk representation of a class.
 *    Part of the OR_CLASSREP structure hierarchy.
 */

typedef struct or_attribute OR_ATTRIBUTE;
struct or_attribute
{
  OR_ATTRIBUTE *next;

  int id;			/* unique id */
  DB_TYPE type;			/* basic type */
  int def_order;		/* order of attributes in class */
  int location;			/* fixed offset or variable table index */
  int position;			/* storage position (list index) */
  OID classoid;			/* source class object id */

  OR_DEFAULT_VALUE default_value;	/* default value */
  OR_DEFAULT_VALUE current_default_value;	/* default value */
  BTID *btids;			/* B-tree ID's for indexes and constraints */
  TP_DOMAIN *domain;		/* full domain of this attribute */
  int n_btids;			/* Number of ID's in the btids array */
  BTID index;			/* btree id if indexed */

  /* local array of btid's to use  if possible */
  int max_btids;		/* Size of the btids array */
  BTID btid_pack[OR_ATT_BTID_PREALLOC];

  OID serial_obj;		/* db_serial's instance */
  unsigned is_fixed:1;		/* non-zero if this is a fixed width attribute */
  unsigned is_autoincrement:1;	/* non-zero if att is auto increment att */
  unsigned is_notnull:1;	/* non-zero if has not null constraint */
};

typedef enum
{
  BTREE_UNIQUE,
  BTREE_INDEX,
  BTREE_REVERSE_UNIQUE,
  BTREE_REVERSE_INDEX,
  BTREE_PRIMARY_KEY,
  BTREE_FOREIGN_KEY
} BTREE_TYPE;

typedef struct or_foreign_key OR_FOREIGN_KEY;
struct or_foreign_key
{
  OR_FOREIGN_KEY *next;		/* for pk */
  char *fkname;			/* foreign key name */
  OID ref_class_oid;
  OID self_oid;
  BTID ref_class_pk_btid;
  BTID self_btid;
  int del_action;
  int upd_action;
  int cache_attr_id;
  bool is_cache_obj;
};

typedef struct or_predicate OR_PREDICATE;
struct or_predicate
{
  char *pred_string;
  char *pred_stream;		/* CREATE INDEX ... WHERE filter_predicate */
  int pred_stream_size;
};

typedef struct or_function_index OR_FUNCTION_INDEX;
struct or_function_index
{
  char *expr_string;
  char *expr_stream;
  int expr_stream_size;
  int col_id;			/* the position at which the function is placed */
  int attr_index_start;		/* the index from where the attributes involved
				 * in a function are placed in OR_INDEX atts 
				 * member */
};

typedef struct or_index OR_INDEX;
struct or_index
{
  OR_INDEX *next;
  OR_ATTRIBUTE **atts;		/* Array of associated attributes */
  int *asc_desc;		/* array of ascending / descending */
  int *attrs_prefix_length;	/* prefix length */
  char *btname;			/* index( or constraint) name */
  OR_PREDICATE *filter_predicate;	/* CREATE INDEX idx ON tbl(col, ...) WHERE filter_predicate */
  OR_FUNCTION_INDEX *func_index_info;	/* function index information */
  OR_FOREIGN_KEY *fk;
  BTREE_TYPE type;		/* btree type */
  int n_atts;			/* Number of associated attributes */
  BTID btid;			/* btree ID */
};

typedef struct or_classrep OR_CLASSREP;
struct or_classrep
{
  OR_CLASSREP *next;

  OR_ATTRIBUTE *attributes;	/* list of attributes */
  OR_ATTRIBUTE *shared_attrs;	/* list of shared attributes */
  OR_ATTRIBUTE *class_attrs;	/* list of class attributes */
  OR_INDEX *indexes;		/* list of BTIDs for this class */

  REPR_ID id;			/* representation id */

  int fixed_length;		/* total size of the fixed width attributes */
  int n_attributes;		/* size of attribute array */
  int n_variable;		/* number of variable width attributes */
  int n_shared_attrs;		/* number of shared attributes */
  int n_class_attrs;		/* number of class attributes */
  int n_indexes;		/* number of indexes */

  unsigned needs_indexes:1;	/* flag indicating if indexes were not loaded */
};

typedef struct or_class OR_CLASS;
struct or_class
{
  OID *superclasses;
  OID *subclasses;
  int n_superclasses;
  int n_subclasses;

  OR_CLASSREP *representations;
  OID statistics;		/* object containing statistics */
};

typedef struct or_partition OR_PARTITION;
struct or_partition
{
  OID db_part_oid;		/* OID of the record from _db_partition class */
  OID class_oid;		/* class OID */
  HFID class_hfid;		/* class HFID */
  int partition_type;		/* partition type (range, list, hash) */
  DB_SEQ *values;		/* values for range and list partition types */
};

extern int or_class_repid (RECDES * record);
extern void or_class_hfid (RECDES * record, HFID * hfid);
#if defined (ENABLE_UNUSED_FUNCTION)
extern void or_class_statistics (RECDES * record, OID * oid);
extern int or_class_subclasses (RECDES * record,
				int *array_size, OID ** array_ptr);
extern int or_classrep_needs_indexes (OR_CLASSREP * rep);
#endif
extern int or_get_unique_hierarchy (THREAD_ENTRY * thread_p, RECDES * record,
				    int attrid,
				    BTID * btid,
				    OID ** class_oids,
				    HFID ** hfids, int *num_heaps);
extern OR_CLASSREP *or_get_classrep (RECDES * record, int repid);
extern OR_CLASSREP *or_get_classrep_noindex (RECDES * record, int repid);
extern OR_CLASSREP *or_classrep_load_indexes (OR_CLASSREP * rep,
					      RECDES * record);
extern int or_class_get_partition_info (RECDES * record,
					OID * partition_info);
extern void or_free_classrep (OR_CLASSREP * rep);
extern const char *or_get_attrname (RECDES * record, int attrid);
extern OR_CLASS *or_get_class (RECDES * record);
extern void or_free_class (OR_CLASS * class_);

/* OLD STYLE INTERFACE */
extern int orc_class_repid (RECDES * record);
extern void orc_class_hfid_from_record (RECDES * record, HFID * hfid);
extern DISK_REPR *orc_diskrep_from_record (THREAD_ENTRY * thread_p,
					   RECDES * record);
extern void orc_free_diskrep (DISK_REPR * rep);
extern CLS_INFO *orc_class_info_from_record (RECDES * record);
extern void orc_free_class_info (CLS_INFO * info);
extern int orc_subclasses_from_record (RECDES * record,
				       int *array_size, OID ** array_ptr);
extern int orc_superclasses_from_record (RECDES * record, int *array_size,
					 OID ** array_ptr);
extern OR_CLASSREP **or_get_all_representation (RECDES * record,
						bool do_indexes, int *count);
#endif /* _OBJECT_REPRESENTATION_SR_H_ */
