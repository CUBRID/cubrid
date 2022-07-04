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
 * log_impl.h -
 *
 */

#ifndef _LOG_IMPL_H_
#define _LOG_IMPL_H_

#ident "$Id$"

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Wrong module
#endif // not SERVER/SA modes

#include "boot.h"
#include "btree_unique.hpp"
#include "client_credentials.hpp"
#include "config.h"
#include "connection_globals.h"
#include "critical_section.h"
#include "es.h"
#include "file_io.h"
#include "lock_free.h"
#include "lock_manager.h"
#include "log_2pc.h"
#include "log_append.hpp"
#include "log_archives.hpp"
#include "log_comm.h"
#include "log_common_impl.h"
#include "log_lsa.hpp"
#include "log_meta.hpp"
#include "log_postpone_cache.hpp"
#include "log_prior_send.hpp"
#if defined (SERVER_MODE)
#include "log_prior_recv.hpp"
#endif // SERVER_MODE
#include "log_storage.hpp"
#include "mvcc.h"
#include "mvcc_table.hpp"
#include "porting.h"
#include "recovery.h"
#include "release_string.h"
#include "storage_common.h"
#include "tde.h"
#include "thread_entry.hpp"
#include "transaction_transient.hpp"
#include "lockfree_circular_queue.hpp"

#include <unordered_set>
#include <queue>
#include <assert.h>
#include <condition_variable>
#include <mutex>
#if defined(SOLARIS)
#include <netdb.h>		/* for MAXHOSTNAMELEN */
#endif /* SOLARIS */
#include <signal.h>

// forward declarations
struct bo_restart_arg;
struct logwr_info;

#if defined(SERVER_MODE)
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
#define TR_TABLE_CS_ENTER(thread_p)
#define TR_TABLE_CS_ENTER_READ_MODE(thread_p)
#define TR_TABLE_CS_EXIT(thread_p)

#define LOG_ARCHIVE_CS_ENTER(thread_p)
#define LOG_ARCHIVE_CS_ENTER_READ_MODE(thread_p)
#define LOG_ARCHIVE_CS_EXIT(thread_p)
#endif /* SERVER_MODE */

#if defined(SERVER_MODE)

#define LOG_ARCHIVE_CS_OWN(thread_p) \
  (csect_check (thread_p, CSECT_LOG_ARCHIVE) >= 1)
#define LOG_ARCHIVE_CS_OWN_WRITE_MODE(thread_p) \
  (csect_check_own (thread_p, CSECT_LOG_ARCHIVE) == 1)
#define LOG_ARCHIVE_CS_OWN_READ_MODE(thread_p) \
  (csect_check_own (thread_p, CSECT_LOG_ARCHIVE) == 2)

#else /* SERVER_MODE */
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

#if defined(SERVER_MODE)
// todo - separate the client & server/sa_mode transaction index
#if !defined(LOG_FIND_THREAD_TRAN_INDEX)
#define LOG_FIND_THREAD_TRAN_INDEX(thrd) \
  ((thrd) ? (thrd)->tran_index : logtb_get_current_tran_index ())
#endif
#define LOG_SET_CURRENT_TRAN_INDEX(thrd, index) \
  ((thrd) ? (void) ((thrd)->tran_index = (index)) : logtb_set_current_tran_index ((thrd), (index)))
#else /* SERVER_MODE */
#if !defined(LOG_FIND_THREAD_TRAN_INDEX)
#define LOG_FIND_THREAD_TRAN_INDEX(thrd) (log_Tran_index)
#endif
#define LOG_SET_CURRENT_TRAN_INDEX(thrd, index) \
  log_Tran_index = (index)
#endif /* SERVER_MODE */

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

const TRANID LOG_SYSTEM_WORKER_FIRST_TRANID = NULL_TRANID - 1;
const int LOG_SYSTEM_WORKER_INCR_TRANID = -1;

#define LOG_READ_NEXT_TRANID (log_Gl.hdr.next_trid)
#define LOG_READ_NEXT_MVCCID (log_Gl.hdr.mvcc_next_id)
#define LOG_HAS_LOGGING_BEEN_IGNORED() \
  (log_Gl.hdr.has_logging_been_skipped == true)

#define LOG_ISRESTARTED() (log_Gl.rcv_phase == LOG_RESTARTED)

/* special action for log applier */
#if defined (SERVER_MODE)
#define LOG_CHECK_LOG_APPLIER(thread_p) \
  (thread_p != NULL \
   && logtb_find_client_type (thread_p->tran_index) == DB_CLIENT_TYPE_LOG_APPLIER)
#else
#define LOG_CHECK_LOG_APPLIER(thread_p) (0)
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

#define MAX_NUM_EXEC_QUERY_HISTORY                      100

/*CDC defines*/

