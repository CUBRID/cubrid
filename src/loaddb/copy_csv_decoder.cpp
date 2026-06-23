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
 * copy_csv_decoder.cpp - Decode CSV text rows for COPY FROM STDIN (FORMAT CSV)
 */

#include "copy_csv_decoder.hpp"

#include "copy_binary_decoder.hpp"	/* COPY_DECODE_NEED_MORE */
#include "db_date.h"
#include "dbtype.h"
#include "error_code.h"
#include "error_manager.h"
#include "intl_support.h"
#include "language_support.h"

#include <cstdlib>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/*
 * Type coercion of one textual field. The text lives in field_storage[*] which
 * outlives the row insert, so VARCHAR may point into it.
 */
static int
csv_coerce_field (const std::string &s, DB_TYPE type, DB_VALUE *val)
{
  char *endp = NULL;

  switch (type)
    {
    case DB_TYPE_INTEGER:
      {
	long v = strtol (s.c_str (), &endp, 10);
	if (endp == s.c_str () || *endp != '\0')
	  {
	    goto bad_value;
	  }
	db_make_int (val, (int) v);
	break;
      }
    case DB_TYPE_BIGINT:
      {
	long long v = strtoll (s.c_str (), &endp, 10);
	if (endp == s.c_str () || *endp != '\0')
	  {
	    goto bad_value;
	  }
	db_make_bigint (val, (DB_BIGINT) v);
	break;
      }
    case DB_TYPE_FLOAT:
      {
	float v = strtof (s.c_str (), &endp);
	if (endp == s.c_str () || *endp != '\0')
	  {
	    goto bad_value;
	  }
	db_make_float (val, v);
	break;
      }
    case DB_TYPE_DOUBLE:
      {
	double v = strtod (s.c_str (), &endp);
	if (endp == s.c_str () || *endp != '\0')
	  {
	    goto bad_value;
	  }
	db_make_double (val, v);
	break;
      }
    case DB_TYPE_SHORT:
      {
	long v = strtol (s.c_str (), &endp, 10);
	if (endp == s.c_str () || *endp != '\0' || v < -32768 || v > 32767)
	  {
	    goto bad_value;
	  }
	db_make_short (val, (short) v);
	break;
      }
    case DB_TYPE_VARCHAR:
      db_make_varchar (val, (int) s.size (), s.data (), (int) s.size (), INTL_CODESET_UTF8, LANG_COLL_UTF8_BINARY);
      break;
    case DB_TYPE_CHAR:
      db_make_char (val, (int) s.size (), s.data (), (int) s.size (), INTL_CODESET_UTF8, LANG_COLL_UTF8_BINARY);
      break;
    case DB_TYPE_DATE:
      {
	DB_DATE d;
	if (db_string_to_date (s.c_str (), &d) != NO_ERROR)
	  {
	    goto bad_value;
	  }
	db_value_put_encoded_date (val, &d);
	break;
      }
    case DB_TYPE_TIME:
      {
	DB_TIME t;
	if (db_string_to_time (s.c_str (), &t) != NO_ERROR)
	  {
	    goto bad_value;
	  }
	db_value_put_encoded_time (val, &t);
	break;
      }
    case DB_TYPE_TIMESTAMP:
      {
	DB_TIMESTAMP ts;
	if (db_string_to_timestamp (s.c_str (), &ts) != NO_ERROR)
	  {
	    goto bad_value;
	  }
	db_make_timestamp (val, ts);
	break;
      }
    case DB_TYPE_DATETIME:
      {
	DB_DATETIME dt;
	if (db_string_to_datetime (s.c_str (), &dt) != NO_ERROR)
	  {
	    goto bad_value;
	  }
	db_make_datetime (val, &dt);
	break;
      }
    default:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_COPY_CSV_FORMAT_ERROR, 1, "unsupported column type");
      return ER_COPY_CSV_FORMAT_ERROR;
    }

  return NO_ERROR;

bad_value:
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_COPY_CSV_FORMAT_ERROR, 1, "value parse error");
  return ER_COPY_CSV_FORMAT_ERROR;
}

int
decode_csv_row (const char *buf, int buf_len, const DB_TYPE *types, int ncols,
		DB_VALUE *out_vals, std::vector<std::string> &field_storage,
		std::vector<char> &quoted, char delimiter, char quote, bool skip_only, int *bytes_consumed)
{
  /* 1. find the line terminator that is not inside a quoted field */
  bool in_quotes = false;
  int line_end = -1;
  for (int i = 0; i < buf_len; i++)
    {
      char c = buf[i];
      if (in_quotes)
	{
	  if (c == quote)
	    {
	      if (i + 1 < buf_len && buf[i + 1] == quote)
		{
		  i++;		/* escaped quote */
		}
	      else
		{
		  in_quotes = false;
		}
	    }
	}
      else
	{
	  if (c == quote)
	    {
	      in_quotes = true;
	    }
	  else if (c == '\n')
	    {
	      line_end = i;
	      break;
	    }
	}
    }

  if (line_end < 0)
    {
      /* no complete line yet (or buffer ended mid-quote) */
      return COPY_DECODE_NEED_MORE;
    }

  *bytes_consumed = line_end + 1;	/* consume through the '\n' */

  if (skip_only)
    {
      /* header line: consume but do not parse/coerce */
      return NO_ERROR;
    }

  int content_end = line_end;
  if (content_end > 0 && buf[content_end - 1] == '\r')
    {
      content_end--;			/* tolerate CRLF */
    }

  /* 2. split into fields, honoring quotes and "" escapes */
  field_storage.clear ();
  quoted.clear ();
  std::string cur;
  bool cur_quoted = false;
  in_quotes = false;
  for (int p = 0; p < content_end; p++)
    {
      char c = buf[p];
      if (in_quotes)
	{
	  if (c == quote)
	    {
	      if (p + 1 < content_end && buf[p + 1] == quote)
		{
		  cur.push_back (quote);
		  p++;
		}
	      else
		{
		  in_quotes = false;
		}
	    }
	  else
	    {
	      cur.push_back (c);
	    }
	}
      else
	{
	  if (c == quote)
	    {
	      in_quotes = true;
	      cur_quoted = true;
	    }
	  else if (c == delimiter)
	    {
	      field_storage.push_back (cur);
	      quoted.push_back (cur_quoted ? 1 : 0);
	      cur.clear ();
	      cur_quoted = false;
	    }
	  else
	    {
	      cur.push_back (c);
	    }
	}
    }
  field_storage.push_back (cur);
  quoted.push_back (cur_quoted ? 1 : 0);

  if ((int) field_storage.size () != ncols)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_COPY_CSV_FORMAT_ERROR, 1, "field count mismatch");
      return ER_COPY_CSV_FORMAT_ERROR;
    }

  /* 3. coerce each field to its column type (empty unquoted field = NULL) */
  for (int j = 0; j < ncols; j++)
    {
      if (!quoted[j] && field_storage[j].empty ())
	{
	  db_make_null (&out_vals[j]);
	  continue;
	}

      int error = csv_coerce_field (field_storage[j], types[j], &out_vals[j]);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }

  return NO_ERROR;
}
