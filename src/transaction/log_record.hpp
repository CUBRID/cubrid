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

//
// log_record - define log record structures
//

#ifndef _LOG_RECORD_HPP_
#define _LOG_RECORD_HPP_

// todo - this should not be exposed to client after HA refactoring
// todo - add to a proper namespace

#include "client_credentials.hpp"
#include "log_lsa.hpp"
#include "recovery.h"
#include "storage_common.h"
#include "system.h"

#include <cstring>

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
  LOG_COMMIT_WITH_POSTPONE = 14,		/* Committing server postpone operations */
  LOG_COMMIT_WITH_POSTPONE_OBSOLETE = 15,	/* Obsolete. It was LOG_COMMIT_WITH_POSTPONE without the donetime. It remains only for the backward compatibility and will be removed with the next major release, maybe 12.0 */
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
                                 * - LOG_SYSOP_END_LOGICAL_MVCC_UNDO: TODO:
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
#if 0
  /* Previous checkpoint system used to have two log records: one for the start and one for the end. The end log
   * record also included a snapshot of the transaction table. The checkpoint info is no longer saved in log records.
   */
  LOG_START_CHKPT = 25,		/* Start a checkpoint */
  LOG_END_CHKPT = 26,		/* Checkpoint information */
#endif
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
  LOG_SYSOP_ATOMIC_START = 50,	/* Log marker to start atomic operations that need to be rollbacked immediately after
                 * redo phase of recovery and before finishing postpones */
  LOG_START_ATOMIC_REPL = 51,
  LOG_END_ATOMIC_REPL = 52,
  LOG_TRANTABLE_SNAPSHOT = 53,
  LOG_ASSIGNED_MVCCID = 54,	/* There are transactions that assign an mvccid but do not also record it in a log record.
                                   Because the log records are the only communication means from active transaction server
                                   towards passive transaction servers, it is needed to relay such mvccid for completion
                                   on the passive transaction server as well. */
  LOG_DUMMY_GENERIC,		/* used for flush for now. it is ridiculous to create dummy log records for every single
                                 * case. we should find a different approach */

  LOG_SUPPLEMENTAL_INFO,        /* used for supplemental logs to support CDC interface.
                                 * it contains transaction user info, DDL statement, undo lsa, redo lsa for DML,
                                 * or undo images that never retrieved from the log. */

  LOG_REPL_DDL_LOCK_INFO,       /* Log when the lock is acquired for DDL operation, and this is for PTS replicator
                                 * to know when to acquire the lock, and which object to be locked.
                                 * PTS needs to block the read transactions which try to access the same object
                                 * being modified by the replicator.
                                 * (lock is required for the record in root class and db_serial, which are not mvcc classes)
                                 */

  /* NOTE: add actual (persistent) new values before this */
  LOG_DUMMY_UNIT_TESTING,	/* exclusively for unit testing; not to be persisted;
                                 * constant value does not need be preserved */

  LOG_LARGER_LOGREC_TYPE	/* A higher bound for checks */
};
typedef enum log_rectype LOG_RECTYPE;

/* Description of a log record */
typedef struct log_rec_header LOG_RECORD_HEADER;
struct log_rec_header
{
  inline bool operator== (const log_rec_header &other) const;

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

/* replication log structure */
typedef struct log_rec_replication LOG_REC_REPLICATION;
struct log_rec_replication
{
  LOG_LSA lsa;
  int length;
  int rcvindex;
};

/* Redefined for explicitness.
 * Not using 'time_t' (which is unsigned) for backward compatibility of stored structure. */
typedef INT64 time_msec_t;

/* Log the time of termination of transaction */
typedef struct log_rec_donetime LOG_REC_DONETIME;
struct log_rec_donetime
{
  time_msec_t at_time;		/* Transaction commit time stored as milliseconds */
};

/* Log the change of the server's HA state */
typedef struct log_rec_ha_server_state LOG_REC_HA_SERVER_STATE;
struct log_rec_ha_server_state
{
  int state;			/* ha_Server_state */
  int dummy;			/* dummy for alignment */

