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
 * cas_oracle_execute.c -
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
#include "cas_oracle.h"
#include "cas_sql_log2.h"
#include "cas_error_log.h"
#include "broker_filename.h"
#include "release_string.h"

#define ORA_ENV     _db_info.env
#define ORA_ERR     _db_info.err
#define ORA_SVC     _db_info.svc
#define ORA_NAME    _db_info.name
#define ORA_USER    _db_info.user
#define ORA_PASS    _db_info.pass

#define ORA_BUFSIZ  4096

#define GOTO_ORA_ERROR(ret, label) \
  if (!ORA_SUCCESS(ret)) { goto label; }
#define RET_ORA_ERROR(ret) \
  if (!ORA_SUCCESS(ret)) { return ret; }

#define ORA_PARAM_COUNT(stmt, count) \
  OCIAttrGet (stmt, OCI_HTYPE_STMT, &count, 0, OCI_ATTR_PARAM_COUNT, ORA_ERR)
#define ORA_STMT_TYPE(stmt, type) \
  OCIAttrGet (stmt, OCI_HTYPE_STMT, &type, 0, OCI_ATTR_STMT_TYPE, ORA_ERR)
#define ORA_BIND_COUNT(stmt, count) \
  OCIAttrGet (stmt, OCI_HTYPE_STMT, &count, 0, OCI_ATTR_BIND_COUNT, ORA_ERR)
#define ORA_ROW_COUNT(stmt, count) \
  OCIAttrGet (stmt, OCI_HTYPE_STMT, &count, 0, OCI_ATTR_ROW_COUNT, ORA_ERR)
#define ORA_CURR_POS(stmt, pos) \
  OCIAttrGet (stmt, OCI_HTYPE_STMT, &pos, 0, OCI_ATTR_CURRENT_POSITION, ORA_ERR)

static T_PREPARE_CALL_INFO *make_prepare_call_info (int num_args);
static void prepare_call_info_dbval_clear (T_PREPARE_CALL_INFO * call_info);
static int db_value_clear (DB_VALUE * value);
static void db_make_null (DB_VALUE * value);

static ORACLE_INFO _db_info;
static int _offset_row_count;

int
cas_oracle_query_cancel (void)
{
  int ret;
  ret = OCIBreak (ORA_SVC, ORA_ERR);
  if (ORA_SUCCESS (ret))
    {
      ret = OCIReset (ORA_SVC, ORA_ERR);
    }
  return ret;
}

int
cas_oracle_stmt_close (void *stmt)
{
  return OCIHandleFree (stmt, OCI_HTYPE_STMT);
}

int
ux_check_connection (void)
{
  return ux_is_database_connected ()? 0 : -1;
}

int
ux_is_database_connected (void)
{
  return (ORA_NAME[0] != 0);
}

static const char *
cas_oracle_get_errmsg (void)
{
  static text buf[ORA_BUFSIZ];
  int ret, code;

  ret = OCIErrorGet (ORA_ERR, 1, NULL, &code, buf, ORA_BUFSIZ,
		     OCI_HTYPE_ERROR);
  if (ORA_SUCCESS (ret))
    {
      int size = strlen ((char *) buf);
      if (buf[size - 1] == '\n')
	{
	  buf[size - 1] = 0;
	}
      cas_error_log_write (code, (const char *) buf);
      ERROR_INFO_SET_WITH_MSG (code, DBMS_ERROR_INDICATOR, (char *) buf);
      return (const char *) buf;
    }
  return NULL;
}

static int
cas_oracle_get_errno (void)
{
  text buf[ORA_BUFSIZ];
  int code, size;

  OCIErrorGet (ORA_ERR, 1, NULL, &code, buf, ORA_BUFSIZ, OCI_HTYPE_ERROR);
  size = strlen ((char *) buf);
  if (buf[size - 1] == '\n')
    {
      buf[size - 1] = 0;
    }
  cas_error_log_write (code, (const char *) buf);
  return ERROR_INFO_SET_WITH_MSG (code, DBMS_ERROR_INDICATOR, (char *) buf);
}

static int
cas_oracle_connect_db (char *tns, char *db_user, char *db_pass,
		       char **db_err_msg)
{
  int ret;
  const char *err_msg;

  ret = OCIInitialize (OCI_OBJECT, 0, 0, 0, 0);
  GOTO_ORA_ERROR (ret, oracle_connect_error);
  ret = OCIEnvInit (&ORA_ENV, OCI_DEFAULT, 0, 0);
  GOTO_ORA_ERROR (ret, oracle_connect_error);
  ret = OCIHandleAlloc (ORA_ENV, (dvoid **) & ORA_ERR, OCI_HTYPE_ERROR, 0, 0);
  GOTO_ORA_ERROR (ret, oracle_connect_error);
  ret = OCIHandleAlloc (ORA_ENV, (dvoid **) & ORA_SVC, OCI_HTYPE_SVCCTX, 0,
			0);
  GOTO_ORA_ERROR (ret, oracle_connect_error);
  ret = OCILogon (ORA_ENV, ORA_ERR, &ORA_SVC, (text *) db_user,
		  strlen (db_user), (text *) db_pass, strlen (db_pass),
		  (text *) tns, strlen (tns));
  GOTO_ORA_ERROR (ret, oracle_connect_error);
  return ret;

oracle_connect_error:
  if (ret < 0 && db_err_msg && ORA_ERR != NULL)
    {
      err_msg = cas_oracle_get_errmsg ();
      if (err_msg == NULL)
	{
	  *db_err_msg = NULL;
	}
      else
	{
	  *db_err_msg = (char *) calloc (1, strlen (err_msg) + 1);
	}

      if (*db_err_msg != NULL)
	{
	  strcpy (*db_err_msg, err_msg);
	}
      else
	{
	  ret = ERROR_INFO_SET (CAS_ER_NO_MORE_MEMORY, CAS_ERROR_INDICATOR);
	}
    }

  if (ORA_ERR != NULL)
    {
      OCIHandleFree (ORA_ERR, OCI_HTYPE_ERROR);
      ORA_ERR = 0;
    }
  if (ORA_ENV != NULL)
    {
      OCIHandleFree (ORA_ENV, OCI_HTYPE_ENV);
      ORA_ENV = 0;
    }
  return ret;
}

static void
c4o_copy_host_to_as_info (char *tns)
{
  const char *delim;
  char *token, *save;
  int host_len;

  delim = "(=) ";
  token = strtok_r (tns, delim, &save);
  while (token != NULL)
    {
      if (strcasecmp (token, "HOST") == 0)
	{
	  token = strtok_r (NULL, delim, &save);
	  if (token == NULL)
	    {
	      return;
	    }
	  else
	    {
	      strncpy (as_info->database_host, token, MAXHOSTNAMELEN);
	      return;
	    }
	}
      token = strtok_r (NULL, delim, &save);
    }
}

int
ux_database_connect (char *db_alias, char *db_user, char *db_passwd,
		     char **db_err_msg)
{
  char tns[ORA_BUFSIZ];
  const char *err_msg;
  int err_code = 0;

  if (ux_is_database_connected ())
    {
      if (strcmp (ORA_NAME, db_alias) != 0 || strcmp (ORA_USER, db_user) != 0)
	{
	  ux_database_shutdown ();
	}
      else
	{
	  return err_code;
	}
    }

  err_code = cfg_get_dbinfo (db_alias, tns);
  if (err_code < 0)
    {
      return ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
    }

  err_code = cas_oracle_connect_db (tns, db_user, db_passwd, db_err_msg);
  if (ORA_SUCCESS (err_code))
    {
      strcpy (ORA_NAME, db_alias);
      strcpy (ORA_USER, db_user);
      strcpy (ORA_PASS, db_passwd);
      strncpy (as_info->database_name, db_alias, SRV_CON_DBNAME_SIZE - 1);
      c4o_copy_host_to_as_info (tns);
      as_info->last_connect_time = time (NULL);
    }

  return err_code;
}

