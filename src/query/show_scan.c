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
 * show_scan.c - scan information for show statements
 */

#ident "$Id$"


#include "config.h"

#include <stdio.h>
#include <string.h>

#include "porting.h"
#include "error_manager.h"
#include "memory_alloc.h"


#include "query_manager.h"
#include "object_primitive.h"
#include "scan_manager.h"
#include "show_scan.h"

#include "disk_manager.h"
#include "log_manager.h"
#include "slotted_page.h"
#include "heap_file.h"
#include "btree.h"
#include "connection_support.h"

#if defined(SERVER_MODE)
#include "thread.h"
#endif /* SERVER_MODE */

#if defined(ENABLE_SYSTEMTAP)
#include "probes.h"
#endif /* ENABLE_SYSTEMTAP */

typedef SCAN_CODE (*NEXT_SCAN_FUNC) (THREAD_ENTRY * thread_p, int cursor,
				     DB_VALUE ** out_values, int out_cnt,
				     void *ctx);
typedef int (*START_SCAN_FUNC) (THREAD_ENTRY * thread_p, int show_type,
				DB_VALUE ** arg_values, int arg_cnt,
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

static SCAN_CODE showstmt_array_next_scan (THREAD_ENTRY * thread_p,
					   int cursor, DB_VALUE ** out_values,
					   int out_cnt, void *ptr);
static int showstmt_array_end_scan (THREAD_ENTRY * thread_p, void **ptr);


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
  req->next_func = css_user_access_status_next_scan;
  req->end_func = css_user_access_status_end_scan;

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

  next_func = show_Requests[show_type].next_func;
  assert (next_func != NULL
	  && show_type == show_Requests[show_type].show_type);

  /* free values which need be cleared */
  for (i = 0; i < stsidp->out_cnt; i++)
    {
      pr_clear_value (stsidp->out_values[i]);
    }
  code = (*next_func) (thread_p, stsidp->cursor++, stsidp->out_values,
		       stsidp->out_cnt, stsidp->ctx);
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

  error = (*start_func) (thread_p, (int) show_type, stsidp->arg_values,
			 stsidp->arg_cnt, &stsidp->ctx);
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
showstmt_alloc_array_context (THREAD_ENTRY * thread_p, int num_total,
			      int num_cols)
{
  SHOWSTMT_ARRAY_CONTEXT *ctx;

  ctx = db_private_alloc (thread_p, sizeof (SHOWSTMT_ARRAY_CONTEXT));
  if (ctx == NULL)
    {
      return NULL;
    }

  ctx->num_used = 0;
  ctx->num_cols = num_cols;
  ctx->num_total = num_total;
  ctx->tuples = db_private_alloc (thread_p, sizeof (DB_VALUE *) * num_total);
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
showstmt_free_array_context (THREAD_ENTRY * thread_p,
			     SHOWSTMT_ARRAY_CONTEXT * ctx)
{
  int i, j;
  DB_VALUE *vals;

  assert (ctx != NULL);

  for (i = 0; i < ctx->num_used; i++)
    {
      vals = ctx->tuples[i];
      for (j = 0; j < ctx->num_cols; j++)
	{
	  db_value_clear (&vals[j]);
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
showstmt_alloc_tuple_in_context (THREAD_ENTRY * thread_p,
				 SHOWSTMT_ARRAY_CONTEXT * ctx)
{
  int i, num_new_total;
  DB_VALUE **new_tuples = NULL;
  DB_VALUE *vals = NULL;

  if (ctx->num_used == ctx->num_total)
    {
      num_new_total = ctx->num_total * 1.5 + 1;
      new_tuples =
	(DB_VALUE **) db_private_realloc (thread_p, ctx->tuples,
					  sizeof (DB_VALUE *) *
					  num_new_total);
      if (new_tuples == NULL)
	{
	  return NULL;
	}

      memset (new_tuples + ctx->num_total, 0,
	      sizeof (DB_VALUE *) * (num_new_total - ctx->num_total));

      ctx->tuples = new_tuples;
      ctx->num_total = num_new_total;
    }

  vals =
    (DB_VALUE *) db_private_alloc (thread_p,
				   sizeof (DB_VALUE) * ctx->num_cols);
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
showstmt_array_next_scan (THREAD_ENTRY * thread_p, int cursor,
			  DB_VALUE ** out_values, int out_cnt, void *ptr)
{
  SHOWSTMT_ARRAY_CONTEXT *ctx = (SHOWSTMT_ARRAY_CONTEXT *) ptr;
  DB_VALUE *vals = NULL;
  int i;

  if (cursor < 0 || cursor >= ctx->num_used)
    {
      return S_END;
    }

  assert (out_cnt == ctx->num_cols);

  vals = ctx->tuples[cursor];

  for (i = 0; i < ctx->num_cols; i++)
    {
      db_value_clone (&vals[i], out_values[i]);
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
      showstmt_free_array_context (thread_p,
				   (SHOWSTMT_ARRAY_CONTEXT *) (*ptr));
      *ptr = NULL;
    }
  return NO_ERROR;
}
