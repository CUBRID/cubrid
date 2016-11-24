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
 * log_impl.h -
 *
 */

#ifndef _LOG_IMPL_H_
#define _LOG_IMPL_H_

#ident "$Id$"

#include "config.h"

#include <signal.h>
#include <assert.h>

#include "storage_common.h"
#include "mvcc.h"
#include "log_comm.h"
#include "recovery.h"
#include "porting.h"
#include "boot.h"
#include "critical_section.h"
#include "release_string.h"
#include "file_io.h"
#include "thread.h"
#include "es.h"
#include "rb_tree.h"
#include "query_list.h"
#include "lock_manager.h"
#include "connection_globals.h"
#include "vacuum.h"

#if defined(SOLARIS)
#include <netdb.h>		/* for MAXHOSTNAMELEN */
#endif /* SOLARIS */

#define NUM_NORMAL_TRANS (prm_get_integer_value (PRM_ID_CSS_MAX_CLIENTS))
#define NUM_SYSTEM_TRANS 1
#define NUM_NON_SYSTEM_TRANS (css_get_max_conn ())
#define MAX_NTRANS \
  (NUM_NON_SYSTEM_TRANS + NUM_SYSTEM_TRANS)

/* TRANS_STATUS_HISTORY_MAX_SIZE must be a power of 2*/
#define TRANS_STATUS_HISTORY_MAX_SIZE 2048

#if defined(SERVER_MODE)
#define LOG_CS_ENTER(thread_p) \
        csect_enter((thread_p), CSECT_LOG, INF_WAIT)
#define LOG_CS_ENTER_READ_MODE(thread_p) \
        csect_enter_as_reader((thread_p), CSECT_LOG, INF_WAIT)
#define LOG_CS_DEMOTE(thread_p) \
        csect_demote((thread_p), CSECT_LOG, INF_WAIT)
#define LOG_CS_PROMOTE(thread_p) \
        csect_promote((thread_p), CSECT_LOG, INF_WAIT)
#define LOG_CS_EXIT(thread_p) \
        csect_exit((thread_p), CSECT_LOG)

#define TR_TABLE_CS_ENTER(thread_p) \
        csect_enter((thread_p), CSECT_TRAN_TABLE, INF_WAIT)
#define TR_TABLE_CS_ENTER_READ_MODE(thread_p) \
        csect_enter_as_reader((thread_p), CSECT_TRAN_TABLE, INF_WAIT)
#define TR_TABLE_CS_EXIT(thread_p) \
        csect_exit((thread_p), CSECT_TRAN_TABLE)

#define LOG_ARCHIVE_CS_ENTER(thread_p) \
        csect_enter (thread_p, CSECT_LOG_ARCHIVE, INF_WAIT)
#define LOG_ARCHIVE_CS_ENTER_READ_MODE(thread_p) \
        csect_enter_as_reader (thread_p, CSECT_LOG_ARCHIVE, INF_WAIT)
#define LOG_ARCHIVE_CS_EXIT(thread_p) \
        csect_exit (thread_p, CSECT_LOG_ARCHIVE)

#else /* SERVER_MODE */
#define LOG_CS_ENTER(thread_p)
#define LOG_CS_ENTER_READ_MODE(thread_p)
#define LOG_CS_DEMOTE(thread_p)
#define LOG_CS_PROMOTE(thread_p)
#define LOG_CS_EXIT(thread_p)

#define TR_TABLE_CS_ENTER(thread_p)
#define TR_TABLE_CS_ENTER_READ_MODE(thread_p)
#define TR_TABLE_CS_EXIT(thread_p)

#define LOG_ARCHIVE_CS_ENTER(thread_p)
#define LOG_ARCHIVE_CS_ENTER_READ_MODE(thread_p)
#define LOG_ARCHIVE_CS_EXIT(thread_p)
#endif /* SERVER_MODE */

#if defined(SERVER_MODE)
/* TODO: Vacuum workers never hold CSECT_LOG lock. Investigate any possible
 *	 unwanted consequences.
 * NOTE: It is considered that a vacuum worker holds a "shared" lock.
 */
#define LOG_CS_OWN(thread_p) \
  (VACUUM_IS_PROCESS_LOG_FOR_VACUUM (thread_p) \
   || csect_check_own (thread_p, CSECT_LOG) >= 1)
#define LOG_CS_OWN_WRITE_MODE(thread_p) \
  (csect_check_own (thread_p, CSECT_LOG) == 1)

#define LOG_ARCHIVE_CS_OWN(thread_p) \
  (csect_check (thread_p, CSECT_LOG_ARCHIVE) >= 1)
#define LOG_ARCHIVE_CS_OWN_WRITE_MODE(thread_p) \
  (csect_check_own (thread_p, CSECT_LOG_ARCHIVE) == 1)
#define LOG_ARCHIVE_CS_OWN_READ_MODE(thread_p) \
  (csect_check_own (thread_p, CSECT_LOG_ARCHIVE) == 2)

#else /* SERVER_MODE */
#define LOG_CS_OWN(thread_p) (true)
#define LOG_CS_OWN_WRITE_MODE(thread_p) (true)

#define LOG_ARCHIVE_CS_OWN(thread_p) (true)
#define LOG_ARCHIVE_CS_OWN_WRITE_MODE(thread_p) (true)
#define LOG_ARCHIVE_CS_OWN_READ_MODE(thread_p) (true)
#endif /* !SERVER_MODE */

#define LOG_ESTIMATE_NACTIVE_TRANS      100	/* Estimate num of trans */
#define LOG_ESTIMATE_NOBJ_LOCKS         977	/* Estimate num of locks */

/* Log data area */
#define LOGAREA_SIZE (LOG_PAGESIZE - SSIZEOF(LOG_HDRPAGE))

/* check if group commit is active */
#define LOG_IS_GROUP_COMMIT_ACTIVE() \
  (prm_get_integer_value (PRM_ID_LOG_GROUP_COMMIT_INTERVAL_MSECS) > 0)

#define LOG_RESET_APPEND_LSA(lsa) \
  do \
    { \
      LSA_COPY (&log_Gl.hdr.append_lsa, (lsa)); \
      LSA_COPY (&log_Gl.prior_info.prior_lsa, (lsa)); \
    } \
  while (0)

#define LOG_RESET_PREV_LSA(lsa) \
  do \
    { \
      LSA_COPY (&log_Gl.append.prev_lsa, (lsa)); \
      LSA_COPY (&log_Gl.prior_info.prev_lsa, (lsa)); \
    } \
  while (0)

#define LOG_APPEND_PTR() ((char *)log_Gl.append.log_pgptr->area \
                          + log_Gl.hdr.append_lsa.offset)

#define LOG_GET_LOG_RECORD_HEADER(log_page_p, lsa) \
  ((LOG_RECORD_HEADER *) ((log_page_p)->area + (lsa)->offset))

#define LOG_READ_ALIGN(thread_p, lsa, log_pgptr) \
  do \
    { \
      (lsa)->offset = DB_ALIGN ((lsa)->offset, DOUBLE_ALIGNMENT); \
      while ((lsa)->offset >= (int) LOGAREA_SIZE) \
        { \
          assert (log_pgptr != NULL); \
          (lsa)->pageid++; \
          if (logpb_fetch_page ((thread_p), (lsa), LOG_CS_FORCE_USE, (log_pgptr)) != NO_ERROR) \
	    { \
              logpb_fatal_error ((thread_p), true, ARG_FILE_LINE, \
                                 "LOG_READ_ALIGN"); \
	    } \
          (lsa)->offset -= LOGAREA_SIZE; \
          (lsa)->offset = DB_ALIGN ((lsa)->offset, DOUBLE_ALIGNMENT); \
        } \
    } \
  while (0)

#define LOG_READ_ADD_ALIGN(thread_p, add, lsa, log_pgptr) \
  do \
    { \
      (lsa)->offset += (add); \
      LOG_READ_ALIGN ((thread_p), (lsa), (log_pgptr)); \
    } \
  while (0)

#define LOG_READ_ADVANCE_WHEN_DOESNT_FIT(thread_p, length, lsa, log_pgptr) \
  do \
    { \
      if ((lsa)->offset + (int) (length) >= (int) LOGAREA_SIZE) \
        { \
          assert (log_pgptr != NULL); \
          (lsa)->pageid++; \
          if ((logpb_fetch_page ((thread_p), (lsa), LOG_CS_FORCE_USE, log_pgptr))!= NO_ERROR) \
            { \
              logpb_fatal_error ((thread_p), true, ARG_FILE_LINE, \
                                 "LOG_READ_ADVANCE_WHEN_DOESNT_FIT"); \
            } \
          (lsa)->offset = 0; \
        } \
    } \
  while (0)

#define LOG_2PC_NULL_GTRID        (-1)
#define LOG_2PC_OBTAIN_LOCKS      true
#define LOG_2PC_DONT_OBTAIN_LOCKS false

#define LOG_SYSTEM_TRAN_INDEX 0	/* The recovery & vacuum worker system transaction index. */
#define LOG_SYSTEM_TRANID     0	/* The recovery & vacuum worker system transaction. */

#if defined(SERVER_MODE)
#if !defined(LOG_FIND_THREAD_TRAN_INDEX)
#define LOG_FIND_THREAD_TRAN_INDEX(thrd) \
  ((thrd) ? (thrd)->tran_index : thread_get_current_tran_index())
#endif
#define LOG_SET_CURRENT_TRAN_INDEX(thrd, index) \
  ((thrd) ? (thrd)->tran_index = (index) : \
            thread_set_current_tran_index ((thrd), (index)))
#else /* SERVER_MODE */
#if !defined(LOG_FIND_THREAD_TRAN_INDEX)
#define LOG_FIND_THREAD_TRAN_INDEX(thrd) (log_Tran_index)
#endif
#define LOG_SET_CURRENT_TRAN_INDEX(thrd, index) \
  log_Tran_index = (index)
#endif /* SERVER_MODE */

#define LOG_FIND_TDES(tran_index) \
  (((tran_index) >= 0 && (tran_index) < log_Gl.trantable.num_total_indices) \
   ? log_Gl.trantable.all_tdes[(tran_index)] : NULL)

#define LOG_FIND_TRAN_LOWEST_ACTIVE_MVCCID(tran_index) \
  (((tran_index) >= 0 && (tran_index) < log_Gl.trantable.num_total_indices) \
  ? (log_Gl.mvcc_table.transaction_lowest_active_mvccids + tran_index) : NULL)

#define LOG_FIND_CURRENT_TDES(thrd) \
  LOG_FIND_TDES (LOG_FIND_THREAD_TRAN_INDEX ((thrd)))

#define LOG_ISTRAN_ACTIVE(tdes) \
  ((tdes)->state == TRAN_ACTIVE && LOG_ISRESTARTED ())

#define LOG_ISTRAN_COMMITTED(tdes) \
  ((tdes)->state == TRAN_UNACTIVE_COMMITTED \
   || (tdes)->state == TRAN_UNACTIVE_WILL_COMMIT \
   || (tdes)->state == TRAN_UNACTIVE_COMMITTED_WITH_POSTPONE \
   || (tdes)->state == TRAN_UNACTIVE_2PC_COMMIT_DECISION \
   || (tdes)->state == TRAN_UNACTIVE_COMMITTED_INFORMING_PARTICIPANTS)

#define LOG_ISTRAN_ABORTED(tdes) \
  ((tdes)->state == TRAN_UNACTIVE_ABORTED \
   || (tdes)->state == TRAN_UNACTIVE_UNILATERALLY_ABORTED \
   || (tdes)->state == TRAN_UNACTIVE_2PC_ABORT_DECISION \
   || (tdes)->state == TRAN_UNACTIVE_ABORTED_INFORMING_PARTICIPANTS)

#define LOG_ISTRAN_LOOSE_ENDS(tdes) \
  ((tdes)->state == TRAN_UNACTIVE_COMMITTED_INFORMING_PARTICIPANTS \
   || (tdes)->state == TRAN_UNACTIVE_ABORTED_INFORMING_PARTICIPANTS \
   || (tdes)->state == TRAN_UNACTIVE_2PC_COLLECTING_PARTICIPANT_VOTES \
   || (tdes)->state == TRAN_UNACTIVE_2PC_PREPARE)

#define LOG_ISTRAN_2PC_IN_SECOND_PHASE(tdes) \
  ((tdes)->state == TRAN_UNACTIVE_2PC_ABORT_DECISION \
   || (tdes)->state == TRAN_UNACTIVE_2PC_COMMIT_DECISION \
   || (tdes)->state == TRAN_UNACTIVE_WILL_COMMIT \
   || (tdes)->state == TRAN_UNACTIVE_COMMITTED_WITH_POSTPONE \
   || (tdes)->state == TRAN_UNACTIVE_ABORTED_INFORMING_PARTICIPANTS \
   || (tdes)->state == TRAN_UNACTIVE_COMMITTED_INFORMING_PARTICIPANTS)

#define LOG_ISTRAN_2PC(tdes) \
  ((tdes)->state == TRAN_UNACTIVE_2PC_COLLECTING_PARTICIPANT_VOTES \
   || (tdes)->state == TRAN_UNACTIVE_2PC_PREPARE \
   || LOG_ISTRAN_2PC_IN_SECOND_PHASE (tdes))

#define LOG_ISTRAN_2PC_PREPARE(tdes) \
  ((tdes)->state == TRAN_UNACTIVE_2PC_PREPARE)

#define LOG_ISTRAN_2PC_INFORMING_PARTICIPANTS(tdes) \
  ((tdes)->state == TRAN_UNACTIVE_COMMITTED_INFORMING_PARTICIPANTS \
   || (tdes)->state == TRAN_UNACTIVE_ABORTED_INFORMING_PARTICIPANTS)

/* Reserved vacuum workers transaction identifiers.
 * Vacuum workers each need one TRANID to have their system operations
 * isolated and identifiable. Even though usually vacuum workers never undo
 * their work, system operations still need to be undone (if server crashes
 * in the middle of the operation).
 * For this reason, the first VACUUM_MAX_WORKER_COUNT negative TRANID values
 * under NULL_TRANID are reserved for vacuum workers.
 */
#define LOG_VACUUM_MASTER_TRANID (NULL_TRANID - 1)
#define LOG_LAST_VACUUM_WORKER_TRANID (LOG_VACUUM_MASTER_TRANID - 1)
#define LOG_FIRST_VACUUM_WORKER_TRANID (LOG_VACUUM_MASTER_TRANID - VACUUM_MAX_WORKER_COUNT)
#define LOG_IS_VACUUM_WORKER_TRANID(trid) \
  (trid <= LOG_LAST_VACUUM_WORKER_TRANID \
   && trid >= LOG_FIRST_VACUUM_WORKER_TRANID)
