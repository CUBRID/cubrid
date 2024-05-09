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
 * show_scan.c - scan information for show statements
 */

#ident "$Id$"

/*TODO this header (cinttypes) apparently has to be the first one included
 *in order to compile (there's some problem with the PRIx64 macro)
 */
#include <cinttypes>
#include "config.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "error_manager.h"
#include "memory_alloc.h"

#include "query_manager.h"
#include "object_primitive.h"
#include "object_representation.h"
#include "scan_manager.h"
#include "show_scan.h"

#include "disk_manager.h"
#include "log_manager.h"
#include "slotted_page.h"
#include "heap_file.h"
#include "btree.h"
#include "connection_support.h"
#include "critical_section.h"
#include "tz_support.h"
#include "db_date.h"
#include "network.h"

#if defined(ENABLE_SYSTEMTAP)
#include "probes.h"
#endif /* ENABLE_SYSTEMTAP */

#include "porting.h"
#include "server_support.h"
#include "dbtype.h"
#include "thread_manager.hpp"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

typedef SCAN_CODE (*NEXT_SCAN_FUNC) (THREAD_ENTRY * thread_p, int cursor, DB_VALUE ** out_values, int out_cnt,
				     void *ctx);
typedef int (*START_SCAN_FUNC) (THREAD_ENTRY * thread_p, int show_type, DB_VALUE ** arg_values, int arg_cnt,
				void **ctx);
typedef int (*END_SCAN_FUNC) (THREAD_ENTRY * thread_p, void **ctx);

typedef struct show_request SHOW_REQUEST;
struct show_request
{
  SHOWSTMT_TYPE show_type;	/* show stmt type */
  START_SCAN_FUNC start_func;	/* start scan function */
  NEXT_SCAN_FUNC next_func;	/* next scan function */
  END_SCAN_FUNC end_func;	/* end scan function */
};

const size_t THREAD_SCAN_COLUMN_COUNT = 26;

static SCAN_CODE showstmt_array_next_scan (THREAD_ENTRY * thread_p, int cursor, DB_VALUE ** out_values, int out_cnt,
					   void *ptr);
static int showstmt_array_end_scan (THREAD_ENTRY * thread_p, void **ptr);
#if defined (SERVER_MODE)
static void thread_scan_mapfunc (THREAD_ENTRY & thread_ref, bool & stop_mapper, THREAD_ENTRY * caller_thread_p,
				 SHOWSTMT_ARRAY_CONTEXT * ctx, int &error);
#endif // SERVER_MODE


static bool show_scan_Inited = false;

static SHOW_REQUEST show_Requests[SHOWSTMT_END];

/*
 *  showstmt_scan_init () - initialize the scan functions of
 *                          show statments.
 *   return: NULL
 */
