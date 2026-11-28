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
 * copy_decode_common.cpp - What the CSV and the BINARY decoder of COPY FROM STDIN share
 */

#include "config.h"

#include "copy_decode_common.hpp"
#include "chartype.h"
#include "dbtype.h"
#include "error_code.h"
#include "error_manager.h"
#include "intl_support.h"
#include "language_support.h"
#include "object_domain.h"
#include "object_primitive.h"

#include <cstdio>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

bool
copy_check_char_encoding (const char *str, int str_len, const COPY_COL_DOMAIN *dom)
{
  char *invalid_pos = NULL;

  return intl_check_string (str, str_len, &invalid_pos, (INTL_CODESET) dom->codeset) == INTL_UTF8_VALID;
}

bool
copy_fit_char_precision (const char *str, int str_len, const COPY_COL_DOMAIN *dom, int *fitted_len)
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
      return true;
    }

  intl_char_count ((unsigned char *) str, str_len, codeset, &char_count);
  if (char_count <= precision)
    {
      return true;
    }

  /* Trailing blanks may be truncated away; anything else is an overflow. */
  intl_char_size ((unsigned char *) str, precision, codeset, &truncate_size);
  p = intl_skip_spaces (&str[truncate_size], &str[str_len], codeset);
  if (p < &str[str_len])
    {
      return false;
    }

  *fitted_len = truncate_size;
  return true;
}

int
copy_format_error (int error_code, std::int64_t row, int col, const char *reason)
{
  char msg[256];

  if (col >= 0)
    {
      snprintf (msg, sizeof (msg), "%s (row %lld, column %d)", reason, (long long) row, col + 1);
    }
  else
    {
      snprintf (msg, sizeof (msg), "%s (row %lld)", reason, (long long) row);
    }
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 1, msg);
  return error_code;
}

static bool
copy_text_is_blank (const char *text, int text_len)
{
  for (int i = 0; i < text_len; i++)
    {
      if (!char_isspace (text[i]))
	{
	  return false;
	}
    }
  return true;
}

copy_cast_result
copy_text_to_column (const char *text, int text_len, const COPY_COL_DOMAIN *dom, DB_VALUE *val)
{
  TP_DOMAIN *domain = (TP_DOMAIN *) dom->domain;
  DB_VALUE src;
  int codeset = LANG_SYS_CODESET;
  int collation = LANG_SYS_COLLATION;

  assert (domain != NULL);

  /* A blank field is not a number, as for the integer and floating types; INSERT's cast reads it as 0. */
  if ((TP_DOMAIN_TYPE (domain) == DB_TYPE_NUMERIC || TP_DOMAIN_TYPE (domain) == DB_TYPE_MONETARY)
      && copy_text_is_blank (text, text_len))
    {
      db_make_null (val);
      return COPY_CAST_BAD_VALUE;
    }

  /* The text is in the column's own codeset where the column has one (ENUM),
   * and JSON text is UTF-8; the cast converts from whatever the source says. */
  if (TP_TYPE_HAS_COLLATION (TP_DOMAIN_TYPE (domain)))
    {
      codeset = dom->codeset;
      collation = dom->collation_id;
    }
  else if (TP_DOMAIN_TYPE (domain) == DB_TYPE_JSON)
    {
      codeset = INTL_CODESET_UTF8;
      collation = LANG_COLL_UTF8_BINARY;
    }

  db_make_varchar (&src, DB_DEFAULT_PRECISION, text, text_len, codeset, collation);

  /* Implicit coercion does not take a string to a bit string or to JSON at all,
   * so those two are cast explicitly, as CAST would; a bit string that does not
   * fit is still refused unless allow_truncated_string says otherwise. */
  DB_TYPE type = TP_DOMAIN_TYPE (domain);
  bool implicit = ! (type == DB_TYPE_BIT || type == DB_TYPE_VARBIT || type == DB_TYPE_JSON);
  TP_DOMAIN_STATUS status = tp_value_cast (&src, val, domain, implicit);
  if (status == DOMAIN_COMPATIBLE)
    {
      return COPY_CAST_OK;
    }

  /* The cast may have raised its own error, which names neither the row nor the
   * column; the caller raises one that does. */
  er_clear ();
  db_make_null (val);
  return (status == DOMAIN_OVERFLOW) ? COPY_CAST_OUT_OF_RANGE : COPY_CAST_BAD_VALUE;
}
