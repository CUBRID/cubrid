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
 * cas_common_function.c 
 */

#ident "$Id$"

#include "cas_common_function.h"
#include "cas_log.h"
#include "cas_net_buf.h"
#include "cas_error.h"
#include "intl_support.h"
#include "object_primitive.h"
#include "broker_cas_cci.h"
#include "dbtype.h"

static const char *type_str_tbl[] = {
  "NULL",			/* CCI_U_TYPE_NULL */
  "CHAR",			/* CCI_U_TYPE_CHAR */
  "VARCHAR",			/* CCI_U_TYPE_STRING */

  /* TODO:
   * DB_TYPE_NCHAR and DB_TYPE_VARNCHAR will no longer be used(NCHAR was deprecated).
   * However, to maintain compatibility with previous versions, the enum list will be preserved.       
   */
  "NCHAR",			/* CCI_U_TYPE_NCHAR_DEPRECATED */
  "VARNCHAR",			/* CCI_U_TYPE_VARNCHAR_DEPRECATED */

  "BIT",			/* CCI_U_TYPE_BIT */
  "VARBIT",			/* CCI_U_TYPE_VARBIT */
  "NUMERIC",			/* CCI_U_TYPE_NUMERIC */
  "INT",			/* CCI_U_TYPE_INT */
  "SHORT",			/* CCI_U_TYPE_SHORT */
  "MONETARY",			/* CCI_U_TYPE_MONETARY */
  "FLOAT",			/* CCI_U_TYPE_FLOAT */
  "DOUBLE",			/* CCI_U_TYPE_DOUBLE */
  "DATE",			/* CCI_U_TYPE_DATE */
  "TIME",			/* CCI_U_TYPE_TIME */
  "TIMESTAMP",			/* CCI_U_TYPE_TIMESTAMP */
  "SET",			/* CCI_U_TYPE_SET */
  "MULTISET",			/* CCI_U_TYPE_MULTISET */
  "SEQUENCE",			/* CCI_U_TYPE_SEQUENCE */
  "OBJECT",			/* CCI_U_TYPE_OBJECT */
  "RESULTSET",			/* CCI_U_TYPE_RESULTSET */
  "BIGINT",			/* CCI_U_TYPE_BIGINT */
  "DATETIME",			/* CCI_U_TYPE_DATETIME */
  "BLOB",			/* CCI_U_TYPE_BLOB */
  "CLOB",			/* CCI_U_TYPE_CLOB */
  "ENUM",			/* CCI_U_TYPE_ENUM */
  "USHORT",			/* CCI_U_TYPE_USHORT */
  "UINT",			/* CCI_U_TYPE_UINT */
  "UBIGINT",			/* CCI_U_TYPE_UBIGINT */
  "TIMESTAMPTZ",		/* CCI_U_TYPE_TIMESTAMPTZ */
  "TIMESTAMPLTZ",		/* CCI_U_TYPE_TIMESTAMPLTZ */
  "DATETIMETZ",			/* CCI_U_TYPE_DATETIMETZ */
  "DATETIMELTZ",		/* CCI_U_TYPE_DATETIMELTZ */
  "TIMETZ",			/* CCI_U_TYPE_TIMETZ */
  "JSON",			/* CCI_U_TYPE_JSON */
};

