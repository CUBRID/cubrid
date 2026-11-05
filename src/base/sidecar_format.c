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

/*
 * sidecar_format.c - codec of Sidecar hot records, refs and store records, and the write policy (sidecar_format.h)
 */

#ident "$Id$"

#include "config.h"

#include "sidecar_format.h"

#include <limits.h>
#include <string.h>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/* the parts of a record with an object header that the codec reads */
typedef struct sidecar_layout SIDECAR_LAYOUT;
struct sidecar_layout
{
  const char *data;
  int length;
  int header_size;
  int offset_size;
  const char *var_table;
  int var_start;		/* entry[0]: the first byte of the variable area, from the table start */
  int var_end;			/* entry[n_variable] */
};

static int
sidecar_entry (const SIDECAR_LAYOUT * layout, int index)
{
  return OR_VAR_TABLE_ELEMENT_OFFSET_INTERNAL (layout->var_table, index, layout->offset_size);
}

static void
sidecar_put_entry (char *var_table, int index, int offset_size, int offset)
{
  char *entry = OR_VAR_TABLE_ELEMENT_PTR (var_table, index, offset_size);

  OR_PUT_OFFSET_INTERNAL (entry, offset, offset_size);
}

/* the largest offset a table of this offset size holds; the heap picks the size by the same bounds
 * (heap_attrinfo_get_record_header_size ()) */
static int
sidecar_offset_limit (int offset_size)
{
  return offset_size == OR_BYTE_SIZE ? OR_MAX_BYTE : (offset_size == OR_SHORT_SIZE ? OR_MAX_SHORT : INT_MAX);
}

static int
sidecar_layout_init (const RECDES * recdes, int n_variable, SIDECAR_LAYOUT * layout)
{
  if (recdes->data == NULL || recdes->length < OR_MVCC_MIN_HEADER_SIZE || n_variable <= 0)
    {
      return SIDECAR_FORMAT_ERROR;
    }

  layout->data = recdes->data;
  layout->length = recdes->length;
  layout->header_size = OR_HEADER_SIZE (recdes->data);
  layout->offset_size = OR_GET_OFFSET_SIZE (recdes->data);

  const int table_size = (int) OR_VAR_TABLE_SIZE_INTERNAL (n_variable, layout->offset_size);

  if (layout->header_size + table_size > recdes->length)
    {
      return SIDECAR_FORMAT_ERROR;
    }

  layout->var_table = recdes->data + layout->header_size;
  layout->var_start = sidecar_entry (layout, 0);
  layout->var_end = sidecar_entry (layout, n_variable);
  if (layout->var_start < table_size || layout->var_end < layout->var_start
      || layout->header_size + layout->var_end > recdes->length)
    {
      return SIDECAR_FORMAT_ERROR;
    }

  return NO_ERROR;
}

void
sidecar_ref_encode (const SIDECAR_REF * ref, char *slot)
{
  OR_PUT_INT (slot, ref->pageid);
  OR_PUT_SHORT (slot + 4, ref->volid);
  OR_PUT_SHORT (slot + 6, ref->slotid);
  OR_PUT_INT (slot + 8, ref->length);
  OR_PUT_SHORT (slot + 12, ref->flags);
  OR_PUT_SHORT (slot + 14, ref->attrid_check);
}

void
sidecar_ref_decode (const char *slot, SIDECAR_REF * ref)
{
  ref->pageid = OR_GET_INT (slot);
  ref->volid = OR_GET_SHORT (slot + 4);
  ref->slotid = OR_GET_SHORT (slot + 6);
  ref->length = OR_GET_INT (slot + 8);
  ref->flags = (UINT16) OR_GET_SHORT (slot + 12);
  ref->attrid_check = (UINT16) OR_GET_SHORT (slot + 14);
}

