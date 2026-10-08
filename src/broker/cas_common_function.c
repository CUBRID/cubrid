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
#include "broker_util.h"

/* the SQL log line after the time, before the value, e.g. " (3) bind 1 (IN) : INT " */
#define BIND_HEADER_FMT " (%u) bind %d %s: %s "

/* the header and the value in one format, or the value alone for a SET element (header == NULL) */
#define BIND_PRINT(line, header, value_fmt, ...) \
  ((header) != NULL \
   ? bind_line_printf ((line), BIND_HEADER_FMT value_fmt, (header)->query_seq_num, (header)->bind_num, (header)->mode, \
		       (header)->type_name, __VA_ARGS__) \
   : bind_line_printf ((line), value_fmt, __VA_ARGS__))

/* the header alone, skipped for a SET element (header == NULL) */
#define BIND_PRINT_HEADER(line, header) \
  do { \
    if ((header) != NULL) \
      { \
	BIND_PRINT ((line), (header), "%s", ""); \
      } \
  } while (0)

/* a bind log line is assembled here and usually written with a single log write */
typedef struct bind_line BIND_LINE;
struct bind_line
{
  void (*fwrite_func) (char *value, int size);
  int len;
  char buf[CAS_LOG_BUFFER_SIZE];
};

typedef struct bind_header BIND_HEADER;
struct bind_header
{
  unsigned int query_seq_num;
  int bind_num;
  const char *mode;
  const char *type_name;
};

typedef void (*BIND_WRITE_FN) (BIND_LINE * line, const BIND_HEADER * header, char type, void *net_value,
			       INTL_CODESET charset);

static void bind_line_flush (BIND_LINE * line);
static void bind_line_printf (BIND_LINE * line, const char *fmt, ...);
static void bind_line_append (BIND_LINE * line, const char *p, int n);
static void bind_value_write (BIND_LINE * line, const BIND_HEADER * header, char type, void *net_value,
			      INTL_CODESET charset);
static void bind_write_null (BIND_LINE * line, const BIND_HEADER * header, char type, void *net_value,
			     INTL_CODESET charset);
static void bind_write_string (BIND_LINE * line, const BIND_HEADER * header, char type, void *net_value,
			       INTL_CODESET charset);
static void bind_write_bit (BIND_LINE * line, const BIND_HEADER * header, char type, void *net_value,
			    INTL_CODESET charset);
static void bind_write_numeric (BIND_LINE * line, const BIND_HEADER * header, char type, void *net_value,
				INTL_CODESET charset);
static void bind_write_int (BIND_LINE * line, const BIND_HEADER * header, char type, void *net_value,
			    INTL_CODESET charset);
static void bind_write_bigint (BIND_LINE * line, const BIND_HEADER * header, char type, void *net_value,
			       INTL_CODESET charset);
static void bind_write_short (BIND_LINE * line, const BIND_HEADER * header, char type, void *net_value,
			      INTL_CODESET charset);
static void bind_write_double (BIND_LINE * line, const BIND_HEADER * header, char type, void *net_value,
			       INTL_CODESET charset);
static void bind_write_float (BIND_LINE * line, const BIND_HEADER * header, char type, void *net_value,
			      INTL_CODESET charset);
static void bind_write_datetime (BIND_LINE * line, const BIND_HEADER * header, char type, void *net_value,
				 INTL_CODESET charset);
static void bind_write_datetimetz (BIND_LINE * line, const BIND_HEADER * header, char type, void *net_value,
				   INTL_CODESET charset);
static void bind_write_set (BIND_LINE * line, const BIND_HEADER * header, char type, void *net_value,
			    INTL_CODESET charset);
static void bind_write_object (BIND_LINE * line, const BIND_HEADER * header, char type, void *net_value,
			       INTL_CODESET charset);
static void bind_write_lob (BIND_LINE * line, const BIND_HEADER * header, char type, void *net_value,
			    INTL_CODESET charset);

