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
 * mvcc_snapshot.h - Multi-Version Concurency Control system (at Server).
 *
 */
#ifndef _MVCC_SNAPSHOT_H_
#define _MVCC_SNAPSHOT_H_

#ident "$Id$"
#include "thread.h"
#include "storage_common.h"

/* MVCC Header Macros */
#define MVCC_GET_INSID(header) \
  ((header)->mvcc_ins_id)

#define MVCC_SET_INSID(header, mvcc_id) \
  ((header)->mvcc_ins_id = (mvcc_id))

#define MVCC_GET_DELID(header) \
  ((header)->delid_chn.mvcc_del_id)

#define MVCC_SET_DELID(header, mvcc_id) \
  ((header)->delid_chn.mvcc_del_id = (mvcc_id))

#define MVCC_SET_NEXT_VERSION(header, next_oid_version) \
  ((header)->next_version = *(next_oid_version))

#define MVCC_GET_NEXT_VERSION(header) \
  ((header)->next_version)

#define MVCC_GET_REPID(header) \
  ((header)->repid)

#define MVCC_SET_REPID(header, rep_id) \
  ((header)->repid = (rep_id))

#define MVCC_GET_CHN(header) \
  ((header)->delid_chn.chn)

#define MVCC_SET_CHN(header, chn_) \
  ((header)->delid_chn.chn = (chn_))

#define MVCC_GET_FLAG(header) \
  ((header)->mvcc_flag)

#define MVCC_SET_FLAG(header, flag) \
  ((header)->mvcc_flag = (flag))

#define MVCC_IS_ANY_FLAG_SET(rec_header_p) \
  (MVCC_IS_FLAG_SET (rec_header_p,  \
		     OR_MVCC_FLAG_VALID_INSID \
		     | OR_MVCC_FLAG_VALID_DELID \
		     | OR_MVCC_FLAG_VALID_NEXT_VERSION \
		     | OR_MVCC_FLAG_VALID_LONG_CHN))

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
  (MVCC_CLEAR_FLAG_BITS (rec_header_p,	\
			 OR_MVCC_FLAG_VALID_INSID \
			 | OR_MVCC_FLAG_VALID_DELID \
			 | OR_MVCC_FLAG_VALID_NEXT_VERSION \
			 | OR_MVCC_FLAG_VALID_LONG_CHN))

#define MVCC_CLEAR_FLAG_BITS(rec_header_p, flag) \
  ((rec_header_p)->mvcc_flag &= ~(flag))

/* MVCC Snapshot Macros */
#define MVCC_SNAPSHOT_GET_LOWEST_ACTIVE_ID(snapshot) \
  ((snapshot)->lowest_active_mvccid)

#define MVCC_SNAPSHOT_GET_HIGHEST_COMMITTED_ID(snapshot) \
  ((snapshot)->highest_completed_mvccid)

/* MVCC Record Macros */

/* Check if record is inserted by current transaction */
#define MVCC_IS_REC_INSERTED_BY_ME(thread_p, rec_header_p)	\
  (logtb_is_current_mvccid (thread_p, (rec_header_p)->mvcc_ins_id))

/* Check if record is deleted by current transaction */
#define MVCC_IS_REC_DELETED_BY_ME(thread_p, rec_header_p)	\
  (logtb_is_current_mvccid (thread_p, (rec_header_p)->delid_chn.mvcc_del_id))

/* Check if record was inserted by the transaction identified by mvcc_id */
#define MVCC_IS_REC_INSERTED_BY(rec_header_p, mvcc_id) \
  ((rec_header_p)->mvcc_ins_id == mvcc_id)

/* Check if record was deleted by the transaction identified by mvcc_id */
#define MVCC_IS_REC_DELETED_BY(rec_header_p, mvcc_id) \
  ((rec_header_p)->delid_chn.mvcc_del_id == mvcc_id)

/* Check if record has a valid chn. This is true when:
 *  1. MVCC is disabled for current record (and in-place update is used).
 *  2. MVCC is inserted by current transaction (checking whether it was
 *     deleted is not necessary. Other transactions cannot delete it, while
 *     current transaction would remove it completely.
 */
#define MVCC_SHOULD_TEST_CHN(thread_p, rec_header_p) \
  (!MVCC_IS_FLAG_SET (rec_header_p, OR_MVCC_FLAG_VALID_INSID | OR_MVCC_FLAG_VALID_DELID) \
    || MVCC_IS_REC_INSERTED_BY_ME (thread_p, rec_header_p))

#define MVCC_SET_SNAPSHOT_DATA(snapshot, fnc, low_act_mvccid, \
			       high_comp_mvccid, act_ids, cnt_act_ids, \
			       is_valid) \
  do \
    { \
      (snapshot)->snapshot_fnc = fnc; \
      (snapshot)->lowest_active_mvccid = low_act_mvccid; \
      (snapshot)->highest_completed_mvccid = high_comp_mvccid; \
      (snapshot)->active_ids = act_ids; \
      (snapshot)->cnt_active_ids = cnt_act_ids; \
      (snapshot)->valid = is_valid; \
    } \
  while (0)

