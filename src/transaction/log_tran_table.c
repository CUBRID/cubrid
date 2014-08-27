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
 * log_tran_table.c -
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <limits.h>
#if defined(SOLARIS)
/* for MAXHOSTNAMELEN */
#include <netdb.h>
#endif /* SOLARIS */
#include <sys/stat.h>
#include <assert.h>

#include "porting.h"
#include "xserver_interface.h"
#include "log_impl.h"
#include "log_manager.h"
#include "log_comm.h"
#include "recovery.h"
#if !defined(SERVER_MODE)
#include "recovery_cl.h"
#endif /* SERVER_MODE */
#include "system_parameter.h"
#include "release_string.h"
#include "memory_alloc.h"
#include "error_manager.h"
#include "storage_common.h"
#include "file_io.h"
#include "disk_manager.h"
#include "page_buffer.h"
#include "lock_manager.h"
#include "wait_for_graph.h"
#include "file_manager.h"
#include "critical_section.h"
#include "query_manager.h"
#include "perf_monitor.h"
#include "object_representation.h"
#include "connection_defs.h"
#if defined(SERVER_MODE)
#include "thread.h"
#endif /* SERVER_MODE */
#include "rb_tree.h"
#include "mvcc.h"
#include "vacuum.h"
#include "partition.h"
#include "btree_load.h"

#if defined(SERVER_MODE) || defined(SA_MODE)
#include "replication.h"
#endif

#define NUM_ASSIGNED_TRAN_INDICES log_Gl.trantable.num_assigned_indices
#define NUM_TOTAL_TRAN_INDICES log_Gl.trantable.num_total_indices

static const int LOG_MAX_NUM_CONTIGUOUS_TDES = INT_MAX / sizeof (LOG_TDES);
static const float LOG_EXPAND_TRANTABLE_RATIO = 1.25;	/* Increase table by 25% */
static const int LOG_TOPOPS_STACK_INCREMENT = 3;	/* No more than 3 nested
							 * top system operations
							 */
static const char *log_Client_id_unknown_string = "(unknown)";
static BOOT_CLIENT_CREDENTIAL log_Client_credential = {
  BOOT_CLIENT_SYSTEM_INTERNAL,	/* client_type */
  NULL,				/* client_info */
  NULL,				/* db_name */
  NULL,				/* db_user */
  NULL,				/* db_password */
  (char *) "(system)",		/* program_name */
  NULL,				/* login_name */
  NULL,				/* host_name */
  NULL,				/* preferred_hosts */
  0,				/* connect_order */
  -1				/* process_id */
};

static int logtb_expand_trantable (THREAD_ENTRY * thread_p,
				   int num_new_indices);
static int logtb_allocate_tran_index (THREAD_ENTRY * thread_p, TRANID trid,
				      TRAN_STATE state,
				      const BOOT_CLIENT_CREDENTIAL *
				      client_credential,
				      TRAN_STATE * current_state,
				      int wait_msecs,
				      TRAN_ISOLATION isolation);
static void logtb_initialize_tdes (LOG_TDES * tdes, int tran_index);
static LOG_ADDR_TDESAREA *logtb_allocate_tdes_area (int num_indices);
static int logtb_alloc_mvcc_info_block (THREAD_ENTRY * thread_p);
static void logtb_initialize_trantable (TRANTABLE * trantable_p);
static int logtb_initialize_system_tdes (THREAD_ENTRY * thread_p);
static void logtb_set_number_of_assigned_tran_indices (int num_trans);
static void logtb_increment_number_of_assigned_tran_indices ();
static void logtb_decrement_number_of_assigned_tran_indices ();
static void logtb_set_number_of_total_tran_indices (int num_total_trans);
static void logtb_set_loose_end_tdes (LOG_TDES * tdes);
static bool logtb_is_interrupted_tdes (THREAD_ENTRY * thread_p,
				       LOG_TDES * tdes, bool clear,
				       bool * continue_checking);
static void logtb_dump_tdes_distribute_transaction (FILE * out_fp,
						    int global_tran_id,
						    LOG_2PC_COORDINATOR *
						    coord);
static void logtb_dump_top_operations (FILE * out_fp,
				       LOG_TOPOPS_STACK * topops_p);
static void logtb_dump_tdes (FILE * out_fp, LOG_TDES * tdes);
static void logtb_set_tdes (THREAD_ENTRY * thread_p, LOG_TDES * tdes,
			    const BOOT_CLIENT_CREDENTIAL * client_credential,
			    int wait_msecs, TRAN_ISOLATION isolation);
static int logtb_initialize_mvcctable (THREAD_ENTRY * thread_p);
static void logtb_finalize_mvcctable (THREAD_ENTRY * thread_p);
static int logtb_get_mvcc_snapshot_data (THREAD_ENTRY * thread_p);

static void logtb_mvcc_free_class_unique_stats (LOG_MVCC_CLASS_UPDATE_STATS *
						class_stats);
static void logtb_mvcc_free_update_stats (LOG_MVCC_UPDATE_STATS *
					  log_upd_stats);
static void logtb_mvcc_clear_update_stats (LOG_MVCC_UPDATE_STATS *
					   log_upd_stats);
static LOG_MVCC_CLASS_UPDATE_STATS
  * logtb_mvcc_alloc_class_stats (LOG_MVCC_UPDATE_STATS * log_upd_stats);
static void logtb_mvcc_free_class_stats (LOG_MVCC_UPDATE_STATS *
					 log_upd_stats,
					 LOG_MVCC_CLASS_UPDATE_STATS * entry);
static LOG_MVCC_CLASS_UPDATE_STATS
  * logtb_mvcc_create_class_stats (THREAD_ENTRY * thread_p,
				   const OID * class_oid);
static LOG_MVCC_BTID_UNIQUE_STATS
  * logtb_mvcc_create_btid_unique_stats (THREAD_ENTRY * thread_p,
					 LOG_MVCC_CLASS_UPDATE_STATS *
					 class_stats, const BTID * btid);
static int logtb_mvcc_reflect_unique_statistics (THREAD_ENTRY * thread_p);
static int logtb_mvcc_load_global_statistics (THREAD_ENTRY * thread_p);
static int logtb_create_unique_stats_from_repr (THREAD_ENTRY * thread_p,
						LOG_MVCC_CLASS_UPDATE_STATS *
						class_stats);

/*
 * logtb_realloc_topops_stack - realloc stack of top system operations
 *
 * return: stack or NULL
 *
 *   tdes(in): State structure of transaction to realloc stack
 *   num_elms(in):
 *
 * Note: Realloc the current transaction top system operation stack by
 *              the given number of entries.
 */
void *
logtb_realloc_topops_stack (LOG_TDES * tdes, int num_elms)
{
  size_t size;
  void *newptr;

  if (num_elms < LOG_TOPOPS_STACK_INCREMENT)
    {
      num_elms = LOG_TOPOPS_STACK_INCREMENT;
    }

  size = tdes->topops.max + num_elms;
  size = size * sizeof (*tdes->topops.stack);

  newptr = (struct log_topops_addresses *) realloc (tdes->topops.stack, size);
  if (newptr != NULL)
    {
      tdes->topops.stack = (struct log_topops_addresses *) newptr;
      if (tdes->topops.max == 0)
	{
	  tdes->topops.last = -1;
	}
      tdes->topops.max += num_elms;
    }
  else
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, size);
      return NULL;
    }
  return tdes->topops.stack;
}

/*
 * logtb_allocate_tdes_area -
 *
 * return:
 *
 *   num_indices(in):
 *
 * Note:
 */
static LOG_ADDR_TDESAREA *
logtb_allocate_tdes_area (int num_indices)
{
  LOG_ADDR_TDESAREA *area;	/* Contiguous area for new
				 * transaction indices
				 */
  LOG_TDES *tdes;		/* Transaction descriptor */
  int i, tran_index;
  size_t area_size;

  /*
   * Allocate an area for the transaction descriptors, set the address of
   * each transaction descriptor, and keep the address of the area for
   * deallocation purposes at shutdown time.
   */
  area_size = num_indices * sizeof (LOG_TDES) + sizeof (LOG_ADDR_TDESAREA);
  area = (LOG_ADDR_TDESAREA *) malloc (area_size);
  if (area == NULL)
    {
      return NULL;
    }

  area->tdesarea = ((LOG_TDES *) ((char *) area
				  + sizeof (LOG_ADDR_TDESAREA)));
  area->next = log_Gl.trantable.area;

  /*
   * Initialize every newly created transaction descriptor index
   */
  for (i = 0, tran_index = NUM_TOTAL_TRAN_INDICES;
       i < num_indices; tran_index++, i++)
    {
      tdes = log_Gl.trantable.all_tdes[tran_index] = &area->tdesarea[i];
      logtb_initialize_tdes (tdes, i);
    }

  return area;
}

/*
 * logtb_expand_trantable - expand the transaction table
 *
 * return: NO_ERROR if all OK, ER_ status otherwise
 *
 *   num_new_indices(in): Number of indices of expansion.
 *                       (i.e., threads/clients of execution)
 *
 * Note: Expand the transaction table with the number of given indices.
 */
static int
logtb_expand_trantable (THREAD_ENTRY * thread_p, int num_new_indices)
{
  LOG_ADDR_TDESAREA *area;	/* Contiguous area for new transaction indices */
  int total_indices;		/* Total number of transaction indices */
  int i;
  int error_code = NO_ERROR;

#if defined(SERVER_MODE)
  /*
   * When second time this function invoked during normal processing,
   * just return.
   */
  total_indices = MAX_NTRANS;
  if (log_Gl.rcv_phase == LOG_RESTARTED
      && total_indices <= NUM_TOTAL_TRAN_INDICES)
    {
      return NO_ERROR;
    }
#endif /* SERVER_MODE */

  while (num_new_indices > LOG_MAX_NUM_CONTIGUOUS_TDES)
    {
      error_code =
	logtb_expand_trantable (thread_p, LOG_MAX_NUM_CONTIGUOUS_TDES);
      if (error_code != NO_ERROR)
	{
	  goto error;
	}
      num_new_indices -= LOG_MAX_NUM_CONTIGUOUS_TDES;
    }

  if (num_new_indices <= 0)
    {
      return NO_ERROR;
    }

#if defined(SERVER_MODE)
  if (log_Gl.rcv_phase != LOG_RESTARTED)
    {
      total_indices = NUM_TOTAL_TRAN_INDICES + num_new_indices;
    }
#else /* SERVER_MODE */
  total_indices = NUM_TOTAL_TRAN_INDICES + num_new_indices;
#endif

  /*
   * NOTE that this realloc is OK since we are in a critical section.
   * Nobody should have pointer to transaction table
   */
  i = total_indices * sizeof (*log_Gl.trantable.all_tdes);
  log_Gl.trantable.all_tdes =
    (LOG_TDES **) realloc (log_Gl.trantable.all_tdes, i);
  if (log_Gl.trantable.all_tdes == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, i);
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      goto error;
    }

  area = logtb_allocate_tdes_area (num_new_indices);
  if (area == NULL)
    {
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      goto error;
    }

  /*
   * Notify other modules of new number of transaction indices
   */
#if defined(ENABLE_UNUSED_FUNCTION)
  error_code = wfg_alloc_nodes (thread_p, total_indices);
  if (error_code != NO_ERROR)
    {
      free_and_init (area);
      goto error;
    }
#endif

  if (qmgr_allocate_tran_entries (thread_p, total_indices) != NO_ERROR)
    {
      free_and_init (area);
      error_code = ER_FAILED;
      goto error;
    }

  log_Gl.trantable.area = area;
  log_Gl.trantable.hint_free_index = NUM_TOTAL_TRAN_INDICES;
  logtb_set_number_of_total_tran_indices (total_indices);

  return error_code;

  /* **** */
error:
  return error_code;
}

/*
 * logtb_define_trantable -  define the transaction table
 *
 * return: nothing
 *
 *   num_expected_tran_indices(in): Number of expected concurrent transactions
 *                                 (i.e., threads/clients of execution)
 *   num_expected_locks(in): Number of expected locks
 *
 * Note: Define the transaction table which is used to support the
 *              number of expected transactions.
 */
void
logtb_define_trantable (THREAD_ENTRY * thread_p,
			int num_expected_tran_indices, int num_expected_locks)
{
  LOG_SET_CURRENT_TRAN_INDEX (thread_p, LOG_SYSTEM_TRAN_INDEX);

  LOG_CS_ENTER (thread_p);
  TR_TABLE_CS_ENTER (thread_p);

  if (logpb_is_initialize_pool ())
    {
      logpb_finalize_pool ();
    }

  (void) logtb_define_trantable_log_latch (thread_p,
					   num_expected_tran_indices);

  LOG_SET_CURRENT_TRAN_INDEX (thread_p, LOG_SYSTEM_TRAN_INDEX);

  TR_TABLE_CS_EXIT (thread_p);
  LOG_CS_EXIT (thread_p);
}

/*
 * logtb_define_trantable_log_latch - define the transaction table
 *
 * return: NO_ERROR if all OK, ER status otherwise
 *
 *   num_expected_tran_indices(in): Number of expected concurrent transactions
 *                                 (i.e., threads/clients of execution)
 *   num_expected_locks(in): Number of expected locks
 *
 * Note: This function is only called by the log manager when the log
 *              latch has already been acquired. (See logtb_define_trantable for
 *              other uses).
 */
int
logtb_define_trantable_log_latch (THREAD_ENTRY * thread_p,
				  int num_expected_tran_indices)
{
  int error_code = NO_ERROR;

  assert (LOG_CS_OWN_WRITE_MODE (thread_p));

  /*
   * for XA support: there is prepared transaction after recovery.
   *                 so, can not recreate transaction description
   *                 table after recovery.
   *
   * Total number of transaction descriptor is set to the value of
   * MAX_NTRANS
   */
  num_expected_tran_indices = MAX (num_expected_tran_indices, MAX_NTRANS);

  num_expected_tran_indices = MAX (num_expected_tran_indices,
				   LOG_SYSTEM_TRAN_INDEX + 1);

  /* If there is an already defined table, free such a table */
  if (log_Gl.trantable.area != NULL)
    {
      logtb_undefine_trantable (thread_p);
    }
  else
    {
      /* Initialize the transaction table as empty */
      logtb_initialize_trantable (&log_Gl.trantable);
    }

  /*
   * Create an area to keep the number of desired transaction descriptors
   */

  error_code = logtb_expand_trantable (thread_p, num_expected_tran_indices);
  if (error_code != NO_ERROR)
    {
      /*
       * Unable to create transaction table to hold the desired number
       * of indices. Probabely, a lot of indices were requested.
       * try again with defaults.
       */
      if (log_Gl.trantable.area != NULL)
	{
	  logtb_undefine_trantable (thread_p);
	}

#if defined(SERVER_MODE)
      if (num_expected_tran_indices <= LOG_ESTIMATE_NACTIVE_TRANS
	  || log_Gl.rcv_phase == LOG_RESTARTED)
#else /* SERVER_MODE */
      if (num_expected_tran_indices <= LOG_ESTIMATE_NACTIVE_TRANS)
#endif /* SERVER_MODE */
	{
	  /* Out of memory */
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			     "log_def_trantable");
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
      else
	{
	  error_code =
	    logtb_define_trantable_log_latch (thread_p,
					      LOG_ESTIMATE_NACTIVE_TRANS);
	  return error_code;
	}
    }

  logtb_set_number_of_assigned_tran_indices (1);	/* sys tran */

  /*
   * Assign the first entry for the system transaction. System transaction
   * has an infinite timeout
   */
  error_code = logtb_initialize_system_tdes (thread_p);
  if (error_code != NO_ERROR)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_LOG_UNKNOWN_TRANINDEX, 1, LOG_SYSTEM_TRAN_INDEX);
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_def_trantable");
      return error_code;
    }

  LOG_SET_CURRENT_TRAN_INDEX (thread_p, LOG_SYSTEM_TRAN_INDEX);

  if (mvcc_Enabled)
    {
      error_code = logtb_initialize_mvcctable (thread_p);
      if (error_code != NO_ERROR)
	{
	  goto error;
	}
    }

  /* Initialize the lock manager and the page buffer pool */
  error_code = lock_initialize ();
  if (error_code != NO_ERROR)
    {
      goto error;
    }
  error_code = pgbuf_initialize ();
  if (error_code != NO_ERROR)
    {
      goto error;
    }
  error_code = file_manager_initialize (thread_p);
  if (error_code != NO_ERROR)
    {
      goto error;
    }
  return error_code;

error:
  logtb_undefine_trantable (thread_p);
  logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_def_trantable");

  return error_code;
}

/*
 * logtb_initialize_trantable -
 *
 * return: nothing
 *
 *   trantable_p(in/out):
 *
 * Note: .
 */
static void
logtb_initialize_trantable (TRANTABLE * trantable_p)
{
  trantable_p->num_total_indices = 0;
  trantable_p->num_assigned_indices = 1;
  trantable_p->num_client_loose_end_indices = 0;
  trantable_p->num_coord_loose_end_indices = 0;
  trantable_p->num_prepared_loose_end_indices = 0;
  trantable_p->hint_free_index = 0;
  trantable_p->num_interrupts = 0;
  trantable_p->area = NULL;
  trantable_p->all_tdes = NULL;
}

/*
 * logtb_initialize_system_tdes -
 *
 * return: NO_ERROR if all OK, ER status otherwise
 *
 * Note: .
 */
static int
logtb_initialize_system_tdes (THREAD_ENTRY * thread_p)
{
  LOG_TDES *tdes;

  tdes = LOG_FIND_TDES (LOG_SYSTEM_TRAN_INDEX);
  if (tdes == NULL)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_LOG_UNKNOWN_TRANINDEX, 1, LOG_SYSTEM_TRAN_INDEX);
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			 "log_initialize_system_tdes");
      return ER_LOG_UNKNOWN_TRANINDEX;
    }

  logtb_clear_tdes (thread_p, tdes);
  tdes->tran_index = LOG_SYSTEM_TRAN_INDEX;
  tdes->trid = LOG_SYSTEM_TRANID;
  tdes->mvcc_info = NULL;
  tdes->isloose_end = true;
  tdes->wait_msecs = TRAN_LOCK_INFINITE_WAIT;
  tdes->isolation = TRAN_DEFAULT_ISOLATION_LEVEL ();
  tdes->client_id = -1;
  logtb_set_client_ids_all (&tdes->client, -1, NULL, NULL, NULL, NULL, NULL,
			    -1);
  tdes->query_timeout = 0;
  tdes->tran_abort_reason = TRAN_NORMAL;

  return NO_ERROR;
}

/*
 * logtb_undefine_trantable - undefine the transaction table
 *
 * return: nothing
 *
 * Note: Undefine and free the transaction table space.
 */
void
logtb_undefine_trantable (THREAD_ENTRY * thread_p)
{
  struct log_addr_tdesarea *area;
  LOG_TDES *tdes;		/* Transaction descriptor */
  int i;

  if (mvcc_Enabled)
    {
      logtb_finalize_mvcctable (thread_p);
    }
  lock_finalize ();
  pgbuf_finalize ();
  (void) file_manager_finalize (thread_p);

  if (log_Gl.trantable.area != NULL)
    {
      /*
       * If any one of the transaction indices has coordinator info,
       * free this area
       */
      for (i = 0; i < NUM_TOTAL_TRAN_INDICES; i++)
	{
	  /*
	   * If there is any memory allocated in the transaction descriptor,
	   * release it
	   */
	  tdes = log_Gl.trantable.all_tdes[i];
	  if (tdes != NULL)
	    {
#if defined(SERVER_MODE)
	      assert (tdes->tran_index == i);
	      assert (tdes->cs_topop.cs_index == CRITICAL_SECTION_COUNT
		      + css_get_max_conn () + NUM_MASTER_CHANNEL
		      + tdes->tran_index);
	      assert (tdes->cs_topop.name == css_Csect_name_tdes);
#endif

	      logtb_clear_tdes (thread_p, tdes);
	      logtb_mvcc_free_update_stats (&tdes->log_upd_stats);
	      csect_finalize_critical_section (&tdes->cs_topop);
	      if (tdes->topops.max != 0)
		{
		  free_and_init (tdes->topops.stack);
		  tdes->topops.max = 0;
		  tdes->topops.last = -1;
		}
	    }
	}

#if defined(ENABLE_UNUSED_FUNCTION)
      wfg_free_nodes (thread_p);
#endif

      if (log_Gl.trantable.all_tdes != NULL)
	{
	  free_and_init (log_Gl.trantable.all_tdes);
	}

      area = log_Gl.trantable.area;
      while (area != NULL)
	{
	  log_Gl.trantable.area = area->next;
	  free_and_init (area);
	}
    }

  logtb_initialize_trantable (&log_Gl.trantable);
}

/*
 * logtb_get_number_assigned_tran_indices - find number of transaction indices
 *
 * return: number of transaction indices
 *
 */
int
logtb_get_number_assigned_tran_indices (void)
{
  /* Do not use TR_TABLE_CS_ENTER()/TR_TABLE_CS_EXIT(),
   * Estimated value is sufficient for the caller
   */
  return NUM_ASSIGNED_TRAN_INDICES;
}

/*
 * logtb_set_number_of_assigned_tran_indices - set the number of tran indices
 *
 * return: nothing
 *      num_trans(in): the number of assigned tran indices
 *
 * Note: Callers have to call this function in the 'TR_TABLE' critical section.
 */
static void
logtb_set_number_of_assigned_tran_indices (int num_trans)
{
  log_Gl.trantable.num_assigned_indices = num_trans;
}

/*
 * logtb_increment_number_of_assigned_tran_indices -
 *      increment the number of tran indices
 *
 * return: nothing
 *
 * Note: Callers have to call this function in the 'TR_TABLE' critical section.
 */
static void
logtb_increment_number_of_assigned_tran_indices (void)
{
  log_Gl.trantable.num_assigned_indices++;
}

/*
 * logtb_decrement_number_of_assigned_tran_indices -
 *      decrement the number of tran indices
 *
 * return: nothing
 *
 * Note: Callers have to call this function in the 'TR_TABLE' critical section.
 */
static void
logtb_decrement_number_of_assigned_tran_indices (void)
{
  log_Gl.trantable.num_assigned_indices--;
}

/*
 * logtb_get_number_of_total_tran_indices - find number of total transaction
 *                                          indices
 *
 * return: nothing
 *
 * Note: Find number of total transaction indices in the transaction
 *              table. Note that some of this indices may have not been
 *              assigned. See logtb_get_number_assigned_tran_indices.
 */
int
logtb_get_number_of_total_tran_indices (void)
{
  return log_Gl.trantable.num_total_indices;
}

