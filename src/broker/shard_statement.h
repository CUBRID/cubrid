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
 * shard_statement.h -
 *
 */

#ifndef _SHARD_STATEMENT_H_
#define _SHARD_STATEMENT_H_

#ident "$Id$"

#include "shard_parser.h"
#include "broker_config.h"
#include "shard_proxy_queue.h"
#include "cas_protocol.h"

#define SHARD_STMT_INVALID_HANDLE_ID	(-1)
#define SHARD_STMT_MAX_NUM_ALLOC	(8192)

enum
{
  SHARD_STMT_STATUS_UNUSED = 0,
  SHARD_STMT_STATUS_IN_PROGRESS = 1,
  SHARD_STMT_STATUS_COMPLETE = 2,
  SHARD_STMT_STATUS_INVALID = 3
};

enum
{
  SHARD_STMT_TYPE_PREPARED = 0,
  SHARD_STMT_TYPE_SCHEMA_INFO = 1,
  SHARD_STMT_TYPE_EXCLUSIVE = 2
};

typedef struct t_shard_stmt T_SHARD_STMT;
struct t_shard_stmt
{
  unsigned int index;
  unsigned int num_alloc;

  int stmt_h_id;		/* stmt handle id for client */
  int status;
  int stmt_type;

  T_BROKER_VERSION client_version;	/* client version */

  int ctx_cid;			/* owner context cid */
  unsigned int ctx_uid;		/* owner context uid */
  char database_user[SRV_CON_DBUSER_SIZE];

  int num_pinned;		/* pinned count */
  T_SHARD_STMT *lru_next;
  T_SHARD_STMT *lru_prev;
  T_SHARD_STMT *hash_next;
  T_SHARD_STMT *hash_prev;

  SP_PARSER_CTX *parser;	/* parser context */

  int request_buffer_length;
  void *request_buffer;		/* prepare request */
  int reply_buffer_length;
  void *reply_buffer;		/* prepare reply */

  T_SHARD_QUEUE waitq;

  int *srv_h_id_ent;
};

typedef struct t_shard_stmt_global T_SHARD_STMT_GLOBAL;
struct t_shard_stmt_global
{
  int max_num_stmt;

  int max_num_shard;
  int num_cas_per_shard;

  T_SHARD_STMT *lru;		/* head */
  T_SHARD_STMT *mru;		/* tail */

  T_SHARD_STMT *stmt_ent;

  MHT_TABLE *stmt_map;
};

extern T_SHARD_STMT *shard_stmt_find_by_sql (char *sql_stmt, const char *db_user, T_BROKER_VERSION client_version);
extern T_SHARD_STMT *shard_stmt_find_by_stmt_h_id (int stmt_h_id);
extern int shard_stmt_pin (T_SHARD_STMT * stmt_p);
extern int shard_stmt_unpin (T_SHARD_STMT * stmt_p);

extern void shard_stmt_check_waiter_and_wakeup (T_SHARD_STMT * stmt_p);

extern T_SHARD_STMT *shard_stmt_new_prepared_stmt (char *sql_stmt, int ctx_cid, unsigned int ctx_uid,
						   T_BROKER_VERSION client_version);
extern T_SHARD_STMT *shard_stmt_new_schema_info (int ctx_cid, unsigned int ctx_uid);
extern T_SHARD_STMT *shard_stmt_new_exclusive (char *sql_stmt, int ctx_cid, unsigned int ctx_uid,
					       T_BROKER_VERSION client_version);
extern void shard_stmt_free (T_SHARD_STMT * stmt_p);
extern void shard_stmt_destroy (void);
extern int shard_stmt_find_srv_h_id_for_shard_cas (int stmt_h_id, int shard_id, int cas_id);
extern int shard_stmt_add_srv_h_id_for_shard_cas (int stmt_h_id, int shard_id, int cas_id, int srv_h_id);
extern void shard_stmt_del_srv_h_id_for_shard_cas (int stmt_h_id, int shard_id, int cas_id);
extern void shard_stmt_del_all_srv_h_id_for_shard_cas (int shard_id, int cas_id);
extern int shard_stmt_set_hint_list (T_SHARD_STMT * stmt_p);
extern int shard_stmt_get_hint_type (T_SHARD_STMT * stmt_p);
extern int shard_stmt_save_prepare_request (T_SHARD_STMT * stmt_p, bool has_shard_val_hint, char **prepare_req,
					    int *prepare_req_len, char *argv_sql_stmt, char *argv_remainder,
					    char *orgzd_sql);
#if defined (PROXY_VERBOSE_DEBUG)
extern void shard_stmt_dump_title (FILE * fp);
extern void shard_stmt_dump (FILE * fp, T_SHARD_STMT * stmt_p);
extern void shard_stmt_dump_all (FILE * fp);
#endif /* PROXY_VERBOSE_DEBUG */
extern char *shard_str_stmt (T_SHARD_STMT * stmt_p);
extern int shard_stmt_initialize (int initial_size);
extern char *shard_stmt_rewrite_sql (bool * has_shard_val_hint, char *sql_stmt, char appl_server);

extern void shard_statement_wait_timer (void);
extern void shard_stmt_set_status_invalid (int stmt_h_id);
extern void shard_stmt_set_status_complete (int stmt_h_id);
#endif /* _SHARD_STATEMENT_H_ */
