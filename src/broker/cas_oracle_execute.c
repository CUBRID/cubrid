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

static ORACLE_INFO _db_info;

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
cas_oracle_get_errmsg ()
{
  static text buf[ORA_BUFSIZ];
  int ret, code;
  const char *fmt =
    "DBMS ERROR : BROKER_ERR_CODE [ERR_CODE : %d, ERR_MSG : %s]";

  ret = OCIErrorGet (ORA_ERR, 1, NULL, &code, buf, ORA_BUFSIZ,
		     OCI_HTYPE_ERROR);
  if (ORA_SUCCESS (ret))
    {
      int size = strlen ((char *) buf);
      if (buf[size - 1] == '\n')
	{
	  buf[size - 1] = 0;
	}
      cas_log_write (0, false, fmt, code, buf);
      ERROR_INFO_SET_WITH_MSG (code, DBMS_ERROR_INDICATOR, (char *) buf);
      return (const char *) buf;
    }
  return NULL;
}

static int
cas_oracle_get_errno ()
{
  text buf[ORA_BUFSIZ];
  int code, size;
  const char *fmt =
    "DBMS ERROR : BROKER_ERR_CODE [ERR_CODE : %d, ERR_MSG : %s]";

  OCIErrorGet (ORA_ERR, 1, NULL, &code, buf, ORA_BUFSIZ, OCI_HTYPE_ERROR);
  size = strlen ((char *) buf);
  if (buf[size - 1] == '\n')
    {
      buf[size - 1] = 0;
    }
  cas_log_write (0, false, fmt, code, buf);
  return ERROR_INFO_SET_WITH_MSG (code, DBMS_ERROR_INDICATOR, (char *) buf);
}

static int
cas_oracle_connect_db (char *tns, char *db_user, char *db_pass,
		       char **db_err_msg)
{
  int ret;
  const char *err_msg;

  ret = OCIInitialize (OCI_DEFAULT, 0, 0, 0, 0);
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
    }

  return err_code;
}

void
ux_database_shutdown ()
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
    case OCI_TYPECODE_NUMBER:
      return CCI_U_TYPE_NUMERIC;
    case OCI_TYPECODE_INTEGER:
      return CCI_U_TYPE_INT;
    case OCI_TYPECODE_SMALLINT:
      return CCI_U_TYPE_SHORT;
    case OCI_TYPECODE_REAL:
    case OCI_TYPECODE_BFLOAT:
      return CCI_U_TYPE_FLOAT;
    case OCI_TYPECODE_FLOAT:
    case OCI_TYPECODE_DOUBLE:
    case OCI_TYPECODE_BDOUBLE:
      return CCI_U_TYPE_DOUBLE;
    case OCI_TYPECODE_TIMESTAMP:
    case OCI_TYPECODE_TIMESTAMP_TZ:
    case OCI_TYPECODE_TIMESTAMP_LTZ:
      return CCI_U_TYPE_TIMESTAMP;
    case OCI_TYPECODE_DATE:
      return CCI_U_TYPE_DATE;
    case OCI_TYPECODE_CHAR:
      return CCI_U_TYPE_CHAR;
    case OCI_TYPECODE_VARCHAR:
    case OCI_TYPECODE_VARCHAR2:
      return CCI_U_TYPE_STRING;
    case OCI_TYPECODE_CLOB:
    case OCI_TYPECODE_BLOB:
      return CCI_U_TYPE_VARBIT;
    case OCI_TYPECODE_REF:
    case OCI_TYPECODE_INTERVAL_YM:
    case OCI_TYPECODE_INTERVAL_DS:
    case OCI_TYPECODE_OCTET:
    case OCI_TYPECODE_DECIMAL:
    case OCI_TYPECODE_RAW:
    case OCI_TYPECODE_VARRAY:
    case OCI_TYPECODE_TABLE:
    case OCI_TYPECODE_BFILE:
    case OCI_TYPECODE_OBJECT:
    case OCI_TYPECODE_NAMEDCOLLECTION:
    default:
      return CCI_U_TYPE_UNKNOWN;
    }
}

