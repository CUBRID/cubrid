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
#include <cstdint>
#include <random>
#include <vector>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

int
collect_strided_vpids_multi (THREAD_ENTRY *thread_p, const HFID *hfids, int n_hfids,
			     VPID **out_picked, int *out_count, int **out_part_offsets, int *out_weight)
{
  VPID *picked = NULL;
  int *part_offsets = NULL;
  int picked_count = 0;
  std::uint64_t total = 0;
  int error_code = NO_ERROR;

  *out_picked = NULL;
  *out_count = 0;
  *out_part_offsets = NULL;
  *out_weight = 1;

  if (n_hfids <= 0)
    {
      return NO_ERROR;
    }

  {
    ftab_set merged;
    // bound_sect[i] = cumulative merged-sector count after partition i
    // heap header pages (hfid->hpgid) to skip in pick: picking sets slotid=-1 -> reads slot 0 (HEAP_HDR_STATS)
    std::vector<VPID> header_pages;
    std::vector<size_t> bound_sect;

    header_pages.reserve ((size_t) n_hfids);
    bound_sect.reserve ((size_t) n_hfids + 1);
    bound_sect.push_back (0);

    for (int i = 0; i < n_hfids; i++)
      {
	FILE_FTAB_COLLECTOR collector = FILE_FTAB_COLLECTOR_INITIALIZER;

	error_code = file_get_all_data_sectors (thread_p, &hfids[i].vfid, &collector);
	if (error_code != NO_ERROR)
	  {
	    if (collector.partsect_ftab != NULL)
	      {
		db_private_free_and_init (thread_p, collector.partsect_ftab);
	      }
	    goto cleanup;
	  }

	// accurate page count from file header (n_page_user); collector.npages is sector-derived
	int part_pages = 0;
	error_code = file_get_num_user_pages (thread_p, &hfids[i].vfid, &part_pages);
	if (error_code != NO_ERROR)
	  {
	    db_private_free_and_init (thread_p, collector.partsect_ftab);
	    goto cleanup;
	  }
	total += (std::uint64_t) part_pages;

	{
	  // convert clobbers; temp per partition then append
	  ftab_set tmp;
	  tmp.convert (&collector);
	  merged.append (tmp);
	}

	bound_sect.push_back (merged.size ());

	VPID header;
	header.volid = hfids[i].vfid.volid;
	header.pageid = hfids[i].hpgid;
	header_pages.push_back (header);
	db_private_free_and_init (thread_p, collector.partsect_ftab);
      }

    // bucketed weight from accurate total user pages: ~33% (gap 3), full scan below MIN, capped near MAX
    int base_weight = 3;
    int min_weight = (int) ((total + MIN_HEAP_SAMPLING_PAGES - 1) / MIN_HEAP_SAMPLING_PAGES);
    int max_weight = (int) (total / MAX_HEAP_SAMPLING_PAGES);
    int weight = std::max (std::min (base_weight, min_weight), std::max (max_weight, 1));
    *out_weight = weight;

    // max picks ~ MAX*(base+1)/base + MAX/base variance headroom; tracks MAX so no realloc
    int capacity = MAX_HEAP_SAMPLING_PAGES * (base_weight + 2) / base_weight;
    picked = (VPID *) db_private_alloc (thread_p, ((size_t) capacity) * sizeof (VPID));
    if (picked == NULL)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, ((size_t) capacity) * sizeof (VPID));
	error_code = ER_OUT_OF_VIRTUAL_MEMORY;
	goto cleanup;
      }

    part_offsets = (int *) db_private_alloc (thread_p, ((size_t) n_hfids + 1) * sizeof (int));
    if (part_offsets == NULL)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, ((size_t) n_hfids + 1) * sizeof (int));
	error_code = ER_OUT_OF_VIRTUAL_MEMORY;
	goto cleanup;
      }
    part_offsets[0] = 0;

    // fresh fixed-seed RNG per call: reproducible sample; thread_local would leak state to next query
    std::mt19937 rng (123456789u);
    std::poisson_distribution<int> gap_dist (weight - 1);

    int current_pos = 0;
    int next_pick_pos = 0;
    size_t sect_index = 0;	/* merged-sector index */
    int emit_part = 0;		/* next partition boundary to stamp */

    while (picked_count < capacity)
      {
	FILE_PARTIAL_SECTOR ftab = merged.get_next ();
	if (VSID_IS_NULL (&ftab.vsid))
	  {
	    break;
	  }

	// stamp boundaries crossed before this sector (handles empty parts)
	while (emit_part < n_hfids && sect_index >= bound_sect[emit_part + 1])
	  {
	    part_offsets[emit_part + 1] = picked_count;
	    emit_part++;
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

	    // skip heap header page: picking it sets slotid=-1 -> reads slot 0 (HEAP_HDR_STATS) as a record
	    bool is_header = false;
	    for (size_t h = 0; h < header_pages.size (); h++)
	      {
		if (candidate.volid == header_pages[h].volid && candidate.pageid == header_pages[h].pageid)
		  {
		    is_header = true;
		    break;
		  }
	      }
	    if (is_header)
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

	sect_index++;
      }

    // stamp trailing boundaries (empty parts, capacity stop)
    while (emit_part < n_hfids)
      {
	part_offsets[emit_part + 1] = picked_count;
	emit_part++;
      }

    assert (part_offsets[0] == 0);
    assert (part_offsets[n_hfids] == picked_count);
    assert (picked_count <= capacity);
  }

  *out_picked = picked;
  *out_count = picked_count;
  *out_part_offsets = part_offsets;
  picked = NULL;
  part_offsets = NULL;

cleanup:
  if (picked != NULL)
    {
      db_private_free_and_init (thread_p, picked);
    }
  if (part_offsets != NULL)
    {
      db_private_free_and_init (thread_p, part_offsets);
    }
  if (error_code != NO_ERROR)
    {
      *out_picked = NULL;
      *out_count = 0;
      *out_part_offsets = NULL;
      *out_weight = 1;
    }
  return error_code;
}
