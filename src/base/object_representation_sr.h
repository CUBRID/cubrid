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
 * object_representation_sr.h - Definitions for the server side functions
 *          that extract class information from disk representations
 */

#ifndef _OBJECT_REPRESENTATION_SR_H_
#define _OBJECT_REPRESENTATION_SR_H_

#ident "$Id$"

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#if !defined (__cplusplus)
#error C++ required
#endif // C++ required

#include "dbtype_def.h"
#include "log_lsa.hpp"
#include "mvcc.h"
#include "storage_common.h"
#include "system_catalog.h"

#include <atomic>

#define OR_ATT_BTID_PREALLOC 8

/* We can't have an attribute with default expression and default value simultaneously. */
typedef struct or_default_value OR_DEFAULT_VALUE;
struct or_default_value
{
  /* could this be converted to a server side DB_VALUE ? */
  void *value;			/* default value */
  int val_length;		/* default value length */
  DB_DEFAULT_EXPR default_expr;	/* default expression */

  // *INDENT-OFF*
  or_default_value ();
  // *INDENT-ON*
};

/*
 * OR_ATTRIBUTE
 *
 *    Server side memory representation of an attribute definition.
 *    Built from the disk representation of a class.
 *    Part of the OR_CLASSREP structure hierarchy.
 */
// *INDENT-OFF*
// work-around for windows compile error:
// error C2338: You've instantiated std::atomic<T> with sizeof(T) equal to 2/4/8 and alignof(T) < sizeof(T)
//
// it may be removed if support for VS versions older than 2015 is dropped.
union or_aligned_oid
{
  std::int64_t dummy_for_alignemnt;
  OID oid;

  or_aligned_oid () noexcept = default;

  or_aligned_oid (const OID & arg_oid)
    : oid (arg_oid)
  {
  }
};

struct or_auto_increment
{
  std::atomic<or_aligned_oid> serial_obj;
};
// *INDENT-ON*

typedef struct or_attribute OR_ATTRIBUTE;
struct or_attribute
{
  OR_ATTRIBUTE *next;		/* obsolete : use array for accessing elements */

  int id;			/* unique id */
  DB_TYPE type;			/* basic type */
  int def_order;		/* order of attributes in class */
  int location;			/* fixed offset or variable table index */
  int position;			/* storage position (list index) */
  OID classoid;			/* source class object id */

  DB_DEFAULT_EXPR_TYPE on_update_expr;	/* on update default expr type */
  OR_DEFAULT_VALUE default_value;	/* default value */
  OR_DEFAULT_VALUE current_default_value;	/* default value */
  BTID *btids;			/* B-tree ID's for indexes and constraints */
  TP_DOMAIN *domain;		/* full domain of this attribute */

  or_auto_increment auto_increment;

  int n_btids;			/* Number of ID's in the btids array */
  BTID index;			/* btree id if indexed */

  /* local array of btid's to use if possible */
  int max_btids;		/* Size of the btids array */
  BTID btid_pack[OR_ATT_BTID_PREALLOC];

  unsigned is_fixed:1;		/* non-zero if this is a fixed width attribute */
  unsigned is_autoincrement:1;	/* non-zero if att is auto increment att */
  unsigned is_notnull:1;	/* non-zero if has not null constraint */

// *INDENT-OFF*
  or_attribute ();

  void initialize_values ();    // initialize values an object that was allocated without construction
  // do not call on properly constructed objects
// *INDENT-ON*
};

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
  int attr_index_start;		/* the index from where the attributes involved in a function are placed in OR_INDEX
				 * atts member */
};

typedef enum
{
  OR_NO_INDEX = 0,
  OR_NORMAL_INDEX = 1,
  OR_INVISIBLE_INDEX = 2,
  OR_ONLINE_INDEX_BUILDING_IN_PROGRESS = 3,

  OR_RESERVED_INDEX_STATUS1 = 4,
  OR_RESERVED_INDEX_STATUS2 = 5,
  OR_RESERVED_INDEX_STATUS3 = 6,
  OR_RESERVED_INDEX_STATUS4 = 7,
  OR_RESERVED_INDEX_STATUS5 = 8,
  OR_RESERVED_INDEX_STATUS6 = 9,
  OR_LAST_INDEX_STATUS = 10
} OR_INDEX_STATUS;

