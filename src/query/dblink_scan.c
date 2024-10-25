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

#ident "$Id$"

#include <string.h>
#include "query_executor.h"
#include "dblink_scan.h"

#include "xasl.h"

#include "dbtype.h"
#include "object_primitive.h"
#include "object_representation.h"
#include "query_list.h"
#include "regu_var.hpp"

#ifndef DBDEF_HEADER_
#define DBDEF_HEADER_
#endif

#include "db_date.h"
#include "tz_support.h"
#include <cas_cci.h>

#include <db_json.hpp>
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#define MAX_LEN_CONNECTION_URL    512

// *INDENT-OFF*
#define  DATETIME_DECODE(date, dt, m, d, y, hour, min, sec, msec) \
  do \
    {  \
      db_datetime_decode (&(dt), &d, &y, &m, &hour, &min, &sec, &msec); \
      date.hh = hour; \
      date.mm = min;  \
      date.ss = sec; \
      date.ms = msec;  \
      date.mon = m; \
      date.day = d; \
      date.yr = y; \
    } \
  while (0)
  	
#define TIMESTAMP_DECODE(date, dt, tm, m, d, y, hour, min, sec) \
  do \
    { \
      db_time_decode (&tm, &hour, &min, &sec); \
      db_date_decode (&dt, &m, &d, &y); \
      date.hh = hour; \
      date.mm = min; \
      date.ss = sec; \
      date.ms = 0; \
      date.mon = m; \
      date.day = d; \
      date.yr = y; \
    } \
  while (0)
// *INDENT-ON*

/*
 * dblink_scan.c - Routines to implement scanning the values
 *                 received by the cci interface
 */

static int type_map[] = {
  0,
  CCI_A_TYPE_STR,		/* CCI_U_TYPE_CHAR */
  CCI_A_TYPE_STR,		/* CCI_U_TYPE_STRING */
  CCI_A_TYPE_STR,		/* CCI_U_TYPE_NCHAR */
  CCI_A_TYPE_STR,		/* CCI_U_TYPE_VARNCHAR */
  CCI_A_TYPE_BIT,		/* CCI_U_TYPE_BIT */
  CCI_A_TYPE_BIT,		/* CCI_U_TYPE_VARBIT */
  CCI_A_TYPE_STR,		/* CCI_U_TYPE_NUMERIC */
  CCI_A_TYPE_INT,		/* CCI_U_TYPE_INT */
  CCI_A_TYPE_INT,		/* CCI_U_TYPE_SHORT */
  CCI_A_TYPE_DOUBLE,		/* CCI_U_TYPE_MONETARY */
  CCI_A_TYPE_FLOAT,		/* CCI_U_TYPE_FLOAT */
  CCI_A_TYPE_DOUBLE,		/* CCI_U_TYPE_DOUBLE */
  CCI_A_TYPE_DATE,		/* CCI_U_TYPE_DATE */
  CCI_A_TYPE_DATE,		/* CCI_U_TYPE_TIME */
  CCI_A_TYPE_DATE,		/* CCI_U_TYPE_TIMESTAMP */

  /* not support for collection type, processing as null */
  0,				/* CCI_U_TYPE_SET */
  0,				/* CCI_U_TYPE_MULTISET */
  0,				/* CCI_U_TYPE_SEQUENCE */
  0,				/* CCI_U_TYPE_OBJECT */

  0,				/* CCI_U_TYPE_RESULTSET */
  CCI_A_TYPE_BIGINT,		/* CCI_U_TYPE_BIGINT */
  CCI_A_TYPE_DATE,		/* CCI_U_TYPE_DATETIME */

  /* not support for BLOB, CLOB, and ENUM */
  0,				/* CCI_U_TYPE_BLOB */
  0,				/* CCI_U_TYPE_CLOB */
  0,				/* CCI_U_TYPE_ENUM */

  CCI_A_TYPE_UINT,		/* CCI_U_TYPE_USHORT */
  CCI_A_TYPE_UINT,		/* CCI_U_TYPE_UINT */
  CCI_A_TYPE_UBIGINT,		/* CCI_U_TYPE_UBIGINT */
  CCI_A_TYPE_DATE_TZ,		/* CCI_U_TYPE_TIMESTAMPTZ */
  CCI_A_TYPE_DATE_TZ,		/* CCI_U_TYPE_TIMESTAMPLTZ */
  CCI_A_TYPE_DATE_TZ,		/* CCI_U_TYPE_DATETIMETZ */
  CCI_A_TYPE_DATE_TZ,		/* CCI_U_TYPE_DATETIMELTZ */
  /* Disabled type */
  CCI_A_TYPE_DATE_TZ,		/* CCI_U_TYPE_TIMETZ - internal only, RESERVED */
  /* end of disabled types */
  CCI_A_TYPE_STR		/* CCI_U_TYPE_JSON */
};