void
ux_database_shutdown (void)
{
  if (!ux_is_database_connected ())
    {
      return;
    }

  OCILogoff (ORA_SVC, ORA_ERR);
  OCIHandleFree (ORA_ERR, OCI_HTYPE_ERROR);
  OCIHandleFree (ORA_ENV, OCI_HTYPE_ENV);

  ORA_SVC = 0;
  ORA_ERR = 0;
  ORA_ENV = 0;
  ORA_NAME[0] = 0;
  ORA_USER[0] = 0;
  ORA_PASS[0] = 0;
}

int
ux_end_tran (int tran_type, bool reset_con_status)
{
  int ret = 0;

  if (shm_appl->select_auto_commit == ON)
    {
      hm_srv_handle_reset_active ();
    }
  if (!as_info->cur_statement_pooling)
    {
      hm_srv_handle_free_all ();
    }

  switch (tran_type)
    {
    case CCI_TRAN_COMMIT:
      ret = OCITransCommit (ORA_SVC, ORA_ERR, OCI_DEFAULT);
      break;
    case CCI_TRAN_ROLLBACK:
      ret = OCITransRollback (ORA_SVC, ORA_ERR, OCI_DEFAULT);
      break;
    default:
      return ERROR_INFO_SET (CAS_ER_INTERNAL, CAS_ERROR_INDICATOR);
    }

  if (ORA_SUCCESS (ret))
    {
      unset_xa_prepare_flag ();
      if (reset_con_status)
	{
	  as_info->con_status = CON_STATUS_OUT_TRAN;
	}
      return ret;
    }
  else
    {
      errors_in_transaction++;
      return cas_oracle_get_errno ();
    }
}

static void
convert_stmt_type_oracle_to_cubrid (T_SRV_HANDLE * srv_handle)
{
  switch (srv_handle->stmt_type)
    {
    case OCI_STMT_SELECT:
      srv_handle->stmt_type = CUBRID_STMT_SELECT;
      break;
    case OCI_STMT_UPDATE:
      srv_handle->stmt_type = CUBRID_STMT_UPDATE;
      break;
    case OCI_STMT_DELETE:
      srv_handle->stmt_type = CUBRID_STMT_DELETE;
      break;
    case OCI_STMT_INSERT:
      srv_handle->stmt_type = CUBRID_STMT_INSERT;
      break;
    case OCI_STMT_CREATE:
    case OCI_STMT_DROP:
    case OCI_STMT_ALTER:
    case OCI_STMT_BEGIN:
    case OCI_STMT_DECLARE:
    default:
      srv_handle->stmt_type = CUBRID_MAX_STMT_TYPE;
      break;
    }
}

static int
convert_data_type_oracle_to_cas (OCIParam * col)
{
  ub2 type;
  OCIAttrGet (col, OCI_DTYPE_PARAM, &type, 0, OCI_ATTR_DATA_TYPE, ORA_ERR);

  switch (type)
    {
    case SQLT_NUM:
    case SQLT_VNU:
      return CCI_U_TYPE_NUMERIC;
    case SQLT_INT:
      return CCI_U_TYPE_INT;
    case SQLT_FLT:
      return CCI_U_TYPE_DOUBLE;
    case SQLT_TIMESTAMP:
    case SQLT_TIMESTAMP_TZ:
    case SQLT_TIMESTAMP_LTZ:
      return CCI_U_TYPE_TIMESTAMP;
    case SQLT_DAT:
      return CCI_U_TYPE_DATE;
    case SQLT_AFC:
    case SQLT_STR:
      return CCI_U_TYPE_CHAR;
    case SQLT_LNG:
    case SQLT_CHR:
    case SQLT_VST:
      return CCI_U_TYPE_STRING;
    case SQLT_CLOB:
    case SQLT_BLOB:
      return CCI_U_TYPE_VARBIT;
    default:
      return CCI_U_TYPE_UNKNOWN;
    }
}

