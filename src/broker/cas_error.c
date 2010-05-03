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
#include "cas_execute.h"

#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
#if defined(CAS_FOR_DBMS)
#include "glo_class.h"
#endif /* CAS_FOR_DBMS */
#endif /* CAS_FOR_ORACLE CAS_FOR_MYSQL */
static bool server_aborted = false;

#ifdef CAS_FOR_DBMS
void
err_msg_set (T_NET_BUF * net_buf, const char *file, int line)
{
  if ((err_info.err_indicator != CAS_ERROR_INDICATOR)
      && (err_info.err_indicator != DBMS_ERROR_INDICATOR))
    {
      cas_log_debug (ARG_FILE_LINE,
		     "invalid internal error info : file %s line %d", file,
		     line);
      return;
    }

  if (net_buf != NULL)
    {
      net_buf_error_msg_set (net_buf, err_info.err_indicator,
			     err_info.err_number, err_info.err_string,
			     err_info.err_file, err_info.err_line);
      cas_log_debug (ARG_FILE_LINE,
		     "err_msg_set: err_code %d file %s line %d",
		     err_info.err_number, file, line);
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
#else /* CAS_FOR_ORACLE CAS_FOR_MYSQL */
#ifndef LIBCAS_FOR_JSP
  if ((net_buf == NULL)
      && (err_info.err_number == ER_TM_SERVER_DOWN_UNILATERALLY_ABORTED))
    {
      set_server_aborted (true);
    }
#endif

  switch (err_info.err_number)
    {
    case -111:			/* ER_TM_SERVER_DOWN_UNILATERALLY_ABORTED */
    case -199:			/* ER_NET_SERVER_CRASHED */
    case -224:			/* ER_OBJ_NO_CONNECT */
      /*case -581: *//* ER_DB_NO_MODIFICATIONS */
#ifndef LIBCAS_FOR_JSP
      as_info->reset_flag = TRUE;
      cas_log_debug (ARG_FILE_LINE, "db_err_msg_set: set reset_flag");
#endif
      break;
    }
#endif /* CAS_FOR_ORACLE CAS_FOR_MYSQL */
}

int
error_info_set (int err_number, int err_indicator, const char *file, int line)
{
  return error_info_set_with_msg (err_number, err_indicator, NULL, file,
				  line);
}

int
error_info_set_with_msg (int err_number, int err_indicator,
			 const char *err_msg, const char *file, int line)
{
  char *tmp_err_msg;

  if (err_info.err_indicator != ERROR_INDICATOR_UNSET)
    {
      cas_log_debug (ARG_FILE_LINE,
		     "ERROR_INFO_SET reset error info : err_code %d",
		     err_info.err_number);
      return err_info.err_indicator;
    }

#if defined(CAS_FOR_ORACLE) || defined(CAS_FOR_MYSQL)
  err_info.err_number = err_number;
#else /* CAS_FOR_ORACLE CAS_FOR_MYSQL */
  if ((err_indicator == DBMS_ERROR_INDICATOR) && (err_number == -1))	/* might be connection error */
    {
      err_info.err_number = er_errid ();
    }
  else
    {
      err_info.err_number = err_number;
    }
#endif /* CAS_FOR_ORACLE CAS_FOR_MYSQL */
  err_info.err_indicator = err_indicator;
  strncpy (err_info.err_file, file, ERR_FILE_LENGTH - 1);
  err_info.err_string[ERR_FILE_LENGTH - 1] = 0;
  err_info.err_line = line;

  if ((err_indicator == CAS_ERROR_INDICATOR) && (err_msg == NULL))
    return err_indicator;

#if defined(CAS_FOR_ORACLE) || defined(CAS_FOR_MYSQL)
  if (err_msg)
    {
      strncpy (err_info.err_string, err_msg, ERR_MSG_LENGTH - 1);
    }
#else /* CAS_FOR_ORACLE CAS_FOR_MYSQL */
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
#endif /* CAS_FOR_ORACLE CAS_FOR_MYSQL */

  return err_indicator;
}

void
error_info_clear ()
{
  err_info.err_indicator = ERROR_INDICATOR_UNSET;
  err_info.err_number = CAS_NO_ERROR;
  memset (err_info.err_string, 0x00, ERR_MSG_LENGTH);
  memset (err_info.err_file, 0x00, ERR_FILE_LENGTH);
  err_info.err_line = 0;
}

#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
void
glo_err_msg_set (T_NET_BUF * net_buf, int err_code, const char *method_nm)
{
  char err_msg[256];

  if (net_buf == NULL)
    return;

  glo_err_msg_get (err_code, err_msg);
#ifdef CAS_DEBUG
  sprintf (err_msg, "%s:%s", method_nm, err_msg);
#endif
  NET_BUF_ERROR_MSG_SET (net_buf, CAS_ERROR_INDICATOR, CAS_ER_GLO, err_msg);
}

void
glo_err_msg_get (int err_code, char *err_msg)
{
  switch (err_code)
    {
    case INVALID_STRING_INPUT_ARGUMENT:
      strcpy (err_msg, "INVALID_STRING_INPUT_ARGUMENT");
      break;
    case INVALID_INTEGER_INPUT_ARGUMENT:
      strcpy (err_msg, "INVALID_INTEGER_INPUT_ARGUMENT");
      break;
    case INVALID_STRING_OR_OBJ_ARGUMENT:
      strcpy (err_msg, "INVALID_STRING_OR_OBJ_ARGUMENT");
      break;
    case INVALID_OBJECT_INPUT_ARGUMENT:
      strcpy (err_msg, "INVALID_OBJECT_INPUT_ARGUMENT");
      break;
    case UNABLE_TO_FIND_GLO_STRUCTURE:
      strcpy (err_msg, "UNABLE_TO_FIND_GLO_STRUCTURE");
      break;
    case COULD_NOT_ACQUIRE_WRITE_LOCK:
      strcpy (err_msg, "COULD_NOT_ACQUIRE_WRITE_LOCK");
      break;
    case ERROR_DURING_TRUNCATION:
      strcpy (err_msg, "ERROR_DURING_TRUNCATION");
      break;
    case ERROR_DURING_DELETE:
      strcpy (err_msg, "ERROR_DURING_DELETE");
      break;
    case ERROR_DURING_INSERT:
      strcpy (err_msg, "ERROR_DURING_INSERT");
      break;
    case ERROR_DURING_WRITE:
      strcpy (err_msg, "ERROR_DURING_WRITE");
      break;
    case ERROR_DURING_READ:
      strcpy (err_msg, "ERROR_DURING_READ");
      break;
    case ERROR_DURING_SEEK:
      strcpy (err_msg, "ERROR_DURING_SEEK");
      break;
    case ERROR_DURING_APPEND:
      strcpy (err_msg, "ERROR_DURING_APPEND");
      break;
    case ERROR_DURING_MIGRATE:
      strcpy (err_msg, "ERROR_DURING_MIGRATE");
      break;
    case COPY_TO_ERROR:
      strcpy (err_msg, "COPY_TO_ERROR");
      break;
    case COPY_FROM_ERROR:
      strcpy (err_msg, "COPY_FROM_ERROR");
      break;
    case COULD_NOT_ALLOCATE_SEARCH_BUFFERS:
      strcpy (err_msg, "COULD_NOT_ALLOCATE_SEARCH_BUFFERS");
      break;
    case COULD_NOT_COMPILE_REGULAR_EXPRESSION:
      strcpy (err_msg, "COULD_NOT_COMPILE_REGULAR_EXPRESSION");
      break;
    case COULD_NOT_RESET_WORKING_BUFFER:
      strcpy (err_msg, "COULD_NOT_RESET_WORKING_BUFFER");
      break;
    case SEARCH_ERROR_ON_POSITION_CACHE:
      strcpy (err_msg, "SEARCH_ERROR_ON_POSITION_CACHE");
      break;
    case SEARCH_ERROR_ON_DATA_READ:
      strcpy (err_msg, "SEARCH_ERROR_ON_DATA_READ");
      break;
    case SEARCH_ERROR_DURING_LOOKUP:
      strcpy (err_msg, "SEARCH_ERROR_DURING_LOOKUP");
      break;
    case SEARCH_ERROR_REPOSITIONING_POINTER:
      strcpy (err_msg, "SEARCH_ERROR_REPOSITIONING_POINTER");
      break;
    default:
      sprintf (err_msg, "%d", err_code);
    }
}
#endif /* CAS_FOR_ORACLE CAS_FOR_MYSQL */
#endif /* CAS_FOR_DBMS */

void
set_server_aborted (bool is_aborted)
{
  server_aborted = is_aborted;
}

bool
is_server_aborted ()
{
  return server_aborted;
}
