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
 *   top  ┌───────────────────┐
 *        │ slots             │  the newest record's slot at the lowest address
 *        ├───────────────────┤
 *        │ second slot array │  merge target
 *        ├───────────────────┤
 *        │ free              │  handed out by reserve ()
 *        ├───────────────────┤
 *        │ records           │  the oldest at the lowest address
 *  base  └───────────────────┘
 */

#include "config.h"

#include "btree_sort.h"

#include "error_manager.h"
#include "memory_alloc.h"
#include "porting.h"

#include <assert.h>
#include <stddef.h>
#include <math.h>
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

typedef struct btsort_stack BTSORT_STACK;
struct btsort_stack
{
  int top;
  BTSORT_SRUN *srun;
};

static void btsort_run_flip (BTSORT_RUN_SLOT *start, BTSORT_RUN_SLOT *stop);
static void btsort_run_find (const char *base, BTSORT_RUN_SLOT *source, long *top, BTSORT_STACK *st_p, long limit,
			     BTSORT_CMP_FUNC *compare, void *comp_arg);
static void btsort_run_merge (const char *base, BTSORT_RUN_SLOT *low, BTSORT_RUN_SLOT *high, BTSORT_STACK *st_p,
			      BTSORT_CMP_FUNC *compare, void *comp_arg);
static BTSORT_RUN_SLOT *btsort_run_sort (const char *rec_base, BTSORT_RUN_SLOT *slots, long limit,
    BTSORT_RUN_SLOT *other_slots, BTSORT_CMP_FUNC *compare, void *comp_arg);
/* per-record cost above the records: one slot in each array */
#define BTSORT_MERGE_REC_COST  (2 * (int) sizeof (BTSORT_RUN_SLOT))

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
 *   return: slots or other_slots, whichever holds the result, or NULL when the run stack could not be allocated
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
  BTSORT_RUN_SLOT *src, *dest, *result;
  BTSORT_STACK sr_stack, *st_p;
  long src_top = 0;
  int cnt;

  if (limit <= 1)
    {
      return slots;
    }

  /* init */
  src = slots;
  dest = other_slots;
  result = NULL;

  st_p = &sr_stack;
  st_p->top = -1;

  cnt = (int) (log10 (ceil ((double) limit / 2.0)) / log10 (2.0)) + 2;

  st_p->srun = (BTSORT_SRUN *) db_private_alloc (NULL, cnt * sizeof (BTSORT_SRUN));
  if (st_p->srun == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, cnt * sizeof (BTSORT_SRUN));
      return NULL;
    }

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

  result = (st_p->srun[0].low_high == 'L') ? other_slots : slots;

  db_private_free_and_init (NULL, st_p->srun);

  return result;
}

/*
 * btsort_merge_sorter_init () - Starts the first run
 *   return: void
 *   run(in/out):
 *   key_domain(in): unused, as records are compared by cmp_fn
 */
static void
btsort_merge_sorter_init (BTSORT_RUN *run, const struct tp_domain *key_domain)
{
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
  run->numrecs = 0;
  run->cursor = 0;
  run->item_ptr = run->base;
  run->slot_ptr = run->top - sizeof (BTSORT_RUN_SLOT);
  run->slot_low = run->slot_ptr - sizeof (BTSORT_RUN_SLOT);
}

/*
 * btsort_merge_sorter_reserve () - Where to write the next record
 *   return: the address, NULL when the run is full
 *   run(in):
 *   maxlen(in):
 *   room(out): writable bytes, at most maxlen
 *
 * Note: Keeps the slots of two records in reserve.
 */
static char *
btsort_merge_sorter_reserve (BTSORT_RUN *run, int maxlen, int *room)
{
  ptrdiff_t free_bytes = (run->slot_low - run->item_ptr) - 2 * (ptrdiff_t) BTSORT_MERGE_REC_COST;

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
  BTSORT_RUN_SLOT *slot = (BTSORT_RUN_SLOT *) run->slot_ptr;

  assert (run->item_ptr + length <= run->slot_low);

  *slot = CAST_BUFLEN (run->item_ptr - run->base);
  run->numrecs++;

  run->slot_ptr -= sizeof (BTSORT_RUN_SLOT);
  run->slot_low -= BTSORT_MERGE_REC_COST;
  run->item_ptr += DB_ALIGN (length, INT_ALIGNMENT);
}

/*
 * btsort_merge_sorter_sort () - Orders the run
 *   return: NO_ERROR or ER_FAILED
 *   thread_p(in):
 *   run(in/out):
 */
static int
btsort_merge_sorter_sort (THREAD_ENTRY *thread_p, BTSORT_RUN *run)
{
  BTSORT_RUN_SLOT *slots = (BTSORT_RUN_SLOT *) (run->slot_ptr + sizeof (BTSORT_RUN_SLOT));	/* the newest slot */
  BTSORT_RUN_SLOT *other_slots = (BTSORT_RUN_SLOT *) run->slot_low;
  BTSORT_RUN_SLOT *result;

  result = btsort_run_sort (run->base, slots, run->numrecs, other_slots, run->cmp_fn, run->cmp_arg);
  if (result == NULL)
    {
      return ER_FAILED;
    }

  /* for next (), kept across reset () */
  run->sorted = (const char *) result;
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

  return run->base + slots[run->cursor++];
}
