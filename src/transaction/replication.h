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

typedef struct repl_info REPL_INFO;	// todo - remove me
struct repl_info
{
  char *info;
  int repl_info_type;
  bool need_replication;

  // *INDENT-OFF*
  repl_info () = default;
  // *INDENT-ON*
};

typedef struct repl_info_sbr REPL_INFO_SBR;
struct repl_info_sbr
{
  int statement_type;
  char *name;
  char *stmt_text;
  char *db_user;
  char *sys_prm_context;
  char *savepoint_name;		/* Debug purpose, must be removed with PRM_ID_REPL_LOG_LOCAL_DEBUG */

  // *INDENT-OFF*
  repl_info_sbr () = default;
  // *INDENT-ON*
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
};
#endif

/*
 * STATES OF TRANSACTIONS
 */

#if defined(SERVER_MODE) || defined(SA_MODE)
extern int repl_log_abort_after_lsa (LOG_TDES * tdes, LOG_LSA * start_lsa);
#endif /* SERVER_MODE || SA_MODE */

#endif /* _REPLICATION_H_ */