#define NULL_CHECK(ind) \
	if ((ind) == -1) break

static T_CCI_U_TYPE
dblink_get_basic_utype (T_CCI_U_EXT_TYPE u_ext_type)
{
  if (CCI_IS_SET_TYPE (u_ext_type))
    {
      return CCI_U_TYPE_SET;
    }
  else if (CCI_IS_MULTISET_TYPE (u_ext_type))
    {
      return CCI_U_TYPE_MULTISET;
    }
  else if (CCI_IS_SEQUENCE_TYPE (u_ext_type))
    {
      return CCI_U_TYPE_SEQUENCE;
    }
  else
    {
      return (T_CCI_U_TYPE) CCI_GET_COLLECTION_DOMAIN (u_ext_type);
    }
}

static char *
print_utype_to_string (int type)
{
  switch (type)
    {
    case CCI_U_TYPE_BIT:
      return (char *) "bit";
    case CCI_U_TYPE_VARBIT:
      return (char *) "varbit";
    case CCI_U_TYPE_NULL:
      return (char *) "null";
    case CCI_U_TYPE_SET:
      return (char *) "set";
    case CCI_U_TYPE_MULTISET:
      return (char *) "multiset";
    case CCI_U_TYPE_SEQUENCE:
      return (char *) "sequence";
    case CCI_U_TYPE_OBJECT:
      return (char *) "object";
    case CCI_U_TYPE_BLOB:
      return (char *) "blob";
    case CCI_U_TYPE_CLOB:
      return (char *) "clob";
    case CCI_U_TYPE_JSON:
      return (char *) "json";
    case CCI_U_TYPE_ENUM:
      return (char *) "enum";
    default:
      return (char *) "";
    }
}

static int
dblink_make_cci_value (DB_VALUE * cci_value, T_CCI_U_TYPE utype, void *val, int prec, int len, int codeset)
{
  int error;

  switch (utype)
    {
    case CCI_U_TYPE_SHORT:
      error = db_make_short (cci_value, *(short *) val);
      break;
    case CCI_U_TYPE_BIGINT:
      error = db_make_bigint (cci_value, *(DB_BIGINT *) val);
      break;
    case CCI_U_TYPE_INT:
      error = db_make_int (cci_value, *(int *) val);
      break;
    case CCI_U_TYPE_FLOAT:
      error = db_make_float (cci_value, *(float *) val);
      break;
    case CCI_U_TYPE_DOUBLE:
      error = db_make_double (cci_value, *(double *) val);
      break;
    case CCI_U_TYPE_MONETARY:
      error = db_make_monetary (cci_value, db_get_currency_default (), *(double *) val);
      break;
    case CCI_U_TYPE_STRING:
      error =
	db_make_varchar (cci_value, prec, (DB_CONST_C_CHAR) val, len, codeset, LANG_GET_BINARY_COLLATION (codeset));
      break;
    case CCI_U_TYPE_VARNCHAR:
      error =
	db_make_varnchar (cci_value, prec, (DB_CONST_C_CHAR) val, len, codeset, LANG_GET_BINARY_COLLATION (codeset));
      break;
    case CCI_U_TYPE_CHAR:
      error = db_make_char (cci_value, prec, (DB_CONST_C_CHAR) val, len, codeset, LANG_GET_BINARY_COLLATION (codeset));
      break;
    case CCI_U_TYPE_NCHAR:
      error = db_make_nchar (cci_value, prec, (DB_CONST_C_CHAR) val, len, codeset, LANG_GET_BINARY_COLLATION (codeset));
      break;
    default:
      assert (false);
      break;
    }

  return error;
}

