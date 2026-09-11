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

#include "dbtype_def.h"

/*
 * Internal LOB uses ordinary DB_VALUE BLOB/CLOB/VARCHAR/VARBIT containers as a
 * transport envelope.  The user payload must never be classified by string
 * prefix alone.  These marker values are transient, never stored as user domain
 * metadata, and are preserved explicitly by primitive value serialization.
 */
#define DB_VALUE_INTERNAL_LOB_MARKER_NONE        0
#define DB_VALUE_INTERNAL_LOB_MARKER_LOCATOR    (-2691401)
#define DB_VALUE_INTERNAL_LOB_MARKER_FILE_SOURCE (-2691402)
#define DB_VALUE_INTERNAL_LOB_MARKER_PENDING    (-2691403)
#define DB_VALUE_INTERNAL_LOB_MARKER_STREAM     (-2691404)
#define DB_VALUE_INTERNAL_LOB_MARKER_UPLOAD     (-2691405)
#define DB_VALUE_INTERNAL_LOB_MARKER_DML_SLOT   (-2691406)

/* Every marker payload (locator, file source, pending, upload, DML slot, scalar stream) is a text that starts with
 * this prefix.  Disk readers use it, together with the marker word, to tell a transport envelope apart from a
 * user string that happens to share the envelope's leading bytes. */
#define INTERNAL_LOB_MARKER_PAYLOAD_PREFIX "@internal_lob"
#define INTERNAL_LOB_MARKER_PAYLOAD_PREFIX_LEN 13

#define INTERNAL_LOB_FILE_SOURCE_PREFIX "@internal_lob_file:"
#define INTERNAL_LOB_PENDING_PREFIX "@internal_lob_pending:"
#define INTERNAL_LOB_UPLOAD_PREFIX "@internal_lob_upload:"
#define INTERNAL_LOB_DML_SLOT_PREFIX "@internal_lob_dml_slot:"

static inline void
db_value_mark_internal_lob (DB_VALUE * value, int marker)
{
  if (value != NULL)
    {
      value->data.ch.medium.compressed_size = marker;
      value->data.ch.info.compressed_need_clear = false;
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

#endif /* _INTERNAL_LOB_MARKER_H_ */