void
showstmt_scan_init (void)
{
  SHOW_REQUEST *req;

  if (show_scan_Inited)
    {
      return;
    }

  memset (show_Requests, 0, SHOWSTMT_END * sizeof (SHOW_REQUEST));

  req = &show_Requests[SHOWSTMT_VOLUME_HEADER];
  req->show_type = SHOWSTMT_VOLUME_HEADER;
  req->start_func = disk_volume_header_start_scan;
  req->next_func = disk_volume_header_next_scan;
  req->end_func = disk_volume_header_end_scan;

  req = &show_Requests[SHOWSTMT_ACCESS_STATUS];
  req->show_type = SHOWSTMT_ACCESS_STATUS;
  req->start_func = css_user_access_status_start_scan;
  req->next_func = showstmt_array_next_scan;
  req->end_func = showstmt_array_end_scan;

  req = &show_Requests[SHOWSTMT_ACTIVE_LOG_HEADER];
  req->show_type = SHOWSTMT_ACTIVE_LOG_HEADER;
  req->start_func = log_active_log_header_start_scan;
  req->next_func = log_active_log_header_next_scan;
  req->end_func = log_active_log_header_end_scan;

  req = &show_Requests[SHOWSTMT_ARCHIVE_LOG_HEADER];
  req->show_type = SHOWSTMT_ARCHIVE_LOG_HEADER;
  req->start_func = log_archive_log_header_start_scan;
  req->next_func = log_archive_log_header_next_scan;
  req->end_func = log_archive_log_header_end_scan;

  req = &show_Requests[SHOWSTMT_SLOTTED_PAGE_HEADER];
  req->show_type = SHOWSTMT_SLOTTED_PAGE_HEADER;
  req->start_func = spage_header_start_scan;
  req->next_func = spage_header_next_scan;
  req->end_func = spage_header_end_scan;

  req = &show_Requests[SHOWSTMT_SLOTTED_PAGE_SLOTS];
  req->show_type = SHOWSTMT_SLOTTED_PAGE_SLOTS;
  req->start_func = spage_slots_start_scan;
  req->next_func = spage_slots_next_scan;
  req->end_func = spage_slots_end_scan;

  req = &show_Requests[SHOWSTMT_HEAP_HEADER];
  req->show_type = SHOWSTMT_HEAP_HEADER;
  req->start_func = heap_header_capacity_start_scan;
  req->next_func = heap_header_next_scan;
  req->end_func = heap_header_capacity_end_scan;

  req = &show_Requests[SHOWSTMT_ALL_HEAP_HEADER];
  req->show_type = SHOWSTMT_ALL_HEAP_HEADER;
  req->start_func = heap_header_capacity_start_scan;
  req->next_func = heap_header_next_scan;
  req->end_func = heap_header_capacity_end_scan;

  req = &show_Requests[SHOWSTMT_HEAP_CAPACITY];
  req->show_type = SHOWSTMT_HEAP_CAPACITY;
  req->start_func = heap_header_capacity_start_scan;
  req->next_func = heap_capacity_next_scan;
  req->end_func = heap_header_capacity_end_scan;

  req = &show_Requests[SHOWSTMT_ALL_HEAP_CAPACITY];
  req->show_type = SHOWSTMT_ALL_HEAP_CAPACITY;
  req->start_func = heap_header_capacity_start_scan;
  req->next_func = heap_capacity_next_scan;
  req->end_func = heap_header_capacity_end_scan;

  req = &show_Requests[SHOWSTMT_INDEX_HEADER];
  req->show_type = SHOWSTMT_INDEX_HEADER;
  req->start_func = btree_index_start_scan;
  req->next_func = btree_index_next_scan;
  req->end_func = btree_index_end_scan;

  req = &show_Requests[SHOWSTMT_INDEX_CAPACITY];
  req->show_type = SHOWSTMT_INDEX_CAPACITY;
  req->start_func = btree_index_start_scan;
  req->next_func = btree_index_next_scan;
  req->end_func = btree_index_end_scan;

  req = &show_Requests[SHOWSTMT_ALL_INDEXES_HEADER];
  req->show_type = SHOWSTMT_ALL_INDEXES_HEADER;
  req->start_func = btree_index_start_scan;
  req->next_func = btree_index_next_scan;
  req->end_func = btree_index_end_scan;

  req = &show_Requests[SHOWSTMT_ALL_INDEXES_CAPACITY];
  req->show_type = SHOWSTMT_ALL_INDEXES_CAPACITY;
  req->start_func = btree_index_start_scan;
  req->next_func = btree_index_next_scan;
  req->end_func = btree_index_end_scan;

  req = &show_Requests[SHOWSTMT_GLOBAL_CRITICAL_SECTIONS];
  req->show_type = SHOWSTMT_GLOBAL_CRITICAL_SECTIONS;
  req->start_func = csect_start_scan;
  req->next_func = showstmt_array_next_scan;
  req->end_func = showstmt_array_end_scan;

  req = &show_Requests[SHOWSTMT_JOB_QUEUES];
  req->show_type = SHOWSTMT_JOB_QUEUES;
  req->start_func = css_job_queues_start_scan;
  req->next_func = showstmt_array_next_scan;
  req->end_func = showstmt_array_end_scan;

  req = &show_Requests[SHOWSTMT_TIMEZONES];
  req->show_type = SHOWSTMT_TIMEZONES;
  req->start_func = tz_timezones_start_scan;
  req->next_func = showstmt_array_next_scan;
  req->end_func = showstmt_array_end_scan;

  req = &show_Requests[SHOWSTMT_FULL_TIMEZONES];
  req->show_type = SHOWSTMT_FULL_TIMEZONES;
  req->start_func = tz_full_timezones_start_scan;
  req->next_func = showstmt_array_next_scan;
  req->end_func = showstmt_array_end_scan;

  req = &show_Requests[SHOWSTMT_TRAN_TABLES];
  req->show_type = SHOWSTMT_TRAN_TABLES;
  req->start_func = logtb_descriptors_start_scan;
  req->next_func = showstmt_array_next_scan;
  req->end_func = showstmt_array_end_scan;

  req = &show_Requests[SHOWSTMT_THREADS];
  req->show_type = SHOWSTMT_THREADS;
  req->start_func = thread_start_scan;
  req->next_func = showstmt_array_next_scan;
  req->end_func = showstmt_array_end_scan;

  req = &show_Requests[SHOWSTMT_PAGE_BUFFER_STATUS];
  req->show_type = SHOWSTMT_PAGE_BUFFER_STATUS;
  req->start_func = pgbuf_start_scan;
  req->next_func = showstmt_array_next_scan;
  req->end_func = showstmt_array_end_scan;

  /* append to init other show statement scan function here */


  show_scan_Inited = true;
}

