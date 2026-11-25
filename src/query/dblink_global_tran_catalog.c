/*
 *
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
 * dblink_global_tran_catalog.c - locator API for _db_global_tran insert/update/delete/scan
 * Uses locator_* and heap_* APIs (no SQL).
 */

#ident "$Id$"

#ifdef CCI_XA

#include "dblink_2pc_daemon.h"
#include "dblink_global_tran_catalog.h"
#include "schema_system_catalog_constants.h"
#include "xserver_interface.h"
#include "system_catalog.h"
#include "heap_file.h"
#include "locator_sr.h"
#include "log_impl.h"
#include "mvcc.h"
#include "thread_manager.hpp"
#include "dbtype.h"
#include "error_manager.h"
#include "crypt_opfunc.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <time.h>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/* heap attrid (reverse of column order): gtrid(7), bqual(6), conn_url(5), user(4), password(3), state(2), created_date(1), updated_date(0) */
#define GLOBAL_TRAN_ATTR_GTRID    7
#define GLOBAL_TRAN_ATTR_BQUAL    6
#define GLOBAL_TRAN_ATTR_CONN_URL 5
#define GLOBAL_TRAN_ATTR_USER     4
#define GLOBAL_TRAN_ATTR_PASSWORD 3
#define GLOBAL_TRAN_ATTR_STATE    2
#define GLOBAL_TRAN_ATTR_CREATED  1
#define GLOBAL_TRAN_ATTR_UPDATED  0

/* Prefer the error code already set by the failing heap/locator op; fall back to ER_FAILED. */
static int
dblink_global_tran_errid (void)
{
  int error = er_errid ();
  return (error == NO_ERROR) ? ER_FAILED : error;
}

/*
 * dblink_global_tran_live_snapshot_start () - build the snapshot a heap scan of _db_global_tran reads rows under
 *
 * return: local, or NULL when the current transaction cannot have a snapshot (the scan then reads every version
 *	   in the heap, as it did before)
 *
 *   thread_p(in): thread entry
 *   local(out): snapshot owned by the caller until dblink_global_tran_live_snapshot_end ()
 *   holds_vacuum(out): to pass to dblink_global_tran_live_snapshot_end ()
 *
 * Note: A row is removed with an MVCC delete once its decision has been delivered, and the deleted version stays
 *	 in the heap until vacuum removes it. A scan without a snapshot returns that version as if the row were
 *	 still there: recovery replays a decision that was already delivered, and a lookup by (gtrid, bqual) can
 *	 take it instead of the live row of a later global transaction that reuses the pair. A catalog read
 *	 snapshot hides it and still shows the rows that the current transaction wrote itself, without building
 *	 or changing the transaction snapshot of the caller.
 */
static MVCC_SNAPSHOT *
dblink_global_tran_live_snapshot_start (THREAD_ENTRY * thread_p, MVCC_SNAPSHOT * local, bool * holds_vacuum)
{
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  return logtb_get_catalog_read_snapshot (thread_p, local, holds_vacuum);
}

/*
 * dblink_global_tran_live_snapshot_end () - stop holding vacuum back for a snapshot that
 *					      dblink_global_tran_live_snapshot_start () built
 */
static void
dblink_global_tran_live_snapshot_end (THREAD_ENTRY * thread_p, const MVCC_SNAPSHOT * local, bool holds_vacuum)
{
  if (!holds_vacuum)
    {
      return;
    }

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  logtb_release_catalog_read_snapshot (thread_p, local);
}

