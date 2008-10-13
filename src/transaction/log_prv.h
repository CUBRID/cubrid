/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * log_prv.h -
 *                                                                             
 */

#ifndef _LOG_INT_HEADER_
#define _LOG_INT_HEADER_

#ident "$Id$"

#include "config.h"

#include <signal.h>
#include <assert.h>

#include "common.h"
#include "logcp.h"
#include "recovery.h"
#include "porting.h"
#include "critical_section.h"
#include "release_string.h"
#include "file_io.h"
#include "thread_impl.h"

#if defined(SOLARIS)
#include <netdb.h>		/* for MAXHOSTNAMELEN */
#endif /* SOLARIS */

#if defined(SERVER_MODE)
#define LOG_CS_ENTER(thread_p) csect_enter((thread_p), CSECT_LOG, INF_WAIT)
#define LOG_CS_EXIT() csect_exit(CSECT_LOG)
#define TR_TABLE_CS_ENTER(thread_p) \
  csect_enter((thread_p), CSECT_TRAN_TABLE, INF_WAIT)
#define TR_TABLE_CS_ENTER_READ_MODE(thread_p) \
  csect_enter_as_reader((thread_p), CSECT_TRAN_TABLE, INF_WAIT)
#define TR_TABLE_CS_EXIT()  csect_exit(CSECT_TRAN_TABLE)
#else /* SERVER_MODE */
#define LOG_CS_ENTER(thread_p)
#define LOG_CS_EXIT()
#define TR_TABLE_CS_ENTER(thread_p)
#define TR_TABLE_CS_ENTER_READ_MODE(thread_p)
#define TR_TABLE_CS_EXIT()
#endif /* SERVER_MODE */

#if defined(LOG_DEBUG) && defined(SERVER_MODE)
#define LOG_CS_OWN()   csect_check_own(CSECT_LOG)
#else /* LOG_DEBUG && SERVER_MODE */
#define LOG_CS_OWN()   (true)
#endif /* LOG_DEBUG && SERVER_MODE */


#define LOG_ESTIMATE_NACTIVE_TRANS      100	/* Estimate num of trans */
#define LOG_ESTIMATE_NOBJ_LOCKS         977	/* Estimate num of locks */

#define LOGAREA_SIZE (IO_PAGESIZE - DB_SIZEOF(LOG_HDRPAGE))	/* Log data area */

/* check if group commit is active */
#define LOG_IS_GROUP_COMMIT_ACTIVE() (PRM_LOG_GROUP_COMMIT_INTERVAL_MSECS > 0)

#define LOG_APPEND_PTR() ((char *)log_Gl.append.log_pgptr->area +             \
                          log_Gl.hdr.append_lsa.offset)

#define LOG_LAST_APPEND_PTR() ((char *)log_Gl.append.log_pgptr->area +        \
                               LOGAREA_SIZE)

#define LOG_PREV_APPEND_PTR()                                                 \
  ((log_Gl.append.delayed_free_log_pgptr != NULL)                             \
   ? ((char *)log_Gl.append.delayed_free_log_pgptr->area +                    \
      log_Gl.append.prev_lsa.offset)                                          \
   : ((char *)log_Gl.append.log_pgptr->area +                                 \
      log_Gl.append.prev_lsa.offset))

#define LOG_APPEND_ALIGN(thread_p, current_setdirty)                                    \
  do {                                                                        \
    if ((current_setdirty) == LOG_SET_DIRTY)                                  \
      logpb_set_dirty(log_Gl.append.log_pgptr, DONT_FREE);                     \
    DB_ALIGN(log_Gl.hdr.append_lsa.offset, INT_ALIGNMENT);                    \
    if (log_Gl.hdr.append_lsa.offset >= (int)LOGAREA_SIZE)                    \
      logpb_next_append_page((thread_p), LOG_DONT_SET_DIRTY);                               \
  } while(0)

#define LOG_APPEND_SETDIRTY_ADD_ALIGN(thread_p, add)                                    \
  do {                                                                        \
    log_Gl.hdr.append_lsa.offset += (add);                                    \
    LOG_APPEND_ALIGN((thread_p), LOG_SET_DIRTY);                                          \
  } while(0)

#define LOG_APPEND_ADVANCE_WHEN_DOESNOT_FIT(thread_p, length)                           \
  do {                                                                        \
    if (log_Gl.hdr.append_lsa.offset + (int)(length) >= (int)LOGAREA_SIZE)    \
      logpb_next_append_page((thread_p), LOG_DONT_SET_DIRTY);                               \
  } while(0)

#define LOG_READ_ALIGN(thread_p, lsa, log_pgptr)                                        \
  do {                                                                        \
    DB_ALIGN((lsa)->offset, INT_ALIGNMENT);                                   \
    while ((lsa)->offset >= (int)LOGAREA_SIZE) {                              \
      assert(log_pgptr != NULL);                                              \
      (lsa)->pageid++;                                                        \
      if ((logpb_fetch_page((thread_p), (lsa)->pageid, log_pgptr)) == NULL)                 \
        logpb_fatal_error((thread_p), true, ARG_FILE_LINE, "LOG_READ_ALIGN");               \
        (lsa)->offset -= LOGAREA_SIZE;                                        \
        DB_ALIGN((lsa)->offset, INT_ALIGNMENT);                               \
    }                                                                         \
  } while(0)

#define LOG_READ_ADD_ALIGN(thread_p, add, lsa, log_pgptr)                               \
  do {                                                                        \
    (lsa)->offset += (add);                                                   \
    LOG_READ_ALIGN((thread_p), (lsa), (log_pgptr));                                       \
  } while(0)

#define LOG_READ_ADVANCE_WHEN_DOESNT_FIT(thread_p, length, lsa, log_pgptr)              \
  do                                                                          \
    if ((lsa)->offset + (int)(length) >= (int)LOGAREA_SIZE) {                 \
      assert(log_pgptr != NULL);                                              \
      (lsa)->pageid++;                                                        \
      if ((logpb_fetch_page((thread_p), (lsa)->pageid, log_pgptr)) == NULL)                 \
        logpb_fatal_error((thread_p), true, ARG_FILE_LINE,                                  \
                        "LOG_READ_ADVANCE_WHEN_DOESNT_FIT");                  \
      (lsa)->offset = 0;                                                      \
      DB_ALIGN((lsa)->offset, INT_ALIGNMENT);                                 \
    }                                                                         \
  while(0)

#define LOG_2PC_NULL_GTRID        (-1)
#define LOG_2PC_OBTAIN_LOCKS      true
#define LOG_2PC_DONT_OBTAIN_LOCKS false

#define LOG_SYSTEM_TRAN_INDEX 0	/* The recovery system transaction index */
#define LOG_SYSTEM_TRANID     0	/* The recovery system transaction       */

#if defined(SERVER_MODE)
#define LOG_FIND_THREAD_TRAN_INDEX(thrd) \
  ((thrd) ? (thrd)->tran_index : thread_get_current_tran_index())
#define LOG_SET_CURRENT_TRAN_INDEX(thrd, index) \
  ((thrd) ? (thrd)->tran_index = (index) : \
            thread_set_current_tran_index ((thrd), (index)))
#else /* SERVER_MODE */
#define LOG_FIND_THREAD_TRAN_INDEX(thrd) (log_Tran_index)
#define LOG_SET_CURRENT_TRAN_INDEX(thrd, index) \
  log_Tran_index = (index)
#endif /* SERVER_MODE */

#define LOG_FIND_TDES(tran_index)                                             \
  (((tran_index) >= 0 && (tran_index) < log_Gl.trantable.num_total_indices)   \
   ? log_Gl.trantable.all_tdes[(tran_index)] : NULL)

#define LOG_FIND_CURRENT_TDES(thrd) \
  LOG_FIND_TDES(LOG_FIND_THREAD_TRAN_INDEX((thrd)))

#define LOG_ISTRAN_ACTIVE(tdes)                                               \
  ((tdes)->state == TRAN_ACTIVE && LOG_ISRESTARTED())

#define LOG_ISTRAN_COMMITTED(tdes)                                            \
  ((tdes)->state == TRAN_UNACTIVE_COMMITTED                             ||    \
   (tdes)->state == TRAN_UNACTIVE_WILL_COMMIT                           ||    \
   (tdes)->state == TRAN_UNACTIVE_COMMITTED_WITH_POSTPONE               ||    \
   (tdes)->state == TRAN_UNACTIVE_COMMITTED_WITH_CLIENT_USER_LOOSE_ENDS ||    \
   (tdes)->state == TRAN_UNACTIVE_2PC_COMMIT_DECISION                   ||    \
   (tdes)->state == TRAN_UNACTIVE_COMMITTED_INFORMING_PARTICIPANTS)