/*
 * logtb_set_number_of_total_tran_indices - set the number of total tran indices
 *
 * return: nothing
 *      num_trans(in): the number of total tran indices
 *
 * Note: Callers have to call this function in the 'TR_TABLE' critical section.
 */
static void
logtb_set_number_of_total_tran_indices (int num_total_trans)
{
  log_Gl.trantable.num_total_indices = num_total_trans;
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * logtb_am_i_sole_tran - Check if no other transactions are running
 *
 * return: If true, return as did TR_TABLE_CS_ENTER()
 *                                          but not TR_TABLE_CS_EXIT()
 *
 * Note: Check if no other transactions are running, that is, i am a
 *              sole transaction. If you get true by this function, you
 *              should call logtb_i_am_not_sole_tran() to exit the critical
 *              section (TR_TABLE_CS_EIXT())
 */
bool
logtb_am_i_sole_tran (THREAD_ENTRY * thread_p)
{
  TR_TABLE_CS_ENTER (thread_p);

  if (NUM_ASSIGNED_TRAN_INDICES <= 2)
    {
      return true;
    }

  return false;
}

/*
 * logtb_i_am_not_sole_tran -
 *
 * return:
 *
 * NOTE:
 */
void
logtb_i_am_not_sole_tran (THREAD_ENTRY * thread_p)
{
  TR_TABLE_CS_EXIT (thread_p);
}
#endif /* ENABLE_UNUSED_FUNCTION */

bool
logtb_am_i_dba_client (THREAD_ENTRY * thread_p)
{
  const char *db_user;

  db_user = logtb_find_current_client_name (thread_p);
  return (db_user != NULL && !strcasecmp (db_user, "DBA"));
}

/*
 * logtb_assign_tran_index - assign a transaction index for a sequence of
 *                        transactions (thread of execution.. a client)
 *
 * return: transaction index
 *
 *   trid(in): Transaction identifier or NULL_TRANID
 *   state(in): Transaction state (Usually active)
 *   client_prog_name(in): Name of the client program or NULL
 *   client_user_name(in): Name of the client user or NULL
 *   client_host_name(in): Name of the client host or NULL
 *   client_process_id(in): Identifier of the process of the host where the
 *                      client transaction runs.
 *   current_state(in/out): Set as a side effect to state of transaction, when
 *                      a valid pointer is given.
 *   wait_msecs(in): Wait for at least this number of milliseconds to acquire a
 *                      lock. Negative value is infinite
 *   isolation(in): Isolation level. One of the following:
 *                         TRAN_SERIALIZABLE
 *                         TRAN_REPEATABLE_READ
 *                         TRAN_READ_COMMITTED
 *
 * Note:Assign a transaction index for a sequence of transactions
 *              (i.e., a client) and initialize the state structure for the
 *              first transaction in the sequence. If trid is equal to
 *              NULL_TRANID, a transaction is assigned to the assigned
 *              structure and the transaction is declared active; otherwise,
 *              the given transaction with the given state is assigned to the
 *              index.
 *
 *       This function must be called when a client is restarted.
 */
int
logtb_assign_tran_index (THREAD_ENTRY * thread_p, TRANID trid,
			 TRAN_STATE state,
			 const BOOT_CLIENT_CREDENTIAL * client_credential,
			 TRAN_STATE * current_state, int wait_msecs,
			 TRAN_ISOLATION isolation)
{
  int tran_index;		/* The allocated transaction index */

#if defined(SERVER_MODE)
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }
#endif /* SERVER_MODE */

  LOG_SET_CURRENT_TRAN_INDEX (thread_p, LOG_SYSTEM_TRAN_INDEX);

  TR_TABLE_CS_ENTER (thread_p);
  tran_index = logtb_allocate_tran_index (thread_p, trid, state,
					  client_credential,
					  current_state, wait_msecs,
					  isolation);
  TR_TABLE_CS_EXIT (thread_p);

  if (tran_index != NULL_TRAN_INDEX)
    {
      if (mvcc_Enabled)
	{
	  if (logtb_allocate_mvcc_info (thread_p) != NO_ERROR)
	    {
	      assert (false);
	      return NULL_TRAN_INDEX;
	    }
	}

      LOG_SET_CURRENT_TRAN_INDEX (thread_p, tran_index);
    }
  else
    {
      LOG_SET_CURRENT_TRAN_INDEX (thread_p, LOG_SYSTEM_TRAN_INDEX);
    }

  return tran_index;
}

/*
 * logtb_set_tdes -
 *
 * return:
 *
 *   tdes(in/out): Transaction descriptor
 *   client_prog_name(in): the name of the client program
 *   client_host_name(in): the name of the client host
 *   client_user_name(in): the name of the client user
 *   client_process_id(in): the process id of the client
 *   wait_msecs(in): Wait for at least this number of milliseconds to acquire a lock.
 *   isolation(in): Isolation level
 */
static void
logtb_set_tdes (THREAD_ENTRY * thread_p, LOG_TDES * tdes,
		const BOOT_CLIENT_CREDENTIAL * client_credential,
		int wait_msecs, TRAN_ISOLATION isolation)
{
#if defined(SERVER_MODE)
  CSS_CONN_ENTRY *conn;
#endif /* SERVER_MODE */

  if (client_credential == NULL)
    {
      client_credential = &log_Client_credential;
    }
  logtb_set_client_ids_all (&tdes->client, client_credential->client_type,
			    client_credential->client_info,
			    client_credential->db_user,
			    client_credential->program_name,
			    client_credential->login_name,
			    client_credential->host_name,
			    client_credential->process_id);
#if defined(SERVER_MODE)
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  conn = thread_p->conn_entry;
  if (conn != NULL)
    {
      tdes->client_id = conn->client_id;
    }
  else
    {
      tdes->client_id = -1;
    }
#else /* SERVER_MODE */
  tdes->client_id = -1;
#endif /* SERVER_MODE */
  tdes->wait_msecs = wait_msecs;
  tdes->isolation = isolation;
  tdes->isloose_end = false;
  tdes->interrupt = false;
  tdes->topops.stack = NULL;
  tdes->topops.max = 0;
  tdes->topops.last = -1;
  tdes->modified_class_list = NULL;
  tdes->num_transient_classnames = 0;
  tdes->first_save_entry = NULL;
  tdes->num_new_files = 0;
  tdes->num_new_tmp_files = 0;
  tdes->num_new_tmp_tmp_files = 0;
  RB_INIT (&tdes->lob_locator_root);
}

/*
 * logtb_allocate_tran_index - allocate a transaction index for a sequence of
 *                       transactions (thread of execution.. a client)
 *
 * return: tran_index or NULL_TRAN_INDEX
 *
 *   trid(in): Transaction identifier or NULL_TRANID
 *   state(in): Transaction state (Usually active)
 *   client_prog_name(in): Name of the client program or NULL
 *   client_user_name(in): Name of the client user or NULL
 *   client_host_name(in): Name of the client host or NULL
 *   client_process_id(in): Identifier of the process of the host where the
 *                      client transaction runs.
 *   current_state(in/out): Set as a side effect to state of transaction, when
 *                      a valid pointer is given.
 *   wait_msecs(in): Wait for at least this number of milliseconds to acquire a
 *                      lock. That is, wait this much before the transaction
 *                      is timed out. Negative value is infinite.
 *   isolation(in): Isolation level. One of the following:
 *                         TRAN_SERIALIZABLE
 *                         TRAN_REPEATABLE_READ
 *                         TRAN_READ_COMMITTED
 *
 * Note:Allocate a transaction index for a sequence of transactions
 *              (i.e., a client) and initialize the state structure for the
 *              first transaction in the sequence. If trid is equal to
 *              NULL_TRANID, a transaction is assigned to the assigned
 *              structure and the transaction is declared active; otherwise,
 *              the given transaction with the given state is assigned to the
 *              index. If the given client user has a dangling entry due to
 *              client loose ends, this index is attached to it.
 *
 *       This function is only called by the log manager when the log
 *              latch has already been acquired. (See logtb_assign_tran_index)
 */
static int
logtb_allocate_tran_index (THREAD_ENTRY * thread_p, TRANID trid,
			   TRAN_STATE state,
			   const BOOT_CLIENT_CREDENTIAL * client_credential,
			   TRAN_STATE * current_state,
			   int wait_msecs, TRAN_ISOLATION isolation)
{
  int i;
  int visited_loop_start_pos;
  LOG_TDES *tdes;		/* Transaction descriptor */
  int tran_index;		/* The assigned index */
  int save_tran_index;		/* Save as a good index to assign */

#if defined(SERVER_MODE)
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }
#endif /* SERVER_MODE */

  save_tran_index = tran_index = NULL_TRAN_INDEX;

  if (log_Gl.trantable.num_client_loose_end_indices > 0
      && client_credential != NULL && client_credential->db_user != NULL)
    {
      /*
       * Check if the client_user has a dangling entry for client loose ends.
       * If it does, assign such a dangling index to it
       */
      for (i = 0; i < NUM_TOTAL_TRAN_INDICES; i++)
	{
	  tdes = log_Gl.trantable.all_tdes[i];
	  if (tdes != NULL
	      && tdes->isloose_end == true
	      && LOG_ISTRAN_CLIENT_LOOSE_ENDS (tdes)
	      && strcmp (tdes->client.db_user,
			 client_credential->db_user) == 0)
	    {
	      /*
	       * A client loose end transaction for current user.
	       */
	      log_Gl.trantable.num_client_loose_end_indices--;
	      logtb_set_tdes (thread_p, tdes, client_credential, wait_msecs,
			      isolation);

	      if (current_state != NULL)
		{
		  *current_state = tdes->state;
		}

	      LOG_SET_CURRENT_TRAN_INDEX (thread_p, i);

	      return i;
	    }
	  if (tdes != NULL && tdes->trid == NULL_TRANID)
	    {
	      save_tran_index = i;
	    }
	}
      tran_index = save_tran_index;
    }

  /* Is there any free index ? */
  if (NUM_ASSIGNED_TRAN_INDICES >= NUM_TOTAL_TRAN_INDICES)
    {
#if defined(SERVER_MODE)
      /* When normal processing, we never expand trantable */
      if (log_Gl.rcv_phase == LOG_RESTARTED && NUM_TOTAL_TRAN_INDICES > 0)
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_TM_TOO_MANY_CLIENTS, 1, NUM_TOTAL_TRAN_INDICES - 1);
	  return NULL_TRAN_INDEX;
	}
#endif /* SERVER_MODE */

      i = (int) (((float) NUM_TOTAL_TRAN_INDICES * LOG_EXPAND_TRANTABLE_RATIO)
		 + 0.5);
      if (logtb_expand_trantable (thread_p, i) != NO_ERROR)
	{
	  /* Out of memory or something like that */
	  return NULL_TRAN_INDEX;
	}
    }

  /*
   * Note that we could have found the entry already and it may be stored in
   * tran_index.
   */
  for (i = log_Gl.trantable.hint_free_index, visited_loop_start_pos = 0;
       tran_index == NULL_TRAN_INDEX && visited_loop_start_pos < 2;
       i = (i + 1) % NUM_TOTAL_TRAN_INDICES)
    {
      if (log_Gl.trantable.all_tdes[i]->trid == NULL_TRANID)
	{
	  tran_index = i;
	}
      if (i == log_Gl.trantable.hint_free_index)
	{
	  visited_loop_start_pos++;
	}
    }

  if (tran_index != NULL_TRAN_INDEX)
    {
      log_Gl.trantable.hint_free_index =
	(tran_index + 1) % NUM_TOTAL_TRAN_INDICES;

      logtb_increment_number_of_assigned_tran_indices ();

      tdes = LOG_FIND_TDES (tran_index);
      if (tdes == NULL)
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
	  return NULL_TRAN_INDEX;
	}

      tdes->tran_index = tran_index;
      logtb_clear_tdes (thread_p, tdes);
      logtb_set_tdes (thread_p, tdes, client_credential, wait_msecs,
		      isolation);

      if (trid == NULL_TRANID)
	{
	  /* Assign a new transaction identifier for the new index */
	  logtb_get_new_tran_id (thread_p, tdes);
	  state = TRAN_ACTIVE;
	}
      else
	{
	  tdes->trid = trid;
	  tdes->state = state;
	}

      if (current_state)
	{
	  *current_state = state;
	}

      LOG_SET_CURRENT_TRAN_INDEX (thread_p, tran_index);

      tdes->tran_abort_reason = TRAN_NORMAL;
    }

  return tran_index;
}

/*
 * logtb_initialize_mvcctable - initialize MVCC table
 *
 * return: error code
 *
 *   thread_p(in): thread entry 
 */
static int
logtb_initialize_mvcctable (THREAD_ENTRY * thread_p)
{
  MVCC_INFO_BLOCK *mvcc_info_block = NULL;
  MVCC_INFO *curr_mvcc_info = NULL, *prev_mvcc_info = NULL;
  MVCCTABLE *mvcc_table = &log_Gl.mvcc_table;
  int error;

  mvcc_table->head_writers = mvcc_table->tail_writers =
    mvcc_table->head_null_mvccids = NULL;

  mvcc_table->block_list = NULL;
  mvcc_table->free_list = NULL;

  error = logtb_alloc_mvcc_info_block (thread_p);
  if (error != NO_ERROR)
    {
      return error;
    }

  *(volatile int *) (&mvcc_table->mvcc_info_free_list_lock) = 0;
  mvcc_table->highest_completed_mvccid = MVCCID_NULL;

  return NO_ERROR;
}

/*
 * logtb_finalize_mvcctable - cleanup MVCC table
 *
 * return: error code
 *
 *   thread_p(in): thread entry
 */
static void
logtb_finalize_mvcctable (THREAD_ENTRY * thread_p)
{
  MVCC_INFO_BLOCK *curr_mvcc_info_block, *next_mvcc_info_block;
  MVCC_INFO *curr_mvcc_info;
  MVCCTABLE *mvcc_table = &log_Gl.mvcc_table;
  int i;

  mvcc_table->head_writers = mvcc_table->tail_writers =
    mvcc_table->head_null_mvccids = NULL;

  curr_mvcc_info_block = mvcc_table->block_list;
  while (curr_mvcc_info_block != NULL)
    {
      next_mvcc_info_block = curr_mvcc_info_block->next_block;
      if (curr_mvcc_info_block->block != NULL)
	{
	  for (i = 0; i < NUM_TOTAL_TRAN_INDICES; i++)
	    {
	      curr_mvcc_info = curr_mvcc_info_block->block + i;
	      if (curr_mvcc_info->mvcc_snapshot.active_ids != NULL)
		{
		  free_and_init (curr_mvcc_info->mvcc_snapshot.active_ids);
		}

	      if (curr_mvcc_info->mvcc_sub_ids != NULL)
		{
		  free_and_init (curr_mvcc_info->mvcc_sub_ids);
		}
	    }

	  free_and_init (curr_mvcc_info_block->block);
	}

      free_and_init (curr_mvcc_info_block);
      curr_mvcc_info_block = next_mvcc_info_block;
    }

  mvcc_table->block_list = NULL;
  mvcc_table->free_list = NULL;

  *(volatile int *) (&mvcc_table->mvcc_info_free_list_lock) = 0;
  mvcc_table->highest_completed_mvccid = MVCCID_NULL;
}

int
logtb_is_tran_modification_disabled (THREAD_ENTRY * thread_p)
{
  LOG_TDES *tdes;
  int tran_index;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);

  if (tdes == NULL)
    {
      return db_Disable_modifications;
    }

  return tdes->disable_modifications;
}

/*
 * logtb_rv_find_allocate_tran_index - find/alloc a transaction during the recovery
 *                         analysis process
 *
 * return: The transaction descriptor
 *
 *   trid(in): The desired transaction identifier
 *   log_lsa(in): Log address where the transaction was seen in the log
 *
 * Note: Find or allocate the transaction descriptor for the given
 *              transaction identifier. If the descriptor was allocated, it is
 *              assumed that this is the first time the transaction is seen.
 *              Thus, the head of the transaction in the log is located at the
 *              givel location (i.e., log_lsa.pageid, log_offset).
 *       This function should be called only by the recovery process
 *              (the analysis phase).
 */
LOG_TDES *
logtb_rv_find_allocate_tran_index (THREAD_ENTRY * thread_p, TRANID trid,
				   const LOG_LSA * log_lsa)
{
  LOG_TDES *tdes;		/* Transaction descriptor */
  int tran_index;

  /*
   * If this is the first time, the transaction is seen. Assign a new
   * index to describe it and assume that the transaction was active
   * at the time of the crash, and thus it will be unilaterally aborted
   */
  tran_index = logtb_find_tran_index (thread_p, trid);
  if (tran_index == NULL_TRAN_INDEX)
    {
      /* Define the index */
      tran_index = logtb_allocate_tran_index (thread_p, trid,
					      TRAN_UNACTIVE_UNILATERALLY_ABORTED,
					      NULL, NULL,
					      TRAN_LOCK_INFINITE_WAIT,
					      TRAN_SERIALIZABLE);
      tdes = LOG_FIND_TDES (tran_index);
      if (tran_index == NULL_TRAN_INDEX || tdes == NULL)
	{
	  /*
	   * Unable to assign a transaction index. The recovery process
	   * cannot continue
	   */
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			     "log_recovery_find_or_alloc");
	  return NULL;
	}
      else
	{
	  LSA_COPY (&tdes->head_lsa, log_lsa);
	}

      if (mvcc_Enabled)
	{
	  /* Need to allocate MVCC info too */
	  if (logtb_allocate_mvcc_info (thread_p) != NO_ERROR)
	    {
	      assert (false);
	      return NULL;
	    }
	}
    }
  else
    {
      tdes = LOG_FIND_TDES (tran_index);
    }

  return tdes;
}

/*
 * logtb_rv_assign_mvccid_for_undo_recovery () - Assign an MVCCID for
 *						 transactions that need to
 *						 undo at recovery.
 *
 * return	 : Void.
 * thread_p (in) : Thread entry.
 * mvccid (in)	 : Assigned MVCCID.
 */
void
logtb_rv_assign_mvccid_for_undo_recovery (THREAD_ENTRY * thread_p,
					  MVCCID mvccid)
{
  LOG_TDES *tdes = LOG_FIND_CURRENT_TDES (thread_p);

  assert (tdes != NULL && tdes->mvcc_info != NULL);
  assert (MVCCID_IS_VALID (mvccid));

  /* Transaction should have no MVCCID assigned, or it should be the same
   * if it is already assigned.
   */
  assert (!MVCCID_IS_VALID (tdes->mvcc_info->mvcc_id)
	  || tdes->mvcc_info->mvcc_id == mvccid);

  tdes->mvcc_info->mvcc_id = mvccid;
}

/*
 * logtb_release_tran_index - return an assigned transaction index
 *
 * return: nothing
 *
 *   tran_index(in): Transaction index
 *
 * Note: Return a transaction index which was used for a sequence of
 *              transactions (i.e., a client).
 *
 *       This function must be called when a client is shutdown (i.e.,
 *              unregistered).
 */
void
logtb_release_tran_index (THREAD_ENTRY * thread_p, int tran_index)
{
  LOG_TDES *tdes;		/* Transaction descriptor */

  qmgr_clear_trans_wakeup (thread_p, tran_index, true, false);
  heap_chnguess_clear (thread_p, tran_index);

  tdes = LOG_FIND_TDES (tran_index);
  if (tran_index != LOG_SYSTEM_TRAN_INDEX && tdes != NULL)
    {
      if (mvcc_Enabled)
	{
	  if (!thread_is_vacuum_worker (thread_p))
	    {
	      logtb_release_mvcc_info (thread_p);
	    }
	  else
	    {
	      assert (tdes->mvcc_info == NULL);
	    }
	}

      TR_TABLE_CS_ENTER (thread_p);

      /*
       * Free the top system operation stack since the transaction entry may
       * not be freed (i.e., left as loose end distributed transaction)
       */
      if (tdes->topops.max != 0)
	{
	  free_and_init (tdes->topops.stack);
	  tdes->topops.max = 0;
	  tdes->topops.last = -1;
	}

      if (LOG_ISTRAN_CLIENT_LOOSE_ENDS (tdes))
	{
	  tdes->isloose_end = true;
	  log_Gl.trantable.num_client_loose_end_indices++;
	}
      else
	{
	  if (LOG_ISTRAN_2PC_PREPARE (tdes))
	    {
	      tdes->isloose_end = true;
	      log_Gl.trantable.num_prepared_loose_end_indices++;
	    }
	  else
	    {
	      if (LOG_ISTRAN_2PC_INFORMING_PARTICIPANTS (tdes))
		{
		  tdes->isloose_end = true;
		  log_Gl.trantable.num_coord_loose_end_indices++;
		}
	      else
		{
		  logtb_free_tran_index (thread_p, tran_index);
		}
	    }
	}

      TR_TABLE_CS_EXIT (thread_p);
    }
}

/*
 * logtb_free_tran_index - free a transaction index
 *
 * return: nothing
 *
 *   tran_index(in): Transaction index
 *
 * Note: Free a transaction index which was used for a sequence of
 *              transactions (i.e., a client).
 *
 *       This function is only called by the log manager when the log
 *              latch has already been acquired. (See logtb_release_tran_index
 *              for other cases).
 */
void
logtb_free_tran_index (THREAD_ENTRY * thread_p, int tran_index)
{
  LOG_TDES *tdes;		/* Transaction descriptor */
  int log_tran_index;

#if defined(SERVER_MODE)
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }
#endif /* SERVER_MODE */

  log_tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);

  tdes = LOG_FIND_TDES (tran_index);
  if (tran_index > NUM_TOTAL_TRAN_INDICES
      || tdes == NULL || tdes->trid == NULL_TRANID)
    {
#if defined(CUBRID_DEBUG)
      er_log_debug (ARG_FILE_LINE, "log_free_tran_index: Unknown index = %d."
		    " Operation is ignored", tran_index);
#endif /* CUBRID_DEBUG */
      return;
    }

  logtb_clear_tdes (thread_p, tdes);
  if (tdes->repl_records)
    {
      free_and_init (tdes->repl_records);
    }
  tdes->num_repl_records = 0;
  if (tdes->topops.max != 0)
    {
      free_and_init (tdes->topops.stack);
      tdes->topops.max = 0;
      tdes->topops.last = -1;
    }

  if (tran_index != LOG_SYSTEM_TRAN_INDEX)
    {
      tdes->trid = NULL_TRANID;
      tdes->client_id = -1;

      TR_TABLE_CS_ENTER (thread_p);
      logtb_decrement_number_of_assigned_tran_indices ();
      if (log_Gl.trantable.hint_free_index > tran_index)
	{
	  log_Gl.trantable.hint_free_index = tran_index;
	}
      TR_TABLE_CS_EXIT (thread_p);

      if (log_tran_index == tran_index)
	{
	  if (!LOG_ISRESTARTED ())
	    {
	      log_tran_index = LOG_SYSTEM_TRAN_INDEX;
	    }
	  else
	    {
	      log_tran_index = NULL_TRAN_INDEX;
	    }

	  LOG_SET_CURRENT_TRAN_INDEX (thread_p, log_tran_index);
	}
    }
}

