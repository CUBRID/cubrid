/*
 *
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
 * query_rewrite.h - replace a query received from the driver with a
 *                       predefined one at prepare time.
 *
 *   The broker builds a shared memory segment from the rule files under the
 *   QUERY_REWRITE_RULE directory at start/ON time, and `cubrid broker qr`
 *   mutates it in place afterwards.  Each CAS attaches read-only (SHM_RDONLY)
 *   for lookup.  All pointers inside the segment are stored as byte offsets so
 *   that the segment can be mapped at any address.
 */

#ifndef _QUERY_REWRITE_H_
#define _QUERY_REWRITE_H_

#ident "$Id$"

#include <stdio.h>

#include "broker_shm.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define QR_SHM_MAGIC		0x51525752	/* "QRWR" */

#define QR_NAME_LEN		32	/* db name / user name buffer */
#define QR_MAX_BINDS		256	/* max markers in a single query */
#define QR_MAX_QUERY_LEN	32768	/* hard ceiling for QUERY_REWRITE_MAX_QUERY_LEN */
#define QR_DEFAULT_HASH_SIZE	1024
#define QR_MAX_RULE_COUNT	200	/* hard ceiling for QUERY_REWRITE_MAX_RULES */
#define QR_RELPATH_LEN		520	/* "user@dbname/file" pool slot for the source path */
#define QR_SHMODE		0600	/* owner-only: the segment holds the rule files' SQL verbatim */
#define QR_RULE_SUFFIX		".rule"	/* only files with this suffix are loaded as rules */

#define QR_FAIL_STRIKE_MAX	2	/* consecutive AMBIGUOUS execute failures per rule -> demote (N-strike) */

#define QR_ORIGIN_STARTUP	0	/* loaded by the startup directory scan */
#define QR_ORIGIN_ADDED		1	/* added/reloaded at runtime (cubrid broker qr) */

/* one rewrite rule stored in the shared memory segment.
 * query strings are kept in the trailing string pool and referenced by offset.
 * marker counts (K_orig / K_rewrite) are NOT stored here: the broker has no SQL
 * parser, so they are computed once at CAS prepare time via get_num_markers()
 * and cached process-locally (see query_rewrite.c). */
  typedef struct t_qr_rule T_QR_RULE;
  struct t_qr_rule
  {
    char db_name[QR_NAME_LEN];	/* upper-cased target db name   */
    char user_name[QR_NAME_LEN];	/* upper-cased target user name */
    int orig_off;		/* offset of normalized original query in string pool */
    int rewrite_off;		/* offset of replacement query in string pool */
    int name_off;		/* offset of source file relative path "user@dbname/file" */
    int num_map_entries;	/* number of BIND_MAP entries, -1 = omitted */
    int next_idx;		/* next rule index in hash chain, -1 = end    */
    unsigned int hash;		/* combined bucket key: orig_hash ^ hash(db) ^ hash(user);
				 * lets qr_lookup reject chain misses with one int compare */
    int orig_len;		/* strlen of the normalized original query; enables the length
				 * prefilter and the length-bounded memcmp verify in qr_lookup */
    int disabled;		/* shared admin disable flag (cubrid broker qr disable) */
    int origin;			/* QR_ORIGIN_STARTUP / QR_ORIGIN_ADDED */
    int admin_seq;		/* bumped by `qr enable`; CAS compares to invalidate its process-local failure-disable */
    /* src_orig_pos[rewrite_pos-1] = original marker pos; unused when num_map_entries == -1 */
    short src_orig_pos[QR_MAX_BINDS];
  };

/* one distinct (db, user) entry of the presence index used for the
 * connection-level early exit (CAS skips lookup when its db/user has no rule). */
  typedef struct t_qr_dbuser T_QR_DBUSER;
  struct t_qr_dbuser
  {
    char db_name[QR_NAME_LEN];	/* upper-cased */
    char user_name[QR_NAME_LEN];	/* upper-cased */
    int next_idx;		/* next dbuser index in hash chain, -1 = end */
  };

