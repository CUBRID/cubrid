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
 * cas_net_buf.c -
 */

#ident "$Id$"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#if defined(WINDOWS)
#include <winsock2.h>
#include <windows.h>
#else /* WINDOWS */
#include <arpa/inet.h>
#endif /* WINDOWS */

#include "cas.h"
#include "cas_common.h"
#include "cas_net_buf.h"
#include "cas_util.h"
#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
#include "dbi.h"
#else
#include "cas_dbms_util.h"
#endif
#include "error_code.h"
#include "dbtype.h"
#include "byte_order.h"

static int net_buf_realloc (T_NET_BUF * net_buf, int size);

void
net_buf_init (T_NET_BUF * net_buf, T_BROKER_VERSION client_version)
{
  net_buf->data = NULL;
  net_buf->alloc_size = 0;
  net_buf->data_size = 0;
  net_buf->err_code = 0;
  net_buf->post_file_size = 0;
  net_buf->post_send_file = NULL;
  net_buf->client_version = client_version;
}

void
net_buf_clear (T_NET_BUF * net_buf)
{
  net_buf->data_size = 0;
  net_buf->err_code = 0;
  net_buf->post_file_size = 0;
  FREE_MEM (net_buf->post_send_file);
}

void
net_buf_destroy (T_NET_BUF * net_buf)
{
  FREE_MEM (net_buf->data);
  net_buf->alloc_size = 0;
  net_buf_clear (net_buf);
}

int
net_buf_cp_post_send_file (T_NET_BUF * net_buf, int size, char *filename)
{
  FREE_MEM (net_buf->post_send_file);
  ALLOC_COPY (net_buf->post_send_file, filename);
  if (net_buf->post_send_file == NULL)
    {
      net_buf->err_code = CAS_ER_NO_MORE_MEMORY;
      return CAS_ER_NO_MORE_MEMORY;
    }
  net_buf->post_file_size = size;
  return 0;
}

int
net_buf_cp_byte (T_NET_BUF * net_buf, char ch)
{
  if (NET_BUF_FREE_SIZE (net_buf) < NET_SIZE_BYTE && net_buf_realloc (net_buf, NET_SIZE_BYTE) < 0)
    {
      return CAS_ER_NO_MORE_MEMORY;
    }

  *(NET_BUF_CURR_PTR (net_buf)) = ch;	/* do not call memcpy(); simply assign */
  net_buf->data_size += NET_SIZE_BYTE;
  return 0;
}

int
net_buf_cp_str (T_NET_BUF * net_buf, const char *buf, int size)
{
  if (size <= 0)
    return 0;

  if (NET_BUF_FREE_SIZE (net_buf) < size && net_buf_realloc (net_buf, size) < 0)
    {
      return CAS_ER_NO_MORE_MEMORY;
    }

  memcpy (NET_BUF_CURR_PTR (net_buf), buf, size);
  net_buf->data_size += size;
  return 0;
}

int
net_buf_cp_int (T_NET_BUF * net_buf, int value, int *begin_offset)
{
  if (NET_BUF_FREE_SIZE (net_buf) < NET_SIZE_INT && net_buf_realloc (net_buf, NET_SIZE_INT) < 0)
    {
      if (begin_offset)
	{
	  *begin_offset = -1;
	}
      return CAS_ER_NO_MORE_MEMORY;
    }

  value = htonl (value);
  memcpy (NET_BUF_CURR_PTR (net_buf), &value, NET_SIZE_INT);

  if (begin_offset)
    {
      *begin_offset = net_buf->data_size;
    }

  net_buf->data_size += NET_SIZE_INT;
  return 0;
}

void
net_buf_overwrite_int (T_NET_BUF * net_buf, int offset, int value)
{
  if (net_buf->data == NULL || offset < 0)
    {
      return;
    }
  value = htonl (value);
  memcpy (net_buf->data + NET_BUF_HEADER_SIZE + offset, &value, NET_SIZE_INT);
}

