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
  int unique_flag;
  int reverse_flag;
  HFID *hfids;			/* Array of HFIDs for the class(es) */
  OID *class_ids;		/* Array of class OIDs              */
  OID cur_oid;			/* Identifier of the current object */
  RECDES in_recdes;		/* Input record descriptor          */
  int n_attrs;			/* Number of attribute ID's         */
  ATTR_ID *attr_ids;		/* Specification of the attribute(s) to
				 * sort on */
  TP_DOMAIN *key_type;
  HEAP_SCANCACHE hfscan_cache;	/* A heap scan cache                */
  HEAP_CACHE_ATTRINFO attr_info;	/* Attribute information            */
  int n_nulls;			/* Number of NULLs */
  int n_oids;			/* Number of OIDs */
  int n_classes;		/* cardinality of the hfids, the class_ids,
				 * and (with n_attrs) the attr_ids arrays
				 */
  int cur_class;		/* index into the hfids, class_ids, and
				 * attr_ids arrarys
				 */
  int scancache_inited;
  int attrinfo_inited;

  BTID_INT *btid;

  OID *fk_refcls_oid;
  BTID *fk_refcls_pk_btid;
  int cache_attr_id;
  const char *fk_name;
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
  int init_pgcnt;		/* Initially allocated page count for index */
  int used_pgcnt;		/* Used page count for the index file */
  int nodup_pgcnt;		/* No duplicate page count estimation for index */
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
  int n_keys;			/* Number of keys */
};

static bool btree_save_last_leafrec (THREAD_ENTRY * thread_p,
				     LOAD_ARGS * load_args);
static PAGE_PTR btree_connect_page (THREAD_ENTRY * thread_p, DB_VALUE * key,
				    int max_key_len, VPID * pageid,
				    LOAD_ARGS * load_args);
static int btree_build_nleafs (THREAD_ENTRY * thread_p, LOAD_ARGS * load_args,
			       int n_nulls, int n_oids, int n_keys,
			       int unique_flag, int reverse_flag);

static void btree_log_page (THREAD_ENTRY * thread_p, VFID * vfid,
			    PAGE_PTR page_ptr);
static PAGE_PTR btree_get_page (THREAD_ENTRY * thread_p, BTID * btid,
				VPID * page_id, VPID * nearpg,
				BTREE_NODE_HEADER * header, short node_type,
				int *init_pgcnt, int *used_pgcnt,
				int nodup_pgcnt);
static PAGE_PTR btree_proceed_leaf (THREAD_ENTRY * thread_p,
				    LOAD_ARGS * load_args);
static int btree_first_oid (THREAD_ENTRY * thread_p, DB_VALUE * this_key,
			    OID * class_oid, OID * first_oid,
			    LOAD_ARGS * load_args);
static int btree_construct_leafs (THREAD_ENTRY * thread_p,
				  const RECDES * in_recdes, void *arg);
#if defined(CUBRID_DEBUG)
static int btree_dump_sort_output (const RECDES * recdes,
				   LOAD_ARGS * load_args);
#endif /* defined(CUBRID_DEBUG) */
static int btree_index_sort (THREAD_ENTRY * thread_p, SORT_ARGS * sort_args,
			     int est_inp_pg_cnt, SORT_PUT_FUNC * out_func,
			     void *out_args);
static SORT_STATUS btree_sort_get_next (THREAD_ENTRY * thread_p,
					RECDES * temp_recdes, void *arg);
static int compare_driver (const void *first, const void *second, void *arg);
static int add_list (BTREE_NODE ** list, VPID * pageid);
static void remove_first (BTREE_NODE ** list);
static int length_list (const BTREE_NODE * this_list);
#if defined(CUBRID_DEBUG)
static void print_list (const BTREE_NODE * this_list);
#endif /* defined(CUBRID_DEBUG) */

/*
 * xbtree_load_index () - create & load b+tree index
 *   return: BTID * (btid on success and NULL on failure)
 *           btid is set as a side effect.
 *   btid(out):
 *      btid: Set to the created B+tree index identifier
 *            (Note: btid->vfid.volid should be set by the caller)
 *   key_type(in): Key type corresponding to the attribute.
 *   class_oids(in): OID of the class for which the index will be created
 *   n_classes(in):
 *   n_attrs(in):
 *   attr_ids(in): Identifier of the attribute of the class for which the index
 *                 will be created.
 *   hfids(in): Identifier of the heap file containing the instances of the
 *	        class
 *   unique_flag(in):
 *   reverse_flag(in):
 *   fk_refcls_oid(in):
 *   fk_refcls_pk_btid(in):
 *   cache_attr_id(in):
 *   fk_name(in):
 *
 */