#define LOG_ISTRAN_ABORTED(tdes)                                              \
  ((tdes)->state == TRAN_UNACTIVE_ABORTED                               ||    \
   (tdes)->state == TRAN_UNACTIVE_UNILATERALLY_ABORTED                  ||    \
   (tdes)->state == TRAN_UNACTIVE_ABORTED_WITH_CLIENT_USER_LOOSE_ENDS   ||    \
   (tdes)->state == TRAN_UNACTIVE_2PC_ABORT_DECISION                    ||    \
   (tdes)->state == TRAN_UNACTIVE_ABORTED_INFORMING_PARTICIPANTS)

#define LOG_ISTRAN_LOOSE_ENDS(tdes)                                           \
  ((tdes)->state == TRAN_UNACTIVE_COMMITTED_WITH_CLIENT_USER_LOOSE_ENDS ||    \
   (tdes)->state == TRAN_UNACTIVE_ABORTED_WITH_CLIENT_USER_LOOSE_ENDS   ||    \
   (tdes)->state ==                                                           \
   TRAN_UNACTIVE_XTOPOPE_COMMITTED_WITH_CLIENT_USER_LOOSE_ENDS          ||    \
   (tdes)->state == TRAN_UNACTIVE_COMMITTED_INFORMING_PARTICIPANTS      ||    \
   (tdes)->state == TRAN_UNACTIVE_ABORTED_INFORMING_PARTICIPANTS        ||    \
   (tdes)->state == TRAN_UNACTIVE_2PC_COLLECTING_PARTICIPANT_VOTES      ||    \
   (tdes)->state == TRAN_UNACTIVE_2PC_PREPARE)

#define LOG_ISTRAN_CLIENT_LOOSE_ENDS(tdes)                                    \
  ((tdes)->state == TRAN_UNACTIVE_COMMITTED_WITH_CLIENT_USER_LOOSE_ENDS ||    \
   (tdes)->state == TRAN_UNACTIVE_ABORTED_WITH_CLIENT_USER_LOOSE_ENDS   ||    \
   (tdes)->state ==                                                           \
   TRAN_UNACTIVE_XTOPOPE_COMMITTED_WITH_CLIENT_USER_LOOSE_ENDS)

#define LOG_ISTRAN_2PC_IN_SECOND_PHASE(tdes)                                  \
  ((tdes)->state == TRAN_UNACTIVE_2PC_ABORT_DECISION                    ||    \
   (tdes)->state == TRAN_UNACTIVE_2PC_COMMIT_DECISION                   ||    \
   (tdes)->state == TRAN_UNACTIVE_WILL_COMMIT                           ||    \
   (tdes)->state == TRAN_UNACTIVE_COMMITTED_WITH_POSTPONE               ||    \
   (tdes)->state == TRAN_UNACTIVE_ABORTED_WITH_CLIENT_USER_LOOSE_ENDS   ||    \
   (tdes)->state == TRAN_UNACTIVE_COMMITTED_WITH_CLIENT_USER_LOOSE_ENDS ||    \
   (tdes)->state == TRAN_UNACTIVE_ABORTED_INFORMING_PARTICIPANTS        ||    \
   (tdes)->state == TRAN_UNACTIVE_COMMITTED_INFORMING_PARTICIPANTS)

#define LOG_ISTRAN_2PC(tdes)                                                  \
  ((tdes)->state == TRAN_UNACTIVE_2PC_COLLECTING_PARTICIPANT_VOTES ||         \
   (tdes)->state == TRAN_UNACTIVE_2PC_PREPARE                      ||         \
   LOG_ISTRAN_2PC_IN_SECOND_PHASE(tdes))

#define LOG_ISTRAN_2PC_PREPARE(tdes) \
  ((tdes)->state == TRAN_UNACTIVE_2PC_PREPARE)

#define LOG_ISTRAN_2PC_INFORMING_PARTICIPANTS(tdes)                           \
  ((tdes)->state == TRAN_UNACTIVE_COMMITTED_INFORMING_PARTICIPANTS ||         \
   (tdes)->state == TRAN_UNACTIVE_ABORTED_INFORMING_PARTICIPANTS)


#define MAXLOGNAME          (30 - 12)

#define SIZEOF_LOG_PAGE_PAGESIZE IO_PAGESIZE


#define LOGPB_HEADER_PAGE_ID             (-9)	/* The first log page in the infinite
						   log sequence. It is always kept
						   on the active portion of the log.
						   Log records are not stored on this
						   page. This page is backed up in
						   all archive logs 
						 */

#define LOG_READ_NEXT_TRANID (log_Gl.hdr.next_trid)
#define LOG_HAS_LOGGING_BEEN_IGNORED() \
  (log_Gl.hdr.has_logging_been_skipped == true)

#define LOG_ISRESTARTED() (log_Gl.rcv_phase == LOG_RESTARTED)

#define LOG_GET_ELAPSED_TIME(end_time, start_time) \
            ((double)(end_time.tv_sec - start_time.tv_sec) + \
             (end_time.tv_usec - start_time.tv_usec)/1000000.0)

/* for debugging */
#define LOG_DEBUG_MSG    (0x0002)
#define LOG_DEBUG_STAT   (0x0004)
#define LOG_DEBUG_MUTEX  (0x0008)

#if defined(LOG_DEBUG)
#define LOG_MUTEX_LOCK(rv, mutex) log_lock_mutex_debug(ARG_FILE_LINE, #mutex)
#define LOG_MUTEX_UNLOCK(mutex)   log_unlock_mutex_debug(ARG_FILE_LINE, #mutex)
#else /* LOG_DEBUG */
#define LOG_MUTEX_LOCK(rv, mutex) MUTEX_LOCK(rv, mutex)
#define LOG_MUTEX_UNLOCK(mutex)   MUTEX_UNLOCK(mutex)
#endif /* LOG_DEBUG */


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


typedef enum log_flush LOG_FLUSH;
enum log_flush
{ LOG_DONT_NEED_FLUSH, LOG_NEED_FLUSH };
typedef enum log_setdirty LOG_SETDIRTY;
enum log_setdirty
{ LOG_DONT_SET_DIRTY, LOG_SET_DIRTY };
typedef enum log_getnewtrid LOG_GETNEWTRID;
enum log_getnewtrid
{ LOG_DONT_NEED_NEWTRID, LOG_NEED_NEWTRID };

/*
 * Specify up to int bits of permanent status indicators.
 * Restore in progress is the only one so far, the rest are reserved
 * for future use.  Note these must be specified and used as mask values
 * to test and set individual bits.
 */
enum LOG_PSTATUS
{
  LOG_PSTAT_CLEAR = 0x0,
  LOG_PSTAT_BACKUP_INPROGRESS = 0x1,	/* only one backup at a time */
  LOG_PSTAT_RESTORE_INPROGRESS = 0x2	/* unset upon successful restore */
};

/*
 *       		          LOG PAGE
 */

typedef struct log_hdrpage LOG_HDRPAGE;
struct log_hdrpage
{
  PAGEID logical_pageid;	/* Logical pageid in infinite log                 */
  PGLENGTH offset;		/* Offset of first log record in this page.
				   This may be useful when previous log page
				   is corrupted and an archive of that page does
				   not exist. Instead of losing the whole log
				   because of such bad page, we could salvage the
				   log starting at the offset address, that is,
				   at the next log record
				 */
};

/* WARNING:
 * Don't use sizeof(LOG_PAGE) or of any structure that contains it
 * Use macro SIZEOF_LOG_PAGE_PAGESIZE instead.
 * It is also bad idea to allocate a variable for LOG_PAGE on the stack.
 */

typedef struct log_page LOG_PAGE;
struct log_page
{				/* The log page */
  LOG_HDRPAGE hdr;
  char area[1];
};


#if defined(LOG_DEBUG)
typedef struct log_mutex_info
{
  int lock_count;
  THREAD_T owner;
} log_mutex_info;
#endif /* LOG_DEBUG */

typedef enum
{
  /* flush pages in commit process */
  LOG_FLUSH_NORMAL = 0,
  /* force to flush pages (chkpt...) */
  LOG_FLUSH_FORCE,
  /* caller have to execute flushall append pages directly
     because some threads can't release log cs. (archive, log buffer replace)
   */
  LOG_FLUSH_DIRECT
} LOG_FLUSH_TYPE;

/*
 * Flush information shared by LFT and normal transaction.
 * Transaction in commit phase has to flush all toflush array's pages.
 */
