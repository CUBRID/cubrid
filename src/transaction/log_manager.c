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
 * log_manager.c -
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdlib.h>
#include <time.h>
#if defined(SOLARIS)
#include <netdb.h>
#endif /* SOLARIS */
#include <sys/stat.h>
#include <assert.h>
#if defined(WINDOWS)
#include <io.h>
#endif /* WINDOWS */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "porting.h"
#include "log_manager.h"
#include "log_impl.h"
#include "log_comm.h"
#include "recovery.h"
#if defined(SA_MODE)
#include "recovery_cl.h"
#endif /* SA_MODE */
#include "lock_manager.h"
#include "memory_alloc.h"
#include "storage_common.h"
#include "release_string.h"
#include "system_parameter.h"
#include "error_manager.h"
#include "xserver_interface.h"
#include "page_buffer.h"
#include "file_io.h"
#include "disk_manager.h"
#include "file_manager.h"
#include "query_manager.h"
#include "message_catalog.h"
#include "environment_variable.h"
#include "wait_for_graph.h"
#if defined(SERVER_MODE)
#include "connection_defs.h"
#include "connection_error.h"
#include "thread.h"
#include "job_queue.h"
#include "server_support.h"
#endif /* SERVER_MODE */
#include "log_compress.h"
#include "object_print.h"
#include "replication.h"
#include "es.h"
#include "memory_hash.h"
#include "partition.h"
#include "connection_support.h"
#include "log_writer.h"

#if !defined(SERVER_MODE)

#define pthread_mutex_init(a, b)
#define pthread_mutex_destroy(a)
#define pthread_mutex_lock(a)	0
#define pthread_mutex_unlock(a)
static int rv;
#endif /* !SERVER_MODE */

/*
 *
 *                      IS TIME TO EXECUTE A CHECKPOINT ?
 *
 */

/* A checkpoint is taken after a set of log pages has been used */

#define LOG_ISCHECKPOINT_TIME() \
  (log_Gl.rcv_phase == LOG_RESTARTED \
   && log_Gl.run_nxchkpt_atpageid != NULL_PAGEID \
   && log_Gl.hdr.append_lsa.pageid >= log_Gl.run_nxchkpt_atpageid)

#if defined(SERVER_MODE)
#define LOG_FLUSH_LOGGING_HAS_BEEN_SKIPPED(thread_p) \
  do { \
   if (log_Gl.hdr.has_logging_been_skipped != true) { \
     /* Write in the log header that logging has been skipped */ \
      LOG_CS_ENTER((thread_p)); \
      if (log_Gl.hdr.has_logging_been_skipped != true) { \
	log_Gl.hdr.has_logging_been_skipped = true; \
	logpb_flush_header((thread_p));	\
      } \
      LOG_CS_EXIT(thread_p); \
    } \
  } while (0)
#else /* SERVER_MODE */
#define LOG_FLUSH_LOGGING_HAS_BEEN_SKIPPED(thread_p) \
  do { \
   if (log_Gl.hdr.has_logging_been_skipped != true) { \
     /* Write in the log header that logging has been skipped */ \
      log_Gl.hdr.has_logging_been_skipped = true; \
      logpb_flush_header((thread_p)); \
    } \
  } while (0)
#endif /* SERVER_MODE */

  /*
   * Some log record rcvindex types should never be skipped.
   * In the case of LINK_PERM_VOLEXT, the link of a permanent temp
   * volume must be logged to support media failures.
   * See also canskip_undo. If there are others, add them here.
   */
#define LOG_ISUNSAFE_TO_SKIP_RCVINDEX(RCVI) \
   ((RCVI) == RVDK_LINK_PERM_VOLEXT)

#define LOG_NEED_TO_SET_LSA(RCVI, PGPTR) \
   (!LOG_IS_VACUUM_DATA_RECOVERY (RCVI) \
    && ((RCVI) != RVBT_MVCC_INCREMENTS_UPD) \
    && ((RCVI) != RVDK_LINK_PERM_VOLEXT || !pgbuf_is_lsa_temporary(PGPTR)))

/* Assume that locator end with <path>/<meta_name>.<key_name> */
#define LOCATOR_KEY(locator_) (strrchr (locator_, '.') + 1)
#define LOCATOR_META(locator_) (strrchr (locator_, PATH_SEPARATOR) + 1)
#define PUT_LOCATOR_META(meta_name_, locator_) \
   do { \
     char *key_, *meta_; \
     key_ = LOCATOR_KEY (locator_); \
     meta_ = LOCATOR_META (locator_); \
     memcpy (meta_name_, meta_, (key_ - meta_) -1); \
     meta_name_[(key_ - meta_) -1] = '\0'; \
   } while (0)

/* definitions for lob locator tree */
typedef struct lob_savepoint_entry LOB_SAVEPOINT_ENTRY;

struct lob_savepoint_entry
{
  LOB_LOCATOR_STATE state;
  LOG_LSA savept_lsa;
  LOB_SAVEPOINT_ENTRY *prev;
  ES_URI locator;
};

struct lob_locator_entry
{
  /* RB_ENTRY defines red-black tree node header for this structure.
     see base/rb_tree.h for more information. */
  RB_ENTRY (lob_locator_entry) head;
  LOB_SAVEPOINT_ENTRY *top;
  /* key_hash is used to reduce the number of strcmp.
     see the comment of lob_locator_cmp for more information */
  int key_hash;
  /* normal case: points &key_data[0], search key: supplied */
  char *key;
  char key_data[1];
};

/* struct for active log header scan */
typedef struct actve_log_header_scan_context ACTIVE_LOG_HEADER_SCAN_CTX;
struct actve_log_header_scan_context
{
  LOG_HEADER header;
};

/* struct for archive log header scan */
typedef struct archive_log_header_scan_context ARCHIVE_LOG_HEADER_SCAN_CTX;
struct archive_log_header_scan_context
{
  struct log_arv_header header;
};

bool mvcc_Enabled;

/*
 * The maximum number of times to try to undo a log record.
 * It is only used by the log_undo_rec_restartable() function.
 */
static const int LOG_REC_UNDO_MAX_ATTEMPTS = 3;

/* true: Skip logging, false: Don't skip logging */
static bool log_No_logging = false;

static bool log_verify_dbcreation (THREAD_ENTRY * thread_p, VOLID volid,
				   const INT64 * log_dbcreation);
static void log_create_internal (THREAD_ENTRY * thread_p,
				 const char *db_fullname, const char *logpath,
				 const char *prefix_logname, DKNPAGES npages,
				 INT64 * db_creation);
static int log_initialize_internal (THREAD_ENTRY * thread_p,
				    const char *db_fullname,
				    const char *logpath,
				    const char *prefix_logname,
				    int ismedia_crash, time_t * stopat,
				    bool init_emergency);
#if defined(SERVER_MODE)
static int log_abort_by_tdes (THREAD_ENTRY * thread_p, LOG_TDES * tdes);
#endif /* SERVER_MODE */
static void log_append_client_name (THREAD_ENTRY * thread_p, LOG_TDES * tdes);
static LOG_LSA *log_get_savepoint_lsa (THREAD_ENTRY * thread_p,
				       const char *savept_name,
				       LOG_TDES * tdes, LOG_LSA * savept_lsa);
static bool log_can_skip_undo_logging (THREAD_ENTRY * thread_p,
				       LOG_RCVINDEX rcvindex,
				       const LOG_TDES * tdes,
				       LOG_DATA_ADDR * addr);
static bool log_can_skip_redo_logging (LOG_RCVINDEX rcvindex,
				       const LOG_TDES * ignore_tdes,
				       LOG_DATA_ADDR * addr);
static void log_append_commit_postpone (THREAD_ENTRY * thread_p,
					LOG_TDES * tdes,
					LOG_LSA * start_postpone_lsa);
static void log_append_topope_commit_postpone (THREAD_ENTRY * thread_p,
					       LOG_TDES * tdes,
					       LOG_LSA * start_postpone_lsa);
static void log_append_topope_commit_client_loose_ends (THREAD_ENTRY *
							thread_p,
							LOG_TDES * tdes);
static void log_append_topope_abort_client_loose_ends (THREAD_ENTRY *
						       thread_p,
						       LOG_TDES * tdes);
static void log_append_repl_info_internal (THREAD_ENTRY * thread_p,
					   LOG_TDES * tdes, bool is_commit,
					   int with_lock);
static void log_append_repl_info (THREAD_ENTRY * thread_p, LOG_TDES * tdes,
				  bool is_commit);
static void log_append_repl_info_with_lock (THREAD_ENTRY * thread_p,
					    LOG_TDES * tdes, bool is_commit);
static void log_append_unlock_log_internal (THREAD_ENTRY * thread_p,
					    LOG_TDES * tdes, int with_lock);
static void log_append_unlock_log (THREAD_ENTRY * thread_p, LOG_TDES * tdes);
static void log_append_unlock_log_with_lock (THREAD_ENTRY * thread_p,
					     LOG_TDES * tdes);
static void log_append_repl_info_and_unlock_log (THREAD_ENTRY * thread_p,
						 LOG_TDES * tdes);
static void log_append_donetime (THREAD_ENTRY * thread_p, LOG_TDES * tdes,
				 LOG_RECTYPE iscommitted);
static void log_rollback_classrepr_cache (THREAD_ENTRY * thread_p,
					  LOG_TDES * tdes,
					  LOG_LSA * upto_lsa);
static void log_free_lob_locator (LOB_LOCATOR_ENTRY * entry);
static int lob_locator_cmp (const LOB_LOCATOR_ENTRY * e1,
			    const LOB_LOCATOR_ENTRY * e2);
/* RB_PROTOTYPE_STATIC declares red-black tree functions. see base/rb_tree.h */
RB_PROTOTYPE_STATIC (lob_rb_root, lob_locator_entry, head, lob_locator_cmp);

static TRAN_STATE log_complete_system_op (THREAD_ENTRY * thread_p,
					  LOG_TDES * tdes,
					  LOG_RESULT_TOPOP result,
					  TRAN_STATE back_to_state);
#if defined(CUBRID_DEBUG)
static void
log_client_find_system_error (LOG_RECTYPE record_type,
			      LOG_RECTYPE client_type);
#endif
static LOG_COPY *log_client_find_actions (THREAD_ENTRY * thread_p,
					  LOG_TDES * tdes, LOG_LSA * next_lsa,
					  LOG_RECTYPE type);
static void log_client_append_done_actions (THREAD_ENTRY * thread_p,
					    LOG_TDES * tdes,
					    LOG_RECTYPE rectype,
					    LOG_LSA * next_lsa);
static void log_dump_record_header_to_string (LOG_RECORD_HEADER * log,
					      char *buf, size_t len);
static void log_ascii_dump (FILE * out_fp, int length, void *data);
static void log_dump_data (THREAD_ENTRY * thread_p, FILE * out_fp, int length,
			   LOG_LSA * log_lsa, LOG_PAGE * log_page_p,
			   void (*dumpfun) (FILE * fp, int, void *),
			   LOG_ZIP * log_dump_ptr);
static void log_dump_header (FILE * out_fp, struct log_header *log_header_p);
static LOG_PAGE *log_dump_record_client_name (THREAD_ENTRY * thread_p,
					      FILE * out_fp, LOG_LSA * lsa_p,
					      LOG_PAGE * log_pgptr);
static LOG_PAGE *log_dump_record_undoredo (THREAD_ENTRY * thread_p,
					   FILE * out_fp, LOG_LSA * lsa_p,
					   LOG_PAGE * log_page_p,
					   LOG_ZIP * log_zip_p);
static LOG_PAGE *log_dump_record_undo (THREAD_ENTRY * thread_p, FILE * out_fp,
				       LOG_LSA * lsa_p, LOG_PAGE * log_page_p,
				       LOG_ZIP * log_zip_p);
static LOG_PAGE *log_dump_record_redo (THREAD_ENTRY * thread_p, FILE * out_fp,
				       LOG_LSA * lsa_p, LOG_PAGE * log_page_p,
				       LOG_ZIP * log_zip_p);
static LOG_PAGE *log_dump_record_mvcc_undoredo (THREAD_ENTRY * thread_p,
						FILE * out_fp,
						LOG_LSA * lsa_p,
						LOG_PAGE * log_page_p,
						LOG_ZIP * log_zip_p);
static LOG_PAGE *log_dump_record_mvcc_undo (THREAD_ENTRY * thread_p,
					    FILE * out_fp,
					    LOG_LSA * lsa_p,
					    LOG_PAGE * log_page_p,
					    LOG_ZIP * log_zip_p);
static LOG_PAGE *log_dump_record_mvcc_redo (THREAD_ENTRY * thread_p,
					    FILE * out_fp,
					    LOG_LSA * lsa_p,
					    LOG_PAGE * log_page_p,
					    LOG_ZIP * log_zip_p);
static LOG_PAGE *log_dump_record_postpone (THREAD_ENTRY * thread_p,
					   FILE * out_fp, LOG_LSA * lsa_p,
					   LOG_PAGE * log_page_p);
static LOG_PAGE *log_dump_record_dbout_redo (THREAD_ENTRY * thread_p,
					     FILE * out_fp, LOG_LSA * lsa_p,
					     LOG_PAGE * log_page_p);
static LOG_PAGE *log_dump_record_compensate (THREAD_ENTRY * thread_p,
					     FILE * out_fp, LOG_LSA * lsa_p,
					     LOG_PAGE * log_page_p);
static LOG_PAGE *log_dump_record_logical_compensate (THREAD_ENTRY * thread_p,
						     FILE * out_fp,
						     LOG_LSA * lsa_p,
						     LOG_PAGE * log_page_p);
static LOG_PAGE *log_dump_record_client_user_undo (THREAD_ENTRY * thread_p,
						   FILE * out_fp,
						   LOG_LSA * lsa_p,
						   LOG_PAGE * log_page_p);
static LOG_PAGE *log_dump_record_client_user_postpone (THREAD_ENTRY *
						       thread_p,
						       FILE * out_fp,
						       LOG_LSA * lsa_p,
						       LOG_PAGE * log_page_p);
static LOG_PAGE *log_dump_record_next_client_undo (THREAD_ENTRY * thread_p,
						   FILE * out_fp,
						   LOG_LSA * lsa_p,
						   LOG_PAGE * log_page_p);
static LOG_PAGE *log_dump_record_commit_postpone (THREAD_ENTRY * thread_p,
						  FILE * out_fp,
						  LOG_LSA * lsa_p,
						  LOG_PAGE * log_page_p);
static LOG_PAGE *log_dump_record_commit_client_loose_end (THREAD_ENTRY *
							  thread_p,
							  FILE * out_fp,
							  LOG_LSA * lsa_p,
							  LOG_PAGE *
							  log_page_p);
static LOG_PAGE *log_dump_record_transaction_finish (THREAD_ENTRY * thread_p,
						     FILE * out_fp,
						     LOG_LSA * lsa_p,
						     LOG_PAGE * log_page_p);
static LOG_PAGE *log_dump_record_replication (THREAD_ENTRY * thread_p,
					      FILE * out_fp, LOG_LSA * lsa_p,
					      LOG_PAGE * log_page_p);
static LOG_PAGE *log_dump_record_commit_topope_postpone (THREAD_ENTRY *
							 thread_p,
							 FILE * out_fp,
							 LOG_LSA * lsa_p,
							 LOG_PAGE *
							 log_page_p);
static LOG_PAGE *log_dump_record_commit_loose_end (THREAD_ENTRY * thread_p,
						   FILE * out_fp,
						   LOG_LSA * lsa_p,
						   LOG_PAGE * log_page_p);
static LOG_PAGE *log_dump_record_abort_loose_end (THREAD_ENTRY * thread_p,
						  FILE * out_fp,
						  LOG_LSA * lsa_p,
						  LOG_PAGE * log_page_p);
static LOG_PAGE *log_dump_record_topope_finish (THREAD_ENTRY * thread_p,
						FILE * out_fp,
						LOG_LSA * lsa_p,
						LOG_PAGE * log_page_p);
static LOG_PAGE *log_dump_record_abort_client_loose_end (THREAD_ENTRY *
							 thread_p,
							 FILE * out_fp,
							 LOG_LSA * lsa_p,
							 LOG_PAGE *
							 log_page_p);
static LOG_PAGE *log_dump_record_checkpoint (THREAD_ENTRY * thread_p,
					     FILE * out_fp, LOG_LSA * lsa_p,
					     LOG_PAGE * log_page_p);
static LOG_PAGE *log_dump_record_save_point (THREAD_ENTRY * thread_p,
					     FILE * out_fp, LOG_LSA * lsa_p,
					     LOG_PAGE * log_page_p);
static LOG_PAGE *log_dump_record_2pc_prepare_commit (THREAD_ENTRY * thread_p,
						     FILE * out_fp,
						     LOG_LSA * lsa_p,
						     LOG_PAGE * log_page_p);
static LOG_PAGE *log_dump_record_2pc_start (THREAD_ENTRY * thread_p,
					    FILE * out_fp, LOG_LSA * lsa_p,
					    LOG_PAGE * log_page_p);
static LOG_PAGE *log_dump_record_2pc_acknowledgement (THREAD_ENTRY * thread_p,
						      FILE * out_fp,
						      LOG_LSA * lsa_p,
						      LOG_PAGE * log_page_p);
static LOG_PAGE *log_dump_record_ha_server_state (THREAD_ENTRY * thread_p,
						  FILE * out_fp,
						  LOG_LSA * log_lsa,
						  LOG_PAGE * log_page_p);
static LOG_PAGE *log_dump_record (THREAD_ENTRY * thread_p, FILE * out_fp,
				  LOG_RECTYPE record_type, LOG_LSA * lsa_p,
				  LOG_PAGE * log_page_p, LOG_ZIP * log_zip_p);
static void log_rollback_record (THREAD_ENTRY * thread_p, LOG_LSA * log_lsa,
				 LOG_PAGE * log_page_p, LOG_RCVINDEX rcvindex,
				 VPID * rcv_vpid, LOG_RCV * rcv,
				 LOG_TDES * tdes, LOG_ZIP * log_unzip_ptr);
static int log_undo_rec_restartable (THREAD_ENTRY * thread_p,
				     LOG_RCVINDEX rcvindex, LOG_RCV * rcv);
static void log_rollback (THREAD_ENTRY * thread_p, LOG_TDES * tdes,
			  const LOG_LSA * upto_lsa_ptr);
static int log_run_postpone_op (THREAD_ENTRY * thread_p, LOG_LSA * log_lsa,
				LOG_PAGE * log_pgptr);
static void log_find_end_log (THREAD_ENTRY * thread_p, LOG_LSA * end_lsa);
static TRAN_STATE
log_complete_topop (THREAD_ENTRY * thread_p, LOG_TDES * tdes,
		    LOG_RESULT_TOPOP result);
static void log_complete_topop_attach (LOG_TDES * tdes);

static int log_is_valid_locator (const char *locator);

static bool log_is_class_being_modified_internal (THREAD_ENTRY * thread_p,
						  const OID * class_oid);
static void log_cleanup_modified_class (THREAD_ENTRY * thread_p,
					MODIFIED_CLASS_ENTRY * class,
					void *arg);
static void log_map_modified_class_list (THREAD_ENTRY * thread_p,
					 LOG_TDES * tdes, bool release,
					 void
					 (*map_func) (THREAD_ENTRY * thread_p,
						      MODIFIED_CLASS_ENTRY *
						      class, void *arg),
					 void *arg);
static void log_cleanup_modified_class_list (THREAD_ENTRY * thread_p,
					     LOG_TDES * tdes, bool release,
					     bool decache_classrepr);

/*
 * log_rectype_string - RETURN TYPE OF LOG RECORD IN STRING FORMAT
 *
 * return:
 *
 *   type(in): Type of log record
 *
 * NOTE: Return the type of the log record in string format
 */
const char *
log_to_string (LOG_RECTYPE type)
{
  switch (type)
    {
    case LOG_CLIENT_NAME:
      return "LOG_CLIENT_NAME";

    case LOG_UNDOREDO_DATA:
      return "LOG_UNDOREDO_DATA";

    case LOG_DIFF_UNDOREDO_DATA:	/* LOG DIFF undo and redo data */
      return "LOG_DIFF_UNDOREDO_DATA";

    case LOG_UNDO_DATA:
      return "LOG_UNDO_DATA";

    case LOG_REDO_DATA:
      return "LOG_REDO_DATA";

    case LOG_MVCC_UNDOREDO_DATA:
      return "LOG_MVCC_UNDOREDO_DATA";

    case LOG_MVCC_DIFF_UNDOREDO_DATA:
      return "LOG_MVCC_DIFF_UNDOREDO_DATA";

    case LOG_MVCC_UNDO_DATA:
      return "LOG_MVCC_UNDO_DATA";

    case LOG_MVCC_REDO_DATA:
      return "LOG_MVCC_REDO_DATA";

    case LOG_DBEXTERN_REDO_DATA:
      return "LOG_DBEXTERN_REDO_DATA";

    case LOG_DUMMY_HEAD_POSTPONE:
      return "LOG_DUMMY_HEAD_POSTPONE";

    case LOG_POSTPONE:
      return "LOG_POSTPONE";

    case LOG_RUN_POSTPONE:
      return "LOG_RUN_POSTPONE";

    case LOG_COMPENSATE:
      return "LOG_COMPENSATE";

    case LOG_LCOMPENSATE:
      return "LOG_LCOMPENSATE";

    case LOG_CLIENT_USER_UNDO_DATA:
      return "LOG_CLIENT_USER_UNDO_DATA";

    case LOG_CLIENT_USER_POSTPONE_DATA:
      return "LOG_CLIENT_USER_POSTPONE_DATA";

    case LOG_RUN_NEXT_CLIENT_UNDO:
      return "LOG_RUN_NEXT_CLIENT_UNDO";

    case LOG_RUN_NEXT_CLIENT_POSTPONE:
      return "LOG_RUN_NEXT_CLIENT_POSTPONE";

    case LOG_WILL_COMMIT:
      return "LOG_WILL_COMMIT";

    case LOG_COMMIT_WITH_POSTPONE:
      return "LOG_COMMIT_WITH_POSTPONE";

    case LOG_COMMIT_WITH_CLIENT_USER_LOOSE_ENDS:
      return "LOG_COMMIT_WITH_CLIENT_USER_LOOSE_ENDS";

    case LOG_COMMIT:
      return "LOG_COMMIT";

    case LOG_COMMIT_TOPOPE_WITH_POSTPONE:
      return "LOG_COMMIT_TOPOPE_WITH_POSTPONE";

    case LOG_COMMIT_TOPOPE_WITH_CLIENT_USER_LOOSE_ENDS:
      return "LOG_COMMIT_TOPOPE_WITH_CLIENT_USER_LOOSE_ENDS";

    case LOG_COMMIT_TOPOPE:
      return "LOG_COMMIT_TOPOPE";

    case LOG_ABORT_WITH_CLIENT_USER_LOOSE_ENDS:
      return "LOG_ABORT_WITH_CLIENT_USER_LOOSE_ENDS";

    case LOG_ABORT:
      return "LOG_ABORT";

    case LOG_ABORT_TOPOPE_WITH_CLIENT_USER_LOOSE_ENDS:
      return "LOG_ABORT_TOPOPE_WITH_CLIENT_USER_LOOSE_ENDS";

    case LOG_ABORT_TOPOPE:
      return "LOG_ABORT_TOPOPE";

    case LOG_START_CHKPT:
      return "LOG_START_CHKPT";

    case LOG_END_CHKPT:
      return "LOG_END_CHKPT";

    case LOG_SAVEPOINT:
      return "LOG_SAVEPOINT";

    case LOG_2PC_PREPARE:
      return "LOG_2PC_PREPARE";

    case LOG_2PC_START:
      return "LOG_2PC_START";

    case LOG_2PC_COMMIT_DECISION:
      return "LOG_2PC_COMMIT_DECISION";

    case LOG_2PC_ABORT_DECISION:
      return "LOG_2PC_ABORT_DECISION";

    case LOG_2PC_COMMIT_INFORM_PARTICPS:
      return "LOG_2PC_COMMIT_INFORM_PARTICPS";

    case LOG_2PC_ABORT_INFORM_PARTICPS:
      return "LOG_2PC_ABORT_INFORM_PARTICPS";

    case LOG_2PC_RECV_ACK:
      return "LOG_2PC_RECV_ACK";

    case LOG_DUMMY_CRASH_RECOVERY:
      return "LOG_DUMMY_CRASH_RECOVERY";

      /*
       * This record is not generated no more.
       * It's kept for backward compatibility.
       */
    case LOG_DUMMY_FILLPAGE_FORARCHIVE:
      return "LOG_DUMMY_FILLPAGE_FORARCHIVE";

    case LOG_END_OF_LOG:
      return "LOG_END_OF_LOG";

    case LOG_REPLICATION_DATA:
      return "LOG_REPLICATION_DATA";
    case LOG_REPLICATION_SCHEMA:
      return "LOG_REPLICATION_SCHEMA";
    case LOG_UNLOCK_COMMIT:
      return "LOG_UNLOCK_COMMIT";
    case LOG_UNLOCK_ABORT:
      return "LOG_UNLOCK_ABORT";

    case LOG_DUMMY_HA_SERVER_STATE:
      return "LOG_DUMMY_HA_SERVER_STATE";
    case LOG_DUMMY_OVF_RECORD:
      return "LOG_DUMMY_OVF_RECORD";

    case LOG_SMALLER_LOGREC_TYPE:
    case LOG_LARGER_LOGREC_TYPE:
      break;
    }

  return "UNKNOWN_LOG_REC_TYPE";

}

/*
 * log_isin_crash_recovery - are we in crash recovery ?
 *
 * return:
 *
 * NOTE: Are we in crash recovery time ?
 */
bool
log_is_in_crash_recovery (void)
{
  if (LOG_ISRESTARTED ())
    {
      return false;
    }
  else
    {
      return true;
    }
}


/*
 * log_get_restart_lsa - FIND RESTART LOG SEQUENCE ADDRESS
 *
 * return:
 *
 * NOTE: Find the restart log sequence address.
 */
LOG_LSA *
log_get_restart_lsa (void)
{
  if (LOG_ISRESTARTED ())
    {
      return &log_Gl.rcv_phase_lsa;
    }
  else
    {
      return &log_Gl.hdr.chkpt_lsa;
    }
}

/*
 * log_get_crash_point_lsa - get last lsa address of the log before a crash
 *
 * return:
 *
 * NOTE: Find the log sequence address at the time of a crash.  This
 *   function can only be called during the recovery phases after analysis
 *   and prior to RESTART.
 */
LOG_LSA *
log_get_crash_point_lsa (void)
{
#if defined(CUBRID_DEBUG)
  if (log_Gl.rcv_phase <= LOG_RECOVERY_ANALYSIS_PHASE)
    {
      /* i.e. cannot be RESTARTED or ANALYSIS */
      er_log_debug (ARG_FILE_LINE,
		    "log_find_crash_point_lsa: Warning, only expected "
		    "to be called during recovery phases.");
    }
#endif /* CUBRID_DEBUG */

  return (&log_Gl.rcv_phase_lsa);
}

/*
 * log_find_find_lsa -
 *
 * return:
 *
 * NOTE:
 */
LOG_LSA *
log_get_append_lsa (void)
{
  return (&log_Gl.hdr.append_lsa);
}


/*
 * log_get_eof_lsa -
 *
 * return:
 *
 * NOTE:
 */
LOG_LSA *
log_get_eof_lsa (void)
{
  return (&log_Gl.hdr.eof_lsa);
}

/*
 * log_is_logged_since_restart - is log sequence address made after restart ?
 *
 * return:
 *
 *   lsa_ptr(in): Log sequence address attached to page
 *
 * NOTE: Find if the log sequence address has been made after restart.
 *              This function is useful to detect bugs. For example, when a
 *              data page (actually a buffer)is freed, and the page is dirty,
 *              there should be a log record for some data of the page,
 *              otherwise, a potential error exists. It is clear that this
 *              function will not detect all kinds of errors, but it will help
 *              some.
 */
bool
log_is_logged_since_restart (const LOG_LSA * lsa_ptr)
{
  return (!LOG_ISRESTARTED () || LSA_LE (&log_Gl.rcv_phase_lsa, lsa_ptr));
}

#if defined(SA_MODE)
/*
 * log_get_final_restored_lsa -
 *
 * return:
 *
 * NOTE:
 */
LOG_LSA *
log_get_final_restored_lsa (void)
{
  return (&log_Gl.final_restored_lsa);
}
#endif /* SA_MODE */

/*
 * FUNCTION RELATED TO INITIALIZATION AND TERMINATION OF LOG MANAGER
 */

/*
 * log_verify_dbcreation - verify database creation time
 *
 * return:
 *
 *   volid(in): Volume identifier
 *   log_dbcreation(in): Database creation time according to the log.
 *
 * NOTE:Verify if database creation time according to the log matches
 *              the one according to the database volume. If they do not, it
 *              is likely that the log and data volume does not correspond to
 *              the same database.
 */
static bool
log_verify_dbcreation (THREAD_ENTRY * thread_p, VOLID volid,
		       const INT64 * log_dbcreation)
{
  INT64 vol_dbcreation;		/* Database creation time in volume */

  if (disk_get_creation_time (thread_p, volid, &vol_dbcreation) != NO_ERROR)
    {
      return false;
    }

  if (difftime ((time_t) vol_dbcreation, (time_t) * log_dbcreation) == 0)
    {
      return true;
    }
  else
    {
      return false;
    }
}

/*
 * log_get_db_start_parameters - Get start parameters
 *
 * return: nothing
 *
 *   db_creation(out): Database creation time
 *   chkpt_lsa(out): Last checkpoint address
 *
 * NOTE: Get the start parameters: database creation time and the last
 *              checkpoint process.
 *              For safety reasons, the database creation time is included, in
 *              all database volumes and the log. This value allows verifying
 *              if a log and a data volume correspond to the same database.
 *       This function is used to obtain the database creation time and
 *              the last checkpoint address, so that they can be included in
 *              new defined volumes.
 */
int
log_get_db_start_parameters (INT64 * db_creation, LOG_LSA * chkpt_lsa)
{
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */
  memcpy (db_creation, &log_Gl.hdr.db_creation, sizeof (*db_creation));
  rv = pthread_mutex_lock (&log_Gl.chkpt_lsa_lock);
  memcpy (chkpt_lsa, &log_Gl.hdr.chkpt_lsa, sizeof (*chkpt_lsa));
  pthread_mutex_unlock (&log_Gl.chkpt_lsa_lock);

  return NO_ERROR;
}

/*
 * log_get_num_pages_for_creation - find default number of pages for the log
 *
 * return: number of pages
 *
 *   db_npages(in): Estimated number of pages for database (for first volume of
 *               database) or -1
 *
 * NOTE: Find the default number of pages to use during the creation of
 *              the log.
 *              If a negative value is given, the database should have been
 *              already created. That is, we are recreating the log
 */
int
log_get_num_pages_for_creation (int db_npages)
{
  int log_npages;
  int vdes;

  log_npages = db_npages;
  if (log_npages < 0)
    {
      /*
       * Use the default that is the size of the database
       * Don't use DK since the database may not be restarted at all.
       */
      vdes = fileio_get_volume_descriptor (LOG_DBFIRST_VOLID);
      if (vdes != NULL_VOLDES)
	{
	  log_npages = fileio_get_number_of_volume_pages (vdes, IO_PAGESIZE);
	}
    }

  if (log_npages < 10)
    {
      log_npages = 10;
    }

  return log_npages;
}

/*
 * log_create - create the active portion of the log
 *
 * return: nothing
 *
 *   db_fullname(in): Full name of the database
 *   logpath(in): Directory where the log volumes reside
 *   prefix_logname(in): Name of the log volumes. It is usually set the same as
 *                      database name. For example, if the value is equal to
 *                      "db", the names of the log volumes created are as
 *                      follow:
 *                      Active_log      = db_logactive
 *                      Archive_logs    = db_logarchive.0
 *                                        db_logarchive.1
 *                                             .
 *                                             .
 *                                             .
 *                                        db_logarchive.n
 *                      Log_information = db_loginfo
 *                      Database Backup = db_backup
 *   npages(in): Size of active log in pages
 *
 * NOTE: Format/create the active log volume. The header of the volume
 *              is initialized.
 */
int
log_create (THREAD_ENTRY * thread_p, const char *db_fullname,
	    const char *logpath, const char *prefix_logname, DKNPAGES npages)
{
  INT64 db_creation;

  db_creation = time (NULL);
  if (db_creation == -1)
    {
      return ER_FAILED;
    }
  log_create_internal (thread_p, db_fullname, logpath, prefix_logname, npages,
		       &db_creation);

  return NO_ERROR;
}

/*
 * log_create_internal -
 *
 * return:
 *
 *   db_fullname(in):
 *   logpath(in):
 *   prefix_logname(in):
 *   npages(in):
 *   db_creation(in):
 *
 * NOTE:
 */
static void
log_create_internal (THREAD_ENTRY * thread_p, const char *db_fullname,
		     const char *logpath, const char *prefix_logname,
		     DKNPAGES npages, INT64 * db_creation)
{
  LOG_PAGE *loghdr_pgptr;	/* Pointer to log header */
  const char *catmsg;
  int error_code = NO_ERROR;
  VOLID volid1, volid2;

  LOG_CS_ENTER (thread_p);

  /* Make sure that we are starting from a clean state */
  if (log_Gl.trantable.area != NULL)
    {
      log_final (thread_p);
    }

  /*
   * Turn off creation bits for group and others
   */

  (void) umask (S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);

  /* Initialize the log buffer pool and the log names */
  if (logpb_initialize_pool (thread_p) != NO_ERROR)
    {
      logpb_fatal_error (thread_p, false, ARG_FILE_LINE, "log_create");
    }
  if (logpb_initialize_log_names (thread_p, db_fullname, logpath,
				  prefix_logname) != NO_ERROR)
    {
      logpb_fatal_error (thread_p, false, ARG_FILE_LINE, "log_create");
    }

  logpb_decache_archive_info (thread_p);

  log_Gl.rcv_phase = LOG_RECOVERY_ANALYSIS_PHASE;

  /* Initialize the log header */
  if (logpb_initialize_header (thread_p, &log_Gl.hdr, prefix_logname, npages,
			       db_creation) != NO_ERROR)
    {
      logpb_finalize_pool ();
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_create");
      LOG_CS_EXIT (thread_p);
      return;
    }

  loghdr_pgptr = logpb_create_header_page (thread_p);

  /*
   * Format the volume and fetch the header page and the first append page
   */
  log_Gl.append.vdes = fileio_format (thread_p, db_fullname, log_Name_active,
				      LOG_DBLOG_ACTIVE_VOLID, npages,
				      prm_get_bool_value
				      (PRM_ID_LOG_SWEEP_CLEAN), true, false,
				      LOG_PAGESIZE, 0, false);
  if (log_Gl.append.vdes == NULL_VOLDES
      || logpb_fetch_start_append_page (thread_p) == NULL
      || loghdr_pgptr == NULL)
    {
      logpb_finalize_pool ();
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_create");
      LOG_CS_EXIT (thread_p);
      return;
    }
  LSA_SET_NULL (&log_Gl.append.prev_lsa);
  /* copy log_Gl.append.prev_lsa to log_Gl.prior_info.prev_lsa */
  LOG_RESET_PREV_LSA (&log_Gl.append.prev_lsa);

  /*
   * Flush the append page, so that the end of the log mark is written.
   * Then, free the page, same for the header page.
   */
  logpb_set_dirty (thread_p, log_Gl.append.log_pgptr, DONT_FREE);
  logpb_flush_pages_direct (thread_p);

  log_Gl.chkpt_every_npages =
    prm_get_integer_value (PRM_ID_LOG_CHECKPOINT_NPAGES);

  /* Flush the log header */

  memcpy (loghdr_pgptr->area, &log_Gl.hdr, sizeof (log_Gl.hdr));
  logpb_set_dirty (thread_p, loghdr_pgptr, DONT_FREE);

#if defined(CUBRID_DEBUG)
  {
    char temp_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT], *aligned_temp_pgbuf;
    LOG_PAGE *temp_pgptr;

    aligned_temp_pgbuf = PTR_ALIGN (temp_pgbuf, MAX_ALIGNMENT);

    temp_pgptr = (LOG_PAGE *) aligned_temp_pgbuf;
    memset (temp_pgptr, 0, LOG_PAGESIZE);
    logpb_read_page_from_file (LOGPB_HEADER_PAGE_ID, temp_pgptr);
    assert (memcmp ((struct log_header *) temp_pgptr->area,
		    &log_Gl.hdr, sizeof (log_Gl.hdr)) != 0);
  }
#endif /* CUBRID_DEBUG */

  if (logpb_flush_page (thread_p, loghdr_pgptr, DONT_FREE) != NO_ERROR)
    {
      logpb_fatal_error (thread_p, false, ARG_FILE_LINE, "log_create");
    }
#if defined(CUBRID_DEBUG)
  {
    char temp_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT], *aligned_temp_pgbuf;
    LOG_PAGE *temp_pgptr;

    aligned_temp_pgbuf = PTR_ALIGN (temp_pgbuf, MAX_ALIGNMENT);

    temp_pgptr = (LOG_PAGE *) aligned_temp_pgbuf;
    memset (temp_pgptr, 0, LOG_PAGESIZE);
    logpb_read_page_from_file (LOGPB_HEADER_PAGE_ID, temp_pgptr);
    assert (memcmp ((struct log_header *) temp_pgptr->area,
		    &log_Gl.hdr, sizeof (log_Gl.hdr)) == 0);
  }
#endif /* CUBRID_DEBUG */

  logpb_free_page (thread_p, loghdr_pgptr);

  /* logpb_flush_header(); */

  /*
   * Free the append and header page and dismount the lg active volume
   */
  logpb_free_page (thread_p, log_Gl.append.log_pgptr);
  log_Gl.append.log_pgptr = NULL;

  fileio_dismount (thread_p, log_Gl.append.vdes);

  if (logpb_create_volume_info (NULL) != NO_ERROR)
    {
      logpb_finalize_pool ();
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_create");
    }

  /* Create the information file to append log info stuff to the DBA */
  logpb_create_log_info (log_Name_info, NULL);

  catmsg = msgcat_message (MSGCAT_CATALOG_CUBRID,
			   MSGCAT_SET_LOG, MSGCAT_LOG_LOGINFO_ACTIVE);
  if (catmsg == NULL)
    {
      catmsg = "ACTIVE: %s %d pages\n";
    }
  error_code = log_dump_log_info (log_Name_info, false, catmsg,
				  log_Name_active, npages);
  if (error_code == NO_ERROR || error_code == ER_LOG_MOUNT_FAIL)
    {
      volid1 = logpb_add_volume (NULL, LOG_DBLOG_BKUPINFO_VOLID,
				 log_Name_bkupinfo, DISK_UNKNOWN_PURPOSE);
      if (volid1 == LOG_DBLOG_BKUPINFO_VOLID)
	{
	  volid2 = logpb_add_volume (NULL, LOG_DBLOG_ACTIVE_VOLID,
				     log_Name_active, DISK_UNKNOWN_PURPOSE);
	}

      if (volid1 != LOG_DBLOG_BKUPINFO_VOLID
	  || volid2 != LOG_DBLOG_ACTIVE_VOLID)
	{
	  logpb_finalize_pool ();
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_create");
	}
    }

  logpb_finalize_pool ();

  LOG_CS_EXIT (thread_p);
}

/*
 * log_set_no_logging - Force the system to do no logging.
 *
 * return: NO_ERROR or error code
 *
 */
int
log_set_no_logging (void)
{
  int error_code = NO_ERROR;

#if defined(SA_MODE)

  if (log_Gl.trantable.num_prepared_loose_end_indices != 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_LOG_THEREARE_PENDING_ACTIONS_MUST_LOG, 0);
      error_code = ER_LOG_THEREARE_PENDING_ACTIONS_MUST_LOG;
    }
  else
    {
      log_No_logging = true;
      error_code = NO_ERROR;
#if !defined(NDEBUG)
      if (prm_get_bool_value (PRM_ID_LOG_TRACE_DEBUG) && log_No_logging)
	{
	  fprintf (stdout, "**Running without logging**\n");
	  fflush (stdout);
	}
#endif /* NDEBUG */
    }

#else /* SA_MODE */
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	  ER_ONLY_IN_STANDALONE, 1, "no logging");
  error_code = ER_ONLY_IN_STANDALONE;
#endif /* SA_MODE */

  return error_code;
}

/*
 * log_initialize - Initialize the log manager
 *
 * return: nothing
 *
 *   db_fullname(in): Full name of the database
 *   logpath(in): Directory where the log volumes reside
 *   prefix_logname(in): Name of the log volumes. It must be the same as the
 *                      one given during the creation of the database.
 *   ismedia_crash(in): Are we recovering from media crash ?.
 *   stopat(in): If we are recovering from a media crash, we can stop
 *                      the recovery process at a given time.
 *
 * NOTE:Initialize the log manager. If the database system crashed,
 *              before the system was shutdown, the recovery process is
 *              executed as part of the initialization. The recovery process
 *              consists of redoing any changes that were previously committed
 *              and currently missing from the database disk, and undoing any
 *              changes that were not committed but that are stored in the
 *              database disk.
 */
void
log_initialize (THREAD_ENTRY * thread_p, const char *db_fullname,
		const char *logpath, const char *prefix_logname,
		int ismedia_crash, time_t * stopat)
{
  (void) log_initialize_internal (thread_p, db_fullname, logpath,
				  prefix_logname, ismedia_crash, stopat,
				  false);

  log_No_logging = prm_get_bool_value (PRM_ID_LOG_NO_LOGGING);
#if !defined(NDEBUG)
  if (prm_get_bool_value (PRM_ID_LOG_TRACE_DEBUG) && log_No_logging)
    {
      fprintf (stdout, "**Running without logging**\n");
      fflush (stdout);
    }
#endif /* !NDEBUG */
}

/*
 * log_initialize_internal -
 *
 * return:
 *
 *   db_fullname(in):
 *   logpath(in):
 *   prefix_logname(in):
 *   ismedia_crash(in):
 *   stopat(in):
 *   init_emergency(in):
 *
 * NOTE:
 */
static int
log_initialize_internal (THREAD_ENTRY * thread_p, const char *db_fullname,
			 const char *logpath, const char *prefix_logname,
			 int ismedia_crash, time_t * stopat,
			 bool init_emergency)
{
  LOG_RECORD_HEADER *eof;	/* End of log record */
  REL_FIXUP_FUNCTION *disk_compatibility_functions = NULL;
  REL_COMPATIBILITY compat;
  int i;
  int error_code = NO_ERROR;

#if defined(CUBRID_DEBUG)
  /* Make sure that the recovery function array is synchronized.. */
  rv_check_rvfuns ();
#endif /* CUBRID_DEBUG */

  (void) umask (S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);

  /* Make sure that the log is a valid one */
  LOG_SET_CURRENT_TRAN_INDEX (thread_p, LOG_SYSTEM_TRAN_INDEX);

  LOG_CS_ENTER (thread_p);

  if (log_Gl.trantable.area != NULL)
    {
      log_final (thread_p);
    }

  /* Initialize log name for log volumes */
  error_code =
    logpb_initialize_log_names (thread_p, db_fullname, logpath,
				prefix_logname);
  if (error_code != NO_ERROR)
    {
      logpb_fatal_error (thread_p, !init_emergency, ARG_FILE_LINE,
			 "log_xinit");
      goto error;
    }
  logpb_decache_archive_info (thread_p);
  log_Gl.run_nxchkpt_atpageid = NULL_PAGEID;	/* Don't run the checkpoint */
  log_Gl.rcv_phase = LOG_RECOVERY_ANALYSIS_PHASE;

  log_Gl.loghdr_pgptr = (LOG_PAGE *) malloc (LOG_PAGESIZE);
  if (log_Gl.loghdr_pgptr == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, LOG_PAGESIZE);
      logpb_fatal_error (thread_p, !init_emergency, ARG_FILE_LINE,
			 "log_xinit");
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      goto error;
    }
  error_code = logpb_initialize_pool (thread_p);
  if (error_code != NO_ERROR)
    {
      goto error;
    }

  /* Mount the active log and read the log header */
  log_Gl.append.vdes = fileio_mount (thread_p, db_fullname, log_Name_active,
				     LOG_DBLOG_ACTIVE_VOLID, true, false);
  if (log_Gl.append.vdes == NULL_VOLDES)
    {
      if (ismedia_crash != false)
	{
	  /*
	   * Set an approximate log header to continue the recovery process
	   */
	  INT64 db_creation = -1;	/* Database creation time in volume */
	  int log_npages;

	  log_npages = log_get_num_pages_for_creation (-1);

	  error_code =
	    logpb_initialize_header (thread_p, &log_Gl.hdr, prefix_logname,
				     log_npages, &db_creation);
	  if (error_code != NO_ERROR)
	    {
	      goto error;
	    }
	  log_Gl.hdr.fpageid = LOGPAGEID_MAX;
	  log_Gl.hdr.append_lsa.pageid = LOGPAGEID_MAX;
	  log_Gl.hdr.append_lsa.offset = 0;

	  /* sync append_lsa to prior_lsa */
	  LOG_RESET_APPEND_LSA (&log_Gl.hdr.append_lsa);

	  LSA_SET_NULL (&log_Gl.hdr.chkpt_lsa);
	  log_Gl.hdr.nxarv_pageid = LOGPAGEID_MAX;
	  log_Gl.hdr.nxarv_num = DB_INT32_MAX;
	  log_Gl.hdr.last_arv_num_for_syscrashes = DB_INT32_MAX;
	}
      else
	{
	  /* Unable to mount the active log */
	  error_code = ER_IO_MOUNT_FAIL;
	  goto error;
	}
    }
  else
    {
      logpb_fetch_header (thread_p, &log_Gl.hdr);
    }

  LSA_COPY (&log_Gl.chkpt_redo_lsa, &log_Gl.hdr.chkpt_lsa);

  /* Make sure that this is the desired log */
  if (strcmp (log_Gl.hdr.prefix_name, prefix_logname) != 0)
    {
      /*
       * This looks like the log or the log was renamed. Incompatible
       * prefix name with the prefix stored on disk
       */
      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE,
	      ER_LOG_INCOMPATIBLE_PREFIX_NAME, 2,
	      prefix_logname, log_Gl.hdr.prefix_name);
      /* Continue anyhow */
    }

  /*
   * Make sure that we are running with the same page size. If we are not,
   * restart again since page and log buffers may reflect an incorrect
   * pagesize
   */

  if (log_Gl.hdr.db_iopagesize != IO_PAGESIZE
      || log_Gl.hdr.db_logpagesize != LOG_PAGESIZE)
    {
      /*
       * Pagesize is incorrect. We need to undefine anything that has been
       * created with old pagesize and start again
       */
      if (db_set_page_size (log_Gl.hdr.db_iopagesize,
			    log_Gl.hdr.db_logpagesize) != NO_ERROR)
	{
	  /* Pagesize is incompatible */
	  error_code = ER_FAILED;
	  goto error;
	}
      /*
       * Call the function again... since we have a different setting for the
       * page size
       */
      logpb_finalize_pool ();
      fileio_dismount (thread_p, log_Gl.append.vdes);
      log_Gl.append.vdes = NULL_VOLDES;

      LOG_SET_CURRENT_TRAN_INDEX (thread_p, LOG_SYSTEM_TRAN_INDEX);
      LOG_CS_EXIT (thread_p);

      error_code =
	logtb_define_trantable_log_latch (thread_p,
					  log_Gl.trantable.num_total_indices);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
      error_code = log_initialize_internal (thread_p, db_fullname, logpath,
					    prefix_logname, ismedia_crash,
					    stopat, init_emergency);

      return error_code;
    }

  /* Make sure that the database is compatible with the CUBRID version.
   * This will compare the given level against the value returned by
   * rel_disk_compatible().
   */
  compat = rel_get_disk_compatible (log_Gl.hdr.db_compatibility,
				    &disk_compatibility_functions);

  /* If we're not completely compatible, signal an error.
   * There had been no compatibility rules on R2.1 or earlier version.
   * However, a compatibility rule between R2.2 and R2.1 (or earlier)
   * was added to provide restoration from R2.1 to R2.2.
   */
  if (compat != REL_FULLY_COMPATIBLE)
    {
      /* Database is incompatible with current release */
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_LOG_INCOMPATIBLE_DATABASE, 2,
	      rel_name (), rel_release_string ());
      error_code = ER_LOG_INCOMPATIBLE_DATABASE;
      goto error;
    }

  if (rel_is_log_compatible (log_Gl.hdr.db_release,
			     rel_release_string ()) != true)
    {
      /*
       * First time this database is restarted using the current version of
       * CUBRID. Recovery should be done using the old version of the
       * system
       */
      if (log_Gl.hdr.is_shutdown == false)
	{
	  const char *env_value;
	  bool unsafe;
	  /*
	   * Check environment variable to see if caller want to force to continue
	   * the recovery using current version.
	   */
	  env_value = envvar_get ("LOG_UNSAFE_RECOVER_NEW_RELEASE");
	  if (env_value != NULL)
	    {
	      if (atoi (env_value) != 0)
		{
		  unsafe = true;
		}
	      else
		{
		  unsafe = false;
		}
	    }
	  else
	    {
	      unsafe = false;
	    }

	  if (unsafe == false)
	    {
	      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_LOG_RECOVER_ON_OLD_RELEASE, 4,
		      rel_name (), log_Gl.hdr.db_release,
		      rel_release_string (), rel_release_string ());
	      error_code = ER_LOG_RECOVER_ON_OLD_RELEASE;
	      goto error;
	    }
	}

      /*
       * It seems safe to move to new version of the system
       */

      if (strlen (rel_release_string ()) >= REL_MAX_RELEASE_LENGTH)
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_LOG_COMPILATION_RELEASE, 2,
		  rel_release_string (), REL_MAX_RELEASE_LENGTH);
	  error_code = ER_LOG_COMPILATION_RELEASE;
	  goto error;
	}
      strncpy (log_Gl.hdr.db_release, rel_release_string (),
	       REL_MAX_RELEASE_LENGTH);
    }


  /*
   * Create the transaction table and make sure that data volumes and log
   * volumes belong to the same database
   */
#if 1
  /*
   * for XA support: there is prepared transaction after recovery.
   *                 so, can not recreate transaction description
   *                 table after recovery.
   *                 NEED MORE CONSIDERATION
   *
   * Total number of transaction descriptor is set to the value of
   * max_clients+1
   */
  error_code = logtb_define_trantable_log_latch (thread_p, -1);
  if (error_code != NO_ERROR)
    {
      goto error;
    }
#else
  error_code = logtb_define_trantable_log_latch (log_Gl.hdr.avg_ntrans);
  if (error_code != NO_ERROR)
    {
      goto error;
    }
#endif

  if (log_Gl.append.vdes != NULL_VOLDES)
    {
      if (fileio_map_mounted (thread_p,
			      (bool (*)(THREAD_ENTRY *, VOLID,
					void *)) log_verify_dbcreation,
			      &log_Gl.hdr.db_creation) != true)
	{
	  /* The log does not belong to the given database */
	  logtb_undefine_trantable (thread_p);
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_LOG_DOESNT_CORRESPOND_TO_DATABASE, 1, log_Name_active);
	  error_code = ER_LOG_DOESNT_CORRESPOND_TO_DATABASE;
	  goto error;
	}
    }

  log_Gl.mvcc_table.highest_completed_mvccid = log_Gl.hdr.mvcc_next_id;
  MVCCID_BACKWARD (log_Gl.mvcc_table.highest_completed_mvccid);

  LSA_SET_NULL (&log_Gl.hdr.mvcc_op_log_lsa);
  log_Gl.hdr.last_block_newest_mvccid = MVCCID_NULL;
  log_Gl.hdr.last_block_oldest_mvccid = MVCCID_NULL;
  /*
   * Was the database system shut down or was it involved in a crash ?
   */
  if (init_emergency == false
      && (log_Gl.hdr.is_shutdown == false || ismedia_crash != false))
    {
      /*
       * System was involved in a crash.
       * Execute the recovery process
       */
      log_recovery (thread_p, ismedia_crash, stopat);
    }
  else
    {
      /*
       * The system was shut down. There is nothing to recover.
       * Find the append page and start execution
       */
      if (logpb_fetch_start_append_page (thread_p) == NULL)
	{
	  error_code = ER_FAILED;
	  goto error;
	}

      /* Read the End of file record to find out the previous address */
      eof = (LOG_RECORD_HEADER *) LOG_APPEND_PTR ();
      LOG_RESET_PREV_LSA (&eof->back_lsa);

#if defined(SERVER_MODE)
      /* fix flushed_lsa_lower_bound become NULL_LSA */
      LSA_COPY (&log_Gl.flushed_lsa_lower_bound, &log_Gl.append.prev_lsa);
#endif /* SERVER_MODE */

      /*
       * Indicate that database system is UP,... flush the header so that we
       * we know that the system was running in the even of crashes
       */
      log_Gl.hdr.is_shutdown = false;
      logpb_flush_header (thread_p);
    }
  log_Gl.rcv_phase = LOG_RESTARTED;

  LSA_COPY (&log_Gl.rcv_phase_lsa, &log_Gl.hdr.chkpt_lsa);
  log_Gl.chkpt_every_npages =
    prm_get_integer_value (PRM_ID_LOG_CHECKPOINT_NPAGES);

  if (!LSA_EQ (&log_Gl.append.prev_lsa, &log_Gl.prior_info.prev_lsa))
    {
      assert (0);
      /* defense code */
      LOG_RESET_PREV_LSA (&log_Gl.append.prev_lsa);
    }
  if (!LSA_EQ (&log_Gl.hdr.append_lsa, &log_Gl.prior_info.prior_lsa))
    {
      assert (0);
      /* defense code */
      LOG_RESET_APPEND_LSA (&log_Gl.hdr.append_lsa);
    }

  /*
   *
   * Don't checkpoint to sizes smaller than the number of log buffers
   */
  if (log_Gl.chkpt_every_npages < prm_get_integer_value (PRM_ID_LOG_NBUFFERS))
    {
      log_Gl.chkpt_every_npages = prm_get_integer_value (PRM_ID_LOG_NBUFFERS);
    }

  /* Next checkpoint should be run at ... */
  log_Gl.run_nxchkpt_atpageid = (log_Gl.hdr.append_lsa.pageid +
				 log_Gl.chkpt_every_npages);

  LOG_SET_CURRENT_TRAN_INDEX (thread_p, LOG_SYSTEM_TRAN_INDEX);

  /* run the compatibility functions if we have any */
  if (disk_compatibility_functions != NULL)
    {
      for (i = 0; disk_compatibility_functions[i] != NULL; i++)
	{
	  (*(disk_compatibility_functions[i])) ();
	}
    }

  logpb_initialize_arv_page_info_table ();
  logpb_initialize_logging_statistics ();

  if (prm_get_bool_value (PRM_ID_LOG_BACKGROUND_ARCHIVING))
    {
      int vdes = NULL_VOLDES;
      BACKGROUND_ARCHIVING_INFO *bg_arv_info;

      bg_arv_info = &log_Gl.bg_archive_info;
      bg_arv_info->start_page_id = NULL_PAGEID;
      bg_arv_info->current_page_id = NULL_PAGEID;
      bg_arv_info->last_sync_pageid = NULL_PAGEID;

      bg_arv_info->vdes = fileio_format (thread_p, log_Db_fullname,
					 log_Name_bg_archive,
					 LOG_DBLOG_BG_ARCHIVE_VOLID,
					 log_Gl.hdr.npages + 1, false, false,
					 false, LOG_PAGESIZE, 0, false);
      if (bg_arv_info->vdes != NULL_VOLDES)
	{
	  bg_arv_info->start_page_id = log_Gl.hdr.nxarv_pageid;
	  bg_arv_info->current_page_id = log_Gl.hdr.nxarv_pageid;
	  bg_arv_info->last_sync_pageid = log_Gl.hdr.nxarv_pageid;
	}
      else
	{
	  er_log_debug (ARG_FILE_LINE,
			"Unable to create temporary archive log %s\n",
			log_Name_bg_archive);
	}

      if (bg_arv_info->vdes != NULL_VOLDES)
	{
	  (void) logpb_background_archiving (thread_p);
	}
    }

  LOG_CS_EXIT (thread_p);

  return error_code;

error:
  /* ***** */

  if (log_Gl.append.vdes != NULL_VOLDES)
    {
      fileio_dismount (thread_p, log_Gl.append.vdes);
    }

  LOG_SET_CURRENT_TRAN_INDEX (thread_p, LOG_SYSTEM_TRAN_INDEX);

  if (log_Gl.loghdr_pgptr != NULL)
    {
      free_and_init (log_Gl.loghdr_pgptr);
    }

  LOG_CS_EXIT (thread_p);

  logpb_fatal_error (thread_p, !init_emergency, ARG_FILE_LINE, "log_init");

  return error_code;

}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * log_update_compatibility_and_release -
 *
 * return: NO_ERROR
 *
 *   compatibility(in):
 *   release(in):
 *
 * NOTE:
 */
int
log_update_compatibility_and_release (THREAD_ENTRY * thread_p,
				      float compatibility, char release[])
{
  LOG_CS_ENTER (thread_p);

  log_Gl.hdr.db_compatibility = compatibility;
  strncpy (log_Gl.hdr.db_release, release, REL_MAX_RELEASE_LENGTH);

  logpb_flush_header (thread_p);

  LOG_CS_EXIT (thread_p);

  return NO_ERROR;
}
#endif /* ENABLE_UNUSED_FUNCTION */

#if defined(SERVER_MODE) || defined(SA_MODE)
/*
 * log_get_db_compatibility -
 *
 * return:
 *
 * NOTE:
 */
float
log_get_db_compatibility (void)
{
  return log_Gl.hdr.db_compatibility;
}
#endif /* SERVER_MODE || SA_MODE */

#if defined(SERVER_MODE)
/*
 * log_abort_by_tdes - Abort a transaction
 *
 * return: NO_ERROR
 *
 *   arg(in): Transaction descriptor
 *
 * NOTE:
 */
static int
log_abort_by_tdes (THREAD_ENTRY * thread_p, LOG_TDES * tdes)
{
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  thread_p->tran_index = tdes->tran_index;
  pthread_mutex_unlock (&thread_p->tran_index_lock);

  (void) log_abort (thread_p, tdes->tran_index);

  return NO_ERROR;
}
#endif /* SERVER_MODE */

/*
 * TODO : STL
 * log_abort_all_active_transaction -
 *
 * return:
 *
 * NOTE:
 */
void
log_abort_all_active_transaction (THREAD_ENTRY * thread_p)
{
  int i;
  LOG_TDES *tdes;		/* Transaction descriptor */
#if defined(SERVER_MODE)
  int repeat_loop;
  CSS_CONN_ENTRY *conn = NULL;
  CSS_JOB_ENTRY *job_entry = NULL;
  int *abort_thread_running;
  static int already_called = 0;

  if (already_called)
    {
      return;
    }
  already_called = 1;

  if (log_Gl.trantable.area == NULL)
    {
      return;
    }

  abort_thread_running =
    (int *) malloc (sizeof (int) * log_Gl.trantable.num_total_indices);
  if (abort_thread_running == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, sizeof (int) * log_Gl.trantable.num_total_indices);
      return;
    }
  memset (abort_thread_running, 0,
	  sizeof (int) * log_Gl.trantable.num_total_indices);

  /* Abort all active transactions */
loop:
  repeat_loop = false;

  for (i = 0; i < log_Gl.trantable.num_total_indices; i++)
    {
      if (i != LOG_SYSTEM_TRAN_INDEX && (tdes = LOG_FIND_TDES (i)) != NULL
	  && tdes->trid != NULL_TRANID)
	{
	  if (thread_has_threads (thread_p, i, tdes->client_id) > 0)
	    {
	      repeat_loop = true;
	    }
	  else if (LOG_ISTRAN_ACTIVE (tdes) && abort_thread_running[i] == 0)
	    {
	      conn = css_find_conn_by_tran_index (i);
	      job_entry = css_make_job_entry (conn,
					      (CSS_THREAD_FN)
					      log_abort_by_tdes,
					      (CSS_THREAD_ARG) tdes,
					      -1 /* implicit: DEFAULT */ );
	      if (job_entry != NULL)
		{
		  css_add_to_job_queue (job_entry);
		  abort_thread_running[i] = 1;
		}

	      repeat_loop = true;
	    }
	}
    }

  if (repeat_loop)
    {
      thread_sleep (50);	/* sleep 0.05 sec */
      if (css_is_shutdown_timeout_expired ())
	{
	  if (abort_thread_running != NULL)
	    {
	      free_and_init (abort_thread_running);
	    }
	  /* exit process after some tries */
	  er_log_debug (ARG_FILE_LINE,
			"log_abort_all_active_transaction: _exit(0)\n");
	  _exit (0);
	}
      goto loop;
    }

  if (abort_thread_running != NULL)
    {
      free_and_init (abort_thread_running);
    }

#else /* SERVER_MODE */
  int save_tran_index = log_Tran_index;	/* Return to this index   */

  if (log_Gl.trantable.area == NULL)
    {
      return;
    }

  /* Abort all active transactions */
  for (i = 0; i < log_Gl.trantable.num_total_indices; i++)
    {
      tdes = LOG_FIND_TDES (i);
      if (i != LOG_SYSTEM_TRAN_INDEX && tdes != NULL
	  && tdes->trid != NULL_TRANID)
	{
	  if (LOG_ISTRAN_ACTIVE (tdes))
	    {
	      log_Tran_index = i;
	      (void) log_abort (NULL, log_Tran_index);
	    }
	}
    }
  log_Tran_index = save_tran_index;
#endif /* SERVER_MODE */
}

/*
 * TODO : STL
 * log_final - Terminate the log manager
 *
 * return: nothing
 *
 * NOTE: Terminate the log correctly, so that no recovery will be
 *              needed when the database system is restarted again. If there
 *              are any active transactions, they are all aborted. The log is
 *              flushed and all dirty data pages are also flushed to disk.
 */
void
log_final (THREAD_ENTRY * thread_p)
{
  int i;
  LOG_TDES *tdes;		/* Transaction descriptor */
  int save_tran_index;
  bool anyloose_ends = false;
  int error_code = NO_ERROR;

  LOG_CS_ENTER (thread_p);

  if (log_Gl.trantable.area == NULL)
    {
      LOG_CS_EXIT (thread_p);
      return;
    }

  save_tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);

  if (!logpb_is_initialize_pool ())
    {
      logtb_undefine_trantable (thread_p);
      LOG_CS_EXIT (thread_p);
      return;
    }

  if (log_Gl.append.vdes == NULL_VOLDES)
    {
      logpb_finalize_pool ();
      logtb_undefine_trantable (thread_p);
      LOG_CS_EXIT (thread_p);
      return;
    }

  /*
   * Cannot use the critical section here since we are assigning the
   * transaction index and the critical sections are base on the transaction
   * index. Acquire the critical section and the get out immediately.. by
   * this time the scheduler will not preempt you.
   */

  /* Abort all active transactions */
  for (i = 0; i < log_Gl.trantable.num_total_indices; i++)
    {
      tdes = LOG_FIND_TDES (i);
      if (i != LOG_SYSTEM_TRAN_INDEX && tdes != NULL
	  && tdes->trid != NULL_TRANID)
	{
	  if (LOG_ISTRAN_ACTIVE (tdes))
	    {
	      LOG_SET_CURRENT_TRAN_INDEX (thread_p, i);
	      (void) log_abort (thread_p, i);
	    }
	  else
	    {
	      anyloose_ends = true;
	    }
	}
    }

  LOG_SET_CURRENT_TRAN_INDEX (thread_p, save_tran_index);

  /*
   * Flush all log append dirty pages and all data dirty pages
   */
  logpb_flush_pages_direct (thread_p);

  error_code = pgbuf_flush_all (thread_p, NULL_VOLID);
  if (error_code == NO_ERROR)
    {
      error_code = fileio_synchronize_all (thread_p, false);
    }

  logpb_decache_archive_info (thread_p);

  /*
   * Flush the header of the log with information to restart the system
   * easily. For example, without a recovery process
   */

  log_Gl.hdr.has_logging_been_skipped = false;
  if (anyloose_ends == false && error_code == NO_ERROR)
    {
      log_Gl.hdr.is_shutdown = true;
      LSA_COPY (&log_Gl.hdr.chkpt_lsa, &log_Gl.hdr.append_lsa);
      LSA_COPY (&log_Gl.hdr.smallest_lsa_at_last_chkpt,
		&log_Gl.hdr.chkpt_lsa);
    }
  else
    {
      (void) logpb_checkpoint (thread_p);
    }

  logpb_flush_header (thread_p);

  /* Undefine page buffer pool and transaction table */
  logpb_finalize_pool ();

  logtb_undefine_trantable (thread_p);

  if (prm_get_bool_value (PRM_ID_LOG_BACKGROUND_ARCHIVING))
    {
      if (log_Gl.bg_archive_info.vdes != NULL_VOLDES)
	{
	  fileio_dismount (thread_p, log_Gl.bg_archive_info.vdes);
	  log_Gl.bg_archive_info.vdes = NULL_VOLDES;
	}
    }

  /* Dismount the active log volume */
  fileio_dismount (thread_p, log_Gl.append.vdes);
  log_Gl.append.vdes = NULL_VOLDES;

  free_and_init (log_Gl.loghdr_pgptr);

  LOG_CS_EXIT (thread_p);
}

/*
 * log_restart_emergency - Emergency restart of log manager
 *
 * return: nothing
 *
 *   db_fullname(in): Full name of the database
 *   logpath(in): Directory where the log volumes reside
 *   prefix_logname(in): Name of the log volumes. It must be the same as the
 *                      one given during the creation of the database.
 *
 * NOTE: Initialize the log manager in emergency fashion. That is,
 *              restart recovery is ignored.
 */
void
log_restart_emergency (THREAD_ENTRY * thread_p, const char *db_fullname,
		       const char *logpath, const char *prefix_logname)
{
  (void) log_initialize_internal (thread_p, db_fullname, logpath,
				  prefix_logname, false, NULL, true);
}

/*
 *
 *                    INTERFACE FUNCTION FOR LOGGING DATA
 *
 */

/*
 * log_append_undoredo_data - LOG UNDO (BEFORE) + REDO (AFTER) DATA
 *
 * return: nothing
 *
 *   rcvindex(in): Index to recovery function
 *   addr(in): Address (Volume, page, and offset) of data
 *   undo_length(in): Length of undo(before) data
 *   redo_length(in): Length of redo(after) data
 *   undo_data(in): Undo (before) data
 *   redo_data(in): Redo (after) data
 *
 */
void
log_append_undoredo_data (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex,
			  LOG_DATA_ADDR * addr, int undo_length,
			  int redo_length, const void *undo_data,
			  const void *redo_data)
{
  LOG_CRUMB undo_crumb;
  LOG_CRUMB redo_crumb;

  /* Set undo length/data to crumb */
  undo_crumb.data = undo_data;
  undo_crumb.length = undo_length;

  /* Set redo length/data to crumb */
  redo_crumb.data = redo_data;
  redo_crumb.length = redo_length;

  log_append_undoredo_crumbs (thread_p, rcvindex, addr, 1, 1, &undo_crumb,
			      &redo_crumb);
}

void
log_append_undoredo_data2 (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex,
			   const VFID * vfid, PAGE_PTR pgptr, PGLENGTH offset,
			   int undo_length, int redo_length,
			   const void *undo_data, const void *redo_data)
{
  LOG_DATA_ADDR addr;
  LOG_CRUMB undo_crumb;
  LOG_CRUMB redo_crumb;

  /* Set data address */
  addr.vfid = vfid;
  addr.pgptr = pgptr;
  addr.offset = offset;

  /* Set undo length/data to crumb */
  undo_crumb.data = undo_data;
  undo_crumb.length = undo_length;

  /* Set redo length/data to crumb */
  redo_crumb.data = redo_data;
  redo_crumb.length = redo_length;

  log_append_undoredo_crumbs (thread_p, rcvindex, &addr, 1, 1, &undo_crumb,
			      &redo_crumb);
}

/*
 * log_append_undo_data - LOG UNDO (BEFORE) DATA
 *
 * return: nothing
 *
 *   rcvindex(in): Index to recovery function
 *   addr(in): Address (Volume, page, and offset) of data
 *   length(in): Length of undo(before) data
 *   data(in): Undo (before) data
 *
 * NOTE: Log undo(before) data. A log record is constructed to recover
 *              data by undoing data during abort and during recovery.
 *
 *              In the case of a rollback, the undo function described by
 *              rcvindex is called with a recovery structure which contains
 *              the page pointer and offset of the data to recover along with
 *              the undo data. It is up to this function to determine how to
 *              undo the data.
 *
 *     1)       This function accepts either page operation logging (with a
 *              valid address) or logical log (with a null address).
 *     2)       Very IMPORTANT: If an update is associated with two individual
 *              log records, the undo record must be logged before the redo
 *              record.
 */
void
log_append_undo_data (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex,
		      LOG_DATA_ADDR * addr, int length, const void *data)
{
  LOG_CRUMB undo_crumb;

  /* Set length/data to crumb */
  undo_crumb.data = data;
  undo_crumb.length = length;

  log_append_undo_crumbs (thread_p, rcvindex, addr, 1, &undo_crumb);
}

void
log_append_undo_data2 (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex,
		       const VFID * vfid, PAGE_PTR pgptr, PGLENGTH offset,
		       int length, const void *data)
{
  LOG_DATA_ADDR addr;
  LOG_CRUMB undo_crumb;

  /* Set data address */
  addr.vfid = vfid;
  addr.pgptr = pgptr;
  addr.offset = offset;

  /* Set length/data to crumb */
  undo_crumb.data = data;
  undo_crumb.length = length;

  log_append_undo_crumbs (thread_p, rcvindex, &addr, 1, &undo_crumb);
}

/*
 * log_append_redo_data - LOG REDO (AFTER) DATA
 *
 * return: nothing
 *
 *   rcvindex(in): Index to recovery function
 *   addr(in): Address (Volume, page, and offset) of data
 *   length(in): Length of redo(after) data
 *   data(in): Redo (after) data
 *
 * NOTE: Log redo(after) data. A log record is constructed to recover
 *              data by redoing data during recovery.
 *
 *              During recovery(e.g., system crash recovery), the redo
 *              function described by rcvindex is called with a recovery
 *              structure which contains the page pointer and offset of the
 *              data to recover along with the redo data. It is up to this
 *              function to determine how to redo the data.
 *
 *     1)       The only type of logging accepted by this function is page
 *              operation level logging, thus, an address must must be given.
 *     2)       During the redo phase of crash recovery, any redo logging is
 *              ignored.
 */
void
log_append_redo_data (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex,
		      LOG_DATA_ADDR * addr, int length, const void *data)
{
  LOG_CRUMB redo_crumb;

  /* Set length/data to crumb */
  redo_crumb.data = data;
  redo_crumb.length = length;

  log_append_redo_crumbs (thread_p, rcvindex, addr, 1, &redo_crumb);
}

void
log_append_redo_data2 (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex,
		       const VFID * vfid, PAGE_PTR pgptr, PGLENGTH offset,
		       int length, const void *data)
{
  LOG_DATA_ADDR addr;
  LOG_CRUMB redo_crumb;

  /* Set data address */
  addr.vfid = vfid;
  addr.pgptr = pgptr;
  addr.offset = offset;

  /* Set length/data to crumb */
  redo_crumb.data = data;
  redo_crumb.length = length;

  log_append_redo_crumbs (thread_p, rcvindex, &addr, 1, &redo_crumb);
}

/*
 * log_append_undoredo_crumbs -  LOG UNDO (BEFORE) + REDO (AFTER) CRUMBS OF DATA
 *
 * return: nothing
 *
 *   rcvindex(in): Index to recovery function
 *   addr(in): Address (Volume, page, and offset) of data
 *   num_undo_crumbs(in): Number of undo crumbs
 *   num_redo_crumbs(in): Number of redo crumbs
 *   undo_crumbs(in): The undo crumbs
 *   redo_crumbs(in): The redo crumbs
 *
 */
void
log_append_undoredo_crumbs (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex,
			    LOG_DATA_ADDR * addr, int num_undo_crumbs,
			    int num_redo_crumbs,
			    const LOG_CRUMB * undo_crumbs,
			    const LOG_CRUMB * redo_crumbs)
{
  LOG_TDES *tdes;		/* Transaction descriptor             */
  int tran_index;
  int error_code = NO_ERROR;
  LOG_PRIOR_NODE *node;
  LOG_LSA start_lsa;
  LOG_RECTYPE rectype =
    LOG_IS_MVCC_OPERATION (rcvindex) ?
    LOG_MVCC_UNDOREDO_DATA : LOG_UNDOREDO_DATA;

#if defined(CUBRID_DEBUG)
  if (addr->pgptr == NULL)
    {
      /*
       * Redo is always an operation page level logging. Thus, a data page
       * pointer must have been given as part of the address
       */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_REDO_INTERFACE, 0);
      error_code = ER_LOG_REDO_INTERFACE;
      return;
    }
  if (RV_fun[rcvindex].undofun == NULL || RV_fun[rcvindex].redofun == NULL)
    {
      assert (false);
      return;
    }
#endif /* CUBRID_DEBUG */

  if (log_No_logging)
    {
      /* We are not logging */
      LOG_FLUSH_LOGGING_HAS_BEEN_SKIPPED (thread_p);
      log_skip_logging (thread_p, addr);
      return;
    }

  /* Find transaction descriptor for current logging transaction */
  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
      error_code = ER_LOG_UNKNOWN_TRANINDEX;
      return;
    }

  /*
   * If we are not in a top system operation, the transaction is unactive, and
   * the transaction is not in the process of been aborted, we do nothing.
   */
  if (tdes->topops.last < 0
      && !LOG_ISTRAN_ACTIVE (tdes) && !LOG_ISTRAN_ABORTED (tdes))
    {
      /*
       * We do not log anything when the transaction is unactive and it is not
       * in the process of aborting.
       */
      return;
    }

  /*
   * is undo logging needed ?
   */

  if (log_can_skip_undo_logging (thread_p, rcvindex, tdes, addr) == true)
    {
      /* undo logging is ignored at this point */
      log_append_redo_crumbs (thread_p, rcvindex, addr, num_redo_crumbs,
			      redo_crumbs);
      return;
    }

  /*
   * Now do the UNDO & REDO portion
   */

  node =
    prior_lsa_alloc_and_copy_crumbs (thread_p, rectype, rcvindex, addr,
				     num_undo_crumbs, undo_crumbs,
				     num_redo_crumbs, redo_crumbs);
  if (node == NULL)
    {
      return;
    }

  start_lsa = prior_lsa_next_record (thread_p, node, tdes);

  if (LOG_NEED_TO_SET_LSA (rcvindex, addr->pgptr))
    {
      if (pgbuf_set_lsa (thread_p, addr->pgptr, &start_lsa) == NULL)
	{
	  assert (false);
	  return;
	}
    }

  /*
   * Vacuum data may occupy many pages but has only one log lsa. Check if this
   * is a change on vacuum data and pass start_lsa to be recorded.
   */
  if (LOG_IS_VACUUM_DATA_RECOVERY (rcvindex))
    {
      vacuum_set_vacuum_data_lsa (thread_p, &start_lsa);
    }

  if (!LOG_CHECK_LOG_APPLIER (thread_p)
      && log_does_allow_replication () == true)
    {
      if (rcvindex == RVHF_UPDATE || rcvindex == RVOVF_CHANGE_LINK)
	{
	  LSA_COPY (&tdes->repl_update_lsa, &tdes->tail_lsa);
	}
      else if (rcvindex == RVHF_INSERT)
	{
	  LSA_COPY (&tdes->repl_insert_lsa, &tdes->tail_lsa);
	}
    }
}

/*
 * log_append_undo_crumbs - LOG UNDO (BEFORE) CRUMBS OF DATA
 *
 * return: nothing
 *
 *   rcvindex(in): Index to recovery function
 *   addr(in): Address (Volume, page, and offset) of data
 *   num_crumbs(in): Number of undo crumbs
 *   crumbs(in): The undo crumbs
 *
 */
void
log_append_undo_crumbs (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex,
			LOG_DATA_ADDR * addr, int num_crumbs,
			const LOG_CRUMB * crumbs)
{
  LOG_TDES *tdes;		/* Transaction descriptor             */
  int tran_index;
  int i = 0;
  int error_code = NO_ERROR;
  LOG_PRIOR_NODE *node;
  LOG_LSA start_lsa;
  LOG_RECTYPE rectype =
    LOG_IS_MVCC_OPERATION (rcvindex) ? LOG_MVCC_UNDO_DATA : LOG_UNDO_DATA;

#if defined(CUBRID_DEBUG)
  if (RV_fun[rcvindex].undofun == NULL)
    {
      assert (false);
      return;
    }
#endif /* CUBRID_DEBUG */

  if (log_No_logging)
    {
      /* We are not logging */
      LOG_FLUSH_LOGGING_HAS_BEEN_SKIPPED (thread_p);
      if (addr->pgptr != NULL)
	{
	  log_skip_logging (thread_p, addr);
	}
      return;
    }

  /* Find transaction descriptor for current logging transaction */
  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
      error_code = ER_LOG_UNKNOWN_TRANINDEX;
      return;
    }

  /*
   * If we are not in a top system operation, the transaction is unactive, and
   * the transaction is not in the process of been aborted, we do nothing.
   */
  if (tdes->topops.last < 0
      && !LOG_ISTRAN_ACTIVE (tdes) && !LOG_ISTRAN_ABORTED (tdes))
    {
      /*
       * We do not log anything when the transaction is unactive and it is not
       * in the process of aborting.
       */
      return;
    }

  /*
   * is undo logging needed ?
   */
  if (log_can_skip_undo_logging (thread_p, rcvindex, tdes, addr) == true)
    {
      /* undo logging is ignored at this point */
      ;				/* NO-OP */
      return;
    }

  /*
   * NOW do the UNDO ...
   */

  node =
    prior_lsa_alloc_and_copy_crumbs (thread_p, rectype, rcvindex, addr,
				     num_crumbs, crumbs, 0, NULL);
  if (node == NULL)
    {
      return;
    }

  start_lsa = prior_lsa_next_record (thread_p, node, tdes);

  if (addr->pgptr != NULL && LOG_NEED_TO_SET_LSA (rcvindex, addr->pgptr))
    {
      if (pgbuf_set_lsa (thread_p, addr->pgptr, &start_lsa) == NULL)
	{
	  assert (false);
	  return;
	}
    }
}

/*
 * log_append_redo_crumbs - LOG REDO (AFTER) CRUMBS OF DATA
 *
 * return: nothing
 *
 *   rcvindex(in): Index to recovery function
 *   addr(in): Address (Volume, page, and offset) of data
 *   num_crumbs(in): Number of undo crumbs
 *   crumbs(in): The undo crumbs
 *
 * NOTE: Log redo(after) crumbs of data. A log record is constructed to
 *              recover data by redoing data during recovery.
 *              The log manager does not really store crumbs of data, instead
 *              the log manager glues them together as a stream of data, and
 *              thus, it looses the knowledge that the data was from crumbs.
 *              This is done to avoid extra storage overhead. It is the
 *              responsibility of the recovery functions to build the crumbs
 *              when needed from the glued data.
 *
 *              During recovery(e.g., system crash recovery), the redo
 *              function described by rcvindex is called with a recovery
 *              structure which contains the page pointer and offset of the
 *              data to recover along with the redo glued data. The redo
 *              function must construct the crumbs when needed. It is up to
 *              this function, how to undo the data.
 *
 *     1)       Same notes as log_append_redo_data (see this function)
 *     2)       The only purpose of this function is to avoid extra data
 *              copying (the glue into one contiguous area) by the caller,
 *              otherwise, the same as log_append_redo_data.
 */
void
log_append_redo_crumbs (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex,
			LOG_DATA_ADDR * addr, int num_crumbs,
			const LOG_CRUMB * crumbs)
{
  LOG_TDES *tdes;		/* Transaction descriptor             */
  int tran_index;
  int error_code = NO_ERROR;
  LOG_PRIOR_NODE *node;
  LOG_LSA start_lsa;
  LOG_RECTYPE rectype =
    LOG_IS_MVCC_OPERATION (rcvindex) ? LOG_MVCC_REDO_DATA : LOG_REDO_DATA;

#if defined(CUBRID_DEBUG)
  if (addr->pgptr == NULL)
    {
      /*
       * Redo is always an operation page level logging. Thus, a data page
       * pointer must have been given as part of the address
       */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_REDO_INTERFACE, 0);
      error_code = ER_LOG_REDO_INTERFACE;
      return;
    }
  if (RV_fun[rcvindex].redofun == NULL)
    {
      assert (false);
      return;
    }
#endif /* CUBRID_DEBUG */

  if (log_No_logging)
    {
      /* We are not logging */
      LOG_FLUSH_LOGGING_HAS_BEEN_SKIPPED (thread_p);
      log_skip_logging (thread_p, addr);
      return;
    }

  /* Find transaction descriptor for current logging transaction */
  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
      error_code = ER_LOG_UNKNOWN_TRANINDEX;
      return;
    }

  /*
   * If we are not in a top system operation, the transaction is unactive, and
   * the transaction is not in the process of been aborted, we do nothing.
   * 
   * NOTE: One exception for this case is the recovery of vacuum data buffer.
   *       Because the buffer may not have been empty during the crash, the
   *       data in the buffer cannot be recovered using log records. It is
   *       however rebuilt in a similar manner as it is built online (by
   *       processing undo and undoredo log records of MVCC operations).
   *       At the end of redo recovery phase, the recovered data is consumed
   *       and added to vacuum data, operations that needs logging.
   */
  if (tdes->topops.last < 0
      && !LOG_ISTRAN_ACTIVE (tdes) && !LOG_ISTRAN_ABORTED (tdes)
      && !LOG_IS_VACUUM_DATA_BUFFER_RECOVERY (rcvindex))
    {
      /*
       * We do not log anything when the transaction is unactive and it is not
       * in the process of aborting.
       */
      return;
    }

  if (log_can_skip_redo_logging (rcvindex, tdes, addr) == true)
    {
      return;
    }

  node =
    prior_lsa_alloc_and_copy_crumbs (thread_p, rectype, rcvindex, addr,
				     0, NULL, num_crumbs, crumbs);
  if (node == NULL)
    {
      return;
    }

  start_lsa = prior_lsa_next_record (thread_p, node, tdes);

  /*
   * Set the LSA on the data page of the corresponding log record for page
   * operation logging.
   *
   * Make sure that I should log. Page operational logging is not done for
   * temporary data of temporary files and volumes
   */
  if (LOG_NEED_TO_SET_LSA (rcvindex, addr->pgptr))
    {
      if (pgbuf_set_lsa (thread_p, addr->pgptr, &start_lsa) == NULL)
	{
	  assert (false);
	  return;
	}
    }

  /*
   * Vacuum data may occupy many pages but has only one log lsa. Check if this
   * is a change on vacuum data and pass start_lsa to be recorded.
   */
  if (LOG_IS_VACUUM_DATA_RECOVERY (rcvindex))
    {
      vacuum_set_vacuum_data_lsa (thread_p, &start_lsa);
    }

  if (!LOG_CHECK_LOG_APPLIER (thread_p)
      && log_does_allow_replication () == true)
    {
      if (rcvindex == RVHF_UPDATE || rcvindex == RVOVF_CHANGE_LINK)
	{
	  LSA_COPY (&tdes->repl_update_lsa, &tdes->tail_lsa);
	}
      else if (rcvindex == RVHF_INSERT)
	{
	  LSA_COPY (&tdes->repl_insert_lsa, &tdes->tail_lsa);
	}
    }
}

/*
 * log_append_undoredo_recdes - LOG UNDO (BEFORE) + REDO (AFTER) RECORD DESCRIPTOR
 *
 * return: nothing
 *
 *   rcvindex(in): Index to recovery function
 *   addr(in): Address (Volume, page, and offset) of data
 *   undo_recdes(in): Undo(before) record descriptor
 *   redo_recdes(in): Redo(after) record descriptor
 *
 */
void
log_append_undoredo_recdes (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex,
			    LOG_DATA_ADDR * addr, const RECDES * undo_recdes,
			    const RECDES * redo_recdes)
{
  log_append_undoredo_recdes2 (thread_p, rcvindex, addr->vfid, addr->pgptr,
			       addr->offset, undo_recdes, redo_recdes);
}

void
log_append_undoredo_recdes2 (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex,
			     const VFID * vfid, PAGE_PTR pgptr,
			     PGLENGTH offset, const RECDES * undo_recdes,
			     const RECDES * redo_recdes)
{
  LOG_CRUMB crumbs[4];
  LOG_CRUMB *undo_crumbs = &crumbs[0];
  LOG_CRUMB *redo_crumbs = &crumbs[2];
  int num_undo_crumbs;
  int num_redo_crumbs;
  LOG_DATA_ADDR addr;

  addr.vfid = vfid;
  addr.pgptr = pgptr;
  addr.offset = offset;

#if 0
  if (rcvindex == RVHF_UPDATE)
    {
      LOG_TDES *tdes = LOG_FIND_CURRENT_TDES (thread_p);
      if (tdes && tdes->null_log.is_set && undo_recdes && redo_recdes)
	{
	  tdes->null_log.recdes = malloc (sizeof (RECDES));
	  if (tdes == NULL)
	    {
	      return;		/* error */
	    }
	  *(tdes->null_log.recdes) = *undo_recdes;
	  tdes->null_log.recdes->data = malloc (undo_recdes->length);
	  if (tdes->null_log.recdes->data == NULL)
	    {
	      free_and_init (tdes->null_log.recdes);
	      return;		/* error */
	    }
	  (void) memcpy (tdes->null_log.recdes->data, undo_recdes->data,
			 undo_recdes->length);
	}
      undo_crumbs[0].length = sizeof (undo_recdes->type);
      undo_crumbs[0].data = (char *) &undo_recdes->type;
      undo_crumbs[1].length = 0;
      undo_crumbs[1].data = NULL;
      num_undo_crumbs = 2;
      redo_crumbs[0].length = sizeof (redo_recdes->type);
      redo_crumbs[0].data = (char *) &redo_recdes->type;
      redo_crumbs[1].length = 0;
      redo_crumbs[1].data = NULL;
      num_redo_crumbs = 2;
      log_append_undoredo_crumbs (rcvindex, addr, num_undo_crumbs,
				  num_redo_crumbs, undo_crumbs, redo_crumbs);
      return;
    }
#endif

  if (undo_recdes != NULL)
    {
      undo_crumbs[0].length = sizeof (undo_recdes->type);
      undo_crumbs[0].data = (char *) &undo_recdes->type;
      undo_crumbs[1].length = undo_recdes->length;
      undo_crumbs[1].data = undo_recdes->data;
      num_undo_crumbs = 2;
    }
  else
    {
      undo_crumbs = NULL;
      num_undo_crumbs = 0;
    }

  if (redo_recdes != NULL)
    {
      redo_crumbs[0].length = sizeof (redo_recdes->type);
      redo_crumbs[0].data = (char *) &redo_recdes->type;
      redo_crumbs[1].length = redo_recdes->length;
      redo_crumbs[1].data = redo_recdes->data;
      num_redo_crumbs = 2;
    }
  else
    {
      redo_crumbs = NULL;
      num_redo_crumbs = 0;
    }

  log_append_undoredo_crumbs (thread_p, rcvindex, &addr, num_undo_crumbs,
			      num_redo_crumbs, undo_crumbs, redo_crumbs);
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * log_append_undo_recdes - LOG UNDO (BEFORE) RECORD DESCRIPTOR
 *
 * return: nothing
 *
 *   rcvindex(in): Index to recovery function
 *   addr(in): Address (Volume, page, and offset) of data
 *   recdes(in): Undo(before) record descriptor
 *
 */
void
log_append_undo_recdes (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex,
			LOG_DATA_ADDR * addr, const RECDES * recdes)
{
  log_append_undo_recdes2 (thread_p, rcvindex, addr->vfid, addr->pgptr,
			   addr->offset, recdes);
}
#endif /* ENABLE_UNUSED_FUNCTION */

void
log_append_undo_recdes2 (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex,
			 const VFID * vfid, PAGE_PTR pgptr, PGLENGTH offset,
			 const RECDES * recdes)
{
  LOG_CRUMB crumbs[2];
  LOG_DATA_ADDR addr;

  addr.vfid = vfid;
  addr.pgptr = pgptr;
  addr.offset = offset;

  if (recdes != NULL)
    {
      crumbs[0].length = sizeof (recdes->type);
      crumbs[0].data = (char *) &recdes->type;
      crumbs[1].length = recdes->length;
      crumbs[1].data = recdes->data;
      log_append_undo_crumbs (thread_p, rcvindex, &addr, 2, crumbs);
    }
  else
    {
      log_append_undo_crumbs (thread_p, rcvindex, &addr, 0, NULL);
    }
}

/*
 * log_append_redo_recdes - LOG REDO (AFTER) RECORD DESCRIPTOR
 *
 * return: nothing
 *
 *   rcvindex(in): Index to recovery function
 *   addr(in): Address (Volume, page, and offset) of data
 *   recdes(in): Redo(after) record descriptor
 *
 */
void
log_append_redo_recdes (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex,
			LOG_DATA_ADDR * addr, const RECDES * recdes)
{
  log_append_redo_recdes2 (thread_p, rcvindex, addr->vfid, addr->pgptr,
			   addr->offset, recdes);
}

void
log_append_redo_recdes2 (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex,
			 const VFID * vfid, PAGE_PTR pgptr, PGLENGTH offset,
			 const RECDES * recdes)
{
  LOG_CRUMB crumbs[2];
  LOG_DATA_ADDR addr;

  addr.vfid = vfid;
  addr.pgptr = pgptr;
  addr.offset = offset;

  if (recdes != NULL)
    {
      crumbs[0].length = sizeof (recdes->type);
      crumbs[0].data = (char *) &recdes->type;
      crumbs[1].length = recdes->length;
      crumbs[1].data = recdes->data;
      log_append_redo_crumbs (thread_p, rcvindex, &addr, 2, crumbs);
    }
  else
    {
      log_append_redo_crumbs (thread_p, rcvindex, &addr, 0, NULL);
    }
}

/*
 * log_append_dboutside_redo - Log redo (after) data for operations outside the db
 *
 * return: nothing
 *
 *   rcvindex(in): Index to recovery function
 *   length(in): Length of redo(after) data
 *   data(in): Redo (after) data
 *
 * NOTE: A log record is constructed to recover external (outside of
 *              database) data by always redoing data during recovery.
 *
 *              During recovery(e.g., system crash recovery), the redo
 *              function described by rcvindex is called with a recovery
 *              structure which contains the page pointer and offset of the
 *              data to recover along with the redo data. It is up to this
 *              function to determine how to redo the data.
 *
 *     1)       The logging of this function is logical since it is for
 *              external data.
 *     2)       Both during the redo and undo phase, dboutside redo is
 *              ignored.
 */
void
log_append_dboutside_redo (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex,
			   int length, const void *data)
{
  LOG_TDES *tdes;		/* Transaction descriptor     */
  int tran_index;
  int error_code = NO_ERROR;
  LOG_PRIOR_NODE *node;

#if defined(CUBRID_DEBUG)
  if (RV_fun[rcvindex].redofun == NULL)
    {
      assert (false);
      return;
    }
#endif /* CUBRID_DEBUG */

  if (log_No_logging)
    {
      /* We are not logging */
      LOG_FLUSH_LOGGING_HAS_BEEN_SKIPPED (thread_p);
      return;
    }

  /* Find transaction descriptor for current logging transaction */
  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
      error_code = ER_LOG_UNKNOWN_TRANINDEX;
      return;
    }

  /*
   * If we are not in a top system operation, the transaction is unactive, and
   * the transaction is not in the process of been aborted, we do nothing.
   */
  if (tdes->topops.last < 0
      && !LOG_ISTRAN_ACTIVE (tdes) && !LOG_ISTRAN_ABORTED (tdes))
    {
      /*
       * We do not log anything when the transaction is unactive and it is not
       * in the process of aborting.
       */
      return;
    }

  node = prior_lsa_alloc_and_copy_data (thread_p, LOG_DBEXTERN_REDO_DATA,
					rcvindex, NULL,
					0, NULL, length, (char *) data);
  if (node == NULL)
    {
      return;
    }

  (void) prior_lsa_next_record (thread_p, node, tdes);
}

/*
 * log_append_postpone - Log postpone after data, for redo
 *
 * return: nothing
 *
 *   rcvindex(in): Index to recovery function
 *   addr(in): Index to recovery function
 *   length(in): Length of postpone redo(after) data
 *   data(in): Postpone redo (after) data
 *
 * NOTE: A postpone data operation is postponed after the transaction
 *              commits. Once it is executed, it becomes a log_redo operation.
 *              This distinction is needed due to log sequence number in the
 *              log and the data pages which are used to avoid redos.
 */
void
log_append_postpone (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex,
		     LOG_DATA_ADDR * addr, int length, const void *data)
{
  LOG_TDES *tdes;		/* Transaction descriptor             */
  LOG_RCV rcv;			/* Recovery structure for execution   */
  bool skipredo;
  LOG_LSA *crash_lsa;
  int tran_index;
  int error_code = NO_ERROR;
  LOG_PRIOR_NODE *node;

#if defined(CUBRID_DEBUG)
  if (addr->pgptr == NULL)
    {
      /*
       * Postpone is always an operation page level logging. Thus, a data page
       * pointer must have been given as part of the address
       */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_POSTPONE_INTERFACE, 0);
      error_code = ER_LOG_POSTPONE_INTERFACE;
      return;
    }
  if (RV_fun[rcvindex].redofun == NULL)
    {
      assert (false);
      return;
    }
#endif /* CUBRID_DEBUG */

  if (log_No_logging)
    {
      /*
       * We are not logging. Execute the postpone operation immediately since
       * we cannot undo
       */
      rcv.length = length;
      rcv.offset = addr->offset;
      rcv.pgptr = addr->pgptr;
      rcv.data = (char *) data;

      if (rcvindex == RVDK_IDDEALLOC_WITH_VOLHEADER)
	{
	  (void) disk_rv_alloctable_with_volheader (thread_p, &rcv, NULL);
	  addr->pgptr = rcv.pgptr;	/* pgptr could be changed by pgbuf_fix_with_retry */
	}
      else
	{
	  assert (RV_fun[rcvindex].redofun != NULL);
	  (void) (*RV_fun[rcvindex].redofun) (thread_p, &rcv);
	}

      LOG_FLUSH_LOGGING_HAS_BEEN_SKIPPED (thread_p);
      log_skip_logging (thread_p, addr);
      return;
    }

  /* Find transaction descriptor for current logging transaction */
  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
      error_code = ER_LOG_UNKNOWN_TRANINDEX;
      return;
    }

  skipredo = log_can_skip_redo_logging (rcvindex, tdes, addr);
  if (skipredo == true
      || (tdes->topops.last < 0 && !LOG_ISTRAN_ACTIVE (tdes)
	  && !LOG_ISTRAN_ABORTED (tdes)))
    {
      /*
       * Warning postpone logging is ignored during REDO recovery, normal
       * rollbacks, and for temporary data pages
       */
      rcv.length = length;
      rcv.offset = addr->offset;
      rcv.pgptr = addr->pgptr;
      rcv.data = (char *) data;

      if (rcvindex == RVDK_IDDEALLOC_WITH_VOLHEADER)
	{
	  assert (skipredo == true);
	  (void) disk_rv_alloctable_with_volheader (thread_p, &rcv, NULL);
	  addr->pgptr = rcv.pgptr;	/* pgptr could be changed by pgbuf_fix_with_retry */
	}
      else
	{
	  assert (RV_fun[rcvindex].redofun != NULL);
	  (void) (*RV_fun[rcvindex].redofun) (thread_p, &rcv);
	  if (skipredo == false)
	    {
	      log_append_redo_data (thread_p, rcvindex, addr, length, data);
	    }
	}

      return;
    }

  /*
   * If the transaction has not logged any record, add a dummy record to
   * start the postpone purposes during the commit.
   */

  if (LSA_ISNULL (&tdes->tail_lsa)
      || (log_is_in_crash_recovery ()
	  && (crash_lsa = log_get_crash_point_lsa ()) != NULL
	  && LSA_LE (&tdes->tail_lsa, crash_lsa)))
    {
      log_append_empty_record (thread_p, LOG_DUMMY_HEAD_POSTPONE, addr);
    }

  node = prior_lsa_alloc_and_copy_data (thread_p, LOG_POSTPONE,
					rcvindex, addr,
					0, NULL, length, (char *) data);
  if (node == NULL)
    {
      return;
    }

  (void) prior_lsa_next_record (thread_p, node, tdes);

  /* Set address early in case there is a crash, because of skip_head */
  if (tdes->topops.last >= 0)
    {
      if (LSA_ISNULL (&tdes->topops.stack[tdes->topops.last].posp_lsa))
	{
	  LSA_COPY (&tdes->topops.stack[tdes->topops.last].posp_lsa,
		    &tdes->tail_lsa);
	}
    }
  else if (LSA_ISNULL (&tdes->posp_nxlsa))
    {
      LSA_COPY (&tdes->posp_nxlsa, &tdes->tail_lsa);
    }

  /*
   * Note: The lsa of the page is not set for postpone log records since
   * the change has not been done (has been postpone) to the page.
   */
}

/*
 * log_run_postpone - Log run redo (after) postpone data
 *
 * return: nothing
 *
 *   rcvindex(in): Index to recovery function
 *   addr(in): Address (Volume, page, and offset) of data
 *   rcv_vpid(in):
 *   length(in): Length of redo(after) data
 *   data(in): Redo (after) data
 *   ref_lsa(in): Log sequence address of original postpone record
 *
 * NOTE: Log run_redo(after) postpone data. This function is only used
 *              when the transaction has been declared as a committed with
 *              postpone actions. A system log record is constructed to
 *              recover data by redoing data during recovery.
 *
 *              During recovery(e.g., system crash recovery), the redo
 *              function described by rcvindex is called with a recovery
 *              structure which contains the page pointer and offset of the
 *              data to recover along with the redo data. It is up to this
 *              function how to redo the data.
 *
 *     1)       The only type of logging accepted by this function is page
 *              operation level logging, thus, an address must be given.
 */
void
log_append_run_postpone (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex,
			 LOG_DATA_ADDR * addr, const VPID * rcv_vpid,
			 int length, const void *data,
			 const LOG_LSA * ref_lsa)
{
  struct log_run_postpone *run_posp;	/* A run postpone record              */
  LOG_TDES *tdes;		/* Transaction descriptor             */
  int tran_index;
  int error_code = NO_ERROR;
  LOG_PRIOR_NODE *node;
  LOG_LSA start_lsa;

  /* Find transaction descriptor for current logging transaction */
  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
      error_code = ER_LOG_UNKNOWN_TRANINDEX;
      return;
    }

  if (tdes->state != TRAN_UNACTIVE_WILL_COMMIT
      && tdes->state != TRAN_UNACTIVE_COMMITTED_WITH_POSTPONE
      && tdes->state != TRAN_UNACTIVE_TOPOPE_COMMITTED_WITH_POSTPONE)
    {
      /* Warning run postpone is ignored when transaction is not committed */
#if defined(CUBRID_DEBUG)
      er_log_debug (ARG_FILE_LINE, "log_run_postpone: Warning run postpone"
		    " logging is ignored when transaction is not committed\n");
#endif /* CUBRID_DEBUG */
      ;				/* Nothing */
    }
  else
    {
      node = prior_lsa_alloc_and_copy_data (thread_p, LOG_RUN_POSTPONE,
					    RV_NOT_DEFINED, NULL,
					    length, (char *) data, 0, NULL);
      if (node == NULL)
	{
	  return;
	}

      run_posp = (struct log_run_postpone *) node->data_header;
      run_posp->data.rcvindex = rcvindex;
      run_posp->data.pageid = rcv_vpid->pageid;
      run_posp->data.volid = rcv_vpid->volid;
      run_posp->data.offset = addr->offset;
      LSA_COPY (&run_posp->ref_lsa, ref_lsa);
      run_posp->length = length;

      start_lsa = prior_lsa_next_record (thread_p, node, tdes);

      /*
       * Set the LSA on the data page of the corresponding log record for page
       * operation logging.
       * Make sure that I should log. Page operational logging is not done for
       * temporary data of temporary files and volumes
       */
      if (addr->pgptr != NULL && LOG_NEED_TO_SET_LSA (rcvindex, addr->pgptr))
	{
	  if (pgbuf_set_lsa (thread_p, addr->pgptr, &start_lsa) == NULL)
	    {
	      assert (false);
	      return;
	    }
	}
    }
}

/*
 * log_append_compensate - LOG COMPENSATE DATA
 *
 * return: nothing
 *
 *   rcvindex(in): Index to recovery function
 *   vpid(in): Volume-page address of compensate data
 *   offset(in): Offset of compensate data
 *   pgptr(in): Page pointer where compensating data resides. It may be
 *                     NULL when the page is not available during recovery.
 *   length(in): Length of compensating data (kind of redo(after) data)
 *   data(in): Compensating data (kind of redo(after) data)
 *   tdes(in/out): State structure of transaction of the log record
 *
 * NOTE: Log a compensating log record. An undo performed during a
 *              rollback or recovery is logged using what is called a
 *              compensation log record. A compensation log record undoes the
 *              redo of an aborted transaction during the redo phase of the
 *              recovery process. Compensating log records are quite useful to
 *              make system and media crash recovery faster. Compensating log
 *              records are redo log records and thus, they are never undone.
 */
void
log_append_compensate (THREAD_ENTRY * thread_p,
		       LOG_RCVINDEX rcvindex, const VPID * vpid,
		       PGLENGTH offset, PAGE_PTR pgptr, int length,
		       const void *data, LOG_TDES * tdes)
{
  struct log_compensate *compensate;	/* Compensate log record      */
  LOG_LSA prev_lsa;		/* LSA of next record to undo */
  LOG_PRIOR_NODE *node;
  LOG_LSA start_lsa;

#if defined(CUBRID_DEBUG)
  int error_code = NO_ERROR;

  if (vpid->volid == NULL_VOLID || vpid->pageid == NULL_PAGEID)
    {
      /*
       * Compensate is always an operation page level logging. Thus, a data page
       * pointer must have been given as part of the address
       */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_COMPENSATE_INTERFACE,
	      0);
      error_code = ER_LOG_COMPENSATE_INTERFACE;
      return;
    }
#endif /* CUBRID_DEBUG */

  node = prior_lsa_alloc_and_copy_data (thread_p, LOG_COMPENSATE,
					rcvindex, NULL,
					length, (char *) data,
					0, (char *) NULL);
  if (node == NULL)
    {
      return;
    }

  LSA_COPY (&prev_lsa, &tdes->undo_nxlsa);

  compensate = (struct log_compensate *) node->data_header;

  compensate->data.rcvindex = rcvindex;
  compensate->data.pageid = vpid->pageid;
  compensate->data.offset = offset;
  compensate->data.volid = vpid->volid;
  LSA_COPY (&compensate->undo_nxlsa, &prev_lsa);
  compensate->length = length;

  start_lsa = prior_lsa_next_record (thread_p, node, tdes);

  /*
   * Set the LSA on the data page of the corresponding log record for page
   * operation logging.
   * Make sure that I should log. Page operational logging is not done for
   * temporary data of temporary files and volumes
   */
  if (pgptr != NULL && pgbuf_set_lsa (thread_p, pgptr, &start_lsa) == NULL)
    {
      assert (false);
      return;
    }

  /* Go back to our undo link */
  LSA_COPY (&tdes->undo_nxlsa, &prev_lsa);
}

/*
 * log_append_logical_compensate - LOG COMPENSATE DATA
 *
 * return: nothing
 *
 *   rcvindex(in): Index to recovery function
 *   tdes(in/out): State structure of transaction of the log record
 *   undo_nxlsa(in): Address of next undo record to rollback after logical
 *                     undo has been done.
 *
 * NOTE:Log a logical/dummy compensating log record. The end of a
 *              logical undo is logged using what it is called a logical/dummy
 *              compensating record. This is needed to allow atomic undoes of
 *              logical undo operations.
 */
void
log_append_logical_compensate (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex,
			       LOG_TDES * tdes, LOG_LSA * undo_nxlsa)
{
  struct log_logical_compensate *logical_comp;	/* end of a logical undo   */
  LOG_PRIOR_NODE *node;

  node = prior_lsa_alloc_and_copy_data (thread_p, LOG_LCOMPENSATE,
					rcvindex, NULL, 0, NULL, 0, NULL);
  if (node == NULL)
    {
      return;
    }

  logical_comp = (struct log_logical_compensate *) node->data_header;

  logical_comp->rcvindex = rcvindex;
  LSA_COPY (&logical_comp->undo_nxlsa, undo_nxlsa);

  (void) prior_lsa_next_record (thread_p, node, tdes);

  /* Go back to our undo link */
  LSA_COPY (&tdes->undo_nxlsa, undo_nxlsa);
}

/*
 * log_append_empty_record -
 *
 * return: nothing
 */
void
log_append_empty_record (THREAD_ENTRY * thread_p, LOG_RECTYPE logrec_type,
			 LOG_DATA_ADDR * addr)
{
  int tran_index;
  bool skip = false;
  LOG_TDES *tdes;
  LOG_PRIOR_NODE *node;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL)
    {
      return;
    }

  if (addr != NULL)
    {
      skip = log_can_skip_redo_logging (RV_NOT_DEFINED, tdes, addr);
      if (skip == true)
	{
	  return;
	}
    }

  node = prior_lsa_alloc_and_copy_data (thread_p, logrec_type,
					RV_NOT_DEFINED, NULL,
					0, NULL, 0, NULL);
  if (node == NULL)
    {
      return;
    }

  (void) prior_lsa_next_record (thread_p, node, tdes);
}

/*
 * log_append_ha_server_state -
 *
 * return: nothing
 */
void
log_append_ha_server_state (THREAD_ENTRY * thread_p, int state)
{
  int tran_index;
  LOG_TDES *tdes;
  struct log_ha_server_state *ha_server_state;
  LOG_PRIOR_NODE *node;
  LOG_LSA start_lsa;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL)
    {
      return;
    }

  node = prior_lsa_alloc_and_copy_data (thread_p, LOG_DUMMY_HA_SERVER_STATE,
					RV_NOT_DEFINED, NULL,
					0, NULL, 0, NULL);
  if (node == NULL)
    {
      return;
    }

  ha_server_state = (struct log_ha_server_state *) node->data_header;
  memset (ha_server_state, 0, sizeof (struct log_ha_server_state));

  ha_server_state->state = state;
  ha_server_state->at_time = time (NULL);

  start_lsa = prior_lsa_next_record (thread_p, node, tdes);

  logpb_flush_pages (thread_p, &start_lsa);
}

/*
 * log_skip_logging_set_lsa -  A log entry was not recorded intentionally
 *                             by the caller. set page LSA
 *
 * return: nothing
*
 *   addr(in): Address (Volume, page, and offset) of data
 *
 * NOTE: A log entry was not recorded intentionally by the caller. For
 *              example, if the data is not accurate, the logging could be
 *              avoided since it will be brought up to date later by the
 *              normal execution of the database.
 *              This function is used to avoid warning of unlogged pages.
 */
void
log_skip_logging_set_lsa (THREAD_ENTRY * thread_p, LOG_DATA_ADDR * addr)
{
  int rv;

  assert (addr && addr->pgptr != NULL);

#if defined(CUBRID_DEBUG)
  if (addr->pgptr == NULL)
    {
      er_log_debug (ARG_FILE_LINE,
		    "log_skip_logging_set_lsa: A data page pointer must"
		    " be given as part of the address... ignored\n");
      return;
    }
#endif /* CUBRID_DEBUG */

  /* Don't need to log */

  rv = pthread_mutex_lock (&log_Gl.prior_info.prior_lsa_mutex);

  (void) pgbuf_set_lsa (thread_p, addr->pgptr, &log_Gl.prior_info.prior_lsa);

  pthread_mutex_unlock (&log_Gl.prior_info.prior_lsa_mutex);

  return;
}

/*
 * log_skip_logging -  A log entry was not recorded intentionally by the
 *                      caller
 *
 * return: nothing
 *
 *   addr(in): Address (Volume, page, and offset) of data
 *
 * NOTE: A log entry was not recorded intentionally by the caller. For
 *              example, if the data is not accurate, the logging could be
 *              avoided since it will be brought up to date later by the
 *              normal execution of the database.
 *              This function is used to avoid warning of unlogged pages.
 */
void
log_skip_logging (THREAD_ENTRY * thread_p, LOG_DATA_ADDR * addr)
{
#if 0
  LOG_TDES *tdes;		/* Transaction descriptor */
  LOG_LSA *page_lsa;
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */
  int tran_index;
  int error_code = NO_ERROR;
#endif

#if defined(CUBRID_DEBUG)
  if (addr->pgptr == NULL)
    {
      er_log_debug (ARG_FILE_LINE,
		    "log_skip_logging: A data page pointer must"
		    " be given as part of the address... ignored\n");
      return;
    }
#endif /* CUBRID_DEBUG */

  return;

#if 0
  if (!pgbuf_is_lsa_temporary (addr->pgptr))
    {
      tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
      tdes = LOG_FIND_TDES (tran_index);
      if (tdes == NULL)
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
	  error_code = ER_LOG_UNKNOWN_TRANINDEX;
	  return;
	}

      /*
       * If the page LSA has not been changed since the lsa checkpoint record,
       * change it to either the checkpoint record or the restart LSA.
       */

      page_lsa = pgbuf_get_lsa (addr->pgptr);

      if (!LSA_ISNULL (&log_Gl.rcv_phase_lsa))
	{
	  if (LSA_GE (&log_Gl.rcv_phase_lsa, page_lsa))
	    {
	      LOG_LSA chkpt_lsa;

	      rv = pthread_mutex_lock (&log_Gl.chkpt_lsa_lock);
	      LSA_COPY (&chkpt_lsa, &log_Gl.hdr.chkpt_lsa);
	      pthread_mutex_unlock (&log_Gl.chkpt_lsa_lock);

	      if (LSA_GT (&chkpt_lsa, &log_Gl.rcv_phase_lsa))
		{
		  (void) pgbuf_set_lsa (thread_p, addr->pgptr, &chkpt_lsa);
		}
	      else
		{
		  (void) pgbuf_set_lsa (thread_p, addr->pgptr,
					&log_Gl.rcv_phase_lsa);
		}
	    }
	}
      else
	{
	  /*
	   * Likely the system is not restarted
	   */
	  if (LSA_GT (&tdes->tail_lsa, page_lsa))
	    {
	      (void) pgbuf_set_lsa (thread_p, addr->pgptr, &tdes->tail_lsa);
	    }
	}
    }
#endif
}

/*
 *
 *          LOGGING FUNCTIONS FOR DATABASE EXTERNAL DATA AT THE CLIENT
 *                    (e.g., MULTIMEDIA EXTERNAL FILES)
 *
 */

/*
 * log_client_name - LOG CLIENT NAME
 *
 * return: nothing
 *
 *   tdes(in/out): State structure of transaction of the log record
 *
 * NOTE: Log the name of the client. This function is used before any
 *              client undo or postpone operation takes over.
 *
 *       Assume that a critical section has already been entered.
 */
static void
log_append_client_name (THREAD_ENTRY * thread_p, LOG_TDES * tdes)
{
  LOG_PRIOR_NODE *node;

  node = prior_lsa_alloc_and_copy_data (thread_p, LOG_CLIENT_NAME,
					RV_NOT_DEFINED, NULL,
					0, NULL, 0, NULL);
  if (node == NULL)
    {
      return;
    }

  memcpy (node->data_header, tdes->client.db_user, LOG_USERNAME_MAX);

  (void) prior_lsa_next_record (thread_p, node, tdes);
}

/*
 * xlog_append_client_undo - LOG UNDO (BEFORE) DATA, FOR USER CLIENT UNDO
 *
 * return: nothing
 *
 *   rcvclient_index(in): Index to recovery function
 *   length(in): Length of undo(before) client data
 *   data(in): undo (before) client data
 *
 * NOTE: Log a client undo data. A log record is constructed to recover
 *              data by undoing the data in the client during aborts and
 *              client and server crashes. It is upto the function described
 *              in rcvclient_index in the client, how to undo the data.
 *              This function is somewhat expensive, the log is flushed
 *              immediately since the log and recovery manager does not
 *              control the client data. Note that this is needed since the
 *              WAL rule must be followed at all the times.
 *
 *     1)       This function accepts only no-page operation logging (i.e.,
 *              logical logging) which is part of the client and associated
 *              with the user and the transaction.
 *     2)       Undo log records are not accepted during recovery or roll
 *              backs. They are ignored by the function.
 *     3)       The actual undo occurs only when the user of the client which
 *              logged the data is connected to any client in the database.
 */
void
xlog_append_client_undo (THREAD_ENTRY * thread_p,
			 LOG_RCVCLIENT_INDEX rcvclient_index, int length,
			 void *data)
{
  LOG_PRIOR_NODE *node;
  LOG_TDES *tdes;		/* Transaction descriptor    */
  int tran_index;
  int error_code = NO_ERROR;
  LOG_LSA start_lsa;

  /* We will log client undo stuff even when we are not logging */

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
      error_code = ER_LOG_UNKNOWN_TRANINDEX;
      return;
    }

  if (!LOG_ISTRAN_ACTIVE (tdes))
    {
      /* Warning, undo logging is ignored during recovery and normal rollbacks */
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
	      ER_LOG_UNDO_LOGGING_DURING_RECOVERY, 0);
      error_code = ER_LOG_UNDO_LOGGING_DURING_RECOVERY;
    }
  else
    {
      if (LSA_ISNULL (&tdes->client_undo_lsa)
	  && LSA_ISNULL (&tdes->client_posp_lsa))
	{
	  /*
	   * Need to append the name of the client associated with the transaction
	   * before we can do anything else
	   */
	  log_append_client_name (thread_p, tdes);
	}

      node = prior_lsa_alloc_and_copy_data (thread_p,
					    LOG_CLIENT_USER_UNDO_DATA,
					    RV_NOT_DEFINED, NULL,
					    0, NULL, 0, NULL);
      if (node == NULL)
	{
	  return;
	}

      start_lsa = prior_lsa_next_record (thread_p, node, tdes);

      /*
       * END append. NOTE: We must flush the log since there is not
       * coordination WAL rule for the client. Thus, the log has to be permanent
       */
      logpb_flush_pages (thread_p, &start_lsa);

      if (tdes->topops.last >= 0)
	{
	  if (LSA_ISNULL
	      (&tdes->topops.stack[tdes->topops.last].client_undo_lsa))
	    {
	      LSA_COPY (&tdes->topops.
			stack[tdes->topops.last].client_undo_lsa,
			&tdes->tail_lsa);
	    }
	}
      else if (LSA_ISNULL (&tdes->client_undo_lsa))
	{
	  LSA_COPY (&tdes->client_undo_lsa, &tdes->tail_lsa);
	}
    }
}

/*
 * xlog_append_client_postpone - LOG POSTPONE CLIENT DATA
 *
 * return: nothing
 *
 *   rcvclient_index(in): Index to recovery function on the client
 *   length(in): Length of postpone client data
 *   data(in): Postpone (after) client data
 *
 * NOTE: A client postpone operation is postponed after the transaction
 *              commits. It is upto the function described in rcvindex in the
 *              client, how to redo the data.
 *
 *     1)       This function accepts only no-page operation logging (i.e.,
 *              logical logging) which is part of the client and associated
 *              with the user and the transaction.
 *     2)       User postpone log records are not accepted during recovery or
 *              roll backs. They are ignored by the function.
 *     3)       The actual redo occurs only when the user of the client which
 *              logged the data is connected to any client in the database.
 */
void
xlog_append_client_postpone (THREAD_ENTRY * thread_p,
			     LOG_RCVCLIENT_INDEX rcvclient_index, int length,
			     void *data)
{
  LOG_TDES *tdes;		/* Transaction descriptor       */
  int tran_index;
  int error_code = NO_ERROR;
  LOG_PRIOR_NODE *node;

  /* We will log client undo stuff even when we are not logging */

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
      error_code = ER_LOG_UNKNOWN_TRANINDEX;
      return;
    }

  if (!LOG_ISTRAN_ACTIVE (tdes))
    {
      /*
       * Warning postpone logging is ignored during REDO recovery and normal roll
       * backs
       */
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
	      ER_LOG_POSTPONE_LOGGING_DURING_RECOVERY, 0);
      error_code = ER_LOG_POSTPONE_LOGGING_DURING_RECOVERY;
    }
  else
    {
      if (LSA_ISNULL (&tdes->client_undo_lsa)
	  && LSA_ISNULL (&tdes->client_posp_lsa))
	{
	  /*
	   * Need to append the name of the client associated with the transaction
	   * before we can do anything else
	   */
	  log_append_client_name (thread_p, tdes);
	}

      node = prior_lsa_alloc_and_copy_data (thread_p,
					    LOG_CLIENT_USER_POSTPONE_DATA,
					    RV_NOT_DEFINED, NULL,
					    0, NULL, 0, NULL);
      if (node == NULL)
	{
	  return;
	}

      (void) prior_lsa_next_record (thread_p, node, tdes);

      /*
       * END append. NOTE: THE LOG does not need to be flushed like in the
       * client_undo since the postpone action does not take effect until
       * the transaction is committed.
       */

      if (tdes->topops.last >= 0)
	{
	  if (LSA_ISNULL
	      (&tdes->topops.stack[tdes->topops.last].client_posp_lsa))
	    {
	      LSA_COPY (&tdes->topops.
			stack[tdes->topops.last].client_posp_lsa,
			&tdes->tail_lsa);
	    }
	}
      else if (LSA_ISNULL (&tdes->client_posp_lsa))
	{
	  LSA_COPY (&tdes->client_posp_lsa, &tdes->tail_lsa);
	}
    }
}

/*
 * log_append_savepoint - DECLARE A USER SAVEPOINT
 *
 * return: LSA
 *
 *   savept_name(in): Name of the savepoint
 *
 * NOTE: A savepoint is established for the current transaction, so
 *              that future transaction actions can be rolled back to this
 *              established savepoint. We call this operation a partial abort
 *              (rollback). That is, all database actions affected by the
 *              transaction after the savepoint are undone, and all effects
 *              of the transaction preceding the savepoint remain. The
 *              transaction can then continue executing other database
 *              statements. It is permissible to abort to the same savepoint
 *              repeatedly within the same transaction.
 *              If the same savepoint name is used in multiple savepoint
 *              declarations within the same transaction, then only the latest
 *              savepoint with that name is available for aborts and the
 *              others are forgotten.
 *              There are no limits on the number of savepoints that a
 *              transaction can have.
 */
LOG_LSA *
log_append_savepoint (THREAD_ENTRY * thread_p, const char *savept_name)
{
  struct log_savept *savept;	/* A savept log record                  */
  LOG_TDES *tdes;		/* Transaction descriptor               */
  int length;			/* Length of the name of the save point */
  int tran_index;
  int error_code;
  LOG_PRIOR_NODE *node;

  /* Find transaction descriptor for current logging transaction */

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
      error_code = ER_LOG_UNKNOWN_TRANINDEX;
      return NULL;
    }

  if (!LOG_ISTRAN_ACTIVE (tdes))
    {
      /*
       * Error, a user savepoint cannot be added when the transaction is not
       * active
       */
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_LOG_CANNOT_ADD_SAVEPOINT, 0);
      error_code = ER_LOG_CANNOT_ADD_SAVEPOINT;
      return NULL;
    }

  if (savept_name == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_NONAME_SAVEPOINT, 0);
      error_code = ER_LOG_NONAME_SAVEPOINT;
      return NULL;
    }

  length = strlen (savept_name) + 1;

  node = prior_lsa_alloc_and_copy_data (thread_p, LOG_SAVEPOINT,
					RV_NOT_DEFINED, NULL,
					length, (char *) savept_name,
					0, (char *) NULL);
  if (node == NULL)
    {
      return NULL;
    }

  savept = (struct log_savept *) node->data_header;
  savept->length = length;
  LSA_COPY (&savept->prv_savept, &tdes->savept_lsa);

  (void) prior_lsa_next_record (thread_p, node, tdes);

  LSA_COPY (&tdes->savept_lsa, &tdes->tail_lsa);

  mnt_tran_savepoints (thread_p);

  return &tdes->savept_lsa;
}

/*
 * log_find_savept_lsa - FIND LSA ADDRESS OF GIVEN SAVEPOINT
 *
 * return: savept_lsa or NULL
 *
 *   savept_name(in):  Name of the savept
 *   tdes(in): State structure of transaction of the log record  or NULL
 *                when unknown
 *   savept_lsa(in/out): Address of the savept_name
 *
 * NOTE:The LSA address of the given savept_name is found.
 */
static LOG_LSA *
log_get_savepoint_lsa (THREAD_ENTRY * thread_p, const char *savept_name,
		       LOG_TDES * tdes, LOG_LSA * savept_lsa)
{
  char *ptr;			/* Pointer to savepoint name       */
  char log_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT], *aligned_log_pgbuf;
  LOG_PAGE *log_pgptr = NULL;	/* Log page pointer where a
				 * savepoint log record is located
				 */
  LOG_RECORD_HEADER *log_rec;	/* Pointer to log record           */
  struct log_savept *savept;	/* A savepoint log record          */
  LOG_LSA prev_lsa;		/* Previous savepoint              */
  LOG_LSA log_lsa;
  int length;			/* Length of savepoint name        */
  bool found = false;

  aligned_log_pgbuf = PTR_ALIGN (log_pgbuf, MAX_ALIGNMENT);

  /* Find the savepoint LSA, for the given savepoint name */
  LSA_COPY (&prev_lsa, &tdes->savept_lsa);

  log_pgptr = (LOG_PAGE *) aligned_log_pgbuf;

  while (!LSA_ISNULL (&prev_lsa) && found == false)
    {

      if (logpb_fetch_page (thread_p, prev_lsa.pageid, log_pgptr) == NULL)
	{
	  break;
	}

      savept_lsa->pageid = log_lsa.pageid = prev_lsa.pageid;

      while (found == false && prev_lsa.pageid == log_lsa.pageid)
	{
	  /* Find the savepoint record */
	  savept_lsa->offset = log_lsa.offset = prev_lsa.offset;
	  log_rec = LOG_GET_LOG_RECORD_HEADER (log_pgptr, &log_lsa);
	  if (log_rec->type != LOG_SAVEPOINT && log_rec->trid != tdes->trid)
	    {
	      /* System error... */
	      er_log_debug (ARG_FILE_LINE,
			    "log_find_savept_lsa: Corrupted log rec");
	      LSA_SET_NULL (&prev_lsa);
	      break;
	    }

	  /* Advance the pointer to read the savepoint log record */

	  LOG_READ_ADD_ALIGN (thread_p, sizeof (*log_rec), &log_lsa,
			      log_pgptr);
	  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*savept),
					    &log_lsa, log_pgptr);

	  savept =
	    (struct log_savept *) ((char *) log_pgptr->area + log_lsa.offset);
	  LSA_COPY (&prev_lsa, &savept->prv_savept);
	  length = savept->length;

	  LOG_READ_ADD_ALIGN (thread_p, sizeof (*savept), &log_lsa,
			      log_pgptr);
	  /*
	   * Is the name contained in only one buffer, or in several buffers
	   */

	  if (log_lsa.offset + length < (int) LOGAREA_SIZE)
	    {
	      /* Savepoint name is in one buffer */
	      ptr = (char *) log_pgptr->area + log_lsa.offset;
	      if (strcmp (savept_name, ptr) == 0)
		{
		  found = true;
		}
	    }
	  else
	    {
	      /* Need to copy the data into a contiguous area */
	      int area_offset;	/* The area offset                       */
	      int remains_length;	/* Length of data that remains to be
					 * copied
					 */
	      unsigned int copy_length;	/* Length to copy into area              */

	      ptr = (char *) db_private_alloc (thread_p, length);
	      if (ptr == NULL)
		{
		  LSA_SET_NULL (&prev_lsa);
		  break;
		}
	      /* Copy the name */
	      remains_length = length;
	      area_offset = 0;
	      while (remains_length > 0)
		{
		  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, 0, &log_lsa,
						    log_pgptr);
		  if (log_lsa.offset + remains_length < (int) LOGAREA_SIZE)
		    {
		      copy_length = remains_length;
		    }
		  else
		    {
		      copy_length = LOGAREA_SIZE - (int) (log_lsa.offset);
		    }

		  memcpy (ptr + area_offset,
			  (char *) log_pgptr->area + log_lsa.offset,
			  copy_length);
		  remains_length -= copy_length;
		  area_offset += copy_length;
		  log_lsa.offset += copy_length;
		}
	      if (strcmp (savept_name, ptr) == 0)
		{
		  found = true;
		}
	      db_private_free_and_init (thread_p, ptr);
	    }
	}
    }

  if (found)
    {
      return savept_lsa;
    }
  else
    {
      LSA_SET_NULL (savept_lsa);
      return NULL;
    }
}

/*
 *
 *       FUNCTIONS RELATED TO TERMINATION OF TRANSACTIONS AND OPERATIONS
 *
 */

/*
 * log_start_system_op - Start a macro system operation
 *
 * return: lsa of parent  or NULL in case of error.
 *
 */
LOG_LSA *
log_start_system_op (THREAD_ENTRY * thread_p)
{
  LOG_TDES *tdes;		/* Transaction descriptor */
  int tran_index;
  int error_code = NO_ERROR;

  /*
   * Remember the current tail of the transaction, so we can allow partial
   * aborts or commits of nested top actions
   */

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
      error_code = ER_LOG_UNKNOWN_TRANINDEX;
      return NULL;
    }

  if (LOG_ISRESTARTED ())
    {
#if defined(SERVER_MODE)
      assert (tdes->cs_topop.cs_index == CRITICAL_SECTION_COUNT
	      + css_get_max_conn () + NUM_MASTER_CHANNEL + tdes->tran_index);
      assert (tdes->cs_topop.name == css_Csect_name_tdes);
#endif

      csect_enter_critical_section (thread_p, &tdes->cs_topop, INF_WAIT);
    }
  if (tdes->topops.max == 0 || (tdes->topops.last + 1) >= tdes->topops.max)
    {
      if (logtb_realloc_topops_stack (tdes, 1) == NULL)
	{
	  /* Out of memory */
	  if (LOG_ISRESTARTED ())
	    {
#if defined(SERVER_MODE)
	      assert (tdes->cs_topop.cs_index == CRITICAL_SECTION_COUNT
		      + css_get_max_conn () + NUM_MASTER_CHANNEL
		      + tdes->tran_index);
	      assert (tdes->cs_topop.name == css_Csect_name_tdes);
#endif

	      csect_exit_critical_section (thread_p, &tdes->cs_topop);
	    }
	  error_code = ER_OUT_OF_VIRTUAL_MEMORY;
	  return NULL;
	}
    }
  /*
   * NOTE if tdes->topops.last >= 0, there is an already defined
   * top system operation.
   */
  tdes->topops.last++;
  LSA_COPY (&tdes->topops.stack[tdes->topops.last].lastparent_lsa,
	    &tdes->tail_lsa);
  LSA_COPY (&tdes->topop_lsa, &tdes->tail_lsa);

  LSA_SET_NULL (&tdes->topops.stack[tdes->topops.last].posp_lsa);
  LSA_SET_NULL (&tdes->topops.stack[tdes->topops.last].client_posp_lsa);
  LSA_SET_NULL (&tdes->topops.stack[tdes->topops.last].client_undo_lsa);

  mnt_tran_start_topops (thread_p);

  return &tdes->topops.stack[tdes->topops.last].lastparent_lsa;
}

/*
 * log_end_system_op - END A MACRO SYSTEM OPERATION
 *
 * return: state of end of the system operation
 *
 *   result(in): Result of the nested top action
 *
 * NOTE: Make a macro system operation either permanent (commit) or
 *              forget about it (abort). The system operation is not
 *              associated with the current transaction.
 */
TRAN_STATE
log_end_system_op (THREAD_ENTRY * thread_p, LOG_RESULT_TOPOP result)
{
  LOG_TDES *tdes;		/* Transaction descriptor        */
  TRAN_STATE save_state;	/* The current state of the transaction. Must be
				 * returned to this state
				 */
  TRAN_STATE state;
  int tran_index;
  int error_code = NO_ERROR;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
      error_code = ER_LOG_UNKNOWN_TRANINDEX;
      return TRAN_UNACTIVE_UNKNOWN;
    }

  if (tdes->topops.last < 0)
    {
      /* There is not any active top system operation */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_NOTACTIVE_TOPOPS, 0);
      error_code = ER_LOG_NOTACTIVE_TOPOPS;
      return TRAN_UNACTIVE_UNKNOWN;
    }

  save_state = tdes->state;

  /*
   * A top system operation should not have any client recovery stuff or
   * distributed transaction stuff
   */

  if (!LOG_ISTRAN_ACTIVE (tdes))
    {
      /*
       * The transaction is not active. That is, it is in the process of commit
       * or abort. It is possible that the fate of the top system operation can
       * be decided at this moment.  Nested topops, however, must still be
       * allowed to attach to their parents.
       */
      if (tdes->topops.last == 1
	  && result == LOG_RESULT_TOPOP_ATTACH_TO_OUTER)
	{
	  /*
	   *
	   * This could be the case of in the middle of an abort. The top system
	   * operation must be committed to undo whatever we were doing.
	   */
	  result = LOG_RESULT_TOPOP_COMMIT;
	}
    }

  if (result != LOG_RESULT_TOPOP_ATTACH_TO_OUTER
      && !LSA_ISNULL (&tdes->tail_lsa)
      && (LSA_ISNULL (&tdes->topops.stack[tdes->topops.last].lastparent_lsa)
	  || LSA_GT (&tdes->tail_lsa,
		     &tdes->topops.stack[tdes->topops.last].lastparent_lsa)))
    {
      /*
       * A top system operation executed something and it is not attached back
       * to its parent, therefore, the top system operation is either committed
       * or aborted at this point and will not depend on the outcome of the
       * parent
       */
      if (result == LOG_RESULT_TOPOP_COMMIT)
	{
	  if (!LOG_CHECK_LOG_APPLIER (thread_p)
	      && log_does_allow_replication () == true)
	    {
	      /* for the replication agent guarantee the order of transaction */
	      /* for CC(Click Counter) : at here */
	      log_append_repl_info (thread_p, tdes, false);
	    }

	  /*
	   * The top system operation may have some commit postpone
	   * operations to do. If it does, we need to execute them at this
	   * point. We need to remove postpone operations of nested top system
	   * operations.
	   */
	  log_do_postpone (thread_p, tdes,
			   &tdes->topops.stack[tdes->topops.last].posp_lsa,
			   LOG_COMMIT_TOPOPE_WITH_POSTPONE);

	  if (!LSA_ISNULL
	      (&tdes->topops.stack[tdes->topops.last].client_posp_lsa))
	    {
	      log_append_topope_commit_client_loose_ends (thread_p, tdes);
	      /*
	       * Now the client transaction manager should request the postpone
	       * actions until all of them become exhausted.
	       */
	      state = tdes->state;
	    }
	  else
	    {
	      state = log_complete_system_op (thread_p, tdes, result,
					      save_state);
	    }
	}
      else
	{
	  if (!LOG_CHECK_LOG_APPLIER (thread_p)
	      && log_does_allow_replication () == true)
	    {
	      repl_log_abort_after_lsa (tdes,
					&tdes->topops.stack[tdes->
							    topops.last].
					lastparent_lsa);
	    }

	  /* Abort the top system operation */
	  tdes->state = TRAN_UNACTIVE_ABORTED;
	  log_rollback (thread_p, tdes,
			&tdes->topops.stack[tdes->topops.
					    last].lastparent_lsa);

	  log_rollback_classrepr_cache (thread_p, tdes,
					&tdes->topops.stack
					[tdes->topops.last].lastparent_lsa);

	  /* Are there any loose ends to be done in the client ? */
	  if (!LSA_ISNULL
	      (&tdes->topops.stack[tdes->topops.last].client_undo_lsa))
	    {
	      log_append_topope_abort_client_loose_ends (thread_p, tdes);
	      /*
	       * Now the client transaction manager should request
	       * all client undo actions.
	       */
	      state = tdes->state;
	    }
	  else
	    {
	      state = log_complete_system_op (thread_p, tdes, result,
					      save_state);
	    }
	}
    }
  else
    {
      /*
       * The top system operation did not do anything, or the result is to
       * attach the transaction to back to its parent
       */

      if (result == LOG_RESULT_TOPOP_ATTACH_TO_OUTER)
	{
	  state = save_state;
	}
      else if (result == LOG_RESULT_TOPOP_COMMIT)
	{
	  state = TRAN_UNACTIVE_COMMITTED;
	}
      else
	{
	  state = TRAN_UNACTIVE_ABORTED;
	  if (!LOG_CHECK_LOG_APPLIER (thread_p)
	      && log_does_allow_replication () == true)
	    {
	      repl_log_abort_after_lsa (tdes,
					&tdes->topops.stack[tdes->
							    topops.last].
					lastparent_lsa);
	    }
	}

      result = LOG_RESULT_TOPOP_ATTACH_TO_OUTER;
      (void) log_complete_system_op (thread_p, tdes, result, save_state);
    }

  if (LOG_ISRESTARTED ())
    {
#if defined(SERVER_MODE)
      assert (tdes->cs_topop.cs_index == CRITICAL_SECTION_COUNT
	      + css_get_max_conn () + NUM_MASTER_CHANNEL + tdes->tran_index);
      assert (tdes->cs_topop.name == css_Csect_name_tdes);
#endif

      csect_exit_critical_section (thread_p, &tdes->cs_topop);
    }

  mnt_tran_end_topops (thread_p);

  return state;
}

/*
 * log_get_parent_lsa_system_op - Get parent lsa of top operation
 *
 * return: lsa of parent or NULL
 *
 *   parent_lsa(in/out): The topop LSA for current top operation
 *
 * NOTE: Find the address of the parent of top operation.
 */
LOG_LSA *
log_get_parent_lsa_system_op (THREAD_ENTRY * thread_p, LOG_LSA * parent_lsa)
{
  LOG_TDES *tdes;		/* Transaction descriptor        */
  int tran_index;
  int error_code = NO_ERROR;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
      error_code = ER_LOG_UNKNOWN_TRANINDEX;
      return NULL;
    }

  if (tdes->topops.last < 0)
    {
      LSA_SET_NULL (parent_lsa);
      return NULL;
    }

  LSA_COPY (parent_lsa,
	    &tdes->topops.stack[tdes->topops.last].lastparent_lsa);

  return parent_lsa;
}

/*
 * log_is_tran_in_system_op - Find if current transaction is doing a top nested
 *                         system operation
 *
 * return:
 *
 * NOTE: Find if the current transaction is doing a top nested system
 *              operation.
 */
bool
log_is_tran_in_system_op (THREAD_ENTRY * thread_p)
{
  LOG_TDES *tdes;		/* Transaction descriptor */
  int tran_index;
  int error_code = NO_ERROR;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
      error_code = ER_LOG_UNKNOWN_TRANINDEX;
      return false;
    }

  if (tdes->topops.last < 0 && LSA_ISNULL (&tdes->savept_lsa))
    {
      return false;
    }
  else
    {
      return true;
    }
}

/*
 * log_can_skip_undo_logging - Is it safe to skip undo logging for given file ?
 *
 * return:
 *
 *   rcvindex(in): Index to recovery function
 *   tdes(in):
 *   addr(in):
 *
 * NOTE: Find if it is safe to skip undo logging for data related to
 *              given file.
 *              Some rcvindex values should never be skipped.
 */
static bool
log_can_skip_undo_logging (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex,
			   const LOG_TDES * tdes, LOG_DATA_ADDR * addr)
{
  bool canskip = false;
  bool has_undolog;
  FILE_TYPE ftype;

  /*
   * Some log record types (rcvindex) should never be skipped.
   * In the case of LINK_PERM_VOLEXT, the link of a permanent temp
   * volume must be logged to support media failures.
   * See also canskip_redo.
   */
  if (LOG_ISUNSAFE_TO_SKIP_RCVINDEX (rcvindex))
    {
      return false;
    }

  if (thread_is_vacuum_worker (thread_p))
    {
      return true;
    }

  /*
   * Operation level undo can be skipped on temporary pages. For example,
   * those of temporary files.
   * No-operational level undo (i.e., logical logging) can be skipped for
   * temporary files.
   */

  if ((addr->pgptr != NULL && pgbuf_is_lsa_temporary (addr->pgptr) == true))
    {
      return true;
    }

  if (addr->pgptr == NULL && addr->vfid != NULL)
    {
      /* TODO: need to access ftype directly from somewhere... PERFORMANCE */
      ftype = file_get_type (thread_p, addr->vfid);
      if (ftype == FILE_TMP || ftype == FILE_TMP_TMP)
	{
	  return true;
	}
    }


  if (addr->vfid != NULL
      && file_is_new_file_with_has_undolog (thread_p, addr->vfid,
					    &has_undolog) == FILE_NEW_FILE
      && has_undolog == false)
    {
      /*
       * We may be able to skip undo logging if we are not in a savepoint or
       * a top system operation.
       */
      if (tdes->topops.last < 0 && LSA_ISNULL (&tdes->savept_lsa))
	{
	  canskip = true;
	}
      else
	{
	  /*
	   * We cannot skip the undo logging. In addition we must declare that
	   * logging must be done on this file from now on, otherwise, we may
	   * not be able to rollback properly. For example:
	   * insert (without top op), delete (with top op),
	   * insert (without top op), rollback. We may not be able to undo the
	   * delete due to lack of space.
	   */
	  (void) file_new_set_has_undolog (thread_p, addr->vfid);
	}
    }

  return canskip;
}

/*
 * log_can_skip_redo_logging - IS IT SAFE TO SKIP REDO LOGGING FOR GIVEN FILE ?
 *
 * return:
 *
 *   rcvindex(in): Index to recovery function
 *   ignore_tdes(in):
 *   addr(in): Address (Volume, page, and offset) of data
 *
 * NOTE: Find if it is safe to skip redo logging for data related to
 *              given file. Redo logging can be skip on any temporary page.
 *              For example, pages of temporary files on any volume.
 *              Some rcvindex values should never be skipped.
 */
static bool
log_can_skip_redo_logging (LOG_RCVINDEX rcvindex,
			   const LOG_TDES * ignore_tdes, LOG_DATA_ADDR * addr)
{
  /*
   * Some log record types (rcvindex) should never be skipped.
   * In the case of LINK_PERM_VOLEXT, the link of a permanent temp
   * volume must be logged to support media failures.
   * See also canskip_undo.
   */
  if (LOG_ISUNSAFE_TO_SKIP_RCVINDEX (rcvindex))
    {
      return false;
    }

  if (LOG_IS_VACUUM_DATA_RECOVERY (rcvindex))
    {
      return false;
    }

  if (rcvindex == RVVAC_DROPPED_FILE_ADD)
    {
      return false;
    }

  /*
   * Operation level redo can be skipped on temporary pages. For example,
   * those of temporary files
   */
  if (addr->pgptr != NULL && pgbuf_is_lsa_temporary (addr->pgptr) == true)
    {
      return true;
    }
  else
    {
      return false;
    }
}

/*
 * log_append_commit_postpone - APPEND COMMIT WITH POSTPONE
 *
 * return: nothing
 *
 *   tdes(in/out): State structure of transaction being committed
 *   start_posplsa(in): Address where the first postpone log record start
 *
 * NOTE: The transaction is declared as committed with postpone actions
 *              The transaction is not fully committed until all postpone
 *              actions are executed.
 *
 *       The postpone operations are not invoked by this function.
 */
static void
log_append_commit_postpone (THREAD_ENTRY * thread_p, LOG_TDES * tdes,
			    LOG_LSA * start_postpone_lsa)
{
  struct log_start_postpone *start_posp;	/* Start postpone actions */
  LOG_PRIOR_NODE *node;
  LOG_LSA start_lsa;

  node = prior_lsa_alloc_and_copy_data (thread_p, LOG_COMMIT_WITH_POSTPONE,
					RV_NOT_DEFINED, NULL,
					0, NULL, 0, NULL);
  if (node == NULL)
    {
      return;
    }

  start_posp = (struct log_start_postpone *) node->data_header;
  LSA_COPY (&start_posp->posp_lsa, start_postpone_lsa);

  start_lsa = prior_lsa_next_record (thread_p, node, tdes);

  tdes->state = TRAN_UNACTIVE_COMMITTED_WITH_POSTPONE;

  logpb_flush_pages (thread_p, &start_lsa);
}

/*
 * log_append_topope_commit_postpone - APPEND TOP SYSTEM COMMIT WITH POSTPONE
 *
 * return: nothing
 *
 *   tdes(in): State structure of transaction being committed
 *   start_posplsa(in): Address where the first postpone log record start
 *
 * NOTE: The top system operation is declared as committed with
 *              postpone actions. The top system operation is not fully
 *              committed until all postpone actions are executed.
 *
 *       The postpone operations are not invoked by this function.
 */
static void
log_append_topope_commit_postpone (THREAD_ENTRY * thread_p, LOG_TDES * tdes,
				   LOG_LSA * start_postpone_lsa)
{
  struct log_topope_start_postpone *top_start_posp;	/* Start postpone
							 * of top system
							 * operations
							 */
  LOG_PRIOR_NODE *node;
  LOG_LSA start_lsa;

  node = prior_lsa_alloc_and_copy_data (thread_p,
					LOG_COMMIT_TOPOPE_WITH_POSTPONE,
					RV_NOT_DEFINED, NULL,
					0, NULL, 0, NULL);
  if (node == NULL)
    {
      return;
    }

  top_start_posp = (struct log_topope_start_postpone *) node->data_header;
  LSA_COPY (&top_start_posp->lastparent_lsa,
	    &tdes->topops.stack[tdes->topops.last].lastparent_lsa);
  LSA_COPY (&top_start_posp->posp_lsa, start_postpone_lsa);

  start_lsa = prior_lsa_next_record (thread_p, node, tdes);

  tdes->state = TRAN_UNACTIVE_TOPOPE_COMMITTED_WITH_POSTPONE;
  logpb_flush_pages (thread_p, &start_lsa);
}

/*
 * log_append_commit_client_loose_ends - APPEND COMMIT WITH CLIENT LOOSE ENDS
 *
 * return: nothing
 *
 *   tdes(in/out): State structure of transaction being committed
 *
 * NOTE:The transaction is declared as committed with client loose
 *              ends. The transaction is not fully committed until all client
 *              loose ends postpone actions are executed.
 *
 *      The client_user postpone operations are not invoked by this
 *              function.
 */
void
log_append_commit_client_loose_ends (THREAD_ENTRY * thread_p, LOG_TDES * tdes)
{
  struct log_start_client *start_client;	/* Start client actions */
  LOG_PRIOR_NODE *node;
  LOG_LSA start_lsa;

  node = prior_lsa_alloc_and_copy_data (thread_p,
					LOG_COMMIT_WITH_CLIENT_USER_LOOSE_ENDS,
					RV_NOT_DEFINED, NULL,
					0, NULL, 0, NULL);
  if (node == NULL)
    {
      return;
    }

  start_client = (struct log_start_client *) node->data_header;
  LSA_COPY (&start_client->lsa, &tdes->client_posp_lsa);

  start_lsa = prior_lsa_next_record (thread_p, node, tdes);

  tdes->state = TRAN_UNACTIVE_COMMITTED_WITH_CLIENT_USER_LOOSE_ENDS;
  logpb_flush_pages (thread_p, &start_lsa);
}

/*
 * log_append_abort_client_loose_ends - APPEND ABORT WITH CLIENT LOOSE ENDS
 *
 * return: nothing
 *
 *   tdes(in/out): State structure of transaction being aborted
 *
 * NOTE:The transaction is declared as aborted with client loose ends.
 *              The transaction is not fully aborted until all client loose
 *              ends undo actions are executed.
 *
 *      The client_user undo operations are not invoked by this
 *              function.
 */
void
log_append_abort_client_loose_ends (THREAD_ENTRY * thread_p, LOG_TDES * tdes)
{
  struct log_start_client *start_client;	/* Start client actions */
  LOG_PRIOR_NODE *node;
  LOG_LSA start_lsa;

  node = prior_lsa_alloc_and_copy_data (thread_p,
					LOG_ABORT_WITH_CLIENT_USER_LOOSE_ENDS,
					RV_NOT_DEFINED, NULL,
					0, NULL, 0, NULL);
  if (node == NULL)
    {
      return;
    }

  start_client = (struct log_start_client *) node->data_header;
  LSA_COPY (&start_client->lsa, &tdes->client_undo_lsa);

  start_lsa = prior_lsa_next_record (thread_p, node, tdes);

  tdes->state = TRAN_UNACTIVE_ABORTED_WITH_CLIENT_USER_LOOSE_ENDS;
  logpb_flush_pages (thread_p, &start_lsa);
}

/*
 * log_append_topope_commit_client_loose_ends - APPEND TOP SYSTEM COMMIT WITH
 *                                             CLIENT LOOSE ENDS
 *
 * return: nothing
 *
 *   tdes(in/out): State structure of transaction being committed
 *
 * NOTE: The top system operation is declared as committed with client
 *              loose ends. The top system operation is not fully committed
 *              until all client loose ends postpone actions are executed.
 *
 *       The client_user postpone operations are not invoked by this
 *              function.
 */
static void
log_append_topope_commit_client_loose_ends (THREAD_ENTRY * thread_p,
					    LOG_TDES * tdes)
{
  struct log_topope_start_client *top_start_client;
  LOG_PRIOR_NODE *node;
  LOG_LSA start_lsa;

  node = prior_lsa_alloc_and_copy_data (thread_p,
					LOG_COMMIT_TOPOPE_WITH_CLIENT_USER_LOOSE_ENDS,
					RV_NOT_DEFINED, NULL,
					0, NULL, 0, NULL);
  if (node == NULL)
    {
      return;
    }

  top_start_client = (struct log_topope_start_client *) node->data_header;
  LSA_COPY (&top_start_client->lastparent_lsa,
	    &tdes->topops.stack[tdes->topops.last].lastparent_lsa);
  LSA_COPY (&top_start_client->lsa, &tdes->client_posp_lsa);

  start_lsa = prior_lsa_next_record (thread_p, node, tdes);

  tdes->state = TRAN_UNACTIVE_XTOPOPE_COMMITTED_WITH_CLIENT_USER_LOOSE_ENDS;
  logpb_flush_pages (thread_p, &start_lsa);
}

/*
 * log_append_topope_abort_client_loose_ends - APPEND TOP SYSTEM ABORT WITH
 *                                            CLIENT LOOSE ENDS
 *
 * return: nothing
 *
 *   tdes(in/out): State structure of transaction being committed
 *
 * NOTE: The top system operation is declared as aborted with client
 *              loose ends. The top system operation is not fully aborted
 *              until all client loose ends postpone actions are executed.
 *
 *       The client_user undo operations are not invoked by this
 *              function.
 */
static void
log_append_topope_abort_client_loose_ends (THREAD_ENTRY * thread_p,
					   LOG_TDES * tdes)
{
  struct log_topope_start_client *top_start_client;
  LOG_PRIOR_NODE *node;
  LOG_LSA start_lsa;

  node = prior_lsa_alloc_and_copy_data (thread_p,
					LOG_ABORT_TOPOPE_WITH_CLIENT_USER_LOOSE_ENDS,
					RV_NOT_DEFINED, NULL,
					0, NULL, 0, NULL);
  if (node == NULL)
    {
      return;
    }
  top_start_client = (struct log_topope_start_client *) node->data_header;

  LSA_COPY (&top_start_client->lastparent_lsa,
	    &tdes->topops.stack[tdes->topops.last].lastparent_lsa);
  LSA_COPY (&top_start_client->lsa, &tdes->client_undo_lsa);

  start_lsa = prior_lsa_next_record (thread_p, node, tdes);

  tdes->state = TRAN_UNACTIVE_TOPOPE_ABORTED_WITH_CLIENT_USER_LOOSE_ENDS;

  logpb_flush_pages (thread_p, &start_lsa);
}

/*
 * log_append_repl_info_internal - APPEND REPLICATION LOG RECORD
 *
 * return: nothing
 *
 *   thread_p(in):
 *   tdes(in): State structure of transaction being committed/aborted.
 *   is_commit(in):
 *   with_lock(in):
 *
 * NOTE:critical section is set by its caller function.
 */
static void
log_append_repl_info_internal (THREAD_ENTRY * thread_p, LOG_TDES * tdes,
			       bool is_commit, int with_lock)
{
  LOG_REPL_RECORD *repl_rec;
  struct log_replication *log;
  LOG_PRIOR_NODE *node;

  if (tdes->append_repl_recidx == -1	/* the first time */
      || is_commit)
    {
      tdes->append_repl_recidx = 0;
    }

  if (tdes->append_repl_recidx < tdes->cur_repl_record)
    {
      /* there is any replication info */
      while (tdes->append_repl_recidx < tdes->cur_repl_record)
	{
	  repl_rec = (LOG_REPL_RECORD *)
	    (&(tdes->repl_records[tdes->append_repl_recidx]));

	  if ((repl_rec->repl_type == LOG_REPLICATION_DATA
	       || repl_rec->repl_type == LOG_REPLICATION_SCHEMA)
	      &&
	      ((is_commit
		&& repl_rec->must_flush != LOG_REPL_DONT_NEED_FLUSH)
	       || repl_rec->must_flush == LOG_REPL_NEED_FLUSH))
	    {
	      node = prior_lsa_alloc_and_copy_data (thread_p,
						    repl_rec->repl_type,
						    RV_NOT_DEFINED, NULL,
						    repl_rec->length,
						    repl_rec->repl_data,
						    0, NULL);
	      if (node == NULL)
		{
		  assert (false);
		  continue;
		}

	      log = (struct log_replication *) node->data_header;
	      if (repl_rec->rcvindex == RVREPL_DATA_DELETE
		  || repl_rec->rcvindex == RVREPL_SCHEMA)
		{
		  LSA_SET_NULL (&log->lsa);
		}
	      else
		{
		  LSA_COPY (&log->lsa, &repl_rec->lsa);
		}
	      log->length = repl_rec->length;
	      log->rcvindex = repl_rec->rcvindex;

	      if (with_lock == LOG_PRIOR_LSA_WITH_LOCK)
		{
		  (void) prior_lsa_next_record_with_lock (thread_p, node,
							  tdes);
		}
	      else
		{
		  (void) prior_lsa_next_record (thread_p, node, tdes);
		}

	      repl_rec->must_flush = LOG_REPL_DONT_NEED_FLUSH;
	    }

	  tdes->append_repl_recidx++;
	}
    }

}

static void
log_append_repl_info (THREAD_ENTRY * thread_p, LOG_TDES * tdes,
		      bool is_commit)
{
  log_append_repl_info_internal (thread_p, tdes, is_commit,
				 LOG_PRIOR_LSA_WITHOUT_LOCK);
}

static void
log_append_repl_info_with_lock (THREAD_ENTRY * thread_p, LOG_TDES * tdes,
				bool is_commit)
{
  log_append_repl_info_internal (thread_p, tdes, is_commit,
				 LOG_PRIOR_LSA_WITH_LOCK);
}

/*
 * log_append_unlock_log_internal - APPEND UNLOCK LOG
 *
 * return: nothing
 *
 *   tdes(in):  State structure of transaction being committed/aborted.
 *   with_lock(in):
 *
 * NOTE:critical section is set by its caller function.
 */
static void
log_append_unlock_log_internal (THREAD_ENTRY * thread_p, LOG_TDES * tdes,
				int with_lock)
{
  LOG_PRIOR_NODE *node;

  node = prior_lsa_alloc_and_copy_data (thread_p, LOG_UNLOCK_COMMIT,
					RV_NOT_DEFINED, NULL,
					0, NULL, 0, NULL);
  if (node == NULL)
    {
      return;
    }

  if (with_lock == LOG_PRIOR_LSA_WITH_LOCK)
    {
      (void) prior_lsa_next_record_with_lock (thread_p, node, tdes);
    }
  else
    {
      (void) prior_lsa_next_record (thread_p, node, tdes);
    }
}

static void
log_append_unlock_log (THREAD_ENTRY * thread_p, LOG_TDES * tdes)
{
  log_append_unlock_log_internal (thread_p, tdes, LOG_PRIOR_LSA_WITHOUT_LOCK);
}

static void
log_append_unlock_log_with_lock (THREAD_ENTRY * thread_p, LOG_TDES * tdes)
{
  log_append_unlock_log_internal (thread_p, tdes, LOG_PRIOR_LSA_WITH_LOCK);
}

static void
log_append_repl_info_and_unlock_log (THREAD_ENTRY * thread_p, LOG_TDES * tdes)
{
  int rv;

  rv = pthread_mutex_lock (&log_Gl.prior_info.prior_lsa_mutex);

  log_append_repl_info_with_lock (thread_p, tdes, true);
  log_append_unlock_log_with_lock (thread_p, tdes);

  pthread_mutex_unlock (&log_Gl.prior_info.prior_lsa_mutex);
}

/*
 * log_append_donetime - APPEND COMMIT/ABORT LOG RECORD ALONG WITH TIME OF
 *                      TERMINATION.
 *
 * return: nothing
 *
 *   tdes(in/out): State structure of transaction being committed/aborted.
 *   iscommitted(in): Is transaction been finished as committed ?
 *
 * NOTE: An append commit or abort record is recorded along with the
 *              current time as the termination time of the transaction.
 */
static void
log_append_donetime (THREAD_ENTRY * thread_p, LOG_TDES * tdes,
		     LOG_RECTYPE iscommitted)
{
  struct log_donetime *donetime;
  LOG_PRIOR_NODE *node;
  LOG_LSA start_lsa;

  node = prior_lsa_alloc_and_copy_data (thread_p, iscommitted,
					RV_NOT_DEFINED, NULL,
					0, NULL, 0, NULL);
  if (node == NULL)
    {
      return;
    }

  donetime = (struct log_donetime *) node->data_header;
  donetime->at_time = time (NULL);

  start_lsa = prior_lsa_next_record (thread_p, node, tdes);

  /* END append */
  if (iscommitted == LOG_COMMIT)
    {
      tdes->state = TRAN_UNACTIVE_COMMITTED;

      log_Stat.commit_count++;

      logpb_flush_pages (thread_p, &start_lsa);

#if !defined(NDEBUG)
      if (prm_get_bool_value (PRM_ID_LOG_TRACE_DEBUG))
	{
	  char time_val[CTIME_MAX];

	  time_t xxtime = time (NULL);
	  (void) ctime_r (&xxtime, time_val);
	  fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID,
					   MSGCAT_SET_LOG,
					   MSGCAT_LOG_FINISH_COMMIT),
		   tdes->tran_index, tdes->trid, log_Gl.hdr.append_lsa.pageid,
		   log_Gl.hdr.append_lsa.offset, time_val);
	  fflush (stdout);
	}
#endif /* !NDEBUG */
    }
  else
    {
      tdes->state = TRAN_UNACTIVE_ABORTED;
#if !defined(NDEBUG)
      if (prm_get_bool_value (PRM_ID_LOG_TRACE_DEBUG))
	{
	  char time_val[CTIME_MAX];

	  time_t xxtime = time (NULL);
	  (void) ctime_r (&xxtime, time_val);
	  fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID,
					   MSGCAT_SET_LOG,
					   MSGCAT_LOG_FINISH_ABORT),
		   tdes->tran_index, tdes->trid, log_Gl.hdr.append_lsa.pageid,
		   log_Gl.hdr.append_lsa.offset, time_val);
	  fflush (stdout);
	}
#endif /* !NDEBUG */
    }
}

/*
 * log_add_to_modified_class_list -
 *
 * return:
 *
 *   class_oid(in):
 *
 * NOTE: Function for LOG_TDES.modified_class_list
 *       This list keeps the following information:
 *        1) OID of modified class and LSA for the last modification
 *        2) whether updating statistics on a class was requested or not
 *
 *       The function "log_add_to_modified_class_list" will add
 *       the first information to the list, and the function
 *       "log_mark_modified_class_as_update_stats_required" will register
 *       the second information.
 */
int
log_add_to_modified_class_list (THREAD_ENTRY * thread_p,
				const OID * class_oid)
{
  LOG_TDES *tdes;
  MODIFIED_CLASS_ENTRY *t = NULL;
  int tran_index;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL)
    {
      return ER_FAILED;
    }

  for (t = tdes->modified_class_list; t; t = t->next)
    {
      if (OID_EQ (&t->class_oid, class_oid))
	{
	  break;
	}
    }

  if (t == NULL)
    {				/* class_oid is not in modified_class_list */
      t = (MODIFIED_CLASS_ENTRY *) malloc (sizeof (MODIFIED_CLASS_ENTRY));
      if (t == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, sizeof (MODIFIED_CLASS_ENTRY));
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}

      COPY_OID (&t->class_oid, class_oid);
      LSA_SET_NULL (&t->last_modified_lsa);
      t->next = tdes->modified_class_list;
      tdes->modified_class_list = t;
    }

  LSA_COPY (&t->last_modified_lsa, &tdes->tail_lsa);

  return NO_ERROR;
}

/*
 * log_is_class_being_modified () - check if a class is being modified by
 *				    the transaction which is executed by the
 *				    thread parameter
 * return : true if the class is being modified, false otherwise
 * thread_p (in)  : thread entry
 * class_oid (in) : class identifier
 */
static bool
log_is_class_being_modified_internal (THREAD_ENTRY * thread_p,
				      const OID * class_oid)
{
  LOG_TDES *tdes;
  int tran_index;
  MODIFIED_CLASS_ENTRY *t = NULL;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);

  if (tdes == NULL)
    {
      /* this is an error but this is not the place for handling it */
      return false;
    }

  for (t = tdes->modified_class_list; t; t = t->next)
    {
      if (OID_EQ (&t->class_oid, class_oid))
	{
	  return true;
	}
    }
  return false;
}

/*
 * log_is_class_being_modified () - check if a class is being modified by
 *				    the transaction which is executed by the
 *				    thread parameter
 * return : true if the class is being modified, false otherwise
 * thread_p (in)  : thread entry
 * class_oid (in) : class identifier
 */
bool
log_is_class_being_modified (THREAD_ENTRY * thread_p, const OID * class_oid)
{
  return log_is_class_being_modified_internal (thread_p, class_oid);
}

/*
 * log_cleanup_modified_class -
 *
 * return:
 *
 *   class(in):
 *   arg(in):
 *
 * NOTE: Function for LOG_TDES.modified_class_list
 *       This will be used to decache the class representations and XASLs
 *       when a transaction is finished.
 */
static void
log_cleanup_modified_class (THREAD_ENTRY * thread_p,
			    MODIFIED_CLASS_ENTRY * class, void *arg)
{
  bool decache_classrepr = (bool) * ((bool *) arg);

  if (decache_classrepr && !LSA_ISNULL (&class->last_modified_lsa))
    {
      (void) heap_classrepr_decache (thread_p, &class->class_oid);
    }
  /* decache this class from the partitions cache also */
  (void) partition_decache_class (thread_p, &class->class_oid);

  /* remove XASL cache entries which are relevant with this class */
  if (prm_get_integer_value (PRM_ID_XASL_MAX_PLAN_CACHE_ENTRIES) > 0
      && (qexec_remove_xasl_cache_ent_by_class (thread_p,
						&class->class_oid, 1) !=
	  NO_ERROR))
    {
      er_log_debug (ARG_FILE_LINE,
		    "log_cleanup_modified_class: "
		    "qexec_remove_xasl_cache_ent_by_class"
		    " failed for class { %d %d %d }\n",
		    class->class_oid.pageid, class->class_oid.slotid,
		    class->class_oid.volid);
    }
  /* remove filter predicatecache entries which are relevant with this class */
  if (prm_get_integer_value (PRM_ID_FILTER_PRED_MAX_CACHE_ENTRIES) > 0
      &&
      (qexec_remove_filter_pred_cache_ent_by_class
       (thread_p, &class->class_oid) != NO_ERROR))
    {
      er_log_debug (ARG_FILE_LINE,
		    "log_cleanup_modified_class: "
		    "xs_remove_filter_pred_cache_ent_by_class"
		    " failed for class { %d %d %d }\n",
		    class->class_oid.pageid, class->class_oid.slotid,
		    class->class_oid.volid);
    }
}

/*
 * log_map_modified_class_list -
 *
 * return:
 *
 *   tdes(in):
 *   release(in): release the memory or not
 *   map_func(in): optinal
 *   arg(in): optional
 *
 * NOTE: Function for LOG_TDES.modified_class_list
 *       This will be used to traverse a modified class list and to do
 *       something on each entry.
 */
static void
log_map_modified_class_list (THREAD_ENTRY * thread_p,
			     LOG_TDES * tdes, bool release,
			     void
			     (*map_func) (THREAD_ENTRY * thread_p,
					  MODIFIED_CLASS_ENTRY * class,
					  void *arg), void *arg)
{
  MODIFIED_CLASS_ENTRY *t;

  t = tdes->modified_class_list;
  while (t != NULL)
    {
      if (map_func)
	{
	  (void) (*map_func) (thread_p, t, arg);
	}

      if (release)
	{
	  tdes->modified_class_list = t->next;
	  free_and_init (t);
	  t = tdes->modified_class_list;
	}
      else
	{
	  t = t->next;
	}
    }
}

/*
 * log_cleanup_modified_class_list -
 *
 * return:
 *
 *   tdes(in):
 *   bool(in): release the memory or not
 *   decache_classrepr(in): decache the class representation or not
 *
 * NOTE: Function for LOG_TDES.modified_class_list
 */
static void
log_cleanup_modified_class_list (THREAD_ENTRY * thread_p, LOG_TDES * tdes,
				 bool release, bool decache_classrepr)
{
  (void) log_map_modified_class_list (thread_p, tdes, release,
				      log_cleanup_modified_class,
				      &decache_classrepr);
}

/*
 * log_rollback_classrepr_cache - Decache class repr to rollback
 *
 * return:
 *
 *   thread_p(in):
 *   tdes(in): transaction description
 *   upto_lsa(in): LSA to rollback
 *
 */
static void
log_rollback_classrepr_cache (THREAD_ENTRY * thread_p, LOG_TDES * tdes,
			      LOG_LSA * upto_lsa)
{
  MODIFIED_CLASS_ENTRY *t;

  for (t = tdes->modified_class_list; t; t = t->next)
    {
      if (LSA_GT (&t->last_modified_lsa, upto_lsa))
	{
	  (void) heap_classrepr_decache (thread_p, &t->class_oid);
	  LSA_SET_NULL (&t->last_modified_lsa);
	}
    }
}

/*
 * log_increase_num_transient_classnames -
 *
 * return:
 *
 *   tran_index(in):
 *
 * NOTE: Functions for LOG_TDES.modified_class_list
 */
void
log_increase_num_transient_classnames (int tran_index)
{
  LOG_TDES *tdes;

  tdes = LOG_FIND_TDES (tran_index);
  /* ignore ER_LOG_UNKNOWN_TRANINDEX : It may be detected somewhere else. */
  if (tdes != NULL)
    {
      tdes->num_transient_classnames += 1;
    }
}

/*
 * log_decrease_num_transient_classnames -
 *
 * return:
 *
 *   tran_index(in):
 *
 * NOTE:
 */
void
log_decrease_num_transient_classnames (int tran_index)
{
  LOG_TDES *tdes;

  tdes = LOG_FIND_TDES (tran_index);
  /* ignore ER_LOG_UNKNOWN_TRANINDEX : It may be detected somewhere else. */
  if (tdes != NULL)
    {
      tdes->num_transient_classnames -= 1;
    }
}

/*
 * log_get_num_transient_classnames -
 *
 * return:
 *
 *   tran_index(in):
 *
 * NOTE:
 */
int
log_get_num_transient_classnames (int tran_index)
{
  LOG_TDES *tdes;

  tdes = LOG_FIND_TDES (tran_index);
  /* ignore ER_LOG_UNKNOWN_TRANINDEX : It may be detected somewhere else. */
  if (tdes != NULL)
    {
      return tdes->num_transient_classnames;
    }
  else
    {
      return 1;			/* someone exists */
    }
}

/*
 * log_is_valid_locator -
 *
 * return: true if the locator is valid, false otherwise
 *
 *   locator(in):
 *
 * NOTE:
 */
static int
log_is_valid_locator (const char *locator)
{
  if (locator)
    {
      char *key, *meta;

      key = LOCATOR_KEY (locator);
      meta = LOCATOR_META (locator);
      if (key && meta && (key > meta))
	{
	  return true;
	}
    }
  return false;
}

/*
 * xlog_find_lob_locator -
 *
 * return: state of locator
 *
 *   thread_p(in):
 *   locator(in):
 *   real_loc_ptr(out):
 *
 * NOTE:
 */
LOB_LOCATOR_STATE
xlog_find_lob_locator (THREAD_ENTRY * thread_p, const char *locator,
		       char *real_locator)
{
  int tran_index;
  LOG_TDES *tdes;

  assert (log_is_valid_locator (locator));
  assert (real_locator != NULL);

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);

  if (tdes != NULL)
    {
      LOB_LOCATOR_ENTRY *entry, find;

      find.key = LOCATOR_KEY (locator);
      find.key_hash = (int) mht_5strhash (find.key, INT_MAX);
      /* Find entry from red-black tree (see base/rb_tree.h) */
      entry = RB_FIND (lob_rb_root, &tdes->lob_locator_root, &find);
      if (entry != NULL)
	{
	  memcpy (real_locator, entry->top->locator,
		  strlen (entry->top->locator) + 1);
	  return entry->top->state;
	}
    }

  memcpy (real_locator, locator, strlen (locator) + 1);
  return LOB_NOT_FOUND;
}

/*
 * xlog_add_lob_locator -
 *
 * return: error code
 *
 *   thread_p(in):
 *   locator(in):
 *   state(in):
 *
 * NOTE:
 */
int
xlog_add_lob_locator (THREAD_ENTRY * thread_p, const char *locator,
		      LOB_LOCATOR_STATE state)
{
  int tran_index;
  LOG_TDES *tdes;
  LOB_LOCATOR_ENTRY *entry;
  LOB_SAVEPOINT_ENTRY *savept;
  char *key;
  int key_len;

  assert (log_is_valid_locator (locator));

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
      return ER_LOG_UNKNOWN_TRANINDEX;
    }

  key = LOCATOR_KEY (locator);
  key_len = strlen (key);

  entry = malloc (sizeof (LOB_LOCATOR_ENTRY) + key_len);
  if (entry == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, sizeof (LOB_LOCATOR_ENTRY) + key_len);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  savept = malloc (sizeof (LOB_SAVEPOINT_ENTRY));
  if (savept == NULL)
    {
      free_and_init (entry);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, sizeof (LOB_SAVEPOINT_ENTRY));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  entry->top = savept;
  entry->key = &entry->key_data[0];
  memcpy (entry->key_data, key, key_len + 1);
  entry->key_hash = (int) mht_5strhash (entry->key, INT_MAX);

  savept->state = state;
  savept->savept_lsa = LSA_LT (&tdes->savept_lsa, &tdes->topop_lsa) ?
    tdes->topop_lsa : tdes->savept_lsa;
  savept->prev = NULL;
  strlcpy (savept->locator, locator, sizeof (ES_URI));

  /* Insert entry to the red-black tree (see base/rb_tree.h) */
  RB_INSERT (lob_rb_root, &tdes->lob_locator_root, entry);

  return NO_ERROR;
}

/*
 * xlog_change_state_of_locator
 *
 * return: error code
 *
 *   thread_p(in):
 *   locator(in):
 *   new_locator(in):
 *   state(in):
 *
 * NOTE:
 */
int
xlog_change_state_of_locator (THREAD_ENTRY * thread_p, const char *locator,
			      const char *new_locator,
			      LOB_LOCATOR_STATE state)
{
  int tran_index;
  LOG_TDES *tdes;
  LOB_LOCATOR_ENTRY *entry, find;

  assert (log_is_valid_locator (locator));

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
      return ER_LOG_UNKNOWN_TRANINDEX;
    }

  find.key = LOCATOR_KEY (locator);
  find.key_hash = (int) mht_5strhash (find.key, INT_MAX);
  entry = RB_FIND (lob_rb_root, &tdes->lob_locator_root, &find);

  if (entry != NULL)
    {
      LOG_LSA last_lsa;

      last_lsa = LSA_GE (&tdes->savept_lsa, &tdes->topop_lsa) ?
	tdes->savept_lsa : tdes->topop_lsa;

      /* if it is created prior to current savepoint,
         push the previous state in the savepoint list */
      if (LSA_LT (&entry->top->savept_lsa, &last_lsa))
	{
	  LOB_SAVEPOINT_ENTRY *savept;

	  savept = malloc (sizeof (LOB_SAVEPOINT_ENTRY));
	  if (savept == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_OUT_OF_VIRTUAL_MEMORY, 1,
		      sizeof (LOB_SAVEPOINT_ENTRY));
	      return ER_OUT_OF_VIRTUAL_MEMORY;
	    }

	  /* copy structure (avoid reduncant memory copy) */
	  savept->state = entry->top->state;
	  savept->savept_lsa = entry->top->savept_lsa;
	  memcpy (savept->locator, entry->top->locator,
		  strlen (entry->top->locator) + 1);
	  savept->prev = entry->top;
	  entry->top = savept;
	}

      /* set the current state */
      if (new_locator != NULL)
	{
	  strlcpy (entry->top->locator, new_locator, sizeof (ES_URI));
	}
      entry->top->state = state;
      entry->top->savept_lsa = last_lsa;
    }

  return NO_ERROR;
}

/*
 * xlog_drop_lob_locator -
 *
 * return: error code
 *
 *   thread_p(in):
 *   locator(in):
 *
 * NOTE:
 */
int
xlog_drop_lob_locator (THREAD_ENTRY * thread_p, const char *locator)
{
  int tran_index;
  LOG_TDES *tdes;
  LOB_LOCATOR_ENTRY *entry, find;

  assert (log_is_valid_locator (locator));

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
      return ER_LOG_UNKNOWN_TRANINDEX;
    }

  find.key = LOCATOR_KEY (locator);
  find.key_hash = (int) mht_5strhash (find.key, INT_MAX);
  /* Remove entry that matches 'find' entry from the red-black tree.
     see base/rb_tree.h for more information */
  entry = RB_FIND (lob_rb_root, &tdes->lob_locator_root, &find);
  if (entry != NULL)
    {
      entry = RB_REMOVE (lob_rb_root, &tdes->lob_locator_root, entry);
      if (entry != NULL)
	{
	  log_free_lob_locator (entry);
	}
    }

  return NO_ERROR;
}

/*
 * log_free_lob_locator -
 *
 * return: void
 *
 *   entry(in):
 *
 * NOTE:
 */
static void
log_free_lob_locator (LOB_LOCATOR_ENTRY * entry)
{
  while (entry->top != NULL)
    {
      LOB_SAVEPOINT_ENTRY *savept;

      savept = entry->top;
      entry->top = savept->prev;
      free_and_init (savept);
    }

  free_and_init (entry);
}

/*
 * log_clear_lob_locator_list -
 *
 * return: void
 *
 *   thread_p(in):
 *   tdes(in):
 *   at_commit(in):
 *   savept_lsa(in): savepoint lsa to rollback
 *
 * NOTE:
 */
void
log_clear_lob_locator_list (THREAD_ENTRY * thread_p, LOG_TDES * tdes,
			    bool at_commit, LOG_LSA * savept_lsa)
{
  LOB_LOCATOR_ENTRY *entry, *next;
  bool need_to_delete;

  if (tdes == NULL)
    {
      return;
    }
#if 0
  er_log_debug (ARG_FILE_LINE, "log_clear_lob_locator_list");
#endif

  for (entry = RB_MIN (lob_rb_root, &tdes->lob_locator_root); entry != NULL;
       entry = next)
    {
#if 0
      er_log_debug (ARG_FILE_LINE,
		    "   locator=%s, state=%s\n, savept_lsa=(%d,%d)",
		    entry->key,
		    (entry->top->state ==
		     LOB_TRANSIENT_CREATED) ? "LOB_TRANSIENT_CREATED"
		    : ((entry->top->state ==
			LOB_TRANSIENT_DELETED) ? "LOB_TRANSIENT_DELETED"
		       : ((entry->top->state ==
			   LOB_PERMANENT_CREATED) ?
			  "LOB_PERMANENT_CREATED" : (entry->top->state ==
						     LOB_PERMANENT_DELETED)
			  ? "LOB_PERMANENT_DELETED" : "LOB_UNKNOWN")),
		    entry->top->savept_lsa.pageid,
		    entry->top->savept_lsa.offset);
#endif
      /* setup next link before destroy */
      next = RB_NEXT (lob_rb_root, &tdes->lob_locator_root, entry);

      need_to_delete = false;

      if (at_commit)
	{
	  if (entry->top->state != LOB_PERMANENT_CREATED)
	    {
	      need_to_delete = true;
	    }
	}
      else			/* rollback */
	{
	  /* at partial rollback, pop the previous states in the savepoint
	     list util it meets the rollback savepoint */
	  if (savept_lsa != NULL)
	    {
	      LOB_SAVEPOINT_ENTRY *savept, *tmp;

	      assert (entry->top != NULL);
	      savept = entry->top->prev;

	      while (savept != NULL)
		{
		  if (LSA_LT (&entry->top->savept_lsa, savept_lsa))
		    {
		      break;
		    }

		  /* rollback file renaming */
		  if (strcmp (entry->top->locator, savept->locator) != 0)
		    {
		      char meta_name[DB_MAX_SCHEMA_LENGTH];

		      assert (log_is_valid_locator (savept->locator));
		      PUT_LOCATOR_META (meta_name, savept->locator);
		      /* ignore return value */
		      (void) es_rename_file (entry->top->locator, meta_name,
					     savept->locator);
		    }
		  tmp = entry->top;
		  entry->top = savept;
		  savept = savept->prev;
		  free_and_init (tmp);
		}
	    }

	  /* delete the locator to be created */
	  if ((savept_lsa == NULL ||
	       LSA_GE (&entry->top->savept_lsa, savept_lsa)) &&
	      entry->top->state != LOB_TRANSIENT_DELETED)
	    {
	      need_to_delete = true;
	    }
	}

      /* remove from the locator tree */
      if (need_to_delete)
	{
	  (void) es_delete_file (entry->top->locator);
	  RB_REMOVE (lob_rb_root, &tdes->lob_locator_root, entry);
	  log_free_lob_locator (entry);
	}
    }

  /* at the end of transaction, free the locator list */
  if (savept_lsa == NULL)
    {
      for (entry = RB_MIN (lob_rb_root, &tdes->lob_locator_root);
	   entry != NULL; entry = next)
	{
	  next = RB_NEXT (lob_rb_root, &tdes->lob_locator_root, entry);
	  RB_REMOVE (lob_rb_root, &tdes->lob_locator_root, entry);
	  log_free_lob_locator (entry);
	}
      assert (RB_EMPTY (&tdes->lob_locator_root));
    }
}

/*
 * lob_locator_cmp - compare function used lob rb tree
 *
 * return: < 0, 0, > 0
 * e1(in): entry 1
 * e2(in): entry 2
 *
 * NOTE:
 *
 * Ordering relation of lob locator entry is defined as (key_hash, key).
 * Because it is very UNLIKELY in normal case that two different locators
 * have same hash value, this compare function is very efficient.
 *
 */
static int
lob_locator_cmp (const LOB_LOCATOR_ENTRY * e1, const LOB_LOCATOR_ENTRY * e2)
{
  if (e1->key_hash != e2->key_hash)
    {
      return e1->key_hash - e2->key_hash;
    }
  else
    {
      return strcmp (e1->key, e2->key);
    }
}

/*
 * Below macro generates lob rb tree functions (see base/rb_tree.h)
 * Note: semi-colon is intentionally added to be more beautiful
 */
RB_GENERATE_STATIC (lob_rb_root, lob_locator_entry, head, lob_locator_cmp);

/*
 * log_commit_local - Perform the local commit operations of a transaction
 *
 * return: state of commit operation
 *
 *   tdes(in/out): State structure of transaction of the log record
 *   retain_lock(in): false = release locks (default)
 *                    true  = retain locks
 *
 * NOTE:  Commit the current transaction locally. The transaction may be
 *              committed in steps if there are either or both postpone
 *              actions to do on the database (in the server) and client
 *              loose_end postpone actions to do at the client machine (e.g.,
 *              multimedia recovery). If there are postpone actions, the
 *              transaction is declared committed_with_postpone_actions by
 *              logging a log record indicating this state. Then, the postpone
 *              actions are executed. If there are client loose_ends postpone
 *              actions the transaction is committed_with_client_loose_ends.
 *              This condition is returned to the client through the state of
 *              the transaction. In this case the client transaction manager
 *              must obtain and execute these actions. When the transaction is
 *              declared as fully committed, the locks acquired by the
 *              transaction are released and query cursors are closed. A
 *              committed transaction is not subject to deadlock when postpone
 *              operations are executed.
 *              The function returns the state of the transaction (i.e.,
 *              notify if the transaction is completely commited or not).
 */
TRAN_STATE
log_commit_local (THREAD_ENTRY * thread_p, LOG_TDES * tdes, bool retain_lock)
{
  qmgr_clear_trans_wakeup (thread_p, tdes->tran_index, false, false);

  tdes->state = TRAN_UNACTIVE_WILL_COMMIT;
  if (tdes->num_new_tmp_files + tdes->num_new_tmp_tmp_files > 0)
    {
      (void) file_new_destroy_all_tmp (thread_p, FILE_EITHER_TMP);
    }
  assert (tdes->num_new_tmp_files == 0 && tdes->num_new_tmp_tmp_files == 0);

  if (!LSA_ISNULL (&tdes->tail_lsa))
    {
      /*
       * Transaction updated data.
       */

      /* clear mvccid before releasing the locks */
      if (mvcc_Enabled == true)
	{
	  /* This operation must be done before do_postpone because it stores
	   * unique statistics for all B-trees and if an error occurs those
	   * operations and all operations of current transaction must be rolled
	   * back. 
	   */
	  logtb_complete_mvcc (thread_p, tdes, true);
	}

      log_do_postpone (thread_p, tdes, &tdes->posp_nxlsa,
		       LOG_COMMIT_WITH_POSTPONE);

      /*
       * The files created by this transaction are not new files any longer.
       * Close any query cursors at this moment too.
       * Release all locks
       */
      if (tdes->first_save_entry != NULL)
	{
	  spage_free_saved_spaces (thread_p, tdes->first_save_entry);
	  tdes->first_save_entry = NULL;
	}

      if (tdes->num_new_files > 0)
	{
	  (void) file_new_declare_as_old (thread_p, NULL);
	}
      assert (tdes->num_new_files == 0);

      log_cleanup_modified_class_list (thread_p, tdes, true, false);

      if (!LOG_CHECK_LOG_APPLIER (thread_p)
	  && log_does_allow_replication () == true)
	{
	  /* for the replication agent guarantee the order of transaction */
	  log_append_repl_info_and_unlock_log (thread_p, tdes);
	}

      if (retain_lock != true)
	{
	  lock_unlock_all (thread_p);
	}
      /* for page latch
         pb_threshold_flush(0);
       */

      /*
       * If the transaction has some commit postpone operations to be done at
       * the client machine, the trasaction is declared as committed with client
       * loose_ends postpone operations
       */

      if (!LSA_ISNULL (&tdes->client_posp_lsa))
	{
	  log_append_commit_client_loose_ends (thread_p, tdes);
	  /*
	   * Now the client transaction manager should request the postpone
	   * actions until all of them become exhausted.
	   */
	}
    }
  else
    {
      /*
       * Transaction did not update anything or we are not logging
       */

      /*
       * We are not logging, and changes were done
       */
      if (tdes->first_save_entry != NULL)
	{
	  spage_free_saved_spaces (thread_p, tdes->first_save_entry);
	  tdes->first_save_entry = NULL;
	}

      if (tdes->num_new_files > 0)
	{
	  (void) file_new_declare_as_old (thread_p, NULL);
	}
      assert (tdes->num_new_files == 0);

      /* clear mvccid before releasing the locks */
      if (mvcc_Enabled == true)
	{
	  logtb_complete_mvcc (thread_p, tdes, true);
	}

      if (retain_lock != true)
	{
	  lock_unlock_all (thread_p);
	}
      tdes->state = TRAN_UNACTIVE_COMMITTED;
      /* There is no need to create a new transaction identifier */
    }

  log_clear_lob_locator_list (thread_p, tdes, true, NULL);

#if defined(ENABLE_UNUSED_FUNCTION)
  /*
   * Since the /M driver transaction group stuff is currently
   * being maintained outside the transaction manager, we cannot
   * check for leaked wfg entries of distributed transactions here.
   */
  if ((wfg_get_tran_entries (tdes->tran_index) > 0)
      && !log_is_tran_distributed (tdes))
    {
      wfg_dump ();
    }
#endif

  return (tdes->state);

}

/*
 * log_abort_local - PERFORM THE LOCAL ABORT OPERATIONS OF A TRANSACTION
 *
 * return: state of abort operation
 *
 *   tdes(in/out): State structure of transaction of the log record
 *
 * NOTE: Abort the current transaction locally. The transaction may be
 *              aborted in steps if there are client loose_end actions. In
 *              this the transaction is declared aborted with client loose
 *              ends. This condition is returned to the client through the
 *              state of the transaction. In this case the client transaction
 *              manager must obtain and execute these actions. When the
 *              transaction is declared as fully aborted, the locks acquired
 *              by the transaction are released and query cursors are closed.
 *      This function is used for both local and coordinator transactions.
 */
TRAN_STATE
log_abort_local (THREAD_ENTRY * thread_p, LOG_TDES * tdes)
{
  qmgr_clear_trans_wakeup (thread_p, tdes->tran_index, false, true);

  tdes->state = TRAN_UNACTIVE_ABORTED;

  /*
   * Delete only temporary files created on volumes with temporary purposes
   * Temporary files created on volumes with permananet purposes
   * (e.g., generic) are cleaned by undo records.
   */
  if (tdes->num_new_tmp_tmp_files > 0)
    {
      (void) file_new_destroy_all_tmp (thread_p, FILE_TMP_TMP);
    }
  assert (tdes->num_new_tmp_tmp_files == 0);

  (void) file_typecache_clear ();
  if (!LSA_ISNULL (&tdes->tail_lsa))
    {
      /* Transaction updated data */
      log_rollback (thread_p, tdes, NULL);

      if (tdes->num_new_files > 0)
	{
	  (void) file_new_declare_as_old (thread_p, NULL);
	}
      assert (tdes->num_new_files == 0);

      log_cleanup_modified_class_list (thread_p, tdes, true, true);

      if (tdes->first_save_entry != NULL)
	{
	  spage_free_saved_spaces (thread_p, tdes->first_save_entry);
	  tdes->first_save_entry = NULL;
	}

      /* clear mvccid before releasing the locks */
      if (mvcc_Enabled == true)
	{
	  logtb_complete_mvcc (thread_p, tdes, false);
	}

      lock_unlock_all (thread_p);

      /* Are there any loose ends to be done in the client ? */
      if (!LSA_ISNULL (&tdes->client_undo_lsa))
	{
	  log_append_abort_client_loose_ends (thread_p, tdes);
	  /*
	   * Now the client transaction manager should request all client undo
	   * actions.
	   */
	}
    }
  else
    {
      /*
       * Transaction did not update anything or we are not logging
       */
      if (tdes->num_new_files > 0)
	{
	  (void) file_new_declare_as_old (thread_p, NULL);
	}
      assert (tdes->num_new_files == 0);

      if (tdes->first_save_entry != NULL)
	{
	  spage_free_saved_spaces (thread_p, tdes->first_save_entry);
	  tdes->first_save_entry = NULL;
	}

      /* clear mvccid before releasing the locks */
      if (mvcc_Enabled == true)
	{
	  logtb_complete_mvcc (thread_p, tdes, false);
	}

      lock_unlock_all (thread_p);
      /* There is no need to create a new transaction identifier */
    }

  log_clear_lob_locator_list (thread_p, tdes, false, NULL);

#if defined(ENABLE_UNUSED_FUNCTION)
  /*
   * Since the /M driver transaction group stuff is currently
   * being maintained outside the transaction manager, we cannot
   * check for leaked wfg entries of distributed transactions here.
   */
  if ((wfg_get_tran_entries (tdes->tran_index) > 0)
      && !log_is_tran_distributed (tdes))
    {
      wfg_dump ();
    }
#endif

  return tdes->state;

}

/*
 * log_commit - COMMIT A TRANSACTION
 *
 * return:  state of commit operation
 *
 *   tran_index(in): tran_index
 *   retain_lock(in): false = release locks (default)
 *                      true  = retain locks
 *
 * NOTE: Commit the current transaction.  The function returns the
 *              state of the transaction (i.e., notify if the transaction
 *              is completely commited or not). If the transaction was
 *              coordinating a global transaction then the Two Phase Commit
 *              protocol is followed by this function. Otherwise, only the
 *              local commit actions are performed.
 */
TRAN_STATE
log_commit (THREAD_ENTRY * thread_p, int tran_index, bool retain_lock)
{
  TRAN_STATE state;		/* State of committed transaction */
  LOG_TDES *tdes;		/* Transaction descriptor         */
  bool decision;
  LOG_2PC_EXECUTE execute_2pc_type;
  int error_code = NO_ERROR;

  if (tran_index == NULL_TRAN_INDEX)
    {
      tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
    }

  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
      error_code = ER_LOG_UNKNOWN_TRANINDEX;
      return TRAN_UNACTIVE_UNKNOWN;
    }

  if (!LOG_ISTRAN_ACTIVE (tdes) && !LOG_ISTRAN_2PC_PREPARE (tdes)
      && LOG_ISRESTARTED ())
    {
      /*
       * May be a system error since transaction is not active.. cannot be
       * committed
       */
#if defined(CUBRID_DEBUG)
      er_log_debug (ARG_FILE_LINE,
		    "log_commit: Transaction %d (index = %d) is"
		    " not active and cannot be committed. Its state is %s\n",
		    tdes->trid, tdes->tran_index,
		    log_state_string (tdes->state));
#endif /* CUBRID_DEBUG */
      return tdes->state;
    }

  if (tdes->topops.last >= 0)
    {
      /*
       * This is likely a system error since the transaction is being committed
       * when there are system permanent operations attached to it. Commit those
       * operations too
       */
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
	      ER_LOG_HAS_TOPOPS_DURING_COMMIT_ABORT, 2,
	      tdes->trid, tdes->tran_index);
      while (tdes->topops.last >= 0)
	{
	  (void) log_end_system_op (thread_p,
				    LOG_RESULT_TOPOP_ATTACH_TO_OUTER);
	}
    }

  if (tdes->tran_unique_stats != NULL)
    {
#if defined(CUBRID_DEBUG)
      er_log_debug (ARG_FILE_LINE,
		    "log_commit: Warning, unique statistical information "
		    "kept in transaction entry is not freed.");
#endif /* CUBRID_DEBUG */
      free_and_init (tdes->tran_unique_stats);
      tdes->num_unique_btrees = 0;
      tdes->max_unique_btrees = 0;
    }

  if (log_clear_and_is_tran_distributed (tdes))
    {
      /*
       * This is the coordinator of a distributed transaction
       *
       * If we are in prepare to commit mode. I cannot be the root coodinator,
       * so the decsion has been taken at this moment by the root coordinator
       */
      if (LOG_ISTRAN_2PC_PREPARE (tdes))
	{
	  execute_2pc_type = LOG_2PC_EXECUTE_COMMIT_DECISION;
	}
      else
	{
	  execute_2pc_type = LOG_2PC_EXECUTE_FULL;
	}

      state = log_2pc_commit (thread_p, tdes, execute_2pc_type, &decision);
    }
  else
    {
      /*
       * This is a local transaction or is a participant of a distributed
       * transaction
       */
      state = log_commit_local (thread_p, tdes, retain_lock);
      if (state != TRAN_UNACTIVE_COMMITTED_WITH_CLIENT_USER_LOOSE_ENDS)
	{
	  state = log_complete (thread_p, tdes, LOG_COMMIT, LOG_NEED_NEWTRID);
	}
    }

#if defined (CUBRID_DEBUG)
  if (logtb_get_number_assigned_tran_indices () <= 2)
    {
      pgbuf_dump_if_any_fixed ();
      (void) heap_classrepr_dump_anyfixed ();
    }
#endif /* CUBRID_DEBUG */

  if (log_No_logging)
    {
      LOG_CS_ENTER (thread_p);
      /* We are not logging */
      logpb_flush_pages_direct (thread_p);
      (void) pgbuf_flush_all_unfixed (thread_p, NULL_VOLID);
      if (LOG_HAS_LOGGING_BEEN_IGNORED ())
	{
	  /*
	   * Indicate that logging has not been ignored for next transaction
	   */
	  log_Gl.hdr.has_logging_been_skipped = false;
	  logpb_flush_header (thread_p);
	}
      LOG_CS_EXIT (thread_p);
    }

  mnt_tran_commits (thread_p);

  return state;
}

/*
 * log_abort - ABORT A TRANSACTION
 *
 * return: TRAN_STATE
 *
 *   tran_index(in): tran_index
 *
 * NOTE: Abort the current transaction. If the transaction is the
 *              coordinator of a global transaction, the participants are also
 *              informed about the abort, and if necessary their
 *              acknowledgements are collected before finishing the
 *              transaction.
 */
TRAN_STATE
log_abort (THREAD_ENTRY * thread_p, int tran_index)
{
  TRAN_STATE state;		/* State of aborted transaction */
  LOG_TDES *tdes;		/* Transaction descriptor       */
  bool decision;
  int error_code = NO_ERROR;

  if (tran_index == NULL_TRAN_INDEX)
    {
      tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
    }

  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
      error_code = ER_LOG_UNKNOWN_TRANINDEX;
      return TRAN_UNACTIVE_UNKNOWN;
    }

  if (LOG_HAS_LOGGING_BEEN_IGNORED ())
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_LOG_CORRUPTED_DB_DUE_NOLOGGING, 0);
      error_code = ER_LOG_CORRUPTED_DB_DUE_NOLOGGING;
      return tdes->state;
    }

  if (!LOG_ISTRAN_ACTIVE (tdes) && !LOG_ISTRAN_2PC_PREPARE (tdes))
    {
      /*
       * May be a system error: Transaction is not in an active state nor
       * prepare to commit state
       */
      return tdes->state;
    }

  if (tdes->topops.last >= 0)
    {
      /*
       * This is likely a system error since the transaction is being aborted
       * when there are system permananet operations attached to it. Abort those
       * operations too.
       */
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
	      ER_LOG_HAS_TOPOPS_DURING_COMMIT_ABORT, 2,
	      tdes->trid, tdes->tran_index);
      while (tdes->topops.last >= 0)
	{
	  (void) log_end_system_op (thread_p,
				    LOG_RESULT_TOPOP_ATTACH_TO_OUTER);
	}
    }

  if (tdes->tran_unique_stats != NULL)
    {
#if defined(CUBRID_DEBUG)
      er_log_debug (ARG_FILE_LINE,
		    "log_abort: Warning, unique statistical information "
		    "kept in transaction entry is not freed.");
#endif /* CUBRID_DEBUG */
      free_and_init (tdes->tran_unique_stats);
      tdes->num_unique_btrees = 0;
      tdes->max_unique_btrees = 0;
    }

  /*
   * If we are in prepare to commit mode. I cannot be the root coodinator,
   * so the decision has already been taken at this moment by the root
   * coordinator. If a distributed transaction is not in 2PC, the decision
   * has been taken without using the 2PC.
   */

  if (log_clear_and_is_tran_distributed (tdes))
    {
      /* This is the coordinator of a distributed transaction */
      state =
	log_2pc_commit (thread_p, tdes, LOG_2PC_EXECUTE_ABORT_DECISION,
			&decision);
    }
  else
    {
      /*
       * This is a local transaction or is a participant of a distributed
       * transaction.
       * Perform the server rollback first.
       */
      state = log_abort_local (thread_p, tdes);
      /*
       * If there are client loose ends then the following operations will
       * be done after the client loose ends are finished; So, skip them here.
       */
      if (tdes->state != TRAN_UNACTIVE_ABORTED_WITH_CLIENT_USER_LOOSE_ENDS)
	{
	  state = log_complete (thread_p, tdes, LOG_ABORT, LOG_NEED_NEWTRID);
	}
    }

#if defined (CUBRID_DEBUG)
  if (logtb_get_number_assigned_tran_indices () <= 2)
    {
      pgbuf_dump_if_any_fixed ();
    }
#endif /* CUBRID_DEBUG */

  mnt_tran_rollbacks (thread_p);

  return state;
}

/*
 * log_abort_partial - ABORT ACTIONS OF A TRANSACTION TO A SAVEPOINT
 *
 * return: state of partial aborted operation (i.e., notify if
 *              there are client actions that need to be undone).
 *
 *   savepoint_name(in):  Name of the savepoint
 *   savept_lsa(in):
 *
 * NOTE: All the effects done by the current transaction after the
 *              given savepoint are undone and all effects of the transaction
 *              preceding the given savepoint remain. After the partial abort
 *              the transaction can continue its normal execution as if
 *              the statements that were undone were never executed.
 */
TRAN_STATE
log_abort_partial (THREAD_ENTRY * thread_p, const char *savepoint_name,
		   LOG_LSA * savept_lsa)
{
  LOG_TDES *tdes;		/* Transaction descriptor */
  TRAN_STATE state;
  int tran_index;

  /* Find current transaction descriptor */
  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
      return TRAN_UNACTIVE_UNKNOWN;
    }

  if (LOG_HAS_LOGGING_BEEN_IGNORED ())
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_LOG_CORRUPTED_DB_DUE_NOLOGGING, 0);
      return tdes->state;
    }

  if (!LOG_ISTRAN_ACTIVE (tdes))
    {
      /*
       * May be a system error: Transaction is not in an active state
       */
      return tdes->state;
    }

  if (savepoint_name == NULL
      || log_get_savepoint_lsa (thread_p, savepoint_name, tdes, savept_lsa) ==
      NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_UNKNOWN_SAVEPOINT, 1,
	      savepoint_name);
      return TRAN_UNACTIVE_UNKNOWN;
    }

  if (tdes->topops.last >= 0)
    {
      /*
       * This is likely a system error since the transaction is being partially
       * aborted when there are nested top system permananet operations
       * attached to it. Abort those operations too.
       */
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
	      ER_LOG_HAS_TOPOPS_DURING_COMMIT_ABORT, 2,
	      tdes->trid, tdes->tran_index);
      while (tdes->topops.last >= 0)
	{
	  (void) log_end_system_op (thread_p,
				    LOG_RESULT_TOPOP_ATTACH_TO_OUTER);
	}
    }

  if (log_start_system_op (thread_p) == NULL)
    {
      return TRAN_UNACTIVE_UNKNOWN;
    }

  LSA_COPY (&tdes->topops.stack[tdes->topops.last].lastparent_lsa,
	    savept_lsa);

  if (!LSA_ISNULL (&tdes->client_undo_lsa))
    {
      if (LSA_LT (savept_lsa, &tdes->client_undo_lsa))
	{
	  LSA_COPY (&tdes->topops.stack[tdes->topops.last].client_undo_lsa,
		    &tdes->client_undo_lsa);
	}
      else
	{
	  LSA_COPY (&tdes->topops.stack[tdes->topops.last].client_undo_lsa,
		    savept_lsa);
	}
    }

  if (!LSA_ISNULL (&tdes->client_posp_lsa))
    {
      if (LSA_LT (savept_lsa, &tdes->client_posp_lsa))
	{
	  LSA_COPY (&tdes->topops.stack[tdes->topops.last].client_posp_lsa,
		    &tdes->client_posp_lsa);
	  LSA_SET_NULL (&tdes->client_posp_lsa);
	}
      else
	{
	  LSA_COPY (&tdes->topops.stack[tdes->topops.last].client_posp_lsa,
		    savept_lsa);
	}
    }

  if (!LSA_ISNULL (&tdes->posp_nxlsa))
    {
      if (LSA_LT (savept_lsa, &tdes->posp_nxlsa))
	{
	  LSA_COPY (&tdes->topops.stack[tdes->topops.last].posp_lsa,
		    &tdes->posp_nxlsa);
	  LSA_SET_NULL (&tdes->posp_nxlsa);
	}
      else
	{
	  LSA_COPY (&tdes->topops.stack[tdes->topops.last].posp_lsa,
		    savept_lsa);
	}
    }

  state = log_end_system_op (thread_p, LOG_RESULT_TOPOP_ABORT);

  log_cleanup_modified_class_list (thread_p, tdes, false, true);

  log_clear_lob_locator_list (thread_p, tdes, false, savept_lsa);

  /*
   * The following is done so that if we go over several savepoints, they
   * get undefined and cannot get call by the user any longer.
   */
  LSA_COPY (&tdes->savept_lsa, savept_lsa);
  return state;
}

/*
 * log_complete - Complete in commit/abort mode the transaction whenever
 *                      is possible otherwise trasfer it to another tran index
 *
 * return: state of transaction
 *
 *   tdes(in/out): State structure of transaction of the log record
 *   iscommitted(in): Is transaction been finished as committed ?
 *   get_newtrid(in):
 *
 */
TRAN_STATE
log_complete (THREAD_ENTRY * thread_p, LOG_TDES * tdes,
	      LOG_RECTYPE iscommitted, LOG_GETNEWTRID get_newtrid)
{
  TRAN_STATE state;		/* State of transaction */
  int new_tran_index;
  LOG_TDES *new_tdes;		/* New transaction descriptor when the
				 * transaction is transfered since the 2PC
				 * cannot be fully completed at this moment
				 */
  int return_2pc_loose_tranindex;	/* Wheater or not to return the
					 * current index
					 */
  bool all_acks = true;
  int i;
  LOG_PRIOR_NODE *node;
  LOG_LSA start_lsa;
  int wait_msecs;

  state = tdes->state;

  if (tdes->coord != NULL && tdes->coord->ack_received != NULL)
    {
      /*
       * Make sure that all acknowledgments from participants have been received
       * before declaring the transaction as finished.
       */
      for (i = 0; i < tdes->coord->num_particps; i++)
	if (tdes->coord->ack_received[i] == false)
	  {
	    all_acks = false;
	    /*
	     * There are missing acknowledgements. The transaction cannot be
	     * completed at this moment. If we are not in the restart recovery
	     * process, the transaction is transfered to another transaction
	     * index which is declared as a distributed loose end and a new
	     * transaction is assigned to the client transaction index. The
	     * transaction will be declared as fully completed once all
	     * acknowledgment are received.
	     */
	    if (iscommitted != LOG_ABORT)
	      {
		/* Committed */
		if (tdes->state !=
		    TRAN_UNACTIVE_COMMITTED_INFORMING_PARTICIPANTS)
		  {
		    node = prior_lsa_alloc_and_copy_data (thread_p,
							  LOG_2PC_COMMIT_INFORM_PARTICPS,
							  RV_NOT_DEFINED,
							  NULL,
							  0, NULL, 0, NULL);
		    if (node == NULL)
		      {
			assert (false);
			return state;
		      }

		    tdes->state =
		      TRAN_UNACTIVE_COMMITTED_INFORMING_PARTICIPANTS;
		    state = tdes->state;

		    start_lsa = prior_lsa_next_record (thread_p, node, tdes);

		    logpb_flush_pages (thread_p, &start_lsa);
		  }
	      }
	    else
	      {
		/* aborted */
		if (tdes->state !=
		    TRAN_UNACTIVE_ABORTED_INFORMING_PARTICIPANTS)
		  {
		    node = prior_lsa_alloc_and_copy_data (thread_p,
							  LOG_2PC_ABORT_INFORM_PARTICPS,
							  RV_NOT_DEFINED,
							  NULL,
							  0, NULL, 0, NULL);
		    if (node == NULL)
		      {
			assert (false);
			return state;
		      }

		    tdes->state =
		      TRAN_UNACTIVE_ABORTED_INFORMING_PARTICIPANTS;
		    state = tdes->state;

		    start_lsa = prior_lsa_next_record (thread_p, node, tdes);

		    logpb_flush_pages (thread_p, &start_lsa);
		  }
	      }
	    /*
	     * If this is not a loose end transaction and the system is not
	     * in restart recovery, transfer the transaction to another
	     * transaction index
	     */
	    if (LOG_ISRESTARTED () && tdes->isloose_end == false)
	      {
		wait_msecs = prm_get_integer_value (PRM_ID_LK_TIMEOUT_SECS);

		if (wait_msecs > 0)
		  {
		    wait_msecs = wait_msecs * 1000;
		  }
		new_tran_index = logtb_assign_tran_index (thread_p,
							  NULL_TRANID,
							  TRAN_RECOVERY, NULL,
							  NULL, wait_msecs,
							  TRAN_SERIALIZABLE);
		new_tdes = LOG_FIND_TDES (new_tran_index);
		if (new_tran_index == NULL_TRAN_INDEX || new_tdes == NULL)
		  {
		    logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
				       "log_fully_completed");
		    return state;
		  }

		/*
		 * Copy of tdes structures, and then reset memory allocated fields
		 * for only one the new or the old one.
		 */

		*new_tdes = *tdes;
		new_tdes->tran_index = new_tran_index;
		new_tdes->isloose_end = true;
		/* new_tdes does not inherit topops fileds */
		new_tdes->topops.stack = NULL;
		new_tdes->topops.last = -1;
		new_tdes->topops.max = 0;

		/* The old one keep the corrdinator/participant information */
		tdes->coord = NULL;

		TR_TABLE_CS_ENTER (thread_p);
		log_Gl.trantable.num_coord_loose_end_indices++;
		TR_TABLE_CS_EXIT (thread_p);
		/*
		 * Start a new transaction for our original transaction index.
		 * Set the coordinator stuff to NULL, in our original index since
		 * it has been transfer to another index. That is, distributed
		 * information should be freed using the new transaction index.
		 */

		/*
		 * Go back to the old index
		 */

		LOG_SET_CURRENT_TRAN_INDEX (thread_p, tdes->tran_index);

		(void) logtb_get_new_tran_id (thread_p, tdes);
	      }

	    if (LOG_ISCHECKPOINT_TIME ())
	      {
#if defined(SERVER_MODE)
		logpb_do_checkpoint ();
#else /* SERVER_MODE */
		(void) logpb_checkpoint (thread_p);
#endif /* SERVER_MODE */
	      }

	    return state;
	  }

      /*
       * All acknowledgements of participants have been received, declare the
       * the transaction as completed
       */
    }

  /*
   * DECLARE THE TRANSACTION AS COMPLETED
   */

  /*
   * Check if this index needs to be returned after finishing the transaction
   */

  if (tdes->isloose_end == true && all_acks == true)
    {
      return_2pc_loose_tranindex = true;
    }
  else
    {
      return_2pc_loose_tranindex = false;
    }

  if (LSA_ISNULL (&tdes->tail_lsa))
    {
      /*
       * Transaction did not update any data, thus we do not need to log a
       * commit/abort log record
       */
      if (iscommitted != LOG_ABORT)
	{
	  state = TRAN_UNACTIVE_COMMITTED;
	}
      else
	{
	  state = TRAN_UNACTIVE_ABORTED;
	}
#if !defined(NDEBUG)
      if (prm_get_bool_value (PRM_ID_LOG_TRACE_DEBUG))
	{
	  char time_val[CTIME_MAX];
	  time_t xxtime = time (NULL);

	  (void) ctime_r (&xxtime, time_val);
	  fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID,
					   MSGCAT_SET_LOG,
					   ((iscommitted != LOG_ABORT)
					    ? MSGCAT_LOG_FINISH_COMMIT
					    : MSGCAT_LOG_FINISH_ABORT)),
		   tdes->tran_index, tdes->trid,
		   log_Gl.hdr.append_lsa.pageid, log_Gl.hdr.append_lsa.offset,
		   time_val);
	  fflush (stdout);
	}
#endif /* !NDEBUG */
      logtb_clear_tdes (thread_p, tdes);
    }
  else
    {
      /*
       * Transaction updated data or this is a coordinator
       */
      log_append_donetime (thread_p, tdes, iscommitted);
      state = tdes->state;

      /* If recovery restart operation, or, if this is a coordinator loose end
       * transaction return this index and decrement coordinator loose end
       * transactions counter.
       */
      if (return_2pc_loose_tranindex == false)
	{
	  if (get_newtrid == LOG_NEED_NEWTRID)
	    {
	      (void) logtb_get_new_tran_id (thread_p, tdes);
	    }
	}
      else
	{
	  /* Free the index */
	  if (tdes->isloose_end == true)
	    {
	      TR_TABLE_CS_ENTER (thread_p);
	      log_Gl.trantable.num_coord_loose_end_indices--;
	      TR_TABLE_CS_EXIT (thread_p);
	    }
	  logtb_free_tran_index (thread_p, tdes->tran_index);
	}

      /* Finish the append operation and flush the log */
    }

  if (LOG_ISCHECKPOINT_TIME ())
    {
#if defined(SERVER_MODE)
      logpb_do_checkpoint ();
#else /* SERVER_MODE */
      (void) logpb_checkpoint (thread_p);
#endif /* SERVER_MODE */
    }

  return state;
}

static TRAN_STATE
log_complete_topop (THREAD_ENTRY * thread_p, LOG_TDES * tdes,
		    LOG_RESULT_TOPOP result)
{
  TRAN_STATE state;
  struct log_topop_result *top_result;	/* Partial outcome               */
  LOG_RECTYPE rectype;
  LOG_PRIOR_NODE *node;

  assert (tdes != NULL);

  if (result == LOG_RESULT_TOPOP_COMMIT)
    {
      rectype = LOG_COMMIT_TOPOPE;
      state = TRAN_UNACTIVE_COMMITTED;
    }
  else
    {
      rectype = LOG_ABORT_TOPOPE;
      state = TRAN_UNACTIVE_ABORTED;
    }
  node = prior_lsa_alloc_and_copy_data (thread_p, rectype,
					RV_NOT_DEFINED, NULL,
					0, NULL, 0, NULL);
  if (node == NULL)
    {
      return state;
    }

  top_result = (struct log_topop_result *) node->data_header;

  LSA_COPY (&top_result->lastparent_lsa,
	    &tdes->topops.stack[tdes->topops.last].lastparent_lsa);
  LSA_COPY (&top_result->prv_topresult_lsa, &tdes->tail_topresult_lsa);

  (void) prior_lsa_next_record (thread_p, node, tdes);

  /* Remember last partial result */
  LSA_COPY (&tdes->tail_topresult_lsa, &tdes->tail_lsa);

  return state;
}

static void
log_complete_topop_attach (LOG_TDES * tdes)
{
  if (tdes->topops.last - 1 >= 0)
    {
      if (LSA_ISNULL (&tdes->topops.stack[tdes->topops.last - 1].posp_lsa))
	{
	  LSA_COPY (&tdes->topops.stack[tdes->topops.last - 1].posp_lsa,
		    &tdes->topops.stack[tdes->topops.last].posp_lsa);
	}
      if (LSA_ISNULL
	  (&tdes->topops.stack[tdes->topops.last - 1].client_posp_lsa))
	{
	  LSA_COPY (&tdes->topops.
		    stack[tdes->topops.last - 1].client_posp_lsa,
		    &tdes->topops.stack[tdes->topops.last].client_posp_lsa);
	}
      if (LSA_ISNULL
	  (&tdes->topops.stack[tdes->topops.last - 1].client_undo_lsa))
	{
	  LSA_COPY (&tdes->topops.
		    stack[tdes->topops.last - 1].client_undo_lsa,
		    &tdes->topops.stack[tdes->topops.last].client_undo_lsa);
	}
    }
  else
    {
      if (LSA_ISNULL (&tdes->posp_nxlsa))
	{
	  LSA_COPY (&tdes->posp_nxlsa,
		    &tdes->topops.stack[tdes->topops.last].posp_lsa);
	}
      if (LSA_ISNULL (&tdes->client_posp_lsa))
	{
	  LSA_COPY (&tdes->client_posp_lsa,
		    &tdes->topops.stack[tdes->topops.last].client_posp_lsa);
	}
      if (LSA_ISNULL (&tdes->client_undo_lsa))
	{
	  LSA_COPY (&tdes->client_undo_lsa,
		    &tdes->topops.stack[tdes->topops.last].client_undo_lsa);
	}
    }
}

/*
 * log_complete_system_op - Complete a system top operation
 *
 * return: state of transaction
 *
 *   tdes(in/out): State structure of transaction of the log record
 *   result(in): Result of the top system operation
 *   back_to_state(in): The outter sysop (or transaction) returns to this state.
 *
 * Note:Declare the system top operation as completely finished. A top
 *              is finished by logging a top system commit or abort log
 *              record (depending upon the result flag).
 */
static TRAN_STATE
log_complete_system_op (THREAD_ENTRY * thread_p, LOG_TDES * tdes,
			LOG_RESULT_TOPOP result, TRAN_STATE back_to_state)
{
  TRAN_STATE state;

  state = tdes->state;

  switch (result)
    {
    case LOG_RESULT_TOPOP_COMMIT:
    case LOG_RESULT_TOPOP_ABORT:
      state = log_complete_topop (thread_p, tdes, result);
      break;

    case LOG_RESULT_TOPOP_ATTACH_TO_OUTER:
      log_complete_topop_attach (tdes);
      break;
    }

  /*
   * Release the top system operation from the transaction and
   * return to normal transaction state
   */
  tdes->topops.last--;
  if (tdes->topops.last >= 0)
    {
      LSA_COPY (&tdes->topop_lsa,
		&tdes->topops.stack[tdes->topops.last].lastparent_lsa);
    }
  else
    {
      LSA_SET_NULL (&tdes->topop_lsa);
    }
  tdes->state = back_to_state;
  if (LOG_ISCHECKPOINT_TIME ())
    {
#if defined(SERVER_MODE)
      logpb_do_checkpoint ();
#else /* SERVER_MODE */
      (void) logpb_checkpoint (thread_p);
#endif /* SERVER_MODE */
    }

  return state;
}

/*
 *
 *                         CLIENT_USER RECOVERY STUFF
 *
 */

/*
 *
 *                    CLIENT_USER RETRIEVAL OF LOG ACTIONS
 *
 */

#if defined(CUBRID_DEBUG)
static void
log_client_find_system_error (LOG_RECTYPE record_type,
			      LOG_RECTYPE client_type)
{
  switch (record_type)
    {
    case LOG_COMMIT_TOPOPE_WITH_POSTPONE:
    case LOG_COMMIT_TOPOPE:
    case LOG_ABORT_TOPOPE:
      er_log_debug (ARG_FILE_LINE, "log_client_find: SYSTEM ERROR.."
		    " Bad log_rectype = %d\n (%s)."
		    " Maybe BAD CLIENT RANGE\n",
		    record_type, log_to_string (record_type));
      break;

    case LOG_COMMIT_TOPOPE_WITH_CLIENT_USER_LOOSE_ENDS:
    case LOG_ABORT_TOPOPE_WITH_CLIENT_USER_LOOSE_ENDS:
      if ((record_type == LOG_ABORT_TOPOPE_WITH_CLIENT_USER_LOOSE_ENDS
	   && client_type != LOG_CLIENT_USER_UNDO_DATA)
	  || (record_type == LOG_COMMIT_TOPOPE_WITH_CLIENT_USER_LOOSE_ENDS
	      && client_type != LOG_CLIENT_USER_POSTPONE_DATA))
	{
	  er_log_debug (ARG_FILE_LINE, "log_client_find: SYSTEM ERROR.. "
			"Bad log_rectype = %d (%s)... ignored",
			record_type, log_to_string (record_type));
	}
      break;

    case LOG_COMMIT_WITH_CLIENT_USER_LOOSE_ENDS:
    case LOG_2PC_COMMIT_DECISION:
    case LOG_2PC_COMMIT_INFORM_PARTICPS:
      if (client_type != LOG_CLIENT_USER_POSTPONE_DATA)
	{
	  er_log_debug (ARG_FILE_LINE, "log_client_find: SYSTEM ERROR.. "
			"Bad log_rectype = %d (%s)... ignored\n",
			record_type, log_to_string (record_type));
	}
      break;

    case LOG_ABORT_WITH_CLIENT_USER_LOOSE_ENDS:
    case LOG_2PC_ABORT_DECISION:
    case LOG_2PC_ABORT_INFORM_PARTICPS:
      if (client_type != LOG_CLIENT_USER_UNDO_DATA)
	{
	  er_log_debug (ARG_FILE_LINE, "log_client_find: SYSTEM ERROR.. "
			"Bad log_rectype = %d (%s)... ignored",
			record_type, log_to_string (record_type));
	}
      break;

    case LOG_COMMIT:
    case LOG_ABORT:
    case LOG_START_CHKPT:
    case LOG_END_CHKPT:
    case LOG_2PC_RECV_ACK:
    case LOG_DUMMY_CRASH_RECOVERY:
    case LOG_DUMMY_HA_SERVER_STATE:
    case LOG_DUMMY_OVF_RECORD:
    case LOG_SMALLER_LOGREC_TYPE:
    case LOG_LARGER_LOGREC_TYPE:
    default:
      er_log_debug (ARG_FILE_LINE, "log_client_find: SYSTEM ERROR.. "
		    "Bad log_rectype = %d (%s)... ignored\n",
		    record_type, log_to_string (record_type));
      break;
    }
}
#endif

/*
 * log_find_client - Scan forward finding client actions to execute on a
 *                  client machine.
 *
 * return: nothing
 *
 *   tdes(in): Transaction descriptor
 *   next_lsa(in/out): Next action to execute (start looking at this address).
 *              Set as a side effect to the next action to execute for future
 *              calls (A null value will indicate done)
 *   type(in): Either: LOG_CLIENT_USER_POSTPONE_DATA or
 *                      LOG_CLIENT_USER_UNDO_DATA
 *
 * NOTE:Scan the log forward (starting at next_lsa) finding either
 *              postpone or undo client actions depending on the value of
 *              type. A set of actions is returned in the log copy area and
 *              the next actions to execute is indicated in next_lsa.
 *
 * NOTE/WARNING: It is an error to call this function when the transaction is
 *               active (i.e., it is not in the commit or abort transition
 *               period).
 */
static LOG_COPY *
log_client_find_actions (THREAD_ENTRY * thread_p, LOG_TDES * tdes,
			 LOG_LSA * next_lsa, LOG_RECTYPE type)
{
  LOG_COPY *log_area = NULL;	/* Area where actions are copied   */
  int area_length = 0;		/* Length of area                  */
  int area_offset = 0;		/* Offset of area                  */
  struct manylogs *manylogs = NULL;	/* Pointer to many log copy records */
  struct onelog *onelog;	/* Pointer to one log copy record  */
  int length;			/* Length to copy                  */
  LOG_LSA forward_lsa;		/* Lsa of log rec. being proceesed */
  LOG_LSA log_lsa;
  char log_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT], *aligned_log_pgbuf;
  LOG_PAGE *log_pgptr = NULL;	/* Pointer to a log page           */
  LOG_RECORD_HEADER *log_rec;	/* Pointer to log record           */
  struct log_client *client;	/* An undo/postpone client log
				 * record
				 */
  LOG_LSA next_start_clientlsa;	/* Next address to look for client
				 * records. Usually the end of a top
				 * system operation.
				 */
  LOG_LSA end_clientlsa;	/* The last client record of
				 * transaction cannot be after this
				 * address
				 */
  LOG_LSA start_rangelsa;	/* start looking for client records
				 * at this address
				 */
  LOG_LSA *end_rangelsa;	/* Stop looking for client records
				 * at this address
				 */

  LOG_TOPOP_RANGE nxtop_array[LOG_TOPOP_STACK_INIT_SIZE];
  LOG_TOPOP_RANGE *nxtop_stack = NULL;
  LOG_TOPOP_RANGE *nxtop_range = NULL;
  int nxtop_count = 0;
  bool isdone;

  aligned_log_pgbuf = PTR_ALIGN (log_pgbuf, MAX_ALIGNMENT);

  /* The last client record cannot be after the current tail */
  LSA_COPY (&end_clientlsa, &tdes->tail_lsa);
  LSA_COPY (&next_start_clientlsa, next_lsa);

  nxtop_stack = nxtop_array;
  nxtop_count = log_get_next_nested_top (thread_p, tdes, next_lsa,
					 &nxtop_stack);

  while (!LSA_ISNULL (&next_start_clientlsa))
    {
      LSA_COPY (&start_rangelsa, &next_start_clientlsa);

      if (nxtop_count > 0)
	{
	  nxtop_count--;
	  nxtop_range = &(nxtop_stack[nxtop_count]);

	  if (LSA_LT (&start_rangelsa, &(nxtop_range->start_lsa)))
	    {
	      end_rangelsa = &(nxtop_range->start_lsa);
	      LSA_COPY (&next_start_clientlsa, &(nxtop_range->end_lsa));
	    }
	  else if (LSA_EQ (&start_rangelsa, &(nxtop_range->end_lsa)))
	    {
	      end_rangelsa = &end_clientlsa;
	      LSA_SET_NULL (&next_start_clientlsa);
	    }
	  else
	    {
	      LSA_COPY (&next_start_clientlsa, &(nxtop_range->end_lsa));
	      continue;
	    }
	}
      else
	{
	  end_rangelsa = &end_clientlsa;
	  LSA_SET_NULL (&next_start_clientlsa);
	}

      /*
       * GO FORWARD from the next_lsa (if any) or from the address indicated in
       * the header of the transaction descriptor
       */

      LSA_COPY (&forward_lsa, &start_rangelsa);

      log_pgptr = (LOG_PAGE *) aligned_log_pgbuf;

      isdone = false;
      while (!LSA_ISNULL (&forward_lsa) && !isdone)
	{
	  /* Find the page where the log record of the lsa is located */

	  log_lsa.pageid = forward_lsa.pageid;

	  if ((logpb_fetch_page (thread_p, log_lsa.pageid, log_pgptr)) ==
	      NULL)
	    {
	      logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
				 "log_client_find");
	      goto error;
	    }

	  while (forward_lsa.pageid == log_lsa.pageid)
	    {
	      if (LSA_GT (&forward_lsa, end_rangelsa))
		{
		  /* Finsh at this point */
		  isdone = true;
		  break;
		}

	      /*
	       * If an offset is missing, it is because an incomplete log record
	       * was archived. This log_record was completed later, but we do not
	       * modify archived pages. Thus, we have to find the offset by
	       * searching for the next log_record in the page
	       */

	      if (forward_lsa.offset == NULL_OFFSET)
		{
		  forward_lsa.offset = log_pgptr->hdr.offset;
		  if (forward_lsa.offset == NULL_OFFSET)
		    {
		      /* Continue at next pageid */
		      if (logpb_is_page_in_archive (log_lsa.pageid))
			{
			  forward_lsa.pageid = log_lsa.pageid + 1;
			}
		      else
			{
			  forward_lsa.pageid = NULL_PAGEID;
			}
		      continue;
		    }
		}

	      /* Find the log record */
	      log_lsa.offset = forward_lsa.offset;
	      log_rec = LOG_GET_LOG_RECORD_HEADER (log_pgptr, &log_lsa);

	      /* Find the next log record in the log */
	      LSA_COPY (&forward_lsa, &log_rec->forw_lsa);

	      /*
	       * If the next page is NULL_PAGEID and the current page is an archive
	       * page, this is not the end, this situation happens when an incomplete
	       * log record is archived. Thus, its forward address is NULL.
	       * Note that we have to set lsa.pageid here since the log_lsa.pageid value
	       * can be changed (e.g., the log record is stored in an archive page
	       * and in an active page. Later, we try to modify it whenever is
	       * possible.
	       */

	      if (LSA_ISNULL (&forward_lsa)
		  && logpb_is_page_in_archive (log_lsa.pageid))
		{
		  forward_lsa.pageid = log_lsa.pageid + 1;
		}

	      if (log_rec->trid == tdes->trid)
		{
		  switch (log_rec->type)
		    {
		    case LOG_CLIENT_NAME:
		    case LOG_UNDOREDO_DATA:
		    case LOG_DIFF_UNDOREDO_DATA:
		    case LOG_UNDO_DATA:
		    case LOG_REDO_DATA:
		    case LOG_MVCC_UNDOREDO_DATA:
		    case LOG_MVCC_DIFF_UNDOREDO_DATA:
		    case LOG_MVCC_UNDO_DATA:
		    case LOG_MVCC_REDO_DATA:
		    case LOG_DBEXTERN_REDO_DATA:
		    case LOG_DUMMY_HEAD_POSTPONE:
		    case LOG_POSTPONE:
		    case LOG_RUN_POSTPONE:
		    case LOG_COMPENSATE:
		    case LOG_LCOMPENSATE:
		    case LOG_WILL_COMMIT:
		    case LOG_COMMIT_WITH_POSTPONE:
		    case LOG_SAVEPOINT:
		    case LOG_2PC_PREPARE:
		    case LOG_2PC_START:
		    case LOG_REPLICATION_DATA:
		    case LOG_REPLICATION_SCHEMA:
		    case LOG_UNLOCK_COMMIT:
		    case LOG_UNLOCK_ABORT:
		    case LOG_DUMMY_HA_SERVER_STATE:
		    case LOG_DUMMY_OVF_RECORD:
		      break;

		    case LOG_COMMIT_TOPOPE_WITH_POSTPONE:
		    case LOG_COMMIT_TOPOPE:
		    case LOG_ABORT_TOPOPE:
		    case LOG_COMMIT_TOPOPE_WITH_CLIENT_USER_LOOSE_ENDS:
		    case LOG_ABORT_TOPOPE_WITH_CLIENT_USER_LOOSE_ENDS:
		      if (!LSA_EQ (&log_lsa, &start_rangelsa))
			{
#if defined(CUBRID_DEBUG)
			  log_client_find_system_error (log_rec->type, type);
#endif /* CUBRID_DEBUG */
			  /* The following is done due to a potential loop */
			  LSA_SET_NULL (next_lsa);
			  LSA_SET_NULL (&forward_lsa);
			}
		      break;

		    case LOG_CLIENT_USER_POSTPONE_DATA:
		    case LOG_CLIENT_USER_UNDO_DATA:
		      if (log_rec->type != type)
			{
			  break;
			}

		      LSA_COPY (next_lsa, &log_lsa);

		      /* Get the DATA HEADER */
		      LOG_READ_ADD_ALIGN (thread_p, sizeof (*log_rec),
					  &log_lsa, log_pgptr);
		      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p,
							sizeof (*client),
							&log_lsa, log_pgptr);

		      client =
			(struct log_client *) ((char *) log_pgptr->area +
					       log_lsa.offset);

		      /* Do we have already a log client copy area ? */

		      if (log_area == NULL || manylogs == NULL)
			{
			  /*
			   * Create a log client copy area that will hold at least, one
			   * log client record.
			   */
			  area_length = client->length + sizeof (*manylogs);
			  log_area = log_alloc_client_copy_area (area_length);
			  if (log_area == NULL)
			    {
			      goto error;
			    }
			  area_length =
			    (log_area->length - sizeof (*manylogs) +
			     sizeof (*onelog));

			  manylogs =
			    (struct manylogs *) ((char *) log_area->mem +
						 log_area->length -
						 sizeof (*manylogs));
			  manylogs->num_logs = 0;
			  area_offset = 0;
			}

		      if (area_length >=
			  (client->length + (int) sizeof (*onelog)))
			{
			  onelog = ((struct onelog *)
				    ((char *) log_area->mem +
				     log_area->length -
				     sizeof (*manylogs) -
				     manylogs->num_logs * sizeof (*onelog)));
			  manylogs->num_logs++;
			  onelog->rcvindex = client->rcvclient_index;
			  onelog->length = client->length;
			  onelog->offset = area_offset;

			  /* NOW COPY THE DATA */

			  LOG_READ_ADD_ALIGN (thread_p, sizeof (*client),
					      &log_lsa, log_pgptr);

			  logpb_copy_from_log (thread_p,
					       (char *) log_area->mem +
					       area_offset, onelog->length,
					       &log_lsa, log_pgptr);

			  /* Update the length and offset of area */

			  /* Add any wasted space due to alignment */
			  length =
			    DB_ALIGN (onelog->length, DOUBLE_ALIGNMENT);
			  area_length -= length + sizeof (*onelog);
			  area_offset += length;
			}
		      else
			{
			  /* The log record does not fit here... stop at this moment */
			  LSA_SET_NULL (&forward_lsa);
			  LSA_SET_NULL (&next_start_clientlsa);
			}
		      break;

		    case LOG_RUN_NEXT_CLIENT_UNDO:
		    case LOG_RUN_NEXT_CLIENT_POSTPONE:
		      break;

		    case LOG_COMMIT_WITH_CLIENT_USER_LOOSE_ENDS:
		    case LOG_2PC_COMMIT_DECISION:
		    case LOG_2PC_COMMIT_INFORM_PARTICPS:
		    case LOG_ABORT_WITH_CLIENT_USER_LOOSE_ENDS:
		    case LOG_2PC_ABORT_DECISION:
		    case LOG_2PC_ABORT_INFORM_PARTICPS:
#if defined(CUBRID_DEBUG)
		      log_client_find_system_error (log_rec->type, type);
#endif /* CUBRID_DEBUG */
		      /* This is it */
		      LSA_SET_NULL (next_lsa);
		      LSA_SET_NULL (&forward_lsa);
		      break;

		    case LOG_END_OF_LOG:
		      if (logpb_is_page_in_archive (log_lsa.pageid))
			{
			  break;
			}

		    case LOG_COMMIT:
		    case LOG_ABORT:
		    case LOG_START_CHKPT:
		    case LOG_END_CHKPT:
		    case LOG_2PC_RECV_ACK:
		    case LOG_DUMMY_CRASH_RECOVERY:
		    case LOG_SMALLER_LOGREC_TYPE:
		    case LOG_LARGER_LOGREC_TYPE:
		      /* fall through default */
		    default:
		      {
			char msg[LINE_MAX];

			er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
				ER_LOG_PAGE_CORRUPTED, 1, log_lsa.pageid);
			log_dump_record_header_to_string (log_rec, msg,
							  LINE_MAX);
			logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
					   msg);
			break;
		      }
		    }
		}
	      /*
	       * We can fix the lsa.pageid in the case of log_records without forward
	       * address at this moment.
	       */
	      if (forward_lsa.offset == NULL_OFFSET
		  && forward_lsa.pageid != NULL_PAGEID
		  && forward_lsa.pageid < log_lsa.pageid)
		{
		  forward_lsa.pageid = log_lsa.pageid;
		}
	    }
	}
    }

  if (nxtop_stack != nxtop_array && nxtop_stack != NULL)
    {
      free_and_init (nxtop_stack);
    }

  return log_area;

error:

  /* The following is done due to a potential loop */
  LSA_SET_NULL (next_lsa);
  LSA_SET_NULL (&forward_lsa);

  if (nxtop_stack != nxtop_array && nxtop_stack != NULL)
    {
      free_and_init (nxtop_stack);
    }

  return NULL;
}

/*
 * log_client_append_done_actions - Record the client actions that have been
 *                                 already executed
 *
 * return: nothing..
 *
 *   tdes(in): Transaction descriptor
 *   rectype(in): Type of client actions executed..
 *              Either: LOG_RUN_NEXT_CLIENT_POSTPONE,
 *                      LOG_RUN_NEXT_CLIENT_UNDO
 *   next_lsa(in): The next action to be executed. Anything before these actions
 *              has been executed.
 *
 * NOTE: Log the next client action to be executed. That is, all
 *              before this one have already been executed.
 */
static void
log_client_append_done_actions (THREAD_ENTRY * thread_p, LOG_TDES * tdes,
				LOG_RECTYPE rectype, LOG_LSA * next_lsa)
{
  struct log_run_client *run_client;
  LOG_PRIOR_NODE *node;
  LOG_LSA start_lsa;

  node = prior_lsa_alloc_and_copy_data (thread_p, rectype,
					RV_NOT_DEFINED, NULL,
					0, NULL, 0, NULL);
  if (node == NULL)
    {
      return;
    }

  run_client = (struct log_run_client *) node->data_header;
  LSA_COPY (&run_client->nxlsa, next_lsa);

  start_lsa = prior_lsa_next_record (thread_p, node, tdes);

  logpb_flush_pages (thread_p, &start_lsa);
}

/*
 * xlog_client_get_first_postpone -GET THE FIRST SET OF POSTPONE CLIENT ACTIONS
 *
 * return: logpb_copy_database area
 *
 *   next_lsa(in/out): Set as a side effect to the next action to execute for
 *              future calls (A null value will indicate no more actions
 *              to execute)
 *
 * NOTE: Scan the log forward from the first client postpone action and
 *              return as many client postpone actions as possible in one log
 *              copy area. (Usually of page size).
 */
LOG_COPY *
xlog_client_get_first_postpone (THREAD_ENTRY * thread_p, LOG_LSA * next_lsa)
{
  LOG_TDES *tdes;		/* Transaction descriptor */
  int tran_index;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
      return NULL;
    }

  if (tdes->state != TRAN_UNACTIVE_COMMITTED_WITH_CLIENT_USER_LOOSE_ENDS
      && (tdes->state
	  != TRAN_UNACTIVE_XTOPOPE_COMMITTED_WITH_CLIENT_USER_LOOSE_ENDS))
    {
      /*
       * May be a system error since transaction is not in correct state to
       * execute client postpone operations
       */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_LOG_BADSTATE_FOR_CLIENT_UNDO_OR_POSTPONE, 3,
	      log_state_string (tdes->state), tdes->trid, tdes->tran_index);
      return NULL;
    }

  LSA_COPY (next_lsa, &tdes->client_posp_lsa);

  return log_client_find_actions (thread_p, tdes, next_lsa,
				  LOG_CLIENT_USER_POSTPONE_DATA);
}

/*
 * xlog_client_get_next_postpone - GET THE NEXT SET OF POSTPONE CLIENT ACTIONS
 *
 * return:  logpb_copy_database area
 *
 *   next_lsa(in/out): Next action to execute (start looking at this address).
 *              Set as a side effect to the next action to execute for future
 *              calls (A null value will indicate done)
 *
 * NOTE:Scan the log forward (starting at next_lsa) finding more
 *              postpone actions that need to be executed.
 */
LOG_COPY *
xlog_client_get_next_postpone (THREAD_ENTRY * thread_p, LOG_LSA * next_lsa)
{
  LOG_TDES *tdes;		/* Transaction descriptor */
  int tran_index;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
      return NULL;
    }

  if (tdes->state != TRAN_UNACTIVE_COMMITTED_WITH_CLIENT_USER_LOOSE_ENDS
      && (tdes->state !=
	  TRAN_UNACTIVE_XTOPOPE_COMMITTED_WITH_CLIENT_USER_LOOSE_ENDS))
    {
      /*
       * May be a system error since transaction is not in correct state to
       * execute client postpone operations
       */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_LOG_BADSTATE_FOR_CLIENT_UNDO_OR_POSTPONE, 3,
	      log_state_string (tdes->state), tdes->trid, tdes->tran_index);
      return NULL;
    }

  /*
   * Assume that all postpone operations before next_lsa have been executed.
   * Log this assumption.
   */

  LSA_COPY (&tdes->client_posp_lsa, next_lsa);

  log_client_append_done_actions (thread_p, tdes,
				  LOG_RUN_NEXT_CLIENT_POSTPONE,
				  &tdes->client_posp_lsa);

  /*
   * Send more information
   */

  return log_client_find_actions (thread_p, tdes, next_lsa,
				  LOG_CLIENT_USER_POSTPONE_DATA);

}

/*
 * xlog_client_complete_postpone -Client has finished all postpone actions
 *
 * return: state of commit operation
 *
 * NOTE:All client postpone actions are declared as completed. The
 *              commit process can continue from this point.
 */
TRAN_STATE
xlog_client_complete_postpone (THREAD_ENTRY * thread_p)
{
  LOG_TDES *tdes;		/* Transaction descriptor */
  TRAN_STATE state;
  int tran_index;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
      return TRAN_UNACTIVE_UNKNOWN;
    }

  if (tdes->state != TRAN_UNACTIVE_COMMITTED_WITH_CLIENT_USER_LOOSE_ENDS
      && (tdes->state
	  != TRAN_UNACTIVE_XTOPOPE_COMMITTED_WITH_CLIENT_USER_LOOSE_ENDS))
    {
      /*
       * May be a system error since transaction is not in correct state to
       * execute client postpone operations
       */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_LOG_BADSTATE_FOR_CLIENT_UNDO_OR_POSTPONE, 3,
	      log_state_string (tdes->state), tdes->trid, tdes->tran_index);
      return tdes->state;
    }

  /* All client postpone actions have been done. */
  LSA_SET_NULL (&tdes->client_posp_lsa);
  log_client_append_done_actions (thread_p, tdes,
				  LOG_RUN_NEXT_CLIENT_POSTPONE,
				  &tdes->client_posp_lsa);

  if (tdes->state
      == TRAN_UNACTIVE_XTOPOPE_COMMITTED_WITH_CLIENT_USER_LOOSE_ENDS)
    {
      /* This is a top system operation */
      state = log_complete_system_op (thread_p, tdes, LOG_RESULT_TOPOP_COMMIT,
				      TRAN_ACTIVE);
    }
  else
    {
      /*
       * If the transaction is a distributed one, continue with the rest of the
       * 2PC. That is, multicast the commit decision.
       * THIS BROADCAST WILL BE REMOVED FROM HERE ONCE THE ASYNCRONOUS
       * COMMUNICATION IS AVAILABLE. (The broadcast will be done before the
       * client postpone operations are done).
       */

      if (tdes->coord != NULL)
	{
	  /*
	   * If the following function fails, the transaction will be dangling
	   * and we need to retry sending the decision at another point.
	   * We have already decided and log the decision in the log file.
	   */
	  (void) log_2pc_send_commit_decision (tdes->gtrid,
					       tdes->coord->num_particps,
					       tdes->coord->ack_received,
					       tdes->
					       coord->block_particps_ids);
	}
      state = log_complete (thread_p, tdes, LOG_COMMIT, LOG_NEED_NEWTRID);
    }

  return state;

}

/*
 * xlog_client_get_first_undo - GET THE FIRST SET OF UNDO CLIENT ACTIONS
 *
 * return: logpb_copy_database area
 *
 *   next_lsa(in/out): Set as a side effect to the next action to execute
 *              for future calls (A null value will indicate no more actions
 *              to execute)
 *
 * NOTE:Scan the log forward from the first client undo action and
 *              return as many client undo actions as possible in one log copy
 *              area. (Usually of page size).
 */
LOG_COPY *
xlog_client_get_first_undo (THREAD_ENTRY * thread_p, LOG_LSA * next_lsa)
{
  LOG_TDES *tdes;		/* Transaction descriptor */
  int tran_index;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
      return NULL;
    }

  if (tdes->state != TRAN_UNACTIVE_ABORTED_WITH_CLIENT_USER_LOOSE_ENDS
      && (tdes->state
	  != TRAN_UNACTIVE_TOPOPE_ABORTED_WITH_CLIENT_USER_LOOSE_ENDS))
    {
      /*
       * May be a system error since transaction is not in correct state to
       * execute client undo operations
       */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_LOG_BADSTATE_FOR_CLIENT_UNDO_OR_POSTPONE, 3,
	      log_state_string (tdes->state), tdes->trid, tdes->tran_index);
      return NULL;
    }

  LSA_COPY (next_lsa, &tdes->client_undo_lsa);

  return log_client_find_actions (thread_p, tdes, next_lsa,
				  LOG_CLIENT_USER_UNDO_DATA);

}

/*
 * xlog_client_unknown_state_abort_get_first_undo - Get the first set of undo
 *                                                client actions
 *
 * return: logpb_copy_database area  or NULL
 *
 *   next_lsa(in/out): Set as a side effect to the next action to execute
 *              for future calls (A null value will indicate no more actions
 *              to execute)
 *
 * NOTE:Same as log_client_get_first_undo, however, if the transaction
 *              is not in the right state, it returns without errors.
 */
LOG_COPY *
xlog_client_unknown_state_abort_get_first_undo (THREAD_ENTRY * thread_p,
						LOG_LSA * next_lsa)
{
  LOG_TDES *tdes;		/* Transaction descriptor */
  int tran_index;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
      return NULL;
    }

  if (tdes->state != TRAN_UNACTIVE_ABORTED_WITH_CLIENT_USER_LOOSE_ENDS
      && (tdes->state
	  != TRAN_UNACTIVE_TOPOPE_ABORTED_WITH_CLIENT_USER_LOOSE_ENDS))
    {
      return NULL;
    }
  else
    {
      return xlog_client_get_first_undo (thread_p, next_lsa);
    }

}

/*
 * xlog_client_get_next_undo - GET THE NEXT SET OF UNDO CLIENT ACTIONS
 *
 * return: logpb_copy_database area
 *
 *   next_lsa(in): Next action to execute (start looking at this address).
 *              Set as a side effect to the next action to execute for future
 *              calls (A null value will indicate done)
 *
 * NOTE:Scan the log forward (starting at next_lsa) finding more
 *              undo actions that need to be executed.
 */
LOG_COPY *
xlog_client_get_next_undo (THREAD_ENTRY * thread_p, LOG_LSA * next_lsa)
{
  LOG_TDES *tdes;		/* Transaction descriptor */
  int tran_index;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
      return NULL;
    }

  if (tdes->state != TRAN_UNACTIVE_ABORTED_WITH_CLIENT_USER_LOOSE_ENDS
      && (tdes->state
	  != TRAN_UNACTIVE_TOPOPE_ABORTED_WITH_CLIENT_USER_LOOSE_ENDS))
    {
      /*
       * May be a system error since transaction is not in correct state to
       * execute client undo operations
       */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_LOG_BADSTATE_FOR_CLIENT_UNDO_OR_POSTPONE, 3,
	      log_state_string (tdes->state), tdes->trid, tdes->tran_index);
      return NULL;
    }

  /*
   * Assume that all postpone operations before next_lsa have been executed.
   * Log this assumption.
   */

  LSA_COPY (&tdes->client_undo_lsa, next_lsa);
  log_client_append_done_actions (thread_p, tdes, LOG_RUN_NEXT_CLIENT_UNDO,
				  &tdes->client_undo_lsa);

  return log_client_find_actions (thread_p, tdes, next_lsa,
				  LOG_CLIENT_USER_UNDO_DATA);
}

/*
 * xlog_client_complete_undo - CLIENT HAS FINISHED ALL UNDO ACTIONS
 *
 * return: state of abort operation
 *
 * NOTE:All client undo actions are declared as completed. The abort
 *              process can continue from this point.
 */
TRAN_STATE
xlog_client_complete_undo (THREAD_ENTRY * thread_p)
{
  LOG_TDES *tdes;		/* Transaction descriptor */
  TRAN_STATE state;
  int tran_index;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
      return TRAN_UNACTIVE_UNKNOWN;
    }

  if (tdes->state != TRAN_UNACTIVE_ABORTED_WITH_CLIENT_USER_LOOSE_ENDS
      && (tdes->state
	  != TRAN_UNACTIVE_TOPOPE_ABORTED_WITH_CLIENT_USER_LOOSE_ENDS))
    {
      /*
       * May be a system error since transaction is not in correct state to
       * execute client undo operations
       */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_LOG_BADSTATE_FOR_CLIENT_UNDO_OR_POSTPONE, 3,
	      log_state_string (tdes->state), tdes->trid, tdes->tran_index);
      return tdes->state;
    }

  /*
   * Assume that all postpone operations before next_lsa have been executed.
   * Log this assumption.
   */

  LSA_SET_NULL (&tdes->client_undo_lsa);
  log_client_append_done_actions (thread_p, tdes, LOG_RUN_NEXT_CLIENT_UNDO,
				  &tdes->client_undo_lsa);

  if (tdes->state == TRAN_UNACTIVE_TOPOPE_ABORTED_WITH_CLIENT_USER_LOOSE_ENDS)
    {
      /* This is a top system operation */
      state = log_complete_system_op (thread_p, tdes, LOG_RESULT_TOPOP_ABORT,
				      TRAN_ACTIVE);
    }
  else
    {
      /*
       * If the transaction is a distributed one, continue with the rest of the
       * 2PC. That is, multicast the abort decsion.
       * THIS BROADCAST WILL BE REMOVED FROM HERE ONCE THE ASYNCRONOUS
       * COMMUNICATION IS AVAILABLE. (The broadcast will be done before the
       * client postpone operations are done).
       */

      if (tdes->coord != NULL)
	{
	  /*
	   * Coordinator site of a distributed transaction
	   */
	  if (tdes->coord->ack_received)
	    {
	      /* Abort was the decision made by the coordinator as a result of
	       * voting phase of 2PC. Thus, we need to collect acknowledgements.
	       *
	       * If the following function fails, the transaction will be dangling
	       * and we need to retry sending the decision at another point.
	       * We have already decided and log the decision in the log file.
	       */
	      (void) log_2pc_send_abort_decision (tdes->gtrid,
						  tdes->coord->num_particps,
						  tdes->coord->ack_received,
						  tdes->
						  coord->block_particps_ids,
						  true);
	    }
	  else
	    {
	      /* Abort was decided prior to starting 2PC protocol. (None of the
	       * participants are prepared to commit, yet). Thus, there is no need
	       * to collect acknowledgements.
	       *
	       * If the following function fails, the transaction will be dangling
	       * and we need to retry sending the decision at another point.
	       * We have already decided and log the decision in the log file.
	       */
	      (void) log_2pc_send_abort_decision (tdes->gtrid,
						  tdes->coord->num_particps,
						  tdes->coord->ack_received,
						  tdes->
						  coord->block_particps_ids,
						  false);
	    }
	}
      state = log_complete (thread_p, tdes, LOG_ABORT, LOG_NEED_NEWTRID);
    }

  return state;

}

/*
 *
 *              FUNCTIONS RELATED TO DUMPING THE LOG AND ITS DATA
 *
 */

/*
 * log_ascii_dump - PRINT DATA IN ASCII FORMAT
 *
 * return: nothing
 *
 *   length(in): Length of Recovery Data
 *   data(in): The data being logged
 *
 * NOTE: Dump recovery information in ascii format.
 *              It is used when a dump function is not provided.
 */
static void
log_ascii_dump (FILE * out_fp, int length, void *data)
{
  char *ptr;			/* Pointer to data */
  int i;

  for (i = 0, ptr = (char *) data; i < length; i++)
    {
      (void) fputc (*ptr++, out_fp);
    }
}

static void
log_repl_data_dump (FILE * out_fp, int length, void *data)
{
  char *ptr;
  char *class_name;
  DB_VALUE value;
  PARSER_VARCHAR *buf;
  PARSER_CONTEXT *parser;

  ptr = data;
  ptr = or_unpack_string_nocopy (ptr, &class_name);
  ptr = or_unpack_mem_value (ptr, &value);

  parser = parser_create_parser ();
  if (parser != NULL)
    {
      buf = describe_value (parser, NULL, &value);
      fprintf (out_fp, "C[%s] K[%s]\n", class_name,
	       pt_get_varchar_bytes (buf));
      parser_free_parser (parser);
    }
  pr_clear_value (&value);
}

static void
log_repl_schema_dump (FILE * out_fp, int length, void *data)
{
  char *ptr;
  int statement_type;
  char *class_name;
  char *sql;

  ptr = data;
  ptr = or_unpack_int (ptr, &statement_type);
  ptr = or_unpack_string_nocopy (ptr, &class_name);
  ptr = or_unpack_string_nocopy (ptr, &sql);

  fprintf (out_fp, "C[%s] S[%s]\n", class_name, sql);
}

/*
 * log_dump_data - DUMP DATA STORED IN LOG
 *
 * return: nothing
 *
 *   length(in): Length of the data
 *   log_lsa(in/out):Log address identifier containing the log record
 *   log_pgptr(in/out):  Pointer to page where data starts (Set as a side
 *              effect to the page where data ends)
 *   dumpfun(in): Function to invoke to dump the data
 *   log_dump_ptr(in):
 *
 * NOTE:Dump the data stored at given log location.
 *              This function is used for debugging purposes.
 */
static void
log_dump_data (THREAD_ENTRY * thread_p, FILE * out_fp, int length,
	       LOG_LSA * log_lsa, LOG_PAGE * log_page_p,
	       void (*dumpfun) (FILE *, int, void *), LOG_ZIP * log_dump_ptr)
{
  char *ptr;			/* Pointer to data to be printed            */
  bool is_zipped = false;
  bool is_unzipped = false;
  /* Call the dumper function */

  /*
   * If data is contained in only one buffer, pass pointer directly.
   * Otherwise, allocate a contiguous area, copy the data and pass this
   * area. At the end deallocate the area
   */

  if (ZIP_CHECK (length))
    {
      length = (int) GET_ZIP_LEN (length);
      is_zipped = true;
    }

  if (log_lsa->offset + length < (int) LOGAREA_SIZE)
    {
      /* Data is contained in one buffer */

      ptr = (char *) log_page_p->area + log_lsa->offset;

      if (length != 0 && is_zipped)
	{
	  is_unzipped = log_unzip (log_dump_ptr, length, ptr);
	}

      if (is_zipped && is_unzipped)
	{
	  (*dumpfun) (out_fp, (int) log_dump_ptr->data_length,
		      log_dump_ptr->log_data);
	  log_lsa->offset += length;
	}
      else
	{
	  (*dumpfun) (out_fp, length, ptr);
	  log_lsa->offset += length;
	}
    }
  else
    {
      /* Need to copy the data into a contiguous area */
      ptr = (char *) malloc (length);
      if (ptr == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, length);
	  return;
	}
      /* Copy the data */
      logpb_copy_from_log (thread_p, ptr, length, log_lsa, log_page_p);

      if (is_zipped)
	{
	  is_unzipped = log_unzip (log_dump_ptr, length, ptr);
	}

      if (is_zipped && is_unzipped)
	{
	  (*dumpfun) (out_fp, (int) log_dump_ptr->data_length,
		      log_dump_ptr->log_data);
	}
      else
	{
	  (*dumpfun) (out_fp, length, ptr);
	}
      free_and_init (ptr);
    }
  LOG_READ_ALIGN (thread_p, log_lsa, log_page_p);

}

static void
log_dump_header (FILE * out_fp, struct log_header *log_header_p)
{
  time_t tmp_time;
  char time_val[CTIME_MAX];

  fprintf (out_fp, "\n ** DUMP LOG HEADER **\n");

  tmp_time = (time_t) log_header_p->db_creation;
  (void) ctime_r (&tmp_time, time_val);
  fprintf (out_fp,
	   "HDR: Magic Symbol = %s at disk location = %lld\n"
	   "     Creation_time = %s"
	   "     Release = %s, Compatibility_disk_version = %g,\n"
	   "     Db_pagesize = %d, log_pagesize= %d, Shutdown = %d,\n"
	   "     Next_trid = %d, Next_mvcc_id = %lld, Num_avg_trans = %d, Num_avg_locks = %d,\n"
	   "     Num_active_log_pages = %d, First_active_log_page = %lld,\n"
	   "     Current_append = %lld|%d, Checkpoint = %lld|%d,\n",
	   log_header_p->magic, (long long) offsetof (LOG_PAGE, area),
	   time_val, log_header_p->db_release,
	   log_header_p->db_compatibility, log_header_p->db_iopagesize,
	   log_header_p->db_logpagesize, log_header_p->is_shutdown,
	   log_header_p->next_trid, log_header_p->mvcc_next_id,
	   log_header_p->avg_ntrans,
	   log_header_p->avg_nlocks, log_header_p->npages,
	   (long long int) log_header_p->fpageid,
	   (long long int) log_header_p->append_lsa.pageid,
	   log_header_p->append_lsa.offset,
	   (long long int) log_header_p->chkpt_lsa.pageid,
	   log_header_p->chkpt_lsa.offset);

  fprintf (out_fp,
	   "     Next_archive_pageid = %lld at active_phy_pageid = %d,\n"
	   "     Next_archive_num = %d, Last_archiv_num_for_syscrashes = %d,\n"
	   "     Last_deleted_arv_num = %d, has_logging_been_skipped = %d,\n"
	   "     bkup_lsa: level0 = %lld|%d, level1 = %lld|%d, level2 = %lld|%d,\n"
	   "     Log_prefix = %s\n",
	   (long long int) log_header_p->nxarv_pageid,
	   log_header_p->nxarv_phy_pageid, log_header_p->nxarv_num,
	   log_header_p->last_arv_num_for_syscrashes,
	   log_header_p->last_deleted_arv_num,
	   log_header_p->has_logging_been_skipped,
	   (long long int) log_header_p->bkup_level0_lsa.pageid,
	   log_header_p->bkup_level0_lsa.offset,
	   (long long int) log_header_p->bkup_level1_lsa.pageid,
	   log_header_p->bkup_level1_lsa.offset,
	   (long long int) log_header_p->bkup_level2_lsa.pageid,
	   log_header_p->bkup_level2_lsa.offset, log_header_p->prefix_name);
}

static LOG_PAGE *
log_dump_record_client_name (THREAD_ENTRY * thread_p, FILE * out_fp,
			     LOG_LSA * log_lsa, LOG_PAGE * log_page_p)
{
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, LOG_USERNAME_MAX, log_lsa,
				    log_page_p);
  fprintf (out_fp, "\n     Client Name = %s\n",
	   (char *) log_page_p->area + log_lsa->offset);

  return log_page_p;
}

static LOG_PAGE *
log_dump_record_undoredo (THREAD_ENTRY * thread_p, FILE * out_fp,
			  LOG_LSA * log_lsa, LOG_PAGE * log_page_p,
			  LOG_ZIP * log_zip_p)
{
  struct log_undoredo *undoredo;
  int undo_length;
  int redo_length;
  LOG_RCVINDEX rcvindex;

  /* Read the DATA HEADER */
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*undoredo), log_lsa,
				    log_page_p);
  undoredo =
    (struct log_undoredo *) ((char *) log_page_p->area + log_lsa->offset);
  fprintf (out_fp, ", Recv_index = %s, \n",
	   rv_rcvindex_string (undoredo->data.rcvindex));
  fprintf (out_fp,
	   "     Volid = %d Pageid = %d Offset = %d,\n"
	   "     Undo(Before) length = %d," " Redo(After) length = %d,\n",
	   undoredo->data.volid, undoredo->data.pageid, undoredo->data.offset,
	   (int) GET_ZIP_LEN (undoredo->ulength),
	   (int) GET_ZIP_LEN (undoredo->rlength));

  undo_length = undoredo->ulength;
  redo_length = undoredo->rlength;
  rcvindex = undoredo->data.rcvindex;

  LOG_READ_ADD_ALIGN (thread_p, sizeof (*undoredo), log_lsa, log_page_p);
  /* Print UNDO(BEFORE) DATA */
  fprintf (out_fp, "-->> Undo (Before) Data:\n");
  log_dump_data (thread_p, out_fp, undo_length, log_lsa,
		 log_page_p,
		 ((RV_fun[rcvindex].dump_undofun !=
		   NULL) ? RV_fun[rcvindex].dump_undofun : log_ascii_dump),
		 log_zip_p);
  /* Print REDO (AFTER) DATA */
  fprintf (out_fp, "-->> Redo (After) Data:\n");
  log_dump_data (thread_p, out_fp, redo_length, log_lsa,
		 log_page_p,
		 ((RV_fun[rcvindex].dump_redofun !=
		   NULL) ? RV_fun[rcvindex].dump_redofun : log_ascii_dump),
		 log_zip_p);

  return log_page_p;
}

static LOG_PAGE *
log_dump_record_undo (THREAD_ENTRY * thread_p, FILE * out_fp,
		      LOG_LSA * log_lsa, LOG_PAGE * log_page_p,
		      LOG_ZIP * log_zip_p)
{
  struct log_undo *undo;
  int undo_length;
  LOG_RCVINDEX rcvindex;

  /* Read the DATA HEADER */
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*undo), log_lsa,
				    log_page_p);
  undo = (struct log_undo *) ((char *) log_page_p->area + log_lsa->offset);

  fprintf (out_fp, ", Recv_index = %s,\n",
	   rv_rcvindex_string (undo->data.rcvindex));
  fprintf (out_fp, "     Volid = %d Pageid = %d Offset = %d,\n"
	   "     Undo (Before) length = %d,\n",
	   undo->data.volid, undo->data.pageid, undo->data.offset,
	   (int) GET_ZIP_LEN (undo->length));

  undo_length = undo->length;
  rcvindex = undo->data.rcvindex;
  LOG_READ_ADD_ALIGN (thread_p, sizeof (*undo), log_lsa, log_page_p);

  /* Print UNDO(BEFORE) DATA */
  fprintf (out_fp, "-->> Undo (Before) Data:\n");
  log_dump_data (thread_p, out_fp, undo_length, log_lsa,
		 log_page_p,
		 ((RV_fun[rcvindex].dump_undofun !=
		   NULL) ? RV_fun[rcvindex].dump_undofun : log_ascii_dump),
		 log_zip_p);

  return log_page_p;
}

static LOG_PAGE *
log_dump_record_redo (THREAD_ENTRY * thread_p, FILE * out_fp,
		      LOG_LSA * log_lsa, LOG_PAGE * log_page_p,
		      LOG_ZIP * log_zip_p)
{
  struct log_redo *redo;
  int redo_length;
  LOG_RCVINDEX rcvindex;

  /* Read the DATA HEADER */
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*redo), log_lsa,
				    log_page_p);
  redo = (struct log_redo *) ((char *) log_page_p->area + log_lsa->offset);

  fprintf (out_fp, ", Recv_index = %s,\n",
	   rv_rcvindex_string (redo->data.rcvindex));
  fprintf (stdout, "     Volid = %d Pageid = %d Offset = %d,\n"
	   "     Redo (After) length = %d,\n",
	   redo->data.volid, redo->data.pageid, redo->data.offset,
	   (int) GET_ZIP_LEN (redo->length));

  redo_length = redo->length;
  rcvindex = redo->data.rcvindex;
  LOG_READ_ADD_ALIGN (thread_p, sizeof (*redo), log_lsa, log_page_p);

  /* Print REDO(AFTER) DATA */
  fprintf (stdout, "-->> Redo (After) Data:\n");
  log_dump_data (thread_p, out_fp, redo_length, log_lsa,
		 log_page_p,
		 ((RV_fun[rcvindex].dump_redofun !=
		   NULL) ? RV_fun[rcvindex].dump_redofun : log_ascii_dump),
		 log_zip_p);

  return log_page_p;
}

static LOG_PAGE *
log_dump_record_mvcc_undoredo (THREAD_ENTRY * thread_p, FILE * out_fp,
			       LOG_LSA * log_lsa, LOG_PAGE * log_page_p,
			       LOG_ZIP * log_zip_p)
{
  struct log_mvcc_undoredo *mvcc_undoredo;
  int undo_length;
  int redo_length;
  LOG_RCVINDEX rcvindex;

  /* Read the DATA HEADER */
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*mvcc_undoredo),
				    log_lsa, log_page_p);
  mvcc_undoredo =
    (struct log_mvcc_undoredo *) ((char *) log_page_p->area +
				  log_lsa->offset);
  fprintf (out_fp, ", Recv_index = %s, \n",
	   rv_rcvindex_string (mvcc_undoredo->undoredo.data.rcvindex));
  fprintf (out_fp,
	   "     Volid = %d Pageid = %d Offset = %d,\n"
	   "     Undo(Before) length = %d," " Redo(After) length = %d,\n",
	   mvcc_undoredo->undoredo.data.volid,
	   mvcc_undoredo->undoredo.data.pageid,
	   mvcc_undoredo->undoredo.data.offset,
	   (int) GET_ZIP_LEN (mvcc_undoredo->undoredo.ulength),
	   (int) GET_ZIP_LEN (mvcc_undoredo->undoredo.rlength));
  fprintf (out_fp,
	   "     MVCCID = %lld, \n"
	   "     Prev_mvcc_op_log_lsa = (%lld, %d), \n"
	   "     VFID = (%d, %d)",
	   mvcc_undoredo->mvccid,
	   (long long int)
	   mvcc_undoredo->vacuum_info.prev_mvcc_op_log_lsa.pageid,
	   (int) mvcc_undoredo->vacuum_info.prev_mvcc_op_log_lsa.offset,
	   mvcc_undoredo->vacuum_info.vfid.volid,
	   mvcc_undoredo->vacuum_info.vfid.fileid);

  undo_length = mvcc_undoredo->undoredo.ulength;
  redo_length = mvcc_undoredo->undoredo.rlength;
  rcvindex = mvcc_undoredo->undoredo.data.rcvindex;

  LOG_READ_ADD_ALIGN (thread_p, sizeof (*mvcc_undoredo), log_lsa, log_page_p);
  /* Print UNDO(BEFORE) DATA */
  fprintf (out_fp, "-->> Undo (Before) Data:\n");
  log_dump_data (thread_p, out_fp, undo_length, log_lsa,
		 log_page_p,
		 ((RV_fun[rcvindex].dump_undofun !=
		   NULL) ? RV_fun[rcvindex].dump_undofun : log_ascii_dump),
		 log_zip_p);
  /* Print REDO (AFTER) DATA */
  fprintf (out_fp, "-->> Redo (After) Data:\n");
  log_dump_data (thread_p, out_fp, redo_length, log_lsa,
		 log_page_p,
		 ((RV_fun[rcvindex].dump_redofun !=
		   NULL) ? RV_fun[rcvindex].dump_redofun : log_ascii_dump),
		 log_zip_p);

  return log_page_p;
}

static LOG_PAGE *
log_dump_record_mvcc_undo (THREAD_ENTRY * thread_p, FILE * out_fp,
			   LOG_LSA * log_lsa, LOG_PAGE * log_page_p,
			   LOG_ZIP * log_zip_p)
{
  struct log_mvcc_undo *mvcc_undo;
  int undo_length;
  LOG_RCVINDEX rcvindex;

  /* Read the DATA HEADER */
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*mvcc_undo), log_lsa,
				    log_page_p);
  mvcc_undo =
    (struct log_mvcc_undo *) ((char *) log_page_p->area + log_lsa->offset);

  fprintf (out_fp, ", Recv_index = %s,\n",
	   rv_rcvindex_string (mvcc_undo->undo.data.rcvindex));
  fprintf (out_fp, "     Volid = %d Pageid = %d Offset = %d,\n"
	   "     Undo (Before) length = %d,\n",
	   mvcc_undo->undo.data.volid, mvcc_undo->undo.data.pageid,
	   mvcc_undo->undo.data.offset,
	   (int) GET_ZIP_LEN (mvcc_undo->undo.length));
  fprintf (out_fp,
	   "     MVCCID = %lld, \n"
	   "     Prev_mvcc_op_log_lsa = (%lld, %d), \n"
	   "     VFID = (%d, %d)",
	   mvcc_undo->mvccid,
	   (long long int) mvcc_undo->vacuum_info.prev_mvcc_op_log_lsa.pageid,
	   (int) mvcc_undo->vacuum_info.prev_mvcc_op_log_lsa.offset,
	   mvcc_undo->vacuum_info.vfid.volid,
	   mvcc_undo->vacuum_info.vfid.fileid);

  undo_length = mvcc_undo->undo.length;
  rcvindex = mvcc_undo->undo.data.rcvindex;
  LOG_READ_ADD_ALIGN (thread_p, sizeof (*mvcc_undo), log_lsa, log_page_p);

  /* Print UNDO(BEFORE) DATA */
  fprintf (out_fp, "-->> Undo (Before) Data:\n");
  log_dump_data (thread_p, out_fp, undo_length, log_lsa,
		 log_page_p,
		 ((RV_fun[rcvindex].dump_undofun !=
		   NULL) ? RV_fun[rcvindex].dump_undofun : log_ascii_dump),
		 log_zip_p);

  return log_page_p;
}

static LOG_PAGE *
log_dump_record_mvcc_redo (THREAD_ENTRY * thread_p, FILE * out_fp,
			   LOG_LSA * log_lsa, LOG_PAGE * log_page_p,
			   LOG_ZIP * log_zip_p)
{
  struct log_mvcc_redo *mvcc_redo;
  int redo_length;
  LOG_RCVINDEX rcvindex;

  /* Read the DATA HEADER */
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*mvcc_redo), log_lsa,
				    log_page_p);
  mvcc_redo =
    (struct log_mvcc_redo *) ((char *) log_page_p->area + log_lsa->offset);

  fprintf (out_fp, ", Recv_index = %s,\n",
	   rv_rcvindex_string (mvcc_redo->redo.data.rcvindex));
  fprintf (out_fp, "     Volid = %d Pageid = %d Offset = %d,\n"
	   "     Redo (After) length = %d,\n",
	   mvcc_redo->redo.data.volid, mvcc_redo->redo.data.pageid,
	   mvcc_redo->redo.data.offset,
	   (int) GET_ZIP_LEN (mvcc_redo->redo.length));
  fprintf (out_fp, "     MVCCID = %lld, \n", mvcc_redo->mvccid);

  redo_length = mvcc_redo->redo.length;
  rcvindex = mvcc_redo->redo.data.rcvindex;
  LOG_READ_ADD_ALIGN (thread_p, sizeof (*mvcc_redo), log_lsa, log_page_p);

  /* Print REDO(AFTER) DATA */
  fprintf (stdout, "-->> Redo (After) Data:\n");
  log_dump_data (thread_p, out_fp, redo_length, log_lsa,
		 log_page_p,
		 ((RV_fun[rcvindex].dump_redofun !=
		   NULL) ? RV_fun[rcvindex].dump_redofun : log_ascii_dump),
		 log_zip_p);

  return log_page_p;
}

static LOG_PAGE *
log_dump_record_postpone (THREAD_ENTRY * thread_p, FILE * out_fp,
			  LOG_LSA * log_lsa, LOG_PAGE * log_page_p)
{
  struct log_run_postpone *run_posp;
  int redo_length;
  LOG_RCVINDEX rcvindex;

  /* Read the DATA HEADER */
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*run_posp), log_lsa,
				    log_page_p);
  run_posp =
    (struct log_run_postpone *) ((char *) log_page_p->area + log_lsa->offset);
  fprintf (out_fp, ", Recv_index = %s,\n",
	   rv_rcvindex_string (run_posp->data.rcvindex));
  fprintf (out_fp,
	   "     Volid = %d Pageid = %d Offset = %d,\n"
	   "     Run postpone (Redo/After) length = %d, corresponding" " to\n"
	   "         Postpone record with LSA = %lld|%d\n",
	   run_posp->data.volid, run_posp->data.pageid, run_posp->data.offset,
	   run_posp->length, (long long int) run_posp->ref_lsa.pageid,
	   run_posp->ref_lsa.offset);

  redo_length = run_posp->length;
  rcvindex = run_posp->data.rcvindex;
  LOG_READ_ADD_ALIGN (thread_p, sizeof (*run_posp), log_lsa, log_page_p);

  /* Print RUN POSTPONE (REDO/AFTER) DATA */
  fprintf (out_fp, "-->> Run Postpone (Redo/After) Data:\n");
  log_dump_data (thread_p, out_fp, redo_length, log_lsa,
		 log_page_p,
		 ((RV_fun[rcvindex].dump_redofun !=
		   NULL) ? RV_fun[rcvindex].dump_redofun : log_ascii_dump),
		 NULL);

  return log_page_p;
}

static LOG_PAGE *
log_dump_record_dbout_redo (THREAD_ENTRY * thread_p, FILE * out_fp,
			    LOG_LSA * log_lsa, LOG_PAGE * log_page_p)
{
  struct log_dbout_redo *dbout_redo;
  int redo_length;
  LOG_RCVINDEX rcvindex;

  /* Read the data header */
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*dbout_redo),
				    log_lsa, log_page_p);
  dbout_redo =
    ((struct log_dbout_redo *) ((char *) log_page_p->area + log_lsa->offset));

  redo_length = dbout_redo->length;
  rcvindex = dbout_redo->rcvindex;

  fprintf (out_fp, ", Recv_index = %s, Length = %d,\n",
	   rv_rcvindex_string (rcvindex), redo_length);

  LOG_READ_ADD_ALIGN (thread_p, sizeof (*dbout_redo), log_lsa, log_page_p);

  /* Print Database External DATA */
  fprintf (out_fp, "-->> Database external Data:\n");
  log_dump_data (thread_p, out_fp, redo_length, log_lsa,
		 log_page_p,
		 ((RV_fun[rcvindex].dump_redofun !=
		   NULL) ? RV_fun[rcvindex].dump_redofun : log_ascii_dump),
		 NULL);

  return log_page_p;
}

static LOG_PAGE *
log_dump_record_compensate (THREAD_ENTRY * thread_p, FILE * out_fp,
			    LOG_LSA * log_lsa, LOG_PAGE * log_page_p)
{
  struct log_compensate *compensate;
  int length_compensate;
  LOG_RCVINDEX rcvindex;

  /* Read the DATA HEADER */
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*compensate), log_lsa,
				    log_page_p);
  compensate =
    (struct log_compensate *) ((char *) log_page_p->area + log_lsa->offset);

  fprintf (out_fp, ", Recv_index = %s,\n",
	   rv_rcvindex_string (compensate->data.rcvindex));
  fprintf (out_fp, "     Volid = %d Pageid = %d Offset = %d,\n"
	   "     Compensate length = %d, Next_to_UNDO = %lld|%d\n",
	   compensate->data.volid, compensate->data.pageid,
	   compensate->data.offset, compensate->length,
	   (long long int) compensate->undo_nxlsa.pageid,
	   compensate->undo_nxlsa.offset);

  length_compensate = compensate->length;
  rcvindex = compensate->data.rcvindex;
  LOG_READ_ADD_ALIGN (thread_p, sizeof (*compensate), log_lsa, log_page_p);

  /* Print COMPENSATE DATA */
  fprintf (out_fp, "-->> Compensate Data:\n");
  log_dump_data (thread_p, out_fp, length_compensate, log_lsa,
		 log_page_p,
		 (RV_fun[rcvindex].dump_undofun !=
		  NULL) ? RV_fun[rcvindex].dump_undofun : log_ascii_dump,
		 NULL);

  return log_page_p;
}

static LOG_PAGE *
log_dump_record_logical_compensate (THREAD_ENTRY * thread_p, FILE * out_fp,
				    LOG_LSA * log_lsa, LOG_PAGE * log_page_p)
{
  struct log_logical_compensate *logical_comp;

  /* Read the DATA HEADER */
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*logical_comp), log_lsa,
				    log_page_p);
  logical_comp =
    ((struct log_logical_compensate *) ((char *) log_page_p->area +
					log_lsa->offset));

  fprintf (out_fp, ", Recv_index = %s,\n",
	   rv_rcvindex_string (logical_comp->rcvindex));
  fprintf (out_fp, "     Next_to_UNDO = %lld|%d\n",
	   (long long int) logical_comp->undo_nxlsa.pageid,
	   logical_comp->undo_nxlsa.offset);

  LOG_READ_ADD_ALIGN (thread_p, sizeof (*logical_comp), log_lsa, log_page_p);

  return log_page_p;
}

static LOG_PAGE *
log_dump_record_client_user_undo (THREAD_ENTRY * thread_p, FILE * out_fp,
				  LOG_LSA * log_lsa, LOG_PAGE * log_page_p)
{
  struct log_client *client_undo;
  int undo_length;
  LOG_RCVCLIENT_INDEX rcvclient_index;

  /* Read the DATA HEADER */
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*client_undo),
				    log_lsa, log_page_p);
  client_undo =
    (struct log_client *) ((char *) log_page_p->area + log_lsa->offset);
#if defined(SA_MODE)
  fprintf (out_fp, ", Recv_index = %s,\n",
	   rv_rcvcl_index_string (client_undo->rcvclient_index));
#else /* SA_MODE */
  fprintf (out_fp, ", Client Recv_index = %d,\n",
	   client_undo->rcvclient_index);
#endif /* SA_MODE */
  undo_length = client_undo->length;
  rcvclient_index = client_undo->rcvclient_index;
  LOG_READ_ADD_ALIGN (thread_p, sizeof (*client_undo), log_lsa, log_page_p);
  /* Print UNDO(BEFORE) DATA */
  fprintf (out_fp, "-->> Undo (Before) Data:\n");
#if defined(SA_MODE)
  log_dump_data (thread_p, out_fp, undo_length, log_lsa, log_page_p,
		 ((RVCL_fun[rcvclient_index].dump_undofun != NULL)
		  ? RVCL_fun[rcvclient_index].dump_undofun : log_ascii_dump),
		 NULL);
#else /* SA_MODE */
  log_dump_data (thread_p, out_fp, undo_length, log_lsa, log_page_p,
		 log_ascii_dump, NULL);
#endif /* SA_MODE */

  return log_page_p;
}

static LOG_PAGE *
log_dump_record_client_user_postpone (THREAD_ENTRY * thread_p, FILE * out_fp,
				      LOG_LSA * log_lsa,
				      LOG_PAGE * log_page_p)
{
  struct log_client *client_postpone;
  int redo_length;
  LOG_RCVCLIENT_INDEX rcvclient_index;

  /* Read the DATA HEADER */
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*client_postpone),
				    log_lsa, log_page_p);
  client_postpone =
    (struct log_client *) ((char *) log_page_p->area + log_lsa->offset);
#if defined(SA_MODE)
  fprintf (out_fp, ", Recv_index = %s,\n",
	   rv_rcvcl_index_string (client_postpone->rcvclient_index));
#else /* SA_MODE */
  fprintf (out_fp, ", Client Recv_index = %d,\n",
	   client_postpone->rcvclient_index);
#endif /* SA_MODE */
  fprintf (out_fp, "     length = %d,\n", client_postpone->length);
  redo_length = client_postpone->length;
  rcvclient_index = client_postpone->rcvclient_index;
  LOG_READ_ADD_ALIGN (thread_p, sizeof (*client_postpone), log_lsa,
		      log_page_p);

  /* Print CLIENT-USER POSTPONE REDO(AFTER) DATA */

  fprintf (out_fp, "-->> Client-User postpone Redo (After) Data:\n");
#if defined(SA_MODE)
  log_dump_data (thread_p, out_fp, redo_length, log_lsa, log_page_p,
		 ((RVCL_fun[rcvclient_index].dump_redofun != NULL)
		  ? RVCL_fun[rcvclient_index].dump_redofun : log_ascii_dump),
		 NULL);
#else /* SA_MODE */
  log_dump_data (thread_p, out_fp, redo_length, log_lsa, log_page_p,
		 log_ascii_dump, NULL);
#endif /* SA_MODE */

  return log_page_p;
}

static LOG_PAGE *
log_dump_record_next_client_undo (THREAD_ENTRY * thread_p, FILE * out_fp,
				  LOG_LSA * log_lsa, LOG_PAGE * log_page_p)
{
  struct log_run_client *run_client;

  /* Read the DATA HEADER */
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*run_client),
				    log_lsa, log_page_p);
  run_client =
    (struct log_run_client *) ((char *) log_page_p->area + log_lsa->offset);
  fprintf (out_fp, ", Next Lsa = %lld|%d \n",
	   (long long int) run_client->nxlsa.pageid,
	   run_client->nxlsa.offset);

  return log_page_p;
}

static LOG_PAGE *
log_dump_record_commit_postpone (THREAD_ENTRY * thread_p, FILE * out_fp,
				 LOG_LSA * log_lsa, LOG_PAGE * log_page_p)
{
  struct log_start_postpone *start_posp;

  /* Read the DATA HEADER */
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*start_posp), log_lsa,
				    log_page_p);
  start_posp =
    (struct log_start_postpone *) ((char *) log_page_p->area +
				   log_lsa->offset);
  fprintf (out_fp, ", First postpone record at before or after"
	   " Page = %lld and offset = %d\n",
	   (long long int) start_posp->posp_lsa.pageid,
	   start_posp->posp_lsa.offset);

  return log_page_p;
}

static LOG_PAGE *
log_dump_record_commit_client_loose_end (THREAD_ENTRY * thread_p,
					 FILE * out_fp, LOG_LSA * log_lsa,
					 LOG_PAGE * log_page_p)
{
  struct log_start_client *start_client;

  /* Read the DATA HEADER */
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*start_client),
				    log_lsa, log_page_p);
  start_client =
    (struct log_start_client *) ((char *) log_page_p->area + log_lsa->offset);
  fprintf (out_fp, ",\n     First POSTPONE CLIENT USER LOOSE END record"
	   " at Page = %lld, offset = %d\n",
	   (long long int) start_client->lsa.pageid,
	   start_client->lsa.offset);

  return log_page_p;
}

static LOG_PAGE *
log_dump_record_transaction_finish (THREAD_ENTRY * thread_p, FILE * out_fp,
				    LOG_LSA * log_lsa, LOG_PAGE * log_page_p)
{
  struct log_donetime *donetime;
  time_t tmp_time;
  char time_val[CTIME_MAX];

  /* Read the DATA HEADER */
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*donetime), log_lsa,
				    log_page_p);
  donetime =
    (struct log_donetime *) ((char *) log_page_p->area + log_lsa->offset);
  tmp_time = (time_t) donetime->at_time;
  (void) ctime_r (&tmp_time, time_val);
  fprintf (out_fp, ",\n     Transaction finish time at = %s\n", time_val);

  return log_page_p;
}

static LOG_PAGE *
log_dump_record_replication (THREAD_ENTRY * thread_p, FILE * out_fp,
			     LOG_LSA * log_lsa, LOG_PAGE * log_page_p)
{
  struct log_replication *repl_log;
  int length;
  const char *type;
  void (*dump_function) (FILE *, int, void *);

  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*repl_log), log_lsa,
				    log_page_p);
  repl_log =
    (struct log_replication *) ((char *) log_page_p->area + log_lsa->offset);
  fprintf (out_fp, ", Target log lsa = %lld|%d\n",
	   (long long int) repl_log->lsa.pageid, repl_log->lsa.offset);
  length = repl_log->length;

  LOG_READ_ADD_ALIGN (thread_p, sizeof (*repl_log), log_lsa, log_page_p);

  switch (repl_log->rcvindex)
    {
    case RVREPL_DATA_INSERT:
      type = "RVREPL_DATA_INSERT";
      dump_function = log_repl_data_dump;
      break;
    case RVREPL_DATA_UPDATE_START:
      type = "RVREPL_DATA_UPDATE_START";
      dump_function = log_repl_data_dump;
      break;
    case RVREPL_DATA_UPDATE:
      type = "RVREPL_DATA_UPDATE";
      dump_function = log_repl_data_dump;
      break;
    case RVREPL_DATA_UPDATE_END:
      type = "RVREPL_DATA_UPDATE_END";
      dump_function = log_repl_data_dump;
      break;
    case RVREPL_DATA_DELETE:
      type = "RVREPL_DATA_DELETE";
      dump_function = log_repl_data_dump;
      break;
    default:
      type = "RVREPL_SCHEMA";
      dump_function = log_repl_schema_dump;
      break;
    }
  fprintf (out_fp, "T[%s] ", type);

  log_dump_data (thread_p, out_fp, length, log_lsa, log_page_p, dump_function,
		 NULL);
  return log_page_p;
}

static LOG_PAGE *
log_dump_record_commit_topope_postpone (THREAD_ENTRY * thread_p,
					FILE * out_fp, LOG_LSA * log_lsa,
					LOG_PAGE * log_page_p)
{
  struct log_topope_start_postpone *top_start_posp;

  /* Read the DATA HEADER */
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*top_start_posp),
				    log_lsa, log_page_p);
  top_start_posp =
    ((struct log_topope_start_postpone *) ((char *) log_page_p->area +
					   log_lsa->offset));
  fprintf (out_fp, ", Lastparent_LSA = %lld|%d, First postpone_LSA"
	   " at or after = %lld|%d\n",
	   (long long int) top_start_posp->lastparent_lsa.pageid,
	   top_start_posp->lastparent_lsa.offset,
	   (long long int) top_start_posp->posp_lsa.pageid,
	   top_start_posp->posp_lsa.offset);

  return log_page_p;
}

static LOG_PAGE *
log_dump_record_commit_loose_end (THREAD_ENTRY * thread_p, FILE * out_fp,
				  LOG_LSA * log_lsa, LOG_PAGE * log_page_p)
{
  struct log_topope_start_client *top_start_client;

  /* Read the DATA HEADER */
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*top_start_client),
				    log_lsa, log_page_p);
  top_start_client =
    ((struct log_topope_start_client *) ((char *) log_page_p->area
					 + log_lsa->offset));
  fprintf (out_fp, ", Lastparent_LSA = %lld|%d, First client_postpone_LSA"
	   " at or after = %lld|%d\n",
	   (long long int) top_start_client->lastparent_lsa.pageid,
	   top_start_client->lastparent_lsa.offset,
	   (long long int) top_start_client->lsa.pageid,
	   top_start_client->lsa.offset);

  return log_page_p;
}

static LOG_PAGE *
log_dump_record_abort_loose_end (THREAD_ENTRY * thread_p, FILE * out_fp,
				 LOG_LSA * log_lsa, LOG_PAGE * log_page_p)
{
  struct log_topope_start_client *top_start_client;

  /* Read the DATA HEADER */
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*top_start_client),
				    log_lsa, log_page_p);
  top_start_client =
    ((struct log_topope_start_client *) ((char *) log_page_p->area
					 + log_lsa->offset));
  fprintf (out_fp, ", Lastparent_LSA = %lld|%d, First client_UNDO_LSA"
	   " at or after = %lld|%d\n",
	   (long long int) top_start_client->lastparent_lsa.pageid,
	   top_start_client->lastparent_lsa.offset,
	   (long long int) top_start_client->lsa.pageid,
	   top_start_client->lsa.offset);

  return log_page_p;
}

static LOG_PAGE *
log_dump_record_topope_finish (THREAD_ENTRY * thread_p, FILE * out_fp,
			       LOG_LSA * log_lsa, LOG_PAGE * log_page_p)
{
  struct log_topop_result *top_result;

  /* Read the DATA HEADER */
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*top_result),
				    log_lsa, log_page_p);
  top_result =
    ((struct log_topop_result *) ((char *) log_page_p->area +
				  log_lsa->offset));
  fprintf (out_fp, ",\n     Next UNDO at/before = %lld|%d,"
	   " Prev_topresult_lsa = %lld|%d\n",
	   (long long int) top_result->lastparent_lsa.pageid,
	   top_result->lastparent_lsa.offset,
	   (long long int) top_result->prv_topresult_lsa.pageid,
	   top_result->prv_topresult_lsa.offset);

  return log_page_p;
}

static LOG_PAGE *
log_dump_record_abort_client_loose_end (THREAD_ENTRY * thread_p,
					FILE * out_fp, LOG_LSA * log_lsa,
					LOG_PAGE * log_page_p)
{
  struct log_start_client *start_client;

  /* Read the DATA HEADER */
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*start_client),
				    log_lsa, log_page_p);
  start_client =
    (struct log_start_client *) ((char *) log_page_p->area + log_lsa->offset);
  fprintf (out_fp, ",\n     First UNDO CLIENT USER LOOSE END record"
	   " at Page = %lld, offset = %d\n",
	   (long long int) start_client->lsa.pageid,
	   start_client->lsa.offset);

  return log_page_p;
}

static LOG_PAGE *
log_dump_record_checkpoint (THREAD_ENTRY * thread_p, FILE * out_fp,
			    LOG_LSA * log_lsa, LOG_PAGE * log_page_p)
{
  struct log_chkpt *chkpt;	/* check point log record */
  int length_active_tran;
  int length_topope;

  /* Read the DATA HEADER */
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*chkpt), log_lsa,
				    log_page_p);

  chkpt = (struct log_chkpt *) ((char *) log_page_p->area + log_lsa->offset);
  fprintf (out_fp, ", Num_trans = %d,\n", chkpt->ntrans);
  fprintf (out_fp, "     Redo_LSA = %lld|%d\n",
	   (long long int) chkpt->redo_lsa.pageid, chkpt->redo_lsa.offset);

  length_active_tran = sizeof (struct log_chkpt_trans) * chkpt->ntrans;
  length_topope =
    (sizeof (struct log_chkpt_topops_commit_posp) * chkpt->ntops);
  LOG_READ_ADD_ALIGN (thread_p, sizeof (*chkpt), log_lsa, log_page_p);
  log_dump_data (thread_p, out_fp, length_active_tran, log_lsa,
		 log_page_p, logpb_dump_checkpoint_trans, NULL);
  if (length_topope > 0)
    {
      log_dump_data (thread_p, out_fp, length_active_tran, log_lsa,
		     log_page_p, logpb_dump_checkpoint_topops, NULL);
    }

  return log_page_p;
}

static LOG_PAGE *
log_dump_record_save_point (THREAD_ENTRY * thread_p, FILE * out_fp,
			    LOG_LSA * log_lsa, LOG_PAGE * log_page_p)
{
  struct log_savept *savept;
  int length_save_point;

  /* Read the DATA HEADER */
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*savept), log_lsa,
				    log_page_p);
  savept =
    (struct log_savept *) ((char *) log_page_p->area + log_lsa->offset);

  fprintf (out_fp, ", Prev_savept_Lsa = %lld|%d, length = %d,\n",
	   (long long int) savept->prv_savept.pageid,
	   savept->prv_savept.offset, savept->length);

  length_save_point = savept->length;
  LOG_READ_ADD_ALIGN (thread_p, sizeof (*savept), log_lsa, log_page_p);

  /* Print savept name */
  fprintf (out_fp, "     Savept Name =");
  log_dump_data (thread_p, out_fp, length_save_point, log_lsa,
		 log_page_p, log_ascii_dump, NULL);

  return log_page_p;
}

static LOG_PAGE *
log_dump_record_2pc_prepare_commit (THREAD_ENTRY * thread_p, FILE * out_fp,
				    LOG_LSA * log_lsa, LOG_PAGE * log_page_p)
{
  struct log_2pc_prepcommit *prepared;
  unsigned int nobj_locks;
  int size;

  /* Get the DATA HEADER */
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*prepared), log_lsa,
				    log_page_p);
  prepared =
    (struct log_2pc_prepcommit *) ((char *) log_page_p->area +
				   log_lsa->offset);

  fprintf (out_fp,
	   ", Client_name = %s, Gtrid = %d, Num objlocks = %u\n",
	   prepared->user_name, prepared->gtrid, prepared->num_object_locks);

  nobj_locks = prepared->num_object_locks;

  LOG_READ_ADD_ALIGN (thread_p, sizeof (*prepared), log_lsa, log_page_p);

  /* Dump global transaction user information */
  if (prepared->gtrinfo_length > 0)
    {
      log_dump_data (thread_p, out_fp, prepared->gtrinfo_length, log_lsa,
		     log_page_p, log_2pc_dump_gtrinfo, NULL);
    }

  /* Dump object locks */
  if (nobj_locks > 0)
    {
      size = nobj_locks * sizeof (LK_ACQOBJ_LOCK);
      log_dump_data (thread_p, out_fp, size, log_lsa, log_page_p,
		     log_2pc_dump_acqobj_locks, NULL);
    }

  return log_page_p;
}

static LOG_PAGE *
log_dump_record_2pc_start (THREAD_ENTRY * thread_p, FILE * out_fp,
			   LOG_LSA * log_lsa, LOG_PAGE * log_page_p)
{
  struct log_2pc_start *start_2pc;	/* Start log record of 2PC protocol */

  /* Get the DATA HEADER */
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*start_2pc), log_lsa,
				    log_page_p);
  start_2pc =
    (struct log_2pc_start *) ((char *) log_page_p->area + log_lsa->offset);

  /* Initilize the coordinator information */
  fprintf (out_fp, "  Client_name = %s, Gtrid = %d, "
	   " Num_participants = %d",
	   start_2pc->user_name, start_2pc->gtrid, start_2pc->num_particps);

  LOG_READ_ADD_ALIGN (thread_p, sizeof (*start_2pc), log_lsa, log_page_p);
  /* Read in the participants info. block from the log */
  log_dump_data (thread_p, out_fp, (start_2pc->particp_id_length *
				    start_2pc->num_particps),
		 log_lsa, log_page_p, log_2pc_dump_participants, NULL);

  return log_page_p;
}

static LOG_PAGE *
log_dump_record_2pc_acknowledgement (THREAD_ENTRY * thread_p, FILE * out_fp,
				     LOG_LSA * log_lsa, LOG_PAGE * log_page_p)
{
  struct log_2pc_particp_ack *received_ack;	/* ack log record of 2pc protocol */

  /* Get the DATA HEADER */
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*received_ack),
				    log_lsa, log_page_p);
  received_ack =
    ((struct log_2pc_particp_ack *) ((char *) log_page_p->area
				     + log_lsa->offset));
  fprintf (out_fp, "  Participant index = %d\n", received_ack->particp_index);

  return log_page_p;
}

static LOG_PAGE *
log_dump_record_ha_server_state (THREAD_ENTRY * thread_p, FILE * out_fp,
				 LOG_LSA * log_lsa, LOG_PAGE * log_page_p)
{
  struct log_ha_server_state *ha_server_state;

  /* Get the DATA HEADER */
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*ha_server_state),
				    log_lsa, log_page_p);
  ha_server_state =
    ((struct log_ha_server_state *) ((char *) log_page_p->area
				     + log_lsa->offset));
  fprintf (out_fp, "  HA server state = %d\n", ha_server_state->state);

  return log_page_p;
}

static LOG_PAGE *
log_dump_record (THREAD_ENTRY * thread_p, FILE * out_fp,
		 LOG_RECTYPE record_type, LOG_LSA * log_lsa,
		 LOG_PAGE * log_page_p, LOG_ZIP * log_zip_p)
{
  switch (record_type)
    {
    case LOG_CLIENT_NAME:
      log_page_p =
	log_dump_record_client_name (thread_p, out_fp, log_lsa, log_page_p);
      break;

    case LOG_UNDOREDO_DATA:
    case LOG_DIFF_UNDOREDO_DATA:
      log_page_p =
	log_dump_record_undoredo (thread_p, out_fp, log_lsa, log_page_p,
				  log_zip_p);
      break;

    case LOG_UNDO_DATA:
      log_page_p =
	log_dump_record_undo (thread_p, out_fp, log_lsa, log_page_p,
			      log_zip_p);
      break;

    case LOG_REDO_DATA:
    case LOG_POSTPONE:
      log_page_p =
	log_dump_record_redo (thread_p, out_fp, log_lsa, log_page_p,
			      log_zip_p);
      break;

    case LOG_MVCC_UNDOREDO_DATA:
    case LOG_MVCC_DIFF_UNDOREDO_DATA:
      log_page_p =
	log_dump_record_mvcc_undoredo (thread_p, out_fp, log_lsa, log_page_p,
				       log_zip_p);
      break;

    case LOG_MVCC_UNDO_DATA:
      log_page_p =
	log_dump_record_mvcc_undo (thread_p, out_fp, log_lsa, log_page_p,
				   log_zip_p);
      break;

    case LOG_MVCC_REDO_DATA:
      log_page_p =
	log_dump_record_mvcc_redo (thread_p, out_fp, log_lsa, log_page_p,
				   log_zip_p);
      break;

    case LOG_RUN_POSTPONE:
      log_page_p =
	log_dump_record_postpone (thread_p, out_fp, log_lsa, log_page_p);
      break;

    case LOG_DBEXTERN_REDO_DATA:
      log_page_p =
	log_dump_record_dbout_redo (thread_p, out_fp, log_lsa, log_page_p);
      break;

    case LOG_COMPENSATE:
      log_page_p =
	log_dump_record_compensate (thread_p, out_fp, log_lsa, log_page_p);
      break;

    case LOG_LCOMPENSATE:
      log_page_p =
	log_dump_record_logical_compensate (thread_p, out_fp, log_lsa,
					    log_page_p);
      break;

    case LOG_CLIENT_USER_UNDO_DATA:
      log_page_p =
	log_dump_record_client_user_undo (thread_p, out_fp, log_lsa,
					  log_page_p);
      break;

    case LOG_CLIENT_USER_POSTPONE_DATA:
      log_page_p =
	log_dump_record_client_user_postpone (thread_p, out_fp, log_lsa,
					      log_page_p);
      break;

    case LOG_RUN_NEXT_CLIENT_UNDO:
    case LOG_RUN_NEXT_CLIENT_POSTPONE:
      log_page_p =
	log_dump_record_next_client_undo (thread_p, out_fp, log_lsa,
					  log_page_p);
      break;

    case LOG_COMMIT_WITH_POSTPONE:
      log_page_p =
	log_dump_record_commit_postpone (thread_p, out_fp, log_lsa,
					 log_page_p);
      break;

    case LOG_COMMIT_WITH_CLIENT_USER_LOOSE_ENDS:
      log_page_p =
	log_dump_record_commit_client_loose_end (thread_p, out_fp, log_lsa,
						 log_page_p);
      break;

    case LOG_WILL_COMMIT:
      fprintf (stdout, "\n");
      break;

    case LOG_COMMIT:
    case LOG_ABORT:
      log_page_p =
	log_dump_record_transaction_finish (thread_p, out_fp, log_lsa,
					    log_page_p);
      break;

    case LOG_REPLICATION_DATA:
    case LOG_REPLICATION_SCHEMA:
      log_page_p =
	log_dump_record_replication (thread_p, out_fp, log_lsa, log_page_p);
      break;

    case LOG_COMMIT_TOPOPE_WITH_POSTPONE:
      log_page_p =
	log_dump_record_commit_topope_postpone (thread_p, out_fp, log_lsa,
						log_page_p);
      break;

    case LOG_COMMIT_TOPOPE_WITH_CLIENT_USER_LOOSE_ENDS:
      log_page_p =
	log_dump_record_commit_loose_end (thread_p, out_fp, log_lsa,
					  log_page_p);
      break;

    case LOG_ABORT_TOPOPE_WITH_CLIENT_USER_LOOSE_ENDS:
      log_page_p =
	log_dump_record_abort_loose_end (thread_p, out_fp, log_lsa,
					 log_page_p);
      break;

    case LOG_COMMIT_TOPOPE:
    case LOG_ABORT_TOPOPE:
      log_page_p =
	log_dump_record_topope_finish (thread_p, out_fp, log_lsa, log_page_p);
      break;

    case LOG_ABORT_WITH_CLIENT_USER_LOOSE_ENDS:
      log_page_p =
	log_dump_record_abort_client_loose_end (thread_p, out_fp, log_lsa,
						log_page_p);
      break;

    case LOG_END_CHKPT:
      log_page_p =
	log_dump_record_checkpoint (thread_p, out_fp, log_lsa, log_page_p);
      break;

    case LOG_SAVEPOINT:
      log_page_p =
	log_dump_record_save_point (thread_p, out_fp, log_lsa, log_page_p);
      break;

    case LOG_2PC_PREPARE:
      log_page_p =
	log_dump_record_2pc_prepare_commit (thread_p, out_fp, log_lsa,
					    log_page_p);
      break;

    case LOG_2PC_START:
      log_page_p =
	log_dump_record_2pc_start (thread_p, out_fp, log_lsa, log_page_p);
      break;

    case LOG_2PC_RECV_ACK:
      log_page_p =
	log_dump_record_2pc_acknowledgement (thread_p, out_fp, log_lsa,
					     log_page_p);
      break;

    case LOG_DUMMY_HA_SERVER_STATE:
      log_page_p =
	log_dump_record_ha_server_state (thread_p, out_fp, log_lsa,
					 log_page_p);
      break;

    case LOG_START_CHKPT:
    case LOG_2PC_COMMIT_DECISION:
    case LOG_2PC_ABORT_DECISION:
    case LOG_2PC_COMMIT_INFORM_PARTICPS:
    case LOG_2PC_ABORT_INFORM_PARTICPS:
    case LOG_DUMMY_HEAD_POSTPONE:
    case LOG_DUMMY_CRASH_RECOVERY:
    case LOG_DUMMY_FILLPAGE_FORARCHIVE:	/* for backward compatibility */
    case LOG_DUMMY_OVF_RECORD:
    case LOG_UNLOCK_COMMIT:
    case LOG_UNLOCK_ABORT:
      fprintf (stdout, "\n");
      /* That is all for this kind of log record */
      break;

    case LOG_END_OF_LOG:
      if (!logpb_is_page_in_archive (log_lsa->pageid))
	{
	  fprintf (stdout, "\n... xxx END OF LOG xxx ...\n");
	}
      break;

    case LOG_SMALLER_LOGREC_TYPE:
    case LOG_LARGER_LOGREC_TYPE:
    default:
      fprintf (stdout, "log_dump: Unknown record type = %d (%s).\n",
	       record_type, log_to_string (record_type));
      LSA_SET_NULL (log_lsa);
      break;
    }

  return log_page_p;
}

/*
 * xlog_dump - DUMP THE LOG
 *
 * return: nothing
 *
 *   isforward(in): Dump the log forward ?
 *   start_logpageid(in): Start dumping the log at this location
 *   dump_npages(in): Number of pages to dump
 *   desired_tranid(in): Dump entries of only this transaction. If NULL_TRANID,
 *                     dump all.
 *
 * NOTE: Dump a set of log records stored in "dump_npages" starting at
 *              page "start_logpageid" forward (or backward) according to the
 *              value of "isforward". When the value of start_logpageid is
 *              negative, we start either at the beginning or at end of the
 *              log according to the direction of the dump. If the value of
 *              dump_npages is a negative value, dump as many pages as
 *              possible.
 *              This function is used for debugging purposes.
 */
void
xlog_dump (THREAD_ENTRY * thread_p, FILE * out_fp, int isforward,
	   LOG_PAGEID start_logpageid, DKNPAGES dump_npages,
	   TRANID desired_tranid)
{
  LOG_LSA lsa;			/* LSA of log record to dump */
  char log_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT], *aligned_log_pgbuf;
  LOG_PAGE *log_pgptr = NULL;	/* Log page pointer where
				 * LSA is located
				 */
  LOG_LSA log_lsa;
  LOG_RECTYPE type;		/* Log record type           */
  LOG_RECORD_HEADER *log_rec;	/* Pointer to log record     */

  LOG_ZIP *log_dump_ptr = NULL;

  aligned_log_pgbuf = PTR_ALIGN (log_pgbuf, MAX_ALIGNMENT);

  if (out_fp == NULL)
    {
      out_fp = stdout;
    }

  fprintf (out_fp,
	   "**************** DUMP LOGGING INFORMATION ************\n");
  /* Dump the transaction table and the log buffers */

  /* Flush any dirty log page */
  LOG_CS_ENTER (thread_p);

  xlogtb_dump_trantable (thread_p, out_fp);
  logpb_dump (out_fp);
  logpb_flush_pages_direct (thread_p);
  logpb_flush_header (thread_p);

  /* Now start dumping the log */
  log_dump_header (out_fp, &log_Gl.hdr);

  lsa.pageid = start_logpageid;
  lsa.offset = NULL_OFFSET;

  if (isforward != false)
    {
      /* Forward */
      if (lsa.pageid < 0)
	{
	  lsa.pageid = 0;
	}
      else if (lsa.pageid > log_Gl.hdr.append_lsa.pageid
	       && LOG_ISRESTARTED ())
	{
	  lsa.pageid = log_Gl.hdr.append_lsa.pageid;
	}
    }
  else
    {
      /* Backward */
      if (lsa.pageid < 0 || lsa.pageid > log_Gl.hdr.append_lsa.pageid)
	{
	  log_find_end_log (thread_p, &lsa);
	}
    }

  if (dump_npages > log_Gl.hdr.npages || dump_npages < 0)
    {
      dump_npages = log_Gl.hdr.npages;
    }

  fprintf (out_fp,
	   "\n START DUMPING LOG_RECORDS: %s, start_logpageid = %lld,\n"
	   " Num_pages_to_dump = %d, desired_tranid = %d\n",
	   (isforward ? "Forward" : "Backaward"),
	   (long long int) start_logpageid, dump_npages, desired_tranid);

  LOG_CS_EXIT (thread_p);

  log_pgptr = (LOG_PAGE *) aligned_log_pgbuf;

  if (log_dump_ptr == NULL)
    {
      log_dump_ptr = log_zip_alloc (IO_PAGESIZE, false);
      if (log_dump_ptr == NULL)
	{
	  fprintf (out_fp, " Error memory alloc... Quit\n");
	  return;
	}
    }

  /* Start dumping all log records following the given direction */
  while (!LSA_ISNULL (&lsa) && dump_npages-- > 0)
    {
      if ((logpb_fetch_page (thread_p, lsa.pageid, log_pgptr)) == NULL)
	{
	  fprintf (out_fp, " Error reading page %lld... Quit\n",
		   (long long int) lsa.pageid);
	  if (log_dump_ptr != NULL)
	    {
	      log_zip_free (log_dump_ptr);
	    }
	  return;
	}
      /*
       * If offset is missing, it is because we archive an incomplete
       * log record or we start dumping the log not from its first page. We
       * have to find the offset by searching for the next log_record in the page
       */
      if (lsa.offset == NULL_OFFSET
	  && (lsa.offset = log_pgptr->hdr.offset) == NULL_OFFSET)
	{
	  /* Nothing in this page.. */
	  if (lsa.pageid >= log_Gl.hdr.append_lsa.pageid || lsa.pageid <= 0)
	    {
	      LSA_SET_NULL (&lsa);
	    }
	  else
	    {
	      /* We need to dump one more page */
	      lsa.pageid--;
	      dump_npages++;
	    }
	  continue;
	}

      /* Dump all the log records stored in current log page */
      log_lsa.pageid = lsa.pageid;

      while (lsa.pageid == log_lsa.pageid)
	{
	  log_lsa.offset = lsa.offset;
	  log_rec = LOG_GET_LOG_RECORD_HEADER (log_pgptr, &log_lsa);
	  type = log_rec->type;

	  {
	    /*
	     * The following is just for debugging next address calculations
	     */
	    LOG_LSA next_lsa;

	    LSA_COPY (&next_lsa, &lsa);
	    if (log_startof_nxrec (thread_p, &next_lsa, false) == NULL
		|| (!LSA_EQ (&next_lsa, &log_rec->forw_lsa)
		    && !LSA_ISNULL (&log_rec->forw_lsa)))
	      {
		fprintf (out_fp, "\n\n>>>>>****\n");
		fprintf (out_fp,
			 "Guess next address = %lld|%d for LSA = %lld|%d\n",
			 (long long int) next_lsa.pageid, next_lsa.offset,
			 (long long int) lsa.pageid, lsa.offset);
		fprintf (out_fp, "<<<<<****\n");
	      }
	  }

	  /* Find the next log record to dump .. after current one is dumped */
	  if (isforward != false)
	    {
	      if (LSA_ISNULL (&log_rec->forw_lsa) && type != LOG_END_OF_LOG)
		{
		  if (log_startof_nxrec (thread_p, &lsa, false) == NULL)
		    {
		      fprintf (out_fp, "\n****\n");
		      fprintf (out_fp,
			       "log_dump: Problems finding next record. BYE\n");
		      fprintf (out_fp, "\n****\n");
		      break;
		    }
		}
	      else
		{
		  LSA_COPY (&lsa, &log_rec->forw_lsa);
		}
	      /*
	       * If the next page is NULL_PAGEID and the current page is an archive
	       * page, this is not the end, this situation happens when an incomplete
	       * log record was archived.
	       * Note that we have to set lsa.pageid here since the log_lsa.pageid value
	       * can be changed (e.g., the log record is stored in an archive page
	       * and in an active page. Later, we try to modify it whenever is
	       * possible.
	       */
	      if (LSA_ISNULL (&lsa)
		  && logpb_is_page_in_archive (log_lsa.pageid))
		{
		  lsa.pageid = log_lsa.pageid + 1;
		}
	    }
	  else
	    {
	      LSA_COPY (&lsa, &log_rec->back_lsa);
	    }

	  if (desired_tranid != NULL_TRANID
	      && desired_tranid != log_rec->trid
	      && log_rec->type != LOG_END_OF_LOG)
	    {
	      /* Don't dump this log record... */
	      continue;
	    }

	  fprintf (out_fp, "\nLSA = %3lld|%3d, Forw log = %3lld|%3d,"
		   " Backw log = %3lld|%3d,\n"
		   "     Trid = %3d, Prev tran logrec = %3lld|%3d\n"
		   "     Type = %s",
		   (long long int) log_lsa.pageid, log_lsa.offset,
		   (long long int) log_rec->forw_lsa.pageid,
		   log_rec->forw_lsa.offset,
		   (long long int) log_rec->back_lsa.pageid,
		   log_rec->back_lsa.offset,
		   log_rec->trid,
		   (long long int) log_rec->prev_tranlsa.pageid,
		   log_rec->prev_tranlsa.offset, log_to_string (type));

	  if (LSA_ISNULL (&log_rec->forw_lsa) && type != LOG_END_OF_LOG)
	    {
	      /*
	       * This record is not generated no more.
	       * It's kept for backward compatibility.
	       */
	      if (type != LOG_DUMMY_FILLPAGE_FORARCHIVE)
		{
		  /* Incomplete log record... quit */
		  fprintf (out_fp, "\n****\n");
		  fprintf (out_fp,
			   "log_dump: Incomplete log_record.. Quit\n");
		  fprintf (out_fp, "\n****\n");
		}
	      continue;
	    }

	  /* Advance the pointer to dump the type of log record */

	  LOG_READ_ADD_ALIGN (thread_p, sizeof (*log_rec), &log_lsa,
			      log_pgptr);
	  log_pgptr = log_dump_record (thread_p, stdout, type, &log_lsa,
				       log_pgptr, log_dump_ptr);
	  fflush (stdout);
	  /*
	   * We can fix the lsa.pageid in the case of log_records without forward
	   * address at this moment.
	   */
	  if (lsa.offset == NULL_OFFSET && lsa.pageid != NULL_PAGEID
	      && lsa.pageid < log_lsa.pageid)
	    {
	      lsa.pageid = log_lsa.pageid;
	    }
	}
    }

  if (log_dump_ptr)
    {
      log_zip_free (log_dump_ptr);
    }

  fprintf (out_fp, "\n FINISH DUMPING LOG_RECORDS \n");
  fprintf (out_fp,
	   "******************************************************\n");
  fflush (out_fp);

  return;
}

/*
 *
 *                     RECOVERY DURING NORMAL PROCESSING
 *
 */

/*
 * log_rollback_record - EXECUTE AN UNDO DURING NORMAL PROCESSING
 *
 * return: nothing
 *
 *   log_lsa(in/out):Log address identifier containing the log record
 *   log_pgptr(in/out): Pointer to page where data starts (Set as a side
 *              effect to the page where data ends)
 *   rcvindex(in): Index to recovery functions
 *   rcv_vpid(in): Address of page to recover
 *   rcv(in/out): Recovery structure for recovery function
 *   tdes(in/out): State structure of transaction undoing data
 *   log_unzip_ptr(in):
 *
 * NOTE: Execute an undo log record during normal rollbacks (i.e.,
 *              other than restart recovery). A compensating log record for
 *              operation page level logging is written by the current
 *              function. For logical level logging, the undo function is
 *              responsible to log a redo record, which is converted into a
 *              compensating record by the log manager.
 *              This function now attempts to repeat an rv function if it
 *              fails in certain ways (e.g. due to deadlock).  This is to
 *              maintain data integrity as much as possible.  The old way was
 *              to simply ignore a failure and continue with the next record,
 *              Obviously, skipping a record during recover could leave the
 *              database inconsistent. All rv functions should return a
 *              int and be coded to be called again if the work wasn't
 *              undone the first time.
 */
static void
log_rollback_record (THREAD_ENTRY * thread_p, LOG_LSA * log_lsa,
		     LOG_PAGE * log_page_p, LOG_RCVINDEX rcvindex,
		     VPID * rcv_vpid, LOG_RCV * rcv, LOG_TDES * tdes,
		     LOG_ZIP * log_unzip_ptr)
{
  char *area = NULL;
  LOG_LSA logical_undo_nxlsa;
  TRAN_STATE save_state;	/* The current state of the transaction. Must be
				 * returned to this state
				 */
  int rv_err;
  bool is_zipped = false;

  /*
   * Fetch the page for physical log records. If the page does not exist
   * anymore or there are problems fetching the page, continue anyhow, so that
   * compensating records are logged.
   */

#if defined(CUBRID_DEBUG)
  if (RV_fun[rcvindex].undofun == NULL)
    {
      assert (false);
      return;
    }
#endif /* CUBRID_DEBUG */

  if (RCV_IS_LOGICAL_LOG (rcv_vpid, rcvindex)
      || (disk_isvalid_page (thread_p, rcv_vpid->volid,
			     rcv_vpid->pageid) != DISK_VALID))
    {
      rcv->pgptr = NULL;
    }
  else
    {
      rcv->pgptr = pgbuf_fix (thread_p, rcv_vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			      PGBUF_UNCONDITIONAL_LATCH);
    }

  /* GET BEFORE DATA */

  /*
   * If data is contained in only one buffer, pass pointer directly.
   * Otherwise, allocate a contiguous area, copy the data and pass this area.
   * At the end deallocate the area.
   */

  if (ZIP_CHECK (rcv->length))
    {				/* check compress data */
      rcv->length = (int) GET_ZIP_LEN (rcv->length);	/* MSB set 0   */
      is_zipped = true;
    }

  if (log_lsa->offset + rcv->length < (int) LOGAREA_SIZE)
    {
      rcv->data = (char *) log_page_p->area + log_lsa->offset;
      log_lsa->offset += rcv->length;
    }
  else
    {
      /* Need to copy the data into a contiguous area */
      area = (char *) malloc (rcv->length);
      if (area == NULL)
	{
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			     "log_rollback_record");
	  if (rcv->pgptr != NULL)
	    {
	      pgbuf_unfix (thread_p, rcv->pgptr);
	    }
	  return;
	}
      /* Copy the data */
      logpb_copy_from_log (thread_p, area, rcv->length, log_lsa, log_page_p);
      rcv->data = area;
    }

  if (is_zipped)
    {
      /* Data UnZip */
      if (log_unzip (log_unzip_ptr, rcv->length, (char *) rcv->data))
	{
	  rcv->length = (int) log_unzip_ptr->data_length;
	  rcv->data = (char *) log_unzip_ptr->log_data;
	}
      else
	{
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			     "log_rollback_record");
	  if (area != NULL)
	    {
	      free_and_init (area);
	    }
	  if (rcv->pgptr != NULL)
	    {
	      pgbuf_unfix (thread_p, rcv->pgptr);
	    }
	  return;
	}
    }

  /* Now call the UNDO recovery function */
  if (rcv->pgptr != NULL || RCV_IS_LOGICAL_LOG (rcv_vpid, rcvindex))
    {
      /*
       * Write a compensating log record for operation page level logging.
       * For logical level logging, the recovery undo function must log an
       * redo/CLR log to describe the undo. This in turn will be transalated
       * to a compensating record.
       */
      if (rcvindex == RVVAC_DROPPED_FILE_ADD)
	{
	  rv_err = vacuum_notify_dropped_file (thread_p, rcv, NULL);
	}
      else if (!RCV_IS_LOGICAL_LOG (rcv_vpid, rcvindex))
	{
	  log_append_compensate (thread_p, rcvindex, rcv_vpid,
				 rcv->offset, rcv->pgptr, rcv->length,
				 rcv->data, tdes);
	  /* Invoke Undo recovery function */
	  rv_err = log_undo_rec_restartable (thread_p, rcvindex, rcv);
	}
      else
	{
	  /*
	   * Logical logging. The undo function is responsible for logging the
	   * needed undo and redo records to make the logical undo operation
	   * atomic.
	   * The recovery manager sets a dummy compensating record, to fix the
	   * undo_nxlsa record at crash recovery time.
	   */
	  LSA_COPY (&logical_undo_nxlsa, &tdes->undo_nxlsa);
	  save_state = tdes->state;

	  /*
	   * A system operation is needed since the postpone operations of an
	   * undo log must be done at the end of the logical undo. Without this
	   * if there is a crash, we will be in trouble since we will not be able
	   * to undo a postpone operation.
	   */
	  if (log_start_system_op (thread_p) == NULL)
	    {
	      logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
				 "log_rollback_record");
	      if (area != NULL)
		{
		  free_and_init (area);
		}
	      if (rcv->pgptr != NULL)
		{
		  pgbuf_unfix (thread_p, rcv->pgptr);
		}
	      return;
	    }

#if defined(CUBRID_DEBUG)
	  {
	    LOG_LSA check_tail_lsa;

	    LSA_COPY (&check_tail_lsa, &tdes->tail_lsa);
	    /*
	     * Note that tail_lsa is changed by the following function
	     */
	    /* Invoke Undo recovery function */
	    rv_err = log_undo_rec_restartable (rcvindex, rcv);

	    /* Make sure that a CLR was logged */
	    if (LSA_EQ (&check_tail_lsa, &tdes->tail_lsa)
		&& rcvindex != RVFL_CREATE_TMPFILE)
	      {
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			ER_LOG_MISSING_COMPENSATING_RECORD, 1,
			rv_rcvindex_string (rcvindex));
	      }
	  }
#else /* CUBRID_DEBUG */
	  /* Invoke Undo recovery function */
	  rv_err = log_undo_rec_restartable (thread_p, rcvindex, rcv);
#endif /* CUBRID_DEBUG */

	  if (rv_err != NO_ERROR)
	    {
	      er_log_debug (ARG_FILE_LINE,
			    "log_rollback_record: SYSTEM ERROR... Transaction %d, "
			    "Log record %lld|%d, rcvindex = %s, "
			    "was not undone due to error (%d)\n",
			    tdes->tran_index, (long long int) log_lsa->pageid,
			    log_lsa->offset, rv_rcvindex_string (rcvindex),
			    rv_err);
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_LOG_MAYNEED_MEDIA_RECOVERY, 1,
		      fileio_get_volume_label (rcv_vpid->volid, PEEK));
	    }

	  log_end_system_op (thread_p, LOG_RESULT_TOPOP_COMMIT);
	  tdes->state = save_state;
	  /*
	   * Now add the dummy logical compensating record. This mark the end of
	   * the logical operation.
	   */
	  log_append_logical_compensate (thread_p, rcvindex, tdes,
					 &logical_undo_nxlsa);
	}
    }
  else
    {
      /*
       * Unable to fetch page of volume... May need media recovery on such
       * page... write a CLR anyhow
       */
      log_append_compensate (thread_p, rcvindex, rcv_vpid,
			     rcv->offset, NULL, rcv->length, rcv->data, tdes);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_MAYNEED_MEDIA_RECOVERY,
	      1, fileio_get_volume_label (rcv_vpid->volid, PEEK));
    }

  if (area != NULL)
    {
      free_and_init (area);
    }

  if (rcv->pgptr != NULL)
    {
      pgbuf_unfix (thread_p, rcv->pgptr);
    }
}

/*
 * log_undo_rec_restartable - Rollback a single undo record w/ restart
 *
 * return: nothing
 *
 *   rcvindex(in): Index to recovery functions
 *   rcv(in/out): Recovery structure for recovery function
 *
 * NOTE: Perform the undo of a singe log record. Even though it would
 *              indicate a serious problem in the design, check for deadlock
 *              and timeout to make sure this log record was truly undone.
 *              Continue to retry the log undo if possible.
 *      CAVEAT: This attempt to retry in the case of failure assumes that the
 *              rcvindex undo function we invoke has no partial side-effects
 *              for the case where it fails. Otherwise restarting it would not
 *              be a very smart thing to do.
 */
static int
log_undo_rec_restartable (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex,
			  LOG_RCV * rcv)
{
  int num_retries = 0;		/* Avoid infinite loop */
  int error_code = NO_ERROR;

  do
    {
      if (error_code != NO_ERROR)
	{
#if defined(CUBRID_DEBUG)
	  er_log_debug (ARG_FILE_LINE,
			"WARNING: RETRY DURING UNDO WAS NEEDED ... TranIndex: %d, Cnt = %d, Err = %d, Rcvindex = %s\n",
			LOG_FIND_THREAD_TRAN_INDEX (thread_p), num_retries,
			error_code, rv_rcvindex_string (rcvindex));
#endif /* CUBRID_DEBUG */
	}
      error_code = (*RV_fun[rcvindex].undofun) (thread_p, rcv);
    }
  while (++num_retries <= LOG_REC_UNDO_MAX_ATTEMPTS
	 && (error_code == ER_LK_PAGE_TIMEOUT
	     || error_code == ER_LK_UNILATERALLY_ABORTED));

  return error_code;
}

/*
 * log_dump_record_header_to_string - dump log record header to string
 *
 * return: nothing
 *
 *   log(in): log record header pointer
 *   buf(out): char buffer pointer
 *   len(in): max size of the buffer
 *
 */
static void
log_dump_record_header_to_string (LOG_RECORD_HEADER * log, char *buf,
				  size_t len)
{
  const char *fmt =
    "TYPE[%d], TRID[%d], PREV[%lld,%d], BACK[%lld,%d], FORW[%lld,%d]";

  snprintf (buf, len, fmt, log->type, log->trid,
	    (long long int) log->prev_tranlsa.pageid,
	    log->prev_tranlsa.offset, (long long int) log->back_lsa.pageid,
	    log->back_lsa.offset, (long long int) log->forw_lsa.pageid,
	    log->forw_lsa.offset);
}

/*
 * log_rollback - Rollback a transaction
 *
 * return: nothing
 *
 *   tdes(in): Transaction descriptor
 *   upto_lsa_ptr(in): Rollback up to this log sequence address
 *
 * NOTE:Rollback the transaction associated with the given tdes
 *              structure upto the given lsa. If LSA is NULL, the transaction
 *              is completely rolled back. This function is used for aborts
 *              related no to database crashes.
 */
static void
log_rollback (THREAD_ENTRY * thread_p, LOG_TDES * tdes,
	      const LOG_LSA * upto_lsa_ptr)
{
  LOG_LSA prev_tranlsa;		/* Previous LSA */
  LOG_LSA upto_lsa;		/* copy of upto_lsa_ptr contents */
  char log_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT], *aligned_log_pgbuf;
  LOG_PAGE *log_pgptr = NULL;	/* Log page pointer of LSA log record */
  LOG_LSA log_lsa;
  LOG_RECORD_HEADER *log_rec = NULL;	/* The log record */
  struct log_undoredo *undoredo = NULL;	/* An undoredo log record */
  struct log_mvcc_undoredo *mvcc_undoredo = NULL;	/* A MVCC undoredo log rec */
  struct log_undo *undo = NULL;	/* An undo log record */
  struct log_mvcc_undo *mvcc_undo = NULL;	/* An undo log record */
  struct log_compensate *compensate = NULL;	/* A compensating log record */
  struct log_logical_compensate *logical_comp = NULL;	/* end of a logical undo */
  struct log_topop_result *top_result = NULL;	/* Partial result from top system
						 * operation
						 */
  LOG_RCV rcv;			/* Recovery structure */
  VPID rcv_vpid;		/* VPID of data to recover */
  LOG_RCVINDEX rcvindex;	/* Recovery index */
  bool isdone;
  int old_wait_msecs = 0;	/* Old transaction lock wait */
  LOG_ZIP *log_unzip_ptr = NULL;
  int data_header_size = 0;
  bool is_mvcc_op = false;


  aligned_log_pgbuf = PTR_ALIGN (log_pgbuf, MAX_ALIGNMENT);

  /*
   * Execute every single undo log record upto the given upto_lsa_ptr since it
   * is not a system crash
   */

  if (LSA_ISNULL (&tdes->tail_lsa))
    {
      /* Nothing to undo */
      return;
    }

  /*
   * I should not timeout on a page that I need to undo, otherwise, I may
   * end up with database corruption problems. That is, no timeouts during
   * rollback.
   */
  old_wait_msecs =
    xlogtb_reset_wait_msecs (thread_p, TRAN_LOCK_INFINITE_WAIT);

  LSA_COPY (&prev_tranlsa, &tdes->undo_nxlsa);
  /*
   * In some cases what upto_lsa_ptr points to is volatile, e.g.
   * when it is from the topops stack (which can be reallocated by
   * operations during this rollback).
   */
  if (upto_lsa_ptr != NULL)
    {
      LSA_COPY (&upto_lsa, upto_lsa_ptr);
    }
  else
    {
      LSA_SET_NULL (&upto_lsa);
    }

  log_pgptr = (LOG_PAGE *) aligned_log_pgbuf;

  isdone = false;

  log_unzip_ptr = log_zip_alloc (IO_PAGESIZE, false);

  if (log_unzip_ptr == NULL)
    {
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_rollback");
      return;
    }

  while (!LSA_ISNULL (&prev_tranlsa) && !isdone)
    {
      /* Fetch the page where the LSA record to undo is located */
      log_lsa.pageid = prev_tranlsa.pageid;

      if ((logpb_fetch_page (thread_p, log_lsa.pageid, log_pgptr)) == NULL)
	{
	  (void) xlogtb_reset_wait_msecs (thread_p, old_wait_msecs);
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_rollback");
	  if (log_unzip_ptr != NULL)
	    {
	      log_zip_free (log_unzip_ptr);
	    }
	  return;
	}

      while (prev_tranlsa.pageid == log_lsa.pageid)
	{
	  /* Break at upto_lsa for partial rollbacks */
	  if (upto_lsa_ptr != NULL && LSA_LE (&prev_tranlsa, &upto_lsa))
	    {
	      /* Finish at this point */
	      isdone = true;
	      break;
	    }

	  /* Find the log record to undo */
	  log_lsa.offset = prev_tranlsa.offset;
	  log_rec = LOG_GET_LOG_RECORD_HEADER (log_pgptr, &log_lsa);

	  /*
	   * Next record to undo.. that is previous record in the chain.
	   * We need to save it in this variable since the undo_nxlsa pointer
	   * may be set when we log something related to rollback (e.g., case
	   * of logical operation). Reset the undo_nxlsa back once the
	   * rollback_rec is done.
	   */

	  LSA_COPY (&prev_tranlsa, &log_rec->prev_tranlsa);
	  LSA_COPY (&tdes->undo_nxlsa, &prev_tranlsa);

	  switch (log_rec->type)
	    {
	    case LOG_MVCC_UNDOREDO_DATA:
	    case LOG_MVCC_DIFF_UNDOREDO_DATA:
	    case LOG_UNDOREDO_DATA:
	    case LOG_DIFF_UNDOREDO_DATA:

	      /* Does this record belong to a MVCC op? */
	      if (log_rec->type == LOG_MVCC_UNDOREDO_DATA
		  || log_rec->type == LOG_MVCC_DIFF_UNDOREDO_DATA)
		{
		  is_mvcc_op = true;
		}
	      else
		{
		  is_mvcc_op = false;
		}

	      /* Read the DATA HEADER */
	      LOG_READ_ADD_ALIGN (thread_p, sizeof (*log_rec), &log_lsa,
				  log_pgptr);
	      if (is_mvcc_op)
		{
		  /* Data header is MVCC undoredo */
		  data_header_size = sizeof (*mvcc_undoredo);
		  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p,
						    data_header_size,
						    &log_lsa, log_pgptr);
		  mvcc_undoredo =
		    (struct log_mvcc_undoredo *) ((char *) log_pgptr->area +
						  log_lsa.offset);

		  /* Get undoredo info */
		  undoredo = &mvcc_undoredo->undoredo;

		  /* Save transaction MVCCID for recovery */
		  rcv.mvcc_id = mvcc_undoredo->mvccid;
		}
	      else
		{
		  data_header_size = sizeof (*undoredo);
		  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p,
						    data_header_size,
						    &log_lsa, log_pgptr);
		  undoredo =
		    (struct log_undoredo *) ((char *) log_pgptr->area +
					     log_lsa.offset);

		  rcv.mvcc_id = MVCCID_NULL;
		}

	      rcvindex = undoredo->data.rcvindex;
	      rcv.length = undoredo->ulength;
	      rcv.offset = undoredo->data.offset;
	      rcv_vpid.volid = undoredo->data.volid;
	      rcv_vpid.pageid = undoredo->data.pageid;

	      LOG_READ_ADD_ALIGN (thread_p, data_header_size, &log_lsa,
				  log_pgptr);

	      log_rollback_record (thread_p, &log_lsa, log_pgptr,
				   rcvindex, &rcv_vpid, &rcv, tdes,
				   log_unzip_ptr);
	      break;

	    case LOG_MVCC_UNDO_DATA:
	    case LOG_UNDO_DATA:
	      /* Does record belong to a MVCC op? */
	      is_mvcc_op = (log_rec->type == LOG_MVCC_UNDO_DATA);

	      /* Read the DATA HEADER */
	      LOG_READ_ADD_ALIGN (thread_p, sizeof (*log_rec), &log_lsa,
				  log_pgptr);
	      if (is_mvcc_op)
		{
		  /* Data header is MVCC undo */
		  data_header_size = sizeof (*mvcc_undo);
		  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p,
						    data_header_size,
						    &log_lsa, log_pgptr);
		  mvcc_undo =
		    (struct log_mvcc_undo *) ((char *) log_pgptr->area +
					      log_lsa.offset);
		  /* Get undo info */
		  undo = &mvcc_undo->undo;

		  /* Save transaction MVCCID for recovery */
		  rcv.mvcc_id = mvcc_undo->mvccid;
		}
	      else
		{
		  data_header_size = sizeof (*undo);
		  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p,
						    data_header_size,
						    &log_lsa, log_pgptr);
		  undo =
		    (struct log_undo *) ((char *) log_pgptr->area +
					 log_lsa.offset);

		  rcv.mvcc_id = MVCCID_NULL;
		}
	      rcvindex = undo->data.rcvindex;
	      rcv.offset = undo->data.offset;
	      rcv_vpid.volid = undo->data.volid;
	      rcv_vpid.pageid = undo->data.pageid;
	      rcv.length = undo->length;

	      LOG_READ_ADD_ALIGN (thread_p, data_header_size, &log_lsa,
				  log_pgptr);

	      log_rollback_record (thread_p, &log_lsa, log_pgptr, rcvindex,
				   &rcv_vpid, &rcv, tdes, log_unzip_ptr);
	      break;

	    case LOG_COMPENSATE:
	      /*
	       * We found a partial rollback, use the CLR to find the next record
	       * to undo
	       */

	      /* Read the DATA HEADER */
	      LOG_READ_ADD_ALIGN (thread_p, sizeof (*log_rec), &log_lsa,
				  log_pgptr);
	      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p,
						sizeof (*compensate),
						&log_lsa, log_pgptr);
	      compensate =
		(struct log_compensate *) ((char *) log_pgptr->area +
					   log_lsa.offset);
	      LSA_COPY (&prev_tranlsa, &compensate->undo_nxlsa);
	      break;

	    case LOG_LCOMPENSATE:
	      /*
	       * We found a partial rollback, use the CLR to find the next record
	       * to undo
	       */

	      /* Read the DATA HEADER */
	      LOG_READ_ADD_ALIGN (thread_p, sizeof (*log_rec), &log_lsa,
				  log_pgptr);
	      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p,
						sizeof (*logical_comp),
						&log_lsa, log_pgptr);
	      logical_comp =
		((struct log_logical_compensate *) ((char *) log_pgptr->area +
						    log_lsa.offset));
	      LSA_COPY (&prev_tranlsa, &logical_comp->undo_nxlsa);
	      break;

	    case LOG_COMMIT_TOPOPE:
	    case LOG_ABORT_TOPOPE:
	      /*
	       * We found a system top operation that should be skipped from
	       * rollback
	       */

	      /* Read the DATA HEADER */
	      LOG_READ_ADD_ALIGN (thread_p, sizeof (*log_rec), &log_lsa,
				  log_pgptr);
	      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p,
						sizeof (*top_result),
						&log_lsa, log_pgptr);
	      top_result =
		((struct log_topop_result *) ((char *) log_pgptr->area +
					      log_lsa.offset));
	      LSA_COPY (&prev_tranlsa, &top_result->lastparent_lsa);
	      break;

	    case LOG_CLIENT_NAME:
	    case LOG_REDO_DATA:
	    case LOG_MVCC_REDO_DATA:
	    case LOG_DBEXTERN_REDO_DATA:
	    case LOG_DUMMY_HEAD_POSTPONE:
	    case LOG_POSTPONE:
	    case LOG_CLIENT_USER_UNDO_DATA:
	    case LOG_CLIENT_USER_POSTPONE_DATA:
	    case LOG_START_CHKPT:
	    case LOG_END_CHKPT:
	    case LOG_SAVEPOINT:
	    case LOG_2PC_PREPARE:
	    case LOG_2PC_START:
	    case LOG_2PC_ABORT_DECISION:
	    case LOG_2PC_ABORT_INFORM_PARTICPS:
	    case LOG_REPLICATION_DATA:
	    case LOG_REPLICATION_SCHEMA:
	    case LOG_UNLOCK_COMMIT:
	    case LOG_UNLOCK_ABORT:
	    case LOG_DUMMY_HA_SERVER_STATE:
	    case LOG_DUMMY_OVF_RECORD:
	      break;

	    case LOG_RUN_POSTPONE:
	    case LOG_RUN_NEXT_CLIENT_UNDO:
	    case LOG_RUN_NEXT_CLIENT_POSTPONE:
	    case LOG_WILL_COMMIT:
	    case LOG_COMMIT_WITH_POSTPONE:
	    case LOG_COMMIT_WITH_CLIENT_USER_LOOSE_ENDS:
	    case LOG_COMMIT:
	    case LOG_COMMIT_TOPOPE_WITH_POSTPONE:
	    case LOG_COMMIT_TOPOPE_WITH_CLIENT_USER_LOOSE_ENDS:
	    case LOG_ABORT_WITH_CLIENT_USER_LOOSE_ENDS:
	    case LOG_ABORT:
	    case LOG_ABORT_TOPOPE_WITH_CLIENT_USER_LOOSE_ENDS:
	    case LOG_2PC_COMMIT_DECISION:
	    case LOG_2PC_COMMIT_INFORM_PARTICPS:
	    case LOG_2PC_RECV_ACK:
	    case LOG_DUMMY_CRASH_RECOVERY:
	    case LOG_DUMMY_FILLPAGE_FORARCHIVE:	/* for backward compatibility */
	    case LOG_END_OF_LOG:
	    case LOG_SMALLER_LOGREC_TYPE:
	    case LOG_LARGER_LOGREC_TYPE:
	    default:
	      {
		char msg[LINE_MAX];

		er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
			ER_LOG_PAGE_CORRUPTED, 1, log_lsa.pageid);
		log_dump_record_header_to_string (log_rec, msg, LINE_MAX);
		logpb_fatal_error (thread_p, true, ARG_FILE_LINE, msg);
		break;
	      }
	    }			/* switch */

	  /* Just in case, it was changed or the previous address has changed */
	  LSA_COPY (&tdes->undo_nxlsa, &prev_tranlsa);
	}			/* while */

    }				/* while */

  /* Remember the undo next lsa for partial rollbacks */
  LSA_COPY (&tdes->undo_nxlsa, &prev_tranlsa);
  (void) xlogtb_reset_wait_msecs (thread_p, old_wait_msecs);

  if (log_unzip_ptr != NULL)
    {
      log_zip_free (log_unzip_ptr);
    }

  return;

}

/*
 * log_get_next_nested_top - Get top system action list
 *
 * return: top system action count
 *
 *   tdes(in): Transaction descriptor
 *   start_postpone_lsa(in): Where to start looking for postpone records
 *   out_nxtop_range_stack(in/out): Set as a side effect to topop range stack.
 *
 * NOTE: Find a nested top system operation which start after
 *              start_postpone_lsa and before tdes->tail_lsa.
 */
int
log_get_next_nested_top (THREAD_ENTRY * thread_p, LOG_TDES * tdes,
			 LOG_LSA * start_postpone_lsa,
			 LOG_TOPOP_RANGE ** out_nxtop_range_stack)
{
  struct log_topop_result *top_result;
  char log_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT];
  char *aligned_log_pgbuf;
  LOG_PAGE *log_pgptr = NULL;
  LOG_RECORD_HEADER *log_rec;
  LOG_LSA tmp_log_lsa;
  LOG_LSA top_result_lsa;
  LOG_LSA prev_last_parent_lsa;
  LOG_TOPOP_RANGE *nxtop_stack;
  LOG_TOPOP_RANGE *prev_nxtop_stack;
  int nxtop_count = 0;
  int nxtop_stack_size = 0;
  LOG_PAGEID last_fetch_page_id = NULL_PAGEID;

  if (LSA_ISNULL (&tdes->tail_topresult_lsa)
      || !LSA_GT (&tdes->tail_topresult_lsa, start_postpone_lsa))
    {
      return 0;
    }

  LSA_COPY (&top_result_lsa, &tdes->tail_topresult_lsa);
  LSA_SET_NULL (&prev_last_parent_lsa);

  aligned_log_pgbuf = PTR_ALIGN (log_pgbuf, MAX_ALIGNMENT);
  log_pgptr = (LOG_PAGE *) aligned_log_pgbuf;

  nxtop_stack = *out_nxtop_range_stack;
  nxtop_stack_size = LOG_TOPOP_STACK_INIT_SIZE;

  do
    {
      if (nxtop_count >= nxtop_stack_size)
	{
	  prev_nxtop_stack = nxtop_stack;

	  nxtop_stack_size *= 2;
	  nxtop_stack = (LOG_TOPOP_RANGE *) malloc (nxtop_stack_size *
						    sizeof (LOG_TOPOP_RANGE));
	  if (nxtop_stack == NULL)
	    {
	      if (prev_nxtop_stack != *out_nxtop_range_stack)
		{
		  free_and_init (prev_nxtop_stack);
		}
	      logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
				 "log_get_next_nested_top");
	      return 0;
	    }

	  memcpy (nxtop_stack, prev_nxtop_stack,
		  (nxtop_stack_size / 2) * sizeof (LOG_TOPOP_RANGE));

	  if (prev_nxtop_stack != *out_nxtop_range_stack)
	    {
	      free_and_init (prev_nxtop_stack);
	    }
	}

      if (last_fetch_page_id != top_result_lsa.pageid)
	{
	  if (logpb_fetch_page (thread_p, top_result_lsa.pageid,
				log_pgptr) == NULL)
	    {
	      if (nxtop_stack != *out_nxtop_range_stack)
		{
		  free_and_init (nxtop_stack);
		}
	      logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
				 "log_get_next_nested_top");
	      return 0;
	    }
	}

      log_rec = LOG_GET_LOG_RECORD_HEADER (log_pgptr, &top_result_lsa);

      if (log_rec->type == LOG_COMMIT_TOPOPE
	  || log_rec->type == LOG_ABORT_TOPOPE)
	{
	  /* Read the DATA HEADER */
	  LSA_COPY (&tmp_log_lsa, &top_result_lsa);
	  LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER),
			      &tmp_log_lsa, log_pgptr);
	  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p,
					    sizeof (struct log_topop_result),
					    &tmp_log_lsa, log_pgptr);
	  top_result =
	    (struct log_topop_result *) ((char *) log_pgptr->area +
					 tmp_log_lsa.offset);
	  last_fetch_page_id = tmp_log_lsa.pageid;

	  /*
	   * There may be some nested top system operations that are committed
	   * and aborted in the desired region
	   */
	  if (LSA_ISNULL (&prev_last_parent_lsa)
	      || LSA_LE (&top_result_lsa, &prev_last_parent_lsa))
	    {
	      LSA_COPY (&(nxtop_stack[nxtop_count].start_lsa),
			&top_result->lastparent_lsa);
	      LSA_COPY (&(nxtop_stack[nxtop_count].end_lsa), &top_result_lsa);
	      nxtop_count++;

	      LSA_COPY (&prev_last_parent_lsa, &top_result->lastparent_lsa);
	    }

	  LSA_COPY (&top_result_lsa, &top_result->prv_topresult_lsa);
	}
      else
	{
	  if (nxtop_stack != *out_nxtop_range_stack)
	    {
	      free_and_init (nxtop_stack);
	    }
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			     "log_get_next_nested_top");
	  return 0;
	}
    }
  while (top_result_lsa.pageid != NULL_PAGEID
	 && LSA_GT (&top_result_lsa, start_postpone_lsa));

  *out_nxtop_range_stack = nxtop_stack;

  return nxtop_count;
}

/*
 * log_do_postpone - Scan forward doing postpone operations of given
 *                  transaction
 *
 * return: nothing
 *
 *   tdes(in): Transaction descriptor
 *   start_posplsa(in): Where to start looking for postpone records
 *   posp_type(in): Type of postpone executed
 *
 * NOTE: Scan the log forward doing postpone operations of given
 *              transaction. This function is invoked after a transaction is
 *              declared committed with postpone actions.
 */
void
log_do_postpone (THREAD_ENTRY * thread_p, LOG_TDES * tdes,
		 LOG_LSA * start_postpone_lsa, LOG_RECTYPE postpone_type)
{
  LOG_LSA end_postpone_lsa;	/* The last postpone record of
				 * transaction cannot be after this
				 * address
				 */
  LOG_LSA start_seek_lsa;	/* start looking for posptpone records
				 * at this address
				 */
  LOG_LSA *end_seek_lsa;	/* Stop looking for postpone records
				 * at this address
				 */
  LOG_LSA next_start_seek_lsa;	/* Next address to look for postpone
				 * records. Usually the end of a top
				 * system operation.
				 */
  LOG_LSA log_lsa;
  LOG_LSA forward_lsa;

  char log_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT];
  char *aligned_log_pgbuf;
  LOG_PAGE *log_pgptr = NULL;
  LOG_RECORD_HEADER *log_rec;
  bool isdone;

  LOG_TOPOP_RANGE nxtop_array[LOG_TOPOP_STACK_INIT_SIZE];
  LOG_TOPOP_RANGE *nxtop_stack = NULL;
  LOG_TOPOP_RANGE *nxtop_range = NULL;
  int nxtop_count = 0;

  if (LSA_ISNULL (start_postpone_lsa))
    {
      return;
    }

  if (log_is_in_crash_recovery () == false)
    {
      /* Log the transaction as committed with postpone actions and then
       * start executing the postpone actions.
       */
      if (postpone_type == LOG_COMMIT_WITH_POSTPONE)
	{
	  log_append_commit_postpone (thread_p, tdes, start_postpone_lsa);
	}
      else
	{
	  log_append_topope_commit_postpone (thread_p, tdes,
					     start_postpone_lsa);
	}
    }

  aligned_log_pgbuf = PTR_ALIGN (log_pgbuf, MAX_ALIGNMENT);
  log_pgptr = (LOG_PAGE *) aligned_log_pgbuf;

  LSA_COPY (&end_postpone_lsa, &tdes->tail_lsa);
  LSA_COPY (&next_start_seek_lsa, start_postpone_lsa);

  nxtop_stack = nxtop_array;
  nxtop_count = log_get_next_nested_top (thread_p, tdes, start_postpone_lsa,
					 &nxtop_stack);

  while (!LSA_ISNULL (&next_start_seek_lsa))
    {
      LSA_COPY (&start_seek_lsa, &next_start_seek_lsa);

      if (nxtop_count > 0)
	{
	  nxtop_count--;
	  nxtop_range = &(nxtop_stack[nxtop_count]);

	  if (LSA_LT (&start_seek_lsa, &(nxtop_range->start_lsa)))
	    {
	      end_seek_lsa = &(nxtop_range->start_lsa);
	      LSA_COPY (&next_start_seek_lsa, &(nxtop_range->end_lsa));
	    }
	  else if (LSA_EQ (&start_seek_lsa, &(nxtop_range->end_lsa)))
	    {
	      end_seek_lsa = &end_postpone_lsa;
	      LSA_SET_NULL (&next_start_seek_lsa);
	    }
	  else
	    {
	      LSA_COPY (&next_start_seek_lsa, &(nxtop_range->end_lsa));
	      continue;
	    }
	}
      else
	{
	  end_seek_lsa = &end_postpone_lsa;
	  LSA_SET_NULL (&next_start_seek_lsa);
	}

      /*
       * Start doing postpone operation for this range
       */

      LSA_COPY (&forward_lsa, &start_seek_lsa);

      isdone = false;
      while (!LSA_ISNULL (&forward_lsa) && !isdone)
	{
	  /* Fetch the page where the postpone LSA record is located */
	  log_lsa.pageid = forward_lsa.pageid;
	  if (logpb_fetch_page (thread_p, log_lsa.pageid, log_pgptr) == NULL)
	    {
	      logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
				 "log_do_postpone");
	      goto end;
	    }

	  while (forward_lsa.pageid == log_lsa.pageid)
	    {
	      if (LSA_GT (&forward_lsa, end_seek_lsa))
		{
		  /* Finish at this point */
		  isdone = true;
		  break;
		}
	      /*
	       * If an offset is missing, it is because we archive an incomplete
	       * log record. This log_record was completed later.
	       * Thus, we have to find the offset by searching
	       * for the next log_record in the page.
	       */
	      if (forward_lsa.offset == NULL_OFFSET)
		{
		  forward_lsa.offset = log_pgptr->hdr.offset;
		  if (forward_lsa.offset == NULL_OFFSET)
		    {
		      /* Continue at next pageid */
		      if (logpb_is_page_in_archive (log_lsa.pageid))
			{
			  forward_lsa.pageid = log_lsa.pageid + 1;
			}
		      else
			{
			  forward_lsa.pageid = NULL_PAGEID;
			}
		      continue;
		    }
		}

	      /* Find the postpone log record to execute */
	      log_lsa.offset = forward_lsa.offset;
	      log_rec = LOG_GET_LOG_RECORD_HEADER (log_pgptr, &log_lsa);

	      /* Find the next log record in the log */
	      LSA_COPY (&forward_lsa, &log_rec->forw_lsa);

	      if (forward_lsa.pageid == NULL_PAGEID
		  && logpb_is_page_in_archive (log_lsa.pageid))
		{
		  forward_lsa.pageid = log_lsa.pageid + 1;
		}

	      if (log_rec->trid == tdes->trid)
		{
		  switch (log_rec->type)
		    {
		    case LOG_CLIENT_NAME:
		    case LOG_UNDOREDO_DATA:
		    case LOG_DIFF_UNDOREDO_DATA:
		    case LOG_UNDO_DATA:
		    case LOG_REDO_DATA:
		    case LOG_MVCC_UNDOREDO_DATA:
		    case LOG_MVCC_DIFF_UNDOREDO_DATA:
		    case LOG_MVCC_UNDO_DATA:
		    case LOG_MVCC_REDO_DATA:
		    case LOG_DBEXTERN_REDO_DATA:
		    case LOG_RUN_POSTPONE:
		    case LOG_COMPENSATE:
		    case LOG_LCOMPENSATE:
		    case LOG_CLIENT_USER_UNDO_DATA:
		    case LOG_CLIENT_USER_POSTPONE_DATA:
		    case LOG_SAVEPOINT:
		    case LOG_DUMMY_HEAD_POSTPONE:
		    case LOG_REPLICATION_DATA:
		    case LOG_REPLICATION_SCHEMA:
		    case LOG_UNLOCK_COMMIT:
		    case LOG_UNLOCK_ABORT:
		    case LOG_DUMMY_HA_SERVER_STATE:
		    case LOG_DUMMY_OVF_RECORD:
		      break;

		    case LOG_POSTPONE:
		      if (log_run_postpone_op (thread_p,
					       &log_lsa,
					       log_pgptr) != NO_ERROR)
			{
			  goto end;
			}
		      break;

		    case LOG_WILL_COMMIT:
		    case LOG_COMMIT_WITH_POSTPONE:
		    case LOG_COMMIT_TOPOPE_WITH_POSTPONE:
		    case LOG_2PC_PREPARE:
		    case LOG_2PC_START:
		    case LOG_2PC_COMMIT_DECISION:
		      /* This is it */
		      LSA_SET_NULL (&forward_lsa);
		      break;

		    case LOG_COMMIT_TOPOPE_WITH_CLIENT_USER_LOOSE_ENDS:
		    case LOG_ABORT_TOPOPE_WITH_CLIENT_USER_LOOSE_ENDS:
		    case LOG_COMMIT_TOPOPE:
		    case LOG_ABORT_TOPOPE:
		      if (!LSA_EQ (&log_lsa, &start_seek_lsa))
			{
#if defined(CUBRID_DEBUG)
			  er_log_debug (ARG_FILE_LINE,
					"log_do_postpone: SYSTEM ERROR.."
					" Bad log_rectype = %d\n (%s)."
					" Maybe BAD POSTPONE RANGE\n",
					log_rec->type,
					log_to_string (log_rec->type));
#endif /* CUBRID_DEBUG */
			  ;	/* Nothing */
			}
		      break;

		    case LOG_END_OF_LOG:
		      if (forward_lsa.pageid == NULL_PAGEID
			  && logpb_is_page_in_archive (log_lsa.pageid))
			{
			  forward_lsa.pageid = log_lsa.pageid + 1;
			}
		      break;

		    case LOG_COMMIT_WITH_CLIENT_USER_LOOSE_ENDS:
		    case LOG_COMMIT:
		    case LOG_ABORT_WITH_CLIENT_USER_LOOSE_ENDS:
		    case LOG_ABORT:
		    case LOG_RUN_NEXT_CLIENT_UNDO:
		    case LOG_RUN_NEXT_CLIENT_POSTPONE:
		    case LOG_START_CHKPT:
		    case LOG_END_CHKPT:
		    case LOG_2PC_ABORT_DECISION:
		    case LOG_2PC_ABORT_INFORM_PARTICPS:
		    case LOG_2PC_COMMIT_INFORM_PARTICPS:
		    case LOG_2PC_RECV_ACK:
		    case LOG_DUMMY_CRASH_RECOVERY:
		    case LOG_DUMMY_FILLPAGE_FORARCHIVE:	/* for backward compatibility */
		    case LOG_SMALLER_LOGREC_TYPE:
		    case LOG_LARGER_LOGREC_TYPE:
		    default:
#if defined(CUBRID_DEBUG)
		      er_log_debug (ARG_FILE_LINE,
				    "log_do_postpone: SYSTEM ERROR.."
				    "Bad log_rectype = %d (%s)... ignored\n",
				    log_rec->type,
				    log_to_string (log_rec->type));
#endif /* CUBRID_DEBUG */
		      break;
		    }
		}

	      /*
	       * We can fix the lsa.pageid in the case of log_records without
	       * forward address at this moment.
	       */

	      if (forward_lsa.offset == NULL_OFFSET
		  && forward_lsa.pageid != NULL_PAGEID
		  && forward_lsa.pageid < log_lsa.pageid)
		{
		  forward_lsa.pageid = log_lsa.pageid;
		}
	    }
	}
    }

end:
  if (nxtop_stack != nxtop_array && nxtop_stack != NULL)
    {
      free_and_init (nxtop_stack);
    }

  return;
}

static int
log_run_postpone_op (THREAD_ENTRY * thread_p, LOG_LSA * log_lsa,
		     LOG_PAGE * log_pgptr)
{
  LOG_LSA ref_lsa;		/* The address of a postpone record    */
  struct log_redo *redo;	/* A redo log record                   */
  LOG_RCV rcv;			/* Recovery structure for execution    */
  VPID rcv_vpid;		/* Location of data to redo            */
  LOG_RCVINDEX rcvindex;	/* The recovery index                  */
  LOG_DATA_ADDR rvaddr;
  char *area = NULL;

  LSA_COPY (&ref_lsa, log_lsa);

  /* Get the DATA HEADER */
  LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER), log_lsa,
		      log_pgptr);
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (struct log_redo),
				    log_lsa, log_pgptr);

  redo = (struct log_redo *) ((char *) log_pgptr->area + log_lsa->offset);

  rcvindex = redo->data.rcvindex;
  rcv_vpid.volid = redo->data.volid;
  rcv_vpid.pageid = redo->data.pageid;
  rcv.offset = redo->data.offset;
  rcv.length = redo->length;

  LOG_READ_ADD_ALIGN (thread_p, sizeof (struct log_redo), log_lsa, log_pgptr);

  if (rcvindex == RVVAC_DROPPED_FILE_ADD)
    {
      rcv.pgptr = NULL;
    }
  else
    {
      if (rcv_vpid.volid == NULL_VOLID
	  || rcv_vpid.pageid == NULL_PAGEID
	  || (disk_isvalid_page (thread_p, rcv_vpid.volid,
				 rcv_vpid.pageid) != DISK_VALID))
	{
	  return NO_ERROR;
	}

      rcv.pgptr = pgbuf_fix_with_retry (thread_p, &rcv_vpid, OLD_PAGE,
					PGBUF_LATCH_WRITE, 10);
    }

  /* GET AFTER DATA */

  /*
   * If data is contained in only one buffer, pass pointer
   * directly. Otherwise, allocate a contiguous area, copy the
   * data and pass this area. At the end deallocate the area
   */

  rvaddr.offset = rcv.offset;
  rvaddr.pgptr = rcv.pgptr;

  if (log_lsa->offset + rcv.length < (int) LOGAREA_SIZE)
    {
      rcv.data = (char *) log_pgptr->area + log_lsa->offset;
    }
  else
    {
      /* Need to copy the data into a contiguous area */
      area = (char *) malloc (rcv.length);
      if (area == NULL)
	{
	  if (rcv.pgptr != NULL)
	    {
	      pgbuf_unfix (thread_p, rcv.pgptr);
	    }

	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			     "log_do_postpone");

	  return ER_FAILED;
	}

      /* Copy the data */
      logpb_copy_from_log (thread_p, area, rcv.length, log_lsa, log_pgptr);
      rcv.data = area;
    }

  /*
   * if rcvindex is RVDK_IDDEALLOC_WITH_VOLHEADER,
   * Don't append same log like others.
   * because it modify two pages (volume header & bit map)
   * so, we must append two WAL logs for each page later
   * (after below RV_fun call ends successfully)
   */

  /* Now call the REDO recovery function */
  if (rcv.pgptr != NULL
      || (rcv_vpid.volid == NULL_VOLID && rcv_vpid.pageid == NULL_PAGEID))
    {
      if (rcvindex == RVDK_IDDEALLOC_WITH_VOLHEADER)
	{
	  (void) disk_rv_alloctable_with_volheader (thread_p, &rcv, &ref_lsa);
	}
      else if (rcvindex == RVVAC_DROPPED_FILE_ADD)
	{
	  /* We don't know yet in which page the dropped file will end up so
	   * we have to do a special call here.
	   */
	  (void) vacuum_notify_dropped_file (thread_p, &rcv, &ref_lsa);
	}
      else
	{
	  /*
	   * Write the corresponding run postpone record for
	   * the postpone action
	   */
	  log_append_run_postpone (thread_p, rcvindex,
				   &rvaddr, &rcv_vpid,
				   rcv.length, rcv.data, &ref_lsa);

	  /* Now call the REDO recovery function */
	  (void) (*RV_fun[rcvindex].redofun) (thread_p, &rcv);
	}
    }
  else
    {
      /*
       * Unable to fetch page of volume... May need media recovery
       * on such page
       */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_MAYNEED_MEDIA_RECOVERY,
	      1, fileio_get_volume_label (rcv_vpid.volid, PEEK));
    }

  if (area != NULL)
    {
      free_and_init (area);
    }

  if (rcv.pgptr != NULL)
    {
      pgbuf_unfix (thread_p, rcv.pgptr);
    }

  return NO_ERROR;
}

/*
 * log_find_end_log - FIND END OF LOG
 *
 * return: nothing
 *
 *   end_lsa(in/out): Address of end of log
 *
 * NOTE: Find the end of the log (i.e., the end of the active portion
 *              of the log).
 */
static void
log_find_end_log (THREAD_ENTRY * thread_p, LOG_LSA * end_lsa)
{
  LOG_PAGEID pageid;		/* Log page identifier   */
  char log_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT], *aligned_log_pgbuf;
  LOG_PAGE *log_pgptr = NULL;	/* Pointer to a log page */
  LOG_RECORD_HEADER *eof = NULL;	/* End of log record     */
  LOG_RECTYPE type;		/* Type of a log record  */

  aligned_log_pgbuf = PTR_ALIGN (log_pgbuf, MAX_ALIGNMENT);

  /* Guess the end of the log from the header */

  LSA_COPY (end_lsa, &log_Gl.hdr.append_lsa);
  type = LOG_LARGER_LOGREC_TYPE;

  log_pgptr = (LOG_PAGE *) aligned_log_pgbuf;

  while (type != LOG_END_OF_LOG && !LSA_ISNULL (end_lsa))
    {
      /* Fetch the page */
      if ((logpb_fetch_page (thread_p, end_lsa->pageid, log_pgptr)) == NULL)
	{
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			     "log_find_end_log");
	  goto error;

	}
      pageid = end_lsa->pageid;

      while (end_lsa->pageid == pageid)
	{
	  /*
	   * If an offset is missing, it is because we archive an incomplete
	   * log record. This log_record was completed later. Thus, we have to
	   * find the offset by searching for the next log_record in the page
	   */
	  if (!(end_lsa->offset == NULL_OFFSET
		&& (end_lsa->offset = log_pgptr->hdr.offset) == NULL_OFFSET))
	    {
	      eof = LOG_GET_LOG_RECORD_HEADER (log_pgptr, end_lsa);
	      /*
	       * If the type is an EOF located at the active portion of the log,
	       * stop
	       */
	      if ((type = eof->type) == LOG_END_OF_LOG)
		{
		  if (logpb_is_page_in_archive (pageid))
		    {
		      type = LOG_LARGER_LOGREC_TYPE;
		    }
		  else
		    {
		      break;
		    }
		}
	      else
		if (type <= LOG_SMALLER_LOGREC_TYPE
		    || type >= LOG_LARGER_LOGREC_TYPE)
		{
#if defined(CUBRID_DEBUG)
		  er_log_debug (ARG_FILE_LINE,
				"log_find_end_log: Unknown record"
				" type = %d (%s).\n", type,
				log_to_string (type));
#endif /* CUBRID_DEBUG */
		  LSA_SET_NULL (end_lsa);
		  break;
		}
	      else
		{
		  LSA_COPY (end_lsa, &eof->forw_lsa);
		}
	    }
	  else
	    {
	      LSA_SET_NULL (end_lsa);
	    }

	  /*
	   * If the next page is NULL_PAGEID and the current page is an archive
	   * page, this is not the end, this situation happens because of an
	   * incomplete log record was archived.
	   */

	  if (LSA_ISNULL (end_lsa) && logpb_is_page_in_archive (pageid))
	    {
	      end_lsa->pageid = pageid + 1;
	    }
	}

      if (type == LOG_END_OF_LOG && eof != NULL
	  && !LSA_EQ (end_lsa, &log_Gl.hdr.append_lsa))
	{
	  /*
	   * Reset the log header for future reads, multiple restart crashes,
	   * and so on
	   */
	  LOG_RESET_APPEND_LSA (end_lsa);
	  log_Gl.hdr.next_trid = eof->trid;
	}
    }

  return;

error:

  LSA_SET_NULL (end_lsa);
  return;
}

/*
 * log_recreate - RECREATE THE LOG WITHOUT REMOVING THE DATABASE
 *
 * return: nothing
 *
 *   num_perm_vols(in):
 *   db_fullname(in): Full name of the database
 *   logpath(in): Directory where the log volumes reside
 *   prefix_logname(in): Name of the log volumes. It is usually set the same as
 *                      database name. For example, if the value is equal to
 *                      "db", the names of the log volumes created are as
 *                      follow:
 *                      Active_log      = db_logactive
 *                      Archive_logs    = db_logarchive.0
 *                                        db_logarchive.1
 *                                             .
 *                                             .
 *                                             .
 *                                        db_logarchive.n
 *                      Log_information = db_loginfo
 *                      Database Backup = db_backup
 *   log_npages(in): Size of active log in pages
 *   out_fp (in) :
 *
 * NOTE: Recreate the active log volume with the new specifications.
 *              All recovery information in each data volume is removed.
 *              If there are anything to recover (e.g., the database suffered
 *              a crash), it is not done. Therefore, it is very important to
 *              make sure that the database does not need to be recovered. You
 *              could restart the database and then shutdown to enfore any
 *              recovery. The database will end up as it is currently on disk.
 *              This function can also be used to restart a database when the
 *              log is corrupted somehow (e.g., system bug, isn't) or the log
 *              is not available or it suffered a media crash. It can also be
 *              used to move the log to another location. It is recommended to
 *              backup the database before and after the operation is
 *              executed.
 *
 *        This function must be run offline. That is, it should not be
 *              run when there are multiusers in the system.
 */
void
log_recreate (THREAD_ENTRY * thread_p, VOLID num_perm_vols,
	      const char *db_fullname, const char *logpath,
	      const char *prefix_logname, DKNPAGES log_npages, FILE * out_fp)
{
  const char *vlabel;
  INT64 db_creation;
  DISK_VOLPURPOSE vol_purpose;
  VOL_SPACE_INFO space_info;
  VOLID volid;
  int vdes;
  LOG_LSA init_nontemp_lsa;
  int ret = NO_ERROR;

  ret = disk_get_creation_time (thread_p, LOG_DBFIRST_VOLID, &db_creation);
  log_create_internal (thread_p, db_fullname, logpath, prefix_logname,
		       log_npages, &db_creation);

  (void) log_initialize_internal (thread_p, db_fullname, logpath,
				  prefix_logname, false, NULL, true);

  /*
   * RESET RECOVERY INFORMATION ON ALL DATA VOLUMES
   */

  LSA_SET_INIT_NONTEMP (&init_nontemp_lsa);

  for (volid = LOG_DBFIRST_VOLID; volid < num_perm_vols; volid++)
    {
      char vol_fullname[PATH_MAX];

      vlabel = fileio_get_volume_label (volid, PEEK);

      /* Find the current pages of the volume and its descriptor */

      if (xdisk_get_purpose_and_space_info (thread_p, volid,
					    &vol_purpose,
					    &space_info) != volid)
	{
	  continue;
	}

      vdes = fileio_get_volume_descriptor (volid);

      /*
       * Flush all dirty pages and then invalidate them from page buffer pool.
       * So that we can reset the recovery information directly using the io
       * module
       */

      LOG_CS_ENTER (thread_p);
      logpb_flush_pages_direct (thread_p);
      LOG_CS_EXIT (thread_p);

      (void) pgbuf_flush_all (thread_p, volid);
      (void) pgbuf_invalidate_all (thread_p, volid);	/* it flush and invalidate */

      if (vol_purpose != DISK_PERMVOL_TEMP_PURPOSE
	  && vol_purpose != DISK_TEMPVOL_TEMP_PURPOSE)
	{
	  (void) fileio_reset_volume (thread_p, vdes, vlabel,
				      space_info.total_pages,
				      &init_nontemp_lsa);
	}

      (void) disk_set_creation (thread_p, volid, vlabel,
				&log_Gl.hdr.db_creation,
				&log_Gl.hdr.chkpt_lsa, false,
				DISK_DONT_FLUSH);
      LOG_CS_ENTER (thread_p);
      logpb_flush_pages_direct (thread_p);
      LOG_CS_EXIT (thread_p);

      (void) pgbuf_flush_all_unfixed_and_set_lsa_as_null (thread_p, volid);

      /*
       * reset temp LSA to special temp LSA
       */
      (void) logpb_check_and_reset_temp_lsa (thread_p, volid);

      /*
       * add volume info to vinf
       */
      xdisk_get_fullname (thread_p, volid, vol_fullname);
      logpb_add_volume (NULL, volid, vol_fullname, vol_purpose);

      if (out_fp != NULL)
	{
	  fprintf (out_fp, "%s... done\n", vol_fullname);
	  fflush (out_fp);
	}
    }

  (void) pgbuf_flush_all (thread_p, NULL_VOLID);
  (void) fileio_synchronize_all (thread_p, false);
  (void) log_commit (thread_p, NULL_TRAN_INDEX, false);
}

/*
 * log_get_io_page_size - FIND SIZE OF DATABASE PAGE
 *
 * return:
 *
 *   db_fullname(in): Full name of the database
 *   logpath(in): Directory where the log volumes reside
 *   prefix_logname(in): Name of the log volumes. It is usually set as database
 *                      name. For example, if the value is equal to "db", the
 *                      names of the log volumes created are as follow:
 *                      Active_log      = db_logactive
 *                      Archive_logs    = db_logarchive.0
 *                                        db_logarchive.1
 *                                             .
 *                                             .
 *                                             .
 *                                        db_logarchive.n
 *                      Log_information = db_loginfo
 *                      Database Backup = db_backup
 *
 * NOTE: Find size of database page according to the log manager.
 */
PGLENGTH
log_get_io_page_size (THREAD_ENTRY * thread_p, const char *db_fullname,
		      const char *logpath, const char *prefix_logname)
{
  PGLENGTH db_iopagesize;
  PGLENGTH ignore_log_page_size;
  INT64 ignore_dbcreation;
  float ignore_dbcomp;
  int dummy;

  LOG_CS_ENTER (thread_p);
  if (logpb_find_header_parameters (thread_p, db_fullname, logpath,
				    prefix_logname, &db_iopagesize,
				    &ignore_log_page_size, &ignore_dbcreation,
				    &ignore_dbcomp, &dummy) == -1)
    {
      /*
       * For case where active log could not be found, user still needs
       * an error.
       */
      if (er_errid () == NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_MOUNT_FAIL, 1,
		  log_Name_active);
	}

      LOG_CS_EXIT (thread_p);
      return -1;
    }
  else
    {
      LOG_CS_EXIT (thread_p);
      return db_iopagesize;
    }
}

/*
 * log_get_charset_from_header_page - get charset stored in header page
 *
 * return: charset id (non-negative values are valid)
 *	   -1 if header page cannot be used to determine database charset
 *	   -2 if an error occurs
 *
 *  See log_get_io_page_size for arguments
 */
int
log_get_charset_from_header_page (THREAD_ENTRY * thread_p,
				  const char *db_fullname,
				  const char *logpath,
				  const char *prefix_logname)
{
  PGLENGTH dummy_db_iopagesize;
  PGLENGTH dummy_ignore_log_page_size;
  INT64 dummy_ignore_dbcreation;
  float dummy_ignore_dbcomp;
  int db_charset = INTL_CODESET_NONE;

  LOG_CS_ENTER (thread_p);
  if (logpb_find_header_parameters (thread_p, db_fullname, logpath,
				    prefix_logname, &dummy_db_iopagesize,
				    &dummy_ignore_log_page_size,
				    &dummy_ignore_dbcreation,
				    &dummy_ignore_dbcomp, &db_charset) == -1)
    {
      /*
       * For case where active log could not be found, user still needs
       * an error.
       */
      if (er_errid () == NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_MOUNT_FAIL, 1,
		  log_Name_active);
	}

      LOG_CS_EXIT (thread_p);
      return INTL_CODESET_ERROR;
    }
  else
    {
      LOG_CS_EXIT (thread_p);
      return db_charset;
    }
}

/*
 *
 *                         GENERIC RECOVERY FUNCTIONS
 *
 */

/*
 * log_rv_copy_char - Recover (undo or redo) a string of chars/bytes
 *
 * return: nothing
 *
 *   rcv(in): Recovery structure
 *
 * NOTE: Recover (undo/redo) by copying a string of characters/bytes
 *              onto the specified location. This function can be used for
 *              physical logging.
 */
int
log_rv_copy_char (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  char *to_data;

  assert (rcv->offset + rcv->length <= DB_PAGESIZE);

  to_data = (char *) rcv->pgptr + rcv->offset;
  memcpy (to_data, rcv->data, rcv->length);
  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);
  return NO_ERROR;
}

/*
 * log_rv_dump_char - DUMP INFORMATION TO RECOVER A SET CHARS/BYTES
 *
 * return: nothing
 *
 *   length(in): Length of Recovery Data
 *   data(in): The data being logged
 *
 * NOTE:Dump the information to recover a set of characters/bytes.
 */
void
log_rv_dump_char (FILE * fp, int length, void *data)
{
  log_ascii_dump (fp, length, data);
  fprintf (fp, "\n");
}

/*
 * log_rv_outside_noop_redo - NO-OP of an outside REDO
 *
 * return: nothing
 *
 *   rcv(in): Recovery structure
 *
 * NOTE: No-op of an outside redo.
 *              This can used to fool the recovery manager when doing a
 *              logical UNDO which fails (e.g., unregistering a file that has
 *              already been unregistered) or when doing an external/outside
 *              (e.g., removing a temporary volume) the data base domain.
 */
int
log_rv_outside_noop_redo (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  return NO_ERROR;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * log_simulate_crash - Simulate a system crash
 *
 * return: nothing
 *
 *   flush_log(in): Flush the log before the crash simulation?
 *   flush_data_pages(in): Flush the data pages (page buffer pool) before the
 *                      crash simulation?
 *
 * NOTE:Simulate a system crash. If flush_data_pages is true,  the
 *              data page buffer pool is flushed and the log buffer pool is
 *              flushed too regardless of the value of flush_log. If flush_log
 *              is true, the log buffer pool is flushed.
 */
void
log_simulate_crash (THREAD_ENTRY * thread_p, int flush_log,
		    int flush_data_pages)
{
  LOG_CS_ENTER (thread_p);

  if (log_Gl.trantable.area == NULL || !logpb_is_initialize_pool ())
    {
      LOG_CS_EXIT (thread_p);
      return;
    }

  if (flush_log != false || flush_data_pages != false)
    {
      logpb_flush_pages_direct (thread_p);
    }

  if (flush_data_pages)
    {
      (void) pgbuf_flush_all (thread_p, NULL_VOLID);
      (void) fileio_synchronize_all (thread_p, false);
    }

  /* Undefine log buffer pool and transaction table */

  logpb_finalize_pool ();
  logtb_undefine_trantable (thread_p);

  LOG_CS_EXIT (thread_p);

  LOG_SET_CURRENT_TRAN_INDEX (thread_p, NULL_TRAN_INDEX);
}
#endif /* ENABLE_UNUSED_FUNCTION */


/*
 * log_active_log_header_start_scan () - 
 *   return: NO_ERROR, or ER_code
 *
 *   thread_p(in): 
 *   show_type(in):
 *   arg_values(in):
 *   arg_cnt(in):
 *   ptr(out): allocate new context. should free by end_scan() function
 */
int
log_active_log_header_start_scan (THREAD_ENTRY * thread_p, int show_type,
				  DB_VALUE ** arg_values, int arg_cnt,
				  void **ptr)
{
  int error = NO_ERROR;
  int idx_val = 0;
  const char *path;
  int fd = -1;
  ACTIVE_LOG_HEADER_SCAN_CTX *ctx = NULL;

  *ptr = NULL;

  assert (arg_cnt == 1);

  ctx = (ACTIVE_LOG_HEADER_SCAN_CTX *)
    db_private_alloc (thread_p, sizeof (ACTIVE_LOG_HEADER_SCAN_CTX));

  if (ctx == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto exit_on_error;
    }

  /* In the case of omit file path, first argument is db null */
  if (DB_VALUE_TYPE (arg_values[0]) == DB_TYPE_NULL)
    {
      LOG_CS_ENTER_READ_MODE (thread_p);
      memcpy (&ctx->header, &log_Gl.hdr, sizeof (LOG_HEADER));
      LOG_CS_EXIT (thread_p);
    }
  else
    {
      char buf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT];
      LOG_PAGE *page_hdr = (LOG_PAGE *) PTR_ALIGN (buf, MAX_ALIGNMENT);

      assert (DB_VALUE_TYPE (arg_values[0]) == DB_TYPE_CHAR);
      path = db_get_string (arg_values[0]);

      fd = fileio_open (path, O_RDONLY, 0);
      if (fd == -1)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			       ER_IO_MOUNT_FAIL, 1, path);
	  error = ER_IO_MOUNT_FAIL;
	  goto exit_on_error;
	}

      if (read (fd, page_hdr, LOG_PAGESIZE) != LOG_PAGESIZE)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			       ER_IO_MOUNT_FAIL, 1, path);
	  error = ER_IO_MOUNT_FAIL;
	  goto exit_on_error;
	}

      memcpy (&ctx->header, page_hdr->area, sizeof (LOG_HEADER));

      ctx->header.magic[sizeof (ctx->header.magic) - 1] = 0;
      ctx->header.db_release[sizeof (ctx->header.db_release) - 1] = 0;
      ctx->header.prefix_name[sizeof (ctx->header.prefix_name) - 1] = 0;

      close (fd);
      fd = -1;

      if (memcmp
	  (ctx->header.magic, CUBRID_MAGIC_LOG_ACTIVE,
	   strlen (CUBRID_MAGIC_LOG_ACTIVE)) != 0)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_MOUNT_FAIL, 1,
		  path);
	  error = ER_IO_MOUNT_FAIL;
	  goto exit_on_error;
	}
    }

  *ptr = ctx;
  ctx = NULL;

exit_on_error:
  if (fd != -1)
    {
      close (fd);
    }

  if (ctx != NULL)
    {
      db_private_free_and_init (thread_p, ctx);
    }

  return error;
}

/*
 * log_active_log_header_next_scan () - 
 *   return: NO_ERROR, or ER_code
 *
 *   thread_p(in): 
 *   cursor(in):
 *   out_values(in):
 *   out_cnt(in):
 *   ptr(in): context pointer
 */
SCAN_CODE
log_active_log_header_next_scan (THREAD_ENTRY * thread_p, int cursor,
				 DB_VALUE ** out_values, int out_cnt,
				 void *ptr)
{
  int error = NO_ERROR;
  int idx = 0;
  int val;
  const char *str;
  char buf[256];
  DB_DATETIME time_val;
  ACTIVE_LOG_HEADER_SCAN_CTX *ctx = (ACTIVE_LOG_HEADER_SCAN_CTX *) ptr;
  LOG_HEADER *header = &ctx->header;

  if (cursor >= 1)
    {
      return S_END;
    }

  db_make_int (out_values[idx], LOG_DBLOG_ACTIVE_VOLID);
  idx++;

  error = db_make_string_copy (out_values[idx], header->magic);
  idx++;

  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  val = offsetof (LOG_PAGE, area) + offsetof (LOG_HEADER, magic);
  db_make_int (out_values[idx], val);
  idx++;

  db_localdatetime ((time_t *) (&header->db_creation), &time_val);
  error = db_make_datetime (out_values[idx], &time_val);
  idx++;
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  error = db_make_string_copy (out_values[idx], header->db_release);
  idx++;

  snprintf (buf, sizeof (buf), "%g", header->db_compatibility);
  buf[sizeof (buf) - 1] = 0;
  error = db_make_string_copy (out_values[idx], buf);
  idx++;
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  db_make_int (out_values[idx], header->db_iopagesize);
  idx++;

  db_make_int (out_values[idx], header->db_logpagesize);
  idx++;

  db_make_int (out_values[idx], header->is_shutdown);
  idx++;

  db_make_int (out_values[idx], header->next_trid);
  idx++;

  db_make_int (out_values[idx], header->avg_ntrans);
  idx++;

  db_make_int (out_values[idx], header->avg_nlocks);
  idx++;

  db_make_int (out_values[idx], header->npages);
  idx++;

  db_make_int (out_values[idx], header->db_charset);
  idx++;

  db_make_bigint (out_values[idx], header->fpageid);
  idx++;

  lsa_to_string (buf, sizeof (buf), &header->append_lsa);
  error = db_make_string_copy (out_values[idx], buf);
  idx++;
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  lsa_to_string (buf, sizeof (buf), &header->chkpt_lsa);
  error = db_make_string_copy (out_values[idx], buf);
  idx++;
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  db_make_bigint (out_values[idx], header->nxarv_pageid);
  idx++;

  db_make_int (out_values[idx], header->nxarv_phy_pageid);
  idx++;

  db_make_int (out_values[idx], header->nxarv_num);
  idx++;

  db_make_int (out_values[idx], header->last_arv_num_for_syscrashes);
  idx++;

  db_make_int (out_values[idx], header->last_deleted_arv_num);
  idx++;

  lsa_to_string (buf, sizeof (buf), &header->bkup_level0_lsa);
  error = db_make_string_copy (out_values[idx], buf);
  idx++;
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  lsa_to_string (buf, sizeof (buf), &header->bkup_level1_lsa);
  error = db_make_string_copy (out_values[idx], buf);
  idx++;
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  lsa_to_string (buf, sizeof (buf), &header->bkup_level2_lsa);
  error = db_make_string_copy (out_values[idx], buf);
  idx++;
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  error = db_make_string_copy (out_values[idx], header->prefix_name);
  idx++;
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  db_make_int (out_values[idx], header->has_logging_been_skipped);
  idx++;

  str = logpb_perm_status_to_string (header->perm_status);
  db_make_string (out_values[idx], str);
  idx++;

  logpb_backup_level_info_to_string (buf, sizeof (buf),
				     header->bkinfo +
				     FILEIO_BACKUP_FULL_LEVEL);
  error = db_make_string_copy (out_values[idx], buf);
  idx++;
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  logpb_backup_level_info_to_string (buf, sizeof (buf),
				     header->bkinfo +
				     FILEIO_BACKUP_BIG_INCREMENT_LEVEL);
  error = db_make_string_copy (out_values[idx], buf);
  idx++;
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  logpb_backup_level_info_to_string (buf, sizeof (buf),
				     header->bkinfo +
				     FILEIO_BACKUP_SMALL_INCREMENT_LEVEL);
  error = db_make_string_copy (out_values[idx], buf);
  idx++;
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  str = css_ha_server_state_string (header->ha_server_state);
  db_make_string (out_values[idx], str);
  idx++;

  str = logwr_log_ha_filestat_to_string (header->ha_file_status);
  db_make_string (out_values[idx], str);
  idx++;

  lsa_to_string (buf, sizeof (buf), &header->eof_lsa);
  error = db_make_string_copy (out_values[idx], buf);
  idx++;
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  lsa_to_string (buf, sizeof (buf), &header->smallest_lsa_at_last_chkpt);
  error = db_make_string_copy (out_values[idx], buf);
  idx++;
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  assert (idx == out_cnt);

  return S_SUCCESS;

exit_on_error:
  return error == NO_ERROR ? S_SUCCESS : S_ERROR;
}

/*
 * log_active_log_header_end_scan () - free the context
 *   return: NO_ERROR, or ER_code
 *
 *   thread_p(in): 
 *   ptr(in): context pointer
 */
int
log_active_log_header_end_scan (THREAD_ENTRY * thread_p, void **ptr)
{
  if (*ptr != NULL)
    {
      db_private_free_and_init (thread_p, *ptr);
    }

  return NO_ERROR;
}

/*
 * log_archive_log_header_start_scan () - 
 *   return: NO_ERROR, or ER_code
 *
 *   thread_p(in): 
 *   show_type(in):
 *   arg_values(in):
 *   arg_cnt(in):
 *   ptr(out): allocate new context. should free by end_scan() function
 */
int
log_archive_log_header_start_scan (THREAD_ENTRY * thread_p, int show_type,
				   DB_VALUE ** arg_values, int arg_cnt,
				   void **ptr)
{
  int idx = 0, error = NO_ERROR;
  const char *path;
  int fd;
  char buf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT];
  LOG_PAGE *page_hdr;
  ARCHIVE_LOG_HEADER_SCAN_CTX *ctx = NULL;

  *ptr = NULL;

  assert (DB_VALUE_TYPE (arg_values[0]) == DB_TYPE_CHAR);

  ctx = (ARCHIVE_LOG_HEADER_SCAN_CTX *)
    db_private_alloc (thread_p, sizeof (ARCHIVE_LOG_HEADER_SCAN_CTX));
  if (ctx == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto exit_on_error;
    }

  path = db_get_string (arg_values[0]);

  page_hdr = (LOG_PAGE *) PTR_ALIGN (buf, MAX_ALIGNMENT);

  fd = fileio_open (path, O_RDONLY, 0);
  if (fd == -1)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_MOUNT_FAIL,
			   1, path);
      error = ER_IO_MOUNT_FAIL;
      goto exit_on_error;
    }

  if (read (fd, page_hdr, LOG_PAGESIZE) != LOG_PAGESIZE)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_MOUNT_FAIL,
			   1, path);
      error = ER_IO_MOUNT_FAIL;
      goto exit_on_error;
    }

  memcpy (&ctx->header, page_hdr->area, sizeof (struct log_arv_header));
  close (fd);
  fd = -1;

  ctx->header.magic[sizeof (ctx->header.magic) - 1] = 0;

  if (memcmp
      (ctx->header.magic, CUBRID_MAGIC_LOG_ARCHIVE,
       strlen (CUBRID_MAGIC_LOG_ARCHIVE)) != 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_MOUNT_FAIL, 1, path);
      error = ER_IO_MOUNT_FAIL;
      goto exit_on_error;
    }

  *ptr = ctx;
  ctx = NULL;

exit_on_error:
  if (ctx != NULL)
    {
      db_private_free_and_init (thread_p, ctx);
    }

  return error;
}

/*
 * log_archive_log_header_next_scan () - 
 *   return: NO_ERROR, or ER_code
 *
 *   thread_p(in): 
 *   cursor(in):
 *   out_values(in):
 *   out_cnt(in):
 *   ptr(in): context pointer
 */
SCAN_CODE
log_archive_log_header_next_scan (THREAD_ENTRY * thread_p, int cursor,
				  DB_VALUE ** out_values, int out_cnt,
				  void *ptr)
{
  int error = NO_ERROR;
  int idx = 0;
  int val;
  DB_DATETIME time_val;

  ARCHIVE_LOG_HEADER_SCAN_CTX *ctx = (ARCHIVE_LOG_HEADER_SCAN_CTX *) ptr;
  struct log_arv_header *header = &ctx->header;

  if (cursor >= 1)
    {
      return S_END;
    }

  db_make_int (out_values[idx], LOG_DBLOG_ARCHIVE_VOLID);
  idx++;

  error = db_make_string_copy (out_values[idx], header->magic);
  idx++;

  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  val = offsetof (LOG_PAGE, area) + offsetof (struct log_arv_header, magic);
  db_make_int (out_values[idx], val);
  idx++;

  db_localdatetime ((time_t *) (&header->db_creation), &time_val);
  error = db_make_datetime (out_values[idx], &time_val);
  idx++;
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  db_make_bigint (out_values[idx], header->next_trid);
  idx++;

  db_make_int (out_values[idx], header->npages);
  idx++;

  db_make_bigint (out_values[idx], header->fpageid);
  idx++;

  db_make_int (out_values[idx], header->arv_num);
  idx++;

  assert (idx == out_cnt);

exit_on_error:
  return error == NO_ERROR ? S_SUCCESS : S_ERROR;
}

/*
 * log_archive_log_header_end_scan () - free the context
 *   return: NO_ERROR, or ER_code
 *
 *   thread_p(in): 
 *   ptr(in): context pointer
 */
int
log_archive_log_header_end_scan (THREAD_ENTRY * thread_p, void **ptr)
{
  if (*ptr != NULL)
    {
      db_private_free_and_init (thread_p, *ptr);
    }

  return NO_ERROR;
}