int
dblink_global_tran_insert_row (THREAD_ENTRY * thread_p, int gtrid, int bqual,
			       const char *conn_url, const char *user_name, const char *password, char state)
{
  OID class_oid, oid;
  CLS_INFO *cls_info = NULL;
  HFID *hfid_p;
  HEAP_SCANCACHE scan;
  HEAP_CACHE_ATTRINFO attr_info;
  int force_count = 0;
  int error = NO_ERROR;
  DB_VALUE dbval;
  bool scan_cache_inited = false;
  bool attr_inited = false;

  char state_str[2] = { state, '\0' };
  char cipher[DBLINK_PASSWORD_MAX_BUFSIZE + 1] = { 0, };

  /* Never persist the participant password in plaintext: encrypt it up-front (same cipher format as
   * server objects). On failure the caller aborts the enclosing 2PC sysop, so nothing is stored. */
  error = crypt_dblink_password_encrypt (password, cipher, sizeof (cipher));
  if (error != NO_ERROR)
    {
      return error;
    }

  if (xlocator_find_class_oid (thread_p, CT_GLOBAL_TRAN_NAME, &class_oid, NULL_LOCK) != LC_CLASSNAME_EXIST)
    {
      return ER_LC_UNKNOWN_CLASSNAME;
    }

  cls_info = catalog_get_class_info (thread_p, &class_oid, NULL);
  if (cls_info == NULL)
    {
      return ER_FAILED;
    }
  hfid_p = &cls_info->ci_hfid;

  if (heap_scancache_start_modify (thread_p, &scan, hfid_p, &class_oid, SINGLE_ROW_INSERT, NULL) != NO_ERROR)
    {
      error = dblink_global_tran_errid ();
      goto cleanup;
    }
  scan_cache_inited = true;

  if (heap_assign_address (thread_p, hfid_p, &class_oid, &oid, 0) != NO_ERROR)
    {
      error = dblink_global_tran_errid ();
      goto cleanup;
    }

  if (heap_attrinfo_start (thread_p, &class_oid, -1, NULL, &attr_info) != NO_ERROR)
    {
      error = dblink_global_tran_errid ();
      goto cleanup;
    }
  attr_inited = true;

  if (heap_attrinfo_clear_dbvalues (&attr_info) != NO_ERROR)
    {
      error = dblink_global_tran_errid ();
      goto cleanup;
    }

  db_make_int (&dbval, gtrid);
  if (heap_attrinfo_set (&oid, GLOBAL_TRAN_ATTR_GTRID, &dbval, &attr_info) != NO_ERROR)
    {
      error = dblink_global_tran_errid ();
      goto cleanup;
    }
  db_make_int (&dbval, bqual);
  if (heap_attrinfo_set (&oid, GLOBAL_TRAN_ATTR_BQUAL, &dbval, &attr_info) != NO_ERROR)
    {
      error = dblink_global_tran_errid ();
      goto cleanup;
    }
  db_make_string (&dbval, (char *) (conn_url ? conn_url : ""));
  if (heap_attrinfo_set (&oid, GLOBAL_TRAN_ATTR_CONN_URL, &dbval, &attr_info) != NO_ERROR)
    {
      error = dblink_global_tran_errid ();
      goto cleanup;
    }
  db_make_string (&dbval, (char *) (user_name ? user_name : ""));
  if (heap_attrinfo_set (&oid, GLOBAL_TRAN_ATTR_USER, &dbval, &attr_info) != NO_ERROR)
    {
      error = dblink_global_tran_errid ();
      goto cleanup;
    }
  db_make_string (&dbval, cipher);
  if (heap_attrinfo_set (&oid, GLOBAL_TRAN_ATTR_PASSWORD, &dbval, &attr_info) != NO_ERROR)
    {
      error = dblink_global_tran_errid ();
      goto cleanup;
    }

  db_make_string (&dbval, state_str);
  if (heap_attrinfo_set (&oid, GLOBAL_TRAN_ATTR_STATE, &dbval, &attr_info) != NO_ERROR)
    {
      error = dblink_global_tran_errid ();
      goto cleanup;
    }

  db_sys_datetime (&dbval);
  if (heap_attrinfo_set (&oid, GLOBAL_TRAN_ATTR_CREATED, &dbval, &attr_info) != NO_ERROR)
    {
      error = dblink_global_tran_errid ();
      goto cleanup;
    }
  if (heap_attrinfo_set (&oid, GLOBAL_TRAN_ATTR_UPDATED, &dbval, &attr_info) != NO_ERROR)
    {
      error = dblink_global_tran_errid ();
      goto cleanup;
    }

  error = locator_attribute_info_force (thread_p, hfid_p, &oid, &attr_info, NULL, 0,
					LC_FLUSH_INSERT, SINGLE_ROW_INSERT, &scan, &force_count,
					true, REPL_INFO_TYPE_RBR_NORMAL, DB_NOT_PARTITIONED_CLASS,
					NULL, NULL, NULL, UPDATE_INPLACE_NONE, NULL, LOCATOR_LOCK_AT_SELECT);

cleanup:
  if (attr_inited)
    {
      heap_attrinfo_end (thread_p, &attr_info);
    }
  if (scan_cache_inited)
    {
      heap_scancache_end_modify (thread_p, &scan);
    }
  if (cls_info != NULL)
    {
      catalog_free_class_info (cls_info);
    }
  return error;
}