static int
dblink_make_date_time (T_CCI_U_TYPE utype, DB_VALUE * value_p, T_CCI_DATE * date_time)
{
  DB_TIME t_time;
  DB_DATE t_date;
  DB_DATETIME t_datetime;
  DB_TIMESTAMP t_timestamp;
  int error = NO_ERROR;

  db_make_null (value_p);
  switch (utype)
    {
    case CCI_U_TYPE_TIME:
      error = db_make_time (value_p, date_time->hh, date_time->mm, date_time->ss);
      break;
    case CCI_U_TYPE_DATE:
      error = db_make_date (value_p, date_time->mon, date_time->day, date_time->yr);
      break;
    case CCI_U_TYPE_DATETIME:
      error = db_datetime_encode (&t_datetime, date_time->mon, date_time->day, date_time->yr,
				  date_time->hh, date_time->mm, date_time->ss, date_time->ms);
      if (error != NO_ERROR)
	{
	  break;
	}
      error = db_make_datetime (value_p, &t_datetime);
      break;
    case CCI_U_TYPE_TIMESTAMP:
      error = db_time_encode (&t_time, date_time->hh, date_time->mm, date_time->ss);
      if (error != NO_ERROR)
	{
	  break;
	}
      error = db_date_encode (&t_date, date_time->mon, date_time->day, date_time->yr);
      if (error != NO_ERROR)
	{
	  break;
	}
      error = db_timestamp_encode (&t_timestamp, &t_date, &t_time);
      if (error != NO_ERROR)
	{
	  break;
	}
      error = db_make_timestamp (value_p, t_timestamp);
      break;
    default:
      assert (false);
      break;
    }

  return error;
}

static int
dblink_make_date_time_tz (T_CCI_U_TYPE utype, DB_VALUE * value_p, T_CCI_DATE_TZ * date_time_tz)
{
  DB_TIME t_time;
  DB_DATE t_date;
  DB_DATETIME t_datetime;
  DB_DATETIMETZ tz_datetime;
  DB_TIMESTAMPTZ tz_timestamp;
  TZ_REGION region;
  int error = NO_ERROR;

  db_make_null (value_p);
  tz_get_session_tz_region (&region);

  switch (utype)
    {
    case CCI_U_TYPE_TIMESTAMPTZ:
      error = db_time_encode (&t_time, date_time_tz->hh, date_time_tz->mm, date_time_tz->ss);
      if (error != NO_ERROR)
	{
	  break;
	}
      error = db_date_encode (&t_date, date_time_tz->mon, date_time_tz->day, date_time_tz->yr);
      if (error != NO_ERROR)
	{
	  break;
	}
      error =
	tz_create_timestamptz (&t_date, &t_time, date_time_tz->tz, strlen (date_time_tz->tz), &region, &tz_timestamp,
			       NULL);
      if (error != NO_ERROR)
	{
	  break;
	}
      error = db_make_timestamptz (value_p, &tz_timestamp);
      break;
    case CCI_U_TYPE_DATETIMETZ:
      error = db_datetime_encode (&t_datetime, date_time_tz->mon, date_time_tz->day, date_time_tz->yr,
				  date_time_tz->hh, date_time_tz->mm, date_time_tz->ss, date_time_tz->ms);
      if (error != NO_ERROR)
	{
	  break;
	}
      error =
	tz_create_datetimetz (&t_datetime, date_time_tz->tz, strlen (date_time_tz->tz), &region, &tz_datetime, NULL);
      if (error != NO_ERROR)
	{
	  break;
	}
      error = db_make_datetimetz (value_p, &tz_datetime);
      break;
    case CCI_U_TYPE_TIMESTAMPLTZ:
      error = db_time_encode (&t_time, date_time_tz->hh, date_time_tz->mm, date_time_tz->ss);
      if (error != NO_ERROR)
	{
	  break;
	}
      error = db_date_encode (&t_date, date_time_tz->mon, date_time_tz->day, date_time_tz->yr);
      if (error != NO_ERROR)
	{
	  break;
	}
      error =
	tz_create_timestamptz (&t_date, &t_time, date_time_tz->tz, strlen (date_time_tz->tz), &region, &tz_timestamp,
			       NULL);
      if (error != NO_ERROR)
	{
	  break;
	}
      error = db_make_timestampltz (value_p, tz_timestamp.timestamp);
      break;
    case CCI_U_TYPE_DATETIMELTZ:
      error = db_datetime_encode (&t_datetime, date_time_tz->mon, date_time_tz->day, date_time_tz->yr,
				  date_time_tz->hh, date_time_tz->mm, date_time_tz->ss, date_time_tz->ms);
      if (error != NO_ERROR)
	{
	  break;
	}
      error =
	tz_create_datetimetz (&t_datetime, date_time_tz->tz, strlen (date_time_tz->tz), &region, &tz_datetime, NULL);
      if (error != NO_ERROR)
	{
	  break;
	}
      error = db_make_datetimeltz (value_p, &tz_datetime.datetime);
      break;
    default:
      assert (false);
      break;
    }

  return error;
}

