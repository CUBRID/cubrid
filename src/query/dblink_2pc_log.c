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
 * transaction_2pc_log.c - 2PC Coordinator Catalog Interface
 * 
 * This module provides interface functions to read/write _db_coordinator log table
 * for distributed transaction coordination using locator interface.
 */

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "heap_file.h"
#include "object_primitive.h"
#include "object_representation.h"
#include "dbtype.h"
#include "db.h"
#include "base64.h"
#include "xserver_interface.h"
#include "locator_sr.h"
#include "thread_manager.hpp"
#include "dblink_2pc_log.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/* _db_coordinator log class name */
#define GTRAN_2PC_CATALOG_CLASS_NAME "_db_global_tran"

/* Attribute names */
#define GTRAN_2PC_ATTR_GTRID "gtrid"
#define GTRAN_2PC_ATTR_BQUAL "bqual"
#define GTRAN_2PC_ATTR_CONN_URL "conn_url"
#define GTRAN_2PC_ATTR_USER "user"
#define GTRAN_2PC_ATTR_PASSWORD "password"
#define GTRAN_2PC_ATTR_STATE "state"
#define GTRAN_2PC_ATTR_CREATED_TIME "created_time"
#define GTRAN_2PC_ATTR_UPDATED_TIME "updated_time"

/* State values */
#define GTRAN_2PC_STATE_STARTED 'S'
#define GTRAN_2PC_STATE_PREPARE 'P'
#define GTRAN_2PC_STATE_COMMIT 'C'
#define GTRAN_2PC_STATE_ABORT 'A'

/*
 * dblink_2pc_encode_password() - Encode password using base64
 *   return: Error code
 *   plain_password(in): Plain text password
 *   encoded_password(out): Encoded password buffer
 *   encoded_size(in): Size of encoded buffer
 */
int
dblink_2pc_encode_password (const char *plain_password, char **encoded_password, int *encoded_size)
{
  int plain_len;
  int encoded_len;
  int error;

  if (plain_password == NULL || encoded_password == NULL || encoded_size == NULL)
    {
      return ER_FAILED;
    }

  plain_len = strlen (plain_password);

  /* Simple base64 encoding - in production, use proper encryption */
  error = base64_encode ((const unsigned char *) plain_password, plain_len,
			 (unsigned char **) encoded_password, encoded_size);

  return error;
}

/*
 * dblink_2pc_decode_password() - Decode password from base64
 *   return: Error code
 *   encoded_password(in): Encoded password
 *   plain_password(out): Plain text password buffer
 *   plain_size(in): Size of plain buffer
 */
int
dblink_2pc_decode_password (const char *encoded_password, char **plain_password, int *plain_size)
{
  int decoded_len;
  int error;

  if (encoded_password == NULL || plain_password == NULL || plain_size == NULL)
    {
      return ER_FAILED;
    }

  /* Simple base64 decoding - in production, use proper decryption */
  error = base64_decode ((unsigned char *) encoded_password, strlen (encoded_password),
			 (unsigned char **) plain_password, plain_size);

  return error;
}

/*
 * dblink_2pc_log_get_class_oid() - Get OID of _db_coordinator log class
 *   return: Error code
 *   thread_p(in): Thread entry
 *   class_oid(out): OID of the log class
 */
static int
dblink_2pc_log_get_class_oid (THREAD_ENTRY * thread_p, OID * class_oid)
{
  int error = NO_ERROR;

  if (class_oid == NULL)
    {
      return ER_FAILED;
    }

  /* Find class OID using server-side API */
  error = xlocator_find_class_oid (thread_p, GTRAN_2PC_CATALOG_CLASS_NAME, class_oid, NULL_LOCK);
  if (error != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LC_UNKNOWN_CLASSNAME, 1, GTRAN_2PC_CATALOG_CLASS_NAME);
      return error;
    }

  return NO_ERROR;
}

/*
 * dblink_2pc_log_insert() - Insert a new 2PC transaction log entry
 *   return: Error code
 *   thread_p(in): Thread entry
 *   gtrid(in): Global transaction ID
 *   participant(in): Participant information
 *   state(in): Initial state ('S' for started)
 */
