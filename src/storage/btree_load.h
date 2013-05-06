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
 * btree_load.h: Contains private information of B+tree Module
 */

#ifndef _BTREE_LOAD_H_
#define _BTREE_LOAD_H_

#ident "$Id$"

#include "btree.h"
#include "object_representation.h"
#include "error_manager.h"
#include "storage_common.h"
#include "oid.h"
#include "system_parameter.h"
#include "object_domain.h"

/*
 * Constants related to b+tree structure
 */

#define PEEK_KEY_VALUE 0
#define COPY_KEY_VALUE 1

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
#define FIXED_EMPTY   ( DB_PAGESIZE * 0.33 )

#define BTREE_MAX_ALIGN INT_ALIGNMENT	/* Maximum Alignment            */
					     /* Maximum Leaf Node Entry Size */
#define LEAFENTSZ(n)  ( LEAF_RECORD_SIZE + BTREE_MAX_ALIGN \
                            + OR_OID_SIZE + BTREE_MAX_ALIGN + n )
					     /* Maximum Non_Leaf Entry Size  */
#define NLEAFENTSZ(n) ( NON_LEAF_RECORD_SIZE + BTREE_MAX_ALIGN + n )

#define OIDCMP( n1, n2 )  \
  ( (n1).volid == (n2).volid && \
    (n1).pageid == (n2).pageid && \
    (n1).slotid == (n2).slotid )	/* compare two object identifiers */

#define HEADER 0		/* Header (Oth) record of the page  */

#define BTREE_INVALID_INDEX_ID(btid) \
 ((btid)->vfid.fileid == NULL_FILEID || (btid)->vfid.volid == NULL_VOLID ||\
  (btid)->root_pageid == NULL_PAGEID)

/*
 * Overflow key related defines
 */

#define SPHEADSIZE 48		/* Assume reserved for slotted page header info */

/* We never want to store keys larger than an eighth of the pagesize
 * directly on the btree page since this will make the btree too deep.
 * Large keys are automatically stored on overflow pages.  With prefix
 * keys this shouldn't be much of a problem anyway (when we get them
 * turned back on).
 */
#define BTREE_MAX_KEYLEN_INPAGE ((int)(DB_PAGESIZE / 4))
#define BTREE_MAX_SEPARATOR_KEYLEN_INPAGE ((int)(DB_PAGESIZE / 8))
#define BTREE_MAX_OIDLEN_INPAGE ((int)(DB_PAGESIZE / 4))

/* B+tree node types */
typedef enum
{
  BTREE_LEAF_NODE = 0,
  BTREE_NON_LEAF_NODE,
  BTREE_OVERFLOW_NODE
} BTREE_NODE_TYPE;

#define NODE_HEADER_SIZE       BTREE_NUM_OIDS_OFFSET	/* Node Header Disk Size */

int btree_get_node_key_cnt (PAGE_PTR page_ptr);

#define BTREE_GET_NODE_TYPE(ptr) \
  (BTREE_GET_NODE_LEVEL(ptr) > 1 ? BTREE_NON_LEAF_NODE : BTREE_LEAF_NODE)

/* readers/writers for fields */
#define BTREE_GET_NODE_LEVEL(ptr) \
  OR_GET_SHORT((ptr) + BTREE_NODE_LEVEL_OFFSET)

#define BTREE_GET_NODE_MAX_KEY_LEN(ptr) \
  OR_GET_SHORT((ptr) + BTREE_NODE_MAX_KEY_LEN_OFFSET)

#define _BTREE_GET_NODE_NEXT_VPID_VOLID(ptr) \
  OR_GET_SHORT((ptr) + BTREE_NODE_NEXT_VPID_OFFSET)

#define _BTREE_GET_NODE_NEXT_VPID_PAGEID(ptr) \
  OR_GET_INT((ptr) + BTREE_NODE_NEXT_VPID_OFFSET + OR_SHORT_SIZE)

#define BTREE_GET_NODE_NEXT_VPID(ptr, vp) \
  do { \
    (vp)->volid = _BTREE_GET_NODE_NEXT_VPID_VOLID (ptr); \
    (vp)->pageid = _BTREE_GET_NODE_NEXT_VPID_PAGEID (ptr); \
  } while (0)

#define _BTREE_GET_NODE_PREV_VPID_VOLID(ptr) \
  OR_GET_SHORT((ptr) + BTREE_NODE_PREV_VPID_OFFSET)

#define _BTREE_GET_NODE_PREV_VPID_PAGEID(ptr) \
  OR_GET_INT((ptr) + BTREE_NODE_PREV_VPID_OFFSET + OR_SHORT_SIZE)

