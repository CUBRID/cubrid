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

#include "log_append.hpp"

#include "file_manager.h"
#include "log_compress.h"
#include "log_impl.h"
#include "log_manager.h"
#include "log_record.hpp"
#include "page_buffer.h"
#include "perf_monitor.h"
#include "thread_entry.hpp"
#include "thread_manager.hpp"
#include "vacuum.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#if !defined(SERVER_MODE)
static LOG_ZIP *log_zip_undo = NULL;
static LOG_ZIP *log_zip_redo = NULL;
static char *log_data_ptr = NULL;
static int log_data_length = 0;
#endif
bool log_Zip_support = false;
int log_Zip_min_size_to_compress = 255;

size_t
LOG_PRIOR_LSA_LAST_APPEND_OFFSET ()
{
  return LOGAREA_SIZE;
}

static void log_prior_lsa_append_align ();
static void log_prior_lsa_append_advance_when_doesnot_fit (size_t length);
static void log_prior_lsa_append_add_align (size_t add);
static int prior_lsa_gen_postpone_record (THREAD_ENTRY *thread_p, LOG_PRIOR_NODE *node, LOG_RCVINDEX rcvindex,
    LOG_DATA_ADDR *addr, int length, const char *data);
static int prior_lsa_gen_dbout_redo_record (THREAD_ENTRY *thread_p, LOG_PRIOR_NODE *node, LOG_RCVINDEX rcvindex,
    int length, const char *data);
static int prior_lsa_gen_record (THREAD_ENTRY *thread_p, LOG_PRIOR_NODE *node, LOG_RECTYPE rec_type, int length,
				 const char *data);
static int prior_lsa_gen_undoredo_record_from_crumbs (THREAD_ENTRY *thread_p, LOG_PRIOR_NODE *node,
    LOG_RCVINDEX rcvindex, LOG_DATA_ADDR *addr, int num_ucrumbs, const LOG_CRUMB *ucrumbs, int num_rcrumbs,
    const LOG_CRUMB *rcrumbs);
static int prior_lsa_gen_2pc_prepare_record (THREAD_ENTRY *thread_p, LOG_PRIOR_NODE *node, int gtran_length,
    const char *gtran_data, int lock_length, const char *lock_data);
static int prior_lsa_gen_end_chkpt_record (THREAD_ENTRY *thread_p, LOG_PRIOR_NODE *node, int tran_length,
    const char *tran_data, int topop_length, const char *topop_data);
static int prior_lsa_copy_undo_data_to_node (LOG_PRIOR_NODE *node, int length, const char *data);
static int prior_lsa_copy_redo_data_to_node (LOG_PRIOR_NODE *node, int length, const char *data);
static int prior_lsa_copy_undo_crumbs_to_node (LOG_PRIOR_NODE *node, int num_crumbs, const LOG_CRUMB *crumbs);
static int prior_lsa_copy_redo_crumbs_to_node (LOG_PRIOR_NODE *node, int num_crumbs, const LOG_CRUMB *crumbs);
static void prior_lsa_start_append (THREAD_ENTRY *thread_p, LOG_PRIOR_NODE *node, LOG_TDES *tdes);
static void prior_lsa_end_append (THREAD_ENTRY *thread_p, LOG_PRIOR_NODE *node);
static void prior_lsa_append_data (int length);
static LOG_LSA prior_lsa_next_record_internal (THREAD_ENTRY *thread_p, LOG_PRIOR_NODE *node, LOG_TDES *tdes,
    int with_lock);
static void prior_update_header_mvcc_info (const LOG_LSA &record_lsa, MVCCID mvccid);
static char *log_append_get_data_ptr (THREAD_ENTRY *thread_p);
static bool log_append_realloc_data_ptr (THREAD_ENTRY *thread_p, int length);

log_data_addr::log_data_addr (const VFID *vfid_arg, PAGE_PTR pgptr_arg, PGLENGTH offset_arg)
  : vfid (vfid_arg)
  , pgptr (pgptr_arg)
  , offset (offset_arg)
{
}

log_append_info::log_append_info ()
  : vdes (NULL_VOLDES)
  , nxio_lsa (NULL_LSA)
  , prev_lsa (NULL_LSA)
  , log_pgptr (NULL)
  , appending_page_tde_encrypted (false)
{

}

log_append_info::log_append_info (const log_append_info &other)
  : vdes (other.vdes)
  , nxio_lsa {other.nxio_lsa.load ()}
  , prev_lsa (other.prev_lsa)
  , log_pgptr (other.log_pgptr)
  , appending_page_tde_encrypted (other.appending_page_tde_encrypted)
{

}

LOG_LSA
log_append_info::get_nxio_lsa () const
{
  return nxio_lsa.load ();
}

void
log_append_info::set_nxio_lsa (const LOG_LSA &next_io_lsa)
{
  nxio_lsa.store (next_io_lsa);
}

log_prior_lsa_info::log_prior_lsa_info ()
  : prior_lsa (NULL_LSA)
  , prev_lsa (NULL_LSA)
  , prior_list_header (NULL)
  , prior_list_tail (NULL)
  , list_size (0)
  , prior_flush_list_header (NULL)
  , prior_lsa_mutex ()
{
}

void
LOG_RESET_APPEND_LSA (const LOG_LSA *lsa)
{
  // todo - concurrency safe-guard
  log_Gl.hdr.append_lsa = *lsa;
  log_Gl.prior_info.prior_lsa = *lsa;
}

void
LOG_RESET_PREV_LSA (const LOG_LSA *lsa)
{
  // todo - concurrency safe-guard
  log_Gl.append.prev_lsa = *lsa;
  log_Gl.prior_info.prev_lsa = *lsa;
}

char *
LOG_APPEND_PTR ()
{
  // todo - concurrency safe-guard
  return log_Gl.append.log_pgptr->area + log_Gl.hdr.append_lsa.offset;
}

bool
log_prior_has_worker_log_records (THREAD_ENTRY *thread_p)
{
  LOG_CS_ENTER (thread_p);

  std::unique_lock<std::mutex> ulock (log_Gl.prior_info.prior_lsa_mutex);
  LOG_LSA nxio_lsa = log_Gl.append.get_nxio_lsa ();

  if (!LSA_EQ (&nxio_lsa, &log_Gl.prior_info.prior_lsa))
    {
      LOG_PRIOR_NODE *node;

      assert (LSA_LT (&nxio_lsa, &log_Gl.prior_info.prior_lsa));
      node = log_Gl.prior_info.prior_list_header;
      while (node != NULL)
	{
	  if (node->log_header.trid != LOG_SYSTEM_TRANID)
	    {
	      ulock.unlock ();
	      LOG_CS_EXIT (thread_p);
	      return true;
	    }
	  node = node->next;
	}
    }

  ulock.unlock ();

  LOG_CS_EXIT (thread_p);

  return false;
}

void
log_append_init_zip ()
{
  if (!prm_get_bool_value (PRM_ID_LOG_COMPRESS))
    {
      log_Zip_support = false;
      return;
    }

#if defined(SERVER_MODE)
  log_Zip_support = true;
#else
  log_zip_undo = log_zip_alloc (IO_PAGESIZE);
  log_zip_redo = log_zip_alloc (IO_PAGESIZE);
  log_data_length = IO_PAGESIZE * 2;
  log_data_ptr = (char *) malloc (log_data_length);
  if (log_data_ptr == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) log_data_length);
    }

  if (log_zip_undo == NULL || log_zip_redo == NULL || log_data_ptr == NULL)
    {
      log_Zip_support = false;
      if (log_zip_undo)
	{
	  log_zip_free (log_zip_undo);
	  log_zip_undo = NULL;
	}
      if (log_zip_redo)
	{
	  log_zip_free (log_zip_redo);
	  log_zip_redo = NULL;
	}
      if (log_data_ptr)
	{
	  free_and_init (log_data_ptr);
	  log_data_length = 0;
	}
    }
  else
    {
      log_Zip_support = true;
    }
