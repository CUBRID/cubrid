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
 * btree_sort_merge.cpp - Natural merge sorter of the in-memory run
 *
 * One 4-byte slot per record. The ascending/descending stretches of the input are merged between two
 * slot arrays. Not to be confused with btsort_exphase_merge (), which merges the temp file runs.
 *
 * A run wider than a few L2 caches is split first (sample sort), unless its sample arrives nearly in order or in long
 * stretches: splitters picked from the sample put each record in a bucket in one pass over the run, and the buckets
 * are merged one by one, each in L2.
 *
 *   top  ┌───────────────────┐
 *        │ context           │  with the splitters and the buckets of the sample sort
 *        ├───────────────────┤
 *        │ slots             │  the newest record's slot at the lowest address
 *        ├───────────────────┤
 *        │ second slot array │  merge target
 *        ├───────────────────┤
 *        │ bucket ids        │  2 bytes a record, when the buffer is wide enough to split a run
 *        ├───────────────────┤
 *        │ free              │  handed out by reserve ()
 *        ├───────────────────┤
 *        │ records           │  the oldest at the lowest address
 *  base  └───────────────────┘
 */

#include "config.h"

#include "btree_sort.h"

#include "memory_alloc.h"
#include "porting.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#define BTSORT_REC_AT(base, slot)  ((const char *) (base) + *(slot))

typedef struct btsort_srun BTSORT_SRUN;
struct btsort_srun
{
  char low_high;		/* location info LOW('L') : otherbase HIGH('H') : base */
  unsigned short tree_depth;	/* depth of this node : leaf is 1 */
  long start;
  long stop;
};

/* the merge stack holds a run a depth and the two runs found last, and a run of depth d holds 2^d records at least */
#define BTSORT_MAX_SRUNS  32

/* a merge searches the top it moves at once only between stretches this long */
#define BTSORT_RUN_SEARCH_MIN  32

typedef struct btsort_stack BTSORT_STACK;
struct btsort_stack
{
  int top;
  BTSORT_SRUN srun[BTSORT_MAX_SRUNS];
};

static void btsort_run_flip (BTSORT_RUN_SLOT *start, BTSORT_RUN_SLOT *stop);
static void btsort_run_find (const char *base, BTSORT_RUN_SLOT *source, long *top, BTSORT_STACK *st_p, long limit,
			     BTSORT_CMP_FUNC *compare, void *comp_arg);
static BTSORT_RUN_SLOT *btsort_run_search_above (const char *base, BTSORT_RUN_SLOT *start, BTSORT_RUN_SLOT *stop,
    const BTSORT_RUN_SLOT *target, BTSORT_CMP_FUNC *compare, void *comp_arg);
static void btsort_run_merge (const char *base, BTSORT_RUN_SLOT *low, BTSORT_RUN_SLOT *high, BTSORT_STACK *st_p,
			      BTSORT_CMP_FUNC *compare, void *comp_arg);
static BTSORT_RUN_SLOT *btsort_run_sort (const char *rec_base, BTSORT_RUN_SLOT *slots, long limit,
    BTSORT_RUN_SLOT *other_slots, BTSORT_CMP_FUNC *compare, void *comp_arg);

/* a bucket of the sample sort spans about this many bytes of records and slots */
#define BTSORT_SAMPLE_BUCKET_BYTES  (256 * 1024)
/* a run splits only into this many buckets or more */
#define BTSORT_SAMPLE_MIN_BUCKETS   16
/* the sample holds this many records a bucket */
#define BTSORT_SAMPLE_OVERSAMPLING  16
/* the fewest records a bucket holds on average */
#define BTSORT_SAMPLE_MIN_BUCKET_RECORDS  256
/* the fewest buckets a stretch spans on average in a run that is merged whole for its long stretches */
#define BTSORT_SAMPLE_MIN_STRETCH_BUCKETS  4

typedef UINT16 BTSORT_BUCKET_ID;

/* next () fetches the record this many slots ahead */
#define BTSORT_NEXT_PREFETCH_AHEAD  16

namespace
{
  struct merge_sorter_ctx
  {
    int rec_overhead;		/* bytes a record claims above the records: a slot in each array, and a bucket id */
    int max_buckets;		/* 0 when the buffer is too small to split a run */
    BTSORT_RUN_SLOT *splitters;	/* max_buckets - 1: the records that bound the buckets */
    int *bucket_offsets;	/* max_buckets + 1: where each bucket starts, and the scatter leaves where it ends */
    char *slots_top;		/* the first byte above the slots */
  };
}

static int btsort_sample_bucket_count (const merge_sorter_ctx *ctx, const BTSORT_RUN *run);
static bool btsort_sample_nearly_in_order (const BTSORT_RUN *run, const BTSORT_RUN_SLOT *sample, int sample_count);
static int btsort_sample_pick_splitters (merge_sorter_ctx *ctx, const BTSORT_RUN *run, const BTSORT_RUN_SLOT *slots,
    BTSORT_RUN_SLOT *other_slots, int bucket_count);