int
sidecar_trailer_n_refs (const RECDES * recdes)
{
  if (recdes->length < OR_MVCC_MIN_HEADER_SIZE || !sidecar_record_has_refs (recdes))
    {
      return 0;
    }

  const char *end = recdes->data + recdes->length;
  const int header_size = OR_HEADER_SIZE (recdes->data);

  if (recdes->length < header_size + SIDECAR_TRAILER_SIZE (1)
      || (unsigned short) OR_GET_SHORT (end - OR_SHORT_SIZE) != SIDECAR_TRAILER_MAGIC)
    {
      return SIDECAR_FORMAT_ERROR;
    }

  const int n_refs = (unsigned short) OR_GET_SHORT (end - 2 * OR_SHORT_SIZE);

  if (n_refs < 1 || recdes->length < header_size + SIDECAR_TRAILER_SIZE (n_refs))
    {
      return SIDECAR_FORMAT_ERROR;
    }
  return n_refs;
}

int
sidecar_trailer_n_variable (const RECDES * recdes)
{
  return (unsigned short) OR_GET_SHORT (recdes->data + recdes->length - 3 * OR_SHORT_SIZE);
}

int
sidecar_trailer_location (const RECDES * recdes, int index)
{
  const int n_refs = (unsigned short) OR_GET_SHORT (recdes->data + recdes->length - 2 * OR_SHORT_SIZE);
  const char *locations = recdes->data + recdes->length - SIDECAR_TRAILER_SIZE (n_refs);

  return (unsigned short) OR_GET_SHORT (locations + index * OR_SHORT_SIZE);
}

int
sidecar_trailer_find (const RECDES * recdes, int location)
{
  const int n_refs = sidecar_trailer_n_refs (recdes);

  for (int i = 0; i < n_refs; i++)
    {
      const int ref_location = sidecar_trailer_location (recdes, i);

      if (ref_location == location)
	{
	  return i;
	}
      if (ref_location > location)
	{
	  break;
	}
    }
  return -1;
}

bool
sidecar_hot_is_well_formed (const RECDES * recdes)
{
  SIDECAR_LAYOUT layout;
  const int n_refs = sidecar_trailer_n_refs (recdes);

  if (n_refs < 1)
    {
      return false;
    }

  const int n_variable = sidecar_trailer_n_variable (recdes);

  if (n_refs > n_variable || sidecar_layout_init (recdes, n_variable, &layout) != NO_ERROR)
    {
      return false;
    }
  if (layout.header_size + layout.var_end != recdes->length - SIDECAR_TRAILER_SIZE (n_refs))
    {
      return false;
    }

  const char *locations = recdes->data + recdes->length - SIDECAR_TRAILER_SIZE (n_refs);
  int previous = -1;

  for (int i = 0; i < SIDECAR_TRAILER_N_PAD (n_refs); i++)
    {
      const int location = (unsigned short) OR_GET_SHORT (locations + i * OR_SHORT_SIZE);

      if (i >= n_refs)
	{
	  if (location != SIDECAR_TRAILER_PAD)
	    {
	      return false;
	    }
	  continue;
	}
      if (location <= previous || location >= n_variable
	  || sidecar_entry (&layout, location + 1) - sidecar_entry (&layout, location) != SIDECAR_REF_SIZE)
	{
	  return false;
	}
      previous = location;
    }
  return true;
}

void
sidecar_hot_get_ref (const RECDES * recdes, int index, SIDECAR_REF * ref)
{
  const int header_size = OR_HEADER_SIZE (recdes->data);
  const int offset_size = OR_GET_OFFSET_SIZE (recdes->data);
  const char *var_table = recdes->data + header_size;
  const int location = sidecar_trailer_location (recdes, index);

  sidecar_ref_decode (var_table + OR_VAR_TABLE_ELEMENT_OFFSET_INTERNAL (var_table, location, offset_size), ref);
}