/*
 * logtb_free_tran_index_with_undo_lsa - free tranindex with lsa
 *
 * return: nothing
 *
 *   undo_lsa(in): Undo log sequence address
 *
 * Note: Remove the transaction index associated with the undo LSA.
 *              This function execute a sequential search on the transaction
 *              table to find out the transaction with such lsa. This
 *              sequential scan is OK since this function is only called when
 *              a system error happens, which in principle never happen.
 */
void
logtb_free_tran_index_with_undo_lsa (THREAD_ENTRY * thread_p,
				     const LOG_LSA * undo_lsa)
{
  int i;
  LOG_TDES *tdes;		/* Transaction descriptor */

  TR_TABLE_CS_ENTER (thread_p);

  if (undo_lsa != NULL && !LSA_ISNULL (undo_lsa))
    {
      for (i = 0; i < NUM_TOTAL_TRAN_INDICES; i++)
	{
	  if (i != LOG_SYSTEM_TRAN_INDEX)
	    {
	      tdes = log_Gl.trantable.all_tdes[i];
	      if (tdes != NULL
		  && tdes->trid != NULL_TRANID
		  && tdes->state == TRAN_UNACTIVE_UNILATERALLY_ABORTED
		  && LSA_EQ (undo_lsa, &tdes->undo_nxlsa))
		{
		  logtb_free_tran_index (thread_p, i);
		}
	    }
	}
    }

  TR_TABLE_CS_EXIT (thread_p);
}

/*
 * logtb_dump_tdes -
 *
 * return: nothing
 *
 *   tdes(in):
 *
 * Note:
 */
static void
logtb_dump_tdes (FILE * out_fp, LOG_TDES * tdes)
{
  fprintf (out_fp, "Tran_index = %2d, Trid = %d,\n"
	   "    State = %s,\n"
	   "    Isolation = %s,\n"
	   "    Wait_msecs = %d, isloose_end = %d,\n"
	   "    Head_lsa = %lld|%d, Tail_lsa = %lld|%d,"
	   " Postpone_lsa = %lld|%d,\n"
	   "    SaveLSA = %lld|%d, UndoNextLSA = %lld|%d,\n"
	   "    Client_User: (Type = %d, User = %s, Program = %s, "
	   "Login = %s, Host = %s, Pid = %d,\n"
	   "                  undo_lsa = %lld|%d, posp_nxlsa = %lld|%d)"
	   "\n",
	   tdes->tran_index, tdes->trid,
	   log_state_string (tdes->state),
	   log_isolation_string (tdes->isolation),
	   tdes->wait_msecs, tdes->isloose_end,
	   (long long int) tdes->head_lsa.pageid, tdes->head_lsa.offset,
	   (long long int) tdes->tail_lsa.pageid, tdes->tail_lsa.offset,
	   (long long int) tdes->posp_nxlsa.pageid, tdes->posp_nxlsa.offset,
	   (long long int) tdes->savept_lsa.pageid, tdes->savept_lsa.offset,
	   (long long int) tdes->undo_nxlsa.pageid, tdes->undo_nxlsa.offset,
	   tdes->client.client_type, tdes->client.db_user,
	   tdes->client.program_name, tdes->client.login_name,
	   tdes->client.host_name, tdes->client.process_id,
	   (long long int) tdes->client_undo_lsa.pageid,
	   tdes->client_undo_lsa.offset,
	   (long long int) tdes->client_posp_lsa.pageid,
	   tdes->client_posp_lsa.offset);

  if (tdes->topops.max != 0 && tdes->topops.last >= 0)
    {
      logtb_dump_top_operations (out_fp, &tdes->topops);
    }

  if (tdes->gtrid != LOG_2PC_NULL_GTRID || tdes->coord != NULL)
    {
      logtb_dump_tdes_distribute_transaction (out_fp, tdes->gtrid,
					      tdes->coord);
    }
}

/*
 * logtb_dump_top_operations -
 *
 * return: nothing
 *
 *   tdes(in):
 *
 * Note:
 */
static void
logtb_dump_top_operations (FILE * out_fp, LOG_TOPOPS_STACK * topops_p)
{
  int i;

  fprintf (out_fp, "    Active top system operations for tran:\n");
  for (i = topops_p->last; i >= 0; i--)
    {
      fprintf (out_fp, " Head = %lld|%d, Posp_Head = %lld|%d,"
	       "Client_posp_Head = %lld|%d, Client_undo_Head = %lld|%d\n",
	       (long long int) topops_p->stack[i].lastparent_lsa.pageid,
	       topops_p->stack[i].lastparent_lsa.offset,
	       (long long int) topops_p->stack[i].posp_lsa.pageid,
	       topops_p->stack[i].posp_lsa.offset,
	       (long long int) topops_p->stack[i].client_posp_lsa.pageid,
	       topops_p->stack[i].client_posp_lsa.offset,
	       (long long int) topops_p->stack[i].client_undo_lsa.pageid,
	       topops_p->stack[i].client_undo_lsa.offset);
    }
}

/*
 * logtb_dump_tdes_distribute_transaction -
 *
 * return: nothing
 *
 *   tdes(in):
 *
 * Note:
 */
static void
logtb_dump_tdes_distribute_transaction (FILE * out_fp, int global_tran_id,
					LOG_2PC_COORDINATOR * coord)
{
  int i;
  char *particp_id;		/* Participant identifier */

  /* This is a distributed transaction */
  if (coord != NULL)
    {
      fprintf (out_fp, "    COORDINATOR SITE" "(or NESTED PARTICIPANT SITE)");
    }
  else
    {
      fprintf (out_fp, "    PARTICIPANT SITE");
    }

  fprintf (out_fp, " of global tranid = %d\n", global_tran_id);

  if (coord != NULL)
    {
      fprintf (out_fp, "    Num_participants = %d, Partids = ",
	       coord->num_particps);
      for (i = 0; i < coord->num_particps; i++)
	{
	  particp_id = ((char *) coord->block_particps_ids +
			i * coord->particp_id_length);
	  if (i == 0)
	    {
	      fprintf (out_fp, " %s", log_2pc_sprintf_particp (particp_id));
	    }
	  else
	    {
	      fprintf (out_fp, ", %s", log_2pc_sprintf_particp (particp_id));
	    }
	}
      fprintf (out_fp, "\n");

      if (coord->ack_received)
	{
	  fprintf (out_fp, "    Acknowledgement vector =");
	  for (i = 0; i < coord->num_particps; i++)
	    {
	      if (i == 0)
		{
		  fprintf (out_fp, " %d", coord->ack_received[i]);
		}
	      else
		{
		  fprintf (out_fp, ", %d", coord->ack_received[i]);
		}
	    }
	}
      fprintf (out_fp, "\n");
    }
}

/*
 * xlogtb_dump_trantable - dump the transaction table
 *
 * return: nothing
 *
 * Note: Dump the transaction state table.
 *              This function is used for debugging purposes.
 */
void
xlogtb_dump_trantable (THREAD_ENTRY * thread_p, FILE * out_fp)
{
  int i;
  LOG_TDES *tdes;		/* Transaction descriptor */

  fprintf (out_fp, "\n ** DUMPING TABLE OF ACTIVE TRANSACTIONS **\n");

  TR_TABLE_CS_ENTER_READ_MODE (thread_p);

  for (i = 0; i < NUM_TOTAL_TRAN_INDICES; i++)
    {
      tdes = log_Gl.trantable.all_tdes[i];
      if (tdes == NULL || tdes->trid == NULL_TRANID)
	{
	  fprintf (out_fp, "Tran_index = %2d... Free transaction index\n", i);
	}
      else
	{
	  logtb_dump_tdes (out_fp, tdes);
	}
    }

  TR_TABLE_CS_EXIT (thread_p);

  fprintf (out_fp, "\n");
}

/*
 * logtb_clear_tdes - clear the transaction descriptor
 *
 * return: nothing..
 *
 *   tdes(in/out): Transaction descriptor
 */
void
logtb_clear_tdes (THREAD_ENTRY * thread_p, LOG_TDES * tdes)
{
  int i, j;
  DB_VALUE *dbval;
  HL_HEAPID save_heap_id;

  tdes->isloose_end = false;
  tdes->state = TRAN_ACTIVE;
  LSA_SET_NULL (&tdes->head_lsa);
  LSA_SET_NULL (&tdes->tail_lsa);
  LSA_SET_NULL (&tdes->undo_nxlsa);
  LSA_SET_NULL (&tdes->posp_nxlsa);
  LSA_SET_NULL (&tdes->savept_lsa);
  LSA_SET_NULL (&tdes->topop_lsa);
  LSA_SET_NULL (&tdes->tail_topresult_lsa);
  LSA_SET_NULL (&tdes->client_undo_lsa);
  LSA_SET_NULL (&tdes->client_posp_lsa);
  tdes->topops.last = -1;
  tdes->gtrid = LOG_2PC_NULL_GTRID;
  tdes->gtrinfo.info_length = 0;
  if (tdes->gtrinfo.info_data != NULL)
    {
      free_and_init (tdes->gtrinfo.info_data);
    }
  if (tdes->coord != NULL)
    {
      log_2pc_free_coord_info (tdes);
    }
  if (tdes->tran_unique_stats != NULL)
    {
      free_and_init (tdes->tran_unique_stats);
      tdes->num_unique_btrees = 0;
      tdes->max_unique_btrees = 0;
    }
  if (tdes->interrupt == true)
    {
      tdes->interrupt = false;
      TR_TABLE_CS_ENTER (thread_p);
      log_Gl.trantable.num_interrupts--;
      TR_TABLE_CS_EXIT (thread_p);
    }
  tdes->modified_class_list = NULL;

  for (i = 0; i < tdes->cur_repl_record; i++)
    {
      if (tdes->repl_records[i].repl_data)
	{
	  free_and_init (tdes->repl_records[i].repl_data);
	}
    }

  save_heap_id = db_change_private_heap (thread_p, 0);
  for (i = 0; i < tdes->num_exec_queries && i < MAX_NUM_EXEC_QUERY_HISTORY;
       i++)
    {
      if (tdes->bind_history[i].vals == NULL)
	{
	  continue;
	}

      dbval = tdes->bind_history[i].vals;
      for (j = 0; j < tdes->bind_history[i].size; j++)
	{
	  db_value_clear (dbval);
	  dbval++;
	}

      db_private_free_and_init (thread_p, tdes->bind_history[i].vals);
      tdes->bind_history[i].size = 0;
    }
  (void) db_change_private_heap (thread_p, save_heap_id);

  tdes->cur_repl_record = 0;
  tdes->append_repl_recidx = -1;
  tdes->fl_mark_repl_recidx = -1;
  LSA_SET_NULL (&tdes->repl_insert_lsa);
  LSA_SET_NULL (&tdes->repl_update_lsa);
  tdes->first_save_entry = NULL;
  tdes->num_new_files = 0;
  tdes->num_new_tmp_files = 0;
  tdes->num_new_tmp_tmp_files = 0;
  tdes->query_timeout = 0;
  tdes->query_start_time = 0;
  tdes->tran_start_time = 0;
  XASL_ID_SET_NULL (&tdes->xasl_id);
  tdes->waiting_for_res = NULL;
  tdes->tran_abort_reason = TRAN_NORMAL;
  tdes->num_exec_queries = 0;
  tdes->suppress_replication = 0;

  logtb_mvcc_clear_update_stats (&tdes->log_upd_stats);

  assert (tdes->mvcc_info == NULL || tdes->mvcc_info->mvcc_id == MVCCID_NULL);

  if (BOOT_WRITE_ON_STANDY_CLIENT_TYPE (tdes->client.client_type))
    {
      tdes->disable_modifications = 0;
    }
  else
    {
      tdes->disable_modifications = db_Disable_modifications;
    }
  tdes->has_deadlock_priority = false;
}

/*
 * logtb_initialize_tdes - initialize the transaction descriptor
 *
 * return: nothing..
 *
 *   tdes(in/out): Transaction descriptor
 *   tran_index(in): Transaction index
 */
static void
logtb_initialize_tdes (LOG_TDES * tdes, int tran_index)
{
  int i;

  tdes->tran_index = tran_index;
  tdes->trid = NULL_TRANID;
  tdes->isloose_end = false;
  tdes->coord = NULL;
  tdes->client_id = -1;
  tdes->client.client_type = BOOT_CLIENT_UNKNOWN;
  tdes->gtrid = LOG_2PC_NULL_GTRID;
  tdes->gtrinfo.info_length = 0;
  tdes->gtrinfo.info_data = NULL;
  tdes->interrupt = false;
  tdes->wait_msecs = TRAN_LOCK_INFINITE_WAIT;
  tdes->isolation = TRAN_SERIALIZABLE;
  LSA_SET_NULL (&tdes->head_lsa);
  LSA_SET_NULL (&tdes->tail_lsa);
  LSA_SET_NULL (&tdes->undo_nxlsa);
  LSA_SET_NULL (&tdes->posp_nxlsa);
  LSA_SET_NULL (&tdes->savept_lsa);
  LSA_SET_NULL (&tdes->topop_lsa);
  LSA_SET_NULL (&tdes->tail_topresult_lsa);
  LSA_SET_NULL (&tdes->client_undo_lsa);
  LSA_SET_NULL (&tdes->client_posp_lsa);

  csect_initialize_critical_section (&tdes->cs_topop);

#if defined(SERVER_MODE)
  assert (tdes->cs_topop.cs_index == -1);
  assert (tdes->cs_topop.name == NULL);

  tdes->cs_topop.cs_index = CRITICAL_SECTION_COUNT
    + css_get_max_conn () + NUM_MASTER_CHANNEL + tdes->tran_index;
  tdes->cs_topop.name = css_Csect_name_tdes;
#endif

  tdes->topops.stack = NULL;
  tdes->topops.last = -1;
  tdes->topops.max = 0;
  tdes->num_unique_btrees = 0;
  tdes->max_unique_btrees = 0;
  tdes->tran_unique_stats = NULL;
  tdes->num_transient_classnames = 0;
  tdes->num_repl_records = 0;
  tdes->cur_repl_record = 0;
  tdes->append_repl_recidx = -1;
  tdes->fl_mark_repl_recidx = -1;
  tdes->repl_records = NULL;
  LSA_SET_NULL (&tdes->repl_insert_lsa);
  LSA_SET_NULL (&tdes->repl_update_lsa);
  tdes->first_save_entry = NULL;
  tdes->num_new_files = 0;
  tdes->num_new_tmp_files = 0;
  tdes->num_new_tmp_tmp_files = 0;
  tdes->suppress_replication = 0;
  RB_INIT (&tdes->lob_locator_root);
  tdes->query_timeout = 0;
  tdes->query_start_time = 0;
  tdes->tran_start_time = 0;
  XASL_ID_SET_NULL (&tdes->xasl_id);
  tdes->waiting_for_res = NULL;
  tdes->disable_modifications = db_Disable_modifications;
  tdes->tran_abort_reason = TRAN_NORMAL;
  tdes->num_exec_queries = 0;

  for (i = 0; i < MAX_NUM_EXEC_QUERY_HISTORY; i++)
    {
      tdes->bind_history[i].size = 0;
      tdes->bind_history[i].vals = NULL;
    }
  tdes->has_deadlock_priority = false;

  tdes->mvcc_info = NULL;

  tdes->log_upd_stats.crt_tran_entries = NULL;
  tdes->log_upd_stats.free_entries = NULL;
  tdes->log_upd_stats.topop_id = -1;
}

/*
 * logtb_get_new_tran_id - assign a new transaction identifier
 *
 * return: tranid
 *
 *   tdes(in/out): Transaction descriptor
 */
int
logtb_get_new_tran_id (THREAD_ENTRY * thread_p, LOG_TDES * tdes)
{
  TR_TABLE_CS_ENTER (thread_p);

  logtb_clear_tdes (thread_p, tdes);

  tdes->trid = log_Gl.hdr.next_trid++;
  /* check overflow */
  if (tdes->trid < 0)
    {
      tdes->trid = LOG_SYSTEM_TRANID + 1;
      /* set MVCC next id to null */
      log_Gl.hdr.next_trid = tdes->trid + 1;
    }

  TR_TABLE_CS_EXIT (thread_p);

  return tdes->trid;
}

/*
 * logtb_find_tran_index - find index of transaction
 *
 * return: tran index
 *
 *   trid(in): Transaction identifier
 *
 * Note: Find the index of a transaction. This function execute a
 *              sequential search in the transaction table to find out the
 *              transaction index. The function bypasses the search if the
 *              trid belongs to the current transaction.
 *
 *       The assumption of this function is that the transaction table
 *              is not very big and that most of the time the search is
 *              avoided. if this assumption becomes false, we may need to
 *              define a hash table from trid to tdes to speed up the
 *              search.
 */
int
logtb_find_tran_index (THREAD_ENTRY * thread_p, TRANID trid)
{
  int i;
  int tran_index = NULL_TRAN_INDEX;	/* The transaction index */
  LOG_TDES *tdes;		/* Transaction descriptor */

  /* Avoid searching as much as possible */
  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL || tdes->trid != trid)
    {
      tran_index = NULL_TRAN_INDEX;

      TR_TABLE_CS_ENTER_READ_MODE (thread_p);
      /* Search the transaction table for such transaction */
      for (i = 0; i < NUM_TOTAL_TRAN_INDICES; i++)
	{
	  tdes = log_Gl.trantable.all_tdes[i];
	  if (tdes != NULL && tdes->trid != NULL_TRANID && tdes->trid == trid)
	    {
	      tran_index = i;
	      break;
	    }
	}
      TR_TABLE_CS_EXIT (thread_p);
    }

  return tran_index;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * logtb_find_tran_index_host_pid - find index of transaction
 *
 * return: tran index
 *
 *   host_name(in): Name of host machine
 *   process_id(in): Process id of client
 *
 * Note: Find the index of a transaction. This function executes a
 *              sequential search in the transaction table to find out the
 *              transaction index given only the host machine and process id.
 *
 *       The assumption of this function is that the transaction table
 *              is not very big and that most of the time the search is
 *              avoided.  It is currently only being used during client
 *              restart to insure that no badly behaving clients manage to
 *              have two open connections at the same time.
 */
