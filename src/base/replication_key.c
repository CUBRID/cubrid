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


/* Canonical, bounded serialization of the RK actually used for a row log. */
#include "config.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "replication_key.h"
#include "porting.h"
#include "error_manager.h"
#include "intl_support.h"
#include "object_domain.h"
#include "object_primitive.h"
#include "object_representation.h"
#include "storage_common.h"
#if !defined (SERVER_MODE)
#include "authenticate.h"
#include "class_object.h"
#include "schema_manager.h"
#endif
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

struct repl_key_buffer
{
  char *data;
  int length;
  int capacity;
};

/*
 * repl_key_type_is_supported () - check whether an index type can be an RK
 *   return: true for PRIMARY KEY, UNIQUE, or REVERSE UNIQUE
 *   type(in): BTREE_TYPE value
 */
static bool
repl_key_type_is_supported (int type)
{
  return type == BTREE_PRIMARY_KEY || type == BTREE_UNIQUE || type == BTREE_REVERSE_UNIQUE;
}

/*
 * repl_key_buffer_reserve_space() - make room for the next write
 *   return: true on success, false on size or allocation error
 *   b(in/out): buffer whose capacity may grow; length is unchanged
 *   extra(in): bytes needed for the next write
 */
static bool
repl_key_buffer_reserve_space (repl_key_buffer * b, int extra)
{
  if (extra < 0 || b->length > INT_MAX - extra)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HA_GENERIC_ERROR, 1, "RK metadata is too large.");
      return false;
    }

  int needed = b->length + extra;
  if (needed <= b->capacity)
    {
      return true;
    }

  int capacity = b->capacity > 0 && b->capacity <= INT_MAX / 2 ? b->capacity * 2 : needed;
  if (capacity < needed)
    {
      capacity = needed;
    }

  char *data = (char *) realloc (b->data, capacity);
  if (data == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, capacity);
      return false;
    }

  b->data = data;
  b->capacity = capacity;
  return true;
}

/*
 * repl_key_put_int() - append one integer to the RK buffer
 *   return: true on success, false if space cannot be reserved
 *   b(in/out): output buffer
 *   value(in): integer to serialize
 */
static bool
repl_key_put_int (repl_key_buffer * b, int value)
{
  if (!repl_key_buffer_reserve_space (b, OR_INT_SIZE))
    {
      return false;
    }

  OR_PUT_INT (b->data + b->length, value);
  b->length += OR_INT_SIZE;
  return true;
}

/*
 * repl_key_put_name() - append a normalized identifier and alignment padding
 *   return: true on success, false on invalid length or allocation error
 *   b(in/out): output buffer
 *   name(in): identifier; NULL is serialized as an empty name
 * Note: On failure, a length prefix may remain; repl_key_pack discards the buffer.
 */
static bool
repl_key_put_name (repl_key_buffer * b, const char *name)
{
  if (name == NULL)
    {
      name = "";
    }

  int length = intl_identifier_lower_string_size (name) + 1;
  if (length <= 0 || length > INT_MAX - (int) INT_ALIGNMENT)
    {
      return false;
    }

  int padded = DB_ALIGN (length, INT_ALIGNMENT);
  if (!repl_key_put_int (b, length) || !repl_key_buffer_reserve_space (b, padded))
    {
      return false;
    }

  memset (b->data + b->length, 0, padded);
  intl_identifier_lower (name, b->data + b->length);
  b->length += padded;
  return true;
}

/*
 * repl_key_invalid() - report malformed or missing RK metadata
 *   return: ER_HA_GENERIC_ERROR
 */
static int
repl_key_invalid (void)
{
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HA_GENERIC_ERROR, 1, "Invalid or missing RK metadata.");
  return ER_HA_GENERIC_ERROR;
}

/*
 * repl_key_pack() - serialize the chosen constraint and its ordered RK columns
 *   return: NO_ERROR or an error code
 *   name(in): constraint name
 *   type(in): index type
 *   count(in): number of RK columns
 *   columns(in): ordered RK columns
 *   data(out): allocated descriptor owned by the caller
 *   length(out): descriptor length in bytes
 */
