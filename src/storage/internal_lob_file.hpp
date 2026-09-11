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

#ifndef _INTERNAL_LOB_FILE_HPP_
#define _INTERNAL_LOB_FILE_HPP_

#include <cstdio>
#include <limits.h>
#include <string>

#include "dbtype_def.h"
#include "internal_lob_marker.h"
#include "oos_file.hpp"
#include "object_domain.h"

struct internal_lob_locator
{
  OID oid;                     /* head chunk OID; NULL only for empty LOB */
  DB_BIGINT length;            /* CLOB: byte length; BLOB: bit length */
  bool adopted = false;        /* loaddb/load-session token already owns storage */
};
using INTERNAL_LOB_LOCATOR = struct internal_lob_locator;

/*
 * Bounded reverse-order writer: the only way internal LOB chains are written.
 *
 * An internal LOB is stored as ONE ordinary OOS chunk chain (in the heap's own
 * FILE_INTERNAL_LOB), with no LOB-specific header of any kind: the chain header
 * carries the byte length and the next-chunk link, exactly as for an OOS column,
 * and the value's logical length lives in the heap record's inline field.
 *
 * Ranges are appended from the end of the LOB toward offset zero (the network
 * stream sends them that way; seekable sources are read backwards). Only ONE
 * chunk is buffered at a time; a completed chunk goes straight to the OOS chain
 * writer, so chunks come out tail-first and each knows the OID of the chunk it
 * links to without any staging.
 */
struct internal_lob_reverse_writer
{
  VFID lob_vfid;
  char *chunk_buffer = NULL;
  int chunk_capacity = 0;
  DB_BIGINT total_bytes = 0;
  DB_BIGINT logical_length = 0;
  DB_BIGINT expected_offset = 0;
  DB_BIGINT chunk_start = 0;
  DB_BIGINT chunk_end = 0;
  DB_BIGINT chunk_received = 0;
  DB_TYPE lob_type = DB_TYPE_NULL;
  OOS_CHAIN_WRITER chain;
  INTERNAL_LOB_LOCATOR locator;
  bool initialized = false;
  bool finished = false;
};
using INTERNAL_LOB_REVERSE_WRITER = struct internal_lob_reverse_writer;

struct internal_lob_reader
{
  OOS_READER oos_reader;       /* streaming reader over the value's single OOS chunk chain */
  DB_BIGINT logical_length = 0;
  DB_BIGINT total_bytes = 0;   /* physical payload bytes to expose */
  DB_BIGINT total_read = 0;
  bool opened = false;
};
using INTERNAL_LOB_READER = struct internal_lob_reader;

#define INTERNAL_LOB_LOCATOR_PREFIX "@internal_lob:"

struct internal_lob_pending
{
  DB_TYPE lob_type;
  DB_BIGINT size;
  bool delete_after_read;
  char locator[PATH_MAX + 16];
};
using INTERNAL_LOB_PENDING = struct internal_lob_pending;

struct internal_lob_upload_token
{
  DB_TYPE lob_type;
  INT64 token;
  DB_BIGINT data_length;
  DB_BIGINT logical_length;
};
using INTERNAL_LOB_UPLOAD_TOKEN = struct internal_lob_upload_token;

struct internal_lob_dml_slot
{
  int slot;
};
using INTERNAL_LOB_DML_SLOT = struct internal_lob_dml_slot;

extern int internal_lob_create_file (THREAD_ENTRY *thread_p, const HFID &heap_hfid, const OID &class_oid,
				     VFID &lob_vfid);
#if defined (CUBRID_UNIT_TEST_ENABLED)
/* Storage-level tests create Internal LOB files with a synthetic owner descriptor. */
extern int internal_lob_create_file (THREAD_ENTRY *thread_p, VFID &lob_vfid);
#endif /* CUBRID_UNIT_TEST_ENABLED */
extern int internal_lob_remove_file (THREAD_ENTRY *thread_p, const VFID &lob_vfid);
extern int internal_lob_reverse_insert_begin (THREAD_ENTRY *thread_p, const VFID &lob_vfid, DB_TYPE lob_type,
    DB_BIGINT total_bytes, DB_BIGINT logical_length,
    INTERNAL_LOB_REVERSE_WRITER &writer);
