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
#include "oid.h"

struct internal_lob_locator
{
  OID oid;                     /* head chunk OID; NULL only for empty LOB */
  DB_BIGINT length;            /* CLOB: byte length; BLOB: bit length */
  bool adopted = false;        /* loaddb/load-session token already owns storage */
  OID class_oid = OID_INITIALIZER;	/* owner class, carried by a client-facing (signed) locator only */
};
using INTERNAL_LOB_LOCATOR = struct internal_lob_locator;

/*
 * Bounded reverse-order writer: the only way internal LOB chains are written.  An internal LOB is ONE ordinary
 * OOS chunk chain in the heap's FILE_INTERNAL_LOB, with no LOB-specific header; its logical length lives in the
 * heap record's inline field.  Ranges are appended from the end toward offset zero with ONE chunk buffered, so
 * chunks come out tail-first and each knows the OID of the chunk it links to.
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

#endif /* _INTERNAL_LOB_FILE_HPP_ */