/* header located at the beginning of the rewrite shared memory segment */
  typedef struct t_qr_shm_header T_QR_SHM_HEADER;
  struct t_qr_shm_header
  {
    int magic;
    int owner_shm_id;		/* appl_server_shm_id that owns this segment */
    char broker_name[BROKER_NAME_LEN];	/* owning broker.  owner_shm_id alone cannot identify the
					 * segment: an admin command derives the key from the conf as
					 * it reads now, which may no longer be what the running broker
					 * built the segment with (see qr_admin_attach). */
    int generation;		/* bumped on every runtime mutation (add/reload/disable/enable).
				 * RESERVED: not yet consumed on the CAS side -- process-local cache
				 * invalidation currently uses T_QR_RULE.admin_seq.  kept for a future
				 * segment-wide cache-flush signal. */
    int writer_pid;		/* admin pid mutating the segment, 0 when none.  mutation runs under
				 * the rule-dir flock, so a non-zero value seen while holding that
				 * lock can only be a writer that died mid-mutation. */
    int rule_count;		/* number of allocated slots */
    int hash_size;
    int min_query_len;		/* shortest normalized original query length */
    int max_query_len;		/* longest normalized original query length  */
    int max_rules;		/* reserved slot capacity (QUERY_REWRITE_MAX_RULES) */
    int cfg_max_query_len;	/* per-query length cap (QUERY_REWRITE_MAX_QUERY_LEN) */
    int pool_slot;		/* fixed per-rule string pool slot size in bytes */
    int bucket_off;		/* offset of int bucket[hash_size]   */
    int rule_off;		/* offset of T_QR_RULE rule[rule_count] */
    int pool_off;		/* offset of string pool             */
    int total_size;
    int dbuser_count;		/* number of distinct (db, user) pairs */
    int dbuser_hash_size;	/* size of the dbuser hash bucket array */
    int dbuser_bucket_off;	/* offset of int dbuser_bucket[dbuser_hash_size] */
    int dbuser_off;		/* offset of T_QR_DBUSER dbuser[dbuser_count]    */
  };

  extern int qr_shm_create (T_BROKER_INFO * br_info_p, T_SHM_APPL_SERVER * shm_as_p);
  extern void qr_shm_destroy (int shm_key, int mode);
  extern int qr_make_shm_key (int appl_server_shm_id);

  extern void qr_admin_dump_info (FILE * fp, const T_BROKER_INFO * br_info_p);
  extern int qr_admin_add (const T_BROKER_INFO * br_info_p, const char *relpath, char *msg, int msgsz);
  extern int qr_admin_reload (const T_BROKER_INFO * br_info_p, const char *relpath, char *msg, int msgsz);
  extern int qr_admin_disable (const T_BROKER_INFO * br_info_p, const char *relpath, char *msg, int msgsz);
  extern int qr_admin_enable (const T_BROKER_INFO * br_info_p, const char *relpath, char *msg, int msgsz);

  extern void qr_rule_log_write (const T_BROKER_INFO * br_info_p, bool do_truncate, const char *relpath,
				 const char *reason);

  extern int qr_init (T_SHM_APPL_SERVER * shm_as_p);
  extern void qr_final ();
  extern void qr_load_dbuser_has_rules (const char *db_name, const char *user_name);
  extern int qr_lookup (const char *sql_stmt, int sql_len);
  extern const char *qr_get_rewrite_query (int rule_idx);
  extern const char *qr_get_orig_query (int rule_idx);
  extern const char *qr_get_rulepath (int rule_idx);
  extern short *qr_get_bind_src (int rule_idx);
  extern int qr_validate_markers (int rule_idx, int k_orig, int k_rewrite);
  extern int qr_get_valid_k_orig (int rule_idx, int k_rewrite);
  extern void qr_set_valid_k_orig (int rule_idx, int k_orig, int k_rewrite);

  extern void qr_set_disabled (int rule_idx);
  extern int qr_is_disabled (int rule_idx);

/* classification of an execute-time error for the demote decision.
 *   TRANSIENT   : infra/tran (server down, deadlock, lock timeout) -> streak unchanged
 *   DATA        : bind/data-dependent (constraint, conversion, ...) -> streak reset
 *   AMBIGUOUS   : plan built but execute failed (-495, SP)          -> streak ++
 *   RULE_DEFECT : bind-independent (authorization, unknown class)   -> demote immediately */
  typedef enum
  {
    QR_ERR_TRANSIENT,
    QR_ERR_DATA,
    QR_ERR_AMBIGUOUS,
    QR_ERR_RULE_DEFECT
  } QR_ERR_TIER;

  extern QR_ERR_TIER qr_exec_error_tier (int err);
/* record the outcome of one execute of a rewritten statement; updates the
 * per-rule failure streak and returns 1 if the rule must now be demoted.
 *   succeeded       : execute ran OK (arrays: at least one row succeeded)
 *   tier            : error class, read only when !succeeded
 *   all_rows_failed : execute_array only, num_query>=2 && every row failed */
  extern int qr_record_exec_result (int rule_idx, bool succeeded, QR_ERR_TIER tier, bool all_rows_failed);

#ifdef __cplusplus
}
#endif

#endif				/* _QUERY_REWRITE_H_ */