/* one entry per CCI_U_TYPE, in enum order. A type without a writer is logged as NULL. */
/* *INDENT-OFF* */
static const struct
{
  const char *name;
  BIND_WRITE_FN write_func;
} bind_type_tbl[] = {
  {"NULL", NULL},			/* CCI_U_TYPE_NULL */
  {"CHAR", bind_write_string},		/* CCI_U_TYPE_CHAR */
  {"VARCHAR", bind_write_string},	/* CCI_U_TYPE_STRING */

  /* TODO:
   * DB_TYPE_NCHAR and DB_TYPE_VARNCHAR will no longer be used(NCHAR was deprecated).
   * However, to maintain compatibility with previous versions, the enum list will be preserved.
   */
  {"NCHAR", NULL},			/* CCI_U_TYPE_NCHAR_DEPRECATED */
  {"VARNCHAR", NULL},			/* CCI_U_TYPE_VARNCHAR_DEPRECATED */

  {"BIT", bind_write_bit},		/* CCI_U_TYPE_BIT */
  {"VARBIT", bind_write_bit},		/* CCI_U_TYPE_VARBIT */
  {"NUMERIC", bind_write_numeric},	/* CCI_U_TYPE_NUMERIC */
  {"INT", bind_write_int},		/* CCI_U_TYPE_INT */
  {"SHORT", bind_write_short},		/* CCI_U_TYPE_SHORT */
  {"MONETARY", bind_write_double},	/* CCI_U_TYPE_MONETARY */
  {"FLOAT", bind_write_float},		/* CCI_U_TYPE_FLOAT */
  {"DOUBLE", bind_write_double},	/* CCI_U_TYPE_DOUBLE */
  {"DATE", bind_write_datetime},	/* CCI_U_TYPE_DATE */
  {"TIME", bind_write_datetime},	/* CCI_U_TYPE_TIME */
  {"TIMESTAMP", bind_write_datetime},	/* CCI_U_TYPE_TIMESTAMP */
  {"SET", bind_write_set},		/* CCI_U_TYPE_SET */
  {"MULTISET", bind_write_set},		/* CCI_U_TYPE_MULTISET */
  {"SEQUENCE", bind_write_set},		/* CCI_U_TYPE_SEQUENCE */
  {"OBJECT", bind_write_object},	/* CCI_U_TYPE_OBJECT */
  {"RESULTSET", NULL},			/* CCI_U_TYPE_RESULTSET */
  {"BIGINT", bind_write_bigint},	/* CCI_U_TYPE_BIGINT */
  {"DATETIME", bind_write_datetime},	/* CCI_U_TYPE_DATETIME */
  {"BLOB", bind_write_lob},		/* CCI_U_TYPE_BLOB */
  {"CLOB", bind_write_lob},		/* CCI_U_TYPE_CLOB */
  {"ENUM", bind_write_string},		/* CCI_U_TYPE_ENUM */
  {"USHORT", bind_write_short},		/* CCI_U_TYPE_USHORT */
  {"UINT", bind_write_int},		/* CCI_U_TYPE_UINT */
  {"UBIGINT", bind_write_bigint},	/* CCI_U_TYPE_UBIGINT */
  {"TIMESTAMPTZ", bind_write_datetimetz},	/* CCI_U_TYPE_TIMESTAMPTZ */
  {"TIMESTAMPLTZ", NULL},		/* CCI_U_TYPE_TIMESTAMPLTZ */
  {"DATETIMETZ", bind_write_datetimetz},	/* CCI_U_TYPE_DATETIMETZ */
  {"DATETIMELTZ", NULL},		/* CCI_U_TYPE_DATETIMELTZ */
  {"TIMETZ", NULL},			/* CCI_U_TYPE_TIMETZ */
  {"JSON", bind_write_string},		/* CCI_U_TYPE_JSON */
};
/* *INDENT-ON* */

static_assert (sizeof (bind_type_tbl) / sizeof (bind_type_tbl[0]) == CCI_U_TYPE_LAST + 1,
	       "bind_type_tbl must have one entry per CCI_U_TYPE");

