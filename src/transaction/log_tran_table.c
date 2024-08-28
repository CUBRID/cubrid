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
 * log_tran_table.c -
 */

#ident "$Id$"


#if !defined(WINDOWS)
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#endif

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

#include "dbtran_def.h"
#include "log_impl.h"
#include "log_lsa.hpp"
#include "log_manager.h"
#include "log_system_tran.hpp"
#include "memory_private_allocator.hpp"
#include "object_representation.h"
#include "error_manager.h"
#include "system_parameter.h"
#include "xserver_interface.h"
#include "file_manager.h"
#include "query_manager.h"
#include "query_monitoring.hpp"
#include "partition_sr.h"
#include "btree_load.h"
#include "serial.h"
#include "show_scan.h"
#include "boot_sr.h"
#include "tz_support.h"
#include "db_date.h"
#include "dbtype.h"
#if defined (SERVER_MODE)
#include "server_support.h"
#endif // SERVER_MODE
#include "string_buffer.hpp"
#if defined (SA_MODE)
#include "transaction_cl.h"	/* for interrupt */
#endif /* defined (SA_MODE) */
#include "thread_entry.hpp"
#include "thread_manager.hpp"
#include "xasl.h"
#include "xasl_cache.h"
#include "method_runtime_context.hpp"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#define RMUTEX_NAME_TDES_TOPOP "TDES_TOPOP"

#define NUM_ASSIGNED_TRAN_INDICES log_Gl.trantable.num_assigned_indices
#define NUM_TOTAL_TRAN_INDICES log_Gl.trantable.num_total_indices

#if !defined(SERVER_MODE)
#define pthread_mutex_init(a, b)
#define pthread_mutex_destroy(a)
#define pthread_mutex_lock(a)   0
#define pthread_mutex_trylock(a)   0
#define pthread_mutex_unlock(a)
#endif /* not SERVER_MODE */

static const int LOG_MAX_NUM_CONTIGUOUS_TDES = INT_MAX / sizeof (LOG_TDES);
static const float LOG_EXPAND_TRANTABLE_RATIO = 1.25;	/* Increase table by 25% */
static const int LOG_TOPOPS_STACK_INCREMENT = 3;	/* No more than 3 nested top system operations */
static BOOT_CLIENT_CREDENTIAL log_Client_credential;

static const unsigned int LOGTB_RETRY_SLAM_MAX_TIMES = 10;

static int logtb_expand_trantable (THREAD_ENTRY * thread_p, int num_new_indices);
static int logtb_allocate_tran_index (THREAD_ENTRY * thread_p, TRANID trid, TRAN_STATE state,
				      const BOOT_CLIENT_CREDENTIAL * client_credential, TRAN_STATE * current_state,
				      int wait_msecs, TRAN_ISOLATION isolation);
static LOG_ADDR_TDESAREA *logtb_allocate_tdes_area (int num_indices);
static void logtb_initialize_trantable (TRANTABLE * trantable_p);
static int logtb_initialize_system_tdes (THREAD_ENTRY * thread_p);
static void logtb_set_number_of_assigned_tran_indices (int num_trans);
static void logtb_increment_number_of_assigned_tran_indices ();
static void logtb_decrement_number_of_assigned_tran_indices ();
static void logtb_set_number_of_total_tran_indices (int num_total_trans);
static void logtb_set_loose_end_tdes (LOG_TDES * tdes);
static bool logtb_is_interrupted_tdes (THREAD_ENTRY * thread_p, LOG_TDES * tdes, bool clear, bool * continue_checking);
static void logtb_dump_tdes_distribute_transaction (FILE * out_fp, int global_tran_id, LOG_2PC_COORDINATOR * coord);
static void logtb_dump_top_operations (FILE * out_fp, LOG_TOPOPS_STACK * topops_p);
static void logtb_dump_tdes (FILE * out_fp, LOG_TDES * tdes);
static void logtb_set_tdes (THREAD_ENTRY * thread_p, LOG_TDES * tdes, const BOOT_CLIENT_CREDENTIAL * client_credential,
			    int wait_msecs, TRAN_ISOLATION isolation);

static void logtb_tran_free_update_stats (LOG_TRAN_UPDATE_STATS * log_upd_stats);
static void logtb_tran_clear_update_stats (LOG_TRAN_UPDATE_STATS * log_upd_stats);
static unsigned int logtb_tran_btid_hash_func (const void *key, const unsigned int ht_size);
static int logtb_tran_btid_hash_cmp_func (const void *key1, const void *key2);
static LOG_TRAN_CLASS_COS *logtb_tran_create_class_cos (THREAD_ENTRY * thread_p, const OID * class_oid);
static LOG_TRAN_BTID_UNIQUE_STATS *logtb_tran_create_btid_unique_stats (THREAD_ENTRY * thread_p, const BTID * btid);
static int logtb_tran_update_delta_hash_func (THREAD_ENTRY * thread_p, void *data, void *args);
static int logtb_tran_load_global_stats_func (THREAD_ENTRY * thread_p, void *data, void *args);
static int logtb_tran_reset_cos_func (THREAD_ENTRY * thread_p, void *data, void *args);
static int logtb_create_unique_stats_from_repr (THREAD_ENTRY * thread_p, OID * class_oid);
static GLOBAL_UNIQUE_STATS *logtb_get_global_unique_stats_entry (THREAD_ENTRY * thread_p, BTID * btid,
								 bool load_at_creation);
static void *logtb_global_unique_stat_alloc (void);
static int logtb_global_unique_stat_free (void *unique_stat);
static int logtb_global_unique_stat_init (void *unique_stat);
static int logtb_global_unique_stat_key_copy (void *src, void *dest);
static void logtb_free_tran_mvcc_info (LOG_TDES * tdes);

static void logtb_assign_subtransaction_mvccid (THREAD_ENTRY * thread_p, MVCC_INFO * curr_mvcc_info, MVCCID mvcc_subid);

static int logtb_check_kill_tran_auth (THREAD_ENTRY * thread_p, int tran_id, bool * has_authorization);
static void logtb_find_thread_entry_mapfunc (THREAD_ENTRY & thread_ref, bool & stop_mapper, int tran_index,
					     bool except_me, REFPTR (THREAD_ENTRY, found_ptr));

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

  newptr = (LOG_TOPOPS_ADDRESSES *) realloc (tdes->topops.stack, size);
  if (newptr != NULL)
    {
      tdes->topops.stack = (LOG_TOPOPS_ADDRESSES *) newptr;
      if (tdes->topops.max == 0)
	{
	  tdes->topops.last = -1;
	}
      tdes->topops.max += num_elms;
    }
  else
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, size);
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
  LOG_ADDR_TDESAREA *area;	/* Contiguous area for new transaction indices */
  LOG_TDES *tdes;		/* Transaction descriptor */
  int i, tran_index;

  /*
   * Allocate an area for the transaction descriptors, set the address of
   * each transaction descriptor, and keep the address of the area for
   * deallocation purposes at shutdown time.
   */
  area = (LOG_ADDR_TDESAREA *) malloc (sizeof (LOG_ADDR_TDESAREA));
  if (area == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (LOG_ADDR_TDESAREA));
      return NULL;
    }

  area->tdesarea = new LOG_TDES[num_indices];
  if (area->tdesarea == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, num_indices * sizeof (LOG_TDES));
      return NULL;
    }
  area->next = log_Gl.trantable.area;

  /*
   * Initialize every newly created transaction descriptor index
   */
  for (i = 0, tran_index = NUM_TOTAL_TRAN_INDICES; i < num_indices; tran_index++, i++)
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
  if (log_Gl.rcv_phase == LOG_RESTARTED && total_indices <= NUM_TOTAL_TRAN_INDICES)
    {
      return NO_ERROR;
    }
#endif /* SERVER_MODE */

  while (num_new_indices > LOG_MAX_NUM_CONTIGUOUS_TDES)
    {
      error_code = logtb_expand_trantable (thread_p, LOG_MAX_NUM_CONTIGUOUS_TDES);
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
  log_Gl.trantable.all_tdes = (LOG_TDES **) realloc (log_Gl.trantable.all_tdes, i);
  if (log_Gl.trantable.all_tdes == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) i);
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
      /* *INDENT-OFF* */
      delete [] area->tdesarea;
      /* *INDENT-ON* */
      free_and_init (area);
      goto error;
    }
