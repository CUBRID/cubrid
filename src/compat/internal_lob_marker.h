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

/*
 * internal_lob_marker_locator_token () - The token a locator text carries over its own fields.
 *
 * A hash over the OID, length and adopted flag that readers recompute: prefix plus token, never the prefix alone,
 * tells a real locator from a user string shaped like one.  Kept free of DB_VALUE and the storage headers so the
 * engine, CAS and the client primitives compute it identically.
 */
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
 * internal_lob_marker_format_locator () - Format a locator text; sig == 0 leaves the class and signature out.
 *
 * Format "@internal_lob:[A:]volid|pageid|slotid:length:token[:class_volid|class_pageid|class_slotid:sig]", read back
 * by the parsers below.  A signed locator names its class so its reader can lock it.
 */
static inline int
internal_lob_marker_format_locator (char *buf, size_t buf_size, int volid, int pageid, int slotid, DB_BIGINT length,
				    bool adopted, int class_volid, int class_pageid, int class_slotid,
				    unsigned long long sig)
{
  const char *adopt_tag = adopted ? "A:" : "";
  unsigned long long token = internal_lob_marker_locator_token (volid, pageid, slotid, length, adopted);

  if (sig != 0)
    {
      return snprintf (buf, buf_size, INTERNAL_LOB_LOCATOR_PREFIX "%s%d|%d|%d:%lld:%016llx:%d|%d|%d:%016llx", adopt_tag,
		       volid, pageid, slotid, (long long) length, token, class_volid, class_pageid, class_slotid, sig);
    }

  return snprintf (buf, buf_size, INTERNAL_LOB_LOCATOR_PREFIX "%s%d|%d|%d:%lld:%016llx", adopt_tag, volid, pageid,
		   slotid, (long long) length, token);
}

typedef struct internal_lob_marker_locator_fields INTERNAL_LOB_MARKER_LOCATOR_FIELDS;
struct internal_lob_marker_locator_fields
{
  int volid;
  int pageid;
  int slotid;
  DB_BIGINT length;
  bool adopted;
  int class_volid;		/* class of a signed locator; class_pageid is -1 when the text carries none */
  int class_pageid;
  int class_slotid;
  unsigned long long sig;	/* 0 when the text carries no signature */
};

/*
 * internal_lob_marker_parse_locator_fields () - The one locator grammar: parse the fields and check the token.
 *
 * *consumed is the byte count up to the end of the parsed text; it is short of size when the text has an embedded
 * NUL.  The signature is only returned: the server read handler, which holds the session key, verifies it.
 */
static inline bool
internal_lob_marker_parse_locator_fields (const char *data, int size, INTERNAL_LOB_MARKER_LOCATOR_FIELDS * fields,
					  int *consumed)
{
  char locator_buf[128];
  const char *oid_part = NULL;
  const char *after_token = NULL;
  long long parsed_length = 0;
  unsigned long long parsed_token = 0;
  int oid_consumed = 0;
  int token_consumed = 0;

  if (!internal_lob_marker_copy_text (data, size, INTERNAL_LOB_LOCATOR_PREFIX, locator_buf, (int) sizeof (locator_buf)))
    {
      return false;
    }

  fields->adopted = false;
  fields->class_volid = -1;
  fields->class_pageid = -1;
  fields->class_slotid = -1;
  fields->sig = 0;
  oid_part = locator_buf + strlen (INTERNAL_LOB_LOCATOR_PREFIX);
  if (oid_part[0] == 'A' && oid_part[1] == ':')
    {
      /* adopted-chain locators carry an "A:" tag ahead of the OID */
      fields->adopted = true;
      oid_part += 2;
    }

  if (sscanf (oid_part, "%d|%d|%d:%lld%n", &fields->volid, &fields->pageid, &fields->slotid, &parsed_length,
	      &oid_consumed) != 4 || parsed_length < 0)
    {
      return false;
    }
  if (oid_part[oid_consumed] != ':'
      || sscanf (oid_part + oid_consumed + 1, "%llx%n", &parsed_token, &token_consumed) != 1
      || parsed_token == 0 || token_consumed <= 0)
    {
      return false;
    }

  after_token = oid_part + oid_consumed + 1 + token_consumed;
  if (after_token[0] == ':')
    {
      int signed_consumed = 0;

      if (sscanf (after_token + 1, "%d|%d|%d:%llx%n", &fields->class_volid, &fields->class_pageid,
		  &fields->class_slotid, &fields->sig, &signed_consumed) != 4 || signed_consumed <= 0
	  || after_token[1 + signed_consumed] != '\0')
	{
	  return false;
	}
      after_token += 1 + signed_consumed;
    }
  else if (after_token[0] != '\0')
    {
      return false;
    }

  /* The token is what separates a locator from a user string shaped like one. */
  fields->length = (DB_BIGINT) parsed_length;
  if (parsed_token != internal_lob_marker_locator_token (fields->volid, fields->pageid, fields->slotid, fields->length,
							 fields->adopted))
    {
      return false;
    }

  *consumed = (int) (after_token - locator_buf);
  return true;
}