int
sidecar_var_slot_length (const RECDES * recdes, int location)
{
  const char *var_table = recdes->data + OR_HEADER_SIZE (recdes->data);
  const int offset_size = OR_GET_OFFSET_SIZE (recdes->data);

  return OR_VAR_TABLE_ELEMENT_LENGTH_INTERNAL (var_table, location, offset_size);
}

int
sidecar_hot_length (const RECDES * image, int n_variable, int n_slots, const SIDECAR_SLOT * slots)
{
  SIDECAR_LAYOUT layout;

  if (n_slots < 1 || n_slots > n_variable || n_variable > SIDECAR_TRAILER_PAD - 1 || sidecar_record_has_refs (image)
      || sidecar_layout_init (image, n_variable, &layout) != NO_ERROR)
    {
      return SIDECAR_FORMAT_ERROR;
    }

  int var_end = layout.var_end;

  for (int i = 0; i < n_slots; i++)
    {
      const int location = slots[i].location;

      if (location < 0 || location >= n_variable || (i > 0 && location <= slots[i - 1].location))
	{
	  return SIDECAR_FORMAT_ERROR;
	}
      var_end += SIDECAR_REF_SIZE - (sidecar_entry (&layout, location + 1) - sidecar_entry (&layout, location));
    }
  if (var_end > sidecar_offset_limit (layout.offset_size))
    {
      return SIDECAR_FORMAT_ERROR;
    }

  return layout.header_size + var_end + SIDECAR_TRAILER_SIZE (n_slots);
}

int
sidecar_build_hot (const RECDES * image, int n_variable, int n_slots, const SIDECAR_SLOT * slots, RECDES * hot)
{
  SIDECAR_LAYOUT layout;
  const int length = sidecar_hot_length (image, n_variable, n_slots, slots);

  if (length < 0 || hot->area_size < length || sidecar_layout_init (image, n_variable, &layout) != NO_ERROR)
    {
      return SIDECAR_FORMAT_ERROR;
    }

  char *var_table = hot->data + layout.header_size;
  int position = layout.var_start;
  int k = 0;

  memcpy (hot->data, image->data, layout.header_size + layout.var_start);
  OR_PUT_INT (hot->data, OR_GET_INT (image->data) | (OR_MVCC_FLAG_HAS_SIDECAR << OR_MVCC_FLAG_SHIFT_BITS));

  for (int j = 0; j < n_variable; j++)
    {
      sidecar_put_entry (var_table, j, layout.offset_size, position);
      if (k < n_slots && slots[k].location == j)
	{
	  sidecar_ref_encode (&slots[k].ref, var_table + position);
	  position += SIDECAR_REF_SIZE;
	  k++;
	}
      else
	{
	  const int start = sidecar_entry (&layout, j);
	  const int slot_length = sidecar_entry (&layout, j + 1) - start;

	  memcpy (var_table + position, layout.var_table + start, slot_length);
	  position += slot_length;
	}
    }
  sidecar_put_entry (var_table, n_variable, layout.offset_size, position);

  char *trailer = var_table + position;

  for (int i = 0; i < SIDECAR_TRAILER_N_PAD (n_slots); i++)
    {
      OR_PUT_SHORT (trailer + i * OR_SHORT_SIZE, i < n_slots ? slots[i].location : SIDECAR_TRAILER_PAD);
    }
  OR_PUT_SHORT (hot->data + length - 3 * OR_SHORT_SIZE, n_variable);
  OR_PUT_SHORT (hot->data + length - 2 * OR_SHORT_SIZE, n_slots);
  OR_PUT_SHORT (hot->data + length - OR_SHORT_SIZE, SIDECAR_TRAILER_MAGIC);

  hot->length = length;
  hot->type = image->type;
  return NO_ERROR;
}