/*
 *  showstmt_next_scan () - scan values from different show statment.
 *   return: NO_ERROR, or ER_code
 *   thread_p(in):
 *   s_id(in):
 */
SCAN_CODE
showstmt_next_scan (THREAD_ENTRY * thread_p, SCAN_ID * s_id)
{
  SHOWSTMT_SCAN_ID *stsidp = &s_id->s.stsid;
  SHOWSTMT_TYPE show_type = stsidp->show_type;
  NEXT_SCAN_FUNC next_func = NULL;
  SCAN_CODE code;
  int i;

  assert (show_type == show_Requests[show_type].show_type);

  next_func = show_Requests[show_type].next_func;
  if (next_func == NULL)
    {
      return S_END;
    }

  /* free values which need be cleared */
  for (i = 0; i < stsidp->out_cnt; i++)
    {
      pr_clear_value (stsidp->out_values[i]);
    }

  code = (*next_func) (thread_p, stsidp->cursor++, stsidp->out_values, stsidp->out_cnt, stsidp->ctx);
  return code;
}

/*
 *  showstmt_start_scan () - before scan.
 *   return: NO_ERROR, or ER_code
 *   thread_p(in):
 *   s_id(in):
 */
int
showstmt_start_scan (THREAD_ENTRY * thread_p, SCAN_ID * s_id)
{
  SHOWSTMT_SCAN_ID *stsidp = &s_id->s.stsid;
  SHOWSTMT_TYPE show_type = stsidp->show_type;
  START_SCAN_FUNC start_func = NULL;
  int error;

  assert (show_type == show_Requests[show_type].show_type);

  start_func = show_Requests[show_type].start_func;
  if (start_func == NULL)
    {
      return NO_ERROR;
    }

  error = (*start_func) (thread_p, (int) show_type, stsidp->arg_values, stsidp->arg_cnt, &stsidp->ctx);
  return error;
}

/*
 *  showstmt_end_scan () - after scan.
 *   return: NO_ERROR, or ER_code
 *   thread_p(in):
 *   s_id(in):
 */
int
showstmt_end_scan (THREAD_ENTRY * thread_p, SCAN_ID * s_id)
{
  SHOWSTMT_SCAN_ID *stsidp = &s_id->s.stsid;
  SHOWSTMT_TYPE show_type = stsidp->show_type;
  END_SCAN_FUNC end_func = NULL;
  int error;

  assert (show_type == show_Requests[show_type].show_type);

  end_func = show_Requests[show_type].end_func;
  if (end_func == NULL)
    {
      return NO_ERROR;
    }

  error = (*end_func) (thread_p, &stsidp->ctx);
  return error;
}

/*
 *   showstmt_alloc_array_context () - init context for db_values arrays
 *   return: NO_ERROR, or ER_code
 *   thread_p(in):
 *   num_total(in):
 *   num_col(in):
 */
SHOWSTMT_ARRAY_CONTEXT *
showstmt_alloc_array_context (THREAD_ENTRY * thread_p, int num_total, int num_cols)
{
  SHOWSTMT_ARRAY_CONTEXT *ctx;

  ctx = (SHOWSTMT_ARRAY_CONTEXT *) db_private_alloc (thread_p, sizeof (SHOWSTMT_ARRAY_CONTEXT));
  if (ctx == NULL)
    {
      return NULL;
    }

  ctx->num_used = 0;
  ctx->num_cols = num_cols;
  ctx->num_total = num_total;
  ctx->tuples = (DB_VALUE **) db_private_alloc (thread_p, sizeof (DB_VALUE *) * num_total);
  if (ctx->tuples == NULL)
    {
      goto on_error;
    }

  memset (ctx->tuples, 0, sizeof (DB_VALUE *) * num_total);
  return ctx;

on_error:
  if (ctx != NULL)
    {
      db_private_free (thread_p, ctx);
    }
  return NULL;
}