int
logtb_find_tran_index_host_pid (THREAD_ENTRY * thread_p,
				const char *host_name, int process_id)
{
  int i;
  int tran_index = NULL_TRAN_INDEX;	/* The transaction index */
  LOG_TDES *tdes;		/* Transaction descriptor */

  TR_TABLE_CS_ENTER_READ_MODE (thread_p);
  /* Search the transaction table for such transaction */
  for (i = 0; i < NUM_TOTAL_TRAN_INDICES; i++)
    {
      tdes = log_Gl.trantable.all_tdes[i];
      if (tdes != NULL
	  && tdes->trid != NULL_TRANID
	  && tdes->client.process_id == process_id
	  && strcmp (tdes->client.host_name, host_name) == 0)
	{
	  tran_index = i;
	  break;
	}
    }
  TR_TABLE_CS_EXIT (thread_p);

  return tran_index;
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * logtb_find_tranid - find TRANID of transaction index
 *
 * return: TRANID
 *
 *   tran_index(in): Index of transaction
 */
TRANID
logtb_find_tranid (int tran_index)
{
  LOG_TDES *tdes;		/* Transaction descriptor */
  TRANID trid = NULL_TRANID;	/* Transaction index */

  tdes = LOG_FIND_TDES (tran_index);
  if (tdes != NULL)
    {
      trid = tdes->trid;
    }
  return trid;
}

/*
 * logtb_find_current_tranid - find current transaction identifier
 *
 * return: TRANID
 */
TRANID
logtb_find_current_tranid (THREAD_ENTRY * thread_p)
{
  return logtb_find_tranid (LOG_FIND_THREAD_TRAN_INDEX (thread_p));
}

/*
 * logtb_set_client_ids_all - Set client identifications
 *
 * return: nothing..
 *
 *   client(in/out): The client block
 *   client_type(in):
 *   client_info(in):
 *   db_user(in):
 *   program_name(in):
 *   login_name(in):
 *   host_name(in):
 *   process_id(in):
 *
 * NOTE: Set client identifications.
 */
void
logtb_set_client_ids_all (LOG_CLIENTIDS * client, int client_type,
			  const char *client_info, const char *db_user,
			  const char *program_name, const char *login_name,
			  const char *host_name, int process_id)
{
  client->client_type = client_type;
  strncpy (client->client_info,
	   (client_info) ? client_info : "", DB_MAX_IDENTIFIER_LENGTH);
  client->client_info[DB_MAX_IDENTIFIER_LENGTH] = '\0';
  strncpy (client->db_user,
	   (db_user) ? db_user : log_Client_id_unknown_string,
	   LOG_USERNAME_MAX - 1);
  client->db_user[LOG_USERNAME_MAX - 1] = '\0';
  if (program_name == NULL
      || basename_r (program_name, client->program_name, PATH_MAX) < 0)
    {
      strncpy (client->program_name, log_Client_id_unknown_string, PATH_MAX);
    }
  client->program_name[PATH_MAX] = '\0';
  strncpy (client->login_name,
	   (login_name) ? login_name : log_Client_id_unknown_string,
	   L_cuserid);
  client->login_name[L_cuserid] = '\0';
  strncpy (client->host_name,
	   (host_name) ? host_name : log_Client_id_unknown_string,
	   MAXHOSTNAMELEN);
  client->host_name[MAXHOSTNAMELEN] = '\0';
  client->process_id = process_id;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * logtb_count_clients_with_type - count number of transaction indices
 *                                 with client type
 *   return: number of clients
 */
int
logtb_count_clients_with_type (THREAD_ENTRY * thread_p, int client_type)
{
  LOG_TDES *tdes;
  int i, count;

  TR_TABLE_CS_ENTER_READ_MODE (thread_p);

  count = 0;
  for (i = 0; i < log_Gl.trantable.num_total_indices; i++)
    {
      tdes = log_Gl.trantable.all_tdes[i];
      if (tdes != NULL && tdes->trid != NULL_TRANID)
	{
	  if (tdes->client.client_type == client_type)
	    {
	      count++;
	    }
	}
    }
  TR_TABLE_CS_EXIT (thread_p);
  return count;
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * logtb_count_clients - count number of transaction indices
 *   return: number of clients
 */
int
logtb_count_clients (THREAD_ENTRY * thread_p)
{
  LOG_TDES *tdes;
  int i, count;

  TR_TABLE_CS_ENTER_READ_MODE (thread_p);

  count = 0;
  for (i = 0; i < log_Gl.trantable.num_total_indices; i++)
    {
      tdes = log_Gl.trantable.all_tdes[i];
      if (tdes != NULL && tdes->trid != NULL_TRANID)
	{
	  if (BOOT_NORMAL_CLIENT_TYPE (tdes->client.client_type))
	    {
	      count++;
	    }
	}
    }
  TR_TABLE_CS_EXIT (thread_p);
  return count;
}

/*
 * logtb_count_not_allowed_clients_in_maintenance_mode -
 *                        count number of transaction indices
 *                        connection not allowed client in maintenancemode.
 *   return: number of clients
 */
int
logtb_count_not_allowed_clients_in_maintenance_mode (THREAD_ENTRY * thread_p)
{
  LOG_TDES *tdes;
  int i, count;

  TR_TABLE_CS_ENTER_READ_MODE (thread_p);

  count = 0;
  for (i = 0; i < log_Gl.trantable.num_total_indices; i++)
    {
      tdes = log_Gl.trantable.all_tdes[i];
      if (tdes != NULL && tdes->trid != NULL_TRANID)
	{
	  if (!BOOT_IS_ALLOWED_CLIENT_TYPE_IN_MT_MODE
	      (tdes->client.host_name, boot_Host_name,
	       tdes->client.client_type))
	    {
	      count++;
	    }
	}
    }
  TR_TABLE_CS_EXIT (thread_p);
  return count;
}

/*
  * logtb_find_client_type - find client type of transaction index
   *
   * return: client type
   *
   *   tran_index(in): Index of transaction
   */
int
logtb_find_client_type (int tran_index)
{
  LOG_TDES *tdes;

  tdes = LOG_FIND_TDES (tran_index);
  if (tdes != NULL && tdes->trid != NULL_TRANID)
    {
      return tdes->client.client_type;
    }
  return -1;
}

/*
 * logtb_find_client_name - find client name of transaction index
 *
 * return: client name
 *
 *   tran_index(in): Index of transaction
 */
char *
logtb_find_client_name (int tran_index)
{
  LOG_TDES *tdes;

  tdes = LOG_FIND_TDES (tran_index);
  if (tdes != NULL && tdes->trid != NULL_TRANID)
    {
      return tdes->client.db_user;
    }
  return NULL;
}

/*
 * logtb_set_user_name - set client name of transaction index
 *
 * return: 
 *
 *   tran_index(in): Index of transaction
 *   user_name(in):
 */
void
logtb_set_user_name (int tran_index, const char *user_name)
{
  LOG_TDES *tdes;

  tdes = LOG_FIND_TDES (tran_index);
  if (tdes != NULL && tdes->trid != NULL_TRANID)
    {
      strncpy (tdes->client.db_user,
	       (user_name) ? user_name : log_Client_id_unknown_string,
	       sizeof (tdes->client.db_user) - 1);
    }
  return;
}

/*
 * logtb_set_current_user_name - set client name of current transaction
 *
 * return:
 */
void
logtb_set_current_user_name (THREAD_ENTRY * thread_p, const char *user_name)
{
  logtb_set_user_name (LOG_FIND_THREAD_TRAN_INDEX (thread_p), user_name);
}

/*
 * logtb_find_client_hostname - find client hostname of transaction index
 *
 * return: client hostname
 *
 *   tran_index(in): Index of transaction
 */
char *
logtb_find_client_hostname (int tran_index)
{
  LOG_TDES *tdes;

  tdes = LOG_FIND_TDES (tran_index);
  if (tdes != NULL && tdes->trid != NULL_TRANID)
    {
      return tdes->client.host_name;
    }
  return NULL;
}

/*
 * logtb_find_client_name_host_pid - find client identifiers(user_name,
 *                                host_name, host_pid) OF TRANSACTION INDEX
 *
 * return: NO_ERROR if all OK, ER_ status otherwise
 *
 *   tran_index(in): Index of transaction
 *   client_prog_name(in/out): Name of the client program
 *   client_user_name(in/out): Name of the client user
 *   client_host_name(in/out): Name of the client host
 *   client_pid(in/out): Identifier of the process of the host where the client
 *                      client transaction runs.
 *
 * Note: Find the client user name, host name, and process identifier
 *              associated with the given transaction index.
 *
 *       The above pointers are valid until the client is unregister.
 */
int
logtb_find_client_name_host_pid (int tran_index, char **client_prog_name,
				 char **client_user_name,
				 char **client_host_name, int *client_pid)
{
  LOG_TDES *tdes;		/* Transaction descriptor */

  tdes = LOG_FIND_TDES (tran_index);

  if (tdes == NULL || tdes->trid == NULL_TRANID)
    {
      *client_prog_name = (char *) log_Client_id_unknown_string;
      *client_user_name = (char *) log_Client_id_unknown_string;
      *client_host_name = (char *) log_Client_id_unknown_string;
      *client_pid = -1;
      return ER_FAILED;
    }

  *client_prog_name = tdes->client.program_name;
  *client_user_name = tdes->client.db_user;
  *client_host_name = tdes->client.host_name;
  *client_pid = tdes->client.process_id;

  return NO_ERROR;
}

int
logtb_get_client_ids (int tran_index, LOG_CLIENTIDS * client_info)
{
  LOG_TDES *tdes;

  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL || tdes->trid == NULL_TRANID)
    {
      return ER_FAILED;
    }

  *client_info = tdes->client;

  return NO_ERROR;
}

/*
 * xlogtb_get_pack_tran_table - return transaction info stored on transaction table
 *
 * return: NO_ERROR if all OK, ER status otherwise
 *
 *   buffer_p(in/out): returned buffer poitner
 *   size_p(in/out): returned buffer size
 *
 * Note: This is a support function which is used mainly for the
 *              killtran utility. It returns a variety of client information
 *              of transactions.  This will be displayed by killtran so that
 *              the user can select which transaction id needs to be aborted.
 *
 *       The buffer is allocated using malloc and must be freed by the
 *       caller.
 */
int
xlogtb_get_pack_tran_table (THREAD_ENTRY * thread_p, char **buffer_p,
			    int *size_p, int include_query_exec_info)
{
  int error_code = NO_ERROR;
  int num_clients = 0;
  int i;
  int size;
  char *buffer, *ptr;
  LOG_TDES *tdes;		/* Transaction descriptor */
#if defined(SERVER_MODE)
  UINT64 current_msec = 0;
  TRAN_QUERY_EXEC_INFO *query_exec_info = NULL;
  XASL_CACHE_ENTRY *ent;
#endif

#if defined(SERVER_MODE)
  if (include_query_exec_info)
    {
      query_exec_info =
	(TRAN_QUERY_EXEC_INFO *) calloc (NUM_TOTAL_TRAN_INDICES,
					 sizeof (TRAN_QUERY_EXEC_INFO));

      if (query_exec_info == NULL)
	{
	  error_code = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto error;
	}

      current_msec = thread_get_log_clock_msec ();
    }
#endif

  /* Note, we'll be in a critical section while we gather the data but
   * the section ends as soon as we return the data.  This means that the
   * transaction table can change after the information is used.
   */

  TR_TABLE_CS_ENTER_READ_MODE (thread_p);

  size = OR_INT_SIZE;		/* Number of client transactions */

  /* Find size of needed buffer */
  for (i = 0; i < NUM_TOTAL_TRAN_INDICES; i++)
    {
      tdes = log_Gl.trantable.all_tdes[i];
      if (tdes == NULL
	  || tdes->trid == NULL_TRANID
	  || tdes->tran_index == LOG_SYSTEM_TRAN_INDEX)
	{
	  /* The index is not assigned or is system transaction (no-client) */
	  continue;
	}

      size += 3 * OR_INT_SIZE	/* tran index + tran state + process id */
	+ or_packed_string_length (tdes->client.db_user, NULL)
	+ or_packed_string_length (tdes->client.program_name, NULL)
	+ or_packed_string_length (tdes->client.login_name, NULL)
	+ or_packed_string_length (tdes->client.host_name, NULL);

#if defined(SERVER_MODE)
      if (include_query_exec_info)
	{
	  if (tdes->query_start_time > 0)
	    {
	      query_exec_info[i].query_time =
		(float) (current_msec - tdes->query_start_time) / 1000.0;
	    }

	  if (tdes->tran_start_time > 0)
	    {
	      query_exec_info[i].tran_time =
		(float) (current_msec - tdes->tran_start_time) / 1000.0;
	    }

	  lock_get_lock_holder_tran_index (thread_p,
					   &query_exec_info[i].
					   wait_for_tran_index_string,
					   tdes->tran_index,
					   tdes->waiting_for_res);

	  if (!XASL_ID_IS_NULL (&tdes->xasl_id))
	    {
	      /* retrieve query statement in the xasl_cache entry */
	      ent =
		qexec_check_xasl_cache_ent_by_xasl (thread_p, &tdes->xasl_id,
						    -1, NULL);

	      /* entry can be NULL, if xasl cache entry is deleted */
	      if (ent != NULL)
		{
		  if (ent->sql_info.sql_hash_text != NULL)
		    {
		      char *sql = ent->sql_info.sql_hash_text;

		      if (qmgr_get_sql_id (thread_p,
					   &query_exec_info[i].sql_id,
					   sql, strlen (sql)) != NO_ERROR)
			{
			  goto error;
			}

		      if (ent->sql_info.sql_user_text != NULL)
			{
			  sql = ent->sql_info.sql_user_text;
			}

		      /* copy query string */
		      query_exec_info[i].query_stmt = strdup (sql);
		      if (query_exec_info[i].query_stmt == NULL)
			{
			  error_code = ER_OUT_OF_VIRTUAL_MEMORY;
			  goto error;
			}
		    }
		  (void) qexec_remove_my_tran_id_in_xasl_entry (thread_p,
								ent, true);
		}

	      /* structure copy */
	      XASL_ID_COPY (&query_exec_info[i].xasl_id, &tdes->xasl_id);
	    }
	  else
	    {
	      XASL_ID_SET_NULL (&query_exec_info[i].xasl_id);
	    }

	  size += 2 * OR_FLOAT_SIZE	/* query time + tran time */
	    + or_packed_string_length (query_exec_info[i].
				       wait_for_tran_index_string, NULL)
	    + or_packed_string_length (query_exec_info[i].query_stmt, NULL)
	    + or_packed_string_length (query_exec_info[i].sql_id, NULL)
	    + OR_XASL_ID_SIZE;
	}
#endif
      num_clients++;
    }

  /* Now allocate the area and pack the information */
  buffer = (char *) malloc (size);
  if (buffer == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, size);
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      goto error;
    }

  ptr = buffer;
  ptr = or_pack_int (ptr, num_clients);

  /* Find size of needed buffer */
  for (i = 0; i < NUM_TOTAL_TRAN_INDICES; i++)
    {
      tdes = log_Gl.trantable.all_tdes[i];
      if (tdes == NULL
	  || tdes->trid == NULL_TRANID
	  || tdes->tran_index == LOG_SYSTEM_TRAN_INDEX)
	{
	  /* The index is not assigned or is system transaction (no-client) */
	  continue;
	}

      ptr = or_pack_int (ptr, tdes->tran_index);
      ptr = or_pack_int (ptr, tdes->state);
      ptr = or_pack_int (ptr, tdes->client.process_id);
      ptr = or_pack_string (ptr, tdes->client.db_user);
      ptr = or_pack_string (ptr, tdes->client.program_name);
      ptr = or_pack_string (ptr, tdes->client.login_name);
      ptr = or_pack_string (ptr, tdes->client.host_name);

#if defined(SERVER_MODE)
      if (include_query_exec_info)
	{
	  ptr = or_pack_float (ptr, query_exec_info[i].query_time);
	  ptr = or_pack_float (ptr, query_exec_info[i].tran_time);
	  ptr =
	    or_pack_string (ptr,
			    query_exec_info[i].wait_for_tran_index_string);
	  ptr = or_pack_string (ptr, query_exec_info[i].query_stmt);
	  ptr = or_pack_string (ptr, query_exec_info[i].sql_id);
	  OR_PACK_XASL_ID (ptr, &query_exec_info[i].xasl_id);
	}
#endif
    }

  *buffer_p = buffer;
  *size_p = size;

error:

#if defined(SERVER_MODE)
  if (query_exec_info != NULL)
    {
      for (i = 0; i < NUM_TOTAL_TRAN_INDICES; i++)
	{
	  if (query_exec_info[i].wait_for_tran_index_string)
	    {
	      free_and_init (query_exec_info[i].wait_for_tran_index_string);
	    }
	  if (query_exec_info[i].query_stmt)
	    {
	      free_and_init (query_exec_info[i].query_stmt);
	    }
	  if (query_exec_info[i].sql_id)
	    {
	      free_and_init (query_exec_info[i].sql_id);
	    }
	}
      free_and_init (query_exec_info);
    }
#endif

  TR_TABLE_CS_EXIT (thread_p);
  return error_code;
}

/*
 * logtb_find_current_client_type - find client type of current transaction
 *
 * return: client type
 */
int
logtb_find_current_client_type (THREAD_ENTRY * thread_p)
{
  return logtb_find_client_type (LOG_FIND_THREAD_TRAN_INDEX (thread_p));
}

/*
 * logtb_find_current_client_name - find client name of current transaction
 *
 * return: client name
 */
char *
logtb_find_current_client_name (THREAD_ENTRY * thread_p)
{
  return logtb_find_client_name (LOG_FIND_THREAD_TRAN_INDEX (thread_p));
}

/*
 * logtb_find_current_client_hostname - find client hostname of current transaction
 *
 * return: client hostname
 */
char *
logtb_find_current_client_hostname (THREAD_ENTRY * thread_p)
{
  return logtb_find_client_hostname (LOG_FIND_THREAD_TRAN_INDEX (thread_p));
}

/*
 * logtb_find_current_tran_lsa - find current transaction log sequence address
 *
 * return:  LOG_LSA *
 */
LOG_LSA *
logtb_find_current_tran_lsa (THREAD_ENTRY * thread_p)
{
  LOG_TDES *tdes;		/* Transaction descriptor */

  tdes = LOG_FIND_CURRENT_TDES (thread_p);
  return ((tdes != NULL) ? &tdes->tail_lsa : NULL);
}

/*
 * logtb_find_state - find the state of the transaction
 *
 * return: TRAN_STATE
 *
 *   tran_index(in): transaction index
 */
TRAN_STATE
logtb_find_state (int tran_index)
{
  LOG_TDES *tdes;		/* Transaction descriptor */

  tdes = LOG_FIND_TDES (tran_index);
  if (tdes != NULL)
    {
      return tdes->state;
    }
  else
    {
      return TRAN_UNACTIVE_UNKNOWN;
    }
}

/*
 * xlogtb_reset_wait_msecs - reset future waiting times
 *
 * return: The old wait_msecs.
 *
 *   wait_msecs(in): Wait for at least this number of milliseconds to acquire a lock
 *               before the transaction is timed out.
 *               A negative value (e.g., -1) means wait forever until a lock
 *               is granted or transaction is selected as a victim of a
 *               deadlock.
 *               A value of zero means do not wait at all, timeout immediately
 *               (in milliseconds)
 *
 * Note:Reset the default waiting time for the current transaction index(client).
 */
int
xlogtb_reset_wait_msecs (THREAD_ENTRY * thread_p, int wait_msecs)
{
  LOG_TDES *tdes;		/* Transaction descriptor */
  int old_wait_msecs;		/* The old waiting time to be returned */
  int tran_index;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
      return -1;
    }

  old_wait_msecs = tdes->wait_msecs;
  tdes->wait_msecs = wait_msecs;

  return old_wait_msecs;
}

/*
 * logtb_find_wait_msecs -  find waiting times for given transaction
 *
 * return: wait_msecs...
 *
 *   tran_index(in): Index of transaction
 */
int
logtb_find_wait_msecs (int tran_index)
{
  LOG_TDES *tdes;		/* Transaction descriptor */

  tdes = LOG_FIND_TDES (tran_index);
  if (tdes != NULL)
    {
      return tdes->wait_msecs;
    }
  else
    {
      assert (false);
      return 0;
    }
}

/*
 * logtb_find_current_wait_msecs - find waiting times for current transaction
 *
 * return : wait_msecs...
 *
 * Note: Find the waiting time for the current transaction.
 */
int
logtb_find_current_wait_msecs (THREAD_ENTRY * thread_p)
{
  LOG_TDES *tdes;		/* Transaction descriptor */
  int tran_index;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes != NULL)
    {
      return tdes->wait_msecs;
    }
  else
    {
      return 0;
    }
}

/*
 * logtb_find_interrupt - 
 *
 * return :
 *
 *  tran_index(in):
 *  interrupt(out):
 *
 */
int
logtb_find_interrupt (int tran_index, bool * interrupt)
{
  LOG_TDES *tdes;

  assert (interrupt);

  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL || tdes->trid == NULL_TRANID)
    {
      return ER_FAILED;
    }
  else
    {
      *interrupt = tdes->interrupt ? true : false;
    }

  return NO_ERROR;
}

/*
 * xlogtb_reset_isolation - reset consistency of transaction
 *
 * return: error code.
 *
 *   isolation(in): New Isolation level. One of the following:
 *                         TRAN_SERIALIZABLE
 *                         TRAN_REPEATABLE_READ
 *                         TRAN_READ_COMMITTED
 *   unlock_by_isolation(in): unlock by isolation during reset
 *
 * Note:Reset the default isolation level for the current transaction
 *              index (client).
 *
 * Note/Warning: This function must be called when the current transaction has
 *               not been done any work (i.e, just after restart, commit, or
 *               abort), otherwise, its isolation behaviour will be undefined.
 */
int
xlogtb_reset_isolation (THREAD_ENTRY * thread_p, TRAN_ISOLATION isolation,
			bool unlock_by_isolation)
{
  TRAN_ISOLATION old_isolation;
  int error_code = NO_ERROR;
  LOG_TDES *tdes;		/* Transaction descriptor */
  int tran_index;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);

  if (IS_VALID_ISOLATION_LEVEL (isolation) && tdes != NULL)
    {
      old_isolation = tdes->isolation;
      tdes->isolation = isolation;
      if (unlock_by_isolation == true)
	{
	  lock_unlock_by_isolation_level (thread_p);
	}
    }
  else
    {
      if (mvcc_Enabled)
	{
	  er_set (ER_SYNTAX_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_MVCC_LOG_INVALID_ISOLATION_LEVEL, 0);
	  error_code = ER_MVCC_LOG_INVALID_ISOLATION_LEVEL;
	}
      else
	{
	  er_set (ER_SYNTAX_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_LOG_INVALID_ISOLATION_LEVEL, 2,
		  TRAN_MINVALUE_ISOLATION, TRAN_MAXVALUE_ISOLATION);
	  error_code = ER_LOG_INVALID_ISOLATION_LEVEL;
	}
    }

  return error_code;
}

/*
 * logtb_find_isolation - find the isolation level for given trans
 *
 * return: isolation
 *
 *   tran_index(in):Index of transaction
 */
TRAN_ISOLATION
logtb_find_isolation (int tran_index)
{
  LOG_TDES *tdes;		/* Transaction descriptor */

  tdes = LOG_FIND_TDES (tran_index);
  if (tdes != NULL)
    {
      return tdes->isolation;
    }
  else
    {
      return TRAN_UNKNOWN_ISOLATION;
    }
}

/*
 * logtb_find_current_isolation - find the isolation level for current
 *                             transaction
 *
 * return: isolation...
 *
 * Note: Find the isolation level for the current transaction
 */
TRAN_ISOLATION
logtb_find_current_isolation (THREAD_ENTRY * thread_p)
{
  int tran_index;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  return logtb_find_isolation (tran_index);
}

/*
 * xlogtb_set_interrupt - indicate interrupt to a future caller of current
 *                     transaction
 *
 * return: nothing
 *
 *   set(in): true for set and false for clear
 *
 * Note: Set the interrupt falg for the current execution, so that the
 *              next caller obtains an interrupt.
 */
void
xlogtb_set_interrupt (THREAD_ENTRY * thread_p, int set)
{
  logtb_set_tran_index_interrupt (thread_p,
				  LOG_FIND_THREAD_TRAN_INDEX (thread_p), set);
}

/*
 * logtb_set_tran_index_interrupt - indicate interrupt to a future caller for an
 *                               specific transaction index
 *
 * return: false is returned when the tran_index is not associated
 *              with a transaction
 *
 *   tran_index(in): Transaction index
 *   set(in): true for set and false for clear
 *
 * Note:Set the interrupt flag for the execution of the given transaction,
 *              so that the next caller obtains an interrupt.
 */
bool
logtb_set_tran_index_interrupt (THREAD_ENTRY * thread_p, int tran_index,
				int set)
{
  LOG_TDES *tdes;		/* Transaction descriptor */

  if (log_Gl.trantable.area != NULL)
    {
      tdes = LOG_FIND_TDES (tran_index);
      if (tdes != NULL && tdes->trid != NULL_TRANID)
	{
	  if (tdes->interrupt != set)
	    {
	      TR_TABLE_CS_ENTER (thread_p);

	      tdes->interrupt = set;
	      if (set == true)
		{
		  log_Gl.trantable.num_interrupts++;
		}
	      else
		{
		  log_Gl.trantable.num_interrupts--;
		}

	      TR_TABLE_CS_EXIT (thread_p);
	    }

	  if (set == true)
	    {
	      pgbuf_force_to_check_for_interrupts ();
	      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE,
		      ER_INTERRUPTING, 1, tran_index);
	      mnt_tran_interrupts (thread_p);
	    }

	  return true;
	}
    }

  return false;
}

/*
 * logtb_is_interrupted_tdes -
 *
 * return:
 *
 *   tdes(in/out):
 *   clear(in/out):
 *   continue_checking(out):
 *
 * Note:
 */