STATIC_INLINE int btsort_sample_bucket_of (const char *base, const BTSORT_RUN_SLOT *splitters, int splitter_count,
    BTSORT_RUN_SLOT slot, BTSORT_CMP_FUNC *compare, void *comp_arg) __attribute__ ((ALWAYS_INLINE));
static void btsort_sample_fill_buckets (merge_sorter_ctx *ctx, const BTSORT_RUN *run, const BTSORT_RUN_SLOT *slots,
					BTSORT_RUN_SLOT *other_slots, int bucket_count);
static bool btsort_sample_sort (merge_sorter_ctx *ctx, const BTSORT_RUN *run, BTSORT_RUN_SLOT *slots,
				BTSORT_RUN_SLOT *other_slots, int bucket_count);
#if !defined (NDEBUG)
static void btsort_merge_validate (const BTSORT_RUN *run, const BTSORT_RUN_SLOT *slots);
#endif /* !NDEBUG */

static void btsort_merge_sorter_init (BTSORT_RUN *run, const struct tp_domain *key_domain);
static void btsort_merge_sorter_reset (BTSORT_RUN *run);
static char *btsort_merge_sorter_reserve (BTSORT_RUN *run, int maxlen, int *room);
static void btsort_merge_sorter_accept (BTSORT_RUN *run, int length);
static int btsort_merge_sorter_sort (THREAD_ENTRY *thread_p, BTSORT_RUN *run);
static const char *btsort_merge_sorter_next (BTSORT_RUN *run);

const BTSORT_RUN_SORTER btsort_merge_sorter =
{
  "merge",
  btsort_merge_sorter_init,
  btsort_merge_sorter_reset,
  btsort_merge_sorter_reserve,
  btsort_merge_sorter_accept,
  btsort_merge_sorter_sort,
  btsort_merge_sorter_next
};

/*
 * btsort_run_flip () - Flip a run in place
 *   return: void
 *   start(in):
 *   stop(in):
 *
 * Note: Odd runs will have middle pointer undisturbed.
 */
static void
btsort_run_flip (BTSORT_RUN_SLOT *start, BTSORT_RUN_SLOT *stop)
{
  BTSORT_RUN_SLOT temp;

  while (start < stop)
    {
      temp = *start;
      *start = *stop;
      *stop = temp;
      start++;
      stop--;
    }

  return;
}

/*
 * btsort_run_find () - Finds the longest ascending or descending run it can
 *   return:
 *   source(in):
 *   top(in):
 *   st_p(in):
 *   limit(in):
 *   compare(in):
 *   comp_arg(in):
 *
 * Note: Flip descending run, and assign RUN start and stop
 */
static void
btsort_run_find (const char *base, BTSORT_RUN_SLOT *source, long *top, BTSORT_STACK *st_p, long limit,
		 BTSORT_CMP_FUNC *compare, void *comp_arg)
{
  BTSORT_RUN_SLOT *start;
  BTSORT_RUN_SLOT *stop;
  BTSORT_RUN_SLOT *next_stop;
  BTSORT_RUN_SLOT *limit_p;
  BTSORT_SRUN *srun_p;
  int cmp;
  bool increasing_order;

  /* init new BTSORT_SRUN */
  st_p->top++;
  assert (st_p->top < BTSORT_MAX_SRUNS);
  srun_p = &st_p->srun[st_p->top];
  srun_p->tree_depth = 1;
  srun_p->low_high = 'H';
  srun_p->start = *top;

  if (*top >= (limit - 1))
    {
      /* degenerate run length 1. Must go ahead and compare with length 2, because we may need to flip them */
      srun_p->stop = limit - 1;
      *top = limit;

      return;
    }

  start = &source[*top];
  stop = start + 1;
  next_stop = stop + 1;
  limit_p = &source[limit];

  /* have a non-trivial run of length 2 or more */
  cmp = (*compare) (BTSORT_REC_AT (base, start), BTSORT_REC_AT (base, stop), comp_arg);
  if (cmp > 0)
    {
      increasing_order = false;	/* mark as non-increasing order run */

      while (next_stop < limit_p
	     && ((cmp = (*compare) (BTSORT_REC_AT (base, stop), BTSORT_REC_AT (base, next_stop), comp_arg)) >= 0))
	{
	  assert (cmp != 0);	/* key ties are broken by OID */

	  stop = next_stop;
	  next_stop = next_stop + 1;
	}
    }
  else
    {
      increasing_order = true;	/* mark as increasing order run */

      assert (cmp != 0);	/* key ties are broken by OID */

      /* build increasing order run */
      while (next_stop < limit_p
	     && ((cmp = (*compare) (BTSORT_REC_AT (base, stop), BTSORT_REC_AT (base, next_stop), comp_arg)) <= 0))
	{
	  assert (cmp != 0);	/* key ties are broken by OID */

	  stop = next_stop;
	  next_stop = next_stop + 1;
	}
    }

  /* change non-increasing order run to increasing order run */
  if (increasing_order != true)
    {
      btsort_run_flip (start, stop);
    }

  *top += CAST_BUFLEN (stop - start);	/* advance to last visited */
  srun_p->stop = *top;

  (*top)++;			/* advance to next unvisited element */

  return;
}

