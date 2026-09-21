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
#include <string.h>

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
#define DB_VALUE_INTERNAL_LOB_MARKER_LOAD_SLOT  (-2691407)

/* Every marker payload (locator, file source, pending, upload, DML slot, load slot, scalar stream) is a text that
 * starts with
 * this prefix.  Disk readers use it, together with the marker word, to tell a transport envelope apart from a
 * user string that happens to share the envelope's leading bytes. */
#define INTERNAL_LOB_MARKER_PAYLOAD_PREFIX "@internal_lob"
#define INTERNAL_LOB_MARKER_PAYLOAD_PREFIX_LEN 13

#define INTERNAL_LOB_LOCATOR_PREFIX "@internal_lob:"
#define INTERNAL_LOB_SCALAR_STREAM_PREFIX "@internal_lob_stream:"
#define INTERNAL_LOB_FILE_SOURCE_PREFIX "@internal_lob_file:"
#define INTERNAL_LOB_PENDING_PREFIX "@internal_lob_pending:"
#define INTERNAL_LOB_UPLOAD_PREFIX "@internal_lob_upload:"
#define INTERNAL_LOB_DML_SLOT_PREFIX "@internal_lob_dml_slot:"
#define INTERNAL_LOB_LOAD_SLOT_PREFIX "@internal_lob_load_slot:"

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


/*
 * internal_lob_marker_locator_token () - The token a locator text carries over its own fields.
 *
 * A locator is handed to clients as plain text, so a reader has to be able to tell a real locator from a user
 * string that merely looks like one.  The token is a hash over the OID, the length and the adopted flag, and a
 * reader recomputes it: prefix plus token is what makes the classification safe, never the prefix alone.  It
 * lives here, free of DB_VALUE and of the storage headers, so the engine, CAS and the client primitives all
 * compute it the same way.
 */
static inline unsigned long long
internal_lob_marker_mix_u64 (unsigned long long value)
{
  value ^= value >> 33;
  value *= 0xff51afd7ed558ccdULL;
  value ^= value >> 33;
  value *= 0xc4ceb9fe1a85ec53ULL;
  value ^= value >> 33;
  return value;
}

static inline unsigned long long
internal_lob_marker_locator_token (int volid, int pageid, int slotid, DB_BIGINT length, bool adopted)
{
  unsigned long long token = 0x26914cbfd15cafe1ULL;

  token ^= (unsigned long long) (unsigned short) volid;
  token = internal_lob_marker_mix_u64 (token);
  token ^= (unsigned long long) (unsigned int) pageid;
  token = internal_lob_marker_mix_u64 (token);
  token ^= (unsigned long long) (unsigned short) slotid;
  token = internal_lob_marker_mix_u64 (token);
  token ^= (unsigned long long) length;
  token = internal_lob_marker_mix_u64 (token);
  token ^= adopted ? 0xad0f7edULL : 0x10c07edULL;
  token = internal_lob_marker_mix_u64 (token);

  if (token == 0)
    {
      token = 1;
    }

  return token;
}

/*
 * internal_lob_marker_parse_locator () - Pull the payload length out of a locator text.
 *
 * A locator reads "@internal_lob:[A:]volid|pageid|slotid:length:token".  The length is the only field a reader
 * needs before it opens a stream, so this works on the raw bytes and stays free of DB_VALUE, letting both the
 * engine-side readers and CAS share one parse.  Returns false for anything that is not a well-formed locator.
 */
static inline bool
internal_lob_marker_parse_locator (const char *data, int size, DB_BIGINT * length)
{
  char locator_buf[128];
  int volid = 0;
  int pageid = 0;
  int slotid = 0;
  long long parsed_length = 0;
  unsigned long long parsed_token = 0;
  int consumed = 0;
  int token_consumed = 0;
  int prefix_len = (int) strlen (INTERNAL_LOB_LOCATOR_PREFIX);
  const char *oid_part = NULL;
  bool adopted = false;

  if (length != NULL)
    {
      *length = 0;
    }

  if (data == NULL || size <= prefix_len || size >= (int) sizeof (locator_buf))
    {
      return false;
    }
  if (memcmp (data, INTERNAL_LOB_LOCATOR_PREFIX, (size_t) prefix_len) != 0)
    {
      return false;
    }

  memcpy (locator_buf, data, (size_t) size);
  locator_buf[size] = '\0';

  oid_part = locator_buf + prefix_len;
  if (oid_part[0] == 'A' && oid_part[1] == ':')
    {
      /* adopted-chain locators carry an "A:" tag ahead of the OID */
      adopted = true;
      oid_part += 2;
    }

  if (sscanf (oid_part, "%d|%d|%d:%lld%n", &volid, &pageid, &slotid, &parsed_length, &consumed) != 4
      || parsed_length < 0)
    {
      return false;
    }
  if (oid_part[consumed] != ':'
      || sscanf (oid_part + consumed + 1, "%llx%n", &parsed_token, &token_consumed) != 1
      || parsed_token == 0 || token_consumed <= 0)
    {
      return false;
    }

  {
    /* An optional per-session signature ":<hex>" may follow the token on a client-facing locator.  This
     * shared parser only classifies the text and returns the length; the signature is verified by the
     * server read handler (session_get_internal_lob_locator_key), which is the only place that holds the key. */
    const char *after_token = oid_part + consumed + 1 + token_consumed;

    if (after_token[0] == ':')
      {
	unsigned long long parsed_sig = 0;
	int sig_consumed = 0;

	if (sscanf (after_token + 1, "%llx%n", &parsed_sig, &sig_consumed) != 1 || sig_consumed <= 0
	    || after_token[1 + sig_consumed] != '\0')
	  {
	    return false;
	  }
      }
    else if (after_token[0] != '\0')
      {
	return false;
      }
  }

  /* The token is what separates a locator from a user string shaped like one. */
  if (parsed_token != internal_lob_marker_locator_token (volid, pageid, slotid, (DB_BIGINT) parsed_length, adopted))
    {
      return false;
    }

  if (length != NULL)
    {
      *length = (DB_BIGINT) parsed_length;
    }
  return true;
}

/*
 * internal_lob_marker_parse_scalar_stream () - Pull the LOB type and payload length out of a scalar stream marker.
 *
 * A scalar stream marker reads "@internal_lob_stream:<C|B>:<locator>".  It is what CLOB_TO_CHAR ()/BLOB_TO_BIT ()
 * hand a csql client instead of a value too large for VARCHAR/VARBIT, and csql pulls the payload through a read
 * cursor.  It is a transport envelope, not a value: anything that would consume it as one has to refuse it.
 */
static inline bool
internal_lob_marker_parse_scalar_stream (const char *data, int size, char *lob_type, DB_BIGINT * length)
{
  const int prefix_len = (int) strlen (INTERNAL_LOB_SCALAR_STREAM_PREFIX);

  if (data == NULL || size <= prefix_len + 2
      || memcmp (data, INTERNAL_LOB_SCALAR_STREAM_PREFIX, (size_t) prefix_len) != 0)
    {
      return false;
    }
  if ((data[prefix_len] != 'C' && data[prefix_len] != 'B') || data[prefix_len + 1] != ':')
    {
      return false;
    }
  if (lob_type != NULL)
    {
      *lob_type = data[prefix_len];
    }
  return internal_lob_marker_parse_locator (data + prefix_len + 2, size - prefix_len - 2, length);
}

#endif /* _INTERNAL_LOB_MARKER_H_ */
