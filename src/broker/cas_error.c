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
 * cas_error.c -
 */

#ident "$Id$"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#if defined(WINDOWS)
#include <winsock2.h>
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <process.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#endif

#include "cas.h"
#include "cas_common.h"
#include "cas_execute.h"
#include "cas_network.h"
#include "cas_util.h"
#include "cas_schema_info.h"
#include "cas_log.h"
#include "cas_str_like.h"

#include "broker_filename.h"
#include "cas_sql_log2.h"

#define CUBRID_CAS_ERR_TRACE        "CUBRID_CAS_ERR_TRACE"

static bool server_aborted = false;

void
err_msg_set (T_NET_BUF * net_buf, const char *file, int line)
{
  if ((err_info.err_indicator != CAS_ERROR_INDICATOR) && (err_info.err_indicator != DBMS_ERROR_INDICATOR))
    {
      cas_log_debug (ARG_FILE_LINE, "invalid internal error info : file %s line %d", file, line);
      return;
    }

  if (net_buf != NULL)
    {
      net_buf_error_msg_set (net_buf, err_info.err_indicator, err_info.err_number, err_info.err_string,
			     err_info.err_file, err_info.err_line);
      cas_log_debug (ARG_FILE_LINE, "err_msg_set: err_code %d file %s line %d", err_info.err_number, file, line);
    }
  if (err_info.err_indicator == CAS_ERROR_INDICATOR)
    {
      return;
    }

#if defined(CAS_FOR_ORACLE) || defined(CAS_FOR_MYSQL)
  if ((err_info.err_indicator == DBMS_ERROR_INDICATOR) && !is_server_alive ())
    {
      set_server_aborted (true);
    }
#if defined(CAS_FOR_MYSQL)
  switch (err_info.err_number)
    {
    case CR_COMMANDS_OUT_OF_SYNC:
      /**
       * if you execute two select query in one connection,
       * [2014][Commands out of sync; you can't run this command now]
       * error will be occurred and all query execution will be failed until
       * close select query statement. so we have to terminate cub_cas_mysql
       * to avoid this situation.
       */
      if (as_info->reset_flag == FALSE)
	{
	  cas_log_write_and_end (0, true, "FAILED TO EXECUTE QUERY AS INTERNAL PROBLEM. (MySQL ERR %d : %s)",
				 err_info.err_number, err_info.err_string);
	}
      as_info->reset_flag = TRUE;
      cas_set_db_connect_status (DB_CONNECTION_STATUS_NOT_CONNECTED);
      break;
    case CR_SERVER_GONE_ERROR:
    case CR_SERVER_LOST:
      as_info->reset_flag = TRUE;
      cas_set_db_connect_status (DB_CONNECTION_STATUS_NOT_CONNECTED);
      cas_log_debug (ARG_FILE_LINE, "db_err_msg_set: set reset_flag");
      break;
    }
#elif defined(CAS_FOR_ORACLE)
  switch (err_info.err_number)
    {
    case 3114:			/* ORA-03114: not connected to ORACLE */
    case 3113:			/* ORA-03113: end-of-file on communication channel */
    case 1012:			/* ORA-01012: not logged on */
    case 28:			/* ORA-00028: your session has been killed */
      as_info->reset_flag = TRUE;
      cas_set_db_connect_status (DB_CONNECTION_STATUS_NOT_CONNECTED);
      cas_log_debug (ARG_FILE_LINE, "db_err_msg_set: set reset_flag");
      break;
    }
#endif /* CAS_FOR_MYSQL */
#else /* CAS_FOR_ORACLE || CAS_FOR_MYSQL */
#ifndef LIBCAS_FOR_JSP
  if ((net_buf == NULL) && (err_info.err_number == ER_TM_SERVER_DOWN_UNILATERALLY_ABORTED))
    {
      set_server_aborted (true);
    }
#endif

  switch (err_info.err_number)
    {
    case ER_TM_SERVER_DOWN_UNILATERALLY_ABORTED:
    case ER_NET_SERVER_CRASHED:
    case ER_OBJ_NO_CONNECT:
    case ER_BO_CONNECT_FAILED:
      /* case -581: *//* ER_DB_NO_MODIFICATIONS */
#ifndef LIBCAS_FOR_JSP
      as_info->reset_flag = TRUE;
      cas_log_debug (ARG_FILE_LINE, "db_err_msg_set: set reset_flag");
#endif
      break;
    }
#endif /* CAS_FOR_ORACLE || CAS_FOR_MYSQL */
}