#define LOG_IS_VACUUM_MASTER_TRANID(trid) ((trid) == LOG_VACUUM_MASTER_TRANID)
#define LOG_IS_VACUUM_THREAD_TRANID(trid) \
  (LOG_IS_VACUUM_WORKER_TRANID (trid) || LOG_IS_VACUUM_MASTER_TRANID (trid))

#define LOG_SET_DATA_ADDR(data_addr, page, vol_file_id, off) \
  do \
    { \
      (data_addr)->pgptr = (page); \
      (data_addr)->vfid = (vol_file_id); \
      (data_addr)->offset = (off); \
    } \
  while (0)

#define MAXLOGNAME          (30 - 12)

#define LOGPB_HEADER_PAGE_ID             (-9)	/* The first log page in the infinite log sequence. It is always kept
						 * on the active portion of the log. Log records are not stored on this
						 * page. This page is backed up in all archive logs */
#define LOGPB_IO_NPAGES                  4

#define LOGPB_BUFFER_NPAGES_LOWER        128

#define LOG_READ_NEXT_TRANID (log_Gl.hdr.next_trid)
#define LOG_READ_NEXT_MVCCID (log_Gl.hdr.mvcc_next_id)
#define LOG_HAS_LOGGING_BEEN_IGNORED() \
  (log_Gl.hdr.has_logging_been_skipped == true)

#define LOG_ISRESTARTED() (log_Gl.rcv_phase == LOG_RESTARTED)

/* special action for log applier */
#if defined (SERVER_MODE)
#define LOG_CHECK_LOG_APPLIER(thread_p) \
  (thread_p != NULL \
   && logtb_find_client_type (thread_p->tran_index) == BOOT_CLIENT_LOG_APPLIER)
#else
#define LOG_CHECK_LOG_APPLIER(thread_p) (0)
#endif /* !SERVER_MODE */

/* special action for log prefetcher */
#if defined (SERVER_MODE)
#define LOG_CHECK_LOG_PREFETCHER(thread_p) \
  (thread_p != NULL \
   && logtb_find_client_type (thread_p->tran_index) == BOOT_CLIENT_LOG_PREFETCHER)
#else
#define LOG_CHECK_LOG_PREFETCHER(thread_p) (0)
#endif /* !SERVER_MODE */

#if !defined(_DB_DISABLE_MODIFICATIONS_)
#define _DB_DISABLE_MODIFICATIONS_
extern int db_Disable_modifications;
#endif /* _DB_DISABLE_MODIFICATIONS_ */

#ifndef CHECK_MODIFICATION_NO_RETURN
#if defined (SA_MODE)
#define CHECK_MODIFICATION_NO_RETURN(thread_p, error) \
  (error) = NO_ERROR
#else /* SA_MODE */
#define CHECK_MODIFICATION_NO_RETURN(thread_p, error) \
  do \
    { \
      int mod_disabled; \
      mod_disabled = logtb_is_tran_modification_disabled (thread_p); \
      if (mod_disabled) \
        { \
          er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DB_NO_MODIFICATIONS, \
                  0); \
          er_log_debug (ARG_FILE_LINE, "tdes->disable_modification = %d\n", \
                        mod_disabled); \
          error = ER_DB_NO_MODIFICATIONS; \
        } \
      else \
        { \
          error = NO_ERROR; \
        } \
    } \
  while (0)
#endif /* !SA_MODE */
#endif /* CHECK_MODIFICATION_NO_RETURN */

/*
 * Message id in the set MSGCAT_SET_LOG
 * in the message catalog MSGCAT_CATALOG_CUBRID (file cubrid.msg).
 */
#define MSGCAT_LOG_STARTS                               1
#define MSGCAT_LOG_LOGARCHIVE_NEEDED                    2
#define MSGCAT_LOG_BACKUPINFO_NEEDED                    3
#define MSGCAT_LOG_NEWLOCATION                          4
#define MSGCAT_LOG_LOGINFO_COMMENT                      5
#define MSGCAT_LOG_LOGINFO_COMMENT_ARCHIVE_NONEEDED     6
#define MSGCAT_LOG_LOGINFO_COMMENT_MANY_ARCHIVES_NONEEDED 7
#define MSGCAT_LOG_LOGINFO_COMMENT_FROM_RENAMED         8
#define MSGCAT_LOG_LOGINFO_ARCHIVE                      9
#define MSGCAT_LOG_LOGINFO_KEYWORD_ARCHIVE              10
#define MSGCAT_LOG_LOGINFO_REMOVE_REASON                11
#define MSGCAT_LOG_LOGINFO_ACTIVE                       12
#define MSGCAT_LOG_FINISH_COMMIT                        13
#define MSGCAT_LOG_FINISH_ABORT                         14
#define MSGCAT_LOG_INCOMPLTE_MEDIA_RECOVERY             15
#define MSGCAT_LOG_RESETLOG_DUE_INCOMPLTE_MEDIA_RECOVERY 16
#define MSGCAT_LOG_DATABASE_BACKUP_WAS_TAKEN            17
#define MSGCAT_LOG_MEDIACRASH_NOT_IMPORTANT             18
#define MSGCAT_LOG_DELETE_BKVOLS                        19
#define MSGCAT_LOG_ENTER_Y2_CONFIRM                     20
#define MSGCAT_LOG_BACKUP_HALTED_BY_USER                21
#define MSGCAT_LOG_LOGINFO_ARCHIVES_NEEDED_FOR_RESTORE  22
#define MSGCAT_LOG_LOGINFO_PENDING_ARCHIVES_RELEASED    23
#define MSGCAT_LOG_LOGINFO_NOTPENDING_ARCHIVE_COMMENT   24
#define MSGCAT_LOG_LOGINFO_MULT_NOTPENDING_ARCHIVES_COMMENT 25
#define MSGCAT_LOG_READ_ERROR_DURING_RESTORE            26
#define MSGCAT_LOG_INPUT_RANGE_ERROR                    27
#define MSGCAT_LOG_UPTODATE_ERROR                       28
#define MSGCAT_LOG_LOGINFO_COMMENT_UNUSED_ARCHIVE_NAME	29
#define MSGCAT_LOG_MAX_ARCHIVES_HAS_BEEN_EXCEEDED	30

#define MAX_NUM_EXEC_QUERY_HISTORY                      100

typedef enum log_flush LOG_FLUSH;
enum log_flush
{ LOG_DONT_NEED_FLUSH, LOG_NEED_FLUSH };

typedef enum log_setdirty LOG_SETDIRTY;
enum log_setdirty
{ LOG_DONT_SET_DIRTY, LOG_SET_DIRTY };

typedef enum log_getnewtrid LOG_GETNEWTRID;
enum log_getnewtrid
{ LOG_DONT_NEED_NEWTRID, LOG_NEED_NEWTRID };

typedef enum log_wrote_eot_log LOG_WRITE_EOT_LOG;
enum log_wrote_eot_log
{ LOG_NEED_TO_WRITE_EOT_LOG, LOG_ALREADY_WROTE_EOT_LOG };

/*
 * Specify up to int bits of permanent status indicators.
 * Restore in progress is the only one so far, the rest are reserved
 * for future use.  Note these must be specified and used as mask values
 * to test and set individual bits.
 */
enum LOG_PSTATUS
{
  LOG_PSTAT_CLEAR = 0x00,
  LOG_PSTAT_BACKUP_INPROGRESS = 0x01,	/* only one backup at a time */
  LOG_PSTAT_RESTORE_INPROGRESS = 0x02,	/* unset upon successful restore */
  LOG_PSTAT_HDRFLUSH_INPPROCESS = 0x04	/* need to flush log header */
};

enum LOG_HA_FILESTAT
{
  LOG_HA_FILESTAT_CLEAR = 0,
  LOG_HA_FILESTAT_ARCHIVED = 1,
  LOG_HA_FILESTAT_SYNCHRONIZED = 2
};

enum LOG_PRIOR_LSA_LOCK
{
  LOG_PRIOR_LSA_WITHOUT_LOCK = 0,
  LOG_PRIOR_LSA_WITH_LOCK = 1
};

typedef struct log_clientids LOG_CLIENTIDS;
struct log_clientids		/* see BOOT_CLIENT_CREDENTIAL */
{
  int client_type;
  char client_info[DB_MAX_IDENTIFIER_LENGTH + 1];
  char db_user[LOG_USERNAME_MAX];
  char program_name[PATH_MAX + 1];
  char login_name[L_cuserid + 1];
  char host_name[MAXHOSTNAMELEN + 1];
  int process_id;
  bool is_user_active;
};

/*
 * LOG PAGE
 */

typedef struct log_hdrpage LOG_HDRPAGE;
struct log_hdrpage
{
  LOG_PAGEID logical_pageid;	/* Logical pageid in infinite log */
  PGLENGTH offset;		/* Offset of first log record in this page. This may be useful when previous log page
				 * is corrupted and an archive of that page does not exist. Instead of losing the whole 
				 * log because of such bad page, we could salvage the log starting at the offset
				 * address, that is, at the next log record */
  short dummy1;			/* Dummy field for 8byte align */
  int dummy2;			/* Dummy field for 8byte align */
};

/* WARNING:
 * Don't use sizeof(LOG_PAGE) or of any structure that contains it
 * Use macro LOG_PAGESIZE instead.
 * It is also bad idea to allocate a variable for LOG_PAGE on the stack.
 */

typedef struct log_page LOG_PAGE;
struct log_page
{				/* The log page */
  LOG_HDRPAGE hdr;
  char area[1];
};

/*
 * Flush information shared by LFT and normal transaction.
 * Transaction in commit phase has to flush all toflush array's pages.
 */
typedef struct log_flush_info LOG_FLUSH_INFO;
struct log_flush_info
{
  /* Size of array to log append pages to flush */
  int max_toflush;

  /* Number of log append pages that can be flush Not all of the append pages may be full. */
  int num_toflush;

  /* A sorted order of log append free pages to flush */
  LOG_PAGE **toflush;

#if defined(SERVER_MODE)
  /* for protecting LOG_FLUSH_INFO */
  pthread_mutex_t flush_mutex;
#endif				/* SERVER_MODE */
};

typedef struct log_group_commit_info LOG_GROUP_COMMIT_INFO;
struct log_group_commit_info
{
  /* group commit waiters count */
  pthread_mutex_t gc_mutex;
  pthread_cond_t gc_cond;
};

#define LOG_GROUP_COMMIT_INFO_INITIALIZER \
  { PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER }

typedef enum logwr_mode LOGWR_MODE;
enum logwr_mode
{
  LOGWR_MODE_ASYNC = 1,
  LOGWR_MODE_SEMISYNC,
  LOGWR_MODE_SYNC
};
#define LOGWR_COPY_FROM_FIRST_PHY_PAGE_MASK	(0x80000000)

typedef enum logwr_status LOGWR_STATUS;
enum logwr_status
{
  LOGWR_STATUS_WAIT,
  LOGWR_STATUS_FETCH,
  LOGWR_STATUS_DONE,
  LOGWR_STATUS_DELAY,
  LOGWR_STATUS_ERROR
};

typedef struct logwr_entry LOGWR_ENTRY;
struct logwr_entry
{
  THREAD_ENTRY *thread_p;
  LOG_PAGEID fpageid;
  LOGWR_MODE mode;
  LOGWR_STATUS status;
  LOG_LSA eof_lsa;
  LOG_LSA last_sent_eof_lsa;
  LOG_LSA tmp_last_sent_eof_lsa;
  INT64 start_copy_time;
  bool copy_from_first_phy_page;
  LOGWR_ENTRY *next;
};

typedef struct logwr_info LOGWR_INFO;
struct logwr_info
{
  LOGWR_ENTRY *writer_list;
  pthread_mutex_t wr_list_mutex;
  pthread_cond_t flush_start_cond;
  pthread_mutex_t flush_start_mutex;
  pthread_cond_t flush_wait_cond;
  pthread_mutex_t flush_wait_mutex;
  pthread_cond_t flush_end_cond;
  pthread_mutex_t flush_end_mutex;
  bool skip_flush;
  bool flush_completed;
  bool is_init;

  /* to measure the time spent by the last LWT delaying LFT */
  bool trace_last_writer;
  LOG_CLIENTIDS last_writer_client_info;
  INT64 last_writer_elapsed_time;
};

#define LOGWR_INFO_INITIALIZER                                 \
  {NULL,                                                       \
    PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER,       \
    PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER,       \
    PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER,       \
    PTHREAD_MUTEX_INITIALIZER,                                 \
    false, false, false, false,                                \
    /* last_writer_client_info */                              \
    { -1, {'0'}, {'0'}, {'0'}, {'0'}, {'0'}, 0, false },       \
    0                                                          \
   }

typedef struct log_append_info LOG_APPEND_INFO;
struct log_append_info
{
  int vdes;			/* Volume descriptor of active log */
  LOG_LSA nxio_lsa;		/* Lowest log sequence number which has not been written to disk (for WAL). */
  LOG_LSA prev_lsa;		/* Address of last append log record */
  LOG_PAGE *log_pgptr;		/* The log page which is fixed */

#if !defined(HAVE_ATOMIC_BUILTINS)
  pthread_mutex_t nxio_lsa_mutex;
#endif
};

#if defined(HAVE_ATOMIC_BUILTINS)
#define LOG_APPEND_INFO_INITIALIZER                           \
  {                                                           \
    /* vdes */                                                \
    NULL_VOLDES,                                              \
    /* nxio_lsa */                                            \
    {NULL_PAGEID, NULL_OFFSET},                               \
    /* prev_lsa */                                            \
    {NULL_PAGEID, NULL_OFFSET},                               \
    /* log_pgptr */                                           \
    NULL}
#else
#define LOG_APPEND_INFO_INITIALIZER                           \
  {                                                           \
    /* vdes */                                                \
    NULL_VOLDES,                                              \
    /* nxio_lsa */                                            \
    {NULL_PAGEID, NULL_OFFSET},                               \
    /* prev_lsa */                                            \
    {NULL_PAGEID, NULL_OFFSET},                               \
    /* log_pgptr */                                           \
    NULL,                                                     \
    /* nxio_lsa_mutex */                                      \
    PTHREAD_MUTEX_INITIALIZER}
