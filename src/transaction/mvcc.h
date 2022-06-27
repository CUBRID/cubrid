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
 * mvcc.h - Multi-Version Concurrency Control system (at Server).
 *
 */
#ifndef _MVCC_H_
#define _MVCC_H_

#ident "$Id$"

#include "log_lsa.hpp"
#include "mvcc_active_tran.hpp"
#include "recovery.h"
#include "storage_common.h"
#include "thread_compat.hpp"

#include <vector>

/* MVCC RECORD HEADER */
typedef struct mvcc_rec_header MVCC_REC_HEADER;
struct mvcc_rec_header
{
  INT32 mvcc_flag:8;		/* MVCC flags */
  INT32 repid:24;		/* representation id */
  int chn;			/* cache coherency number */
  MVCCID mvcc_ins_id;		/* MVCC insert id */
  MVCCID mvcc_del_id;		/* MVCC delete id */
  LOG_LSA prev_version_lsa;	/* log address of previous version */
};
#define MVCC_REC_HEADER_INITIALIZER \
{ 0, 0, NULL_CHN, MVCCID_NULL, MVCCID_NULL, LSA_INITIALIZER }

/* MVCC Header Macros */
#define MVCC_GET_INSID(header) \
  ((header)->mvcc_ins_id)

#define MVCC_SET_INSID(header, mvcc_id) \
  ((header)->mvcc_ins_id = (mvcc_id))

#define MVCC_GET_DELID(header) \
  ((header)->mvcc_del_id)

#define MVCC_SET_DELID(header, mvcc_id) \
  ((header)->mvcc_del_id = (mvcc_id))

#define MVCC_GET_REPID(header) \
  ((header)->repid)

#define MVCC_SET_REPID(header, rep_id) \
  ((header)->repid = (rep_id))

#define MVCC_GET_CHN(header) \
  ((header)->chn)

#define MVCC_SET_CHN(header, chn_) \
  ((header)->chn = (chn_))

#define MVCC_GET_FLAG(header) \
  ((header)->mvcc_flag)

#define MVCC_SET_FLAG(header, flag) \
  ((header)->mvcc_flag = (flag))

#define MVCC_IS_ANY_FLAG_SET(rec_header_p) \
  (MVCC_IS_FLAG_SET (rec_header_p, OR_MVCC_FLAG_MASK))

#define MVCC_IS_FLAG_SET(rec_header_p, flags) \
  ((rec_header_p)->mvcc_flag & (flags))

#define MVCC_IS_HEADER_DELID_VALID(rec_header_p) \
  (MVCC_IS_FLAG_SET (rec_header_p, OR_MVCC_FLAG_VALID_DELID) \
   && MVCCID_IS_VALID (MVCC_GET_DELID (rec_header_p)))

#define MVCC_IS_HEADER_INSID_NOT_ALL_VISIBLE(rec_header_p) \
  (MVCC_IS_FLAG_SET (rec_header_p, OR_MVCC_FLAG_VALID_INSID) \
   && MVCC_GET_INSID (rec_header_p) != MVCCID_ALL_VISIBLE)

#define MVCC_SET_FLAG_BITS(rec_header_p, flag) \
  ((rec_header_p)->mvcc_flag |= (flag))

#define MVCC_CLEAR_ALL_FLAG_BITS(rec_header_p) \
  (MVCC_CLEAR_FLAG_BITS (rec_header_p, OR_MVCC_FLAG_MASK))

#define MVCC_CLEAR_FLAG_BITS(rec_header_p, flag) \
  ((rec_header_p)->mvcc_flag &= ~(flag))

/* MVCC Snapshot Macros */
#define MVCC_SNAPSHOT_GET_LOWEST_ACTIVE_ID(snapshot) \
  ((snapshot)->lowest_active_mvccid)

#define MVCC_SNAPSHOT_GET_HIGHEST_COMMITTED_ID(snapshot) \
  ((snapshot)->highest_completed_mvccid)

/* MVCC Record Macros */

/* Check if record is inserted by current transaction or its children */
#define MVCC_IS_REC_INSERTED_BY_ME(thread_p, rec_header_p)	\
  (logtb_is_current_mvccid (thread_p, (rec_header_p)->mvcc_ins_id))

/* Check if record is deleted by current transaction or its children */
#define MVCC_IS_REC_DELETED_BY_ME(thread_p, rec_header_p)	\
  (logtb_is_current_mvccid (thread_p, (rec_header_p)->mvcc_del_id))