static int
set_metadata_info (T_SRV_HANDLE * srv_handle, T_NET_BUF * net_buf)
{
  int count, i, ret;
  char type;
  sb1 scale, ptype, null;
  sb2 prec;
  ub4 attr_size, attr_schm_size;
  ub4 char_semantics;
  text *attr_name, *attr_schm_name, tmp[ORA_BUFSIZ];
  OCIParam *col;
  void **data;
  DB_VALUE *columns;

  ORA_PARAM_COUNT (srv_handle->session, count);
  net_buf_cp_byte (net_buf, 0);	/* updatable_flag */
  net_buf_cp_int (net_buf, count, NULL);

  columns = (DB_VALUE *) calloc (count, sizeof (DB_VALUE));
  if (columns == NULL)
    {
      return ERROR_INFO_SET (CAS_ER_NO_MORE_MEMORY, CAS_ERROR_INDICATOR);
    }
  memset ((void *) columns, 0, count * sizeof (DB_VALUE));

  srv_handle->q_result = MALLOC (sizeof (T_QUERY_RESULT));
  if (srv_handle->q_result == NULL)
    {
      FREE_MEM (columns);
      return ERROR_INFO_SET (CAS_ER_NO_MORE_MEMORY, CAS_ERROR_INDICATOR);
    }
  memset ((void *) srv_handle->q_result, 0, sizeof (T_QUERY_RESULT));
  srv_handle->q_result->column_count = count;
  srv_handle->q_result->columns = columns;

  /* TODO: error check all OCI call */
  for (i = 0; i < count; i++)
    {
      ret = OCIParamGet (srv_handle->session, OCI_HTYPE_STMT, ORA_ERR,
			 (void **) &col, i + 1);
      GOTO_ORA_ERROR (ret, oracle_error);
      ret = OCIAttrGet (col, OCI_DTYPE_PARAM, &scale, 0, OCI_ATTR_SCALE,
			ORA_ERR);
      GOTO_ORA_ERROR (ret, oracle_error);
      ret = OCIAttrGet (col, OCI_DTYPE_PARAM, &prec, 0, OCI_ATTR_PRECISION,
			ORA_ERR);
      GOTO_ORA_ERROR (ret, oracle_error);
      ret = OCIAttrGet (col, OCI_DTYPE_PARAM, &attr_name, &attr_size,
			OCI_ATTR_NAME, ORA_ERR);
      GOTO_ORA_ERROR (ret, oracle_error);
      ret = OCIAttrGet (col, OCI_DTYPE_PARAM, &columns[i].db_type, 0,
			OCI_ATTR_DATA_TYPE, ORA_ERR);
      GOTO_ORA_ERROR (ret, oracle_error);
      ret = OCIAttrGet (col, OCI_DTYPE_PARAM, &char_semantics, 0,
			OCI_ATTR_CHAR_USED, ORA_ERR);
      GOTO_ORA_ERROR (ret, oracle_error);
      if (char_semantics == 1)
	{
	  ret = OCIAttrGet (col, OCI_DTYPE_PARAM, &columns[i].size, 0,
			    OCI_ATTR_CHAR_SIZE, ORA_ERR);
	  GOTO_ORA_ERROR (ret, oracle_error);
	}
      else
	{
	  ret = OCIAttrGet (col, OCI_DTYPE_PARAM, &columns[i].size, 0,
			    OCI_ATTR_DATA_SIZE, ORA_ERR);
	  GOTO_ORA_ERROR (ret, oracle_error);
	}
      ret = OCIAttrGet (col, OCI_DTYPE_PARAM, &attr_schm_name,
			&attr_schm_size, OCI_ATTR_SCHEMA_NAME, ORA_ERR);
      GOTO_ORA_ERROR (ret, oracle_error);
      ret = OCIAttrGet (col, OCI_DTYPE_PARAM, &null, 0, OCI_ATTR_IS_NULL,
			ORA_ERR);
      GOTO_ORA_ERROR (ret, oracle_error);

      type = convert_data_type_oracle_to_cas (col);
      strncpy ((char *) tmp, (char *) attr_name, attr_size);
      tmp[attr_size] = 0;

      net_buf_column_info_set (net_buf, type, scale, prec, (char *) tmp);
      /* fprintf (stdout, "SN: %d, %s\n", size, tmp); */

      net_buf_cp_int (net_buf, 0, NULL);
      net_buf_cp_str (net_buf, "", 0);	/* attr_name */
      net_buf_cp_int (net_buf, attr_schm_size, NULL);
      net_buf_cp_str (net_buf, attr_schm_size == 0 ?	/* table name */
		      "" : (char *) attr_schm_name, attr_schm_size);
      net_buf_cp_byte (net_buf, null == 0);	/* is_non_null */
      /* 3.0 protocol */
      /* default value */
      net_buf_cp_int (net_buf, 1, NULL);
      net_buf_cp_byte (net_buf, '\0');
      net_buf_cp_byte (net_buf, '\0');	/* auto increment */
      net_buf_cp_byte (net_buf, '\0');	/* unique_key */
      net_buf_cp_byte (net_buf, '\0');	/* primary_key */
      net_buf_cp_byte (net_buf, '\0');	/* reverse_index */
      net_buf_cp_byte (net_buf, '\0');	/* reverse_unique */
      net_buf_cp_byte (net_buf, '\0');	/* foreign_key */
      net_buf_cp_byte (net_buf, '\0');	/* shared */

      /* make column data */
      switch (columns[i].db_type)
	{
	case SQLT_NUM:
	  columns[i].db_type = SQLT_VNU;
	  columns[i].size = sizeof (OCINumber);
	  data = (void **) &columns[i].data.number;
	  break;
	case SQLT_DAT:
	  columns[i].size = sizeof (OCIDate);
	  data = (void **) &columns[i].data.date;
	  break;
	case SQLT_TIMESTAMP:
	case SQLT_TIMESTAMP_TZ:
	case SQLT_TIMESTAMP_LTZ:
	  columns[i].data.p = NULL;
	  ret = OCIDescriptorAlloc (ORA_ENV, &columns[i].data.p,
				    OCI_DTYPE_TIMESTAMP, 0, 0);
	  GOTO_ORA_ERROR (ret, oracle_error);
	  columns[i].size = sizeof (OCIDateTime *);
	  columns[i].db_type = SQLT_TIMESTAMP;
	  data = &columns[i].data.p;
	  break;
	case SQLT_CLOB:
	case SQLT_BLOB:
	  ret = OCIDescriptorAlloc (ORA_ENV, &columns[i].data.p,
				    OCI_DTYPE_LOB, 0, 0);
	  GOTO_ORA_ERROR (ret, oracle_error);
	  data = &columns[i].data.p;
	  columns[i].size = -1;
	  break;
	case SQLT_CHR:
	case SQLT_VST:
	  columns[i].data.p = NULL;
	  ret = OCIStringResize (ORA_ENV, ORA_ERR, columns[i].size + 1,
				 (OCIString **) & columns[i].data.p);
	  GOTO_ORA_ERROR (ret, oracle_error);
	  columns[i].db_type = SQLT_VST;
	  columns[i].size = sizeof (OCIString *);
	  data = &columns[i].data.p;
	  break;
	case SQLT_LNG:
	  columns[i].size = MAX_CAS_BLOB_SIZE;
	  columns[i].data.p = MALLOC (columns[i].size);
	  data = columns[i].data.p;
	  break;
	case SQLT_AFC:
	case SQLT_STR:
	  columns[i].size = columns[i].size + 1;
	  columns[i].db_type = SQLT_STR;
	  columns[i].data.p = MALLOC (columns[i].size);
	  data = columns[i].data.p;
	  break;
	default:
	  return ERROR_INFO_SET (CAS_ER_INTERNAL, CAS_ERROR_INDICATOR);
	}

      ret = OCIDefineByPos (srv_handle->session,
			    (OCIDefine **) & columns[i].define, ORA_ERR,
			    i + 1, data, columns[i].size, columns[i].db_type,
			    &columns[i].is_null, 0, 0, OCI_DEFAULT);
      GOTO_ORA_ERROR (ret, oracle_error);
    }

  return CAS_NO_ERROR;

oracle_error:
  cas_oracle_get_errno ();
  return ret;
}

static char *
change_placeholder (char *sql_stmt)
{
  static char *buffer = 0;
  static int count = 0;
  char *p = sql_stmt;
  char *t;
  bool in_quato = false;
  int bind_count = 1;
  int len;

  len = strlen (sql_stmt);
  if ((count * ORA_BUFSIZ) < (len * 7))
    {
      count = ((len * 8) / ORA_BUFSIZ) + 1;
      buffer = (char *) realloc (buffer, ORA_BUFSIZ * count);
    }
  t = buffer;

  while (*p != 0)
    {
      if (*p == 0x22 || *p == 0x27)	/* " and ' */
	{
	  in_quato = !in_quato;
	}

      if (*p == 0x3F && !in_quato)	/* ? */
	{
	  sprintf (t, ":C4O%03d", bind_count++);
	  t += 7;
	  p++;
	  continue;
	}

      *t = *p;
      t++;
      p++;
    }
  *t = 0;

  return buffer;
}

