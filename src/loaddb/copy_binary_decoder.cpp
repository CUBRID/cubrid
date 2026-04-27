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
#include "db_vector.hpp"
#include "dbtype.h"
#include "error_manager.h"
#include "intl_support.h"
#include "language_support.h"
#include "object_representation.h"
#include "porting.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/* thin wrappers over OR_GET_* macros to keep call sites expression-style */
static inline int16_t
read_int16 (const char *buf)
{
  return (int16_t) OR_GET_SHORT (buf);
}

static inline int32_t
read_int32 (const char *buf)
{
  return (int32_t) OR_GET_INT (buf);
}

static inline int64_t
read_int64 (const char *buf)
{
  INT64 v;
  OR_GET_INT64 (buf, &v);
  return (int64_t) v;
}

static inline float
read_float (const char *buf)
{
  float v;
  OR_GET_FLOAT (buf, &v);
  return v;
}

static inline double
read_double (const char *buf)
{
  double v;
  OR_GET_DOUBLE (buf, &v);
  return v;
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
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_COPY_BINARY_PROTOCOL_GENERIC, 1, "invalid field length");
      return ER_COPY_BINARY_PROTOCOL_GENERIC;
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
      if (field_len != OR_INT_SIZE)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_COPY_BINARY_PROTOCOL, 2, "INT", OR_INT_SIZE);
	  return ER_COPY_BINARY_PROTOCOL;
	}
      db_make_int (val, read_int32 (data));
      break;

    case DB_TYPE_BIGINT:
      if (field_len != OR_BIGINT_SIZE)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_COPY_BINARY_PROTOCOL, 2, "BIGINT", OR_BIGINT_SIZE);
	  return ER_COPY_BINARY_PROTOCOL;
	}
      db_make_bigint (val, read_int64 (data));
      break;

    case DB_TYPE_FLOAT:
      if (field_len != OR_FLOAT_SIZE)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_COPY_BINARY_PROTOCOL, 2, "FLOAT", OR_FLOAT_SIZE);
	  return ER_COPY_BINARY_PROTOCOL;
	}
      db_make_float (val, read_float (data));
      break;

    case DB_TYPE_DOUBLE:
      if (field_len != OR_DOUBLE_SIZE)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_COPY_BINARY_PROTOCOL, 2, "DOUBLE", OR_DOUBLE_SIZE);
	  return ER_COPY_BINARY_PROTOCOL;
	}
      db_make_double (val, read_double (data));
      break;

    case DB_TYPE_VARCHAR:
      db_make_varchar (val, field_len, data, field_len, INTL_CODESET_UTF8, LANG_COLL_UTF8_BINARY);
      break;

    case DB_TYPE_VECTOR:
    {
      if (field_len < OR_INT_SIZE)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_COPY_BINARY_PROTOCOL_GENERIC, 1,
		  "VECTOR needs at least 4 bytes for dimension");
	  return ER_COPY_BINARY_PROTOCOL_GENERIC;
	}

      int32_t dim = read_int32 (data);
      /* validate dim before any arithmetic to avoid signed overflow UB and
       * negative-size allocation downstream. */
      if (dim < 0)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_COPY_BINARY_PROTOCOL_GENERIC, 1,
		  "VECTOR dimension must not be negative");
	  return ER_COPY_BINARY_PROTOCOL_GENERIC;
	}
      int expected_len = OR_INT_SIZE + dim * OR_FLOAT_SIZE;
      if (field_len != expected_len)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_COPY_BINARY_PROTOCOL, 2, "VECTOR", expected_len);
	  return ER_COPY_BINARY_PROTOCOL;
	}

      DB_VECTOR_FLOAT vf;
      vf.dim = dim;
      /* decode float array from network byte order */
      float *floats = db_vector_allocate_float_array (dim);
      if (floats == NULL)
	{
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
      const char *fptr = data + OR_INT_SIZE;
      for (int i = 0; i < dim; i++)
	{
	  floats[i] = read_float (fptr + i * OR_FLOAT_SIZE);
	}
      vf.float_array = floats;
      db_make_vector_float (val, &vf);
      /* db_make_vector_float stores the pointer, not a copy — db_value_clear frees it */
    }
    break;

    default:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_COPY_BINARY_PROTOCOL_GENERIC, 1, "unsupported column type");
      return ER_COPY_BINARY_PROTOCOL_GENERIC;
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
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_COPY_BINARY_PROTOCOL_GENERIC, 1,
	      "field count mismatch");
      return ER_COPY_BINARY_PROTOCOL_GENERIC;
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