#endif

typedef enum log_2pc_execute LOG_2PC_EXECUTE;
enum log_2pc_execute
{
  LOG_2PC_EXECUTE_FULL,		/* For the root coordinator */
  LOG_2PC_EXECUTE_PREPARE,	/* For a participant that is also a non root coordinator execute the first phase of 2PC */
  LOG_2PC_EXECUTE_COMMIT_DECISION,	/* For a participant that is also a non root coordinator execute the second
					 * phase of 2PC. The root coordinator has decided a commit decision */
  LOG_2PC_EXECUTE_ABORT_DECISION	/* For a participant that is also a non root coordinator execute the second
					 * phase of 2PC. The root coordinator has decided an abort decision with or
					 * without going to the first phase (i.e., prepare) of the 2PC */
};

typedef struct log_2pc_gtrinfo LOG_2PC_GTRINFO;
struct log_2pc_gtrinfo
{				/* Global transaction user information */
  int info_length;
  void *info_data;
};

typedef struct log_2pc_coordinator LOG_2PC_COORDINATOR;
struct log_2pc_coordinator
{				/* Coordinator maintains this info */
  int num_particps;		/* Number of participating sites */
  int particp_id_length;	/* Length of a participant identifier */
  void *block_particps_ids;	/* A block of participants identifiers */
  int *ack_received;		/* Acknowledgment received vector */
};

typedef struct log_topops_addresses LOG_TOPOPS_ADDRESSES;
struct log_topops_addresses
{
  LOG_LSA lastparent_lsa;	/* The last address of the parent transaction. This is needed for undo of the top
				 * system action */
  LOG_LSA posp_lsa;		/* The first address of a postpone log record for top system operation. We add this
				 * since it is reset during recovery to the last reference postpone address. */
};

typedef enum log_topops_type LOG_TOPOPS_TYPE;
enum log_topops_type
{
  LOG_TOPOPS_NORMAL,
  LOG_TOPOPS_COMPENSATE_TRAN_ABORT,
  LOG_TOPOPS_COMPENSATE_SYSOP_ABORT,
  LOG_TOPOPS_POSTPONE
};

typedef struct log_topops_stack LOG_TOPOPS_STACK;
struct log_topops_stack
{
  int max;			/* Size of stack */
  int last;			/* Last entry in stack */
  LOG_TOPOPS_ADDRESSES *stack;	/* Stack for push and pop of top system actions */
};

typedef struct modified_class_entry MODIFIED_CLASS_ENTRY;
struct modified_class_entry
{
  MODIFIED_CLASS_ENTRY *m_next;
  const char *m_classname;	/* Name of the modified class */
  OID m_class_oid;
  LOG_LSA m_last_modified_lsa;
};

/* there can be following transitions in transient lobs

   -------------------------------------------------------------------------
   | 	       locator  | created               | deleted		   |
   |--------------------|-----------------------|--------------------------|
   | in     | transient | LOB_TRANSIENT_CREATED i LOB_UNKNOWN		   |
   | tran   |-----------|-----------------------|--------------------------|
   |        | permanent | LOB_PERMANENT_CREATED | LOB_PERMANENT_DELETED    |
   |--------------------|-----------------------|--------------------------|
   | out of | transient | LOB_UNKNOWN		| LOB_UNKNOWN		   |
   | tran   |-----------|-----------------------|--------------------------|
   |        | permanent | LOB_UNKNOWN 		| LOB_TRANSIENT_DELETED    |
   -------------------------------------------------------------------------

   s1: create a transient locator and delete it
       LOB_TRANSIENT_CREATED -> LOB_UNKNOWN

   s2: create a transient locator and bind it to a row in table
       LOB_TRANSIENT_CREATED -> LOB_PERMANENT_CREATED

   s3: bind a transient locator to a row and delete the locator
       LOB_PERMANENT_CREATED -> LOB_PERMANENT_DELETED

   s4: delete a locator to be create out of transaction
       LOB_UNKNOWN -> LOB_TRANSIENT_DELETED

 */
typedef enum lob_locator_state LOB_LOCATOR_STATE;
enum lob_locator_state
{
  LOB_UNKNOWN,
  LOB_TRANSIENT_CREATED,
  LOB_TRANSIENT_DELETED,
  LOB_PERMANENT_CREATED,
  LOB_PERMANENT_DELETED,
  LOB_NOT_FOUND
};

/* lob entry */
typedef struct lob_locator_entry LOB_LOCATOR_ENTRY;

/*  lob rb tree head
  The macro RB_HEAD is defined in rb_tree.h. It will be expanede like this;

  struct lob_rb_root {
    struct lob_locator_entry* rbh_root;
  };
 */
RB_HEAD (lob_rb_root, lob_locator_entry);

typedef enum tran_abort_reason TRAN_ABORT_REASON;
enum tran_abort_reason
{
  TRAN_NORMAL = 0,
  TRAN_ABORT_DUE_DEADLOCK = 1,
  TRAN_ABORT_DUE_ROLLBACK_ON_ESCALATION = 2
};

typedef struct log_unique_stats LOG_UNIQUE_STATS;
struct log_unique_stats
{
  int num_nulls;		/* number of nulls */
  int num_keys;			/* number of keys */
  int num_oids;			/* number of oids */
};

typedef struct log_tran_btid_unique_stats LOG_TRAN_BTID_UNIQUE_STATS;
struct log_tran_btid_unique_stats
{
  BTID btid;			/* id of B-tree */
  bool deleted;			/* true if the B-tree was deleted */

  LOG_UNIQUE_STATS tran_stats;	/* statistics accumulated during entire transaction */
  LOG_UNIQUE_STATS global_stats;	/* statistics loaded from index */
};

typedef enum count_optim_state COUNT_OPTIM_STATE;
enum count_optim_state
{
  COS_NOT_LOADED = 0,		/* the global statistics was not loaded yet */
  COS_TO_LOAD = 1,		/* the global statistics must be loaded when snapshot is taken */
  COS_LOADED = 2		/* the global statistics were loaded */
};

#define TRAN_UNIQUE_STATS_CHUNK_SIZE  128	/* size of the memory chunk for unique statistics */

/* LOG_TRAN_BTID_UNIQUE_STATS_CHUNK
 * Represents a chunk of memory for transaction unique statistics
 */
typedef struct log_tran_btid_unique_stats_chunk LOG_TRAN_BTID_UNIQUE_STATS_CHUNK;
struct log_tran_btid_unique_stats_chunk
{
  LOG_TRAN_BTID_UNIQUE_STATS_CHUNK *next_chunk;	/* address of next chunk of memory */
  LOG_TRAN_BTID_UNIQUE_STATS buffer[1];	/* more than one */
};

/* LOG_TRAN_CLASS_COS
 * Structure used to store the state of the count optimization for classes.
 */
typedef struct log_tran_class_cos LOG_TRAN_CLASS_COS;
struct log_tran_class_cos
{
  OID class_oid;		/* class object identifier. */
  COUNT_OPTIM_STATE count_state;	/* count optimization state for class_oid */
};

#define COS_CLASSES_CHUNK_SIZE	64	/* size of the memory chunk for count optimization classes */

/* LOG_TRAN_CLASS_COS_CHUNK
 * Represents a chunk of memory for count optimization states
 */
typedef struct log_tran_class_cos_chunk LOG_TRAN_CLASS_COS_CHUNK;
struct log_tran_class_cos_chunk
{
  LOG_TRAN_CLASS_COS_CHUNK *next_chunk;	/* address of next chunk of memory */
  LOG_TRAN_CLASS_COS buffer[1];	/* more than one */
};

/* LOG_TRAN_UPDATE_STATS
 * Structure used for transaction local unique statistics and count optimization
 * management
 */
typedef struct log_tran_update_stats LOG_TRAN_UPDATE_STATS;
struct log_tran_update_stats
{
  int cos_count;		/* the number of hashed elements */
  LOG_TRAN_CLASS_COS_CHUNK *cos_first_chunk;	/* address of first chunk in the chunks list */
  LOG_TRAN_CLASS_COS_CHUNK *cos_current_chunk;	/* address of current chunk from which new elements are assigned */
  MHT_TABLE *classes_cos_hash;	/* hash of count optimization states for classes that were or will be subject to count
				 * optimization. */

  int stats_count;		/* the number of hashed elements */
  LOG_TRAN_BTID_UNIQUE_STATS_CHUNK *stats_first_chunk;	/* address of first chunk in the chunks list */
  LOG_TRAN_BTID_UNIQUE_STATS_CHUNK *stats_current_chunk;	/* address of current chunk from which new elements are
								 * assigned */
  MHT_TABLE *unique_stats_hash;	/* hash of unique statistics for indexes used during transaction. */
};

/*
 * MVCC_TRANS_STATUS keep MVCCIDs status in bit area. Thus bit 0 means active
 * MVCCID bit 1 means committed transaction. This structure keep also lowest
 * active MVCCIDs used by VACUUM for MVCCID threshold computation. Also, MVCCIDs
 * of long time transactions MVCCIDs are kept in this structure.
 */
typedef struct mvcc_trans_status MVCC_TRANS_STATUS;
struct mvcc_trans_status
{
  /* bit area to store MVCCIDS status - size MVCC_BITAREA_MAXIMUM_ELEMENTS */
  UINT64 *bit_area;
  /* first MVCCID whose status is stored in bit area */
  MVCCID bit_area_start_mvccid;
  /* the area length expressed in bits */
  unsigned int bit_area_length;

  /* long time transaction mvccid array */
  MVCCID *long_tran_mvccids;
  /* long time transactions mvccid array length */
  unsigned int long_tran_mvccids_length;

  volatile unsigned int version;

  /* lowest active MVCCID */
  MVCCID lowest_active_mvccid;
};

#define MVCC_STATUS_INITIALIZER \
  { NULL, MVCCID_FIRST, 0, NULL, 0, 0, MVCCID_FIRST }

typedef struct mvcctable MVCCTABLE;
struct mvcctable
{
  /* current transaction status */
  MVCC_TRANS_STATUS current_trans_status;

  /* lowest active MVCCIDs - array of size NUM_TOTAL_TRAN_INDICES */
  volatile MVCCID *transaction_lowest_active_mvccids;

  /* transaction status history - array of size TRANS_STATUS_HISTORY_MAX_SIZE */
  MVCC_TRANS_STATUS *trans_status_history;
  /* the position in transaction status history array */
  volatile int trans_status_history_position;

  /* protect against getting new MVCCIDs concurrently */
#if defined(HAVE_ATOMIC_BUILTINS)
  pthread_mutex_t new_mvccid_lock;
#endif

  /* protect against current transaction status modifications */
  pthread_mutex_t active_trans_mutex;
};

#if defined(HAVE_ATOMIC_BUILTINS)
#define MVCCTABLE_INITIALIZER \
  { MVCC_STATUS_INITIALIZER, NULL, NULL, 0, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER }
#else
#define MVCCTABLE_INITIALIZER \
  { MVCC_STATUS_INITIALIZER, NULL, NULL, 0, PTHREAD_MUTEX_INITIALIZER }
#endif

/*
 * This structure encapsulates various information and metrics related
 * to each backup level.
 * Estimates and heuristics are not currently used but are placeholder
 * for the future to avoid changing the physical representation again.
 */
typedef struct log_hdr_bkup_level_info LOG_HDR_BKUP_LEVEL_INFO;
struct log_hdr_bkup_level_info
{
  INT64 bkup_attime;		/* Timestamp when this backup lsa taken */
  INT64 io_baseln_time;		/* time (secs.) to write a single page */
  INT64 io_bkuptime;		/* total time to write the backup */
  int ndirty_pages_post_bkup;	/* number of pages written since the lsa for this backup level. */
  int io_numpages;		/* total number of pages in last backup */
};

/*
 * LOG HEADER INFORMATION
 */
typedef struct log_header LOG_HEADER;
struct log_header
{				/* Log header information */
  char magic[CUBRID_MAGIC_MAX_LENGTH];	/* Magic value for file/magic Unix utility */
  INT32 dummy;			/* for 8byte align */
  INT64 db_creation;		/* Database creation time. For safety reasons, this value is set on all volumes and the
				 * log. The value is generated by the log manager */
  char db_release[REL_MAX_RELEASE_LENGTH];	/* CUBRID Release */
  float db_compatibility;	/* Compatibility of the database against the current release of CUBRID */
  PGLENGTH db_iopagesize;	/* Size of pages in the database. For safety reasons this value is recorded in the log
				 * to make sure that the database is always run with the same page size */
  PGLENGTH db_logpagesize;	/* Size of log pages in the database. */
  int is_shutdown;		/* Was the log shutdown ? */
  TRANID next_trid;		/* Next Transaction identifier */
  MVCCID mvcc_next_id;		/* Next MVCC ID */
  int avg_ntrans;		/* Number of average transactions */
  int avg_nlocks;		/* Average number of object locks */
  DKNPAGES npages;		/* Number of pages in the active log portion. Does not include the log header page. */
  INT8 db_charset;
  INT8 dummy2;			/* Dummy fields for 8byte align */
  INT8 dummy3;
  INT8 dummy4;
  LOG_PAGEID fpageid;		/* Logical pageid at physical location 1 in active log */
  LOG_LSA append_lsa;		/* Current append location */
  LOG_LSA chkpt_lsa;		/* Lowest log sequence address to start the recovery process */
  LOG_PAGEID nxarv_pageid;	/* Next logical page to archive */
  LOG_PHY_PAGEID nxarv_phy_pageid;	/* Physical location of logical page to archive */
  int nxarv_num;		/* Next log archive number */
  int last_arv_num_for_syscrashes;	/* Last log archive needed for system crashes */
  int last_deleted_arv_num;	/* Last deleted archive number */
  LOG_LSA bkup_level0_lsa;	/* Lsa of backup level 0 */
  LOG_LSA bkup_level1_lsa;	/* Lsa of backup level 1 */
  LOG_LSA bkup_level2_lsa;	/* Lsa of backup level 2 */
  char prefix_name[MAXLOGNAME];	/* Log prefix name */
  bool has_logging_been_skipped;	/* Has logging been skipped ? */
  int reserved_int_1;		/* for backward compitablity - previously used for lowest_arv_num_for_backup */
  int reserved_int_2;		/* for backward compitablity - previously used for highest_arv_num_for_backup */
  int perm_status;		/* Reserved for future expansion and permanent status indicators, e.g. to mark
				 * RESTORE_IN_PROGRESS */
  LOG_HDR_BKUP_LEVEL_INFO bkinfo[FILEIO_BACKUP_UNDEFINED_LEVEL];
  /* backup specific info for future growth */