static bool
logtb_is_interrupted_tdes (THREAD_ENTRY * thread_p, LOG_TDES * tdes,
			   bool clear, bool * continue_checking)
{
  int interrupt;
  INT64 now;
#if !defined(SERVER_MODE)
  struct timeval tv;
#endif /* !SERVER_MODE */

  interrupt = tdes->interrupt;
  if (!LOG_ISTRAN_ACTIVE (tdes))
    {
      interrupt = false;

      if (log_Gl.trantable.num_interrupts > 0)
	{
	  *continue_checking = true;
	}
      else
	{
	  *continue_checking = false;
	}
    }
  else if (interrupt == true && clear == true)
    {
      tdes->interrupt = false;

      TR_TABLE_CS_ENTER (thread_p);
      log_Gl.trantable.num_interrupts--;
      if (log_Gl.trantable.num_interrupts > 0)
	{
	  *continue_checking = true;
	}
      else
	{
	  *continue_checking = false;
	}
      TR_TABLE_CS_EXIT (thread_p);
    }
  else if (interrupt == false && tdes->query_timeout > 0)
    {
      /* In order to prevent performance degradation, we use log_Clock_msec
       * set by thread_log_clock_thread instead of calling gettimeofday here
       * if the system supports atomic built-ins.
       */
#if defined(SERVER_MODE)
      now = thread_get_log_clock_msec ();
#else /* SERVER_MODE */
      gettimeofday (&tv, NULL);
      now = (tv.tv_sec * 1000LL) + (tv.tv_usec / 1000LL);
#endif /* !SERVER_MODE */
      if (tdes->query_timeout < now)
	{
	  er_log_debug (ARG_FILE_LINE,
			"logtb_is_interrupted_tdes: timeout %lld milliseconds "
			"delayed (expected=%lld, now=%lld)",
			now - tdes->query_timeout, tdes->query_timeout, now);
	  interrupt = true;
	}
    }
  return interrupt;
}

/*
 * logtb_is_interrupted - find if execution must be stopped due to
 *			  an interrupt (^C)
 *
 * return:
 *
 *   clear(in): true if the interrupt should be cleared.
 *   continue_checking(in): Set as a side effect to true if there are more
 *                        interrupts to check or to false if there are not
 *                        more interrupts.
 *
 * Note: Find if the current execution must be stopped due to an
 *              interrupt (^C). If clear is true, the interruption flag is
 *              cleared; This is the expected case, once someone is notified,
 *              we do not have to keep the flag on.
 *
 *       If the transaction is not active, false is returned. For
 *              example, in the middle of an undo action, the transaction will
 *              not be interrupted. The recovery manager will interrupt the
 *              transaction at the end of the undo action...int this case the
 *              transaction will be partially aborted.
 */
bool
logtb_is_interrupted (THREAD_ENTRY * thread_p, bool clear,
		      bool * continue_checking)
{
  LOG_TDES *tdes;		/* Transaction descriptor */
  int tran_index;

  if (log_Gl.trantable.area == NULL)
    {
      return false;
    }
  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL)
    {
      return false;
    }

  return logtb_is_interrupted_tdes (thread_p, tdes, clear, continue_checking);
}

/*
 * logtb_is_interrupted_tran - find if the execution of the given transaction
 *			       must be stopped due to an interrupt (^C)
 *
 * return:
 *
 *   clear(in): true if the interrupt should be cleared.
 *   continue_checking(in): Set as a side effect to true if there are more
 *                        interrupts to check or to false if there are not
 *                        more interrupts.
 *   tran_index(in):
 *
 * Note: Find if the execution o fthe given transaction must be stopped
 *              due to an interrupt (^C). If clear is true, the
 *              interruption flag is cleared; This is the expected case, once
 *              someone is notified, we do not have to keep the flag on.
 *       This function is called to see if a transaction that is
 *              waiting (e.g., suspended wiating on a lock) on an event must
 *              be interrupted.
 */
bool
logtb_is_interrupted_tran (THREAD_ENTRY * thread_p, bool clear,
			   bool * continue_checking, int tran_index)
{
  LOG_TDES *tdes;		/* Transaction descriptor */

  tdes = LOG_FIND_TDES (tran_index);
  if (log_Gl.trantable.area == NULL || tdes == NULL)
    {
      return false;
    }

  return logtb_is_interrupted_tdes (thread_p, tdes, clear, continue_checking);
}

/*
 * xlogtb_set_suppress_repl_on_transaction - set or unset suppress_replication flag
 *                                           on transaction descriptor
 *
 * return: nothing
 *
 *   set(in): non-zero to set, zero to unset
 */
void
xlogtb_set_suppress_repl_on_transaction (THREAD_ENTRY * thread_p, int set)
{
  logtb_set_suppress_repl_on_transaction (thread_p,
					  LOG_FIND_THREAD_TRAN_INDEX
					  (thread_p), set);
}

/*
 * logtb_set_suppress_repl_on_transaction - set or unset suppress_replication flag
 *                                          on transaction descriptor
 *
 * return: false is returned when the tran_index is not associated
 *              with a transaction
 *
 *   tran_index(in): Transaction index
 *   set(in): non-zero to set, zero to unset
 */
bool
logtb_set_suppress_repl_on_transaction (THREAD_ENTRY * thread_p,
					int tran_index, int set)
{
  LOG_TDES *tdes;		/* Transaction descriptor */

  if (log_Gl.trantable.area != NULL)
    {
      tdes = LOG_FIND_TDES (tran_index);
      if (tdes != NULL && tdes->trid != NULL_TRANID)
	{
	  if (tdes->suppress_replication != set)
	    {
	      TR_TABLE_CS_ENTER (thread_p);

	      tdes->suppress_replication = set;

	      TR_TABLE_CS_EXIT (thread_p);
	    }
	  return true;
	}
    }
  return false;
}


/*
 * logtb_is_active - is transaction active ?
 *
 * return:
 *
 *   trid(in): Transaction identifier
 *
 * Note: Find if given transaction is an active one. This function
 *              execute a sequential search in the transaction table to find
 *              out the given transaction. The function bypasses the search if
 *              the given trid is current transaction.
 *
 * Note:        The assumption of this function is that the transaction table
 *              is not very big and that most of the time the search is
 *              avoided. if this assumption becomes false, we may need to
 *              define a hash table from trid to tdes to speed up the search.
 */
bool
logtb_is_active (THREAD_ENTRY * thread_p, TRANID trid)
{
  int i;
  LOG_TDES *tdes;		/* Transaction descriptor */
  bool active = false;
  int tran_index;

  if (!LOG_ISRESTARTED ())
    {
      return false;
    }

  /* Avoid searching as much as possible */
  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes != NULL && tdes->trid == trid)
    {
      active = LOG_ISTRAN_ACTIVE (tdes);
    }
  else
    {
      TR_TABLE_CS_ENTER_READ_MODE (thread_p);
      /* Search the transaction table for such transaction */
      for (i = 0; i < NUM_TOTAL_TRAN_INDICES; i++)
	{
	  tdes = log_Gl.trantable.all_tdes[i];
	  if (tdes != NULL && tdes->trid != NULL_TRANID && tdes->trid == trid)
	    {
	      active = LOG_ISTRAN_ACTIVE (tdes);
	      break;
	    }
	}
      TR_TABLE_CS_EXIT (thread_p);
    }

  return active;
}

/*
 * logtb_is_current_active - is current transaction active ?
 *
 * return:
 *
 * Note: Find if the current transaction is an active one.
 */
bool
logtb_is_current_active (THREAD_ENTRY * thread_p)
{
  LOG_TDES *tdes;		/* Transaction descriptor */
  int tran_index;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);

  if (tdes != NULL && LOG_ISTRAN_ACTIVE (tdes))
    {
      return true;
    }
  else
    {
      return false;
    }
}

/*
 * logtb_istran_finished - is transaction finished?
 *
 * return:
 *
 *   trid(in): Transaction identifier
 *
 * Note: Find if given transaction exists. That is, find if the
 *              transaction has completely finished its execution.
 *              Note that this function differs from log_istran_active in
 *              that a transaction in commit or abort state is not active but
 *              it is still alive (i.e., has not done completely).
 *              This function execute a sequential search in the transaction
 *              table to find out the given transaction. The function bypasses
 *              the search if the given trid is current transaction.
 *
 *       The assumption of this function is that the transaction table
 *              is not very big and that most of the time the search is
 *              avoided. if this assumption becomes false, we may need to
 *              define a hash table from trid to tdes to speed up the search.
 */
bool
logtb_istran_finished (THREAD_ENTRY * thread_p, TRANID trid)
{
  int i;
  LOG_TDES *tdes;		/* Transaction descriptor */
  bool active = true;
  int tran_index;

  /* Avoid searching as much as possible */
  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes != NULL && tdes->trid == trid)
    {
      active = false;
    }
  else
    {
      TR_TABLE_CS_ENTER_READ_MODE (thread_p);
      /* Search the transaction table for such transaction */
      for (i = 0; i < NUM_TOTAL_TRAN_INDICES; i++)
	{
	  tdes = log_Gl.trantable.all_tdes[i];
	  if (tdes != NULL && tdes->trid != NULL_TRANID && tdes->trid == trid)
	    {
	      active = false;
	      break;
	    }
	}
      TR_TABLE_CS_EXIT (thread_p);
    }

  return active;
}

/*
 * logtb_has_updated - has transaction updated the database ?
 *
 * return:
 *
 */
bool
logtb_has_updated (THREAD_ENTRY * thread_p)
{
  LOG_TDES *tdes;		/* Transaction descriptor */
  int tran_index;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes != NULL && !LSA_ISNULL (&tdes->tail_lsa))
    {
      return true;
    }
  else
    {
      return false;
    }
}

/*
 * logtb_disable_update -
 *   return: none
 */
void
logtb_disable_update (THREAD_ENTRY * thread_p)
{
  db_Disable_modifications = 1;
  er_log_debug (ARG_FILE_LINE,
		"logtb_disable_update: db_Disable_modifications = %d\n",
		db_Disable_modifications);
}

/*
 * logtb_enable_update -
 *   return: none
 */
void
logtb_enable_update (THREAD_ENTRY * thread_p)
{
  if (prm_get_bool_value (PRM_ID_READ_ONLY_MODE) == false)
    {
      db_Disable_modifications = 0;
      er_log_debug (ARG_FILE_LINE,
		    "logtb_enable_update: db_Disable_modifications = %d\n",
		    db_Disable_modifications);
    }
}

/*
 * logtb_set_to_system_tran_index - set to tran index system
 *
 * return: nothing
 *
 * Note: The current log_tran_index is set to the system transaction index.
 */
void
logtb_set_to_system_tran_index (THREAD_ENTRY * thread_p)
{
  LOG_SET_CURRENT_TRAN_INDEX (thread_p, LOG_SYSTEM_TRAN_INDEX);
}

/*
 * logtb_mvcc_free_class_unique_stats - free class unique statistics
 *
 * return: nothing
 */
static void
logtb_mvcc_free_class_unique_stats (LOG_MVCC_CLASS_UPDATE_STATS * class_stats)
{
  if (class_stats == NULL)
    {
      return;
    }

  if (class_stats->unique_stats != NULL)
    {
      free_and_init (class_stats->unique_stats);
      class_stats->n_btids = class_stats->n_max_btids = 0;
    }
}

/*
 * logtb_mvcc_free_update_stats () - Free logged list of update statistics.
 *
 * return	    : Void.
 * log_upd_stats (in) : List of logged update statistics records.
 */
static void
logtb_mvcc_free_update_stats (LOG_MVCC_UPDATE_STATS * log_upd_stats)
{
  LOG_MVCC_CLASS_UPDATE_STATS *entry = NULL, *save_next = NULL;

  for (entry = log_upd_stats->crt_tran_entries; entry != NULL;
       entry = save_next)
    {
      save_next = entry->next;
      logtb_mvcc_free_class_unique_stats (entry);
      free (entry);
    }

  for (entry = log_upd_stats->free_entries; entry != NULL; entry = save_next)
    {
      save_next = entry->next;
      logtb_mvcc_free_class_unique_stats (entry);
      free (entry);
    }

  log_upd_stats->crt_tran_entries = NULL;
  log_upd_stats->free_entries = NULL;
}

/*
 * logtb_mvcc_clear_update_stats () - Clear logged update statistics.
 *				      Entries are not actually freed, they are
 *				      appended to a list of free entries ready
 *				      to be reused.
 *
 * return	    : Void.
 * log_upd_stats (in) : Pointer to update statistics log.
 */
static void
logtb_mvcc_clear_update_stats (LOG_MVCC_UPDATE_STATS * log_upd_stats)
{
  LOG_MVCC_CLASS_UPDATE_STATS *entry = NULL, *save_next = NULL;

  for (entry = log_upd_stats->crt_tran_entries; entry != NULL;
       entry = save_next)
    {
      save_next = entry->next;
      logtb_mvcc_free_class_stats (log_upd_stats, entry);
    }

  log_upd_stats->crt_tran_entries = NULL;
}

/*
 * logtb_mvcc_alloc_class_stats () - Allocate a new entry to class statistics
 *				     records during transaction. First check if
 *				     there are any entries in the list of free
 *				     entries, and allocate memory for a new one
 *				     only if this list is empty.
 *
 * return	    : Pointer to allocated entry.
 * thread_p (in)    : Thread entry.
 * log_upd_stats (in) : 
 */
static LOG_MVCC_CLASS_UPDATE_STATS *
logtb_mvcc_alloc_class_stats (LOG_MVCC_UPDATE_STATS * log_upd_stats)
{
  LOG_MVCC_CLASS_UPDATE_STATS *new_entry = NULL;

  assert (log_upd_stats != NULL);

  if (log_upd_stats->free_entries != NULL)
    {
      new_entry = log_upd_stats->free_entries;
      log_upd_stats->free_entries = new_entry->next;
      new_entry->next = NULL;
      new_entry->n_btids = 0;
      new_entry->count_state = COS_NOT_LOADED;
    }
  else
    {
      /* new_entry will be added to log_upd_stats->free_entries later */
      new_entry =
	(LOG_MVCC_CLASS_UPDATE_STATS *)
	malloc (sizeof (LOG_MVCC_CLASS_UPDATE_STATS));
      if (new_entry == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, sizeof (LOG_MVCC_CLASS_UPDATE_STATS));
	  return NULL;
	}

      /* clear all data */
      memset (new_entry, 0, sizeof (LOG_MVCC_CLASS_UPDATE_STATS));
    }

  return new_entry;
}

/*
 * logtb_mvcc_free_class_stats () - Append entry to a list of free entries.
 *
 * return	    : Void.
 * log_upd_stats (in) : Update statistics log.
 * entry (in)	    : Entry to free.
 */
static void
logtb_mvcc_free_class_stats (LOG_MVCC_UPDATE_STATS * log_upd_stats,
			     LOG_MVCC_CLASS_UPDATE_STATS * entry)
{
  assert (log_upd_stats != NULL);

  entry->next = log_upd_stats->free_entries;
  log_upd_stats->free_entries = entry;
}

/*
 * logtb_mvcc_create_btid_unique_stats () - allocates memory and initializes
 *					    statistics associated with btid
 *
 * return	    : The address of newly created statistics structure
 * thread_p(in)	    :
 * class_stats (in) : entry associated with a class
 * btid (in)	    : Id of unique index for which the statistics will be
 *		      created
 */
static LOG_MVCC_BTID_UNIQUE_STATS *
logtb_mvcc_create_btid_unique_stats (THREAD_ENTRY * thread_p,
				     LOG_MVCC_CLASS_UPDATE_STATS *
				     class_stats, const BTID * btid)
{
  LOG_MVCC_BTID_UNIQUE_STATS *unique_stats = NULL;

  if (class_stats == NULL || btid == NULL)
    {
      assert (false);
      return NULL;
    }

  /* if space is full then extend it */
  if (class_stats->n_btids == class_stats->n_max_btids)
    {
      class_stats->n_max_btids += UNIQUE_STAT_INFO_INCREMENT;
      class_stats->unique_stats =
	(LOG_MVCC_BTID_UNIQUE_STATS *) realloc (class_stats->unique_stats,
						class_stats->n_max_btids *
						sizeof
						(LOG_MVCC_BTID_UNIQUE_STATS));
      if (class_stats->unique_stats == NULL)
	{
	  return NULL;
	}
    }

  unique_stats = &class_stats->unique_stats[class_stats->n_btids++];

  unique_stats->btid = *btid;
  unique_stats->deleted = false;

  unique_stats->tran_stats.num_keys = 0;
  unique_stats->tran_stats.num_oids = 0;
  unique_stats->tran_stats.num_nulls = 0;

  unique_stats->global_stats.num_keys = -1;
  unique_stats->global_stats.num_oids = -1;
  unique_stats->global_stats.num_nulls = -1;

  return unique_stats;
}

/*
 * logtb_mvcc_create_class_stats () - creates an entry of statistics for the
 *				      given class
 *
 * return	    : return the adddress of newly created entry
 * thread_p(in)	    :
 * class_oid (in)   : OID of the class for which the entry will be created
 */
static LOG_MVCC_CLASS_UPDATE_STATS *
logtb_mvcc_create_class_stats (THREAD_ENTRY * thread_p, const OID * class_oid)
{
  LOG_TDES *tdes = LOG_FIND_TDES (LOG_FIND_THREAD_TRAN_INDEX (thread_p));
  LOG_MVCC_CLASS_UPDATE_STATS *entry = NULL;
  int error = NO_ERROR;

  /* An entry for class_oid was not found, create a new one */
  entry = logtb_mvcc_alloc_class_stats (&tdes->log_upd_stats);
  if (entry == NULL)
    {
      /* ER_OUT_OF_VIRTUAL_MEMORY */
      return NULL;
    }

  COPY_OID (&entry->class_oid, class_oid);

  /* Append entry to current list of inserted/deleted records */
  entry->next = tdes->log_upd_stats.crt_tran_entries;
  tdes->log_upd_stats.crt_tran_entries = entry;

  return entry;
}

/*
 * logtb_mvcc_find_class_stats () - searches the list of class statistics entries
 *				    for statistics for a specified class
 *
 * return	    : address of found (or newly created) entry or null
 *		      otherwise
 * thread_p(in)	    :
 * class_oid (in)   : OID of the class for which the entry will be created
 * create(in)	    : true if the caller needs a new entry to be created if not
 *		      found an already existing one
 */
LOG_MVCC_CLASS_UPDATE_STATS *
logtb_mvcc_find_class_stats (THREAD_ENTRY * thread_p, const OID * class_oid,
			     bool create)
{
  LOG_TDES *tdes = LOG_FIND_TDES (LOG_FIND_THREAD_TRAN_INDEX (thread_p));
  LOG_MVCC_CLASS_UPDATE_STATS *entry = NULL;

  assert (tdes != NULL && class_oid != NULL);

  for (entry = tdes->log_upd_stats.crt_tran_entries;
       entry != NULL; entry = entry->next)
    {
      if (OID_EQ (class_oid, &entry->class_oid))
	{
	  /* Found, stop looking */
	  break;
	}
    }

  if (entry == NULL && create)
    {
      entry = logtb_mvcc_create_class_stats (thread_p, class_oid);
    }

  return entry;
}

/*
 * logtb_mvcc_find_btid_stats () - searches the list of statistics of a given class
 *			       for a specified index
 *
 * return	    : address of found (or newly created) statistics or null
 *		      otherwise
 * thread_p(in)	    :
 * btid (in)	    : B-tree id to be searched
 * create(in)	    : true if the caller needs a new entry to be created if not
 *		      found an already existing one
 */
LOG_MVCC_BTID_UNIQUE_STATS *
logtb_mvcc_find_btid_stats (THREAD_ENTRY * thread_p,
			    LOG_MVCC_CLASS_UPDATE_STATS * class_stats,
			    const BTID * btid, bool create)
{
  LOG_MVCC_BTID_UNIQUE_STATS *unique_stats = NULL;
  int idx;

  if (class_stats == NULL || btid == NULL)
    {
      return NULL;
    }

  for (idx = class_stats->n_btids - 1; idx >= 0; idx--)
    {
      unique_stats = &class_stats->unique_stats[idx];
      if (BTID_IS_EQUAL (&unique_stats->btid, btid))
	{
	  return unique_stats;
	}
    }

  if (idx < 0)
    {
      unique_stats = NULL;
      if (create)
	{
	  unique_stats =
	    logtb_mvcc_create_btid_unique_stats (thread_p, class_stats, btid);
	}
    }

  return unique_stats;
}

/*
 * logtb_mvcc_find_class_oid_btid_stats () - searches the list of statistics for
 *					     statistics associated with
 *					     class_oid and btid.
 *
 * return	    : address of found (or newly created) statistics or null
 *		      otherwise
 * thread_p(in)	    :
 * class_oid(in)    : class_oid to be searched
 * btid (in)	    : B-tree id to be searched
 * create(in)	    : true if the caller needs a new entry to be created if not
 *		      found an already existing one
 */
LOG_MVCC_BTID_UNIQUE_STATS *
logtb_mvcc_find_class_oid_btid_stats (THREAD_ENTRY * thread_p,
				      OID * class_oid, BTID * btid,
				      bool create)
{
  LOG_MVCC_CLASS_UPDATE_STATS *class_stats =
    logtb_mvcc_find_class_stats (thread_p, class_oid, create);

  if (class_stats == NULL)
    {
      return NULL;
    }

  return logtb_mvcc_find_btid_stats (thread_p, class_stats, btid, create);
}

/*
 * logtb_mvcc_search_btid_stats_all_classes () - searches for a given B-tree id
 *						 in all existing classes
 *
 * return	    : address of found statistics or null otherwise
 * thread_p(in)	    :
 * btid (in)	    : B-tree id to be searched
 * create(in)	    : 
 */
LOG_MVCC_BTID_UNIQUE_STATS *
logtb_mvcc_search_btid_stats_all_classes (THREAD_ENTRY * thread_p,
					  const BTID * btid, bool create)
{
  LOG_TDES *tdes = NULL;
  LOG_MVCC_CLASS_UPDATE_STATS *entry = NULL;
  LOG_MVCC_BTID_UNIQUE_STATS *unique_stats = NULL;

  tdes = LOG_FIND_TDES (LOG_FIND_THREAD_TRAN_INDEX (thread_p));
  if (tdes == NULL || tdes->log_upd_stats.crt_tran_entries == NULL)
    {
      return NULL;
    }

  for (entry = tdes->log_upd_stats.crt_tran_entries; entry != NULL;
       entry = entry->next)
    {
      unique_stats =
	logtb_mvcc_find_btid_stats (thread_p, entry, btid, false);
      if (unique_stats != NULL)
	{
	  break;
	}
    }

  if (unique_stats == NULL && create)
    {
      VPID root_vpid;
      PAGE_PTR root = NULL;
      OID class_oid;
      BTREE_ROOT_HEADER *root_header = NULL;

      OID_SET_NULL (&class_oid);
      root_vpid.pageid = btid->root_pageid;
      root_vpid.volid = btid->vfid.volid;

      root = pgbuf_fix (thread_p, &root_vpid, OLD_PAGE, PGBUF_LATCH_READ,
			PGBUF_UNCONDITIONAL_LATCH);
      if (root == NULL)
	{
	  return NULL;
	}

      (void) pgbuf_check_page_ptype (thread_p, root, PAGE_BTREE);

      root_header = btree_get_root_header (root);
      if (root_header == NULL)
	{
	  pgbuf_unfix_and_init (thread_p, root);
	  return NULL;
	}

      if (OID_ISNULL (&class_oid))
	{
	  pgbuf_unfix_and_init (thread_p, root);
	  return NULL;
	}

      entry = logtb_mvcc_find_class_stats (thread_p, &class_oid, true);
      if (entry == NULL)
	{
	  pgbuf_unfix_and_init (thread_p, root);
	  return NULL;
	}

      pgbuf_unfix_and_init (thread_p, root);
      unique_stats = logtb_mvcc_find_btid_stats (thread_p, entry, btid, true);
    }

  return unique_stats;
}

