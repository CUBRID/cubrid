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
 * replication.h - the header file of replication module
 *
 */

#ifndef _REPLICATION_H_
#define _REPLICATION_H_

#ident "$Id$"

#include "config.h"
#include "error_manager.h"
#if defined(SERVER_MODE) || defined(SA_MODE)
#include "log_impl.h"
#endif /* defined(SERVER_MODE) || defined(SA_MODE) */
#include "log_lsa.hpp"
#include "memory_alloc.h"
#include "oid.h"
#include "system_parameter.h"
#if defined(SERVER_MODE) || defined(SA_MODE)
#include "thread_compat.hpp"
#endif /* defined(SERVER_MODE) || defined(SA_MODE) */

typedef enum
{
  REPL_INFO_TYPE_SBR,		/* statement-based */
  REPL_INFO_TYPE_RBR_START,	/* row-based start */
  REPL_INFO_TYPE_RBR_NORMAL,	/* row-based normal */
  REPL_INFO_TYPE_RBR_END	/* row-based end */
} REPL_INFO_TYPE;

typedef struct repl_info REPL_INFO;
struct repl_info
{
  char *info;
  int repl_info_type;
  bool need_replication;
};

typedef struct repl_info_statement REPL_INFO_SBR;
struct repl_info_statement
{
  int statement_type;
  char *name;
  char *stmt_text;
  char *db_user;
  char *sys_prm_context;
};

#if defined(SERVER_MODE) || defined(SA_MODE)
enum log_repl_flush
{
  LOG_REPL_DONT_NEED_FLUSH = -1,	/* no flush */
  LOG_REPL_COMMIT_NEED_FLUSH = 0,	/* log must be flushed at commit */
  LOG_REPL_NEED_FLUSH = 1	/* log must be flushed at commit and rollback */
};
typedef enum log_repl_flush LOG_REPL_FLUSH;

typedef struct log_repl LOG_REPL_RECORD;
struct log_repl
{
  LOG_RECTYPE repl_type;	/* LOG_REPLICATION_DATA or LOG_REPLICATION_SCHEMA */
  LOG_RCVINDEX rcvindex;
  OID inst_oid;
  LOG_LSA lsa;
  char *repl_data;		/* the content of the replication log record */
  int length;
  LOG_REPL_FLUSH must_flush;
  bool tde_encrypted;		/* if it contains user data of tde-class */
};
#endif

/*
 * STATES OF TRANSACTIONS
 */

#if defined(SERVER_MODE) || defined(SA_MODE)
/* for replication, declare replication log dump function */
extern void repl_data_insert_log_dump (FILE * fp, int length, void *data);
#if defined (ENABLE_UNUSED_FUNCTION)
extern void repl_data_udpate_log_dump (FILE * fp, int length, void *data);
extern void repl_data_delete_log_dump (FILE * fp, int length, void *data);
#endif
extern void repl_schema_log_dump (FILE * fp, int length, void *data);
extern void repl_log_send (void);
extern int repl_add_update_lsa (THREAD_ENTRY * thread_p, const OID * inst_oid);
extern int repl_log_insert (THREAD_ENTRY * thread_p, const OID * class_oid, const OID * inst_oid, LOG_RECTYPE log_type,
			    LOG_RCVINDEX rcvindex, DB_VALUE * key_dbvalue, REPL_INFO_TYPE repl_type);
extern int repl_log_insert_statement (THREAD_ENTRY * thread_p, REPL_INFO_SBR * repl_info);
extern void repl_start_flush_mark (THREAD_ENTRY * thread_p);
extern void repl_end_flush_mark (THREAD_ENTRY * thread_p, bool need_undo);
extern int repl_log_abort_after_lsa (LOG_TDES * tdes, LOG_LSA * start_lsa);
#if defined(CUBRID_DEBUG)
extern void repl_debug_info ();
#endif /* CUBRID_DEBUG */
#endif /* SERVER_MODE || SA_MODE */

#endif /* _REPLICATION_H_ */