int
net_buf_cp_bigint (T_NET_BUF * net_buf, DB_BIGINT value, int *begin_offset)
{
  if (NET_BUF_FREE_SIZE (net_buf) < NET_SIZE_BIGINT && net_buf_realloc (net_buf, NET_SIZE_BIGINT) < 0)
    {
      if (begin_offset)
	{
	  *begin_offset = -1;
	}
      return CAS_ER_NO_MORE_MEMORY;
    }

  value = net_htoni64 (value);
  memcpy (NET_BUF_CURR_PTR (net_buf), &value, NET_SIZE_BIGINT);

  if (begin_offset)
    {
      *begin_offset = net_buf->data_size;
    }

  net_buf->data_size += NET_SIZE_BIGINT;
  return 0;
}

#if defined (ENABLE_UNUSED_FUNCTION)
void
net_buf_overwrite_bigint (T_NET_BUF * net_buf, int offset, DB_BIGINT value)
{
  if (net_buf->data == NULL || offset < 0)
    {
      return;
    }

  value = net_htoni64 (value);
  memcpy (net_buf->data + NET_BUF_HEADER_SIZE + offset, &value, NET_SIZE_BIGINT);
}
#endif /* ENABLE_UNUSED_FUNCTION */

int
net_buf_cp_float (T_NET_BUF * net_buf, float value)
{
  if (NET_BUF_FREE_SIZE (net_buf) < NET_SIZE_FLOAT && net_buf_realloc (net_buf, NET_SIZE_FLOAT) < 0)
    {
      return CAS_ER_NO_MORE_MEMORY;
    }
  value = net_htonf (value);
  memcpy (NET_BUF_CURR_PTR (net_buf), &value, NET_SIZE_FLOAT);
  net_buf->data_size += NET_SIZE_FLOAT;

  return 0;
}

int
net_buf_cp_double (T_NET_BUF * net_buf, double value)
{
  if (NET_BUF_FREE_SIZE (net_buf) < NET_SIZE_DOUBLE && net_buf_realloc (net_buf, NET_SIZE_DOUBLE) < 0)
    {
      return CAS_ER_NO_MORE_MEMORY;
    }
  value = net_htond (value);
  memcpy (NET_BUF_CURR_PTR (net_buf), &value, NET_SIZE_DOUBLE);
  net_buf->data_size += NET_SIZE_DOUBLE;

  return 0;
}

int
net_buf_cp_short (T_NET_BUF * net_buf, short value)
{
  if (NET_BUF_FREE_SIZE (net_buf) < NET_SIZE_SHORT && net_buf_realloc (net_buf, NET_SIZE_SHORT) < 0)
    {
      return CAS_ER_NO_MORE_MEMORY;
    }
  value = htons (value);
  memcpy (NET_BUF_CURR_PTR (net_buf), &value, NET_SIZE_SHORT);
  net_buf->data_size += NET_SIZE_SHORT;

  return 0;
}

int
net_buf_cp_object (T_NET_BUF * net_buf, T_OBJECT * oid)
{
  if (NET_BUF_FREE_SIZE (net_buf) < NET_SIZE_OBJECT && net_buf_realloc (net_buf, NET_SIZE_OBJECT) < 0)
    {
      return CAS_ER_NO_MORE_MEMORY;
    }
  net_buf_cp_int (net_buf, oid->pageid, NULL);
  net_buf_cp_short (net_buf, oid->slotid);
  net_buf_cp_short (net_buf, oid->volid);

  return 0;
}

#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
int
net_buf_cp_lob_handle (T_NET_BUF * net_buf, T_LOB_HANDLE * lob)
{
  int lob_handle_size = NET_SIZE_INT + NET_SIZE_INT64 + NET_SIZE_INT + lob->locator_size;
  /* db_type + lob_size + locator_size + locator including null character */
  if (NET_BUF_FREE_SIZE (net_buf) < lob_handle_size && net_buf_realloc (net_buf, lob_handle_size))
    {
      return CAS_ER_NO_MORE_MEMORY;
    }
  net_buf_cp_int (net_buf, lob->db_type, NULL);
  net_buf_cp_bigint (net_buf, lob->lob_size, NULL);
  net_buf_cp_int (net_buf, lob->locator_size, NULL);
  net_buf_cp_str (net_buf, lob->locator, lob->locator_size);
  /* including null character */

  return 0;
}
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */


/* shard_proxy dose not use this function */
void
net_buf_error_msg_set (T_NET_BUF * net_buf, int err_indicator, int err_code, char *err_str, const char *file, int line)
{
#ifdef CAS_DEBUG
  char msg_buf[1024];
#endif
#ifndef LIBCAS_FOR_JSP
#if defined(CAS_CUBRID) || defined(CAS_FOR_MYSQL) || defined(CAS_FOR_ORACLE)
  T_BROKER_VERSION ver;
#endif /* CAS_CUBRID || CAS_FOR_MYSQL || CAS_FOR_ORACLE */
#endif /* !LIBCAS_FOR_JSP */
  size_t err_msg_len = 0;
  char err_msg[ERR_MSG_LENGTH];

  assert (err_code != NO_ERROR);

  net_buf_clear (net_buf);
#ifndef LIBCAS_FOR_JSP
#if defined(CAS_CUBRID) || defined(CAS_FOR_MYSQL) || defined(CAS_FOR_ORACLE)
  ver = as_info->clt_version;
  if (ver >= CAS_MAKE_VER (8, 3, 0))
    {
      net_buf_cp_int (net_buf, err_indicator, NULL);
    }

  if (!DOES_CLIENT_MATCH_THE_PROTOCOL (ver, PROTOCOL_V2) && !cas_di_understand_renewed_error_code (as_info->driver_info)
      && err_code != NO_ERROR)
    {
      if (err_indicator == CAS_ERROR_INDICATOR || err_code == CAS_ER_NOT_AUTHORIZED_CLIENT)
	{
	  err_code = CAS_CONV_ERROR_TO_OLD (err_code);
	}
    }
#else /* CAS_CUBRID || CAS_FOR_MYSQL || CAS_FOR_ORACLE */
  /* shard_proxy do not use net_buf_error_msg_set. it is dummy code. */
  net_buf_cp_int (net_buf, err_indicator, NULL);
#endif /* FOR SHARD_PROXY */
#else /* !LIBCAS_FOR_JSP */
  net_buf_cp_int (net_buf, err_indicator, NULL);
#endif /* !LIBCAS_FOR_JSP */
  net_buf_cp_int (net_buf, err_code, NULL);

#ifdef CAS_DEBUG
  sprintf (msg_buf, "%s:%d ", file, line);
  net_buf_cp_str (net_buf, msg_buf, strlen (msg_buf));
#endif

  err_msg_len = net_error_append_shard_info (err_msg, err_str, ERR_MSG_LENGTH);
  if (err_msg_len == 0)
    {
      net_buf_cp_byte (net_buf, '\0');
    }
  else
    {
      net_buf_cp_str (net_buf, err_msg, (int) err_msg_len + 1);
    }
}

#ifndef BYTE_ORDER_BIG_ENDIAN
INT64
net_htoni64 (INT64 from)
{
  INT64 to;
  char *p, *q;

  p = (char *) &from;
  q = (char *) &to;

  q[0] = p[7];
  q[1] = p[6];
  q[2] = p[5];
  q[3] = p[4];
  q[4] = p[3];
  q[5] = p[2];
  q[6] = p[1];
  q[7] = p[0];

  return to;
}

float
net_htonf (float from)
{
  float to;
  char *p, *q;

  p = (char *) &from;
  q = (char *) &to;

  q[0] = p[3];
  q[1] = p[2];
  q[2] = p[1];
  q[3] = p[0];

  return to;
}

double
net_htond (double from)
{
  double to;
  char *p, *q;

  p = (char *) &from;
  q = (char *) &to;

  q[0] = p[7];
  q[1] = p[6];
  q[2] = p[5];
  q[3] = p[4];
  q[4] = p[3];
  q[5] = p[2];
  q[6] = p[1];
  q[7] = p[0];

  return to;
}
#endif /* !BYTE_ORDER_BIG_ENDIAN */

void
net_buf_column_info_set (T_NET_BUF * net_buf, char ut, short scale, int prec, char charset, const char *name)
{
  net_buf_cp_cas_type_and_charset (net_buf, ut, charset);
  net_buf_cp_short (net_buf, scale);
  net_buf_cp_int (net_buf, prec, NULL);
  if (name == NULL)
    {
      net_buf_cp_int (net_buf, 1, NULL);
      net_buf_cp_byte (net_buf, '\0');
    }
  else
    {
      char *tmp_str;

      ALLOC_COPY (tmp_str, name);
      if (tmp_str == NULL)
	{
	  net_buf_cp_int (net_buf, 1, NULL);
	  net_buf_cp_byte (net_buf, '\0');
	}
      else
	{
	  ut_trim (tmp_str);
	  net_buf_cp_int (net_buf, (int) strlen (tmp_str) + 1, NULL);
	  net_buf_cp_str (net_buf, tmp_str, (int) strlen (tmp_str) + 1);
	  FREE_MEM (tmp_str);
	}
    }
}