  int ha_server_state;
  int ha_file_status;
  LOG_LSA eof_lsa;

  LOG_LSA smallest_lsa_at_last_chkpt;

  LOG_LSA mvcc_op_log_lsa;	/* Used to link log entries for mvcc operations. Vacuum will then process these entries */
  MVCCID last_block_oldest_mvccid;	/* Used to find the oldest MVCCID in a block of log data. */
  MVCCID last_block_newest_mvccid;	/* Used to find the newest MVCCID in a block of log data. */
  VPID vacuum_data_first_vpid;	/* First vacuum data page VPID. */

  INT64 ha_promotion_time;
  INT64 db_restore_time;
  bool mark_will_del;
};

#define LOG_HEADER_INITIALIZER                   \
  {                                              \
     /* magic */                                 \
     {'0'},                                      \
     0, 0,                                       \
     /* db_release */                            \
     {'0'},                                      \
     /* db_compatibility */                      \
     0.0,                                        \
     0, 0, 0,                                    \
     /* next_trid */                             \
     (LOG_SYSTEM_TRANID + 1),                    \
     /* mvcc_id */				 \
     MVCCID_NULL,                                \
     0, 0, 0,					 \
     /* db_charset */				 \
     0,						 \
     0, 0, 0, 0,				 \
     /* append_lsa */                            \
     {NULL_PAGEID, NULL_OFFSET},                 \
     /* chkpt_lsa */                             \
     {NULL_PAGEID, NULL_OFFSET},                 \
     /* nxarv_pageid */                          \
     0,                                          \
     /* nxarv_phy_pageid */                      \
     0,                                          \
     0, 0, 0,                                    \
     /* bkup_level0_lsa */                       \
     {NULL_PAGEID, NULL_OFFSET},                 \
     /* bkup_level1_lsa */                       \
     {NULL_PAGEID, NULL_OFFSET},                 \
     /* bkup_level2_lsa */                       \
     {NULL_PAGEID, NULL_OFFSET},                 \
     /* prefix_name */                           \
     {'0'},                                      \
     /* has_logging_been_skipped */              \
     false,                                      \
     0, 0, 0,                                    \
     /* bkinfo */                                \
     {{0, 0, 0, 0, 0}},                          \
     0, 0,                                       \
     /* eof_lsa */                               \
     {NULL_PAGEID, NULL_OFFSET},                 \
     /* smallest_lsa_at_last_chkpt */            \
     {NULL_PAGEID, NULL_OFFSET},                 \
     /* mvcc_op_log_lsa */			 \
     {NULL_PAGEID, NULL_OFFSET},                 \
     /* last_block_oldest_mvccid */		 \
     MVCCID_NULL,				 \
     /* last_block_newest_mvccid */		 \
     MVCCID_NULL,				 \
     /* vacuum_data_first_vpid */		 \
     VPID_INITIALIZER,				 \
     /* ha_promotion_time */ 			 \
     0, 					 \
     /* db_restore_time */			 \
     0,						 \
     /* mark_will_del */			 \
     false					 \
  }

#define LOGWR_HEADER_INITIALIZER                 \
  {                                              \
     /* magic */                                 \
     {'0'},                                      \
     0, 0,                                       \
     /* db_release */                            \
     {'0'},                                      \
     /* db_compatibility */                      \
     0.0,                                        \
     0, 0, 0,                                    \
     /* next_trid */                             \
     NULL_TRANID,                                \
     /* mvcc_next_id */                          \
     MVCCID_NULL,                                \
     0, 0, 0,					 \
     /* db_charset */				 \
     0,						 \
     0, 0, 0, 0,				 \
     /* append_lsa */                            \
     {NULL_PAGEID, NULL_OFFSET},                 \
     /* chkpt_lsa */                             \
     {NULL_PAGEID, NULL_OFFSET},                 \
     /* nxarv_pageid */                          \
     NULL_PAGEID,                                \
     /* nxarv_phy_pageid */                      \
     NULL_PAGEID,                                \
     -1, -1, -1,                                 \
     /* bkup_level0_lsa */                       \
     {NULL_PAGEID, NULL_OFFSET},                 \
     /* bkup_level1_lsa */                       \
     {NULL_PAGEID, NULL_OFFSET},                 \
     /* bkup_level2_lsa */                       \
     {NULL_PAGEID, NULL_OFFSET},                 \
     /* prefix_name */                           \
     {'0'},                                      \
     /* has_logging_been_skipped */              \
     false,                                      \
     0, 0, 0,                                    \
     /* bkinfo */                                \
     {{0, 0, 0, 0, 0}},                          \
     0, 0,                                       \
     /* eof_lsa */                               \
     {NULL_PAGEID, NULL_OFFSET},                 \
     /* smallest_lsa_at_last_chkpt */            \
     {NULL_PAGEID, NULL_OFFSET},                 \
     /* mvcc_op_log_lsa */			 \
     {NULL_PAGEID, NULL_OFFSET},                 \
     /* last_block_oldest_mvccid */		 \
     MVCCID_NULL,				 \
     /* last_block_newest_mvccid */		 \
     MVCCID_NULL,				 \
     /* vacuum_data_first_vpid */		 \
     VPID_INITIALIZER,				 \
     /* ha_promotion_time */ 			 \
     0, 					 \
     /* db_restore_time */			 \
     0,						 \
     /* mark_will_del */			 \
     false					 \
  }

typedef struct log_arv_header LOG_ARV_HEADER;
struct log_arv_header
{				/* Log archive header information */
  char magic[CUBRID_MAGIC_MAX_LENGTH];	/* Magic value for file/magic Unix utility */
  INT32 dummy;			/* for 8byte align */
  INT64 db_creation;		/* Database creation time. For safety reasons, this value is set on all volumes and the
				 * log. The value is generated by the log manager */
  TRANID next_trid;		/* Next Transaction identifier */
  DKNPAGES npages;		/* Number of pages in the archive log */
  LOG_PAGEID fpageid;		/* Logical pageid at physical location 1 in archive log */
  int arv_num;			/* The archive number */
  INT32 dummy2;			/* Dummy field for 8byte align */
};

#define LOG_ARV_HEADER_INITIALIZER \
  { /* magic */ {'0'}, 0, 0, 0, 0, 0, 0, 0 }

typedef struct log_bgarv_header LOG_BGARV_HEADER;
struct log_bgarv_header
{				/* Background log archive header information */
  char magic[CUBRID_MAGIC_MAX_LENGTH];

  INT32 dummy;
  INT64 db_creation;

  LOG_PAGEID start_page_id;
  LOG_PAGEID current_page_id;
  LOG_PAGEID last_sync_pageid;
};

#define LOG_BGARV_HEADER_INITIALIZER \
  { /* magic */ {'0'}, 0, 0, NULL_PAGEID, NULL_PAGEID, NULL_PAGEID }

typedef enum log_rectype LOG_RECTYPE;
enum log_rectype
{
  /* In order of likely of appearance in the log */
  LOG_SMALLER_LOGREC_TYPE = 0,	/* A lower bound check */

#if 0
  LOG_CLIENT_NAME = 1,		/* Obsolete */
#endif
  LOG_UNDOREDO_DATA = 2,	/* An undo and redo data record */
  LOG_UNDO_DATA = 3,		/* Only undo data */
  LOG_REDO_DATA = 4,		/* Only redo data */
  LOG_DBEXTERN_REDO_DATA = 5,	/* Only database external redo data */
  LOG_POSTPONE = 6,		/* Postpone redo data */
  LOG_RUN_POSTPONE = 7,		/* Run/redo a postpone data. Only for transactions committed with postpone operations */
  LOG_COMPENSATE = 8,		/* Compensation record (compensate a undo record of an aborted tran) */
#if 0
  LOG_LCOMPENSATE = 9,		/* Obsolete */
  LOG_CLIENT_USER_UNDO_DATA = 10,	/* Obsolete */
  LOG_CLIENT_USER_POSTPONE_DATA = 11,	/* Obsolete */
  LOG_RUN_NEXT_CLIENT_UNDO = 12,	/* Obsolete */
  LOG_RUN_NEXT_CLIENT_POSTPONE = 13,	/* Obsolete */
#endif
  LOG_WILL_COMMIT = 14,		/* Transaction will be committed */
  LOG_COMMIT_WITH_POSTPONE = 15,	/* Committing server postpone operations */
#if 0
  LOG_COMMIT_WITH_CLIENT_USER_LOOSE_ENDS = 16,	/* Obsolete */
#endif
  LOG_COMMIT = 17,		/* A commit record */
  LOG_SYSOP_START_POSTPONE = 18,	/* Committing server top system postpone operations */
#if 0
  LOG_COMMIT_TOPOPE_WITH_CLIENT_USER_LOOSE_ENDS = 19,	/* Obsolete */
#endif
  LOG_SYSOP_END = 20,		/* end of system operation record. Its functionality can vary based on LOG_SYSOP_END_TYPE:
				 *
				 * - LOG_SYSOP_END_COMMIT: the usual functionality. changes under system operation become
				 *   permanent immediately
				 *
				 * - LOG_SYSOP_END_LOGICAL_UNDO: system operation is used for complex logical operation (that
				 *   usually affects more than one page). end system operation also includes undo data that
				 *   is processed during rollback or undo.
				 *
				 * - LOG_SYSOP_END_LOGICAL_COMPENSATE: system operation is used for complex logical operation
				 *   that has the purpose of compensating a change on undo or rollback. end system operation
				 *   also includes the LSA of previous undo log record.
				 *
				 * - LOG_SYSOP_END_LOGICAL_RUN_POSTPONE: system operation is used for complex logical operation
				 *   that has the purpose of running a postpone record. end system operation also includes the
				 *   postpone LSA and is_sysop_postpone (recovery is different for logical run postpones during
				 *   system operation postpone compared to transaction postpone).
				 *
				 * - LOG_SYSOP_END_ABORT: any of the above system operations are not ended due to crash or
				 *   errors. the system operation is rollbacked and ended with this type.
				 */
#if 0
  LOG_ABORT_WITH_CLIENT_USER_LOOSE_ENDS = 21,	/* Obsolete */
#endif
  LOG_ABORT = 22,		/* An abort record */
#if 0
  LOG_ABORT_TOPOPE_WITH_CLIENT_USER_LOOSE_ENDS = 23,	/* Obsolete */
#endif
  LOG_ABORT_TOPOPE = 24,	/* obsolete */
  LOG_START_CHKPT = 25,		/* Start a checkpoint */
  LOG_END_CHKPT = 26,		/* Checkpoint information */
  LOG_SAVEPOINT = 27,		/* A user savepoint record */
  LOG_2PC_PREPARE = 28,		/* A prepare to commit record */
  LOG_2PC_START = 29,		/* Start the 2PC protocol by sending vote request messages to participants of
				 * distributed tran. */
  LOG_2PC_COMMIT_DECISION = 30,	/* Beginning of the second phase of 2PC, need to perform local & global commits. */
  LOG_2PC_ABORT_DECISION = 31,	/* Beginning of the second phase of 2PC, need to perform local & global aborts. */
  LOG_2PC_COMMIT_INFORM_PARTICPS = 32,	/* Committing, need to inform the participants */
  LOG_2PC_ABORT_INFORM_PARTICPS = 33,	/* Aborting, need to inform the participants */
  LOG_2PC_RECV_ACK = 34,	/* Received ack. from the participant that it received the decision on the fate of
				 * dist. trans. */
  LOG_END_OF_LOG = 35,		/* End of log */
  LOG_DUMMY_HEAD_POSTPONE = 36,	/* A dummy log record. No-op */
  LOG_DUMMY_CRASH_RECOVERY = 37,	/* A dummy log record which indicate the start of crash recovery. No-op */

#if 0				/* not used */
  LOG_DUMMY_FILLPAGE_FORARCHIVE = 38,	/* Indicates logical end of current page so it could be archived safely. No-op
					 * This record is not generated no more. It's kept for backward compatibility. */
#endif
  LOG_REPLICATION_DATA = 39,	/* Replication log for insert, delete or update */
  LOG_REPLICATION_STATEMENT = 40,	/* Replication log for schema, index, trigger or system catalog updates */
#if 0
  LOG_UNLOCK_COMMIT = 41,	/* for repl_agent to guarantee the order of */
  LOG_UNLOCK_ABORT = 42,	/* transaction commit, we append the unlock info. before calling lock_unlock_all() */
#endif
  LOG_DIFF_UNDOREDO_DATA = 43,	/* diff undo redo data */
  LOG_DUMMY_HA_SERVER_STATE = 44,	/* HA server state */
  LOG_DUMMY_OVF_RECORD = 45,	/* indicator of the first part of an overflow record */

  LOG_MVCC_UNDOREDO_DATA = 46,	/* Undoredo for MVCC operations (will require more fields than a regular undo-redo. */
  LOG_MVCC_UNDO_DATA = 47,	/* Undo for MVCC operations */
  LOG_MVCC_REDO_DATA = 48,	/* Redo for MVCC operations */
  LOG_MVCC_DIFF_UNDOREDO_DATA = 49,	/* diff undo redo data for MVCC operations */

  LOG_DUMMY_GENERIC,		/* used for flush for now. it is ridiculous to create dummy log records for every single
				 * case. we should find a different approach */

  LOG_LARGER_LOGREC_TYPE	/* A higher bound for checks */
};

typedef enum log_repl_flush LOG_REPL_FLUSH;
enum log_repl_flush
{
  LOG_REPL_DONT_NEED_FLUSH = -1,	/* no flush */
  LOG_REPL_COMMIT_NEED_FLUSH = 0,	/* log must be flushed at commit */
  LOG_REPL_NEED_FLUSH = 1	/* log must be flushed at commit and rollback */
};

/* Definitions used to identify UNDO/REDO/UNDOREDO log record data types */

/* Is record type UNDO */
#define LOG_IS_UNDO_RECORD_TYPE(type) \
  (((type) == LOG_UNDO_DATA) || ((type) == LOG_MVCC_UNDO_DATA))

/* Is record type REDO */
#define LOG_IS_REDO_RECORD_TYPE(type) \
  (((type) == LOG_REDO_DATA) || ((type) == LOG_MVCC_REDO_DATA))