int
ux_prepare (char *sql_stmt, int flag, char auto_commit_mode,
	    T_NET_BUF * net_buf, T_REQ_INFO * req_info,
	    unsigned int query_seq_num)
{
  T_SRV_HANDLE *srv_handle = NULL;
  OCIStmt *stmt;
  int srv_h_id;
  int err_code;
  int num_markers = 0;
  T_PREPARE_CALL_INFO *prepare_call_info;
  ub4 size;

  srv_h_id = hm_new_srv_handle (&srv_handle, query_seq_num);
  if (srv_h_id < 0)
    {
      err_code = srv_h_id;
      goto prepare_error;
    }
  srv_handle->schema_type = -1;
  srv_handle->auto_commit_mode = auto_commit_mode;

  sql_stmt = change_placeholder (sql_stmt);
  ALLOC_COPY (srv_handle->sql_stmt, sql_stmt);
  if (srv_handle->sql_stmt == NULL)
    {
      err_code = ERROR_INFO_SET (CAS_ER_NO_MORE_MEMORY, CAS_ERROR_INDICATOR);
      goto prepare_error;
    }

  err_code = OCIHandleAlloc (ORA_ENV, (void **) &stmt, OCI_HTYPE_STMT, 0, 0);
  GOTO_ORA_ERROR (err_code, prepare_error);
  err_code = OCIStmtPrepare (stmt, ORA_ERR, (text *) sql_stmt,
			     strlen (sql_stmt), OCI_NTV_SYNTAX, OCI_DEFAULT);
  GOTO_ORA_ERROR (err_code, prepare_error);
  err_code = ORA_STMT_TYPE (stmt, srv_handle->stmt_type);
  GOTO_ORA_ERROR (err_code, prepare_error);

  switch (srv_handle->stmt_type)
    {
    case OCI_STMT_SELECT:
      err_code = OCIStmtExecute (ORA_SVC, stmt, ORA_ERR, 1, 0, 0, 0,
				 OCI_DESCRIBE_ONLY);
      GOTO_ORA_ERROR (err_code, prepare_error);
      break;
    case OCI_STMT_UPDATE:
    case OCI_STMT_DELETE:
    case OCI_STMT_INSERT:
    case OCI_STMT_CREATE:
    case OCI_STMT_DROP:
    case OCI_STMT_ALTER:
    case OCI_STMT_BEGIN:
    case OCI_STMT_DECLARE:
      break;
    }
  err_code = ORA_BIND_COUNT (stmt, num_markers);
  GOTO_ORA_ERROR (err_code, prepare_error);

  srv_handle->is_prepared = TRUE;
  srv_handle->prepare_flag = flag;

  net_buf_cp_int (net_buf, srv_h_id, NULL);
  net_buf_cp_int (net_buf, -1, NULL);	/* result_cache_lifetime */
  convert_stmt_type_oracle_to_cubrid (srv_handle);
  net_buf_cp_byte (net_buf, srv_handle->stmt_type);
  net_buf_cp_int (net_buf, num_markers, NULL);
  prepare_call_info = make_prepare_call_info (num_markers);
  if (prepare_call_info == NULL)
    {
      err_code = ERROR_INFO_SET (CAS_ER_NO_MORE_MEMORY, CAS_ERROR_INDICATOR);
      goto prepare_error_internal;
    }

  srv_handle->session = (void *) stmt;
  if (srv_handle->stmt_type == CUBRID_STMT_SELECT)
    {
      err_code = set_metadata_info (srv_handle, net_buf);
      if (err_code != CAS_NO_ERROR)
	{
	  goto prepare_error_internal;
	}
    }
  else
    {
      net_buf_cp_byte (net_buf, 1);	/* updatable flag */
      net_buf_cp_int (net_buf, 0, NULL);
    }
  srv_handle->prepare_call_info = prepare_call_info;
  return srv_h_id;

prepare_error:
  cas_oracle_get_errno ();
prepare_error_internal:
  NET_BUF_ERR_SET (net_buf);
  if (auto_commit_mode == true)
    {
      req_info->need_auto_commit = TRAN_AUTOROLLBACK;
    }
  errors_in_transaction++;

  if (srv_handle)
    {
      hm_srv_handle_free (srv_h_id);
    }
  return err_code;
}

static void
get_date_from_ora_date (char *ora_date, sb2 * year, ub1 * month, ub1 * day)
{
  unsigned char cc = (unsigned char) abs (ora_date[0] - 100);
  unsigned char yy = (unsigned char) abs (ora_date[1] - 100);
  *month = ora_date[2];
  *day = ora_date[3];
  *year = (sb2) cc *100 + (sb2) yy;
}

static void
get_time_from_ora_date (char *ora_date, ub1 * hour, ub1 * min, ub1 * sec)
{
  *hour = ora_date[4] - 1;
  *min = ora_date[5] - 1;
  *sec = ora_date[6] - 1;;
}

static void
set_date_to_ora_date (char *ora_date, sb2 year, ub1 month, ub1 day)
{
  unsigned char cc = (unsigned char) (year / 100);
  unsigned char yy = (unsigned char) (year - year / 100 * 100);
  ora_date[0] = (ub1) abs (cc + 100);
  ora_date[1] = (ub1) abs (yy + 100);
  ora_date[2] = month;
  ora_date[3] = day;
}

static void
set_time_to_ora_date (char *ora_date, ub1 hour, ub1 min, ub1 sec)
{
  ora_date[4] = hour + 1;
  ora_date[5] = min + 1;
  ora_date[6] = sec + 1;;
}

static int
netval_to_oraval (void *net_type, void *net_value, DB_VALUE * out_val)
{
  char cas_type;
  int data_size;
  char *data = NULL;

  NET_ARG_GET_CHAR (cas_type, net_type);
  NET_ARG_GET_SIZE (data_size, net_value);

  out_val->need_clear = false;
  switch (cas_type)
    {
    case CCI_U_TYPE_NULL:
      db_make_null (out_val);
      break;
    case CCI_U_TYPE_CHAR:
    case CCI_U_TYPE_STRING:
      {
	char *val;
	NET_ARG_GET_STR (val, out_val->size, net_value);
	data = MALLOC (out_val->size);
	strncpy (data, val, out_val->size);

	out_val->db_type = SQLT_STR;
	out_val->data.p = data;
	out_val->buf = (void *) out_val->data.p;
	out_val->need_clear = true;
	break;
      }
    case CCI_U_TYPE_INT:
      {
	int *val = &(out_val->data.i);
	NET_ARG_GET_INT (*val, net_value);

	out_val->size = sizeof (int);
	out_val->db_type = SQLT_INT;
	out_val->buf = (void *) &(out_val->data.i);
	break;
      }
    case CCI_U_TYPE_SHORT:
      {
	short *val = &(out_val->data.sh);
	NET_ARG_GET_SHORT (*val, net_value);
	out_val->size = sizeof (short);
	out_val->db_type = SQLT_INT;
	out_val->buf = (void *) &(out_val->data.sh);
	break;
      }
    case CCI_U_TYPE_FLOAT:
      {
	float *val = &(out_val->data.f);
	NET_ARG_GET_FLOAT (*val, net_value);
	out_val->size = sizeof (float);
	out_val->db_type = SQLT_BFLOAT;
	out_val->buf = (void *) &(out_val->data.f);
	break;
      }
    case CCI_U_TYPE_DOUBLE:
      {
	double *val = &(out_val->data.d);
	NET_ARG_GET_DOUBLE (*val, net_value);
	out_val->size = sizeof (double);
	out_val->db_type = SQLT_BDOUBLE;
	out_val->buf = (void *) &(out_val->data.d);
	break;
      }
    case CCI_U_TYPE_DATE:
      {
	OCIDate *val = &(out_val->data.date);
	int year, month, day;
	NET_ARG_GET_DATE (year, month, day, net_value);
	out_val->size = 7;
	out_val->db_type = SQLT_DAT;
	set_date_to_ora_date ((char *) val, year, month, day);
	set_time_to_ora_date ((char *) val, 0, 0, 0);
	out_val->buf = (void *) &(out_val->data.date);
	break;
      }
    case CCI_U_TYPE_TIME:
      {
	OCIDate *val = &(out_val->data.date);
	int hh, mm, ss;
	NET_ARG_GET_TIME (hh, mm, ss, net_value);
	out_val->size = 7;
	out_val->db_type = SQLT_DAT;
	set_date_to_ora_date ((char *) val, 0, 0, 0);
	set_time_to_ora_date ((char *) val, hh, mm, ss);
	out_val->buf = (void *) &(out_val->data.date);
	break;
      }
    case CCI_U_TYPE_TIMESTAMP:
      {
	OCIDate *val = &(out_val->data.date);
	int year, month, day, hh, mm, ss;
	NET_ARG_GET_TIMESTAMP (year, month, day, hh, mm, ss, net_value);
	out_val->size = 7;
	out_val->db_type = SQLT_DAT;
	OCIDateSetDate (val, year, month, day);
	OCIDateSetTime (val, hh, mm, ss);
	out_val->buf = (void *) &(out_val->data.date);
	break;
      }
    case CCI_U_TYPE_DATETIME:
      {
	OCIDate *val = &(out_val->data.date);
	int year, month, day, hh, mm, ss, ms;
	NET_ARG_GET_DATETIME (year, month, day, hh, mm, ss, ms, net_value);
	out_val->size = 7;
	out_val->db_type = SQLT_DAT;
	OCIDateSetDate (val, year, month, day);
	OCIDateSetTime (val, hh, mm, ss);
	out_val->buf = (void *) &(out_val->data.date);
	break;
      }
      break;
    case CCI_U_TYPE_BIT:
    case CCI_U_TYPE_VARBIT:
      {
	char *value;
	NET_ARG_GET_STR (value, out_val->size, net_value);
	out_val->db_type = SQLT_BIN;
	data = (char *) MALLOC (out_val->size);
	if (data == NULL)
	  {
	    return ERROR_INFO_SET (CAS_ER_NO_MORE_MEMORY,
				   CAS_ERROR_INDICATOR);
	  }
	memcpy (data, value, out_val->size);
	out_val->data.p = data;
	out_val->buf = (void *) out_val->data.p;
	out_val->need_clear = true;
	break;
      }
    case CCI_U_TYPE_NUMERIC:
      {
	OCINumber *val = &(out_val->data.number);
	char *value;
	int length, i;
	char fmt[BUFSIZ];
	NET_ARG_GET_STR (value, length, net_value);
	for (i = 0; i < length; i++)
	  {
	    fmt[i] = value[i];
	    if (fmt[i] >= 0 && fmt[i] <= 9)
	      fmt[i] = '9';
	  }
	OCINumberFromText (ORA_ERR, (text *) value, length, (text *) fmt,
			   length, NULL, 0, val);
	out_val->db_type = SQLT_NUM;
	out_val->size = sizeof (OCINumber);
	out_val->buf = (void *) &(out_val->data.number);
	break;
      }
    case CCI_U_TYPE_BIGINT:
      {
	data = (char *) MALLOC (OCI_NUMBER_SIZE);
	int ret;
	sb8 bi_val;
	NET_ARG_GET_BIGINT (bi_val, net_value);
	out_val->db_type = SQLT_STR;
	snprintf (data, OCI_NUMBER_SIZE, "%ld", bi_val);
	out_val->size = strlen (data) + 1;
	out_val->data.p = data;
	out_val->buf = (void *) out_val->data.p;
	out_val->need_clear = true;
	break;
      }
    case CCI_U_TYPE_MONETARY:
    case CCI_U_TYPE_NCHAR:
    case CCI_U_TYPE_VARNCHAR:
    case CCI_U_TYPE_SET:
    case CCI_U_TYPE_MULTISET:
    case CCI_U_TYPE_SEQUENCE:
    case CCI_U_TYPE_OBJECT:
    default:
      return OCI_ERROR;
    }
  return OCI_SUCCESS;
}