extern int internal_lob_reverse_insert_append (THREAD_ENTRY *thread_p, INTERNAL_LOB_REVERSE_WRITER &writer,
    DB_BIGINT offset, oos_buffer chunk);
extern int internal_lob_reverse_insert_end (THREAD_ENTRY *thread_p, INTERNAL_LOB_REVERSE_WRITER &writer,
    INTERNAL_LOB_LOCATOR &locator);
extern void internal_lob_reverse_insert_abort (INTERNAL_LOB_REVERSE_WRITER &writer);
extern int internal_lob_clone (THREAD_ENTRY *thread_p, const VFID &target_lob_vfid, DB_TYPE lob_type,
			       const INTERNAL_LOB_LOCATOR &source_locator, INTERNAL_LOB_LOCATOR &target_locator);
extern int internal_lob_read (THREAD_ENTRY *thread_p, const INTERNAL_LOB_LOCATOR &locator, oos_buffer dest);
extern int internal_lob_read_range (THREAD_ENTRY *thread_p, const INTERNAL_LOB_LOCATOR &locator, DB_BIGINT offset,
				    oos_buffer dest, int &nread);
extern int internal_lob_read_open (THREAD_ENTRY *thread_p, const INTERNAL_LOB_LOCATOR &locator,
				   INTERNAL_LOB_READER &reader);
extern int internal_lob_read_pull (THREAD_ENTRY *thread_p, INTERNAL_LOB_READER &reader, oos_buffer dest, int &nread);
extern int internal_lob_delete (THREAD_ENTRY *thread_p, const VFID &lob_vfid, const INTERNAL_LOB_LOCATOR &locator);

extern bool internal_lob_parse_locator_string (const char *data, int size, INTERNAL_LOB_LOCATOR *locator);
extern bool internal_lob_db_value_is_locator (const DB_VALUE *value, INTERNAL_LOB_LOCATOR *locator);
extern bool internal_lob_db_value_is_pending (const DB_VALUE *value, INTERNAL_LOB_PENDING *pending);
extern bool internal_lob_db_value_is_upload (const DB_VALUE *value, INTERNAL_LOB_UPLOAD_TOKEN *upload);
extern bool internal_lob_db_value_is_dml_slot (const DB_VALUE *value, INTERNAL_LOB_DML_SLOT *dml_slot);
extern int internal_lob_format_locator_string (const INTERNAL_LOB_LOCATOR &locator, char *buf, size_t buf_size,
    bool adopted = false);
inline bool
internal_lob_is_valid_blob_bit_length (DB_BIGINT data_length, DB_BIGINT bit_length)
{
  if (data_length < 0 || bit_length < 0)
    {
      return false;
    }

  if (data_length == 0)
    {
      return bit_length == 0;
    }

  if (data_length > DB_BIGINT_MAX / 8)
    {
      return false;
    }

  return bit_length > (data_length - 1) * 8 && bit_length <= data_length * 8;
}
extern int internal_lob_encode_disk_length (const INTERNAL_LOB_LOCATOR &locator, DB_BIGINT &disk_length);
extern int internal_lob_decode_disk_length (INTERNAL_LOB_LOCATOR &locator, DB_BIGINT disk_length);
extern int internal_lob_make_locator_db_value (DB_VALUE *value, DB_TYPE lob_type, const INTERNAL_LOB_LOCATOR &locator);
extern int internal_lob_make_adopt_locator_db_value (DB_VALUE *value, DB_TYPE lob_type,
    const INTERNAL_LOB_LOCATOR &locator);
extern int internal_lob_read_db_value (THREAD_ENTRY *thread_p, const INTERNAL_LOB_LOCATOR &locator, DB_TYPE lob_type,
				       DB_VALUE *value, TP_DOMAIN *domain);

#endif /* _INTERNAL_LOB_FILE_HPP_ */
