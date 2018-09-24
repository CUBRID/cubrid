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
 * replication.c -
 */

#ident "$Id$"

#include <stdio.h>
#include <assert.h>
#include "replication.h"
#include "replication_object.hpp"
#include "object_primitive.h"
#include "heap_file.h"
#include "dbtype.h"
/*
 * EXTERN TO ALL SERVER RECOVERY FUNCTION CODED SOMEWHERE ELSE
 */

#define 	REPL_LOG_IS_NOT_EXISTS(tran_index)                            \
               (log_Gl.trantable.all_tdes[(tran_index)]->num_repl_records == 0)
#define 	REPL_LOG_IS_FULL(tran_index)                                  \
               (log_Gl.trantable.all_tdes[(tran_index)]->num_repl_records     \
                 == log_Gl.trantable.all_tdes[(tran_index)]->cur_repl_record+1)

static const int REPL_LOG_INFO_ALLOC_SIZE = 100;

#if defined(SERVER_MODE) || defined(SA_MODE)
static int repl_log_info_alloc (LOG_TDES * tdes, int arr_size, bool need_realloc);
#endif /* SERVER_MODE || SA_MODE */

#if defined(SERVER_MODE) || defined(SA_MODE)

/*
 * repl_log_info_alloc - log info area allocation
 *
 * return: Error Code
 *
 *   tdes(in): transaction descriptor
 *   arr_size(in): array size to be allocated
 *   need_realloc(in): 0-> initial allocation (malloc), otherwise "realloc"
 *
 * NOTE:This function allocates the memory for the log info area of the
 *       target transaction. It is called when the transaction tries to to
 *   insert/update/delete operation. If the transaction do read operation
 *   only, no memory is allocated.
 *
 *   The allocation size is defined by a constant - REPL_LOG_INFO_ALLOC_SIZE
 *   We need to set the size for the user request ?
 */
static int
repl_log_info_alloc (LOG_TDES * tdes, int arr_size, bool need_realloc)
{
  int i = 0, k;
  int error = NO_ERROR;

  if (need_realloc == false)
    {
      i = arr_size * DB_SIZEOF (LOG_REPL_RECORD);
      tdes->repl_records = (LOG_REPL_RECORD *) malloc (i);
      if (tdes->repl_records == NULL)
	{
	  error = ER_REPL_ERROR;
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_REPL_ERROR, 1, "can't allocate memory");
	  return error;
	}
      tdes->num_repl_records = arr_size;
      k = 0;
    }
  else
    {
      i = tdes->num_repl_records + arr_size;
      tdes->repl_records = (LOG_REPL_RECORD *) realloc (tdes->repl_records, i * DB_SIZEOF (LOG_REPL_RECORD));
      if (tdes->repl_records == NULL)
	{
	  error = ER_REPL_ERROR;
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_REPL_ERROR, 1, "can't allocate memory");
	  return error;
	}
      k = tdes->num_repl_records;
      tdes->num_repl_records = i;
    }

  for (i = k; i < tdes->num_repl_records; i++)
    {
      tdes->repl_records[i].repl_data = NULL;
    }

  return error;
}

/*
 * repl_start_flush_mark -
 *
 * return:
 *
 * NOTE:start to mark "must_flush" for repl records to be appended after this
 */
void
repl_start_flush_mark (THREAD_ENTRY * thread_p)
{
  LOG_TDES *tdes;

  tdes = LOG_FIND_CURRENT_TDES (thread_p);

  if (tdes == NULL)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_UNKNOWN_TRANINDEX, 1,
	      LOG_FIND_THREAD_TRAN_INDEX (thread_p));
      return;
    }
  if (tdes->fl_mark_repl_recidx == -1)
    {
      tdes->fl_mark_repl_recidx = tdes->cur_repl_record;
    }				/* else already started, return */
  return;
}

/*
 * repl_end_flush_mark -
 *
 * return:
 *
 *   need_undo(in):
 *
 * NOTE:end to mark "must_flush" for repl records
 */
void
repl_end_flush_mark (THREAD_ENTRY * thread_p, bool need_undo)
{
  LOG_TDES *tdes;
  int i;

  tdes = LOG_FIND_CURRENT_TDES (thread_p);

  if (tdes == NULL)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_UNKNOWN_TRANINDEX, 1,
	      LOG_FIND_THREAD_TRAN_INDEX (thread_p));
      return;
    }
  if (need_undo)
    {
      LOG_REPL_RECORD *recsp = tdes->repl_records;
      for (i = tdes->fl_mark_repl_recidx; i < tdes->cur_repl_record; i++)
	{
	  /* initialize repl records to be marked as flush */
	  free_and_init (recsp[i].repl_data);
	}
      tdes->cur_repl_record = tdes->fl_mark_repl_recidx;
    }
  tdes->fl_mark_repl_recidx = -1;

  return;
}

/*
 * repl_log_abort_after_lsa -
 *
 * return:
 *
 *   tdes (in) :
 *   start_lsa (in) :
 *
 */
int
repl_log_abort_after_lsa (LOG_TDES * tdes, LOG_LSA * start_lsa)
{
  // todo - abort partial
  // http://jira.cubrid.org/browse/CBRD-22214

  return NO_ERROR;
}

#endif /* SERVER_MODE || SA_MODE */