int
ux_execute_all (T_SRV_HANDLE * srv_handle, char flag, int max_col_size,
		int max_row, int argc, void **argv, T_NET_BUF * net_buf,
		T_REQ_INFO * req_info, CACHE_TIME * clt_cache_time,
		int *clt_cache_reusable)
{
  return ux_execute (srv_handle, flag, max_col_size, max_row, argc, argv,
		     net_buf, req_info, clt_cache_time, clt_cache_reusable);
}

int
ux_execute (T_SRV_HANDLE * srv_handle, char flag, int max_col_size,
	    int max_row, int argc, void **argv, T_NET_BUF * net_buf,
	    T_REQ_INFO * req_info, CACHE_TIME * clt_cache_time,
	    int *clt_cache_reusable)
{
  OCIStmt *stmt;
  OCIBind **bind;
  ub4 i, bind_count, row_count, iters, mode;
  int ret;
  ub2 type;
  size_t size;
  DB_VALUE **out_vals;
  T_OBJECT ins_oid;
  T_PREPARE_CALL_INFO *call_info;

  hm_qresult_end (srv_handle, FALSE);
  stmt = (OCIStmt *) srv_handle->session;
  call_info = srv_handle->prepare_call_info;

  bind_count = call_info->num_args;
  if (bind_count > 0)
    {
      bind = (OCIBind **) call_info->bind;
      out_vals = (DB_VALUE **) call_info->dbval_args;
      if ((bind == NULL) || (out_vals == NULL))
	{
	  ret = ERROR_INFO_SET (CAS_ER_INTERNAL, CAS_ERROR_INDICATOR);
	  goto execute_error_internal;
	}
    }

  for (i = 0; i < bind_count; i++)
    {
      ret = netval_to_oraval (argv[2 * i], argv[2 * i + 1], out_vals[i]);
      GOTO_ORA_ERROR (ret, execute_error);
      ret = OCIBindByPos (stmt, &bind[i], ORA_ERR, i + 1,
			  (void *) out_vals[i]->buf, out_vals[i]->size,
			  out_vals[i]->db_type, 0, 0, 0, 0, 0, OCI_DEFAULT);
      GOTO_ORA_ERROR (ret, execute_error);
    }

  SQL_LOG2_EXEC_BEGIN (as_info->cur_sql_log2, 1);
  if (srv_handle->stmt_type == CUBRID_STMT_SELECT)
    {
      iters = 0;
      mode = OCI_DEFAULT;
    }
  else
    {
      iters = 1;
      mode = OCI_DEFAULT;
    }
  ret = OCIStmtExecute (ORA_SVC, stmt, ORA_ERR, iters, 0, 0, 0, mode);
  as_info->num_queries_processed %= MAX_DIAG_DATA_VALUE;
  as_info->num_queries_processed++;
  SQL_LOG2_EXEC_END (as_info->cur_sql_log2, 1, ret);
  GOTO_ORA_ERROR (ret, execute_error);

  if (srv_handle->stmt_type == CUBRID_STMT_SELECT)
    {
      srv_handle->is_no_data = false;
      row_count = INT_MAX;
    }
  else
    {
      ret = ORA_ROW_COUNT (stmt, row_count);
    }

  net_buf_cp_int (net_buf, row_count, NULL);
  net_buf_cp_byte (net_buf, 0);
  net_buf_cp_int (net_buf, 1, NULL);	/* num_q_result */
  net_buf_cp_byte (net_buf, srv_handle->stmt_type);
  net_buf_cp_int (net_buf, row_count, &_offset_row_count);
  memset (&ins_oid, 0, sizeof (T_OBJECT));
  NET_BUF_CP_OBJECT (net_buf, &ins_oid);
  net_buf_cp_int (net_buf, 0, NULL);	/* cache time sec */
  net_buf_cp_int (net_buf, 0, NULL);	/* cache time usec */

  srv_handle->max_row = max_row;
  srv_handle->max_col_size = max_col_size;
  srv_handle->tuple_count = row_count;
  for (i = 0; i < bind_count; i++)
    {
      db_value_clear (out_vals[i]);
    }
  return ret;

execute_error:
  cas_oracle_get_errno ();
execute_error_internal:
  NET_BUF_ERR_SET (net_buf);
  if (srv_handle->auto_commit_mode)
    {
      req_info->need_auto_commit = TRAN_AUTOROLLBACK;
    }
  errors_in_transaction++;
  for (i = 0; i < bind_count; i++)
    {
      db_value_clear (out_vals[i]);
    }
  return ret;
}

int
ux_execute_call (T_SRV_HANDLE * srv_handle, char flag, int max_col_size,
		 int max_row, int argc, void **argv, T_NET_BUF * net_buf,
		 T_REQ_INFO * req_info, CACHE_TIME * clt_cache_time,
		 int *clt_cache_reusable)
{
  return ux_execute (srv_handle, flag, max_col_size, max_row, argc, argv,
		     net_buf, req_info, clt_cache_time, clt_cache_reusable);
}

