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
 * heap_file_internal.hpp - Heap file internals shared by heap modules
 */

#ifndef _HEAP_FILE_INTERNAL_HPP_
#define _HEAP_FILE_INTERNAL_HPP_

#include "heap_file.h"

/*
 * Heap file header
 */

#define HEAP_NUM_BEST_SPACESTATS   10

typedef struct heap_hdr_stats HEAP_HDR_STATS;
struct heap_hdr_stats
{
  /* the first must be class_oid */
  OID class_oid;
  VFID ovf_vfid;		/* Overflow file identifier (if any) */
  VPID next_vpid;		/* Next page (i.e., the 2nd page of heap file) */
  VFID oos_vfid;		/* OOS file identifier (if any) */
  int unfill_space;		/* Stop inserting when page has run below this. leave it for updates */
  struct
  {
    int num_pages;		/* Estimation of number of heap pages. Consult file manager if accurate number is
				 * needed */
    int num_recs;		/* Estimation of number of objects in heap */
    float recs_sumlen;		/* Estimation total length of records */
    int num_other_high_best;	/* Total of other believed known best pages, which are not included in the best array
				 * and we believe they have at least HEAP_DROP_FREE_SPACE */
    int num_high_best;		/* Number of pages in the best array that we believe have at least
				 * HEAP_DROP_FREE_SPACE. When this number goes to zero and there is at least other
				 * HEAP_NUM_BEST_SPACESTATS best pages, we look for them. */
    int num_substitutions;	/* Number of page substitutions. This will be used to insert a new second best page
				 * into second best hints. */
    int num_second_best;	/* Number of second best hints. The hints are in "second_best" array. They are used
				 * when finding new best pages. See the function "heap_stats_sync_bestspace". */
    int head_second_best;	/* Index of head of second best hints. */
    int tail_second_best;	/* Index of tail of second best hints. A new second best hint will be stored on this
				 * index. */
    int head;			/* Head of best circular array */
    VPID last_vpid;		/* todo: move out of estimates */
    VPID full_search_vpid;
    VPID second_best[HEAP_NUM_BEST_SPACESTATS];
    HEAP_BESTSPACE best[HEAP_NUM_BEST_SPACESTATS];
  } estimates;			/* Probably, the set of pages with more free space on the heap. Changes to any values
				 * of this array (either page or the free space for the page) are not logged since
				 * these values are only used for hints. These values may not be accurate at any given
				 * time and the entries may contain duplicated pages. */

  int reserve0_for_future;	/* Nothing reserved for future */
  int reserve1_for_future;	/* Nothing reserved for future */
  int reserve2_for_future;	/* Nothing reserved for future */
};

typedef struct heap_show_scan_ctx HEAP_SHOW_SCAN_CTX;
struct heap_show_scan_ctx
{
  HFID *hfids;			/* Array of class HFID */
  int hfids_count;		/* Count of above hfids array */
};

#endif /* _HEAP_FILE_INTERNAL_HPP_ */