static void
set_metadata_info (T_SRV_HANDLE * srv_handle, T_NET_BUF * net_buf)
{
  int count, i, ret;
  char type;
  sb1 scale, ptype, null;
  sb2 prec;
  ub4 size;
  text *name;
  OCIParam *col;

  ORA_PARAM_COUNT (srv_handle->session, count);
  net_buf_cp_byte (net_buf, 0);	/* updatable_flag */
  net_buf_cp_int (net_buf, count, NULL);

  /* TODO: error check all OCI call */
  for (i = 1; i <= count; i++)
    {
      ret = OCIParamGet (srv_handle->session, OCI_HTYPE_STMT, ORA_ERR,
			 (void **) &col, i);
      type = convert_data_type_oracle_to_cas (col);
      ret = OCIAttrGet (col, OCI_DTYPE_PARAM, &scale, 0, OCI_ATTR_SCALE,
			ORA_ERR);
      ret = OCIAttrGet (col, OCI_DTYPE_PARAM, &prec, 0, OCI_ATTR_PRECISION,
			ORA_ERR);
      ret = OCIAttrGet (col, OCI_DTYPE_PARAM, &name, &size, OCI_ATTR_NAME,
			ORA_ERR);
      net_buf_column_info_set (net_buf, type, scale, prec, (char *) name);
      net_buf_cp_int (net_buf, 1, NULL);
      net_buf_cp_str (net_buf, "", 1);	/* attr_name */
      ret = OCIAttrGet (col, OCI_DTYPE_PARAM, &name, &size,
			OCI_ATTR_SCHEMA_NAME, ORA_ERR);
      net_buf_cp_int (net_buf, size + 1, NULL);
      net_buf_cp_str (net_buf, size == 0 ? "" : (char *) name, size + 1);	/* table_name */
      ret = OCIAttrGet (col, OCI_DTYPE_PARAM, &null, 0, OCI_ATTR_IS_NULL,
			ORA_ERR);
      net_buf_cp_byte (net_buf, null);	/* is_non_null */
    }
}