static int
dblink_bind_param (int stmt_handle, VAL_DESCR * vd, DBLINK_HOST_VARS * host_vars)
{
  int i, n, ret;
  T_CCI_PARAM_INFO *param;
  T_CCI_A_TYPE a_type;
  T_CCI_U_TYPE u_type;
  void *value;
  double adouble;
  int month, day, year;
  int hh, mm, ss, ms;
  DB_TIMESTAMP *timestamp;
  DB_DATETIME *datetime;
  DB_DATETIME dt_local;
  TZ_ID *zone_id;
  DB_DATE date;
  DB_TIME time;
  T_CCI_DATE cci_date;
  T_CCI_BIT cci_bit;
  int num_size;
  char num_str[40];

  unsigned char type;

  for (n = 0; n < host_vars->count; n++)
    {
      i = host_vars->index[n];
      value = &vd->dbval_ptr[i].data;
      type = vd->dbval_ptr[i].domain.general_info.type;
      switch (type)
	{
	case DB_TYPE_BIT:
	  a_type = CCI_A_TYPE_BIT;
	  u_type = CCI_U_TYPE_BIT;
	  value = (void *) &cci_bit;
	  cci_bit.buf = (char *) db_get_bit (&vd->dbval_ptr[i], &num_size);
	  cci_bit.size = QSTR_NUM_BYTES (num_size);
	  break;
	case DB_TYPE_VARBIT:
	  a_type = CCI_A_TYPE_BIT;
	  u_type = CCI_U_TYPE_VARBIT;
	  value = (void *) &cci_bit;
	  cci_bit.buf = (char *) db_get_bit (&vd->dbval_ptr[i], &num_size);
	  cci_bit.size = QSTR_NUM_BYTES (num_size);
	  break;
	case DB_TYPE_JSON:
	  a_type = CCI_A_TYPE_STR;
	  u_type = CCI_U_TYPE_JSON;
	  value = (void *) db_get_json_raw_body (&vd->dbval_ptr[i]);
	  break;
	case DB_TYPE_SHORT:
	  a_type = CCI_A_TYPE_INT;
	  u_type = CCI_U_TYPE_SHORT;
	  break;
	case DB_TYPE_INTEGER:
	  a_type = CCI_A_TYPE_INT;
	  u_type = CCI_U_TYPE_INT;
	  break;
	case DB_TYPE_BIGINT:
	  a_type = CCI_A_TYPE_BIGINT;
	  u_type = CCI_U_TYPE_BIGINT;
	  break;
	case DB_TYPE_NUMERIC:
	  a_type = CCI_A_TYPE_STR;
	  u_type = CCI_U_TYPE_NUMERIC;
	  value = (void *) numeric_db_value_print (&vd->dbval_ptr[i], num_str);
	  break;
	case DB_TYPE_DOUBLE:
	case DB_TYPE_FLOAT:
	  a_type = CCI_A_TYPE_DOUBLE;
	  u_type = CCI_U_TYPE_DOUBLE;
	  break;
	case DB_TYPE_STRING:
	case DB_TYPE_VARNCHAR:
	case DB_TYPE_CHAR:
	case DB_TYPE_NCHAR:
	  a_type = CCI_A_TYPE_STR;
	  u_type = CCI_U_TYPE_STRING;
	  value = (void *) db_get_string (&vd->dbval_ptr[i]);
	  break;
	case DB_TYPE_DATE:
	  a_type = CCI_A_TYPE_DATE;
	  u_type = CCI_U_TYPE_DATE;
	  db_date_decode ((DB_DATE *) value, &month, &day, &year);
	  cci_date.mon = month;
	  cci_date.day = day;
	  cci_date.yr = year;
	  value = &cci_date;
	  break;
	case DB_TYPE_TIME:
	  a_type = CCI_A_TYPE_DATE;
	  u_type = CCI_U_TYPE_TIME;
	  db_time_decode (&vd->dbval_ptr[i].data.time, &hh, &mm, &ss);
	  cci_date.hh = hh;
	  cci_date.mm = mm;
	  cci_date.ss = ss;
	  cci_date.ms = 0;
	  value = &cci_date;
	  break;
	case DB_TYPE_DATETIME:
	  a_type = CCI_A_TYPE_DATE;
	  u_type = CCI_U_TYPE_DATETIME;
	  DATETIME_DECODE (cci_date, vd->dbval_ptr[i].data.datetime, month, day, year, hh, mm, ss, ms);
	  value = &cci_date;
	  break;
	case DB_TYPE_TIMESTAMP:
	  a_type = CCI_A_TYPE_DATE;
	  u_type = CCI_U_TYPE_TIMESTAMP;
	  timestamp = &vd->dbval_ptr[i].data.utime;
	  db_timestamp_decode_ses (timestamp, &date, &time);
	  TIMESTAMP_DECODE (cci_date, date, time, month, day, year, hh, mm, ss);
	  value = &cci_date;
	  break;
	case DB_TYPE_TIMESTAMPTZ:
	  a_type = CCI_A_TYPE_DATE;
	  u_type = CCI_U_TYPE_TIMESTAMPTZ;
	  timestamp = &vd->dbval_ptr[i].data.timestamptz.timestamp;
	  zone_id = &vd->dbval_ptr[i].data.timestamptz.tz_id;
	  db_timestamp_decode_w_tz_id (timestamp, zone_id, &date, &time);
	  TIMESTAMP_DECODE (cci_date, date, time, month, day, year, hh, mm, ss);
	  value = &cci_date;
	  break;
	case DB_TYPE_TIMESTAMPLTZ:
	  a_type = CCI_A_TYPE_DATE;
	  u_type = CCI_U_TYPE_TIMESTAMPLTZ;
	  timestamp = &vd->dbval_ptr[i].data.timestamptz.timestamp;
	  db_timestamp_decode_utc (timestamp, &date, &time);
	  TIMESTAMP_DECODE (cci_date, date, time, month, day, year, hh, mm, ss);
	  value = &cci_date;
	  break;
	case DB_TYPE_DATETIMETZ:
	  a_type = CCI_A_TYPE_DATE;
	  u_type = CCI_U_TYPE_DATETIMETZ;
	  datetime = &vd->dbval_ptr[i].data.datetimetz.datetime;
	  zone_id = &vd->dbval_ptr[i].data.datetimetz.tz_id;
	  tz_utc_datetimetz_to_local (datetime, zone_id, &dt_local);
	  DATETIME_DECODE (cci_date, dt_local, month, day, year, hh, mm, ss, ms);
	  value = &cci_date;
	  break;
	case DB_TYPE_DATETIMELTZ:
	  a_type = CCI_A_TYPE_DATE;
	  u_type = CCI_U_TYPE_DATETIMELTZ;
	  datetime = &vd->dbval_ptr[i].data.datetimetz.datetime;
	  tz_datetimeltz_to_local (datetime, &dt_local);
	  DATETIME_DECODE (cci_date, dt_local, month, day, year, hh, mm, ss, ms);
	  value = &cci_date;
	  break;
	case DB_TYPE_NULL:
	  value = NULL;
	  u_type = CCI_U_TYPE_NULL;
	  break;
	default:
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DBLINK_UNSUPPORTED_TYPE, 1, "unknown");
	  return ER_DBLINK_UNSUPPORTED_TYPE;
	}
      ret = cci_bind_param (stmt_handle, n + 1, a_type, value, u_type, 0);
      if (ret < 0)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DBLINK_INVALID_BIND_PARAM, 0);
	  return ER_DBLINK_INVALID_BIND_PARAM;
	}
    }

  return S_SUCCESS;
}