/*
 *  showstmt_free_array_context () - free context for db_values arrays
 *   return: NO_ERROR, or ER_code
 *   thread_p(in):
 *   ctx(in):
 */
void
showstmt_free_array_context (THREAD_ENTRY * thread_p, SHOWSTMT_ARRAY_CONTEXT * ctx)
{
  int i, j;
  DB_VALUE *vals;

  assert (ctx != NULL);

  for (i = 0; i < ctx->num_used; i++)
    {
      vals = ctx->tuples[i];
      for (j = 0; j < ctx->num_cols; j++)
	{
	  pr_clear_value (&vals[j]);
	}

      db_private_free (thread_p, vals);
    }

  db_private_free (thread_p, ctx->tuples);
  db_private_free (thread_p, ctx);
}

/*
 *  showstmt_alloc_tuple_in_context () - alloc and return next tuple from context
 *   return:  tuple pointer
 *   thread_p(in):
 *   ctx(in):
 */
DB_VALUE *
showstmt_alloc_tuple_in_context (THREAD_ENTRY * thread_p, SHOWSTMT_ARRAY_CONTEXT * ctx)
{
  int i, num_new_total;
  DB_VALUE **new_tuples = NULL;
  DB_VALUE *vals = NULL;

  if (ctx->num_used == ctx->num_total)
    {
      num_new_total = (int) (ctx->num_total * 1.5 + 1);
      new_tuples = (DB_VALUE **) db_private_realloc (thread_p, ctx->tuples, sizeof (DB_VALUE *) * num_new_total);
      if (new_tuples == NULL)
	{
	  return NULL;
	}

      memset (new_tuples + ctx->num_total, 0, sizeof (DB_VALUE *) * (num_new_total - ctx->num_total));

      ctx->tuples = new_tuples;
      ctx->num_total = num_new_total;
    }

  vals = (DB_VALUE *) db_private_alloc (thread_p, sizeof (DB_VALUE) * ctx->num_cols);
  if (vals == NULL)
    {
      return NULL;
    }
  for (i = 0; i < ctx->num_cols; i++)
    {
      db_make_null (&vals[i]);
    }

  ctx->tuples[ctx->num_used++] = vals;
  return vals;
}

/*
 *  showstmt_array_next_scan () - next scan function for array
 *   return: NO_ERROR, or ER_code
 *   thread_p(in):
 *   cursor(in):
 *   out_values(in/out):
 *   out_cnt(in):
 *   ptr(in):
 */
static SCAN_CODE
showstmt_array_next_scan (THREAD_ENTRY * thread_p, int cursor, DB_VALUE ** out_values, int out_cnt, void *ptr)
{
  SHOWSTMT_ARRAY_CONTEXT *ctx = (SHOWSTMT_ARRAY_CONTEXT *) ptr;
  DB_VALUE *vals = NULL;
  int i;

  if (ctx == NULL || cursor < 0 || cursor >= ctx->num_used)
    {
      return S_END;
    }

  assert (out_cnt == ctx->num_cols);

  vals = ctx->tuples[cursor];

  for (i = 0; i < ctx->num_cols; i++)
    {
      pr_clone_value (&vals[i], out_values[i]);
    }

  return S_SUCCESS;
}

/*
 *  showstmt_array_end_scan () - end scan function for array
 *   return: NO_ERROR, or ER_code
 *   thread_p(in):
 *   ptr(in/out):
 */
static int
showstmt_array_end_scan (THREAD_ENTRY * thread_p, void **ptr)
{
  if (*ptr != NULL)
    {
      showstmt_free_array_context (thread_p, (SHOWSTMT_ARRAY_CONTEXT *) (*ptr));
      *ptr = NULL;
    }
  return NO_ERROR;
}

