/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * 	Overview: LOCK MANAGMENT MODULE (AT THE SERVER) -- Interface --	       
 * See .c file for overview and description of the interface functions.	       
 * 									       
 */

#ifndef _LK_H_
#define _LK_H_

#ident "$Id$"

#include "config.h"

#include <time.h>
#include <stdio.h>

#include "error_manager.h"
#include "oid.h"
#include "common.h"
#include "locator_bt.h"

#if defined(SERVER_MODE)
#include "csserror.h"
#endif /* SERVER_MODE */
#include "thread_impl.h"

#define LK_GRANTED                           1
#define LK_NOTGRANTED                        2
#define LK_NOTGRANTED_DUE_ABORTED            3
#define LK_NOTGRANTED_DUE_TIMEOUT            4
#define LK_NOTGRANTED_DUE_ERROR              5
#define LK_GRANTED_PUSHINSET_LOCKONE         6
#define LK_GRANTED_PUSHINSET_RELOCKALL       7

#define LK_INFINITE_WAIT   (-1)	/* Value to wait forever                   */
#define LK_FORCE_ZERO_WAIT (-2)	/* Value to force a timeout without setting 
				 * errors */
#define LK_ZERO_WAIT       0	/* Value to timeout immediately.. not wait */


enum
{ LK_UNCOND_LOCK, LK_COND_LOCK };

typedef struct lk_acquisition_history LK_ACQUISITION_HISTORY;
struct lk_acquisition_history
{
  LOCK req_mode;
  struct lk_acquisition_history *next;
  struct lk_acquisition_history *prev;
};

/*****************************/
/* Lock Heap Entry Structure */
/*****************************/
typedef struct lk_entry LK_ENTRY;
struct lk_entry
{
#if defined(SERVER_MODE)
  struct lk_res *res_head;	/* back to resource entry           */
  int tran_index;		/* transaction table index          */
  THREAD_ENTRY *thrd_entry;	/* thread entry pointer             */
  LOCK granted_mode;		/* granted lock mode                */
  LOCK blocked_mode;		/* blocked lock mode                */
  int count;			/* number of lock requests          */
  int ngranules;		/* number of finer granules         */
  struct lk_entry *next;	/* next entry                       */
  struct lk_entry *tran_next;	/* list of locks that trans. holds  */
  struct lk_entry *class_entry;	/* ptr. to class lk_entry           */
  LK_ACQUISITION_HISTORY *history;	/* lock acquisition history         */
  LK_ACQUISITION_HISTORY *recent;	/* last node of history list        */
  int mlk_count;		/* number of instant lock requests  */
  unsigned char scanid_bitset[1];	/* PRM_LK_MAX_SCANID_BIT/8];       */
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
  unsigned int nobj_locks;	/* Number of actual object locks     */
  LK_ACQOBJ_LOCK *obj;		/* The list of acquired object locks */
};

/* During delete and update operation,
 * if the number of objects to be deleted or updated is larger than
 * lock escalation threshold, we should acquire a lock on the class
 * instead of acquiring a lock on each instance.
 */
typedef struct lk_composite_lock LK_COMPOSITE_LOCK;
struct lk_composite_lock
{
  void *lockcomp;
};

#if defined(SERVER_MODE)
/* in xserver.h */
extern void xlock_dump (THREAD_ENTRY * thread_p, FILE * outfp);
#endif /* SERVER_MODE */

#if defined(SERVER_MODE)
extern void lock_remove_all_inst_locks (THREAD_ENTRY * thread_p,
					int tran_index, const OID * class_oid,
					LOCK lock);
#endif /* SERVER_MODE */
extern int lock_initialize (void);
extern void lock_finalize (void);
extern int lock_hold_object_instant (THREAD_ENTRY * thread_p, const OID * oid,
				     const OID * class_oid, LOCK lock);
extern int lock_object (THREAD_ENTRY * thread_p, const OID * oid,
			const OID * class_oid, LOCK lock, int cond_flag);
extern int lock_object_on_iscan (THREAD_ENTRY * thread_p, const OID * oid,
				 const OID * class_oid, LOCK lock,
				 int cond_flag, int scanid_bit);
extern int lock_objects_lock_set (THREAD_ENTRY * thread_p,
				  LC_LOCKSET * lockset);
extern int lock_scan (THREAD_ENTRY * thread_p, const OID * class_oid,
		      bool is_indexscan, int lock_hint, LOCK * current_lock,
		      int *scanid_bit);
extern int lock_classes_lock_hint (THREAD_ENTRY * thread_p,
				   LC_LOCKHINT * lockhint);
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
extern void lock_demote_all_update_inst_locks (THREAD_ENTRY * thread_p);
extern LOCK lock_get_object_lock (const OID * oid, const OID * class_oid,
				  int tran_index);
extern bool lock_has_xlock (THREAD_ENTRY * thread_p);
extern bool lock_has_lock_transaction (int tran_index);
extern bool lock_is_waiting_transaction (int tran_index);
extern LK_ENTRY *lock_get_class_lock (const OID * class_oid, int tran_index);
extern void lock_force_timeout_lock_wait_transactions (void);
extern bool lock_force_timeout_expired_wait_transactions (void *thrd_entry);
extern void
lock_notify_isolation_incons (THREAD_ENTRY * thread_p,
			      bool (*fun) (const OID * oid, void *args),
			      void *args);
extern bool lock_check_local_deadlock_detection (void);
extern void lock_detect_local_deadlock (THREAD_ENTRY * thread_p);
extern int lock_reacquire_crash_locks (THREAD_ENTRY * thread_p,
				       LK_ACQUIRED_LOCKS * acqlocks,
				       int tran_index);
extern void lock_unlock_all_shared_get_all_exclusive (THREAD_ENTRY * thread_p,
						      LK_ACQUIRED_LOCKS *
						      acqlocks);
extern void lock_dump_acquired (LK_ACQUIRED_LOCKS * acqlocks);
extern void lock_start_instant_lock_mode (int tran_index);
extern void lock_stop_instant_lock_mode (THREAD_ENTRY * thread_p,
					 int tran_index, bool need_unlock);
extern bool lock_is_instant_lock_mode (int tran_index);
extern void lock_check_consistency (THREAD_ENTRY * thread_p);
extern unsigned int lock_get_number_object_locks (void);
extern int lock_initialize_composite_lock (THREAD_ENTRY * thread_p,
					   LK_COMPOSITE_LOCK * comp_lock);
extern int lock_add_composite_lock (THREAD_ENTRY * thread_p,
				    LK_COMPOSITE_LOCK * comp_lock,
				    const OID * oid, const OID * class_oid);
extern int lock_finalize_composite_lock (THREAD_ENTRY * thread_p,
					 LK_COMPOSITE_LOCK * comp_lock);
extern void lock_abort_composite_lock (LK_COMPOSITE_LOCK * comp_lock);

#endif /* _LK_H_ */