/*
 * btsort_run_search_above () - Finds the first record of an ordered stretch above a record, searching from the top
 *   return: the slot of the first record above target
 *   base(in):
 *   start(in): the first slot of the stretch
 *   stop(in): the last slot, whose record is above target
 *   target(in):
 *   compare(in):
 *   comp_arg(in):
 *
 * Note: The step from the top doubles, so that k records above target take about 2 log2 k compares.
 */
static BTSORT_RUN_SLOT *
btsort_run_search_above (const char *base, BTSORT_RUN_SLOT *start, BTSORT_RUN_SLOT *stop,
			 const BTSORT_RUN_SLOT *target, BTSORT_CMP_FUNC *compare, void *comp_arg)
{
  BTSORT_RUN_SLOT *above = stop;
  BTSORT_RUN_SLOT *low;
  long step = 1;

  while (stop - start >= step
	 && (*compare) (BTSORT_REC_AT (base, stop - step), BTSORT_REC_AT (base, target), comp_arg) > 0)
    {
      above = stop - step;
      step *= 2;
    }

  /* the first record above target lies from low to above */
  low = (stop - start >= step) ? stop - step + 1 : start;
  while (low < above)
    {
      BTSORT_RUN_SLOT *middle = low + (above - low) / 2;

      if ((*compare) (BTSORT_REC_AT (base, middle), BTSORT_REC_AT (base, target), comp_arg) > 0)
	{
	  above = middle;
	}
      else
	{
	  low = middle + 1;
	}
    }

  return above;
}

/*
 * btsort_run_merge () - Merges two runs from source to dest, updateing dest_top
 *   return:
 *   low(in):
 *   high(in):
 *   st_p(in):
 *   compare(in):
 *   comp_arg(in):
 */
static void
btsort_run_merge (const char *base, BTSORT_RUN_SLOT *low, BTSORT_RUN_SLOT *high, BTSORT_STACK *st_p,
		  BTSORT_CMP_FUNC *compare, void *comp_arg)
{
  char dest_low_high;
  BTSORT_RUN_SLOT *left_start, *right_start;
  BTSORT_RUN_SLOT *left_stop, *right_stop;
  BTSORT_RUN_SLOT *dest_ptr;
  BTSORT_SRUN *left_srun_p, *right_srun_p;
  int cmp;

  do
    {
      /* STEP 1: initialize */
      left_srun_p = &st_p->srun[st_p->top - 1];
      right_srun_p = &st_p->srun[st_p->top];

      left_srun_p->tree_depth++;

      if (left_srun_p->low_high == 'L')
	{
	  left_start = &low[left_srun_p->start];
	  left_stop = &low[left_srun_p->stop];
	}
      else
	{
	  left_start = &high[left_srun_p->start];
	  left_stop = &high[left_srun_p->stop];
	}

      if (right_srun_p->low_high == 'L')
	{
	  right_start = &low[right_srun_p->start];
	  right_stop = &low[right_srun_p->stop];
	}
      else
	{
	  right_start = &high[right_srun_p->start];
	  right_stop = &high[right_srun_p->stop];
	}

      /* STEP 2: check CON conditions srun follows ascending order. if (left_max < right_min) do FORWARD-CON. we use
       * '<' instead of '<=' */
      cmp = (*compare) (BTSORT_REC_AT (base, left_stop), BTSORT_REC_AT (base, right_start), comp_arg);
      if (cmp < 0)
	{
	  /* con == TRUE */
	  dest_low_high = right_srun_p->low_high;

	  if (left_srun_p->low_high == right_srun_p->low_high && left_srun_p->stop + 1 == right_srun_p->start)
	    {
	      ;
	    }
	  else
	    {
	      /* move LEFT to RIGHT's current PART */
	      if (right_srun_p->low_high == 'L')
		{
		  dest_ptr = &low[right_srun_p->start - 1];
		}
	      else
		{
		  dest_ptr = &high[right_srun_p->start - 1];
		}

	      while (left_stop >= left_start)
		{
		  /* copy LEFT */
		  *dest_ptr-- = *left_stop--;
		}
	    }
	}
      else
	{
	  /* con == FALSE do the actual merge; right-shift merge slots */
	  if (right_srun_p->low_high == 'L')
	    {
	      dest_low_high = 'H';
	      dest_ptr = &high[right_srun_p->stop];
	    }
	  else
	    {
	      dest_low_high = 'L';
	      dest_ptr = &low[right_srun_p->stop];
	    }

	  /* the top of the stretch with the larger last record, as far as it rises above the other stretch, moves at
	   * once: pages that arrive in order, each in reverse, merge at a few compares a page */
	  if (left_stop - left_start >= BTSORT_RUN_SEARCH_MIN - 1
	      && right_stop - right_start >= BTSORT_RUN_SEARCH_MIN - 1)
	    {
	      BTSORT_RUN_SLOT *top;

	      if ((*compare) (BTSORT_REC_AT (base, left_stop), BTSORT_REC_AT (base, right_stop), comp_arg) > 0)
		{
		  top = btsort_run_search_above (base, left_start, left_stop, right_stop, compare, comp_arg);
		  while (left_stop >= top)
		    {
		      *dest_ptr-- = *left_stop--;
		    }
		}
	      else
		{
		  top = btsort_run_search_above (base, right_start, right_stop, left_stop, compare, comp_arg);
		  while (right_stop >= top)
		    {
		      *dest_ptr-- = *right_stop--;
		    }
		}
	    }

	  while (left_stop >= left_start && right_stop >= right_start)
	    {
	      cmp = (*compare) (BTSORT_REC_AT (base, left_stop), BTSORT_REC_AT (base, right_stop), comp_arg);
	      assert (cmp != 0);	/* key ties are broken by OID */
	      if (cmp > 0)
		{
		  *dest_ptr-- = *left_stop--;
		}
	      else
		{
		  *dest_ptr-- = *right_stop--;
		}
	    }

	  while (left_stop >= left_start)
	    {
	      /* copy the rest of LEFT */
	      *dest_ptr-- = *left_stop--;
	    }
	  while (right_stop >= right_start)
	    {
	      /* copy the rest of RIGHT */
	      *dest_ptr-- = *right_stop--;
	    }
	}

      /* STEP 3: reconfig BTSORT_STACK */
      st_p->top--;
      left_srun_p->low_high = dest_low_high;
      left_srun_p->start = right_srun_p->start - (left_srun_p->stop - left_srun_p->start + 1);
      left_srun_p->stop = right_srun_p->stop;

    }
  while ((st_p->top >= 1)	/* may need to merge */
	 && (st_p->srun[st_p->top - 1].tree_depth == st_p->srun[st_p->top].tree_depth));

  return;
}