static int
find_row_by_gtrid_bqual (THREAD_ENTRY * thread_p, int gtrid, int bqual, OID * out_oid, HEAP_CACHE_ATTRINFO * attr_info,
			 HEAP_SCANCACHE * scan_p, const HFID * hfid_p, const OID * class_oid_p)
{
  OID inst_oid;
  RECDES recdes = RECDES_INITIALIZER;
  SCAN_CODE sc;
  int i;
  HEAP_ATTRVALUE *heap_value = NULL;
  bool valid_gtrid, valid_bqual;
  int found_gtrid = 0, found_bqual = 0;
  int error = ER_FAILED;	/* not found */
  MVCC_SNAPSHOT live_snapshot;
  MVCC_SNAPSHOT *save_snapshot;
  bool holds_vacuum = false;

  /* scan_p is the modify scan cache of the caller: look the row up under a snapshot, then give the cache back
   * unchanged so that the update/delete which follows behaves as before */
  save_snapshot = scan_p->mvcc_snapshot;
  scan_p->mvcc_snapshot = dblink_global_tran_live_snapshot_start (thread_p, &live_snapshot, &holds_vacuum);

  sc = heap_first (thread_p, hfid_p, (OID *) class_oid_p, &inst_oid, &recdes, scan_p, PEEK);
  while (sc == S_SUCCESS)
    {
      if (heap_attrinfo_read_dbvalues (thread_p, &inst_oid, &recdes, attr_info) != NO_ERROR)
	{
	  error = ER_FAILED;
	  goto end;
	}
      valid_gtrid = false;
      valid_bqual = false;
      for (i = 0, heap_value = attr_info->values; i < attr_info->num_values; i++, heap_value++)
	{
	  if (heap_value->attrid == GLOBAL_TRAN_ATTR_GTRID && !DB_IS_NULL (&heap_value->dbvalue))
	    {
	      found_gtrid = db_get_int (&heap_value->dbvalue);
	      valid_gtrid = true;
	    }
	  else if (heap_value->attrid == GLOBAL_TRAN_ATTR_BQUAL && !DB_IS_NULL (&heap_value->dbvalue))
	    {
	      found_bqual = db_get_int (&heap_value->dbvalue);
	      valid_bqual = true;
	    }

	  /* gtrid/bqual are the only attributes inspected here; stop once both are read */
	  if (valid_gtrid && valid_bqual)
	    {
	      break;
	    }
	}
      if (valid_gtrid && valid_bqual && found_gtrid == gtrid && found_bqual == bqual)
	{
	  *out_oid = inst_oid;
	  error = NO_ERROR;
	  goto end;
	}
      /* a version that the snapshot does not see is read from the log into the scan cache area; with PEEK,
       * recdes still points into the page of the previous row, so let heap_next () assign the area again */
      recdes.data = NULL;
      sc =
	heap_next (thread_p, hfid_p, (OID *) class_oid_p, &inst_oid, &recdes, scan_p, PEEK,
		   HEAP_RECDES_DONT_CONSUME_RAW_BYTES);
    }

end:
  scan_p->mvcc_snapshot = save_snapshot;
  dblink_global_tran_live_snapshot_end (thread_p, &live_snapshot, holds_vacuum);
  return error;
}