#endif
}

void
log_append_final_zip ()
{
  if (!log_Zip_support)
    {
      return;
    }

#if defined (SERVER_MODE)
#else
  if (log_zip_undo)
    {
      log_zip_free (log_zip_undo);
      log_zip_undo = NULL;
    }
  if (log_zip_redo)
    {
      log_zip_free (log_zip_redo);
      log_zip_redo = NULL;
    }
  if (log_data_ptr)
    {
      free_and_init (log_data_ptr);
      log_data_length = 0;
    }
#endif
}

/*
 * prior_lsa_alloc_and_copy_data -
 *
 * return: new node
 *
 *   rec_type(in):
 *   rcvindex(in):
 *   addr(in):
 *   ulength(in):
 *   udata(in):
 *   rlength(in):
 *   rdata(in):
 */
LOG_PRIOR_NODE *
prior_lsa_alloc_and_copy_data (THREAD_ENTRY *thread_p, LOG_RECTYPE rec_type, LOG_RCVINDEX rcvindex,
			       LOG_DATA_ADDR *addr, int ulength, const char *udata, int rlength, const char *rdata)
{
  LOG_PRIOR_NODE *node;
  int error_code = NO_ERROR;

  node = (LOG_PRIOR_NODE *) malloc (sizeof (LOG_PRIOR_NODE));
  if (node == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (LOG_PRIOR_NODE));
      return NULL;
    }

  node->log_header.type = rec_type;

  node->tde_encrypted = false;

  node->data_header_length = 0;
  node->data_header = NULL;
  node->ulength = 0;
  node->udata = NULL;
  node->rlength = 0;
  node->rdata = NULL;
  node->next = NULL;

  switch (rec_type)
    {
    case LOG_UNDOREDO_DATA:
    case LOG_DIFF_UNDOREDO_DATA:
    case LOG_UNDO_DATA:
    case LOG_REDO_DATA:
    case LOG_MVCC_UNDOREDO_DATA:
    case LOG_MVCC_DIFF_UNDOREDO_DATA:
    case LOG_MVCC_REDO_DATA:
    case LOG_MVCC_UNDO_DATA:
      /* We shouldn't be here */
      /* Use prior_lsa_alloc_and_copy_crumbs instead */
      assert_release (false);
      error_code = ER_FAILED;
      break;

    case LOG_DBEXTERN_REDO_DATA:
      error_code = prior_lsa_gen_dbout_redo_record (thread_p, node, rcvindex, rlength, rdata);
      break;

    case LOG_POSTPONE:
      assert (ulength == 0 && udata == NULL);

      error_code = prior_lsa_gen_postpone_record (thread_p, node, rcvindex, addr, rlength, rdata);
      break;

    case LOG_2PC_PREPARE:
      assert (addr == NULL);
      error_code = prior_lsa_gen_2pc_prepare_record (thread_p, node, ulength, udata, rlength, rdata);
      break;
    case LOG_END_CHKPT:
      assert (addr == NULL);
      error_code = prior_lsa_gen_end_chkpt_record (thread_p, node, ulength, udata, rlength, rdata);
      break;

    case LOG_RUN_POSTPONE:
    case LOG_COMPENSATE:
    case LOG_SAVEPOINT:

    case LOG_DUMMY_HEAD_POSTPONE:

    case LOG_DUMMY_CRASH_RECOVERY:
    case LOG_DUMMY_HA_SERVER_STATE:
    case LOG_DUMMY_OVF_RECORD:
    case LOG_DUMMY_GENERIC:
    case LOG_SUPPLEMENTAL_INFO:

    case LOG_2PC_COMMIT_DECISION:
    case LOG_2PC_ABORT_DECISION:
    case LOG_COMMIT_WITH_POSTPONE:
    case LOG_COMMIT_WITH_POSTPONE_OBSOLETE:
    case LOG_SYSOP_START_POSTPONE:
    case LOG_COMMIT:
    case LOG_ABORT:
    case LOG_2PC_COMMIT_INFORM_PARTICPS:
    case LOG_2PC_ABORT_INFORM_PARTICPS:
    case LOG_SYSOP_END:
    case LOG_REPLICATION_DATA:
    case LOG_REPLICATION_STATEMENT:
    case LOG_2PC_START:
    case LOG_START_CHKPT:
    case LOG_SYSOP_ATOMIC_START:
      assert (rlength == 0 && rdata == NULL);

      error_code = prior_lsa_gen_record (thread_p, node, rec_type, ulength, udata);
      break;

    default:
      break;
    }

  if (error_code == NO_ERROR)
    {
      return node;
    }
  else
    {
      if (node != NULL)
	{
	  if (node->data_header != NULL)
	    {
	      free_and_init (node->data_header);
	    }
	  if (node->udata != NULL)
	    {
	      free_and_init (node->udata);
	    }
	  if (node->rdata != NULL)
	    {
	      free_and_init (node->rdata);
	    }
	  free_and_init (node);
	}

      return NULL;
    }
}

/*
 * prior_lsa_alloc_and_copy_crumbs -
 *
 * return: new node
 *
 *   rec_type(in):
 *   rcvindex(in):
 *   addr(in):
 *   num_ucrumbs(in):
 *   ucrumbs(in):
 *   num_rcrumbs(in):
 *   rcrumbs(in):
 */
LOG_PRIOR_NODE *
prior_lsa_alloc_and_copy_crumbs (THREAD_ENTRY *thread_p, LOG_RECTYPE rec_type, LOG_RCVINDEX rcvindex,
				 LOG_DATA_ADDR *addr, const int num_ucrumbs,
				 const LOG_CRUMB *ucrumbs, const int num_rcrumbs, const LOG_CRUMB *rcrumbs)
{
  LOG_PRIOR_NODE *node;
  int error = NO_ERROR;

  node = (LOG_PRIOR_NODE *) malloc (sizeof (LOG_PRIOR_NODE));
  if (node == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (LOG_PRIOR_NODE));
      return NULL;
    }

  node->log_header.type = rec_type;

  node->tde_encrypted = false;

  node->data_header_length = 0;
  node->data_header = NULL;
  node->ulength = 0;
  node->udata = NULL;
  node->rlength = 0;
  node->rdata = NULL;
  node->next = NULL;

  switch (rec_type)
    {
    case LOG_UNDOREDO_DATA:
    case LOG_DIFF_UNDOREDO_DATA:
    case LOG_UNDO_DATA:
    case LOG_REDO_DATA:
    case LOG_MVCC_UNDOREDO_DATA:
    case LOG_MVCC_DIFF_UNDOREDO_DATA:
    case LOG_MVCC_UNDO_DATA:
    case LOG_MVCC_REDO_DATA:
      error = prior_lsa_gen_undoredo_record_from_crumbs (thread_p, node, rcvindex, addr, num_ucrumbs, ucrumbs,
	      num_rcrumbs, rcrumbs);
      break;

    default:
      /* Unhandled */
      assert_release (false);
      error = ER_FAILED;
      break;
    }

  if (error == NO_ERROR)
    {
      return node;
    }
  else
    {
      if (node != NULL)
	{
	  if (node->data_header != NULL)
	    {
	      free_and_init (node->data_header);
	    }
	  if (node->udata != NULL)
	    {
	      free_and_init (node->udata);
	    }
	  if (node->rdata != NULL)
	    {
	      free_and_init (node->rdata);
	    }
	  free_and_init (node);
	}
      return NULL;
    }
}