int
dblink_2pc_log_insert (THREAD_ENTRY * thread_p, int gtrid, const PARTICIPANT_INFO * participant, char state)
{
  int error = NO_ERROR;
  int encoded_size;
  OID class_oid;
  OID new_oid;
  HFID hfid;
  HEAP_SCANCACHE scan_cache;
  HEAP_CACHE_ATTRINFO attr_info;
  DB_VALUE values[8];
  ATTR_ID attr_ids[8];
  time_t current_time;
  char *encoded_password;
  bool scan_cache_inited = false;
  bool attr_info_inited = false;
  int force_count = 0;
  int i;

  if (participant == NULL)
    {
      return ER_FAILED;
    }

  /* Initialize values */
  for (i = 0; i < 8; i++)
    {
      db_make_null (&values[i]);
      attr_ids[i] = i;
    }

  /* Encode password */
  error = dblink_2pc_encode_password (participant->password, &encoded_password, &encoded_size);
  if (error != NO_ERROR)
    {
      return error;
    }

  /* Get class OID */
  error = dblink_2pc_log_get_class_oid (thread_p, &class_oid);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  /* Get HFID from class */
  error = heap_get_class_info (thread_p, &class_oid, &hfid, NULL, NULL);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  /* Initialize scan cache */
  error = heap_scancache_start (thread_p, &scan_cache, &hfid, &class_oid, true, NULL);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }
  scan_cache_inited = true;

  /* Initialize attribute info */
  error = heap_attrinfo_start (thread_p, &class_oid, -1, NULL, &attr_info);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }
  attr_info_inited = true;

  /* Build attribute values */
  current_time = time (NULL);

  /* Attr 0: GTRID (int) */
  db_make_int (&values[0], gtrid);

  /* Attr 1: BQUAL (int) */
  db_make_int (&values[1], participant->bqual);

  /* Attr 2: CONN_URL (string) */
  db_make_string (&values[2], participant->conn_url);

  /* Attr 3: USER (string) */
  db_make_string (&values[3], participant->user);

  /* Attr 4: PASSWORD (encoded string) */
  db_make_string (&values[4], encoded_password);

  /* Attr 5: STATE (char) */
  db_make_char (&values[5], 1, &state, 1, LANG_SYS_CODESET, LANG_SYS_COLLATION);

  /* Attr 6: CREATED_TIME (datetime) */
  db_make_timestamp (&values[6], (DB_TIMESTAMP) current_time);

  /* Attr 7: UPDATED_TIME (datetime) */
  db_make_timestamp (&values[7], (DB_TIMESTAMP) current_time);

  /* Set attribute values to attr_info */
  for (i = 0; i < 8; i++)
    {
      error = heap_attrinfo_set (&new_oid, attr_ids[i], &values[i], &attr_info);
      if (error != NO_ERROR)
	{
	  goto error_exit;
	}
    }

  /* Insert record using locator_attribute_info_force */
  OID_SET_NULL (&new_oid);
  error = locator_attribute_info_force (thread_p, &hfid, &new_oid, &attr_info,
					attr_ids, 8, LC_FLUSH_INSERT,
					SINGLE_ROW_INSERT, &scan_cache,
					&force_count, false, REPL_INFO_TYPE_RBR_NORMAL,
					DB_NOT_PARTITIONED_CLASS, NULL, NULL, NULL, UPDATE_INPLACE_NONE, NULL, true);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  /* Clear values */
  for (i = 0; i < 8; i++)
    {
      db_value_clear (&values[i]);
    }

  /* End attribute info */
  heap_attrinfo_end (thread_p, &attr_info);
  attr_info_inited = false;

  /* End scan cache */
  heap_scancache_end (thread_p, &scan_cache);
  scan_cache_inited = false;

  return NO_ERROR;

error_exit:
  /* Clear values */
  for (i = 0; i < 8; i++)
    {
      db_value_clear (&values[i]);
    }

  if (attr_info_inited)
    {
      heap_attrinfo_end (thread_p, &attr_info);
    }
  if (scan_cache_inited)
    {
      heap_scancache_end (thread_p, &scan_cache);
    }

  return error;
}

/*
 * dblink_2pc_log_update_state() - Update state of a 2PC transaction log
 *   return: Error code
 *   thread_p(in): Thread entry
 *   gtrid(in): Global transaction ID to update
 *   bqual(in): Branch qualifier
 *   new_state(in): New state value ('P', 'C', or 'A')
 */
