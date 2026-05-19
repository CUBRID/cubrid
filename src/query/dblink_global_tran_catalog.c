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
#include "dbtype.h"
#include "error_manager.h"

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
      error = ER_FAILED;
      goto cleanup;
    }
  scan_cache_inited = true;

  if (heap_assign_address (thread_p, hfid_p, &class_oid, &oid, 0) != NO_ERROR)
    {
      error = er_errid ();
      if (error == NO_ERROR)
	{
	  error = ER_FAILED;
	}
      goto cleanup;
    }

  if (heap_attrinfo_start (thread_p, &class_oid, -1, NULL, &attr_info) != NO_ERROR)
    {
      error = ER_FAILED;
      goto cleanup;
    }
  attr_inited = true;

  if (heap_attrinfo_clear_dbvalues (&attr_info) != NO_ERROR)
    {
      error = ER_FAILED;
      goto cleanup;
    }

  db_make_int (&dbval, gtrid);
  if (heap_attrinfo_set (&oid, GLOBAL_TRAN_ATTR_GTRID, &dbval, &attr_info) != NO_ERROR)
    {
      error = ER_FAILED;
      goto cleanup;
    }
  db_make_int (&dbval, bqual);
  if (heap_attrinfo_set (&oid, GLOBAL_TRAN_ATTR_BQUAL, &dbval, &attr_info) != NO_ERROR)
    {
      error = ER_FAILED;
      goto cleanup;
    }
  db_make_string (&dbval, (char *) (conn_url ? conn_url : ""));
  if (heap_attrinfo_set (&oid, GLOBAL_TRAN_ATTR_CONN_URL, &dbval, &attr_info) != NO_ERROR)
    {
      error = ER_FAILED;
      goto cleanup;
    }
  db_make_string (&dbval, (char *) (user_name ? user_name : ""));
  if (heap_attrinfo_set (&oid, GLOBAL_TRAN_ATTR_USER, &dbval, &attr_info) != NO_ERROR)
    {
      error = ER_FAILED;
      goto cleanup;
    }
  db_make_string (&dbval, (char *) (password ? password : ""));
  if (heap_attrinfo_set (&oid, GLOBAL_TRAN_ATTR_PASSWORD, &dbval, &attr_info) != NO_ERROR)
    {
      error = ER_FAILED;
      goto cleanup;
    }

  db_make_string (&dbval, state_str);
  if (heap_attrinfo_set (&oid, GLOBAL_TRAN_ATTR_STATE, &dbval, &attr_info) != NO_ERROR)
    {
      error = ER_FAILED;
      goto cleanup;
    }

  db_sys_datetime (&dbval);
  if (heap_attrinfo_set (&oid, GLOBAL_TRAN_ATTR_CREATED, &dbval, &attr_info) != NO_ERROR)
    {
      error = ER_FAILED;
      goto cleanup;
    }
  if (heap_attrinfo_set (&oid, GLOBAL_TRAN_ATTR_UPDATED, &dbval, &attr_info) != NO_ERROR)
    {
      error = ER_FAILED;
      goto cleanup;
    }

  error = locator_attribute_info_force (thread_p, hfid_p, &oid, &attr_info, NULL, 0,
					LC_FLUSH_INSERT, SINGLE_ROW_INSERT, &scan, &force_count,
					true, REPL_INFO_TYPE_RBR_NORMAL, DB_NOT_PARTITIONED_CLASS,
					NULL, NULL, NULL, UPDATE_INPLACE_NONE, NULL, false);

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
  RECDES recdes;
  SCAN_CODE sc;
  int i;
  HEAP_ATTRVALUE *heap_value = NULL;
  bool valid_gtrid, valid_bqual;
  int found_gtrid = 0, found_bqual = 0;

  sc = heap_first (thread_p, hfid_p, (OID *) class_oid_p, &inst_oid, &recdes, scan_p, PEEK);
  while (sc == S_SUCCESS)
    {
      if (heap_attrinfo_read_dbvalues (thread_p, &inst_oid, &recdes, attr_info) != NO_ERROR)
	{
	  return ER_FAILED;
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
	}
      if (valid_gtrid && valid_bqual && found_gtrid == gtrid && found_bqual == bqual)
	{
	  *out_oid = inst_oid;
	  return NO_ERROR;
	}
      sc = heap_next (thread_p, hfid_p, (OID *) class_oid_p, &inst_oid, &recdes, scan_p, PEEK);
    }
  return ER_FAILED;		/* not found */
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
      error = ER_FAILED;
      goto cleanup;
    }
  scan_cache_inited = true;

  if (heap_attrinfo_start (thread_p, &class_oid, -1, NULL, &attr_info) != NO_ERROR)
    {
      error = ER_FAILED;
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
      error = ER_FAILED;
      goto cleanup;
    }

  db_sys_datetime (&dbval);
  if (heap_attrinfo_set (&oid, GLOBAL_TRAN_ATTR_UPDATED, &dbval, &attr_info) != NO_ERROR)
    {
      error = ER_FAILED;
      goto cleanup;
    }

  error = locator_attribute_info_force (thread_p, hfid_p, &oid, &attr_info, NULL, 0,
					LC_FLUSH_UPDATE, SINGLE_ROW_UPDATE, &scan, &force_count,
					true, REPL_INFO_TYPE_RBR_NORMAL, DB_NOT_PARTITIONED_CLASS,
					NULL, NULL, NULL, UPDATE_INPLACE_NONE, NULL, false);

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
      error = ER_FAILED;
      goto cleanup;
    }
  scan_cache_inited = true;

  if (heap_attrinfo_start (thread_p, &class_oid, -1, NULL, &attr_info) != NO_ERROR)
    {
      error = ER_FAILED;
      goto cleanup;
    }
  attr_inited = true;

  error = find_row_by_gtrid_bqual (thread_p, gtrid, bqual, &oid, &attr_info, &scan, hfid_p, &class_oid);
  if (error != NO_ERROR)
    {
      goto cleanup;
    }

  error = locator_delete_force (thread_p, hfid_p, &oid, true, SINGLE_ROW_DELETE, &scan, &force_count, NULL, false);

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
  RECDES recdes;
  SCAN_CODE sc;
  int i;
  HEAP_ATTRVALUE *heap_value = NULL;
  DBLINK_GLOBAL_TRAN_ROW row;
  int error = NO_ERROR;
  bool scan_cache_inited = false;
  bool attr_inited = false;

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

  if (heap_scancache_start (thread_p, &scan, hfid_p, &class_oid, true, NULL) != NO_ERROR)
    {
      error = ER_FAILED;
      goto cleanup;
    }
  scan_cache_inited = true;

  if (heap_attrinfo_start (thread_p, &class_oid, -1, NULL, &attr_info) != NO_ERROR)
    {
      error = ER_FAILED;
      goto cleanup;
    }
  attr_inited = true;

  sc = heap_first (thread_p, hfid_p, &class_oid, &inst_oid, &recdes, &scan, PEEK);
  while (sc == S_SUCCESS)
    {
      if (heap_attrinfo_read_dbvalues (thread_p, &inst_oid, &recdes, &attr_info) != NO_ERROR)
	{
	  error = ER_FAILED;
	  goto cleanup;
	}
      memset (&row, 0, sizeof (row));
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
		snprintf (row.password, sizeof (row.password), "%s", s ? s : "");
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
	  if (!(*callback) (&row))
	    {
	      break;
	    }
	}
      sc = heap_next (thread_p, hfid_p, &class_oid, &inst_oid, &recdes, &scan, PEEK);
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
  if (cls_info != NULL)
    {
      catalog_free_class_info (cls_info);
    }
  return error;
}

#endif /* CCI_XA */