int
repl_key_pack (const char *name, int type, int count, const REPL_KEY_COLUMN * columns, char **data, int *length)
{
  repl_key_buffer b = { NULL, 0, 0 };
  *data = NULL;
  *length = 0;

  if (name == NULL || *name == '\0' || count <= 0 || columns == NULL || !repl_key_type_is_supported (type))
    {
      return repl_key_invalid ();
    }

  if (!repl_key_put_int (&b, type) || !repl_key_put_name (&b, name) || !repl_key_put_int (&b, count))
    {
      goto error;
    }

  for (int i = 0; i < count; i++)
    {
      const REPL_KEY_COLUMN *c = &columns[i];
      TP_DOMAIN *domain = c->domain;

      /* Indexed attributes are scalar domains; never serialize cache pointers or physical class OIDs. */
      if (c->id < 0 || c->name == NULL || c->name[0] == '\0' || domain == NULL
	  || domain->next != NULL || domain->setdomain != NULL || !tp_valid_indextype (TP_DOMAIN_TYPE (domain)))
	{
	  repl_key_invalid ();
	  goto error;
	}

      /* The packed domain below carries the type, precision, scale, codeset, collation and enum values. */
      if (!repl_key_put_int (&b, c->id) || !repl_key_put_name (&b, c->name)
	  || !repl_key_put_name (&b, c->domain_class_name)
	  || !repl_key_put_int (&b, c->descending != 0) || !repl_key_put_int (&b, c->not_null != 0))
	{
	  goto error;
	}

      TP_DOMAIN full_domain = *domain;
      full_domain.built_in_index = 0;
      int domain_length = or_packed_domain_size (&full_domain, 0);

      if (domain_length <= 0 || !repl_key_put_int (&b, domain_length)
	  || !repl_key_buffer_reserve_space (&b, domain_length))
	{
	  goto error;
	}

      char *end = or_pack_domain (b.data + b.length, &full_domain, 0, 0);
      if (end == NULL || end != b.data + b.length + domain_length)
	{
	  repl_key_invalid ();
	  goto error;
	}

      b.length += domain_length;
    }

  *data = b.data;
  *length = b.length;
  return NO_ERROR;
error:
  if (b.data != NULL)
    {
      free_and_init (b.data);
    }

  int error = er_errid ();
  return error != NO_ERROR ? error : repl_key_invalid ();
}

struct repl_key_reader
{
  const char *data;
  int remaining;
};

/*
 * repl_key_get_int() - read one integer within the input bounds
 *   return: true on success, false if fewer than OR_INT_SIZE bytes remain
 *   r(in/out): reader position and remaining length
 *   value(out): decoded integer
 */
static bool
repl_key_get_int (repl_key_reader * r, int *value)
{
  if (r->remaining < OR_INT_SIZE)
    {
      return false;
    }

  int word;
  memcpy (&word, r->data, OR_INT_SIZE);
  *value = OR_GET_INT ((char *) &word);
  r->data += OR_INT_SIZE;
  r->remaining -= OR_INT_SIZE;
  return true;
}

/*
 * repl_key_get_name() - read a bounded, NUL-terminated name
 *   return: true on success, false for an invalid name or length
 *   r(in/out): reader position and remaining length
 *   name(out): pointer into the input buffer; do not free
 */
static bool
repl_key_get_name (repl_key_reader * r, const char **name)
{
  int length;
  if (!repl_key_get_int (r, &length) || length <= 0 || length > r->remaining || length > INT_MAX - (int) INT_ALIGNMENT)
    {
      return false;
    }

  int padded = DB_ALIGN (length, INT_ALIGNMENT);
  if (padded > r->remaining || r->data[length - 1] != '\0' || (int) strlen (r->data) != length - 1)
    {
      return false;
    }

  *name = r->data;
  r->data += padded;
  r->remaining -= padded;
  return true;
}

/*
 * repl_key_unpack_log() - extract the class, key, and RK descriptor from a row log
 *   return: true on success, false for a malformed log
 *   data(in): packed log payload
 *   length(in): payload length in bytes
 *   class_name, key, metadata(out): pointers into data; do not free. metadata is NULL with a zero length
 *                                    for a row log written before the RK digest was logged
 *   key_length, metadata_length(out): lengths of the corresponding fields
 */