/*
 * prior_lsa_copy_undo_data_to_node -
 *
 * return: error code or NO_ERROR
 *
 *   node(in/out):
 *   length(in):
 *   data(in):
 */
static int
prior_lsa_copy_undo_data_to_node (LOG_PRIOR_NODE *node, int length, const char *data)
{
  if (length <= 0 || data == NULL)
    {
      return NO_ERROR;
    }

  node->udata = (char *) malloc (length);
  if (node->udata == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) length);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  memcpy (node->udata, data, length);

  node->ulength = length;

  return NO_ERROR;
}

/*
 * prior_lsa_copy_redo_data_to_node -
 *
 * return: error code or NO_ERROR
 *
 *   node(in/out):
 *   length(in):
 *   data(in):
 */
static int
prior_lsa_copy_redo_data_to_node (LOG_PRIOR_NODE *node, int length, const char *data)
{
  if (length <= 0 || data == NULL)
    {
      return NO_ERROR;
    }

  node->rdata = (char *) malloc (length);
  if (node->rdata == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) length);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  memcpy (node->rdata, data, length);

  node->rlength = length;

  return NO_ERROR;
}

/*
 * prior_lsa_copy_undo_crumbs_to_node -
 *
 * return: error code or NO_ERROR
 *
 *   node(in/out):
 *   num_crumbs(in):
 *   crumbs(in):
 */
static int
prior_lsa_copy_undo_crumbs_to_node (LOG_PRIOR_NODE *node, int num_crumbs, const LOG_CRUMB *crumbs)
{
  int i, length;
  char *ptr;

  /* Safe guard: either num_crumbs is 0 or crumbs array is not NULL */
  assert (num_crumbs == 0 || crumbs != NULL);

  for (i = 0, length = 0; i < num_crumbs; i++)
    {
      length += crumbs[i].length;
    }

  assert (node->udata == NULL);
  if (length > 0)
    {
      node->udata = (char *) malloc (length);
      if (node->udata == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) length);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}

      ptr = node->udata;
      for (i = 0; i < num_crumbs; i++)
	{
	  memcpy (ptr, crumbs[i].data, crumbs[i].length);
	  ptr += crumbs[i].length;
	}
    }

  node->ulength = length;
  return NO_ERROR;
}

/*
 * prior_lsa_copy_redo_crumbs_to_node -
 *
 * return: error code or NO_ERROR
 *
 *   node(in/out):
 *   num_crumbs(in):
 *   crumbs(in):
 */
static int
prior_lsa_copy_redo_crumbs_to_node (LOG_PRIOR_NODE *node, int num_crumbs, const LOG_CRUMB *crumbs)
{
  int i, length;
  char *ptr;

  /* Safe guard: either num_crumbs is 0 or crumbs array is not NULL */
  assert (num_crumbs == 0 || crumbs != NULL);

  for (i = 0, length = 0; i < num_crumbs; i++)
    {
      length += crumbs[i].length;
    }

  assert (node->rdata == NULL);
  if (length > 0)
    {
      node->rdata = (char *) malloc (length);
      if (node->rdata == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) length);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}

      ptr = node->rdata;
      for (i = 0; i < num_crumbs; i++)
	{
	  memcpy (ptr, crumbs[i].data, crumbs[i].length);
	  ptr += crumbs[i].length;
	}
    }

  node->rlength = length;

  return NO_ERROR;
}

/*
 * prior_lsa_gen_undoredo_record_from_crumbs () - Generate undoredo or MVCC
 *						  undoredo log record.
 *
 * return	    : Error code.
 * thread_p (in)    : Thread entry.
 * node (in)	    : Log prior node.
 * rcvindex (in)    : Index of recovery function.
 * addr (in)	    : Logged data address.
 * num_ucrumbs (in) : Number of undo data crumbs.
 * ucrumbs (in)	    : Undo data crumbs.
 * num_rcrumbs (in) : Number of redo data crumbs.
 * rcrumbs (in)	    : Redo data crumbs.
 */
static int
prior_lsa_gen_undoredo_record_from_crumbs (THREAD_ENTRY *thread_p, LOG_PRIOR_NODE *node, LOG_RCVINDEX rcvindex,
    LOG_DATA_ADDR *addr, int num_ucrumbs, const LOG_CRUMB *ucrumbs,
    int num_rcrumbs, const LOG_CRUMB *rcrumbs)
{
  LOG_REC_REDO *redo_p = NULL;
  LOG_REC_UNDO *undo_p = NULL;
  LOG_REC_UNDOREDO *undoredo_p = NULL;
  LOG_REC_MVCC_REDO *mvcc_redo_p = NULL;
  LOG_REC_MVCC_UNDO *mvcc_undo_p = NULL;
  LOG_REC_MVCC_UNDOREDO *mvcc_undoredo_p = NULL;
  LOG_DATA *log_data_p = NULL;
  LOG_VACUUM_INFO *vacuum_info_p = NULL;
  VPID *vpid = NULL;
  int error_code = NO_ERROR;
  int i;
  int ulength, rlength, *data_header_ulength_p = NULL, *data_header_rlength_p = NULL;
  int total_length;
  MVCCID *mvccid_p = NULL;
  LOG_TDES *tdes = NULL;
  char *data_ptr = NULL, *tmp_ptr = NULL;
  char *undo_data = NULL, *redo_data = NULL;
  LOG_ZIP *zip_undo = NULL, *zip_redo = NULL;
  bool is_mvcc_op = LOG_IS_MVCC_OP_RECORD_TYPE (node->log_header.type);
  bool has_undo = false;
  bool has_redo = false;
  bool is_undo_zip = false, is_redo_zip = false, is_diff = false;
  bool can_zip = false;

  assert (node->log_header.type != LOG_DIFF_UNDOREDO_DATA && node->log_header.type != LOG_MVCC_DIFF_UNDOREDO_DATA);
  assert (num_ucrumbs == 0 || ucrumbs != NULL);
  assert (num_rcrumbs == 0 || rcrumbs != NULL);

  zip_undo = log_append_get_zip_undo (thread_p);
  zip_redo = log_append_get_zip_redo (thread_p);

  ulength = 0;
  for (i = 0; i < num_ucrumbs; i++)
    {
      ulength += ucrumbs[i].length;
    }
  assert (0 <= ulength);

  rlength = 0;
  for (i = 0; i < num_rcrumbs; i++)
    {
      rlength += rcrumbs[i].length;
    }
  assert (0 <= rlength);

  /* Check if we have undo or redo and if we can zip */
  if (LOG_IS_UNDOREDO_RECORD_TYPE (node->log_header.type))
    {
      has_undo = true;
      has_redo = true;
      can_zip = log_Zip_support && (zip_undo != NULL || ulength == 0) && (zip_redo != NULL || rlength == 0);
    }
  else if (LOG_IS_REDO_RECORD_TYPE (node->log_header.type))
    {
      has_redo = true;
      can_zip = log_Zip_support && zip_redo;
    }
  else
    {
      /* UNDO type */
      assert (LOG_IS_UNDO_RECORD_TYPE (node->log_header.type));
      has_undo = true;
      can_zip = log_Zip_support && zip_undo;
    }

  if (can_zip == true && (ulength >= log_Zip_min_size_to_compress || rlength >= log_Zip_min_size_to_compress))
    {
      /* Try to zip undo and/or redo data */
      total_length = 0;
      if (ulength > 0)
	{
	  total_length += ulength;
	}
      if (rlength > 0)
	{
	  total_length += rlength;
	}

      if (log_append_realloc_data_ptr (thread_p, total_length))
	{
	  data_ptr = log_append_get_data_ptr (thread_p);
	}

      if (data_ptr != NULL)
	{
	  tmp_ptr = data_ptr;

	  if (ulength >= log_Zip_min_size_to_compress)
	    {
	      assert (has_undo == true);

	      undo_data = data_ptr;

	      for (i = 0; i < num_ucrumbs; i++)
		{
		  memcpy (tmp_ptr, (char *) ucrumbs[i].data, ucrumbs[i].length);
		  tmp_ptr += ucrumbs[i].length;
		}

	      assert (CAST_BUFLEN (tmp_ptr - undo_data) == ulength);
	    }

	  if (rlength >= log_Zip_min_size_to_compress)
	    {
	      assert (has_redo == true);

	      redo_data = tmp_ptr;

	      for (i = 0; i < num_rcrumbs; i++)
		{
		  (void) memcpy (tmp_ptr, (char *) rcrumbs[i].data, rcrumbs[i].length);
		  tmp_ptr += rcrumbs[i].length;
		}

	      assert (CAST_BUFLEN (tmp_ptr - redo_data) == rlength);
	    }

	  assert (CAST_BUFLEN (tmp_ptr - data_ptr) == total_length
		  || ulength < log_Zip_min_size_to_compress || rlength < log_Zip_min_size_to_compress);

	  if (ulength >= log_Zip_min_size_to_compress && rlength >= log_Zip_min_size_to_compress)
	    {
	      (void) log_diff (ulength, undo_data, rlength, redo_data);

	      is_undo_zip = log_zip (zip_undo, ulength, undo_data);
	      is_redo_zip = log_zip (zip_redo, rlength, redo_data);

	      if (is_redo_zip)
		{
		  is_diff = true;
		}
	    }
	  else
	    {
	      if (ulength >= log_Zip_min_size_to_compress)
		{
		  is_undo_zip = log_zip (zip_undo, ulength, undo_data);
		}
	      if (rlength >= log_Zip_min_size_to_compress)
		{
		  is_redo_zip = log_zip (zip_redo, rlength, redo_data);
		}
	    }
	}
    }

  if (is_diff)
    {
      /* Set diff UNDOREDO type */
      assert (has_redo && has_undo);
      if (is_mvcc_op)
	{
	  node->log_header.type = LOG_MVCC_DIFF_UNDOREDO_DATA;
	}
      else
	{
	  node->log_header.type = LOG_DIFF_UNDOREDO_DATA;
	}
    }

  /* Compute the length of data header */
  switch (node->log_header.type)
    {
    case LOG_MVCC_UNDO_DATA:
      node->data_header_length = sizeof (LOG_REC_MVCC_UNDO);
      break;
    case LOG_UNDO_DATA:
      node->data_header_length = sizeof (LOG_REC_UNDO);
      break;
    case LOG_MVCC_REDO_DATA:
      node->data_header_length = sizeof (LOG_REC_MVCC_REDO);
      break;
    case LOG_REDO_DATA:
      node->data_header_length = sizeof (LOG_REC_REDO);
      break;
    case LOG_MVCC_UNDOREDO_DATA:
    case LOG_MVCC_DIFF_UNDOREDO_DATA:
      node->data_header_length = sizeof (LOG_REC_MVCC_UNDOREDO);
      break;
    case LOG_UNDOREDO_DATA:
    case LOG_DIFF_UNDOREDO_DATA:
      node->data_header_length = sizeof (LOG_REC_UNDOREDO);
      break;
    default:
      assert (0);
      break;
    }

  /* Allocate memory for data header */
  node->data_header = (char *) malloc (node->data_header_length);
  if (node->data_header == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (LOG_REC_UNDOREDO));
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      goto error;
    }