#define CDC_GET_TEMP_LOGPAGE(thread_p, process_lsa, log_page_p) \
  do \
    { \
      if (cdc_Gl.producer.temp_logbuf[(process_lsa)->pageid % 2].log_page_p->hdr.logical_pageid \
          != (process_lsa)->pageid) \
      { \
        if (logpb_fetch_page ((thread_p), (process_lsa), LOG_CS_FORCE_USE, (log_page_p)) \
            != NO_ERROR) \
        { \
          goto error; \
        } \
         memcpy (cdc_Gl.producer.temp_logbuf[(process_lsa)->pageid % 2].log_page_p, (log_page_p), IO_MAX_PAGE_SIZE); \
      } \
      else \
      { \
        (log_page_p) = cdc_Gl.producer.temp_logbuf[(process_lsa)->pageid % 2].log_page_p ;\
      } \
    } \
  while (0)

#define CDC_CHECK_TEMP_LOGPAGE(process_lsa, tmpbuf_index, log_page_p) \
  do \
    { \
      if (((process_lsa)->pageid % 2) != *(tmpbuf_index)) \
      { \
	  *(tmpbuf_index) = (*(tmpbuf_index) + 1) % 2; \
	  memcpy (cdc_Gl.producer.temp_logbuf[*(tmpbuf_index)].log_page_p, (log_page_p), IO_MAX_PAGE_SIZE); \
      } \
    } \
  while (0)

#define CDC_UPDATE_TEMP_LOGPAGE(thread_p, process_lsa, log_page_p) \
  do \
    { \
      if (cdc_Gl.producer.temp_logbuf[(process_lsa)->pageid % 2].log_page_p->hdr.logical_pageid \
          == (process_lsa)->pageid) \
      { \
        if (logpb_fetch_page ((thread_p), (process_lsa), LOG_CS_FORCE_USE, (log_page_p)) \
            != NO_ERROR) \
        { \
          goto error; \
        } \
         memcpy (cdc_Gl.producer.temp_logbuf[(process_lsa)->pageid % 2].log_page_p, (log_page_p), IO_MAX_PAGE_SIZE); \
      } \
    } \
  while (0)

#define CDC_MAKE_SUPPLEMENT_DATA(supplement_data, recdes) \
  do \
    { \
      memcpy ((supplement_data), &(recdes).type, sizeof ((recdes).type)); \
      memcpy ((supplement_data) + sizeof((recdes).type), (recdes).data, (recdes).length); \
    } \
  while (0)

#define cdc_log(...) if (cdc_Logging) _er_log_debug (ARG_FILE_LINE, "CDC: " __VA_ARGS__)

#define MAX_CDC_LOGINFO_QUEUE_ENTRY  2048
#define MAX_CDC_LOGINFO_QUEUE_SIZE   32 * 1024 * 1024	/*32 MB */
#define MAX_CDC_TRAN_USER_TABLE       4000

enum log_flush
{ LOG_DONT_NEED_FLUSH, LOG_NEED_FLUSH };
typedef enum log_flush LOG_FLUSH;

enum log_setdirty
{ LOG_DONT_SET_DIRTY, LOG_SET_DIRTY };
typedef enum log_setdirty LOG_SETDIRTY;

enum log_getnewtrid
{ LOG_DONT_NEED_NEWTRID, LOG_NEED_NEWTRID };
typedef enum log_getnewtrid LOG_GETNEWTRID;

enum log_wrote_eot_log
{ LOG_NEED_TO_WRITE_EOT_LOG, LOG_ALREADY_WROTE_EOT_LOG };
typedef enum log_wrote_eot_log LOG_WRITE_EOT_LOG;

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



typedef struct log_topops_addresses LOG_TOPOPS_ADDRESSES;
struct log_topops_addresses
{
  LOG_LSA lastparent_lsa;	/* The last address of the parent transaction. This is needed for undo of the top
				 * system action */
  LOG_LSA posp_lsa;		/* The first address of a postpone log record for top system operation. We add this
				 * since it is reset during recovery to the last reference postpone address. */
};

typedef struct log_topops_stack LOG_TOPOPS_STACK;
struct log_topops_stack
{
  int max;			/* Size of stack */
  int last;			/* Last entry in stack */
  LOG_TOPOPS_ADDRESSES *stack;	/* Stack for push and pop of top system actions */
};

enum tran_abort_reason
{
  TRAN_NORMAL = 0,
  TRAN_ABORT_DUE_DEADLOCK = 1,
  TRAN_ABORT_DUE_ROLLBACK_ON_ESCALATION = 2
};
typedef enum tran_abort_reason TRAN_ABORT_REASON;

typedef struct log_unique_stats LOG_UNIQUE_STATS;
struct log_unique_stats
{
  long long num_nulls;		/* number of nulls */
  long long num_keys;		/* number of keys */
  long long num_oids;		/* number of oids */
};

typedef struct log_tran_btid_unique_stats LOG_TRAN_BTID_UNIQUE_STATS;
struct log_tran_btid_unique_stats
{
  BTID btid;			/* id of B-tree */
  bool deleted;			/* true if the B-tree was deleted */