static int
net_buf_realloc (T_NET_BUF * net_buf, int size)
{
  if (NET_BUF_FREE_SIZE (net_buf) < size)
    {
      int extra, new_alloc_size;

      /* realloc unit is 64 Kbyte */
      extra = (size + NET_BUF_EXTRA_SIZE - 1) / NET_BUF_EXTRA_SIZE;
      new_alloc_size = net_buf->alloc_size + extra * NET_BUF_EXTRA_SIZE;
      net_buf->data = (char *) REALLOC (net_buf->data, new_alloc_size);
      if (net_buf->data == NULL)
	{
	  net_buf->alloc_size = 0;
	  net_buf->err_code = CAS_ER_NO_MORE_MEMORY;
	  return -1;
	}

      net_buf->alloc_size = new_alloc_size;
    }

  return 0;
}

void
net_arg_get_size (int *size, void *arg)
{
  int tmp_i;

  memcpy (&tmp_i, arg, NET_SIZE_INT);
  *size = ntohl (tmp_i);
}

#if defined(CAS_FOR_ORACLE) || defined(CAS_FOR_MYSQL)
void
net_arg_get_bigint (int64_t * value, void *arg)
{
  int64_t tmp_i;
  char *cur_p = (char *) arg + NET_SIZE_INT;

  memcpy (&tmp_i, cur_p, NET_SIZE_BIGINT);
  *value = ntohi64 (tmp_i);
  cur_p += NET_SIZE_BIGINT;
}
#else /* CAS_FOR_ORACLE || CAS_FOR_MYSQL */
void
net_arg_get_bigint (DB_BIGINT * value, void *arg)
{
  DB_BIGINT tmp_i;
  char *cur_p = (char *) arg + NET_SIZE_INT;

  memcpy (&tmp_i, cur_p, NET_SIZE_BIGINT);
  *value = ntohi64 (tmp_i);
  cur_p += NET_SIZE_BIGINT;
}
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */

void
net_arg_get_int (int *value, void *arg)
{
  int tmp_i;
  char *cur_p = (char *) arg + NET_SIZE_INT;

  memcpy (&tmp_i, cur_p, NET_SIZE_INT);
  *value = ntohl (tmp_i);
  cur_p += NET_SIZE_INT;
}

void
net_arg_get_short (short *value, void *arg)
{
  short tmp_s;
  char *cur_p = (char *) arg + NET_SIZE_INT;

  memcpy (&tmp_s, cur_p, NET_SIZE_SHORT);
  *value = ntohs (tmp_s);
  cur_p += NET_SIZE_SHORT;
}

void
net_arg_get_float (float *value, void *arg)
{
  float tmp_f;
  char *cur_p = (char *) arg + NET_SIZE_INT;

  memcpy (&tmp_f, cur_p, NET_SIZE_FLOAT);
  *value = net_ntohf (tmp_f);
  cur_p += NET_SIZE_FLOAT;
}

void
net_arg_get_double (double *value, void *arg)
{
  double tmp_d;
  char *cur_p = (char *) arg + NET_SIZE_INT;

  memcpy (&tmp_d, cur_p, NET_SIZE_DOUBLE);
  *value = net_ntohd (tmp_d);
  cur_p += NET_SIZE_DOUBLE;
}

void
net_arg_get_str (char **value, int *size, void *arg)
{
  int tmp_i;
  char *cur_p = (char *) arg;

  memcpy (&tmp_i, cur_p, NET_SIZE_INT);
  *size = ntohl (tmp_i);
  cur_p += NET_SIZE_INT;
  if (*size <= 0)
    {
      *value = NULL;
      *size = 0;
    }
  else
    {
      *value = cur_p;
    }
}