/*
 * btsort_run_sort () - An implementation of a run-sort algorithm
 *   return: slots or other_slots, whichever holds the result
 *   rec_base(in): sort buffer start
 *   slots(in): pointer to the element at the base of the table
 *   limit(in): number of slots
 *   other_slots(in): pointer to alternate area suffecient to store base-limit
 *   compare(in): record comparison function
 *   comp_arg(in):
 *
 * Note: This sorts files by successive merging of runs.
 *
 *       This has the advantage of being liner on sorted or reversed data,
 *       and being order N log base k N, where k is the average length of a
 *       run. Note that k must be at least 2, so the worst case is N log2 N.
 *
 *       Overall, since long runs are common, this beats the pants off
 *       quick-sort.
 *
 *       This could be sped up a bit by looking for N runs, and sorting these
 *       into lists of concatennatable runs.
 */
static BTSORT_RUN_SLOT *
btsort_run_sort (const char *rec_base, BTSORT_RUN_SLOT *slots, long limit, BTSORT_RUN_SLOT *other_slots,
		 BTSORT_CMP_FUNC *compare, void *comp_arg)
{
  BTSORT_RUN_SLOT *src, *dest;
  BTSORT_STACK sr_stack, *st_p;
  long src_top = 0;

  if (limit <= 1)
    {
      return slots;
    }

  /* init */
  src = slots;
  dest = other_slots;

  st_p = &sr_stack;
  st_p->top = -1;

  do
    {
      btsort_run_find (rec_base, src, &src_top, st_p, limit, compare, comp_arg);
      if (src_top < limit)
	{
	  btsort_run_find (rec_base, src, &src_top, st_p, limit, compare, comp_arg);
	}

      while ((st_p->top >= 1)	/* may need to merge */
	     && ((src_top >= limit)	/* case 1: final merge stage */
		 || ((src_top < limit)	/* case 2: non-final merge stage */
		     && (st_p->srun[st_p->top - 1].tree_depth == st_p->srun[st_p->top].tree_depth))))
	{
	  btsort_run_merge (rec_base, dest, src, st_p, compare, comp_arg);
	}
    }
  while (src_top < limit);

  /* nothing is dropped, so the last stretch spans every slot */
  assert (st_p->srun[0].start == 0 && st_p->srun[0].stop == limit - 1);

  return (st_p->srun[0].low_high == 'L') ? other_slots : slots;
}

