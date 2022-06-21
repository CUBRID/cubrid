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
 * btree_load.h: Contains private information of B+tree Module
 */

#ifndef _BTREE_LOAD_H_
#define _BTREE_LOAD_H_

#ident "$Id$"

#include <assert.h>

#include "btree.h"
#include "object_representation_constants.h"
#include "error_manager.h"
#include "storage_common.h"
#include "oid.h"
#include "system_parameter.h"
#include "object_domain.h"
#include "slotted_page.h"

/*
 * Constants related to b+tree structure
 */

#define PEEK_KEY_VALUE PEEK
#define COPY_KEY_VALUE COPY

/* The revision level of the the Btree should be incremented whenever there
 * is a disk representation change for the Btree structure.
 */
#define BTREE_CURRENT_REV_LEVEL 0

/* each index page is supposed to be left empty as indicated by the
 * UNFILL FACTOR during index loading phase.
 */
#define LOAD_FIXED_EMPTY_FOR_LEAF \
  (DB_PAGESIZE * prm_get_float_value (PRM_ID_BT_UNFILL_FACTOR) \
   + DISK_VPID_SIZE)
#define LOAD_FIXED_EMPTY_FOR_NONLEAF \
  (DB_PAGESIZE * MAX (prm_get_float_value (PRM_ID_BT_UNFILL_FACTOR), 0.1) \
   + DISK_VPID_SIZE)

/* each page is supposed to have around 30% blank area during merge
   considerations of a delete operation */

/* Maximum Alignment */
#define BTREE_MAX_ALIGN INT_ALIGNMENT

/* Maximum Leaf Node Entry Size */
#define LEAF_ENTRY_MAX_SIZE(n) \
  (LEAF_RECORD_SIZE \
   + (2 * OR_OID_SIZE) /* OID + class OID */ \
   + (2 * OR_MVCCID_SIZE) /* Insert/delete MVCCID */ \
   + BTREE_MAX_ALIGN /* Alignment */ \
   + n /* Key disk length. */)

/* Maximum size of new fence key. */
#define LEAF_FENCE_MAX_SIZE(n) \
  (LEAF_RECORD_SIZE \
   + OR_OID_SIZE /* OID marker for fence key */ \
   + BTREE_MAX_ALIGN /* Alignment */ \
   + n /* Key disk length. */)

/* Maximum Non_Leaf Entry Size */
#define NON_LEAF_ENTRY_MAX_SIZE(n) \
  (NON_LEAF_RECORD_SIZE + BTREE_MAX_ALIGN + n)

/* New b-tree entry maximum size. */
#define BTREE_NEW_ENTRY_MAX_SIZE(key_disk_size, node_type) \
  ((node_type) == BTREE_LEAF_NODE ? \
   LEAF_ENTRY_MAX_SIZE (key_disk_size) : \
   NON_LEAF_ENTRY_MAX_SIZE (key_disk_size))

/* compare two object identifiers */
#define OIDCMP(n1, n2) \
  ((n1).pageid == (n2).pageid \
   && (n1).slotid == (n2).slotid \
   && (n1).volid == (n2).volid)

/* Header (Oth) record of the page */
#define HEADER 0

#if !defined(NDEBUG)
#define BTREE_INVALID_INDEX_ID(btid) \
 ((btid)->vfid.fileid == NULL_FILEID || (btid)->vfid.volid == NULL_VOLID \
  || (btid)->root_pageid == NULL_PAGEID)
#endif

/* The size of an object in cases it has fixed size (includes all required
 * info).
 * In case of unique: OID, class OID, insert and delete MVCCID.
 * In case of non-unique: OID, insert and delete MVCCID.
 *
 * Fixed size is used when:
 * 1. object is saved in overflow page.
 * 2. object is non-first in leaf record.
 * 3. object is first in a leaf record that has overflow pages.
 */
#define BTREE_OBJECT_FIXED_SIZE(btree_info) \
  (BTREE_IS_UNIQUE ((btree_info)->unique_pk) ? \
   2 * OR_OID_SIZE + 2 * OR_MVCCID_SIZE : OR_OID_SIZE + 2 * OR_MVCCID_SIZE)
/* Maximum possible size for one b-tree object including all its information.
 */
#define BTREE_OBJECT_MAX_SIZE (2 * OR_OID_SIZE + 2 * OR_MVCCID_SIZE)

/*
 * Overflow key related defines
 */

/* We never want to store keys larger than an eighth of the page size
 * directly on the btree page since this will make the btree too deep.
 * Large keys are automatically stored on overflow pages.  With prefix
 * keys this shouldn't be much of a problem anyway (when we get them
 * turned back on).
 */