/* Is record type UNDOREDO */
#define LOG_IS_UNDOREDO_RECORD_TYPE(type) \
  (((type) == LOG_UNDOREDO_DATA) || ((type) == LOG_MVCC_UNDOREDO_DATA) \
   || ((type) == LOG_DIFF_UNDOREDO_DATA) || ((type) == LOG_MVCC_DIFF_UNDOREDO_DATA))

#define LOG_IS_DIFF_UNDOREDO_TYPE(type) \
  ((type) == LOG_DIFF_UNDOREDO_DATA || (type) == LOG_MVCC_DIFF_UNDOREDO_DATA)

/* Definitions used to identify MVCC log records. Used by log manager and
 * vacuum.
 */

/* Is record type used a MVCC operation */
#define LOG_IS_MVCC_OP_RECORD_TYPE(type) \
  (((type) == LOG_MVCC_UNDO_DATA) \
   || ((type) == LOG_MVCC_REDO_DATA) \
   || ((type) == LOG_MVCC_UNDOREDO_DATA) \
   || ((type) == LOG_MVCC_DIFF_UNDOREDO_DATA))

/* Is log record for a heap MVCC operation */
#define LOG_IS_MVCC_HEAP_OPERATION(rcvindex) \
  (((rcvindex) == RVHF_MVCC_DELETE_REC_HOME) \
   || ((rcvindex) == RVHF_MVCC_INSERT) \
   || ((rcvindex) == RVHF_UPDATE_NOTIFY_VACUUM) \
   || ((rcvindex) == RVHF_MVCC_DELETE_MODIFY_HOME) \
   || ((rcvindex) == RVHF_MVCC_NO_MODIFY_HOME) \
   || ((rcvindex) == RVHF_MVCC_REDISTRIBUTE))

/* Is log record for a b-tree MVCC operation */
#define LOG_IS_MVCC_BTREE_OPERATION(rcvindex) \
  ((rcvindex) == RVBT_MVCC_DELETE_OBJECT \
   || (rcvindex) == RVBT_MVCC_INSERT_OBJECT \
   || (rcvindex) == RVBT_MVCC_INSERT_OBJECT_UNQ \
   || (rcvindex) == RVBT_MVCC_NOTIFY_VACUUM)

/* Is log record for a MVCC operation */
#define LOG_IS_MVCC_OPERATION(rcvindex) \
  (LOG_IS_MVCC_HEAP_OPERATION (rcvindex) \
   || LOG_IS_MVCC_BTREE_OPERATION (rcvindex) \
   || ((rcvindex) == RVES_NOTIFY_VACUUM))

/* Is log record for a change on vacuum data */
#define LOG_IS_VACUUM_DATA_RECOVERY(rcvindex) \
  ((rcvindex) == RVVAC_LOG_BLOCK_APPEND	\
   || (rcvindex) == RVVAC_LOG_BLOCK_REMOVE \
   || (rcvindex) == RVVAC_LOG_BLOCK_SAVE \
   || (rcvindex) == RVVAC_START_OR_END_JOB \
   || (rcvindex) == RVVAC_COMPLETE)

#define LOG_IS_VACUUM_DATA_BUFFER_RECOVERY(rcvindex) \
  (((rcvindex) == RVVAC_LOG_BLOCK_APPEND || (rcvindex) == RVVAC_LOG_BLOCK_SAVE) \
   && log_Gl.rcv_phase == LOG_RECOVERY_REDO_PHASE)

typedef struct log_repl LOG_REPL_RECORD;
struct log_repl
{
  LOG_RECTYPE repl_type;	/* LOG_REPLICATION_DATA or LOG_REPLICATION_SCHEMA */
  LOG_RCVINDEX rcvindex;
  OID inst_oid;
  LOG_LSA lsa;
  char *repl_data;		/* the content of the replication log record */
  int length;
  LOG_REPL_FLUSH must_flush;
};

/* Description of a log record */
typedef struct log_rec_header LOG_RECORD_HEADER;
struct log_rec_header
{
  LOG_LSA prev_tranlsa;		/* Address of previous log record for the same transaction */
  LOG_LSA back_lsa;		/* Backward log address */
  LOG_LSA forw_lsa;		/* Forward log address */
  TRANID trid;			/* Transaction identifier of the log record */
  LOG_RECTYPE type;		/* Log record type (e.g., commit, abort) */
};

/* Common information of log data records */
typedef struct log_data LOG_DATA;
struct log_data
{
  LOG_RCVINDEX rcvindex;	/* Index to recovery function */
  PAGEID pageid;		/* Pageid of recovery data */
  PGLENGTH offset;		/* offset of recovery data in pageid */
  VOLID volid;			/* Volume identifier of recovery data */
};

/* Information of undo_redo log records */
typedef struct log_rec_undoredo LOG_REC_UNDOREDO;
struct log_rec_undoredo
{
  LOG_DATA data;		/* Location of recovery data */
  int ulength;			/* Length of undo data */
  int rlength;			/* Length of redo data */
};

/* Information of undo log records */
typedef struct log_rec_undo LOG_REC_UNDO;
struct log_rec_undo
{
  LOG_DATA data;		/* Location of recovery data */
  int length;			/* Length of undo data */
};

/* Information of redo log records */
typedef struct log_rec_redo LOG_REC_REDO;
struct log_rec_redo
{
  LOG_DATA data;		/* Location of recovery data */
  int length;			/* Length of redo data */
};

/* Log information required for vacuum */
typedef struct log_vacuum_info LOG_VACUUM_INFO;
struct log_vacuum_info
{
  LOG_LSA prev_mvcc_op_log_lsa;	/* Log lsa of previous MVCC operation log record. Used by vacuum to process log data. */
  VFID vfid;			/* File identifier. Will be used by vacuum for heap files (TODO: maybe b-tree too).
				 * Used to: - Find if the file was dropped/reused. - Find the type of objects in heap
				 * file (reusable or referable). */
};

/* Information of undo_redo log records for MVCC operations */
typedef struct log_rec_mvcc_undoredo LOG_REC_MVCC_UNDOREDO;
struct log_rec_mvcc_undoredo
{
  LOG_REC_UNDOREDO undoredo;	/* Undoredo information */
  MVCCID mvccid;		/* MVCC Identifier for transaction */
  LOG_VACUUM_INFO vacuum_info;	/* Info required for vacuum */
};

/* Information of undo log records for MVCC operations */
typedef struct log_rec_mvcc_undo LOG_REC_MVCC_UNDO;
struct log_rec_mvcc_undo
{
  LOG_REC_UNDO undo;		/* Undo information */
  MVCCID mvccid;		/* MVCC Identifier for transaction */
  LOG_VACUUM_INFO vacuum_info;	/* Info required for vacuum */
};

/* Information of redo log records for MVCC operations */
typedef struct log_rec_mvcc_redo LOG_REC_MVCC_REDO;
struct log_rec_mvcc_redo
{
  LOG_REC_REDO redo;		/* Location of recovery data */
  MVCCID mvccid;		/* MVCC Identifier for transaction */
};

/* Information of database external redo log records */
typedef struct log_rec_dbout_redo LOG_REC_DBOUT_REDO;
struct log_rec_dbout_redo
{
  LOG_RCVINDEX rcvindex;	/* Index to recovery function */
  int length;			/* Length of redo data */
};

/* Information of a compensating log records */
typedef struct log_rec_compensate LOG_REC_COMPENSATE;
struct log_rec_compensate
{
  LOG_DATA data;		/* Location of recovery data */
  LOG_LSA undo_nxlsa;		/* Address of next log record to undo */
  int length;			/* Length of compensating data */
};

/* This entry is included during commit */
typedef struct log_rec_start_postpone LOG_REC_START_POSTPONE;
struct log_rec_start_postpone
{
  LOG_LSA posp_lsa;
};

/* types of end system operation */
typedef enum log_sysop_end_type LOG_SYSOP_END_TYPE;
enum log_sysop_end_type
{
  LOG_SYSOP_END_COMMIT,		/* permanent changes */
  LOG_SYSOP_END_ABORT,		/* aborted system op */
  LOG_SYSOP_END_LOGICAL_UNDO,	/* logical undo */
  LOG_SYSOP_END_LOGICAL_MVCC_UNDO,	/* logical mvcc undo */
  LOG_SYSOP_END_LOGICAL_COMPENSATE,	/* logical compensate */
  LOG_SYSOP_END_LOGICAL_RUN_POSTPONE	/* logical run postpone */
};
#define LOG_SYSOP_END_TYPE_CHECK(type) \
  assert ((type) == LOG_SYSOP_END_COMMIT \
          || (type) == LOG_SYSOP_END_ABORT \
          || (type) == LOG_SYSOP_END_LOGICAL_UNDO \
          || (type) == LOG_SYSOP_END_LOGICAL_MVCC_UNDO \
          || (type) == LOG_SYSOP_END_LOGICAL_COMPENSATE \
          || (type) == LOG_SYSOP_END_LOGICAL_RUN_POSTPONE)

/* end system operation log record */
typedef struct log_rec_sysop_end LOG_REC_SYSOP_END;
struct log_rec_sysop_end
{
  LOG_LSA lastparent_lsa;	/* last address before the top action */
  LOG_LSA prv_topresult_lsa;	/* previous top action (either, partial abort or partial commit) address */
  LOG_SYSOP_END_TYPE type;	/* end system op type */
  union				/* other info based on type */
  {
    LOG_REC_UNDO undo;		/* undo data for logical undo */
    LOG_REC_MVCC_UNDO mvcc_undo;	/* undo data for logical undo of MVCC operation */
    LOG_LSA compensate_lsa;	/* compensate lsa for logical compensate */
    struct
    {
      LOG_LSA postpone_lsa;	/* postpone lsa */
      bool is_sysop_postpone;	/* true if run postpone is used during a system op postpone, false if used during
				 * transaction postpone */
    } run_postpone;		/* run postpone info */
  };
};

/* This entry is included during the commit of top system operations */
typedef struct log_rec_sysop_start_postpone LOG_REC_SYSOP_START_POSTPONE;
struct log_rec_sysop_start_postpone
{
  LOG_REC_SYSOP_END sysop_end;	/* log record used for end of system operation */
  LOG_LSA posp_lsa;		/* address where the first postpone operation start */
};

/* Information of execution of a postpone data */
typedef struct log_rec_run_postpone LOG_REC_RUN_POSTPONE;
struct log_rec_run_postpone
{
  LOG_DATA data;		/* Location of recovery data */
  LOG_LSA ref_lsa;		/* Address of the original postpone record */
  int length;			/* Length of redo data */
};

/* A checkpoint record */
typedef struct log_rec_chkpt LOG_REC_CHKPT;
struct log_rec_chkpt
{
  LOG_LSA redo_lsa;		/* Oldest LSA of dirty data page in page buffers */
  int ntrans;			/* Number of active transactions */
  int ntops;			/* Total number of system operations */
};

/* replication log structure */
typedef struct log_rec_replication LOG_REC_REPLICATION;
struct log_rec_replication
{
  LOG_LSA lsa;
  int length;
  int rcvindex;
};

/* Transaction descriptor */
typedef struct log_info_chkpt_trans LOG_INFO_CHKPT_TRANS;
struct log_info_chkpt_trans
{
  int isloose_end;
  TRANID trid;			/* Transaction identifier */
  TRAN_STATE state;		/* Transaction state (e.g., Active, aborted) */
  LOG_LSA head_lsa;		/* First log address of transaction */
  LOG_LSA tail_lsa;		/* Last log record address of transaction */
  LOG_LSA undo_nxlsa;		/* Next log record address of transaction for UNDO purposes. Needed since compensating
				 * log records are logged during UNDO */
  LOG_LSA posp_nxlsa;		/* First address of a postpone record */
  LOG_LSA savept_lsa;		/* Address of last savepoint */
  LOG_LSA tail_topresult_lsa;	/* Address of last partial abort/commit */
  char user_name[LOG_USERNAME_MAX];	/* Name of the client */

};

typedef struct log_info_chkpt_sysop_start_postpone LOG_INFO_CHKPT_SYSOP_START_POSTPONE;
struct log_info_chkpt_sysop_start_postpone
{
  TRANID trid;			/* Transaction identifier */
  LOG_LSA sysop_start_postpone_lsa;	/* saved lsa of system op start postpone log record */
};

typedef struct log_rec_savept LOG_REC_SAVEPT;
struct log_rec_savept
{
  LOG_LSA prv_savept;		/* Previous savepoint record */
  int length;			/* Savepoint name */
};

/* Log a prepare to commit record */
typedef struct log_rec_2pc_prepcommit LOG_REC_2PC_PREPCOMMIT;
struct log_rec_2pc_prepcommit
{
  char user_name[DB_MAX_USER_LENGTH + 1];	/* Name of the client */
  int gtrid;			/* Identifier of the global transaction */
  int gtrinfo_length;		/* length of the global transaction info */
  unsigned int num_object_locks;	/* Total number of update-type locks acquired by this transaction on the
					 * objects. */
  unsigned int num_page_locks;	/* Total number of update-type locks acquired by this transaction on the pages. */
};

/* Start 2PC protocol. Record information about identifiers of participants. */
typedef struct log_rec_2pc_start LOG_REC_2PC_START;
struct log_rec_2pc_start
{
  char user_name[DB_MAX_USER_LENGTH + 1];	/* Name of the client */
  int gtrid;			/* Identifier of the global tran */
  int num_particps;		/* number of participants */
  int particp_id_length;	/* length of a participant identifier */
};

/*
 * Log the acknowledgment from a participant that it received the commit/abort
 * decision
 */
typedef struct log_rec_2pc_particp_ack LOG_REC_2PC_PARTICP_ACK;
struct log_rec_2pc_particp_ack
{
  int particp_index;		/* Index of the acknowledging participant */
};

/* Log the time of termination of transaction */
typedef struct log_rec_donetime LOG_REC_DONETIME;
struct log_rec_donetime
{
  INT64 at_time;		/* Database creation time. For safety reasons */
};

/* Log the change of the server's HA state */
typedef struct log_rec_ha_server_state LOG_REC_HA_SERVER_STATE;
struct log_rec_ha_server_state
{
  int state;			/* ha_Server_state */
  int dummy;			/* dummy for alignment */

  INT64 at_time;		/* time recorded by active server */
};

typedef struct log_rcv_tdes LOG_RCV_TDES;
struct log_rcv_tdes
{
  /* structure stored in transaction descriptor and used for recovery purpose.
   * currently we store the LSA of a system op start postpone. it is required to finish the system op postpone phase
   * correctly */
  LOG_LSA sysop_start_postpone_lsa;
};