#define BTREE_GET_NODE_PREV_VPID(ptr, vp) \
  do { \
  (vp)->volid = _BTREE_GET_NODE_PREV_VPID_VOLID (ptr); \
  (vp)->pageid = _BTREE_GET_NODE_PREV_VPID_PAGEID (ptr); \
    } while (0)

#define _BTREE_GET_NODE_SPLIT_INFO_PIVOT(ptr, val) \
  OR_GET_FLOAT((ptr) + BTREE_NODE_SPLIT_INFO_OFFSET, val)

#define _BTREE_GET_NODE_SPLIT_INFO_INDEX(ptr) \
  OR_GET_INT((ptr) + BTREE_NODE_SPLIT_INFO_OFFSET + OR_FLOAT_SIZE)

#define BTREE_GET_NODE_SPLIT_INFO(ptr, vp) \
  do { \
    _BTREE_GET_NODE_SPLIT_INFO_PIVOT(ptr, &((vp)->pivot)); \
    (vp)->index = _BTREE_GET_NODE_SPLIT_INFO_INDEX(ptr); \
  } while (0)

#define BTREE_GET_NUM_OIDS(ptr)\
  OR_GET_INT((ptr) + BTREE_NUM_OIDS_OFFSET)

#define BTREE_GET_NUM_NULLS(ptr) \
  OR_GET_INT((ptr) + BTREE_NUM_NULLS_OFFSET)

#define BTREE_GET_NUM_KEYS(ptr) \
  OR_GET_INT((ptr) + BTREE_NUM_KEYS_OFFSET)

#define BTREE_GET_TOPCLASS_OID(ptr, oid) \
  OR_GET_OID((ptr) + BTREE_TOPCLASS_OID_OFFSET, oid)

#define BTREE_GET_UNIQUE(ptr) \
  OR_GET_INT((ptr) + BTREE_UNIQUE_OFFSET)

#define BTREE_GET_REVERSE_RESERVED(ptr) \
  OR_GET_INT((ptr) + BTREE_REVERSE_RESERVED_OFFSET)

#define BTREE_GET_REV_LEVEL(ptr) \
  OR_GET_INT((ptr) + BTREE_REV_LEVEL_OFFSET)

#define _BTREE_GET_OVFID_FILEID(ptr) \
  OR_GET_INT((ptr) + BTREE_OVFID_OFFSET)

#define _BTREE_GET_OVFID_VOLID(ptr) \
  OR_GET_SHORT((ptr) + BTREE_OVFID_OFFSET + OR_INT_SIZE)

#define BTREE_GET_OVFID(ptr, vf) \
  do { \
    (vf)->fileid = _BTREE_GET_OVFID_FILEID (ptr); \
    (vf)->volid = _BTREE_GET_OVFID_VOLID (ptr); \
  } while (0)

#define BTREE_GET_KEY_LEN_IN_PAGE(node_type, key_len) \
  ((((node_type) == BTREE_LEAF_NODE && (key_len) >= BTREE_MAX_KEYLEN_INPAGE) \
    ||((node_type) == BTREE_NON_LEAF_NODE  \
      && (key_len) >= BTREE_MAX_SEPARATOR_KEYLEN_INPAGE)) \
    ? DISK_VPID_SIZE : (key_len))

#define BTREE_PUT_NODE_LEVEL(ptr, val) \
  OR_PUT_SHORT((ptr) + BTREE_NODE_LEVEL_OFFSET, val)

#define BTREE_PUT_NODE_MAX_KEY_LEN(ptr, val) \
  OR_PUT_SHORT((ptr) + BTREE_NODE_MAX_KEY_LEN_OFFSET, val)

#define _BTREE_PUT_NODE_NEXT_VPID_VOLID(ptr, val) \
  OR_PUT_SHORT((ptr) + BTREE_NODE_NEXT_VPID_OFFSET, val)

#define _BTREE_PUT_NODE_NEXT_VPID_PAGEID(ptr, val) \
  OR_PUT_INT((ptr) + BTREE_NODE_NEXT_VPID_OFFSET + OR_SHORT_SIZE, val)

#define BTREE_PUT_NODE_NEXT_VPID(ptr, vp) \
  do { \
    _BTREE_PUT_NODE_NEXT_VPID_VOLID (ptr, (vp)->volid); \
    _BTREE_PUT_NODE_NEXT_VPID_PAGEID (ptr, (vp)->pageid); \
  } while (0)

#define _BTREE_PUT_NODE_PREV_VPID_VOLID(ptr, val) \
  OR_PUT_SHORT((ptr) + BTREE_NODE_PREV_VPID_OFFSET, val)