  LOG_UNIQUE_STATS tran_stats;	/* statistics accumulated during entire transaction */
  LOG_UNIQUE_STATS global_stats;	/* statistics loaded from index */
};

enum count_optim_state
{
  COS_NOT_LOADED = 0,		/* the global statistics was not loaded yet */
  COS_TO_LOAD = 1,		/* the global statistics must be loaded when snapshot is taken */
  COS_LOADED = 2		/* the global statistics were loaded */
};
typedef enum count_optim_state COUNT_OPTIM_STATE;

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

typedef struct log_rcv_tdes LOG_RCV_TDES;
struct log_rcv_tdes
{
  /* structure stored in transaction descriptor and used for recovery purpose.
   * currently we store the LSA of a system op start postpone. it is required to finish the system op postpone phase
   * correctly */
  LOG_LSA sysop_start_postpone_lsa;
  /* we need to know where transaction postpone has started. */
  LOG_LSA tran_start_postpone_lsa;
  /* we need to know if file_perm_alloc or file_perm_dealloc operations have been interrupted. these operation must be
   * executed atomically (all changes applied or all rollbacked) before executing finish all postpones. to know what
   * to abort, we remember the starting LSA of such operation. */
  LOG_LSA atomic_sysop_start_lsa;
  LOG_LSA analysis_last_aborted_sysop_lsa;	/* to recover logical redo operation. */
  LOG_LSA analysis_last_aborted_sysop_start_lsa;	/* to recover logical redo operation. */
};

typedef struct log_tdes LOG_TDES;
struct log_tdes
{
/* Transaction descriptor */
  MVCC_INFO mvccinfo;		/* MVCC info */

  int tran_index;		/* Index onto transaction table */
  TRANID trid;			/* Transaction identifier */

  bool isloose_end;
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
  LOG_LSA commit_abort_lsa;	/* Address of the commit/abort operation. Used by checkpoint to decide whether to
				 * consider or not a transaction as concluded. */
  LOG_LSA last_mvcc_lsa;	/* The address of transaction's last MVCC log record. */
  LOG_LSA page_desync_lsa;	/* Only on PTS: the LSA of a page found to be ahead of replication, that could cause a page
				 * desynchronization issue. */
  int client_id;		/* unique client id */
  int gtrid;			/* Global transaction identifier; used only if this transaction is a participant to a
				 * global transaction and it is prepared to commit. */
  CLIENTIDS client;		/* Client identification */
  SYNC_RMUTEX rmutex_topop;	/* reentrant mutex to serialize system top operations */
  LOG_TOPOPS_STACK topops;	/* Active top system operations. Used for system permanent nested operations which are
				 * independent from current transaction outcome. */
  LOG_2PC_GTRINFO gtrinfo;	/* Global transaction user information; used to store XID of XA interface. */
  LOG_2PC_COORDINATOR *coord;	/* Information about the participants of the distributed transactions. Used only if
				 * this site is the coordinator. Will be NULL if the transaction is a local one, or if
				 * this site is a participant. */
  int num_unique_btrees;	/* # of unique btrees contained in unique_stat_info array */
  int max_unique_btrees;	/* size of unique_stat_info array */
  multi_index_unique_stats m_multiupd_stats;
#if defined(_AIX)
  sig_atomic_t interrupt;
#else				/* _AIX */
  volatile sig_atomic_t interrupt;
#endif				/* _AIX */
  /* Set to one when the current execution must be stopped somehow. We stop it by sending an error message during
   * fetching of a page. */
  tx_transient_class_registry m_modified_classes;	// list of classes made dirty

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
  bool is_user_active;

  LOG_RCV_TDES rcv;

  log_postpone_cache m_log_postpone_cache;

  bool has_supplemental_log;	/* Checks if supplemental log has been appended within the transaction */

  // *INDENT-OFF*
#if defined (SERVER_MODE) || (defined (SA_MODE) && defined (__cplusplus))

  bool is_active_worker_transaction () const;
  bool is_system_transaction () const;
  bool is_system_main_transaction () const;
  bool is_system_worker_transaction () const;
  bool is_allowed_undo () const;
  bool is_allowed_sysop () const;
  bool is_under_sysop () const;

  void lock_topop ();
  void unlock_topop ();

  void on_sysop_start ();
  void on_sysop_end ();