void
net_arg_get_date (short *year, short *mon, short *day, void *arg)
{
  short tmp_s;
  char *cur_p = (char *) arg + NET_SIZE_INT;

  memcpy (&tmp_s, cur_p, NET_SIZE_SHORT);
  *year = ntohs (tmp_s);
  cur_p += NET_SIZE_SHORT;
  memcpy (&tmp_s, cur_p, NET_SIZE_SHORT);
  *mon = ntohs (tmp_s);
  cur_p += NET_SIZE_SHORT;
  memcpy (&tmp_s, cur_p, NET_SIZE_SHORT);
  *day = ntohs (tmp_s);
  cur_p += NET_SIZE_SHORT;
}

void
net_arg_get_time (short *hh, short *mm, short *ss, void *arg)
{
  short tmp_s;
  char *cur_p = (char *) arg + NET_SIZE_INT + NET_SIZE_SHORT * 3;

  memcpy (&tmp_s, cur_p, NET_SIZE_SHORT);
  *hh = ntohs (tmp_s);
  cur_p += NET_SIZE_SHORT;
  memcpy (&tmp_s, cur_p, NET_SIZE_SHORT);
  *mm = ntohs (tmp_s);
  cur_p += NET_SIZE_SHORT;
  memcpy (&tmp_s, cur_p, NET_SIZE_SHORT);
  *ss = ntohs (tmp_s);
  cur_p += NET_SIZE_SHORT;
}

void
net_arg_get_timestamp (short *yr, short *mon, short *day, short *hh, short *mm, short *ss, void *arg)
{
  short tmp_s;
  char *cur_p = (char *) arg + NET_SIZE_INT;

  memcpy (&tmp_s, cur_p, NET_SIZE_SHORT);
  *yr = ntohs (tmp_s);
  cur_p += NET_SIZE_SHORT;
  memcpy (&tmp_s, cur_p, NET_SIZE_SHORT);
  *mon = ntohs (tmp_s);
  cur_p += NET_SIZE_SHORT;
  memcpy (&tmp_s, cur_p, NET_SIZE_SHORT);
  *day = ntohs (tmp_s);
  cur_p += NET_SIZE_SHORT;
  memcpy (&tmp_s, cur_p, NET_SIZE_SHORT);
  *hh = ntohs (tmp_s);
  cur_p += NET_SIZE_SHORT;
  memcpy (&tmp_s, cur_p, NET_SIZE_SHORT);
  *mm = ntohs (tmp_s);
  cur_p += NET_SIZE_SHORT;
  memcpy (&tmp_s, cur_p, NET_SIZE_SHORT);
  *ss = ntohs (tmp_s);
  cur_p += NET_SIZE_SHORT;
}

void
net_arg_get_timestamptz (short *yr, short *mon, short *day, short *hh, short *mm, short *ss, char **tz, int *tz_size,
			 void *arg)
{
  int tmp_i;
  char *cur_p = (char *) arg;

  net_arg_get_timestamp (yr, mon, day, hh, mm, ss, arg);

  memcpy (&tmp_i, cur_p, NET_SIZE_INT);
  /* skip dummy milisecond values */
  *tz_size = ntohl (tmp_i) - NET_SIZE_TIMESTAMP - NET_SIZE_SHORT;
  cur_p += NET_SIZE_INT + NET_SIZE_TIMESTAMP + NET_SIZE_SHORT;

  *tz = cur_p;
}

void
net_arg_get_datetime (short *yr, short *mon, short *day, short *hh, short *mm, short *ss, short *ms, void *arg)
{
  short tmp_s;
  char *cur_p = (char *) arg + NET_SIZE_INT;

  memcpy (&tmp_s, cur_p, NET_SIZE_SHORT);
  *yr = ntohs (tmp_s);
  cur_p += NET_SIZE_SHORT;
  memcpy (&tmp_s, cur_p, NET_SIZE_SHORT);
  *mon = ntohs (tmp_s);
  cur_p += NET_SIZE_SHORT;
  memcpy (&tmp_s, cur_p, NET_SIZE_SHORT);
  *day = ntohs (tmp_s);
  cur_p += NET_SIZE_SHORT;
  memcpy (&tmp_s, cur_p, NET_SIZE_SHORT);
  *hh = ntohs (tmp_s);
  cur_p += NET_SIZE_SHORT;
  memcpy (&tmp_s, cur_p, NET_SIZE_SHORT);
  *mm = ntohs (tmp_s);
  cur_p += NET_SIZE_SHORT;
  memcpy (&tmp_s, cur_p, NET_SIZE_SHORT);
  *ss = ntohs (tmp_s);
  cur_p += NET_SIZE_SHORT;
  memcpy (&tmp_s, cur_p, NET_SIZE_SHORT);
  *ms = ntohs (tmp_s);
  cur_p += NET_SIZE_SHORT;
}