bool
repl_key_unpack_log (const char *data, int length, const char **class_name, const char **key, int *key_length,
		     const char **metadata, int *metadata_length)
{
  if (data == NULL || length <= 0)
    {
      return false;
    }

  repl_key_reader r = { data, length };
  int class_name_length;
  if (!repl_key_get_int (&r, key_length) || *key_length <= 0
      || !repl_key_get_int (&r, &class_name_length) || class_name_length <= 0
      || class_name_length % INT_ALIGNMENT != 0 || class_name_length > r.remaining)
    {
      return false;
    }

  /* or_pack_string_with_length stores the padded size, unlike RK metadata names. */
  const char *terminator = (const char *) memchr (r.data, '\0', class_name_length);
  if (terminator == NULL || terminator == r.data || r.data + class_name_length - terminator > (int) INT_ALIGNMENT)
    {
      return false;
    }

  for (const char *p = terminator; p < r.data + class_name_length; p++)
    {
      if (*p != '\0')
	{
	  return false;
	}
    }

  *class_name = r.data;
  r.data += class_name_length;
  r.remaining -= class_name_length;

  int padding = (MAX_ALIGNMENT - (r.data - data) % MAX_ALIGNMENT) % MAX_ALIGNMENT;
  if (padding > r.remaining || *key_length > r.remaining - padding)
    {
      return false;
    }

  r.data += padding;
  r.remaining -= padding;
  *key = r.data;
  r.data += *key_length;
  r.remaining -= *key_length;

  padding = (INT_ALIGNMENT - (r.data - data) % INT_ALIGNMENT) % INT_ALIGNMENT;
  if (padding > r.remaining)
    {
      return false;
    }

  r.data += padding;
  r.remaining -= padding;

  if (r.remaining == 0)
    {
      /* a row log written before the RK digest was logged ends here */
      *metadata = NULL;
      *metadata_length = 0;
      return true;
    }

  if (!repl_key_get_int (&r, metadata_length) || *metadata_length != r.remaining
      || *metadata_length != REPL_KEY_DIGEST_SIZE)
    {
      return false;
    }

  *metadata = r.data;
  return true;
}

/*
 * repl_key_is_valid() - check the RK descriptor's fields and bounds
 *   return: true if valid, false otherwise
 *   data(in): packed descriptor
 *   length(in): descriptor length in bytes
 */
static bool
repl_key_is_valid (const char *data, int length)
{
  if (data == NULL || length <= 0)
    {
      return false;
    }

  repl_key_reader r = { data, length };
  int type, count;
  const char *name;
  if (!repl_key_get_int (&r, &type) || !repl_key_type_is_supported (type)
      || !repl_key_get_name (&r, &name) || name[0] == '\0' || !repl_key_get_int (&r, &count)
      || count <= 0 || count > r.remaining / (OR_INT_SIZE * 9))
    {
      return false;
    }

  for (int i = 0; i < count; i++)
    {
      int id, desc, not_null, domain_length;
      const char *column, *class_name;

      if (!repl_key_get_int (&r, &id) || id < 0 || !repl_key_get_name (&r, &column) || column[0] == '\0'
	  || !repl_key_get_name (&r, &class_name)
	  || !repl_key_get_int (&r, &desc) || (desc != 0 && desc != 1)
	  || !repl_key_get_int (&r, &not_null) || (not_null != 0 && not_null != 1)
	  || !repl_key_get_int (&r, &domain_length) || domain_length <= 0 || domain_length > r.remaining
	  || domain_length % INT_ALIGNMENT != 0)
	{
	  return false;
	}

      r.data += domain_length;
      r.remaining -= domain_length;
    }

  return r.remaining == 0;
}

/*
 * repl_key_digest() - digest a packed descriptor for a row log
 *   return: void
 *   data(in): packed descriptor
 *   length(in): descriptor length in bytes
 *   digest(out): REPL_KEY_DIGEST_SIZE bytes, the 64-bit FNV-1a hash of data stored big-endian
 */
void
repl_key_digest (const char *data, int length, char *digest)
{
  UINT64 hash = 14695981039346656037ULL;

  for (int i = 0; i < length; i++)
    {
      hash ^= (unsigned char) data[i];
      hash *= 1099511628211ULL;
    }

  for (int i = REPL_KEY_DIGEST_SIZE - 1; i >= 0; i--)
    {
      digest[i] = (char) (hash & 0xff);
      hash >>= 8;
    }
}

/*
 * repl_key_matches_digest() - compare a row log's digest with a local descriptor
 *   return: true only if the digest is well-formed, the descriptor is valid and their digests are equal
 *   digest(in): digest read from the row log
 *   digest_length(in): its length in bytes
 *   data(in): local packed descriptor
 *   length(in): descriptor length in bytes
 */