typedef struct log_flush_info LOG_FLUSH_INFO;
struct log_flush_info
{
  /* Size of array to log append pages to flush */
  int max_toflush;

  /* Number of log append pages that can be flush
   * Not all of the append pages may be full.
   */
  int num_toflush;
  LOG_FLUSH_TYPE flush_type;

  /* A sorted order of log append free pages to flush */
  LOG_PAGE **toflush;

#if defined(SERVER_MODE)
  /* for pretecting LOG_FLUSH_INFO */
  MUTEX_T flush_mutex;
#if defined(LOG_DEBUG)

  /* Volatile for debugging in release mode.(-DLOG_DEBUG)
   * It doesn't make an impact on execution in debug mode.
   * log_*_mutex_not_own can't work properly in release mode
   * without volatile because mutex_info can be changed unexpectedly.
   * log_*_mutex_not_own is only called carefully in single thread context now.
   * (when only log_cs acquired)
   */
  volatile log_mutex_info mutex_info;
#endif				/* LOG_DEBUG */
#endif				/* SERVER_MODE */
};

typedef struct log_group_commit_info LOG_GROUP_COMMIT_INFO;
struct log_group_commit_info
{
  /* group commit waiters count */
  int waiters;
  MUTEX_T gc_mutex;
  COND_T gc_cond;
};

struct log_append_info
{
  int vdes;			/* Volume descriptor of active log            */
  LOG_LSA nxio_lsa;		/* Lowest log sequence number which has not
				 * been written to disk (for WAL).
				 */
  LOG_LSA prev_lsa;		/* Address of last append log record          */
  LOG_PAGE *log_pgptr;		/* The log page which is fixed                */
  LOG_PAGE *delayed_free_log_pgptr;	/* Delay freeing a log append page       */

};

typedef enum
{
  LOG_2PC_EXECUTE_FULL,		/* For the root coordinator */
  LOG_2PC_EXECUTE_PREPARE,	/* For a participant that is also a non root
				   coordinator execute the first phase of
				   2PC
				 */
  LOG_2PC_EXECUTE_COMMIT_DECISION,	/* For a participant that is also a non root
					   coordinator execute the second phase of
					   2PC. The root coordinator has decided a
					   commit decision
					 */
  LOG_2PC_EXECUTE_ABORT_DECISION	/* For a participant that is also a non root
					   coordinator execute the second phase of
					   2PC. The root coordinator has decided an
					   abort decision with or without going to
					   the first phase (i.e., prepare) of the
					   2PC
					 */
} LOG_2PC_EXECUTE;

typedef struct log_2pc_gtrinfo LOG_2PC_GTRINFO;
struct log_2pc_gtrinfo
{				/* Global transaction user information */
  int info_length;
  void *info_data;
};

typedef struct log_2pc_coordinator LOG_2PC_COORDINATOR;
struct log_2pc_coordinator
{				/* Coordinator maintains this info */
  int num_particps;		/* Number of participating sites      */
  int particp_id_length;	/* Length of a participant identifier */
  void *block_particps_ids;	/* A block of participants identfiers */
  int *ack_received;		/* Acknowledgement received vector   */
};

struct log_topops_addresses
{
  LOG_LSA lastparent_lsa;	/* The last address of the parent transaction.
				 * This is needed for undo of the top system
				 * action
				 */
  LOG_LSA posp_lsa;		/* The first address of a pospone log record
				 * for top system operation. We add this since
				 * it is reset during recovery to the last
				 * reference pospone address.
				 */
  LOG_LSA client_posp_lsa;	/* The first address of a client pospone log record
				 * for top system operation.
				 */
  LOG_LSA client_undo_lsa;	/* The first address of a client undo log record
				 * for top system operation.
				 */
};

typedef struct log_topops_stack LOG_TOPOPS_STACK;
struct log_topops_stack
{
  int max;			/* Size of stack                   */
  int last;			/* Last entry in stack             */
  struct log_topops_addresses *stack;	/* Stack for push and pop of top
					   system actions
					 */
};

typedef struct log_clientids LOG_CLIENTIDS;
struct log_clientids
{				/* User, host, and process_id */
  char prog_name[PATH_MAX];
  char user_name[LOG_USERNAME_MAX];
  char host_name[MAXHOSTNAMELEN];
  int process_id;
};

typedef struct modified_class_entry MODIFIED_CLASS_ENTRY;
struct modified_class_entry
{
  OID class_oid;
  struct modified_class_entry *next;
};


typedef struct log_tdes LOG_TDES;
struct log_tdes
{				/* Transaction descriptor */
  int tran_index;		/* Index onto transaction table          */
  TRANID trid;			/* Transaction identifier                */
  int isloose_end;		/* is this index waiting to be assigned to
				   user client to perform client loose ends
				   or is waiting on two phase related
				   commits such as:
				   a) If coordinator: collection of
				   acknowledgments from participants
				   that have been notified the 2PC
				   decision.
				   b) If participant: has voted and waiting
				   for the decision.
				 */
  TRAN_STATE state;		/* Transaction state (e.g., Active,
				   aborted)
				 */
  TRAN_ISOLATION isolation;	/* Isolation level                       */
  int waitsecs;			/* Wait until this number of miliseconds
				   for locks
				 */
  LOG_LSA head_lsa;		/* First log address of transaction      */
  LOG_LSA tail_lsa;		/* Last log record address of
				   transaction
				 */
  LOG_LSA undo_nxlsa;		/* Next log record address of transaction
				   for UNDO purposes. Needed since
				   compensating log records are logged
				   during UNDO
				 */
  LOG_LSA posp_nxlsa;		/* Next address of a postpone record to be
				   executed. Most of the time is the first
				   address of a postpone log record
				 */
  LOG_LSA savept_lsa;		/* Address of last savepoint             */
  LOG_LSA tail_topresult_lsa;	/* Address of last partial abort/commit  */
  LOG_LSA client_undo_lsa;	/* First address of a client undo log
				   record
				 */
  LOG_LSA client_posp_lsa;	/* First address of a client postpone
				   log
				 */
  int client_id;		/* unique client id */
  LOG_CLIENTIDS client;		/* Client identification            */
  CSS_CRITICAL_SECTION cs_topop;	/* critical section to serialize
					   system top operations
					 */
  LOG_TOPOPS_STACK topops;	/* Active top system operations.
				   Used for system permanent nested
				   operations which are independent from
				   current transaction outcome.
				 */
  int gtrid;			/* Global transaction identifier; used only
				   if this transaction is a participant to
				   a global transaction and it is prepared
				   to commit.
				 */
  LOG_2PC_GTRINFO gtrinfo;	/* Global transaction user information;
				   used to store XID of XA interface.
				 */
  LOG_2PC_COORDINATOR *coord;	/* Information about the participants of
				   the distributed transactions. Used only
				   if this site is the coordinator. Will be
				   NULL if the transaction is a local one,
				   or if this site is a participant.
				 */
  int num_unique_btrees;	/* # of unique btrees contained in
				   unique_stat_info array
				 */
  int max_unique_btrees;	/* size of unique_stat_info array */
  BTREE_UNIQUE_STATS *unique_stat_info;	/* Store local statistical info for
					   multiple row update performed by client.
					 */
#if defined(_AIX)
  sig_atomic_t interrupt;
#else				/* _AIX */
  volatile sig_atomic_t interrupt;
#endif				/* _AIX */
  /* Set to one when the current execution
     must be stopped somehow. We stop it by
     sending an error message during fetching
     of a page.
   */
  MODIFIED_CLASS_ENTRY *modified_class_list;	/* List of classes made dirty. */

  int num_transient_classnames;	/* # of transient classnames in
				   lc_Mht_classnames by this
				   transaction
				 */
  int num_repl_records;		/* # of replication records */
  int cur_repl_record;		/* # of replication records */
  int append_repl_recidx;	/* index of append replication records */
  int fl_mark_repl_recidx;	/* index of flush marked
				   replication record at first
				 */
  struct log_repl *repl_records;	/* replication records */
  LOG_LSA repl_insert_lsa;	/* insert target lsa */
  LOG_LSA repl_update_lsa;	/* update target lsa */
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
  int num_total_indices;	/* Number of total transaction indices     */
  int num_assigned_indices;	/* Number of assigned transaction indices
				 * (i.e., number of active thread/clients)
				 */
  /* Number of client loose end indices */
  int num_client_loose_end_indices;
  /* Number of coordinator loose end indices */
  int num_coord_loose_end_indices;
  /* Number of prepared participant loose end
   * indices
   */
  int num_prepared_loose_end_indices;
  int hint_free_index;		/* Hint for a free index                   */
  /* Number of transactions that must be 
   * interrupted
   */
#if defined(_AIX)
  sig_atomic_t num_interrupts;
#else				/* _AIX */
  volatile sig_atomic_t num_interrupts;
#endif				/* _AIX */
  struct log_addr_tdesarea *area;	/* Contiguous area to transaction
					 * descriptors
					 */
  LOG_TDES **all_tdes;		/* Pointers to all transaction descriptors */
};