#if !defined (NDEBUG)
  /* Suppress valgrind complaint. */
  memset (node->data_header, 0, node->data_header_length);
#endif // DEBUG

  /* Fill the data header fields */
  switch (node->log_header.type)
    {
    case LOG_MVCC_UNDO_DATA:
      /* Use undo data from MVCC undo structure */
      mvcc_undo_p = (LOG_REC_MVCC_UNDO *) node->data_header;

      /* Must also fill vacuum info */
      vacuum_info_p = &mvcc_undo_p->vacuum_info;

      /* Must also fill MVCCID field */
      mvccid_p = &mvcc_undo_p->mvccid;

    /* Fall through */
    case LOG_UNDO_DATA:
      undo_p = (node->log_header.type == LOG_UNDO_DATA ? (LOG_REC_UNDO *) node->data_header : &mvcc_undo_p->undo);

      data_header_ulength_p = &undo_p->length;
      log_data_p = &undo_p->data;
      break;

    case LOG_MVCC_REDO_DATA:
      /* Use redo data from MVCC redo structure */
      mvcc_redo_p = (LOG_REC_MVCC_REDO *) node->data_header;

      /* Must also fill MVCCID field */
      mvccid_p = &mvcc_redo_p->mvccid;

    /* Fall through */
    case LOG_REDO_DATA:
      redo_p = (node->log_header.type == LOG_REDO_DATA ? (LOG_REC_REDO *) node->data_header : &mvcc_redo_p->redo);

      data_header_rlength_p = &redo_p->length;
      log_data_p = &redo_p->data;
      break;

    case LOG_MVCC_UNDOREDO_DATA:
    case LOG_MVCC_DIFF_UNDOREDO_DATA:
      /* Use undoredo data from MVCC undoredo structure */
      mvcc_undoredo_p = (LOG_REC_MVCC_UNDOREDO *) node->data_header;

      /* Must also fill vacuum info */
      vacuum_info_p = &mvcc_undoredo_p->vacuum_info;

      /* Must also fill MVCCID field */
      mvccid_p = &mvcc_undoredo_p->mvccid;

    /* Fall through */
    case LOG_UNDOREDO_DATA:
    case LOG_DIFF_UNDOREDO_DATA:
      undoredo_p = ((node->log_header.type == LOG_UNDOREDO_DATA || node->log_header.type == LOG_DIFF_UNDOREDO_DATA)
		    ? (LOG_REC_UNDOREDO *) node->data_header : &mvcc_undoredo_p->undoredo);

      data_header_ulength_p = &undoredo_p->ulength;
      data_header_rlength_p = &undoredo_p->rlength;
      log_data_p = &undoredo_p->data;
      break;

    default:
      assert (0);
      break;
    }

  /* Fill log data fields */
  assert (log_data_p != NULL);

  log_data_p->rcvindex = rcvindex;
  log_data_p->offset = addr->offset;

  if (addr->pgptr != NULL)
    {
      vpid = pgbuf_get_vpid_ptr (addr->pgptr);
      log_data_p->pageid = vpid->pageid;
      log_data_p->volid = vpid->volid;
    }
  else
    {
      log_data_p->pageid = NULL_PAGEID;
      log_data_p->volid = NULL_VOLID;
    }

  if (mvccid_p != NULL)
    {
      /* Fill mvccid field */

      /* Must be an MVCC operation */
      assert (LOG_IS_MVCC_OP_RECORD_TYPE (node->log_header.type));
      assert (LOG_IS_MVCC_OPERATION (rcvindex));

      tdes = LOG_FIND_CURRENT_TDES (thread_p);
      if (tdes == NULL || !MVCCID_IS_VALID (tdes->mvccinfo.id))
	{
	  assert_release (false);
	  error_code = ER_FAILED;
	  goto error;
	}
      else
	{
	  if (!tdes->mvccinfo.sub_ids.empty ())
	    {
	      *mvccid_p = tdes->mvccinfo.sub_ids.back ();
	    }
	  else
	    {
	      *mvccid_p = tdes->mvccinfo.id;
	    }
	}
    }

  if (vacuum_info_p != NULL)
    {
      /* Fill vacuum info field */

      /* Must be an UNDO or UNDOREDO MVCC operation */
      assert (node->log_header.type == LOG_MVCC_UNDO_DATA || node->log_header.type == LOG_MVCC_UNDOREDO_DATA
	      || node->log_header.type == LOG_MVCC_DIFF_UNDOREDO_DATA);
      assert (LOG_IS_MVCC_OPERATION (rcvindex));

      if (addr->vfid != NULL)
	{
	  VFID_COPY (&vacuum_info_p->vfid, addr->vfid);
	}
      else
	{
	  if (rcvindex == RVES_NOTIFY_VACUUM)
	    {
	      VFID_SET_NULL (&vacuum_info_p->vfid);
	    }
	  else
	    {
	      /* We require VFID for vacuum */
	      assert_release (false);
	      error_code = ER_FAILED;
	      goto error;
	    }
	}

      /* Initialize previous MVCC op log lsa - will be completed later */
      LSA_SET_NULL (&vacuum_info_p->prev_mvcc_op_log_lsa);
    }

  if (is_undo_zip)
    {
      assert (has_undo && (data_header_ulength_p != NULL));

      *data_header_ulength_p = MAKE_ZIP_LEN (zip_undo->data_length);
      error_code = prior_lsa_copy_undo_data_to_node (node, zip_undo->data_length, (char *) zip_undo->log_data);
    }
  else if (has_undo)
    {
      assert (data_header_ulength_p != NULL);

      *data_header_ulength_p = ulength;
      error_code = prior_lsa_copy_undo_crumbs_to_node (node, num_ucrumbs, ucrumbs);
    }

  if (is_redo_zip)
    {
      assert (has_redo && (data_header_rlength_p != NULL));

      *data_header_rlength_p = MAKE_ZIP_LEN (zip_redo->data_length);
      error_code = prior_lsa_copy_redo_data_to_node (node, zip_redo->data_length, (char *) zip_redo->log_data);
    }
  else if (has_redo)
    {
      *data_header_rlength_p = rlength;
      error_code = prior_lsa_copy_redo_crumbs_to_node (node, num_rcrumbs, rcrumbs);
    }

  if (error_code != NO_ERROR)
    {
      goto error;
    }

  return error_code;