/*
 * btsort_sample_bucket_count () - How many buckets the sample sort splits the run into
 *   return: the bucket count, 0 to merge the run whole
 *   ctx(in): merge sorter context, for max_buckets
 *   run(in): the full run
 */
static int
btsort_sample_bucket_count (const merge_sorter_ctx *ctx, const BTSORT_RUN *run)
{
  /* the bytes the merge walks: the records and a slot in each array */
  size_t run_bytes = (size_t) (run->item_ptr - run->base) + 2 * sizeof (BTSORT_RUN_SLOT) * run->numrecs;
  size_t bucket_count = run_bytes / BTSORT_SAMPLE_BUCKET_BYTES;

  bucket_count = MIN (bucket_count, (size_t) ctx->max_buckets);
  /* with long records a bucket of that many bytes holds few of them, and splitting so finely only adds compares */
  bucket_count = MIN (bucket_count, (size_t) run->numrecs / BTSORT_SAMPLE_MIN_BUCKET_RECORDS);

  return (bucket_count >= BTSORT_SAMPLE_MIN_BUCKETS) ? (int) bucket_count : 0;
}

/*
 * btsort_sample_nearly_in_order () - Whether the sample arrives nearly in order, rising or falling, or in long
 *                                    stretches that do
 *   return: true when the pairs a bucket apart change their way only where long stretches meet, but for one record
 *           far out of place
 *   run(in): the full run
 *   sample(in): the sample, unsorted
 *   sample_count(in): BTSORT_SAMPLE_OVERSAMPLING records a bucket
 *
 * Note: A pair is a sample record and the one a bucket after it.
 *       A row is a series of pairs going the same way, up or down.
 *       A turn is where one row ends and the next begins.
 *       Where two long stretches meet, a row of pairs goes the other way.
 *       That row makes two turns, one on each side.
 *       A row of one pair between two turns is a stray.
 *       A stray is a record far out of place.
 *
 *       In the pairs below, + goes up, - goes down and | is a turn.
 *
 *         + + + + + | - | + + + + + | - - - - - - - - - - - - - - - - | + + + + +
 *                     ^             ^------- a bucket of pairs -------^
 *                  a stray                two long stretches meet
 *
 *       They make 4 turns and 1 stray.
 */
static bool
btsort_sample_nearly_in_order (const BTSORT_RUN *run, const BTSORT_RUN_SLOT *sample, int sample_count)
{
  /* the keys alone, as the records of a key arrive in OID order whichever way the keys arrive */
  BTSORT_CMP_FUNC *compare = run->key_cmp_fn;
  int bucket_count = sample_count / BTSORT_SAMPLE_OVERSAMPLING;
  int turns = 0, strays = 0;
  int length = 0;		/* the pairs in a row going the way of falling */
  bool falling = false;
  int at, c;

  assert (compare != NULL);

  /* compare each sample record with the one a bucket after it, and count the turns, where the pairs change their way.
   * A record out of place by less than a bucket reverses no pair, and the merge of the whole run puts it right within
   * the cache, as the merge of its bucket does */
  for (at = 0; at + BTSORT_SAMPLE_OVERSAMPLING < sample_count; at++)
    {
      c = (*compare) (run->base + sample[at], run->base + sample[at + BTSORT_SAMPLE_OVERSAMPLING], run->cmp_arg);
      if (c == 0)
	{
	  /* a pair of equal keys goes neither way */
	  continue;
	}

      if (length > 0 && (c > 0) != falling)
	{
	  /* a lone pair between two turns is a record far out of place. The rows at the ends of the sample may go on
	   * past it */
	  if (length == 1 && turns > 0)
	    {
	      strays++;
	    }
	  turns++;
	  length = 0;
	}

      falling = (c > 0);
      length++;
    }

  /* allow one record far out of place, which costs the whole merge little, and two turns where long stretches meet,
   * into the pairs they reverse and out of them, which the whole merge joins with fewer compares than the split
   * spends */
  return strays <= 1 && turns <= 2 * (bucket_count / BTSORT_SAMPLE_MIN_STRETCH_BUCKETS);
}

/*
 * btsort_sample_pick_splitters () - Picks the splitters of the buckets from a sample of the run
 *   return: the number of splitters, 0 when the sample arrives nearly in order
 *   ctx(in/out): the splitters
 *   run(in): the full run
 *   slots(in): the slots of the run
 *   other_slots(in): as many slots, for scratch
 *   bucket_count(in): the bucket count from btsort_sample_bucket_count ()
 *
 * Note: Every BTSORT_SAMPLE_OVERSAMPLING-th record of the sorted sample is a splitter.
 */
