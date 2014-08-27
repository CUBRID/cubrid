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
 * btree_load.c - B+-Tree Loader
 */

#ident "$Id$"

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "btree_load.h"
#include "storage_common.h"
#include "locator_sr.h"
#include "file_io.h"
#include "page_buffer.h"
#include "external_sort.h"
#include "slotted_page.h"
#include "heap_file.h"
#include "file_manager.h"
#include "disk_manager.h"
#include "memory_alloc.h"
#include "db.h"
#include "log_impl.h"
#include "xserver_interface.h"

typedef struct sort_args SORT_ARGS;
struct sort_args
{				/* Collection of information required for "sr_index_sort" */
  int unique_pk;
  int not_null_flag;
  HFID *hfids;			/* Array of HFIDs for the class(es) */
  OID *class_ids;		/* Array of class OIDs              */
  OID cur_oid;			/* Identifier of the current object */
  RECDES in_recdes;		/* Input record descriptor          */
  int n_attrs;			/* Number of attribute ID's         */
  ATTR_ID *attr_ids;		/* Specification of the attribute(s) to
				 * sort on */
  int *attrs_prefix_length;	/* prefix length */
  TP_DOMAIN *key_type;
  HEAP_SCANCACHE hfscan_cache;	/* A heap scan cache                */
  HEAP_CACHE_ATTRINFO attr_info;	/* Attribute information            */
  int n_nulls;			/* Number of NULLs */
  int n_oids;			/* Number of OIDs */
  int n_classes;		/* cardinality of the hfids, the class_ids,
				 * and (with n_attrs) the attr_ids arrays
				 */
  int cur_class;		/* index into the hfids, class_ids, and
				 * attr_ids arrays
				 */
  int scancache_inited;
  int attrinfo_inited;

  BTID_INT *btid;

  OID *fk_refcls_oid;
  BTID *fk_refcls_pk_btid;
  int cache_attr_id;
  const char *fk_name;
  PRED_EXPR_WITH_CONTEXT *filter;
  PR_EVAL_FNC filter_eval_func;
  FUNCTION_INDEX_INFO *func_index_info;

  MVCCID lowest_active_mvccid;
};

typedef struct btree_page BTREE_PAGE;
struct btree_page
{
  VPID vpid;
  PAGE_PTR pgptr;
  BTREE_NODE_HEADER hdr;
};

typedef struct load_args LOAD_ARGS;
struct load_args
{				/* This structure is never written to disk; thus logical
				   ordering of fields is ok. */
  BTID_INT *btid;
  const char *bt_name;		/* index name */

  int allocated_pgcnt;		/* Allocated page count for index */
  int used_pgcnt;		/* Used page count for the index file */
  RECDES out_recdes;
  int max_recsize;		/* maximum record size that can be inserted into
				   either an empty leaf page or into an empty
				   overflow page (depending on the value of
				   "overflowing" */
  char *new_pos;
  DB_VALUE current_key;		/* Current key value */
  int max_key_size;		/* The maximum key size encountered so far;
				   used for string types */
  int cur_key_len;		/* The length of the current key */

  /* Linked list variables */
  BTREE_NODE *push_list;
  BTREE_NODE *pop_list;

  /* Variables for managing non-leaf, leaf & overflow pages */
  BTREE_PAGE nleaf;

#if 0				/* TODO: currently not used */
  VPID first_leafpgid;
#endif

  BTREE_PAGE leaf;

  BTREE_PAGE ovf;

  bool overflowing;		/* Currently, are we filling in an
				   overflow page (then, true); or a regular
				   leaf page (then, false) */
  int n_keys;			/* Number of keys - note that in the context
				 * of MVCC, only keys that have at least one
				 * non-deleted object are counted.
				 */

  int curr_non_del_obj_count;	/* Number of objects that have not been
				 * deleted. Unique indexes must have only one
				 * such object.
				 */

  PGSLOTID last_leaf_insert_slotid;	/* Slotid of last inserted leaf
					 * record.
					 */
};

/* While loading an index, BTREE_NUM_ALLOC_PAGES number of pages will be
 * allocated if there is no more page can be used.
 */
#define BTREE_NUM_ALLOC_PAGES           (DISK_SECTOR_NPAGES)


static bool btree_save_last_leafrec (THREAD_ENTRY * thread_p,
				     LOAD_ARGS * load_args);
static PAGE_PTR btree_connect_page (THREAD_ENTRY * thread_p, DB_VALUE * key,
				    int max_key_len, VPID * pageid,
				    LOAD_ARGS * load_args, int node_level);
static int btree_build_nleafs (THREAD_ENTRY * thread_p, LOAD_ARGS * load_args,
			       int n_nulls, int n_oids, int n_keys);

static void btree_log_page (THREAD_ENTRY * thread_p, VFID * vfid,
			    PAGE_PTR page_ptr);
static PAGE_PTR btree_get_page (THREAD_ENTRY * thread_p, BTID * btid,
				VPID * page_id, VPID * nearpg,
				BTREE_NODE_HEADER * header,
				int node_level,
				int *allocated_pgcnt, int *used_pgcnt);
static PAGE_PTR btree_proceed_leaf (THREAD_ENTRY * thread_p,
				    LOAD_ARGS * load_args);
static int btree_first_oid (THREAD_ENTRY * thread_p, DB_VALUE * this_key,
			    OID * class_oid, OID * first_oid,
			    MVCC_REC_HEADER * p_mvcc_rec_header,
			    LOAD_ARGS * load_args);
static int btree_construct_leafs (THREAD_ENTRY * thread_p,
				  const RECDES * in_recdes, void *arg);
#if defined(CUBRID_DEBUG)
static int btree_dump_sort_output (const RECDES * recdes,
				   LOAD_ARGS * load_args);
#endif /* defined(CUBRID_DEBUG) */
static int btree_index_sort (THREAD_ENTRY * thread_p, SORT_ARGS * sort_args,
			     SORT_PUT_FUNC * out_func, void *out_args);
static SORT_STATUS btree_sort_get_next (THREAD_ENTRY * thread_p,
					RECDES * temp_recdes, void *arg);
static int compare_driver (const void *first, const void *second, void *arg);
static int add_list (BTREE_NODE ** list, VPID * pageid);
static void remove_first (BTREE_NODE ** list);
static int length_list (const BTREE_NODE * this_list);
#if defined(CUBRID_DEBUG)
static void print_list (const BTREE_NODE * this_list);
#endif /* defined(CUBRID_DEBUG) */
static int btree_pack_root_header (RECDES * Rec, BTREE_ROOT_HEADER * header,
				   TP_DOMAIN * key_type);
static void btree_rv_save_root_head (int null_delta,
				     int oid_delta, int key_delta,
				     RECDES * recdes);

/*
 * btree_get_node_header () -
 *
 *   return:
 *   page_ptr(in):
 *
 */
BTREE_NODE_HEADER *
btree_get_node_header (PAGE_PTR page_ptr)
{
  RECDES header_record;
  BTREE_NODE_HEADER *header = NULL;

  assert (page_ptr != NULL);

#if !defined(NDEBUG)
  (void) pgbuf_check_page_ptype (NULL, page_ptr, PAGE_BTREE);
#endif

  if (spage_get_record (page_ptr, HEADER, &header_record, PEEK) != S_SUCCESS)
    {
      assert_release (false);
      return NULL;
    }

  header = (BTREE_NODE_HEADER *) header_record.data;
  if (header != NULL)
    {
      assert (header->node_level > 0);
      assert (header->max_key_len >= 0);
    }

  return header;
}

/*
 * btree_get_root_header () -
 *
 *   return:
 *   page_ptr(in):
 *
 */
BTREE_ROOT_HEADER *
btree_get_root_header (PAGE_PTR page_ptr)
{
  RECDES header_record;
  BTREE_ROOT_HEADER *root_header = NULL;

  assert (page_ptr != NULL);

#if !defined(NDEBUG)
  (void) pgbuf_check_page_ptype (NULL, page_ptr, PAGE_BTREE);
#endif

  if (spage_get_record (page_ptr, HEADER, &header_record, PEEK) != S_SUCCESS)
    {
      assert_release (false);
      return NULL;
    }

  root_header = (BTREE_ROOT_HEADER *) header_record.data;
  if (root_header != NULL)
    {
      assert (root_header->node.node_level > 0);
      assert (root_header->node.max_key_len >= 0);
    }

  return root_header;
}

/*
 * btree_get_overflow_header () -
 *
 *   return:
 *   page_ptr(in):
 *
 */
BTREE_OVERFLOW_HEADER *
btree_get_overflow_header (PAGE_PTR page_ptr)
{
  RECDES header_record;

  assert (page_ptr != NULL);

#if !defined(NDEBUG)
  (void) pgbuf_check_page_ptype (NULL, page_ptr, PAGE_BTREE);
#endif

  if (spage_get_record (page_ptr, HEADER, &header_record, PEEK) != S_SUCCESS)
    {
      assert_release (false);
      return NULL;
    }

  return (BTREE_OVERFLOW_HEADER *) header_record.data;
}

/*
 * btree_init_node_header () - insert btree node header
 *
 *   return:
 *   vfid(in):
 *   page_ptr(in):
 *   header(in):
 *
 */
int
btree_init_node_header (THREAD_ENTRY * thread_p, VFID * vfid,
			PAGE_PTR page_ptr, BTREE_NODE_HEADER * header,
			bool redo)
{
  RECDES rec;
  char rec_buf[IO_MAX_PAGE_SIZE + BTREE_MAX_ALIGN];

  assert (header != NULL);
  assert (header->node_level > 0);
  assert (header->max_key_len >= 0);

  /* create header record */
  rec.area_size = DB_PAGESIZE;
  rec.data = PTR_ALIGN (rec_buf, BTREE_MAX_ALIGN);
  rec.type = REC_HOME;
  rec.length = sizeof (BTREE_NODE_HEADER);
  memcpy (rec.data, header, sizeof (BTREE_NODE_HEADER));

  if (redo == true)
    {
      /* log the new header record for redo purposes */
      log_append_redo_data2 (thread_p, RVBT_NDHEADER_INS, vfid,
			     page_ptr, HEADER, rec.length, rec.data);
    }

  if (spage_insert_at (thread_p, page_ptr, HEADER, &rec) != SP_SUCCESS)
    {
      return ER_FAILED;
    }

  return NO_ERROR;
}


/*
 * btree_node_header_undo_log () -
 *
 *   return:
 *   vfid(in):
 *   page_ptr(in):
 *
 */
int
btree_node_header_undo_log (THREAD_ENTRY * thread_p, VFID * vfid,
			    PAGE_PTR page_ptr)
{
  RECDES rec;
  if (spage_get_record (page_ptr, HEADER, &rec, PEEK) != S_SUCCESS)
    {
      return ER_FAILED;
    }

  log_append_undo_data2 (thread_p, RVBT_NDHEADER_UPD,
			 vfid, page_ptr, HEADER, rec.length, rec.data);

  return NO_ERROR;
}

/*
 * btree_node_header_redo_log () -
 *
 *   return:
 *   vfid(in):
 *   page_ptr(in):
 *
 */
int
btree_node_header_redo_log (THREAD_ENTRY * thread_p, VFID * vfid,
			    PAGE_PTR page_ptr)
{
  RECDES rec;
  if (spage_get_record (page_ptr, HEADER, &rec, PEEK) != S_SUCCESS)
    {
      return ER_FAILED;
    }

  log_append_redo_data2 (thread_p, RVBT_NDHEADER_UPD,
			 vfid, page_ptr, HEADER, rec.length, rec.data);

  return NO_ERROR;
}

/*
 * btree_change_root_header_delta () -
 *
 *   return:
 *   vfid(in):
 *   page_ptr(in):
 *   null_delta(in):
 *   oid_delta(in):
 *   key_delta(in):
 *
 */
int
btree_change_root_header_delta (THREAD_ENTRY * thread_p, VFID * vfid,
				PAGE_PTR page_ptr, int null_delta,
				int oid_delta, int key_delta)
{
  RECDES rec, delta_rec;
  char delta_rec_buf[(3 * OR_INT_SIZE) + BTREE_MAX_ALIGN];
  BTREE_ROOT_HEADER *root_header = NULL;

  delta_rec.data = NULL;
  delta_rec.area_size = 3 * OR_INT_SIZE;
  delta_rec.data = PTR_ALIGN (delta_rec_buf, BTREE_MAX_ALIGN);

  if (spage_get_record (page_ptr, HEADER, &rec, PEEK) != S_SUCCESS)
    {
      return ER_FAILED;
    }

  root_header = (BTREE_ROOT_HEADER *) rec.data;
  root_header->num_nulls += null_delta;
  root_header->num_oids += oid_delta;
  root_header->num_keys += key_delta;

  /* save root head for undo purposes */
  btree_rv_save_root_head (-null_delta, -oid_delta, -key_delta, &delta_rec);

  log_append_undoredo_data2 (thread_p, RVBT_ROOTHEADER_UPD,
			     vfid, page_ptr, HEADER,
			     delta_rec.length, rec.length,
			     delta_rec.data, rec.data);

  return NO_ERROR;
}


/*
 * btree_pack_root_header () -
 *   return:
 *   rec(out):
 *   root_header(in):
 *
 * Note: Writes the first record (header record) for a root page.
 * rec must be long enough to hold the header record.
 */
static int
btree_pack_root_header (RECDES * rec, BTREE_ROOT_HEADER * root_header,
			TP_DOMAIN * key_type)
{
  OR_BUF buf;
  int rc = NO_ERROR;
  int fixed_size = (int) offsetof (BTREE_ROOT_HEADER, packed_key_domain);

  BTREE_NODE_HEADER *header = &root_header->node;

  memcpy (rec->data, root_header, fixed_size);

  or_init (&buf, rec->data + fixed_size,
	   (rec->area_size == -1) ? -1 : (rec->area_size - fixed_size));

  rc = or_put_domain (&buf, key_type, 0, 0);

  rec->length = fixed_size + CAST_BUFLEN (buf.ptr - buf.buffer);
  rec->type = REC_HOME;

  if (rc != NO_ERROR && er_errid () == NO_ERROR)
    {
      /* if an error occurs then set a generic error so that at least
         an error to be send to client. */
      if (er_errid () == NO_ERROR)
	{
	  assert (false);
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	}
    }

  return rc;
}

/*
 * btree_init_root_header () - insert btree node header
 *
 *   return:
 *   vfid(in):
 *   page_ptr(in):
 *   root_header(in):
 *
 */
int
btree_init_root_header (THREAD_ENTRY * thread_p, VFID * vfid,
			PAGE_PTR page_ptr, BTREE_ROOT_HEADER * root_header,
			TP_DOMAIN * key_type)
{
  RECDES rec;
  char copy_rec_buf[IO_MAX_PAGE_SIZE + BTREE_MAX_ALIGN];

  assert (root_header != NULL);
  assert (root_header->node.node_level > 0);
  assert (root_header->node.max_key_len >= 0);

  rec.area_size = DB_PAGESIZE;
  rec.data = PTR_ALIGN (copy_rec_buf, BTREE_MAX_ALIGN);

  if (btree_pack_root_header (&rec, root_header, key_type) != NO_ERROR)
    {
      return ER_FAILED;
    }

  /* insert the root header information into the root page */
  if (spage_insert_at (thread_p, page_ptr, HEADER, &rec) != SP_SUCCESS)
    {
      return ER_FAILED;
    }

  /* log the new header record for redo purposes */
  log_append_redo_data2 (thread_p, RVBT_NDHEADER_INS, vfid, page_ptr,
			 HEADER, rec.length, rec.data);

  return NO_ERROR;
}

/*
 * btree_init_overflow_header () - insert btree overflow node header
 *
 *   return:
 *   page_ptr(in):
 *   ovf_header(in):
 *
 */
int
btree_init_overflow_header (THREAD_ENTRY * thread_p,
			    PAGE_PTR page_ptr,
			    BTREE_OVERFLOW_HEADER * ovf_header)
{
  RECDES rec;
  char copy_rec_buf[IO_MAX_PAGE_SIZE + BTREE_MAX_ALIGN];

  rec.area_size = DB_PAGESIZE;
  rec.data = PTR_ALIGN (copy_rec_buf, BTREE_MAX_ALIGN);
  memcpy (rec.data, ovf_header, sizeof (BTREE_OVERFLOW_HEADER));
  rec.length = sizeof (BTREE_OVERFLOW_HEADER);
  rec.type = REC_HOME;

  /* insert the root header information into the root page */
  if (spage_insert_at (thread_p, page_ptr, HEADER, &rec) != SP_SUCCESS)
    {
      return ER_FAILED;
    }

  return NO_ERROR;
}

/*
 * btree_rv_save_root_head () - Save root head stats FOR LOGICAL LOG PURPOSES
 *   return:
 *   null_delta(in):
 *   oid_delta(in):
 *   key_delta(in):
 *   recdes(out):
 *
 * Note: Copy the root header statistics to the data area provided.
 *
 * Note: This is a UTILITY routine, but not an actual recovery routine.
 */
