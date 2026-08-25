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

/*
 * copy_binary_decoder.cpp - Decode binary rows for COPY FROM STDIN
 */

#include "config.h"

#include "copy_binary_decoder.hpp"
#include "copy_binary_format.hpp"
#include "dbtype.h"
#include "error_code.h"
#include "error_manager.h"
#include "intl_support.h"
#include "object_primitive.h"
#include "language_support.h"
#include "porting.h"

#include <arpa/inet.h>
#include <cstring>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/* read int16 from buffer in network byte order */
static inline int16_t
read_int16 (const char *buf)
{
  uint16_t v;
  memcpy (&v, buf, sizeof (v));
  return (int16_t) ntohs (v);
}

/* read int32 from buffer in network byte order */
static inline int32_t
read_int32 (const char *buf)
{
  uint32_t v;
  memcpy (&v, buf, sizeof (v));
  return (int32_t) ntohl (v);
}

/* read int64 from buffer in network byte order */
static inline int64_t
read_int64 (const char *buf)
{
  uint32_t hi, lo;
  memcpy (&hi, buf, sizeof (hi));
  memcpy (&lo, buf + 4, sizeof (lo));
  return ((int64_t) ntohl (hi) << 32) | (uint32_t) ntohl (lo);
}

/* read float from buffer (IEEE 754, same byte order as int32) */
static inline float
read_float (const char *buf)
{
  int32_t v = read_int32 (buf);
  float f;
  memcpy (&f, &v, sizeof (f));
  return f;
}

/* read double from buffer (IEEE 754, same byte order as int64) */
static inline double
read_double (const char *buf)
{
  int64_t v = read_int64 (buf);
  double d;
  memcpy (&d, &v, sizeof (d));
  return d;
}

int
copy_fit_char_precision (DB_TYPE type, const char *str, int str_len, const COPY_COL_DOMAIN *dom, int *fitted_len)
{
  int precision = dom->precision;
  INTL_CODESET codeset = (INTL_CODESET) dom->codeset;
  int char_count = 0;
  int truncate_size = 0;
  const char *p;

  *fitted_len = str_len;

  /* char_count <= byte_count in every codeset, so this covers the common case */
  if (precision <= 0 || str_len <= precision)
    {
      return NO_ERROR;
    }

  intl_char_count ((unsigned char *) str, str_len, codeset, &char_count);
  if (char_count <= precision)
    {
      return NO_ERROR;
    }

  /* Trailing blanks may be truncated away; anything else is an overflow. */
  intl_char_size ((unsigned char *) str, precision, codeset, &truncate_size);
  p = intl_skip_spaces (&str[truncate_size], &str[str_len], codeset);
  if (p < &str[str_len])
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1, pr_type_name (type));
      return ER_IT_DATA_OVERFLOW;
    }

  *fitted_len = truncate_size;
  return NO_ERROR;
}