int
dblink_global_tran_update_state (THREAD_ENTRY * thread_p, int gtrid, int bqual, char new_state)
{
  OID class_oid, oid;
  CLS_INFO *cls_info = NULL;
  HFID *hfid_p;
  HEAP_SCANCACHE scan;
  HEAP_CACHE_ATTRINFO attr_info;
  int force_count = 0;
  int error = NO_ERROR;
  DB_VALUE dbval;
  bool scan_cache_inited = false;
  bool attr_inited = false;

  char state_str[2] = { new_state, '\0' };

  if (xlocator_find_class_oid (thread_p, CT_GLOBAL_TRAN_NAME, &class_oid, NULL_LOCK) != LC_CLASSNAME_EXIST)
    {
      return ER_LC_UNKNOWN_CLASSNAME;
    }
  cls_info = catalog_get_class_info (thread_p, &class_oid, NULL);
  if (cls_info == NULL)
    {
      return ER_FAILED;
    }
  hfid_p = &cls_info->ci_hfid;

  if (heap_scancache_start_modify (thread_p, &scan, hfid_p, &class_oid, SINGLE_ROW_UPDATE, NULL) != NO_ERROR)
    {
      error = dblink_global_tran_errid ();
      goto cleanup;
    }
  scan_cache_inited = true;

  if (heap_attrinfo_start (thread_p, &class_oid, -1, NULL, &attr_info) != NO_ERROR)
    {
      error = dblink_global_tran_errid ();
      goto cleanup;
    }
  attr_inited = true;

  error = find_row_by_gtrid_bqual (thread_p, gtrid, bqual, &oid, &attr_info, &scan, hfid_p, &class_oid);
  if (error != NO_ERROR)
    {
      goto cleanup;
    }

  db_make_string (&dbval, state_str);
  if (heap_attrinfo_set (&oid, GLOBAL_TRAN_ATTR_STATE, &dbval, &attr_info) != NO_ERROR)
    {
      error = dblink_global_tran_errid ();
      goto cleanup;
    }

  db_sys_datetime (&dbval);
  if (heap_attrinfo_set (&oid, GLOBAL_TRAN_ATTR_UPDATED, &dbval, &attr_info) != NO_ERROR)
    {
      error = dblink_global_tran_errid ();
      goto cleanup;
    }

  error = locator_attribute_info_force (thread_p, hfid_p, &oid, &attr_info, NULL, 0,
					LC_FLUSH_UPDATE, SINGLE_ROW_UPDATE, &scan, &force_count,
					true, REPL_INFO_TYPE_RBR_NORMAL, DB_NOT_PARTITIONED_CLASS,
					NULL, NULL, NULL, UPDATE_INPLACE_NONE, NULL, LOCATOR_LOCK_AT_SELECT);

cleanup:
  if (attr_inited)
    {
      heap_attrinfo_end (thread_p, &attr_info);
    }
  if (scan_cache_inited)
    {
      heap_scancache_end_modify (thread_p, &scan);
    }
  if (cls_info != NULL)
    {
      catalog_free_class_info (cls_info);
    }
  return error;
}