int
dblink_2pc_log_update_state (THREAD_ENTRY * thread_p, int gtrid, int bqual, char new_state)
{
  int error = NO_ERROR;
  OID class_oid;
  HFID hfid;
  HEAP_SCANCACHE scan_cache;
  HEAP_CACHE_ATTRINFO attr_info;
  OID target_oid;
  DB_VALUE values[2];
  ATTR_ID attr_ids[2];
  time_t current_time;
  bool scan_cache_inited = false;
  bool attr_info_inited = false;
  bool found = false;
  int force_count = 0;
  int i;

  /* Initialize values */
  for (i = 0; i < 2; i++)
    {
      db_make_null (&values[i]);
    }

  /* Get class OID */
  error = dblink_2pc_log_get_class_oid (thread_p, &class_oid);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  /* Get HFID from class */
  error = heap_get_class_info (thread_p, &class_oid, &hfid, NULL, NULL);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  /* Initialize scan cache */
  error = heap_scancache_start (thread_p, &scan_cache, &hfid, &class_oid, true, NULL);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }
  scan_cache_inited = true;

  /* Find the record with matching GTRID and BQUAL */
  OID scan_oid;
  RECDES scan_recdes;

  OID_SET_NULL (&scan_oid);

  while ((error = heap_next (thread_p, &hfid, &class_oid, &scan_oid, &scan_recdes, &scan_cache, PEEK)) == S_SUCCESS)
    {
      OR_BUF read_buf;
      DB_VALUE gtrid_value, bqual_value;

      or_init (&read_buf, scan_recdes.data, scan_recdes.length);

      /* Read GTRID attribute */
      error = or_get_value (&read_buf, &gtrid_value, NULL, -1, true);
      if (error != NO_ERROR)
	{
	  db_value_clear (&gtrid_value);
	  continue;
	}

      /* Read BQUAL attribute */
      error = or_get_value (&read_buf, &bqual_value, NULL, -1, true);
      if (error != NO_ERROR)
	{
	  db_value_clear (&gtrid_value);
	  db_value_clear (&bqual_value);
	  continue;
	}

      /* Check if GTRID and BQUAL match */
      if (DB_VALUE_TYPE (&gtrid_value) == DB_TYPE_INTEGER &&
	  DB_VALUE_TYPE (&bqual_value) == DB_TYPE_INTEGER &&
	  db_get_int (&gtrid_value) == gtrid && db_get_int (&bqual_value) == bqual)
	{
	  COPY_OID (&target_oid, &scan_oid);
	  found = true;
	  db_value_clear (&gtrid_value);
	  db_value_clear (&bqual_value);
	  break;
	}

      db_value_clear (&gtrid_value);
      db_value_clear (&bqual_value);
    }

  if (!found)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 0);
      error = ER_FAILED;
      goto error_exit;
    }

  /* Initialize attribute info for update */
  error = heap_attrinfo_start (thread_p, &class_oid, -1, NULL, &attr_info);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }
  attr_info_inited = true;

  /* Set attribute values to update */
  current_time = time (NULL);

  /* Attr 5: STATE (char) */
  attr_ids[0] = 5;
  db_make_char (&values[0], 1, &new_state, 1, LANG_SYS_CODESET, LANG_SYS_COLLATION);

  /* Attr 7: UPDATED_TIME (datetime) */
  attr_ids[1] = 7;
  db_make_timestamp (&values[1], (DB_TIMESTAMP) current_time);

  /* Set attribute values to attr_info */
  for (i = 0; i < 2; i++)
    {
      error = heap_attrinfo_set (&target_oid, attr_ids[i], &values[i], &attr_info);
      if (error != NO_ERROR)
	{
	  goto error_exit;
	}
    }

  /* Update record using locator_attribute_info_force */
  error = locator_attribute_info_force (thread_p, &hfid, &target_oid, &attr_info,
					attr_ids, 2, LC_FLUSH_UPDATE,
					SINGLE_ROW_UPDATE, &scan_cache,
					&force_count, false, REPL_INFO_TYPE_RBR_NORMAL,
					DB_NOT_PARTITIONED_CLASS, NULL, NULL, NULL, UPDATE_INPLACE_NONE, NULL, true);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  /* Clear values */
  for (i = 0; i < 2; i++)
    {
      db_value_clear (&values[i]);
    }

  /* End attribute info */
  heap_attrinfo_end (thread_p, &attr_info);
  attr_info_inited = false;

  /* End scan cache */
  heap_scancache_end (thread_p, &scan_cache);
  scan_cache_inited = false;

  return NO_ERROR;

error_exit:
  /* Clear values */
  for (i = 0; i < 2; i++)
    {
      db_value_clear (&values[i]);
    }

  if (attr_info_inited)
    {
      heap_attrinfo_end (thread_p, &attr_info);
    }
  if (scan_cache_inited)
    {
      heap_scancache_end (thread_p, &scan_cache);
    }

  return error;
}