error:
  if (node->data_header != NULL)
    {
      free_and_init (node->data_header);
    }
  if (node->udata != NULL)
    {
      free_and_init (node->udata);
    }
  if (node->rdata != NULL)
    {
      free_and_init (node->rdata);
    }

  return error_code;
}

/*
 * prior_lsa_gen_postpone_record -
 *
 * return: error code or NO_ERROR
 *
 *   node(in/out):
 *   rcvindex(in):
 *   addr(in):
 *   length(in):
 *   data(in):
 */
static int
prior_lsa_gen_postpone_record (THREAD_ENTRY *thread_p, LOG_PRIOR_NODE *node, LOG_RCVINDEX rcvindex,
			       LOG_DATA_ADDR *addr, int length, const char *data)
{
  LOG_REC_REDO *redo;
  VPID *vpid;
  int error_code = NO_ERROR;

  node->data_header_length = sizeof (LOG_REC_REDO);
  node->data_header = (char *) malloc (node->data_header_length);
  if (node->data_header == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) node->data_header_length);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  redo = (LOG_REC_REDO *) node->data_header;

  redo->data.rcvindex = rcvindex;
  if (addr->pgptr != NULL)
    {
      vpid = pgbuf_get_vpid_ptr (addr->pgptr);
      redo->data.pageid = vpid->pageid;
      redo->data.volid = vpid->volid;
    }
  else
    {
      redo->data.pageid = NULL_PAGEID;
      redo->data.volid = NULL_VOLID;
    }
  redo->data.offset = addr->offset;

  redo->length = length;
  error_code = prior_lsa_copy_redo_data_to_node (node, redo->length, data);

  return error_code;
}

/*
 * prior_lsa_gen_dbout_redo_record -
 *
 * return: error code or NO_ERROR
 *
 *   node(in/out):
 *   rcvindex(in):
 *   length(in):
 *   data(in):
 */
static int
prior_lsa_gen_dbout_redo_record (THREAD_ENTRY *thread_p, LOG_PRIOR_NODE *node, LOG_RCVINDEX rcvindex, int length,
				 const char *data)
{
  LOG_REC_DBOUT_REDO *dbout_redo;
  int error_code = NO_ERROR;

  node->data_header_length = sizeof (LOG_REC_DBOUT_REDO);
  node->data_header = (char *) malloc (node->data_header_length);
  if (node->data_header == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) node->data_header_length);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  dbout_redo = (LOG_REC_DBOUT_REDO *) node->data_header;

  dbout_redo->rcvindex = rcvindex;
  dbout_redo->length = length;

  error_code = prior_lsa_copy_redo_data_to_node (node, dbout_redo->length, data);

  return error_code;
}

/*
 * prior_lsa_gen_2pc_prepare_record -
 *
 * return: error code or NO_ERROR
 *
 *   node(in/out):
 *   gtran_length(in):
 *   gtran_data(in):
 *   lock_length(in):
 *   lock_data(in):
 */
static int
prior_lsa_gen_2pc_prepare_record (THREAD_ENTRY *thread_p, LOG_PRIOR_NODE *node, int gtran_length,
				  const char *gtran_data, int lock_length, const char *lock_data)
{
  int error_code = NO_ERROR;

  node->data_header_length = sizeof (LOG_REC_2PC_PREPCOMMIT);
  node->data_header = (char *) malloc (node->data_header_length);
  if (node->data_header == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) node->data_header_length);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  if (gtran_length > 0)
    {
      error_code = prior_lsa_copy_undo_data_to_node (node, gtran_length, gtran_data);
    }
  if (lock_length > 0)
    {
      error_code = prior_lsa_copy_redo_data_to_node (node, lock_length, lock_data);
    }

  return error_code;
}

/*
 * prior_lsa_gen_end_chkpt_record -
 *
 * return: error code or NO_ERROR
 *
 *   node(in/out):
 *   tran_length(in):
 *   tran_data(in):
 *   topop_length(in):
 *   topop_data(in):
 */
static int
prior_lsa_gen_end_chkpt_record (THREAD_ENTRY *thread_p, LOG_PRIOR_NODE *node, int tran_length, const char *tran_data,
				int topop_length, const char *topop_data)
{
  int error_code = NO_ERROR;

  node->data_header_length = sizeof (LOG_REC_CHKPT);
  node->data_header = (char *) malloc (node->data_header_length);
  if (node->data_header == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) node->data_header_length);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  if (tran_length > 0)
    {
      error_code = prior_lsa_copy_undo_data_to_node (node, tran_length, tran_data);
    }
  if (topop_length > 0)
    {
      error_code = prior_lsa_copy_redo_data_to_node (node, topop_length, topop_data);
    }

  return error_code;
}

/*
 * prior_lsa_gen_record -
 *
 * return: error code or NO_ERROR
 *
 *   node(in/out):
 *   rec_type(in):
 *   length(in):
 *   data(in):
 */
