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

static bool
repl_key_reserve (repl_key_buffer *b, int extra)
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

static bool
repl_key_put_int (repl_key_buffer *b, int value)
{
  if (!repl_key_reserve (b, OR_INT_SIZE))
    {
      return false;
    }
  OR_PUT_INT (b->data + b->length, value);
  b->length += OR_INT_SIZE;
  return true;
}

static bool
repl_key_put_name (repl_key_buffer *b, const char *name)
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
  if (!repl_key_put_int (b, length) || !repl_key_reserve (b, padded))
    {
      return false;
    }
  memset (b->data + b->length, 0, padded);
  intl_identifier_lower (name, b->data + b->length);
  b->length += padded;
  return true;
}

static int
repl_key_invalid (void)
{
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HA_GENERIC_ERROR, 1, "Invalid or missing RK metadata.");
  return ER_HA_GENERIC_ERROR;
}

int
repl_key_pack (const char *name, int type, int count, const REPL_KEY_COLUMN *columns, char **data, int *length)
{
  repl_key_buffer b = { NULL, 0, 0 };
  TP_DOMAIN *copy = NULL;
  *data = NULL;
  *length = 0;
  if (name == NULL || name[0] == '\0' || count <= 0 || columns == NULL
      || (type != BTREE_PRIMARY_KEY && type != BTREE_UNIQUE && type != BTREE_REVERSE_UNIQUE))
    {
      return repl_key_invalid ();
    }
  if (!repl_key_put_int (&b, REPL_KEY_METADATA_VERSION) || !repl_key_put_int (&b, type)
      || !repl_key_put_name (&b, name) || !repl_key_put_int (&b, count))
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
      if (!repl_key_put_int (&b, c->id) || !repl_key_put_name (&b, c->name)
          || !repl_key_put_int (&b, TP_DOMAIN_TYPE (domain)) || !repl_key_put_int (&b, domain->precision)
          || !repl_key_put_int (&b, domain->scale) || !repl_key_put_int (&b, domain->codeset)
          || !repl_key_put_int (&b, domain->collation_id) || !repl_key_put_int (&b, domain->collation_flag)
          || !repl_key_put_int (&b, domain->is_parameterized) || !repl_key_put_int (&b, domain->self_ref)
          || !repl_key_put_name (&b, c->domain_class_name)
          || !repl_key_put_int (&b, c->descending != 0) || !repl_key_put_int (&b, c->not_null != 0))
        {
          goto error;
        }
      copy = tp_domain_copy (domain, false);
      if (copy == NULL)
        {
          goto error;
        }
      copy->built_in_index = 0;
      copy->collation_flag = domain->collation_flag;
      int domain_length = or_packed_domain_size (copy, 0);
      if (domain_length <= 0 || !repl_key_put_int (&b, domain_length) || !repl_key_reserve (&b, domain_length))
        {
          goto error;
        }
      char *end = or_pack_domain (b.data + b.length, copy, 0, 0);
      if (end == NULL || end != b.data + b.length + domain_length)
        {
          repl_key_invalid ();
          goto error;
        }
      b.length += domain_length;
      tp_domain_free (copy);
      copy = NULL;
    }
  *data = b.data;
  *length = b.length;
  return NO_ERROR;
error:
  if (copy != NULL)
    {
      tp_domain_free (copy);
    }
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

static bool
repl_key_get_int (repl_key_reader *r, int *value)
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

static bool
repl_key_get_name (repl_key_reader *r, const char **name)
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

/* Parse the data-log envelope without trusting offsets or an optional legacy tail. */
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
  if (!repl_key_get_int (&r, metadata_length) || *metadata_length != r.remaining
      || !repl_key_is_valid (r.data, *metadata_length))
    {
      return false;
    }
  *metadata = r.data;
  return true;
}

