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
 * ftab_set.cpp
 */

#include "config.h"

#include "ftab_set.hpp"

#include "bit.h"
#include "error_code.h"
#include "error_manager.h"
#include "file_manager.h"
#include "memory_alloc.h"
#include "statistics.h"
#include "storage_common.h"
#include "thread_compat.hpp"

#include <algorithm>
#include <random>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

int
collect_strided_vpids (THREAD_ENTRY *thread_p, const HFID *hfid,
		       VPID **out_picked, int *out_count, int *out_total_data_pages)
{
  FILE_FTAB_COLLECTOR collector = FILE_FTAB_COLLECTOR_INITIALIZER;
  VPID *picked = NULL;
  int picked_count = 0;
  int error_code = NO_ERROR;

  *out_picked = NULL;
  *out_count = 0;
  *out_total_data_pages = 0;

  error_code = file_get_all_data_sectors (thread_p, &hfid->vfid, &collector);
  if (error_code != NO_ERROR)
    {
      goto cleanup;
    }

  // exclude heap header page from total: pick loop skips it, else weight = total/picked is off by one
  *out_total_data_pages = collector.npages > 0 ? collector.npages - 1 : 0;

  {
    ftab_set bitmap_set;
    bitmap_set.convert (&collector);

    // mean Poisson gap = weight: base 3 (~33%), all pages below MIN, capped near MAX sampling pages
    int total = *out_total_data_pages;
    int base_weight = 3;
    int min_weight = (total + MIN_HEAP_SAMPLING_PAGES - 1) / MIN_HEAP_SAMPLING_PAGES;
    int max_weight = total / MAX_HEAP_SAMPLING_PAGES;
    int weight = std::max (std::min (base_weight, min_weight), std::max (max_weight, 1));

    // max picks ~ MAX*(base+1)/base + MAX/base variance headroom; tracks MAX so no realloc
    int capacity = MAX_HEAP_SAMPLING_PAGES * (base_weight + 2) / base_weight;
    picked = (VPID *) db_private_alloc (thread_p, ((size_t) capacity) * sizeof (VPID));
    if (picked == NULL)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, ((size_t) capacity) * sizeof (VPID));
	error_code = ER_OUT_OF_VIRTUAL_MEMORY;
	goto cleanup;
      }

    // fresh fixed-seed RNG per call: reproducible sample; thread_local would leak state to next query
    std::mt19937 rng (123456789u);
    std::poisson_distribution<int> gap_dist (weight - 1);

    int current_pos = 0;
    int next_pick_pos = 0;

    while (picked_count < capacity)
      {
	FILE_PARTIAL_SECTOR ftab = bitmap_set.get_next ();
	if (VSID_IS_NULL (&ftab.vsid))
	  {
	    break;
	  }

	for (int offset = 0; offset < DISK_SECTOR_NPAGES && picked_count < capacity; offset++)
	  {
	    if (!bit64_is_set (ftab.page_bitmap, offset))
	      {
		continue;
	      }

	    VPID candidate;
	    candidate.volid = ftab.vsid.volid;
	    candidate.pageid = SECTOR_FIRST_PAGEID (ftab.vsid.sectid) + offset;

	    // skip heap header page; do NOT advance next_pick_pos so the next valid bit fills the slot
	    if (candidate.volid == hfid->vfid.volid && candidate.pageid == hfid->hpgid)
	      {
		continue;
	      }

	    if (current_pos == next_pick_pos)
	      {
		picked[picked_count++] = candidate;
		// shifted Poisson gap (mean = weight); weight == 1 -> sample every page
		next_pick_pos += (weight > 1) ? (gap_dist (rng) + 1) : 1;
	      }
	    current_pos++;
	  }
      }
  }

  *out_picked = picked;
  *out_count = picked_count;
  picked = NULL;

cleanup:
  if (collector.partsect_ftab != NULL)
    {
      db_private_free_and_init (thread_p, collector.partsect_ftab);
    }
  if (picked != NULL)
    {
      db_private_free_and_init (thread_p, picked);
    }
  return error_code;
}