static int
prior_lsa_gen_record (THREAD_ENTRY *thread_p, LOG_PRIOR_NODE *node, LOG_RECTYPE rec_type, int length,
		      const char *data)
{
  int error_code = NO_ERROR;

  node->data_header_length = 0;
  switch (rec_type)
    {
    case LOG_DUMMY_HEAD_POSTPONE:
    case LOG_DUMMY_CRASH_RECOVERY:
    case LOG_DUMMY_OVF_RECORD:
    case LOG_DUMMY_GENERIC:
    case LOG_2PC_COMMIT_DECISION:
    case LOG_2PC_ABORT_DECISION:
    case LOG_2PC_COMMIT_INFORM_PARTICPS:
    case LOG_2PC_ABORT_INFORM_PARTICPS:
    case LOG_START_CHKPT:
    case LOG_SYSOP_ATOMIC_START:
      assert (length == 0 && data == NULL);
      break;

    case LOG_RUN_POSTPONE:
      node->data_header_length = sizeof (LOG_REC_RUN_POSTPONE);
      break;

    case LOG_COMPENSATE:
      node->data_header_length = sizeof (LOG_REC_COMPENSATE);
      break;

    case LOG_DUMMY_HA_SERVER_STATE:
      assert (length == 0 && data == NULL);
      node->data_header_length = sizeof (LOG_REC_HA_SERVER_STATE);
      break;

    case LOG_SAVEPOINT:
      node->data_header_length = sizeof (LOG_REC_SAVEPT);
      break;

    case LOG_COMMIT_WITH_POSTPONE:
      node->data_header_length = sizeof (LOG_REC_START_POSTPONE);
      break;

    case LOG_COMMIT_WITH_POSTPONE_OBSOLETE:
      node->data_header_length = sizeof (LOG_REC_START_POSTPONE_OBSOLETE);
      break;

    case LOG_SYSOP_START_POSTPONE:
      node->data_header_length = sizeof (LOG_REC_SYSOP_START_POSTPONE);
      break;

    case LOG_COMMIT:
    case LOG_ABORT:
      assert (length == 0 && data == NULL);
      node->data_header_length = sizeof (LOG_REC_DONETIME);
      break;

    case LOG_SYSOP_END:
      node->data_header_length = sizeof (LOG_REC_SYSOP_END);
      break;

    case LOG_REPLICATION_DATA:
    case LOG_REPLICATION_STATEMENT:
      node->data_header_length = sizeof (LOG_REC_REPLICATION);
      break;

    case LOG_2PC_START:
      node->data_header_length = sizeof (LOG_REC_2PC_START);
      break;

    case LOG_END_CHKPT:
      node->data_header_length = sizeof (LOG_REC_CHKPT);
      break;
    case LOG_SUPPLEMENTAL_INFO:
      node->data_header_length = sizeof (LOG_REC_SUPPLEMENT);
      break;
    default:
      break;
    }

  if (node->data_header_length > 0)
    {
      node->data_header = (char *) malloc (node->data_header_length);
      if (node->data_header == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) node->data_header_length);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}

#if !defined (NDEBUG)
      /* Suppress valgrind complaint. */
      memset (node->data_header, 0, node->data_header_length);
#endif // DEBUG
    }

  if (length > 0)
    {
      error_code = prior_lsa_copy_undo_data_to_node (node, length, data);
    }

  return error_code;
}

static void
prior_update_header_mvcc_info (const LOG_LSA &record_lsa, MVCCID mvccid)
{
  assert (MVCCID_IS_VALID (mvccid));
  if (!log_Gl.hdr.does_block_need_vacuum)
    {
      // first mvcc record for this block
      log_Gl.hdr.oldest_visible_mvccid = log_Gl.mvcc_table.get_global_oldest_visible ();
      log_Gl.hdr.newest_block_mvccid = mvccid;
    }
  else
    {
      // sanity checks
      assert (MVCCID_IS_VALID (log_Gl.hdr.oldest_visible_mvccid));
      assert (MVCCID_IS_VALID (log_Gl.hdr.newest_block_mvccid));
      assert (log_Gl.hdr.oldest_visible_mvccid <= mvccid);
      assert (!log_Gl.hdr.mvcc_op_log_lsa.is_null ());
      assert (vacuum_get_log_blockid (log_Gl.hdr.mvcc_op_log_lsa.pageid) == vacuum_get_log_blockid (record_lsa.pageid));

      if (log_Gl.hdr.newest_block_mvccid < mvccid)
	{
	  log_Gl.hdr.newest_block_mvccid = mvccid;
	}
    }
  log_Gl.hdr.mvcc_op_log_lsa = record_lsa;
  log_Gl.hdr.does_block_need_vacuum = true;
}

/*
 * prior_lsa_next_record_internal -
 *
 * return: start lsa of log record
 *
 *   node(in/out):
 *   tdes(in/out):
 *   with_lock(in):
 */