typedef struct log_tdes LOG_TDES;
struct log_tdes
{
/* Transaction descriptor */
  MVCC_INFO mvccinfo;		/* MVCC info */

  int tran_index;		/* Index onto transaction table */
  TRANID trid;			/* Transaction identifier */

  int isloose_end;
  TRAN_STATE state;		/* Transaction state (e.g., Active, aborted) */
  TRAN_ISOLATION isolation;	/* Isolation level */
  int wait_msecs;		/* Wait until this number of milliseconds for locks; also see xlogtb_reset_wait_msecs */
  LOG_LSA head_lsa;		/* First log address of transaction */
  LOG_LSA tail_lsa;		/* Last log record address of transaction */
  LOG_LSA undo_nxlsa;		/* Next log record address of transaction for UNDO purposes. Needed since compensating
				 * log records are logged during UNDO */
  LOG_LSA posp_nxlsa;		/* Next address of a postpone record to be executed. Most of the time is the first
				 * address of a postpone log record */
  LOG_LSA savept_lsa;		/* Address of last savepoint */
  LOG_LSA topop_lsa;		/* Address of last top operation */
  LOG_LSA tail_topresult_lsa;	/* Address of last partial abort/commit */
  int client_id;		/* unique client id */
  int gtrid;			/* Global transaction identifier; used only if this transaction is a participant to a
				 * global transaction and it is prepared to commit. */
  LOG_CLIENTIDS client;		/* Client identification */
  SYNC_RMUTEX rmutex_topop;	/* reentrant mutex to serialize system top operations */
  LOG_TOPOPS_STACK topops;	/* Active top system operations. Used for system permanent nested operations which are
				 * independent from current transaction outcome. */
  LOG_2PC_GTRINFO gtrinfo;	/* Global transaction user information; used to store XID of XA interface. */
  LOG_2PC_COORDINATOR *coord;	/* Information about the participants of the distributed transactions. Used only if
				 * this site is the coordinator. Will be NULL if the transaction is a local one, or if
				 * this site is a participant. */
  int num_unique_btrees;	/* # of unique btrees contained in unique_stat_info array */
  int max_unique_btrees;	/* size of unique_stat_info array */
  BTREE_UNIQUE_STATS *tran_unique_stats;	/* Store local statistical info for multiple row update performed by
						 * client. */
#if defined(_AIX)
  sig_atomic_t interrupt;
#else				/* _AIX */
  volatile sig_atomic_t interrupt;
#endif				/* _AIX */
  /* Set to one when the current execution must be stopped somehow. We stop it by sending an error message during
   * fetching of a page. */
  MODIFIED_CLASS_ENTRY *modified_class_list;	/* List of classes made dirty. */

  int num_transient_classnames;	/* # of transient classnames by this transaction */
  int num_repl_records;		/* # of replication records */
  int cur_repl_record;		/* # of replication records */
  int append_repl_recidx;	/* index of append replication records */
  int fl_mark_repl_recidx;	/* index of flush marked replication record at first */
  struct log_repl *repl_records;	/* replication records */
  LOG_LSA repl_insert_lsa;	/* insert or mvcc update target lsa */
  LOG_LSA repl_update_lsa;	/* in-place update target lsa */
  void *first_save_entry;	/* first save entry for the transaction */

  int suppress_replication;	/* suppress writing replication logs when flag is set */

  struct lob_rb_root lob_locator_root;	/* all LOB locators to be created or delete during a transaction */

  INT64 query_timeout;		/* a query should be executed before query_timeout time. */

  INT64 query_start_time;
  INT64 tran_start_time;
  XASL_ID xasl_id;		/* xasl id of current query */
  LK_RES *waiting_for_res;	/* resource that i'm waiting for */
  int disable_modifications;	/* db_Disable_modification for each tran */

  TRAN_ABORT_REASON tran_abort_reason;

  /* bind values of executed queries in transaction */
  int num_exec_queries;
  DB_VALUE_ARRAY bind_history[MAX_NUM_EXEC_QUERY_HISTORY];

  int num_log_records_written;	/* # of log records generated */

  LOG_TRAN_UPDATE_STATS log_upd_stats;	/* Collects data about inserted/ deleted records during last
					 * command/transaction */
  bool has_deadlock_priority;

  bool block_global_oldest_active_until_commit;

  LOG_RCV_TDES rcv;
};

typedef struct log_addr_tdesarea LOG_ADDR_TDESAREA;
struct log_addr_tdesarea
{
  LOG_TDES *tdesarea;
  LOG_ADDR_TDESAREA *next;
};

/* Transaction Table */
typedef struct trantable TRANTABLE;
struct trantable
{
  int num_total_indices;	/* Number of total transaction indices */
  int num_assigned_indices;	/* Number of assigned transaction indices (i.e., number of active thread/clients) */
  /* Number of coordinator loose end indices */
  int num_coord_loose_end_indices;
  /* Number of prepared participant loose end indices */
  int num_prepared_loose_end_indices;
  int hint_free_index;		/* Hint for a free index */
  /* Number of transactions that must be interrupted */
#if defined(_AIX)
  sig_atomic_t num_interrupts;
#else				/* _AIX */
  volatile sig_atomic_t num_interrupts;
#endif				/* _AIX */
  LOG_ADDR_TDESAREA *area;	/* Contiguous area to transaction descriptors */
  LOG_TDES **all_tdes;		/* Pointers to all transaction descriptors */
};

#define TRANTABLE_INITIALIZER \
  { 0, 0, 0, 0, 0, 0, NULL, NULL }

typedef struct log_crumb LOG_CRUMB;
struct log_crumb
{
  int length;
  const void *data;
};

/* state of recovery process */
typedef enum log_recvphase LOG_RECVPHASE;
enum log_recvphase
{
  LOG_RESTARTED,		/* Normal processing.. recovery has been executed. */
  LOG_RECOVERY_ANALYSIS_PHASE,	/* Start recovering. Find the transactions that were active at the time of the crash */
  LOG_RECOVERY_REDO_PHASE,	/* Redoing phase */
  LOG_RECOVERY_UNDO_PHASE,	/* Undoing phase */
  LOG_RECOVERY_FINISH_2PC_PHASE	/* Finishing up transactions that were in 2PC protocol at the time of the crash */
};

typedef struct log_archives LOG_ARCHIVES;
struct log_archives
{
  int vdes;			/* Last archived accessed */
  LOG_ARV_HEADER hdr;		/* The log archive header */
  int max_unav;			/* Max size of unavailable array */
  int next_unav;		/* Last unavailable entry */
  int *unav_archives;		/* Unavailable archives */
};

#define LOG_ARCHIVES_INITIALIZER \
  { NULL_VOLDES, LOG_ARV_HEADER_INITIALIZER, 0, 0, NULL /* unav_archives */ }

typedef struct background_archiving_info BACKGROUND_ARCHIVING_INFO;
struct background_archiving_info
{
  LOG_PAGEID start_page_id;
  LOG_PAGEID current_page_id;
  LOG_PAGEID last_sync_pageid;
  int vdes;
};

#define BACKGROUND_ARCHIVING_INFO_INITIALIZER \
  { NULL_PAGEID, NULL_PAGEID, NULL_PAGEID, NULL_VOLDES }

typedef struct log_data_addr LOG_DATA_ADDR;
struct log_data_addr
{
  const VFID *vfid;		/* File where the page belong or NULL when the page is not associated with a file */
  PAGE_PTR pgptr;
  PGLENGTH offset;		/* Offset or slot */
};

#define LOG_DATA_ADDR_INITIALIZER \
  { NULL, NULL, 0 }

typedef struct log_prior_node LOG_PRIOR_NODE;
struct log_prior_node
{
  LOG_RECORD_HEADER log_header;
  LOG_LSA start_lsa;		/* for assertion */

  /* data header info */
  int data_header_length;
  char *data_header;

  /* data info */
  int ulength;
  char *udata;
  int rlength;
  char *rdata;

  LOG_PRIOR_NODE *next;
};

typedef struct log_prior_lsa_info LOG_PRIOR_LSA_INFO;
struct log_prior_lsa_info
{
  LOG_LSA prior_lsa;
  LOG_LSA prev_lsa;

  /* list */
  LOG_PRIOR_NODE *prior_list_header;
  LOG_PRIOR_NODE *prior_list_tail;

  INT64 list_size;		/* bytes */

  /* flush list */
  LOG_PRIOR_NODE *prior_flush_list_header;

  pthread_mutex_t prior_lsa_mutex;
};

#define LOG_PRIOR_LSA_INFO_INITIALIZER                     \
  {                                                        \
    /* prior_lsa */                                        \
    {NULL_PAGEID, NULL_OFFSET},                            \
    /* prev_lsa */                                         \
    {NULL_PAGEID, NULL_OFFSET},                            \
    /* list */                                             \
    NULL, NULL,                                            \
    /* list_size */                                        \
    0,                                                     \
    /* prior_flush_list_header */                          \
    NULL,                                                  \
    /* prior_lsa_mutex */                                  \
    PTHREAD_MUTEX_INITIALIZER                              \
  }

/* stores global statistics for a unique btree */
typedef struct global_unique_stats GLOBAL_UNIQUE_STATS;
struct global_unique_stats
{
  BTID btid;			/* btree id */
  GLOBAL_UNIQUE_STATS *stack;	/* used in freelist */
  GLOBAL_UNIQUE_STATS *next;	/* used in hash table */
  pthread_mutex_t mutex;	/* state mutex */
  UINT64 del_id;		/* delete transaction ID (for lock free) */

  LOG_UNIQUE_STATS unique_stats;	/* statistics for btid unique btree */
  LOG_LSA last_log_lsa;		/* The log lsa of the last RVBT_LOG_GLOBAL_UNIQUE_STATS_COMMIT record logged into the
				 * btree header page of btid */
};

/* stores global statistics for all unique btrees */
typedef struct global_unique_stats_table GLOBAL_UNIQUE_STATS_TABLE;
struct global_unique_stats_table
{
  LF_HASH_TABLE unique_stats_hash;	/* hash with btid as key and GLOBAL_UNIQUE_STATS as data */
  LF_ENTRY_DESCRIPTOR unique_stats_descriptor;	/* used by unique_stats_hash */
  LF_FREELIST unique_stats_freelist;	/* used by unique_stats_hash */

  LOG_LSA curr_rcv_rec_lsa;	/* This is used at recovery stage to pass the lsa of the log record to be processed, to 
				 * the record processing funtion, in order to restore the last_log_lsa from
				 * GLOBAL_UNIQUE_STATS */
  bool initialized;		/* true if the current instance was initialized */
};

#define GLOBAL_UNIQUE_STATS_TABLE_INITIALIZER \
 { LF_HASH_TABLE_INITIALIZER, LF_ENTRY_DESCRIPTOR_INITIALIZER, LF_FREELIST_INITIALIZER, LSA_INITIALIZER, false }

#define GLOBAL_UNIQUE_STATS_HASH_SIZE 1000

/* Global structure to trantable, log buffer pool, etc */
typedef struct log_global LOG_GLOBAL;
struct log_global
{
  TRANTABLE trantable;		/* Transaction table */
  LOG_APPEND_INFO append;	/* The log append info */
  LOG_PRIOR_LSA_INFO prior_info;
  LOG_HEADER hdr;		/* The log header */
  LOG_ARCHIVES archive;		/* Current archive information */
  LOG_PAGEID run_nxchkpt_atpageid;
#if defined(SERVER_MODE)
  LOG_LSA flushed_lsa_lower_bound;	/* lsa */
  pthread_mutex_t chkpt_lsa_lock;
#endif				/* SERVER_MODE */
  LOG_LSA chkpt_redo_lsa;
  DKNPAGES chkpt_every_npages;	/* How frequent a checkpoint should be taken ? */
  LOG_RECVPHASE rcv_phase;	/* Phase of the recovery */
  LOG_LSA rcv_phase_lsa;	/* LSA of phase (e.g. Restart) */

#if defined(SERVER_MODE)
  int backup_in_progress;
#else				/* SERVER_MODE */
  LOG_LSA final_restored_lsa;
#endif				/* SERVER_MODE */

  /* Buffer for log hdr I/O, size : SIZEOF_LOG_PAGE_SIZE */
  LOG_PAGE *loghdr_pgptr;

  /* Flush information for dirty log pages */
  LOG_FLUSH_INFO flush_info;

  /* group commit information */
  LOG_GROUP_COMMIT_INFO group_commit_info;
  /* remote log writer information */
  LOGWR_INFO writer_info;
  /* background log archiving info */
  BACKGROUND_ARCHIVING_INFO bg_archive_info;

  MVCCTABLE mvcc_table;		/* MVCC table */
  GLOBAL_UNIQUE_STATS_TABLE unique_stats_table;	/* global unique statistics */
};

/* logging statistics */
typedef struct log_logging_stat
{
  /* logpb_next_append_page() call count */
  unsigned long total_append_page_count;
  /* last created page id for logging */
  LOG_PAGEID last_append_pageid;
  /* time taken to use a page for logging */
  double use_append_page_sec;

  /* log buffer full count */
  unsigned long log_buffer_full_count;
  /* log buffer flush count by replacement */
  unsigned long log_buffer_flush_count_by_replacement;

  /* normal flush */
  /* logpb_flush_all_append_pages() call count */
  unsigned long flushall_append_pages_call_count;
  /* pages count to flush in logpb_flush_all_append_pages() */
  unsigned long last_flush_count_by_trans;
  /* total pages count to flush in logpb_flush_all_append_pages() */
  unsigned long total_flush_count_by_trans;
  /* time taken to flush in logpb_flush_all_append_pages() */
  double last_flush_sec_by_trans;
  /* total time taken to flush in logpb_flush_all_append_pages() */
  double total_flush_sec_by_trans;
  /* logpb_flush_pages_direct() count */
  unsigned long direct_flush_count;

  /* logpb_flush_header() call count */
  unsigned long flush_hdr_call_count;
  /* page count to flush in logpb_flush_header() */
  double last_flush_hdr_sec_by_LFT;
  /* total page count to flush in logpb_flush_header() */
  double total_flush_hdr_sec_by_LFT;

  /* total sync count */
  unsigned long total_sync_count;

  /* commit count */
  unsigned long commit_count;
  /* group commit count */
  unsigned long last_group_commit_count;
  /* total group commit count */
  unsigned long total_group_commit_count;

  /* commit count while using a log page */
  unsigned long last_commit_count_while_using_a_page;
  /* total commit count while using a log page */
  unsigned long total_commit_count_while_using_a_page;

  /* commit count included logpb_flush_all_append_pages */
  unsigned long last_commit_count_in_flush_pages;
  /* total commit count included logpb_flush_all_append_pages */
  unsigned long total_commit_count_in_flush_pages;

  /* group commit request count */
  unsigned long gc_commit_request_count;

  /* wait time for group commit */
  double gc_total_wait_time;

  /* flush count in group commit mode by LFT */
  unsigned long gc_flush_count;

  /* async commit request count */
  unsigned long async_commit_request_count;
} LOG_LOGGING_STAT;