int
dblink_execute_query (struct access_spec_node *spec, VAL_DESCR * vd, DBLINK_HOST_VARS * host_vars)
{
  int ret = NO_ERROR, result, conn_handle, stmt_handle;
  T_CCI_ERROR err_buf;
  char conn_url[MAX_LEN_CONNECTION_URL] = { 0, };
  char *user_name = spec->s.dblink_node.conn_user;
  char *password = spec->s.dblink_node.conn_password;
  char *sql_text = spec->s.dblink_node.conn_sql;

  char *find = strstr (spec->s.dblink_node.conn_url, ":?");
  if (find)
    {
      snprintf (conn_url, MAX_LEN_CONNECTION_URL, "%s%s", spec->s.dblink_node.conn_url, "&__gateway=true");
    }
  else
    {
      snprintf (conn_url, MAX_LEN_CONNECTION_URL, "%s%s", spec->s.dblink_node.conn_url, "?__gateway=true");
    }

  conn_handle = cci_connect_with_url_ex (conn_url, user_name, password, &err_buf);
  if (conn_handle < 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DBLINK, 1, err_buf.err_msg);
      goto error_exit;
    }
  else
    {
      cci_set_autocommit (conn_handle, CCI_AUTOCOMMIT_TRUE);
      stmt_handle = cci_prepare (conn_handle, sql_text, 0, &err_buf);
      if (stmt_handle < 0)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DBLINK, 1, err_buf.err_msg);
	  goto error_exit;
	}

      if (host_vars->count > 0)
	{
	  if ((ret = dblink_bind_param (stmt_handle, vd, host_vars)) < 0)
	    {
	      return ret;
	    }
	}

      result = cci_execute (stmt_handle, 0, 0, &err_buf);
      if (result < 0)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DBLINK, 1, err_buf.err_msg);
	  goto error_exit;
	}

      ret = cci_disconnect (conn_handle, &err_buf);
      if (ret < 0)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DBLINK, 1, err_buf.err_msg);
	  goto error_exit;
	}
    }

  return result;