/*
 * This structure encapsulates various information and metrics related
 * to each backup level.
 * Estimates and heuristics are not currently used but are placeholder
 * for the future to avoid changing the physical representation again.
 */
typedef struct log_hdr_bkup_level_info LOG_HDR_BKUP_LEVEL_INFO;
struct log_hdr_bkup_level_info
{
  time_t bkup_attime;		/* Timestamp when this backup lsa taken */
  int ndirty_pages_post_bkup;	/* number of pages written since the lsa
				   for this backup level. */
  time_t io_baseln_time;	/* time (secs.) to write a single page */
  int io_numpages;		/* total number of pages in last backup */
  time_t io_bkuptime;		/* total time to write the backup  */
};

/*
 * LOG HEADER INFORMATION
 */
struct log_header
{				/* Log header information */
  char magic[CUBRID_MAGIC_MAX_LENGTH];	/* Magic value for file/magic Unix
					 * utility
					 */
  time_t db_creation;		/* Database creation time. For safety reasons,
				 * this value is set on all volumes and the
				 * log. The value is generated by the log
				 * manager
				 */
  char db_release[REL_MAX_RELEASE_LENGTH];	/* CUBRID Release */
  float db_compatibility;	/* Compatibility of the database against the
				 * current release of CUBRID
				 */
  PGLENGTH db_iopagesize;	/* Size of pages in the database. For safety
				 * reasons this value is recorded in the log
				 * to make sure that the database is always
				 * run with the same page size
				 */
  int is_shutdown;		/* Was the log shutdown ?                   */
  TRANID next_trid;		/* Next Transaction identifier              */
  int avg_ntrans;		/* Number of average transactions           */
  int avg_nlocks;		/* Average number of object locks           */
  DKNPAGES npages;		/* Number of pages in the active log portion.
				 * Does not include the log header page.
				 */
  PAGEID fpageid;		/* Logical pageid at physical location 1 in
				 * active log
				 */
  LOG_LSA append_lsa;		/* Current append location                  */
  LOG_LSA chkpt_lsa;		/* Lowest log sequence address to start the
				 * recovery process
				 */
  PAGEID nxarv_pageid;		/* Next logical page to archive             */
  PAGEID nxarv_phy_pageid;	/* Physical location of logical page to
				 * archive
				 */
  int nxarv_num;		/* Next log archive number                  */
  int last_arv_num_for_syscrashes;	/* Last log archive needed for system
					 * crashes
					 */
  int last_deleted_arv_num;	/* Last deleted archive number              */
  bool has_logging_been_skipped;	/* Has logging been skipped ?       */
  LOG_LSA bkup_level0_lsa;	/* Lsa of backup level 0                    */
  LOG_LSA bkup_level1_lsa;	/* Lsa of backup level 1                    */
  LOG_LSA bkup_level2_lsa;	/* Lsa of backup level 2                    */
  char prefix_name[MAXLOGNAME];	/* Log prefix name                          */
  int lowest_arv_num_for_backup;	/* Last log archive needed to guarantee
					 * a consistent restore for the most
					 * recent backup
					 */
  int highest_arv_num_for_backup;	/* with 'lowest' above, forms a range
					 * of all needed archives to make the
					 * most recent backup consistent.
					 */
  int perm_status;		/* Reserved for future expansion and
				 * permanent status indicators,
				 * e.g. to mark RESTORE_IN_PROGRESS
				 */
  LOG_HDR_BKUP_LEVEL_INFO bkinfo[FILEIO_BACKUP_UNDEFINED_LEVEL];
  /* backup specific info
   * for future growth
   */
};

struct log_arv_header
{				/* Log archive header information */
  char magic[CUBRID_MAGIC_MAX_LENGTH];	/* Magic value for file/magic Unix
					 * utility     */
  time_t db_creation;		/* Database creation time. For safety reasons,
				 * this value is set on all volumes and the
				 * log. The value is generated by the log
				 * manager
				 */
  TRANID next_trid;		/* Next Transaction identifier              */
  DKNPAGES npages;		/* Number of pages in the archive log       */
  PAGEID fpageid;		/* Logical pageid at physical location 1 in
				 * archive log
				 */
  int arv_num;			/* The archive number                       */
};

typedef enum
{
  /* In order of likely of appearance in the log */
  LOG_SMALLER_LOGREC_TYPE,	/* A lower bound check             */

  LOG_CLIENT_NAME,		/* Name of the client associated
				   with transaction
				 */
  LOG_UNDOREDO_DATA,		/* An undo and redo data record    */
  LOG_UNDO_DATA,		/* Only undo data                  */
  LOG_REDO_DATA,		/* Only redo data                  */
  LOG_DBEXTERN_REDO_DATA,	/* Only database external redo data */
  LOG_POSTPONE,			/* Postpone redo data              */
  LOG_RUN_POSTPONE,		/* Run/redo a postpone data. Only
				   for transactions committed with
				   postpone operations
				 */
  LOG_COMPENSATE,		/* Compensation record (compensate a
				   undo record of an aborted tran)
				 */
  LOG_LCOMPENSATE,		/* Compensation record (compensate a
				   logical undo of an aborted tran)
				 */
  LOG_CLIENT_USER_UNDO_DATA,	/* User client undo data           */
  LOG_CLIENT_USER_POSTPONE_DATA,	/* User client postpone            */
  LOG_RUN_NEXT_CLIENT_UNDO,	/* Used to indicate that a set of
				   client undo operations has
				   been executed and the address of
				   the next client undo to execute
				 */
  LOG_RUN_NEXT_CLIENT_POSTPONE,	/* Used to indicate that a set of
				   client postpone operations has
				   been executed and the address of
				   the next client postpone to
				   execute
				 */
  LOG_WILL_COMMIT,		/* Transaction will be committed */
  LOG_COMMIT_WITH_POSTPONE,	/* Committing server postpon
				   operations
				 */
  LOG_COMMIT_WITH_CLIENT_USER_LOOSE_ENDS,	/* Committing client postpone
						   operations
						 */
  LOG_COMMIT,			/* A commit record                 */
  LOG_COMMIT_TOPOPE_WITH_POSTPONE,	/* Committing server top system
					   postpone operations
					 */
  LOG_COMMIT_TOPOPE_WITH_CLIENT_USER_LOOSE_ENDS,
  /* Committing client postpone
     top system operations
   */
  LOG_COMMIT_TOPOPE,		/* A partial commit record, usually
				   from a top system operation
				 */
  LOG_ABORT_WITH_CLIENT_USER_LOOSE_ENDS,	/* Aborting client loose ends      */
  LOG_ABORT,			/* An abort record                 */
  LOG_ABORT_TOPOPE_WITH_CLIENT_USER_LOOSE_ENDS,
  /* Committing client postpone
     top system operations
   */
  LOG_ABORT_TOPOPE,		/* A partial abort record, usually
				   from a top system operation or
				   partial rollback to a save point
				 */
  LOG_START_CHKPT,		/* Start a checkpoint              */
  LOG_END_CHKPT,		/* Checkpoint information          */
  LOG_SAVEPOINT,		/* A user savepoint record         */
  LOG_2PC_PREPARE,		/* A prepare to commit record      */
  LOG_2PC_START,		/* Start the 2PC protocol by sending
				   vote request messages to
				   participants of distributed tran.
				 */
  LOG_2PC_COMMIT_DECISION,	/* Beginning of the second phase of
				   2PC, need to perform local &
				   global commits.
				 */
  LOG_2PC_ABORT_DECISION,	/* Beginning of the second phase of
				   2PC, need to perform local &
				   global aborts.
				 */
  LOG_2PC_COMMIT_INFORM_PARTICPS,	/* Committing, need to inform the
					   participants
					 */
  LOG_2PC_ABORT_INFORM_PARTICPS,	/* Aborting, need to inform the
					   participants
					 */
  LOG_2PC_RECV_ACK,		/* Received ack. from the
				   participant that it received the
				   decision on the fate of dist.
				   trans.
				 */
  LOG_END_OF_LOG,		/* End of log                      */
  LOG_DUMMY_HEAD_POSTPONE,	/* A dummy log record. No-op       */
  LOG_DUMMY_CRASH_RECOVERY,	/* A dummy log record which indicate
				   the start of crash recovery.
				   No-op
				 */
  LOG_DUMMY_FILLPAGE_FORARCHIVE,	/* Indicates logical end of current
					   page so it could be archived
					   safely. No-op
					 */
  LOG_REPLICATION_DATA,		/* Replicaion Rog Record
				   for record updates
				 */
  LOG_REPLICATION_SCHEMA,	/* Replicaion Rog Record
				   for schema, index, trigger, or system catalog updates
				 */
  LOG_UNLOCK_COMMIT,		/* for repl_agent to guarantee the order of  */
  LOG_UNLOCK_ABORT,		/* transaction commit, we append the unlock info.
				   before calling lock_unlock_all()
				 */
  LOG_DIFF_UNDOREDO_DATA,	/* diff undo redo data             */
  LOG_LARGER_LOGREC_TYPE	/* A higher bound for checks       */
} LOG_RECTYPE;