int
sidecar_expanded_length (const RECDES * hot)
{
  SIDECAR_LAYOUT layout;

  if (!sidecar_hot_is_well_formed (hot) || sidecar_layout_init (hot, sidecar_trailer_n_variable (hot), &layout)
      != NO_ERROR)
    {
      return SIDECAR_FORMAT_ERROR;
    }

  const int n_refs = sidecar_trailer_n_refs (hot);
  long long var_end = layout.var_end;

  for (int i = 0; i < n_refs; i++)
    {
      SIDECAR_REF ref;

      sidecar_hot_get_ref (hot, i, &ref);
      if (ref.length < 0)
	{
	  return SIDECAR_FORMAT_ERROR;
	}
      var_end += ref.length - SIDECAR_REF_SIZE;
    }
  if (var_end > sidecar_offset_limit (layout.offset_size) || layout.header_size + var_end > INT_MAX)
    {
      return SIDECAR_FORMAT_ERROR;
    }

  return layout.header_size + (int) var_end;
}

int
sidecar_expand_frame (const RECDES * hot, RECDES * out, int *value_offsets)
{
  SIDECAR_LAYOUT layout;
  const int length = sidecar_expanded_length (hot);

  if (length < 0 || out->area_size < length)
    {
      return SIDECAR_FORMAT_ERROR;
    }

  const int n_variable = sidecar_trailer_n_variable (hot);

  if (sidecar_layout_init (hot, n_variable, &layout) != NO_ERROR)
    {
      return SIDECAR_FORMAT_ERROR;
    }

  const int n_refs = sidecar_trailer_n_refs (hot);
  char *var_table = out->data + layout.header_size;
  int position = layout.var_start;
  int k = 0;

  memcpy (out->data, hot->data, layout.header_size + layout.var_start);
  OR_PUT_INT (out->data, OR_GET_INT (hot->data) & ~(OR_MVCC_FLAG_HAS_SIDECAR << OR_MVCC_FLAG_SHIFT_BITS));

  for (int j = 0; j < n_variable; j++)
    {
      sidecar_put_entry (var_table, j, layout.offset_size, position);
      if (k < n_refs && sidecar_trailer_location (hot, k) == j)
	{
	  SIDECAR_REF ref;

	  sidecar_hot_get_ref (hot, k, &ref);
	  value_offsets[k] = layout.header_size + position;
	  position += ref.length;
	  k++;
	}
      else
	{
	  const int start = sidecar_entry (&layout, j);
	  const int slot_length = sidecar_entry (&layout, j + 1) - start;

	  memcpy (var_table + position, layout.var_table + start, slot_length);
	  position += slot_length;
	}
    }
  sidecar_put_entry (var_table, n_variable, layout.offset_size, position);

  out->length = length;
  out->type = hot->type;
  return NO_ERROR;
}

void
sidecar_store_record_build (int attrid, const char *value, int length, RECDES * out)
{
  OR_PUT_INT (out->data, SIDECAR_STORE_REPID);
  OR_PUT_INT (out->data + OR_CHN_OFFSET, 0);
  OR_PUT_INT (out->data + OR_MVCC_MIN_HEADER_SIZE, attrid);
  OR_PUT_SHORT (out->data + OR_MVCC_MIN_HEADER_SIZE + 4, SIDECAR_VALUE_FORMAT);
  OR_PUT_SHORT (out->data + OR_MVCC_MIN_HEADER_SIZE + 6, 0);
  memcpy (out->data + OR_MVCC_MIN_HEADER_SIZE + SIDECAR_VALUE_HEADER_SIZE, value, length);

  out->length = SIDECAR_STORE_RECORD_LENGTH (length);
  out->type = REC_HOME;
}