  time_msec_t at_time;		/* Time recorded by active server stored as milliseconds */
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
  INT64 at_time;		/* donetime. For the time-specific recovery */
};

/* This entry is included during commit. Obsolete. See the comment of LOG_COMMIT_WITH_POSTPONE_OBSOLETE. */
typedef struct log_rec_start_postpone_obsolete LOG_REC_START_POSTPONE_OBSOLETE;
struct log_rec_start_postpone_obsolete
{
  LOG_LSA posp_lsa;
};

/* types of end system operation */
enum log_sysop_end_type
{
  LOG_SYSOP_END_COMMIT,		/* permanent changes */
  LOG_SYSOP_END_ABORT,		/* aborted system op */
  LOG_SYSOP_END_LOGICAL_UNDO,	/* logical undo */
  LOG_SYSOP_END_LOGICAL_MVCC_UNDO,	/* logical mvcc undo */
  LOG_SYSOP_END_LOGICAL_COMPENSATE,	/* logical compensate */
  LOG_SYSOP_END_LOGICAL_RUN_POSTPONE	/* logical run postpone */
};
typedef enum log_sysop_end_type LOG_SYSOP_END_TYPE;
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
  /* File where the page belong. same as mvcc_undo->vacuum_info if type == LOG_SYSOP_END_LOGICAL_MVCC_UNDO. It is used to get TDE information.*/
  const VFID *vfid;
  union				/* other info based on type */
  {
    LOG_REC_UNDO undo;		/* undo data for logical undo */
    struct
    {
      LOG_REC_MVCC_UNDO mvcc_undo;	/* undo data for logical undo of MVCC operation */
      MVCCID parent_mvccid;	/* If transaction has an mvccid allocated by a sub-transaction, this field will
				 * contain the transaction's "main" mvccid (which, as indicated in implementations
				 * in logtb_get_new_subtransaction_mvccid and logtb_get_current_mvccid, must
				 * be valid) while the mvcc_undo.mvccid will contain the mvccid of the subtransaction.
				 * Otherwise, null.
				 * The purpose of this field is twofold across transactional log replication boundary
				 * (currently only on passive transaction server):
				 *  - to discerne the nature of the mvccid as either 'main' mvccid or sub-mvccid
				 *  - and to allow proper completion of either[both] mvccid or[and] sub-mvccid */
    } mvcc_undo_info;
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

typedef struct log_rec_trantable_snapshot LOG_REC_TRANTABLE_SNAPSHOT;
struct log_rec_trantable_snapshot
{
  LOG_LSA snapshot_lsa = NULL_LSA;       // the LSA when transaction table snapshot is taken
  size_t length = 0;                    // packed snapshot recovery data length
};

typedef struct log_rec_assigned_mvccid LOG_REC_ASSIGNED_MVCCID;
struct log_rec_assigned_mvccid
{
  MVCCID mvccid;
};

typedef enum supplement_rec_type
{
  LOG_SUPPLEMENT_TRAN_USER,
  LOG_SUPPLEMENT_UNDO_RECORD, /*Contains undo raw record that can not be retrieved from the logs */
  LOG_SUPPLEMENT_DDL,
  /* Contains lsa of logs which contain undo, redo raw record (UPDATE, DELETE, INSERT)
   * | LOG_REC_HEADER | SUPPLEMENT_REC_TYPE | LENGTH | CLASS OID |  UNDO LSA (sizeof LOG_LSA) | REDO LSA | */
  LOG_SUPPLEMENT_INSERT,
  LOG_SUPPLEMENT_UPDATE,
  LOG_SUPPLEMENT_DELETE,
  LOG_SUPPLEMENT_TRIGGER_INSERT, /* INSERT, UPDATE, DELETE logs appended by a trigger action */
  LOG_SUPPLEMENT_TRIGGER_UPDATE,
  LOG_SUPPLEMENT_TRIGGER_DELETE,
  LOG_SUPPLEMENT_LARGER_REC_TYPE,
} SUPPLEMENT_REC_TYPE;

typedef struct log_rec_supplement LOG_REC_SUPPLEMENT;
struct log_rec_supplement
{
  SUPPLEMENT_REC_TYPE rec_type;
  int length;
};

typedef struct log_rec_repl_ddl_lock_info LOG_REC_REPL_DDL_LOCK_INFO;
struct log_rec_repl_ddl_lock_info
{
  OID oid;
  OID classoid;
  LOCK lock_mode;

  // *INDENT-OFF*
  log_rec_repl_ddl_lock_info () {};
  log_rec_repl_ddl_lock_info (OID oid, OID classoid, LOCK lock_mode)
    : oid (oid), classoid (classoid), lock_mode (lock_mode) {};
  log_rec_repl_ddl_lock_info (const log_rec_repl_ddl_lock_info &other)
    : oid (other.oid), classoid (other.classoid), lock_mode (other.lock_mode) {};
  log_rec_repl_ddl_lock_info (log_rec_repl_ddl_lock_info &&other) = delete;
  // *INDENT-ON*
};

#define LOG_GET_LOG_RECORD_HEADER(log_page_p, lsa) \
  ((LOG_RECORD_HEADER *) ((log_page_p)->area + (lsa)->offset))

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

/* Is record type used a MVCC operation */
#define LOG_IS_MVCC_OP_RECORD_TYPE(type) \
  (((type) == LOG_MVCC_UNDO_DATA) \
   || ((type) == LOG_MVCC_REDO_DATA) \
   || ((type) == LOG_MVCC_UNDOREDO_DATA) \
   || ((type) == LOG_MVCC_DIFF_UNDOREDO_DATA))

//
// inline/template implementations
//

inline bool
log_rec_header::operator== (const log_rec_header &other) const
{
  return prev_tranlsa == other.prev_tranlsa && back_lsa == other.back_lsa
	 && forw_lsa == other.forw_lsa && trid == other.trid && type == other.type;
}

#endif // _LOG_RECORD_HPP_