typedef enum
{
  LOG_REPL_DONT_NEED_FLUSH = -1,	/* no flush */
  LOG_REPL_COMMIT_NEED_FLUSH = 0,	/* log must be flushed at commit */
  LOG_REPL_NEED_FLUSH = 1	/* log must be flushed at commit
				 *  and rollback
				 */
} LOG_REPL_FLUSH;

typedef struct log_repl LOG_REPL_RECORD;
struct log_repl
{
  LOG_RECTYPE repl_type;	/* LOG_REPLICATION_DATA
				 *  or LOG_REPLICATION_SCHEMA */
  OID inst_oid;
  LOG_LSA lsa;
  char *repl_data;		/* the content of the replication log record */
  int length;
  LOG_REPL_FLUSH must_flush;
};

/* Description of a log record */
struct log_rec
{
  TRANID trid;			/* Transaction identifier of the log record  */
  LOG_LSA prev_tranlsa;		/* Address of previous log record for the
				 * same transaction
				 */
  LOG_LSA back_lsa;		/* Backward log address                      */
  LOG_LSA forw_lsa;		/* Forward  log address                      */
  LOG_RECTYPE type;		/* Log record type (e.g., commit, abort)     */
};

/* Common information of log data records */
struct log_data
{
  LOG_RCVINDEX rcvindex;	/* Index to recovery function                */
  PAGEID pageid;		/* Pageid of recovery data                   */
  PGLENGTH offset;		/* offset of recovery data in pageid         */
  VOLID volid;			/* Volume identifier of recovery data        */
};

/* Information of undo_redo log records */
struct log_undoredo
{
  struct log_data data;		/* Location of recovery data                 */
  int ulength;			/* Length of undo data                       */
  int rlength;			/* Length of redo data                       */
};

/* Information of undo log records */
struct log_undo
{
  struct log_data data;		/* Location of recovery data                 */
  int length;			/* Length of undo data                       */
};

/* Information of redo log records */
struct log_redo
{
  struct log_data data;		/* Location of recovery data                 */
  int length;			/* Length of redo data                       */
};

/* Information of database external redo log records */
struct log_dbout_redo
{
  LOG_RCVINDEX rcvindex;	/* Index to recovery function                */
  int length;			/* Length of redo data                       */
};

/* Information of a compensating log records */
struct log_compensate
{
  struct log_data data;		/* Location of recovery data                 */
  LOG_LSA undo_nxlsa;		/* Address of next log record to undo        */
  int length;			/* Length of compensating data               */
};

/*
 * Information of a logical compensate record which marks the end of logical
 * undo
 */
struct log_logical_compensate
{
  LOG_RCVINDEX rcvindex;	/* Index to recovery function                */
  LOG_LSA undo_nxlsa;		/* Address of next log record to undo        */
};

/* This entry is included during commit */
struct log_start_postpone
{
  LOG_LSA posp_lsa;
};

/* This entry is included during the commit of top system operations */
struct log_topope_start_postpone
{
  LOG_LSA lastparent_lsa;	/* The last address of the parent transaction. */
  LOG_LSA posp_lsa;		/* Address where the first postpone operation
				 * start
				 */
};

/* Information of execution of a postpone data */
struct log_run_postpone
{
  struct log_data data;		/* Location of recovery data                 */
  LOG_LSA ref_lsa;		/* Address of the original postpone record   */
  int length;			/* Length of redo data                       */
};

/* A checkpoint record */
struct log_chkpt
{
  LOG_LSA redo_lsa;		/* Oldest LSA of dirty data page in page
				 * buffers
				 */
  int ntrans;			/* Number of active transactions             */
  int ntops;			/* Total number of system operations         */
};

/* replication log structure */
struct log_replication
{
  LOG_LSA lsa;
  int length;
};

/* Transaction descriptor */
struct log_chkpt_trans
{
  int isloose_end;		/* is this index waiting to be assigned to
				 * user client to perform client loose ends
				 * or is waiting on two phase related commits
				 * such as:
				 * a) If coordinator: collection of
				 *    acknowledgments from participants that
				 *    have been notified the 2PC decision.
				 * b) If participant: has voted and waiting
				 *    for the decision.
				 */
  TRANID trid;			/* Transaction identifier */
  TRAN_STATE state;		/* Transaction state (e.g., Active, aborted)  */
  LOG_LSA head_lsa;		/* First log address of transaction */
  LOG_LSA tail_lsa;		/* Last log record address of transaction */
  LOG_LSA undo_nxlsa;		/* Next log record address of transaction for
				 * UNDO purposes. Needed since compensating log
				 * records are logged during UNDO
				 */
  LOG_LSA posp_nxlsa;		/* First address of a postpone record */
  LOG_LSA savept_lsa;		/* Address of last savepoint */
  LOG_LSA tail_topresult_lsa;	/* Address of last partial abort/commit */
  LOG_LSA client_undo_lsa;	/* First address of a client undo log record  */
  LOG_LSA client_posp_lsa;	/* First address of a client postpone log
				 * record
				 */
  char user_name[LOG_USERNAME_MAX];	/* Name of the client */

};

struct log_chkpt_topops_commit_posp
{
  TRANID trid;			/* Transaction identifier */
  LOG_LSA lastparent_lsa;	/* The last address of the parent transaction.
				 * This is needed for undo of the top system
				 * action
				 */
  LOG_LSA posp_lsa;		/* The first address of a pospone log record
				 * for top system operation. We add this since
				 * it is reset during recovery to the last
				 * reference pospone address.
				 */
  LOG_LSA client_posp_lsa;	/* The first address of a client pospone log record
				 * for top system operation.
				 */
  LOG_LSA client_undo_lsa;	/* The first address of a client undo log record
				 * for top system operation.
				 */

};

/* Information of a client undo/postpone log record */
struct log_client
{
  LOG_RCVCLIENT_INDEX rcvclient_index;	/* Index to recovery function on
					 * client
					 */
  int length;			/* Length of client data          */
};

/* Hint where to start looking for client actions either for commit or abort */
struct log_start_client
{
  LOG_LSA lsa;			/* Starting page with a client record */
};

/* Hint where to start looking for client actions either for commit or abort */
struct log_topope_start_client
{
  LOG_LSA lastparent_lsa;	/* The last address of the parent transaction. */
  LOG_LSA lsa;			/* Starting page with a client record          */
};

/*
 * Information of execution of a client undo or postpone data. This record
 * is needed since we do not have compensating records and run postpone
 * records for client actions
 */
struct log_run_client
{
  LOG_LSA nxlsa;		/* Check for the next undo client record to check */
};

struct log_savept
{
  LOG_LSA prv_savept;		/* Previous savepoint record */
  int length;			/* Savepoint name */
};

struct log_topop_result
{
  LOG_LSA lastparent_lsa;	/* Next log record address of transaction for
				 * UNDO purposes. Last address before the top
				 * action
				 */
  LOG_LSA prv_topresult_lsa;	/* Previous top action (either, partial abort
				 * or partial commit) address
				 */
};

/* Log a prepare to commit record */
struct log_2pc_prepcommit
{
  char user_name[LOG_USERNAME_MAX];	/* Name of the client */
  int gtrid;			/* Identifier of the global transaction */
  int gtrinfo_length;		/* length of the global transaction info */
  unsigned int num_object_locks;	/* Total number of update-type locks
					 * acquired by this transaction on the
					 * objects.
					 */
  unsigned int num_page_locks;	/* Total number of update-type locks
				 * acquired by this transaction on the
				 * pages.
				 */
};