BTID *
xbtree_load_index (THREAD_ENTRY * thread_p, BTID * btid, TP_DOMAIN * key_type,
		   OID * class_oids, int n_classes, int n_attrs,
		   int *attr_ids, HFID * hfids, int unique_flag,
		   int reverse_flag, OID * fk_refcls_oid,
		   BTID * fk_refcls_pk_btid, int cache_attr_id,
		   const char *fk_name)
{
  SORT_ARGS sort_args_info, *sort_args;
  LOAD_ARGS load_args_info, *load_args;
  int est_obj_cnt, key_len, init_pgcnt, used_pgcnt, i, k, est1, est2;
  int num_sort_pages, file_created = 0, cur_class, attr_offset;
  VPID vpid;
  INT16 save_volid;
  FILE_BTREE_DES btdes;
  DB_VALUE key_copy;
  BTID_INT btid_int;
  LOG_DATA_ADDR addr;
  char midxkeybuf[DBVAL_BUFSIZE + MAX_ALIGNMENT], *aligned_midxkey_buf;

  aligned_midxkey_buf = PTR_ALIGN (midxkeybuf, MAX_ALIGNMENT);

  /* Check for robustness */
  if (!btid || !hfids || !class_oids || !attr_ids)
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

  btid_int.sys_btid = btid;
  btid_int.unique = unique_flag;
  btid_int.reverse = reverse_flag;
  btid_int.key_type = key_type;
  VFID_SET_NULL (&btid_int.ovfid);
  btid_int.rev_level = BTREE_CURRENT_REV_LEVEL;

  btid_int.nonleaf_key_type = btree_generate_prefix_domain (&btid_int);
  db_make_null (&key_copy);

  /* create a file descriptor */
  COPY_OID (&btdes.class_oid, &class_oids[0]);
  /* This needs to change to list all the attr_ids for multi-column */
  btdes.attr_id = attr_ids[0];
  save_volid = btid->vfid.volid;

  /* Initialize the fields of sorting argument structures */
  sort_args->unique_flag = unique_flag;
  sort_args->reverse_flag = reverse_flag;
  sort_args->hfids = hfids;
  sort_args->class_ids = class_oids;
  sort_args->attr_ids = attr_ids;
  sort_args->n_attrs = n_attrs;
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

  /*
   * Start a heap scancache for reading objects using the first nun-null heap
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
  if (heap_scancache_start (thread_p, &sort_args->hfscan_cache,
			    &sort_args->hfids[cur_class],
			    &sort_args->class_ids[cur_class],
			    true, false, LOCKHINT_NONE) != NO_ERROR)
    {
      goto error;
    }
  sort_args->scancache_inited = 1;

  if (heap_attrinfo_start (thread_p, &sort_args->class_ids[cur_class],
			   sort_args->n_attrs,
			   &sort_args->attr_ids[attr_offset],
			   &sort_args->attr_info) != NO_ERROR)
    {
      goto error;
    }
  sort_args->attrinfo_inited = 1;

  /* get an estimate of number of index pages to be created
   * assume that all the objects have distinct keys.
   */
  for (i = 0, est_obj_cnt = 0; i < sort_args->n_classes; i++)
    {
      int obj_cnt;
      if (!HFID_IS_NULL (&hfids[i]))
	{
	  obj_cnt = heap_estimate_num_objects (thread_p, &hfids[i]);
	  if (obj_cnt == -1)
	    {
	      goto error;
	    }
	  est_obj_cnt += obj_cnt;
	}
    }

  key_len = tp_domain_disk_size (key_type);

  /* set the key length information for index page estimations */
  if (key_len == -1)
    {
      SCAN_CODE hf_scan;	/* Heap file scan result               */
      OID prev_oid;		/* Previous object identifier          */
      DB_VALUE *dbvalue_ptr;	/* Pointer to Key dbvalue              */
      DB_VALUE dbvalue;		/* Key dbvalue                         */
      int key_cnt;		/* Number of keys considered           */
      int sum_key_len;		/* Total key length up to now          */

      /* for variable size key types, instead of using a constant value
       * get the first heap file page and get the average key
       * length from the first heap file page objects. This makes
       * an extra page access, but it may prevent allocating many
       * more pages for the index than is necessary. Moreover, the
       * first heap file page will very soon be accessed anyway.
       * It will then already be in the page buffer pool.
       */

#define BTREE_AVG_STRING_KEY_LEN 25

      key_len = BTREE_AVG_STRING_KEY_LEN;
      key_cnt = 0;
      sum_key_len = 0;
      OID_SET_NULL (&prev_oid);
      sort_args->in_recdes.area_size = -1;
      hf_scan = heap_first (thread_p, &sort_args->hfids[cur_class],
			    &sort_args->class_ids[cur_class],
			    &sort_args->cur_oid, &sort_args->in_recdes,
			    &sort_args->hfscan_cache, PEEK);
      while (hf_scan == S_SUCCESS
	     && (OID_ISNULL (&prev_oid)
		 || sort_args->cur_oid.pageid == prev_oid.pageid))
	{
	  /* still in first page */
	  if (sort_args->n_attrs == 1)
	    {			/* single-column index */
	      if (heap_attrinfo_read_dbvalues (thread_p, &sort_args->cur_oid,
					       &sort_args->in_recdes,
					       &sort_args->attr_info) !=
		  NO_ERROR)
		{
		  hf_scan = S_ERROR;
		  continue;
		}
	    }

	  dbvalue_ptr =
	    heap_attrinfo_generate_key (thread_p, sort_args->n_attrs,
					&sort_args->attr_ids[attr_offset],
					&sort_args->attr_info,
					&sort_args->in_recdes, &dbvalue,
					aligned_midxkey_buf);
	  if (dbvalue_ptr == NULL || db_value_is_null (dbvalue_ptr))
	    {
	      hf_scan = S_ERROR;
	      continue;
	    }

	  key_cnt++;
	  key_len = btree_get_key_length (dbvalue_ptr);
	  sum_key_len += key_len;

	  COPY_OID (&prev_oid, &sort_args->cur_oid);
	  hf_scan =
	    heap_next (thread_p, &sort_args->hfids[0],
		       &sort_args->class_ids[0], &sort_args->cur_oid,
		       &sort_args->in_recdes, &sort_args->hfscan_cache, PEEK);

	  if (dbvalue_ptr == &dbvalue)
	    {
	      pr_clear_value (&dbvalue);
	    }
	}			/* while */

      /* Note: If there is an error, continue, since this is only for
       * estimation purposes.
       */

      if (key_cnt > 0 && sum_key_len > 0)
	{
	  key_len = (int) (sum_key_len / key_cnt);
	}

      /*
       * Set the cyrrent oid for scan cache purpose back to NULL, so we can
       * start the sort from the first heap object
       */
      OID_SET_NULL (&sort_args->cur_oid);
    }

  /*
   * estimate number of index pages with no duplicates and 50% duplicates
   * The calculated number of pages without duplicates is a good estimate
   * for the first phase of the sort-merge. The 50% duplicates is used
   * to pre-allocate the pages for the index. If more pages are needed, they
   * are allocated at a later time in sets.
   */

  num_sort_pages =
    btree_estimate_total_numpages (thread_p, est_obj_cnt, key_len, key_type,
				   est_obj_cnt, &est1, &est2);

  if (est_obj_cnt > 100)
    {
      init_pgcnt =
	btree_estimate_total_numpages (thread_p,
				       (int) (est_obj_cnt * 0.50) + 1,
				       key_len, key_type, est_obj_cnt, &est1,
				       &est2);
    }
  else
    {
      init_pgcnt = num_sort_pages;
    }

  if (file_create
      (thread_p, &btid->vfid, init_pgcnt, FILE_BTREE, &btdes, &vpid,
       1) == NULL)
    {
      goto error;
    }
  file_created = 1;

  /*
   * Note: We do not initialize the allocated pages during the allocation
   *       since they belong to a new file and we do not perfom any undo
   *       logging on it. In fact some of the pages may be returned to the
   *       file manager at a later point. We should not waste time and
   *       storage to initialize those pages at this moment. This is safe
   *       since this is a new file.
   *       The pages are initialized in btree_get_page
   */
  if (file_alloc_pages_as_noncontiguous (thread_p, &btid->vfid, &vpid, &est1,
					 init_pgcnt, NULL, NULL,
					 NULL) == NULL)
    {

      /* allocation failed, allocate maximum possible */
      init_pgcnt = file_find_maxpages_allocable (thread_p, &btid->vfid);
      if (init_pgcnt > 0)
	{
	  if (file_alloc_pages_as_noncontiguous
	      (thread_p, &btid->vfid, &vpid, &est1, init_pgcnt, NULL, NULL,
	       NULL) == NULL)
	    {
	      /* allocate pages one by one */
	      for (k = 0; k < init_pgcnt; k++)
		{
		  if (file_alloc_pages (thread_p, &btid->vfid, &vpid, 1,
					NULL, NULL, NULL) == NULL)
		    {
		      break;
		    }
		}
	      init_pgcnt = k;	/* add also the root page allocated */
	    }
	}
    }

  init_pgcnt++;			/* increment for the root page allocated */

  used_pgcnt = 1;		/* set used page count (first page used for root) */

    /** Initialize the fields of loading argument structures **/
  load_args->btid = &btid_int;
  load_args->init_pgcnt = init_pgcnt;
  load_args->used_pgcnt = used_pgcnt;
  load_args->nodup_pgcnt = num_sort_pages;
  db_make_null (&load_args->current_key);
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
  load_args->out_recdes.data = (char *) malloc (DB_PAGESIZE);
  if (load_args->out_recdes.data == NULL)
    {
      goto error;
    }

  /* Build the leaf pages of the btree as the output of the sort */
  if (btree_index_sort
      (thread_p, sort_args, num_sort_pages, btree_construct_leafs,
       load_args) != NO_ERROR)
    {
      goto error;
    }

  if (sort_args->attrinfo_inited)
    {
      heap_attrinfo_end (thread_p, &sort_args->attr_info);
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

      if (btree_build_nleafs
	  (thread_p, load_args, sort_args->n_nulls, sort_args->n_oids,
	   load_args->n_keys, unique_flag, reverse_flag) != NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BTREE_LOAD_FAILED, 0);
	  goto error;
	}

      /* There is at least one leaf page */

      /* Release the memory area */
      free_and_init (load_args->out_recdes.data);
      pr_clear_value (&load_args->current_key);

      /* deallocate unused pages from the index file */
      if (load_args->used_pgcnt < load_args->init_pgcnt
	  && file_truncate_to_numpages (thread_p, &btid->vfid,
					load_args->used_pgcnt) != NO_ERROR)
	{
	  goto error;
	}

#if defined(BTREE_DEBUG)
      (void) btree_verify_tree (thread_p, &btid_int, NULL, NULL);
#endif /* BTREE_DEBUG */

    }
  else
    {
      /* there are no index entries, destroy index file and call index
         creation */
      if (file_destroy (thread_p, &btid->vfid) != NO_ERROR)
	{
	  goto error;
	}
      file_created = 0;
      free_and_init (load_args->out_recdes.data);

      btid->vfid.volid = save_volid;
      xbtree_add_index (thread_p, btid, key_type, &class_oids[0], attr_ids[0],
			unique_flag, reverse_flag,
			sort_args->n_oids, sort_args->n_nulls,
			load_args->n_keys);
    }


  log_end_system_op (thread_p, LOG_RESULT_TOPOP_COMMIT);
  addr.vfid = NULL;
  addr.pgptr = NULL;
  addr.offset = 0;
  log_append_undo_data (thread_p, RVBT_CREATE_INDEX, &addr, sizeof (VFID),
			&(btid->vfid));

  LOG_CS_ENTER (thread_p);
  logpb_force (thread_p);
  LOG_CS_EXIT ();

  return btid;