bool
repl_key_matches_digest (const char *digest, int digest_length, const char *data, int length)
{
  char local_digest[REPL_KEY_DIGEST_SIZE];

  if (digest == NULL || digest_length != REPL_KEY_DIGEST_SIZE || !repl_key_is_valid (data, length))
    {
      return false;
    }

  repl_key_digest (data, length, local_digest);

  return memcmp (digest, local_digest, REPL_KEY_DIGEST_SIZE) == 0;
}

/*
 * repl_key_describe_digest() - format a digest for an error message or a log dump
 *   return: void
 *   digest(in): digest bytes
 *   digest_length(in): its length in bytes
 *   output(out): text buffer, "digest=<16 hex digits>" or "(invalid RK digest)"
 *   capacity(in): size of output in bytes
 */
void
repl_key_describe_digest (const char *digest, int digest_length, char *output, int capacity)
{
  if (capacity <= 0)
    {
      return;
    }

  if (digest == NULL || digest_length != REPL_KEY_DIGEST_SIZE)
    {
      snprintf (output, capacity, "(invalid RK digest)");
      return;
    }

  snprintf (output, capacity, "digest=%02x%02x%02x%02x%02x%02x%02x%02x", (unsigned char) digest[0],
	    (unsigned char) digest[1], (unsigned char) digest[2], (unsigned char) digest[3], (unsigned char) digest[4],
	    (unsigned char) digest[5], (unsigned char) digest[6], (unsigned char) digest[7]);
}

#if !defined (SERVER_MODE)
/*
 * repl_key_from_class() - describe the client-side RK used for a SQL log
 *   return: NO_ERROR or an error code
 *   class_obj(in): class whose RK is selected
 *   data(out): allocated descriptor owned by the caller, or NULL if no RK exists
 *   length(out): descriptor length in bytes
 */
int
repl_key_from_class (struct db_object *class_obj, char **data, int *length)
{
  SM_CLASS *class_ = NULL;
  SM_CLASS_CONSTRAINT *chosen = NULL;
  REPL_KEY_COLUMN *columns = NULL;
  int error = au_fetch_class_force (class_obj, &class_, AU_FETCH_READ);
  *data = NULL;
  *length = 0;

  if (error != NO_ERROR)
    {
      return error;
    }

  SM_CONSTRAINT_TYPE types[] = { SM_CONSTRAINT_PRIMARY_KEY, SM_CONSTRAINT_UNIQUE, SM_CONSTRAINT_REVERSE_UNIQUE };
  BTREE_TYPE index_types[] = { BTREE_PRIMARY_KEY, BTREE_UNIQUE, BTREE_REVERSE_UNIQUE };
  int kind = BTREE_UNIQUE;

  for (int i = 0; i < 3 && chosen == NULL; i++)
    {
      for (SM_CLASS_CONSTRAINT * c = class_->constraints; c != NULL; c = c->next)
	{
	  if (c->type == types[i] && c->index_status != SM_ONLINE_INDEX_BUILDING_IN_PROGRESS
	      && (c->type == SM_CONSTRAINT_PRIMARY_KEY || sm_has_non_null_attribute (c->attributes)))
	    {
	      chosen = c;
	      kind = index_types[i];
	      break;
	    }
	}
    }

  if (chosen == NULL)
    {
      return NO_ERROR;
    }

  int count = 0;
  while (chosen->attributes[count] != NULL)
    {
      count++;
    }

  columns = (REPL_KEY_COLUMN *) calloc (count, sizeof (*columns));
  if (columns == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, count * sizeof (*columns));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  for (int i = 0; i < count; i++)
    {
      SM_ATTRIBUTE *a = chosen->attributes[i];
      columns[i].id = a->id;
      columns[i].name = a->header.name;
      columns[i].domain = a->domain;
      columns[i].descending = chosen->asc_desc ? chosen->asc_desc[i] : 0;
      columns[i].not_null = (a->flags & SM_ATTFLAG_NON_NULL) != 0;

      if (TP_DOMAIN_TYPE (a->domain) == DB_TYPE_OBJECT && a->domain->class_mop != NULL)
	{
	  columns[i].domain_class_name = sm_get_ch_name (a->domain->class_mop);
	  if (columns[i].domain_class_name == NULL)
	    {
	      error = er_errid ();
	      goto end;
	    }
	}
    }

  error = repl_key_pack (chosen->name, kind, count, columns, data, length);
end:
  free_and_init (columns);
  return error;
}
#endif