#define _BTREE_PUT_NODE_PREV_VPID_PAGEID(ptr, val) \
  OR_PUT_INT((ptr) + BTREE_NODE_PREV_VPID_OFFSET + OR_SHORT_SIZE, val)

#define BTREE_PUT_NODE_PREV_VPID(ptr, vp) \
  do { \
  _BTREE_PUT_NODE_PREV_VPID_VOLID (ptr, (vp)->volid); \
  _BTREE_PUT_NODE_PREV_VPID_PAGEID (ptr, (vp)->pageid); \
    } while (0)

#define _BTREE_PUT_NODE_SPLIT_INFO_PIVOT(ptr, vp) \
  OR_PUT_FLOAT((ptr) + BTREE_NODE_SPLIT_INFO_OFFSET, vp)

#define _BTREE_PUT_NODE_SPLIT_INFO_INDEX(ptr, val) \
  OR_PUT_INT((ptr) + BTREE_NODE_SPLIT_INFO_OFFSET + OR_FLOAT_SIZE, val)

#define BTREE_PUT_NODE_SPLIT_INFO(ptr, vp) \
  do { \
    _BTREE_PUT_NODE_SPLIT_INFO_PIVOT (ptr, &((vp)->pivot)); \
    _BTREE_PUT_NODE_SPLIT_INFO_INDEX (ptr, (vp)->index); \
  } while (0)

#define BTREE_PUT_NUM_OIDS(ptr, val) \
  OR_PUT_INT((ptr) + BTREE_NUM_OIDS_OFFSET, val)

#define BTREE_PUT_NUM_NULLS(ptr, val) \
  OR_PUT_INT((ptr) + BTREE_NUM_NULLS_OFFSET, val)

#define BTREE_PUT_NUM_KEYS(ptr, val) \
  OR_PUT_INT((ptr) + BTREE_NUM_KEYS_OFFSET, val)

#define BTREE_PUT_TOPCLASS_OID(ptr, val) \
  OR_PUT_OID((ptr) + BTREE_TOPCLASS_OID_OFFSET, val)

#define BTREE_PUT_UNIQUE(ptr, val) \
  OR_PUT_INT((ptr) + BTREE_UNIQUE_OFFSET, val)

#define BTREE_PUT_REVERSE_RESERVED(ptr, val) \
  OR_PUT_INT((ptr) + BTREE_REVERSE_RESERVED_OFFSET, val)

#define BTREE_PUT_REV_LEVEL(ptr, val) \
  OR_PUT_INT((ptr) + BTREE_REV_LEVEL_OFFSET, val)

#define _BTREE_PUT_OVFID_FILEID(ptr, val) \
  OR_PUT_INT((ptr) + BTREE_OVFID_OFFSET, val)

#define _BTREE_PUT_OVFID_VOLID(ptr, val) \
  OR_PUT_SHORT((ptr) + BTREE_OVFID_OFFSET + OR_INT_SIZE, val)

#define BTREE_PUT_OVFID(ptr, vf) \
  do { \
     _BTREE_PUT_OVFID_FILEID (ptr, (vf)->fileid); \
     _BTREE_PUT_OVFID_VOLID (ptr, (vf)->volid); \
  } while (0)

/*
 * Type definitions related to b+tree structure and operations
 */

/* TODO: alignment  split_info, next_vpid, prev_vpid, ...*/
typedef struct btree_node_header BTREE_NODE_HEADER;
struct btree_node_header
{				/*  Node header information  */
  BTREE_NODE_SPLIT_INFO split_info;	/* split point info. of the node */
  VPID prev_vpid;		/* Leaf Page Previous Node Pointer     */
  VPID next_vpid;		/* Leaf Page Next Node Pointer         */
  short node_level;		/* btree depth; Leaf(= 1), Non_leaf(> 1) */
  short max_key_len;		/* Maximum key length for the subtree  */
};

typedef struct btree_root_header BTREE_ROOT_HEADER;
struct btree_root_header
{				/*  Root header information  */
  BTREE_NODE_HEADER node;
  VPID next_vpid;		/* Leaf Page Next Node Pointer       */
  int num_oids;			/* Number of OIDs stored in the Btree */
  int num_nulls;		/* Number of NULLs (they aren't stored) */
  int num_keys;			/* Number of unique keys in the Btree */
  OID topclass_oid;		/* topclass oid or NULL OID(non unique index) */
  int unique;			/* unique or non-unique */
  int reverse_reserved;		/* reverse or normal *//* not used */
  int rev_level;		/* Btree revision level */
  VFID ovfid;			/* Overflow file */
  TP_DOMAIN *key_type;		/* The key type for the index        */
};

