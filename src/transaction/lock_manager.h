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
 * 	Overview: LOCK MANAGMENT MODULE (AT THE SERVER) -- Interface --
 *
 */

#ifndef _LOCK_MANAGER_H_
#define _LOCK_MANAGER_H_

#ident "$Id$"

#include "config.h"

#include <time.h>
#include <stdio.h>

#include "error_manager.h"
#include "oid.h"
#include "storage_common.h"
#include "locator.h"
#include "log_comm.h"

#if defined(SERVER_MODE)
#include "connection_error.h"
#endif /* SERVER_MODE */
#include "thread.h"

enum
{
  LK_GRANTED = 1,
  LK_NOTGRANTED = 2,
  LK_NOTGRANTED_DUE_ABORTED = 3,
  LK_NOTGRANTED_DUE_TIMEOUT = 4,
  LK_NOTGRANTED_DUE_ERROR = 5,
  LK_GRANTED_PUSHINSET_LOCKONE = 6,
  LK_GRANTED_PUSHINSET_RELOCKALL = 7
};

enum
{
/* Value to timeout immediately.. not wait */
  LK_ZERO_WAIT = 0,
/* Value to wait forever */
  LK_INFINITE_WAIT = -1,
/* Value to force a timeout without setting errors */
  LK_FORCE_ZERO_WAIT = -2
};

enum
{ LK_UNCOND_LOCK, LK_COND_LOCK };

typedef enum
{
  NO_KEY_LOCK_ESCALATION = 0,
  NEED_KEY_LOCK_ESCALATION = 1,
  KEY_LOCK_ESCALATED = 2
} KEY_LOCK_ESCALATION;

typedef struct lk_acquisition_history LK_ACQUISITION_HISTORY;
struct lk_acquisition_history
{
  LOCK req_mode;
  LK_ACQUISITION_HISTORY *next;
  LK_ACQUISITION_HISTORY *prev;
};

/*****************************/
/* Lock Heap Entry Structure */
/*****************************/
typedef struct lk_entry LK_ENTRY;
struct lk_entry
{
#if defined(SERVER_MODE)
  struct lk_res *res_head;	/* back to resource entry           */
  THREAD_ENTRY *thrd_entry;	/* thread entry pointer             */
  int tran_index;		/* transaction table index          */
  LOCK granted_mode;		/* granted lock mode                */
  LOCK blocked_mode;		/* blocked lock mode                */
  int count;			/* number of lock requests          */
  LK_ENTRY *next;		/* next entry                       */
  LK_ENTRY *tran_next;		/* list of locks that trans. holds  */
  LK_ENTRY *tran_prev;		/* list of locks that trans. holds  */
  LK_ENTRY *class_entry;	/* ptr. to class lk_entry           */
  LK_ACQUISITION_HISTORY *history;	/* lock acquisition history         */
  LK_ACQUISITION_HISTORY *recent;	/* last node of history list        */
  int ngranules;		/* number of finer granules         */
  int instant_lock_count;	/* number of instant lock requests  */
  int bind_index_in_tran;
  XASL_ID xasl_id;
  unsigned char scanid_bitset[1];	/* prm_get_integer_value (PRM_ID_LK_MAX_SCANID_BIT)/8];       */
#else				/* not SERVER_MODE */
  int dummy;
#endif				/* not SERVER_MODE */
};

typedef struct lk_acqobj_lock LK_ACQOBJ_LOCK;
struct lk_acqobj_lock
{
  OID oid;			/* lock resource object identifier      */
  OID class_oid;		/* only needed in case of instance lock */
  LOCK lock;			/* lock mode                            */
};

typedef struct lk_acquired_locks LK_ACQUIRED_LOCKS;
struct lk_acquired_locks
{
  LK_ACQOBJ_LOCK *obj;		/* The list of acquired object locks */
  unsigned int nobj_locks;	/* Number of actual object locks     */
};

/* During delete and update operation,
 * if the number of objects to be deleted or updated is larger than
 * lock escalation threshold, we should acquire a lock on the class
 * instead of acquiring a lock on each instance.
 */