/* Check if record was inserted by the transaction identified by mvcc_id */
#define MVCC_IS_REC_INSERTED_BY(rec_header_p, mvcc_id) \
  ((rec_header_p)->mvcc_ins_id == mvcc_id)

/* Check if record was deleted by the transaction identified by mvcc_id */
#define MVCC_IS_REC_DELETED_BY(rec_header_p, mvcc_id) \
  ((rec_header_p)->delid_chn.mvcc_del_id == mvcc_id)

/* Check if given CHN is up-to-date according to MVCC header:
 * 1. Given CHN must be non-NULL.
 * 2. header CHN matches given CHN.
 */
#define MVCC_IS_CHN_UPTODATE(rec_header_p, chn) \
  (chn != NULL_CHN \
   && (chn == MVCC_GET_CHN (rec_header_p)))

#define MVCC_ID_PRECEDES(id1, id2) ((id1) < (id2))
#define MVCC_ID_FOLLOW_OR_EQUAL(id1, id2) ((id1) >= (id2))

#define MVCC_IS_HEADER_PREV_VERSION_VALID(rec_header_p) \
  (MVCC_IS_FLAG_SET (rec_header_p, OR_MVCC_FLAG_VALID_PREV_VERSION) \
  && !LSA_ISNULL (&MVCC_GET_PREV_VERSION_LSA (rec_header_p)))

#define MVCC_SET_PREVIOUS_VERSION_LSA(header, new_lsa) \
  do \
    { \
      (header)->prev_version_lsa.pageid = (new_lsa)->pageid; \
      (header)->prev_version_lsa.offset = (new_lsa)->offset; \
    } \
  while (0)

#define MVCC_GET_PREV_VERSION_LSA(header) \
  ((header)->prev_version_lsa)

enum mvcc_satisfies_snapshot_result
{
  TOO_OLD_FOR_SNAPSHOT,		/* not visible, deleted by me or deleted by inactive transaction */
  SNAPSHOT_SATISFIED,		/* is visible and valid */
  TOO_NEW_FOR_SNAPSHOT		/* not visible, inserter is still active.
				 * when looking for visible version, if this is the snapshot result, we have to
				 * check previous versions in log (if there are previous versions).
				 */
};				/* Possible results by check versions against snapshots. */
typedef enum mvcc_satisfies_snapshot_result MVCC_SATISFIES_SNAPSHOT_RESULT;
typedef struct mvcc_snapshot MVCC_SNAPSHOT;

typedef MVCC_SATISFIES_SNAPSHOT_RESULT (*MVCC_SNAPSHOT_FUNC) (THREAD_ENTRY * thread_p, MVCC_REC_HEADER * rec_header,
							      MVCC_SNAPSHOT * snapshot);
struct mvcc_snapshot
{
  MVCCID lowest_active_mvccid;	/* lowest active id */
  MVCCID highest_completed_mvccid;	/* highest mvccid in snapshot */

  mvcc_active_tran m_active_mvccs;

  MVCC_SNAPSHOT_FUNC snapshot_fnc;	/* the snapshot function */

  bool valid;			/* true, if the snapshot is valid */

  // *INDENT-OFF*
  mvcc_snapshot ();
  void reset ();

  mvcc_snapshot &operator= (const mvcc_snapshot& snapshot) = delete;

  void copy_to (mvcc_snapshot & other) const;
  // *INDENT-ON*
};

/* MVCC INFO - such structure is attached to each active transaction */
typedef struct mvcc_info MVCC_INFO;
struct mvcc_info
{
  MVCC_SNAPSHOT snapshot;	/* MVCC Snapshot */

  /* MVCC ID - increase with each transaction that modified data */
  MVCCID id;

  /* recent_snapshot_lowest_active_mvccid - the lowest active MVCCID computed for the most recent snapshot of current
   * transaction. This field help to know faster whether an MVCCID is active or not. Thus, mvccid older than this field
   * are not active anymore */
  MVCCID recent_snapshot_lowest_active_mvccid;

  // *INDENT-OFF*
  std::vector<MVCCID> sub_ids;		/* MVCC sub-transaction ID array. Even if the implementation supports more than
					 * one transaction mvcc sub-id, in practice the scenario is never encountered
					 * and asserts are present everywhere in this regard.
					 * NOTE: the same structure & logic is implemented for scalability passive
					 * transaction server replication with the class cublog::replicator_mvcc */
  // *INDENT-ON*
  bool is_sub_active;		/* true in case that sub-transaction is running */