static LOG_LSA
prior_lsa_next_record_internal (THREAD_ENTRY *thread_p, LOG_PRIOR_NODE *node, LOG_TDES *tdes, int with_lock)
{
  LOG_LSA start_lsa;
  LOG_REC_MVCC_UNDO *mvcc_undo = NULL;
  LOG_REC_MVCC_UNDOREDO *mvcc_undoredo = NULL;
  LOG_VACUUM_INFO *vacuum_info = NULL;
  MVCCID mvccid = MVCCID_NULL;

  if (with_lock == LOG_PRIOR_LSA_WITHOUT_LOCK)
    {
      log_Gl.prior_info.prior_lsa_mutex.lock ();
    }

  prior_lsa_start_append (thread_p, node, tdes);

  LSA_COPY (&start_lsa, &node->start_lsa);

  if (LOG_ISRESTARTED () && log_Gl.hdr.does_block_need_vacuum)
    {
      assert (!LSA_ISNULL (&log_Gl.hdr.mvcc_op_log_lsa));
      if (vacuum_get_log_blockid (log_Gl.hdr.mvcc_op_log_lsa.pageid) != vacuum_get_log_blockid (start_lsa.pageid))
	{
	  assert (vacuum_get_log_blockid (log_Gl.hdr.mvcc_op_log_lsa.pageid)
		  <= (vacuum_get_log_blockid (start_lsa.pageid) - 1));

	  vacuum_produce_log_block_data (thread_p);
	}
    }

  /* Is this a valid MVCC operations: 1. node must be undoredo/undo type and must have undo data. 2. record index must
   * the index of MVCC operations. */
  if (node->log_header.type == LOG_MVCC_UNDO_DATA || node->log_header.type == LOG_MVCC_UNDOREDO_DATA
      || node->log_header.type == LOG_MVCC_DIFF_UNDOREDO_DATA
      || (node->log_header.type == LOG_SYSOP_END
	  && ((LOG_REC_SYSOP_END *) node->data_header)->type == LOG_SYSOP_END_LOGICAL_MVCC_UNDO))
    {
      /* Link the log record to previous MVCC delete/update log record */
      /* Will be used by vacuum */
      if (node->log_header.type == LOG_MVCC_UNDO_DATA)
	{
	  /* Read from mvcc_undo structure */
	  mvcc_undo = (LOG_REC_MVCC_UNDO *) node->data_header;
	  vacuum_info = &mvcc_undo->vacuum_info;
	  mvccid = mvcc_undo->mvccid;
	}
      else if (node->log_header.type == LOG_SYSOP_END)
	{
	  /* Read from mvcc_undo structure */
	  mvcc_undo = & ((LOG_REC_SYSOP_END *) node->data_header)->mvcc_undo;
	  vacuum_info = &mvcc_undo->vacuum_info;
	  mvccid = mvcc_undo->mvccid;
	}
      else
	{
	  /* Read for mvcc_undoredo structure */
	  assert (node->log_header.type == LOG_MVCC_UNDOREDO_DATA
		  || node->log_header.type == LOG_MVCC_DIFF_UNDOREDO_DATA);

	  mvcc_undoredo = (LOG_REC_MVCC_UNDOREDO *) node->data_header;
	  vacuum_info = &mvcc_undoredo->vacuum_info;
	  mvccid = mvcc_undoredo->mvccid;
	}

      /* Save previous mvcc operation log lsa to vacuum info */
      LSA_COPY (&vacuum_info->prev_mvcc_op_log_lsa, &log_Gl.hdr.mvcc_op_log_lsa);

      vacuum_er_log (VACUUM_ER_LOG_LOGGING,
		     "log mvcc op at (%lld, %d) and create link with log_lsa(%lld, %d)",
		     LSA_AS_ARGS (&node->start_lsa), LSA_AS_ARGS (&log_Gl.hdr.mvcc_op_log_lsa));

      prior_update_header_mvcc_info (start_lsa, mvccid);
    }
  else if (node->log_header.type == LOG_SYSOP_START_POSTPONE)
    {
      /* we need the system operation start postpone LSA for recovery. we have to save it under prior_lsa_mutex
       * protection.
       * at the same time, tdes->rcv.atomic_sysop_start_lsa must be reset if it was inside this system op. */
      LOG_REC_SYSOP_START_POSTPONE *sysop_start_postpone = NULL;

      assert (LSA_ISNULL (&tdes->rcv.sysop_start_postpone_lsa));
      tdes->rcv.sysop_start_postpone_lsa = start_lsa;

      sysop_start_postpone = (LOG_REC_SYSOP_START_POSTPONE *) node->data_header;
      if (LSA_LT (&sysop_start_postpone->sysop_end.lastparent_lsa, &tdes->rcv.atomic_sysop_start_lsa))
	{
	  /* atomic system operation finished. */
	  LSA_SET_NULL (&tdes->rcv.atomic_sysop_start_lsa);
	}

      /* for correct checkpoint, this state change must be done under the protection of prior_lsa_mutex */
      tdes->state = TRAN_UNACTIVE_TOPOPE_COMMITTED_WITH_POSTPONE;
    }
  else if (node->log_header.type == LOG_SYSOP_END)
    {
      /* reset tdes->rcv.sysop_start_postpone_lsa and tdes->rcv.atomic_sysop_start_lsa, if this system op is not nested.
       * we'll use lastparent_lsa to check if system op is nested or not. */
      LOG_REC_SYSOP_END *sysop_end = NULL;

      sysop_end = (LOG_REC_SYSOP_END *) node->data_header;
      if (!LSA_ISNULL (&tdes->rcv.atomic_sysop_start_lsa)
	  && LSA_LT (&sysop_end->lastparent_lsa, &tdes->rcv.atomic_sysop_start_lsa))
	{
	  /* atomic system operation finished. */
	  LSA_SET_NULL (&tdes->rcv.atomic_sysop_start_lsa);
	}
      if (!LSA_ISNULL (&tdes->rcv.sysop_start_postpone_lsa)
	  && LSA_LT (&sysop_end->lastparent_lsa, &tdes->rcv.sysop_start_postpone_lsa))
	{
	  /* atomic system operation finished. */
	  LSA_SET_NULL (&tdes->rcv.sysop_start_postpone_lsa);
	}
    }
  else if (node->log_header.type == LOG_COMMIT_WITH_POSTPONE
	   || node->log_header.type == LOG_COMMIT_WITH_POSTPONE_OBSOLETE)
    {
      /* we need the commit with postpone LSA for recovery. we have to save it under prior_lsa_mutex protection */
      tdes->rcv.tran_start_postpone_lsa = start_lsa;
    }
  else if (node->log_header.type == LOG_SYSOP_ATOMIC_START)
    {
      /* same as with system op start postpone, we need to save these log records lsa */
      assert (LSA_ISNULL (&tdes->rcv.atomic_sysop_start_lsa));
      tdes->rcv.atomic_sysop_start_lsa = start_lsa;
    }
  else if (node->log_header.type == LOG_COMMIT || node->log_header.type == LOG_ABORT)
    {
      /* mark the commit/abort in the transaction,  */
      assert (tdes->commit_abort_lsa.is_null ());
      LSA_COPY (&tdes->commit_abort_lsa, &start_lsa);
    }

  log_prior_lsa_append_advance_when_doesnot_fit (node->data_header_length);
  log_prior_lsa_append_add_align (node->data_header_length);

  if (node->ulength > 0)
    {
      prior_lsa_append_data (node->ulength);
    }

  if (node->rlength > 0)
    {
      prior_lsa_append_data (node->rlength);
    }

  /* END append */
  prior_lsa_end_append (thread_p, node);

  if (log_Gl.prior_info.prior_list_tail == NULL)
    {
      log_Gl.prior_info.prior_list_header = node;
      log_Gl.prior_info.prior_list_tail = node;
    }
  else
    {
      log_Gl.prior_info.prior_list_tail->next = node;
      log_Gl.prior_info.prior_list_tail = node;
    }

  /* list_size in bytes */
  log_Gl.prior_info.list_size += (sizeof (LOG_PRIOR_NODE) + node->data_header_length + node->ulength + node->rlength);

  if (with_lock == LOG_PRIOR_LSA_WITHOUT_LOCK)
    {
      log_Gl.prior_info.prior_lsa_mutex.unlock ();

      if (log_Gl.prior_info.list_size >= (INT64) logpb_get_memsize ())
	{
	  perfmon_inc_stat (thread_p, PSTAT_PRIOR_LSA_LIST_MAXED);

#if defined(SERVER_MODE)
	  if (!log_is_in_crash_recovery ())
	    {
	      log_wakeup_log_flush_daemon ();

	      thread_sleep (1);	/* 1msec */
	    }
	  else
	    {
	      LOG_CS_ENTER (thread_p);
	      logpb_prior_lsa_append_all_list (thread_p);
	      LOG_CS_EXIT (thread_p);
	    }
#else
	  LOG_CS_ENTER (thread_p);
	  logpb_prior_lsa_append_all_list (thread_p);
	  LOG_CS_EXIT (thread_p);
#endif
	}
    }

  tdes->num_log_records_written++;

  return start_lsa;
}

LOG_LSA
prior_lsa_next_record (THREAD_ENTRY *thread_p, LOG_PRIOR_NODE *node, log_tdes *tdes)
{
  return prior_lsa_next_record_internal (thread_p, node, tdes, LOG_PRIOR_LSA_WITHOUT_LOCK);
}

LOG_LSA
prior_lsa_next_record_with_lock (THREAD_ENTRY *thread_p, LOG_PRIOR_NODE *node, log_tdes *tdes)
{
  return prior_lsa_next_record_internal (thread_p, node, tdes, LOG_PRIOR_LSA_WITH_LOCK);
}

int
prior_set_tde_encrypted (log_prior_node *node, LOG_RCVINDEX recvindex)
{
  if (!tde_is_loaded())
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TDE_CIPHER_IS_NOT_LOADED, 0);
      return ER_TDE_CIPHER_IS_NOT_LOADED;
    }

  tde_er_log ("prior_set_tde_encrypted(): rcvindex = %s\n", rv_rcvindex_string (recvindex));

  node->tde_encrypted = true;

  return NO_ERROR;
}

bool
prior_is_tde_encrypted (const log_prior_node *node)
{
  return node->tde_encrypted;
}

/*
 * prior_lsa_start_append:
 *
 *   node(in/out):
 *   tdes(in):
 */