/*
 * logtb_mvcc_update_btid_unique_stats () - updates statistics associated with
 *					    the given btid and class
 *
 * return	    : error code or NO_ERROR
 * thread_p(in)	    :
 * class_stats(in)  : entry for class to which the btid belongs
 * btid (in)	    : B-tree id to be searched
 * n_keys(in)	    : number of keys to be added to statistics
 * n_oids(in)	    : number of oids to be added to statistics
 * n_nulls(in)	    : number of nulls to be added to statistics
 *
 * Note: the statistics are searched and created if they not exist.
 */
int
logtb_mvcc_update_btid_unique_stats (THREAD_ENTRY * thread_p,
				     LOG_MVCC_CLASS_UPDATE_STATS *
				     class_stats, BTID * btid, int n_keys,
				     int n_oids, int n_nulls)
{
  LOG_MVCC_BTID_UNIQUE_STATS *unique_stats =
    logtb_mvcc_find_btid_stats (thread_p, class_stats, btid, true);
  LOG_TDES *tdes = LOG_FIND_TDES (LOG_FIND_THREAD_TRAN_INDEX (thread_p));

  if (unique_stats == NULL)
    {
      return ER_FAILED;
    }

  unique_stats->tran_stats.num_keys += n_keys;
  unique_stats->tran_stats.num_oids += n_oids;
  unique_stats->tran_stats.num_nulls += n_nulls;

  if (tdes->log_upd_stats.topop_id < 0)
    {
      tdes->log_upd_stats.topop_id = tdes->topops.last;
    }

  return NO_ERROR;
}

/*
 * logtb_mvcc_update_class_unique_stats () - updates statistics associated with
 *					     the given class and btid
 *
 * return	    : error code or NO_ERROR
 * thread_p(in)	    :
 * class_oid(in)    : class OID of class to which the btid belongs
 * btid (in)	    : B-tree id to be searched
 * n_keys(in)	    : number of keys to be added to statistics
 * n_oids(in)	    : number of oids to be added to statistics
 * n_nulls(in)	    : number of nulls to be added to statistics
 * write_to_log	    : if true then new statistics wil be written to log
 *
 * Note: the statistics are searched and created if they not exist.
 */
int
logtb_mvcc_update_class_unique_stats (THREAD_ENTRY * thread_p,
				      OID * class_oid, BTID * btid,
				      int n_keys, int n_oids, int n_nulls,
				      bool write_to_log)
{
  LOG_MVCC_CLASS_UPDATE_STATS *class_stats = NULL;
  int error = NO_ERROR;

  if (class_oid == NULL)
    {
      return ER_FAILED;
    }

  class_stats = logtb_mvcc_find_class_stats (thread_p, class_oid, true);
  if (class_stats == NULL)
    {
      return ER_FAILED;
    }

  error = logtb_mvcc_update_btid_unique_stats (thread_p, class_stats, btid,
					       n_keys, n_oids, n_nulls);

  if (write_to_log)
    {
      char undo_rec_buf[3 * OR_INT_SIZE + OR_OID_SIZE + OR_BTID_ALIGNED_SIZE +
			MAX_ALIGNMENT];
      char redo_rec_buf[3 * OR_INT_SIZE + OR_OID_SIZE + OR_BTID_ALIGNED_SIZE +
			MAX_ALIGNMENT];
      RECDES undo_rec, redo_rec;

      undo_rec.area_size = ((3 * OR_INT_SIZE) + OR_OID_SIZE
			    + OR_BTID_ALIGNED_SIZE);
      undo_rec.data = PTR_ALIGN (undo_rec_buf, MAX_ALIGNMENT);

      redo_rec.area_size = ((3 * OR_INT_SIZE) + OR_OID_SIZE
			    + OR_BTID_ALIGNED_SIZE);
      redo_rec.data = PTR_ALIGN (redo_rec_buf, MAX_ALIGNMENT);

      btree_rv_mvcc_save_increments (class_oid, btid, -n_keys, -n_oids,
				     -n_nulls, &undo_rec);
      btree_rv_mvcc_save_increments (class_oid, btid, n_keys, n_oids, n_nulls,
				     &redo_rec);

      log_append_undoredo_data2 (thread_p, RVBT_MVCC_INCREMENTS_UPD,
				 NULL, NULL, -1,
				 undo_rec.length, redo_rec.length,
				 undo_rec.data, redo_rec.data);
    }

  return error;
}

/*
 * logtb_mvcc_reflect_unique_statistics () - reflects in B-tree the statistics
 *					     accumulated during transaction 
 *
 * return	    : error code or NO_ERROR
 * thread_p(in)	    :
 * class_oid(in)    : class OID of class to which the btid belongs
 * btid (in)	    : B-tree id to be searched
 * n_keys(in)	    : number of keys to be added to statistics
 * n_oids(in)	    : number of oids to be added to statistics
 * n_nulls(in)	    : number of nulls to be added to statistics
 *
 * Note: the statistics are searched and created if they not exist.
 */
static int
logtb_mvcc_reflect_unique_statistics (THREAD_ENTRY * thread_p)
{
  LOG_TDES *tdes = LOG_FIND_TDES (LOG_FIND_THREAD_TRAN_INDEX (thread_p));
  int error_code = NO_ERROR, idx;
  LOG_MVCC_CLASS_UPDATE_STATS *entry = NULL;
  LOG_MVCC_BTID_UNIQUE_STATS *unique_stats = NULL;
  BTREE_UNIQUE_STATS btree_stats;

  if (tdes == NULL)
    {
      return ER_FAILED;
    }

  if (log_start_system_op (thread_p) == NULL)
    {
      return ER_FAILED;
    }

  for (entry = tdes->log_upd_stats.crt_tran_entries; entry != NULL;
       entry = entry->next)
    {
      if (heap_is_mvcc_disabled_for_class (&(entry->class_oid)))
	{
	  /* do not reflect statistics for non-MVCC classes
	   * since they are already reflected at insert/delete
	   */
	  continue;
	}

      for (idx = entry->n_btids - 1; idx >= 0; idx--)
	{
	  unique_stats = &entry->unique_stats[idx];
	  if (unique_stats->deleted)
	    {
	      continue;
	    }

	  BTID_COPY (&btree_stats.btid, &unique_stats->btid);

	  btree_stats.num_keys = unique_stats->tran_stats.num_keys;
	  btree_stats.num_nulls = unique_stats->tran_stats.num_nulls;
	  btree_stats.num_oids = unique_stats->tran_stats.num_oids;

	  error_code =
	    btree_reflect_unique_statistics (thread_p, &btree_stats, false);
	  if (error_code != NO_ERROR)
	    {
	      log_end_system_op (thread_p, LOG_RESULT_TOPOP_ABORT);
	      return error_code;
	    }
	}
    }

  log_end_system_op (thread_p, LOG_RESULT_TOPOP_COMMIT);

  return error_code;
}

/*
 * logtb_mvcc_load_global_statistics () - loads global statistics from B-tree
 *					  into memory for classes for which the
 *					  COS_TO_LOAD state is active
 *
 * return	    : error code or NO_ERROR
 * thread_p(in)	    :
 *
 * Note: if the statistics were successfully loaded then set state to COS_LOADED
 */
static int
logtb_mvcc_load_global_statistics (THREAD_ENTRY * thread_p)
{
  int error_code = NO_ERROR, idx, idx2;
  LOG_TDES *tdes = LOG_FIND_TDES (LOG_FIND_THREAD_TRAN_INDEX (thread_p));
  LOG_MVCC_CLASS_UPDATE_STATS *entry = NULL, *new_entry = NULL;
  LOG_MVCC_BTID_UNIQUE_STATS *unique_stats = NULL;
  PRUNING_CONTEXT context;

  if (tdes == NULL)
    {
      return ER_FAILED;
    }

  partition_init_pruning_context (&context);

  for (entry = tdes->log_upd_stats.crt_tran_entries; entry != NULL;
       entry = entry->next)
    {
      if (entry->count_state != COS_TO_LOAD)
	{
	  continue;
	}

      /* In case of partitioned class load statistics for each partition */
      error_code =
	partition_load_pruning_context (thread_p, &entry->class_oid,
					DB_PARTITIONED_CLASS, &context);
      if (error_code != NO_ERROR)
	{
	  goto cleanup;
	}

      if (context.count > 0)
	{
	  for (idx = 0; idx < context.count; idx++)
	    {
	      if (OID_ISNULL (&context.partitions[idx].class_oid))
		{
		  continue;
		}
	      new_entry =
		logtb_mvcc_find_class_stats (thread_p,
					     &context.partitions[idx].
					     class_oid, true);
	      if (new_entry == NULL)
		{
		  error_code = ER_FAILED;
		  goto cleanup;
		}

	      error_code =
		logtb_create_unique_stats_from_repr (thread_p, new_entry);
	      if (error_code != NO_ERROR)
		{
		  goto cleanup;
		}

	      for (idx2 = new_entry->n_btids - 1; idx2 >= 0; idx2--)
		{
		  unique_stats = &new_entry->unique_stats[idx2];
		  error_code =
		    btree_get_unique_statistics (thread_p,
						 &unique_stats->btid,
						 &unique_stats->global_stats.
						 num_oids,
						 &unique_stats->global_stats.
						 num_nulls,
						 &unique_stats->global_stats.
						 num_keys);
		  if (error_code != NO_ERROR)
		    {
		      goto cleanup;
		    }
		}

	      new_entry->count_state = COS_LOADED;
	    }
	}

      error_code = logtb_create_unique_stats_from_repr (thread_p, entry);
      if (error_code != NO_ERROR)
	{
	  goto cleanup;
	}

      for (idx = entry->n_btids - 1; idx >= 0; idx--)
	{
	  unique_stats = &entry->unique_stats[idx];
	  error_code =
	    btree_get_unique_statistics (thread_p, &unique_stats->btid,
					 &unique_stats->global_stats.num_oids,
					 &unique_stats->global_stats.
					 num_nulls,
					 &unique_stats->global_stats.
					 num_keys);
	  if (error_code != NO_ERROR)
	    {
	      goto cleanup;
	    }
	}

      entry->count_state = COS_LOADED;
    }

cleanup:
  partition_clear_pruning_context (&context);

  return error_code;
}

/*
 * logtb_mvcc_update_tran_class_stats () - Called at the end of a command,
 *					   should update the class sattistics
 *					   for the current running
 *					   transaction.
 *
 * return	       : Error code.
 * thread_p (in)       : Thread entry.
 * cancel_command (in) : True if command was canceled due to error. All
 *			 inserted records will not be physically removed,
 *			 but are also counted as deleted. All deleted records
 *			 are "revived" (n_deleted is ignored).
 */
int
logtb_mvcc_update_tran_class_stats (THREAD_ENTRY * thread_p,
				    bool cancel_command)
{
  LOG_TDES *tdes = LOG_FIND_TDES (LOG_FIND_THREAD_TRAN_INDEX (thread_p));
  int error = NO_ERROR, idx;
  LOG_MVCC_CLASS_UPDATE_STATS *class_stats = NULL;
  LOG_MVCC_BTID_UNIQUE_STATS *unique_stats = NULL;

  if (tdes == NULL)
    {
      assert (0);
      return ER_FAILED;
    }

  for (class_stats = tdes->log_upd_stats.crt_tran_entries;
       class_stats != NULL; class_stats = class_stats->next)
    {
      for (idx = class_stats->n_btids - 1; idx >= 0; idx--)
	{
	  unique_stats = &class_stats->unique_stats[idx];
	  if (cancel_command)
	    {
	      unique_stats->deleted = false;
	    }
	}
    }

  return NO_ERROR;
}

/*
 * logtb_get_mvcc_snapshot_data - Obtain a new snapshot for current
 *				  transaction descriptor.
 *
 * return	 : Error code.
 * thread_p (in) : Thread entry.
 *
 * Note:  Allow other transactions to get snapshots, do not allow to any
 *	      transaction with an MVCCID assigned to end while we build
 *	      snapshot. This will prevent inconsistencies like in the
 *	      following example:
 *		- T1 transaction gets new MVCCID (>= highest_completed_mvccid)
 *	      update row1->row2 and T2 transaction is blocked by T1 when
 *	      trying to update row2->row3
 *		- T1 is doing commit while T3 gets a snapshot
 *		- T2 commit and complete soon after T1 release locks
 *	      T3 sees T1 as active (T1 MVCCID >= highest_completed_mvccid)
 *	      and T2 committed. T3 sees two row versions : row1 deleted
 *	      by T1 and row3 inserted by T2 => Inconsistence.
 *	      By taking CSECT_TRAN_TABLE in read mode in
 *	      logtb_get_mvcc_snapshot_data and in exclusive mode in
 *	      logtb_complete_mvcc the inconsistency is avoided :
 *	      T1 and T2 will be seen as active.
 */
static int
logtb_get_mvcc_snapshot_data (THREAD_ENTRY * thread_p)
{
  MVCCID lowest_active_mvccid = 0, highest_completed_mvccid = 0;
  unsigned int cnt_active_trans = 0;
  LOG_TDES *curr_tdes = NULL;
  int tran_index, error_code = NO_ERROR;
  LOG_TDES *tdes;
  MVCCID curr_mvccid;
  MVCC_SNAPSHOT *snapshot = NULL;
  MVCC_INFO *elem = NULL, *curr_mvcc_info = NULL;
  MVCCTABLE *mvcc_table = &log_Gl.mvcc_table;
  MVCCID mvcc_sub_id;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);

  curr_mvcc_info = tdes->mvcc_info;

  assert (tdes != NULL && curr_mvcc_info != NULL);

  snapshot = &(curr_mvcc_info->mvcc_snapshot);
  curr_mvccid = curr_mvcc_info->mvcc_id;

  snapshot->snapshot_fnc = mvcc_satisfies_snapshot;
  if (snapshot->active_ids == NULL)
    {
      /* allocate only once */
      int size;
      size = NUM_TOTAL_TRAN_INDICES * OR_MVCCID_SIZE;

      snapshot->active_ids = malloc (size);
      if (snapshot->active_ids == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, size);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
    }

  /* Start building the snapshot. See the note. */
  csect_enter_as_reader (thread_p, CSECT_MVCC_ACTIVE_TRANS, INF_WAIT);

  highest_completed_mvccid = mvcc_table->highest_completed_mvccid;
  MVCCID_FORWARD (highest_completed_mvccid);
  lowest_active_mvccid = highest_completed_mvccid;

  /* record the active MVCC ids */

  elem = mvcc_table->tail_writers;
  /* heap_readers is the next node after tail writer or null */
  while (elem != NULL)
    {
      assert (MVCCID_IS_NORMAL (elem->mvcc_id));

      if (mvcc_id_follow_or_equal (elem->mvcc_id, highest_completed_mvccid))
	{
	  /* skip remaining MVCC ids since these transactions are considered
	   * as active anyway
	   */
	  break;
	}

      if (elem->mvcc_id < lowest_active_mvccid)
	{
	  lowest_active_mvccid = elem->mvcc_id;
	}

      /* do not add MVCC id of current transaction in snapshot */
      if (elem->mvcc_id != curr_mvccid)
	{
	  snapshot->active_ids[cnt_active_trans++] = elem->mvcc_id;
	  if (elem->count_sub_ids > 0 && elem->is_sub_active)
	    {
	      /* only the last sub-transaction may be active */
	      assert (elem->mvcc_sub_ids != NULL);

	      mvcc_sub_id = elem->mvcc_sub_ids[elem->count_sub_ids - 1];

	      if (!mvcc_id_follow_or_equal (mvcc_sub_id,
					    highest_completed_mvccid))
		{
		  snapshot->active_ids[cnt_active_trans++] = mvcc_sub_id;
		}
	    }
	}

      elem = elem->prev;
    }

  assert (cnt_active_trans <= (unsigned int) 2 * NUM_TOTAL_TRAN_INDICES);

  /* set the lowest active MVCC id when we start the current transaction
   * if was not already set to not null value
   */
  if (!MVCCID_IS_VALID (curr_mvcc_info->transaction_lowest_active_mvccid))
    {
      curr_mvcc_info->transaction_lowest_active_mvccid = lowest_active_mvccid;
    }

  /* The below code resided initially after the critical section but was moved
   * here because we need the snapshot in logtb_mvcc_load_global_statistics in
   * order to check that the class is partitioned or not not and if it is then
   * also load unique statistics for partitions. 
   */

  /* update lowest active mvccid computed for the most recent snapshot */
  curr_mvcc_info->recent_snapshot_lowest_active_mvccid = lowest_active_mvccid;

  /* update snapshot data */
  snapshot->lowest_active_mvccid = lowest_active_mvccid;
  snapshot->highest_completed_mvccid = highest_completed_mvccid;
  snapshot->cnt_active_ids = cnt_active_trans;
  snapshot->valid = true;

  /* load global statistics. This must take place here and no where else. */
  if (logtb_mvcc_load_global_statistics (thread_p) != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_MVCC_CANT_GET_SNAPSHOT, 0);
      error_code = ER_MVCC_CANT_GET_SNAPSHOT;
    }

  csect_exit (thread_p, CSECT_MVCC_ACTIVE_TRANS);

  return error_code;
}

/*
 * xlogtb_invalidate_snapshot_data () - Make sure MVCC is invalidated.
 *
 * return	 : Void.
 * thread_p (in) : Thread entry.
 */
int
xlogtb_invalidate_snapshot_data (THREAD_ENTRY * thread_p)
{
  /* Get transaction descriptor */
  LOG_TDES *tdes = LOG_FIND_TDES (LOG_FIND_THREAD_TRAN_INDEX (thread_p));

  if (!mvcc_Enabled || tdes == NULL || tdes->mvcc_info == NULL
      || tdes->isolation >= TRAN_REPEATABLE_READ)
    {
      /* Nothing to do */
      return NO_ERROR;
    }

  if (tdes->mvcc_info->mvcc_snapshot.valid)
    {
      /* Invalidate snapshot */
      tdes->mvcc_info->mvcc_snapshot.valid = false;
      logtb_mvcc_reset_count_optim_state (thread_p);
    }

  return NO_ERROR;
}

/*
 * logtb_get_lowest_active_mvccid () - Get oldest MVCCID that was running
 *				       when any active transaction started.
 *
 * return	 : MVCCID for oldest active transaction.
 * thread_p (in) : Thread entry.
 *
 * Note:  The returned MVCCID is used as threshold by vacuum. The rows deleted
 *	      by transaction having MVCCIDs >= logtb_get_lowest_active_mvccid
 *	      will not be physical removed by vacuum. That's because that rows
 *	      may still be visible to active transactions, even if deleting
 *	      transaction has commit meanwhile. If there is no active
 *	      transaction, this function return highest_completed_mvccid + 1.
 */
MVCCID
logtb_get_lowest_active_mvccid (THREAD_ENTRY * thread_p)
{
  MVCCID lowest_active_mvccid;
  LOG_TDES *tdes = NULL;
  MVCC_INFO *elem = NULL;
  MVCCTABLE *mvcc_table = &log_Gl.mvcc_table;

  csect_enter_as_reader (thread_p, CSECT_MVCC_ACTIVE_TRANS, INF_WAIT);

  /* init lowest_active_mvccid */
  lowest_active_mvccid = mvcc_table->highest_completed_mvccid;
  MVCCID_FORWARD (lowest_active_mvccid);

  elem = mvcc_table->head_writers;
  if (elem == NULL)
    {
      /* No writers, point to first reader */
      elem = mvcc_table->head_null_mvccids;
    }

  /* heap_readers is the next node after tail writer or null */
  while (elem != NULL)
    {
      /* check lowest_active_mvccid against mvccid and
       * transaction_lowest_active_mvccid since a transaction can have
       * only one of this fields set
       */
      if (MVCCID_IS_NORMAL (elem->mvcc_id)
	  && mvcc_id_precedes (elem->mvcc_id, lowest_active_mvccid))
	{
	  lowest_active_mvccid = elem->mvcc_id;
	}

      if (MVCCID_IS_NORMAL (elem->transaction_lowest_active_mvccid)
	  && mvcc_id_precedes (elem->transaction_lowest_active_mvccid,
			       lowest_active_mvccid))
	{
	  lowest_active_mvccid = elem->transaction_lowest_active_mvccid;
	}

      elem = elem->next;
    }

  csect_exit (thread_p, CSECT_MVCC_ACTIVE_TRANS);

  return lowest_active_mvccid;
}

/*
 * logtb_get_new_mvccid - MVCC get new id
 *
 * return: error code
 *
 *  thread_p(in):
 *  curr_mvcc_info(in/out): current MVCC info
 *
 *  Note: This function set mvcc_id field of curr_mvcc_info. By keeping mvccid
 *	    into curr_mvcc_info before releasing CSECT_MVCC_ACTIVE_TRANS,
 *	    we are sure that all MVCCIDs <= highest_completed_mvccid are
 *	    either present in the transaction table or not running anymore.
 *	    If the current transaction will set its MVCCID after releasing
 *	    CSECT_MVCC_ACTIVE_TRANS, other transaction can generate and commit
 *	    a later MVCCID (causing highest_completed_mvccid > current mvccid) 
 *	    before current transaction MVCCID to be stored in tran table.
 *	    This would break logtb_get_lowest_active_mvccid() that may
 *	    return a bigger value than the real one. Thus, this function
 *	    can consider that there is no active transaction and return
 *	    highest_completed_mvccid + 1, when, in fact, the only active
 *	    transaction MVCCID has not been stored yet in transaction table. 
 */