void
net_arg_get_datetimetz (short *yr, short *mon, short *day, short *hh, short *mm, short *ss, short *ms, char **tz,
			int *tz_size, void *arg)
{
  int tmp_i;
  char *cur_p = (char *) arg;

  net_arg_get_datetime (yr, mon, day, hh, mm, ss, ms, arg);

  memcpy (&tmp_i, cur_p, NET_SIZE_INT);
  *tz_size = ntohl (tmp_i) - NET_SIZE_DATETIME;
  cur_p += NET_SIZE_INT + NET_SIZE_DATETIME;

  *tz = cur_p;
}

void
net_arg_get_object (T_OBJECT * obj, void *arg)
{
  int tmp_i;
  short tmp_s;
  char *cur_p = (char *) arg + NET_SIZE_INT;

  memcpy (&tmp_i, cur_p, NET_SIZE_INT);
  obj->pageid = ntohl (tmp_i);
  cur_p += NET_SIZE_INT;
  memcpy (&tmp_s, cur_p, NET_SIZE_SHORT);
  obj->slotid = ntohs (tmp_s);
  cur_p += NET_SIZE_SHORT;
  memcpy (&tmp_s, cur_p, NET_SIZE_SHORT);
  obj->volid = ntohs (tmp_s);
  cur_p += NET_SIZE_SHORT;
}

void
net_arg_get_cache_time (void *ct, void *arg)
{
  int tmp_i;
  char *cur_p = (char *) arg + NET_SIZE_INT;
  CACHE_TIME *cache_time = (CACHE_TIME *) ct;

  memcpy (&tmp_i, cur_p, NET_SIZE_INT);
  cache_time->sec = ntohl (tmp_i);
  cur_p += NET_SIZE_INT;
  memcpy (&tmp_i, cur_p, NET_SIZE_INT);
  cache_time->usec = ntohl (tmp_i);
  cur_p += NET_SIZE_INT;
}

#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
void
net_arg_get_dbobject (DB_OBJECT ** obj, void *arg)
{
  T_OBJECT cas_obj;
  DB_IDENTIFIER oid;

  net_arg_get_object (&cas_obj, arg);
  oid.pageid = cas_obj.pageid;
  oid.volid = cas_obj.volid;
  oid.slotid = cas_obj.slotid;
  *obj = db_object (&oid);
}

void
net_arg_get_cci_object (int *pageid, short *slotid, short *volid, void *arg)
{
  int tmp_i;
  short tmp_s;
  char *cur_p = (char *) arg + NET_SIZE_INT;

  memcpy (&tmp_i, cur_p, NET_SIZE_INT);
  *pageid = ntohl (tmp_i);
  cur_p += NET_SIZE_INT;
  memcpy (&tmp_s, cur_p, NET_SIZE_SHORT);
  *slotid = ntohs (tmp_s);
  cur_p += NET_SIZE_SHORT;
  memcpy (&tmp_s, cur_p, NET_SIZE_SHORT);
  *volid = ntohs (tmp_s);
  cur_p += NET_SIZE_SHORT;
}

void
net_arg_get_lob_handle (T_LOB_HANDLE * lob, void *arg)
{
  int tmp_i;
  INT64 tmp_i64;
  char *cur_p = (char *) arg + NET_SIZE_INT;

  memcpy (&tmp_i, cur_p, NET_SIZE_INT);
  lob->db_type = ntohl (tmp_i);
  cur_p += NET_SIZE_INT;
  memcpy (&tmp_i64, cur_p, NET_SIZE_INT64);
  lob->lob_size = ntohi64 (tmp_i64);
  cur_p += NET_SIZE_INT64;
  memcpy (&tmp_i, cur_p, NET_SIZE_INT);
  lob->locator_size = ntohl (tmp_i);
  cur_p += NET_SIZE_INT;
  if (lob->locator_size <= 0)
    {
      lob->locator = NULL;
      lob->locator_size = 0;
    }
  else
    {
      lob->locator = cur_p;	/* null terminated */
    }
}