#define BTREE_MAX_KEYLEN_INPAGE ((int)(DB_PAGESIZE / 8))
/* in MVCC BTREE_MAX_OIDLEN_INPAGE include MVCC fields too */
#define BTREE_MAX_OIDLEN_INPAGE ((int)(DB_PAGESIZE / 8))

/* Maximum number of objects that surely fit given size including all info. */
#define BTREE_MAX_OIDCOUNT_IN_SIZE(btid, size) \
  ((int) (size) / BTREE_OBJECT_FIXED_SIZE (btid))
/* Maximum number of objects for a leaf record */
#define BTREE_MAX_OIDCOUNT_IN_LEAF_RECORD(btid) \
  (BTREE_MAX_OIDCOUNT_IN_SIZE (btid, BTREE_MAX_OIDLEN_INPAGE))

#define BTREE_MAX_OVERFLOW_RECORD_SIZE \
  (DB_PAGESIZE - DB_ALIGN (spage_header_size (), BTREE_MAX_ALIGN) \
   - DB_ALIGN (sizeof (BTREE_OVERFLOW_HEADER), BTREE_MAX_ALIGN))
#define BTREE_MAX_OIDCOUNT_IN_OVERFLOW_RECORD(btid) \
  (BTREE_MAX_OIDCOUNT_IN_SIZE (btid, BTREE_MAX_OVERFLOW_RECORD_SIZE))

extern int btree_node_number_of_keys (THREAD_ENTRY * thread_p, PAGE_PTR page_ptr);
extern int btree_get_next_overflow_vpid (THREAD_ENTRY * thread_p, PAGE_PTR page_ptr, VPID * vpid);

#define BTREE_GET_KEY_LEN_IN_PAGE(key_len) \
  (((key_len) >= BTREE_MAX_KEYLEN_INPAGE) ? DISK_VPID_SIZE : (key_len))

/* for notification log messages */
#define BTREE_SET_CREATED_OVERFLOW_KEY_NOTIFICATION(THREAD,KEY,OID,C_OID,BTID,BTNM) \
  btree_set_error(THREAD, KEY, OID, C_OID, BTID, BTNM, ER_NOTIFICATION_SEVERITY, \
		  ER_BTREE_CREATED_OVERFLOW_KEY, __FILE__, __LINE__)

#define BTREE_SET_CREATED_OVERFLOW_PAGE_NOTIFICATION(THREAD,KEY,OID,C_OID,BTID) \
  btree_set_error(THREAD, KEY, OID, C_OID, BTID, NULL, ER_NOTIFICATION_SEVERITY, \
		  ER_BTREE_CREATED_OVERFLOW_PAGE, __FILE__, __LINE__)

#define BTREE_SET_DELETED_OVERFLOW_PAGE_NOTIFICATION(THREAD,KEY,OID,C_OID,BTID) \
  btree_set_error(THREAD, KEY, OID, C_OID, BTID, NULL, ER_NOTIFICATION_SEVERITY, \
		  ER_BTREE_DELETED_OVERFLOW_PAGE, __FILE__, __LINE__)

/* set fixed size for MVCC record header */
inline void
BTREE_MVCC_SET_HEADER_FIXED_SIZE (MVCC_REC_HEADER * p_mvcc_rec_header)
{
  assert (p_mvcc_rec_header != NULL);
  if (!(p_mvcc_rec_header->mvcc_flag & OR_MVCC_FLAG_VALID_INSID))
    {
      p_mvcc_rec_header->mvcc_flag |= OR_MVCC_FLAG_VALID_INSID;
      p_mvcc_rec_header->mvcc_ins_id = MVCCID_ALL_VISIBLE;
    }
  if (!(p_mvcc_rec_header->mvcc_flag & OR_MVCC_FLAG_VALID_DELID))
    {
      p_mvcc_rec_header->mvcc_flag |= OR_MVCC_FLAG_VALID_DELID;
      p_mvcc_rec_header->mvcc_del_id = MVCCID_NULL;
    }
}

/*
 * Type definitions related to b+tree structure and operations
 */

/* Node header information */
typedef struct btree_node_header BTREE_NODE_HEADER;
struct btree_node_header
{
  BTREE_NODE_SPLIT_INFO split_info;	/* split point info. of the node */
  VPID prev_vpid;		/* Leaf Page Previous Node Pointer */
  VPID next_vpid;		/* Leaf Page Next Node Pointer */
  short node_level;		/* btree depth; Leaf(= 1), Non_leaf(> 1) */
  short max_key_len;		/* Maximum key length for the subtree */
};