static int
btsort_sample_pick_splitters (merge_sorter_ctx *ctx, const BTSORT_RUN *run, const BTSORT_RUN_SLOT *slots,
			      BTSORT_RUN_SLOT *other_slots, int bucket_count)
{
  const char *base = run->base;
  BTSORT_CMP_FUNC *compare = run->cmp_fn;
  void *comp_arg = run->cmp_arg;
  int record_count = (int) run->numrecs;
  int splitter_count = bucket_count - 1;
  int sample_count = BTSORT_SAMPLE_OVERSAMPLING * bucket_count;
  int stride = record_count / sample_count;
  BTSORT_RUN_SLOT *sample;
  int at, bucket_id;

  /* the sample and its spare fit the second slot array */
  assert (bucket_count <= ctx->max_buckets && 2 * sample_count <= record_count);

  /* the sample, in the order of the run and then sorted, in the second slot array, which the slots fill later */
  for (at = 0; at < sample_count; at++)
    {
      other_slots[at] = slots[at * stride];
    }

  /* do not split a run that arrives nearly in order or in long stretches, which the merge of the whole run sorts with
   * fewer compares */
  if (btsort_sample_nearly_in_order (run, other_slots, sample_count))
    {
      return 0;
    }

  sample = btsort_run_sort (base, other_slots, sample_count, other_slots + sample_count, compare, comp_arg);

  /* take the splitters evenly apart in the sorted sample, so that the buckets get about as many records each */
  for (bucket_id = 0; bucket_id < splitter_count; bucket_id++)
    {
      ctx->splitters[bucket_id] = sample[BTSORT_SAMPLE_OVERSAMPLING * (bucket_id + 1) - 1];
    }

  return splitter_count;
}

/*
 * btsort_sample_bucket_of () - Finds the bucket of a record
 *   return: the number of splitters below the record
 *   base(in):
 *   splitters(in): in order
 *   splitter_count(in):
 *   slot(in): the slot of the record
 *   compare(in):
 *   comp_arg(in):
 */
STATIC_INLINE int
btsort_sample_bucket_of (const char *base, const BTSORT_RUN_SLOT *splitters, int splitter_count, BTSORT_RUN_SLOT slot,
			 BTSORT_CMP_FUNC *compare, void *comp_arg)
{
  int low = 0, high = splitter_count;

  while (low < high)
    {
      int middle = (low + high) >> 1;

      /* a splitter is a record of the run, and the comparator stops on a record compared with itself */
      if (slot == splitters[middle] || (*compare) (base + slot, base + splitters[middle], comp_arg) < 0)
	{
	  high = middle;
	}
      else
	{
	  low = middle + 1;
	}
    }

  return low;
}

/*
 * btsort_sample_fill_buckets () - Moves the slots of the run into their buckets
 *   return: void
 *   ctx(in/out): the splitters, and where each bucket ends on return
 *   run(in): the full run
 *   slots(in): the slots of the run
 *   other_slots(out): the slots bucket by bucket, above the bucket ids
 *   bucket_count(in): the bucket count from btsort_sample_bucket_count ()
 *
 * Note: The splitters put a record in a bucket in one pass that reads the records in the order they lie, and then the
 *       slots move to their buckets in that order.
 */
static void
btsort_sample_fill_buckets (merge_sorter_ctx *ctx, const BTSORT_RUN *run, const BTSORT_RUN_SLOT *slots,
			    BTSORT_RUN_SLOT *other_slots, int bucket_count)
{
  const char *base = run->base;
  BTSORT_CMP_FUNC *compare = run->cmp_fn;
  void *comp_arg = run->cmp_arg;
  int record_count = (int) run->numrecs;
  BTSORT_BUCKET_ID *bucket_ids = (BTSORT_BUCKET_ID *) other_slots - record_count;
  const BTSORT_RUN_SLOT *splitters = ctx->splitters;
  int splitter_count = bucket_count - 1;
  int at, bucket_id;

  /* the bucket id of each record, and the records of each bucket counted */
  memset (ctx->bucket_offsets, 0, (bucket_count + 1) * sizeof (int));
  for (at = 0; at < record_count; at++)
    {
      bucket_id = btsort_sample_bucket_of (base, splitters, splitter_count, slots[at], compare, comp_arg);

      bucket_ids[at] = (BTSORT_BUCKET_ID) bucket_id;
      ctx->bucket_offsets[bucket_id + 1]++;
    }

  /* the counts into starts */
  for (bucket_id = 0; bucket_id < bucket_count; bucket_id++)
    {
      ctx->bucket_offsets[bucket_id + 1] += ctx->bucket_offsets[bucket_id];
    }

  /* each slot to the next free place of its bucket, in the order they come */
  for (at = 0; at < record_count; at++)
    {
      bucket_id = bucket_ids[at];

      other_slots[ctx->bucket_offsets[bucket_id]] = slots[at];
      ctx->bucket_offsets[bucket_id]++;
    }
}