/*
 * dblink_2pc_log_read_log() - Read 2PC transaction log by GTRID and BQUAL
 *   return: Error code
 *   thread_p(in): Thread entry
 *   gtrid(in): Global transaction ID to search
 *   bqual(in): Branch qualifier
 *   log_entry(out): Structure to store the log entry
 */
int
dblink_2pc_log_read (THREAD_ENTRY * thread_p, int gtrid, int bqual, GTRAN_2PC_LOG_ENTRY * log_entry)
{
  int error = NO_ERROR;
  OID class_oid;
  HFID hfid;
  HEAP_SCANCACHE scan_cache;
  bool scan_cache_inited = false;
  bool found = false;

  if (log_entry == NULL)
    {
      return ER_FAILED;
    }

  /* Initialize output structure */
  memset (log_entry, 0, sizeof (GTRAN_2PC_LOG_ENTRY));

  /* Get class OID */
  error = dblink_2pc_log_get_class_oid (thread_p, &class_oid);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  /* Get HFID from class */
  error = heap_get_class_info (thread_p, &class_oid, &hfid, NULL, NULL);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  /* Initialize scan cache */
  error = heap_scancache_start (thread_p, &scan_cache, &hfid, &class_oid, true, NULL);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }
  scan_cache_inited = true;

  /* Scan for matching GTRID and BQUAL */
  OID scan_oid;
  RECDES scan_recdes;

  OID_SET_NULL (&scan_oid);

  while ((error = heap_next (thread_p, &hfid, &class_oid, &scan_oid, &scan_recdes, &scan_cache, PEEK)) == S_SUCCESS)
    {
      OR_BUF read_buf;
      DB_VALUE value;

      or_init (&read_buf, scan_recdes.data, scan_recdes.length);

      /* Read GTRID */
      error = or_get_value (&read_buf, &value, NULL, -1, true);
      if (error != NO_ERROR || DB_VALUE_TYPE (&value) != DB_TYPE_INTEGER)
	{
	  db_value_clear (&value);
	  continue;
	}

      int read_gtrid = db_get_int (&value);
      db_value_clear (&value);

      /* Read BQUAL */
      error = or_get_value (&read_buf, &value, NULL, -1, true);
      if (error != NO_ERROR || DB_VALUE_TYPE (&value) != DB_TYPE_INTEGER)
	{
	  db_value_clear (&value);
	  continue;
	}

      int read_bqual = db_get_int (&value);
      db_value_clear (&value);

      if (read_gtrid == gtrid && read_bqual == bqual)
	{
	  /* Found matching record - read all attributes */
	  log_entry->gtrid = gtrid;
	  log_entry->bqual = bqual;

	  /* Read CONN_URL */
	  if (or_get_value (&read_buf, &value, NULL, -1, true) == NO_ERROR)
	    {
	      const char *conn_url = db_get_string (&value);
	      if (conn_url != NULL)
		{
		  strncpy (log_entry->conn_url, conn_url, sizeof (log_entry->conn_url) - 1);
		}
	      db_value_clear (&value);
	    }

	  /* Read USER */
	  if (or_get_value (&read_buf, &value, NULL, -1, true) == NO_ERROR)
	    {
	      const char *user = db_get_string (&value);
	      if (user != NULL)
		{
		  strncpy (log_entry->user, user, sizeof (log_entry->user) - 1);
		}
	      db_value_clear (&value);
	    }

	  /* Read PASSWORD (encoded) */
	  if (or_get_value (&read_buf, &value, NULL, -1, true) == NO_ERROR)
	    {
	      const char *password = db_get_string (&value);
	      if (password != NULL)
		{
		  strncpy (log_entry->password, password, sizeof (log_entry->password) - 1);
		}
	      db_value_clear (&value);
	    }

	  /* Read STATE */
	  if (or_get_value (&read_buf, &value, NULL, -1, true) == NO_ERROR)
	    {
	      const char *state_str = db_get_string (&value);
	      log_entry->state = (state_str != NULL) ? state_str[0] : 0;
	      db_value_clear (&value);
	    }

	  /* Read CREATED_TIME */
	  if (or_get_value (&read_buf, &value, NULL, -1, true) == NO_ERROR)
	    {
	      log_entry->created_time = (time_t) * db_get_timestamp (&value);
	      db_value_clear (&value);
	    }

	  /* Read UPDATED_TIME */
	  if (or_get_value (&read_buf, &value, NULL, -1, true) == NO_ERROR)
	    {
	      log_entry->updated_time = (time_t) * db_get_timestamp (&value);
	      db_value_clear (&value);
	    }

	  found = true;
	  break;
	}
    }

  if (!found)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 0);
      error = ER_FAILED;
      goto error_exit;
    }

  /* End scan cache */
  heap_scancache_end (thread_p, &scan_cache);
  scan_cache_inited = false;

  return NO_ERROR;

