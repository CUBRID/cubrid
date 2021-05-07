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

#include "config.h"

#include <string.h>

#include "dblink_scan.h"

#include "network_interface_sr.h"	/* TODO: should not be here */

#ifndef	SERVER_MODE
#include "object_accessor.h"
#include "dbi.h"
#include "authenticate.h"
#endif

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
  0,				/* CCI_U_TYPE_BLOB */
  0,				/* CCI_U_TYPE_CLOB */
  CCI_A_TYPE_STR,		/* CCI_U_TYPE_ENUM */
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

static void
dblink_make_date_time (T_CCI_U_TYPE utype, DB_VALUE * value_p, T_CCI_DATE * date_time)
{
  DB_TIME t_time;
  DB_DATE t_date;
  DB_DATETIME t_datetime;
  DB_TIMESTAMP t_timestamp;

  switch (utype)
    {
    case CCI_U_TYPE_TIME:
      db_make_time (value_p, date_time->hh, date_time->mm, date_time->ss);
      break;
    case CCI_U_TYPE_DATE:
      db_make_date (value_p, date_time->mon, date_time->day, date_time->yr);
      break;
    case CCI_U_TYPE_DATETIME:
      db_datetime_encode (&t_datetime, date_time->mon, date_time->day, date_time->yr,
			  date_time->hh, date_time->mm, date_time->ss, date_time->ms);
      db_make_datetime (value_p, &t_datetime);
      break;
    case CCI_U_TYPE_TIMESTAMP:
      db_time_encode (&t_time, date_time->hh, date_time->mm, date_time->ss);
      db_date_encode (&t_date, date_time->mon, date_time->day, date_time->yr);
      db_timestamp_encode (&t_timestamp, &t_date, &t_time);
      db_make_timestamp (value_p, t_timestamp);
      break;
    default:
      break;
    }
}