/*
 * btsort_sample_sort () - Sorts the run in buckets that each sort in L2, unless its sample arrives nearly in order
 *   return: false when the run is left as it came, for the merge of the whole
 *   ctx(in): the splitters and the bucket bounds
 *   run(in): the full run
 *   slots(in/out): the slots of the run, in order on return
 *   other_slots(in): as many slots, for scratch, above the bucket ids
 *   bucket_count(in): the bucket count from btsort_sample_bucket_count ()
 *
 * Note: Each bucket is merged on its own while it stays in L2.
 */
static bool
btsort_sample_sort (merge_sorter_ctx *ctx, const BTSORT_RUN *run, BTSORT_RUN_SLOT *slots,
		    BTSORT_RUN_SLOT *other_slots, int bucket_count)
{
  const char *base = run->base;
  BTSORT_CMP_FUNC *compare = run->cmp_fn;
  void *comp_arg = run->cmp_arg;
  BTSORT_RUN_SLOT *sorted;
  int bucket_id, start, count, at;

  /* pick the splitters that bound the buckets from a sample of the run */
  if (btsort_sample_pick_splitters (ctx, run, slots, other_slots, bucket_count) == 0)
    {
      /* the sample arrives nearly in order, and the run is merged whole */
      return false;
    }

  /* move the slots into their buckets in the second slot array */
  btsort_sample_fill_buckets (ctx, run, slots, other_slots, bucket_count);

  /* the buckets in order, each from the end of the one before, back in the slot array */
  for (bucket_id = 0, start = 0; bucket_id < bucket_count; bucket_id++, start += count)
    {
      count = ctx->bucket_offsets[bucket_id] - start;

      /* the records of a bucket lie all over the buffer: fetch the head of each before the merge walks them */
      for (at = start; at < start + count; at++)
	{
	  prefetch (base + other_slots[at], PREFETCH_READ, PREFETCH_CACHE_L2);
	  prefetch (base + other_slots[at] + 32, PREFETCH_READ, PREFETCH_CACHE_L2);
	}

      sorted = btsort_run_sort (base, other_slots + start, count, slots + start, compare, comp_arg);
      if (sorted != slots + start)
	{
	  memcpy (slots + start, sorted, count * sizeof (BTSORT_RUN_SLOT));
	}
    }

  return true;
}

#if !defined (NDEBUG)
/* the comparator never returns 0, as it breaks key ties by OID */
static void
btsort_merge_validate (const BTSORT_RUN *run, const BTSORT_RUN_SLOT *slots)
{
  long at;

  for (at = 0; at + 1 < run->numrecs; at++)
    {
      assert ((*run->cmp_fn) (run->base + slots[at], run->base + slots[at + 1], run->cmp_arg) < 0);
    }
}
#endif /* !NDEBUG */

/*
 * btsort_merge_sorter_init () - Sets the context up at the top of the buffer and starts the first run
 *   return: void
 *   run(in/out):
 *   key_domain(in): unused, as records are compared by cmp_fn
 */
static void
btsort_merge_sorter_init (BTSORT_RUN *run, const struct tp_domain *key_domain)
{
  merge_sorter_ctx *ctx = (merge_sorter_ctx *) (run->top - DB_ALIGN (sizeof (merge_sorter_ctx), DOUBLE_ALIGNMENT));
  char *ctx_low = (char *) ctx;	/* the lowest byte the context takes */
  int max_buckets;

  assert (PTR_ALIGN (run->top, DOUBLE_ALIGNMENT) == run->top);

  max_buckets = (int) ((size_t) (run->top - run->base) / BTSORT_SAMPLE_BUCKET_BYTES);
  if (max_buckets < BTSORT_SAMPLE_MIN_BUCKETS)
    {
      max_buckets = 0;
    }

  ctx->max_buckets = max_buckets;
  ctx->rec_overhead = 2 * (int) sizeof (BTSORT_RUN_SLOT) + ((max_buckets > 0) ? (int) sizeof (BTSORT_BUCKET_ID) : 0);
  ctx->splitters = NULL;
  ctx->bucket_offsets = NULL;

  if (max_buckets > 0)
    {
      ctx_low -= DB_ALIGN ((max_buckets + 1) * sizeof (int), DOUBLE_ALIGNMENT);
      ctx->bucket_offsets = (int *) ctx_low;
      ctx_low -= DB_ALIGN ((max_buckets - 1) * sizeof (BTSORT_RUN_SLOT), DOUBLE_ALIGNMENT);
      ctx->splitters = (BTSORT_RUN_SLOT *) ctx_low;
    }
  ctx->slots_top = ctx_low;

  run->sorter_ctx = ctx;

  btsort_merge_sorter_reset (run);
}

/*
 * btsort_merge_sorter_reset () - Starts a new run
 *   return: void
 *   run(in/out):
 *
 * Note: The last sorted run stays readable until the next accept ().
 */