int
dblink_global_tran_delete_row (THREAD_ENTRY * thread_p, int gtrid, int bqual)
{
  OID class_oid, oid;
  CLS_INFO *cls_info = NULL;
  HFID *hfid_p;
  HEAP_SCANCACHE scan;
  HEAP_CACHE_ATTRINFO attr_info;
  int force_count = 0;
  int error = NO_ERROR;
  bool scan_cache_inited = false;
  bool attr_inited = false;

  if (xlocator_find_class_oid (thread_p, CT_GLOBAL_TRAN_NAME, &class_oid, NULL_LOCK) != LC_CLASSNAME_EXIST)
    {
      return ER_LC_UNKNOWN_CLASSNAME;
    }
  cls_info = catalog_get_class_info (thread_p, &class_oid, NULL);
  if (cls_info == NULL)
    {
      return ER_FAILED;
    }
  hfid_p = &cls_info->ci_hfid;

  if (heap_scancache_start_modify (thread_p, &scan, hfid_p, &class_oid, SINGLE_ROW_DELETE, NULL) != NO_ERROR)
    {
      error = dblink_global_tran_errid ();
      goto cleanup;
    }
  scan_cache_inited = true;

  if (heap_attrinfo_start (thread_p, &class_oid, -1, NULL, &attr_info) != NO_ERROR)
    {
      error = dblink_global_tran_errid ();
      goto cleanup;
    }
  attr_inited = true;

  error = find_row_by_gtrid_bqual (thread_p, gtrid, bqual, &oid, &attr_info, &scan, hfid_p, &class_oid);
  if (error != NO_ERROR)
    {
      goto cleanup;
    }

  error =
    locator_delete_force (thread_p, hfid_p, &oid, true, SINGLE_ROW_DELETE, &scan, &force_count, NULL,
			  LOCATOR_LOCK_AT_FORCE);

cleanup:
  if (attr_inited)
    {
      heap_attrinfo_end (thread_p, &attr_info);
    }
  if (scan_cache_inited)
    {
      heap_scancache_end_modify (thread_p, &scan);
    }
  if (cls_info != NULL)
    {
      catalog_free_class_info (cls_info);
    }
  return error;
}