int
sidecar_store_record_parse (const RECDES * recdes, SIDECAR_VALUE_HEADER * header, const char **value, int *length)
{
  if (recdes->data == NULL || recdes->length < OR_MVCC_MIN_HEADER_SIZE
      || OR_GET_MVCC_REPID (recdes->data) != SIDECAR_STORE_REPID || sidecar_record_has_refs (recdes))
    {
      return SIDECAR_FORMAT_ERROR;
    }

  const int header_size = OR_HEADER_SIZE (recdes->data);
  const char *value_header = recdes->data + header_size;

  if (recdes->length < header_size + SIDECAR_VALUE_HEADER_SIZE)
    {
      return SIDECAR_FORMAT_ERROR;
    }

  header->attrid = OR_GET_INT (value_header);
  header->format = OR_GET_SHORT (value_header + 4);
  header->reserved = OR_GET_SHORT (value_header + 6);
  if (header->format != SIDECAR_VALUE_FORMAT)
    {
      return SIDECAR_FORMAT_ERROR;
    }

  *value = value_header + SIDECAR_VALUE_HEADER_SIZE;
  *length = recdes->length - header_size - SIDECAR_VALUE_HEADER_SIZE;
  return NO_ERROR;
}

static bool
sidecar_policy_is_chosen (const int *chosen, int n_chosen, int index)
{
  for (int i = 0; i < n_chosen; i++)
    {
      if (chosen[i] == index)
	{
	  return true;
	}
    }
  return false;
}

/* the hot record an image of length becomes once refs replace values that held saved bytes more than they */
static int
sidecar_policy_estimate (int length, int saved, int n_chosen)
{
  return length - saved + (n_chosen > 0 ? SIDECAR_TRAILER_SIZE (n_chosen) : 0);
}

bool
sidecar_policy_admits (const SIDECAR_IMAGE_FACTS * facts)
{
  return (facts->mvcc_user_class && !facts->supplemental_log && !facts->ha_enabled && !facts->has_delid
	  && !facts->page_held);
}

int
sidecar_policy_choose (const SIDECAR_POLICY * policy, const SIDECAR_IMAGE_FACTS * facts,
		       const SIDECAR_CANDIDATE * candidates, int n_candidates, int *chosen_locations)
{
  if (!sidecar_policy_admits (facts))
    {
      return 0;
    }

  const int external_min = policy->force_external ? SIDECAR_FORCE_MIN_VALUE_SIZE : policy->min_value_size;
  int *chosen = chosen_locations;	/* indices into candidates until the end */
  int n_chosen = 0;
  int saved = 0;

  for (int i = 0; i < n_candidates; i++)
    {
      const SIDECAR_CANDIDATE *c = &candidates[i];

      if (c->eligible && (c->external || policy->force_external) && c->length >= external_min)
	{
	  chosen[n_chosen++] = i;
	  saved += c->length - SIDECAR_REF_SIZE;
	}
    }
  if (n_chosen == 0 && facts->length <= policy->inline_target)
    {
      return 0;
    }

  while (sidecar_policy_estimate (facts->length, saved, n_chosen) > policy->inline_target)
    {
      int best = -1;

      for (int i = 0; i < n_candidates; i++)
	{
	  const SIDECAR_CANDIDATE *c = &candidates[i];

	  if (c->eligible && c->length >= policy->min_value_size && (best < 0 || c->length > candidates[best].length)
	      && !sidecar_policy_is_chosen (chosen, n_chosen, i))
	    {
	      best = i;
	    }
	}
      if (best < 0)
	{
	  break;
	}
      chosen[n_chosen++] = best;
      saved += candidates[best].length - SIDECAR_REF_SIZE;
    }

  if (n_chosen == 0 || sidecar_policy_estimate (facts->length, saved, n_chosen) - facts->header_size > policy->headroom)
    {
      return 0;
    }

  /* indices to locations, ascending: the order of the trailer */
  for (int i = 0; i < n_chosen; i++)
    {
      chosen[i] = candidates[chosen[i]].location;
    }
  for (int i = 1; i < n_chosen; i++)
    {
      const int location = chosen[i];
      int j = i - 1;

      for (; j >= 0 && chosen[j] > location; j--)
	{
	  chosen[j + 1] = chosen[j];
	}
      chosen[j + 1] = location;
    }
  return n_chosen;
}