error_exit:
  return ER_DBLINK;
}

/*
 * dblink_open_scan () - open the scan for dblink
 *   return: int
 *   scan_info(out)      : dblink information
 *   conn_url(in)        : connection URL for dblink
 *   user_name(in)	 : user name for dblink
 *   passowrd(in)	 : password for dblink
 *   sql_text(in)	 : SQL text for dblink
 */
int
dblink_open_scan (DBLINK_SCAN_INFO * scan_info, struct access_spec_node *spec,
		  VAL_DESCR * vd, DBLINK_HOST_VARS * host_vars)
{
  int ret;
  T_CCI_ERROR err_buf;
  char conn_url[MAX_LEN_CONNECTION_URL] = { 0, };
  char *user_name = spec->s.dblink_node.conn_user;
  char *password = spec->s.dblink_node.conn_password;
  char *sql_text = spec->s.dblink_node.conn_sql;

  char *find = strstr (spec->s.dblink_node.conn_url, ":?");
  if (find)
    {
      snprintf (conn_url, MAX_LEN_CONNECTION_URL, "%s%s", spec->s.dblink_node.conn_url, "&__gateway=true");
    }
  else
    {
      snprintf (conn_url, MAX_LEN_CONNECTION_URL, "%s%s", spec->s.dblink_node.conn_url, "?__gateway=true");
    }

  scan_info->conn_handle = cci_connect_with_url_ex (conn_url, user_name, password, &err_buf);
  if (scan_info->conn_handle < 0)
    {
      scan_info->stmt_handle = -1;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DBLINK, 1, err_buf.err_msg);
      goto error_exit;
    }
  else
    {
      cci_set_autocommit (scan_info->conn_handle, CCI_AUTOCOMMIT_TRUE);
      scan_info->stmt_handle = cci_prepare (scan_info->conn_handle, sql_text, 0, &err_buf);
      if (scan_info->stmt_handle < 0)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DBLINK, 1, err_buf.err_msg);
	  goto error_exit;
	}

      if (host_vars->count > 0)
	{
	  if ((ret = dblink_bind_param (scan_info->stmt_handle, vd, host_vars)) < 0)
	    {
	      return ret;
	    }
	}

      ret = cci_execute (scan_info->stmt_handle, 0, 0, &err_buf);
      if (ret < 0)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DBLINK, 1, err_buf.err_msg);
	  goto error_exit;
	}
      else
	{
	  T_CCI_CUBRID_STMT stmt_type;

	  scan_info->col_info = (void *) cci_get_result_info (scan_info->stmt_handle, &stmt_type, &scan_info->col_cnt);
	  if (scan_info->col_info == NULL)
	    {
	      /* this can not be reached, something wrong */
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DBLINK, 1, "unknown error");
	      goto error_exit;
	    }
	  scan_info->cursor = CCI_CURSOR_FIRST;
	}
    }

  return NO_ERROR;

error_exit:
  return ER_DBLINK;
}

/*
 * dblink_close_scan () -
 *   return: int
 *   scan_info(in)       : information for dblink
 */