void
cas_common_bind_value_print (char type, void *net_value, bool slow_log, INTL_CODESET charset)
{
  int data_size;
  void (*write2_func) (const char *, ...);
  void (*fwrite_func) (char *value, int size);

  if (slow_log)
    {
      write2_func = cas_slow_log_write2;
      fwrite_func = cas_slow_log_write_value_string;
    }
  else
    {
      write2_func = cas_log_write2_nonl;
      fwrite_func = cas_log_write_value_string;
    }

  net_arg_get_size (&data_size, net_value);
  if (data_size <= 0)
    {
      type = CCI_U_TYPE_NULL;
      data_size = 0;
    }

  switch (type)
    {
    case CCI_U_TYPE_CHAR:
    case CCI_U_TYPE_STRING:
    case CCI_U_TYPE_ENUM:
    case CCI_U_TYPE_JSON:
      {
	char *str_val;
	int val_size;
	int num_chars = 0;

	net_arg_get_str (&str_val, &val_size, net_value);
	if (val_size > 0)
	  {
	    num_chars = intl_char_count ((const unsigned char *) str_val, val_size, charset, &num_chars) - 1;
	  }
	write2_func ("(%d)", num_chars);
	fwrite_func (str_val, val_size - 1);
      }
      break;
    case CCI_U_TYPE_BIT:
    case CCI_U_TYPE_VARBIT:
    case CCI_U_TYPE_NUMERIC:
      {
	char *str_val;
	int val_size;
	net_arg_get_str (&str_val, &val_size, net_value);
	if (type != CCI_U_TYPE_NUMERIC)
	  {
	    write2_func ("(%d)", val_size);
	    fwrite_func (str_val, val_size);
	  }
	else
	  {
	    fwrite_func (str_val, val_size - 1);
	  }
      }
      break;
    case CCI_U_TYPE_BIGINT:
      {
	INT64 bi_val;
	net_arg_get_bigint (&bi_val, net_value);
	write2_func ("%lld", (long long) bi_val);
      }
      break;
    case CCI_U_TYPE_UBIGINT:
      {
	UINT64 ubi_val;
	net_arg_get_bigint ((INT64 *) (&ubi_val), net_value);
	write2_func ("%llu", (unsigned long long) ubi_val);
      }
      break;
    case CCI_U_TYPE_INT:
      {
	int i_val;
	net_arg_get_int (&i_val, net_value);
	write2_func ("%d", i_val);
      }
      break;
    case CCI_U_TYPE_UINT:
      {
	unsigned int ui_val;
	net_arg_get_int ((int *) &ui_val, net_value);
	write2_func ("%u", ui_val);
      }
      break;
    case CCI_U_TYPE_SHORT:
      {
	short s_val;
	net_arg_get_short (&s_val, net_value);
	write2_func ("%d", s_val);
      }
      break;
    case CCI_U_TYPE_USHORT:
      {
	unsigned short us_val;
	net_arg_get_short ((short *) &us_val, net_value);
	write2_func ("%u", us_val);
      }
      break;
    case CCI_U_TYPE_MONETARY:
    case CCI_U_TYPE_DOUBLE:
      {
	double d_val;
	net_arg_get_double (&d_val, net_value);
	write2_func ("%.15e", d_val);
      }
      break;
    case CCI_U_TYPE_FLOAT:
      {
	float f_val;
	net_arg_get_float (&f_val, net_value);
	write2_func ("%.6e", f_val);
      }
      break;
    case CCI_U_TYPE_DATE:
    case CCI_U_TYPE_TIME:
    case CCI_U_TYPE_TIMESTAMP:
    case CCI_U_TYPE_DATETIME:
      {
	short yr, mon, day, hh, mm, ss, ms;
	net_arg_get_datetime (&yr, &mon, &day, &hh, &mm, &ss, &ms, net_value);
	if (type == CCI_U_TYPE_DATE)
	  write2_func ("%d-%d-%d", yr, mon, day);
	else if (type == CCI_U_TYPE_TIME)
	  write2_func ("%d:%d:%d", hh, mm, ss);
	else if (type == CCI_U_TYPE_TIMESTAMP)
	  write2_func ("%d-%d-%d %d:%d:%d", yr, mon, day, hh, mm, ss);
	else
	  write2_func ("%d-%d-%d %d:%d:%d.%03d", yr, mon, day, hh, mm, ss, ms);
      }
      break;
    case CCI_U_TYPE_TIMESTAMPTZ:
    case CCI_U_TYPE_DATETIMETZ:
      {
	short yr, mon, day, hh, mm, ss, ms;
	char *tz_str_p;
	int tz_size;
	char tz_str[CCI_TZ_SIZE + 1];

	net_arg_get_datetimetz (&yr, &mon, &day, &hh, &mm, &ss, &ms, &tz_str_p, &tz_size, net_value);
	tz_size = MIN (CCI_TZ_SIZE, tz_size);
	strncpy (tz_str, tz_str_p, tz_size);
	tz_str[tz_size] = '\0';

	if (type == CCI_U_TYPE_TIMESTAMPTZ)
	  {
	    write2_func ("%d-%d-%d %d:%d:%d %s", yr, mon, day, hh, mm, ss, tz_str);
	  }
	else
	  {
	    write2_func ("%d-%d-%d %d:%d:%d.%03d %s", yr, mon, day, hh, mm, ss, ms, tz_str);
	  }
      }
      break;
    case CCI_U_TYPE_SET:
    case CCI_U_TYPE_MULTISET:
    case CCI_U_TYPE_SEQUENCE:
      {
	int remain_size = data_size;
	int ele_size;
	char ele_type;
	char *cur_p = (char *) net_value;
	char print_comma = 0;

	cur_p += 4;
	ele_type = *cur_p;
	cur_p++;
	remain_size--;

	if (ele_type <= CCI_U_TYPE_FIRST || ele_type > CCI_U_TYPE_LAST)
	  break;

	write2_func ("(%s) {", type_str_tbl[(int) ele_type]);

	while (remain_size > 0)
	  {
	    net_arg_get_size (&ele_size, cur_p);
	    if (ele_size + 4 > remain_size)
	      break;
	    if (print_comma)
	      write2_func (", ");
	    else
	      print_comma = 1;
	    cas_common_bind_value_print (ele_type, cur_p, slow_log, charset);
	    ele_size += 4;
	    cur_p += ele_size;
	    remain_size -= ele_size;
	  }

	write2_func ("}");
      }
      break;
    case CCI_U_TYPE_OBJECT:
      {
	int pageid;
	short slotid, volid;

	net_arg_get_cci_object (&pageid, &slotid, &volid, net_value);
	write2_func ("%d|%d|%d", pageid, slotid, volid);
      }
      break;
    case CCI_U_TYPE_BLOB:
    case CCI_U_TYPE_CLOB:
      {
	DB_VALUE db_val;
	DB_ELO *db_elo;
	net_arg_get_lob_value (&db_val, net_value);
	db_elo = db_get_elo (&db_val);
	if (db_elo)
	  {
	    write2_func ("%s|%lld|%s|%s|%d", (type == CCI_U_TYPE_BLOB) ? "BLOB" : "CLOB", db_elo->size, db_elo->locator,
			 db_elo->meta_data, db_elo->type);
	  }
	else
	  {
	    write2_func ("invalid LOB");
	  }

	db_value_clear (&db_val);
      }
      break;
    default:
      write2_func ("NULL");
      break;
    }
}