static void
prior_lsa_start_append (THREAD_ENTRY *thread_p, LOG_PRIOR_NODE *node, LOG_TDES *tdes)
{
  /* Does the new log record fit in this page ? */
  log_prior_lsa_append_advance_when_doesnot_fit (sizeof (LOG_RECORD_HEADER));

  node->log_header.trid = tdes->trid;

  /*
   * Link the record with the previous transaction record for quick undos.
   * Link the record backward for backward traversal of the log.
   */
  LSA_COPY (&node->start_lsa, &log_Gl.prior_info.prior_lsa);

  if (tdes->is_system_worker_transaction () && !tdes->is_under_sysop ())
    {
      // lose the link to previous record
      LSA_SET_NULL (&node->log_header.prev_tranlsa);
      LSA_SET_NULL (&tdes->head_lsa);
      LSA_SET_NULL (&tdes->tail_lsa);
    }
  else
    {
      LSA_COPY (&node->log_header.prev_tranlsa, &tdes->tail_lsa);

      LSA_COPY (&tdes->tail_lsa, &log_Gl.prior_info.prior_lsa);

      /*
       * Is this the first log record of transaction ?
       */
      if (LSA_ISNULL (&tdes->head_lsa))
	{
	  LSA_COPY (&tdes->head_lsa, &tdes->tail_lsa);
	}

      LSA_COPY (&tdes->undo_nxlsa, &log_Gl.prior_info.prior_lsa);
    }

  /*
   * Remember the address of new append record
   */
  LSA_COPY (&node->log_header.back_lsa, &log_Gl.prior_info.prev_lsa);
  LSA_SET_NULL (&node->log_header.forw_lsa);

  LSA_COPY (&log_Gl.prior_info.prev_lsa, &log_Gl.prior_info.prior_lsa);

  /*
   * Set the page dirty, increase and align the append offset
   */
  log_prior_lsa_append_add_align (sizeof (LOG_RECORD_HEADER));
}

/*
 * prior_lsa_end_append -
 *
 * return:
 *
 *   node(in/out):
 */
static void
prior_lsa_end_append (THREAD_ENTRY *thread_p, LOG_PRIOR_NODE *node)
{
  log_prior_lsa_append_align ();
  log_prior_lsa_append_advance_when_doesnot_fit (sizeof (LOG_RECORD_HEADER));

  LSA_COPY (&node->log_header.forw_lsa, &log_Gl.prior_info.prior_lsa);
}

static void
prior_lsa_append_data (int length)
{
  int copy_length;		/* Amount of contiguous data that can be copied */
  int current_offset;
  int last_offset;

  if (length == 0)
    {
      return;
    }

  /*
   * Align if needed,
   * don't set it dirty since this function has not updated
   */
  log_prior_lsa_append_align ();

  current_offset = (int) log_Gl.prior_info.prior_lsa.offset;
  last_offset = (int) LOG_PRIOR_LSA_LAST_APPEND_OFFSET ();

  /* Does data fit completely in current page ? */
  if ((current_offset + length) >= last_offset)
    {
      while (length > 0)
	{
	  if (current_offset >= last_offset)
	    {
	      /*
	       * Get next page and set the current one dirty
	       */
	      log_Gl.prior_info.prior_lsa.pageid++;
	      log_Gl.prior_info.prior_lsa.offset = 0;

	      current_offset = 0;
	      last_offset = (int) LOG_PRIOR_LSA_LAST_APPEND_OFFSET ();
	    }
	  /* Find the amount of contiguous data that can be copied */
	  if (current_offset + length >= last_offset)
	    {
	      copy_length = CAST_BUFLEN (last_offset - current_offset);
	    }
	  else
	    {
	      copy_length = length;
	    }

	  current_offset += copy_length;
	  length -= copy_length;
	  log_Gl.prior_info.prior_lsa.offset += copy_length;
	}
    }
  else
    {
      log_Gl.prior_info.prior_lsa.offset += length;
    }

  /*
   * Align the data for future appends.
   * Indicate that modifications were done
   */
  log_prior_lsa_append_align ();
}

LOG_ZIP *
log_append_get_zip_undo (THREAD_ENTRY *thread_p)
{
#if defined (SERVER_MODE)
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  if (thread_p == NULL)
    {
      return NULL;
    }
  else
    {
      if (thread_p->log_zip_undo == NULL)
	{
	  thread_p->log_zip_undo = log_zip_alloc (IO_PAGESIZE);
	}
      return (LOG_ZIP *) thread_p->log_zip_undo;
    }
#else
  return log_zip_undo;
#endif
}

LOG_ZIP *
log_append_get_zip_redo (THREAD_ENTRY *thread_p)
{
#if defined (SERVER_MODE)
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  if (thread_p == NULL)
    {
      return NULL;
    }
  else
    {
      if (thread_p->log_zip_redo == NULL)
	{
	  thread_p->log_zip_redo = log_zip_alloc (IO_PAGESIZE);
	}
      return (LOG_ZIP *) thread_p->log_zip_redo;
    }
#else
  return log_zip_redo;
#endif
}

/*
 * log_append_realloc_data_ptr -
 *
 * return:
 *
 *   data_length(in):
 *   length(in):
 *
 * NOTE:
 */
static bool
log_append_realloc_data_ptr (THREAD_ENTRY *thread_p, int length)
{
  char *data_ptr;
  int alloc_len;
#if defined (SERVER_MODE)
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  if (thread_p == NULL)
    {
      return false;
    }

  if (thread_p->log_data_length < length)
    {
      alloc_len = ((int) CEIL_PTVDIV (length, IO_PAGESIZE)) * IO_PAGESIZE;

      data_ptr = (char *) realloc (thread_p->log_data_ptr, alloc_len);
      if (data_ptr == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) alloc_len);
	  if (thread_p->log_data_ptr)
	    {
	      free_and_init (thread_p->log_data_ptr);
	    }
	  thread_p->log_data_length = 0;
	  return false;
	}
      else
	{
	  thread_p->log_data_ptr = data_ptr;
	  thread_p->log_data_length = alloc_len;
	}
    }
  return true;
#else
  if (log_data_length < length)
    {
      alloc_len = ((int) CEIL_PTVDIV (length, IO_PAGESIZE)) * IO_PAGESIZE;

      data_ptr = (char *) realloc (log_data_ptr, alloc_len);
      if (data_ptr == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) alloc_len);
	  if (log_data_ptr)
	    {
	      free_and_init (log_data_ptr);
	    }
	  log_data_length = 0;
	  return false;
	}
      else
	{
	  log_data_ptr = data_ptr;
	  log_data_length = alloc_len;
	}
    }

  return true;
#endif
}

/*
 * log_append_get_data_ptr  -
 *
 * return:
 *
 */
static char *
log_append_get_data_ptr (THREAD_ENTRY *thread_p)
{
#if defined (SERVER_MODE)
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  if (thread_p == NULL)
    {
      return NULL;
    }
  else
    {
      if (thread_p->log_data_ptr == NULL)
	{
	  thread_p->log_data_length = IO_PAGESIZE * 2;
	  thread_p->log_data_ptr = (char *) malloc (thread_p->log_data_length);

	  if (thread_p->log_data_ptr == NULL)
	    {
	      thread_p->log_data_length = 0;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
		      (size_t) thread_p->log_data_length);
	    }
	}
      return thread_p->log_data_ptr;
    }
#else
  return log_data_ptr;
#endif
}

static void
log_prior_lsa_append_align ()
{
  assert (log_Gl.prior_info.prior_lsa.offset >= 0);

  log_Gl.prior_info.prior_lsa.offset = DB_ALIGN (log_Gl.prior_info.prior_lsa.offset, DOUBLE_ALIGNMENT);
  if ((size_t) log_Gl.prior_info.prior_lsa.offset >= (size_t) LOGAREA_SIZE)
    {
      log_Gl.prior_info.prior_lsa.pageid++;
      log_Gl.prior_info.prior_lsa.offset = 0;
    }
}

static void
log_prior_lsa_append_advance_when_doesnot_fit (size_t length)
{
  assert (log_Gl.prior_info.prior_lsa.offset >= 0);

  if ((size_t) log_Gl.prior_info.prior_lsa.offset + length >= (size_t) LOGAREA_SIZE)
    {
      log_Gl.prior_info.prior_lsa.pageid++;
      log_Gl.prior_info.prior_lsa.offset = 0;
    }
}

static void
log_prior_lsa_append_add_align (size_t add)
{
  assert (log_Gl.prior_info.prior_lsa.offset >= 0);

  log_Gl.prior_info.prior_lsa.offset += (add);
  log_prior_lsa_append_align ();
}