int
logtb_get_new_mvccid (THREAD_ENTRY * thread_p, MVCC_INFO * curr_mvcc_info)
{
  MVCCID mvcc_id;
  MVCC_INFO *elem = NULL, *head_null_mvccids = NULL;
  int error;
  MVCCTABLE *mvcc_table;

  assert (curr_mvcc_info != NULL && curr_mvcc_info->mvcc_id == MVCCID_NULL);

  mvcc_table = &log_Gl.mvcc_table;

  /* do not allow others to read/write MVCC info list */
  error = csect_enter (NULL, CSECT_MVCC_ACTIVE_TRANS, INF_WAIT);
  if (error != NO_ERROR)
    {
      return error;
    }

  head_null_mvccids = mvcc_table->head_null_mvccids;

  mvcc_id = log_Gl.hdr.mvcc_next_id;
  MVCCID_FORWARD (log_Gl.hdr.mvcc_next_id);
  curr_mvcc_info->mvcc_id = mvcc_id;

  /* remove current MVCC info from null mvccid list */
  if (curr_mvcc_info->next != NULL)
    {
      curr_mvcc_info->next->prev = curr_mvcc_info->prev;
    }

  if (curr_mvcc_info->prev != NULL)
    {
      curr_mvcc_info->prev->next = curr_mvcc_info->next;
    }

  if (curr_mvcc_info == head_null_mvccids)
    {
      mvcc_table->head_null_mvccids = curr_mvcc_info->next;
      head_null_mvccids = curr_mvcc_info->next;
    }

  /* move current MVCC info into writers list */
  elem = mvcc_table->head_writers;
  if (elem == NULL)
    {
      /* empty writer list */
      curr_mvcc_info->prev = NULL;
      curr_mvcc_info->next = head_null_mvccids;
      if (head_null_mvccids != NULL)
	{
	  head_null_mvccids->prev = curr_mvcc_info;
	}
      mvcc_table->head_writers = mvcc_table->tail_writers = curr_mvcc_info;
    }
  else
    {
      /* writers list is not null */
      while (elem != head_null_mvccids)
	{
	  if (mvcc_id > elem->mvcc_id)
	    {
	      break;
	    }
	  elem = elem->next;
	}

      if (elem == NULL)
	{
	  /* tail_writers is not null */
	  assert (mvcc_table->tail_writers != NULL);
	  /* head_null_mvccids list is null - insert at the end of the list */
	  curr_mvcc_info->next = NULL;
	  curr_mvcc_info->prev = mvcc_table->tail_writers;
	  mvcc_table->tail_writers->next = curr_mvcc_info;

	  mvcc_table->tail_writers = curr_mvcc_info;
	}
      else
	{
	  /* insert before elem */
	  curr_mvcc_info->prev = elem->prev;
	  curr_mvcc_info->next = elem;

	  if (elem->prev != NULL)
	    {
	      elem->prev->next = curr_mvcc_info;
	      if (elem == head_null_mvccids)
		{
		  /* insert before head_null_mvccids - update tail_writers */
		  mvcc_table->tail_writers = curr_mvcc_info;
		}
	    }
	  else
	    {
	      /* insert at head writer */
	      mvcc_table->head_writers = curr_mvcc_info;
	    }

	  elem->prev = curr_mvcc_info;
	}
    }

  csect_exit (thread_p, CSECT_MVCC_ACTIVE_TRANS);
  return NO_ERROR;
}

/*
 * logtb_find_current_mvccid - find current transaction MVCC id
 *
 * return: MVCCID
 *
 *   thread_p(in):
 */
MVCCID
logtb_find_current_mvccid (THREAD_ENTRY * thread_p)
{
  LOG_TDES *tdes;
  MVCCID mvcc_id = MVCCID_NULL;

  tdes = LOG_FIND_TDES (LOG_FIND_THREAD_TRAN_INDEX (thread_p));
  if (tdes != NULL && tdes->mvcc_info != NULL)
    {
      if (tdes->mvcc_info->count_sub_ids > 0
	  && tdes->mvcc_info->is_sub_active)
	{
	  assert (tdes->mvcc_info->mvcc_sub_ids != NULL);
	  mvcc_id = tdes->mvcc_info->mvcc_sub_ids[tdes->mvcc_info->
						  count_sub_ids - 1];
	}
      else
	{
	  mvcc_id = tdes->mvcc_info->mvcc_id;
	}
    }

  return mvcc_id;
}

/*
 * logtb_get_current_mvccid - return current transaction MVCC id. Assign
 *			      a new ID if not previously set.
 *
 * return: current MVCCID
 *
 *   thread_p(in):
 */
MVCCID
logtb_get_current_mvccid (THREAD_ENTRY * thread_p)
{
  LOG_TDES *tdes = LOG_FIND_TDES (LOG_FIND_THREAD_TRAN_INDEX (thread_p));
  MVCC_INFO *curr_mvcc_info = tdes->mvcc_info;

  assert (tdes != NULL && curr_mvcc_info != NULL);

  if (MVCCID_IS_VALID (curr_mvcc_info->mvcc_id) == false)
    {
      (void) logtb_get_new_mvccid (thread_p, curr_mvcc_info);
    }

  if (tdes->mvcc_info->count_sub_ids > 0 && tdes->mvcc_info->is_sub_active)
    {
      assert (tdes->mvcc_info->mvcc_sub_ids != NULL);
      return tdes->mvcc_info->mvcc_sub_ids[tdes->mvcc_info->count_sub_ids -
					   1];
    }

  return curr_mvcc_info->mvcc_id;
}

/*
 * logtb_is_current_mvccid - check whether given mvccid is current mvccid
 *
 * return: bool
 *
 *   thread_p(in): thred entry
 *   mvccid(in): MVCC id
 */
bool
logtb_is_current_mvccid (THREAD_ENTRY * thread_p, MVCCID mvccid)
{
  LOG_TDES *tdes = LOG_FIND_TDES (LOG_FIND_THREAD_TRAN_INDEX (thread_p));
  int i;

  assert (tdes != NULL && tdes->mvcc_info != NULL);

  if (tdes->mvcc_info != NULL && tdes->mvcc_info->mvcc_id == mvccid)
    {
      return true;
    }
  else if (tdes->mvcc_info->count_sub_ids > 0)
    {
      /* is the child of current transaction ? */
      assert (tdes->mvcc_info->mvcc_sub_ids != NULL);

      for (i = 0; i < tdes->mvcc_info->count_sub_ids; i++)
	{
	  if (tdes->mvcc_info->mvcc_sub_ids[i] == mvccid)
	    {
	      return true;
	    }
	}
    }

  return false;
}

/*
 * logtb_is_active_mvccid - check whether given mvccid is active
 *
 * return: bool
 *
 *   thread_p(in): thread entry
 *   mvccid(in): MVCC id
 */
bool
logtb_is_active_mvccid (THREAD_ENTRY * thread_p, MVCCID mvccid)
{
  LOG_TDES *tdes = LOG_FIND_TDES (LOG_FIND_THREAD_TRAN_INDEX (thread_p));
  MVCC_INFO *curr_mvcc_info = NULL, *elem = NULL;
  MVCCTABLE *mvcc_table = &log_Gl.mvcc_table;

  assert (tdes != NULL && tdes->mvcc_info != NULL && mvccid != MVCCID_NULL);

  curr_mvcc_info = tdes->mvcc_info;

  if (mvcc_id_precedes (mvccid,
			curr_mvcc_info->recent_snapshot_lowest_active_mvccid))
    {
      return false;
    }

  if (logtb_is_current_mvccid (thread_p, mvccid))
    {
      return true;
    }

  (void) csect_enter_as_reader (thread_p, CSECT_MVCC_ACTIVE_TRANS, INF_WAIT);

  if (mvcc_id_precedes (mvcc_table->highest_completed_mvccid, mvccid))
    {
      csect_exit (thread_p, CSECT_MVCC_ACTIVE_TRANS);
      return true;
    }

  elem = mvcc_table->tail_writers;

  /* search not null mvccids  */
  while (elem != NULL)
    {
      assert (MVCCID_IS_VALID (elem->mvcc_id));

      if (MVCCID_IS_EQUAL (mvccid, elem->mvcc_id))
	{
	  csect_exit (thread_p, CSECT_MVCC_ACTIVE_TRANS);
	  return true;
	}
      else if (elem->count_sub_ids > 0 && elem->is_sub_active)
	{
	  assert (elem->mvcc_sub_ids != NULL);
	  if (MVCCID_IS_EQUAL (mvccid, elem->mvcc_sub_ids
			       [elem->count_sub_ids - 1]))
	    {
	      csect_exit (thread_p, CSECT_MVCC_ACTIVE_TRANS);
	      return true;
	    }
	}

      elem = elem->prev;
    }

  csect_exit (thread_p, CSECT_MVCC_ACTIVE_TRANS);
  return false;
}

/*
 * logtb_get_mvcc_snapshot  - get MVCC snapshot
 *
 * return: MVCC snapshot
 *
 *   thread_p(in): thread entry
 */
MVCC_SNAPSHOT *
logtb_get_mvcc_snapshot (THREAD_ENTRY * thread_p)
{
  if (mvcc_Enabled == false)
    {
      /* null snapshot if MVCC is disabled */
      return NULL;
    }
  else
    {
      LOG_TDES *tdes = LOG_FIND_TDES (LOG_FIND_THREAD_TRAN_INDEX (thread_p));

      if (tdes->tran_index == LOG_SYSTEM_TRAN_INDEX
	  || thread_is_vacuum_worker (thread_p))
	{
	  /* System transactions do not have snapshots */
	  return NULL;
	}

      assert (tdes != NULL && tdes->mvcc_info != NULL);

      if (!tdes->mvcc_info->mvcc_snapshot.valid)
	{
	  if (logtb_get_mvcc_snapshot_data (thread_p) != NO_ERROR)
	    {
	      return NULL;
	    }
	}

      return &tdes->mvcc_info->mvcc_snapshot;
    }
}

/*
 * logtb_complete_mvcc () - Called at commit or rollback, completes MVCC info
 *			    for current transaction.
 *
 * return	  : Void. 
 * thread_p (in)  : Thread entry.
 * tdes (in)	  : Transaction descriptor.
 * committed (in) : True if transaction was committed false if it was aborted.
 */
void
logtb_complete_mvcc (THREAD_ENTRY * thread_p, LOG_TDES * tdes, bool committed)
{
  LOG_MVCC_CLASS_UPDATE_STATS *entry = NULL;
  MVCC_INFO *curr_mvcc_info = NULL, *head_null_mvccids = NULL;
  MVCCTABLE *mvcc_table = &log_Gl.mvcc_table;
  MVCC_SNAPSHOT *p_mvcc_snapshot = NULL;

  assert (tdes != NULL);

  assert (mvcc_Enabled == true);

  curr_mvcc_info = tdes->mvcc_info;
  if (curr_mvcc_info == NULL)
    {
      return;
    }

  if (MVCCID_IS_VALID (curr_mvcc_info->mvcc_id))
    {
      /* reflect accumulated statistics to B-trees
       * temporary reflected before acquiring CSECT_TRAN_TABLE critical section,
       * in order to not affect the performance
       * Don't hold CS here because it will generate a deadlock on latch
       * pages.
       */
      if (committed
	  && logtb_mvcc_reflect_unique_statistics (thread_p) != NO_ERROR)
	{
	  assert (false);
	}

      (void) csect_enter (NULL, CSECT_MVCC_ACTIVE_TRANS, INF_WAIT);

      head_null_mvccids = mvcc_table->head_null_mvccids;

      /* update highest completed mvccid */
      if (mvcc_id_precedes (mvcc_table->highest_completed_mvccid,
			    curr_mvcc_info->mvcc_id))
	{
	  mvcc_table->highest_completed_mvccid = curr_mvcc_info->mvcc_id;
	}

      curr_mvcc_info->mvcc_id = MVCCID_NULL;
      curr_mvcc_info->count_sub_ids = 0;
      curr_mvcc_info->transaction_lowest_active_mvccid = MVCCID_NULL;

      /* remove current MVCC info from writers */
      if (curr_mvcc_info->next != NULL)
	{
	  curr_mvcc_info->next->prev = curr_mvcc_info->prev;
	}

      if (curr_mvcc_info->prev != NULL)
	{
	  curr_mvcc_info->prev->next = curr_mvcc_info->next;
	}

      if (curr_mvcc_info == mvcc_table->head_writers)
	{
	  if (curr_mvcc_info == mvcc_table->tail_writers)
	    {
	      mvcc_table->head_writers = mvcc_table->tail_writers = NULL;
	    }
	  else
	    {
	      /* remove first element from list */
	      mvcc_table->head_writers = curr_mvcc_info->next;
	    }
	}
      else if (curr_mvcc_info == mvcc_table->tail_writers)
	{
	  /* remove the last element from list */
	  mvcc_table->tail_writers = curr_mvcc_info->prev;
	}

      /* add current MVCC info null mvccid list */
      if (head_null_mvccids == NULL)
	{
	  /* empty reader list */
	  curr_mvcc_info->prev = mvcc_table->tail_writers;
	  curr_mvcc_info->next = NULL;
	  if (mvcc_table->tail_writers != NULL)
	    {
	      mvcc_table->tail_writers->next = curr_mvcc_info;
	    }

	  mvcc_table->head_null_mvccids = curr_mvcc_info;
	}
      else
	{
	  /* non empty reader list - insert before head_null_mvccids */
	  curr_mvcc_info->prev = head_null_mvccids->prev;
	  curr_mvcc_info->next = head_null_mvccids;

	  if (head_null_mvccids->prev != NULL)
	    {
	      head_null_mvccids->prev->next = curr_mvcc_info;
	    }

	  head_null_mvccids->prev = curr_mvcc_info;
	  mvcc_table->head_null_mvccids = curr_mvcc_info;
	}

      csect_exit (thread_p, CSECT_MVCC_ACTIVE_TRANS);
    }
  else
    {
      (void) csect_enter (NULL, CSECT_MVCC_ACTIVE_TRANS, INF_WAIT);

      curr_mvcc_info->transaction_lowest_active_mvccid = MVCCID_NULL;

      csect_exit (thread_p, CSECT_MVCC_ACTIVE_TRANS);
    }

  curr_mvcc_info->recent_snapshot_lowest_active_mvccid = MVCCID_NULL;

  p_mvcc_snapshot = &(curr_mvcc_info->mvcc_snapshot);
  if (p_mvcc_snapshot->valid)
    {
      logtb_mvcc_reset_count_optim_state (thread_p);
    }

  MVCC_CLEAR_SNAPSHOT_DATA (p_mvcc_snapshot);

  logtb_mvcc_clear_update_stats (&tdes->log_upd_stats);
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * logtb_set_current_tran_index - set index of current transaction
 *
 * return: tran_index or NULL_TRAN_INDEX
 *
 *   tran_index(in): Transaction index to acquire by current execution
 *
 * Note: The current execution acquire the given index. If the given
 *              index is not register, it is not set, an NULL_TRAN_INDEX is
 *              set instead.
 */
int
logtb_set_current_tran_index (THREAD_ENTRY * thread_p, int tran_index)
{
  LOG_TDES *tdes;		/* Transaction descriptor   */
  int index;			/* The returned index value */

  tdes = LOG_FIND_TDES (tran_index);
  if (tdes != NULL && tdes->trid != NULL_TRANID)
    {
      index = tran_index;
    }
  else
    {
      index = NULL_TRAN_INDEX;
    }

  if (index == tran_index)
    {
      LOG_SET_CURRENT_TRAN_INDEX (thread_p, index);
    }

  return index;
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * logtb_set_loose_end_tdes -
 *
 * return:
 *
 *   tdes(in/out):
 *
 * Note:
 */
static void
logtb_set_loose_end_tdes (LOG_TDES * tdes)
{
  if (LOG_ISTRAN_CLIENT_LOOSE_ENDS (tdes))
    {
      tdes->isloose_end = true;
      log_Gl.trantable.num_client_loose_end_indices++;
#if !defined(NDEBUG)
      if (prm_get_bool_value (PRM_ID_LOG_TRACE_DEBUG))
	{
	  const char *str_tmp;
	  switch (tdes->state)
	    {
	    case TRAN_UNACTIVE_COMMITTED_WITH_CLIENT_USER_LOOSE_ENDS:
	      str_tmp = "COMMIT";
	      break;
	    case TRAN_UNACTIVE_ABORTED_WITH_CLIENT_USER_LOOSE_ENDS:
	      str_tmp = "ABORT";
	      break;
	    case TRAN_UNACTIVE_XTOPOPE_COMMITTED_WITH_CLIENT_USER_LOOSE_ENDS:
	      str_tmp = "TOPSYS COMMIT";
	      break;
	    default:
	      str_tmp = "TOPSYS ABORT";
	      break;
	    }
	  fprintf (stdout,
		   "\n*** Transaction = %d (index = %d) has loose"
		   " ends %s recovery actions.\n They will be fully recovered"
		   " when client user = %s restarts a client ***\n",
		   tdes->trid, tdes->tran_index, str_tmp,
		   tdes->client.db_user);
	  fflush (stdout);
	}
#endif
    }
  else if (LOG_ISTRAN_2PC_PREPARE (tdes))
    {
      tdes->isloose_end = true;
      log_Gl.trantable.num_prepared_loose_end_indices++;
#if !defined(NDEBUG)
      if (prm_get_bool_value (PRM_ID_LOG_TRACE_DEBUG))
	{
	  fprintf (stdout,
		   "\n*** Transaction = %d (index = %d) is"
		   " prepared to commit as gobal tran = %d\n"
		   "    The coordinator site (maybe the client user = %s)"
		   " needs to attach\n"
		   "    to this transaction and either commit or abort it."
		   " ***\n", tdes->trid, tdes->tran_index,
		   tdes->gtrid, tdes->client.db_user);
	  fflush (stdout);
	}
#endif
    }
  else if (LOG_ISTRAN_2PC_IN_SECOND_PHASE (tdes)
	   || tdes->state == TRAN_UNACTIVE_2PC_COLLECTING_PARTICIPANT_VOTES)
    {
      tdes->isloose_end = true;
      log_Gl.trantable.num_coord_loose_end_indices++;
#if !defined(NDEBUG)
      if (prm_get_bool_value (PRM_ID_LOG_TRACE_DEBUG))
	{
	  fprintf (stdout,
		   "\n*** Transaction = %d (index = %d) needs to"
		   " complete informing participants\n"
		   "    about its fate = %s and collect participant"
		   " acknowledgements.\n"
		   "    This transaction has been disassociated from"
		   " the client user = %s.\n"
		   "    The transaction will be completely finished by"
		   " the system ***\n", tdes->trid,
		   tdes->tran_index,
		   ((LOG_ISTRAN_COMMITTED (tdes)) ? "COMMIT" :
		    "ABORT"), tdes->client.db_user);
	  fflush (stdout);
	}
#endif
    }
}

/*
 * logtb_set_num_loose_end_trans - set the number of loose end transactions
 *
 * return: num loose ends
 *
 * Note: The number of loose ends transactions is set by searching the
 *              transaction table.
 */
int
logtb_set_num_loose_end_trans (THREAD_ENTRY * thread_p)
{
  int i;
  int r;
  LOG_TDES *tdes;		/* Transaction descriptor */

  TR_TABLE_CS_ENTER (thread_p);

  log_Gl.trantable.num_client_loose_end_indices = 0;
  log_Gl.trantable.num_coord_loose_end_indices = 0;
  log_Gl.trantable.num_prepared_loose_end_indices = 0;

  for (i = 0; i < NUM_TOTAL_TRAN_INDICES; i++)
    {
      if (i != LOG_SYSTEM_TRAN_INDEX)
	{
	  tdes = log_Gl.trantable.all_tdes[i];
	  if (tdes != NULL && tdes->trid != NULL_TRANID)
	    {
	      logtb_set_loose_end_tdes (tdes);
	    }
	}
    }

  r = (log_Gl.trantable.num_client_loose_end_indices
       + log_Gl.trantable.num_coord_loose_end_indices
       + log_Gl.trantable.num_prepared_loose_end_indices);

  TR_TABLE_CS_EXIT (thread_p);

  return r;
}

/*
 * log_find_unilaterally_largest_undo_lsa - find maximum lsa address to undo
 *
 * return:
 *
 * Note: Find the maximum log sequence address to undo during the undo
 *              crash recovery phase.
 */
LOG_LSA *
log_find_unilaterally_largest_undo_lsa (THREAD_ENTRY * thread_p)
{
  int i;
  LOG_TDES *tdes;		/* Transaction descriptor */
  LOG_LSA *max = NULL;		/* The maximum LSA value  */

  TR_TABLE_CS_ENTER_READ_MODE (thread_p);

  for (i = 0; i < NUM_TOTAL_TRAN_INDICES; i++)
    {
      if (i != LOG_SYSTEM_TRAN_INDEX)
	{
	  tdes = log_Gl.trantable.all_tdes[i];
	  if (tdes != NULL
	      && tdes->trid != NULL_TRANID
	      && (tdes->state == TRAN_UNACTIVE_UNILATERALLY_ABORTED
		  || tdes->state == TRAN_UNACTIVE_ABORTED)
	      && !LSA_ISNULL (&tdes->undo_nxlsa)
	      && (max == NULL || LSA_LT (max, &tdes->undo_nxlsa)))
	    {
	      max = &tdes->undo_nxlsa;
	    }
	}
    }

  TR_TABLE_CS_EXIT (thread_p);

  return max;
}

/*
 * logtb_find_smallest_lsa - smallest lsa address of all active transactions
 *
 * return:
 *
 *   lsa(in):
 *
 */
void
logtb_find_smallest_lsa (THREAD_ENTRY * thread_p, LOG_LSA * lsa)
{
  int i;
  LOG_TDES *tdes;		/* Transaction descriptor */
  LOG_LSA *min_lsa = NULL;	/* The smallest lsa value */

  LSA_SET_NULL (lsa);

  TR_TABLE_CS_ENTER_READ_MODE (thread_p);

  for (i = 0; i < NUM_TOTAL_TRAN_INDICES; i++)
    {
      if (i != LOG_SYSTEM_TRAN_INDEX)
	{
	  tdes = log_Gl.trantable.all_tdes[i];
	  if (tdes != NULL
	      && tdes->trid != NULL_TRANID
	      && !LSA_ISNULL (&tdes->head_lsa)
	      && (min_lsa == NULL || LSA_LT (&tdes->head_lsa, min_lsa)))
	    {
	      min_lsa = &tdes->head_lsa;
	    }
	}
    }

  if (min_lsa != NULL)
    {
      LSA_COPY (lsa, min_lsa);
    }

  TR_TABLE_CS_EXIT (thread_p);
}

/*
 * logtb_allocate_mvcc_info - allocate MVCC info
 *
 * return: error code
 *
 *   thread_p(in): thread entry 
 */
int
logtb_allocate_mvcc_info (THREAD_ENTRY * thread_p)
{
  int tran_index;
  int error;
  LOG_TDES *tdes;
  MVCC_INFO *curr_mvcc_info = NULL, *elem = NULL, *head_null_mvccids = NULL;
  MVCCTABLE *mvcc_table = &log_Gl.mvcc_table;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);

  assert (tdes != NULL);

  if (thread_is_vacuum_worker (thread_p))
    {
      /* MVCC info is not required */
      if (tdes->mvcc_info != NULL)
	{
	  /* TODO: Not sure if this can happen, must investigate */
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  logtb_release_mvcc_info (thread_p);
	}

      assert (tdes->mvcc_info == NULL);
      return NO_ERROR;
    }

  if (tdes->mvcc_info != NULL)
    {
      /* MVCC info already added */
      return NO_ERROR;
    }

#if defined(HAVE_ATOMIC_BUILTINS)
  while (!ATOMIC_CAS_32 (&mvcc_table->mvcc_info_free_list_lock, 0, 1))
    {
    }
#endif

  if (mvcc_table->free_list == NULL)
    {
      logtb_alloc_mvcc_info_block (thread_p);
    }

  curr_mvcc_info = mvcc_table->free_list;
  mvcc_table->free_list = curr_mvcc_info->next;

#if defined(HAVE_ATOMIC_BUILTINS)
  *(volatile int *) (&mvcc_table->mvcc_info_free_list_lock) = 0;
#endif

  /* no need to init MVCC info - already cleared */
  curr_mvcc_info->next = curr_mvcc_info->prev = NULL;

  error = csect_enter (NULL, CSECT_MVCC_ACTIVE_TRANS, INF_WAIT);
  if (error != NO_ERROR)
    {
      return error;
    }

  head_null_mvccids = mvcc_table->head_null_mvccids;

  /* add curr_mvcc_info on top of head_null_mvccids */
  if (head_null_mvccids == NULL)
    {
      /* empty reader list */
      curr_mvcc_info->prev = mvcc_table->tail_writers;
      curr_mvcc_info->next = NULL;
      if (mvcc_table->tail_writers != NULL)
	{
	  mvcc_table->tail_writers->next = curr_mvcc_info;
	}

      mvcc_table->head_null_mvccids = curr_mvcc_info;
    }
  else
    {
      /* non empty reader list - insert before head_null_mvccids */
      curr_mvcc_info->prev = head_null_mvccids->prev;
      curr_mvcc_info->next = head_null_mvccids;

      if (head_null_mvccids->prev != NULL)
	{
	  head_null_mvccids->prev->next = curr_mvcc_info;
	}

      head_null_mvccids->prev = curr_mvcc_info;
      mvcc_table->head_null_mvccids = curr_mvcc_info;
    }

  tdes->mvcc_info = curr_mvcc_info;
  tdes->mvcc_info->mvcc_id = MVCCID_NULL;
  tdes->mvcc_info->count_sub_ids = 0;
  tdes->mvcc_info->transaction_lowest_active_mvccid = MVCCID_NULL;
  tdes->mvcc_info->recent_snapshot_lowest_active_mvccid = MVCCID_NULL;

  csect_exit (thread_p, CSECT_MVCC_ACTIVE_TRANS);

  return NO_ERROR;
}

/*
 * logtb_release_mvcc_info - release MVCC info
 *
 * return: error code
 *
 *   thread_p(in): thread entry 
 *
 * Note: If transaction MVCC info is valid then it's mvcc_id must be null.
 *  This means that transaction has complete before this call
 */
int
logtb_release_mvcc_info (THREAD_ENTRY * thread_p)
{
  int tran_index;
  int error;
  LOG_TDES *tdes;
  MVCC_INFO *curr_mvcc_info = NULL, *elem = NULL;
  MVCC_SNAPSHOT *p_mvcc_snapshot = NULL;
  MVCCTABLE *mvcc_table = &log_Gl.mvcc_table;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);

  assert (tdes != NULL);

  /* MVCC info already released */
  if (tdes->mvcc_info == NULL)
    {
      /* nothing to do */
      return NO_ERROR;
    }

  assert (tdes->mvcc_info->mvcc_id == MVCCID_NULL);

  curr_mvcc_info = tdes->mvcc_info;

  error = csect_enter (NULL, CSECT_MVCC_ACTIVE_TRANS, INF_WAIT);
  if (error != NO_ERROR)
    {
      return error;
    }

  /* remove current MVCC info from readers */
  if (curr_mvcc_info->next != NULL)
    {
      curr_mvcc_info->next->prev = curr_mvcc_info->prev;
    }

  if (curr_mvcc_info->prev != NULL)
    {
      curr_mvcc_info->prev->next = curr_mvcc_info->next;
    }

  if (curr_mvcc_info == mvcc_table->head_null_mvccids)
    {
      mvcc_table->head_null_mvccids = curr_mvcc_info->next;
    }

  csect_exit (thread_p, CSECT_MVCC_ACTIVE_TRANS);

  /* clean MVCC info */
  p_mvcc_snapshot = &(curr_mvcc_info->mvcc_snapshot);
  if (p_mvcc_snapshot->valid)
    {
      logtb_mvcc_reset_count_optim_state (thread_p);
    }
  MVCC_CLEAR_SNAPSHOT_DATA (p_mvcc_snapshot);

  curr_mvcc_info->transaction_lowest_active_mvccid =
    curr_mvcc_info->recent_snapshot_lowest_active_mvccid =
    curr_mvcc_info->mvcc_id = MVCCID_NULL;
  tdes->mvcc_info->count_sub_ids = 0;

  curr_mvcc_info->prev = NULL;

  /* add curr_mvcc_info into free area using spin lock */
#if defined(HAVE_ATOMIC_BUILTINS)
  while (!ATOMIC_CAS_32 (&mvcc_table->mvcc_info_free_list_lock, 0, 1))
    {
    }
#endif

  curr_mvcc_info->next = mvcc_table->free_list;
  mvcc_table->free_list = curr_mvcc_info;

#if defined(HAVE_ATOMIC_BUILTINS)
  *(volatile int *) (&mvcc_table->mvcc_info_free_list_lock) = 0;
#endif

  tdes->mvcc_info = NULL;

  return NO_ERROR;
}