/* composite locking for delete and update operation */
typedef struct lk_lockcomp_class LK_LOCKCOMP_CLASS;
struct lk_lockcomp_class
{
  OID class_oid;
  LK_ENTRY *class_lock_ptr;
  int num_inst_oids;
  int max_inst_oids;
  OID *inst_oid_space;
  LK_LOCKCOMP_CLASS *next;
};

typedef struct lk_lockcomp LK_LOCKCOMP;
struct lk_lockcomp
{
  int tran_index;
  int wait_msecs;
  LK_ENTRY *root_class_ptr;
  LK_LOCKCOMP_CLASS *class_list;
};

typedef struct lk_composite_lock LK_COMPOSITE_LOCK;
struct lk_composite_lock
{
  LK_LOCKCOMP lockcomp;
};

/* type of locking resource */
typedef enum
{
  LOCK_RESOURCE_INSTANCE,	/* An instance resource */
  LOCK_RESOURCE_CLASS,		/* A class resource */
  LOCK_RESOURCE_ROOT_CLASS,	/* A root class resource */
  LOCK_RESOURCE_OBJECT		/* An object resource */
} LOCK_RESOURCE_TYPE;

/*
 * Lock Resource Entry Structure
 */
typedef struct lk_res LK_RES;
struct lk_res
{
  pthread_mutex_t res_mutex;	/* resource mutex */
  LOCK_RESOURCE_TYPE type;	/* type of resource: class,instance */
  OID oid;
  OID class_oid;
  BTID btid;
  LOCK total_holders_mode;	/* total mode of the holders */
  LOCK total_waiters_mode;	/* total mode of the waiters */
  LK_ENTRY *holder;		/* lock holder list */
  LK_ENTRY *waiter;		/* lock waiter list */
  LK_ENTRY *non2pl;		/* non2pl list */
  LK_RES *hash_next;		/* for hash chain */
};

#if defined(SERVER_MODE)
extern void lock_remove_all_inst_locks (THREAD_ENTRY * thread_p,
					int tran_index, const OID * class_oid,
					LOCK lock);
#endif /* SERVER_MODE */
extern int lock_initialize (void);
extern void lock_finalize (void);
extern int lock_hold_object_instant (THREAD_ENTRY * thread_p, const OID * oid,
				     const OID * class_oid, LOCK lock);
extern int lock_object_wait_msecs (THREAD_ENTRY * thread_p, const OID * oid,
				   const OID * class_oid, LOCK lock,
				   int cond_flag, int wait_msecs);
extern int lock_object (THREAD_ENTRY * thread_p, const OID * oid,
			const OID * class_oid, LOCK lock, int cond_flag);
extern int lock_subclass (THREAD_ENTRY * thread_p, const OID * subclass_oid,
			  const OID * superclass_oid, LOCK lock,
			  int cond_flag);
extern int lock_object_with_btid (THREAD_ENTRY * thread_p, const OID * oid,
				  const OID * class_oid, const BTID * btid,
				  LOCK lock, int cond_flag);
extern int lock_object_on_iscan (THREAD_ENTRY * thread_p, const OID * oid,
				 const OID * class_oid, const BTID * btid,
				 LOCK lock, int cond_flag, int scanid_bit);
extern int lock_objects_lock_set (THREAD_ENTRY * thread_p,
				  LC_LOCKSET * lockset);
extern int lock_scan (THREAD_ENTRY * thread_p, const OID * class_oid,
		      bool is_indexscan, LOCK * current_lock,
		      int *scanid_bit);
extern int lock_classes_lock_hint (THREAD_ENTRY * thread_p,
				   LC_LOCKHINT * lockhint);
extern void lock_remove_object_lock (THREAD_ENTRY * thread_p, const OID * oid,
				     const OID * class_oid, LOCK lock);
extern void lock_unlock_object_donot_move_to_non2pl (THREAD_ENTRY * thread_p,
						     const OID * oid,
						     const OID * class_oid,
						     LOCK lock);
extern void lock_unlock_object (THREAD_ENTRY * thread_p, const OID * oid,
				const OID * class_oid, LOCK lock, int force);
extern void lock_unlock_objects_lock_set (THREAD_ENTRY * thread_p,
					  LC_LOCKSET * lockset);
extern void lock_unlock_scan (THREAD_ENTRY * thread_p, const OID * class_oid,
			      int scanid_bit, bool scan_state);
