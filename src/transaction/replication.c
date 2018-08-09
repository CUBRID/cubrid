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
 * repl_data_insert_log_dump - dump the "DATA INSERT" replication log
 *
 * return:
 *
 *   length(in): length of the data
 *   data(in): log data
 *
 * NOTE:
 */
void
repl_data_insert_log_dump (FILE * fp, int length, void *data)
{
  char *class_name;
  DB_VALUE key;
  char *ptr;

  ptr = or_unpack_string_nocopy ((char *) data, &class_name);
  ptr = or_unpack_mem_value (ptr, &key);
  fprintf (fp, "      class_name: %s\n", class_name);
  fprintf (fp, "      pk_value: ");
  db_value_print (&key);
  pr_clear_value (&key);
  fprintf (fp, "\n");
  fflush (fp);
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * repl_data_udpate_log_dump - dump the "DATA UPDATE" replication log
 *
 * return:
 *
 *   length(in): length of the data
 *   data(in): log data
 *
 * NOTE:
 */
void
repl_data_udpate_log_dump (FILE * fp, int length, void *data)
{
  /* currently same logic as insert case, but I can't gaurantee it's true after this... */
  repl_data_insert_log_dump (fp, length, data);
}

/*
 * repl_data_delete_log_dump - dump the "DATA DELETE" replication log
 *
 * return:
 *
 *   length(in): length of the data
 *   data(in): log data
 *
 * NOTE:
 */
void
repl_data_delete_log_dump (FILE * fp, int length, void *data)
{
  /* currently same logic as insert case, but I can't gaurantee it's true after this... */
  repl_data_insert_log_dump (fp, length, data);
}

#endif

/*
 * repl_schema_log_dump -
 *
 * return:
 *
 *   length(in):
 *   data(in):
 *
 * NOTE:
 */
void
repl_schema_log_dump (FILE * fp, int length, void *data)
{
  int type;
  char *class_name, *ddl;
  char *ptr;

  ptr = or_unpack_int ((char *) data, &type);
  ptr = or_unpack_string_nocopy (ptr, &class_name);
  ptr = or_unpack_string_nocopy (ptr, &ddl);
  fprintf (fp, "      type: %d\n", type);
  fprintf (fp, "      class_name: %s\n", class_name);
  fprintf (fp, "      DDL: %s\n", ddl);
  fflush (fp);
}

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
 * repl_add_update_lsa - update the LSA of the target transaction lo
 *
 * return: NO_ERROR or error code
 *
 *   inst_oid(in): OID of the instance
 *
 * NOTE:For update operation, the server does "Heap stuff" after "Index Stuff".
 *     In order to reduce the cost of replication log, we generates a
 *     replication log at the point of indexing  processing step.
 *     (During index processing, the primary key value is fetched ..)
 *     After Heap operation, we have to set the target LSA into the
 *     replication log.
 *
 *     For update case, this function is called by locator_update_force().
 *     In the case of insert/delete cases, when the replication log info. is
 *     generated, the server already has the target transaction log(HEAP_INSERT
 *     or HEAP_DELETE).
 *     But, for the udpate case, the server doesn't has the target log when
 *     it generates the replication log. So, the server has to find out the
 *     location of replication record and match with the target transaction
 *     log after heap_update(). This is done by locator_update_force().
 */
int
repl_add_update_lsa (THREAD_ENTRY * thread_p, const OID * inst_oid)
{
  int tran_index;
  LOG_TDES *tdes;
  LOG_REPL_RECORD *repl_rec;
  int i;
  bool find = false;
  int error = NO_ERROR;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);

  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL)
    {
      return ER_FAILED;
    }

  /* If suppress_replication flag is set, do not write replication log. */
  if (tdes->suppress_replication != 0)
    {
      return NO_ERROR;
    }

  for (i = tdes->cur_repl_record - 1; i >= 0; i--)
    {
      repl_rec = (LOG_REPL_RECORD *) (&tdes->repl_records[i]);
      if (OID_EQ (&repl_rec->inst_oid, inst_oid) && !LSA_ISNULL (&tdes->repl_update_lsa))
	{
	  assert (repl_rec->rcvindex == RVREPL_DATA_UPDATE || repl_rec->rcvindex == RVREPL_DATA_UPDATE_START
		  || repl_rec->rcvindex == RVREPL_DATA_UPDATE_END);
	  if (repl_rec->rcvindex == RVREPL_DATA_UPDATE || repl_rec->rcvindex == RVREPL_DATA_UPDATE_START
	      || repl_rec->rcvindex == RVREPL_DATA_UPDATE_END)
	    {
	      LSA_COPY (&repl_rec->lsa, &tdes->repl_update_lsa);
	      LSA_SET_NULL (&tdes->repl_update_lsa);
	      LSA_SET_NULL (&tdes->repl_insert_lsa);
	      find = true;
	      break;
	    }
	}
    }

  if (find == false)
    {
      er_log_debug (ARG_FILE_LINE, "can't find out the UPDATE LSA");
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
  LOG_REPL_RECORD *repl_rec_arr;
  int i;

  repl_rec_arr = tdes->repl_records;
  for (i = 0; i < tdes->cur_repl_record; i++)
    {
      if (LSA_GT (&repl_rec_arr[i].lsa, start_lsa))
	{
	  repl_rec_arr[i].must_flush = LOG_REPL_DONT_NEED_FLUSH;
	}
    }

  return NO_ERROR;
}

#if defined(CUBRID_DEBUG)
/*
 * repl_debug_info - DEBUGGING Function, print out the replication log info.
 *
 * return:
 *
 * NOTE:Dump the replication log info to the stdout.
 */
void
repl_debug_info ()
{
  LOG_TDES *tdes;
  LOG_REPL_RECORD *repl_rec;
  int tnum, rnum;
  char *class_name;
  DB_VALUE key;
  char *ptr;

  fprintf (stdout, "REPLICATION LOG DUMP -- Memory\n");
  for (tnum = 0; tnum < log_Gl.trantable.num_assigned_indices; tnum++)
    {
      fprintf (stdout, "*************************************************\n");
      tdes = log_Gl.trantable.all_tdes[tnum];
      fprintf (stdout, "For the Trid : %d\n", (int) tdes->trid);
      for (rnum = 0; rnum < tdes->cur_repl_record; rnum++)
	{
	  fprintf (stdout, "   RECORD # %d\n", rnum);
	  repl_rec = (LOG_REPL_RECORD *) (&tdes->repl_records[rnum]);
	  fprintf (stdout, "      type: %s\n", log_to_string (repl_rec->repl_type));
	  fprintf (stdout, "      OID: %d - %d - %d\n", repl_rec->inst_oid.volid, repl_rec->inst_oid.pageid,
		   repl_rec->inst_oid.slotid);
	  ptr = or_unpack_string_nocopy (repl_rec->repl_data, &class_name);
	  ptr = or_unpack_mem_value (ptr, &key);
	  fprintf (stdout, "      class_name: %s\n", class_name);
	  fprintf (stdout, "      LSA: %lld | %d\n", repl_rec->lsa.pageid, repl_rec->lsa.offset);
	  db_value_print (&key);
	  fprintf (stdout, "\n----------------------------------------------\n");
	  pr_clear_value (&key);
	}
    }
  fflush (stdout);
}
#endif /* CUBRID_DEBUG */

#endif /* SERVER_MODE || SA_MODE */