int
dblink_close_scan (DBLINK_SCAN_INFO * scan_info)
{
  int error;
  T_CCI_ERROR err_buf;

  /*  note: return NO_ERROR even though the connection or stmt handle is not valid */

  if (scan_info->stmt_handle >= 0)
    {
      if ((error = cci_close_req_handle (scan_info->stmt_handle)) < 0)
	{
	  cci_get_err_msg (error, err_buf.err_msg, sizeof (err_buf.err_msg));
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DBLINK, 1, err_buf.err_msg);
	  (void) cci_disconnect (scan_info->conn_handle, &err_buf);
	  return S_ERROR;
	}
    }

  if (scan_info->conn_handle >= 0)
    {
      if ((error = cci_disconnect (scan_info->conn_handle, &err_buf)) < 0)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DBLINK, 1, err_buf.err_msg);
	  return S_ERROR;
	}
    }

  return NO_ERROR;
}

/*
 * dblink_scan_next () - get next tuple for dblink
 *   return: SCAN_CODE
 *   scan_info(in)      : information for dblink
 *   val_list(in)       : value list for derived dblink table
 */
SCAN_CODE
dblink_scan_next (DBLINK_SCAN_INFO * scan_info, val_list_node * val_list)
{
  T_CCI_ERROR err_buf;
  int col_no, col_cnt, ind, error = NO_ERROR;
  T_CCI_U_TYPE utype;
  T_CCI_BIT bit_val;		/* for bit or varbit type */
  T_CCI_DATE date_time;		/* for date or time type */
  T_CCI_DATE_TZ date_time_tz;	/* for date or time with zone */
  void *value;			/* for any other type */
  DB_VALUE cci_value = { 0 };	/* from cci interface */
  QPROC_DB_VALUE_LIST valptrp;
  T_CCI_COL_INFO *col_info = (T_CCI_COL_INFO *) scan_info->col_info;

  int codeset;

  col_cnt = scan_info->col_cnt;

  assert (scan_info->stmt_handle >= 0);

  if ((error = cci_cursor (scan_info->stmt_handle, 1, (T_CCI_CURSOR_POS) scan_info->cursor, &err_buf)) < 0)
    {
      if (error == CCI_ER_NO_MORE_DATA)
	{
	  return S_END;
	}
      else
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DBLINK, 1, err_buf.err_msg);
	  goto close_exit;
	}
    }

  /* for next scan, set cursor posioning */
  scan_info->cursor = CCI_CURSOR_CURRENT;

  if ((error = cci_fetch (scan_info->stmt_handle, &err_buf)) < 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DBLINK, 1, err_buf.err_msg);
      goto close_exit;
    }

  assert (col_info);
  assert (val_list->valp);

  if (val_list->val_cnt != col_cnt)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DBLINK_INVALID_COLUMNS_SPECIFIED, 0);
      goto close_exit;
    }

  for (valptrp = val_list->valp, col_no = 1; col_no <= col_cnt; col_no++, valptrp = valptrp->next)
    {
      DB_DATA cci_data;
      int prec = col_info[col_no - 1].precision;

      pr_clear_value (valptrp->val);

      valptrp->val->domain.general_info.is_null = 0;
      utype = dblink_get_basic_utype (CCI_GET_RESULT_INFO_TYPE (col_info, col_no));
      value = &cci_data;

      codeset = col_info[col_no - 1].charset;

      switch (utype)
	{
	case CCI_U_TYPE_NULL:
	  ind = -1;
	  break;
	case CCI_U_TYPE_BIGINT:
	case CCI_U_TYPE_INT:
	case CCI_U_TYPE_SHORT:
	case CCI_U_TYPE_FLOAT:
	case CCI_U_TYPE_DOUBLE:
	case CCI_U_TYPE_MONETARY:
	  if ((error = cci_get_data (scan_info->stmt_handle, col_no, type_map[utype], value, &ind)) < 0)
	    {
	      goto error_exit;
	    }
	  NULL_CHECK (ind);
	  (void) dblink_make_cci_value (&cci_value, utype, value, prec, ind, codeset);
	  break;

	case CCI_U_TYPE_NUMERIC:
	  if ((error = cci_get_data (scan_info->stmt_handle, col_no, type_map[utype], &value, &ind)) < 0)
	    {
	      goto error_exit;
	    }
	  NULL_CHECK (ind);
	  error = numeric_coerce_string_to_num ((char *) value, ind, (INTL_CODESET) codeset, &cci_value);
	  break;

	case CCI_U_TYPE_STRING:
	case CCI_U_TYPE_VARNCHAR:
	case CCI_U_TYPE_CHAR:
	case CCI_U_TYPE_NCHAR:
	case CCI_U_TYPE_JSON:
	  if ((error = cci_get_data (scan_info->stmt_handle, col_no, type_map[utype], &value, &ind)) < 0)
	    {
	      goto error_exit;
	    }
	  NULL_CHECK (ind);
	  if (utype == CCI_U_TYPE_JSON)
	    {
	      JSON_DOC *json_doc = NULL;

	      error = db_json_get_json_from_str ((char *) value, json_doc, ind);
	      if (error != NO_ERROR)
		{
		  goto close_exit;
		}

	      (void) db_make_json (&cci_value, json_doc, true);
	    }
	  else
	    {
	      (void) dblink_make_cci_value (&cci_value, utype, value, prec, ind, codeset);
	    }
	  break;

	case CCI_U_TYPE_BIT:
	case CCI_U_TYPE_VARBIT:
	  if ((error = cci_get_data (scan_info->stmt_handle, col_no, type_map[utype], &bit_val, &ind)) < 0)
	    {
	      goto error_exit;
	    }
	  NULL_CHECK (ind);
	  if (utype == CCI_U_TYPE_BIT)
	    {
	      /* bit_val.size * 8 : bit length for the value */
	      (void) db_make_bit (&cci_value, prec, bit_val.buf, bit_val.size * 8);
	    }
	  else
	    {
	      /* bit_val.size * 8 : bit length for the value */
	      (void) db_make_varbit (&cci_value, prec, bit_val.buf, bit_val.size * 8);
	    }
	  break;

	case CCI_U_TYPE_DATE:
	case CCI_U_TYPE_TIME:
	case CCI_U_TYPE_TIMESTAMP:
	case CCI_U_TYPE_DATETIME:
	  if ((error = cci_get_data (scan_info->stmt_handle, col_no, type_map[utype], &date_time, &ind)) < 0)
	    {
	      goto error_exit;
	    }
	  NULL_CHECK (ind);
	  error = dblink_make_date_time (utype, &cci_value, &date_time);
	  break;

	case CCI_U_TYPE_DATETIMETZ:
	case CCI_U_TYPE_DATETIMELTZ:
	case CCI_U_TYPE_TIMESTAMPTZ:
	case CCI_U_TYPE_TIMESTAMPLTZ:
	  if ((error = cci_get_data (scan_info->stmt_handle, col_no, type_map[utype], &date_time_tz, &ind)) < 0)
	    {
	      goto error_exit;
	    }
	  NULL_CHECK (ind);
	  error = dblink_make_date_time_tz (utype, &cci_value, &date_time_tz);
	  break;
	default:
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DBLINK_UNSUPPORTED_TYPE, 1, print_utype_to_string (utype));
	  goto close_exit;
	}

      if (error < 0)
	{
	  break;
	}

      if (ind == -1)
	{
	  valptrp->val->domain.general_info.is_null = 1;
	}
      else
	{
	  TP_DOMAIN dom;
	  TP_DOMAIN_STATUS status;

	  tp_domain_init (&dom, (DB_TYPE) db_value_domain_type (valptrp->val));

	  dom.precision = db_value_precision (valptrp->val);
	  dom.collation_id = db_get_string_collation (valptrp->val);
	  dom.codeset = db_get_string_codeset (valptrp->val);
	  dom.scale = db_value_scale (valptrp->val);

	  if ((status =
	       tp_value_cast_preserve_domain (&cci_value, valptrp->val, &dom, false, true)) != DOMAIN_COMPATIBLE)
	    {
	      (void) tp_domain_status_er_set (status, ARG_FILE_LINE, &cci_value, &dom);
	      goto close_exit;
	    }
	}

      if (cci_value.need_clear)
	{
	  pr_clear_value (&cci_value);
	}

    }

  if (error != NO_ERROR)
    {
      goto close_exit;
    }

  return S_SUCCESS;

error_exit:
  cci_get_err_msg (error, err_buf.err_msg, sizeof (err_buf.err_msg));
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DBLINK, 1, err_buf.err_msg);

close_exit:
  if (cci_value.need_clear)
    {
      pr_clear_value (&cci_value);
    }

  return S_ERROR;
}

/*
 * dblink_scan_reset () - reset the cursor
 *   return: SCAN_CODE
 *   scan_info(in)      : information for dblink
 */
SCAN_CODE
dblink_scan_reset (DBLINK_SCAN_INFO * scan_info)
{
  assert (scan_info->conn_handle >= 0 && scan_info->stmt_handle >= 0);

  scan_info->cursor = CCI_CURSOR_FIRST;

  return S_SUCCESS;
}
