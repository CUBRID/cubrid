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
 * locator_sr.c : Transaction object locator (at server)
 */

#ident "$Id$"

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>

#include "locator_sr.h"
#include "locator.h"
#include "memory_alloc.h"
#include "storage_common.h"
#include "oid.h"
#include "memory_hash.h"
#include "error_manager.h"
#include "xserver_interface.h"
#include "list_file.h"
#include "query_manager.h"
#include "slotted_page.h"
#include "extendible_hash.h"
#include "btree.h"
#include "btree_load.h"
#include "heap_file.h"
#include "object_representation.h"
#include "object_representation_sr.h"
#include "log_manager.h"
#include "lock_manager.h"
#include "system_catalog.h"
#include "replication.h"
#include "critical_section.h"
#if defined(SERVER_MODE)
#include "connection_error.h"
#include "thread.h"
#endif /* SERVER_MODE */
#include "object_print.h"
#include "object_primitive.h"
#include "object_domain.h"
#include "system_parameter.h"
#include "log_impl.h"
#include "transaction_sr.h"
#include "boot_sr.h"
#include "partition.h"
#include "vacuum.h"
#if defined(ENABLE_SYSTEMTAP)
#include "probes.h"
#endif /* ENABLE_SYSTEMTAP */
#include "db.h"
#include "filter_pred_cache.h"
#include "fetch.h"

#if defined(DMALLOC)
#include "dmalloc.h"
#endif /* DMALLOC */

/* TODO : remove */
extern bool catcls_Enable;

static const int LOCATOR_GUESS_NUM_NESTED_REFERENCES = 100;
#define LOCATOR_GUESS_HT_SIZE    LOCATOR_GUESS_NUM_NESTED_REFERENCES * 2

#define CLASSNAME_CACHE_SIZE            1024

/* flag for INSERT/UPDATE/DELETE statement */
typedef enum
{
  FOR_INSERT_OR_DELETE,		/* It is an delete or insert statement, not update statement */
  FOR_MOVE			/* It is an update statement on partitioned table, move a record from one partition to
				 * another */
} LOCATOR_INDEX_ACTION_FLAG;

extern int catcls_insert_catalog_classes (THREAD_ENTRY * thread_p, RECDES * record);
extern int catcls_delete_catalog_classes (THREAD_ENTRY * thread_p, const char *name, OID * class_oid);
extern int catcls_update_catalog_classes (THREAD_ENTRY * thread_p, const char *name, RECDES * record, OID * class_oid_p,
					  UPDATE_INPLACE_STYLE force_in_place);
extern int catcls_remove_entry (THREAD_ENTRY * thread_p, OID * class_oid);

typedef struct locator_classname_action LOCATOR_CLASSNAME_ACTION;
struct locator_classname_action
{
  LC_FIND_CLASSNAME action;	/* The transient operation, delete or reserve name */
  OID oid;			/* The class identifier of classname */
  LOG_LSA savep_lsa;		/* A top action LSA address (likely a savepoint) for return NULL is for current */
  LOCATOR_CLASSNAME_ACTION *prev;	/* To previous top action */
};

typedef struct locator_classname_entry LOCATOR_CLASSNAME_ENTRY;
struct locator_classname_entry
{
  char *e_name;			/* Name of the class */
  int e_tran_index;		/* Transaction of entry */
  LOCATOR_CLASSNAME_ACTION e_current;	/* The most current action */
};

typedef struct locator_return_nxobj LOCATOR_RETURN_NXOBJ;
struct locator_return_nxobj
{				/* Location of next object to return in communication (fetch) area */
  LC_COPYAREA *comm_area;	/* Communication area where objects are returned and described */
  LC_COPYAREA_MANYOBJS *mobjs;	/* Location in the communication area where all objects to be returned are described. */
  LC_COPYAREA_ONEOBJ *obj;	/* Location in the communication area where the next object to return is described. */
  HEAP_SCANCACHE *ptr_scancache;	/* Scan cache used for fetching purposes */
  HEAP_SCANCACHE area_scancache;	/* Scan cache used for fetching purposes */
  RECDES recdes;		/* Location in the communication area where the content of the next object to return is 
				 * placed. */
  int area_offset;		/* Relative offset to recdes->data in the communication area */
};

extern INT32 vacuum_Global_oldest_active_blockers_counter;

bool locator_Dont_check_foreign_key = false;

static MHT_TABLE *locator_Mht_classnames = NULL;

static const HFID NULL_HFID = { {-1, -1}, -1 };

/* Pseudo pageid used to generate pseudo OID for reserved class names. */
static const INT32 locator_Pseudo_pageid_first = -2;
static const INT32 locator_Pseudo_pageid_last = -0x7FFF;
static INT32 locator_Pseudo_pageid_crt = -2;

static int locator_permoid_class_name (THREAD_ENTRY * thread_p, const char *classname, const OID * class_oid);
static int locator_defence_drop_class_name_entry (const void *name, void *ent, void *args);
static int locator_force_drop_class_name_entry (const void *name, void *ent, void *args);
static int locator_drop_class_name_entry (THREAD_ENTRY * thread_p, const char *classname, LOG_LSA * savep_lsa);
static int locator_savepoint_class_name_entry (const char *classname, LOG_LSA * savep_lsa);
static int locator_print_class_name (FILE * outfp, const void *key, void *ent, void *args);
static int locator_check_class_on_heap (const void *name, void *ent, void *args);
static SCAN_CODE locator_lock_and_return_object (THREAD_ENTRY * thread_p, LOCATOR_RETURN_NXOBJ * assign,
						 OID * class_oid, OID * oid, int chn, LOCK lock_mode,
						 SCAN_OPERATION_TYPE op_type);
static int locator_find_lockset_missing_class_oids (THREAD_ENTRY * thread_p, LC_LOCKSET * lockset);
static SCAN_CODE locator_return_object_assign (THREAD_ENTRY * thread_p, LOCATOR_RETURN_NXOBJ * assign, OID * class_oid,
					       OID * oid, int chn, int guess_chn, SCAN_CODE scan, int tran_index);
static LC_LOCKSET *locator_all_reference_lockset (THREAD_ENTRY * thread_p, OID * oid, int prune_level, LOCK inst_lock,
						  LOCK class_lock, int quit_on_errors);
static bool locator_notify_decache (const OID * class_oid, const OID * oid, void *notify_area);
static int locator_guess_sub_classes (THREAD_ENTRY * thread_p, LC_LOCKHINT ** lockhint_subclasses);
static int locator_repl_prepare_force (THREAD_ENTRY * thread_p, LC_COPYAREA_ONEOBJ * obj, RECDES * old_recdes,
				       RECDES * recdes, DB_VALUE * key_value, HEAP_SCANCACHE * force_scancache);
static int locator_repl_get_key_value (DB_VALUE * key_value, LC_COPYAREA * force_area, LC_COPYAREA_ONEOBJ * obj);
static void locator_repl_add_error_to_copyarea (LC_COPYAREA ** copy_area, RECDES * recdes, LC_COPYAREA_ONEOBJ * obj,
						DB_VALUE * key_value, int err_code, const char *err_msg);
static int locator_insert_force (THREAD_ENTRY * thread_p, HFID * hfid, OID * class_oid, OID * oid, RECDES * recdes,
				 int has_index, int op_type, HEAP_SCANCACHE * scan_cache, int *force_count,
				 int pruning_type, PRUNING_CONTEXT * pcontext, FUNC_PRED_UNPACK_INFO * func_preds,
				 UPDATE_INPLACE_STYLE force_in_place);
static int locator_update_force (THREAD_ENTRY * thread_p, HFID * hfid, OID * class_oid, OID * oid, RECDES * ikdrecdes,
				 RECDES * recdes, int has_index, ATTR_ID * att_id, int n_att_id, int op_type,
				 HEAP_SCANCACHE * scan_cache, int *force_count, bool not_check_fk,
				 REPL_INFO_TYPE repl_info_type, int pruning_type, PRUNING_CONTEXT * pcontext,
				 MVCC_REEV_DATA * mvcc_reev_data, UPDATE_INPLACE_STYLE force_in_place,
				 bool need_locking);
static int locator_move_record (THREAD_ENTRY * thread_p, HFID * old_hfid, OID * old_class_oid, OID * obj_oid,
				OID * new_class_oid, HFID * new_class_hfid, RECDES * recdes,
				HEAP_SCANCACHE * scan_cache, int op_type, int has_index, int *force_count,
				PRUNING_CONTEXT * context, MVCC_REEV_DATA * mvcc_reev_data, bool need_locking);
static int locator_delete_force_for_moving (THREAD_ENTRY * thread_p, HFID * hfid, OID * oid, int has_index, int op_type,
					    HEAP_SCANCACHE * scan_cache, int *force_count,
					    MVCC_REEV_DATA * mvcc_reev_data, OID * new_obj_oid, OID * partition_oid,
					    bool need_locking);
static int locator_delete_force_internal (THREAD_ENTRY * thread_p, HFID * hfid, OID * oid, int has_index, int op_type,
					  HEAP_SCANCACHE * scan_cache, int *force_count,
					  MVCC_REEV_DATA * mvcc_reev_data, LOCATOR_INDEX_ACTION_FLAG idx_action_flag,
					  OID * new_obj_oid, OID * partition_oid, bool need_locking);
static int locator_force_for_multi_update (THREAD_ENTRY * thread_p, LC_COPYAREA * force_area);

#if defined(ENABLE_UNUSED_FUNCTION)
static void locator_increase_catalog_count (THREAD_ENTRY * thread_p, OID * cls_oid);
static void locator_decrease_catalog_count (THREAD_ENTRY * thread_p, OID * cls_oid);
#endif

static int locator_add_or_remove_index_for_moving (THREAD_ENTRY * thread_p, RECDES * recdes, OID * inst_oid,
						   OID * class_oid, int is_insert, int op_type,
						   HEAP_SCANCACHE * scan_cache, bool datayn, bool need_replication,
						   HFID * hfid, FUNC_PRED_UNPACK_INFO * func_preds);
static int locator_add_or_remove_index_internal (THREAD_ENTRY * thread_p, RECDES * recdes, OID * inst_oid,
						 OID * class_oid, int is_insert, int op_type,
						 HEAP_SCANCACHE * scan_cache, bool datayn, bool replyn, HFID * hfid,
						 FUNC_PRED_UNPACK_INFO * func_preds,
						 LOCATOR_INDEX_ACTION_FLAG idx_action_flag);
static int locator_check_foreign_key (THREAD_ENTRY * thread_p, HFID * hfid, OID * class_oid, OID * inst_oid,
				      RECDES * recdes, RECDES * new_recdes, bool * is_cached, LC_COPYAREA ** copyarea);
static int locator_check_primary_key_delete (THREAD_ENTRY * thread_p, OR_INDEX * index, DB_VALUE * key);
static int locator_check_primary_key_update (THREAD_ENTRY * thread_p, OR_INDEX * index, DB_VALUE * key);
#if defined(ENABLE_UNUSED_FUNCTION)
static TP_DOMAIN *locator_make_midxkey_domain (OR_INDEX * index);
#endif
static DISK_ISVALID locator_check_unique_btree_entries (THREAD_ENTRY * thread_p, BTID * btid, OID * cls_oid,
							RECDES * classrec, ATTR_ID * attr_ids, const char *btname,
							bool repair);
static int locator_eval_filter_predicate (THREAD_ENTRY * thread_p, BTID * btid, OR_PREDICATE * or_pred, OID * class_oid,
					  OID ** inst_oids, int num_insts, RECDES ** recs, DB_LOGICAL * results);
static bool locator_was_index_already_applied (HEAP_CACHE_ATTRINFO * index_attrinfo, BTID * btid, int pos);
static LC_FIND_CLASSNAME xlocator_reserve_class_name (THREAD_ENTRY * thread_p, const char *classname, OID * class_oid);

static int locator_filter_errid (THREAD_ENTRY * thread_p, int num_ignore_error_count, int *ignore_error_list);
static int locator_area_op_to_pruning_type (LC_COPYAREA_OPERATION op);

static int locator_prefetch_index_page (THREAD_ENTRY * thread_p, OID * class_oid, RECDES * classrec, RECDES * recdes,
					int btid_index, HEAP_CACHE_ATTRINFO * attr_info);
static int locator_prefetch_index_page_internal (THREAD_ENTRY * thread_p, BTID * btid, OID * class_oid,
						 RECDES * classrec, RECDES * recdes);

static int locator_check_primary_key_upddel (THREAD_ENTRY * thread_p, OID * class_oid, OID * inst_oid, RECDES * recdes,
					     LOCATOR_INDEX_ACTION_FLAG idx_action_flag);

static void locator_incr_num_transient_classnames (int tran_index);
static void locator_decr_num_transient_classnames (int tran_index);
static int locator_get_num_transient_classnames (int tran_index);
static bool locator_is_exist_class_name_entry (THREAD_ENTRY * thread_p, LOCATOR_CLASSNAME_ENTRY * entry);

static DISK_ISVALID locator_repair_btree_by_delete (THREAD_ENTRY * thread_p, OID * class_oid, BTID * btid,
						    OID * inst_oid);

static void locator_generate_class_pseudo_oid (THREAD_ENTRY * thread_p, OID * class_oid);

static int redistribute_partition_data (THREAD_ENTRY * thread_p, OID * class_oid, int no_oids, OID * oid_list);
static SCAN_CODE locator_lock_and_get_object_internal (THREAD_ENTRY * thread_p, HEAP_GET_CONTEXT * context,
						       LOCK lock_mode);
static DB_LOGICAL locator_mvcc_reev_cond_assigns (THREAD_ENTRY * thread_p, OID * class_oid, const OID * oid,
						  HEAP_SCANCACHE * scan_cache, RECDES * recdes,
						  MVCC_UPDDEL_REEV_DATA * mvcc_reev_data);
static DB_LOGICAL locator_mvcc_reeval_scan_filters (THREAD_ENTRY * thread_p, const OID * oid,
						    HEAP_SCANCACHE * scan_cache, RECDES * recdes,
						    UPDDEL_MVCC_COND_REEVAL * mvcc_cond_reeval, bool is_upddel);
static DB_LOGICAL locator_mvcc_reevaluate_filters (THREAD_ENTRY * thread_p, MVCC_SCAN_REEV_DATA * mvcc_reev_data,
						   const OID * oid, RECDES * recdes);
static DB_LOGICAL locator_mvcc_reev_cond_and_assignment (THREAD_ENTRY * thread_p, HEAP_SCANCACHE * scan_cache,
							 MVCC_REEV_DATA * mvcc_reev_data_p,
							 MVCC_REC_HEADER * mvcc_header_p,
							 const OID * curr_row_version_oid_p, RECDES * recdes);

/*
 * locator_initialize () - Initialize the locator on the server
 *
 * return: NO_ERROR if all OK, ER_ status otherwise
 *
 * Note: Initialize the server transaction object locator.
 *       Currently, only the classname memory hash table is initialized.
 */
int
locator_initialize (THREAD_ENTRY * thread_p)
{
  RECDES peek = RECDES_INITIALIZER;	/* Record descriptor for peeking object */
  HFID root_hfid;
  OID class_oid;
  char *classname = NULL;
  HEAP_SCANCACHE scan_cache;
  LOCATOR_CLASSNAME_ENTRY *entry;

  if (csect_enter (thread_p, CSECT_LOCATOR_SR_CLASSNAME_TABLE, INF_WAIT) != NO_ERROR)
    {
      /* Some kind of failure. We must notify the error to the caller. */
      assert (false);
      return DISK_ERROR;
    }

  if (csect_enter (thread_p, CSECT_CT_OID_TABLE, INF_WAIT) != NO_ERROR)
    {
      assert (false);
      csect_exit (thread_p, CSECT_LOCATOR_SR_CLASSNAME_TABLE);
      return DISK_ERROR;
    }

  if (locator_Mht_classnames != NULL)
    {
      (void) mht_map (locator_Mht_classnames, locator_force_drop_class_name_entry, NULL);
    }
  else
    {
      locator_Mht_classnames =
	mht_create ("Memory hash Classname to OID", CLASSNAME_CACHE_SIZE, mht_1strhash, mht_compare_strings_are_equal);
    }

  if (locator_Mht_classnames == NULL)
    {
      assert (false);
      goto error;
    }

  /* Find every single class */

  if (boot_find_root_heap (&root_hfid) != NO_ERROR || HFID_IS_NULL (&root_hfid))
    {
      goto error;
    }

  if (heap_scancache_start (thread_p, &scan_cache, &root_hfid, NULL, true, false, NULL) != NO_ERROR)
    {
      goto error;
    }

  OID_SET_NULL (&class_oid);

  while (heap_next (thread_p, &root_hfid, NULL, &class_oid, &peek, &scan_cache, PEEK) == S_SUCCESS)
    {
      assert (!OID_ISNULL (&class_oid));

      classname = or_class_name (&peek);
      assert (classname != NULL);
      assert (strlen (classname) < 255);

      entry = ((LOCATOR_CLASSNAME_ENTRY *) malloc (sizeof (*entry)));
      if (entry == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (*entry));
	  goto error;
	}

      entry->e_name = strdup ((char *) classname);
      if (entry->e_name == NULL)
	{
	  free_and_init (entry);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) (strlen (classname) + 1));
	  goto error;
	}

      entry->e_tran_index = NULL_TRAN_INDEX;

      entry->e_current.action = LC_CLASSNAME_EXIST;
      COPY_OID (&entry->e_current.oid, &class_oid);
      LSA_SET_NULL (&entry->e_current.savep_lsa);
      entry->e_current.prev = NULL;

      assert (locator_is_exist_class_name_entry (thread_p, entry));

      (void) mht_put (locator_Mht_classnames, entry->e_name, entry);
    }

  /* End the scan cursor */
  if (heap_scancache_end (thread_p, &scan_cache) != NO_ERROR)
    {
      goto error;
    }

  csect_exit (thread_p, CSECT_CT_OID_TABLE);

  csect_exit (thread_p, CSECT_LOCATOR_SR_CLASSNAME_TABLE);

  return NO_ERROR;

error:

  csect_exit (thread_p, CSECT_CT_OID_TABLE);

  csect_exit (thread_p, CSECT_LOCATOR_SR_CLASSNAME_TABLE);

  return DISK_ERROR;
}

/*
 * locator_finalize () - Terminates the locator on the server
 *
 * return: nothing
 *
 * Note:Terminate the object locator on the server. Currently, only
 *              the classname memory hash table is removed.
 */
void
locator_finalize (THREAD_ENTRY * thread_p)
{
  if (csect_enter (thread_p, CSECT_LOCATOR_SR_CLASSNAME_TABLE, INF_WAIT) != NO_ERROR)
    {
      /* Some kind of failure. We will leak resources. */
      assert (false);
      return;
    }

  if (locator_Mht_classnames == NULL)
    {
      csect_exit (thread_p, CSECT_LOCATOR_SR_CLASSNAME_TABLE);
      return;
    }

  if (csect_enter (thread_p, CSECT_CT_OID_TABLE, INF_WAIT) != NO_ERROR)
    {
      assert (false);
      csect_exit (thread_p, CSECT_LOCATOR_SR_CLASSNAME_TABLE);
      return;
    }

  (void) mht_map (locator_Mht_classnames, locator_force_drop_class_name_entry, NULL);

  mht_destroy (locator_Mht_classnames);
  locator_Mht_classnames = NULL;

  csect_exit (thread_p, CSECT_CT_OID_TABLE);

  csect_exit (thread_p, CSECT_LOCATOR_SR_CLASSNAME_TABLE);
}

/*
 * xlocator_reserve_class_names () - Reserve several classnames
 *
 * return: LC_FIND_CLASSNAME
 *                       (either of LC_CLASSNAME_RESERVED,
 *                                  LC_CLASSNAME_EXIST,
 *                                  LC_CLASSNAME_ERROR)
 *
 *   num_classes(in): Number of classes
 *   classnames(in): Names of the classes
 *   class_oids(in/out): Object identifiers of the classes
 */
LC_FIND_CLASSNAME
xlocator_reserve_class_names (THREAD_ENTRY * thread_p, const int num_classes, const char **classnames, OID * class_oids)
{
  int i = 0;
  LC_FIND_CLASSNAME result = LC_CLASSNAME_RESERVED;

  for (i = 0; i < num_classes; ++i)
    {
      assert (classnames[i] != NULL);
      assert (strlen (classnames[i]) < 255);

      result = xlocator_reserve_class_name (thread_p, classnames[i], &class_oids[i]);
      if (result != LC_CLASSNAME_RESERVED)
	{
	  /* We could potentially revert the reservation but the transient entries should be properly cleaned up by the 
	   * rollback so we don't really need to do this here. */
	  break;
	}
    }

  return result;
}

/*
 * xlocator_reserve_class_name () - Reserve a classname
 *
 * return: LC_FIND_CLASSNAME
 *                       (either of LC_CLASSNAME_RESERVED,
 *                                  LC_CLASSNAME_EXIST,
 *                                  LC_CLASSNAME_ERROR)
 *
 *   classname(in): Name of class
 *   class_oid(in/out): Object identifier of the class
 *
 * Note: Reserve the name of a class.
 *              If there is an entry on the memory classname table,
 *              we can proceed if the entry belongs to the current
 *              transaction, otherwise, we must wait until the transaction
 *              holding the entry terminates since the fate of the classname
 *              entry cannot be predicted. If the entry belongs to
 *              the current transaction, we can reserve the name only if the
 *              entry indicates that a class with such a name has been
 *              deleted or reserved. If there is not a entry with such a name,
 *              the classname is reserved or an error is returned.
 */
static LC_FIND_CLASSNAME
xlocator_reserve_class_name (THREAD_ENTRY * thread_p, const char *classname, OID * class_oid)
{
  LOCATOR_CLASSNAME_ENTRY *entry;
  LOCATOR_CLASSNAME_ACTION *old_action;
  LC_FIND_CLASSNAME reserve = LC_CLASSNAME_RESERVED;
  OID tmp_classoid;
  int tran_index;

  if (classname == NULL)
    {
      return LC_CLASSNAME_ERROR;
    }

  assert (classname != NULL);
  assert (strlen (classname) < 255);

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);

start:
  reserve = LC_CLASSNAME_RESERVED;

  if (csect_enter (thread_p, CSECT_LOCATOR_SR_CLASSNAME_TABLE, INF_WAIT) != NO_ERROR)
    {
      /* Some kind of failure. We must notify the error to the caller. */
      assert (false);
      return LC_CLASSNAME_ERROR;
    }

  /* Is there any entries on the classname hash table ? */
  entry = (LOCATOR_CLASSNAME_ENTRY *) mht_get (locator_Mht_classnames, classname);

  if (locator_is_exist_class_name_entry (thread_p, entry))
    {
      /* There is a class with such a name on the classname cache. */
      reserve = LC_CLASSNAME_EXIST;
    }
  else if (entry != NULL)
    {
      assert (entry->e_current.action != LC_CLASSNAME_EXIST);

      /* 
       * We can only proceed if the entry belongs to the current transaction,
       * otherwise, we must lock the class associated with the classname and
       * retry the operation once the lock is granted.
       */
      if (entry->e_tran_index == tran_index)
	{
	  /* 
	   * The name can be reserved only if it has been deleted or
	   * previously reserved. We allow double reservations in order for
	   * multiple table renaming to properly reserve all the names
	   * involved.
	   */
	  if (entry->e_current.action == LC_CLASSNAME_DELETED || entry->e_current.action == LC_CLASSNAME_DELETED_RENAME
	      || (entry->e_current.action == LC_CLASSNAME_RESERVED && LSA_ISNULL (&entry->e_current.savep_lsa)))
	    {
	      /* 
	       * The entry can be changed.
	       * Do we need to save the old action...just in case we do a
	       * partial rollback ?
	       */
	      if (!LSA_ISNULL (&entry->e_current.savep_lsa))
		{
		  /* 
		   * There is a possibility of returning to this top LSA
		   * (savepoint). Save the action.. just in case
		   */
		  old_action = (LOCATOR_CLASSNAME_ACTION *) malloc (sizeof (*old_action));

		  if (old_action == NULL)
		    {
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (*old_action));
		      csect_exit (thread_p, CSECT_LOCATOR_SR_CLASSNAME_TABLE);
		      return LC_CLASSNAME_ERROR;
		    }

		  *old_action = entry->e_current;
		  LSA_SET_NULL (&entry->e_current.savep_lsa);
		  entry->e_current.prev = old_action;
		}

	      entry->e_current.action = LC_CLASSNAME_RESERVED;
	      if (OID_ISNULL (class_oid))
		{
		  locator_generate_class_pseudo_oid (thread_p, class_oid);
		}
	      COPY_OID (&entry->e_current.oid, class_oid);

	      /* Add dummy log to make sure we can revert transient state change. See comment in
	       * xlocator_rename_class_name. Since reserve has been moved before any change (or flush) is done, same
	       * scenario can happen here. */
	      log_append_redo_data2 (thread_p, RVLOC_CLASSNAME_DUMMY, NULL, NULL, 0, 0, NULL);
	    }
	  else
	    {
	      assert ((entry->e_current.action == LC_CLASSNAME_RESERVED && !LSA_ISNULL (&entry->e_current.savep_lsa))
		      || entry->e_current.action == LC_CLASSNAME_RESERVED_RENAME);

	      reserve = LC_CLASSNAME_EXIST;
	    }
	}
      else
	{
	  COPY_OID (&tmp_classoid, &entry->e_current.oid);

	  /* 
	   * The fate of this entry is known when the transaction holding
	   * this entry either commits or aborts. Get the lock and try again.
	   */

	  /* 
	   * Exit from critical section since we are going to be suspended and
	   * then retry again.
	   */
	  csect_exit (thread_p, CSECT_LOCATOR_SR_CLASSNAME_TABLE);

	  if (lock_object (thread_p, &tmp_classoid, oid_Root_class_oid, SCH_M_LOCK, LK_UNCOND_LOCK) != LK_GRANTED)
	    {
	      /* 
	       * Unable to acquired lock
	       */
	      return LC_CLASSNAME_ERROR;
	    }
	  else
	    {
	      /* 
	       * Try again
	       * Remove the lock.. since the above was a dirty read
	       */
	      lock_unlock_object (thread_p, &tmp_classoid, oid_Root_class_oid, SCH_M_LOCK, true);
	      goto start;
	    }
	}
    }
  else
    {
      entry = (LOCATOR_CLASSNAME_ENTRY *) malloc (sizeof (*entry));
      if (entry == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (*entry));
	  csect_exit (thread_p, CSECT_LOCATOR_SR_CLASSNAME_TABLE);
	  return LC_CLASSNAME_ERROR;
	}

      entry->e_name = strdup ((char *) classname);
      if (entry->e_name == NULL)
	{
	  free_and_init (entry);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) (strlen (classname) + 1));
	  csect_exit (thread_p, CSECT_LOCATOR_SR_CLASSNAME_TABLE);
	  return LC_CLASSNAME_ERROR;
	}

      entry->e_tran_index = tran_index;

      entry->e_current.action = LC_CLASSNAME_RESERVED;
      if (OID_ISNULL (class_oid))
	{
	  locator_generate_class_pseudo_oid (thread_p, class_oid);
	}
      COPY_OID (&entry->e_current.oid, class_oid);
      LSA_SET_NULL (&entry->e_current.savep_lsa);
      entry->e_current.prev = NULL;

      /* Add dummy log to make sure we can revert transient state change. See comment in xlocator_rename_class_name.
       * Since reserve has been moved before any change (or flush) is done, same scenario can happen here. */
      log_append_redo_data2 (thread_p, RVLOC_CLASSNAME_DUMMY, NULL, NULL, 0, 0, NULL);

      assert (locator_is_exist_class_name_entry (thread_p, entry) == false);

      (void) mht_put (locator_Mht_classnames, entry->e_name, entry);

      locator_incr_num_transient_classnames (entry->e_tran_index);
    }

  /* 
   * Note that the index has not been made permanently into the database.
   *      That is, it has not been inserted onto extendible hash.
   */

  csect_exit (thread_p, CSECT_LOCATOR_SR_CLASSNAME_TABLE);

  /* 
   * Get the lock on the class if we were able to reserve the name
   */
  if (reserve == LC_CLASSNAME_RESERVED && entry != NULL)
    {
      assert (entry->e_tran_index == tran_index);

      if (lock_object (thread_p, class_oid, oid_Root_class_oid, SCH_M_LOCK, LK_UNCOND_LOCK) != LK_GRANTED)
	{
	  /* 
	   * Something wrong. Remove the entry from hash table.
	   */
	  if (csect_enter (thread_p, CSECT_LOCATOR_SR_CLASSNAME_TABLE, INF_WAIT) != NO_ERROR)
	    {
	      assert (false);
	      return LC_CLASSNAME_ERROR;
	    }

	  if (entry->e_current.prev == NULL)
	    {
	      locator_decr_num_transient_classnames (entry->e_tran_index);

	      (void) mht_rem (locator_Mht_classnames, entry->e_name, NULL, NULL);

	      free_and_init (entry->e_name);
	      free_and_init (entry);
	    }
	  else
	    {
	      old_action = entry->e_current.prev;
	      entry->e_current = *old_action;
	      free_and_init (old_action);
	    }

	  csect_exit (thread_p, CSECT_LOCATOR_SR_CLASSNAME_TABLE);

	  reserve = LC_CLASSNAME_ERROR;
	}
    }

  return reserve;
}

/*
 * xlocator_get_reserved_class_name_oid () - Is class name reserved by this
 *					transaction?
 *
 * return          : Error code.
 * thread_p (in)   : Thread entry.
 * classname (in)  : Class name.
 * class_oid (out) : Class OID.
 */
int
xlocator_get_reserved_class_name_oid (THREAD_ENTRY * thread_p, const char *classname, OID * class_oid)
{
  int tran_index;
  LOCATOR_CLASSNAME_ENTRY *entry;
  bool is_reserved = false;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);

  if (csect_enter_as_reader (thread_p, CSECT_LOCATOR_SR_CLASSNAME_TABLE, INF_WAIT) != NO_ERROR)
    {
      assert (false);
      return ER_FAILED;
    }
  entry = (LOCATOR_CLASSNAME_ENTRY *) mht_get (locator_Mht_classnames, classname);
  if (entry == NULL)
    {
      assert (false);
      goto error;
    }

  if (entry->e_current.action != LC_CLASSNAME_RESERVED)
    {
      assert (false);
      goto error;
    }

  if (entry->e_tran_index != tran_index)
    {
      assert (false);
      goto error;
    }

  COPY_OID (class_oid, &entry->e_current.oid);

  csect_exit (thread_p, CSECT_LOCATOR_SR_CLASSNAME_TABLE);

  return NO_ERROR;

error:
  csect_exit (thread_p, CSECT_LOCATOR_SR_CLASSNAME_TABLE);
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
  return ER_FAILED;
}

/*
 * xlocator_delete_class_name () - Remove a classname
 *
 * return: LC_FIND_CLASSNAME (either of LC_CLASSNAME_DELETED,
 *                                      LC_CLASSNAME_ERROR)
 *
 *   classname(in): Name of the class to delete
 *
 * Note: Indicate that a classname has been deleted.
 *              A classname to OID entry is created in memory to indicate the
 *              deletion.
 *              If there is an entry on the memory classname table,
 *              we can proceed if the entry belongs to the current
 *              transaction, otherwise, we must wait until the transaction
 *              holding the entry terminates since the fate of the classname
 *              entry cannot be predicted. If the entry belongs to
 *              the current transaction, we can delete the name only if the
 *              entry indicates that a class with such a name has been
 *              reserved. If there is not a entry with such a namek,
 *              the deleted class is locked and a entry is created informing
 *              of the deletion.
 */
LC_FIND_CLASSNAME
xlocator_delete_class_name (THREAD_ENTRY * thread_p, const char *classname)
{
  LOCATOR_CLASSNAME_ENTRY *entry;
  LOCATOR_CLASSNAME_ACTION *old_action;
  LC_FIND_CLASSNAME classname_delete = LC_CLASSNAME_DELETED;
  OID tmp_classoid;
  int tran_index;

  if (classname == NULL)
    {
      return LC_CLASSNAME_ERROR;
    }

  assert (classname != NULL);
  assert (strlen (classname) < 255);

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);

start:
  classname_delete = LC_CLASSNAME_DELETED;

  if (csect_enter (thread_p, CSECT_LOCATOR_SR_CLASSNAME_TABLE, INF_WAIT) != NO_ERROR)
    {
      /* Some kind of failure. We must notify the error to the caller. */
      assert (false);
      return LC_CLASSNAME_ERROR;
    }

  entry = (LOCATOR_CLASSNAME_ENTRY *) mht_get (locator_Mht_classnames, classname);
  if (entry != NULL)
    {
      assert (entry->e_tran_index == NULL_TRAN_INDEX || entry->e_tran_index == tran_index);

      COPY_OID (&tmp_classoid, &entry->e_current.oid);
    }

  if (locator_is_exist_class_name_entry (thread_p, entry))
    {
      /* There is a class with such a name on the classname cache. We should convert it to transient one. */
      entry->e_tran_index = tran_index;
      entry->e_current.action = LC_CLASSNAME_DELETED;

      locator_incr_num_transient_classnames (entry->e_tran_index);
    }
  else if (entry != NULL)
    {
      assert (entry->e_current.action != LC_CLASSNAME_EXIST);
      assert (entry->e_tran_index == tran_index);

      /* 
       * We can only proceed if the entry belongs to the current transaction,
       * otherwise, we must lock the class associated with the classname and
       * retry the operation once the lock is granted.
       */
      if (entry->e_tran_index == tran_index)
	{
	  /* 
	   * The name can be deleted only if it has been reserved by current
	   * transaction
	   */
	  if (entry->e_current.action == LC_CLASSNAME_DELETED || entry->e_current.action == LC_CLASSNAME_DELETED_RENAME)
	    {
	      classname_delete = LC_CLASSNAME_ERROR;
	      goto error;
	    }

	  assert (entry->e_current.action == LC_CLASSNAME_RESERVED
		  || entry->e_current.action == LC_CLASSNAME_RESERVED_RENAME);

	  /* 
	   * The entry can be changed.
	   * Do we need to save the old action...just in case we do a partial
	   * rollback ?
	   */
	  if (!LSA_ISNULL (&entry->e_current.savep_lsa))
	    {
	      /* 
	       * There is a possibility of returning to this top LSA (savepoint).
	       * Save the action.. just in case
	       */
	      old_action = (LOCATOR_CLASSNAME_ACTION *) malloc (sizeof (*old_action));
	      if (old_action == NULL)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (*old_action));
		  classname_delete = LC_CLASSNAME_ERROR;
		  goto error;
		}

	      *old_action = entry->e_current;
	      LSA_SET_NULL (&entry->e_current.savep_lsa);
	      entry->e_current.prev = old_action;
	    }

	  entry->e_current.action = LC_CLASSNAME_DELETED;
	}
      else
	{
	  /* 
	   * Do not know the fate of this entry until the transaction holding
	   * this entry either commits or aborts. Get the lock and try again.
	   */

	  /* 
	   * Exit from critical section since we are going to be suspended and
	   * then retry again.
	   */

	  csect_exit (thread_p, CSECT_LOCATOR_SR_CLASSNAME_TABLE);

	  if (lock_object (thread_p, &tmp_classoid, oid_Root_class_oid, SCH_M_LOCK, LK_UNCOND_LOCK) != LK_GRANTED)
	    {
	      /* 
	       * Unable to acquired lock
	       */
	      return LC_CLASSNAME_ERROR;
	    }
	  else
	    {
	      /* 
	       * Try again
	       * Remove the lock.. since the above was a dirty read
	       */
	      lock_unlock_object (thread_p, &tmp_classoid, oid_Root_class_oid, SCH_M_LOCK, true);
	      goto start;
	    }
	}
    }
  else
    {
      /* Some kind of failure. We must notify the error to the caller. */
      classname_delete = LC_CLASSNAME_ERROR;
    }

error:
  csect_exit (thread_p, CSECT_LOCATOR_SR_CLASSNAME_TABLE);

  /* 
   * We do not need to lock the entry->oid since it has already been locked
   * in exclusive mode when the class was deleted or renamed. Avoid duplicate
   * calls.
   */

  /* Note that the index has not been dropped permanently from the database */
  return classname_delete;
}

/*
 * xlocator_rename_class_name () - Rename a classname
 *
 * return: LC_FIND_CLASSNAME
 *                       (either of LC_CLASSNAME_RESERVED_RENAME,
 *                                  LC_CLASSNAME_EXIST,
 *                                  LC_CLASSNAME_ERROR)
 *
 *   oldname(in): Old name of class
 *   newname(in): New name of class
 *   class_oid(in/out): Object identifier of the class
 *
 * Note: Rename a class in transient form.
 */
LC_FIND_CLASSNAME
xlocator_rename_class_name (THREAD_ENTRY * thread_p, const char *oldname, const char *newname, OID * class_oid)
{
  LOCATOR_CLASSNAME_ENTRY *entry;
  LC_FIND_CLASSNAME renamed;
  int tran_index;

  if (oldname == NULL || newname == NULL)
    {
      return LC_CLASSNAME_ERROR;
    }

  assert (oldname != NULL);
  assert (strlen (oldname) < 255);
  assert (newname != NULL);
  assert (strlen (newname) < 255);
  assert (!OID_ISNULL (class_oid));

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);

  renamed = xlocator_reserve_class_name (thread_p, newname, class_oid);
  if (renamed != LC_CLASSNAME_RESERVED)
    {
      return renamed;
    }

  if (csect_enter (thread_p, CSECT_LOCATOR_SR_CLASSNAME_TABLE, INF_WAIT) != NO_ERROR)
    {
      /* Some kind of failure. We must notify the error to the caller. */
      assert (false);
      return LC_CLASSNAME_ERROR;
    }

  entry = (LOCATOR_CLASSNAME_ENTRY *) mht_get (locator_Mht_classnames, newname);
  if (entry != NULL)
    {
      assert (entry->e_current.action == LC_CLASSNAME_RESERVED);

      entry->e_current.action = LC_CLASSNAME_RESERVED_RENAME;
      renamed = xlocator_delete_class_name (thread_p, oldname);
      entry = (LOCATOR_CLASSNAME_ENTRY *) mht_get (locator_Mht_classnames, oldname);
      if (renamed == LC_CLASSNAME_DELETED && entry != NULL)
	{
	  entry->e_current.action = LC_CLASSNAME_DELETED_RENAME;
	  renamed = LC_CLASSNAME_RESERVED_RENAME;

	  /* We need to add a dummy log here. If rename is the first clause of alter statement, there will be no log
	   * entries between parent system savepoint and next flush. Next flush will start a system operation which
	   * will mark the delete action of old class name with same LSA as parent system savepoint. Therefore, the
	   * delete action is not removed when alter statement is aborted and a new table with the same name may be
	   * created. */
	  log_append_redo_data2 (thread_p, RVLOC_CLASSNAME_DUMMY, NULL, NULL, 0, 0, NULL);
	}
      else
	{
	  entry = ((LOCATOR_CLASSNAME_ENTRY *) mht_get (locator_Mht_classnames, newname));
	  if (entry == NULL)
	    {
	      renamed = LC_CLASSNAME_ERROR;
	      goto error;
	    }

	  assert (locator_is_exist_class_name_entry (thread_p, entry) == false);
	  assert (entry->e_tran_index == tran_index);

	  if (csect_enter (thread_p, CSECT_CT_OID_TABLE, INF_WAIT) != NO_ERROR)
	    {
	      assert (false);
	      renamed = LC_CLASSNAME_ERROR;
	      goto error;
	    }

	  if (locator_drop_class_name_entry (thread_p, newname, NULL) != NO_ERROR)
	    {
	      csect_exit (thread_p, CSECT_CT_OID_TABLE);
	      renamed = LC_CLASSNAME_ERROR;
	      goto error;
	    }

	  csect_exit (thread_p, CSECT_CT_OID_TABLE);
	}
    }

error:

  csect_exit (thread_p, CSECT_LOCATOR_SR_CLASSNAME_TABLE);

  return renamed;
}

/*
 * xlocator_find_class_oid () - Find oid of a classname
 *
 * return: LC_FIND_CLASSNAME
 *                       (either of LC_CLASSNAME_EXIST,
 *                                  LC_CLASSNAME_DELETED,
 *                                  LC_CLASSNAME_ERROR)
 *
 *   classname(in): Name of class to find
 *   class_oid(out): Set as a side effect
 *   lock(in): Lock to acquire for the class
 *
 * Note: Find the class identifier of the given class name and lock the
 *              class with the given mode.
 *              If there is an entry on the memory classname table,
 *              we can proceed if the entry belongs to the current
 *              transaction, otherwise, we must wait until the transaction
 *              holding the entry terminates since the fate of the classname
 *              entry cannot be predicted.
 */
LC_FIND_CLASSNAME
xlocator_find_class_oid (THREAD_ENTRY * thread_p, const char *classname, OID * class_oid, LOCK lock)
{
  int tran_index;
  LOCATOR_CLASSNAME_ENTRY *entry;
  LOCK tmp_lock;
  LC_FIND_CLASSNAME find = LC_CLASSNAME_EXIST;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);

start:
  find = LC_CLASSNAME_EXIST;

  if (csect_enter_as_reader (thread_p, CSECT_LOCATOR_SR_CLASSNAME_TABLE, INF_WAIT) != NO_ERROR)
    {
      assert (false);
      return LC_CLASSNAME_ERROR;
    }

  entry = (LOCATOR_CLASSNAME_ENTRY *) mht_get (locator_Mht_classnames, classname);

  if (entry != NULL)
    {
      COPY_OID (class_oid, &entry->e_current.oid);
      assert (find == LC_CLASSNAME_EXIST);
    }

  if (locator_is_exist_class_name_entry (thread_p, entry))
    {
      assert (find == LC_CLASSNAME_EXIST);	/* OK, go ahead */
    }
  else if (entry != NULL)
    {
      assert (entry->e_current.action != LC_CLASSNAME_EXIST);

      /* 
       * We can only proceed if the entry belongs to the current transaction,
       * otherwise, we must lock the class associated with the classname and
       * retry the operation once the lock is granted.
       */
      assert (find == LC_CLASSNAME_EXIST);

      if (entry->e_tran_index == tran_index)
	{
	  if (entry->e_current.action == LC_CLASSNAME_DELETED || entry->e_current.action == LC_CLASSNAME_DELETED_RENAME)
	    {
	      OID_SET_NULL (class_oid);	/* clear */
	      find = LC_CLASSNAME_DELETED;
	    }
	  else
	    {
	      assert (find == LC_CLASSNAME_EXIST);	/* OK, go ahead */
	    }
	}
      else
	{
	  /* 
	   * Do not know the fate of this entry until the transaction is
	   * committed or aborted. Get the lock and try again.
	   */
	  csect_exit (thread_p, CSECT_LOCATOR_SR_CLASSNAME_TABLE);

	  if (lock != NULL_LOCK)
	    {
	      tmp_lock = lock;
	    }
	  else
	    {
	      tmp_lock = IS_LOCK;
	    }

	  if (lock_object (thread_p, class_oid, oid_Root_class_oid, tmp_lock, LK_UNCOND_LOCK) != LK_GRANTED)
	    {
	      /* 
	       * Unable to acquired lock
	       */
	      OID_SET_NULL (class_oid);	/* clear */
	      return LC_CLASSNAME_ERROR;
	    }
	  else
	    {
	      /* 
	       * Try again
	       * Remove the lock.. since the above was a dirty read
	       */
	      lock_unlock_object (thread_p, class_oid, oid_Root_class_oid, tmp_lock, true);
	      goto start;
	    }
	}
    }
  else
    {
      find = LC_CLASSNAME_DELETED;
    }

  csect_exit (thread_p, CSECT_LOCATOR_SR_CLASSNAME_TABLE);

  if (lock != NULL_LOCK && find == LC_CLASSNAME_EXIST)
    {
      /* Now acquired the desired lock */
      if (lock_object (thread_p, class_oid, oid_Root_class_oid, lock, LK_UNCOND_LOCK) != LK_GRANTED)
	{
	  /* 
	   * Unable to acquired lock
	   */
	  OID_SET_NULL (class_oid);	/* clear */
	  find = LC_CLASSNAME_ERROR;
	}
      else
	{
	  lock_unlock_object (thread_p, class_oid, oid_Root_class_oid, lock, false);
	}
    }

#if !defined(NDEBUG)
  if (find == LC_CLASSNAME_EXIST)
    {
      assert (!OID_ISNULL (class_oid));
    }
  else if (find == LC_CLASSNAME_ERROR)
    {
      assert (OID_ISNULL (class_oid));
    }
#endif

  return find;
}

/*
 * locator_permoid_class_name () - Change reserve name with permanent oid
 *
 * return: NO_ERROR if all OK, ER_ status otherwise
 *
 *   classname(in): Name of class
 *   class_oid(in):  New OID
 *
 * Note: Update the entry for the given classname with the
 *              given class identifier. The entry must belong to the
 *              current transaction.
 */
static int
locator_permoid_class_name (THREAD_ENTRY * thread_p, const char *classname, const OID * class_oid)
{
  LOCATOR_CLASSNAME_ENTRY *entry;
  LOCATOR_CLASSNAME_ACTION *old_action;
  int error_code = NO_ERROR;

  /* Is there any entries on the classname hash table ? */
  if (csect_enter (thread_p, CSECT_LOCATOR_SR_CLASSNAME_TABLE, INF_WAIT) != NO_ERROR)
    {
      assert (false);
      return ER_FAILED;
    }

  entry = (LOCATOR_CLASSNAME_ENTRY *) mht_get (locator_Mht_classnames, classname);
  if (entry == NULL || entry->e_tran_index != LOG_FIND_THREAD_TRAN_INDEX (thread_p))
    {
      assert (false);
      error_code = ER_FAILED;
      goto error;		/* should be impossible */
    }

  assert (locator_is_exist_class_name_entry (thread_p, entry) == false);
  assert (entry->e_tran_index == LOG_FIND_THREAD_TRAN_INDEX (thread_p));

  if (entry->e_current.action != LC_CLASSNAME_EXIST)
    {
      /* 
       * Remove the old lock entry. The new entry has already been acquired by
       * the caller
       */
      lock_unlock_object (thread_p, &entry->e_current.oid, oid_Root_class_oid, X_LOCK, true);

      /* 
       * Do we need to save the old action...just in case we do a partial
       * rollback ?
       */
      if (!LSA_ISNULL (&entry->e_current.savep_lsa))
	{
	  /* 
	   * There is a possibility of returning to this top LSA (savepoint).
	   * Save the action.. just in case
	   */
	  old_action = (LOCATOR_CLASSNAME_ACTION *) malloc (sizeof (*old_action));
	  if (old_action == NULL)
	    {
	      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 1, sizeof (*old_action));
	      goto error;
	    }

	  *old_action = entry->e_current;
	  LSA_SET_NULL (&entry->e_current.savep_lsa);
	  entry->e_current.prev = old_action;
	}

      COPY_OID (&entry->e_current.oid, class_oid);
    }

  csect_exit (thread_p, CSECT_LOCATOR_SR_CLASSNAME_TABLE);

  assert (error_code == NO_ERROR);

  return NO_ERROR;

error:

  csect_exit (thread_p, CSECT_LOCATOR_SR_CLASSNAME_TABLE);

  assert (error_code != NO_ERROR);

  return error_code;
}

/*
 * locator_drop_transient_class_name_entries () - Drop transient classname entries
 *
 * return: NO_ERROR if all OK, ER_ status otherwise
 *
 *   savep_lsa(in): up to given LSA
 *
 * Note: Remove all the classname transient entries of the given
 *              transaction up to the given savepoint.
 *              This is done when the transaction terminates.
 */
int
locator_drop_transient_class_name_entries (THREAD_ENTRY * thread_p, LOG_LSA * savep_lsa)
{
  MODIFIED_CLASS_ENTRY *t;
  int tran_index;
  LOG_TDES *tdes;		/* Transaction descriptor */
  int error_code = NO_ERROR;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);

  if (tran_index != NULL_TRAN_INDEX)
    {
      if (locator_get_num_transient_classnames (tran_index) <= 0)
	{
	  return NO_ERROR;
	}
    }

  tdes = LOG_FIND_TDES (tran_index);

  if (csect_enter (thread_p, CSECT_LOCATOR_SR_CLASSNAME_TABLE, INF_WAIT) != NO_ERROR)
    {
      /* Some kind of failure. We must notify the error to the caller. */
      assert (false);
      return ER_FAILED;
    }

  if (csect_enter (thread_p, CSECT_CT_OID_TABLE, INF_WAIT) != NO_ERROR)
    {
      assert (false);
      csect_exit (thread_p, CSECT_LOCATOR_SR_CLASSNAME_TABLE);
      return ER_FAILED;
    }

  for (t = tdes->modified_class_list; t != NULL; t = t->m_next)
    {
      assert (t->m_classname != NULL);
      error_code = locator_drop_class_name_entry (thread_p, t->m_classname, savep_lsa);
      if (error_code != NO_ERROR)
	{
	  assert (false);	/* is impossible */
	  break;
	}
    }
  assert (locator_get_num_transient_classnames (tran_index) >= 0);

  /* defence for commit or rollback; include partial rollback */
  if (tran_index != NULL_TRAN_INDEX)
    {
      if (locator_get_num_transient_classnames (tran_index) > 0)
	{
	  (void) mht_map (locator_Mht_classnames, locator_defence_drop_class_name_entry, savep_lsa);
	}
    }

  if (error_code == NO_ERROR)
    {
      /* defence for commit or rollback; exclude partial rollback */
      if (savep_lsa == NULL)
	{
	  if (locator_get_num_transient_classnames (tran_index) != 0)
	    {
	      assert (false);	/* something wrong */
	      error_code = ER_FAILED;
	    }
	}
    }

  csect_exit (thread_p, CSECT_CT_OID_TABLE);

  csect_exit (thread_p, CSECT_LOCATOR_SR_CLASSNAME_TABLE);

  return error_code;
}

/*
 * locator_drop_class_name_entry () - Remove one transient entry
 *
 * return: NO_ERROR or error code
 *
 *   thread_p(in):
 *   classname(in): The classname (key)
 *   savep_lsa(in): up to given LSA
 *
 * Note: Remove transient entry if it belongs to current transaction.
 */
static int
locator_drop_class_name_entry (THREAD_ENTRY * thread_p, const char *classname, LOG_LSA * savep_lsa)
{
  int tran_index;
  LOG_TDES *tdes;		/* Transaction descriptor */
  LOCATOR_CLASSNAME_ENTRY *entry;
  LOCATOR_CLASSNAME_ACTION *old_action;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);

  tdes = LOG_FIND_TDES (tran_index);

  assert (csect_check_own (thread_p, CSECT_CT_OID_TABLE) == 1);
  assert (csect_check_own (thread_p, CSECT_LOCATOR_SR_CLASSNAME_TABLE) == 1);

  entry = (LOCATOR_CLASSNAME_ENTRY *) mht_get (locator_Mht_classnames, classname);
  if (entry == NULL)
    {
      /* table is dropped by myself; not exist */
      return NO_ERROR;		/* do nothing */
    }

  if (!(entry->e_tran_index == NULL_TRAN_INDEX || entry->e_tran_index == tran_index))
    {
      /* table is dropped by myself and reserved by other tranx */
      return NO_ERROR;
    }

  assert (entry->e_name != NULL);
  assert (!OID_ISNULL (&entry->e_current.oid));

#if !defined(NDEBUG)
  if (entry->e_current.action == LC_CLASSNAME_EXIST)
    {
      assert (locator_is_exist_class_name_entry (thread_p, entry));
    }
#endif

  assert (savep_lsa == NULL || !LSA_ISNULL (savep_lsa));

  if (locator_is_exist_class_name_entry (thread_p, entry) == false)
    {
      assert (entry->e_tran_index == tran_index);

      if (savep_lsa == NULL && tdes->state != TRAN_UNACTIVE_ABORTED)
	{
	  /* is commit; get current */
	  while (entry->e_current.prev != NULL)
	    {
	      old_action = entry->e_current.prev;
	      entry->e_current.prev = old_action->prev;
	      free_and_init (old_action);
	    }
	  assert (entry->e_current.prev == NULL);

	  locator_decr_num_transient_classnames (entry->e_tran_index);

	  if (entry->e_current.action == LC_CLASSNAME_RESERVED
	      || entry->e_current.action == LC_CLASSNAME_RESERVED_RENAME)
	    {
	      entry->e_tran_index = NULL_TRAN_INDEX;

	      entry->e_current.action = LC_CLASSNAME_EXIST;
	      LSA_SET_NULL (&entry->e_current.savep_lsa);

	      assert (locator_is_exist_class_name_entry (thread_p, entry));
	    }
	  else
	    {
	      assert (entry->e_current.action == LC_CLASSNAME_DELETED
		      || entry->e_current.action == LC_CLASSNAME_DELETED_RENAME);

	      (void) locator_force_drop_class_name_entry (entry->e_name, entry, NULL);
	      entry = NULL;	/* clear */
	    }
	}
      else
	{
	  /* is rollback or partial rollback; get prev */
	  while (savep_lsa == NULL	/* is rollback */
		 || LSA_ISNULL (&entry->e_current.savep_lsa) || LSA_LT (savep_lsa, &entry->e_current.savep_lsa))
	    {
	      if (entry->e_current.prev == NULL)
		{
		  break;
		}

	      old_action = entry->e_current.prev;
	      entry->e_current = *old_action;
	      free_and_init (old_action);
	    }

	  if (savep_lsa == NULL	/* is rollback */
	      || LSA_ISNULL (&entry->e_current.savep_lsa) || LSA_LT (savep_lsa, &entry->e_current.savep_lsa))
	    {
	      assert (entry->e_current.prev == NULL);

	      locator_decr_num_transient_classnames (entry->e_tran_index);

	      if (entry->e_current.action == LC_CLASSNAME_DELETED
		  || entry->e_current.action == LC_CLASSNAME_DELETED_RENAME)
		{
		  entry->e_tran_index = NULL_TRAN_INDEX;

		  entry->e_current.action = LC_CLASSNAME_EXIST;
		  LSA_SET_NULL (&entry->e_current.savep_lsa);

		  assert (locator_is_exist_class_name_entry (thread_p, entry));
		}
	      else
		{
		  assert (entry->e_current.action == LC_CLASSNAME_RESERVED
			  || entry->e_current.action == LC_CLASSNAME_RESERVED_RENAME);

		  (void) locator_force_drop_class_name_entry (entry->e_name, entry, NULL);
		  entry = NULL;	/* clear */
		}
	    }
	}			/* else */
    }

#if !defined(NDEBUG)
  /* check iff permanent entry */
  if (locator_is_exist_class_name_entry (thread_p, entry))
    {
      OID class_oid;

      COPY_OID (&class_oid, &entry->e_current.oid);

      if (class_oid.slotid <= 0 || class_oid.volid < 0 || class_oid.pageid < 0)
	{
	  assert (false);
	}

      if (disk_is_page_sector_reserved_with_debug_crash (thread_p, class_oid.volid, class_oid.pageid, true)
	  != DISK_VALID)
	{
	  assert (false);
	}
    }
#endif

  return NO_ERROR;
}

/*
 * locator_defence_drop_class_name_entry () - Remove one transient entry
 *
 * return: NO_ERROR or error code
 *
 *   name(in):
 *   ent(in):
 *   args(in):
 *
 * Note: Remove transient entry if it belongs to current transaction.
 */
static int
locator_defence_drop_class_name_entry (const void *name, void *ent, void *args)
{
  LOCATOR_CLASSNAME_ENTRY *entry = (LOCATOR_CLASSNAME_ENTRY *) ent;
  LOG_LSA *savep_lsa = (LOG_LSA *) args;
  THREAD_ENTRY *thread_p;
  int tran_index;

  thread_p = thread_get_thread_entry_info ();

  assert (csect_check_own (thread_p, CSECT_CT_OID_TABLE) == 1);
  assert (csect_check_own (thread_p, CSECT_LOCATOR_SR_CLASSNAME_TABLE) == 1);

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  assert (tran_index != NULL_TRAN_INDEX);

#if !defined(NDEBUG)
  if (locator_is_exist_class_name_entry (thread_p, entry))
    {
      OID class_oid;

      COPY_OID (&class_oid, &entry->e_current.oid);

      assert (heap_does_exist (thread_p, oid_Root_class_oid, &class_oid));

      /* check class_oid */
      if (class_oid.slotid <= 0 || class_oid.volid < 0 || class_oid.pageid < 0)
	{
	  assert (false);
	}

      if (disk_is_page_sector_reserved_with_debug_crash (thread_p, class_oid.volid, class_oid.pageid, true)
	  != DISK_VALID)
	{
	  assert (false);
	}
    }
#endif

  /* check iff uncleared entry */
  if (entry->e_tran_index == tran_index)
    {
      assert (entry->e_tran_index != NULL_TRAN_INDEX);

      (void) locator_drop_class_name_entry (thread_p, entry->e_name, savep_lsa);
    }

  return NO_ERROR;
}

/*
 * locator_force_drop_class_name_entry () -
 *
 * return:
 *
 *   name(in):
 *   ent(in):
 *   args(in): Not used
 */
static int
locator_force_drop_class_name_entry (const void *name, void *ent, void *args)
{
  THREAD_ENTRY *thread_p;
  LOCATOR_CLASSNAME_ENTRY *entry = (LOCATOR_CLASSNAME_ENTRY *) ent;
  LOCATOR_CLASSNAME_ACTION *old_action;
  OID class_oid;

  thread_p = thread_get_thread_entry_info ();

  assert (csect_check_own (thread_p, CSECT_CT_OID_TABLE) == 1);
  assert (csect_check_own (thread_p, CSECT_LOCATOR_SR_CLASSNAME_TABLE) == 1);

  COPY_OID (&class_oid, &entry->e_current.oid);

  while (entry->e_current.prev != NULL)
    {
      old_action = entry->e_current.prev;
      entry->e_current = *old_action;
      free_and_init (old_action);
    }

  (void) mht_rem (locator_Mht_classnames, name, NULL, NULL);

  free_and_init (entry->e_name);
  free_and_init (entry);

  (void) catcls_remove_entry (thread_p, &class_oid);

  return NO_ERROR;
}

/*
 * locator_savepoint_transient_class_name_entries () - Reset savepoint of
 *                                                     classname entries
 *
 * return: NO_ERROR if all OK, ER_ status otherwise
 *
 *   savep_lsa(in): LSA of a possible rollback point
 *
 * Note: Reset the classname entries of the current
 *              transaction with the given LSA address. This is done when a
 *              new savepoint is taken or top system operations is started
 */
int
locator_savepoint_transient_class_name_entries (THREAD_ENTRY * thread_p, LOG_LSA * savep_lsa)
{
  MODIFIED_CLASS_ENTRY *t;
  int tran_index;
  LOG_TDES *tdes;		/* Transaction descriptor */
  int error_code = NO_ERROR;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);

  if (tran_index != NULL_TRAN_INDEX)
    {
      if (locator_get_num_transient_classnames (tran_index) <= 0)
	{
	  return NO_ERROR;
	}
    }

  tdes = LOG_FIND_TDES (tran_index);

  if (tdes->modified_class_list == NULL)
    {
      /* We may have new transient classnames or dropping classnames whose heap files have not yet been updated; Then,
       * Those classnames have not been added to the modifed list Refer locator_defence_drop_class_name_entry () */
      return NO_ERROR;		/* do nothing */
    }

  if (csect_enter (thread_p, CSECT_LOCATOR_SR_CLASSNAME_TABLE, INF_WAIT) != NO_ERROR)
    {
      /* Some kind of failure. We must notify the error to the caller. */
      assert (false);
      return ER_FAILED;
    }

  for (t = tdes->modified_class_list; t != NULL; t = t->m_next)
    {
      assert (t->m_classname != NULL);
      error_code = locator_savepoint_class_name_entry (t->m_classname, savep_lsa);
      if (error_code != NO_ERROR)
	{
	  assert (false);	/* is impossible */
	  break;
	}
    }

  csect_exit (thread_p, CSECT_LOCATOR_SR_CLASSNAME_TABLE);

  return error_code;
}

/*
 * locator_savepoint_class_name_entry () - Savepoint one transient entry
 *
 * return: NO_ERROR
 *
 *   classname(in):  The classname
 *   savep_lsa(in): LSA of a possible rollback point
 *
 * Note: Savepoint a transient entry if it belongs to current
 *              transaction and it does not have a savepoint as the last
 *              modified point.
 */
static int
locator_savepoint_class_name_entry (const char *classname, LOG_LSA * savep_lsa)
{
  THREAD_ENTRY *thread_p;
  int tran_index;
  LOCATOR_CLASSNAME_ENTRY *entry;

  thread_p = thread_get_thread_entry_info ();

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);

  assert (csect_check_own (thread_p, CSECT_LOCATOR_SR_CLASSNAME_TABLE) == 1);

  entry = (LOCATOR_CLASSNAME_ENTRY *) mht_get (locator_Mht_classnames, classname);
  if (entry == NULL)
    {
      /* table is dropped by myself; not exist */
      return NO_ERROR;		/* do nothing */
    }

  if (!(entry->e_tran_index == NULL_TRAN_INDEX || entry->e_tran_index == tran_index))
    {
      /* table is dropped by myself and reserved by other tranx */
      return NO_ERROR;		/* do nothing */
    }

  assert (entry->e_name != NULL);
  assert (!OID_ISNULL (&entry->e_current.oid));

  /* check iff need to savepoint */
  if (locator_is_exist_class_name_entry (thread_p, entry) == false)
    {
      assert (entry->e_tran_index == tran_index);

      if (LSA_ISNULL (&entry->e_current.savep_lsa))
	{
	  LSA_COPY (&entry->e_current.savep_lsa, savep_lsa);
	}
    }

  return NO_ERROR;
}

/*
 * locator_print_class_name () - Print an entry of classname memory hash table
 *
 * return: always return true
 *
 *   outfp(in): FILE stream where to dump the entry
 *   key(in): Classname
 *   ent(in): The entry associated with classname
 *   args(in): class No
 *
 * Note:Print an entry of the classname memory hash table.
 */
static int
locator_print_class_name (FILE * outfp, const void *key, void *ent, void *args)
{
  LOCATOR_CLASSNAME_ENTRY *entry = (LOCATOR_CLASSNAME_ENTRY *) ent;
  int *class_no_p = (int *) args;
  LOCATOR_CLASSNAME_ACTION *action;
  const char *str_action;
  int key_size, i;

  assert (class_no_p != NULL);

  fprintf (outfp, "   %2d   %s   ", *class_no_p, (char *) key);
  (*class_no_p)++;
  key_size = strlen ((char *) key);
  for (i = 0; i < (29 - key_size); i++)
    {
      fprintf (outfp, " ");
    }
  fprintf (outfp, "TRAN_INDEX = %d\n", entry->e_tran_index);

  action = &entry->e_current;
  while (action != NULL)
    {
      switch (action->action)
	{
	case LC_CLASSNAME_RESERVED:
	  str_action = "LC_CLASSNAME_RESERVE";
	  break;
	case LC_CLASSNAME_DELETED:
	  str_action = "LC_CLASSNAME_DELETED";
	  break;
	case LC_CLASSNAME_EXIST:
	  str_action = "LC_CLASSNAME_EXIST";
	  break;
	case LC_CLASSNAME_RESERVED_RENAME:
	  str_action = "LC_CLASSNAME_RESERVED_RENAME";
	  break;
	case LC_CLASSNAME_DELETED_RENAME:
	  str_action = "LC_CLASSNAME_DELETED_RENAME";
	  break;
	default:
	  assert (action->action == LC_CLASSNAME_ERROR);
	  str_action = "UNKNOWN";
	  break;
	}
      fprintf (outfp, "           action = %s, OID = %d|%d|%d, Save_Lsa = %lld|%d\n", str_action, action->oid.volid,
	       action->oid.pageid, action->oid.slotid, (long long int) action->savep_lsa.pageid,
	       action->savep_lsa.offset);
      action = action->prev;
    }

  return (true);
}

/*
 * locator_dump_class_names () - Dump all classname entries
 *
 * return:
 *
 *    out_fp(in): output file
 *
 * Note:Dump all names of classes and their corresponding OIDs.
 *              This function is used for debugging purposes.
 */
void
locator_dump_class_names (THREAD_ENTRY * thread_p, FILE * out_fp)
{
  int class_no;

#if defined(NDEBUG)		/* skip at debug build */
  if (csect_enter_as_reader (thread_p, CSECT_LOCATOR_SR_CLASSNAME_TABLE, INF_WAIT) != NO_ERROR)
    {
      assert (false);
      return;
    }
#endif

  class_no = 1;			/* init */
  (void) mht_dump (out_fp, locator_Mht_classnames, false, locator_print_class_name, &class_no);

#if defined(NDEBUG)		/* skip at debug build */
  csect_exit (thread_p, CSECT_LOCATOR_SR_CLASSNAME_TABLE);
#endif
}

/*
 * locator_generate_class_pseudo_oid () - Generate a pseudo-OID to be used
 *					  by reserved class name until a
 *					  permanent one is provided.
 *
 * return : 
 * thread_p (in) :
 * class_oid (in) :
 */
static void
locator_generate_class_pseudo_oid (THREAD_ENTRY * thread_p, OID * class_oid)
{
  assert (csect_check_own (thread_p, CSECT_LOCATOR_SR_CLASSNAME_TABLE) == 1);

  class_oid->volid = -2;
  class_oid->slotid = -2;
  class_oid->pageid = locator_Pseudo_pageid_crt;

  if (locator_Pseudo_pageid_crt == locator_Pseudo_pageid_last)
    {
      locator_Pseudo_pageid_crt = locator_Pseudo_pageid_first;
    }
  else
    {
      locator_Pseudo_pageid_crt--;
    }
}

/*
 * locator_check_class_on_heap () - Check the classname on the heap object
 *
 * return: NO_ERROR continue checking, error code stop checking, bad error
 *
 *   name(in): The expected class name
 *   ent(in):
 *   args(out): Could be set as a side effect to either: DISK_INVALID,
 *              DISK_ERROR when an inconsistency is found. Otherwise, it is
 *              left in touch. The caller should initialize it to DISK_VALID
 *
 * Note: Check if the classname of the class associated with ent
 *       is the same as the given class name.
 *       If class does not exist, or its name is different from the
 *       given one, isvalid is set to DISK_INVALID. In the case of other
 *       kind of error, isvalid is set to DISK_ERROR.
 *       If isvalid is set to DISK_ERROR, we return false to stop
 *       the map hash, otherwise, we return true to continue.
 */
static int
locator_check_class_on_heap (const void *name, void *ent, void *args)
{
  THREAD_ENTRY *thread_p;
  LOCATOR_CLASSNAME_ENTRY *entry = (LOCATOR_CLASSNAME_ENTRY *) ent;
  DISK_ISVALID *isvalid = (DISK_ISVALID *) args;
  const char *classname;
  char *heap_classname;
  OID *class_oid;

  thread_p = thread_get_thread_entry_info ();

  assert (csect_check_own (thread_p, CSECT_LOCATOR_SR_CLASSNAME_TABLE) == 1);

  if (entry->e_current.action != LC_CLASSNAME_EXIST)
    {
      /* is not permanent classname */
      return NO_ERROR;
    }

  classname = (char *) name;
  class_oid = &entry->e_current.oid;

  heap_classname = heap_get_class_name_alloc_if_diff (thread_p, class_oid, (char *) classname);
  if (heap_classname == NULL)
    {
      if (er_errid () == ER_HEAP_UNKNOWN_OBJECT)
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LC_INCONSISTENT_CLASSNAME_TYPE4, 4, classname,
		  class_oid->volid, class_oid->pageid, class_oid->slotid);
	  *isvalid = DISK_INVALID;
	}
      else
	{
	  *isvalid = DISK_ERROR;
	}

      goto error;
    }

  /* Compare the classname pointers. If the same pointers classes are the same since the class was no malloc */
  if (heap_classname != classname)
    {
      /* Different names */
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LC_INCONSISTENT_CLASSNAME_TYPE1, 5, class_oid->volid,
	      class_oid->pageid, class_oid->slotid, classname, heap_classname);
      *isvalid = DISK_INVALID;
      free_and_init (heap_classname);
    }

error:
  if (*isvalid == DISK_ERROR)
    {
      return ER_FAILED;
    }
  else
    {
      return NO_ERROR;
    }
}

/*
 * locator_check_class_names () - Check classname consistency
 *
 * return: DISK_ISVALID
 *
 * Note: Check the consistency of the classname_to_oid and the heap of
 *              classes and vice versa.
 */
DISK_ISVALID
locator_check_class_names (THREAD_ENTRY * thread_p)
{
  DISK_ISVALID isvalid;
  RECDES peek = RECDES_INITIALIZER;	/* Record descriptor for peeking object */
  HFID root_hfid;
  OID class_oid;
  char *classname = NULL;
  HEAP_SCANCACHE scan_cache;
  MVCC_SNAPSHOT *mvcc_snapshot = NULL;
  LOCATOR_CLASSNAME_ENTRY *entry;

  mvcc_snapshot = logtb_get_mvcc_snapshot (thread_p);
  if (mvcc_snapshot == NULL)
    {
      return DISK_ERROR;
    }

  if (csect_enter (thread_p, CSECT_LOCATOR_SR_CLASSNAME_TABLE, INF_WAIT) != NO_ERROR)
    {
      /* Some kind of failure. We must notify the error to the caller. */
      assert (false);
      return DISK_ERROR;
    }

  /* 
   * CHECK 1: Each class that is found by scanning the heap of classes, must
   *          be part of the permanent classname_to_oid table.
   */

  /* Find the heap for the classes */

  /* Find every single class */

  if (boot_find_root_heap (&root_hfid) != NO_ERROR || HFID_IS_NULL (&root_hfid))
    {
      goto error;
    }

  if (heap_scancache_start (thread_p, &scan_cache, &root_hfid, oid_Root_class_oid, true, false, mvcc_snapshot) !=
      NO_ERROR)
    {
      goto error;
    }

  class_oid.volid = root_hfid.vfid.volid;
  class_oid.pageid = NULL_PAGEID;
  class_oid.slotid = NULL_SLOTID;

  isvalid = DISK_VALID;
  while (heap_next (thread_p, &root_hfid, oid_Root_class_oid, &class_oid, &peek, &scan_cache, PEEK) == S_SUCCESS)
    {
      classname = or_class_name (&peek);
      assert (classname != NULL);
      assert (strlen (classname) < 255);

      /* 
       * Make sure that this class exists in classname_to_OID table and that
       * the OIDS matches
       */
      entry = (LOCATOR_CLASSNAME_ENTRY *) mht_get (locator_Mht_classnames, classname);
      if (entry == NULL)
	{
	  isvalid = DISK_INVALID;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LC_INCONSISTENT_CLASSNAME_TYPE3, 4, classname, class_oid.volid,
		  class_oid.pageid, class_oid.slotid);
	}
      else
	{
	  /* Are OIDs the same ? */
	  if (!OID_EQ (&class_oid, &entry->e_current.oid))
	    {
	      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LC_INCONSISTENT_CLASSNAME_TYPE2, 7, classname,
		      entry->e_current.oid.volid, entry->e_current.oid.pageid, entry->e_current.oid.slotid,
		      class_oid.volid, class_oid.pageid, class_oid.slotid);
	      isvalid = DISK_INVALID;
	    }
	}
    }				/* while (...) */

  /* End the scan cursor */
  if (heap_scancache_end (thread_p, &scan_cache) != NO_ERROR)
    {
      isvalid = DISK_ERROR;
    }

  /* 
   * CHECK 2: Same that check1 but from classname_to_OID to existance of class
   */
  (void) mht_map (locator_Mht_classnames, locator_check_class_on_heap, &isvalid);

  csect_exit (thread_p, CSECT_LOCATOR_SR_CLASSNAME_TABLE);

  return isvalid;

error:
  csect_exit (thread_p, CSECT_LOCATOR_SR_CLASSNAME_TABLE);

  return DISK_ERROR;
}

/*
 * Functions related to fetching and flushing
 */

/*
 * xlocator_assign_oid () - Assign a permanent oid
 *
 * return: NO_ERROR if all OK, ER_ status otherwise
 *
 *   hfid(in): Heap where the object will be stored
 *   perm_oid(in/out): Object identifier.. (set as a side effect)
 *   expected_length(in): Expected length of the object
 *   class_oid(in): The class of the instance
 *   classname(in): Optional... classname for classes
 *
 * Note: A permanent oid is assigned, the object associated with that
 *              OID is locked through the new OID. If the object is a class
 *              the classname to OID entry is updated to reflect the
 *              newly assigned OID.
 */
int
xlocator_assign_oid (THREAD_ENTRY * thread_p, const HFID * hfid, OID * perm_oid, int expected_length, OID * class_oid,
		     const char *classname)
{
  int error_code = NO_ERROR;

  if (heap_assign_address (thread_p, hfid, class_oid, perm_oid, expected_length) != NO_ERROR)
    {
      return ER_FAILED;
    }

  if (classname != NULL)
    {
      error_code = locator_permoid_class_name (thread_p, classname, perm_oid);
      assert (error_code == NO_ERROR);
    }

  return error_code;
}

/*
 * locator_find_lockset_missing_class_oids () - Find missing class oids
 *
 * return: NO_ERROR if all OK, ER_ status otherwise
 *
 *   lockset(in): Request for finding missing classes
 *
 * Note: Find missing class oids in requested area.
 *              The function does not quit when an error is found if the value
 *              of lockset->quit_on_errors is false. In this case the
 *              object with the error is set to a NULL_OID. For example, when
 *              a class of an object does not exist.
 * Note: There must be enough space in the lockset area to define all
 *              missing classes.
 */
static int
locator_find_lockset_missing_class_oids (THREAD_ENTRY * thread_p, LC_LOCKSET * lockset)
{
  LC_LOCKSET_REQOBJ *reqobjs;	/* Description of one instance to fetch */
  LC_LOCKSET_CLASSOF *reqclasses;	/* Description of one class of a requested object */
  OID class_oid;		/* Uses to hold the class_oid when it is unknown */
  int i, j;
  int error_code = NO_ERROR;

  /* Locate array of objects and array of classes */

  reqobjs = lockset->objects;
  reqclasses = lockset->classes;

#if defined(CUBRID_DEBUG)
  i = (sizeof (*lockset) + (lockset->num_reqobjs * (sizeof (*reqclasses) + sizeof (*reqobjs))));

  if (lockset->length < i || lockset->classes != ((LC_LOCKSET_CLASSOF *) (lockset->mem + sizeof (*lockset)))
      || lockset->objects < ((LC_LOCKSET_REQOBJ *) (lockset->classes + lockset->num_reqobjs)))
    {
      er_log_debug (ARG_FILE_LINE,
		    "locator_find_lockset_missing_class_oids:  *** SYSTEM ERROR. Requesting area is incorrect,\n"
		    " either area is too small %d (expect at least %d),\n pointer to classes %p (expected %p), or\n"
		    " pointer to objects %p (expected >= %p) are incorrect\n", lockset->length, i, lockset->classes,
		    ((LC_LOCKSET_CLASSOF *) (lockset->mem + sizeof (*lockset))), lockset->objects,
		    ((LC_LOCKSET_REQOBJ *) (lockset->classes + lockset->num_reqobjs)));
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);

      error_code = ER_GENERIC_ERROR;
      goto error;
    }
#endif /* CUBRID_DEBUG */


  /* 
   * All class identifiers of requested objects must be known. Find the ones
   * that the caller is unaware
   */

  for (i = 0; i < lockset->num_reqobjs; i++)
    {
      if (reqobjs[i].class_index != -1 || OID_ISNULL (&reqobjs[i].oid))
	{
	  continue;
	}
      /* 
       * Caller does not know the class identifier of the requested object.
       * Get the class identifier from disk
       */
      if (heap_get_class_oid (thread_p, &reqobjs[i].oid, &class_oid) != S_SUCCESS)
	{
	  /* 
	   * Unable to find the class of the object. Remove the object from
	   * the list of requested objects.
	   */
	  OID_SET_NULL (&reqobjs[i].oid);
	  if (lockset->quit_on_errors != false)
	    {
	      error_code = ER_FAILED;
	      goto error;
	    }
	  continue;
	}

      /* 
       * Insert this class in the list of classes of requested objects.
       * Make sure that the class is not already present in the list.
       */

      for (j = 0; j < lockset->num_classes_of_reqobjs; j++)
	{
	  if (OID_EQ (&class_oid, &reqclasses[j].oid))
	    {
	      /* OID is already in the list */
	      reqobjs[i].class_index = j;
	      break;
	    }
	}
      if (j >= lockset->num_classes_of_reqobjs)
	{
	  /* OID is not in the list */
	  COPY_OID (&reqclasses[lockset->num_classes_of_reqobjs].oid, &class_oid);
	  reqclasses[lockset->num_classes_of_reqobjs].chn = CHN_UNKNOWN_ATCLIENT;
	  reqobjs[i].class_index = lockset->num_classes_of_reqobjs;
	  lockset->num_classes_of_reqobjs++;
	}
    }

error:
  return error_code;
}

static SCAN_CODE
locator_return_object_assign (THREAD_ENTRY * thread_p, LOCATOR_RETURN_NXOBJ * assign, OID * class_oid, OID * oid,
			      int chn, int guess_chn, SCAN_CODE scan, int tran_index)
{
  int round_length;		/* Length of object rounded to integer alignment */

  switch (scan)
    {
    case S_SUCCESS:
      /* 
       * The cached object was obsolete.
       */
      round_length = DB_ALIGN (assign->recdes.length, MAX_ALIGNMENT);
      if (assign->recdes.area_size < (round_length + sizeof (*assign->obj)))
	{
	  assign->recdes.area_size -= (round_length + sizeof (*assign->obj));
	  scan = S_DOESNT_FIT;
	}
      else
	{
	  if (OID_IS_ROOTOID (class_oid))
	    {
	      if (tran_index == NULL_TRAN_INDEX)
		{
		  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
		}
	      (void) heap_chnguess_put (thread_p, oid, tran_index, or_chn (&assign->recdes));
	    }
	  assign->mobjs->num_objs++;

	  COPY_OID (&assign->obj->class_oid, class_oid);
	  COPY_OID (&assign->obj->oid, oid);

	  /* Set object flag */
	  assign->obj->flag = 0;

	  assign->obj->hfid = NULL_HFID;
	  assign->obj->length = assign->recdes.length;
	  assign->obj->offset = assign->area_offset;
	  assign->obj->operation = LC_FETCH;
	  assign->obj = LC_NEXT_ONEOBJ_PTR_IN_COPYAREA (assign->obj);

#if !defined(NDEBUG)
	  /* suppress valgrind UMW error */
	  memset (assign->recdes.data + assign->recdes.length, 0,
		  MIN (round_length - assign->recdes.length, assign->recdes.area_size - assign->recdes.length));
#endif
	  assign->recdes.length = round_length;
	  assign->area_offset += round_length;
	  assign->recdes.data += round_length;
	  assign->recdes.area_size -= round_length + sizeof (*assign->obj);
	}

      break;

    case S_SUCCESS_CHN_UPTODATE:
      /* 
       * the cached object was on the right state
       */
      scan = S_SUCCESS;

      if (guess_chn == CHN_UNKNOWN_ATCLIENT)
	{
	  if (assign->recdes.area_size < sizeof (*assign->obj))
	    {
	      assign->recdes.area_size -= sizeof (*assign->obj);
	      scan = S_DOESNT_FIT;
	    }
	  else
	    {
	      assign->mobjs->num_objs++;

	      /* Indicate to the caller that the object does not exist any longer */
	      COPY_OID (&assign->obj->class_oid, class_oid);
	      COPY_OID (&assign->obj->oid, oid);
	      assign->obj->flag = 0;
	      assign->obj->hfid = NULL_HFID;
	      assign->obj->length = -chn;
	      assign->obj->offset = -1;
	      assign->obj->operation = LC_FETCH_VERIFY_CHN;
	      assign->obj = LC_NEXT_ONEOBJ_PTR_IN_COPYAREA (assign->obj);
	      assign->recdes.area_size -= sizeof (*assign->obj);
	    }
	}
      break;

    case S_DOESNT_EXIST:
      /* 
       * The object does not exist
       */
      if (assign->recdes.area_size < sizeof (*assign->obj))
	{
	  assign->recdes.area_size -= sizeof (*assign->obj);
	  scan = S_DOESNT_FIT;
	}
      else
	{
	  assign->mobjs->num_objs++;

	  /* Indicate to the caller that the object does not exist any longer */
	  COPY_OID (&assign->obj->class_oid, class_oid);
	  COPY_OID (&assign->obj->oid, oid);
	  assign->obj->flag = 0;
	  assign->obj->hfid = NULL_HFID;
	  assign->obj->length = -1;
	  assign->obj->offset = -1;
	  assign->obj->operation = LC_FETCH_DELETED;
	  assign->obj = LC_NEXT_ONEOBJ_PTR_IN_COPYAREA (assign->obj);
	  assign->recdes.area_size -= sizeof (*assign->obj);
	}

      break;

    default:
      break;

    }

  return scan;
}

/*
 * locator_lock_and_return_object () - Lock object, get its data and place it
 *				       in communication area.
 *
 * return : SCAN_CODE
 *              (Either of S_SUCCESS,
 *                         S_DOESNT_FIT,
 *                         S_DOESNT_EXIST,
 *                         S_ERROR)
 * thread_p (in)   : Thread entry.
 * assign (in/out) : Locator structure to store object data.
 * class_oid (in)  : Class OID.
 * oid (in)	   : Object OID.
 * chn (in)	   : Known object CHN (or NULL_CHN if not known).
 * lock_mode (in)  : Require lock on object.
 *
 * Note: The desired object is placed in the assigned return area when the
 *	 state of the object(chn) in the client workspace is different from
 *	 the one on disk. If the object does not fit in assigned return area,
 *	 the length of the object is returned as a negative value in the area
 *	 recdes length.
 */
static SCAN_CODE
locator_lock_and_return_object (THREAD_ENTRY * thread_p, LOCATOR_RETURN_NXOBJ * assign, OID * class_oid, OID * oid,
				int chn, LOCK lock_mode, SCAN_OPERATION_TYPE op_type)
{
  SCAN_CODE scan;		/* Scan return value for next operation */
  int guess_chn = chn;
  int tran_index = NULL_TRAN_INDEX;

  /* 
   * The next object is placed in the assigned recdes area if the cached
   * object is obsolete and the object fits in the recdes
   */
  assert (oid != NULL);
  if (chn == CHN_UNKNOWN_ATCLIENT)
    {
      tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
      chn = heap_chnguess_get (thread_p, oid, tran_index);
    }

  scan = locator_get_object (thread_p, oid, class_oid, &assign->recdes, assign->ptr_scancache, op_type, lock_mode, COPY,
			     chn);
  if (scan == S_ERROR || scan == S_SNAPSHOT_NOT_SATISFIED || scan == S_END || scan == S_DOESNT_EXIST)
    {
      return scan;
    }

  scan = locator_return_object_assign (thread_p, assign, class_oid, oid, chn, guess_chn, scan, tran_index);

  return scan;
}

/*
 * xlocator_fetch () - Lock and fetch an object, and prefetch some other objects
 *
 * return: NO_ERROR if all OK, ER_ status otherwise
 *
 *   thread_p(in):
 *   oid(in): Object identifier of requested object
 *   chn(in): Cache coherence number of object
 *   lock(in): Lock to acquire before the object is fetched
 *   fetch_version_type(in): fetch version type
 *   initial_fetch_version_type(in): initial fetch version type
 *   class_oid(in): Class identifier of the object
 *   class_chn(in): Cache coherence number of the class of the object
 *   prefetching(in): true when prefetching of neighbors is desired
 *   fetch_area(in/out): Pointer to area where the objects are placed
                         (set to point to fetching area)
 *
 * Note: This function locks and fetches the object associated with the
 *              given oid. The object is only placed in the fetch area when
 *              the state of the object (chn) in the workspace (client) is
 *              different from the one on disk. The class along with several
 *              other neighbors of the object may also be included in the
 *              fetch area. It is up to the caller if the additional objects
 *              are cached. Fetch_area is set to NULL when there is an error
 *              or when there is not a need to send any object back since the
 *              cache coherent numbers were the same as those on disk. The
 *              caller must check the return value of the function to find out
 *              if there was any error.
 *       The returned fetch area should be freed by the caller.
 */
int
xlocator_fetch (THREAD_ENTRY * thread_p, OID * oid, int chn, LOCK lock,
		LC_FETCH_VERSION_TYPE fetch_version_type, LC_FETCH_VERSION_TYPE initial_fetch_version_type,
		OID * class_oid, int class_chn, int prefetching, LC_COPYAREA ** fetch_area)
{
  OID tmp_oid;			/* Uses to hold the class_oid when it is not know by the caller */
  LC_COPYAREA_DESC prefetch_des;
  LOCATOR_RETURN_NXOBJ nxobj;	/* Description to return next obj */
  int copyarea_length;
  SCAN_CODE scan = S_ERROR;
  int error_code = NO_ERROR;
  MVCC_SNAPSHOT *mvcc_snapshot;
  MVCC_SNAPSHOT mvcc_snapshot_dirty;
  SCAN_OPERATION_TYPE operation_type;
  OID *p_oid = oid;
  bool object_locked = false;
  int is_mvcc_disabled_class = -1;
  LOCK class_lock;
  /* We assume that the object have to be locked; after it is locked or we determine that this is not necessary,
   * object_need_locking will be set to false; */
  bool object_need_locking = true;
  bool skip_fetch_version_type_check = false;

  if (fetch_version_type == LC_FETCH_CURRENT_VERSION_NO_CHECK)
    {
      /* The purpose was to fetch the current version. Avoid checks. */
      fetch_version_type = LC_FETCH_CURRENT_VERSION;
      skip_fetch_version_type_check = true;
    }

  if (LC_FETCH_IS_MVCC_VERSION_NEEDED (initial_fetch_version_type))
    {
      skip_fetch_version_type_check = true;
    }

  switch (fetch_version_type)
    {
    case LC_FETCH_MVCC_VERSION:
      mvcc_snapshot = logtb_get_mvcc_snapshot (thread_p);
      if (mvcc_snapshot == NULL)
	{
	  error_code = er_errid ();
	  return (error_code == NO_ERROR ? ER_FAILED : error_code);
	}
      break;

    case LC_FETCH_DIRTY_VERSION:
      mvcc_snapshot_dirty.snapshot_fnc = mvcc_satisfies_dirty;
      mvcc_snapshot = &mvcc_snapshot_dirty;
      break;

    case LC_FETCH_CURRENT_VERSION:
      mvcc_snapshot = NULL;
      break;

    default:
      /* no usage at this point */
      assert (0);
      break;
    }

  /* Compute operation type */
  operation_type = locator_decide_operation_type (lock, fetch_version_type);

  if (class_oid == NULL)
    {
      /* The class_oid is not known by the caller. */
      class_oid = &tmp_oid;
      OID_SET_NULL (class_oid);
    }

#if !defined (NDEBUG)
  if (OID_ISNULL (class_oid))
    {
      /*
       * Caller does not know the class of the object. Get the class
       * identifier from disk
       */
      scan = heap_get_class_oid (thread_p, oid, class_oid);
      if (scan != S_SUCCESS)
	{
	  /* Unable to find the class of the object.. return */
	  *fetch_area = NULL;

	  error_code = ER_HEAP_UNKNOWN_OBJECT;
	  if (er_errid () != error_code)
	    {
	      /* error was not previously set; set error in order to have it returned to client */
	      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error_code, 3, oid->volid, oid->pageid, oid->slotid);
	    }
	  return error_code;
	}
    }

  assert (!OID_ISNULL (class_oid));

  /* Current version or dirty version for instance with NULL_LOCK is allowed only if the transaction already has a
   * lock. This means that is not necessary to request the lock again. */
  assert (skip_fetch_version_type_check || (OID_EQ (class_oid, oid_Root_class_oid))
	  || ((lock != NULL_LOCK)
	      || (lock_get_object_lock (oid, class_oid, LOG_FIND_THREAD_TRAN_INDEX (thread_p)) != NULL_LOCK)
	      || ((class_lock = lock_get_object_lock (class_oid, oid_Root_class_oid,
						      LOG_FIND_THREAD_TRAN_INDEX (thread_p))) == S_LOCK
		  || class_lock >= SIX_LOCK)
	      || ((class_lock = lock_get_object_lock (oid_Root_class_oid, NULL,
						      LOG_FIND_THREAD_TRAN_INDEX (thread_p))) == S_LOCK
		  || class_lock >= SIX_LOCK)));

  /* LC_FETCH_CURRENT_VERSION should be used for classes only */
  assert (fetch_version_type != LC_FETCH_CURRENT_VERSION || OID_IS_ROOTOID (class_oid));
#endif /* DEBUG */

  /* 
   * Lock and fetch the object.
   */

  /* Assume that the needed object can fit in one page */
  copyarea_length = DB_PAGESIZE;

  error_code = heap_scancache_start (thread_p, &nxobj.area_scancache, NULL, NULL, false, false, mvcc_snapshot);
  if (error_code != NO_ERROR)
    {
      nxobj.mobjs = NULL;
      error_code = ER_FAILED;
      goto error;
    }
  nxobj.ptr_scancache = &nxobj.area_scancache;

  while (true)
    {
      nxobj.comm_area = *fetch_area = locator_allocate_copy_area_by_length (copyarea_length);
      if (nxobj.comm_area == NULL)
	{
	  heap_scancache_end (thread_p, &nxobj.area_scancache);
	  nxobj.mobjs = NULL;
	  error_code = ER_FAILED;
	  goto error;
	}

      nxobj.mobjs = LC_MANYOBJS_PTR_IN_COPYAREA (nxobj.comm_area);
      nxobj.obj = LC_START_ONEOBJ_PTR_IN_COPYAREA (nxobj.mobjs);
      LC_RECDES_IN_COPYAREA (nxobj.comm_area, &nxobj.recdes);
      nxobj.area_offset = 0;
      nxobj.mobjs->num_objs = 0;

      /* Get the interested object first */
      scan = locator_lock_and_return_object (thread_p, &nxobj, class_oid, p_oid, chn,
					     object_need_locking ? lock : NULL_LOCK, operation_type);
      if (scan == S_SUCCESS)
	{
	  if (object_need_locking)
	    {
	      object_need_locking = false;
	      is_mvcc_disabled_class = mvcc_is_mvcc_disabled_class (class_oid);
	      if (is_mvcc_disabled_class == true)
		{
		  if (lock > NULL_LOCK)
		    {
		      /* Non-MVCC: Always lock object */
		      object_locked = true;
		    }
		}
	      else if (lock == X_LOCK)
		{
		  /* MVCC X_LOCK: Need exclusive lock on object. Get MVCC object version for delete/update. */
		  object_locked = true;
		}
	      else if (operation_type == S_SELECT_WITH_LOCK)
		{
		  /* MVCC S_SELECT_WITH_LOCK: We need to read and lock last object version found with satisfy dirty
		   * rules. This object must be also locked. */
		  object_locked = true;
		}
	    }
	  break;
	}

      /* Get the real length of current fetch/copy area */
      copyarea_length = nxobj.comm_area->length;
      locator_free_copy_area (nxobj.comm_area);

      /* 
       * If the object does not fit even when the copy area seems to be
       * large enough, increase the copy area by at least one page size.
       */
      if (scan == S_DOESNT_EXIST || scan == S_SNAPSHOT_NOT_SATISFIED)
	{
	  heap_scancache_end (thread_p, &nxobj.area_scancache);
	  nxobj.comm_area = *fetch_area = NULL;
	  error_code = ER_HEAP_UNKNOWN_OBJECT;
	  if (er_errid () != error_code)
	    {
	      /* error was not previously set; set error in order to have it returned to client */
	      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error_code, 3, oid->volid, oid->pageid, oid->slotid);
	    }
	  goto error;
	}
      else if (scan != S_DOESNT_FIT)
	{
	  heap_scancache_end (thread_p, &nxobj.area_scancache);
	  nxobj.comm_area = *fetch_area = NULL;
	  error_code = ER_FAILED;
	  goto error;
	}

      if ((-nxobj.recdes.length) > copyarea_length)
	{
	  copyarea_length = (DB_ALIGN (-nxobj.recdes.length, MAX_ALIGNMENT) + sizeof (*nxobj.mobjs));
	}
      else
	{
	  copyarea_length += DB_PAGESIZE;
	}
    }

  /* 
   * Then, get the interested class, if given class coherency number is not
   * current.
   */
  scan = locator_lock_and_return_object (thread_p, &nxobj, oid_Root_class_oid, class_oid, class_chn, NULL_LOCK,
					 S_SELECT);
  if (scan == S_SUCCESS && nxobj.mobjs->num_objs == 2)
    {
      LC_COPYAREA_ONEOBJ *first, *second;
      LC_COPYAREA_ONEOBJ save;
      /*
       * It is better if the class is cached first, so swap the
       * description. The object was stored first because it has
       * priority of retrieval, however, if both the object and its
       * class fits, the class should go first for performance reasons
       */

      /* Save the object information, then move the class information */
      first = LC_START_ONEOBJ_PTR_IN_COPYAREA (nxobj.mobjs);
      second = LC_NEXT_ONEOBJ_PTR_IN_COPYAREA (first);

      /* Swap the values */
      save = *first;
      *first = *second;
      *second = save;
    }

  error_code = heap_scancache_end (thread_p, &nxobj.area_scancache);
  if (error_code != NO_ERROR)
    {
      locator_free_copy_area (nxobj.comm_area);
      *fetch_area = NULL;
      error_code = ER_FAILED;
      goto error;
    }
  nxobj.ptr_scancache = NULL;


  prefetch_des.mobjs = nxobj.mobjs;
  prefetch_des.obj = &nxobj.obj;

  prefetch_des.offset = &nxobj.area_offset;
  prefetch_des.recdes = &nxobj.recdes;

  /* 
   * Find any decache notifications and prefetch any neighbors of the
   * instance
   */

  lock_notify_isolation_incons (thread_p, locator_notify_decache, &prefetch_des);
  if (prefetching && nxobj.mobjs->num_objs > 0)
    {
      error_code = heap_prefetch (thread_p, class_oid, oid, &prefetch_des);
    }

  if (nxobj.mobjs->num_objs == 0)
    {
      /* 
       * Don't need to send anything. The cache coherency numbers were
       * identical. Deallocate the area and return without failure
       */
      locator_free_copy_area (nxobj.comm_area);
      *fetch_area = NULL;
    }

error:
  if (lock != NULL_LOCK)
    {
      if (error_code != NO_ERROR)
	{
	  if (object_locked)
	    {
	      lock_unlock_object_donot_move_to_non2pl (thread_p, p_oid, class_oid, lock);
	    }
	}
      else if (is_mvcc_disabled_class == true
	       || (is_mvcc_disabled_class == -1 && mvcc_is_mvcc_disabled_class (class_oid)))
	{
	  if (lock <= S_LOCK)
	    {
	      assert (object_locked);
	      lock_unlock_object (thread_p, p_oid, class_oid, lock, false);
	    }
	}
    }

  return error_code;
}

/*
 * xlocator_get_class () - Lock and fetch the class of an instance, and prefetch
 *                    given instance and some other instances of class
 *
 * return: NO_ERROR if all OK, ER_ status otherwise
 *
 *   class_oid(in/out): Class identifier of the object. (It is set as a side
 *                 effect when its initial value is a null OID)
 *   class_chn(in): Cache coherence number of the class of the object
 *   oid(in): Object identifier of the instance of the desired class
 *   lock(in): Lock to acquire before the class is acquired/fetched
 *                 Note that the lock is for the class.
 *   prefetching(in): true when pretching of neighbors is desired
 *   fetch_area(in/out): Pointer to area where the objects are placed (set to
 *                 point to fetching area)
 *
 * Note: This function locks and fetches the class of the given
 *              instance. The class is only placed in a communication copy
 *              area when the state of the class (class_chn) in the workspace
 *              (client) is different from the one on disk. Other neighbors of
 *              the class are included in the copy_area. It is up to the
 *              caller if the additional classes are cached.  Fetch_area is
 *              set to NULL when there is an error or when there is not a need
 *              to send any object back since the cache coherent numbers were
 *              the same as those on disk. The caller must check the return
 *              value of the function to find out if there was any error.
 *
 * Note: The returned fetch area should be freed by the caller.
 */
int
xlocator_get_class (THREAD_ENTRY * thread_p, OID * class_oid, int class_chn, const OID * oid, LOCK lock,
		    int prefetching, LC_COPYAREA ** fetch_area)
{
  int error_code;

  if (OID_ISNULL (class_oid))
    {
      /* 
       * Caller does not know the class of the object. Get the class identifier
       * from disk
       */
      if (heap_get_class_oid (thread_p, oid, class_oid) != S_SUCCESS)
	{
	  /* 
	   * Unable to find out the class identifier.
	   */
	  ASSERT_ERROR_AND_SET (error_code);
	  *fetch_area = NULL;
	  return error_code;
	}
    }

  /* Now acquired the desired lock */
  if (lock != NULL_LOCK)
    {
      /* Now acquired the desired lock */
      if (lock_object (thread_p, class_oid, oid_Root_class_oid, lock, LK_UNCOND_LOCK) != LK_GRANTED)
	{
	  /* 
	   * Unable to acquired lock
	   */
	  *fetch_area = NULL;
	  return ER_FAILED;
	}
    }

  /* 
   * Now fetch the class, the instance and optionally prefetch some
   * neighbors of the instance. No need to get the last version for class.
   */

  error_code =
    xlocator_fetch (thread_p, class_oid, class_chn, NULL_LOCK, LC_FETCH_CURRENT_VERSION, LC_FETCH_CURRENT_VERSION,
		    oid_Root_class_oid, -1, prefetching, fetch_area);

  if (error_code != NO_ERROR && lock != NULL_LOCK)
    {
      lock_unlock_object (thread_p, class_oid, oid_Root_class_oid, lock, false);
    }

  return error_code;
}

/*
 * xlocator_fetch_all () - Fetch all instances of a class
 *
 * return: NO_ERROR if all OK, ER_ status otherwise
 *
 *   hfid(in): Heap file where the instances of the class are placed
 *   lock(in): Lock to acquired (Set as a side effect to NULL_LOCKID)
 *   fetch_version_type(in): fetch version type
 *   class_oid(in): Class identifier of the instances to fetch
 *   nobjects(out): Total number of objects to fetch.
 *   nfetched(out): Current number of object fetched.
 *   last_oid(out): Object identifier of last fetched object
 *   fetch_area(in/out): Pointer to area where the objects are placed
 *
 */
int
xlocator_fetch_all (THREAD_ENTRY * thread_p, const HFID * hfid, LOCK * lock, LC_FETCH_VERSION_TYPE fetch_version_type,
		    OID * class_oid, int *nobjects, int *nfetched, OID * last_oid, LC_COPYAREA ** fetch_area)
{
  LC_COPYAREA_DESC prefetch_des;	/* Descriptor for decache of objects related to transaction isolation level */
  LC_COPYAREA_MANYOBJS *mobjs;	/* Describe multiple objects in area */
  LC_COPYAREA_ONEOBJ *obj;	/* Describe on object in area */
  RECDES recdes;		/* Record descriptor for insertion */
  int offset;			/* Place to store next object in area */
  int round_length;		/* Length of object rounded to integer alignment */
  int copyarea_length;
  OID oid;
  HEAP_SCANCACHE scan_cache;
  SCAN_CODE scan;
  int error_code = NO_ERROR;
  MVCC_SNAPSHOT *mvcc_snapshot = NULL;
  MVCC_SNAPSHOT mvcc_snapshot_dirty;

  assert (lock != NULL);
  if (OID_ISNULL (last_oid))
    {
      /* FIRST TIME. */

      /* Obtain the desired lock for the class scan */
      if (*lock != NULL_LOCK
	  && lock_object (thread_p, class_oid, oid_Root_class_oid, *lock, LK_UNCOND_LOCK) != LK_GRANTED)
	{
	  /* 
	   * Unable to acquired lock
	   */
	  *fetch_area = NULL;
	  *lock = NULL_LOCK;
	  *nobjects = -1;
	  *nfetched = -1;

	  error_code = ER_FAILED;
	  goto error;
	}

      /* Get statistics */
      last_oid->volid = hfid->vfid.volid;
      last_oid->pageid = NULL_PAGEID;
      last_oid->slotid = NULL_SLOTID;
      /* Estimate the number of objects to be fetched */
      *nobjects = heap_estimate_num_objects (thread_p, hfid);
      *nfetched = 0;
      if (*nobjects == -1)
	{
	  if (*lock != NULL_LOCK)
	    {
	      lock_unlock_object (thread_p, class_oid, oid_Root_class_oid, *lock, false);
	    }
	  *fetch_area = NULL;
	  error_code = ER_FAILED;
	  goto error;
	}
    }

  switch (fetch_version_type)
    {
    case LC_FETCH_MVCC_VERSION:
      mvcc_snapshot = logtb_get_mvcc_snapshot (thread_p);
      if (mvcc_snapshot == NULL)
	{
	  error_code = er_errid ();
	  if (error_code == NO_ERROR)
	    {
	      error_code = ER_FAILED;
	    }
	  goto error;
	}
      break;

    case LC_FETCH_DIRTY_VERSION:
      mvcc_snapshot_dirty.snapshot_fnc = mvcc_satisfies_dirty;
      mvcc_snapshot = &mvcc_snapshot_dirty;
      break;

    case LC_FETCH_CURRENT_VERSION:
      mvcc_snapshot = NULL;
      break;

    default:
      assert (0);
    }

  /* Set OID to last fetched object */
  COPY_OID (&oid, last_oid);

  /* Start a scan cursor for getting several classes */
  error_code = heap_scancache_start (thread_p, &scan_cache, hfid, class_oid, true, false, mvcc_snapshot);
  if (error_code != NO_ERROR)
    {
      if (*lock != NULL_LOCK)
	{
	  lock_unlock_object (thread_p, class_oid, oid_Root_class_oid, *lock, false);
	}
      *fetch_area = NULL;

      goto error;
    }

  /* Assume that the next object can fit in one page */
  copyarea_length = DB_PAGESIZE;

  while (true)
    {
      *fetch_area = locator_allocate_copy_area_by_length (copyarea_length);
      if (*fetch_area == NULL)
	{
	  (void) heap_scancache_end (thread_p, &scan_cache);
	  if (*lock != NULL_LOCK)
	    {
	      lock_unlock_object (thread_p, class_oid, oid_Root_class_oid, *lock, false);
	    }

	  error_code = ER_FAILED;
	  goto error;
	}

      mobjs = LC_MANYOBJS_PTR_IN_COPYAREA (*fetch_area);
      LC_RECDES_IN_COPYAREA (*fetch_area, &recdes);
      obj = LC_START_ONEOBJ_PTR_IN_COPYAREA (mobjs);
      mobjs->num_objs = 0;
      offset = 0;

      while ((scan = heap_next (thread_p, hfid, class_oid, &oid, &recdes, &scan_cache, COPY)) == S_SUCCESS)
	{
	  mobjs->num_objs++;
	  COPY_OID (&obj->class_oid, class_oid);
	  COPY_OID (&obj->oid, &oid);
	  obj->flag = 0;
	  obj->hfid = NULL_HFID;
	  obj->length = recdes.length;
	  obj->offset = offset;
	  obj->operation = LC_FETCH;
	  obj = LC_NEXT_ONEOBJ_PTR_IN_COPYAREA (obj);
	  round_length = DB_ALIGN (recdes.length, MAX_ALIGNMENT);
#if !defined(NDEBUG)
	  /* suppress valgrind UMW error */
	  memset (recdes.data + recdes.length, 0, MIN (round_length - recdes.length, recdes.area_size - recdes.length));
#endif
	  offset += round_length;
	  recdes.data += round_length;
	  recdes.area_size -= round_length + sizeof (*obj);

	  if (mobjs->num_objs == DB_INT32_MAX)
	    {
	      /* Prevent overflow */
	      break;
	    }
	}

      if (scan != S_DOESNT_FIT || mobjs->num_objs > 0)
	{
	  break;
	}
      /* 
       * The first object does not fit into given copy area
       * Get a larger area
       */

      /* Get the real length of current fetch/copy area */
      copyarea_length = (*fetch_area)->length;
      locator_free_copy_area (*fetch_area);

      /* 
       * If the object does not fit even when the copy area seems to be
       * large enough, increase the copy area by at least one page size.
       */

      if ((-recdes.length) > copyarea_length)
	{
	  copyarea_length = DB_ALIGN (-recdes.length, MAX_ALIGNMENT) + sizeof (*mobjs);
	}
      else
	{
	  copyarea_length += DB_PAGESIZE;
	}
    }

  if (scan == S_END)
    {
      /* 
       * This is the end of the loop. Indicate the caller that no more calls
       * are needed by setting nobjects and nfetched to the same value.
       */
      error_code = heap_scancache_end (thread_p, &scan_cache);
      if (error_code != NO_ERROR)
	{
	  locator_free_copy_area (*fetch_area);
	  *fetch_area = NULL;
	  if (*lock != NULL_LOCK)
	    {
	      lock_unlock_object (thread_p, class_oid, oid_Root_class_oid, *lock, false);
	    }
	  *nobjects = *nfetched = -1;

	  goto error;
	}

      *nfetched += mobjs->num_objs;
      *nobjects = *nfetched;
      OID_SET_NULL (last_oid);
      if (*lock != NULL_LOCK)
	{
	  lock_unlock_object (thread_p, class_oid, oid_Root_class_oid, *lock, false);
	}
    }
  else if (scan == S_ERROR)
    {
      /* There was an error.. */
      (void) heap_scancache_end (thread_p, &scan_cache);

      locator_free_copy_area (*fetch_area);
      *fetch_area = NULL;
      if (*lock != NULL_LOCK)
	{
	  lock_unlock_object (thread_p, class_oid, oid_Root_class_oid, *lock, false);
	}
      *nobjects = *nfetched = -1;

      error_code = ER_FAILED;
      goto error;
    }
  else if (mobjs->num_objs != 0)
    {
      heap_scancache_end_when_scan_will_resume (thread_p, &scan_cache);
      /* Set the last_oid.. and the number of fetched objects */
      obj = LC_PRIOR_ONEOBJ_PTR_IN_COPYAREA (obj);
      COPY_OID (last_oid, &obj->oid);
      *nfetched += mobjs->num_objs;
      /* 
       * If the guess on the number of objects to fetch was low, reset the
       * value, so that the caller continue to call us until the end of the
       * scan
       */
      if (*nobjects <= *nfetched)
	{
	  *nobjects = *nfetched + 10;
	}
    }
  else
    {
      error_code = heap_scancache_end (thread_p, &scan_cache);
      if (error_code != NO_ERROR)
	{
	  goto error;
	}
    }

  if (*fetch_area != NULL)
    {
      prefetch_des.mobjs = mobjs;
      prefetch_des.obj = &obj;
      prefetch_des.offset = &offset;
      prefetch_des.recdes = &recdes;
      lock_notify_isolation_incons (thread_p, locator_notify_decache, &prefetch_des);
    }

error:
  return error_code;
}

/*
 * xlocator_fetch_lockset () - Lock and fetch many objects
 *
 * return: NO_ERROR if all OK, ER_ status otherwise
 *
 *   lockset(in/out): Request for finding missing classes and the lock
 *		      requested objects (Set as a side effect).
 *   fetch_area(in/out): Pointer to area where the objects are placed
 *
 */
int
xlocator_fetch_lockset (THREAD_ENTRY * thread_p, LC_LOCKSET * lockset, LC_COPYAREA ** fetch_area)
{
  LC_COPYAREA_DESC prefetch_des;	/* Descriptor for decache of objects related to transaction isolation level */
  LOCATOR_RETURN_NXOBJ nxobj;	/* Description to return next obj */
  struct lc_lockset_reqobj *reqobjs;	/* Description of requested objects */
  struct lc_lockset_classof *reqclasses;	/* Description of classes of requested objects. */
  int copyarea_length;
  SCAN_CODE scan = S_SUCCESS;
  int i, j;
  int error_code = NO_ERROR;
  MVCC_SNAPSHOT *mvcc_snapshot = NULL;

  mvcc_snapshot = logtb_get_mvcc_snapshot (thread_p);
  if (mvcc_snapshot == NULL)
    {
      error_code = er_errid ();
      return (error_code == NO_ERROR ? ER_FAILED : error_code);
    }

  *fetch_area = NULL;

  reqobjs = lockset->objects;
  reqclasses = lockset->classes;

  if (lockset->num_reqobjs_processed == -1)
    {
      /* 
       * FIRST CALL.
       * Initialize num of object processed.
       * Make sure that all classes are known and lock the classes and objects
       */
      lockset->num_reqobjs_processed = 0;
      lockset->num_classes_of_reqobjs_processed = 0;

      error_code = locator_find_lockset_missing_class_oids (thread_p, lockset);
      if (error_code != NO_ERROR)
	{
	  goto error;
	}
    }

  /* Start a scan cursor for getting several classes */
  error_code = heap_scancache_start (thread_p, &nxobj.area_scancache, NULL, NULL, true, false, NULL);
  if (error_code != NO_ERROR)
    {
      lock_unlock_objects_lock_set (thread_p, lockset);
      goto error;
    }
  nxobj.ptr_scancache = &nxobj.area_scancache;

  /* 
   * Assume that there are not any objects larger than one page. If there are
   * the number of pages is fixed later.
   */

  copyarea_length = DB_PAGESIZE;

  nxobj.mobjs = NULL;
  nxobj.comm_area = NULL;

  while (scan == S_SUCCESS
	 && ((lockset->num_classes_of_reqobjs_processed < lockset->num_classes_of_reqobjs)
	     || (lockset->num_reqobjs_processed < lockset->num_reqobjs)))
    {
      nxobj.comm_area = locator_allocate_copy_area_by_length (copyarea_length);
      if (nxobj.comm_area == NULL)
	{
	  (void) heap_scancache_end (thread_p, &nxobj.area_scancache);
	  lock_unlock_objects_lock_set (thread_p, lockset);
	  error_code = ER_FAILED;
	  goto error;
	}

      nxobj.mobjs = LC_MANYOBJS_PTR_IN_COPYAREA (nxobj.comm_area);
      nxobj.obj = LC_START_ONEOBJ_PTR_IN_COPYAREA (nxobj.mobjs);
      LC_RECDES_IN_COPYAREA (nxobj.comm_area, &nxobj.recdes);
      nxobj.area_offset = 0;
      nxobj.mobjs->num_objs = 0;

      /* 
       * CLASSES
       * Place the classes on the communication area, don't place those classes
       * with correct chns.
       */

      for (i = lockset->num_classes_of_reqobjs_processed; scan == S_SUCCESS && i < lockset->num_classes_of_reqobjs; i++)
	{
	  if (OID_ISNULL (&reqclasses[i].oid))
	    {
	      lockset->num_classes_of_reqobjs_processed++;
	      continue;
	    }
	  if (OID_ISTEMP (&reqclasses[i].oid))
	    {
	      lockset->num_classes_of_reqobjs_processed++;
	      continue;
	    }
	  scan =
	    locator_lock_and_return_object (thread_p, &nxobj, oid_Root_class_oid, &reqclasses[i].oid, reqclasses[i].chn,
					    lockset->reqobj_class_lock, S_SELECT);
	  if (scan == S_SUCCESS)
	    {
	      lockset->num_classes_of_reqobjs_processed++;
	    }
	  else if (scan == S_DOESNT_FIT && nxobj.mobjs->num_objs == 0)
	    {
	      /* 
	       * The first object does not fit into given copy area
	       * Get a larger area
	       */

	      /* Get the real length of current fetch/copy area */

	      copyarea_length = nxobj.comm_area->length;

	      /* 
	       * If the object does not fit even when the copy area seems
	       * to be large enough, increase the copy area by at least one
	       * page size.
	       */

	      if ((-nxobj.recdes.length) > copyarea_length)
		{
		  copyarea_length = (DB_ALIGN (-nxobj.recdes.length, MAX_ALIGNMENT) + sizeof (*nxobj.mobjs));
		}
	      else
		{
		  copyarea_length += DB_PAGESIZE;
		}

	      locator_free_copy_area (nxobj.comm_area);
	      scan = S_SUCCESS;
	      break;		/* finish the for */
	    }
	  else if (scan != S_DOESNT_FIT && (scan == S_DOESNT_EXIST || lockset->quit_on_errors == false))
	    {
	      OID_SET_NULL (&reqclasses[i].oid);
	      lockset->num_classes_of_reqobjs_processed += 1;
	      scan = S_SUCCESS;
	    }
	  else
	    {
	      break;		/* Quit on errors */
	    }
	}

      if (i >= lockset->num_classes_of_reqobjs)
	{
	  /* 
	   * DONE WITH CLASSES... NOW START WITH INSTANCES
	   * Place the instances in the fetching area, don't place those
	   * instances with correct chns or when they have been placed through
	   * the class array
	   */

	  for (i = lockset->num_reqobjs_processed; scan == S_SUCCESS && i < lockset->num_reqobjs; i++)
	    {
	      if (OID_ISNULL (&reqobjs[i].oid) || OID_ISTEMP (&reqobjs[i].oid) || reqobjs[i].class_index == -1
		  || OID_ISNULL (&reqclasses[reqobjs[i].class_index].oid))
		{
		  lockset->num_reqobjs_processed += 1;
		  continue;
		}

	      if (OID_IS_ROOTOID (&reqclasses[reqobjs[i].class_index].oid))
		{
		  /* 
		   * The requested object is a class. If this object is a class
		   * of other requested objects, the object has already been
		   * processed in the previous class iteration
		   */
		  for (j = 0; j < lockset->num_classes_of_reqobjs; j++)
		    {
		      if (OID_EQ (&reqobjs[i].oid, &reqclasses[j].oid))
			{
			  /* It has already been processed */
			  lockset->num_reqobjs_processed += 1;
			  break;
			}
		    }
		  if (j < lockset->num_classes_of_reqobjs)
		    {
		      continue;
		    }
		  nxobj.ptr_scancache->mvcc_snapshot = NULL;
		}
	      else
		{
		  nxobj.ptr_scancache->mvcc_snapshot = mvcc_snapshot;
		}

	      /* Now return the object */
	      scan = locator_lock_and_return_object (thread_p, &nxobj, &reqclasses[reqobjs[i].class_index].oid,
						     &reqobjs[i].oid, reqobjs[i].chn, lockset->reqobj_inst_lock,
						     S_SELECT);
	      if (scan == S_SUCCESS)
		{
		  lockset->num_reqobjs_processed++;
		  continue;
		}

	      if (scan == S_DOESNT_FIT && nxobj.mobjs->num_objs == 0)
		{
		  /* 
		   * The first object does not fit into given copy area
		   * Get a larger area
		   */

		  /* Get the real length of current fetch/copy area */

		  copyarea_length = nxobj.comm_area->length;

		  /* 
		   * If the object does not fit even when the copy area
		   * seems to be large enough, increase the copy area by at
		   * least one page size.
		   */

		  if ((-nxobj.recdes.length) > copyarea_length)
		    {
		      copyarea_length = ((-nxobj.recdes.length) + sizeof (*nxobj.mobjs));
		    }
		  else
		    {
		      copyarea_length += DB_PAGESIZE;
		    }

		  locator_free_copy_area (nxobj.comm_area);
		  scan = S_SUCCESS;
		  break;	/* finish the for */
		}
	      else if (scan != S_DOESNT_FIT && (scan == S_DOESNT_EXIST || lockset->quit_on_errors == false))
		{
		  OID_SET_NULL (&reqobjs[i].oid);
		  lockset->num_reqobjs_processed += 1;
		  scan = S_SUCCESS;
		}
	    }
	}
    }

  /* End the scan cursor */
  error_code = heap_scancache_end (thread_p, &nxobj.area_scancache);
  if (error_code != NO_ERROR)
    {
      /* There was an error.. */
      if (nxobj.mobjs != NULL)
	{
	  locator_free_copy_area (nxobj.comm_area);
	}
      lock_unlock_objects_lock_set (thread_p, lockset);
      goto error;
    }

  if (scan == S_ERROR)
    {
      /* There was an error.. */
      if (nxobj.mobjs != NULL)
	{
	  locator_free_copy_area (nxobj.comm_area);
	}
      lock_unlock_objects_lock_set (thread_p, lockset);
      error_code = ER_FAILED;
      goto error;
    }
  else if (nxobj.mobjs != NULL && nxobj.mobjs->num_objs == 0)
    {
      locator_free_copy_area (nxobj.comm_area);
    }
  else
    {
      *fetch_area = nxobj.comm_area;
    }

  if (*fetch_area != NULL)
    {
      prefetch_des.mobjs = nxobj.mobjs;
      prefetch_des.obj = &nxobj.obj;
      prefetch_des.offset = &nxobj.area_offset;
      prefetch_des.recdes = &nxobj.recdes;
      lock_notify_isolation_incons (thread_p, locator_notify_decache, &prefetch_des);
    }

  if ((lockset->num_classes_of_reqobjs_processed >= lockset->num_classes_of_reqobjs)
      && lockset->num_reqobjs_processed >= lockset->num_reqobjs)
    {
      lock_unlock_objects_lock_set (thread_p, lockset);
    }

error:
  return error_code;
}

/*
 * locator_all_reference_lockset () - Find all objects referenced by given object
 *
 * return: LC_LOCKSET * or NULL (in case of error)
 *
 *   oid(in): The desired object.
 *   prune_level(in): Get references up to this level. If the value is <= 0
 *                     means up to an infinite level (i.e., all references).
 *   inst_lock(in): Indicate this lock in the request area for objects that
 *                     are instances.
 *   class_lock(in): Indicate this lock in the request area for objects that
 *                     are classes.
 *   quit_on_errors(in): Quit when an error is found such as cannot lock all
 *                 nested objects.
 *
 * Note: This function find all direct and indirect references from the
 *              given object upto the given prune level in the nested graph.
 *              The given object is also included as a reference. Thus, the
 *              function can be seen as listing a graph of referenced/nested
 *              objects.
 *
 *        For performance reasons, the search for duplicate oids now uses an
 *        mht (hash table).  This means that we have to copy the oids into a
 *        non-relocatable place (see lc_ht_permoids below) until the entire
 *        graph is known.
 */
static LC_LOCKSET *
locator_all_reference_lockset (THREAD_ENTRY * thread_p, OID * oid, int prune_level, LOCK inst_lock, LOCK class_lock,
			       int quit_on_errors)
{
  OID class_oid;		/* The class_oid of an inst */
  int max_refs, ref_num;	/* Max and reference number in request area */
  int max_stack;		/* Total size of stack */
  int stack_actual_size;	/* Actual size of stack */
  int level;			/* The current listing level */
  int oid_list_size = 0;	/* Oid list size */
  OID *oid_list = NULL;		/* List of ref for one object */
  LC_LOCKSET *lockset = NULL;	/* Building request for obj. */
  struct lc_lockset_reqobj *reqobjs;	/* Description of one inst */
  struct lc_lockset_reqobj *to_reqobjs;	/* Description of one inst */
  struct lc_lockset_classof *reqclasses;	/* Description of one class */
  int *stack = NULL;		/* The stack for the search */
  HEAP_SCANCACHE scan_cache;	/* Scan cache used for fetching purposes */
  SCAN_CODE scan;		/* Scan return value for an object */
  RECDES peek_recdes;
  void *new_ptr;
  int i, tmp_ref_num, number;
  MHT_TABLE *lc_ht_permoids = NULL;	/* Hash table of already found oids */
  HL_HEAPID heap_id = HL_NULL_HEAPID;	/* Id of Heap allocator */
  MVCC_SNAPSHOT *mvcc_snapshot = NULL;

  struct ht_obj_info
  {
    OID oid;
    int ref_num;
  };				/* info stored into hash table */
  struct ht_obj_info *ht_obj;

  /* Make sure that the object exists ? */
  if (!heap_does_exist (thread_p, NULL, oid))
    {
      if (er_errid () != ER_INTERRUPTED)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_UNKNOWN_OBJECT, 3, oid->volid, oid->pageid, oid->slotid);
	}

      goto error;
    }

  /* Let's assume a number of references for allocation start purposes */
  max_refs = max_stack = LOCATOR_GUESS_NUM_NESTED_REFERENCES;

  lockset = locator_allocate_lockset (max_refs, inst_lock, class_lock, quit_on_errors);
  if (lockset == NULL)
    {
      goto error;
    }

  stack = (int *) malloc (sizeof (*stack) * max_stack);
  if (stack == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (*stack) * max_stack);
      goto error;
    }

  /* Use a hash table to speed up lockset verification when looking for cycles in the graph */
  lc_ht_permoids = mht_create ("Memory hash lc_allrefs", LOCATOR_GUESS_HT_SIZE, oid_hash, oid_compare_equals);
  if (lc_ht_permoids == NULL)
    {
      goto error;
    }

  /* Use a chunky memory manager, for fewer mallocs of small stuff */
  heap_id = db_create_fixed_heap (sizeof (struct ht_obj_info), LOCATOR_GUESS_HT_SIZE);
  if (heap_id == HL_NULL_HEAPID)
    {
      goto error;
    }

  /* Initialize the stack */
  stack_actual_size = 0;
  level = 0;

  reqobjs = lockset->objects;
  reqclasses = lockset->classes;

  /* 
   * Add first object to the stack and request structure.
   * Indicate that the object is only on the stack. That is, the object has
   * not been visited.
   * The cache coherence number is used to hold the level of the object in
   * the nested graph/tree
   */

  COPY_OID (&reqobjs->oid, oid);
  reqobjs->class_index = -1;
  reqobjs->chn = level;
  stack[stack_actual_size++] = lockset->num_reqobjs++;	/* Push */
  reqobjs++;

  mvcc_snapshot = logtb_get_mvcc_snapshot (thread_p);
  if (mvcc_snapshot == NULL)
    {
      goto error;
    }

  /* 
   * Start a kind of depth-first search algorithm to find out all references
   * until the prune level is reached
   */

  /* Start a scan cursor for getting several classes */
  if (heap_scancache_start (thread_p, &scan_cache, NULL, NULL, true, false, mvcc_snapshot) != NO_ERROR)
    {
      goto error;
    }

  while (stack_actual_size > 0)
    {
      ref_num = stack[--stack_actual_size];	/* Pop */

      /* Get the object to find out its direct references */
      OID_SET_NULL (&class_oid);
      scan = heap_get_visible_version (thread_p, &lockset->objects[ref_num].oid, &class_oid, &peek_recdes, &scan_cache,
				       PEEK, NULL_CHN);
      if (scan != S_SUCCESS)
	{
	  if (scan != S_DOESNT_EXIST && (quit_on_errors == true || er_errid () == ER_INTERRUPTED))
	    {
	      (void) heap_scancache_end (thread_p, &scan_cache);
	      goto error;
	    }

	  /* Remove the object from the list of requested objects */
	  if (ref_num == lockset->num_reqobjs - 1)
	    {
	      /* Last element remove it */
	      lockset->num_reqobjs--;
	      reqobjs--;
	    }
	  else
	    {
	      /* Marked it as invalid */
	      OID_SET_NULL (&lockset->objects[ref_num].oid);
	    }
	  er_clear ();
	  continue;
	}

      /* 
       * has the object been visited ?
       */
      if (lockset->objects[ref_num].class_index == -1)
	{
	  /* 
	   * Object has never been visited. First time in the stack.
	   * Find its class and marked as listed in the lockset structure
	   */

	  /* Is this class already stored ? */

	  for (i = 0; i < lockset->num_classes_of_reqobjs; i++)
	    {
	      if (OID_EQ (&class_oid, &lockset->classes[i].oid))
		{
		  break;
		}
	    }
	  if (i < lockset->num_classes_of_reqobjs)
	    {
	      /* Class is already in the lockset class list array */
	      lockset->objects[ref_num].class_index = i;
	    }
	  else
	    {
	      /* 
	       * Class is not in the lockset class list array.
	       * Make sure that this is a valid class
	       */
	      if (!heap_does_exist (thread_p, oid_Root_class_oid, &class_oid))
		{
		  /* Remove the object from the list of requested objects */
		  if (ref_num == lockset->num_reqobjs - 1)
		    {
		      /* Last element remove it */
		      lockset->num_reqobjs--;
		      reqobjs--;
		    }
		  else
		    {
		      /* Marked it as invalid */
		      OID_SET_NULL (&lockset->objects[ref_num].oid);
		    }
		  continue;
		}
	      COPY_OID (&reqclasses->oid, &class_oid);
	      reqclasses->chn = NULL_CHN;	/* Note that this is a level */
	      lockset->objects[ref_num].class_index = lockset->num_classes_of_reqobjs;
	      lockset->num_classes_of_reqobjs++;
	      reqclasses++;
	    }
	}

      /* Level for the directly referenced objects */

      level = lockset->objects[ref_num].chn + 1;
      if (prune_level >= 0 && level > prune_level)
	{
	  continue;
	}

      /* 
       * Find all direct references from the given object
       */
      if (OID_IS_ROOTOID (&class_oid))
	{
	  continue;
	}
      number =
	heap_get_referenced_by (thread_p, &class_oid, &lockset->objects[ref_num].oid, &peek_recdes, &oid_list_size,
				&oid_list);
      if (number <= 0)
	{
	  continue;
	}

      /* 
       * Add the above references to the stack if these objects have not
       * been alredy visited or if their current level is smaller than their
       * visited level
       */

      if (oid_list == NULL || number <= 0)
	{
	  continue;
	}
      for (i = 0; i < number; i++)
	{
	  if (OID_ISNULL (&oid_list[i]))
	    {
	      continue;
	    }

	  ht_obj = (struct ht_obj_info *) mht_get (lc_ht_permoids, &oid_list[i]);
	  if (ht_obj != NULL)
	    {
	      tmp_ref_num = ht_obj->ref_num;
	      if (lockset->objects[tmp_ref_num].chn > level)
		{
		  /* 
		   * Re-visit the object again since some of its
		   * references may have been pruned
		   */
		  lockset->objects[tmp_ref_num].chn = level;
		  /* push */
		  stack[stack_actual_size++] = tmp_ref_num;
		}
	    }
	  else
	    {
	      tmp_ref_num = lockset->num_reqobjs;
	      /* 
	       * Push the object onto the stack.
	       * Make sure that we have area in the stack and the
	       * request area
	       */
	      if (stack_actual_size >= max_stack)
		{
		  /* Expand the stack */
		  if (number > LOCATOR_GUESS_NUM_NESTED_REFERENCES)
		    {
		      max_stack += number;
		    }
		  else
		    {
		      max_stack += LOCATOR_GUESS_NUM_NESTED_REFERENCES;
		    }
		  new_ptr = realloc (stack, sizeof (*stack) * max_stack);
		  if (new_ptr == NULL)
		    {
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
			      sizeof (*stack) * max_stack);
		      if (quit_on_errors == false)
			{
			  break;
			}
		      (void) heap_scancache_end (thread_p, &scan_cache);
		      goto error;
		    }
		  stack = (int *) new_ptr;
		}

	      if ((lockset->num_reqobjs + 1) > max_refs)
		{
		  if (number > LOCATOR_GUESS_NUM_NESTED_REFERENCES)
		    {
		      max_refs += number;
		    }
		  else
		    {
		      max_refs += LOCATOR_GUESS_NUM_NESTED_REFERENCES;
		    }
		  new_ptr = locator_reallocate_lockset (lockset, max_refs);
		  if (new_ptr == NULL)
		    {
		      if (quit_on_errors == false)
			{
			  break;
			}
		      (void) heap_scancache_end (thread_p, &scan_cache);
		      goto error;
		    }
		  lockset = (LC_LOCKSET *) new_ptr;
		  /* Find the new locations since the structure was reallocated */
		  reqobjs = lockset->objects + lockset->num_reqobjs;
		  reqclasses = lockset->classes + lockset->num_classes_of_reqobjs;
		}

	      /* Put object in the hash table */
	      ht_obj = (struct ht_obj_info *) db_fixed_alloc (heap_id, sizeof (*ht_obj));
	      if (ht_obj == NULL)
		{
		  if (quit_on_errors == false)
		    {
		      break;
		    }
		  (void) heap_scancache_end (thread_p, &scan_cache);
		  goto error;
		}
	      COPY_OID (&ht_obj->oid, &oid_list[i]);
	      ht_obj->ref_num = tmp_ref_num;

	      if (mht_put (lc_ht_permoids, &ht_obj->oid, ht_obj) != ht_obj)
		{
		  if (quit_on_errors == false)
		    {
		      break;
		    }
		  (void) heap_scancache_end (thread_p, &scan_cache);
		  goto error;
		}

	      /* 
	       * Push the object
	       * Indicate that the object is only on the stack. That is,
	       * the object has not been visited.
	       * The cache coherence number is used to hold the level of
	       * the object in the nested graph/tree
	       */
	      COPY_OID (&reqobjs->oid, &oid_list[i]);
	      reqobjs->class_index = -1;
	      reqobjs->chn = level;
	      /* Push */
	      stack[stack_actual_size++] = lockset->num_reqobjs++;
	      reqobjs++;
	    }
	}
    }

  /* Cleanup */
  if (oid_list != NULL)
    {
      free_and_init (oid_list);
    }
  free_and_init (stack);
  (void) heap_scancache_end (thread_p, &scan_cache);
  db_destroy_fixed_heap (heap_id);
  mht_destroy (lc_ht_permoids);

  /* 
   * Set the cache coherence numbers as unknown (these are the ones of the
   * client workspace) and compact the array of requested objects. Note that
   * before we have used the chn as the level, so it needs to be reset.
   */

  number = 0;
  to_reqobjs = reqobjs = lockset->objects;
  for (i = 0; i < lockset->num_reqobjs; i++)
    {
      if (!OID_ISNULL (&reqobjs->oid))
	{
	  /* Move it to */
	  if (to_reqobjs != reqobjs)
	    {
	      memcpy (to_reqobjs, reqobjs, sizeof (*reqobjs));
	    }
	  to_reqobjs->chn = NULL_CHN;
	  to_reqobjs++;
	}
      else
	{
	  number++;
	}
      reqobjs++;
    }
  lockset->num_reqobjs -= number;

  for (i = 0; i < lockset->num_classes_of_reqobjs; i++)
    {
      lockset->classes[i].chn = CHN_UNKNOWN_ATCLIENT;
    }

  return lockset;

error:
  if (oid_list != NULL)
    {
      free_and_init (oid_list);
    }
  if (lc_ht_permoids != NULL)
    {
      mht_destroy (lc_ht_permoids);
      lc_ht_permoids = NULL;
    }
  if (stack != NULL)
    {
      free_and_init (stack);
    }
  if (lockset != NULL)
    {
      locator_free_lockset (lockset);
      lockset = NULL;
    }
  if (heap_id != HL_NULL_HEAPID)
    {
      db_destroy_fixed_heap (heap_id);
    }

  return NULL;
}

/*
 * xlocator_fetch_all_reference_lockset () - Lock and fetch the requested objects and its
 *                                direct and indirect references
 *
 * return: NO_ERROR if all OK, ER_ status otherwise
 *
 *   oid(in): The desired object in the root of nested references
 *   chn(in): Cache coherence number of desired object
 *   class_oid(in): Class identifier of the desired object
 *   class_chn(in): Cache coherence number of the class of the desired object
 *   lock(in): Lock to acquire on the desired object and its references
 *   quit_on_errors(in): Wheater to continue in case an error, such as an
 *                 object can be locked
 *   prune_level(in): Get references upto this level. If the value is <= 0
 *                 means upto an infonite level (i.e., all references).
 *   lockset(in/out): Request for finding the all references. This is set to
 *                 NULL when the references are unknown.
 *   fetch_area(in/out): Pointer to area where the objects are placed (Set as a
 *                 side effect)
 *
 */
int
xlocator_fetch_all_reference_lockset (THREAD_ENTRY * thread_p, OID * oid, int chn, OID * class_oid, int class_chn,
				      LOCK lock, int quit_on_errors, int prune_level, LC_LOCKSET ** lockset,
				      LC_COPYAREA ** fetch_area)
{
  int i;
  LOCK instance_lock;

  /* Find all the references */
  if (lock <= S_LOCK)
    {
      instance_lock = IS_LOCK;
    }
  else
    {
      instance_lock = IX_LOCK;
    }
  *lockset = locator_all_reference_lockset (thread_p, oid, prune_level, lock, instance_lock, quit_on_errors);
  if (*lockset == NULL)
    {
      return ER_FAILED;
    }

  /* 
   * Set the known cache coherence numbers of the desired object and its
   * class
   */

  if (chn != NULL_CHN)
    {
      for (i = 0; i < (*lockset)->num_reqobjs; i++)
	{
	  if (OID_EQ (oid, &(*lockset)->objects[i].oid))
	    {
	      (*lockset)->objects[i].chn = chn;
	      break;
	    }
	}
    }

  if (class_oid != NULL && class_chn != NULL_CHN)
    {
      for (i = 0; i < (*lockset)->num_classes_of_reqobjs; i++)
	{
	  if (OID_EQ (class_oid, &(*lockset)->classes[i].oid))
	    {
	      (*lockset)->classes[i].chn = class_chn;
	      break;
	    }
	}
    }

  /* Get the first batch of classes and objects */
  return xlocator_fetch_lockset (thread_p, *lockset, fetch_area);
}

/*
 * xlocator_does_exist () - Does object exist? if it does prefetch it
 *
 * return: Either of (LC_EXIST, LC_DOESNOT_EXIST, LC_ERROR)
 *
 *   oid(in): Object identifier of desired object
 *   chn(in): Cache coherence number of object
 *   lock(in): Lock to acquire for the object
 *   fetch_type(in): fetch type
 *   class_oid(in): Class identifier of the object
 *   class_chn(in): Cache coherence number of the class of the object
 *   need_fetching(in):
 *   prefetching(in): true if prefetching of some of the object neighbors is
 *                 desired.
 *   fetch_area(in/out):  Pointer to area where the objects are placed
                   (set to point to fetching area)
 *
 * Note:This function checks if the desired object exist. An error is
 *              not set if the object does not exist. If the object exists and
 *              prefetching is desired, prefetching is done for the object and
 *              some of its neighbors.
 */
int
xlocator_does_exist (THREAD_ENTRY * thread_p, OID * oid, int chn, LOCK lock, LC_FETCH_VERSION_TYPE fetch_version_type,
		     OID * class_oid, int class_chn, int need_fetching, int prefetching, LC_COPYAREA ** fetch_area)
{
  OID tmp_oid;
  SCAN_CODE scan_code = S_SUCCESS;

  if (need_fetching && fetch_area != NULL)
    {
      *fetch_area = NULL;
    }

  if (class_oid == NULL)
    {
      class_oid = &tmp_oid;
      OID_SET_NULL (class_oid);
    }

  /* Quick fix: we need to check if OID is valid - meaning that page is still valid. This code is going to be
   * removed with one of the refactoring issues anyway.
   */
  if (HEAP_ISVALID_OID (oid) != DISK_VALID)
    {
      return LC_DOESNOT_EXIST;
    }

  /* Prefetch the object if that operation is desirable */
  if (need_fetching && fetch_area != NULL)
    {
      int ret = xlocator_fetch (thread_p, oid, NULL_CHN, lock, fetch_version_type, fetch_version_type,
				class_oid, class_chn, prefetching, fetch_area);
      if (ret == ER_FAILED)
	{
	  ASSERT_ERROR ();
	  if (lock != NULL_LOCK)
	    {
	      lock_unlock_object (thread_p, oid, class_oid, lock, false);
	    }
	  return LC_ERROR;
	}
      else if (ret != NO_ERROR)
	{
	  return LC_DOESNOT_EXIST;
	}

      /* success -> fall through */
    }
  else
    {
      if (lock != NULL_LOCK)
	{
	  /* try to aquire requested lock or the appropriate one; it will be decided according to class type */
	  SCAN_OPERATION_TYPE op_type;
	  HEAP_SCANCACHE scan_cache;

	  op_type = locator_decide_operation_type (lock, fetch_version_type);

	  (void) heap_scancache_quick_start (&scan_cache);

	  scan_code = locator_get_object (thread_p, oid, class_oid, NULL, &scan_cache, op_type, lock, PEEK, NULL_CHN);
	  heap_scancache_end (thread_p, &scan_cache);
	  if (scan_code == S_ERROR)
	    {
	      ASSERT_ERROR ();
	      return LC_ERROR;
	    }
	  else if (scan_code != S_SUCCESS)
	    {
	      return LC_DOESNOT_EXIST;
	    }
	}
      else if (!heap_does_exist (thread_p, class_oid, oid))
	{
	  /* object doesn't exist */
	  return LC_DOESNOT_EXIST;
	}

      /* success -> fall through */
    }

  /* Success */
  return LC_EXIST;
}

/*
 * locator_start_force_scan_cache () -
 *
 * return: NO_ERROR if all OK, ER_ status otherwise
 *
 *   scan_cache(in/out):
 *   hfid(in):
 *   class_oid(in):
 *   op_type(in):
 */
int
locator_start_force_scan_cache (THREAD_ENTRY * thread_p, HEAP_SCANCACHE * scan_cache, const HFID * hfid,
				const OID * class_oid, int op_type)
{
  return heap_scancache_start_modify (thread_p, scan_cache, hfid, class_oid, op_type, NULL);
}

/*
 * locator_end_force_scan_cache () -
 *
 * return:
 *
 *   scan_cache(in):
 */
void
locator_end_force_scan_cache (THREAD_ENTRY * thread_p, HEAP_SCANCACHE * scan_cache)
{
  heap_scancache_end_modify (thread_p, scan_cache);
}


/*
 * locator_check_primary_key_upddel () -
 *
 * return: NO_ERROR if all OK, ER_ status otherwise
 *
 *   thread_p(in):
 *   class_oid(in):
 *   inst_oid(in):
 *   recdes(in):
 *   idx_action_flag(in): is moving record between partitioned table? 
 *			 If FOR_MOVE, this delete(&insert) is caused by 
 *			 'UPDATE ... SET ...', NOT 'DELETE FROM ...'
 */
static int
locator_check_primary_key_upddel (THREAD_ENTRY * thread_p, OID * class_oid, OID * inst_oid, RECDES * recdes,
				  LOCATOR_INDEX_ACTION_FLAG idx_action_flag)
{
  int num_found, i;
  HEAP_CACHE_ATTRINFO index_attrinfo;
  HEAP_IDX_ELEMENTS_INFO idx_info;
  BTID btid;
  DB_VALUE dbvalue;
  DB_VALUE *key_dbvalue;
  char buf[DBVAL_BUFSIZE + MAX_ALIGNMENT], *aligned_buf;
  OR_INDEX *index;
  bool is_null;
  int error_code = NO_ERROR;

  DB_MAKE_NULL (&dbvalue);

  aligned_buf = PTR_ALIGN (buf, MAX_ALIGNMENT);

  num_found = heap_attrinfo_start_with_index (thread_p, class_oid, NULL, &index_attrinfo, &idx_info);
  if (num_found <= 0)
    {
      return error_code;
    }

  if (idx_info.has_single_col)
    {
      error_code = heap_attrinfo_read_dbvalues (thread_p, inst_oid, recdes, NULL, &index_attrinfo);
      if (error_code != NO_ERROR)
	{
	  goto error;
	}
    }

  for (i = 0; i < idx_info.num_btids; i++)
    {
      index = &(index_attrinfo.last_classrepr->indexes[i]);
      if (index->type != BTREE_PRIMARY_KEY || index->fk == NULL)
	{
	  continue;
	}

      key_dbvalue =
	heap_attrvalue_get_key (thread_p, i, &index_attrinfo, recdes, &btid, &dbvalue, aligned_buf, NULL, NULL);
      if (key_dbvalue == NULL)
	{
	  error_code = ER_FAILED;
	  goto error;
	}

      if (index->n_atts > 1)
	{
	  is_null = btree_multicol_key_is_null (key_dbvalue);
	}
      else
	{
	  is_null = DB_IS_NULL (key_dbvalue);
	}

      if (!is_null)
	{
	  switch (idx_action_flag)
	    {
	    case FOR_MOVE:
	      error_code = locator_check_primary_key_update (thread_p, index, key_dbvalue);
	      if (error_code != NO_ERROR)
		{
		  goto error;
		}
	      break;
	    case FOR_INSERT_OR_DELETE:
	      error_code = locator_check_primary_key_delete (thread_p, index, key_dbvalue);
	      if (error_code != NO_ERROR)
		{
		  goto error;
		}
	      break;
	    default:
	      error_code = ER_FAILED;
	      goto error;
	    }
	}
    }

error:
  if (key_dbvalue == &dbvalue)
    {
      pr_clear_value (&dbvalue);
    }

  heap_attrinfo_end (thread_p, &index_attrinfo);
  return error_code;
}

/*
 * locator_check_foreign_key () -
 *
 * return: NO_ERROR if all OK, ER_ status otherwise
 *
 *   hfid(in):
 *   class_oid(in):
 *   inst_oid(in):
 *   recdes(in):
 *   new_recdes(in):
 *   is_cached(in):
 *   copyarea(in):
 */
static int
locator_check_foreign_key (THREAD_ENTRY * thread_p, HFID * hfid, OID * class_oid, OID * inst_oid, RECDES * recdes,
			   RECDES * new_recdes, bool * is_cached, LC_COPYAREA ** copyarea)
{
  int num_found, i;
  HEAP_CACHE_ATTRINFO index_attrinfo;
  HEAP_IDX_ELEMENTS_INFO idx_info;
  BTID btid, local_btid;
  DB_VALUE *key_dbvalue;
  DB_VALUE dbvalue;
  char buf[DBVAL_BUFSIZE + MAX_ALIGNMENT], *aligned_buf;
  OR_INDEX *index;
  OID unique_oid;
  OID part_oid;
  HFID class_hfid;
  bool has_null;
  int error_code = NO_ERROR;
  PRUNING_CONTEXT pcontext;
  bool clear_pcontext = false;
  OR_CLASSREP *classrepr = NULL;
  int classrepr_cacheindex = -1;
  BTREE_SEARCH ret;

  DB_MAKE_NULL (&dbvalue);

  aligned_buf = PTR_ALIGN (buf, MAX_ALIGNMENT);

  num_found = heap_attrinfo_start_with_index (thread_p, class_oid, NULL, &index_attrinfo, &idx_info);
  if (num_found <= 0)
    {
      return error_code;
    }

  if (idx_info.has_single_col)
    {
      error_code = heap_attrinfo_read_dbvalues (thread_p, inst_oid, recdes, NULL, &index_attrinfo);
      if (error_code != NO_ERROR)
	{
	  goto error;
	}
    }

  for (i = 0; i < idx_info.num_btids; i++)
    {
      index = &(index_attrinfo.last_classrepr->indexes[i]);
      if (index->type != BTREE_FOREIGN_KEY)
	{
	  continue;
	}

      /* must be updated when key_prefix_length will be added for FK and PK */
      key_dbvalue =
	heap_attrvalue_get_key (thread_p, i, &index_attrinfo, recdes, &btid, &dbvalue, aligned_buf, NULL, NULL);
      if (key_dbvalue == NULL)
	{
	  error_code = ER_FAILED;
	  goto error;
	}

      /* SQL standard defines as follows:
       * If no <match type> was specified then, for each row R1 of the referencing table, 
       * either at least one of the values of the referencing columns in R1 shall be a null value,
       * or the value of each referencing column in R1 shall be equal to the value of 
       * the corresponding referenced column in some row of the referenced table.
       * Please notice that we don't currently support <match type>. 
       */
      if (index->n_atts > 1)
	{

	  has_null = btree_multicol_key_has_null (key_dbvalue);
	}
      else
	{
	  has_null = DB_IS_NULL (key_dbvalue);
	}

      if (!has_null)
	{
	  /* get class representation to find partition information */
	  classrepr =
	    heap_classrepr_get (thread_p, &index->fk->ref_class_oid, NULL, NULL_REPRID, &classrepr_cacheindex);
	  if (classrepr == NULL)
	    {
	      error_code = ER_FAILED;
	      goto error;
	    }
	  if (classrepr->has_partition_info > 0)
	    {
	      (void) partition_init_pruning_context (&pcontext);
	      clear_pcontext = true;
	      error_code =
		partition_load_pruning_context (thread_p, &index->fk->ref_class_oid, DB_PARTITIONED_CLASS, &pcontext);
	      if (error_code != NO_ERROR)
		{
		  goto error;
		}
	    }

	  BTID_COPY (&local_btid, &index->fk->ref_class_pk_btid);
	  COPY_OID (&part_oid, &index->fk->ref_class_oid);

	  if (classrepr->has_partition_info > 0 && pcontext.partitions != NULL)
	    {
	      error_code = partition_prune_unique_btid (&pcontext, key_dbvalue, &part_oid, &class_hfid, &local_btid);
	      if (error_code != NO_ERROR)
		{
		  goto error;
		}
	    }
	  ret =
	    xbtree_find_unique (thread_p, &local_btid, S_SELECT_WITH_LOCK, key_dbvalue, &part_oid, &unique_oid, true);
	  if (ret == BTREE_KEY_NOTFOUND)
	    {
	      char *val_print = NULL;

	      val_print = pr_valstring (key_dbvalue);
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FK_INVALID, 2, index->fk->fkname,
		      (val_print ? val_print : "unknown value"));
	      error_code = ER_FK_INVALID;
	      if (val_print)
		{
		  free_and_init (val_print);
		}

	      if (key_dbvalue == &dbvalue)
		{
		  pr_clear_value (&dbvalue);
		}

	      if (LOG_CHECK_LOG_APPLIER (thread_p))
		{
		  continue;
		}

	      goto error;
	    }
	  else if (ret == BTREE_ERROR_OCCURRED)
	    {
	      ASSERT_ERROR_AND_SET (error_code);
	      goto error;
	    }
	  assert (ret == BTREE_KEY_FOUND);
	  /* TODO: For read committed... Do we need to keep the lock? */
	}

      if (key_dbvalue == &dbvalue)
	{
	  pr_clear_value (&dbvalue);
	}
    }

error:
  if (clear_pcontext == true)
    {
      partition_clear_pruning_context (&pcontext);
    }
  if (classrepr != NULL)
    {
      (void) heap_classrepr_free (classrepr, &classrepr_cacheindex);
    }
  heap_attrinfo_end (thread_p, &index_attrinfo);
  return error_code;
}

/*
 * locator_check_primary_key_delete () -
 *
 * return: NO_ERROR if all OK, ER_ status otherwise
 *
 *   fkref(in):
 *   key(in):
 */
static int
locator_check_primary_key_delete (THREAD_ENTRY * thread_p, OR_INDEX * index, DB_VALUE * key)
{
  OR_FOREIGN_KEY *fkref;
  int oid_cnt, force_count, i;
  RECDES recdes;
  HEAP_SCANCACHE scan_cache;
  HFID hfid;
  BTREE_SCAN bt_scan;
  INDX_SCAN_ID isid;
  KEY_VAL_RANGE key_val_range;
  bool is_upd_scan_init;
  int error_code = NO_ERROR;
  HEAP_CACHE_ATTRINFO attr_info;
  DB_VALUE null_value;
  ATTR_ID *attr_ids = NULL;
  int num_attrs = 0;
  int k;
  int *keys_prefix_length = NULL;
  MVCC_SNAPSHOT *mvcc_snapshot = NULL;
  OID found_oid;
  BTREE_ISCAN_OID_LIST oid_list;

  oid_list.oidp = NULL;

  mvcc_snapshot = logtb_get_mvcc_snapshot (thread_p);
  if (mvcc_snapshot == NULL)
    {
      error_code = er_errid ();
      return (error_code == NO_ERROR ? ER_FAILED : error_code);
    }

  db_make_null (&null_value);
  db_make_null (&key_val_range.key1);
  db_make_null (&key_val_range.key2);

  heap_attrinfo_start (thread_p, NULL, 0, NULL, &attr_info);

  for (fkref = index->fk; fkref != NULL; fkref = fkref->next)
    {
      if (fkref->del_action == SM_FOREIGN_KEY_RESTRICT || fkref->del_action == SM_FOREIGN_KEY_NO_ACTION)
	{
	  if (!LOG_CHECK_LOG_APPLIER (thread_p))
	    {
	      error_code = btree_find_foreign_key (thread_p, &fkref->self_btid, key, &fkref->self_oid, &found_oid);
	      if (error_code != NO_ERROR)
		{
		  assert (er_errid () != NO_ERROR);
		  goto error3;
		}
	      if (!OID_ISNULL (&found_oid))
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FK_RESTRICT, 1, fkref->fkname);
		  error_code = ER_FK_RESTRICT;
		  /* Unlock child object. */
		  lock_unlock_object_donot_move_to_non2pl (thread_p, &found_oid, &fkref->self_oid, S_LOCK);
		  goto error3;
		}
	    }
	}
      else if (fkref->del_action == SM_FOREIGN_KEY_CASCADE || fkref->del_action == SM_FOREIGN_KEY_SET_NULL)
	{
	  if (attr_ids)
	    {
	      db_private_free_and_init (thread_p, attr_ids);
	    }
	  error_code =
	    heap_get_indexinfo_of_btid (thread_p, &fkref->self_oid, &fkref->self_btid, NULL, &num_attrs, &attr_ids,
					&keys_prefix_length, NULL, NULL);
	  if (error_code != NO_ERROR)
	    {
	      goto error3;
	    }
	  assert (num_attrs == index->n_atts);
	  /* We might check for foreign key and schema consistency problems here but we rely on the schema manager to
	   * prevent inconsistency; see do_check_fk_constraints() for details */

	  error_code = heap_get_hfid_from_class_oid (thread_p, &fkref->self_oid, &hfid);
	  if (error_code != NO_ERROR)
	    {
	      goto error3;
	    }

	  if (oid_list.oidp == NULL)
	    {
	      oid_list.oidp = (OID *) db_private_alloc (thread_p, ISCAN_OID_BUFFER_CAPACITY);
	      if (oid_list.oidp == NULL)
		{
		  error_code = ER_OUT_OF_VIRTUAL_MEMORY;
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 1, ISCAN_OID_BUFFER_CAPACITY);
		  goto error3;
		}
	      oid_list.oid_cnt = 0;
	      oid_list.capacity = ISCAN_OID_BUFFER_CAPACITY / OR_OID_SIZE;
	      oid_list.max_oid_cnt = oid_list.capacity;
	      oid_list.next_list = NULL;
	    }

	  error_code = heap_scancache_start (thread_p, &isid.scan_cache, &hfid, &fkref->self_oid, true, true, NULL);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      goto error3;
	    }
	  scan_init_index_scan (&isid, &oid_list, mvcc_snapshot);
	  is_upd_scan_init = false;
	  pr_clone_value (key, &key_val_range.key1);
	  pr_clone_value (key, &key_val_range.key2);
	  key_val_range.range = GE_LE;
	  key_val_range.num_index_term = 0;
	  BTREE_INIT_SCAN (&bt_scan);

	  do
	    {
	      bool lob_exist = false;

	      error_code =
		btree_prepare_bts (thread_p, &bt_scan, &fkref->self_btid, &isid, &key_val_range, NULL, &fkref->self_oid,
				   NULL, NULL, false, NULL);
	      if (error_code != NO_ERROR)
		{
		  assert (er_errid () != NO_ERROR);
		  goto error2;
		}
	      error_code = btree_range_scan (thread_p, &bt_scan, btree_range_scan_select_visible_oids);
	      if (error_code != NO_ERROR)
		{
		  assert (er_errid () != NO_ERROR);
		  goto error2;
		}
	      oid_cnt = bt_scan.n_oids_read_last_iteration;

	      if (oid_cnt < 0)
		{
		  assert (false);
		  error_code = ER_FAILED;
		  goto error2;
		}
	      else if (oid_cnt == 0)
		{
		  break;
		}

	      if (is_upd_scan_init == false)
		{
		  int op_type = -1;
		  if (fkref->del_action == SM_FOREIGN_KEY_CASCADE)
		    {
		      op_type = SINGLE_ROW_DELETE;
		    }
		  else if (fkref->del_action == SM_FOREIGN_KEY_SET_NULL)
		    {
		      op_type = SINGLE_ROW_UPDATE;
		    }
		  else
		    {
		      assert (false);
		    }
		  error_code =
		    heap_scancache_start_modify (thread_p, &scan_cache, &hfid, &fkref->self_oid, op_type, NULL);
		  if (error_code != NO_ERROR)
		    {
		      goto error2;
		    }
		  is_upd_scan_init = true;
		  if (fkref->del_action == SM_FOREIGN_KEY_CASCADE || fkref->del_action == SM_FOREIGN_KEY_SET_NULL)
		    {
		      HEAP_ATTRVALUE *value;
		      error_code = heap_attrinfo_start (thread_p, &fkref->self_oid, -1, NULL, &attr_info);
		      if (error_code != NO_ERROR)
			{
			  goto error1;
			}

		      for (i = 0; i < attr_info.num_values; i++)
			{
			  value = &attr_info.values[i];
			  if (value->last_attrepr->type == DB_TYPE_BLOB || value->last_attrepr->type == DB_TYPE_CLOB)
			    {
			      lob_exist = true;
			      break;
			    }
			}
		    }
		}

	      for (i = 0; i < oid_cnt; i++)
		{
		  OID *oid_ptr = &(oid_list.oidp[i]);
		  SCAN_CODE scan_code = S_SUCCESS;
		  recdes.data = NULL;
		  /* TO DO - handle reevaluation */

		  scan_code = locator_lock_and_get_object (thread_p, oid_ptr, &fkref->self_oid, &recdes, &scan_cache,
							   X_LOCK, COPY, NULL_CHN, LOG_ERROR_IF_DELETED);
		  if (scan_code != S_SUCCESS)
		    {
		      if (scan_code == S_DOESNT_EXIST && er_errid () != ER_HEAP_UNKNOWN_OBJECT)
			{
			  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_UNKNOWN_OBJECT, oid_ptr->volid,
				  oid_ptr->pageid, oid_ptr->slotid);
			}

		      if (er_errid () == ER_HEAP_UNKNOWN_OBJECT)
			{
			  er_log_debug (ARG_FILE_LINE, "locator_update_force: unknown oid ( %d|%d|%d )\n",
					oid_ptr->pageid, oid_ptr->slotid, oid_ptr->volid);
			  continue;
			}

		      error_code = er_errid ();
		      error_code = (error_code == NO_ERROR ? ER_FAILED : error_code);
		      goto error1;
		    }

		  if (fkref->del_action == SM_FOREIGN_KEY_CASCADE)
		    {
		      if (lob_exist)
			{
			  error_code = locator_delete_lob_force (thread_p, &fkref->self_oid, oid_ptr, NULL);
			}
		      if (error_code != NO_ERROR)
			{
			  goto error1;
			}

		      /* oid already locked at locator_lock_and_get_object */
		      error_code =
			locator_delete_force (thread_p, &hfid, oid_ptr, true, SINGLE_ROW_DELETE, &scan_cache,
					      &force_count, NULL, false);
		      if (error_code == ER_MVCC_NOT_SATISFIED_REEVALUATION)
			{
			  /* skip foreign keys that were already deleted. For example the "cross type" reference */
			  error_code = NO_ERROR;
			}
		      else if (error_code != NO_ERROR)
			{
			  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FK_CANT_DELETE_INSTANCE, 1, fkref->fkname);
			  goto error1;
			}
		    }
		  else if (fkref->del_action == SM_FOREIGN_KEY_SET_NULL)
		    {
		      error_code = heap_attrinfo_clear_dbvalues (&attr_info);
		      if (error_code != NO_ERROR)
			{
			  goto error1;
			}
		      for (k = 0; k < num_attrs; ++k)
			{
			  error_code = heap_attrinfo_set (oid_ptr, attr_ids[k], &null_value, &attr_info);
			  if (error_code != NO_ERROR)
			    {
			      goto error1;
			    }
			}
		      /* oid already locked at locator_lock_and_get_object */
		      error_code =
			locator_attribute_info_force (thread_p, &hfid, oid_ptr, &attr_info, attr_ids, index->n_atts,
						      LC_FLUSH_UPDATE, SINGLE_ROW_UPDATE, &scan_cache, &force_count,
						      false, REPL_INFO_TYPE_RBR_NORMAL, DB_NOT_PARTITIONED_CLASS, NULL,
						      NULL, NULL, UPDATE_INPLACE_NONE, &recdes, false);
		      if (error_code != NO_ERROR)
			{
			  if (error_code == ER_MVCC_NOT_SATISFIED_REEVALUATION)
			    {
			      error_code = NO_ERROR;
			    }
			  else
			    {
			      goto error1;
			    }
			}
		    }
		  else
		    {
		      assert (false);
		    }
		}
	    }
	  while (!BTREE_END_OF_SCAN (&bt_scan));

	  if (is_upd_scan_init)
	    {
	      heap_scancache_end_modify (thread_p, &scan_cache);
	      if (fkref->del_action == SM_FOREIGN_KEY_CASCADE || fkref->del_action == SM_FOREIGN_KEY_SET_NULL)
		{
		  heap_attrinfo_end (thread_p, &attr_info);
		}
	    }

	  btree_scan_clear_key (&bt_scan);
	  pr_clear_value (&key_val_range.key1);
	  pr_clear_value (&key_val_range.key2);
	  error_code = heap_scancache_end (thread_p, &isid.scan_cache);
	  if (error_code != NO_ERROR)
	    {
	      error_code = NO_ERROR;
	      goto end;
	    }
	}
      else
	{
	  assert (false);
	}
    }

end:
  if (oid_list.oidp)
    {
      db_private_free_and_init (thread_p, oid_list.oidp);
    }
  if (attr_ids)
    {
      db_private_free_and_init (thread_p, attr_ids);
    }
  if (keys_prefix_length)
    {
      db_private_free_and_init (thread_p, keys_prefix_length);
    }

  return error_code;

error1:
  heap_scancache_end_modify (thread_p, &scan_cache);

error2:
  btree_scan_clear_key (&bt_scan);
  pr_clear_value (&key_val_range.key1);
  pr_clear_value (&key_val_range.key2);
  (void) heap_scancache_end (thread_p, &isid.scan_cache);

error3:
  heap_attrinfo_end (thread_p, &attr_info);

  goto end;
}

/*
 * locator_check_primary_key_update () -
 *
 * return: NO_ERROR if all OK, ER_ status otherwise
 *
 *   index(in):
 *   key(in):
 */
static int
locator_check_primary_key_update (THREAD_ENTRY * thread_p, OR_INDEX * index, DB_VALUE * key)
{
  OR_FOREIGN_KEY *fkref;
  int oid_cnt, force_count, i;
  RECDES recdes;
  HEAP_SCANCACHE scan_cache;
  HFID hfid;
  BTREE_SCAN bt_scan;
  INDX_SCAN_ID isid;
  KEY_VAL_RANGE key_val_range;
  bool is_upd_scan_init;
  int error_code = NO_ERROR;
  HEAP_CACHE_ATTRINFO attr_info;
  DB_VALUE null_value;
  ATTR_ID *attr_ids = NULL;
  int num_attrs = 0;
  int k;
  int *keys_prefix_length = NULL;
  MVCC_SNAPSHOT *mvcc_snapshot = NULL;
  OID found_oid;
  BTREE_ISCAN_OID_LIST oid_list;

  oid_list.oidp = NULL;

  mvcc_snapshot = logtb_get_mvcc_snapshot (thread_p);
  if (mvcc_snapshot == NULL)
    {
      error_code = er_errid ();
      return (error_code == NO_ERROR ? ER_FAILED : error_code);
    }

  db_make_null (&key_val_range.key1);
  db_make_null (&key_val_range.key2);
  db_make_null (&null_value);
  heap_attrinfo_start (thread_p, NULL, 0, NULL, &attr_info);

  for (fkref = index->fk; fkref != NULL; fkref = fkref->next)
    {
      if (fkref->upd_action == SM_FOREIGN_KEY_RESTRICT || fkref->upd_action == SM_FOREIGN_KEY_NO_ACTION)
	{
	  if (!LOG_CHECK_LOG_APPLIER (thread_p))
	    {
	      error_code = btree_find_foreign_key (thread_p, &fkref->self_btid, key, &fkref->self_oid, &found_oid);
	      if (error_code != NO_ERROR)
		{
		  assert (er_errid () != NO_ERROR);
		  goto error3;
		}
	      if (!OID_ISNULL (&found_oid))
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FK_RESTRICT, 1, fkref->fkname);
		  error_code = ER_FK_RESTRICT;
		  /* Unlock child object. */
		  lock_unlock_object_donot_move_to_non2pl (thread_p, &found_oid, &fkref->self_oid, S_LOCK);
		  goto error3;
		}
	    }
	}
      else if (fkref->upd_action == SM_FOREIGN_KEY_CASCADE || fkref->upd_action == SM_FOREIGN_KEY_SET_NULL)
	{
	  if (attr_ids)
	    {
	      db_private_free_and_init (thread_p, attr_ids);
	    }
	  error_code =
	    heap_get_indexinfo_of_btid (thread_p, &fkref->self_oid, &fkref->self_btid, NULL, &num_attrs, &attr_ids,
					&keys_prefix_length, NULL, NULL);
	  if (error_code != NO_ERROR)
	    {
	      goto error3;
	    }
	  assert (num_attrs == index->n_atts);
	  /* We might check for foreign key and schema consistency problems here but we rely on the schema manager to
	   * prevent inconsistency; see do_check_fk_constraints() for details */

	  error_code = heap_get_hfid_from_class_oid (thread_p, &fkref->self_oid, &hfid);
	  if (error_code != NO_ERROR)
	    {
	      goto error3;
	    }

	  if (oid_list.oidp == NULL)
	    {
	      oid_list.oidp = (OID *) db_private_alloc (thread_p, ISCAN_OID_BUFFER_CAPACITY);
	      if (oid_list.oidp == NULL)
		{
		  error_code = ER_OUT_OF_VIRTUAL_MEMORY;
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 1, ISCAN_OID_BUFFER_CAPACITY);
		  goto error3;
		}
	      oid_list.capacity = ISCAN_OID_BUFFER_CAPACITY / OR_OID_SIZE;
	      oid_list.max_oid_cnt = oid_list.capacity;
	      oid_list.oid_cnt = 0;
	      oid_list.next_list = NULL;
	    }

	  error_code = heap_scancache_start (thread_p, &isid.scan_cache, &hfid, &fkref->self_oid, true, true, NULL);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      goto error3;
	    }

	  scan_init_index_scan (&isid, &oid_list, mvcc_snapshot);

	  is_upd_scan_init = false;
	  pr_clone_value (key, &key_val_range.key1);
	  pr_clone_value (key, &key_val_range.key2);
	  key_val_range.range = GE_LE;
	  key_val_range.num_index_term = 0;
	  BTREE_INIT_SCAN (&bt_scan);

	  do
	    {
	      error_code =
		btree_prepare_bts (thread_p, &bt_scan, &fkref->self_btid, &isid, &key_val_range, NULL, &fkref->self_oid,
				   NULL, NULL, false, NULL);
	      if (error_code != NO_ERROR)
		{
		  assert (er_errid () != NO_ERROR);
		  goto error2;
		}
	      error_code = btree_range_scan (thread_p, &bt_scan, btree_range_scan_select_visible_oids);
	      if (error_code != NO_ERROR)
		{
		  assert (er_errid () != NO_ERROR);
		  goto error2;
		}
	      oid_cnt = bt_scan.n_oids_read_last_iteration;
	      if (oid_cnt < 0)
		{
		  assert (er_errid () != NO_ERROR);
		  error_code = er_errid ();
		  if (error_code == NO_ERROR)
		    {
		      error_code = ER_FAILED;
		    }

		  goto error2;
		}
	      else if (oid_cnt == 0)
		{
		  break;
		}

	      if (is_upd_scan_init == false)
		{
		  error_code =
		    heap_scancache_start_modify (thread_p, &scan_cache, &hfid, &fkref->self_oid, SINGLE_ROW_UPDATE,
						 NULL);
		  if (error_code != NO_ERROR)
		    {
		      goto error2;
		    }
		  is_upd_scan_init = true;
		  error_code = heap_attrinfo_start (thread_p, &fkref->self_oid, -1, NULL, &attr_info);
		  if (error_code != NO_ERROR)
		    {
		      goto error1;
		    }
		}

	      for (i = 0; i < oid_cnt; i++)
		{
		  OID *oid_ptr = &(oid_list.oidp[i]);
		  SCAN_CODE scan_code = S_SUCCESS;
		  recdes.data = NULL;
		  /* TO DO - handle reevaluation */

		  scan_code = locator_lock_and_get_object (thread_p, oid_ptr, &fkref->self_oid, &recdes, &scan_cache,
							   X_LOCK, COPY, NULL_CHN, LOG_ERROR_IF_DELETED);
		  if (scan_code != S_SUCCESS)
		    {
		      if (scan_code == S_DOESNT_EXIST && er_errid () != ER_HEAP_UNKNOWN_OBJECT)
			{
			  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_UNKNOWN_OBJECT, oid_ptr->volid,
				  oid_ptr->pageid, oid_ptr->slotid);
			}
		      if (er_errid () == ER_HEAP_UNKNOWN_OBJECT)
			{
			  er_log_debug (ARG_FILE_LINE, "locator_update_force: unknown oid ( %d|%d|%d )\n",
					oid_ptr->pageid, oid_ptr->slotid, oid_ptr->volid);
			  continue;
			}

		      error_code = er_errid ();
		      error_code = (error_code == NO_ERROR ? ER_FAILED : error_code);
		      goto error1;
		    }

		  if ((error_code = heap_attrinfo_clear_dbvalues (&attr_info)) != NO_ERROR)
		    {
		      goto error1;
		    }

		  if (fkref->upd_action == SM_FOREIGN_KEY_CASCADE)
		    {
		      /* This is not yet implemented and this code should not be reached. */
		      assert (false);
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FK_RESTRICT, 1, fkref->fkname);
		      error_code = ER_FK_RESTRICT;
		      goto error1;
		    }
		  else if (fkref->upd_action == SM_FOREIGN_KEY_SET_NULL)
		    {
		      for (k = 0; k < num_attrs; ++k)
			{
			  error_code = heap_attrinfo_set (oid_ptr, attr_ids[k], &null_value, &attr_info);
			  if (error_code != NO_ERROR)
			    {
			      goto error1;
			    }
			}
		    }
		  else
		    {
		      assert (false);
		    }
		  /* oid already locked at locator_lock_and_get_object */
		  error_code =
		    locator_attribute_info_force (thread_p, &hfid, oid_ptr, &attr_info, attr_ids, index->n_atts,
						  LC_FLUSH_UPDATE, SINGLE_ROW_UPDATE, &scan_cache, &force_count, false,
						  REPL_INFO_TYPE_RBR_NORMAL, DB_NOT_PARTITIONED_CLASS, NULL, NULL, NULL,
						  UPDATE_INPLACE_NONE, &recdes, false);
		  if (error_code != NO_ERROR)
		    {
		      if (error_code == ER_MVCC_NOT_SATISFIED_REEVALUATION)
			{
			  error_code = NO_ERROR;
			}
		      else
			{
			  goto error1;
			}
		    }
		}
	    }
	  while (!BTREE_END_OF_SCAN (&bt_scan));

	  if (is_upd_scan_init)
	    {
	      heap_scancache_end_modify (thread_p, &scan_cache);
	      heap_attrinfo_end (thread_p, &attr_info);
	    }

	  btree_scan_clear_key (&bt_scan);
	  pr_clear_value (&key_val_range.key1);
	  pr_clear_value (&key_val_range.key2);
	  error_code = heap_scancache_end (thread_p, &isid.scan_cache);
	  if (error_code != NO_ERROR)
	    {
	      error_code = NO_ERROR;
	      goto end;
	    }
	}
      else
	{
	  assert (false);
	}
    }

end:
  if (oid_list.oidp)
    {
      db_private_free_and_init (thread_p, oid_list.oidp);
    }
  if (attr_ids)
    {
      db_private_free_and_init (thread_p, attr_ids);
    }
  if (keys_prefix_length)
    {
      db_private_free_and_init (thread_p, keys_prefix_length);
    }

  return error_code;

error1:
  heap_scancache_end_modify (thread_p, &scan_cache);

error2:
  btree_scan_clear_key (&bt_scan);
  pr_clear_value (&key_val_range.key1);
  pr_clear_value (&key_val_range.key2);
  (void) heap_scancache_end (thread_p, &isid.scan_cache);

error3:
  heap_attrinfo_end (thread_p, &attr_info);

  goto end;
}

/*
 * locator_insert_force () - Insert the given object on this heap
 *
 * return: NO_ERROR if all OK, ER_ status otherwise
 *
 *   hfid(in): Heap where the object is going to be inserted
 *   class_oid(in/out): the class OID in which it was inserted
 *		(in case of a partitioned class: it will hold the class OID of
 *		 the actual partition in which the record was inserted)
 *   oid(in/out): The new object identifier
 *   recdes(in): The object in disk format
 *   has_index(in): false if we now for sure that there is not any index on the
 *              instances of the class.
 *   op_type(in):
 *   scan_cache(in/out): Scan cache used to estimate the best space pages
 *              between heap changes.
 *   force_count(in):
 *   pruning_type(in): type of pruning that should be performed
 *   pcontext(in): partition pruning context
 *   func_preds(in): cached function index expressions
 *   force_in_place:
 *
 * Note: The given object is inserted on this heap and all appropriate
 *              index entries are inserted.
 */
static int
locator_insert_force (THREAD_ENTRY * thread_p, HFID * hfid, OID * class_oid, OID * oid, RECDES * recdes, int has_index,
		      int op_type, HEAP_SCANCACHE * scan_cache, int *force_count, int pruning_type,
		      PRUNING_CONTEXT * pcontext, FUNC_PRED_UNPACK_INFO * func_preds,
		      UPDATE_INPLACE_STYLE force_in_place)
{
#if 0				/* TODO - dead code; do not delete me */
  OID rep_dir = { NULL_PAGEID, NULL_SLOTID, NULL_VOLID };
#endif
  bool isold_object;		/* Make sure that this is an old object */
  RECDES new_recdes;
  bool is_cached = false;
  LC_COPYAREA *cache_attr_copyarea = NULL;
  int error_code = NO_ERROR;
  OID real_class_oid;
  HFID real_hfid;
  HEAP_SCANCACHE *local_scan_cache = NULL;
  FUNC_PRED_UNPACK_INFO *local_func_preds = NULL;
  OID null_oid = { NULL_PAGEID, NULL_SLOTID, NULL_VOLID };
  HEAP_OPERATION_CONTEXT context;

  assert (class_oid != NULL);
  assert (!OID_ISNULL (class_oid));
  assert (!OID_IS_ROOTOID (class_oid));

  HFID_COPY (&real_hfid, hfid);
  COPY_OID (&real_class_oid, class_oid);

  local_scan_cache = scan_cache;
  local_func_preds = func_preds;

  if (pruning_type != DB_NOT_PARTITIONED_CLASS)
    {
      OID superclass_oid;
      int granted;
      /* Perform partition pruning on the given class */
      error_code =
	partition_prune_insert (thread_p, class_oid, recdes, scan_cache, pcontext, pruning_type, &real_class_oid,
				&real_hfid, &superclass_oid);
      if (error_code != NO_ERROR)
	{
	  goto error2;
	}
      if (!OID_ISNULL (&superclass_oid))
	{
	  granted = lock_subclass (thread_p, &real_class_oid, &superclass_oid, IX_LOCK, LK_UNCOND_LOCK);
	  if (granted != LK_GRANTED)
	    {
	      assert (er_errid () != NO_ERROR);
	      error_code = er_errid ();
	      if (error_code == NO_ERROR)
		{
		  error_code = ER_FAILED;
		}
	      goto error2;
	    }
	  if (pcontext != NULL)
	    {
	      /* The scan_cache above is started for the partitioned class, not for the actual partition in which we
	       * will be performing the insert. See if we already have a scan_cache structure created for the target
	       * partition and use that one instead of the one supplied to this function */
	      PRUNING_SCAN_CACHE *ins_cache = NULL;
	      bool has_func_idx = (func_preds != NULL);

	      ins_cache =
		locator_get_partition_scancache (pcontext, &real_class_oid, &real_hfid, op_type, has_func_idx);
	      if (ins_cache == NULL)
		{
		  assert (er_errid () != NO_ERROR);
		  error_code = er_errid ();
		  if (error_code == NO_ERROR)
		    {
		      error_code = ER_FAILED;
		    }
		  goto error2;
		}
	      local_func_preds = ins_cache->func_index_pred;
	      local_scan_cache = &ins_cache->scan_cache;
	    }
	  else
	    {
	      /* disable function indexes optimization if we don't have access to a pruning context */
	      local_func_preds = NULL;
	    }
	}
      else
	{
	  /* class_oid was not a partitioned class. This can happen if, for example, this is a request from HA. In this 
	   * which case class_oid already points to the designated partition */
	  assert_release (OID_EQ (class_oid, &real_class_oid));
	}
    }

  *force_count = 0;

  /* 
   * This is a new object. The object must be locked in exclusive mode,
   * once its OID is assigned. We just do it for the classes, the new
   * instances are not locked since another client cannot get to them,
   * in any way. How can a client know their OIDs
   */

  /* insert object and lock it */

  assert (!OID_IS_ROOTOID (&real_class_oid));

  /* adjust recdes type (if we got here it should be REC_HOME or REC_BIGONE; REC_BIGONE is detected and handled in
   * heap_insert_logical */
  recdes->type = REC_HOME;

  /* prepare context */
  heap_create_insert_context (&context, &real_hfid, &real_class_oid, recdes, local_scan_cache);
  context.update_in_place = force_in_place;

  if (force_in_place == UPDATE_INPLACE_OLD_MVCCID)
    {
      REPR_ID rep;

      /* insert due to redistribute partition data - set the correct representation id of the new class */

      rep = heap_get_class_repr_id (thread_p, &real_class_oid);
      (void) or_replace_rep_id (context.recdes_p, rep);
    }

  /* execute insert */
  if (heap_insert_logical (thread_p, &context) != NO_ERROR)
    {
      /* 
       * Problems inserting the object...Maybe, the transaction should be
       * aborted by the caller...Quit..
       */
      assert (er_errid () != NO_ERROR);
      error_code = er_errid ();
      if (error_code == NO_ERROR)
	{
	  error_code = ER_FAILED;
	}
      goto error2;
    }
  COPY_OID (oid, &context.res_oid);

#if 0				/* TODO - dead code; do not delete me */
  if (OID_IS_ROOTOID (&real_class_oid))
    {
      assert (false);		/* is impossible */

      /* 
       * A CLASS: Add the classname to class_OID entry and add the class
       *          to the catalog.
       *          Update the classname table.
       *          Remove XASL cache entries which is relevant with that class.
       */

      classname = or_class_name (recdes);
      assert (classname != NULL);
      assert (strlen (classname) < 255);

      /* Indicate new oid to classname table */
      if (locator_permoid_class_name (thread_p, classname, oid) != NO_ERROR)
	{
	  assert (false);	/* should be impossible */
	  goto error1;
	}

      if (!OID_IS_ROOTOID (oid))
	{
	  HEAP_OPERATION_CONTEXT update_context;
	  char *rep_dir_offset;

	  or_class_rep_dir (recdes, &rep_dir);
	  assert (OID_ISNULL (&rep_dir));

	  if (catalog_insert (thread_p, recdes, oid, &rep_dir) < 0)
	    {
	      /* 
	       * There is an error inserting the hash entry or catalog
	       * information. Maybe, the transaction should be aborted by
	       * the caller...Quit
	       */
	      assert (er_errid () != NO_ERROR);
	      error_code = er_errid ();
	      if (error_code == NO_ERROR)
		{
		  error_code = ER_FAILED;
		}
	      goto error1;
	    }

	  assert (!OID_ISNULL (&rep_dir));

	  /* save oid of the representation directory */
	  rep_dir_offset =
	    (char *) recdes->data + OR_FIXED_ATTRIBUTES_OFFSET (recdes->data,
								ORC_CLASS_VAR_ATT_COUNT) + ORC_REP_DIR_OFFSET;

	  OR_PUT_OID (rep_dir_offset, &rep_dir);

	  heap_create_update_context (&update_context, &real_hfid, oid, &real_class_oid, recdes, local_scan_cache);
	  update_context.force_non_mvcc = true;

	  if (heap_update_logical (thread_p, &update_context) != NO_ERROR)
	    {
	      /* 
	       * Problems updating the object...Maybe, the transaction should be
	       * aborted by the caller...Quit..
	       */
	      error_code = er_errid ();
	      if (error_code == NO_ERROR)
		{
		  error_code = ER_FAILED;
		}
	      goto error1;
	    }

	  assert (isold_object == true);

#if !defined(NDEBUG)
	  or_class_rep_dir (recdes, &rep_dir);
	  assert (!OID_ISNULL (&rep_dir));
#endif
	}

      if (catcls_Enable == true && catcls_insert_catalog_classes (thread_p, recdes) != NO_ERROR)
	{
	  assert (er_errid () != NO_ERROR);
	  error_code = er_errid ();
	  if (error_code == NO_ERROR)
	    {
	      error_code = ER_FAILED;
	    }
	  goto error1;
	}

      /* system class do not include foreign keys, we need not check here. */
    }
  else
    {
#endif
      /* 
       * AN INSTANCE: Apply the necessary index insertions
       */
      if (has_index
	  && locator_add_or_remove_index (thread_p, recdes, oid, &real_class_oid, true, op_type, local_scan_cache, true,
					  true, &real_hfid, local_func_preds) != NO_ERROR)
	{
	  assert (er_errid () != NO_ERROR);
	  error_code = er_errid ();
	  if (error_code == NO_ERROR)
	    {
	      error_code = ER_FAILED;
	    }
	  goto error1;
	}

      /* check the foreign key constraints */
      if (has_index && !locator_Dont_check_foreign_key)
	{
	  error_code =
	    locator_check_foreign_key (thread_p, &real_hfid, &real_class_oid, oid, recdes, &new_recdes, &is_cached,
				       &cache_attr_copyarea);
	  if (error_code != NO_ERROR)
	    {
	      goto error1;
	    }

	  if (is_cached)
	    {
	      HEAP_OPERATION_CONTEXT update_context;

	      recdes = &new_recdes;
	      /* Cache object has been updated, we need update the value again */
	      heap_create_update_context (&update_context, &real_hfid, oid, &real_class_oid, recdes, local_scan_cache,
					  UPDATE_INPLACE_CURRENT_MVCCID);
	      if (heap_update_logical (thread_p, &update_context) != NO_ERROR)
		{
		  assert (er_errid () != NO_ERROR);
		  error_code = er_errid ();
		  if (error_code == NO_ERROR)
		    {
		      error_code = ER_FAILED;
		    }
		  goto error1;
		}

	      assert (update_context.is_logical_old);
	      isold_object = update_context.is_logical_old;
	    }
	}

#if defined(ENABLE_UNUSED_FUNCTION)
      /* increase the counter of the catalog */
      locator_increase_catalog_count (thread_p, &real_class_oid);
#endif

      /* remove query result cache entries which are relevant with this class */
      if (!QFILE_IS_LIST_CACHE_DISABLED)
	{
	  if (qexec_clear_list_cache_by_class (thread_p, &real_class_oid) != NO_ERROR)
	    {
	      er_log_debug (ARG_FILE_LINE,
			    "locator_insert_force: qexec_clear_list_cache_by_class failed for class { %d %d %d }\n",
			    real_class_oid.pageid, real_class_oid.slotid, real_class_oid.volid);
	    }
	  qmgr_add_modified_class (thread_p, &real_class_oid);
	}
#if 0				/* TODO - dead code; do not delete me */
    }
#endif

  *force_count = 1;

error1:
  /* update the OID of the class with the actual partition in which the object was inserted */
  COPY_OID (class_oid, &real_class_oid);
  HFID_COPY (hfid, &real_hfid);

error2:
  if (cache_attr_copyarea != NULL)
    {
      locator_free_copy_area (cache_attr_copyarea);
    }

  return error_code;
}

/*
 * locator_move_record () - relocate a record from a partitioned class
 * return : error code or NO_ERROR
 * thread_p (in)	: caller thread
 * old_hfid (in)	: source location of the record
 * old_class_oid (in)	: class owning the record
 * obj_oid (in)		: record OID
 * new_class_oid (in)	: destination class
 * new_class_hfid (in)	: destination hfid
 * recdes (in)		: record
 * scan_cache (in)	: scan cache
 * op_type (in)		: operation type
 * has_index (in)	: true if the class has indexes
 * force_count (in/out)	:
 * context(in)	        : pruning context
 * mvcc_reev_data(in)	: MVCC reevaluation data
 * need_locking(in)	: true, if need locking
 *
 * Note: this function calls locator_delete_force on the current object oid
 * and locator_insert_force for the RECDES it receives. The record has already
 * been set to be used by the receiving class when it went through the pruning
 * algorithm (see function partition_find_partition_for_record)
 */
static int
locator_move_record (THREAD_ENTRY * thread_p, HFID * old_hfid, OID * old_class_oid, OID * obj_oid, OID * new_class_oid,
		     HFID * new_class_hfid, RECDES * recdes, HEAP_SCANCACHE * scan_cache, int op_type, int has_index,
		     int *force_count, PRUNING_CONTEXT * context, MVCC_REEV_DATA * mvcc_reev_data, bool need_locking)
{
  int error = NO_ERROR;
  OID new_obj_oid;

  assert (!OID_IS_ROOTOID (old_class_oid));
  assert (!OID_IS_ROOTOID (new_class_oid));

  if (context != NULL)
    {
      /* setup a PRUNING_SCAN_CACHE object for this class */
      HEAP_SCANCACHE *insert_cache = NULL;
      PRUNING_SCAN_CACHE *ins_cache = NULL;
      ins_cache = locator_get_partition_scancache (context, new_class_oid, new_class_hfid, op_type, false);
      if (ins_cache == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	  return (error != NO_ERROR) ? error : ER_FAILED;
	}

      insert_cache = &ins_cache->scan_cache;

      error =
	locator_insert_force (thread_p, new_class_hfid, new_class_oid, &new_obj_oid, recdes, has_index, op_type,
			      insert_cache, force_count, context->pruning_type, NULL, NULL, UPDATE_INPLACE_NONE);
    }
  else
    {
      HEAP_SCANCACHE insert_cache;

      error =
	locator_start_force_scan_cache (thread_p, &insert_cache, new_class_hfid, new_class_oid, SINGLE_ROW_INSERT);
      if (error != NO_ERROR)
	{
	  return error;
	}

      /* insert the new record */
      error =
	locator_insert_force (thread_p, new_class_hfid, new_class_oid, &new_obj_oid, recdes, has_index, op_type,
			      &insert_cache, force_count, DB_NOT_PARTITIONED_CLASS, NULL, NULL, UPDATE_INPLACE_NONE);
      heap_scancache_end (thread_p, &insert_cache);
    }

  if (error != NO_ERROR)
    {
      return error;
    }

  /* delete this record from the class it currently resides in */
  error =
    locator_delete_force_for_moving (thread_p, old_hfid, obj_oid, true, op_type, scan_cache, force_count,
				     mvcc_reev_data, &new_obj_oid, new_class_oid, need_locking);
  if (error != NO_ERROR)
    {
      return error;
    }

  COPY_OID (obj_oid, &new_obj_oid);

  return NO_ERROR;
}

/*
 * locator_update_force () - Update the given object
 *
 * return: NO_ERROR if all OK, ER_ status otherwise
 *
 *   hfid(in): Heap where the object is going to be inserted
 *   class_oid(in):
 *   oid(in): The object identifier
 *   oldrecdes(in):
 *   recdes(in):  The object in disk format
 *   has_index(in): false if we now for sure that there is not any index
 *                   on the instances of the class.
 *   att_id(in): Updated attr id array
 *   n_att_id(in): Updated attr id array length
 *   op_type(in):
 *   scan_cache(in/out): Scan cache used to estimate the best space pages
 *                   between heap changes.
 *   force_count(in):
 *   not_check_fk(in):
 *   repl_inf(in): replication info
 *   pruning_type(in): pruning type
 *   pcontext(in): pruning context
 *   mvcc_reev_data(in): MVCC reevaluation data
 *   force_in_place(in): if UPDATE_INPLACE_NONE then the 'in place' will not be forced
 *			 and the update style will be decided in this function.
 *			 Otherwise the update of the instance will be made in
 *			 place and according to provided style.
 *
 * Note: The given object is updated on this heap and all appropriate
 *              index entries are updated.
 */
static int
locator_update_force (THREAD_ENTRY * thread_p, HFID * hfid, OID * class_oid, OID * oid, RECDES * oldrecdes,
		      RECDES * recdes, int has_index, ATTR_ID * att_id, int n_att_id, int op_type,
		      HEAP_SCANCACHE * scan_cache, int *force_count, bool not_check_fk, REPL_INFO_TYPE repl_info_type,
		      int pruning_type, PRUNING_CONTEXT * pcontext, MVCC_REEV_DATA * mvcc_reev_data,
		      UPDATE_INPLACE_STYLE force_in_place, bool need_locking)
{
  OID rep_dir = { NULL_PAGEID, NULL_SLOTID, NULL_VOLID };
  char *rep_dir_offset;
  char *classname = NULL;	/* Classname to update */
  char *old_classname = NULL;	/* Classname that may have been renamed */

  bool isold_object;		/* Make sure that this is an old object */
  RECDES copy_recdes;
  SCAN_CODE scan = S_SUCCESS;

  RECDES new_record;
  bool is_cached = false;
  LC_COPYAREA *cache_attr_copyarea = NULL;
  int error_code = NO_ERROR;
  HEAP_SCANCACHE *local_scan_cache;
  bool no_data_new_address = false;
  REPL_INFO repl_info;

  assert (class_oid != NULL && !OID_ISNULL (class_oid));

  /* 
   * While scanning objects, the given scancache does not fix the last
   * accessed page. So, the object must be copied to the record descriptor.
   */
  copy_recdes.data = NULL;

  *force_count = 0;

  repl_info.repl_info_type = repl_info_type;
  repl_info.need_replication = true;
  repl_info.info = NULL;

  if (OID_IS_ROOTOID (class_oid))
    {
      HEAP_OPERATION_CONTEXT update_context;

      /* 
       * A CLASS: classes do not have any indices...however, the classname
       * to oid table may need to be updated
       */
      classname = or_class_name (recdes);
      assert (classname != NULL);
      assert (strlen (classname) < 255);

      old_classname = heap_get_class_name_alloc_if_diff (thread_p, oid, classname);

      /* 
       * Compare the classname pointers. If the same pointers classes are the
       * same since the class was no malloc
       */
      if (old_classname != NULL && old_classname != classname)
	{
	  assert (old_classname != NULL);
	  assert (strlen (old_classname) < 255);

	  /* Different names, the class was renamed. */
	  error_code = log_add_to_modified_class_list (thread_p, old_classname, oid);
	  if (error_code != NO_ERROR)
	    {
	      goto error;
	    }
	}

      if ((catcls_Enable == true) && (old_classname != NULL))
	{
	  error_code = catcls_update_catalog_classes (thread_p, old_classname, recdes, oid, force_in_place);
	  if (error_code != NO_ERROR)
	    {
	      goto error;
	    }
	}

      /* TODO - defence code to save oid of the representation directory */
      if (!OID_IS_ROOTOID (oid))
	{
	  or_class_rep_dir (recdes, &rep_dir);

	  if (OID_ISNULL (&rep_dir))
	    {
	      OID old_rep_dir = { NULL_PAGEID, NULL_SLOTID, NULL_VOLID };
	      RECDES old_record, *old_recdes;

	      old_record.data = NULL;
	      old_recdes = &old_record;

	      if (heap_get_class_record (thread_p, oid, old_recdes, scan_cache, PEEK) == S_SUCCESS)
		{
		  or_class_rep_dir (old_recdes, &old_rep_dir);

		  /* save current oid of the representation directory */
		  if (!OID_ISNULL (&old_rep_dir))
		    {
		      assert (false);	/* should avoid */

		      rep_dir_offset =
			(char *) recdes->data + OR_FIXED_ATTRIBUTES_OFFSET (recdes->data, ORC_CLASS_VAR_ATT_COUNT)
			+ ORC_REP_DIR_OFFSET;

		      OR_PUT_OID (rep_dir_offset, &old_rep_dir);
		    }
		}
	      else
		{
		  /* ignore if the class hasn't been flushed yet */
		  if (er_errid () == ER_HEAP_NODATA_NEWADDRESS)
		    {
		      er_clear ();	/* clear ER_HEAP_NODATA_NEWADDRESS */
		    }
		}
	    }
	}

      heap_create_update_context (&update_context, hfid, oid, class_oid, recdes, scan_cache,
				  UPDATE_INPLACE_CURRENT_MVCCID);
      error_code = heap_update_logical (thread_p, &update_context);
      if (error_code != NO_ERROR)
	{
	  /* 
	   * Problems updating the object...Maybe, the transaction should be
	   * aborted by the caller...Quit..
	   */
	  if (error_code == ER_FAILED)
	    {
	      ASSERT_ERROR_AND_SET (error_code);
	      assert (false);
	    }
	  else
	    {
	      ASSERT_ERROR ();
	    }

	  goto error;
	}
      isold_object = update_context.is_logical_old;

      if (update_context.is_logical_old)
	{
	  /* Update the catalog as long as it is not the root class */
	  if (!OID_IS_ROOTOID (oid))
	    {
#if !defined(NDEBUG)
	      or_class_rep_dir (recdes, &rep_dir);
	      assert (!OID_ISNULL (&rep_dir));
#endif
	      error_code = catalog_update (thread_p, recdes, oid);
	      if (error_code < 0)
		{
		  /* 
		   * An error occurred during the update of the catalog
		   */
		  goto error;
		}
	    }
	}
      else
	{
	  /* 
	   * NEW CLASS
	   */
	  if (!OID_IS_ROOTOID (oid))
	    {
	      HEAP_OPERATION_CONTEXT update_context;
	      or_class_rep_dir (recdes, &rep_dir);
	      assert (OID_ISNULL (&rep_dir));

	      if (catalog_insert (thread_p, recdes, oid, &rep_dir) < 0)
		{
		  /* 
		   * There is an error inserting the hash entry or catalog
		   * information. The transaction must be aborted by the caller
		   */
		  error_code = ER_FAILED;
		  goto error;
		}

	      assert (!OID_ISNULL (&rep_dir));

	      /* save oid of the representation directory */
	      rep_dir_offset =
		(char *) recdes->data + OR_FIXED_ATTRIBUTES_OFFSET (recdes->data, ORC_CLASS_VAR_ATT_COUNT)
		+ ORC_REP_DIR_OFFSET;

	      OR_PUT_OID (rep_dir_offset, &rep_dir);

	      heap_create_update_context (&update_context, hfid, oid, class_oid, recdes, scan_cache,
					  UPDATE_INPLACE_CURRENT_MVCCID);
	      error_code = heap_update_logical (thread_p, &update_context);
	      if (error_code != NO_ERROR)
		{
		  /* 
		   * Problems updating the object...Maybe, the transaction should be
		   * aborted by the caller...Quit..
		   */
		  if (error_code == ER_FAILED)
		    {
		      ASSERT_ERROR_AND_SET (error_code);
		      assert (false);
		    }
		  else
		    {
		      ASSERT_ERROR ();
		    }
		  goto error;
		}
	      isold_object = update_context.is_logical_old;

#if !defined(NDEBUG)
	      or_class_rep_dir (recdes, &rep_dir);
	      assert (!OID_ISNULL (&rep_dir));
#endif
	    }

	  if (catcls_Enable == true)
	    {
	      error_code = catcls_insert_catalog_classes (thread_p, recdes);
	      if (error_code != NO_ERROR)
		{
		  goto error;
		}
	    }
	}

      /* system class do not include foreign keys. we need not check here. */

      /* remove XASL cache entries which is relevant with that class */
      if (!OID_IS_ROOTOID (oid))
	{
	  xcache_remove_by_oid (thread_p, oid);
	}

      if (!OID_IS_ROOTOID (oid))
	{
	  fpcache_remove_by_class (thread_p, oid);
	}
    }
  else
    {
      HEAP_OPERATION_CONTEXT update_context;

      local_scan_cache = scan_cache;
      if (pruning_type != DB_NOT_PARTITIONED_CLASS && pcontext != NULL)
	{
	  /* Get a scan_cache object for the actual class which is updated. This object is kept in a list in the
	   * pruning context */
	  OID real_class_oid;
	  HFID real_hfid;
	  PRUNING_SCAN_CACHE *pcache;

	  HFID_COPY (&real_hfid, hfid);
	  COPY_OID (&real_class_oid, class_oid);
	  pcache = locator_get_partition_scancache (pcontext, &real_class_oid, &real_hfid, op_type, false);
	  if (pcache == NULL)
	    {
	      return ER_FAILED;
	    }
	  local_scan_cache = &pcache->scan_cache;
	}

      /* There will be no pruning after this point so we should reset op_type to a non pruning operation */

      if (!mvcc_is_mvcc_disabled_class (class_oid))
	{
	  if (oldrecdes == NULL)
	    {
	      copy_recdes.data = NULL;
	      if (mvcc_reev_data != NULL && mvcc_reev_data->type == REEV_DATA_UPDDEL)
		{
		  /* The new recdes can be changed during reevaluation. That's because new recdes fields may refer
		   * fields of old recdes */
		  mvcc_reev_data->upddel_reev_data->new_recdes = recdes;
		}

	      if (need_locking)
		{
		  scan = locator_lock_and_get_object_with_evaluation (thread_p, oid, class_oid, &copy_recdes,
								      local_scan_cache, COPY, NULL_CHN, mvcc_reev_data,
								      LOG_ERROR_IF_DELETED);
		}
	      else
		{
		  scan = heap_get_visible_version (thread_p, oid, class_oid, &copy_recdes, local_scan_cache, COPY,
						   NULL_CHN);
		}


	      if (scan == S_SUCCESS && mvcc_reev_data != NULL && mvcc_reev_data->filter_result == V_FALSE)
		{
		  return ER_MVCC_NOT_SATISFIED_REEVALUATION;
		}
	      else if (scan != S_SUCCESS)
		{
		  if (er_errid () == ER_HEAP_NODATA_NEWADDRESS)
		    {
		      force_in_place = UPDATE_INPLACE_CURRENT_MVCCID;

		      /* The object is a new instance, that is only the address (no content) is known by the heap
		       * manager. This is a normal behavior and, if we have an index, we need to add the object to the 
		       * index later. Because the following processing can remove this error, we save it here in
		       * no_data_new_address */
		      no_data_new_address = true;
		      er_clear ();	/* clear ER_HEAP_NODATA_NEWADDRESS */
		    }
		  else
		    {
		      if ((scan == S_DOESNT_EXIST || scan == S_SNAPSHOT_NOT_SATISFIED)
			  && er_errid () != ER_HEAP_UNKNOWN_OBJECT)
			{
			  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_UNKNOWN_OBJECT, oid->volid, oid->pageid,
				  oid->slotid);
			}
		      error_code = er_errid ();
		      if (error_code == ER_HEAP_UNKNOWN_OBJECT)
			{
			  er_log_debug (ARG_FILE_LINE, "locator_update_force: unknown oid ( %d|%d|%d )\n",
					oid->pageid, oid->slotid, oid->volid);
			}
		      else if (error_code == NO_ERROR)
			{
			  error_code = ER_FAILED;
			}

		      goto error;
		    }
		}
	      else
		{
		  oldrecdes = &copy_recdes;
		}
	    }

	  if (!HEAP_IS_UPDATE_INPLACE (force_in_place))
	    {
	      LOG_TDES *tdes;

	      tdes = LOG_FIND_CURRENT_TDES (thread_p);
	      if (!(has_index & LC_FLAG_HAS_UNIQUE_INDEX))
		{
		  MVCC_REC_HEADER old_rec_header;

		  or_mvcc_get_header (oldrecdes, &old_rec_header);
		  if (logtb_find_current_mvccid (thread_p) != old_rec_header.mvcc_ins_id)
		    {
#if defined (SERVER_MODE)
		      /* If not inserted by me, I must have lock. */
		      assert (lock_has_lock_on_object (oid, class_oid, thread_get_current_tran_index (), X_LOCK) > 0);
#endif /* SERVER_MODE */
		    }
		}
	    }
	  else if (force_in_place == UPDATE_INPLACE_OLD_MVCCID)
	    {
	      MVCC_REC_HEADER old_rec_header, new_rec_header;

	      if (or_mvcc_get_header (oldrecdes, &old_rec_header) != NO_ERROR
		  || or_mvcc_get_header (recdes, &new_rec_header) != NO_ERROR)
		{
		  goto error;
		}

	      if (MVCC_IS_FLAG_SET (&old_rec_header, OR_MVCC_FLAG_VALID_INSID))
		{
		  MVCC_SET_FLAG_BITS (&new_rec_header, OR_MVCC_FLAG_VALID_INSID);
		  MVCC_SET_INSID (&new_rec_header, MVCC_GET_INSID (&old_rec_header));
		}
	      else
		{
		  MVCC_CLEAR_FLAG_BITS (&new_rec_header, OR_MVCC_FLAG_VALID_INSID);
		}

	      if (MVCC_IS_HEADER_DELID_VALID (&old_rec_header))
		{
		  MVCC_SET_FLAG_BITS (&new_rec_header, OR_MVCC_FLAG_VALID_DELID);
		  MVCC_SET_DELID (&new_rec_header, MVCC_GET_DELID (&old_rec_header));
		}
	      else
		{
		  MVCC_CLEAR_FLAG_BITS (&new_rec_header, OR_MVCC_FLAG_VALID_DELID);
		}

	      if (MVCC_IS_FLAG_SET (&old_rec_header, OR_MVCC_FLAG_VALID_PREV_VERSION))
		{
		  MVCC_SET_FLAG_BITS (&new_rec_header, OR_MVCC_FLAG_VALID_PREV_VERSION);
		  MVCC_SET_PREVIOUS_VERSION_LSA (&new_rec_header, &MVCC_GET_PREV_VERSION_LSA (&old_rec_header));
		}
	      else
		{
		  MVCC_CLEAR_FLAG_BITS (&new_rec_header, OR_MVCC_FLAG_VALID_PREV_VERSION);
		}

	      if (or_mvcc_set_header (recdes, &new_rec_header) != NO_ERROR)
		{
		  goto error;
		}
	    }
	}
      else
	{
	  if (!HEAP_IS_UPDATE_INPLACE (force_in_place))
	    {
	      force_in_place = UPDATE_INPLACE_CURRENT_MVCCID;
	    }

	  if (lock_object (thread_p, oid, class_oid, X_LOCK, LK_UNCOND_LOCK) != LK_GRANTED)
	    {
	      ASSERT_ERROR_AND_SET (error_code);
	      goto error;
	    }

	  if (has_index && oldrecdes == NULL)
	    {
	      /* get the old record first */
	      local_scan_cache->mvcc_snapshot = logtb_get_mvcc_snapshot (thread_p);
	      if (local_scan_cache->mvcc_snapshot == NULL)
		{
		  error_code = er_errid ();
		  if (error_code == NO_ERROR)
		    {
		      error_code = ER_FAILED;
		    }
		  goto error;
		}

	      scan =
		heap_get_visible_version (thread_p, oid, class_oid, &copy_recdes, local_scan_cache, COPY, NULL_CHN);
	      if (scan == S_SUCCESS)
		{
		  oldrecdes = &copy_recdes;
		}
	      else if (er_errid () == ER_HEAP_NODATA_NEWADDRESS)
		{
		  er_clear ();	/* clear ER_HEAP_NODATA_NEWADDRESS */

		  /* The object is a new instance, that is only the address (no content) is known by the heap manager.
		   * This is a normal behaviour and, if we have an index, we need to add the object to the index later. 
		   * Because the following processing can remove this error, we save it here in no_data_new_address */
		  no_data_new_address = true;
		}
	      else
		{
		  if (er_errid () == ER_HEAP_UNKNOWN_OBJECT)
		    {
		      er_log_debug (ARG_FILE_LINE, "locator_update_force: unknown oid ( %d|%d|%d )\n", oid->pageid,
				    oid->slotid, oid->volid);
		    }

		  error_code = ER_HEAP_UNKNOWN_OBJECT;
		  goto error;
		}
	    }
	}

      if (pruning_type != DB_NOT_PARTITIONED_CLASS)
	{
	  OID superclass_oid;
	  OID real_class_oid;
	  HFID real_hfid;
	  int granted;

	  HFID_COPY (&real_hfid, hfid);
	  COPY_OID (&real_class_oid, class_oid);
	  error_code =
	    partition_prune_update (thread_p, class_oid, recdes, pcontext, pruning_type, &real_class_oid, &real_hfid,
				    &superclass_oid);
	  if (error_code != NO_ERROR)
	    {
	      goto error;
	    }

	  /* make sure we use the correct class oid - we could be dealing with a classoid resulted from a unique btid
	   * pruning */
	  if (heap_get_class_oid (thread_p, oid, class_oid) != S_SUCCESS)
	    {
	      ASSERT_ERROR_AND_SET (error_code);
	      goto error;
	    }

	  if (heap_get_hfid_from_class_oid (thread_p, class_oid, hfid) != NO_ERROR)
	    {
	      goto error;
	    }

	  if (!OID_EQ (class_oid, &real_class_oid))
	    {
	      /* If we have to move the record to another partition, we have to lock the target partition for insert.
	       * The class from which we delete was already locked (X_LOCK for heap scan or IX_LOCK for index scan)
	       * during the SELECT phase of UPDATE */
	      granted = lock_subclass (thread_p, &real_class_oid, &superclass_oid, IX_LOCK, LK_UNCOND_LOCK);
	      if (granted != LK_GRANTED)
		{
		  assert (er_errid () != NO_ERROR);
		  error_code = er_errid ();
		  if (error_code == NO_ERROR)
		    {
		      error_code = ER_FAILED;
		    }
		  goto error;
		}

	      error_code =
		locator_move_record (thread_p, hfid, class_oid, oid, &real_class_oid, &real_hfid, recdes, scan_cache,
				     op_type, has_index, force_count, pcontext, mvcc_reev_data, need_locking);
	      if (error_code == NO_ERROR)
		{
		  COPY_OID (class_oid, &real_class_oid);
		  HFID_COPY (hfid, &real_hfid);
		}
	      return error_code;
	    }
	}

      /* AN INSTANCE: Update indices if any */
      if (has_index)
	{
	  if (scan == S_SUCCESS)
	    {
	      error_code =
		locator_update_index (thread_p, recdes, oldrecdes, att_id, n_att_id, oid, class_oid, op_type,
				      local_scan_cache, &repl_info);
	      if (error_code != NO_ERROR)
		{
		  /* 
		   * There is an error updating the index... Quit... The
		   * transaction must be aborted by the caller
		   */
		  goto error;
		}
	    }
	  else
	    {
	      /* 
	       * We could not get the object.
	       * The object may be a new instance, that is only the address
	       * (no content) is known by the heap manager.
	       */

	      if (no_data_new_address)
		{
		  er_clear ();	/* clear the error code */
		  if (op_type == SINGLE_ROW_MODIFY)
		    {		/* to enable uniqueness checking */
		      op_type = SINGLE_ROW_INSERT;
		    }

		  error_code =
		    locator_add_or_remove_index (thread_p, recdes, oid, class_oid, true, op_type, local_scan_cache,
						 true, true, hfid, NULL);
		  if (error_code != NO_ERROR)
		    {
		      goto error;
		    }
		}
	    }

	  /* check the foreign key constraints */
	  if (!not_check_fk && !locator_Dont_check_foreign_key)
	    {
	      error_code =
		locator_check_foreign_key (thread_p, hfid, class_oid, oid, recdes, &new_record, &is_cached,
					   &cache_attr_copyarea);
	      if (error_code != NO_ERROR)
		{
		  goto error;
		}

	      if (is_cached)
		{
		  recdes = &new_record;
		}
	    }
	}

      heap_create_update_context (&update_context, hfid, oid, class_oid, recdes, local_scan_cache, force_in_place);
      error_code = heap_update_logical (thread_p, &update_context);
      if (error_code != NO_ERROR)
	{
	  /*
	   * Problems updating the object...Maybe, the transaction should be aborted by the caller...Quit..
	   */
	  if (error_code == ER_FAILED)
	    {
	      ASSERT_ERROR_AND_SET (error_code);
	      assert (error_code == ER_INTERRUPTED);
	    }
	  else
	    {
	      ASSERT_ERROR ();
	    }
	  goto error;
	}
      isold_object = update_context.is_logical_old;

      /* 
       * for replication,
       * We have to set UPDATE LSA number to the log info.
       * The target log info was already created when the locator_update_index
       */
      if (!LOG_CHECK_LOG_APPLIER (thread_p) && log_does_allow_replication () == true
	  && repl_info.need_replication == true)
	{
	  repl_add_update_lsa (thread_p, oid);
	}

#if defined(ENABLE_UNUSED_FUNCTION)
      if (isold_object == false)
	{
	  locator_increase_catalog_count (thread_p, class_oid);
	}
#endif

      /* remove query result cache entries which are relevant with this class */
      if (!QFILE_IS_LIST_CACHE_DISABLED)
	{
	  if (qexec_clear_list_cache_by_class (thread_p, class_oid) != NO_ERROR)
	    {
	      er_log_debug (ARG_FILE_LINE,
			    "locator_update_force: qexec_clear_list_cache_by_class failed for class { %d %d %d }\n",
			    class_oid->pageid, class_oid->slotid, class_oid->volid);
	    }
	  qmgr_add_modified_class (thread_p, class_oid);
	}
    }

  *force_count = 1;

error:

  if (old_classname != NULL && old_classname != classname)
    {
      free_and_init (old_classname);
    }

  if (cache_attr_copyarea != NULL)
    {
      locator_free_copy_area (cache_attr_copyarea);
    }

  return error_code;
}

/*
 * locator_delete_force () - Delete the given object
 *
 * return: NO_ERROR if all OK, ER_ status otherwise
 *
 *   hfid(in): Heap where the object is going to be inserted
 *   oid(in): The object identifier
 *   has_index(in): false if we now for sure that there is not any index
 *                   on the instances of the class.
 *   op_type(in):
 *   scan_cache(in/out): Scan cache used to estimate the best space pages
 *                   between heap changes.
 *   force_count(in):
 *   mvcc_reev_data(in): MVCC data
 *   idx_action_flag(in): is moving record between partitioned table? 
 *			  If FOR_MOVE, this delete&insert is caused by 
 *			  'UPDATE ... SET ...', NOT 'DELETE FROM ...'
 *   need_locking(in): true, if need locking
 *
 * Note: The given object is deleted on this heap and all appropiate
 *              index entries are deleted.
 */
int
locator_delete_force (THREAD_ENTRY * thread_p, HFID * hfid, OID * oid, int has_index, int op_type,
		      HEAP_SCANCACHE * scan_cache, int *force_count, MVCC_REEV_DATA * mvcc_reev_data, bool need_locking)
{
  return locator_delete_force_internal (thread_p, hfid, oid, has_index, op_type, scan_cache, force_count,
					mvcc_reev_data, FOR_INSERT_OR_DELETE, NULL, NULL, need_locking);
}

/*
 * locator_delete_force_for_moving () - Delete the given object
 *                                      To move record between partitions.
 *
 * return: NO_ERROR if all OK, ER_ status otherwise
 *
 *   thread_p(in):
 *   hfid(in): Heap where the object is going to be inserted
 *   oid(in): The object identifier
 *   has_index(in): false if we now for sure that there is not any index
 *                   on the instances of the class.
 *   op_type(in):
 *   scan_cache(in/out): Scan cache used to estimate the best space pages
 *                   between heap changes.
 *   force_count(in):
 *   mvcc_reev_data(in): MVCC data
 *   idx_action_flag(in): is moving record between partitioned table? 
 *			  If FOR_MOVE, this delete&insert is caused by 
 *			  'UPDATE ... SET ...', NOT 'DELETE FROM ...'
 *   new_obj_oid(in): next version - only to be used with records relocated in
 *				  other partitions, in MVCC.
 *   partition_oid(in): new partition class oid
 *   need_locking(in): true, if need locking
 *
 * Note: The given object is deleted on this heap and all appropriate
 *              index entries are deleted.
 */
static int
locator_delete_force_for_moving (THREAD_ENTRY * thread_p, HFID * hfid, OID * oid, int has_index, int op_type,
				 HEAP_SCANCACHE * scan_cache, int *force_count, MVCC_REEV_DATA * mvcc_reev_data,
				 OID * new_obj_oid, OID * partition_oid, bool need_locking)
{
  return locator_delete_force_internal (thread_p, hfid, oid, has_index, op_type, scan_cache, force_count,
					mvcc_reev_data, FOR_MOVE, new_obj_oid, partition_oid, need_locking);
}

/*
 *
 *   hfid(in): Heap where the object is going to be inserted
 *   oid(in): The object identifier
 *   has_index(in): false if we now for sure that there is not any index
 *                   on the instances of the class.
 *   op_type(in):
 *   scan_cache(in/out): Scan cache used to estimate the best space pages
 *                   between heap changes.
 *   force_count(in):
 *   mvcc_reev_data(in): MVCC data
 *   idx_action_flag(in): is moving record between partitioned table? 
 *			  If FOR_MOVE, this delete&insert is caused by 
 *			  'UPDATE ... SET ...', NOT 'DELETE FROM ...'
 *   new_obj_oid(in): next version - only to be used with records relocated in
 *				  other partitions, in MVCC.
 *   partition_oid(in): new partition class oid
 *   need_locking(in): true, if need locking
 *
 * Note: The given object is deleted on this heap and all appropriate
 *              index entries are deleted.
 */
static int
locator_delete_force_internal (THREAD_ENTRY * thread_p, HFID * hfid, OID * oid, int has_index, int op_type,
			       HEAP_SCANCACHE * scan_cache, int *force_count, MVCC_REEV_DATA * mvcc_reev_data,
			       LOCATOR_INDEX_ACTION_FLAG idx_action_flag, OID * new_obj_oid, OID * partition_oid,
			       bool need_locking)
{
  bool isold_object;		/* Make sure that this is an old object during the deletion */
  OID class_oid = { NULL_PAGEID, NULL_SLOTID, NULL_VOLID };
  /* Class identifier */
  char *classname;		/* Classname to update */
  RECDES copy_recdes;
  int error_code = NO_ERROR;
  bool deleted = false;
  SCAN_CODE scan_code = S_SUCCESS;

  /* Update note : While scanning objects, the given scancache does not fix the last accessed page. So, the object must 
   * be copied to the record descriptor. Changes : (1) variable name : peek_recdes => copy_recdes (2) function call :
   * heap_get_visible_version(..., PEEK, ...) => heap_get_visible_version(..., COPY, ...) (3) SCAN_CODE scan, char *new_area are added */

  copy_recdes.data = NULL;

  *force_count = 0;

  /* 
   * Is the object a class ?
   */
  isold_object = true;

  copy_recdes.data = NULL;

  if (need_locking == false)
    {
      /* the reevaluation is not necessary if the object is already locked */
      mvcc_reev_data = NULL;
    }

  /* IMPORTANT TODO: use a different get function when need_locking==false, but make sure it gets the last version,
     not the visible one; we need only the last version to use it to retrieve the last version of the btree key */
  scan_code =
    locator_lock_and_get_object_with_evaluation (thread_p, oid, &class_oid, &copy_recdes, scan_cache, COPY, NULL_CHN,
						 mvcc_reev_data, LOG_WARNING_IF_DELETED);

  if (scan_code == S_SUCCESS && mvcc_reev_data != NULL && mvcc_reev_data->filter_result == V_FALSE)
    {
      return ER_MVCC_NOT_SATISFIED_REEVALUATION;
    }

  if (scan_code != S_SUCCESS)
    {
      error_code = er_errid ();

      if (error_code == ER_HEAP_NODATA_NEWADDRESS)
	{
	  isold_object = false;
	  er_clear ();		/* clear ER_HEAP_NODATA_NEWADDRESS */

	  error_code = NO_ERROR;
	}
      else if (error_code == ER_HEAP_UNKNOWN_OBJECT || scan_code == S_DOESNT_EXIST)
	{
	  isold_object = false;
	  er_clear ();

	  error_code = NO_ERROR;
	  goto error;
	}
      else
	{
	  /* 
	   * Problems reading the object...Maybe, the transaction should be
	   * aborted by the caller...Quit..
	   */
	  if (error_code == NO_ERROR)
	    {
	      error_code = ER_FAILED;
	    }
	  goto error;
	}
    }

  if (isold_object == false)
    {
      OID_SET_NULL (&class_oid);
    }

  if (isold_object == true && OID_IS_ROOTOID (&class_oid))
    {
      /* 
       * A CLASS: Remove class from catalog and
       *          remove any indices on that class
       *          remove XASL cache entries which is relevant with that class
       */

      /* Delete the classname entry */
      classname = or_class_name (&copy_recdes);
      assert (classname != NULL);
      assert (strlen (classname) < 255);

      /* Note: by now, the client has probably already requested this class be deleted. We try again here just to be
       * sure it has been marked properly.  Note that we would normally want to check the return code, but we must not
       * check the return code for this one function in its current form, because we cannot distinguish between a class 
       * that has already been marked deleted and a real error. */
      (void) xlocator_delete_class_name (thread_p, classname);
      /* remove from the catalog... when is not the root */
      if (!OID_IS_ROOTOID (oid))
	{
	  error_code = catalog_delete (thread_p, oid);
	  if (error_code != NO_ERROR)
	    {
	      er_log_debug (ARG_FILE_LINE, "locator_delete_force: ct_delete_catalog failed for tran %d\n",
			    LOG_FIND_THREAD_TRAN_INDEX (thread_p));
	      goto error;
	    }
	}
      if (catcls_Enable)
	{
	  error_code = catcls_delete_catalog_classes (thread_p, classname, oid);
	  if (error_code != NO_ERROR)
	    {
	      goto error;
	    }
	}

      /* remove XASL cache entries which is relevant with that class */
      if (!OID_IS_ROOTOID (oid))
	{
	  xcache_remove_by_oid (thread_p, oid);
	}

      if (!OID_IS_ROOTOID (oid))
	{
	  fpcache_remove_by_class (thread_p, oid);
	}
    }
  else
    {
      /* 
       * Likely an INSTANCE: Apply the necessary index deletions
       *
       * If this is a server delete on an instance, the object must be locked
       * in exclusive  mode since it is likely that we have just added an
       * SIX lock on the class at this moment.
       *
       * Note that we cannot have server deletes on classes.
       */
      if (isold_object == true && has_index)
	{
	  /* if MVCC then delete before updating index */
	  if (!mvcc_is_mvcc_disabled_class (&class_oid))
	    {
	      HEAP_OPERATION_CONTEXT delete_context;

	      /* build operation context */
	      heap_create_delete_context (&delete_context, hfid, oid, &class_oid, scan_cache);

	      /* attempt delete */
	      if (heap_delete_logical (thread_p, &delete_context) != NO_ERROR)
		{
		  /* 
		   * Problems deleting the object...Maybe, the transaction should be
		   * aborted by the caller...Quit..
		   */
		  error_code = er_errid ();
		  er_log_debug (ARG_FILE_LINE, "locator_delete_force: hf_delete failed for tran %d\n",
				LOG_FIND_THREAD_TRAN_INDEX (thread_p));
		  if (error_code == NO_ERROR)
		    {
		      error_code = ER_FAILED;
		    }
		  goto error;
		}

	      deleted = true;
	    }

	  if (idx_action_flag == FOR_INSERT_OR_DELETE)
	    {
	      error_code =
		locator_add_or_remove_index (thread_p, &copy_recdes, oid, &class_oid, false, op_type, scan_cache, true,
					     true, hfid, NULL);
	    }
	  else
	    {
	      error_code =
		locator_add_or_remove_index_for_moving (thread_p, &copy_recdes, oid, &class_oid, false, op_type,
							scan_cache, true, true, hfid, NULL);
	    }

	  if (error_code != NO_ERROR)
	    {
	      /* 
	       * There is an error deleting the index... Quit... The
	       * transaction must be aborted by the caller
	       */
	      goto error;
	    }
	}

      /* remove query result cache entries which are relevant with this class */
      if (!QFILE_IS_LIST_CACHE_DISABLED)
	{
	  if (qexec_clear_list_cache_by_class (thread_p, &class_oid) != NO_ERROR)
	    {
	      er_log_debug (ARG_FILE_LINE,
			    "locator_delete_force: qexec_clear_list_cache_by_class failed for class { %d %d %d }\n",
			    class_oid.pageid, class_oid.slotid, class_oid.volid);
	    }
	  qmgr_add_modified_class (thread_p, &class_oid);
	}
    }

  if (!deleted)
    {
      HEAP_OPERATION_CONTEXT delete_context;

      /* build operation context */
      heap_create_delete_context (&delete_context, hfid, oid, &class_oid, scan_cache);

      /* attempt delete */
      if (heap_delete_logical (thread_p, &delete_context) != NO_ERROR)
	{
	  /* 
	   * Problems deleting the object...Maybe, the transaction should be
	   * aborted by the caller...Quit..
	   */
	  er_log_debug (ARG_FILE_LINE, "locator_delete_force: hf_delete failed for tran %d\n",
			LOG_FIND_THREAD_TRAN_INDEX (thread_p));
	  assert (er_errid () != NO_ERROR);
	  error_code = er_errid ();
	  if (error_code == NO_ERROR)
	    {
	      error_code = ER_FAILED;
	    }
	  goto error;
	}
      deleted = true;
    }
  *force_count = 1;

#if defined(ENABLE_UNUSED_FUNCTION)
  if (isold_object == true && !OID_IS_ROOTOID (&class_oid))
    {
      /* decrease the counter of the catalog */
      locator_decrease_catalog_count (thread_p, &class_oid);
    }
#endif

error:

  return error_code;
}

/*
 * locator_delete_lob_force () - Delete all blob which is in the given object
 *
 * return: NO_ERROR if all OK, ER_ status otherwise
 *
 *   thread_p(in):
 *   class_oid(in):
 *   oid(in):
 *   recdes(in):
 */
int
locator_delete_lob_force (THREAD_ENTRY * thread_p, OID * class_oid, OID * oid, RECDES * recdes)
{
  HEAP_CACHE_ATTRINFO attr_info;
  HEAP_ATTRVALUE *value;
  bool attr_info_inited;
  HEAP_SCANCACHE scan_cache;
  RECDES copy_recdes;
  bool scan_cache_inited;
  bool found;
  int i;
  int error_code = NO_ERROR;

  assert ((class_oid != NULL) && (recdes != NULL || oid != NULL));

  attr_info_inited = false;
  scan_cache_inited = false;
  found = false;

  error_code = heap_attrinfo_start (thread_p, class_oid, -1, NULL, &attr_info);
  if (error_code != NO_ERROR)
    {
      goto error;
    }
  attr_info_inited = true;

  /* check if lob attribute exists */
  for (i = 0; i < attr_info.num_values; i++)
    {
      value = &attr_info.values[i];
      if (value->last_attrepr->type == DB_TYPE_BLOB || value->last_attrepr->type == DB_TYPE_CLOB)
	{
	  found = true;
	  break;
	}
    }

  if (found)
    {
      if (recdes == NULL)
	{
	  SCAN_CODE scan;
	  error_code = heap_scancache_quick_start (&scan_cache);
	  if (error_code != NO_ERROR)
	    {
	      goto error;
	    }
	  scan_cache_inited = true;
	  copy_recdes.data = NULL;
	  scan = heap_get_visible_version (thread_p, oid, class_oid, &copy_recdes, &scan_cache, COPY, NULL_CHN);
	  if (scan != S_SUCCESS)
	    {
	      goto error;
	    }
	  recdes = &copy_recdes;
	}

      error_code = heap_attrinfo_delete_lob (thread_p, recdes, &attr_info);
    }

error:

  if (attr_info_inited)
    {
      heap_attrinfo_end (thread_p, &attr_info);
    }
  if (scan_cache_inited)
    {
      error_code = heap_scancache_end (thread_p, &scan_cache);
    }

  return error_code;
}

/*
 * locator_force_for_multi_update () -
 *
 * return: NO_ERROR if all OK, ER_ status otherwise
 *
 *   force_area(in):  Copy area where objects are placed
 *
 * Note: This function update given objects that are sent by clients.
 *              The objects are updated by multiple row update performed
 *              on client and sent to the server through flush request.
 */
static int
locator_force_for_multi_update (THREAD_ENTRY * thread_p, LC_COPYAREA * force_area)
{
  LC_COPYAREA_MANYOBJS *mobjs;	/* Describe multiple objects in area */
  LC_COPYAREA_ONEOBJ *obj;	/* Describe on object in area */
  RECDES recdes;		/* Record descriptor for object */
  HEAP_SCANCACHE scan_cache;
  int scan_cache_inited = 0;
  LOG_TDES *tdes;
  int i, s, t;
  int malloc_size;
  BTREE_UNIQUE_STATS *temp_info;
  char *ptr;
  int force_count;
  int tran_index;
  int error_code = NO_ERROR;
  REPL_INFO_TYPE repl_info;
  int has_index;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
      error_code = ER_LOG_UNKNOWN_TRANINDEX;
      goto error;
    }

  mobjs = LC_MANYOBJS_PTR_IN_COPYAREA (force_area);

  if (mobjs->start_multi_update && tdes->tran_unique_stats == NULL)
    {
      tdes->num_unique_btrees = 0;
      tdes->max_unique_btrees = UNIQUE_STAT_INFO_INCREMENT;

      malloc_size = sizeof (BTREE_UNIQUE_STATS) * UNIQUE_STAT_INFO_INCREMENT;

      /* Do not use db_private_alloc */
      tdes->tran_unique_stats = (BTREE_UNIQUE_STATS *) malloc (malloc_size);
      if (tdes->tran_unique_stats == NULL)
	{
	  error_code = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto error;
	}
    }

  if (mobjs->num_objs > 0)
    {
      int first_update_obj = -1, last_update_obj = -1;

      /* 
       * Find first and last UPDATE objects
       */
      obj = LC_START_ONEOBJ_PTR_IN_COPYAREA (mobjs);
      obj = LC_PRIOR_ONEOBJ_PTR_IN_COPYAREA (obj);
      for (i = 0; i < mobjs->num_objs; i++)
	{
	  obj = LC_NEXT_ONEOBJ_PTR_IN_COPYAREA (obj);
	  if (LC_IS_FLUSH_UPDATE (obj->operation))
	    {
	      last_update_obj = i;
	      if (first_update_obj == -1)
		{
		  first_update_obj = i;
		}
	    }
	}
      if (last_update_obj == -1)
	{
	  /* this is not exactly an error case, but we somehow managed to generate a multi-update flush with no updated 
	   * objects */
	  error_code = NO_ERROR;
	  goto error;
	}

      /* 
       * Flush objects
       */
      obj = LC_START_ONEOBJ_PTR_IN_COPYAREA (mobjs);
      obj = LC_PRIOR_ONEOBJ_PTR_IN_COPYAREA (obj);
      LC_RECDES_IN_COPYAREA (force_area, &recdes);

      for (i = 0; i < mobjs->num_objs; i++)
	{
	  obj = LC_NEXT_ONEOBJ_PTR_IN_COPYAREA (obj);
	  LC_RECDES_TO_GET_ONEOBJ (force_area, obj, &recdes);

	  /* skip all non-updates operations and updates of class objects (non-updates operations and class objects are 
	   * already flushed in xlocator_force) */
	  if (!LC_IS_FLUSH_UPDATE (obj->operation) || OID_EQ (&obj->class_oid, oid_Root_class_oid))
	    {
	      continue;
	    }

	  if (!scan_cache_inited)
	    {
	      /* Initialize a modify scancache */
	      error_code =
		locator_start_force_scan_cache (thread_p, &scan_cache, &obj->hfid, &obj->class_oid, MULTI_ROW_UPDATE);
	      if (error_code != NO_ERROR)
		{
		  goto error;
		}
	      scan_cache_inited = 1;
	    }

	  if (mobjs->start_multi_update && i == first_update_obj)
	    {
	      repl_info = REPL_INFO_TYPE_RBR_START;
	    }
	  else if (mobjs->end_multi_update && i == last_update_obj)
	    {
	      repl_info = REPL_INFO_TYPE_RBR_END;
	    }
	  else
	    {
	      repl_info = REPL_INFO_TYPE_RBR_NORMAL;
	    }
	  has_index = LC_ONEOBJ_GET_INDEX_FLAG (obj);

	  scan_cache.mvcc_snapshot = logtb_get_mvcc_snapshot (thread_p);
	  if (scan_cache.mvcc_snapshot == NULL)
	    {
	      error_code = er_errid ();
	      if (error_code == NO_ERROR)
		{
		  error_code = ER_FAILED;
		}

	      goto error;
	    }

	  /* update */
	  error_code =
	    locator_update_force (thread_p, &obj->hfid, &obj->class_oid, &obj->oid, NULL, &recdes,
				  has_index, NULL, 0, MULTI_ROW_UPDATE, &scan_cache, &force_count, false, repl_info,
				  DB_NOT_PARTITIONED_CLASS, NULL, NULL, UPDATE_INPLACE_NONE, true);
	  if (error_code != NO_ERROR)
	    {
	      /* 
	       * Problems updating the object...Maybe, the transaction should be
	       * aborted by the caller...Quit..
	       */
	      goto error;
	    }
	}			/* end-for */

      for (s = 0; s < scan_cache.num_btids; s++)
	{
	  temp_info = &(scan_cache.index_stat_info[s]);
	  if (temp_info->num_nulls == 0 && temp_info->num_keys == 0 && temp_info->num_oids == 0)
	    {
	      continue;
	    }
	  /* non-unique index would be filtered out at above statement. */

	  for (t = 0; t < tdes->num_unique_btrees; t++)
	    {
	      if (BTID_IS_EQUAL (&temp_info->btid, &tdes->tran_unique_stats[t].btid))
		{
		  break;
		}
	    }
	  if (t < tdes->num_unique_btrees)
	    {
	      /* The same unique index has been found */
	      tdes->tran_unique_stats[t].num_nulls += temp_info->num_nulls;
	      tdes->tran_unique_stats[t].num_keys += temp_info->num_keys;
	      tdes->tran_unique_stats[t].num_oids += temp_info->num_oids;
	    }
	  else
	    {
	      /* The same unique index has not been found */
	      if (tdes->num_unique_btrees == tdes->max_unique_btrees)
		{
		  /* we need more space for storing unique index stat. info.  */
		  tdes->max_unique_btrees += UNIQUE_STAT_INFO_INCREMENT;
		  malloc_size = (sizeof (BTREE_UNIQUE_STATS) * tdes->max_unique_btrees);

		  /* Do not use db_private_realloc */
		  ptr = (char *) realloc (tdes->tran_unique_stats, malloc_size);
		  if (ptr == NULL)
		    {
		      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
		      goto error;
		    }
		  tdes->tran_unique_stats = (BTREE_UNIQUE_STATS *) ptr;
		}
	      t = tdes->num_unique_btrees;
	      BTID_COPY (&tdes->tran_unique_stats[t].btid, &temp_info->btid);
	      tdes->tran_unique_stats[t].num_nulls = temp_info->num_nulls;
	      tdes->tran_unique_stats[t].num_keys = temp_info->num_keys;
	      tdes->tran_unique_stats[t].num_oids = temp_info->num_oids;
	      tdes->num_unique_btrees++;	/* increment */
	    }
	}

      locator_end_force_scan_cache (thread_p, &scan_cache);
      scan_cache_inited = 0;
    }

  if (mobjs->end_multi_update)
    {
      BTREE_UNIQUE_STATS *unique_stats = NULL;

      for (s = 0; s < tdes->num_unique_btrees; s++)
	{
	  unique_stats = &tdes->tran_unique_stats[s];
	  if (unique_stats->num_nulls == 0 && unique_stats->num_keys == 0 && unique_stats->num_oids == 0)
	    {
	      continue;		/* no modification : non-unique index */
	    }
	  if ((unique_stats->num_nulls + unique_stats->num_keys) != unique_stats->num_oids)
	    {
	      BTREE_SET_UNIQUE_VIOLATION_ERROR (thread_p, NULL, NULL, &mobjs->objs.class_oid, &unique_stats->btid,
						NULL);
	      error_code = ER_BTREE_UNIQUE_FAILED;
	      goto error;
	    }

	  error_code =
	    logtb_tran_update_unique_stats (thread_p, &unique_stats->btid, unique_stats->num_keys,
					    unique_stats->num_oids, unique_stats->num_nulls, true);
	  if (error_code != NO_ERROR)
	    {
	      goto error;
	    }
	}

      if (tdes->tran_unique_stats != NULL)
	{
	  free_and_init (tdes->tran_unique_stats);
	}
    }

  return error_code;

error:
  if (scan_cache_inited)
    {
      locator_end_force_scan_cache (thread_p, &scan_cache);
    }

  if (tdes != NULL && tdes->tran_unique_stats != NULL)
    {
      free_and_init (tdes->tran_unique_stats);
    }

  return error_code;
}

/*
 * locator_repl_add_error_to_copyarea () - place error info into copy area that
 *                                         will be sent to client
 *
 * return:
 *
 *   copy_area(out): copy area where error info will be placed
 *   recdes(in): struct that describes copy_area
 *   obj(in): object that describes the operation which caused an error
 *   key_value(in): primary key value
 *   err_code(in):
 *   err_msg(in):
 */
static void
locator_repl_add_error_to_copyarea (LC_COPYAREA ** copy_area, RECDES * recdes, LC_COPYAREA_ONEOBJ * obj,
				    DB_VALUE * key_value, int err_code, const char *err_msg)
{
  LC_COPYAREA *new_copy_area = NULL;
  LC_COPYAREA_MANYOBJS *reply_mobjs = NULL;
  LC_COPYAREA_ONEOBJ *reply_obj = NULL;
  char *ptr;
  int packed_length = 0, round_length;
  int prev_offset, prev_length;

  packed_length += OR_VALUE_ALIGNED_SIZE (key_value);
  packed_length += OR_INT_SIZE;
  packed_length += or_packed_string_length (err_msg, NULL);

  if (packed_length > recdes->area_size)
    {
      int new_length, nw_pagesize;

      prev_offset = CAST_BUFLEN (recdes->data - (*copy_area)->mem);
      prev_length = (*copy_area)->length;

      nw_pagesize = db_network_page_size ();
      new_length = prev_length + CEIL_PTVDIV (packed_length - recdes->area_size, nw_pagesize) * nw_pagesize;

      new_copy_area = locator_reallocate_copy_area_by_length (*copy_area, new_length);
      if (new_copy_area == NULL)
	{
	  /* failed to reallocate copy area. skip the current error */
	  return;
	}
      else
	{
	  recdes->data = new_copy_area->mem + prev_offset;
	  recdes->area_size += CAST_BUFLEN (new_copy_area->length - prev_length);
	  *copy_area = new_copy_area;
	}
    }

  reply_mobjs = LC_MANYOBJS_PTR_IN_COPYAREA (*copy_area);
  reply_obj = LC_FIND_ONEOBJ_PTR_IN_COPYAREA (reply_mobjs, reply_mobjs->num_objs);

  ptr = recdes->data;
  ptr = or_pack_mem_value (ptr, key_value);
  ptr = or_pack_int (ptr, err_code);
  ptr = or_pack_string (ptr, err_msg);

  reply_obj->length = CAST_BUFLEN (ptr - recdes->data);
  reply_obj->offset = CAST_BUFLEN (recdes->data - (*copy_area)->mem);
  COPY_OID (&reply_obj->class_oid, &obj->class_oid);
  reply_obj->operation = obj->operation;

  reply_mobjs->num_objs++;

  recdes->length = reply_obj->length;
  round_length = DB_ALIGN (recdes->length, MAX_ALIGNMENT);
#if !defined(NDEBUG)
  /* suppress valgrind UMW error */
  memset (recdes->data + recdes->length, 0, MIN (round_length - recdes->length, recdes->area_size - recdes->length));
#endif
  recdes->data += round_length;
  recdes->area_size -= round_length + sizeof (*reply_obj);
}

/*
 * locator_repl_prepare_force () - prepare required info for each operation
 *
 * return: NO_ERROR if all OK, ER_ status otherwise
 *
 *   thread_p(in):
 *   obj(in): object that describes the current operation
 *   old_recdes(out): original record needed for update operation
 *   recdes(in/out): record to be applied
 *   key_value(in): primary key value
 *   force_scancache(in):
 */
static int
locator_repl_prepare_force (THREAD_ENTRY * thread_p, LC_COPYAREA_ONEOBJ * obj, RECDES * old_recdes, RECDES * recdes,
			    DB_VALUE * key_value, HEAP_SCANCACHE * force_scancache)
{
  int error_code = NO_ERROR;
  int last_repr_id = -1;
  int old_chn = -1;
  BTID btid;
  SCAN_CODE scan;
  SCAN_OPERATION_TYPE scan_op_type;

  if (obj->operation == LC_FLUSH_DELETE)
    {
      scan_op_type = S_DELETE;
    }
  else if (LC_IS_FLUSH_UPDATE (obj->operation) == true)
    {
      scan_op_type = S_UPDATE;
    }

  if (obj->operation != LC_FLUSH_DELETE)
    {
      last_repr_id = heap_get_class_repr_id (thread_p, &obj->class_oid);
      if (last_repr_id == 0)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CT_INVALID_REPRID, 1, last_repr_id);
	  return ER_CT_INVALID_REPRID;
	}

      error_code = or_replace_rep_id (recdes, last_repr_id);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }

  if (LC_IS_FLUSH_INSERT (obj->operation) == false)
    {
      error_code = btree_get_pkey_btid (thread_p, &obj->class_oid, &btid);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}

      if (xbtree_find_unique (thread_p, &btid, scan_op_type, key_value, &obj->class_oid, &obj->oid, true) !=
	  BTREE_KEY_FOUND)
	{
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_OBJECT_NOT_FOUND, 0);
	  return ER_OBJ_OBJECT_NOT_FOUND;
	}
    }


  if (LC_IS_FLUSH_UPDATE (obj->operation) == true)
    {
      assert (OID_ISNULL (&obj->oid) != true);

      scan =
	heap_get_visible_version (thread_p, &obj->oid, &obj->class_oid, old_recdes, force_scancache, PEEK, NULL_CHN);

      if (scan != S_SUCCESS)
	{
	  assert (er_errid () != NO_ERROR);
	  error_code = er_errid ();
	  if (error_code == ER_HEAP_UNKNOWN_OBJECT)
	    {
	      er_log_debug (ARG_FILE_LINE, "locator_repl_prepare_force : unknown oid ( %d|%d|%d )\n",
			    obj->oid.pageid, obj->oid.slotid, obj->oid.volid);
	    }

	  return error_code;
	}

      old_chn = or_chn (old_recdes);

      error_code = or_replace_chn (recdes, old_chn + 1);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }

  return NO_ERROR;
}

/*
 * locator_repl_get_key_value () - read pkey value from copy_area
 *
 * return: length of unpacked key value
 *
 *   key_value(out): primary key value
 *   force_area(in):
 *   obj(in): object that describes memory location of key_value
 *
 */
static int
locator_repl_get_key_value (DB_VALUE * key_value, LC_COPYAREA * force_area, LC_COPYAREA_ONEOBJ * obj)
{
  char *ptr, *start_ptr;

  start_ptr = ptr = force_area->mem + obj->offset;
  ptr = or_unpack_mem_value (ptr, key_value);

  return (int) (ptr - start_ptr);
}

/*
 * xlocator_force () - Updates objects sent by log applier
 *
 * return: NO_ERROR if all OK, ER_ status otherwise
 *
 *   force_area(in): Copy area where objects are placed
 *
 * Note: This function applies all the desired operations on each of
 *              object placed in the force_area.
 */
int
xlocator_repl_force (THREAD_ENTRY * thread_p, LC_COPYAREA * force_area, LC_COPYAREA ** reply_area)
{
  LC_COPYAREA_MANYOBJS *mobjs;	/* Describe multiple objects in area */
  LC_COPYAREA_ONEOBJ *obj;	/* Describe on object in area */
  RECDES recdes;		/* Record descriptor for object */
  RECDES old_recdes;
  RECDES reply_recdes;		/* Describe copy area for reply */
  int i;
  HEAP_SCANCACHE *force_scancache = NULL;
  HEAP_SCANCACHE scan_cache;
  int force_count;
  LOG_LSA lsa, oneobj_lsa;
  int error_code = NO_ERROR;
  int pruning_type = 0;
  int num_continue_on_error = 0;
  DB_VALUE key_value;
  int packed_key_value_len;
  HFID prev_hfid;
  int has_index;

  /* need to start a topop to ensure the atomic operation. */
  error_code = xtran_server_start_topop (thread_p, &lsa);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  mobjs = LC_MANYOBJS_PTR_IN_COPYAREA (force_area);

  obj = LC_START_ONEOBJ_PTR_IN_COPYAREA (mobjs);
  obj = LC_PRIOR_ONEOBJ_PTR_IN_COPYAREA (obj);

  HFID_SET_NULL (&prev_hfid);
  db_value_put_null (&key_value);

  LC_RECDES_IN_COPYAREA (*reply_area, &reply_recdes);

  for (i = 0; i < mobjs->num_objs; i++)
    {
      er_clear ();

      obj = LC_NEXT_ONEOBJ_PTR_IN_COPYAREA (obj);

      packed_key_value_len = locator_repl_get_key_value (&key_value, force_area, obj);

      LC_REPL_RECDES_FOR_ONEOBJ (force_area, obj, packed_key_value_len, &recdes);

      error_code = heap_get_hfid_from_class_oid (thread_p, &obj->class_oid, &obj->hfid);
      if (error_code != NO_ERROR)
	{
	  goto exit_on_error;
	}

      if (HFID_EQ (&prev_hfid, &obj->hfid) != true && force_scancache != NULL)
	{
	  locator_end_force_scan_cache (thread_p, force_scancache);
	  force_scancache = NULL;
	}

      if (force_scancache == NULL)
	{
	  /* Initialize a modify scancache */
	  error_code =
	    locator_start_force_scan_cache (thread_p, &scan_cache, &obj->hfid, &obj->class_oid, SINGLE_ROW_UPDATE);
	  if (error_code != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	  force_scancache = &scan_cache;
	  HFID_COPY (&prev_hfid, &obj->hfid);
	}

      error_code = xtran_server_start_topop (thread_p, &oneobj_lsa);
      if (error_code != NO_ERROR)
	{
	  goto exit_on_error;
	}

      error_code = locator_repl_prepare_force (thread_p, obj, &old_recdes, &recdes, &key_value, force_scancache);
      if (error_code == NO_ERROR)
	{
	  has_index = LC_ONEOBJ_GET_INDEX_FLAG (obj);

	  switch (obj->operation)
	    {
	    case LC_FLUSH_INSERT:
	    case LC_FLUSH_INSERT_PRUNE:
	    case LC_FLUSH_INSERT_PRUNE_VERIFY:
	      pruning_type = locator_area_op_to_pruning_type (obj->operation);
	      error_code =
		locator_insert_force (thread_p, &obj->hfid, &obj->class_oid, &obj->oid, &recdes, has_index,
				      SINGLE_ROW_INSERT, force_scancache, &force_count, pruning_type, NULL, NULL,
				      UPDATE_INPLACE_NONE);

	      if (error_code == NO_ERROR)
		{
		  /* monitor */
		  perfmon_inc_stat (thread_p, PSTAT_QM_NUM_INSERTS);
		}
	      break;

	    case LC_FLUSH_UPDATE:
	    case LC_FLUSH_UPDATE_PRUNE:
	    case LC_FLUSH_UPDATE_PRUNE_VERIFY:
	      pruning_type = locator_area_op_to_pruning_type (obj->operation);
	      error_code =
		locator_update_force (thread_p, &obj->hfid, &obj->class_oid, &obj->oid, NULL, &recdes, has_index,
				      NULL, 0, SINGLE_ROW_UPDATE, force_scancache, &force_count, false,
				      REPL_INFO_TYPE_RBR_NORMAL, pruning_type, NULL, NULL, UPDATE_INPLACE_NONE, true);

	      if (error_code == NO_ERROR)
		{
		  /* monitor */
		  perfmon_inc_stat (thread_p, PSTAT_QM_NUM_UPDATES);
		}
	      break;

	    case LC_FLUSH_DELETE:
	      error_code =
		locator_delete_force (thread_p, &obj->hfid, &obj->oid, has_index, SINGLE_ROW_DELETE, force_scancache,
				      &force_count, NULL, true);

	      if (error_code == NO_ERROR)
		{
		  /* monitor */
		  perfmon_inc_stat (thread_p, PSTAT_QM_NUM_DELETES);
		}
	      break;

	    default:
	      /* 
	       * Problems forcing the object. Don't known what flush/force operation
	       * to execute on the object... This is a system error...
	       * Maybe, the transaction should be aborted by the caller...Quit..
	       */
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LC_BADFORCE_OPERATION, 4, obj->operation, obj->oid.volid,
		      obj->oid.pageid, obj->oid.slotid);
	      error_code = ER_LC_BADFORCE_OPERATION;
	      break;
	    }			/* end-switch */
	}

      if (error_code != NO_ERROR)
	{
	  assert (er_errid () != NO_ERROR);
	  locator_repl_add_error_to_copyarea (reply_area, &reply_recdes, obj, &key_value, er_errid (), er_msg ());

	  error_code = NO_ERROR;
	  num_continue_on_error++;

	  (void) xtran_server_end_topop (thread_p, LOG_RESULT_TOPOP_ABORT, &oneobj_lsa);
	}
      else
	{
	  (void) xtran_server_end_topop (thread_p, LOG_RESULT_TOPOP_ATTACH_TO_OUTER, &oneobj_lsa);
	}
      pr_clear_value (&key_value);
    }

  if (force_scancache != NULL)
    {
      locator_end_force_scan_cache (thread_p, force_scancache);
    }

  (void) xtran_server_end_topop (thread_p, LOG_RESULT_TOPOP_ATTACH_TO_OUTER, &lsa);

  if (num_continue_on_error > 0)
    {
      return ER_LC_PARTIALLY_FAILED_TO_FLUSH;
    }

  return error_code;

exit_on_error:
  assert_release (error_code == ER_FAILED || error_code == er_errid ());

  if (DB_IS_NULL (&key_value) == false)
    {
      pr_clear_value (&key_value);
    }

  if (force_scancache != NULL)
    {
      locator_end_force_scan_cache (thread_p, force_scancache);
    }

  (void) xtran_server_end_topop (thread_p, LOG_RESULT_TOPOP_ABORT, &lsa);

  return error_code;
}

/*
 * xlocator_force () - Updates objects placed on page
 *
 * return: NO_ERROR if all OK, ER_ status otherwise
 *
 *   force_area(in): Copy area where objects are placed
 *
 * Note: This function applies all the desired operations on each of
 *              object placed in the force_area.
 */
int
xlocator_force (THREAD_ENTRY * thread_p, LC_COPYAREA * force_area, int num_ignore_error, int *ignore_error_list)
{
  LC_COPYAREA_MANYOBJS *mobjs;	/* Describe multiple objects in area */
  LC_COPYAREA_ONEOBJ *obj;	/* Describe on object in area */
  RECDES recdes;		/* Record descriptor for object */
  int i;
  HEAP_SCANCACHE *force_scancache = NULL;
  HEAP_SCANCACHE scan_cache;
  int force_count;
  LOG_LSA lsa, oneobj_lsa;
  int error_code = NO_ERROR;
  int pruning_type = 0;
  int has_index;

  /* need to start a topop to ensure the atomic operation. */
  error_code = xtran_server_start_topop (thread_p, &lsa);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  mobjs = LC_MANYOBJS_PTR_IN_COPYAREA (force_area);
  obj = LC_START_ONEOBJ_PTR_IN_COPYAREA (mobjs);
  obj = LC_PRIOR_ONEOBJ_PTR_IN_COPYAREA (obj);
  LC_RECDES_IN_COPYAREA (force_area, &recdes);

  for (i = 0; i < mobjs->num_objs; i++)
    {
      obj = LC_NEXT_ONEOBJ_PTR_IN_COPYAREA (obj);
      LC_RECDES_TO_GET_ONEOBJ (force_area, obj, &recdes);

      if (i == 0)
	{
	  /* 
	   * Initialize a modify scancache
	   */
	  error_code =
	    locator_start_force_scan_cache (thread_p, &scan_cache, &obj->hfid, &obj->class_oid, SINGLE_ROW_UPDATE);
	  if (error_code != NO_ERROR)
	    {
	      goto error;
	    }
	  force_scancache = &scan_cache;
	}

      if (LOG_CHECK_LOG_APPLIER (thread_p) || num_ignore_error > 0)
	{
	  error_code = xtran_server_start_topop (thread_p, &oneobj_lsa);
	  if (error_code != NO_ERROR)
	    {
	      goto error;
	    }
	}

      has_index = LC_ONEOBJ_GET_INDEX_FLAG (obj);

      /* For multi update, we're just interested in the INSERT and DELETE operations here, which were generated by
       * triggers operating on the same class that is being updated; treat these operations as single row and skip all
       * updates. Also, for multi UPDATE operations, the class objects should be flushed here as single row. Instance
       * updates will be handled by locator_force_for_multi_update() TODO: trigger generated updates will not be
       * treated correctly. */
      if (mobjs->start_multi_update && LC_IS_FLUSH_UPDATE (obj->operation)
	  && !OID_EQ (&obj->class_oid, oid_Root_class_oid))
	{
	  continue;
	}

      /* delete old row must be set to true for system classes and false for others, therefore it must be updated for
       * each object. */

      switch (obj->operation)
	{
	case LC_FLUSH_INSERT:
	case LC_FLUSH_INSERT_PRUNE:
	case LC_FLUSH_INSERT_PRUNE_VERIFY:
	  pruning_type = locator_area_op_to_pruning_type (obj->operation);
	  error_code =
	    locator_insert_force (thread_p, &obj->hfid, &obj->class_oid, &obj->oid, &recdes, has_index,
				  SINGLE_ROW_INSERT, force_scancache, &force_count, pruning_type, NULL, NULL,
				  UPDATE_INPLACE_NONE);

	  if (error_code == NO_ERROR)
	    {
	      /* monitor */
	      perfmon_inc_stat (thread_p, PSTAT_QM_NUM_INSERTS);
	    }
	  break;

	case LC_FLUSH_UPDATE:
	case LC_FLUSH_UPDATE_PRUNE:
	case LC_FLUSH_UPDATE_PRUNE_VERIFY:
	  pruning_type = locator_area_op_to_pruning_type (obj->operation);
	  error_code =
	    locator_update_force (thread_p, &obj->hfid, &obj->class_oid, &obj->oid, NULL, &recdes,
				  has_index, NULL, 0, SINGLE_ROW_UPDATE, force_scancache, &force_count, false,
				  REPL_INFO_TYPE_RBR_NORMAL, pruning_type, NULL, NULL, UPDATE_INPLACE_NONE, true);

	  if (error_code == NO_ERROR)
	    {
	      /* monitor */
	      perfmon_inc_stat (thread_p, PSTAT_QM_NUM_UPDATES);
	    }
	  break;

	case LC_FLUSH_DELETE:
	  error_code =
	    locator_delete_force (thread_p, &obj->hfid, &obj->oid, has_index, SINGLE_ROW_DELETE, force_scancache,
				  &force_count, NULL, true);

	  if (error_code == NO_ERROR)
	    {
	      /* monitor */
	      perfmon_inc_stat (thread_p, PSTAT_QM_NUM_DELETES);
	    }
	  break;

	default:
	  /* 
	   * Problems forcing the object. Don't known what flush/force operation
	   * to execute on the object... This is a system error...
	   * Maybe, the transaction should be aborted by the caller...Quit..
	   */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LC_BADFORCE_OPERATION, 4, obj->operation, obj->oid.volid,
		  obj->oid.pageid, obj->oid.slotid);
	  error_code = ER_LC_BADFORCE_OPERATION;
	  break;
	}			/* end-switch */

      if (LOG_CHECK_LOG_APPLIER (thread_p) || num_ignore_error > 0)
	{
	  bool need_to_abort_oneobj = false;

	  if (error_code != NO_ERROR)
	    {
	      if (LOG_CHECK_LOG_APPLIER (thread_p))
		{
		  need_to_abort_oneobj = true;
		}
	      else
		{
		  error_code = locator_filter_errid (thread_p, num_ignore_error, ignore_error_list);
		  if (error_code == NO_ERROR)
		    {
		      /* error is filtered out */
		      OID_SET_NULL (&obj->oid);
		      need_to_abort_oneobj = true;
		    }
		}
	    }

	  if (need_to_abort_oneobj)
	    {
	      (void) xtran_server_end_topop (thread_p, LOG_RESULT_TOPOP_ABORT, &oneobj_lsa);
	    }
	  else
	    {
	      (void) xtran_server_end_topop (thread_p, LOG_RESULT_TOPOP_ATTACH_TO_OUTER, &oneobj_lsa);
	    }
	}

      if (error_code != NO_ERROR)
	{
	  /* 
	   * Problems... Maybe, the transaction should
	   * be aborted by the caller...Quit..
	   */
	  goto error;
	}
    }				/* end-for */

  if (force_scancache != NULL)
    {
      locator_end_force_scan_cache (thread_p, force_scancache);
      force_scancache = NULL;
    }

  /* handle multi-update case */
  if (mobjs->start_multi_update)
    {
      error_code = locator_force_for_multi_update (thread_p, force_area);
      if (error_code != NO_ERROR)
	{
	  goto error;
	}
    }

  (void) xtran_server_end_topop (thread_p, LOG_RESULT_TOPOP_ATTACH_TO_OUTER, &lsa);

  return error_code;

error:

  /* The reevaluation at update phase of update is currently disabled */
  assert (error_code != ER_MVCC_NOT_SATISFIED_REEVALUATION);
  assert_release (error_code == ER_FAILED || error_code == er_errid ());

  if (force_scancache != NULL)
    {
      locator_end_force_scan_cache (thread_p, force_scancache);
    }

  (void) xtran_server_end_topop (thread_p, LOG_RESULT_TOPOP_ABORT, &lsa);

  return error_code;
}

/*
 * locator_allocate_copy_area_by_attr_info () - Transforms attribute
 *              information into a disk representation and allocates a
 *              LC_COPYAREA big enough to fit the representation
 *
 * return: the allocated LC_COPYAREA if all OK, NULL otherwise
 *
 *   attr_info(in/out): Attribute information
 *                      (Set as a side effect to fill the rest of values)
 *   old_recdes(in): The old representation of the object or NULL if this is a
 *                   new object (to be inserted).
 *   new_recdes(in): The resulting new representation of the object.
 *   copyarea_length_hint(in): An estimated size for the LC_COPYAREA or -1 if
 *                             an estimated size is not known.
 *   lob_create_flag(in) :
 *
 * Note: The allocated should be freed by using locator_free_copy_area ()
 */
LC_COPYAREA *
locator_allocate_copy_area_by_attr_info (THREAD_ENTRY * thread_p, HEAP_CACHE_ATTRINFO * attr_info, RECDES * old_recdes,
					 RECDES * new_recdes, const int copyarea_length_hint, int lob_create_flag)
{
  LC_COPYAREA *copyarea = NULL;
  int copyarea_length = copyarea_length_hint <= 0 ? DB_PAGESIZE : copyarea_length_hint;
  SCAN_CODE scan = S_DOESNT_FIT;

  while (scan == S_DOESNT_FIT)
    {
      copyarea = locator_allocate_copy_area_by_length (copyarea_length);
      if (copyarea == NULL)
	{
	  break;
	}

      new_recdes->data = copyarea->mem;
      new_recdes->area_size = copyarea->length;

      if (lob_create_flag == LOB_FLAG_EXCLUDE_LOB)
	{
	  scan = heap_attrinfo_transform_to_disk_except_lob (thread_p, attr_info, old_recdes, new_recdes);
	}
      else
	{
	  scan = heap_attrinfo_transform_to_disk (thread_p, attr_info, old_recdes, new_recdes);
	}

      if (scan != S_SUCCESS)
	{
	  /* Get the real length used in the copy area */
	  copyarea_length = copyarea->length;
	  locator_free_copy_area (copyarea);
	  copyarea = NULL;
	  new_recdes->data = NULL;
	  new_recdes->area_size = 0;

	  /* Is more space needed ? */
	  if (scan == S_DOESNT_FIT)
	    {
	      /* 
	       * The object does not fit into copy area, increase the area
	       * to estimated size included in length of record descriptor.
	       */
	      if (copyarea_length < (-new_recdes->length))
		{
		  copyarea_length = DB_ALIGN (-new_recdes->length, MAX_ALIGNMENT);
		}
	      else
		{
		  /* 
		   * This is done for security purposes only, since the
		   * transformation may not have given us the correct length,
		   * somehow.
		   */
		  copyarea_length += DB_PAGESIZE;
		}
	    }
	}
    }
  return copyarea;
}

/*
 * locator_attribute_info_force () - Force an object represented by attribute
 *                                   information structure
 *
 * return: NO_ERROR if all OK, ER_ status otherwise
 *
 *   hfid(in): Where of the object
 *   oid(in/out): The object identifier
 *                   (Set as a side effect when operation is insert)
 *   attr_info(in/out): Attribute information
 *                   (Set as a side effect to fill the rest of values)
 *   att_id(in): Updated attr id array
 *   n_att_id(in/out): Updated attr id array length
 *                   (Set as a side effect to fill the rest of values)
 *   operation(in): Type of operation (either LC_FLUSH_INSERT,
 *                   LC_FLUSH_UPDATE, or LC_FLUSH_DELETE)
 *   op_type(in):
 *   scan_cache(in):
 *   force_count(in):
 *   not_check_fk(in):
 *   pruning_type(in):
 *   pcontext(in): partition pruning context
 *   func_preds(in): cached function index expressions
 *   mvcc_reev_data(in): MVCC reevaluation data
 *   force_in_place(in): if UPDATE_INPLACE_NONE then the 'in place' will not be
 *			 forced and the update style will be decided in this
 *			 function. Otherwise the update of the instance will be
 *			 made in place and according to provided style.
 *  need_locking(in): true, if need locking
 *
 * Note: Force an object represented by an attribute information structure.
 *       For insert the oid is set as a side effect.
 *       For delete, the attr_info does not need to be given.
 */
int
locator_attribute_info_force (THREAD_ENTRY * thread_p, const HFID * hfid, OID * oid, HEAP_CACHE_ATTRINFO * attr_info,
			      ATTR_ID * att_id, int n_att_id, LC_COPYAREA_OPERATION operation, int op_type,
			      HEAP_SCANCACHE * scan_cache, int *force_count, bool not_check_fk,
			      REPL_INFO_TYPE repl_info, int pruning_type, PRUNING_CONTEXT * pcontext,
			      FUNC_PRED_UNPACK_INFO * func_preds, MVCC_REEV_DATA * mvcc_reev_data,
			      UPDATE_INPLACE_STYLE force_update_inplace, RECDES * rec_descriptor, bool need_locking)
{
  LC_COPYAREA *copyarea = NULL;
  RECDES new_recdes;
  SCAN_CODE scan = S_SUCCESS;	/* Scan return value for next operation */
  RECDES copy_recdes;
  RECDES *old_recdes = NULL;
  int error_code = NO_ERROR;
  HFID class_hfid;
  OID class_oid;
  MVCC_SNAPSHOT *saved_mvcc_snapshot = NULL;

  /* 
   * While scanning objects, the given scancache does not fix the last
   * accessed page. So, the object must be copied to the record descriptor.
   */
  copy_recdes.data = NULL;

  /* Backup the provided class_oid and class_hfid because the locator actions bellow will change them if this is a
   * pruning operation. This changes must not be reflected in the calls to this function. */
  HFID_COPY (&class_hfid, hfid);
  if (attr_info != NULL)
    {
      COPY_OID (&class_oid, &attr_info->class_oid);
    }
  switch (operation)
    {
    case LC_FLUSH_UPDATE:
    case LC_FLUSH_UPDATE_PRUNE:
    case LC_FLUSH_UPDATE_PRUNE_VERIFY:
      /* MVCC snapshot no needed for now scan_cache->mvcc_snapshot = logtb_get_mvcc_snapshot (thread_p); */
      if (rec_descriptor != NULL)
	{
	  copy_recdes = *rec_descriptor;
	}
      else if (HEAP_IS_UPDATE_INPLACE (force_update_inplace) || need_locking == false)
	{
	  HEAP_GET_CONTEXT context;

	  /* don't consider visiblity, just get the last version of the object */
	  heap_init_get_context (thread_p, &context, oid, &class_oid, &copy_recdes, scan_cache, COPY, NULL_CHN);
	  scan = heap_get_last_version (thread_p, &context);
	  heap_clean_get_context (thread_p, &context);

	  assert ((lock_get_object_lock (oid, &class_oid, LOG_FIND_THREAD_TRAN_INDEX (thread_p)) >= X_LOCK)
		  || (lock_get_object_lock (&class_oid, oid_Root_class_oid,
					    LOG_FIND_THREAD_TRAN_INDEX (thread_p) >= X_LOCK)));
	}
      else
	{
	  /* The oid has been already locked in select phase, however need to get the last object that may differ by
	   * the current one in case that transaction updates same OID many times during command execution */
	  /* TODO: investigate if this is still true */
	  if (scan_cache && scan_cache->mvcc_snapshot != NULL)
	    {
	      /* Why is snapshot set to NULL? */
	      saved_mvcc_snapshot = scan_cache->mvcc_snapshot;
	      scan_cache->mvcc_snapshot = NULL;
	    }

	  scan = locator_lock_and_get_object (thread_p, oid, &class_oid, &copy_recdes, scan_cache, X_LOCK, COPY,
					      NULL_CHN, LOG_ERROR_IF_DELETED);
	  if (saved_mvcc_snapshot != NULL)
	    {
	      scan_cache->mvcc_snapshot = saved_mvcc_snapshot;
	    }
	}

      if (scan == S_SUCCESS)
	{
	  /* do nothing for the moment */
	}
      else if (scan == S_ERROR || scan == S_DOESNT_FIT)
	{
	  /* Whenever an error including an interrupt was broken out, quit the update. */
	  return ER_FAILED;
	}
      else if (scan == S_DOESNT_EXIST)
	{
	  int err_id = er_errid ();
	  if (err_id == ER_HEAP_NODATA_NEWADDRESS)
	    {
	      /* it is an immature record. go ahead to update */
	      er_clear ();	/* clear ER_HEAP_NODATA_NEWADDRESS */
	    }
	  else
	    {
	      return ((err_id == NO_ERROR) ? ER_HEAP_UNKNOWN_OBJECT : err_id);	/* other errors should return S_ERROR? */
	    }
	}
      else if (scan == S_SNAPSHOT_NOT_SATISFIED)
	{
	  return ER_FAILED;
	}
      else
	{
	  assert (false);
	  return ER_FAILED;
	}

      old_recdes = &copy_recdes;

      /* Fall through */

    case LC_FLUSH_INSERT:
    case LC_FLUSH_INSERT_PRUNE:
    case LC_FLUSH_INSERT_PRUNE_VERIFY:
      copyarea =
	locator_allocate_copy_area_by_attr_info (thread_p, attr_info, old_recdes, &new_recdes, -1,
						 LOB_FLAG_INCLUDE_LOB);
      if (copyarea == NULL)
	{
	  error_code = ER_FAILED;
	  break;
	}

      /* Assume that it has indices */
      if (LC_IS_FLUSH_INSERT (operation))
	{
	  error_code =
	    locator_insert_force (thread_p, &class_hfid, &class_oid, oid, &new_recdes, true, op_type, scan_cache,
				  force_count, pruning_type, pcontext, func_preds, UPDATE_INPLACE_NONE);
	}
      else
	{
	  int has_index;

	  assert (LC_IS_FLUSH_UPDATE (operation));

	  /* MVCC snapshot no needed for now scan_cache->mvcc_snapshot = logtb_get_mvcc_snapshot (thread_p); */
	  has_index = LC_FLAG_HAS_INDEX;
	  if (heap_attrinfo_check_unique_index (thread_p, attr_info, att_id, n_att_id))
	    {
	      has_index |= LC_FLAG_HAS_UNIQUE_INDEX;
	    }

	  error_code =
	    locator_update_force (thread_p, &class_hfid, &class_oid, oid, old_recdes, &new_recdes, has_index,
				  att_id, n_att_id, op_type, scan_cache, force_count, not_check_fk, repl_info,
				  pruning_type, pcontext, mvcc_reev_data, force_update_inplace, need_locking);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	    }
	}

      if (copyarea != NULL)
	{
	  locator_free_copy_area (copyarea);
	  copyarea = NULL;
	  new_recdes.data = NULL;
	  new_recdes.area_size = 0;
	}
      break;

    case LC_FLUSH_DELETE:
      error_code =
	locator_delete_force (thread_p, &class_hfid, oid, true, op_type, scan_cache, force_count, mvcc_reev_data,
			      need_locking);
      break;

    default:
      /* 
       * Problems forcing the object. Don't known what flush/force operation
       * to execute on the object... This is a system error...
       * Maybe, the transaction should be aborted by the caller...Quit..
       */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LC_BADFORCE_OPERATION, 4, operation, oid->volid, oid->pageid,
	      oid->slotid);
      error_code = ER_LC_BADFORCE_OPERATION;
      break;
    }

  return error_code;
}

/*
 * locator_was_index_already_applied () - Check B-Tree was already added
 *                                        or removed entries
 *
 * return: true if index was already applied
 *
 *   index_attrinfo(in): information of indices
 *   btid(in): btid of index
 *   pos(in): index position on indices
 *
 * Note: B-Tree can be shared by constraints (PK, or FK). The shared B-Tree can
 * be added or removed entries one more times during one INSERT or DELETE
 * statement is executing. Therefore, we need to check B-Tree was already added
 * or removed entries.
 */
static bool
locator_was_index_already_applied (HEAP_CACHE_ATTRINFO * index_attrinfo, BTID * btid, int pos)
{
  OR_INDEX *index;
  int i;

  for (i = 0; i < pos; i++)
    {
      index = &(index_attrinfo->last_classrepr->indexes[i]);
      if (BTID_IS_EQUAL (btid, &index->btid))
	{
	  return true;
	}
    }

  return false;
}

/*
 * locator_add_or_remove_index () - Add or remove index entries
 *
 * return: NO_ERROR if all OK, ER_ status otherwise
 *
 *   recdes(in): The object
 *   inst_oid(in): The object identifier
 *   class_oid(in): The class object identifier
 *   is_insert(in): whether to add or remove the object from the indexes
 *   op_type(in):
 *   scan_cache(in):
 *   datayn(in): true if the target object is "data",
 *                false if the target object is "schema"
 *   need_replication(in): true if replication is needed
 *   hfid(in):
 *   func_preds(in): cached function index expressions
 *   idx_action_flag(in): is moving record between partitioned table?
 *			 If FOR_MOVE, this delete(&insert) is caused by
 *			 'UPDATE ... SET ...', NOT 'DELETE FROM ...'
 *
 * Note:Either insert indices (in_insert) or delete indices.
 */
int
locator_add_or_remove_index (THREAD_ENTRY * thread_p, RECDES * recdes, OID * inst_oid, OID * class_oid, int is_insert,
			     int op_type, HEAP_SCANCACHE * scan_cache, bool datayn, bool need_replication, HFID * hfid,
			     FUNC_PRED_UNPACK_INFO * func_preds)
{
  return locator_add_or_remove_index_internal (thread_p, recdes, inst_oid, class_oid, is_insert, op_type, scan_cache,
					       datayn, need_replication, hfid, func_preds, FOR_INSERT_OR_DELETE);
}

/*
 * locator_add_or_remove_index_for_moving () - Add or remove index entries
 *                                             To move record between partitions.
 *
 * return: NO_ERROR if all OK, ER_ status otherwise
 *
 *   recdes(in): The object
 *   inst_oid(in): The object identifier
 *   class_oid(in): The class object identifier
 *   is_insert(in): whether to add or remove the object from the indexes
 *   op_type(in):
 *   scan_cache(in):
 *   datayn(in): true if the target object is "data",
 *                false if the target object is "schema"
 *   need_replication(in): true if replication is needed
 *   hfid(in):
 *   func_preds(in): cached function index expressions
 *   idx_action_flag(in): is moving record between partitioned table?
 *			 If FOR_MOVE, this delete(&insert) is caused by
 *			 'UPDATE ... SET ...', NOT 'DELETE FROM ...'
 *
 * Note:Either insert indices (in_insert) or delete indices.
 */
static int
locator_add_or_remove_index_for_moving (THREAD_ENTRY * thread_p, RECDES * recdes, OID * inst_oid, OID * class_oid,
					int is_insert, int op_type, HEAP_SCANCACHE * scan_cache, bool datayn,
					bool need_replication, HFID * hfid, FUNC_PRED_UNPACK_INFO * func_preds)
{
  return locator_add_or_remove_index_internal (thread_p, recdes, inst_oid, class_oid, is_insert, op_type, scan_cache,
					       datayn, need_replication, hfid, func_preds, FOR_MOVE);
}

/*
 * locator_add_or_remove_index_internal () - helper function for
 *                                     locator_add_or_remove_index () and
 *                                     locator_add_or_remove_index_for_moving ()
 *
 * return: NO_ERROR if all OK, ER_ status otherwise
 *
 *   recdes(in): The object
 *   inst_oid(in): The object identifier
 *   class_oid(in): The class object identifier
 *   is_insert(in): whether to add or remove the object from the indexes
 *   op_type(in):
 *   scan_cache(in):
 *   datayn(in): true if the target object is "data",
 *                false if the target object is "schema"
 *   need_replication(in): true if replication is needed
 *   hfid(in):
 *   func_preds(in): cached function index expressions
 *   idx_action_flag(in): is moving record between partitioned table?
 *			 If FOR_MOVE, this delete(&insert) is caused by
 *			 'UPDATE ... SET ...', NOT 'DELETE FROM ...'
 *
 * Note:Either insert indices (in_insert) or delete indices.
 */
static int
locator_add_or_remove_index_internal (THREAD_ENTRY * thread_p, RECDES * recdes, OID * inst_oid, OID * class_oid,
				      int is_insert, int op_type, HEAP_SCANCACHE * scan_cache, bool datayn,
				      bool need_replication, HFID * hfid, FUNC_PRED_UNPACK_INFO * func_preds,
				      LOCATOR_INDEX_ACTION_FLAG idx_action_flag)
{
  int num_found;
  int i, num_btids;
  HEAP_CACHE_ATTRINFO index_attrinfo;
  BTID btid;
  DB_VALUE *key_dbvalue, *key_ins_del = NULL;
  DB_VALUE dbvalue;
  int dummy_unique;
  BTREE_UNIQUE_STATS *unique_stat_info;
  HEAP_IDX_ELEMENTS_INFO idx_info;
  char buf[DBVAL_BUFSIZE + MAX_ALIGNMENT], *aligned_buf;
  OR_INDEX *index;
  int error_code = NO_ERROR;
  PR_EVAL_FNC filter_eval_func = NULL;
  DB_TYPE single_node_type = DB_TYPE_NULL;
  OR_PREDICATE *or_pred = NULL;
  PRED_EXPR_WITH_CONTEXT *pred_filter = NULL;
  DB_LOGICAL ev_res;
  bool use_mvcc = false;
  MVCCID mvccid;
  MVCC_REC_HEADER *p_mvcc_rec_header = NULL;

/* temporary disable standalone optimization (non-mvcc insert/delete style).
 * Must be activated when dynamic heap is introduced */
/* #if defined (SERVER_MODE) */
  MVCC_REC_HEADER mvcc_rec_header[2];
/* #endif */

#if defined(ENABLE_SYSTEMTAP)
  char *classname = NULL;
#endif /* ENABLE_SYSTEMTAP */

  assert_release (class_oid != NULL);
  assert_release (!OID_ISNULL (class_oid));

  key_dbvalue = NULL;
  DB_MAKE_NULL (&dbvalue);

  aligned_buf = PTR_ALIGN (buf, MAX_ALIGNMENT);

#if defined(SERVER_MODE)
  if (!mvcc_is_mvcc_disabled_class (class_oid))
    {
      /* Use MVCC if it's not disabled for current class */
      use_mvcc = true;
      mvccid = logtb_get_current_mvccid (thread_p);
    }
#endif /* SERVER_MODE */

  /* 
   *  Populate the index_attrinfo structure.
   *  Return the number of indexed attributes found.
   */
  num_found = heap_attrinfo_start_with_index (thread_p, class_oid, NULL, &index_attrinfo, &idx_info);
  num_btids = idx_info.num_btids;

  if (num_found == 0)
    {
      return NO_ERROR;
    }
  else if (num_found < 0)
    {
      return ER_FAILED;
    }

  /* 
   *  At this point, there are indices and the index attrinfo has
   *  been initialized
   *
   *  Read the values of the indexed attributes
   */
  if (idx_info.has_single_col)
    {
      error_code = heap_attrinfo_read_dbvalues (thread_p, inst_oid, recdes, NULL, &index_attrinfo);
      if (error_code != NO_ERROR)
	{
	  goto error;
	}
    }

#if defined(ENABLE_SYSTEMTAP)
  classname = heap_get_class_name (thread_p, class_oid);
#endif /* ENABLE_SYSTEMTAP */

  for (i = 0; i < num_btids; i++)
    {
      index = &(index_attrinfo.last_classrepr->indexes[i]);
      or_pred = index->filter_predicate;
      if (or_pred && or_pred->pred_stream)
	{
	  error_code =
	    locator_eval_filter_predicate (thread_p, &index->btid, or_pred, class_oid, &inst_oid, 1, &recdes, &ev_res);
	  if (error_code == ER_FAILED)
	    {
	      goto error;
	    }
	  else if (ev_res != V_TRUE)
	    {
	      continue;
	    }
	}
      /* 
       *  Generate a B-tree key contained in a DB_VALUE and return a
       *  pointer to it.
       */
      key_dbvalue =
	heap_attrvalue_get_key (thread_p, i, &index_attrinfo, recdes, &btid, &dbvalue, aligned_buf,
				(func_preds ? &func_preds[i] : NULL), NULL);
      if (key_dbvalue == NULL)
	{
	  error_code = ER_FAILED;
	  goto error;
	}

      if (i < 1 || !locator_was_index_already_applied (&index_attrinfo, &index->btid, i))
	{
	  if (scan_cache == NULL)
	    {
	      unique_stat_info = NULL;
	    }
	  else
	    {
	      if (op_type == MULTI_ROW_UPDATE || op_type == MULTI_ROW_INSERT || op_type == MULTI_ROW_DELETE)
		{
		  unique_stat_info = &(scan_cache->index_stat_info[i]);
		}
	      else
		{
		  unique_stat_info = NULL;
		}
	    }
	  if (use_mvcc)
	    {
	      btree_set_mvcc_header_ids_for_update (thread_p, !is_insert, is_insert, &mvccid, mvcc_rec_header);
	      p_mvcc_rec_header = mvcc_rec_header;
	    }

	  if (is_insert)
	    {
#if defined(ENABLE_SYSTEMTAP)
	      CUBRID_IDX_INSERT_START (classname, index->btname);
#endif /* ENABLE_SYSTEMTAP */

	      if (index->type == BTREE_FOREIGN_KEY)
		{
		  if (lock_object (thread_p, inst_oid, class_oid, X_LOCK, LK_UNCOND_LOCK) != LK_GRANTED)
		    {
		      goto error;
		    }
		}

	      error_code =
		btree_insert (thread_p, &btid, key_dbvalue, class_oid, inst_oid, op_type, unique_stat_info,
			      &dummy_unique, p_mvcc_rec_header);

#if defined(ENABLE_SYSTEMTAP)
	      CUBRID_IDX_INSERT_END (classname, index->btname, (error_code != NO_ERROR));
#endif /* ENABLE_SYSTEMTAP */
	    }
	  else
	    {
#if defined(ENABLE_SYSTEMTAP)
	      CUBRID_IDX_DELETE_START (classname, index->btname);
#endif /* ENABLE_SYSTEMTAP */

	      if (use_mvcc == true)
		{
		  /* in MVCC logical deletion means MVCC DEL_ID insertion */
		  error_code =
		    btree_mvcc_delete (thread_p, &btid, key_dbvalue, class_oid, inst_oid, op_type, unique_stat_info,
				       &dummy_unique, p_mvcc_rec_header);
		}
	      else
		{
		  error_code =
		    btree_physical_delete (thread_p, &btid, key_dbvalue, inst_oid, class_oid, &dummy_unique, op_type,
					   unique_stat_info);
		  if (error_code != NO_ERROR)
		    {
		      ASSERT_ERROR ();

#if defined(ENABLE_SYSTEMTAP)
		      CUBRID_IDX_DELETE_END (classname, index->btname, 1);
#endif /* ENABLE_SYSTEMTAP */

		      goto error;
		    }
		}
#if defined(ENABLE_SYSTEMTAP)
	      CUBRID_IDX_DELETE_END (classname, index->btname, (error_code != NO_ERROR));
#endif /* ENABLE_SYSTEMTAP */

	    }
	}

      if (!locator_Dont_check_foreign_key && index->type == BTREE_PRIMARY_KEY && index->fk)
	{
	  /* actually key_prefix_length is -1 for BTREE_PRIMARY_KEY; must be changed when key_prefix_length will be
	   * added to FK and PK */
	  if (is_insert)
	    {
	      ;			/* nop */
	    }
	  else
	    {
	      if (idx_action_flag == FOR_MOVE)
		{
		  /* This delete is caused by 'UPDATE ... SET ...' between partitioned tables. It first delete a
		   * record in a partitioned table and insert a record in another partitioned table. It should check
		   * the 'ON UPDATE ...' condition, not check 'ON DELETE ...' condition. */
		  error_code = locator_check_primary_key_update (thread_p, index, key_dbvalue);
		}
	      else
		{
		  error_code = locator_check_primary_key_delete (thread_p, index, key_dbvalue);
		}
	      if (error_code != NO_ERROR)
		{
		  goto error;
		}
	    }
	}

      /* 
       * for replication,
       * Following step would be executed only when the target index is a
       * primary key.
       * The right place to insert a replication log info is here
       * to avoid another "fetching key values"
       * Generates the replication log info. for data insert/delete
       * for the update cases, refer to locator_update_force()
       */
      if (need_replication && index->type == BTREE_PRIMARY_KEY && error_code == NO_ERROR
	  && !LOG_CHECK_LOG_APPLIER (thread_p) && log_does_allow_replication () == true)
	{
	  error_code =
	    repl_log_insert (thread_p, class_oid, inst_oid, datayn ? LOG_REPLICATION_DATA : LOG_REPLICATION_STATEMENT,
			     is_insert ? RVREPL_DATA_INSERT : RVREPL_DATA_DELETE, key_dbvalue,
			     REPL_INFO_TYPE_RBR_NORMAL);
	}
      if (error_code != NO_ERROR)
	{
	  assert (er_errid () != NO_ERROR);
	  goto error;
	}

#if 0
      if (key_ins_del != NULL && !DB_IS_NULL (key_dbvalue) && !btree_multicol_key_is_null (key_dbvalue))
	{
	  BTREE_CHECKSCAN bt_checkscan;
	  DISK_ISVALID isvalid = DISK_VALID;

	  /* start a check-scan on index */
	  if (btree_keyoid_checkscan_start (thread_p, &btid, &bt_checkscan) != NO_ERROR)
	    {
	      goto error;
	    }

	  isvalid = btree_keyoid_checkscan_check (thread_p, &bt_checkscan, class_oid, key_dbvalue, inst_oid);

	  if (er_errid () == ER_INTERRUPTED)
	    {
	      /* in case of user interrupt */
	      ;			/* do not check isvalid */
	    }
	  else
	    {
	      if (is_insert)
		{
		  assert (isvalid == DISK_VALID);	/* found */
		}
	      else
		{
		  assert (isvalid == DISK_INVALID);	/* not found */
		}
	    }

	  /* close the index check-scan */
	  btree_keyoid_checkscan_end (thread_p, &bt_checkscan);
	}
#endif

      if (key_dbvalue == &dbvalue)
	{
	  pr_clear_value (&dbvalue);
	  key_dbvalue = NULL;
	}
    }

error:

  heap_attrinfo_end (thread_p, &index_attrinfo);

  if (key_dbvalue == &dbvalue)
    {
      pr_clear_value (&dbvalue);
      key_dbvalue = NULL;
    }

#if defined(ENABLE_SYSTEMTAP)
  if (classname != NULL)
    {
      free_and_init (classname);
    }
#endif /* ENABLE_SYSTEMTAP */

  return error_code;
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * locator_make_midxkey_domain () -
 *
 * return:
 *
 *   index(in):
 */
static TP_DOMAIN *
locator_make_midxkey_domain (OR_INDEX * index)
{
  TP_DOMAIN *set_domain;
  TP_DOMAIN *domain = NULL;

  OR_ATTRIBUTE **atts;
  int num_atts, i;

  if (index == NULL || index->n_atts < 2)
    {
      return NULL;
    }

  num_atts = index->n_atts;
  atts = index->atts;

  set_domain = NULL;
  for (i = 0; i < num_atts; i++)
    {
      if (i == 0)
	{
	  set_domain = tp_domain_copy (atts[i]->domain, 0);
	  if (set_domain == NULL)
	    {
	      return NULL;
	    }
	  domain = set_domain;
	}
      else
	{
	  domain->next = tp_domain_copy (atts[i]->domain, 0);
	  if (domain->next == NULL)
	    {
	      goto error;
	    }
	  domain = domain->next;
	}
    }

  domain = tp_domain_construct (DB_TYPE_MIDXKEY, (DB_OBJECT *) 0, num_atts, 0, set_domain);

  if (domain)
    {
      return tp_domain_cache (domain);
    }
  else
    {
      goto error;
    }

error:

  if (set_domain)
    {
      TP_DOMAIN *td, *next;

      /* tp_domain_free(set_domain); */
      for (td = set_domain, next = NULL; td != NULL; td = next)
	{
	  next = td->next;
	  tp_domain_free (td);
	}
    }

  return NULL;
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * locator_eval_filter_predicate () - evaluate index filter predicate
 *
 *   return: error code
 *
 *   btid(in): btid of index
 *   or_pred(in): index filter predicate
 *   class_oid(in): object identifier of the class
 *   inst_oids(in): object identifiers of the instances
 *   num_insts(in): the length of inst_oids, recs, results
 *   recs(in): record descriptors
 *   results(out): predicate evaluation results
 */
static int
locator_eval_filter_predicate (THREAD_ENTRY * thread_p, BTID * btid, OR_PREDICATE * or_pred, OID * class_oid,
			       OID ** inst_oids, int num_insts, RECDES ** recs, DB_LOGICAL * results)
{
  int i;
  PRED_EXPR_WITH_CONTEXT *pred_filter = NULL;
  PR_EVAL_FNC filter_eval_func = NULL;
  DB_TYPE single_node_type = DB_TYPE_NULL;
  HL_HEAPID old_pri_heap_id = HL_NULL_HEAPID;
  int error_code = NO_ERROR;

  if (or_pred == NULL || class_oid == NULL || recs == NULL || inst_oids == NULL || num_insts <= 0 || results == NULL
      || or_pred->pred_stream == NULL || btid == NULL)
    {
      assert (false);
      return ER_FAILED;
    }

  error_code = fpcache_claim (thread_p, btid, or_pred, &pred_filter);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }
  assert (pred_filter != NULL);

  error_code =
    heap_attrinfo_start (thread_p, class_oid, pred_filter->num_attrs_pred, pred_filter->attrids_pred,
			 pred_filter->cache_pred);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto end;
    }
  filter_eval_func = eval_fnc (thread_p, pred_filter->pred, &single_node_type);
  assert (filter_eval_func != NULL);

  for (i = 0; i < num_insts; i++)
    {
      error_code = heap_attrinfo_read_dbvalues (thread_p, inst_oids[i], recs[i], NULL, pred_filter->cache_pred);
      if (error_code != NO_ERROR)
	{
	  goto end;
	}

      /* use global heap for memory allocation */
      old_pri_heap_id = db_change_private_heap (thread_p, 0);
      results[i] = (*filter_eval_func) (thread_p, pred_filter->pred, NULL, inst_oids[i]);
      if (results[i] == V_ERROR)
	{
	  /* restore private heap */
	  (void) db_change_private_heap (thread_p, old_pri_heap_id);
	  error_code = ER_FAILED;
	  goto end;
	}
      /* restore private heap */
      (void) db_change_private_heap (thread_p, old_pri_heap_id);
    }

end:
  if (pred_filter)
    {
      heap_attrinfo_end (thread_p, pred_filter->cache_pred);
      /* Except for db_value regu variable, all regu variables from pred expression are cleared. */
      old_pri_heap_id = db_change_private_heap (thread_p, 0);
      qexec_clear_pred_context (thread_p, pred_filter, false);
      (void) db_change_private_heap (thread_p, old_pri_heap_id);
    }

  fpcache_retire (thread_p, class_oid, btid, pred_filter);
  return error_code;
}

/*
 * locator_update_index () - Update index entries
 *
 * return: NO_ERROR if all OK, ER_ status otherwise
 *
 *   new_recdes(in): The new recdes object
 *   old_recdes(in): The old recdes object
 *   att_id(in): Updated attr id array
 *   n_att_id(in): Updated attr id array length
 *   oid(in): The identifier of old/new object
 *   class_oid(in): The class object identifier
 *   op_type(in):
 *   scan_cache(in):
 *   repl_info(in/out): replication info, set need_replication to false when
 *                      no primary is found
 *
 * Note: Update the index entries of the given object.
 */
int
locator_update_index (THREAD_ENTRY * thread_p, RECDES * new_recdes, RECDES * old_recdes, ATTR_ID * att_id, int n_att_id,
		      OID * oid, OID * class_oid, int op_type, HEAP_SCANCACHE * scan_cache, REPL_INFO * repl_info)
{
  HEAP_CACHE_ATTRINFO space_attrinfo[2];
  HEAP_CACHE_ATTRINFO *new_attrinfo = NULL;
  HEAP_CACHE_ATTRINFO *old_attrinfo = NULL;
  int new_num_found, old_num_found;
  BTID new_btid, old_btid;
  int pk_btid_index = -1;
  DB_VALUE *new_key = NULL, *old_key = NULL;
  DB_VALUE *repl_old_key = NULL;
  DB_VALUE new_dbvalue, old_dbvalue;
  bool new_isnull, old_isnull;
  PR_TYPE *pr_type;
  OR_INDEX *index = NULL;
  int i, j, k, num_btids, old_num_btids, unique;
  bool found_btid = true;
  BTREE_UNIQUE_STATS *unique_stat_info;
  HEAP_IDX_ELEMENTS_INFO new_idx_info;
  HEAP_IDX_ELEMENTS_INFO old_idx_info;
  char newbuf[DBVAL_BUFSIZE + MAX_ALIGNMENT], *aligned_newbuf;
  char oldbuf[DBVAL_BUFSIZE + MAX_ALIGNMENT], *aligned_oldbuf;
  DB_TYPE dbval_type;
  TP_DOMAIN *key_domain = NULL;
  int error_code;
  DB_LOGICAL ev_results[2];
  bool do_delete_only = false;
  bool do_insert_only = false;
  OID *inst_oids[2];
  RECDES *recs[2];
  bool same_key = true;
  int c = DB_UNK;
  bool use_mvcc = false;
  MVCC_REC_HEADER *p_mvcc_rec_header = NULL;
  /* TODO: MVCC_BTREE_DELETE_OBJECT is removed due to recovery issue regarding MVCCID. Must find a solution to recover
   * MVCC info on rollback (otherwise we will have inconsistencies regarding visibility). */
  /* MVCC_BTREE_OP_ARGUMENTS mvcc_args, *mvcc_args_p = NULL; */
  MVCCID mvccid;
/* temporary disable standalone optimization (non-mvcc insert/delete style).
 * Must be activated when dynamic heap is introduced */
/* #if defined(SERVER_MODE) */
  MVCC_REC_HEADER mvcc_rec_header[2];
/* #endif */
#if defined(ENABLE_SYSTEMTAP)
  char *classname = NULL;
  bool is_started = false;
#endif /* ENABLE_SYSTEMTAP */
  LOG_TDES *tdes;
  LOG_LSA preserved_repl_lsa;
  int tran_index;

  assert_release (class_oid != NULL);
  assert_release (!OID_ISNULL (class_oid));

  mvccid = MVCCID_NULL;

#if defined(SERVER_MODE)
  if (!mvcc_is_mvcc_disabled_class (class_oid))
    {
      use_mvcc = true;

      mvccid = logtb_get_current_mvccid (thread_p);
    }
#endif /* SERVER_MODE */

  LSA_SET_NULL (&preserved_repl_lsa);

  DB_MAKE_NULL (&new_dbvalue);
  DB_MAKE_NULL (&old_dbvalue);

  aligned_newbuf = PTR_ALIGN (newbuf, MAX_ALIGNMENT);
  aligned_oldbuf = PTR_ALIGN (oldbuf, MAX_ALIGNMENT);

  new_num_found = heap_attrinfo_start_with_index (thread_p, class_oid, NULL, &space_attrinfo[0], &new_idx_info);
  num_btids = new_idx_info.num_btids;
  if (new_num_found < 0)
    {
      return ER_FAILED;
    }
  new_attrinfo = &space_attrinfo[0];

  old_num_found = heap_attrinfo_start_with_index (thread_p, class_oid, NULL, &space_attrinfo[1], &old_idx_info);
  old_num_btids = old_idx_info.num_btids;
  if (old_num_found < 0)
    {
      error_code = ER_FAILED;
      goto error;
    }
  old_attrinfo = &space_attrinfo[1];

  if (new_num_found != old_num_found)
    {
      if (new_num_found > 0)
	{
	  heap_attrinfo_end (thread_p, &space_attrinfo[0]);
	}
      if (old_num_found > 0)
	{
	  heap_attrinfo_end (thread_p, &space_attrinfo[1]);
	}
      return ER_FAILED;
    }

  if (new_num_found == 0)
    {
      /* No need to replicate this record since there is no primary key */
      if (repl_info != NULL)
	{
	  repl_info->need_replication = false;
	}

      return NO_ERROR;
    }

  /* 
   * There are indices and the index attrinfo has been initialized
   * Indices must be updated when the indexed attributes have changed in value
   * Get the new and old values of key and update the index when
   * the keys are different
   */

  new_attrinfo = &space_attrinfo[0];
  old_attrinfo = &space_attrinfo[1];

  error_code = heap_attrinfo_read_dbvalues (thread_p, oid, new_recdes, NULL, new_attrinfo);
  if (error_code != NO_ERROR)
    {
      goto error;
    }
  error_code = heap_attrinfo_read_dbvalues (thread_p, oid, old_recdes, NULL, old_attrinfo);
  if (error_code != NO_ERROR)
    {
      goto error;
    }

  /* 
   *  Ensure that we have the same number of indexes and
   *  get the number of B-tree IDs.
   */
  if (old_attrinfo->last_classrepr->n_indexes != new_attrinfo->last_classrepr->n_indexes)
    {
      error_code = ER_FAILED;
      goto error;
    }

#if defined(ENABLE_SYSTEMTAP)
  classname = heap_get_class_name (thread_p, class_oid);
#endif /* ENABLE_SYSTEMTAP */

  for (i = 0; i < num_btids; i++)
    {
      index = &(new_attrinfo->last_classrepr->indexes[i]);
      if (pk_btid_index == -1 && repl_info != NULL && repl_info->need_replication == true
	  && !LOG_CHECK_LOG_APPLIER (thread_p) && index->type == BTREE_PRIMARY_KEY
	  && log_does_allow_replication () == true)
	{
	  pk_btid_index = i;
	}

      /* check for specified update attributes */
      if ((att_id != NULL) && ((use_mvcc == false) || (index->type == BTREE_PRIMARY_KEY && index->fk != NULL)))
	{
	  found_btid = false;	/* guess as not found */

	  for (j = 0; j < n_att_id && !found_btid; j++)
	    {
	      for (k = 0; k < index->n_atts && !found_btid; k++)
		{
		  if (att_id[j] == (ATTR_ID) (index->atts[k]->id))
		    {		/* the index key_type has updated attr */
		      found_btid = true;
		    }
		}
	    }

	  /* in MVCC, in case of BTREE_PRIMARY_KEY having FK need to update PK index but skip foreign key restrictions
	   * checking */
	  if (!found_btid && !index->filter_predicate && (index->type != BTREE_PRIMARY_KEY || index->fk == NULL))
	    {
	      continue;		/* skip and go ahead */
	    }
	}

      do_delete_only = false;
      do_insert_only = false;
      if (index->filter_predicate)
	{
	  inst_oids[0] = inst_oids[1] = oid;
	  recs[0] = old_recdes;
	  recs[1] = new_recdes;
	  error_code =
	    locator_eval_filter_predicate (thread_p, &index->btid, index->filter_predicate, class_oid, inst_oids, 2,
					   recs, ev_results);
	  if (error_code == ER_FAILED)
	    {
	      goto error;
	    }

	  if (ev_results[0] != V_TRUE)
	    {
	      if (ev_results[1] != V_TRUE)
		{
		  /* the old rec and the new rec does not satisfied the filter predicate */
		  continue;
		}
	      else
		{
		  /* the old rec does not satisfied the filter predicate */
		  /* the new rec satisfied the filter predicate */
		  do_insert_only = true;
		}
	    }
	  else
	    {
	      if (ev_results[1] != V_TRUE)
		{
		  /* the old rec satisfied the filter predicate the new rec does not satisfied the filter predicate */
		  do_delete_only = true;
		}
	      else
		{
		  if (found_btid == false)
		    {
		      /* the old rec satisfied the filter predicate the new rec satisfied the filter predicate the
		       * index does not contain updated attributes */
		      continue;
		    }
		  /* nothing to do - update operation */
		}
	    }
	}

      new_key =
	heap_attrvalue_get_key (thread_p, i, new_attrinfo, new_recdes, &new_btid, &new_dbvalue, aligned_newbuf, NULL,
				NULL);
      old_key =
	heap_attrvalue_get_key (thread_p, i, old_attrinfo, old_recdes, &old_btid, &old_dbvalue, aligned_oldbuf, NULL,
				&key_domain);

      if ((new_key == NULL) || (old_key == NULL))
	{
	  error_code = ER_FAILED;
	  goto error;
	}

      dbval_type = DB_VALUE_DOMAIN_TYPE (old_key);
      if (DB_VALUE_DOMAIN_TYPE (new_key) != dbval_type)
	{
	  error_code = ER_FAILED;
	  goto error;
	}

      if (scan_cache == NULL)
	{
	  unique_stat_info = NULL;
	}
      else
	{
	  if (op_type == MULTI_ROW_UPDATE || op_type == MULTI_ROW_INSERT || op_type == MULTI_ROW_DELETE)
	    {
	      unique_stat_info = &(scan_cache->index_stat_info[i]);
	    }
	  else
	    {
	      unique_stat_info = NULL;
	    }
	}

      new_isnull = db_value_is_null (new_key);
      old_isnull = db_value_is_null (old_key);
      pr_type = PR_TYPE_FROM_ID (dbval_type);
      if (pr_type == NULL)
	{
	  error_code = ER_FAILED;
	  goto error;
	}

      assert (key_domain != NULL);
      if (pr_type->id == DB_TYPE_MIDXKEY)
	{
	  assert (TP_DOMAIN_TYPE (key_domain) == DB_TYPE_MIDXKEY);
	  new_key->data.midxkey.domain = old_key->data.midxkey.domain = key_domain;
	}
      else
	{
	  assert (old_isnull || TP_ARE_COMPARABLE_KEY_TYPES (TP_DOMAIN_TYPE (key_domain), pr_type->id));
	}

      same_key = true;		/* init */
      if ((new_isnull && !old_isnull) || (old_isnull && !new_isnull))
	{
	  same_key = false;
	}
      else
	{
	  if (!(new_isnull && old_isnull))
	    {
	      c = btree_compare_key (old_key, new_key, key_domain, 0, 1, NULL);

	      if (c == DB_UNK)
		{
		  assert (er_errid () != NO_ERROR);
		  error_code = er_errid ();
		  goto error;
		}

	      if (c != DB_EQ)
		{
		  same_key = false;
		}
	    }
	}

#if defined(ENABLE_SYSTEMTAP)
      CUBRID_IDX_UPDATE_START (classname, index->btname);
      is_started = true;
#endif /* ENABLE_SYSTEMTAP */

      if (!same_key || do_delete_only || do_insert_only)
	{
	  if (i < 1 || !locator_was_index_already_applied (new_attrinfo, &index->btid, i))
	    {
	      if (mvccid != MVCCID_NULL)
		{
		  btree_set_mvcc_header_ids_for_update (thread_p, do_delete_only, do_insert_only, &mvccid,
							mvcc_rec_header);
		  p_mvcc_rec_header = mvcc_rec_header;
		}
	      if (do_delete_only)
		{
		  if (use_mvcc)
		    {
		      /* in MVCC logical deletion means MVCC DEL_ID insertion */
		      error_code =
			btree_mvcc_delete (thread_p, &old_btid, old_key, class_oid, oid, op_type, unique_stat_info,
					   &unique, p_mvcc_rec_header);
		      if (error_code != NO_ERROR)
			{
			  assert (er_errid () != NO_ERROR);
			  goto error;
			}
		    }
		  else
		    {
		      error_code =
			btree_physical_delete (thread_p, &old_btid, old_key, oid, class_oid, &unique, op_type,
					       unique_stat_info);
		      if (error_code != NO_ERROR)
			{
			  ASSERT_ERROR ();
			  goto error;
			}
		    }
		}
	      else
		{
		  /* in MVCC - update index key means insert index key */
		  if ((do_insert_only == true))
		    {
		      if (index->type == BTREE_FOREIGN_KEY)
			{
			  if (lock_object (thread_p, oid, class_oid, X_LOCK, LK_UNCOND_LOCK) != LK_GRANTED)
			    {
			      goto error;
			    }
			}

		      error_code =
			btree_insert (thread_p, &old_btid, new_key, class_oid, oid, op_type, unique_stat_info,
				      &unique, p_mvcc_rec_header);
		      if (error_code != NO_ERROR)
			{
			  ASSERT_ERROR ();
			  goto error;
			}
		    }
		  else
		    {
		      error_code =
			btree_update (thread_p, &old_btid, old_key, new_key, class_oid, oid, op_type,
				      unique_stat_info, &unique, p_mvcc_rec_header);

		      if (error_code != NO_ERROR)
			{
			  goto error;
			}
		    }
		}
	    }

#if defined(ENABLE_SYSTEMTAP)
	  CUBRID_IDX_UPDATE_END (classname, index->btname, 0);
#endif /* ENABLE_SYSTEMTAP */

	  /* In MVCC need to check for specified update attributes */
	  if (!locator_Dont_check_foreign_key && !same_key && index->type == BTREE_PRIMARY_KEY && index->fk
	      && found_btid)
	    {
	      assert (do_insert_only == false && do_delete_only == false);

	      tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
	      tdes = LOG_FIND_TDES (tran_index);

	      if (pk_btid_index == i)
		{
		  /* 
		   * save lsa before it is overwritten by FK action. No need
		   * to consider in-place update (repl_update_lsa) since it updates
		   * index first and then updates heap
		   */
		  LSA_COPY (&preserved_repl_lsa, &tdes->repl_insert_lsa);
		  LSA_SET_NULL (&tdes->repl_insert_lsa);
		}

	      error_code = locator_check_primary_key_update (thread_p, index, old_key);
	      if (error_code != NO_ERROR)
		{
		  goto error;
		}

	      if (pk_btid_index == i)
		{
		  /* restore repl_insert_lsa */
		  assert (LSA_ISNULL (&tdes->repl_insert_lsa));
		  LSA_COPY (&tdes->repl_insert_lsa, &preserved_repl_lsa);
		}
	    }

#if 0
	  {
	    BTREE_CHECKSCAN bt_checkscan;
	    DISK_ISVALID isvalid = DISK_VALID;

	    /* start a check-scan on index */
	    if (btree_keyoid_checkscan_start (thread_p, &old_btid, &bt_checkscan) != NO_ERROR)
	      {
		goto error;
	      }

	    if (!do_insert_only && !DB_IS_NULL (old_key) && !btree_multicol_key_is_null (old_key))
	      {
		isvalid = btree_keyoid_checkscan_check (thread_p, &bt_checkscan, class_oid, old_key, old_oid);

		if (er_errid () == ER_INTERRUPTED)
		  {
		    /* in case of user interrupt */
		    ;		/* do not check isvalid */
		  }
		else
		  {
		    assert (isvalid == DISK_INVALID);	/* not found */
		  }
	      }

	    if (!do_delete_only && !DB_IS_NULL (new_key) && !btree_multicol_key_is_null (new_key))
	      {

		isvalid = btree_keyoid_checkscan_check (thread_p, &bt_checkscan, class_oid, new_key, new_oid);
		if (er_errid () == ER_INTERRUPTED)
		  {
		    /* in case of user interrupt */
		    ;		/* do not check isvalid */
		  }
		else
		  {
		    assert (isvalid == DISK_VALID);	/* found */
		  }
	      }

	    /* close the index check-scan */
	    btree_keyoid_checkscan_end (thread_p, &bt_checkscan);
	  }
#endif
	}

      if (pk_btid_index == i && repl_old_key == NULL)
	{
	  repl_old_key = pr_make_ext_value ();
	  pr_clone_value (old_key, repl_old_key);
	}

      if (new_key == &new_dbvalue)
	{
	  pr_clear_value (&new_dbvalue);
	  new_key = NULL;
	}
      if (old_key == &old_dbvalue)
	{
	  pr_clear_value (&old_dbvalue);
	  old_key = NULL;
	}
    }

  if (pk_btid_index != -1)
    {
      assert (repl_info != NULL);

      if (repl_old_key == NULL)
	{
	  key_domain = NULL;
	  repl_old_key =
	    heap_attrvalue_get_key (thread_p, pk_btid_index, old_attrinfo, old_recdes, &old_btid, &old_dbvalue,
				    aligned_oldbuf, NULL, &key_domain);
	  if (repl_old_key == NULL)
	    {
	      error_code = ER_FAILED;
	      goto error;
	    }

	  old_isnull = db_value_is_null (repl_old_key);
	  pr_type = pr_type_from_id (DB_VALUE_DOMAIN_TYPE (repl_old_key));
	  if (pr_type == NULL)
	    {
	      error_code = ER_FAILED;
	      goto error;
	    }

	  if (pr_type->id == DB_TYPE_MIDXKEY)
	    {
	      /* 
	       * The asc/desc properties in midxkey from log_applier may be
	       * inaccurate. therefore, we should use btree header's domain
	       * while processing btree search request from log_applier.
	       */
	      repl_old_key->data.midxkey.domain = key_domain;
	    }

	  error_code =
	    repl_log_insert (thread_p, class_oid, oid, LOG_REPLICATION_DATA, RVREPL_DATA_UPDATE, repl_old_key,
			     repl_info->repl_info_type);
	  if (repl_old_key == &old_dbvalue)
	    {
	      pr_clear_value (&old_dbvalue);
	    }
	}
      else
	{
	  error_code =
	    repl_log_insert (thread_p, class_oid, oid, LOG_REPLICATION_DATA, RVREPL_DATA_UPDATE, repl_old_key,
			     repl_info->repl_info_type);
	  pr_free_ext_value (repl_old_key);
	  repl_old_key = NULL;
	}
    }
  else
    {
      /* 
       * clear repl_insert_lsa to make sure that FK action
       * does not overwrite the original update's target lsa.
       */
      tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
      tdes = LOG_FIND_TDES (tran_index);

      LSA_SET_NULL (&tdes->repl_insert_lsa);

      /* No need to replicate this record since there is no primary key */
      if (repl_info != NULL)
	{
	  repl_info->need_replication = false;
	}
    }

  heap_attrinfo_end (thread_p, new_attrinfo);
  heap_attrinfo_end (thread_p, old_attrinfo);

#if defined(ENABLE_SYSTEMTAP)
  if (classname != NULL)
    {
      free_and_init (classname);
    }
#endif /* ENABLE_SYSTEMTAP */

  return error_code;

error:

  if (new_key == &new_dbvalue)
    {
      pr_clear_value (&new_dbvalue);
      new_key = NULL;
    }
  if (old_key == &old_dbvalue)
    {
      pr_clear_value (&old_dbvalue);
      old_key = NULL;
    }

  if (repl_old_key != NULL)
    {
      pr_free_ext_value (repl_old_key);
    }

  /* Deallocate any index_list .. if any */

  if (new_attrinfo != NULL)
    {
      heap_attrinfo_end (thread_p, new_attrinfo);
    }

  if (old_attrinfo != NULL)
    {
      heap_attrinfo_end (thread_p, old_attrinfo);
    }

#if defined(ENABLE_SYSTEMTAP)
  if (is_started == true)
    {
      CUBRID_IDX_UPDATE_END (classname, index->btname, 1);
    }
  if (classname != NULL)
    {
      free_and_init (classname);
    }
#endif /* ENABLE_SYSTEMTAP */

  return error_code;
}

/*
 * xlocator_remove_class_from_index () - Removes class instances from the B-tree
 *
 * return: NO_ERROR if all OK, ER_ status otherwise
 *
 *   class_oid(in): The class object identifier
 *   btid(in): B-tree ID
 *   hfid(in): Heap ID
 *
 * Note: This function searches for instances belonging to the passes
 *      class OID and removes the ones found.  This function is used to
 *      remove a class from a spanning B-tree such as a UNIQUE.
 */
int
xlocator_remove_class_from_index (THREAD_ENTRY * thread_p, OID * class_oid, BTID * btid, HFID * hfid)
{
  HEAP_CACHE_ATTRINFO index_attrinfo;
  HEAP_SCANCACHE scan_cache;
  OID inst_oid, *p_inst_oid = &inst_oid;
  int key_index, i, num_btids, num_found, dummy_unique, key_found;
  RECDES copy_rec, *p_copy_rec = &copy_rec;
  BTID inst_btid;
  DB_VALUE dbvalue;
  DB_VALUE *dbvalue_ptr = NULL;
  DB_VALUE *key_del = NULL;
  SCAN_CODE scan;
  char *new_area;
  BTREE_UNIQUE_STATS unique_info;
  HEAP_IDX_ELEMENTS_INFO idx_info;
  char buf[DBVAL_BUFSIZE + MAX_ALIGNMENT], *aligned_buf;
  int error_code = NO_ERROR;
  OR_INDEX *index = NULL;
  DB_LOGICAL ev_res;
  MVCC_SNAPSHOT *mvcc_snapshot = NULL;

  if (class_oid != NULL && !OID_IS_ROOTOID (class_oid))
    {
      mvcc_snapshot = logtb_get_mvcc_snapshot (thread_p);
      if (mvcc_snapshot == NULL)
	{
	  error_code = er_errid ();
	  return (error_code == NO_ERROR ? ER_FAILED : error_code);
	}
    }

  DB_MAKE_NULL (&dbvalue);

  aligned_buf = PTR_ALIGN (buf, MAX_ALIGNMENT);

  /* allocate memory space for copying an instance image. */
  copy_rec.area_size = DB_PAGESIZE;
  copy_rec.data = (char *) malloc (copy_rec.area_size);
  if (copy_rec.data == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) (copy_rec.area_size));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  /* initialize 'unique_info' structure. */
  BTID_COPY (&(unique_info.btid), btid);
  unique_info.num_nulls = 0;
  unique_info.num_keys = 0;
  unique_info.num_oids = 0;

  /* Start a scan cursor */
  error_code = heap_scancache_start (thread_p, &scan_cache, hfid, class_oid, false, false, mvcc_snapshot);
  if (error_code != NO_ERROR)
    {
      free_and_init (copy_rec.data);
      return error_code;
    }

  /* 
   *  Populate the index_attrinfo structure.
   *  Return the number of indexed attributes found.
   */
  num_found = heap_attrinfo_start_with_index (thread_p, class_oid, NULL, &index_attrinfo, &idx_info);
  num_btids = idx_info.num_btids;
  if (num_found < 1)
    {
      (void) heap_scancache_end (thread_p, &scan_cache);
      free_and_init (copy_rec.data);
      return ER_FAILED;
    }

  /* Loop over each instance of the class found in the heap */
  inst_oid.volid = hfid->vfid.volid;
  inst_oid.pageid = NULL_PAGEID;
  inst_oid.slotid = NULL_SLOTID;
  key_found = false;
  key_index = 0;

  while (true)
    {
      if (dbvalue_ptr == &dbvalue)
	{
	  pr_clear_value (&dbvalue);
	  dbvalue_ptr = NULL;
	}

      scan = heap_next (thread_p, hfid, class_oid, &inst_oid, &copy_rec, &scan_cache, COPY);
      if (scan != S_SUCCESS)
	{
	  if (scan != S_DOESNT_FIT)
	    {
	      break;
	    }

	  new_area = (char *) realloc (copy_rec.data, -(copy_rec.length));
	  if (new_area == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) (-(copy_rec.length)));
	      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
	      goto error;
	    }
	  copy_rec.area_size = -copy_rec.length;
	  copy_rec.data = new_area;
	  continue;
	}

      error_code = heap_attrinfo_read_dbvalues (thread_p, &inst_oid, &copy_rec, NULL, &index_attrinfo);
      if (error_code != NO_ERROR)
	{
	  goto error;
	}

      /* Find the correct key by matching the BTID */
      if (key_found == false)
	{
	  for (i = 0; i < num_btids; i++)
	    {
	      if (dbvalue_ptr == &dbvalue)
		{
		  pr_clear_value (&dbvalue);
		  dbvalue_ptr = NULL;
		}

	      dbvalue_ptr =
		heap_attrvalue_get_key (thread_p, i, &index_attrinfo, &copy_rec, &inst_btid, &dbvalue, aligned_buf,
					NULL, NULL);
	      if (dbvalue_ptr == NULL)
		{
		  continue;
		}

	      if (BTID_IS_EQUAL (btid, &inst_btid))
		{
		  key_found = true;
		  key_index = i;
		  index = &(index_attrinfo.last_classrepr->indexes[key_index]);
		  break;
		}
	    }
	}
      /* 
       * We already know the correct BTID index (key_index) so just use it
       * to retrieve the key
       */
      else
	{
	  dbvalue_ptr =
	    heap_attrvalue_get_key (thread_p, key_index, &index_attrinfo, &copy_rec, &inst_btid, &dbvalue, aligned_buf,
				    NULL, NULL);
	}

      /* Delete the instance from the B-tree */
      if (key_found == false || dbvalue_ptr == NULL)
	{
	  error_code = ER_FAILED;
	  goto error;
	}

      assert (index != NULL);
      if (index->filter_predicate)
	{
	  error_code =
	    locator_eval_filter_predicate (thread_p, &index->btid, index->filter_predicate, class_oid, &p_inst_oid, 1,
					   &p_copy_rec, &ev_res);
	  if (error_code != NO_ERROR)
	    {
	      goto error;
	    }

	  if (ev_res != V_TRUE)
	    {
	      continue;
	    }
	}

      error_code =
	btree_physical_delete (thread_p, btid, dbvalue_ptr, &inst_oid, class_oid, &dummy_unique, MULTI_ROW_DELETE,
			       &unique_info);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto error;
	}
    }

  if (unique_info.num_nulls != 0 || unique_info.num_keys != 0 || unique_info.num_oids != 0)
    {
      error_code =
	logtb_tran_update_unique_stats (thread_p, btid, unique_info.num_keys, unique_info.num_oids,
					unique_info.num_nulls, true);
      if (error_code != NO_ERROR)
	{
	  goto error;
	}
    }

  error_code = heap_scancache_end (thread_p, &scan_cache);
  if (error_code != NO_ERROR)
    {
      goto error;
    }
  heap_attrinfo_end (thread_p, &index_attrinfo);

end:
  if (copy_rec.data != NULL)
    {
      free_and_init (copy_rec.data);
    }

  if (dbvalue_ptr == &dbvalue)
    {
      pr_clear_value (dbvalue_ptr);
      dbvalue_ptr = NULL;
    }

  return error_code;

error:

  (void) heap_scancache_end (thread_p, &scan_cache);
  heap_attrinfo_end (thread_p, &index_attrinfo);

  goto end;
}

/*
 * locator_notify_decache  () - Notify of a decache
 *
 * return:
 *
 *   class_oid(in):
 *   oid(in): Oid to decache
 *   notify_area(in): Information used for notification purposes
 *
 * Note: Add an entry in the fetch area with respect to decaching an
 *              object at the workspace.
 */
static bool
locator_notify_decache (const OID * class_oid, const OID * oid, void *notify_area)
{
  LC_COPYAREA_DESC *notify;
  LC_COPYAREA_ONEOBJ *obj;	/* Describe on object in area */
  int i;

  /* safe guard. oid must be not null. */
  if (OID_ISNULL (oid))
    {
      assert (false);
      return true;
    }

  notify = (LC_COPYAREA_DESC *) notify_area;
  if (notify->recdes->area_size <= SSIZEOF (**notify->obj))
    {
      return false;
    }

  /* 
   * Make sure that the object is not already part of the notification area
   */
  obj = LC_START_ONEOBJ_PTR_IN_COPYAREA (notify->mobjs);
  obj = LC_PRIOR_ONEOBJ_PTR_IN_COPYAREA (obj);
  for (i = 0; i < notify->mobjs->num_objs; i++)
    {
      obj = LC_NEXT_ONEOBJ_PTR_IN_COPYAREA (obj);
      if (OID_EQ (&obj->oid, oid))
	{
	  /* The object is already in the notification/fetch area */
	  obj->operation = LC_FETCH_DECACHE_LOCK;
	  return true;
	}
    }
  /* 
   * The object was not part of the notification/fetch area
   */
  notify->mobjs->num_objs++;
  COPY_OID (&((*notify->obj)->class_oid), class_oid);
  COPY_OID (&((*notify->obj)->oid), oid);
  (*notify->obj)->flag = 0;
  (*notify->obj)->hfid = NULL_HFID;
  (*notify->obj)->length = -1;
  (*notify->obj)->operation = LC_FETCH_DECACHE_LOCK;

  (*notify->obj)->offset = -1;
  *notify->obj = LC_NEXT_ONEOBJ_PTR_IN_COPYAREA (*notify->obj);
  notify->recdes->area_size -= sizeof (**notify->obj);

  return true;
}

/*
 * xlocator_notify_isolation_incons () - Synchronize possible inconsistencies related
 *                                  to non two phase locking
 *
 * return:
 *
 *   synch_area(in): Pointer to area where the name of the objects are placed.
 *
 * Note: Notify all inconsistencies related to the transaction
 *              isolation level.
 */
bool
xlocator_notify_isolation_incons (THREAD_ENTRY * thread_p, LC_COPYAREA ** synch_area)
{
  LC_COPYAREA_DESC prefetch_des;	/* Descriptor for decache of objects related to transaction isolation level */
  LC_COPYAREA_MANYOBJS *mobjs;	/* Describe multiple objects in area */
  LC_COPYAREA_ONEOBJ *obj;	/* Describe on object in area */
  RECDES recdes;		/* Record descriptor for insertion */
  int offset;			/* Place to store next object in area */
  bool more_synch = false;

  *synch_area = locator_allocate_copy_area_by_length (DB_PAGESIZE);
  if (*synch_area == NULL)
    {
      return false;
    }

  mobjs = LC_MANYOBJS_PTR_IN_COPYAREA (*synch_area);
  LC_RECDES_IN_COPYAREA (*synch_area, &recdes);
  obj = LC_START_ONEOBJ_PTR_IN_COPYAREA (mobjs);
  mobjs->num_objs = 0;
  offset = 0;

  prefetch_des.mobjs = mobjs;
  prefetch_des.obj = &obj;
  prefetch_des.offset = &offset;
  prefetch_des.recdes = &recdes;
  lock_notify_isolation_incons (thread_p, locator_notify_decache, &prefetch_des);
  if (mobjs->num_objs == 0)
    {
      /* 
       * Don't need to notify of any client workspace lock decaches
       * (i.e., possible object inconsistencies).
       */
      locator_free_copy_area (*synch_area);
      *synch_area = NULL;
    }
  else if (recdes.area_size >= SSIZEOF (*obj))
    {
      more_synch = true;
    }

  return more_synch;
}

static DISK_ISVALID
locator_repair_btree_by_insert (THREAD_ENTRY * thread_p, OID * class_oid, BTID * btid, DB_VALUE * key, OID * inst_oid)
{
  DISK_ISVALID isvalid = DISK_INVALID;
#if defined(SERVER_MODE)
  int tran_index;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
#endif /* SERVER_MODE */

  if (lock_object (thread_p, inst_oid, class_oid, X_LOCK, LK_UNCOND_LOCK) != LK_GRANTED)
    {
      return DISK_INVALID;
    }

  log_sysop_start (thread_p);

  if (btree_insert (thread_p, btid, key, class_oid, inst_oid, SINGLE_ROW_INSERT, NULL, NULL, NULL /* TO DO */ )
      == NO_ERROR)
    {
      isvalid = DISK_VALID;
      log_sysop_commit (thread_p);
    }
  else
    {
      ASSERT_ERROR ();
      log_sysop_abort (thread_p);
    }

#if defined(SERVER_MODE)
  lock_remove_all_inst_locks (thread_p, tran_index, class_oid, X_LOCK);
#endif /* SERVER_MODE */

  return isvalid;
}

static DISK_ISVALID
locator_repair_btree_by_delete (THREAD_ENTRY * thread_p, OID * class_oid, BTID * btid, OID * inst_oid)
{
  DB_VALUE key;
  bool clear_key = false;
  int dummy_unique;
  DISK_ISVALID isvalid = DISK_INVALID;
#if defined(SERVER_MODE)
  int tran_index;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
#endif /* SERVER_MODE */

  if (btree_find_key (thread_p, btid, inst_oid, &key, &clear_key) != DISK_VALID)
    {
      return DISK_INVALID;
    }

  if (lock_object (thread_p, inst_oid, class_oid, X_LOCK, LK_UNCOND_LOCK) == LK_GRANTED)
    {
      log_sysop_start (thread_p);

      if (btree_physical_delete (thread_p, btid, &key, inst_oid, class_oid, &dummy_unique, SINGLE_ROW_DELETE, NULL) ==
	  NO_ERROR)
	{
	  isvalid = DISK_VALID;
	  log_sysop_commit (thread_p);
	}
      else
	{
	  ASSERT_ERROR ();
	  log_sysop_abort (thread_p);
	}

#if defined(SERVER_MODE)
      lock_remove_all_inst_locks (thread_p, tran_index, class_oid, X_LOCK);
#endif /* SERVER_MODE */
    }

  if (clear_key)
    {
      pr_clear_value (&key);
    }

  return isvalid;
}

/*
 * locator_check_btree_entries () - Check consistency of btree entries and heap
 *
 * return: valid
 *
 *   btid(in): Btree identifier
 *   hfid(in): Heap identfier of the instances of class that are indexed
 *   class_oid(in): The class identifier
 *   n_attr_ids(in):  Number of attribute ids (size of the array).
 *   attr_ids(in): Attribute ID array.
 *   btname(in) :
 *   repair(in):
 *
 * Note: Check the consistency of the btree entries against the
 *              instances stored on heap and vive versa.
 */
DISK_ISVALID
locator_check_btree_entries (THREAD_ENTRY * thread_p, BTID * btid, HFID * hfid, OID * class_oid, int n_attr_ids,
			     ATTR_ID * attr_ids, int *atts_prefix_length, const char *btname, bool repair)
{
  DISK_ISVALID isvalid = DISK_VALID;
  DISK_ISVALID isallvalid = DISK_VALID;
  OID inst_oid, *p_inst_oid = &inst_oid;
  RECDES record = RECDES_INITIALIZER, *recordp = &record;	/* Record descriptor for copying object */
  SCAN_CODE scan;
  HEAP_SCANCACHE scan_cache;
  BTREE_CHECKSCAN bt_checkscan;
  BTREE_SCAN bt_scan;
  HEAP_CACHE_ATTRINFO attr_info;
  int num_btree_oids = 0;
  int num_heap_oids = 0;
  int oid_cnt;
  OID *oid_area = NULL;
  INDX_SCAN_ID isid;
  BTREE_ISCAN_OID_LIST oid_list;
  int i;
  DB_VALUE dbvalue;
  DB_VALUE *key = NULL;
  char buf[DBVAL_BUFSIZE + MAX_ALIGNMENT], *aligned_buf;
  char *classname = NULL;
  KEY_VAL_RANGE key_val_range;
  int index_id;
  OR_INDEX *index = NULL;
  DB_LOGICAL ev_res;
  OR_CLASSREP *classrepr = NULL;
  MVCC_SNAPSHOT *mvcc_snapshot = NULL;
  BTID btid_info;
#if defined(SERVER_MODE)
  int tran_index;
#endif /* SERVER_MODE */
  bool is_scancache_started = false;
  bool is_attrinfo_started = false;
  bool is_bt_checkscan_started = false;

  mvcc_snapshot = logtb_get_mvcc_snapshot (thread_p);
  if (mvcc_snapshot == NULL)
    {
      return DISK_ERROR;
    }
  DB_MAKE_NULL (&dbvalue);

  aligned_buf = PTR_ALIGN (buf, MAX_ALIGNMENT);

#if defined(SERVER_MODE)
  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
#endif /* SERVER_MODE */

  scan_init_index_scan (&isid, NULL, mvcc_snapshot);

  /* Start a scan cursor and a class attribute information */
  if (heap_scancache_start (thread_p, &scan_cache, hfid, class_oid, false, false, mvcc_snapshot) != NO_ERROR)
    {
      return DISK_ERROR;
    }
  is_scancache_started = true;

  if (heap_attrinfo_start (thread_p, class_oid, n_attr_ids, attr_ids, &attr_info) != NO_ERROR)
    {
      goto error;
    }
  is_attrinfo_started = true;

  classrepr = attr_info.last_classrepr;
  if (classrepr == NULL)
    {
      goto error;
    }
  index_id = -1;
  for (i = 0; i < classrepr->n_indexes; i++)
    {
      if (BTID_IS_EQUAL (&(classrepr->indexes[i].btid), btid))
	{
	  index_id = i;
	  break;
	}
    }
  assert (index_id != -1);

  index = &(attr_info.last_classrepr->indexes[index_id]);
  assert (index != NULL);

  /* 
   * Step 1) From Heap to B+tree
   */

  /* start a check-scan on index */
  if (btree_keyoid_checkscan_start (thread_p, btid, &bt_checkscan) != NO_ERROR)
    {
      goto error;
    }
  is_bt_checkscan_started = true;

  inst_oid.volid = hfid->vfid.volid;
  inst_oid.pageid = NULL_PAGEID;
  inst_oid.slotid = NULL_SLOTID;

  while ((scan = heap_next (thread_p, hfid, class_oid, &inst_oid, &record, &scan_cache, COPY)) == S_SUCCESS)
    {
      num_heap_oids++;

      if (index->filter_predicate)
	{
	  if (locator_eval_filter_predicate (thread_p, &index->btid, index->filter_predicate, class_oid, &p_inst_oid, 1,
					     &recordp, &ev_res) != NO_ERROR)
	    {
	      isallvalid = DISK_ERROR;
	    }
	  else if (ev_res != V_TRUE)
	    {
	      /* 
	       * To exclude the heap OID of which record cannot satisfy the
	       * conditions of the index filter predicate.
	       */
	      num_heap_oids--;
	      continue;
	    }
	}

      /* Make sure that the index entry exist */
      if ((n_attr_ids == 1 && heap_attrinfo_read_dbvalues (thread_p, &inst_oid, &record, NULL, &attr_info) != NO_ERROR)
	  || (key = heap_attrvalue_get_key (thread_p, index_id, &attr_info, &record, &btid_info, &dbvalue, aligned_buf,
					    NULL, NULL)) == NULL)
	{
	  if (isallvalid != DISK_INVALID)
	    {
	      isallvalid = DISK_ERROR;
	    }
	}
      else
	{
	  assert (key != NULL);

	  if (db_value_is_null (key) || btree_multicol_key_is_null (key))
	    {
	      /* Do not check the btree since unbound values are not recorded */
	      num_heap_oids--;
	    }
	  else
	    {
	      isvalid = btree_keyoid_checkscan_check (thread_p, &bt_checkscan, class_oid, key, &inst_oid);

	      if (er_errid () == ER_INTERRUPTED)
		{
		  /* in case of user interrupt */
		  goto error;
		}

	      if (isvalid == DISK_INVALID)
		{
		  if (repair)
		    {
		      isvalid = locator_repair_btree_by_insert (thread_p, class_oid, btid, key, &inst_oid);
		    }

		  if (isvalid == DISK_INVALID)
		    {
		      char *key_dmp;

		      key_dmp = pr_valstring (key);

		      if (!OID_ISNULL (class_oid))
			{
			  classname = heap_get_class_name (thread_p, class_oid);
			}

		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LC_INCONSISTENT_BTREE_ENTRY_TYPE1, 12,
			      (btname) ? btname : "*UNKNOWN-INDEX*", (classname) ? classname : "*UNKNOWN-CLASS*",
			      class_oid->volid, class_oid->pageid, class_oid->slotid, (key_dmp) ? key_dmp : "_NULL_KEY",
			      inst_oid.volid, inst_oid.pageid, inst_oid.slotid, btid->vfid.volid, btid->vfid.fileid,
			      btid->root_pageid);

		      if (key_dmp)
			{
			  free_and_init (key_dmp);
			}

		      if (classname)
			{
			  free_and_init (classname);
			}

		      if (isallvalid != DISK_INVALID)
			{
			  isallvalid = isvalid;
			}
		    }
		}
	    }
	}

      if (key == &dbvalue)
	{
	  pr_clear_value (key);
	  key = NULL;
	}
    }

  if (scan != S_END && isallvalid != DISK_INVALID)
    {
      isallvalid = DISK_ERROR;
    }

  /* close the index check-scan */
  btree_keyoid_checkscan_end (thread_p, &bt_checkscan);
  is_bt_checkscan_started = false;

  /* Finish scan cursor and class attribute cache information */
  heap_attrinfo_end (thread_p, &attr_info);
  is_attrinfo_started = false;

  /* 
   * Step 2) From B+tree to Heap
   */

  BTREE_INIT_SCAN (&bt_scan);

  isid.oid_list = &oid_list;
  isid.oid_list->oid_cnt = 0;
  isid.oid_list->oidp = (OID *) malloc (ISCAN_OID_BUFFER_CAPACITY);
  if (isid.oid_list->oidp == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) ISCAN_OID_BUFFER_SIZE);

      isallvalid = DISK_ERROR;
      goto error;
    }
  isid.oid_list->capacity = ISCAN_OID_BUFFER_CAPACITY / OR_OID_SIZE;
  isid.oid_list->max_oid_cnt = isid.oid_list->capacity;
  isid.oid_list->next_list = NULL;
  isid.indx_info = NULL;

  /* alloc index key copy_buf */
  isid.copy_buf = (char *) db_private_alloc (thread_p, DBVAL_BUFSIZE);
  if (isid.copy_buf == NULL)
    {
      isallvalid = DISK_ERROR;
      goto error;
    }
  isid.copy_buf_len = DBVAL_BUFSIZE;
  memset ((void *) (&(isid.indx_cov)), 0, sizeof (INDX_COV));
  memset ((void *) (&(isid.multi_range_opt)), 0, sizeof (MULTI_RANGE_OPT));

  scan_init_iss (&isid);

  if (heap_scancache_start (thread_p, &isid.scan_cache, hfid, class_oid, true, true, mvcc_snapshot) != NO_ERROR)
    {
      isallvalid = DISK_ERROR;
      goto error;
    }

  isid.check_not_vacuumed = true;
  db_make_null (&key_val_range.key1);
  db_make_null (&key_val_range.key2);
  key_val_range.range = INF_INF;
  key_val_range.num_index_term = 0;
  do
    {
      /* search index */
      if (btree_prepare_bts (thread_p, &bt_scan, btid, &isid, &key_val_range, NULL, class_oid, NULL, NULL, false, NULL)
	  != NO_ERROR)
	{
	  assert (er_errid () != NO_ERROR);
	  break;
	}
      if (btree_range_scan (thread_p, &bt_scan, btree_range_scan_select_visible_oids) != NO_ERROR)
	{
	  assert (er_errid () != NO_ERROR);
	  break;
	}
      oid_cnt = bt_scan.n_oids_read_last_iteration;
      if (oid_cnt < 0)
	{
	  assert (false);
	  break;
	}
      oid_area = isid.oid_list->oidp;
      num_btree_oids += oid_cnt;
      for (i = 0; i < oid_cnt; i++)
	{
	  if (!heap_does_exist (thread_p, class_oid, &oid_area[i]))
	    {
	      isvalid = DISK_INVALID;

	      if (repair)
		{
		  /* don't care about filter predicate here since we are sure that oid_area[i] is contained in tree,
		   * the keys has been already S_LOCK-ed. */
		  isvalid = locator_repair_btree_by_delete (thread_p, class_oid, btid, &oid_area[i]);
		}

	      if (isvalid == DISK_VALID)
		{
		  num_btree_oids--;
		}
	      else
		{
		  if (!OID_ISNULL (class_oid))
		    {
		      classname = heap_get_class_name (thread_p, class_oid);
		    }

		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LC_INCONSISTENT_BTREE_ENTRY_TYPE2, 11,
			  (btname) ? btname : "*UNKNOWN-INDEX*", (classname) ? classname : "*UNKNOWN-CLASS*",
			  class_oid->volid, class_oid->pageid, class_oid->slotid, oid_area[i].volid, oid_area[i].pageid,
			  oid_area[i].slotid, btid->vfid.volid, btid->vfid.fileid, btid->root_pageid);

		  if (classname)
		    {
		      free_and_init (classname);
		    }

		  isallvalid = DISK_INVALID;
		}
	    }
	}

    }
  while (!BTREE_END_OF_SCAN (&bt_scan));

  if (heap_scancache_end (thread_p, &isid.scan_cache) != NO_ERROR)
    {
      isallvalid = DISK_INVALID;
    }

  if (num_heap_oids != num_btree_oids)
    {
      if (!OID_ISNULL (class_oid))
	{
	  classname = heap_get_class_name (thread_p, class_oid);
	}

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LC_INCONSISTENT_BTREE_ENTRY_TYPE3, 10,
	      (btname) ? btname : "*UNKNOWN-INDEX*", (classname) ? classname : "*UNKNOWN-CLASS*", class_oid->volid,
	      class_oid->pageid, class_oid->slotid, num_heap_oids, num_btree_oids, btid->vfid.volid, btid->vfid.fileid,
	      btid->root_pageid);

      if (classname)
	{
	  free_and_init (classname);
	}

      isallvalid = DISK_INVALID;
    }

  if (isid.check_not_vacuumed && isid.not_vacuumed_res != DISK_VALID)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_INDEX_FOUND_NOT_VACUUMED, 5,
	      (btname) ? btname : "*UNKNOWN-INDEX*", (classname) ? classname : "*UNKNOWN-CLASS*", class_oid->volid,
	      class_oid->pageid, class_oid->slotid);
      isallvalid = isid.not_vacuumed_res;
    }

end:

  if (key == &dbvalue)
    {
      pr_clear_value (key);
    }

  if (isid.oid_list != NULL && isid.oid_list->oidp != NULL)
    {
      free_and_init (isid.oid_list->oidp);
    }

  /* free index key copy_buf */
  if (isid.copy_buf)
    {
      db_private_free_and_init (thread_p, isid.copy_buf);
    }

  if (is_scancache_started && heap_scancache_end (thread_p, &scan_cache) != NO_ERROR)
    {
      isallvalid = DISK_INVALID;
    }

  if (is_bt_checkscan_started)
    {
      btree_keyoid_checkscan_end (thread_p, &bt_checkscan);
    }

  if (is_attrinfo_started)
    {
      heap_attrinfo_end (thread_p, &attr_info);
    }

  return isallvalid;

error:
  isallvalid = DISK_ERROR;
  goto end;
}

/*
 * locator_check_unique_btree_entries () - Check consistency of unique btree entries
 *                                    and heaps
 *
 * return: valid
 *
 *   btid(in): Btree identifier
 *   class_oid(in):
 *   classrec(in):
 *   attr_ids(in): Array of indexed attributes for the btid
 *   repair(in):
 *
 * Note: Check the consistency of the unique btree entries against the
 *              instances stored on heap and vice versa.  Unique btrees are
 *              special because they span hierarchies and can have multiple
 *              heaps associated with them.
 */
static DISK_ISVALID
locator_check_unique_btree_entries (THREAD_ENTRY * thread_p, BTID * btid, OID * cls_oid, RECDES * classrec,
				    ATTR_ID * attr_ids, const char *btname, bool repair)
{
  DISK_ISVALID isvalid = DISK_VALID, isallvalid = DISK_VALID;
  OID inst_oid, *p_inst_oid = &inst_oid;
  RECDES peek, *p_peek = &peek;
  SCAN_CODE scan;
  HEAP_SCANCACHE *scan_cache = NULL;
  BTREE_CHECKSCAN bt_checkscan;
  BTREE_SCAN bt_scan;
  HEAP_CACHE_ATTRINFO attr_info;
  DB_VALUE *key = NULL;
  DB_VALUE dbvalue;
  int num_btree_oids = 0, num_heap_oids = 0, num_nulls = 0;
  int oid_cnt;
  OID *oid_area = NULL;
  int num_classes, scancache_inited = 0, attrinfo_inited = 0;
  int i, j, index_id;
  HFID *hfids = NULL, *hfid = NULL;
  OID *class_oids = NULL, *class_oid = NULL;
  INDX_SCAN_ID isid;
  BTREE_ISCAN_OID_LIST oid_list;
  char buf[DBVAL_BUFSIZE + MAX_ALIGNMENT], *aligned_buf;
  char *classname = NULL;
  KEY_VAL_RANGE key_val_range;
  OR_INDEX *index;
  DB_LOGICAL ev_res;
  int partition_local_index = 0;
  MVCC_SNAPSHOT *mvcc_snapshot = NULL;
#if defined(SERVER_MODE)
  int tran_index;
#else
  int btree_oid_cnt, btree_null_cnt, btree_key_cnt;
#endif /* SERVER_MODE */
  bool bt_checkscan_inited = false;

  mvcc_snapshot = logtb_get_mvcc_snapshot (thread_p);
  if (mvcc_snapshot == NULL)
    {
      return DISK_ERROR;
    }
  DB_MAKE_NULL (&dbvalue);

  aligned_buf = PTR_ALIGN (buf, MAX_ALIGNMENT);

#if defined(SERVER_MODE)
  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
#endif /* SERVER_MODE */

  scan_init_index_scan (&isid, NULL, mvcc_snapshot);

  /* get all the heap files associated with this unique btree */
  if (or_get_unique_hierarchy (thread_p, classrec, attr_ids[0], btid, &class_oids, &hfids, &num_classes,
			       &partition_local_index) != NO_ERROR
      || class_oids == NULL || hfids == NULL || num_classes < 1)
    {
      if (class_oids != NULL)
	{
	  free_and_init (class_oids);
	}

      if (hfids != NULL)
	{
	  free_and_init (hfids);
	}

      goto error;
    }

  /* 
   * Step 1) Check if all instances of all the heaps are in the unique btree.
   */

  scan_cache = (HEAP_SCANCACHE *) malloc (num_classes * sizeof (HEAP_SCANCACHE));
  if (scan_cache == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, num_classes * sizeof (HEAP_SCANCACHE));
      goto error;
    }

  if (partition_local_index == 1)
    {
      if (num_classes == 1)
	{
	  /* partition class with local index */
	  COPY_OID (&class_oids[0], cls_oid);
	  or_class_hfid (classrec, &hfids[0]);
	}
      else
	{
	  /* a partitioned class and a local index */
	  goto end;
	}
    }

  for (j = 0; j < num_classes; j++)
    {
      hfid = &hfids[j];
      class_oid = &class_oids[j];

      /* Start a scan cursor and a class attribute information */
      if (heap_scancache_start (thread_p, &scan_cache[j], hfid, class_oid, true, false, mvcc_snapshot) != NO_ERROR)
	{
	  goto error;
	}
      scancache_inited++;

      index_id = heap_attrinfo_start_with_btid (thread_p, class_oid, btid, &attr_info);
      if (index_id < 0)
	{
	  goto error;
	}
      index = &(attr_info.last_classrepr->indexes[index_id]);
      assert (index != NULL);

      attrinfo_inited = 1;

      /* start a check-scan on index */
      if (btree_keyoid_checkscan_start (thread_p, btid, &bt_checkscan) != NO_ERROR)
	{
	  goto error;
	}
      bt_checkscan_inited = true;

      inst_oid.volid = hfid->vfid.volid;
      inst_oid.pageid = NULL_PAGEID;
      inst_oid.slotid = NULL_SLOTID;

      while ((scan = heap_next (thread_p, hfid, class_oid, &inst_oid, &peek, &scan_cache[j], PEEK)) == S_SUCCESS)
	{
	  num_heap_oids++;

	  if (index->filter_predicate)
	    {
	      if (locator_eval_filter_predicate
		  (thread_p, btid, index->filter_predicate, class_oid, &p_inst_oid, 1, &p_peek, &ev_res) != NO_ERROR)
		{
		  goto error;
		}
	      else if (ev_res != V_TRUE)
		{
		  continue;
		}
	    }

	  /* Make sure that the index entry exists */
	  if ((heap_attrinfo_read_dbvalues (thread_p, &inst_oid, &peek, NULL, &attr_info) != NO_ERROR)
	      ||
	      ((key =
		heap_attrvalue_get_key (thread_p, index_id, &attr_info, &peek, btid, &dbvalue, aligned_buf, NULL,
					NULL)) == NULL))
	    {
	      if (isallvalid != DISK_INVALID)
		{
		  isallvalid = DISK_ERROR;
		}
	    }
	  else
	    {
	      assert (key != NULL);

	      if (db_value_is_null (key) || btree_multicol_key_is_null (key))
		{
		  num_nulls++;
		}
	      else
		{
		  isvalid = btree_keyoid_checkscan_check (thread_p, &bt_checkscan, class_oid, key, &inst_oid);

		  if (er_errid () == ER_INTERRUPTED)
		    {
		      /* in case of user interrupt */
		      goto error;
		    }

		  if (isvalid == DISK_INVALID)
		    {
		      if (repair)
			{
			  isvalid = locator_repair_btree_by_insert (thread_p, class_oid, btid, key, &inst_oid);
			}

		      if (isvalid == DISK_INVALID)
			{
			  char *key_dmp;

			  key_dmp = pr_valstring (key);
			  if (!OID_ISNULL (class_oid))
			    {
			      classname = heap_get_class_name (thread_p, class_oid);
			    }

			  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LC_INCONSISTENT_BTREE_ENTRY_TYPE1, 12,
				  (btname) ? btname : "*UNKNOWN-INDEX*", (classname) ? classname : "*UNKNOWN-CLASS*",
				  class_oid->volid, class_oid->pageid, class_oid->slotid,
				  (key_dmp) ? key_dmp : "_NULL_KEY", inst_oid.volid, inst_oid.pageid, inst_oid.slotid,
				  btid->vfid.volid, btid->vfid.fileid, btid->root_pageid);

			  if (key_dmp)
			    {
			      free_and_init (key_dmp);
			    }

			  if (classname)
			    {
			      free_and_init (classname);
			    }

			  if (isallvalid != DISK_INVALID)
			    {
			      isallvalid = isvalid;
			    }
			}
		    }
		}
	    }

	  if (key == &dbvalue)
	    {
	      pr_clear_value (key);
	    }
	}

      if (scan != S_END && isallvalid != DISK_INVALID)
	{
	  isallvalid = DISK_ERROR;
	}

      /* close the index check-scan */
      btree_keyoid_checkscan_end (thread_p, &bt_checkscan);
      bt_checkscan_inited = false;

      /* Finish scan cursor and class attribute cache information */
      heap_attrinfo_end (thread_p, &attr_info);
      attrinfo_inited = 0;
    }

  /* 
   * Step 2) Check that all the btree entries are members of one of the heaps.
   */

  BTREE_INIT_SCAN (&bt_scan);

  isid.oid_list = &oid_list;
  isid.oid_list->oid_cnt = 0;
  isid.oid_list->oidp = (OID *) malloc (ISCAN_OID_BUFFER_CAPACITY);
  if (isid.oid_list->oidp == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) ISCAN_OID_BUFFER_SIZE);
      goto error;
    }
  isid.oid_list->capacity = ISCAN_OID_BUFFER_CAPACITY / OR_OID_SIZE;
  isid.oid_list->max_oid_cnt = isid.oid_list->capacity;
  isid.oid_list->next_list = NULL;
  /* alloc index key copy_buf */
  isid.copy_buf = (char *) db_private_alloc (thread_p, DBVAL_BUFSIZE);
  if (isid.copy_buf == NULL)
    {
      goto error;
    }
  isid.copy_buf_len = DBVAL_BUFSIZE;

  if (heap_scancache_start (thread_p, &isid.scan_cache, hfid, class_oid, true, true, mvcc_snapshot) != NO_ERROR)
    {
      goto error;
    }

  isid.check_not_vacuumed = true;
  db_make_null (&key_val_range.key1);
  db_make_null (&key_val_range.key2);
  key_val_range.range = INF_INF;
  key_val_range.num_index_term = 0;

  do
    {
      /* search index */
      if (btree_prepare_bts (thread_p, &bt_scan, btid, &isid, &key_val_range, NULL, NULL, NULL, NULL, false, NULL) !=
	  NO_ERROR)
	{
	  assert (er_errid () != NO_ERROR);
	  break;
	}
      if (btree_range_scan (thread_p, &bt_scan, btree_range_scan_select_visible_oids) != NO_ERROR)
	{
	  assert (er_errid () != NO_ERROR);
	  break;
	}
      oid_cnt = bt_scan.n_oids_read_last_iteration;

      /* TODO: unique with prefix length */
      /* ^ What is this TODO? ^ */

      if (oid_cnt == -1)
	{
	  assert (false);
	  break;
	}

      oid_area = isid.oid_list->oidp;

      num_btree_oids += oid_cnt;
      for (i = 0; i < oid_cnt; i++)
	{
	  if (!heap_does_exist (thread_p, NULL, &oid_area[i]))
	    {
	      isvalid = DISK_INVALID;
	      if (repair)
		{
		  /* don't care about filter predicate here since we are sure that oid_area[i] is contained in tree the 
		   * keys has been already S_LOCK-ed. */
		  isvalid = locator_repair_btree_by_delete (thread_p, class_oid, btid, &oid_area[i]);
		}

	      if (isvalid == DISK_VALID)
		{
		  num_btree_oids--;
		}
	      else
		{
		  if (!OID_ISNULL (class_oid))
		    {
		      classname = heap_get_class_name (thread_p, class_oid);
		    }

		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LC_INCONSISTENT_BTREE_ENTRY_TYPE2, 11,
			  (btname) ? btname : "*UNKNOWN-INDEX*", (classname) ? classname : "*UNKNOWN-CLASS*",
			  class_oid->volid, class_oid->pageid, class_oid->slotid, oid_area[i].volid, oid_area[i].pageid,
			  oid_area[i].slotid, btid->vfid.volid, btid->vfid.fileid, btid->root_pageid);

		  if (classname)
		    {
		      free_and_init (classname);
		    }

		  isallvalid = DISK_INVALID;
		}
	    }
	  else
	    {
	      OID cl_oid;
	      int found = 0;

	      /* 
	       * check to make sure that the OID is one of the OIDs from our
	       * list of classes.
	       */
	      if (heap_get_class_oid (thread_p, &oid_area[i], &cl_oid) != S_SUCCESS)
		{
		  (void) heap_scancache_end (thread_p, &isid.scan_cache);
		  goto error;
		}

	      for (j = 0, found = 0; found == 0 && class_oids != NULL && j < num_classes; j++)
		{
		  if (OID_EQ (&cl_oid, &(class_oids[j])))
		    {
		      found = 1;
		    }
		}

	      if (!found)
		{
		  if (!OID_ISNULL (class_oid))
		    {
		      classname = heap_get_class_name (thread_p, class_oid);
		    }

		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LC_INCONSISTENT_BTREE_ENTRY_TYPE8, 11,
			  (btname) ? btname : "*UNKNOWN-INDEX*", (classname) ? classname : "*UNKNOWN-CLASS*",
			  class_oid->volid, class_oid->pageid, class_oid->slotid, oid_area[i].volid, oid_area[i].pageid,
			  oid_area[i].slotid, btid->vfid.volid, btid->vfid.fileid, btid->root_pageid);

		  if (classname)
		    {
		      free_and_init (classname);
		    }
		  isallvalid = DISK_INVALID;
		}
	    }
	}
    }
  while (!BTREE_END_OF_SCAN (&bt_scan));

  free_and_init (isid.oid_list->oidp);
  /* free index key copy_buf */
  if (isid.copy_buf)
    {
      db_private_free_and_init (thread_p, isid.copy_buf);
    }

  if (heap_scancache_end (thread_p, &isid.scan_cache) != NO_ERROR)
    {
      goto error;
    }

  /* check to see that the btree root statistics are correct. */
#if defined(SA_MODE)
  if (logtb_get_global_unique_stats (thread_p, btid, &btree_oid_cnt, &btree_null_cnt, &btree_key_cnt) != NO_ERROR)
    {
      goto error;
    }
#endif

  /* Do the numbers add up? */
  if (num_heap_oids != num_btree_oids + num_nulls)
    {
      if (!OID_ISNULL (class_oid))
	{
	  classname = heap_get_class_name (thread_p, class_oid);
	}

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LC_INCONSISTENT_BTREE_ENTRY_TYPE4, 11,
	      (btname) ? btname : "*UNKNOWN-INDEX*", (classname) ? classname : "*UNKNOWN-CLASS*", class_oid->volid,
	      class_oid->pageid, class_oid->slotid, num_heap_oids, num_btree_oids, num_nulls, btid->vfid.volid,
	      btid->vfid.fileid, btid->root_pageid);

      if (classname)
	{
	  free_and_init (classname);
	}

      isallvalid = DISK_INVALID;
    }

#if defined(SA_MODE)
  if (num_heap_oids != btree_oid_cnt)
    {
      if (!OID_ISNULL (class_oid))
	{
	  classname = heap_get_class_name (thread_p, class_oid);
	}

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LC_INCONSISTENT_BTREE_ENTRY_TYPE5, 10,
	      (btname) ? btname : "*UNKNOWN-INDEX*", (classname) ? classname : "*UNKNOWN-CLASS*", class_oid->volid,
	      class_oid->pageid, class_oid->slotid, num_heap_oids, btree_oid_cnt, btid->vfid.volid, btid->vfid.fileid,
	      btid->root_pageid);

      if (classname)
	{
	  free_and_init (classname);
	}

      isallvalid = DISK_INVALID;
    }

  if (num_nulls != btree_null_cnt)
    {
      if (!OID_ISNULL (class_oid))
	{
	  classname = heap_get_class_name (thread_p, class_oid);
	}

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LC_INCONSISTENT_BTREE_ENTRY_TYPE7, 10,
	      (btname) ? btname : "*UNKNOWN-INDEX*", (classname) ? classname : "*UNKNOWN-CLASS*", class_oid->volid,
	      class_oid->pageid, class_oid->slotid, num_nulls, btree_null_cnt, btid->vfid.volid, btid->vfid.fileid,
	      btid->root_pageid);

      if (classname)
	{
	  free_and_init (classname);
	}
      isallvalid = DISK_INVALID;
    }

  /* finally check if the btree thinks that it is unique */
  if (btree_oid_cnt != btree_null_cnt + btree_key_cnt)
    {
      if (!OID_ISNULL (class_oid))
	{
	  classname = heap_get_class_name (thread_p, class_oid);
	}

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LC_INCONSISTENT_BTREE_ENTRY_TYPE6, 11,
	      (btname) ? btname : "*UNKNOWN-INDEX*", (classname) ? classname : "*UNKNOWN-CLASS*", class_oid->volid,
	      class_oid->pageid, class_oid->slotid, btree_oid_cnt, btree_null_cnt, btree_key_cnt, btid->vfid.volid,
	      btid->vfid.fileid, btid->root_pageid);

      if (classname)
	{
	  free_and_init (classname);
	}

      isallvalid = DISK_INVALID;
    }
#endif

  if (isid.check_not_vacuumed && isid.not_vacuumed_res != DISK_VALID)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_INDEX_FOUND_NOT_VACUUMED, 5,
	      (btname) ? btname : "*UNKNOWN-INDEX*", (classname) ? classname : "*UNKNOWN-CLASS*", class_oid->volid,
	      class_oid->pageid, class_oid->slotid);
      isallvalid = isid.not_vacuumed_res;
    }

end:

  for (j = 0; j < scancache_inited; j++)
    {
      if (heap_scancache_end (thread_p, &scan_cache[j]) != NO_ERROR)
	{
	  goto error;
	}
    }

  if (scan_cache)
    {
      free_and_init (scan_cache);
    }

  free_and_init (class_oids);
  free_and_init (hfids);

  return isallvalid;

error:

  if (isid.oid_list != NULL && isid.oid_list->oidp != NULL)
    {
      free_and_init (isid.oid_list->oidp);
    }

  /* free index key copy_buf */
  if (isid.copy_buf)
    {
      db_private_free_and_init (thread_p, isid.copy_buf);
    }

  if (class_oids)
    {
      free_and_init (class_oids);
    }

  if (hfids)
    {
      free_and_init (hfids);
    }

  if (attrinfo_inited)
    {
      heap_attrinfo_end (thread_p, &attr_info);
    }

  for (j = 0; j < scancache_inited; j++)
    {
      (void) heap_scancache_end (thread_p, &scan_cache[j]);
    }

  if (scan_cache)
    {
      free_and_init (scan_cache);
    }

  if (bt_checkscan_inited)
    {
      btree_keyoid_checkscan_end (thread_p, &bt_checkscan);
    }

  return DISK_ERROR;
}

/*
 * locator_check_class () - Check consistency of a class
 *
 * return: valid
 *
 *   repair(in):
 */
DISK_ISVALID
locator_check_class (THREAD_ENTRY * thread_p, OID * class_oid, RECDES * peek, HFID * class_hfid, BTID * index_btid,
		     bool repair)
{
  DISK_ISVALID isvalid = DISK_VALID, rv = DISK_VALID;
  HEAP_CACHE_ATTRINFO attr_info;
  int i;
  HEAP_IDX_ELEMENTS_INFO idx_info;
  BTID *btid;
  ATTR_ID *attrids = NULL;
  int n_attrs;
  char *btname = NULL;
  int *attrs_prefix_length = NULL;

  if (heap_attrinfo_start_with_index (thread_p, class_oid, peek, &attr_info, &idx_info) < 0)
    {
      return DISK_ERROR;
    }

  if (idx_info.num_btids <= 0)
    {
      heap_attrinfo_end (thread_p, &attr_info);
      return DISK_VALID;
    }

  for (i = 0; i < idx_info.num_btids && rv != DISK_ERROR; i++)
    {
      btid = heap_indexinfo_get_btid (i, &attr_info);
      if (btid == NULL)
	{
	  isvalid = DISK_ERROR;
	  break;
	}

      if (index_btid != NULL && !BTID_IS_EQUAL (btid, index_btid))
	{
	  continue;
	}

      n_attrs = heap_indexinfo_get_num_attrs (i, &attr_info);
      if (n_attrs <= 0)
	{
	  isvalid = DISK_ERROR;
	  break;
	}

      attrids = (ATTR_ID *) malloc (n_attrs * sizeof (ATTR_ID));
      if (attrids == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, n_attrs * sizeof (ATTR_ID));
	  isvalid = DISK_ERROR;
	  break;
	}

      if (heap_indexinfo_get_attrids (i, &attr_info, attrids) != NO_ERROR)
	{
	  free_and_init (attrids);
	  isvalid = DISK_ERROR;
	  break;
	}

      attrs_prefix_length = (int *) malloc (n_attrs * sizeof (int));
      if (attrs_prefix_length == NULL)
	{
	  free_and_init (attrids);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, n_attrs * sizeof (int));
	  isvalid = DISK_ERROR;
	  break;
	}

      if (heap_indexinfo_get_attrs_prefix_length (i, &attr_info, attrs_prefix_length, n_attrs) != NO_ERROR)
	{
	  free_and_init (attrids);
	  free_and_init (attrs_prefix_length);
	  isvalid = DISK_ERROR;
	  break;
	}

      if (heap_get_indexinfo_of_btid (thread_p, class_oid, btid, NULL, NULL, NULL, NULL, &btname, NULL) != NO_ERROR)
	{
	  free_and_init (attrids);
	  free_and_init (attrs_prefix_length);
	  isvalid = DISK_ERROR;
	  break;
	}

      if (xbtree_get_unique_pk (thread_p, btid))
	{
	  rv = locator_check_unique_btree_entries (thread_p, btid, class_oid, peek, attrids, btname, repair);
	}
      else
	{
	  rv =
	    locator_check_btree_entries (thread_p, btid, class_hfid, class_oid, n_attrs, attrids, attrs_prefix_length,
					 btname, repair);
	}
      if (rv != DISK_VALID)
	{
	  isvalid = DISK_ERROR;
	}

      free_and_init (attrids);
      if (attrs_prefix_length)
	{
	  free_and_init (attrs_prefix_length);
	}
      if (btname)
	{
	  free_and_init (btname);
	}
    }

  heap_attrinfo_end (thread_p, &attr_info);
  return isvalid;
}

/*
 * locator_check_by_class_oid () - Check consistency of a class
 *
 * return: valid
 *
 *   repair(in):
 *
 */
DISK_ISVALID
locator_check_by_class_oid (THREAD_ENTRY * thread_p, OID * cls_oid, HFID * hfid, BTID * index_btid, bool repair)
{
  RECDES copy_rec = RECDES_INITIALIZER;
  HEAP_SCANCACHE scan;
  HFID root_hfid;
  DISK_ISVALID rv = DISK_ERROR;
  MVCC_SNAPSHOT *mvcc_snapshot = NULL;

  mvcc_snapshot = logtb_get_mvcc_snapshot (thread_p);
  if (mvcc_snapshot == NULL)
    {
      return DISK_ERROR;
    }

  if (boot_find_root_heap (&root_hfid) != NO_ERROR || HFID_IS_NULL (&root_hfid))
    {
      return DISK_ERROR;
    }

  if (heap_scancache_start (thread_p, &scan, &root_hfid, oid_Root_class_oid, true, false, mvcc_snapshot) != NO_ERROR)
    {
      return DISK_ERROR;
    }

  if (heap_get_class_record (thread_p, cls_oid, &copy_rec, &scan, COPY) != S_SUCCESS)
    {
      heap_scancache_end (thread_p, &scan);
      return DISK_ERROR;
    }

  /* lock class and unlatch page */
  if (lock_object (thread_p, cls_oid, oid_Root_class_oid, IS_LOCK, LK_COND_LOCK) != LK_GRANTED)
    {
      if (scan.page_watcher.pgptr != NULL)
	{
	  pgbuf_ordered_unfix (thread_p, &scan.page_watcher);
	}

      if (lock_object (thread_p, cls_oid, oid_Root_class_oid, IS_LOCK, LK_UNCOND_LOCK) != LK_GRANTED)
	{
	  heap_scancache_end (thread_p, &scan);
	  return DISK_ERROR;
	}

      copy_rec.data = NULL;
      if (heap_get_class_record (thread_p, cls_oid, &copy_rec, &scan, COPY) != S_SUCCESS)
	{
	  lock_unlock_object (thread_p, cls_oid, oid_Root_class_oid, IS_LOCK, true);
	  heap_scancache_end (thread_p, &scan);
	  return DISK_ERROR;
	}
    }

  /* we have lock on class_oid, record COPYed, while page is latched */
  if (scan.page_watcher.pgptr != NULL)
    {
      pgbuf_ordered_unfix (thread_p, &scan.page_watcher);
    }

  rv = locator_check_class (thread_p, cls_oid, &copy_rec, hfid, index_btid, repair);

  lock_unlock_object (thread_p, cls_oid, oid_Root_class_oid, IS_LOCK, true);

  heap_scancache_end (thread_p, &scan);

  return rv;
}

/*
 * locator_check_all_entries_of_all_btrees () - Check consistency of all
 *						entries of all btrees
 *
 * return: valid
 *
 *   repair(in):
 *
 * Note: Check the consistency of all entries of all btrees against the
 *              the corresponding heaps.
 */
DISK_ISVALID
locator_check_all_entries_of_all_btrees (THREAD_ENTRY * thread_p, bool repair)
{
  RECDES copy_rec = RECDES_INITIALIZER;	/* Record descriptor for copy object */
  HFID root_hfid;
  HFID hfid;
  OID oid;
  HEAP_SCANCACHE scan;
  SCAN_CODE code = S_SUCCESS;
  DISK_ISVALID isallvalid = DISK_VALID;
  MVCC_SNAPSHOT *mvcc_snapshot = NULL;

  mvcc_snapshot = logtb_get_mvcc_snapshot (thread_p);
  if (mvcc_snapshot == NULL)
    {
      return DISK_ERROR;
    }

  /* 
   * Find all the classes.
   * If the class has an index, check the logical consistency of the index
   */

  /* Find the heap for the root classes */

  if (boot_find_root_heap (&root_hfid) != NO_ERROR || HFID_IS_NULL (&root_hfid))
    {
      return DISK_ERROR;
    }

  if (heap_scancache_start (thread_p, &scan, &root_hfid, oid_Root_class_oid, true, false, mvcc_snapshot) != NO_ERROR)
    {
      return DISK_ERROR;
    }

  oid.volid = root_hfid.vfid.volid;
  oid.pageid = NULL_PAGEID;
  oid.slotid = NULL_SLOTID;

  while (isallvalid != DISK_ERROR)
    {
      copy_rec.data = NULL;
      code = heap_next (thread_p, &root_hfid, oid_Root_class_oid, &oid, &copy_rec, &scan, COPY);
      if (code != S_SUCCESS)
	{
	  break;
	}

      or_class_hfid (&copy_rec, &hfid);
      if (HFID_IS_NULL (&hfid))
	{
	  continue;
	}

      /* lock class and unlatch page */
      if (lock_object (thread_p, &oid, oid_Root_class_oid, IS_LOCK, LK_COND_LOCK) != LK_GRANTED)
	{
	  if (scan.page_watcher.pgptr != NULL)
	    {
	      pgbuf_ordered_unfix (thread_p, &scan.page_watcher);
	    }

	  if (lock_object (thread_p, &oid, oid_Root_class_oid, IS_LOCK, LK_UNCOND_LOCK) != LK_GRANTED)
	    {
	      break;
	    }

	  copy_rec.data = NULL;
	  code = heap_get_class_record (thread_p, &oid, &copy_rec, &scan, COPY);
	  if (code != S_SUCCESS)
	    {
	      break;
	    }
	}

      /* we have lock on class_oid, record COPYed, while page is latched */
      if (scan.page_watcher.pgptr != NULL)
	{
	  pgbuf_ordered_unfix (thread_p, &scan.page_watcher);
	}

      if (locator_check_class (thread_p, &oid, &copy_rec, &hfid, NULL, repair) != DISK_VALID)
	{
	  isallvalid = DISK_ERROR;
	}

      lock_unlock_object (thread_p, &oid, oid_Root_class_oid, IS_LOCK, true);
    }

  if (code != S_END)
    {
      isallvalid = DISK_ERROR;
    }

  /* End the scan cursor */
  if (heap_scancache_end (thread_p, &scan) != NO_ERROR)
    {
      isallvalid = DISK_ERROR;
    }

  return isallvalid;
}

/*
 * locator_guess_sub_classes () - Guess the subclasses of the given hinted classes
 *
 * return: NO_ERROR if all OK, ER_ status otherwise
 *
 *   lockhint_subclasses(in): lockhint structure which describes classes
 *             The content is updated and the structure itself may be
 *             reallocated
 *
 * Note:This function guess the subclasses identifiers of requested
 *              subclasses for the classes referenced by the lockhint
 *              structure. The lockhint structure is updated to contain the
 *              needed subclasses.
 *
 *              The subclasses are only guessed for lock hint purposes and
 *              they should not be used for any other purposes (it is OK, to
 *              send the objects to the client). That is, the found subclasses
 *              reflects the classes on the server(database volumes) as they
 *              are when the function is invoked; the function does not wait
 *              even when the classes/subclasses may be in the process of been
 *              updated by any transaction.
 *
 *              In general the function is used to approximately find out all
 *              needed subclasses, so that they can be locked along with a
 *              requested set of classes all at once...and not in pieces since
 *              the later can produce deadlocks.
 */
static int
locator_guess_sub_classes (THREAD_ENTRY * thread_p, LC_LOCKHINT ** lockhint_subclasses)
{
  int ref_num;			/* Max and reference number in request area */
  int max_stack;		/* Total size of stack */
  int stack_actual_size;	/* Actual size of stack */
  int *stack;			/* The stack for the search */
  int max_oid_list;		/* Max number of immediate subclasses */
  OID *oid_list = NULL;		/* List of ref for one object */
  HEAP_SCANCACHE scan_cache;	/* Scan cache used for fetching purposes */
  SCAN_CODE scan;		/* Scan return value for an object */
  void *new_ptr;
  RECDES peek_recdes;
  LC_LOCKHINT *lockhint;
  int num_original_classes;
  LOCK lock;
  int i, j, k;
  int error_code = NO_ERROR;

  /* 
   * Start a scan cursor for fetching the desired classes.
   */

  error_code = heap_scancache_start (thread_p, &scan_cache, NULL, NULL, true, false, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  lockhint = *lockhint_subclasses;

  /* 
   * Let's assume a number of subclasses for allocation purposes of the stack.
   * We will assume at least one subclass per class and a minimum of 10
   * subclasses for all requested classes.
   */

  max_stack = lockhint->max_classes * 2;
  if (max_stack < 10)
    {
      max_stack = 10;
    }
  max_oid_list = max_stack;

  stack = (int *) malloc (sizeof (*stack) * max_stack);
  if (stack == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (*stack) * max_stack);
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      goto error;
    }
  oid_list = (OID *) malloc (sizeof (*oid_list) * max_oid_list);
  if (oid_list == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (*oid_list) * max_oid_list);
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      goto error;
    }

  /* 
   * Obtain the subclasses requested.
   */

  num_original_classes = lockhint->num_classes;
  for (i = 0; i < num_original_classes; i++)
    {
      if (OID_ISNULL (&lockhint->classes[i].oid) || OID_ISTEMP (&lockhint->classes[i].oid)
	  || lockhint->classes[i].need_subclasses <= 0)
	{
	  /* 
	   * It has already been visited or we don't care about its subclasses
	   */
	  continue;
	}

      /* 
       * Make sure that this is a valid class
       */

      if (!heap_does_exist (thread_p, NULL, &lockhint->classes[i].oid))
	{
	  if (er_errid () == ER_INTERRUPTED)
	    {
	      error_code = ER_INTERRUPTED;
	      goto error;
	    }

	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_UNKNOWN_OBJECT, 3, lockhint->classes[i].oid.volid,
		  lockhint->classes[i].oid.pageid, lockhint->classes[i].oid.slotid);
	  /* 
	   * The class did not exist, continue even in the event of errors.
	   * Eliminate this class from the list of requested classes.
	   */
	  OID_SET_NULL (&lockhint->classes[i].oid);
	  continue;
	}

      /* 
       * Add the class to the stack and indicate that it has not been visited.
       */

      /* Initialize the stack and push the element */
      stack_actual_size = 0;
      stack[stack_actual_size++] = i;

      /* 
       * Star a kind of depth-first search algorithm to find out subclasses
       */

      while (stack_actual_size > 0)
	{
	  /* Pop */
	  ref_num = stack[--stack_actual_size];

	  /* 
	   * Get the class to find out its immediate subclasses
	   */

	  scan = heap_get_class_record (thread_p, &lockhint->classes[ref_num].oid, &peek_recdes, &scan_cache, PEEK);
	  if (scan != S_SUCCESS)
	    {
	      if (scan != S_DOESNT_EXIST && (lockhint->quit_on_errors == true || er_errid () == ER_INTERRUPTED))
		{
		  error_code = ER_FAILED;
		  goto error;
		}

	      /* 
	       * Continue after an error. Remove the class from the list of
	       * requested classes
	       */

	      if (ref_num == lockhint->num_classes - 1)
		{
		  /* Last element remove it */
		  lockhint->num_classes--;
		}
	      else
		{
		  /* Marked it as invalid */
		  OID_SET_NULL (&lockhint->classes[ref_num].oid);
		}
	      er_clear ();
	      continue;
	    }

	  /* 
	   * has the class been visited ?
	   */

	  if (lockhint->classes[i].need_subclasses <= 0)
	    {
	      /* 
	       * This class has already been visited;
	       */
	      continue;
	    }


	  /* 
	   * Object has never been visited. First time in the stack.
	   * Mark this class as visited.
	   */

	  lockhint->classes[ref_num].need_subclasses = -lockhint->classes[ref_num].need_subclasses;

	  /* 
	   * Find all immediate subclasses for this class
	   */

	  OID_SET_NULL (&oid_list[0]);

	  error_code = orc_subclasses_from_record (&peek_recdes, &max_oid_list, &oid_list);
	  if (error_code != NO_ERROR)
	    {
	      if (lockhint->quit_on_errors == true)
		{
		  goto error;
		}

	      /* Continue even in the case of an error */
	      error_code = NO_ERROR;
	      continue;
	    }

	  /* 
	   * Add the above references to the stack if these classes have not
	   * been already been visited or if their current level is smaller
	   * than their visited level
	   */

	  for (k = 0; k < max_oid_list && !OID_ISNULL (&oid_list[k]); k++)
	    {
	      /* 
	       * Has this class already been listed ?
	       */
	      for (j = 0; j < lockhint->num_classes; j++)
		{
		  if (OID_EQ (&oid_list[k], &lockhint->classes[j].oid))
		    {
		      break;	/* It is already listed */
		    }
		}

	      if (j == lockhint->num_classes)
		{
		  /* 
		   * This is the first time we have seen this class. Push the
		   * class onto the stack.
		   * Make sure that we have area in the stack and the lockhint
		   * area
		   */

		  if (stack_actual_size >= max_stack)
		    {
		      /* Expand the stack by two */
		      max_stack = max_stack * 2;
		      new_ptr = realloc (stack, sizeof (*stack) * max_stack);
		      if (new_ptr == NULL)
			{
			  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
				  sizeof (*stack) * max_stack);
			  if (lockhint->quit_on_errors == false)
			    {
			      /* Finish but without an error */
			      break;
			    }
			  error_code = ER_OUT_OF_VIRTUAL_MEMORY;
			  goto error;
			}
		      stack = (int *) new_ptr;
		    }
		  if (lockhint->num_classes >= lockhint->max_classes)
		    {
		      /* Expand the lockhint area by two */
		      new_ptr = locator_reallocate_lockhint (lockhint, (lockhint->max_classes * 2));
		      if (new_ptr == NULL)
			{
			  if (lockhint->quit_on_errors == false)
			    {
			      /* Finish but without an error */
			      break;
			    }
			  error_code = ER_OUT_OF_VIRTUAL_MEMORY;
			  goto error;
			}
		      lockhint = *lockhint_subclasses = (LC_LOCKHINT *) new_ptr;
		    }

		  /* 
		   * Push the class on the stack.
		   */

		  /* Push */
		  stack[stack_actual_size++] = lockhint->num_classes;

		  COPY_OID (&lockhint->classes[lockhint->num_classes].oid, &oid_list[k]);
		  lockhint->classes[lockhint->num_classes].chn = CHN_UNKNOWN_ATCLIENT;
		  lockhint->classes[lockhint->num_classes].lock = lockhint->classes[ref_num].lock;
		  lockhint->classes[lockhint->num_classes].need_subclasses = 1;
		  lockhint->num_classes++;

		}
	      else
		{
		  /* 
		   * This is a class that has already been listed and it may
		   * have already been visited.
		   */
		  assert (lockhint->classes[j].lock >= NULL_LOCK && lockhint->classes[ref_num].lock >= NULL_LOCK);

		  if (lockhint->classes[j].need_subclasses >= 0)
		    {
		      /* 
		       * The class is only listed at this point. It will be
		       * visited later. The lock may need to be changed, as well
		       * as its subclass flag
		       */

		      /* May be lock change */
		      lockhint->classes[j].lock = lock_Conv[lockhint->classes[j].lock][lockhint->classes[ref_num].lock];
		      assert (lockhint->classes[j].lock != NA_LOCK);

		      /* Make sure that subclasses are obtained */
		      lockhint->classes[j].need_subclasses = 1;
		    }
		  else
		    {
		      /* 
		       * This class has already been visited. We may need to
		       * revisit if a lock conversion is needed as a result of
		       * several super classes
		       */
		      lock = lock_Conv[lockhint->classes[j].lock][lockhint->classes[ref_num].lock];
		      assert (lock != NA_LOCK);

		      if (lockhint->classes[j].lock != lock)
			{
			  /* 
			   * Re-visit
			   */
			  lockhint->classes[j].lock = lock;
			  lockhint->classes[j].need_subclasses = 1;
			  /* Push */
			  stack[stack_actual_size++] = j;
			}
		    }
		}
	    }
	}
    }

  free_and_init (stack);
  free_and_init (oid_list);
  error_code = heap_scancache_end (thread_p, &scan_cache);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  /* 
   * Scan the lockhint area to make the prune levels positive
   */

  for (i = 0; i < lockhint->num_classes; i++)
    {
      lockhint->classes[i].need_subclasses = -lockhint->classes[i].need_subclasses;
    }

  return error_code;

error:

  if (stack)
    {
      free_and_init (stack);
    }
  if (oid_list)
    {
      free_and_init (oid_list);
    }
  (void) heap_scancache_end (thread_p, &scan_cache);

  return error_code;
}

/*
 * xlocator_find_lockhint_class_oids () - Find the oids associated with the given
 *                                  classes
 *
 * return: LC_FIND_CLASSNAME
 *                        (either of LC_CLASSNAME_EXIST,
 *                                   LC_CLASSNAME_DELETED,
 *                                   LC_CLASSNAME_ERROR)
 *
 *   num_classes(in): Number of needed classes
 *   many_classnames(in): Name of the classes
 *   many_locks(in): The desired lock for each class
 *   many_need_subclasses(in): Wheater or not the subclasses are needed.
 *   many_flags(in): flags associated with class names
 *   guessed_class_oids(in):
 *   guessed_class_chns(in):
 *   quit_on_errors(in): Wheater to continue finding the classes in case of
 *                          an error, such as a class does not exist or a lock
 *                          one a may not be granted.
 *   hlock(in): hlock structure which is set to describe the
 *                          classes
 *   fetch_area(in):
 *
 * Note: This function find the class identifiers of the given class
 *              names and requested subclasses of the above classes, and lock
 *              the classes with given locks. The function does not quit when
 *              an error is found and the value of quit_on_errors is false.
 *              In this case the class (an may be its subclasses) with the
 *              error is not locked/fetched.
 *              The function tries to lock all the classes at once, however if
 *              this fails and the function is allowed to continue when errors
 *              are detected, the classes are locked individually.
 *
 *              The subclasses are only guessed for locking purposed and they
 *              should not be used for any other purposes. For example, the
 *              subclasses should not given to the upper levels of the system.
 *
 *              In general the function is used to find out all needed classes
 *              and lock them togheter.
 */
LC_FIND_CLASSNAME
xlocator_find_lockhint_class_oids (THREAD_ENTRY * thread_p, int num_classes, const char **many_classnames,
				   LOCK * many_locks, int *many_need_subclasses, LC_PREFETCH_FLAGS * many_flags,
				   OID * guessed_class_oids, int *guessed_class_chns, int quit_on_errors,
				   LC_LOCKHINT ** hlock, LC_COPYAREA ** fetch_area)
{
  int tran_index;
  LOCATOR_CLASSNAME_ENTRY *entry;
  const char *classname;
  LOCK tmp_lock;
  LC_FIND_CLASSNAME find = LC_CLASSNAME_EXIST;
  LC_FIND_CLASSNAME allfind = LC_CLASSNAME_EXIST;
  bool allneed_subclasses = false;
  int retry;
  int i, j;
  int n;
#if !defined(NDEBUG)
  int check_own;
#endif

  *fetch_area = NULL;

  /* 
   * Let's assume the number of classes that are going to be described in the
   * lockhint area.
   */

  *hlock = locator_allocate_lockhint (num_classes, quit_on_errors);
  if (*hlock == NULL)
    {
      return LC_CLASSNAME_ERROR;
    }

  /* 
   * Find the class oids of the given classnames.
   */

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);

  for (i = 0; i < num_classes && (allfind == LC_CLASSNAME_EXIST || quit_on_errors == false); i++)
    {
      classname = many_classnames[i];
      if (classname == NULL || !(many_flags[i] & LC_PREF_FLAG_LOCK))
	{
	  continue;
	}

      if (many_need_subclasses[i])
	{
	  allneed_subclasses = true;
	}

      n = (*hlock)->num_classes;
      find = LC_CLASSNAME_EXIST;
      retry = 1;

      while (retry)
	{
	  retry = 0;

	  /* 
	   * Describe the hinted class
	   */

	  (*hlock)->classes[n].chn = CHN_UNKNOWN_ATCLIENT;
	  (*hlock)->classes[n].lock = many_locks[i];
	  (*hlock)->classes[n].need_subclasses = many_need_subclasses[i];

	  if (csect_enter_as_reader (thread_p, CSECT_LOCATOR_SR_CLASSNAME_TABLE, INF_WAIT) != NO_ERROR)
	    {
	      assert (false);
	      return LC_CLASSNAME_ERROR;
	    }

	  entry = ((LOCATOR_CLASSNAME_ENTRY *) mht_get (locator_Mht_classnames, classname));

	  if (entry != NULL)
	    {
	      COPY_OID (&(*hlock)->classes[n].oid, &entry->e_current.oid);
	      assert (find == LC_CLASSNAME_EXIST);

	      if (locator_is_exist_class_name_entry (thread_p, entry))
		{
		  assert (find == LC_CLASSNAME_EXIST);	/* OK, go ahead */
		}
	      else
		{
		  assert (entry->e_current.action != LC_CLASSNAME_EXIST);

		  /* 
		   * We can only proceed if the entry belongs to the current
		   * transaction, otherwise, we must lock the class associated
		   * with the classname and retry the operation once the lock
		   * is granted.
		   */
		  assert (find == LC_CLASSNAME_EXIST);

		  if (entry->e_tran_index == tran_index)
		    {
		      if (entry->e_current.action == LC_CLASSNAME_DELETED
			  || entry->e_current.action == LC_CLASSNAME_DELETED_RENAME)
			{
			  find = LC_CLASSNAME_DELETED;
			}
		      else
			{
			  assert (find == LC_CLASSNAME_EXIST);	/* OK, go ahead */
			}
		    }
		  else
		    {
		      csect_exit (thread_p, CSECT_LOCATOR_SR_CLASSNAME_TABLE);

		      /* 
		       * Do not know the fate of this entry until the transaction is
		       * committed or aborted. Get the lock and retry later on.
		       */
		      if ((*hlock)->classes[n].lock != NULL_LOCK)
			{
			  tmp_lock = (*hlock)->classes[n].lock;
			}
		      else
			{
			  tmp_lock = IS_LOCK;
			}

		      if (lock_object (thread_p, &(*hlock)->classes[n].oid, oid_Root_class_oid, tmp_lock,
				       LK_UNCOND_LOCK) != LK_GRANTED)
			{
			  /* 
			   * Unable to acquired the lock; exit retry-loop
			   */
			  allfind = find = LC_CLASSNAME_ERROR;
			  assert (allfind == LC_CLASSNAME_ERROR);
			}
		      else
			{
			  /* 
			   * Try again
			   * Remove the lock.. since the above was a dirty read
			   */
			  lock_unlock_object (thread_p, &(*hlock)->classes[n].oid, oid_Root_class_oid, tmp_lock, true);
			  retry = 1;
			}

		      /* already exit cset */
		      continue;
		    }
		}
	    }
	  else
	    {
	      assert (entry == NULL);

	      find = LC_CLASSNAME_DELETED;
	    }

#if !defined(NDEBUG)
	  check_own = csect_check_own (thread_p, CSECT_LOCATOR_SR_CLASSNAME_TABLE);
	  assert (check_own >= 1);
#endif

	  csect_exit (thread_p, CSECT_LOCATOR_SR_CLASSNAME_TABLE);

	}			/* while (retry) */

      if (find == LC_CLASSNAME_EXIST)
	{
	  /* 
	   * If the client has guessed the right class_oid, use the cache
	   * coherency number on the client to avoid sending the class object
	   */
	  if (guessed_class_oids != NULL && OID_EQ (&(*hlock)->classes[n].oid, &guessed_class_oids[i]))
	    {
	      (*hlock)->classes[n].chn = guessed_class_chns[i];
	    }

	  n++;
	  (*hlock)->num_classes = n;
	}
      else
	{
	  if (allfind != LC_CLASSNAME_ERROR)
	    {
	      allfind = find;
	    }
	  if (find == LC_CLASSNAME_DELETED)
	    {
	      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_LC_UNKNOWN_CLASSNAME, 1, classname);
	    }
	}
    }				/* for (i = 0; ... ) */

  /* 
   * Eliminate any duplicates. Note that we did not want to do above since
   * we did not want to modify the original arrays.
   */

  for (i = 0; i < (*hlock)->num_classes; i++)
    {
      if (OID_ISNULL (&(*hlock)->classes[i].oid))
	{
	  continue;
	}
      /* 
       * Is this duplicated ?
       */
      for (j = i + 1; j < (*hlock)->num_classes; j++)
	{
	  if (OID_EQ (&(*hlock)->classes[i].oid, &(*hlock)->classes[j].oid))
	    {
	      /* Duplicate class, merge the lock and the subclass entry */
	      assert ((*hlock)->classes[i].lock >= NULL_LOCK && (*hlock)->classes[j].lock >= NULL_LOCK);
	      (*hlock)->classes[i].lock = lock_Conv[(*hlock)->classes[i].lock][(*hlock)->classes[j].lock];
	      assert ((*hlock)->classes[i].lock != NA_LOCK);

	      if ((*hlock)->classes[i].need_subclasses == 0)
		{
		  (*hlock)->classes[i].need_subclasses = (*hlock)->classes[j].need_subclasses;
		}

	      /* Now eliminate the entry */
	      OID_SET_NULL (&(*hlock)->classes[j].oid);
	    }
	}
    }

  /* 
   * Do we need to get subclasses ?
   */

  if (allneed_subclasses == true && (allfind == LC_CLASSNAME_EXIST || quit_on_errors == false))
    {
      if (locator_guess_sub_classes (thread_p, &(*hlock)) != NO_ERROR)
	{
	  allfind = LC_CLASSNAME_ERROR;
	}
    }

  if (allfind == LC_CLASSNAME_EXIST || quit_on_errors == false)
    {
      if (xlocator_fetch_lockhint_classes (thread_p, (*hlock), fetch_area) != NO_ERROR)
	{
	  allfind = LC_CLASSNAME_ERROR;
	  if (quit_on_errors == true)
	    {
	      locator_free_lockhint ((*hlock));
	      *hlock = NULL;
	    }
	}
    }
  else
    {
      locator_free_lockhint ((*hlock));
      *hlock = NULL;
    }

  if (logtb_tran_prepare_count_optim_classes (thread_p, many_classnames, many_flags, num_classes) != NO_ERROR)
    {
      allfind = LC_CLASSNAME_ERROR;
    }

  return allfind;
}

/*
 * xlocator_fetch_lockhint_classes () - Lock and fetch a set of classes
 *
 * return: NO_ERROR if all OK, ER_ status otherwise
 *
 *   lockhint(in): Description of hinted classses
 *   fetch_area(in/out): Pointer to area where the objects are placed
 *
 */
int
xlocator_fetch_lockhint_classes (THREAD_ENTRY * thread_p, LC_LOCKHINT * lockhint, LC_COPYAREA ** fetch_area)
{
  LOCATOR_RETURN_NXOBJ nxobj;	/* Description to return next object */
  LC_COPYAREA_DESC prefetch_des;	/* Descriptor for decache of objects related to transaction isolation level */
  SCAN_CODE scan = S_SUCCESS;
  int copyarea_length;
  int i;
  int error_code = NO_ERROR;

  *fetch_area = NULL;

  if (lockhint->num_classes <= 0)
    {
      lockhint->num_classes_processed = lockhint->num_classes;
      return NO_ERROR;
    }

  if (lockhint->num_classes_processed == -1)
    {
      /* 
       * FIRST CALL.
       * Initialize num of object processed.
       */
      lockhint->num_classes_processed = 0;

      /* Obtain the locks */
      if (lock_classes_lock_hint (thread_p, lockhint) != LK_GRANTED)
	{
	  if (lockhint->quit_on_errors != false)
	    {
	      return ER_FAILED;
	    }
	  else
	    {
	      error_code = ER_FAILED;
	      /* Lock individual classes */
	      for (i = 0; i < lockhint->num_classes; i++)
		{
		  if (OID_ISNULL (&lockhint->classes[i].oid))
		    {
		      continue;
		    }
		  if (lock_object (thread_p, &lockhint->classes[i].oid, oid_Root_class_oid, lockhint->classes[i].lock,
				   LK_UNCOND_LOCK) != LK_GRANTED)
		    {
		      OID_SET_NULL (&lockhint->classes[i].oid);
		    }
		  else
		    {
		      /* We are unable to continue since we lock at least one */
		      error_code = NO_ERROR;
		    }
		}
	      if (error_code != NO_ERROR)
		{
		  return error_code;
		}
	    }
	}
    }

  /* 
   * Start a scan cursor for getting the classes
   */

  error_code = heap_scancache_start (thread_p, &nxobj.area_scancache, NULL, NULL, true, false, NULL);
  if (error_code != NO_ERROR)
    {
      lock_unlock_classes_lock_hint (thread_p, lockhint);
      return error_code;
    }

  nxobj.ptr_scancache = &nxobj.area_scancache;

  /* 
   * Assume that there are not any classes larger than one page. If there are
   * the number of pages is fixed later.
   */

  /* Assume that the needed object can fit in one page */
  copyarea_length = DB_PAGESIZE;

  nxobj.mobjs = NULL;
  nxobj.comm_area = NULL;

  while (scan == S_SUCCESS && (lockhint->num_classes_processed < lockhint->num_classes))
    {
      nxobj.comm_area = locator_allocate_copy_area_by_length (copyarea_length);
      if (nxobj.comm_area == NULL)
	{
	  (void) heap_scancache_end (thread_p, &nxobj.area_scancache);
	  lock_unlock_classes_lock_hint (thread_p, lockhint);
	  return ER_FAILED;
	}

      nxobj.mobjs = LC_MANYOBJS_PTR_IN_COPYAREA (nxobj.comm_area);
      nxobj.obj = LC_START_ONEOBJ_PTR_IN_COPYAREA (nxobj.mobjs);
      LC_RECDES_IN_COPYAREA (nxobj.comm_area, &nxobj.recdes);
      nxobj.area_offset = 0;
      nxobj.mobjs->num_objs = 0;

      /* 
       * Place the classes on the communication area, don't place those classes
       * with correct chns.
       */

      for (i = lockhint->num_classes_processed; scan == S_SUCCESS && i < lockhint->num_classes; i++)
	{
	  if (OID_ISNULL (&lockhint->classes[i].oid) || OID_ISTEMP (&lockhint->classes[i].oid))
	    {
	      lockhint->num_classes_processed += 1;
	      continue;
	    }

	  /* Now return the object */
	  /* Since classes are already locked, call locator_lock_and_return_object with NULL_LOCK as lock_mode argument 
	   * to skip locking. */
	  scan =
	    locator_lock_and_return_object (thread_p, &nxobj, oid_Root_class_oid, &lockhint->classes[i].oid,
					    lockhint->classes[i].chn, NULL_LOCK, S_SELECT);
	  if (scan == S_SUCCESS)
	    {
	      lockhint->num_classes_processed += 1;
	    }
	  else if (scan == S_DOESNT_FIT && nxobj.mobjs->num_objs == 0)
	    {
	      /* 
	       * The first object on the copy area does not fit.
	       * Get a larger area
	       */

	      /* Get the real length of the fetch/copy area */

	      copyarea_length = nxobj.comm_area->length;

	      if ((-nxobj.recdes.length) > copyarea_length)
		{
		  copyarea_length = (DB_ALIGN (-nxobj.recdes.length, MAX_ALIGNMENT) + sizeof (*nxobj.mobjs));
		}
	      else
		{
		  copyarea_length += DB_PAGESIZE;
		}

	      locator_free_copy_area (nxobj.comm_area);
	      scan = S_SUCCESS;
	      break;		/* finish the for */
	    }
	  else if (scan != S_DOESNT_FIT && (scan == S_DOESNT_EXIST || lockhint->quit_on_errors == false))
	    {
	      OID_SET_NULL (&lockhint->classes[i].oid);
	      lockhint->num_classes_processed += 1;
	      scan = S_SUCCESS;
	    }
	  else
	    {
	      break;		/* Quit on errors */
	    }
	}
    }

  /* End the scan cursor */
  error_code = heap_scancache_end (thread_p, &nxobj.area_scancache);
  if (error_code != NO_ERROR)
    {
      /* There was an error.. */
      if (nxobj.mobjs != NULL)
	{
	  locator_free_copy_area (nxobj.comm_area);
	}
      lock_unlock_classes_lock_hint (thread_p, lockhint);
      return error_code;
    }

  if (scan == S_ERROR)
    {
      /* There was an error.. */
      if (nxobj.mobjs != NULL)
	{
	  locator_free_copy_area (nxobj.comm_area);
	}
      lock_unlock_classes_lock_hint (thread_p, lockhint);
      return ER_FAILED;
    }
  else if (nxobj.mobjs != NULL && nxobj.mobjs->num_objs == 0)
    {
      locator_free_copy_area (nxobj.comm_area);
    }
  else
    {
      *fetch_area = nxobj.comm_area;
    }

  if (*fetch_area != NULL)
    {
      prefetch_des.mobjs = nxobj.mobjs;
      prefetch_des.obj = &nxobj.obj;
      prefetch_des.offset = &nxobj.area_offset;
      prefetch_des.recdes = &nxobj.recdes;
      lock_notify_isolation_incons (thread_p, locator_notify_decache, &prefetch_des);
    }

  if (lockhint->num_classes_processed >= lockhint->num_classes)
    {
      lock_unlock_classes_lock_hint (thread_p, lockhint);
    }

  return NO_ERROR;
}

/*
 * xlocator_assign_oid_batch () - Assign a group of permanent oids
 *
 * return:  NO_ERROR if all OK, ER_ status otherwise
 *
 *   oidset(in): LC_OIDSET describing all of the temp oids
 *
 * Note:Permanent oids are assigned to each of the temporary oids
 *              listed in the LC_OIDSET.
 */
int
xlocator_assign_oid_batch (THREAD_ENTRY * thread_p, LC_OIDSET * oidset)
{
  LC_CLASS_OIDSET *class_oidset;
  LC_OIDMAP *oid;
  int error_code = NO_ERROR;

  /* establish a rollback point in case we get an error part way through */
  log_sysop_start (thread_p);

  /* Now assign the OID's stop on the first error */
  for (class_oidset = oidset->classes; class_oidset != NULL; class_oidset = class_oidset->next)
    {
      for (oid = class_oidset->oids; oid != NULL; oid = oid->next)
	{
	  error_code =
	    xlocator_assign_oid (thread_p, &class_oidset->hfid, &oid->oid, oid->est_size, &class_oidset->class_oid,
				 NULL);
	  if (error_code != NO_ERROR)
	    {
	      goto error;
	    }
	}
    }

  /* accept the operation */
  log_sysop_attach_to_outer (thread_p);

  return error_code;

error:
  /* rollback the operation */
  log_sysop_abort (thread_p);
  return error_code;
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * locator_increase_catalog_count () -
 *
 * return:
 *
 *   cls_oid(in): Class OID
 *
 * Note:Increase the 'tot_objects' counter of the CLS_INFO
 *        and do update the catalog record in-place.
 */
static void
locator_increase_catalog_count (THREAD_ENTRY * thread_p, OID * cls_oid)
{
  CLS_INFO *cls_infop = NULL;

  /* retrieve the class information */
  cls_infop = catalog_get_class_info (thread_p, cls_oid);
  if (cls_infop == NULL)
    {
      return;
    }

  if (cls_infop->ci_hfid.vfid.fileid < 0 || cls_infop->ci_hfid.vfid.volid < 0)
    {
      /* The class does not have a heap file (i.e. it has no instances); so no statistics can be obtained for this
       * class; just set 'tot_objects' field to 0. */
      /* Is it safe not to initialize the other fields of CLS_INFO? */
      cls_infop->ci_tot_objects = 0;
    }
  else
    {
      /* increase the 'tot_objects' counter */
      cls_infop->ci_tot_objects++;
    }

  /* update the class information to the catalog */
  /* NOTE that tot_objects may not be correct because changes are NOT logged. */
  (void) catalog_update_class_info (thread_p, cls_oid, cls_infop, true);

  catalog_free_class_info (cls_infop);
}

/*
 * locator_decrease_catalog_count  () -
 *
 * return:
 *
 *   cls_oid(in): Class OID
 *
 * Note: Descrease the 'tot_objects' counter of the CLS_INFO
 *        and do update the catalog record in-place.
 */
static void
locator_decrease_catalog_count (THREAD_ENTRY * thread_p, OID * cls_oid)
{
  CLS_INFO *cls_infop = NULL;

  /* retrieve the class information */
  cls_infop = catalog_get_class_info (thread_p, cls_oid);
  if (cls_infop == NULL)
    {
      return;
    }

  if (cls_infop->ci_hfid.vfid.fileid < 0 || cls_infop->ci_hfid.vfid.volid < 0)
    {
      /* The class does not have a heap file (i.e. it has no instances); so no statistics can be obtained for this
       * class; just set 'tot_objects' field to 0. */
      /* Is it an error to delete an instance with no heap file? */
      cls_infop->ci_tot_objects = 0;
    }

  /* decrease the 'tot_objects' counter */
  if (cls_infop->ci_tot_objects > 0)
    {
      cls_infop->ci_tot_objects--;
    }

  /* update the class information to the catalog */
  /* NOTE that tot_objects may not be correct because changes are NOT logged. */
  (void) catalog_update_class_info (thread_p, cls_oid, cls_infop, true);

  catalog_free_class_info (cls_infop);
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * xrepl_set_info () -
 *
 * return:
 *
 *   repl_info(in):
 */
int
xrepl_set_info (THREAD_ENTRY * thread_p, REPL_INFO * repl_info)
{
  int error_code = NO_ERROR;

  if (!LOG_CHECK_LOG_APPLIER (thread_p) && log_does_allow_replication () == true)
    {
      switch (repl_info->repl_info_type)
	{
	case REPL_INFO_TYPE_SBR:
	  error_code = repl_log_insert_statement (thread_p, (REPL_INFO_SBR *) repl_info->info);
	  break;
	default:
	  error_code = ER_REPL_ERROR;
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_REPL_ERROR, 1, "can't make repl sbr info");
	  break;
	}
    }

  return error_code;
}

/*
 * xrepl_log_get_append_lsa () -
 *
 * return:
 */
LOG_LSA *
xrepl_log_get_append_lsa (void)
{
  return log_get_append_lsa ();
}

/*
 * xlocator_check_fk_validity () -
 *
 * return: NO_ERROR if all OK, ER_ status otherwise
 *
 *   cls_oid(in):
 *   hfid(in):
 *   key_type(in):
 *   n_attrs(in):
 *   attr_ids(in):
 *   pk_cls_oid(in):
 *   pk_btid(in):
 *   fk_name(in):
 */
int
xlocator_check_fk_validity (THREAD_ENTRY * thread_p, OID * cls_oid, HFID * hfid, TP_DOMAIN * key_type, int n_attrs,
			    int *attr_ids, OID * pk_cls_oid, BTID * pk_btid, char *fk_name)
{
  HEAP_SCANCACHE scan_cache;
  HEAP_CACHE_ATTRINFO attr_info;
  OID oid;
  RECDES copy_recdes;
  DB_VALUE *key_val, tmpval;
  char midxkey_buf[DBVAL_BUFSIZE + MAX_ALIGNMENT], *aligned_midxkey_buf;
  int error_code;
  MVCC_SNAPSHOT *mvcc_snapshot = NULL;

  mvcc_snapshot = logtb_get_mvcc_snapshot (thread_p);
  if (mvcc_snapshot == NULL)
    {
      error_code = er_errid ();
      return (error_code == NO_ERROR ? ER_FAILED : error_code);
    }

  DB_MAKE_NULL (&tmpval);

  aligned_midxkey_buf = PTR_ALIGN (midxkey_buf, MAX_ALIGNMENT);

  error_code = heap_scancache_start (thread_p, &scan_cache, hfid, cls_oid, false, false, mvcc_snapshot);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = heap_attrinfo_start (thread_p, cls_oid, n_attrs, attr_ids, &attr_info);
  if (error_code != NO_ERROR)
    {
      (void) heap_scancache_end (thread_p, &scan_cache);
      return error_code;
    }

  OID_SET_NULL (&oid);
  oid.volid = hfid->vfid.volid;

  copy_recdes.data = NULL;
  while (heap_next (thread_p, hfid, NULL, &oid, &copy_recdes, &scan_cache, COPY) == S_SUCCESS)
    {
      if (n_attrs == 1)
	{
	  error_code = heap_attrinfo_read_dbvalues (thread_p, &oid, &copy_recdes, NULL, &attr_info);
	  if (error_code != NO_ERROR)
	    {
	      goto end;
	    }
	}

      key_val =
	heap_attrinfo_generate_key (thread_p, n_attrs, attr_ids, NULL, &attr_info, &copy_recdes, &tmpval,
				    aligned_midxkey_buf, NULL);
      if (key_val == NULL)
	{
	  error_code = ER_FAILED;
	  goto end;
	}

      error_code =
	btree_check_foreign_key (thread_p, cls_oid, hfid, &oid, key_val, n_attrs, pk_cls_oid, pk_btid, fk_name);

      if (key_val == &tmpval)
	{
	  pr_clear_value (&tmpval);
	}

      if (error_code != NO_ERROR)
	{
	  goto end;
	}
    }

end:

  (void) heap_scancache_end (thread_p, &scan_cache);
  heap_attrinfo_end (thread_p, &attr_info);

  return error_code;
}

int
xlocator_prefetch_repl_insert (THREAD_ENTRY * thread_p, OID * class_oid, RECDES * recdes, bool update_last_reprid)
{
  int error = NO_ERROR;
  int i = 0;
  REPR_ID last_repr_id;
  RECDES classrec;
  HEAP_SCANCACHE scan;
  HEAP_CACHE_ATTRINFO attr_info;
  HEAP_IDX_ELEMENTS_INFO idx_info;

  if (heap_scancache_quick_start_root_hfid (thread_p, &scan) != NO_ERROR)
    {
      return ER_FAILED;
    }

  if (heap_get_class_record (thread_p, class_oid, &classrec, &scan, PEEK) != S_SUCCESS)
    {
      heap_scancache_end (thread_p, &scan);
      return ER_FAILED;
    }

  if (heap_attrinfo_start_with_index (thread_p, class_oid, &classrec, &attr_info, &idx_info) < 0)
    {
      heap_scancache_end (thread_p, &scan);
      return ER_FAILED;
    }

  /**
   * if we prefetch for update or delete,
   * we don't need to update last representation id
   */
  if (update_last_reprid)
    {
      last_repr_id = heap_get_class_repr_id (thread_p, class_oid);
      if (last_repr_id == 0)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CT_INVALID_REPRID, 1, last_repr_id);
	  error = ER_CT_INVALID_REPRID;
	  goto free_and_return;
	}

      or_replace_rep_id (recdes, last_repr_id);
    }

  if (idx_info.num_btids <= 0)
    {
      goto free_and_return;
    }

  for (i = 0; i < idx_info.num_btids; i++)
    {
      error = locator_prefetch_index_page (thread_p, class_oid, &classrec, recdes, i, &attr_info);
      if (error != NO_ERROR)
	{
	  goto free_and_return;
	}
    }

free_and_return:
  heap_attrinfo_end (thread_p, &attr_info);
  heap_scancache_end (thread_p, &scan);

  return error;
}

int
xlocator_prefetch_repl_update_or_delete (THREAD_ENTRY * thread_p, BTID * btid, OID * class_oid, DB_VALUE * key_value)
{
  int error = NO_ERROR;
  OID unique_oid;
  RECDES recdes;
  HEAP_SCANCACHE scan;

  if (xbtree_find_unique (thread_p, btid, S_SELECT, key_value, class_oid, &unique_oid, true) == BTREE_KEY_FOUND)
    {
      if (heap_scancache_quick_start (&scan) != NO_ERROR)
	{
	  return ER_FAILED;
	}

      if (heap_get_visible_version (thread_p, &unique_oid, class_oid, &recdes, &scan, PEEK, NULL_CHN) != S_SUCCESS)
	{
	  heap_scancache_end (thread_p, &scan);
	  return ER_FAILED;
	}

      error = xlocator_prefetch_repl_insert (thread_p, class_oid, &recdes, false);

      heap_scancache_end (thread_p, &scan);
    }

  return error;
}

/*
 * xlocator_lock_and_fetch_all () - Fetch all class instances that can be locked
 *				    in specified locked time
 * return: NO_ERROR if all OK, ER_ status otherwise
 *
 *   hfid(in): Heap file where the instances of the class are placed
 *   instance_lock(in): Instance lock to aquire
 *   instance_lock_timeout(in): Timeout for instance lock
 *   class_oid(in): Class identifier of the instances to fetch
 *   class_lock(in): Lock to acquire (Set as a side effect to NULL_LOCKID)
 *   nobjects(out): Total number of objects to fetch.
 *   nfetched(out): Current number of object fetched.
 *   nfailed_instance_locks(out): count failed instance locks
 *   last_oid(out): Object identifier of last fetched object
 *   fetch_area(in/out): Pointer to area where the objects are placed
 *   mvcc_snapshot(in): the snapshot to be applied in scan
 *
 */
int
xlocator_lock_and_fetch_all (THREAD_ENTRY * thread_p, const HFID * hfid, LOCK * instance_lock,
			     int *instance_lock_timeout, OID * class_oid, LOCK * class_lock, int *nobjects,
			     int *nfetched, int *nfailed_instance_locks, OID * last_oid, LC_COPYAREA ** fetch_area,
			     MVCC_SNAPSHOT * mvcc_snapshot)
{
  LC_COPYAREA_DESC prefetch_des;	/* Descriptor for decache of objects related to transaction isolation level */
  LC_COPYAREA_MANYOBJS *mobjs;	/* Describe multiple objects in area */
  LC_COPYAREA_ONEOBJ *obj;	/* Describe on object in area */
  RECDES recdes;		/* Record descriptor for insertion */
  int offset;			/* Place to store next object in area */
  int round_length;		/* Length of object rounded to integer alignment */
  int copyarea_length;
  OID oid;
  HEAP_SCANCACHE scan_cache;
  SCAN_CODE scan;
  int error_code = NO_ERROR;

  if (fetch_area == NULL)
    {
      return ER_FAILED;
    }
  *fetch_area = NULL;

  if (nfailed_instance_locks == NULL)
    {
      return ER_FAILED;
    }
  *nfailed_instance_locks = 0;

  if (OID_ISNULL (last_oid))
    {
      /* FIRST TIME. */

      /* Obtain the desired lock for the class scan */
      if (*class_lock != NULL_LOCK
	  && lock_object (thread_p, class_oid, oid_Root_class_oid, *class_lock, LK_UNCOND_LOCK) != LK_GRANTED)
	{
	  /* 
	   * Unable to acquired lock
	   */
	  *class_lock = NULL_LOCK;
	  *nobjects = -1;
	  *nfetched = -1;

	  error_code = ER_FAILED;
	  goto error;
	}

      /* Get statistics */
      last_oid->volid = hfid->vfid.volid;
      last_oid->pageid = NULL_PAGEID;
      last_oid->slotid = NULL_SLOTID;
      /* Estimate the number of objects to be fetched */
      *nobjects = heap_estimate_num_objects (thread_p, hfid);
      *nfetched = 0;
      if (*nobjects == -1)
	{
	  error_code = ER_FAILED;
	  goto error;
	}
    }

  /* Set OID to last fetched object */
  COPY_OID (&oid, last_oid);

  /* Start a scan cursor for getting several classes */
  error_code = heap_scancache_start (thread_p, &scan_cache, hfid, class_oid, true, false, mvcc_snapshot);
  if (error_code != NO_ERROR)
    {
      goto error;
    }

  /* Assume that the next object can fit in one page */
  copyarea_length = DB_PAGESIZE;

  while (true)
    {
      *fetch_area = locator_allocate_copy_area_by_length (copyarea_length);
      if (*fetch_area == NULL)
	{
	  (void) heap_scancache_end (thread_p, &scan_cache);
	  error_code = ER_FAILED;
	  goto error;
	}

      mobjs = LC_MANYOBJS_PTR_IN_COPYAREA (*fetch_area);
      LC_RECDES_IN_COPYAREA (*fetch_area, &recdes);
      obj = LC_START_ONEOBJ_PTR_IN_COPYAREA (mobjs);
      mobjs->num_objs = 0;
      offset = 0;

      while (true)
	{
	  if (instance_lock && (*instance_lock != NULL_LOCK))
	    {
	      int lock_result = 0;

	      scan = heap_next (thread_p, hfid, class_oid, &oid, &recdes, &scan_cache, COPY);
	      if (scan != S_SUCCESS)
		{
		  break;
		}

	      if (instance_lock_timeout == NULL)
		{
		  lock_result = lock_object (thread_p, &oid, class_oid, *instance_lock, LK_UNCOND_LOCK);
		}
	      else
		{
		  lock_result =
		    lock_object_wait_msecs (thread_p, &oid, class_oid, *instance_lock, LK_UNCOND_LOCK,
					    *instance_lock_timeout);
		}

	      if (lock_result != LK_GRANTED)
		{
		  (*nfailed_instance_locks)++;
		  continue;
		}

	      scan = heap_get_visible_version (thread_p, &oid, class_oid, &recdes, &scan_cache, COPY, NULL_CHN);
	      if (scan != S_SUCCESS)
		{
		  (*nfailed_instance_locks)++;
		  continue;
		}

	    }
	  else
	    {
	      scan = heap_next (thread_p, hfid, class_oid, &oid, &recdes, &scan_cache, COPY);
	      if (scan != S_SUCCESS)
		{
		  break;
		}
	    }

	  mobjs->num_objs++;
	  COPY_OID (&obj->class_oid, class_oid);
	  COPY_OID (&obj->oid, &oid);
	  obj->flag = 0;
	  obj->hfid = NULL_HFID;
	  obj->length = recdes.length;
	  obj->offset = offset;
	  obj->operation = LC_FETCH;
	  obj = LC_NEXT_ONEOBJ_PTR_IN_COPYAREA (obj);
	  round_length = DB_ALIGN (recdes.length, MAX_ALIGNMENT);
#if !defined(NDEBUG)
	  /* suppress valgrind UMW error */
	  memset (recdes.data + recdes.length, 0, MIN (round_length - recdes.length, recdes.area_size - recdes.length));
#endif
	  offset += round_length;
	  recdes.data += round_length;
	  recdes.area_size -= round_length + sizeof (*obj);
	}

      if (scan != S_DOESNT_FIT || mobjs->num_objs > 0)
	{
	  break;
	}
      /* 
       * The first object does not fit into given copy area
       * Get a larger area
       */

      /* Get the real length of current fetch/copy area */
      copyarea_length = (*fetch_area)->length;
      locator_free_copy_area (*fetch_area);

      /* 
       * If the object does not fit even when the copy area seems to be
       * large enough, increase the copy area by at least one page size.
       */

      if ((-recdes.length) > copyarea_length)
	{
	  copyarea_length = DB_ALIGN (-recdes.length, MAX_ALIGNMENT) + sizeof (*mobjs);
	}
      else
	{
	  copyarea_length += DB_PAGESIZE;
	}
    }

  if (scan == S_END)
    {
      /* 
       * This is the end of the loop. Indicate the caller that no more calls
       * are needed by setting nobjects and nfetched to the same value.
       */
      error_code = heap_scancache_end (thread_p, &scan_cache);
      if (error_code != NO_ERROR)
	{
	  *nobjects = *nfetched = -1;
	  goto error;
	}

      *nfetched += mobjs->num_objs;
      *nobjects = *nfetched;
      OID_SET_NULL (last_oid);
    }
  else if (scan == S_ERROR)
    {
      /* There was an error.. */
      (void) heap_scancache_end (thread_p, &scan_cache);
      *nobjects = *nfetched = -1;
      error_code = ER_FAILED;
      goto error;
    }
  else if (mobjs->num_objs != 0)
    {
      heap_scancache_end_when_scan_will_resume (thread_p, &scan_cache);
      /* Set the last_oid.. and the number of fetched objects */
      obj = LC_PRIOR_ONEOBJ_PTR_IN_COPYAREA (obj);
      COPY_OID (last_oid, &obj->oid);
      *nfetched += mobjs->num_objs;
      /* 
       * If the guess on the number of objects to fetch was low, reset the
       * value, so that the caller continue to call us until the end of the
       * scan
       */
      if (*nobjects <= *nfetched)
	{
	  *nobjects = *nfetched + 10;
	}
    }
  else
    {
      error_code = heap_scancache_end (thread_p, &scan_cache);
      if (error_code != NO_ERROR)
	{
	  goto end;
	}
    }

  if (*fetch_area != NULL)
    {
      prefetch_des.mobjs = mobjs;
      prefetch_des.obj = &obj;
      prefetch_des.offset = &offset;
      prefetch_des.recdes = &recdes;
      lock_notify_isolation_incons (thread_p, locator_notify_decache, &prefetch_des);
    }

end:

  return error_code;

error:
  if (*class_lock != NULL_LOCK)
    {
      lock_unlock_object (thread_p, class_oid, oid_Root_class_oid, *class_lock, false);
    }
  if (*fetch_area != NULL)
    {
      locator_free_copy_area (*fetch_area);
      *fetch_area = NULL;
    }
  return error_code;
}

/*
 * xlocator_upgrade_instances_domain () - scans all instances of a class and
 *		performs an in-place domain upgrade of the specified attribute
 *		(identified by its id) from the domain found in its current
 *		stored representation to the domain found in the last
 *		representation
 *
 * return: NO_ERROR if all OK, ER_ status otherwise
 *
 *   thread_p(in): thread context
 *   class_oid(in): class to upgrade
 *   att_id(in): attribute id within class to update
 *
 *  Note: This function is used in context of ALTER CHANGE (type change
 *	  syntax). Proper lock (SCH_M_LOCK) is assumed when this function
 *	  is reached.
 *	  The entire tuple is rewritten using the new latest representation.
 */
int
xlocator_upgrade_instances_domain (THREAD_ENTRY * thread_p, OID * class_oid, int att_id)
{
  LOCK class_lock = SCH_M_LOCK;
  LOCK oid_lock = X_LOCK;
  LC_COPYAREA *fetch_area = NULL;	/* Area where objects are received */
  LC_COPYAREA_MANYOBJS *mobjs = NULL;	/* Describe multiple objects in area */
  LC_COPYAREA_ONEOBJ *obj = NULL;	/* Describe one object in area */
  RECDES recdes;
  HEAP_CACHE_ATTRINFO attr_info;
  HEAP_SCANCACHE upd_scancache;
  int error = NO_ERROR;
  HFID hfid;
  int nobjects = 0, nfetched = 0, i = 0;
  OID last_oid;
  bool scancache_inited = false;
  bool attrinfo_inited = false;
  int tran_index;
  LOG_TDES *tdes = LOG_FIND_CURRENT_TDES (thread_p);
  MVCCID threshold_mvccid;

  HFID_SET_NULL (&hfid);
  OID_SET_NULL (&last_oid);

  if (class_oid == NULL || OID_ISNULL (class_oid) || att_id < 0)
    {
      error = ER_UNEXPECTED;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, "Unexpected argument of class OID or attribute id.");
      goto error_exit;
    }

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  nobjects = 0;
  nfetched = -1;

  error = heap_get_hfid_from_class_oid (thread_p, class_oid, &hfid);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  error = heap_scancache_start_modify (thread_p, &upd_scancache, &hfid, class_oid, SINGLE_ROW_UPDATE, NULL);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }
  scancache_inited = true;

  if (tdes->block_global_oldest_active_until_commit == false)
    {
      /* do not allow to advance with vacuum_Global_oldest_active_mvccid */
      ATOMIC_INC_32 (&vacuum_Global_oldest_active_blockers_counter, 1);
      tdes->block_global_oldest_active_until_commit = true;
    }
  else
    {
      assert (vacuum_Global_oldest_active_blockers_counter > 0);
    }

  /* Can't use vacuum_Global_oldest_active_mvccid here. That's because we want to avoid scenarios where VACUUM compute
   * oldest active mvccid, but didn't set yet vacuum_Global_oldest_active_mvccid, current transaction uses the old
   * value of vacuum_Global_oldest_active_mvccid, then VACUUM uses updated value of vacuum_Global_oldest_active_mvccid. 
   * In such scenario, VACUUM can remove heap records that can't be removed by the current thread. */
  threshold_mvccid = logtb_get_oldest_active_mvccid (thread_p);

  /* VACUUM all cleanable heap objects before upgrading the domain */
  error = heap_vacuum_all_objects (thread_p, &upd_scancache, threshold_mvccid);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  error = heap_attrinfo_start (thread_p, class_oid, -1, NULL, &attr_info);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }
  attrinfo_inited = true;

  while (nobjects != nfetched)
    {
      int nfailed_instances = 0;

      error =
	xlocator_lock_and_fetch_all (thread_p, &hfid, &oid_lock, NULL, class_oid, &class_lock, &nobjects, &nfetched,
				     &nfailed_instances, &last_oid, &fetch_area, NULL);
      if (error != NO_ERROR)
	{
	  goto error_exit;
	}

      if (nfailed_instances != 0)
	{
	  error = ER_UNEXPECTED;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, "Some instance is not locked.");
	  goto error_exit;
	}

      if (fetch_area == NULL)
	{
	  error = ER_UNEXPECTED;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, "Fetch area should not be NULL.");
	  goto error_exit;
	}
      mobjs = LC_MANYOBJS_PTR_IN_COPYAREA (fetch_area);
      obj = LC_START_ONEOBJ_PTR_IN_COPYAREA (mobjs);

      for (i = 0; i < mobjs->num_objs; i++)
	{
	  if (obj->operation == LC_FETCH_DECACHE_LOCK)
	    {
	      /* Skip decache lock objects, they have been added by lock_notify_isolation_incons function. */
	      obj = LC_NEXT_ONEOBJ_PTR_IN_COPYAREA (obj);
	      continue;
	    }
	  LC_RECDES_TO_GET_ONEOBJ (fetch_area, obj, &recdes);

	  error = heap_attrinfo_clear_dbvalues (&attr_info);
	  if (error != NO_ERROR)
	    {
	      goto error_exit;
	    }

	  error = heap_attrinfo_read_dbvalues (thread_p, &obj->oid, &recdes, NULL, &attr_info);
	  if (error != NO_ERROR)
	    {
	      goto error_exit;
	    }

	  error = heap_object_upgrade_domain (thread_p, &upd_scancache, &attr_info, &obj->oid, att_id);

	  if (error != NO_ERROR)
	    {
	      goto error_exit;
	    }

	  COPY_OID (&last_oid, &obj->oid);
	  obj = LC_NEXT_ONEOBJ_PTR_IN_COPYAREA (obj);
	}
      if (fetch_area)
	{
	  locator_free_copy_area (fetch_area);
	  fetch_area = NULL;
	}
    }

error_exit:

  if (fetch_area)
    {
      locator_free_copy_area (fetch_area);
      fetch_area = NULL;
    }
  if (attrinfo_inited)
    {
      heap_attrinfo_end (thread_p, &attr_info);
    }
  if (scancache_inited)
    {
      heap_scancache_end_modify (thread_p, &upd_scancache);
    }

  return error;
}

/*
 * locator_filter_errid() -
 *
 * return:
 *   num_ignore_error_count(in):
 *   ignore_error_list(in):
 *   error_code(in):
 */
static int
locator_filter_errid (THREAD_ENTRY * thread_p, int num_ignore_error_count, int *ignore_error_list)
{
  int i;
  int error_code;

  assert (er_errid () != NO_ERROR);
  error_code = er_errid ();

  for (i = 0; i < num_ignore_error_count; i++)
    {
      if (ignore_error_list[i] == error_code)
	{
	  er_clear ();
	  return NO_ERROR;
	}
    }
  return error_code;
}

/*
 * locator_get_partition_scancache () - setup a cache for repeated operations
 *					which require partition pruning
 * return : PRUNING_SCAN_CACHE on success, NULL on error
 * pcontext (in)  : pruning context
 * class_oid (in) : OID of the pruned partition
 * hfid (in)	  : HFID of the pruned partition
 * op_type (in)	  : operation type
 * has_function_indexes (in)  : true if function indexes should be cached
 */
PRUNING_SCAN_CACHE *
locator_get_partition_scancache (PRUNING_CONTEXT * pcontext, const OID * class_oid, const HFID * hfid, int op_type,
				 bool has_function_indexes)
{
  PRUNING_SCAN_CACHE *scan_cache = NULL;
  int error_code = NO_ERROR;

  scan_cache = partition_get_scancache (pcontext, class_oid);
  if (scan_cache != NULL)
    {
      /* already cached, return it */
      return scan_cache;
    }

  /* create a new one and cache it */
  scan_cache = partition_new_scancache (pcontext);
  if (scan_cache == NULL)
    {
      return NULL;
    }
  error_code = locator_start_force_scan_cache (pcontext->thread_p, &scan_cache->scan_cache, hfid, class_oid, op_type);
  if (error_code != NO_ERROR)
    {
      return NULL;
    }

  scan_cache->is_scan_cache_started = true;

  if (has_function_indexes)
    {
      HEAP_CACHE_ATTRINFO attr_info;

      error_code = heap_attrinfo_start (pcontext->thread_p, class_oid, -1, NULL, &attr_info);
      if (error_code != NO_ERROR)
	{
	  return NULL;
	}
      scan_cache->n_indexes = attr_info.last_classrepr->n_indexes;
      error_code =
	heap_init_func_pred_unpack_info (pcontext->thread_p, &attr_info, class_oid, &scan_cache->func_index_pred);
      heap_attrinfo_end (pcontext->thread_p, &attr_info);
      if (error_code != NO_ERROR)
	{
	  scan_cache->n_indexes = 0;
	  scan_cache->func_index_pred = NULL;
	  return NULL;
	}
    }

  return scan_cache;
}

/*
 * locator_area_op_to_pruning_type () - get pruning_type operation from
 *					locator area operation
 * return: pruning type
 * op (in)	    : locator operation
 */
static int
locator_area_op_to_pruning_type (LC_COPYAREA_OPERATION op)
{
  switch (op)
    {
    case LC_FLUSH_INSERT:
    case LC_FLUSH_UPDATE:
    case LC_FLUSH_DELETE:
      return DB_NOT_PARTITIONED_CLASS;

    case LC_FLUSH_INSERT_PRUNE:
    case LC_FLUSH_UPDATE_PRUNE:
      return DB_PARTITIONED_CLASS;

    case LC_FLUSH_INSERT_PRUNE_VERIFY:
    case LC_FLUSH_UPDATE_PRUNE_VERIFY:
      return DB_PARTITION_CLASS;

    default:
      assert (false);
      return 0;
    }
  return 0;
}

static int
locator_prefetch_index_page (THREAD_ENTRY * thread_p, OID * class_oid, RECDES * classrec, RECDES * recdes,
			     int btid_index, HEAP_CACHE_ATTRINFO * attr_info)
{
  int error = NO_ERROR;
  BTID *btid = NULL;

  btid = heap_indexinfo_get_btid (btid_index, attr_info);
  if (btid == NULL)
    {
      return ER_FAILED;
    }

  error = locator_prefetch_index_page_internal (thread_p, btid, class_oid, classrec, recdes);
  return error;
}

static int
locator_prefetch_index_page_internal (THREAD_ENTRY * thread_p, BTID * btid, OID * class_oid, RECDES * classrec,
				      RECDES * recdes)
{
  int error = NO_ERROR;
  BTREE_CHECKSCAN bt_checkscan, *bt_checkscan_p = NULL;
  HEAP_CACHE_ATTRINFO attr_info;
  HEAP_CACHE_ATTRINFO *attr_info_p = NULL;
  HFID hfid;
  int index_id = -1;
  char buf[DBVAL_BUFSIZE + MAX_ALIGNMENT], *aligned_buf;
  DB_VALUE dbvalue;
  DB_VALUE *key = NULL;
  KEY_VAL_RANGE key_val_range;
  INDX_SCAN_ID isid;
  BTID tmp_btid = *btid;

  aligned_buf = PTR_ALIGN (buf, MAX_ALIGNMENT);

  or_class_hfid (classrec, &hfid);
  if (HFID_IS_NULL (&hfid))
    {
      return NO_ERROR;
    }

  index_id = heap_attrinfo_start_with_btid (thread_p, class_oid, &tmp_btid, &attr_info);
  if (index_id < 0)
    {
      goto free_and_return;
    }
  attr_info_p = &attr_info;

  if (btree_keyoid_checkscan_start (thread_p, &tmp_btid, &bt_checkscan) != NO_ERROR)
    {
      error = ER_FAILED;
      goto free_and_return;
    }
  bt_checkscan_p = &bt_checkscan;

  BTREE_INIT_SCAN (&bt_checkscan_p->btree_scan);
  scan_init_index_scan (&isid, &bt_checkscan_p->oid_list, NULL);

  if (heap_attrinfo_read_dbvalues_without_oid (thread_p, recdes, attr_info_p) != NO_ERROR)
    {
      error = ER_FAILED;
      goto free_and_return;
    }

  key = heap_attrvalue_get_key (thread_p, index_id, attr_info_p, recdes, &tmp_btid, &dbvalue, aligned_buf, NULL, NULL);
  if (key == NULL)
    {
      error = ER_FAILED;
      goto free_and_return;
    }

  PR_SHARE_VALUE (key, &key_val_range.key1);
  PR_SHARE_VALUE (key, &key_val_range.key2);
  key_val_range.range = GE_LE;
  key_val_range.num_index_term = 0;

  btree_keyval_search (thread_p, &tmp_btid, S_SELECT, &bt_checkscan_p->btree_scan, &key_val_range, class_oid, NULL,
		       &isid, false);

  btree_scan_clear_key (&bt_checkscan.btree_scan);

free_and_return:
  if (key == &dbvalue)
    {
      pr_clear_value (key);
    }

  if (bt_checkscan_p)
    {
      btree_keyoid_checkscan_end (thread_p, bt_checkscan_p);
    }

  if (attr_info_p)
    {
      heap_attrinfo_end (thread_p, attr_info_p);
    }

  return error;
}

/*
 * locator_incr_num_transient_classnames - increase number
 *
 * return:
 *
 *   tran_index(in):
 *
 */
static void
locator_incr_num_transient_classnames (int tran_index)
{
  LOG_TDES *tdes;

  tdes = LOG_FIND_TDES (tran_index);
  /* ignore ER_LOG_UNKNOWN_TRANINDEX : It may be detected somewhere else. */
  if (tdes != NULL)
    {
      assert (tdes->num_transient_classnames >= 0);
      tdes->num_transient_classnames += 1;
    }
}

/*
 * locator_decr_num_transient_classnames - decrease number
 *
 * return:
 *
 *   tran_index(in):
 *
 * NOTE:
 */
static void
locator_decr_num_transient_classnames (int tran_index)
{
  LOG_TDES *tdes;

  tdes = LOG_FIND_TDES (tran_index);
  /* ignore ER_LOG_UNKNOWN_TRANINDEX : It may be detected somewhere else. */
  if (tdes != NULL)
    {
      tdes->num_transient_classnames -= 1;
      assert (tdes->num_transient_classnames >= 0);
    }
}

/*
 * locator_get_num_transient_classnames -
 *
 * return:
 *
 *   tran_index(in):
 *
 * NOTE:
 */
static int
locator_get_num_transient_classnames (int tran_index)
{
  LOG_TDES *tdes;

  tdes = LOG_FIND_TDES (tran_index);
  /* ignore ER_LOG_UNKNOWN_TRANINDEX : It may be detected somewhere else. */
  if (tdes != NULL)
    {
      assert (tdes->num_transient_classnames >= 0);
      return tdes->num_transient_classnames;
    }
  else
    {
      return 1;			/* someone exists */
    }
}

/*
 * locator_is_exist_class_name_entry -
 *
 * return:
 *
 *   entry(in):
 *
 * NOTE: debugging function
 */
static bool
locator_is_exist_class_name_entry (THREAD_ENTRY * thread_p, LOCATOR_CLASSNAME_ENTRY * entry)
{
#if !defined(NDEBUG)
  int check_own;

  check_own = csect_check_own (thread_p, CSECT_LOCATOR_SR_CLASSNAME_TABLE);
  assert (check_own >= 1);
#endif

  if (entry != NULL && entry->e_current.action == LC_CLASSNAME_EXIST)
    {
      assert (entry->e_name != NULL);
      assert (entry->e_tran_index == NULL_TRAN_INDEX);

      assert (!OID_ISNULL (&entry->e_current.oid));
      assert (heap_does_exist (thread_p, oid_Root_class_oid, &entry->e_current.oid));

      assert (LSA_ISNULL (&entry->e_current.savep_lsa));
      assert (entry->e_current.prev == NULL);

      return true;
    }

  return false;
}

/*
 * xchksum_insert_repl_log_and_demote_table_lock -
 *
 * return: error code
 *
 *   repl_info(in):
 *
 * NOTE: insert replication log and demote the read lock of the table
 */
int
xchksum_insert_repl_log_and_demote_table_lock (THREAD_ENTRY * thread_p, REPL_INFO * repl_info, const OID * class_oidp)
{
  LOG_TDES *tdes;
  int error = NO_ERROR;

  tdes = LOG_FIND_CURRENT_TDES (thread_p);
  if (tdes == NULL)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_UNKNOWN_TRANINDEX, 1,
	      LOG_FIND_THREAD_TRAN_INDEX (thread_p));

      return ER_LOG_UNKNOWN_TRANINDEX;
    }

  /* need to start a topop to make sure the repl log is inserted in a correct order */
  log_sysop_start (thread_p);

  repl_start_flush_mark (thread_p);

  error = xrepl_set_info (thread_p, repl_info);

  repl_end_flush_mark (thread_p, false);

  if (error != NO_ERROR)
    {
      ASSERT_ERROR ();
      log_sysop_abort (thread_p);
    }
  else
    {
      /* manually append repl info */
      log_append_repl_info (thread_p, tdes, false);

      log_sysop_commit (thread_p);
    }

#if defined (SERVER_MODE)
  /* demote S-lock to IS-lock to allow blocking writers to resume. This will not hurt transactional consistencies of
   * checksumdb. */
  lock_demote_read_class_lock_for_checksumdb (thread_p, tdes->tran_index, class_oidp);

  assert (lock_get_object_lock (class_oidp, oid_Root_class_oid, tdes->tran_index) == IS_LOCK);
#endif /* SERVER_MODE */

  return error;
}

/*
 * locator_rv_redo_rename () - Dummy logical redo function that does nothing.
 *
 * return	 : NO_ERROR
 * thread_p (in) : Thread entry.
 * rcv (in)	 : Recovery data.
 */
int
locator_rv_redo_rename (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  /* Does nothing on recovery. */
  return NO_ERROR;
}

/*
 * redistribute_partition_data () - redistribute partition data
 *
 * return : error code
 *
 * thread_p (in)      :
 * class_oid (in)     : parent class OID
 * no_oids (in)	      : number of OIDs in the list (promoted partitions)
 * oid_list (in)      : partition OID list (promoted partitions)
 */
static int
redistribute_partition_data (THREAD_ENTRY * thread_p, OID * class_oid, int no_oids, OID * oid_list)
{
  int error = NO_ERROR;
  int i = 0, j = 0;
  RECDES recdes;
  HFID hfid, class_hfid;
  OID inst_oid;
  SCAN_CODE scan = S_SUCCESS;
  VPID vpid;
  PGBUF_WATCHER old_page_watcher;
  PGSLOTID slotid;
  HEAP_SCANCACHE parent_scan_cache, scan_cache;
  PRUNING_CONTEXT pcontext;
  bool is_scan_end = false;
  bool is_parent_scancache_started = false;
  bool is_part_scancache_started = false;
  bool is_pcontext_inited = false;
  int force_count = -1;
  OID oid;
  OID cls_oid;
  LOG_TDES *tdes = LOG_FIND_CURRENT_TDES (thread_p);
  MVCCID threshold_mvccid = MVCCID_NULL;

  assert (tdes != NULL);

  PGBUF_INIT_WATCHER (&old_page_watcher, PGBUF_ORDERED_RANK_UNDEFINED, PGBUF_ORDERED_NULL_HFID);

  error = heap_get_hfid_from_class_oid (thread_p, class_oid, &class_hfid);
  if (error != NO_ERROR || HFID_IS_NULL (&class_hfid))
    {
      error = ER_FAILED;
      goto exit;
    }

  /* start scan cache for insert on parent class */
  error = heap_scancache_start_modify (thread_p, &parent_scan_cache, &class_hfid, class_oid, SINGLE_ROW_INSERT, NULL);
  if (error != NO_ERROR)
    {
      goto exit;
    }
  is_parent_scancache_started = true;

  /* initialize pruning context for parent class */
  (void) partition_init_pruning_context (&pcontext);
  error = partition_load_pruning_context (thread_p, class_oid, DB_PARTITIONED_CLASS, &pcontext);
  if (error != NO_ERROR)
    {
      goto exit;
    }
  is_pcontext_inited = true;

  recdes.data = NULL;

  for (i = 0; i < no_oids; i++)
    {
      if (OID_ISNULL (&oid_list[i]))
	{
	  goto exit;
	}

      error = heap_get_hfid_from_class_oid (thread_p, &oid_list[i], &hfid);
      if (error != NO_ERROR || HFID_IS_NULL (&hfid))
	{
	  error = ER_FAILED;
	  goto exit;
	}

      PGBUF_INIT_WATCHER (&old_page_watcher, PGBUF_ORDERED_HEAP_NORMAL, &hfid);

      error = heap_scancache_start (thread_p, &scan_cache, &hfid, &oid_list[i], false, false, NULL);
      if (error != NO_ERROR)
	{
	  goto exit;
	}
      is_part_scancache_started = true;

      if (tdes->block_global_oldest_active_until_commit == false)
	{
	  /* do not allow to advance with vacuum_Global_oldest_active_mvccid */
	  ATOMIC_INC_32 (&vacuum_Global_oldest_active_blockers_counter, 1);
	  tdes->block_global_oldest_active_until_commit = true;
	}
      else
	{
	  assert (vacuum_Global_oldest_active_blockers_counter > 0);
	}

      if (threshold_mvccid == MVCCID_NULL)
	{
	  /* Can't use vacuum_Global_oldest_active_mvccid here. That's because we want to avoid scenarios where VACUUM
	   * compute oldest active mvccid, but didn't set yet vacuum_Global_oldest_active_mvccid, current transaction
	   * uses the old value of vacuum_Global_oldest_active_mvccid, then VACUUM uses updated value of
	   * vacuum_Global_oldest_active_mvccid.
	   * In such scenario, VACUUM can remove heap records that can't be removed by the current thread. */
	  threshold_mvccid = logtb_get_oldest_active_mvccid (thread_p);
	}

      /* VACUUM all cleanable heap objects before upgrading the domain */
      error = heap_vacuum_all_objects (thread_p, &scan_cache, threshold_mvccid);
      if (error != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit;
	}

      /* start with first OID of the first page */
      vpid.volid = hfid.vfid.volid;
      vpid.pageid = hfid.hpgid;
      is_scan_end = false;
      while (!is_scan_end)
	{
	  if (scan_cache.page_watcher.pgptr == NULL)
	    {
	      error =
		pgbuf_ordered_fix (thread_p, &vpid, OLD_PAGE_PREVENT_DEALLOC, PGBUF_LATCH_WRITE,
				   &scan_cache.page_watcher);
	      if (old_page_watcher.pgptr != NULL)
		{
		  pgbuf_ordered_unfix (thread_p, &old_page_watcher);
		}
	      if (error != NO_ERROR)
		{
		  goto exit;
		}
	    }

	  slotid = 0;
	  while (true)
	    {
	      /* get next record */
	      scan = spage_next_record (scan_cache.page_watcher.pgptr, &slotid, &recdes, PEEK);
	      if (scan == S_ERROR)
		{
		  error = ER_FAILED;
		  goto exit;
		}
	      else if (scan == S_END)
		{
		  /* move to next page */
		  error = heap_vpid_next (&hfid, scan_cache.page_watcher.pgptr, &vpid);
		  if (error != NO_ERROR)
		    {
		      goto exit;
		    }

		  /* keep latch on current page until the next page is fixed */
		  pgbuf_replace_watcher (thread_p, &scan_cache.page_watcher, &old_page_watcher);

		  if (VPID_ISNULL (&vpid))
		    {
		      /* no more pages in the current heap file */
		      is_scan_end = true;
		    }
		  break;
		}

	      if (slotid == HEAP_HEADER_AND_CHAIN_SLOTID)
		{
		  /* skip the header */
		  continue;
		}

	      if (recdes.type != REC_HOME && recdes.type != REC_BIGONE && recdes.type != REC_RELOCATION)
		{
		  continue;
		}

	      inst_oid.pageid = vpid.pageid;
	      inst_oid.volid = vpid.volid;
	      inst_oid.slotid = slotid;

	      recdes.data = NULL;

	      if (heap_get_visible_version (thread_p, &inst_oid, class_oid, &recdes, &scan_cache, COPY, NULL_CHN) !=
		  S_SUCCESS)
		{
		  error = ER_FAILED;
		  goto exit;
		}

	      /* make sure that pruning does not change the given class OID */
	      COPY_OID (&cls_oid, class_oid);
	      error =
		locator_insert_force (thread_p, &class_hfid, &cls_oid, &oid, &recdes, true, SINGLE_ROW_INSERT,
				      &parent_scan_cache, &force_count, DB_PARTITIONED_CLASS, &pcontext, NULL,
				      UPDATE_INPLACE_OLD_MVCCID);
	      if (error != NO_ERROR)
		{
		  goto exit;
		}
	    }
	}

      if (old_page_watcher.pgptr != NULL)
	{
	  pgbuf_ordered_unfix (thread_p, &old_page_watcher);
	}
      if (is_part_scancache_started == true)
	{
	  (void) heap_scancache_end (thread_p, &scan_cache);
	  is_part_scancache_started = false;
	}
    }

exit:
  if (old_page_watcher.pgptr != NULL)
    {
      pgbuf_ordered_unfix (thread_p, &old_page_watcher);
    }
  if (scan_cache.page_watcher.pgptr != NULL)
    {
      pgbuf_ordered_unfix (thread_p, &scan_cache.page_watcher);
    }
  if (is_parent_scancache_started == true)
    {
      (void) heap_scancache_end (thread_p, &parent_scan_cache);
    }
  if (is_part_scancache_started == true)
    {
      (void) heap_scancache_end (thread_p, &scan_cache);
    }
  if (is_pcontext_inited == true)
    {
      partition_clear_pruning_context (&pcontext);
    }

  return error;
}

/*
 * xlocator_redistribute_partition_data () - redistribute partition data
 *
 * return : error code
 *
 * thread_p (in)      :
 * class_oid (in)     : parent class OID
 * no_oids (in)	      : number of OIDs in the list (promoted partitions)
 * oid_list (in)      : partition OID list (promoted partitions)
 */
int
xlocator_redistribute_partition_data (THREAD_ENTRY * thread_p, OID * class_oid, int no_oids, OID * oid_list)
{
  return redistribute_partition_data (thread_p, class_oid, no_oids, oid_list);
}

/*
 * locator_lock_and_get_object_internal () - Internal function: aquire lock and return object
 *
 * return : scan code
 * thread_p (in)   : 
 * context (in/out): Heap get context .
 * lock_mode (in)  : Type of lock.
 *
 * NOTE: Caller must handle the cleanup of context
 */
static SCAN_CODE
locator_lock_and_get_object_internal (THREAD_ENTRY * thread_p, HEAP_GET_CONTEXT * context, LOCK lock_mode)
{
  SCAN_CODE scan = S_SUCCESS;
  bool lock_acquired = false;

  assert (context != NULL);
  assert (context->oid_p != NULL && !OID_ISNULL (context->oid_p));
  assert (context->class_oid_p != NULL && !OID_ISNULL (context->class_oid_p));
  assert (lock_mode > NULL_LOCK);	/* this is not the appropriate function for NULL_LOCK */
  assert (context->scan_cache != NULL);

  /* try to lock the object conditionally, if it fails unfix page watchers and try unconditionally */

  if (lock_object (thread_p, context->oid_p, context->class_oid_p, lock_mode, LK_COND_LOCK) != LK_GRANTED)
    {
      if (context->scan_cache && context->scan_cache->cache_last_fix_page && context->home_page_watcher.pgptr != NULL)
	{
	  /* prevent caching home page watcher in scan_cache */
	  pgbuf_ordered_unfix (thread_p, &context->home_page_watcher);
	}
      heap_clean_get_context (thread_p, context);
      if (lock_object (thread_p, context->oid_p, context->class_oid_p, lock_mode, LK_UNCOND_LOCK) != LK_GRANTED)
	{
	  goto error;
	}

      lock_acquired = true;

      /* Prepare for getting record again. Since pages have been unlatched, others may have changed them */
      scan = heap_prepare_get_context (thread_p, context, PGBUF_LATCH_READ, false, LOG_WARNING_IF_DELETED);
      if (scan != S_SUCCESS)
	{
	  goto error;
	}
    }
  else
    {
      lock_acquired = true;
    }

  assert (OID_IS_ROOTOID (context->class_oid_p) || lock_mode == S_LOCK || lock_mode == X_LOCK);

  /* Lock should be aquired now -> get recdes */
  if (context->recdes_p != NULL)
    {
      scan = heap_get_last_version (thread_p, context);
      /* this scan_code must be preserved until the end of this function to be returned; - unless an error occur */
      if (scan != S_SUCCESS && scan != S_SUCCESS_CHN_UPTODATE)
	{
	  goto error;
	}
    }

  /* Check isolation restrictions and the visibility of the object if it belongs to a mvcc class */
  if (!mvcc_is_mvcc_disabled_class (context->class_oid_p))
    {
      MVCC_REC_HEADER recdes_header;

      /* get header: directly from recdes if it has been obtained, otherwise from heap */
      if (context->recdes_p == NULL || scan == S_SUCCESS_CHN_UPTODATE)
	{
	  /* ensure context is prepared to get header of the record */
	  if (heap_prepare_get_context (thread_p, context, PGBUF_LATCH_READ, false, LOG_WARNING_IF_DELETED) !=
	      S_SUCCESS)
	    {
	      scan = S_ERROR;
	      goto error;
	    }
	  if (heap_get_mvcc_header (thread_p, context, &recdes_header) != S_SUCCESS)
	    {
	      scan = S_ERROR;
	      goto error;
	    }
	}
      else if (or_mvcc_get_header (context->recdes_p, &recdes_header) != NO_ERROR)
	{
	  goto error;
	}

      /* Check REPEATABLE READ/SERIALIZABLE isolation restrictions. */
      if (logtb_find_current_isolation (thread_p) > TRAN_READ_COMMITTED
	  && logtb_check_class_for_rr_isolation_err (context->class_oid_p))
	{
	  /* In these isolation levels, the transaction is not allowed to modify an object that was already
	   * modified by other transactions. This would be true if last version matched the visible version.
	   *
	   * TODO: We already know here that this last row version is not deleted. It would be enough to just
	   * check whether the insert MVCCID is considered active relatively to transaction's snapshot.
	   */
	  MVCC_SNAPSHOT *tran_snapshot = logtb_get_mvcc_snapshot (thread_p);
	  MVCC_SATISFIES_SNAPSHOT_RESULT snapshot_res;

	  assert (tran_snapshot != NULL && tran_snapshot->snapshot_fnc != NULL);
	  snapshot_res = tran_snapshot->snapshot_fnc (thread_p, &recdes_header, tran_snapshot);
	  if (snapshot_res == TOO_OLD_FOR_SNAPSHOT)
	    {
	      /* Not visible. */
	      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_HEAP_UNKNOWN_OBJECT, 3, context->oid_p->volid,
		      context->oid_p->pageid, context->oid_p->slotid);
	      scan = S_DOESNT_EXIST;
	      goto error;
	    }
	  else if (snapshot_res == TOO_NEW_FOR_SNAPSHOT)
	    {
	      /* Trying to modify a version already modified by concurrent transaction, which is an isolation conflict.
	       */
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_MVCC_SERIALIZABLE_CONFLICT, 0);
	      goto error;
	    }
	  else if (MVCC_IS_HEADER_DELID_VALID (&recdes_header))
	    {
	      /* Trying to modify version deleted by concurrent transaction, which is an isolation conflict. */
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_MVCC_SERIALIZABLE_CONFLICT, 0);
	      goto error;
	    }
	  else
	    {
	      /* Last version is also visible version and it is not deleted. Fall through. */
	    }
	}

      if (MVCC_IS_HEADER_DELID_VALID (&recdes_header))
	{
	  scan = S_DOESNT_EXIST;
	  goto error;
	}
    }

  return scan;

error:

  if (scan == S_ERROR)
    {
      /* Caller should handle error setting for other scan codes e.g. S_DOESNT_EXIST, S_DOESNT_FIT. */
      ASSERT_ERROR ();
    }

  if (lock_acquired)
    {
      lock_unlock_object_donot_move_to_non2pl (thread_p, context->oid_p, context->class_oid_p, lock_mode);
    }

  return (scan != S_SUCCESS && scan != S_SUCCESS_CHN_UPTODATE) ? scan : S_ERROR;
}

/*
 * locator_lock_and_get_object_with_evaluation () - Get MVCC object version for delete/update and check reevaluation.
 *
 * return	       : SCAN_CODE.
 * thread_p (in)       : Thread entry.
 * oid (in)	       : Object OID.
 * class_oid (in)      : Class OID.
 * recdes (out)	       : Record descriptor.
 * scan_cache (in)     : Heap scan cache.
 * ispeeking (in)      : PEEK or COPY.
 * old_chn (in)	       : CHN of known record data.
 * mvcc_reev_data (in) : MVCC reevaluation data.
 * (obsolete) non_ex_handling_type (in): - LOG_ERROR_IF_DELETED: write the
 *				ER_HEAP_UNKNOWN_OBJECT error to log
 *                            - LOG_WARNING_IF_DELETED: set only warning
 * 
 * Note: This function will lock the object with X_LOCK. This lock type should correspond to delete/update operations.
 */
SCAN_CODE
locator_lock_and_get_object_with_evaluation (THREAD_ENTRY * thread_p, OID * oid, OID * class_oid, RECDES * recdes,
					     HEAP_SCANCACHE * scan_cache, int ispeeking, int old_chn,
					     MVCC_REEV_DATA * mvcc_reev_data,
					     NON_EXISTENT_HANDLING non_ex_handling_type)
{
  HEAP_GET_CONTEXT context;
  SCAN_CODE scan;
  RECDES recdes_local = RECDES_INITIALIZER;
  MVCC_REC_HEADER mvcc_header;
  DB_LOGICAL ev_res;		/* Re-evaluation result. */
  OID class_oid_local = OID_INITIALIZER;
  LOCK lock_mode = X_LOCK;
  int err;

  if (recdes == NULL && mvcc_reev_data != NULL)
    {
      /* peek if only for reevaluation */
      recdes = &recdes_local;
      ispeeking = PEEK;
      old_chn = NULL_CHN;
    }

  if (class_oid == NULL)
    {
      class_oid = &class_oid_local;
    }

  if (scan_cache && ispeeking == COPY && recdes != NULL)
    {
      /* Allocate an area to hold the object. Assume that the object will fit in two pages for not better estimates. */
      if (heap_scan_cache_allocate_area (thread_p, scan_cache, DB_PAGESIZE * 2) != NO_ERROR)
	{
	  return S_ERROR;
	}
    }
  heap_init_get_context (thread_p, &context, oid, class_oid, recdes, scan_cache, ispeeking, old_chn);

  /* get class_oid if it is unknown */
  if (OID_ISNULL (class_oid))
    {
      err = heap_prepare_object_page (thread_p, oid, &context.home_page_watcher, PGBUF_LATCH_READ);
      if (err != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  heap_clean_get_context (thread_p, &context);

	  /* for non existent object, return S_DOESNT_EXIST and let the caller handle the case */
	  return err == ER_HEAP_UNKNOWN_OBJECT ? S_DOESNT_EXIST : S_ERROR;
	}
      if (heap_get_class_oid_from_page (thread_p, context.home_page_watcher.pgptr, class_oid) != NO_ERROR)
	{
	  heap_clean_get_context (thread_p, &context);
	  return S_DOESNT_EXIST;
	}
    }

  scan = locator_lock_and_get_object_internal (thread_p, &context, lock_mode);

  /* perform reevaluation */
  if (mvcc_reev_data != NULL && (scan == S_SUCCESS || scan == S_SUCCESS_CHN_UPTODATE))
    {
      if (scan == S_SUCCESS_CHN_UPTODATE)
	{
	  /* PEEK record */
	  scan = heap_get_record_data_when_all_ready (thread_p, &context);
	  assert (scan == S_SUCCESS);
	}

      if (or_mvcc_get_header (recdes, &mvcc_header) != NO_ERROR)
	{
	  scan = S_ERROR;
	  goto exit;
	}

      if (scan_cache->mvcc_snapshot)
	{
	  MVCC_SATISFIES_SNAPSHOT_RESULT snapshot_res;

	  snapshot_res = scan_cache->mvcc_snapshot->snapshot_fnc (thread_p, &mvcc_header, scan_cache->mvcc_snapshot);
	  if (snapshot_res == SNAPSHOT_SATISFIED)
	    {
	      /* Skip the re-evaluation if last version is visible. It should be the same as the visible version
	       * which was already evaluated. */
	      goto exit;
	    }
	}
      ev_res = locator_mvcc_reev_cond_and_assignment (thread_p, scan_cache, mvcc_reev_data, &mvcc_header, oid, recdes);
      if (ev_res != V_TRUE)
	{
	  /* did not pass the evaluation or error occurred - unlock object */
	  lock_unlock_object_donot_move_to_non2pl (thread_p, oid, class_oid, lock_mode);
	}
      switch (ev_res)
	{
	case V_TRUE:
	  /* Object was locked and passed re-evaluation. Get record. */
	  goto exit;
	case V_ERROR:
	  /* Error. */
	  assert (er_errid () != NO_ERROR);
	  scan = S_ERROR;
	  goto exit;
	case V_FALSE:
	case V_UNKNOWN:
	  /* Record didn't pass re-evaluation. Return S_SUCCESS and let the caller handle the case. */
	  goto exit;
	default:
	  /* Unhandled. */
	  assert_release (false);
	  scan = S_ERROR;
	  goto exit;
	}

    }

exit:
  heap_clean_get_context (thread_p, &context);
  return scan;
}

/*
 * locator_get_object () - Retrieve heap objects and decide the type of lock according to the operation type
 *
 * return	  : Scan code.
 * thread_p (in)  : Thread entry.
 * oid (in)       : Object identifier.
 * class_oid (in) : Class oid.
 * recdes (out)	  : Record descriptor.
 * scan_cache (in): Scan cache.
 * op_type (in)	  : Requested type of operation.
 * lock_mode (in) : Lock type, see note.
 * ispeeking (in) : Peek record or copy.
 *
 * Note: This function should be used when class_oid is unknown, which is required to decide lock_mode;
 *       When lock_mode is known, a more appropriate function is recommended.
 *
 *	 op_type and lock_mode exclude each other according to class type:
 *	    - root class    : lock_mode is considered, op_type is ignored
 *	    - instance class: op_type is considered, a new lock_mode is used (according to op_type)
 */
SCAN_CODE
locator_get_object (THREAD_ENTRY * thread_p, const OID * oid, OID * class_oid, RECDES * recdes,
		    HEAP_SCANCACHE * scan_cache, SCAN_OPERATION_TYPE op_type, LOCK lock_mode, int ispeeking, int chn)
{
  SCAN_CODE scan_code;
  OID class_oid_local = OID_INITIALIZER;
  HEAP_GET_CONTEXT context;
  int err;

  /* decide the type of lock before anything */

  assert (oid != NULL && !OID_ISNULL (oid));

  if (class_oid == NULL)
    {
      /* mandatory to decide the lock type */
      class_oid = &class_oid_local;
    }

  if (scan_cache && ispeeking == COPY && recdes != NULL)
    {
      /* Allocate an area to hold the object. Assume that the object will fit in two pages for not better estimates. */
      if (heap_scan_cache_allocate_area (thread_p, scan_cache, DB_PAGESIZE * 2) != NO_ERROR)
	{
	  return S_ERROR;
	}
    }

  heap_init_get_context (thread_p, &context, oid, class_oid, recdes, scan_cache, ispeeking, chn);

  /* get class_oid if it is unknown */
  if (OID_ISNULL (class_oid))
    {
      err = heap_prepare_object_page (thread_p, oid, &context.home_page_watcher, PGBUF_LATCH_READ);
      if (err != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  heap_clean_get_context (thread_p, &context);

	  /* for non existent object, return S_DOESNT_EXIST and let the caller handle the case */
	  return err == ER_HEAP_UNKNOWN_OBJECT ? S_DOESNT_EXIST : S_ERROR;
	}
      if (heap_get_class_oid_from_page (thread_p, context.home_page_watcher.pgptr, class_oid) != NO_ERROR)
	{
	  heap_clean_get_context (thread_p, &context);
	  return S_DOESNT_EXIST;
	}
    }

  /* decide lock_mode according to class_oid and op_type */
  if (!OID_IS_ROOTOID (class_oid))
    {
      if (op_type == S_SELECT && !mvcc_is_mvcc_disabled_class (class_oid))
	{
	  /* S_SELECT and mvcc class */
	  lock_mode = NULL_LOCK;
	}
      else if (op_type == S_DELETE || op_type == S_UPDATE)
	{
	  assert (lock_mode > S_LOCK);
	  lock_mode = X_LOCK;
	}
      else
	{
	  /* S_SELECT and non-mvcc class || S_SELECT_WITH_LOCK */
	  assert (op_type == S_SELECT || op_type == S_SELECT_WITH_LOCK);
	  if (lock_mode > S_LOCK)
	    {
	      assert (false);
	      lock_mode = X_LOCK;
	    }
	  else
	    {
	      lock_mode = S_LOCK;
	    }
	}
    }

  if (op_type == S_SELECT && lock_mode == NULL_LOCK)
    {
      /* No locking */
      scan_code = heap_get_visible_version_internal (thread_p, &context, false);
    }
  else
    {
      /* Locking */
      scan_code = locator_lock_and_get_object_internal (thread_p, &context, lock_mode);
    }

  heap_clean_get_context (thread_p, &context);

  return scan_code;
}


/*
 * locator_lock_and_get_object () - Get MVCC object version for delete/update.
 *
 * return	       : SCAN_CODE.
 * thread_p (in)       : Thread entry.
 * oid (in)	       : Object OID.
 * class_oid (in)      : Class OID.
 * recdes (out)	       : Record descriptor.
 * scan_cache (in)     : Heap scan cache.
 * ispeeking (in)      : PEEK or COPY.
 * old_chn (in)	       : CHN of known record data.
 * mvcc_reev_data (in) : MVCC reevaluation data.
 * (obsolete) non_ex_handling_type (in): - LOG_ERROR_IF_DELETED: write the
 *				ER_HEAP_UNKNOWN_OBJECT error to log
 *                            - LOG_WARNING_IF_DELETED: set only warning
 */
SCAN_CODE
locator_lock_and_get_object (THREAD_ENTRY * thread_p, const OID * oid, OID * class_oid, RECDES * recdes,
			     HEAP_SCANCACHE * scan_cache, LOCK lock, int ispeeking, int old_chn,
			     NON_EXISTENT_HANDLING non_ex_handling_type)
{
  HEAP_GET_CONTEXT context;
  SCAN_CODE scan_code;

  if (scan_cache && ispeeking == COPY && recdes != NULL)
    {
      /* Allocate an area to hold the object. Assume that the object will fit in two pages for not better estimates. */
      if (heap_scan_cache_allocate_area (thread_p, scan_cache, DB_PAGESIZE * 2) != NO_ERROR)
	{
	  return S_ERROR;
	}
    }

  heap_init_get_context (thread_p, &context, oid, class_oid, recdes, scan_cache, ispeeking, old_chn);
  scan_code = locator_lock_and_get_object_internal (thread_p, &context, lock);
  heap_clean_get_context (thread_p, &context);
  return scan_code;
}

/*
 * locator_mvcc_reev_cond_and_assignment () -
 *
 *   return: DB_LOGICAL
 *   thread_p(in): thread entry
 *   scan_cache(in):
 *   mvcc_reev_data_p(in/out):
 *   mvcc_header_p(in):
 *   curr_row_version_oid_p(in):
 *   recdes(in):
 */
/* TODO: We need to reevaluate relation between primary key * and foreign key. */
static DB_LOGICAL
locator_mvcc_reev_cond_and_assignment (THREAD_ENTRY * thread_p, HEAP_SCANCACHE * scan_cache,
				       MVCC_REEV_DATA * mvcc_reev_data_p, MVCC_REC_HEADER * mvcc_header_p,
				       const OID * curr_row_version_oid_p, RECDES * recdes)
{
  bool ev_res = V_TRUE;

  if (mvcc_reev_data_p == NULL)
    {
      return ev_res;
    }

  assert (mvcc_header_p != NULL && curr_row_version_oid_p != NULL);

  ev_res = V_TRUE;
  if (!MVCC_IS_REC_INSERTED_BY_ME (thread_p, mvcc_header_p))
    {
      switch (mvcc_reev_data_p->type)
	{
	case REEV_DATA_SCAN:
	  ev_res = locator_mvcc_reevaluate_filters (thread_p, mvcc_reev_data_p->select_reev_data,
						    curr_row_version_oid_p, recdes);
	  mvcc_reev_data_p->filter_result = ev_res;
	  break;

	case REEV_DATA_UPDDEL:
	  ev_res =
	    locator_mvcc_reev_cond_assigns (thread_p, &scan_cache->node.class_oid, curr_row_version_oid_p, scan_cache,
					    recdes, mvcc_reev_data_p->upddel_reev_data);
	  mvcc_reev_data_p->filter_result = ev_res;
	  break;

	default:
	  break;
	}
    }
  else
    {
      mvcc_reev_data_p->filter_result = V_TRUE;
    }

  return ev_res;
}

/*
 * locator_mvcc_reev_cond_assigns () - reevaluates conditions and assignments
 *				    at update/delete stage of an UPDATE/DELETE
 *				    statement
 *   return: result of reevaluation
 *   thread_p(in): thread entry
 *   class_oid(in): OID of the class that triggered reevaluation
 *   oid(in) : The OID of the latest version of record that triggered
 *	       reevaluation
 *   scan_cache(in): scan_cache
 *   recdes(in): Record descriptor that will contain the updated/deleted record
 *   mvcc_reev_data(in): The structure that contains data needed for
 *			 reevaluation
 *
 *  Note: the current transaction already acquired X-LOCK on oid parameter
 *	    before calling this function. If the condition returns false then
 *	    the lock must be released by the caller.
 *	  The function reevaluates entire condition: key range, key filter and
 *	    data filter.
 *	  This function allocates memory for recdes and deallocates it if it
 *	    was already allocated for previous reevaluated record. After last
 *	    reevaluation this memory must be deallocated by one of its callers
 *	    (e.g. qexec_execute_update).
 */
static DB_LOGICAL
locator_mvcc_reev_cond_assigns (THREAD_ENTRY * thread_p, OID * class_oid, const OID * oid, HEAP_SCANCACHE * scan_cache,
				RECDES * recdes, MVCC_UPDDEL_REEV_DATA * mvcc_reev_data)
{
  DB_LOGICAL ev_res = V_TRUE;
  UPDDEL_MVCC_COND_REEVAL *mvcc_cond_reeval = NULL;
  int idx;

  /* reevaluate condition for each class involved into */
  for (mvcc_cond_reeval = mvcc_reev_data->mvcc_cond_reev_list; mvcc_cond_reeval != NULL;
       mvcc_cond_reeval = mvcc_cond_reeval->next)
    {
      ev_res =
	locator_mvcc_reeval_scan_filters (thread_p, oid, scan_cache, recdes, mvcc_cond_reeval,
					  mvcc_reev_data->curr_upddel == mvcc_cond_reeval);
      if (ev_res != V_TRUE)
	{
	  goto end;
	}
    }

  if (mvcc_reev_data->new_recdes == NULL)
    {
      /* Seems that the caller wants to reevaluate only the condition */
      goto end;
    }

  /* reload data from classes involved only in right side of assignments (not in condition) */
  if (mvcc_reev_data->curr_extra_assign_reev != NULL)
    {
      for (idx = 0; idx < mvcc_reev_data->curr_extra_assign_cnt; idx++)
	{
	  mvcc_cond_reeval = mvcc_reev_data->curr_extra_assign_reev[idx];
	  ev_res = locator_mvcc_reeval_scan_filters (thread_p, oid, scan_cache, recdes, mvcc_cond_reeval, false);
	  if (ev_res != V_TRUE)
	    {
	      goto end;
	    }
	}
    }

  /* after reevaluation perform assignments */
  if (mvcc_reev_data->curr_assigns != NULL)
    {
      UPDATE_MVCC_REEV_ASSIGNMENT *assign = mvcc_reev_data->curr_assigns;
      int rc;
      DB_VALUE *dbval = NULL;

      if (heap_attrinfo_clear_dbvalues (mvcc_reev_data->curr_attrinfo) != NO_ERROR)
	{
	  ev_res = V_ERROR;
	  goto end;
	}
      for (; assign != NULL; assign = assign->next)
	{
	  if (assign->constant != NULL)
	    {
	      rc = heap_attrinfo_set (oid, assign->att_id, assign->constant, mvcc_reev_data->curr_attrinfo);
	    }
	  else
	    {
	      if (fetch_peek_dbval (thread_p, assign->regu_right, mvcc_reev_data->vd, (OID *) class_oid, (OID *) oid,
				    NULL, &dbval) != NO_ERROR)
		{
		  ev_res = V_ERROR;
		  goto end;
		}
	      rc = heap_attrinfo_set (oid, assign->att_id, dbval, mvcc_reev_data->curr_attrinfo);
	      if (dbval->need_clear)
		{
		  pr_clear_value (dbval);
		}
	    }
	  if (rc != NO_ERROR)
	    {
	      ev_res = V_ERROR;
	      goto end;
	    }
	}

      /* TO DO - reuse already allocated area */
      if (mvcc_reev_data->copyarea != NULL)
	{
	  locator_free_copy_area (mvcc_reev_data->copyarea);
	  mvcc_reev_data->new_recdes->data = NULL;
	  mvcc_reev_data->new_recdes->area_size = 0;
	}
      mvcc_reev_data->copyarea =
	locator_allocate_copy_area_by_attr_info (thread_p, mvcc_reev_data->curr_attrinfo, recdes,
						 mvcc_reev_data->new_recdes, -1, LOB_FLAG_INCLUDE_LOB);
      if (mvcc_reev_data->copyarea == NULL)
	{
	  ev_res = V_ERROR;
	  goto end;
	}
    }

end:

  return ev_res;
}

/*
 * locator_mvcc_reeval_scan_filters () - reevaluates conditions for a scan table
 *				    at update/delete stage of an UPDATE/DELETE
 *				    statement
 *   return: result of reevaluation
 *   thread_p(in): thread entry
 *   oid(in) : The OID of the latest version of record that triggered
 *	       reevaluation
 *   scan_cache(in): scan_cache
 *   recdes(in): Record descriptor of the record to be updated/deleted.
 *   mvcc_cond_reeval(in): The structure that contains data needed for
 *			   reevaluation
 *   is_upddel(in): true if current scan is updated/deleted when reevaluation
 *		    occured.
 *
 *  Note: The current transaction already acquired X-LOCK on oid parameter
 *	    before calling this function. If the condition returns false then
 *	    the lock must be released by the caller.
 *	  The function reevaluates entire condition: key range, key filter and
 *	    data filter.
 */
static DB_LOGICAL
locator_mvcc_reeval_scan_filters (THREAD_ENTRY * thread_p, const OID * oid, HEAP_SCANCACHE * scan_cache,
				  RECDES * recdes, UPDDEL_MVCC_COND_REEVAL * mvcc_cond_reeval, bool is_upddel)
{
  OID *cls_oid = NULL;
  const OID *oid_inst = NULL;
  MVCC_SCAN_REEV_DATA scan_reev;
  RECDES temp_recdes, *recdesp = NULL;
  HEAP_SCANCACHE local_scan_cache;
  bool scan_cache_inited = false;
  SCAN_CODE scan_code;
  DB_LOGICAL ev_res = V_TRUE;

  cls_oid = &mvcc_cond_reeval->cls_oid;
  if (!is_upddel)
    {
      /* the class is different than the class to be updated/deleted, so use the latest version of row */
      recdesp = &temp_recdes;
      oid_inst = oid;
      if (heap_scancache_quick_start_with_class_hfid (thread_p, &local_scan_cache, &scan_cache->node.hfid) != NO_ERROR)
	{
	  ev_res = V_ERROR;
	  goto end;
	}
      scan_cache_inited = true;
      scan_code = heap_get_visible_version (thread_p, oid_inst, NULL, recdesp, &local_scan_cache, PEEK, NULL_CHN);
      if (scan_code != S_SUCCESS)
	{
	  ev_res = V_ERROR;
	  goto end;
	}
    }
  else
    {
      /* the class to be updated/deleted */
      recdesp = recdes;
      oid_inst = mvcc_cond_reeval->inst_oid;
    }

  if (mvcc_cond_reeval->rest_attrs->num_attrs != 0)
    {
      if (heap_attrinfo_read_dbvalues (thread_p, oid_inst, recdesp, NULL, mvcc_cond_reeval->rest_attrs->attr_cache) !=
	  NO_ERROR)
	{
	  ev_res = V_ERROR;
	  goto end;
	}

      if (fetch_val_list (thread_p, mvcc_cond_reeval->rest_regu_list, NULL, cls_oid, (OID *) oid_inst, NULL, PEEK)
	  != NO_ERROR)
	{
	  ev_res = V_ERROR;
	  goto end;
	}
    }

  if (mvcc_cond_reeval->range_filter.scan_pred != NULL || mvcc_cond_reeval->key_filter.scan_pred != NULL
      || mvcc_cond_reeval->data_filter.scan_pred != NULL)
    {
      /* evaluate conditions */
      scan_reev.range_filter = &mvcc_cond_reeval->range_filter;
      scan_reev.key_filter = &mvcc_cond_reeval->key_filter;
      scan_reev.data_filter = &mvcc_cond_reeval->data_filter;
      scan_reev.qualification = &mvcc_cond_reeval->qualification;
      ev_res = locator_mvcc_reevaluate_filters (thread_p, &scan_reev, oid_inst, recdesp);
    }

end:
  if (scan_cache_inited)
    {
      heap_scancache_end (thread_p, &local_scan_cache);
    }

  return ev_res;
}

/*
 * locator_mvcc_reevaluate_filters () - reevaluates key range, key filter and data
 *				filter predicates
 *   return: result of reevaluation
 *   thread_p(in): thread entry
 *   mvcc_reev_data(in): The structure that contains data needed for
 *			 reevaluation
 *   oid(in) : The record that was modified by other transactions and is
 *	       involved in filters.
 *   recdes(in): Record descriptor that will contain the record
 */
static DB_LOGICAL
locator_mvcc_reevaluate_filters (THREAD_ENTRY * thread_p, MVCC_SCAN_REEV_DATA * mvcc_reev_data, const OID * oid,
				 RECDES * recdes)
{
  FILTER_INFO *filter;
  DB_LOGICAL ev_res = V_TRUE;

  filter = mvcc_reev_data->range_filter;
  if (filter != NULL && filter->scan_pred != NULL && filter->scan_pred->pred_expr != NULL)
    {
      if (heap_attrinfo_read_dbvalues (thread_p, oid, recdes, NULL, filter->scan_attrs->attr_cache) != NO_ERROR)
	{
	  return V_ERROR;
	}
      ev_res =
	(*filter->scan_pred->pr_eval_fnc) (thread_p, filter->scan_pred->pred_expr, filter->val_descr, (OID *) oid);
      ev_res = update_logical_result (thread_p, ev_res, NULL, NULL, NULL, NULL);
      if (ev_res != V_TRUE)
	{
	  goto end;
	}
    }

  filter = mvcc_reev_data->key_filter;
  if (filter != NULL && filter->scan_pred != NULL && filter->scan_pred->pred_expr != NULL)
    {
      if (heap_attrinfo_read_dbvalues (thread_p, oid, recdes, NULL, filter->scan_attrs->attr_cache) != NO_ERROR)
	{
	  return V_ERROR;
	}
      ev_res =
	(*filter->scan_pred->pr_eval_fnc) (thread_p, filter->scan_pred->pred_expr, filter->val_descr, (OID *) oid);
      ev_res = update_logical_result (thread_p, ev_res, NULL, NULL, NULL, NULL);
      if (ev_res != V_TRUE)
	{
	  goto end;
	}
    }

  filter = mvcc_reev_data->data_filter;
  if (filter != NULL && filter->scan_pred != NULL && filter->scan_pred->pred_expr != NULL)
    {
      ev_res = eval_data_filter (thread_p, (OID *) oid, recdes, NULL, filter);
      ev_res =
	update_logical_result (thread_p, ev_res, (int *) mvcc_reev_data->qualification, mvcc_reev_data->key_filter,
			       recdes, oid);
    }

end:
  return ev_res;
}

/*
 * locator_decide_operation_type () - returns the operation type that corresponds to the provided lock mode.
 *
 * return	  : operation type
 * lock_mode (in) : lock_mode
 */
SCAN_OPERATION_TYPE
locator_decide_operation_type (LOCK lock_mode, LC_FETCH_VERSION_TYPE fetch_version_type)
{
  SCAN_OPERATION_TYPE op_type;

  if (lock_mode == NULL_LOCK)
    {
      /* for non-mvcc classes, corresponding lock for S_SELECT is S_LOCK; 
       * this inconsistency should be acceptable, as the operation type is ignored anyway for non-mvcc classes */
      op_type = S_SELECT;
    }
  else if (lock_mode <= S_LOCK)
    {
      op_type = S_SELECT_WITH_LOCK;
    }
  else
    {
      op_type = S_DELETE;
      /* equivalent to S_UPDATE */
    }

  if (lock_mode > NULL_LOCK && lock_mode <= S_LOCK
      && (fetch_version_type == LC_FETCH_MVCC_VERSION || fetch_version_type == LC_FETCH_CURRENT_VERSION))
    {
      /* In this situation, the operation type must be changed to S_SELECT. 
       * The final lock mode will be decided when class type will be known */
      assert (op_type == S_SELECT_WITH_LOCK);
      op_type = S_SELECT;
    }

  return op_type;
}

/*
* locator_get_lock_mode_from_op_type () - returns the lock mode that corresponds to the provided operation type.
*
* return	  : lock mode
* lock_mode (in) : operation type
*/
LOCK
locator_get_lock_mode_from_op_type (SCAN_OPERATION_TYPE op_type)
{
  switch (op_type)
    {
    case S_SELECT:
    case S_SELECT_WITH_LOCK:
      /* S_LOCK -> will be converted to NULL_LOCK for mvcc classes */
      return S_LOCK;
    case S_UPDATE:
    case S_DELETE:
      return X_LOCK;
    default:
      assert (false);
      return NA_LOCK;
    }
}