#ifndef LIBCAS_FOR_JSP
static bool
check_auto_commit_after_fetch_done (T_SRV_HANDLE * srv_handle)
{
  if (((srv_handle->stmt_type == CUBRID_STMT_SELECT
	&& srv_handle->auto_commit_mode == TRUE
	&& srv_handle->forward_only_cursor == TRUE)
       || (shm_appl->select_auto_commit == ON
	   && hm_srv_handle_is_all_active_fetch_completed () == true)))
    {
      return true;
    }
  return false;
}
#endif /* !LIBCAS_FOR_JSP */

static void
add_res_data_bytes (T_NET_BUF * net_buf, char *str, int size, char type)
{
  if (type)
    {
      net_buf_cp_int (net_buf, size + 1, NULL);	/* type */
      net_buf_cp_byte (net_buf, type);
    }
  else
    {
      net_buf_cp_int (net_buf, size, NULL);
    }
  net_buf_cp_str (net_buf, str, size);
  /* do not append NULL terminator */
}

static void
add_res_data_string (T_NET_BUF * net_buf, const char *str, int size,
		     char type)
{
  if (type)
    {
      net_buf_cp_int (net_buf, size + 1 + 1, NULL);	/* type, NULL terminator */
      net_buf_cp_byte (net_buf, type);
    }
  else
    {
      net_buf_cp_int (net_buf, size + 1, NULL);	/* NULL terminator */
    }
  net_buf_cp_str (net_buf, str, size);
  net_buf_cp_byte (net_buf, '\0');
}

static void
add_res_data_int (T_NET_BUF * net_buf, int value, char type)
{
  if (type)
    {
      net_buf_cp_int (net_buf, 5, NULL);
      net_buf_cp_byte (net_buf, type);
      net_buf_cp_int (net_buf, value, NULL);
    }
  else
    {
      net_buf_cp_int (net_buf, 4, NULL);
      net_buf_cp_int (net_buf, value, NULL);
    }
}

static void
add_res_data_bigint (T_NET_BUF * net_buf, DB_BIGINT value, char type)
{
  if (type)
    {
      net_buf_cp_int (net_buf, 9, NULL);
      net_buf_cp_byte (net_buf, type);
      net_buf_cp_bigint (net_buf, value, NULL);
    }
  else
    {
      net_buf_cp_int (net_buf, 8, NULL);
      net_buf_cp_bigint (net_buf, value, NULL);
    }
}

static void
add_res_data_short (T_NET_BUF * net_buf, short value, char type)
{
  if (type)
    {
      net_buf_cp_int (net_buf, 3, NULL);
      net_buf_cp_byte (net_buf, type);
      net_buf_cp_short (net_buf, value);
    }
  else
    {
      net_buf_cp_int (net_buf, 2, NULL);
      net_buf_cp_short (net_buf, value);
    }
}

static void
add_res_data_float (T_NET_BUF * net_buf, float value, char type)
{
  if (type)
    {
      net_buf_cp_int (net_buf, 5, NULL);
      net_buf_cp_byte (net_buf, type);
      net_buf_cp_float (net_buf, value);
    }
  else
    {
      net_buf_cp_int (net_buf, 4, NULL);
      net_buf_cp_float (net_buf, value);
    }
}

static void
add_res_data_double (T_NET_BUF * net_buf, double value, char type)
{
  if (type)
    {
      net_buf_cp_int (net_buf, 9, NULL);
      net_buf_cp_byte (net_buf, type);
      net_buf_cp_double (net_buf, value);
    }
  else
    {
      net_buf_cp_int (net_buf, 8, NULL);
      net_buf_cp_double (net_buf, value);
    }
}

static void
add_res_data_timestamp (T_NET_BUF * net_buf, short yr, short mon, short day,
			short hh, short mm, short ss, char type)
{
  if (type)
    {
      net_buf_cp_int (net_buf, CAS_TIMESTAMP_SIZE + 1, NULL);
      net_buf_cp_byte (net_buf, type);
    }
  else
    {
      net_buf_cp_int (net_buf, CAS_TIMESTAMP_SIZE, NULL);
    }

  net_buf_cp_short (net_buf, yr);
  net_buf_cp_short (net_buf, mon);
  net_buf_cp_short (net_buf, day);
  net_buf_cp_short (net_buf, hh);
  net_buf_cp_short (net_buf, mm);
  net_buf_cp_short (net_buf, ss);
}

static void
add_res_data_datetime (T_NET_BUF * net_buf, short yr, short mon, short day,
		       short hh, short mm, short ss, short ms, char type)
{
  if (type)
    {
      net_buf_cp_int (net_buf, CAS_DATETIME_SIZE + 1, NULL);
      net_buf_cp_byte (net_buf, type);
    }
  else
    {
      net_buf_cp_int (net_buf, CAS_DATETIME_SIZE, NULL);
    }

  net_buf_cp_short (net_buf, yr);
  net_buf_cp_short (net_buf, mon);
  net_buf_cp_short (net_buf, day);
  net_buf_cp_short (net_buf, hh);
  net_buf_cp_short (net_buf, mm);
  net_buf_cp_short (net_buf, ss);
  net_buf_cp_short (net_buf, ms);
}

static void
add_res_data_time (T_NET_BUF * net_buf, short hh, short mm, short ss,
		   char type)
{
  if (type)
    {
      net_buf_cp_int (net_buf, SIZE_TIME + 1, NULL);
      net_buf_cp_byte (net_buf, type);
    }
  else
    {
      net_buf_cp_int (net_buf, SIZE_TIME, NULL);
    }

  net_buf_cp_short (net_buf, hh);
  net_buf_cp_short (net_buf, mm);
  net_buf_cp_short (net_buf, ss);
}

static void
add_res_data_date (T_NET_BUF * net_buf, short yr, short mon, short day,
		   char type)
{
  if (type)
    {
      net_buf_cp_int (net_buf, SIZE_DATE + 1, NULL);
      net_buf_cp_byte (net_buf, type);
    }
  else
    {
      net_buf_cp_int (net_buf, SIZE_DATE, NULL);
    }
  net_buf_cp_short (net_buf, yr);
  net_buf_cp_short (net_buf, mon);
  net_buf_cp_short (net_buf, day);
}

static void
add_res_data_object (T_NET_BUF * net_buf, T_OBJECT * obj, char type)
{
  if (type)
    {
      net_buf_cp_int (net_buf, SIZE_OBJECT + 1, NULL);
      net_buf_cp_byte (net_buf, type);
    }
  else
    {
      net_buf_cp_int (net_buf, SIZE_OBJECT, NULL);
    }

  NET_BUF_CP_OBJECT (net_buf, obj);
}