/* Start 2PC protocol. Record information about identifiers of participants. */
struct log_2pc_start
{
  char user_name[LOG_USERNAME_MAX];	/* Name of the client */
  int gtrid;			/* Identifier of the global tran      */
  int num_particps;		/* number of participants             */
  int particp_id_length;	/* length of a participant identifier */
};

/*
 * Log the acknowledgement from a participant that it received the commit/abort
 * decision
 */
struct log_2pc_particp_ack
{
  int particp_index;		/* Index of the acknowledging participant   */
};

/* Log the time of termination of transaction */
struct log_donetime
{
  time_t at_time;		/* Database creation time. For safety reasons */
};

typedef struct log_crumb LOG_CRUMB;
struct log_crumb
{
  int length;
  const void *data;
};

/* state of recovery process */
typedef enum
{
  LOG_RESTARTED,		/* Normal processing.. recovery has been
				   executed.
				 */
  LOG_RECOVERY_ANALYSIS_PHASE,	/* Start recovering. Find the transactions
				   that were active at the time of the crash
				 */
  LOG_RECOVERY_REDO_PHASE,	/* Redoing phase */
  LOG_RECOVERY_UNDO_PHASE,	/* Undoing phase */
  LOG_RECOVERY_FINISH_2PC_PHASE	/* Finishing up transactions that were in 2PC
				   protocol at the time of the crash
				 */
} LOG_RECVPHASE;

struct log_archives
{
  int vdes;			/* Last archived accessed        */
  struct log_arv_header hdr;	/* The log archive header        */
  int max_unav;			/* Max size of unavailable array */
  int next_unav;		/* Last unavailable entry        */
  int *unav_archives;		/* Unavailable archives          */
};

/* Global structure to trantable, log buffer pool, etc */
typedef struct log_global LOG_GLOBAL;
struct log_global
{
  TRANTABLE trantable;		/* Transaction table           */
  struct log_append_info append;	/* The log append info         */
  struct log_header hdr;	/* The log header              */
  struct log_archives archive;	/* Current archive information */
  int run_nxchkpt_atpageid;	/* Next checkpoint at page.
				   This filed is needed since
				   the checkpoint address is
				   reset during removal of
				   archivals and when an
				   archival is skipped.
				   This value is set to
				   NULL_PAGEID when a chkpt is
				   running.
				 */
#if defined(SERVER_MODE)
  LOG_LSA flushed_lsa_lower_bound;	/* lsa */
  MUTEX_T chkpt_lsa_lock;
#endif				/* SERVER_MODE */
  DKNPAGES chkpt_every_npages;	/* How frequent a checkpoint
				   should be taken ?
				 */
  LOG_RECVPHASE rcv_phase;	/* Phase of the recovery       */
  LOG_LSA rcv_phase_lsa;	/* LSA of phase (e.g. Restart) */

#if defined(SERVER_MODE)
  int backup_in_progress;
#else				/* SERVER_MODE */
  LOG_LSA final_restored_lsa;
#endif				/* SERVER_MODE */

  /* Buffer for log hdr I/O, size : SIZEOF_LOG_PAGE_SIZE */
  LOG_PAGE *loghdr_pgptr;

  /*  2 thread cannot enter to flush */
  MUTEX_T flush_run_mutex;

  /* Flush information for dirty log pages */
  LOG_FLUSH_INFO flush_info;

  /* group commit information */
  LOG_GROUP_COMMIT_INFO group_commit_info;

};

/* logging statistics */
typedef struct log_logging_stat
{
  /* logpb_next_append_page() call count */
  long total_append_page_count;
  /* last created page id for logging */
  PAGEID last_append_pageid;
  /* time taken to use a page for logging */
  double use_append_page_sec;

  /* total delayed page count */
  long total_delayed_page_count;
  /* last delayed page id */
  PAGEID last_delayed_pageid;

  /* log buffer full count */
  long log_buffer_full_count;
  /* log buffer expand count */
  long log_buffer_expand_count;
  /* log buffer flush count by replacement */
  long log_buffer_flush_count_by_replacement;

  /* normal flush */
  /* logpb_flush_all_append_pages() call count */
  long flushall_append_pages_call_count;
  /* pages count to flush in logpb_flush_all_append_pages() */
  long last_flush_count_by_trans;
  /* total pages count to flush in logpb_flush_all_append_pages() */
  long total_flush_count_by_trans;
  /* time taken to flush in logpb_flush_all_append_pages() */
  double last_flush_sec_by_trans;
  /* total time taken to flush in logpb_flush_all_append_pages() */
  double total_flush_sec_by_trans;
  /* LOG_FLUSH_DIRECT count */
  long direct_flush_count;

  /* background flush */
  /* logpb_flush_pages_background() call count */
  long flush_pages_call_count;
  /* logpb_flush_pages_background() call count when no page is flushed */
  long flush_pages_call_miss_count;
  /* pages count to flush in logpb_flush_pages_background() */
  long last_flush_count_by_LFT;
  /* total pages count to flush in logpb_flush_pages_background() */
  long total_flush_count_by_LFT;
  /* time taken to flush in logpb_flush_pages_background() */
  double last_flush_sec_by_LFT;
  /* total time taken to flush in logpb_flush_pages_background() */
  double total_flush_sec_by_LFT;

  /* logpb_flush_header() call count */
  long flush_hdr_call_count;
  /* page count to flush in logpb_flush_header() */
  double last_flush_hdr_sec_by_LFT;
  /* total page count to flush in logpb_flush_header() */
  double total_flush_hdr_sec_by_LFT;

  /* total sync count */
  long total_sync_count;

  /* commit count */
  long commit_count;
  /* group commit count */
  long last_group_commit_count;
  /* total group commit count */
  long total_group_commit_count;

  /* commit count while using a log page */
  long last_commit_count_while_using_a_page;
  /* total commit count while using a log page */
  long total_commit_count_while_using_a_page;

  /* commit count included logpb_flush_all_append_pages */
  long last_commit_count_in_flush_pages;
  /* total commit count included logpb_flush_all_append_pages */
  long total_commit_count_in_flush_pages;

  /* group commit request count */
  long gc_commit_request_count;

  /* wait time for group commit */
  double gc_total_wait_time;

  /* flush count in group commit mode by LFT */
  long gc_flush_count;

  /* async commit request count */
  long async_commit_request_count;
} log_logging_stat;


#if !defined(SERVER_MODE)
extern int log_Tran_index;	/* Index onto transaction table for
				   current thread of execution (client)
				 */
#endif /* !SERVER_MODE */

extern LOG_GLOBAL log_Gl;

extern log_logging_stat log_Stat;

/* Name of the database and logs */
extern char log_Path[];
extern char log_Archive_path[];
extern char log_Prefix[];

extern const char *log_Db_fullname;
extern char log_Name_active[];
extern char log_Name_info[];
extern char log_Name_bkupinfo[];
extern char log_Name_volinfo[];

extern char log_Client_progname_unknown[];
extern char log_Client_name_unknown[];
extern char log_Client_host_unknown[];
extern int log_Client_process_id_unknown;

/*
 *
 *         internal/public definitions for files in log/recovery module
 *
 *            (don't add interface definitions in this header file)
 *            -----------------------------------------------------
 *
 *      this file should not be included by files which are not part of
 *      log/recovery module.
 *
 */

/*
 * internal declarations from log_pb.c file
 */
extern int logpb_initialize_pool (THREAD_ENTRY * thread_p);
extern void logpb_finalize_pool (void);
extern bool logpb_is_initialize_pool (void);
extern void logpb_invalidate_pool (THREAD_ENTRY * thread_p);
extern LOG_PAGE *logpb_create (THREAD_ENTRY * thread_p, PAGEID pageid);
extern LOG_PAGE *log_pbfetch (PAGEID pageid);
extern void logpb_set_dirty (LOG_PAGE * log_pgptr, int free_page);
extern int logpb_flush_page (THREAD_ENTRY * thread_p, LOG_PAGE * log_pgptr,
			     int free_page);
extern void logpb_free_page (LOG_PAGE * log_pgptr);
extern void logpb_free_without_mutex (LOG_PAGE * log_pgptr);
extern PAGEID logpb_get_page_id (LOG_PAGE * log_pgptr);
extern int logpb_print_hash_entry (FILE * outfp, const void *key,
				   void *ent, void *ignore);
extern int logpb_initialize_header (THREAD_ENTRY * thread_p,
				    struct log_header *loghdr,
				    const char *prefix_logname,
				    DKNPAGES npages, time_t * db_creation);
extern LOG_PAGE *logpb_create_header_page (THREAD_ENTRY * thread_p);
extern void logpb_fetch_header (THREAD_ENTRY * thread_p,
				struct log_header *hdr);
