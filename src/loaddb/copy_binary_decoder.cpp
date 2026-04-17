/*
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
#include "db_vector.hpp"
#include "dbtype.h"
#include "error_manager.h"
#include "intl_support.h"
#include "language_support.h"
#include "porting.h"

#include <arpa/inet.h>
#include <cstring>

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

static int
decode_field (const char *buf, int buf_remaining, DB_TYPE type, DB_VALUE *val, int *consumed)
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
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DB_UNIMPLEMENTED, 1, "COPY binary: invalid field length");
      return ER_DB_UNIMPLEMENTED;
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
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DB_UNIMPLEMENTED, 1, "COPY binary: INT expects 4 bytes");
	  return ER_DB_UNIMPLEMENTED;
	}
      db_make_int (val, read_int32 (data));
      break;

    case DB_TYPE_BIGINT:
      if (field_len != 8)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DB_UNIMPLEMENTED, 1, "COPY binary: BIGINT expects 8 bytes");
	  return ER_DB_UNIMPLEMENTED;
	}
      db_make_bigint (val, read_int64 (data));
      break;

    case DB_TYPE_FLOAT:
      if (field_len != 4)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DB_UNIMPLEMENTED, 1, "COPY binary: FLOAT expects 4 bytes");
	  return ER_DB_UNIMPLEMENTED;
	}
      db_make_float (val, read_float (data));
      break;

    case DB_TYPE_DOUBLE:
      if (field_len != 8)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DB_UNIMPLEMENTED, 1, "COPY binary: DOUBLE expects 8 bytes");
	  return ER_DB_UNIMPLEMENTED;
	}
      db_make_double (val, read_double (data));
      break;

    case DB_TYPE_VARCHAR:
      db_make_varchar (val, field_len, data, field_len, INTL_CODESET_UTF8, LANG_COLL_UTF8_BINARY);
      break;

    case DB_TYPE_VECTOR:
    {
      if (field_len < (int) sizeof (int32_t))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DB_UNIMPLEMENTED, 1,
		  "COPY binary: VECTOR needs at least 4 bytes for dimension");
	  return ER_DB_UNIMPLEMENTED;
	}

      int32_t dim = read_int32 (data);
      int expected_len = (int) sizeof (int32_t) + dim * (int) sizeof (float);
      if (field_len != expected_len)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DB_UNIMPLEMENTED, 1,
		  "COPY binary: VECTOR size mismatch");
	  return ER_DB_UNIMPLEMENTED;
	}

      DB_VECTOR_FLOAT vf;
      vf.dim = dim;
      /* Wire format for the vector body is little-endian float32 — matches
       * the on-disk vector representation on little-endian hosts, so the
       * decoder can memcpy the wire bytes straight into the DB_VALUE buffer
       * instead of byteswapping 256 floats one at a time. Big-endian hosts
       * still pay per-element swap. */
      float *floats = db_vector_allocate_float_array (dim);
      if (floats == NULL)
	{
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
      const char *fptr = data + sizeof (int32_t);
#if defined (__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
      memcpy (floats, fptr, (size_t) dim * sizeof (float));
#else
      for (int i = 0; i < dim; i++)
	{
	  uint32_t raw;
	  memcpy (&raw, fptr + i * sizeof (float), sizeof (raw));
	  raw = __builtin_bswap32 (raw);
	  memcpy (&floats[i], &raw, sizeof (float));
	}
#endif
      vf.float_array = floats;
      db_make_vector_float (val, &vf);
      /* db_make_vector_float stores the pointer, not a copy — db_value_clear frees it */
    }
    break;

    default:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DB_UNIMPLEMENTED, 1, "COPY binary: unsupported column type");
      return ER_DB_UNIMPLEMENTED;
    }

  *consumed += field_len;
  return NO_ERROR;
}

int
decode_binary_row (const char *buf, int buf_len, const DB_TYPE *types, int ncols,
		   DB_VALUE *out_vals, int *bytes_consumed)
{
  int error = NO_ERROR;
  int pos = 0;

  /* read num_fields */
  if (buf_len < (int) sizeof (int16_t))
    {
      /* not enough bytes even for the row header — caller should buffer */
      return COPY_DECODE_NEED_MORE;
    }

  int16_t num_fields = read_int16 (buf);
  pos += sizeof (int16_t);

  /* check for footer sentinel */
  if (num_fields == COPY_BINARY_FOOTER_SENTINEL)
    {
      *bytes_consumed = pos;
      return COPY_DECODE_FOOTER;
    }

  if (num_fields != (int16_t) ncols)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DB_UNIMPLEMENTED, 1,
	      "COPY binary: field count mismatch");
      return ER_DB_UNIMPLEMENTED;
    }

  /* decode each field */
  for (int i = 0; i < ncols; i++)
    {
      int field_consumed = 0;
      error = decode_field (buf + pos, buf_len - pos, types[i], &out_vals[i], &field_consumed);
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
