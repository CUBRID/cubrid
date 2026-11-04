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
 * sidecar.cpp - the bodies behind the Sidecar hooks of the heap (sidecar.hpp)
 *
 * No class has a store yet, so a write plan has nothing to do and no record has refs: every body returns at once.
 */

#include "config.h"

#include "sidecar.hpp"

#include "error_code.h"
#include "error_manager.h"
#include "heap_file.h"
#include "object_representation.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/* the default sidecar_inline_target: four records per 16 KB page */
int sidecar_Policy_min_length = 4060;

__attribute__ ((noinline)) int
sidecar_plan_insert (THREAD_ENTRY *thread_p, HEAP_OPERATION_CONTEXT *context)
{
  return NO_ERROR;
}

__attribute__ ((noinline)) int
sidecar_apply_plan (THREAD_ENTRY *thread_p, HEAP_OPERATION_CONTEXT *context)
{
  return NO_ERROR;
}

__attribute__ ((noinline)) int
sidecar_plan_update (THREAD_ENTRY *thread_p, HEAP_OPERATION_CONTEXT *context, const RECDES *old_stored)
{
  return NO_ERROR;
}

__attribute__ ((noinline)) int
sidecar_plan_delete (THREAD_ENTRY *thread_p, HEAP_OPERATION_CONTEXT *context, const RECDES *old_stored)
{
  return NO_ERROR;
}

__attribute__ ((noinline)) void
sidecar_op_end_plan (THREAD_ENTRY *thread_p, HEAP_OPERATION_CONTEXT *context, int rc)
{
}

/* nothing marks a value HEAP_KEPT_REF_ATTRVALUE while no record has refs */
__attribute__ ((noinline)) SCAN_CODE
sidecar_write_kept_ref (HEAP_CACHE_ATTRINFO *attr_info, int value_index, OR_BUF *buf, char **ptr_varvals,
			int offset_size, int header_size)
{
  assert (false);
  return S_ERROR;
}

__attribute__ ((noinline)) SCAN_CODE
sidecar_on_fetch (THREAD_ENTRY *thread_p, HEAP_SCANCACHE *scan_cache, RECDES *recdes)
{
  return S_SUCCESS;
}

__attribute__ ((noinline)) bool
sidecar_point_variable (const HEAP_REC_LAYOUT *layout, HEAP_CACHE_ATTRINFO *attr_info,
			const OR_ATTRIBUTE *attrepr, RECDES *raw)
{
  return false;
}

__attribute__ ((noinline)) char *
sidecar_midxkey_point (HEAP_CACHE_ATTRINFO *attr_info, const RECDES *recdes, const OR_ATTRIBUTE *att,
		       char *disk_data)
{
  return disk_data;
}

__attribute__ ((noinline)) void
sidecar_cursor_release (THREAD_ENTRY *thread_p, HEAP_SCANCACHE *scan_cache)
{
}

#if !defined (NDEBUG)
void
sidecar_assert_record_format (const RECDES *recdes)
{
  if ((recdes->type != REC_HOME && recdes->type != REC_NEWHOME) || recdes->length < OR_MVCC_REP_SIZE
      || !sidecar_record_has_refs (recdes))
    {
      return;
    }

  const char *end = recdes->data + recdes->length;
  const int header_size = OR_HEADER_SIZE (recdes->data);

  assert (!heap_is_big_length (recdes->length - header_size + OR_MVCC_MAX_HEADER_SIZE));
  assert (recdes->length >= header_size + SIDECAR_TRAILER_SIZE (1));
  assert ((unsigned short) OR_GET_SHORT (end - OR_SHORT_SIZE) == SIDECAR_TRAILER_MAGIC);

  const int n_refs = (unsigned short) OR_GET_SHORT (end - 2 * OR_SHORT_SIZE);
  assert (n_refs >= 1 && recdes->length >= header_size + SIDECAR_TRAILER_SIZE (n_refs));

  const char *var_table = recdes->data + header_size;
  const int offset_size = OR_GET_OFFSET_SIZE (recdes->data);
  const char *location_p = end - SIDECAR_TRAILER_SIZE (n_refs);
  const int n_pad = (n_refs + 1) & ~1;
  int previous = -1;

  for (int i = 0; i < n_pad; i++, location_p += OR_SHORT_SIZE)
    {
      const int location = (unsigned short) OR_GET_SHORT (location_p);

      if (i >= n_refs)
	{
	  assert (location == SIDECAR_TRAILER_PAD);
	  continue;
	}
      assert (location > previous);
      previous = location;
      assert (OR_VAR_TABLE_ELEMENT_OFFSET_INTERNAL (var_table, location + 1, offset_size)
	      - OR_VAR_TABLE_ELEMENT_OFFSET_INTERNAL (var_table, location, offset_size) == SIDECAR_REF_SIZE);
    }
}
#endif /* !NDEBUG */