static void
bind_line_flush (BIND_LINE * line)
{
  if (line->len > 0)
    {
      line->fwrite_func (line->buf, line->len);
      line->len = 0;
    }
}

static void
bind_line_printf (BIND_LINE * line, const char *fmt, ...)
{
  va_list ap;
  int free_size = CAS_LOG_BUFFER_SIZE - line->len;
  int n;

  va_start (ap, fmt);
  n = vsnprintf (line->buf + line->len, free_size, fmt, ap);
  va_end (ap);
  if (n < 0)
    {
      return;
    }
  if (n >= free_size)
    {
      /* the result is longer than the free size, so write out what is assembled to empty the buffer and format again */
      bind_line_flush (line);
      va_start (ap, fmt);
      n = vsnprintf (line->buf, CAS_LOG_BUFFER_SIZE, fmt, ap);
      va_end (ap);
      if (n < 0)
	{
	  return;
	}
      if (n >= CAS_LOG_BUFFER_SIZE)
	{
	  n = CAS_LOG_BUFFER_SIZE - 1;
	}
    }
  line->len += n;
}

static void
bind_line_append (BIND_LINE * line, const char *p, int n)
{
  int free_size = CAS_LOG_BUFFER_SIZE - line->len;

  if (n <= 0)
    {
      return;
    }
  if (n <= free_size)
    {
      memcpy (line->buf + line->len, p, n);
      line->len += n;
      return;
    }

  /* a value longer than the free size is written as it is, without copying */
  bind_line_flush (line);
  line->fwrite_func ((char *) p, n);
}

static void
bind_value_write (BIND_LINE * line, const BIND_HEADER * header, char type, void *net_value, INTL_CODESET charset)
{
  int data_size;
  BIND_WRITE_FN write_func = NULL;

  net_arg_get_size (&data_size, net_value);
  if (data_size > 0 && type > CCI_U_TYPE_FIRST && type <= CCI_U_TYPE_LAST)
    {
      write_func = bind_type_tbl[(int) type].write_func;
    }
  if (write_func == NULL)
    {
      write_func = bind_write_null;
    }
  write_func (line, header, type, net_value, charset);
}

static void
bind_write_null (BIND_LINE * line, const BIND_HEADER * header, char type, void *net_value, INTL_CODESET charset)
{
  BIND_PRINT (line, header, "%s", "NULL");
}

static void
bind_write_string (BIND_LINE * line, const BIND_HEADER * header, char type, void *net_value, INTL_CODESET charset)
{
  char *str_val;
  int val_size;
  int num_chars = 0;

  net_arg_get_str (&str_val, &val_size, net_value);
  if (val_size > 0)
    {
      /* CAS protocol: string payload carries a trailing NUL that is counted in val_size. */
      assert (str_val[val_size - 1] == '\0');
      intl_char_count ((const unsigned char *) str_val, val_size - 1, charset, &num_chars);
    }
  BIND_PRINT (line, header, "(%d)", num_chars);
  bind_line_append (line, str_val, val_size - 1);
}

static void
bind_write_bit (BIND_LINE * line, const BIND_HEADER * header, char type, void *net_value, INTL_CODESET charset)
{
  char *str_val;
  int val_size;

  net_arg_get_str (&str_val, &val_size, net_value);
  BIND_PRINT (line, header, "(%d)", val_size);
  bind_line_append (line, str_val, val_size);
}

static void
bind_write_numeric (BIND_LINE * line, const BIND_HEADER * header, char type, void *net_value, INTL_CODESET charset)
{
  char *str_val;
  int val_size;

  net_arg_get_str (&str_val, &val_size, net_value);
  BIND_PRINT_HEADER (line, header);
  bind_line_append (line, str_val, val_size - 1);
}

static void
bind_write_int (BIND_LINE * line, const BIND_HEADER * header, char type, void *net_value, INTL_CODESET charset)
{
  int i_val;

  net_arg_get_int (&i_val, net_value);
  if (type == CCI_U_TYPE_UINT)
    {
      BIND_PRINT (line, header, "%u", (unsigned int) i_val);
    }
  else
    {
      BIND_PRINT (line, header, "%d", i_val);
    }
}