static void
btree_rv_save_root_head (int null_delta, int oid_delta, int key_delta,
			 RECDES * recdes)
{
  char *datap;

  assert (recdes->area_size >= 3 * OR_INT_SIZE);

  recdes->length = 0;
  datap = (char *) recdes->data;
  OR_PUT_INT (datap, null_delta);
  datap += OR_INT_SIZE;
  OR_PUT_INT (datap, oid_delta);
  datap += OR_INT_SIZE;
  OR_PUT_INT (datap, key_delta);
  datap += OR_INT_SIZE;

  recdes->length = CAST_STRLEN (datap - recdes->data);
}

/*
 * btree_rv_mvcc_save_increments () - Save unique_stats
 *   return:
 *   max_key_len(in):
 *   null_delta(in):
 *   oid_delta(in):
 *   key_delta(in):
 *   recdes(in):
 *
 * Note: Copy the unique statistics to the data area provided.
 *
 * Note: This is a UTILITY routine, but not an actual recovery routine.
 */
void
btree_rv_mvcc_save_increments (OID * class_oid, BTID * btid,
			       int key_delta, int oid_delta, int null_delta,
			       RECDES * recdes)
{
  char *datap;

  assert (recdes != NULL
	  && (recdes->area_size >=
	      ((3 * OR_INT_SIZE) + OR_OID_SIZE + OR_BTID_ALIGNED_SIZE)));

  recdes->length = (3 * OR_INT_SIZE) + OR_OID_SIZE + OR_BTID_ALIGNED_SIZE;
  datap = (char *) recdes->data;

  OR_PUT_OID (datap, class_oid);
  datap += OR_OID_SIZE;

  OR_PUT_BTID (datap, btid);
  datap += OR_BTID_ALIGNED_SIZE;

  OR_PUT_INT (datap, key_delta);
  datap += OR_INT_SIZE;

  OR_PUT_INT (datap, oid_delta);
  datap += OR_INT_SIZE;

  OR_PUT_INT (datap, null_delta);
  datap += OR_INT_SIZE;

  recdes->length = CAST_STRLEN (datap - recdes->data);
}

/*
 * btree_get_next_overflow_vpid () -
 *
 *   return:
 *   page_ptr(in):
 *
 */
int
btree_get_next_overflow_vpid (PAGE_PTR page_ptr, VPID * vpid)
{
  BTREE_OVERFLOW_HEADER *ovf_header = NULL;

  ovf_header = btree_get_overflow_header (page_ptr);
  if (ovf_header == NULL)
    {
      return ER_FAILED;
    }

  *vpid = ovf_header->next_vpid;

  return NO_ERROR;
}

/*
 * xbtree_load_index () - create & load b+tree index
 *   return: BTID * (btid on success and NULL on failure)
 *           btid is set as a side effect.
 *   btid(out):
 *      btid: Set to the created B+tree index identifier
 *            (Note: btid->vfid.volid should be set by the caller)
 *   bt_name(in): index name
 *   key_type(in): Key type corresponding to the attribute.
 *   class_oids(in): OID of the class for which the index will be created
 *   n_classes(in):
 *   n_attrs(in):
 *   attr_ids(in): Identifier of the attribute of the class for which the index
 *                 will be created.
 *   hfids(in): Identifier of the heap file containing the instances of the
 *	        class
 *   unique_pk(in):
 *   not_null_flag(in):
 *   fk_refcls_oid(in):
 *   fk_refcls_pk_btid(in):
 *   cache_attr_id(in):
 *   fk_name(in):
 *
 */
BTID *
xbtree_load_index (THREAD_ENTRY * thread_p, BTID * btid, const char *bt_name,
		   TP_DOMAIN * key_type, OID * class_oids, int n_classes,
		   int n_attrs, int *attr_ids, int *attrs_prefix_length,
		   HFID * hfids, int unique_pk, int not_null_flag,
		   OID * fk_refcls_oid, BTID * fk_refcls_pk_btid,
		   int cache_attr_id, const char *fk_name, char *pred_stream,
		   int pred_stream_size, char *func_pred_stream,
		   int func_pred_stream_size, int func_col_id,
		   int func_attr_index_start)
{
  LOG_TDES *tdes = NULL;
  SORT_ARGS sort_args_info, *sort_args;
  LOAD_ARGS load_args_info, *load_args;
  int init_pgcnt, i, first_alloc_nthpage;
  int file_created = 0, cur_class, attr_offset;
  VPID vpid;
  INT16 save_volid;
  FILE_BTREE_DES btdes;
  BTID_INT btid_int;
  LOG_DATA_ADDR addr;
  PRED_EXPR_WITH_CONTEXT *filter_pred = NULL;
  FUNCTION_INDEX_INFO func_index_info;
  DB_TYPE single_node_type = DB_TYPE_NULL;
  void *buf_info = NULL;
  void *func_unpack_info = NULL;
  VPID *ret_vpid;
#if !defined(NDEBUG)
  int track_id;
#endif

  /* Check for robustness */
  if (!btid || !hfids || !class_oids || !attr_ids || !key_type)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BTREE_LOAD_FAILED, 0);
      return NULL;
    }

  sort_args = &sort_args_info;
  load_args = &load_args_info;

  /* Initialize pointers which might be used in case of error */
  load_args->nleaf.pgptr = NULL;
  load_args->leaf.pgptr = NULL;
  load_args->ovf.pgptr = NULL;
  load_args->out_recdes.data = NULL;

  /*
   * Start a TOP SYSTEM OPERATION.
   * This top system operation will be either ABORTED (case of failure) or
   * COMMITTED, so that the new file becomes kind of permanent.  This allows
   * us to make use of un-used pages in the case of a bad init_pgcnt guess.
   */

  if (log_start_system_op (thread_p) == NULL)
    {
      return NULL;
    }

#if !defined(NDEBUG)
  track_id = thread_rc_track_enter (thread_p);
#endif

  btid_int.sys_btid = btid;
  btid_int.unique_pk = unique_pk;
#if !defined(NDEBUG)
  if (unique_pk)
    {
      assert (BTREE_IS_UNIQUE (btid_int.unique_pk));
      assert (BTREE_IS_PRIMARY_KEY (btid_int.unique_pk)
	      || !BTREE_IS_PRIMARY_KEY (btid_int.unique_pk));
    }
#endif
  btid_int.key_type = key_type;
  VFID_SET_NULL (&btid_int.ovfid);
  btid_int.rev_level = BTREE_CURRENT_REV_LEVEL;
  btid_int.new_file = (file_is_new_file (thread_p, &(btid_int.sys_btid->vfid))
		       == FILE_NEW_FILE) ? 1 : 0;
  COPY_OID (&btid_int.topclass_oid, &class_oids[0]);


  /*
   * for btree_range_search, part_key_desc is re-set at btree_initialize_bts
   */
  btid_int.part_key_desc = 0;

  /* init index key copy_buf info */
  btid_int.copy_buf = NULL;
  btid_int.copy_buf_len = 0;

  btid_int.nonleaf_key_type = btree_generate_prefix_domain (&btid_int);

  /* create a file descriptor */
  COPY_OID (&btdes.class_oid, &class_oids[0]);
  /* This needs to change to list all the attr_ids for multi-column */
  btdes.attr_id = attr_ids[0];
  save_volid = btid->vfid.volid;

  /* Initialize the fields of sorting argument structures */
  sort_args->lowest_active_mvccid = logtb_get_lowest_active_mvccid (thread_p);
  sort_args->unique_pk = unique_pk;
  sort_args->not_null_flag = not_null_flag;
  sort_args->hfids = hfids;
  sort_args->class_ids = class_oids;
  sort_args->attr_ids = attr_ids;
  sort_args->n_attrs = n_attrs;
  sort_args->attrs_prefix_length = attrs_prefix_length;
  sort_args->n_classes = n_classes;
  sort_args->key_type = key_type;
  OID_SET_NULL (&sort_args->cur_oid);
  sort_args->n_nulls = 0;
  sort_args->n_oids = 0;
  sort_args->cur_class = 0;
  sort_args->scancache_inited = 0;
  sort_args->attrinfo_inited = 0;
  sort_args->btid = &btid_int;
  sort_args->fk_refcls_oid = fk_refcls_oid;
  sort_args->fk_refcls_pk_btid = fk_refcls_pk_btid;
  sort_args->cache_attr_id = cache_attr_id;
  sort_args->fk_name = fk_name;
  if (pred_stream && pred_stream_size > 0)
    {
      if (stx_map_stream_to_filter_pred (thread_p, &filter_pred, pred_stream,
					 pred_stream_size, &buf_info) !=
	  NO_ERROR)
	{
	  goto error;
	}
    }
  sort_args->filter = filter_pred;
  sort_args->filter_eval_func =
    (filter_pred) ? eval_fnc (thread_p, filter_pred->pred, &single_node_type)
    : NULL;
  sort_args->func_index_info = NULL;
  if (func_pred_stream && func_pred_stream_size > 0)
    {
      func_index_info.expr_stream = func_pred_stream;
      func_index_info.expr_stream_size = func_pred_stream_size;
      func_index_info.col_id = func_col_id;
      func_index_info.attr_index_start = func_attr_index_start;
      func_index_info.expr = NULL;
      if (stx_map_stream_to_func_pred (thread_p,
				       (FUNC_PRED **) (&func_index_info.expr),
				       func_pred_stream,
				       func_pred_stream_size,
				       &func_unpack_info))
	{
	  goto error;
	}
      sort_args->func_index_info = &func_index_info;
    }

  /*
   * Start a heap scan cache for reading objects using the first nun-null heap
   * We are guaranteed that such a heap exists, otherwise btree_load_index
   * would not have been called.
   */
  while (sort_args->cur_class < sort_args->n_classes
	 && HFID_IS_NULL (&sort_args->hfids[sort_args->cur_class]))
    {
      sort_args->cur_class++;
    }

  cur_class = sort_args->cur_class;
  attr_offset = cur_class * sort_args->n_attrs;

  /* Start scancache */
  if (heap_scancache_start (thread_p, &sort_args->hfscan_cache,
			    &sort_args->hfids[cur_class],
			    &sort_args->class_ids[cur_class],
			    true, false, NULL) != NO_ERROR)
    {
      goto error;
    }
  sort_args->scancache_inited = 1;

  /* After building index acquire lock on table, the transaction has deadlock priority */
  tdes = LOG_FIND_CURRENT_TDES (thread_p);
  if (tdes)
    {
      tdes->has_deadlock_priority = true;
    }

  if (heap_attrinfo_start (thread_p, &sort_args->class_ids[cur_class],
			   sort_args->n_attrs,
			   &sort_args->attr_ids[attr_offset],
			   &sort_args->attr_info) != NO_ERROR)
    {
      goto error;
    }
  if (sort_args->filter)
    {
      if (heap_attrinfo_start (thread_p, &sort_args->class_ids[cur_class],
			       sort_args->filter->num_attrs_pred,
			       sort_args->filter->attrids_pred,
			       sort_args->filter->cache_pred) != NO_ERROR)
	{
	  goto error;
	}
    }
  if (sort_args->func_index_info)
    {
      if (heap_attrinfo_start (thread_p, &sort_args->class_ids[cur_class],
			       sort_args->n_attrs,
			       &sort_args->attr_ids[attr_offset],
			       ((FUNC_PRED *) sort_args->func_index_info->
				expr)->cache_attrinfo) != NO_ERROR)
	{
	  goto error;
	}
    }
  sort_args->attrinfo_inited = 1;

  /* There is no estimation for the number of pages to be used.
   * We will allocate pages on demand.
   */
  init_pgcnt = BTREE_NUM_ALLOC_PAGES;

  if (file_create (thread_p, &btid->vfid, init_pgcnt, FILE_BTREE, &btdes,
		   &vpid, 1) == NULL)
    {
      goto error;
    }
  file_created = 1;

  vacuum_log_add_dropped_file (thread_p, &btid->vfid,
			       VACUUM_LOG_ADD_DROPPED_FILE_UNDO);

  /*
   * Note: We do not initialize the allocated pages during the allocation
   *       since they belong to a new file and we do not perform any undo
   *       logging on it. In fact some of the pages may be returned to the
   *       file manager at a later point. We should not waste time and
   *       storage to initialize those pages at this moment. This is safe
   *       since this is a new file.
   *       The pages are initialized in btree_get_page
   */

  ret_vpid = file_alloc_pages_as_noncontiguous (thread_p, &btid->vfid, &vpid,
						&first_alloc_nthpage,
						init_pgcnt, NULL, NULL,
						NULL, NULL);

  if (ret_vpid == NULL && er_errid () != ER_INTERRUPTED)
    {
      /* try to allocate pages as many as possible */
      for (i = 0; i < init_pgcnt; i++)
	{
	  if (file_alloc_pages (thread_p, &btid->vfid, &vpid, 1,
				NULL, NULL, NULL) == NULL)
	    {
	      break;
	    }
	}
      init_pgcnt = i;
    }

  if (init_pgcnt == 0 || er_errid () == ER_INTERRUPTED)
    {
      goto error;
    }

  init_pgcnt++;			/* increment for the root page allocated */

    /** Initialize the fields of loading argument structures **/
  load_args->btid = &btid_int;
  load_args->bt_name = bt_name;
  load_args->allocated_pgcnt = init_pgcnt;
  load_args->used_pgcnt = 1;	/* set used page count (first page used for root) */
  DB_MAKE_NULL (&load_args->current_key);
  VPID_SET_NULL (&load_args->nleaf.vpid);
  load_args->nleaf.pgptr = NULL;
  VPID_SET_NULL (&load_args->leaf.vpid);
  load_args->leaf.pgptr = NULL;
  VPID_SET_NULL (&load_args->ovf.vpid);
  load_args->ovf.pgptr = NULL;
  load_args->out_recdes.area_size = DB_PAGESIZE;
  load_args->out_recdes.length = 0;
  load_args->out_recdes.type = REC_HOME;
  load_args->n_keys = 0;
  load_args->curr_non_del_obj_count = 0;
  load_args->out_recdes.data = (char *) os_malloc (DB_PAGESIZE);
  if (load_args->out_recdes.data == NULL)
    {
      goto error;
    }

  /* Allocate a root page and save the page_id */
  if (file_find_nthpages (thread_p, &load_args->btid->sys_btid->vfid, &vpid,
			  0, 1) != 1)
    {
      goto error;
    }
  load_args->btid->sys_btid->root_pageid = vpid.pageid;

  if (prm_get_bool_value (PRM_ID_LOG_BTREE_OPS))
    {
      _er_log_debug (ARG_FILE_LINE, "DEBUG_BTREE: load start on "
		     "class(%d, %d, %d), btid(%d, (%d, %d)).",
		     sort_args->class_ids[sort_args->cur_class].volid,
		     sort_args->class_ids[sort_args->cur_class].pageid,
		     sort_args->class_ids[sort_args->cur_class].slotid,
		     sort_args->btid->sys_btid->root_pageid,
		     sort_args->btid->sys_btid->vfid.volid,
		     sort_args->btid->sys_btid->vfid.fileid);
    }

  /* Build the leaf pages of the btree as the output of the sort.
   * We do not estimate the number of pages required.
   */
  if (btree_index_sort (thread_p, sort_args,
			btree_construct_leafs, load_args) != NO_ERROR)
    {
      goto error;
    }

  if (prm_get_bool_value (PRM_ID_LOG_BTREE_OPS))
    {
      _er_log_debug (ARG_FILE_LINE, "DEBUG_BTREE: load finished all. "
		     "%d classes loaded, found %d nulls and %d oids, "
		     "load %d keys.",
		     sort_args->n_classes, sort_args->n_nulls,
		     sort_args->n_oids, load_args->n_keys);
    }

  if (sort_args->attrinfo_inited)
    {
      heap_attrinfo_end (thread_p, &sort_args->attr_info);
      if (sort_args->filter)
	{
	  heap_attrinfo_end (thread_p, sort_args->filter->cache_pred);
	}
      if (sort_args->func_index_info)
	{
	  heap_attrinfo_end (thread_p,
			     ((FUNC_PRED *) sort_args->func_index_info->
			      expr)->cache_attrinfo);
	}
    }
  sort_args->attrinfo_inited = 0;
  if (sort_args->scancache_inited)
    {
      (void) heap_scancache_end (thread_p, &sort_args->hfscan_cache);
    }
  sort_args->scancache_inited = 0;

  /* Just to make sure that there were entries to put into the tree */
  if (load_args->leaf.pgptr != NULL)
    {
      /* Save the last leaf record */

      if (btree_save_last_leafrec (thread_p, load_args) != NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BTREE_LOAD_FAILED, 0);
	  goto error;
	}

      /* No need to deal with overflow pages anymore */
      load_args->ovf.pgptr = NULL;

      /* Build the non leaf nodes of the btree; Root page id will be assigned
         here */

      if (btree_build_nleafs (thread_p, load_args, sort_args->n_nulls,
			      sort_args->n_oids,
			      load_args->n_keys) != NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BTREE_LOAD_FAILED, 0);
	  goto error;
	}

      /* There is at least one leaf page */

      /* Release the memory area */
      os_free_and_init (load_args->out_recdes.data);
      pr_clear_value (&load_args->current_key);

      /* deallocate unused pages from the index file */
      if (load_args->used_pgcnt < load_args->allocated_pgcnt
	  && file_truncate_to_numpages (thread_p, &btid->vfid,
					load_args->used_pgcnt) != NO_ERROR)
	{
	  goto error;
	}

      if (prm_get_bool_value (PRM_ID_LOG_BTREE_OPS))
	{
	  _er_log_debug (ARG_FILE_LINE, "DEBUG_BTREE: "
			 "load built index btid (%d, (%d, %d)).",
			 load_args->btid->sys_btid->root_pageid,
			 load_args->btid->sys_btid->vfid.volid,
			 load_args->btid->sys_btid->vfid.fileid);
	}

