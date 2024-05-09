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

#include <cstdint>

#include "log_manager.h"

#include "btree.h"
#include "elo.h"
#include "recovery.h"
#include "replication.h"
#include "xserver_interface.h"
#include "page_buffer.h"
#include "porting_inline.hpp"
#include "query_manager.h"
#include "message_catalog.h"
#include "msgcat_set_log.hpp"
#include "environment_variable.h"
#if defined(SERVER_MODE)
#include "server_support.h"
#endif /* SERVER_MODE */
#include "log_append.hpp"
#include "log_archives.hpp"
#include "log_compress.h"
#include "log_record.hpp"
#include "log_system_tran.hpp"
#include "log_volids.hpp"
#include "log_writer.h"
#include "partition_sr.h"
#include "filter_pred_cache.h"
#include "heap_file.h"
#include "slotted_page.h"
#include "object_primitive.h"
#include "object_representation.h"
#include "tz_support.h"
#include "db_date.h"
#include "fault_injection.h"
#if defined (SA_MODE)
#include "connection_support.h"
#endif /* defined (SA_MODE) */
#include "db_value_printer.hpp"
#include "mem_block.hpp"
#include "string_buffer.hpp"
#include "boot_sr.h"
#include "thread_daemon.hpp"
#include "thread_entry.hpp"
#include "thread_entry_task.hpp"
#include "thread_manager.hpp"
#include "transaction_transient.hpp"
#include "vacuum.h"
#include "xasl_cache.h"
#include "overflow_file.h"
#include "dbtype.h"
#include "cnv.h"
#include "flashback.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

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
   (((RCVI) != RVBT_MVCC_INCREMENTS_UPD) \
    && ((RCVI) != RVBT_LOG_GLOBAL_UNIQUE_STATS_COMMIT) \
    && ((RCVI) != RVBT_REMOVE_UNIQUE_STATS) \
    && ((RCVI) != RVLOC_CLASSNAME_DUMMY) \
    && ((RCVI) != RVDK_LINK_PERM_VOLEXT || !pgbuf_is_lsa_temporary(PGPTR)))

#define CDC_IS_IGNORE_LOGINFO_ERROR(ERROR) \
   (((ERROR) == ER_CDC_IGNORE_LOG_INFO) \
    || ((ERROR) == ER_CDC_IGNORE_LOG_INFO_INTERNAL) \
    || ((ERROR) == ER_CDC_IGNORE_TRANSACTION))

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
  LOG_ARV_HEADER header;
};

CDC_GLOBAL cdc_Gl;
bool cdc_Logging = false;
/* CDC end */

/*
 * The maximum number of times to try to undo a log record.
 * It is only used by the log_undo_rec_restartable() function.
 */
static const int LOG_REC_UNDO_MAX_ATTEMPTS = 3;

/* true: Skip logging, false: Don't skip logging */
static bool log_No_logging = false;

#define LOG_TDES_LAST_SYSOP(tdes) (&(tdes)->topops.stack[(tdes)->topops.last])
#define LOG_TDES_LAST_SYSOP_PARENT_LSA(tdes) (&LOG_TDES_LAST_SYSOP(tdes)->lastparent_lsa)
#define LOG_TDES_LAST_SYSOP_POSP_LSA(tdes) (&LOG_TDES_LAST_SYSOP(tdes)->posp_lsa)

#if defined (SERVER_MODE)
/* Current time in milliseconds */
// *INDENT-OFF*
std::atomic<std::int64_t> log_Clock_msec = {0};
// *INDENT-ON*
#endif /* SERVER_MODE */

static bool log_verify_dbcreation (THREAD_ENTRY * thread_p, VOLID volid, const INT64 * log_dbcreation);
static int log_create_internal (THREAD_ENTRY * thread_p, const char *db_fullname, const char *logpath,
				const char *prefix_logname, DKNPAGES npages, INT64 * db_creation);
static int log_initialize_internal (THREAD_ENTRY * thread_p, const char *db_fullname, const char *logpath,
				    const char *prefix_logname, bool ismedia_crash, BO_RESTART_ARG * r_args,
				    bool init_emergency);
#if defined(SERVER_MODE)
static int log_abort_by_tdes (THREAD_ENTRY * thread_p, LOG_TDES * tdes);
#endif /* SERVER_MODE */
static LOG_LSA *log_get_savepoint_lsa (THREAD_ENTRY * thread_p, const char *savept_name, LOG_TDES * tdes,
				       LOG_LSA * savept_lsa);
static bool log_can_skip_undo_logging (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex, const LOG_TDES * tdes,
				       LOG_DATA_ADDR * addr);
static bool log_can_skip_redo_logging (LOG_RCVINDEX rcvindex, const LOG_TDES * ignore_tdes, LOG_DATA_ADDR * addr);
static void log_append_commit_postpone (THREAD_ENTRY * thread_p, LOG_TDES * tdes, LOG_LSA * start_postpone_lsa);
static void log_append_commit_postpone_obsolete (THREAD_ENTRY * thread_p, LOG_TDES * tdes,
						 LOG_LSA * start_postpone_lsa);
static void log_append_sysop_start_postpone (THREAD_ENTRY * thread_p, LOG_TDES * tdes,
					     LOG_REC_SYSOP_START_POSTPONE * sysop_start_postpone, int data_size,
					     const char *data);
static void log_append_sysop_end (THREAD_ENTRY * thread_p, LOG_TDES * tdes, LOG_REC_SYSOP_END * sysop_end,
				  int data_size, const char *data);
static void log_append_repl_info_internal (THREAD_ENTRY * thread_p, LOG_TDES * tdes, bool is_commit, int with_lock);
static void log_append_repl_info_with_lock (THREAD_ENTRY * thread_p, LOG_TDES * tdes, bool is_commit);
static void log_append_repl_info_and_commit_log (THREAD_ENTRY * thread_p, LOG_TDES * tdes, LOG_LSA * commit_lsa);
static void log_append_donetime_internal (THREAD_ENTRY * thread_p, LOG_TDES * tdes, LOG_LSA * eot_lsa,
					  LOG_RECTYPE iscommitted, enum LOG_PRIOR_LSA_LOCK with_lock);
static void log_change_tran_as_completed (THREAD_ENTRY * thread_p, LOG_TDES * tdes, LOG_RECTYPE iscommitted,
					  LOG_LSA * lsa);
static void log_append_commit_log (THREAD_ENTRY * thread_p, LOG_TDES * tdes, LOG_LSA * commit_lsa);
static void log_append_commit_log_with_lock (THREAD_ENTRY * thread_p, LOG_TDES * tdes, LOG_LSA * commit_lsa);
static void log_append_abort_log (THREAD_ENTRY * thread_p, LOG_TDES * tdes, LOG_LSA * abort_lsa);

static void log_dump_record_header_to_string (LOG_RECORD_HEADER * log, char *buf, size_t len);
static void log_ascii_dump (FILE * out_fp, int length, void *data);
static void log_hexa_dump (FILE * out_fp, int length, void *data);
static void log_dump_data (THREAD_ENTRY * thread_p, FILE * out_fp, int length, LOG_LSA * log_lsa, LOG_PAGE * log_page_p,
			   void (*dumpfun) (FILE * fp, int, void *), LOG_ZIP * log_dump_ptr);
static void log_dump_header (FILE * out_fp, LOG_HEADER * log_header_p);
static LOG_PAGE *log_dump_record_undoredo (THREAD_ENTRY * thread_p, FILE * out_fp, LOG_LSA * lsa_p,
					   LOG_PAGE * log_page_p, LOG_ZIP * log_zip_p);
static LOG_PAGE *log_dump_record_undo (THREAD_ENTRY * thread_p, FILE * out_fp, LOG_LSA * lsa_p, LOG_PAGE * log_page_p,
				       LOG_ZIP * log_zip_p);
static LOG_PAGE *log_dump_record_redo (THREAD_ENTRY * thread_p, FILE * out_fp, LOG_LSA * lsa_p, LOG_PAGE * log_page_p,
				       LOG_ZIP * log_zip_p);
static LOG_PAGE *log_dump_record_mvcc_undoredo (THREAD_ENTRY * thread_p, FILE * out_fp, LOG_LSA * lsa_p,
						LOG_PAGE * log_page_p, LOG_ZIP * log_zip_p);
static LOG_PAGE *log_dump_record_mvcc_undo (THREAD_ENTRY * thread_p, FILE * out_fp, LOG_LSA * lsa_p,
					    LOG_PAGE * log_page_p, LOG_ZIP * log_zip_p);
static LOG_PAGE *log_dump_record_mvcc_redo (THREAD_ENTRY * thread_p, FILE * out_fp, LOG_LSA * lsa_p,
					    LOG_PAGE * log_page_p, LOG_ZIP * log_zip_p);
static LOG_PAGE *log_dump_record_postpone (THREAD_ENTRY * thread_p, FILE * out_fp, LOG_LSA * lsa_p,
					   LOG_PAGE * log_page_p);
static LOG_PAGE *log_dump_record_dbout_redo (THREAD_ENTRY * thread_p, FILE * out_fp, LOG_LSA * lsa_p,
					     LOG_PAGE * log_page_p);
static LOG_PAGE *log_dump_record_compensate (THREAD_ENTRY * thread_p, FILE * out_fp, LOG_LSA * lsa_p,
					     LOG_PAGE * log_page_p);
static LOG_PAGE *log_dump_record_commit_postpone (THREAD_ENTRY * thread_p, FILE * out_fp, LOG_LSA * lsa_p,
						  LOG_PAGE * log_page_p);
static LOG_PAGE *log_dump_record_transaction_finish (THREAD_ENTRY * thread_p, FILE * out_fp, LOG_LSA * lsa_p,
						     LOG_PAGE * log_page_p);
static LOG_PAGE *log_dump_record_replication (THREAD_ENTRY * thread_p, FILE * out_fp, LOG_LSA * lsa_p,
					      LOG_PAGE * log_page_p);
static LOG_PAGE *log_dump_record_sysop_start_postpone (THREAD_ENTRY * thread_p, FILE * out_fp, LOG_LSA * lsa_p,
						       LOG_PAGE * log_page_p, LOG_ZIP * log_zip_p);
static LOG_PAGE *log_dump_record_sysop_end (THREAD_ENTRY * thread_p, LOG_LSA * log_lsa, LOG_PAGE * log_page_p,
					    LOG_ZIP * log_zip_p, FILE * out_fp);
static LOG_PAGE *log_dump_record_sysop_end_internal (THREAD_ENTRY * thread_p, LOG_REC_SYSOP_END * sysop_end,
						     LOG_LSA * log_lsa, LOG_PAGE * log_page_p, LOG_ZIP * log_zip_p,
						     FILE * out_fp);
static LOG_PAGE *log_dump_record_checkpoint (THREAD_ENTRY * thread_p, FILE * out_fp, LOG_LSA * lsa_p,
					     LOG_PAGE * log_page_p);
static void log_dump_checkpoint_topops (FILE * out_fp, int length, void *data);
static LOG_PAGE *log_dump_record_save_point (THREAD_ENTRY * thread_p, FILE * out_fp, LOG_LSA * lsa_p,
					     LOG_PAGE * log_page_p);
static LOG_PAGE *log_dump_record_2pc_prepare_commit (THREAD_ENTRY * thread_p, FILE * out_fp, LOG_LSA * lsa_p,
						     LOG_PAGE * log_page_p);
static LOG_PAGE *log_dump_record_2pc_start (THREAD_ENTRY * thread_p, FILE * out_fp, LOG_LSA * lsa_p,
					    LOG_PAGE * log_page_p);
static LOG_PAGE *log_dump_record_2pc_acknowledgement (THREAD_ENTRY * thread_p, FILE * out_fp, LOG_LSA * lsa_p,
						      LOG_PAGE * log_page_p);
static LOG_PAGE *log_dump_record_ha_server_state (THREAD_ENTRY * thread_p, FILE * out_fp, LOG_LSA * log_lsa,
						  LOG_PAGE * log_page_p);
static LOG_PAGE *log_dump_record_supplemental_info (THREAD_ENTRY * thread_p, FILE * out_fp, LOG_LSA * log_lsa,
						    LOG_PAGE * log_page_p);
static LOG_PAGE *log_dump_record (THREAD_ENTRY * thread_p, FILE * out_fp, LOG_RECTYPE record_type, LOG_LSA * lsa_p,
				  LOG_PAGE * log_page_p, LOG_ZIP * log_zip_p);
static void log_rollback_record (THREAD_ENTRY * thread_p, LOG_LSA * log_lsa, LOG_PAGE * log_page_p,
				 LOG_RCVINDEX rcvindex, VPID * rcv_vpid, LOG_RCV * rcv, LOG_TDES * tdes,
				 LOG_ZIP * log_unzip_ptr);
static int log_undo_rec_restartable (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex, LOG_RCV * rcv);
static void log_rollback (THREAD_ENTRY * thread_p, LOG_TDES * tdes, const LOG_LSA * upto_lsa_ptr);
static int log_run_postpone_op (THREAD_ENTRY * thread_p, LOG_LSA * log_lsa, LOG_PAGE * log_pgptr);
static void log_find_end_log (THREAD_ENTRY * thread_p, LOG_LSA * end_lsa);

static void log_cleanup_modified_class (const tx_transient_class_entry & t, bool & stop);
static void log_cleanup_modified_class_list (THREAD_ENTRY * thread_p, LOG_TDES * tdes, LOG_LSA * savept_lsa,
					     bool release, bool decache_classrepr);

static void log_append_compensate_internal (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex, const VPID * vpid,
					    PGLENGTH offset, PAGE_PTR pgptr, int length, const void *data,
					    LOG_TDES * tdes, const LOG_LSA * undo_nxlsa);

STATIC_INLINE void log_sysop_end_random_exit (THREAD_ENTRY * thread_p) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void log_sysop_end_begin (THREAD_ENTRY * thread_p, int *tran_index_out, LOG_TDES ** tdes_out)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void log_sysop_end_unstack (THREAD_ENTRY * thread_p, LOG_TDES * tdes) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void log_sysop_end_final (THREAD_ENTRY * thread_p, LOG_TDES * tdes) __attribute__ ((ALWAYS_INLINE));
static void log_sysop_commit_internal (THREAD_ENTRY * thread_p, LOG_REC_SYSOP_END * log_record, int data_size,
				       const char *data, bool is_rv_finish_postpone);
STATIC_INLINE void log_sysop_get_tran_index_and_tdes (THREAD_ENTRY * thread_p, int *tran_index_out,
						      LOG_TDES ** tdes_out) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int log_sysop_get_level (THREAD_ENTRY * thread_p) __attribute__ ((ALWAYS_INLINE));

static void log_tran_do_postpone (THREAD_ENTRY * thread_p, LOG_TDES * tdes);
static void log_sysop_do_postpone (THREAD_ENTRY * thread_p, LOG_TDES * tdes, LOG_REC_SYSOP_END * sysop_end,
				   int data_size, const char *data);

static int logtb_tran_update_stats_online_index_rb (THREAD_ENTRY * thread_p, void *data, void *args);

/*for CDC */
static int cdc_log_extract (THREAD_ENTRY * thread_p, LOG_LSA * process_lsa, CDC_LOGINFO_ENTRY * log_info_entry);
static int cdc_get_overflow_recdes (THREAD_ENTRY * thread_p, LOG_PAGE * log_page_p, RECDES * recdes,
				    LOG_LSA lsa, LOG_RCVINDEX rcvindex, bool is_redo);
static int cdc_get_ovfdata_from_log (THREAD_ENTRY * thread_p, LOG_PAGE * log_page_p, LOG_LSA * process_lsa, int *length,
				     char **data, LOG_RCVINDEX rcvindex, bool is_redo);
static int cdc_find_primary_key (THREAD_ENTRY * thread_p, OID classoid, int repr_id, int *num_attr, int **pk_attr_id);
static int cdc_make_ddl_loginfo (char *supplement_data, int trid, const char *user, CDC_LOGINFO_ENTRY * ddl_entry);
static int cdc_make_dcl_loginfo (time_t at_time, int trid, char *user, int log_type, CDC_LOGINFO_ENTRY * dcl_entry);
static int cdc_make_timer_loginfo (time_t at_time, int trid, char *user, CDC_LOGINFO_ENTRY * timer_entry);
static int cdc_find_user (THREAD_ENTRY * thread_p, LOG_LSA lsa, int trid, char **user);
static int cdc_compare_undoredo_dbvalue (const db_value * new_value, const db_value * old_value);
static int cdc_put_value_to_loginfo (db_value * new_value, char **ptr);

static int cdc_get_start_point_from_file (THREAD_ENTRY * thread_p, int arv_num, LOG_LSA * ret_lsa, time_t * time);
static int cdc_get_lsa_with_start_point (THREAD_ENTRY * thread_p, time_t * time, LOG_LSA * start_lsa);

static bool cdc_is_filtered_class (OID classoid);
static bool cdc_is_filtered_user (char *user);

#if defined(SERVER_MODE)
// *INDENT-OFF*
static void log_abort_task_execute (cubthread::entry &thread_ref, LOG_TDES &tdes);
// *INDENT-ON*
#endif // SERVER_MODE

#if defined(SERVER_MODE)
// *INDENT-OFF*
static cubthread::daemon *log_Clock_daemon = NULL;
static cubthread::daemon *log_Checkpoint_daemon = NULL;
static cubthread::daemon *log_Remove_log_archive_daemon = NULL;
static cubthread::daemon *log_Check_ha_delay_info_daemon = NULL;

static cubthread::daemon *log_Flush_daemon = NULL;
static std::atomic_bool log_Flush_has_been_requested = {false};

static cubthread::daemon *cdc_Loginfo_producer_daemon = NULL;
// *INDENT-ON*

static void log_daemons_init ();
static void log_daemons_destroy ();

// used by log_Check_ha_delay_info_daemon
extern int catcls_get_apply_info_log_record_time (THREAD_ENTRY * thread_p, time_t * log_record_time);
#endif /* SERVER_MODE */

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

    case LOG_COMMIT_WITH_POSTPONE:
      return "LOG_COMMIT_WITH_POSTPONE";

    case LOG_COMMIT_WITH_POSTPONE_OBSOLETE:
      return "LOG_COMMIT_WITH_POSTPONE_OBSOLETE";

    case LOG_COMMIT:
      return "LOG_COMMIT";

    case LOG_SYSOP_START_POSTPONE:
      return "LOG_SYSOP_START_POSTPONE";

    case LOG_SYSOP_END:
      return "LOG_SYSOP_END";

    case LOG_ABORT:
      return "LOG_ABORT";

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

    case LOG_END_OF_LOG:
      return "LOG_END_OF_LOG";

    case LOG_REPLICATION_DATA:
      return "LOG_REPLICATION_DATA";
    case LOG_REPLICATION_STATEMENT:
      return "LOG_REPLICATION_STATEMENT";

    case LOG_SYSOP_ATOMIC_START:
      return "LOG_SYSOP_ATOMIC_START";

    case LOG_DUMMY_HA_SERVER_STATE:
      return "LOG_DUMMY_HA_SERVER_STATE";
    case LOG_DUMMY_OVF_RECORD:
      return "LOG_DUMMY_OVF_RECORD";
    case LOG_DUMMY_GENERIC:
      return "LOG_DUMMY_GENERIC";
    case LOG_SUPPLEMENTAL_INFO:
      return "LOG_SUPPLEMENTAL_INFO";
    case LOG_SMALLER_LOGREC_TYPE:
    case LOG_LARGER_LOGREC_TYPE:
      break;

    default:
      assert (false);
      break;
    }

  return "UNKNOWN_LOG_REC_TYPE";

}

/*
 * log_is_in_crash_recovery - are we in crash recovery ?
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
 * log_is_in_crash_recovery_and_not_year_complets_redo - completes redo recovery?
 *
 * return:
 *
 */
bool
log_is_in_crash_recovery_and_not_yet_completes_redo (void)
{
  if (log_Gl.rcv_phase == LOG_RECOVERY_ANALYSIS_PHASE || log_Gl.rcv_phase == LOG_RECOVERY_REDO_PHASE)
    {
      return true;
    }
  else
    {
      return false;
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
		    "log_find_crash_point_lsa: Warning, only expected to be called during recovery phases.");
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
log_verify_dbcreation (THREAD_ENTRY * thread_p, VOLID volid, const INT64 * log_dbcreation)
{
  INT64 vol_dbcreation;		/* Database creation time in volume */

  if (disk_get_creation_time (thread_p, volid, &vol_dbcreation) != NO_ERROR)
    {
      return false;
    }

  if (difftime ((time_t) vol_dbcreation, (time_t) (*log_dbcreation)) == 0)
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
 * return:
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
log_create (THREAD_ENTRY * thread_p, const char *db_fullname, const char *logpath, const char *prefix_logname,
	    DKNPAGES npages)
{
  int error_code = NO_ERROR;
  INT64 db_creation;

  db_creation = time (NULL);
  if (db_creation == -1)
    {
      error_code = ER_FAILED;
      return error_code;
    }

  error_code = log_create_internal (thread_p, db_fullname, logpath, prefix_logname, npages, &db_creation);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

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
static int
log_create_internal (THREAD_ENTRY * thread_p, const char *db_fullname, const char *logpath, const char *prefix_logname,
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
  error_code = logpb_initialize_pool (thread_p);
  if (error_code != NO_ERROR)
    {
      goto error;
    }
  error_code = logpb_initialize_log_names (thread_p, db_fullname, logpath, prefix_logname);
  if (error_code != NO_ERROR)
    {
      goto error;
    }

  logpb_decache_archive_info (thread_p);

  log_Gl.rcv_phase = LOG_RECOVERY_ANALYSIS_PHASE;

  /* Initialize the log header */
  error_code = logpb_initialize_header (thread_p, &log_Gl.hdr, prefix_logname, npages, db_creation);
  if (error_code != NO_ERROR)
    {
      goto error;
    }

  loghdr_pgptr = logpb_create_header_page (thread_p);

  /*
   * Format the volume and fetch the header page and the first append page
   */
  log_Gl.append.vdes =
    fileio_format (thread_p, db_fullname, log_Name_active, LOG_DBLOG_ACTIVE_VOLID, npages,
		   prm_get_bool_value (PRM_ID_LOG_SWEEP_CLEAN), true, false, LOG_PAGESIZE, 0, false);
  if (log_Gl.append.vdes == NULL_VOLDES || logpb_fetch_start_append_page (thread_p) != NO_ERROR || loghdr_pgptr == NULL)
    {
      goto error;
    }

  LSA_SET_NULL (&log_Gl.append.prev_lsa);
  /* copy log_Gl.append.prev_lsa to log_Gl.prior_info.prev_lsa */
  LOG_RESET_PREV_LSA (&log_Gl.append.prev_lsa);

  /*
   * Flush the append page, so that the end of the log mark is written.
   * Then, free the page, same for the header page.
   */
  logpb_set_dirty (thread_p, log_Gl.append.log_pgptr);
  logpb_flush_pages_direct (thread_p);

  log_Gl.chkpt_every_npages = prm_get_integer_value (PRM_ID_LOG_CHECKPOINT_NPAGES);

  /* Flush the log header */

  memcpy (loghdr_pgptr->area, &log_Gl.hdr, sizeof (log_Gl.hdr));
  logpb_set_dirty (thread_p, loghdr_pgptr);

#if defined(CUBRID_DEBUG)
  {
    char temp_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT], *aligned_temp_pgbuf;
    LOG_PAGE *temp_pgptr;

    aligned_temp_pgbuf = PTR_ALIGN (temp_pgbuf, MAX_ALIGNMENT);

    temp_pgptr = (LOG_PAGE *) aligned_temp_pgbuf;
    memset (temp_pgptr, 0, LOG_PAGESIZE);
    logpb_read_page_from_file (thread_p, LOGPB_HEADER_PAGE_ID, LOG_CS_FORCE_USE, temp_pgptr);
    assert (memcmp ((LOG_HEADER *) temp_pgptr->area, &log_Gl.hdr, sizeof (log_Gl.hdr)) != 0);
  }
#endif /* CUBRID_DEBUG */

  error_code = logpb_flush_page (thread_p, loghdr_pgptr);
  if (error_code != NO_ERROR)
    {
      goto error;
    }
#if defined(CUBRID_DEBUG)
  {
    char temp_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT], *aligned_temp_pgbuf;
    LOG_PAGE *temp_pgptr;

    aligned_temp_pgbuf = PTR_ALIGN (temp_pgbuf, MAX_ALIGNMENT);

    temp_pgptr = (LOG_PAGE *) aligned_temp_pgbuf;
    memset (temp_pgptr, 0, LOG_PAGESIZE);
    logpb_read_page_from_file (thread_p, LOGPB_HEADER_PAGE_ID, LOG_CS_FORCE_USE, temp_pgptr);
    assert (memcmp ((LOG_HEADER *) temp_pgptr->area, &log_Gl.hdr, sizeof (log_Gl.hdr)) == 0);
  }
#endif /* CUBRID_DEBUG */

  /* logpb_flush_header(); */

  /*
   * Free the append and header page and dismount the lg active volume
   */
  log_Gl.append.log_pgptr = NULL;

  fileio_dismount (thread_p, log_Gl.append.vdes);

  error_code = logpb_create_volume_info (NULL);
  if (error_code != NO_ERROR)
    {
      goto error;
    }

  /* Create the information file to append log info stuff to the DBA */
  logpb_create_log_info (log_Name_info, NULL);

  catmsg = msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOG, MSGCAT_LOG_LOGINFO_ACTIVE);
  if (catmsg == NULL)
    {
      catmsg = "ACTIVE: %s %d pages\n";
    }
  error_code = log_dump_log_info (log_Name_info, false, catmsg, log_Name_active, npages);
  if (error_code == NO_ERROR || error_code == ER_LOG_MOUNT_FAIL)
    {
      volid1 = logpb_add_volume (NULL, LOG_DBLOG_BKUPINFO_VOLID, log_Name_bkupinfo, DISK_UNKNOWN_PURPOSE);
      if (volid1 == LOG_DBLOG_BKUPINFO_VOLID)
	{
	  volid2 = logpb_add_volume (NULL, LOG_DBLOG_ACTIVE_VOLID, log_Name_active, DISK_UNKNOWN_PURPOSE);
	}

      if (volid1 != LOG_DBLOG_BKUPINFO_VOLID || volid2 != LOG_DBLOG_ACTIVE_VOLID)
	{
	  goto error;
	}
    }

  logpb_finalize_pool (thread_p);
  LOG_CS_EXIT (thread_p);

  return NO_ERROR;

error:
  logpb_finalize_pool (thread_p);
  LOG_CS_EXIT (thread_p);

  return (error_code == NO_ERROR) ? ER_FAILED : error_code;
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
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_THEREARE_PENDING_ACTIONS_MUST_LOG, 0);
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
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ONLY_IN_STANDALONE, 1, "no logging");
  error_code = ER_ONLY_IN_STANDALONE;
#endif /* SA_MODE */

  return error_code;
}

/*
 * This function is for an external interface
 * to process statistical information of the B-tree index header.
 * In a no logging environment, the statistical information of the B-tree index header
 * should not depend on the updated log. This is a function to check this.
 */
bool
log_is_no_logging (void)
{
  return log_No_logging;
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
log_initialize (THREAD_ENTRY * thread_p, const char *db_fullname, const char *logpath, const char *prefix_logname,
		int ismedia_crash, BO_RESTART_ARG * r_args)
{
  er_log_debug (ARG_FILE_LINE, "LOG INITIALIZE\n" "\tdb_fullname = %s \n" "\tlogpath = %s \n"
		"\tprefix_logname = %s \n" "\tismedia_crash = %d \n",
		db_fullname != NULL ? db_fullname : "(UNKNOWN)",
		logpath != NULL ? logpath : "(UNKNOWN)",
		prefix_logname != NULL ? prefix_logname : "(UNKNOWN)", ismedia_crash);

  (void) log_initialize_internal (thread_p, db_fullname, logpath, prefix_logname, ismedia_crash, r_args, false);

#if defined(SERVER_MODE)
  log_daemons_init ();
#endif // SERVER_MODE

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
 *   db_fullname(in): Full name of the database
 *   logpath(in): Directory where the log volumes reside
 *   prefix_logname(in): Name of the log volumes. It must be the same as the
 *                      one given during the creation of the database.
 *   ismedia_crash(in): Are we recovering from media crash ?.
 *   stopat(in): If we are recovering from a media crash, we can stop
 *                      the recovery process at a given time.
 *
 * NOTE:
 */
static int
log_initialize_internal (THREAD_ENTRY * thread_p, const char *db_fullname, const char *logpath,
			 const char *prefix_logname, bool ismedia_crash, BO_RESTART_ARG * r_args, bool init_emergency)
{
  LOG_RECORD_HEADER *eof;	/* End of log record */
  REL_FIXUP_FUNCTION *disk_compatibility_functions = NULL;
  REL_COMPATIBILITY compat;
  int i;
  int error_code = NO_ERROR;
  time_t *stopat = (r_args) ? &r_args->stopat : NULL;

#if !defined (NDEBUG)
  /* Make sure that the recovery function array is synchronized.. */
  rv_check_rvfuns ();
#endif /* !NDEBUG */

  (void) umask (S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);

  /* Make sure that the log is a valid one */
  LOG_SET_CURRENT_TRAN_INDEX (thread_p, LOG_SYSTEM_TRAN_INDEX);

  LOG_CS_ENTER (thread_p);

  if (log_Gl.trantable.area != NULL)
    {
      log_final (thread_p);
    }

  /* Initialize log name for log volumes */
  error_code = logpb_initialize_log_names (thread_p, db_fullname, logpath, prefix_logname);
  if (error_code != NO_ERROR)
    {
      logpb_fatal_error (thread_p, !init_emergency, ARG_FILE_LINE, "log_xinit");
      goto error;
    }
  logpb_decache_archive_info (thread_p);
  log_Gl.run_nxchkpt_atpageid = NULL_PAGEID;	/* Don't run the checkpoint */
  log_Gl.rcv_phase = LOG_RECOVERY_ANALYSIS_PHASE;

  log_Gl.loghdr_pgptr = (LOG_PAGE *) malloc (LOG_PAGESIZE);
  if (log_Gl.loghdr_pgptr == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) LOG_PAGESIZE);
      logpb_fatal_error (thread_p, !init_emergency, ARG_FILE_LINE, "log_xinit");
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      goto error;
    }
  error_code = logpb_initialize_pool (thread_p);
  if (error_code != NO_ERROR)
    {
      goto error;
    }

  /* Mount the active log and read the log header */
  log_Gl.append.vdes = fileio_mount (thread_p, db_fullname, log_Name_active, LOG_DBLOG_ACTIVE_VOLID, true, false);
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

	  error_code = logpb_initialize_header (thread_p, &log_Gl.hdr, prefix_logname, log_npages, &db_creation);
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

  if (ismedia_crash != false && (r_args) && r_args->restore_slave)
    {
      r_args->db_creation = log_Gl.hdr.db_creation;
      LSA_COPY (&r_args->restart_repl_lsa, &log_Gl.hdr.smallest_lsa_at_last_chkpt);
    }

  LSA_COPY (&log_Gl.chkpt_redo_lsa, &log_Gl.hdr.chkpt_lsa);

  /* Make sure that this is the desired log */
  if (strcmp (log_Gl.hdr.prefix_name, prefix_logname) != 0)
    {
      /*
       * This looks like the log or the log was renamed. Incompatible
       * prefix name with the prefix stored on disk
       */
      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_LOG_INCOMPATIBLE_PREFIX_NAME, 2, prefix_logname,
	      log_Gl.hdr.prefix_name);
      /* Continue anyhow */
    }

  /*
   * Make sure that we are running with the same page size. If we are not,
   * restart again since page and log buffers may reflect an incorrect
   * pagesize
   */

  if (log_Gl.hdr.db_iopagesize != IO_PAGESIZE || log_Gl.hdr.db_logpagesize != LOG_PAGESIZE)
    {
      /*
       * Pagesize is incorrect. We need to undefine anything that has been
       * created with old pagesize and start again
       */
      if (db_set_page_size (log_Gl.hdr.db_iopagesize, log_Gl.hdr.db_logpagesize) != NO_ERROR)
	{
	  /* Pagesize is incompatible */
	  error_code = ER_FAILED;
	  goto error;
	}
      /*
       * Call the function again... since we have a different setting for the
       * page size
       */
      logpb_finalize_pool (thread_p);
      fileio_dismount (thread_p, log_Gl.append.vdes);
      log_Gl.append.vdes = NULL_VOLDES;

      LOG_SET_CURRENT_TRAN_INDEX (thread_p, LOG_SYSTEM_TRAN_INDEX);
      LOG_CS_EXIT (thread_p);

      error_code = logtb_define_trantable_log_latch (thread_p, log_Gl.trantable.num_total_indices);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
      error_code = log_initialize_internal (thread_p, db_fullname, logpath, prefix_logname, ismedia_crash,
					    r_args, init_emergency);

      return error_code;
    }

  /* Make sure that the database is compatible with the CUBRID version. This will compare the given level against the
   * value returned by rel_disk_compatible(). */
  compat = rel_get_disk_compatible (log_Gl.hdr.db_compatibility, &disk_compatibility_functions);

  /* If we're not completely compatible, signal an error. There had been no compatibility rules on R2.1 or earlier
   * version. However, a compatibility rule between R2.2 and R2.1 (or earlier) was added to provide restoration from
   * R2.1 to R2.2. */
  if (compat != REL_FULLY_COMPATIBLE)
    {
      /* Database is incompatible with current release */
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_INCOMPATIBLE_DATABASE, 2, rel_name (),
	      rel_release_string ());
      error_code = ER_LOG_INCOMPATIBLE_DATABASE;
      goto error;
    }

  if (rel_is_log_compatible (log_Gl.hdr.db_release, rel_release_string ()) != true)
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
	      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_RECOVER_ON_OLD_RELEASE, 4, rel_name (),
		      log_Gl.hdr.db_release, rel_release_string (), rel_release_string ());
	      error_code = ER_LOG_RECOVER_ON_OLD_RELEASE;
	      goto error;
	    }
	}

      /*
       * It seems safe to move to new version of the system
       */

      if (strlen (rel_release_string ()) >= REL_MAX_RELEASE_LENGTH)
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_COMPILATION_RELEASE, 2, rel_release_string (),
		  REL_MAX_RELEASE_LENGTH);
	  error_code = ER_LOG_COMPILATION_RELEASE;
	  goto error;
	}
      strncpy_bufsize (log_Gl.hdr.db_release, rel_release_string ());
    }

  /*
   * Create the transaction table and make sure that data volumes and log
   * volumes belong to the same database
   */

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

  if (log_Gl.append.vdes != NULL_VOLDES)
    {
      if (fileio_map_mounted (thread_p, (bool (*)(THREAD_ENTRY *, VOLID, void *)) log_verify_dbcreation,
			      &log_Gl.hdr.db_creation) != true)
	{
	  /* The log does not belong to the given database */
	  logtb_undefine_trantable (thread_p);
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_DOESNT_CORRESPOND_TO_DATABASE, 1, log_Name_active);
	  error_code = ER_LOG_DOESNT_CORRESPOND_TO_DATABASE;
	  goto error;
	}
    }

  log_Gl.mvcc_table.reset_start_mvccid ();

  if (prm_get_bool_value (PRM_ID_FORCE_RESTART_TO_SKIP_RECOVERY))
    {
      init_emergency = true;
    }

  /*
   * Was the database system shut down or was it involved in a crash ?
   */
  if (init_emergency == false && (log_Gl.hdr.is_shutdown == false || ismedia_crash == true))
    {
      /*
       * System was involved in a crash.
       * Execute the recovery process
       */
      log_recovery (thread_p, ismedia_crash, stopat);
    }
  else
    {
      if (init_emergency == true && log_Gl.hdr.is_shutdown == false)
	{
	  if (!LSA_ISNULL (&log_Gl.hdr.eof_lsa) && LSA_GT (&log_Gl.hdr.append_lsa, &log_Gl.hdr.eof_lsa))
	    {
	      /* We cannot believe in append_lsa for this case. It points to an unflushed log page. Since we are
	       * going to skip recovery for emergency startup, just replace it with eof_lsa. */
	      LOG_RESET_APPEND_LSA (&log_Gl.hdr.eof_lsa);
	    }
	}

      /*
       * The system was shut down. There is nothing to recover.
       * Find the append page and start execution
       */
      if (logpb_fetch_start_append_page (thread_p) != NO_ERROR)
	{
	  error_code = ER_FAILED;
	  goto error;
	}

      /* Read the End of file record to find out the previous address */
      if (log_Gl.hdr.append_lsa.pageid > 0 || log_Gl.hdr.append_lsa.offset > 0)
	{
	  eof = (LOG_RECORD_HEADER *) LOG_APPEND_PTR ();
	  LOG_RESET_PREV_LSA (&eof->back_lsa);
	}

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
  log_Gl.chkpt_every_npages = prm_get_integer_value (PRM_ID_LOG_CHECKPOINT_NPAGES);

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
  log_Gl.run_nxchkpt_atpageid = (log_Gl.hdr.append_lsa.pageid + log_Gl.chkpt_every_npages);

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
      BACKGROUND_ARCHIVING_INFO *bg_arv_info;

      bg_arv_info = &log_Gl.bg_archive_info;
      bg_arv_info->start_page_id = NULL_PAGEID;
      bg_arv_info->current_page_id = NULL_PAGEID;
      bg_arv_info->last_sync_pageid = NULL_PAGEID;

      bg_arv_info->vdes =
	fileio_format (thread_p, log_Db_fullname, log_Name_bg_archive, LOG_DBLOG_BG_ARCHIVE_VOLID,
		       log_Gl.hdr.npages + 1, false, false, false, LOG_PAGESIZE, 0, false);
      if (bg_arv_info->vdes != NULL_VOLDES)
	{
	  bg_arv_info->start_page_id = log_Gl.hdr.nxarv_pageid;
	  bg_arv_info->current_page_id = log_Gl.hdr.nxarv_pageid;
	  bg_arv_info->last_sync_pageid = log_Gl.hdr.nxarv_pageid;
	}
      else
	{
	  er_log_debug (ARG_FILE_LINE, "Unable to create temporary archive log %s\n", log_Name_bg_archive);
	}

      if (bg_arv_info->vdes != NULL_VOLDES)
	{
	  (void) logpb_background_archiving (thread_p);
	}
    }

  LOG_CS_EXIT (thread_p);

  er_log_debug (ARG_FILE_LINE, "log_initialize_internal: end of log initializaton, append_lsa = (%lld|%d) \n",
		(long long int) log_Gl.hdr.append_lsa.pageid, log_Gl.hdr.append_lsa.offset);

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
log_update_compatibility_and_release (THREAD_ENTRY * thread_p, float compatibility, char release[])
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

  abort_thread_running = (int *) malloc (sizeof (int) * log_Gl.trantable.num_total_indices);
  if (abort_thread_running == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      sizeof (int) * log_Gl.trantable.num_total_indices);
      return;
    }
  memset (abort_thread_running, 0, sizeof (int) * log_Gl.trantable.num_total_indices);

  /* Abort all active transactions */
loop:
  repeat_loop = false;

  for (i = 0; i < log_Gl.trantable.num_total_indices; i++)
    {
      if (i != LOG_SYSTEM_TRAN_INDEX && (tdes = LOG_FIND_TDES (i)) != NULL && tdes->trid != NULL_TRANID)
	{
	  if (css_count_transaction_worker_threads (thread_p, i, tdes->client_id) > 0)
	    {
	      repeat_loop = true;
	    }
	  else if (LOG_ISTRAN_ACTIVE (tdes) && abort_thread_running[i] == 0)
	    {
              // *INDENT-OFF*
              cubthread::entry_callable_task::exec_func_type exec_f =
                std::bind (log_abort_task_execute, std::placeholders::_1, std::ref (*tdes));
	      css_push_external_task (css_find_conn_by_tran_index (i), new cubthread::entry_callable_task (exec_f));
              // *INDENT-ON*
	      abort_thread_running[i] = 1;
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
	  er_log_debug (ARG_FILE_LINE, "log_abort_all_active_transaction: _exit(0)\n");
	  _exit (0);
	}
      goto loop;
    }

  if (abort_thread_running != NULL)
    {
      free_and_init (abort_thread_running);
    }

#else /* SERVER_MODE */
  int save_tran_index = log_Tran_index;	/* Return to this index */

  if (log_Gl.trantable.area == NULL)
    {
      return;
    }

  /* Abort all active transactions */
  for (i = 0; i < log_Gl.trantable.num_total_indices; i++)
    {
      tdes = LOG_FIND_TDES (i);
      if (i != LOG_SYSTEM_TRAN_INDEX && tdes != NULL && tdes->trid != NULL_TRANID)
	{
	  if (LOG_ISTRAN_ACTIVE (tdes))
	    {
	      log_Tran_index = i;
	      (void) log_abort (thread_p, log_Tran_index);
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

#if defined(SERVER_MODE)
  log_daemons_destroy ();
#endif /* SERVER_MODE */
  // *INDENT-OFF*
  log_system_tdes::destroy_system_transactions ();
  // *INDENT-ON*

  LOG_CS_ENTER (thread_p);

  /* reset log_Gl.rcv_phase */
  log_Gl.rcv_phase = LOG_RECOVERY_ANALYSIS_PHASE;

  if (log_Gl.trantable.area == NULL)
    {
      LOG_CS_EXIT (thread_p);
      return;
    }

  save_tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);

  if (!logpb_is_pool_initialized ())
    {
      logtb_undefine_trantable (thread_p);
      LOG_CS_EXIT (thread_p);
      return;
    }

  if (log_Gl.append.vdes == NULL_VOLDES)
    {
      logpb_finalize_pool (thread_p);
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
      if (i != LOG_SYSTEM_TRAN_INDEX && tdes != NULL && tdes->trid != NULL_TRANID)
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
      LSA_COPY (&log_Gl.hdr.smallest_lsa_at_last_chkpt, &log_Gl.hdr.chkpt_lsa);
    }
  else
    {
      (void) logpb_checkpoint (thread_p);
    }

  logpb_flush_header (thread_p);

  /* Undefine page buffer pool and transaction table */
  logpb_finalize_pool (thread_p);

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

void
log_stop_ha_delay_registration ()
{
#if defined (SERVER_MODE)
  cubthread::get_manager ()->destroy_daemon (log_Check_ha_delay_info_daemon);
#endif // SERVER_MODE
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
log_restart_emergency (THREAD_ENTRY * thread_p, const char *db_fullname, const char *logpath,
		       const char *prefix_logname)
{
  (void) log_initialize_internal (thread_p, db_fullname, logpath, prefix_logname, false, NULL, true);
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
log_append_undoredo_data (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex, LOG_DATA_ADDR * addr, int undo_length,
			  int redo_length, const void *undo_data, const void *redo_data)
{
  LOG_CRUMB undo_crumb;
  LOG_CRUMB redo_crumb;

  /* Set undo length/data to crumb */
  assert (0 <= undo_length);
  assert (0 == undo_length || undo_data != NULL);

  undo_crumb.data = undo_data;
  undo_crumb.length = undo_length;

  /* Set redo length/data to crumb */
  assert (0 <= redo_length);
  assert (0 == redo_length || redo_data != NULL);

  redo_crumb.data = redo_data;
  redo_crumb.length = redo_length;

  log_append_undoredo_crumbs (thread_p, rcvindex, addr, 1, 1, &undo_crumb, &redo_crumb);
}

void
log_append_undoredo_data2 (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex, const VFID * vfid, PAGE_PTR pgptr,
			   PGLENGTH offset, int undo_length, int redo_length, const void *undo_data,
			   const void *redo_data)
{
  LOG_DATA_ADDR addr;
  LOG_CRUMB undo_crumb;
  LOG_CRUMB redo_crumb;

  /* Set data address */
  addr.vfid = vfid;
  addr.pgptr = pgptr;
  addr.offset = offset;

  /* Set undo length/data to crumb */
  assert (0 <= undo_length);
  assert (0 == undo_length || undo_data != NULL);

  undo_crumb.data = undo_data;
  undo_crumb.length = undo_length;

  /* Set redo length/data to crumb */
  assert (0 <= redo_length);
  assert (0 == redo_length || redo_data != NULL);

  redo_crumb.data = redo_data;
  redo_crumb.length = redo_length;

  log_append_undoredo_crumbs (thread_p, rcvindex, &addr, 1, 1, &undo_crumb, &redo_crumb);
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
log_append_undo_data (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex, LOG_DATA_ADDR * addr, int length,
		      const void *data)
{
  LOG_CRUMB undo_crumb;

  /* Set length/data to crumb */
  assert (0 <= length);
  assert (0 == length || data != NULL);

  undo_crumb.data = data;
  undo_crumb.length = length;

  log_append_undo_crumbs (thread_p, rcvindex, addr, 1, &undo_crumb);
}

void
log_append_undo_data2 (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex, const VFID * vfid, PAGE_PTR pgptr,
		       PGLENGTH offset, int length, const void *data)
{
  LOG_DATA_ADDR addr;
  LOG_CRUMB undo_crumb;

  /* Set data address */
  addr.vfid = vfid;
  addr.pgptr = pgptr;
  addr.offset = offset;

  /* Set length/data to crumb */
  assert (0 <= length);
  assert (0 == length || data != NULL);

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
log_append_redo_data (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex, LOG_DATA_ADDR * addr, int length,
		      const void *data)
{
  LOG_CRUMB redo_crumb;

  /* Set length/data to crumb */
  assert (0 <= length);
  assert (0 == length || data != NULL);

  redo_crumb.data = data;
  redo_crumb.length = length;

  log_append_redo_crumbs (thread_p, rcvindex, addr, 1, &redo_crumb);
}

void
log_append_redo_data2 (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex, const VFID * vfid, PAGE_PTR pgptr,
		       PGLENGTH offset, int length, const void *data)
{
  LOG_DATA_ADDR addr;
  LOG_CRUMB redo_crumb;

  /* Set data address */
  addr.vfid = vfid;
  addr.pgptr = pgptr;
  addr.offset = offset;

  /* Set length/data to crumb */
  assert (0 <= length);
  assert (0 == length || data != NULL);

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
log_append_undoredo_crumbs (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex, LOG_DATA_ADDR * addr, int num_undo_crumbs,
			    int num_redo_crumbs, const LOG_CRUMB * undo_crumbs, const LOG_CRUMB * redo_crumbs)
{
  LOG_TDES *tdes;		/* Transaction descriptor */
  int tran_index;
  int error_code = NO_ERROR;
  LOG_PRIOR_NODE *node;
  LOG_LSA start_lsa;
  LOG_RECTYPE rectype = LOG_IS_MVCC_OPERATION (rcvindex) ? LOG_MVCC_UNDOREDO_DATA : LOG_UNDOREDO_DATA;

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

#if !defined(SERVER_MODE)
  assert_release (!LOG_IS_MVCC_OPERATION (rcvindex));
#endif /* SERVER_MODE */

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
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
      error_code = ER_LOG_UNKNOWN_TRANINDEX;
      return;
    }

  /*
   * If we are not in a top system operation, the transaction is unactive, and
   * the transaction is not in the process of been aborted, we do nothing.
   */
  if (tdes->topops.last < 0 && !LOG_ISTRAN_ACTIVE (tdes) && !LOG_ISTRAN_ABORTED (tdes))
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
      log_append_redo_crumbs (thread_p, rcvindex, addr, num_redo_crumbs, redo_crumbs);
      return;
    }

  /*
   * Now do the UNDO & REDO portion
   */

  node = prior_lsa_alloc_and_copy_crumbs (thread_p, rectype, rcvindex, addr, num_undo_crumbs, undo_crumbs,
					  num_redo_crumbs, redo_crumbs);
  if (node == NULL)
    {
      return;
    }

  if (LOG_MAY_CONTAIN_USER_DATA (rcvindex))
    {
      if (pgbuf_get_tde_algorithm (addr->pgptr) != TDE_ALGORITHM_NONE)
	{
	  if (prior_set_tde_encrypted (node, rcvindex) != NO_ERROR)
	    {
	      assert (false);
	      return;
	    }
	}
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
  if (addr->pgptr != NULL && LOG_IS_MVCC_OPERATION (rcvindex))
    {
      pgbuf_notify_vacuum_follows (thread_p, addr->pgptr);
    }

  if (!LOG_CHECK_LOG_APPLIER (thread_p) && log_does_allow_replication () == true)
    {
      if (rcvindex == RVHF_UPDATE || rcvindex == RVOVF_CHANGE_LINK || rcvindex == RVHF_UPDATE_NOTIFY_VACUUM
	  || rcvindex == RVHF_INSERT_NEWHOME)
	{
	  LSA_COPY (&tdes->repl_update_lsa, &tdes->tail_lsa);
	  assert (tdes->is_active_worker_transaction ());
	}
      else if (rcvindex == RVHF_INSERT || rcvindex == RVHF_MVCC_INSERT)
	{
	  LSA_COPY (&tdes->repl_insert_lsa, &tdes->tail_lsa);
	  assert (tdes->is_active_worker_transaction ());
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
log_append_undo_crumbs (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex, LOG_DATA_ADDR * addr, int num_crumbs,
			const LOG_CRUMB * crumbs)
{
  LOG_TDES *tdes;		/* Transaction descriptor */
  int tran_index;
  int error_code = NO_ERROR;
  LOG_PRIOR_NODE *node;
  LOG_LSA start_lsa;
  LOG_RECTYPE rectype = LOG_IS_MVCC_OPERATION (rcvindex) ? LOG_MVCC_UNDO_DATA : LOG_UNDO_DATA;

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
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
      error_code = ER_LOG_UNKNOWN_TRANINDEX;
      return;
    }

  /*
   * If we are not in a top system operation, the transaction is unactive, and
   * the transaction is not in the process of been aborted, we do nothing.
   */
  if (tdes->topops.last < 0 && !LOG_ISTRAN_ACTIVE (tdes) && !LOG_ISTRAN_ABORTED (tdes))
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

  node = prior_lsa_alloc_and_copy_crumbs (thread_p, rectype, rcvindex, addr, num_crumbs, crumbs, 0, NULL);
  if (node == NULL)
    {
      assert (false);
      return;
    }

  /* 
   * if pgptr is NULL, the user data can be spilled as un-encrypted. 
   * Now it seems that there is no case, but can be in the future.
   */
  if (addr->pgptr != NULL && LOG_MAY_CONTAIN_USER_DATA (rcvindex))
    {
      if (pgbuf_get_tde_algorithm (addr->pgptr) != TDE_ALGORITHM_NONE)
	{
	  if (prior_set_tde_encrypted (node, rcvindex) != NO_ERROR)
	    {
	      assert (false);
	      return;
	    }
	}
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
  if (addr->pgptr != NULL && LOG_IS_MVCC_OPERATION (rcvindex))
    {
      pgbuf_notify_vacuum_follows (thread_p, addr->pgptr);
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
log_append_redo_crumbs (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex, LOG_DATA_ADDR * addr, int num_crumbs,
			const LOG_CRUMB * crumbs)
{
  LOG_TDES *tdes;		/* Transaction descriptor */
  int tran_index;
  int error_code = NO_ERROR;
  LOG_PRIOR_NODE *node;
  LOG_LSA start_lsa;
  LOG_RECTYPE rectype = LOG_IS_MVCC_OPERATION (rcvindex) ? LOG_MVCC_REDO_DATA : LOG_REDO_DATA;

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
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
      error_code = ER_LOG_UNKNOWN_TRANINDEX;
      return;
    }

  /*
   * If we are not in a top system operation, the transaction is unactive, and
   * the transaction is not in the process of been aborted, we do nothing.
   */
  if (tdes->topops.last < 0 && !LOG_ISTRAN_ACTIVE (tdes) && !LOG_ISTRAN_ABORTED (tdes))
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

  node = prior_lsa_alloc_and_copy_crumbs (thread_p, rectype, rcvindex, addr, 0, NULL, num_crumbs, crumbs);
  if (node == NULL)
    {
      return;
    }

  if (LOG_MAY_CONTAIN_USER_DATA (rcvindex))
    {
      if (pgbuf_get_tde_algorithm (addr->pgptr) != TDE_ALGORITHM_NONE)
	{
	  if (prior_set_tde_encrypted (node, rcvindex) != NO_ERROR)
	    {
	      assert (false);
	      return;
	    }
	}
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

  if (!LOG_CHECK_LOG_APPLIER (thread_p) && log_does_allow_replication () == true)
    {
      if (rcvindex == RVHF_UPDATE || rcvindex == RVOVF_CHANGE_LINK || rcvindex == RVHF_UPDATE_NOTIFY_VACUUM)
	{
	  LSA_COPY (&tdes->repl_update_lsa, &tdes->tail_lsa);
	  assert (tdes->is_active_worker_transaction ());
	}
      else if (rcvindex == RVHF_INSERT || rcvindex == RVHF_MVCC_INSERT)
	{
	  LSA_COPY (&tdes->repl_insert_lsa, &tdes->tail_lsa);
	  assert (tdes->is_active_worker_transaction ());
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
log_append_undoredo_recdes (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex, LOG_DATA_ADDR * addr,
			    const RECDES * undo_recdes, const RECDES * redo_recdes)
{
  log_append_undoredo_recdes2 (thread_p, rcvindex, addr->vfid, addr->pgptr, addr->offset, undo_recdes, redo_recdes);
}

void
log_append_undoredo_recdes2 (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex, const VFID * vfid, PAGE_PTR pgptr,
			     PGLENGTH offset, const RECDES * undo_recdes, const RECDES * redo_recdes)
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

  log_append_undoredo_crumbs (thread_p, rcvindex, &addr, num_undo_crumbs, num_redo_crumbs, undo_crumbs, redo_crumbs);
}

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
log_append_undo_recdes (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex, LOG_DATA_ADDR * addr, const RECDES * recdes)
{
  log_append_undo_recdes2 (thread_p, rcvindex, addr->vfid, addr->pgptr, addr->offset, recdes);
}

void
log_append_undo_recdes2 (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex, const VFID * vfid, PAGE_PTR pgptr,
			 PGLENGTH offset, const RECDES * recdes)
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
log_append_redo_recdes (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex, LOG_DATA_ADDR * addr, const RECDES * recdes)
{
  log_append_redo_recdes2 (thread_p, rcvindex, addr->vfid, addr->pgptr, addr->offset, recdes);
}

void
log_append_redo_recdes2 (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex, const VFID * vfid, PAGE_PTR pgptr,
			 PGLENGTH offset, const RECDES * recdes)
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
log_append_dboutside_redo (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex, int length, const void *data)
{
  LOG_TDES *tdes;		/* Transaction descriptor */
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
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
      error_code = ER_LOG_UNKNOWN_TRANINDEX;
      return;
    }

  /*
   * If we are not in a top system operation, the transaction is unactive, and
   * the transaction is not in the process of been aborted, we do nothing.
   */
  if (tdes->topops.last < 0 && !LOG_ISTRAN_ACTIVE (tdes) && !LOG_ISTRAN_ABORTED (tdes))
    {
      /*
       * We do not log anything when the transaction is unactive and it is not
       * in the process of aborting.
       */
      return;
    }

  node =
    prior_lsa_alloc_and_copy_data (thread_p, LOG_DBEXTERN_REDO_DATA, rcvindex, NULL, 0, NULL, length, (char *) data);
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
log_append_postpone (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex, LOG_DATA_ADDR * addr, int length, const void *data)
{
  LOG_TDES *tdes;		/* Transaction descriptor */
  LOG_RCV rcv;			/* Recovery structure for execution */
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

      assert (RV_fun[rcvindex].redofun != NULL);
      (void) (*RV_fun[rcvindex].redofun) (thread_p, &rcv);

      LOG_FLUSH_LOGGING_HAS_BEEN_SKIPPED (thread_p);
      log_skip_logging (thread_p, addr);
      return;
    }

  /* Find transaction descriptor for current logging transaction */
  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
      error_code = ER_LOG_UNKNOWN_TRANINDEX;
      return;
    }

  skipredo = log_can_skip_redo_logging (rcvindex, tdes, addr);
  if (skipredo == true || (tdes->topops.last < 0 && !LOG_ISTRAN_ACTIVE (tdes) && !LOG_ISTRAN_ABORTED (tdes)))
    {
      /*
       * Warning postpone logging is ignored during REDO recovery, normal
       * rollbacks, and for temporary data pages
       */
      rcv.length = length;
      rcv.offset = addr->offset;
      rcv.pgptr = addr->pgptr;
      rcv.data = (char *) data;

      assert (RV_fun[rcvindex].redofun != NULL);
      (void) (*RV_fun[rcvindex].redofun) (thread_p, &rcv);
      if (skipredo == false)
	{
	  log_append_redo_data (thread_p, rcvindex, addr, length, data);
	}

      return;
    }

  /*
   * If the transaction has not logged any record, add a dummy record to
   * start the postpone purposes during the commit.
   */

  if (LSA_ISNULL (&tdes->tail_lsa)
      || (log_is_in_crash_recovery ()
	  && (crash_lsa = log_get_crash_point_lsa ()) != NULL && LSA_LE (&tdes->tail_lsa, crash_lsa)))
    {
      log_append_empty_record (thread_p, LOG_DUMMY_HEAD_POSTPONE, addr);
    }

  node = prior_lsa_alloc_and_copy_data (thread_p, LOG_POSTPONE, rcvindex, addr, 0, NULL, length, (char *) data);
  if (node == NULL)
    {
      return;
    }

  if (LOG_MAY_CONTAIN_USER_DATA (rcvindex))
    {
      if (pgbuf_get_tde_algorithm (addr->pgptr) != TDE_ALGORITHM_NONE)
	{
	  if (prior_set_tde_encrypted (node, rcvindex) != NO_ERROR)
	    {
	      assert (false);
	      return;
	    }
	}
    }

  // redo data must be saved before calling prior_lsa_next_record, which may free this prior node
  tdes->m_log_postpone_cache.add_redo_data (*node);

  // an entry for this postpone log record was already created and we also need to save its LSA
  LOG_LSA start_lsa = prior_lsa_next_record (thread_p, node, tdes);
  tdes->m_log_postpone_cache.add_lsa (start_lsa);

  /* Set address early in case there is a crash, because of skip_head */
  if (tdes->topops.last >= 0)
    {
      if (LSA_ISNULL (&tdes->topops.stack[tdes->topops.last].posp_lsa))
	{
	  LSA_COPY (&tdes->topops.stack[tdes->topops.last].posp_lsa, &tdes->tail_lsa);
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
log_append_run_postpone (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex, LOG_DATA_ADDR * addr, const VPID * rcv_vpid,
			 int length, const void *data, const LOG_LSA * ref_lsa)
{
  LOG_REC_RUN_POSTPONE *run_posp;	/* A run postpone record */
  LOG_TDES *tdes;		/* Transaction descriptor */
  int tran_index;
  int error_code = NO_ERROR;
  LOG_PRIOR_NODE *node;
  LOG_LSA start_lsa;

  /* Find transaction descriptor for current logging transaction */
  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
      error_code = ER_LOG_UNKNOWN_TRANINDEX;
      return;
    }

  if (tdes->state != TRAN_UNACTIVE_WILL_COMMIT && tdes->state != TRAN_UNACTIVE_COMMITTED_WITH_POSTPONE
      && tdes->state != TRAN_UNACTIVE_TOPOPE_COMMITTED_WITH_POSTPONE)
    {
      /* Warning run postpone is ignored when transaction is not committed */
#if defined(CUBRID_DEBUG)
      er_log_debug (ARG_FILE_LINE,
		    "log_run_postpone: Warning run postpone logging is ignored when transaction is not committed\n");
#endif /* CUBRID_DEBUG */
      assert (false);
    }
  else
    {
      node = prior_lsa_alloc_and_copy_data (thread_p, LOG_RUN_POSTPONE, RV_NOT_DEFINED, NULL, length, (char *) data,
					    0, NULL);
      if (node == NULL)
	{
	  return;
	}

      /* 
       * By the comment above for this function, all the potpone log is page-oriented, 
       * and have to contain page address. However, code below check if addr->pgptr is NULL.
       * So, we also check it just in case.
       */
      if (addr->pgptr != NULL && LOG_MAY_CONTAIN_USER_DATA (rcvindex))
	{
	  if (pgbuf_get_tde_algorithm (addr->pgptr) != TDE_ALGORITHM_NONE)
	    {
	      if (prior_set_tde_encrypted (node, rcvindex) != NO_ERROR)
		{
		  assert (false);
		  return;
		}
	    }
	}

      run_posp = (LOG_REC_RUN_POSTPONE *) node->data_header;
      run_posp->data.rcvindex = rcvindex;
      run_posp->data.pageid = rcv_vpid->pageid;
      run_posp->data.volid = rcv_vpid->volid;
      run_posp->data.offset = addr->offset;
      LSA_COPY (&run_posp->ref_lsa, ref_lsa);
      run_posp->length = length;

      start_lsa = prior_lsa_next_record (thread_p, node, tdes);

      /*
       * Set the LSA on the data page of the corresponding log record for page operation logging.
       * Make sure that I should log. Page operational logging is not done for temporary data of temporary files/volumes
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
log_append_compensate (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex, const VPID * vpid, PGLENGTH offset,
		       PAGE_PTR pgptr, int length, const void *data, LOG_TDES * tdes)
{
  log_append_compensate_internal (thread_p, rcvindex, vpid, offset, pgptr, length, data, tdes, NULL);
}

/*
 * log_append_compensate_with_undo_nxlsa () - Append compensate log record
 *					      and overwrite its undo_nxlsa.
 *
 * return	   : Void.
 * thread_p (in)   : Thread entry.
 * rcvindex (in)   : Index to recovery function.
 * vpid (in)	   : Volume-page address of compensate data
 * offset(in)	   : Offset of compensate data
 * pgptr(in)	   : Page pointer where compensating data resides. It may be
 *		     NULL when the page is not available during recovery.
 * length (in)	   : Length of compensating data (kind of redo(after) data)
 * data (in)	   : Compensating data (kind of redo(after) data)
 * tdes (in/out)   : State structure of transaction of the log record
 * undo_nxlsa (in) : Use a different undo_nxlsa than tdes->undo_nxlsa.
 *		     Necessary for cases when log records may be added before
 *		     compensation (one example being index merge/split before
 *		     undoing b-tree operation).
 */
void
log_append_compensate_with_undo_nxlsa (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex, const VPID * vpid,
				       PGLENGTH offset, PAGE_PTR pgptr, int length, const void *data, LOG_TDES * tdes,
				       const LOG_LSA * undo_nxlsa)
{
  assert (undo_nxlsa != NULL);

  log_append_compensate_internal (thread_p, rcvindex, vpid, offset, pgptr, length, data, tdes, undo_nxlsa);
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
 *   undo_nxlsa(in): Use a different undo_nxlsa than tdes->undo_nxlsa.
 *		     Necessary for cases when log records may be added before
 *		     compensation (one example being index merge/split before
 *		     undoing b-tree operation).
 *
 * NOTE: Log a compensating log record. An undo performed during a
 *              rollback or recovery is logged using what is called a
 *              compensation log record. A compensation log record undoes the
 *              redo of an aborted transaction during the redo phase of the
 *              recovery process. Compensating log records are quite useful to
 *              make system and media crash recovery faster. Compensating log
 *              records are redo log records and thus, they are never undone.
 */
static void
log_append_compensate_internal (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex, const VPID * vpid, PGLENGTH offset,
				PAGE_PTR pgptr, int length, const void *data, LOG_TDES * tdes,
				const LOG_LSA * undo_nxlsa)
{
  LOG_REC_COMPENSATE *compensate;	/* Compensate log record */
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
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_COMPENSATE_INTERFACE, 0);
      error_code = ER_LOG_COMPENSATE_INTERFACE;
      return;
    }
#endif /* CUBRID_DEBUG */

  node =
    prior_lsa_alloc_and_copy_data (thread_p, LOG_COMPENSATE, rcvindex, NULL, length, (char *) data, 0, (char *) NULL);
  if (node == NULL)
    {
      return;
    }

  LSA_COPY (&prev_lsa, &tdes->undo_nxlsa);

  compensate = (LOG_REC_COMPENSATE *) node->data_header;

  compensate->data.rcvindex = rcvindex;
  compensate->data.pageid = vpid->pageid;
  compensate->data.offset = offset;
  compensate->data.volid = vpid->volid;
  if (undo_nxlsa != NULL)
    {
      LSA_COPY (&compensate->undo_nxlsa, undo_nxlsa);
    }
  else
    {
      LSA_COPY (&compensate->undo_nxlsa, &prev_lsa);
    }
  compensate->length = length;

  /*
   * Although compensation log is page-oriented, pgptr can be NULL 
   * when fails to fix the page because of an error.
   * In this case, we don't encrypt the log and it can contain user data un-encrypted.
   * After all, it is very rare and exceptional case.
   */
  if (pgptr != NULL && LOG_MAY_CONTAIN_USER_DATA (rcvindex))
    {
      if (pgbuf_get_tde_algorithm (pgptr) != TDE_ALGORITHM_NONE)
	{
	  if (prior_set_tde_encrypted (node, rcvindex) != NO_ERROR)
	    {
	      assert (false);
	      return;
	    }
	}
    }

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
 * log_append_empty_record -
 *
 * return: nothing
 */
void
log_append_empty_record (THREAD_ENTRY * thread_p, LOG_RECTYPE logrec_type, LOG_DATA_ADDR * addr)
{
  int tran_index;
  bool skip = false;
  LOG_TDES *tdes;
  LOG_PRIOR_NODE *node;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL)
    {
      assert (false);
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

  node = prior_lsa_alloc_and_copy_data (thread_p, logrec_type, RV_NOT_DEFINED, NULL, 0, NULL, 0, NULL);
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
  LOG_REC_HA_SERVER_STATE *ha_server_state;
  LOG_PRIOR_NODE *node;
  LOG_LSA start_lsa;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL)
    {
      return;
    }
  assert (tdes->is_active_worker_transaction () || tdes->is_system_main_transaction ());

  node = prior_lsa_alloc_and_copy_data (thread_p, LOG_DUMMY_HA_SERVER_STATE, RV_NOT_DEFINED, NULL, 0, NULL, 0, NULL);
  if (node == NULL)
    {
      return;
    }

  ha_server_state = (LOG_REC_HA_SERVER_STATE *) node->data_header;
  memset (ha_server_state, 0, sizeof (LOG_REC_HA_SERVER_STATE));

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

  log_Gl.prior_info.prior_lsa_mutex.lock ();

  (void) pgbuf_set_lsa (thread_p, addr->pgptr, &log_Gl.prior_info.prior_lsa);

  log_Gl.prior_info.prior_lsa_mutex.unlock ();

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
		    "log_skip_logging: A data page pointer must be given as part of the address... ignored\n");
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
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
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
		  (void) pgbuf_set_lsa (thread_p, addr->pgptr, &log_Gl.rcv_phase_lsa);
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
  LOG_REC_SAVEPT *savept;	/* A savept log record */
  LOG_TDES *tdes;		/* Transaction descriptor */
  int length;			/* Length of the name of the save point */
  int tran_index;
  int error_code;
  LOG_PRIOR_NODE *node;

  /* Find transaction descriptor for current logging transaction */

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
      error_code = ER_LOG_UNKNOWN_TRANINDEX;
      return NULL;
    }
  assert (tdes->is_active_worker_transaction ());

  if (!LOG_ISTRAN_ACTIVE (tdes))
    {
      /*
       * Error, a user savepoint cannot be added when the transaction is not
       * active
       */
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_CANNOT_ADD_SAVEPOINT, 0);
      error_code = ER_LOG_CANNOT_ADD_SAVEPOINT;
      return NULL;
    }

  if (savept_name == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_NONAME_SAVEPOINT, 0);
      error_code = ER_LOG_NONAME_SAVEPOINT;
      return NULL;
    }

  length = (int) strlen (savept_name) + 1;

  node =
    prior_lsa_alloc_and_copy_data (thread_p, LOG_SAVEPOINT, RV_NOT_DEFINED, NULL, length, (char *) savept_name, 0,
				   (char *) NULL);
  if (node == NULL)
    {
      return NULL;
    }

  savept = (LOG_REC_SAVEPT *) node->data_header;
  savept->length = length;
  LSA_COPY (&savept->prv_savept, &tdes->savept_lsa);

  (void) prior_lsa_next_record (thread_p, node, tdes);

  LSA_COPY (&tdes->savept_lsa, &tdes->tail_lsa);

  perfmon_inc_stat (thread_p, PSTAT_TRAN_NUM_SAVEPOINTS);

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
log_get_savepoint_lsa (THREAD_ENTRY * thread_p, const char *savept_name, LOG_TDES * tdes, LOG_LSA * savept_lsa)
{
  char *ptr;			/* Pointer to savepoint name */
  char log_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT], *aligned_log_pgbuf;
  LOG_PAGE *log_pgptr = NULL;	/* Log page pointer where a savepoint log record is located */
  LOG_RECORD_HEADER *log_rec;	/* Pointer to log record */
  LOG_REC_SAVEPT *savept;	/* A savepoint log record */
  LOG_LSA prev_lsa;		/* Previous savepoint */
  LOG_LSA log_lsa;
  int length;			/* Length of savepoint name */
  bool found = false;

  aligned_log_pgbuf = PTR_ALIGN (log_pgbuf, MAX_ALIGNMENT);

  /* Find the savepoint LSA, for the given savepoint name */
  LSA_COPY (&prev_lsa, &tdes->savept_lsa);

  log_pgptr = (LOG_PAGE *) aligned_log_pgbuf;

  while (!LSA_ISNULL (&prev_lsa) && found == false)
    {
      if (logpb_fetch_page (thread_p, &prev_lsa, LOG_CS_FORCE_USE, log_pgptr) != NO_ERROR)
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
	      er_log_debug (ARG_FILE_LINE, "log_find_savept_lsa: Corrupted log rec");
	      LSA_SET_NULL (&prev_lsa);
	      break;
	    }

	  /* Advance the pointer to read the savepoint log record */

	  LOG_READ_ADD_ALIGN (thread_p, sizeof (*log_rec), &log_lsa, log_pgptr);
	  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*savept), &log_lsa, log_pgptr);

	  savept = (LOG_REC_SAVEPT *) ((char *) log_pgptr->area + log_lsa.offset);
	  LSA_COPY (&prev_lsa, &savept->prv_savept);
	  length = savept->length;

	  LOG_READ_ADD_ALIGN (thread_p, sizeof (*savept), &log_lsa, log_pgptr);
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
	      int area_offset;	/* The area offset */
	      int remains_length;	/* Length of data that remains to be copied */
	      unsigned int copy_length;	/* Length to copy into area */

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
		  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, 0, &log_lsa, log_pgptr);
		  if (log_lsa.offset + remains_length < (int) LOGAREA_SIZE)
		    {
		      copy_length = remains_length;
		    }
		  else
		    {
		      copy_length = LOGAREA_SIZE - (int) (log_lsa.offset);
		    }

		  memcpy (ptr + area_offset, (char *) log_pgptr->area + log_lsa.offset, copy_length);
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
 * log_sysop_end_type_string () - string for log sys op end type
 *
 * return        : string for log sys op end type
 * end_type (in) : log sys op end type
 */
const char *
log_sysop_end_type_string (LOG_SYSOP_END_TYPE end_type)
{
  switch (end_type)
    {
    case LOG_SYSOP_END_COMMIT:
      return "LOG_SYSOP_END_COMMIT";
    case LOG_SYSOP_END_ABORT:
      return "LOG_SYSOP_END_ABORT";
    case LOG_SYSOP_END_LOGICAL_UNDO:
      return "LOG_SYSOP_END_LOGICAL_UNDO";
    case LOG_SYSOP_END_LOGICAL_MVCC_UNDO:
      return "LOG_SYSOP_END_LOGICAL_MVCC_UNDO";
    case LOG_SYSOP_END_LOGICAL_COMPENSATE:
      return "LOG_SYSOP_END_LOGICAL_COMPENSATE";
    case LOG_SYSOP_END_LOGICAL_RUN_POSTPONE:
      return "LOG_SYSOP_END_LOGICAL_RUN_POSTPONE";
    default:
      assert (false);
      return "UNKNOWN LOG_SYSOP_END_TYPE";
    }
}

/*
 * log_sysop_start () - Start a new system operation. This can also be nested in another system operation.
 *
 * return	 : Error code.
 * thread_p (in) : Thread entry.
 */
void
log_sysop_start (THREAD_ENTRY * thread_p)
{
  LOG_TDES *tdes = NULL;
  int tran_index;

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
      return;
    }

  assert (tdes->is_allowed_sysop ());

  tdes->lock_topop ();

  /* Can current tdes.topops stack handle another system operation? */
  if (tdes->topops.max == 0 || (tdes->topops.last + 1) >= tdes->topops.max)
    {
      if (logtb_realloc_topops_stack (tdes, 1) == NULL)
	{
	  /* Out of memory */
	  assert (false);
	  tdes->unlock_topop ();
	  return;
	}
    }

  if (VACUUM_IS_THREAD_VACUUM (thread_p))
    {
      /* should not be in process log */
      assert (vacuum_worker_state_is_execute (thread_p));

      vacuum_er_log (VACUUM_ER_LOG_TOPOPS | VACUUM_ER_LOG_WORKER,
		     "Start system operation. Current worker tdes: tdes->trid=%d, tdes->topops.last=%d, "
		     "tdes->tail_lsa=(%lld, %d). Worker state=%d.", tdes->trid, tdes->topops.last,
		     (long long int) tdes->tail_lsa.pageid, (int) tdes->tail_lsa.offset,
		     vacuum_get_worker_state (thread_p));
    }

  tdes->on_sysop_start ();

  /* NOTE if tdes->topops.last >= 0, there is an already defined top system operation. */
  tdes->topops.last++;
  LSA_COPY (&tdes->topops.stack[tdes->topops.last].lastparent_lsa, &tdes->tail_lsa);
  LSA_COPY (&tdes->topop_lsa, &tdes->tail_lsa);

  LSA_SET_NULL (&tdes->topops.stack[tdes->topops.last].posp_lsa);

  perfmon_inc_stat (thread_p, PSTAT_TRAN_NUM_START_TOPOPS);
}

/*
 * log_sysop_start_atomic () - start a system operation that required to be atomic. it is aborted during recovery before
 *                             all postpones are finished.
 *
 * return        : void
 * thread_p (in) : thread entry
 */
void
log_sysop_start_atomic (THREAD_ENTRY * thread_p)
{
  LOG_TDES *tdes = NULL;
  int tran_index;

  log_sysop_start (thread_p);
  log_sysop_get_tran_index_and_tdes (thread_p, &tran_index, &tdes);
  if (tdes == NULL || tdes->topops.last < 0)
    {
      /* not a good context. must be in a system operation */
      assert_release (false);
      return;
    }
  if (LSA_ISNULL (&tdes->rcv.atomic_sysop_start_lsa))
    {
      LOG_PRIOR_NODE *node =
	prior_lsa_alloc_and_copy_data (thread_p, LOG_SYSOP_ATOMIC_START, RV_NOT_DEFINED, NULL, 0, NULL, 0, NULL);
      if (node == NULL)
	{
	  return;
	}

      (void) prior_lsa_next_record (thread_p, node, tdes);
    }
  else
    {
      /* this must be a nested atomic system operation. If parent is atomic, we'll be atomic too. */
      assert (tdes->topops.last > 0);

      /* oh, and please tell me this is not a nested system operation during postpone of system operation nested to
       * another atomic system operation... */
      assert (LSA_ISNULL (&tdes->rcv.sysop_start_postpone_lsa));
    }
}

/*
 * log_sysop_end_random_exit () - Random exit from system operation end functions. Used to simulate crashes.
 *
 * return	 : Void.
 * thread_p (in) : Thread entry.
 */
STATIC_INLINE void
log_sysop_end_random_exit (THREAD_ENTRY * thread_p)
{
  int mod_factor = 5000;	/* 0.02% */

  FI_TEST_ARG (thread_p, FI_TEST_LOG_MANAGER_RANDOM_EXIT_AT_END_SYSTEMOP, &mod_factor, 0);
}

/*
 * log_sysop_end_begin () - Used at the beginning of system operation end functions. Verifies valid context and outputs
 *			    transaction index and descriptor.
 *
 * return		: Void.
 * thread_p (in)	: Thread entry.
 * tran_index_out (out) : Transaction index.
 * tdes_out (out)       : Transaction descriptor.
 */
STATIC_INLINE void
log_sysop_end_begin (THREAD_ENTRY * thread_p, int *tran_index_out, LOG_TDES ** tdes_out)
{
  log_sysop_end_random_exit (thread_p);

  log_sysop_get_tran_index_and_tdes (thread_p, tran_index_out, tdes_out);
  if ((*tdes_out) != NULL && (*tdes_out)->topops.last < 0)
    {
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_NOTACTIVE_TOPOPS, 0);
      *tdes_out = NULL;
      return;
    }
}

/*
 * log_sysop_end_unstack () - Used for ending system operations, removes last sysop from transaction descriptor's stack.
 *
 * return	 : Void.
 * thread_p (in) : Thread entry.
 * tdes (in/out) : Transaction descriptor.
 */
STATIC_INLINE void
log_sysop_end_unstack (THREAD_ENTRY * thread_p, LOG_TDES * tdes)
{
  tdes->topops.last--;
  if (tdes->topops.last >= 0)
    {
      LSA_COPY (&tdes->topop_lsa, &LOG_TDES_LAST_SYSOP (tdes)->lastparent_lsa);
    }
  else
    {
      LSA_SET_NULL (&tdes->topop_lsa);
    }
}

/*
 * log_sysop_end_final () - Used to complete a system operation at the end of system operation end functions.
 *
 * return	 : Void.
 * thread_p (in) : Thread entry.
 * tdes (in/out) : Transaction descriptor.
 */
STATIC_INLINE void
log_sysop_end_final (THREAD_ENTRY * thread_p, LOG_TDES * tdes)
{
  int r = NO_ERROR;

  log_sysop_end_unstack (thread_p, tdes);

  tdes->unlock_topop ();

  perfmon_inc_stat (thread_p, PSTAT_TRAN_NUM_END_TOPOPS);

  if (VACUUM_IS_THREAD_VACUUM (thread_p) && tdes->topops.last < 0)
    {
      assert (vacuum_worker_state_is_execute (thread_p));
      vacuum_er_log (VACUUM_ER_LOG_TOPOPS,
		     "Ended all top operations. Tdes: tdes->trid=%d tdes->head_lsa=(%lld, %d), "
		     "tdes->tail_lsa=(%lld, %d), tdes->undo_nxlsa=(%lld, %d), "
		     "tdes->tail_topresult_lsa=(%lld, %d). Worker state=%d.", tdes->trid,
		     (long long int) tdes->head_lsa.pageid, (int) tdes->head_lsa.offset,
		     (long long int) tdes->tail_lsa.pageid, (int) tdes->tail_lsa.offset,
		     (long long int) tdes->undo_nxlsa.pageid, (int) tdes->undo_nxlsa.offset,
		     (long long int) tdes->tail_topresult_lsa.pageid, (int) tdes->tail_topresult_lsa.offset,
		     vacuum_get_worker_state (thread_p));
    }
  tdes->on_sysop_end ();

  log_sysop_end_random_exit (thread_p);

  if (LOG_ISCHECKPOINT_TIME ())
    {
#if defined(SERVER_MODE)
      log_wakeup_checkpoint_daemon ();
#else /* SERVER_MODE */
      if (!tdes->is_under_sysop ())
	{
	  (void) logpb_checkpoint (thread_p);
	}
      else
	{
	  // not safe to do a checkpoint in the middle of a system operations; for instance, tdes is cleared after
	  // checkpoint
	}
#endif /* SERVER_MODE */
    }
}

/*
 * log_sysop_commit_internal () - Commit system operation. This can be used just to guarantee atomicity or permanence of
 *				  all changes in system operation. Or it can be extended to also act as an undo,
 *				  compensate or run postpone log record. The type is decided using log_record argument.
 *
 * return	              : Void.
 * thread_p (in)              : Thread entry.
 * log_record (in)            : All information that are required to build the log record for commit system operation.
 * data_size (in)             : recovery data size
 * data (in)                  : recovery data
 * is_rv_finish_postpone (in) : true if this is called during recovery to finish a system op postpone
 */
void
log_sysop_commit_internal (THREAD_ENTRY * thread_p, LOG_REC_SYSOP_END * log_record, int data_size, const char *data,
			   bool is_rv_finish_postpone)
{
  int tran_index;
  LOG_TDES *tdes = NULL;

  assert (log_record != NULL);
  assert (log_record->type != LOG_SYSOP_END_ABORT);

  log_sysop_end_begin (thread_p, &tran_index, &tdes);
  if (tdes == NULL)
    {
      assert_release (false);
      return;
    }

  if ((LSA_ISNULL (&tdes->tail_lsa) || LSA_LE (&tdes->tail_lsa, LOG_TDES_LAST_SYSOP_PARENT_LSA (tdes)))
      && (log_record->type == LOG_SYSOP_END_COMMIT || log_No_logging))
    {
      /* No change. */
      assert (LSA_ISNULL (&LOG_TDES_LAST_SYSOP (tdes)->posp_lsa));
    }
  else
    {
      /* we are here because either system operation is not empty, or this is the end of a logical system operation.
       * we don't actually allow empty logical system operation because it might hide a logic flaw. however, there are
       * unusual cases when a logical operation does not really require logging (see RVPGBUF_FLUSH_PAGE). if you create
       * such a case, you should add a dummy log record to trick this assert. */
      assert (!LSA_ISNULL (&tdes->tail_lsa) && LSA_GT (&tdes->tail_lsa, LOG_TDES_LAST_SYSOP_PARENT_LSA (tdes)));

      /* now that we have access to tdes, we can do some updates on log record and sanity checks */
      if (log_record->type == LOG_SYSOP_END_LOGICAL_RUN_POSTPONE)
	{
	  /* only allowed for postpones */
	  assert (tdes->state == TRAN_UNACTIVE_COMMITTED_WITH_POSTPONE
		  || tdes->state == TRAN_UNACTIVE_TOPOPE_COMMITTED_WITH_POSTPONE);

	  /* this is relevant for proper recovery */
	  log_record->run_postpone.is_sysop_postpone =
	    (tdes->state == TRAN_UNACTIVE_TOPOPE_COMMITTED_WITH_POSTPONE && !is_rv_finish_postpone);
	}
      else if (log_record->type == LOG_SYSOP_END_LOGICAL_COMPENSATE)
	{
	  /* we should be doing rollback or undo recovery */
	  assert (tdes->state == TRAN_UNACTIVE_ABORTED || tdes->state == TRAN_UNACTIVE_UNILATERALLY_ABORTED
		  || (is_rv_finish_postpone && (tdes->state == TRAN_UNACTIVE_TOPOPE_COMMITTED_WITH_POSTPONE
						|| tdes->state == TRAN_UNACTIVE_COMMITTED_WITH_POSTPONE)));
	}
      else if (log_record->type == LOG_SYSOP_END_LOGICAL_UNDO || log_record->type == LOG_SYSOP_END_LOGICAL_MVCC_UNDO)
	{
	  /* ... no restrictions I can think of */
	}
      else
	{
	  assert (log_record->type == LOG_SYSOP_END_COMMIT);
	  assert (tdes->state != TRAN_UNACTIVE_COMMITTED_WITH_POSTPONE
		  && (tdes->state != TRAN_UNACTIVE_TOPOPE_COMMITTED_WITH_POSTPONE || is_rv_finish_postpone));
	}

      if (!LOG_CHECK_LOG_APPLIER (thread_p) && tdes->is_active_worker_transaction ()
	  && log_does_allow_replication () == true)
	{
	  /* for the replication agent guarantee the order of transaction */
	  /* for CC(Click Counter) : at here */
	  log_append_repl_info (thread_p, tdes, false);
	}

      log_record->lastparent_lsa = *LOG_TDES_LAST_SYSOP_PARENT_LSA (tdes);
      log_record->prv_topresult_lsa = tdes->tail_topresult_lsa;

      /* do postpone */
      log_sysop_do_postpone (thread_p, tdes, log_record, data_size, data);

      /* log system operation end */
      log_append_sysop_end (thread_p, tdes, log_record, data_size, data);

      /* Remember last partial result */
      LSA_COPY (&tdes->tail_topresult_lsa, &tdes->tail_lsa);
    }

  log_sysop_end_final (thread_p, tdes);
}

/*
 * log_sysop_commit () - Commit system operation. This is the default type to end a system operation successfully and
 *			 to guarantee atomicity/permanency of all its operations.
 *
 * return	 : Void.
 * thread_p (in) : Thread entry.
 */
void
log_sysop_commit (THREAD_ENTRY * thread_p)
{
  LOG_REC_SYSOP_END log_record;

  log_record.type = LOG_SYSOP_END_COMMIT;

  log_sysop_commit_internal (thread_p, &log_record, 0, NULL, false);
}

/*
 * log_sysop_end_logical_undo () - Commit system operation and add an undo log record. This is a logical undo for complex
 *				  operations that cannot be easily located when rollback or recovery undo is executed.
 *
 * return	  : Void.
 * thread_p (in)  : Thread entry.
 * rcvindex (in)  : Recovery index for undo operation.
 * vfid (in)      : NULL or file identifier. Must be not NULL for mvcc operations.
 * undo_size (in) : Undo data size.
 * undo_data (in) : Undo data.
 *
 * note: sys ops used for logical undo have a limitation: they cannot use postpone log records. this limitation can
 *       be changed if needed by extending sys op start postpone log record to support undo data. so far, the extension
 *       was not necessary.
 */
void
log_sysop_end_logical_undo (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex, const VFID * vfid, int undo_size,
			    const char *undo_data)
{
  LOG_REC_SYSOP_END log_record;

  assert (rcvindex != RV_NOT_DEFINED);

  if (LOG_IS_MVCC_OPERATION (rcvindex))
    {
      log_record.type = LOG_SYSOP_END_LOGICAL_MVCC_UNDO;
      log_record.mvcc_undo.undo.data.offset = NULL_OFFSET;
      log_record.mvcc_undo.undo.data.volid = NULL_VOLID;
      log_record.mvcc_undo.undo.data.pageid = NULL_PAGEID;
      log_record.mvcc_undo.undo.data.rcvindex = rcvindex;
      log_record.mvcc_undo.undo.length = undo_size;
      log_record.mvcc_undo.mvccid = logtb_get_current_mvccid (thread_p);
      log_record.mvcc_undo.vacuum_info.vfid = *vfid;
      LSA_SET_NULL (&log_record.mvcc_undo.vacuum_info.prev_mvcc_op_log_lsa);
    }
  else
    {
      log_record.type = LOG_SYSOP_END_LOGICAL_UNDO;
      log_record.undo.data.offset = NULL_OFFSET;
      log_record.undo.data.volid = NULL_VOLID;
      log_record.undo.data.pageid = NULL_PAGEID;
      log_record.undo.data.rcvindex = rcvindex;
      log_record.undo.length = undo_size;
    }
  assert (LOG_MAY_CONTAIN_USER_DATA (rcvindex) ? vfid != NULL : true);
  log_record.vfid = vfid;

  log_sysop_commit_internal (thread_p, &log_record, undo_size, undo_data, false);
}

/*
 * log_sysop_commit_and_compensate () - Commit system operation and add a compensate log record. This is a logical
 *					compensation that is too complex to be included in a single log record.
 *
 * return	   : Void.
 * thread_p (in)   : Thread entry.
 * undo_nxlsa (in) : LSA of next undo LSA (equivalent to compensated undo record previous LSA).
 */
void
log_sysop_end_logical_compensate (THREAD_ENTRY * thread_p, LOG_LSA * undo_nxlsa)
{
  LOG_REC_SYSOP_END log_record;

  log_record.type = LOG_SYSOP_END_LOGICAL_COMPENSATE;
  log_record.compensate_lsa = *undo_nxlsa;

  log_sysop_commit_internal (thread_p, &log_record, 0, NULL, false);
}

/*
 * log_sysop_end_logical_run_postpone () - Commit system operation and add a run postpone log record. This is a logical
 *					   run postpone that is too complex to be included in a single log record.
 *
 * return	 : Void.
 * thread_p (in) : Thread entry.
 * posp_lsa (in) : The LSA of postpone record which was executed by this run postpone.
 */
void
log_sysop_end_logical_run_postpone (THREAD_ENTRY * thread_p, LOG_LSA * posp_lsa)
{
  LOG_REC_SYSOP_END log_record;

  log_record.type = LOG_SYSOP_END_LOGICAL_RUN_POSTPONE;
  log_record.run_postpone.postpone_lsa = *posp_lsa;
  /* is_sysop_postpone will be set in log_sysop_commit_internal */

  log_sysop_commit_internal (thread_p, &log_record, 0, NULL, false);
}

/*
 * log_sysop_end_recovery_postpone () - called during recovery to finish the postpone phase of system op
 *
 * return          : void
 * thread_p (in)   : thread entry
 * log_record (in) : end system op log record as it was read from start postpone log record
 * data_size (in)  : undo data size
 * data (in)       : undo data
 */
void
log_sysop_end_recovery_postpone (THREAD_ENTRY * thread_p, LOG_REC_SYSOP_END * log_record, int data_size,
				 const char *data)
{
  log_sysop_commit_internal (thread_p, log_record, data_size, data, true);
}

/*
 * log_sysop_abort () - Abort sytem operations (usually due to errors). All changes in this system operation are
 *			rollbacked.
 *
 * return	 : Void.
 * thread_p (in) : Thread entry.
 */
void
log_sysop_abort (THREAD_ENTRY * thread_p)
{
  int tran_index;
  LOG_TDES *tdes = NULL;
  LOG_REC_SYSOP_END sysop_end;

  log_sysop_end_begin (thread_p, &tran_index, &tdes);
  if (tdes == NULL)
    {
      assert_release (false);
      return;
    }

  if (LSA_ISNULL (&tdes->tail_lsa) || LSA_LE (&tdes->tail_lsa, &LOG_TDES_LAST_SYSOP (tdes)->lastparent_lsa))
    {
      /* No change. */
    }
  else
    {
      TRAN_STATE save_state;

      if (!LOG_CHECK_LOG_APPLIER (thread_p) && tdes->is_active_worker_transaction ()
	  && log_does_allow_replication () == true)
	{
	  repl_log_abort_after_lsa (tdes, LOG_TDES_LAST_SYSOP_PARENT_LSA (tdes));
	}

      /* Abort changes in system op. */
      save_state = tdes->state;
      tdes->state = TRAN_UNACTIVE_ABORTED;

      /* Rollback changes. */
      log_rollback (thread_p, tdes, LOG_TDES_LAST_SYSOP_PARENT_LSA (tdes));
      tdes->m_modified_classes.decache_heap_repr (*LOG_TDES_LAST_SYSOP_PARENT_LSA (tdes));

      /* Log abort system operation. */
      sysop_end.type = LOG_SYSOP_END_ABORT;
      sysop_end.lastparent_lsa = *LOG_TDES_LAST_SYSOP_PARENT_LSA (tdes);
      sysop_end.prv_topresult_lsa = tdes->tail_topresult_lsa;
      log_append_sysop_end (thread_p, tdes, &sysop_end, 0, NULL);

      /* Remember last partial result */
      LSA_COPY (&tdes->tail_topresult_lsa, &tdes->tail_lsa);

      /* Restore transaction state. */
      tdes->state = save_state;
    }

  log_sysop_end_final (thread_p, tdes);
}

/*
 * log_sysop_attach_to_outer () - Attach system operation to its immediate parent (another system operation or, if this
 *				  is top system operation, to transaction descriptor).
 *
 * return	 : Void.
 * thread_p (in) : Thread entry.
 */
void
log_sysop_attach_to_outer (THREAD_ENTRY * thread_p)
{
  int tran_index;
  LOG_TDES *tdes = NULL;

  log_sysop_end_begin (thread_p, &tran_index, &tdes);
  if (tdes == NULL)
    {
      assert_release (false);
      return;
    }

  /* Is attach to outer allowed? */
  if (tdes->topops.last == 0 && (!LOG_ISTRAN_ACTIVE (tdes) || tdes->is_system_transaction ()))
    {
      /* Nothing to attach to. Be conservative and commit the transaction. */
      assert_release (false);
      log_sysop_commit (thread_p);
      return;
    }

  /* Attach to outer: transfer postpone LSA. Not much to do really :) */
  if (tdes->topops.last - 1 >= 0)
    {
      if (LSA_ISNULL (&tdes->topops.stack[tdes->topops.last - 1].posp_lsa))
	{
	  LSA_COPY (&tdes->topops.stack[tdes->topops.last - 1].posp_lsa,
		    &tdes->topops.stack[tdes->topops.last].posp_lsa);
	}
    }
  else
    {
      if (LSA_ISNULL (&tdes->posp_nxlsa))
	{
	  LSA_COPY (&tdes->posp_nxlsa, &tdes->topops.stack[tdes->topops.last].posp_lsa);
	}
    }

  log_sysop_end_final (thread_p, tdes);
}

/*
 * log_sysop_get_level () - Get current system operation level. If no system operation is started, it returns -1.
 *
 * return        : System op level
 * thread_p (in) : Thread entry
 */
STATIC_INLINE int
log_sysop_get_level (THREAD_ENTRY * thread_p)
{
  int tran_index;
  LOG_TDES *tdes = NULL;

  log_sysop_get_tran_index_and_tdes (thread_p, &tran_index, &tdes);
  if (tdes == NULL)
    {
      assert_release (false);
      return -1;
    }
  return tdes->topops.last;
}

/*
 * log_sysop_get_tran_index_and_tdes () - Get transaction descriptor for system operations (in case of VACUUM, it will
 *                                        return the thread special tdes instead of system tdes).
 *
 * return               : Void
 * thread_p (in)        : Thread entry
 * tran_index_out (out) : Transaction index
 * tdes_out (out)       : Transaction descriptor
 */
STATIC_INLINE void
log_sysop_get_tran_index_and_tdes (THREAD_ENTRY * thread_p, int *tran_index_out, LOG_TDES ** tdes_out)
{
  *tran_index_out = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  *tdes_out = LOG_FIND_TDES (*tran_index_out);
  if (*tdes_out == NULL)
    {
      assert_release (false);
      return;
    }
}

/*
 * log_check_system_op_is_started () - Check system op is started.
 *
 * return	 : Error code.
 * thread_p (in) : Thread entry.
 */
bool
log_check_system_op_is_started (THREAD_ENTRY * thread_p)
{
  LOG_TDES *tdes;		/* Transaction descriptor */
  int tran_index;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL)
    {
      assert_release (false);
      return false;
    }

  if (!LOG_IS_SYSTEM_OP_STARTED (tdes))
    {
      assert_release (false);
      return false;
    }

  return true;
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
  LOG_TDES *tdes;		/* Transaction descriptor */
  int tran_index;
  int error_code = NO_ERROR;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
      error_code = ER_LOG_UNKNOWN_TRANINDEX;
      return NULL;
    }

  if (tdes->topops.last < 0)
    {
      LSA_SET_NULL (parent_lsa);
      return NULL;
    }

  LSA_COPY (parent_lsa, &tdes->topops.stack[tdes->topops.last].lastparent_lsa);

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
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
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
 * NOTE: Find if it is safe to skip undo logging for data related to given file.
 *       Some rcvindex values should never be skipped.
 */
static bool
log_can_skip_undo_logging (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex, const LOG_TDES * tdes, LOG_DATA_ADDR * addr)
{
  /*
   * Some log record types (rcvindex) should never be skipped.
   * In the case of LINK_PERM_VOLEXT, the link of a permanent temp volume must be logged to support media failures.
   * See also log_can_skip_redo_logging.
   */
  if (LOG_ISUNSAFE_TO_SKIP_RCVINDEX (rcvindex))
    {
      return false;
    }

  if (tdes->is_system_worker_transaction () && !tdes->is_under_sysop ())
    {
      /* If vacuum worker has not started a system operation, it can skip using undo logging. */
      // note - maybe it is better to add an assert (false)?
      return true;
    }

  /*
   * Operation level undo can be skipped on temporary pages. For example, those of temporary files.
   * No-operational level undo (i.e., logical logging) can be skipped for temporary files.
   */
  if (addr->pgptr != NULL && pgbuf_is_lsa_temporary (addr->pgptr) == true)
    {
      /* why do we log temporary files */
      assert (false);
      return true;
    }

  if (addr->vfid == NULL || VFID_ISNULL (addr->vfid))
    {
      return false;
    }

  return false;
}

/*
 * log_can_skip_redo_logging - Is it safe to skip redo logging for given file ?
 *
 * return:
 *
 *   rcvindex(in): Index to recovery function
 *   ignore_tdes(in):
 *   addr(in): Address (Volume, page, and offset) of data
 *
 * NOTE: Find if it is safe to skip redo logging for data related to given file.
 *       Redo logging can be skip on any temporary page. For example, pages of temporary files on any volume.
 *       Some rcvindex values should never be skipped.
 */
static bool
log_can_skip_redo_logging (LOG_RCVINDEX rcvindex, const LOG_TDES * ignore_tdes, LOG_DATA_ADDR * addr)
{
  /*
   * Some log record types (rcvindex) should never be skipped.
   * In the case of LINK_PERM_VOLEXT, the link of a permanent temp volume must be logged to support media failures.
   * See also log_can_skip_undo_logging.
   */
  if (LOG_ISUNSAFE_TO_SKIP_RCVINDEX (rcvindex))
    {
      return false;
    }

  /*
   * Operation level redo can be skipped on temporary pages. For example, those of temporary files
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
 * NOTE: The transaction is declared as committed with postpone actions.
 *       The transaction is not fully committed until all postpone actions are executed.
 *
 *       The postpone operations are not invoked by this function.
 */
static void
log_append_commit_postpone (THREAD_ENTRY * thread_p, LOG_TDES * tdes, LOG_LSA * start_postpone_lsa)
{
  LOG_REC_START_POSTPONE *start_posp;	/* Start postpone actions */
  LOG_PRIOR_NODE *node;
  LOG_LSA start_lsa;

  node = prior_lsa_alloc_and_copy_data (thread_p, LOG_COMMIT_WITH_POSTPONE, RV_NOT_DEFINED, NULL, 0, NULL, 0, NULL);
  if (node == NULL)
    {
      return;
    }

  start_posp = (LOG_REC_START_POSTPONE *) node->data_header;
  LSA_COPY (&start_posp->posp_lsa, start_postpone_lsa);
  start_posp->at_time = time (NULL);

  start_lsa = prior_lsa_next_record (thread_p, node, tdes);

  tdes->state = TRAN_UNACTIVE_COMMITTED_WITH_POSTPONE;

  logpb_flush_pages (thread_p, &start_lsa);
}

/*
 * log_append_commit_postpone_obsolete - APPEND COMMIT WITH POSTPONE
 *
 * return: nothing
 *
 *   tdes(in/out): State structure of transaction being committed
 *   start_posplsa(in): Address where the first postpone log record start
 *
 * NOTE: The transaction is declared as committed with postpone actions.
 *       The transaction is not fully committed until all postpone actions are executed.
 *
 *       The postpone operations are not invoked by this function.
 */
static void
log_append_commit_postpone_obsolete (THREAD_ENTRY * thread_p, LOG_TDES * tdes, LOG_LSA * start_postpone_lsa)
{
  LOG_REC_START_POSTPONE_OBSOLETE *start_posp;	/* Start postpone actions */
  LOG_PRIOR_NODE *node;
  LOG_LSA start_lsa;

  node =
    prior_lsa_alloc_and_copy_data (thread_p, LOG_COMMIT_WITH_POSTPONE_OBSOLETE, RV_NOT_DEFINED, NULL, 0, NULL, 0, NULL);
  if (node == NULL)
    {
      return;
    }

  start_posp = (LOG_REC_START_POSTPONE_OBSOLETE *) node->data_header;
  LSA_COPY (&start_posp->posp_lsa, start_postpone_lsa);

  start_lsa = prior_lsa_next_record (thread_p, node, tdes);

  tdes->state = TRAN_UNACTIVE_COMMITTED_WITH_POSTPONE;

  logpb_flush_pages (thread_p, &start_lsa);
}

/*
 * log_append_sysop_start_postpone () - append a log record when system op starts postpone.
 *
 * return                    : void
 * thread_p (in)             : thread entry
 * tdes (in)                 : transaction descriptor
 * sysop_start_postpone (in) : start postpone log record
 * data_size (in)            : undo data size
 * data (in)                 : undo data
 */
static void
log_append_sysop_start_postpone (THREAD_ENTRY * thread_p, LOG_TDES * tdes,
				 LOG_REC_SYSOP_START_POSTPONE * sysop_start_postpone, int data_size, const char *data)
{
  LOG_PRIOR_NODE *node;

  node =
    prior_lsa_alloc_and_copy_data (thread_p, LOG_SYSOP_START_POSTPONE, RV_NOT_DEFINED, NULL, data_size, (char *) data,
				   0, NULL);
  if (node == NULL)
    {
      return;
    }

  *(LOG_REC_SYSOP_START_POSTPONE *) node->data_header = *sysop_start_postpone;
  (void) prior_lsa_next_record (thread_p, node, tdes);
}

/*
 * log_append_sysop_end () - append sys op end log record
 *
 * return         : void
 * thread_p (in)  : thread entry
 * tdes (in)      : transaction descriptor
 * sysop_end (in) : sys op end log record
 * data_size (in) : data size
 * data (in)      : recovery data
 */
static void
log_append_sysop_end (THREAD_ENTRY * thread_p, LOG_TDES * tdes, LOG_REC_SYSOP_END * sysop_end, int data_size,
		      const char *data)
{
  LOG_PRIOR_NODE *node = NULL;

  assert (tdes != NULL);
  assert (sysop_end != NULL);
  assert (data_size == 0 || data != NULL);

  node = prior_lsa_alloc_and_copy_data (thread_p, LOG_SYSOP_END, RV_NOT_DEFINED, NULL, data_size, data, 0, NULL);
  if (node == NULL)
    {
      /* Is this possible? */
      assert (false);
    }
  else
    {
      LOG_RCVINDEX rcvindex = RV_NOT_DEFINED;
      /* Save data head. */
      /* First save lastparent_lsa and prv_topresult_lsa. */
      LOG_LSA start_lsa = LSA_INITIALIZER;

      memcpy (node->data_header, sysop_end, node->data_header_length);

      if (sysop_end->type == LOG_SYSOP_END_LOGICAL_UNDO)
	{
	  rcvindex = sysop_end->undo.data.rcvindex;
	}
      else if (sysop_end->type == LOG_SYSOP_END_LOGICAL_MVCC_UNDO)
	{
	  rcvindex = sysop_end->mvcc_undo.undo.data.rcvindex;
	}

      if (LOG_MAY_CONTAIN_USER_DATA (rcvindex))
	{
	  /* Some cases of logical undo */
	  TDE_ALGORITHM tde_algo = TDE_ALGORITHM_NONE;

	  assert (sysop_end->vfid != NULL);
	  if (file_get_tde_algorithm (thread_p, sysop_end->vfid, PGBUF_CONDITIONAL_LATCH, &tde_algo) != NO_ERROR)
	    {
	      tde_algo = TDE_ALGORITHM_NONE;
	      /* skip to encrypt in release */
	    }
	  if (tde_algo != TDE_ALGORITHM_NONE)
	    {
	      if (prior_set_tde_encrypted (node, rcvindex) != NO_ERROR)
		{
		  assert (false);
		  return;
		}
	    }
	}

      start_lsa = prior_lsa_next_record (thread_p, node, tdes);
      assert (!LSA_ISNULL (&start_lsa));
    }
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
log_append_repl_info_internal (THREAD_ENTRY * thread_p, LOG_TDES * tdes, bool is_commit, int with_lock)
{
  LOG_REPL_RECORD *repl_rec;
  LOG_REC_REPLICATION *log;
  LOG_PRIOR_NODE *node;

  if (tdes->append_repl_recidx == -1	/* the first time */
      || is_commit)
    {
      tdes->append_repl_recidx = 0;
    }

  /* there is any replication info */
  while (tdes->append_repl_recidx < tdes->cur_repl_record)
    {
      repl_rec = (LOG_REPL_RECORD *) (&(tdes->repl_records[tdes->append_repl_recidx]));

      if ((repl_rec->repl_type == LOG_REPLICATION_DATA || repl_rec->repl_type == LOG_REPLICATION_STATEMENT)
	  && ((is_commit && repl_rec->must_flush != LOG_REPL_DONT_NEED_FLUSH)
	      || repl_rec->must_flush == LOG_REPL_NEED_FLUSH))
	{
	  node =
	    prior_lsa_alloc_and_copy_data (thread_p, repl_rec->repl_type, RV_NOT_DEFINED, NULL, repl_rec->length,
					   repl_rec->repl_data, 0, NULL);
	  if (node == NULL)
	    {
	      assert (false);
	      continue;
	    }

	  if (repl_rec->tde_encrypted)
	    {
	      if (prior_set_tde_encrypted (node, repl_rec->rcvindex) != NO_ERROR)
		{
		  assert (false);
		  continue;
		}
	    }

	  log = (LOG_REC_REPLICATION *) node->data_header;
	  if (repl_rec->rcvindex == RVREPL_DATA_DELETE || repl_rec->rcvindex == RVREPL_STATEMENT)
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
	      (void) prior_lsa_next_record_with_lock (thread_p, node, tdes);
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

void
log_append_repl_info (THREAD_ENTRY * thread_p, LOG_TDES * tdes, bool is_commit)
{
  log_append_repl_info_internal (thread_p, tdes, is_commit, LOG_PRIOR_LSA_WITHOUT_LOCK);
}

static void
log_append_repl_info_with_lock (THREAD_ENTRY * thread_p, LOG_TDES * tdes, bool is_commit)
{
  log_append_repl_info_internal (thread_p, tdes, is_commit, LOG_PRIOR_LSA_WITH_LOCK);
}

/*
 * log_append_repl_info_and_commit_log - append repl log along with commit log.
 *
 * return: none
 *
 *   tdes(in):
 *   commit_lsa(out): LSA of commit log
 *
 * NOTE: Atomic write of replication log and commit log is crucial for replication consistencies.
 *       When a commit log of others is written in the middle of one's replication and commit log,
 *       a restart of replication will break consistencies of slaves/replicas.
 */
static void
log_append_repl_info_and_commit_log (THREAD_ENTRY * thread_p, LOG_TDES * tdes, LOG_LSA * commit_lsa)
{
  if (tdes->has_supplemental_log)
    {
      log_append_supplemental_info (thread_p, LOG_SUPPLEMENT_TRAN_USER, strlen (tdes->client.get_db_user ()),
				    tdes->client.get_db_user ());

      tdes->has_supplemental_log = false;
    }


  log_Gl.prior_info.prior_lsa_mutex.lock ();

  log_append_repl_info_with_lock (thread_p, tdes, true);
  log_append_commit_log_with_lock (thread_p, tdes, commit_lsa);

  log_Gl.prior_info.prior_lsa_mutex.unlock ();
}

/*
 * log_append_donetime_internal - APPEND COMMIT/ABORT LOG RECORD ALONG WITH TIME OF TERMINATION.
 *
 * return: none
 *
 *   tdes(in):
 *   eot_lsa(out): LSA of COMMIT/ABORT log
 *   iscommitted(in): Is transaction been finished as committed?
 *   with_lock(in): whether it has mutex or not.
 *
 * NOTE: a commit or abort record is recorded along with the current time as the termination time of the transaction.
 */
static void
log_append_donetime_internal (THREAD_ENTRY * thread_p, LOG_TDES * tdes, LOG_LSA * eot_lsa, LOG_RECTYPE iscommitted,
			      enum LOG_PRIOR_LSA_LOCK with_lock)
{
  LOG_REC_DONETIME *donetime;
  LOG_PRIOR_NODE *node;
  LOG_LSA lsa;

  eot_lsa->pageid = NULL_PAGEID;
  eot_lsa->offset = NULL_OFFSET;

  node = prior_lsa_alloc_and_copy_data (thread_p, iscommitted, RV_NOT_DEFINED, NULL, 0, NULL, 0, NULL);
  if (node == NULL)
    {
      /* FIXME */
      return;
    }

  donetime = (LOG_REC_DONETIME *) node->data_header;
  donetime->at_time = time (NULL);

  if (with_lock == LOG_PRIOR_LSA_WITH_LOCK)
    {
      lsa = prior_lsa_next_record_with_lock (thread_p, node, tdes);
    }
  else
    {
      lsa = prior_lsa_next_record (thread_p, node, tdes);
    }

  LSA_COPY (eot_lsa, &lsa);
}

/*
 * log_change_tran_as_completed - change the state of a transaction as committed/aborted
 *
 * return: nothing
 *
 *   tdes(in/out): State structure of transaction being committed/aborted.
 *   iscommitted(in): Is transaction been finished as committed ?
 *   lsa(in): commit lsa to flush logs
 *
 */
static void
log_change_tran_as_completed (THREAD_ENTRY * thread_p, LOG_TDES * tdes, LOG_RECTYPE iscommitted, LOG_LSA * lsa)
{
  if (iscommitted == LOG_COMMIT)
    {
      tdes->state = TRAN_UNACTIVE_COMMITTED;

      log_Stat.commit_count++;

      logpb_flush_pages (thread_p, lsa);
    }
  else
    {
      tdes->state = TRAN_UNACTIVE_ABORTED;

#if defined(SERVER_MODE)
      // [TODO] Here is an argument: committers are waiting for flush. Then, why not aborters?
      // The current fix minimizes potential impacts but it may miss some other cases.
      // I think it might be better to remove the condition and let aborters also wait for flush.
      // Maybe in the next milestone.
      if (BO_IS_SERVER_RESTARTED () && VOLATILE_ACCESS (log_Gl.run_nxchkpt_atpageid, INT64) == NULL_PAGEID)
	{
	  /* Flush the log in case that checkpoint is started. Otherwise, the current transaction
	   * may finish, but its LOG_ABORT not flushed yet. The checkpoint can advance with smallest
	   * LSA. Also, VACUUM can finalize cleaning. So, the archive may be removed. If the server crashes,
	   * at recovery, the current transaction must be aborted. But, some of its log records are in
	   * the archive that was previously removed => crash. Fixed, by forcing log flush before ending.
	   */
	  logpb_flush_pages (thread_p, lsa);
	}
#endif
    }

#if !defined (NDEBUG)
  if (prm_get_bool_value (PRM_ID_LOG_TRACE_DEBUG))
    {
      char time_val[CTIME_MAX];
      time_t xxtime = time (NULL);

      (void) ctime_r (&xxtime, time_val);
      fprintf (stdout,
	       msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOG,
			       ((iscommitted == LOG_COMMIT) ? MSGCAT_LOG_FINISH_COMMIT : MSGCAT_LOG_FINISH_ABORT)),
	       tdes->tran_index, tdes->trid, log_Gl.hdr.append_lsa.pageid, log_Gl.hdr.append_lsa.offset, time_val);
      fflush (stdout);
    }
#endif /* !NDEBUG */
}

/*
 * log_append_commit_log - append commit log record along with time of termination.
 *
 * return: nothing
 *
 *   tdes(in/out): State structure of transaction being committed.
 *   commit_lsa(out): LSA of commit log.
 */
static void
log_append_commit_log (THREAD_ENTRY * thread_p, LOG_TDES * tdes, LOG_LSA * commit_lsa)
{
  if (tdes->has_supplemental_log)
    {
      log_append_supplemental_info (thread_p, LOG_SUPPLEMENT_TRAN_USER, strlen (tdes->client.get_db_user ()),
				    tdes->client.get_db_user ());

      tdes->has_supplemental_log = false;
    }


  log_append_donetime_internal (thread_p, tdes, commit_lsa, LOG_COMMIT, LOG_PRIOR_LSA_WITHOUT_LOCK);
}

/*
 * log_append_commit_log_with_lock - append commit log record along with time of termination with prior lsa mutex.
 *
 * return: none
 *
 *   tdes(in/out): State structure of transaction being committed.
 *   commit_lsa(out): LSA of commit log.
 */
static void
log_append_commit_log_with_lock (THREAD_ENTRY * thread_p, LOG_TDES * tdes, LOG_LSA * commit_lsa)
{
  log_append_donetime_internal (thread_p, tdes, commit_lsa, LOG_COMMIT, LOG_PRIOR_LSA_WITH_LOCK);
}

/*
 * log_append_abort_log - append abort log record along with time of termination
 *
 * return: nothing
 *
 *   tdes(in/out): State structure of transaction being aborted.
 *   abort_lsa(out): LSA of abort log.
 */
static void
log_append_abort_log (THREAD_ENTRY * thread_p, LOG_TDES * tdes, LOG_LSA * abort_lsa)
{
  if (tdes->has_supplemental_log)
    {
      tdes->has_supplemental_log = false;
    }

  log_append_donetime_internal (thread_p, tdes, abort_lsa, LOG_ABORT, LOG_PRIOR_LSA_WITHOUT_LOCK);
}

/*
 * log_append_supplemental_info - append supplemental log record 
 *
 * return: nothing
 *
 *   rec_type (in): type of supplemental log record .
 *   length (in) : length of supplemental data length.
 *   data (in) : supplemental data
 *   
 */
void
log_append_supplemental_info (THREAD_ENTRY * thread_p, SUPPLEMENT_REC_TYPE rec_type, int length, const void *data)
{
  assert (prm_get_integer_value (PRM_ID_SUPPLEMENTAL_LOG) > 0);

  LOG_PRIOR_NODE *node;
  LOG_REC_SUPPLEMENT *supplement;

  LOG_TDES *tdes;
  int tran_index;

  LOG_ZIP *zip_undo = NULL;
  bool is_zipped = false;

  if (length >= log_Zip_min_size_to_compress && log_Zip_support)
    {
      zip_undo = log_append_get_zip_undo (thread_p);
      if (zip_undo == NULL)
	{
	  return;
	}

      log_zip (zip_undo, length, data);
      length = zip_undo->data_length;
      data = zip_undo->log_data;

      is_zipped = true;
    }

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);

  /* supplement data will be stored at undo data */
  node =
    prior_lsa_alloc_and_copy_data (thread_p, LOG_SUPPLEMENTAL_INFO, RV_NOT_DEFINED, NULL, length, (char *) data, 0,
				   NULL);
  if (node == NULL)
    {
      return;
    }

  supplement = (LOG_REC_SUPPLEMENT *) node->data_header;
  supplement->rec_type = rec_type;
  if (is_zipped)
    {
      supplement->length = MAKE_ZIP_LEN (zip_undo->data_length);
    }
  else
    {
      supplement->length = length;
    }

  prior_lsa_next_record (thread_p, node, tdes);
}

int
log_append_supplemental_lsa (THREAD_ENTRY * thread_p, SUPPLEMENT_REC_TYPE rec_type, OID * classoid, LOG_LSA * undo_lsa,
			     LOG_LSA * redo_lsa)
{
  int size;

  /* sizeof (OID) = 8, sizeof (LOG_LSA) = 8, and data contains classoid and undo, redo lsa. 
   * OR_OID_SIZE and OR_LOG_LSA_SIZE are not used here, because this function just copy the memory, not using OR_PUT_* function*/
  char data[24];

  assert (prm_get_integer_value (PRM_ID_SUPPLEMENTAL_LOG) > 0);

  switch (rec_type)
    {
    case LOG_SUPPLEMENT_INSERT:
    case LOG_SUPPLEMENT_TRIGGER_INSERT:
      assert (redo_lsa != NULL);

      size = sizeof (OID) + sizeof (LOG_LSA);

      memcpy (data, classoid, sizeof (OID));
      memcpy (data + sizeof (OID), redo_lsa, sizeof (LOG_LSA));
      break;

    case LOG_SUPPLEMENT_UPDATE:
    case LOG_SUPPLEMENT_TRIGGER_UPDATE:
      assert (undo_lsa != NULL && redo_lsa != NULL);

      size = sizeof (OID) + sizeof (LOG_LSA) + sizeof (LOG_LSA);

      memcpy (data, classoid, sizeof (OID));
      memcpy (data + sizeof (OID), undo_lsa, sizeof (LOG_LSA));
      memcpy (data + sizeof (OID) + sizeof (LOG_LSA), redo_lsa, sizeof (LOG_LSA));
      break;

    case LOG_SUPPLEMENT_DELETE:
    case LOG_SUPPLEMENT_TRIGGER_DELETE:
      assert (undo_lsa != NULL);

      size = sizeof (OID) + sizeof (LOG_LSA);

      memcpy (data, classoid, sizeof (OID));
      memcpy (data + sizeof (OID), undo_lsa, sizeof (LOG_LSA));
      break;

    default:
      assert (false);
      return ER_FAILED;
    }

  log_append_supplemental_info (thread_p, rec_type, size, (void *) data);

  return NO_ERROR;
}

int
log_append_supplemental_undo_record (THREAD_ENTRY * thread_p, RECDES * undo_recdes)
{
  assert (undo_recdes != NULL);
  assert (prm_get_integer_value (PRM_ID_SUPPLEMENTAL_LOG) > 0);

  int length = undo_recdes->length + sizeof (undo_recdes->type);

  char *data = (char *) malloc (length);
  if (data == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, length);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  memcpy (data, &undo_recdes->type, sizeof (undo_recdes->type));
  memcpy (data + sizeof (undo_recdes->type), undo_recdes->data, undo_recdes->length);

  log_append_supplemental_info (thread_p, LOG_SUPPLEMENT_UNDO_RECORD, length, data);

  free_and_init (data);

  return NO_ERROR;
}

int
log_append_supplemental_serial (THREAD_ENTRY * thread_p, const char *serial_name, int cached_num, OID * classoid,
				const OID * serial_oid)
{
  assert (prm_get_integer_value (PRM_ID_SUPPLEMENTAL_LOG) > 0);

  int ddl_type = 1;
  int obj_type = 2;
  int data_len;
  char stmt[1024];
  char *supplemental_data = NULL;

  char *ptr, *start_ptr;

  LOG_TDES *tdes;
  if (cached_num == 0)
    {
      cached_num = 1;
    }

  sprintf (stmt, "SELECT SERIAL_NEXT_VALUE(%s, %d);", serial_name, cached_num);

  data_len = OR_INT_SIZE + OR_INT_SIZE + OR_OID_SIZE + OR_OID_SIZE + OR_INT_SIZE + or_packed_string_length (stmt, NULL);

  supplemental_data = (char *) malloc (data_len + MAX_ALIGNMENT);
  if (supplemental_data == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, data_len + MAX_ALIGNMENT);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  ptr = start_ptr = supplemental_data;

  ptr = or_pack_int (ptr, ddl_type);
  ptr = or_pack_int (ptr, obj_type);
  ptr = or_pack_oid (ptr, classoid);
  ptr = or_pack_oid (ptr, serial_oid);
  ptr = or_pack_int (ptr, strlen (stmt));
  ptr = or_pack_string (ptr, stmt);

  data_len = ptr - start_ptr;

  log_append_supplemental_info (thread_p, LOG_SUPPLEMENT_DDL, data_len, (void *) supplemental_data);

  free_and_init (supplemental_data);

  return NO_ERROR;
}

/*
 * log_add_to_modified_class_list -
 *
 * return:
 *
 *   classname(in):
 *   class_oid(in):
 *
 * NOTE: Function for LOG_TDES.modified_class_list
 *       This list keeps the following information:
 *        {name, OID} of modified class and LSA for the last modification
 */
int
log_add_to_modified_class_list (THREAD_ENTRY * thread_p, const char *classname, const OID * class_oid)
{
  LOG_TDES *tdes;
  int tran_index;

  assert (classname != NULL);
  assert (class_oid != NULL);
  assert (!OID_ISNULL (class_oid));
  assert (class_oid->volid >= 0);	/* is not temp_oid */

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL)
    {
      return ER_FAILED;
    }

  tdes->m_modified_classes.add (classname, *class_oid, tdes->tail_lsa);
  return NO_ERROR;
}

/*
 * log_is_class_being_modified () - check if a class is being modified by the transaction which is executed by the
 *				    thread parameter
 * return : true if the class is being modified, false otherwise
 * thread_p (in)  : thread entry
 * class_oid (in) : class identifier
 */
bool
log_is_class_being_modified (THREAD_ENTRY * thread_p, const OID * class_oid)
{
  LOG_TDES *tdes;
  int tran_index;

  assert (class_oid != NULL);
  assert (!OID_ISNULL (class_oid));
  assert (class_oid->volid >= 0);	/* is not temp_oid */

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);

  if (tdes == NULL)
    {
      /* this is an error but this is not the place for handling it */
      return false;
    }

  return tdes->m_modified_classes.has_class (*class_oid);
}

/*
 * log_cleanup_modified_class -
 *
 * return:
 *
 *   t(in):
 *   arg(in):
 *
 * NOTE: Function for LOG_TDES.modified_class_list
 *       This will be used to decache the class representations and XASLs when a transaction is finished.
 */
static void
log_cleanup_modified_class (const tx_transient_class_entry & t, bool & stop)
{
  THREAD_ENTRY *thread_p = thread_get_thread_entry_info ();

  /* decache this class from the partitions cache also */
  (void) partition_decache_class (thread_p, &t.m_class_oid);

  /* remove XASL cache entries which are relevant with this class */
  xcache_remove_by_oid (thread_p, &t.m_class_oid);
  /* remove filter predicate cache entries which are relevant with this class */
  fpcache_remove_by_class (thread_p, &t.m_class_oid);
}

extern int locator_drop_transient_class_name_entries (THREAD_ENTRY * thread_p, LOG_LSA * savep_lsa);

/*
 * log_cleanup_modified_class_list -
 *
 * return:
 *
 *   tdes(in):
 *   savept_lsa(in): savepoint lsa to rollback
 *   bool(in): release the memory or not
 *   decache_classrepr(in): decache the class representation or not
 *
 * NOTE: Function for LOG_TDES.modified_class_list
 */
static void
log_cleanup_modified_class_list (THREAD_ENTRY * thread_p, LOG_TDES * tdes, LOG_LSA * savept_lsa, bool release,
				 bool decache_classrepr)
{
  if (decache_classrepr)
    {
      // decache heap representations
      tdes->m_modified_classes.decache_heap_repr (savept_lsa != NULL ? *savept_lsa : NULL_LSA);
    }
  tdes->m_modified_classes.map (log_cleanup_modified_class);

  /* always execute for defense */
  (void) locator_drop_transient_class_name_entries (thread_p, savept_lsa);

  if (release)
    {
      tdes->m_modified_classes.clear ();
    }
}

/*
 * log_commit_local - Perform the local commit operations of a transaction
 *
 * return: state of commit operation
 *
 *   tdes(in/out): State structure of transaction of the log record
 *   retain_lock(in): false = release locks (default)
 *                    true  = retain locks
 *   is_local_tran(in): Is a local transaction?
 *
 * NOTE:  Commit the current transaction locally. If there are postpone actions, the transaction is declared
 *        committed_with_postpone_actions by logging a log record indicating this state. Then, the postpone actions
 *        are executed. When the transaction is declared as fully committed, the locks acquired by the transaction
 *        are released. A committed transaction is not subject to deadlock when postpone operations are executed.
 *	  The function returns the state of the transaction(i.e. whether it is completely committed or not).
 */
TRAN_STATE
log_commit_local (THREAD_ENTRY * thread_p, LOG_TDES * tdes, bool retain_lock, bool is_local_tran)
{
  qmgr_clear_trans_wakeup (thread_p, tdes->tran_index, false, false);

  /* tx_lob_locator_clear and logtb_complete_mvcc operations must be done before entering unactive state because
   * they do some logging. We must NOT log (or do other regular changes to the database) after the transaction enters
   * the unactive state because of the following scenario: 1. enter TRAN_UNACTIVE_WILL_COMMIT state 2. a checkpoint
   * occurs and finishes. All active transactions are saved in log including their state. Our transaction will be saved
   * with TRAN_UNACTIVE_WILL_COMMIT state. 3. a crash occurs before our logging. Here, for example in case of unique
   * statistics, we will lost logging of unique statistics. 4. A recovery will occur. Because our transaction was saved
   * at checkpoint with TRAN_UNACTIVE_WILL_COMMIT state, it will be committed. Because we didn't logged the changes
   * made by the transaction we will not reflect the changes. They will be definitely lost. */
  tx_lob_locator_clear (thread_p, tdes, true, NULL);

  /* clear mvccid before releasing the locks. This operation must be done before do_postpone because it stores unique
   * statistics for all B-trees and if an error occurs those operations and all operations of current transaction must
   * be rolled back. */
  logtb_complete_mvcc (thread_p, tdes, true);

  tdes->state = TRAN_UNACTIVE_WILL_COMMIT;
  /* undo_nxlsa is no longer required here and must be reset, in case checkpoint takes a snapshot of this transaction
   * during TRAN_UNACTIVE_WILL_COMMIT phase.
   */
  LSA_SET_NULL (&tdes->undo_nxlsa);

  /* destroy all transaction's remaining temporary files */
  file_tempcache_drop_tran_temp_files (thread_p);

  if (!LSA_ISNULL (&tdes->tail_lsa))
    {
      /*
       * Transaction updated data.
       */

      log_tran_do_postpone (thread_p, tdes);

      /* The files created by this transaction are not new files any longer. Close any query cursors at this moment
       * too. */
      if (tdes->first_save_entry != NULL)
	{
	  spage_free_saved_spaces (thread_p, tdes->first_save_entry);
	  tdes->first_save_entry = NULL;
	}

      log_cleanup_modified_class_list (thread_p, tdes, NULL, true, false);

      if (is_local_tran)
	{
	  LOG_LSA commit_lsa;

	  /* To write unlock log before releasing locks for transactional consistencies. When a transaction(T2) which
	   * is resumed by this committing transaction(T1) commits and a crash happens before T1 completes, transaction
	   * consistencies will be broken because T1 will be aborted during restart recovery and T2 was already
	   * committed. */
	  if (!LOG_CHECK_LOG_APPLIER (thread_p) && tdes->is_active_worker_transaction ()
	      && log_does_allow_replication () == true)
	    {
	      /* for the replication agent guarantee the order of transaction */
	      log_append_repl_info_and_commit_log (thread_p, tdes, &commit_lsa);
	    }
	  else
	    {
	      log_append_commit_log (thread_p, tdes, &commit_lsa);
	    }

	  if (retain_lock != true)
	    {
	      lock_unlock_all (thread_p);
	    }

	  /* Flush commit log and change the transaction state. */
	  log_change_tran_as_completed (thread_p, tdes, LOG_COMMIT, &commit_lsa);
	}
      else
	{
	  /* Postpone appending replication and commit log and releasing locks to log_complete_for_2pc. */
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

      if (retain_lock != true)
	{
	  lock_unlock_all (thread_p);
	}

      tdes->state = TRAN_UNACTIVE_COMMITTED;
    }

  return tdes->state;
}

/*
 * log_abort_local - Perform the local abort operations of a transaction
 *
 * return: state of abort operation
 *
 *   tdes(in/out): State structure of transaction of the log record
 *   is_local_tran(in): Is a local transaction? (It is not used at this point)
 *
 * NOTE: Abort the current transaction locally.
 *	 When the transaction is declared as fully aborted, the locks acquired by the transaction are released and
 *	 query cursors are closed.
 *       This function is used for both local and coordinator transactions.
 */
TRAN_STATE
log_abort_local (THREAD_ENTRY * thread_p, LOG_TDES * tdes, bool is_local_tran)
{
  qmgr_clear_trans_wakeup (thread_p, tdes->tran_index, false, true);

  tdes->state = TRAN_UNACTIVE_ABORTED;

  /* destroy transaction's temporary files */
  file_tempcache_drop_tran_temp_files (thread_p);

  if (!LSA_ISNULL (&tdes->tail_lsa))
    {
      /*
       * Transaction updated data.
       */

      log_rollback (thread_p, tdes, NULL);

      log_update_global_btid_online_index_stats (thread_p);

      log_cleanup_modified_class_list (thread_p, tdes, NULL, true, true);

      if (tdes->first_save_entry != NULL)
	{
	  spage_free_saved_spaces (thread_p, tdes->first_save_entry);
	  tdes->first_save_entry = NULL;
	}

      /* clear mvccid before releasing the locks */
      logtb_complete_mvcc (thread_p, tdes, false);

      /* It is safe to release locks here, since we already completed abort. */
      lock_unlock_all (thread_p);
    }
  else
    {
      /*
       * Transaction did not update anything or we are not logging
       */

      if (tdes->first_save_entry != NULL)
	{
	  spage_free_saved_spaces (thread_p, tdes->first_save_entry);
	  tdes->first_save_entry = NULL;
	}

      /* clear mvccid before releasing the locks */
      logtb_complete_mvcc (thread_p, tdes, false);

      lock_unlock_all (thread_p);

      /* There is no need to create a new transaction identifier */
    }

  tx_lob_locator_clear (thread_p, tdes, false, NULL);

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
  LOG_TDES *tdes;		/* Transaction descriptor */
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
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
      error_code = ER_LOG_UNKNOWN_TRANINDEX;
      return TRAN_UNACTIVE_UNKNOWN;
    }
  assert (!tdes->is_system_worker_transaction ());

  if (!LOG_ISTRAN_ACTIVE (tdes) && !LOG_ISTRAN_2PC_PREPARE (tdes) && LOG_ISRESTARTED ())
    {
      /* May be a system error since transaction is not active.. cannot be committed */
#if defined(CUBRID_DEBUG)
      er_log_debug (ARG_FILE_LINE,
		    "log_commit: Transaction %d (index = %d) is"
		    " not active and cannot be committed. Its state is %s\n", tdes->trid, tdes->tran_index,
		    log_state_string (tdes->state));
#endif /* CUBRID_DEBUG */
      return tdes->state;
    }

  if (tdes->topops.last >= 0)
    {
      /* This is likely a system error since the transaction is being committed when there are system permanent
       * operations attached to it. Commit those operations too */
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_LOG_HAS_TOPOPS_DURING_COMMIT_ABORT, 2, tdes->trid,
	      tdes->tran_index);
      assert (false);
      while (tdes->topops.last >= 0)
	{
	  log_sysop_attach_to_outer (thread_p);
	}
    }

  tdes->m_multiupd_stats.clear ();

  if (log_2pc_clear_and_is_tran_distributed (tdes))
    {
      /* This is the coordinator of a distributed transaction If we are in prepare to commit mode. I cannot be the
       * root coordinator, so the decision has been taken at this moment by the root coordinator */
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
       * This is a local transaction or is a participant of a distributed transaction
       */
      state = log_commit_local (thread_p, tdes, retain_lock, true);
      state = log_complete (thread_p, tdes, LOG_COMMIT, LOG_NEED_NEWTRID, LOG_ALREADY_WROTE_EOT_LOG);
    }

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

  perfmon_inc_stat (thread_p, PSTAT_TRAN_NUM_COMMITS);

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
  LOG_TDES *tdes;		/* Transaction descriptor */
  bool decision;
  int error_code = NO_ERROR;

  if (tran_index == NULL_TRAN_INDEX)
    {
      tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
    }

  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
      error_code = ER_LOG_UNKNOWN_TRANINDEX;
      return TRAN_UNACTIVE_UNKNOWN;
    }
  assert (!tdes->is_system_worker_transaction ());

  if (LOG_HAS_LOGGING_BEEN_IGNORED ())
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_CORRUPTED_DB_DUE_NOLOGGING, 0);
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
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_LOG_HAS_TOPOPS_DURING_COMMIT_ABORT, 2, tdes->trid,
	      tdes->tran_index);
      assert (false);
      while (tdes->topops.last >= 0)
	{
	  log_sysop_attach_to_outer (thread_p);
	}
    }

  tdes->m_multiupd_stats.clear ();

  /*
   * If we are in prepare to commit mode. I cannot be the root coodinator,
   * so the decision has already been taken at this moment by the root
   * coordinator. If a distributed transaction is not in 2PC, the decision
   * has been taken without using the 2PC.
   */

  if (log_2pc_clear_and_is_tran_distributed (tdes))
    {
      /* This is the coordinator of a distributed transaction */
      state = log_2pc_commit (thread_p, tdes, LOG_2PC_EXECUTE_ABORT_DECISION, &decision);
    }
  else
    {
      /*
       * This is a local transaction or is a participant of a distributed transaction.
       * Perform the server rollback first.
       */
      state = log_abort_local (thread_p, tdes, true);
      state = log_complete (thread_p, tdes, LOG_ABORT, LOG_NEED_NEWTRID, LOG_NEED_TO_WRITE_EOT_LOG);
    }

  perfmon_inc_stat (thread_p, PSTAT_TRAN_NUM_ROLLBACKS);

  return state;
}

/*
 * log_abort_partial - ABORT ACTIONS OF A TRANSACTION TO A SAVEPOINT
 *
 * return: state of partial aborted operation.
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
log_abort_partial (THREAD_ENTRY * thread_p, const char *savepoint_name, LOG_LSA * savept_lsa)
{
  LOG_TDES *tdes;		/* Transaction descriptor */
  int tran_index;

  /* Find current transaction descriptor */
  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
      return TRAN_UNACTIVE_UNKNOWN;
    }

  if (LOG_HAS_LOGGING_BEEN_IGNORED ())
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_CORRUPTED_DB_DUE_NOLOGGING, 0);
      return tdes->state;
    }

  if (!LOG_ISTRAN_ACTIVE (tdes))
    {
      /*
       * May be a system error: Transaction is not in an active state
       */
      return tdes->state;
    }

  if (savepoint_name == NULL || log_get_savepoint_lsa (thread_p, savepoint_name, tdes, savept_lsa) == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_UNKNOWN_SAVEPOINT, 1, savepoint_name);
      return TRAN_UNACTIVE_UNKNOWN;
    }

  if (tdes->topops.last >= 0)
    {
      /*
       * This is likely a system error since the transaction is being partially
       * aborted when there are nested top system permanent operations
       * attached to it. Abort those operations too.
       */
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_LOG_HAS_TOPOPS_DURING_COMMIT_ABORT, 2, tdes->trid,
	      tdes->tran_index);
      assert (false);
      while (tdes->topops.last >= 0)
	{
	  log_sysop_attach_to_outer (thread_p);
	}
    }

  log_sysop_start (thread_p);

  LSA_COPY (&tdes->topops.stack[tdes->topops.last].lastparent_lsa, savept_lsa);

  if (!LSA_ISNULL (&tdes->posp_nxlsa))
    {
      if (LSA_LT (savept_lsa, &tdes->posp_nxlsa))
	{
	  LSA_COPY (&tdes->topops.stack[tdes->topops.last].posp_lsa, &tdes->posp_nxlsa);
	  LSA_SET_NULL (&tdes->posp_nxlsa);
	}
      else
	{
	  LSA_COPY (&tdes->topops.stack[tdes->topops.last].posp_lsa, savept_lsa);
	}
    }

  log_sysop_abort (thread_p);

  log_cleanup_modified_class_list (thread_p, tdes, savept_lsa, false, true);

  tx_lob_locator_clear (thread_p, tdes, false, savept_lsa);

  /*
   * The following is done so that if we go over several savepoints, they
   * get undefined and cannot get call by the user any longer.
   */
  LSA_COPY (&tdes->savept_lsa, savept_lsa);
  return TRAN_UNACTIVE_ABORTED;
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
 * NOTE: This function does not consider 2PC.
 *       Find the existing function as log_complete_for_2pc
 */
TRAN_STATE
log_complete (THREAD_ENTRY * thread_p, LOG_TDES * tdes, LOG_RECTYPE iscommitted, LOG_GETNEWTRID get_newtrid,
	      LOG_WRITE_EOT_LOG wrote_eot_log)
{
  TRAN_STATE state;		/* State of transaction */

  assert (iscommitted == LOG_COMMIT || iscommitted == LOG_ABORT);
  assert (!tdes->is_system_worker_transaction ());

  state = tdes->state;

  /*
   * DECLARE THE TRANSACTION AS COMPLETED
   */

  if (LSA_ISNULL (&tdes->tail_lsa))
    {
      /* Transaction did not update any data, thus we do not need to log a commit/abort log record. */
      if (iscommitted == LOG_COMMIT)
	{
	  state = TRAN_UNACTIVE_COMMITTED;
	}
      else
	{
	  state = TRAN_UNACTIVE_ABORTED;
	}

      logtb_clear_tdes (thread_p, tdes);
    }
  else
    {
      /*
       * Transaction updated data
       */
      if (wrote_eot_log == LOG_NEED_TO_WRITE_EOT_LOG)
	{
	  if (iscommitted == LOG_COMMIT)
	    {
	      LOG_LSA commit_lsa;

	      log_append_commit_log (thread_p, tdes, &commit_lsa);
	      log_change_tran_as_completed (thread_p, tdes, LOG_COMMIT, &commit_lsa);
	    }
	  else
	    {
	      LOG_LSA abort_lsa;

	      log_append_abort_log (thread_p, tdes, &abort_lsa);
	      log_change_tran_as_completed (thread_p, tdes, LOG_ABORT, &abort_lsa);
	    }

	  state = tdes->state;
	}
      else
	{
	  assert (iscommitted == LOG_COMMIT && state == TRAN_UNACTIVE_COMMITTED);
	}

      /* Unblock global oldest active update. */
      tdes->unlock_global_oldest_visible_mvccid ();

      if (iscommitted == LOG_COMMIT)
	{
	  log_Gl.mvcc_table.reset_transaction_lowest_active (LOG_FIND_THREAD_TRAN_INDEX (thread_p));
	}

      if (get_newtrid == LOG_NEED_NEWTRID)
	{
	  (void) logtb_get_new_tran_id (thread_p, tdes);
	}

      /* Finish the append operation and flush the log */
    }

  if (LOG_ISCHECKPOINT_TIME ())
    {
#if defined(SERVER_MODE)
      log_wakeup_checkpoint_daemon ();
#else /* SERVER_MODE */
      (void) logpb_checkpoint (thread_p);
#endif /* SERVER_MODE */
    }

  return state;
}

/*
 * log_complete_for_2pc - Complete in commit/abort mode the transaction whenever
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
log_complete_for_2pc (THREAD_ENTRY * thread_p, LOG_TDES * tdes, LOG_RECTYPE iscommitted, LOG_GETNEWTRID get_newtrid)
{
  TRAN_STATE state;		/* State of transaction */
  int new_tran_index;
  LOG_TDES *new_tdes;		/* New transaction descriptor when the transaction is transfered since the 2PC cannot
				 * be fully completed at this moment */
  int return_2pc_loose_tranindex;	/* Whether or not to return the current index */
  bool all_acks = true;
  int i;
  LOG_PRIOR_NODE *node;
  LOG_LSA start_lsa;
  int wait_msecs;

  assert (iscommitted == LOG_COMMIT || iscommitted == LOG_ABORT);
  assert (!tdes->is_system_worker_transaction ());

  state = tdes->state;

  if (tdes->coord != NULL && tdes->coord->ack_received != NULL)
    {
      /*
       * Make sure that all acknowledgments from participants have been received
       * before declaring the transaction as finished.
       */
      for (i = 0; i < tdes->coord->num_particps; i++)
	{
	  if (tdes->coord->ack_received[i] == false)
	    {
	      all_acks = false;
	      /*
	       * There are missing acknowledgments. The transaction cannot be
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
		  if (tdes->state != TRAN_UNACTIVE_COMMITTED_INFORMING_PARTICIPANTS)
		    {
		      node =
			prior_lsa_alloc_and_copy_data (thread_p, LOG_2PC_COMMIT_INFORM_PARTICPS, RV_NOT_DEFINED, NULL,
						       0, NULL, 0, NULL);
		      if (node == NULL)
			{
			  assert (false);
			  return state;
			}

		      tdes->state = TRAN_UNACTIVE_COMMITTED_INFORMING_PARTICIPANTS;
		      state = tdes->state;

		      start_lsa = prior_lsa_next_record (thread_p, node, tdes);

		      logpb_flush_pages (thread_p, &start_lsa);
		    }
		}
	      else
		{
		  /* aborted */
		  if (tdes->state != TRAN_UNACTIVE_ABORTED_INFORMING_PARTICIPANTS)
		    {
		      node =
			prior_lsa_alloc_and_copy_data (thread_p, LOG_2PC_ABORT_INFORM_PARTICPS, RV_NOT_DEFINED, NULL, 0,
						       NULL, 0, NULL);
		      if (node == NULL)
			{
			  assert (false);
			  return state;
			}

		      tdes->state = TRAN_UNACTIVE_ABORTED_INFORMING_PARTICIPANTS;
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
		  new_tran_index =
		    logtb_assign_tran_index (thread_p, NULL_TRANID, TRAN_RECOVERY, NULL, NULL, wait_msecs,
					     TRAN_SERIALIZABLE);
		  new_tdes = LOG_FIND_TDES (new_tran_index);
		  if (new_tran_index == NULL_TRAN_INDEX || new_tdes == NULL)
		    {
		      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_fully_completed");
		      return state;
		    }

		  /*
		   * Copy of tdes structures, and then reset memory allocated fields
		   * for only one the new or the old one.
		   */

		  // todo - this is completely unsafe.
		  memcpy (new_tdes, tdes, sizeof (*tdes));
		  new_tdes->tran_index = new_tran_index;
		  new_tdes->isloose_end = true;
		  /* new_tdes does not inherit topops fields */
		  new_tdes->topops.stack = NULL;
		  new_tdes->topops.last = -1;
		  new_tdes->topops.max = 0;

		  /* The old one keep the coordinator/participant information */
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
		  log_wakeup_checkpoint_daemon ();
#else /* SERVER_MODE */
		  (void) logpb_checkpoint (thread_p);
#endif /* SERVER_MODE */
		}

	      return state;
	    }
	}

      /*
       * All acknowledgments of participants have been received, declare the
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
	  fprintf (stdout,
		   msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOG,
				   ((iscommitted != LOG_ABORT) ? MSGCAT_LOG_FINISH_COMMIT : MSGCAT_LOG_FINISH_ABORT)),
		   tdes->tran_index, tdes->trid, log_Gl.hdr.append_lsa.pageid, log_Gl.hdr.append_lsa.offset, time_val);
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
      if (iscommitted == LOG_COMMIT)
	{
	  LOG_LSA commit_lsa;

	  /* To write unlock log before releasing locks for transactional consistencies. When a transaction(T2) which
	   * is resumed by this committing transaction(T1) commits and a crash happens before T1 completes, transaction
	   * * consistencies will be broken because T1 will be aborted during restart recovery and T2 was already
	   * committed. */
	  if (!LOG_CHECK_LOG_APPLIER (thread_p) && log_does_allow_replication () == true)
	    {
	      log_append_repl_info_and_commit_log (thread_p, tdes, &commit_lsa);
	    }
	  else
	    {
	      log_append_commit_log (thread_p, tdes, &commit_lsa);
	    }

	  log_change_tran_as_completed (thread_p, tdes, LOG_COMMIT, &commit_lsa);
	}
      else
	{
	  LOG_LSA abort_lsa;

	  log_append_abort_log (thread_p, tdes, &abort_lsa);
	  log_change_tran_as_completed (thread_p, tdes, LOG_ABORT, &abort_lsa);
	}

      state = tdes->state;

      /* now releases locks */
      lock_unlock_all (thread_p);

      /* Unblock global oldest active update. */
      tdes->unlock_global_oldest_visible_mvccid ();

      if (iscommitted == LOG_COMMIT)
	{
	  log_Gl.mvcc_table.reset_transaction_lowest_active (LOG_FIND_THREAD_TRAN_INDEX (thread_p));
	}

      /* If recovery restart operation, or, if this is a coordinator loose end transaction return this index and
       * decrement coordinator loose end transactions counter. */
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
      log_wakeup_checkpoint_daemon ();
#else /* SERVER_MODE */
      (void) logpb_checkpoint (thread_p);
#endif /* SERVER_MODE */
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

/*
 * log_hexa_dump () - Point recovery data as hexadecimals.
 *
 * return      : Void.
 * out_fp (in) : Print output.
 * length (in) : Recovery data length.
 * data (in)   : Recovery data.
 */
void
log_hexa_dump (FILE * out_fp, int length, void *data)
{
  char *ptr;			/* Pointer to data */
  int i;

  fprintf (out_fp, "  00000: ");
  for (i = 0, ptr = (char *) data; i < length; i++)
    {
      fprintf (out_fp, "%02X ", (unsigned char) (*ptr++));
      if (i % 16 == 15 && i != length)
	{
	  fprintf (out_fp, "\n  %05d: ", i + 1);
	}
    }
  fprintf (out_fp, "\n");
}

static void
log_repl_data_dump (FILE * out_fp, int length, void *data)
{
  char *ptr = (char *) data;
  char *class_name;
  DB_VALUE value;

  ptr = or_unpack_string_nocopy (ptr, &class_name);
  ptr = or_unpack_mem_value (ptr, &value);

  string_buffer sb;
  db_value_printer printer (sb);

  printer.describe_value (&value);
  fprintf (out_fp, "C[%s] K[%s]\n", class_name, sb.get_buffer ());
  pr_clear_value (&value);
}

static void
log_repl_schema_dump (FILE * out_fp, int length, void *data)
{
  char *ptr;
  int statement_type;
  char *class_name;
  char *sql;

  ptr = (char *) data;
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
log_dump_data (THREAD_ENTRY * thread_p, FILE * out_fp, int length, LOG_LSA * log_lsa, LOG_PAGE * log_page_p,
	       void (*dumpfun) (FILE *, int, void *), LOG_ZIP * log_dump_ptr)
{
  char *ptr;			/* Pointer to data to be printed */
  bool is_zipped = false;
  bool is_unzipped = false;
  /* Call the dumper function */

  /*
   * If data is contained in only one buffer, pass pointer directly.
   * Otherwise, allocate a contiguous area, copy the data and pass this
   * area. At the end deallocate the area
   */

  if (dumpfun == NULL)
    {
      /* Set default to log_hexa_dump */
      dumpfun = log_hexa_dump;
    }

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
	  (*dumpfun) (out_fp, (int) log_dump_ptr->data_length, log_dump_ptr->log_data);
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
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) length);
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
	  (*dumpfun) (out_fp, (int) log_dump_ptr->data_length, log_dump_ptr->log_data);
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
log_dump_header (FILE * out_fp, LOG_HEADER * log_header_p)
{
  time_t tmp_time;
  char time_val[CTIME_MAX];

  fprintf (out_fp, "\n ** DUMP LOG HEADER **\n");

  tmp_time = (time_t) log_header_p->db_creation;
  (void) ctime_r (&tmp_time, time_val);
  fprintf (out_fp,
	   "HDR: Magic Symbol = %s at disk location = %lld\n     Creation_time = %s"
	   "     Release = %s, Compatibility_disk_version = %g,\n"
	   "     Db_pagesize = %d, log_pagesize= %d, Shutdown = %d,\n"
	   "     Next_trid = %d, Next_mvcc_id = %llu, Num_avg_trans = %d, Num_avg_locks = %d,\n"
	   "     Num_active_log_pages = %d, First_active_log_page = %lld,\n"
	   "     Current_append = %lld|%d, Checkpoint = %lld|%d,\n", log_header_p->magic,
	   (long long) offsetof (LOG_PAGE, area), time_val, log_header_p->db_release, log_header_p->db_compatibility,
	   log_header_p->db_iopagesize, log_header_p->db_logpagesize, log_header_p->is_shutdown,
	   log_header_p->next_trid, (long long int) log_header_p->mvcc_next_id, log_header_p->avg_ntrans,
	   log_header_p->avg_nlocks, log_header_p->npages, (long long) log_header_p->fpageid,
	   LSA_AS_ARGS (&log_header_p->append_lsa), LSA_AS_ARGS (&log_header_p->chkpt_lsa));

  fprintf (out_fp,
	   "     Next_archive_pageid = %lld at active_phy_pageid = %d,\n"
	   "     Next_archive_num = %d, Last_archiv_num_for_syscrashes = %d,\n"
	   "     Last_deleted_arv_num = %d, has_logging_been_skipped = %d,\n"
	   "     bkup_lsa: level0 = %lld|%d, level1 = %lld|%d, level2 = %lld|%d,\n     Log_prefix = %s\n",
	   (long long int) log_header_p->nxarv_pageid, log_header_p->nxarv_phy_pageid, log_header_p->nxarv_num,
	   log_header_p->last_arv_num_for_syscrashes, log_header_p->last_deleted_arv_num,
	   log_header_p->has_logging_been_skipped, LSA_AS_ARGS (&log_header_p->bkup_level0_lsa),
	   LSA_AS_ARGS (&log_header_p->bkup_level1_lsa), LSA_AS_ARGS (&log_header_p->bkup_level2_lsa),
	   log_header_p->prefix_name);
}

static LOG_PAGE *
log_dump_record_undoredo (THREAD_ENTRY * thread_p, FILE * out_fp, LOG_LSA * log_lsa, LOG_PAGE * log_page_p,
			  LOG_ZIP * log_zip_p)
{
  LOG_REC_UNDOREDO *undoredo;
  int undo_length;
  int redo_length;
  LOG_RCVINDEX rcvindex;

  /* Read the DATA HEADER */
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*undoredo), log_lsa, log_page_p);
  undoredo = (LOG_REC_UNDOREDO *) ((char *) log_page_p->area + log_lsa->offset);
  fprintf (out_fp, ", Recv_index = %s, \n", rv_rcvindex_string (undoredo->data.rcvindex));
  fprintf (out_fp,
	   "     Volid = %d Pageid = %d Offset = %d,\n     Undo(Before) length = %d, Redo(After) length = %d,\n",
	   undoredo->data.volid, undoredo->data.pageid, undoredo->data.offset, (int) GET_ZIP_LEN (undoredo->ulength),
	   (int) GET_ZIP_LEN (undoredo->rlength));

  undo_length = undoredo->ulength;
  redo_length = undoredo->rlength;
  rcvindex = undoredo->data.rcvindex;

  LOG_READ_ADD_ALIGN (thread_p, sizeof (*undoredo), log_lsa, log_page_p);
  /* Print UNDO(BEFORE) DATA */
  fprintf (out_fp, "-->> Undo (Before) Data:\n");
  log_dump_data (thread_p, out_fp, undo_length, log_lsa, log_page_p, RV_fun[rcvindex].dump_undofun, log_zip_p);
  /* Print REDO (AFTER) DATA */
  fprintf (out_fp, "-->> Redo (After) Data:\n");
  log_dump_data (thread_p, out_fp, redo_length, log_lsa, log_page_p, RV_fun[rcvindex].dump_redofun, log_zip_p);

  return log_page_p;
}

static LOG_PAGE *
log_dump_record_undo (THREAD_ENTRY * thread_p, FILE * out_fp, LOG_LSA * log_lsa, LOG_PAGE * log_page_p,
		      LOG_ZIP * log_zip_p)
{
  LOG_REC_UNDO *undo;
  int undo_length;
  LOG_RCVINDEX rcvindex;

  /* Read the DATA HEADER */
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*undo), log_lsa, log_page_p);
  undo = (LOG_REC_UNDO *) ((char *) log_page_p->area + log_lsa->offset);

  fprintf (out_fp, ", Recv_index = %s,\n", rv_rcvindex_string (undo->data.rcvindex));
  fprintf (out_fp, "     Volid = %d Pageid = %d Offset = %d,\n     Undo (Before) length = %d,\n", undo->data.volid,
	   undo->data.pageid, undo->data.offset, (int) GET_ZIP_LEN (undo->length));

  undo_length = undo->length;
  rcvindex = undo->data.rcvindex;
  LOG_READ_ADD_ALIGN (thread_p, sizeof (*undo), log_lsa, log_page_p);

  /* Print UNDO(BEFORE) DATA */
  fprintf (out_fp, "-->> Undo (Before) Data:\n");
  log_dump_data (thread_p, out_fp, undo_length, log_lsa, log_page_p, RV_fun[rcvindex].dump_undofun, log_zip_p);

  return log_page_p;
}

static LOG_PAGE *
log_dump_record_redo (THREAD_ENTRY * thread_p, FILE * out_fp, LOG_LSA * log_lsa, LOG_PAGE * log_page_p,
		      LOG_ZIP * log_zip_p)
{
  LOG_REC_REDO *redo;
  int redo_length;
  LOG_RCVINDEX rcvindex;

  /* Read the DATA HEADER */
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*redo), log_lsa, log_page_p);
  redo = (LOG_REC_REDO *) ((char *) log_page_p->area + log_lsa->offset);

  fprintf (out_fp, ", Recv_index = %s,\n", rv_rcvindex_string (redo->data.rcvindex));
  fprintf (out_fp, "     Volid = %d Pageid = %d Offset = %d,\n     Redo (After) length = %d,\n", redo->data.volid,
	   redo->data.pageid, redo->data.offset, (int) GET_ZIP_LEN (redo->length));

  redo_length = redo->length;
  rcvindex = redo->data.rcvindex;
  LOG_READ_ADD_ALIGN (thread_p, sizeof (*redo), log_lsa, log_page_p);

  /* Print REDO(AFTER) DATA */
  fprintf (out_fp, "-->> Redo (After) Data:\n");
  log_dump_data (thread_p, out_fp, redo_length, log_lsa, log_page_p, RV_fun[rcvindex].dump_redofun, log_zip_p);

  return log_page_p;
}

static LOG_PAGE *
log_dump_record_mvcc_undoredo (THREAD_ENTRY * thread_p, FILE * out_fp, LOG_LSA * log_lsa, LOG_PAGE * log_page_p,
			       LOG_ZIP * log_zip_p)
{
  LOG_REC_MVCC_UNDOREDO *mvcc_undoredo;
  int undo_length;
  int redo_length;
  LOG_RCVINDEX rcvindex;

  /* Read the DATA HEADER */
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*mvcc_undoredo), log_lsa, log_page_p);
  mvcc_undoredo = (LOG_REC_MVCC_UNDOREDO *) ((char *) log_page_p->area + log_lsa->offset);
  fprintf (out_fp, ", Recv_index = %s, \n", rv_rcvindex_string (mvcc_undoredo->undoredo.data.rcvindex));
  fprintf (out_fp,
	   "     Volid = %d Pageid = %d Offset = %d,\n     Undo(Before) length = %d, Redo(After) length = %d,\n",
	   mvcc_undoredo->undoredo.data.volid, mvcc_undoredo->undoredo.data.pageid, mvcc_undoredo->undoredo.data.offset,
	   (int) GET_ZIP_LEN (mvcc_undoredo->undoredo.ulength), (int) GET_ZIP_LEN (mvcc_undoredo->undoredo.rlength));
  fprintf (out_fp, "     MVCCID = %llu, \n     Prev_mvcc_op_log_lsa = %lld|%d, \n     VFID = (%d, %d)",
	   (long long int) mvcc_undoredo->mvccid,
	   (long long int) mvcc_undoredo->vacuum_info.prev_mvcc_op_log_lsa.pageid,
	   (int) mvcc_undoredo->vacuum_info.prev_mvcc_op_log_lsa.offset, mvcc_undoredo->vacuum_info.vfid.volid,
	   mvcc_undoredo->vacuum_info.vfid.fileid);

  undo_length = mvcc_undoredo->undoredo.ulength;
  redo_length = mvcc_undoredo->undoredo.rlength;
  rcvindex = mvcc_undoredo->undoredo.data.rcvindex;

  LOG_READ_ADD_ALIGN (thread_p, sizeof (*mvcc_undoredo), log_lsa, log_page_p);
  /* Print UNDO(BEFORE) DATA */
  fprintf (out_fp, "-->> Undo (Before) Data:\n");
  log_dump_data (thread_p, out_fp, undo_length, log_lsa, log_page_p, RV_fun[rcvindex].dump_undofun, log_zip_p);
  /* Print REDO (AFTER) DATA */
  fprintf (out_fp, "-->> Redo (After) Data:\n");
  log_dump_data (thread_p, out_fp, redo_length, log_lsa, log_page_p, RV_fun[rcvindex].dump_redofun, log_zip_p);

  return log_page_p;
}

static LOG_PAGE *
log_dump_record_mvcc_undo (THREAD_ENTRY * thread_p, FILE * out_fp, LOG_LSA * log_lsa, LOG_PAGE * log_page_p,
			   LOG_ZIP * log_zip_p)
{
  LOG_REC_MVCC_UNDO *mvcc_undo;
  int undo_length;
  LOG_RCVINDEX rcvindex;

  /* Read the DATA HEADER */
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*mvcc_undo), log_lsa, log_page_p);
  mvcc_undo = (LOG_REC_MVCC_UNDO *) ((char *) log_page_p->area + log_lsa->offset);

  fprintf (out_fp, ", Recv_index = %s,\n", rv_rcvindex_string (mvcc_undo->undo.data.rcvindex));
  fprintf (out_fp, "     Volid = %d Pageid = %d Offset = %d,\n     Undo (Before) length = %d,\n",
	   mvcc_undo->undo.data.volid, mvcc_undo->undo.data.pageid, mvcc_undo->undo.data.offset,
	   (int) GET_ZIP_LEN (mvcc_undo->undo.length));
  fprintf (out_fp, "     MVCCID = %llu, \n     Prev_mvcc_op_log_lsa = %lld|%d, \n     VFID = (%d, %d)",
	   (long long int) mvcc_undo->mvccid, (long long int) mvcc_undo->vacuum_info.prev_mvcc_op_log_lsa.pageid,
	   (int) mvcc_undo->vacuum_info.prev_mvcc_op_log_lsa.offset, mvcc_undo->vacuum_info.vfid.volid,
	   mvcc_undo->vacuum_info.vfid.fileid);

  undo_length = mvcc_undo->undo.length;
  rcvindex = mvcc_undo->undo.data.rcvindex;
  LOG_READ_ADD_ALIGN (thread_p, sizeof (*mvcc_undo), log_lsa, log_page_p);

  /* Print UNDO(BEFORE) DATA */
  fprintf (out_fp, "-->> Undo (Before) Data:\n");
  log_dump_data (thread_p, out_fp, undo_length, log_lsa, log_page_p, RV_fun[rcvindex].dump_undofun, log_zip_p);

  return log_page_p;
}

static LOG_PAGE *
log_dump_record_mvcc_redo (THREAD_ENTRY * thread_p, FILE * out_fp, LOG_LSA * log_lsa, LOG_PAGE * log_page_p,
			   LOG_ZIP * log_zip_p)
{
  LOG_REC_MVCC_REDO *mvcc_redo;
  int redo_length;
  LOG_RCVINDEX rcvindex;

  /* Read the DATA HEADER */
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*mvcc_redo), log_lsa, log_page_p);
  mvcc_redo = (LOG_REC_MVCC_REDO *) ((char *) log_page_p->area + log_lsa->offset);

  fprintf (out_fp, ", Recv_index = %s,\n", rv_rcvindex_string (mvcc_redo->redo.data.rcvindex));
  fprintf (out_fp, "     Volid = %d Pageid = %d Offset = %d,\n     Redo (After) length = %d,\n",
	   mvcc_redo->redo.data.volid, mvcc_redo->redo.data.pageid, mvcc_redo->redo.data.offset,
	   (int) GET_ZIP_LEN (mvcc_redo->redo.length));
  fprintf (out_fp, "     MVCCID = %llu, \n", (long long int) mvcc_redo->mvccid);

  redo_length = mvcc_redo->redo.length;
  rcvindex = mvcc_redo->redo.data.rcvindex;
  LOG_READ_ADD_ALIGN (thread_p, sizeof (*mvcc_redo), log_lsa, log_page_p);

  /* Print REDO(AFTER) DATA */
  fprintf (out_fp, "-->> Redo (After) Data:\n");
  log_dump_data (thread_p, out_fp, redo_length, log_lsa, log_page_p, RV_fun[rcvindex].dump_redofun, log_zip_p);

  return log_page_p;
}

static LOG_PAGE *
log_dump_record_postpone (THREAD_ENTRY * thread_p, FILE * out_fp, LOG_LSA * log_lsa, LOG_PAGE * log_page_p)
{
  LOG_REC_RUN_POSTPONE *run_posp;
  int redo_length;
  LOG_RCVINDEX rcvindex;

  /* Read the DATA HEADER */
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*run_posp), log_lsa, log_page_p);
  run_posp = (LOG_REC_RUN_POSTPONE *) ((char *) log_page_p->area + log_lsa->offset);
  fprintf (out_fp, ", Recv_index = %s,\n", rv_rcvindex_string (run_posp->data.rcvindex));
  fprintf (out_fp,
	   "     Volid = %d Pageid = %d Offset = %d,\n     Run postpone (Redo/After) length = %d, corresponding"
	   " to\n         Postpone record with LSA = %lld|%d\n", run_posp->data.volid, run_posp->data.pageid,
	   run_posp->data.offset, run_posp->length, LSA_AS_ARGS (&run_posp->ref_lsa));

  redo_length = run_posp->length;
  rcvindex = run_posp->data.rcvindex;
  LOG_READ_ADD_ALIGN (thread_p, sizeof (*run_posp), log_lsa, log_page_p);

  /* Print RUN POSTPONE (REDO/AFTER) DATA */
  fprintf (out_fp, "-->> Run Postpone (Redo/After) Data:\n");
  log_dump_data (thread_p, out_fp, redo_length, log_lsa, log_page_p, RV_fun[rcvindex].dump_redofun, NULL);

  return log_page_p;
}

static LOG_PAGE *
log_dump_record_dbout_redo (THREAD_ENTRY * thread_p, FILE * out_fp, LOG_LSA * log_lsa, LOG_PAGE * log_page_p)
{
  LOG_REC_DBOUT_REDO *dbout_redo;
  int redo_length;
  LOG_RCVINDEX rcvindex;

  /* Read the data header */
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*dbout_redo), log_lsa, log_page_p);
  dbout_redo = ((LOG_REC_DBOUT_REDO *) ((char *) log_page_p->area + log_lsa->offset));

  redo_length = dbout_redo->length;
  rcvindex = dbout_redo->rcvindex;

  fprintf (out_fp, ", Recv_index = %s, Length = %d,\n", rv_rcvindex_string (rcvindex), redo_length);

  LOG_READ_ADD_ALIGN (thread_p, sizeof (*dbout_redo), log_lsa, log_page_p);

  /* Print Database External DATA */
  fprintf (out_fp, "-->> Database external Data:\n");
  log_dump_data (thread_p, out_fp, redo_length, log_lsa, log_page_p, RV_fun[rcvindex].dump_redofun, NULL);

  return log_page_p;
}

static LOG_PAGE *
log_dump_record_compensate (THREAD_ENTRY * thread_p, FILE * out_fp, LOG_LSA * log_lsa, LOG_PAGE * log_page_p)
{
  LOG_REC_COMPENSATE *compensate;
  int length_compensate;
  LOG_RCVINDEX rcvindex;

  /* Read the DATA HEADER */
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*compensate), log_lsa, log_page_p);
  compensate = (LOG_REC_COMPENSATE *) ((char *) log_page_p->area + log_lsa->offset);

  fprintf (out_fp, ", Recv_index = %s,\n", rv_rcvindex_string (compensate->data.rcvindex));
  fprintf (out_fp, "     Volid = %d Pageid = %d Offset = %d,\n     Compensate length = %d, Next_to_UNDO = %lld|%d\n",
	   compensate->data.volid, compensate->data.pageid, compensate->data.offset, compensate->length,
	   LSA_AS_ARGS (&compensate->undo_nxlsa));

  length_compensate = compensate->length;
  rcvindex = compensate->data.rcvindex;
  LOG_READ_ADD_ALIGN (thread_p, sizeof (*compensate), log_lsa, log_page_p);

  /* Print COMPENSATE DATA */
  fprintf (out_fp, "-->> Compensate Data:\n");
  log_dump_data (thread_p, out_fp, length_compensate, log_lsa, log_page_p, RV_fun[rcvindex].dump_undofun, NULL);

  return log_page_p;
}

static LOG_PAGE *
log_dump_record_commit_postpone (THREAD_ENTRY * thread_p, FILE * out_fp, LOG_LSA * log_lsa, LOG_PAGE * log_page_p)
{
  LOG_REC_START_POSTPONE *start_posp;
  LOG_REC_DONETIME *donetime;
  time_t tmp_time;
  char time_val[CTIME_MAX];

  /* Read the DATA HEADER */
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*start_posp), log_lsa, log_page_p);
  start_posp = (LOG_REC_START_POSTPONE *) ((char *) log_page_p->area + log_lsa->offset);
  tmp_time = (time_t) start_posp->at_time;
  (void) ctime_r (&tmp_time, time_val);
  fprintf (out_fp, ", First postpone record at before or after Page = %lld and offset = %d\n",
	   LSA_AS_ARGS (&start_posp->posp_lsa));
  fprintf (out_fp, "     Transaction finish time at = %s\n", time_val);

  return log_page_p;
}

static LOG_PAGE *
log_dump_record_commit_postpone_obsolete (THREAD_ENTRY * thread_p, FILE * out_fp, LOG_LSA * log_lsa,
					  LOG_PAGE * log_page_p)
{
  LOG_REC_START_POSTPONE_OBSOLETE *start_posp;

  /* Read the DATA HEADER */
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*start_posp), log_lsa, log_page_p);
  start_posp = (LOG_REC_START_POSTPONE_OBSOLETE *) ((char *) log_page_p->area + log_lsa->offset);
  fprintf (out_fp, ", First postpone record at before or after Page = %lld and offset = %d\n",
	   LSA_AS_ARGS (&start_posp->posp_lsa));

  return log_page_p;
}

static LOG_PAGE *
log_dump_record_transaction_finish (THREAD_ENTRY * thread_p, FILE * out_fp, LOG_LSA * log_lsa, LOG_PAGE * log_page_p)
{
  LOG_REC_DONETIME *donetime;
  time_t tmp_time;
  char time_val[CTIME_MAX];

  /* Read the DATA HEADER */
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*donetime), log_lsa, log_page_p);
  donetime = (LOG_REC_DONETIME *) ((char *) log_page_p->area + log_lsa->offset);
  tmp_time = (time_t) donetime->at_time;
  (void) ctime_r (&tmp_time, time_val);
  fprintf (out_fp, ",\n     Transaction finish time at = %s\n", time_val);

  return log_page_p;
}

static LOG_PAGE *
log_dump_record_replication (THREAD_ENTRY * thread_p, FILE * out_fp, LOG_LSA * log_lsa, LOG_PAGE * log_page_p)
{
  LOG_REC_REPLICATION *repl_log;
  int length;
  const char *type;
  void (*dump_function) (FILE *, int, void *);

  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*repl_log), log_lsa, log_page_p);
  repl_log = (LOG_REC_REPLICATION *) ((char *) log_page_p->area + log_lsa->offset);
  fprintf (out_fp, ", Target log lsa = %lld|%d\n", LSA_AS_ARGS (&repl_log->lsa));
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

  log_dump_data (thread_p, out_fp, length, log_lsa, log_page_p, dump_function, NULL);
  return log_page_p;
}

/*
 * log_dump_record_sysop_start_postpone () - dump system op start postpone log record
 *
 * return              : log page
 * thread_p (in)       : thread entry
 * out_fp (in/out)     : dump output
 * log_lsa (in/out)    : log lsa
 * log_page_p (in/out) : log page
 * log_zip_p (in/out)  : log unzip
 */
static LOG_PAGE *
log_dump_record_sysop_start_postpone (THREAD_ENTRY * thread_p, FILE * out_fp, LOG_LSA * log_lsa, LOG_PAGE * log_page_p,
				      LOG_ZIP * log_zip_p)
{
  LOG_REC_SYSOP_START_POSTPONE sysop_start_postpone;

  /* Read the DATA HEADER */
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (sysop_start_postpone), log_lsa, log_page_p);
  sysop_start_postpone = *((LOG_REC_SYSOP_START_POSTPONE *) ((char *) log_page_p->area + log_lsa->offset));

  (void) log_dump_record_sysop_end_internal (thread_p, &sysop_start_postpone.sysop_end, log_lsa, log_page_p, log_zip_p,
					     out_fp);
  fprintf (out_fp, "     postpone_lsa = %lld|%d \n", LSA_AS_ARGS (&sysop_start_postpone.posp_lsa));

  return log_page_p;
}

/*
 * log_dump_record_sysop_end_internal () - dump sysop end log record types
 *
 * return              : log page
 * thread_p (in)       : thread entry
 * sysop_end (in)      : system op end log record
 * log_lsa (in/out)    : LSA of undo data (logical undo only)
 * log_page_p (in/out) : page of undo data (logical undo only)
 * log_zip_p (in/out)  : log unzip
 * out_fp (in/out)     : dump output
 */
static LOG_PAGE *
log_dump_record_sysop_end_internal (THREAD_ENTRY * thread_p, LOG_REC_SYSOP_END * sysop_end, LOG_LSA * log_lsa,
				    LOG_PAGE * log_page_p, LOG_ZIP * log_zip_p, FILE * out_fp)
{
  int undo_length;
  LOG_RCVINDEX rcvindex;

  fprintf (out_fp, ",\n     Prev parent LSA = %lld|%d, Prev_topresult_lsa = %lld|%d, %s \n",
	   LSA_AS_ARGS (&sysop_end->lastparent_lsa), LSA_AS_ARGS (&sysop_end->prv_topresult_lsa),
	   log_sysop_end_type_string (sysop_end->type));
  switch (sysop_end->type)
    {
    case LOG_SYSOP_END_ABORT:
    case LOG_SYSOP_END_COMMIT:
      /* nothing else to print */
      break;
    case LOG_SYSOP_END_LOGICAL_COMPENSATE:
      fprintf (out_fp, "     compansate_lsa = %lld|%d \n", LSA_AS_ARGS (&sysop_end->compensate_lsa));
      break;
    case LOG_SYSOP_END_LOGICAL_RUN_POSTPONE:
      fprintf (out_fp, "     run_postpone_lsa = %lld|%d, postpone = %s \n",
	       LSA_AS_ARGS (&sysop_end->run_postpone.postpone_lsa),
	       sysop_end->run_postpone.is_sysop_postpone ? "sysop" : "transaction");
      break;
    case LOG_SYSOP_END_LOGICAL_UNDO:
      assert (log_lsa != NULL && log_page_p != NULL && log_zip_p != NULL);

      fprintf (out_fp, ", Recv_index = %s,\n", rv_rcvindex_string (sysop_end->undo.data.rcvindex));
      fprintf (out_fp, "     Volid = %d Pageid = %d Offset = %d,\n     Undo (Before) length = %d,\n",
	       sysop_end->undo.data.volid, sysop_end->undo.data.pageid, sysop_end->undo.data.offset,
	       (int) GET_ZIP_LEN (sysop_end->undo.length));

      undo_length = sysop_end->undo.length;
      rcvindex = sysop_end->undo.data.rcvindex;
      LOG_READ_ADD_ALIGN (thread_p, sizeof (*sysop_end), log_lsa, log_page_p);
      log_dump_data (thread_p, out_fp, undo_length, log_lsa, log_page_p, RV_fun[rcvindex].dump_undofun, log_zip_p);
      break;
    case LOG_SYSOP_END_LOGICAL_MVCC_UNDO:
      assert (log_lsa != NULL && log_page_p != NULL && log_zip_p != NULL);

      fprintf (out_fp, ", Recv_index = %s,\n", rv_rcvindex_string (sysop_end->mvcc_undo.undo.data.rcvindex));
      fprintf (out_fp, "     Volid = %d Pageid = %d Offset = %d,\n     Undo (Before) length = %d,\n",
	       sysop_end->mvcc_undo.undo.data.volid, sysop_end->mvcc_undo.undo.data.pageid,
	       sysop_end->mvcc_undo.undo.data.offset, (int) GET_ZIP_LEN (sysop_end->mvcc_undo.undo.length));
      fprintf (out_fp, "     MVCCID = %llu, \n     Prev_mvcc_op_log_lsa = %lld|%d, \n     VFID = (%d, %d)",
	       (unsigned long long int) sysop_end->mvcc_undo.mvccid,
	       LSA_AS_ARGS (&sysop_end->mvcc_undo.vacuum_info.prev_mvcc_op_log_lsa),
	       VFID_AS_ARGS (&sysop_end->mvcc_undo.vacuum_info.vfid));

      undo_length = sysop_end->mvcc_undo.undo.length;
      rcvindex = sysop_end->mvcc_undo.undo.data.rcvindex;
      LOG_READ_ADD_ALIGN (thread_p, sizeof (*sysop_end), log_lsa, log_page_p);
      log_dump_data (thread_p, out_fp, undo_length, log_lsa, log_page_p, RV_fun[rcvindex].dump_undofun, log_zip_p);
      break;
    default:
      assert (false);
      break;
    }
  return log_page_p;
}

/*
 * log_dump_record_sysop_end () - Dump sysop end log record types. Side-effect: log_lsa and log_page_p will be
 *				  positioned after the log record.
 *
 * return	       : NULL if something bad happens, pointer to log page otherwise.
 * thread_p (in)       : Thread entry.
 * log_lsa (in/out)    : in - LSA of log record, out - LSA after log record.
 * log_page_p (in/out) : in - page of log record, out - page after log record.
 * log_zip_p (in)      : Unzip context.
 * out_fp (in/out)     : Dump output.
 */
static LOG_PAGE *
log_dump_record_sysop_end (THREAD_ENTRY * thread_p, LOG_LSA * log_lsa, LOG_PAGE * log_page_p, LOG_ZIP * log_zip_p,
			   FILE * out_fp)
{
  LOG_REC_SYSOP_END *sysop_end;

  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*sysop_end), log_lsa, log_page_p);
  sysop_end = (LOG_REC_SYSOP_END *) ((char *) log_page_p->area + log_lsa->offset);

  log_dump_record_sysop_end_internal (thread_p, sysop_end, log_lsa, log_page_p, log_zip_p, out_fp);

  return log_page_p;
}

/*
 * log_dump_checkpoint_topops - DUMP CHECKPOINT OF TOP SYSTEM OPERATIONS
 *
 * return: nothing
 *
 *   length(in): Length to dump in bytes
 *   data(in): The data being logged
 *
 * NOTE: Dump the checkpoint top system operation structure.
 */
static void
log_dump_checkpoint_topops (FILE * out_fp, int length, void *data)
{
  int ntops, i;
  LOG_INFO_CHKPT_SYSOP *chkpt_topops;	/* Checkpoint top system operations that are in commit postpone
					 * mode */
  LOG_INFO_CHKPT_SYSOP *chkpt_topone;	/* One top system ope */

  chkpt_topops = (LOG_INFO_CHKPT_SYSOP *) data;
  ntops = length / sizeof (*chkpt_topops);

  /* Start dumping each checkpoint top system operation */

  for (i = 0; i < ntops; i++)
    {
      chkpt_topone = &chkpt_topops[i];
      fprintf (out_fp, "     Trid = %d \n", chkpt_topone->trid);
      fprintf (out_fp, "     Sysop start postpone LSA = %lld|%d \n",
	       LSA_AS_ARGS (&chkpt_topone->sysop_start_postpone_lsa));
    }
  (void) fprintf (out_fp, "\n");
}

static LOG_PAGE *
log_dump_record_checkpoint (THREAD_ENTRY * thread_p, FILE * out_fp, LOG_LSA * log_lsa, LOG_PAGE * log_page_p)
{
  LOG_REC_CHKPT *chkpt;		/* check point log record */
  int length_active_tran;
  int length_topope;

  /* Read the DATA HEADER */
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*chkpt), log_lsa, log_page_p);

  chkpt = (LOG_REC_CHKPT *) ((char *) log_page_p->area + log_lsa->offset);
  fprintf (out_fp, ", Num_trans = %d,\n", chkpt->ntrans);
  fprintf (out_fp, "     Redo_LSA = %lld|%d\n", LSA_AS_ARGS (&chkpt->redo_lsa));

  length_active_tran = sizeof (LOG_INFO_CHKPT_TRANS) * chkpt->ntrans;
  length_topope = (sizeof (LOG_INFO_CHKPT_SYSOP) * chkpt->ntops);
  LOG_READ_ADD_ALIGN (thread_p, sizeof (*chkpt), log_lsa, log_page_p);
  log_dump_data (thread_p, out_fp, length_active_tran, log_lsa, log_page_p, logpb_dump_checkpoint_trans, NULL);
  if (length_topope > 0)
    {
      log_dump_data (thread_p, out_fp, length_active_tran, log_lsa, log_page_p, log_dump_checkpoint_topops, NULL);
    }

  return log_page_p;
}

static LOG_PAGE *
log_dump_record_save_point (THREAD_ENTRY * thread_p, FILE * out_fp, LOG_LSA * log_lsa, LOG_PAGE * log_page_p)
{
  LOG_REC_SAVEPT *savept;
  int length_save_point;

  /* Read the DATA HEADER */
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*savept), log_lsa, log_page_p);
  savept = (LOG_REC_SAVEPT *) ((char *) log_page_p->area + log_lsa->offset);

  fprintf (out_fp, ", Prev_savept_Lsa = %lld|%d, length = %d,\n", LSA_AS_ARGS (&savept->prv_savept), savept->length);

  length_save_point = savept->length;
  LOG_READ_ADD_ALIGN (thread_p, sizeof (*savept), log_lsa, log_page_p);

  /* Print savept name */
  fprintf (out_fp, "     Savept Name =");
  log_dump_data (thread_p, out_fp, length_save_point, log_lsa, log_page_p, log_hexa_dump, NULL);

  return log_page_p;
}

static LOG_PAGE *
log_dump_record_2pc_prepare_commit (THREAD_ENTRY * thread_p, FILE * out_fp, LOG_LSA * log_lsa, LOG_PAGE * log_page_p)
{
  LOG_REC_2PC_PREPCOMMIT *prepared;
  unsigned int nobj_locks;
  int size;

  /* Get the DATA HEADER */
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*prepared), log_lsa, log_page_p);
  prepared = (LOG_REC_2PC_PREPCOMMIT *) ((char *) log_page_p->area + log_lsa->offset);

  fprintf (out_fp, ", Client_name = %s, Gtrid = %d, Num objlocks = %u\n", prepared->user_name, prepared->gtrid,
	   prepared->num_object_locks);

  nobj_locks = prepared->num_object_locks;

  LOG_READ_ADD_ALIGN (thread_p, sizeof (*prepared), log_lsa, log_page_p);

  /* Dump global transaction user information */
  if (prepared->gtrinfo_length > 0)
    {
      log_dump_data (thread_p, out_fp, prepared->gtrinfo_length, log_lsa, log_page_p, log_2pc_dump_gtrinfo, NULL);
    }

  /* Dump object locks */
  if (nobj_locks > 0)
    {
      size = nobj_locks * sizeof (LK_ACQOBJ_LOCK);
      log_dump_data (thread_p, out_fp, size, log_lsa, log_page_p, log_2pc_dump_acqobj_locks, NULL);
    }

  return log_page_p;
}

static LOG_PAGE *
log_dump_record_2pc_start (THREAD_ENTRY * thread_p, FILE * out_fp, LOG_LSA * log_lsa, LOG_PAGE * log_page_p)
{
  LOG_REC_2PC_START *start_2pc;	/* Start log record of 2PC protocol */

  /* Get the DATA HEADER */
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*start_2pc), log_lsa, log_page_p);
  start_2pc = (LOG_REC_2PC_START *) ((char *) log_page_p->area + log_lsa->offset);

  /* Initilize the coordinator information */
  fprintf (out_fp, "  Client_name = %s, Gtrid = %d,  Num_participants = %d", start_2pc->user_name, start_2pc->gtrid,
	   start_2pc->num_particps);

  LOG_READ_ADD_ALIGN (thread_p, sizeof (*start_2pc), log_lsa, log_page_p);
  /* Read in the participants info. block from the log */
  log_dump_data (thread_p, out_fp, (start_2pc->particp_id_length * start_2pc->num_particps), log_lsa, log_page_p,
		 log_2pc_dump_participants, NULL);

  return log_page_p;
}

static LOG_PAGE *
log_dump_record_2pc_acknowledgement (THREAD_ENTRY * thread_p, FILE * out_fp, LOG_LSA * log_lsa, LOG_PAGE * log_page_p)
{
  LOG_REC_2PC_PARTICP_ACK *received_ack;	/* ack log record of 2pc protocol */

  /* Get the DATA HEADER */
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*received_ack), log_lsa, log_page_p);
  received_ack = ((LOG_REC_2PC_PARTICP_ACK *) ((char *) log_page_p->area + log_lsa->offset));
  fprintf (out_fp, "  Participant index = %d\n", received_ack->particp_index);

  return log_page_p;
}

static LOG_PAGE *
log_dump_record_ha_server_state (THREAD_ENTRY * thread_p, FILE * out_fp, LOG_LSA * log_lsa, LOG_PAGE * log_page_p)
{
  LOG_REC_HA_SERVER_STATE *ha_server_state;

  /* Get the DATA HEADER */
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*ha_server_state), log_lsa, log_page_p);
  ha_server_state = ((LOG_REC_HA_SERVER_STATE *) ((char *) log_page_p->area + log_lsa->offset));
  fprintf (out_fp, "  HA server state = %d\n", ha_server_state->state);

  return log_page_p;
}

static LOG_PAGE *
log_dump_record_supplemental_info (THREAD_ENTRY * thread_p, FILE * out_fp, LOG_LSA * log_lsa, LOG_PAGE * log_page_p)
{
  LOG_REC_SUPPLEMENT *supplement;

  /* Get the DATA HEADER */
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*supplement), log_lsa, log_page_p);
  supplement = ((LOG_REC_SUPPLEMENT *) ((char *) log_page_p->area + log_lsa->offset));
  fprintf (out_fp, "\tSUPPLEMENT TYPE = %d\n", supplement->rec_type);
  fprintf (out_fp, "\tSUPPLEMENT LENGTH = %d\n", supplement->length);

  return log_page_p;
}

static LOG_PAGE *
log_dump_record (THREAD_ENTRY * thread_p, FILE * out_fp, LOG_RECTYPE record_type, LOG_LSA * log_lsa,
		 LOG_PAGE * log_page_p, LOG_ZIP * log_zip_p)
{
  switch (record_type)
    {
    case LOG_UNDOREDO_DATA:
    case LOG_DIFF_UNDOREDO_DATA:
      log_page_p = log_dump_record_undoredo (thread_p, out_fp, log_lsa, log_page_p, log_zip_p);
      break;

    case LOG_UNDO_DATA:
      log_page_p = log_dump_record_undo (thread_p, out_fp, log_lsa, log_page_p, log_zip_p);
      break;

    case LOG_REDO_DATA:
    case LOG_POSTPONE:
      log_page_p = log_dump_record_redo (thread_p, out_fp, log_lsa, log_page_p, log_zip_p);
      break;

    case LOG_MVCC_UNDOREDO_DATA:
    case LOG_MVCC_DIFF_UNDOREDO_DATA:
      log_page_p = log_dump_record_mvcc_undoredo (thread_p, out_fp, log_lsa, log_page_p, log_zip_p);
      break;

    case LOG_MVCC_UNDO_DATA:
      log_page_p = log_dump_record_mvcc_undo (thread_p, out_fp, log_lsa, log_page_p, log_zip_p);
      break;

    case LOG_MVCC_REDO_DATA:
      log_page_p = log_dump_record_mvcc_redo (thread_p, out_fp, log_lsa, log_page_p, log_zip_p);
      break;

    case LOG_RUN_POSTPONE:
      log_page_p = log_dump_record_postpone (thread_p, out_fp, log_lsa, log_page_p);
      break;

    case LOG_DBEXTERN_REDO_DATA:
      log_page_p = log_dump_record_dbout_redo (thread_p, out_fp, log_lsa, log_page_p);
      break;

    case LOG_COMPENSATE:
      log_page_p = log_dump_record_compensate (thread_p, out_fp, log_lsa, log_page_p);
      break;

    case LOG_COMMIT_WITH_POSTPONE:
      log_page_p = log_dump_record_commit_postpone (thread_p, out_fp, log_lsa, log_page_p);
      break;

    case LOG_COMMIT_WITH_POSTPONE_OBSOLETE:
      log_page_p = log_dump_record_commit_postpone_obsolete (thread_p, out_fp, log_lsa, log_page_p);
      break;

    case LOG_COMMIT:
    case LOG_ABORT:
      log_page_p = log_dump_record_transaction_finish (thread_p, out_fp, log_lsa, log_page_p);
      break;

    case LOG_REPLICATION_DATA:
    case LOG_REPLICATION_STATEMENT:
      log_page_p = log_dump_record_replication (thread_p, out_fp, log_lsa, log_page_p);
      break;

    case LOG_SYSOP_START_POSTPONE:
      log_page_p = log_dump_record_sysop_start_postpone (thread_p, out_fp, log_lsa, log_page_p, log_zip_p);
      break;

    case LOG_SYSOP_END:
      log_page_p = log_dump_record_sysop_end (thread_p, log_lsa, log_page_p, log_zip_p, out_fp);
      break;

    case LOG_END_CHKPT:
      log_page_p = log_dump_record_checkpoint (thread_p, out_fp, log_lsa, log_page_p);
      break;

    case LOG_SAVEPOINT:
      log_page_p = log_dump_record_save_point (thread_p, out_fp, log_lsa, log_page_p);
      break;

    case LOG_2PC_PREPARE:
      log_page_p = log_dump_record_2pc_prepare_commit (thread_p, out_fp, log_lsa, log_page_p);
      break;

    case LOG_2PC_START:
      log_page_p = log_dump_record_2pc_start (thread_p, out_fp, log_lsa, log_page_p);
      break;

    case LOG_2PC_RECV_ACK:
      log_page_p = log_dump_record_2pc_acknowledgement (thread_p, out_fp, log_lsa, log_page_p);
      break;

    case LOG_DUMMY_HA_SERVER_STATE:
      log_page_p = log_dump_record_ha_server_state (thread_p, out_fp, log_lsa, log_page_p);
      break;

    case LOG_SUPPLEMENTAL_INFO:
      log_page_p = log_dump_record_supplemental_info (thread_p, out_fp, log_lsa, log_page_p);
      break;

    case LOG_START_CHKPT:
    case LOG_2PC_COMMIT_DECISION:
    case LOG_2PC_ABORT_DECISION:
    case LOG_2PC_COMMIT_INFORM_PARTICPS:
    case LOG_2PC_ABORT_INFORM_PARTICPS:
    case LOG_SYSOP_ATOMIC_START:
    case LOG_DUMMY_HEAD_POSTPONE:
    case LOG_DUMMY_CRASH_RECOVERY:
    case LOG_DUMMY_OVF_RECORD:
    case LOG_DUMMY_GENERIC:
      fprintf (out_fp, "\n");
      /* That is all for this kind of log record */
      break;

    case LOG_END_OF_LOG:
      if (!logpb_is_page_in_archive (log_lsa->pageid))
	{
	  fprintf (out_fp, "\n... xxx END OF LOG xxx ...\n");
	}
      break;

    case LOG_SMALLER_LOGREC_TYPE:
    case LOG_LARGER_LOGREC_TYPE:
    default:
      fprintf (out_fp, "log_dump: Unknown record type = %d (%s).\n", record_type, log_to_string (record_type));
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
xlog_dump (THREAD_ENTRY * thread_p, FILE * out_fp, int isforward, LOG_PAGEID start_logpageid, DKNPAGES dump_npages,
	   TRANID desired_tranid)
{
  LOG_LSA lsa;			/* LSA of log record to dump */
  char log_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT], *aligned_log_pgbuf;
  LOG_PAGE *log_pgptr = NULL;	/* Log page pointer where LSA is located */
  LOG_LSA log_lsa;
  LOG_RECTYPE type;		/* Log record type */
  LOG_RECORD_HEADER *log_rec;	/* Pointer to log record */

  LOG_ZIP *log_dump_ptr = NULL;

  aligned_log_pgbuf = PTR_ALIGN (log_pgbuf, MAX_ALIGNMENT);

  if (out_fp == NULL)
    {
      out_fp = stdout;
    }

  fprintf (out_fp, "**************** DUMP LOGGING INFORMATION ************\n");
  /* Dump the transaction table and the log buffers */

  /* Flush any dirty log page */
  LOG_CS_ENTER (thread_p);

  xlogtb_dump_trantable (thread_p, out_fp);
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
      else if (lsa.pageid > log_Gl.hdr.append_lsa.pageid && LOG_ISRESTARTED ())
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
	   " Num_pages_to_dump = %d, desired_tranid = %d\n", (isforward ? "Forward" : "Backaward"),
	   (long long int) start_logpageid, dump_npages, desired_tranid);

  LOG_CS_EXIT (thread_p);

  log_pgptr = (LOG_PAGE *) aligned_log_pgbuf;

  if (log_dump_ptr == NULL)
    {
      log_dump_ptr = log_zip_alloc (IO_PAGESIZE);
      if (log_dump_ptr == NULL)
	{
	  fprintf (out_fp, " Error memory alloc... Quit\n");
	  return;
	}
    }

  /* Start dumping all log records following the given direction */
  while (!LSA_ISNULL (&lsa) && dump_npages-- > 0)
    {
      if ((logpb_fetch_page (thread_p, &lsa, LOG_CS_SAFE_READER, log_pgptr)) != NO_ERROR)
	{
	  fprintf (out_fp, " Error reading page %lld... Quit\n", (long long int) lsa.pageid);
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
      if (lsa.offset == NULL_OFFSET && (lsa.offset = log_pgptr->hdr.offset) == NULL_OFFSET)
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
		|| (!LSA_EQ (&next_lsa, &log_rec->forw_lsa) && !LSA_ISNULL (&log_rec->forw_lsa)))
	      {
		fprintf (out_fp, "\n\n>>>>>****\n");
		fprintf (out_fp, "Guess next address = %lld|%d for LSA = %lld|%d\n",
			 LSA_AS_ARGS (&next_lsa), LSA_AS_ARGS (&lsa));
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
		      fprintf (out_fp, "log_dump: Problems finding next record. BYE\n");
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
	      if (LSA_ISNULL (&lsa) && logpb_is_page_in_archive (log_lsa.pageid))
		{
		  lsa.pageid = log_lsa.pageid + 1;
		}
	    }
	  else
	    {
	      LSA_COPY (&lsa, &log_rec->back_lsa);
	    }

	  if (desired_tranid != NULL_TRANID && desired_tranid != log_rec->trid && log_rec->type != LOG_END_OF_LOG)
	    {
	      /* Don't dump this log record... */
	      continue;
	    }

	  fprintf (out_fp,
		   "\nLSA = %3lld|%3d, Forw log = %3lld|%3d, Backw log = %3lld|%3d,\n"
		   "     Trid = %3d, Prev tran logrec = %3lld|%3d\n     Type = %s", (long long int) log_lsa.pageid,
		   (int) log_lsa.offset, (long long int) log_rec->forw_lsa.pageid, (int) log_rec->forw_lsa.offset,
		   (long long int) log_rec->back_lsa.pageid, (int) log_rec->back_lsa.offset, log_rec->trid,
		   (long long int) log_rec->prev_tranlsa.pageid, (int) log_rec->prev_tranlsa.offset,
		   log_to_string (type));

	  if (LSA_ISNULL (&log_rec->forw_lsa) && type != LOG_END_OF_LOG)
	    {
	      /* Incomplete log record... quit */
	      fprintf (out_fp, "\n****\n");
	      fprintf (out_fp, "log_dump: Incomplete log_record.. Quit\n");
	      fprintf (out_fp, "\n****\n");
	      continue;
	    }

	  /* Advance the pointer to dump the type of log record */

	  LOG_READ_ADD_ALIGN (thread_p, sizeof (*log_rec), &log_lsa, log_pgptr);
	  log_pgptr = log_dump_record (thread_p, out_fp, type, &log_lsa, log_pgptr, log_dump_ptr);
	  fflush (out_fp);
	  /*
	   * We can fix the lsa.pageid in the case of log_records without forward
	   * address at this moment.
	   */
	  if (lsa.offset == NULL_OFFSET && lsa.pageid != NULL_PAGEID && lsa.pageid < log_lsa.pageid)
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
  fprintf (out_fp, "******************************************************\n");
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
log_rollback_record (THREAD_ENTRY * thread_p, LOG_LSA * log_lsa, LOG_PAGE * log_page_p, LOG_RCVINDEX rcvindex,
		     VPID * rcv_vpid, LOG_RCV * rcv, LOG_TDES * tdes, LOG_ZIP * log_unzip_ptr)
{
  char *area = NULL;
  TRAN_STATE save_state;	/* The current state of the transaction. Must be returned to this state */
  int rv_err;
  bool is_zipped = false;

  /*
   * Fetch the page for physical log records. If the page does not exist
   * anymore or there are problems fetching the page, continue anyhow, so that
   * compensating records are logged.
   */

  assert (rcvindex != RV_NOT_DEFINED);
  assert (RV_fun[rcvindex].undofun != NULL);

  if (RCV_IS_LOGICAL_LOG (rcv_vpid, rcvindex))
    {
      rcv->pgptr = NULL;
    }
  else
    {
      rcv->pgptr = pgbuf_fix (thread_p, rcv_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (rcv->pgptr == NULL)
	{
	  assert (false);
	}
    }

  /* GET BEFORE DATA */

  /*
   * If data is contained in only one buffer, pass pointer directly.
   * Otherwise, allocate a contiguous area, copy the data and pass this area.
   * At the end deallocate the area.
   */

  if (ZIP_CHECK (rcv->length))
    {				/* check compress data */
      rcv->length = (int) GET_ZIP_LEN (rcv->length);	/* MSB set 0 */
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
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_rollback_record");
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
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_rollback_record");
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
       * redo/CLR log to describe the undo. This in turn will be translated
       * to a compensating record.
       */
      if (rcvindex == RVBT_MVCC_INCREMENTS_UPD)
	{
	  /* this is a special case. we need to undo changes to transaction local stats. this only has an impact on
	   * this transaction during runtime, and recovery has no interest, so we don't have to add a compensate log
	   * record. */
	  rv_err = (*RV_fun[rcvindex].undofun) (thread_p, rcv);
	  assert (rv_err == NO_ERROR);
	}
      else if (rcvindex == RVBT_MVCC_NOTIFY_VACUUM || rcvindex == RVES_NOTIFY_VACUUM)
	{
	  /* do nothing */
	}
      else if (rcvindex == RVBT_LOG_GLOBAL_UNIQUE_STATS_COMMIT)
	{
	  /* impossible. we cannot rollback anymore. */
	  assert_release (false);
	  rv_err = ER_FAILED;
	}
      else if (RCV_IS_LOGICAL_COMPENSATE_MANUAL (rcvindex))
	{
	  /* B-tree logical logs will add a regular compensate in the modified pages. They do not require a logical
	   * compensation since the "undone" page can be accessed and logged. Only no-page logical operations require
	   * logical compensation. */
	  /* Invoke Undo recovery function */
	  LSA_COPY (&rcv->reference_lsa, &tdes->undo_nxlsa);
	  rv_err = log_undo_rec_restartable (thread_p, rcvindex, rcv);
	  if (rv_err != NO_ERROR)
	    {
	      er_log_debug (ARG_FILE_LINE,
			    "log_rollback_record: SYSTEM ERROR... Transaction %d, "
			    "Log record %lld|%d, rcvindex = %s, was not undone due to error (%d)\n",
			    tdes->tran_index, (long long int) log_lsa->pageid, log_lsa->offset,
			    rv_rcvindex_string (rcvindex), rv_err);
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_MAYNEED_MEDIA_RECOVERY, 1,
		      fileio_get_volume_label (rcv_vpid->volid, PEEK));
	      assert (false);
	    }
	  else if (RCV_IS_BTREE_LOGICAL_LOG (rcvindex) && prm_get_bool_value (PRM_ID_LOG_BTREE_OPS))
	    {
	      _er_log_debug (ARG_FILE_LINE,
			     "BTREE_ROLLBACK: Successfully executed undo/compensate for log entry before "
			     "lsa=%lld|%d, undo_nxlsa=%lld|%d. Transaction=%d, rcvindex=%d.\n",
			     (long long int) log_lsa->pageid, (int) log_lsa->offset,
			     (long long int) tdes->undo_nxlsa.pageid, (int) tdes->undo_nxlsa.offset, tdes->tran_index,
			     rcvindex);
	    }
	}
      else if (!RCV_IS_LOGICAL_LOG (rcv_vpid, rcvindex))
	{
	  log_append_compensate (thread_p, rcvindex, rcv_vpid, rcv->offset, rcv->pgptr, rcv->length, rcv->data, tdes);
	  /* Invoke Undo recovery function */
	  rv_err = log_undo_rec_restartable (thread_p, rcvindex, rcv);
	  if (rv_err != NO_ERROR)
	    {
	      er_log_debug (ARG_FILE_LINE,
			    "log_rollback_record: SYSTEM ERROR... Transaction %d, "
			    "Log record %lld|%d, rcvindex = %s, was not undone due to error (%d)\n",
			    tdes->tran_index, (long long int) log_lsa->pageid, log_lsa->offset,
			    rv_rcvindex_string (rcvindex), rv_err);
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_MAYNEED_MEDIA_RECOVERY, 1,
		      fileio_get_volume_label (rcv_vpid->volid, PEEK));
	      assert (false);
	    }
	}
      else
	{
	  /* Logical logging? This is a logical undo. For now, we also use a logical compensation, meaning that we
	   * open a system operation that is committed & compensate at the same time.
	   * However, there might be cases when compensation is not necessarily logical. If the compensation can be
	   * made in a single log record and can be attached to a page, the system operation becomes useless. Take the
	   * example of some b-tree cases for compensations. There might be other cases too.
	   */
	  save_state = tdes->state;

	  LSA_COPY (&rcv->reference_lsa, &tdes->undo_nxlsa);

	  log_sysop_start (thread_p);

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
	    if (LSA_EQ (&check_tail_lsa, &tdes->tail_lsa))
	      {
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_MISSING_COMPENSATING_RECORD, 1,
			rv_rcvindex_string (rcvindex));
	      }
	  }
#else /* CUBRID_DEBUG */
	  /* Invoke Undo recovery function */
	  /* TODO: Is undo restartable needed? */
	  rv_err = log_undo_rec_restartable (thread_p, rcvindex, rcv);
#endif /* CUBRID_DEBUG */

	  if (rv_err != NO_ERROR)
	    {
	      er_log_debug (ARG_FILE_LINE,
			    "log_rollback_record: SYSTEM ERROR... Transaction %d, "
			    "Log record %lld|%d, rcvindex = %s, was not undone due to error (%d)\n",
			    tdes->tran_index, (long long int) log_lsa->pageid, log_lsa->offset,
			    rv_rcvindex_string (rcvindex), rv_err);
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_MAYNEED_MEDIA_RECOVERY, 1,
		      fileio_get_volume_label (rcv_vpid->volid, PEEK));
	      assert (false);
	    }

	  log_sysop_end_logical_compensate (thread_p, &rcv->reference_lsa);
	  tdes->state = save_state;
	}
    }
  else
    {
      /*
       * Unable to fetch page of volume... May need media recovery on such
       * page... write a CLR anyhow
       */
      log_append_compensate (thread_p, rcvindex, rcv_vpid, rcv->offset, NULL, rcv->length, rcv->data, tdes);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_MAYNEED_MEDIA_RECOVERY, 1,
	      fileio_get_volume_label (rcv_vpid->volid, PEEK));
      assert (false);
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
log_undo_rec_restartable (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex, LOG_RCV * rcv)
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
			LOG_FIND_THREAD_TRAN_INDEX (thread_p), num_retries, error_code, rv_rcvindex_string (rcvindex));
#endif /* CUBRID_DEBUG */
	}
      error_code = (*RV_fun[rcvindex].undofun) (thread_p, rcv);
    }
  while (++num_retries <= LOG_REC_UNDO_MAX_ATTEMPTS
	 && (error_code == ER_LK_PAGE_TIMEOUT || error_code == ER_LK_UNILATERALLY_ABORTED));

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
log_dump_record_header_to_string (LOG_RECORD_HEADER * log, char *buf, size_t len)
{
  const char *fmt = "TYPE[%d], TRID[%d], PREV[%lld,%d], BACK[%lld,%d], FORW[%lld,%d]";

  snprintf (buf, len, fmt, log->type, log->trid, (long long int) log->prev_tranlsa.pageid, log->prev_tranlsa.offset,
	    (long long int) log->back_lsa.pageid, log->back_lsa.offset, (long long int) log->forw_lsa.pageid,
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
log_rollback (THREAD_ENTRY * thread_p, LOG_TDES * tdes, const LOG_LSA * upto_lsa_ptr)
{
  LOG_LSA prev_tranlsa;		/* Previous LSA */
  LOG_LSA upto_lsa;		/* copy of upto_lsa_ptr contents */
  char log_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT], *aligned_log_pgbuf;
  LOG_PAGE *log_pgptr = NULL;	/* Log page pointer of LSA log record */
  LOG_LSA log_lsa;
  LOG_RECORD_HEADER *log_rec = NULL;	/* The log record */
  LOG_REC_UNDOREDO *undoredo = NULL;	/* An undoredo log record */
  LOG_REC_MVCC_UNDOREDO *mvcc_undoredo = NULL;	/* A MVCC undoredo log rec */
  LOG_REC_UNDO *undo = NULL;	/* An undo log record */
  LOG_REC_MVCC_UNDO *mvcc_undo = NULL;	/* An undo log record */
  LOG_REC_COMPENSATE *compensate = NULL;	/* A compensating log record */
  LOG_REC_SYSOP_END *sysop_end = NULL;	/* Partial result from top system operation */
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
  old_wait_msecs = xlogtb_reset_wait_msecs (thread_p, TRAN_LOCK_INFINITE_WAIT);

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

  log_unzip_ptr = log_zip_alloc (IO_PAGESIZE);

  if (log_unzip_ptr == NULL)
    {
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_rollback");
      return;
    }

  while (!LSA_ISNULL (&prev_tranlsa) && !isdone)
    {
      /* Fetch the page where the LSA record to undo is located */
      LSA_COPY (&log_lsa, &prev_tranlsa);
      log_lsa.offset = LOG_PAGESIZE;

      if ((logpb_fetch_page (thread_p, &log_lsa, LOG_CS_FORCE_USE, log_pgptr)) != NO_ERROR)
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
	      if (log_rec->type == LOG_MVCC_UNDOREDO_DATA || log_rec->type == LOG_MVCC_DIFF_UNDOREDO_DATA)
		{
		  is_mvcc_op = true;
		}
	      else
		{
		  is_mvcc_op = false;
		}

	      /* Read the DATA HEADER */
	      LOG_READ_ADD_ALIGN (thread_p, sizeof (*log_rec), &log_lsa, log_pgptr);
	      if (is_mvcc_op)
		{
		  /* Data header is MVCC undoredo */
		  data_header_size = sizeof (*mvcc_undoredo);
		  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, data_header_size, &log_lsa, log_pgptr);
		  mvcc_undoredo = (LOG_REC_MVCC_UNDOREDO *) ((char *) log_pgptr->area + log_lsa.offset);

		  /* Get undoredo info */
		  undoredo = &mvcc_undoredo->undoredo;

		  /* Save transaction MVCCID for recovery */
		  rcv.mvcc_id = mvcc_undoredo->mvccid;
		}
	      else
		{
		  data_header_size = sizeof (*undoredo);
		  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, data_header_size, &log_lsa, log_pgptr);
		  undoredo = (LOG_REC_UNDOREDO *) ((char *) log_pgptr->area + log_lsa.offset);

		  rcv.mvcc_id = MVCCID_NULL;
		}

	      rcvindex = undoredo->data.rcvindex;
	      rcv.length = undoredo->ulength;
	      rcv.offset = undoredo->data.offset;
	      rcv_vpid.volid = undoredo->data.volid;
	      rcv_vpid.pageid = undoredo->data.pageid;

	      LOG_READ_ADD_ALIGN (thread_p, data_header_size, &log_lsa, log_pgptr);

	      log_rollback_record (thread_p, &log_lsa, log_pgptr, rcvindex, &rcv_vpid, &rcv, tdes, log_unzip_ptr);
	      break;

	    case LOG_MVCC_UNDO_DATA:
	    case LOG_UNDO_DATA:
	      /* Does record belong to a MVCC op? */
	      is_mvcc_op = (log_rec->type == LOG_MVCC_UNDO_DATA);

	      /* Read the DATA HEADER */
	      LOG_READ_ADD_ALIGN (thread_p, sizeof (*log_rec), &log_lsa, log_pgptr);
	      if (is_mvcc_op)
		{
		  /* Data header is MVCC undo */
		  data_header_size = sizeof (*mvcc_undo);
		  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, data_header_size, &log_lsa, log_pgptr);
		  mvcc_undo = (LOG_REC_MVCC_UNDO *) ((char *) log_pgptr->area + log_lsa.offset);
		  /* Get undo info */
		  undo = &mvcc_undo->undo;

		  /* Save transaction MVCCID for recovery */
		  rcv.mvcc_id = mvcc_undo->mvccid;
		}
	      else
		{
		  data_header_size = sizeof (*undo);
		  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, data_header_size, &log_lsa, log_pgptr);
		  undo = (LOG_REC_UNDO *) ((char *) log_pgptr->area + log_lsa.offset);

		  rcv.mvcc_id = MVCCID_NULL;
		}
	      rcvindex = undo->data.rcvindex;
	      rcv.offset = undo->data.offset;
	      rcv_vpid.volid = undo->data.volid;
	      rcv_vpid.pageid = undo->data.pageid;
	      rcv.length = undo->length;

	      LOG_READ_ADD_ALIGN (thread_p, data_header_size, &log_lsa, log_pgptr);

	      log_rollback_record (thread_p, &log_lsa, log_pgptr, rcvindex, &rcv_vpid, &rcv, tdes, log_unzip_ptr);
	      break;

	    case LOG_COMPENSATE:
	      /*
	       * We found a partial rollback, use the CLR to find the next record
	       * to undo
	       */

	      /* Read the DATA HEADER */
	      LOG_READ_ADD_ALIGN (thread_p, sizeof (*log_rec), &log_lsa, log_pgptr);
	      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*compensate), &log_lsa, log_pgptr);
	      compensate = (LOG_REC_COMPENSATE *) ((char *) log_pgptr->area + log_lsa.offset);
	      LSA_COPY (&prev_tranlsa, &compensate->undo_nxlsa);
	      break;

	    case LOG_SYSOP_END:
	      /*
	       * We found a system top operation that should be skipped from rollback.
	       */

	      /* Read the DATA HEADER */
	      LOG_READ_ADD_ALIGN (thread_p, sizeof (*log_rec), &log_lsa, log_pgptr);
	      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*sysop_end), &log_lsa, log_pgptr);
	      sysop_end = ((LOG_REC_SYSOP_END *) ((char *) log_pgptr->area + log_lsa.offset));

	      if (sysop_end->type == LOG_SYSOP_END_LOGICAL_UNDO)
		{
		  rcvindex = sysop_end->undo.data.rcvindex;
		  rcv.offset = sysop_end->undo.data.offset;
		  rcv_vpid.volid = sysop_end->undo.data.volid;
		  rcv_vpid.pageid = sysop_end->undo.data.pageid;
		  rcv.length = sysop_end->undo.length;
		  rcv.mvcc_id = MVCCID_NULL;

		  /* will jump to parent LSA. save it now before advancing to undo data */
		  LSA_COPY (&prev_tranlsa, &sysop_end->lastparent_lsa);
		  LSA_COPY (&tdes->undo_nxlsa, &sysop_end->lastparent_lsa);

		  LOG_READ_ADD_ALIGN (thread_p, sizeof (*sysop_end), &log_lsa, log_pgptr);
		  log_rollback_record (thread_p, &log_lsa, log_pgptr, rcvindex, &rcv_vpid, &rcv, tdes, log_unzip_ptr);
		}
	      else if (sysop_end->type == LOG_SYSOP_END_LOGICAL_MVCC_UNDO)
		{
		  rcvindex = sysop_end->mvcc_undo.undo.data.rcvindex;
		  rcv.offset = sysop_end->mvcc_undo.undo.data.offset;
		  rcv_vpid.volid = sysop_end->mvcc_undo.undo.data.volid;
		  rcv_vpid.pageid = sysop_end->mvcc_undo.undo.data.pageid;
		  rcv.length = sysop_end->mvcc_undo.undo.length;
		  rcv.mvcc_id = sysop_end->mvcc_undo.mvccid;

		  /* will jump to parent LSA. save it now before advancing to undo data */
		  LSA_COPY (&prev_tranlsa, &sysop_end->lastparent_lsa);
		  LSA_COPY (&tdes->undo_nxlsa, &sysop_end->lastparent_lsa);

		  LOG_READ_ADD_ALIGN (thread_p, sizeof (*sysop_end), &log_lsa, log_pgptr);
		  log_rollback_record (thread_p, &log_lsa, log_pgptr, rcvindex, &rcv_vpid, &rcv, tdes, log_unzip_ptr);
		}
	      else if (sysop_end->type == LOG_SYSOP_END_LOGICAL_COMPENSATE)
		{
		  /* compensate */
		  LSA_COPY (&prev_tranlsa, &sysop_end->compensate_lsa);
		}
	      else if (sysop_end->type == LOG_SYSOP_END_LOGICAL_RUN_POSTPONE)
		{
		  /* this must be partial rollback during recovery of another logical run postpone */
		  assert (!LOG_ISRESTARTED ());
		  /* we have to stop */
		  LSA_SET_NULL (&prev_tranlsa);
		}
	      else
		{
		  /* jump to last parent */
		  LSA_COPY (&prev_tranlsa, &sysop_end->lastparent_lsa);
		}
	      break;

	    case LOG_REDO_DATA:
	    case LOG_MVCC_REDO_DATA:
	    case LOG_DBEXTERN_REDO_DATA:
	    case LOG_DUMMY_HEAD_POSTPONE:
	    case LOG_POSTPONE:
	    case LOG_START_CHKPT:
	    case LOG_END_CHKPT:
	    case LOG_SAVEPOINT:
	    case LOG_2PC_PREPARE:
	    case LOG_2PC_START:
	    case LOG_2PC_ABORT_DECISION:
	    case LOG_2PC_ABORT_INFORM_PARTICPS:
	    case LOG_REPLICATION_DATA:
	    case LOG_REPLICATION_STATEMENT:
	    case LOG_SYSOP_ATOMIC_START:
	    case LOG_DUMMY_HA_SERVER_STATE:
	    case LOG_DUMMY_OVF_RECORD:
	    case LOG_DUMMY_GENERIC:
	    case LOG_SUPPLEMENTAL_INFO:
	      break;

	    case LOG_RUN_POSTPONE:
	    case LOG_COMMIT_WITH_POSTPONE:
	    case LOG_COMMIT_WITH_POSTPONE_OBSOLETE:
	    case LOG_SYSOP_START_POSTPONE:
	      /* Undo of run postpone system operation. End here. */
	      assert (!LOG_ISRESTARTED ());
	      LSA_SET_NULL (&prev_tranlsa);
	      break;

	    case LOG_COMMIT:
	    case LOG_ABORT:
	    case LOG_2PC_COMMIT_DECISION:
	    case LOG_2PC_COMMIT_INFORM_PARTICPS:
	    case LOG_2PC_RECV_ACK:
	    case LOG_DUMMY_CRASH_RECOVERY:
	    case LOG_END_OF_LOG:
	    case LOG_SMALLER_LOGREC_TYPE:
	    case LOG_LARGER_LOGREC_TYPE:
	    default:
	      {
		char msg[LINE_MAX];

		er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_PAGE_CORRUPTED, 1, log_lsa.pageid);
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
  tdes->m_log_postpone_cache.reset ();

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
log_get_next_nested_top (THREAD_ENTRY * thread_p, LOG_TDES * tdes, LOG_LSA * start_postpone_lsa,
			 LOG_TOPOP_RANGE ** out_nxtop_range_stack)
{
  LOG_REC_SYSOP_END *top_result;
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

  if (LSA_ISNULL (&tdes->tail_topresult_lsa) || !LSA_GT (&tdes->tail_topresult_lsa, start_postpone_lsa))
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
	  nxtop_stack = (LOG_TOPOP_RANGE *) malloc (nxtop_stack_size * sizeof (LOG_TOPOP_RANGE));
	  if (nxtop_stack == NULL)
	    {
	      if (prev_nxtop_stack != *out_nxtop_range_stack)
		{
		  free_and_init (prev_nxtop_stack);
		}
	      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_get_next_nested_top");
	      return 0;
	    }

	  memcpy (nxtop_stack, prev_nxtop_stack, (nxtop_stack_size / 2) * sizeof (LOG_TOPOP_RANGE));

	  if (prev_nxtop_stack != *out_nxtop_range_stack)
	    {
	      free_and_init (prev_nxtop_stack);
	    }
	}

      if (last_fetch_page_id != top_result_lsa.pageid)
	{
	  if (logpb_fetch_page (thread_p, &top_result_lsa, LOG_CS_FORCE_USE, log_pgptr) != NO_ERROR)
	    {
	      if (nxtop_stack != *out_nxtop_range_stack)
		{
		  free_and_init (nxtop_stack);
		}
	      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_get_next_nested_top");
	      return 0;
	    }
	}

      log_rec = LOG_GET_LOG_RECORD_HEADER (log_pgptr, &top_result_lsa);

      if (log_rec->type == LOG_SYSOP_END)
	{
	  /* Read the DATA HEADER */
	  LOG_LSA prev_tran_lsa = log_rec->back_lsa;

	  LSA_COPY (&tmp_log_lsa, &top_result_lsa);
	  LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER), &tmp_log_lsa, log_pgptr);
	  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (LOG_REC_SYSOP_END), &tmp_log_lsa, log_pgptr);
	  top_result = (LOG_REC_SYSOP_END *) ((char *) log_pgptr->area + tmp_log_lsa.offset);
	  last_fetch_page_id = tmp_log_lsa.pageid;

	  /*
	   * There may be some nested top system operations that are committed
	   * and aborted in the desired region
	   */
	  if (LSA_ISNULL (&prev_last_parent_lsa) || LSA_LE (&top_result_lsa, &prev_last_parent_lsa))
	    {
	      LSA_COPY (&(nxtop_stack[nxtop_count].start_lsa), &top_result->lastparent_lsa);
	      if (top_result->type == LOG_SYSOP_END_LOGICAL_RUN_POSTPONE)
		{
		  /* we need to process this log record. end range at previous log record. */
		  LSA_COPY (&(nxtop_stack[nxtop_count].end_lsa), &prev_tran_lsa);
		}
	      else
		{
		  /* end range at system op end log record */
		  LSA_COPY (&(nxtop_stack[nxtop_count].end_lsa), &top_result_lsa);
		}

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
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_get_next_nested_top");
	  return 0;
	}
    }
  while (top_result_lsa.pageid != NULL_PAGEID && LSA_GT (&top_result_lsa, start_postpone_lsa));

  *out_nxtop_range_stack = nxtop_stack;

  return nxtop_count;
}

/*
 * log_tran_do_postpone () - do postpone for transaction.
 *
 * return        : void
 * thread_p (in) : thread entry
 * tdes (in)     : transaction descriptor
 */
static void
log_tran_do_postpone (THREAD_ENTRY * thread_p, LOG_TDES * tdes)
{
  if (LSA_ISNULL (&tdes->posp_nxlsa))
    {
      /* nothing to do */
      return;
    }

  assert (tdes->topops.last < 0);

  log_append_commit_postpone (thread_p, tdes, &tdes->posp_nxlsa);

  if (tdes->m_log_postpone_cache.do_postpone (*thread_p, tdes->posp_nxlsa))
    {
      // do postpone from cache first
      perfmon_inc_stat (thread_p, PSTAT_TRAN_NUM_PPCACHE_HITS);
      return;
    }
  perfmon_inc_stat (thread_p, PSTAT_TRAN_NUM_PPCACHE_MISS);

  log_do_postpone (thread_p, tdes, &tdes->posp_nxlsa);
}

/*
 * log_sysop_do_postpone () - do postpone for system operation
 *
 * return         : void
 * thread_p (in)  : thread entry
 * tdes (in)      : transaction descriptor
 * sysop_end (in) : system end op log record
 * data_size (in) : data size (for logical undo)
 * data (in)      : data (for logical undo)
 */
static void
log_sysop_do_postpone (THREAD_ENTRY * thread_p, LOG_TDES * tdes, LOG_REC_SYSOP_END * sysop_end, int data_size,
		       const char *data)
{
  LOG_REC_SYSOP_START_POSTPONE sysop_start_postpone;
  TRAN_STATE save_state = tdes->state;

  if (LSA_ISNULL (LOG_TDES_LAST_SYSOP_POSP_LSA (tdes)))
    {
      /* nothing to postpone */
      return;
    }

  assert (sysop_end != NULL);
  assert (sysop_end->type != LOG_SYSOP_END_ABORT);
  /* we cannot have TRAN_UNACTIVE_TOPOPE_COMMITTED_WITH_POSTPONE inside TRAN_UNACTIVE_TOPOPE_COMMITTED_WITH_POSTPONE */
  assert (tdes->state != TRAN_UNACTIVE_TOPOPE_COMMITTED_WITH_POSTPONE);

  sysop_start_postpone.sysop_end = *sysop_end;
  sysop_start_postpone.posp_lsa = *LOG_TDES_LAST_SYSOP_POSP_LSA (tdes);
  log_append_sysop_start_postpone (thread_p, tdes, &sysop_start_postpone, data_size, data);

  if (tdes->m_log_postpone_cache.do_postpone (*thread_p, *(LOG_TDES_LAST_SYSOP_POSP_LSA (tdes))))
    {
      /* Do postpone was run from cached postpone entries. */
      tdes->state = save_state;
      perfmon_inc_stat (thread_p, PSTAT_TRAN_NUM_TOPOP_PPCACHE_HITS);
      return;
    }
  perfmon_inc_stat (thread_p, PSTAT_TRAN_NUM_TOPOP_PPCACHE_MISS);

  log_do_postpone (thread_p, tdes, LOG_TDES_LAST_SYSOP_POSP_LSA (tdes));

  tdes->state = save_state;
}

/*
 * log_do_postpone - Scan forward doing postpone operations of given transaction
 *
 * return: nothing
 *
 *   tdes(in): Transaction descriptor
 *   start_posplsa(in): Where to start looking for postpone records
 *
 * NOTE: Scan the log forward doing postpone operations of given transaction.
 *       This function is invoked after a transaction is declared committed with postpone actions.
 */
void
log_do_postpone (THREAD_ENTRY * thread_p, LOG_TDES * tdes, LOG_LSA * start_postpone_lsa)
{
  LOG_LSA end_postpone_lsa;	/* The last postpone record of transaction cannot be after this address */
  LOG_LSA start_seek_lsa;	/* start looking for postpone records at this address */
  LOG_LSA *end_seek_lsa;	/* Stop looking for postpone records at this address */
  LOG_LSA next_start_seek_lsa;	/* Next address to look for postpone records. Usually the end of a top system
				 * operation. */
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

  assert (!LSA_ISNULL (start_postpone_lsa));
  assert (tdes->state == TRAN_UNACTIVE_TOPOPE_COMMITTED_WITH_POSTPONE || tdes->state == TRAN_UNACTIVE_WILL_COMMIT
	  || tdes->state == TRAN_UNACTIVE_COMMITTED_WITH_POSTPONE);

  aligned_log_pgbuf = PTR_ALIGN (log_pgbuf, MAX_ALIGNMENT);
  log_pgptr = (LOG_PAGE *) aligned_log_pgbuf;

  LSA_COPY (&end_postpone_lsa, &tdes->tail_lsa);
  LSA_COPY (&next_start_seek_lsa, start_postpone_lsa);

  nxtop_stack = nxtop_array;
  nxtop_count = log_get_next_nested_top (thread_p, tdes, start_postpone_lsa, &nxtop_stack);

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
	  LOG_LSA fetch_lsa;

	  /* Fetch the page where the postpone LSA record is located */
	  LSA_COPY (&log_lsa, &forward_lsa);
	  fetch_lsa.pageid = log_lsa.pageid;
	  fetch_lsa.offset = LOG_PAGESIZE;
	  if (logpb_fetch_page (thread_p, &fetch_lsa, LOG_CS_FORCE_USE, log_pgptr) != NO_ERROR)
	    {
	      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_do_postpone");
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
	       * If an offset is missing, it is because we archive an incomplete log record.
	       * This log_record was completed later.
	       * Thus, we have to find the offset by searching for the next log_record in the page.
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

	      if (forward_lsa.pageid == NULL_PAGEID && logpb_is_page_in_archive (log_lsa.pageid))
		{
		  forward_lsa.pageid = log_lsa.pageid + 1;
		}

	      if (log_rec->trid == tdes->trid)
		{
		  switch (log_rec->type)
		    {
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
		    case LOG_SAVEPOINT:
		    case LOG_DUMMY_HEAD_POSTPONE:
		    case LOG_REPLICATION_DATA:
		    case LOG_REPLICATION_STATEMENT:
		    case LOG_SYSOP_ATOMIC_START:
		    case LOG_DUMMY_HA_SERVER_STATE:
		    case LOG_DUMMY_OVF_RECORD:
		    case LOG_DUMMY_GENERIC:
		    case LOG_SUPPLEMENTAL_INFO:
		      break;

		    case LOG_POSTPONE:
		      {
			int mod_factor = 5000;	/* 0.02% */

			FI_TEST_ARG (thread_p, FI_TEST_LOG_MANAGER_RANDOM_EXIT_AT_RUN_POSTPONE, &mod_factor, 0);
		      }

		      if (log_run_postpone_op (thread_p, &log_lsa, log_pgptr) != NO_ERROR)
			{
			  goto end;
			}

		      /* TODO: consider to add FI here */
		      break;

		    case LOG_COMMIT_WITH_POSTPONE:
		    case LOG_COMMIT_WITH_POSTPONE_OBSOLETE:
		    case LOG_SYSOP_START_POSTPONE:
		    case LOG_2PC_PREPARE:
		    case LOG_2PC_START:
		    case LOG_2PC_COMMIT_DECISION:
		      /* This is it */
		      LSA_SET_NULL (&forward_lsa);
		      break;

		    case LOG_SYSOP_END:
		      if (!LSA_EQ (&log_lsa, &start_seek_lsa))
			{
#if defined(CUBRID_DEBUG)
			  er_log_debug (ARG_FILE_LINE,
					"log_do_postpone: SYSTEM ERROR.. Bad log_rectype = %d\n (%s)."
					" Maybe BAD POSTPONE RANGE\n", log_rec->type, log_to_string (log_rec->type));
#endif /* CUBRID_DEBUG */
			  ;	/* Nothing */
			}
		      break;

		    case LOG_END_OF_LOG:
		      if (forward_lsa.pageid == NULL_PAGEID && logpb_is_page_in_archive (log_lsa.pageid))
			{
			  forward_lsa.pageid = log_lsa.pageid + 1;
			}
		      break;

		    case LOG_COMMIT:
		    case LOG_ABORT:
		    case LOG_START_CHKPT:
		    case LOG_END_CHKPT:
		    case LOG_2PC_ABORT_DECISION:
		    case LOG_2PC_ABORT_INFORM_PARTICPS:
		    case LOG_2PC_COMMIT_INFORM_PARTICPS:
		    case LOG_2PC_RECV_ACK:
		    case LOG_DUMMY_CRASH_RECOVERY:
		    case LOG_SMALLER_LOGREC_TYPE:
		    case LOG_LARGER_LOGREC_TYPE:
		    default:
#if defined(CUBRID_DEBUG)
		      er_log_debug (ARG_FILE_LINE,
				    "log_do_postpone: SYSTEM ERROR..Bad log_rectype = %d (%s)... ignored\n",
				    log_rec->type, log_to_string (log_rec->type));
#endif /* CUBRID_DEBUG */
		      break;
		    }
		}

	      /*
	       * We can fix the lsa.pageid in the case of log_records without forward address at this moment.
	       */

	      if (forward_lsa.offset == NULL_OFFSET && forward_lsa.pageid != NULL_PAGEID
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
log_run_postpone_op (THREAD_ENTRY * thread_p, LOG_LSA * log_lsa, LOG_PAGE * log_pgptr)
{
  LOG_LSA ref_lsa;		/* The address of a postpone record */
  LOG_REC_REDO redo;		/* A redo log record */
  char *rcv_data = NULL;
  char *area = NULL;

  LSA_COPY (&ref_lsa, log_lsa);

  /* Get the DATA HEADER */
  LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER), log_lsa, log_pgptr);
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (LOG_REC_REDO), log_lsa, log_pgptr);

  redo = *((LOG_REC_REDO *) ((char *) log_pgptr->area + log_lsa->offset));

  LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_REC_REDO), log_lsa, log_pgptr);

  /* GET AFTER DATA */

  /* If data is contained in only one buffer, pass pointer directly. Otherwise, allocate a contiguous area, copy
   * data and pass this area. At the end deallocate the area.
   */
  if (log_lsa->offset + redo.length < (int) LOGAREA_SIZE)
    {
      rcv_data = (char *) log_pgptr->area + log_lsa->offset;
    }
  else
    {
      /* Need to copy the data into a contiguous area */
      area = (char *) malloc (redo.length);
      if (area == NULL)
	{
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_run_postpone_op");

	  return ER_FAILED;
	}

      /* Copy the data */
      logpb_copy_from_log (thread_p, area, redo.length, log_lsa, log_pgptr);
      rcv_data = area;
    }

  (void) log_execute_run_postpone (thread_p, &ref_lsa, &redo, rcv_data);

  if (area != NULL)
    {
      free_and_init (area);
    }

  return NO_ERROR;
}

/*
 * log_execute_run_postpone () - Execute run postpone.
 *
 * return	      : Error code.
 * thread_p (in)      : Thread entry.
 * log_lsa (in)	      : Postpone log LSA.
 * redo (in)	      : Redo log data.
 * redo_rcv_data (in) : Redo recovery data.
 */
int
log_execute_run_postpone (THREAD_ENTRY * thread_p, LOG_LSA * log_lsa, LOG_REC_REDO * redo, char *redo_rcv_data)
{
  int error_code = NO_ERROR;
  LOG_RCV rcv;			/* Recovery structure for execution */
  VPID rcv_vpid;		/* Location of data to redo */
  LOG_RCVINDEX rcvindex;	/* The recovery index */
  LOG_DATA_ADDR rvaddr;

  rcvindex = redo->data.rcvindex;
  rcv_vpid.volid = redo->data.volid;
  rcv_vpid.pageid = redo->data.pageid;
  rcv.offset = redo->data.offset;
  rcv.length = redo->length;
  rcv.data = redo_rcv_data;

  if (VPID_ISNULL (&rcv_vpid))
    {
      /* logical */
      rcv.pgptr = NULL;
    }
  else
    {
      error_code =
	pgbuf_fix_if_not_deallocated (thread_p, &rcv_vpid, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH, &rcv.pgptr);
      if (error_code != NO_ERROR)
	{
	  assert (false);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_MAYNEED_MEDIA_RECOVERY, 1,
		  fileio_get_volume_label (rcv_vpid.volid, PEEK));
	  return ER_LOG_MAYNEED_MEDIA_RECOVERY;
	}
      if (rcv.pgptr == NULL)
	{
	  /* deallocated */
	  return NO_ERROR;
	}
    }

  /* Now call the REDO recovery function */

  if (RCV_IS_LOGICAL_RUN_POSTPONE_MANUAL (rcvindex))
    {
      LSA_COPY (&rcv.reference_lsa, log_lsa);

      error_code = (*RV_fun[rcvindex].redofun) (thread_p, &rcv);
      assert (error_code == NO_ERROR);
    }
  else if (RCV_IS_LOGICAL_LOG (&rcv_vpid, rcvindex))
    {
      /* Logical postpone. Use a system operation and commit with run postpone */
      log_sysop_start (thread_p);

      error_code = (*RV_fun[rcvindex].redofun) (thread_p, &rcv);
      assert (error_code == NO_ERROR);

      log_sysop_end_logical_run_postpone (thread_p, log_lsa);
    }
  else
    {
      /* Write the corresponding run postpone record for the postpone action */
      rvaddr.offset = rcv.offset;
      rvaddr.pgptr = rcv.pgptr;

      log_append_run_postpone (thread_p, rcvindex, &rvaddr, &rcv_vpid, rcv.length, rcv.data, log_lsa);

      error_code = (*RV_fun[rcvindex].redofun) (thread_p, &rcv);
      assert (error_code == NO_ERROR);
    }

  if (rcv.pgptr != NULL)
    {
      pgbuf_unfix (thread_p, rcv.pgptr);
    }

  return error_code;
}

/*
 * log_find_end_log - FIND END OF LOG
 *
 * return: nothing
 *
 *   end_lsa(in/out): Address of end of log
 *
 * NOTE: Find the end of the log (i.e., the end of the active portion of the log).
 */
static void
log_find_end_log (THREAD_ENTRY * thread_p, LOG_LSA * end_lsa)
{
  LOG_PAGEID pageid;		/* Log page identifier */
  char log_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT], *aligned_log_pgbuf;
  LOG_PAGE *log_pgptr = NULL;	/* Pointer to a log page */
  LOG_RECORD_HEADER *eof = NULL;	/* End of log record */
  LOG_RECTYPE type;		/* Type of a log record */

  aligned_log_pgbuf = PTR_ALIGN (log_pgbuf, MAX_ALIGNMENT);

  /* Guess the end of the log from the header */

  LSA_COPY (end_lsa, &log_Gl.hdr.append_lsa);
  type = LOG_LARGER_LOGREC_TYPE;

  log_pgptr = (LOG_PAGE *) aligned_log_pgbuf;

  while (type != LOG_END_OF_LOG && !LSA_ISNULL (end_lsa))
    {
      LOG_LSA fetch_lsa;

      fetch_lsa.pageid = end_lsa->pageid;
      fetch_lsa.offset = LOG_PAGESIZE;

      /* Fetch the page */
      if ((logpb_fetch_page (thread_p, &fetch_lsa, LOG_CS_FORCE_USE, log_pgptr)) != NO_ERROR)
	{
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_find_end_log");
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
	  if (!(end_lsa->offset == NULL_OFFSET && (end_lsa->offset = log_pgptr->hdr.offset) == NULL_OFFSET))
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
	      else if (type <= LOG_SMALLER_LOGREC_TYPE || type >= LOG_LARGER_LOGREC_TYPE)
		{
#if defined(CUBRID_DEBUG)
		  er_log_debug (ARG_FILE_LINE, "log_find_end_log: Unknown record type = %d (%s).\n", type,
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

      if (type == LOG_END_OF_LOG && eof != NULL && !LSA_EQ (end_lsa, &log_Gl.hdr.append_lsa))
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
 * log_is_active_log_sane - Check whether the active log volume is sane. Note that it does NOT guarantee that the active log volume is perfectly fine. It checks the existance of the log volume, the checksum of the log header page and compatibility.
 *
 * return: whether the active log volume is sane
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
 */
bool
log_is_active_log_sane (THREAD_ENTRY * thread_p, const char *db_fullname, const char *logpath,
			const char *prefix_logname)
{
  LOG_HEADER hdr;
  REL_COMPATIBILITY compat;
  bool is_corrupted = false;
  char log_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT], *aligned_log_pgbuf;
  LOG_PAGE *log_pgptr = NULL;
  int error_code = NO_ERROR;

  aligned_log_pgbuf = PTR_ALIGN (log_pgbuf, MAX_ALIGNMENT);
  log_pgptr = (LOG_PAGE *) aligned_log_pgbuf;

  error_code = logpb_fetch_header_from_active_log (thread_p, db_fullname, logpath, prefix_logname, &hdr, log_pgptr);
  if (error_code != NO_ERROR)
    {
      _er_log_debug (ARG_FILE_LINE, "The active log volume (%s) is insane: mounting or fetching header fails.\n",
		     logpath);
      return false;
    }

  error_code = logpb_page_check_corruption (thread_p, log_pgptr, &is_corrupted);
  if (error_code != NO_ERROR || is_corrupted == true)
    {
      _er_log_debug (ARG_FILE_LINE, "The active log volume (%s) is insane: the header page is corrupted.\n", logpath);
      return false;
    }

  if (rel_is_log_compatible (hdr.db_release, rel_release_string ()) == false)
    {
      _er_log_debug (ARG_FILE_LINE,
		     "The active log volume (%s) is insane: unmatched release version. database release version: %s, build release version: %s\n",
		     logpath, hdr.db_release, rel_release_string ());
      return false;
    }

  return true;
}

/*
 * log_recreate - RECREATE THE LOG WITHOUT REMOVING THE DATABASE
 *
 * return:
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
int
log_recreate (THREAD_ENTRY * thread_p, const char *db_fullname, const char *logpath, const char *prefix_logname,
	      DKNPAGES log_npages, FILE * out_fp)
{
  const char *vlabel;
  INT64 db_creation;
  DISK_VOLPURPOSE vol_purpose;
  DISK_VOLUME_SPACE_INFO space_info;
  VOLID volid;
  int vdes;
  LOG_LSA init_nontemp_lsa;
  int ret = NO_ERROR;

  ret = disk_get_creation_time (thread_p, LOG_DBFIRST_VOLID, &db_creation);
  if (ret != NO_ERROR)
    {
      return ret;
    }

  ret = log_create_internal (thread_p, db_fullname, logpath, prefix_logname, log_npages, &db_creation);
  if (ret != NO_ERROR)
    {
      return ret;
    }

  (void) log_initialize_internal (thread_p, db_fullname, logpath, prefix_logname, false, NULL, true);

  /*
   * RESET RECOVERY INFORMATION ON ALL DATA VOLUMES
   */

  LSA_SET_NULL (&init_nontemp_lsa);

  for (volid = LOG_DBFIRST_VOLID; volid != NULL_VOLID; volid = fileio_find_next_perm_volume (thread_p, volid))
    {
      char vol_fullname[PATH_MAX];

      vlabel = fileio_get_volume_label (volid, PEEK);

      /* Find the current pages of the volume and its descriptor */

      if (xdisk_get_purpose_and_space_info (thread_p, volid, &vol_purpose, &space_info) != NO_ERROR)
	{
	  /* we just give up? */
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

      if (vol_purpose != DB_TEMPORARY_DATA_PURPOSE)
	{
	  (void) fileio_reset_volume (thread_p, vdes, vlabel, DISK_SECTS_NPAGES (space_info.n_total_sects),
				      &init_nontemp_lsa);
	}

      (void) disk_set_creation (thread_p, volid, vlabel, &log_Gl.hdr.db_creation, &log_Gl.hdr.chkpt_lsa, false,
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

  return ret;
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
log_get_io_page_size (THREAD_ENTRY * thread_p, const char *db_fullname, const char *logpath, const char *prefix_logname)
{
  PGLENGTH db_iopagesize;
  PGLENGTH log_page_size;
  INT64 ignore_dbcreation;
  float ignore_dbcomp;
  int dummy;

  LOG_CS_ENTER (thread_p);
  if (logpb_find_header_parameters (thread_p, false, db_fullname, logpath, prefix_logname, &db_iopagesize,
				    &log_page_size, &ignore_dbcreation, &ignore_dbcomp, &dummy) == -1)
    {
      /*
       * For case where active log could not be found, user still needs
       * an error.
       */
      if (er_errid () == NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_MOUNT_FAIL, 1, log_Name_active);
	}

      LOG_CS_EXIT (thread_p);
      return -1;
    }
  else
    {
      if (IO_PAGESIZE != db_iopagesize || LOG_PAGESIZE != log_page_size)
	{
	  if (db_set_page_size (db_iopagesize, log_page_size) != NO_ERROR)
	    {
	      LOG_CS_EXIT (thread_p);
	      return -1;
	    }
	  else
	    {
	      if (sysprm_reload_and_init (NULL, NULL) != NO_ERROR)
		{
		  LOG_CS_EXIT (thread_p);
		  return -1;
		}

	      /* page size changed, reinit tran tables only if previously initialized */
	      if (log_Gl.trantable.area == NULL)
		{
		  LOG_CS_EXIT (thread_p);
		  return db_iopagesize;
		}

	      if (logtb_define_trantable_log_latch (thread_p, log_Gl.trantable.num_total_indices) != NO_ERROR)
		{
		  LOG_CS_EXIT (thread_p);
		  return -1;
		}
	    }

	}

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
log_get_charset_from_header_page (THREAD_ENTRY * thread_p, const char *db_fullname, const char *logpath,
				  const char *prefix_logname)
{
  PGLENGTH dummy_db_iopagesize;
  PGLENGTH dummy_ignore_log_page_size;
  INT64 dummy_ignore_dbcreation;
  float dummy_ignore_dbcomp;
  int db_charset = INTL_CODESET_NONE;

  LOG_CS_ENTER (thread_p);
  if (logpb_find_header_parameters (thread_p, false, db_fullname, logpath, prefix_logname, &dummy_db_iopagesize,
				    &dummy_ignore_log_page_size, &dummy_ignore_dbcreation, &dummy_ignore_dbcomp,
				    &db_charset) == -1)
    {
      /*
       * For case where active log could not be found, user still needs
       * an error.
       */
      if (er_errid () == NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_MOUNT_FAIL, 1, log_Name_active);
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
 * log_rv_dump_hexa () - Point recovery data as hexadecimals.
 *
 * return      : Void.
 * fp (in)     : Print output.
 * length (in) : Recovery data length.
 * data (in)   : Recovery data.
 */
void
log_rv_dump_hexa (FILE * fp, int length, void *data)
{
  log_hexa_dump (fp, length, data);
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
log_simulate_crash (THREAD_ENTRY * thread_p, int flush_log, int flush_data_pages)
{
  LOG_CS_ENTER (thread_p);

  if (log_Gl.trantable.area == NULL || !logpb_is_pool_initialized ())
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

  logpb_finalize_pool (thread_p);
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
log_active_log_header_start_scan (THREAD_ENTRY * thread_p, int show_type, DB_VALUE ** arg_values, int arg_cnt,
				  void **ptr)
{
  int error = NO_ERROR;
  const char *path;
  int fd = -1;
  ACTIVE_LOG_HEADER_SCAN_CTX *ctx = NULL;

  *ptr = NULL;

  assert (arg_cnt == 1);

  ctx = (ACTIVE_LOG_HEADER_SCAN_CTX *) db_private_alloc (thread_p, sizeof (ACTIVE_LOG_HEADER_SCAN_CTX));

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
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_MOUNT_FAIL, 1, path);
	  error = ER_IO_MOUNT_FAIL;
	  goto exit_on_error;
	}

      if (read (fd, page_hdr, LOG_PAGESIZE) != LOG_PAGESIZE)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_MOUNT_FAIL, 1, path);
	  error = ER_IO_MOUNT_FAIL;
	  goto exit_on_error;
	}

      memcpy (&ctx->header, page_hdr->area, sizeof (LOG_HEADER));

      ctx->header.magic[sizeof (ctx->header.magic) - 1] = 0;
      ctx->header.db_release[sizeof (ctx->header.db_release) - 1] = 0;
      ctx->header.prefix_name[sizeof (ctx->header.prefix_name) - 1] = 0;

      close (fd);
      fd = -1;

      if (memcmp (ctx->header.magic, CUBRID_MAGIC_LOG_ACTIVE, strlen (CUBRID_MAGIC_LOG_ACTIVE)) != 0)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_MOUNT_FAIL, 1, path);
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
log_active_log_header_next_scan (THREAD_ENTRY * thread_p, int cursor, DB_VALUE ** out_values, int out_cnt, void *ptr)
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

  db_make_string (out_values[idx], "LOG_PSTATUS_OBSOLETE");
  idx++;

  logpb_backup_level_info_to_string (buf, sizeof (buf), header->bkinfo + FILEIO_BACKUP_FULL_LEVEL);
  error = db_make_string_copy (out_values[idx], buf);
  idx++;
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  logpb_backup_level_info_to_string (buf, sizeof (buf), header->bkinfo + FILEIO_BACKUP_BIG_INCREMENT_LEVEL);
  error = db_make_string_copy (out_values[idx], buf);
  idx++;
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  logpb_backup_level_info_to_string (buf, sizeof (buf), header->bkinfo + FILEIO_BACKUP_SMALL_INCREMENT_LEVEL);
  error = db_make_string_copy (out_values[idx], buf);
  idx++;
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  str = css_ha_server_state_string ((HA_SERVER_STATE) header->ha_server_state);
  db_make_string (out_values[idx], str);
  idx++;

  str = logwr_log_ha_filestat_to_string ((LOG_HA_FILESTAT) header->ha_file_status);
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

  db_make_bigint (out_values[idx], header->mvcc_next_id);
  idx++;

  lsa_to_string (buf, sizeof (buf), &header->mvcc_op_log_lsa);
  error = db_make_string_copy (out_values[idx], buf);
  idx++;
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  if (header->oldest_visible_mvccid == MVCCID_NULL)
    {
      db_make_null (out_values[idx]);
    }
  else
    {
      db_make_bigint (out_values[idx], header->oldest_visible_mvccid);
    }
  idx++;

  if (header->newest_block_mvccid == MVCCID_NULL)
    {
      db_make_null (out_values[idx]);
    }
  else
    {
      db_make_bigint (out_values[idx], header->newest_block_mvccid);
    }
  idx++;

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
log_archive_log_header_start_scan (THREAD_ENTRY * thread_p, int show_type, DB_VALUE ** arg_values, int arg_cnt,
				   void **ptr)
{
  int error = NO_ERROR;
  const char *path;
  int fd;
  char buf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT];
  LOG_PAGE *page_hdr;
  ARCHIVE_LOG_HEADER_SCAN_CTX *ctx = NULL;

  *ptr = NULL;

  assert (DB_VALUE_TYPE (arg_values[0]) == DB_TYPE_CHAR);

  ctx = (ARCHIVE_LOG_HEADER_SCAN_CTX *) db_private_alloc (thread_p, sizeof (ARCHIVE_LOG_HEADER_SCAN_CTX));
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
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_MOUNT_FAIL, 1, path);
      error = ER_IO_MOUNT_FAIL;
      goto exit_on_error;
    }

  if (read (fd, page_hdr, LOG_PAGESIZE) != LOG_PAGESIZE)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_MOUNT_FAIL, 1, path);
      error = ER_IO_MOUNT_FAIL;
      goto exit_on_error;
    }

  memcpy (&ctx->header, page_hdr->area, sizeof (LOG_ARV_HEADER));
  close (fd);
  fd = -1;

  ctx->header.magic[sizeof (ctx->header.magic) - 1] = 0;

  if (memcmp (ctx->header.magic, CUBRID_MAGIC_LOG_ARCHIVE, strlen (CUBRID_MAGIC_LOG_ARCHIVE)) != 0)
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
log_archive_log_header_next_scan (THREAD_ENTRY * thread_p, int cursor, DB_VALUE ** out_values, int out_cnt, void *ptr)
{
  int error = NO_ERROR;
  int idx = 0;
  int val;
  DB_DATETIME time_val;

  ARCHIVE_LOG_HEADER_SCAN_CTX *ctx = (ARCHIVE_LOG_HEADER_SCAN_CTX *) ptr;
  LOG_ARV_HEADER *header = &ctx->header;

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

  val = offsetof (LOG_PAGE, area) + offsetof (LOG_ARV_HEADER, magic);
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

/*
 * log_set_ha_promotion_time () - set ha promotion time
 *   return: none
 *
 *   thread_p(in):
 *   ha_promotion_time(in):
 */
void
log_set_ha_promotion_time (THREAD_ENTRY * thread_p, INT64 ha_promotion_time)
{
  LOG_CS_ENTER (thread_p);
  log_Gl.hdr.ha_promotion_time = ha_promotion_time;
  LOG_CS_EXIT (thread_p);

  return;
}

/*
 * log_set_db_restore_time () - set db restore time
 *   return: none
 *
 *   thread_p(in):
 *   db_restore_time(in):
 */
void
log_set_db_restore_time (THREAD_ENTRY * thread_p, INT64 db_restore_time)
{
  LOG_CS_ENTER (thread_p);

  log_Gl.hdr.db_restore_time = db_restore_time;

  LOG_CS_EXIT (thread_p);
}

/*
 * log_get_undo_record () - gets undo record from log lsa adress
 *   return: S_SUCCESS or ER_code
 *
 * thread_p (in):
 * lsa_addr (in):
 * page (in):
 * record (in/out):
 */
SCAN_CODE
log_get_undo_record (THREAD_ENTRY * thread_p, LOG_PAGE * log_page_p, LOG_LSA process_lsa, RECDES * recdes)
{
  LOG_RECORD_HEADER *log_rec_header = NULL;
  LOG_REC_MVCC_UNDO *mvcc_undo = NULL;
  LOG_REC_MVCC_UNDOREDO *mvcc_undoredo = NULL;
  LOG_REC_UNDO *undo = NULL;
  LOG_REC_UNDOREDO *undoredo = NULL;
  LOG_REC_SUPPLEMENT *supplement = NULL;
  int udata_length;
  int udata_size;
  char *undo_data;
  LOG_LSA oldest_prior_lsa;
  bool is_zipped = false;
  char log_buf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT];
  LOG_ZIP *log_unzip_ptr = NULL;
  char *area = NULL;
  SCAN_CODE scan = S_SUCCESS;
  bool area_was_mallocated = false;

  /* assert log record is not in prior list */
  oldest_prior_lsa = *log_get_append_lsa ();
  assert (LSA_LT (&process_lsa, &oldest_prior_lsa));

  log_rec_header = LOG_GET_LOG_RECORD_HEADER (log_page_p, &process_lsa);
  LOG_READ_ADD_ALIGN (thread_p, sizeof (*log_rec_header), &process_lsa, log_page_p);

  if (log_rec_header->type == LOG_MVCC_UNDO_DATA)
    {
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*mvcc_undo), &process_lsa, log_page_p);
      mvcc_undo = (LOG_REC_MVCC_UNDO *) (log_page_p->area + process_lsa.offset);

      udata_length = mvcc_undo->undo.length;
      LOG_READ_ADD_ALIGN (thread_p, sizeof (*mvcc_undo), &process_lsa, log_page_p);
    }
  else if (log_rec_header->type == LOG_MVCC_UNDOREDO_DATA || log_rec_header->type == LOG_MVCC_DIFF_UNDOREDO_DATA)
    {
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*mvcc_undoredo), &process_lsa, log_page_p);
      mvcc_undoredo = (LOG_REC_MVCC_UNDOREDO *) (log_page_p->area + process_lsa.offset);

      udata_length = mvcc_undoredo->undoredo.ulength;
      LOG_READ_ADD_ALIGN (thread_p, sizeof (*mvcc_undoredo), &process_lsa, log_page_p);
    }
  else if (log_rec_header->type == LOG_UNDO_DATA)
    {
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*undo), &process_lsa, log_page_p);
      undo = (LOG_REC_UNDO *) (log_page_p->area + process_lsa.offset);

      udata_length = undo->length;
      LOG_READ_ADD_ALIGN (thread_p, sizeof (*undo), &process_lsa, log_page_p);
    }
  else if (log_rec_header->type == LOG_UNDOREDO_DATA || log_rec_header->type == LOG_DIFF_UNDOREDO_DATA)
    {
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*undoredo), &process_lsa, log_page_p);
      undoredo = (LOG_REC_UNDOREDO *) (log_page_p->area + process_lsa.offset);

      udata_length = undoredo->ulength;
      LOG_READ_ADD_ALIGN (thread_p, sizeof (*undoredo), &process_lsa, log_page_p);
    }
  else if (log_rec_header->type == LOG_SUPPLEMENTAL_INFO)
    {
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*supplement), &process_lsa, log_page_p);
      supplement = (LOG_REC_SUPPLEMENT *) (log_page_p->area + process_lsa.offset);

      udata_length = supplement->length;
      LOG_READ_ADD_ALIGN (thread_p, sizeof (*supplement), &process_lsa, log_page_p);
    }
  else
    {
      assert_release (log_rec_header->type == LOG_MVCC_UNDO_DATA || log_rec_header->type == LOG_MVCC_UNDOREDO_DATA
		      || log_rec_header->type == LOG_MVCC_DIFF_UNDOREDO_DATA || log_rec_header->type == LOG_UNDO_DATA
		      || log_rec_header->type == LOG_UNDOREDO_DATA
		      || log_rec_header->type == LOG_MVCC_DIFF_UNDOREDO_DATA
		      || log_rec_header->type == LOG_SUPPLEMENTAL_INFO);
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_FATAL_ERROR, 1, "Expecting undo/undoredo log record");
      scan = S_ERROR;
      goto exit;
    }

  /* get undo record */
  if (ZIP_CHECK (udata_length))
    {
      /* Get real size */
      udata_size = (int) GET_ZIP_LEN (udata_length);
      is_zipped = true;
    }
  else
    {
      udata_size = udata_length;
    }

  /*
   * If data is contained in only one buffer, pass pointer directly.
   * Otherwise, copy the data into a contiguous area and pass this area.
   */
  if (process_lsa.offset + udata_size < (int) LOGAREA_SIZE)
    {
      undo_data = (char *) log_page_p->area + process_lsa.offset;
    }
  else
    {
      /* Need to copy the data into a contiguous area */

      if (udata_size <= IO_MAX_PAGE_SIZE)
	{
	  area = PTR_ALIGN (log_buf, MAX_ALIGNMENT);
	}
      else
	{
	  area = (char *) malloc (udata_size);
	  if (area == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, udata_size);
	      scan = S_ERROR;
	      goto exit;
	    }
	  area_was_mallocated = true;
	}

      /* Copy the data */
      logpb_copy_from_log (thread_p, area, udata_size, &process_lsa, log_page_p);
      undo_data = area;
    }

  if (is_zipped)
    {
      log_unzip_ptr = log_zip_alloc (IO_PAGESIZE);
      if (log_unzip_ptr == NULL)
	{
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_get_undo_record");
	  scan = S_ERROR;
	  goto exit;
	}

      if (log_unzip (log_unzip_ptr, udata_size, (char *) undo_data))
	{
	  udata_size = (int) log_unzip_ptr->data_length;
	  undo_data = (char *) log_unzip_ptr->log_data;
	}
      else
	{
	  assert (false);
	  scan = S_ERROR;
	  goto exit;
	}
    }

  /* copy the record */
  recdes->type = *(INT16 *) (undo_data);
  recdes->length = udata_size - sizeof (recdes->type);
  if (recdes->area_size < 0 || recdes->area_size < (int) recdes->length)
    {
      /*
       * DOES NOT FIT
       * Give a hint to the user of the needed length. Hint is given as a
       * negative value
       */
      /* do not use unary minus because slot_p->record_length is unsigned */
      recdes->length *= -1;

      scan = S_DOESNT_FIT;
      goto exit;
    }

  memcpy (recdes->data, (char *) (undo_data) + sizeof (recdes->type), recdes->length);

exit:
  if (area_was_mallocated)
    {
      free (area);
    }
  if (log_unzip_ptr != NULL)
    {
      log_zip_free (log_unzip_ptr);
    }

  return scan;
}

/*
 * log_read_sysop_start_postpone () - read system op start postpone and its recovery data
 *
 * return                     : error code
 * thread_p (in)              : thread entry
 * log_lsa (in/out)           : log address
 * log_page (in/out)          : log page
 * with_undo_data (in)        : true to read undo data
 * sysop_start_postpone (out) : output system op start postpone log record
 * undo_buffer_size (in/out)  : size for undo data buffer
 * undo_buffer (in/out)       : undo data buffer
 * undo_size (out)            : output undo data size
 * undo_data (out)            : output undo data
 */
int
log_read_sysop_start_postpone (THREAD_ENTRY * thread_p, LOG_LSA * log_lsa, LOG_PAGE * log_page, bool with_undo_data,
			       LOG_REC_SYSOP_START_POSTPONE * sysop_start_postpone, int *undo_buffer_size,
			       char **undo_buffer, int *undo_size, char **undo_data)
{
  int error_code = NO_ERROR;

  assert (!log_lsa->is_null ());

  if (log_page->hdr.logical_pageid != log_lsa->pageid)
    {
      error_code = logpb_fetch_page (thread_p, log_lsa, LOG_CS_FORCE_USE, log_page);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}
    }

  assert (((LOG_RECORD_HEADER *) (log_page->area + log_lsa->offset))->type == LOG_SYSOP_START_POSTPONE);

  /* skip log record header */
  LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER), log_lsa, log_page);

  /* read sysop_start_postpone */
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (LOG_REC_SYSOP_START_POSTPONE), log_lsa, log_page);
  *sysop_start_postpone = *(LOG_REC_SYSOP_START_POSTPONE *) (log_page->area + log_lsa->offset);
  if (!with_undo_data
      || (sysop_start_postpone->sysop_end.type != LOG_SYSOP_END_LOGICAL_UNDO
	  && sysop_start_postpone->sysop_end.type != LOG_SYSOP_END_LOGICAL_MVCC_UNDO))
    {
      /* no undo */
      return NO_ERROR;
    }

  /* read undo data and size */
  assert (undo_buffer_size != NULL);
  assert (undo_buffer != NULL);
  assert (undo_size != NULL);
  assert (undo_data != NULL);

  if (sysop_start_postpone->sysop_end.type == LOG_SYSOP_END_LOGICAL_UNDO)
    {
      *undo_size = sysop_start_postpone->sysop_end.undo.length;
    }
  else
    {
      assert (sysop_start_postpone->sysop_end.type == LOG_SYSOP_END_LOGICAL_MVCC_UNDO);
      *undo_size = sysop_start_postpone->sysop_end.mvcc_undo.undo.length;
    }

  LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_REC_SYSOP_START_POSTPONE), log_lsa, log_page);
  if (log_lsa->offset + (*undo_size) < (int) LOGAREA_SIZE)
    {
      *undo_data = log_page->area + log_lsa->offset;
    }
  else
    {
      if (*undo_buffer_size == 0)
	{
	  *undo_buffer = (char *) db_private_alloc (thread_p, *undo_size);
	}
      else if (*undo_buffer_size < *undo_size)
	{
	  char *new_ptr = (char *) db_private_realloc (thread_p, *undo_buffer, *undo_size);
	  if (new_ptr == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, *undo_size);
	      return ER_OUT_OF_VIRTUAL_MEMORY;
	    }
	  *undo_buffer_size = *undo_size;
	  *undo_buffer = new_ptr;
	}
      *undo_data = *undo_buffer;
      logpb_copy_from_log (thread_p, *undo_data, *undo_size, log_lsa, log_page);
    }
  return NO_ERROR;
}

/*
 * log_get_log_group_commit_interval () - setup flush daemon period based on system parameter
 */
void
log_get_log_group_commit_interval (bool & is_timed_wait, cubthread::delta_time & period)
{
  is_timed_wait = true;

#if defined (SERVER_MODE)
  if (log_Flush_has_been_requested)
    {
      period = std::chrono::milliseconds (0);
      return;
    }
#endif /* SERVER_MODE */

  const int MAX_WAIT_TIME_MSEC = 1000;
  int log_group_commit_interval_msec = prm_get_integer_value (PRM_ID_LOG_GROUP_COMMIT_INTERVAL_MSECS);

  assert (log_group_commit_interval_msec >= 0);

  if (log_group_commit_interval_msec == 0)
    {
      period = std::chrono::milliseconds (MAX_WAIT_TIME_MSEC);
    }
  else
    {
      period = std::chrono::milliseconds (log_group_commit_interval_msec);
    }
}

/*
 * log_get_checkpoint_interval () - setup log checkpoint daemon period based on system parameter
 */
void
log_get_checkpoint_interval (bool & is_timed_wait, cubthread::delta_time & period)
{
  int log_checkpoint_interval_sec = prm_get_integer_value (PRM_ID_LOG_CHECKPOINT_INTERVAL_SECS);

  assert (log_checkpoint_interval_sec >= 0);

  if (log_checkpoint_interval_sec > 0)
    {
      // if log_checkpoint_interval_sec > 0 (zero) then loop for fixed interval
      is_timed_wait = true;
      period = std::chrono::seconds (log_checkpoint_interval_sec);
    }
  else
    {
      // infinite wait
      is_timed_wait = false;
    }
}

#if defined (SERVER_MODE)
/*
 * log_wakeup_remove_log_archive_daemon () - wakeup remove log archive daemon
 */
void
log_wakeup_remove_log_archive_daemon ()
{
  if (log_Remove_log_archive_daemon)
    {
      log_Remove_log_archive_daemon->wakeup ();
    }
}
#endif /* SERVER_MODE */

#if defined (SERVER_MODE)
/*
 * log_wakeup_checkpoint_daemon () - wakeup checkpoint daemon
 */
void
log_wakeup_checkpoint_daemon ()
{
  if (log_Checkpoint_daemon)
    {
      log_Checkpoint_daemon->wakeup ();
    }
}
#endif /* SERVER_MODE */

/*
 * log_wakeup_log_flush_daemon () - wakeup log flush daemon
 */
void
log_wakeup_log_flush_daemon ()
{
  if (log_is_log_flush_daemon_available ())
    {
#if defined (SERVER_MODE)
      log_Flush_has_been_requested = true;
      log_Flush_daemon->wakeup ();
#endif /* SERVER_MODE */
    }
}

/*
 * log_is_log_flush_daemon_available () - check if log flush daemon is available
 */
bool
log_is_log_flush_daemon_available ()
{
#if defined (SERVER_MODE)
  return log_Flush_daemon != NULL;
#else
  return false;
#endif
}

#if defined (SERVER_MODE)
/*
 * log_flush_daemon_get_stats () - get log flush daemon thread statistics into statsp
 */
void
log_flush_daemon_get_stats (UINT64 * statsp)
{
  if (log_Flush_daemon != NULL)
    {
      log_Flush_daemon->get_stats (statsp);
    }
}
#endif // SERVER_MODE

// *INDENT-OFF*
#if defined(SERVER_MODE)
static void
log_checkpoint_execute (cubthread::entry & thread_ref)
{
  if (!BO_IS_SERVER_RESTARTED ())
    {
      // wait for boot to finish
      return;
    }

  logpb_checkpoint (&thread_ref);
}
#endif /* SERVER_MODE */

#if defined(SERVER_MODE)
// class log_remove_log_archive_daemon_task
//
//  description:
//    remove archive logs daemon task
//
class log_remove_log_archive_daemon_task : public cubthread::entry_task
{
  private:
    using clock = std::chrono::system_clock;

    bool m_is_timed_wait;
    cubthread::delta_time m_period;
    std::chrono::milliseconds m_param_period;
    clock::time_point m_log_deleted_time;

    void compute_period ()
    {
      // fetch remove log archives interval
      int remove_log_archives_interval_sec = prm_get_integer_value (PRM_ID_REMOVE_LOG_ARCHIVES_INTERVAL);
      assert (remove_log_archives_interval_sec >= 0);

      // cache interval for later use
      m_param_period = std::chrono::milliseconds (remove_log_archives_interval_sec * 1000);

      if (m_param_period > std::chrono::milliseconds (0))
	{
	  m_is_timed_wait = true;
	  clock::time_point now = clock::now ();

	  // now - m_log_deleted_time: represents time elapsed since last log archive deletion
	  if ((now - m_log_deleted_time) > m_param_period)
	    {
	      m_period = m_param_period;
	    }
	  else
	    {
	      m_period = m_param_period - (now - m_log_deleted_time);
	    }
	}
      else
	{
	  // infinite wait
	  m_is_timed_wait = false;
	}
    }

  public:
    log_remove_log_archive_daemon_task ()
      : m_is_timed_wait (true)
      , m_period (0)
      , m_param_period (0)
      , m_log_deleted_time ()
    {
      // initialize period
      compute_period ();
    }

    void get_remove_log_archives_interval (bool & is_timed_wait, cubthread::delta_time & period)
    {
      period = m_period;
      is_timed_wait = m_is_timed_wait;
    }

    void execute (cubthread::entry & thread_ref) override
    {
      if (!BO_IS_SERVER_RESTARTED ())
	{
	  // wait for boot to finish
	  return;
	}

      // compute wait period based on configured interval
      compute_period ();

      if (m_is_timed_wait)
	{
	  if (m_period != m_param_period)
	    {
	      // do not delete logs. wait more time
	      return;
	    }
	  if (logpb_remove_archive_logs_exceed_limit (&thread_ref, 1) > 0)
	    {
	      // a log was deleted
	      m_log_deleted_time = clock::now ();
	    }
	}
      else
	{
	  // remove all unnecessary logs
	  logpb_remove_archive_logs_exceed_limit (&thread_ref, 0);
	}
    }
};
#endif /* SERVER_MODE */

#if defined(SERVER_MODE)
static void
log_clock_execute (cubthread::entry & thread_ref)
{
  if (!BO_IS_SERVER_RESTARTED ())
    {
      // wait for boot to finish
      return;
    }

  struct timeval now;
  gettimeofday (&now, NULL);

  log_Clock_msec = (now.tv_sec * 1000LL) + (now.tv_usec / 1000LL);
}
#endif /* SERVER_MODE */

#if defined(SERVER_MODE)
static void
log_check_ha_delay_info_execute (cubthread::entry &thread_ref)
{
  if (!BO_IS_SERVER_RESTARTED ())
    {
      // wait for boot to finish
      return;
    }

  time_t log_record_time = 0;
  int error_code;
  int delay_limit_in_secs;
  int acceptable_delay_in_secs;
  int curr_delay_in_secs;
  HA_SERVER_STATE server_state;

  csect_enter (&thread_ref, CSECT_HA_SERVER_STATE, INF_WAIT);

  server_state = css_ha_server_state ();

  if (server_state == HA_SERVER_STATE_ACTIVE || server_state == HA_SERVER_STATE_TO_BE_STANDBY)
    {
      css_unset_ha_repl_delayed ();
      perfmon_set_stat (&thread_ref, PSTAT_HA_REPL_DELAY, 0, true);

      log_append_ha_server_state (&thread_ref, server_state);

      csect_exit (&thread_ref, CSECT_HA_SERVER_STATE);
    }
  else
    {
      csect_exit (&thread_ref, CSECT_HA_SERVER_STATE);

      delay_limit_in_secs = prm_get_integer_value (PRM_ID_HA_DELAY_LIMIT_IN_SECS);
      acceptable_delay_in_secs = delay_limit_in_secs - prm_get_integer_value (PRM_ID_HA_DELAY_LIMIT_DELTA_IN_SECS);

      if (acceptable_delay_in_secs < 0)
	{
	  acceptable_delay_in_secs = 0;
	}

      error_code = catcls_get_apply_info_log_record_time (&thread_ref, &log_record_time);

      if (error_code == NO_ERROR && log_record_time > 0)
	{
	  curr_delay_in_secs = (int) (time (NULL) - log_record_time);
	  if (curr_delay_in_secs > 0)
	    {
	      curr_delay_in_secs -= HA_DELAY_ERR_CORRECTION;
	    }

	  if (delay_limit_in_secs > 0)
	    {
	      if (curr_delay_in_secs > delay_limit_in_secs)
		{
		  if (!css_is_ha_repl_delayed ())
		    {
		      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_HA_REPL_DELAY_DETECTED, 2,
			      curr_delay_in_secs, delay_limit_in_secs);

		      css_set_ha_repl_delayed ();
		    }
		}
	      else if (curr_delay_in_secs <= acceptable_delay_in_secs)
		{
		  if (css_is_ha_repl_delayed ())
		    {
		      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_HA_REPL_DELAY_RESOLVED, 2,
			      curr_delay_in_secs, acceptable_delay_in_secs);

		      css_unset_ha_repl_delayed ();
		    }
		}
	    }

	  perfmon_set_stat (&thread_ref, PSTAT_HA_REPL_DELAY, curr_delay_in_secs, true);
	}
    }
}
#endif /* SERVER_MODE */

#if defined(SERVER_MODE)
static void
log_flush_execute (cubthread::entry & thread_ref)
{
  if (!BO_IS_SERVER_RESTARTED () || !log_Flush_has_been_requested)
    {
      return;
    }

  // refresh log trace flush time
  thread_ref.event_stats.trace_log_flush_time = prm_get_integer_value (PRM_ID_LOG_TRACE_FLUSH_TIME_MSECS);

  LOG_CS_ENTER (&thread_ref);
  logpb_flush_pages_direct (&thread_ref);
  LOG_CS_EXIT (&thread_ref);

  log_Stat.gc_flush_count++;

  pthread_mutex_lock (&log_Gl.group_commit_info.gc_mutex);
  pthread_cond_broadcast (&log_Gl.group_commit_info.gc_cond);
  log_Flush_has_been_requested = false;
  pthread_mutex_unlock (&log_Gl.group_commit_info.gc_mutex);
}
#endif /* SERVER_MODE */

#if defined(SERVER_MODE)
/*
 * log_checkpoint_daemon_init () - initialize checkpoint daemon
 */
void
log_checkpoint_daemon_init ()
{
  assert (log_Checkpoint_daemon == NULL);

  cubthread::looper looper = cubthread::looper (log_get_checkpoint_interval);
  cubthread::entry_callable_task *daemon_task = new cubthread::entry_callable_task (log_checkpoint_execute);

  // create checkpoint daemon thread
  log_Checkpoint_daemon = cubthread::get_manager ()->create_daemon (looper, daemon_task, "log_checkpoint");
}
#endif /* SERVER_MODE */

#if defined(SERVER_MODE)
/*
 * log_remove_log_archive_daemon_init () - initialize remove log archive daemon
 */
void
log_remove_log_archive_daemon_init ()
{
  assert (log_Remove_log_archive_daemon == NULL);

  log_remove_log_archive_daemon_task *daemon_task = new log_remove_log_archive_daemon_task ();
  cubthread::period_function setup_period_function = std::bind (
      &log_remove_log_archive_daemon_task::get_remove_log_archives_interval,
      daemon_task,
      std::placeholders::_1,
      std::placeholders::_2);

  cubthread::looper looper = cubthread::looper (setup_period_function);

  // create log archive remover daemon thread
  log_Remove_log_archive_daemon = cubthread::get_manager ()->create_daemon (looper, daemon_task,
                                                                            "log_remove_log_archive");
}
#endif /* SERVER_MODE */

#if defined(SERVER_MODE)
/*
 * log_clock_daemon_init () - initialize log clock daemon
 */
void
log_clock_daemon_init ()
{
  assert (log_Clock_daemon == NULL);

  cubthread::looper looper = cubthread::looper (std::chrono::milliseconds (200));
  log_Clock_daemon =
    cubthread::get_manager ()->create_daemon (looper, new cubthread::entry_callable_task (log_clock_execute),
                                              "log_clock");
}
#endif /* SERVER_MODE */

#if defined(SERVER_MODE)
/*
 * log_check_ha_delay_info_daemon_init () - initialize check ha delay info daemon
 */
void
log_check_ha_delay_info_daemon_init ()
{
  bool do_supplemental_log = prm_get_integer_value (PRM_ID_SUPPLEMENTAL_LOG) > 0 ? true : false;

  if (HA_DISABLED () && !do_supplemental_log)
    {
      return;
    }

  assert (log_Check_ha_delay_info_daemon == NULL);

  cubthread::looper looper = cubthread::looper (std::chrono::seconds (1));
  cubthread::entry_callable_task *daemon_task = new cubthread::entry_callable_task (log_check_ha_delay_info_execute);

  log_Check_ha_delay_info_daemon = cubthread::get_manager ()->create_daemon (looper, daemon_task,
                                                                             "log_check_ha_delay_info");
}
#endif /* SERVER_MODE */

#if defined(SERVER_MODE)
/*
 * log_flush_daemon_init () - initialize log flush daemon
 */
void
log_flush_daemon_init ()
{
  assert (log_Flush_daemon == NULL);

  cubthread::looper looper = cubthread::looper (log_get_log_group_commit_interval);
  cubthread::entry_callable_task *daemon_task = new cubthread::entry_callable_task (log_flush_execute);

  log_Flush_daemon = cubthread::get_manager ()->create_daemon (looper, daemon_task, "log_flush");
}
#endif /* SERVER_MODE */

#if defined(SERVER_MODE)
/*
 * log_daemons_init () - initialize daemon threads
 */
static void
log_daemons_init ()
{
  log_remove_log_archive_daemon_init ();
  log_checkpoint_daemon_init ();
  log_check_ha_delay_info_daemon_init ();
  log_clock_daemon_init ();
  log_flush_daemon_init ();
}
#endif /* SERVER_MODE */

#if defined(SERVER_MODE)
/*
 * log_daemons_destroy () - destroy daemon threads
 */
static void
log_daemons_destroy ()
{
  cubthread::get_manager ()->destroy_daemon (log_Remove_log_archive_daemon);
  cubthread::get_manager ()->destroy_daemon (log_Checkpoint_daemon);
  cubthread::get_manager ()->destroy_daemon (log_Check_ha_delay_info_daemon);
  cubthread::get_manager ()->destroy_daemon (log_Clock_daemon);
  cubthread::get_manager ()->destroy_daemon (log_Flush_daemon);
}
#endif /* SERVER_MODE */
// *INDENT-ON*

/*
 * log_get_clock_msec () - get current system time in milliseconds.
 *   return cached value by log_Clock_daemon if SERVER_MODE is defined
 */
INT64
log_get_clock_msec (void)
{
#if defined (SERVER_MODE)
  if (log_Clock_msec > 0)
    {
      return log_Clock_msec;
    }
#endif /* SERVER_MODE */

  struct timeval now;
  gettimeofday (&now, NULL);

  return (now.tv_sec * 1000LL) + (now.tv_usec / 1000LL);
}

// *INDENT-OFF*
#if defined (SERVER_MODE)
static void
log_abort_task_execute (cubthread::entry &thread_ref, LOG_TDES &tdes)
{
  (void) log_abort_by_tdes (&thread_ref, &tdes);
}
#endif // SERVER_MODE
// *INDENT-ON*

void
log_update_global_btid_online_index_stats (THREAD_ENTRY * thread_p)
{
  LOG_TDES *tdes = LOG_FIND_TDES (LOG_FIND_THREAD_TRAN_INDEX (thread_p));
  int error_code = NO_ERROR;

  if (tdes == NULL)
    {
      return;
    }

  error_code =
    mht_map_no_key (thread_p, tdes->log_upd_stats.unique_stats_hash, logtb_tran_update_stats_online_index_rb, thread_p);

  if (error_code != NO_ERROR)
    {
      assert (false);
    }
}

/*
 * logtb_tran_update_stats_online_index_rb - Updates statistics during an online index when a transaction
 *                                           gets rollbacked.
 *
 * TODO: This can be easily optimized since it is slow. Try to find a better approach!
 */
static int
logtb_tran_update_stats_online_index_rb (THREAD_ENTRY * thread_p, void *data, void *args)
{
  /* This is called only during a rollback on a transaction that has updated an index which was under
   * online loading.
   */
  LOG_TRAN_BTID_UNIQUE_STATS *unique_stats = (LOG_TRAN_BTID_UNIQUE_STATS *) data;
  int error_code = NO_ERROR;
  OID class_oid;
#if !defined (NDEBUG)
  LOG_TDES *tdes = LOG_FIND_TDES (LOG_FIND_THREAD_TRAN_INDEX (thread_p));

  assert (LOG_ISTRAN_ABORTED (tdes));
#endif /* !NDEBUG */

  if (unique_stats->deleted)
    {
      /* ignore if deleted */
      return NO_ERROR;
    }

  OID_SET_NULL (&class_oid);

  error_code = btree_get_class_oid_of_unique_btid (thread_p, &unique_stats->btid, &class_oid);
  if (error_code != NO_ERROR)
    {
      assert (false);
      return error_code;
    }

  assert (!OID_ISNULL (&class_oid));

  if (!btree_is_btid_online_index (thread_p, &class_oid, &unique_stats->btid))
    {
      /* We can skip. */
      return NO_ERROR;
    }

  /* We can update the statistics. */
  error_code =
    logtb_update_global_unique_stats_by_delta (thread_p, &unique_stats->btid, unique_stats->tran_stats.num_oids,
					       unique_stats->tran_stats.num_nulls, unique_stats->tran_stats.num_keys,
					       false);

  return error_code;
}

static int
cdc_log_extract (THREAD_ENTRY * thread_p, LOG_LSA * process_lsa, CDC_LOGINFO_ENTRY * log_info_entry)
{
  LOG_LSA cur_log_rec_lsa = LSA_INITIALIZER;
  LOG_LSA next_log_rec_lsa = LSA_INITIALIZER;

  LOG_PAGE *log_page_p = NULL;
  char log_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT];

  LOG_RECORD_HEADER *log_rec_header = NULL;
  LOG_RECORD_HEADER *nx_rec_header = NULL;

  char *tran_user = NULL;
  int trid;
  int tmpbuf_index = 0;

  int error = NO_ERROR;

  LOG_RECTYPE log_type;
  LOG_ZIP *supp_zip = NULL;

  char *supplement_data = NULL;

  RECDES supp_recdes = RECDES_INITIALIZER;
  RECDES undo_recdes = RECDES_INITIALIZER;
  RECDES redo_recdes = RECDES_INITIALIZER;

  LSA_COPY (&cur_log_rec_lsa, process_lsa);

  log_page_p = (LOG_PAGE *) PTR_ALIGN (log_pgbuf, MAX_ALIGNMENT);

  /*fetch log page */

  CDC_GET_TEMP_LOGPAGE (thread_p, process_lsa, log_page_p);
  tmpbuf_index = process_lsa->pageid % 2;

  log_rec_header = LOG_GET_LOG_RECORD_HEADER (log_page_p, process_lsa);

  if (log_rec_header->type == LOG_END_OF_LOG || LSA_ISNULL (&log_rec_header->forw_lsa))
    {
      CDC_UPDATE_TEMP_LOGPAGE (thread_p, process_lsa, log_page_p);

      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_CDC_NULL_EXTRACTION_LSA, 0);
      error = ER_CDC_NULL_EXTRACTION_LSA;
      goto error;
    }

  log_type = log_rec_header->type;
  trid = log_rec_header->trid;

  LSA_COPY (&next_log_rec_lsa, &log_rec_header->forw_lsa);

  LOG_READ_ADD_ALIGN (thread_p, sizeof (*log_rec_header), process_lsa, log_page_p);

  switch (log_type)
    {
    case LOG_COMMIT:
    case LOG_ABORT:
      LOG_REC_DONETIME * donetime;

      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*donetime), process_lsa, log_page_p);

      CDC_CHECK_TEMP_LOGPAGE (process_lsa, &tmpbuf_index, log_page_p);

      if (cdc_Gl.producer.tran_ignore.count (trid) != 0)
	{
	  cdc_Gl.producer.tran_ignore.erase (trid);
	  break;
	}

      donetime = (LOG_REC_DONETIME *) (log_page_p->area + process_lsa->offset);

      if (cdc_Gl.producer.tran_user.count (trid) == 0)
	{
	  goto end;
	}
      else
	{
          /* *INDENT-OFF* */
          tran_user = cdc_Gl.producer.tran_user.at (trid);
          /* *INDENT-ON* */
	}

      if (!cdc_is_filtered_user (tran_user))
	{
	  break;
	}

      if ((error =
	   cdc_make_dcl_loginfo (donetime->at_time, trid, tran_user, log_type,
				 log_info_entry)) != ER_CDC_LOGINFO_ENTRY_GENERATED)
	{
	  goto error;
	}

      free_and_init (tran_user);

      cdc_Gl.producer.tran_user.erase (trid);

      break;

    case LOG_DUMMY_HA_SERVER_STATE:
      LOG_REC_HA_SERVER_STATE * ha_dummy;

      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*ha_dummy), process_lsa, log_page_p);

      CDC_CHECK_TEMP_LOGPAGE (process_lsa, &tmpbuf_index, log_page_p);

      ha_dummy = (LOG_REC_HA_SERVER_STATE *) (log_page_p->area + process_lsa->offset);

      if ((error = cdc_make_timer_loginfo (ha_dummy->at_time, trid, NULL, log_info_entry)) == ER_OUT_OF_VIRTUAL_MEMORY)
	{
	  goto error;
	}

      break;

    case LOG_SUPPLEMENTAL_INFO:
      {
	/*supplemental log info types : time, tran_user, undo image */
	LOG_REC_SUPPLEMENT *supplement;
	int supplement_length;
	SUPPLEMENT_REC_TYPE rec_type = LOG_SUPPLEMENT_LARGER_REC_TYPE;

	bool is_zip_supplement = false;
	bool is_unzip_supplement = false;

	OID classoid;

	LOG_LSA undo_lsa, redo_lsa;

	LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*supplement), process_lsa, log_page_p);

	CDC_CHECK_TEMP_LOGPAGE (process_lsa, &tmpbuf_index, log_page_p);

	if (cdc_Gl.producer.tran_ignore.count (trid) != 0)
	  {
	    goto end;
	  }

	supplement = (LOG_REC_SUPPLEMENT *) (log_page_p->area + process_lsa->offset);
	supplement_length = supplement->length;
	rec_type = supplement->rec_type;

	LOG_READ_ADD_ALIGN (thread_p, sizeof (*supplement), process_lsa, log_page_p);

	CDC_CHECK_TEMP_LOGPAGE (process_lsa, &tmpbuf_index, log_page_p);

	if (cdc_get_undo_record (thread_p, log_page_p, cur_log_rec_lsa, &supp_recdes) != S_SUCCESS)
	  {
	    error = ER_FAILED;
	    goto error;
	  }

	supplement_length = sizeof (supp_recdes.type) + supp_recdes.length;
	supplement_data = (char *) malloc (supplement_length);
	if (supplement_data == NULL)
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, supplement_length);
	    error = ER_OUT_OF_VIRTUAL_MEMORY;
	    goto error;
	  }

	memcpy (supplement_data, &supp_recdes.type, sizeof (supp_recdes.type));
	memcpy (supplement_data + sizeof (supp_recdes.type), supp_recdes.data, supp_recdes.length);

	free_and_init (supp_recdes.data);

	CDC_UPDATE_TEMP_LOGPAGE (thread_p, process_lsa, log_page_p);

	if (rec_type != LOG_SUPPLEMENT_TRAN_USER)
	  {
	    if (cdc_Gl.producer.tran_user.count (trid) == 0)
	      {
		if ((error = cdc_find_user (thread_p, cur_log_rec_lsa, trid, &tran_user)) == NO_ERROR)
		  {
		    cdc_Gl.producer.tran_user.insert (std::make_pair (trid, tran_user));
		  }
		else if (error == ER_CDC_IGNORE_TRANSACTION)
		  {
		    /* can not find user. It meets abort log. So, ignore the logs from this transaction */
		    cdc_Gl.producer.tran_ignore.insert (std::make_pair (trid, 1));

		    goto end;
		  }
		else
		  {
		    /* can not find user */
		    goto end;
		  }
	      }
	    else
	      {
		tran_user = cdc_Gl.producer.tran_user.at (trid);
	      }

	    if (!cdc_is_filtered_user (tran_user))
	      {
		goto end;
	      }
	  }

	switch (rec_type)
	  {
	  case LOG_SUPPLEMENT_TRAN_USER:
	    if (cdc_Gl.producer.tran_user.count (trid) != 0)
	      {
		break;
	      }
	    else
	      {
		tran_user = (char *) malloc (supplement_length + 1);
		if (tran_user == NULL)
		  {
		    goto error;
		  }

		// |string|, |smart pointer|, strdup
		memcpy (tran_user, supplement_data, supplement_length);
		tran_user[supplement_length] = '\0';

		cdc_Gl.producer.tran_user.insert (std::make_pair (trid, tran_user));

		break;
	      }
	  case LOG_SUPPLEMENT_INSERT:
	  case LOG_SUPPLEMENT_TRIGGER_INSERT:
	    memcpy (&classoid, supplement_data, sizeof (OID));

	    if (!cdc_is_filtered_class (classoid) || oid_is_system_class (&classoid))
	      {
		error = ER_CDC_IGNORE_LOG_INFO;
		cdc_log ("cdc_log_extract : Skip producing log info for an invalid class (%d|%d|%d)",
			 OID_AS_ARGS (&classoid));
		goto end;
	      }

	    memcpy (&redo_lsa, supplement_data + sizeof (OID), sizeof (LOG_LSA));

	    if (cdc_get_recdes (thread_p, NULL, NULL, &redo_lsa, &redo_recdes, false) != NO_ERROR)
	      {
		goto error;
	      }

	    error =
	      cdc_make_dml_loginfo (thread_p, trid, tran_user,
				    rec_type == LOG_SUPPLEMENT_INSERT ? CDC_INSERT : CDC_TRIGGER_INSERT, classoid, NULL,
				    &redo_recdes, log_info_entry, false);

	    if (error != ER_CDC_LOGINFO_ENTRY_GENERATED)
	      {
		goto error;
	      }

	    break;
	  case LOG_SUPPLEMENT_UPDATE:
	  case LOG_SUPPLEMENT_TRIGGER_UPDATE:
	    memcpy (&classoid, supplement_data, sizeof (OID));

	    if (!cdc_is_filtered_class (classoid) || oid_is_system_class (&classoid))
	      {
		error = ER_CDC_IGNORE_LOG_INFO;
		cdc_log ("cdc_log_extract : Skip producing log info for an invalid class (%d|%d|%d)",
			 OID_AS_ARGS (&classoid));
		goto end;
	      }

	    memcpy (&undo_lsa, supplement_data + sizeof (OID), sizeof (LOG_LSA));
	    memcpy (&redo_lsa, supplement_data + sizeof (OID) + sizeof (LOG_LSA), sizeof (LOG_LSA));

	    if (cdc_get_recdes (thread_p, &undo_lsa, &undo_recdes, &redo_lsa, &redo_recdes, false) != NO_ERROR)
	      {
		goto error;
	      }

	    if (undo_recdes.type == REC_ASSIGN_ADDRESS)
	      {
		/* This occurs when series of logs are appended like
		 * INSERT record for reserve OID (REC_ASSIGN_ADDRESS) then UPDATE to some record.
		 * And this is a sequence for INSERT a record with OID reservation.
		 * undo record with REC_ASSIGN_ADDRESS type has no undo image to extract, so this will be treated as INSERT
		 * CUBRID engine used to do INSERT a record like this way,
		 * for instance CREATE a class or INSERT a record by trigger execution */

		assert (rec_type == LOG_SUPPLEMENT_TRIGGER_UPDATE);

		error =
		  cdc_make_dml_loginfo (thread_p, trid, tran_user, CDC_TRIGGER_INSERT, classoid, NULL, &redo_recdes,
					log_info_entry, false);
	      }
	    else
	      {
		error =
		  cdc_make_dml_loginfo (thread_p, trid, tran_user,
					rec_type == LOG_SUPPLEMENT_UPDATE ? CDC_UPDATE : CDC_TRIGGER_UPDATE, classoid,
					&undo_recdes, &redo_recdes, log_info_entry, false);
	      }

	    if (error == ER_CDC_IGNORE_LOG_INFO || error == ER_CDC_IGNORE_LOG_INFO_INTERNAL)
	      {
		goto end;
	      }

	    if (error != ER_CDC_LOGINFO_ENTRY_GENERATED)
	      {
		goto error;
	      }

	    break;
	  case LOG_SUPPLEMENT_DELETE:
	  case LOG_SUPPLEMENT_TRIGGER_DELETE:
	    memcpy (&classoid, supplement_data, sizeof (OID));

	    if (!cdc_is_filtered_class (classoid) || oid_is_system_class (&classoid))
	      {
		error = ER_CDC_IGNORE_LOG_INFO;
		cdc_log ("cdc_log_extract : Skip producing log info for an invalid class (%d|%d|%d)",
			 OID_AS_ARGS (&classoid));
		goto end;
	      }

	    memcpy (&undo_lsa, supplement_data + sizeof (OID), sizeof (LOG_LSA));

	    if (cdc_get_recdes (thread_p, &undo_lsa, &undo_recdes, NULL, NULL, false) != NO_ERROR)
	      {
		goto error;
	      }

	    error =
	      cdc_make_dml_loginfo (thread_p, trid, tran_user,
				    rec_type == LOG_SUPPLEMENT_DELETE ? CDC_DELETE : CDC_TRIGGER_DELETE, classoid,
				    &undo_recdes, NULL, log_info_entry, false);

	    if (error != ER_CDC_LOGINFO_ENTRY_GENERATED)
	      {
		goto error;
	      }

	    break;
	  case LOG_SUPPLEMENT_DDL:
	    error = cdc_make_ddl_loginfo (supplement_data, trid, tran_user, log_info_entry);

	    if (error == ER_CDC_IGNORE_LOG_INFO)
	      {
		goto end;
	      }
	    else if (error != ER_CDC_LOGINFO_ENTRY_GENERATED)
	      {
		goto error;
	      }

	    break;

	  default:
	    break;
	  }


	break;
      }

    default:
      break;
    }

end:
  if (supplement_data != NULL)
    {
      free_and_init (supplement_data);
    }

  if (undo_recdes.data != NULL)
    {
      free_and_init (undo_recdes.data);
    }

  if (redo_recdes.data != NULL)
    {
      free_and_init (redo_recdes.data);
    }

  LSA_COPY (process_lsa, &next_log_rec_lsa);

  return error;

error:
  if (supplement_data != NULL)
    {
      free_and_init (supplement_data);
    }

  if (undo_recdes.data != NULL)
    {
      free_and_init (undo_recdes.data);
    }

  if (redo_recdes.data != NULL)
    {
      free_and_init (redo_recdes.data);
    }

  LSA_COPY (process_lsa, &cur_log_rec_lsa);

  return error;
}

static void
cdc_loginfo_producer_execute (cubthread::entry & thread_ref)
{
  LOG_LSA cur_log_rec_lsa = LSA_INITIALIZER;
  LOG_LSA process_lsa = LSA_INITIALIZER;
  LOG_LSA nxio_lsa = LSA_INITIALIZER;

  CDC_LOGINFO_ENTRY log_info_entry;

  THREAD_ENTRY *thread_p = &thread_ref;
  thread_p->is_cdc_daemon = true;

  int error = NO_ERROR;

  cdc_Gl.producer.state = CDC_PRODUCER_STATE_RUN;

  while (cdc_Gl.producer.request != CDC_REQUEST_PRODUCER_TO_BE_DEAD)
    {
      if (cdc_Gl.producer.request == CDC_REQUEST_PRODUCER_TO_WAIT)
	{
	  cdc_log ("cdc_loginfo_producer_execute : cdc_Gl.producer.state is in CDC_PRODUCER_STATE_WAIT ");

	  cdc_Gl.producer.state = CDC_PRODUCER_STATE_WAIT;

	  pthread_mutex_lock (&cdc_Gl.producer.lock);
	  pthread_cond_wait (&cdc_Gl.producer.wait_cond, &cdc_Gl.producer.lock);
	  pthread_mutex_unlock (&cdc_Gl.producer.lock);

	  cdc_Gl.producer.state = CDC_PRODUCER_STATE_RUN;

	  cdc_log ("cdc_loginfo_producer_execute : cdc_Gl.producer.state is in CDC_PRODUCER_STATE_RUN ");
	  continue;
	}

      if (cdc_Gl.producer.produced_queue_size >= MAX_CDC_LOGINFO_QUEUE_SIZE || cdc_Gl.loginfo_queue->is_full ())
	{
	  cdc_log ("cdc_loginfo_producer_execute : produced queue size is over the limit");

	  cdc_Gl.producer.state = CDC_PRODUCER_STATE_WAIT;

	  cdc_pause_consumer ();

	  pthread_mutex_lock (&cdc_Gl.producer.lock);
	  pthread_cond_wait (&cdc_Gl.producer.wait_cond, &cdc_Gl.producer.lock);
	  pthread_mutex_unlock (&cdc_Gl.producer.lock);

	  cdc_Gl.producer.state = CDC_PRODUCER_STATE_RUN;

	  cdc_log ("cdc_loginfo_producer_execute : cdc_Gl.producer.state is in CDC_PRODUCER_STATE_RUN ");

	  cdc_Gl.producer.produced_queue_size -= cdc_Gl.consumer.consumed_queue_size;
	  cdc_Gl.consumer.consumed_queue_size = 0;

	  cdc_wakeup_consumer ();

	  continue;
	}

      nxio_lsa = log_Gl.append.get_nxio_lsa ();

      if (LSA_GE (&cdc_Gl.producer.next_extraction_lsa, &nxio_lsa))
	{
	  /* LOG_HA_DUMMY_SERVER_STATUS is appended every 1 seconds and flushed.
	   * So it is expected to be woken up by looper within period of looper */

	  cdc_log
	    ("cdc_loginfo_producer_execute : next_extraction_lsa (%lld | %d)  is greater or equal than nxio_lsa (%lld | %d)",
	     LSA_AS_ARGS (&cdc_Gl.producer.next_extraction_lsa), LSA_AS_ARGS (&nxio_lsa));

	  sleep (1);

	  continue;
	}

      log_info_entry.length = 0;
      LSA_SET_NULL (&log_info_entry.next_lsa);
      log_info_entry.log_info = NULL;

      LSA_COPY (&cur_log_rec_lsa, &cdc_Gl.producer.next_extraction_lsa);
      LSA_COPY (&process_lsa, &cur_log_rec_lsa);

      error = cdc_log_extract (thread_p, &process_lsa, &log_info_entry);
      if (!(error == NO_ERROR || error == ER_CDC_LOGINFO_ENTRY_GENERATED))
	{
	  cdc_log
	    ("cdc_loginfo_producer_execute : cdc_log_extract() error(%d) is returned at extracting log from lsa (%lld | %d)",
	     error, LSA_AS_ARGS (&cur_log_rec_lsa));

	  if (!CDC_IS_IGNORE_LOGINFO_ERROR (error))
	    {
	      continue;
	    }
	}

      assert (!LSA_ISNULL (&process_lsa));

      /* when refined log info is queued, update cdc_Gl */
      if (error == ER_CDC_LOGINFO_ENTRY_GENERATED)
	{
	  CDC_LOGINFO_ENTRY *tmp = (CDC_LOGINFO_ENTRY *) malloc (sizeof (CDC_LOGINFO_ENTRY));
	  if (tmp == NULL)
	    {
	      cdc_log
		("cdc_loginfo_producer_execute : failed to allocate memory for log info entry of LOG_LSA (%lld | %d)",
		 LSA_AS_ARGS (&process_lsa));

	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (CDC_LOGINFO_ENTRY));
	      error = ER_OUT_OF_VIRTUAL_MEMORY;
	      continue;
	    }

	  tmp->length = log_info_entry.length;
	  tmp->log_info = log_info_entry.log_info;
	  LSA_COPY (&tmp->next_lsa, &process_lsa);

	  if (cdc_Gl.is_queue_reinitialized)
	    {
	      free_and_init (tmp->log_info);
	      free_and_init (tmp);

	      cdc_Gl.is_queue_reinitialized = false;

	      continue;
	    }

          /* *INDENT-OFF* */
	  cdc_Gl.loginfo_queue->produce (tmp);
          /* *INDENT-ON* */
	  cdc_Gl.producer.produced_queue_size += tmp->length;

	  LSA_COPY (&cdc_Gl.last_loginfo_queue_lsa, &cur_log_rec_lsa);

	  cdc_log ("cdc_loginfo_producer_execute : log info is produced on LOG_LSA (%lld | %d)",
		   LSA_AS_ARGS (&process_lsa));
	}

      LSA_COPY (&cdc_Gl.producer.next_extraction_lsa, &process_lsa);
    }

  cdc_Gl.producer.state = CDC_PRODUCER_STATE_DEAD;

end:

  thread_p->is_cdc_daemon = false;

  return;

error:

  thread_p->is_cdc_daemon = false;
  return;
}

static int
cdc_check_log_page (THREAD_ENTRY * thread_p, LOG_PAGE * log_page_p, LOG_LSA * lsa)
{
  if (log_page_p->hdr.logical_pageid != lsa->pageid)
    {
      if (logpb_fetch_page (thread_p, lsa, LOG_CS_SAFE_READER, log_page_p) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }

  return NO_ERROR;
}

SCAN_CODE
cdc_get_undo_record (THREAD_ENTRY * thread_p, LOG_PAGE * log_page_p, LOG_LSA lsa, RECDES * undo_recdes)
{
  SCAN_CODE scan_code = S_SUCCESS;

  if (cdc_check_log_page (thread_p, log_page_p, &lsa) != NO_ERROR)
    {
      return S_ERROR;
    }

  undo_recdes->data = (char *) malloc (ONE_K);
  if (undo_recdes->data == NULL)
    {
      cdc_log ("cdc_get_undo_record : failed to allocate memory while reading from undo log lsa:(%lld | %d)",
	       LSA_AS_ARGS (&lsa));
      return S_ERROR;
    }

  undo_recdes->area_size = ONE_K;

  scan_code = log_get_undo_record (thread_p, log_page_p, lsa, undo_recdes);
  if (scan_code != S_SUCCESS)
    {
      if (scan_code == S_DOESNT_FIT)
	{
	  undo_recdes->data = (char *) realloc (undo_recdes->data, (size_t) (-undo_recdes->length));	//realloc error 처리
	  undo_recdes->area_size = (size_t) (-undo_recdes->length);

	  if (cdc_check_log_page (thread_p, log_page_p, &lsa) != NO_ERROR)
	    {
	      return S_ERROR;
	    }

	  scan_code = log_get_undo_record (thread_p, log_page_p, lsa, undo_recdes);
	  if (scan_code != S_SUCCESS)
	    {
	      cdc_log ("cdc_get_undo_record : failed to allocate memory for undo record at lsa (%lld | %d)",
		       LSA_AS_ARGS (&lsa));
	      return scan_code;
	    }
	}
      else
	{
	  return scan_code;
	}
    }

  cdc_log ("cdc_get_undo_record : success to get undo record of lsa(%lld | %d)", LSA_AS_ARGS (&lsa));
  return scan_code;
}

/*
 * cdc_log_read_advance_and_preserve_if_needed () - Fetch the next log page and preserve the existing one,
 *                                                  if the (lsa.offset + size) exceeds the log page size
 *
 * return                     : error code
 * thread_p (in)              : thread entry
 * size (in)                  : size to read in log page
 * lsa (in/out)               : log address to read
 * log_page_p (in/out)        : fetch the next log page if needed
 * preserved  (in/out)        : preserve existing log page if needed
 */
static int
cdc_log_read_advance_and_preserve_if_needed (THREAD_ENTRY * thread_p, size_t size, LOG_LSA * lsa, LOG_PAGE * log_page_p,
					     LOG_PAGE * preserved)
{
  if (lsa->offset + size >= (int) LOGAREA_SIZE)
    {
      /* Before fetching the next page, current log page is required to be preserved */
      memcpy (preserved, log_page_p, IO_MAX_PAGE_SIZE);

      lsa->pageid++;

      if (logpb_fetch_page (thread_p, lsa, LOG_CS_FORCE_USE, log_page_p) != NO_ERROR)
	{
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			     "log page fetch in cdc_log_read_advance_and_preserve_if_needed ()");
	  return ER_FAILED;
	}

      lsa->offset = 0;
    }

  return NO_ERROR;
}

int
cdc_get_recdes (THREAD_ENTRY * thread_p, LOG_LSA * undo_lsa, RECDES * undo_recdes, LOG_LSA * redo_lsa,
		RECDES * redo_recdes, bool is_flashback)
{
  LOG_RECORD_HEADER *log_rec_hdr = NULL;
  int tmpbuf_index;

  char *log_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT];
  char *preserved_log_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT];
  LOG_PAGE *log_page_p = NULL;	// log_page for process_lsa : when process_lsa is advanced, then next log_page can be fetched into this buffer
  LOG_PAGE *preserved_log_page_p = NULL;	// log_page for redo/undo lsa

  LOG_LSA process_lsa = LSA_INITIALIZER;
  LOG_LSA prev_lsa = LSA_INITIALIZER;

  LOG_RECTYPE log_type;

  int is_zipped_redo = false;
  int is_unzipped_redo = false;
  LOG_ZIP *redo_zip_ptr = NULL;

  bool is_diff = false;

  LOG_RCVINDEX rcvindex;
  int redo_length;
  int undo_length;
  char *redo_data = NULL;
  char *undo_data = NULL;

  bool is_redo_alloced = false;

  RECDES tmp_undo_recdes = RECDES_INITIALIZER;

  SCAN_CODE scan_code = S_SUCCESS;

  int error_code = NO_ERROR;

  log_page_p = (LOG_PAGE *) PTR_ALIGN (log_pgbuf, MAX_ALIGNMENT);
  preserved_log_page_p = (LOG_PAGE *) PTR_ALIGN (preserved_log_pgbuf, MAX_ALIGNMENT);

  /* Get UNDO RECDES from undo lsa */

  /* Because it is unable to know exact size of data (recdes.data), can not use log_get_undo_record. 
   * In order to use log_get_undo_record(), memory pool for recdes (assign_recdes_to_area()) is required just as scan cache where log_get_undo_record() is called. */

  if (undo_lsa != NULL)
    {
      tmpbuf_index = undo_lsa->pageid % 2;
      if (cdc_Gl.producer.temp_logbuf[tmpbuf_index].log_page_p->hdr.logical_pageid == undo_lsa->pageid && !is_flashback)
	{
	  memcpy (log_page_p, cdc_Gl.producer.temp_logbuf[tmpbuf_index].log_page_p, IO_MAX_PAGE_SIZE);
	}
      else
	{
	  if ((error_code = logpb_fetch_page (thread_p, undo_lsa, LOG_CS_SAFE_READER, log_page_p)) != NO_ERROR)
	    {
	      goto error;
	    }
	}

      LSA_COPY (&process_lsa, undo_lsa);

      log_rec_hdr = LOG_GET_LOG_RECORD_HEADER (log_page_p, &process_lsa);

      LSA_COPY (&prev_lsa, &log_rec_hdr->prev_tranlsa);
      log_type = log_rec_hdr->type;

      LOG_READ_ADD_ALIGN (thread_p, sizeof (*log_rec_hdr), &process_lsa, log_page_p);

      cdc_log ("cdc_get_recdes : reading from undo log lsa:(%lld | %d), undo log record type:%s",
	       LSA_AS_ARGS (undo_lsa), log_to_string (log_type));

      switch (log_type)
	{
	case LOG_SUPPLEMENTAL_INFO:
	  {
	    scan_code = cdc_get_undo_record (thread_p, log_page_p, *undo_lsa, undo_recdes);
	    if (scan_code != S_SUCCESS)
	      {
		error_code = ER_FAILED;
		goto error;
	      }

	    break;
	  }
	case LOG_MVCC_DIFF_UNDOREDO_DATA:
	case LOG_MVCC_UNDOREDO_DATA:
	  {
	    LOG_REC_MVCC_UNDOREDO *mvcc_undoredo = NULL;

	    if ((error_code =
		 cdc_log_read_advance_and_preserve_if_needed (thread_p, sizeof (*mvcc_undoredo), &process_lsa,
							      log_page_p, preserved_log_page_p)) != NO_ERROR)
	      {
		goto error;
	      }

	    mvcc_undoredo = (LOG_REC_MVCC_UNDOREDO *) (log_page_p->area + process_lsa.offset);
	    rcvindex = mvcc_undoredo->undoredo.data.rcvindex;

	    log_page_p = process_lsa.pageid == undo_lsa->pageid ? log_page_p : preserved_log_page_p;

	    if (rcvindex == RVHF_MVCC_DELETE_MODIFY_HOME || rcvindex == RVHF_UPDATE_NOTIFY_VACUUM)
	      {
		scan_code = cdc_get_undo_record (thread_p, log_page_p, *undo_lsa, undo_recdes);
		if (scan_code != S_SUCCESS)
		  {
		    error_code = ER_FAILED;
		    goto error;
		  }

	      }

	    break;
	  }
	case LOG_DIFF_UNDOREDO_DATA:
	case LOG_UNDOREDO_DATA:
	  {
	    LOG_REC_UNDOREDO *undoredo = NULL;

	    if ((error_code =
		 cdc_log_read_advance_and_preserve_if_needed (thread_p, sizeof (*undoredo), &process_lsa, log_page_p,
							      preserved_log_page_p)) != NO_ERROR)
	      {
		goto error;
	      }

	    undoredo = (LOG_REC_UNDOREDO *) (log_page_p->area + process_lsa.offset);
	    rcvindex = undoredo->data.rcvindex;

	    /* if the next log page is fetched into log_page_p, then use the preserved_log_page_p */
	    log_page_p = process_lsa.pageid == undo_lsa->pageid ? log_page_p : preserved_log_page_p;

	    if (rcvindex == RVHF_DELETE || rcvindex == RVHF_UPDATE)
	      {
		scan_code = cdc_get_undo_record (thread_p, log_page_p, *undo_lsa, undo_recdes);
		if (scan_code != S_SUCCESS)
		  {
		    error_code = ER_FAILED;
		    goto error;
		  }
	      }
	    else if (rcvindex == RVOVF_CHANGE_LINK || rcvindex == RVOVF_NEWPAGE_LINK)
	      {
		/* GET OVF UNDO IMAGE */
		if ((error_code =
		     cdc_get_overflow_recdes (thread_p, log_page_p, undo_recdes, *undo_lsa, rcvindex,
					      false)) != NO_ERROR)
		  {
		    goto error;
		  }

	      }

	    break;
	  }
	case LOG_UNDO_DATA:
	  {
	    LOG_REC_UNDO *undo = NULL;

	    if ((error_code =
		 cdc_log_read_advance_and_preserve_if_needed (thread_p, sizeof (*undo), &process_lsa, log_page_p,
							      preserved_log_page_p)) != NO_ERROR)
	      {
		goto error;
	      }

	    undo = (LOG_REC_UNDO *) (log_page_p->area + process_lsa.offset);
	    rcvindex = undo->data.rcvindex;

	    /* if the next log page is fetched into log_page_p, then use the preserved_log_page_p */
	    log_page_p = process_lsa.pageid == undo_lsa->pageid ? log_page_p : preserved_log_page_p;

	    if (rcvindex == RVOVF_PAGE_UPDATE)
	      {
		/* GET OVF UNDO IMAGE */
		if ((error_code =
		     cdc_get_overflow_recdes (thread_p, log_page_p, undo_recdes, *undo_lsa, rcvindex,
					      false)) != NO_ERROR)
		  {
		    goto error;
		  }

	      }
	    else if (rcvindex == RVHF_MVCC_UPDATE_OVERFLOW)
	      {
		scan_code = cdc_get_undo_record (thread_p, log_page_p, *undo_lsa, undo_recdes);
		if (scan_code != S_SUCCESS)
		  {
		    error_code = ER_FAILED;

		    goto error;
		  }
	      }

	    break;
	  }

	case LOG_REDO_DATA:
	  {
	    LOG_REC_REDO *redo = NULL;
	    LOG_RCVINDEX rcvindex;

	    if ((error_code =
		 cdc_log_read_advance_and_preserve_if_needed (thread_p, sizeof (*redo), &process_lsa, log_page_p,
							      preserved_log_page_p)) != NO_ERROR)
	      {
		goto error;
	      }

	    redo = (LOG_REC_REDO *) (log_page_p->area + process_lsa.offset);
	    rcvindex = redo->data.rcvindex;

	    if (rcvindex == RVOVF_PAGE_UPDATE)
	      {
		/* if the next log page is fetched into log_page_p, then use the preserved_log_page_p */
		log_page_p = process_lsa.pageid == undo_lsa->pageid ? log_page_p : preserved_log_page_p;

		/* GET OVF UNDO IMAGE */
		if ((error_code =
		     cdc_get_overflow_recdes (thread_p, log_page_p, undo_recdes, *undo_lsa, rcvindex,
					      false)) != NO_ERROR)
		  {
		    goto error;
		  }
	      }

	    break;
	  }

	default:
	  break;
	}
    }

/* Get REDO RECDES from redo lsa */
  if (redo_lsa != NULL)
    {
      tmpbuf_index = redo_lsa->pageid % 2;

      if (LSA_ISNULL (&process_lsa) && process_lsa.pageid == redo_lsa->pageid)
	{
	  /* if undo_lsa != NULL and current log_page_p can be reusable */
	  assert (log_page_p != NULL);
	}
      else
	{
	  if (cdc_Gl.producer.temp_logbuf[tmpbuf_index].log_page_p->hdr.logical_pageid == redo_lsa->pageid
	      && !is_flashback)
	    {
	      memcpy (log_page_p, cdc_Gl.producer.temp_logbuf[tmpbuf_index].log_page_p, IO_MAX_PAGE_SIZE);
	    }
	  else
	    {
	      if (logpb_fetch_page (thread_p, redo_lsa, LOG_CS_SAFE_READER, log_page_p) != NO_ERROR)
		{
		  error_code = ER_FAILED;
		  goto error;
		}
	    }
	}

      LSA_COPY (&process_lsa, redo_lsa);

      log_rec_hdr = LOG_GET_LOG_RECORD_HEADER (log_page_p, &process_lsa);

      log_type = log_rec_hdr->type;
      LSA_COPY (&prev_lsa, &log_rec_hdr->prev_tranlsa);

      LOG_READ_ADD_ALIGN (thread_p, sizeof (*log_rec_hdr), &process_lsa, log_page_p);

      cdc_log ("cdc_get_recdes : reading from redo log lsa:(%lld | %d), redo log record type:%s",
	       LSA_AS_ARGS (redo_lsa), log_to_string (log_type));

      switch (log_type)
	{
	case LOG_MVCC_DIFF_UNDOREDO_DATA:
	case LOG_MVCC_UNDOREDO_DATA:
	  {
	    LOG_REC_MVCC_UNDOREDO *mvcc_undoredo = NULL;
	    LOG_RCVINDEX rcvindex;

	    if ((error_code =
		 cdc_log_read_advance_and_preserve_if_needed (thread_p, sizeof (*mvcc_undoredo), &process_lsa,
							      log_page_p, preserved_log_page_p)) != NO_ERROR)
	      {
		goto error;
	      }

	    mvcc_undoredo = (LOG_REC_MVCC_UNDOREDO *) (log_page_p->area + process_lsa.offset);
	    rcvindex = mvcc_undoredo->undoredo.data.rcvindex;
	    redo_length = mvcc_undoredo->undoredo.rlength;
	    undo_length = mvcc_undoredo->undoredo.ulength;

	    if (LOG_IS_DIFF_UNDOREDO_TYPE (log_type) == true)
	      {
		is_diff = true;
	      }

	    LOG_READ_ADD_ALIGN (thread_p, sizeof (*mvcc_undoredo), &process_lsa, log_page_p);

	    if (rcvindex == RVHF_MVCC_INSERT)
	      {
		MVCC_REC_HEADER mvcc_rec_header;	/* To clear mvcc rec header for MVCC INSERT , because RECDES in log record for RVHF_MVCC_INSERT does not contain */
		char *tmp_ptr;

		if (ZIP_CHECK (redo_length))
		  {
		    redo_length = (int) GET_ZIP_LEN (redo_length);
		    is_zipped_redo = true;
		  }
		if (process_lsa.offset + redo_length < (int) LOGAREA_SIZE)
		  {
		    redo_data = (char *) (log_page_p->area + process_lsa.offset);
		  }
		else
		  {
		    redo_data = (char *) malloc (redo_length);
		    if (redo_data == NULL)
		      {
			cdc_log ("cdc_get_recdes : failed to allocate memory for redo data on recovery index:%d",
				 rcvindex);

			er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, redo_length);
			error_code = ER_OUT_OF_VIRTUAL_MEMORY;
			goto error;
		      }

		    logpb_copy_from_log (thread_p, redo_data, redo_length, &process_lsa, log_page_p);
		    is_redo_alloced = true;
		  }

		if (is_zipped_redo && redo_length != 0)
		  {
		    redo_zip_ptr = log_append_get_zip_redo (thread_p);
		    if (redo_zip_ptr == NULL)
		      {

			er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, IO_PAGESIZE);
			error_code = ER_OUT_OF_VIRTUAL_MEMORY;
			goto error;
		      }

		    is_unzipped_redo = log_unzip (redo_zip_ptr, redo_length, redo_data);
		    if (is_unzipped_redo != true)
		      {
			cdc_log ("cdc_get_recdes : failed to unzip the redo data on recovery index:%d", rcvindex);
			error_code = ER_IO_LZ4_DECOMPRESS_FAIL;
			goto error;
		      }
		  }

		if (is_zipped_redo && is_unzipped_redo)
		  {
		    redo_length = (int) redo_zip_ptr->data_length;

		    if (is_redo_alloced)
		      {
			free_and_init (redo_data);
			is_redo_alloced = false;
		      }

		    redo_data = redo_zip_ptr->log_data;
		  }

		redo_recdes->type = *(INT16 *) redo_data;
		redo_recdes->length = redo_length - sizeof (INT16);

		tmp_ptr = (char *) redo_data + sizeof (redo_recdes->type);
		redo_recdes->length += OR_HEADER_SIZE (tmp_ptr);

		redo_recdes->data = (char *) malloc (redo_recdes->length + MAX_ALIGNMENT);
		if (redo_recdes->data == NULL)
		  {
		    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
			    redo_recdes->length + MAX_ALIGNMENT);
		    error_code = ER_OUT_OF_VIRTUAL_MEMORY;
		    goto error;
		  }

		memcpy (redo_recdes->data, tmp_ptr, OR_INT_SIZE);
		memcpy (redo_recdes->data + OR_CHN_OFFSET, tmp_ptr + OR_INT_SIZE, OR_INT_SIZE);
		memcpy (redo_recdes->data + OR_HEADER_SIZE (tmp_ptr), tmp_ptr + OR_INT_SIZE + OR_INT_SIZE,
			redo_recdes->length - OR_INT_SIZE - OR_INT_SIZE);
	      }
	    else if (rcvindex == RVHF_UPDATE_NOTIFY_VACUUM)
	      {
		if (ZIP_CHECK (undo_length))
		  {
		    undo_length = (int) GET_ZIP_LEN (undo_length);
		  }

		/*if LOG_MVCC_UNDOREDO_DATA_DIFF , get undo data first and get diff */
		if (is_diff)
		  {
		    char temp_buf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT] = "\0";
		    LOG_PAGE *temp_pgptr = (LOG_PAGE *) PTR_ALIGN (temp_buf, MAX_ALIGNMENT);

		    scan_code = cdc_get_undo_record (thread_p, temp_pgptr, *redo_lsa, &tmp_undo_recdes);
		    if (scan_code != S_SUCCESS)
		      {
			error_code = ER_FAILED;
			goto error;
		      }

		    undo_data = (char *) malloc (tmp_undo_recdes.length + sizeof (tmp_undo_recdes.type));
		    if (undo_data == NULL)
		      {

			er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
				tmp_undo_recdes.length + sizeof (tmp_undo_recdes.type));
			error_code = ER_OUT_OF_VIRTUAL_MEMORY;
			goto error;
		      }

		    memcpy (undo_data, &tmp_undo_recdes.type, sizeof (tmp_undo_recdes.type));
		    memcpy (undo_data + sizeof (tmp_undo_recdes.type), tmp_undo_recdes.data, tmp_undo_recdes.length);
		  }

		/* get REDO record */
		LOG_READ_ADD_ALIGN (thread_p, undo_length, &process_lsa, log_page_p);

		if (ZIP_CHECK (redo_length))
		  {
		    redo_length = (int) GET_ZIP_LEN (redo_length);
		    is_zipped_redo = true;
		  }

		redo_data = (char *) malloc (redo_length);
		if (redo_data == NULL)
		  {
		    cdc_log ("cdc_get_recdes : failed to allocate memory for redo data on recovery index:%d", rcvindex);

		    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, redo_length);
		    error_code = ER_OUT_OF_VIRTUAL_MEMORY;
		    goto error;
		  }

		logpb_copy_from_log (thread_p, redo_data, redo_length, &process_lsa, log_page_p);
		is_redo_alloced = true;

		if (is_zipped_redo && redo_length != 0)
		  {
		    redo_zip_ptr = log_append_get_zip_redo (thread_p);
		    if (redo_zip_ptr == NULL)
		      {
			cdc_log ("cdc_get_recdes : failed to get memory of redo zip on recovery index:%d", rcvindex);

			er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, IO_PAGESIZE);
			error_code = ER_OUT_OF_VIRTUAL_MEMORY;
			goto error;
		      }

		    is_unzipped_redo = log_unzip (redo_zip_ptr, redo_length, redo_data);
		    if (is_unzipped_redo != true)
		      {
			cdc_log ("cdc_get_recdes : failed to unzip the redo data on recovery index:%d", rcvindex);
			error_code = ER_IO_LZ4_DECOMPRESS_FAIL;
			goto error;
		      }
		  }

		if (is_zipped_redo && is_unzipped_redo)
		  {
		    redo_length = (int) redo_zip_ptr->data_length;

		    if (is_redo_alloced)
		      {
			free_and_init (redo_data);
			is_redo_alloced = false;
		      }

		    redo_data = redo_zip_ptr->log_data;
		  }

		if (is_diff)
		  {
		    undo_length = tmp_undo_recdes.length + sizeof (tmp_undo_recdes.type);
		    (void) log_diff (undo_length, undo_data, redo_length, redo_data);
		  }

		redo_recdes->type = *(INT16 *) redo_data;
		redo_recdes->length = redo_length - sizeof (INT16);
//              redo_recdes->data = (char *) malloc (redo_recdes->length);
		redo_recdes->data = (char *) malloc (redo_recdes->length + MAX_ALIGNMENT);
		if (redo_recdes->data == NULL)
		  {
		    cdc_log ("cdc_get_recdes : failed to allocate memory for redo_recdes->data on recovery index:%d",
			     rcvindex);

		    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
			    redo_recdes->length + MAX_ALIGNMENT);
		    error_code = ER_OUT_OF_VIRTUAL_MEMORY;
		    goto error;
		  }

		memcpy (redo_recdes->data, (char *) redo_data + sizeof (redo_recdes->type), redo_recdes->length);
	      }

	    break;
	  }
	case LOG_UNDOREDO_DATA:
	case LOG_DIFF_UNDOREDO_DATA:
	  {
	    LOG_REC_UNDOREDO *undoredo = NULL;

	    if ((error_code =
		 cdc_log_read_advance_and_preserve_if_needed (thread_p, sizeof (*undoredo), &process_lsa, log_page_p,
							      preserved_log_page_p)) != NO_ERROR)
	      {
		goto error;
	      }

	    undoredo = (LOG_REC_UNDOREDO *) (log_page_p->area + process_lsa.offset);
	    rcvindex = undoredo->data.rcvindex;
	    redo_length = undoredo->rlength;
	    undo_length = undoredo->ulength;

	    if (LOG_IS_DIFF_UNDOREDO_TYPE (log_type) == true)
	      {
		is_diff = true;
	      }

	    LOG_READ_ADD_ALIGN (thread_p, sizeof (*undoredo), &process_lsa, log_page_p);

	    if (rcvindex == RVHF_INSERT || rcvindex == RVHF_INSERT_NEWHOME)
	      {
		if (ZIP_CHECK (redo_length))
		  {
		    redo_length = (int) GET_ZIP_LEN (redo_length);
		    is_zipped_redo = true;
		  }

		if (process_lsa.offset + redo_length < (int) LOGAREA_SIZE)
		  {
		    redo_data = (char *) (log_page_p->area + process_lsa.offset);
		  }
		else
		  {
		    redo_data = (char *) malloc (redo_length);
		    if (redo_data == NULL)
		      {
			cdc_log ("cdc_get_recdes : failed to allocate memory for redo data on recovery index:%d",
				 rcvindex);

			er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, redo_length);
			error_code = ER_OUT_OF_VIRTUAL_MEMORY;

			goto error;
		      }

		    logpb_copy_from_log (thread_p, redo_data, redo_length, &process_lsa, log_page_p);
		    is_redo_alloced = true;
		  }

		if (is_zipped_redo && redo_length != 0)
		  {
		    redo_zip_ptr = log_append_get_zip_redo (thread_p);
		    if (redo_zip_ptr == NULL)
		      {
			cdc_log ("cdc_get_recdes : failed to get memory for redo zip on recovery index:%d", rcvindex);

			er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, IO_PAGESIZE);
			error_code = ER_OUT_OF_VIRTUAL_MEMORY;

			goto error;
		      }

		    is_unzipped_redo = log_unzip (redo_zip_ptr, redo_length, redo_data);
		    if (is_unzipped_redo != true)
		      {
			cdc_log ("cdc_get_recdes : failed to unzip the redo data on recovery index:%d", rcvindex);
			error_code = ER_IO_LZ4_DECOMPRESS_FAIL;
			goto error;
		      }
		  }

		if (is_zipped_redo && is_unzipped_redo)
		  {
		    redo_length = (int) redo_zip_ptr->data_length;

		    if (is_redo_alloced)
		      {
			free_and_init (redo_data);
			is_redo_alloced = false;
		      }

		    redo_data = redo_zip_ptr->log_data;
		  }

		redo_recdes->type = *(INT16 *) redo_data;
		redo_recdes->length = redo_length - sizeof (INT16);
		redo_recdes->data = (char *) malloc (redo_recdes->length);
		if (redo_recdes->data == NULL)
		  {
		    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, redo_recdes->length);
		    error_code = ER_OUT_OF_VIRTUAL_MEMORY;
		    goto error;
		  }

		memcpy (redo_recdes->data, (char *) redo_data + sizeof (redo_recdes->type), redo_recdes->length);
	      }
	    else if (rcvindex == RVHF_UPDATE)
	      {
		RECDES tmp_undo_recdes = RECDES_INITIALIZER;

		if (ZIP_CHECK (undo_length))
		  {
		    undo_length = (int) GET_ZIP_LEN (undo_length);
		  }

		/*if LOG_MVCC_UNDOREDO_DATA_DIFF , get undo data first and get diff */
		if (is_diff)
		  {
		    char temp_buf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT] = "\0";
		    LOG_PAGE *temp_pgptr = (LOG_PAGE *) PTR_ALIGN (temp_buf, MAX_ALIGNMENT);

		    scan_code = cdc_get_undo_record (thread_p, temp_pgptr, *redo_lsa, &tmp_undo_recdes);
		    if (scan_code != S_SUCCESS)
		      {
			error_code = ER_FAILED;

			goto error;
		      }

		    undo_data = (char *) malloc (tmp_undo_recdes.length + sizeof (tmp_undo_recdes.type));
		    if (undo_data == NULL)
		      {
			er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
				tmp_undo_recdes.length + sizeof (tmp_undo_recdes.type));
			error_code = ER_OUT_OF_VIRTUAL_MEMORY;
			goto error;
		      }

		    memcpy (undo_data, &tmp_undo_recdes.type, sizeof (tmp_undo_recdes.type));
		    memcpy (undo_data + sizeof (tmp_undo_recdes.type), tmp_undo_recdes.data, tmp_undo_recdes.length);
		  }

		/*get REDO record */
		LOG_READ_ADD_ALIGN (thread_p, undo_length, &process_lsa, log_page_p);

		if (ZIP_CHECK (redo_length))
		  {
		    redo_length = (int) GET_ZIP_LEN (redo_length);
		    is_zipped_redo = true;
		  }

		if (process_lsa.offset + redo_length < (int) LOGAREA_SIZE)
		  {
		    redo_data = (char *) (log_page_p->area + process_lsa.offset);
		  }
		else
		  {
		    redo_data = (char *) malloc (redo_length);
		    if (redo_data == NULL)
		      {
			cdc_log ("cdc_get_recdes : failed to allocate memory for redo data on recovery index:%d",
				 rcvindex);

			er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, redo_length);
			error_code = ER_OUT_OF_VIRTUAL_MEMORY;
			goto error;
		      }

		    logpb_copy_from_log (thread_p, redo_data, redo_length, &process_lsa, log_page_p);
		    is_redo_alloced = true;
		  }

		if (is_zipped_redo && redo_length != 0)
		  {
		    redo_zip_ptr = log_append_get_zip_redo (thread_p);
		    if (redo_zip_ptr == NULL)
		      {
			cdc_log ("cdc_get_recdes : failed to get memory for redo zip on recovery index:%d", rcvindex);

			er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, IO_PAGESIZE);
			error_code = ER_OUT_OF_VIRTUAL_MEMORY;
			goto error;
		      }

		    is_unzipped_redo = log_unzip (redo_zip_ptr, redo_length, redo_data);
		    if (is_unzipped_redo != true)
		      {
			cdc_log ("cdc_get_recdes : failed to unzip redo data on recovery index:%d", rcvindex);
			error_code = ER_IO_LZ4_DECOMPRESS_FAIL;
			goto error;
		      }
		  }

		if (is_zipped_redo && is_unzipped_redo)
		  {
		    redo_length = (int) redo_zip_ptr->data_length;

		    if (is_redo_alloced)
		      {
			free_and_init (redo_data);
			is_redo_alloced = false;
		      }

		    redo_data = redo_zip_ptr->log_data;
		  }

		if (is_diff)
		  {
		    undo_length = tmp_undo_recdes.length + sizeof (tmp_undo_recdes.type);

		    (void) log_diff (undo_length, undo_data, redo_length, redo_data);
		  }


		redo_recdes->type = *(INT16 *) redo_data;
		redo_recdes->length = redo_length - sizeof (INT16);
		redo_recdes->data = (char *) malloc (redo_recdes->length);

		if (redo_recdes->data == NULL)
		  {
		    cdc_log ("cdc_get_recdes : failed to allocate memory for redo_recdes->data on recovery index:%d",
			     rcvindex);

		    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, redo_recdes->length);
		    error_code = ER_OUT_OF_VIRTUAL_MEMORY;
		    goto error;
		  }
		memcpy (redo_recdes->data, (char *) redo_data + sizeof (redo_recdes->type), redo_recdes->length);
	      }
	    else if (rcvindex == RVOVF_CHANGE_LINK || rcvindex == RVOVF_NEWPAGE_LINK)
	      {
		/* if the next log page is fetched into log_page_p, then use the preserved_log_page_p */
		log_page_p = process_lsa.pageid == redo_lsa->pageid ? log_page_p : preserved_log_page_p;

		/* GET OVF REDO IMAGE */
		if ((error_code =
		     cdc_get_overflow_recdes (thread_p, log_page_p, redo_recdes, *redo_lsa, rcvindex,
					      true)) != NO_ERROR)
		  {
		    goto error;
		  }

	      }

	    break;
	  }
	case LOG_REDO_DATA:
	  {
	    LOG_REC_REDO *redo = NULL;
	    LOG_RCVINDEX rcvindex;

	    if ((error_code =
		 cdc_log_read_advance_and_preserve_if_needed (thread_p, sizeof (*redo), &process_lsa, log_page_p,
							      preserved_log_page_p)) != NO_ERROR)
	      {
		goto error;
	      }

	    redo = (LOG_REC_REDO *) (log_page_p->area + process_lsa.offset);
	    rcvindex = redo->data.rcvindex;

	    if (rcvindex == RVOVF_NEWPAGE_INSERT || rcvindex == RVOVF_PAGE_UPDATE)
	      {
		/* if the next log page is fetched into log_page_p, then use the preserved_log_page_p */
		log_page_p = process_lsa.pageid == redo_lsa->pageid ? log_page_p : preserved_log_page_p;

		/* GET OVF REDO IMAGE */
		if ((error_code =
		     cdc_get_overflow_recdes (thread_p, log_page_p, redo_recdes, *redo_lsa, rcvindex,
					      true)) != NO_ERROR)
		  {
		    goto error;
		  }
	      }

	    break;

	  }
	case LOG_UNDO_DATA:
	  {
	    LOG_REC_UNDO *undo = NULL;
	    LOG_RCVINDEX rcvindex;

	    if ((error_code =
		 cdc_log_read_advance_and_preserve_if_needed (thread_p, sizeof (*undo), &process_lsa, log_page_p,
							      preserved_log_page_p)) != NO_ERROR)
	      {
		goto error;
	      }

	    undo = (LOG_REC_UNDO *) (log_page_p->area + process_lsa.offset);
	    rcvindex = undo->data.rcvindex;

	    if (rcvindex == RVOVF_PAGE_UPDATE)
	      {
		/* if the next log page is fetched into log_page_p, then use the preserved_log_page_p */
		log_page_p = process_lsa.pageid == redo_lsa->pageid ? log_page_p : preserved_log_page_p;

		if ((error_code =
		     cdc_get_overflow_recdes (thread_p, log_page_p, redo_recdes, *redo_lsa, rcvindex,
					      true)) != NO_ERROR)
		  {
		    goto error;
		  }
	      }
	    break;
	  }
	default:
	  break;

	}
    }

  if (redo_data != NULL && is_redo_alloced)
    {
      free_and_init (redo_data);
    }

  if (undo_data != NULL)
    {
      free_and_init (undo_data);
    }

  if (tmp_undo_recdes.data != NULL)
    {
      free_and_init (tmp_undo_recdes.data);
    }

  return NO_ERROR;

error:
  if (redo_data != NULL && is_redo_alloced)
    {
      free_and_init (redo_data);
    }

  if (undo_data != NULL)
    {
      free_and_init (undo_data);
    }

  if (tmp_undo_recdes.data != NULL)
    {
      free_and_init (tmp_undo_recdes.data);
    }

  if (redo_recdes != NULL && redo_recdes->data != NULL)
    {
      free_and_init (redo_recdes->data);
    }

  if (undo_recdes != NULL && undo_recdes->data != NULL)
    {
      free_and_init (undo_recdes->data);
    }
  return error_code;
}

static int
cdc_get_ovfdata_from_log (THREAD_ENTRY * thread_p, LOG_PAGE * log_page_p,
			  LOG_LSA * process_lsa, int *outlength, char **outdata, LOG_RCVINDEX rcvindex, bool is_redo)
{
  LOG_REC_REDO *redo = NULL;
  LOG_REC_UNDO *undo = NULL;
  int length;
  char *data = NULL;

  LOG_ZIP *zip_ptr = NULL;

  bool is_zipped = false;
  bool is_unzipped = false;

  bool is_alloced = false;

  int error_code = NO_ERROR;

  cdc_log ("cdc_get_ovfdata_from_log : process_lsa:(%lld | %d), recovery index:%d, is_redo:%d",
	   LSA_AS_ARGS (process_lsa), rcvindex, is_redo);
  LOG_READ_ADD_ALIGN (thread_p, DB_SIZEOF (LOG_RECORD_HEADER), process_lsa, log_page_p);

  if (is_redo)
    {
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*redo), process_lsa, log_page_p);
      redo = (LOG_REC_REDO *) (log_page_p->area + process_lsa->offset);
      if (redo->data.rcvindex != rcvindex)
	{
	  goto end;
	}

      length = redo->length;
      LOG_READ_ADD_ALIGN (thread_p, sizeof (*redo), process_lsa, log_page_p);
    }
  else
    {
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*undo), process_lsa, log_page_p);
      undo = (LOG_REC_UNDO *) (log_page_p->area + process_lsa->offset);
      if (undo->data.rcvindex != rcvindex)
	{
	  goto end;
	}
      length = undo->length;
      LOG_READ_ADD_ALIGN (thread_p, sizeof (*undo), process_lsa, log_page_p);
    }

  if (ZIP_CHECK (length))
    {
      length = (int) GET_ZIP_LEN (length);
      is_zipped = true;
    }

  data = (char *) malloc (length);
  if (data == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, length);
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      goto end;
    }

  logpb_copy_from_log (thread_p, data, length, process_lsa, log_page_p);
  is_alloced = true;

  if (is_zipped && length != 0)
    {
      zip_ptr = log_zip_alloc (IO_MAX_PAGE_SIZE);
      if (zip_ptr == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, IO_MAX_PAGE_SIZE);
	  error_code = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto end;
	}

      is_unzipped = log_unzip (zip_ptr, length, data);
      if (is_unzipped == true)
	{
	  length = (int) zip_ptr->data_length;

	  if (is_alloced)
	    {
	      free_and_init (data);
	      is_alloced = false;
	    }

	  data = zip_ptr->log_data;
	}
      else
	{
	  error_code = ER_IO_LZ4_DECOMPRESS_FAIL;
	  goto end;
	}
    }

  *outlength = length;
  *outdata = (char *) malloc (length);
  if (*outdata == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, length);
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      goto end;
    }

  memcpy (*outdata, (char *) data, length);

end:
  if (data != NULL && is_alloced)
    {
      free_and_init (data);
    }
  if (zip_ptr != NULL)
    {
      log_zip_free (zip_ptr);
    }

  cdc_log ("cdc_get_ovfdata_from_log : success to get overflow data. length:%d", length);

  return error_code;
}

static int
cdc_get_overflow_recdes (THREAD_ENTRY * thread_p, LOG_PAGE * log_page_p, RECDES * recdes, LOG_LSA lsa,
			 LOG_RCVINDEX rcvindex, bool is_redo)
{
  LOG_LSA current_lsa;
  LOG_LSA prev_lsa;

  LOG_PAGE *current_log_page;
  LOG_RECORD_HEADER *current_log_record;

  OVF_PAGE_LIST *ovf_list_head = NULL;
  OVF_PAGE_LIST *ovf_list_tail = NULL;
  OVF_PAGE_LIST *ovf_list_data = NULL;

  int trid;

  bool first = true;
  int copyed_len;
  int area_len;
  int area_offset;
  int error_code = NO_ERROR;
  int length = 0;

  LSA_COPY (&current_lsa, &lsa);
  current_log_page = log_page_p;
  current_log_record = LOG_GET_LOG_RECORD_HEADER (current_log_page, &current_lsa);

  trid = current_log_record->trid;

  if (((current_log_record->type == LOG_UNDO_DATA) && (rcvindex == RVOVF_PAGE_UPDATE) && is_redo)
      || ((current_log_record->type == LOG_REDO_DATA) && (rcvindex == RVOVF_PAGE_UPDATE) && !is_redo)
      || rcvindex == RVOVF_CHANGE_LINK || rcvindex == RVOVF_NEWPAGE_LINK)
    {
      /* start to traverse with prev_transla log record */
      LSA_COPY (&current_lsa, &current_log_record->prev_tranlsa);

      if (current_lsa.pageid != lsa.pageid)
	{
	  if (logpb_fetch_page (thread_p, &current_lsa, LOG_CS_SAFE_READER, current_log_page) != NO_ERROR)
	    {
	      error_code = ER_FAILED;
	      goto end;
	    }
	}

      if (rcvindex == RVOVF_CHANGE_LINK || rcvindex == RVOVF_NEWPAGE_LINK)
	{
	  /* rcvindex of previous log record is RVOVF_PAGE_UPDATE */
	  rcvindex = RVOVF_PAGE_UPDATE;
	}
    }

  while (!LSA_ISNULL (&current_lsa))
    {
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*current_log_record), &current_lsa, current_log_page);

      current_log_record = LOG_GET_LOG_RECORD_HEADER (current_log_page, &current_lsa);

      LSA_COPY (&prev_lsa, &current_log_record->prev_tranlsa);

      if (current_log_record->trid != trid || current_log_record->type == LOG_DUMMY_OVF_RECORD)
	{
	  if (!is_redo && current_log_record->type == LOG_DUMMY_OVF_RECORD)
	    {
	      /*get one more */
	      ovf_list_data = (OVF_PAGE_LIST *) malloc (DB_SIZEOF (OVF_PAGE_LIST));
	      if (ovf_list_data == NULL)
		{
		  cdc_log ("cdc_get_overflow_recdes : failed to allocate memory for overflow data ");
		  /* malloc failed */

		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, DB_SIZEOF (OVF_PAGE_LIST));
		  error_code = ER_OUT_OF_VIRTUAL_MEMORY;
		  goto end;
		}

	      memset (ovf_list_data, 0, DB_SIZEOF (OVF_PAGE_LIST));

	      error_code =
		cdc_get_ovfdata_from_log (thread_p, current_log_page,
					  &prev_lsa, &ovf_list_data->length, &ovf_list_data->data, rcvindex, is_redo);

	      if (error_code == NO_ERROR && ovf_list_data->data)
		{
		  /* add to linked-list */
		  if (ovf_list_head == NULL)
		    {
		      ovf_list_head = ovf_list_tail = ovf_list_data;
		    }
		  else
		    {
		      ovf_list_data->next = ovf_list_head;
		      ovf_list_head = ovf_list_data;
		    }

		  length += ovf_list_data->length;
		}
	      else
		{
		  if (ovf_list_data->data != NULL)
		    {
		      free_and_init (ovf_list_data->data);
		    }

		  free_and_init (ovf_list_data);
		  goto end;
		}
	    }

	  break;
	}
      else if ((LOG_IS_REDO_RECORD_TYPE (current_log_record->type) && is_redo)
	       || (LOG_IS_UNDO_RECORD_TYPE (current_log_record->type) && !is_redo))
	{
	  ovf_list_data = (OVF_PAGE_LIST *) malloc (DB_SIZEOF (OVF_PAGE_LIST));
	  if (ovf_list_data == NULL)
	    {
	      cdc_log ("cdc_get_overflow_recdes : failed to allocate memory for overflow data ");

	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, DB_SIZEOF (OVF_PAGE_LIST));
	      /* malloc failed */
	      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
	      goto end;
	    }

	  memset (ovf_list_data, 0, DB_SIZEOF (OVF_PAGE_LIST));

	  error_code =
	    cdc_get_ovfdata_from_log (thread_p, current_log_page, &current_lsa,
				      &ovf_list_data->length, &ovf_list_data->data, rcvindex, is_redo);

	  if (error_code == NO_ERROR && ovf_list_data->data)
	    {
	      /* add to linked-list */
	      if (ovf_list_head == NULL)
		{
		  ovf_list_head = ovf_list_tail = ovf_list_data;
		}
	      else
		{
		  ovf_list_data->next = ovf_list_head;
		  ovf_list_head = ovf_list_data;
		}

	      length += ovf_list_data->length;
	    }
	  else
	    {
	      if (ovf_list_data->data != NULL)
		{
		  free_and_init (ovf_list_data->data);
		}

	      free_and_init (ovf_list_data);
	      goto end;
	    }
	}

      if (current_lsa.pageid != prev_lsa.pageid && !LSA_ISNULL (&prev_lsa))
	{
	  if (logpb_fetch_page (thread_p, &prev_lsa, LOG_CS_SAFE_READER, current_log_page) != NO_ERROR)
	    {
	      error_code = ER_FAILED;

	      goto end;
	    }
	}
      LSA_COPY (&current_lsa, &prev_lsa);
    }

  assert (recdes != NULL);

  recdes->data = (char *) malloc (length);
  if (recdes->data == NULL)
    {
      cdc_log ("cdc_get_overflow_recdes : failed to allocate memory for record descriptor for overflow data");
      /* malloc failed: clear linked-list */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, length);
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;

      goto end;
    }

  /* make record description */
  copyed_len = 0;
  while (ovf_list_head)
    {
      ovf_list_data = ovf_list_head;
      ovf_list_head = ovf_list_head->next;

      if (first)
	{
	  area_offset = offsetof (OVERFLOW_FIRST_PART, data);
	  first = false;
	}
      else
	{
	  area_offset = offsetof (OVERFLOW_REST_PART, data);
	}
      area_len = ovf_list_data->length - area_offset;
      memcpy (recdes->data + copyed_len, ovf_list_data->data + area_offset, area_len);
      copyed_len += area_len;

      free_and_init (ovf_list_data->data);
      free_and_init (ovf_list_data);
    }

  recdes->length = length;

end:
  while (ovf_list_head)
    {
      ovf_list_data = ovf_list_head;
      ovf_list_head = ovf_list_head->next;
      free_and_init (ovf_list_data->data);
      free_and_init (ovf_list_data);
    }

  return error_code;

}

static int
cdc_find_primary_key (THREAD_ENTRY * thread_p, OID classoid, int repr_id, int *num_attr, int **pk_attr_id)
{
  /*1. if PK exists, return 0 with PK column id(pk_attr_id) and number of columns(num_attr) 
   *2. if PK does not exist, return -1 
   *3. pk_attr_id is required to be free_and_init() from caller 
   * */

  /*refer locator_check_foreign_key */

  /*check if it has PK and, maybe.. it can returns PK attributes ID */
  OR_CLASSREP *rep = NULL;
  OR_INDEX *index = NULL;
  OR_ATTRIBUTE *index_att = NULL;
  int idx_incache = -1;
  int has_pk = 0;
  int *pk_attr;
  int num_idx_att = 0;
  *num_attr = 0;

  /*class representation initialization */
  rep = heap_classrepr_get (thread_p, &classoid, NULL, repr_id, &idx_incache);

  assert (rep != NULL);

  for (int i = 0; i < rep->n_indexes; i++)
    {
      index = rep->indexes + i;
      if (index->type == BTREE_PRIMARY_KEY)
	{
	  has_pk = 1;
	  /*reference : qexec_execute_build_indexes() */
	  if (index->func_index_info == NULL)
	    {
	      num_idx_att = index->n_atts;
	    }
	  else
	    {
	      // TODO : function indexes 
	      num_idx_att = index->func_index_info->attr_index_start;
	      return ER_FAILED;
	    }

	  pk_attr = (int *) malloc (sizeof (int) * num_idx_att);
	  if (pk_attr == NULL)
	    {
	      cdc_log ("cdc_find_primary_key : failed to allocate memory for primary key attributes");

	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (int) * num_idx_att);
	      return ER_OUT_OF_VIRTUAL_MEMORY;
	    }

	  for (int j = 0; j < num_idx_att; j++)
	    {
	      index_att = index->atts[j];
	      pk_attr[j] = index_att->def_order;
	      *num_attr += 1;
	    }
	  *pk_attr_id = pk_attr;
	  break;
	}
    }

  return has_pk;
}

static int
cdc_make_error_loginfo (int trid, char *user, CDC_DML_TYPE dml_type, OID classoid, CDC_LOGINFO_ENTRY * dml_entry)
{
  char *loginfo_buf = NULL;
  int defalut_length = 32 + DB_MAX_USER_LENGTH;

  char *ptr, *start_ptr;

  uint64_t b_classoid;
  CDC_DATAITEM_TYPE dataitem_type = CDC_DML;
  int num_change_col = 0;
  int num_cond_col = 0;

  int error_code = NO_ERROR;

  /* if not able to find schema  */
  loginfo_buf = (char *) malloc ((defalut_length * 2) + MAX_ALIGNMENT);
  if (loginfo_buf == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, defalut_length * 2 + MAX_ALIGNMENT);
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      goto error;
    }

  ptr = start_ptr = PTR_ALIGN (loginfo_buf, MAX_ALIGNMENT);
  ptr = or_pack_int (ptr, 0);	//dummy for log info length 
  ptr = or_pack_int (ptr, trid);
  ptr = or_pack_string (ptr, user);
  ptr = or_pack_int (ptr, dataitem_type);

  memcpy (&b_classoid, &classoid, sizeof (uint64_t));
  ptr = or_pack_int (ptr, dml_type);
  ptr = or_pack_int64 (ptr, b_classoid);
  ptr = or_pack_int (ptr, num_change_col);
  ptr = or_pack_int (ptr, num_cond_col);
  dml_entry->length = ptr - start_ptr;
  or_pack_int (start_ptr, dml_entry->length);

  dml_entry->log_info = (char *) malloc (dml_entry->length);
  if (dml_entry->log_info == NULL)
    {
      cdc_log ("cdc_make_error_loginfo : failed to allocate memory for log info in dml log entry");
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, dml_entry->length);
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      goto error;
    }

  memcpy (dml_entry->log_info, start_ptr, dml_entry->length);

  free_and_init (loginfo_buf);

  return ER_CDC_LOGINFO_ENTRY_GENERATED;

error:
  if (loginfo_buf != NULL)
    {
      free_and_init (loginfo_buf);
    }

  return error_code;
}

#define FLASHBACK_ERROR_HANDLING(is_flashback, e, classoid, classname)\
  do \
    { \
      if (is_flashback) \
        { \
          error_code = (e); \
          if (error_code == ER_FLASHBACK_SCHEMA_CHANGED) \
            { \
              if (heap_get_class_name (thread_p, &(classoid), &(classname)) == NO_ERROR) \
                { \
                  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FLASHBACK_SCHEMA_CHANGED, 4, (classname), OID_AS_ARGS (&(classoid))); \
                  free_and_init ((classname)); \
                } \
            } \
          \
          goto exit; \
        } \
    } \
  while (0)

/*
 * cdc_check_if_schema_changed - compare the representation in record descriptor with latest representation,
 *                               then check if they have different representation ID
 *
 * return: TRUE if schema has been changed
 *
 * recdes (in) : the instance record descriptor
 * attr_info (in) : attribute information structure which contains last class representation
 *
 * NOTE: This function is called in making log info entry for CDC and Flashback.
 *       CDC cannot support schema-changed tables because CDC can interpret tables with schemas pre-fetched through JDBC.
 *       Also, since the old representation does not contain information necessary to construct SQL,
 *       such as def_order, CDC and Flashback have design issue that cannot support tables whose schema has been changed.
 *       So this function is used to check if the schema has changed.
 */
static bool
cdc_check_if_schema_changed (RECDES * recdes, HEAP_CACHE_ATTRINFO * attr_info)
{
  /* schema has been changed */
  return or_rep_id (recdes) != attr_info->last_classrepr->id;
}

static int
cdc_get_attribute_size (DB_VALUE * value)
{
  int size = 0;

  switch (DB_VALUE_TYPE (value))
    {
    case DB_TYPE_INTEGER:
      size = sizeof (DB_C_INT);
      break;
    case DB_TYPE_BIGINT:
      size = sizeof (DB_C_BIGINT);
      break;
    case DB_TYPE_SHORT:
      size = sizeof (DB_C_SHORT);
      break;
    case DB_TYPE_FLOAT:
      size = sizeof (DB_C_FLOAT);
      break;
    case DB_TYPE_DOUBLE:
      size = sizeof (DB_C_DOUBLE);
      break;
    case DB_TYPE_NUMERIC:
      size = DB_NUMERIC_BUF_SIZE;
      break;
    case DB_TYPE_BIT:
    case DB_TYPE_VARBIT:
      {
	/* size of the data converted into bit string include "X''"
	 * e.g. 17 = B'10001' = X'11' */
	size = ((db_get_string_length (value) + 3) / 4) + 3;
	break;
      }
    case DB_TYPE_CHAR:
    case DB_TYPE_VARCHAR:
      size = db_get_string_size (value);
      break;
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
      /* size of string "N''" is 4
       * e.g. N'string' */
      size = db_get_string_size (value) + 3;
      break;
    case DB_TYPE_TIME:
      /* precision in data types related to DATE/TIME means the size the string converted from the date/time data */
      size = DB_TIME_PRECISION;
      break;
    case DB_TYPE_TIMESTAMP:
      size = DB_TIMESTAMP_PRECISION;
      break;
    case DB_TYPE_DATETIME:
      size = DB_DATETIME_PRECISION;
      break;
    case DB_TYPE_TIMESTAMPTZ:
    case DB_TYPE_TIMESTAMPLTZ:
      size = DB_TIMESTAMPTZ_PRECISION;
      break;
    case DB_TYPE_DATETIMETZ:
    case DB_TYPE_DATETIMELTZ:
      size = DB_DATETIMETZ_PRECISION;
      break;
    case DB_TYPE_DATE:
      size = DB_DATE_PRECISION;
      break;
    case DB_TYPE_MONETARY:
      {
	DB_MONETARY *money_p = db_get_monetary (value);
	const char *currency_symbol = lang_currency_symbol (money_p->type);

	/* monetary contains double value, and size of the string converted from double value can not exceeds 23 bytes
	 * maximum value : 1.7976931348623157E+308
	 * 15bytes significant number + '.' + 'e+308' = 23 bytes */
	const int maximum_double_buffer_size = 23;

	size = strlen (currency_symbol) + maximum_double_buffer_size;
      }
      break;
    case DB_TYPE_ENUMERATION:
      if (db_get_enum_string (value) == NULL && db_get_enum_short (value) != 0)
	{
	  size = sizeof (DB_C_SHORT);
	}
      else
	{
	  DB_VALUE varchar_val;
	  /* print enumerations as strings */
	  if (tp_enumeration_to_varchar (value, &varchar_val) == NO_ERROR)
	    {
	      size = db_get_string_size (&varchar_val);
	    }
	  else
	    {
	      assert (false);
	    }
	}
      break;
    case DB_TYPE_BLOB:
    case DB_TYPE_CLOB:
      {
	DB_ELO *elo;
	elo = db_get_elo (value);

	size = elo == NULL ? 0 : (int) strlen (elo->locator);
      }
      break;
    case DB_TYPE_NULL:
    case DB_TYPE_VARIABLE:
    case DB_TYPE_SUB:
    case DB_TYPE_DB_VALUE:
    case DB_TYPE_OBJECT:
    case DB_TYPE_SET:
    case DB_TYPE_MULTISET:
    case DB_TYPE_SEQUENCE:
    case DB_TYPE_ELO:
    case DB_TYPE_JSON:
    case DB_TYPE_POINTER:
    case DB_TYPE_ERROR:
      /* Not supported */
      size = 0;
      break;
    default:
      assert (false);
      break;
    }

  return size;

}

int
cdc_make_dml_loginfo (THREAD_ENTRY * thread_p, int trid, char *user, CDC_DML_TYPE dml_type,
		      OID classoid, RECDES * undo_recdes, RECDES * redo_recdes, CDC_LOGINFO_ENTRY * dml_entry,
		      bool is_flashback)
{
  /*this is for constructing dml data item */
  int has_pk = 0;
  int *pk_attr_index = NULL;	/*not attr_id, def_order array */
  int num_pk_attr;

  CDC_DATAITEM_TYPE dataitem_type = CDC_DML;
  char *ptr, *start_ptr;
  uint64_t b_classoid = 0;

  DB_VALUE *old_values = NULL;
  DB_VALUE *new_values = NULL;

  int oldval_deforder;
  int newval_deforder;

  int repid;

  int num_change_col = 0;
  char **changed_col_data = NULL;
  int *changed_col_data_len = NULL;
  int *changed_col_idx = NULL;

  int num_cond_col = 0;
  int *cond_col_idx = NULL;
  char **cond_col_data = NULL;
  int *cond_col_data_len = NULL;

  int error_code = NO_ERROR;

  OR_CLASSREP *rep = NULL;
  HEAP_CACHE_ATTRINFO attr_info;
  bool attrinfo_inited = false;

  HEAP_ATTRVALUE *heap_value = NULL;

  int i = 0;
  int cnt = 0;
  int length = 0;

  int record_length = 0;
  int metadata_length = 0;
  int buffer_size = 0;
  int align_size = 0;

  char *loginfo_buf = NULL;
  OID partitioned_classoid = OID_INITIALIZER;

  char *classname = NULL;

  /* when partition class oid input, it is required to be changed to partitioned class oid  */

  if ((error_code = partition_find_root_class_oid (thread_p, &classoid, &partitioned_classoid)) == NO_ERROR)
    {
      if (!OID_ISNULL (&partitioned_classoid))
	{
	  COPY_OID (&classoid, &partitioned_classoid);
	}
    }
  else
    {
      /* can not get class schema of classoid due to drop */

      /* if schema is changed, flashback error handling first then cdc error handling */
      FLASHBACK_ERROR_HANDLING (is_flashback, ER_FLASHBACK_SCHEMA_CHANGED, classoid, classname);

      error_code = cdc_make_error_loginfo (trid, user, dml_type, classoid, dml_entry);
      cdc_log ("cdc_make_dml_loginfo : failed to find class old representation ");
      goto exit;
    }

  cdc_log ("cdc_make_dml_loginfo : started with trid:%d, transaction user:%s, class oid:(%d|%d|%d), dml type:%d", trid,
	   user, OID_AS_ARGS (&classoid), dml_type);

  if ((error_code = heap_attrinfo_start (thread_p, &classoid, -1, NULL, &attr_info)) != NO_ERROR)
    {
      FLASHBACK_ERROR_HANDLING (is_flashback, ER_FLASHBACK_SCHEMA_CHANGED, classoid, classname);

      error_code = cdc_make_error_loginfo (trid, user, dml_type, classoid, dml_entry);
      cdc_log ("cdc_make_dml_loginfo : failed to find class representation ");

      goto exit;
    }
  else
    {
      attrinfo_inited = true;
    }

  if (undo_recdes != NULL)
    {
      if (cdc_check_if_schema_changed (undo_recdes, &attr_info))
	{
	  FLASHBACK_ERROR_HANDLING (is_flashback, ER_FLASHBACK_SCHEMA_CHANGED, classoid, classname);

	  error_code = cdc_make_error_loginfo (trid, user, dml_type, classoid, dml_entry);
	  cdc_log ("cdc_make_dml_loginfo : failed to find class old representation ");

	  goto exit;
	}

      if ((error_code = heap_attrinfo_read_dbvalues (thread_p, &classoid, undo_recdes, &attr_info)) != NO_ERROR)
	{
	  goto exit;
	}

      old_values = (DB_VALUE *) malloc (sizeof (DB_VALUE) * attr_info.num_values);
      if (old_values == NULL)
	{
	  error_code = ER_OUT_OF_VIRTUAL_MEMORY;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
		  sizeof (DB_VALUE) * attr_info.num_values);
	  goto exit;
	}

      for (i = 0; i < attr_info.num_values; i++)
	{
	  heap_value = &attr_info.values[i];

	  assert (heap_value->read_attrepr != NULL);

	  oldval_deforder = heap_value->read_attrepr->def_order;

	  memcpy (&old_values[oldval_deforder], &heap_value->dbvalue, sizeof (DB_VALUE));

	  record_length += cdc_get_attribute_size (&heap_value->dbvalue);
	}
    }

  if (redo_recdes != NULL)
    {
      if (cdc_check_if_schema_changed (redo_recdes, &attr_info))
	{
	  FLASHBACK_ERROR_HANDLING (is_flashback, ER_FLASHBACK_SCHEMA_CHANGED, classoid, classname);

	  error_code = cdc_make_error_loginfo (trid, user, dml_type, classoid, dml_entry);
	  cdc_log ("cdc_make_dml_loginfo : failed to find class old representation ");

	  goto exit;
	}

      if ((error_code = heap_attrinfo_read_dbvalues (thread_p, &classoid, redo_recdes, &attr_info)) != NO_ERROR)
	{
	  goto exit;
	}

      new_values = (DB_VALUE *) malloc (sizeof (DB_VALUE) * attr_info.num_values);
      if (new_values == NULL)
	{
	  error_code = ER_OUT_OF_VIRTUAL_MEMORY;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
		  sizeof (DB_VALUE) * attr_info.num_values);

	  goto exit;
	}

      for (i = 0; i < attr_info.num_values; i++)
	{
	  heap_value = &attr_info.values[i];

	  assert (heap_value->read_attrepr != NULL);

	  newval_deforder = heap_value->read_attrepr->def_order;

	  memcpy (&new_values[newval_deforder], &heap_value->dbvalue, sizeof (DB_VALUE));

	  record_length += cdc_get_attribute_size (&heap_value->dbvalue);
	}
    }

  if ((cdc_Gl.producer.all_in_cond == 0) && dml_type != CDC_INSERT && !is_flashback)
    {
      if (redo_recdes != NULL)
	{
	  repid = or_rep_id (redo_recdes);
	}
      else
	{
	  repid = or_rep_id (undo_recdes);
	}

      /*for dml type == insert, it does not need to find PK info */
      has_pk = cdc_find_primary_key (thread_p, classoid, repid, &num_pk_attr, &pk_attr_index);
      if (has_pk < 0)
	{
	  error_code = ER_FAILED;
	  goto exit;
	}
    }

  /* metadata for CDC loginfo :
   * loginfo length (int) + trid (int) + user name (32) + data item type (int) + dml_type (int) + classoid (int64)
   * + number of changed column (int) + changed column index (int * number of column)
   * + number of condition column + condition column index (int * number of column)
   * + function type (int) * number of column * 2 */

  metadata_length = OR_INT_SIZE + OR_INT_SIZE + DB_MAX_USER_LENGTH + OR_INT_SIZE + OR_INT_SIZE + OR_BIGINT_SIZE +
    OR_INT_SIZE + (attr_info.num_values * OR_INT_SIZE) + OR_INT_SIZE + (attr_info.num_values * OR_INT_SIZE) +
    attr_info.num_values * OR_INT_SIZE * 2;

  /* sum of the pad size through aligning the attributes (changed column, cond column) */
  align_size = MAX_ALIGNMENT * attr_info.num_values * 2;

  buffer_size = metadata_length + record_length + align_size;

  loginfo_buf = (char *) malloc (buffer_size + MAX_ALIGNMENT);
  if (loginfo_buf == NULL)
    {
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, record_length * 5 + MAX_ALIGNMENT);
      goto exit;
    }

  changed_col_idx = (int *) malloc (sizeof (int) * attr_info.num_values);
  if (changed_col_idx == NULL)
    {
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, attr_info.num_values);
      goto exit;
    }

  ptr = start_ptr = PTR_ALIGN (loginfo_buf, MAX_ALIGNMENT);
  ptr = or_pack_int (ptr, 0);	//dummy for log info length 
  ptr = or_pack_int (ptr, trid);
  ptr = or_pack_string (ptr, user);
  ptr = or_pack_int (ptr, dataitem_type);
  memcpy (&b_classoid, &classoid, sizeof (uint64_t));

  switch (dml_type)
    {
    case CDC_INSERT:
    case CDC_TRIGGER_INSERT:
      /*insert */
      num_change_col = attr_info.num_values;
      ptr = or_pack_int (ptr, dml_type);
      ptr = or_pack_int64 (ptr, b_classoid);
      ptr = or_pack_int (ptr, num_change_col);
      for (i = 0; i < num_change_col; i++)
	{
	  ptr = or_pack_int (ptr, i);
	}

      for (i = 0; i < num_change_col; i++)
	{
	  if ((error_code = cdc_put_value_to_loginfo (&new_values[i], &ptr)) != NO_ERROR)
	    {
	      goto exit;
	    }
	}

      ptr = or_pack_int (ptr, num_cond_col);
      break;
    case CDC_UPDATE:
    case CDC_TRIGGER_UPDATE:
      /*update */
      ptr = or_pack_int (ptr, dml_type);
      ptr = or_pack_int64 (ptr, b_classoid);

      if (!is_flashback)
	{
	  for (i = 0; i < attr_info.num_values; i++)
	    {
	      if (cdc_compare_undoredo_dbvalue (&new_values[i], &old_values[i]) > 0)
		{
		  changed_col_idx[cnt++] = i;	//TODO: replace i with def_order to reduce memory alloc and copy 
		}
	    }

	  if (cnt == 0)
	    {
	      /* This is due to update log record appended by trigger savepoint.
	       * It is not sure why update log is appended by trigger savepoint */

	      error_code = ER_CDC_IGNORE_LOG_INFO_INTERNAL;
	      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_CDC_IGNORE_LOG_INFO_INTERNAL, 0);
	      goto exit;
	    }

	  num_change_col = cnt;
	  ptr = or_pack_int (ptr, num_change_col);
	  for (i = 0; i < num_change_col; i++)
	    {
	      ptr = or_pack_int (ptr, changed_col_idx[i]);
	    }

	  for (i = 0; i < num_change_col; i++)
	    {
	      if (cdc_put_value_to_loginfo (&new_values[changed_col_idx[i]], &ptr) != NO_ERROR)
		{
		  error_code = ER_FAILED;
		  goto exit;
		}
	    }
	}
      else
	{
	  ptr = or_pack_int (ptr, attr_info.num_values);

	  for (i = 0; i < attr_info.num_values; i++)
	    {
	      ptr = or_pack_int (ptr, i);
	    }

	  for (i = 0; i < attr_info.num_values; i++)
	    {
	      if (cdc_put_value_to_loginfo (&new_values[i], &ptr) != NO_ERROR)
		{
		  error_code = ER_FAILED;
		  goto exit;
		}
	    }

	}

      if (has_pk == 1)
	{
	  num_cond_col = num_pk_attr;
	  cond_col_idx = pk_attr_index;
	  ptr = or_pack_int (ptr, num_cond_col);

	  for (i = 0; i < num_cond_col; i++)
	    {
	      ptr = or_pack_int (ptr, cond_col_idx[i]);
	    }

	  for (i = 0; i < num_cond_col; i++)
	    {
	      if (cdc_put_value_to_loginfo (&old_values[cond_col_idx[i]], &ptr) != NO_ERROR)
		{
		  error_code = ER_FAILED;
		  goto exit;
		}
	    }
	}
      else
	{
	  num_cond_col = attr_info.num_values;
	  ptr = or_pack_int (ptr, num_cond_col);

	  for (i = 0; i < num_cond_col; i++)
	    {
	      ptr = or_pack_int (ptr, i);
	    }

	  for (i = 0; i < num_cond_col; i++)
	    {
	      if (cdc_put_value_to_loginfo (&old_values[i], &ptr) != NO_ERROR)
		{
		  error_code = ER_FAILED;
		  goto exit;
		}
	    }
	}
      break;
    case CDC_DELETE:
    case CDC_TRIGGER_DELETE:
      /*delete */
      ptr = or_pack_int (ptr, dml_type);
      ptr = or_pack_int64 (ptr, b_classoid);
      ptr = or_pack_int (ptr, num_change_col);
      if (has_pk == 1)
	{
	  num_cond_col = num_pk_attr;
	  cond_col_idx = pk_attr_index;
	  ptr = or_pack_int (ptr, num_cond_col);
	  for (i = 0; i < num_cond_col; i++)
	    {
	      ptr = or_pack_int (ptr, cond_col_idx[i]);
	    }

	  for (i = 0; i < num_cond_col; i++)
	    {
	      if (cdc_put_value_to_loginfo (&old_values[cond_col_idx[i]], &ptr) != NO_ERROR)
		{
		  error_code = ER_FAILED;
		  goto exit;
		}
	    }
	}
      else
	{
	  num_cond_col = attr_info.num_values;
	  ptr = or_pack_int (ptr, num_cond_col);
	  for (i = 0; i < num_cond_col; i++)
	    {
	      ptr = or_pack_int (ptr, i);
	    }

	  for (i = 0; i < num_cond_col; i++)
	    {
	      if (cdc_put_value_to_loginfo (&old_values[i], &ptr) != NO_ERROR)
		{
		  error_code = ER_FAILED;
		  goto exit;
		}
	    }
	}
      break;
    }
  /*malloc the size of log_info and packing and  entry->log_info will pointing it  */

  dml_entry->length = ptr - start_ptr;
  or_pack_int (start_ptr, dml_entry->length);

  dml_entry->log_info = (char *) malloc (dml_entry->length);
  if (dml_entry->log_info == NULL)
    {
      cdc_log ("cdc_make_dml_loginfo : failed to allocate memory for log info in dml log entry");
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, dml_entry->length);
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      goto exit;
    }

  memcpy (dml_entry->log_info, start_ptr, dml_entry->length);

  FLASHBACK_ERROR_HANDLING (is_flashback, NO_ERROR, classoid, classname);

  error_code = ER_CDC_LOGINFO_ENTRY_GENERATED;

  cdc_log ("cdc_make_dml_loginfo : success to generated dml log info. length:%d", dml_entry->length);

exit:

  if (loginfo_buf != NULL)
    {
      free_and_init (loginfo_buf);
    }

  if (changed_col_idx != NULL)
    {
      free_and_init (changed_col_idx);
    }

  if (cond_col_idx != NULL)
    {
      free_and_init (cond_col_idx);
    }

  if (old_values != NULL)
    {
      free_and_init (old_values);
    }

  if (new_values != NULL)
    {
      free_and_init (new_values);
    }

  if (attrinfo_inited)
    {
      heap_attrinfo_end (thread_p, &attr_info);
    }

  return error_code;
}

static int
cdc_make_ddl_loginfo (char *supplement_data, int trid, const char *user, CDC_LOGINFO_ENTRY * ddl_entry)
{
  /* supplemental data : | ddl type | obj type | class OID | object OID | statement length | statement | */

  char *ptr, *start_ptr;
  int ddl_type, object_type;
  uint64_t b_classoid, b_objectoid;
  OID classoid;
  OID oid;
  int statement_length;
  char *statement;

  /*ddl log info : TRID | user | data_item_type | ddl_type | object_type | OID | class OID | statement length | statement | 
   * cdc_make_ddl_loginfo construct log info from ddl_type to statement */
  int loginfo_length;
  int dataitem_type = CDC_DDL;
  char *loginfo_buf = NULL;;

  int error_code = NO_ERROR;

  ptr = PTR_ALIGN (supplement_data, MAX_ALIGNMENT);

  ptr = or_unpack_int (ptr, &ddl_type);
  ptr = or_unpack_int (ptr, &object_type);
  ptr = or_unpack_oid (ptr, &classoid);

  if (!OID_ISNULL (&classoid))
    {
      if (oid_is_system_class (&classoid) || !cdc_is_filtered_class (classoid))
	{
	  error_code = ER_CDC_IGNORE_LOG_INFO;
	  cdc_log ("cdc_log_extract : Skip producing log info for an invalid class (%d|%d|%d)",
		   OID_AS_ARGS (&classoid));
	  goto error;
	}
    }

  ptr = or_unpack_oid (ptr, &oid);
  ptr = or_unpack_int (ptr, &statement_length);
  ptr = or_unpack_string_nocopy (ptr, &statement);

  cdc_log
    ("cdc_make_ddl_loginfo : started with trid:%d, transaction user:%s, class oid:(%d|%d|%d), ddl type:%d, object type:%d",
     trid, user, OID_AS_ARGS (&classoid), ddl_type, object_type);

  memcpy (&b_classoid, &classoid, sizeof (OID));
  memcpy (&b_objectoid, &oid, sizeof (OID));

  loginfo_length = (OR_INT_SIZE
		    + OR_INT_SIZE
		    + or_packed_string_length (user, NULL)
		    + OR_INT_SIZE
		    + OR_INT_SIZE + OR_INT_SIZE + OR_BIGINT_SIZE + OR_BIGINT_SIZE + OR_INT_SIZE + statement_length);

  loginfo_buf = (char *) malloc (loginfo_length * 2 + MAX_ALIGNMENT);
  if (loginfo_buf == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, loginfo_length * 2 + MAX_ALIGNMENT);
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      goto error;
    }

  ptr = start_ptr = PTR_ALIGN (loginfo_buf, MAX_ALIGNMENT);
  ptr = or_pack_int (ptr, loginfo_length);
  ptr = or_pack_int (ptr, trid);
  ptr = or_pack_string (ptr, user);
  ptr = or_pack_int (ptr, dataitem_type);
  ptr = or_pack_int (ptr, ddl_type);
  ptr = or_pack_int (ptr, object_type);
  ptr = or_pack_int64 (ptr, (INT64) b_objectoid);
  ptr = or_pack_int64 (ptr, (INT64) b_classoid);
  ptr = or_pack_int (ptr, statement_length);
  ptr = or_pack_string (ptr, statement);

  ddl_entry->length = ptr - start_ptr;
  or_pack_int (start_ptr, ddl_entry->length);

  ddl_entry->log_info = (char *) malloc (ddl_entry->length);
  if (ddl_entry->log_info == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, ddl_entry->length);
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      goto error;
    }

  memcpy (ddl_entry->log_info, start_ptr, ddl_entry->length);

  free_and_init (loginfo_buf);

  cdc_log ("cdc_make_ddl_loginfo : success to generated ddl log info. length:%d", ddl_entry->length);

  return ER_CDC_LOGINFO_ENTRY_GENERATED;

error:

  if (loginfo_buf != NULL)
    {
      free_and_init (loginfo_buf);
    }

  return error_code;
}

static int
cdc_make_dcl_loginfo (time_t at_time, int trid, char *user, int log_type, CDC_LOGINFO_ENTRY * dcl_entry)
{
  CDC_DATAITEM_TYPE dataitem_type = CDC_DCL;
  CDC_DCL_TYPE dcl_type;
  char *ptr, *start_ptr;
  int length = 0;
  char *loginfo_buf = NULL;

  int error_code = NO_ERROR;

  switch (log_type)
    {
    case LOG_COMMIT:
      dcl_type = CDC_COMMIT;
      break;
    case LOG_ABORT:
      dcl_type = CDC_ABORT;
      break;
    default:
      assert (false);
      return ER_FAILED;
    }

  cdc_log ("cdc_make_dcl_loginfo : started with trid:%d, transaction user:%s, dcl type:%d", trid, user, dcl_type);
  length =
    (OR_INT_SIZE + OR_INT_SIZE + or_packed_string_length (user, NULL) + OR_INT_SIZE + OR_INT_SIZE + OR_BIGINT_SIZE);

  loginfo_buf = (char *) malloc (length * 2 + MAX_ALIGNMENT);
  if (loginfo_buf == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, length * 2 + MAX_ALIGNMENT);
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      goto error;
    }

  ptr = start_ptr = PTR_ALIGN (loginfo_buf, MAX_ALIGNMENT);
  ptr = or_pack_int (ptr, dcl_entry->length);
  ptr = or_pack_int (ptr, trid);
  ptr = or_pack_string (ptr, user);
  ptr = or_pack_int (ptr, dataitem_type);
  ptr = or_pack_int (ptr, dcl_type);
  ptr = or_pack_int64 (ptr, at_time);
  dcl_entry->length = ptr - start_ptr;
  or_pack_int (start_ptr, dcl_entry->length);

  dcl_entry->log_info = (char *) malloc (dcl_entry->length);
  if (dcl_entry->log_info == NULL)
    {
      cdc_log ("cdc_make_dcl_loginfo : failed to allocate memory for log info in dcl entry", trid, user, dcl_type);

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, dcl_entry->length);
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      goto error;
    }

  memcpy (dcl_entry->log_info, start_ptr, dcl_entry->length);

  free_and_init (loginfo_buf);
  cdc_log ("cdc_make_dcl_loginfo : success to generated dcl log info. length:%d", dcl_entry->length);

  return ER_CDC_LOGINFO_ENTRY_GENERATED;

error:

  if (loginfo_buf != NULL)
    {
      free_and_init (loginfo_buf);
    }

  return error_code;
}

static int
cdc_make_timer_loginfo (time_t at_time, int trid, char *user, CDC_LOGINFO_ENTRY * timer_entry)
{
  CDC_DATAITEM_TYPE dataitem_type = CDC_TIMER;

  char *ptr, *start_ptr;
  int length = 0;
  length = (OR_INT_SIZE + OR_INT_SIZE + or_packed_string_length (user, NULL) + OR_INT_SIZE + OR_BIGINT_SIZE);
  char *loginfo_buf = NULL;

  int error_code = NO_ERROR;

  loginfo_buf = (char *) malloc (length * 2 + MAX_ALIGNMENT);
  if (loginfo_buf == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, length * 2 + MAX_ALIGNMENT);
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      goto error;
    }

  ptr = start_ptr = PTR_ALIGN (loginfo_buf, MAX_ALIGNMENT);
  ptr = or_pack_int (ptr, timer_entry->length);
  ptr = or_pack_int (ptr, trid);
  ptr = or_pack_string (ptr, user);
  ptr = or_pack_int (ptr, dataitem_type);
  ptr = or_pack_int64 (ptr, (INT64) at_time);
  timer_entry->length = ptr - start_ptr;
  or_pack_int (start_ptr, timer_entry->length);

  timer_entry->log_info = (char *) malloc (timer_entry->length);
  if (timer_entry->log_info == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, timer_entry->length);
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      goto error;
    }

  memcpy (timer_entry->log_info, start_ptr, timer_entry->length);

  free_and_init (loginfo_buf);

  cdc_log ("cdc_make_timer_loginfo : success to generated timer log info. length:%d", timer_entry->length);

  return ER_CDC_LOGINFO_ENTRY_GENERATED;

error:

  if (loginfo_buf != NULL)
    {
      free_and_init (loginfo_buf);
    }

  return error_code;
}

/*
 * cdc_find_user - find a user name who performed the specified transaction(trid).
 *
 * return: NO_ERROR if user name is found
 *
 * thread_p (in)    : cdc worker(log info producer) thread
 * process_lsa (in) : log lsa from which to start traversal to find user information
 * trid (in)        : identifier of the transaction performed by the user
 * user (out)       : transaction user name
 *
 * NOTE: The log storing transaction user (LOG_SUPPLEMENT_TRAN_USER) is logged at the begin
 *       and end of the transaction.
 *       e.g) trx1 : BEGIN - LOG_SUPPLEMENT_TRAN_USER - LOG_SUPPLEMENT_INSERT/UDPATE/DELETE/..
 *       - LOG_SUPPLEMENT_TRAN_USER - LOG_COMMIT
 *
 *       This function is called only when the TRAN_USER logged at the begin of the transaction
 *       cannot be found while traversing the log records logged in one transaction.
 *       So, This function is to find the TRAN_USER log left before commit.
 *       e.g) If the TRAN_USER log at the begin of the transaction is truncated at the time of
 *            performing CDC
 *
 *       if specified transaction is aborted or active, there will be no TRAN_USER to find.
 */

static int
cdc_find_user (THREAD_ENTRY * thread_p, LOG_LSA process_lsa, int trid, char **user)
{
  LOG_PAGE *log_page_p = NULL;
  char log_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT];

  /* if the transaction is committed, then TRAN_USER log is appended and flushed to the disk.
   * So, it is only necessary to traverse up to nxio_lsa (next LSA to be flushed to the disk) */
  LOG_LSA nxio_lsa = log_Gl.append.get_nxio_lsa ();
  LOG_LSA forw_lsa;

  LOG_RECORD_HEADER *log_rec_hdr = NULL;
  LOG_REC_SUPPLEMENT *supplement;
  char *data;

  log_page_p = (LOG_PAGE *) PTR_ALIGN (log_pgbuf, MAX_ALIGNMENT);
  if (logpb_fetch_page (thread_p, &process_lsa, LOG_CS_SAFE_READER, log_page_p) != NO_ERROR)
    {
      logpb_fatal_error (thread_p, false, ARG_FILE_LINE, "cdc_find_user");
      return ER_FAILED;
    }

  while (!LSA_ISNULL (&process_lsa) && LSA_LT (&process_lsa, &nxio_lsa))
    {
      log_rec_hdr = LOG_GET_LOG_RECORD_HEADER (log_page_p, &process_lsa);
      LSA_COPY (&forw_lsa, &log_rec_hdr->forw_lsa);
      LOG_READ_ADD_ALIGN (thread_p, sizeof (*log_rec_hdr), &process_lsa, log_page_p);
      if (log_rec_hdr->type == LOG_SUPPLEMENTAL_INFO && log_rec_hdr->trid == trid)
	{
	  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*supplement), &process_lsa, log_page_p);
	  supplement = (LOG_REC_SUPPLEMENT *) (log_page_p->area + process_lsa.offset);
	  if (supplement->rec_type == LOG_SUPPLEMENT_TRAN_USER)
	    {
	      *user = (char *) malloc (supplement->length + 1);
	      if (*user == NULL)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, supplement->length + 1);
		  return ER_OUT_OF_VIRTUAL_MEMORY;
		}

	      LOG_READ_ADD_ALIGN (thread_p, sizeof (*supplement), &process_lsa, log_page_p);
	      data = (char *) log_page_p->area + process_lsa.offset;
	      memcpy (*user, data, supplement->length);
	      (*user)[supplement->length] = '\0';
	      return NO_ERROR;
	    }
	}
      else if (log_rec_hdr->type == LOG_ABORT && log_rec_hdr->trid == trid)
	{
	  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_CDC_IGNORE_TRANSACTION, 1, trid);
	  return ER_CDC_IGNORE_TRANSACTION;
	}

      /* transaction user information should be logged before commit */
      assert (!(log_rec_hdr->type == LOG_COMMIT && log_rec_hdr->trid == trid));

      if (process_lsa.pageid != forw_lsa.pageid && !LSA_ISNULL (&forw_lsa))
	{
	  if (logpb_fetch_page (thread_p, &forw_lsa, LOG_CS_SAFE_READER, log_page_p) != NO_ERROR)
	    {
	      logpb_fatal_error (thread_p, false, ARG_FILE_LINE, "cdc_find_user");
	      return ER_FAILED;
	    }
	}

      LSA_COPY (&process_lsa, &forw_lsa);
    }

  cdc_log
    ("cdc_find_user : failed to find transaction user for TRANID (%d) because the supplemental log for trasaction user is not logged",
     trid);
  return ER_FAILED;
}

static int
cdc_compare_undoredo_dbvalue (const db_value * new_value, const db_value * old_value)
{
  /* return 1 if different */
  /* return 0 if same */

  assert (new_value != NULL && old_value != NULL);

  if (DB_IS_NULL (new_value) && DB_IS_NULL (old_value))
    {
      return 0;
    }
  else
    {
      return db_value_compare (new_value, old_value) == 0 ? 0 : 1;
    }
}

static int
cdc_put_value_to_loginfo (db_value * new_value, char **data_ptr)
{
  const char *src, *end;
  double d;
  char line[1025] = "\0";
  int line_length = 0;
  int func_type = 0;

  /*DATE, TIME */
  DB_VALUE format;
  DB_VALUE lang_str;
  DB_VALUE result;
  INTL_CODESET format_codeset = LANG_SYS_CODESET;
  const char *date_format = "YYYY-MM-DD";
  const char *datetime_frmt = "YYYY-MM-DD HH24:MI:SS.FF";
  const char *datetimetz_frmt = "YYYY-MM-DD HH24:MI:SS.FF TZH:TZM";
  const char *datetimeltz_frmt = "YYYY-MM-DD HH24:MI:SS.FF TZR";

  const char *time_format = "HH24:MI:SS";
  const char *timestamp_frmt = "YYYY-MM-DD HH24:MI:SS";
  const char *timestamptz_frmt = "YYYY-MM-DD HH24:MI:SS TZH:TZM";
  const char *timestampltz_frmt = "YYYY-MM-DD HH24:MI:SS TZR";
  db_make_int (&lang_str, 1);
  db_make_null (&result);

  char *ptr = *data_ptr;

  if (DB_IS_NULL (new_value))
    {
      cdc_log ("cdc_put_value_to_loginfo : failed due to dbvalue of the data is NULL");
      func_type = 7;
      ptr = or_pack_int (ptr, func_type);
      ptr = or_pack_string (ptr, NULL);
      *data_ptr = ptr;
      /* for alter case . if num of col is changed, there will be NULL db_value inserted */
      return NO_ERROR;		/*error */
    }

  switch (DB_VALUE_TYPE (new_value))
    {
    case DB_TYPE_INTEGER:
      func_type = 0;
      ptr = or_pack_int (ptr, func_type);
      ptr = or_pack_int (ptr, db_get_int (new_value));
      break;

    case DB_TYPE_BIGINT:
      func_type = 1;
      ptr = or_pack_int (ptr, func_type);
      ptr = or_pack_int64 (ptr, db_get_bigint (new_value));
      break;
    case DB_TYPE_SHORT:
      func_type = 4;
      ptr = or_pack_int (ptr, func_type);
      ptr = or_pack_short (ptr, db_get_short (new_value));
      break;
    case DB_TYPE_FLOAT:
      func_type = 2;
      ptr = or_pack_int (ptr, func_type);
      ptr = or_pack_float (ptr, db_get_float (new_value));
      break;
    case DB_TYPE_DOUBLE:
      func_type = 3;
      ptr = or_pack_int (ptr, func_type);
      ptr = or_pack_double (ptr, db_get_double (new_value));
      break;
    case DB_TYPE_NUMERIC:
      numeric_db_value_print (new_value, line);
      func_type = 7;
      ptr = or_pack_int (ptr, func_type);
      ptr = or_pack_string (ptr, line);
      break;
    case DB_TYPE_BIT:
    case DB_TYPE_VARBIT:
      {
	char temp[1024];
	char *result = NULL;
	int length, n, count;
	char *bitstring = NULL;
	func_type = 7;

	length = ((db_get_string_length (new_value) + 3) / 4) + 4;

	if (length <= 1024)
	  {
	    result = temp;
	  }
	else
	  {
	    result = (char *) malloc (length);
	    if (result == NULL)
	      {
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, length);
		return ER_OUT_OF_VIRTUAL_MEMORY;
	      }
	  }

	snprintf (result, 3, "X'");

	if (db_bit_string (new_value, "%X", result + 2, length - 2) != NO_ERROR)
	  {
	    if (result != temp)
	      {
		free_and_init (result);
	      }

	    return ER_FAILED;
	  }

	snprintf (result + length - 2, 2, "'");

	assert ((int) strlen (result) == (length - 1));

	ptr = or_pack_int (ptr, func_type);
	ptr = or_pack_string (ptr, result);

	if (result != temp)
	  {
	    free_and_init (result);
	  }

	break;
      }
    case DB_TYPE_CHAR:
      func_type = 7;

      ptr = or_pack_int (ptr, func_type);
      ptr = or_pack_string_with_length (ptr, db_get_string (new_value), db_get_string_size (new_value) - 1);
      break;
    case DB_TYPE_VARCHAR:
      func_type = 7;
      ptr = or_pack_int (ptr, func_type);
      ptr = or_pack_string (ptr, db_get_string (new_value));
      break;
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
      {
	int size = 0;
	int length = 0;
	char *result = NULL;
	const char *temp_string = NULL;

	temp_string = db_get_nchar (new_value, &length);
	size = db_get_string_size (new_value);

	if (temp_string != NULL)
	  {
	    result = (char *) malloc (size + 4);
	    if (result == NULL)
	      {
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, size + 4);
		return ER_OUT_OF_VIRTUAL_MEMORY;
	      }

	    snprintf (result, size + 3, "N'%s", temp_string);
	    result[size + 2] = '\'';
	    result[size + 3] = '\0';
	  }

	func_type = 7;
	ptr = or_pack_int (ptr, func_type);
	ptr = or_pack_string (ptr, result);

	if (result != NULL)
	  {
	    free_and_init (result);
	  }

	break;
      }
#define TOO_BIG_TO_MATTER       1024
    case DB_TYPE_TIME:
      db_make_char (&format, strlen (time_format), time_format,
		    strlen (time_format), format_codeset, LANG_GET_BINARY_COLLATION (format_codeset));
      db_to_char (new_value, &format, &lang_str, &result, &tp_Char_domain);

      line_length = db_get_string_length (&result);
      strncpy (line, db_get_string (&result), line_length);

      func_type = 7;
      ptr = or_pack_int (ptr, func_type);
      ptr = or_pack_string (ptr, line);

      db_value_clear (&result);

      break;
    case DB_TYPE_TIMESTAMP:
      db_make_char (&format, strlen (timestamp_frmt), timestamp_frmt,
		    strlen (timestamp_frmt), format_codeset, LANG_GET_BINARY_COLLATION (format_codeset));
      db_to_char (new_value, &format, &lang_str, &result, &tp_Char_domain);

      line_length = db_get_string_length (&result);
      strncpy (line, db_get_string (&result), line_length);

      func_type = 7;
      ptr = or_pack_int (ptr, func_type);
      ptr = or_pack_string (ptr, line);

      db_value_clear (&result);

      break;
    case DB_TYPE_DATETIME:
      db_make_char (&format, strlen (datetime_frmt), datetime_frmt,
		    strlen (datetime_frmt), format_codeset, LANG_GET_BINARY_COLLATION (format_codeset));
      db_to_char (new_value, &format, &lang_str, &result, &tp_Char_domain);

      line_length = db_get_string_length (&result);
      strncpy (line, db_get_string (&result), line_length);

      func_type = 7;
      ptr = or_pack_int (ptr, func_type);
      ptr = or_pack_string (ptr, line);

      db_value_clear (&result);

      break;
    case DB_TYPE_TIMESTAMPTZ:
      db_make_char (&format, strlen (timestamptz_frmt), timestamptz_frmt,
		    strlen (timestamptz_frmt), format_codeset, LANG_GET_BINARY_COLLATION (format_codeset));
      db_to_char (new_value, &format, &lang_str, &result, &tp_Char_domain);

      line_length = db_get_string_length (&result);
      strncpy (line, db_get_string (&result), line_length);

      func_type = 7;
      ptr = or_pack_int (ptr, func_type);
      ptr = or_pack_string (ptr, line);

      db_value_clear (&result);

      break;
    case DB_TYPE_DATETIMETZ:
      db_make_char (&format, strlen (datetimetz_frmt), datetimetz_frmt,
		    strlen (datetimetz_frmt), format_codeset, LANG_GET_BINARY_COLLATION (format_codeset));
      db_to_char (new_value, &format, &lang_str, &result, &tp_Char_domain);
      line_length = db_get_string_length (&result);
      strncpy (line, db_get_string (&result), line_length);

      func_type = 7;
      ptr = or_pack_int (ptr, func_type);
      ptr = or_pack_string (ptr, line);

      db_value_clear (&result);

      break;
    case DB_TYPE_TIMESTAMPLTZ:

      db_make_char (&format, strlen (timestampltz_frmt), timestampltz_frmt,
		    strlen (timestampltz_frmt), format_codeset, LANG_GET_BINARY_COLLATION (format_codeset));

      db_to_char (new_value, &format, &lang_str, &result, &tp_Char_domain);

      line_length = db_get_string_length (&result);
      strncpy (line, db_get_string (&result), line_length);

      func_type = 7;
      ptr = or_pack_int (ptr, func_type);
      ptr = or_pack_string (ptr, line);

      db_value_clear (&result);

      break;
    case DB_TYPE_DATETIMELTZ:
      db_make_char (&format, strlen (datetimeltz_frmt), datetimeltz_frmt,
		    strlen (datetimeltz_frmt), format_codeset, LANG_GET_BINARY_COLLATION (format_codeset));

      db_to_char (new_value, &format, &lang_str, &result, &tp_Char_domain);
      line_length = db_get_string_length (&result);
      strncpy (line, db_get_string (&result), line_length);

      func_type = 7;
      ptr = or_pack_int (ptr, func_type);
      ptr = or_pack_string (ptr, line);

      db_value_clear (&result);

      break;
    case DB_TYPE_DATE:

      db_make_char (&format, strlen (date_format), date_format,
		    strlen (date_format), format_codeset, LANG_GET_BINARY_COLLATION (format_codeset));
      db_to_char (new_value, &format, &lang_str, &result, &tp_Char_domain);

      line_length = db_get_string_length (&result);
      strncpy (line, db_get_string (&result), line_length);

      func_type = 7;
      ptr = or_pack_int (ptr, func_type);
      ptr = or_pack_string (ptr, line);

      db_value_clear (&result);

      break;
    case DB_TYPE_MONETARY:
      {
	valcnv_convert_value_to_string (new_value);

	func_type = 7;
	ptr = or_pack_int (ptr, func_type);
	ptr = or_pack_string (ptr, db_get_string (new_value));

	db_value_clear (new_value);
      }
      break;
    case DB_TYPE_NULL:
      /* Can't get here because the DB_IS_NULL test covers DB_TYPE_NULL */
      break;
    case DB_TYPE_VARIABLE:
    case DB_TYPE_SUB:
    case DB_TYPE_DB_VALUE:
      /* make sure line is NULL terminated, may not be necessary line[0] = '\0'; */
      break;
    case DB_TYPE_ENUMERATION:
      if (db_get_enum_string (new_value) == NULL && db_get_enum_short (new_value) != 0)
	{
	  func_type = 4;
	  ptr = or_pack_int (ptr, func_type);
	  ptr = or_pack_short (ptr, db_get_enum_short (new_value));
	}
      else
	{
	  DB_VALUE varchar_val;
	  func_type = 7;
	  /* print enumerations as strings */
	  if (tp_enumeration_to_varchar (new_value, &varchar_val) == NO_ERROR)
	    {
	      ptr = or_pack_int (ptr, func_type);
	      ptr = or_pack_string (ptr, db_get_string (&varchar_val));
	    }
	  else
	    {
	      assert (false);
	    }
	}
      break;
    case DB_TYPE_BLOB:
    case DB_TYPE_CLOB:
      {
	DB_ELO *elo;
	func_type = 7;
	elo = db_get_elo (new_value);
	if (elo != NULL)
	  {
	    if (elo->type == ELO_FBO)
	      {
		assert (elo->locator != NULL);
		ptr = or_pack_int (ptr, func_type);
		ptr = or_pack_string (ptr, elo->locator);
	      }
	    else		/* ELO_LO */
	      {
		/* should not happen for now */
		return ER_FAILED;
	      }
	  }
	else
	  {
	    cdc_log ("cdc_put_value_to_loginfo : Failed to extract LOB File");
	    return ER_FAILED;
	  }
      }

      break;
    case DB_TYPE_OBJECT:
    case DB_TYPE_SET:
    case DB_TYPE_MULTISET:
    case DB_TYPE_SEQUENCE:
    case DB_TYPE_ELO:
    case DB_TYPE_JSON:
    case DB_TYPE_POINTER:
    case DB_TYPE_ERROR:
      func_type = 7;
      ptr = or_pack_int (ptr, func_type);
      ptr = or_pack_string (ptr, NULL);
      *data_ptr = ptr;

      cdc_log ("cdc_put_value_to_loginfo : Not Supported data type %d", DB_VALUE_TYPE (new_value));
      break;
    default:
      /* NB: THERE MUST BE NO DEFAULT CASE HERE. ALL TYPES MUST BE HANDLED! */
      assert (false);
      break;
    }

  *data_ptr = ptr;
  return NO_ERROR;
}

LOG_PAGEID
cdc_min_log_pageid_to_keep ()
{
  return cdc_Gl.consumer.start_lsa.pageid;
}

#if defined (SERVER_MODE)
void
cdc_loginfo_producer_daemon_init ()
{
  assert (cdc_Loginfo_producer_daemon == NULL);

  pthread_mutex_init (&cdc_Gl.producer.lock, NULL);

  pthread_cond_init (&cdc_Gl.producer.wait_cond, NULL);

  LSA_SET_NULL (&cdc_Gl.producer.next_extraction_lsa);

  cdc_Gl.producer.request = CDC_REQUEST_PRODUCER_TO_WAIT;

  /* *INDENT-OFF* */
  cubthread::looper looper = cubthread::looper (std::chrono::milliseconds (10)); /* 주석 처리  */
  cubthread::entry_callable_task *daemon_task = new cubthread::entry_callable_task (cdc_loginfo_producer_execute);

  cdc_Loginfo_producer_daemon = cubthread::get_manager ()->create_daemon (looper, daemon_task, "cdc_loginfo_producer"); 
  /* *INDENT-ON* */
}

void
cdc_daemons_init ()
{
  if (prm_get_integer_value (PRM_ID_SUPPLEMENTAL_LOG) == 0)
    {
      return;
    }

  cdc_Logging = prm_get_bool_value (PRM_ID_CDC_LOGGING_DEBUG);

  cdc_initialize ();

  cdc_loginfo_producer_daemon_init ();
}

void
cdc_daemons_destroy ()
{
  if (prm_get_integer_value (PRM_ID_SUPPLEMENTAL_LOG) == 0 || cdc_Loginfo_producer_daemon == NULL)
    {
      return;
    }

  cdc_kill_producer ();

  /* *INDENT-OFF* */
  cubthread::get_manager ()->destroy_daemon (cdc_Loginfo_producer_daemon);
   /* *INDENT-ON* */

  cdc_finalize ();
}
#endif
void
cdc_pause_producer ()
{
  cdc_log ("cdc_pause_producer : consumer request the producer to pause");

  cdc_Gl.producer.request = CDC_REQUEST_PRODUCER_TO_WAIT;

  while (cdc_Gl.producer.state != CDC_PRODUCER_STATE_WAIT)
    {
      sleep (1);
    }

  cdc_log ("cdc_pause_producer : producer is paused");
}

void
cdc_wakeup_producer ()
{
  cdc_log ("cdc_wakeup_producer : consumer request the producer to wakeup");

  cdc_Gl.producer.request = CDC_REQUEST_PRODUCER_NONE;

  pthread_cond_signal (&cdc_Gl.producer.wait_cond);
}

void
cdc_kill_producer ()
{
  cdc_log ("cdc_kill_producer : consumer request the producer to be dead");
  cdc_Gl.producer.request = CDC_REQUEST_PRODUCER_TO_BE_DEAD;

  while (cdc_Gl.producer.state != CDC_PRODUCER_STATE_DEAD)
    {
      pthread_cond_signal (&cdc_Gl.producer.wait_cond);
      sleep (1);
    }

  cdc_log ("cdc_kill_producer : producer is dead");
}

void
cdc_pause_consumer ()
{
  cdc_log ("cdc_pause_consumer : producer request the consumer to be pause");
  cdc_Gl.consumer.request = CDC_REQUEST_CONSUMER_TO_WAIT;
}

void
cdc_wakeup_consumer ()
{
  cdc_log ("cdc_wakeup_consumer : producer request the consumer to wakeup");
  cdc_Gl.consumer.request = CDC_REQUEST_CONSUMER_TO_RUN;
}

int
cdc_find_lsa (THREAD_ENTRY * thread_p, time_t * extraction_time, LOG_LSA * start_lsa)
{
  /*
   * 1. get volume list
   * 2. get fpage from each volume 
   * 3. get commit/abort/ha_dummy_server_state which contains time from fpage 
   * */
  int begin = log_Gl.hdr.last_deleted_arv_num;
  int end = log_Gl.hdr.nxarv_num - 1;
  char arv_name[PATH_MAX];
  LOG_ARV_HEADER *arv_hdr = NULL;
  int num_arvs = end - begin;

  time_t active_start_time = 0;
  time_t archive_start_time = 0;
  int target_arv_num = -1;

  LOG_LSA ret_lsa = LSA_INITIALIZER;
  bool is_found = false;

  char input_time_buf[CTIME_MAX];
  char output_time_buf[CTIME_MAX];
  time_t input_time = *extraction_time;
  int error = NO_ERROR;

  /*
   * 1. traverse from the latest log volume 
   * 2. when num_arvs > 0, no logic to handle the active log volume 
   * 3. check condition when i = begin while finding target_arv_num 
   */

  /* At first, compare the time in active log volume. */
  error = cdc_get_start_point_from_file (thread_p, -1, &ret_lsa, &active_start_time);
  if (error == ER_FAILED || error == ER_LOG_READ)
    {
      goto end;
    }
  else
    {
      /* NO ERROR */
      if (active_start_time != 0 && active_start_time <= *extraction_time)
	{
	  // active
	  error = cdc_get_lsa_with_start_point (thread_p, extraction_time, &ret_lsa);
	  if (error == NO_ERROR)
	    {
	      LSA_COPY (start_lsa, &ret_lsa);
	      is_found = true;
	    }
	  else if (error == ER_CDC_LSA_NOT_FOUND)
	    {
	      /* input time is too big to find log, then returns latest log */
	      LSA_COPY (start_lsa, &log_Gl.append.prev_lsa);

	      *extraction_time = time (NULL);	/* can not know time of latest log */
	      is_found = true;

	      ctime_r (&input_time, input_time_buf);
	      ctime_r (extraction_time, output_time_buf);
	      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_CDC_ADJUSTED_LSA, 2, input_time_buf, output_time_buf);

	      error = ER_CDC_ADJUSTED_LSA;
	    }
	}
      else
	{
	  /* if not found in active log volume, then traverse archives */
	  if (num_arvs > 0)
	    {
	      /* travers from the latest */
	      for (int i = end; i > begin; i--)
		{
		  error = cdc_get_start_point_from_file (thread_p, i, &ret_lsa, &archive_start_time);
		  if (error != NO_ERROR)
		    {
		      goto end;
		    }

		  if (archive_start_time <= *extraction_time)
		    {
		      target_arv_num = i;
		      break;
		    }
		}

	      if (target_arv_num == -1)
		{
		  /* returns oldest LSA */
		  LSA_COPY (start_lsa, &ret_lsa);
		  *extraction_time = archive_start_time;
		  is_found = true;

		  ctime_r (&input_time, input_time_buf);
		  ctime_r (extraction_time, output_time_buf);
		  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_CDC_ADJUSTED_LSA, 2, input_time_buf,
			  output_time_buf);
		  error = ER_CDC_ADJUSTED_LSA;
		}
	      else
		{
		  if ((error = cdc_get_lsa_with_start_point (thread_p, extraction_time, &ret_lsa)) != NO_ERROR)
		    {

		      ctime_r (&input_time, input_time_buf);
		      ctime_r (extraction_time, output_time_buf);
		      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_CDC_ADJUSTED_LSA, 2, input_time_buf,
			      output_time_buf);
		      error = ER_CDC_ADJUSTED_LSA;
		    }
		  else
		    {
		      error = NO_ERROR;
		    }

		  LSA_COPY (start_lsa, &ret_lsa);
		  is_found = true;
		}
	    }
	  else
	    {
	      /* num_arvs == 0, and active_start_time > input time 
	       * returns oldest LSA in active log volume */
	      if (active_start_time != 0)
		{
		  *extraction_time = active_start_time;
		  LSA_COPY (start_lsa, &ret_lsa);
		  is_found = true;

		  ctime_r (&input_time, input_time_buf);
		  ctime_r (extraction_time, output_time_buf);
		  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_CDC_ADJUSTED_LSA, 2, input_time_buf,
			  output_time_buf);
		  error = ER_CDC_ADJUSTED_LSA;
		}
	      else
		{
		  /* num_arvs ==0 but no time info has been found in active log volume */
		  LSA_COPY (start_lsa, &log_Gl.append.prev_lsa);

		  *extraction_time = time (NULL);	/* can not know time of latest log */
		  is_found = true;

		  ctime_r (&input_time, input_time_buf);
		  ctime_r (extraction_time, output_time_buf);
		  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_CDC_ADJUSTED_LSA, 2, input_time_buf,
			  output_time_buf);
		  error = ER_CDC_ADJUSTED_LSA;
		}
	    }
	}
    }

end:
  if (is_found)
    {
      cdc_log ("cdc_find_lsa : find LOG_LSA (%lld | %d) from time (%lld)", LSA_AS_ARGS (start_lsa), *extraction_time);
    }
  else
    {
      cdc_log ("cdc_find_lsa : failed to find LOG_LSA from time (%lld)", *extraction_time);
    }

  return error;
}

static int
cdc_check_lsa_range (THREAD_ENTRY * thread_p, LOG_LSA * lsa)
{
  LOG_PAGE *hdr_pgptr = NULL;
  LOG_PAGE *log_pgptr = NULL;
  LOG_PHY_PAGEID phy_pageid = NULL_PAGEID;
  char hdr_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT], *aligned_hdr_pgbuf;
  char log_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT], *aligned_log_pgbuf;
  int vdes = NULL_VOLDES;

  int begin = log_Gl.hdr.last_deleted_arv_num + 1;
  int end = log_Gl.hdr.nxarv_num;
  char arv_name[PATH_MAX] = "\0";
  LOG_ARV_HEADER *arv_hdr = NULL;
  int num_arvs = end - begin;

  int error_code = NO_ERROR;

  LOG_LSA first_lsa = LSA_INITIALIZER;
  LOG_LSA nxio_lsa = log_Gl.append.get_nxio_lsa ();

  aligned_log_pgbuf = PTR_ALIGN (log_pgbuf, MAX_ALIGNMENT);
  log_pgptr = (LOG_PAGE *) aligned_log_pgbuf;
  LOG_CS_ENTER_READ_MODE (thread_p);

  if (num_arvs == 0)
    {
      first_lsa.pageid = log_Gl.hdr.fpageid;
      first_lsa.offset = 0;
    }
  else
    {
      LOG_ARCHIVE_CS_ENTER (thread_p);
      aligned_hdr_pgbuf = PTR_ALIGN (hdr_pgbuf, MAX_ALIGNMENT);

      hdr_pgptr = (LOG_PAGE *) aligned_hdr_pgbuf;

      fileio_make_log_archive_name (arv_name, log_Archive_path, log_Prefix, begin);

      if (fileio_is_volume_exist (arv_name) == true)
	{
	  vdes = fileio_mount (thread_p, log_Db_fullname, arv_name, LOG_DBLOG_ARCHIVE_VOLID, false, false);
	  if (vdes != NULL_VOLDES)
	    {
	      if (fileio_read (thread_p, vdes, hdr_pgptr, 0, IO_MAX_PAGE_SIZE) == NULL)
		{
		  fileio_dismount (thread_p, vdes);

		  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_READ, 3, 0LL, 0LL, arv_name);

		  LOG_ARCHIVE_CS_EXIT (thread_p);

		  LOG_CS_EXIT (thread_p);

		  return ER_LOG_READ;
		}

	      arv_hdr = (LOG_ARV_HEADER *) hdr_pgptr->area;
	      if (difftime64 ((time_t) arv_hdr->db_creation, (time_t) log_Gl.hdr.db_creation) != 0)
		{
		  fileio_dismount (thread_p, vdes);
		  LOG_ARCHIVE_CS_EXIT (thread_p);
		  LOG_CS_EXIT (thread_p);

		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_DOESNT_CORRESPOND_TO_DATABASE, 1, arv_name);
		  return ER_LOG_DOESNT_CORRESPOND_TO_DATABASE;
		}

	      first_lsa.pageid = arv_hdr->fpageid;
	      first_lsa.offset = 0;

	      fileio_dismount (thread_p, vdes);
	      LOG_ARCHIVE_CS_EXIT (thread_p);
	    }
	}
      else
	{
	  LOG_ARCHIVE_CS_EXIT (thread_p);

	  first_lsa.pageid = log_Gl.hdr.fpageid;
	  first_lsa.offset = 0;
	}
    }

  LOG_CS_EXIT (thread_p);

  cdc_log ("%s : first log lsa from log volume is (%lld|%d) and last lsa is (%lld|%d). input lsa is (%lld|%d)",
	   __func__, LSA_AS_ARGS (&first_lsa), LSA_AS_ARGS (&nxio_lsa), LSA_AS_ARGS (lsa));

  if (LSA_GE (lsa, &first_lsa) && LSA_LT (lsa, &nxio_lsa))
    {
      return NO_ERROR;
    }
  else
    {
      return ER_CDC_INVALID_LOG_LSA;
    }
}

int
cdc_validate_lsa (THREAD_ENTRY * thread_p, LOG_LSA * lsa)
{
  LOG_RECORD_HEADER *log_rec_header;
  LOG_PAGE *log_page_p = NULL;
  char *log_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT];

  LOG_LSA process_lsa;

  LOG_PAGEID pageid;

  log_page_p = (LOG_PAGE *) PTR_ALIGN (log_pgbuf, MAX_ALIGNMENT);
  log_page_p->hdr.logical_pageid = NULL_PAGEID;
  log_page_p->hdr.offset = NULL_OFFSET;

  int error = NO_ERROR;

  if (LSA_ISNULL (lsa))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CDC_INVALID_LOG_LSA, LSA_AS_ARGS (lsa));
      return ER_CDC_INVALID_LOG_LSA;
    }

  if (lsa->pageid >= LOGPAGEID_MAX)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CDC_INVALID_LOG_LSA, LSA_AS_ARGS (lsa));
      return ER_CDC_INVALID_LOG_LSA;
    }

  if (cdc_check_lsa_range (thread_p, lsa) != NO_ERROR)
    {
      return ER_CDC_INVALID_LOG_LSA;
    }

  cdc_log ("cdc_validate_lsa : fetch page from LOG_LSA (%lld | %d) to validate ", LSA_AS_ARGS (lsa));

  /*fetch log page */
  if (logpb_fetch_page (thread_p, lsa, LOG_CS_SAFE_READER, log_page_p) != NO_ERROR)
    {
      return ER_FAILED;
    }

  process_lsa.pageid = log_page_p->hdr.logical_pageid;
  process_lsa.offset = log_page_p->hdr.offset;
  pageid = log_page_p->hdr.logical_pageid;

  while (process_lsa.pageid == pageid)
    {
      log_rec_header = LOG_GET_LOG_RECORD_HEADER (log_page_p, &process_lsa);

      if (LSA_EQ (&process_lsa, lsa))
	{
	  cdc_log ("cdc_validate_lsa : LOG_LSA (%lld | %d) validation success ", LSA_AS_ARGS (lsa));
	  return NO_ERROR;
	}

      LSA_COPY (&process_lsa, &log_rec_header->forw_lsa);
    }

  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CDC_INVALID_LOG_LSA, LSA_AS_ARGS (lsa));
  return ER_CDC_INVALID_LOG_LSA;
}

int
cdc_set_extraction_lsa (LOG_LSA * lsa)
{
  LSA_COPY (&cdc_Gl.producer.next_extraction_lsa, lsa);
  LSA_COPY (&cdc_Gl.consumer.next_lsa, lsa);

  cdc_log ("cdc_set_extraction_lsa : set LOG_LSA (%lld | %d) to produce ", LSA_AS_ARGS (lsa));

  return NO_ERROR;
}

void
cdc_reinitialize_queue (LOG_LSA * start_lsa)
{
  assert (cdc_Gl.loginfo_queue != NULL);
  CDC_LOGINFO_ENTRY *consume;

  if (cdc_Gl.producer.produced_queue_size == 0)
    {
      cdc_log ("cdc_reinitialize_queue : don't need to be reinitialized");
      goto end;
    }

  cdc_Gl.is_queue_reinitialized = true;

  if (LSA_LT (&cdc_Gl.first_loginfo_queue_lsa, start_lsa) && LSA_GE (&cdc_Gl.last_loginfo_queue_lsa, start_lsa))
    {
      cdc_log
	("cdc_reinitialize_queue : reconstruct existing log info queue to remove the log infos before the LOG_LSA (%lld | %d)",
	 LSA_AS_ARGS (start_lsa));

      LOG_LSA next_consume_lsa = LSA_INITIALIZER;
      LSA_COPY (&next_consume_lsa, &cdc_Gl.first_loginfo_queue_lsa);
      while (LSA_LT (&next_consume_lsa, start_lsa))
	{
	  cdc_Gl.loginfo_queue->consume (consume);
	  cdc_Gl.consumer.consumed_queue_size += consume->length;
	  LSA_COPY (&next_consume_lsa, &consume->next_lsa);

	  if (consume->log_info != NULL)
	    {
	      free_and_init (consume->log_info);
	    }
	}

      cdc_Gl.producer.produced_queue_size -= cdc_Gl.consumer.consumed_queue_size;
      cdc_Gl.consumer.consumed_queue_size = 0;
    }
  else
    {
      cdc_log ("cdc_reinitialize_queue : initialize the whole log infos in the queue");

      while (!cdc_Gl.loginfo_queue->is_empty ())
	{
	  cdc_Gl.loginfo_queue->consume (consume);

	  if (consume->log_info != NULL)
	    {
	      free_and_init (consume->log_info);
	    }
	}
      cdc_Gl.producer.produced_queue_size = 0;
      cdc_Gl.consumer.consumed_queue_size = 0;

          /* *INDENT-OFF* */
    delete cdc_Gl.loginfo_queue;
    cdc_Gl.loginfo_queue = new lockfree::circular_queue <CDC_LOGINFO_ENTRY *> (MAX_CDC_LOGINFO_QUEUE_ENTRY);
          /* *INDENT-ON* */
    }

end:

  cdc_log ("cdc_reinitialize_queue : reinitialize end");
}

/*
 * arv_num (in) : archive log volume number to traverse. If it is -1, then traverse active log volume. 
 * ret_lsa (out) : lsa of the first log which contains time info 
 * time (out) : time of the first log which contains time info  
 */

static int
cdc_get_start_point_from_file (THREAD_ENTRY * thread_p, int arv_num, LOG_LSA * ret_lsa, time_t * time)
{
  char arv_name[PATH_MAX];
  LOG_ARV_HEADER *arv_hdr;
  char hdr_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT], *aligned_hdr_pgbuf;
  char log_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT], *aligned_log_pgbuf;

  LOG_PAGE *hdr_pgptr;
  LOG_PAGE *log_pgptr;
  LOG_PHY_PAGEID phy_pageid = NULL_PAGEID;
  int vdes;

  char ctime_buf[CTIME_MAX];
  int error_code;

  LOG_LSA process_lsa = LSA_INITIALIZER;
  LOG_LSA forw_lsa = LSA_INITIALIZER;
  LOG_LSA cur_log_lsa = LSA_INITIALIZER;

  LOG_RECORD_HEADER *log_rec_header;
  LOG_RECTYPE log_type;
  LOG_REC_DONETIME *donetime;
  LOG_REC_HA_SERVER_STATE *dummy;

  aligned_log_pgbuf = PTR_ALIGN (log_pgbuf, MAX_ALIGNMENT);
  log_pgptr = (LOG_PAGE *) aligned_log_pgbuf;
  LOG_CS_ENTER_READ_MODE (thread_p);

  if (arv_num == -1)
    {
      process_lsa.pageid = log_Gl.hdr.fpageid;
      process_lsa.offset = 0;
    }
  else
    {
      LOG_ARCHIVE_CS_ENTER (thread_p);

      if (log_Gl.archive.vdes != NULL_VOLDES && log_Gl.archive.hdr.arv_num == arv_num)
	{
	  /* if target archive log volume is currenty mounted, then use that */
	  process_lsa.pageid = log_Gl.archive.hdr.fpageid;
	  process_lsa.offset = 0;
	}
      else
	{
	  aligned_hdr_pgbuf = PTR_ALIGN (hdr_pgbuf, MAX_ALIGNMENT);

	  hdr_pgptr = (LOG_PAGE *) aligned_hdr_pgbuf;

	  fileio_make_log_archive_name (arv_name, log_Archive_path, log_Prefix, arv_num);

	  if (fileio_is_volume_exist (arv_name) == true)
	    {
	      vdes = fileio_mount (thread_p, log_Db_fullname, arv_name, LOG_DBLOG_ARCHIVE_VOLID, false, false);
	      if (vdes != NULL_VOLDES)
		{
		  if (fileio_read (thread_p, vdes, hdr_pgptr, 0, IO_MAX_PAGE_SIZE) == NULL)
		    {
		      fileio_dismount (thread_p, vdes);

		      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_READ, 3, 0LL, 0LL, arv_name);

		      LOG_ARCHIVE_CS_EXIT (thread_p);

		      LOG_CS_EXIT (thread_p);

		      return ER_LOG_READ;
		    }

		  arv_hdr = (LOG_ARV_HEADER *) hdr_pgptr->area;
		  if (difftime64 ((time_t) arv_hdr->db_creation, (time_t) log_Gl.hdr.db_creation) != 0)
		    {
		      fileio_dismount (thread_p, vdes);

		      LOG_ARCHIVE_CS_EXIT (thread_p);
		      LOG_CS_EXIT (thread_p);

		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_DOESNT_CORRESPOND_TO_DATABASE, 1, arv_name);
		      return ER_LOG_DOESNT_CORRESPOND_TO_DATABASE;
		    }

		  process_lsa.pageid = arv_hdr->fpageid;
		  process_lsa.offset = 0;

		  fileio_dismount (thread_p, vdes);
		}
	    }
	}

      LOG_ARCHIVE_CS_EXIT (thread_p);
    }

  LOG_CS_EXIT (thread_p);

  if (LSA_ISNULL (&process_lsa))
    {
      /* can not find any LSA from archive log volume */
      assert (!LSA_ISNULL (&process_lsa));

      ctime_r (time, ctime_buf);
      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_CDC_LSA_NOT_FOUND, 1, ctime_buf);

      return ER_CDC_LSA_NOT_FOUND;
    }

  if ((error_code = logpb_fetch_page (thread_p, &process_lsa, LOG_CS_SAFE_READER, log_pgptr)) != NO_ERROR)
    {
      return error_code;
    }

  process_lsa.pageid = log_pgptr->hdr.logical_pageid;
  process_lsa.offset = log_pgptr->hdr.offset;

  while (!LSA_ISNULL (&process_lsa))
    {
      LSA_COPY (&cur_log_lsa, &process_lsa);	// save the current log record lsa

      log_rec_header = LOG_GET_LOG_RECORD_HEADER (log_pgptr, &process_lsa);
      LSA_COPY (&forw_lsa, &log_rec_header->forw_lsa);
      log_type = log_rec_header->type;

      LOG_READ_ADD_ALIGN (thread_p, sizeof (*log_rec_header), &process_lsa, log_pgptr);

      if (log_type == LOG_COMMIT || log_type == LOG_ABORT)
	{
	  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*donetime), &process_lsa, log_pgptr);
	  donetime = (LOG_REC_DONETIME *) (log_pgptr->area + process_lsa.offset);

	  LSA_COPY (ret_lsa, &cur_log_lsa);
	  *time = donetime->at_time;

	  return NO_ERROR;
	}

      if (log_type == LOG_DUMMY_HA_SERVER_STATE)
	{
	  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*dummy), &process_lsa, log_pgptr);
	  dummy = (LOG_REC_HA_SERVER_STATE *) (log_pgptr->area + process_lsa.offset);

	  LSA_COPY (ret_lsa, &cur_log_lsa);
	  *time = dummy->at_time;

	  return NO_ERROR;
	}

      if (process_lsa.pageid != forw_lsa.pageid)
	{
	  if (LSA_ISNULL (&forw_lsa))
	    {
	      ctime_r (time, ctime_buf);
	      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_CDC_LSA_NOT_FOUND, 1, ctime_buf);

	      return ER_CDC_LSA_NOT_FOUND;
	    }

	  if ((error_code = logpb_fetch_page (thread_p, &forw_lsa, LOG_CS_SAFE_READER, log_pgptr)) != NO_ERROR)
	    {
	      return error_code;
	    }
	}

      LSA_COPY (&process_lsa, &forw_lsa);
    }

  ctime_r (time, ctime_buf);
  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_CDC_LSA_NOT_FOUND, 1, ctime_buf);

  return ER_CDC_LSA_NOT_FOUND;
}

/*
 * time (in/out) : Time to compare (in) and actual time of log for start_lsa (out)
 * start_lsa (in/out) : start point (in) and lsa of LOG which is found (out)  
 */

static int
cdc_get_lsa_with_start_point (THREAD_ENTRY * thread_p, time_t * time, LOG_LSA * start_lsa)
{
  LOG_LSA process_lsa;
  LOG_LSA current_lsa;

  LOG_PAGE *log_page_p = NULL;
  char *log_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT];

  LOG_RECORD_HEADER *log_rec_header;
  LOG_RECTYPE log_type;
  LOG_REC_DONETIME *donetime;
  LOG_REC_HA_SERVER_STATE *dummy;
  time_t at_time;

  LOG_LSA forw_lsa;

  log_page_p = (LOG_PAGE *) PTR_ALIGN (log_pgbuf, MAX_ALIGNMENT);
  log_page_p->hdr.logical_pageid = NULL_PAGEID;
  log_page_p->hdr.offset = NULL_OFFSET;

  char ctime_buf[CTIME_MAX];
  int error = NO_ERROR;

  assert (!LSA_ISNULL (start_lsa));
  cdc_log ("%s : start point LSA = %3lld|%3d", __func__, LSA_AS_ARGS (start_lsa));

  LSA_COPY (&process_lsa, start_lsa);

  /*fetch log page */
  if (logpb_fetch_page (thread_p, &process_lsa, LOG_CS_SAFE_READER, log_page_p) != NO_ERROR)
    {
      return ER_FAILED;
    }

  while (!LSA_ISNULL (&process_lsa))
    {
      log_rec_header = LOG_GET_LOG_RECORD_HEADER (log_page_p, &process_lsa);

      LSA_COPY (&current_lsa, &process_lsa);
      LSA_COPY (&forw_lsa, &log_rec_header->forw_lsa);
      log_type = log_rec_header->type;

      LOG_READ_ADD_ALIGN (thread_p, sizeof (*log_rec_header), &process_lsa, log_page_p);

      if (log_type == LOG_COMMIT || log_type == LOG_ABORT)
	{
	  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*donetime), &process_lsa, log_page_p);
	  donetime = (LOG_REC_DONETIME *) (log_page_p->area + process_lsa.offset);
	  if (donetime->at_time >= *time)
	    {
	      *time = donetime->at_time;

	      LSA_COPY (start_lsa, &current_lsa);

	      return NO_ERROR;
	    }
	}

      if (log_type == LOG_DUMMY_HA_SERVER_STATE)
	{
	  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*dummy), &process_lsa, log_page_p);
	  dummy = (LOG_REC_HA_SERVER_STATE *) (log_page_p->area + process_lsa.offset);

	  if (dummy->at_time >= *time)
	    {
	      *time = dummy->at_time;

	      LSA_COPY (start_lsa, &current_lsa);

	      return NO_ERROR;
	    }
	}

      if (process_lsa.pageid != forw_lsa.pageid)
	{
	  if (LSA_ISNULL (&forw_lsa))
	    {
	      ctime_r (time, ctime_buf);
	      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_CDC_LSA_NOT_FOUND, 1, ctime_buf);

	      return ER_CDC_LSA_NOT_FOUND;
	    }

	  if (logpb_fetch_page (thread_p, &forw_lsa, LOG_CS_SAFE_READER, log_page_p) != NO_ERROR)
	    {
	      return ER_FAILED;
	    }
	}

      LSA_COPY (&process_lsa, &forw_lsa);
    }

  ctime_r (time, ctime_buf);
  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_CDC_LSA_NOT_FOUND, 1, ctime_buf);

  return ER_CDC_LSA_NOT_FOUND;
}

int
cdc_get_loginfo_metadata (LOG_LSA * lsa, int *length, int *num_log_info)
{
  LSA_COPY (lsa, &cdc_Gl.consumer.next_lsa);
  *length = cdc_Gl.consumer.log_info_size;
  *num_log_info = cdc_Gl.consumer.num_log_info;

  return NO_ERROR;
}

int
cdc_make_loginfo (THREAD_ENTRY * thread_p, LOG_LSA * start_lsa)
{
  int rv;

  int begin = 0;
  int end = 0;

  char *log_infos = NULL;
  char *temp_log_infos = NULL;

  CDC_LOGINFO_ENTRY *consume;

  int num_log_info = 0;
  int total_length = 0;

  char ctime_buf[CTIME_MAX];

  begin = (int) time (NULL);

  while (cdc_Gl.loginfo_queue->is_empty ())
    {
      sleep (1);
      end = (int) time (NULL);
      if ((end - begin) >= cdc_Gl.consumer.extraction_timeout)
	{
	  time_t elapsed = end - begin;

	  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_CDC_EXTRACTION_TIMEOUT, 2, elapsed,
		  cdc_Gl.consumer.extraction_timeout);

	  return ER_CDC_EXTRACTION_TIMEOUT;
	}
    }

  LSA_COPY (&cdc_Gl.consumer.start_lsa, start_lsa);	/* stores start lsa to consume */
  log_infos = cdc_Gl.consumer.log_info;
  memset (log_infos, 0, cdc_Gl.consumer.log_info_size);

  while (cdc_Gl.loginfo_queue->is_empty () == false && (num_log_info < cdc_Gl.consumer.max_log_item))
    {
      /* *INDENT-OFF* */
      if (cdc_Gl.loginfo_queue->consume (consume) == false)
        {
          /* consume failed, queue is blocked by producer */
          continue;
        }
      /* *INDENT-ON* */

      if (LSA_GE (&consume->next_lsa, start_lsa))
	{
	  if ((int) (total_length + consume->length + MAX_ALIGNMENT) > cdc_Gl.consumer.log_info_size)
	    {
	      temp_log_infos = (char *) realloc (log_infos, total_length + consume->length + MAX_ALIGNMENT);
	      if (temp_log_infos == NULL)
		{
		  goto end;
		}
	      else
		{
		  log_infos = temp_log_infos;
		}
	    }
	  memcpy (PTR_ALIGN (log_infos + total_length, MAX_ALIGNMENT), PTR_ALIGN (consume->log_info, MAX_ALIGNMENT),
		  consume->length);

	  total_length =
	    (PTR_ALIGN (log_infos + total_length, MAX_ALIGNMENT) + consume->length) - PTR_ALIGN (log_infos,
												 MAX_ALIGNMENT);

	  num_log_info++;

	  LSA_COPY (&cdc_Gl.first_loginfo_queue_lsa, &consume->next_lsa);
	  LSA_COPY (start_lsa, &consume->next_lsa);

	  cdc_Gl.consumer.consumed_queue_size += consume->length;

	  if (consume->log_info != NULL)
	    {
	      free_and_init (consume->log_info);
	    }

	  if (consume != NULL)
	    {
	      free_and_init (consume);
	    }
	}

      end = (int) time (NULL);
      if ((end - begin) >= cdc_Gl.consumer.extraction_timeout)
	{
	  cdc_log ("cdc_make_loginfo : finished extraction due to extraction timeout (%lld / %lld)", end - begin,
		   cdc_Gl.consumer.extraction_timeout);
	  goto end;
	}
    }

end:

  cdc_Gl.consumer.log_info = log_infos;
  cdc_Gl.consumer.log_info_size = total_length;
  cdc_Gl.consumer.num_log_info = num_log_info;
  LSA_COPY (&cdc_Gl.consumer.next_lsa, start_lsa);	/* stores next lsa to consume */

  if (cdc_Gl.consumer.request == CDC_REQUEST_CONSUMER_TO_WAIT)
    {
      cdc_log ("cdc_make_loginfo : consumer is requested to wait");

      while (cdc_Gl.consumer.consumed_queue_size != 0)
	{
	  cdc_wakeup_producer ();
	}
    }

//  if producer status is wait, and producer queue size is over the limit 
  cdc_log
    ("cdc_make_loginfo : consume the log info entry in the queue and send to the requester.\nnumber of loginfos:(%d), total length of loginfos:(%d), next LOG_LSA to consume:(%lld | %d).",
     cdc_Gl.consumer.num_log_info, cdc_Gl.consumer.log_info_size, LSA_AS_ARGS (&cdc_Gl.consumer.next_lsa));

  return NO_ERROR;
}

int
cdc_initialize ()
{
  cdc_Gl.conn.fd = -1;
  cdc_Gl.conn.status = CONN_CLOSED;

  cdc_Gl.producer.extraction_user = NULL;
  cdc_Gl.producer.extraction_classoids = NULL;

  cdc_Gl.producer.request = CDC_REQUEST_PRODUCER_NONE;
  cdc_Gl.consumer.request = CDC_REQUEST_CONSUMER_NONE;
  cdc_Gl.producer.state = CDC_PRODUCER_STATE_DEAD;

  /* *INDENT-OFF* */
  cdc_Gl.loginfo_queue = new lockfree::circular_queue <CDC_LOGINFO_ENTRY *> (MAX_CDC_LOGINFO_QUEUE_ENTRY);
  /* *INDENT-ON* */

  cdc_Gl.consumer.consumed_queue_size = 0;
  cdc_Gl.producer.produced_queue_size = 0;

  LSA_SET_NULL (&cdc_Gl.first_loginfo_queue_lsa);
  LSA_SET_NULL (&cdc_Gl.last_loginfo_queue_lsa);

  cdc_Gl.producer.temp_logbuf[0].log_page_p =
    (LOG_PAGE *) PTR_ALIGN (cdc_Gl.producer.temp_logbuf[0].log_page, MAX_ALIGNMENT);
  cdc_Gl.producer.temp_logbuf[1].log_page_p =
    (LOG_PAGE *) PTR_ALIGN (cdc_Gl.producer.temp_logbuf[1].log_page, MAX_ALIGNMENT);

  /*communication buffer from server to client initialization */
  cdc_Gl.consumer.log_info = NULL;
  cdc_Gl.consumer.log_info_size = 0;
  cdc_Gl.consumer.log_info_buf_size = 0;

  cdc_Gl.consumer.num_log_info = 0;

  LSA_SET_NULL (&cdc_Gl.consumer.start_lsa);
  LSA_SET_NULL (&cdc_Gl.consumer.next_lsa);

  return 0;
}

int
cdc_free_extraction_filter ()
{
  if (cdc_Gl.producer.extraction_user != NULL)
    {
      for (int i = 0; i < cdc_Gl.producer.num_extraction_user; i++)
	{
	  if (cdc_Gl.producer.extraction_user[i] != NULL)
	    {
	      free_and_init (cdc_Gl.producer.extraction_user[i]);
	    }
	}

      free_and_init (cdc_Gl.producer.extraction_user);
    }

  if (cdc_Gl.producer.extraction_classoids != NULL)
    {
      free_and_init (cdc_Gl.producer.extraction_classoids);
    }
  return NO_ERROR;
}

/* if client request for session end, it clean up all data structure */
int
cdc_cleanup ()
{
  cdc_log ("cdc_cleanup () : cleanup start");

  if (cdc_Gl.producer.state != CDC_PRODUCER_STATE_WAIT)
    {
      cdc_pause_producer ();
    }

  cdc_free_extraction_filter ();

  assert (cdc_Gl.loginfo_queue != NULL);

  while (!cdc_Gl.loginfo_queue->is_empty ())
    {
      CDC_LOGINFO_ENTRY *tmp;
      cdc_Gl.loginfo_queue->consume (tmp);

      if (tmp->log_info != NULL)
	{
	  free (tmp->log_info);
	}

      if (tmp != NULL)
	{
	  free (tmp);
	}
    }

  cdc_Gl.consumer.consumed_queue_size = 0;
  cdc_Gl.producer.produced_queue_size = 0;

  LSA_SET_NULL (&cdc_Gl.first_loginfo_queue_lsa);
  LSA_SET_NULL (&cdc_Gl.last_loginfo_queue_lsa);

  LSA_SET_NULL (&cdc_Gl.producer.next_extraction_lsa);

  /*communication buffer from server to client initialization */
  cdc_cleanup_consumer ();

  cdc_log ("cdc_cleanup () : cleanup end");
  return NO_ERROR;
}

void
cdc_cleanup_consumer ()
{
  if (cdc_Gl.consumer.log_info_size != 0)
    {
      cdc_Gl.consumer.log_info_size = 0;
      cdc_Gl.consumer.num_log_info = 0;

      if (cdc_Gl.consumer.log_info != NULL)
	{
	  free_and_init (cdc_Gl.consumer.log_info);
	}
    }

  LSA_SET_NULL (&cdc_Gl.consumer.start_lsa);
  LSA_SET_NULL (&cdc_Gl.consumer.next_lsa);
}

int
cdc_finalize ()
{
  int i = 0;

  cdc_log ("cdc_finalize () : finalize start");

  cdc_free_extraction_filter ();

/* *INDENT-OFF* */
  for (auto iter:cdc_Gl.producer.tran_user)
    {
      if (iter.second != NULL)
      {
        free_and_init (iter.second);
      }
    }
/* *INDENT-ON* */

  if (cdc_Gl.loginfo_queue != NULL)
    {
      while (!cdc_Gl.loginfo_queue->is_empty ())
	{
	  CDC_LOGINFO_ENTRY *tmp;
	  cdc_Gl.loginfo_queue->consume (tmp);

	  if (tmp->log_info != NULL)
	    {
	      free_and_init (tmp->log_info);
	    }

	  if (tmp != NULL)
	    {
	      free_and_init (tmp);
	    }
	}

          /* *INDENT-OFF* */
      delete cdc_Gl.loginfo_queue;
          /* *INDENT-ON* */
      cdc_Gl.loginfo_queue = NULL;
    }

  cdc_Gl.consumer.consumed_queue_size = 0;
  cdc_Gl.producer.produced_queue_size = 0;

  LSA_SET_NULL (&cdc_Gl.producer.next_extraction_lsa);
  LSA_SET_NULL (&cdc_Gl.last_loginfo_queue_lsa);
  LSA_SET_NULL (&cdc_Gl.first_loginfo_queue_lsa);

  cdc_log ("cdc_finalize () : finalize end");

  return NO_ERROR;
}

int
cdc_set_configuration (int max_log_item, int timeout, int all_in_cond, char **user, int num_user,
		       uint64_t * classoids, int num_class)
{
  /* if CDC client exits abnomaly, extraction user and classoids are not freed. 
   * So, reconnection requires these variables to be reset */
  cdc_free_extraction_filter ();

  cdc_Gl.consumer.extraction_timeout = timeout;
  cdc_Gl.consumer.max_log_item = max_log_item;
  cdc_Gl.producer.all_in_cond = all_in_cond;

  cdc_Gl.producer.extraction_user = user;
  cdc_Gl.producer.num_extraction_user = num_user;

  cdc_Gl.producer.extraction_classoids = classoids;
  cdc_Gl.producer.num_extraction_class = num_class;

  return NO_ERROR;
}

static bool
cdc_is_filtered_class (OID classoid)
{
  int i = 0;
  uint64_t b_classoid;
  memcpy (&b_classoid, &classoid, sizeof (uint64_t));

  if (cdc_Gl.producer.num_extraction_class == 0)
    {
      return true;
    }

  for (i = 0; i < cdc_Gl.producer.num_extraction_class; i++)
    {
      if (cdc_Gl.producer.extraction_classoids[i] == b_classoid)
	{
	  return true;
	}
    }

  return false;
}

static bool
cdc_is_filtered_user (char *user)
{
  int i = 0;

  if (cdc_Gl.producer.num_extraction_user == 0)
    {
      return true;
    }

  for (i = 0; i < cdc_Gl.producer.num_extraction_user; i++)
    {
      if (strcmp (cdc_Gl.producer.extraction_user[i], user) == 0)
	{
	  return true;
	}
    }

  return false;
}

//
// log critical section
//

void
LOG_CS_ENTER (THREAD_ENTRY * thread_p)
{
#if defined (SERVER_MODE)
  if (csect_enter (thread_p, CSECT_LOG, INF_WAIT) != NO_ERROR)
    {
      assert (false);
    }
#endif
}

void
LOG_CS_ENTER_READ_MODE (THREAD_ENTRY * thread_p)
{
#if defined (SERVER_MODE)
  if (csect_enter_as_reader (thread_p, CSECT_LOG, INF_WAIT) != NO_ERROR)
    {
      assert (false);
    }
#endif
}

void
LOG_CS_EXIT (THREAD_ENTRY * thread_p)
{
#if defined (SERVER_MODE)
  if (csect_exit (thread_p, CSECT_LOG) != NO_ERROR)
    {
      assert (false);
    }
#endif
}

void
LOG_CS_DEMOTE (THREAD_ENTRY * thread_p)
{
#if defined (SERVER_MODE)
  if (csect_demote (thread_p, CSECT_LOG, INF_WAIT) != NO_ERROR)
    {
      assert (false);
    }
#endif
}

void
LOG_CS_PROMOTE (THREAD_ENTRY * thread_p)
{
#if defined (SERVER_MODE)
  if (csect_promote (thread_p, CSECT_LOG, INF_WAIT) != NO_ERROR)
    {
      assert (false);
    }
#endif
}

bool
LOG_CS_OWN (THREAD_ENTRY * thread_p)
{
#if defined (SERVER_MODE)
  /* TODO: Vacuum workers never hold CSECT_LOG lock. Investigate any possible
   *     unwanted consequences.
   * NOTE: It is considered that a vacuum worker holds a "shared" lock.
   * TODO: remove vacuum code from LOG_CS_OWN
   */
  return vacuum_is_process_log_for_vacuum (thread_p) || (csect_check_own (thread_p, CSECT_LOG) >= 1);
#else // not server mode
  return true;
#endif // not server mode
}

bool
LOG_CS_OWN_WRITE_MODE (THREAD_ENTRY * thread_p)
{
#if defined (SERVER_MODE)
  return csect_check_own (thread_p, CSECT_LOG) == 1;
#else // not server mode
  return true;
#endif // not server mode
}