/*
 * internal_lob_marker_parse_locator () - Pull the payload length out of a locator text.
 *
 * Works on raw bytes, free of DB_VALUE, so engine readers and CAS share one parse.  Returns false for anything that
 * is not a well-formed locator; bytes after an embedded NUL are not looked at.
 */
static inline bool
internal_lob_marker_parse_locator (const char *data, int size, DB_BIGINT * length)
{
  INTERNAL_LOB_MARKER_LOCATOR_FIELDS fields;
  int consumed = 0;

  if (length != NULL)
    {
      *length = 0;
    }

  if (!internal_lob_marker_parse_locator_fields (data, size, &fields, &consumed))
    {
      return false;
    }

  if (length != NULL)
    {
      *length = fields.length;
    }
  return true;
}

/*
 * internal_lob_marker_parse_scalar_stream () - Pull the LOB type, payload length and locator out of a scalar stream
 *                                              marker.
 *
 * Format "@internal_lob_stream:<C|B>:<locator>": what CLOB_TO_CHAR ()/BLOB_TO_BIT () hand csql instead of a value
 * too large for VARCHAR/VARBIT.  It is a transport envelope, not a value: anything consuming it as one must refuse it.
 */
static inline bool
internal_lob_marker_parse_scalar_stream (const char *data, int size, char *lob_type, DB_BIGINT * length,
					 const char **locator, int *locator_len)
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
  if (!internal_lob_marker_parse_locator (data + prefix_len + 2, size - prefix_len - 2, length))
    {
      return false;
    }
  if (locator != NULL)
    {
      *locator = data + prefix_len + 2;
    }
  if (locator_len != NULL)
    {
      *locator_len = size - prefix_len - 2;
    }
  return true;
}

/*
 * internal_lob_marker_parse_file_source () - Split a FILE_SOURCE text (INTERNAL_LOB_FILE_SOURCE_FORMAT) into fields.
 *
 * Grammar only: the caller checks the values.  buf receives the NUL-terminated text and *path points into it.
 */
static inline bool
internal_lob_marker_parse_file_source (const char *data, int size, char *buf, int buf_size, char *lob_type,
				       long long *data_length, long long *logical_length, const char **path)
{
  char *p = NULL;
  char *endptr = NULL;

  if (!internal_lob_marker_copy_text (data, size, INTERNAL_LOB_FILE_SOURCE_PREFIX, buf, buf_size))
    {
      return false;
    }

  p = buf + strlen (INTERNAL_LOB_FILE_SOURCE_PREFIX);
  if ((p[0] != 'C' && p[0] != 'B') || p[1] != ':')
    {
      return false;
    }
  *lob_type = p[0];
  p += 2;

  *data_length = strtoll (p, &endptr, 10);
  if (endptr == p || *endptr != ':')
    {
      return false;
    }
  p = endptr + 1;

  *logical_length = strtoll (p, &endptr, 10);
  if (endptr == p || *endptr != ':')
    {
      return false;
    }

  *path = endptr + 1;
  return true;
}

/*
 * internal_lob_marker_parse_pending () - Split a PENDING text (INTERNAL_LOB_PENDING_FORMAT) into fields.
 *
 * Grammar only: the caller checks the values.  buf receives the NUL-terminated text and *locator points into it.
 * The delete-after-read flag must still read 0 or 1 but is dropped: no reader may delete a file a marker names.
 */
static inline bool
internal_lob_marker_parse_pending (const char *data, int size, char *buf, int buf_size, char *lob_type,
				   long long *pending_size, const char **locator)
{
  const int prefix_len = (int) strlen (INTERNAL_LOB_PENDING_PREFIX);
  int delete_after_read = 0;
  int locator_offset = 0;

  if (!internal_lob_marker_copy_text (data, size, INTERNAL_LOB_PENDING_PREFIX, buf, buf_size))
    {
      return false;
    }

  if (sscanf (buf + prefix_len, "%c:%lld:%d:%n", lob_type, pending_size, &delete_after_read, &locator_offset) < 3
      || locator_offset <= 0 || (delete_after_read != 0 && delete_after_read != 1)
      || (*lob_type != 'C' && *lob_type != 'B'))
    {
      return false;
    }

  *locator = buf + prefix_len + locator_offset;
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