error:

  if (sort_args->scancache_inited)
    {
      (void) heap_scancache_end (thread_p, &sort_args->hfscan_cache);
    }
  if (sort_args->attrinfo_inited)
    {
      heap_attrinfo_end (thread_p, &sort_args->attr_info);
    }

  if (file_created)
    {
      (void) file_destroy (thread_p, &btid->vfid);
    }
  VFID_SET_NULL (&btid->vfid);
  btid->root_pageid = NULL_PAGEID;
  if (load_args->out_recdes.data)
    {
      free_and_init (load_args->out_recdes.data);
    }
  if (load_args->leaf.pgptr)
    {
      pgbuf_unfix (thread_p, load_args->leaf.pgptr);
      load_args->leaf.pgptr = NULL;
    }
  if (load_args->ovf.pgptr)
    {
      pgbuf_unfix (thread_p, load_args->ovf.pgptr);
      load_args->ovf.pgptr = NULL;
    }
  if (load_args->nleaf.pgptr)
    {
      pgbuf_unfix (thread_p, load_args->nleaf.pgptr);
      load_args->nleaf.pgptr = NULL;
    }

  log_end_system_op (thread_p, LOG_RESULT_TOPOP_ABORT);

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
  RECDES temp_recdes;		/* Temporary record descriptor; */
  int sp_success;
  int cur_maxspace;
  OR_ALIGNED_BUF (NODE_HEADER_SIZE) a_temp_data;
  int ret = NO_ERROR;

  temp_recdes.data = OR_ALIGNED_BUF_START (a_temp_data);
  temp_recdes.area_size = NODE_HEADER_SIZE;

  if (load_args->overflowing == true)
    {
      /* Store the record in current overflow page */
      sp_success = spage_insert (thread_p, load_args->ovf.pgptr,
				 &load_args->out_recdes, &slotid);
      if (sp_success != SP_SUCCESS)
	{
	  goto exit_on_error;
	}

      /* Save the current overflow page */
      btree_log_page (thread_p, &load_args->btid->sys_btid->vfid,
		      load_args->ovf.pgptr);
      load_args->ovf.pgptr = NULL;

      /* Update the leaf header of the current leaf page */
      btree_write_node_header (&temp_recdes, &load_args->leaf.hdr);

      sp_success =
	spage_update (thread_p, load_args->leaf.pgptr, HEADER, &temp_recdes);
      if (sp_success != SP_SUCCESS)
	{
	  goto exit_on_error;
	}

      /* Save the current leaf page */
      btree_log_page (thread_p, &load_args->btid->sys_btid->vfid,
		      load_args->leaf.pgptr);
      load_args->leaf.pgptr = NULL;
    }
  else
    {				/* No overflowing */

      cur_maxspace =
	spage_max_space_for_new_record (thread_p, load_args->leaf.pgptr);
      if (((cur_maxspace - load_args->out_recdes.length) < LOAD_FIXED_EMPTY)
	  && (spage_number_of_records (load_args->leaf.pgptr) > 1))
	{

	  /* New record does not fit into the current leaf page (within
	   * the thresold value); so allocate a new leaf page and dump
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

      /* Update the node header information for this record */
      load_args->leaf.hdr.key_cnt++;
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
      btree_write_node_header (&temp_recdes, &load_args->leaf.hdr);

      sp_success =
	spage_update (thread_p, load_args->leaf.pgptr, HEADER, &temp_recdes);
      if (sp_success != SP_SUCCESS)
	{
	  goto exit_on_error;
	}

      /* Save the current leaf page */
      btree_log_page (thread_p, &load_args->btid->sys_btid->vfid,
		      load_args->leaf.pgptr);
      load_args->leaf.pgptr = NULL;
    }				/* No overflowing */

end:

  return ret;

exit_on_error:

  if (load_args->leaf.pgptr)
    {
      pgbuf_unfix (thread_p, load_args->leaf.pgptr);
      load_args->leaf.pgptr = NULL;
    }
  if (load_args->ovf.pgptr)
    {
      pgbuf_unfix (thread_p, load_args->ovf.pgptr);
      load_args->ovf.pgptr = NULL;
    }

  if (ret == NO_ERROR)
    {
      ret = er_errid ();
      if (ret == NO_ERROR)
	{
	  ret = ER_FAILED;
	}
    }
  goto end;
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
 * the given threshold boundries (LOAD_FIXED_EMPTY bytes of each
 * page are reserved for future insertions) then this page is
 * flushed to disk and a new page is allocated to become the
 * current non-leaf page.
 */

static PAGE_PTR
btree_connect_page (THREAD_ENTRY * thread_p, DB_VALUE * key, int max_key_len,
		    VPID * pageid, LOAD_ARGS * load_args)
{
  NON_LEAF_REC nleaf_rec;
  INT16 slotid;
  RECDES temp_recdes;		/* Temporary record descriptor; */
  int sp_success;
  int cur_maxspace;
  int key_len = btree_get_key_length (key);
  OR_ALIGNED_BUF (NODE_HEADER_SIZE) a_temp_data;

  temp_recdes.data = OR_ALIGNED_BUF_START (a_temp_data);
  temp_recdes.area_size = NODE_HEADER_SIZE;

  /* form the leaf record (create the header & insert the key) */
  cur_maxspace =
    spage_max_space_for_new_record (thread_p, load_args->nleaf.pgptr);

  nleaf_rec.pnt = *pageid;
  nleaf_rec.key_len = (key_len < BTREE_MAX_KEYLEN_INPAGE) ? key_len : -1;
  if (btree_write_record (thread_p, load_args->btid, &nleaf_rec, key, false,
			  (nleaf_rec.key_len == -1), key_len,
			  true, NULL, NULL, &load_args->out_recdes)
      != NO_ERROR)
    {
      return NULL;
    }

  if ((cur_maxspace - load_args->out_recdes.length) < LOAD_FIXED_EMPTY)
    {

      /* New record does not fit into the current non-leaf page (within
       * the thresold value); so allocate a new non-leaf page and dump
       * the current non-leaf page.
       */

      /* Decrement the key counter for this non-leaf node */
      load_args->nleaf.hdr.key_cnt--;

      /* Update the non-leaf page header */
      btree_write_node_header (&temp_recdes, &load_args->nleaf.hdr);

      sp_success = spage_update (thread_p, load_args->nleaf.pgptr, HEADER,
				 &temp_recdes);
      if (sp_success != SP_SUCCESS)
	{
	  return NULL;
	}

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
			&load_args->nleaf.hdr, NON_LEAF_NODE,
			&load_args->init_pgcnt, &load_args->used_pgcnt,
			load_args->nodup_pgcnt);
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

  /* Update the node header information for this record */
  load_args->nleaf.hdr.key_cnt++;
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
 *   unique_flag(in):
 *   reverse_flag(in):
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
		    int n_nulls, int n_oids, int n_keys, int unique_flag,
		    int reverse_flag)
{
  RECDES temp_recdes;		/* Temporary record descriptor; */
  char *temp_data = NULL;
  VPID next_vpid;
  PAGE_PTR next_pageptr = NULL;

  /* Variables used in the second phase to go over lower level non-leaf
     pages */
  VPID cur_nleafpgid;
  PAGE_PTR cur_nleafpgptr = NULL;

  char *header_ptr;
  BTREE_NODE *temp;
  BTREE_ROOT_HEADER root_header;
  int key_cnt;
  int max_key_len, new_max;
  LEAF_REC leaf_pnt;
  NON_LEAF_REC nleaf_pnt;
  /* Variables for keeping keys */
  DB_VALUE last_key;		/* Last key of the current page */
  DB_VALUE first_key;		/* First key of the next page; used only if
				   key_type is one of the string types */
  DB_VALUE prefix_key;		/* Prefix key; prefix of last & first keys; used
				   only if type is one of the string types */
  int last_key_offset, first_key_offset;
  bool clear_last_key = false, clear_first_key = false;
  int sp_success;
  int ret = NO_ERROR;

  db_make_null (&last_key);
  db_make_null (&first_key);
  db_make_null (&prefix_key);

  temp_data = (char *) malloc (DB_PAGESIZE);
  if (temp_data == NULL)
    {
      goto exit_on_error;
    }

  /*****************************************************
      PHASE I : Build the first non_leaf level nodes
   ****************************************************/

  /* Initilize the first non-leaf page */
  load_args->nleaf.pgptr =
    btree_get_page (thread_p, load_args->btid->sys_btid,
		    &load_args->nleaf.vpid, &load_args->nleaf.vpid,
		    &load_args->nleaf.hdr, NON_LEAF_NODE,
		    &load_args->init_pgcnt, &load_args->used_pgcnt,
		    load_args->nodup_pgcnt);
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
  load_args->leaf.pgptr = pgbuf_fix (thread_p, &load_args->leaf.vpid,
				     OLD_PAGE, PGBUF_LATCH_WRITE,
				     PGBUF_UNCONDITIONAL_LATCH);
  if (load_args->leaf.pgptr == NULL)
    {
      goto exit_on_error;
    }

  btree_get_header_ptr (load_args->leaf.pgptr, &header_ptr);
  /* get the maximum key length on this leaf page */
  max_key_len = BTREE_GET_NODE_MAX_KEY_LEN (header_ptr);
  /* get the number of keys in this page */
  key_cnt = BTREE_GET_NODE_KEY_CNT (header_ptr);
  BTREE_GET_NODE_NEXT_VPID (header_ptr, &next_vpid);

  /* While there are some more leaf pages do */

  while (next_vpid.pageid != NULL_PAGEID)
    {
      /* Learn the last key of the current page */
      if (spage_get_record (load_args->leaf.pgptr, key_cnt,
			    &temp_recdes, PEEK) != S_SUCCESS)
	{
	  goto exit_on_error;
	}

      btree_read_record (thread_p, load_args->btid, &temp_recdes, &last_key,
			 &leaf_pnt, true, &clear_last_key, &last_key_offset,
			 0);

      if (pr_is_string_type (load_args->btid->key_type->type->id))
	{
	  /* Key type is string.
	   * Should insert the prefix key to the parent level
	   */

	  /* Learn the first key of the next page */
	  next_pageptr =
	    pgbuf_fix (thread_p, &next_vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
		       PGBUF_UNCONDITIONAL_LATCH);
	  if (next_pageptr == NULL)
	    {
	      goto exit_on_error;
	    }
	  if (spage_get_record (next_pageptr, 1, &temp_recdes, PEEK)
	      != S_SUCCESS)
	    {
	      goto exit_on_error;
	    }

	  btree_read_record (thread_p, load_args->btid, &temp_recdes,
			     &first_key, &leaf_pnt, true, &clear_first_key,
			     &first_key_offset, 0);

	  /* Insert the prefix key to the parent level */
	  ret =
	    btree_get_prefix (&last_key, &first_key, &prefix_key,
			      reverse_flag);
	  if (ret != NO_ERROR)
	    {
	      goto exit_on_error;
	    }

	  /* We may need to update the max_key length if the mid key is larger than
	   * the max key length.  This will only happen when the varying key length
	   * is larger than the fixed key length in pathological cases like char(4)
	   */
	  new_max = btree_get_key_length (&prefix_key);
	  max_key_len = (new_max > max_key_len) ? new_max : max_key_len;

	  if (btree_connect_page
	      (thread_p, &prefix_key, max_key_len, &load_args->leaf.vpid,
	       load_args) == NULL)
	    {
	      goto exit_on_error;
	    }

	  /* Proceed the current leaf page pointer to the next leaf page */
	  pgbuf_unfix (thread_p, load_args->leaf.pgptr);
	  load_args->leaf.vpid = next_vpid;
	  load_args->leaf.pgptr = next_pageptr;
	  next_pageptr = NULL;

	  btree_clear_key_value (&clear_first_key, &first_key);

	  /* We always need to clear the prefix key.  It is always a copy. */
	  pr_clear_value (&prefix_key);
	}
      else
	{			/* key type is not string */

	  /* Insert this key to the parent level */
	  if (btree_connect_page
	      (thread_p, &last_key, max_key_len, &load_args->leaf.vpid,
	       load_args) == NULL)
	    {
	      goto exit_on_error;
	    }

	  /* Proceed the current leaf page pointer to the next leaf page */
	  pgbuf_unfix (thread_p, load_args->leaf.pgptr);
	  load_args->leaf.vpid = next_vpid;
	  load_args->leaf.pgptr = pgbuf_fix (thread_p, &load_args->leaf.vpid,
					     OLD_PAGE, PGBUF_LATCH_WRITE,
					     PGBUF_UNCONDITIONAL_LATCH);
	  if (load_args->leaf.pgptr == NULL)
	    {
	      goto exit_on_error;
	    }
	}

      /* obtain the header information for the current leaf page */
      btree_get_header_ptr (load_args->leaf.pgptr, &header_ptr);
      /* get the maximum key length on this leaf page */
      max_key_len = BTREE_GET_NODE_MAX_KEY_LEN (header_ptr);
      /* get the number of keys in this page */
      key_cnt = BTREE_GET_NODE_KEY_CNT (header_ptr);
      BTREE_GET_NODE_NEXT_VPID (header_ptr, &next_vpid);

      btree_clear_key_value (&clear_last_key, &last_key);
    }				/* while there are some more leaf pages */

  /* Current page is the last leaf page; So, obtain the biggest key
     and max_key_size from this last page and insert them to the parent level */

  /* Learn the last key of the current page */
  if (spage_get_record (load_args->leaf.pgptr, key_cnt,
			&temp_recdes, PEEK) != S_SUCCESS)
    {
      goto exit_on_error;
    }

  btree_read_record (thread_p, load_args->btid, &temp_recdes, &last_key,
		     &leaf_pnt, true, &clear_last_key, &last_key_offset, 0);

  /* Insert this key to the parent level */
  if (btree_connect_page
      (thread_p, &last_key, max_key_len, &load_args->leaf.vpid,
       load_args) == NULL)
    {
      goto exit_on_error;
    }

  /* Now, we can free the leaf page buffer */
  pgbuf_unfix (thread_p, load_args->leaf.pgptr);
  load_args->leaf.pgptr = NULL;

  /* Decrement the key counter for this non-leaf node */
  load_args->nleaf.hdr.key_cnt--;

  /* Update the non-leaf page header */
  temp_recdes.area_size = DB_PAGESIZE;
  temp_recdes.data = temp_data;
  btree_write_node_header (&temp_recdes, &load_args->nleaf.hdr);

  sp_success =
    spage_update (thread_p, load_args->nleaf.pgptr, HEADER, &temp_recdes);
  if (sp_success != SP_SUCCESS)
    {
      goto exit_on_error;
    }


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

  /* Switch the push and pop lists */
  load_args->pop_list = load_args->push_list;
  load_args->push_list = NULL;

  while (length_list (load_args->pop_list) > 1)
    {
      /* while there are more than one page at the previous level do
         construct the next level */

      /* Initiliaze the first non-leaf page of current level */
      load_args->nleaf.pgptr =
	btree_get_page (thread_p, load_args->btid->sys_btid,
			&load_args->nleaf.vpid, &load_args->nleaf.vpid,
			&load_args->nleaf.hdr, NON_LEAF_NODE,
			&load_args->init_pgcnt, &load_args->used_pgcnt,
			load_args->nodup_pgcnt);
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

	  /* obtain the header information for the current non-leaf page */
	  btree_get_header_ptr (cur_nleafpgptr, &header_ptr);
	  /* get the maximum key length on this leaf page */
	  max_key_len = BTREE_GET_NODE_MAX_KEY_LEN (header_ptr);
	  /* get the number of keys in this page */
	  key_cnt = BTREE_GET_NODE_KEY_CNT (header_ptr);

	  /* Learn the last key of the current page */
	  /* Notice that since this is a non-leaf node */
	  if (spage_get_record
	      (cur_nleafpgptr, key_cnt + 1, &temp_recdes, PEEK) != S_SUCCESS)
	    {
	      goto exit_on_error;
	    }

	  btree_read_record (thread_p, load_args->btid, &temp_recdes,
			     &last_key, &nleaf_pnt, false, &clear_last_key,
			     &last_key_offset, 0);

	  /* Insert this key to the parent level */
	  if (btree_connect_page
	      (thread_p, &last_key, max_key_len, &cur_nleafpgid,
	       load_args) == NULL)
	    {
	      goto exit_on_error;
	    }

	  pgbuf_unfix (thread_p, cur_nleafpgptr);
	  cur_nleafpgptr = NULL;

	  /* Remove this pageid from the pop list */
	  remove_first (&load_args->pop_list);

	  btree_clear_key_value (&clear_last_key, &last_key);
	}
      while (length_list (load_args->pop_list) > 0);

      /* FLUSH LAST NON-LEAF PAGE */

      /* Decrement the key counter for this non-leaf node */
      load_args->nleaf.hdr.key_cnt--;

      /* Update the non-leaf page header */
      temp_recdes.area_size = DB_PAGESIZE;
      temp_recdes.data = temp_data;
      btree_write_node_header (&temp_recdes, &load_args->nleaf.hdr);

      sp_success = spage_update (thread_p, load_args->nleaf.pgptr, HEADER,
				 &temp_recdes);
      if (sp_success != SP_SUCCESS)
	{
	  goto exit_on_error;
	}

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

    }				/* while */

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

  /* Prepare the root header by using the last leaf node header */
  root_header.node.node_type = NON_LEAF_NODE;
  root_header.node.key_cnt = load_args->nleaf.hdr.key_cnt;
  root_header.node.max_key_len = load_args->nleaf.hdr.max_key_len;
  VPID_SET_NULL (&root_header.node.next_vpid);
  root_header.key_type = load_args->btid->key_type;

  if (unique_flag)
    {
      root_header.num_nulls = n_nulls;
      root_header.num_oids = n_oids;
      root_header.num_keys = n_keys;
      root_header.unique = unique_flag;
    }
  else
    {
      root_header.num_nulls = -1;
      root_header.num_oids = -1;
      root_header.num_keys = -1;
      root_header.unique = false;
    }

  if (reverse_flag)
    {
      root_header.reverse = true;
    }
  else
    {
      root_header.reverse = false;
    }

  root_header.ovfid = load_args->btid->ovfid;	/* structure copy */
  root_header.rev_level = BTREE_CURRENT_REV_LEVEL;

  temp_recdes.area_size = DB_PAGESIZE;
  temp_recdes.data = temp_data;
  btree_write_root_header (&temp_recdes, &root_header);

  sp_success =
    spage_update (thread_p, load_args->nleaf.pgptr, HEADER, &temp_recdes);
  if (sp_success != SP_SUCCESS)
    {
      goto exit_on_error;
    }

  /* move current ROOT page content to the first page allocated */
  if (file_find_nthpages (thread_p, &load_args->btid->sys_btid->vfid,
			  &cur_nleafpgid, 0, 1) != 1)
    {
      goto exit_on_error;
    }
  next_pageptr = pgbuf_fix (thread_p, &cur_nleafpgid, OLD_PAGE,
			    PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (next_pageptr == NULL)
    {
      goto exit_on_error;
    }

  memcpy (next_pageptr, load_args->nleaf.pgptr, DB_PAGESIZE);
  pgbuf_unfix (thread_p, load_args->nleaf.pgptr);
  load_args->nleaf.pgptr = NULL;

  if (load_args->used_pgcnt > load_args->init_pgcnt)
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

  /* Note: Here ONLY Page ID ASSIGNMENT is made */
  load_args->btid->sys_btid->root_pageid = load_args->nleaf.vpid.pageid;

  /*
   * The root page must be logged, otherwise, in the event of a crash. The
   * index may be gone.
   */
  btree_log_page (thread_p, &load_args->btid->sys_btid->vfid,
		  load_args->nleaf.pgptr);
  load_args->nleaf.pgptr = NULL;

end:

  /* cleanup */
  if (next_pageptr)
    {
      pgbuf_unfix (thread_p, next_pageptr);
      next_pageptr = NULL;
    }
  if (cur_nleafpgptr)
    {
      pgbuf_unfix (thread_p, cur_nleafpgptr);
      cur_nleafpgptr = NULL;
    }
  if (load_args->leaf.pgptr)
    {
      pgbuf_unfix (thread_p, load_args->leaf.pgptr);
      load_args->leaf.pgptr = NULL;
    }
  if (load_args->nleaf.pgptr)
    {
      pgbuf_unfix (thread_p, load_args->nleaf.pgptr);
      load_args->nleaf.pgptr = NULL;
    }
  if (temp_data)
    {
      free_and_init (temp_data);
    }

  return ret;

/* error handling */
exit_on_error:

  if (ret == NO_ERROR)
    {
      ret = er_errid ();
      if (ret == NO_ERROR)
	{
	  ret = ER_FAILED;
	}
    }
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
 *   init_pgcnt(in): The number of pages already allocated
 *   used_pgcnt(in): The number of in use pages that has been allocated
 *   nodup_pgcnt(in): The number of estimated pages without duplicates keys
 *
 * Note: This function allocates a new page for the B+Tree index.
 * The page is initialized and the node header is inserted before
 * returning the page for further usage.
 */
static PAGE_PTR
btree_get_page (THREAD_ENTRY * thread_p, BTID * btid, VPID * page_id,
		VPID * nearpg, BTREE_NODE_HEADER * header, short node_type,
		int *init_pgcnt, int *used_pgcnt, int nodup_pgcnt)
{
  PAGE_PTR page_ptr = NULL;
  VPID ovf_vpid = { NULL_PAGEID, NULL_VOLID };
  RECDES temp_recdes;		/* Temporary record descriptor; */
  VPID vpid;
  LOG_DATA_ADDR addr;
  int num_pages, nthpage;
  OR_ALIGNED_BUF (NODE_HEADER_SIZE) a_temp_data;
  unsigned short alignment;

  temp_recdes.data = OR_ALIGNED_BUF_START (a_temp_data);
  temp_recdes.area_size = NODE_HEADER_SIZE;

  if (*used_pgcnt < *init_pgcnt)
    {				/* index has still unused pages */
      if (file_find_nthpages (thread_p, &btid->vfid, page_id, *used_pgcnt, 1)
	  <= 0)
	{
	  return NULL;
	}
    }
  else
    {

      if ((num_pages = (int) ((nodup_pgcnt - *init_pgcnt) * 0.20)) < 500)
	{
	  num_pages = nodup_pgcnt - *init_pgcnt;
	  if (num_pages > 500)
	    {
	      num_pages = 500;
	    }
	  else if (num_pages < 10)
	    {
	      num_pages = 10;
	    }
	}

      /*
       * Note: We do not initialize the allocated pages during the allocation
       *       since they belong to a new file and we do not perfom any undo
       *       logging on it. In fact some of the pages may be returned to the
       *       file manager at a later point. We should not waste time and
       *       storage to initialize those pages at this moment. This is safe
       *       since this is a new file.
       */

      if (file_alloc_pages_as_noncontiguous
	  (thread_p, &btid->vfid, page_id, &nthpage, num_pages, nearpg, NULL,
	   NULL) != NULL)
	{
	  *init_pgcnt += num_pages;
	}
      else
	{
	  if (file_alloc_pages
	      (thread_p, &btid->vfid, page_id, 1, nearpg, NULL, NULL) == NULL)
	    {
	      return NULL;
	    }
	  else
	    {
	      *init_pgcnt += 1;
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

  alignment = MAX_ALIGNMENT;
  spage_initialize (thread_p, page_ptr, UNANCHORED_KEEP_SEQUENCE,
		    MAX_ALIGNMENT, DONT_SAFEGUARD_RVSPACE);

  addr.vfid = &btid->vfid;
  addr.offset = -1;		/* No header slot is initialized */
  addr.pgptr = page_ptr;

  log_append_redo_data (thread_p, RVBT_GET_NEWPAGE, &addr, sizeof (alignment),
			&alignment);

  if (header)
    {				/* This is going to be a leaf page */
      /* Insert the node header (with initial values) to the leaf node */
      header->node_type = node_type;
      header->key_cnt = 0;
      header->max_key_len = 0;
      VPID_SET_NULL (&header->next_vpid);

      btree_write_node_header (&temp_recdes, header);
    }
  else
    {				/* This is going to be an overflow page */
      /* the new overflow page is the last one */
      btree_write_overflow_header (&temp_recdes, &ovf_vpid);
    }

  temp_recdes.type = REC_HOME;
  if (spage_insert_at (thread_p, page_ptr, HEADER, &temp_recdes) !=
      SP_SUCCESS)
    {
      /* Cannot happen; header is smaller than new page... */
      return NULL;
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
  BTREE_NODE_HEADER new_leafhdr;
  RECDES temp_recdes;		/* Temporary record descriptor; */
  int sp_success;
  OR_ALIGNED_BUF (NODE_HEADER_SIZE) a_temp_data;

  temp_recdes.data = OR_ALIGNED_BUF_START (a_temp_data);
  temp_recdes.area_size = NODE_HEADER_SIZE;

  /* Allocate the new leaf page */
  new_leafpgptr = btree_get_page (thread_p, load_args->btid->sys_btid,
				  &new_leafpgid,
				  &load_args->leaf.vpid,
				  &new_leafhdr,
				  LEAF_NODE,
				  &load_args->init_pgcnt,
				  &load_args->used_pgcnt,
				  load_args->nodup_pgcnt);
  if (new_leafpgptr == NULL)
    {
      return NULL;
    }

  /* make the current leaf page point to the new one */
  load_args->leaf.hdr.next_vpid = new_leafpgid;
  btree_write_node_header (&temp_recdes, &load_args->leaf.hdr);

  sp_success =
    spage_update (thread_p, load_args->leaf.pgptr, HEADER, &temp_recdes);
  if (sp_success != SP_SUCCESS)
    {
      pgbuf_unfix (thread_p, new_leafpgptr);
      new_leafpgptr = NULL;

      return NULL;
    }

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
		 OID * class_oid, OID * first_oid, LOAD_ARGS * load_args)
{
  LEAF_REC leaf_rec;
  int key_len;

  /* form the leaf record (create the header & insert the key) */
  key_len = load_args->cur_key_len = btree_get_key_length (this_key);
  VPID_SET_NULL (&leaf_rec.ovfl);
  leaf_rec.key_len = (key_len < BTREE_MAX_KEYLEN_INPAGE) ? key_len : -1;
  if (btree_write_record
      (thread_p, load_args->btid, &leaf_rec, this_key, true,
       (leaf_rec.key_len == -1), key_len, true, class_oid, first_oid,
       &load_args->out_recdes) != NO_ERROR)
    {
      /* this must be an overflow key insertion failure, we assume the
       * overflow manager has logged an error.
       */
      return er_errid ();
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
  LOAD_ARGS *load_args;
  LEAF_REC leaf_rec;
  DB_VALUE this_key;		/* Key value in this sorted item
				   (specified with in_recdes) */
  OID this_class_oid;		/* Class OID value in this sorted item */
  OID this_oid;			/* OID value in this sorted item */
  RECDES temp_recdes;		/* Temporary record descriptor; */
  int sp_success;
  int cur_maxspace;
  INT16 slotid;
  bool same_key = true;
  VPID new_ovfpgid;
  PAGE_PTR new_ovfpgptr = NULL;
  OR_BUF buf;
  bool copy = false;
  OR_ALIGNED_BUF (NODE_HEADER_SIZE) a_temp_data;
  RECDES sort_key_recdes, *recdes;
  char *next;
  int next_size;
  int key_size = -1;

  temp_recdes.data = OR_ALIGNED_BUF_START (a_temp_data);
  temp_recdes.area_size = NODE_HEADER_SIZE;
  load_args = (LOAD_ARGS *) arg;

  sort_key_recdes = *in_recdes;
  recdes = &sort_key_recdes;

  next_size = sizeof (char *);

  for (;;)
    {				/* Infinite loop; will exit with break statement */

      next = *(char **) recdes->data;	/* save forward link */

      /* First decompose the input record into the key and oid components */
      or_init (&buf, recdes->data, recdes->length);
      assert (((UINTPTR) recdes->data) % 8 == 0);

      /* Skip forward link */
      or_advance (&buf, next_size);

      /* Instance level uniqueness checking */
      if (BTREE_IS_UNIQUE (load_args->btid))
	{			/* unique index */
	  /* extract class OID */
	  if (or_get_oid (&buf, &this_class_oid) != NO_ERROR)
	    {
	      goto error;
	    }
	}

      if (or_get_oid (&buf, &this_oid) != NO_ERROR)
	{
	  goto error;
	}
      buf.ptr = PTR_ALIGN (buf.ptr, MAX_ALIGNMENT);

      /* Do not copy the string--just use the pointer.  The pr_ routines
       * for strings and sets have different semantics for length.
       */
      if (load_args->btid->key_type->type->id == DB_TYPE_MIDXKEY)
	{
	  key_size = CAST_STRLEN (buf.endptr - buf.ptr);
	}

      if ((*(load_args->btid->key_type->type->readval)) (&buf, &this_key,
							 load_args->btid->
							 key_type, key_size,
							 copy, NULL,
							 0) != NO_ERROR)
	{
	  goto error;
	}

      /* Find out if this is the first call to this function */
      if (load_args->leaf.vpid.pageid == NULL_PAGEID)
	{
	  /* This is the first call to this function; so, initilize some fields
	     in the LOAD_ARGS structure */

	  (load_args->n_keys)++;	/* Increment the key counter */

	  /* Allocate the first page for the index */
	  load_args->leaf.pgptr =
	    btree_get_page (thread_p, load_args->btid->sys_btid,
			    &load_args->leaf.vpid, &load_args->leaf.vpid,
			    &load_args->leaf.hdr, LEAF_NODE,
			    &load_args->init_pgcnt, &load_args->used_pgcnt,
			    load_args->nodup_pgcnt);
	  if (load_args->leaf.pgptr == NULL)
	    {
	      goto error;
	    }

#if 0				/* TODO: currently not used */
	  load_args->first_leafpgid = load_args->leaf.vpid;
#endif
	  load_args->max_recsize =
	    spage_max_space_for_new_record (thread_p, load_args->leaf.pgptr);
	  load_args->overflowing = false;

	  /* Create the first record of the current page in main memory */
	  if (btree_first_oid
	      (thread_p, &this_key, &this_class_oid, &this_oid,
	       load_args) != NO_ERROR)
	    {
	      goto error;
	    }
	}
      else
	{			/* This is not the first call to this function */
	  /*
	   * Compare the received key with the current one.
	   * If different, then dump the current record and create a new record.
	   */

	  same_key = !(*(load_args->btid->key_type->type->cmpval))
	    (&this_key, &load_args->current_key,
	     load_args->btid->key_type, load_args->btid->reverse, 0, 1, NULL);

	  /* EQUALITY test only - doesn't care the reverse index */

	  if (same_key)
	    {
	      /* This key (retrieved key) is the same with the current one. */

	      /* instance level uniqueness checking */
	      if (BTREE_IS_UNIQUE (load_args->btid))
		{		/* unique index */
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_BTREE_UNIQUE_FAILED, 0);
		  goto error;
		}

	      /* Check if there is space in the memory record for the new OID.
	         If not dump the current record and create a new record. */

	      if ((load_args->new_pos + OR_OID_SIZE -
		   load_args->out_recdes.data) > load_args->max_recsize)
		{		/* There is no space for the new oid */


		  if (load_args->overflowing == true)
		    {
		      /* Store the record in current overflow page */
		      sp_success =
			spage_insert (thread_p, load_args->ovf.pgptr,
				      &load_args->out_recdes, &slotid);
		      if (sp_success != SP_SUCCESS)
			{
			  goto error;
			}

		      /* Allocate the new overflow page */
		      if ((new_ovfpgptr =
			   btree_get_page (thread_p,
					   load_args->btid->sys_btid,
					   &new_ovfpgid, &load_args->ovf.vpid,
					   NULL, LEAF_NODE,
					   &load_args->init_pgcnt,
					   &load_args->used_pgcnt,
					   load_args->nodup_pgcnt)) == NULL)
			goto error;

		      /* make the current overflow page point to the new one */
		      btree_write_overflow_header (&temp_recdes,
						   &new_ovfpgid);
		      temp_recdes.type = REC_HOME;

		      sp_success =
			spage_update (thread_p, load_args->ovf.pgptr, HEADER,
				      &temp_recdes);
		      if (sp_success != SP_SUCCESS)
			{
			  goto error;
			}

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

		      /* Try to insert the new record to the current leaf page. Remember
		       * that the new record is very big (almost the size of a page)
		       * and thus can fit only into an empty page. If it does not fit into
		       * current leaf page then get a new one, and insert it there.
		       */

		      if (spage_number_of_records (load_args->leaf.pgptr) > 1)
			{
			  /* The current leaf page does not have room for the new record,
			   * since only one record on a page can have overflow OIDs.
			   */
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
					&load_args->ovf.vpid, NULL,
					LEAF_NODE,
					&load_args->init_pgcnt,
					&load_args->used_pgcnt,
					load_args->nodup_pgcnt);
		      if (load_args->ovf.pgptr == NULL)
			{
			  goto error;
			}

		      /* Connect the new overflow page to the leaf page */
		      btree_read_fixed_portion_of_leaf_record (&load_args->
							       out_recdes,
							       &leaf_rec);
		      leaf_rec.ovfl = load_args->ovf.vpid;
		      btree_write_fixed_portion_of_leaf_record (&load_args->
								out_recdes,
								&leaf_rec);

		      /* Try to Store the record into the current leaf page */
		      sp_success =
			spage_insert (thread_p, load_args->leaf.pgptr,
				      &load_args->out_recdes, &slotid);
		      if (sp_success != SP_SUCCESS)
			{
			  goto error;
			}

		      /* Update the node header information for this record */
		      load_args->leaf.hdr.key_cnt++;
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

	      /* Insert the new oid to the current record and return */
	      OR_PUT_OID (load_args->new_pos, &this_oid);
	      load_args->out_recdes.length += OR_OID_SIZE;
	      load_args->new_pos += OR_OID_SIZE;

	    }			/* same key */
	  else
	    {
	      /* Current key is finished; dump this output record to the disk page */


	      (load_args->n_keys)++;	/* Increment the key counter */

	      /* Insert the current record to the current page */
	      if (load_args->overflowing == false)
		{
		  cur_maxspace =
		    spage_max_space_for_new_record (thread_p,
						    load_args->leaf.pgptr);

		  if (((cur_maxspace - load_args->out_recdes.length) <
		       LOAD_FIXED_EMPTY)
		      && (spage_number_of_records (load_args->leaf.pgptr) >
			  1))
		    {
		      /* New record does not fit into the current leaf page (within
		       * the thresold value); so allocate a new leaf page and dump
		       * the current leaf page.
		       */

		      if (btree_proceed_leaf (thread_p, load_args) == NULL)
			{
			  goto error;
			}

		    }		/* get a new leaf */

		  /* Insert the record to the current leaf page */
		  sp_success = spage_insert (thread_p, load_args->leaf.pgptr,
					     &load_args->out_recdes, &slotid);
		  if (sp_success != SP_SUCCESS)
		    {
		      goto error;
		    }

		  /* Update the node header information for this record */
		  load_args->leaf.hdr.key_cnt++;
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

		  /* Save the current overflow page */
		  btree_log_page (thread_p, &load_args->btid->sys_btid->vfid,
				  load_args->ovf.pgptr);
		  load_args->ovf.pgptr = NULL;

		  /* Proceed the current leaf page (i.e., flush the current leaf page
		     which had some overflow pages connected to it; and obtain a new
		     leaf page */

		  if (btree_proceed_leaf (thread_p, load_args) == NULL)
		    {
		      goto error;
		    }

		  /* Turn off the overflowing mode */
		  load_args->overflowing = false;
		  load_args->max_recsize =
		    spage_max_space_for_new_record (thread_p,
						    load_args->leaf.pgptr);

		}		/* Current page is an overflow page */

	      /* Create the first part of the next record in main memory */
	      if (btree_first_oid
		  (thread_p, &this_key, &this_class_oid, &this_oid,
		   load_args) != NO_ERROR)
		{
		  goto error;
		}
	    }			/* different key */
	}

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

  return NO_ERROR;

error:
  if (load_args->leaf.pgptr)
    {
      pgbuf_unfix (thread_p, load_args->leaf.pgptr);
      load_args->leaf.pgptr = NULL;
    }
  if (load_args->ovf.pgptr)
    {
      pgbuf_unfix (thread_p, load_args->ovf.pgptr);
      load_args->ovf.pgptr = NULL;
    }
  if (new_ovfpgptr)
    {
      pgbuf_unfix (thread_p, new_ovfpgptr);
      new_ovfpgptr = NULL;
    }
  btree_clear_key_value (&copy, &this_key);

  return er_errid ();

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
  if (load_args->btid->key_type->type->id == DB_TYPE_MIDXKEY)
    {
      key_size = buf.endptr - buf.ptr;
    }

  if ((*(load_args->btid->key_type->type->readval))
      (&buf, &this_key, load_args->btid->key_type, key_size, copy,
       NULL, 0) != NO_ERROR)
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

  if (ret == NO_ERROR)
    {
      ret = er_errid ();
      if (ret == NO_ERROR)
	{
	  ret = ER_FAILED;
	}
    }

  return ret;
}
#endif /* CUBRID_DEBUG */

/*
 * btree_index_sort () - Sort for the index file creation
 *   return: int
 *   sort_args(in): sort arguments; specifies the sort-attribute as well as
 *	            the structure of the input objects
 *   est_inp_pg_cnt(in): Estimated number of input pages to the sorting
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
		  int est_inp_pg_cnt, SORT_PUT_FUNC * out_func,
		  void *out_args)
{
  return sort_listfile (thread_p, sort_args->hfids[0].vfid.volid,
			est_inp_pg_cnt, &btree_sort_get_next, sort_args,
			out_func, out_args, compare_driver, sort_args,
			SORT_DUP);
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

  if (n_attrs > 1)
    {
      is_null = btree_multicol_key_is_null (keyval);
    }
  else
    {
      is_null = DB_IS_NULL (keyval);
    }

  if (!is_null && xbtree_find_unique (thread_p, pk_btid, keyval,
				      pk_cls_oid, &unique_oid,
				      true) != BTREE_KEY_FOUND)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FK_INVALID, 1, fk_name);
      ret = ER_FK_INVALID;
      goto exit_on_error;
    }

  if (cache_attr_id > 0)
    {
      ret =
	heap_scancache_start_modify (thread_p, &upd_scancache, hfid, cls_oid,
				     SINGLE_ROW_UPDATE);
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

      ret = locator_attribute_info_force (thread_p, hfid, oid, &attr_info,
					  &cache_attr_id, 1, LC_FLUSH_UPDATE,
					  SINGLE_ROW_UPDATE, &upd_scancache,
					  &force_count, true);
      if (ret != NO_ERROR)
	{
	  heap_attrinfo_end (thread_p, &attr_info);
	  heap_scancache_end_modify (thread_p, &upd_scancache);
	  goto exit_on_error;
	}

      heap_attrinfo_end (thread_p, &attr_info);
      heap_scancache_end_modify (thread_p, &upd_scancache);
    }

end:

  return ret;

exit_on_error:

  if (ret == NO_ERROR)
    {
      ret = er_errid ();
      if (ret == NO_ERROR)
	{
	  ret = ER_FAILED;
	}
    }
  goto end;
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
  OID class_oid;
  SORT_ARGS *sort_args;
  OR_BUF buf;
  int next_size;
  int oid_size;
  char midxkey_buf[DBVAL_BUFSIZE + MAX_ALIGNMENT], *aligned_midxkey_buf;

  aligned_midxkey_buf = PTR_ALIGN (midxkey_buf, MAX_ALIGNMENT);

  sort_args = (SORT_ARGS *) arg;
  prev_oid = sort_args->cur_oid;

  if (sort_args->unique_flag)
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
					true, false,
					LOCKHINT_NONE) != NO_ERROR)
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
	    }
	  continue;

	case S_ERROR:
	case S_DOESNT_EXIST:
	case S_DOESNT_FIT:
	case S_SUCCESS_CHN_UPTODATE:
	  return SORT_ERROR_OCCURRED;

	case S_SUCCESS:
	  break;
	}

      /* Obtain the key attribute value out of the object */
      /* Check for the class id, the object may belong to another class */

      OR_GET_OID (sort_args->in_recdes.data, &class_oid);

      if (OID_EQ (&class_oid, &sort_args->class_ids[cur_class]))
	{
	  /*
	   * Produce the sort item for this object
	   */
	  if (sort_args->n_attrs == 1)
	    {			/* single-column index */
	      if (heap_attrinfo_read_dbvalues (thread_p, &sort_args->cur_oid,
					       &sort_args->in_recdes,
					       &sort_args->attr_info) !=
		  NO_ERROR)
		{
		  return (SORT_ERROR_OCCURRED);
		}
	    }

	  dbvalue_ptr = heap_attrinfo_generate_key (thread_p,
						    sort_args->n_attrs,
						    &sort_args->
						    attr_ids[attr_offset],
						    &sort_args->attr_info,
						    &sort_args->in_recdes,
						    &dbvalue,
						    aligned_midxkey_buf);
	  if (dbvalue_ptr == NULL)
	    {
	      return (SORT_ERROR_OCCURRED);
	    }

	  if (sort_args->fk_refcls_oid
	      && !OID_ISNULL (sort_args->fk_refcls_oid))
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
		  return SORT_ERROR_OCCURRED;
		}
	    }

	  if (db_value_is_null (dbvalue_ptr)
	      || btree_multicol_key_is_null (dbvalue_ptr))
	    {
	      sort_args->n_oids++;	/* Increment the OID counter */
	      sort_args->n_nulls++;	/* Increment the NULL counter */
	      if (dbvalue_ptr == &dbvalue)
		{
		  pr_clear_value (&dbvalue);
		}
	      continue;
	    }

	  key_len = btree_get_key_length (dbvalue_ptr);

	  if (key_len > 0)
	    {
	      next_size = sizeof (char *);

	      if ((next_size + oid_size + key_len + MAX_ALIGN) >
		  temp_recdes->area_size)
		{
		  /*
		   * Record is too big to fit into temp_recdes area; so
		   * backtrack this iteration
		   */
		  sort_args->cur_oid = prev_oid;
		  temp_recdes->length = (next_size + oid_size
					 + key_len + MAX_ALIGN);
		  goto nofit;
		}

	      assert (((UINTPTR) temp_recdes->data) % 8 == 0);
	      or_init (&buf, temp_recdes->data, 0);

	      or_pad (&buf, next_size);	/* init as NULL */

	      if (sort_args->unique_flag)
		{
		  if (or_put_oid (&buf, &class_oid) != NO_ERROR)
		    {
		      goto nofit;
		    }
		}

	      if (or_put_oid (&buf, &sort_args->cur_oid) != NO_ERROR)
		{
		  goto nofit;
		}

	      buf.ptr = PTR_ALIGN (buf.ptr, MAX_ALIGNMENT);

	      if ((*(sort_args->key_type->type->writeval)) (&buf, dbvalue_ptr)
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

	  sort_args->n_oids++;	/* Increment the OID counter */

	  if (key_len > 0)
	    {
	      return SORT_SUCCESS;
	    }
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
  SORT_ARGS *sort_args;
  TP_DOMAIN *key_type;
  int r;

  sort_args = (SORT_ARGS *) arg;
  key_type = sort_args->key_type;

  assert (((UINTPTR) mem1) % 8 == 0);
  assert (((UINTPTR) mem2) % 8 == 0);

  /* Skip next link */
  mem1 += sizeof (char *);
  mem2 += sizeof (char *);

  /* Skip the oids */
  if (sort_args->unique_flag)
    {				/* unique index */
      mem1 += (2 * OR_OID_SIZE);
      mem2 += (2 * OR_OID_SIZE);
    }
  else
    {				/* non-unique index */
      mem1 += OR_OID_SIZE;
      mem2 += OR_OID_SIZE;
    }

  mem1 = PTR_ALIGN (mem1, MAX_ALIGNMENT);
  mem2 = PTR_ALIGN (mem2, MAX_ALIGNMENT);

  r = (*(key_type->type->cmpdisk)) (mem1, mem2,
				    key_type, sort_args->reverse_flag,
				    0, 1, NULL);
  return r;
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

  new_node = (BTREE_NODE *) malloc (sizeof (BTREE_NODE));
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

end:

  return ret;

exit_on_error:

  if (ret == NO_ERROR)
    {
      ret = er_errid ();
      if (ret == NO_ERROR)
	{
	  ret = ER_FAILED;
	}
    }
  goto end;
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
      free_and_init (temp);
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

  vfid = (VFID *) rcv->data;

  return ((file_destroy (thread_p, vfid) == NO_ERROR)
	  ? NO_ERROR : er_errid ());
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
