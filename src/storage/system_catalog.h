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
 * system_catalog.h : Catalog manager interface header file
 */

#ifndef _SYSTEM_CATALOG_H_
#define _SYSTEM_CATALOG_H_

#ident "$Id$"

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#include "config.h"

#include "error_manager.h"
#include "log_manager.h"
#include "memory_alloc.h"
#include "statistics.h"
#include "disk_manager.h"
#include "storage_common.h"

#define CATALOG_DIR_REPR_KEY -2

typedef struct ctid CTID;
struct ctid
{
  VFID vfid;			/* catalog volume identifier */
  EHID xhid;			/* extendible hash index identifier */
  PAGEID hpgid;			/* catalog header page identifier */
};				/* catalog identifier */



/*
 * disk_representation
 *
 * This is the primary communication structure between the schema manager
 * and the catalog manager.   It contains information on the order and
 * location of attributes for a particular class of object.
 */

typedef struct disk_representation DISK_REPR;
struct disk_representation
{
  REPR_ID id;			/* representation identifier */
  int n_fixed;			/* number of fixed attributes */
  struct disk_attribute *fixed;	/* fixed attribute structures */
  int fixed_length;		/* total length of fixed attributes */
  int n_variable;		/* number of variable attributes */
  struct disk_attribute *variable;	/* variable attribute structures */
#if 0				/* reserved for future use */
  int repr_reserved_1;
#endif
};				/* object disk representation */




typedef struct disk_attribute DISK_ATTR;
struct disk_attribute
{
  ATTR_ID id;			/* attribute identifier */
  int location;			/* location in disk representation exact offset if fixed attr. index to offset table if
				 * var attr. */
  DB_TYPE type;			/* datatype */
  int val_length;		/* default value length >= 0 */
  void *value;			/* default value. Currently, we do not keep default expression. */
  int position;			/* storage position (fixed attributes only) */
  OID classoid;			/* source class object id */
  int n_btstats;		/* number of B+tree statistics information */
  BTREE_STATS *bt_stats;	/* pointer to array of BTREE_STATS; BTREE_STATS[n_btstats] */
};				/* disk attribute structure */

typedef struct cls_info CLS_INFO;
struct cls_info
{
  HFID ci_hfid;			/* heap file identifier for the class */
  int ci_tot_pages;		/* total number of pages in the heap file */
  int ci_tot_objects;		/* total number of objects for this class */
  unsigned int ci_time_stamp;	/* timestamp of last update */
  OID ci_rep_dir;		/* representation directory record OID */
};				/* class specific information */

typedef struct catalog_access_info CATALOG_ACCESS_INFO;
struct catalog_access_info
{
  OID *class_oid;
  OID *dir_oid;
  char *class_name;
  bool is_update;
  bool need_unlock;
  bool access_started;
  bool need_free_class_name;
#if !defined (NDEBUG)
  bool is_systemop_started;
#endif
};

#if !defined (NDEBUG)
#define CATALOG_ACCESS_INFO_INITIALIZER \
  {NULL, NULL, NULL, false, false, false, false, false}
#else
#define CATALOG_ACCESS_INFO_INITIALIZER \
  {NULL, NULL, NULL, false, false, false, false}
#endif

#define CLS_INFO_INITIALIZER \
  { HFID_INITIALIZER, 0, 0, 0, { NULL_PAGEID, NULL_SLOTID, NULL_VOLID } }

#define catalog_free_class_info_and_init(class_info_p) \
  do \
    { \
      if ((class_info_p) != NULL) \
        { \
          catalog_free_class_info ((class_info_p)); \
          (class_info_p) = NULL; \
        } \
    } \
  while (0)

#define catalog_free_representation_and_init(repr_p) \
  do \
    { \
      if ((repr_p) != NULL) \
        { \
          catalog_free_representation ((repr_p)); \
          (repr_p) = NULL; \
        } \
    } \
  while (0)

extern CTID catalog_Id;		/* global catalog identifier */

extern void catalog_free_representation (DISK_REPR * repr_p);
extern void catalog_free_class_info (CLS_INFO * class_info_p);
extern void catalog_initialize (CTID * catid);
extern void catalog_finalize (void);