static int
decode_field (const char *buf, int buf_remaining, DB_TYPE type, const COPY_COL_DOMAIN *dom, DB_VALUE *val,
	      int *consumed)
{
  int32_t field_len;

  if (buf_remaining < (int) sizeof (int32_t))
    {
      /* not enough bytes to read field length header — caller should buffer */
      return COPY_DECODE_NEED_MORE;
    }

  field_len = read_int32 (buf);
  *consumed = sizeof (int32_t);

  if (field_len == COPY_BINARY_NULL_FIELD_LEN)
    {
      db_make_null (val);
      return NO_ERROR;
    }

  if (field_len < 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_COPY_BINARY_FORMAT_ERROR, 1, "invalid field length");
      return ER_COPY_BINARY_FORMAT_ERROR;
    }

  if (buf_remaining - (int) sizeof (int32_t) < field_len)
    {
      /* field body split across chunks — caller should buffer and retry */
      return COPY_DECODE_NEED_MORE;
    }

  const char *data = buf + sizeof (int32_t);

  switch (type)
    {
    case DB_TYPE_INTEGER:
      if (field_len != 4)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_COPY_BINARY_FORMAT_ERROR, 1, "INT expects 4 bytes");
	  return ER_COPY_BINARY_FORMAT_ERROR;
	}
      db_make_int (val, read_int32 (data));
      break;

    case DB_TYPE_BIGINT:
      if (field_len != 8)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_COPY_BINARY_FORMAT_ERROR, 1, "BIGINT expects 8 bytes");
	  return ER_COPY_BINARY_FORMAT_ERROR;
	}
      db_make_bigint (val, read_int64 (data));
      break;

    case DB_TYPE_FLOAT:
      if (field_len != 4)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_COPY_BINARY_FORMAT_ERROR, 1, "FLOAT expects 4 bytes");
	  return ER_COPY_BINARY_FORMAT_ERROR;
	}
      db_make_float (val, read_float (data));
      break;

    case DB_TYPE_DOUBLE:
      if (field_len != 8)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_COPY_BINARY_FORMAT_ERROR, 1, "DOUBLE expects 8 bytes");
	  return ER_COPY_BINARY_FORMAT_ERROR;
	}
      db_make_double (val, read_double (data));
      break;

    case DB_TYPE_SHORT:
      if (field_len != 2)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_COPY_BINARY_FORMAT_ERROR, 1, "SHORT expects 2 bytes");
	  return ER_COPY_BINARY_FORMAT_ERROR;
	}
      db_make_short (val, read_int16 (data));
      break;

    case DB_TYPE_VARCHAR:
    case DB_TYPE_CHAR:
    {
      /* Build against the column's own domain: the value then matches the
       * attribute exactly and heap_attrinfo_set stores it without coercing. */
      int fitted = field_len;
      int rc = copy_fit_char_precision (type, data, field_len, dom, &fitted);
      if (rc != NO_ERROR)
	{
	  return rc;
	}
      if (db_value_domain_init (val, type, dom->precision, 0) != NO_ERROR
	  || db_make_db_char (val, (INTL_CODESET) dom->codeset, dom->collation_id, data, fitted) != NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_COPY_BINARY_FORMAT_ERROR, 1, "character value conversion failed");
	  return ER_COPY_BINARY_FORMAT_ERROR;
	}
      break;
    }

    case DB_TYPE_DATE:
      /* body: 4-byte encoded DB_DATE (julian day), network order */
      if (field_len != 4)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_COPY_BINARY_FORMAT_ERROR, 1, "DATE expects 4 bytes");
	  return ER_COPY_BINARY_FORMAT_ERROR;
	}
      {
	DB_DATE d = (DB_DATE) read_int32 (data);
	db_value_put_encoded_date (val, &d);
      }
      break;

    case DB_TYPE_TIME:
      /* body: 4-byte encoded DB_TIME (seconds since midnight), network order */
      if (field_len != 4)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_COPY_BINARY_FORMAT_ERROR, 1, "TIME expects 4 bytes");
	  return ER_COPY_BINARY_FORMAT_ERROR;
	}
      {
	DB_TIME t = (DB_TIME) read_int32 (data);
	db_value_put_encoded_time (val, &t);
      }
      break;

    case DB_TYPE_TIMESTAMP:
      /* body: 4-byte DB_TIMESTAMP (unix epoch seconds), network order */
      if (field_len != 4)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_COPY_BINARY_FORMAT_ERROR, 1, "TIMESTAMP expects 4 bytes");
	  return ER_COPY_BINARY_FORMAT_ERROR;
	}
      {
	DB_TIMESTAMP ts = (DB_TIMESTAMP) read_int32 (data);
	db_make_timestamp (val, ts);
      }
      break;

    case DB_TYPE_DATETIME:
      /* body: 4-byte date (julian) + 4-byte time (milliseconds), network order */
      if (field_len != 8)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_COPY_BINARY_FORMAT_ERROR, 1, "DATETIME expects 8 bytes");
	  return ER_COPY_BINARY_FORMAT_ERROR;
	}
      {
	DB_DATETIME dt;
	dt.date = (unsigned int) read_int32 (data);
	dt.time = (unsigned int) read_int32 (data + 4);
	db_make_datetime (val, &dt);
      }
      break;

    default:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_COPY_BINARY_FORMAT_ERROR, 1, "unsupported column type");
      return ER_COPY_BINARY_FORMAT_ERROR;
    }

  *consumed += field_len;
  return NO_ERROR;
}

int
decode_binary_row (const char *buf, int buf_len, const DB_TYPE *types, const COPY_COL_DOMAIN *domains, int ncols,
		   DB_VALUE *out_vals, int *bytes_consumed)
{
  int error = NO_ERROR;
  int pos = 0;

  if (buf_len < (int) sizeof (int16_t))
    {
      /* not enough bytes even for the row header — caller should buffer */
      return COPY_DECODE_NEED_MORE;
    }

  int16_t num_fields = read_int16 (buf);
  pos += sizeof (int16_t);

  if (num_fields == COPY_BINARY_FOOTER_SENTINEL)
    {
      *bytes_consumed = pos;
      return COPY_DECODE_FOOTER;
    }

  if (num_fields != (int16_t) ncols)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_COPY_BINARY_FORMAT_ERROR, 1,
	      "field count mismatch");
      return ER_COPY_BINARY_FORMAT_ERROR;
    }

  for (int i = 0; i < ncols; i++)
    {
      int field_consumed = 0;
      error = decode_field (buf + pos, buf_len - pos, types[i], &domains[i], &out_vals[i], &field_consumed);
      if (error != NO_ERROR)
	{
	  /* clean up already-decoded values (NEED_MORE path too) */
	  for (int j = 0; j < i; j++)
	    {
	      db_value_clear (&out_vals[j]);
	    }
	  return error;
	}
      pos += field_consumed;
    }

  *bytes_consumed = pos;
  return NO_ERROR;
}