#if !defined(NDEBUG)
      (void) btree_verify_tree (thread_p, &btdes.class_oid, &btid_int,
				bt_name);
#endif

    }
  else
    {
      if (prm_get_bool_value (PRM_ID_LOG_BTREE_OPS))
	{
	  _er_log_debug (ARG_FILE_LINE, "DEBUG_BTREE: "
			 "load didn't build any leaves btid (%d, (%d, %d)).",
			 load_args->btid->sys_btid->root_pageid,
			 load_args->btid->sys_btid->vfid.volid,
			 load_args->btid->sys_btid->vfid.fileid);
	}
      /* there are no index entries, destroy index file and call index
         creation */
      if (file_destroy (thread_p, &btid->vfid) != NO_ERROR)
	{
	  goto error;
	}
      file_created = 0;
      os_free_and_init (load_args->out_recdes.data);
      pr_clear_value (&load_args->current_key);

      btid->vfid.volid = save_volid;
      if (xbtree_add_index (thread_p, btid, key_type, &class_oids[0],
			    attr_ids[0], unique_pk, sort_args->n_oids,
			    sort_args->n_nulls, load_args->n_keys) == NULL)
	{
	  goto error;
	}
      file_created = 1;
    }

  if (!VFID_ISNULL (&load_args->btid->ovfid))
    {
      /* notification */
      BTREE_SET_CREATED_OVERFLOW_KEY_NOTIFICATION (thread_p, NULL, NULL,
						   &class_oids[0], btid,
						   bt_name);
    }

  if (sort_args->filter)
    {
      /* to clear db values from dbvalue regu variable */
      qexec_clear_pred_context (thread_p, sort_args->filter, true);
    }
  if (buf_info)
    {
      stx_free_additional_buff (thread_p, buf_info);
      stx_free_xasl_unpack_info (buf_info);
      db_private_free_and_init (thread_p, buf_info);
    }
  if (sort_args->func_index_info && sort_args->func_index_info->expr)
    {
      (void) qexec_clear_func_pred (thread_p,
				    sort_args->func_index_info->expr);
    }
  if (func_unpack_info)
    {
      stx_free_additional_buff (thread_p, func_unpack_info);
      stx_free_xasl_unpack_info (func_unpack_info);
      db_private_free_and_init (thread_p, func_unpack_info);
    }

#if !defined(NDEBUG)
  if (thread_rc_track_exit (thread_p, track_id) != NO_ERROR)
    {
      assert_release (false);
    }
#endif

  if (sort_args->cache_attr_id < 0)
    {
      log_end_system_op (thread_p, LOG_RESULT_TOPOP_COMMIT);

      addr.vfid = NULL;
      addr.pgptr = NULL;
      addr.offset = 0;
      log_append_undo_data (thread_p, RVBT_CREATE_INDEX, &addr, sizeof (VFID),
			    &(btid->vfid));

      /* Already append a vacuum undo logging when file was created, but
       * since that was included in the system operation which just got
       * committed, we need to do it again in case of rollback.
       */
      vacuum_log_add_dropped_file (thread_p, &btid->vfid,
				   VACUUM_LOG_ADD_DROPPED_FILE_UNDO);
    }
  else
    {
      log_end_system_op (thread_p, LOG_RESULT_TOPOP_ATTACH_TO_OUTER);
    }

  LOG_CS_ENTER (thread_p);
  logpb_flush_pages_direct (thread_p);
  LOG_CS_EXIT (thread_p);

  if (prm_get_bool_value (PRM_ID_LOG_BTREE_OPS))
    {
      _er_log_debug (ARG_FILE_LINE, "BTREE_DEBUG: load finished successful "
		     "index btid(%d, (%d, %d)).",
		     load_args->btid->sys_btid->root_pageid,
		     load_args->btid->sys_btid->vfid.volid,
		     load_args->btid->sys_btid->vfid.fileid);
    }

  return btid;

error:

  if (sort_args->scancache_inited)
    {
      (void) heap_scancache_end (thread_p, &sort_args->hfscan_cache);
    }
  if (sort_args->attrinfo_inited)
    {
      heap_attrinfo_end (thread_p, &sort_args->attr_info);
      if (sort_args->filter)
	{
	  heap_attrinfo_end (thread_p, sort_args->filter->cache_pred);
	}
      if (sort_args->func_index_info && sort_args->func_index_info->expr)
	{
	  heap_attrinfo_end (thread_p,
			     ((FUNC_PRED *) sort_args->func_index_info->
			      expr)->cache_attrinfo);
	}
    }
  if (file_created)
    {
      (void) file_destroy (thread_p, &btid->vfid);
    }
  VFID_SET_NULL (&btid->vfid);
  btid->root_pageid = NULL_PAGEID;
  if (load_args->out_recdes.data)
    {
      os_free_and_init (load_args->out_recdes.data);
    }
  pr_clear_value (&load_args->current_key);

  if (load_args->leaf.pgptr)
    {
      pgbuf_unfix_and_init (thread_p, load_args->leaf.pgptr);
    }
  if (load_args->ovf.pgptr)
    {
      pgbuf_unfix_and_init (thread_p, load_args->ovf.pgptr);
    }
  if (load_args->nleaf.pgptr)
    {
      pgbuf_unfix_and_init (thread_p, load_args->nleaf.pgptr);
    }
  if (sort_args->filter)
    {
      /* to clear db values from dbvalue regu variable */
      qexec_clear_pred_context (thread_p, sort_args->filter, true);
    }
  if (buf_info)
    {
      stx_free_additional_buff (thread_p, buf_info);
      stx_free_xasl_unpack_info (buf_info);
      db_private_free_and_init (thread_p, buf_info);
    }
  if (sort_args->func_index_info && sort_args->func_index_info->expr)
    {
      (void) qexec_clear_func_pred (thread_p,
				    (FUNC_PRED *) sort_args->func_index_info->
				    expr);
    }
  if (func_unpack_info)
    {
      stx_free_additional_buff (thread_p, func_unpack_info);
      stx_free_xasl_unpack_info (func_unpack_info);
      db_private_free_and_init (thread_p, func_unpack_info);
    }

#if !defined(NDEBUG)
  if (thread_rc_track_exit (thread_p, track_id) != NO_ERROR)
    {
      assert_release (false);
    }
#endif

  log_end_system_op (thread_p, LOG_RESULT_TOPOP_ABORT);

  if (prm_get_bool_value (PRM_ID_LOG_BTREE_OPS))
    {
      _er_log_debug (ARG_FILE_LINE, "BTREE_DEBUG: load aborted index "
		     "btid(%d, (%d, %d)).",
		     load_args->btid->sys_btid->root_pageid,
		     load_args->btid->sys_btid->vfid.volid,
		     load_args->btid->sys_btid->vfid.fileid);
    }

  return NULL;
}

/*
 * btree_save_last_leafrec () - save the last leaf record
 *   return: NO_ERROR
 *   load_args(in): Collection of info. for btree load operation
 *
 * Note: Stores the last leaf record left in the load_args->out_recdes
 * area to the last leaf page (or to the last overflow page).
 * Then it saves the last leaf page (and the last overflow page,
 * if there is one) to the disk.
 */