extern void logpb_fetch_header_with_buffer (THREAD_ENTRY * thread_p,
					    struct log_header *hdr,
					    LOG_PAGE * log_pgptr);
extern void logpb_flush_header (THREAD_ENTRY * thread_p);
extern LOG_PAGE *logpb_fetch_page (THREAD_ENTRY * thread_p, PAGEID pageid,
				   LOG_PAGE * log_pgptr);
extern LOG_PAGE *logpb_read_page_from_file (THREAD_ENTRY * thread_p,
					    PAGEID pageid,
					    LOG_PAGE * log_pgptr);
extern LOG_PAGE *logpb_write_page_to_disk (THREAD_ENTRY * thread_p,
					   LOG_PAGE * log_pgptr,
					   PAGEID logical_pageid);
extern PGLENGTH logpb_find_header_parameters (THREAD_ENTRY * thread_p,
					      const char *db_fullname,
					      const char *logpath,
					      const char *prefix_logname,
					      PGLENGTH * db_iopagesize,
					      time_t * db_creation,
					      float *db_compatibility);
extern LOG_PAGE *logpb_fetch_start_append_page (THREAD_ENTRY * thread_p);
extern void logpb_next_append_page (THREAD_ENTRY * thread_p,
				    LOG_SETDIRTY current_setdirty);
extern int logpb_flush_all_append_pages_low (THREAD_ENTRY * thread_p);
extern void logpb_flush_all_append_pages (THREAD_ENTRY * thread_p,
					  LOG_FLUSH_TYPE flush_type);
extern void logpb_invalid_all_append_pages (THREAD_ENTRY * thread_p);
extern void logpb_flush_log_for_wal (THREAD_ENTRY * thread_p,
				     const LOG_LSA * lsa_ptr);
extern void logpb_force (THREAD_ENTRY * thread_p);
extern void logpb_start_append (THREAD_ENTRY * thread_p, LOG_RECTYPE rectype,
				LOG_TDES * tdes);
extern void logpb_append_data (THREAD_ENTRY * thread_p, int length,
			       const char *data);
extern void logpb_append_crumbs (THREAD_ENTRY * thread_p, int num_crumbs,
				 const LOG_CRUMB * crumbs);
extern void logpb_end_append (THREAD_ENTRY * thread_p, LOG_FLUSH flush,
			      bool force_flush);
extern void logpb_remove_append (LOG_TDES * tdes);
extern void logpb_create_log_info (const char *logname_info,
				   const char *db_fullname);
extern int logpb_dump_log_info (const char *logname_info,
				int also_stdout, const char *fmt, ...);
extern bool logpb_find_volume_info_exist (void);
extern int logpb_create_volume_info (const char *db_fullname);
extern int logpb_recreate_volume_info (THREAD_ENTRY * thread_p);
extern VOLID logpb_add_volume (const char *db_fullname,
			       VOLID new_volid,
			       const char *new_volfullname,
			       DISK_VOLPURPOSE new_volpurpose);
extern int logpb_scan_volume_info (THREAD_ENTRY * thread_p,
				   const char *db_fullname,
				   VOLID ignore_volid, VOLID start_volid,
				   int (*fun) (THREAD_ENTRY * thread_p,
					       VOLID xvolid,
					       const char *vlabel,
					       void *args), void *args);
extern PAGEID logpb_to_physical_pageid (PAGEID logical_pageid);
extern bool logpb_is_page_in_archive (PAGEID pageid);
extern bool logpb_is_smallest_lsa_in_archive (THREAD_ENTRY * thread_p);
extern int logpb_get_archive_number (THREAD_ENTRY * thread_p, PAGEID pageid);
extern void logpb_decache_archive_info (void);
extern void logpb_remove_archive_logs (THREAD_ENTRY * thread_p,
				       const char *info_reason);
extern void logpb_copy_from_log (THREAD_ENTRY * thread_p, char *area,
				 int length, LOG_LSA * log_lsa,
				 LOG_PAGE * log_pgptr);
extern int logpb_initialize_log_names (THREAD_ENTRY * thread_p,
				       const char *db_fullname,
				       const char *logpath,
				       const char *prefix_logname);
extern bool logpb_exist_log (THREAD_ENTRY * thread_p, const char *db_fullname,
			     const char *logpath, const char *prefix_logname);
#if defined(SERVER_MODE)
extern void logpb_do_checkpoint (void);
#endif /* SERVER_MODE */
extern PAGEID logpb_checkpoint (THREAD_ENTRY * thread_p);
extern void logpb_dump_checkpoint_trans (int length, void *data);
extern void logpb_dump_checkpoint_topops (int length, void *data);
extern int logpb_backup (THREAD_ENTRY * thread_p, int num_perm_vols,
			 const char *allbackup_path,
			 FILEIO_BACKUP_LEVEL backup_level,
			 int delete_unneeded_logarchives,
			 const char *backup_verbose_file,
			 int num_threads,
			 FILEIO_ZIP_METHOD zip_method,
			 FILEIO_ZIP_LEVEL zip_level,
			 int skip_activelog, PAGEID safe_pageid);
extern int
logpb_xrestore (THREAD_ENTRY * thread_p, const char *db_fullname,
		const char *logpath, const char *prefix_logname,
		const char *newall_path, bool ask_forpath,
		BO_RESTART_ARG * r_args);
extern int logpb_restore (THREAD_ENTRY * thread_p, const char *db_fullname,
			  const char *logpath, const char *prefix_logname,
			  BO_RESTART_ARG * r_args);
extern int logpb_copy_database (THREAD_ENTRY * thread_p, VOLID num_perm_vols,
				const char *to_db_fullname,
				const char *to_logpath,
				const char *to_prefix_logname,
				const char *toext_path,
				const char *fileof_vols_and_copypaths);
extern int logpb_rename_all_volumes_files (THREAD_ENTRY * thread_p,
					   VOLID num_perm_vols,
					   const char *to_db_fullname,
					   const char *to_logpath,
					   const char *to_prefix_logname,
					   const char *toext_path,
					   const char
					   *fileof_vols_and_renamepaths,
					   int extern_rename,
					   bool force_delete);
extern int logpb_delete (THREAD_ENTRY * thread_p, VOLID num_perm_vols,
			 const char *db_fullname, const char *logpath,
			 const char *prefix_logname, bool force_delete);
extern int logpb_check_exist_any_volumes (THREAD_ENTRY * thread_p,
					  const char *db_fullname,
					  const char *logpath,
					  const char *prefix_logname,
					  char *first_vol, bool * is_exist);
extern void logpb_fatal_error (THREAD_ENTRY * thread_p, bool logexit,
			       const char *file_name, const int lineno,
			       const char *fmt, ...);
extern int logpb_check_and_reset_temp_lsa (THREAD_ENTRY * thread_p,
					   VOLID volid);
extern void logpb_initialize_logging_statistics (void);
extern void logpb_flush_pages_background (THREAD_ENTRY * thread_p);
void xlogpb_dump_stat (FILE * outfp);

extern void logpb_dump (FILE * out_fp);


/*
 * internal declarations from log_rcv.c file
 */
extern void log_recovery (THREAD_ENTRY * thread_p, int ismedia_crash,
			  time_t * stopat);
extern LOG_LSA *log_startof_nxrec (THREAD_ENTRY * thread_p, LOG_LSA * lsa,
				   bool canuse_forwaddr);


/*
 * internal declarations from log_2pc.c file
 */
extern void
log_2pc_define_funs (int (*get_participants) (int *particp_id_length,
					      void **block_particps_ids),
		     int (*lookup_participant) (void *particp_id,
						int num_particps,
						void *block_particps_ids),
		     char *(*fmt_participant) (void *particp_id),
		     void (*dump_participants) (int block_length,
						void *block_particps_id),
		     int (*send_prepare) (int gtrid, int num_particps,
					  void *block_particps_ids),
		     int (*send_commit) (int gtrid, int num_particps,
					 int *particp_indices,
					 void *block_particps_ids),
		     int (*send_abort) (int gtrid, int num_particps,
					int *particp_indices,
					void *block_particps_ids,
					int collect));
extern char *log_2pc_sprintf_particp (void *particp_id);
extern void log_2pc_dump_participants (int block_length,
				       void *block_particps_ids);
extern bool log_2pc_send_prepare (int gtrid, int num_particps,
				  void *block_particps_ids);
extern bool log_2pc_send_commit_decision (int gtrid,
					  int num_particps,
					  int *particps_indices,
					  void *block_particps_ids);
extern bool log_2pc_send_abort_decision (int gtrid,
					 int num_particps,
					 int *particps_indices,
					 void *block_particps_ids,
					 bool collect);