int
error_info_set (int err_number, int err_indicator, const char *file, int line)
{
  return error_info_set_with_msg (err_number, err_indicator, NULL, false, file, line);
}

int
error_info_set_force (int err_number, int err_indicator, const char *file, int line)
{
  return error_info_set_with_msg (err_number, err_indicator, NULL, true, file, line);
}

int
error_info_set_with_msg (int err_number, int err_indicator, const char *err_msg, bool force, const char *file, int line)
{
  char *tmp_err_msg;

  if ((!force) && (err_info.err_indicator != ERROR_INDICATOR_UNSET))
    {
      cas_log_debug (ARG_FILE_LINE, "ERROR_INFO_SET reset error info : err_code %d", err_info.err_number);
      return err_info.err_indicator;
    }

#if defined(CAS_FOR_ORACLE) || defined(CAS_FOR_MYSQL)
  err_info.err_number = err_number;
#else /* CAS_FOR_ORACLE || CAS_FOR_MYSQL */
  if ((err_indicator == DBMS_ERROR_INDICATOR) && (err_number == -1))	/* might be connection error */
    {
      assert (er_errid () != NO_ERROR);
      err_info.err_number = er_errid ();
    }
  else
    {
      err_info.err_number = err_number;
    }
#endif /* CAS_FOR_ORACLE || CAS_FOR_MYSQL */
  err_info.err_indicator = err_indicator;
  strncpy (err_info.err_file, file, ERR_FILE_LENGTH - 1);
  err_info.err_string[ERR_FILE_LENGTH - 1] = 0;
  err_info.err_line = line;

  if (err_indicator == CAS_ERROR_INDICATOR)
    {
      const char *envvar_caserr_trace = NULL;
      envvar_caserr_trace = getenv (CUBRID_CAS_ERR_TRACE);
      if (err_msg == NULL)
	{
	  if (envvar_caserr_trace != NULL && strcasecmp (envvar_caserr_trace, "on") == 0)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SQL_ERROR_LOG_TRACE, 1, err_number);
	    }
	  return err_indicator;
	}
      else
	{
	  if (envvar_caserr_trace != NULL && strcasecmp (envvar_caserr_trace, "on") == 0)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SQL_ERROR_LOG_MSG_TRACE, 2, err_number, err_msg);
	    }
	}
    }

#if defined(CAS_FOR_ORACLE) || defined(CAS_FOR_MYSQL)
  if (err_msg)
    {
      strncpy (err_info.err_string, err_msg, ERR_MSG_LENGTH - 1);
    }
#else /* CAS_FOR_ORACLE || CAS_FOR_MYSQL */
  if (err_msg)
    {
      strncpy (err_info.err_string, err_msg, ERR_MSG_LENGTH - 1);
    }
  else if (err_indicator == DBMS_ERROR_INDICATOR)
    {
      tmp_err_msg = (char *) db_error_string (1);
      strncpy (err_info.err_string, tmp_err_msg, ERR_MSG_LENGTH - 1);
    }
  err_info.err_string[ERR_MSG_LENGTH - 1] = 0;
#endif /* CAS_FOR_ORACLE || CAS_FOR_MYSQL */

  return err_indicator;
}

int
is_error_info_set (void)
{
  return (err_info.err_indicator == ERROR_INDICATOR_UNSET) ? 0 : 1;
}

void
error_info_clear (void)
{
  err_info.err_indicator = ERROR_INDICATOR_UNSET;
  err_info.err_number = CAS_NO_ERROR;
  memset (err_info.err_string, 0x00, ERR_MSG_LENGTH);
  memset (err_info.err_file, 0x00, ERR_FILE_LENGTH);
  err_info.err_line = 0;
}


void
set_server_aborted (bool is_aborted)
{
  server_aborted = is_aborted;
}

bool
is_server_aborted (void)
{
  return server_aborted;
}