int
ux_prepare (char *sql_stmt, int flag, bool auto_commit_mode,
	    T_NET_BUF * net_buf, T_REQ_INFO * req_info,
	    unsigned int query_seq_num)
{
  T_SRV_HANDLE *srv_handle = NULL;
  OCIStmt *stmt;
  int srv_h_id;
  int err_code;
  int num_markers = 0;
  ub4 size;

  srv_h_id = hm_new_srv_handle (&srv_handle, query_seq_num);
  if (srv_h_id < 0)
    {
      err_code = srv_h_id;
      goto prepare_error;
    }
  srv_handle->schema_type = -1;
  srv_handle->auto_commit_mode = auto_commit_mode;

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
  net_buf_cp_int (net_buf, 0, NULL);	/* result_cache_lifetime */
  convert_stmt_type_oracle_to_cubrid (srv_handle);
  net_buf_cp_byte (net_buf, srv_handle->stmt_type);
  net_buf_cp_int (net_buf, num_markers, NULL);

  srv_handle->session = (void *) stmt;
  if (srv_handle->stmt_type == CUBRID_STMT_SELECT)
    {
      set_metadata_info (srv_handle, net_buf);
    }
  else
    {
      net_buf_cp_byte (net_buf, 1);	/* updatable flag */
      net_buf_cp_int (net_buf, 0, NULL);
    }
  return srv_h_id;

prepare_error:
  cas_oracle_get_errno ();
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
netval_to_oraval (void *net_type, void *net_value, ub2 * type, size_t * size,
		  void **data)
{
  char cas_type;
  int data_size;

  *data = 0;
  NET_ARG_GET_CHAR (cas_type, net_type);
  NET_ARG_GET_SIZE (data_size, net_value);

  switch (cas_type)
    {
    case CCI_U_TYPE_NULL:
      *type = 0;
      *size = 0;
      *data = 0;
      break;
    case CCI_U_TYPE_CHAR:
    case CCI_U_TYPE_STRING:
      {
	char *val;
	*type = SQLT_STR;
	NET_ARG_GET_STR (val, *size, net_value);
	*data = malloc (*size);
	strncpy (*data, val, *size);
	break;
      }
    case CCI_U_TYPE_INT:
      {
	int *val = (int *) malloc (sizeof (int));
	NET_ARG_GET_INT (*val, net_value);
	*size = sizeof (int);
	*type = SQLT_INT;
	*data = val;
	break;
      }
    case CCI_U_TYPE_SHORT:
      {
	short *val = (short *) malloc (sizeof (short));
	NET_ARG_GET_SHORT (*val, net_value);
	*size = sizeof (short);
	*type = SQLT_INT;
	*data = val;
	break;
      }
    case CCI_U_TYPE_FLOAT:
      {
	float *val = (float *) malloc (sizeof (float));
	NET_ARG_GET_FLOAT (*val, net_value);
	*size = sizeof (float);
	*type = SQLT_IBFLOAT;
	*data = val;
	break;
      }
    case CCI_U_TYPE_DOUBLE:
      {
	double *val = (double *) malloc (sizeof (double));
	NET_ARG_GET_DOUBLE (*val, net_value);
	*size = sizeof (double);
	*type = SQLT_IBDOUBLE;
	*data = val;
	break;
      }
    case CCI_U_TYPE_DATE:
      {
	OCIDate *val = (OCIDate *) malloc (sizeof (OCIDate));
	int year, month, day;
	NET_ARG_GET_DATE (year, month, day, net_value);
	*size = 7;
	*type = SQLT_DAT;
	set_date_to_ora_date ((char *) val, year, month, day);
	set_time_to_ora_date ((char *) val, 0, 0, 0);
	*data = val;
	break;
      }
    case CCI_U_TYPE_TIME:
      {
	OCIDate *val = (OCIDate *) malloc (sizeof (OCIDate));
	int hh, mm, ss;
	NET_ARG_GET_TIME (hh, mm, ss, net_value);
	*size = 7;
	*type = SQLT_DAT;
	set_date_to_ora_date ((char *) val, 0, 0, 0);
	set_time_to_ora_date ((char *) val, hh, mm, ss);
	*data = val;
	break;
      }
    case CCI_U_TYPE_TIMESTAMP:
      {
	OCIDate *val = (OCIDate *) malloc (sizeof (OCIDate));
	int year, month, day, hh, mm, ss;
	NET_ARG_GET_TIMESTAMP (year, month, day, hh, mm, ss, net_value);
	*size = 7;
	*type = SQLT_DAT;
	OCIDateSetDate (val, year, month, day);
	OCIDateSetTime (val, hh, mm, ss);
	*data = val;
	break;
      }
    case CCI_U_TYPE_DATETIME:
      {
	OCIDate *val = (OCIDate *) malloc (sizeof (OCIDate));
	int year, month, day, hh, mm, ss, ms;
	NET_ARG_GET_DATETIME (year, month, day, hh, mm, ss, ms, net_value);
	*size = 7;
	*type = SQLT_DAT;
	OCIDateSetDate (val, year, month, day);
	OCIDateSetTime (val, hh, mm, ss);
	*data = val;
	break;
      }
      break;
    case CCI_U_TYPE_BIT:
    case CCI_U_TYPE_VARBIT:
      {
	char *value;
	NET_ARG_GET_STR (value, *size, net_value);
	*type = SQLT_LNG;
	*data = (char *) malloc (*size);
	if (*data == NULL)
	  {
	    return ERROR_INFO_SET (CAS_ER_NO_MORE_MEMORY,
				   CAS_ERROR_INDICATOR);
	  }
	memcpy (*data, value, *size);
	break;
      }
    case CCI_U_TYPE_NUMERIC:
      {
	OCINumber *val = (OCINumber *) malloc (sizeof (OCINumber));
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
	*type = SQLT_NUM;
	*size = sizeof (OCINumber);
	*data = val;
	break;
      }
    case CCI_U_TYPE_BIGINT:
      {
	char *buf = (char *) malloc (OCI_NUMBER_SIZE);
	int ret;
	sb8 bi_val;
	NET_ARG_GET_BIGINT (bi_val, net_value);
	*type = SQLT_STR;
	*size = sizeof (bi_val);
	snprintf (buf, OCI_NUMBER_SIZE, "%ld", bi_val);
	*data = buf;
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

static int
set_result_info (T_SRV_HANDLE * srv_handle)
{
  OCIStmt *stmt;
  OCIParam *col;
  int ret, count, i;
  ub4 char_semantics;
  T_QUERY_RESULT_COLUMN *columns;
  void **data;

  stmt = srv_handle->session;
  ret = ORA_PARAM_COUNT (stmt, count);
  columns = (T_QUERY_RESULT_COLUMN *) calloc (count,
					      sizeof (T_QUERY_RESULT_COLUMN));
  if (columns == NULL)
    {
      return ERROR_INFO_SET (CAS_ER_NO_MORE_MEMORY, CAS_ERROR_INDICATOR);
    }

  /* TODO: error check all OCI call */
  for (i = 0; i < count; i++)
    {
      ret = OCIParamGet (stmt, OCI_HTYPE_STMT, ORA_ERR, (void **) &col,
			 i + 1);
      ret = OCIAttrGet (col, OCI_DTYPE_PARAM, &columns[i].type, 0,
			OCI_ATTR_DATA_TYPE, ORA_ERR);
      ret = OCIAttrGet (col, OCI_DTYPE_PARAM, &char_semantics, 0,
			OCI_ATTR_CHAR_USED, ORA_ERR);
      if (char_semantics == 1)
	{
	  ret = OCIAttrGet (col, OCI_DTYPE_PARAM, &columns[i].size, 0,
			    OCI_ATTR_CHAR_SIZE, ORA_ERR);
	}
      else
	{
	  ret = OCIAttrGet (col, OCI_DTYPE_PARAM, &columns[i].size, 0,
			    OCI_ATTR_DATA_SIZE, ORA_ERR);
	}

      switch (columns[i].type)
	{
	case SQLT_NUM:
	  {
	    columns[i].type = SQLT_VNU;
	    columns[i].size = sizeof (OCINumber);
	    columns[i].data = malloc (sizeof (OCINumber));
	    data = columns[i].data;
	    break;
	  }
	case SQLT_TIMESTAMP:
	  {
	    ret = OCIDescriptorAlloc (ORA_ENV, &columns[i].data,
				      OCI_DTYPE_TIMESTAMP, 0, 0);
	    data = &columns[i].data;
	    break;
	  }
	case SQLT_TIMESTAMP_TZ:
	  {
	    ret = OCIDescriptorAlloc (ORA_ENV, &columns[i].data,
				      OCI_DTYPE_TIMESTAMP_TZ, 0, 0);
	    data = &columns[i].data;
	    break;
	  }
	case SQLT_TIMESTAMP_LTZ:
	  {
	    ret = OCIDescriptorAlloc (ORA_ENV, &columns[i].data,
				      OCI_DTYPE_TIMESTAMP_LTZ, 0, 0);
	    data = &columns[i].data;
	    break;
	  }
	case OCI_TYPECODE_CLOB:
	case OCI_TYPECODE_BLOB:
	  {
	    ret = OCIDescriptorAlloc (ORA_ENV, &columns[i].data,
				      OCI_DTYPE_LOB, 0, 0);
	    data = &columns[i].data;
	    columns[i].size = -1;
	    break;
	  }
	default:
	  {
	    columns[i].data = malloc (columns[i].size);
	    data = columns[i].data;
	    break;
	  }
	}
      ret = OCIDefineByPos (stmt, (OCIDefine **) & columns[i].define,
			    ORA_ERR, i + 1, data, columns[i].size,
			    columns[i].type, &columns[i].null, 0, 0,
			    OCI_DEFAULT);
    }
  srv_handle->q_result = malloc (sizeof (T_QUERY_RESULT));
  srv_handle->q_result->column_count = count;
  srv_handle->q_result->columns = columns;
  return ret;
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
  void **data;
  T_OBJECT ins_oid;

  hm_qresult_end (srv_handle, FALSE);
  stmt = (OCIStmt *) srv_handle->session;
  ret = ORA_BIND_COUNT (stmt, bind_count);
  bind = (OCIBind **) malloc (sizeof (OCIBind *) * bind_count);
  if (bind == NULL)
    {
      return ERROR_INFO_SET (CAS_ER_NO_MORE_MEMORY, CAS_ERROR_INDICATOR);
    }
  data = (void **) malloc (sizeof (void *) * bind_count);
  if (data == NULL)
    {
      return ERROR_INFO_SET (CAS_ER_NO_MORE_MEMORY, CAS_ERROR_INDICATOR);
    }

  for (i = 0; i < bind_count; i++)
    {
      ret = netval_to_oraval (argv[2 * i], argv[2 * i + 1], &type, &size,
			      &data[i]);
      GOTO_ORA_ERROR (ret, execute_error);
      ret = OCIBindByPos (stmt, &bind[i], ORA_ERR, i + 1, data[i], size, type,
			  0, 0, 0, 0, 0, OCI_DEFAULT);
      GOTO_ORA_ERROR (ret, execute_error);
    }

  SQL_LOG2_EXEC_BEGIN (as_info->cur_sql_log2, 1);
  if (srv_handle->stmt_type == CUBRID_STMT_SELECT)
    {
      iters = 0;
      mode = OCI_STMT_SCROLLABLE_READONLY;
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
      set_result_info (srv_handle);
      ret = OCIStmtFetch2 (stmt, ORA_ERR, 1, OCI_FETCH_LAST, 0, OCI_DEFAULT);
      if (ORA_SUCCESS (ret))
	{
	  ret = ORA_CURR_POS (stmt, row_count);
	  ret = OCIStmtFetch2 (stmt, ORA_ERR, 1, OCI_FETCH_FIRST, 0,
			       OCI_DEFAULT);
	}
      else
	{
	  row_count = 0;
	}
    }
  else
    {
      ret = ORA_ROW_COUNT (stmt, row_count);
    }

  net_buf_cp_int (net_buf, row_count, NULL);
  net_buf_cp_byte (net_buf, 0);
  net_buf_cp_int (net_buf, 1, NULL);	/* num_q_result */
  net_buf_cp_byte (net_buf, srv_handle->stmt_type);
  net_buf_cp_int (net_buf, row_count, NULL);
  memset (&ins_oid, 0, sizeof (T_OBJECT));
  NET_BUF_CP_OBJECT (net_buf, &ins_oid);
  net_buf_cp_int (net_buf, 0, NULL);	/* cache time sec */
  net_buf_cp_int (net_buf, 0, NULL);	/* cache time usec */

  srv_handle->max_row = max_row;
  srv_handle->max_col_size = max_col_size;
  FREE_MEM (bind);
  for (i = 0; i < bind_count; i++)
    {
      FREE_MEM (data[i]);
    }
  FREE_MEM (data);
  return ret;

execute_error:
  cas_oracle_get_errno ();
  NET_BUF_ERR_SET (net_buf);
  if (srv_handle->auto_commit_mode)
    {
      req_info->need_auto_commit = TRAN_AUTOROLLBACK;
    }
  errors_in_transaction++;
  FREE_MEM (bind);
  for (i = 0; i < bind_count; i++)
    {
      FREE_MEM (data[i]);
    }
  FREE_MEM (data);
  return ret;
}

int
ux_execute_call (T_SRV_HANDLE * srv_handle, char flag, int max_col_size,
		 int max_row, int argc, void **argv, T_NET_BUF * net_buf,
		 T_REQ_INFO * req_info, CACHE_TIME * clt_cache_time,
		 int *clt_cache_reusable)
{
  return ERROR_INFO_SET (CAS_ER_NOT_IMPLEMENTED, CAS_ERROR_INDICATOR);
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
ora_value_to_net_buf (void *value, int type, int null, int size,
		      T_NET_BUF * net_buf)
{
  if (null == true)
    {
      net_buf_cp_int (net_buf, -1, NULL);
      return 4;
    }

  switch (type)
    {
    case OCI_TYPECODE_CHAR:
    case OCI_TYPECODE_VARCHAR:
    case OCI_TYPECODE_VARCHAR2:
      {
	add_res_data_string (net_buf, value, size, 0);
	return 4 + size + 1;
      }
    case OCI_TYPECODE_SMALLINT:
      {
	short val;
	memcpy (&val, value, size);
	add_res_data_short (net_buf, val, 0);
	return 4 + 2;
      }
    case OCI_TYPECODE_INTEGER:
      {
	int val;
	memcpy (&val, value, size);
	add_res_data_int (net_buf, val, 0);
	return 4 + 4;
      }
    case OCI_TYPECODE_FLOAT:
    case OCI_TYPECODE_REAL:
    case OCI_TYPECODE_BFLOAT:
      {
	float val;
	memcpy (&val, value, size);
	add_res_data_float (net_buf, val, 0);
	return 4 + 4;
      }
    case OCI_TYPECODE_DOUBLE:
    case OCI_TYPECODE_BDOUBLE:
      {
	double val;
	memcpy (&val, value, size);
	add_res_data_double (net_buf, val, 0);
	return 4 + 8;
      }
    case OCI_TYPECODE_TIMESTAMP:
    case OCI_TYPECODE_TIMESTAMP_TZ:
    case OCI_TYPECODE_TIMESTAMP_LTZ:
      {
	sb2 year;
	ub1 month, day, hour, min, sec;
	ub4 fsec;
	int ret;
	OCIDateTime *datetime = (OCIDateTime *) value;
	ret = OCIDateTimeGetDate (ORA_SVC, ORA_ERR, (OCIDateTime *) value,
				  &year, &month, &day);
	ret = OCIDateTimeGetTime (ORA_SVC, ORA_ERR, (OCIDateTime *) value,
				  &hour, &min, &sec, &fsec);
	add_res_data_timestamp (net_buf, year, month, day, hour, min, sec, 0);
	return 4 + CAS_TIMESTAMP_SIZE;
      }
    case OCI_TYPECODE_DATE:
      {
	sb2 year;
	ub1 month, day;
	get_date_from_ora_date (value, &year, &month, &day);
	add_res_data_date (net_buf, year, month, day, 0);
	return 4 + SIZE_DATE;
      }
    case SQLT_LNG:
      {
	add_res_data_bytes (net_buf, value, size, 0);
	return 4 + size;
      }
    case OCI_TYPECODE_NUMBER:
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
    case OCI_TYPECODE_CLOB:
    case OCI_TYPECODE_BLOB:
      {
	OCILobLocator *lob = (OCILobLocator *) value;
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
	buf = (ub1 *) malloc (len);
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
    case OCI_TYPECODE_REF:
    case OCI_TYPECODE_INTERVAL_YM:
    case OCI_TYPECODE_INTERVAL_DS:
    case OCI_TYPECODE_OCTET:
    case OCI_TYPECODE_DECIMAL:
    case OCI_TYPECODE_RAW:
    case OCI_TYPECODE_VARRAY:
    case OCI_TYPECODE_TABLE:
    case OCI_TYPECODE_BFILE:
    case OCI_TYPECODE_OBJECT:
    case OCI_TYPECODE_NAMEDCOLLECTION:
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
  T_QUERY_RESULT_COLUMN *columns = q_result->columns;
  void *value;
  int i, type, null, size;
  int data_size = 0;

  for (i = 0; i < q_result->column_count; i++)
    {
      value = columns[i].data;
      type = columns[i].type;
      null = columns[i].null;
      size = columns[i].size;
      data_size += ora_value_to_net_buf (value, type, null, size, net_buf);
    }
  return data_size;
}

int
ux_fetch (T_SRV_HANDLE * srv_handle, int cursor_pos, int fetch_count,
	  char fetch_flag, int result_set_index, T_NET_BUF * net_buf,
	  T_REQ_INFO * req_info)
{
  OCIStmt *stmt;
  int count, ret, tuple, num_tuple_msg_offset;
  T_OBJECT tuple_obj;

  net_buf_cp_int (net_buf, 0, NULL);	/* result code */
  if (fetch_count <= 0)
    {
      fetch_count = 100;
    }

  if (srv_handle->q_result == NULL
      || srv_handle->stmt_type != CUBRID_STMT_SELECT)
    {
      return ERROR_INFO_SET (CAS_ER_NO_MORE_DATA, CAS_ERROR_INDICATOR);
    }

  stmt = srv_handle->session;
  ret = ORA_ROW_COUNT (stmt, count);
  net_buf_cp_int (net_buf, count, &num_tuple_msg_offset);
  memset (&tuple_obj, 0, sizeof (T_OBJECT));
  tuple = 0;

  while (count > 0 && CHECK_NET_BUF_SIZE (net_buf))
    {
      if (srv_handle->cursor_pos > 0)
	{
	  ret = OCIStmtFetch (stmt, ORA_ERR, 1, OCI_FETCH_NEXT, OCI_DEFAULT);
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
	      break;
	    }
	  else if (!ORA_SUCCESS (ret))
	    {
	      /* TODO: error */
	      break;
	    }
	}
      net_buf_cp_int (net_buf, cursor_pos, NULL);
      NET_BUF_CP_OBJECT (net_buf, &tuple_obj);
      net_buf_cp_oracle_row (srv_handle, net_buf, stmt);
      srv_handle->cursor_pos++;
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
      if (r->columns[i].data)
	{
	  switch (r->columns[i].type)
	    {
	    case SQLT_TIMESTAMP:
	      {
		OCIDescriptorFree (r->columns[i].data, OCI_DTYPE_TIMESTAMP);
		break;
	      }
	    case SQLT_TIMESTAMP_TZ:
	      {
		OCIDescriptorFree (r->columns[i].data,
				   OCI_DTYPE_TIMESTAMP_TZ);
		break;
	      }
	    case SQLT_TIMESTAMP_LTZ:
	      {
		OCIDescriptorFree (r->columns[i].data,
				   OCI_DTYPE_TIMESTAMP_LTZ);
		break;
	      }
	    case OCI_TYPECODE_CLOB:
	    case OCI_TYPECODE_BLOB:
	      {
		OCIDescriptorFree (r->columns[i].data, OCI_DTYPE_LOB);
		break;
	      }
	    default:
	      {
		FREE_MEM (r->columns[i].data);
		break;
	      }
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
is_server_alive ()
{
  /* TODO */
  return true;
}