extern void lock_unlock_classes_lock_hint (THREAD_ENTRY * thread_p,
					   LC_LOCKHINT * lockhint);
extern void lock_unlock_all (THREAD_ENTRY * thread_p);
extern void lock_unlock_by_isolation_level (THREAD_ENTRY * thread_p);
extern LOCK lock_get_object_lock (const OID * oid, const OID * class_oid,
				  int tran_index);
extern bool lock_has_xlock (THREAD_ENTRY * thread_p);
#if defined (ENABLE_UNUSED_FUNCTION)
extern bool lock_has_lock_transaction (int tran_index);
#endif
extern bool lock_is_waiting_transaction (int tran_index);
extern LK_ENTRY *lock_get_class_lock (const OID * class_oid, int tran_index);
extern void lock_force_timeout_lock_wait_transactions (unsigned short
						       stop_phase);
extern bool lock_force_timeout_expired_wait_transactions (void *thrd_entry);
extern void
lock_notify_isolation_incons (THREAD_ENTRY * thread_p,
			      bool (*fun) (const OID * class_oid,
					   const OID * oid, void *args),
			      void *args);
extern bool lock_check_local_deadlock_detection (void);
extern void lock_detect_local_deadlock (THREAD_ENTRY * thread_p);
extern int lock_reacquire_crash_locks (THREAD_ENTRY * thread_p,
				       LK_ACQUIRED_LOCKS * acqlocks,
				       int tran_index);
extern void lock_unlock_all_shared_get_all_exclusive (THREAD_ENTRY * thread_p,
						      LK_ACQUIRED_LOCKS *
						      acqlocks);
extern void lock_dump_acquired (FILE * fp, LK_ACQUIRED_LOCKS * acqlocks);
extern void lock_start_instant_lock_mode (int tran_index);
extern void lock_stop_instant_lock_mode (THREAD_ENTRY * thread_p,
					 int tran_index, bool need_unlock);
extern bool lock_is_instant_lock_mode (int tran_index);
extern void lock_clear_deadlock_victim (int tran_index);
#if defined (ENABLE_UNUSED_FUNCTION)
extern void lock_check_consistency (THREAD_ENTRY * thread_p);
#endif /* ENABLE_UNUSED_FUNCTION */
extern unsigned int lock_get_number_object_locks (void);
extern int lock_initialize_composite_lock (THREAD_ENTRY * thread_p,
					   LK_COMPOSITE_LOCK * comp_lock);
extern int lock_add_composite_lock (THREAD_ENTRY * thread_p,
				    LK_COMPOSITE_LOCK * comp_lock,
				    const OID * oid, const OID * class_oid);
extern int lock_finalize_composite_lock (THREAD_ENTRY * thread_p,
					 LK_COMPOSITE_LOCK * comp_lock);
extern void lock_abort_composite_lock (LK_COMPOSITE_LOCK * comp_lock);
extern int lock_hold_object_instant_get_granted_mode (THREAD_ENTRY * thread_p,
						      const OID * oid,
						      const OID * class_oid,
						      LOCK lock,
						      LOCK * granted_mode);
extern int lock_object_with_btid_get_granted_mode (THREAD_ENTRY * thread_p,
						   const OID * oid,
						   const OID * class_oid,
						   const BTID * btid,
						   LOCK lock, int cond_flag,
						   LOCK * granted_mode);
extern int lock_btid_object_get_prev_total_hold_mode (THREAD_ENTRY * thread_p,
						      const OID * oid,
						      const OID * class_oid,
						      const BTID * btid,
						      LOCK lock,
						      int cond_flag,
						      LOCK *
						      prev_tot_hold_mode,
						      KEY_LOCK_ESCALATION *
						      key_lock_escalation);
extern LOCK lock_get_total_holders_mode (const OID * oid,
					 const OID * class_oid);
extern LOCK lock_get_all_except_transaction (const OID * oid,
					     const OID * class_oid,
					     int tran_index);
extern int lock_get_lock_holder_tran_index (THREAD_ENTRY * thread_p,
					    char **out_buf, int waiter_index,
					    LK_RES * res);
extern int lock_has_lock_on_object (const OID * oid, const OID * class_oid,
				    int tran_index, LOCK lock);
#endif /* _LOCK_MANAGER_H_ */