  // lock global oldest visible mvccid to current value; required for heavy operations that need to do their own
  // vacuuming, like upgrade domain / reorganize partitions
  void lock_global_oldest_visible_mvccid ();
  void unlock_global_oldest_visible_mvccid ();
#endif
  // *INDENT-ON*
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

/* state of recovery process */
enum log_recvphase
{
  LOG_RESTARTED,		/* Normal processing. Recovery has been executed. */
  LOG_RECOVERY_ANALYSIS_PHASE,	/* Start recovering. Find the transactions that were active at the time of the crash */
  LOG_RECOVERY_REDO_PHASE,	/* Redoing phase */
  LOG_RECOVERY_UNDO_PHASE,	/* Undoing phase */
  LOG_RECOVERY_FINISH_2PC_PHASE	/* Finishing up transactions that were in 2PC protocol at the time of the crash */
};
typedef enum log_recvphase LOG_RECVPHASE;

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
  LOG_LSA chkpt_redo_lsa;	/* checkpoint redo lsa working variable; it's the member checkpoint lsa
				   in the header that must be used as reference */
  DKNPAGES chkpt_every_npages;	/* How frequent a checkpoint should be taken ? */
  LOG_RECVPHASE rcv_phase;	/* Phase of the recovery */
  LOG_LSA rcv_phase_lsa;	/* LSA of phase (e.g. Restart) */

#if defined(SERVER_MODE)
  bool backup_in_progress;
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
  logwr_info *writer_info;
  /* background log archiving info */
  BACKGROUND_ARCHIVING_INFO bg_archive_info;

  mvcctable mvcc_table;		/* MVCC table */
  GLOBAL_UNIQUE_STATS_TABLE unique_stats_table;	/* global unique statistics */

  // *INDENT-OFF*
  cublog::meta m_metainfo;
#if defined (SERVER_MODE)
  std::unique_ptr<cublog::prior_sender> m_prior_sender = nullptr;
  std::unique_ptr<cublog::prior_recver> m_prior_recver = nullptr;
#endif // SERVER_MODE = !SA_MODE

  std::mutex m_ps_lsa_mutex;
  std::condition_variable m_ps_lsa_cv;
  LOG_LSA m_max_ps_flushed_lsa = NULL_LSA;

  log_global ();
  ~log_global ();

#if defined (SERVER_MODE)
  void initialize_log_prior_receiver ();
  void finalize_log_prior_receiver ();
  cublog::prior_recver &get_log_prior_receiver ();
#endif // SERVER_MODE

  void update_max_ps_flushed_lsa (const LOG_LSA & lsa);
  void wait_flushed_lsa (const log_lsa & flush_lsa);