static int
ora_value_to_net_buf (void *value, int type, bool null, int size,
		      T_NET_BUF * net_buf)
{
  DB_DATA *v = (DB_DATA *) value;
  if (null == true)
    {
      net_buf_cp_int (net_buf, -1, NULL);
      return 4;
    }

  switch (type)
    {
    case SQLT_LNG:
      size = strlen (v->p);
    case SQLT_AFC:
    case SQLT_STR:
      add_res_data_bytes (net_buf, v->p, size, 0);
      memset (v->p, 0, size);
      return 4 + size + 1;
    case SQLT_CHR:
    case SQLT_VST:
      {
	OCIString *ocistr = (OCIString *) v->p;
	size = OCIStringSize (ORA_ENV, ocistr);
	add_res_data_string (net_buf, (char *) OCIStringPtr (ORA_ENV, ocistr),
			     size, 0);
	return 4 + size + 1;
      }
    case SQLT_TIMESTAMP:
    case SQLT_TIMESTAMP_TZ:
    case SQLT_TIMESTAMP_LTZ:
      {
	sb2 yy;
	ub1 mm, dd, hh, mi, ss;
	ub4 fs;
	int ret;
	OCIDateTime *dt = (OCIDateTime *) v->p;
	ret = OCIDateTimeGetDate (ORA_ENV, ORA_ERR, dt, &yy, &mm, &dd);
	ret = OCIDateTimeGetTime (ORA_ENV, ORA_ERR, dt, &hh, &mi, &ss, &fs);
	add_res_data_timestamp (net_buf, yy, mm, dd, hh, mi, ss, 0);
	return 4 + CAS_TIMESTAMP_SIZE;
      }
    case SQLT_DAT:
      {
	sb2 yy;
	ub1 mm, dd;
	get_date_from_ora_date (value, &yy, &mm, &dd);
	add_res_data_date (net_buf, yy, mm, dd, 0);
	return 4 + SIZE_DATE;
      }
    case SQLT_NUM:
    case SQLT_VNU:
      {
	/* TODO: don't work */
	OCINumber *number = (OCINumber *) value;
	text buf[128];
	text fmt[] = "FM99999999999999999999D99999999999999999999";
	ub4 buf_size = 128;
	int ret;
	memset (buf, 0, 128);
	ret = OCINumberToText (ORA_ERR, number, fmt, 43, 0, 0, &buf_size,
			       buf);
	if (ret == -1)
	  {
	    cas_oracle_get_errmsg ();
	    net_buf_cp_int (net_buf, -1, NULL);
	    return 4;
	  }
	else
	  {
	    buf_size = strlen ((char *) buf);
	    if (buf[buf_size - 1] == '.')
	      {
		buf[buf_size - 1] = 0;
		buf_size--;
	      }
	    add_res_data_string (net_buf, (char *) buf, buf_size, 0);
	    return 4 + buf_size + 1;
	  }
      }
    case SQLT_CLOB:
    case SQLT_BLOB:
      {
	OCILobLocator *lob = (OCILobLocator *) v->p;
	ub4 len, amtp;
	ub1 *buf;

	OCILobGetLength (ORA_SVC, ORA_ERR, lob, &len);
	if (len == 0)
	  {
	    cas_oracle_get_errmsg ();
	    net_buf_cp_int (net_buf, -1, NULL);
	    return 4;
	  }

	amtp = len;
	buf = (ub1 *) MALLOC (len);
	if (buf == NULL)
	  {
	    ERROR_INFO_SET (CAS_ER_NO_MORE_MEMORY, CAS_ERROR_INDICATOR);
	    net_buf_cp_int (net_buf, -1, NULL);
	    return 4;
	  }
	OCILobRead (ORA_SVC, ORA_ERR, lob, &amtp, 1, buf, len, NULL, NULL, 0,
		    SQLCS_IMPLICIT);
	add_res_data_string (net_buf, (char *) buf, len, 0);
	return 4 + len + 1;
      }
    default:
      net_buf_cp_int (net_buf, -1, NULL);
      return 4;
    }
}

static int
net_buf_cp_oracle_row (T_SRV_HANDLE * srv_handle, T_NET_BUF * net_buf,
		       OCIStmt * stmt)
{
  T_QUERY_RESULT *q_result = (T_QUERY_RESULT *) srv_handle->q_result;
  DB_VALUE *columns = q_result->columns;
  void *value;
  int i, type, size;
  signed short null;
  int data_size = 0;

  for (i = 0; i < q_result->column_count; i++)
    {
      type = columns[i].db_type;
      null = columns[i].is_null;
      size = columns[i].size;
      value = &columns[i].data;
      data_size +=
	ora_value_to_net_buf (value, type, (null != 0), size, net_buf);
    }
  return data_size;
}

