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
 * lock_manager.c - lock management module (at the server)
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#if defined(SOLARIS)
#include <netdb.h>
#endif /* SOLARIS */

#include "porting.h"
#include "xserver_interface.h"
#include "lock_manager.h"
#include "system_parameter.h"
#include "memory_alloc.h"
#include "oid.h"
#include "storage_common.h"
#include "log_manager.h"
#include "transaction_sr.h"
#include "wait_for_graph.h"
#include "critical_section.h"
#include "memory_hash.h"
#include "locator.h"
#include "perf_monitor.h"
#include "page_buffer.h"
#include "message_catalog.h"
#include "environment_variable.h"
#include "log_impl.h"
#include "thread.h"
#include "query_manager.h"
#if defined(ENABLE_SYSTEMTAP)
#include "probes.h"
#endif /* ENABLE_SYSTEMTAP */
#include "event_log.h"
#include "tsc_timer.h"
#include "lock_free.h"

#ifndef DB_NA
#define DB_NA           2
#endif
extern int lock_Comp[11][11];

#if defined (SERVER_MODE)
/* object lock hash function */
#define LK_OBJ_LOCK_HASH(oid,htsize)    \
  ((OID_ISTEMP(oid)) ? (unsigned int)(-((oid)->pageid) % htsize) :\
                       lock_get_hash_value(oid, htsize))

/* thread is lock-waiting ? */
#define LK_IS_LOCKWAIT_THREAD(thrd) \
  ((thrd)->lockwait != NULL \
   && (thrd)->lockwait_state == (int) LOCK_SUSPENDED)

/* transaction wait for only some msecs ? */
#define LK_CAN_TIMEOUT(msecs) ((msecs) != LK_INFINITE_WAIT)

/* is younger transaction ? */
#define LK_ISYOUNGER(young_tranid, old_tranid) (young_tranid > old_tranid)

/* Defines for printing lock activity messages */
#define LK_MSG_LOCK_HELPER(entry, msgnum) \
  fprintf(stdout, \
      msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOCK, msgnum)), \
      (entry)->tran_index, LOCK_TO_LOCKMODE_STRING((entry)->granted_mode), \
      (entry)->res_head->oid->volid, (entry)->res_head->oid->pageid, \
      (entry)->oid->slotid)

#define LK_MSG_LOCK_ACQUIRED(entry) \
  LK_MSG_LOCK_HELPER(entry, MSGCAT_LK_OID_LOCK_ACQUIRED)

#define LK_MSG_LOCK_CONVERTED(entry) \
  LK_MSG_LOCK_HELPER(entry, MSGCAT_LK_OID_LOCK_CONVERTED)

#define LK_MSG_LOCK_WAITFOR(entry) \
  LK_MSG_LOCK_HELPER(entry, MSGCAT_LK_OID_LOCK_WAITFOR)

#define LK_MSG_LOCK_RELEASE(entry) \
  LK_MSG_LOCK_HELPER(entry, MSGCAT_LK_OID_LOCK_RELEASE)

#define LK_MSG_LOCK_DEMOTE(entry) \
  LK_MSG_LOCK_HELPER(entry, MSGCAT_LK_OID_LOCK_DEMOTE)

#define EXPAND_WAIT_FOR_ARRAY_IF_NEEDED() \
  do \
    { \
      if (nwaits == max_waits) \
        { \
          if (wait_for == wait_for_buf) \
            { \
              t = (int *) malloc (sizeof (int) * max_waits * 2); \
              if (t != NULL) \
                { \
                  memcpy (t, wait_for, sizeof (int) * max_waits); \
                } \
            } \
          else \
            { \
              t = (int *) realloc (wait_for, sizeof (int) * max_waits * 2); \
            } \
          if (t != NULL) \
            { \
              wait_for = t; \
              max_waits *= 2; \
            } \
          else \
            { \
              goto set_error; \
            } \
        } \
    } \
  while (0)

#define SET_EMULATE_THREAD_WITH_LOCK_ENTRY(th,lock_entry) \
  do \
    { \
      THREAD_ENTRY *locked_thread_entry_p; \
      assert ((th)->emulate_tid == ((pthread_t) 0)); \
      locked_thread_entry_p = thread_find_entry_by_tran_index ((lock_entry)->tran_index); \
      if (locked_thread_entry_p != NULL) \
	{ \
	  (th)->emulate_tid = locked_thread_entry_p->tid; \
	} \
    } \
   while (0)

#define CLEAR_EMULATE_THREAD(th) \
  do \
    { \
      (th)->emulate_tid = ((pthread_t) 0); \
    } \
   while (0)

#endif /* SERVER_MODE */

#define RESOURCE_ALLOC_WAIT_TIME 10	/* 10 msec */
#define KEY_LOCK_ESCALATION_THRESHOLD 10	/* key lock escalation threshold */
#define MAX_NUM_LOCKS_DUMP_TO_EVENT_LOG 100

/* state of suspended threads */
typedef enum
{
  LOCK_SUSPENDED,		/* Thread has been suspended */
  LOCK_RESUMED,			/* Thread has been resumed */
  LOCK_RESUMED_TIMEOUT,		/* Thread has been resumed and notified of lock timeout */
  LOCK_RESUMED_DEADLOCK_TIMEOUT,	/* Thread has been resumed and notified of lock timeout because the current
					 * transaction is selected as a deadlock victim */
  LOCK_RESUMED_ABORTED,		/* Thread has been resumed, however it must be aborted because of a deadlock */
  LOCK_RESUMED_ABORTED_FIRST,	/* in case of the first aborted thread */
  LOCK_RESUMED_ABORTED_OTHER,	/* in case of other aborted threads */
  LOCK_RESUMED_INTERRUPT
} LOCK_WAIT_STATE;

/*
 * Message id in the set MSGCAT_SET_LOCK
 * in the message catalog MSGCAT_CATALOG_CUBRID (file cubrid.msg).
 */
#define MSGCAT_LK_NEWLINE                       1
#define MSGCAT_LK_SUSPEND_TRAN                  2
#define MSGCAT_LK_RESUME_TRAN                   3
#define MSGCAT_LK_OID_LOCK_ACQUIRED             4
#define MSGCAT_LK_VPID_LOCK_ACQUIRED            5
#define MSGCAT_LK_OID_LOCK_CONVERTED            6
#define MSGCAT_LK_VPID_LOCK_CONVERTED           7
#define MSGCAT_LK_OID_LOCK_WAITFOR              8
#define MSGCAT_LK_VPID_LOCK_WAITFOR             9
#define MSGCAT_LK_OID_LOCK_RELEASE              10
#define MSGCAT_LK_VPID_LOCK_RELEASE             11
#define MSGCAT_LK_OID_LOCK_DEMOTE               12
#define MSGCAT_LK_VPID_LOCK_DEMOTE              13
#define MSGCAT_LK_RES_OID                       14
#define MSGCAT_LK_RES_ROOT_CLASS_TYPE           15
#define MSGCAT_LK_RES_CLASS_TYPE                16
#define MSGCAT_LK_RES_INSTANCE_TYPE             17
#define MSGCAT_LK_RES_UNKNOWN_TYPE              18
#define MSGCAT_LK_RES_TOTAL_MODE                19
#define MSGCAT_LK_RES_LOCK_COUNT                20
#define MSGCAT_LK_RES_NON_BLOCKED_HOLDER_HEAD   21
#define MSGCAT_LK_RES_BLOCKED_HOLDER_HEAD       22
#define MSGCAT_LK_RES_BLOCKED_WAITER_HEAD       23
#define MSGCAT_LK_RES_NON2PL_RELEASED_HEAD      24
#define MSGCAT_LK_RES_NON_BLOCKED_HOLDER_ENTRY  25
#define MSGCAT_LK_RES_NON_BLOCKED_HOLDER_ENTRY_WITH_GRANULE 26
#define MSGCAT_LK_RES_BLOCKED_HOLDER_ENTRY      27
#define MSGCAT_LK_RES_BLOCKED_HOLDER_ENTRY_WITH_GRANULE 28
#define MSGCAT_LK_RES_BLOCKED_WAITER_ENTRY      29
#define MSGCAT_LK_RES_NON2PL_RELEASED_ENTRY     30
#define MSGCAT_LK_RES_VPID                      31
#define MSGCAT_LK_DUMP_LOCK_TABLE               32
#define MSGCAT_LK_DUMP_TRAN_IDENTIFIERS         33
#define MSGCAT_LK_DUMP_TRAN_ISOLATION           34
#define MSGCAT_LK_DUMP_TRAN_STATE               35
#define MSGCAT_LK_DUMP_TRAN_TIMEOUT_PERIOD      36
#define MSGCAT_LK_DEADLOCK_ABORT_HDR            37
#define MSGCAT_LK_DEADLOCK_ABORT                38
#define MSGCAT_LK_DEADLOCK_TIMEOUT_HDR          39
#define MSGCAT_LK_DEADLOCK_TIMEOUT              40
#define MSGCAT_LK_DEADLOCK_FUN_HDR              41
#define MSGCAT_LK_DEADLOCK_FUN                  42
#define MSGCAT_LK_RES_INDEX_KEY_TYPE            43
#define MSGCAT_LK_INDEXNAME                     44
#define MSGCAT_LK_RES_RR_TYPE			45
#define MSGCAT_LK_MVCC_INFO			46
#define MSGCAT_LK_LASTONE                       47

#if defined(SERVER_MODE)

typedef struct lk_lockinfo LK_LOCKINFO;
struct lk_lockinfo
{
  OID *org_oidp;
  OID oid;
  OID class_oid;
  LOCK lock;
};


/* TWFG (transaction wait-for graph) entry and edge */
typedef struct lk_WFG_node LK_WFG_NODE;
struct lk_WFG_node
{
  int first_edge;
  int candidate;
  int current;
  int ancestor;
  INT64 thrd_wait_stime;
  int tran_edge_seq_num;
  bool checked_by_deadlock_detector;
  bool DL_victim;
};

typedef struct lk_WFG_edge LK_WFG_EDGE;
struct lk_WFG_edge
{
  int to_tran_index;
  int edge_seq_num;
  int holder_flag;
  int next;
  INT64 edge_wait_stime;
};

typedef struct lk_deadlock_victim LK_DEADLOCK_VICTIM;
struct lk_deadlock_victim
{
  /* following two fields are used for only global deadlock detection */
  int (*cycle_fun) (int tran_index, void *args);
  void *args;			/* Arguments to be passed to cycle_fun */

  int tran_index;		/* Index of selected victim */
  TRANID tranid;		/* Transaction identifier */
  int can_timeout;		/* Is abort or timeout */

  int num_trans_in_cycle;	/* # of transaction in cycle */
  int *tran_index_in_cycle;	/* tran_index array for transaction in cycle */
};

/*
 * Lock Entry Block Structure
 */
typedef struct lk_entry_block LK_ENTRY_BLOCK;
struct lk_entry_block
{
  LK_ENTRY_BLOCK *next_block;	/* next lock entry block */
  LK_ENTRY *block;		/* lk_entry block */
  int count;			/* # of entries in lock entry block */
};

/*
 * Lock Resource Block Structure
 */
typedef struct lk_res_block LK_RES_BLOCK;
struct lk_res_block
{
  LK_RES_BLOCK *next_block;	/* next lock resource block */
  LK_RES *block;		/* lk_res block */
  int count;			/* # of entries in lock res block */
};

/*
 * Transaction Lock Entry Structure
 */
typedef struct lk_tran_lock LK_TRAN_LOCK;
struct lk_tran_lock
{
  /* transaction lock hold lists */
  pthread_mutex_t hold_mutex;	/* mutex for hold lists */
  LK_ENTRY *inst_hold_list;	/* instance lock hold list */
  LK_ENTRY *class_hold_list;	/* class lock hold list */
  LK_ENTRY *root_class_hold;	/* root class lock hold */
  LK_ENTRY *lk_entry_pool;	/* local pool of lock entries which can be used with no synchronization. */
  int lk_entry_pool_count;	/* Current count of lock entries in local pool. */
  int inst_hold_count;		/* # of entries in inst_hold_list */
  int class_hold_count;		/* # of entries in class_hold_list */

  LK_ENTRY *waiting;		/* waiting lock entry */

  /* non two phase lock list */
  pthread_mutex_t non2pl_mutex;	/* mutex for non2pl_list */
  LK_ENTRY *non2pl_list;	/* non2pl list */
  int num_incons_non2pl;	/* # of inconsistent non2pl */

  /* lock escalation related fields */
  bool lock_escalation_on;

  /* locking on manual duration */
  bool is_instant_duration;
};
/* Max size of transaction local pool of lock entries. */
#define LOCK_TRAN_LOCAL_POOL_MAX_SIZE 10

/*
 * Lock Manager Global Data Structure
 */

typedef struct lk_global_data LK_GLOBAL_DATA;
struct lk_global_data
{
  /* object lock table including hash table */
  int max_obj_locks;		/* max # of object locks */

  LF_HASH_TABLE obj_hash_table;
  LF_FREELIST obj_free_res_list;
  LF_FREELIST obj_free_entry_list;

  /* transaction lock table */
  int num_trans;		/* # of transactions */
  LK_TRAN_LOCK *tran_lock_table;	/* transaction lock hold table */

  /* deadlock detection related fields */
  pthread_mutex_t DL_detection_mutex;
  struct timeval last_deadlock_run;	/* last deadlock detetion time */
  LK_WFG_NODE *TWFG_node;	/* transaction WFG node */
  LK_WFG_EDGE *TWFG_edge;	/* transaction WFG edge */
  int max_TWFG_edge;
  int TWFG_free_edge_idx;
  int global_edge_seq_num;

  /* miscellaneous things */
  short no_victim_case_count;
  bool verbose_mode;
#if defined(LK_DUMP)
  bool dump_level;
#endif				/* LK_DUMP */
};

LK_GLOBAL_DATA lk_Gl = {
  0, LF_HASH_TABLE_INITIALIZER,
  LF_FREELIST_INITIALIZER, LF_FREELIST_INITIALIZER,
  0, NULL, PTHREAD_MUTEX_INITIALIZER, {0, 0},
  NULL, NULL, 0, 0, 0, 0, false
#if defined(LK_DUMP)
    , 0
#endif /* LK_DUMP */
};

/* size of each data structure */
static const int SIZEOF_LK_LOCKINFO = sizeof (LK_LOCKINFO);
static const int SIZEOF_LK_WFG_NODE = sizeof (LK_WFG_NODE);
static const int SIZEOF_LK_WFG_EDGE = sizeof (LK_WFG_EDGE);
static const int SIZEOF_LK_TRAN_LOCK = sizeof (LK_TRAN_LOCK);

static const int SIZEOF_LK_RES = sizeof (LK_RES);
static const int SIZEOF_LK_ENTRY_BLOCK = sizeof (LK_ENTRY_BLOCK);
static const int SIZEOF_LK_RES_BLOCK = sizeof (LK_RES_BLOCK);
static const int SIZEOF_LK_ACQOBJ_LOCK = sizeof (LK_ACQOBJ_LOCK);

/* minimum # of locks that are required */
/* TODO : change const */
#define LK_MIN_OBJECT_LOCKS  (MAX_NTRANS * 300)

/* the ratio in the number of lock entries for each entry type */
static const int LK_HASH_RATIO = 8;
static const float LK_RES_RATIO = 0.1f;
static const float LK_ENTRY_RATIO = 0.1f;

/* the lock entry expansion count */
/* TODO : change const */
#define LK_MORE_RES_COUNT  (MAX_NTRANS * 20 * LK_RES_RATIO)
#define LK_MORE_ENTRY_COUNT (MAX_NTRANS * 20 * LK_ENTRY_RATIO)

/* miscellaneous constants */
static const int LK_SLEEP_MAX_COUNT = 3;
#define LK_LOCKINFO_FIXED_COUNT	30
/* TODO : change const */
#define LK_MAX_VICTIM_COUNT  300

/* transaction WFG edge related constants */
static const int LK_MIN_TWFG_EDGE_COUNT = 200;
/* TODO : change const */
#define LK_MID_TWFG_EDGE_COUNT 1000
/* TODO : change const */
#define LK_MAX_TWFG_EDGE_COUNT (MAX_NTRANS * MAX_NTRANS)

#define DEFAULT_WAIT_USERS	10
static const int LK_COMPOSITE_LOCK_OID_INCREMENT = 100;
#endif /* SERVER_MODE */

#if defined(SERVER_MODE)

static LK_WFG_EDGE TWFG_edge_block[LK_MID_TWFG_EDGE_COUNT];
static LK_DEADLOCK_VICTIM victims[LK_MAX_VICTIM_COUNT];
static int victim_count;
#else /* !SERVER_MODE */
static int lk_Standalone_has_xlock = 0;
#define LK_SET_STANDALONE_XLOCK(lock)					      \
  do {									      \
    if ((lock) == SCH_M_LOCK || (lock) == X_LOCK || lock == IX_LOCK	      \
	|| lock == SIX_LOCK)						      \
      {									      \
	lk_Standalone_has_xlock = true;					      \
      }									      \
  } while (0)
#endif /* !SERVER_MODE */

#if defined(SERVER_MODE)
static void lock_initialize_entry (LK_ENTRY * entry_ptr);
static void lock_initialize_entry_as_granted (LK_ENTRY * entry_ptr, int tran_index, LK_RES * res, LOCK lock);
static void lock_initialize_entry_as_blocked (LK_ENTRY * entry_ptr, THREAD_ENTRY * thread_p, int tran_index,
					      LK_RES * res, LOCK lock);
static void lock_initialize_entry_as_non2pl (LK_ENTRY * entry_ptr, int tran_index, LK_RES * res, LOCK lock);
static void lock_initialize_resource (LK_RES * res_ptr);
static void lock_initialize_resource_as_allocated (LK_RES * res_ptr, LOCK lock);
static unsigned int lock_get_hash_value (const OID * oid, int htsize);
static int lock_initialize_tran_lock_table (void);
static int lock_initialize_object_hash_table (void);
static int lock_initialize_object_lock_res_list (void);
static int lock_initialize_object_lock_entry_list (void);
static int lock_initialize_deadlock_detection (void);
static int lock_remove_resource (LK_RES * res_ptr);
static void lock_insert_into_tran_hold_list (LK_ENTRY * entry_ptr, int owner_tran_index);
static int lock_delete_from_tran_hold_list (LK_ENTRY * entry_ptr, int owner_tran_index);
static void lock_insert_into_tran_non2pl_list (LK_ENTRY * non2pl, int owner_tran_index);
static int lock_delete_from_tran_non2pl_list (LK_ENTRY * non2pl, int owner_tran_index);
static LK_ENTRY *lock_find_tran_hold_entry (int tran_index, const OID * oid, bool is_class);
static bool lock_is_class_lock_escalated (LOCK class_lock, LOCK lock_escalation);
static LK_ENTRY *lock_add_non2pl_lock (THREAD_ENTRY * thread_p, LK_RES * res_ptr, int tran_index, LOCK lock);
static void lock_position_holder_entry (LK_RES * res_ptr, LK_ENTRY * entry_ptr);
static void lock_set_error_for_timeout (THREAD_ENTRY * thread_p, LK_ENTRY * entry_ptr);
static void lock_set_error_for_aborted (LK_ENTRY * entry_ptr, TRAN_ABORT_REASON abort_reason);
static LOCK_WAIT_STATE lock_suspend (THREAD_ENTRY * thread_p, LK_ENTRY * entry_ptr, int wait_msecs);
static void lock_resume (LK_ENTRY * entry_ptr, int state);
static bool lock_wakeup_deadlock_victim_timeout (int tran_index);
static bool lock_wakeup_deadlock_victim_aborted (int tran_index);
static void lock_grant_blocked_holder (THREAD_ENTRY * thread_p, LK_RES * res_ptr);
static int lock_grant_blocked_waiter (THREAD_ENTRY * thread_p, LK_RES * res_ptr);
static void lock_grant_blocked_waiter_partial (THREAD_ENTRY * thread_p, LK_RES * res_ptr, LK_ENTRY * from_whom);
static bool lock_check_escalate (THREAD_ENTRY * thread_p, LK_ENTRY * class_entry, LK_TRAN_LOCK * tran_lock);
static int lock_escalate_if_needed (THREAD_ENTRY * thread_p, LK_ENTRY * class_entry, int tran_index);
static int lock_internal_hold_lock_object_instant (int tran_index, const OID * oid, const OID * class_oid, LOCK lock);
static int lock_internal_perform_lock_object (THREAD_ENTRY * thread_p, int tran_index, const OID * oid,
					      const OID * class_oid, LOCK lock, int wait_msecs,
					      LK_ENTRY ** entry_addr_ptr, LK_ENTRY * class_entry);
static void lock_internal_perform_unlock_object (THREAD_ENTRY * thread_p, LK_ENTRY * entry_ptr, int release_flag,
						 int move_to_non2pl);
static void lock_unlock_object_by_isolation (THREAD_ENTRY * thread_p, int tran_index, TRAN_ISOLATION isolation,
					     const OID * class_oid, const OID * oid);
static void lock_unlock_inst_locks_of_class_by_isolation (THREAD_ENTRY * thread_p, int tran_index,
							  TRAN_ISOLATION isolation, const OID * class_oid);
static int lock_internal_demote_shared_class_lock (THREAD_ENTRY * thread_p, LK_ENTRY * entry_ptr);
static void lock_demote_all_shared_class_locks (THREAD_ENTRY * thread_p, int tran_index);
static void lock_unlock_shared_inst_lock (THREAD_ENTRY * thread_p, int tran_index, const OID * inst_oid);
static void lock_remove_all_class_locks (THREAD_ENTRY * thread_p, int tran_index, LOCK lock);
static void lock_remove_non2pl (LK_ENTRY * non2pl, int tran_index);
static void lock_update_non2pl_list (THREAD_ENTRY * thread_p, LK_RES * res_ptr, int tran_index, LOCK lock);
static int lock_add_WFG_edge (int from_tran_index, int to_tran_index, int holder_flag, INT64 edge_wait_stime);
static void lock_select_deadlock_victim (THREAD_ENTRY * thread_p, int s, int t);
static void lock_dump_deadlock_victims (THREAD_ENTRY * thread_p, FILE * outfile);
static int lock_compare_lock_info (const void *lockinfo1, const void *lockinfo2);
static float lock_wait_msecs_to_secs (int msecs);
static void lock_dump_resource (THREAD_ENTRY * thread_p, FILE * outfp, LK_RES * res_ptr);
#if defined (ENABLE_UNUSED_FUNCTION)
static bool lock_check_consistent_resource (THREAD_ENTRY * thread_p, LK_RES * res_ptr);
static bool lock_check_consistent_tran_lock (LK_TRAN_LOCK * tran_lock);
#endif

static void lock_increment_class_granules (LK_ENTRY * class_entry);

static void lock_decrement_class_granules (LK_ENTRY * class_entry);
static LK_ENTRY *lock_find_class_entry (int tran_index, const OID * class_oid);

static void lock_event_log_tran_locks (THREAD_ENTRY * thread_p, FILE * log_fp, int tran_index);
static void lock_event_log_blocked_lock (THREAD_ENTRY * thread_p, FILE * log_fp, LK_ENTRY * entry);
static void lock_event_log_blocking_locks (THREAD_ENTRY * thread_p, FILE * log_fp, LK_ENTRY * wait_entry);
static void lock_event_log_lock_info (THREAD_ENTRY * thread_p, FILE * log_fp, LK_ENTRY * entry);
static void lock_event_set_tran_wait_entry (int tran_index, LK_ENTRY * entry);
static void lock_event_set_xasl_id_to_entry (int tran_index, LK_ENTRY * entry);
static LK_RES_KEY lock_create_search_key (OID * oid, OID * class_oid);
#if defined (SERVER_MODE)
static bool lock_is_safe_lock_with_page (THREAD_ENTRY * thread_p, LK_ENTRY * entry_ptr);
#endif /* SERVER_MODE */

static LK_ENTRY *lock_get_new_entry (int tran_index, LF_TRAN_ENTRY * tran_entry, LF_FREELIST * freelist);
static void lock_free_entry (int tran_index, LF_TRAN_ENTRY * tran_entry, LF_FREELIST * freelist, LK_ENTRY * lock_entry);

/* object lock entry */
static void *lock_alloc_entry (void);
static int lock_dealloc_entry (void *res);
static int lock_init_entry (void *res);
static int lock_uninit_entry (void *res);

LF_ENTRY_DESCRIPTOR obj_lock_entry_desc = {
  offsetof (LK_ENTRY, stack),
  offsetof (LK_ENTRY, next),
  offsetof (LK_ENTRY, del_id),
  0,				/* does not have a key, not used in a hash table */
  0,				/* does not have a mutex, protected by resource mutex */
  LF_EM_NOT_USING_MUTEX,
  lock_alloc_entry,
  lock_dealloc_entry,
  lock_init_entry,
  lock_uninit_entry,
  NULL, NULL, NULL,		/* no key */
  NULL				/* no inserts */
};

/*
 * Object lock resource
 */
static void *lock_alloc_resource (void);
static int lock_dealloc_resource (void *res);
static int lock_init_resource (void *res);
static int lock_uninit_resource (void *res);
static int lock_res_key_copy (void *src, void *dest);
static int lock_res_key_compare (void *k1, void *k2);
static unsigned int lock_res_key_hash (void *key, int htsize);

LF_ENTRY_DESCRIPTOR obj_lock_res_desc = {
  offsetof (LK_RES, stack),
  offsetof (LK_RES, hash_next),
  offsetof (LK_RES, del_id),
  offsetof (LK_RES, key),
  offsetof (LK_RES, res_mutex),

  LF_EM_USING_MUTEX,

  lock_alloc_resource,
  lock_dealloc_resource,
  lock_init_resource,
  lock_uninit_resource,
  lock_res_key_copy,
  lock_res_key_compare,
  lock_res_key_hash,
  NULL				/* no inserts */
};
#endif /* SERVER_MODE */


#if defined(SERVER_MODE)

static LK_RES_KEY
lock_create_search_key (OID * oid, OID * class_oid)
{
  LK_RES_KEY search_key;

  /* copy *IDs */
  if (oid != NULL)
    {
      COPY_OID (&search_key.oid, oid);
    }
  else
    {
      OID_SET_NULL (&search_key.oid);
    }

  if (class_oid != NULL)
    {
      COPY_OID (&search_key.class_oid, class_oid);
    }
  else
    {
      OID_SET_NULL (&search_key.class_oid);
    }

  /* set correct type */
  if (oid != NULL && OID_IS_ROOTOID (oid))
    {
      search_key.type = LOCK_RESOURCE_ROOT_CLASS;
    }
  else
    {
      if (class_oid == NULL || OID_IS_ROOTOID (class_oid))
	{
	  search_key.type = LOCK_RESOURCE_CLASS;
	}
      else
	{
	  search_key.type = LOCK_RESOURCE_INSTANCE;
	}
    }

  /* done! */
  return search_key;
}

static void *
lock_alloc_entry (void)
{
  return malloc (sizeof (LK_ENTRY));
}

static int
lock_dealloc_entry (void *res)
{
  free (res);
  return NO_ERROR;
}

static int
lock_init_entry (void *entry)
{
  LK_ENTRY *entry_ptr = (LK_ENTRY *) entry;
  if (entry_ptr != NULL)
    {
      return NO_ERROR;
    }
  else
    {
      assert (false);
      return ER_FAILED;
    }
}

static int
lock_uninit_entry (void *entry)
{
  LK_ENTRY *entry_ptr = (LK_ENTRY *) entry;

  if (entry_ptr == NULL)
    {
      return ER_FAILED;
    }

  entry_ptr->tran_index = -1;
  entry_ptr->thrd_entry = NULL;

  return NO_ERROR;
}

static void *
lock_alloc_resource (void)
{
  LK_RES *res_ptr = (LK_RES *) malloc (sizeof (LK_RES));
  if (res_ptr != NULL)
    {
      pthread_mutex_init (&(res_ptr->res_mutex), NULL);
    }
  return res_ptr;
}

static int
lock_dealloc_resource (void *res)
{
  LK_RES *res_ptr = (LK_RES *) res;
  if (res_ptr != NULL)
    {
      pthread_mutex_destroy (&res_ptr->res_mutex);
      free (res_ptr);
      return NO_ERROR;
    }
  else
    {
      assert (false);
      return ER_FAILED;
    }
}

static int
lock_init_resource (void *res)
{
  LK_RES *res_ptr = (LK_RES *) res;

  if (res_ptr == NULL)
    {
      return ER_FAILED;
    }

  res_ptr->total_holders_mode = NULL_LOCK;
  res_ptr->total_waiters_mode = NULL_LOCK;
  res_ptr->holder = NULL;
  res_ptr->waiter = NULL;
  res_ptr->non2pl = NULL;
  res_ptr->hash_next = NULL;

  return NO_ERROR;
}

static int
lock_uninit_resource (void *res)
{
  LK_RES *res_ptr = (LK_RES *) res;

  if (res == NULL)
    {
      return ER_FAILED;
    }

  assert (res_ptr->holder == NULL);
  assert (res_ptr->waiter == NULL);
  assert (res_ptr->non2pl == NULL);

  /* TO BE FILLED IN AS NECESSARY */

  return NO_ERROR;
}

static int
lock_res_key_copy (void *src, void *dest)
{
  LK_RES_KEY *src_k = (LK_RES_KEY *) src;
  LK_RES_KEY *dest_k = (LK_RES_KEY *) dest;

  if (src_k == NULL || dest_k == NULL)
    {
      return ER_FAILED;
    }

  dest_k->type = src_k->type;
  switch (src_k->type)
    {
    case LOCK_RESOURCE_INSTANCE:
      COPY_OID (&dest_k->oid, &src_k->oid);
      COPY_OID (&dest_k->class_oid, &src_k->class_oid);
      break;

    case LOCK_RESOURCE_CLASS:
    case LOCK_RESOURCE_ROOT_CLASS:
      COPY_OID (&dest_k->oid, &src_k->oid);
      OID_SET_NULL (&dest_k->class_oid);
      break;

    case LOCK_RESOURCE_OBJECT:
      /* nothing, it's a free object */
      break;

    default:
      assert (false);
      return ER_FAILED;
    }

  return NO_ERROR;
}

static int
lock_res_key_compare (void *k1, void *k2)
{
  LK_RES_KEY *k1_k = (LK_RES_KEY *) k1;
  LK_RES_KEY *k2_k = (LK_RES_KEY *) k2;

  if (k1_k == NULL || k2_k == NULL)
    {
      return 1;
    }

  switch (k1_k->type)
    {
    case LOCK_RESOURCE_INSTANCE:
    case LOCK_RESOURCE_CLASS:
    case LOCK_RESOURCE_ROOT_CLASS:
      /* fast and dirty oid comparison */
      if (OID_EQ (&k1_k->oid, &k2_k->oid))
	{
	  assert (k1_k->type == k2_k->type);

	  /* equal */
	  return 0;
	}
      else
	{
	  /* not equal */
	  return 1;
	}
      break;

    case LOCK_RESOURCE_OBJECT:
    default:
      /* unfortunately, there's no error reporting here, but an always-true comparison will generate errors early on
       * and is easier to spot */
      assert (false);
      return 0;
    }
}

static unsigned int
lock_res_key_hash (void *key, int htsize)
{
  LK_RES_KEY *key_k = (LK_RES_KEY *) key;

  if (key_k != NULL)
    {
      return LK_OBJ_LOCK_HASH (&key_k->oid, htsize);
    }
  else
    {
      assert (false);
      return 0;
    }
}

/* initialize lock entry as free state */
static void
lock_initialize_entry (LK_ENTRY * entry_ptr)
{
  entry_ptr->tran_index = -1;
  entry_ptr->thrd_entry = NULL;
  entry_ptr->res_head = NULL;
  entry_ptr->granted_mode = NULL_LOCK;
  entry_ptr->blocked_mode = NULL_LOCK;
  entry_ptr->next = NULL;
  entry_ptr->tran_next = NULL;
  entry_ptr->tran_prev = NULL;
  entry_ptr->class_entry = NULL;
  entry_ptr->ngranules = 0;
  entry_ptr->instant_lock_count = 0;
  entry_ptr->bind_index_in_tran = -1;
  XASL_ID_SET_NULL (&entry_ptr->xasl_id);
}

/* initialize lock entry as granted state */
static void
lock_initialize_entry_as_granted (LK_ENTRY * entry_ptr, int tran_index, LK_RES * res, LOCK lock)
{
  entry_ptr->tran_index = tran_index;
  entry_ptr->thrd_entry = NULL;
  entry_ptr->res_head = res;
  entry_ptr->granted_mode = lock;
  entry_ptr->blocked_mode = NULL_LOCK;
  entry_ptr->count = 1;
  entry_ptr->next = NULL;
  entry_ptr->tran_next = NULL;
  entry_ptr->tran_prev = NULL;
  entry_ptr->class_entry = NULL;
  entry_ptr->ngranules = 0;
  entry_ptr->instant_lock_count = 0;

  lock_event_set_xasl_id_to_entry (tran_index, entry_ptr);
}

/* initialize lock entry as blocked state */
static void
lock_initialize_entry_as_blocked (LK_ENTRY * entry_ptr, THREAD_ENTRY * thread_p, int tran_index, LK_RES * res,
				  LOCK lock)
{
  entry_ptr->tran_index = tran_index;
  entry_ptr->thrd_entry = thread_p;
  entry_ptr->res_head = res;
  entry_ptr->granted_mode = NULL_LOCK;
  entry_ptr->blocked_mode = lock;
  entry_ptr->count = 1;
  entry_ptr->next = NULL;
  entry_ptr->tran_next = NULL;
  entry_ptr->tran_prev = NULL;
  entry_ptr->class_entry = NULL;
  entry_ptr->ngranules = 0;
  entry_ptr->instant_lock_count = 0;

  lock_event_set_xasl_id_to_entry (tran_index, entry_ptr);
}

/* initialize lock entry as non2pl state */
static void
lock_initialize_entry_as_non2pl (LK_ENTRY * entry_ptr, int tran_index, LK_RES * res, LOCK lock)
{
  entry_ptr->tran_index = tran_index;
  entry_ptr->thrd_entry = NULL;
  entry_ptr->res_head = res;
  entry_ptr->granted_mode = lock;
  entry_ptr->blocked_mode = NULL_LOCK;
  entry_ptr->count = 0;
  entry_ptr->next = NULL;
  entry_ptr->tran_next = NULL;
  entry_ptr->tran_prev = NULL;
  entry_ptr->class_entry = NULL;
  entry_ptr->ngranules = 0;
  entry_ptr->instant_lock_count = 0;
}

/* initialize lock resource as free state */
static void
lock_initialize_resource (LK_RES * res_ptr)
{
  pthread_mutex_init (&(res_ptr->res_mutex), NULL);
  res_ptr->key.type = LOCK_RESOURCE_OBJECT;
  OID_SET_NULL (&(res_ptr->key.oid));
  OID_SET_NULL (&(res_ptr->key.class_oid));
  res_ptr->total_holders_mode = NULL_LOCK;
  res_ptr->total_waiters_mode = NULL_LOCK;
  res_ptr->holder = NULL;
  res_ptr->waiter = NULL;
  res_ptr->non2pl = NULL;
  res_ptr->hash_next = NULL;
}

/* initialize lock resource as allocated state */
static void
lock_initialize_resource_as_allocated (LK_RES * res_ptr, LOCK lock)
{
  res_ptr->total_holders_mode = lock;
  res_ptr->total_waiters_mode = NULL_LOCK;
  res_ptr->holder = NULL;
  res_ptr->waiter = NULL;
  res_ptr->non2pl = NULL;
}

/*
 * lock_get_hash_value -
 *
 * return:
 *
 *   oid(in):
 */
static unsigned int
lock_get_hash_value (const OID * oid, int htsize)
{
  unsigned int next_base_slotid, addr;

  if (oid->slotid <= 0)
    {
      /* In an unique index, the OID and ClassOID of the last key are <root page's volid, root page's pageid, -1> and
       * <root page's volid, root page's pageid, 0>, recpectively. In a non-unique index, the OID of the last key is
       * <root page's volid, root page's pageid, -1> */
      addr = oid->pageid - oid->slotid;
    }
  else
    {
      next_base_slotid = 2;
      while (next_base_slotid <= oid->slotid)
	{
	  next_base_slotid = next_base_slotid * 2;
	}

      addr = oid->pageid + (htsize / next_base_slotid) * (2 * oid->slotid - next_base_slotid + 1);
    }

  return (addr % htsize);
}
#endif /* SERVER_MODE */

/*
 *  Private Functions Group 1: initalize and finalize major structures
 *
 *   - lock_init_tran_lock_table()
 *   - lock_init_object_hash_table()
 *   - lock_init_object_lock_res_list()
 *   - lock_init_object_lock_entry_list()
 *   - lock_init_deadlock_detection()
 */

#if defined(SERVER_MODE)
/*
 * lock_initialize_tran_lock_table - Initialize the transaction lock hold table.
 *
 * return: error code
 *
 * Note:This function allocates the transaction lock hold table and
 *     initializes the table.
 */
static int
lock_initialize_tran_lock_table (void)
{
  LK_TRAN_LOCK *tran_lock;	/* pointer to transaction hold entry */
  int i, j;			/* loop variable */
  LK_ENTRY *entry = NULL;

  /* initialize the number of transactions */
  lk_Gl.num_trans = MAX_NTRANS;

  /* allocate memory space for transaction lock table */
  lk_Gl.tran_lock_table = (LK_TRAN_LOCK *) malloc (SIZEOF_LK_TRAN_LOCK * lk_Gl.num_trans);
  if (lk_Gl.tran_lock_table == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      (size_t) (SIZEOF_LK_TRAN_LOCK * lk_Gl.num_trans));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  /* initialize all the entries of transaction lock table */
  memset (lk_Gl.tran_lock_table, 0, SIZEOF_LK_TRAN_LOCK * lk_Gl.num_trans);
  for (i = 0; i < lk_Gl.num_trans; i++)
    {
      tran_lock = &lk_Gl.tran_lock_table[i];
      pthread_mutex_init (&tran_lock->hold_mutex, NULL);
      pthread_mutex_init (&tran_lock->non2pl_mutex, NULL);

      for (j = 0; j < LOCK_TRAN_LOCAL_POOL_MAX_SIZE; j++)
	{
	  entry = (LK_ENTRY *) malloc (sizeof (LK_ENTRY));
	  lock_initialize_entry (entry);
	  entry->next = tran_lock->lk_entry_pool;
	  tran_lock->lk_entry_pool = entry;
	}
      tran_lock->lk_entry_pool_count = LOCK_TRAN_LOCAL_POOL_MAX_SIZE;
    }

  return NO_ERROR;
}
#endif /* SERVER_MODE */

#if defined(SERVER_MODE)
/*
 * lock_initialize_object_hash_table - Initializes the object lock hash table
 *
 * return: error code
 *
 * Note:This function initializes an object lock hash table.
 */
static int
lock_initialize_object_hash_table (void)
{
#define LK_INITIAL_OBJECT_LOCK_TABLE_SIZE       10000
  int obj_hash_size, ret;

  lk_Gl.max_obj_locks = LK_INITIAL_OBJECT_LOCK_TABLE_SIZE;

  /* allocate an object lock hash table */
  if (lk_Gl.max_obj_locks > LK_MIN_OBJECT_LOCKS)
    {
      obj_hash_size = lk_Gl.max_obj_locks * LK_HASH_RATIO;
    }
  else
    {
      obj_hash_size = LK_MIN_OBJECT_LOCKS * LK_HASH_RATIO;
    }

  /* initialize object hash table */
  ret = lf_hash_init (&lk_Gl.obj_hash_table, &lk_Gl.obj_free_res_list, obj_hash_size, &obj_lock_res_desc);
  if (ret != NO_ERROR)
    {
      return ret;
    }

  return NO_ERROR;
}
#endif /* SERVER_MODE */

#if defined(SERVER_MODE)
/*
 * lock_initialize_object_lock_res_list - Initializes the object lock resource list
 *
 * return: error code
 *
 * Note:
 *     This function initializes following two lists.
 *     1. a list of object lock resource block
 *        => each block has object lock resource block.
 *     2. a list of freed object lock resource entries.
 */
static int
lock_initialize_object_lock_res_list (void)
{
  int block_size, block_count, ret;

  /* initialize */
  block_count = 1;
  block_size = (int) MAX ((lk_Gl.max_obj_locks * LK_RES_RATIO), 1);
  ret = lf_freelist_init (&lk_Gl.obj_free_res_list, block_count, block_size, &obj_lock_res_desc, &obj_lock_res_Ts);
  if (ret != NO_ERROR)
    {
      return ret;
    }

  return NO_ERROR;
}
#endif /* SERVER_MODE */

#if defined(SERVER_MODE)
/*
 * lockk_initialize_object_lock_entry_list - Initializes the object lock entry list
 *
 * return: error code
 *
 * Note:
 *     This function initializes following two lists.
 *     1. a list of object lock entry block
 *        => each node has object lock entry block.
 *     2. a list of freed object lock entries.
 */
static int
lock_initialize_object_lock_entry_list (void)
{
  int block_count, block_size, ret;

  /* initialize the entry freelist */
  block_count = 1;
  block_size = (int) MAX ((lk_Gl.max_obj_locks * LK_ENTRY_RATIO), 1);
  ret = lf_freelist_init (&lk_Gl.obj_free_entry_list, block_count, block_size, &obj_lock_entry_desc, &obj_lock_ent_Ts);
  if (ret != NO_ERROR)
    {
      return ER_FAILED;
    }

  return NO_ERROR;
}
#endif /* SERVER_MODE */

#if defined(SERVER_MODE)
/*
 * lock_initialize_deadlock_detection - Initializes transaction wait-for graph.
 *
 * return: error code
 *
 * Note:This function initializes the transaction waif-for graph.
 */
static int
lock_initialize_deadlock_detection (void)
{
  int i;

  pthread_mutex_init (&lk_Gl.DL_detection_mutex, NULL);
  gettimeofday (&lk_Gl.last_deadlock_run, NULL);

  /* allocate transaction WFG node table */
  lk_Gl.TWFG_node = (LK_WFG_NODE *) malloc (SIZEOF_LK_WFG_NODE * lk_Gl.num_trans);
  if (lk_Gl.TWFG_node == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      (size_t) (SIZEOF_LK_WFG_NODE * lk_Gl.num_trans));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  /* initialize transaction WFG node table */
  for (i = 0; i < lk_Gl.num_trans; i++)
    {
      lk_Gl.TWFG_node[i].DL_victim = false;
      lk_Gl.TWFG_node[i].checked_by_deadlock_detector = false;
      lk_Gl.TWFG_node[i].thrd_wait_stime = 0;
    }

  /* initialize other related fields */
  lk_Gl.TWFG_edge = NULL;
  lk_Gl.max_TWFG_edge = 0;
  lk_Gl.TWFG_free_edge_idx = -1;
  lk_Gl.global_edge_seq_num = 0;

  return NO_ERROR;
}
#endif /* SERVER_MODE */

#if defined(SERVER_MODE)
/*
 * lock_remove_resource - Remove lock resource entry
 *
 * return: error code
 *
 *   res_ptr(in):
 *
 * Note:This function removes the given lock resource entry
 *     from lock hash table.
 */
static int
lock_remove_resource (LK_RES * res_ptr)
{
  LF_TRAN_ENTRY *t_entry = thread_get_tran_entry (NULL, THREAD_TS_OBJ_LOCK_RES);
  int success = 0, rc;

  rc = lf_hash_delete_already_locked (t_entry, &lk_Gl.obj_hash_table, (void *) &res_ptr->key, res_ptr, &success);
  if (!success)
    {
      /* this should not happen, as the hash entry is mutex protected and no clear operations are performed on the hash 
       * table */
      pthread_mutex_unlock (&res_ptr->res_mutex);
      assert_release (false);
      return ER_FAILED;
    }
  else
    {
      assert (rc == NO_ERROR);
      return rc;
    }
}
#endif /* SERVER_MODE */

/*
 *  Private Functions Group: transaction lock list related functios
 *   - lk_insert_into_tran_hold_list()
 *   - lk_delete_from_tran_hold_list()
 *   - lk_insert_into_tran_non2pl_list()
 *   - lk_delete_from_tran_non2pl_list()
 */

#if defined(SERVER_MODE)
/*
 * lock_insert_into_tran_hold_list - Insert the given lock entry
 *                      into the transaction lock hold list
 *
 * return: nothing
 *
 *   entry_ptr(in):
 *
 * Note:This function inserts the given lock entry into the transaction lock
 *     hold list. The given lock entry was included in the lock holder
 *     list. That is, The lock is held by the transaction.
 */
static void
lock_insert_into_tran_hold_list (LK_ENTRY * entry_ptr, int owner_tran_index)
{
  LK_TRAN_LOCK *tran_lock;
  int rv;

  /* The caller is holding a resource mutex */

  if (owner_tran_index != entry_ptr->tran_index)
    {
      assert (owner_tran_index == entry_ptr->tran_index);
      return;
    }

  tran_lock = &lk_Gl.tran_lock_table[entry_ptr->tran_index];
  rv = pthread_mutex_lock (&tran_lock->hold_mutex);

  switch (entry_ptr->res_head->key.type)
    {
    case LOCK_RESOURCE_ROOT_CLASS:
#if defined(CUBRID_DEBUG)
      if (tran_lock->root_class_hold != NULL)
	{
	  fprintf (stderr, "lk_insert_into_tran_hold_list() error.. (1)\n");
	}
#endif /* CUBRID_DEBUG */
      entry_ptr->tran_next = tran_lock->root_class_hold;
      tran_lock->root_class_hold = entry_ptr;
      break;

    case LOCK_RESOURCE_CLASS:
#if defined(CUBRID_DEBUG)
      if (tran_lock->class_hold_list != NULL)
	{
	  LK_ENTRY *_ptr;
	  _ptr = tran_lock->class_hold_list;
	  while (_ptr != NULL)
	    {
	      if (_ptr->res_head == entry_ptr->res_head)
		{
		  break;
		}
	      _ptr = _ptr->tran_next;
	    }
	  if (_ptr != NULL)
	    {
	      fprintf (stderr, "lk_insert_into_tran_hold_list() error.. (2)\n");
	    }
	}
#endif /* CUBRID_DEBUG */
      if (tran_lock->class_hold_list != NULL)
	{
	  tran_lock->class_hold_list->tran_prev = entry_ptr;
	}
      entry_ptr->tran_next = tran_lock->class_hold_list;
      tran_lock->class_hold_list = entry_ptr;
      tran_lock->class_hold_count++;
      break;

    case LOCK_RESOURCE_INSTANCE:
#if defined(CUBRID_DEBUG)
      if (tran_lock->inst_hold_list != NULL)
	{
	  LK_ENTRY *_ptr;
	  _ptr = tran_lock->inst_hold_list;
	  while (_ptr != NULL)
	    {
	      if (_ptr->res_head == entry_ptr->res_head)
		{
		  break;
		}
	      _ptr = _ptr->tran_next;
	    }
	  if (_ptr != NULL)
	    {
	      fprintf (stderr, "lk_insert_into_tran_hold_list() error.. (3)\n");
	    }
	}
#endif /* CUBRID_DEBUG */
      if (tran_lock->inst_hold_list != NULL)
	{
	  tran_lock->inst_hold_list->tran_prev = entry_ptr;
	}
      entry_ptr->tran_next = tran_lock->inst_hold_list;
      tran_lock->inst_hold_list = entry_ptr;
      tran_lock->inst_hold_count++;
      break;

    default:
      break;
    }

  pthread_mutex_unlock (&tran_lock->hold_mutex);
}
#endif /* SERVER_MODE */

#if defined(SERVER_MODE)
/*
 * lock_delete_from_tran_hold_list - Delted the given lock entry
 *                      from the transaction lock hold list
 *
 * return: error code
 *
 *   entry_ptr(in):
 *
 * Note:This functions finds the given lock entry in the transaction
 *     lock hold list and then deletes it from the lock hold list.
 */
static int
lock_delete_from_tran_hold_list (LK_ENTRY * entry_ptr, int owner_tran_index)
{
  LK_TRAN_LOCK *tran_lock;
  int rv;
  int error_code = NO_ERROR;

  /* The caller is holding a resource mutex */

  if (owner_tran_index != entry_ptr->tran_index)
    {
      assert (owner_tran_index == entry_ptr->tran_index);
      return ER_FAILED;
    }

  tran_lock = &lk_Gl.tran_lock_table[entry_ptr->tran_index];
  rv = pthread_mutex_lock (&tran_lock->hold_mutex);

  switch (entry_ptr->res_head->key.type)
    {
    case LOCK_RESOURCE_ROOT_CLASS:
      if (entry_ptr != tran_lock->root_class_hold)
	{			/* does not exist */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LK_NOTFOUND_IN_TRAN_HOLD_LIST, 7,
		  LOCK_TO_LOCKMODE_STRING (entry_ptr->granted_mode), "ROOT CLASS", entry_ptr->res_head->key.oid.volid,
		  entry_ptr->res_head->key.oid.pageid, entry_ptr->res_head->key.oid.slotid, entry_ptr->tran_index,
		  (tran_lock->root_class_hold == NULL ? 0 : 1));
	  error_code = ER_LK_NOTFOUND_IN_TRAN_HOLD_LIST;
	}
      else
	{
	  tran_lock->root_class_hold = NULL;
	}
      break;

    case LOCK_RESOURCE_CLASS:
      if (tran_lock->class_hold_list == entry_ptr)
	{
	  tran_lock->class_hold_list = entry_ptr->tran_next;
	  if (entry_ptr->tran_next)
	    {
	      entry_ptr->tran_next->tran_prev = NULL;
	    }
	}
      else
	{
	  if (entry_ptr->tran_prev)
	    {
	      entry_ptr->tran_prev->tran_next = entry_ptr->tran_next;
	    }
	  if (entry_ptr->tran_next)
	    {
	      entry_ptr->tran_next->tran_prev = entry_ptr->tran_prev;
	    }
	}
      tran_lock->class_hold_count--;
      break;

    case LOCK_RESOURCE_INSTANCE:
      if (tran_lock->inst_hold_list == entry_ptr)
	{
	  tran_lock->inst_hold_list = entry_ptr->tran_next;
	  if (entry_ptr->tran_next)
	    {
	      entry_ptr->tran_next->tran_prev = NULL;
	    }
	}
      else
	{
	  if (entry_ptr->tran_prev)
	    {
	      entry_ptr->tran_prev->tran_next = entry_ptr->tran_next;
	    }
	  if (entry_ptr->tran_next)
	    {
	      entry_ptr->tran_next->tran_prev = entry_ptr->tran_prev;
	    }
	}
      tran_lock->inst_hold_count--;
      break;

    default:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LK_INVALID_OBJECT_TYPE, 4, entry_ptr->res_head->key.type,
	      entry_ptr->res_head->key.oid.volid, entry_ptr->res_head->key.oid.pageid,
	      entry_ptr->res_head->key.oid.slotid);
      error_code = ER_LK_INVALID_OBJECT_TYPE;
      break;
    }

  pthread_mutex_unlock (&tran_lock->hold_mutex);

  return error_code;
}
#endif /* SERVER_MODE */

#if defined(SERVER_MODE)
/*
 * lock_insert_into_tran_non2pl_list - Insert the given lock entry
 *                        into the transaction non2pl list
 *
 * return: nothing
 *
 *   non2pl(in):
 *
 * Note:This function inserts the given lock entry into the transaction
 *     non2pl list.
 */
static void
lock_insert_into_tran_non2pl_list (LK_ENTRY * non2pl, int owner_tran_index)
{
  LK_TRAN_LOCK *tran_lock;
  int rv;

  /* The caller is holding a resource mutex */

  if (owner_tran_index != non2pl->tran_index)
    {
      assert (owner_tran_index == non2pl->tran_index);
      return;
    }

  tran_lock = &lk_Gl.tran_lock_table[non2pl->tran_index];
  rv = pthread_mutex_lock (&tran_lock->non2pl_mutex);

  non2pl->tran_next = tran_lock->non2pl_list;
  tran_lock->non2pl_list = non2pl;
  if (non2pl->granted_mode == INCON_NON_TWO_PHASE_LOCK)
    {
      tran_lock->num_incons_non2pl += 1;
    }

  pthread_mutex_unlock (&tran_lock->non2pl_mutex);
}
#endif /* SERVER_MODE */

#if defined(SERVER_MODE)
/*
 * lock_delete_from_tran_non2pl_list - Delete the given lock entry
 *                        from the transaction non2pl list
 *
 * return: error code
 *
 *   non2pl(in):
 *
 * Note:This function finds the given lock entry in the transaction
 *     non2pl list and then deletes it from the non2pl list.
 */
static int
lock_delete_from_tran_non2pl_list (LK_ENTRY * non2pl, int owner_tran_index)
{
  LK_TRAN_LOCK *tran_lock;
  LK_ENTRY *prev, *curr;
  int rv;
  int error_code = NO_ERROR;

  /* The caller is holding a resource mutex */

  if (owner_tran_index != non2pl->tran_index)
    {
      assert (owner_tran_index == non2pl->tran_index);
      return ER_FAILED;
    }

  tran_lock = &lk_Gl.tran_lock_table[non2pl->tran_index];
  rv = pthread_mutex_lock (&tran_lock->non2pl_mutex);

  /* find the given non2pl entry in transaction non2pl list */
  prev = NULL;
  curr = tran_lock->non2pl_list;
  while (curr != NULL && curr != non2pl)
    {
      prev = curr;
      curr = curr->tran_next;
    }
  if (curr == NULL)
    {				/* not found */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LK_NOTFOUND_IN_TRAN_NON2PL_LIST, 5,
	      LOCK_TO_LOCKMODE_STRING (non2pl->granted_mode),
	      (non2pl->res_head != NULL ? non2pl->res_head->key.oid.volid : -2),
	      (non2pl->res_head != NULL ? non2pl->res_head->key.oid.pageid : -2),
	      (non2pl->res_head != NULL ? non2pl->res_head->key.oid.slotid : -2), non2pl->tran_index);
      error_code = ER_LK_NOTFOUND_IN_TRAN_NON2PL_LIST;
    }
  else
    {				/* found */
      /* delete it from the transaction non2pl list */
      if (prev == NULL)
	{
	  tran_lock->non2pl_list = curr->tran_next;
	}
      else
	{
	  prev->tran_next = curr->tran_next;
	}

      if (curr->granted_mode == INCON_NON_TWO_PHASE_LOCK)
	{
	  tran_lock->num_incons_non2pl -= 1;
	}
    }
  pthread_mutex_unlock (&tran_lock->non2pl_mutex);

  return error_code;
}
#endif /* SERVER_MODE */

/*
 *  Private Functions Group: lock entry addition related functions
 *   - lk_add_non2pl_lock()
 *   - lk_position_holder_entry()
 */

#if defined(SERVER_MODE)
/*
 * lock_find_class_entry - Find a class lock entry
 *                           in the transaction lock hold list
 *
 * return:
 *
 *   tran_index(in):
 *   class_oid(in):
 *
 * Note:This function finds a class lock entry, whose lock object id
 *     is the given class_oid, in the transaction lock hold list.
 */
static LK_ENTRY *
lock_find_class_entry (int tran_index, const OID * class_oid)
{
  LK_TRAN_LOCK *tran_lock;
  LK_ENTRY *entry_ptr;
  int rv;

  /* The caller is not holding any mutex */

  tran_lock = &lk_Gl.tran_lock_table[tran_index];
  rv = pthread_mutex_lock (&tran_lock->hold_mutex);

  if (OID_IS_ROOTOID (class_oid))
    {
      entry_ptr = tran_lock->root_class_hold;
    }
  else
    {
      entry_ptr = tran_lock->class_hold_list;
      while (entry_ptr != NULL)
	{
	  assert (tran_index == entry_ptr->tran_index);

	  if (OID_EQ (&entry_ptr->res_head->key.oid, class_oid))
	    {
	      break;
	    }
	  entry_ptr = entry_ptr->tran_next;
	}
    }

  pthread_mutex_unlock (&tran_lock->hold_mutex);

  return entry_ptr;		/* it might be NULL */
}
#endif /* SERVER_MODE */

#if defined(SERVER_MODE)
/*
 * lock_add_non2pl_lock - Add a release lock which has never been acquired
 *
 * return: pointer to the lock entry in non2pl list.
 *
 *   res_ptr(in): pointer to lock resource
 *   tran_index(in): transaction table index
 *   lock(in): the lock mode of non2pl lock
 *
 * Note:Cache a release lock (which has never been acquired) onto the list
 *     of non two phase lock to detect future serializable inconsistencies
 *
 */
static LK_ENTRY *
lock_add_non2pl_lock (THREAD_ENTRY * thread_p, LK_RES * res_ptr, int tran_index, LOCK lock)
{
  LF_TRAN_ENTRY *t_entry = thread_get_tran_entry (thread_p, THREAD_TS_OBJ_LOCK_ENT);
  LK_ENTRY *non2pl;
  LK_TRAN_LOCK *tran_lock;
  int rv;
  int compat;

  assert (!OID_ISNULL (&res_ptr->key.oid));

  /* The caller is holding a resource mutex */

  /* find the non2pl entry of the given transaction */
  non2pl = res_ptr->non2pl;
  while (non2pl != NULL)
    {
      if (non2pl->tran_index == tran_index)
	{
	  break;
	}
      non2pl = non2pl->next;
    }

  if (non2pl != NULL)
    {
      /* 1. I have a non2pl entry on the lock resource */
      /* reflect the current lock acquisition into the non2pl entry */
      tran_lock = &lk_Gl.tran_lock_table[tran_index];
      rv = pthread_mutex_lock (&tran_lock->non2pl_mutex);

      if (non2pl->granted_mode != INCON_NON_TWO_PHASE_LOCK)
	{
	  if (lock == INCON_NON_TWO_PHASE_LOCK)
	    {
	      non2pl->granted_mode = INCON_NON_TWO_PHASE_LOCK;
	      tran_lock->num_incons_non2pl += 1;
	    }
	  else
	    {
	      assert (lock >= NULL_LOCK && non2pl->granted_mode >= NULL_LOCK);
	      compat = lock_Comp[lock][non2pl->granted_mode];
	      assert (compat != DB_NA);

	      if (compat == false)
		{
		  non2pl->granted_mode = INCON_NON_TWO_PHASE_LOCK;
		  tran_lock->num_incons_non2pl += 1;
		}
	      else
		{
		  non2pl->granted_mode = lock_Conv[lock][non2pl->granted_mode];
		  assert (non2pl->granted_mode != NA_LOCK);
		}
	    }
	}

      pthread_mutex_unlock (&tran_lock->non2pl_mutex);
    }
  else
    {				/* non2pl == (LK_ENTRY *)NULL */
      /* 2. I do not have a non2pl entry on the lock resource */
      /* allocate a lock entry, initialize it, and connect it */
      non2pl = lock_get_new_entry (tran_index, t_entry, &lk_Gl.obj_free_entry_list);
      if (non2pl != NULL)
	{
	  lock_initialize_entry_as_non2pl (non2pl, tran_index, res_ptr, lock);
	  non2pl->next = res_ptr->non2pl;
	  res_ptr->non2pl = non2pl;
	  lock_insert_into_tran_non2pl_list (non2pl, tran_index);
	}
      else
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LK_ALLOC_RESOURCE, 1, "lock heap entry");
	}
    }
  return non2pl;		/* it might be NULL */
}
#endif /* SERVER_MODE */

#if defined(SERVER_MODE)
/*
 * lock_position_holder_entry - Position given lock entry in the lock
 *                                holder list of given lock resource
 *
 * return:
 *
 *   res_ptr(in):
 *   entry_ptr(in):
 *
 * Note:This function positions the given lock entry
 *     in the lock holder list of the given lock resource
 *     according to Upgrader Positioning Rule(UPR).
 *
 *     NOTE that the granted_mode and blocked_mode of the given lock
 *          entry must be set before this function is called.
 */
static void
lock_position_holder_entry (LK_RES * res_ptr, LK_ENTRY * entry_ptr)
{
  LK_ENTRY *prev, *i;
  LK_ENTRY *ta, *tap;
  LK_ENTRY *tb, *tbp;
  LK_ENTRY *tc, *tcp;
  int compat1, compat2;

  /* find the position where the lock entry to be inserted */
  if (entry_ptr->blocked_mode == NULL_LOCK)
    {
      /* case 1: when block_mode is NULL_LOCK */
      prev = NULL;
      i = res_ptr->holder;
      while (i != NULL)
	{
	  if (i->blocked_mode == NULL_LOCK)
	    {
	      break;
	    }
	  prev = i;
	  i = i->next;
	}
    }
  else
    {
      /* case 2: when block_mode is not NULL_LOCK */
      /* find ta, tb, tc among other holders */
      ta = tb = tc = NULL;
      tap = tbp = tcp = NULL;

      prev = NULL;
      i = res_ptr->holder;
      while (i != NULL)
	{
	  if (i->blocked_mode != NULL_LOCK)
	    {
	      assert (entry_ptr->blocked_mode >= NULL_LOCK && entry_ptr->granted_mode >= NULL_LOCK);
	      assert (i->blocked_mode >= NULL_LOCK && i->granted_mode >= NULL_LOCK);

	      compat1 = lock_Comp[entry_ptr->blocked_mode][i->blocked_mode];
	      assert (compat1 != DB_NA);

	      if (ta == NULL && compat1 == true)
		{
		  ta = i;
		  tap = prev;
		}

	      compat1 = lock_Comp[entry_ptr->blocked_mode][i->granted_mode];
	      assert (compat1 != DB_NA);

	      compat2 = lock_Comp[i->blocked_mode][entry_ptr->granted_mode];
	      assert (compat2 != DB_NA);

	      if (ta == NULL && tb == NULL && compat1 == true && compat2 == false)
		{
		  tb = i;
		  tbp = prev;
		}
	    }
	  else
	    {
	      if (tc == NULL)
		{
		  tc = i;
		  tcp = prev;
		}
	    }
	  prev = i;
	  i = i->next;
	}
      if (ta != NULL)
	{
	  prev = tap;
	}
      else if (tb != NULL)
	{
	  prev = tbp;
	}
      else if (tc != NULL)
	{
	  prev = tcp;
	}
    }

  /* insert the given lock entry into the found position */
  if (prev == NULL)
    {
      entry_ptr->next = res_ptr->holder;
      res_ptr->holder = entry_ptr;
    }
  else
    {
      entry_ptr->next = prev->next;
      prev->next = entry_ptr;
    }
}
#endif /* SERVER_MODE */


/*
 *  Private Functions Group: timeout related functions
 *
 *   - lock_set_error_for_timeout()
 *   - lock_set_error_for_aborted()
 *   - lock_suspend(), lock_resume()
 *   - lock_wakeup_deadlock_victim_timeout()
 *   - lock_wakeup_deadlock_victim_aborted()
 */

#if defined(SERVER_MODE)
/*
 * lock_set_error_for_timeout - Set error for lock timeout
 *
 * return:
 *
 *   entry_ptr(in): pointer to the lock entry for waiting
 *
 * Note:Set error code for lock timeout
 */
static void
lock_set_error_for_timeout (THREAD_ENTRY * thread_p, LK_ENTRY * entry_ptr)
{
  char *client_prog_name;	/* Client program name for transaction */
  char *client_user_name;	/* Client user name for transaction */
  char *client_host_name;	/* Client host for transaction */
  int client_pid;		/* Client process id for transaction */
  char *waitfor_client_users_default = (char *) "";
  char *waitfor_client_users;	/* Waitfor users */
  char *classname;		/* Name of the class */
  int n, i, nwaits, max_waits = DEFAULT_WAIT_USERS;
  int wait_for_buf[DEFAULT_WAIT_USERS];
  int *wait_for = wait_for_buf, *t;
  LK_ENTRY *entry;
  LK_RES *res_ptr = NULL;
  int unit_size = LOG_USERNAME_MAX + MAXHOSTNAMELEN + PATH_MAX + 20 + 4;
  char *ptr;
  int rv;
  bool is_classname_alloced = false;
  bool free_mutex_flag = false;
  bool isdeadlock_timeout = false;
  int compat1, compat2;
  OID *oid_rr;

  /* Find the users that transaction is waiting for */
  waitfor_client_users = waitfor_client_users_default;
  nwaits = 0;

  assert (entry_ptr->granted_mode >= NULL_LOCK && entry_ptr->blocked_mode >= NULL_LOCK);

  /* Dump all the tran. info. which this tran. is waiting for */
  res_ptr = entry_ptr->res_head;
  wait_for[0] = NULL_TRAN_INDEX;

  rv = pthread_mutex_lock (&res_ptr->res_mutex);
  free_mutex_flag = true;
  for (entry = res_ptr->holder; entry != NULL; entry = entry->next)
    {
      if (entry == entry_ptr)
	{
	  continue;
	}

      assert (entry->granted_mode >= NULL_LOCK && entry->blocked_mode >= NULL_LOCK);
      compat1 = lock_Comp[entry->granted_mode][entry_ptr->blocked_mode];
      compat2 = lock_Comp[entry->blocked_mode][entry_ptr->blocked_mode];
      assert (compat1 != DB_NA && compat2 != DB_NA);

      if (compat1 == false || compat2 == false)
	{
	  EXPAND_WAIT_FOR_ARRAY_IF_NEEDED ();
	  wait_for[nwaits++] = entry->tran_index;
	}
    }

  for (entry = res_ptr->waiter; entry != NULL; entry = entry->next)
    {
      if (entry == entry_ptr)
	{
	  continue;
	}

      assert (entry->granted_mode >= NULL_LOCK && entry->blocked_mode >= NULL_LOCK);
      compat1 = lock_Comp[entry->blocked_mode][entry_ptr->blocked_mode];
      assert (compat1 != DB_NA);

      if (compat1 == false)
	{
	  EXPAND_WAIT_FOR_ARRAY_IF_NEEDED ();
	  wait_for[nwaits++] = entry->tran_index;
	}
    }

  pthread_mutex_unlock (&res_ptr->res_mutex);
  free_mutex_flag = false;

  if (nwaits == 0 || (waitfor_client_users = (char *) malloc (unit_size * nwaits)) == NULL)
    {
      waitfor_client_users = waitfor_client_users_default;
    }
  else
    {
      for (ptr = waitfor_client_users, i = 0; i < nwaits; i++)
	{
	  (void) logtb_find_client_name_host_pid (wait_for[i], &client_prog_name, &client_user_name, &client_host_name,
						  &client_pid);
	  n =
	    sprintf (ptr, "%s%s@%s|%s(%d)", ((i == 0) ? "" : ", "), client_user_name, client_host_name,
		     client_prog_name, client_pid);
	  ptr += n;
	}
    }

set_error:

  if (wait_for != wait_for_buf)
    {
      free_and_init (wait_for);
    }

  if (free_mutex_flag)
    {
      pthread_mutex_unlock (&res_ptr->res_mutex);
      free_mutex_flag = false;
    }

  /* get the client information of current transaction */
  (void) logtb_find_client_name_host_pid (entry_ptr->tran_index, &client_prog_name, &client_user_name,
					  &client_host_name, &client_pid);

  if (entry_ptr->thrd_entry != NULL
      && ((entry_ptr->thrd_entry->lockwait_state == LOCK_RESUMED_DEADLOCK_TIMEOUT)
	  || (entry_ptr->thrd_entry->lockwait_state == LOCK_RESUMED_ABORTED_OTHER)))
    {
      isdeadlock_timeout = true;
    }

  switch (entry_ptr->res_head->key.type)
    {
    case LOCK_RESOURCE_ROOT_CLASS:
    case LOCK_RESOURCE_CLASS:
      oid_rr = oid_get_rep_read_tran_oid ();
      if (oid_rr != NULL && OID_EQ (&entry_ptr->res_head->key.oid, oid_rr))
	{
	  classname = (char *) "Generic object for Repeatable Read consistency";
	  is_classname_alloced = false;
	}
      else if (OID_ISTEMP (&entry_ptr->res_head->key.oid))
	{
	  classname = NULL;
	}
      else
	{
	  OID real_class_oid;

	  if (entry_ptr->res_head->key.type == LOCK_RESOURCE_CLASS
	      && OID_IS_VIRTUAL_CLASS_OF_DIR_OID (&entry_ptr->res_head->key.oid))
	    {
	      OID_GET_REAL_CLASS_OF_DIR_OID (&entry_ptr->res_head->key.oid, &real_class_oid);
	    }
	  else
	    {
	      COPY_OID (&real_class_oid, &entry_ptr->res_head->key.oid);
	    }
	  if (heap_get_class_name (thread_p, &real_class_oid, &classname) != NO_ERROR)
	    {
	      /* ignore */
	      er_clear ();
	    }
	  else if (classname != NULL)
	    {
	      is_classname_alloced = true;
	    }
	}

      if (classname != NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ((isdeadlock_timeout) ? ER_LK_OBJECT_DL_TIMEOUT_CLASS_MSG : ER_LK_OBJECT_TIMEOUT_CLASS_MSG), 7,
		  entry_ptr->tran_index, client_user_name, client_host_name, client_pid,
		  LOCK_TO_LOCKMODE_STRING (entry_ptr->blocked_mode), classname, waitfor_client_users);
	  if (is_classname_alloced)
	    {
	      free_and_init (classname);
	    }
	}
      else
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ((isdeadlock_timeout) ? ER_LK_OBJECT_DL_TIMEOUT_SIMPLE_MSG : ER_LK_OBJECT_TIMEOUT_SIMPLE_MSG), 9,
		  entry_ptr->tran_index, client_user_name, client_host_name, client_pid,
		  LOCK_TO_LOCKMODE_STRING (entry_ptr->blocked_mode), entry_ptr->res_head->key.oid.volid,
		  entry_ptr->res_head->key.oid.pageid, entry_ptr->res_head->key.oid.slotid, waitfor_client_users);
	}
      break;

    case LOCK_RESOURCE_INSTANCE:
      if (OID_ISTEMP (&entry_ptr->res_head->key.class_oid))
	{
	  classname = NULL;
	}
      else
	{
	  OID real_class_oid;
	  if (OID_IS_VIRTUAL_CLASS_OF_DIR_OID (&entry_ptr->res_head->key.class_oid))
	    {
	      OID_GET_REAL_CLASS_OF_DIR_OID (&entry_ptr->res_head->key.class_oid, &real_class_oid);
	    }
	  else
	    {
	      COPY_OID (&real_class_oid, &entry_ptr->res_head->key.class_oid);
	    }
	  if (heap_get_class_name (thread_p, &real_class_oid, &classname) != NO_ERROR)
	    {
	      /* ignore */
	      er_clear ();
	    }
	}

      if (classname != NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ((isdeadlock_timeout) ? ER_LK_OBJECT_DL_TIMEOUT_CLASSOF_MSG : ER_LK_OBJECT_TIMEOUT_CLASSOF_MSG), 10,
		  entry_ptr->tran_index, client_user_name, client_host_name, client_pid,
		  LOCK_TO_LOCKMODE_STRING (entry_ptr->blocked_mode), entry_ptr->res_head->key.oid.volid,
		  entry_ptr->res_head->key.oid.pageid, entry_ptr->res_head->key.oid.slotid, classname,
		  waitfor_client_users);
	  free_and_init (classname);
	}
      else
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ((isdeadlock_timeout) ? ER_LK_OBJECT_DL_TIMEOUT_SIMPLE_MSG : ER_LK_OBJECT_TIMEOUT_SIMPLE_MSG), 9,
		  entry_ptr->tran_index, client_user_name, client_host_name, client_pid,
		  LOCK_TO_LOCKMODE_STRING (entry_ptr->blocked_mode), entry_ptr->res_head->key.oid.volid,
		  entry_ptr->res_head->key.oid.pageid, entry_ptr->res_head->key.oid.slotid, waitfor_client_users);
	}
      break;
    default:
      break;
    }

  if (waitfor_client_users && waitfor_client_users != waitfor_client_users_default)
    {
      free_and_init (waitfor_client_users);
    }

  if (isdeadlock_timeout == false)
    {
      FILE *log_fp;

      log_fp = event_log_start (thread_p, "LOCK_TIMEOUT");
      if (log_fp == NULL)
	{
	  return;
	}

      lock_event_log_blocked_lock (thread_p, log_fp, entry_ptr);
      lock_event_log_blocking_locks (thread_p, log_fp, entry_ptr);

      event_log_end (thread_p);
    }
}
#endif /* SERVER_MODE */

#if defined(SERVER_MODE)
/*
 * lock_set_error_for_aborted - Set error for unilaterally aborted
 *
 * return:
 *
 *   entry_ptr(in): pointer to the entry for waiting
 *
 * Note:set error code for unilaterally aborted deadlock victim
 */
static void
lock_set_error_for_aborted (LK_ENTRY * entry_ptr, TRAN_ABORT_REASON abort_reason)
{
  char *client_prog_name;	/* Client user name for transaction */
  char *client_user_name;	/* Client user name for transaction */
  char *client_host_name;	/* Client host for transaction */
  int client_pid;		/* Client process id for transaction */
  LOG_TDES *tdes;

  (void) logtb_find_client_name_host_pid (entry_ptr->tran_index, &client_prog_name, &client_user_name,
					  &client_host_name, &client_pid);
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LK_UNILATERALLY_ABORTED, 4, entry_ptr->tran_index, client_user_name,
	  client_host_name, client_pid);

  tdes = LOG_FIND_TDES (entry_ptr->tran_index);
  assert (tdes != NULL);
  tdes->tran_abort_reason = abort_reason;
}
#endif /* SERVER_MODE */

#if defined(SERVER_MODE)
/*
 * lock_suspend - Suspend current thread (transaction)
 *
 * return: LOCK_WAIT_STATE (state of resumption)
 *
 *   entry_ptr(in): lock entry for lock waiting
 *   wait_msecs(in): lock wait milliseconds
 */
static LOCK_WAIT_STATE
lock_suspend (THREAD_ENTRY * thread_p, LK_ENTRY * entry_ptr, int wait_msecs)
{
  THREAD_ENTRY *p;
  struct timeval tv;
  int client_id;
  LOG_TDES *tdes;

  /* The threads must not hold a page latch to be blocked on a lock request. */
  assert (lock_is_safe_lock_with_page (thread_p, entry_ptr) || !pgbuf_has_perm_pages_fixed (thread_p));

  /* The caller is holding the thread entry mutex */

  if (lk_Gl.verbose_mode == true)
    {
      char *__client_prog_name;	/* Client program name for transaction */
      char *__client_user_name;	/* Client user name for transaction */
      char *__client_host_name;	/* Client host for transaction */
      int __client_pid;		/* Client process id for transaction */

      fflush (stderr);
      fflush (stdout);
      logtb_find_client_name_host_pid (entry_ptr->tran_index, &__client_prog_name, &__client_user_name,
				       &__client_host_name, &__client_pid);
      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOCK, MSGCAT_LK_SUSPEND_TRAN),
	       entry_ptr->thrd_entry->index, entry_ptr->tran_index, __client_prog_name, __client_user_name,
	       __client_host_name, __client_pid);
      fflush (stdout);
    }

  /* register lock wait info. into the thread entry */
  entry_ptr->thrd_entry->lockwait = (void *) entry_ptr;
  gettimeofday (&tv, NULL);
  entry_ptr->thrd_entry->lockwait_stime = (tv.tv_sec * 1000000LL + tv.tv_usec) / 1000LL;
  entry_ptr->thrd_entry->lockwait_msecs = wait_msecs;
  entry_ptr->thrd_entry->lockwait_state = (int) LOCK_SUSPENDED;

  lk_Gl.TWFG_node[entry_ptr->tran_index].thrd_wait_stime = entry_ptr->thrd_entry->lockwait_stime;

  /* wakeup the dealock detect thread */
  thread_wakeup_deadlock_detect_thread ();

  tdes = LOG_FIND_CURRENT_TDES (thread_p);

  /* I must not be a deadlock-victim thread */
  assert (tdes->tran_abort_reason == TRAN_NORMAL);

  if (tdes)
    {
      tdes->waiting_for_res = entry_ptr->res_head;
    }

  lock_event_set_tran_wait_entry (entry_ptr->tran_index, entry_ptr);

  /* suspend the worker thread (transaction) */
  thread_suspend_wakeup_and_unlock_entry (entry_ptr->thrd_entry, THREAD_LOCK_SUSPENDED);

  lk_Gl.TWFG_node[entry_ptr->tran_index].thrd_wait_stime = 0;

  if (tdes)
    {
      tdes->waiting_for_res = NULL;
    }

  lock_event_set_tran_wait_entry (entry_ptr->tran_index, NULL);

  if (entry_ptr->thrd_entry->resume_status == THREAD_RESUME_DUE_TO_INTERRUPT)
    {
      /* a shutdown thread wakes me up */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERRUPTED, 0);
      return LOCK_RESUMED_INTERRUPT;
    }
  else if (entry_ptr->thrd_entry->resume_status != THREAD_LOCK_RESUMED)
    {
      /* wake up with other reason */
      assert (0);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERRUPTED, 0);
      return LOCK_RESUMED_INTERRUPT;
    }
  else
    {
      assert (entry_ptr->thrd_entry->resume_status == THREAD_LOCK_RESUMED);
    }

  thread_lock_entry (entry_ptr->thrd_entry);
  while (entry_ptr->thrd_entry->tran_next_wait)
    {
      p = entry_ptr->thrd_entry->tran_next_wait;
      entry_ptr->thrd_entry->tran_next_wait = p->tran_next_wait;
      p->tran_next_wait = NULL;
      thread_wakeup (p, THREAD_LOCK_RESUMED);
    }
  thread_unlock_entry (entry_ptr->thrd_entry);

  /* The thread has been awaken Before waking up the thread, the waker cleared the lockwait field of the thread entry
   * and set lockwait_state field of it to the resumed state while holding the thread entry mutex. After the wakeup, no 
   * one can update the lockwait releated fields of the thread entry. Therefore, waken-up thread can read the values of 
   * lockwait related fields of its own thread entry without holding thread entry mutex. */

  switch ((LOCK_WAIT_STATE) (entry_ptr->thrd_entry->lockwait_state))
    {
    case LOCK_RESUMED:
      /* The lock entry has already been moved to the holder list */
      return LOCK_RESUMED;

    case LOCK_RESUMED_ABORTED_FIRST:
      /* The lock entry does exist within the blocked holder list or blocked waiter list. Therefore, current thread
       * must disconnect it from the list. */
      if (logtb_is_current_active (thread_p) == true)
	{
	  /* set error code */
	  lock_set_error_for_aborted (entry_ptr, TRAN_ABORT_DUE_DEADLOCK);

	  /* wait until other threads finish their works A css_server_thread is always running for this transaction.
	   * so, wait until thread_has_threads() becomes 1 (except me) */
	  if (thread_has_threads (thread_p, entry_ptr->tran_index, thread_get_client_id (thread_p)) >= 1)
	    {
	      logtb_set_tran_index_interrupt (thread_p, entry_ptr->tran_index, true);
	      while (1)
		{
		  thread_sleep (10);	/* sleep 10 msec */
		  thread_wakeup_with_tran_index (entry_ptr->tran_index, THREAD_RESUME_DUE_TO_INTERRUPT);

		  client_id = thread_get_client_id (thread_p);
		  if (thread_has_threads (thread_p, entry_ptr->tran_index, client_id) == 0)
		    {
		      break;
		    }
		}
	      logtb_set_tran_index_interrupt (thread_p, entry_ptr->tran_index, false);
	    }
	}
      else
	{
	  /* We are already aborting, fall through. Don't do double aborts that could cause an infinite loop. */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LK_ABORT_TRAN_TWICE, 1, entry_ptr->tran_index);
	  /* er_log_debug(ARG_FILE_LINE, "lk_suspend: Likely a system error. Trying to abort a transaction
	   * twice.\n"); */

	  /* Since we deadlocked during an abort, forcibly remove all page latches of this transaction and hope this
	   * transaction is the cause of the logjam. We are hoping that this frees things just enough to let other
	   * transactions continue. Note it is not be safe to unlock object locks this way. */
	  pgbuf_unfix_all (thread_p);
	}
      return LOCK_RESUMED_ABORTED;

    case LOCK_RESUMED_ABORTED_OTHER:
      /* The lock entry does exist within the blocked holder list or blocked waiter list. Therefore, current thread
       * must diconnect it from the list. */
      /* If two or more threads, which were executing for one transaction, are selected as deadlock victims, one of
       * them is charged of the transaction abortion and the other threads are notified of timeout. */
      (void) lock_set_error_for_timeout (thread_p, entry_ptr);
      return LOCK_RESUMED_DEADLOCK_TIMEOUT;

    case LOCK_RESUMED_DEADLOCK_TIMEOUT:
      (void) lock_set_error_for_timeout (thread_p, entry_ptr);
      return LOCK_RESUMED_DEADLOCK_TIMEOUT;

    case LOCK_RESUMED_TIMEOUT:
      /* The lock entry does exist within the blocked holder list or blocked waiter list. Therefore, current thread
       * must diconnect it from the list. An error is ONLY set when the caller was willing to wait.
       * entry_ptr->thrd_entry->lockwait_msecs > 0 */
      (void) lock_set_error_for_timeout (thread_p, entry_ptr);
      return LOCK_RESUMED_TIMEOUT;

    case LOCK_RESUMED_INTERRUPT:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERRUPTED, 0);
      return LOCK_RESUMED_INTERRUPT;

    case LOCK_SUSPENDED:
    default:
      /* Probabely, the waiting structure has not been removed from the waiting hash table. May be a system error. */
      (void) lock_set_error_for_timeout (thread_p, entry_ptr);
      return LOCK_RESUMED_TIMEOUT;
    }
}
#endif /* SERVER_MODE */

#if defined(SERVER_MODE)
/*
 * lk_resume - Resume the thread (transaction)
 *
 * return:
 *
 *   entry_ptr(in):
 *   state(in): resume state
 */
static void
lock_resume (LK_ENTRY * entry_ptr, int state)
{
  /* The caller is holding the thread entry mutex */
  /* The caller has identified the fact that lockwait is not NULL. that is, the thread is suspended. */
  if (lk_Gl.verbose_mode == true)
    {
      char *__client_prog_name;	/* Client program name for transaction */
      char *__client_user_name;	/* Client user name for transaction */
      char *__client_host_name;	/* Client host for transaction */
      int __client_pid;		/* Client process id for transaction */

      fflush (stderr);
      fflush (stdout);
      (void) logtb_find_client_name_host_pid (entry_ptr->tran_index, &__client_prog_name, &__client_user_name,
					      &__client_host_name, &__client_pid);
      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOCK, MSGCAT_LK_RESUME_TRAN),
	       entry_ptr->tran_index, entry_ptr->tran_index, __client_prog_name, __client_user_name, __client_host_name,
	       __client_pid);
      fflush (stdout);
    }

  /* Before wake up the thread, clears lockwait field and set lockwait_state with the given state. */
  entry_ptr->thrd_entry->lockwait = NULL;
  entry_ptr->thrd_entry->lockwait_state = (int) state;

  /* wakes up the thread and release the thread entry mutex */
  entry_ptr->thrd_entry->resume_status = THREAD_LOCK_RESUMED;
  pthread_cond_signal (&entry_ptr->thrd_entry->wakeup_cond);
  thread_unlock_entry (entry_ptr->thrd_entry);
}
#endif /* SERVER_MODE */

#if defined(SERVER_MODE)
/*
 * lock_wakeup_deadlock_victim_timeout - Wake up the deadlock victim while notifying timeout
 *
 * return: true  if the transaction is treated as deadlock victim or
 *     false if the transaction is not treated as deadlock victim.
 *              in this case, the transaction has already been waken up
 *              by other threads with other purposes(ex. lock is granted)
 *
 *   tran_index(in): deadlock victim transaction
 *
 * Note:The given transaction was selected as a deadlock victim in the last
 *     deadlock detection. The deadlock victim is waked up and noitified of
 *     timeout by this function if the deadlock victim is still suspended.
 */
static bool
lock_wakeup_deadlock_victim_timeout (int tran_index)
{
  THREAD_ENTRY *thrd_array[10];
  int thrd_count, i;
  THREAD_ENTRY *thrd_ptr;
  bool wakeup_first = false;

  thrd_count = thread_get_lockwait_entry (tran_index, &thrd_array[0]);
  for (i = 0; i < thrd_count; i++)
    {
      thrd_ptr = thrd_array[i];
      (void) thread_lock_entry (thrd_ptr);
      if (thrd_ptr->tran_index == tran_index && LK_IS_LOCKWAIT_THREAD (thrd_ptr))
	{
	  /* wake up the thread while notifying timeout */
	  lock_resume ((LK_ENTRY *) thrd_ptr->lockwait, LOCK_RESUMED_DEADLOCK_TIMEOUT);
	  wakeup_first = true;
	}
      else
	{
	  if (thrd_ptr->lockwait != NULL || thrd_ptr->lockwait_state == (int) LOCK_SUSPENDED)
	    {
	      /* some strange lock wait state.. */
	      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_LK_STRANGE_LOCK_WAIT, 5, thrd_ptr->lockwait,
		      thrd_ptr->lockwait_state, thrd_ptr->index, thrd_ptr->tid, thrd_ptr->tran_index);
	    }
	  /* The current thread has already been waken up by other threads. The current thread might be granted the
	   * lock. or with any other reason....... even if it is a thread of the deadlock victim. */
	  /* release the thread entry mutex */
	  (void) thread_unlock_entry (thrd_ptr);
	}
    }
  return wakeup_first;
}
#endif /* SERVER_MODE */

#if defined(SERVER_MODE)
/*
 * lock_wakeup_deadlock_victim_aborted - Wake up the deadlock victim while notifying aborted
 *
 * return: true  if the transaction is treated as deadlock victim or
 *     false if the transaction is not treated as deadlock victim.
 *              in this case, the transaction has already been waken up
 *              by other threads with other purposes(ex. lock is granted)
 *
 *   tran_index(in): deadlock victim transaction
 *
 * Note:The given transaction was selected as a deadlock victim in the last
 *     deadlock detection. The deadlock victim is waked up and noitified of
 *     abortion by this function if the deadlock victim is still suspended.
 */
static bool
lock_wakeup_deadlock_victim_aborted (int tran_index)
{
  THREAD_ENTRY *thrd_array[10];
  int thrd_count, i;
  THREAD_ENTRY *thrd_ptr;
  bool wakeup_first = false;

  thrd_count = thread_get_lockwait_entry (tran_index, &thrd_array[0]);
  for (i = 0; i < thrd_count; i++)
    {
      thrd_ptr = thrd_array[i];
      (void) thread_lock_entry (thrd_ptr);
      if (thrd_ptr->tran_index == tran_index && LK_IS_LOCKWAIT_THREAD (thrd_ptr))
	{
	  /* wake up the thread while notifying deadlock victim */
	  if (wakeup_first == false)
	    {
	      /* The current transaction is really aborted. Therefore, other threads of the current transaction must
	       * quit their executions and return to client. Then the first waken-up thread must be charge of the
	       * rollback of the current transaction. */
	      /* set the transaction as deadlock victim */
	      lk_Gl.TWFG_node[tran_index].DL_victim = true;
	      lock_resume ((LK_ENTRY *) thrd_ptr->lockwait, LOCK_RESUMED_ABORTED_FIRST);
	      wakeup_first = true;
	    }
	  else
	    {
	      lock_resume ((LK_ENTRY *) thrd_ptr->lockwait, LOCK_RESUMED_ABORTED_OTHER);
	    }
	}
      else
	{
	  if (thrd_ptr->lockwait != NULL || thrd_ptr->lockwait_state == (int) LOCK_SUSPENDED)
	    {
	      /* some strange lock wait state.. */
	      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_LK_STRANGE_LOCK_WAIT, 5, thrd_ptr->lockwait,
		      thrd_ptr->lockwait_state, thrd_ptr->index, thrd_ptr->tid, thrd_ptr->tran_index);
	    }
	  /* The current thread has already been waken up by other threads. The current thread might have held the
	   * lock. or with any other reason....... even if it is a thread of the deadlock victim. */
	  /* release the thread entry mutex */
	  (void) thread_unlock_entry (thrd_ptr);
	}
    }
  return wakeup_first;
}
#endif /* SERVER_MODE */


/*
 *  Private Functions Group: grant lock requests of blocked threads
 *   - lock_grant_blocked_holder()
 *   - lock_grant_blocked_waiter()
 *   - lock_grant_blocked_waiter_partial()
 */

#if defined(SERVER_MODE)
/*
 * lock_grant_blocked_holder - Grant blocked holders
 *
 * return:
 *
 *   res_ptr(in): This function grants blocked holders whose blocked lock mode is
 *     compatible with all the granted lock mode of non-blocked holders.
 */
static void
lock_grant_blocked_holder (THREAD_ENTRY * thread_p, LK_RES * res_ptr)
{
  LK_ENTRY *prev_check;
  LK_ENTRY *check, *i, *prev;
  LOCK mode;
  int compat;

  /* The caller is holding a resource mutex */

  prev_check = NULL;
  check = res_ptr->holder;
  while (check != NULL && check->blocked_mode != NULL_LOCK)
    {
      /* there are some blocked holders */
      mode = NULL_LOCK;
      for (i = check->next; i != NULL; i = i->next)
	{
	  assert (i->granted_mode >= NULL_LOCK && mode >= NULL_LOCK);
	  mode = lock_Conv[i->granted_mode][mode];
	  assert (mode != NA_LOCK);
	}

      assert (check->blocked_mode >= NULL_LOCK);
      compat = lock_Comp[check->blocked_mode][mode];
      assert (compat != DB_NA);

      if (compat == false)
	{
	  break;		/* stop the granting */
	}

      /* compatible: grant it */

      /* hold the thread entry mutex */
      (void) thread_lock_entry (check->thrd_entry);

      /* check if the thread is still waiting on a lock */
      if (LK_IS_LOCKWAIT_THREAD (check->thrd_entry))
	{
	  /* the thread is still waiting on a lock */

	  /* reposition the lock entry according to UPR */
	  for (prev = check, i = check->next; i != NULL;)
	    {
	      if (i->blocked_mode == NULL_LOCK)
		{
		  break;
		}
	      prev = i;
	      i = i->next;
	    }
	  if (prev != check)
	    {			/* reposition it */
	      /* remove it */
	      if (prev_check == NULL)
		{
		  res_ptr->holder = check->next;
		}
	      else
		{
		  prev_check->next = check->next;
		}
	      /* insert it */
	      check->next = prev->next;
	      prev->next = check;
	    }

	  /* change granted_mode and blocked_mode */
	  check->granted_mode = check->blocked_mode;
	  check->blocked_mode = NULL_LOCK;

	  /* reflect the granted lock in the non2pl list */
	  lock_update_non2pl_list (thread_p, res_ptr, check->tran_index, check->granted_mode);

	  /* Record number of acquired locks */
	  perfmon_inc_stat (thread_p, PSTAT_LK_NUM_ACQUIRED_ON_OBJECTS);
#if defined(LK_TRACE_OBJECT)
	  LK_MSG_LOCK_ACQUIRED (entry_ptr);
#endif /* LK_TRACE_OBJECT */
	  /* wake up the blocked holder */
	  lock_resume (check, LOCK_RESUMED);
	}
      else
	{
	  if (check->thrd_entry->lockwait != NULL || check->thrd_entry->lockwait_state == (int) LOCK_SUSPENDED)
	    {
	      /* some strange lock wait state.. */
	      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_LK_STRANGE_LOCK_WAIT, 5, check->thrd_entry->lockwait,
		      check->thrd_entry->lockwait_state, check->thrd_entry->index, check->thrd_entry->tid,
		      check->thrd_entry->tran_index);
	    }
	  /* The thread is not waiting for a lock, currently. That is, the thread has already been waked up by timeout,
	   * deadlock victim or interrupt. In this case, we have nothing to do since the thread itself will remove this
	   * lock entry. */
	  (void) thread_unlock_entry (check->thrd_entry);
	  prev_check = check;
	}

      if (prev_check == NULL)
	{
	  check = res_ptr->holder;
	}
      else
	{
	  check = prev_check->next;
	}
    }

}
#endif /* SERVER_MODE */

#if defined(SERVER_MODE)
/*
 * lock_grant_blocked_waiter - Grant blocked waiters
 *
 * return:
 *
 *   res_ptr(in): This function grants blocked waiters whose blocked lock mode is
 *     compatible with the total mode of lock holders.
 */
static int
lock_grant_blocked_waiter (THREAD_ENTRY * thread_p, LK_RES * res_ptr)
{
  LK_ENTRY *prev_check;
  LK_ENTRY *check, *i;
  LOCK mode;
  bool change_total_waiters_mode = false;
  int error_code = NO_ERROR;
  int compat;

  /* The caller is holding a resource mutex */

  prev_check = NULL;
  check = res_ptr->waiter;
  while (check != NULL)
    {
      assert (check->blocked_mode >= NULL_LOCK && res_ptr->total_holders_mode >= NULL_LOCK);
      compat = lock_Comp[check->blocked_mode][res_ptr->total_holders_mode];
      assert (compat != DB_NA);

      if (compat == false)
	{
	  break;		/* stop the granting */
	}

      /* compatible: grant it */
      /* hold the thread entry mutex */
      (void) thread_lock_entry (check->thrd_entry);

      /* check if the thread is still waiting for a lock */
      if (LK_IS_LOCKWAIT_THREAD (check->thrd_entry))
	{
	  int owner_tran_index;

	  /* The thread is still waiting for a lock. */
	  change_total_waiters_mode = true;

	  /* remove the lock entry from the waiter */
	  if (prev_check == NULL)
	    {
	      res_ptr->waiter = check->next;
	    }
	  else
	    {
	      prev_check->next = check->next;
	    }

	  /* change granted_mode and blocked_mode of the entry */
	  check->granted_mode = check->blocked_mode;
	  check->blocked_mode = NULL_LOCK;

	  /* position the lock entry in the holder list */
	  lock_position_holder_entry (res_ptr, check);

	  /* change total_holders_mode */
	  assert (check->granted_mode >= NULL_LOCK && res_ptr->total_holders_mode >= NULL_LOCK);
	  res_ptr->total_holders_mode = lock_Conv[check->granted_mode][res_ptr->total_holders_mode];
	  assert (res_ptr->total_holders_mode != NA_LOCK);

	  /* insert the lock entry into transaction hold list. */
	  owner_tran_index = LOG_FIND_THREAD_TRAN_INDEX (check->thrd_entry);
	  lock_insert_into_tran_hold_list (check, owner_tran_index);

	  /* reflect the granted lock in the non2pl list */
	  lock_update_non2pl_list (thread_p, res_ptr, check->tran_index, check->granted_mode);

	  /* Record number of acquired locks */
	  perfmon_inc_stat (thread_p, PSTAT_LK_NUM_ACQUIRED_ON_OBJECTS);
#if defined(LK_TRACE_OBJECT)
	  LK_MSG_LOCK_ACQUIRED (entry_ptr);
#endif /* LK_TRACE_OBJECT */

	  /* wake up the blocked waiter */
	  lock_resume (check, LOCK_RESUMED);
	}
      else
	{
	  if (check->thrd_entry->lockwait != NULL || check->thrd_entry->lockwait_state == (int) LOCK_SUSPENDED)
	    {
	      /* some strange lock wait state.. */
	      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_LK_STRANGE_LOCK_WAIT, 5, check->thrd_entry->lockwait,
		      check->thrd_entry->lockwait_state, check->thrd_entry->index, check->thrd_entry->tid,
		      check->thrd_entry->tran_index);
	      error_code = ER_LK_STRANGE_LOCK_WAIT;
	    }
	  /* The thread is not waiting on the lock, currently. That is, the thread has already been waken up by lock
	   * timeout, deadlock victim or interrupt. In this case, we have nothing to do since the thread itself will
	   * remove this lock entry. */
	  (void) thread_unlock_entry (check->thrd_entry);
	  prev_check = check;
	}

      if (prev_check == NULL)
	{
	  check = res_ptr->waiter;
	}
      else
	{
	  check = prev_check->next;
	}
    }

  if (change_total_waiters_mode == true)
    {
      mode = NULL_LOCK;
      for (i = res_ptr->waiter; i != NULL; i = i->next)
	{
	  assert (i->blocked_mode >= NULL_LOCK && mode >= NULL_LOCK);
	  mode = lock_Conv[i->blocked_mode][mode];
	  assert (mode != NA_LOCK);
	}
      res_ptr->total_waiters_mode = mode;
    }

  return error_code;
}
#endif /* SERVER_MODE */

#if defined(SERVER_MODE)
/*
 * lock_grant_blocked_waiter_partial - Grant blocked waiters partially
 *
 * return:
 *
 *   res_ptr(in):
 *   from_whom(in):
 *
 * Note:This function grants blocked waiters that are located from from_whom
 *     to the end of waiter list whose blocked lock mode is compatible with
 *     all the blocked mode of the previous lock waiters and the total mode
 *     of lock holders.
 */
static void
lock_grant_blocked_waiter_partial (THREAD_ENTRY * thread_p, LK_RES * res_ptr, LK_ENTRY * from_whom)
{
  LK_ENTRY *prev_check;
  LK_ENTRY *check, *i;
  LOCK mode;
  int compat;

  /* the caller is holding a resource mutex */

  mode = NULL_LOCK;
  prev_check = NULL;
  check = res_ptr->waiter;
  while (check != from_whom)
    {
      assert (check->blocked_mode >= NULL_LOCK && mode >= NULL_LOCK);
      mode = lock_Conv[check->blocked_mode][mode];
      assert (mode != NA_LOCK);

      prev_check = check;
      check = check->next;
    }

  /* check = from_whom; */
  while (check != NULL)
    {
      assert (check->blocked_mode >= NULL_LOCK && mode >= NULL_LOCK);
      compat = lock_Comp[check->blocked_mode][mode];
      assert (compat != DB_NA);

      if (compat != true)
	{
	  break;
	}

      assert (check->blocked_mode >= NULL_LOCK && res_ptr->total_holders_mode >= NULL_LOCK);
      compat = lock_Comp[check->blocked_mode][res_ptr->total_holders_mode];
      assert (compat != DB_NA);

      if (compat == false)
	{
	  assert (check->blocked_mode >= NULL_LOCK && mode >= NULL_LOCK);
	  mode = lock_Conv[check->blocked_mode][mode];
	  assert (mode != NA_LOCK);

	  prev_check = check;
	  check = check->next;
	  continue;
	}

      /* compatible: grant it */
      (void) thread_lock_entry (check->thrd_entry);
      if (LK_IS_LOCKWAIT_THREAD (check->thrd_entry))
	{
	  int owner_tran_index;

	  /* the thread is waiting on a lock */
	  /* remove the lock entry from the waiter */
	  if (prev_check == NULL)
	    {
	      res_ptr->waiter = check->next;
	    }
	  else
	    {
	      prev_check->next = check->next;
	    }

	  /* change granted_mode and blocked_mode of the entry */
	  check->granted_mode = check->blocked_mode;
	  check->blocked_mode = NULL_LOCK;

	  /* position the lock entry into the holder list */
	  lock_position_holder_entry (res_ptr, check);

	  /* change total_holders_mode */
	  assert (check->granted_mode >= NULL_LOCK && res_ptr->total_holders_mode >= NULL_LOCK);
	  res_ptr->total_holders_mode = lock_Conv[check->granted_mode][res_ptr->total_holders_mode];
	  assert (res_ptr->total_holders_mode != NA_LOCK);

	  /* insert into transaction lock hold list */
	  owner_tran_index = LOG_FIND_THREAD_TRAN_INDEX (check->thrd_entry);
	  lock_insert_into_tran_hold_list (check, owner_tran_index);

	  /* reflect the granted lock in the non2pl list */
	  lock_update_non2pl_list (thread_p, res_ptr, check->tran_index, check->granted_mode);

	  /* Record number of acquired locks */
	  perfmon_inc_stat (thread_p, PSTAT_LK_NUM_ACQUIRED_ON_OBJECTS);
#if defined(LK_TRACE_OBJECT)
	  LK_MSG_LOCK_ACQUIRED (entry_ptr);
#endif /* LK_TRACE_OBJECT */

	  /* wake up the blocked waiter (correctness must be checked) */
	  lock_resume (check, LOCK_RESUMED);
	}
      else
	{
	  if (check->thrd_entry->lockwait != NULL || check->thrd_entry->lockwait_state == (int) LOCK_SUSPENDED)
	    {
	      /* some strange lock wait state.. */
	      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_LK_STRANGE_LOCK_WAIT, 5, check->thrd_entry->lockwait,
		      check->thrd_entry->lockwait_state, check->thrd_entry->index, check->thrd_entry->tid,
		      check->thrd_entry->tran_index);
	    }
	  /* The thread is not waiting on the lock. That is, the thread has already been waken up by lock timeout,
	   * deadlock victim or interrupt. In this case, we have nothing to do since the thread itself will remove this 
	   * lock entry. */
	  (void) thread_unlock_entry (check->thrd_entry);

	  /* change prev_check */
	  assert (check->blocked_mode >= NULL_LOCK && mode >= NULL_LOCK);
	  mode = lock_Conv[check->blocked_mode][mode];
	  assert (mode != NA_LOCK);

	  prev_check = check;
	}

      if (prev_check == NULL)
	{
	  check = res_ptr->waiter;
	}
      else
	{
	  check = prev_check->next;
	}
    }

  if (check == NULL)
    {
      res_ptr->total_waiters_mode = mode;
    }
  else
    {
      mode = NULL_LOCK;
      for (i = res_ptr->waiter; i != NULL; i = i->next)
	{
	  assert (i->blocked_mode >= NULL_LOCK && mode >= NULL_LOCK);
	  mode = lock_Conv[i->blocked_mode][mode];
	  assert (mode != NA_LOCK);
	}
      res_ptr->total_waiters_mode = mode;
    }

}
#endif /* SERVER_MODE */

#if defined(SERVER_MODE)
/*
 * lock_check_escalate- check if lcok counts over escalation limits or not
 *
 *   return: true if escalation is needed.
 *   thread_p(in):
 *   class_entry(in):
 *   tran_lock(in):
 *
 */
static bool
lock_check_escalate (THREAD_ENTRY * thread_p, LK_ENTRY * class_entry, LK_TRAN_LOCK * tran_lock)
{
  LK_ENTRY *superclass_entry = NULL;

  if (tran_lock->lock_escalation_on == true)
    {
      /* An another thread of current transaction is doing lock escalation. Therefore, the current thread gives up
       * doing lock escalation. */
      return false;
    }

  /* It cannot do lock escalation if class_entry is NULL */
  if (class_entry == NULL)
    {
      return false;
    }

  superclass_entry = class_entry->class_entry;

  /* check if the lock escalation is needed. */
  if (superclass_entry != NULL && !OID_IS_ROOTOID (&superclass_entry->res_head->key.oid))
    {
      /* Superclass_entry points to a root class in a class hierarchy. Escalate locks only if the criteria for the
       * superclass is met. Superclass keeps a counter for all locks set in the hierarchy. */
      if (superclass_entry->ngranules < prm_get_integer_value (PRM_ID_LK_ESCALATION_AT))
	{
	  return false;
	}
    }
  else if (class_entry->ngranules < prm_get_integer_value (PRM_ID_LK_ESCALATION_AT))
    {
      return false;
    }

  return true;
}


/*
 * lock_escalate_if_needed -
 *
 * return: one of following values
 *     LK_GRANTED
 *     LK_NOTGRANTED_DUE_ABORTED
 *     LK_NOTGRANTED_DUE_TIMEOUT
 *     LK_NOTGRANTED_DUE_ERROR
 *
 *   class_entry(in):
 *   tran_index(in):
 *
 * Note:This function check if lock escalation is needed at first.
 *     If lock escalation is needed, that is, an escalation threshold is over,
 *     this function converts instance lock(s) to a class lock and
 *     releases unnecessary instance locks.
 */
static int
lock_escalate_if_needed (THREAD_ENTRY * thread_p, LK_ENTRY * class_entry, int tran_index)
{
  LK_TRAN_LOCK *tran_lock;
  LOCK max_class_lock = NULL_LOCK;	/* escalated class lock mode */
  int granted;
  int wait_msecs;
  int rv;

  /* check lock escalation count */
  tran_lock = &lk_Gl.tran_lock_table[tran_index];
  rv = pthread_mutex_lock (&tran_lock->hold_mutex);

  if (lock_check_escalate (thread_p, class_entry, tran_lock) == false)
    {
      pthread_mutex_unlock (&tran_lock->hold_mutex);
      return LK_NOTGRANTED;
    }

  /* abort lock escalation if lock_escalation_abort = yes */
  if (prm_get_bool_value (PRM_ID_LK_ROLLBACK_ON_LOCK_ESCALATION) == true)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LK_ROLLBACK_ON_LOCK_ESCALATION, 1,
	      prm_get_integer_value (PRM_ID_LK_ESCALATION_AT));

      lock_set_error_for_aborted (class_entry, TRAN_ABORT_DUE_ROLLBACK_ON_ESCALATION);

      pthread_mutex_unlock (&tran_lock->hold_mutex);
      return LK_NOTGRANTED_DUE_ABORTED;
    }

  /* lock escalation should be performed */
  tran_lock->lock_escalation_on = true;

  if (class_entry->granted_mode == NULL_LOCK || class_entry->granted_mode == S_LOCK
      || class_entry->granted_mode == X_LOCK || class_entry->granted_mode == SCH_M_LOCK)
    {
      /* The class has no instance lock. */
      tran_lock->lock_escalation_on = false;
      pthread_mutex_unlock (&tran_lock->hold_mutex);
      return LK_GRANTED;
    }

  /* class_entry->granted_mode : IS_LOCK, IX_LOCK or SIX_LOCK */

  /* Because to count the shared and exclusive instance locks may cause high CPU usage, we used a simple rule to decide
   * the escalated class lock mode */
  if (class_entry->granted_mode == IX_LOCK || class_entry->granted_mode == SIX_LOCK)
    {
      max_class_lock = X_LOCK;
    }
  else
    {
      max_class_lock = S_LOCK;
    }

  pthread_mutex_unlock (&tran_lock->hold_mutex);

  if (max_class_lock != NULL_LOCK)
    {
      /* 
       * lock escalation is performed
       * 1. hold a lock on the class with the escalated lock mode
       */
      wait_msecs = LK_FORCE_ZERO_WAIT;	/* Conditional Locking */
      granted = lock_internal_perform_lock_object (thread_p, tran_index, &class_entry->res_head->key.oid, NULL,
						   max_class_lock, wait_msecs, &class_entry, NULL);
      if (granted != LK_GRANTED)
	{
	  /* The reason of the lock request failure: 1. interrupt 2. shortage of lock resource entries 3. shortage of
	   * lock entries */
	  /* reset lock_escalation_on */
	  rv = pthread_mutex_lock (&tran_lock->hold_mutex);
	  tran_lock->lock_escalation_on = false;
	  pthread_mutex_unlock (&tran_lock->hold_mutex);
	  return granted;
	}

      /* 2. release original class lock only one time in order to maintain original class lock count */
      lock_internal_perform_unlock_object (thread_p, class_entry, false, true);
    }

  /* reset lock_escalation_on */
  rv = pthread_mutex_lock (&tran_lock->hold_mutex);
  tran_lock->lock_escalation_on = false;
  pthread_mutex_unlock (&tran_lock->hold_mutex);

  return LK_GRANTED;
}
#endif /* SERVER_MODE */

/*
 *  Private Functions Group: major functions for locking and unlocking
 *
 *   - lk_internal_lock_object_instant()
 *   - lk_internal_lock_object()
 *   - lk_internal_unlock_object()
 */

#if defined(SERVER_MODE)
/*
 * lock_internal_hold_lock_object_instant - Hold object lock with instant duration
 *
 * return: LK_GRANTED/LK_NOTGRANTED/LK_NOTGRANTED_DUE_ERROR
 *
 *   tran_index(in):
 *   oid(in):
 *   class_oid(in):
 *   lock(in):
 *
 * Note:hold a lock on the given object with instant duration.
 */
static int
lock_internal_hold_lock_object_instant (int tran_index, const OID * oid, const OID * class_oid, LOCK lock)
{
  LF_TRAN_ENTRY *t_entry = thread_get_tran_entry (NULL, THREAD_TS_OBJ_LOCK_RES);
  LK_RES_KEY search_key;
  LK_RES *res_ptr;
  LK_ENTRY *entry_ptr, *i;
  LOCK new_mode;
  LOCK group_mode;
  int rv;
  int compat1, compat2;

#if defined(LK_DUMP)
  if (lk_Gl.dump_level >= 1)
    {
      fprintf (stderr,
	       "LK_DUMP::lk_internal_lock_object_instant()\n"
	       "  tran(%2d) : oid(%2d|%3d|%3d), class_oid(%2d|%3d|%3d), LOCK(%7s)\n", tran_index, oid->volid,
	       oid->pageid, oid->slotid, class_oid ? class_oid->volid : -1, class_oid ? class_oid->pageid : -1,
	       class_oid ? class_oid->slotid : -1, LOCK_TO_LOCKMODE_STRING (lock));
    }
#endif /* LK_DUMP */

#if defined(SERVER_MODE) && defined(DIAG_DEVEL)
  SET_DIAG_VALUE (diag_executediag, DIAG_OBJ_TYPE_LOCK_REQUEST, 1, DIAG_VAL_SETTYPE_INC, NULL);
#endif /* SERVER_MODE && DIAG_DEVEL */
  if (class_oid != NULL && !OID_IS_ROOTOID (class_oid))
    {
      /* instance lock request */
      /* check if an implicit lock has been acquired */
      if (lock_is_class_lock_escalated (lock_get_object_lock (class_oid, oid_Root_class_oid, tran_index), lock) == true)
	{
	  return LK_GRANTED;
	}
    }

  /* search hash table */
  search_key = lock_create_search_key ((OID *) oid, (OID *) class_oid);
  rv = lf_hash_find (t_entry, &lk_Gl.obj_hash_table, (void *) &search_key, (void **) &res_ptr);
  if (rv != NO_ERROR)
    {
      return rv;
    }

  if (res_ptr == NULL)
    {
      /* the lockable object is NOT in the hash chain */
      /* the request can be granted */
      return LK_GRANTED;
    }

  /* the lockable object exists in the hash chain */
  /* So, check whether I am a holder of the object. */
  /* find the lock entry of current transaction */
  for (entry_ptr = res_ptr->holder; entry_ptr != NULL; entry_ptr = entry_ptr->next)
    {
      if (entry_ptr->tran_index == tran_index)
	{
	  break;
	}
    }

  /* I am not a lock holder of the lockable object. */
  if (entry_ptr == NULL)
    {
      assert (lock >= NULL_LOCK && res_ptr->total_waiters_mode >= NULL_LOCK
	      && res_ptr->total_holders_mode >= NULL_LOCK);

      compat1 = lock_Comp[lock][res_ptr->total_waiters_mode];
      compat2 = lock_Comp[lock][res_ptr->total_holders_mode];
      assert (compat1 != DB_NA && compat2 != DB_NA);

      if (compat1 == true && compat2 == true)
	{
	  pthread_mutex_unlock (&res_ptr->res_mutex);
	  return LK_GRANTED;
	}
      else
	{
	  pthread_mutex_unlock (&res_ptr->res_mutex);
	  return LK_NOTGRANTED;
	}
    }

  /* I am a lock holder of the lockable object. */
  assert (lock >= NULL_LOCK && entry_ptr->granted_mode >= NULL_LOCK);
  new_mode = lock_Conv[lock][entry_ptr->granted_mode];
  assert (new_mode != NA_LOCK);

  if (new_mode == entry_ptr->granted_mode)
    {
      /* a request with either a less exclusive or an equal mode of lock */
      pthread_mutex_unlock (&res_ptr->res_mutex);
      return LK_GRANTED;
    }
  else
    {
      /* check the compatibility with other holders' granted mode */
      group_mode = NULL_LOCK;
      for (i = res_ptr->holder; i != NULL; i = i->next)
	{
	  if (i != entry_ptr)
	    {
	      assert (i->granted_mode >= NULL_LOCK && group_mode >= NULL_LOCK);
	      group_mode = lock_Conv[i->granted_mode][group_mode];
	      assert (group_mode != NA_LOCK);
	    }
	}

      assert (new_mode >= NULL_LOCK && group_mode >= NULL_LOCK);
      compat1 = lock_Comp[new_mode][group_mode];
      assert (compat1 != DB_NA);

      if (compat1 == true)
	{
	  pthread_mutex_unlock (&res_ptr->res_mutex);
	  return LK_GRANTED;
	}
      else
	{
	  pthread_mutex_unlock (&res_ptr->res_mutex);
	  return LK_NOTGRANTED;
	}
    }
}
#endif /* SERVER_MODE */

#if defined(SERVER_MODE)
/*
 * lock_internal_perform_lock_object - Performs actual object lock operation
 *
 * return: one of following values
 *              LK_GRANTED
 *              LK_NOTGRANTED_DUE_ABORTED
 *              LK_NOTGRANTED_DUE_TIMEOUT
 *              LK_NOTGRANTED_DUE_ERROR
 *
 *   tran_index(in):
 *   oid(in):
 *   class_oid(in):
 *   lock(in):
 *   wait_msecs(in):
 *   entry_addr_ptr(in):
 *   class_entry(in):
 *
 * Note:lock an object whose id is pointed by oid with given lock mode 'lock'.
 *
 *     If cond_flag is true and the object has already been locked
 *     by other transaction, then return LK_NOTGRANTED;
 *     else this transaction is suspended until it can acquire the lock.
 */
static int
lock_internal_perform_lock_object (THREAD_ENTRY * thread_p, int tran_index, const OID * oid, const OID * class_oid,
				   LOCK lock, int wait_msecs, LK_ENTRY ** entry_addr_ptr, LK_ENTRY * class_entry)
{
  LF_TRAN_ENTRY *t_entry_res = thread_get_tran_entry (thread_p, THREAD_TS_OBJ_LOCK_RES);
  LF_TRAN_ENTRY *t_entry_ent = thread_get_tran_entry (thread_p, THREAD_TS_OBJ_LOCK_ENT);
  LK_RES_KEY search_key;
  TRAN_ISOLATION isolation;
  int ret_val;
  LOCK group_mode, old_mode, new_mode;	/* lock mode */
  LK_RES *res_ptr;
  LK_ENTRY *entry_ptr = NULL;
  LK_ENTRY *wait_entry_ptr = NULL;
  LK_ENTRY *prev, *curr, *i;
  bool lock_conversion = false;
  THREAD_ENTRY *thrd_entry;
  int rv;
  LK_TRAN_LOCK *tran_lock;
  bool is_instant_duration;
  int compat1, compat2;
  bool is_res_mutex_locked = false;
  TSC_TICKS start_tick, end_tick;
  TSCTIMEVAL tv_diff;
  UINT64 lock_wait_time;

#if defined(ENABLE_SYSTEMTAP)
  OID *class_oid_for_marker_p, *oid_for_marker_p;
#endif /* ENABLE_SYSTEMTAP */

  assert (!OID_ISNULL (oid));
  assert (class_oid == NULL || !OID_ISNULL (class_oid));

#if defined(ENABLE_SYSTEMTAP)
  if (class_oid == NULL)
    {
      class_oid_for_marker_p = &oid_Null_oid;
    }
  else
    {
      class_oid_for_marker_p = class_oid;
    }

  if (oid == NULL)
    {
      oid_for_marker_p = &oid_Null_oid;
    }
  else
    {
      oid_for_marker_p = oid;
    }

  CUBRID_LOCK_ACQUIRE_START (oid_for_marker_p, class_oid_for_marker_p, lock);
#endif /* ENABLE_SYSTEMTAP */

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  thrd_entry = thread_p;

  new_mode = group_mode = old_mode = NULL_LOCK;
#if defined(LK_DUMP)
  if (lk_Gl.dump_level >= 1)
    {
      fprintf (stderr,
	       "LK_DUMP::lk_internal_lock_object()\n"
	       "  tran(%2d) : oid(%2d|%3d|%3d), class_oid(%2d|%3d|%3d), LOCK(%7s) wait_msecs(%d)\n", tran_index,
	       oid->volid, oid->pageid, oid->slotid, class_oid ? class_oid->volid : -1,
	       class_oid ? class_oid->pageid : -1, class_oid ? class_oid->slotid : -1, LOCK_TO_LOCKMODE_STRING (lock),
	       wait_msecs);
    }
#endif /* LK_DUMP */

  /* isolation */
  isolation = logtb_find_isolation (tran_index);

  /* initialize */
  *entry_addr_ptr = NULL;

#if defined(SERVER_MODE) && defined(DIAG_DEVEL)
  SET_DIAG_VALUE (diag_executediag, DIAG_OBJ_TYPE_LOCK_REQUEST, 1, DIAG_VAL_SETTYPE_INC, NULL);
#endif /* SERVER_MODE && DIAG_DEVEL */

  /* get current locking phase */
  tran_lock = &lk_Gl.tran_lock_table[tran_index];
  is_instant_duration = tran_lock->is_instant_duration;

start:
  assert (!is_res_mutex_locked);

  if (class_oid != NULL && !OID_IS_ROOTOID (class_oid))
    {
      /* instance lock request */

      /* do lock escalation if it is needed and check if an implicit lock has been acquired. */
      ret_val = lock_escalate_if_needed (thread_p, class_entry, tran_index);
      if (ret_val == LK_NOTGRANTED_DUE_ABORTED)
	{
	  LOG_TDES *tdes = LOG_FIND_TDES (tran_index);
	  if (tdes && tdes->tran_abort_reason == TRAN_ABORT_DUE_ROLLBACK_ON_ESCALATION)
	    {
	      goto end;
	    }
	}

      if (ret_val == LK_GRANTED
	  && lock_is_class_lock_escalated (lock_get_object_lock (class_oid, oid_Root_class_oid, tran_index),
					   lock) == true)
	{
	  perfmon_inc_stat (thread_p, PSTAT_LK_NUM_RE_REQUESTED_ON_OBJECTS);	/* monitoring */
	  ret_val = LK_GRANTED;
	  goto end;
	}
    }
  else
    {
      /* Class lock request. */
      /* Try to find class lock entry if it already exists to avoid using the expensive resource mutex. */
      entry_ptr = lock_find_class_entry (tran_index, oid);
      if (entry_ptr != NULL)
	{
	  res_ptr = entry_ptr->res_head;
	  goto lock_tran_lk_entry;
	}
    }

  /* find or add the lockable object in the lock table */
  search_key = lock_create_search_key ((OID *) oid, (OID *) class_oid);
  rv = lf_hash_find_or_insert (t_entry_res, &lk_Gl.obj_hash_table, (void *) &search_key, (void **) &res_ptr, NULL);
  if (rv != NO_ERROR)
    {
      return rv;
    }
  else if (res_ptr == NULL)
    {
      return ER_FAILED;
    }
  /* Find or insert also locks the resource mutex. */
  is_res_mutex_locked = true;

  if (res_ptr->holder == NULL && res_ptr->waiter == NULL && res_ptr->non2pl == NULL)
    {
      /* the lockable object was NOT in the hash chain */
      /* the lock request can be granted. */

      /* initialize the lock resource entry */
      lock_initialize_resource_as_allocated (res_ptr, NULL_LOCK);

      entry_ptr = lock_get_new_entry (tran_index, t_entry_ent, &lk_Gl.obj_free_entry_list);
      if (entry_ptr == NULL)
	{
	  assert (is_res_mutex_locked);
	  pthread_mutex_unlock (&res_ptr->res_mutex);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LK_ALLOC_RESOURCE, 1, "lock heap entry");
	  ret_val = LK_NOTGRANTED_DUE_ERROR;
	  goto end;
	}

      /* initialize the lock entry as granted state */
      lock_initialize_entry_as_granted (entry_ptr, tran_index, res_ptr, lock);
      if (is_instant_duration)
	{
	  entry_ptr->instant_lock_count++;
	  assert (entry_ptr->instant_lock_count > 0);
	}

      /* add the lock entry into the holder list */
      res_ptr->holder = entry_ptr;

      /* to manage granules */
      entry_ptr->class_entry = class_entry;
      lock_increment_class_granules (class_entry);

      /* add the lock entry into the transaction hold list */
      lock_insert_into_tran_hold_list (entry_ptr, tran_index);

      res_ptr->total_holders_mode = lock;

      /* Record number of acquired locks */
      perfmon_inc_stat (thread_p, PSTAT_LK_NUM_ACQUIRED_ON_OBJECTS);
#if defined(LK_TRACE_OBJECT)
      LK_MSG_LOCK_ACQUIRED (entry_ptr);
#endif /* LK_TRACE_OBJECT */

      /* release all mutexes */
      assert (is_res_mutex_locked);
      pthread_mutex_unlock (&res_ptr->res_mutex);

      *entry_addr_ptr = entry_ptr;

      ret_val = LK_GRANTED;
      goto end;
    }

  /* the lockable object existed in the hash chain So, check whether I am a holder of the object. */

  /* find the lock entry of current transaction */
  entry_ptr = res_ptr->holder;
  while (entry_ptr != NULL)
    {
      if (entry_ptr->tran_index == tran_index)
	{
	  break;
	}
      entry_ptr = entry_ptr->next;
    }

  if (entry_ptr == NULL)
    {
      /* The object exists in the hash chain & I am not a lock holder of the lockable object. */

      /* 1. I am not a holder & my request can be granted. */
      assert (lock >= NULL_LOCK && res_ptr->total_waiters_mode >= NULL_LOCK
	      && res_ptr->total_holders_mode >= NULL_LOCK);
      compat1 = lock_Comp[lock][res_ptr->total_waiters_mode];
      compat2 = lock_Comp[lock][res_ptr->total_holders_mode];
      assert (compat1 != DB_NA && compat2 != DB_NA);

      if (compat1 == true && compat2 == true)
	{
	  entry_ptr = lock_get_new_entry (tran_index, t_entry_ent, &lk_Gl.obj_free_entry_list);
	  if (entry_ptr == NULL)
	    {
	      pthread_mutex_unlock (&res_ptr->res_mutex);
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LK_ALLOC_RESOURCE, 1, "lock heap entry");

	      ret_val = LK_NOTGRANTED_DUE_ERROR;
	      goto end;
	    }

	  /* initialize the lock entry as granted state */
	  lock_initialize_entry_as_granted (entry_ptr, tran_index, res_ptr, lock);
	  if (is_instant_duration)
	    {
	      entry_ptr->instant_lock_count++;
	      assert (entry_ptr->instant_lock_count > 0);
	    }

	  /* to manage granules */
	  entry_ptr->class_entry = class_entry;
	  lock_increment_class_granules (class_entry);

	  /* add the lock entry into the holder list */
	  lock_position_holder_entry (res_ptr, entry_ptr);

	  /* change total_holders_mode (total mode of holder list) */
	  assert (lock >= NULL_LOCK && res_ptr->total_holders_mode >= NULL_LOCK);
	  res_ptr->total_holders_mode = lock_Conv[lock][res_ptr->total_holders_mode];
	  assert (res_ptr->total_holders_mode != NA_LOCK);

	  /* add the lock entry into the transaction hold list */
	  lock_insert_into_tran_hold_list (entry_ptr, tran_index);

	  lock_update_non2pl_list (thread_p, res_ptr, tran_index, lock);

	  /* Record number of acquired locks */
	  perfmon_inc_stat (thread_p, PSTAT_LK_NUM_ACQUIRED_ON_OBJECTS);
#if defined(LK_TRACE_OBJECT)
	  LK_MSG_LOCK_ACQUIRED (entry_ptr);
#endif /* LK_TRACE_OBJECT */

	  assert (is_res_mutex_locked);
	  pthread_mutex_unlock (&res_ptr->res_mutex);
	  *entry_addr_ptr = entry_ptr;

	  ret_val = LK_GRANTED;
	  goto end;
	}

      /* 2. I am not a holder & my request cannot be granted. */
      if (wait_msecs == LK_ZERO_WAIT || wait_msecs == LK_FORCE_ZERO_WAIT)
	{
	  assert (is_res_mutex_locked);
	  pthread_mutex_unlock (&res_ptr->res_mutex);
	  if (wait_msecs == LK_ZERO_WAIT)
	    {
	      if (entry_ptr == NULL)
		{
		  entry_ptr = lock_get_new_entry (tran_index, t_entry_ent, &lk_Gl.obj_free_entry_list);
		  if (entry_ptr == NULL)
		    {
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LK_ALLOC_RESOURCE, 1, "lock heap entry");
		      ret_val = LK_NOTGRANTED_DUE_ERROR;
		      goto end;
		    }
		  lock_initialize_entry_as_blocked (entry_ptr, thread_p, tran_index, res_ptr, lock);
		  if (is_instant_duration
		      /* && lock_Comp[lock][NULL_LOCK] == true */ )
		    {
		      entry_ptr->instant_lock_count++;
		      assert (entry_ptr->instant_lock_count > 0);
		    }
		}
	      (void) lock_set_error_for_timeout (thread_p, entry_ptr);

	      lock_free_entry (tran_index, t_entry_ent, &lk_Gl.obj_free_entry_list, entry_ptr);
	    }

	  ret_val = LK_NOTGRANTED_DUE_TIMEOUT;
	  goto end;
	}

      /* check if another thread is waiting for the same resource */
      wait_entry_ptr = res_ptr->waiter;
      while (wait_entry_ptr != NULL)
	{
	  if (wait_entry_ptr->tran_index == tran_index)
	    {
	      break;
	    }
	  wait_entry_ptr = wait_entry_ptr->next;
	}

      if (wait_entry_ptr != NULL)
	{
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_LK_MANY_LOCK_WAIT_TRAN, 1, tran_index);
	  thread_lock_entry (thrd_entry);
	  thread_lock_entry (wait_entry_ptr->thrd_entry);
	  if (wait_entry_ptr->thrd_entry->lockwait == NULL)
	    {
	      /* */
	      thread_unlock_entry (wait_entry_ptr->thrd_entry);
	      thread_unlock_entry (thrd_entry);
	      assert (is_res_mutex_locked);
	      pthread_mutex_unlock (&res_ptr->res_mutex);
	      is_res_mutex_locked = false;
	      goto start;
	    }

	  thrd_entry->tran_next_wait = wait_entry_ptr->thrd_entry->tran_next_wait;
	  wait_entry_ptr->thrd_entry->tran_next_wait = thrd_entry;

	  thread_unlock_entry (wait_entry_ptr->thrd_entry);
	  assert (is_res_mutex_locked);
	  pthread_mutex_unlock (&res_ptr->res_mutex);
	  is_res_mutex_locked = false;

	  thread_suspend_wakeup_and_unlock_entry (thrd_entry, THREAD_LOCK_SUSPENDED);
	  if (entry_ptr)
	    {
	      if (entry_ptr->thrd_entry->resume_status == THREAD_RESUME_DUE_TO_INTERRUPT)
		{
		  /* a shutdown thread wakes me up */
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERRUPTED, 0);

		  ret_val = LK_NOTGRANTED_DUE_ERROR;
		  goto end;
		}
	      else if (entry_ptr->thrd_entry->resume_status != THREAD_LOCK_RESUMED)
		{
		  /* wake up with other reason */
		  assert (0);

		  if (er_errid () == NO_ERROR)
		    {
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERRUPTED, 0);
		    }
		  ret_val = LK_NOTGRANTED_DUE_ERROR;
		  goto end;
		}
	      else
		{
		  assert (entry_ptr->thrd_entry->resume_status == THREAD_LOCK_RESUMED);
		}
	    }

	  goto start;
	}

      /* allocate a lock entry. */
      entry_ptr = lock_get_new_entry (tran_index, t_entry_ent, &lk_Gl.obj_free_entry_list);
      if (entry_ptr == NULL)
	{
	  assert (is_res_mutex_locked);
	  pthread_mutex_unlock (&res_ptr->res_mutex);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LK_ALLOC_RESOURCE, 1, "lock heap entry");
	  ret_val = LK_NOTGRANTED_DUE_ERROR;
	  goto end;
	}
      /* initialize the lock entry as blocked state */
      lock_initialize_entry_as_blocked (entry_ptr, thread_p, tran_index, res_ptr, lock);
      if (is_instant_duration)
	{
	  entry_ptr->instant_lock_count++;
	  assert (entry_ptr->instant_lock_count > 0);
	}

      /* append the lock request at the end of the waiter */
      prev = NULL;
      for (i = res_ptr->waiter; i != NULL; i = i->next)
	{
	  prev = i;
	}
      if (prev == NULL)
	{
	  res_ptr->waiter = entry_ptr;
	}
      else
	{
	  prev->next = entry_ptr;
	}

      /* change total_waiters_mode (total mode of waiting waiter) */
      assert (lock >= NULL_LOCK && res_ptr->total_waiters_mode >= NULL_LOCK);
      res_ptr->total_waiters_mode = lock_Conv[lock][res_ptr->total_waiters_mode];
      assert (res_ptr->total_waiters_mode != NA_LOCK);

      goto blocked;
    }				/* end of a new lock request */

lock_tran_lk_entry:
  /* The object exists in the hash chain & I am a lock holder of the lockable object. */
  lock_conversion = true;
  old_mode = entry_ptr->granted_mode;
  assert (lock >= NULL_LOCK && entry_ptr->granted_mode >= NULL_LOCK);
  new_mode = lock_Conv[lock][entry_ptr->granted_mode];
  assert (new_mode != NA_LOCK);

  if (new_mode == entry_ptr->granted_mode)
    {
      /* a request with either a less exclusive or an equal mode of lock */
      entry_ptr->count += 1;
      if (is_instant_duration)
	{
	  compat1 = lock_Comp[lock][entry_ptr->granted_mode];
	  assert (compat1 != DB_NA);

	  if ((lock >= IX_LOCK && (entry_ptr->instant_lock_count == 0 && entry_ptr->granted_mode >= IX_LOCK))
	      && compat1 != true)
	    {
	      /* if the lock is already acquired with incompatible mode by current transaction, remove instant instance 
	       * locks */
	      lock_stop_instant_lock_mode (thread_p, tran_index, false);
	    }
	  else
	    {
	      entry_ptr->instant_lock_count++;
	      assert (entry_ptr->instant_lock_count > 0);
	    }
	}

      if (is_res_mutex_locked)
	{
	  pthread_mutex_unlock (&res_ptr->res_mutex);
	}
      perfmon_inc_stat (thread_p, PSTAT_LK_NUM_RE_REQUESTED_ON_OBJECTS);	/* monitoring */
      *entry_addr_ptr = entry_ptr;

      ret_val = LK_GRANTED;
      goto end;
    }

  if (!is_res_mutex_locked)
    {
      /* We need to lock resource mutex. */
      pthread_mutex_lock (&res_ptr->res_mutex);
      is_res_mutex_locked = true;
    }

  /* check the compatibility with other holders' granted mode */
  group_mode = NULL_LOCK;
  for (i = res_ptr->holder; i != NULL; i = i->next)
    {
      if (i != entry_ptr)
	{
	  assert (i->granted_mode >= NULL_LOCK && group_mode >= NULL_LOCK);
	  group_mode = lock_Conv[i->granted_mode][group_mode];
	  assert (group_mode != NA_LOCK);
	}
    }

  assert (new_mode >= NULL_LOCK && group_mode >= NULL_LOCK);
  compat1 = lock_Comp[new_mode][group_mode];
  assert (compat1 != DB_NA);

  if (compat1 == true)
    {
      entry_ptr->granted_mode = new_mode;
      entry_ptr->count += 1;
      if (is_instant_duration)
	{
	  entry_ptr->instant_lock_count++;
	  assert (entry_ptr->instant_lock_count > 0);
	}

      assert (lock >= NULL_LOCK && res_ptr->total_holders_mode >= NULL_LOCK);
      res_ptr->total_holders_mode = lock_Conv[lock][res_ptr->total_holders_mode];
      assert (res_ptr->total_holders_mode != NA_LOCK);

      lock_update_non2pl_list (thread_p, res_ptr, tran_index, lock);
      assert (is_res_mutex_locked);
      pthread_mutex_unlock (&res_ptr->res_mutex);

      goto lock_conversion_treatement;
    }

  /* I am a holder & my request cannot be granted. */
  if (wait_msecs == LK_ZERO_WAIT || wait_msecs == LK_FORCE_ZERO_WAIT)
    {
      pthread_mutex_unlock (&res_ptr->res_mutex);
      if (wait_msecs == LK_ZERO_WAIT)
	{
	  LK_ENTRY *p = lock_get_new_entry (tran_index, t_entry_ent,
					    &lk_Gl.obj_free_entry_list);

	  if (p != NULL)
	    {
	      lock_initialize_entry_as_blocked (p, thread_p, tran_index, res_ptr, lock);
	      lock_set_error_for_timeout (thread_p, p);
	      lock_free_entry (tran_index, t_entry_ent, &lk_Gl.obj_free_entry_list, p);
	    }
	}

      ret_val = LK_NOTGRANTED_DUE_TIMEOUT;
      goto end;
    }

  /* Upgrader Positioning Rule (UPR) */

  /* check if another thread is waiting for the same resource */
  if (entry_ptr->blocked_mode != NULL_LOCK)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_LK_MANY_LOCK_WAIT_TRAN, 1, tran_index);
      thread_lock_entry (thrd_entry);
      thread_lock_entry (entry_ptr->thrd_entry);

      if (entry_ptr->thrd_entry->lockwait == NULL)
	{
	  thread_unlock_entry (entry_ptr->thrd_entry);
	  thread_unlock_entry (thrd_entry);
	  assert (is_res_mutex_locked);
	  pthread_mutex_unlock (&res_ptr->res_mutex);
	  is_res_mutex_locked = false;
	  goto start;
	}

      thrd_entry->tran_next_wait = entry_ptr->thrd_entry->tran_next_wait;
      entry_ptr->thrd_entry->tran_next_wait = thrd_entry;

      thread_unlock_entry (entry_ptr->thrd_entry);

      assert (is_res_mutex_locked);
      pthread_mutex_unlock (&res_ptr->res_mutex);
      is_res_mutex_locked = false;

      thread_suspend_wakeup_and_unlock_entry (thrd_entry, THREAD_LOCK_SUSPENDED);
      if (thrd_entry->resume_status == THREAD_RESUME_DUE_TO_INTERRUPT)
	{
	  /* a shutdown thread wakes me up */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERRUPTED, 0);
	  ret_val = LK_NOTGRANTED_DUE_ERROR;
	  goto end;
	}
      else if (thrd_entry->resume_status != THREAD_LOCK_RESUMED)
	{
	  /* wake up with other reason */
	  assert (0);

	  if (er_errid () == NO_ERROR)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERRUPTED, 0);
	    }
	  ret_val = LK_NOTGRANTED_DUE_ERROR;
	  goto end;
	}
      else
	{
	  assert (thrd_entry->resume_status == THREAD_LOCK_RESUMED);
	}

      goto start;
    }

  entry_ptr->blocked_mode = new_mode;
  entry_ptr->count += 1;
  if (is_instant_duration)
    {
      entry_ptr->instant_lock_count++;
      assert (entry_ptr->instant_lock_count > 0);
    }

  entry_ptr->thrd_entry = thread_p;

  assert (lock >= NULL_LOCK && res_ptr->total_holders_mode >= NULL_LOCK);
  res_ptr->total_holders_mode = lock_Conv[lock][res_ptr->total_holders_mode];
  assert (res_ptr->total_holders_mode != NA_LOCK);

  /* remove the lock entry from the holder list */
  prev = NULL;
  curr = res_ptr->holder;
  while ((curr != NULL) && (curr != entry_ptr))
    {
      prev = curr;
      curr = curr->next;
    }
  if (prev == NULL)
    {
      res_ptr->holder = entry_ptr->next;
    }
  else
    {
      prev->next = entry_ptr->next;
    }

  /* position the lock entry in the holder list according to UPR */
  lock_position_holder_entry (res_ptr, entry_ptr);

blocked:

  if (perfmon_is_perf_tracking_and_active (PERFMON_ACTIVE_LOCK_OBJECT))
    {
      tsc_getticks (&start_tick);
    }

  /* LK_CANWAIT(wait_msecs) : wait_msecs > 0 */
  perfmon_inc_stat (thread_p, PSTAT_LK_NUM_WAITED_ON_OBJECTS);
#if defined(LK_TRACE_OBJECT)
  LK_MSG_LOCK_WAITFOR (entry_ptr);
#endif /* LK_TRACE_OBJECT */

  (void) thread_lock_entry (entry_ptr->thrd_entry);
  if (is_res_mutex_locked)
    {
      pthread_mutex_unlock (&res_ptr->res_mutex);
    }
  ret_val = lock_suspend (thread_p, entry_ptr, wait_msecs);

  if (perfmon_is_perf_tracking_and_active (PERFMON_ACTIVE_LOCK_OBJECT))
    {
      tsc_getticks (&end_tick);
      tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
      lock_wait_time = tv_diff.tv_sec * 1000000LL + tv_diff.tv_usec;
      perfmon_lk_waited_time_on_objects (thread_p, lock, lock_wait_time);
    }

  if (ret_val != LOCK_RESUMED)
    {
      /* Following three cases are possible. 1. lock timeout 2. deadlock victim 3. interrupt In any case, current
       * thread must remove the wait info. */
      lock_internal_perform_unlock_object (thread_p, entry_ptr, false, false);

      if (ret_val == LOCK_RESUMED_ABORTED)
	{
	  ret_val = LK_NOTGRANTED_DUE_ABORTED;
	  goto end;
	}
      else if (ret_val == LOCK_RESUMED_INTERRUPT)
	{
	  ret_val = LK_NOTGRANTED_DUE_ERROR;
	  goto end;
	}
      else			/* LOCK_RESUMED_TIMEOUT || LOCK_SUSPENDED */
	{
	  ret_val = LK_NOTGRANTED_DUE_TIMEOUT;
	  goto end;
	}
    }

  /* The transaction now got the lock on the object */
lock_conversion_treatement:

  if (entry_ptr->res_head->key.type == LOCK_RESOURCE_CLASS && lock_conversion == true)
    {
      new_mode = entry_ptr->granted_mode;
      switch (old_mode)
	{
	case IS_LOCK:
	  if (IS_WRITE_EXCLUSIVE_LOCK (new_mode) || new_mode == S_LOCK || new_mode == SIX_LOCK)
	    {
	      lock_remove_all_inst_locks (thread_p, tran_index, oid, S_LOCK);
	    }
	  break;

	case IX_LOCK:
	  if (new_mode == SIX_LOCK)
	    {
	      lock_remove_all_inst_locks (thread_p, tran_index, oid, S_LOCK);
	    }
	  else if (IS_WRITE_EXCLUSIVE_LOCK (new_mode))
	    {
	      lock_remove_all_inst_locks (thread_p, tran_index, oid, X_LOCK);
	    }
	  break;

	case SIX_LOCK:
	  /* new_mode == X_LOCK */
	  lock_remove_all_inst_locks (thread_p, tran_index, oid, X_LOCK);
	  break;

	default:
	  break;
	}

      perfmon_inc_stat (thread_p, PSTAT_LK_NUM_CONVERTED_ON_OBJECTS);
#if defined(LK_TRACE_OBJECT)
      LK_MSG_LOCK_CONVERTED (entry_ptr);
#endif /* LK_TRACE_OBJECT */
    }

  if (lock_conversion == false)
    {
      /* to manage granules */
      entry_ptr->class_entry = class_entry;
      lock_increment_class_granules (class_entry);
    }

  *entry_addr_ptr = entry_ptr;
  ret_val = LK_GRANTED;

end:
#if defined(ENABLE_SYSTEMTAP)
  CUBRID_LOCK_ACQUIRE_END (oid_for_marker_p, class_oid_for_marker_p, lock, ret_val != LK_GRANTED);
#endif /* ENABLE_SYSTEMTAP */

  return ret_val;
}
#endif /* SERVER_MODE */

#if defined(SERVER_MODE)
/*
 * lock_internal_perform_unlock_object - Performs actual object unlock operation
 *
 * return:
 *
 *   entry_ptr(in):
 *   release_flag(in):
 *   move_to_non2pl(in):
 *
 * Note:Unlock a lock specified by entry_ptr.
 *     Therefore, for the 2 phase locking, the caller must unlock from leaf
 *     to root or atomically all locks to which the transaction is related.
 *
 *     if release_flag is true, release the lock item.
 *     Otherwise, just decrement the lock count for supporting isolation level.
 */
static void
lock_internal_perform_unlock_object (THREAD_ENTRY * thread_p, LK_ENTRY * entry_ptr, int release_flag,
				     int move_to_non2pl)
{
  LF_TRAN_ENTRY *t_entry = thread_get_tran_entry (thread_p, THREAD_TS_OBJ_LOCK_ENT);
  int tran_index;
  LK_RES *res_ptr;
  LK_ENTRY *i;
  LK_ENTRY *prev, *curr;
  LK_ENTRY *from_whom;
  LOCK mode;
  int rv;

#if defined(LK_DUMP)
  if (lk_Gl.dump_level >= 1)
    {
      fprintf (stderr,
	       "LK_DUMP::lk_internal_unlock_object()\n"
	       "  tran(%2d) : oid(%2d|%3d|%3d), class_oid(%2d|%3d|%3d), LOCK(%7s)\n", entry_ptr->tran_index,
	       entry_ptr->res_head->oid.volid, entry_ptr->res_head->oid.pageid, entry_ptr->res_head->oid.slotid,
	       entry_ptr->res_head->class_oid.volid, entry_ptr->res_head->class_oid.pageid,
	       entry_ptr->res_head->class_oid.slotid, LOCK_TO_LOCKMODE_STRING (entry_ptr->granted_mode));
    }
#endif /* LK_DUMP */

  if (entry_ptr == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LK_BAD_ARGUMENT, 2, "lk_internal_unlock_object",
	      "NULL entry pointer");
      return;
    }

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  if (entry_ptr->tran_index != tran_index)
    {
      assert (false);
      return;
    }

  if (release_flag == false)
    {
      entry_ptr->count--;
      if (lock_is_instant_lock_mode (tran_index))
	{
	  entry_ptr->instant_lock_count--;
	  assert (entry_ptr->instant_lock_count >= 0);
	}

      if (entry_ptr->blocked_mode == NULL_LOCK && entry_ptr->count > 0)
	{
	  return;
	}
    }

  /* hold resource mutex */
  res_ptr = entry_ptr->res_head;
  rv = pthread_mutex_lock (&res_ptr->res_mutex);

  /* check if the transaction is in the holder list */
  prev = NULL;
  curr = res_ptr->holder;
  while (curr != NULL)
    {
      if (curr->tran_index == tran_index)
	{
	  break;
	}
      prev = curr;
      curr = curr->next;
    }

  if (curr == NULL)
    {
      /* the transaction is not in the holder list, check the waiter. */
      prev = NULL;
      curr = res_ptr->waiter;
      while (curr != NULL)
	{
	  if (curr->tran_index == tran_index)
	    {
	      break;
	    }
	  prev = curr;
	  curr = curr->next;
	}

      if (curr != NULL)
	{
	  /* get the next lock waiter */
	  from_whom = curr->next;

	  /* remove the lock entry from the waiter */
	  if (prev == NULL)
	    {
	      res_ptr->waiter = curr->next;
	    }
	  else
	    {
	      prev->next = curr->next;
	    }

	  /* free the lock entry */
	  lock_free_entry (tran_index, t_entry, &lk_Gl.obj_free_entry_list, curr);

	  if (from_whom != NULL)
	    {
	      /* grant blocked waiter & change total_waiters_mode */
	      lock_grant_blocked_waiter_partial (thread_p, res_ptr, from_whom);
	    }
	  else
	    {
	      /* change only total_waiters_mode */
	      mode = NULL_LOCK;
	      for (i = res_ptr->waiter; i != NULL; i = i->next)
		{
		  assert (i->blocked_mode >= NULL_LOCK && mode >= NULL_LOCK);
		  mode = lock_Conv[i->blocked_mode][mode];
		  assert (mode != NA_LOCK);
		}
	      res_ptr->total_waiters_mode = mode;
	    }
	}
      else
	{
	  assert (false);
	  /* The transaction is neither the lock holder nor the lock waiter */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LK_LOST_TRANSACTION, 4, tran_index, res_ptr->key.oid.volid,
		  res_ptr->key.oid.pageid, res_ptr->key.oid.slotid);
	}

      pthread_mutex_unlock (&res_ptr->res_mutex);

      return;
    }

  /* The transaction is in the holder list. Consult the holder list. */

  /* remove the entry from the holder list */
  if (prev == NULL)
    {
      res_ptr->holder = curr->next;
    }
  else
    {
      prev->next = curr->next;
    }

  if (release_flag == false && curr->count > 0)
    {
      /* The current transaction was a blocked holder. lock timeout is called or it is selected as a deadlock victim */
      curr->blocked_mode = NULL_LOCK;
      lock_position_holder_entry (res_ptr, entry_ptr);
    }
  else
    {
      /* remove the lock entry from the transaction lock hold list */
      (void) lock_delete_from_tran_hold_list (curr, tran_index);

      /* to manage granules */
      lock_decrement_class_granules (curr->class_entry);

      /* If it's not the end of transaction, it's a non2pl lock */
      if (release_flag == false && move_to_non2pl == true)
	{
	  (void) lock_add_non2pl_lock (thread_p, res_ptr, tran_index, curr->granted_mode);
	}
      /* free the lock entry */
      lock_free_entry (tran_index, t_entry, &lk_Gl.obj_free_entry_list, curr);
    }

  /* change total_holders_mode */
  mode = NULL_LOCK;
  for (i = res_ptr->holder; i != NULL; i = i->next)
    {
      assert (i->granted_mode >= NULL_LOCK && mode >= NULL_LOCK);
      mode = lock_Conv[i->granted_mode][mode];
      assert (mode != NA_LOCK);

      assert (i->blocked_mode >= NULL_LOCK && mode >= NULL_LOCK);
      mode = lock_Conv[i->blocked_mode][mode];
      assert (mode != NA_LOCK);
    }
  res_ptr->total_holders_mode = mode;

  if (res_ptr->holder == NULL && res_ptr->waiter == NULL)
    {
      if (res_ptr->non2pl == NULL)
	{
	  /* if resource entry is empty, remove it. */
	  (void) lock_remove_resource (res_ptr);
	}
      else
	{
	  pthread_mutex_unlock (&res_ptr->res_mutex);
	}
    }
  else
    {
      /* grant blocked holders and blocked waiters */
      lock_grant_blocked_holder (thread_p, res_ptr);

      (void) lock_grant_blocked_waiter (thread_p, res_ptr);
      pthread_mutex_unlock (&res_ptr->res_mutex);
    }
}
#endif /* SERVER_MODE */

/*
 *  Private Functions Group: demote, unlock and remove locks
 *
 *   - lock_internal_demote_shared_class_lock()
 *   - lock_demote_all_shared_class_locks()
 *   - lock_unlock_shared_inst_lock()
 *   - lock_remove_all_class_locks()
 *   - lock_remove_all_inst_locks()
 */

#if defined(SERVER_MODE)
/*
 * lock_internal_demote_shared_class_lock - Demote the shared class lock
 *
 * return: error code
 *
 *   entry_ptr(in):
 *
 * Note:This function demotes the lock mode of given class lock
 *     if the lock mode is shared lock. After the demotion, this function
 *     grants the blocked requestors if the blocked lock mode is grantable.
 *
 *     demote shared class lock (S_LOCK => IS_LOCK, SIX_LOCK => IX_LOCK)
 */
static int
lock_internal_demote_shared_class_lock (THREAD_ENTRY * thread_p, LK_ENTRY * entry_ptr)
{
  LK_RES *res_ptr;		/* lock resource entry pointer */
  LK_ENTRY *check, *i;		/* lock entry pointer */
  LOCK total_mode;
  int rv;

  /* The caller is not holding any mutex */

  res_ptr = entry_ptr->res_head;
  rv = pthread_mutex_lock (&res_ptr->res_mutex);

  /* find the given lock entry in the holder list */
  for (check = res_ptr->holder; check != NULL; check = check->next)
    {
      if (check == entry_ptr)
	{
	  break;
	}
    }
  if (check == NULL)
    {				/* not found */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LK_NOTFOUND_IN_LOCK_HOLDER_LIST, 5,
	      LOCK_TO_LOCKMODE_STRING (entry_ptr->granted_mode), entry_ptr->tran_index, res_ptr->key.oid.volid,
	      res_ptr->key.oid.pageid, res_ptr->key.oid.slotid);
      pthread_mutex_unlock (&res_ptr->res_mutex);
      return ER_LK_NOTFOUND_IN_LOCK_HOLDER_LIST;
    }

#if defined(LK_DUMP)
  if (lk_Gl.dump_level >= 1)
    {
      fprintf (stderr,
	       "LK_DUMP::lk_internal_demote_shared_class_lock()\n"
	       "  tran(%2d) : oid(%d|%d|%d), class_oid(%d|%d|%d), LOCK(%7s -> %7s)\n", entry_ptr->tran_index,
	       entry_ptr->res_head->key.oid.volid, entry_ptr->res_head->key.oid.pageid,
	       entry_ptr->res_head->key.oid.slotid, entry_ptr->res_head->key.class_oid.volid,
	       entry_ptr->res_head->key.class_oid.pageid, entry_ptr->res_head->key.class_oid.slotid,
	       LOCK_TO_LOCKMODE_STRING (entry_ptr->granted_mode),
	       entry_ptr->granted_mode == S_LOCK ? LOCK_TO_LOCKMODE_STRING (IS_LOCK)
	       : (entry_ptr->granted_mode == X_LOCK ? LOCK_TO_LOCKMODE_STRING (IX_LOCK)
		  : LOCK_TO_LOCKMODE_STRING (entry_ptr->granted_mode)));
    }
#endif /* LK_DUMP */

  /* demote the shared class lock(granted mode) of the lock entry */
  switch (check->granted_mode)
    {
    case S_LOCK:
      check->granted_mode = IS_LOCK;
      break;

    case SIX_LOCK:
      check->granted_mode = IX_LOCK;
      break;

    default:
      pthread_mutex_unlock (&res_ptr->res_mutex);
      return NO_ERROR;
    }

  /* change total_holders_mode */
  total_mode = NULL_LOCK;
  for (i = res_ptr->holder; i != NULL; i = i->next)
    {
      assert (i->granted_mode >= NULL_LOCK && total_mode >= NULL_LOCK);
      total_mode = lock_Conv[i->granted_mode][total_mode];
      assert (total_mode != NA_LOCK);

      assert (i->blocked_mode >= NULL_LOCK && total_mode >= NULL_LOCK);
      total_mode = lock_Conv[i->blocked_mode][total_mode];
      assert (total_mode != NA_LOCK);
    }
  res_ptr->total_holders_mode = total_mode;

  /* grant the blocked holders and blocked waiters */
  lock_grant_blocked_holder (thread_p, res_ptr);
  (void) lock_grant_blocked_waiter (thread_p, res_ptr);

  pthread_mutex_unlock (&res_ptr->res_mutex);

  return NO_ERROR;
}
#endif /* SERVER_MODE */

#if defined(SERVER_MODE)
/*
 * lock_demote_read_class_lock_for_checksumdb -  Demote one shared class lock to intention shared only for checksumdb
 *
 * return:
 *
 *   tran_index(in):
 *   class_oid(in):
 *
 * Note: This is exported ONLY for checksumdb. NEVER consider to use this function for any other clients/threads.
 *
 * Note:This function finds the lock entry whose lock object id is same with the given class_oid in the transaction
 *	lock hold list. And then, demote the class lock if the class lock is shared mode.
 */
void
lock_demote_read_class_lock_for_checksumdb (THREAD_ENTRY * thread_p, int tran_index, const OID * class_oid)
{
  LK_ENTRY *entry_ptr;

  /* The caller is not holding any mutex */

  /* demote only one class lock */
  entry_ptr = lock_find_tran_hold_entry (tran_index, class_oid, true);
  if (entry_ptr == NULL)
    {
      assert (entry_ptr != NULL);
      return;
    }

  if (entry_ptr->granted_mode == S_LOCK)
    {
      (void) lock_internal_demote_shared_class_lock (thread_p, entry_ptr);
    }
}
#endif /* SERVER_MODE */

#if defined(SERVER_MODE)
/*
 * lock_demote_all_shared_class_locks - Demote all shared class locks
 *
 * return:
 *
 *   tran_index(in):
 *
 * Note:This function demotes all shared class locks that are held
 *     by the given transaction.
 */
static void
lock_demote_all_shared_class_locks (THREAD_ENTRY * thread_p, int tran_index)
{
  LK_TRAN_LOCK *tran_lock;
  LK_ENTRY *curr, *next;

  /* When this function is called, only one thread is executing for the transaction. (transaction : thread = 1 : 1)
   * Therefore, there is no need to hold tran_lock->hold_mutex. */

  tran_lock = &lk_Gl.tran_lock_table[tran_index];

  /* 1. demote general class locks */
  curr = tran_lock->class_hold_list;
  while (curr != NULL)
    {
      assert (tran_index == curr->tran_index);

      next = curr->tran_next;
      if (curr->granted_mode == S_LOCK || curr->granted_mode == SIX_LOCK)
	{
	  (void) lock_internal_demote_shared_class_lock (thread_p, curr);
	}
      curr = next;
    }

  /* 2. demote root class lock */
  curr = tran_lock->root_class_hold;
  if (curr != NULL)
    {
      assert (tran_index == curr->tran_index);

      if (curr->granted_mode == S_LOCK || curr->granted_mode == SIX_LOCK)
	{
	  (void) lock_internal_demote_shared_class_lock (thread_p, curr);
	}
    }
}
#endif /* SERVER_MODE */

#if defined(SERVER_MODE)
/*
 * lk_unlock_shared_inst_lock - Unlock one shared instance lock
 *
 * return:
 *
 *   tran_index(in):
 *   inst_oid(in):
 *
 * Note:This function finds the lock entry whose lock object id is same
 *     with the given inst_oid in the transaction lock hold list. And then,
 *     unlock the instance lock if the instance lock is shared lock.
 */
static void
lock_unlock_shared_inst_lock (THREAD_ENTRY * thread_p, int tran_index, const OID * inst_oid)
{
  LK_ENTRY *entry_ptr;

  /* unlock the shared instance lock (S_LOCK) */
  entry_ptr = lock_find_tran_hold_entry (tran_index, inst_oid, false);

  if (entry_ptr != NULL && entry_ptr->granted_mode == S_LOCK)
    {
      lock_internal_perform_unlock_object (thread_p, entry_ptr, false, true);
    }
}
#endif /* SERVER_MODE */

#if defined(SERVER_MODE)
/*
 * lock_remove_all_class_locks - Remove class locks whose lock mode is lower than the given lock mode
 *
 * return:
 *
 *   tran_index(in):
 *   lock(in):
 *
 * Note:This function removes class locks whose lock mode is lower than the given lock mode.
 */
static void
lock_remove_all_class_locks (THREAD_ENTRY * thread_p, int tran_index, LOCK lock)
{
  LK_TRAN_LOCK *tran_lock;
  LK_ENTRY *curr, *next;

  /* When this function is called, only one thread is executing for the transaction. (transaction : thread = 1 : 1)
   * Therefore, there is no need to hold tran_lock->hold_mutex. */

  tran_lock = &lk_Gl.tran_lock_table[tran_index];

  /* remove class locks if given condition is satisfied */
  curr = tran_lock->class_hold_list;
  while (curr != NULL)
    {
      assert (tran_index == curr->tran_index);

      next = curr->tran_next;
      if (curr->granted_mode <= lock)
	{
	  lock_internal_perform_unlock_object (thread_p, curr, true, false);
	}
      curr = next;
    }

  /* remove root class lock if given condition is satisfied */
  curr = tran_lock->root_class_hold;
  if (curr != NULL)
    {
      assert (tran_index == curr->tran_index);

      if (curr->granted_mode <= lock)
	{
	  lock_internal_perform_unlock_object (thread_p, curr, true, false);
	}
    }

}
#endif /* SERVER_MODE */

#if defined(SERVER_MODE)
/*
 * lock_remove_all_inst_locks - Remove instance locks whose lock mode is lower than the given lock mode
 *
 * return:
 *
 *   tran_index(in):
 *   class_oid(in):
 *   lock(in):
 *
 * Note:This function removes instance locks whose lock mode is lower than the given lock mode.
 */
void
lock_remove_all_inst_locks (THREAD_ENTRY * thread_p, int tran_index, const OID * class_oid, LOCK lock)
{
  LK_TRAN_LOCK *tran_lock;
  LK_ENTRY *curr, *next;

  tran_lock = &lk_Gl.tran_lock_table[tran_index];

  /* remove instance locks if given condition is satisfied */
  curr = tran_lock->inst_hold_list;
  while (curr != NULL)
    {
      assert (tran_index == curr->tran_index);

      next = curr->tran_next;
      if (class_oid == NULL || OID_ISNULL (class_oid) || OID_EQ (&curr->res_head->key.class_oid, class_oid))
	{
	  if (curr->granted_mode <= lock || lock == X_LOCK)
	    {
	      /* found : the same class_oid and interesting lock mode --> unlock it. */
	      lock_internal_perform_unlock_object (thread_p, curr, true, false);
	    }
	}
      curr = next;
    }

}
#endif /* SERVER_MODE */

/*
 *  Private Functions Group: non two pahse locks
 *
 *   - lk_remove_non2pl()
 *   - lk_update_non2pl_list()
 */

#if defined(SERVER_MODE)
/*
 * lock_remove_non2pl -
 *
 * return:
 *
 *   non2pl(in):
 *   tran_index(in):
 */
static void
lock_remove_non2pl (LK_ENTRY * non2pl, int tran_index)
{
  LF_TRAN_ENTRY *t_entry = thread_get_tran_entry (NULL, THREAD_TS_OBJ_LOCK_ENT);
  LK_RES *res_ptr;
  LK_ENTRY *prev, *curr;
  int rv;

  /* The given non2pl entry has already been removed from the transaction non2pl list. Therefore, This function removes 
   * the given non2pl entry from the resource non2pl list and then frees the entry. */

  res_ptr = non2pl->res_head;
  rv = pthread_mutex_lock (&res_ptr->res_mutex);

  /* find the given non2pl in non2pl list of resource entry */
  prev = NULL;
  curr = res_ptr->non2pl;
  while (curr != NULL)
    {
      if (curr->tran_index == tran_index)
	{
	  break;
	}
      prev = curr;
      curr = curr->next;
    }
  if (curr == NULL)
    {				/* not found */
      pthread_mutex_unlock (&res_ptr->res_mutex);
      return;
    }

  /* found : remove it */
  if (prev == NULL)
    {
      res_ptr->non2pl = curr->next;
    }
  else
    {
      prev->next = curr->next;
    }
  /* (void)lk_delete_from_tran_non2pl_list(curr); */

  /* free the lock entry */
  lock_free_entry (tran_index, t_entry, &lk_Gl.obj_free_entry_list, curr);

  if (res_ptr->holder == NULL && res_ptr->waiter == NULL && res_ptr->non2pl == NULL)
    {
      (void) lock_remove_resource (res_ptr);
    }
  else
    {
      pthread_mutex_unlock (&res_ptr->res_mutex);
    }

}
#endif /* SERVER_MODE */

#if defined(SERVER_MODE)
/*
 * lock_update_non2pl_list -
 *
 * return:
 *
 *   res_ptr(in):
 *   tran_index(in):
 *   lock(in):
 */
static void
lock_update_non2pl_list (THREAD_ENTRY * thread_p, LK_RES * res_ptr, int tran_index, LOCK lock)
{
  LF_TRAN_ENTRY *t_entry = thread_get_tran_entry (thread_p, THREAD_TS_OBJ_LOCK_ENT);
  LK_ENTRY *prev;
  LK_ENTRY *curr;
  LK_ENTRY *next;
  LK_TRAN_LOCK *tran_lock;
  int rv;
  int compat;

  /* The caller is holding a resource mutex */

  prev = NULL;
  curr = res_ptr->non2pl;
  while (curr != NULL)
    {
      if (curr->tran_index == tran_index)
	{			/* same transaction */
	  /* remove current non2pl entry */
	  next = curr->next;
	  if (prev == NULL)
	    {
	      res_ptr->non2pl = curr->next;
	    }
	  else
	    {
	      prev->next = curr->next;
	    }
	  (void) lock_delete_from_tran_non2pl_list (curr, tran_index);
	  lock_free_entry (tran_index, t_entry, &lk_Gl.obj_free_entry_list, curr);
	  curr = next;
	}
      else
	{			/* different transaction */
	  if (curr->granted_mode != INCON_NON_TWO_PHASE_LOCK)
	    {
	      /* The transaction with the released lock must decache the lock object since an incompatible locks has
	       * been acquired. This implies that the transaction with the released lock may not be serializable
	       * (repeatable read consistent) any longer. */
	      assert (lock >= NULL_LOCK && curr->granted_mode >= NULL_LOCK);
	      compat = lock_Comp[lock][curr->granted_mode];
	      assert (compat != DB_NA);

	      if (compat == false)
		{
		  curr->granted_mode = INCON_NON_TWO_PHASE_LOCK;
		  tran_lock = &lk_Gl.tran_lock_table[curr->tran_index];
		  rv = pthread_mutex_lock (&tran_lock->non2pl_mutex);
		  tran_lock->num_incons_non2pl += 1;
		  pthread_mutex_unlock (&tran_lock->non2pl_mutex);
		}
	    }
	  prev = curr;
	  curr = curr->next;
	}
    }

}
#endif /* SERVER_MODE */

/*
 *  Private Functions Group: local deadlock detection and resolution
 *
 *   - lk_add_WFG_edge()
 */

#if defined(SERVER_MODE)
/*
 * lock_add_WFG_edge -
 *
 * return: error code
 *
 *   from_tran_index(in): waiting transaction index
 *   to_tran_index(in): waited transaction index
 *   holder_flag(in): true(if to_tran_index is Holder), false(otherwise)
 *   edge_wait_stime(in):
 *
 * Note:add an edge to WFG which represents that
 *     'from_tran_index' transaction waits for 'to_tran_index' transaction.
 */
static int
lock_add_WFG_edge (int from_tran_index, int to_tran_index, int holder_flag, INT64 edge_wait_stime)
{
  int prev, curr;
  int i;
  int alloc_idx;
  char *temp_ptr;

  /* check if the transactions has been selected as victims */
  /* Note that the transactions might be old deadlock victims */
  if (lk_Gl.TWFG_node[from_tran_index].DL_victim == true || lk_Gl.TWFG_node[to_tran_index].DL_victim == true)
    {
      return NO_ERROR;
    }

  /* increment global edge sequence number */
  lk_Gl.global_edge_seq_num++;

  if (lk_Gl.TWFG_node[from_tran_index].checked_by_deadlock_detector == false)
    {
      /* a new transaction started */
      if (lk_Gl.TWFG_node[from_tran_index].first_edge != -1)
	{
	  prev = -1;
	  curr = lk_Gl.TWFG_node[from_tran_index].first_edge;
	  while (curr != -1)
	    {
	      prev = curr;
	      curr = lk_Gl.TWFG_edge[curr].next;
	    }
	  lk_Gl.TWFG_edge[prev].next = lk_Gl.TWFG_free_edge_idx;
	  lk_Gl.TWFG_free_edge_idx = lk_Gl.TWFG_node[from_tran_index].first_edge;
	  lk_Gl.TWFG_node[from_tran_index].first_edge = -1;
	}
      lk_Gl.TWFG_node[from_tran_index].checked_by_deadlock_detector = true;
      lk_Gl.TWFG_node[from_tran_index].tran_edge_seq_num = lk_Gl.global_edge_seq_num;
    }

  if (lk_Gl.TWFG_node[to_tran_index].checked_by_deadlock_detector == false)
    {
      /* a new transaction started */
      if (lk_Gl.TWFG_node[to_tran_index].first_edge != -1)
	{
	  prev = -1;
	  curr = lk_Gl.TWFG_node[to_tran_index].first_edge;
	  while (curr != -1)
	    {
	      prev = curr;
	      curr = lk_Gl.TWFG_edge[curr].next;
	    }
	  lk_Gl.TWFG_edge[prev].next = lk_Gl.TWFG_free_edge_idx;
	  lk_Gl.TWFG_free_edge_idx = lk_Gl.TWFG_node[to_tran_index].first_edge;
	  lk_Gl.TWFG_node[to_tran_index].first_edge = -1;
	}
      lk_Gl.TWFG_node[to_tran_index].checked_by_deadlock_detector = true;
      lk_Gl.TWFG_node[to_tran_index].tran_edge_seq_num = lk_Gl.global_edge_seq_num;
    }

  /* NOTE the following description.. According to the above code, whenever it is identified that a transaction has
   * been terminated during deadlock detection, the transaction is checked again as a new transaction. And, the current 
   * edge is based on the current active transactions. */

  if (lk_Gl.TWFG_free_edge_idx == -1)
    {				/* too many WFG edges */
      if (lk_Gl.max_TWFG_edge == LK_MIN_TWFG_EDGE_COUNT)
	{
	  lk_Gl.max_TWFG_edge = LK_MID_TWFG_EDGE_COUNT;
	  for (i = LK_MIN_TWFG_EDGE_COUNT; i < lk_Gl.max_TWFG_edge; i++)
	    {
	      lk_Gl.TWFG_edge[i].to_tran_index = -1;
	      lk_Gl.TWFG_edge[i].next = (i + 1);
	    }
	  lk_Gl.TWFG_edge[lk_Gl.max_TWFG_edge - 1].next = -1;
	  lk_Gl.TWFG_free_edge_idx = LK_MIN_TWFG_EDGE_COUNT;
	}
      else if (lk_Gl.max_TWFG_edge == LK_MID_TWFG_EDGE_COUNT)
	{
	  temp_ptr = (char *) lk_Gl.TWFG_edge;
	  lk_Gl.max_TWFG_edge = LK_MAX_TWFG_EDGE_COUNT;
	  lk_Gl.TWFG_edge = (LK_WFG_EDGE *) malloc (SIZEOF_LK_WFG_EDGE * lk_Gl.max_TWFG_edge);
	  if (lk_Gl.TWFG_edge == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
		      (size_t) (SIZEOF_LK_WFG_EDGE * lk_Gl.max_TWFG_edge));
	      return ER_OUT_OF_VIRTUAL_MEMORY;	/* no method */
	    }
	  (void) memcpy ((char *) lk_Gl.TWFG_edge, temp_ptr, (SIZEOF_LK_WFG_EDGE * LK_MID_TWFG_EDGE_COUNT));
	  for (i = LK_MID_TWFG_EDGE_COUNT; i < lk_Gl.max_TWFG_edge; i++)
	    {
	      lk_Gl.TWFG_edge[i].to_tran_index = -1;
	      lk_Gl.TWFG_edge[i].next = (i + 1);
	    }
	  lk_Gl.TWFG_edge[lk_Gl.max_TWFG_edge - 1].next = -1;
	  lk_Gl.TWFG_free_edge_idx = LK_MID_TWFG_EDGE_COUNT;
	}
      else
	{
#if defined(CUBRID_DEBUG)
	  er_log_debug (ARG_FILE_LINE, "So many TWFG edges are used..\n");
#endif /* CUBRID_DEBUG */
	  return ER_FAILED;	/* no method */
	}
    }

  /* allocate free WFG edge */
  alloc_idx = lk_Gl.TWFG_free_edge_idx;
  lk_Gl.TWFG_free_edge_idx = lk_Gl.TWFG_edge[alloc_idx].next;

  /* set WFG edge with given information */
  lk_Gl.TWFG_edge[alloc_idx].to_tran_index = to_tran_index;
  lk_Gl.TWFG_edge[alloc_idx].edge_seq_num = lk_Gl.global_edge_seq_num;
  lk_Gl.TWFG_edge[alloc_idx].holder_flag = holder_flag;
  lk_Gl.TWFG_edge[alloc_idx].edge_wait_stime = edge_wait_stime;

  /* connect the WFG edge into WFG */
  lk_Gl.TWFG_edge[alloc_idx].next = lk_Gl.TWFG_node[from_tran_index].first_edge;
  lk_Gl.TWFG_node[from_tran_index].first_edge = alloc_idx;

  return NO_ERROR;
}
#endif /* SERVER_MODE */

#if defined(SERVER_MODE)
/*
 * lock_select_deadlock_victim -
 *
 * return:
 *
 *   s(in):
 *   t(in):
 *
 * Note:
 */
static void
lock_select_deadlock_victim (THREAD_ENTRY * thread_p, int s, int t)
{
  LK_WFG_NODE *TWFG_node;
  LK_WFG_EDGE *TWFG_edge;
  TRANID tranid;
  TRANID victim_tranid;
  int can_timeout;
  int i, u, v, w, n;
  bool false_dd_cycle = false;
  bool lock_holder_found = false;
  bool inact_trans_found = false;
  int tot_WFG_nodes;
#if defined(CUBRID_DEBUG)
  int num_WFG_nodes;
  int WFG_nidx;
  int tran_index_area[20];
  int *tran_index_set = &tran_index_area[0];
#endif
  char *cycle_info_string;
  char *ptr;
  int num_tran_in_cycle;
  int unit_size = LOG_USERNAME_MAX + MAXHOSTNAMELEN + PATH_MAX + 10;
  char *client_prog_name, *client_user_name, *client_host_name;
  int client_pid;
  int next_node;
  int *tran_index_in_cycle = NULL;
  int victim_tran_index;
  int tran_log_count, victim_tran_log_count;

  /* simple notation */
  TWFG_node = lk_Gl.TWFG_node;
  TWFG_edge = lk_Gl.TWFG_edge;

  /* 
   * check if current deadlock cycle is false deadlock cycle
   */
  tot_WFG_nodes = 0;
  if (TWFG_node[t].current == -1)
    {
      /* old WFG edge : remove it */
      TWFG_edge[TWFG_node[s].current].to_tran_index = -2;
      false_dd_cycle = true;
    }
  else
    {
      if (TWFG_node[t].checked_by_deadlock_detector == false || TWFG_node[t].thrd_wait_stime == 0
	  || (TWFG_node[t].thrd_wait_stime > TWFG_edge[TWFG_node[t].current].edge_wait_stime))
	{
	  /* old transaction, not lockwait state, or incorrect WFG edge */
	  /* remove all outgoing edges */
	  TWFG_node[t].first_edge = -1;
	  TWFG_node[t].current = -1;
	  /* remove incoming edge */
	  TWFG_edge[TWFG_node[s].current].to_tran_index = -2;
	  false_dd_cycle = true;
	}
      else
	{
	  if (TWFG_edge[TWFG_node[s].current].edge_seq_num < TWFG_node[t].tran_edge_seq_num)
	    {
	      /* old WFG edge : remove it */
	      TWFG_edge[TWFG_node[s].current].to_tran_index = -2;
	      false_dd_cycle = true;
	    }
	  else
	    {
	      tot_WFG_nodes += 1;
	    }
	}
    }
  for (v = s; v != t;)
    {
      u = lk_Gl.TWFG_node[v].ancestor;
      if (TWFG_node[v].current == -1)
	{
	  /* old WFG edge : remove it */
	  TWFG_edge[TWFG_node[u].current].to_tran_index = -2;
	  false_dd_cycle = true;
	}
      else
	{
	  if (TWFG_node[v].checked_by_deadlock_detector == false || TWFG_node[v].thrd_wait_stime == 0
	      || (TWFG_node[v].thrd_wait_stime > TWFG_edge[TWFG_node[v].current].edge_wait_stime))
	    {
	      /* old transaction, not lockwait state, or incorrect WFG edge */
	      /* remove all outgoing edges */
	      TWFG_node[v].first_edge = -1;
	      TWFG_node[v].current = -1;
	      /* remove incoming edge */
	      TWFG_edge[TWFG_node[u].current].to_tran_index = -2;
	      false_dd_cycle = true;
	    }
	  else
	    {
	      if (TWFG_edge[TWFG_node[u].current].edge_seq_num < TWFG_node[v].tran_edge_seq_num)
		{
		  /* old WFG edge : remove it */
		  TWFG_edge[TWFG_node[u].current].to_tran_index = -2;
		  false_dd_cycle = true;
		}
	      else
		{
		  tot_WFG_nodes += 1;
		}
	    }
	}
      v = u;
    }

  if (false_dd_cycle == true)
    {				/* clear deadlock cycle */
      for (v = s; v != t;)
	{
	  w = TWFG_node[v].ancestor;
	  TWFG_node[v].ancestor = -1;
	  v = w;
	}
      return;
    }

  /* 
   * Victim Selection Strategy 1) Must be lock holder. 2) Must be active transaction. 3) Prefer a transaction does not
   * have victim priority. 4) Prefer a transaction has written less log records. 5) Prefer a transaction with a closer
   * timeout. 6) Prefer the youngest transaction. */
#if defined(CUBRID_DEBUG)
  num_WFG_nodes = tot_WFG_nodes;
  if (num_WFG_nodes > 20)
    {
      tran_index_set = (int *) malloc (sizeof (int) * num_WFG_nodes);
      if (tran_index_set == NULL)
	{
	  num_WFG_nodes = 20;
	  tran_index_set = &tran_index_area[0];
	}
    }
  WFG_nidx = 0;

  if (TWFG_node[t].checked_by_deadlock_detector == false)
    {
      er_log_debug (ARG_FILE_LINE, "transaction(index=%d) is old in deadlock cycle\n", t);
    }
#endif /* CUBRID_DEBUG */
  if (TWFG_edge[TWFG_node[s].current].holder_flag)
    {
      tranid = logtb_find_tranid (t);
      if (logtb_is_active (thread_p, tranid) == false)
	{
	  victims[victim_count].tran_index = NULL_TRAN_INDEX;
	  inact_trans_found = true;
#if defined(CUBRID_DEBUG)
	  er_log_debug (ARG_FILE_LINE,
			"Inactive transaction is found in a deadlock cycle\n(tran_index=%d, tranid=%d, state=%s)\n",
			t, tranid, log_state_string (logtb_find_state (t)));
	  tran_index_set[WFG_nidx] = t;
	  WFG_nidx += 1;
#endif /* CUBRID_DEBUG */
	}
      else
	{
	  victims[victim_count].tran_index = t;
	  victims[victim_count].tranid = tranid;
	  victims[victim_count].can_timeout = LK_CAN_TIMEOUT (logtb_find_wait_msecs (t));
	  lock_holder_found = true;
	}
    }
  else
    {
      victims[victim_count].tran_index = NULL_TRAN_INDEX;
#if defined(CUBRID_DEBUG)
      tran_index_set[WFG_nidx] = t;
      WFG_nidx += 1;
#endif
    }

  victims[victim_count].tran_index_in_cycle = NULL;
  victims[victim_count].num_trans_in_cycle = 0;

  num_tran_in_cycle = 1;
  for (v = s; v != t; v = TWFG_node[v].ancestor)
    {
      num_tran_in_cycle++;
    }

  cycle_info_string = (char *) malloc (unit_size * num_tran_in_cycle);
  tran_index_in_cycle = (int *) malloc (sizeof (int) * num_tran_in_cycle);

  if (cycle_info_string != NULL && tran_index_in_cycle != NULL)
    {
      int i;

      ptr = cycle_info_string;

      for (i = 0, v = s; i < num_tran_in_cycle; i++, v = TWFG_node[v].ancestor)
	{
	  (void) logtb_find_client_name_host_pid (v, &client_prog_name, &client_user_name, &client_host_name,
						  &client_pid);

	  n =
	    snprintf (ptr, unit_size, "%s%s@%s|%s(%d)", ((v == s) ? "" : ", "), client_user_name, client_host_name,
		      client_prog_name, client_pid);
	  ptr += n;
	  assert_release (ptr < cycle_info_string + unit_size * num_tran_in_cycle);

	  tran_index_in_cycle[i] = v;
	}
    }

  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_LK_DEADLOCK_CYCLE_DETECTED, 1,
	  (cycle_info_string) ? cycle_info_string : "");

  if (cycle_info_string != NULL)
    {
      free_and_init (cycle_info_string);
    }

  for (v = s; v != t;)
    {
#if defined(CUBRID_DEBUG)
      if (TWFG_node[v].checked_by_deadlock_detector == false)
	{
	  er_log_debug (ARG_FILE_LINE, "transaction(index=%d) is old in deadlock cycle\n", v);
	}
#endif /* CUBRID_DEBUG */
      if (TWFG_node[v].candidate == true)
	{
	  tranid = logtb_find_tranid (v);
	  victim_tran_index = victims[victim_count].tran_index;
	  if (logtb_is_active (thread_p, tranid) == false)	/* Must be active transaction. */
	    {
	      inact_trans_found = true;
#if defined(CUBRID_DEBUG)
	      er_log_debug (ARG_FILE_LINE,
			    "Inactive transaction is found in a deadlock cycle\n"
			    "(tran_index=%d, tranid=%d, state=%s)\n", v, tranid,
			    log_state_string (logtb_find_state (v)));
	      tran_index_set[WFG_nidx] = v;
	      WFG_nidx += 1;
#endif /* CUBRID_DEBUG */
	    }
	  else
	    {
	      victim_tranid = NULL_TRANID;
	      lock_holder_found = true;
	      can_timeout = LK_CAN_TIMEOUT (logtb_find_wait_msecs (v));
	      if (victim_tran_index == NULL_TRAN_INDEX)
		{
		  victim_tranid = tranid;
		}
	      else if (logtb_has_deadlock_priority (victim_tran_index) != logtb_has_deadlock_priority (v))
		{
		  /* Prefer a transaction does not have victim priority. */
		  if (logtb_has_deadlock_priority (v) == false)
		    {
		      victim_tranid = tranid;
		    }
		}
	      else
		{
		  tran_log_count = logtb_find_log_records_count (v);
		  victim_tran_log_count = logtb_find_log_records_count (victim_tran_index);

		  if (tran_log_count != victim_tran_log_count)
		    {
		      if (tran_log_count < victim_tran_log_count)
			{
			  /* Prefer a transaction has written less log records. */
			  victim_tranid = tranid;
			}
		    }
		  else
		    {
		      /* 
		       *  Prefer a transaction with a closer timeout.
		       *  Prefer the youngest transaction.
		       */
		      if ((victims[victim_count].can_timeout == false && can_timeout == true)
			  || (victims[victim_count].can_timeout == can_timeout
			      && (LK_ISYOUNGER (tranid, victims[victim_count].tranid))))
			{
			  victim_tranid = tranid;
			}
		    }
		}

	      if (victim_tranid != NULL_TRANID)
		{
		  victims[victim_count].tran_index = v;
		  victims[victim_count].tranid = victim_tranid;
		  victims[victim_count].can_timeout = can_timeout;
		}
	    }
	}
#if defined(CUBRID_DEBUG)
      else
	{			/* TWFG_node[v].candidate == false */
	  tran_index_set[WFG_nidx] = v;
	  WFG_nidx += 1;
	}
#endif
      v = TWFG_node[v].ancestor;
    }

  if (victims[victim_count].tran_index != NULL_TRAN_INDEX)
    {
#if defined(CUBRID_DEBUG)
      if (TWFG_node[victims[victim_count].tran_index].checked_by_deadlock_detector == false)
	{
	  er_log_debug (ARG_FILE_LINE, "victim(index=%d) is old in deadlock cycle\n", victims[victim_count].tran_index);
	}
#endif /* CUBRID_DEBUG */
      TWFG_node[victims[victim_count].tran_index].current = -1;
      victims[victim_count].tran_index_in_cycle = tran_index_in_cycle;
      victims[victim_count].num_trans_in_cycle = num_tran_in_cycle;
      victim_count++;
    }
  else
    {
      /* We can't find active holder. In this case, this cycle is regarded as a false deadlock. */
      for (i = 0, v = s; i < num_tran_in_cycle; v = TWFG_node[v].ancestor, i++)
	{
	  assert_release (TWFG_node[v].current >= 0 && TWFG_node[v].current < lk_Gl.max_TWFG_edge);

	  next_node = TWFG_edge[TWFG_node[v].current].to_tran_index;

	  if (TWFG_node[next_node].checked_by_deadlock_detector == false || TWFG_node[next_node].thrd_wait_stime == 0
	      || TWFG_node[next_node].thrd_wait_stime > TWFG_edge[TWFG_node[next_node].current].edge_wait_stime)
	    {
	      /* The edge from v to next_node is removed(false edge). */
	      TWFG_node[next_node].first_edge = -1;
	      TWFG_node[next_node].current = -1;
	      TWFG_edge[TWFG_node[v].current].to_tran_index = -2;
	      TWFG_node[v].current = TWFG_edge[TWFG_node[v].current].next;
	      break;
	    }
	}

      if (i == num_tran_in_cycle)
	{
	  /* can't find false edge */
	  TWFG_edge[TWFG_node[s].current].to_tran_index = -2;
	  TWFG_node[s].current = TWFG_edge[TWFG_node[s].current].next;
	}

      if (tran_index_in_cycle != NULL)
	{
	  free_and_init (tran_index_in_cycle);
	}

#if defined(CUBRID_DEBUG)
      er_log_debug (ARG_FILE_LINE, "No victim in deadlock cycle....\n");
      if (lock_holder_found == false)
	{
	  er_log_debug (ARG_FILE_LINE, "Any Lock holder is not found in deadlock cycle.\n");
	}
      if (inact_trans_found == true)
	{
	  er_log_debug (ARG_FILE_LINE, "Inactive transactions are found in deadlock cycle.\n");
	}
      er_log_debug (ARG_FILE_LINE, "total_edges=%d, free_edge_idx=%d, global_edge_seq=%d\n", lk_Gl.max_TWFG_edge,
		    lk_Gl.TWFG_free_edge_idx, lk_Gl.global_edge_seq_num);
      er_log_debug (ARG_FILE_LINE, "# of WFG nodes in deadlock cycle = %d (%d printed)\n", tot_WFG_nodes,
		    num_WFG_nodes);
      for (WFG_nidx = 0; WFG_nidx < num_WFG_nodes; WFG_nidx++)
	{
	  er_log_debug (ARG_FILE_LINE, "%3d ", tran_index_set[WFG_nidx]);
	  if ((WFG_nidx + 1) == num_WFG_nodes || (WFG_nidx % 10) == 9)
	    {
	      er_log_debug (ARG_FILE_LINE, "\n");
	    }
	}
#endif /* CUBRID_DEBUG */
    }

  for (v = s; v != t;)
    {
      w = TWFG_node[v].ancestor;
      TWFG_node[v].ancestor = -1;
      v = w;
    }
}
#endif /* SERVER_MODE */

#if defined(SERVER_MODE)
/*
 * lock_dump_deadlock_victims -
 *
 * return:
 */
static void
lock_dump_deadlock_victims (THREAD_ENTRY * thread_p, FILE * outfile)
{
  int k, count;

  fprintf (outfile, "*** Deadlock Victim Information ***\n");
  fprintf (outfile, "Victim count = %d\n", victim_count);
  /* print aborted transactions (deadlock victims) */
  fprintf (outfile, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOCK, MSGCAT_LK_DEADLOCK_ABORT_HDR));
  count = 0;
  for (k = 0; k < victim_count; k++)
    {
      if (!victims[k].can_timeout)
	{
	  fprintf (outfile, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOCK, MSGCAT_LK_DEADLOCK_ABORT),
		   victims[k].tran_index);
	  if ((count % 10) == 9)
	    {
	      fprintf (outfile, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOCK, MSGCAT_LK_NEWLINE));
	    }
	  count++;
	}
    }
  fprintf (outfile, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOCK, MSGCAT_LK_NEWLINE));
  /* print timeout transactions (deadlock victims) */
  fprintf (outfile, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOCK, MSGCAT_LK_DEADLOCK_TIMEOUT_HDR));
  count = 0;
  for (k = 0; k < victim_count; k++)
    {
      if (victims[k].can_timeout)
	{
	  fprintf (outfile, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOCK, MSGCAT_LK_DEADLOCK_TIMEOUT),
		   victims[k].tran_index);
	  if ((count % 10) == 9)
	    {
	      fprintf (outfile, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOCK, MSGCAT_LK_NEWLINE));
	    }
	  count++;
	}
    }

  xlock_dump (thread_p, outfile);
}
#endif /* SERVER_MODE */

/*
 *  Private Functions Group: miscellaneous functions
 *
 *   - lk_lockinfo_compare()
 *   - lk_dump_res()
 *   - lk_consistent_res()
 *   - lk_consistent_tran_lock()
 */

#if defined(SERVER_MODE)
/*
 * lock_compare_lock_info -
 *
 * return:
 *
 *   lockinfo1(in):
 *   lockinfo2(in):
 *
 * Note:compare two OID of lockable objects.
 */
static int
lock_compare_lock_info (const void *lockinfo1, const void *lockinfo2)
{
  const OID *oid1;
  const OID *oid2;

  oid1 = &(((LK_LOCKINFO *) (lockinfo1))->oid);
  oid2 = &(((LK_LOCKINFO *) (lockinfo2))->oid);

  return oid_compare (oid1, oid2);
}
#endif /* SERVER_MODE */

#if defined(SERVER_MODE)
/*
 * lock_wait_msecs_to_secs -
 *
 * return: seconds
 *
 *   msecs(in): milliseconds
 */
static float
lock_wait_msecs_to_secs (int msecs)
{
  if (msecs > 0)
    {
      return (float) msecs / 1000;
    }

  return (float) msecs;
}
#endif /* SERVER_MODE */

#if defined(SERVER_MODE)
/*
 * lock_dump_resource - Dump locks acquired on a resource
 *
 * return:
 *
 *   outfp(in): FILE stream where to dump the lock resource entry.
 *   res_ptr(in): pointer to lock resource entry
 *
 * Note:Dump contents of the lock resource entry pointed by res_ptr.
 */
static void
lock_dump_resource (THREAD_ENTRY * thread_p, FILE * outfp, LK_RES * res_ptr)
{
#define TEMP_BUFFER_SIZE  128
  LK_ENTRY *entry_ptr;
  char *classname;		/* Name of the class */
  int num_holders, num_blocked_holders, num_waiters;
  char time_val[CTIME_MAX];
  int time_str_len;
  OID *oid_rr = NULL;
  HEAP_SCANCACHE scan_cache;
  OID real_class_oid;

  memset (time_val, 0, sizeof (time_val));

  /* dump object identifier */
  fprintf (outfp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOCK, MSGCAT_LK_RES_OID), res_ptr->key.oid.volid,
	   res_ptr->key.oid.pageid, res_ptr->key.oid.slotid);

  /* dump object type related information */
  switch (res_ptr->key.type)
    {
    case LOCK_RESOURCE_ROOT_CLASS:
      fprintf (outfp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOCK, MSGCAT_LK_RES_ROOT_CLASS_TYPE));
      break;
    case LOCK_RESOURCE_CLASS:
      oid_rr = oid_get_rep_read_tran_oid ();
      if (oid_rr != NULL && OID_EQ (&res_ptr->key.oid, oid_rr))
	{
	  /* This is the generic object for RR transactions */
	  fprintf (outfp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOCK, MSGCAT_LK_RES_RR_TYPE));
	}
      else if (!OID_ISTEMP (&res_ptr->key.oid))
	{
	  if (OID_IS_VIRTUAL_CLASS_OF_DIR_OID (&res_ptr->key.oid))
	    {
	      OID_GET_REAL_CLASS_OF_DIR_OID (&res_ptr->key.oid, &real_class_oid);
	    }
	  else
	    {
	      COPY_OID (&real_class_oid, &res_ptr->key.oid);
	    }
	  /* Don't get class names for temporary class objects. */
	  if (heap_get_class_name (thread_p, &real_class_oid, &classname) != NO_ERROR || classname == NULL)
	    {
	      /* We must stop processing if an interrupt occurs */
	      if (er_errid () == ER_INTERRUPTED)
		{
		  return;
		}

	      /* Otherwise continue */
	      er_clear ();
	    }
	  else
	    {
	      fprintf (outfp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOCK, MSGCAT_LK_RES_CLASS_TYPE),
		       classname);
	      free_and_init (classname);
	    }
	}
      break;
    case LOCK_RESOURCE_INSTANCE:
      if (!OID_ISTEMP (&res_ptr->key.class_oid))
	{
	  bool is_virtual_directory_oid;

	  /* Don't get class names for temporary class objects. */
	  if (OID_IS_VIRTUAL_CLASS_OF_DIR_OID (&res_ptr->key.class_oid))
	    {
	      is_virtual_directory_oid = true;
	      OID_GET_REAL_CLASS_OF_DIR_OID (&res_ptr->key.class_oid, &real_class_oid);
	    }
	  else
	    {
	      is_virtual_directory_oid = false;
	      COPY_OID (&real_class_oid, &res_ptr->key.class_oid);
	    }

	  if (heap_get_class_name (thread_p, &real_class_oid, &classname) != NO_ERROR || classname == NULL)
	    {
	      /* We must stop processing if an interrupt occurs */
	      if (er_errid () == ER_INTERRUPTED)
		{
		  return;
		}

	      /* Otherwise continue */
	      er_clear ();
	    }
	  else
	    {
	      fprintf (outfp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOCK, MSGCAT_LK_RES_INSTANCE_TYPE),
		       res_ptr->key.class_oid.volid, res_ptr->key.class_oid.pageid, res_ptr->key.class_oid.slotid,
		       classname);
	      free_and_init (classname);
	    }

	  /* Dump MVCC info */
	  if (is_virtual_directory_oid == false && heap_scancache_quick_start (&scan_cache) == NO_ERROR)
	    {
	      RECDES recdes;

	      recdes.data = NULL;

	      if (heap_get_visible_version (thread_p, &res_ptr->key.oid, &res_ptr->key.class_oid, &recdes, &scan_cache,
					    PEEK, NULL_CHN) == S_SUCCESS)
		{
		  MVCC_REC_HEADER mvcc_rec_header;
		  if (or_mvcc_get_header (&recdes, &mvcc_rec_header) == NO_ERROR)
		    {
		      char str_insid[128], str_delid[128];
		      if (MVCC_IS_FLAG_SET (&mvcc_rec_header, OR_MVCC_FLAG_VALID_INSID))
			{
			  sprintf (str_insid, "%llu", (unsigned long long int) MVCC_GET_INSID (&mvcc_rec_header));
			}
		      else
			{
			  strcpy (str_insid, "missing");
			}
		      if (MVCC_IS_HEADER_DELID_VALID (&mvcc_rec_header))
			{
			  sprintf (str_delid, "%llu", (unsigned long long int) MVCC_GET_DELID (&mvcc_rec_header));
			}
		      else
			{
			  strcpy (str_delid, "missing");
			}
		      fprintf (outfp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOCK, MSGCAT_LK_MVCC_INFO),
			       str_insid, str_delid);
		    }
		}
	      heap_scancache_end (thread_p, &scan_cache);
	    }
	}
      break;
    default:
      fprintf (outfp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOCK, MSGCAT_LK_RES_UNKNOWN_TYPE));
    }

  /* dump total modes of holders and waiters */
  fprintf (outfp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOCK, MSGCAT_LK_RES_TOTAL_MODE),
	   LOCK_TO_LOCKMODE_STRING (res_ptr->total_holders_mode),
	   LOCK_TO_LOCKMODE_STRING (res_ptr->total_waiters_mode));

  num_holders = num_blocked_holders = 0;
  if (res_ptr->holder != NULL)
    {
      entry_ptr = res_ptr->holder;
      while (entry_ptr != NULL)
	{
	  if (entry_ptr->blocked_mode == NULL_LOCK)
	    {
	      num_holders++;
	    }
	  else
	    {
	      num_blocked_holders++;
	    }
	  entry_ptr = entry_ptr->next;
	}
    }
  num_waiters = 0;
  if (res_ptr->waiter != NULL)
    {
      entry_ptr = res_ptr->waiter;
      while (entry_ptr != NULL)
	{
	  num_waiters++;
	  entry_ptr = entry_ptr->next;
	}
    }

  fprintf (outfp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOCK, MSGCAT_LK_RES_LOCK_COUNT), num_holders,
	   num_blocked_holders, num_waiters);

  /* dump holders */
  if (num_holders > 0)
    {
      /* dump non blocked holders */
      fprintf (outfp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOCK, MSGCAT_LK_RES_NON_BLOCKED_HOLDER_HEAD));
      entry_ptr = res_ptr->holder;
      while (entry_ptr != NULL)
	{
	  if (entry_ptr->blocked_mode == NULL_LOCK)
	    {
	      if (res_ptr->key.type == LOCK_RESOURCE_INSTANCE)
		{
		  fprintf (outfp,
			   msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOCK,
					   MSGCAT_LK_RES_NON_BLOCKED_HOLDER_ENTRY), "", entry_ptr->tran_index,
			   LOCK_TO_LOCKMODE_STRING (entry_ptr->granted_mode), entry_ptr->count);
		}
	      else
		{
		  fprintf (outfp,
			   msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOCK,
					   MSGCAT_LK_RES_NON_BLOCKED_HOLDER_ENTRY_WITH_GRANULE), "",
			   entry_ptr->tran_index, LOCK_TO_LOCKMODE_STRING (entry_ptr->granted_mode), entry_ptr->count,
			   entry_ptr->ngranules);
		}
	    }
	  entry_ptr = entry_ptr->next;
	}
    }

  if (num_blocked_holders > 0)
    {
      /* dump blocked holders */
      fprintf (outfp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOCK, MSGCAT_LK_RES_BLOCKED_HOLDER_HEAD));
      entry_ptr = res_ptr->holder;
      while (entry_ptr != NULL)
	{
	  if (entry_ptr->blocked_mode != NULL_LOCK)
	    {
	      time_t stime = (time_t) (entry_ptr->thrd_entry->lockwait_stime / 1000LL);
	      if (ctime_r (&stime, time_val) == NULL)
		{
		  strcpy (time_val, "???");
		}

	      time_str_len = strlen (time_val);
	      if (time_str_len > 0 && time_val[time_str_len - 1] == '\n')
		{
		  time_val[time_str_len - 1] = 0;
		}
	      if (res_ptr->key.type == LOCK_RESOURCE_INSTANCE)
		{
		  fprintf (outfp,
			   msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOCK, MSGCAT_LK_RES_BLOCKED_HOLDER_ENTRY),
			   "", entry_ptr->tran_index, LOCK_TO_LOCKMODE_STRING (entry_ptr->granted_mode),
			   entry_ptr->count, "", LOCK_TO_LOCKMODE_STRING (entry_ptr->blocked_mode), "", time_val, "",
			   lock_wait_msecs_to_secs (entry_ptr->thrd_entry->lockwait_msecs));
		}
	      else
		{
		  fprintf (outfp,
			   msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOCK,
					   MSGCAT_LK_RES_BLOCKED_HOLDER_ENTRY_WITH_GRANULE), "", entry_ptr->tran_index,
			   LOCK_TO_LOCKMODE_STRING (entry_ptr->granted_mode), entry_ptr->count, entry_ptr->ngranules,
			   "", LOCK_TO_LOCKMODE_STRING (entry_ptr->blocked_mode), "", time_val, "",
			   lock_wait_msecs_to_secs (entry_ptr->thrd_entry->lockwait_msecs));
		}
	    }
	  entry_ptr = entry_ptr->next;
	}
    }

  /* dump blocked waiters */
  if (res_ptr->waiter != NULL)
    {
      fprintf (outfp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOCK, MSGCAT_LK_RES_BLOCKED_WAITER_HEAD));
      entry_ptr = res_ptr->waiter;
      while (entry_ptr != NULL)
	{
	  time_t stime = (time_t) (entry_ptr->thrd_entry->lockwait_stime / 1000LL);
	  (void) ctime_r (&stime, time_val);

	  time_str_len = strlen (time_val);
	  if (time_str_len > 0 && time_val[time_str_len - 1] == '\n')
	    {
	      time_val[time_str_len - 1] = 0;
	    }
	  fprintf (outfp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOCK, MSGCAT_LK_RES_BLOCKED_WAITER_ENTRY),
		   "", entry_ptr->tran_index, LOCK_TO_LOCKMODE_STRING (entry_ptr->blocked_mode), "", time_val, "",
		   lock_wait_msecs_to_secs (entry_ptr->thrd_entry->lockwait_msecs));
	  entry_ptr = entry_ptr->next;
	}
    }

  /* dump non two phase locks */
  if (res_ptr->non2pl != NULL)
    {
      fprintf (outfp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOCK, MSGCAT_LK_RES_NON2PL_RELEASED_HEAD));
      entry_ptr = res_ptr->non2pl;
      while (entry_ptr != NULL)
	{
	  fprintf (outfp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOCK, MSGCAT_LK_RES_NON2PL_RELEASED_ENTRY),
		   "", entry_ptr->tran_index,
		   ((entry_ptr->granted_mode == INCON_NON_TWO_PHASE_LOCK) ? "INCON_NON_TWO_PHASE_LOCK"
		    : LOCK_TO_LOCKMODE_STRING (entry_ptr->granted_mode)));
	  entry_ptr = entry_ptr->next;
	}
    }
  fprintf (outfp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOCK, MSGCAT_LK_NEWLINE));

}
#endif /* SERVER_MODE */

#if defined(ENABLE_UNUSED_FUNCTION)
#if defined(SERVER_MODE)
/*
 * lock_check_consistent_resource - Check if the lock resource entry is consistent
 *
 * return: true/false
 *
 *   res_ptr(in):
 *
 * Note:Verify if a lock entry is consistent. At least one waiter must be
 *     waiting on at least one transaction holding a lock, otherwise,
 *     an inconsistent entry exist. Some waiters may be waiting on waiters.
 */
static bool
lock_check_consistent_resource (THREAD_ENTRY * thread_p, LK_RES * res_ptr)
{
  LOCK total_mode;
  LK_ENTRY *i, *j;
  const char *msg_str;
  int rv;

  /* hold resource mutex */
  rv = pthread_mutex_lock (&res_ptr->res_mutex);

  /* check total_holders_mode (total mode of lock holders) */
  total_mode = NULL_LOCK;
  for (i = res_ptr->holder; i != NULL; i = i->next)
    {
      assert (i->granted_mode >= NULL_LOCK && total_mode >= NULL_LOCK);
      total_mode = lock_Conv[i->granted_mode][total_mode];
      assert (total_mode != NA_LOCK);

      assert (i->blocked_mode >= NULL_LOCK && total_mode >= NULL_LOCK);
      total_mode = lock_Conv[i->blocked_mode][total_mode];
      assert (total_mode != NA_LOCK);
    }
  if (total_mode != res_ptr->total_holders_mode)
    {
      msg_str = "lk_consistent_res: total mode of holders is inconsistent.";
      goto inconsistent;
    }

  /* check total_waiters_mode (total mode of lock waiters) */
  total_mode = NULL_LOCK;
  for (i = res_ptr->waiter; i != NULL; i = i->next)
    {
      assert (i->blocked_mode >= NULL_LOCK && total_mode >= NULL_LOCK);
      total_mode = lock_Conv[i->blocked_mode][total_mode];
      assert (total_mode != NA_LOCK);
    }
  if (total_mode != res_ptr->total_waiters_mode)
    {
      msg_str = "lk_consistent_res: total mode of waiters is incons.";
      goto inconsistent;
    }

  /* check holders : lock information */
  for (i = res_ptr->holder; i != NULL; i = i->next)
    {
      /* check res_head */
      if (i->res_head != res_ptr)
	{
	  msg_str = "lk_consistent_res: res_head of a holder is incons.";
	  goto inconsistent;
	}
      /* check tran_index */
      for (j = res_ptr->holder; j != NULL && j != i; j = j->next)
	{
	  if (j->tran_index == i->tran_index)
	    {
	      msg_str = "lk_consistent_res: tran_index of a holder is incons. (1)";
	      goto inconsistent;
	    }
	}
      for (j = res_ptr->waiter; j != NULL; j = j->next)
	{
	  if (j->tran_index == i->tran_index)
	    {
	      msg_str = "lk_consistent_res: tran_index of a holder is incons. (2)";
	      goto inconsistent;
	    }
	}
      for (j = res_ptr->non2pl; j != NULL; j = j->next)
	{
	  if (j->tran_index == i->tran_index)
	    {
	      msg_str = "lk_consistent_res: tran_index of a holder is incons. (3)";
	      goto inconsistent;
	    }
	}
      /* check thrd_entry */
      if (i->blocked_mode != NULL_LOCK)
	{
	  if (i->thrd_entry == NULL)
	    {
	      msg_str = "lk_consistent_res: thrd_entry of a blocked holder is incons.";
	      goto inconsistent;
	    }
	}
    }

  /* check lock waiters */
  for (i = res_ptr->waiter; i != NULL; i = i->next)
    {
      /* check res_head */
      if (i->res_head != res_ptr)
	{
	  msg_str = "lk_consistent_res: res_head of a waiter is incons.";
	  goto inconsistent;
	}
      /* check tran_index */
      for (j = res_ptr->holder; j != NULL; j = j->next)
	{
	  if (j->tran_index == i->tran_index)
	    {
	      msg_str = "lk_consistent_res: tran_index of a waiter is incons. (1)";
	      goto inconsistent;
	    }
	}
      for (j = res_ptr->waiter; j != NULL && j != i; j = j->next)
	{
	  if (j->tran_index == i->tran_index)
	    {
	      msg_str = "lk_consistent_res: tran_index of a waiter is incons. (2)";
	      goto inconsistent;
	    }
	}
      for (j = res_ptr->non2pl; j != NULL; j = j->next)
	{
	  if (j->tran_index == i->tran_index)
	    {
	      msg_str = "lk_consistent_res: tran_index of a waiter is incons. (3)";
	      goto inconsistent;
	    }
	}
      /* check lock mode */
      if (i->blocked_mode == NULL_LOCK)
	{
	  msg_str = "lk_consistent_res: blocked_mode of a waiter is incons.";
	  goto inconsistent;
	}
      /* check thrd_entry */
      if (i->thrd_entry == NULL)
	{
	  msg_str = "lk_consistent_res: thrd_entry of a waiter is incons.";
	  goto inconsistent;
	}
    }

  /* check non2pl locks */
  for (i = res_ptr->non2pl; i != NULL; i = i->next)
    {
      /* check tran_index */
      for (j = res_ptr->holder; j != NULL; j = j->next)
	{
	  if (j->tran_index == i->tran_index)
	    {
	      msg_str = "lk_consistent_res: tran_index of a non2pl is incons. (1)";
	      goto inconsistent;
	    }
	}
      for (j = res_ptr->waiter; j != NULL; j = j->next)
	{
	  if (j->tran_index == i->tran_index)
	    {
	      msg_str = "lk_consistent_res: tran_index of a non2pl is incons. (2)";
	      goto inconsistent;
	    }
	}
      for (j = res_ptr->non2pl; j != NULL && j != i; j = j->next)
	{
	  if (j->tran_index == i->tran_index)
	    {
	      msg_str = "lk_consistent_res: tran_index of a non2pl is incons. (3)";
	      goto inconsistent;
	    }
	}
    }

  /* consistent */
  pthread_mutex_unlock (&res_ptr->res_mutex);
  return true;

inconsistent:
  lock_dump_resource (thread_p, stdout, res_ptr);
  pthread_mutex_unlock (&res_ptr->res_mutex);
  er_log_debug (ARG_FILE_LINE, msg_str);
  return false;
}
#endif /* SERVER_MODE */

#if defined(SERVER_MODE)
/*
 * lock_check_consistent_tran_lock - Check consistency of transaction lock info entry
 *
 * return: true/false
 *
 *   tran_lock(in): ponter to the transaction lock information entry
 *
 * Note:check if the given transaction lock information enrtry is consistent.
 */
static bool
lock_check_consistent_tran_lock (LK_TRAN_LOCK * tran_lock)
{
  int count;
  LK_ENTRY *i;
  int rv;

  /* hold transaction hold mutex */
  rv = pthread_mutex_lock (&tran_lock->hold_mutex);

  /* check held locks */
  /* check inst_hold_count and class_hold_count */
  for (count = 0, i = tran_lock->inst_hold_list; i != NULL; i = i->tran_next)
    {
      count++;
    }
  if (count != tran_lock->inst_hold_count)
    {
      pthread_mutex_unlock (&tran_lock->hold_mutex);
      er_log_debug (ARG_FILE_LINE, "lk_consistent_tran_lock: inst_hold_count incorrect.");
      return false;
    }
  for (count = 0, i = tran_lock->class_hold_list; i != NULL; i = i->tran_next)
    {
      count++;
    }
  if (count != tran_lock->class_hold_count)
    {
      pthread_mutex_unlock (&tran_lock->hold_mutex);
      er_log_debug (ARG_FILE_LINE, "lk_consistent_tran_lock: class_hold_count incorrect.");
      return false;
    }

  /* release transaction hold mutex */
  pthread_mutex_unlock (&tran_lock->hold_mutex);

  /* hold transaction non2pl mutex */
  rv = pthread_mutex_lock (&tran_lock->non2pl_mutex);

  /* check non2pl locks */
  /* check num_incons_non2pl */
  for (count = 0, i = tran_lock->non2pl_list; i != NULL; i = i->tran_next)
    {
      if (i->granted_mode == INCON_NON_TWO_PHASE_LOCK)
	{
	  count++;
	}
    }
  if (count != tran_lock->num_incons_non2pl)
    {
      pthread_mutex_unlock (&tran_lock->non2pl_mutex);
      er_log_debug (ARG_FILE_LINE, "lk_consistent_tran_lock: num_incons_non2pl incorrect.");
      return false;
    }

  /* release transactino non2pl mutex */
  pthread_mutex_unlock (&tran_lock->non2pl_mutex);

  /* consistent */
  return true;
}
#endif /* SERVER_MODE */
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * lock_initialize - Initialize the lock manager
 *
 * return: error code
 *
 *   estimate_nobj_locks(in): estimate_nobj_locks(useless)
 *
 * Note:Initialize the lock manager memory structures.
 */
int
lock_initialize (void)
{
#if !defined (SERVER_MODE)
  lk_Standalone_has_xlock = false;
  return NO_ERROR;
#else /* !SERVER_MODE */
  const char *env_value;
  int error_code = NO_ERROR;

  error_code = lock_initialize_tran_lock_table ();
  if (error_code != NO_ERROR)
    {
      goto error;
    }
  error_code = lock_initialize_object_hash_table ();
  if (error_code != NO_ERROR)
    {
      goto error;
    }
  error_code = lock_initialize_object_lock_res_list ();
  if (error_code != NO_ERROR)
    {
      goto error;
    }
  error_code = lock_initialize_object_lock_entry_list ();
  if (error_code != NO_ERROR)
    {
      goto error;
    }
  error_code = lock_initialize_deadlock_detection ();
  if (error_code != NO_ERROR)
    {
      goto error;
    }

  /* initialize some parameters */
#if defined(CUBRID_DEBUG)
  lk_Gl.verbose_mode = true;
  lk_Gl.no_victim_case_count = 0;
#else /* !CUBRID_DEBUG */
  env_value = envvar_get ("LK_VERBOSE_SUSPENDED");
  if (env_value != NULL)
    {
      lk_Gl.verbose_mode = (bool) atoi (env_value);
      if (lk_Gl.verbose_mode != false)
	{
	  lk_Gl.verbose_mode = true;
	}
    }
  lk_Gl.no_victim_case_count = 0;
#endif /* !CUBRID_DEBUG */

#if defined(LK_DUMP)
  lk_Gl.dump_level = 0;
  env_value = envvar_get ("LK_DUMP_LEVEL");
  if (env_value != NULL)
    {
      lk_Gl.dump_level = atoi (env_value);
      if (lk_Gl.dump_level < 0 || lk_Gl.dump_level > 3)
	{
	  lk_Gl.dump_level = 0;
	}
    }
#endif /* LK_DUMP */

  return error_code;

error:
  (void) lock_finalize ();
  return error_code;
#endif /* !SERVER_MODE */
}

/*
 * lock_finalize - Finalize the lock manager
 *
 * return: nothing
 *
 * Note:This function finalize the lock manager.
 *     Memory structures of the lock manager are deallocated.
 */
void
lock_finalize (void)
{
#if !defined (SERVER_MODE)
  lk_Standalone_has_xlock = false;
#else /* !SERVER_MODE */
  LK_TRAN_LOCK *tran_lock;
  int i;

  /* Release all the locks and awake all transactions */
  /* TODO: Why ? */
  /* transaction deadlock information table */
  /* deallocate memory space for the transaction deadlock info. */
  if (lk_Gl.TWFG_node != NULL)
    {
      free_and_init (lk_Gl.TWFG_node);
    }

  /* transaction lock information table */
  /* deallocate memory space for transaction lock table */
  if (lk_Gl.tran_lock_table != NULL)
    {
      for (i = 0; i < lk_Gl.num_trans; i++)
	{
	  tran_lock = &lk_Gl.tran_lock_table[i];
	  pthread_mutex_destroy (&tran_lock->hold_mutex);
	  pthread_mutex_destroy (&tran_lock->non2pl_mutex);
	  while (tran_lock->lk_entry_pool != NULL)
	    {
	      LK_ENTRY *entry = tran_lock->lk_entry_pool;
	      tran_lock->lk_entry_pool = tran_lock->lk_entry_pool->next;
	      free (entry);
	    }
	}
      free_and_init (lk_Gl.tran_lock_table);
    }
  /* reset the number of transactions */
  lk_Gl.num_trans = 0;
  pthread_mutex_destroy (&lk_Gl.DL_detection_mutex);

  /* reset max number of object locks */
  lk_Gl.max_obj_locks = 0;

  /* destroy hash table and freelists */
  lf_hash_destroy (&lk_Gl.obj_hash_table);
  lf_freelist_destroy (&lk_Gl.obj_free_entry_list);
  lf_freelist_destroy (&lk_Gl.obj_free_res_list);
#endif /* !SERVER_MODE */
}

/*
 * lock_hold_object_instant - Hold object lock with instant duration
 *
 * return: one of following values
 *     LK_GRANTED
 *     LK_NOTGRANTED
 *     LK_NOTGRANTED_DUE_ERROR
 *
 *   oid(in):
 *   class_oid(in):
 *   lock(in):
 */
int
lock_hold_object_instant (THREAD_ENTRY * thread_p, const OID * oid, const OID * class_oid, LOCK lock)
{
#if !defined (SERVER_MODE)
  LK_SET_STANDALONE_XLOCK (lock);
  return LK_GRANTED;
#else /* !SERVER_MODE */
  int tran_index;
  if (oid == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LK_BAD_ARGUMENT, 2, "lk_object_instant", "NULL OID pointer");
      return LK_NOTGRANTED_DUE_ERROR;
    }
  if (class_oid == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LK_BAD_ARGUMENT, 2, "lk_object_instant", "NULL ClassOID pointer");
      return LK_NOTGRANTED_DUE_ERROR;
    }
  if (lock == NULL_LOCK)
    {
      return LK_GRANTED;
    }

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  return lock_internal_hold_lock_object_instant (tran_index, oid, class_oid, lock);

#endif /* !SERVER_MODE */
}

/*
 * lock_object - Lock an object
 *
 * return: one of following values)
 *     LK_GRANTED
 *     LK_NOTGRANTED_DUE_ABORTED
 *     LK_NOTGRANTED_DUE_TIMEOUT
 *     LK_NOTGRANTED_DUE_ERROR
 *
 *   oid(in): Identifier of object(instance, class, root class) to lock
 *   class_oid(in): Identifier of the class instance of the given object
 *   lock(in): Requested lock mode
 *   cond_flag(in):
 *
 */
int
lock_object (THREAD_ENTRY * thread_p, const OID * oid, const OID * class_oid, LOCK lock, int cond_flag)
{
#if !defined (SERVER_MODE)
  LK_SET_STANDALONE_XLOCK (lock);
  return LK_GRANTED;
#else /* !SERVER_MODE */
  int tran_index;
  int wait_msecs;
  TRAN_ISOLATION isolation;
  LOCK new_class_lock;
  LOCK old_class_lock;
  int granted;
  LK_ENTRY *root_class_entry = NULL;
  LK_ENTRY *class_entry = NULL, *superclass_entry = NULL;
  LK_ENTRY *inst_entry = NULL;
#if defined (EnableThreadMonitoring)
  TSC_TICKS start_tick, end_tick;
  TSCTIMEVAL elapsed_time;
#endif

  if (oid == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LK_BAD_ARGUMENT, 2, "lock_object", "NULL OID pointer");
      return LK_NOTGRANTED_DUE_ERROR;
    }
  if (class_oid == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LK_BAD_ARGUMENT, 2, "lock_object", "NULL ClassOID pointer");
      return LK_NOTGRANTED_DUE_ERROR;
    }


  if (lock == NULL_LOCK)
    {
      return LK_GRANTED;
    }

#if defined (EnableThreadMonitoring)
  if (0 < prm_get_integer_value (PRM_ID_MNT_WAITING_THREAD))
    {
      tsc_getticks (&start_tick);
    }
#endif

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  if (cond_flag == LK_COND_LOCK)	/* conditional request */
    {
      wait_msecs = LK_FORCE_ZERO_WAIT;
    }
  else
    {
      wait_msecs = logtb_find_wait_msecs (tran_index);
    }
  isolation = logtb_find_isolation (tran_index);

  /* check if the given oid is root class oid */
  if (OID_IS_ROOTOID (oid))
    {
      /* case 1 : resource type is LOCK_RESOURCE_ROOT_CLASS acquire a lock on the root class oid. NOTE that in case of
       * acquiring a lock on a class object, the higher lock granule of the class object must not be given. */
      granted = lock_internal_perform_lock_object (thread_p, tran_index, oid, NULL, lock, wait_msecs,
						   &root_class_entry, NULL);
      goto end;
    }

  /* get the intentional lock mode to be acquired on class oid */
  if (lock <= S_LOCK)
    {
      new_class_lock = IS_LOCK;
    }
  else
    {
      new_class_lock = IX_LOCK;
    }

  /* Check if current transaction has already held the class lock. If the class lock is not held, hold the class lock,
   * now. */
  class_entry = lock_get_class_lock (class_oid, tran_index);
  old_class_lock = (class_entry) ? class_entry->granted_mode : NULL_LOCK;

  if (OID_IS_ROOTOID (class_oid))
    {
      if (old_class_lock < new_class_lock)
	{
	  granted = lock_internal_perform_lock_object (thread_p, tran_index, class_oid, NULL, new_class_lock,
						       wait_msecs, &root_class_entry, NULL);
	  if (granted != LK_GRANTED)
	    {
	      goto end;
	    }
	}
      /* case 2 : resource type is LOCK_RESOURCE_CLASS */
      /* acquire a lock on the given class object */

      /* NOTE that in case of acquiring a lock on a class object, the higher lock granule of the class object must not
       * be given. */
      granted = lock_internal_perform_lock_object (thread_p, tran_index, oid, NULL, lock, wait_msecs, &class_entry,
						   root_class_entry);
      goto end;
    }
  else
    {
      if (old_class_lock < new_class_lock)
	{
	  if (class_entry != NULL && class_entry->class_entry != NULL
	      && !OID_IS_ROOTOID (&class_entry->class_entry->res_head->key.oid))
	    {
	      /* preserve class hierarchy */
	      superclass_entry = class_entry->class_entry;
	    }
	  else
	    {
	      superclass_entry = lock_get_class_lock (oid_Root_class_oid, tran_index);
	    }

	  granted =
	    lock_internal_perform_lock_object (thread_p, tran_index, class_oid, NULL, new_class_lock, wait_msecs,
					       &class_entry, superclass_entry);
	  if (granted != LK_GRANTED)
	    {
	      goto end;
	    }
	}

      /* case 3 : resource type is LOCK_RESOURCE_INSTANCE */
      if (lock_is_class_lock_escalated (old_class_lock, lock) == true)
	{			/* already granted on the class level */
	  /* if incompatible old class lock with requested lock, remove instant class locks */
	  lock_stop_instant_lock_mode (thread_p, tran_index, false);
	  granted = LK_GRANTED;
	  goto end;
	}
      /* acquire a lock on the given instance oid */

      /* NOTE that in case of acquiring a lock on an instance object, the class oid of the instance object must be
       * given. */
      granted = lock_internal_perform_lock_object (thread_p, tran_index, oid, class_oid, lock, wait_msecs, &inst_entry,
						   class_entry);
      goto end;
    }

end:
#if defined (EnableThreadMonitoring)
  if (0 < prm_get_integer_value (PRM_ID_MNT_WAITING_THREAD))
    {
      tsc_getticks (&end_tick);
      tsc_elapsed_time_usec (&elapsed_time, end_tick, start_tick);
    }
  if (MONITOR_WAITING_THREAD (elapsed_time))
    {
      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_MNT_WAITING_THREAD, 2, "lock object (lock_object)",
	      prm_get_integer_value (PRM_ID_MNT_WAITING_THREAD));
      er_log_debug (ARG_FILE_LINE, "lock_object: %6d.%06d\n", elapsed_time.tv_sec, elapsed_time.tv_usec);
    }
#endif

  return granted;
#endif /* !SERVER_MODE */
}

/*
 * lock_subclass () - Lock a class in a class hierarchy
 *
 * return: one of following values)
 *     LK_GRANTED
 *     LK_NOTGRANTED_DUE_ABORTED
 *     LK_NOTGRANTED_DUE_TIMEOUT
 *     LK_NOTGRANTED_DUE_ERROR
 *
 *   subclass_oid(in): Identifier of subclass to lock
 *   superclass_oid(in): Identifier of the superclass
 *   lock(in): Requested lock mode
 *   cond_flag(in):
 */
int
lock_subclass (THREAD_ENTRY * thread_p, const OID * subclass_oid, const OID * superclass_oid, LOCK lock, int cond_flag)
{
#if !defined (SERVER_MODE)
  LK_SET_STANDALONE_XLOCK (lock);
  return LK_GRANTED;
#else /* !SERVER_MODE */
  LOCK new_superclass_lock, old_superclass_lock;
  LK_ENTRY *superclass_entry = NULL, *subclass_entry = NULL;
  int granted;
  int tran_index;
  int wait_msecs;
  TRAN_ISOLATION isolation;
#if defined (EnableThreadMonitoring)
  TSC_TICKS start_tick, end_tick;
  TSCTIMEVAL elapsed_time;
#endif

  if (subclass_oid == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LK_BAD_ARGUMENT, 2, "lock_subclass", "NULL subclass OID pointer");
      return LK_NOTGRANTED_DUE_ERROR;
    }

  if (superclass_oid == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LK_BAD_ARGUMENT, 2, "lock_subclass", "NULL superclass OID pointer");
      return LK_NOTGRANTED_DUE_ERROR;
    }

  if (lock == NULL_LOCK)
    {
      return LK_GRANTED;
    }

#if defined (EnableThreadMonitoring)
  if (0 < prm_get_integer_value (PRM_ID_MNT_WAITING_THREAD))
    {
      tsc_getticks (&start_tick);
    }
#endif

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  if (cond_flag == LK_COND_LOCK)	/* conditional request */
    {
      wait_msecs = LK_FORCE_ZERO_WAIT;
    }
  else
    {
      wait_msecs = logtb_find_wait_msecs (tran_index);
    }
  isolation = logtb_find_isolation (tran_index);

  /* get the intentional lock mode to be acquired on class oid */
  if (lock <= S_LOCK)
    {
      new_superclass_lock = IS_LOCK;
    }
  else
    {
      new_superclass_lock = IX_LOCK;
    }

  /* Check if current transaction has already held the class lock. If the class lock is not held, hold the class lock,
   * now. */
  superclass_entry = lock_get_class_lock (superclass_oid, tran_index);
  old_superclass_lock = (superclass_entry) ? superclass_entry->granted_mode : NULL_LOCK;


  if (old_superclass_lock < new_superclass_lock)
    {
      /* superclass is already locked, just promote to the new lock */
      granted = lock_internal_perform_lock_object (thread_p, tran_index, superclass_oid, NULL, new_superclass_lock,
						   wait_msecs, &superclass_entry, NULL);
      if (granted != LK_GRANTED)
	{
	  goto end;
	}
    }
  /* case 2 : resource type is LOCK_RESOURCE_CLASS */
  /* acquire a lock on the given class object */

  /* NOTE that in case of acquiring a lock on a class object, the higher lock granule of the class object must not be
   * given. */

  granted = lock_internal_perform_lock_object (thread_p, tran_index, subclass_oid, NULL, lock, wait_msecs,
					       &subclass_entry, superclass_entry);
end:
#if defined (EnableThreadMonitoring)
  if (0 < prm_get_integer_value (PRM_ID_MNT_WAITING_THREAD))
    {
      tsc_getticks (&end_tick);
      tsc_elapsed_time_usec (&elapsed_time, end_tick, start_tick);
    }
  if (MONITOR_WAITING_THREAD (elapsed_time))
    {
      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_MNT_WAITING_THREAD, 2, "lock object (lock_object)",
	      prm_get_integer_value (PRM_ID_MNT_WAITING_THREAD));
      er_log_debug (ARG_FILE_LINE, "lock_object: %6d.%06d\n", elapsed_time.tv_sec, elapsed_time.tv_usec);
    }
#endif

  return granted;
#endif /* !SERVER_MODE */
}

/*
 * lock_object_wait_msecs - Lock an object
 *
 * return: one of following values)
 *     LK_GRANTED
 *     LK_NOTGRANTED_DUE_ABORTED
 *     LK_NOTGRANTED_DUE_TIMEOUT
 *     LK_NOTGRANTED_DUE_ERROR
 *
 *   oid(in): Identifier of object(instance, class, root class) to lock
 *   class_oid(in): Identifier of the class instance of the given object
 *   lock(in): Requested lock mode
 *   cond_flag(in):
 *   wait_msecs(in):
 *
 */
int
lock_object_wait_msecs (THREAD_ENTRY * thread_p, const OID * oid, const OID * class_oid, LOCK lock, int cond_flag,
			int wait_msecs)
{
#if !defined (SERVER_MODE)
  LK_SET_STANDALONE_XLOCK (lock);
  return LK_GRANTED;
#else /* !SERVER_MODE */
  int old_wait_msecs = xlogtb_reset_wait_msecs (thread_p, wait_msecs);
  int lock_result = lock_object (thread_p, oid, class_oid, lock, cond_flag);

  xlogtb_reset_wait_msecs (thread_p, old_wait_msecs);

  return lock_result;
#endif
}

/*
 * lock_scan - Lock for scanning a heap
 *
 * return: one of following values)
 *     LK_GRANTED
 *     LK_NOTGRANTED_DUE_ABORTED
 *     LK_NOTGRANTED_DUE_TIMEOUT
 *     LK_NOTGRANTED_DUE_ERROR
 *
 *   class_oid(in): class oid of the instances to be scanned
 *   current_lock(in): acquired lock
 *
 */
int
lock_scan (THREAD_ENTRY * thread_p, const OID * class_oid, int cond_flag, LOCK class_lock)
{
#if !defined (SERVER_MODE)
  return LK_GRANTED;
#else /* !SERVER_MODE */
  int tran_index;
  int wait_msecs;
  TRAN_ISOLATION isolation;
  int granted;
  LK_ENTRY *root_class_entry = NULL;
  LK_ENTRY *class_entry = NULL;
#if defined (EnableThreadMonitoring)
  TSC_TICKS start_tick, end_tick;
  TSCTIMEVAL elapsed_time;
#endif

  if (class_oid == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LK_BAD_ARGUMENT, 2, "lock_scan", "NULL ClassOID pointer");
      return LK_NOTGRANTED_DUE_ERROR;
    }

#if defined (EnableThreadMonitoring)
  if (0 < prm_get_integer_value (PRM_ID_MNT_WAITING_THREAD))
    {
      tsc_getticks (&start_tick);
    }
#endif

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  if (cond_flag == LK_COND_LOCK)
    {
      wait_msecs = LK_FORCE_ZERO_WAIT;
    }
  else
    {
      assert (cond_flag == LK_UNCOND_LOCK);
      wait_msecs = logtb_find_wait_msecs (tran_index);
    }
  isolation = logtb_find_isolation (tran_index);

  /* acquire the lock on the class */
  /* NOTE that in case of acquiring a lock on a class object, the higher lock granule of the class object is not given. */
  root_class_entry = lock_get_class_lock (oid_Root_class_oid, tran_index);
  granted = lock_internal_perform_lock_object (thread_p, tran_index, class_oid, NULL, class_lock, wait_msecs,
					       &class_entry, root_class_entry);
  assert (granted == LK_GRANTED || cond_flag == LK_COND_LOCK || er_errid () != NO_ERROR);

#if defined (EnableThreadMonitoring)
  if (0 < prm_get_integer_value (PRM_ID_MNT_WAITING_THREAD))
    {
      tsc_getticks (&end_tick);
      tsc_elapsed_time_usec (&elapsed_time, end_tick, start_tick);
    }
  if (MONITOR_WAITING_THREAD (elapsed_time))
    {
      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_MNT_WAITING_THREAD, 2, "lock object (lock_scan)",
	      prm_get_integer_value (PRM_ID_MNT_WAITING_THREAD));
      er_log_debug (ARG_FILE_LINE, "lock_scan: %6d.%06d\n", elapsed_time.tv_sec, elapsed_time.tv_usec);
    }
#endif

  return granted;
#endif /* !SERVER_MODE */
}

/*
 * lock_classes_lock_hint - Lock many classes that has been hinted
 *
 * return: one of following values
 *     LK_GRANTED
 *     LK_NOTGRANTED_DUE_ABORTED
 *     LK_NOTGRANTED_DUE_TIMEOUT
 *     LK_NOTGRANTED_DUE_ERROR
 *
 *   lockhint(in): description of hinted classes
 *
 */
int
lock_classes_lock_hint (THREAD_ENTRY * thread_p, LC_LOCKHINT * lockhint)
{
#if !defined (SERVER_MODE)
  int i;

  for (i = 0; i < lockhint->num_classes; i++)
    {
      if (lockhint->classes[i].lock == SCH_M_LOCK || lockhint->classes[i].lock == X_LOCK
	  || lockhint->classes[i].lock == IX_LOCK || lockhint->classes[i].lock == SIX_LOCK)
	{
	  lk_Standalone_has_xlock = true;
	  break;
	}
    }
  return LK_GRANTED;
#else /* !SERVER_MODE */
  int tran_index;
  int wait_msecs;
  TRAN_ISOLATION isolation;
  LK_LOCKINFO cls_lockinfo_space[LK_LOCKINFO_FIXED_COUNT];
  LK_LOCKINFO *cls_lockinfo;
  LK_ENTRY *root_class_entry = NULL;
  LK_ENTRY *class_entry = NULL;
  OID *root_oidp;
  LOCK root_lock;
  LOCK intention_mode;
  int cls_count;
  int granted, i;
#if defined (EnableThreadMonitoring)
  TSC_TICKS start_tick, end_tick;
  TSCTIMEVAL elapsed_time;
#endif

  if (lockhint == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LK_BAD_ARGUMENT, 2, "lock_classes_lock_hint",
	      "NULL lockhint pointer");
      return LK_NOTGRANTED_DUE_ERROR;
    }

  /* If there is nothing to lock, returns */
  if (lockhint->num_classes <= 0)
    {
      return LK_GRANTED;
    }

#if defined (EnableThreadMonitoring)
  if (0 < prm_get_integer_value (PRM_ID_MNT_WAITING_THREAD))
    {
      tsc_getticks (&start_tick);
    }
#endif

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  wait_msecs = logtb_find_wait_msecs (tran_index);
  isolation = logtb_find_isolation (tran_index);

  /* We do not want to rollback the transaction in the event of a deadlock. For now, let's just wait a long time. If
   * deadlock, the transaction is going to be notified of lock timeout instead of aborted. */
  if (lockhint->quit_on_errors == false && wait_msecs == LK_INFINITE_WAIT)
    {
      wait_msecs = INT_MAX;	/* to be notified of lock timeout */
    }

  /* prepare cls_lockinfo array */
  if (lockhint->num_classes <= LK_LOCKINFO_FIXED_COUNT)
    {
      cls_lockinfo = &cls_lockinfo_space[0];
    }
  else
    {				/* num_classes > LK_LOCKINFO_FIXED_COUNT */
      cls_lockinfo = (LK_LOCKINFO *) db_private_alloc (thread_p, SIZEOF_LK_LOCKINFO * lockhint->num_classes);
      if (cls_lockinfo == NULL)
	{
	  return LK_NOTGRANTED_DUE_ERROR;
	}
    }

  /* Define the desired locks for all classes */
  /* get class_oids and class_locks */
  cls_count = 0;
  for (i = 0; i < lockhint->num_classes; i++)
    {
      if (OID_ISNULL (&lockhint->classes[i].oid) || lockhint->classes[i].lock == NULL_LOCK)
	{
	  continue;
	}

      if (OID_IS_ROOTOID (&lockhint->classes[i].oid))
	{
	  /* When the given class is the root class */
	  root_oidp = &lockhint->classes[i].oid;
	  root_lock = lockhint->classes[i].lock;

	  /* hold an explicit lock on the root class */
	  granted = lock_internal_perform_lock_object (thread_p, tran_index, root_oidp, NULL, root_lock, wait_msecs,
						       &root_class_entry, NULL);
	  if (granted != LK_GRANTED)
	    {
	      if (lockhint->quit_on_errors == true || granted != LK_NOTGRANTED_DUE_TIMEOUT)
		{
		  goto error;
		}
	      else
		{
		  OID_SET_NULL (root_oidp);
		}
	    }
	}
      else
	{
	  /* build cls_lockinfo[cls_count] */
	  COPY_OID (&cls_lockinfo[cls_count].oid, &lockhint->classes[i].oid);
	  cls_lockinfo[cls_count].org_oidp = &lockhint->classes[i].oid;
	  cls_lockinfo[cls_count].lock = lockhint->classes[i].lock;

	  /* increment cls_count */
	  cls_count++;
	}
    }

  /* sort class oids before hold the locks in order to avoid deadlocks */
  if (cls_count > 1)
    {
      (void) qsort (cls_lockinfo, cls_count, SIZEOF_LK_LOCKINFO, lock_compare_lock_info);
    }

  /* get root class lock mode */
  root_class_entry = lock_get_class_lock (oid_Root_class_oid, tran_index);

  for (i = 0; i < cls_count; i++)
    {
      /* hold the intentional lock on the root class if needed. */
      if (cls_lockinfo[i].lock <= S_LOCK)
	{
	  intention_mode = IS_LOCK;
	}
      else
	{
	  intention_mode = IX_LOCK;
	}

      if (root_class_entry == NULL || root_class_entry->granted_mode < intention_mode)
	{
	  granted = lock_internal_perform_lock_object (thread_p, tran_index, oid_Root_class_oid, NULL, intention_mode,
						       wait_msecs, &root_class_entry, NULL);
	  if (granted != LK_GRANTED)
	    {
	      if (lockhint->quit_on_errors == false && granted == LK_NOTGRANTED_DUE_TIMEOUT)
		{
		  OID_SET_NULL (cls_lockinfo[i].org_oidp);
		  continue;
		}
	      goto error;
	    }
	}

      /* hold the lock on the given class. */
      granted = lock_internal_perform_lock_object (thread_p, tran_index, &cls_lockinfo[i].oid, NULL,
						   cls_lockinfo[i].lock, wait_msecs, &class_entry, root_class_entry);

      if (granted != LK_GRANTED)
	{
	  if (lockhint->quit_on_errors == false && granted == LK_NOTGRANTED_DUE_TIMEOUT)
	    {
	      OID_SET_NULL (cls_lockinfo[i].org_oidp);
	      continue;
	    }
	  goto error;
	}
    }

  /* release memory space for cls_lockinfo */
  if (cls_lockinfo != &cls_lockinfo_space[0])
    {
      db_private_free_and_init (thread_p, cls_lockinfo);
    }

#if defined (EnableThreadMonitoring)
  if (0 < prm_get_integer_value (PRM_ID_MNT_WAITING_THREAD))
    {
      tsc_getticks (&end_tick);
      tsc_elapsed_time_usec (&elapsed_time, end_tick, start_tick);
    }
  if (MONITOR_WAITING_THREAD (elapsed_time))
    {
      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_MNT_WAITING_THREAD, 2, "lock object (lock_classes_lock_hint)",
	      prm_get_integer_value (PRM_ID_MNT_WAITING_THREAD));
      er_log_debug (ARG_FILE_LINE, "lock_classes_lock_hint: %6d.%06d\n", elapsed_time.tv_sec, elapsed_time.tv_usec);
    }
#endif

  return LK_GRANTED;

error:
  if (cls_lockinfo != &cls_lockinfo_space[0])
    {
      db_private_free_and_init (thread_p, cls_lockinfo);
    }
  return granted;
#endif /* !SERVER_MODE */
}

static void
lock_unlock_object_lock_internal (THREAD_ENTRY * thread_p, const OID * oid, const OID * class_oid, LOCK lock,
				  int release_flag, int move_to_non2pl)
{
#if !defined (SERVER_MODE)
  return;
#else /* !SERVER_MODE */
  LK_ENTRY *entry_ptr = NULL;
  int tran_index;
  bool is_class;

  is_class = (OID_IS_ROOTOID (oid) || OID_IS_ROOTOID (class_oid)) ? true : false;

  /* get transaction table index */
  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  entry_ptr = lock_find_tran_hold_entry (tran_index, oid, is_class);

  if (entry_ptr != NULL)
    {
      lock_internal_perform_unlock_object (thread_p, entry_ptr, release_flag, move_to_non2pl);
    }
#endif
}

/*
 * lock_unlock_object_donot_move_to_non2pl - Unlock an object lock on the specified object
 *   return:
 *   thread_p(in):
 *   oid(in):  Identifier of instance to unlock from
 *   class_oid(in): Identifier of the class of the instance
 *   lock(in): Lock to release
 *
 */
void
lock_unlock_object_donot_move_to_non2pl (THREAD_ENTRY * thread_p, const OID * oid, const OID * class_oid, LOCK lock)
{
  lock_unlock_object_lock_internal (thread_p, oid, class_oid, lock, false, false);
}

/*
 * lock_remove_object_lock - Removes a lock on the specified object
 *   return:
 *   thread_p(in):
 *   oid(in):  Identifier of instance to remove lock from
 *   class_oid(in): Identifier of the class of the instance
 *   lock(in): Lock to remove
 *
 */
void
lock_remove_object_lock (THREAD_ENTRY * thread_p, const OID * oid, const OID * class_oid, LOCK lock)
{
  lock_unlock_object_lock_internal (thread_p, oid, class_oid, lock, true, false);
}

/*
 * lock_unlock_object - Unlock an object according to transaction isolation level
 *
 * return: nothing..
 *
 *   oid(in): Identifier of instance to lock
 *   class_oid(in): Identifier of the class of the instance
 *   lock(in): Lock to release
 *   force(in): Unlock the object no matter what it is the isolation level.
 *
 */
void
lock_unlock_object (THREAD_ENTRY * thread_p, const OID * oid, const OID * class_oid, LOCK lock, int force)
{
#if !defined (SERVER_MODE)
  return;
#else /* !SERVER_MODE */
  int tran_index;		/* transaction table index */
  TRAN_ISOLATION isolation;	/* transaction isolation level */
  LK_ENTRY *entry_ptr;
  bool is_class;

  if (oid == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LK_BAD_ARGUMENT, 2, "lk_unlock_object", "NULL OID pointer");
      return;
    }
  if (class_oid == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LK_BAD_ARGUMENT, 2, "lk_unlock_object", "NULL ClassOID pointer");
      return;
    }

  is_class = (OID_IS_ROOTOID (oid) || OID_IS_ROOTOID (class_oid)) ? true : false;

  /* get transaction table index */
  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);

  if (force == true)
    {
#if defined(ENABLE_SYSTEMTAP)
      CUBRID_LOCK_RELEASE_START (oid, class_oid, lock);
#endif /* ENABLE_SYSTEMTAP */

      entry_ptr = lock_find_tran_hold_entry (tran_index, oid, is_class);

      if (entry_ptr != NULL)
	{
	  lock_internal_perform_unlock_object (thread_p, entry_ptr, false, true);
	}

#if defined(ENABLE_SYSTEMTAP)
      CUBRID_LOCK_RELEASE_END (oid, class_oid, lock);
#endif /* ENABLE_SYSTEMTAP */

      return;
    }

  /* force != true */
  if (lock != S_LOCK)
    {
      assert (lock != NULL_LOCK);
      /* These will not be released. */
      return;
    }

  isolation = logtb_find_isolation (tran_index);
  switch (isolation)
    {
    case TRAN_SERIALIZABLE:
    case TRAN_REPEATABLE_READ:
      return;			/* nothing to do */

    case TRAN_READ_COMMITTED:
#if defined(ENABLE_SYSTEMTAP)
      CUBRID_LOCK_RELEASE_START (oid, class_oid, lock);
#endif /* ENABLE_SYSTEMTAP */

      /* The intentional lock on the higher lock granule must be kept. */
      lock_unlock_object_by_isolation (thread_p, tran_index, isolation, class_oid, oid);

#if defined(ENABLE_SYSTEMTAP)
      CUBRID_LOCK_RELEASE_END (oid, class_oid, lock);
#endif /* ENABLE_SYSTEMTAP */
      break;

    default:			/* TRAN_UNKNOWN_ISOLATION */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LK_UNKNOWN_ISOLATION, 2, isolation, tran_index);
      break;
    }

  return;
#endif /* !SERVER_MODE */
}

/*
 * lock_unlock_objects_lock_set - Unlock many objects according to isolation level
 *
 * return: nothing..
 *
 *   lockset(in):
 *
 */
void
lock_unlock_objects_lock_set (THREAD_ENTRY * thread_p, LC_LOCKSET * lockset)
{
#if !defined (SERVER_MODE)
  return;
#else /* !SERVER_MODE */
  int tran_index;		/* transaction table index */
  TRAN_ISOLATION isolation;	/* transaction isolation level */
  LOCK reqobj_class_unlock;
  OID *oid, *class_oid;
  int i;

  if (lockset == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LK_BAD_ARGUMENT, 2, "lk_unlock_objects_lockset",
	      "NULL lockset pointer");
      return;
    }

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  isolation = logtb_find_isolation (tran_index);

  if (isolation == TRAN_SERIALIZABLE || isolation == TRAN_REPEATABLE_READ)
    {
      return;			/* Nothing to release */
    }
  else if (isolation != TRAN_READ_COMMITTED)
    {
      assert (0);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LK_UNKNOWN_ISOLATION, 2, isolation, tran_index);
      return;
    }

  assert (isolation == TRAN_READ_COMMITTED);

  reqobj_class_unlock = lockset->reqobj_class_lock;
  if (reqobj_class_unlock == X_LOCK)
    {
      return;			/* Don't release the lock */
    }

  for (i = 0; i < lockset->num_reqobjs_processed; i++)
    {
      oid = &lockset->objects[i].oid;
      if (OID_ISNULL (oid) || lockset->objects[i].class_index == -1)
	{
	  continue;
	}

      class_oid = &lockset->classes[lockset->objects[i].class_index].oid;
      if (OID_ISNULL (class_oid))
	{
	  continue;
	}

      /* The intentional lock on the higher lock granule must be kept. */
      lock_unlock_object_by_isolation (thread_p, tran_index, isolation, class_oid, oid);
    }
#endif /* !SERVER_MODE */
}

/*
 * lock_unlock_classes_lock_hint - Unlock many hinted classes according to
 *                             transaction isolation level
 *
 * return: nothing..
 *
 *   lockhint(in): Description of hinted classses
 *
 */
void
lock_unlock_classes_lock_hint (THREAD_ENTRY * thread_p, LC_LOCKHINT * lockhint)
{
#if !defined (SERVER_MODE)
  return;
#else /* !SERVER_MODE */
  int tran_index;		/* transaction table index */
  TRAN_ISOLATION isolation;	/* transaction isolation level */
  int i;

  if (lockhint == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LK_BAD_ARGUMENT, 2, "lk_unlock_classes_lockhint",
	      "NULL lockhint pointer");
      return;
    }

  /* If there is nothing to unlock, returns */
  if (lockhint->num_classes <= 0)
    {
      return;
    }

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  isolation = logtb_find_isolation (tran_index);

  switch (isolation)
    {
    case TRAN_SERIALIZABLE:
    case TRAN_REPEATABLE_READ:
      return;			/* nothing to do */

    case TRAN_READ_COMMITTED:
      for (i = 0; i < lockhint->num_classes; i++)
	{
	  if (OID_ISNULL (&lockhint->classes[i].oid) || lockhint->classes[i].lock == NULL_LOCK)
	    {
	      continue;
	    }
	  lock_unlock_inst_locks_of_class_by_isolation (thread_p, tran_index, isolation, &lockhint->classes[i].oid);
	}
      return;

    default:			/* TRAN_UNKNOWN_ISOLATION */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LK_UNKNOWN_ISOLATION, 2, isolation, tran_index);
      return;
    }
#endif /* !SERVER_MODE */
}

/*
 * lock_unlock_all - Release all locks of current transaction
 *
 * return: nothing
 *
 * Note:Release all locks acquired by the current transaction.
 *
 *      This function must be called at the end of the transaction.
 */
void
lock_unlock_all (THREAD_ENTRY * thread_p)
{
#if !defined (SERVER_MODE)
  lk_Standalone_has_xlock = false;
  pgbuf_unfix_all (thread_p);

  return;
#else /* !SERVER_MODE */
  int tran_index;
  LK_TRAN_LOCK *tran_lock;
  LK_ENTRY *entry_ptr;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tran_lock = &lk_Gl.tran_lock_table[tran_index];

  /* remove all instance locks */
  entry_ptr = tran_lock->inst_hold_list;
  while (entry_ptr != NULL)
    {
      assert (tran_index == entry_ptr->tran_index);

      lock_internal_perform_unlock_object (thread_p, entry_ptr, true, false);
      entry_ptr = tran_lock->inst_hold_list;
    }

  /* remove all class locks */
  entry_ptr = tran_lock->class_hold_list;
  while (entry_ptr != NULL)
    {
      assert (tran_index == entry_ptr->tran_index);

      lock_internal_perform_unlock_object (thread_p, entry_ptr, true, false);
      entry_ptr = tran_lock->class_hold_list;
    }

  /* remove root class lock */
  entry_ptr = tran_lock->root_class_hold;
  if (entry_ptr != NULL)
    {
      assert (tran_index == entry_ptr->tran_index);

      lock_internal_perform_unlock_object (thread_p, entry_ptr, true, false);
    }

  /* remove non2pl locks */
  while (tran_lock->non2pl_list != NULL)
    {
      /* remove the non2pl entry from transaction non2pl list */
      entry_ptr = tran_lock->non2pl_list;
      tran_lock->non2pl_list = entry_ptr->tran_next;

      assert (tran_index == entry_ptr->tran_index);

      if (entry_ptr->granted_mode == INCON_NON_TWO_PHASE_LOCK)
	{
	  tran_lock->num_incons_non2pl -= 1;
	}
      /* remove the non2pl entry from resource non2pl list and free it */
      lock_remove_non2pl (entry_ptr, tran_index);
    }

  lock_clear_deadlock_victim (tran_index);

  pgbuf_unfix_all (thread_p);
#endif /* !SERVER_MODE */
}

static LK_ENTRY *
lock_find_tran_hold_entry (int tran_index, const OID * oid, bool is_class)
{
#if !defined (SERVER_MODE)
  return NULL;
#else /* !SERVER_MODE */
  LF_TRAN_ENTRY *t_entry = thread_get_tran_entry (NULL, THREAD_TS_OBJ_LOCK_RES);
  LK_RES_KEY search_key;
  LK_RES *res_ptr;
  LK_ENTRY *entry_ptr;
  int rv;

  if (is_class)
    {
      return lock_find_class_entry (tran_index, oid);
    }

  /* search hash */
  search_key = lock_create_search_key ((OID *) oid, NULL);
  if (search_key.type != LOCK_RESOURCE_ROOT_CLASS)
    {
      /* override type; we don't insert here, so class_oid is neither passed to us nor needed for the search */
      search_key.type = (is_class ? LOCK_RESOURCE_CLASS : LOCK_RESOURCE_INSTANCE);
    }
  rv = lf_hash_find (t_entry, &lk_Gl.obj_hash_table, (void *) &search_key, (void **) &res_ptr);
  if (rv != NO_ERROR)
    {
      return NULL;
    }

  if (res_ptr == NULL)
    {
      /* not found */
      return NULL;
    }

  entry_ptr = res_ptr->holder;
  for (; entry_ptr != NULL; entry_ptr = entry_ptr->next)
    {
      if (entry_ptr->tran_index == tran_index)
	{
	  break;
	}
    }

  pthread_mutex_unlock (&res_ptr->res_mutex);
  return entry_ptr;
#endif
}

/*
 * lock_get_object_lock - Find the acquired lock mode
 *
 * return:
 *
 *   oid(in): target object ientifier
 *   class_oid(in): class identifier of the target object
 *   tran_index(in): the transaction table index of target transaction.
 *
 * Note:Find the acquired lock on the given object by the given transaction.
 *     Currently, the value of the "tran_index" must not be NULL_TRAN_INDEX.
 *
 *     If this function is changed in order to allow NULL_TRAN_INDEX to be
 *     transferred as the value of the "tran_index", this function will
 *     find the resulting lock acquired by all transactions holding a lock
 *     on the given OID (The most powerful lock, actually a combination of
 *     the locks).
 */
LOCK
lock_get_object_lock (const OID * oid, const OID * class_oid, int tran_index)
{
#if !defined (SERVER_MODE)
  return X_LOCK;
#else /* !SERVER_MODE */
  LOCK lock_mode = NULL_LOCK;	/* return value */
  LK_TRAN_LOCK *tran_lock;
  LK_ENTRY *entry_ptr;
  int rv;

  if (oid == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LK_BAD_ARGUMENT, 2, "lk_get_object_lock", "NULL OID pointer");
      return NULL_LOCK;
    }
  if (tran_index == NULL_TRAN_INDEX)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LK_BAD_ARGUMENT, 2, "lk_get_object_lock", "NULL_TRAN_INDEX");
      return NULL_LOCK;
    }

  /* get a pointer to transaction lock info entry */
  tran_lock = &lk_Gl.tran_lock_table[tran_index];

  /* 
   * case 1: root class lock
   */
  /* get the granted lock mode acquired on the root class oid */
  if (OID_EQ (oid, oid_Root_class_oid))
    {
      rv = pthread_mutex_lock (&tran_lock->hold_mutex);
      if (tran_lock->root_class_hold != NULL)
	{
	  lock_mode = tran_lock->root_class_hold->granted_mode;
	}
      pthread_mutex_unlock (&tran_lock->hold_mutex);
      return lock_mode;		/* might be NULL_LOCK */
    }

  /* 
   * case 2: general class lock
   */
  /* get the granted lock mode acquired on the given class oid */
  if (class_oid == NULL || OID_EQ (class_oid, oid_Root_class_oid))
    {
      entry_ptr = lock_find_tran_hold_entry (tran_index, oid, true);
      if (entry_ptr != NULL)
	{
	  lock_mode = entry_ptr->granted_mode;
	}
      return lock_mode;		/* might be NULL_LOCK */
    }

  entry_ptr = lock_find_tran_hold_entry (tran_index, class_oid, true);
  if (entry_ptr != NULL)
    {
      lock_mode = entry_ptr->granted_mode;
    }

  /* If the class lock mode is one of S_LOCK, X_LOCK or SCH_M_LOCK, the lock is held on the instance implicitly. In
   * this case, there is no need to check instance lock. If the class lock mode is SIX_LOCK, S_LOCK is held on the
   * instance implicitly. In this case, we must check for a possible X_LOCK on the instance. In other cases, we must
   * check the lock held on the instance. */
  if (lock_mode == SCH_M_LOCK)
    {
      return X_LOCK;
    }
  else if (lock_mode != S_LOCK && lock_mode != X_LOCK)
    {
      if (lock_mode == SIX_LOCK)
	{
	  lock_mode = S_LOCK;
	}
      else
	{
	  lock_mode = NULL_LOCK;
	}

      entry_ptr = lock_find_tran_hold_entry (tran_index, oid, false);
      if (entry_ptr != NULL)
	{
	  lock_mode = entry_ptr->granted_mode;
	}
    }

  return lock_mode;		/* might be NULL_LOCK */
#endif /* !SERVER_MODE */
}

/*
 * lock_has_lock_on_object -
 *
 * return:
 *
 *   oid(in): target object ientifier
 *   class_oid(in): class identifier of the target object
 *   tran_index(in): the transaction table index of target transaction.
 *   lock(in): the lock mode
 *
 * Note: Find whether the transaction holds an enough lock on the object
 *
 */
int
lock_has_lock_on_object (const OID * oid, const OID * class_oid, int tran_index, LOCK lock)
{
#if !defined (SERVER_MODE)
  return 1;
#else /* !SERVER_MODE */
  LOCK granted_lock_mode = NULL_LOCK;
  LK_TRAN_LOCK *tran_lock;
  LK_ENTRY *entry_ptr;
  int rv;

  if (oid == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LK_BAD_ARGUMENT, 2, "lock_has_lock_on_object", "NULL OID pointer");
      return ER_LK_BAD_ARGUMENT;
    }
  if (tran_index == NULL_TRAN_INDEX)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LK_BAD_ARGUMENT, 2, "lock_has_lock_on_object", "NULL_TRAN_INDEX");
      return ER_LK_BAD_ARGUMENT;
    }

  /* get a pointer to transaction lock info entry */
  tran_lock = &lk_Gl.tran_lock_table[tran_index];

  /* 
   * case 1: root class lock
   */
  /* get the granted lock mode acquired on the root class oid */
  if (OID_EQ (oid, oid_Root_class_oid))
    {
      rv = pthread_mutex_lock (&tran_lock->hold_mutex);
      if (tran_lock->root_class_hold != NULL)
	{
	  granted_lock_mode = tran_lock->root_class_hold->granted_mode;
	}
      pthread_mutex_unlock (&tran_lock->hold_mutex);
      return (lock_Conv[lock][granted_lock_mode] == granted_lock_mode);
    }

  /* 
   * case 2: general class lock
   */
  /* get the granted lock mode acquired on the given class oid */
  if (class_oid == NULL || OID_EQ (class_oid, oid_Root_class_oid))
    {
      entry_ptr = lock_find_tran_hold_entry (tran_index, oid, true);
      if (entry_ptr != NULL)
	{
	  granted_lock_mode = entry_ptr->granted_mode;
	}
      return (lock_Conv[lock][granted_lock_mode] == granted_lock_mode);
    }

  entry_ptr = lock_find_tran_hold_entry (tran_index, class_oid, true);
  if (entry_ptr != NULL)
    {
      granted_lock_mode = entry_ptr->granted_mode;
      if (lock_Conv[lock][granted_lock_mode] == granted_lock_mode)
	{
	  return 1;
	}
    }

  /* 
   * case 3: object lock
   */
  /* get the granted lock mode acquired on the given instance/pseudo oid */
  entry_ptr = lock_find_tran_hold_entry (tran_index, oid, false);
  if (entry_ptr != NULL)
    {
      granted_lock_mode = entry_ptr->granted_mode;
      return 1;
    }

  return 0;
#endif /* !SERVER_MODE */
}

/*
 * lock_has_xlock - Does transaction have an exclusive lock on any resource ?
 *
 * return:
 *
 * Note:Find if the current transaction has any kind of exclusive lock
 *     on any lock resource.
 */
bool
lock_has_xlock (THREAD_ENTRY * thread_p)
{
#if !defined (SERVER_MODE)
  return lk_Standalone_has_xlock;
#else /* !SERVER_MODE */
  int tran_index;
  LK_TRAN_LOCK *tran_lock;
  LOCK lock_mode;
  LK_ENTRY *entry_ptr;
  int rv;

  /* 
   * Exclusive locks in this context mean IX_LOCK, SIX_LOCK, X_LOCK and
   * SCH_M_LOCK. NOTE that U_LOCK are excluded from exclusive locks. 
   * Because U_LOCK is currently for reading the object.
   */
  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tran_lock = &lk_Gl.tran_lock_table[tran_index];
  rv = pthread_mutex_lock (&tran_lock->hold_mutex);

  /* 1. check root class lock */
  if (tran_lock->root_class_hold != NULL)
    {
      lock_mode = tran_lock->root_class_hold->granted_mode;
      if (lock_mode == X_LOCK || lock_mode == IX_LOCK || lock_mode == SIX_LOCK || lock_mode == SCH_M_LOCK)
	{
	  pthread_mutex_unlock (&tran_lock->hold_mutex);
	  return true;
	}
    }

  /* 2. check general class locks */
  entry_ptr = tran_lock->class_hold_list;
  while (entry_ptr != NULL)
    {
      lock_mode = entry_ptr->granted_mode;
      if (lock_mode == X_LOCK || lock_mode == IX_LOCK || lock_mode == SIX_LOCK || lock_mode == SCH_M_LOCK)
	{
	  pthread_mutex_unlock (&tran_lock->hold_mutex);
	  return true;
	}
      entry_ptr = entry_ptr->tran_next;
    }

  /* 3. checking instance locks is not needed. According to MGL ptotocol, an exclusive class lock has been acquired
   * with intention mode before an exclusive instance is acquired. */

  pthread_mutex_unlock (&tran_lock->hold_mutex);
  return false;
#endif /* !SERVER_MODE */
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * lock_has_lock_transaction - Does transaction have any lock on any resource ?
 *
 * return:
 *
 *   tran_index(in):
 *
 * Note:Find if given transaction has any kind of lock.
 *     Used by css_check_for_clients_down() to eliminate needless pinging.
 */
bool
lock_has_lock_transaction (int tran_index)
{
#if !defined (SERVER_MODE)
  return lk_Standalone_has_xlock;
#else /* !SERVER_MODE */
  LK_TRAN_LOCK *tran_lock;
  bool lock_hold;
  int rv;

  tran_lock = &lk_Gl.tran_lock_table[tran_index];
  rv = pthread_mutex_lock (&tran_lock->hold_mutex);
  if (tran_lock->root_class_hold != NULL || tran_lock->class_hold_list != NULL || tran_lock->inst_hold_list != NULL
      || tran_lock->non2pl_list != NULL)
    {
      lock_hold = true;
    }
  else
    {
      lock_hold = false;
    }
  pthread_mutex_unlock (&tran_lock->hold_mutex);

  return lock_hold;
#endif /* !SERVER_MODE */
}
#endif

/*
 * lock_is_waiting_transaction -
 *
 * return:
 *
 *   tran_index(in):
 */
bool
lock_is_waiting_transaction (int tran_index)
{
#if !defined (SERVER_MODE)
  return false;
#else /* !SERVER_MODE */
  THREAD_ENTRY *thrd_array[10];
  int thrd_count, i;
  THREAD_ENTRY *thrd_ptr;

  thrd_count = thread_get_lockwait_entry (tran_index, &thrd_array[0]);
  for (i = 0; i < thrd_count; i++)
    {
      thrd_ptr = thrd_array[i];
      (void) thread_lock_entry (thrd_ptr);
      if (LK_IS_LOCKWAIT_THREAD (thrd_ptr))
	{
	  (void) thread_unlock_entry (thrd_ptr);
	  return true;
	}
      else
	{
	  if (thrd_ptr->lockwait != NULL || thrd_ptr->lockwait_state == (int) LOCK_SUSPENDED)
	    {
	      /* some strange lock wait state.. */
	      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_LK_STRANGE_LOCK_WAIT, 5, thrd_ptr->lockwait,
		      thrd_ptr->lockwait_state, thrd_ptr->index, thrd_ptr->tid, thrd_ptr->tran_index);
	    }
	}
      (void) thread_unlock_entry (thrd_ptr);
    }

  return false;
#endif /* !SERVER_MODE */
}

/*
 * lock_get_class_lock - Get a pointer to lock heap entry acquired by
 *                        given transaction on given class object
 *
 * return:
 *
 *   class_oid(in): target class object identifier
 *   tran_index(in): target transaction
 *
 * Note:This function finds lock entry acquired by the given transaction
 *     on the given class and then return a pointer to the lock entry.
 */
LK_ENTRY *
lock_get_class_lock (const OID * class_oid, int tran_index)
{
#if !defined (SERVER_MODE)
  assert (false);

  return NULL;
#else /* !SERVER_MODE */
  LK_TRAN_LOCK *tran_lock;
  LK_ENTRY *entry_ptr;
  int rv;

  if (class_oid == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LK_BAD_ARGUMENT, 2, "lk_get_class_lock_ptr",
	      "NULL ClassOID pointer");
      return NULL;
    }
  if (OID_ISNULL (class_oid))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LK_BAD_ARGUMENT, 2, "lk_get_class_lock_ptr", "NULL_ClassOID");
      return NULL;
    }

  /* get a pointer to transaction lock info entry */
  tran_lock = &lk_Gl.tran_lock_table[tran_index];

  /* case 1: root class lock */
  if (OID_EQ (class_oid, oid_Root_class_oid))
    {
      rv = pthread_mutex_lock (&tran_lock->hold_mutex);
      entry_ptr = tran_lock->root_class_hold;
      pthread_mutex_unlock (&tran_lock->hold_mutex);
    }
  else
    {
      entry_ptr = lock_find_tran_hold_entry (tran_index, class_oid, true);
    }

  return entry_ptr;
#endif /* !SERVER_MODE */
}

/*
 * lock_force_timeout_lock_wait_transactions - All lock-wait transactions
 *                               are forced to timeout
 *
 * return: nothing
 *
 * Note:All lock-waiting transacions are forced to timeout.
 *     For this task, all lock-waiting threads are searched and
 *     then the threads are forced to timeout.
 */
void
lock_force_timeout_lock_wait_transactions (unsigned short stop_phase)
{
#if !defined (SERVER_MODE)
  return;
#else /* !SERVER_MODE */
  int i;
  THREAD_ENTRY *thrd;
  CSS_CONN_ENTRY *conn_p;

  for (i = 1; i < thread_num_total_threads (); i++)
    {
      thrd = thread_find_entry_by_index (i);

      conn_p = thrd->conn_entry;
      if ((stop_phase == THREAD_STOP_LOGWR && conn_p == NULL) || (conn_p && conn_p->stop_phase != stop_phase))
	{
	  continue;
	}

      (void) thread_lock_entry (thrd);
      if (LK_IS_LOCKWAIT_THREAD (thrd))
	{
	  /* wake up the thread */
	  lock_resume ((LK_ENTRY *) thrd->lockwait, LOCK_RESUMED_TIMEOUT);
	}
      else
	{
	  if (thrd->lockwait != NULL || thrd->lockwait_state == (int) LOCK_SUSPENDED)
	    {
	      /* some strange lock wait state.. */
	      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_LK_STRANGE_LOCK_WAIT, 5, thrd->lockwait,
		      thrd->lockwait_state, thrd->index, thrd->tid, thrd->tran_index);
	    }
	  /* release the thread entry mutex */
	  (void) thread_unlock_entry (thrd);
	}
    }
  return;
#endif /* !SERVER_MODE */
}

/*
 * lock_force_timeout_expired_wait_transactions - Transaction is timeout if its waiting time has
 *                           expired or it is interrupted
 *
 * return: true if the thread was timed out or
 *                       false if the thread was not timed out.
 *
 *   thrd_entry(in): thread entry pointer
 *
 * Note:If the given thread is waiting on a lock to be granted, and
 *     either its expiration time has expired or it is interrupted,
 *     the thread is timed-out.
 *     If NULL is given, it applies to all threads.
 */
bool
lock_force_timeout_expired_wait_transactions (void *thrd_entry)
{
#if !defined (SERVER_MODE)
  return true;
#else /* !SERVER_MODE */
  int i;
  bool ignore;
  THREAD_ENTRY *thrd;

  if (thrd_entry != NULL)
    {
      thrd = (THREAD_ENTRY *) thrd_entry;
      (void) thread_lock_entry (thrd);
      if (LK_IS_LOCKWAIT_THREAD (thrd))
	{
	  struct timeval tv;
	  INT64 etime;
	  (void) gettimeofday (&tv, NULL);
	  etime = (tv.tv_sec * 1000000LL + tv.tv_usec) / 1000LL;
	  if (LK_CAN_TIMEOUT (thrd->lockwait_msecs) && etime - thrd->lockwait_stime > thrd->lockwait_msecs)
	    {
	      /* wake up the thread */
	      lock_resume ((LK_ENTRY *) thrd->lockwait, LOCK_RESUMED_TIMEOUT);
	      return true;
	    }
	  else if (logtb_is_interrupted_tran (NULL, true, &ignore, thrd->tran_index))
	    {
	      /* wake up the thread */
	      lock_resume ((LK_ENTRY *) thrd->lockwait, LOCK_RESUMED_INTERRUPT);
	      return true;
	    }
	  else
	    {
	      /* release the thread entry mutex */
	      (void) thread_unlock_entry (thrd);
	      return false;
	    }
	}
      else
	{
	  if (thrd->lockwait != NULL || thrd->lockwait_state == (int) LOCK_SUSPENDED)
	    {
	      /* some strange lock wait state.. */
	      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_LK_STRANGE_LOCK_WAIT, 5, thrd->lockwait,
		      thrd->lockwait_state, thrd->index, thrd->tid, thrd->tran_index);
	    }
	  /* release the thread entry mutex */
	  (void) thread_unlock_entry (thrd);
	  return false;
	}
    }
  else
    {
      for (i = 1; i < thread_num_total_threads (); i++)
	{
	  thrd = thread_find_entry_by_index (i);
	  (void) thread_lock_entry (thrd);
	  if (LK_IS_LOCKWAIT_THREAD (thrd))
	    {
	      struct timeval tv;
	      INT64 etime;
	      (void) gettimeofday (&tv, NULL);
	      etime = (tv.tv_sec * 1000000LL + tv.tv_usec) / 1000LL;
	      if ((LK_CAN_TIMEOUT (thrd->lockwait_msecs) && etime - thrd->lockwait_stime > thrd->lockwait_msecs)
		  || logtb_is_interrupted_tran (NULL, true, &ignore, thrd->tran_index))
		{
		  /* wake up the thread */
		  lock_resume ((LK_ENTRY *) thrd->lockwait, LOCK_RESUMED_TIMEOUT);
		}
	      else
		{
		  /* release the thread entry mutex */
		  (void) thread_unlock_entry (thrd);
		}
	    }
	  else
	    {
	      if (thrd->lockwait != NULL || thrd->lockwait_state == (int) LOCK_SUSPENDED)
		{
		  /* some strange lock wait state.. */
		  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_LK_STRANGE_LOCK_WAIT, 5, thrd->lockwait,
			  thrd->lockwait_state, thrd->index, thrd->tid, thrd->tran_index);
		}
	      /* release the thread entry mutex */
	      (void) thread_unlock_entry (thrd);
	    }
	}
      return true;
    }
#endif /* !SERVER_MODE */
}

/*
 * lock_notify_isolation_incons - Notify of possible inconsistencies (no
 *                             repeatable reads) due to transaction isolation
 *                             level
 *
 * return: nothing.
 *
 *   fun(in): Function to notify
 *   args(in): Extra arguments for function
 *
 * Note:The current transaction is notified of any possible
 *              inconsistencies due to its isolation level. For each possible
 *              inconsistency the given function is called to decache any
 *              copies of the object.
 */
void
lock_notify_isolation_incons (THREAD_ENTRY * thread_p, bool (*fun) (const OID * class_oid, const OID * oid, void *args),
			      void *args)
{
#if !defined (SERVER_MODE)
  return;
#else /* !SERVER_MODE */
  TRAN_ISOLATION isolation;
  int tran_index;
  LK_TRAN_LOCK *tran_lock;
  LK_ENTRY *curr, *prev, *next;
  LK_ENTRY *incon_non2pl_list_header = NULL;
  LK_ENTRY *incon_non2pl_list_tail = NULL;
  bool ret_val;
  int rv;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  isolation = logtb_find_isolation (tran_index);
  if (isolation == TRAN_REPEATABLE_READ || isolation == TRAN_SERIALIZABLE)
    {
      return;			/* Nothing was released */
    }

  tran_lock = &lk_Gl.tran_lock_table[tran_index];
  rv = pthread_mutex_lock (&tran_lock->non2pl_mutex);

  prev = NULL;
  curr = tran_lock->non2pl_list;
  while (tran_lock->num_incons_non2pl > 0 && curr != NULL)
    {
      if (curr->granted_mode != INCON_NON_TWO_PHASE_LOCK)
	{
	  prev = curr;
	  curr = curr->tran_next;
	  continue;
	}

      /* curr->granted_mode == INCON_NON_TWO_PHASE_LOCK */
      assert (curr->res_head->key.type != LOCK_RESOURCE_INSTANCE || !OID_ISNULL (&curr->res_head->key.class_oid));

      ret_val = (*fun) (&curr->res_head->key.class_oid, &curr->res_head->key.oid, args);
      if (ret_val != true)
	{
	  /* the notification area is full */
	  pthread_mutex_unlock (&(tran_lock->non2pl_mutex));

	  goto end;
	}

      /* the non-2pl entry should be freed. */
      /* 1. remove it from transaction non2pl list */
      next = curr->tran_next;
      if (prev == NULL)
	{
	  tran_lock->non2pl_list = next;
	}
      else
	{
	  prev->tran_next = next;
	}

      tran_lock->num_incons_non2pl -= 1;

      /* 2. append current entry to incon_non2pl_list */
      curr->tran_next = NULL;
      if (incon_non2pl_list_header == NULL)
	{
	  incon_non2pl_list_header = curr;
	  incon_non2pl_list_tail = curr;
	}
      else
	{
	  incon_non2pl_list_tail->tran_next = curr;
	  incon_non2pl_list_tail = curr;
	}

      curr = next;
    }

  /* release transaction non2pl mutex */
  pthread_mutex_unlock (&tran_lock->non2pl_mutex);

end:

  curr = incon_non2pl_list_header;
  while (curr != NULL)
    {
      next = curr->tran_next;

      /* remove it from resource non2pl list and free it */
      lock_remove_non2pl (curr, tran_index);

      curr = next;
    }

  return;
#endif /* !SERVER_MODE */
}

/*
 * lock_check_local_deadlock_detection - Check local deadlock detection interval
 *
 * return:
 *
 * Note:check if the local deadlock detection should be performed.
 */
bool
lock_check_local_deadlock_detection (void)
{
#if !defined (SERVER_MODE)
  return false;
#else /* !SERVER_MODE */
  struct timeval now, elapsed;
  double elapsed_sec;

  /* check deadlock detection interval */
  gettimeofday (&now, NULL);
  DIFF_TIMEVAL (lk_Gl.last_deadlock_run, now, elapsed);
  /* add 0.01 for the processing time by deadlock detection */
  elapsed_sec = elapsed.tv_sec + (elapsed.tv_usec / 1000) + 0.01;
  if (elapsed_sec <= prm_get_float_value (PRM_ID_LK_RUN_DEADLOCK_INTERVAL))
    {
      return false;
    }
  else
    {
      return true;
    }
#endif /* !SERVER_MODE */
}

/*
 * lock_detect_local_deadlock - Run the local deadlock detection
 *
 * return: nothing
 *
 * Note:Run the deadlock detection. For every cycle either timeout or
 *     abort a transaction. The timeout option is always preferred over
 *     the unilaterally abort option. When the unilaterally abort option
 *     is exercised, the youngest transaction in the cycle is selected.
 *     The youngest transaction is hopefully the one that has done less work.
 *
 *     First, allocate heaps for WFG table from local memory.
 *     Check whether deadlock(s) have been occurred or not.
 *
 *     Deadlock detection is peformed via exhaustive loop construction
 *     which indicates the wait-for-relationship.
 *     If deadlock is detected,
 *     the first transaction which enables a cycle
 *     when scanning from the first of object lock table to the last of it.
 *
 *     The deadlock of victims are waken up and aborted by themselves.
 *
 *     Last, free WFG framework.
 */
void
lock_detect_local_deadlock (THREAD_ENTRY * thread_p)
{
#if !defined (SERVER_MODE)
  return;
#else /* !SERVER_MODE */
  LF_HASH_TABLE_ITERATOR iterator;
  LF_TRAN_ENTRY *t_entry = thread_get_tran_entry (thread_p, THREAD_TS_OBJ_LOCK_RES);
  int k, s, t;
  LK_RES *res_ptr;
  LK_ENTRY *hi, *hj;
  LK_WFG_NODE *TWFG_node;
  LK_WFG_EDGE *TWFG_edge;
  int i, rv;
  int compat1, compat2;
  int tran_index;
  FILE *log_fp;

  /* initialize deadlock detection related structures */

  /* initialize transaction WFG node table.. The current transaction might be old deadlock victim. And, the transaction 
   * may have not been aborted, until now. Even if the transaction(old deadlock victim) has not been aborted, set
   * checked_by_deadlock_detector of the transaction to true. */
  for (i = 1; i < lk_Gl.num_trans; i++)
    {
      lk_Gl.TWFG_node[i].first_edge = -1;
      lk_Gl.TWFG_node[i].tran_edge_seq_num = 0;
      lk_Gl.TWFG_node[i].checked_by_deadlock_detector = true;
    }

  /* initialize transaction WFG edge table */
  lk_Gl.TWFG_edge = &TWFG_edge_block[0];
  lk_Gl.max_TWFG_edge = LK_MIN_TWFG_EDGE_COUNT;	/* initial value */
  for (i = 0; i < LK_MIN_TWFG_EDGE_COUNT; i++)
    {
      lk_Gl.TWFG_edge[i].to_tran_index = -1;
      lk_Gl.TWFG_edge[i].next = (i + 1);
    }
  lk_Gl.TWFG_edge[lk_Gl.max_TWFG_edge - 1].next = -1;
  lk_Gl.TWFG_free_edge_idx = 0;

  /* initialize global_edge_seq_num */
  lk_Gl.global_edge_seq_num = 0;

  /* initialize victim count */
  victim_count = 0;		/* used as index of victims array */

  /* hold the deadlock detection mutex */
  rv = pthread_mutex_lock (&lk_Gl.DL_detection_mutex);

  lf_hash_create_iterator (&iterator, t_entry, &lk_Gl.obj_hash_table);
  res_ptr = lf_hash_iterate (&iterator);

  for (; res_ptr != NULL; res_ptr = lf_hash_iterate (&iterator))
    {
      /* holding resource mutex */
      if (res_ptr->holder == NULL)
	{
	  if (res_ptr->waiter == NULL)
	    {
	      continue;
	    }
	  else
	    {
#if defined(CUBRID_DEBUG)
	      FILE *lk_fp;
	      time_t cur_time;
	      char time_val[CTIME_MAX];

	      lk_fp = fopen ("lock_waiter_only_info.log", "a");
	      if (lk_fp != NULL)
		{
		  cur_time = time (NULL);
		  (void) ctime_r (&cur_time, time_val);
		  fprintf (lk_fp, "##########################################\n");
		  fprintf (lk_fp, "# current time: %s\n", time_val);
		  lock_dump_resource (lk_fp, res_ptr);
		  fprintf (lk_fp, "##########################################\n");
		  fclose (lk_fp);
		}
#endif /* CUBRID_DEBUG */
	      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_LK_LOCK_WAITER_ONLY, 1, "lock_waiter_only_info.log");

	      if (res_ptr->total_holders_mode != NULL_LOCK)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LK_TOTAL_HOLDERS_MODE, 1, res_ptr->total_holders_mode);
		  res_ptr->total_holders_mode = NULL_LOCK;
		}
	      (void) lock_grant_blocked_waiter (thread_p, res_ptr);
	    }
	}

      /* among holders */
      for (hi = res_ptr->holder; hi != NULL; hi = hi->next)
	{
	  if (hi->blocked_mode == NULL_LOCK)
	    {
	      break;
	    }
	  for (hj = hi->next; hj != NULL; hj = hj->next)
	    {
	      assert (hi->granted_mode >= NULL_LOCK && hi->blocked_mode >= NULL_LOCK);
	      assert (hj->granted_mode >= NULL_LOCK && hj->blocked_mode >= NULL_LOCK);

	      compat1 = lock_Comp[hj->blocked_mode][hi->granted_mode];
	      compat2 = lock_Comp[hj->blocked_mode][hi->blocked_mode];
	      assert (compat1 != DB_NA && compat2 != DB_NA);

	      if (compat1 == false || compat2 == false)
		{
		  (void) lock_add_WFG_edge (hj->tran_index, hi->tran_index, true, hj->thrd_entry->lockwait_stime);
		}

	      compat1 = lock_Comp[hi->blocked_mode][hj->granted_mode];
	      assert (compat1 != DB_NA);

	      if (compat1 == false)
		{
		  (void) lock_add_WFG_edge (hi->tran_index, hj->tran_index, true, hi->thrd_entry->lockwait_stime);
		}
	    }
	}

      /* from waiters in the waiter to holders */
      for (hi = res_ptr->holder; hi != NULL; hi = hi->next)
	{
	  for (hj = res_ptr->waiter; hj != NULL; hj = hj->next)
	    {
	      assert (hi->granted_mode >= NULL_LOCK && hi->blocked_mode >= NULL_LOCK);
	      assert (hj->granted_mode >= NULL_LOCK && hj->blocked_mode >= NULL_LOCK);

	      compat1 = lock_Comp[hj->blocked_mode][hi->granted_mode];
	      compat2 = lock_Comp[hj->blocked_mode][hi->blocked_mode];
	      assert (compat1 != DB_NA && compat2 != DB_NA);

	      if (compat1 == false || compat2 == false)
		{
		  (void) lock_add_WFG_edge (hj->tran_index, hi->tran_index, true, hj->thrd_entry->lockwait_stime);
		}
	    }
	}

      /* from waiters in the waiter to other waiters in the waiter */
      for (hi = res_ptr->waiter; hi != NULL; hi = hi->next)
	{
	  for (hj = hi->next; hj != NULL; hj = hj->next)
	    {
	      assert (hj->blocked_mode >= NULL_LOCK && hi->blocked_mode >= NULL_LOCK);

	      compat1 = lock_Comp[hj->blocked_mode][hi->blocked_mode];
	      assert (compat1 != DB_NA);

	      if (compat1 == false)
		{
		  (void) lock_add_WFG_edge (hj->tran_index, hi->tran_index, false, hj->thrd_entry->lockwait_stime);
		}
	    }
	}
    }

  /* release DL detection mutex */
  pthread_mutex_unlock (&lk_Gl.DL_detection_mutex);

  /* simple notation for using in the following statements */
  TWFG_node = lk_Gl.TWFG_node;
  TWFG_edge = lk_Gl.TWFG_edge;

  /* 
   * deadlock detection and victim selection
   */

  for (k = 1; k < lk_Gl.num_trans; k++)
    {
      TWFG_node[k].current = TWFG_node[k].first_edge;
      TWFG_node[k].ancestor = -1;
    }
  for (k = 1; k < lk_Gl.num_trans; k++)
    {
      if (TWFG_node[k].current == -1)
	{
	  continue;
	}
      s = k;
      TWFG_node[s].ancestor = -2;
      for (; s != -2;)
	{
	  if (TWFG_node[s].checked_by_deadlock_detector == false || TWFG_node[s].thrd_wait_stime == 0
	      || (TWFG_node[s].current != -1
		  && (TWFG_node[s].thrd_wait_stime > TWFG_edge[TWFG_node[s].current].edge_wait_stime)))
	    {
	      /* A new transaction started */
	      TWFG_node[s].first_edge = -1;
	      TWFG_node[s].current = -1;
	    }

	  if (TWFG_node[s].current == -1)
	    {
	      t = TWFG_node[s].ancestor;
	      TWFG_node[s].ancestor = -1;
	      s = t;
	      if (s != -2 && TWFG_node[s].current != -1)
		{
		  assert_release (TWFG_node[s].current >= 0 && TWFG_node[s].current < lk_Gl.max_TWFG_edge);
		  TWFG_node[s].current = TWFG_edge[TWFG_node[s].current].next;
		}
	      continue;
	    }

	  assert_release (TWFG_node[s].current >= 0 && TWFG_node[s].current < lk_Gl.max_TWFG_edge);

	  t = TWFG_edge[TWFG_node[s].current].to_tran_index;

	  if (t == -2)
	    {			/* old WFG edge */
	      TWFG_node[s].current = TWFG_edge[TWFG_node[s].current].next;
	      continue;
	    }

	  if (TWFG_node[t].current == -1)
	    {
	      TWFG_edge[TWFG_node[s].current].to_tran_index = -2;
	      TWFG_node[s].current = TWFG_edge[TWFG_node[s].current].next;
	      continue;
	    }

	  if (TWFG_node[t].checked_by_deadlock_detector == false || TWFG_node[t].thrd_wait_stime == 0
	      || TWFG_node[t].thrd_wait_stime > TWFG_edge[TWFG_node[t].current].edge_wait_stime)
	    {
	      TWFG_node[t].first_edge = -1;
	      TWFG_node[t].current = -1;
	      TWFG_edge[TWFG_node[s].current].to_tran_index = -2;
	      TWFG_node[s].current = TWFG_edge[TWFG_node[s].current].next;
	      continue;
	    }

	  if (TWFG_edge[TWFG_node[s].current].edge_seq_num < TWFG_node[t].tran_edge_seq_num)
	    {			/* old WFG edge */
	      TWFG_edge[TWFG_node[s].current].to_tran_index = -2;
	      TWFG_node[s].current = TWFG_edge[TWFG_node[s].current].next;
	      continue;
	    }

	  if (TWFG_node[t].ancestor != -1)
	    {
	      /* A deadlock cycle is found */
	      lock_select_deadlock_victim (thread_p, s, t);
	      if (victim_count >= LK_MAX_VICTIM_COUNT)
		{
		  goto final;
		}
	    }
	  else
	    {
	      TWFG_node[t].ancestor = s;
	      TWFG_node[t].candidate = TWFG_edge[TWFG_node[s].current].holder_flag;
	    }
	  s = t;
	}
    }

final:

#if defined(ENABLE_SYSTEMTAP)
  if (victim_count > 0)
    {
      CUBRID_TRAN_DEADLOCK ();
    }
#endif /* ENABLE_SYSTEMTAP */

#if defined(SERVER_MODE) && defined(DIAG_DEVEL)
  if (victim_count > 0)
    {
      SET_DIAG_VALUE (diag_executediag, DIAG_OBJ_TYPE_LOCK_DEADLOCK, 1, DIAG_VAL_SETTYPE_INC, NULL);
#if 0				/* ACTIVITY PROFILE */
      ADD_ACTIVITY_DATA (diag_executediag, DIAG_EVENTCLASS_TYPE_SERVER_LOCK_DEADLOCK, "", "", victim_count);
#endif
    }
#endif /* SERVER_MODE && DIAG_DEVEL */

#if defined (ENABLE_UNUSED_FUNCTION)
  if (victim_count > 0)
    {
      size_t size_loc;
      char *ptr;
      FILE *fp = port_open_memstream (&ptr, &size_loc);

      if (fp)
	{
	  lock_dump_deadlock_victims (thread_p, fp);
	  port_close_memstream (fp, &ptr, &size_loc);

	  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_LK_DEADLOCK_SPECIFIC_INFO, 1, ptr);

	  if (ptr != NULL)
	    {
	      free (ptr);
	    }
	}
    }
#endif /* ENABLE_UNUSED_FUNCTION */

  /* dump deadlock cycle to event log file */
  for (k = 0; k < victim_count; k++)
    {
      if (victims[k].tran_index_in_cycle == NULL)
	{
	  continue;
	}

      log_fp = event_log_start (thread_p, "DEADLOCK");
      if (log_fp != NULL)
	{
	  for (i = 0; i < victims[k].num_trans_in_cycle; i++)
	    {
	      tran_index = victims[k].tran_index_in_cycle[i];
	      event_log_print_client_info (tran_index, 0);
	      lock_event_log_tran_locks (thread_p, log_fp, tran_index);
	    }

	  event_log_end (thread_p);
	}

      free_and_init (victims[k].tran_index_in_cycle);
    }

  /* Now solve the deadlocks (cycles) by executing the cycle resolution function (e.g., aborting victim) */
  for (k = 0; k < victim_count; k++)
    {
      if (victims[k].can_timeout)
	{
	  (void) lock_wakeup_deadlock_victim_timeout (victims[k].tran_index);
	}
      else
	{
	  (void) lock_wakeup_deadlock_victim_aborted (victims[k].tran_index);
	}
    }

  /* deallocate memory space used for deadlock detection */
  if (lk_Gl.max_TWFG_edge > LK_MID_TWFG_EDGE_COUNT)
    {
      free_and_init (lk_Gl.TWFG_edge);
    }

  if (victim_count == 0)
    {
      if (lk_Gl.no_victim_case_count < 60)
	{
	  lk_Gl.no_victim_case_count += 1;
	}
      else
	{
	  int worker_threads = 0;
	  int suspended_threads = 0;
	  int thrd_index;
	  THREAD_ENTRY *thrd_ptr;

	  /* Make sure that we have threads available for another client to execute, otherwise Panic... */
	  thread_get_info_threads (NULL, &worker_threads, NULL, &suspended_threads);
	  if (worker_threads == suspended_threads)
	    {
	      /* We must timeout at least one thread, so other clients can execute, otherwise, the server will hang. */
	      thrd_ptr = thread_find_first_lockwait_entry (&thrd_index);
	      while (thrd_ptr != NULL)
		{
		  if (lock_wakeup_deadlock_victim_timeout (thrd_ptr->tran_index) == true)
		    {
		      break;
		    }
		  thrd_ptr = thread_find_next_lockwait_entry (&thrd_index);
		}

	      if (thrd_ptr != NULL)
		{
		  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_LK_NOTENOUGH_ACTIVE_THREADS, 3, worker_threads,
			  logtb_get_number_assigned_tran_indices (), thrd_ptr->tran_index);
		}
	    }
	  lk_Gl.no_victim_case_count = 0;
	}
    }

  /* save the last deadlock run time */
  gettimeofday (&lk_Gl.last_deadlock_run, NULL);

  return;
#endif /* !SERVER_MODE */
}

#if 0				/* NOT_USED */
/*
 */

/*
 * lk_global_deadlock_detection: RUN THE GLOBAL DEADLOCK DETECTION
 * arguments:
 * returns/side-effects: nothing
 * Note: Run the deadlock detection. For every cycle either timeout or
 *              abort a transaction. The timeout option is always preferred
 *              over the unilaterally abort option. When the unilaterally
 *              abort option is exercised, the youngest transaction in the
 *              cycle is selected. The youngest transaction is hopefully the
 *              one that has done less work.
 */
void
lk_global_deadlock_detection (void)
{
#if !defined (SERVER_MODE)
  return;
#else /* !SERVER_MODE */
  int i, j;
  WFG_CYCLE *cycles, *cur_cycle;
  WFG_CYCLE_CASE cycle_case;
  int num_victims;
  int tot_num_victims = 0;
  LK_DEADLOCK_VICTIM victims[LK_MAX_VICTIM_COUNT];
  LK_DEADLOCK_VICTIM *v_p;
  int tran_index;
  TRANID tranid;
  int can_timeout;
  int already_picked;
  int ok;
  int error;
  bool isvictim_tg_waiting;
  bool iscandidate_tg_waiting;

  /* 1. Find all the cycles that are currently involved in the system */
  cycle_case = WFG_CYCLE_YES_PRUNE;
  while (cycle_case == WFG_CYCLE_YES_PRUNE)
    {
      error = wfg_detect_cycle (&cycle_case, &cycles);

      if (error == NO_ERROR && (cycle_case == WFG_CYCLE_YES_PRUNE || cycle_case == WFG_CYCLE_YES))
	{
	  /* There are deadlocks, we must select a victim for each cycle. We try to break a cycle by timing out a
	   * transaction whenever is possible. In any other case, we select a victim for an unilaterally abort. */
	  num_victims = 0;
	  for (cur_cycle = cycles; cur_cycle != NULL && num_victims < LK_MAX_VICTIM_COUNT; cur_cycle = cur_cycle->next)
	    {
	      victims[num_victims].tran_index = NULL_TRAN_INDEX;
	      victims[num_victims].can_timeout = false;
	      already_picked = false;

	      /* Pick a victim for next cycle */
	      for (i = 0; i < cur_cycle->num_trans && already_picked == false; i++)
		{
		  tran_index = cur_cycle->waiters[i].tran_index;
		  for (j = 0; j < num_victims; j++)
		    {
		      if (tran_index == victims[j].tran_index)
			{
			  /* A victim for this cycle has already been picked. The index is part of another cycle */
			  already_picked = true;
			  break;
			}
		    }
		  if (already_picked != true)
		    {
		      tranid = logtb_find_tranid (tran_index);
		      can_timeout = LK_CAN_TIMEOUT (logtb_find_wait_msecs (tran_index));
		      /* Victim selection: 1) Avoid unactive transactions. 2) Prefer a waiter of TG resources. 3)
		       * Prefer a transaction with a closer tiemout. 4) Prefer the youngest transaction. */
		      /* Have we selected a victim or the currently victim is inactive (i.e., in rollback or commit
		       * process), select the new candidate as the victim. */
		      ok = 0;

		      /* 
		       * never consider the unactive one as a victim
		       */
		      if (logtb_is_active (tranid) == false)
			continue;

		      if (victims[num_victims].tran_index == NULL_TRAN_INDEX
			  || (logtb_is_active (victims[num_victims].tranid) == false
			      && logtb_is_active (tranid) != false))
			{
			  ok = 1;
			}
		      else
			{
			  isvictim_tg_waiting = wfg_is_tran_group_waiting (victims[num_victims].tran_index);

			  iscandidate_tg_waiting = wfg_is_tran_group_waiting (tran_index);

			  if (isvictim_tg_waiting != NO_ERROR)
			    {
			      if (iscandidate_tg_waiting == NO_ERROR
				  || (victims[num_victims].can_timeout == false && can_timeout == true)
				  || (victims[num_victims].can_timeout == can_timeout
				      && LK_ISYOUNGER (tranid, victims[num_victims].tranid)))
				{
				  ok = 1;
				}
			    }
			  else
			    {
			      if (iscandidate_tg_waiting == NO_ERROR
				  && ((victims[num_victims].can_timeout == false && can_timeout == true)
				      || (victims[num_victims].can_timeout == can_timeout
					  && LK_ISYOUNGER (tranid, victims[num_victims].tranid))))
				{
				  ok = 1;
				}
			    }
			}

		      if (ok == 1)
			{
			  victims[num_victims].tran_index = tran_index;
			  victims[num_victims].tranid = tranid;
			  victims[num_victims].can_timeout = can_timeout;
			  victims[num_victims].cycle_fun = cur_cycle->waiters[i].cycle_fun;
			  victims[num_victims].args = cur_cycle->waiters[i].args;
			}
		    }
		}
	      if (already_picked != true && victims[num_victims].tran_index != NULL_TRAN_INDEX)
		{
		  num_victims++;
		}
	    }

	  /* Now, solve the deadlocks (cycles) by executing the cycle resolution function (e.g., aborting victim) */
	  for (i = 0; i < num_victims; i++)
	    {
	      *v_p = victims[i];
	      if (v_p->cycle_fun != NULL)
		{
		  /* There is a function to solve the cycle. */
		  if ((*v_p->cycle_fun) (v_p->tran_index, v_p->args) == NO_ERROR)
		    ok = true;
		  else
		    ok = false;
		}
	      else
		{
		  ok = false;
		}

	      /* If a function to break the cycle was not provided or the function failed, the transaction is
	       * aborted/timed-out */
	      if (ok == false)
		{
		  if (v_p->can_timeout == false)
		    {
		      if (lock_wakeup_deadlock_victim_aborted (v_p->tran_index) == false)
			msql_tm_abort_detected (v_p->tran_index, NULL);
		    }
		  else
		    {
		      if (lock_wakeup_deadlock_victim_timeout (v_p->tran_index) == false)
			msql_tm_timeout_detected (v_p->tran_index, NULL);
		    }
		}
	    }
	  wfg_free_cycle (cycles);

	  tot_num_victims += num_victims;

	  if (num_victims >= LK_MAX_VICTIM_COUNT)
	    cycle_case = WFG_CYCLE_YES_PRUNE;
	}
    }
#endif /* !SERVER_MODE */
}
#endif /* NOT_USED */

/*
 * lock_reacquire_crash_locks - Reacquire given (exclusive) locks
 *
 * return: returns one value of following three:
 *     (LK_GRANTED, LK_NOTGRANTED_DUE_TIMEOUT, LK_NOTGRANTED_DUE_ERROR)
 *
 *   acqlocks(in): list of locks to be acquired
 *   tran_index(in): transaction index
 *                 whose transaction needs to obtain the given locks
 *
 * Note:This function acquires locks (likely exclusive locks) which were
 *     acquired before a crash on behalf of the specified transaction.
 *
 *     Note: This function should only be called during recovery restart
 *           time. The function does not try to get all or none of the locks
 *           since they have already been granted to the transaction before
 *           the crash. If a lock cannot be granted, an error is set and
 *           returned, however, the fucntion will not stop acquiring the rest
 *           of the indicated locks.
 */
int
lock_reacquire_crash_locks (THREAD_ENTRY * thread_p, LK_ACQUIRED_LOCKS * acqlocks, int tran_index)
{
#if !defined (SERVER_MODE)
  return LK_GRANTED;
#else /* !SERVER_MODE */
  int granted = LK_GRANTED, r;
  unsigned int i;
  LK_ENTRY *dummy_ptr;

  if (acqlocks == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LK_BAD_ARGUMENT, 2, "lk_reacquire_crash_locks",
	      "NULL acqlocks pointer");
      return LK_NOTGRANTED_DUE_ERROR;
    }

  /* reacquire given exclusive locks on behalf of the transaction */
  for (i = 0; i < acqlocks->nobj_locks; i++)
    {
      /* 
       * lock wait duration       : LK_INFINITE_WAIT
       * conditional lock request : false
       */
      r = lock_internal_perform_lock_object (thread_p, tran_index, &acqlocks->obj[i].oid, &acqlocks->obj[i].class_oid,
					     acqlocks->obj[i].lock, LK_INFINITE_WAIT, &dummy_ptr, NULL);
      if (r != LK_GRANTED)
	{
	  er_log_debug (ARG_FILE_LINE, "lk_reacquire_crash_locks: The lock cannot be reacquired...");
	  granted = r;
	  continue;
	}
    }
  return granted;
#endif /* !SERVER_MODE */
}

/*
 * lock_unlock_all_shared_get_all_exclusive - Release all shared type locks and
 *                              optionally list the exclusive type locks
 *
 * return: nothing
 *
 *   acqlocks(in/out):Get the list of acquired exclusive locks or NULL
 *
 * Note:Release all shared type locks (i.e., S_LOCK, IS_LOCK, SIX_LOCK
 *     -- demoted to IX_LOCK), and obtain all remianing locks (i.e.,
 *     exclusive locks such as IX_LOCK, X_LOCK).
 *
 *     Note: This function must be called during the two phase commit
 *           protocol of a distributed transaction.
 */
void
lock_unlock_all_shared_get_all_exclusive (THREAD_ENTRY * thread_p, LK_ACQUIRED_LOCKS * acqlocks)
{
#if !defined (SERVER_MODE)
  /* No locks in standalone */
  if (acqlocks != NULL)
    {
      acqlocks->nobj_locks = 0;
      acqlocks->obj = NULL;
    }
  return;
#else /* !SERVER_MODE */
  int tran_index;
  LK_TRAN_LOCK *tran_lock;
  int idx;
  LK_ENTRY *entry_ptr;
  int rv;

  /* some preparation */
  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);

  /************************************/
  /* phase 1: unlock all shared locks */
  /************************************/
  lock_demote_all_shared_class_locks (thread_p, tran_index);
  lock_remove_all_inst_locks (thread_p, tran_index, NULL, S_LOCK);
  lock_remove_all_class_locks (thread_p, tran_index, S_LOCK);

  /************************************/
  /* phase 2: get all exclusive locks */
  /************************************/
  if (acqlocks != NULL)
    {
      /* get a pointer to transaction lock info entry */
      tran_lock = &lk_Gl.tran_lock_table[tran_index];

      /* hold transction lock hold mutex */
      rv = pthread_mutex_lock (&tran_lock->hold_mutex);

      /* get nobj_locks */
      acqlocks->nobj_locks = (unsigned int) (tran_lock->class_hold_count + tran_lock->inst_hold_count);
      if (tran_lock->root_class_hold != NULL)
	{
	  acqlocks->nobj_locks += 1;
	}

      /* allocate momory space for saving exclusive lock information */
      acqlocks->obj = (LK_ACQOBJ_LOCK *) malloc (SIZEOF_LK_ACQOBJ_LOCK * acqlocks->nobj_locks);
      if (acqlocks->obj == NULL)
	{
	  pthread_mutex_unlock (&tran_lock->hold_mutex);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
		  (size_t) (SIZEOF_LK_ACQOBJ_LOCK * acqlocks->nobj_locks));
	  acqlocks->nobj_locks = 0;
	  return;
	}

      /* initialize idx in acqlocks->obj array */
      idx = 0;

      /* collect root class lock information */
      entry_ptr = tran_lock->root_class_hold;
      if (entry_ptr != NULL)
	{
	  assert (tran_index == entry_ptr->tran_index);

	  COPY_OID (&acqlocks->obj[idx].oid, oid_Root_class_oid);
	  OID_SET_NULL (&acqlocks->obj[idx].class_oid);
	  acqlocks->obj[idx].lock = entry_ptr->granted_mode;
	  idx += 1;
	}

      /* collect general class lock information */
      for (entry_ptr = tran_lock->class_hold_list; entry_ptr != NULL; entry_ptr = entry_ptr->tran_next)
	{
	  assert (tran_index == entry_ptr->tran_index);

	  COPY_OID (&acqlocks->obj[idx].oid, &entry_ptr->res_head->key.oid);
	  COPY_OID (&acqlocks->obj[idx].class_oid, oid_Root_class_oid);
	  acqlocks->obj[idx].lock = entry_ptr->granted_mode;
	  idx += 1;
	}

      /* collect instance lock information */
      for (entry_ptr = tran_lock->inst_hold_list; entry_ptr != NULL; entry_ptr = entry_ptr->tran_next)
	{
	  assert (tran_index == entry_ptr->tran_index);

	  COPY_OID (&acqlocks->obj[idx].oid, &entry_ptr->res_head->key.oid);
	  COPY_OID (&acqlocks->obj[idx].class_oid, &entry_ptr->res_head->key.class_oid);
	  acqlocks->obj[idx].lock = entry_ptr->granted_mode;
	  idx += 1;
	}

      /* release transaction lock hold mutex */
      pthread_mutex_unlock (&tran_lock->hold_mutex);
    }
  return;
#endif /* !SERVER_MODE */
}

/*
 * lock_dump_acquired - Dump structure of acquired locks
 *
 * return: nothing
 *
 *   acqlocks(in): The acquired locks
 *
 * Note:Dump the structure of acquired locks
 */
void
lock_dump_acquired (FILE * fp, LK_ACQUIRED_LOCKS * acqlocks)
{
#if !defined (SERVER_MODE)
  return;
#else /* !SERVER_MODE */
  unsigned int i;

  /* Dump object locks */
  if (acqlocks->obj != NULL && acqlocks->nobj_locks > 0)
    {
      fprintf (fp, "Object_locks: count = %d\n", acqlocks->nobj_locks);
      for (i = 0; i < acqlocks->nobj_locks; i++)
	{
	  fprintf (fp, "   |%d|%d|%d| %s\n", acqlocks->obj[i].oid.volid, acqlocks->obj[i].oid.pageid,
		   acqlocks->obj[i].oid.slotid, LOCK_TO_LOCKMODE_STRING (acqlocks->obj[i].lock));
	}
    }
#endif /* !SERVER_MODE */
}

/*
 * xlock_dump - Dump the contents of lock table
 *
 * return: nothing
 *
 *   outfp(in): FILE stream where to dump the lock table. If NULL is given,
 *            it is dumped to stdout.
 *
 * Note:Dump the lock and waiting tables for both objects and pages.
 *              That is, the lock activity of the datbase. It may be useful
 *              for finding concurrency problems and locking bottlenecks on
 *              an application, so that you can set the appropiate isolation
 *              level or modify the design of the application.
 */
void
xlock_dump (THREAD_ENTRY * thread_p, FILE * outfp)
{
#if !defined (SERVER_MODE)
  return;
#else /* !SERVER_MODE */
  LF_HASH_TABLE_ITERATOR iterator;

  LF_TRAN_ENTRY *t_entry = thread_get_tran_entry (thread_p, THREAD_TS_OBJ_LOCK_RES);
  char *client_prog_name;	/* Client program name for tran */
  char *client_user_name;	/* Client user name for tran */
  char *client_host_name;	/* Client host for tran */
  int client_pid;		/* Client process id for tran */
  TRAN_ISOLATION isolation;	/* Isolation for client tran */
  TRAN_STATE state;
  int wait_msecs;
  int old_wait_msecs = 0;	/* Old transaction lock wait */
  int tran_index;
  LK_RES *res_ptr;
  int num_locked;
  float lock_timeout_sec;
  char lock_timeout_string[64];

  if (outfp == NULL)
    {
      outfp = stdout;
    }

  fprintf (outfp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOCK, MSGCAT_LK_NEWLINE));
  fprintf (outfp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOCK, MSGCAT_LK_DUMP_LOCK_TABLE),
	   prm_get_integer_value (PRM_ID_LK_ESCALATION_AT), prm_get_float_value (PRM_ID_LK_RUN_DEADLOCK_INTERVAL));

  /* Don't get block from anything when dumping object lock table. */
  old_wait_msecs = xlogtb_reset_wait_msecs (thread_p, LK_FORCE_ZERO_WAIT);

  /* Dump some information about all transactions */
  fprintf (outfp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOCK, MSGCAT_LK_NEWLINE));
  for (tran_index = 0; tran_index < lk_Gl.num_trans; tran_index++)
    {
      if (logtb_find_client_name_host_pid (tran_index, &client_prog_name, &client_user_name, &client_host_name,
					   &client_pid) != NO_ERROR)
	{
	  /* Likely this index is not assigned */
	  continue;
	}
      isolation = logtb_find_isolation (tran_index);
      state = logtb_find_state (tran_index);
      wait_msecs = logtb_find_wait_msecs (tran_index);
      lock_timeout_sec = lock_wait_msecs_to_secs (wait_msecs);

      if (lock_timeout_sec > 0)
	{
	  sprintf (lock_timeout_string, ": %.2f", lock_timeout_sec);
	}
      else if ((int) lock_timeout_sec == LK_ZERO_WAIT || (int) lock_timeout_sec == LK_FORCE_ZERO_WAIT)
	{
	  sprintf (lock_timeout_string, ": No wait");
	}
      else if ((int) lock_timeout_sec == LK_INFINITE_WAIT)
	{
	  sprintf (lock_timeout_string, ": Infinite wait");
	}
      else
	{
	  assert_release (0);
	  sprintf (lock_timeout_string, ": %d", (int) lock_timeout_sec);
	}

      fprintf (outfp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOCK, MSGCAT_LK_DUMP_TRAN_IDENTIFIERS),
	       tran_index, client_prog_name, client_user_name, client_host_name, client_pid);
      fprintf (outfp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOCK, MSGCAT_LK_DUMP_TRAN_ISOLATION),
	       log_isolation_string (isolation));
      fprintf (outfp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOCK, MSGCAT_LK_DUMP_TRAN_STATE),
	       log_state_string (state));
      fprintf (outfp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOCK, MSGCAT_LK_DUMP_TRAN_TIMEOUT_PERIOD),
	       lock_timeout_string);
      fprintf (outfp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOCK, MSGCAT_LK_NEWLINE));
    }

  /* compute number of lock res entries */
  num_locked =
    lk_Gl.obj_hash_table.freelist->alloc_cnt - lk_Gl.obj_hash_table.freelist->retired_cnt -
    lk_Gl.obj_hash_table.freelist->available_cnt;
  num_locked = MAX (num_locked, 0);

  /* dump object lock table */
  fprintf (outfp, "Object Lock Table:\n");
  fprintf (outfp, "\tCurrent number of objects which are locked    = %d\n", num_locked);
  fprintf (outfp, "\tMaximum number of objects which can be locked = %d\n\n", lk_Gl.obj_hash_table.freelist->alloc_cnt);

  lf_hash_create_iterator (&iterator, t_entry, &lk_Gl.obj_hash_table);
  res_ptr = lf_hash_iterate (&iterator);
  for (; res_ptr != NULL; res_ptr = lf_hash_iterate (&iterator))
    {
      lock_dump_resource (thread_p, outfp, res_ptr);
    }

  /* Reset the wait back to the way it was */
  (void) xlogtb_reset_wait_msecs (thread_p, old_wait_msecs);

  return;
#endif /* !SERVER_MODE */
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * lock_check_consistency - Check consistency of lock table
 *
 * return: nothing
 *
 * Note:Check consistency of lock table.
 *     This function is used for debugging purposes.
 */
void
lock_check_consistency (THREAD_ENTRY * thread_p)
{
#if !defined (SERVER_MODE)
  return;
#else /* !SERVER_MODE */
  int consistent = true;
  int hash_index;
  LK_HASH *hash_anchor;
  LK_RES *res_ptr;
  int tran_index;
  LK_TRAN_LOCK *tran_lock;
  int rv;

  /* check the consisteny in object lock table */
  for (hash_index = 0; hash_index < lk_Gl.obj_hash_size; hash_index++)
    {
      hash_anchor = &lk_Gl.obj_hash_table[hash_index];
      rv = pthread_mutex_lock (&hash_anchor->hash_mutex);
      res_ptr = hash_anchor->hash_next;
      while (res_ptr != NULL)
	{
	  if (lock_check_consistent_resource (thread_p, res_ptr) == false)
	    {
	      consistent = false;
	      fprintf (stderr, "lk_consistent: res_ptr is inconsistent.\n");
	      break;
	    }
	  res_ptr = res_ptr->hash_next;
	}
      pthread_mutex_unlock (&hash_anchor->hash_mutex);
      if (consistent == false)
	{
	  return;
	}
    }

  /* check transaction lock information */
  for (tran_index = 0; tran_index < lk_Gl.num_trans; tran_index++)
    {
      tran_lock = &lk_Gl.tran_lock_table[tran_index];
      if (lock_check_consistent_tran_lock (tran_lock) == false)
	{
	  fprintf (stderr, "lk_consistent: tran_lock is inconsistent.\n");
	  consistent = false;
	  break;
	}
    }
  return;
#endif /* !SERVER_MODE */
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * lock_initialize_composite_lock -
 *
 * return: error code
 *
 *   comp_lock(in):
 */
int
lock_initialize_composite_lock (THREAD_ENTRY * thread_p, LK_COMPOSITE_LOCK * comp_lock)
{
#if !defined (SERVER_MODE)
  return NO_ERROR;
#else /* !SERVER_MODE */
  LK_LOCKCOMP *lockcomp;

  lockcomp = &(comp_lock->lockcomp);
  lockcomp->tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  lockcomp->wait_msecs = logtb_find_wait_msecs (lockcomp->tran_index);
  lockcomp->class_list = NULL;
  lockcomp->root_class_ptr = NULL;
  return NO_ERROR;
#endif /* !SERVER_MODE */
}

/*
 * lock_add_composite_lock -
 *
 * return: error code
 *
 *   comp_lock(in):
 *   oid(in):
 *   class_oid(in):
 *   lock(in):
 */
int
lock_add_composite_lock (THREAD_ENTRY * thread_p, LK_COMPOSITE_LOCK * comp_lock, const OID * oid, const OID * class_oid)
{
#if !defined (SERVER_MODE)
  return NO_ERROR;
#else /* !SERVER_MODE */
  LK_LOCKCOMP *lockcomp;
  LK_LOCKCOMP_CLASS *lockcomp_class;
  OID *p;
  int max_oids;
  bool need_free;
  int ret = NO_ERROR;

  need_free = false;		/* init */

  lockcomp = &(comp_lock->lockcomp);
  for (lockcomp_class = lockcomp->class_list; lockcomp_class != NULL; lockcomp_class = lockcomp_class->next)
    {
      if (OID_EQ (class_oid, &lockcomp_class->class_oid))
	{
	  break;
	}
    }

  if (lockcomp_class == NULL)
    {				/* class is not found */
      /* allocate lockcomp_class */
      lockcomp_class = (LK_LOCKCOMP_CLASS *) db_private_alloc (thread_p, sizeof (LK_LOCKCOMP_CLASS));
      if (lockcomp_class == NULL)
	{
	  ret = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto exit_on_error;
	}

      need_free = true;

      lockcomp_class->inst_oid_space = NULL;	/* init */

      if (lockcomp->root_class_ptr == NULL)
	{
	  lockcomp->root_class_ptr = lock_get_class_lock (oid_Root_class_oid, lockcomp->tran_index);
	}

      /* initialize lockcomp_class */
      COPY_OID (&lockcomp_class->class_oid, class_oid);
      if (lock_internal_perform_lock_object (thread_p, lockcomp->tran_index, class_oid, NULL, IX_LOCK,
					     lockcomp->wait_msecs, &lockcomp_class->class_lock_ptr,
					     lockcomp->root_class_ptr) != LK_GRANTED)
	{
	  ret = ER_FAILED;
	  goto exit_on_error;
	}
      if (IS_WRITE_EXCLUSIVE_LOCK (lockcomp_class->class_lock_ptr->granted_mode))
	{
	  lockcomp_class->inst_oid_space = NULL;
	}
      else
	{
	  if (LK_COMPOSITE_LOCK_OID_INCREMENT < prm_get_integer_value (PRM_ID_LK_ESCALATION_AT))
	    {
	      lockcomp_class->max_inst_oids = LK_COMPOSITE_LOCK_OID_INCREMENT;
	    }
	  else
	    {
	      lockcomp_class->max_inst_oids = prm_get_integer_value (PRM_ID_LK_ESCALATION_AT);
	    }

	  lockcomp_class->inst_oid_space =
	    (OID *) db_private_alloc (thread_p, sizeof (OID) * lockcomp_class->max_inst_oids);
	  if (lockcomp_class->inst_oid_space == NULL)
	    {
	      ret = ER_OUT_OF_VIRTUAL_MEMORY;
	      goto exit_on_error;
	    }
	  lockcomp_class->num_inst_oids = 0;
	}

      /* connect lockcomp_class into the class_list of lockcomp */
      lockcomp_class->next = lockcomp->class_list;
      lockcomp->class_list = lockcomp_class;

      need_free = false;
    }

  if (lockcomp_class->class_lock_ptr->granted_mode < X_LOCK)
    {
      if (lockcomp_class->num_inst_oids == lockcomp_class->max_inst_oids)
	{
	  if (lockcomp_class->max_inst_oids < prm_get_integer_value (PRM_ID_LK_ESCALATION_AT))
	    {
	      if ((lockcomp_class->max_inst_oids + LK_COMPOSITE_LOCK_OID_INCREMENT) <
		  prm_get_integer_value (PRM_ID_LK_ESCALATION_AT))
		{
		  max_oids = lockcomp_class->max_inst_oids + LK_COMPOSITE_LOCK_OID_INCREMENT;
		}
	      else
		{
		  max_oids = prm_get_integer_value (PRM_ID_LK_ESCALATION_AT);
		}
	      p = (OID *) db_private_realloc (thread_p, lockcomp_class->inst_oid_space, sizeof (OID) * max_oids);
	      if (p == NULL)
		{
		  ret = ER_OUT_OF_VIRTUAL_MEMORY;
		  goto exit_on_error;
		}

	      lockcomp_class->inst_oid_space = p;
	      lockcomp_class->max_inst_oids = max_oids;
	    }
	}

      if (lockcomp_class->num_inst_oids < lockcomp_class->max_inst_oids)
	{
	  COPY_OID (&lockcomp_class->inst_oid_space[lockcomp_class->num_inst_oids], oid);
	  lockcomp_class->num_inst_oids++;
	}
      /* else, lockcomp_class->max_inst_oids equals PRM_LK_ESCALATION_AT. lock escalation will be performed. so no more 
       * instance OID is stored. */
    }

  assert (ret == NO_ERROR);

end:

  if (need_free)
    {
      if (lockcomp_class->inst_oid_space)
	{
	  db_private_free_and_init (thread_p, lockcomp_class->inst_oid_space);
	}
      db_private_free_and_init (thread_p, lockcomp_class);
    }

  return ret;

exit_on_error:

  assert (ret != NO_ERROR);
  if (ret == NO_ERROR)
    {
      ret = ER_FAILED;
    }

  goto end;
#endif /* !SERVER_MODE */
}

/*
 * lock_finalize_composite_lock -
 *
 * return:
 *
 *   comp_lock(in):
 */
int
lock_finalize_composite_lock (THREAD_ENTRY * thread_p, LK_COMPOSITE_LOCK * comp_lock)
{
#if !defined (SERVER_MODE)
  return LK_GRANTED;
#else /* !SERVER_MODE */
  LK_LOCKCOMP *lockcomp;
  LK_LOCKCOMP_CLASS *lockcomp_class;
  LK_ENTRY *dummy;
  int i, value = LK_GRANTED;

  lockcomp = &(comp_lock->lockcomp);
  for (lockcomp_class = lockcomp->class_list; lockcomp_class != NULL; lockcomp_class = lockcomp_class->next)
    {
      if (IS_WRITE_EXCLUSIVE_LOCK (lockcomp_class->class_lock_ptr->granted_mode)
	  || lockcomp_class->num_inst_oids == prm_get_integer_value (PRM_ID_LK_ESCALATION_AT))
	{
	  /* hold X_LOCK on the class object */
	  value = lock_internal_perform_lock_object (thread_p, lockcomp->tran_index, &lockcomp_class->class_oid, NULL,
						     X_LOCK, lockcomp->wait_msecs, &dummy, lockcomp->root_class_ptr);
	  if (value != LK_GRANTED)
	    {
	      break;
	    }
	}
      else
	{
	  /* hold X_LOCKs on the instance objects */
	  for (i = 0; i < lockcomp_class->num_inst_oids; i++)
	    {
	      value = lock_internal_perform_lock_object (thread_p, lockcomp->tran_index,
							 &lockcomp_class->inst_oid_space[i],
							 &lockcomp_class->class_oid, X_LOCK, lockcomp->wait_msecs,
							 &dummy, lockcomp_class->class_lock_ptr);
	      if (value != LK_GRANTED)
		{
		  break;
		}
	    }
	  if (value != LK_GRANTED)
	    {
	      break;
	    }
	}
    }

  /* free alloced memory for composite locking */
  lock_abort_composite_lock (comp_lock);

  return value;
#endif /* !SERVER_MODE */
}

/*
 * lock_abort_composite_lock -
 *
 * return:
 *
 *   comp_lock(in):
 */
void
lock_abort_composite_lock (LK_COMPOSITE_LOCK * comp_lock)
{
#if !defined (SERVER_MODE)
  return;
#else /* !SERVER_MODE */
  LK_LOCKCOMP *lockcomp;
  LK_LOCKCOMP_CLASS *lockcomp_class;

  lockcomp = &(comp_lock->lockcomp);
  lockcomp->tran_index = NULL_TRAN_INDEX;
  lockcomp->wait_msecs = 0;
  while (lockcomp->class_list != NULL)
    {
      lockcomp_class = lockcomp->class_list;
      lockcomp->class_list = lockcomp_class->next;
      if (lockcomp_class->inst_oid_space)
	{
	  db_private_free_and_init (NULL, lockcomp_class->inst_oid_space);
	}
      db_private_free_and_init (NULL, lockcomp_class);
    }

#endif /* !SERVER_MODE */
}

/*
 * lock_is_class_lock_escalated - check if class lock is escalated
 *
 * return: true if class lock is escalated, false otherwise
 *
 *   class_lock(in): class lock
 *   lock_escalation(in): lock escalation
 */
static bool
lock_is_class_lock_escalated (LOCK class_lock, LOCK lock_escalation)
{
#if !defined (SERVER_MODE)
  return false;
#else
  if (class_lock < lock_escalation && !IS_WRITE_EXCLUSIVE_LOCK (class_lock))
    {
      return false;
    }

  if (class_lock == IX_LOCK && lock_escalation == S_LOCK)
    {
      return false;
    }

  return true;
#endif
}

/*
 * lock_get_number_object_locks - Number of object lock entries
 *
 * return:
 *
 * Note:Find the number of total object lock entries of all
 *              transactions
 */
unsigned int
lock_get_number_object_locks (void)
{
#if defined(SA_MODE)
  return 0;
#else
  int available = lk_Gl.obj_hash_table.freelist->available_cnt;
  int retired = lk_Gl.obj_hash_table.freelist->retired_cnt;
  int allocd = lk_Gl.obj_hash_table.freelist->alloc_cnt;

  /* might fetch values mid-operation, so impose a lower cap */
  return MAX (allocd - available - retired, 0);
#endif
}

/*
 * lock_start_instant_lock_mode -
 *
 * return:
 *
 *   tran_index(in):
 */
void
lock_start_instant_lock_mode (int tran_index)
{
#if !defined (SERVER_MODE)
  return;
#else /* !SERVER_MODE */
  LK_TRAN_LOCK *tran_lock;

  tran_lock = &lk_Gl.tran_lock_table[tran_index];
  tran_lock->is_instant_duration = true;
  return;
#endif /* !SERVER_MODE */
}

/*
 * lock_stop_instant_lock_mode -
 *
 * return:
 *
 *   tran_index(in):
 *   need_unlock(in):
 */
void
lock_stop_instant_lock_mode (THREAD_ENTRY * thread_p, int tran_index, bool need_unlock)
{
#if !defined (SERVER_MODE)
  return;
#else /* !SERVER_MODE */
  LK_TRAN_LOCK *tran_lock;
  LK_ENTRY *entry_ptr, *next_ptr;
  int count;

  tran_lock = &lk_Gl.tran_lock_table[tran_index];

  if (!tran_lock->is_instant_duration)
    {
      /* if already stopped, return */
      return;
    }

  /* remove instance locks */
  entry_ptr = tran_lock->inst_hold_list;
  while (entry_ptr != NULL)
    {
      assert (tran_index == entry_ptr->tran_index);

      next_ptr = entry_ptr->tran_next;
      count = entry_ptr->instant_lock_count;
      assert_release (count >= 0);
      if (need_unlock)
	{
	  assert_release (count >= 0);
	  while (count > 0)
	    {
	      lock_internal_perform_unlock_object (thread_p, entry_ptr, false, true);
	      count--;
	    }
	}
      entry_ptr->instant_lock_count = 0;
      entry_ptr = next_ptr;
    }

  /* remove class locks */
  entry_ptr = tran_lock->class_hold_list;
  while (entry_ptr != NULL)
    {
      assert (tran_index == entry_ptr->tran_index);

      next_ptr = entry_ptr->tran_next;
      count = entry_ptr->instant_lock_count;
      assert_release (count >= 0);
      if (need_unlock)
	{
	  assert_release (count >= 0);
	  while (count > 0)
	    {
	      lock_internal_perform_unlock_object (thread_p, entry_ptr, false, true);
	      count--;
	    }
	}
      entry_ptr->instant_lock_count = 0;
      entry_ptr = next_ptr;
    }

  /* remove root class lock */
  entry_ptr = tran_lock->root_class_hold;
  if (entry_ptr != NULL)
    {
      assert (tran_index == entry_ptr->tran_index);

      count = entry_ptr->instant_lock_count;
      assert_release (count >= 0);
      if (need_unlock)
	{
	  assert_release (count >= 0);
	  while (count > 0)
	    {
	      lock_internal_perform_unlock_object (thread_p, entry_ptr, false, true);
	      count--;
	    }
	}
      entry_ptr->instant_lock_count = 0;
    }

  /* change locking phase as normal */
  tran_lock->is_instant_duration = false;
  return;
#endif /* !SERVER_MODE */
}

/* lock_clear_deadlock_victim:
 *
 * tran_index(in):
 */
void
lock_clear_deadlock_victim (int tran_index)
{
#if !defined (SERVER_MODE)
  return;
#else /* !SERVER_MODE */
  int rv;

  /* communication with deadlock detector */
  if (lk_Gl.TWFG_node[tran_index].checked_by_deadlock_detector)
    {
      lk_Gl.TWFG_node[tran_index].checked_by_deadlock_detector = false;
    }
  if (lk_Gl.TWFG_node[tran_index].DL_victim)
    {
      rv = pthread_mutex_lock (&lk_Gl.DL_detection_mutex);
      lk_Gl.TWFG_node[tran_index].DL_victim = false;
      pthread_mutex_unlock (&lk_Gl.DL_detection_mutex);
    }
#endif /* !SERVER_MODE */
}

/*
 * lock_is_instant_lock_mode -
 *
 * return:
 *
 *   tran_index(in):
 */
bool
lock_is_instant_lock_mode (int tran_index)
{
#if !defined (SERVER_MODE)
  return false;
#else /* !SERVER_MODE */
  LK_TRAN_LOCK *tran_lock;

  tran_lock = &lk_Gl.tran_lock_table[tran_index];
  return tran_lock->is_instant_duration;
#endif /* !SERVER_MODE */
}

#if defined(SERVER_MODE)
/*
 * lock_increment_class_granules () - increment the lock counter for a class
 * return : void
 * class_entry (in/out)	     : class entry
 *
 */
static void
lock_increment_class_granules (LK_ENTRY * class_entry)
{
  if (class_entry == NULL || class_entry->res_head->key.type != LOCK_RESOURCE_CLASS)
    {
      return;
    }

  class_entry->ngranules++;
  if (class_entry->class_entry != NULL && !OID_IS_ROOTOID (&class_entry->class_entry->res_head->key.oid))
    {
      /* This is a class in a class hierarchy so increment the number of granules for the superclass */
      class_entry->class_entry->ngranules++;
    }
}

/*
 * lock_decrement_class_granules () - decrement the lock counter for a class
 * return : void
 * class_entry (in/out)	     : class entry
 *
 */
static void
lock_decrement_class_granules (LK_ENTRY * class_entry)
{
  if (class_entry == NULL || class_entry->res_head->key.type != LOCK_RESOURCE_CLASS)
    {
      return;
    }

  class_entry->ngranules--;
  if (class_entry->class_entry != NULL && !OID_IS_ROOTOID (&class_entry->class_entry->res_head->key.oid))
    {
      /* This is a class in a class hierarchy so decrement the number of granules for the superclass */
      class_entry->class_entry->ngranules--;
    }
}

#endif /* SERVER_MODE */

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * lock_get_total_holders_mode - Get the total holders mode
 *
 * return: the total holders mode
 *
 *   oid(in): target object ientifier
 *   class_oid(in): class identifier of the target object
 */

LOCK
lock_get_total_holders_mode (const OID * oid, const OID * class_oid)
{
#if !defined (SERVER_MODE)
  return X_LOCK;
#else /* !SERVER_MODE */
  LF_TRAN_ENTRY *t_entry = thread_get_tran_entry (NULL, THREAD_TS_OBJ_LOCK_RES);
  LK_RES_KEY search_key;
  LOCK total_hold_mode = NULL_LOCK;	/* lock mode */
  LK_RES *res_ptr;

  int rv;
  bool is_class = false;

  if (oid == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LK_BAD_ARGUMENT, 2, "lock_get_total_holders_mode",
	      "NULL OID pointer");
      return NULL_LOCK;
    }

  is_class = OID_EQ (oid, oid_Root_class_oid) || class_oid == NULL || OID_EQ (class_oid, oid_Root_class_oid);

  /* search hash */
  search_key = lock_create_search_key ((OID *) oid, (OID *) class_oid);
  rv = lf_hash_find (t_entry, &lk_Gl.obj_hash_table, (void *) &search_key, (void **) &res_ptr);
  if (rv != NO_ERROR)
    {
      /* hopefully will never happen */
      assert (false);
      return 0;
    }
  else if (res_ptr == NULL)
    {
      return NULL_LOCK;
    }

  if (is_class || OID_EQ (&res_ptr->key.class_oid, class_oid))
    {
      total_hold_mode = res_ptr->total_holders_mode;
    }

  /* release lock hash mutex */
  pthread_mutex_unlock (&res_ptr->res_mutex);

  return total_hold_mode;
#endif /* !SERVER_MODE */
}

/*
 * lock_get_all_except_transaction - Find the acquired lock mode of all except
 *				     tran_index transactions
 * return:
 *
 *   oid(in): target object ientifier
 *   class_oid(in): class identifier of the target object
 *   tran_index(in): the transaction index
 */

LOCK
lock_get_all_except_transaction (const OID * oid, const OID * class_oid, int tran_index)
{
#if !defined (SERVER_MODE)
  return X_LOCK;
#else /* !SERVER_MODE */
  LF_TRAN_ENTRY *t_entry = thread_get_tran_entry (NULL, THREAD_TS_OBJ_LOCK_RES);
  LK_RES_KEY search_key;
  LOCK group_mode;		/* lock mode */
  LK_RES *res_ptr;
  LK_ENTRY *i;
  int rv;
  bool is_class = false;

  if (oid == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LK_BAD_ARGUMENT, 2, "lock_get_all_except_transaction",
	      "NULL OID pointer");
      return NULL_LOCK;
    }

  is_class = OID_EQ (oid, oid_Root_class_oid) || class_oid == NULL || OID_EQ (class_oid, oid_Root_class_oid);

  /* search hash */
  search_key = lock_create_search_key ((OID *) oid, (OID *) class_oid);
  rv = lf_hash_find (t_entry, &lk_Gl.obj_hash_table, (void *) &search_key, (void **) &res_ptr);
  if (rv != NO_ERROR)
    {
      /* hopefully will never happen */
      assert (false);
      return 0;
    }

#if defined (REWRITE)
  if (OID_EQ (&res_ptr->key.oid, oid))
    {
      if (is_class || OID_EQ (&res_ptr->key.class_oid, class_oid))
	{
	  break;
	}
    }
#endif

  if (res_ptr == NULL)
    {
      /* not found */
      return NULL_LOCK;
    }

  /* other holder's granted mode */
  group_mode = NULL_LOCK;
  for (i = res_ptr->holder; i != NULL; i = i->next)
    {
      if (i->tran_index != tran_index)
	{
	  assert (i->granted_mode >= NULL_LOCK && group_mode >= NULL_LOCK);
	  group_mode = lock_Conv[i->granted_mode][group_mode];
	  assert (group_mode != NA_LOCK);
	}
    }

  pthread_mutex_unlock (&res_ptr->res_mutex);

  return group_mode;
#endif /* !SERVER_MODE */
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * lock_get_lock_holder_tran_index -
 *
 * return:
 *  out_buf(out):
 *  waiter_index(in):
 *  res (in):
 *
 *  note : caller must free *out_buf.
 */
int
lock_get_lock_holder_tran_index (THREAD_ENTRY * thread_p, char **out_buf, int waiter_index, LK_RES * res)
{
#if !defined (SERVER_MODE)
  if (res == NULL)
    {
      return NO_ERROR;
    }

  if (out_buf == NULL)
    {
      assert_release (0);
      return ER_FAILED;
    }

  *out_buf = NULL;

  return NO_ERROR;

#else

#define HOLDER_ENTRY_LENGTH (12)
  int rv;
  LK_ENTRY *holder, *waiter;
  int holder_number = 0;
  int buf_size, n, remained_size;
  bool is_valid = false;	/* validation check */
  char *buf, *p;

  if (res == NULL)
    {
      return NO_ERROR;
    }

  if (out_buf == NULL)
    {
      assert_release (0);
      return ER_FAILED;
    }

  *out_buf = NULL;

  rv = pthread_mutex_lock (&res->res_mutex);
  if (rv != 0)
    {
      return ER_FAILED;
    }

  if (OID_ISNULL (&res->key.oid))
    {
      pthread_mutex_unlock (&res->res_mutex);
      return NO_ERROR;
    }

  waiter = res->waiter;
  while (waiter != NULL)
    {
      if (waiter->tran_index == waiter_index)
	{
	  is_valid = true;
	  break;
	}
      waiter = waiter->next;
    }

  if (is_valid == false)
    {
      holder = res->holder;
      while (holder != NULL)
	{
	  if (holder->blocked_mode != NULL_LOCK && holder->tran_index == waiter_index)
	    {
	      is_valid = true;
	      break;
	    }
	  holder = holder->next;
	}
    }

  if (is_valid == false)
    {
      /* not a valid waiter of this resource */
      pthread_mutex_unlock (&res->res_mutex);
      return NO_ERROR;
    }

  holder = res->holder;
  while (holder != NULL)
    {
      if (holder->tran_index != waiter_index)
	{
	  holder_number++;
	}
      holder = holder->next;
    }

  if (holder_number == 0)
    {
      pthread_mutex_unlock (&res->res_mutex);
      return NO_ERROR;
    }

  buf_size = holder_number * HOLDER_ENTRY_LENGTH + 1;
  buf = (char *) malloc (sizeof (char) * buf_size);

  if (buf == NULL)
    {
      pthread_mutex_unlock (&res->res_mutex);

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) buf_size);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  remained_size = buf_size;
  p = buf;

  /* write first holder index */
  holder = res->holder;
  while (holder && holder->tran_index == waiter_index)
    {
      holder = holder->next;
    }

  assert_release (holder != NULL);

  n = snprintf (p, remained_size, "%d", holder->tran_index);
  remained_size -= n;
  p += n;
  assert_release (remained_size >= 0);

  /* write remained holder index */
  holder = holder->next;
  while (holder != NULL)
    {
      if (holder->tran_index != waiter_index)
	{
	  n = snprintf (p, remained_size, ", %d", holder->tran_index);
	  remained_size -= n;
	  p += n;
	  assert_release (remained_size >= 0);
	}
      holder = holder->next;
    }

  *out_buf = buf;

  pthread_mutex_unlock (&res->res_mutex);

  return NO_ERROR;
#endif
}

/*
 * lock_wait_state_to_string () - Translate lock wait state into string 
 *                                representation
 *   return:
 *   state(in): lock wait state
 */
const char *
lock_wait_state_to_string (int state)
{
  switch (state)
    {
    case LOCK_SUSPENDED:
      return "SUSPENDED";
    case LOCK_RESUMED:
      return "RESUMED";
    case LOCK_RESUMED_TIMEOUT:
      return "RESUMED_TIMEOUT";
    case LOCK_RESUMED_DEADLOCK_TIMEOUT:
      return "RESUMED_DEADLOCK_TIMEOUT";
    case LOCK_RESUMED_ABORTED:
      return "RESUMED_ABORTED";
    case LOCK_RESUMED_ABORTED_FIRST:
      return "RESUMED_ABORTED_FIRST";
    case LOCK_RESUMED_ABORTED_OTHER:
      return "RESUMED_ABORTED_OTHER";
    case LOCK_RESUMED_INTERRUPT:
      return "RESUMED_INTERRUPT";
    }
  return "UNKNOWN";
}

/*
 * lock dump to event log file (lock timeout, deadlock)
 */

#if defined(SERVER_MODE)
/*
 * lock_event_log_tran_locks - dump transaction locks to event log file
 *   return:
 *   thread_p(in):
 *   log_fp(in):
 *   tran_index(in):
 *
 *   note: for deadlock
 */
static void
lock_event_log_tran_locks (THREAD_ENTRY * thread_p, FILE * log_fp, int tran_index)
{
  int rv, i, indent = 2;
  LK_TRAN_LOCK *tran_lock;
  LK_ENTRY *entry;

  assert (csect_check_own (thread_p, CSECT_EVENT_LOG_FILE) == 1);

  fprintf (log_fp, "hold:\n");

  tran_lock = &lk_Gl.tran_lock_table[tran_index];
  rv = pthread_mutex_lock (&tran_lock->hold_mutex);

  entry = tran_lock->inst_hold_list;
  for (i = 0; entry != NULL && i < MAX_NUM_LOCKS_DUMP_TO_EVENT_LOG; entry = entry->tran_next, i++)
    {
      assert (tran_index == entry->tran_index);

      fprintf (log_fp, "%*clock: %s", indent, ' ', LOCK_TO_LOCKMODE_STRING (entry->granted_mode));

      SET_EMULATE_THREAD_WITH_LOCK_ENTRY (thread_p, entry);
      lock_event_log_lock_info (thread_p, log_fp, entry);

      event_log_sql_string (thread_p, log_fp, &entry->xasl_id, indent);
      event_log_bind_values (log_fp, tran_index, entry->bind_index_in_tran);

      fprintf (log_fp, "\n");

      CLEAR_EMULATE_THREAD (thread_p);
    }

  if (entry != NULL)
    {
      fprintf (log_fp, "%*c...\n", indent, ' ');
    }

  entry = tran_lock->waiting;
  if (entry != NULL)
    {
      fprintf (log_fp, "wait:\n");
      fprintf (log_fp, "%*clock: %s", indent, ' ', LOCK_TO_LOCKMODE_STRING (entry->blocked_mode));

      SET_EMULATE_THREAD_WITH_LOCK_ENTRY (thread_p, entry);

      lock_event_log_lock_info (thread_p, log_fp, entry);

      event_log_sql_string (thread_p, log_fp, &entry->xasl_id, indent);
      event_log_bind_values (log_fp, tran_index, entry->bind_index_in_tran);

      fprintf (log_fp, "\n");
    }
  CLEAR_EMULATE_THREAD (thread_p);

  pthread_mutex_unlock (&tran_lock->hold_mutex);
}

/*
 * lock_event_log_blocked_lock - dump lock waiter info to event log file
 *   return:
 *   thread_p(in):
 *   log_fp(in):
 *   entry(in):
 *
 *   note: for lock timeout
 */
static void
lock_event_log_blocked_lock (THREAD_ENTRY * thread_p, FILE * log_fp, LK_ENTRY * entry)
{
  int indent = 2;

  assert (csect_check_own (thread_p, CSECT_EVENT_LOG_FILE) == 1);

  SET_EMULATE_THREAD_WITH_LOCK_ENTRY (thread_p, entry);

  fprintf (log_fp, "waiter:\n");
  event_log_print_client_info (entry->tran_index, indent);

  fprintf (log_fp, "%*clock: %s", indent, ' ', LOCK_TO_LOCKMODE_STRING (entry->blocked_mode));
  lock_event_log_lock_info (thread_p, log_fp, entry);

  event_log_sql_string (thread_p, log_fp, &entry->xasl_id, indent);
  event_log_bind_values (log_fp, entry->tran_index, entry->bind_index_in_tran);

  CLEAR_EMULATE_THREAD (thread_p);

  fprintf (log_fp, "\n");
}

/*
 * lock_event_log_blocking_locks - dump lock blocker info to event log file
 *   return:
 *   thread_p(in):
 *   log_fp(in):
 *   wait_entry(in):
 *
 *   note: for lock timeout
 */
static void
lock_event_log_blocking_locks (THREAD_ENTRY * thread_p, FILE * log_fp, LK_ENTRY * wait_entry)
{
  LK_ENTRY *entry;
  LK_RES *res_ptr = NULL;
  int compat1, compat2, rv, indent = 2;

  assert (csect_check_own (thread_p, CSECT_EVENT_LOG_FILE) == 1);

  res_ptr = wait_entry->res_head;
  rv = pthread_mutex_lock (&res_ptr->res_mutex);

  fprintf (log_fp, "blocker:\n");

  for (entry = res_ptr->holder; entry != NULL; entry = entry->next)
    {
      if (entry == wait_entry)
	{
	  continue;
	}

      compat1 = lock_Comp[entry->granted_mode][wait_entry->blocked_mode];
      compat2 = lock_Comp[entry->blocked_mode][wait_entry->blocked_mode];

      if (compat1 == false || compat2 == false)
	{
	  event_log_print_client_info (entry->tran_index, indent);

	  fprintf (log_fp, "%*clock: %s", indent, ' ', LOCK_TO_LOCKMODE_STRING (entry->granted_mode));

	  SET_EMULATE_THREAD_WITH_LOCK_ENTRY (thread_p, entry);

	  lock_event_log_lock_info (thread_p, log_fp, entry);

	  event_log_sql_string (thread_p, log_fp, &entry->xasl_id, indent);
	  event_log_bind_values (log_fp, entry->tran_index, entry->bind_index_in_tran);

	  CLEAR_EMULATE_THREAD (thread_p);

	  fprintf (log_fp, "\n");
	}
    }

  for (entry = res_ptr->waiter; entry != NULL; entry = entry->next)
    {
      if (entry == wait_entry)
	{
	  continue;
	}

      compat1 = lock_Comp[entry->blocked_mode][wait_entry->blocked_mode];

      if (compat1 == false)
	{
	  event_log_print_client_info (entry->tran_index, indent);

	  fprintf (log_fp, "%*clock: %s", indent, ' ', LOCK_TO_LOCKMODE_STRING (entry->granted_mode));

	  SET_EMULATE_THREAD_WITH_LOCK_ENTRY (thread_p, entry);

	  lock_event_log_lock_info (thread_p, log_fp, entry);

	  event_log_sql_string (thread_p, log_fp, &entry->xasl_id, indent);
	  event_log_bind_values (log_fp, entry->tran_index, entry->bind_index_in_tran);

	  CLEAR_EMULATE_THREAD (thread_p);

	  fprintf (log_fp, "\n");
	}
    }

  pthread_mutex_unlock (&res_ptr->res_mutex);
}

/*
 * lock_event_log_lock_info - dump lock resource info to event log file
 *   return:
 *   thread_p(in):
 *   log_fp(in):
 *   entry(in):
 */
static void
lock_event_log_lock_info (THREAD_ENTRY * thread_p, FILE * log_fp, LK_ENTRY * entry)
{
  LK_RES *res_ptr;
  char *classname, *btname;
  OID *oid_rr;

  assert (csect_check_own (thread_p, CSECT_EVENT_LOG_FILE) == 1);

  res_ptr = entry->res_head;

  fprintf (log_fp, " (oid=%d|%d|%d", res_ptr->key.oid.volid, res_ptr->key.oid.pageid, res_ptr->key.oid.slotid);

  switch (res_ptr->key.type)
    {
    case LOCK_RESOURCE_ROOT_CLASS:
      fprintf (log_fp, ", table=db_root");
      break;

    case LOCK_RESOURCE_CLASS:
      oid_rr = oid_get_rep_read_tran_oid ();
      if (oid_rr != NULL && OID_EQ (&res_ptr->key.oid, oid_rr))
	{
	  /* This is the generic object for RR transactions */
	  fprintf (log_fp, ", Generic object for Repeatable Read consistency");
	}
      else if (!OID_ISTEMP (&res_ptr->key.oid))
	{
	  OID real_class_oid;

	  if (OID_IS_VIRTUAL_CLASS_OF_DIR_OID (&res_ptr->key.oid))
	    {
	      OID_GET_REAL_CLASS_OF_DIR_OID (&res_ptr->key.oid, &real_class_oid);
	    }
	  else
	    {
	      COPY_OID (&real_class_oid, &res_ptr->key.oid);
	    }
	  if (heap_get_class_name (thread_p, &real_class_oid, &classname) != NO_ERROR)
	    {
	      /* ignore */
	      er_clear ();
	    }

	  if (classname != NULL)
	    {
	      fprintf (log_fp, ", table=%s", classname);
	      free_and_init (classname);
	    }
	}
      break;

    case LOCK_RESOURCE_INSTANCE:
      if (!OID_ISTEMP (&res_ptr->key.class_oid))
	{
	  OID real_class_oid;

	  if (OID_IS_VIRTUAL_CLASS_OF_DIR_OID (&res_ptr->key.class_oid))
	    {
	      OID_GET_REAL_CLASS_OF_DIR_OID (&res_ptr->key.class_oid, &real_class_oid);
	    }
	  else
	    {
	      COPY_OID (&real_class_oid, &res_ptr->key.class_oid);
	    }
	  if (heap_get_class_name (thread_p, &real_class_oid, &classname) != NO_ERROR)
	    {
	      /* ignore */
	      er_clear ();

	    }

	  if (classname != NULL)
	    {
	      fprintf (log_fp, ", table=%s", classname);
	      free_and_init (classname);
	    }
	}
      break;

    default:
      break;
    }

  fprintf (log_fp, ")\n");
}

/*
 * lock_event_set_tran_wait_entry - save the lock entry tran is waiting
 *   return:
 *   entry(in):
 */
static void
lock_event_set_tran_wait_entry (int tran_index, LK_ENTRY * entry)
{
  LK_TRAN_LOCK *tran_lock;
  int rv;

  tran_lock = &lk_Gl.tran_lock_table[tran_index];
  rv = pthread_mutex_lock (&tran_lock->hold_mutex);

  tran_lock->waiting = entry;

  if (entry != NULL)
    {
      lock_event_set_xasl_id_to_entry (tran_index, entry);
    }

  pthread_mutex_unlock (&tran_lock->hold_mutex);
}

/*
 * lock_event_set_xasl_id_to_entry - save the xasl id related lock entry
 *   return:
 *   entry(in):
 */
static void
lock_event_set_xasl_id_to_entry (int tran_index, LK_ENTRY * entry)
{
  LOG_TDES *tdes;

  tdes = LOG_FIND_TDES (tran_index);
  if (tdes != NULL && !XASL_ID_IS_NULL (&tdes->xasl_id))
    {
      if (tdes->num_exec_queries <= MAX_NUM_EXEC_QUERY_HISTORY)
	{
	  entry->bind_index_in_tran = tdes->num_exec_queries - 1;
	}
      else
	{
	  entry->bind_index_in_tran = -1;
	}

      XASL_ID_COPY (&entry->xasl_id, &tdes->xasl_id);
    }
  else
    {
      XASL_ID_SET_NULL (&entry->xasl_id);
      entry->bind_index_in_tran = -1;
    }
}
#endif /* SERVER_MODE */

/*
 * lock_rep_read_tran - lock the object used in RR transaction with ALTER TABLE
 *			ADDCOLUMN NOT NULL scenario
 *   return:
 *   thread_p(in):
 *   lock(in): type of lock
 *   cond_flag
 */
int
lock_rep_read_tran (THREAD_ENTRY * thread_p, LOCK lock, int cond_flag)
{
#if !defined (SERVER_MODE)
  LK_SET_STANDALONE_XLOCK (lock);
  return NO_ERROR;
#else /* !SERVER_MODE */
  int tran_index;
  int wait_msecs;
  OID *rep_read_oid = oid_get_rep_read_tran_oid ();
  LK_ENTRY *entry_addr = NULL;

  if (lock == NULL_LOCK)
    {
      return NO_ERROR;
    }

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  if (cond_flag == LK_COND_LOCK)	/* conditional request */
    {
      wait_msecs = LK_FORCE_ZERO_WAIT;
    }
  else
    {
      wait_msecs = logtb_find_wait_msecs (tran_index);
    }

  if (lock_internal_perform_lock_object (thread_p, tran_index, rep_read_oid, NULL, lock, wait_msecs, &entry_addr,
					 NULL) != LK_GRANTED)
    {
      return ER_FAILED;
    }

  return NO_ERROR;
#endif
}

#if defined (SERVER_MODE)
static bool
lock_is_safe_lock_with_page (THREAD_ENTRY * thread_p, LK_ENTRY * entry_ptr)
{
  LK_RES *lock_res;
  bool is_safe = true;

  lock_res = entry_ptr->res_head;
  if (lock_res != NULL)
    {
      is_safe = false;
      if (lock_res->key.type == LOCK_RESOURCE_INSTANCE && OID_IS_VIRTUAL_CLASS_OF_DIR_OID (&lock_res->key.class_oid))
	{
	  is_safe = true;
	}
      else if (lock_res->key.type == LOCK_RESOURCE_CLASS && OID_IS_VIRTUAL_CLASS_OF_DIR_OID (&lock_res->key.oid))
	{
	  is_safe = true;
	}
    }
  return is_safe;
}
#endif /* SERVER_MODE */

#if defined (SERVER_MODE)
/*
 * lock_get_new_entry () - Get new lock entry. Local pool of free entries is
 *			   first used. When this pool is depleted, a new
 *			   entry is claimed from shared list of lock entries.
 *
 * return	   : New lock entry.
 * tran_index (in) : Transaction index of requester.
 * tran_entry (in) : Lock-free transaction entry.
 * freelist (in)   : Lock-free shared list of entries.
 */
static LK_ENTRY *
lock_get_new_entry (int tran_index, LF_TRAN_ENTRY * tran_entry, LF_FREELIST * freelist)
{
  LK_TRAN_LOCK *tran_lock = &lk_Gl.tran_lock_table[tran_index];
  LK_ENTRY *lock_entry;

  /* Check if local pool has free entries. */
  if (tran_lock->lk_entry_pool)
    {
      assert (tran_lock->lk_entry_pool_count > 0);
      lock_entry = tran_lock->lk_entry_pool;
      tran_lock->lk_entry_pool = tran_lock->lk_entry_pool->next;
      tran_lock->lk_entry_pool_count--;
      return lock_entry;
    }

  /* Claim from shared freelist. */
  return lf_freelist_claim (tran_entry, freelist);
}

/*
 * lock_free_entry () - Free lock entry. Local pool has high priority if its
 *			maximum size is not reached. Otherwise, the entry
 *			is "retired" to shared list of free lock entries.
 *
 * return	   : Error code.
 * tran_index (in) : Transaction index.
 * tran_entry (in) : Lock-free transaction entry.
 * freelist (in)   : Lock-free shared list of lock entries.
 * lock_entry (in) : Lock entry being freed.
 */
static void
lock_free_entry (int tran_index, LF_TRAN_ENTRY * tran_entry, LF_FREELIST * freelist, LK_ENTRY * lock_entry)
{
  LK_TRAN_LOCK *tran_lock = &lk_Gl.tran_lock_table[tran_index];

  assert (tran_lock->lk_entry_pool_count >= 0 && tran_lock->lk_entry_pool_count <= LOCK_TRAN_LOCAL_POOL_MAX_SIZE);

  /* "Free" entry to local pool or shared list. */
  if (tran_lock->lk_entry_pool_count < LOCK_TRAN_LOCAL_POOL_MAX_SIZE)
    {
      lock_uninit_entry (lock_entry);
      lock_entry->next = tran_lock->lk_entry_pool;
      tran_lock->lk_entry_pool = lock_entry;
      tran_lock->lk_entry_pool_count++;
    }
  else
    {
      lf_freelist_retire (tran_entry, freelist, lock_entry);
    }
}
#endif

#if defined (SERVER_MODE)
/*
 * lock_unlock_object_by_isolation - No lock is unlocked/demoted for MVCC tables. 
 *			             Shared instance lock on non-MVCC table is unlocked for TRAN_READ_COMMITTED.
 *
 * return : nothing
 * tran_index(in): Transaction index.
 * class_oid(in): class oid.
 * oid(in): instance oid.
 */
static void
lock_unlock_object_by_isolation (THREAD_ENTRY * thread_p, int tran_index, TRAN_ISOLATION isolation,
				 const OID * class_oid, const OID * oid)
{
  assert (class_oid != NULL && oid != NULL);
  assert (!OID_ISNULL (class_oid) && !OID_ISNULL (oid));

  if (isolation != TRAN_READ_COMMITTED)
    {
      return;			/* do nothing */
    }

  /* The intentional lock on the higher lock granule must be kept. */
  if (OID_IS_ROOTOID (oid) || OID_IS_ROOTOID (class_oid))
    {
      /* Don't release locks on classes. READ COMMITTED isolation is only applied on instances, classes must
       * have at least REPEATABLE READ isolation. */
    }
  else if (mvcc_is_mvcc_disabled_class (class_oid))
    {
      /* Release S_LOCK after reading object. */
      lock_unlock_shared_inst_lock (thread_p, tran_index, oid);
    }
  else
    {
      /* MVCC table. READ COMMITTED isolation uses snapshot instead of locks. We don't have to release anything here. */
    }
}

/*
 * lock_unlock_inst_locks_of_class_by_isolation - No lock is unlocked/demoted for MVCC tables. 
 *						  Shared instance locks on non-MVCC table is unlocked for 
 *						  TRAN_READ_COMMITTED.
 *
 * return : nothing
 * tran_index(in): Transaction index.
 * class_oid(in): class oid.
 */
static void
lock_unlock_inst_locks_of_class_by_isolation (THREAD_ENTRY * thread_p, int tran_index, TRAN_ISOLATION isolation,
					      const OID * class_oid)
{
  assert (class_oid != NULL);
  assert (!OID_ISNULL (class_oid));

  if (isolation != TRAN_READ_COMMITTED)
    {
      return;			/* do nothing */
    }

  if (mvcc_is_mvcc_disabled_class (class_oid))
    {
      /* Release S_LOCKs of non-MVCC tables. */
      lock_remove_all_inst_locks (thread_p, tran_index, class_oid, S_LOCK);
    }
  else
    {
      /* MVCC table. READ COMMITTED isolation uses snapshot instead of locks. We don't have to release anything here. */
    }
}
#endif /* SERVER_MODE */