extern int log_get_global_tran_id (THREAD_ENTRY * thread_p);
extern TRAN_STATE log_2pc_commit (THREAD_ENTRY * thread_p, LOG_TDES * tdes,
				  LOG_2PC_EXECUTE execute_2pc_type,
				  bool * decision);
extern int log_set_global_tran_info (THREAD_ENTRY * thread_p, int gtrid,
				     void *info, int size);
extern int log_get_global_tran_info (THREAD_ENTRY * thread_p, int gtrid,
				     void *buffer, int size);
extern int log_2pc_start (THREAD_ENTRY * thread_p);
extern TRAN_STATE log_2pc_prepare (THREAD_ENTRY * thread_p);
extern int log_2pc_recovery_prepared (THREAD_ENTRY * thread_p, int gtrids[],
				      int size);
extern int log_2pc_attach_global_tran (THREAD_ENTRY * thread_p, int gtrid);
extern int log_2pc_append_recv_ack (THREAD_ENTRY * thread_p,
				    int particp_index);
extern TRAN_STATE log_2pc_prepare_global_tran (THREAD_ENTRY * thread_p,
					       int gtrid);
extern void log_2pc_read_prepare (THREAD_ENTRY * thread_p, int acquire_locks,
				  LOG_TDES * tdes, LOG_LSA * lsa,
				  LOG_PAGE * log_pgptr);
extern void log_2pc_dump_gtrinfo (int length, void *data);
extern void log_2pc_dump_acqobj_locks (int length, void *data);
extern void log_2pc_dump_acqpage_locks (int length, void *data);
extern LOG_TDES *log_2pc_alloc_coord_info (LOG_TDES * tdes,
					   int num_particps,
					   int particp_id_length,
					   void *block_particps_ids);
extern void log_2pc_free_coord_info (LOG_TDES * tdes);
extern void log_2pc_crash_participant (THREAD_ENTRY * thread_p);
extern void log_2pc_send_decision_participant (THREAD_ENTRY * thread_p,
					       void *particp_id);
extern void log_2pc_recovery_analysis_info (THREAD_ENTRY * thread_p,
					    LOG_TDES * tdes,
					    LOG_LSA * upto_chain_lsa);
extern void log_2pc_recovery (THREAD_ENTRY * thread_p);
extern bool log_is_tran_in_2pc (THREAD_ENTRY * thread_p);
extern bool log_is_tran_distributed (LOG_TDES * tdes);
extern bool log_clear_and_is_tran_distributed (LOG_TDES * tdes);


/*
 *       	   internal declarations from log_tb.c file
 */
extern void *logtb_realloc_topops_stack (LOG_TDES * tdes, int num_elms);
extern void logtb_define_trantable (THREAD_ENTRY * thread_p,
				    int num_expected_tran_indices,
				    int num_expected_locks);
extern int logtb_define_trantable_log_latch (THREAD_ENTRY * thread_p,
					     int num_expected_tran_indices);
extern void logtb_undefine_trantable (THREAD_ENTRY * thread_p);
extern int logtb_get_number_assigned_tran_indices (void);
extern int logtb_get_number_of_total_tran_indices (void);
extern bool logtb_am_i_sole_tran (THREAD_ENTRY * thread_p);
extern void logtb_i_am_not_sole_tran (void);
extern int
logtb_assign_tran_index (THREAD_ENTRY * thread_p, TRANID trid,
			 TRAN_STATE state, const char *client_prog_name,
			 const char *client_user_name,
			 const char *client_host_name, int client_process_id,
			 TRAN_STATE * current_state, int waitsecs,
			 TRAN_ISOLATION isolation);
extern LOG_TDES *logtb_rv_find_allocate_tran_index (THREAD_ENTRY * thread_p,
						    TRANID trid,
						    const LOG_LSA * log_lsa);
extern void logtb_release_tran_index (THREAD_ENTRY * thread_p,
				      int tran_index);
extern void logtb_free_tran_index (THREAD_ENTRY * thread_p, int tran_index);
extern void logtb_free_tran_index_with_undo_lsa (THREAD_ENTRY * thread_p,
						 const LOG_LSA * undo_lsa);
extern void logtb_clear_tdes (THREAD_ENTRY * thread_p, LOG_TDES * tdes);
extern int logtb_get_new_tran_id (THREAD_ENTRY * thread_p, LOG_TDES * tdes);
extern int logtb_find_tran_index (THREAD_ENTRY * thread_p, TRANID trid);
extern int
logtb_find_tran_index_host_pid (THREAD_ENTRY * thread_p,
				const char *host_name, int process_id);
extern TRANID logtb_find_tranid (int tran_index);
extern TRANID logtb_find_current_tranid (THREAD_ENTRY * thread_p);
extern void
logtb_set_client_ids_all (LOG_CLIENTIDS * client, const char *prog_name,
			  const char *host_name, const char *user_name,
			  int process_id);
extern char *logtb_find_client_name (int tran_index);
extern int
logtb_find_client_name_host_pid (int tran_index, char **client_prog_name,
				 char **client_user_name,
				 char **client_host_name, int *client_pid);
#if defined(SERVER_MODE)
extern int xlogtb_get_pack_tran_table (THREAD_ENTRY * thread_p,
				       char **buffer_p, int *size_p);
#endif /* SERVER_MODE */
extern char *logtb_find_current_client_name (THREAD_ENTRY * thread_p);
extern LOG_LSA *logtb_find_current_tran_lsa (THREAD_ENTRY * thread_p);
extern TRAN_STATE logtb_find_state (int tran_index);
#if defined(SERVER_MODE)
extern int xlogtb_reset_wait_secs (THREAD_ENTRY * thread_p, int waitsecs);
#endif /* SERVER_MODE */
extern int logtb_find_wait_secs (int tran_index);
extern int logtb_find_current_wait_secs (THREAD_ENTRY * thread_p);
#if defined(SERVER_MODE)
extern int xlogtb_reset_isolation (THREAD_ENTRY * thread_p,
				   TRAN_ISOLATION isolation,
				   bool unlock_by_isolation);
#endif /* SERVER_MODE */
extern TRAN_ISOLATION logtb_find_isolation (int tran_index);
extern TRAN_ISOLATION logtb_find_current_isolation (THREAD_ENTRY * thread_p);
#if defined(SERVER_MODE)
extern void xlogtb_set_interrupt (THREAD_ENTRY * thread_p, int set);
#endif /* SERVER_MODE */
extern bool logtb_set_tran_index_interrupt (THREAD_ENTRY * thread_p,
					    int tran_index, int set);
extern bool logtb_is_interrupt (THREAD_ENTRY * thread_p, bool clear,
				bool * continue_checking);
extern bool log_isinterrupt_tdes (LOG_TDES * tdes, bool clear);
extern bool
logtb_is_interrupt_tran (THREAD_ENTRY * thread_p, bool clear,
			 bool * continue_checking, int tran_index);
extern bool logtb_are_any_interrupts (void);
extern bool logtb_is_active (THREAD_ENTRY * thread_p, TRANID trid);
extern bool logtb_is_current_active (THREAD_ENTRY * thread_p);
extern bool logtb_istran_finished (THREAD_ENTRY * thread_p, TRANID trid);
#if defined(SERVER_MODE)
extern bool logtb_has_updated (THREAD_ENTRY * thread_p);
#endif /* SERVER_MODE */
extern void logtb_set_to_system_tran_index (THREAD_ENTRY * thread_p);
extern int logtb_set_current_tran_index (THREAD_ENTRY * thread_p,
					 int tran_index);
extern int logtb_set_num_loose_end_trans (THREAD_ENTRY * thread_p);
extern LOG_LSA *log_find_unilaterally_largest_undo_lsa (THREAD_ENTRY *
							thread_p);
extern void logtb_find_smallest_lsa (THREAD_ENTRY * thread_p, LOG_LSA * lsa);
extern LOG_LSA *logtb_find_largest_lsa (THREAD_ENTRY * thread_p);
extern void
logtb_find_smallest_and_largest_active_pages (THREAD_ENTRY * thread_p,
					      PAGEID * smallest,
					      PAGEID * largest);
/* For Debugging */
extern void logtb_dump_trantable (THREAD_ENTRY * thread_p, FILE * out_fp);


/*
 * internal declarations from log_def
 */
extern int log_any_debug_hooks (void);
extern int log_docall_during_hook (int *callat);


/*
 * internal declarations from log
 */
extern LOG_LSA *log_start_sysop_logical_undo (LOG_LSA * undo_nxlsa);

#endif