/* clear MVCC snapshot data - do not free active_ids since they are reused */
#define MVCC_CLEAR_SNAPSHOT_DATA(snapshot) \
  do \
    { \
      (snapshot)->snapshot_fnc = NULL; \
      (snapshot)->lowest_active_mvccid = MVCCID_NULL; \
      (snapshot)->highest_completed_mvccid = MVCCID_NULL; \
      (snapshot)->cnt_active_ids = 0; \
      (snapshot)->valid = false; \
    } \
  while (0)

typedef struct mvcc_snapshot MVCC_SNAPSHOT;

typedef bool (*MVCC_SNAPSHOT_FUNC) (THREAD_ENTRY * thread_p,
				    MVCC_REC_HEADER * rec_header,
				    MVCC_SNAPSHOT * snapshot);
struct mvcc_snapshot
{
  MVCC_SNAPSHOT_FUNC snapshot_fnc;

  MVCCID lowest_active_mvccid;	/* lowest active id */

  MVCCID highest_completed_mvccid;	/* highest committed id */

  MVCCID *active_ids;		/* active ids */

  unsigned int cnt_active_ids;	/* count active ids */

  bool valid;
};

/* MVCC INFO - such structure is attached to each active transaction */
typedef struct mvcc_info MVCC_INFO;
struct mvcc_info
{
  MVCC_SNAPSHOT mvcc_snapshot;	/* MVCC Snapshot */
  MVCCID mvcc_id;		/* MVCC ID - increase with each transaction
				 * that modified data
				 */
  /* transaction_lowest_active_mvccid - the lowest active mvcc id when we
   * start the current transaction
   */
  MVCCID transaction_lowest_active_mvccid;

  /* recent_snapshot_lowest_active_mvccid - the lowest active mvcc id computed
   * for the most recent snapshot of current transaction. This field help to
   * know faster whether an mvcc id is active or not. Thus, mvccid older than
   * this field are not active anymore
   */
  MVCCID recent_snapshot_lowest_active_mvccid;
  MVCC_INFO *next, *prev;	/* link to the next/previous active mvcc info */

  MVCCID *mvcc_sub_ids;		/* MVCC sub-transaction ID array */
  int max_sub_ids;		/* allocated MVCC sub-transaction ids */
  int count_sub_ids;		/* count sub-transaction ids */
  bool is_sub_active;		/* true in case that sub-transaction is running */
};

/* MVCC INFO BLOCK Structure */
typedef struct mvcc_info_block MVCC_INFO_BLOCK;
struct mvcc_info_block
{
  MVCC_INFO *block;		/* mvcc info block - each block contains
				 * NUM_TOTAL_TRAN_INDICES entries
				 */
  MVCC_INFO_BLOCK *next_block;	/* next mvcc info block */
};

typedef enum mvcc_satisfies_delete_result MVCC_SATISFIES_DELETE_RESULT;
enum mvcc_satisfies_delete_result
{
  DELETE_RECORD_INVISIBLE,	/* invisible - created after scan started */
  DELETE_RECORD_CAN_DELETE,	/* is visible and valid - can be deleted */
  DELETE_RECORD_DELETED,	/* deleted by the current transaction */
  DELETE_RECORD_IN_PROGRESS,	/* deleted by other in progress transaction */
  DELETE_RECORD_SELF_DELETED	/* deleted by the current transaction */
};				/* Heap record satisfies delete result */

typedef enum mvcc_satisfies_vacuum_result MVCC_SATISFIES_VACUUM_RESULT;
enum mvcc_satisfies_vacuum_result
{
  VACUUM_RECORD_REMOVE,		/* record can be removed completely */
  VACUUM_RECORD_DELETE_INSID,	/* record insert MVCCID can be removed */
  VACUUM_RECORD_CANNOT_VACUUM	/* record cannot be vacuumed because:
				 * 1. it was already vacuumed.
				 * 2. it was recently inserted.
				 * 3. it was recently deleted and has no
				 *    insert MVCCID.
				 */
};				/* Heap record satisfies vacuum result */

extern bool mvcc_satisfies_snapshot (THREAD_ENTRY * thread_p,
				     MVCC_REC_HEADER * rec_header,
				     MVCC_SNAPSHOT * snapshot);
extern MVCC_SATISFIES_VACUUM_RESULT mvcc_satisfies_vacuum (THREAD_ENTRY *
							   thread_p,
							   MVCC_REC_HEADER *
							   rec_header,
							   MVCCID
							   oldest_mvccid);
extern MVCC_SATISFIES_DELETE_RESULT mvcc_satisfies_delete (THREAD_ENTRY *
							   thread_p,
							   MVCC_REC_HEADER *
							   rec_header);

extern bool mvcc_satisfies_dirty (THREAD_ENTRY * thread_p,
				  MVCC_REC_HEADER * rec_header,
				  MVCC_SNAPSHOT * snapshot);
extern bool mvcc_id_precedes (MVCCID id1, MVCCID id2);
extern bool mvcc_id_follow_or_equal (MVCCID id1, MVCCID id2);
#endif /* _MVCC_SNAPSHOT_H_ */