typedef enum log_cs_access_mode LOG_CS_ACCESS_MODE;
enum log_cs_access_mode
{ LOG_CS_FORCE_USE, LOG_CS_SAFE_READER };


#if !defined(SERVER_MODE)
#if !defined(LOG_TRAN_INDEX)
#define LOG_TRAN_INDEX
extern int log_Tran_index;	/* Index onto transaction table for current thread of execution (client) */
#endif /* !LOG_TRAN_INDEX */
#endif /* !SERVER_MODE */

extern LOG_GLOBAL log_Gl;

extern LOG_LOGGING_STAT log_Stat;

#if defined(HAVE_ATOMIC_BUILTINS)
/* Current time in seconds */
extern UINT64 log_Clock_msec;
#endif /* HAVE_ATOMIC_BUILTINS */

/* Name of the database and logs */
extern char log_Path[];
extern char log_Archive_path[];
extern char log_Prefix[];

extern const char *log_Db_fullname;
extern char log_Name_active[];
extern char log_Name_info[];
extern char log_Name_bkupinfo[];
extern char log_Name_volinfo[];
extern char log_Name_bg_archive[];
extern char log_Name_removed_archive[];

#define LOG_RV_RECORD_INSERT		  0x8000
#define LOG_RV_RECORD_DELETE		  0x4000
#define LOG_RV_RECORD_UPDATE_ALL	  0xC000
#define LOG_RV_RECORD_UPDATE_PARTIAL	  0x0000
#define LOG_RV_RECORD_MODIFY_MASK	  0xC000

#define LOG_RV_RECORD_IS_INSERT(flags) \
  (((flags) & LOG_RV_RECORD_MODIFY_MASK) == LOG_RV_RECORD_INSERT)
#define LOG_RV_RECORD_IS_DELETE(flags) \
  (((flags) & LOG_RV_RECORD_MODIFY_MASK) == LOG_RV_RECORD_DELETE)
#define LOG_RV_RECORD_IS_UPDATE_ALL(flags) \
  (((flags) & LOG_RV_RECORD_MODIFY_MASK) == LOG_RV_RECORD_UPDATE_ALL)
#define LOG_RV_RECORD_IS_UPDATE_PARTIAL(flags) \
  (((flags) & LOG_RV_RECORD_MODIFY_MASK) == LOG_RV_RECORD_UPDATE_PARTIAL)

#define LOG_RV_RECORD_SET_MODIFY_MODE(addr, mode) \
  ((addr)->offset = ((addr)->offset & (~LOG_RV_RECORD_MODIFY_MASK)) | (mode))

#define LOG_RV_RECORD_UPDPARTIAL_ALIGNED_SIZE(new_data_size) \
  (DB_ALIGN (new_data_size + OR_SHORT_SIZE + 2 * OR_BYTE_SIZE, INT_ALIGNMENT))

extern int logpb_initialize_pool (THREAD_ENTRY * thread_p);
extern void logpb_finalize_pool (THREAD_ENTRY * thread_p);
extern bool logpb_is_pool_initialized (void);
extern void logpb_invalidate_pool (THREAD_ENTRY * thread_p);
extern LOG_PAGE *logpb_create_page (THREAD_ENTRY * thread_p, LOG_PAGEID pageid);
extern LOG_PAGE *log_pbfetch (LOG_PAGEID pageid);
extern void logpb_set_dirty (THREAD_ENTRY * thread_p, LOG_PAGE * log_pgptr);
extern int logpb_flush_page (THREAD_ENTRY * thread_p, LOG_PAGE * log_pgptr);
extern LOG_PAGEID logpb_get_page_id (LOG_PAGE * log_pgptr);
extern int logpb_initialize_header (THREAD_ENTRY * thread_p, LOG_HEADER * loghdr, const char *prefix_logname,
				    DKNPAGES npages, INT64 * db_creation);
extern LOG_PAGE *logpb_create_header_page (THREAD_ENTRY * thread_p);
extern void logpb_fetch_header (THREAD_ENTRY * thread_p, LOG_HEADER * hdr);
extern void logpb_fetch_header_with_buffer (THREAD_ENTRY * thread_p, LOG_HEADER * hdr, LOG_PAGE * log_pgptr);
extern void logpb_flush_header (THREAD_ENTRY * thread_p);
extern int logpb_fetch_page (THREAD_ENTRY * thread_p, LOG_LSA * req_lsa, LOG_CS_ACCESS_MODE access_mode,
			     LOG_PAGE * log_pgptr);
extern int logpb_copy_page_from_log_buffer (THREAD_ENTRY * thread_p, LOG_PAGEID pageid, LOG_PAGE * log_pgptr);
extern int logpb_copy_page_from_file (THREAD_ENTRY * thread_p, LOG_PAGEID pageid, LOG_PAGE * log_pgptr);
extern int logpb_read_page_from_file (THREAD_ENTRY * thread_p, LOG_PAGEID pageid, LOG_CS_ACCESS_MODE access_mode,
				      LOG_PAGE * log_pgptr);
extern int logpb_read_page_from_active_log (THREAD_ENTRY * thread_p, LOG_PAGEID pageid, int num_pages,
					    LOG_PAGE * log_pgptr);
extern int logpb_write_page_to_disk (THREAD_ENTRY * thread_p, LOG_PAGE * log_pgptr, LOG_PAGEID logical_pageid);
extern PGLENGTH logpb_find_header_parameters (THREAD_ENTRY * thread_p, const char *db_fullname, const char *logpath,
					      const char *prefix_logname, PGLENGTH * io_page_size,
					      PGLENGTH * log_page_size, INT64 * db_creation, float *db_compatibility,
					      int *db_charset);
extern int logpb_fetch_start_append_page (THREAD_ENTRY * thread_p);
extern LOG_PAGE *logpb_fetch_start_append_page_new (THREAD_ENTRY * thread_p);
extern void logpb_flush_pages_direct (THREAD_ENTRY * thread_p);
extern void logpb_flush_pages (THREAD_ENTRY * thread_p, LOG_LSA * flush_lsa);
extern void logpb_invalid_all_append_pages (THREAD_ENTRY * thread_p);
extern void logpb_flush_log_for_wal (THREAD_ENTRY * thread_p, const LOG_LSA * lsa_ptr);
extern LOG_PRIOR_NODE *prior_lsa_alloc_and_copy_data (THREAD_ENTRY * thread_p, LOG_RECTYPE rec_type,
						      LOG_RCVINDEX rcvindex, LOG_DATA_ADDR * addr, int ulength,
						      const char *udata, int rlength, const char *rdata);
extern LOG_PRIOR_NODE *prior_lsa_alloc_and_copy_crumbs (THREAD_ENTRY * thread_p, LOG_RECTYPE rec_type,
							LOG_RCVINDEX rcvindex, LOG_DATA_ADDR * addr,
							const int num_ucrumbs, const LOG_CRUMB * ucrumbs,
							const int num_rcrumbs, const LOG_CRUMB * rcrumbs);
extern LOG_LSA prior_lsa_next_record (THREAD_ENTRY * thread_p, LOG_PRIOR_NODE * node, LOG_TDES * tdes);
extern LOG_LSA prior_lsa_next_record_with_lock (THREAD_ENTRY * thread_p, LOG_PRIOR_NODE * node, LOG_TDES * tdes);

#if defined (ENABLE_UNUSED_FUNCTION)
extern void logpb_remove_append (LOG_TDES * tdes);
#endif
extern void logpb_create_log_info (const char *logname_info, const char *db_fullname);
extern bool logpb_find_volume_info_exist (void);
extern int logpb_create_volume_info (const char *db_fullname);
extern int logpb_recreate_volume_info (THREAD_ENTRY * thread_p);
extern VOLID logpb_add_volume (const char *db_fullname, VOLID new_volid, const char *new_volfullname,
			       DISK_VOLPURPOSE new_volpurpose);
extern int logpb_scan_volume_info (THREAD_ENTRY * thread_p, const char *db_fullname, VOLID ignore_volid,
				   VOLID start_volid, int (*fun) (THREAD_ENTRY * thread_p, VOLID xvolid,
								  const char *vlabel, void *args), void *args);
extern LOG_PHY_PAGEID logpb_to_physical_pageid (LOG_PAGEID logical_pageid);
extern bool logpb_is_page_in_archive (LOG_PAGEID pageid);
extern bool logpb_is_smallest_lsa_in_archive (THREAD_ENTRY * thread_p);
extern int logpb_get_archive_number (THREAD_ENTRY * thread_p, LOG_PAGEID pageid);
extern void logpb_decache_archive_info (THREAD_ENTRY * thread_p);
extern LOG_PAGE *logpb_fetch_from_archive (THREAD_ENTRY * thread_p, LOG_PAGEID pageid, LOG_PAGE * log_pgptr,
					   int *ret_arv_num, LOG_ARV_HEADER * arv_hdr, bool is_fatal);
extern void logpb_remove_archive_logs (THREAD_ENTRY * thread_p, const char *info_reason);
extern int logpb_remove_archive_logs_exceed_limit (THREAD_ENTRY * thread_p, int max_count);
extern void logpb_copy_from_log (THREAD_ENTRY * thread_p, char *area, int length, LOG_LSA * log_lsa,
				 LOG_PAGE * log_pgptr);
extern int logpb_initialize_log_names (THREAD_ENTRY * thread_p, const char *db_fullname, const char *logpath,
				       const char *prefix_logname);
extern bool logpb_exist_log (THREAD_ENTRY * thread_p, const char *db_fullname, const char *logpath,
			     const char *prefix_logname);
#if defined(SERVER_MODE)
extern void logpb_do_checkpoint (void);
#endif /* SERVER_MODE */
extern LOG_PAGEID logpb_checkpoint (THREAD_ENTRY * thread_p);
extern void logpb_dump_checkpoint_trans (FILE * out_fp, int length, void *data);
extern int logpb_backup (THREAD_ENTRY * thread_p, int num_perm_vols, const char *allbackup_path,
			 FILEIO_BACKUP_LEVEL backup_level, bool delete_unneeded_logarchives,
			 const char *backup_verbose_file_path, int num_threads, FILEIO_ZIP_METHOD zip_method,
			 FILEIO_ZIP_LEVEL zip_level, int skip_activelog, int sleep_msecs);
extern int logpb_restore (THREAD_ENTRY * thread_p, const char *db_fullname, const char *logpath,
			  const char *prefix_logname, BO_RESTART_ARG * r_args);
extern int logpb_copy_database (THREAD_ENTRY * thread_p, VOLID num_perm_vols, const char *to_db_fullname,
				const char *to_logpath, const char *to_prefix_logname, const char *toext_path,
				const char *fileof_vols_and_copypaths);
extern int logpb_rename_all_volumes_files (THREAD_ENTRY * thread_p, VOLID num_perm_vols, const char *to_db_fullname,
					   const char *to_logpath, const char *to_prefix_logname,
					   const char *toext_path, const char *fileof_vols_and_renamepaths,
					   int extern_rename, bool force_delete);
extern int logpb_delete (THREAD_ENTRY * thread_p, VOLID num_perm_vols, const char *db_fullname, const char *logpath,
			 const char *prefix_logname, bool force_delete);
extern int logpb_check_exist_any_volumes (THREAD_ENTRY * thread_p, const char *db_fullname, const char *logpath,
					  const char *prefix_logname, char *first_vol, bool * is_exist);
extern void logpb_fatal_error (THREAD_ENTRY * thread_p, bool logexit, const char *file_name, const int lineno,
			       const char *fmt, ...);
extern void logpb_fatal_error_exit_immediately_wo_flush (THREAD_ENTRY * thread_p, const char *file_name,
							 const int lineno, const char *fmt, ...);
extern int logpb_check_and_reset_temp_lsa (THREAD_ENTRY * thread_p, VOLID volid);
extern void logpb_initialize_arv_page_info_table (void);
extern void logpb_initialize_logging_statistics (void);
extern int logpb_background_archiving (THREAD_ENTRY * thread_p);
extern void xlogpb_dump_stat (FILE * outfp);

extern void logpb_dump (THREAD_ENTRY * thread_p, FILE * out_fp);

extern int logpb_remove_all_in_log_path (THREAD_ENTRY * thread_p, const char *db_fullname, const char *logpath,
					 const char *prefix_logname);

extern void log_recovery (THREAD_ENTRY * thread_p, int ismedia_crash, time_t * stopat);
extern LOG_LSA *log_startof_nxrec (THREAD_ENTRY * thread_p, LOG_LSA * lsa, bool canuse_forwaddr);


#if defined (ENABLE_UNUSED_FUNCTION)
extern void log_2pc_define_funs (int (*get_participants) (int *particp_id_length, void **block_particps_ids),
				 int (*lookup_participant) (void *particp_id, int num_particps,
							    void *block_particps_ids),
				 char *(*fmt_participant) (void *particp_id),
				 void (*dump_participants) (FILE * fp, int block_length, void *block_particps_id),
				 int (*send_prepare) (int gtrid, int num_particps, void *block_particps_ids),
				 bool (*send_commit) (int gtrid, int num_particps, int *particp_indices,
						      void *block_particps_ids),
				 bool (*send_abort) (int gtrid, int num_particps, int *particp_indices,
						     void *block_particps_ids, int collect));
#endif
extern char *log_2pc_sprintf_particp (void *particp_id);
extern void log_2pc_dump_participants (FILE * fp, int block_length, void *block_particps_ids);
extern bool log_2pc_send_prepare (int gtrid, int num_particps, void *block_particps_ids);
extern bool log_2pc_send_commit_decision (int gtrid, int num_particps, int *particps_indices, void *block_particps_ids);
extern bool log_2pc_send_abort_decision (int gtrid, int num_particps, int *particps_indices, void *block_particps_ids,
					 bool collect);