static void
bind_write_bigint (BIND_LINE * line, const BIND_HEADER * header, char type, void *net_value, INTL_CODESET charset)
{
  INT64 bi_val;

  net_arg_get_bigint (&bi_val, net_value);
  if (type == CCI_U_TYPE_UBIGINT)
    {
      BIND_PRINT (line, header, "%llu", (unsigned long long) bi_val);
    }
  else
    {
      BIND_PRINT (line, header, "%lld", (long long) bi_val);
    }
}

static void
bind_write_short (BIND_LINE * line, const BIND_HEADER * header, char type, void *net_value, INTL_CODESET charset)
{
  short s_val;

  net_arg_get_short (&s_val, net_value);
  if (type == CCI_U_TYPE_USHORT)
    {
      BIND_PRINT (line, header, "%u", (unsigned short) s_val);
    }
  else
    {
      BIND_PRINT (line, header, "%d", s_val);
    }
}

static void
bind_write_double (BIND_LINE * line, const BIND_HEADER * header, char type, void *net_value, INTL_CODESET charset)
{
  double d_val;

  net_arg_get_double (&d_val, net_value);
  BIND_PRINT (line, header, "%.15e", d_val);
}

static void
bind_write_float (BIND_LINE * line, const BIND_HEADER * header, char type, void *net_value, INTL_CODESET charset)
{
  float f_val;

  net_arg_get_float (&f_val, net_value);
  BIND_PRINT (line, header, "%.6e", f_val);
}

static void
bind_write_datetime (BIND_LINE * line, const BIND_HEADER * header, char type, void *net_value, INTL_CODESET charset)
{
  short yr, mon, day, hh, mm, ss, ms;

  net_arg_get_datetime (&yr, &mon, &day, &hh, &mm, &ss, &ms, net_value);
  if (type == CCI_U_TYPE_DATE)
    {
      BIND_PRINT (line, header, "%d-%d-%d", yr, mon, day);
    }
  else if (type == CCI_U_TYPE_TIME)
    {
      BIND_PRINT (line, header, "%d:%d:%d", hh, mm, ss);
    }
  else if (type == CCI_U_TYPE_TIMESTAMP)
    {
      BIND_PRINT (line, header, "%d-%d-%d %d:%d:%d", yr, mon, day, hh, mm, ss);
    }
  else
    {
      BIND_PRINT (line, header, "%d-%d-%d %d:%d:%d.%03d", yr, mon, day, hh, mm, ss, ms);
    }
}

static void
bind_write_datetimetz (BIND_LINE * line, const BIND_HEADER * header, char type, void *net_value, INTL_CODESET charset)
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
      BIND_PRINT (line, header, "%d-%d-%d %d:%d:%d %s", yr, mon, day, hh, mm, ss, tz_str);
    }
  else
    {
      BIND_PRINT (line, header, "%d-%d-%d %d:%d:%d.%03d %s", yr, mon, day, hh, mm, ss, ms, tz_str);
    }
}

static void
bind_write_set (BIND_LINE * line, const BIND_HEADER * header, char type, void *net_value, INTL_CODESET charset)
{
  int data_size;
  int remain_size;
  int ele_size;
  char ele_type;
  char *cur_p = (char *) net_value;
  bool print_comma = false;

  net_arg_get_size (&data_size, net_value);
  remain_size = data_size;
  cur_p += 4;
  ele_type = *cur_p;
  cur_p++;
  remain_size--;

  if (ele_type <= CCI_U_TYPE_FIRST || ele_type > CCI_U_TYPE_LAST)
    {
      BIND_PRINT_HEADER (line, header);
      return;
    }

  BIND_PRINT (line, header, "(%s) {", bind_type_tbl[(int) ele_type].name);

  while (remain_size > 0)
    {
      net_arg_get_size (&ele_size, cur_p);
      if (ele_size + 4 > remain_size)
	{
	  break;
	}
      if (print_comma)
	{
	  bind_line_append (line, ", ", 2);
	}
      else
	{
	  print_comma = true;
	}
      bind_value_write (line, NULL, ele_type, cur_p, charset);
      ele_size += 4;
      cur_p += ele_size;
      remain_size -= ele_size;
    }

  bind_line_append (line, "}", 1);
}