/*
 * logtb_alloc_mvcc_info_block - allocate an MVCC info block
 *
 * return: error code
 *
 *   thread_p(in): thread entry 
 */
int
logtb_alloc_mvcc_info_block (THREAD_ENTRY * thread_p)
{
  MVCC_INFO_BLOCK *new_mvcc_info_block = NULL;
  MVCC_INFO *curr_mvcc_info = NULL, *prev_mvcc_info = NULL;
  MVCC_SNAPSHOT *p_mvcc_snapshot = NULL;
  int i;
  MVCCTABLE *mvcc_table = &log_Gl.mvcc_table;

  new_mvcc_info_block = (MVCC_INFO_BLOCK *) malloc (sizeof (MVCC_INFO_BLOCK));
  if (new_mvcc_info_block == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      sizeof (MVCC_INFO_BLOCK));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  new_mvcc_info_block->next_block = NULL;
  new_mvcc_info_block->block = (MVCC_INFO *) malloc (sizeof (MVCC_INFO) *
						     NUM_TOTAL_TRAN_INDICES);
  if (new_mvcc_info_block->block == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      sizeof (sizeof (MVCC_INFO) * NUM_TOTAL_TRAN_INDICES));
      free_and_init (new_mvcc_info_block);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  /* initialize allocated MVCC info objects of the block */
  prev_mvcc_info = NULL;
  for (i = 0; i < NUM_TOTAL_TRAN_INDICES; i++)
    {
      curr_mvcc_info = new_mvcc_info_block->block + i;

      p_mvcc_snapshot = &(curr_mvcc_info->mvcc_snapshot);
      MVCC_SET_SNAPSHOT_DATA (p_mvcc_snapshot, NULL, MVCCID_NULL, MVCCID_NULL,
			      NULL, 0, false);

      curr_mvcc_info->transaction_lowest_active_mvccid =
	curr_mvcc_info->recent_snapshot_lowest_active_mvccid =
	curr_mvcc_info->mvcc_id = MVCCID_NULL;
      curr_mvcc_info->mvcc_sub_ids = NULL;
      curr_mvcc_info->count_sub_ids = curr_mvcc_info->max_sub_ids = 0;

      curr_mvcc_info->prev = prev_mvcc_info;
      curr_mvcc_info->next = new_mvcc_info_block->block + i + 1;
      prev_mvcc_info = curr_mvcc_info;
    }
  curr_mvcc_info->next = NULL;

  if (mvcc_table->block_list == NULL)
    {
      mvcc_table->block_list = new_mvcc_info_block;
    }
  else
    {
      new_mvcc_info_block->next_block = mvcc_table->block_list;
      mvcc_table->block_list = new_mvcc_info_block;
    }

  if (mvcc_table->free_list == NULL)
    {
      mvcc_table->free_list = new_mvcc_info_block->block;
    }
  else
    {
      curr_mvcc_info->next = mvcc_table->free_list;
      mvcc_table->free_list = new_mvcc_info_block->block;
    }

  return NO_ERROR;
}

/*
 * logtb_mvcc_prepare_count_optim_classes - prepare classes for count
 *					    optimization (for unique statistics
 *					    loading)
 *
 * return: error code
 *
 * thread_p(in): thread entry
 * classes(in): classes names list
 * flags(in): flags associated with class names
 * n_classes(in): number of classes names
 */
int
logtb_mvcc_prepare_count_optim_classes (THREAD_ENTRY * thread_p,
					const char **classes,
					LC_PREFETCH_FLAGS * flags,
					int n_classes)
{
  int idx;
  OID class_oid;
  LC_FIND_CLASSNAME find;
  LOG_MVCC_CLASS_UPDATE_STATS *class_stats = NULL;

  for (idx = n_classes - 1; idx >= 0; idx--)
    {
      if (!(flags[idx] & LC_PREF_FLAG_COUNT_OPTIM))
	{
	  continue;
	}

      /* get class OID from class name */
      find =
	xlocator_find_class_oid (thread_p, classes[idx], &class_oid,
				 NULL_LOCK);
      switch (find)
	{
	case LC_CLASSNAME_ERROR:
	  return ER_FAILED;

	case LC_CLASSNAME_EXIST:
	  if (OID_ISNULL (&class_oid))
	    {
	      /* The class OID could not be retrieved. Return error. */
	      return ER_FAILED;
	    }
	  else
	    {
	      /* search for class statistics (create if not exist). */
	      class_stats =
		logtb_mvcc_find_class_stats (thread_p, &class_oid, true);
	      if (class_stats == NULL)
		{
		  /* something wrong happened. Just return error */
		  return ER_FAILED;
		}

	      /* Mark class for unique statistics loading. The statistics will
	       * be loaded when snapshot will be taken 
	       */
	      if (class_stats->count_state != COS_LOADED)
		{
		  class_stats->count_state = COS_TO_LOAD;
		}
	    }
	  break;

	default:
	  break;
	}
    }

  return NO_ERROR;
}

/*
 * logtb_mvcc_reset_count_optim_state - reset count optimization state for all
 *					class statistics instances
 *
 * return:
 *
 * thread_p(in): thread entry
 */
void
logtb_mvcc_reset_count_optim_state (THREAD_ENTRY * thread_p)
{
  LOG_TDES *tdes = LOG_FIND_TDES (LOG_FIND_THREAD_TRAN_INDEX (thread_p));
  LOG_MVCC_CLASS_UPDATE_STATS *class_stats =
    tdes->log_upd_stats.crt_tran_entries;

  while (class_stats != NULL)
    {
      class_stats->count_state = COS_NOT_LOADED;

      class_stats = class_stats->next;
    }
}

/*
 * logtb_create_unique_stats_from_repr - create count optimization instances
 *					 for all unique indexes of the given
 *					 class
 *
 * return: error code
 *
 * thread_p(in)	  : thread entry 
 * class_stats(in): class statistics instance for which count optimization
 *		    unique statistics will be created.
 */
static int
logtb_create_unique_stats_from_repr (THREAD_ENTRY * thread_p,
				     LOG_MVCC_CLASS_UPDATE_STATS *
				     class_stats)
{
  OR_CLASSREP *classrepr = NULL;
  int error_code = NO_ERROR, idx, classrepr_cacheindex = -1;

  /* get class representation to find the total number of indexes */
  classrepr =
    heap_classrepr_get (thread_p, &class_stats->class_oid, NULL, 0,
			&classrepr_cacheindex, true);
  if (classrepr == NULL)
    {
      goto exit_on_error;
    }

  for (idx = classrepr->n_indexes - 1; idx >= 0; idx--)
    {
      if (btree_is_unique_type (classrepr->indexes[idx].type))
	{
	  if (logtb_mvcc_find_btid_stats (thread_p, class_stats,
					  &classrepr->indexes[idx].btid,
					  true) == NULL)
	    {
	      error_code = ER_FAILED;
	      goto exit_on_error;
	    }
	}
    }

  /* free class representation */
  error_code = heap_classrepr_free (classrepr, &classrepr_cacheindex);
  if (error_code != NO_ERROR)
    {
      goto exit_on_error;
    }

  return NO_ERROR;

exit_on_error:
  if (classrepr != NULL)
    {
      (void) heap_classrepr_free (classrepr, &classrepr_cacheindex);
    }

  return (error_code == NO_ERROR
	  && (error_code = er_errid ()) == NO_ERROR) ? ER_FAILED : error_code;
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * logtb_find_largest_lsa - largest lsa address of all active transactions
 *
 * return: LOG_LSA *
 *
 * Note: Find the largest LSA address of all active transactions.
 */
LOG_LSA *
logtb_find_largest_lsa (THREAD_ENTRY * thread_p)
{
  int i;
  LOG_TDES *tdes;		/* Transaction descriptor */
  LOG_LSA *max_lsa = NULL;	/* The largest lsa value  */

  TR_TABLE_CS_ENTER_READ_MODE (thread_p);
  for (i = 0; i < NUM_TOTAL_TRAN_INDICES; i++)
    {
      if (i != LOG_SYSTEM_TRAN_INDEX)
	{
	  tdes = log_Gl.trantable.all_tdes[i];
	  if (tdes != NULL
	      && tdes->trid != NULL_TRANID
	      && !LSA_ISNULL (&tdes->tail_lsa)
	      && (max_lsa == NULL || LSA_GT (&tdes->tail_lsa, max_lsa)))
	    {
	      max_lsa = &tdes->tail_lsa;
	    }
	}
    }
  TR_TABLE_CS_EXIT (thread_p);

  return max_lsa;
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * logtb_find_smallest_and_largest_active_pages - smallest and larger active pages
 *
 * return: nothing...
 *
 *   smallest(in/out): smallest active log page
 *   largest(in/out): largest active log page
 *
 * Note: Find the smallest and larger active pages.
 */
void
logtb_find_smallest_and_largest_active_pages (THREAD_ENTRY * thread_p,
					      LOG_PAGEID * smallest,
					      LOG_PAGEID * largest)
{
  int i;
  LOG_TDES *tdes;		/* Transaction descriptor */

  TR_TABLE_CS_ENTER_READ_MODE (thread_p);

  *smallest = *largest = NULL_PAGEID;

  for (i = 0; i < NUM_TOTAL_TRAN_INDICES; i++)
    {
      if (i != LOG_SYSTEM_TRAN_INDEX)
	{
	  tdes = log_Gl.trantable.all_tdes[i];
	  if (tdes != NULL && tdes->trid != NULL_TRANID
	      && !LSA_ISNULL (&tdes->head_lsa))
	    {
	      if (*smallest == NULL_PAGEID
		  || tdes->head_lsa.pageid < *smallest)
		{
		  *smallest = tdes->head_lsa.pageid;
		}
	      if (*largest == NULL_PAGEID || tdes->tail_lsa.pageid > *largest)
		{
		  *largest = tdes->tail_lsa.pageid;
		}
	      if (*largest == NULL_PAGEID
		  || tdes->posp_nxlsa.pageid > *largest)
		{
		  *largest = tdes->posp_nxlsa.pageid;
		}
	    }
	}
    }

  TR_TABLE_CS_EXIT (thread_p);
}

/*
 * logtb_has_deadlock_priority -  
 *
 * return: whether this transaction has deadlock priority
 */
bool
logtb_has_deadlock_priority (int tran_index)
{
  LOG_TDES *tdes;		/* Transaction descriptor */

  tdes = LOG_FIND_TDES (tran_index);
  if (tdes)
    {
      return tdes->has_deadlock_priority;
    }

  return false;
}

/*
 * logtb_get_new_subtransaction_mvccid - assign a new sub-transaction MVCCID
 *
 * return: error code
 *
 *   thread_p(in): Thread entry
 *   curr_mvcc_info(in): current MVCC info
 *
 *  Note: If transaction MVCCID is NULL then a new transaction MVCCID is
 *    allocated first.
 */
int
logtb_get_new_subtransaction_mvccid (THREAD_ENTRY * thread_p,
				     MVCC_INFO * curr_mvcc_info)
{
  MVCCID mvcc_id;
  MVCC_INFO *elem = NULL, *head_null_mvccids = NULL;
  int error;
  MVCCTABLE *mvcc_table;

  assert (curr_mvcc_info != NULL);

  /* allocate before acquiring CS */
  if (curr_mvcc_info->mvcc_sub_ids == NULL)
    {
      curr_mvcc_info->mvcc_sub_ids = (MVCCID *) malloc (OR_MVCCID_SIZE * 10);
      if (curr_mvcc_info->mvcc_sub_ids == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, sizeof (MVCCID) * 10);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
      curr_mvcc_info->count_sub_ids = 0;
      curr_mvcc_info->max_sub_ids = 10;
    }
  else if (curr_mvcc_info->count_sub_ids >= curr_mvcc_info->max_sub_ids)
    {
      curr_mvcc_info->mvcc_sub_ids =
	(MVCCID *) realloc (curr_mvcc_info->mvcc_sub_ids,
			    OR_MVCCID_SIZE
			    * (curr_mvcc_info->max_sub_ids + 10));
      if (curr_mvcc_info->mvcc_sub_ids == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, OR_MVCCID_SIZE * (curr_mvcc_info->max_sub_ids + 10));
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
      curr_mvcc_info->max_sub_ids += 10;
    }

  mvcc_table = &log_Gl.mvcc_table;

  /* do not allow others to read/write MVCC info list */
  error = csect_enter (NULL, CSECT_MVCC_ACTIVE_TRANS, INF_WAIT);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (!MVCCID_IS_VALID (curr_mvcc_info->mvcc_id))
    {
      /* if don't have MVCCID - assign an transaction MVCCID first and
       * then sub-transaction MVCCID       
       */

      head_null_mvccids = mvcc_table->head_null_mvccids;

      mvcc_id = log_Gl.hdr.mvcc_next_id;
      MVCCID_FORWARD (log_Gl.hdr.mvcc_next_id);
      curr_mvcc_info->mvcc_id = mvcc_id;

      /* remove current MVCC info from null mvccid list */
      if (curr_mvcc_info->next != NULL)
	{
	  curr_mvcc_info->next->prev = curr_mvcc_info->prev;
	}

      if (curr_mvcc_info->prev != NULL)
	{
	  curr_mvcc_info->prev->next = curr_mvcc_info->next;
	}

      if (curr_mvcc_info == head_null_mvccids)
	{
	  mvcc_table->head_null_mvccids = curr_mvcc_info->next;
	  head_null_mvccids = curr_mvcc_info->next;
	}

      /* move current MVCC info into writers list */
      elem = mvcc_table->head_writers;
      if (elem == NULL)
	{
	  /* empty writer list */
	  curr_mvcc_info->prev = NULL;
	  curr_mvcc_info->next = head_null_mvccids;
	  if (head_null_mvccids != NULL)
	    {
	      head_null_mvccids->prev = curr_mvcc_info;
	    }
	  mvcc_table->head_writers = mvcc_table->tail_writers =
	    curr_mvcc_info;
	}
      else
	{
	  /* writers list is not null */
	  while (elem != head_null_mvccids)
	    {
	      if (mvcc_id > elem->mvcc_id)
		{
		  break;
		}
	      elem = elem->next;
	    }

	  if (elem == NULL)
	    {
	      /* tail_writers is not null */
	      assert (mvcc_table->tail_writers != NULL);
	      /* head_null_mvccids list is null - insert at the end of the list */
	      curr_mvcc_info->next = NULL;
	      curr_mvcc_info->prev = mvcc_table->tail_writers;
	      mvcc_table->tail_writers->next = curr_mvcc_info;

	      mvcc_table->tail_writers = curr_mvcc_info;
	    }
	  else
	    {
	      /* insert before elem */
	      curr_mvcc_info->prev = elem->prev;
	      curr_mvcc_info->next = elem;

	      if (elem->prev != NULL)
		{
		  elem->prev->next = curr_mvcc_info;
		  if (elem == head_null_mvccids)
		    {
		      /* insert before head_null_mvccids - update tail_writers */
		      mvcc_table->tail_writers = curr_mvcc_info;
		    }
		}
	      else
		{
		  /* insert at head writer */
		  mvcc_table->head_writers = curr_mvcc_info;
		}

	      elem->prev = curr_mvcc_info;
	    }
	}
    }

  mvcc_id = log_Gl.hdr.mvcc_next_id;
  MVCCID_FORWARD (log_Gl.hdr.mvcc_next_id);
  curr_mvcc_info->mvcc_sub_ids[curr_mvcc_info->count_sub_ids] = mvcc_id;
  curr_mvcc_info->count_sub_ids++;
  curr_mvcc_info->is_sub_active = true;

  csect_exit (thread_p, CSECT_MVCC_ACTIVE_TRANS);

  return NO_ERROR;
}

/*
 * logtb_complete_sub_mvcc () - Called at end of sub-transaction 
 *
 * return	  : Void. 
 * thread_p (in)  : Thread entry.
 * tdes (in)	  : Transaction descriptor.
 */
void
logtb_complete_sub_mvcc (THREAD_ENTRY * thread_p, LOG_TDES * tdes)
{
  MVCC_INFO *curr_mvcc_info = NULL;
  MVCCID mvcc_sub_id;
  MVCCTABLE *mvcc_table = &log_Gl.mvcc_table;

  assert (mvcc_Enabled == true && tdes != NULL);

  curr_mvcc_info = tdes->mvcc_info;
  if (curr_mvcc_info == NULL)
    {
      return;
    }

  (void) csect_enter (NULL, CSECT_MVCC_ACTIVE_TRANS, INF_WAIT);

  curr_mvcc_info->is_sub_active = false;
  mvcc_sub_id =
    curr_mvcc_info->mvcc_sub_ids[curr_mvcc_info->count_sub_ids - 1];
  if (mvcc_id_precedes (mvcc_table->highest_completed_mvccid, mvcc_sub_id))
    {
      mvcc_table->highest_completed_mvccid = mvcc_sub_id;
    }

  csect_exit (thread_p, CSECT_MVCC_ACTIVE_TRANS);
}