#endif

  if (qmgr_allocate_tran_entries (thread_p, total_indices) != NO_ERROR)
    {
      /* *INDENT-OFF* */
      delete [] area->tdesarea;
      /* *INDENT-ON* */
      free_and_init (area);
      error_code = ER_FAILED;
      goto error;
    }

  log_Gl.trantable.area = area;
  log_Gl.trantable.hint_free_index = NUM_TOTAL_TRAN_INDICES;
  logtb_set_number_of_total_tran_indices (total_indices);

  // make sure MVCC table resizes if necessary
  log_Gl.mvcc_table.alloc_transaction_lowest_active ();

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
logtb_define_trantable (THREAD_ENTRY * thread_p, int num_expected_tran_indices, int num_expected_locks)
{
  LOG_SET_CURRENT_TRAN_INDEX (thread_p, LOG_SYSTEM_TRAN_INDEX);

  LOG_CS_ENTER (thread_p);
  TR_TABLE_CS_ENTER (thread_p);

  if (logpb_is_pool_initialized ())
    {
      logpb_finalize_pool (thread_p);
    }

  (void) logtb_define_trantable_log_latch (thread_p, num_expected_tran_indices);

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
logtb_define_trantable_log_latch (THREAD_ENTRY * thread_p, int num_expected_tran_indices)
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

  num_expected_tran_indices = MAX (num_expected_tran_indices, LOG_SYSTEM_TRAN_INDEX + 1);

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
       * of indices. Probably, a lot of indices were requested.
       * try again with defaults.
       */
      if (log_Gl.trantable.area != NULL)
	{
	  logtb_undefine_trantable (thread_p);
	}

#if defined(SERVER_MODE)
      if (num_expected_tran_indices <= LOG_ESTIMATE_NACTIVE_TRANS || log_Gl.rcv_phase == LOG_RESTARTED)
#else /* SERVER_MODE */
      if (num_expected_tran_indices <= LOG_ESTIMATE_NACTIVE_TRANS)
#endif /* SERVER_MODE */
	{
	  /* Out of memory */
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_def_trantable");
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
      else
	{
	  error_code = logtb_define_trantable_log_latch (thread_p, LOG_ESTIMATE_NACTIVE_TRANS);
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
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_UNKNOWN_TRANINDEX, 1, LOG_SYSTEM_TRAN_INDEX);
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_def_trantable");
      return error_code;
    }

  LOG_SET_CURRENT_TRAN_INDEX (thread_p, LOG_SYSTEM_TRAN_INDEX);

  log_Gl.mvcc_table.initialize ();

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
  error_code = file_manager_init ();
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
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_UNKNOWN_TRANINDEX, 1, LOG_SYSTEM_TRAN_INDEX);
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_initialize_system_tdes");
      return ER_LOG_UNKNOWN_TRANINDEX;
    }

  logtb_clear_tdes (thread_p, tdes);
  tdes->tran_index = LOG_SYSTEM_TRAN_INDEX;
  tdes->trid = LOG_SYSTEM_TRANID;
  tdes->mvccinfo.reset ();
  tdes->isloose_end = true;
  tdes->wait_msecs = TRAN_LOCK_INFINITE_WAIT;
  tdes->isolation = TRAN_DEFAULT_ISOLATION_LEVEL ();
  tdes->client_id = -1;
  tdes->client.set_system_internal ();
  tdes->query_timeout = 0;
  tdes->tran_abort_reason = TRAN_NORMAL;
  tdes->block_global_oldest_active_until_commit = false;

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
  LOG_ADDR_TDESAREA *area;
  LOG_TDES *tdes;		/* Transaction descriptor */
  int i;

  log_Gl.mvcc_table.finalize ();
  lock_finalize ();
  pgbuf_finalize ();
  file_manager_final ();

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
#endif

	      logtb_finalize_tdes (thread_p, tdes);
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
	  /* *INDENT-OFF* */
	  delete[] area->tdesarea;
	  /* *INDENT-ON* */
	  free_and_init (area);
	  area = log_Gl.trantable.area;
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
  /* Do not use TR_TABLE_CS_ENTER()/TR_TABLE_CS_EXIT(), Estimated value is sufficient for the caller */
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
logtb_assign_tran_index (THREAD_ENTRY * thread_p, TRANID trid, TRAN_STATE state,
			 const BOOT_CLIENT_CREDENTIAL * client_credential, TRAN_STATE * current_state, int wait_msecs,
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
  tran_index =
    logtb_allocate_tran_index (thread_p, trid, state, client_credential, current_state, wait_msecs, isolation);
  TR_TABLE_CS_EXIT (thread_p);

  if (tran_index != NULL_TRAN_INDEX)
    {
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
logtb_set_tdes (THREAD_ENTRY * thread_p, LOG_TDES * tdes, const BOOT_CLIENT_CREDENTIAL * client_credential,
		int wait_msecs, TRAN_ISOLATION isolation)
{
#if defined(SERVER_MODE)
  CSS_CONN_ENTRY *conn;
#endif /* SERVER_MODE */

  if (client_credential == NULL)
    {
      client_credential = &log_Client_credential;
    }
  tdes->client.set_ids (*client_credential);
  tdes->is_user_active = false;
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
  tdes->m_modified_classes.clear ();
  tdes->num_transient_classnames = 0;
  tdes->first_save_entry = NULL;
  tdes->lob_locator_root.init ();
  tdes->m_log_postpone_cache.reset ();
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
logtb_allocate_tran_index (THREAD_ENTRY * thread_p, TRANID trid, TRAN_STATE state,
			   const BOOT_CLIENT_CREDENTIAL * client_credential, TRAN_STATE * current_state, int wait_msecs,
			   TRAN_ISOLATION isolation)
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

  /* Is there any free index ? */
  if (NUM_ASSIGNED_TRAN_INDICES >= NUM_TOTAL_TRAN_INDICES)
    {
#if defined(SERVER_MODE)
      /* When normal processing, we never expand trantable */
      if (log_Gl.rcv_phase == LOG_RESTARTED && NUM_TOTAL_TRAN_INDICES > 0)
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_TM_TOO_MANY_CLIENTS, 1, NUM_TOTAL_TRAN_INDICES - 1);
	  return NULL_TRAN_INDEX;
	}
#endif /* SERVER_MODE */

      i = (int) (((float) NUM_TOTAL_TRAN_INDICES * LOG_EXPAND_TRANTABLE_RATIO) + 0.5);
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
       tran_index == NULL_TRAN_INDEX && visited_loop_start_pos < 2; i = (i + 1) % NUM_TOTAL_TRAN_INDICES)
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
      log_Gl.trantable.hint_free_index = (tran_index + 1) % NUM_TOTAL_TRAN_INDICES;

      logtb_increment_number_of_assigned_tran_indices ();

      tdes = LOG_FIND_TDES (tran_index);
      if (tdes == NULL)
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
	  return NULL_TRAN_INDEX;
	}

      tdes->tran_index = tran_index;
      logtb_clear_tdes (thread_p, tdes);
      logtb_set_tdes (thread_p, tdes, client_credential, wait_msecs, isolation);

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
logtb_rv_find_allocate_tran_index (THREAD_ENTRY * thread_p, TRANID trid, const LOG_LSA * log_lsa)
{
  LOG_TDES *tdes;		/* Transaction descriptor */
  int tran_index;

  assert (trid != NULL_TRANID);

  if (logtb_is_system_worker_tranid (trid))
    {
      // *INDENT-OFF*
      return log_system_tdes::rv_get_or_alloc_tdes (trid, *log_lsa);
      // *INDENT-ON*
    }

  /*
   * If this is the first time, the transaction is seen. Assign a new
   * index to describe it and assume that the transaction was active
   * at the time of the crash, and thus it will be unilaterally aborted
   */
  tran_index = logtb_find_tran_index (thread_p, trid);
  if (tran_index == NULL_TRAN_INDEX)
    {
      /* Define the index */
      tran_index =
	logtb_allocate_tran_index (thread_p, trid, TRAN_UNACTIVE_UNILATERALLY_ABORTED, NULL, NULL,
				   TRAN_LOCK_INFINITE_WAIT, TRAN_SERIALIZABLE);
      tdes = LOG_FIND_TDES (tran_index);
      if (tran_index == NULL_TRAN_INDEX || tdes == NULL)
	{
	  /*
	   * Unable to assign a transaction index. The recovery process
	   * cannot continue
	   */
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_recovery_find_or_alloc");
	  return NULL;
	}
      else
	{
	  LSA_COPY (&tdes->head_lsa, log_lsa);
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
logtb_rv_assign_mvccid_for_undo_recovery (THREAD_ENTRY * thread_p, MVCCID mvccid)
{
  LOG_TDES *tdes = LOG_FIND_CURRENT_TDES (thread_p);

  assert (tdes != NULL);
  assert (MVCCID_IS_VALID (mvccid));

  tdes->mvccinfo.id = mvccid;
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
      tdes->mvccinfo.reset ();
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
	      tdes->client.reset ();
	      tdes->is_user_active = false;
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
  if (tran_index > NUM_TOTAL_TRAN_INDICES || tdes == NULL || tdes->trid == NULL_TRANID)
    {
#if defined(CUBRID_DEBUG)
      er_log_debug (ARG_FILE_LINE, "log_free_tran_index: Unknown index = %d. Operation is ignored", tran_index);
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
logtb_free_tran_index_with_undo_lsa (THREAD_ENTRY * thread_p, const LOG_LSA * undo_lsa)
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
	      if (tdes != NULL && tdes->trid != NULL_TRANID && tdes->state == TRAN_UNACTIVE_UNILATERALLY_ABORTED
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
  fprintf (out_fp,
	   "Tran_index = %2d, Trid = %d,\n    State = %s,\n    Isolation = %s,\n"
	   "    Wait_msecs = %d, isloose_end = %d,\n    Head_lsa = %lld|%d, Tail_lsa = %lld|%d,"
	   " Postpone_lsa = %lld|%d,\n    SaveLSA = %lld|%d, UndoNextLSA = %lld|%d,\n"
	   "    Client_User: (Type = %d, User = %s, Program = %s, Login = %s, Host = %s, Pid = %d)\n",
	   tdes->tran_index, tdes->trid, log_state_string (tdes->state), log_isolation_string (tdes->isolation),
	   tdes->wait_msecs, tdes->isloose_end, (long long int) tdes->head_lsa.pageid, (int) tdes->head_lsa.offset,
	   (long long int) tdes->tail_lsa.pageid, (int) tdes->tail_lsa.offset, (long long int) tdes->posp_nxlsa.pageid,
	   (int) tdes->posp_nxlsa.offset, (long long int) tdes->savept_lsa.pageid, (int) tdes->savept_lsa.offset,
	   (long long int) tdes->undo_nxlsa.pageid, (int) tdes->undo_nxlsa.offset, tdes->client.client_type,
	   tdes->client.get_db_user (), tdes->client.get_program_name (), tdes->client.get_login_name (),
	   tdes->client.get_host_name (), tdes->client.process_id);

  if (tdes->topops.max != 0 && tdes->topops.last >= 0)
    {
      logtb_dump_top_operations (out_fp, &tdes->topops);
    }

  if (tdes->gtrid != LOG_2PC_NULL_GTRID || tdes->coord != NULL)
    {
      logtb_dump_tdes_distribute_transaction (out_fp, tdes->gtrid, tdes->coord);
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
      fprintf (out_fp, " Head = %lld|%d, Posp_Head = %lld|%d\n",
	       LSA_AS_ARGS (&topops_p->stack[i].lastparent_lsa), LSA_AS_ARGS (&topops_p->stack[i].posp_lsa));
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
logtb_dump_tdes_distribute_transaction (FILE * out_fp, int global_tran_id, LOG_2PC_COORDINATOR * coord)
{
  int i;
  char *particp_id;		/* Participant identifier */

  /* This is a distributed transaction */
  if (coord != NULL)
    {
      fprintf (out_fp, "    COORDINATOR SITE(or NESTED PARTICIPANT SITE)");
    }
  else
    {
      fprintf (out_fp, "    PARTICIPANT SITE");
    }

  fprintf (out_fp, " of global tranid = %d\n", global_tran_id);

  if (coord != NULL)
    {
      fprintf (out_fp, "    Num_participants = %d, Partids = ", coord->num_particps);
      for (i = 0; i < coord->num_particps; i++)
	{
	  particp_id = ((char *) coord->block_particps_ids + i * coord->particp_id_length);
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
 * logtb_free_tran_mvcc_info - free transaction MVCC info
 *
 * return: nothing..
 *
 *   tdes(in/out): Transaction descriptor
 */
static void
logtb_free_tran_mvcc_info (LOG_TDES * tdes)
{
  MVCC_INFO *curr_mvcc_info = &tdes->mvccinfo;

  curr_mvcc_info->snapshot.m_active_mvccs.finalize ();
  curr_mvcc_info->sub_ids.clear ();
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
  LSA_SET_NULL (&tdes->commit_abort_lsa);
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
  tdes->m_multiupd_stats.clear ();
  if (tdes->interrupt == (int) true)
    {
      tdes->interrupt = false;
#if defined (HAVE_ATOMIC_BUILTINS)
      ATOMIC_INC_32 (&log_Gl.trantable.num_interrupts, -1);
#else
      TR_TABLE_CS_ENTER (thread_p);
      log_Gl.trantable.num_interrupts--;
      TR_TABLE_CS_EXIT (thread_p);
#endif
    }
  tdes->m_modified_classes.clear ();

  for (i = 0; i < tdes->cur_repl_record; i++)
    {
      if (tdes->repl_records[i].repl_data)
	{
	  free_and_init (tdes->repl_records[i].repl_data);
	}
    }

  save_heap_id = db_change_private_heap (thread_p, 0);
  for (i = 0; i < tdes->num_exec_queries && i < MAX_NUM_EXEC_QUERY_HISTORY; i++)
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
  tdes->query_timeout = 0;
  tdes->query_start_time = 0;
  tdes->tran_start_time = 0;
  XASL_ID_SET_NULL (&tdes->xasl_id);
  tdes->waiting_for_res = NULL;
  tdes->tran_abort_reason = TRAN_NORMAL;
  tdes->num_exec_queries = 0;
  tdes->suppress_replication = 0;
  tdes->m_log_postpone_cache.reset ();
  tdes->has_supplemental_log = false;

  logtb_tran_clear_update_stats (&tdes->log_upd_stats);

  assert (tdes->mvccinfo.id == MVCCID_NULL);

  if (BOOT_WRITE_ON_STANDY_CLIENT_TYPE (tdes->client.client_type))
    {
      tdes->disable_modifications = 0;
    }
  else
    {
      tdes->disable_modifications = db_Disable_modifications;
    }
  tdes->has_deadlock_priority = false;

  tdes->num_log_records_written = 0;

  LSA_SET_NULL (&tdes->rcv.tran_start_postpone_lsa);
  LSA_SET_NULL (&tdes->rcv.sysop_start_postpone_lsa);
  LSA_SET_NULL (&tdes->rcv.atomic_sysop_start_lsa);
  LSA_SET_NULL (&tdes->rcv.analysis_last_aborted_sysop_lsa);
  LSA_SET_NULL (&tdes->rcv.analysis_last_aborted_sysop_start_lsa);
}

/*
 * logtb_initialize_tdes - initialize the transaction descriptor
 *
 * return: nothing..
 *
 *   tdes(in/out): Transaction descriptor
 *   tran_index(in): Transaction index
 */
void
logtb_initialize_tdes (LOG_TDES * tdes, int tran_index)
{
  int i, r;

  tdes->tran_index = tran_index;
  tdes->trid = NULL_TRANID;
  tdes->isloose_end = false;
  tdes->coord = NULL;
  tdes->client_id = -1;
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
  LSA_SET_NULL (&tdes->commit_abort_lsa);

  r = rmutex_initialize (&tdes->rmutex_topop, RMUTEX_NAME_TDES_TOPOP);
  assert (r == NO_ERROR);

  tdes->topops.stack = NULL;
  tdes->topops.last = -1;
  tdes->topops.max = 0;
  tdes->num_unique_btrees = 0;
  tdes->max_unique_btrees = 0;
  tdes->m_multiupd_stats.construct ();
  tdes->num_transient_classnames = 0;
  tdes->num_repl_records = 0;
  tdes->cur_repl_record = 0;
  tdes->append_repl_recidx = -1;
  tdes->fl_mark_repl_recidx = -1;
  tdes->repl_records = NULL;
  LSA_SET_NULL (&tdes->repl_insert_lsa);
  LSA_SET_NULL (&tdes->repl_update_lsa);
  tdes->first_save_entry = NULL;
  tdes->suppress_replication = 0;
  tdes->lob_locator_root.init ();
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

  tdes->num_log_records_written = 0;

  tdes->mvccinfo.init ();

  tdes->log_upd_stats.cos_count = 0;
  tdes->log_upd_stats.cos_first_chunk = NULL;
  tdes->log_upd_stats.cos_current_chunk = NULL;
  tdes->log_upd_stats.classes_cos_hash = NULL;

  tdes->log_upd_stats.stats_count = 0;
  tdes->log_upd_stats.stats_first_chunk = NULL;
  tdes->log_upd_stats.stats_current_chunk = NULL;
  tdes->log_upd_stats.unique_stats_hash = NULL;

  tdes->log_upd_stats.unique_stats_hash =
    mht_create ("Tran_unique_stats", 101, logtb_tran_btid_hash_func, logtb_tran_btid_hash_cmp_func);
  tdes->log_upd_stats.classes_cos_hash = mht_create ("Tran_classes_cos", 101, oid_hash, oid_compare_equals);

  tdes->block_global_oldest_active_until_commit = false;
  tdes->is_user_active = false;

  tdes->has_supplemental_log = false;

  LSA_SET_NULL (&tdes->rcv.tran_start_postpone_lsa);
  LSA_SET_NULL (&tdes->rcv.sysop_start_postpone_lsa);
  LSA_SET_NULL (&tdes->rcv.atomic_sysop_start_lsa);
  LSA_SET_NULL (&tdes->rcv.analysis_last_aborted_sysop_lsa);
  LSA_SET_NULL (&tdes->rcv.analysis_last_aborted_sysop_start_lsa);
}

/*
 * logtb_finalize_tdes - finalize the transaction descriptor
 *
 * return: nothing.
 *
 *   thread_p(in):
 *   tdes(in/out): Transaction descriptor
 */
void
logtb_finalize_tdes (THREAD_ENTRY * thread_p, LOG_TDES * tdes)
{
  int r;

  logtb_clear_tdes (thread_p, tdes);
  logtb_free_tran_mvcc_info (tdes);
  logtb_tran_free_update_stats (&tdes->log_upd_stats);

  r = rmutex_finalize (&tdes->rmutex_topop);
  assert (r == NO_ERROR);

  if (tdes->topops.max != 0)
    {
      free_and_init (tdes->topops.stack);
      tdes->topops.max = 0;
      tdes->topops.last = -1;
    }
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
#if defined (HAVE_ATOMIC_BUILTINS)
  int trid, next_trid;

  logtb_clear_tdes (thread_p, tdes);

  do
    {
      trid = VOLATILE_ACCESS (log_Gl.hdr.next_trid, int);

      next_trid = trid + 1;
      if (next_trid < 0)
	{
	  /* an overflow happened. starts with its base */
	  next_trid = LOG_SYSTEM_TRANID + 1;
	}

      /* Need to check (trid < LOG_SYSTEM_TRANID + 1) for robustness. If log_Gl.hdr.next_trid was reset to 0 (see
       * log_rv_analysis_log_end), this prevents us from correctly generating trids. */
    }
  while (!ATOMIC_CAS_32 (&log_Gl.hdr.next_trid, trid, next_trid) || (trid < LOG_SYSTEM_TRANID + 1));

  assert (LOG_SYSTEM_TRANID + 1 <= trid && trid <= DB_INT32_MAX);

  tdes->trid = trid;
  return trid;
#else
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
#endif
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

  assert (trid != NULL_TRANID);

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
logtb_find_tran_index_host_pid (THREAD_ENTRY * thread_p, const char *host_name, int process_id)
{
  int i;
  int tran_index = NULL_TRAN_INDEX;	/* The transaction index */
  LOG_TDES *tdes;		/* Transaction descriptor */

  TR_TABLE_CS_ENTER_READ_MODE (thread_p);
  /* Search the transaction table for such transaction */
  for (i = 0; i < NUM_TOTAL_TRAN_INDICES; i++)
    {
      tdes = log_Gl.trantable.all_tdes[i];
      if (tdes != NULL && tdes->trid != NULL_TRANID && tdes->client.process_id == process_id
	  && strcmp (tdes->client.get_host_name (), host_name) == 0)
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
	  if (!BOOT_IS_ALLOWED_CLIENT_TYPE_IN_MT_MODE (tdes->client.get_host_name (), boot_Host_name,
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
const char *
logtb_find_client_name (int tran_index)
{
  LOG_TDES *tdes;

  tdes = LOG_FIND_TDES (tran_index);
  if (tdes != NULL && tdes->trid != NULL_TRANID)
    {
      return tdes->client.get_db_user ();
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
      // *INDENT-OFF*
      tdes->client.set_user ((user_name) ? user_name : clientids::UNKNOWN_ID);
      // *INDENT-ON*
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
 * logtb_set_current_user_active() - set active state of current user
 *
 * return:
 * thread_p(in):
 * is_user_active(in):
 */
void
logtb_set_current_user_active (THREAD_ENTRY * thread_p, bool is_user_active)
{
  int tran_index;
  LOG_TDES *tdes;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);

  tdes->is_user_active = is_user_active;
}

/*
 * logtb_find_client_hostname - find client hostname of transaction index
 *
 * return: client hostname
 *
 *   tran_index(in): Index of transaction
 */
const char *
logtb_find_client_hostname (int tran_index)
{
  LOG_TDES *tdes;

  tdes = LOG_FIND_TDES (tran_index);
  if (tdes != NULL && tdes->trid != NULL_TRANID)
    {
      return tdes->client.get_host_name ();
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
 *   client_prog_name(out): Name of the client program
 *   client_user_name(out): Name of the client user
 *   client_host_name(out): Name of the client host
 *   client_pid(out): Identifier of the process of the host where the client
 *                    client transaction runs.
 *
 * Note: Find the client user name, host name, and process identifier
 *              associated with the given transaction index.
 *
 *       The above pointers are valid until the client is unregister.
 */
int
logtb_find_client_name_host_pid (int tran_index, const char **client_prog_name, const char **client_user_name,
				 const char **client_host_name, int *client_pid)
{
  LOG_TDES *tdes;		/* Transaction descriptor */

  tdes = LOG_FIND_TDES (tran_index);

  if (tdes == NULL || tdes->trid == NULL_TRANID)
    {
      // *INDENT-OFF*
      *client_prog_name = clientids::UNKNOWN_ID;
      *client_user_name = clientids::UNKNOWN_ID;
      *client_host_name = clientids::UNKNOWN_ID;
      // *INDENT-ON*
      *client_pid = -1;
      return ER_FAILED;
    }

  *client_prog_name = tdes->client.get_program_name ();
  *client_user_name = tdes->client.get_db_user ();
  *client_host_name = tdes->client.get_host_name ();
  *client_pid = tdes->client.process_id;

  return NO_ERROR;
}

#if defined (SERVER_MODE)
/* logtb_find_client_tran_name_host_pid - same as logtb_find_client_name_host_pid, but also gets tran_index.
 */
int
logtb_find_client_tran_name_host_pid (int &tran_index, const char **client_prog_name, const char **client_user_name,
				      const char **client_host_name, int *client_pid)
{
  tran_index = logtb_get_current_tran_index ();
  return logtb_find_client_name_host_pid (tran_index, client_prog_name, client_user_name, client_host_name, client_pid);
}
#endif // SERVER_MODE

/*
 * logtb_find_client_ids - find client identifiers OF TRANSACTION INDEX
 *
 * return: NO_ERROR if all OK, ER_ status otherwise
 *
 *   tran_index(in): Index of transaction
 *   client_info(out): pointer to CLIENTIDS structure
 *
 */
int
logtb_get_client_ids (int tran_index, CLIENTIDS * client_info)
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
xlogtb_get_pack_tran_table (THREAD_ENTRY * thread_p, char **buffer_p, int *size_p, int include_query_exec_info)
{
  int error_code = NO_ERROR;
  int num_clients = 0, num_clients_packed = 0;
  int i;
  int size;
  char *buffer, *ptr;
  LOG_TDES *tdes;		/* Transaction descriptor */
  int num_total_indices;
#if defined(SERVER_MODE)
  INT64 current_msec = 0;
  TRAN_QUERY_EXEC_INFO *query_exec_info = NULL;
  XASL_CACHE_ENTRY *ent = NULL;
#endif

  /* Note, we'll be in a critical section while we gather the data but the section ends as soon as we return the data.
   * This means that the transaction table can change after the information is used. */

  TR_TABLE_CS_ENTER_READ_MODE (thread_p);

  num_total_indices = NUM_TOTAL_TRAN_INDICES;
#if defined(SERVER_MODE)
  if (include_query_exec_info)
    {
      query_exec_info = (TRAN_QUERY_EXEC_INFO *) calloc (num_total_indices, sizeof (TRAN_QUERY_EXEC_INFO));

      if (query_exec_info == NULL)
	{
	  error_code = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto error;
	}

      current_msec = log_get_clock_msec ();
    }
#endif

  size = OR_INT_SIZE;		/* Number of client transactions */

  /* Find size of needed buffer */
  for (i = 0; i < num_total_indices; i++)
    {
      tdes = log_Gl.trantable.all_tdes[i];
      if (tdes == NULL || tdes->trid == NULL_TRANID || tdes->tran_index == LOG_SYSTEM_TRAN_INDEX)
	{
	  /* The index is not assigned or is system transaction (no-client) */
	  continue;
	}

      size += (3 * OR_INT_SIZE	/* tran index + tran state + process id */
	       + OR_INT_SIZE + DB_ALIGN (LOG_USERNAME_MAX, INT_ALIGNMENT)
	       + OR_INT_SIZE + DB_ALIGN (PATH_MAX, INT_ALIGNMENT)
	       + OR_INT_SIZE + DB_ALIGN (L_cuserid, INT_ALIGNMENT)
	       + OR_INT_SIZE + DB_ALIGN (CUB_MAXHOSTNAMELEN, INT_ALIGNMENT));

#if defined(SERVER_MODE)
      if (include_query_exec_info)
	{
	  if (tdes->query_start_time > 0)
	    {
	      query_exec_info[i].query_time = (float) (current_msec - tdes->query_start_time) / 1000.0f;
	    }

	  if (tdes->tran_start_time > 0)
	    {
	      query_exec_info[i].tran_time = (float) (current_msec - tdes->tran_start_time) / 1000.0f;
	    }

	  lock_get_lock_holder_tran_index (thread_p, &query_exec_info[i].wait_for_tran_index_string, tdes->tran_index,
					   tdes->waiting_for_res);

	  if (!XASL_ID_IS_NULL (&tdes->xasl_id))
	    {
	      /* retrieve query statement in the xasl_cache entry */
	      error_code = xcache_find_sha1 (thread_p, &tdes->xasl_id.sha1, XASL_CACHE_SEARCH_GENERIC, &ent, NULL);
	      if (error_code != NO_ERROR)
		{
		  ASSERT_ERROR ();
		  goto error;
		}

	      /* entry can be NULL, if xasl cache entry is deleted */
	      if (ent != NULL)
		{
		  if (ent->sql_info.sql_hash_text != NULL)
		    {
		      char *sql = ent->sql_info.sql_hash_text;

		      if (qmgr_get_sql_id (thread_p, &query_exec_info[i].sql_id, sql, (int) strlen (sql)) != NO_ERROR)
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
		  xcache_unfix (thread_p, ent);
		  ent = NULL;
		}

	      /* structure copy */
	      XASL_ID_COPY (&query_exec_info[i].xasl_id, &tdes->xasl_id);
	    }
	  else
	    {
	      XASL_ID_SET_NULL (&query_exec_info[i].xasl_id);
	    }

	  size += (2 * OR_FLOAT_SIZE	/* query time + tran time */
		   + or_packed_string_length (query_exec_info[i].wait_for_tran_index_string, NULL)
		   + or_packed_string_length (query_exec_info[i].query_stmt, NULL)
		   + or_packed_string_length (query_exec_info[i].sql_id, NULL) + OR_XASL_ID_SIZE);
	}
#endif
      num_clients++;
    }

  /* Now allocate the area and pack the information */
  buffer = (char *) malloc (size);
  if (buffer == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) size);
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      goto error;
    }

  ptr = buffer;
  ptr = or_pack_int (ptr, num_clients);

  /* Find size of needed buffer */
  for (i = 0; i < num_total_indices; i++)
    {
      tdes = log_Gl.trantable.all_tdes[i];
      if (tdes == NULL || tdes->trid == NULL_TRANID || tdes->tran_index == LOG_SYSTEM_TRAN_INDEX)
	{
	  /* The index is not assigned or is system transaction (no-client) */
	  continue;
	}

      ptr = or_pack_int (ptr, tdes->tran_index);
      ptr = or_pack_int (ptr, tdes->state);
      ptr = or_pack_int (ptr, tdes->client.process_id);
      ptr = or_pack_string_with_length (ptr, tdes->client.get_db_user (), tdes->client.db_user.size ());
      ptr = or_pack_string_with_length (ptr, tdes->client.get_program_name (), tdes->client.program_name.size ());
      ptr = or_pack_string_with_length (ptr, tdes->client.get_login_name (), tdes->client.login_name.size ());
      ptr = or_pack_string_with_length (ptr, tdes->client.get_host_name (), tdes->client.host_name.size ());

#if defined(SERVER_MODE)
      if (include_query_exec_info)
	{
	  ptr = or_pack_float (ptr, query_exec_info[i].query_time);
	  ptr = or_pack_float (ptr, query_exec_info[i].tran_time);
	  ptr = or_pack_string (ptr, query_exec_info[i].wait_for_tran_index_string);
	  ptr = or_pack_string (ptr, query_exec_info[i].query_stmt);
	  ptr = or_pack_string (ptr, query_exec_info[i].sql_id);
	  OR_PACK_XASL_ID (ptr, &query_exec_info[i].xasl_id);
	}
#endif

      num_clients_packed++;
      assert (num_clients_packed <= num_clients);
      assert (ptr <= buffer + size);
    }

  assert (num_clients_packed == num_clients);
  assert (num_total_indices == NUM_TOTAL_TRAN_INDICES);
  assert (ptr <= buffer + size);

  *buffer_p = buffer;
  *size_p = CAST_BUFLEN (ptr - buffer);

error:
  TR_TABLE_CS_EXIT (thread_p);

#if defined(SERVER_MODE)
  if (query_exec_info != NULL)
    {
      for (i = 0; i < num_total_indices; i++)
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

  if (ent != NULL)
    {
      xcache_unfix (thread_p, ent);
    }
#endif

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
const char *
logtb_find_current_client_name (THREAD_ENTRY * thread_p)
{
  return logtb_find_client_name (LOG_FIND_THREAD_TRAN_INDEX (thread_p));
}

/*
 * logtb_find_current_client_hostname - find client hostname of current transaction
 *
 * return: client hostname
 */
const char *
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
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
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
 * logtb_find_log_records_count -  find log records count for given transaction
 *
 * return: num_log_records_written...
 *
 *   tran_index(in): Index of transaction
 */
int
logtb_find_log_records_count (int tran_index)
{
  LOG_TDES *tdes;		/* Transaction descriptor */

  tdes = LOG_FIND_TDES (tran_index);
  if (tdes != NULL)
    {
      return tdes->num_log_records_written;
    }
  else
    {
      return 0;
    }
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
 *
 * Note:Reset the default isolation level for the current transaction index (client).
 *
 * Note/Warning: This function must be called when the current transaction has
 *               not been done any work (i.e, just after restart, commit, or
 *               abort), otherwise, its isolation behaviour will be undefined.
 */
int
xlogtb_reset_isolation (THREAD_ENTRY * thread_p, TRAN_ISOLATION isolation)
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
    }
  else
    {
      er_set (ER_SYNTAX_ERROR_SEVERITY, ARG_FILE_LINE, ER_MVCC_LOG_INVALID_ISOLATION_LEVEL, 0);
      error_code = ER_MVCC_LOG_INVALID_ISOLATION_LEVEL;
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
  logtb_set_tran_index_interrupt (thread_p, LOG_FIND_THREAD_TRAN_INDEX (thread_p), set);
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
logtb_set_tran_index_interrupt (THREAD_ENTRY * thread_p, int tran_index, bool set)
{
  LOG_TDES *tdes;		/* Transaction descriptor */

  if (tran_index == LOG_SYSTEM_TRAN_INDEX)
    {
#if defined (SERVER_MODE)
      assert (false);
#endif // SERVER_MODE
      return false;
    }

  if (log_Gl.trantable.area != NULL)
    {
      tdes = LOG_FIND_TDES (tran_index);
      if (tdes != NULL && tdes->trid != NULL_TRANID)
	{
	  if (tdes->interrupt != (int) set)
	    {
#if defined (HAVE_ATOMIC_BUILTINS)
	      tdes->interrupt = (int) set;
	      if (set == true)
		{
		  ATOMIC_INC_32 (&log_Gl.trantable.num_interrupts, 1);
		}
	      else
		{
		  ATOMIC_INC_32 (&log_Gl.trantable.num_interrupts, -1);
		}
#else
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
#endif
	    }

	  if (set == true)
	    {
	      pgbuf_force_to_check_for_interrupts ();
	      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_INTERRUPTING, 1, tran_index);
	      perfmon_inc_stat (thread_p, PSTAT_TRAN_NUM_INTERRUPTS);
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
logtb_is_interrupted_tdes (THREAD_ENTRY * thread_p, LOG_TDES * tdes, bool clear, bool * continue_checking)
{
  bool interrupt;
  INT64 now;
#if !defined(SERVER_MODE)
  struct timeval tv;

#else /* SERVER_MODE */
  /* vacuum threads should not be interruptible (unless this is still recovery). */
  assert (!BO_IS_SERVER_RESTARTED () || !VACUUM_IS_THREAD_VACUUM (thread_p));
#endif /* SERVER_MODE */

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
  else if (interrupt == true)
    {
      if (clear)
	{
	  tdes->interrupt = false;
#if !defined (HAVE_ATOMIC_BUILTINS)
	  TR_TABLE_CS_ENTER (thread_p);
	  log_Gl.trantable.num_interrupts--;
#else
	  ATOMIC_INC_32 (&log_Gl.trantable.num_interrupts, -1);
#endif

	  if (log_Gl.trantable.num_interrupts > 0)
	    {
	      *continue_checking = true;
	    }
	  else
	    {
	      *continue_checking = false;
	    }

#if !defined (HAVE_ATOMIC_BUILTINS)
	  TR_TABLE_CS_EXIT (thread_p);
#endif
	}

      cubmethod::runtime_context * rctx = cubmethod::get_rctx (thread_p);
      if (rctx)
	{
	  rctx->set_interrupt (ER_INTERRUPTED);
	}
    }
  else if (interrupt == false && tdes->query_timeout > 0)
    {
      /* In order to prevent performance degradation, we use log_Clock_msec set by thread_log_clock_thread instead of
       * calling gettimeofday here if the system supports atomic built-ins. */
#if defined(SERVER_MODE)
      now = log_get_clock_msec ();
#else /* SERVER_MODE */
      gettimeofday (&tv, NULL);
      now = (tv.tv_sec * 1000LL) + (tv.tv_usec / 1000LL);
#endif /* !SERVER_MODE */
      if (tdes->query_timeout < now)
	{
	  er_log_debug (ARG_FILE_LINE,
			"logtb_is_interrupted_tdes: timeout %lld milliseconds delayed (expected=%lld, now=%lld)",
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
 * Note: Find if the current execution must be stopped due to an interrupt (^C). If clear is true, the interruption flag
 *       is cleared; This is the expected case, once someone is notified, we do not have to keep the flag on.
 *
 *       If the transaction is not active, false is returned. For example, in the middle of an undo action, the
 *       transaction will not be interrupted. The recovery manager will interrupt the transaction at the end of the undo
 *       action... in this case the transaction will be partially aborted.
 */
bool
logtb_is_interrupted (THREAD_ENTRY * thread_p, bool clear, bool * continue_checking)
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
 * Note: Find if the execution of the given transaction must be stopped due to an interrupt (^C). If clear is true, the
 *       interruption flag is cleared; This is the expected case, once someone is notified, we do not have to keep the
 *       flag on.
 *       This function is called to see if a transaction that is waiting (e.g., suspended on a lock) on an event must
 *       be interrupted.
 */
bool
logtb_is_interrupted_tran (THREAD_ENTRY * thread_p, bool clear, bool * continue_checking, int tran_index)
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
  logtb_set_suppress_repl_on_transaction (thread_p, LOG_FIND_THREAD_TRAN_INDEX (thread_p), set);
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
logtb_set_suppress_repl_on_transaction (THREAD_ENTRY * thread_p, int tran_index, int set)
{
  LOG_TDES *tdes;		/* Transaction descriptor */

  if (log_Gl.trantable.area != NULL)
    {
      tdes = LOG_FIND_TDES (tran_index);
      if (tdes != NULL && tdes->trid != NULL_TRANID)
	{
	  if (tdes->suppress_replication != set)
	    {
	      tdes->suppress_replication = set;
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
      assert (tdes == NULL || tdes->is_active_worker_transaction ());
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
  er_log_debug (ARG_FILE_LINE, "logtb_disable_update: db_Disable_modifications = %d\n", db_Disable_modifications);
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
      er_log_debug (ARG_FILE_LINE, "logtb_enable_update: db_Disable_modifications = %d\n", db_Disable_modifications);
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
 * logtb_tran_free_update_stats () - Free logged list of update statistics.
 *
 * return	    : Void.
 * log_upd_stats (in) : List of logged update statistics records.
 */
static void
logtb_tran_free_update_stats (LOG_TRAN_UPDATE_STATS * log_upd_stats)
{
  LOG_TRAN_CLASS_COS_CHUNK *cos_chunk = NULL, *next_cos_chunk = NULL;
  LOG_TRAN_BTID_UNIQUE_STATS_CHUNK *stats_chunk = NULL, *next_stats_chunk = NULL;

  logtb_tran_clear_update_stats (log_upd_stats);

  /* free count optimization structure */
  cos_chunk = log_upd_stats->cos_first_chunk;
  if (cos_chunk != NULL)
    {
      for (; cos_chunk != NULL; cos_chunk = next_cos_chunk)
	{
	  next_cos_chunk = cos_chunk->next_chunk;
	  free (cos_chunk);
	}
      log_upd_stats->cos_first_chunk = NULL;
      log_upd_stats->cos_current_chunk = NULL;
      log_upd_stats->cos_count = 0;
    }
  if (log_upd_stats->classes_cos_hash != NULL)
    {
      mht_destroy (log_upd_stats->classes_cos_hash);
      log_upd_stats->classes_cos_hash = NULL;
    }

  /* free unique statistics structure */
  stats_chunk = log_upd_stats->stats_first_chunk;
  if (stats_chunk != NULL)
    {
      for (; stats_chunk != NULL; stats_chunk = next_stats_chunk)
	{
	  next_stats_chunk = stats_chunk->next_chunk;
	  free (stats_chunk);
	}
      log_upd_stats->stats_first_chunk = NULL;
      log_upd_stats->stats_current_chunk = NULL;
      log_upd_stats->stats_count = 0;
    }
  if (log_upd_stats->unique_stats_hash != NULL)
    {
      mht_destroy (log_upd_stats->unique_stats_hash);
      log_upd_stats->unique_stats_hash = NULL;
    }
}

/*
 * logtb_tran_clear_update_stats () - Clear logged update statistics.
 *				      Entries are not actually freed, they are
 *				      appended to a list of free entries ready
 *				      to be reused.
 *
 * return	    : Void.
 * log_upd_stats (in) : Pointer to update statistics log.
 */
static void
logtb_tran_clear_update_stats (LOG_TRAN_UPDATE_STATS * log_upd_stats)
{
  /* clear count optimization structure */
  log_upd_stats->cos_current_chunk = log_upd_stats->cos_first_chunk;
  log_upd_stats->cos_count = 0;
  if (log_upd_stats->classes_cos_hash != NULL)
    {
      mht_clear (log_upd_stats->classes_cos_hash, NULL, NULL);
    }

  /* clear unique statistics structure */
  log_upd_stats->stats_current_chunk = log_upd_stats->stats_first_chunk;
  log_upd_stats->stats_count = 0;
  if (log_upd_stats->unique_stats_hash != NULL)
    {
      mht_clear (log_upd_stats->unique_stats_hash, NULL, NULL);
    }
}

/*
 * logtb_tran_btid_hash_func() - Hash function for BTIDs
 *   return: hash value
 *   key(in): BTID to hash
 *   ht_size(in): Size of hash table
 */
static unsigned int
logtb_tran_btid_hash_func (const void *key, const unsigned int ht_size)
{
  return ((BTID *) key)->vfid.fileid % ht_size;
}

/*
 * logtb_tran_btid_hash_func() - Comparison function for BTIDs (equal or not)
 *   return: 0 not equal, 1 otherwise
 *   key1(in): left key
 *   key2(in): right key
 */
static int
logtb_tran_btid_hash_cmp_func (const void *key1, const void *key2)
{
  return BTID_IS_EQUAL ((BTID *) key1, (BTID *) key2);
}

/*
 * logtb_tran_create_btid_unique_stats () - allocates memory and initializes
 *					    statistics associated with btid
 *
 * return	    : The address of newly created statistics structure or NULL
 *		      in case of error.
 * thread_p(in)	    :
 * btid (in)	    : Id of unique index for which the statistics will be
 *		      created
 */
static LOG_TRAN_BTID_UNIQUE_STATS *
logtb_tran_create_btid_unique_stats (THREAD_ENTRY * thread_p, const BTID * btid)
{
  LOG_TRAN_BTID_UNIQUE_STATS *unique_stats = NULL;
  LOG_TDES *tdes = LOG_FIND_TDES (LOG_FIND_THREAD_TRAN_INDEX (thread_p));

  if (btid == NULL)
    {
      assert (false);
      return NULL;
    }

  if (tdes->log_upd_stats.stats_count % TRAN_UNIQUE_STATS_CHUNK_SIZE == 0)
    {
      LOG_TRAN_BTID_UNIQUE_STATS_CHUNK *chunk = NULL;

      if (tdes->log_upd_stats.stats_current_chunk != NULL
	  && tdes->log_upd_stats.stats_current_chunk->next_chunk != NULL)
	{
	  /* reuse the old chunk */
	  chunk = tdes->log_upd_stats.stats_current_chunk->next_chunk;
	}
      else
	{
	  /* if the entire allocated space was exhausted then alloc a new chunk */
	  int size =
	    sizeof (LOG_TRAN_BTID_UNIQUE_STATS_CHUNK) + (TRAN_UNIQUE_STATS_CHUNK_SIZE -
							 1) * sizeof (LOG_TRAN_BTID_UNIQUE_STATS);
	  chunk = (LOG_TRAN_BTID_UNIQUE_STATS_CHUNK *) malloc (size);

	  if (chunk == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, size);
	      return NULL;
	    }
	  if (tdes->log_upd_stats.stats_first_chunk == NULL)
	    {
	      tdes->log_upd_stats.stats_first_chunk = chunk;
	    }
	  else
	    {
	      tdes->log_upd_stats.stats_current_chunk->next_chunk = chunk;
	    }
	  chunk->next_chunk = NULL;
	}
      tdes->log_upd_stats.stats_current_chunk = chunk;
    }

  /* get a new entry */
  unique_stats =
    &tdes->log_upd_stats.stats_current_chunk->buffer[tdes->log_upd_stats.stats_count++ % TRAN_UNIQUE_STATS_CHUNK_SIZE];

  BTID_COPY (&unique_stats->btid, btid);
  unique_stats->deleted = false;

  /* init transaction local statistics */
  unique_stats->tran_stats.num_keys = 0;
  unique_stats->tran_stats.num_oids = 0;
  unique_stats->tran_stats.num_nulls = 0;

  /* init global statistics */
  unique_stats->global_stats.num_keys = -1;
  unique_stats->global_stats.num_oids = -1;
  unique_stats->global_stats.num_nulls = -1;

  /* Store the new entry in hash */
  if (mht_put (tdes->log_upd_stats.unique_stats_hash, &unique_stats->btid, unique_stats) == NULL)
    {
      return NULL;
    }

  return unique_stats;
}

/*
 * logtb_tran_create_class_cos () - creates a new count optimization state entry
 *				    for a class
 *
 * return	    : return the adddress of newly created entry
 * thread_p(in)	    :
 * class_oid (in)   : OID of the class for which the entry will be created
 */
static LOG_TRAN_CLASS_COS *
logtb_tran_create_class_cos (THREAD_ENTRY * thread_p, const OID * class_oid)
{
  LOG_TDES *tdes = LOG_FIND_TDES (LOG_FIND_THREAD_TRAN_INDEX (thread_p));
  LOG_TRAN_CLASS_COS *entry = NULL;

  if (tdes->log_upd_stats.cos_count % COS_CLASSES_CHUNK_SIZE == 0)
    {
      LOG_TRAN_CLASS_COS_CHUNK *chunk = NULL;
      if (tdes->log_upd_stats.cos_current_chunk != NULL && tdes->log_upd_stats.cos_current_chunk->next_chunk != NULL)
	{
	  /* reuse the old chunk */
	  chunk = tdes->log_upd_stats.cos_current_chunk->next_chunk;
	}
      else
	{
	  /* if the entire allocated space was exhausted then alloc a new chunk */
	  int size = sizeof (LOG_TRAN_CLASS_COS_CHUNK) + (COS_CLASSES_CHUNK_SIZE - 1) * sizeof (LOG_TRAN_CLASS_COS);
	  chunk = (LOG_TRAN_CLASS_COS_CHUNK *) malloc (size);

	  if (chunk == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, size);
	      return NULL;
	    }
	  if (tdes->log_upd_stats.cos_first_chunk == NULL)
	    {
	      tdes->log_upd_stats.cos_first_chunk = chunk;
	    }
	  else
	    {
	      tdes->log_upd_stats.cos_current_chunk->next_chunk = chunk;
	    }
	  chunk->next_chunk = NULL;
	}
      tdes->log_upd_stats.cos_current_chunk = chunk;
    }

  /* get a new entry */
  entry = &tdes->log_upd_stats.cos_current_chunk->buffer[tdes->log_upd_stats.cos_count++ % COS_CLASSES_CHUNK_SIZE];

  /* init newly created entry */
  COPY_OID (&entry->class_oid, class_oid);
  entry->count_state = COS_NOT_LOADED;

  if (mht_put (tdes->log_upd_stats.classes_cos_hash, &entry->class_oid, entry) == NULL)
    {
      return NULL;
    }

  return entry;
}

/*
 * logtb_tran_find_class_cos () - searches the hash of count optimization states
 *				  for a specific class oid
 *
 * return	    : address of found (or newly created) entry or null
 *		      otherwise
 * thread_p(in)	    :
 * class_oid (in)   : OID of the class for which the entry will be created
 * create(in)	    : true if the caller needs a new entry to be created if not
 *		      found an already existing one
 */
LOG_TRAN_CLASS_COS *
logtb_tran_find_class_cos (THREAD_ENTRY * thread_p, const OID * class_oid, bool create)
{
  LOG_TDES *tdes = LOG_FIND_TDES (LOG_FIND_THREAD_TRAN_INDEX (thread_p));
  LOG_TRAN_CLASS_COS *entry = NULL;

  assert (tdes != NULL && class_oid != NULL);

  /* search */
  entry = (LOG_TRAN_CLASS_COS *) mht_get (tdes->log_upd_stats.classes_cos_hash, class_oid);

  if (entry == NULL && create)
    {
      /* create if not found */
      entry = logtb_tran_create_class_cos (thread_p, class_oid);
    }

  return entry;
}

/*
 * logtb_tran_find_btid_stats () - searches the hash of statistics for a
 *				   specific index.
 *
 * return	    : address of found (or newly created) statistics or null
 *		      otherwise
 * thread_p(in)	    :
 * btid (in)	    : index id to be searched
 * create(in)	    : true if the caller needs a new entry to be created if not
 *		      found an already existing one
 */
LOG_TRAN_BTID_UNIQUE_STATS *
logtb_tran_find_btid_stats (THREAD_ENTRY * thread_p, const BTID * btid, bool create)
{
  LOG_TRAN_BTID_UNIQUE_STATS *unique_stats = NULL;
  LOG_TDES *tdes = LOG_FIND_TDES (LOG_FIND_THREAD_TRAN_INDEX (thread_p));

  if (btid == NULL)
    {
      return NULL;
    }

  /* search */
  unique_stats = (LOG_TRAN_BTID_UNIQUE_STATS *) mht_get (tdes->log_upd_stats.unique_stats_hash, btid);

  if (unique_stats == NULL && create)
    {
      /* create if not found */
      unique_stats = logtb_tran_create_btid_unique_stats (thread_p, btid);
    }

  return unique_stats;
}

/*
 * logtb_tran_update_btid_unique_stats () - updates statistics associated with
 *					    the given btid
 *
 * return	    : error code or NO_ERROR
 * thread_p(in)	    :
 * btid (in)	    : index id to be searched
 * n_keys(in)	    : number of keys to be added to statistics
 * n_oids(in)	    : number of oids to be added to statistics
 * n_nulls(in)	    : number of nulls to be added to statistics
 *
 * Note: the statistics are searched and created if they not exist.
 */
int
logtb_tran_update_btid_unique_stats (THREAD_ENTRY * thread_p, const BTID * btid, long long n_keys, long long n_oids,
				     long long n_nulls)
{
  /* search and create if not found */
  LOG_TRAN_BTID_UNIQUE_STATS *unique_stats = logtb_tran_find_btid_stats (thread_p, btid, true);

  if (unique_stats == NULL)
    {
      return ER_FAILED;
    }

  /* update statistics */
  unique_stats->tran_stats.num_keys += n_keys;
  unique_stats->tran_stats.num_oids += n_oids;
  unique_stats->tran_stats.num_nulls += n_nulls;

  return NO_ERROR;
}

/*
 * logtb_tran_update_unique_stats () - updates statistics associated with
 *				       the given class and btid
 *
 * return	    : error code or NO_ERROR
 * thread_p(in)	    :
 * btid (in)	    : B-tree id to be searched
 * n_keys(in)	    : number of keys to be added to statistics
 * n_oids(in)	    : number of oids to be added to statistics
 * n_nulls(in)	    : number of nulls to be added to statistics
 * write_to_log	    : if true then new statistics wil be written to log
 *
 * Note: the statistics are searched and created if they not exist.
 */
int
logtb_tran_update_unique_stats (THREAD_ENTRY * thread_p, const BTID * btid, long long n_keys, long long n_oids,
				long long n_nulls, bool write_to_log)
{
  int error = NO_ERROR;

  /* update statistics */
  error = logtb_tran_update_btid_unique_stats (thread_p, btid, n_keys, n_oids, n_nulls);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (write_to_log)
    {
      /* log statistics */
      char undo_rec_buf[3 * OR_BIGINT_SIZE + OR_BTID_ALIGNED_SIZE + MAX_ALIGNMENT];
      char redo_rec_buf[3 * OR_BIGINT_SIZE + OR_BTID_ALIGNED_SIZE + MAX_ALIGNMENT];
      RECDES undo_rec, redo_rec;

      undo_rec.area_size = ((3 * OR_BIGINT_SIZE) + OR_BTID_ALIGNED_SIZE);
      undo_rec.data = PTR_ALIGN (undo_rec_buf, MAX_ALIGNMENT);

      redo_rec.area_size = ((3 * OR_BIGINT_SIZE) + OR_BTID_ALIGNED_SIZE);
      redo_rec.data = PTR_ALIGN (redo_rec_buf, MAX_ALIGNMENT);

      btree_rv_mvcc_save_increments (btid, -n_keys, -n_oids, -n_nulls, &undo_rec);

      /* todo: remove me. redo has no use */
      /*btree_rv_mvcc_save_increments (btid, n_keys, n_oids, n_nulls, &redo_rec);

         log_append_undoredo_data2 (thread_p, RVBT_MVCC_INCREMENTS_UPD, NULL, NULL, -1, undo_rec.length, redo_rec.length,
         undo_rec.data, redo_rec.data); */
      log_append_undo_data2 (thread_p, RVBT_MVCC_INCREMENTS_UPD, NULL, NULL, NULL_OFFSET, undo_rec.length,
			     undo_rec.data);
    }

  return error;
}

// *INDENT-OFF*
int
logtb_tran_update_unique_stats (THREAD_ENTRY * thread_p, const BTID &btid, const btree_unique_stats &ustats,
                                bool write_to_log)
{
  if (ustats.is_zero ())
    {
      return NO_ERROR;
    }
  return logtb_tran_update_unique_stats (thread_p, &btid, ustats.get_key_count (), ustats.get_row_count (),
                                         ustats.get_null_count (), write_to_log);
}

int
logtb_tran_update_unique_stats (THREAD_ENTRY * thread_p, const multi_index_unique_stats &multi_stats, bool write_to_log)
{
  int error = NO_ERROR;
  for (const auto &it : multi_stats.get_map ())
    {
      error = logtb_tran_update_unique_stats (thread_p, it.first, it.second, write_to_log);
      if (error != NO_ERROR)
        {
          ASSERT_ERROR ();
          return error;
        }
    }
  return NO_ERROR;
}
// *INDENT-ON*

/*
 * logtb_tran_update_delta_hash_func () - updates statistics associated with
 *					  the given btid by local statistics
 *
 * return	    : error code or NO_ERROR
 * thread_p(in)	    :
 * data(in)	    : unique statistics
 * args(in)	    : not used
 *
 * Note: This is a function that is called for each element during the hash
 *	 iteration.
 */
static int
logtb_tran_update_delta_hash_func (THREAD_ENTRY * thread_p, void *data, void *args)
{
  LOG_TRAN_BTID_UNIQUE_STATS *unique_stats = (LOG_TRAN_BTID_UNIQUE_STATS *) data;
  int error_code = NO_ERROR;

  if (unique_stats->deleted)
    {
      /* ignore if deleted */
      return NO_ERROR;
    }

  error_code =
    logtb_update_global_unique_stats_by_delta (thread_p, &unique_stats->btid, unique_stats->tran_stats.num_oids,
					       unique_stats->tran_stats.num_nulls, unique_stats->tran_stats.num_keys,
					       true);

  return error_code;
}

/*
 * logtb_tran_update_all_global_unique_stats () - update global statistics
 *						  by local statistics for all
 *						  indexes found in transaction's
 *						  unique statistics hash
 *
 * return	    : error code or NO_ERROR
 * thread_p(in)	    :
 *
 * Note: this function must be called at the end of transaction (commit)
 */
int
logtb_tran_update_all_global_unique_stats (THREAD_ENTRY * thread_p)
{
  LOG_TDES *tdes = LOG_FIND_TDES (LOG_FIND_THREAD_TRAN_INDEX (thread_p));
  int error_code = NO_ERROR;
  bool old_check_interrupt;

  if (tdes == NULL)
    {
      return ER_FAILED;
    }

  /* We have to disable interrupt while reflecting unique stats. Please notice that the transaction is still in
   * TRAN_ACTIVE state and it may be previously interrupted but user eventually issues commit. The transaction should
   * successfully complete commit in spite of interrupt. */
  old_check_interrupt = logtb_set_check_interrupt (thread_p, false);

  error_code =
    mht_map_no_key (thread_p, tdes->log_upd_stats.unique_stats_hash, logtb_tran_update_delta_hash_func, thread_p);

  (void) logtb_set_check_interrupt (thread_p, old_check_interrupt);

  return error_code;
}

/*
 * logtb_tran_load_global_stats_func () - load global statistics into the
 *					  current transaction for a given class.
 *
 * return	    : error code or NO_ERROR
 * thread_p(in)	    :
 * data(in)	    : count optimization state entry
 * args(in)	    : not used
 *
 * Note: This function is called for each element of the count optimization
 *	 states hash. If the statistics were successfully loaded then set state
 *	 to COS_LOADED. In case of a partitioned class, statistics for each
 *	 partition are loaded.
 */
static int
logtb_tran_load_global_stats_func (THREAD_ENTRY * thread_p, void *data, void *args)
{
  int error_code = NO_ERROR, idx;
  PRUNING_CONTEXT context;
  LOG_TRAN_CLASS_COS *entry = NULL, *new_entry = NULL;
  OR_CLASSREP *classrepr = NULL;
  int classrepr_cacheindex = -1;
  bool clear_pcontext = false;

  entry = (LOG_TRAN_CLASS_COS *) data;
  if (entry->count_state != COS_TO_LOAD)
    {
      return NO_ERROR;
    }

  /* get class representation to find partition information */
  classrepr = heap_classrepr_get (thread_p, &entry->class_oid, NULL, NULL_REPRID, &classrepr_cacheindex);
  if (classrepr == NULL)
    {
      goto cleanup;
    }

  if (classrepr->has_partition_info > 0)
    {
      partition_init_pruning_context (&context);
      clear_pcontext = true;

      /* In case of partitioned class load statistics for each partition */
      error_code = partition_load_pruning_context (thread_p, &entry->class_oid, DB_PARTITIONED_CLASS, &context);
      if (error_code != NO_ERROR)
	{
	  goto cleanup;
	}
    }

  if (classrepr->has_partition_info > 0 && context.count > 0)
    {
      for (idx = 0; idx < context.count; idx++)
	{
	  if (OID_ISNULL (&context.partitions[idx].class_oid))
	    {
	      continue;
	    }
	  new_entry = logtb_tran_find_class_cos (thread_p, &context.partitions[idx].class_oid, true);
	  if (new_entry == NULL)
	    {
	      error_code = ER_FAILED;
	      goto cleanup;
	    }

	  error_code = logtb_create_unique_stats_from_repr (thread_p, &new_entry->class_oid);
	  if (error_code != NO_ERROR)
	    {
	      goto cleanup;
	    }

	  new_entry->count_state = COS_LOADED;
	}
    }

  error_code = logtb_create_unique_stats_from_repr (thread_p, &entry->class_oid);
  if (error_code != NO_ERROR)
    {
      goto cleanup;
    }

  entry->count_state = COS_LOADED;

cleanup:
  if (clear_pcontext == true)
    {
      partition_clear_pruning_context (&context);
    }
  if (classrepr != NULL)
    {
      heap_classrepr_free_and_init (classrepr, &classrepr_cacheindex);
    }

  return error_code;
}

/*
 * logtb_load_global_statistics_to_tran () - load global statistics into the
 *					     current transaction for all classes
 *					     with COS_TO_LOAD count optimization
 *					     state.
 *
 * return	    : error code or NO_ERROR
 * thread_p(in)	    :
 *
 * Note: The statistics will be loaded only for classes that have COS_TO_LOAD
 *	 count optimization state. This function is used when a snapshot is
 *	 taken.
 */
int
logtb_load_global_statistics_to_tran (THREAD_ENTRY * thread_p)
{
  int error_code = NO_ERROR;
  LOG_TDES *tdes = LOG_FIND_TDES (LOG_FIND_THREAD_TRAN_INDEX (thread_p));

  if (tdes == NULL)
    {
      return ER_FAILED;
    }

  error_code =
    mht_map_no_key (thread_p, tdes->log_upd_stats.classes_cos_hash, logtb_tran_load_global_stats_func, thread_p);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return error_code;
}

/*
 * logtb_invalidate_snapshot_data () - Make sure MVCC is invalidated.
 *
 * return	 : Void.
 * thread_p (in) : Thread entry.
 */
int
logtb_invalidate_snapshot_data (THREAD_ENTRY * thread_p)
{
  /* Get transaction descriptor */
  LOG_TDES *tdes = LOG_FIND_TDES (LOG_FIND_THREAD_TRAN_INDEX (thread_p));

  if (tdes == NULL || tdes->isolation >= TRAN_REPEATABLE_READ)
    {
      /* Nothing to do */
      return NO_ERROR;
    }

  if (tdes->mvccinfo.snapshot.valid)
    {
      /* Invalidate snapshot */
      tdes->mvccinfo.snapshot.valid = false;
      logtb_tran_reset_count_optim_state (thread_p);
    }

  return NO_ERROR;
}

/*
 * xlogtb_get_mvcc_snapshot () - Make sure snapshot is generated.
 *
 * return	 : Void.
 * thread_p (in) : Thread entry.
 */
int
xlogtb_get_mvcc_snapshot (THREAD_ENTRY * thread_p)
{
  /* Get transaction descriptor */
  MVCC_SNAPSHOT *snapshot = logtb_get_mvcc_snapshot (thread_p);
  int error_code = NO_ERROR;

  if (snapshot == NULL)
    {
      ASSERT_ERROR_AND_SET (error_code);
    }
  return error_code;
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
  MVCCID id = MVCCID_NULL;

  tdes = LOG_FIND_TDES (LOG_FIND_THREAD_TRAN_INDEX (thread_p));
  if (tdes != NULL)
    {
      if (!tdes->mvccinfo.sub_ids.empty ())
	{
	  id = tdes->mvccinfo.sub_ids.back ();
	}
      else
	{
	  id = tdes->mvccinfo.id;
	}
    }

  return id;
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
  MVCC_INFO *curr_mvcc_info = &tdes->mvccinfo;

#if defined (SA_MODE)
  /* We shouldn't be here */
  assert (false);
#endif /* SA_MODE */
  assert (tdes != NULL && curr_mvcc_info != NULL);

  if (MVCCID_IS_VALID (curr_mvcc_info->id) == false)
    {
      curr_mvcc_info->id = log_Gl.mvcc_table.get_new_mvccid ();
    }

  if (!tdes->mvccinfo.sub_ids.empty ())
    {
      return tdes->mvccinfo.sub_ids.back ();
    }

  return curr_mvcc_info->id;
}

/*
 * logtb_is_current_mvccid - check whether given mvccid is current mvccid
 *
 * return: bool
 *
 *   thread_p(in): thread entry
 *   mvccid(in): MVCC id
 */
bool
logtb_is_current_mvccid (THREAD_ENTRY * thread_p, MVCCID mvccid)
{
  LOG_TDES *tdes = LOG_FIND_TDES (LOG_FIND_THREAD_TRAN_INDEX (thread_p));
  MVCC_INFO *curr_mvcc_info;

  assert (tdes != NULL);

  curr_mvcc_info = &tdes->mvccinfo;
  if (curr_mvcc_info->id == mvccid)
    {
      return true;
    }
  else if (curr_mvcc_info->sub_ids.size () > 0)
    {
      for (size_t i = 0; i < curr_mvcc_info->sub_ids.size (); i++)
	{
	  if (curr_mvcc_info->sub_ids[i] == mvccid)
	    {
	      return true;
	    }
	}
    }

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
  LOG_TDES *tdes = LOG_FIND_TDES (LOG_FIND_THREAD_TRAN_INDEX (thread_p));

  if (!tdes->is_active_worker_transaction ())
    {
      /* System transactions do not have snapshots */
      return NULL;
    }

  assert (tdes != NULL);

  if (!tdes->mvccinfo.snapshot.valid)
    {
      log_Gl.mvcc_table.build_mvcc_info (*tdes);
    }

  return &tdes->mvccinfo.snapshot;

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
  MVCC_INFO *curr_mvcc_info = NULL;
  mvcctable *mvcc_table = &log_Gl.mvcc_table;
  MVCC_SNAPSHOT *p_mvcc_snapshot = NULL;
  MVCCID mvccid;
  int tran_index;
  TSC_TICKS start_tick, end_tick;
  TSCTIMEVAL tv_diff;
  UINT64 tran_complete_time;
  bool is_perf_tracking = false;

  assert (tdes != NULL);

  is_perf_tracking = perfmon_is_perf_tracking ();
  if (is_perf_tracking)
    {
      tsc_getticks (&start_tick);
    }

  curr_mvcc_info = &tdes->mvccinfo;
  mvccid = curr_mvcc_info->id;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);

  if (MVCCID_IS_VALID (mvccid))
    {
      mvcc_table->complete_mvcc (tran_index, mvccid, committed);
    }
  else
    {
      if (committed && logtb_tran_update_all_global_unique_stats (thread_p) != NO_ERROR)
	{
	  assert (false);
	}

      /* atomic set transaction lowest active MVCCID */
      log_Gl.mvcc_table.reset_transaction_lowest_active (tran_index);
    }

  curr_mvcc_info->recent_snapshot_lowest_active_mvccid = MVCCID_NULL;

  p_mvcc_snapshot = &(curr_mvcc_info->snapshot);
  if (p_mvcc_snapshot->valid)
    {
      logtb_tran_reset_count_optim_state (thread_p);
    }

  curr_mvcc_info->reset ();

  logtb_tran_clear_update_stats (&tdes->log_upd_stats);

  if (is_perf_tracking)
    {
      tsc_getticks (&end_tick);
      tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
      tran_complete_time = tv_diff.tv_sec * 1000000LL + tv_diff.tv_usec;
      if (tran_complete_time > 0)
	{
	  perfmon_add_stat (thread_p, PSTAT_LOG_TRAN_COMPLETE_TIME_COUNTERS, tran_complete_time);
	}
    }
}

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
  if (LOG_ISTRAN_2PC_PREPARE (tdes))
    {
      tdes->isloose_end = true;
      log_Gl.trantable.num_prepared_loose_end_indices++;
#if !defined(NDEBUG)
      if (prm_get_bool_value (PRM_ID_LOG_TRACE_DEBUG))
	{
	  fprintf (stdout,
		   "\n*** Transaction = %d (index = %d) is prepared to commit as gobal tran = %d\n"
		   "    The coordinator site (maybe the client user = %s) needs to attach\n"
		   "    to this transaction and either commit or abort it. ***\n", tdes->trid, tdes->tran_index,
		   tdes->gtrid, tdes->client.get_db_user ());
	  fflush (stdout);
	}
#endif
    }
  else if (LOG_ISTRAN_2PC_IN_SECOND_PHASE (tdes) || tdes->state == TRAN_UNACTIVE_2PC_COLLECTING_PARTICIPANT_VOTES)
    {
      tdes->isloose_end = true;
      log_Gl.trantable.num_coord_loose_end_indices++;
#if !defined(NDEBUG)
      if (prm_get_bool_value (PRM_ID_LOG_TRACE_DEBUG))
	{
	  fprintf (stdout,
		   "\n*** Transaction = %d (index = %d) needs to complete informing participants\n"
		   "    about its fate = %s and collect participant acknowledgements.\n"
		   "    This transaction has been disassociated from the client user = %s.\n"
		   "    The transaction will be completely finished by the system ***\n", tdes->trid,
		   tdes->tran_index, ((LOG_ISTRAN_COMMITTED (tdes)) ? "COMMIT" : "ABORT"), tdes->client.get_db_user ());
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

  r = (log_Gl.trantable.num_coord_loose_end_indices + log_Gl.trantable.num_prepared_loose_end_indices);

  TR_TABLE_CS_EXIT (thread_p);

  return r;
}

/*
 * logtb_rv_read_only_map_undo_tdes - map func to all tdes to abort in the UNOO recovery phase.
 */
void
logtb_rv_read_only_map_undo_tdes (THREAD_ENTRY * thread_p, const std::function < void (const log_tdes &) > map_func)
{
  int i;
  LOG_TDES *tdes;		/* Transaction descriptor */

  TR_TABLE_CS_ENTER_READ_MODE (thread_p);

  /* Check active transactions. */
  for (i = 0; i < NUM_TOTAL_TRAN_INDICES; i++)
    {
      if (i != LOG_SYSTEM_TRAN_INDEX)
	{
	  tdes = log_Gl.trantable.all_tdes[i];
	  if (tdes != NULL && tdes->trid != NULL_TRANID
	      && (tdes->state == TRAN_UNACTIVE_UNILATERALLY_ABORTED || tdes->state == TRAN_UNACTIVE_ABORTED))
	    {
	      map_func (*tdes);
	    }
	}
    }
  /* Check system worker transactions. */
  // *INDENT-OFF*
  log_system_tdes::map_all_tdes (map_func);
  // *INDENT-ON*

  TR_TABLE_CS_EXIT (thread_p);
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

	  if (tdes != NULL && tdes->trid != NULL_TRANID && !LSA_ISNULL (&tdes->head_lsa)
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
 * logtb_tran_prepare_count_optim_classes - prepare classes for count
 *					    optimization (for unique statistics
 *					    loading)
 *
 * return: error code
 *
 * thread_p(in): thread entry
 * classes(in): classes names list
 * flags(in): flags associated with class names
 * n_classes(in): number of classes names
 *
 * Note: This function is called at prefetch request. It receives a list of
 *	 classes and for each class the COS_TO_LOAD flag will be set if the
 *	 statistics were not already loaded. The statistics will be loaded at
 *	 snapshot.
 */
int
logtb_tran_prepare_count_optim_classes (THREAD_ENTRY * thread_p, const char **classes, LC_PREFETCH_FLAGS * flags,
					int n_classes)
{
  int idx;
  OID class_oid;
  LC_FIND_CLASSNAME find;
  LOG_TRAN_CLASS_COS *class_cos = NULL;

  for (idx = n_classes - 1; idx >= 0; idx--)
    {
      if (!(flags[idx] & LC_PREF_FLAG_COUNT_OPTIM))
	{
	  continue;
	}

      /* get class OID from class name */
      find = xlocator_find_class_oid (thread_p, classes[idx], &class_oid, NULL_LOCK);
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
	      class_cos = logtb_tran_find_class_cos (thread_p, &class_oid, true);
	      if (class_cos == NULL)
		{
		  /* something wrong happened. Just return error */
		  return ER_FAILED;
		}

	      /* Mark class for unique statistics loading. The statistics will be loaded when snapshot will be taken */
	      if (class_cos->count_state != COS_LOADED)
		{
		  class_cos->count_state = COS_TO_LOAD;
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
 * logtb_tran_reset_cos_func - working function for
 *			       logtb_tran_reset_count_optim_state
 *
 * return:
 * data(in): count optimization state entry.
 * args(in): not used.
 *
 * thread_p(in): thread entry
 */
static int
logtb_tran_reset_cos_func (THREAD_ENTRY * thread_p, void *data, void *args)
{
  ((LOG_TRAN_CLASS_COS *) data)->count_state = COS_NOT_LOADED;

  return NO_ERROR;
}

/*
 * logtb_tran_reset_count_optim_state - reset count optimization state for all
 *					class statistics instances
 *
 * return:
 *
 * thread_p(in): thread entry
 */
void
logtb_tran_reset_count_optim_state (THREAD_ENTRY * thread_p)
{
  LOG_TDES *tdes = LOG_FIND_TDES (LOG_FIND_THREAD_TRAN_INDEX (thread_p));

  mht_map_no_key (thread_p, tdes->log_upd_stats.classes_cos_hash, logtb_tran_reset_cos_func, NULL);
}

/*
 * logtb_create_unique_stats_from_repr - create unique statistics instances
 *					 for all unique indexes of a class
 *
 * return: error code
 *
 * thread_p(in)	  : thread entry
 * class_oid(in)  : class for which the unique statistics will be created
 */
static int
logtb_create_unique_stats_from_repr (THREAD_ENTRY * thread_p, OID * class_oid)
{
  OR_CLASSREP *classrepr = NULL;
  int error_code = NO_ERROR, idx, classrepr_cacheindex = -1;
  LOG_TRAN_BTID_UNIQUE_STATS *unique_stats = NULL;

  /* get class representation to find the total number of indexes */
  classrepr = heap_classrepr_get (thread_p, class_oid, NULL, NULL_REPRID, &classrepr_cacheindex);
  if (classrepr == NULL)
    {
      goto exit_on_error;
    }

  for (idx = classrepr->n_indexes - 1; idx >= 0; idx--)
    {
      if (btree_is_unique_type (classrepr->indexes[idx].type))
	{
	  unique_stats = logtb_tran_find_btid_stats (thread_p, &classrepr->indexes[idx].btid, true);
	  if (unique_stats == NULL)
	    {
	      error_code = ER_FAILED;
	      goto exit_on_error;
	    }
	  error_code =
	    logtb_get_global_unique_stats (thread_p, &unique_stats->btid, &unique_stats->global_stats.num_oids,
					   &unique_stats->global_stats.num_nulls, &unique_stats->global_stats.num_keys);
	  if (error_code != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	}
    }

  /* free class representation */
  heap_classrepr_free_and_init (classrepr, &classrepr_cacheindex);

  return NO_ERROR;

exit_on_error:
  if (classrepr != NULL)
    {
      heap_classrepr_free_and_init (classrepr, &classrepr_cacheindex);
    }

  return (error_code == NO_ERROR && (error_code = er_errid ()) == NO_ERROR) ? ER_FAILED : error_code;
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
  LOG_LSA *max_lsa = NULL;	/* The largest lsa value */

  TR_TABLE_CS_ENTER_READ_MODE (thread_p);
  for (i = 0; i < NUM_TOTAL_TRAN_INDICES; i++)
    {
      if (i != LOG_SYSTEM_TRAN_INDEX)
	{
	  tdes = log_Gl.trantable.all_tdes[i];
	  if (tdes != NULL && tdes->trid != NULL_TRANID && !LSA_ISNULL (&tdes->tail_lsa)
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
logtb_find_smallest_and_largest_active_pages (THREAD_ENTRY * thread_p, LOG_PAGEID * smallest, LOG_PAGEID * largest)
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
	  if (tdes != NULL && tdes->trid != NULL_TRANID && !LSA_ISNULL (&tdes->head_lsa))
	    {
	      if (*smallest == NULL_PAGEID || tdes->head_lsa.pageid < *smallest)
		{
		  *smallest = tdes->head_lsa.pageid;
		}
	      if (*largest == NULL_PAGEID || tdes->tail_lsa.pageid > *largest)
		{
		  *largest = tdes->tail_lsa.pageid;
		}
	      if (*largest == NULL_PAGEID || tdes->posp_nxlsa.pageid > *largest)
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
 *logtb_get_new_subtransaction_mvccid - assign a new sub-transaction MVCCID
 *
 * return: error code
 *
 *   thread_p(in): Thread entry
 *   curr_mvcc_info(in): current MVCC info
 *
 *  Note: If transaction MVCCID is NULL then a new transaction MVCCID is
 *    allocated first.
 */
void
logtb_get_new_subtransaction_mvccid (THREAD_ENTRY * thread_p, MVCC_INFO * curr_mvcc_info)
{
  MVCCID mvcc_subid;
  mvcctable *mvcc_table;

  assert (curr_mvcc_info != NULL);

  mvcc_table = &log_Gl.mvcc_table;

  // curr_mvcc_info->id must be valid too!
  if (MVCCID_IS_VALID (curr_mvcc_info->id))
    {
      mvcc_subid = mvcc_table->get_new_mvccid ();
    }
  else
    {
      mvcc_table->get_two_new_mvccid (curr_mvcc_info->id, mvcc_subid);
    }

  logtb_assign_subtransaction_mvccid (thread_p, curr_mvcc_info, mvcc_subid);
}

/*
 * logtb_assign_subtransaction_mvccid () - Assign sub-transaction MVCCID.
 *
 * return	       : Error code.
 * thread_p (in)       : Thread entry.
 * curr_mvcc_info (in) : Current transaction MVCC information.
 * mvcc_subid (in)     : Sub-transaction MVCCID.
 */
static void
logtb_assign_subtransaction_mvccid (THREAD_ENTRY * thread_p, MVCC_INFO * curr_mvcc_info, MVCCID mvcc_subid)
{
  assert (curr_mvcc_info != NULL);
  assert (MVCCID_IS_VALID (curr_mvcc_info->id));
  curr_mvcc_info->sub_ids.push_back (mvcc_subid);
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
  mvcctable *mvcc_table = &log_Gl.mvcc_table;

  assert (tdes != NULL);

  curr_mvcc_info = &tdes->mvccinfo;
  mvcc_sub_id = curr_mvcc_info->sub_ids.back ();

  mvcc_table->complete_sub_mvcc (mvcc_sub_id);
  curr_mvcc_info->sub_ids.pop_back ();

  if (tdes->mvccinfo.snapshot.valid)
    {
      /* adjust snapshot to reflect committed sub-transaction, since the parent transaction didn't finished yet */
      MVCC_SNAPSHOT *snapshot = &tdes->mvccinfo.snapshot;
      if (mvcc_sub_id >= snapshot->highest_completed_mvccid)
	{
	  snapshot->highest_completed_mvccid = mvcc_sub_id;
	  MVCCID_FORWARD (snapshot->highest_completed_mvccid);
	}
      snapshot->m_active_mvccs.set_inactive_mvccid (mvcc_sub_id);
    }
}

/*
 * logtb_global_unique_stat_alloc () - allocate a new structure of unique
 *				       statistics for a btree
 *   returns: new pointer or NULL on error
 */
static void *
logtb_global_unique_stat_alloc (void)
{
  GLOBAL_UNIQUE_STATS *unique_stat;

  unique_stat = (GLOBAL_UNIQUE_STATS *) malloc (sizeof (GLOBAL_UNIQUE_STATS));
  if (unique_stat == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (GLOBAL_UNIQUE_STATS));
      return NULL;
    }

  pthread_mutex_init (&unique_stat->mutex, NULL);
  return (void *) unique_stat;
}

/*
 * logtb_global_unique_stat_free () - free a global unique statistics of a btree
 *   returns: error code or NO_ERROR
 *   unique_stat(in): global unique statistics entry to free
 *		      (GLOBAL_UNIQUE_STATS)
 */
static int
logtb_global_unique_stat_free (void *unique_stat)
{
  if (unique_stat != NULL)
    {
      pthread_mutex_destroy (&((GLOBAL_UNIQUE_STATS *) unique_stat)->mutex);
      free (unique_stat);
      return NO_ERROR;
    }
  else
    {
      return ER_FAILED;
    }
}

/*
 * logtb_global_unique_stat_init () - initialize global unique statistics element
 *   returns: error code or NO_ERROR
 *   unique_stat(in): global unique statistics element
 */
static int
logtb_global_unique_stat_init (void *unique_stat)
{
  GLOBAL_UNIQUE_STATS *unique_stat_p = (GLOBAL_UNIQUE_STATS *) unique_stat;

  if (unique_stat == NULL)
    {
      return ER_FAILED;
    }

  /* initialize fields */
  BTID_SET_NULL (&unique_stat_p->btid);
  unique_stat_p->unique_stats.num_nulls = 0;
  unique_stat_p->unique_stats.num_keys = 0;
  unique_stat_p->unique_stats.num_oids = 0;
  LSA_SET_NULL (&unique_stat_p->last_log_lsa);

  return NO_ERROR;
}

/*
 * logtb_global_unique_stat_key_copy () - copy a global unique statistics key
 *   returns: error code or NO_ERROR
 *   src(in): source
 *   dest(in): destination
 */
static int
logtb_global_unique_stat_key_copy (void *src, void *dest)
{
  if (src == NULL || dest == NULL)
    {
      return ER_FAILED;
    }

  BTID_COPY ((BTID *) dest, (BTID *) src);

  /* all ok */
  return NO_ERROR;
}

/*
 * logtb_initialize_global_unique_stats_table () - Creates and initializes
 *						   global structure for global
 *						   unique statistics
 *   return: error code
 *   thread_p  (in) :
 */
int
logtb_initialize_global_unique_stats_table (THREAD_ENTRY * thread_p)
{
  int ret = NO_ERROR;
  LF_ENTRY_DESCRIPTOR *edesc = &log_Gl.unique_stats_table.unique_stats_descriptor;

  if (log_Gl.unique_stats_table.initialized)
    {
      return NO_ERROR;
    }
  edesc->of_local_next = offsetof (GLOBAL_UNIQUE_STATS, stack);
  edesc->of_next = offsetof (GLOBAL_UNIQUE_STATS, next);
  edesc->of_del_tran_id = offsetof (GLOBAL_UNIQUE_STATS, del_id);
  edesc->of_key = offsetof (GLOBAL_UNIQUE_STATS, btid);
  edesc->of_mutex = offsetof (GLOBAL_UNIQUE_STATS, mutex);
  edesc->using_mutex = LF_EM_USING_MUTEX;
  edesc->max_alloc_cnt = LF_ENTRY_DESCRIPTOR_MAX_ALLOC;
  edesc->f_alloc = logtb_global_unique_stat_alloc;
  edesc->f_free = logtb_global_unique_stat_free;
  edesc->f_init = logtb_global_unique_stat_init;
  edesc->f_uninit = NULL;
  edesc->f_key_copy = logtb_global_unique_stat_key_copy;
  edesc->f_key_cmp = btree_compare_btids;
  edesc->f_hash = btree_hash_btid;
  edesc->f_duplicate = NULL;

  /* initialize freelist */
  ret = lf_freelist_init (&log_Gl.unique_stats_table.unique_stats_freelist, 1, 100, edesc, &global_unique_stats_Ts);
  if (ret != NO_ERROR)
    {
      return ret;
    }

  /* initialize hash table */
  ret =
    lf_hash_init (&log_Gl.unique_stats_table.unique_stats_hash, &log_Gl.unique_stats_table.unique_stats_freelist,
		  GLOBAL_UNIQUE_STATS_HASH_SIZE, edesc);
  if (ret != NO_ERROR)
    {
      lf_hash_destroy (&log_Gl.unique_stats_table.unique_stats_hash);
      return ret;
    }

  LSA_SET_NULL (&log_Gl.unique_stats_table.curr_rcv_rec_lsa);
  log_Gl.unique_stats_table.initialized = true;

  return ret;
}

/*
 * logtb_finalize_global_unique_stats_table () - Finalize global structure for
 *						 global unique statistics
 *   return: error code
 *   thread_p  (in) :
 */
void
logtb_finalize_global_unique_stats_table (THREAD_ENTRY * thread_p)
{
  if (log_Gl.unique_stats_table.initialized)
    {
      /* destroy hash and freelist */
      lf_hash_destroy (&log_Gl.unique_stats_table.unique_stats_hash);
      lf_freelist_destroy (&log_Gl.unique_stats_table.unique_stats_freelist);

      log_Gl.unique_stats_table.initialized = false;
    }
}

/*
 * logtb_get_global_unique_stats_entry () - returns the entry into the global
 *					    unique statistics associated with
 *					    the given btid
 *   return: the entry associated with btid or NULL in case of error
 *   thread_p  (in) :
 *   btid (in) : the btree id for which the entry will be returned
 *   load_at_creation (in) : if true and there is no entry in hash for btid then
 *			     load statistics from btree header into hash
 *
 *    NOTE: The statistics are searched in the global hash. If they are found
 *	    then the found entry is returned. Otherwise a new entry will be
 *	    created and inserted into hash. If load_at_creation is true then the
 *	    statistics will be loaded from btree header.
 *
 *    NOTE: !!! DO NOT CALL THIS FUNCTION IF YOU HAVE A LATCH ON THE BTREE
 *          HEADER. THIS CAN CAUSE A DEADLOCK BETWEEN THE LATCH AND THE MUTEX !!!
 */
static GLOBAL_UNIQUE_STATS *
logtb_get_global_unique_stats_entry (THREAD_ENTRY * thread_p, BTID * btid, bool load_at_creation)
{
  int error_code = NO_ERROR;
  LF_TRAN_ENTRY *t_entry = thread_get_tran_entry (thread_p, THREAD_TS_GLOBAL_UNIQUE_STATS);
  GLOBAL_UNIQUE_STATS *stats = NULL;
  long long num_oids, num_nulls, num_keys;

  assert (btid != NULL);

#if !defined(NDEBUG)
  {
    VPID root_vpid;
    root_vpid.pageid = btid->root_pageid;
    root_vpid.volid = btid->vfid.volid;
    assert (!pgbuf_is_page_fixed_by_thread (thread_p, &root_vpid));
  }
#endif

  error_code = lf_hash_find (t_entry, &log_Gl.unique_stats_table.unique_stats_hash, btid, (void **) &stats);
  if (error_code != NO_ERROR)
    {
      return NULL;
    }
  if (stats == NULL)
    {
      if (load_at_creation)
	{
	  error_code = btree_get_unique_statistics (thread_p, btid, &num_oids, &num_nulls, &num_keys);
	  if (error_code != NO_ERROR)
	    {
	      return NULL;
	    }
	}
      error_code =
	lf_hash_find_or_insert (t_entry, &log_Gl.unique_stats_table.unique_stats_hash, btid, (void **) &stats, NULL);
      if (error_code != NO_ERROR || stats == NULL)
	{
	  return NULL;
	}
      if (load_at_creation)
	{
	  stats->unique_stats.num_oids = num_oids;
	  stats->unique_stats.num_nulls = num_nulls;
	  stats->unique_stats.num_keys = num_keys;
	}
    }

  return stats;
}

/*
 * logtb_get_global_unique_stats () - returns the global unique statistics for
 *				      the given btid
 *   return: error code
 *   thread_p  (in) :
 *   btid (in) : the btree id for which the statistics will be returned
 *   num_oids (in) : address of an integer that will receive the global number
 *		     of oids for the given btid
 *   num_nulls (in) : address of an integer that will receive the global number
 *		     of nulls for the given btid
 *   num_keys (in) : address of an integer that will receive the global number
 *		     of keys for the given btid
 */
int
logtb_get_global_unique_stats (THREAD_ENTRY * thread_p, BTID * btid, long long *num_oids, long long *num_nulls,
			       long long *num_keys)
{
  int error_code = NO_ERROR;
  GLOBAL_UNIQUE_STATS *stats = NULL;

  assert (btid != NULL);

  stats = logtb_get_global_unique_stats_entry (thread_p, btid, true);
  if (stats == NULL)
    {
      return ER_FAILED;
    }

  *num_oids = stats->unique_stats.num_oids;
  *num_nulls = stats->unique_stats.num_nulls;
  *num_keys = stats->unique_stats.num_keys;

  pthread_mutex_unlock (&stats->mutex);

  return error_code;
}

/*
 * logtb_rv_update_global_unique_stats_by_abs () - updates the global unique
 *						   statistics associated with
 *						   the given btid by absolute
 *						   values. used for recovery.
 *   return: error code
 *   thread_p  (in) :
 *   btid (in) : the btree id for which the statistics will be updated
 *   num_oids (in) : the new number of oids
 *   num_nulls (in) : the new number of nulls
 *   num_keys (in) : the new number of keys
 */
int
logtb_rv_update_global_unique_stats_by_abs (THREAD_ENTRY * thread_p, BTID * btid, long long num_oids,
					    long long num_nulls, long long num_keys)
{
  int error_code = NO_ERROR;
  GLOBAL_UNIQUE_STATS *stats = NULL;

  /* Because we update the statistics with absolute values (this means that we override old values) we don't need to
   * load from btree header old values and, therefore, we give a 'false' value to 'load_at_creation' parameter */
  stats = logtb_get_global_unique_stats_entry (thread_p, btid, false);
  if (stats == NULL)
    {
      return ER_FAILED;
    }

  if (!LSA_ISNULL (&log_Gl.unique_stats_table.curr_rcv_rec_lsa))
    {
      /* Here we assume that we are at recovery stage */
      LSA_COPY (&stats->last_log_lsa, &log_Gl.unique_stats_table.curr_rcv_rec_lsa);
    }

  if (prm_get_bool_value (PRM_ID_LOG_UNIQUE_STATS))
    {
      _er_log_debug (ARG_FILE_LINE,
		     "Update stats for index (%d, %d|%d) to nulls=%d, oids=%d, keys=%d. LSA=%lld|%d.\n",
		     btid->root_pageid, btid->vfid.volid, btid->vfid.fileid, num_nulls, num_oids, num_keys,
		     (long long int) stats->last_log_lsa.pageid, (int) stats->last_log_lsa.offset);
    }

  stats->unique_stats.num_oids = num_oids;
  stats->unique_stats.num_nulls = num_nulls;
  stats->unique_stats.num_keys = num_keys;

  pthread_mutex_unlock (&stats->mutex);

  return error_code;
}

/*
 * logtb_update_global_unique_stats_by_delta () - updates the global unique
 *						  statistics associated with the
 *						  given btid by delta values
 *   return: error code
 *   thread_p  (in) :
 *   btid (in) : the btree id for which the statistics will be updated
 *   oid_delta (in) : the delta of oids that will be added
 *   null_delta (in) : the delta of nulls that will be added
 *   key_delta (in) : the delta of keys that will be added
 *   log (in) : true if we need to log the changes
 */
int
logtb_update_global_unique_stats_by_delta (THREAD_ENTRY * thread_p, BTID * btid, long long oid_delta,
					   long long null_delta, long long key_delta, bool log)
{
  int error_code = NO_ERROR;
  GLOBAL_UNIQUE_STATS *stats = NULL;
  LOG_TDES *tdes = LOG_FIND_CURRENT_TDES (thread_p);
  long long num_oids, num_nulls, num_keys;

  if (oid_delta == 0 && key_delta == 0 && null_delta == 0)
    {
      return NO_ERROR;
    }

  stats = logtb_get_global_unique_stats_entry (thread_p, btid, true);
  if (stats == NULL)
    {
      return ER_FAILED;
    }

  num_oids = stats->unique_stats.num_oids + oid_delta;
  num_nulls = stats->unique_stats.num_nulls + null_delta;
  num_keys = stats->unique_stats.num_keys + key_delta;

  if (log)
    {
      RECDES undo_rec, redo_rec;
      char undo_rec_buf[(3 * OR_BIGINT_SIZE) + OR_BTID_ALIGNED_SIZE + BTREE_MAX_ALIGN], *datap = NULL;
      char redo_rec_buf[(3 * OR_BIGINT_SIZE) + OR_BTID_ALIGNED_SIZE + BTREE_MAX_ALIGN];

      /* although we don't change the btree header, we still need to log here the new values of statistics so that they
       * can be recovered at recover stage. For undo purposes we log the increments. */
      undo_rec.data = NULL;
      undo_rec.area_size = 3 * OR_BIGINT_SIZE + OR_BTID_ALIGNED_SIZE;
      undo_rec.data = PTR_ALIGN (undo_rec_buf, BTREE_MAX_ALIGN);

      undo_rec.length = 0;
      datap = (char *) undo_rec.data;
      OR_PUT_BTID (datap, btid);
      datap += OR_BTID_ALIGNED_SIZE;
      OR_PUT_BIGINT (datap, &null_delta);
      datap += OR_BIGINT_SIZE;
      OR_PUT_BIGINT (datap, &oid_delta);
      datap += OR_BIGINT_SIZE;
      OR_PUT_BIGINT (datap, &key_delta);
      datap += OR_BIGINT_SIZE;
      undo_rec.length = CAST_BUFLEN (datap - undo_rec.data);

      redo_rec.data = NULL;
      redo_rec.area_size = 3 * OR_BIGINT_SIZE + OR_BTID_ALIGNED_SIZE;
      redo_rec.data = PTR_ALIGN (redo_rec_buf, BTREE_MAX_ALIGN);

      redo_rec.length = 0;
      datap = (char *) redo_rec.data;
      OR_PUT_BTID (datap, btid);
      datap += OR_BTID_ALIGNED_SIZE;
      OR_PUT_BIGINT (datap, &num_nulls);
      datap += OR_BIGINT_SIZE;
      OR_PUT_BIGINT (datap, &num_oids);
      datap += OR_BIGINT_SIZE;
      OR_PUT_BIGINT (datap, &num_keys);
      datap += OR_BIGINT_SIZE;
      redo_rec.length = CAST_BUFLEN (datap - redo_rec.data);

      log_append_undoredo_data2 (thread_p, RVBT_LOG_GLOBAL_UNIQUE_STATS_COMMIT, NULL, NULL, HEADER, undo_rec.length,
				 redo_rec.length, undo_rec.data, redo_rec.data);
      LSA_COPY (&stats->last_log_lsa, &tdes->tail_lsa);
    }
  else if (!LSA_ISNULL (&log_Gl.unique_stats_table.curr_rcv_rec_lsa))
    {
      /* Here we assume that we are at recovery stage */
      LSA_COPY (&stats->last_log_lsa, &log_Gl.unique_stats_table.curr_rcv_rec_lsa);
    }

  if (prm_get_bool_value (PRM_ID_LOG_UNIQUE_STATS))
    {
      _er_log_debug (ARG_FILE_LINE,
		     "Update stats for index (%d, %d|%d) by nulls=%lld, "
		     "oids=%lld, keys=%lld to nulls=%lld, oids=%lld, keys=%lld. LSA=%lld|%d.\n", btid->root_pageid,
		     btid->vfid.volid, btid->vfid.fileid, null_delta, oid_delta, key_delta, num_nulls, num_oids,
		     num_keys, (long long int) stats->last_log_lsa.pageid, (int) stats->last_log_lsa.offset);
    }

  stats->unique_stats.num_oids = num_oids;
  stats->unique_stats.num_nulls = num_nulls;
  stats->unique_stats.num_keys = num_keys;

  pthread_mutex_unlock (&stats->mutex);

  return error_code;
}

/*
 * logtb_delete_global_unique_stats () - deletes the entry associated with
 *					 the given btid from global unique
 *					 statistics hash
 *   return: error code
 *   thread_p  (in) :
 *   btid (in) : the btree id for which the statistics entry will be deleted
 */
int
logtb_delete_global_unique_stats (THREAD_ENTRY * thread_p, BTID * btid)
{
  LF_TRAN_ENTRY *t_entry = thread_get_tran_entry (thread_p, THREAD_TS_GLOBAL_UNIQUE_STATS);
  int error = NO_ERROR;

  assert (!BTID_IS_NULL (btid));

#if !defined(NDEBUG)
  {
    VPID root_vpid;
    root_vpid.pageid = btid->root_pageid;
    root_vpid.volid = btid->vfid.volid;
    assert (!pgbuf_is_page_fixed_by_thread (thread_p, &root_vpid));
  }
#endif

  error = lf_hash_delete (t_entry, &log_Gl.unique_stats_table.unique_stats_hash, btid, NULL);
  if (error != NO_ERROR)
    {
      return error;
    }

  return NO_ERROR;
}

/*
 * logtb_reflect_global_unique_stats_to_btree () - reflects the global
 *						   statistics into the btree
 *						   header
 *   return: error code
 *   thread_p  (in) :
 */
int
logtb_reflect_global_unique_stats_to_btree (THREAD_ENTRY * thread_p)
{
  int error = NO_ERROR;
  LF_HASH_TABLE_ITERATOR it;
  LF_TRAN_ENTRY *t_entry = thread_get_tran_entry (thread_p, THREAD_TS_GLOBAL_UNIQUE_STATS);
  GLOBAL_UNIQUE_STATS *stats = NULL;

  if (!log_Gl.unique_stats_table.initialized)
    {
      return NO_ERROR;
    }

  // reflecting stats should not be interrupted
  bool save_check_interrupt = logtb_set_check_interrupt (thread_p, false);

  lf_hash_create_iterator (&it, t_entry, &log_Gl.unique_stats_table.unique_stats_hash);
  for (stats = (GLOBAL_UNIQUE_STATS *) lf_hash_iterate (&it); stats != NULL;
       stats = (GLOBAL_UNIQUE_STATS *) lf_hash_iterate (&it))
    {
      /* reflect only if some changes were logged */
      if (log_is_no_logging () || !LSA_ISNULL (&stats->last_log_lsa))
	{
	  error = btree_reflect_global_unique_statistics (thread_p, stats, false);
	  if (error != NO_ERROR)
	    {
	      ASSERT_ERROR ();

	      // must unlock entry
	      pthread_mutex_unlock (&stats->mutex);

	      // finish transaction
	      lf_tran_end_with_mb (t_entry);
	      break;
	    }
	  LSA_SET_NULL (&stats->last_log_lsa);
	}
    }

  (void) logtb_set_check_interrupt (thread_p, save_check_interrupt);

  return error;
}

/*
 * logtb_does_active_user_exist - check whether the specified user is active user
 * 			or not. active user means it is in trantable now.
 *
 * return: true for existed
 *    thread_p(in):
 *    user_name(in): the specified user name
 *
 */
bool
xlogtb_does_active_user_exist (THREAD_ENTRY * thread_p, const char *user_name)
{
  int i;
  LOG_TDES *tdes;		/* Transaction descriptor */
  bool existed = false;

  TR_TABLE_CS_ENTER_READ_MODE (thread_p);

  for (i = 0; i < NUM_TOTAL_TRAN_INDICES; i++)
    {
      tdes = log_Gl.trantable.all_tdes[i];
      if (tdes != NULL && tdes->is_user_active && strcmp (tdes->client.get_db_user (), user_name) == 0)
	{
	  existed = true;
	  break;
	}
    }
  TR_TABLE_CS_EXIT (thread_p);

  return existed;
}

#if !defined (NDEBUG) && !defined (WINDOWS)
int
logtb_collect_local_clients (int **local_clients_pids)
{
  LOG_TDES *tdes;		/* Transaction descriptor */
  int i, num_client;
  int *table;

  *local_clients_pids = NULL;

  table = (int *) malloc (NUM_TOTAL_TRAN_INDICES * sizeof (int));
  if (table == NULL)
    {
      return ER_FAILED;
    }

  memset (table, 0, NUM_TOTAL_TRAN_INDICES * sizeof (int));

  for (i = 0, num_client = 0; i < NUM_TOTAL_TRAN_INDICES; i++)
    {
      tdes = log_Gl.trantable.all_tdes[i];
      if (tdes != NULL && tdes->client.process_id > 0 && !tdes->client.host_name.empty ()
	  && (strcmp (tdes->client.get_host_name (), boot_Host_name) == 0
	      || strcmp (tdes->client.get_host_name (), "localhost") == 0))
	{
	  table[num_client++] = tdes->client.process_id;
	}
    }

  *local_clients_pids = table;
  return num_client;
}
#endif /* !defined (NDEBUG) && !defined (WINDOWS) */

/*
 * logtb_descriptors_start_scan () -  start scan function for tran descriptors
 *   return: NO_ERROR, or ER_code
 *
 *   thread_p(in):
 *   type(in):
 *   arg_values(in):
 *   arg_cnt(in):
 *   ptr(in/out):
 */
int
logtb_descriptors_start_scan (THREAD_ENTRY * thread_p, int type, DB_VALUE ** arg_values, int arg_cnt, void **ptr)
{
  SHOWSTMT_ARRAY_CONTEXT *ctx = NULL;
  int i, idx, msecs, error = NO_ERROR;
  char buf[512];
  const char *str;
  time_t tval;
  INT64 i64val;
  XASL_ID xasl_val;
  DB_DATETIME time_val;
  void *ptr_val;
  LOG_TDES *tdes;
  DB_VALUE *vals = NULL;
  const int num_cols = 46;

  *ptr = NULL;

  ctx = showstmt_alloc_array_context (thread_p, NUM_TOTAL_TRAN_INDICES, num_cols);
  if (ctx == NULL)
    {
      error = er_errid ();
      return error;
    }

  TR_TABLE_CS_ENTER_READ_MODE (thread_p);

  for (i = 0; i < NUM_TOTAL_TRAN_INDICES; i++)
    {
      tdes = log_Gl.trantable.all_tdes[i];
      if (tdes == NULL || tdes->trid == NULL_TRANID)
	{
	  /* The index is not assigned or is system transaction (no-client) */
	  continue;
	}

      idx = 0;
      vals = showstmt_alloc_tuple_in_context (thread_p, ctx);
      if (vals == NULL)
	{
	  error = er_errid ();
	  goto exit_on_error;
	}

      /* Tran_index */
      db_make_int (&vals[idx], tdes->tran_index);
      idx++;

      /* Tran_id */
      db_make_int (&vals[idx], tdes->trid);
      idx++;

      /* Is_loose_end */
      db_make_int (&vals[idx], tdes->isloose_end);
      idx++;

      /* State */
      db_make_string (&vals[idx], log_state_short_string (tdes->state));
      idx++;

      /* isolation */
      db_make_string (&vals[idx], log_isolation_string (tdes->isolation));
      idx++;

      /* Wait_msecs */
      db_make_int (&vals[idx], tdes->wait_msecs);
      idx++;

      /* Head_lsa */
      lsa_to_string (buf, sizeof (buf), &tdes->head_lsa);
      error = db_make_string_copy (&vals[idx], buf);
      idx++;
      if (error != NO_ERROR)
	{
	  goto exit_on_error;
	}

      /* Tail_lsa */
      lsa_to_string (buf, sizeof (buf), &tdes->tail_lsa);
      error = db_make_string_copy (&vals[idx], buf);
      idx++;
      if (error != NO_ERROR)
	{
	  goto exit_on_error;
	}

      /* Undo_next_lsa */
      lsa_to_string (buf, sizeof (buf), &tdes->undo_nxlsa);
      error = db_make_string_copy (&vals[idx], buf);
      idx++;
      if (error != NO_ERROR)
	{
	  goto exit_on_error;
	}

      /* Postpone_next_lsa */
      lsa_to_string (buf, sizeof (buf), &tdes->posp_nxlsa);
      error = db_make_string_copy (&vals[idx], buf);
      idx++;
      if (error != NO_ERROR)
	{
	  goto exit_on_error;
	}

      /* Savepoint_lsa */
      lsa_to_string (buf, sizeof (buf), &tdes->savept_lsa);
      error = db_make_string_copy (&vals[idx], buf);
      idx++;
      if (error != NO_ERROR)
	{
	  goto exit_on_error;
	}

      /* Topop_lsa */
      lsa_to_string (buf, sizeof (buf), &tdes->topop_lsa);
      error = db_make_string_copy (&vals[idx], buf);
      idx++;
      if (error != NO_ERROR)
	{
	  goto exit_on_error;
	}

      /* Tail_top_result_lsa */
      lsa_to_string (buf, sizeof (buf), &tdes->tail_topresult_lsa);
      error = db_make_string_copy (&vals[idx], buf);
      idx++;
      if (error != NO_ERROR)
	{
	  goto exit_on_error;
	}

      /* Client_id */
      db_make_int (&vals[idx], tdes->client_id);
      idx++;

      /* Client_type */
      str = boot_client_type_to_string ((BOOT_CLIENT_TYPE) tdes->client.client_type);
      error = db_make_string_copy (&vals[idx], str);
      idx++;
      if (error != NO_ERROR)
	{
	  goto exit_on_error;
	}

      /* Client_info */
      error = db_make_string_copy (&vals[idx], tdes->client.get_client_info ());
      idx++;
      if (error != NO_ERROR)
	{
	  goto exit_on_error;
	}

      /* Client_db_user */
      error = db_make_string_copy (&vals[idx], tdes->client.get_db_user ());
      idx++;
      if (error != NO_ERROR)
	{
	  goto exit_on_error;
	}

      /* Client_program */
      error = db_make_string_copy (&vals[idx], tdes->client.get_program_name ());
      idx++;
      if (error != NO_ERROR)
	{
	  goto exit_on_error;
	}

      /* Client_login_user */
      error = db_make_string_copy (&vals[idx], tdes->client.get_login_name ());
      idx++;
      if (error != NO_ERROR)
	{
	  goto exit_on_error;
	}

      /* Client_host */
      error = db_make_string_copy (&vals[idx], tdes->client.get_host_name ());
      idx++;
      if (error != NO_ERROR)
	{
	  goto exit_on_error;
	}

      /* Client_pid */
      db_make_int (&vals[idx], tdes->client.process_id);
      idx++;

      /* Topop_depth */
      db_make_int (&vals[idx], tdes->topops.last + 1);
      idx++;

      /* Num_unique_btrees */
      db_make_int (&vals[idx], tdes->num_unique_btrees);
      idx++;

      /* Max_unique_btrees */
      db_make_int (&vals[idx], tdes->max_unique_btrees);
      idx++;

      /* Interrupt */
      db_make_int (&vals[idx], tdes->interrupt);
      idx++;

      /* Num_transient_classnames */
      db_make_int (&vals[idx], tdes->num_transient_classnames);
      idx++;

      /* Repl_max_records */
      db_make_int (&vals[idx], tdes->num_repl_records);
      idx++;

      /* Repl_records */
      ptr_val = tdes->repl_records;
      if (ptr_val == NULL)
	{
	  db_make_null (&vals[idx]);
	}
      else
	{
	  snprintf (buf, sizeof (buf), "0x%08" PRIx64, (UINT64) ptr_val);
	  error = db_make_string_copy (&vals[idx], buf);
	  if (error != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	}
      idx++;

      /* Repl_current_index */
      db_make_int (&vals[idx], tdes->cur_repl_record);
      idx++;

      /* Repl_append_index */
      db_make_int (&vals[idx], tdes->append_repl_recidx);
      idx++;

      /* Repl_flush_marked_index */
      db_make_int (&vals[idx], tdes->fl_mark_repl_recidx);
      idx++;

      /* Repl_insert_lsa */
      lsa_to_string (buf, sizeof (buf), &tdes->repl_insert_lsa);
      error = db_make_string_copy (&vals[idx], buf);
      idx++;
      if (error != NO_ERROR)
	{
	  goto exit_on_error;
	}

      /* Repl_update_lsa */
      lsa_to_string (buf, sizeof (buf), &tdes->repl_update_lsa);
      error = db_make_string_copy (&vals[idx], buf);
      idx++;
      if (error != NO_ERROR)
	{
	  goto exit_on_error;
	}

      /* First_save_entry */
      ptr_val = tdes->first_save_entry;
      if (ptr_val == NULL)
	{
	  db_make_null (&vals[idx]);
	}
      else
	{
	  snprintf (buf, sizeof (buf), "0x%08" PRIx64, (UINT64) ptr_val);
	  error = db_make_string_copy (&vals[idx], buf);
	  if (error != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	}
      idx++;

      /* Tran_unique_stats */
      if (tdes->m_multiupd_stats.empty ())
	{
	  db_make_null (&vals[idx]);
	}
      else
	{
	  string_buffer strbuf (cubmem::PRIVATE_BLOCK_ALLOCATOR);
	  tdes->m_multiupd_stats.to_string (strbuf);
	  error = db_make_string (&vals[idx], strbuf.release_ptr ());
	  vals[idx].need_clear = true;
	  if (error != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	}
      idx++;

      /* modified class list */
      if (tdes->m_modified_classes.empty ())
	{
	  db_make_null (&vals[idx]);
	}
      else
	{
	  (void) db_make_string (&vals[idx], tdes->m_modified_classes.to_string ());
	  vals[idx].need_clear = true;
	}
      idx++;

      /* Num_temp_files */
      db_make_int (&vals[idx], file_get_tran_num_temp_files (thread_p));
      idx++;

      /* Waiting_for_res */
      ptr_val = tdes->waiting_for_res;
      if (ptr_val == NULL)
	{
	  db_make_null (&vals[idx]);
	}
      else
	{
	  snprintf (buf, sizeof (buf), "0x%08" PRIx64, (UINT64) ptr_val);
	  error = db_make_string_copy (&vals[idx], buf);
	  if (error != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	}
      idx++;

      /* Has_deadlock_priority */
      db_make_int (&vals[idx], tdes->has_deadlock_priority);
      idx++;

      /* Suppress_replication */
      db_make_int (&vals[idx], tdes->suppress_replication);
      idx++;

      /* Query_timeout */
      i64val = tdes->query_timeout;
      if (i64val <= 0)
	{
	  db_make_null (&vals[idx]);
	}
      else
	{
	  tval = i64val / 1000;
	  msecs = i64val % 1000;
	  db_localdatetime_msec (&tval, msecs, &time_val);
	  db_make_datetime (&vals[idx], &time_val);
	}
      idx++;

      /* Query_start_time */
      i64val = tdes->query_start_time;
      if (i64val <= 0)
	{
	  db_make_null (&vals[idx]);
	}
      else
	{
	  tval = i64val / 1000;
	  msecs = i64val % 1000;
	  db_localdatetime_msec (&tval, msecs, &time_val);
	  db_make_datetime (&vals[idx], &time_val);
	}
      idx++;

      /* Tran_start_time */
      i64val = tdes->tran_start_time;
      if (i64val <= 0)
	{
	  db_make_null (&vals[idx]);
	}
      else
	{
	  tval = i64val / 1000;
	  msecs = i64val % 1000;
	  db_localdatetime_msec (&tval, msecs, &time_val);
	  db_make_datetime (&vals[idx], &time_val);
	}
      idx++;

      /* Xasl_id */
      XASL_ID_COPY (&xasl_val, &tdes->xasl_id);
      if (XASL_ID_IS_NULL (&xasl_val))
	{
	  db_make_null (&vals[idx]);
	}
      else
	{
	  snprintf (buf, sizeof (buf), "sha1 = %08x | %08x | %08x | %08x | %08x, time_stored = %d sec %d usec",
		    SHA1_AS_ARGS (&xasl_val.sha1), CACHE_TIME_AS_ARGS (&xasl_val.time_stored));
	  error = db_make_string_copy (&vals[idx], buf);
	  if (error != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	}
      idx++;

      /* Disable_modifications */
      db_make_int (&vals[idx], tdes->disable_modifications);
      idx++;

      /* Abort_reason */
      str = tran_abort_reason_to_string (tdes->tran_abort_reason);
      db_make_string (&vals[idx], str);
      idx++;

      assert (idx == num_cols);
    }


  TR_TABLE_CS_EXIT (thread_p);

  *ptr = ctx;
  return NO_ERROR;

exit_on_error:
  TR_TABLE_CS_EXIT (thread_p);

  if (ctx != NULL)
    {
      showstmt_free_array_context (thread_p, ctx);
    }

  return error;
}

/*
 * tran_abort_reason_to_string() - return the string alias of enum value
 *
 *   return: constant string
 *
 *   val(in): the enum value
 */
const char *
tran_abort_reason_to_string (TRAN_ABORT_REASON val)
{
  switch (val)
    {
    case TRAN_NORMAL:
      return "NORMAL";
    case TRAN_ABORT_DUE_DEADLOCK:
      return "ABORT_DUE_DEADLOCK";
    case TRAN_ABORT_DUE_ROLLBACK_ON_ESCALATION:
      return "ABORT_DUE_ROLLBACK_ON_ESCALATION";
    }
  return "UNKNOWN";
}

/*
 * logtb_check_class_for_rr_isolation_err () - Check if the class have to be checked against serializable conflicts
 *
 * return		   : true if the class is not root/trigger/user class, otherwise false
 * class_oid (in)	   : Class object identifier.
 *
 * Note: Do not check system classes that are not part of catalog for rr isolation level error. Isolation consistency
 *	 is secured using locks anyway. These classes are in a way related to table schema's and can be accessed
 *	 before the actual classes. db_user instances are fetched to check authorizations, while db_root and db_trigger
 *	 are accessed when triggers are modified.
 *	 The RR isolation has to check if an instance that we want to lock was modified by concurrent transaction.
 *	 If the instance was modified, then this means we have an isolation conflict. The check must verify last
 *	 instance version visibility over transaction snapshot. The version is visible if and only if it was not
 *	 modified by concurrent transaction. To check visibility, we must first generate a transaction snapshot.
 *	 Since instances from these classes are accessed before locking tables, the snapshot is generated before
 *	 transaction is blocked on table lock. The results will then seem to be inconsistent with most cases when table
 *	 locks are acquired before snapshot.
 */
bool
logtb_check_class_for_rr_isolation_err (const OID * class_oid)
{
  assert (class_oid != NULL && !OID_ISNULL (class_oid));

  if (!oid_check_cached_class_oid (OID_CACHE_DB_ROOT_CLASS_ID, class_oid)
      && !oid_check_cached_class_oid (OID_CACHE_USER_CLASS_ID, class_oid)
      && !oid_check_cached_class_oid (OID_CACHE_TRIGGER_CLASS_ID, class_oid))
    {
      return true;
    }

  return false;
}

void
logtb_slam_transaction (THREAD_ENTRY * thread_p, int tran_index)
{
  logtb_set_tran_index_interrupt (thread_p, tran_index, true);
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_CONN_SHUTDOWN, 0);
#if defined (SERVER_MODE)
  css_shutdown_conn_by_tran_index (tran_index);
#endif // SERVER_MODE
}

/*
 * logtb_check_kill_tran_auth () - User who is not DBA can kill only own transaction
 *   return: NO_ERROR or error code
 *   thread_p(in):
 *   tran_id(in):
 *   has_authorization(out):
 */
static int
logtb_check_kill_tran_auth (THREAD_ENTRY * thread_p, int tran_id, bool * has_authorization)
{
  const char *tran_client_name;
  const char *current_client_name;

  assert (has_authorization);

  *has_authorization = false;

  if (logtb_am_i_dba_client (thread_p) == true)
    {
      *has_authorization = true;
      return NO_ERROR;
    }

  tran_client_name = logtb_find_client_name (tran_id);
  current_client_name = logtb_find_current_client_name (thread_p);

  if (tran_client_name == NULL || current_client_name == NULL)
    {
      return ER_CSS_KILL_UNKNOWN_TRANSACTION;
    }

  if (strcasecmp (tran_client_name, current_client_name) == 0)
    {
      *has_authorization = true;
    }

  return NO_ERROR;
}

/*
 * xlogtb_kill_tran_index() - Kill given transaction.
 *   return:
 *   kill_tran_index(in):
 *   kill_user(in):
 *   kill_host(in):
 *   kill_pid(id):
 */
int
xlogtb_kill_tran_index (THREAD_ENTRY * thread_p, int kill_tran_index, char *kill_user_p, char *kill_host_p,
			int kill_pid)
{
  const char *slam_progname_p;	/* Client program name for tran */
  const char *slam_user_p;	/* Client user name for tran */
  const char *slam_host_p;	/* Client host for tran */
  int slam_pid;			/* Client process id for tran */
  bool signaled = false;
  int error_code = NO_ERROR;
  bool killed = false;
  size_t i;

  if (kill_tran_index == NULL_TRAN_INDEX || kill_user_p == NULL || kill_host_p == NULL || strcmp (kill_user_p, "") == 0
      || strcmp (kill_host_p, "") == 0)
    {
      /*
       * Not enough information to kill specific transaction..
       *
       * For now.. I am setting an er_set..since I have so many files out..and
       * I cannot compile more junk..
       */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_KILL_BAD_INTERFACE, 0);
      return ER_CSS_KILL_BAD_INTERFACE;
    }

  if (kill_tran_index == LOG_SYSTEM_TRAN_INDEX)
    {
      // cannot kill system transaction; not even if this is dba
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_KILL_TR_NOT_ALLOWED, 1, kill_tran_index);
      return ER_KILL_TR_NOT_ALLOWED;
    }

  signaled = false;
  for (i = 0; i < LOGTB_RETRY_SLAM_MAX_TIMES && error_code == NO_ERROR && !killed; i++)
    {
      if (logtb_find_client_name_host_pid (kill_tran_index, &slam_progname_p, &slam_user_p, &slam_host_p, &slam_pid) !=
	  NO_ERROR)
	{
	  if (signaled == false)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_KILL_UNKNOWN_TRANSACTION, 4, kill_tran_index,
		      kill_user_p, kill_host_p, kill_pid);
	      error_code = ER_CSS_KILL_UNKNOWN_TRANSACTION;
	    }
	  else
	    {
	      killed = true;
	    }
	  break;
	}

      if (kill_pid == slam_pid && strcmp (kill_user_p, slam_user_p) == 0 && strcmp (kill_host_p, slam_host_p) == 0)
	{
	  logtb_slam_transaction (thread_p, kill_tran_index);
	  signaled = true;
	}
      else
	{
	  if (signaled == false)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_KILL_DOES_NOTMATCH, 8, kill_tran_index, kill_user_p,
		      kill_host_p, kill_pid, kill_tran_index, slam_user_p, slam_host_p, slam_pid);
	      error_code = ER_CSS_KILL_DOES_NOTMATCH;
	    }
	  else
	    {
	      killed = true;
	    }
	  break;
	}
      thread_sleep_for (std::chrono::seconds (1));
    }

  if (error_code == NO_ERROR && !killed)
    {
      error_code = ER_FAILED;	/* timeout */
    }

  return error_code;
}

/*
 * xlogtb_kill_or_interrupt_tran() -
 *   return:
 *   thread_p(in):
 *   tran_index(in):
 *   is_dba_group_member(in):
 *   kill_query_only(in):
 */
int
xlogtb_kill_or_interrupt_tran (THREAD_ENTRY * thread_p, int tran_index, bool is_dba_group_member, bool interrupt_only)
{
  int error;
  bool interrupt, has_authorization;
  bool is_trx_exists;
  KILLSTMT_TYPE kill_type;
  size_t i;

  if (tran_index == LOG_SYSTEM_TRAN_INDEX)
    {
      // cannot kill system transaction; not even if this is dba
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_KILL_TR_NOT_ALLOWED, 1, tran_index);
      return ER_KILL_TR_NOT_ALLOWED;
    }

  if (!is_dba_group_member)
    {
      error = logtb_check_kill_tran_auth (thread_p, tran_index, &has_authorization);
      if (error != NO_ERROR)
	{
	  return error;
	}

      if (has_authorization == false)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_KILL_TR_NOT_ALLOWED, 1, tran_index);
	  return ER_KILL_TR_NOT_ALLOWED;
	}
    }

  is_trx_exists = logtb_set_tran_index_interrupt (thread_p, tran_index, true);

  kill_type = interrupt_only ? KILLSTMT_QUERY : KILLSTMT_TRAN;
  if (kill_type == KILLSTMT_TRAN)
    {
#if defined (SERVER_MODE)
      css_shutdown_conn_by_tran_index (tran_index);
#endif // SERVER_MODE
    }

  for (i = 0; i < LOGTB_RETRY_SLAM_MAX_TIMES; i++)
    {
      thread_sleep_for (std::chrono::seconds (1));

      if (logtb_find_interrupt (tran_index, &interrupt) != NO_ERROR)
	{
	  break;
	}
      if (interrupt == false)
	{
	  break;
	}
    }

  if (i == LOGTB_RETRY_SLAM_MAX_TIMES)
    {
      return ER_FAILED;		/* timeout */
    }

  if (is_trx_exists == false)
    {
      /*
       * Note that the following error will be ignored by
       * sthread_kill_or_interrupt_tran().
       */
      return ER_FAILED;
    }

  return NO_ERROR;
}

//
// logtb_find_thread_entry_mapfunc - function mapped over thread manager's entries to find thread belonging to given
//                                   transaction index
//
// thread_ref (in)   : thread entry
// stop_mapper (out) : output true to stop mapping
// tran_index (in)   : searched transaction index
// except_me (in)    : true to accept current transaction, false otherwise
// found_ptr (out)   : saves pointer to found thread entry
//
static void
logtb_find_thread_entry_mapfunc (THREAD_ENTRY & thread_ref, bool & stop_mapper, int tran_index, bool except_me,
				 REFPTR (THREAD_ENTRY, found_ptr))
{
  if (thread_ref.tran_index != tran_index)
    {
      // not this
      return;
    }
  if (except_me && thread_ref.is_on_current_thread ())
    {
      // not me
      return;
    }
  // found
  found_ptr = &thread_ref;
  stop_mapper = true;		// stop searching
}

//
// logtb_find_thread_by_tran_index - find thread entry by transaction index
//
// return          : NULL or pointer to found thread
// tran_index (in) : searched transaction index
//
THREAD_ENTRY *
logtb_find_thread_by_tran_index (int tran_index)
{
  THREAD_ENTRY *found_thread = NULL;
  thread_get_manager ()->map_entries (logtb_find_thread_entry_mapfunc, tran_index, false, found_thread);
  return found_thread;
}

//
// thread_find_entry_by_tran_index_except_me - find thread entry by transaction index; ignore current thread
//
// return          : NULL or pointer to found thread
// tran_index (in) : searched transaction index
//
THREAD_ENTRY *
logtb_find_thread_by_tran_index_except_me (int tran_index)
{
  THREAD_ENTRY *found_thread = NULL;
  thread_get_manager ()->map_entries (logtb_find_thread_entry_mapfunc, tran_index, true, found_thread);
  return found_thread;
}

#if defined (SERVER_MODE)
//
// logtb_wakeup_thread_with_tran_index - find thread by transaction index and wake it
//
// tran_index (in)    : searched transaction index
// resume_reason (in) : the reason thread is resumed
void
logtb_wakeup_thread_with_tran_index (int tran_index, thread_resume_suspend_status resume_reason)
{
  // find thread with transaction index; ignore current thread
  THREAD_ENTRY *thread_p = logtb_find_thread_by_tran_index_except_me (tran_index);
  if (thread_p == NULL)
    {
      // not found
      return;
    }

  thread_wakeup (thread_p, resume_reason);
}
#endif // SERVER_MODE

/*
 * logtb_get_current_tran_index() - get transaction index of current thread
 *   return:
 */
int
logtb_get_current_tran_index (void)
{
  THREAD_ENTRY *thread_p = thread_get_thread_entry_info ();
  assert (thread_p != NULL);

  return thread_p->tran_index;
}

/*
 * logtb_set_current_tran_index - set transaction index on current thread
 */
void
logtb_set_current_tran_index (THREAD_ENTRY * thread_p, int tran_index)
{
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }
  thread_p->tran_index = tran_index;
}

/*
 * logtb_set_check_interrupt() -
 *   return:
 *   flag(in):
 */
bool
logtb_set_check_interrupt (THREAD_ENTRY * thread_p, bool flag)
{
#if defined (SERVER_MODE)
  bool old_val = true;

  if (BO_IS_SERVER_RESTARTED ())
    {
      if (thread_p == NULL)
	{
	  thread_p = thread_get_thread_entry_info ();
	}

      /* safe guard: vacuum workers should not check for interrupt */
      assert (flag == false || !VACUUM_IS_THREAD_VACUUM (thread_p));
      old_val = thread_p->check_interrupt;
      thread_p->check_interrupt = flag;
    }

  return old_val;
#else // not SERVER_MODE = SA_MODE
  return tran_set_check_interrupt (flag);
#endif // not SERVER_MODE = SA_MODE
}

/*
 * logtb_get_check_interrupt() -
 *   return:
 */
bool
logtb_get_check_interrupt (THREAD_ENTRY * thread_p)
{
#if defined (SERVER_MODE)
  bool ret_val = true;

  if (BO_IS_SERVER_RESTARTED ())
    {
      if (thread_p == NULL)
	{
	  thread_p = thread_get_thread_entry_info ();
	}

      ret_val = thread_p->check_interrupt;
    }

  return ret_val;
#else // not SERVER_MODE = SA_MODE
  return tran_get_check_interrupt ();
#endif // not SERVER_MODE = SA_MODE
}

LOG_TDES *
logtb_get_system_tdes (THREAD_ENTRY * thread_p)
{
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }
  // if requesting system tran_index and this is a system worker, return its own log_tdes
  if (thread_p->tran_index == LOG_SYSTEM_TRAN_INDEX && thread_p->get_system_tdes () != NULL)
    {
      return thread_p->get_system_tdes ()->get_tdes ();
    }
  else
    {
      return log_Gl.trantable.all_tdes[LOG_SYSTEM_TRAN_INDEX];
    }
}

// *INDENT-OFF*
// C++

bool
log_tdes::is_active_worker_transaction () const
{
  return tran_index > LOG_SYSTEM_TRAN_INDEX && tran_index < log_Gl.trantable.num_total_indices;
}

bool
log_tdes::is_system_transaction () const
{
  return tran_index == LOG_SYSTEM_TRAN_INDEX;
}

bool
log_tdes::is_system_main_transaction () const
{
  return is_system_transaction () && trid == LOG_SYSTEM_TRANID;
}

bool
log_tdes::is_system_worker_transaction () const
{
  return is_system_transaction () && trid < NULL_TRANID;
}

bool
log_tdes::is_allowed_sysop () const
{
  return is_active_worker_transaction () || is_system_worker_transaction ();
}

bool
log_tdes::is_under_sysop () const
{
  return topops.last >= 0;
}

bool
log_tdes::is_allowed_undo () const
{
  return is_active_worker_transaction () || is_under_sysop ();
}

void
log_tdes::lock_topop ()
{
  if (LOG_ISRESTARTED () && is_active_worker_transaction ())
    {
      int r = rmutex_lock (NULL, &rmutex_topop);
      assert (r == NO_ERROR);
    }
}

void
log_tdes::unlock_topop ()
{
  if (LOG_ISRESTARTED () && is_active_worker_transaction ())
    {
      int r = rmutex_unlock (NULL, &rmutex_topop);
      assert (r == NO_ERROR);
    }
}

void
log_tdes::on_sysop_start ()
{
  assert (is_allowed_sysop ());

  if (is_system_worker_transaction () && topops.last < 0)
    {
      if (!LOG_ISRESTARTED ())
	{
	  /* The links are used at recovery. */
	  return;
	}

      // make sure all links to previous records are lost
      assert (topops.last == -1);
      LSA_SET_NULL (&head_lsa);
      LSA_SET_NULL (&tail_lsa);
      LSA_SET_NULL (&undo_nxlsa);
      LSA_SET_NULL (&tail_topresult_lsa);
      assert (commit_abort_lsa.is_null ());
      LSA_SET_NULL (&rcv.tran_start_postpone_lsa);
      LSA_SET_NULL (&rcv.sysop_start_postpone_lsa);
    }
}

void
log_tdes::on_sysop_end ()
{
  assert (is_allowed_sysop ());
  if (is_system_worker_transaction() && topops.last < 0)
    {
      // make sure this system operation cannot be linked
      assert (topops.last == -1);
      LSA_SET_NULL (&head_lsa);
      LSA_SET_NULL (&tail_lsa);
      LSA_SET_NULL (&undo_nxlsa);
      LSA_SET_NULL (&tail_topresult_lsa);
      assert (commit_abort_lsa.is_null ());
    }
}

void
log_tdes::lock_global_oldest_visible_mvccid ()
{
  if (!block_global_oldest_active_until_commit)
    {
      log_Gl.mvcc_table.lock_global_oldest_visible ();
      block_global_oldest_active_until_commit = true;
    }
}

void
log_tdes::unlock_global_oldest_visible_mvccid ()
{
  if (block_global_oldest_active_until_commit)
    {
      assert (log_Gl.mvcc_table.is_global_oldest_visible_locked ());
      log_Gl.mvcc_table.unlock_global_oldest_visible ();
      block_global_oldest_active_until_commit = false;
    }
}
// *INDENT-ON*