static bool
btree_save_last_leafrec (THREAD_ENTRY * thread_p, LOAD_ARGS * load_args)
{
  INT16 slotid;
  int sp_success;
  int cur_maxspace;
  int ret = NO_ERROR;
  BTREE_NODE_HEADER *header = NULL;

  if (load_args->overflowing == true)
    {
      /* Store the record in current overflow page */
      sp_success = spage_insert (thread_p, load_args->ovf.pgptr,
				 &load_args->out_recdes, &slotid);
      if (sp_success != SP_SUCCESS)
	{
	  goto exit_on_error;
	}

      assert (slotid > 0);

      /* Save the current overflow page */
      btree_log_page (thread_p, &load_args->btid->sys_btid->vfid,
		      load_args->ovf.pgptr);
      load_args->ovf.pgptr = NULL;

      /* update node header */
      header = btree_get_node_header (load_args->leaf.pgptr);
      if (header == NULL)
	{
	  goto exit_on_error;
	}

      *header = load_args->leaf.hdr;

      /* Save the current leaf page */
      btree_log_page (thread_p, &load_args->btid->sys_btid->vfid,
		      load_args->leaf.pgptr);
      load_args->leaf.pgptr = NULL;
    }
  else
    {				/* No overflowing */

      cur_maxspace =
	spage_max_space_for_new_record (thread_p, load_args->leaf.pgptr);
      if (((cur_maxspace - load_args->out_recdes.length) <
	   LOAD_FIXED_EMPTY_FOR_LEAF)
	  && (spage_number_of_records (load_args->leaf.pgptr) > 1))
	{

	  /* New record does not fit into the current leaf page (within
	   * the threshold value); so allocate a new leaf page and dump
	   * the current leaf page.
	   */

	  if (btree_proceed_leaf (thread_p, load_args) == NULL)
	    {
	      goto exit_on_error;
	    }

	}			/* get a new leaf */

      /* Insert the record to the current leaf page */
      sp_success = spage_insert (thread_p, load_args->leaf.pgptr,
				 &load_args->out_recdes, &slotid);
      if (sp_success != SP_SUCCESS)
	{
	  goto exit_on_error;
	}

      assert (slotid > 0);

      /* Update the node header information for this record */
      if (load_args->cur_key_len >= BTREE_MAX_KEYLEN_INPAGE)
	{
	  if (load_args->leaf.hdr.max_key_len < DISK_VPID_SIZE)
	    {
	      load_args->leaf.hdr.max_key_len = DISK_VPID_SIZE;
	    }
	}
      else
	{
	  if (load_args->leaf.hdr.max_key_len < load_args->cur_key_len)
	    {
	      load_args->leaf.hdr.max_key_len = load_args->cur_key_len;
	    }
	}

      /* Update the leaf header of the current leaf page */
      header = btree_get_node_header (load_args->leaf.pgptr);
      if (header == NULL)
	{
	  goto exit_on_error;
	}

      *header = load_args->leaf.hdr;

      /* Save the current leaf page */
      btree_log_page (thread_p, &load_args->btid->sys_btid->vfid,
		      load_args->leaf.pgptr);
      load_args->leaf.pgptr = NULL;
    }				/* No overflowing */

  return ret;

exit_on_error:

  if (load_args->leaf.pgptr)
    {
      pgbuf_unfix_and_init (thread_p, load_args->leaf.pgptr);
    }
  if (load_args->ovf.pgptr)
    {
      pgbuf_unfix_and_init (thread_p, load_args->ovf.pgptr);
    }

  return (ret == NO_ERROR
	  && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

/*
 * btree_connect_page () - Connect child page to the new parent page
 *   return: PAGE_PTR (pointer to the parent page if finished successfully,
 *                     or NULL in case of an error)
 *   key(in): Biggest key value (or, separator value in case of string
 *            keys) of the child page to be connected.
 *   max_key_len(in): size of the biggest key in this leaf page
 *   pageid(in): identifier of this leaf page
 *   load_args(in):
 *
 * Note: This function connects a page to its parent page. In the
 * parent level a new record is prepared with the given & pageid
 * parameters and inserted into current non-leaf page being
 * filled up. If the record does not fit into this page within
 * the given threshold boundaries (LOAD_FIXED_EMPTY_FOR_NONLEAF bytes of each
 * page are reserved for future insertions) then this page is
 * flushed to disk and a new page is allocated to become the
 * current non-leaf page.
 */

static PAGE_PTR
btree_connect_page (THREAD_ENTRY * thread_p, DB_VALUE * key, int max_key_len,
		    VPID * pageid, LOAD_ARGS * load_args, int node_level)
{
  NON_LEAF_REC nleaf_rec;
  INT16 slotid;
  int sp_success;
  int cur_maxspace;
  int key_len;
  int key_type = BTREE_NORMAL_KEY;
  BTREE_NODE_HEADER *header = NULL;

  /* form the leaf record (create the header & insert the key) */
  cur_maxspace =
    spage_max_space_for_new_record (thread_p, load_args->nleaf.pgptr);

  nleaf_rec.pnt = *pageid;
  key_len = btree_get_key_length (key);
  if (key_len < BTREE_MAX_KEYLEN_INPAGE)
    {
      nleaf_rec.key_len = key_len;
      key_type = BTREE_NORMAL_KEY;
    }
  else
    {
      nleaf_rec.key_len = -1;
      key_type = BTREE_OVERFLOW_KEY;

      if (VFID_ISNULL (&load_args->btid->ovfid))
	{
	  if (btree_create_overflow_key_file (thread_p,
					      load_args->btid) != NO_ERROR)
	    {
	      return NULL;
	    }
	}
    }

  if (btree_write_record (thread_p, load_args->btid, &nleaf_rec, key,
			  BTREE_NON_LEAF_NODE, key_type, key_len, true, NULL,
			  NULL, NULL, &load_args->out_recdes) != NO_ERROR)
    {
      return NULL;
    }

  if ((cur_maxspace - load_args->out_recdes.length) <
      LOAD_FIXED_EMPTY_FOR_NONLEAF)
    {

      /* New record does not fit into the current non-leaf page (within
       * the threshold value); so allocate a new non-leaf page and dump
       * the current non-leaf page.
       */

      /* Update the non-leaf page header */
      header = btree_get_node_header (load_args->nleaf.pgptr);
      if (header == NULL)
	{
	  return NULL;
	}

      *header = load_args->nleaf.hdr;

      /* Flush the current non-leaf page */
      btree_log_page (thread_p, &load_args->btid->sys_btid->vfid,
		      load_args->nleaf.pgptr);
      load_args->nleaf.pgptr = NULL;

      /* Insert the pageid to the linked list */
      if (add_list (&load_args->push_list,
		    &load_args->nleaf.vpid) != NO_ERROR)
	{
	  return NULL;
	}

      /* get a new non-leaf page */
      load_args->nleaf.pgptr =
	btree_get_page (thread_p, load_args->btid->sys_btid,
			&load_args->nleaf.vpid, &load_args->nleaf.vpid,
			&load_args->nleaf.hdr, node_level,
			&load_args->allocated_pgcnt, &load_args->used_pgcnt);
      if (load_args->nleaf.pgptr == NULL)
	{
	  return NULL;
	}
      load_args->max_recsize =
	spage_max_space_for_new_record (thread_p, load_args->nleaf.pgptr);

    }				/* get a new leaf */

  /* Insert the record to the current leaf page */
  sp_success = spage_insert (thread_p, load_args->nleaf.pgptr,
			     &load_args->out_recdes, &slotid);
  if (sp_success != SP_SUCCESS)
    {
      return NULL;
    }

  assert (slotid > 0);

  /* Update the node header information for this record */
  if (load_args->nleaf.hdr.max_key_len < max_key_len)
    {
      load_args->nleaf.hdr.max_key_len = max_key_len;
    }

  return load_args->nleaf.pgptr;
}

/*
 * btree_build_nleafs () -
 *   return: NO_ERROR
 *   load_args(in): Collection of information on how to build B+tree.
 *   n_nulls(in):
 *   n_oids(in):
 *   n_keys(in):
 *
 * Note: This function builds the non-leaf level nodes of the B+Tree
 * index in three phases. In the first phase, the first non-leaf
 * level nodes of the tree are constructed. Then, the upper
 * levels are built on top of this level, and finally, the last
 * non-leaf page (the single page of the top level) is updated
 * to become the root page.
 */
static int
btree_build_nleafs (THREAD_ENTRY * thread_p, LOAD_ARGS * load_args,
		    int n_nulls, int n_oids, int n_keys)
{
  RECDES temp_recdes;		/* Temporary record descriptor; */
  char *temp_data = NULL;
  VPID next_vpid;
  PAGE_PTR next_pageptr = NULL;

  /* Variables used in the second phase to go over lower level non-leaf
     pages */
  VPID cur_nleafpgid;
  PAGE_PTR cur_nleafpgptr = NULL;

  BTREE_NODE *temp;
  BTREE_ROOT_HEADER root_header_info, *root_header = NULL;
  BTREE_NODE_HEADER *header = NULL;
  int key_cnt;
  int max_key_len, new_max;
  LEAF_REC leaf_pnt;
  NON_LEAF_REC nleaf_pnt;
  /* Variables for keeping keys */
  DB_VALUE prefix_key;		/* Prefix key; prefix of last & first keys; used
				   only if type is one of the string types */
  int last_key_offset, first_key_offset;
  bool clear_last_key = false, clear_first_key = false;

  int ret = NO_ERROR;
  int node_level = 2;		/* leaf level = 1, lowest non-leaf level = 2 */
  RECDES rec;
  char rec_buf[IO_MAX_PAGE_SIZE + BTREE_MAX_ALIGN];
  DB_VALUE last_key;		/* Last key of the current page */
  DB_VALUE first_key;		/* First key of the next page; used only if
				   key_type is one of the string types */

  root_header = &root_header_info;

  rec.area_size = DB_PAGESIZE;
  rec.data = PTR_ALIGN (rec_buf, BTREE_MAX_ALIGN);

  DB_MAKE_NULL (&last_key);
  DB_MAKE_NULL (&first_key);
  DB_MAKE_NULL (&prefix_key);

  temp_data = (char *) os_malloc (DB_PAGESIZE);
  if (temp_data == NULL)
    {
      goto exit_on_error;
    }

  /*****************************************************
      PHASE I : Build the first non_leaf level nodes
   ****************************************************/

  assert (node_level == 2);

  /* Initialize the first non-leaf page */
  load_args->nleaf.pgptr = btree_get_page (thread_p,
					   load_args->btid->sys_btid,
					   &load_args->nleaf.vpid,
					   &load_args->nleaf.vpid,
					   &load_args->nleaf.hdr,
					   node_level,
					   &load_args->allocated_pgcnt,
					   &load_args->used_pgcnt);
  if (load_args->nleaf.pgptr == NULL)
    {
      goto exit_on_error;
    }

  load_args->max_recsize =
    spage_max_space_for_new_record (thread_p, load_args->nleaf.pgptr);
  load_args->push_list = NULL;
  load_args->pop_list = NULL;

  /* Access to the first leaf page of the btree */

  /* Initialize current_page to the first leaf page
   * Note: The first page is actually the second page, since
   *       first page is reserved for the root page.
   */

  /* Fetch the current page and obtain the header information */
  if (file_find_nthpages (thread_p, &load_args->btid->sys_btid->vfid,
			  &load_args->leaf.vpid, 1, 1) == -1)
    {
      goto exit_on_error;
    }

  DB_MAKE_NULL (&last_key);

  /* While there are some leaf pages do */
  while (!VPID_ISNULL (&(load_args->leaf.vpid)))
    {
      load_args->leaf.pgptr = pgbuf_fix (thread_p, &load_args->leaf.vpid,
					 OLD_PAGE, PGBUF_LATCH_WRITE,
					 PGBUF_UNCONDITIONAL_LATCH);
      if (load_args->leaf.pgptr == NULL)
	{
	  goto exit_on_error;
	}

      (void) pgbuf_check_page_ptype (thread_p, load_args->leaf.pgptr,
				     PAGE_BTREE);

      key_cnt = btree_node_number_of_keys (load_args->leaf.pgptr);
      assert (key_cnt > 0);

      /* obtain the header information for the leaf page */
      header = btree_get_node_header (load_args->leaf.pgptr);
      if (header == NULL)
	{
	  goto exit_on_error;
	}

      /* get the maximum key length on this leaf page */
      max_key_len = header->max_key_len;
      next_vpid = header->next_vpid;

      /* set level 2 to first non-leaf page */
      load_args->nleaf.hdr.node_level = node_level;

      /* Learn the first key of the leaf page */
      if (spage_get_record (load_args->leaf.pgptr, 1,
			    &temp_recdes, PEEK) != S_SUCCESS)

	{
	  goto exit_on_error;
	}

      btree_read_record (thread_p, load_args->btid, load_args->leaf.pgptr,
			 &temp_recdes, &first_key, &leaf_pnt, BTREE_LEAF_NODE,
			 &clear_first_key, &first_key_offset, PEEK_KEY_VALUE,
			 NULL);

      if (pr_is_prefix_key_type (TP_DOMAIN_TYPE (load_args->btid->key_type)))
	{
	  /*
	   * Key type is string or midxkey.
	   * Should insert the prefix key to the parent level
	   */
	  if (DB_IS_NULL (&last_key))
	    {
	      /* is the first leaf
	       * When the types of leaf node are char, nchar, bit,
	       * the type that is saved on non-leaf node is different.
	       * non-leaf spec (char -> varchar, nchar -> varnchar, bit -> varbit)
	       * hence it should be configured by using setval of nonleaf_key_type.
	       */
	      ret =
		(*(load_args->btid->nonleaf_key_type->type->setval))
		(&prefix_key, &first_key, true);
	      if (ret != NO_ERROR)
		{
		  assert (!"setval error");
		  goto exit_on_error;
		}
	    }
	  else
	    {
	      /* Insert the prefix key to the parent level */
	      ret = btree_get_prefix_separator (&last_key, &first_key,
						&prefix_key,
						load_args->btid->key_type);
	      if (ret != NO_ERROR)
		{
		  goto exit_on_error;
		}
	    }

	  /*
	   * We may need to update the max_key length if the mid key is
	   * larger than the max key length.  This will only happen when
	   * the varying key length is larger than the fixed key length
	   * in pathological cases like char(4)
	   */
	  new_max = btree_get_key_length (&prefix_key);
	  new_max = BTREE_GET_KEY_LEN_IN_PAGE (new_max);
	  max_key_len = MAX (new_max, max_key_len);

	  assert (node_level == 2);
	  if (btree_connect_page (thread_p, &prefix_key, max_key_len,
				  &load_args->leaf.vpid, load_args,
				  node_level) == NULL)
	    {
	      pr_clear_value (&prefix_key);
	      goto exit_on_error;
	    }

	  /* We always need to clear the prefix key.
	   * It is always a copy. */
	  pr_clear_value (&prefix_key);

	  btree_clear_key_value (&clear_last_key, &last_key);

	  /* Learn the last key of the leaf page */
	  assert (key_cnt > 0);
	  if (spage_get_record (load_args->leaf.pgptr, key_cnt,
				&temp_recdes, PEEK) != S_SUCCESS)
	    {
	      goto exit_on_error;
	    }

	  btree_read_record (thread_p, load_args->btid, load_args->leaf.pgptr,
			     &temp_recdes, &last_key, &leaf_pnt,
			     BTREE_LEAF_NODE, &clear_last_key,
			     &last_key_offset, PEEK_KEY_VALUE, NULL);
	}
      else
	{
	  max_key_len = BTREE_GET_KEY_LEN_IN_PAGE (max_key_len);

	  /* Insert this key to the parent level */
	  assert (node_level == 2);
	  if (btree_connect_page (thread_p, &first_key, max_key_len,
				  &load_args->leaf.vpid, load_args,
				  node_level) == NULL)
	    {
	      goto exit_on_error;
	    }
	}

      btree_clear_key_value (&clear_first_key, &first_key);

      /* Proceed the current leaf page pointer to the next leaf page */
      pgbuf_unfix_and_init (thread_p, load_args->leaf.pgptr);
      load_args->leaf.vpid = next_vpid;
      load_args->leaf.pgptr = next_pageptr;
      next_pageptr = NULL;

    }				/* while there are some more leaf pages */


  /* Update the non-leaf page header */
  header = btree_get_node_header (load_args->nleaf.pgptr);
  if (header == NULL)
    {
      goto exit_on_error;
    }

  *header = load_args->nleaf.hdr;

  /* Flush the last non-leaf page */
  btree_log_page (thread_p, &load_args->btid->sys_btid->vfid,
		  load_args->nleaf.pgptr);
  load_args->nleaf.pgptr = NULL;

  /* Insert the pageid to the linked list */
  ret = add_list (&load_args->push_list, &load_args->nleaf.vpid);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  btree_clear_key_value (&clear_last_key, &last_key);

  /*****************************************************
      PHASE II: Build the upper levels of tree
   ****************************************************/

  assert (node_level == 2);

  /* Switch the push and pop lists */
  load_args->pop_list = load_args->push_list;
  load_args->push_list = NULL;

  while (length_list (load_args->pop_list) > 1)
    {
      node_level++;

      /* while there are more than one page at the previous level do
         construct the next level */

      /* Initiliaze the first non-leaf page of current level */
      load_args->nleaf.pgptr =
	btree_get_page (thread_p, load_args->btid->sys_btid,
			&load_args->nleaf.vpid, &load_args->nleaf.vpid,
			&load_args->nleaf.hdr, node_level,
			&load_args->allocated_pgcnt, &load_args->used_pgcnt);
      if (load_args->nleaf.pgptr == NULL)
	{
	  goto exit_on_error;
	}

      load_args->max_recsize =
	spage_max_space_for_new_record (thread_p, load_args->nleaf.pgptr);

      do
	{			/* while pop_list is not empty */
	  /* Get current pageid from the poplist */
	  cur_nleafpgid = load_args->pop_list->pageid;

	  /* Fetch the current page */
	  cur_nleafpgptr = pgbuf_fix (thread_p, &cur_nleafpgid, OLD_PAGE,
				      PGBUF_LATCH_WRITE,
				      PGBUF_UNCONDITIONAL_LATCH);
	  if (cur_nleafpgptr == NULL)
	    {
	      goto exit_on_error;
	    }

	  (void) pgbuf_check_page_ptype (thread_p, cur_nleafpgptr,
					 PAGE_BTREE);

	  /* obtain the header information for the current non-leaf page */
	  header = btree_get_node_header (cur_nleafpgptr);
	  if (header == NULL)
	    {
	      goto exit_on_error;
	    }

	  /* get the maximum key length on this leaf page */
	  max_key_len = header->max_key_len;

	  /* Learn the first key of the current page */
	  /* Notice that since this is a non-leaf node */
	  if (spage_get_record (cur_nleafpgptr, 1, &temp_recdes,
				PEEK) != S_SUCCESS)
	    {
	      goto exit_on_error;
	    }

	  btree_read_record (thread_p, load_args->btid, cur_nleafpgptr,
			     &temp_recdes, &first_key, &nleaf_pnt,
			     BTREE_NON_LEAF_NODE, &clear_first_key,
			     &first_key_offset, PEEK_KEY_VALUE, NULL);

	  max_key_len = BTREE_GET_KEY_LEN_IN_PAGE (max_key_len);

	  /* set level to non-leaf page
	   * nleaf page could be changed in btree_connect_page */

	  assert (node_level > 2);

	  load_args->nleaf.hdr.node_level = node_level;

	  /* Insert this key to the parent level */
	  if (btree_connect_page (thread_p, &first_key, max_key_len,
				  &cur_nleafpgid, load_args,
				  node_level) == NULL)
	    {
	      goto exit_on_error;
	    }

	  pgbuf_unfix_and_init (thread_p, cur_nleafpgptr);

	  /* Remove this pageid from the pop list */
	  remove_first (&load_args->pop_list);

	  btree_clear_key_value (&clear_first_key, &first_key);
	}
      while (length_list (load_args->pop_list) > 0);

      /* FLUSH LAST NON-LEAF PAGE */

      /* Update the non-leaf page header */
      header = btree_get_node_header (load_args->nleaf.pgptr);
      if (header == NULL)
	{
	  goto exit_on_error;
	}

      *header = load_args->nleaf.hdr;

      /* Flush the last non-leaf page */
      btree_log_page (thread_p, &load_args->btid->sys_btid->vfid,
		      load_args->nleaf.pgptr);
      load_args->nleaf.pgptr = NULL;

      /* Insert the pageid to the linked list */
      ret = add_list (&load_args->push_list, &load_args->nleaf.vpid);
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}

      /* Switch push and pop lists */
      temp = load_args->pop_list;
      load_args->pop_list = load_args->push_list;
      load_args->push_list = temp;
    }

  /* Deallocate the last node (only one exists) */
  remove_first (&load_args->pop_list);

  /******************************************
      PHASE III: Update the root page
   *****************************************/

  /* Retrieve the last non-leaf page (the current one); guaranteed to exist */
  /* Fetch the current page */
  load_args->nleaf.pgptr = pgbuf_fix (thread_p, &load_args->nleaf.vpid,
				      OLD_PAGE, PGBUF_LATCH_WRITE,
				      PGBUF_UNCONDITIONAL_LATCH);
  if (load_args->nleaf.pgptr == NULL)
    {
      goto exit_on_error;
    }

  (void) pgbuf_check_page_ptype (thread_p, load_args->nleaf.pgptr,
				 PAGE_BTREE);

  /* Prepare the root header by using the last leaf node header */
  root_header->node.max_key_len = load_args->nleaf.hdr.max_key_len;
  root_header->node.node_level = node_level;
  VPID_SET_NULL (&(root_header->node.next_vpid));
  VPID_SET_NULL (&(root_header->node.prev_vpid));
  root_header->node.split_info.pivot = 0.0f;
  root_header->node.split_info.index = 0;

  if (load_args->btid->unique_pk)
    {
      root_header->num_nulls = n_nulls;
      root_header->num_oids = n_oids;
      root_header->num_keys = n_keys;
      root_header->unique_pk = load_args->btid->unique_pk;

      assert (BTREE_IS_UNIQUE (root_header->unique_pk));
      assert (BTREE_IS_PRIMARY_KEY (root_header->unique_pk)
	      || !BTREE_IS_PRIMARY_KEY (root_header->unique_pk));
    }
  else
    {
      root_header->num_nulls = -1;
      root_header->num_oids = -1;
      root_header->num_keys = -1;
      root_header->unique_pk = 0;
    }

  COPY_OID (&(root_header->topclass_oid), &load_args->btid->topclass_oid);

  root_header->ovfid = load_args->btid->ovfid;	/* structure copy */
  root_header->rev_level = BTREE_CURRENT_REV_LEVEL;

  /* change node header as root header */
  if (btree_pack_root_header
      (&rec, root_header, load_args->btid->key_type) != NO_ERROR)
    {
      goto exit_on_error;
    }

  if (spage_update (thread_p, load_args->nleaf.pgptr, HEADER, &rec) !=
      SP_SUCCESS)
    {
      goto exit_on_error;
    }

  /* move current ROOT page content to the first page allocated */
  if (file_find_nthpages (thread_p, &load_args->btid->sys_btid->vfid,
			  &cur_nleafpgid, 0, 1) != 1)
    {
      goto exit_on_error;
    }
  next_pageptr = pgbuf_fix (thread_p, &cur_nleafpgid, NEW_PAGE,
			    PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (next_pageptr == NULL)
    {
      goto exit_on_error;
    }

  (void) pgbuf_set_page_ptype (thread_p, next_pageptr, PAGE_BTREE);

  memcpy (next_pageptr, load_args->nleaf.pgptr, DB_PAGESIZE);
  pgbuf_unfix_and_init (thread_p, load_args->nleaf.pgptr);

  if (load_args->used_pgcnt > load_args->allocated_pgcnt)
    {
      /* deallocate this last page of index file
       * Note: If used_pgcnt is less than init_pgcnt, this last page
       * will be deleted on page resource release at the end.
       */
      ret = file_dealloc_page (thread_p, &load_args->btid->sys_btid->vfid,
			       &load_args->nleaf.vpid);
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}
    }
  load_args->used_pgcnt--;

  load_args->nleaf.pgptr = next_pageptr;
  next_pageptr = NULL;
  load_args->nleaf.vpid = cur_nleafpgid;

  /*
   * The root page must be logged, otherwise, in the event of a crash. The
   * index may be gone.
   */
  btree_log_page (thread_p, &load_args->btid->sys_btid->vfid,
		  load_args->nleaf.pgptr);
  /* Root was unfixed */
  load_args->nleaf.pgptr = NULL;

end:

  /* cleanup */
  if (next_pageptr)
    {
      pgbuf_unfix_and_init (thread_p, next_pageptr);
    }
  if (cur_nleafpgptr)
    {
      pgbuf_unfix_and_init (thread_p, cur_nleafpgptr);
    }
  if (load_args->leaf.pgptr)
    {
      pgbuf_unfix_and_init (thread_p, load_args->leaf.pgptr);
    }
  if (load_args->nleaf.pgptr)
    {
      pgbuf_unfix_and_init (thread_p, load_args->nleaf.pgptr);
    }
  if (temp_data)
    {
      os_free_and_init (temp_data);
    }

  btree_clear_key_value (&clear_last_key, &last_key);
  btree_clear_key_value (&clear_first_key, &first_key);

  return ret;

/* error handling */
exit_on_error:

  ret = (ret == NO_ERROR
	 && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
  goto end;
}

/*
 * btree_log_page () - Save the contents of the buffer
 *   return: nothing
 *   vfid(in):
 *   page_ptr(in): pointer to the page buffer to be saved
 *
 * Note: This function logs the contents of the given page and frees
 * the page after setting on the dirty bit.
 */
static void
btree_log_page (THREAD_ENTRY * thread_p, VFID * vfid, PAGE_PTR page_ptr)
{
  LOG_DATA_ADDR addr;		/* For recovery purposes */

  /* log the whole page for redo purposes. */
  addr.vfid = vfid;
  addr.pgptr = page_ptr;
  addr.offset = -1;		/* irrelevant */
  log_append_redo_data (thread_p, RVBT_COPYPAGE, &addr, DB_PAGESIZE,
			page_ptr);

  pgbuf_set_dirty (thread_p, page_ptr, FREE);
  page_ptr = NULL;
}

/*
 * btree_get_page () - Get a new page for the b+tree index
 *   return: PAGE_PTR
 *   btid(in): Identifier of the B+Tree index
 *   page_id(in): Set to the page identifier for the newly allocated page
 *   nearpg(in): A page identifier that may be used in a nearby page.
 *               allocation. (It may be ignored.)
 *   header(in): header to be initialized and inserted into the new page
 *               (must be NULL if the new page is going to be an overflow page)
 *   node_type(in): type of the node the page will be used as (either 1,
 *                  or 0)
 *   allocated_pgcnt(in): The number of pages already allocated
 *   used_pgcnt(in): The number of in use pages that has been allocated
 *
 * Note: This function allocates a new page for the B+Tree index.
 * The page is initialized and the node header is inserted before
 * returning the page for further usage.
 */
static PAGE_PTR
btree_get_page (THREAD_ENTRY * thread_p, BTID * btid, VPID * page_id,
		VPID * nearpg, BTREE_NODE_HEADER * header,
		int node_level, int *allocated_pgcnt, int *used_pgcnt)
{
  PAGE_PTR page_ptr = NULL;
  VPID ovf_vpid = { NULL_PAGEID, NULL_VOLID };
  RECDES temp_recdes;		/* Temporary record descriptor; */
  VPID vpid;
  LOG_DATA_ADDR addr;
  int nthpage;
  OR_ALIGNED_BUF (sizeof (BTREE_NODE_HEADER)) a_temp_data;
  unsigned short alignment;

  assert ((header != NULL && node_level >= 1)
	  || (header == NULL && node_level == -1));

  temp_recdes.data = OR_ALIGNED_BUF_START (a_temp_data);
  temp_recdes.area_size = sizeof (BTREE_NODE_HEADER);

  if (*used_pgcnt < *allocated_pgcnt)
    {				/* index has still unused pages */
      if (file_find_nthpages (thread_p, &btid->vfid, page_id, *used_pgcnt, 1)
	  <= 0)
	{
	  return NULL;
	}
    }
  else
    {
      /*
       * Note: We do not initialize the allocated pages during the allocation
       *       since they belong to a new file and we do not perform any undo
       *       logging on it. In fact some of the pages may be returned to the
       *       file manager at a later point. We should not waste time and
       *       storage to initialize those pages at this moment. This is safe
       *       since this is a new file.
       */

      if (file_alloc_pages_as_noncontiguous (thread_p, &btid->vfid, page_id,
					     &nthpage, BTREE_NUM_ALLOC_PAGES,
					     nearpg, NULL, NULL,
					     NULL) != NULL)
	{
	  *allocated_pgcnt += BTREE_NUM_ALLOC_PAGES;
	}
      else
	{
	  if (file_alloc_pages (thread_p, &btid->vfid, page_id, 1, nearpg,
				NULL, NULL) == NULL)
	    {
	      return NULL;
	    }
	  else
	    {
	      *allocated_pgcnt += 1;
	    }
	}
    }
  vpid = *page_id;

  page_ptr = pgbuf_fix (thread_p, &vpid, NEW_PAGE, PGBUF_LATCH_WRITE,
			PGBUF_UNCONDITIONAL_LATCH);
  if (page_ptr == NULL)
    {
      (void) file_dealloc_page (thread_p, &btid->vfid, page_id);
      return NULL;
    }				/* if */
  (*used_pgcnt)++;

  (void) pgbuf_set_page_ptype (thread_p, page_ptr, PAGE_BTREE);

  alignment = BTREE_MAX_ALIGN;
  spage_initialize (thread_p, page_ptr, UNANCHORED_KEEP_SEQUENCE,
		    alignment, DONT_SAFEGUARD_RVSPACE);

  addr.vfid = &btid->vfid;
  addr.offset = -1;		/* No header slot is initialized */
  addr.pgptr = page_ptr;

  log_append_redo_data (thread_p, RVBT_GET_NEWPAGE, &addr, sizeof (alignment),
			&alignment);

  if (header)
    {				/* This is going to be a leaf page */
      /* Insert the node header (with initial values) to the leaf node */
      header->node_level = node_level;
      header->max_key_len = 0;
      VPID_SET_NULL (&header->next_vpid);
      VPID_SET_NULL (&header->prev_vpid);
      header->split_info.pivot = 0.0f;
      header->split_info.index = 0;

      if (btree_init_node_header
	  (thread_p, &btid->vfid, page_ptr, header, false) != NO_ERROR)
	{
	  return NULL;
	}
    }
  else
    {				/* This is going to be an overflow page */
      BTREE_OVERFLOW_HEADER ovf_header_info, *ovf_header = NULL;

      ovf_header = &ovf_header_info;

      assert (node_level == -1);
      ovf_header->next_vpid = ovf_vpid;

      if (btree_init_overflow_header (thread_p, page_ptr, ovf_header) !=
	  NO_ERROR)
	{
	  return NULL;
	}
    }

  return page_ptr;
}

/*
 * btree_proceed_leaf () - Proceed the current leaf page to a new one
 *   return: PAGE_PTR (pointer to the new leaf page, or NULL in
 *                     case of an error).
 *   load_args(in): Collection of information on how to build B+tree.
 *
 * Note: This function proceeds the current leaf page pointer from a
 * full leaf page to a new (completely empty) one. It first
 * allocates a new leaf page and connects it to the current
 * leaf page; then, it dumps the current one; and finally makes
 * the new leaf page "current".
 */
static PAGE_PTR
btree_proceed_leaf (THREAD_ENTRY * thread_p, LOAD_ARGS * load_args)
{
  /* Local variables for managing leaf & overflow pages */
  VPID new_leafpgid;
  PAGE_PTR new_leafpgptr = NULL;
  BTREE_NODE_HEADER new_leafhdr, *header = NULL;
  RECDES temp_recdes;		/* Temporary record descriptor; */
  OR_ALIGNED_BUF (sizeof (BTREE_NODE_HEADER)) a_temp_data;

  temp_recdes.data = OR_ALIGNED_BUF_START (a_temp_data);
  temp_recdes.area_size = sizeof (BTREE_NODE_HEADER);

  /* Allocate the new leaf page */
  new_leafpgptr = btree_get_page (thread_p, load_args->btid->sys_btid,
				  &new_leafpgid,
				  &load_args->leaf.vpid,
				  &new_leafhdr, 1,
				  &load_args->allocated_pgcnt,
				  &load_args->used_pgcnt);
  if (new_leafpgptr == NULL)
    {
      return NULL;
    }

  /* new leaf update header */
  new_leafhdr.prev_vpid = load_args->leaf.vpid;

  header = btree_get_node_header (new_leafpgptr);
  if (header == NULL)
    {
      pgbuf_unfix_and_init (thread_p, new_leafpgptr);
      return NULL;
    }

  *header = new_leafhdr;	/* update header */

  /* current leaf update header */
  /* make the current leaf page point to the new one */
  load_args->leaf.hdr.next_vpid = new_leafpgid;

  /* and the new leaf point to the current one */
  header = btree_get_node_header (load_args->leaf.pgptr);
  if (header == NULL)
    {
      pgbuf_unfix_and_init (thread_p, new_leafpgptr);
      return NULL;
    }

  *header = load_args->leaf.hdr;

  /* Flush the current leaf page */
  btree_log_page (thread_p, &load_args->btid->sys_btid->vfid,
		  load_args->leaf.pgptr);
  load_args->leaf.pgptr = NULL;

  /* Make the new leaf page become the current one */
  load_args->leaf.vpid = new_leafpgid;
  load_args->leaf.pgptr = new_leafpgptr;
  load_args->leaf.hdr = new_leafhdr;

  return load_args->leaf.pgptr;
}

/*
 * btree_first_oid () - Prepare record for the key and its first oid
 *   return: int
 *   this_key(in): Key
 *   class_oid(in):
 *   first_oid(in): First OID associated with this key; inserted into the
 *                  record.
 *   load_args(in): Contains fields specifying where & how to create the record
 *
 * Note: This function prepares the leaf record for the given key and
 * its first OID at the storage location specified by
 * load_args->out_recdes field.
 */
static int
btree_first_oid (THREAD_ENTRY * thread_p, DB_VALUE * this_key,
		 OID * class_oid, OID * first_oid,
		 MVCC_REC_HEADER * p_mvcc_rec_header, LOAD_ARGS * load_args)
{
  int key_len;
  int key_type;
  int error;

  /* form the leaf record (create the header & insert the key) */
  key_len = load_args->cur_key_len = btree_get_key_length (this_key);
  if (key_len < BTREE_MAX_KEYLEN_INPAGE)
    {
      key_type = BTREE_NORMAL_KEY;
    }
  else
    {
      key_type = BTREE_OVERFLOW_KEY;
      if (VFID_ISNULL (&load_args->btid->ovfid))
	{
	  error = btree_create_overflow_key_file (thread_p, load_args->btid);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	}
    }
  error =
    btree_write_record (thread_p, load_args->btid, NULL, this_key,
			BTREE_LEAF_NODE, key_type, key_len, true, class_oid,
			first_oid, p_mvcc_rec_header, &load_args->out_recdes);
  if (error != NO_ERROR)
    {
      /* this must be an overflow key insertion failure, we assume the
       * overflow manager has logged an error.
       */
      return error;
    }

  /* Set the location where the new oid should be inserted */
  load_args->new_pos = (load_args->out_recdes.data +
			load_args->out_recdes.length);

  /* Set the current key value to this_key */
  pr_clear_value (&load_args->current_key);	/* clear previous value */
  load_args->cur_key_len = key_len;

  return pr_clone_value (this_key, &load_args->current_key);
}

/*
 * btree_construct_leafs () - Output function for index sorting;constructs leaf
 *                         nodes
 *   return: int
 *   in_recdes(in): specifies the next sort item in the sorting order.
 *   arg(in): output arguments; provides information about how to use the
 *            next item in the sorted list to construct the leaf nodes of
 *            the Btree.
 *
 * Note: This function creates the btree leaf nodes. It is passed
 * by the "btree_index_sort" function to the "sort_listfile" function
 * to use the sort items to build the records and pages of the btree.
 */
static int
btree_construct_leafs (THREAD_ENTRY * thread_p, const RECDES * in_recdes,
		       void *arg)
{
  int ret = NO_ERROR;
  LOAD_ARGS *load_args;
  DB_VALUE this_key;		/* Key value in this sorted item
				   (specified with in_recdes) */
  OID this_class_oid = oid_Null_oid;	/* Class OID value in this sorted item */
  OID this_oid = oid_Null_oid;	/* OID value in this sorted item */
  int sp_success;
  int cur_maxspace;
  INT16 slotid;
  bool same_key = true;
  VPID new_ovfpgid;
  PAGE_PTR new_ovfpgptr = NULL;
  OR_BUF buf;
  bool copy = false;
  RECDES sort_key_recdes, *recdes;
  char *next;
  int next_size;
  int key_size = -1;
  int max_key_len;
  int rec_length;
  BTREE_OVERFLOW_HEADER *ovf_header = NULL;

  int oid_size = OR_OID_SIZE;
  int fixed_mvccid_size = 0;
  MVCC_REC_HEADER mvcc_header;
  MVCC_BTREE_OP_ARGUMENTS mvcc_args;

  load_args = (LOAD_ARGS *) arg;

  if (mvcc_Enabled)
    {
      (void) logtb_get_current_mvccid (thread_p);
      fixed_mvccid_size = 2 * OR_MVCCID_SIZE;
      if (BTREE_IS_UNIQUE (load_args->btid->unique_pk))
	{
	  oid_size = 2 * OR_OID_SIZE;
	}
    }

  sort_key_recdes = *in_recdes;
  recdes = &sort_key_recdes;

  next_size = sizeof (char *);

  for (;;)
    {				/* Infinite loop; will exit with break statement */

      next = *(char **) recdes->data;	/* save forward link */

      /* First decompose the input record into the key and oid components */
      or_init (&buf, recdes->data, recdes->length);
      assert (buf.ptr == PTR_ALIGN (buf.ptr, MAX_ALIGNMENT));

      /* Skip forward link, value_has_null */
      or_advance (&buf, next_size + OR_INT_SIZE);

      assert (buf.ptr == PTR_ALIGN (buf.ptr, INT_ALIGNMENT));

      /* Instance level uniqueness checking */
      if (BTREE_IS_UNIQUE (load_args->btid->unique_pk))
	{			/* unique index */
	  /* extract class OID */
	  ret = or_get_oid (&buf, &this_class_oid);
	  if (ret != NO_ERROR)
	    {
	      goto error;
	    }
	}

      /* Get OID */
      ret = or_get_oid (&buf, &this_oid);
      if (ret != NO_ERROR)
	{
	  goto error;
	}

      /* Create MVCC header */
      BTREE_INIT_MVCC_HEADER (&mvcc_header);

      ret = or_get_mvccid (&buf, &MVCC_GET_INSID (&mvcc_header));
      if (ret != NO_ERROR)
	{
	  goto error;
	}

      ret = or_get_mvccid (&buf, &MVCC_GET_DELID (&mvcc_header));
      if (ret != NO_ERROR)
	{
	  goto error;
	}

      if (MVCC_GET_INSID (&mvcc_header) != MVCCID_ALL_VISIBLE)
	{
	  /* Set valid insert MVCCID flag */
	  MVCC_SET_FLAG_BITS (&mvcc_header, OR_MVCC_FLAG_VALID_INSID);
	}

      if (MVCC_GET_DELID (&mvcc_header) != MVCCID_NULL)
	{
	  /* Set valid delete MVCCID */
	  MVCC_SET_FLAG_BITS (&mvcc_header, OR_MVCC_FLAG_VALID_DELID);
	}

      assert (buf.ptr == PTR_ALIGN (buf.ptr, INT_ALIGNMENT));

      if (prm_get_bool_value (PRM_ID_LOG_BTREE_OPS))
	{
	  _er_log_debug (ARG_FILE_LINE, "DEBUG_BTREE: load new "
			 "object(%d, %d, %d) "
			 "class(%d, %d, %d) and btid(%d, (%d, %d)) with "
			 "mvccinfo=%lld | %lld",
			 this_oid.volid, this_oid.pageid, this_oid.slotid,
			 this_class_oid.volid, this_class_oid.pageid,
			 this_class_oid.slotid,
			 load_args->btid->sys_btid->root_pageid,
			 load_args->btid->sys_btid->vfid.volid,
			 load_args->btid->sys_btid->vfid.fileid,
			 MVCC_GET_INSID (&mvcc_header),
			 MVCC_GET_DELID (&mvcc_header));
	}

      /* Do not copy the string--just use the pointer.  The pr_ routines
       * for strings and sets have different semantics for length.
       */
      if (TP_DOMAIN_TYPE (load_args->btid->key_type) == DB_TYPE_MIDXKEY)
	{
	  key_size = CAST_STRLEN (buf.endptr - buf.ptr);
	}

      ret =
	(*(load_args->btid->key_type->type->data_readval)) (&buf, &this_key,
							    load_args->btid->
							    key_type,
							    key_size, copy,
							    NULL, 0);
      if (ret != NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TF_CORRUPTED, 0);
	  goto error;
	}

      /* Find out if this is the first call to this function */
      if (VPID_ISNULL (&(load_args->leaf.vpid)))
	{
	  /* This is the first call to this function; so, initialize some fields
	     in the LOAD_ARGS structure */

	  /* Allocate the first page for the index */
	  load_args->leaf.pgptr =
	    btree_get_page (thread_p, load_args->btid->sys_btid,
			    &load_args->leaf.vpid, &load_args->leaf.vpid,
			    &load_args->leaf.hdr, 1,
			    &load_args->allocated_pgcnt,
			    &load_args->used_pgcnt);
	  if (load_args->leaf.pgptr == NULL)
	    {
	      goto error;
	    }

#if 0				/* TODO: currently not used */
	  load_args->first_leafpgid = load_args->leaf.vpid;
#endif
	  load_args->overflowing = false;
	  assert (load_args->ovf.pgptr == NULL);

	  /* Create the first record of the current page in main memory */
	  ret = btree_first_oid (thread_p, &this_key, &this_class_oid,
				 &this_oid, &mvcc_header, load_args);
	  if (ret != NO_ERROR)
	    {
	      goto error;
	    }

	  if (!MVCC_IS_HEADER_DELID_VALID (&mvcc_header))
	    {
	      /* Object was not deleted, increment curr_non_del_obj_count */
	      load_args->curr_non_del_obj_count = 1;

	      /* Increment the key counter if object is not deleted */
	      (load_args->n_keys)++;
	    }
	  else
	    {
	      /* Object is deleted, initialize curr_non_del_obj_count as 0 */
	      load_args->curr_non_del_obj_count = 0;
	    }

	  max_key_len = BTREE_GET_KEY_LEN_IN_PAGE (load_args->cur_key_len);
	  load_args->max_recsize = max_key_len + BTREE_MAX_OIDLEN_INPAGE;

#if !defined (NDEBUG)
	  btree_check_valid_record (NULL, load_args->btid,
				    &load_args->out_recdes, BTREE_LEAF_NODE,
				    NULL);
#endif
	}
      else
	{			/* This is not the first call to this function */
	  int c = DB_UNK;

	  /*
	   * Compare the received key with the current one.
	   * If different, then dump the current record and create a new record.
	   */

	  c = btree_compare_key (&this_key, &load_args->current_key,
				 load_args->btid->key_type, 0, 1, NULL);
	  if (c == DB_EQ || c == DB_GT)
	    {
	      ;			/* ok */
	    }
	  else
	    {
	      assert_release (false);
	      goto error;
	    }

	  same_key = (c == DB_EQ) ? true : false;

	  /* EQUALITY test only - doesn't care the reverse index */

	  if (same_key)
	    {
	      /* This key (retrieved key) is the same with the current one. */
	      if (!MVCC_IS_HEADER_DELID_VALID (&mvcc_header))
		{
		  load_args->curr_non_del_obj_count++;
		}
	      if (load_args->curr_non_del_obj_count == 1)
		{
		  /* When first non-deleted object is found, we must
		   * increment that number of keys for statistics.
		   */
		  (load_args->n_keys)++;
		}
	      if (BTREE_IS_UNIQUE (load_args->btid->unique_pk))
		{
		  /* instance level uniqueness checking */
		  if (load_args->curr_non_del_obj_count > 1)
		    {
		      /* Unique constrain violation - more than one visible
		       * records for this key.
		       */
		      BTREE_SET_UNIQUE_VIOLATION_ERROR (thread_p, &this_key,
							&this_oid,
							&this_class_oid,
							load_args->btid->
							sys_btid,
							load_args->bt_name);
		      ret = ER_BTREE_UNIQUE_FAILED;
		      goto error;
		    }
		  else if (load_args->curr_non_del_obj_count == 1)
		    {
		      /* this is the first non-deleted OID of the key; it
		       * must be placed as the first OID 
		       */
		      MVCC_REC_HEADER first_mvcc_header;
		      OID first_oid, first_class_oid;
		      RECDES replace_recdes;
		      RECDES *replace_recdes_p;
		      int offset = 0;

		      /* Safe guard */
		      assert_release (load_args->curr_non_del_obj_count == 1);

		      if (load_args->overflowing)
			{
			  /* OID was already written to page so create a recdes
			   * pointing to page; NOTE: we know the first OID is
			   * of fixed size 
			   */
			  ret =
			    spage_get_record (load_args->leaf.pgptr,
					      load_args->
					      last_leaf_insert_slotid,
					      &replace_recdes, PEEK);
			  if (ret != S_SUCCESS)
			    {
			      goto error;
			    }
			  replace_recdes_p = &replace_recdes;

			  /* mark current MVCC header as fixed size */
			  BTREE_MVCC_SET_HEADER_FIXED_SIZE (&mvcc_header);
			}
		      else
			{
			  /* record was not written, make changes in transient
			     record */
			  replace_recdes_p = &load_args->out_recdes;
			}

		      /* retrieve the first OID from unwritten record */
		      ret =
			btree_leaf_get_first_oid (load_args->btid,
						  &load_args->out_recdes,
						  &first_oid,
						  &first_class_oid,
						  &first_mvcc_header);
		      if (ret != NO_ERROR)
			{
			  goto error;
			}

		      /* replace with current OID (might move memory in
		         record) */
		      btree_leaf_change_first_oid (&load_args->out_recdes,
						   load_args->btid,
						   &this_oid,
						   &this_class_oid,
						   &mvcc_header, &offset);
		      if (ret != NO_ERROR)
			{
			  goto error;
			}
		      /* Update load_args->new_pos in case record has grown
		       * after replace
		       */
		      load_args->new_pos += offset;

#if !defined (NDEBUG)
		      btree_check_valid_record (NULL, load_args->btid,
						&load_args->out_recdes,
						(load_args->
						 overflowing ?
						 BTREE_OVERFLOW_NODE :
						 BTREE_LEAF_NODE), NULL);
#endif

		      /* save first OID as current OID, to be written */
		      COPY_OID (&this_oid, &first_oid);
		      COPY_OID (&this_class_oid, &first_class_oid);
		      mvcc_header = first_mvcc_header;
		    }
		}

	      /* Check if there is space in the memory record for the new OID.
	         If not dump the current record and create a new record. */

	      if (((load_args->new_pos + oid_size + DISK_VPID_SIZE
		    + fixed_mvccid_size) - load_args->out_recdes.data)
		  > load_args->max_recsize)
		{		/* There is no space for the new oid */
		  if (load_args->overflowing == true)
		    {
		      /* Store the record in current overflow page */
		      sp_success = spage_insert (thread_p,
						 load_args->ovf.pgptr,
						 &load_args->out_recdes,
						 &slotid);
		      if (sp_success != SP_SUCCESS)
			{
			  goto error;
			}

		      assert (slotid > 0);

		      /* Allocate the new overflow page */
		      new_ovfpgptr =
			btree_get_page (thread_p, load_args->btid->sys_btid,
					&new_ovfpgid, &load_args->ovf.vpid,
					NULL, -1,
					&load_args->allocated_pgcnt,
					&load_args->used_pgcnt);
		      if (new_ovfpgptr == NULL)
			{
			  goto error;
			}

		      /* make the current overflow page point to the new one */
		      ovf_header =
			btree_get_overflow_header (load_args->ovf.pgptr);
		      if (ovf_header == NULL)
			{
			  goto error;
			}

		      ovf_header->next_vpid = new_ovfpgid;

		      /* Save the current overflow page */
		      btree_log_page (thread_p,
				      &load_args->btid->sys_btid->vfid,
				      load_args->ovf.pgptr);
		      load_args->ovf.pgptr = NULL;

		      /* Make the new overflow page become the current one */
		      load_args->ovf.vpid = new_ovfpgid;
		      load_args->ovf.pgptr = new_ovfpgptr;
		      new_ovfpgptr = NULL;
		    }
		  else
		    {		/* Current page is a leaf page */
		      cur_maxspace =
			spage_max_space_for_new_record (thread_p,
							load_args->leaf.
							pgptr);

		      /* Check whether there is enough space for adding
		       * VPID of first overflow in record and also for
		       * setting first object fixed size.
		       */
		      rec_length =
			load_args->new_pos - load_args->out_recdes.data
			+ DB_ALIGN (DISK_VPID_SIZE, INT_ALIGNMENT)
			+ (BTREE_IS_UNIQUE (load_args->btid->unique_pk)
			   ? OR_OID_SIZE : 0) + fixed_mvccid_size;

		      if (cur_maxspace <
			  rec_length + LOAD_FIXED_EMPTY_FOR_LEAF)
			{
			  if (btree_proceed_leaf (thread_p, load_args) ==
			      NULL)
			    {
			      goto error;
			    }
			}

		      /* Allocate the new overflow page */
		      load_args->ovf.pgptr =
			btree_get_page (thread_p, load_args->btid->sys_btid,
					&load_args->ovf.vpid,
					&load_args->ovf.vpid, NULL, -1,
					&load_args->allocated_pgcnt,
					&load_args->used_pgcnt);
		      if (load_args->ovf.pgptr == NULL)
			{
			  goto error;
			}

		      /* Connect the new overflow page to the leaf page */
		      btree_leaf_new_overflow_oids_vpid
			(&load_args->out_recdes, &load_args->ovf.vpid,
			 load_args->btid->unique_pk, &this_class_oid);

		      /* Try to Store the record into the current leaf page */
		      sp_success = spage_insert (thread_p,
						 load_args->leaf.pgptr,
						 &load_args->out_recdes,
						 &load_args->
						 last_leaf_insert_slotid);
		      if (sp_success != SP_SUCCESS)
			{
			  goto error;
			}

		      assert (load_args->last_leaf_insert_slotid > 0);

		      /* Update the node header information for this record */
		      if (load_args->cur_key_len >= BTREE_MAX_KEYLEN_INPAGE)
			{
			  if (load_args->leaf.hdr.max_key_len <
			      DISK_VPID_SIZE)
			    {
			      load_args->leaf.hdr.max_key_len =
				DISK_VPID_SIZE;
			    }
			}
		      else
			{
			  if (load_args->leaf.hdr.max_key_len <
			      load_args->cur_key_len)
			    {
			      load_args->leaf.hdr.max_key_len =
				load_args->cur_key_len;
			    }
			}

		      load_args->overflowing = true;
		    }		/* Current page is a leaf page */

		  /* Initialize the memory area for the next record */
		  load_args->out_recdes.length = 0;
		  load_args->new_pos = load_args->out_recdes.data;
		  load_args->max_recsize =
		    spage_max_space_for_new_record (thread_p,
						    load_args->ovf.pgptr);
		}		/* no space for the new OID */

	      if (mvcc_Enabled)
		{
		  if (load_args->overflowing
		      || BTREE_IS_UNIQUE (load_args->btid->unique_pk))
		    {
		      /* all overflow OIDs have fixed header size; also, all
		         OIDs of a unique index key (except the first one) have
		         fixed size */
		      BTREE_MVCC_SET_HEADER_FIXED_SIZE (&mvcc_header);
		    }

		  /* Insert new OID, class OID (for unique), and MVCCID's. */
		  /* Insert OID (and MVCC flags) */
		  btree_set_mvcc_flags_into_oid (&mvcc_header, &this_oid);
		  OR_PUT_OID (load_args->new_pos, &this_oid);
		  load_args->out_recdes.length += OR_OID_SIZE;
		  load_args->new_pos += OR_OID_SIZE;

		  if (BTREE_IS_UNIQUE (load_args->btid->unique_pk))
		    {
		      /* Insert class OID */
		      OR_PUT_OID (load_args->new_pos, &this_class_oid);
		      load_args->out_recdes.length += OR_OID_SIZE;
		      load_args->new_pos += OR_OID_SIZE;
		    }

		  /* Insert MVCCID's */
		  load_args->out_recdes.length +=
		    btree_packed_mvccinfo_size (&mvcc_header);
		  load_args->new_pos =
		    btree_pack_mvccinfo (load_args->new_pos, &mvcc_header);

#if !defined (NDEBUG)
		  btree_check_valid_record (NULL, load_args->btid,
					    &load_args->out_recdes,
					    (load_args->
					     overflowing ? BTREE_OVERFLOW_NODE
					     : BTREE_LEAF_NODE), NULL);
#endif
		}
	    }			/* same key */
	  else
	    {
	      /* Current key is finished; dump this output record to the disk page */

	      /* Insert the current record to the current page */
	      if (load_args->overflowing == false)
		{
		  cur_maxspace =
		    spage_max_space_for_new_record (thread_p,
						    load_args->leaf.pgptr);

		  if (((cur_maxspace - load_args->out_recdes.length)
		       < LOAD_FIXED_EMPTY_FOR_LEAF)
		      && (spage_number_of_records (load_args->leaf.pgptr) >
			  1))
		    {
		      /* New record does not fit into the current leaf page (within
		       * the threshold value); so allocate a new leaf page and dump
		       * the current leaf page.
		       */

		      if (btree_proceed_leaf (thread_p, load_args) == NULL)
			{
			  goto error;
			}

		    }		/* get a new leaf */

		  /* Insert the record to the current leaf page */
		  sp_success = spage_insert (thread_p, load_args->leaf.pgptr,
					     &load_args->out_recdes,
					     &load_args->
					     last_leaf_insert_slotid);
		  if (sp_success != SP_SUCCESS)
		    {
		      goto error;
		    }

		  assert (load_args->last_leaf_insert_slotid > 0);

		  /* Update the node header information for this record */
		  if (load_args->cur_key_len >= BTREE_MAX_KEYLEN_INPAGE)
		    {
		      if (load_args->leaf.hdr.max_key_len < DISK_VPID_SIZE)
			{
			  load_args->leaf.hdr.max_key_len = DISK_VPID_SIZE;
			}
		    }
		  else
		    {
		      if (load_args->leaf.hdr.max_key_len <
			  load_args->cur_key_len)
			{
			  load_args->leaf.hdr.max_key_len =
			    load_args->cur_key_len;
			}
		    }

		}
	      else
		{
		  /* Insert the new record to the current overflow page
		     and flush this page */

		  /* Store the record in current overflow page */
		  sp_success = spage_insert (thread_p, load_args->ovf.pgptr,
					     &load_args->out_recdes, &slotid);
		  if (sp_success != SP_SUCCESS)
		    {
		      goto error;
		    }

		  assert (slotid > 0);

		  /* Save the current overflow page */
		  btree_log_page (thread_p, &load_args->btid->sys_btid->vfid,
				  load_args->ovf.pgptr);
		  load_args->ovf.pgptr = NULL;

		  /* Turn off the overflowing mode */
		  load_args->overflowing = false;
		}		/* Current page is an overflow page */

	      /* Create the first part of the next record in main memory */
	      ret = btree_first_oid (thread_p, &this_key, &this_class_oid,
				     &this_oid, &mvcc_header, load_args);
	      if (ret != NO_ERROR)
		{
		  goto error;
		}

	      if (!MVCC_IS_HEADER_DELID_VALID (&mvcc_header))
		{
		  /* Object was not deleted, increment
		   * curr_non_del_obj_count.
		   */
		  load_args->curr_non_del_obj_count = 1;
		  (load_args->n_keys)++;	/* Increment the key counter */
		}
	      else
		{
		  /* Object is deleted, initialize curr_non_del_obj_count as
		   * 0.
		   */
		  load_args->curr_non_del_obj_count = 0;
		}

	      max_key_len =
		BTREE_GET_KEY_LEN_IN_PAGE (load_args->cur_key_len);
	      load_args->max_recsize = max_key_len + BTREE_MAX_OIDLEN_INPAGE;

#if !defined (NDEBUG)
	      btree_check_valid_record (NULL, load_args->btid,
					&load_args->out_recdes,
					BTREE_LEAF_NODE, NULL);
#endif
	    }			/* different key */
	}

	if (prm_get_bool_value (PRM_ID_LOG_BTREE_OPS))
	  {
	    _er_log_debug (ARG_FILE_LINE, "DEBUG_BTREE: load added "
			   "object(%d, %d, %d) "
			   "class(%d, %d, %d) and btid(%d, (%d, %d)) with "
			   "mvccinfo=%lld | %lld",
			   this_oid.volid, this_oid.pageid, this_oid.slotid,
			   this_class_oid.volid, this_class_oid.pageid,
			   this_class_oid.slotid,
			   load_args->btid->sys_btid->root_pageid,
			   load_args->btid->sys_btid->vfid.volid,
			   load_args->btid->sys_btid->vfid.fileid,
			   MVCC_GET_INSID (&mvcc_header),
			   MVCC_GET_DELID (&mvcc_header));
	  }

      /* Some objects have been recently deleted and couldn't be filtered
       * (because there may be running transaction that can still see them.
       * However, they must be vacuumed later. Vacuum can only find them by
       * parsing log, therefore some log records are required. These are dummy
       * records with the sole purpose of notifying vacuum.
       */
      if (MVCC_IS_HEADER_DELID_VALID (&mvcc_header)
	  || MVCC_IS_HEADER_INSID_NOT_ALL_VISIBLE (&mvcc_header))
	{
	  /* There is something to vacuum for this object */
	  char *pgptr;
	  char *data;
	  int length;

	  pgptr = (load_args->overflowing
		   ? load_args->ovf.pgptr : load_args->leaf.pgptr);

	  if (MVCC_IS_HEADER_DELID_VALID (&mvcc_header))
	    {
	      /* Vacuum must delete this object */
	      mvcc_args.purpose = MVCC_BTREE_INSERT_DELID;
	      mvcc_args.delete_mvccid = MVCC_GET_DELID (&mvcc_header);
	    }
	  else
	    {
	      /* Vacuum must remove insert MVCCID from this object */
	      mvcc_args.purpose = MVCC_BTREE_INSERT_OBJECT;
	      mvcc_args.insert_mvccid = MVCC_GET_INSID (&mvcc_header);
	    }

	  /* clear flags for logging */
	  btree_clear_mvcc_flags_from_oid (&this_oid);
	  btree_clear_mvcc_flags_from_oid (&this_class_oid);

	  if (prm_get_bool_value (PRM_ID_LOG_BTREE_OPS))
	    {
	      _er_log_debug (ARG_FILE_LINE, "DEBUG_BTREE: load notify "
			     "vacuum object(%d, %d, %d) "
			     "class(%d, %d, %d) and btid(%d, (%d, %d)) with "
			     "mvccinfo=%lld | %lld",
			     this_oid.volid, this_oid.pageid, this_oid.slotid,
			     this_class_oid.volid, this_class_oid.pageid,
			     this_class_oid.slotid,
			     load_args->btid->sys_btid->root_pageid,
			     load_args->btid->sys_btid->vfid.volid,
			     load_args->btid->sys_btid->vfid.fileid,
			     MVCC_GET_INSID (&mvcc_header),
			     MVCC_GET_DELID (&mvcc_header));
	    }

	  /* append log data */
	  ret =
	    btree_rv_save_keyval (load_args->btid, &this_key, &this_class_oid,
				  &this_oid, &mvcc_args, &data, &length);
	  if (ret != NO_ERROR)
	    {
	      goto error;
	    }
	  log_append_undo_data2 (thread_p, RVBT_MVCC_NOTIFY_VACUUM,
				 &load_args->btid->sys_btid->vfid, pgptr, -1,
				 length, data);

	  db_private_free (NULL, data);
	}

      /* set level 1 to leaf */
      load_args->leaf.hdr.node_level = 1;

      btree_clear_key_value (&copy, &this_key);

      if (next)
	{			/* move to next link */
	  recdes->data = next;
	  recdes->length = SORT_RECORD_LENGTH (next);
	}
      else
	{
	  break;		/* exit infinite loop */
	}

    }

  assert (ret == NO_ERROR);
  return ret;

error:
  if (load_args->leaf.pgptr)
    {
      pgbuf_unfix_and_init (thread_p, load_args->leaf.pgptr);
    }
  if (load_args->ovf.pgptr)
    {
      pgbuf_unfix_and_init (thread_p, load_args->ovf.pgptr);
    }
  if (new_ovfpgptr)
    {
      pgbuf_unfix_and_init (thread_p, new_ovfpgptr);
    }
  btree_clear_key_value (&copy, &this_key);

  assert (er_errid () != NO_ERROR);
  return (ret == NO_ERROR
	  && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

#if defined(CUBRID_DEBUG)
/*
 * btree_dump_sort_output () - Sample output function for index sorting
 *   return: NO_ERROR
 *   recdes(in):
 *   load_args(in):
 *
 * Note: This function is a debugging function. It is passed by the
 * btree_index_sort function to the sort_listfile function to print
 * out the contents of the sort items once they are obtained in
 * the requested sorting order.
 *
 */
static int
btree_dump_sort_output (const RECDES * recdes, LOAD_ARGS * load_args)
{
  OID this_oid;
  DB_VALUE this_key;
  OR_BUF buf;
  bool copy = false;
  int key_size = -1;
  int ret = NO_ERROR;

  /* First decompose the input record into the key and oid components */
  or_init (&buf, recdes->data, recdes->length);

  if (or_get_oid (&buf, &this_oid) != NO_ERROR)
    {
      goto exit_on_error;
    }
  buf.ptr = PTR_ALIGN (buf.ptr, MAX_ALIGNMENT);

  /* Do not copy the string--just use the pointer.  The pr_ routines
   * for strings and sets have different semantics for length.
   */
  if (TP_DOMAIN_TYPE (load_args->btid->key_type) == DB_TYPE_MIDXKEY)
    {
      key_size = buf.endptr - buf.ptr;
    }

  if ((*(load_args->btid->key_type->type->readval)) (&buf, &this_key,
						     load_args->
						     btid->key_type, key_size,
						     copy, NULL,
						     0) != NO_ERROR)
    {
      goto exit_on_error;
    }

  printf ("Attribute: ");
  btree_dump_key (&this_key);
  printf ("   Volid: %d", this_oid.volid);
  printf ("   Pageid: %d", this_oid.pageid);
  printf ("   Slotid: %d\n", this_oid.slotid);

end:

  copy = btree_clear_key_value (copy, &this_key);

  return ret;

exit_on_error:

  return (ret == NO_ERROR
	  && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}
#endif /* CUBRID_DEBUG */

/*
 * btree_index_sort () - Sort for the index file creation
 *   return: int
 *   sort_args(in): sort arguments; specifies the sort-attribute as well as
 *	            the structure of the input objects
 *   out_func(in): output function to utilize the sorted items as they are
 *                 produced
 *   out_args(in): arguments to the out_func.
 *
 * Note: This function supports the initial loading phase of B+tree
 * indices by providing an ordered list of (index-attribute
 * value, object address) pairs. It uses the general sorting
 * facility provided in the "sr" module.
 */
static int
btree_index_sort (THREAD_ENTRY * thread_p, SORT_ARGS * sort_args,
		  SORT_PUT_FUNC * out_func, void *out_args)
{
  return sort_listfile (thread_p, sort_args->hfids[0].vfid.volid,
			0 /* TODO - support parallelism */ ,
			&btree_sort_get_next, sort_args,
			out_func, out_args, compare_driver, sort_args,
			SORT_DUP, NO_SORT_LIMIT);
}

/*
 * btree_check_foreign_key () -
 *   return: NO_ERROR
 *   cls_oid(in):
 *   hfid(in):
 *   oid(in):
 *   keyval(in):
 *   n_attrs(in):
 *   pk_cls_oid(in):
 *   pk_btid(in):
 *   cache_attr_id(in):
 *   fk_name(in):
 */
int
btree_check_foreign_key (THREAD_ENTRY * thread_p, OID * cls_oid, HFID * hfid,
			 OID * oid, DB_VALUE * keyval, int n_attrs,
			 OID * pk_cls_oid, BTID * pk_btid, int cache_attr_id,
			 const char *fk_name)
{
  OID unique_oid;
  bool is_null;
  HEAP_CACHE_ATTRINFO attr_info;
  DB_VALUE val;
  int force_count;
  HEAP_SCANCACHE upd_scancache;
  int ret = NO_ERROR;

  /* TO DO - adapt this function to MVCC */
  DB_MAKE_NULL (&val);
  OID_SET_NULL (&unique_oid);

  if (n_attrs > 1)
    {
      is_null = btree_multicol_key_is_null (keyval);
    }
  else
    {
      is_null = DB_IS_NULL (keyval);
    }

  if (!is_null
      && xbtree_find_unique (thread_p, pk_btid, S_SELECT, keyval,
			     pk_cls_oid, &unique_oid,
			     true) != BTREE_KEY_FOUND)
    {
      char *val_print = NULL;

      val_print = pr_valstring (keyval);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FK_INVALID, 2, fk_name,
	      (val_print ? val_print : "unknown value"));
      if (val_print)
	{
	  free_and_init (val_print);
	}
      ret = ER_FK_INVALID;
      goto exit_on_error;
    }

  if (cache_attr_id >= 0)
    {
      ret =
	heap_scancache_start_modify (thread_p, &upd_scancache, hfid, cls_oid,
				     SINGLE_ROW_UPDATE, NULL);
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}
      ret = heap_attrinfo_start (thread_p, cls_oid, -1, NULL, &attr_info);
      if (ret != NO_ERROR)
	{
	  heap_scancache_end_modify (thread_p, &upd_scancache);
	  goto exit_on_error;
	}

      db_make_oid (&val, &unique_oid);
      ret = heap_attrinfo_clear_dbvalues (&attr_info);
      if (ret != NO_ERROR)
	{
	  heap_scancache_end_modify (thread_p, &upd_scancache);
	  goto exit_on_error;
	}
      ret = heap_attrinfo_set (oid, cache_attr_id, &val, &attr_info);
      if (ret != NO_ERROR)
	{
	  heap_attrinfo_end (thread_p, &attr_info);
	  heap_scancache_end_modify (thread_p, &upd_scancache);
	  goto exit_on_error;
	}

      /* since this function is called when the class is altered 
       * (SCH_M_LOCK on class) update in place is forced.
       */
      ret = locator_attribute_info_force (thread_p, hfid, oid, NULL, false,
					  &attr_info, &cache_attr_id, 1,
					  LC_FLUSH_UPDATE, SINGLE_ROW_UPDATE,
					  &upd_scancache, &force_count, true,
					  REPL_INFO_TYPE_STMT_NORMAL,
					  DB_NOT_PARTITIONED_CLASS, NULL,
					  NULL, NULL, true);
      if (ret != NO_ERROR)
	{
	  if (ret == ER_MVCC_NOT_SATISFIED_REEVALUATION)
	    {
	      ret = NO_ERROR;
	    }
	  else
	    {
	      heap_attrinfo_end (thread_p, &attr_info);
	      heap_scancache_end_modify (thread_p, &upd_scancache);
	      goto exit_on_error;
	    }
	}

      heap_attrinfo_end (thread_p, &attr_info);
      heap_scancache_end_modify (thread_p, &upd_scancache);
    }

  return ret;

exit_on_error:

  return (ret == NO_ERROR
	  && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

/*
 * btree_sort_get_next () - Get_key function for index sorting
 *   return: SORT_STATUS
 *   temp_recdes(in): temporary record descriptor; specifies where to put the
 *                    next sort item.
 *   arg(in): sort arguments; provides information about how to produce
 *            the next sort item.
 *
 * Note: This function is passed by the "btree_index_sort" function to
 * the "sort_listfile" function to obtain the value of the attribute
 * (on which the B+tree index for the class is to be created)
 * of each object successively.
 */
static SORT_STATUS
btree_sort_get_next (THREAD_ENTRY * thread_p, RECDES * temp_recdes, void *arg)
{
  SCAN_CODE scan_result;
  DB_VALUE dbvalue;
  DB_VALUE *dbvalue_ptr;
  int key_len;
  OID prev_oid;
  SORT_ARGS *sort_args;
  OR_BUF buf;
  int value_has_null;
  int next_size;
  int record_size;
  int oid_size;
  char midxkey_buf[DBVAL_BUFSIZE + MAX_ALIGNMENT], *aligned_midxkey_buf;
  int *prefix_lengthp;
  int result;
  MVCC_REC_HEADER mvcc_header;

  DB_MAKE_NULL (&dbvalue);

  aligned_midxkey_buf = PTR_ALIGN (midxkey_buf, MAX_ALIGNMENT);

  sort_args = (SORT_ARGS *) arg;
  prev_oid = sort_args->cur_oid;

  if (BTREE_IS_UNIQUE (sort_args->unique_pk))
    {
      oid_size = 2 * OR_OID_SIZE;
    }
  else
    {
      oid_size = OR_OID_SIZE;
    }

  do
    {				/* Infinite loop */
      int cur_class, attr_offset;

      /*
       * This infinite loop will be exited when a satisfactory next value is
       * found (i.e., when an object belonging to this class with a non-null
       * attribute value is found), or when there are no more objects in the
       * heap files.
       */

      /*
       * RETRIEVE THE NEXT OBJECT
       */

      cur_class = sort_args->cur_class;
      attr_offset = cur_class * sort_args->n_attrs;
      scan_result = heap_next (thread_p, &sort_args->hfids[cur_class],
			       &sort_args->class_ids[cur_class],
			       &sort_args->cur_oid, &sort_args->in_recdes,
			       &sort_args->hfscan_cache, PEEK);

      switch (scan_result)
	{

	case S_END:
	  /* No more objects in this heap, finish the current scan */
	  if (sort_args->attrinfo_inited)
	    {
	      heap_attrinfo_end (thread_p, &sort_args->attr_info);
	      if (sort_args->filter)
		{
		  heap_attrinfo_end (thread_p, sort_args->filter->cache_pred);
		}
	      if (sort_args->func_index_info &&
		  sort_args->func_index_info->expr)
		{
		  heap_attrinfo_end (thread_p,
				     ((FUNC_PRED *) sort_args->
				      func_index_info->expr)->cache_attrinfo);
		}
	    }
	  sort_args->attrinfo_inited = 0;
	  if (sort_args->scancache_inited)
	    {
	      (void) heap_scancache_end (thread_p, &sort_args->hfscan_cache);
	    }
	  sort_args->scancache_inited = 0;

	  /* Are we through with all the non-null heaps? */
	  sort_args->cur_class++;
	  while ((sort_args->cur_class < sort_args->n_classes)
		 && HFID_IS_NULL (&sort_args->hfids[sort_args->cur_class]))
	    {
	      sort_args->cur_class++;
	    }

	  if (sort_args->cur_class == sort_args->n_classes)
	    {
	      return SORT_NOMORE_RECS;
	    }
	  else
	    {
	      /* start up the next scan */
	      cur_class = sort_args->cur_class;
	      attr_offset = cur_class * sort_args->n_attrs;

	      if (heap_scancache_start (thread_p, &sort_args->hfscan_cache,
					&sort_args->hfids[cur_class],
					&sort_args->class_ids[cur_class],
					true, false, NULL) != NO_ERROR)
		{
		  return SORT_ERROR_OCCURRED;
		}
	      sort_args->scancache_inited = 1;

	      if (heap_attrinfo_start (thread_p,
				       &sort_args->class_ids[cur_class],
				       sort_args->n_attrs,
				       &sort_args->attr_ids[attr_offset],
				       &sort_args->attr_info) != NO_ERROR)
		{
		  return SORT_ERROR_OCCURRED;
		}
	      sort_args->attrinfo_inited = 1;

	      /* set the scan to the initial state for this new heap */
	      OID_SET_NULL (&sort_args->cur_oid);

	      if (prm_get_bool_value (PRM_ID_LOG_BTREE_OPS))
		{
		  _er_log_debug (ARG_FILE_LINE, "DEBUG_BTREE: load start on "
				 "class(%d, %d, %d), btid(%d, (%d, %d)).",
				 sort_args->class_ids[sort_args->cur_class].
				 volid,
				 sort_args->class_ids[sort_args->cur_class].
				 pageid,
				 sort_args->class_ids[sort_args->cur_class].
				 slotid,
				 sort_args->btid->sys_btid->root_pageid,
				 sort_args->btid->sys_btid->vfid.volid,
				 sort_args->btid->sys_btid->vfid.fileid);
		}
	    }
	  continue;

	case S_ERROR:
	case S_DOESNT_EXIST:
	case S_DOESNT_FIT:
	case S_SUCCESS_CHN_UPTODATE:
	case S_SNAPSHOT_NOT_SATISFIED:
	  return SORT_ERROR_OCCURRED;

	case S_SUCCESS:
	  break;
	}

      /*
       * Produce the sort item for this object
       */

      /* filter out dead records before any more checks */
      if (or_mvcc_get_header (&sort_args->in_recdes, &mvcc_header) !=
	  NO_ERROR)
	{
	  return SORT_ERROR_OCCURRED;
	}
      if (MVCC_IS_HEADER_DELID_VALID (&mvcc_header)
	  && MVCC_GET_DELID (&mvcc_header) < sort_args->lowest_active_mvccid)
	{
	  continue;
	}

      if (sort_args->filter)
	{
	  if (heap_attrinfo_read_dbvalues (thread_p, &sort_args->cur_oid,
					   &sort_args->in_recdes, NULL,
					   sort_args->filter->cache_pred)
	      != NO_ERROR)
	    {
	      return SORT_ERROR_OCCURRED;
	    }

	  result = (*sort_args->filter_eval_func) (thread_p,
						   sort_args->filter->pred,
						   NULL, &sort_args->cur_oid);
	  if (result == V_ERROR)
	    {
	      return SORT_ERROR_OCCURRED;
	    }
	  else if (result != V_TRUE)
	    {
	      continue;
	    }
	}

      if (sort_args->func_index_info && sort_args->func_index_info->expr)
	{
	  if (heap_attrinfo_read_dbvalues (thread_p, &sort_args->cur_oid,
					   &sort_args->in_recdes, NULL,
					   ((FUNC_PRED *) sort_args->
					    func_index_info->expr)->
					   cache_attrinfo) != NO_ERROR)
	    {
	      return SORT_ERROR_OCCURRED;
	    }
	}

      if (sort_args->n_attrs == 1)
	{			/* single-column index */
	  if (heap_attrinfo_read_dbvalues (thread_p, &sort_args->cur_oid,
					   &sort_args->in_recdes, NULL,
					   &sort_args->attr_info) != NO_ERROR)
	    {
	      return SORT_ERROR_OCCURRED;
	    }
	}

      prefix_lengthp = NULL;
      if (sort_args->attrs_prefix_length)
	{
	  prefix_lengthp = &(sort_args->attrs_prefix_length[0]);
	}

      dbvalue_ptr =
	heap_attrinfo_generate_key (thread_p, sort_args->n_attrs,
				    &sort_args->attr_ids[attr_offset],
				    prefix_lengthp,
				    &sort_args->attr_info,
				    &sort_args->in_recdes, &dbvalue,
				    aligned_midxkey_buf,
				    sort_args->func_index_info);
      if (dbvalue_ptr == NULL)
	{
	  return SORT_ERROR_OCCURRED;
	}

      if (sort_args->fk_refcls_oid && !OID_ISNULL (sort_args->fk_refcls_oid))
	{
	  if (btree_check_foreign_key (thread_p,
				       &sort_args->class_ids[cur_class],
				       &sort_args->hfids[cur_class],
				       &sort_args->cur_oid, dbvalue_ptr,
				       sort_args->n_attrs,
				       sort_args->fk_refcls_oid,
				       sort_args->fk_refcls_pk_btid,
				       sort_args->cache_attr_id,
				       sort_args->fk_name) != NO_ERROR)
	    {
	      if (dbvalue_ptr == &dbvalue)
		{
		  pr_clear_value (&dbvalue);
		}
	      return SORT_ERROR_OCCURRED;
	    }
	}

      value_has_null = 0;	/* init */
      if (DB_IS_NULL (dbvalue_ptr)
	  || btree_multicol_key_has_null (dbvalue_ptr))
	{
	  value_has_null = 1;	/* found null columns */
	}

      if (sort_args->not_null_flag && value_has_null)
	{
	  if (dbvalue_ptr == &dbvalue)
	    {
	      pr_clear_value (&dbvalue);
	    }

	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_NOT_NULL_DOES_NOT_ALLOW_NULL_VALUE, 0);
	  return SORT_ERROR_OCCURRED;
	}

      if (DB_IS_NULL (dbvalue_ptr)
	  || btree_multicol_key_is_null (dbvalue_ptr))
	{
	  if (!MVCC_IS_HEADER_DELID_VALID (&mvcc_header))
	    {
	      /* All objects that were not candidates for vacuum are loaded,
	       * but statistics should only care for objects that have not
	       * been deleted and committed at the time of load.
	       */
	      sort_args->n_oids++;	/* Increment the OID counter */
	      sort_args->n_nulls++;	/* Increment the NULL counter */
	    }
	  if (dbvalue_ptr == &dbvalue)
	    {
	      pr_clear_value (&dbvalue);
	    }
	  if (prm_get_bool_value (PRM_ID_LOG_BTREE_OPS))
	    {
	      _er_log_debug (ARG_FILE_LINE,
			     "DEBUG_BTREE: load sort found null at "
			     "oid(%d, %d, %d)"
			     ", class_oid(%d, %d, %d), btid(%d, (%d, %d).",
			     sort_args->cur_oid.volid,
			     sort_args->cur_oid.pageid,
			     sort_args->cur_oid.slotid,
			     sort_args->class_ids[sort_args->cur_class].volid,
			     sort_args->class_ids[sort_args->cur_class].
			     pageid,
			     sort_args->class_ids[sort_args->cur_class].
			     slotid,
			     sort_args->btid->sys_btid->root_pageid,
			     sort_args->btid->sys_btid->vfid.volid,
			     sort_args->btid->sys_btid->vfid.fileid);
	    }
	  continue;
	}

      key_len = pr_data_writeval_disk_size (dbvalue_ptr);

      if (key_len > 0)
	{
	  next_size = sizeof (char *);
	  record_size = (next_size	/* Pointer to next */
			 + OR_INT_SIZE	/* Has null */
			 + oid_size	/* OID, Class OID */
			 + 2 * OR_MVCCID_SIZE	/* Insert and delete MVCCID */
			 + key_len	/* Key length */
			 + (int) MAX_ALIGNMENT /* Alignment */ );

	  if (temp_recdes->area_size < record_size)
	    {
	      /*
	       * Record is too big to fit into temp_recdes area; so
	       * backtrack this iteration
	       */
	      sort_args->cur_oid = prev_oid;
	      temp_recdes->length = record_size;
	      goto nofit;
	    }

	  assert (PTR_ALIGN (temp_recdes->data, MAX_ALIGNMENT)
		  == temp_recdes->data);
	  or_init (&buf, temp_recdes->data, 0);

	  or_pad (&buf, next_size);	/* init as NULL */

	  /* save has_null */
	  if (or_put_byte (&buf, value_has_null) != NO_ERROR)
	    {
	      goto nofit;
	    }

	  or_advance (&buf, (OR_INT_SIZE - OR_BYTE_SIZE));
	  assert (buf.ptr == PTR_ALIGN (buf.ptr, INT_ALIGNMENT));

	  if (BTREE_IS_UNIQUE (sort_args->unique_pk))
	    {
	      if (or_put_oid (&buf, &sort_args->class_ids[cur_class]) !=
		  NO_ERROR)
		{
		  goto nofit;
		}
	    }

	  if (or_put_oid (&buf, &sort_args->cur_oid) != NO_ERROR)
	    {
	      goto nofit;
	    }

	  /* Pack insert and delete MVCCID's */
	  if (MVCC_IS_HEADER_INSID_NOT_ALL_VISIBLE (&mvcc_header))
	    {
	      if (or_put_mvccid (&buf, MVCC_GET_INSID (&mvcc_header))
		  != NO_ERROR)
		{
		  goto nofit;
		}
	    }
	  else
	    {
	      if (or_put_mvccid (&buf, MVCCID_ALL_VISIBLE) != NO_ERROR)
		{
		  goto nofit;
		}
	    }

	  if (MVCC_IS_HEADER_DELID_VALID (&mvcc_header))
	    {
	      if (or_put_mvccid (&buf, MVCC_GET_DELID (&mvcc_header))
		  != NO_ERROR)
		{
		  goto nofit;
		}
	    }
	  else
	    {
	      if (or_put_mvccid (&buf, MVCCID_NULL) != NO_ERROR)
		{
		  goto nofit;
		}
	    }

	  if (prm_get_bool_value (PRM_ID_LOG_BTREE_OPS))
	    {
	      _er_log_debug (ARG_FILE_LINE,
			     "DEBUG_BTREE: load sort found oid(%d, %d, %d)"
			     ", class_oid(%d, %d, %d), btid(%d, (%d, %d), "
			     "mvcc_info=%lld | %lld.",
			     sort_args->cur_oid.volid,
			     sort_args->cur_oid.pageid,
			     sort_args->cur_oid.slotid,
			     sort_args->class_ids[sort_args->cur_class].volid,
			     sort_args->class_ids[sort_args->cur_class].
			     pageid,
			     sort_args->class_ids[sort_args->cur_class].
			     slotid,
			     sort_args->btid->sys_btid->root_pageid,
			     sort_args->btid->sys_btid->vfid.volid,
			     sort_args->btid->sys_btid->vfid.fileid,
			     MVCC_GET_INSID (&mvcc_header),
			     MVCC_GET_DELID (&mvcc_header));
	    }

	  assert (buf.ptr == PTR_ALIGN (buf.ptr, INT_ALIGNMENT));

	  if ((*(sort_args->key_type->type->data_writeval)) (&buf,
							     dbvalue_ptr)
	      != NO_ERROR)
	    {
	      goto nofit;
	    }

	  temp_recdes->length = CAST_STRLEN (buf.ptr - buf.buffer);

	  if (dbvalue_ptr == &dbvalue)
	    {
	      pr_clear_value (&dbvalue);
	    }
	}

      if (!MVCC_IS_HEADER_DELID_VALID (&mvcc_header))
	{
	  /* All objects that were not candidates for vacuum are loaded,
	   * but statistics should only care for objects that have not
	   * been deleted and committed at the time of load.
	   */
	  sort_args->n_oids++;	/* Increment the OID counter */
	}

      if (key_len > 0)
	{
	  return SORT_SUCCESS;
	}

    }
  while (true);

nofit:

  if (dbvalue_ptr == &dbvalue)
    {
      pr_clear_value (&dbvalue);
    }

  return SORT_REC_DOESNT_FIT;
}

/*
 * compare_driver () -
 *   return:
 *   first(in):
 *   second(in):
 *   arg(in):
 */
static int
compare_driver (const void *first, const void *second, void *arg)
{
  char *mem1 = *(char **) first;
  char *mem2 = *(char **) second;
  int has_null;
  SORT_ARGS *sort_args;
  TP_DOMAIN *key_type;
  int c = DB_UNK;

  sort_args = (SORT_ARGS *) arg;
  key_type = sort_args->key_type;

  assert (PTR_ALIGN (mem1, MAX_ALIGNMENT) == mem1);
  assert (PTR_ALIGN (mem2, MAX_ALIGNMENT) == mem2);

  /* Skip next link */
  mem1 += sizeof (char *);
  mem2 += sizeof (char *);

  /* Read value_has_null */
  assert (OR_GET_BYTE (mem1) == 0 || OR_GET_BYTE (mem1) == 1);
  assert (OR_GET_BYTE (mem2) == 0 || OR_GET_BYTE (mem2) == 1);
  has_null = (OR_GET_BYTE (mem1) || OR_GET_BYTE (mem2)) ? 1 : 0;

  mem1 += OR_INT_SIZE;
  mem2 += OR_INT_SIZE;

  assert (PTR_ALIGN (mem1, INT_ALIGNMENT) == mem1);
  assert (PTR_ALIGN (mem2, INT_ALIGNMENT) == mem2);

  /* Skip the oids */
  if (BTREE_IS_UNIQUE (sort_args->unique_pk))
    {				/* unique index */
      mem1 += (2 * OR_OID_SIZE);
      mem2 += (2 * OR_OID_SIZE);
    }
  else
    {				/* non-unique index */
      mem1 += OR_OID_SIZE;
      mem2 += OR_OID_SIZE;
    }

  assert (PTR_ALIGN (mem1, INT_ALIGNMENT) == mem1);
  assert (PTR_ALIGN (mem2, INT_ALIGNMENT) == mem2);

  /* Skip the MVCCID's */
  mem1 += 2 * OR_MVCCID_SIZE;
  mem2 += 2 * OR_MVCCID_SIZE;

  assert (PTR_ALIGN (mem1, INT_ALIGNMENT) == mem1);
  assert (PTR_ALIGN (mem2, INT_ALIGNMENT) == mem2);

  if (TP_DOMAIN_TYPE (key_type) == DB_TYPE_MIDXKEY)
    {
      int i;
      char *bitptr1, *bitptr2;
      int bitmap_size;
      TP_DOMAIN *dom;

      /* fast implementation of pr_midxkey_compare ().
       * do not use DB_VALUE container for speed-up
       */

      bitptr1 = mem1;
      bitptr2 = mem2;

      bitmap_size = OR_MULTI_BOUND_BIT_BYTES (key_type->precision);

      mem1 += bitmap_size;
      mem2 += bitmap_size;

#if !defined(NDEBUG)
      for (i = 0, dom = key_type->setdomain; dom; dom = dom->next, i++);
      assert (i == key_type->precision);
#endif

      if (sort_args->func_index_info != NULL)
	{
	  assert (sort_args->n_attrs <= key_type->precision);
	}
      else
	{
	  assert (sort_args->n_attrs == key_type->precision);
	}
      assert (key_type->setdomain != NULL);

      for (i = 0, dom = key_type->setdomain;
	   i < key_type->precision && dom; i++, dom = dom->next)
	{
	  /* val1 or val2 is NULL */
	  if (has_null)
	    {
	      if (OR_MULTI_ATT_IS_UNBOUND (bitptr1, i))
		{		/* element val is null? */
		  if (OR_MULTI_ATT_IS_UNBOUND (bitptr2, i))
		    {
		      continue;
		    }

		  c = DB_LT;
		  break;	/* exit for-loop */
		}
	      else if (OR_MULTI_ATT_IS_UNBOUND (bitptr2, i))
		{
		  c = DB_GT;
		  break;	/* exit for-loop */
		}
	    }

	  /* check for val1 and val2 same domain */
	  c = (*(dom->type->index_cmpdisk)) (mem1, mem2, dom, 0, 1, NULL);
	  assert (c == DB_LT || c == DB_EQ || c == DB_GT);

	  if (c != DB_EQ)
	    {
	      break;		/* exit for-loop */
	    }

	  mem1 += pr_midxkey_element_disk_size (mem1, dom);
	  mem2 += pr_midxkey_element_disk_size (mem2, dom);
	}			/* for (i = 0; ... ) */
      assert (c == DB_LT || c == DB_EQ || c == DB_GT);

      if (dom && dom->is_desc)
	{
	  c = ((c == DB_GT) ? DB_LT : (c == DB_LT) ? DB_GT : c);
	}
    }
  else
    {
      OR_BUF buf_val1, buf_val2;
      DB_VALUE val1, val2;

      OR_BUF_INIT (buf_val1, mem1, -1);
      OR_BUF_INIT (buf_val2, mem2, -1);

      if ((*(key_type->type->data_readval))
	  (&buf_val1, &val1, key_type, -1, false, NULL, 0) != NO_ERROR)
	{
	  assert (false);
	  return DB_UNK;
	}

      if ((*(key_type->type->data_readval))
	  (&buf_val2, &val2, key_type, -1, false, NULL, 0) != NO_ERROR)
	{
	  assert (false);
	  return DB_UNK;
	}

      c = btree_compare_key (&val1, &val2, key_type, 0, 1, NULL);
    }

  assert (c == DB_LT || c == DB_EQ || c == DB_GT);

  /* compare OID for non-unique index 
   */
  if (c == DB_EQ)
    {
      OID first_oid, second_oid;

      mem1 = *(char **) first;
      mem2 = *(char **) second;

      /* Skip next link */
      mem1 += sizeof (char *);
      mem2 += sizeof (char *);

      /* Skip value_has_null */
      mem1 += OR_INT_SIZE;
      mem2 += OR_INT_SIZE;

      if (BTREE_IS_UNIQUE (sort_args->unique_pk))
	{
	  /* Skip class OID */
	  mem1 += OR_OID_SIZE;
	  mem2 += OR_OID_SIZE;
	}

      OR_GET_OID (mem1, &first_oid);
      OR_GET_OID (mem2, &second_oid);

      assert_release (!OID_EQ (&first_oid, &second_oid));

      if (OID_LT (&first_oid, &second_oid))
	{
	  c = DB_LT;
	}
      else
	{
	  c = DB_GT;
	}
    }

  return c;
}

/*
 * Linked list implementation
 */

/*
 * add_list () -
 *   return: NO_ERROR
 *   list(in): which list to add
 *   pageid(in): what value to put to the new node
 *
 * Note: This function adds a new node to the end of the given list.
 */
static int
add_list (BTREE_NODE ** list, VPID * pageid)
{
  BTREE_NODE *new_node;
  BTREE_NODE *next_node;
  int ret = NO_ERROR;

  new_node = (BTREE_NODE *) os_malloc (sizeof (BTREE_NODE));
  if (new_node == NULL)
    {
      goto exit_on_error;
    }

  new_node->pageid = *pageid;
  new_node->next = NULL;

  if (*list == NULL)
    {
      *list = new_node;
    }
  else
    {
      next_node = *list;
      while (next_node->next != NULL)
	{
	  next_node = next_node->next;
	}

      next_node->next = new_node;
    }

  return ret;

exit_on_error:

  return (ret == NO_ERROR
	  && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

/*
 * remove_first () -
 *   return: nothing
 *   list(in):
 *
 * Note: This function removes the first node of the given list (if it has one).
 */
static void
remove_first (BTREE_NODE ** list)
{
  BTREE_NODE *temp;

  if (*list != NULL)
    {
      temp = *list;
      *list = (*list)->next;
      os_free_and_init (temp);
    }
}

/*
 * length_list () -
 *   return: int
 *   this_list(in): which list
 *
 * Note: This function returns the number of elements kept in the
 * given linked list.
 */
static int
length_list (const BTREE_NODE * this_list)
{
  int length = 0;

  while (this_list != NULL)
    {
      length++;
      this_list = this_list->next;
    }

  return length;
}

#if defined(CUBRID_DEBUG)
/*
 * print_list () -
 *   return: this_list
 *   this_list(in): which list to print
 *
 * Note: This function prints the elements of the given linked list;
 * It is used for debugging purposes.
 */
static void
print_list (const BTREE_NODE * this_list)
{
  while (this_list != NULL)
    {
      (void) printf ("{%d, %d}n", this_list->pageid.volid,
		     this_list->pageid.pageid);
      this_list = this_list->next;
    }
}


/*
 * btree_load_foo_debug () -
 *   return:
 *
 * Note: To avoid warning during development
 */
void
btree_load_foo_debug (void)
{
  (void) btree_dump_sort_output (NULL, NULL);
  print_list (NULL);
}
#endif /* CUBRID_DEBUG */

/*
 * btree_rv_undo_create_index () - Undo the creation of an index file
 *   return: int
 *   rcv(in): Recovery structure
 *
 * Note: The index file is destroyed completely.
 */
int
btree_rv_undo_create_index (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  VFID *vfid;
  int ret;

  vfid = (VFID *) rcv->data;
  ret = file_destroy (thread_p, vfid);

  assert (ret == NO_ERROR);

  return ((ret == NO_ERROR) ? NO_ERROR : er_errid ());
}

/*
 * btree_rv_dump_create_index () -
 *   return: int
 *   length_ignore(in): Length of Recovery Data
 *   data(in): The data being logged
 *
 * Note: Dump the information to undo the creation of an index file
 */
void
btree_rv_dump_create_index (FILE * fp, int length_ignore, void *data)
{
  VFID *vfid;

  vfid = (VFID *) data;
  (void) fprintf (fp, "Undo creation of Index vfid: %d|%d\n",
		  vfid->volid, vfid->fileid);
}

/*
 * btree_rv_nodehdr_dump () - Dump node header recovery information
 *   return: int
 *   length(in): Length of Recovery Data
 *   data(in): The data being logged
 *
 * Note: Dump node header recovery information
 */
void
btree_rv_nodehdr_dump (FILE * fp, int length, void *data)
{
  BTREE_NODE_HEADER *header = NULL;

  header = (BTREE_NODE_HEADER *) data;
  assert (header != NULL);

  fprintf (fp,
	   "\nNODE_TYPE: %s MAX_KEY_LEN: %4d "
	   "PREV_PAGEID: {%4d , %4d} NEXT_PAGEID: {%4d , %4d} \n\n",
	   header->node_level > 1 ? "NON_LEAF" : "LEAF",
	   header->max_key_len,
	   header->prev_vpid.volid, header->prev_vpid.pageid,
	   header->next_vpid.volid, header->next_vpid.pageid);
}

/*
 * btree_node_number_of_keys () -
 *   return: int
 *
 */
int
btree_node_number_of_keys (PAGE_PTR page_ptr)
{
  int key_cnt;

  assert (page_ptr != NULL);
#if !defined(NDEBUG)
  (void) pgbuf_check_page_ptype (NULL, page_ptr, PAGE_BTREE);
#endif

  key_cnt = spage_number_of_records (page_ptr) - 1;

#if !defined(NDEBUG)
  {
    BTREE_NODE_HEADER *header = NULL;
    BTREE_NODE_TYPE node_type;

    header = btree_get_node_header (page_ptr);
    if (header == NULL)
      {
	assert (false);
      }

    node_type =
      (header->node_level > 1) ? BTREE_NON_LEAF_NODE : BTREE_LEAF_NODE;

    if ((node_type == BTREE_NON_LEAF_NODE && key_cnt <= 0)
	|| (node_type == BTREE_LEAF_NODE && key_cnt < 0))
      {
	er_log_debug (ARG_FILE_LINE, "btree_node_number_of_keys: "
		      "node key count underflow: %d\n", key_cnt);
	assert (false);
      }
  }
#endif

  assert_release (key_cnt >= 0);

  return key_cnt;
}