  // *INDENT-ON*
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

/* For CDC interface */

typedef enum cdc_producer_state
{
  CDC_PRODUCER_STATE_WAIT,
  CDC_PRODUCER_STATE_RUN,
  CDC_PRODUCER_STATE_DEAD
} CDC_PRODUCER_STATE;

typedef enum cdc_consumer_request
{
  CDC_REQUEST_CONSUMER_TO_WAIT,
  CDC_REQUEST_CONSUMER_TO_RUN,
  CDC_REQUEST_CONSUMER_NONE
} CDC_CONSUMER_REQUEST;

typedef enum cdc_producer_request
{
  CDC_REQUEST_PRODUCER_TO_WAIT,
  CDC_REQUEST_PRODUCER_TO_BE_DEAD,
  CDC_REQUEST_PRODUCER_NONE
} CDC_PRODUCER_REQUEST;

typedef struct cdc_loginfo_entry
{
  LOG_LSA next_lsa;
  int length;
  char *log_info;
} CDC_LOGINFO_ENTRY;

typedef struct cdc_temp_logbuf
{
  LOG_PAGE *log_page_p;
  char log_page[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT];
} CDC_TEMP_LOGBUF;

typedef struct cdc_producer
{
  LOG_LSA next_extraction_lsa;

  /* configuration */
  int all_in_cond;

  int num_extraction_user;
  char **extraction_user;

  int num_extraction_class;
  UINT64 *extraction_classoids;

  volatile CDC_PRODUCER_STATE state;
  volatile CDC_PRODUCER_REQUEST request;

  int produced_queue_size;

  pthread_mutex_t lock;
  pthread_cond_t wait_cond;

  CDC_TEMP_LOGBUF temp_logbuf[2];

/* *INDENT-OFF* */
  std::unordered_map <TRANID, char *> tran_user; /*to clear when log producer ends suddenly */
  std::unordered_map<TRANID, int > tran_ignore;
  /* *INDENT-ON* */
} CDC_PRODUCER;

typedef struct cdc_consumer
{
  int extraction_timeout;
  int max_log_item;

  char *log_info;		/* log info list. it is used as buffer to send to client */
  int log_info_size;		/* total length of data in log_info */
  int log_info_buf_size;	/* size of buffer for log_info */
  int num_log_info;		/* how many log info is stored in log_infos (log info list) */

  int consumed_queue_size;

  volatile CDC_CONSUMER_REQUEST request;

  LOG_LSA start_lsa;		/* first LSA of log info that should be sent */
  LOG_LSA next_lsa;		/* next LSA to be sent to client */

} CDC_CONSUMER;

typedef struct cdc_global
{
  css_conn_entry conn;

  CDC_PRODUCER producer;
  CDC_CONSUMER consumer;

  /* *INDENT-OFF* */
  lockfree::circular_queue<CDC_LOGINFO_ENTRY *> *loginfo_queue;
  /* *INDENT-ON* */

  LOG_LSA first_loginfo_queue_lsa;
  LOG_LSA last_loginfo_queue_lsa;

  bool is_queue_reinitialized;

} CDC_GLOBAL;

/* will be moved to new file for CDC */
typedef struct ovf_page_list
{
  char *rec_type;
  char *data;
  int length;
  struct ovf_page_list *next;
} OVF_PAGE_LIST;

typedef enum cdc_dataitem_type
{
  CDC_DDL = 0,
  CDC_DML,
  CDC_DCL,
  CDC_TIMER
} CDC_DATAITEM_TYPE;

typedef enum cdc_dcl_type
{
  CDC_COMMIT = 0,
  CDC_ABORT
} CDC_DCL_TYPE;

typedef enum cdc_dml_type
{
  CDC_INSERT = 0,
  CDC_UPDATE,
  CDC_DELETE,
  CDC_TRIGGER_INSERT,
  CDC_TRIGGER_UPDATE,
  CDC_TRIGGER_DELETE
} CDC_DML_TYPE;

/*Data structure for CDC interface end */

// todo - move to manager
enum log_cs_access_mode
{ LOG_CS_FORCE_USE, LOG_CS_SAFE_READER };
typedef enum log_cs_access_mode LOG_CS_ACCESS_MODE;

#if !defined(SERVER_MODE)
#if !defined(LOG_TRAN_INDEX)
#define LOG_TRAN_INDEX
extern int log_Tran_index;	/* Index onto transaction table for current thread of execution (client) */
#endif /* !LOG_TRAN_INDEX */
#endif /* !SERVER_MODE */

extern LOG_GLOBAL log_Gl;

extern LOG_LOGGING_STAT log_Stat;

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
extern char log_Name_metainfo[];

/*CDC global variables */
extern CDC_GLOBAL cdc_Gl;
extern bool cdc_Logging;

/* logging */
#if defined (SA_MODE)
#define LOG_THREAD_TRAN_MSG "%s"
#define LOG_THREAD_TRAN_ARGS(thread_p) "(SA_MODE)"
#else	/* !SA_MODE */	       /* SERVER_MODE */
#define LOG_THREAD_TRAN_MSG "(thr=%d, trid=%d)"
#define LOG_THREAD_TRAN_ARGS(thread_p) thread_get_current_entry_index (), LOG_FIND_CURRENT_TDES (thread_p)
#endif /* SERVER_MODE */

extern int logpb_initialize_pool (THREAD_ENTRY * thread_p);
extern void logpb_finalize_pool (THREAD_ENTRY * thread_p);
extern bool logpb_is_pool_initialized (void);
extern void logpb_invalidate_pool (THREAD_ENTRY * thread_p);
extern LOG_PAGE *logpb_create_page (THREAD_ENTRY * thread_p, LOG_PAGEID pageid);
extern void logpb_set_dirty (THREAD_ENTRY * thread_p, LOG_PAGE * log_pgptr);
extern int logpb_flush_page (THREAD_ENTRY * thread_p, LOG_PAGE * log_pgptr);
extern LOG_PAGEID logpb_get_page_id (LOG_PAGE * log_pgptr);
extern int logpb_initialize_header (THREAD_ENTRY * thread_p, LOG_HEADER * loghdr, const char *prefix_logname,
				    DKNPAGES npages, INT64 * db_creation);
extern LOG_PAGE *logpb_create_header_page (THREAD_ENTRY * thread_p);
extern void logpb_fetch_header (THREAD_ENTRY * thread_p, LOG_HEADER * hdr);
extern void logpb_flush_header (THREAD_ENTRY * thread_p);
extern int logpb_fetch_page (THREAD_ENTRY * thread_p, const LOG_LSA * req_lsa, LOG_CS_ACCESS_MODE access_mode,
			     LOG_PAGE * log_pgptr);
extern int logpb_copy_page_from_log_buffer (THREAD_ENTRY * thread_p, LOG_PAGEID pageid, LOG_PAGE * log_pgptr);
extern int logpb_copy_page_from_file (THREAD_ENTRY * thread_p, LOG_PAGEID pageid, LOG_PAGE * log_pgptr);
extern int logpb_read_page_from_file_or_page_server (THREAD_ENTRY * thread_p, LOG_PAGEID pageid,
						     LOG_CS_ACCESS_MODE access_mode, LOG_PAGE * log_pgptr);
extern int logpb_read_page_from_active_log (THREAD_ENTRY * thread_p, LOG_PAGEID pageid, int num_pages,
					    bool decrypt_needed, LOG_PAGE * log_pgptr);
extern int logpb_write_page_to_disk (THREAD_ENTRY * thread_p, LOG_PAGE * log_pgptr, LOG_PAGEID logical_pageid);
extern int logpb_fetch_header_from_file (THREAD_ENTRY * thread_p, const char *db_fullname, const char *logpath,
					 const char *prefix_logname, LOG_HEADER * hdr, LOG_PAGE * log_pgptr);
extern PGLENGTH logpb_find_header_parameters (THREAD_ENTRY * thread_p, const bool force_read_log_header,
					      const char *db_fullname, const char *logpath,
					      const char *prefix_logname, PGLENGTH * io_page_size,
					      PGLENGTH * log_page_size, INT64 * db_creation, float *db_compatibility,
					      int *db_charset);
extern int logpb_fetch_start_append_page (THREAD_ENTRY * thread_p);
extern LOG_PAGE *logpb_fetch_start_append_page_new (THREAD_ENTRY * thread_p);
extern void logpb_flush_pages_direct (THREAD_ENTRY * thread_p);
extern void logpb_flush_pages (THREAD_ENTRY * thread_p, const LOG_LSA * flush_lsa);
extern void logpb_force_flush_pages (THREAD_ENTRY * thread_p);
extern void logpb_force_flush_header_and_pages (THREAD_ENTRY * thread_p);
extern void logpb_invalid_all_append_pages (THREAD_ENTRY * thread_p);
extern void logpb_flush_log_for_wal (THREAD_ENTRY * thread_p, const LOG_LSA * lsa_ptr);


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
extern LOG_PAGEID logpb_checkpoint (THREAD_ENTRY * thread_p);
extern int logpb_checkpoint_trantable (THREAD_ENTRY * const thread_p);
extern int logpb_backup (THREAD_ENTRY * thread_p, int num_perm_vols, const char *allbackup_path,
			 FILEIO_BACKUP_LEVEL backup_level, bool delete_unneeded_logarchives,
			 const char *backup_verbose_file_path, int num_threads, FILEIO_ZIP_METHOD zip_method,
			 FILEIO_ZIP_LEVEL zip_level, int skip_activelog, int sleep_msecs, bool separate_keys);
extern int logpb_restore (THREAD_ENTRY * thread_p, const char *db_fullname, const char *logpath,
			  const char *prefix_logname, bo_restart_arg * r_args);
extern int logpb_copy_database (THREAD_ENTRY * thread_p, VOLID num_perm_vols, const char *to_db_fullname,
				const char *to_logpath, const char *to_prefix_logname, const char *toext_path,
				const char *fileof_vols_and_copypaths);
extern int logpb_rename_all_volumes_files (THREAD_ENTRY * thread_p, VOLID num_perm_vols, const char *to_db_fullname,
					   const char *to_logpath, const char *to_prefix_logname,
					   const char *toext_path, const char *fileof_vols_and_renamepaths,
					   bool extern_rename, bool force_delete);
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
extern TDE_ALGORITHM logpb_get_tde_algorithm (const LOG_PAGE * log_pgptr);
extern void logpb_set_tde_algorithm (THREAD_ENTRY * thread_p, LOG_PAGE * log_pgptr, const TDE_ALGORITHM tde_algo);

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
extern void logtb_initialize_tdes (LOG_TDES * tdes, int tran_index);
extern void logtb_clear_tdes (THREAD_ENTRY * thread_p, LOG_TDES * tdes);
extern void logtb_finalize_tdes (THREAD_ENTRY * thread_p, LOG_TDES * tdes);
extern int logtb_get_new_tran_id (THREAD_ENTRY * thread_p, LOG_TDES * tdes);
extern int logtb_find_tran_index (THREAD_ENTRY * thread_p, TRANID trid);
#if defined (ENABLE_UNUSED_FUNCTION)
extern int logtb_find_tran_index_host_pid (THREAD_ENTRY * thread_p, const char *host_name, int process_id);
#endif
extern TRANID logtb_find_tranid (int tran_index);
extern TRANID logtb_find_current_tranid (THREAD_ENTRY * thread_p);
#if defined (ENABLE_UNUSED_FUNCTION)
extern int logtb_count_clients_with_type (THREAD_ENTRY * thread_p, int client_type);
#endif
extern int logtb_count_clients (THREAD_ENTRY * thread_p);
extern int logtb_count_not_allowed_clients_in_maintenance_mode (THREAD_ENTRY * thread_p);
extern int logtb_find_client_type (int tran_index);
extern const char *logtb_find_client_name (int tran_index);
extern void logtb_set_user_name (int tran_index, const char *client_name);
extern void logtb_set_current_user_name (THREAD_ENTRY * thread_p, const char *client_name);
extern const char *logtb_find_client_hostname (int tran_index);
extern void logtb_set_current_user_active (THREAD_ENTRY * thread_p, bool is_user_active);
extern int logtb_find_client_name_host_pid (int tran_index, const char **client_prog_name,
					    const char **client_user_name, const char **client_host_name,
					    int *client_pid);
#if !defined(NDEBUG)
extern void logpb_debug_check_log_page (THREAD_ENTRY * thread_p, const LOG_PAGE * log_pgptr);
#endif
#if defined (SERVER_MODE)
extern int logtb_find_client_tran_name_host_pid (int &tran_index, const char **client_prog_name,
						 const char **client_user_name, const char **client_host_name,
						 int *client_pid);
#endif // SERVER_MODE
extern int logtb_get_client_ids (int tran_index, CLIENTIDS * client_info);

extern int logtb_find_current_client_type (THREAD_ENTRY * thread_p);
extern const char *logtb_find_current_client_name (THREAD_ENTRY * thread_p);
extern const char *logtb_find_current_client_hostname (THREAD_ENTRY * thread_p);
extern LOG_LSA *logtb_find_current_tran_lsa (THREAD_ENTRY * thread_p);
extern TRAN_STATE logtb_find_state (int tran_index);
extern int logtb_find_wait_msecs (int tran_index);

extern int logtb_find_interrupt (int tran_index, bool * interrupt);
extern TRAN_ISOLATION logtb_find_isolation (int tran_index);
extern TRAN_ISOLATION logtb_find_current_isolation (THREAD_ENTRY * thread_p);
extern bool logtb_set_tran_index_interrupt (THREAD_ENTRY * thread_p, int tran_index, bool set);
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
extern LOG_LSA *logtb_find_largest_lsa (THREAD_ENTRY * thread_p);
#endif
extern int logtb_set_num_loose_end_trans (THREAD_ENTRY * thread_p);
extern void logtb_rv_read_only_map_undo_tdes (THREAD_ENTRY * thread_p,
					      const std::function < void (const log_tdes &) > map_func);
extern void logtb_find_smallest_lsa (THREAD_ENTRY * thread_p, LOG_LSA * lsa);
extern void logtb_find_smallest_and_largest_active_pages (THREAD_ENTRY * thread_p, LOG_PAGEID * smallest,
							  LOG_PAGEID * largest);
extern int logtb_is_tran_modification_disabled (THREAD_ENTRY * thread_p);
extern bool logtb_has_deadlock_priority (int tran_index);
/* For Debugging */
extern void xlogtb_dump_trantable (THREAD_ENTRY * thread_p, FILE * out_fp);

extern bool logpb_need_wal (const LOG_LSA * lsa);
extern char *logpb_backup_level_info_to_string (char *buf, int buf_size, const LOG_HDR_BKUP_LEVEL_INFO * info);
extern const char *tran_abort_reason_to_string (TRAN_ABORT_REASON val);
extern int logtb_descriptors_start_scan (THREAD_ENTRY * thread_p, int type, DB_VALUE ** arg_values, int arg_cnt,
					 void **ctx);

extern LOG_PAGEID logpb_find_oldest_available_page_id (THREAD_ENTRY * thread_p);
extern int logpb_find_oldest_available_arv_num (THREAD_ENTRY * thread_p);

extern void logtb_get_new_subtransaction_mvccid (THREAD_ENTRY * thread_p, MVCC_INFO * curr_mvcc_info);

extern MVCCID logtb_find_current_mvccid (THREAD_ENTRY * thread_p);
extern MVCCID logtb_get_current_mvccid (THREAD_ENTRY * thread_p);
extern void logtb_get_current_mvccid_and_parent_mvccid (THREAD_ENTRY * thread_p,
							MVCCID & mvccid, MVCCID & parent_mvccid);

extern int logtb_invalidate_snapshot_data (THREAD_ENTRY * thread_p);
extern int xlogtb_get_mvcc_snapshot (THREAD_ENTRY * thread_p);

extern bool logtb_is_current_mvccid (THREAD_ENTRY * thread_p, MVCCID mvccid);
extern bool logtb_is_mvccid_committed (THREAD_ENTRY * thread_p, MVCCID mvccid);
extern MVCC_SNAPSHOT *logtb_get_mvcc_snapshot (THREAD_ENTRY * thread_p);
extern void logtb_complete_mvcc (THREAD_ENTRY * thread_p, LOG_TDES * tdes, bool committed);
extern void logtb_complete_sub_mvcc (THREAD_ENTRY * thread_p, LOG_TDES * tdes);

extern LOG_TRAN_CLASS_COS *logtb_tran_find_class_cos (THREAD_ENTRY * thread_p, const OID * class_oid, bool create);
extern int logtb_tran_update_unique_stats (THREAD_ENTRY * thread_p, const BTID * btid, long long n_keys,
					   long long n_oids, long long n_nulls, bool write_to_log);

// *INDENT-OFF*
extern int logtb_tran_update_unique_stats (THREAD_ENTRY * thread_p, const BTID &btid, const btree_unique_stats &ustats,
                                           bool write_to_log);
extern int logtb_tran_update_unique_stats (THREAD_ENTRY * thread_p, const multi_index_unique_stats &multi_stats,
                                           bool write_to_log);
// *INDENT-ON*

extern int logtb_tran_update_btid_unique_stats (THREAD_ENTRY * thread_p, const BTID * btid, long long n_keys,
						long long n_oids, long long n_nulls);
extern LOG_TRAN_BTID_UNIQUE_STATS *logtb_tran_find_btid_stats (THREAD_ENTRY * thread_p, const BTID * btid, bool create);
extern int logtb_tran_prepare_count_optim_classes (THREAD_ENTRY * thread_p, const char **classes,
						   LC_PREFETCH_FLAGS * flags, int n_classes);
extern void logtb_tran_reset_count_optim_state (THREAD_ENTRY * thread_p);
extern int logtb_find_log_records_count (int tran_index);

extern int logtb_initialize_global_unique_stats_table (THREAD_ENTRY * thread_p);
extern void logtb_finalize_global_unique_stats_table (THREAD_ENTRY * thread_p);
extern int logtb_get_global_unique_stats (THREAD_ENTRY * thread_p, BTID * btid, long long *num_oids,
					  long long *num_nulls, long long *num_keys);
extern int logtb_rv_update_global_unique_stats_by_abs (THREAD_ENTRY * thread_p, BTID * btid, long long num_oids,
						       long long num_nulls, long long num_keys);
extern int logtb_update_global_unique_stats_by_delta (THREAD_ENTRY * thread_p, BTID * btid, long long oid_delta,
						      long long null_delta, long long key_delta, bool log);
extern int logtb_delete_global_unique_stats (THREAD_ENTRY * thread_p, BTID * btid);
extern int logtb_reflect_global_unique_stats_to_btree (THREAD_ENTRY * thread_p);
extern int logtb_tran_update_all_global_unique_stats (THREAD_ENTRY * thread_p);

extern void log_set_ha_promotion_time (THREAD_ENTRY * thread_p, INT64 ha_promotion_time);
extern void log_set_db_restore_time (THREAD_ENTRY * thread_p, INT64 db_restore_time);

extern int logpb_prior_lsa_append_all_list (THREAD_ENTRY * thread_p);

extern bool logtb_check_class_for_rr_isolation_err (const OID * class_oid);

extern void logpb_vacuum_reset_log_header_cache (THREAD_ENTRY * thread_p, LOG_HEADER * loghdr);

extern VACUUM_LOG_BLOCKID logpb_last_complete_blockid (void);
extern bool logpb_page_has_valid_checksum (const LOG_PAGE * log_pgptr);
extern void logpb_dump_log_page_area (THREAD_ENTRY * thread_p, LOG_PAGE * log_pgptr, int offset, int length);
extern void logtb_slam_transaction (THREAD_ENTRY * thread_p, int tran_index);
extern int xlogtb_kill_tran_index (THREAD_ENTRY * thread_p, int kill_tran_index, char *kill_user, char *kill_host,
				   int kill_pid);
extern int xlogtb_kill_or_interrupt_tran (THREAD_ENTRY * thread_p, int tran_id, bool is_dba_group_member,
					  bool interrupt_only);
extern THREAD_ENTRY *logtb_find_thread_by_tran_index (int tran_index);
extern THREAD_ENTRY *logtb_find_thread_by_tran_index_except_me (int tran_index);
extern int logtb_get_current_tran_index (void);
extern void logtb_set_current_tran_index (THREAD_ENTRY * thread_p, int tran_index);
#if defined (SERVER_MODE)
extern void logtb_wakeup_thread_with_tran_index (int tran_index, thread_resume_suspend_status resume_reason);
#endif // SERVER_MODE

extern bool logtb_set_check_interrupt (THREAD_ENTRY * thread_p, bool flag);
extern bool logtb_get_check_interrupt (THREAD_ENTRY * thread_p);
extern void logpb_set_page_checksum (LOG_PAGE * log_pgptr);

extern LOG_TDES *logtb_get_system_tdes (THREAD_ENTRY * thread_p = NULL);
extern int logtb_load_global_statistics_to_tran (THREAD_ENTRY * thread_p);

// *INDENT-OFF*
extern void logpb_respond_fetch_log_page_request (THREAD_ENTRY &thread_r, std::string &payload_in_out);
// *INDENT-ON*

//////////////////////////////////////////////////////////////////////////
// inline/template implementation
//////////////////////////////////////////////////////////////////////////

inline LOG_TDES *
LOG_FIND_TDES (int tran_index)
{
  if (tran_index >= LOG_SYSTEM_TRAN_INDEX && tran_index < log_Gl.trantable.num_total_indices)
    {
      if (tran_index == LOG_SYSTEM_TRAN_INDEX)
	{
	  return logtb_get_system_tdes ();
	}
      else
	{
	  return log_Gl.trantable.all_tdes[tran_index];
	}
    }
  else
    {
      return NULL;
    }
}

inline LOG_TDES *
LOG_FIND_CURRENT_TDES (THREAD_ENTRY * thread_p = NULL)
{
  return LOG_FIND_TDES (LOG_FIND_THREAD_TRAN_INDEX (thread_p));
}

inline bool
logtb_is_system_worker_tranid (TRANID trid)
{
  return trid < NULL_TRANID;
}

#endif /* _LOG_IMPL_H_ */