#if defined (SERVER_MODE)
//
// thread_scan_mapfunc () - mapper function to get information from thread entry for scanner
//
// thread_ref (in)      : mapped thread entry
// stop_mapper (out)    : output true to stop mapping
// caller_thread_p (in) : thread entry of show scan thread
// ctx (out)            : show scan array context
// error (out)          : output NO_ERROR or error code
//
static void
thread_scan_mapfunc (THREAD_ENTRY & thread_ref, bool & stop_mapper, THREAD_ENTRY * caller_thread_p,
		     SHOWSTMT_ARRAY_CONTEXT * ctx, int &error)
{
  DB_VALUE *vals = NULL;
  THREAD_ENTRY *thrd = &thread_ref;
  THREAD_ENTRY *next_thrd = NULL;
  size_t idx = 0;
  int ival;
  INT64 i64val;
  CSS_CONN_ENTRY *conn_entry = NULL;
  OR_ALIGNED_BUF (1024) a_buffer;
  char *buffer;
  char *area;
  int buf_len;
  HL_HEAPID private_heap_id;
  void *query_entry;
  LK_ENTRY *lockwait;
  time_t stime;
  int msecs;
  DB_DATETIME time_val;

  if (thrd->m_status == cubthread::entry::status::TS_DEAD)
    {
      // thread entry does not belong to a running thread; should not be shown in SHOW THREADS statement
      return;
    }

  vals = showstmt_alloc_tuple_in_context (caller_thread_p, ctx);
  if (vals == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      stop_mapper = true;
      return;
    }

  /* Index */
  db_make_int (&vals[idx], thrd->index);
  idx++;

  /* Jobq_index */// it is obsolete
  db_make_null (&vals[idx]);
  idx++;

  /* Thread_id */
  db_make_bigint (&vals[idx], (DB_BIGINT) thrd->get_posix_id ());
  idx++;

  /* Tran_index */
  ival = thrd->tran_index;
  if (ival >= 0)
    {
      db_make_int (&vals[idx], ival);
    }
  else
    {
      db_make_null (&vals[idx]);
    }
  idx++;

  /* Type */
  db_make_string (&vals[idx], thread_type_to_string (thrd->type));
  idx++;

  /* Status */
  db_make_string (&vals[idx], thread_status_to_string (thrd->m_status));
  idx++;

  /* Resume_status */
  db_make_string (&vals[idx], thread_resume_status_to_string (thrd->resume_status));
  idx++;

  /* Net_request */
  ival = thrd->net_request_index;
  if (ival != -1)
    {
      db_make_string (&vals[idx], get_net_request_name (ival));
    }
  else
    {
      db_make_null (&vals[idx]);
    }
  idx++;

  /* Conn_client_id */
  ival = thrd->client_id;
  if (ival != -1)
    {
      db_make_int (&vals[idx], ival);
    }
  else
    {
      db_make_null (&vals[idx]);
    }
  idx++;

  /* Conn_request_id */
  ival = thrd->rid;
  if (ival != 0)
    {
      db_make_int (&vals[idx], ival);
    }
  else
    {
      db_make_null (&vals[idx]);
    }
  idx++;

  /* Conn_index */
  conn_entry = thrd->conn_entry;
  if (conn_entry != NULL)
    {
      db_make_int (&vals[idx], conn_entry->idx);
    }
  else
    {
      db_make_null (&vals[idx]);
    }
  idx++;

  /* Last_error_code */
  ival = er_errid ();
  db_make_int (&vals[idx], ival);
  idx++;

  /* Last_error_msg */
  buffer = OR_ALIGNED_BUF_START (a_buffer);
  buf_len = 1024;

  if (ival != NO_ERROR)
    {
      char *ermsg;

      area = er_get_area_error (buffer, &buf_len);
      ermsg = er_get_ermsg_from_area_error (area);
      ermsg[255] = '\0';	/* truncate msg */

      error = db_make_string_copy (&vals[idx], ermsg);
      if (error != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  stop_mapper = true;
	  return;
	}
    }
  else
    {
      db_make_null (&vals[idx]);
    }
  idx++;

  /* Private_heap_id */
  buffer = OR_ALIGNED_BUF_START (a_buffer);
  buf_len = 1024;

  private_heap_id = thrd->private_heap_id;
  if (private_heap_id != 0)
    {
      snprintf (buffer, buf_len, "0x%08" PRIx64, (UINT64) private_heap_id);
      error = db_make_string_copy (&vals[idx], buffer);
      if (error != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  stop_mapper = true;
	  return;
	}
    }
  else
    {
      db_make_null (&vals[idx]);
    }
  idx++;

  /* Query_entry */
  buffer = OR_ALIGNED_BUF_START (a_buffer);
  buf_len = 1024;

  query_entry = thrd->query_entry;
  if (query_entry != NULL)
    {
      snprintf (buffer, buf_len, "0x%08" PRIx64, (UINT64) query_entry);
      error = db_make_string_copy (&vals[idx], buffer);
      if (error != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  stop_mapper = true;
	  return;
	}
    }
  else
    {
      db_make_null (&vals[idx]);
    }
  idx++;

  /* Interrupted */
  db_make_int (&vals[idx], thrd->interrupted);
  idx++;

  /* Shutdown */
  db_make_int (&vals[idx], thrd->shutdown);
  idx++;

  /* Check_interrupt */
  db_make_int (&vals[idx], thrd->check_interrupt);
  idx++;

  /* Wait_for_latch_promote */
  db_make_int (&vals[idx], thrd->wait_for_latch_promote);
  idx++;

  buffer = OR_ALIGNED_BUF_START (a_buffer);
  buf_len = 1024;

  lockwait = (LK_ENTRY *) thrd->lockwait;
  if (lockwait != NULL)
    {
      /* lockwait_blocked_mode */
      strncpy (buffer, LOCK_TO_LOCKMODE_STRING (lockwait->blocked_mode), buf_len);
      buffer[buf_len - 1] = '\0';
      trim (buffer);
      error = db_make_string_copy (&vals[idx], buffer);
      if (error != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  stop_mapper = true;
	  return;
	}
      idx++;

      /* Lockwait_start_time */
      i64val = thrd->lockwait_stime;
      stime = (time_t) (i64val / 1000LL);
      msecs = i64val % 1000;
      db_localdatetime_msec (&stime, msecs, &time_val);
      error = db_make_datetime (&vals[idx], &time_val);
      if (error != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  stop_mapper = true;
	  return;
	}
      idx++;

      /* Lockwait_msecs */
      db_make_int (&vals[idx], thrd->lockwait_msecs);
      idx++;

      /* Lockwait_state */
      db_make_string (&vals[idx], lock_wait_state_to_string (thrd->lockwait_state));
      idx++;
    }
  else
    {
      /* lockwait_blocked_mode */
      db_make_null (&vals[idx]);
      idx++;

      /* Lockwait_start_time */
      db_make_null (&vals[idx]);
      idx++;

      /* Lockwait_msecs */
      db_make_null (&vals[idx]);
      idx++;

      /* Lockwait_state */
      db_make_null (&vals[idx]);
      idx++;
    }

  /* Next_wait_thread_index */
  next_thrd = thrd->next_wait_thrd;
  if (next_thrd != NULL)
    {
      db_make_int (&vals[idx], next_thrd->index);
    }
  else
    {
      db_make_null (&vals[idx]);
    }
  idx++;

  /* Next_tran_wait_thread_index */
  next_thrd = thrd->tran_next_wait;
  if (next_thrd != NULL)
    {
      db_make_int (&vals[idx], next_thrd->index);
    }
  else
    {
      db_make_null (&vals[idx]);
    }
  idx++;

  /* Next_worker_thread_index */
  next_thrd = thrd->worker_thrd_list;
  if (next_thrd != NULL)
    {
      db_make_int (&vals[idx], next_thrd->index);
    }
  else
    {
      db_make_null (&vals[idx]);
    }
  idx++;

  assert (idx == THREAD_SCAN_COLUMN_COUNT);
}
#endif // SERVER_MODE

/*
 * thread_start_scan () -  start scan function for show threads
 *   return: NO_ERROR, or ER_code
 *
 *   thread_p(in):
 *   type (in):
 *   arg_values(in):
 *   arg_cnt(in):
 *   ptr(in/out):
 */
int
thread_start_scan (THREAD_ENTRY * thread_p, int type, DB_VALUE ** arg_values, int arg_cnt, void **ptr)
{
#if defined(SERVER_MODE)
  SHOWSTMT_ARRAY_CONTEXT *ctx = NULL;
  int error = NO_ERROR;

  *ptr = NULL;

  ctx = showstmt_alloc_array_context (thread_p, (int) thread_num_total_threads (), THREAD_SCAN_COLUMN_COUNT);
  if (ctx == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      return error;
    }

  // scan all threads
  thread_get_manager ()->map_entries (thread_scan_mapfunc, thread_p, ctx, error);

  if (error == NO_ERROR)
    {
      *ptr = ctx;
    }
  else
    {
      ASSERT_ERROR ();
      showstmt_free_array_context (thread_p, ctx);
    }

  return error;
#else // not SERVER_MODE
  return NO_ERROR;
#endif // not SERVER_MODE
}
