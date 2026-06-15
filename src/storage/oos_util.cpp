/*
 *
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
 * oos_util.cpp - Generic OOS (Out-of-row Overflow Storage) helper utilities
 */

#include "oos_util.hpp"

#include "oid.h"
#include "object_representation.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/*
 * oos_oid_in_vector () - True if oid appears in oids (linear scan; vector is small by design).
 */
bool
oos_oid_in_vector (const std::vector<OID> &oids, const OID *oid)
{
  for (const OID &candidate : oids)
    {
      if (OID_EQ (&candidate, oid))
	{
	  return true;
	}
    }
  return false;
}

#if !defined (NDEBUG)
/*
 * heap_recdes_compute_oos_flag_debug - debug-only audit of OR_MVCC_FLAG_HAS_OOS
 *                                      against the on-disk VOT
 *    return: true if the VOT contains any OOS-flagged entry, false otherwise
 *    recdes(in): heap record being written
 *
 * DO NOT REMOVE THIS.
 *    The only caller is itself wrapped in #if !defined (NDEBUG)
 *    (heap_update_adjust_recdes_header in heap_file.c), so release builds carry
 *    no reference to this symbol and dead-code / unused-symbol sweeps will flag
 *    it for deletion. It is the assert that catches HAS_OOS divergence in debug
 *    builds and is intentionally retained.
 *
 * Note:
 *    Debug-only sanity check used by heap_update_adjust_recdes_header () to
 *    verify the upstream builder (heap_attrinfo_transform_header_to_disk)
 *    stamped HAS_OOS consistently with the VOT contents. The production path
 *    trusts the bit; this walker is the assert that catches divergence.
 *
 *    Defense against non-object-instance records: the heap can hold records
 *    with layouts other than the object-instance VOT format (class records,
 *    root records, etc.). For those, the bytes read as a VOT offset are
 *    arbitrary and could spuriously satisfy OR_IS_OOS.
 *
 *    The caller of this function asserts !OID_IS_ROOTOID() upstream, but we
 *    still validate the first VOT entry as a cheap belt-and-suspenders check:
 *    a real first offset must fall inside [0, recdes->length] (after masking
 *    the two flag bits). Anything outside that range means we are not looking
 *    at a real VOT, and we return false rather than walking arbitrary bytes.
 *
 *    Only index 0 needs the bound check — once the first entry validates,
 *    the loop is self-terminating via OR_IS_OOS or OR_IS_LAST_ELEMENT, and
 *    max_var_count is a backstop against malformed data.
 */
bool
heap_recdes_compute_oos_flag_debug (const RECDES *recdes)
{
  if (recdes == NULL || recdes->data == NULL)
    {
      return false;
    }

  const int offset_size = OR_GET_OFFSET_SIZE (recdes->data);
  void *var_table = OR_GET_OBJECT_VAR_TABLE (recdes->data);
  const int header_size = OR_HEADER_SIZE (recdes->data);
  const int max_var_count = (recdes->length - header_size) / offset_size;

  /* Sanity check: validate the first VOT entry is a reasonable offset.
   * Class/root records have different internal formats — their data area
   * looks like garbage when interpreted as a VOT.
   * VOT offsets are relative to end-of-header, so the valid range is
   * [0, recdes->length - header_size]. */
  if (max_var_count > 0)
    {
      int first;
      switch (offset_size)
	{
	case OR_BYTE_SIZE:
	  first = OR_GET_BYTE (OR_VAR_TABLE_ELEMENT_PTR (var_table, 0, offset_size));
	  break;
	case OR_SHORT_SIZE:
	  first = (unsigned short) OR_GET_SHORT (OR_VAR_TABLE_ELEMENT_PTR (var_table, 0, offset_size));
	  break;
	default:
	  first = OR_GET_INT (OR_VAR_TABLE_ELEMENT_PTR (var_table, 0, offset_size));
	  break;
	}
      int clean = first & ~OR_VAR_FLAG_MASK;
      if (clean < 0 || clean > recdes->length - header_size)
	{
	  return false;
	}
    }

  bool has_oos = false;

  for (int index = 0; index < max_var_count; ++index)
    {
      int offset;
      switch (offset_size)
	{
	case OR_BYTE_SIZE:
	  offset = OR_GET_BYTE (OR_VAR_TABLE_ELEMENT_PTR (var_table, index, offset_size));
	  break;
	case OR_SHORT_SIZE:
	  offset = OR_GET_SHORT (OR_VAR_TABLE_ELEMENT_PTR (var_table, index, offset_size));
	  break;
	case OR_INT_SIZE:
	  offset = OR_GET_INT (OR_VAR_TABLE_ELEMENT_PTR (var_table, index, offset_size));
	  break;
	default:
	  assert (false && "unexpected variable offset size");
	  return false;
	}

      /* The first-entry bound check lives in the pre-loop guard above (which uses the correct
       * end-of-header-relative bound recdes->length - header_size); no need to repeat it here. */

      if (OR_IS_OOS (offset))
	{
	  has_oos = true;
	}

      if (OR_IS_LAST_ELEMENT (offset))
	{
	  /* LAST_ELEMENT found — this is an OOS-aware VOT format.
	   * Trust the OOS detection result. */
	  return has_oos;
	}
    }

  /* No LAST_ELEMENT found — old-format VOT without OOS flag support.
   * Odd offsets in old records would false-positive OR_IS_OOS, so return false. */
  return false;
}
#endif /* !NDEBUG */