error_exit:
  if (scan_cache_inited)
    {
      heap_scancache_end (thread_p, &scan_cache);
    }

  return error;
}

/*
 * dblink_2pc_log_delete_log() - Delete a 2PC transaction log entry
 *   return: Error code
 *   thread_p(in): Thread entry
 *   gtrid(in): Global transaction ID to delete
 *   bqual(in): Branch qualifier
 */
int
dblink_2pc_log_delete (THREAD_ENTRY * thread_p, int gtrid, int bqual)
{
  int error = NO_ERROR;
  OID class_oid;
  HFID hfid;
  HEAP_SCANCACHE scan_cache;
  HEAP_CACHE_ATTRINFO attr_info;
  OID target_oid;
  bool scan_cache_inited = false;
  bool attr_info_inited = false;
  bool found = false;
  int force_count = 0;

  /* Get class OID */
  error = dblink_2pc_log_get_class_oid (thread_p, &class_oid);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  /* Get HFID from class */
  error = heap_get_class_info (thread_p, &class_oid, &hfid, NULL, NULL);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  /* Initialize scan cache */
  error = heap_scancache_start (thread_p, &scan_cache, &hfid, &class_oid, true, NULL);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }
  scan_cache_inited = true;

  /* Find the record with matching GTRID and BQUAL */
  OID scan_oid;
  RECDES scan_recdes;

  OID_SET_NULL (&scan_oid);

  while ((error = heap_next (thread_p, &hfid, &class_oid, &scan_oid, &scan_recdes, &scan_cache, PEEK)) == S_SUCCESS)
    {
      OR_BUF read_buf;
      DB_VALUE gtrid_value, bqual_value;

      or_init (&read_buf, scan_recdes.data, scan_recdes.length);

      /* Read GTRID attribute */
      error = or_get_value (&read_buf, &gtrid_value, NULL, -1, true);
      if (error != NO_ERROR)
	{
	  db_value_clear (&gtrid_value);
	  continue;
	}

      /* Read BQUAL attribute */
      error = or_get_value (&read_buf, &bqual_value, NULL, -1, true);
      if (error != NO_ERROR)
	{
	  db_value_clear (&gtrid_value);
	  db_value_clear (&bqual_value);
	  continue;
	}

      /* Check if GTRID and BQUAL match */
      if (DB_VALUE_TYPE (&gtrid_value) == DB_TYPE_INTEGER &&
	  DB_VALUE_TYPE (&bqual_value) == DB_TYPE_INTEGER &&
	  db_get_int (&gtrid_value) == gtrid && db_get_int (&bqual_value) == bqual)
	{
	  COPY_OID (&target_oid, &scan_oid);
	  found = true;
	  db_value_clear (&gtrid_value);
	  db_value_clear (&bqual_value);
	  break;
	}

      db_value_clear (&gtrid_value);
      db_value_clear (&bqual_value);
    }

  if (!found)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 0);
      error = ER_FAILED;
      goto error_exit;
    }

  /* Initialize attribute info for delete */
  error = heap_attrinfo_start (thread_p, &class_oid, -1, NULL, &attr_info);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }
  attr_info_inited = true;

  /* Delete record using locator_attribute_info_force */
  error = locator_attribute_info_force (thread_p, &hfid, &target_oid, &attr_info,
					NULL, 0, LC_FLUSH_DELETE,
					SINGLE_ROW_DELETE, &scan_cache,
					&force_count, false, REPL_INFO_TYPE_RBR_NORMAL,
					DB_NOT_PARTITIONED_CLASS, NULL, NULL, NULL, UPDATE_INPLACE_NONE, NULL, true);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  /* End attribute info */
  heap_attrinfo_end (thread_p, &attr_info);
  attr_info_inited = false;

  /* End scan cache */
  heap_scancache_end (thread_p, &scan_cache);
  scan_cache_inited = false;

  return NO_ERROR;

error_exit:
  if (attr_info_inited)
    {
      heap_attrinfo_end (thread_p, &attr_info);
    }
  if (scan_cache_inited)
    {
      heap_scancache_end (thread_p, &scan_cache);
    }

  return error;
}