/* these two routines should be called only once and by the root */
extern CTID *catalog_create (THREAD_ENTRY * thread_p, CTID * catid);
extern int catalog_destroy (void);

extern int catalog_reclaim_space (THREAD_ENTRY * thread_p);
extern int catalog_add_representation (THREAD_ENTRY * thread_p, OID * class_id, REPR_ID repr_id, DISK_REPR * Disk_Repr,
				       OID * rep_dir_p, CATALOG_ACCESS_INFO * catalog_access_info_p);
extern int catalog_add_class_info (THREAD_ENTRY * thread_p, OID * class_oid_p, CLS_INFO * class_info_p,
				   CATALOG_ACCESS_INFO * catalog_access_info_p);
extern CLS_INFO *catalog_update_class_info (THREAD_ENTRY * thread_p, OID * class_id, CLS_INFO * cls_info,
					    CATALOG_ACCESS_INFO * catalog_access_info_p, bool skip_logging);
extern int catalog_drop_old_representations (THREAD_ENTRY * thread_p, OID * class_id);
extern DISK_REPR *catalog_get_representation (THREAD_ENTRY * thread_p, OID * class_id, REPR_ID repr_id,
					      CATALOG_ACCESS_INFO * catalog_access_info_p);
extern CLS_INFO *catalog_get_class_info (THREAD_ENTRY * thread_p, OID * class_id,
					 CATALOG_ACCESS_INFO * catalog_access_info_p);
extern int catalog_get_representation_directory (THREAD_ENTRY * thread_p, OID * class_id, REPR_ID ** reprid_set,
						 int *repr_cnt);
extern int catalog_get_last_representation_id (THREAD_ENTRY * thread_p, OID * cls_oid, REPR_ID * repr_id);
extern int catalog_insert (THREAD_ENTRY * thread_p, RECDES * record, OID * classoid, OID * rep_dir_p);
extern int catalog_update (THREAD_ENTRY * thread_p, RECDES * record, OID * classoid);
extern int catalog_delete (THREAD_ENTRY * thread_p, OID * classoid);

extern int catalog_get_cardinality (THREAD_ENTRY * thread_p, OID * class_oid, DISK_REPR * rep, BTID * btid,
				    const int key_pos, int *cardinality);
extern int catalog_get_cardinality_by_name (THREAD_ENTRY * thread_p, const char *class_name, const char *index_name,
					    const int key_pos, int *cardinality);

/* Checkdb consistency check routines */
extern DISK_ISVALID catalog_check_consistency (THREAD_ENTRY * thread_p);

/* Dump routines */
extern void catalog_dump (THREAD_ENTRY * thread_p, FILE * fp, int dump_flg);

/* Recovery routines */
extern int catalog_rv_new_page_redo (THREAD_ENTRY * thread_p, const LOG_RCV * recv);
extern int catalog_rv_insert_redo (THREAD_ENTRY * thread_p, const LOG_RCV * recv);
extern int catalog_rv_insert_undo (THREAD_ENTRY * thread_p, const LOG_RCV * recv);
extern int catalog_rv_delete_redo (THREAD_ENTRY * thread_p, const LOG_RCV * recv);
extern int catalog_rv_delete_undo (THREAD_ENTRY * thread_p, const LOG_RCV * recv);
extern int catalog_rv_update (THREAD_ENTRY * thread_p, const LOG_RCV * recv);
extern int catalog_rv_ovf_page_logical_insert_undo (THREAD_ENTRY * thread_p, const LOG_RCV * recv);
extern int catalog_get_dir_oid_from_cache (THREAD_ENTRY * thread_p, const OID * class_id_p, OID * dir_oid_p);
extern int catalog_start_access_with_dir_oid (THREAD_ENTRY * thread_p, CATALOG_ACCESS_INFO * catalog_access_info,
					      LOCK lock_mode);
extern int catalog_end_access_with_dir_oid (THREAD_ENTRY * thread_p, CATALOG_ACCESS_INFO * catalog_access_info,
					    int error);
#endif /* _SYSTEM_CATALOG_H_ */