#if defined (ENABLE_UNUSED_FUNCTION)
extern int log_get_global_tran_id (THREAD_ENTRY * thread_p);
#endif
extern TRAN_STATE log_2pc_commit (THREAD_ENTRY * thread_p, LOG_TDES * tdes, LOG_2PC_EXECUTE execute_2pc_type,
				  bool * decision);
extern int log_set_global_tran_info (THREAD_ENTRY * thread_p, int gtrid, void *info, int size);
extern int log_get_global_tran_info (THREAD_ENTRY * thread_p, int gtrid, void *buffer, int size);
extern int log_2pc_start (THREAD_ENTRY * thread_p);
extern TRAN_STATE log_2pc_prepare (THREAD_ENTRY * thread_p);
extern int log_2pc_recovery_prepared (THREAD_ENTRY * thread_p, int gtrids[], int size);
extern int log_2pc_attach_global_tran (THREAD_ENTRY * thread_p, int gtrid);
#if defined (ENABLE_UNUSED_FUNCTION)
extern int log_2pc_append_recv_ack (THREAD_ENTRY * thread_p, int particp_index);
#endif
extern TRAN_STATE log_2pc_prepare_global_tran (THREAD_ENTRY * thread_p, int gtrid);
extern void log_2pc_read_prepare (THREAD_ENTRY * thread_p, int acquire_locks, LOG_TDES * tdes, LOG_LSA * lsa,
				  LOG_PAGE * log_pgptr);
extern void log_2pc_dump_gtrinfo (FILE * fp, int length, void *data);
extern void log_2pc_dump_acqobj_locks (FILE * fp, int length, void *data);
#if defined (ENABLE_UNUSED_FUNCTION)
extern void log_2pc_dump_acqpage_locks (FILE * fp, int length, void *data);
#endif
extern LOG_TDES *log_2pc_alloc_coord_info (LOG_TDES * tdes, int num_particps, int particp_id_length,
					   void *block_particps_ids);
extern void log_2pc_free_coord_info (LOG_TDES * tdes);
#if defined (ENABLE_UNUSED_FUNCTION)
extern void log_2pc_crash_participant (THREAD_ENTRY * thread_p);
extern void log_2pc_send_decision_participant (THREAD_ENTRY * thread_p, void *particp_id);
extern bool log_is_tran_in_2pc (THREAD_ENTRY * thread_p);
#endif
extern void log_2pc_recovery_analysis_info (THREAD_ENTRY * thread_p, LOG_TDES * tdes, LOG_LSA * upto_chain_lsa);
extern void log_2pc_recovery (THREAD_ENTRY * thread_p);
extern bool log_is_tran_distributed (LOG_TDES * tdes);
extern bool log_clear_and_is_tran_distributed (LOG_TDES * tdes);

extern void *logtb_realloc_topops_stack (LOG_TDES * tdes, int num_elms);
extern void logtb_define_trantable (THREAD_ENTRY * thread_p, int num_expected_tran_indices, int num_expected_locks);
extern int logtb_define_trantable_log_latch (THREAD_ENTRY * thread_p, int num_expected_tran_indices);
extern void logtb_undefine_trantable (THREAD_ENTRY * thread_p);
extern int logtb_get_number_assigned_tran_indices (void);
extern int logtb_get_number_of_total_tran_indices (void);
#if defined(ENABLE_UNUSED_FUNCTION)
extern bool logtb_am_i_sole_tran (THREAD_ENTRY * thread_p);
extern void logtb_i_am_not_sole_tran (THREAD_ENTRY * thread_p);
#endif
extern bool logtb_am_i_dba_client (THREAD_ENTRY * thread_p);
extern int logtb_assign_tran_index (THREAD_ENTRY * thread_p, TRANID trid, TRAN_STATE state,
				    const BOOT_CLIENT_CREDENTIAL * client_credential, TRAN_STATE * current_state,
				    int wait_msecs, TRAN_ISOLATION isolation);
extern LOG_TDES *logtb_rv_find_allocate_tran_index (THREAD_ENTRY * thread_p, TRANID trid, const LOG_LSA * log_lsa);
extern void logtb_rv_assign_mvccid_for_undo_recovery (THREAD_ENTRY * thread_p, MVCCID mvccid);
extern void logtb_release_tran_index (THREAD_ENTRY * thread_p, int tran_index);
extern void logtb_free_tran_index (THREAD_ENTRY * thread_p, int tran_index);
extern void logtb_free_tran_index_with_undo_lsa (THREAD_ENTRY * thread_p, const LOG_LSA * undo_lsa);
extern void logtb_clear_tdes (THREAD_ENTRY * thread_p, LOG_TDES * tdes);
extern void logtb_finalize_tdes (THREAD_ENTRY * thread_p, LOG_TDES * tdes);
extern int logtb_get_new_tran_id (THREAD_ENTRY * thread_p, LOG_TDES * tdes);
extern int logtb_find_tran_index (THREAD_ENTRY * thread_p, TRANID trid);
#if defined (ENABLE_UNUSED_FUNCTION)
extern int logtb_find_tran_index_host_pid (THREAD_ENTRY * thread_p, const char *host_name, int process_id);
#endif
extern TRANID logtb_find_tranid (int tran_index);
extern TRANID logtb_find_current_tranid (THREAD_ENTRY * thread_p);
extern void logtb_set_client_ids_all (LOG_CLIENTIDS * client, int client_type, const char *client_info,
				      const char *db_user, const char *program_name, const char *login_name,
				      const char *host_name, int process_id);
#if defined (ENABLE_UNUSED_FUNCTION)
extern int logtb_count_clients_with_type (THREAD_ENTRY * thread_p, int client_type);
#endif
extern int logtb_count_clients (THREAD_ENTRY * thread_p);
extern int logtb_count_not_allowed_clients_in_maintenance_mode (THREAD_ENTRY * thread_p);
extern int logtb_find_client_type (int tran_index);
extern char *logtb_find_client_name (int tran_index);
extern void logtb_set_user_name (int tran_index, const char *client_name);
extern void logtb_set_current_user_name (THREAD_ENTRY * thread_p, const char *client_name);
extern char *logtb_find_client_hostname (int tran_index);
extern void logtb_set_current_user_active (THREAD_ENTRY * thread_p, bool is_user_active);
extern int logtb_find_client_name_host_pid (int tran_index, char **client_prog_name, char **client_user_name,
					    char **client_host_name, int *client_pid);
extern int logtb_find_current_client_name_host_pid (char **client_prog_name, char **client_user_name,
						    char **client_host_name, int *client_pid);
extern int logtb_get_client_ids (int tran_index, LOG_CLIENTIDS * client_info);

extern int logtb_find_current_client_type (THREAD_ENTRY * thread_p);
extern char *logtb_find_current_client_name (THREAD_ENTRY * thread_p);
extern char *logtb_find_current_client_hostname (THREAD_ENTRY * thread_p);
extern LOG_LSA *logtb_find_current_tran_lsa (THREAD_ENTRY * thread_p);
extern TRAN_STATE logtb_find_state (int tran_index);
extern int logtb_find_wait_msecs (int tran_index);
extern int logtb_find_current_wait_msecs (THREAD_ENTRY * thread_p);
extern int logtb_find_interrupt (int tran_index, bool * interrupt);
extern TRAN_ISOLATION logtb_find_isolation (int tran_index);
extern TRAN_ISOLATION logtb_find_current_isolation (THREAD_ENTRY * thread_p);
extern bool logtb_set_tran_index_interrupt (THREAD_ENTRY * thread_p, int tran_index, int set);
extern bool logtb_set_suppress_repl_on_transaction (THREAD_ENTRY * thread_p, int tran_index, int set);
extern bool logtb_is_interrupted (THREAD_ENTRY * thread_p, bool clear, bool * continue_checking);
extern bool logtb_is_interrupted_tran (THREAD_ENTRY * thread_p, bool clear, bool * continue_checking, int tran_index);
extern bool logtb_is_active (THREAD_ENTRY * thread_p, TRANID trid);
extern bool logtb_is_current_active (THREAD_ENTRY * thread_p);
extern bool logtb_istran_finished (THREAD_ENTRY * thread_p, TRANID trid);
extern void logtb_disable_update (THREAD_ENTRY * thread_p);
extern void logtb_enable_update (THREAD_ENTRY * thread_p);
extern void logtb_set_to_system_tran_index (THREAD_ENTRY * thread_p);

#if defined (ENABLE_UNUSED_FUNCTION)
extern int logtb_set_current_tran_index (THREAD_ENTRY * thread_p, int tran_index);
extern LOG_LSA *logtb_find_largest_lsa (THREAD_ENTRY * thread_p);
#endif
extern int logtb_set_num_loose_end_trans (THREAD_ENTRY * thread_p);
extern LOG_LSA *log_find_unilaterally_largest_undo_lsa (THREAD_ENTRY * thread_p);
extern void logtb_find_smallest_lsa (THREAD_ENTRY * thread_p, LOG_LSA * lsa);
extern void logtb_find_smallest_and_largest_active_pages (THREAD_ENTRY * thread_p, LOG_PAGEID * smallest,
							  LOG_PAGEID * largest);
extern int logtb_is_tran_modification_disabled (THREAD_ENTRY * thread_p);
extern bool logtb_has_deadlock_priority (int tran_index);
/* For Debugging */
extern void xlogtb_dump_trantable (THREAD_ENTRY * thread_p, FILE * out_fp);

extern bool logpb_need_wal (const LOG_LSA * lsa);
extern char *logpb_backup_level_info_to_string (char *buf, int buf_size, const LOG_HDR_BKUP_LEVEL_INFO * info);
extern void logpb_get_nxio_lsa (LOG_LSA * lsa_p);
extern const char *logpb_perm_status_to_string (enum LOG_PSTATUS val);
extern const char *tran_abort_reason_to_string (TRAN_ABORT_REASON val);
extern int logtb_descriptors_start_scan (THREAD_ENTRY * thread_p, int type, DB_VALUE ** arg_values, int arg_cnt,
					 void **ctx);
extern MVCCID logtb_get_oldest_active_mvccid (THREAD_ENTRY * thread_p);

extern LOG_PAGEID logpb_find_oldest_available_page_id (THREAD_ENTRY * thread_p);
extern int logpb_find_oldest_available_arv_num (THREAD_ENTRY * thread_p);

extern int logtb_get_new_mvccid (THREAD_ENTRY * thread_p, MVCC_INFO * curr_mvcc_info);
extern int logtb_get_new_subtransaction_mvccid (THREAD_ENTRY * thread_p, MVCC_INFO * curr_mvcc_info);

extern MVCCID logtb_find_current_mvccid (THREAD_ENTRY * thread_p);
extern MVCCID logtb_get_current_mvccid (THREAD_ENTRY * thread_p);
extern int logtb_invalidate_snapshot_data (THREAD_ENTRY * thread_p);
extern int xlogtb_get_mvcc_snapshot (THREAD_ENTRY * thread_p);

extern bool logtb_is_current_mvccid (THREAD_ENTRY * thread_p, MVCCID mvccid);
extern bool logtb_is_mvccid_committed (THREAD_ENTRY * thread_p, MVCCID mvccid);
extern MVCC_SNAPSHOT *logtb_get_mvcc_snapshot (THREAD_ENTRY * thread_p);
extern void logtb_complete_mvcc (THREAD_ENTRY * thread_p, LOG_TDES * tdes, bool committed);

extern LOG_TRAN_CLASS_COS *logtb_tran_find_class_cos (THREAD_ENTRY * thread_p, const OID * class_oid, bool create);
extern int logtb_tran_update_unique_stats (THREAD_ENTRY * thread_p, BTID * btid, int n_keys, int n_oids, int n_nulls,
					   bool write_to_log);
extern int logtb_tran_update_btid_unique_stats (THREAD_ENTRY * thread_p, BTID * btid, int n_keys, int n_oids,
						int n_nulls);
extern LOG_TRAN_BTID_UNIQUE_STATS *logtb_tran_find_btid_stats (THREAD_ENTRY * thread_p, const BTID * btid, bool create);
extern int logtb_tran_prepare_count_optim_classes (THREAD_ENTRY * thread_p, const char **classes,
						   LC_PREFETCH_FLAGS * flags, int n_classes);
extern void logtb_tran_reset_count_optim_state (THREAD_ENTRY * thread_p);
extern void logtb_complete_sub_mvcc (THREAD_ENTRY * thread_p, LOG_TDES * tdes);
extern int logtb_find_log_records_count (int tran_index);

extern void logtb_initialize_vacuum_thread_tdes (LOG_TDES * tdes, TRANID trid);
extern int logtb_initialize_global_unique_stats_table (THREAD_ENTRY * thread_p);
extern void logtb_finalize_global_unique_stats_table (THREAD_ENTRY * thread_p);
extern int logtb_get_global_unique_stats (THREAD_ENTRY * thread_p, BTID * btid, int *num_oids, int *num_nulls,
					  int *num_keys);
extern int logtb_rv_update_global_unique_stats_by_abs (THREAD_ENTRY * thread_p, BTID * btid, int num_oids,
						       int num_nulls, int num_keys);
extern int logtb_update_global_unique_stats_by_delta (THREAD_ENTRY * thread_p, BTID * btid, int oid_delta,
						      int null_delta, int key_delta, bool log);
extern int logtb_delete_global_unique_stats (THREAD_ENTRY * thread_p, BTID * btid);
extern int logtb_reflect_global_unique_stats_to_btree (THREAD_ENTRY * thread_p);

extern int log_rv_undoredo_record_partial_changes (THREAD_ENTRY * thread_p, char *rcv_data, int rcv_data_length,
						   RECDES * record, bool is_undo);
extern int log_rv_redo_record_modify (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int log_rv_undo_record_modify (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern char *log_rv_pack_redo_record_changes (char *ptr, int offset_to_data, int old_data_size, int new_data_size,
					      char *new_data);
extern char *log_rv_pack_undo_record_changes (char *ptr, int offset_to_data, int old_data_size, int new_data_size,
					      char *old_data);
extern void logtb_reset_bit_area_start_mvccid (void);

extern void log_set_ha_promotion_time (THREAD_ENTRY * thread_p, INT64 ha_promotion_time);
extern void log_set_db_restore_time (THREAD_ENTRY * thread_p, INT64 db_restore_time);

#if !defined (NDEBUG)
extern int logtb_collect_local_clients (int **local_client_pids);
#endif /* !NDEBUG */

extern int logpb_prior_lsa_append_all_list (THREAD_ENTRY * thread_p);

extern bool logtb_check_class_for_rr_isolation_err (const OID * class_oid);

#endif /* _LOG_IMPL_H_ */