int
ux_fetch (T_SRV_HANDLE * srv_handle, int cursor_pos, int fetch_count,
	  char fetch_flag, int result_set_index, T_NET_BUF * net_buf,
	  T_REQ_INFO * req_info)
{
  OCIStmt *stmt;
  int ret, tuple, num_tuple_msg_offset;
  T_OBJECT tuple_obj;
  bool first_flag;

  net_buf_cp_int (net_buf, 0, NULL);	/* result code */
  if (fetch_count <= 0)
    {
      fetch_count = 100;
    }

  if (srv_handle->q_result == NULL
      || srv_handle->stmt_type != CUBRID_STMT_SELECT
      || srv_handle->is_no_data)
    {
      ERROR_INFO_SET (CAS_ER_NO_MORE_DATA, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (net_buf);
      return ret;
    }

  stmt = srv_handle->session;
  tuple = 0;
  net_buf_cp_int (net_buf, tuple, &num_tuple_msg_offset);
  memset (&tuple_obj, 0, sizeof (T_OBJECT));
  first_flag = (cursor_pos == 1);

  while (tuple < fetch_count && CHECK_NET_BUF_SIZE (net_buf))
    {
      ret = OCIStmtFetch2 (stmt, ORA_ERR, 1, OCI_DEFAULT, 0, OCI_DEFAULT);
      if (ret == OCI_NO_DATA)
	{
	  if (shm_appl->select_auto_commit == ON)
	    {
	      hm_srv_handle_set_fetch_completed (srv_handle);
	    }
	  if (check_auto_commit_after_fetch_done (srv_handle) == true)
	    {
	      req_info->need_auto_commit = TRAN_AUTOCOMMIT;
	    }
	  srv_handle->is_no_data = true;
	  if (first_flag)
	    {
	      net_buf_overwrite_int (net_buf, 0, tuple);
	      net_buf_overwrite_int (net_buf, _offset_row_count, tuple);
	      srv_handle->tuple_count = tuple;
	    }
	  else if (tuple == 0)
	    {
	      ERROR_INFO_SET (CAS_ER_NO_MORE_DATA, CAS_ERROR_INDICATOR);
	      NET_BUF_ERR_SET (net_buf);
	      return ret;
	    }
	  break;
	}
      else if (!ORA_SUCCESS (ret))
	{
	  cas_oracle_get_errno ();
	  NET_BUF_ERR_SET (net_buf);
	  return ret;
	}
      net_buf_cp_int (net_buf, cursor_pos, NULL);
      NET_BUF_CP_OBJECT (net_buf, &tuple_obj);
      net_buf_cp_oracle_row (srv_handle, net_buf, stmt);
      cursor_pos++;
      tuple++;
    }
  net_buf_overwrite_int (net_buf, num_tuple_msg_offset, tuple);
  return ret;
}

int
ux_get_db_version (T_NET_BUF * net_buf, T_REQ_INFO * req_info)
{
  char *p;

  p = (char *) makestring (BUILD_NUMBER);
  net_buf_cp_int (net_buf, 0, NULL);
  net_buf_cp_str (net_buf, p, strlen (p) + 1);

  return 0;
}

void
ux_free_result (void *result)
{
  T_QUERY_RESULT *r = (T_QUERY_RESULT *) result;
  int i;

  if (r == NULL)
    {
      return;
    }

  for (i = 0; i < r->column_count; i++)
    {
      if (r->columns[i].data.p)
	{
	  switch (r->columns[i].db_type)
	    {
	    case SQLT_TIMESTAMP:
	    case SQLT_TIMESTAMP_TZ:
	    case SQLT_TIMESTAMP_LTZ:
	      {
		OCIDescriptorFree (r->columns[i].data.p, OCI_DTYPE_TIMESTAMP);
		break;
	      }
	    case SQLT_CLOB:
	    case SQLT_BLOB:
	      {
		OCIDescriptorFree (r->columns[i].data.p, OCI_DTYPE_LOB);
		break;
	      }
	    case SQLT_CHR:
	    case SQLT_VST:
	      OCIStringResize (ORA_ENV, ORA_ERR, 0,
			       (OCIString **) & r->columns[i].data.p);
	      break;
	    case SQLT_LNG:
	    case SQLT_AFC:
	    case SQLT_STR:
	      {
		FREE_MEM (r->columns[i].data.p);
		break;
	      }
	    case SQLT_NUM:
	    case SQLT_VNU:
	    case SQLT_DAT:
	    default:
	      break;
	    }
	}
    }

  FREE_MEM (r->columns);
}

char
ux_db_type_to_cas_type (int db_type)
{
  /* TODO: for debug */
  printf ("%s\n", "ux_db_type_to_cas_type");
  return CCI_U_TYPE_UNKNOWN;
}

int
ux_auto_commit (T_NET_BUF * net_buf, T_REQ_INFO * req_info)
{
#ifndef LIBCAS_FOR_JSP
  int err_code;
  int elapsed_sec = 0, elapsed_msec = 0;

  if (req_info->need_auto_commit == TRAN_AUTOCOMMIT)
    {
      cas_log_write (0, false, "auto_commit");
      err_code = ux_end_tran (CCI_TRAN_COMMIT, true);
      cas_log_write (0, false, "auto_commit %d", err_code);
    }
  else if (req_info->need_auto_commit == TRAN_AUTOROLLBACK)
    {
      cas_log_write (0, false, "auto_rollback");
      err_code = ux_end_tran (CCI_TRAN_ROLLBACK, true);
      cas_log_write (0, false, "auto_rollback %d", err_code);
    }
  else
    {
      err_code = ERROR_INFO_SET (CAS_ER_INTERNAL, CAS_ERROR_INDICATOR);
    }

  if (err_code < 0)
    {
      NET_BUF_ERR_SET (net_buf);
      req_info->need_rollback = TRUE;
      errors_in_transaction++;
    }
  else
    {
      req_info->need_rollback = FALSE;
    }

  tran_timeout = ut_check_timeout (&tran_start_time,
				   shm_appl->long_transaction_time,
				   &elapsed_sec, &elapsed_msec);
  if (tran_timeout >= 0)
    {
      as_info->num_long_transactions %= MAX_DIAG_DATA_VALUE;
      as_info->num_long_transactions++;
    }
  if (err_code < 0 || errors_in_transaction > 0)
    {
      cas_log_end (SQL_LOG_MODE_ERROR, elapsed_sec, elapsed_msec);
      errors_in_transaction = 0;
    }
  else
    {
      if (tran_timeout >= 0 || query_timeout >= 0)
	{
	  cas_log_end (SQL_LOG_MODE_TIMEOUT, elapsed_sec, elapsed_msec);
	}
      else
	{
	  cas_log_end (SQL_LOG_MODE_NONE, elapsed_sec, elapsed_msec);
	}
    }
  gettimeofday (&tran_start_time, NULL);
  gettimeofday (&query_start_time, NULL);
  tran_timeout = 0;
  query_timeout = 0;

  if (as_info->cur_keep_con != KEEP_CON_OFF)
    {
      if (as_info->cas_log_reset)
	{
	  cas_log_reset (broker_name, shm_as_index);
	}
      if (!ux_is_database_connected () || restart_is_needed ())
	{
	  return -1;
	}

      if (shm_appl->sql_log2 != as_info->cur_sql_log2)
	{
	  sql_log2_end (false);
	  as_info->cur_sql_log2 = shm_appl->sql_log2;
	  sql_log2_init (broker_name, shm_as_index, as_info->cur_sql_log2,
			 true);
	}
      return 0;
    }
#endif /* !LIBCAS_FOR_JSP */

  return -1;
}

void
set_db_connect_status (int status)
{
  /* TODO: for debug */
  printf ("%s(%d)\n", "set_db_connect_status", status);
}

bool
is_server_alive (void)
{
  /* TODO */
  return true;
}

int
get_tuple_count (T_SRV_HANDLE * srv_handle)
{
  return srv_handle->tuple_count;
}

static void
db_make_null (DB_VALUE * value)
{
  memset ((char *) value, 0x00, sizeof (DB_VALUE));
  value->is_null = true;
}

static int
db_value_clear (DB_VALUE * value)
{
  if (value == NULL)
    {
      return ERROR_INFO_SET (CAS_ER_DB_VALUE, CAS_ERROR_INDICATOR);
    }
  if (value->need_clear)
    {
      FREE_MEM (value->data.p);
    }
  memset ((char *) value, 0x00, sizeof (DB_VALUE));
  return 0;
}

static T_PREPARE_CALL_INFO *
make_prepare_call_info (int num_args)
{
  T_PREPARE_CALL_INFO *call_info;
  OCIBind **bind = NULL;
  DB_VALUE **arg_val = NULL;
  char *param_mode = NULL;
  int i;

  call_info = (T_PREPARE_CALL_INFO *) MALLOC (sizeof (T_PREPARE_CALL_INFO));
  if (call_info == NULL)
    {
      return NULL;
    }

  memset (call_info, 0, sizeof (T_PREPARE_CALL_INFO));

  if (num_args > 0)
    {
      bind = (OCIBind **) MALLOC (sizeof (OCIBind *) * num_args);
      if (bind == NULL)
	{
	  goto make_prpare_call_info;
	}

      arg_val = (DB_VALUE **) MALLOC (sizeof (DB_VALUE *) * (num_args + 1));
      if (arg_val == NULL)
	{
	  goto make_prpare_call_info;
	}
      memset (arg_val, 0, sizeof (DB_VALUE *) * (num_args + 1));

      param_mode = (char *) MALLOC (sizeof (char) * num_args);
      if (param_mode == NULL)
	{
	  goto make_prpare_call_info;
	}

      for (i = 0; i < num_args; i++)
	{
	  arg_val[i] = (DB_VALUE *) MALLOC (sizeof (DB_VALUE));
	  if (arg_val[i] == NULL)
	    {
	      goto make_prpare_call_info;
	    }
	  db_make_null (arg_val[i]);
	  param_mode[i] = CCI_PARAM_MODE_UNKNOWN;
	}
    }

  call_info->dbval_ret = NULL;
  call_info->dbval_args = arg_val;
  call_info->num_args = num_args;
  call_info->bind = (void *) bind;
  call_info->param_mode = param_mode;

  return call_info;
make_prpare_call_info:
  FREE_MEM (call_info);
  FREE_MEM (bind);
  FREE_MEM (param_mode);
  if (arg_val != NULL)
    {
      for (i = 0; i < num_args; i++)
	{
	  FREE_MEM (arg_val[i]);
	}
      FREE_MEM (arg_val);
    }
  return NULL;
}

void
ux_prepare_call_info_free (T_PREPARE_CALL_INFO * call_info)
{
  DB_VALUE **args;

  if (call_info)
    {
      int i;

      FREE_MEM (call_info->dbval_ret);

      args = (DB_VALUE **) call_info->dbval_args;
      for (i = 0; i < call_info->num_args; i++)
	{
	  db_value_clear (args[i]);
	  FREE_MEM (((DB_VALUE **) args)[i]);
	}

      FREE_MEM (call_info->dbval_args);
      FREE_MEM (call_info->bind);
      FREE_MEM (call_info->param_mode);
      FREE_MEM (call_info);
    }
}