void
net_arg_get_lob_value (DB_VALUE * db_lob, void *arg)
{
  T_LOB_HANDLE lob_handle;
  DB_ELO elo;

  net_arg_get_lob_handle (&lob_handle, arg);
  elo_init_structure (&elo);
  elo.size = lob_handle.lob_size;
  elo.type = ELO_FBO;
  elo.locator = db_private_strdup (NULL, lob_handle.locator);
  db_make_elo (db_lob, (DB_TYPE) lob_handle.db_type, &elo);
  db_lob->need_clear = true;
}
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */

void
net_arg_put_int (void *arg, int *value)
{
  int len;
  int put_value;
  char *cur_p;

  cur_p = (char *) (arg);
  len = htonl (NET_SIZE_INT);
  memcpy (cur_p, &len, NET_SIZE_INT);

  cur_p += NET_SIZE_INT;
  put_value = htonl (*value);
  memcpy (cur_p, &put_value, NET_SIZE_INT);
}


size_t
net_error_append_shard_info (char *err_buf, const char *err_msg, int buf_size)
{
  assert (err_buf);

#if !defined(LIBCAS_FOR_JSP) && !defined(CUB_PROXY)
  if (cas_shard_flag == ON)
    {
      if (err_msg == NULL)
	{
	  snprintf (err_buf, buf_size, "[SHARD/CAS ID-%d,%d]", shm_shard_id, shm_shard_cas_id + 1);
	}
      else if ((int) strlen (err_msg) + MAX_SHARD_INFO_LENGTH >= buf_size)
	{
	  snprintf (err_buf, buf_size, "%s", err_msg);
	}
      else
	{
	  snprintf (err_buf, buf_size, "%s [SHARD/CAS ID-%d,%d]", err_msg, shm_shard_id, shm_shard_cas_id + 1);
	}
    }
  else
#endif /* !LIBCAS_FOR_JSP && !CUB_PROXY */
    {
      if (err_msg == NULL)
	{
	  err_buf[0] = '\0';
	  return 0;
	}
      else
	{
	  strncpy (err_buf, err_msg, buf_size - 1);
	}
    }

  err_buf[buf_size - 1] = '\0';

  return strlen (err_buf);
}

/* net_buf_cp_cas_type_and_charset -
 *			 sends the type information into a network buffer
 *		         The cas_type is expected to be encoded by function
 *			 'set_extended_cas_type'. The network buffer bytes will
 *			 be encoded as:
 *			      MSB byte : 1CCR RHHH : C = collection code bits
 *					 R = reserved bits
 *					 H = charset
 *			      (please note the bit 7 is 1)
 *			      LSB byte : TTTT TTTT : T = type bits
 */
int
net_buf_cp_cas_type_and_charset (T_NET_BUF * net_buf, unsigned char cas_type, unsigned char charset)
{
  if (DOES_CLIENT_UNDERSTAND_THE_PROTOCOL (net_buf->client_version, PROTOCOL_V7))
    {
      unsigned char cas_net_first_byte, cas_net_second_byte;

      if (NET_BUF_FREE_SIZE (net_buf) < NET_SIZE_SHORT && net_buf_realloc (net_buf, NET_SIZE_SHORT) < 0)
	{
	  return CAS_ER_NO_MORE_MEMORY;
	}

      cas_net_first_byte = cas_type & CCI_CODE_COLLECTION;
      cas_net_first_byte |= CAS_TYPE_FIRST_BYTE_PROTOCOL_MASK;
      cas_net_first_byte |= charset & 0x07;

      net_buf_cp_byte (net_buf, cas_net_first_byte);

      cas_net_second_byte = CCI_GET_COLLECTION_DOMAIN (cas_type);

      net_buf_cp_byte (net_buf, cas_net_second_byte);
    }
  else
    {
      if (NET_BUF_FREE_SIZE (net_buf) < NET_SIZE_BYTE && net_buf_realloc (net_buf, NET_SIZE_BYTE) < 0)
	{
	  return CAS_ER_NO_MORE_MEMORY;
	}

      assert (cas_type < 0x80);
      net_buf_cp_byte (net_buf, cas_type);
    }

  return 0;
}