  // *INDENT-OFF*
  mvcc_info ();
  void init ();
  void reset ();
  // *INDENT-ON*
};

enum mvcc_satisfies_delete_result
{
  DELETE_RECORD_INSERT_IN_PROGRESS,	/* invisible - created after scan started */
  DELETE_RECORD_CAN_DELETE,	/* is visible and valid - can be deleted */
  DELETE_RECORD_DELETED,	/* deleted by committed transaction */
  DELETE_RECORD_DELETE_IN_PROGRESS,	/* deleted by other in progress transaction */
  DELETE_RECORD_SELF_DELETED	/* deleted by the current transaction */
};				/* Heap record satisfies delete result */
typedef enum mvcc_satisfies_delete_result MVCC_SATISFIES_DELETE_RESULT;

enum mvcc_satisfies_vacuum_result
{
  VACUUM_RECORD_REMOVE,		/* record can be removed completely */
  VACUUM_RECORD_DELETE_INSID_PREV_VER,	/* record insert MVCCID and prev version lsa can be removed */
  VACUUM_RECORD_CANNOT_VACUUM	/* record cannot be vacuumed because: 1. it was already vacuumed. 2. it was recently
				 * inserted. 3. it was recently deleted and has no insert MVCCID. */
};				/* Heap record satisfies vacuum result */
typedef enum mvcc_satisfies_vacuum_result MVCC_SATISFIES_VACUUM_RESULT;

/* Definitions used to identify MVCC log records. */

/* Is log record for a heap MVCC operation */
inline bool
LOG_IS_MVCC_HEAP_OPERATION (LOG_RCVINDEX rcvindex)
{
  // *INDENT-OFF*
  return rcvindex == RVHF_MVCC_DELETE_REC_HOME
    || rcvindex == RVHF_MVCC_INSERT
    || rcvindex == RVHF_UPDATE_NOTIFY_VACUUM
    || rcvindex == RVHF_MVCC_DELETE_MODIFY_HOME
    || rcvindex == RVHF_MVCC_NO_MODIFY_HOME
    || rcvindex == RVHF_MVCC_REDISTRIBUTE;
  // *INDENT-ON*
}

/* Is log record for a b-tree MVCC operation */
inline bool
LOG_IS_MVCC_BTREE_OPERATION (LOG_RCVINDEX rcvindex)
{
  // *INDENT-OFF*
  return rcvindex == RVBT_MVCC_DELETE_OBJECT
    || rcvindex == RVBT_MVCC_INSERT_OBJECT
    || rcvindex == RVBT_MVCC_INSERT_OBJECT_UNQ
    || rcvindex == RVBT_MVCC_NOTIFY_VACUUM
    || rcvindex == RVBT_MARK_DELETED;
  // *INDENT-ON*
}

/* Is log record for a MVCC operation */
inline bool
LOG_IS_MVCC_OPERATION (LOG_RCVINDEX rcvindex)
{
  // *INDENT-OFF*
  return LOG_IS_MVCC_HEAP_OPERATION (rcvindex)
    || LOG_IS_MVCC_BTREE_OPERATION (rcvindex)
    || rcvindex == RVES_NOTIFY_VACUUM;
  // *INDENT-ON*
}

extern MVCC_SATISFIES_SNAPSHOT_RESULT mvcc_satisfies_snapshot (THREAD_ENTRY * thread_p, MVCC_REC_HEADER * rec_header,
							       MVCC_SNAPSHOT * snapshot);
extern MVCC_SATISFIES_SNAPSHOT_RESULT mvcc_is_not_deleted_for_snapshot (THREAD_ENTRY * thread_p,
									MVCC_REC_HEADER * rec_header,
									MVCC_SNAPSHOT * snapshot);
extern MVCC_SATISFIES_VACUUM_RESULT mvcc_satisfies_vacuum (THREAD_ENTRY * thread_p, const MVCC_REC_HEADER * rec_header,
							   MVCCID oldest_mvccid);
extern MVCC_SATISFIES_DELETE_RESULT mvcc_satisfies_delete (THREAD_ENTRY * thread_p, MVCC_REC_HEADER * rec_header);

extern MVCC_SATISFIES_SNAPSHOT_RESULT mvcc_satisfies_dirty (THREAD_ENTRY * thread_p, MVCC_REC_HEADER * rec_header,
							    MVCC_SNAPSHOT * snapshot);
extern bool mvcc_is_mvcc_disabled_class (const OID * class_oid);

#endif /* _MVCC_H_ */
