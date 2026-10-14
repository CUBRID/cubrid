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

#ifndef _INTERNAL_LOB_MARKER_H_
#define _INTERNAL_LOB_MARKER_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dbtype_def.h"

/*
 * Transient markers tagging a BLOB/CLOB/VARCHAR/VARBIT DB_VALUE as an Internal LOB transport envelope: never stored
 * as domain metadata, carried explicitly by primitive serialization, and never inferred from a string prefix alone.
 */
#define DB_VALUE_INTERNAL_LOB_MARKER_NONE        0
#define DB_VALUE_INTERNAL_LOB_MARKER_LOCATOR    (-2691401)
#define DB_VALUE_INTERNAL_LOB_MARKER_FILE_SOURCE (-2691402)
#define DB_VALUE_INTERNAL_LOB_MARKER_PENDING    (-2691403)
#define DB_VALUE_INTERNAL_LOB_MARKER_STREAM     (-2691404)
#define DB_VALUE_INTERNAL_LOB_MARKER_UPLOAD     (-2691405)
#define DB_VALUE_INTERNAL_LOB_MARKER_DML_SLOT   (-2691406)
#define DB_VALUE_INTERNAL_LOB_MARKER_LOAD_SLOT  (-2691407)

/* Prefix of every marker payload text; readers need it plus the marker word to tell an envelope from a user string. */
#define INTERNAL_LOB_MARKER_PAYLOAD_PREFIX "@internal_lob"
#define INTERNAL_LOB_MARKER_PAYLOAD_PREFIX_LEN 13

#define INTERNAL_LOB_LOCATOR_PREFIX "@internal_lob:"
#define INTERNAL_LOB_SCALAR_STREAM_PREFIX "@internal_lob_stream:"
#define INTERNAL_LOB_FILE_SOURCE_PREFIX "@internal_lob_file:"
#define INTERNAL_LOB_PENDING_PREFIX "@internal_lob_pending:"
#define INTERNAL_LOB_UPLOAD_PREFIX "@internal_lob_upload:"
#define INTERNAL_LOB_DML_SLOT_PREFIX "@internal_lob_dml_slot:"
#define INTERNAL_LOB_LOAD_SLOT_PREFIX "@internal_lob_load_slot:"

/* <C|B>:<data_length>:<logical_length, -1 for CLOB>:<path> */
#define INTERNAL_LOB_FILE_SOURCE_FORMAT INTERNAL_LOB_FILE_SOURCE_PREFIX "%c:%lld:%lld:%s"
/* <C|B>:<size>:<delete-after-read flag, written 0 and ignored>:<ES locator> */
#define INTERNAL_LOB_PENDING_FORMAT INTERNAL_LOB_PENDING_PREFIX "%c:%lld:0:%s"

static inline void
db_value_mark_internal_lob (DB_VALUE * value, int marker)
{
  if (value != NULL)
    {
      /* The marker lives in compressed_size, so marking loses that field's original meaning.  Do NOT touch
       * compressed_need_clear here: it records who owns compressed_buf, and pr_clear_value () frees the
       * buffer from that flag alone (it never consults compressed_size).  Clearing it leaked the buffer. */
      value->data.ch.medium.compressed_size = marker;
    }
}

static inline int
db_value_get_internal_lob_marker (const DB_VALUE * value)
{
  if (value == NULL || value->domain.general_info.is_null != 0)
    {
      return DB_VALUE_INTERNAL_LOB_MARKER_NONE;
    }

  if (value->data.ch.info.style != MEDIUM_STRING)
    {
      return DB_VALUE_INTERNAL_LOB_MARKER_NONE;
    }

  return value->data.ch.medium.compressed_size;
}

static inline bool
db_value_has_internal_lob_marker (const DB_VALUE * value, int marker)
{
  return db_value_get_internal_lob_marker (value) == marker;
}

/* Is this an envelope whose payload is not in storage yet?  Only the server heap path resolves one. */
static inline bool
internal_lob_marker_is_unresolved_transport (int marker)
{
  switch (marker)
    {
    case DB_VALUE_INTERNAL_LOB_MARKER_FILE_SOURCE:
    case DB_VALUE_INTERNAL_LOB_MARKER_PENDING:
    case DB_VALUE_INTERNAL_LOB_MARKER_STREAM:
    case DB_VALUE_INTERNAL_LOB_MARKER_UPLOAD:
    case DB_VALUE_INTERNAL_LOB_MARKER_DML_SLOT:
    case DB_VALUE_INTERNAL_LOB_MARKER_LOAD_SLOT:
      return true;
    default:
      return false;
    }
}

static inline bool
internal_lob_marker_is_known (int marker)
{
  return marker == DB_VALUE_INTERNAL_LOB_MARKER_LOCATOR || internal_lob_marker_is_unresolved_transport (marker);
}

/* Copy a marker text that starts with prefix and fits buf (with its NUL) into buf, NUL-terminated. */
static inline bool
internal_lob_marker_copy_text (const char *data, int size, const char *prefix, char *buf, int buf_size)
{
  const int prefix_len = (int) strlen (prefix);

  if (data == NULL || size <= prefix_len || size >= buf_size || memcmp (data, prefix, (size_t) prefix_len) != 0)
    {
      return false;
    }

  memcpy (buf, data, (size_t) size);
  buf[size] = '\0';
  return true;
}


/* Split a locator's logical length into payload bytes and BLOB bit length (0 for a CLOB); false if it overflows. */
static inline bool
internal_lob_marker_split_length (char lob_type, DB_BIGINT logical_length, DB_BIGINT * data_len, DB_BIGINT * bit_length)
{
  DB_BIGINT bytes = logical_length;
  DB_BIGINT bits = 0;

  if (lob_type == 'B')
    {
      if (logical_length > DB_BIGINT_MAX - 7)
	{
	  return false;
	}
      bytes = (logical_length + 7) / 8;
      bits = logical_length;
    }

  if (data_len != NULL)
    {
      *data_len = bytes;
    }
  if (bit_length != NULL)
    {
      *bit_length = bits;
    }
  return true;
}

#endif /* _INTERNAL_LOB_MARKER_H_ */