bool
repl_key_is_valid (const char *data, int length)
{
  if (data == NULL || length <= 0)
    {
      return false;
    }
  repl_key_reader r = { data, length };
  int version, type, count, value;
  const char *name;
  if (!repl_key_get_int (&r, &version) || version != REPL_KEY_METADATA_VERSION
      || !repl_key_get_int (&r, &type)
      || (type != BTREE_PRIMARY_KEY && type != BTREE_UNIQUE && type != BTREE_REVERSE_UNIQUE)
      || !repl_key_get_name (&r, &name) || name[0] == '\0' || !repl_key_get_int (&r, &count)
      || count <= 0 || count > r.remaining / (OR_INT_SIZE * 12))
    {
      return false;
    }
  for (int i = 0; i < count; i++)
    {
      if (!repl_key_get_int (&r, &value) || value < 0 || !repl_key_get_name (&r, &name) || name[0] == '\0')
        {
          return false;
        }
      for (int j = 0; j < 8; j++)
        {
          if (!repl_key_get_int (&r, &value))
            {
              return false;
            }
        }
      if (!repl_key_get_name (&r, &name))
        {
          return false;
        }
      for (int j = 0; j < 2; j++)
        {
          if (!repl_key_get_int (&r, &value) || (value != 0 && value != 1))
            {
              return false;
            }
        }
      int domain_length;
      if (!repl_key_get_int (&r, &domain_length) || domain_length <= 0 || domain_length > r.remaining
          || domain_length % INT_ALIGNMENT != 0)
        {
          return false;
        }
      r.data += domain_length;
      r.remaining -= domain_length;
    }
  return r.remaining == 0;
}

bool
repl_key_equal (const char *left, int left_length, const char *right, int right_length)
{
  return repl_key_is_valid (left, left_length) && repl_key_is_valid (right, right_length)
    && left_length == right_length && memcmp (left, right, left_length) == 0;
}

void
repl_key_describe (const char *data, int length, char *output, int capacity)
{
  if (capacity <= 0)
    {
      return;
    }
  if (!repl_key_is_valid (data, length))
    {
      snprintf (output, capacity, "%s", data == NULL || length == 0 ? "(no RK)" : "(invalid RK metadata)");
      return;
    }
  repl_key_reader r = { data, length };
  int version = 0, type = 0, count = 0;
  const char *name = "";
  repl_key_get_int (&r, &version);
  repl_key_get_int (&r, &type);
  repl_key_get_name (&r, &name);
  repl_key_get_int (&r, &count);
  const char *type_name = type == BTREE_PRIMARY_KEY ? "PRIMARY KEY"
    : type == BTREE_REVERSE_UNIQUE ? "REVERSE UNIQUE" : "UNIQUE";
  int used = snprintf (output, capacity, "constraint='%s', type=%s, columns=%d", name, type_name, count);
  for (int i = 0; i < count && used >= 0 && used < capacity; i++)
    {
      int id = 0, fields[8] = { 0 }, desc = 0, not_null = 0, domain_length = 0;
      const char *column = "", *class_name = "";
      repl_key_get_int (&r, &id);
      repl_key_get_name (&r, &column);
      for (int j = 0; j < 8; j++)
        {
          repl_key_get_int (&r, &fields[j]);
        }
      repl_key_get_name (&r, &class_name);
      repl_key_get_int (&r, &desc);
      repl_key_get_int (&r, &not_null);
      repl_key_get_int (&r, &domain_length);
      r.data += domain_length;
      r.remaining -= domain_length;
      const char *domain_type = pr_type_name ((DB_TYPE) fields[0]);
      int written = snprintf (output + used, capacity - used,
                              " [column='%s', id=%d, domain=%s(%d,%d), charset=%d, collation=%d, "
                              "collation_flag=%d, parameterized=%d, self_ref=%d, class='%s', order=%s, %s]",
                              column, id, domain_type ? domain_type : "UNKNOWN", fields[1], fields[2], fields[3],
                              fields[4], fields[5], fields[6], fields[7], class_name,
                              desc ? "DESC" : "ASC", not_null ? "NOT NULL" : "NULL");
      if (written < 0 || written >= capacity - used)
        {
          break;
        }
      used += written;
    }
}

#if !defined (SERVER_MODE)
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
      for (SM_CLASS_CONSTRAINT *c = class_->constraints; c != NULL; c = c->next)
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
