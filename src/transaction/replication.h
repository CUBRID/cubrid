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
 * replication.h - the header file of replication module
 *
 */

#ifndef _REPLICATION_H_
#define _REPLICATION_H_

#ident "$Id$"

#include "config.h"

#include "system_parameter.h"
#include "oid.h"
#include "log_impl.h"
#include "memory_alloc.h"
#include "page_buffer.h"
#include "error_manager.h"
#include "thread_impl.h"


/* Replication would be started only when
 *    is_replicated is set by 1  (cubrid.conf)
 *    the target index is unique (would be replaced by primary key index)
 *    the caller wants to replicate
 *    the master has been backuped
 *    is_repliated is set by 1 for the target class
 */
#define IS_REPLICATED_MODE(class_oid, unique, need_replication)             \
               (PRM_REPLICATION_MODE &&                                     \
                need_replication &&                                         \
                repl_class_is_replicated(class_oid) &&                      \
                unique)
#define REPL_ERROR(error, arg)                                              \
           do { error = ER_REPL_ERROR;                                      \
                er_set(ER_WARNING_SEVERITY, ARG_FILE_LINE,ER_REPL_ERROR,    \
                       1, arg);                                             \
           } while (0)


typedef enum
{
  REPL_INFO_TYPE_SCHEMA
} REPL_INFO_TYPE;

typedef struct repl_info REPL_INFO;
struct repl_info
{
  int repl_info_type;
  char *info;
};

typedef struct repl_info_schema REPL_INFO_SCHEMA;
struct repl_info_schema
{
  int statement_type;
  char *name;
  char *ddl;
};

typedef struct repl_savepoint_info REPL_SAVEPOINT_INFO;
struct repl_savepoint_info
{
  REPL_SAVEPOINT_INFO *next;
  char *sp_name;
  int log_rec_start_idx;
};

/*
 * STATES OF TRANSACTIONS
 */

#if defined(SERVER_MODE) || defined(SA_MODE)
#if !defined(WINDOWS)
/* for replication, declare replication log dump function */
extern void repl_data_insert_log_dump (FILE * fp, int length, void *data);
extern void repl_data_udpate_log_dump (FILE * fp, int length, void *data);
extern void repl_data_delete_log_dump (FILE * fp, int length, void *data);
extern void repl_schema_log_dump (FILE * fp, int length, void *data);
extern bool repl_class_is_replicated (OID * class_oid);
extern void repl_log_send (void);
extern int repl_add_update_lsa (THREAD_ENTRY * thread_p, OID * inst_oid);
extern int
repl_log_insert (THREAD_ENTRY * thread_p, OID * class_oid, OID * inst_oid,
		 LOG_RECTYPE log_type, LOG_RCVINDEX rcvindex,
		 DB_VALUE * key_dbvalue);
extern int repl_log_insert_schema (THREAD_ENTRY * thread_p,
				   REPL_INFO_SCHEMA * repl_schema);
extern void repl_start_flush_mark (THREAD_ENTRY * thread_p);
extern void repl_end_flush_mark (THREAD_ENTRY * thread_p, bool need_undo);
extern int repl_add_savepoint_info (THREAD_ENTRY * thread_p, const char *sp_name);
extern int repl_log_abort_to_savepoint (THREAD_ENTRY * thread_p,
					const char *sp_name);
extern void repl_free_savepoint_info (REPL_SAVEPOINT_INFO * node);
#if defined(CUBRID_DEBUG)
extern void repl_debug_info ();
#endif /* CUBRID_DEBUG */
#endif /* !WINDOWS */
#endif /* SERVER_MODE || SA_MODE */

#endif /* _REPLICATION_H_ */