static void
btsort_merge_sorter_reset (BTSORT_RUN *run)
{
  merge_sorter_ctx *ctx = (merge_sorter_ctx *) run->sorter_ctx;

  run->numrecs = 0;
  run->cursor = 0;
  run->item_ptr = run->base;
  run->slot_ptr = ctx->slots_top - sizeof (BTSORT_RUN_SLOT);
  run->slot_low = ctx->slots_top;	/* nothing claimed yet */
}

/*
 * btsort_merge_sorter_reserve () - Where to write the next record
 *   return: the address, NULL when the run is full
 *   run(in):
 *   maxlen(in):
 *   room(out): writable bytes, at most maxlen
 *
 * Note: Keeps the share of two records in reserve.
 */
static char *
btsort_merge_sorter_reserve (BTSORT_RUN *run, int maxlen, int *room)
{
  merge_sorter_ctx *ctx = (merge_sorter_ctx *) run->sorter_ctx;
  ptrdiff_t free_bytes = (run->slot_low - run->item_ptr) - 2 * (ptrdiff_t) ctx->rec_overhead;

  if (free_bytes <= 0)
    {
      *room = 0;
      return NULL;
    }

  *room = (free_bytes < (ptrdiff_t) maxlen) ? (int) free_bytes : maxlen;

  return run->item_ptr;
}

/*
 * btsort_merge_sorter_accept () - Adds the record written at reserve ()
 *   return: void
 *   run(in/out):
 *   length(in): record length, header included
 */
static void
btsort_merge_sorter_accept (BTSORT_RUN *run, int length)
{
  merge_sorter_ctx *ctx = (merge_sorter_ctx *) run->sorter_ctx;
  BTSORT_RUN_SLOT *slot = (BTSORT_RUN_SLOT *) run->slot_ptr;

  assert (run->item_ptr + length <= run->slot_low);

  *slot = CAST_BUFLEN (run->item_ptr - run->base);
  run->numrecs++;

  run->slot_ptr -= sizeof (BTSORT_RUN_SLOT);
  run->slot_low -= ctx->rec_overhead;
  run->item_ptr += DB_ALIGN (length, INT_ALIGNMENT);
}

/*
 * btsort_merge_sorter_sort () - Orders the run
 *   return: NO_ERROR
 *   thread_p(in):
 *   run(in/out):
 */
static int
btsort_merge_sorter_sort (THREAD_ENTRY *thread_p, BTSORT_RUN *run)
{
  merge_sorter_ctx *ctx = (merge_sorter_ctx *) run->sorter_ctx;
  BTSORT_RUN_SLOT *slots = (BTSORT_RUN_SLOT *) (run->slot_ptr + sizeof (BTSORT_RUN_SLOT));	/* the newest slot */
  /* right below the slots, and above the bucket ids that accept () claimed */
  BTSORT_RUN_SLOT *other_slots = slots - run->numrecs;
  BTSORT_RUN_SLOT *sorted;
  int bucket_count;

  assert ((char *) other_slots - run->slot_low
	  == (ptrdiff_t) (ctx->rec_overhead - 2 * (int) sizeof (BTSORT_RUN_SLOT)) * run->numrecs);
  /* a slot is a 32-bit offset into a buffer under 2GB */
  assert (run->numrecs <= INT_MAX);

  bucket_count = btsort_sample_bucket_count (ctx, run);
  if (bucket_count > 0 && btsort_sample_sort (ctx, run, slots, other_slots, bucket_count))
    {
      sorted = slots;
      run->split_runs++;
    }
  else
    {
      sorted = btsort_run_sort (run->base, slots, run->numrecs, other_slots, run->cmp_fn, run->cmp_arg);
    }

#if !defined (NDEBUG)
  btsort_merge_validate (run, sorted);
#endif /* !NDEBUG */

  /* for next (), kept across reset () */
  run->sorted = (const char *) sorted;
  run->readrecs = run->numrecs;
  run->cursor = 0;

  return NO_ERROR;
}

/*
 * btsort_merge_sorter_next () - Next record of the sorted run
 *   return: the record, NULL at the end
 *   run(in/out):
 */
static const char *
btsort_merge_sorter_next (BTSORT_RUN *run)
{
  const BTSORT_RUN_SLOT *slots = (const BTSORT_RUN_SLOT *) run->sorted;

  /* read before new records are accepted */
  assert (run->numrecs == 0 || run->numrecs == run->readrecs);

  if (run->cursor >= run->readrecs)
    {
      return NULL;
    }

  if (run->cursor + BTSORT_NEXT_PREFETCH_AHEAD < run->readrecs)
    {
      prefetch (run->base + slots[run->cursor + BTSORT_NEXT_PREFETCH_AHEAD], PREFETCH_READ, PREFETCH_CACHE_L1);
    }

  return run->base + slots[run->cursor++];
}