static void
bind_write_object (BIND_LINE * line, const BIND_HEADER * header, char type, void *net_value, INTL_CODESET charset)
{
  int pageid;
  short slotid, volid;

  net_arg_get_cci_object (&pageid, &slotid, &volid, net_value);
  BIND_PRINT (line, header, "%d|%d|%d", pageid, slotid, volid);
}

static void
bind_write_lob (BIND_LINE * line, const BIND_HEADER * header, char type, void *net_value, INTL_CODESET charset)
{
  DB_VALUE db_val;
  DB_ELO *db_elo;

  net_arg_get_lob_value (&db_val, net_value);
  db_elo = db_get_elo (&db_val);
  /* format the value apart from the header so the length limit applies to the value alone */
  BIND_PRINT_HEADER (line, header);
  if (db_elo)
    {
      bind_line_printf (line, "%s|%lld|%s|%s|%d", (type == CCI_U_TYPE_BLOB) ? "BLOB" : "CLOB", db_elo->size,
			db_elo->locator, db_elo->meta_data, db_elo->type);
    }
  else
    {
      bind_line_printf (line, "%s", "invalid LOB");
    }
  db_value_clear (&db_val);
}

void
cas_common_bind_value_log (struct timeval *log_time, int start, int argc, void **argv, int param_size, char *param_mode,
			   unsigned int query_seq_num, bool slow_log, INTL_CODESET charset)
{
  BIND_LINE line;
  BIND_HEADER header;
  int idx;
  char type;
  void *net_value;

  if (slow_log)
    {
      line.fwrite_func = cas_slow_log_write_value_string;
    }
  else
    {
      line.fwrite_func = cas_log_write_value_string;
    }
  header.query_seq_num = query_seq_num;
  header.bind_num = 1;
  idx = start;

  while (idx < argc)
    {
      net_arg_get_char (type, argv[idx++]);
      net_value = argv[idx++];

      header.mode = "";
      if (param_mode != NULL && param_size >= header.bind_num)
	{
	  if (param_mode[header.bind_num - 1] == CCI_PARAM_MODE_IN)
	    header.mode = "(IN) ";
	  else if (param_mode[header.bind_num - 1] == CCI_PARAM_MODE_OUT)
	    header.mode = "(OUT) ";
	  else if (param_mode[header.bind_num - 1] == CCI_PARAM_MODE_INOUT)
	    header.mode = "(INOUT) ";
	}

      /* the SQL log takes the current time and the slow log takes the query start time */
      line.len = ut_time_string (line.buf, slow_log ? log_time : NULL);

      if (type > CCI_U_TYPE_FIRST && type <= CCI_U_TYPE_LAST)
	{
	  /* Since the existing test code uses CCI_U_TYPE_NCHAR and CCI_U_TYPE_VARNCHAR, the assert() is commented out. */
	  //assert (type != CCI_U_TYPE_NCHAR_DEPRECATED && type != CCI_U_TYPE_VARNCHAR_DEPRECATED);
	  header.type_name = bind_type_tbl[(int) type].name;
	  bind_value_write (&line, &header, type, net_value, charset);
	}
      else
	{
	  bind_line_printf (&line, " (%u) bind %d %s: NULL", header.query_seq_num, header.bind_num, header.mode);
	}
      bind_line_append (&line, "\n", 1);
      bind_line_flush (&line);
      header.bind_num++;
    }

  /*
   * The bind lines above skip the per-line flush,
   * so flush them here in one call before the statement is executed.
   */
  if (!slow_log)
    {
      cas_log_flush_if_needed ();
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