/* Root header information */
typedef struct btree_root_header BTREE_ROOT_HEADER;
struct btree_root_header
{
  BTREE_NODE_HEADER node;
  int num_oids;			/* Number of OIDs stored in the Btree */
  int num_nulls;		/* Number of NULLs (they aren't stored) */
  int num_keys;			/* Number of unique keys in the Btree */
  OID topclass_oid;		/* topclass oid or NULL OID(non unique index) */
  int unique_pk;		/* unique or non-unique, is primary key */
  struct
  {
    int over:2;			/* for checking to over 32 bit */
    int num_oids:10;		/* extend 10 bit for num_oids */
    int num_nulls:10;		/* extend 10 bit for num_nulls */
    int num_keys:10;		/* extend 10 bit for num_keys */
  } _64;
  int rev_level;		/* Btree revision level */
  VFID ovfid;			/* Overflow file */
  MVCCID creator_mvccid;	/* MVCCID of creator transaction. */

  /* Always leave this field last. */
  char packed_key_domain[1];	/* The key type for the index */
};

/* overflow header information */
typedef struct btree_overflow_header BTREE_OVERFLOW_HEADER;
struct btree_overflow_header
{
  VPID next_vpid;
};

typedef struct btree_node_info BTREE_NODE_INFO;
struct btree_node_info
{
  short max_key_len;		/* Maximum key length for the subtree */
  int height;			/* The height of the subtree */
  INT32 tot_key_cnt;		/* Total key count in the subtree */
  int page_cnt;			/* Total page count in the subtree */
  int leafpg_cnt;		/* Total leaf page count in the subtree */
  int nleafpg_cnt;		/* Total non_leaf page count */
  int key_area_len;		/* Current max_key area length malloced */
  DB_VALUE max_key;		/* Largest key in the subtreee */
};				/* contains statistical data for testing purposes */

/*
 * B+tree load structures
 */

typedef struct btree_node BTREE_NODE;
struct btree_node
{
  BTREE_NODE *next;		/* Pointer to next node */
  VPID pageid;			/* Identifier of the page */
};

/* Recovery routines */
extern void btree_rv_nodehdr_dump (FILE * fp, int length, void *data);
extern void btree_rv_mvcc_save_increments (const BTID * btid, long long key_delta, long long oid_delta,
					   long long null_delta, RECDES * recdes);

extern bool btree_clear_key_value (bool * clear_flag, DB_VALUE * key_value);
extern void btree_init_temp_key_value (bool * clear_flag, DB_VALUE * key_value);
extern int btree_create_overflow_key_file (THREAD_ENTRY * thread_p, BTID_INT * btid);
extern int btree_init_overflow_header (THREAD_ENTRY * thread_p, PAGE_PTR page_ptr, BTREE_OVERFLOW_HEADER * ovf_header);
extern int btree_init_node_header (THREAD_ENTRY * thread_p, const VFID * vfid, PAGE_PTR page_ptr,
				   BTREE_NODE_HEADER * header, bool redo);
extern int btree_init_root_header (THREAD_ENTRY * thread_p, VFID * vfid, PAGE_PTR page_ptr,
				   BTREE_ROOT_HEADER * root_header, TP_DOMAIN * key_type);
extern BTREE_NODE_HEADER *btree_get_node_header (THREAD_ENTRY * thread_p, PAGE_PTR page_ptr);
extern BTREE_ROOT_HEADER *btree_get_root_header (THREAD_ENTRY * thread_p, PAGE_PTR page_ptr);
extern BTREE_OVERFLOW_HEADER *btree_get_overflow_header (THREAD_ENTRY * thread_p, PAGE_PTR page_ptr);
extern int btree_node_header_undo_log (THREAD_ENTRY * thread_p, VFID * vfid, PAGE_PTR page_ptr);
extern int btree_node_header_redo_log (THREAD_ENTRY * thread_p, VFID * vfid, PAGE_PTR page_ptr);
extern int btree_change_root_header_delta (THREAD_ENTRY * thread_p, VFID * vfid, PAGE_PTR page_ptr,
					   long long null_delta, long long oid_delta, long long key_delta);

extern int btree_get_disk_size_of_key (DB_VALUE *);
extern TP_DOMAIN *btree_generate_prefix_domain (BTID_INT * btid);
extern int btree_glean_root_header_info (THREAD_ENTRY * thread_p, BTREE_ROOT_HEADER * root_header, BTID_INT * btid,
					 bool is_key_type);
extern DISK_ISVALID btree_verify_tree (THREAD_ENTRY * thread_p, const OID * class_oid_p, BTID_INT * btid,
				       const char *btname);
extern int btree_get_prefix_separator (const DB_VALUE * key1, const DB_VALUE * key2, DB_VALUE * prefix_key,
				       TP_DOMAIN * key_domain);

extern int btree_get_asc_desc (THREAD_ENTRY * thread_p, BTID * btid, int col_idx, int *asc_desc);

#endif /* _BTREE_LOAD_H_ */