typedef struct or_index OR_INDEX;
struct or_index
{
  OR_INDEX *next;		/* obsolete : use array for accessing elements */
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
  OR_INDEX_STATUS index_status;
};

typedef struct or_partition OR_PARTITION;
struct or_partition
{
  OID class_oid;		/* class OID */
  HFID class_hfid;		/* class HFID */
  int partition_type;		/* partition type (range, list, hash) */
  REPR_ID rep_id;		/* class representation id */
  DB_SEQ *values;		/* values for range and list partition types */
};

typedef struct or_classrep OR_CLASSREP;
struct or_classrep
{
  OR_CLASSREP *next;		/* obsolete : use array for accessing elements */

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
  unsigned has_partition_info:1;	/* flag indicating if class has partition info */
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

extern void or_class_rep_dir (RECDES * record, OID * rep_dir_p);
extern void or_class_hfid (RECDES * record, HFID * hfid);
extern void or_class_tde_algorithm (RECDES * record, TDE_ALGORITHM * tde_algo);
#if defined (ENABLE_UNUSED_FUNCTION)
extern void or_class_statistics (RECDES * record, OID * oid);
extern int or_class_subclasses (RECDES * record, int *array_size, OID ** array_ptr);
extern int or_classrep_needs_indexes (OR_CLASSREP * rep);
#endif
extern int or_get_unique_hierarchy (THREAD_ENTRY * thread_p, RECDES * record, int attrid, BTID * btid,
				    OID ** class_oids, HFID ** hfids, int *num_heaps, int *partition_local_index);
extern OR_CLASSREP *or_get_classrep (RECDES * record, int repid);
extern OR_CLASSREP *or_get_classrep_noindex (RECDES * record, int repid);
extern OR_CLASSREP *or_classrep_load_indexes (OR_CLASSREP * rep, RECDES * record);
extern int or_class_get_partition_info (RECDES * record, OR_PARTITION * partition_info, REPR_ID * repr_id,
					int *has_partition_info);
const char *or_get_constraint_comment (RECDES * record, const char *constraint_name);
extern void or_free_classrep (OR_CLASSREP * rep);
extern int or_get_attrname (RECDES * record, int attrid, char **string, int *alloced_string);
extern int or_get_attrcomment (RECDES * record, int attrid, char **string, int *alloced_string);

/* OLD STYLE INTERFACE */
#if defined (ENABLE_UNUSED_FUNCTION)
extern void orc_class_rep_dir (RECDES * record, OID * rep_dir_p);
extern void orc_class_hfid_from_record (RECDES * record, HFID * hfid);
#endif
extern DISK_REPR *orc_diskrep_from_record (THREAD_ENTRY * thread_p, RECDES * record);
extern void orc_free_diskrep (DISK_REPR * rep);
extern CLS_INFO *orc_class_info_from_record (RECDES * record);
extern void orc_free_class_info (CLS_INFO * info);
extern int orc_subclasses_from_record (RECDES * record, int *array_size, OID ** array_ptr);
extern int orc_superclasses_from_record (RECDES * record, int *array_size, OID ** array_ptr);
extern OR_CLASSREP **or_get_all_representation (RECDES * record, bool do_indexes, int *count);

extern int or_replace_rep_id (RECDES * record, int repid);

extern int or_mvcc_get_header (const RECDES * record, MVCC_REC_HEADER * mvcc_rec_header);
extern int or_mvcc_set_header (RECDES * record, MVCC_REC_HEADER * mvcc_rec_header);
extern int or_mvcc_add_header (RECDES * record, MVCC_REC_HEADER * mvcc_rec_header, int bound_bit,
			       int variable_offset_size);
extern int or_mvcc_set_log_lsa_to_record (RECDES * record, const LOG_LSA * lsa);
#endif /* _OBJECT_REPRESENTATION_SR_H_ */