typedef struct non_leaf_rec NON_LEAF_REC;
struct non_leaf_rec
{				/*  Fixed part of a non_leaf record  */
  VPID pnt;			/* The Child Page Pointer  */
  short key_len;
};

typedef struct leaf_rec LEAF_REC;
struct leaf_rec
{				/*  Fixed part of a leaf record  */
  VPID ovfl;			/* Overflow page pointer, for overflow OIDs  */
  short key_len;
};

typedef struct btree_node_info BTREE_NODE_INFO;
struct btree_node_info
{				/*  STATISTICAL TEST INFORMATION  */
  short max_key_len;		/* Maximum key length for the subtree   */
  int height;			/* The height of the subtree            */
  INT32 tot_key_cnt;		/* Total key count in the subtree       */
  int page_cnt;			/* Total page count in the subtree      */
  int leafpg_cnt;		/* Total leaf page count in the subtree */
  int nleafpg_cnt;		/* Total non_leaf page count            */
  int key_area_len;		/* Current max_key area length malloced */
  DB_VALUE max_key;		/* Largest key in the subtreee          */
};				/* contains statistical data for testing purposes */

/*
 * B+tree load structures
 */

typedef struct btree_node BTREE_NODE;
struct btree_node
{				/* node of the file_contents linked list */
  BTREE_NODE *next;		/* Pointer to next node */
  VPID pageid;			/* Identifier of the page */
};

extern int btree_check_foreign_key (THREAD_ENTRY * thread_p, OID * cls_oid,
				    HFID * hfid, OID * oid, DB_VALUE * keyval,
				    int n_attrs, OID * pk_cls_oid,
				    BTID * pk_btid, int cache_attr_id,
				    const char *fk_name);

/* Recovery routines */
extern int btree_rv_undo_create_index (THREAD_ENTRY * thread_p,
				       LOG_RCV * rcv);
extern void btree_rv_dump_create_index (FILE * fp, int length_ignore,
					void *data);

extern bool btree_clear_key_value (bool * clear_flag, DB_VALUE * key_value);
extern void btree_write_overflow_header (RECDES * Rec,
					 VPID * next_overflow_page);
extern int btree_create_overflow_key_file (THREAD_ENTRY * thread_p,
					   BTID_INT * btid);
#if defined(ENABLE_UNUSED_FUNCTION)
extern void btree_read_overflow_header (RECDES * Rec,
					VPID * next_overflow_page);
#endif
extern void btree_write_node_header (RECDES * Rec,
				     BTREE_NODE_HEADER * header);
extern int btree_write_root_header (RECDES * Rec,
				    BTREE_ROOT_HEADER * root_header);
extern void btree_read_root_header (RECDES * Rec,
				    BTREE_ROOT_HEADER * root_header);
extern int btree_get_key_length (DB_VALUE *);
extern int btree_write_record (THREAD_ENTRY * thread_p, BTID_INT * btid,
			       void *node_rec, DB_VALUE * key,
			       int node_type, int key_type,
			       int key_len, bool during_loading,
			       OID * class_oid, OID * oid, RECDES * rec);
extern void btree_read_record (THREAD_ENTRY * thread_p, BTID_INT * btid,
			       RECDES * Rec, DB_VALUE * key, void *rec_header,
			       int node_type, bool * clear_key, int *offset,
			       int copy);
extern TP_DOMAIN *btree_generate_prefix_domain (BTID_INT * btid);
extern int btree_glean_root_header_info (THREAD_ENTRY * thread_p,
					 BTREE_ROOT_HEADER * root_header,
					 BTID_INT * btid);
extern DISK_ISVALID btree_verify_tree (THREAD_ENTRY * thread_p,
				       const OID * class_oid_p,
				       BTID_INT * btid, const char *btname);
extern int btree_get_prefix (const DB_VALUE * key1, const DB_VALUE * key2,
			     DB_VALUE * prefix_key, TP_DOMAIN * key_domain);
extern char *btree_get_header_ptr (PAGE_PTR page_ptr, char **header_ptrptr);
extern int btree_compare_key (DB_VALUE * key1, DB_VALUE * key2,
			      TP_DOMAIN * key_domain,
			      int do_coercion, int total_order,
			      int *start_colp);
extern int btree_leaf_new_overflow_oids_vpid (RECDES * rec, VPID * ovfl_vpid);
extern int btree_get_asc_desc (THREAD_ENTRY * thread_p, BTID * btid,
			       int col_idx, int *asc_desc);

extern void btree_dump_key (FILE * fp, DB_VALUE * key);

extern int btree_insert_oid_with_order (RECDES * rec, OID * oid);

#endif /* _BTREE_LOAD_H_ */
