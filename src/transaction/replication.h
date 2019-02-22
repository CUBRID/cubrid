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
#include "error_manager.h"
#if defined (SERVER_MODE) || defined (SA_MODE)
#include "log_impl.h"
#endif /* defined(SERVER_MODE) || defined(SA_MODE) */
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