void
cas_common_bind_value_log (struct timeval *log_time, int start, int argc, void **argv, int param_size, char *param_mode,
			   unsigned int query_seq_num, bool slow_log, INTL_CODESET charset)
{
  int idx;
  char type;
  int num_bind;
  void *net_value;
  const char *param_mode_str;
  void (*write2_func) (const char *, ...);

  if (slow_log)
    {
      write2_func = cas_slow_log_write2;
    }
  else
    {
      write2_func = cas_log_write2_nonl;
    }

  num_bind = 1;
  idx = start;

  while (idx < argc)
    {
      net_arg_get_char (type, argv[idx++]);
      net_value = argv[idx++];

      param_mode_str = "";
      if (param_mode != NULL && param_size >= num_bind)
	{
	  if (param_mode[num_bind - 1] == CCI_PARAM_MODE_IN)
	    param_mode_str = "(IN) ";
	  else if (param_mode[num_bind - 1] == CCI_PARAM_MODE_OUT)
	    param_mode_str = "(OUT) ";
	  else if (param_mode[num_bind - 1] == CCI_PARAM_MODE_INOUT)
	    param_mode_str = "(INOUT) ";
	}

      if (slow_log)
	{
	  cas_slow_log_write (log_time, query_seq_num, false, "bind %d %s: ", num_bind++, param_mode_str);
	}
      else
	{
	  cas_log_write_nonl (query_seq_num, false, "bind %d %s: ", num_bind++, param_mode_str);
	}

      if (type > CCI_U_TYPE_FIRST && type <= CCI_U_TYPE_LAST)
	{
	  /* Since the existing test code uses CCI_U_TYPE_NCHAR and CCI_U_TYPE_VARNCHAR, the assert() is commented out. */
	  //assert (type != CCI_U_TYPE_NCHAR_DEPRECATED && type != CCI_U_TYPE_VARNCHAR_DEPRECATED);
	  write2_func ("%s ", type_str_tbl[(int) type]);
	  cas_common_bind_value_print (type, net_value, slow_log, charset);
	}
      else
	{
	  write2_func ("NULL");
	}
      write2_func ("\n");
    }
}

FN_RETURN
fn_not_supported (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info)
{
  ERROR_INFO_SET (CAS_ER_NOT_IMPLEMENTED, CAS_ERROR_INDICATOR);
  NET_BUF_ERR_SET (net_buf);
  return FN_KEEP_CONN;
}

FN_RETURN
fn_deprecated (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info)
{
#if defined(CAS_FOR_DBMS)
  ERROR_INFO_SET (CAS_ER_NOT_IMPLEMENTED, CAS_ERROR_INDICATOR);
  NET_BUF_ERR_SET (net_buf);
#else /* CAS_FOR_DBMS */
  net_buf_cp_int (net_buf, CAS_ER_NOT_IMPLEMENTED, NULL);
#endif /* CAS_FOR_DBMS */
  return FN_KEEP_CONN;
}