static void
dblink_make_date_time_tz (T_CCI_U_TYPE utype, DB_VALUE * value_p, T_CCI_DATE_TZ * date_time_tz)
{
  DB_TIME t_time;
  DB_DATE t_date;
  DB_DATETIME t_datetime;
  DB_DATETIMETZ tz_datetime;
  DB_TIMESTAMPTZ tz_timestamp;
  TZ_REGION region;

  tz_get_session_tz_region (&region);

  switch (utype)
    {
    case CCI_U_TYPE_TIMESTAMPTZ:
      db_time_encode (&t_time, date_time_tz->hh, date_time_tz->mm, date_time_tz->ss);
      db_date_encode (&t_date, date_time_tz->mon, date_time_tz->day, date_time_tz->yr);
      tz_create_timestamptz (&t_date, &t_time, date_time_tz->tz, strlen (date_time_tz->tz), &region, &tz_timestamp,
			     NULL);
      db_make_timestamptz (value_p, &tz_timestamp);
      break;
    case CCI_U_TYPE_DATETIMETZ:
      db_datetime_encode (&t_datetime, date_time_tz->mon, date_time_tz->day, date_time_tz->yr,
			  date_time_tz->hh, date_time_tz->mm, date_time_tz->ss, date_time_tz->ms);
      tz_create_datetimetz (&t_datetime, date_time_tz->tz, strlen (date_time_tz->tz), &region, &tz_datetime, NULL);
      db_make_datetimetz (value_p, &tz_datetime);
      break;
    case CCI_U_TYPE_TIMESTAMPLTZ:
      db_time_encode (&t_time, date_time_tz->hh, date_time_tz->mm, date_time_tz->ss);
      db_date_encode (&t_date, date_time_tz->mon, date_time_tz->day, date_time_tz->yr);
      tz_create_timestamptz (&t_date, &t_time, date_time_tz->tz, strlen (date_time_tz->tz), &region, &tz_timestamp,
			     NULL);
      db_make_timestampltz (value_p, tz_timestamp.timestamp);
      break;
    case CCI_U_TYPE_DATETIMELTZ:
      db_datetime_encode (&t_datetime, date_time_tz->mon, date_time_tz->day, date_time_tz->yr,
			  date_time_tz->hh, date_time_tz->mm, date_time_tz->ss, date_time_tz->ms);
      tz_create_datetimetz (&t_datetime, date_time_tz->tz, strlen (date_time_tz->tz), &region, &tz_datetime, NULL);
      db_make_datetimeltz (value_p, &tz_datetime.datetime);
      break;
    default:
      break;
    }
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
dblink_open_scan (DBLINK_SCAN_INFO * scan_info, char *conn_url, char *user_name, char *password, char *sql_text)
{
  int ret, error = NO_ERROR;
  T_CCI_ERROR err_buf;
  T_CCI_CUBRID_STMT stmt_type;

  scan_info->conn_handle = cci_connect_with_url_ex (conn_url, user_name, password, &err_buf);

  if (scan_info->conn_handle < 0)
    {
      error = err_buf.err_code;
    }
  else
    {
      scan_info->stmt_handle = cci_prepare_and_execute (scan_info->conn_handle, sql_text, 0, &ret, &err_buf);
      if (ret < 0)
	{
	  error = err_buf.err_code;
	}
      else
	{
	  scan_info->col_info = (void *) cci_get_result_info (scan_info->stmt_handle, &stmt_type, &scan_info->col_cnt);
	  if (scan_info->col_info == NULL)
	    {
	      error = S_ERROR;
	    }
	  scan_info->cursor = CCI_CURSOR_FIRST;
	}
    }

  return error;
}

/*
 * dblink_close_scan () -
 *   return: int
 *   scan_info(in)       : information for dblink
 */
int
dblink_close_scan (DBLINK_SCAN_INFO * scan_info)
{
  T_CCI_ERROR err_buf;
  int error;

  if ((error = cci_close_req_handle (scan_info->stmt_handle)) < 0)
    {
      return error;
    }

  if ((error = cci_disconnect (scan_info->conn_handle, &err_buf)) < 0)
    {
      return error;
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
  int col_no, col_cnt, ind, error;
  T_CCI_U_TYPE utype;
  T_CCI_BIT bit_val;		/* for bit or varbit type */
  T_CCI_DATE date_time;		/* for date or time type */
  T_CCI_DATE_TZ date_time_tz;	/* for date or time with zone */
  void *value;			/* for any other type */
  QPROC_DB_VALUE_LIST valptrp;
  T_CCI_COL_INFO *col_info = (T_CCI_COL_INFO *) scan_info->col_info;

  INTL_CODESET codeset;

  col_cnt = scan_info->col_cnt;

  if (scan_info->stmt_handle < 0)
    {
      return S_ERROR;
    }

  if ((error = cci_cursor (scan_info->stmt_handle, 1, (T_CCI_CURSOR_POS) scan_info->cursor, &err_buf)) < 0)
    {
      if (error == CCI_ER_NO_MORE_DATA)
	{
	  return S_END;
	}
      else
	{
	  return S_ERROR;
	}
    }

  /* for next scan, set cursor posioning */
  scan_info->cursor = CCI_CURSOR_CURRENT;

  if ((error = cci_fetch (scan_info->stmt_handle, &err_buf)) < 0)
    {
      return S_ERROR;
    }

  assert (col_info);
  assert (val_list->valp);

  for (valptrp = val_list->valp, col_no = 1; col_no <= col_cnt; col_no++, valptrp = valptrp->next)
    {
      valptrp->val->domain.general_info.is_null = 0;
      utype = dblink_get_basic_utype (CCI_GET_RESULT_INFO_TYPE (col_info, col_no));
      value = &valptrp->val->data;
      switch (utype)
	{
	case CCI_U_TYPE_NULL:
	  ind = -1;
	  break;
	case CCI_U_TYPE_BIGINT:
	case CCI_U_TYPE_INT:
	case CCI_U_TYPE_FLOAT:
	case CCI_U_TYPE_DOUBLE:
	case CCI_U_TYPE_MONETARY:
	  if (cci_get_data (scan_info->stmt_handle, col_no, type_map[utype], value, &ind) < 0)
	    {
	      return S_ERROR;
	    }
	  NULL_CHECK (ind);
	  break;

	case CCI_U_TYPE_NUMERIC:
	  if (cci_get_data (scan_info->stmt_handle, col_no, type_map[utype], &value, &ind) < 0)
	    {
	      return S_ERROR;
	    }
	  NULL_CHECK (ind);
	  codeset = (INTL_CODESET)valptrp->val->data.enumeration.str_val.info.codeset;
	  numeric_coerce_string_to_num ((char *) value, ind, codeset, valptrp->val);
	  break;

	case CCI_U_TYPE_STRING:
	case CCI_U_TYPE_VARNCHAR:
	case CCI_U_TYPE_CHAR:
	case CCI_U_TYPE_NCHAR:
	case CCI_U_TYPE_ENUM:
	  /* for enum type, it will be coerced to string type in the future */
	case CCI_U_TYPE_JSON:
	  if (cci_get_data (scan_info->stmt_handle, col_no, type_map[utype], &value, &ind) < 0)
	    {
	      return S_ERROR;
	    }
	  NULL_CHECK (ind);	  
  	  valptrp->val->data.ch.medium.buf = (char *) value;
	  valptrp->val->data.ch.medium.size = ind;
	  if (utype == CCI_U_TYPE_ENUM)
	    {
	      int collation = valptrp->val->domain.char_info.collation_id;

	      codeset = (INTL_CODESET)valptrp->val->data.enumeration.str_val.info.codeset;
	      db_make_enumeration(valptrp->val, 1, (char *) value, ind, codeset, collation);
	    }
	  else if (utype == CCI_U_TYPE_JSON)
	    {
	      db_json_val_from_str ((char *)value, ind, valptrp->val);
	    }
	  break;

	case CCI_U_TYPE_BIT:
	case CCI_U_TYPE_VARBIT:
	  if (cci_get_data (scan_info->stmt_handle, col_no, type_map[utype], &bit_val, &ind) < 0)
	    {
	      return S_ERROR;
	    }
	  NULL_CHECK (ind);
	  if (utype == CCI_U_TYPE_BIT)
	    {
	      /* bit_val.size * 8 : bit length for the value */
	      db_make_bit (valptrp->val, bit_val.size * 8, bit_val.buf, col_info[col_no - 1].precision);
	    }
	  else
	    {
	      /* bit_val.size * 8 : bit length for the value */
	      db_make_varbit (valptrp->val, bit_val.size * 8, bit_val.buf, col_info[col_no - 1].precision);
	    }
	  break;

	case CCI_U_TYPE_DATE:
	case CCI_U_TYPE_TIME:
	case CCI_U_TYPE_TIMESTAMP:
	case CCI_U_TYPE_DATETIME:
	  if (cci_get_data (scan_info->stmt_handle, col_no, type_map[utype], &date_time, &ind) < 0)
	    {
	      return S_ERROR;
	    }
	  NULL_CHECK (ind);
	  dblink_make_date_time (utype, valptrp->val, &date_time);
	  break;

	case CCI_U_TYPE_DATETIMETZ:
	case CCI_U_TYPE_DATETIMELTZ:
	case CCI_U_TYPE_TIMESTAMPTZ:
	case CCI_U_TYPE_TIMESTAMPLTZ:
	  if (cci_get_data (scan_info->stmt_handle, col_no, type_map[utype], &date_time_tz, &ind) < 0)
	    {
	      return S_ERROR;
	      break;
	    }
	  NULL_CHECK (ind);
	  dblink_make_date_time_tz (utype, valptrp->val, &date_time_tz);
	  break;
	default:
	  ind = -1;
	  break;
	}
      if (ind == -1)
	{
	  valptrp->val->domain.general_info.is_null = 1;
	}
    }

  return S_SUCCESS;
}

/*
 * dblink_scan_reset () - reset the cursor
 *   return: SCAN_CODE
 *   scan_info(in)      : information for dblink
 */
SCAN_CODE
dblink_scan_reset (DBLINK_SCAN_INFO * scan_info)
{
  T_CCI_ERROR err_buf;

  if (scan_info->conn_handle >= 0 && scan_info->stmt_handle >= 0)
    {
      scan_info->cursor = CCI_CURSOR_FIRST;
      return S_SUCCESS;
    }

  return S_ERROR;
}