int
dblink_global_tran_scan_for_recovery (THREAD_ENTRY * thread_p, dblink_global_tran_scan_callback callback)
{
  OID class_oid, inst_oid;
  CLS_INFO *cls_info = NULL;
  HFID *hfid_p;
  HEAP_SCANCACHE scan;
  HEAP_CACHE_ATTRINFO attr_info;
  RECDES recdes = RECDES_INITIALIZER;
  SCAN_CODE sc;
  int i;
  HEAP_ATTRVALUE *heap_value = NULL;
  DBLINK_GLOBAL_TRAN_ROW row;
  int error = NO_ERROR;
  bool scan_cache_inited = false;
  bool attr_inited = false;
  char cipher[DBLINK_PASSWORD_MAX_BUFSIZE + 1];
  MVCC_SNAPSHOT live_snapshot;
  MVCC_SNAPSHOT *snapshot_p;
  bool holds_vacuum = false;

  if (callback == NULL)
    {
      return ER_FAILED;
    }

  if (xlocator_find_class_oid (thread_p, CT_GLOBAL_TRAN_NAME, &class_oid, NULL_LOCK) != LC_CLASSNAME_EXIST)
    {
      return ER_LC_UNKNOWN_CLASSNAME;
    }
  cls_info = catalog_get_class_info (thread_p, &class_oid, NULL);
  if (cls_info == NULL)
    {
      return ER_FAILED;
    }
  hfid_p = &cls_info->ci_hfid;

  /* replay only the rows that are still there: a row whose decision was delivered is deleted, but its version
   * stays in the heap until vacuum removes it */
  snapshot_p = dblink_global_tran_live_snapshot_start (thread_p, &live_snapshot, &holds_vacuum);
  if (heap_scancache_start (thread_p, &scan, hfid_p, &class_oid, true, snapshot_p) != NO_ERROR)
    {
      error = dblink_global_tran_errid ();
      goto cleanup;
    }
  scan_cache_inited = true;

  if (heap_attrinfo_start (thread_p, &class_oid, -1, NULL, &attr_info) != NO_ERROR)
    {
      error = dblink_global_tran_errid ();
      goto cleanup;
    }
  attr_inited = true;

  sc = heap_first (thread_p, hfid_p, &class_oid, &inst_oid, &recdes, &scan, PEEK);
  while (sc == S_SUCCESS)
    {
      if (heap_attrinfo_read_dbvalues (thread_p, &inst_oid, &recdes, &attr_info) != NO_ERROR)
	{
	  error = dblink_global_tran_errid ();
	  goto cleanup;
	}
      memset (&row, 0, sizeof (row));
      cipher[0] = '\0';
      for (i = 0, heap_value = attr_info.values; i < attr_info.num_values; i++, heap_value++)
	{
	  if (DB_IS_NULL (&heap_value->dbvalue))
	    {
	      continue;
	    }
	  switch (heap_value->attrid)
	    {
	    case GLOBAL_TRAN_ATTR_GTRID:
	      row.gtrid = db_get_int (&heap_value->dbvalue);
	      break;
	    case GLOBAL_TRAN_ATTR_BQUAL:
	      row.bqual = db_get_int (&heap_value->dbvalue);
	      break;
	    case GLOBAL_TRAN_ATTR_CONN_URL:
	      {
		const char *s = db_get_string (&heap_value->dbvalue);
		snprintf (row.conn_url, sizeof (row.conn_url), "%s", s ? s : "");
	      }
	      break;
	    case GLOBAL_TRAN_ATTR_USER:
	      {
		const char *s = db_get_string (&heap_value->dbvalue);
		snprintf (row.user_name, sizeof (row.user_name), "%s", s ? s : "");
	      }
	      break;
	    case GLOBAL_TRAN_ATTR_PASSWORD:
	      {
		const char *s = db_get_string (&heap_value->dbvalue);
		/* stored as encrypted cipher; keep it locally and decrypt after the row is fully read */
		snprintf (cipher, sizeof (cipher), "%s", s ? s : "");
	      }
	      break;
	    case GLOBAL_TRAN_ATTR_STATE:
	      {
		const char *s = db_get_string (&heap_value->dbvalue);
		row.state = (s && *s) ? *s : '\0';
	      }
	      break;
	    default:
	      break;
	    }
	}
      /* Include 'P' (Prepare), 'A' (Abort), 'C' (Commit) states for recovery */
      if (row.state == DBLINK_2PC_STATE_PREPARE || row.state == DBLINK_2PC_STATE_ABORT
	  || row.state == DBLINK_2PC_STATE_COMMIT)
	{
	  /* Decrypt the participant password back to the raw form used to reconnect. A single corrupt
	   * row must not block recovery of the others, so log and skip it instead of aborting. */
	  if (crypt_dblink_password_decrypt (cipher, row.password, sizeof (row.password)) != NO_ERROR)
	    {
	      er_log_debug (ARG_FILE_LINE,
			    "dblink_global_tran: skip recovery row (gtrid=%d, bqual=%d): password decrypt failed\n",
			    row.gtrid, row.bqual);
	    }
	  else if (!(*callback) (&row))
	    {
	      break;
	    }
	}
      /* see find_row_by_gtrid_bqual () */
      recdes.data = NULL;
      sc =
	heap_next (thread_p, hfid_p, &class_oid, &inst_oid, &recdes, &scan, PEEK, HEAP_RECDES_DONT_CONSUME_RAW_BYTES);
    }

cleanup:
  if (attr_inited)
    {
      heap_attrinfo_end (thread_p, &attr_info);
    }
  if (scan_cache_inited)
    {
      heap_scancache_end (thread_p, &scan);
    }
  dblink_global_tran_live_snapshot_end (thread_p, &live_snapshot, holds_vacuum);
  if (cls_info != NULL)
    {
      catalog_free_class_info (cls_info);
    }
  return error;
}

#endif /* CCI_XA */
