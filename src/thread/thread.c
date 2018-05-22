/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

/*
 * thread.c - Thread management module at the server
 */

// *INDENT-OFF*

#include "thread.h"

#include "error_manager.h"
#include "memory_alloc.h"
#include "system_parameter.h"
#include "thread_manager.hpp"
#include "critical_section.h"

#define THREAD_RC_TRACK_VMEM_THRESHOLD_AMOUNT	      32767
#define THREAD_RC_TRACK_PGBUF_THRESHOLD_AMOUNT	      1024
#define THREAD_RC_TRACK_QLIST_THRESHOLD_AMOUNT	      1024
#define THREAD_RC_TRACK_CS_THRESHOLD_AMOUNT	      1024

#define THREAD_METER_DEMOTED_READER_FLAG	      0x40
#define THREAD_METER_MAX_ENTER_COUNT		      0x3F

#define THREAD_METER_IS_DEMOTED_READER(rlock) \
	(rlock & THREAD_METER_DEMOTED_READER_FLAG)
#define THREAD_METER_STRIP_DEMOTED_READER_FLAG(rlock) \
	(rlock & ~THREAD_METER_DEMOTED_READER_FLAG)
#define THREAD_METER_WITH_DEMOTED_READER_FLAG(rlock) \
	(rlock | THREAD_METER_DEMOTED_READER_FLAG)

#define THREAD_RC_TRACK_ASSERT(thread_p, outfp, cond) \
  do \
    { \
      if (!(cond)) \
        { \
          thread_rc_track_dump_all (thread_p, outfp); \
        } \
      assert_release (cond); \
    } \
  while (0)

#define THREAD_RC_TRACK_METER_ASSERT(thread_p, outfp, meter, cond) \
  do \
    { \
      if (!(cond)) \
        { \
          thread_rc_track_meter_dump (thread_p, outfp, meter); \
        } \
      assert_release (cond); \
    } \
  while (0)


static int thread_rc_track_meter_check (THREAD_ENTRY * thread_p, THREAD_RC_METER * meter, THREAD_RC_METER * prev_meter);
static int thread_rc_track_check (THREAD_ENTRY * thread_p, int id);
static THREAD_RC_TRACK *thread_rc_track_alloc (THREAD_ENTRY * thread_p);
static void thread_rc_track_free (THREAD_ENTRY * thread_p, int id);
static INT32 thread_rc_track_amount_helper (THREAD_ENTRY * thread_p, int rc_idx);
static const char *thread_rc_track_rcname (int rc_idx);
static const char *thread_rc_track_mgrname (int mgr_idx);
static void thread_rc_track_meter_dump (THREAD_ENTRY * thread_p, FILE * outfp, THREAD_RC_METER * meter);
static void thread_rc_track_dump (THREAD_ENTRY * thread_p, FILE * outfp, THREAD_RC_TRACK * track, int depth);
static INT32 thread_rc_track_threshold_amount (int rc_idx);
static bool thread_rc_track_is_enabled (THREAD_ENTRY * thread_p);

#if !defined(NDEBUG)
static void thread_rc_track_meter_at (THREAD_RC_METER * meter, const char *caller_file, int caller_line, int amount,
				      void *ptr);
static void thread_rc_track_meter_assert_csect_dependency (THREAD_ENTRY * thread_p, THREAD_RC_METER * meter, int amount,
							   void *ptr);
static void thread_rc_track_meter_assert_csect_usage (THREAD_ENTRY * thread_p, THREAD_RC_METER * meter, int enter_mode,
						      void *ptr);
#endif /* !NDEBUG */
/*
 * thread_rc_track_meter_check () -
 *   return:
 *   thread_p(in):
 */
static int
thread_rc_track_meter_check (THREAD_ENTRY * thread_p, THREAD_RC_METER * meter, THREAD_RC_METER * prev_meter)
{
#if !defined(NDEBUG)
  int i;
#endif

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  assert_release (thread_p != NULL);
  assert_release (meter != NULL);

  /* assert (meter->m_amount >= 0); assert (meter->m_amount <= meter->m_threshold); */
  if (meter->m_amount < 0 || meter->m_amount > meter->m_threshold)
    {
      THREAD_RC_TRACK_METER_ASSERT (thread_p, stderr, meter, false);
      return ER_FAILED;
    }

  if (prev_meter != NULL)
    {
      /* assert (meter->m_amount == prev_meter->m_amount); */
      if (meter->m_amount != prev_meter->m_amount)
	{
	  THREAD_RC_TRACK_ASSERT (thread_p, stderr, false);
	  return ER_FAILED;
	}
    }
  else
    {
      /* assert (meter->m_amount == 0); */
      if (meter->m_amount != 0)
	{
	  THREAD_RC_TRACK_METER_ASSERT (thread_p, stderr, meter, false);
	  return ER_FAILED;
	}
    }

#if !defined(NDEBUG)
  /* check hold_buf */
  if (meter->m_hold_buf_size > 0)
    {
      for (i = 0; i < meter->m_hold_buf_size; i++)
	{
	  if (meter->m_hold_buf[i] != '\0')
	    {
	      THREAD_RC_TRACK_METER_ASSERT (thread_p, stderr, meter, false);
	      return ER_FAILED;
	    }
	}
    }
#endif

  return NO_ERROR;
}

/*
 * thread_rc_track_check () -
 *   return:
 *   thread_p(in):
 */
static int
thread_rc_track_check (THREAD_ENTRY * thread_p, int id)
{
  int i, j;
  THREAD_RC_TRACK *track, *prev_track;
  THREAD_RC_METER *meter, *prev_meter;
  int num_invalid_meter;

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  assert_release (thread_p != NULL);
  assert_release (thread_p->track != NULL);
  assert_release (id == thread_p->track_depth);

  num_invalid_meter = 0;	/* init */

  if (thread_p->track != NULL)
    {
      assert_release (id >= 0);

      track = thread_p->track;
      prev_track = track->prev;

      for (i = 0; i < RC_LAST; i++)
	{
#if 1				/* TODO - */
	  /* skip out qlist check; is checked separately */
	  if (i == RC_QLIST)
	    {
	      continue;
	    }
#endif

	  for (j = 0; j < MGR_LAST; j++)
	    {
	      meter = &(track->meter[i][j]);

	      if (prev_track != NULL)
		{
		  prev_meter = &(prev_track->meter[i][j]);
		}
	      else
		{
		  prev_meter = NULL;
		}

	      if (thread_rc_track_meter_check (thread_p, meter, prev_meter) != NO_ERROR)
		{
		  num_invalid_meter++;
		}
	    }			/* for */
	}			/* for */
    }

  return (num_invalid_meter == 0) ? NO_ERROR : ER_FAILED;
}

/*
 * thread_rc_track_clear_all () -
 *   return:
 *   thread_p(in):
 */
void
thread_rc_track_clear_all (THREAD_ENTRY * thread_p)
{
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  assert_release (thread_p != NULL);

  /* pop/free every track info */
  while (thread_p->track != NULL)
    {
      (void) thread_rc_track_free (thread_p, thread_p->track_depth);
    }

  assert_release (thread_p->track_depth == -1);

  thread_p->track_depth = -1;	/* defence */
}

/*
 * thread_rc_track_initialize () -
 *   return:
 *   thread_p(in):
 */
void
thread_rc_track_initialize (THREAD_ENTRY * thread_p)
{
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  assert_release (thread_p != NULL);

  thread_p->track = NULL;
  thread_p->track_depth = -1;
  thread_p->track_threshold = 0x7F;	/* 127 */
  thread_p->track_free_list = NULL;

  (void) thread_rc_track_clear_all (thread_p);
}

/*
 * thread_rc_track_finalize () -
 *   return:
 *   thread_p(in):
 */
void
thread_rc_track_finalize (THREAD_ENTRY * thread_p)
{
  THREAD_RC_TRACK *track;

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  assert_release (thread_p != NULL);

  (void) thread_rc_track_clear_all (thread_p);

  while (thread_p->track_free_list != NULL)
    {
      track = thread_p->track_free_list;
      thread_p->track_free_list = track->prev;
      track->prev = NULL;	/* cut-off */

      free (track);
    }
}

/*
 * thread_rc_track_rcname () - TODO
 *   return:
 *   rc_idx(in):
 */
static const char *
thread_rc_track_rcname (int rc_idx)
{
  const char *name;

  assert_release (rc_idx >= 0);
  assert_release (rc_idx < RC_LAST);

  switch (rc_idx)
    {
    case RC_VMEM:
      name = "Virtual Memory";
      break;
    case RC_PGBUF:
      name = "Page Buffer";
      break;
    case RC_QLIST:
      name = "List File";
      break;
    case RC_CS:
      name = "Critical Section";
      break;
    default:
      name = "**UNKNOWN_RESOURCE**";
      break;
    }

  return name;
}

/*
 * thread_rc_track_mgrname () - TODO
 *   return:
 *   mgr_idx(in):
 */
static const char *
thread_rc_track_mgrname (int mgr_idx)
{
  const char *name;

  assert_release (mgr_idx >= 0);
  assert_release (mgr_idx < MGR_LAST);

  switch (mgr_idx)
    {
#if 0
    case MGR_BTREE:
      name = "Index Manager";
      break;
    case MGR_QUERY:
      name = "Query Manager";
      break;
    case MGR_SPAGE:
      name = "Slotted-Page Manager";
      break;
#endif
    case MGR_DEF:
      name = "Default Manager";
      break;
    default:
      name = "**UNKNOWN_MANAGER**";
      break;
    }

  return name;
}

/*
 * thread_rc_track_threshold_amount () - Get the maximum amount for different
 *					 trackers.
 *
 * return	 :
 * thread_p (in) :
 * rc_idx (in)	 :
 */
static INT32
thread_rc_track_threshold_amount (int rc_idx)
{
  switch (rc_idx)
    {
    case RC_VMEM:
      return THREAD_RC_TRACK_VMEM_THRESHOLD_AMOUNT;
    case RC_PGBUF:
      return THREAD_RC_TRACK_PGBUF_THRESHOLD_AMOUNT;
    case RC_QLIST:
      return THREAD_RC_TRACK_QLIST_THRESHOLD_AMOUNT;
    case RC_CS:
      return THREAD_RC_TRACK_CS_THRESHOLD_AMOUNT;
    default:
      assert_release (false);
      return -1;
    }
}

/*
 * thread_rc_track_alloc ()
 *   return:
 *   thread_p(in):
 */
static THREAD_RC_TRACK *
thread_rc_track_alloc (THREAD_ENTRY * thread_p)
{
  int i, j;
#if !defined(NDEBUG)
  int max_tracked_res;
  THREAD_TRACKED_RESOURCE *tracked_res_chunk = NULL, *tracked_res_ptr = NULL;
#endif /* !NDEBUG */
  THREAD_RC_TRACK *new_track;

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  assert_release (thread_p != NULL);
  assert_release (thread_p->track_depth < thread_p->track_threshold);

  new_track = NULL;		/* init */

#if !defined (NDEBUG)
  /* Compute the required size for tracked resources */
  max_tracked_res = 0;
  /* Compute the size required for one manager */
  for (i = 0; i < RC_LAST; i++)
    {
      max_tracked_res += thread_rc_track_threshold_amount (i);
    }
  /* Compute the size required for all managers */
  max_tracked_res *= MGR_LAST;

  /* Allocate a chunk of memory for all tracked resources */
  tracked_res_chunk = (THREAD_TRACKED_RESOURCE *) malloc (max_tracked_res * sizeof (THREAD_TRACKED_RESOURCE));
  if (tracked_res_chunk == NULL)
    {
      assert_release (false);
      goto error;
    }
  tracked_res_ptr = tracked_res_chunk;
#endif /* !NDEBUG */

  if (thread_p->track_depth < thread_p->track_threshold)
    {
      if (thread_p->track_free_list != NULL)
	{
	  new_track = thread_p->track_free_list;
	  thread_p->track_free_list = new_track->prev;
	}
      else
	{
	  new_track = (THREAD_RC_TRACK *) malloc (sizeof (THREAD_RC_TRACK));
	  if (new_track == NULL)
	    {
	      assert_release (false);
	      goto error;
	    }
	}
      assert_release (new_track != NULL);

      if (new_track != NULL)
	{
	  /* keep current track info */

	  /* id of thread private memory allocator */
	  new_track->private_heap_id = thread_p->private_heap_id;

	  for (i = 0; i < RC_LAST; i++)
	    {
	      for (j = 0; j < MGR_LAST; j++)
		{
		  if (thread_p->track != NULL)
		    {
		      new_track->meter[i][j].m_amount = thread_p->track->meter[i][j].m_amount;
		    }
		  else
		    {
		      new_track->meter[i][j].m_amount = 0;
		    }
		  new_track->meter[i][j].m_threshold = thread_rc_track_threshold_amount (i);
		  new_track->meter[i][j].m_add_file_name = NULL;
		  new_track->meter[i][j].m_add_line_no = -1;
		  new_track->meter[i][j].m_sub_file_name = NULL;
		  new_track->meter[i][j].m_sub_line_no = -1;
#if !defined(NDEBUG)
		  new_track->meter[i][j].m_hold_buf[0] = '\0';
		  new_track->meter[i][j].m_rwlock_buf[0] = '\0';
		  new_track->meter[i][j].m_hold_buf_size = 0;

		  /* init Critical Section hold_buf */
		  if (i == RC_CS)
		    {
		      memset (new_track->meter[i][j].m_hold_buf, 0, ONE_K);
		      memset (new_track->meter[i][j].m_rwlock_buf, 0, ONE_K);
		    }

		  /* Initialize tracked resources */
		  new_track->meter[i][j].m_tracked_res_capacity = thread_rc_track_threshold_amount (i);
		  new_track->meter[i][j].m_tracked_res_count = 0;
		  new_track->meter[i][j].m_tracked_res = tracked_res_ptr;
		  /* Advance pointer in preallocated chunk of resources */
		  tracked_res_ptr += new_track->meter[i][j].m_tracked_res_capacity;
#endif /* !NDEBUG */
		}
	    }

#if !defined (NDEBUG)
	  assert ((tracked_res_ptr - tracked_res_chunk) == max_tracked_res);
	  new_track->tracked_resources = tracked_res_chunk;
#endif /* !NDEBUG */

	  /* push current track info */
	  new_track->prev = thread_p->track;
	  thread_p->track = new_track;

	  thread_p->track_depth++;
	}
    }

  return new_track;

error:

  if (new_track != NULL)
    {
      free (new_track);
    }

#if !defined (NDEBUG)
  if (tracked_res_chunk != NULL)
    {
      free (tracked_res_chunk);
    }
#endif /* !NDEBUG */
  return NULL;
}

/*
 * thread_rc_track_free ()
 *   return:
 *   thread_p(in):
 */
static void
thread_rc_track_free (THREAD_ENTRY * thread_p, int id)
{
  THREAD_RC_TRACK *prev_track;

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  assert_release (thread_p != NULL);
  assert_release (id == thread_p->track_depth);

  if (thread_p->track != NULL)
    {
      assert_release (id >= 0);

#if !defined (NDEBUG)
      if (thread_p->track->tracked_resources != NULL)
	{
	  free_and_init (thread_p->track->tracked_resources);
	}
#endif

      prev_track = thread_p->track->prev;

      /* add to free list */
      thread_p->track->prev = thread_p->track_free_list;
      thread_p->track_free_list = thread_p->track;

      /* pop previous track info */
      thread_p->track = prev_track;

      thread_p->track_depth--;
    }
}

/*
 * thread_rc_track_is_enabled () - check if is enabled
 *   return:
 *   thread_p(in):
 */
static bool
thread_rc_track_is_enabled (THREAD_ENTRY * thread_p)
{
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  assert_release (thread_p != NULL);

  if (prm_get_bool_value (PRM_ID_USE_SYSTEM_MALLOC))
    {
      /* disable tracking */
      assert_release (thread_p->track == NULL);
      assert_release (thread_p->track_depth == -1);

      return false;
    }

  return true;
}

/*
 * thread_rc_track_need_to_trace () - check if is track valid
 *   return:
 *   thread_p(in):
 */
bool
thread_rc_track_need_to_trace (THREAD_ENTRY * thread_p)
{
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  assert_release (thread_p != NULL);

  /* If it reaches the threshold, cubrid stop tracking and clean thread_p->track. See thread_rc_track_meter. */
  return thread_rc_track_is_enabled (thread_p) && thread_p->track != NULL;
}

/*
 * thread_rc_track_enter () - save current track info
 *   return:
 *   thread_p(in):
 */
int
thread_rc_track_enter (THREAD_ENTRY * thread_p)
{
  THREAD_RC_TRACK *track;

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  assert_release (thread_p != NULL);

  if (thread_rc_track_is_enabled (thread_p))
    {
      track = thread_rc_track_alloc (thread_p);
      assert_release (track != NULL);
      if (track == NULL)
	{
	  return ER_FAILED;
	}
    }

  return thread_p->track_depth;
}

/*
 * thread_rc_track_exit () -
 *   return:
 *   thread_p(in):
 *   id(in): saved track id
 */
int
thread_rc_track_exit (THREAD_ENTRY * thread_p, int id)
{
  int ret = NO_ERROR;

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  assert_release (thread_p != NULL);

  if (thread_rc_track_need_to_trace (thread_p))
    {
      assert_release (id == thread_p->track_depth);
      assert_release (id >= 0);

      ret = thread_rc_track_check (thread_p, id);
#if !defined(NDEBUG)
      if (ret != NO_ERROR)
	{
	  (void) thread_rc_track_dump_all (thread_p, stderr);
	}
#endif

      (void) thread_rc_track_free (thread_p, id);
    }

  return ret;
}

/*
 * thread_rc_track_amount_helper () -
 *   return:
 *   thread_p(in):
 */
static INT32
thread_rc_track_amount_helper (THREAD_ENTRY * thread_p, int rc_idx)
{
  INT32 amount;
  THREAD_RC_TRACK *track;
  THREAD_RC_METER *meter;
  int j;

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  assert_release (thread_p != NULL);

  assert_release (rc_idx >= 0);
  assert_release (rc_idx < RC_LAST);

  amount = 0;			/* init */

  track = thread_p->track;
  if (track != NULL)
    {
      for (j = 0; j < MGR_LAST; j++)
	{
	  meter = &(track->meter[rc_idx][j]);
	  amount += meter->m_amount;
	}
    }

  THREAD_RC_TRACK_ASSERT (thread_p, stderr, amount >= 0);

  return amount;
}

/*
 * thread_rc_track_amount_pgbuf () -
 *   return:
 *   thread_p(in):
 */
int
thread_rc_track_amount_pgbuf (THREAD_ENTRY * thread_p)
{
  return thread_rc_track_amount_helper (thread_p, RC_PGBUF);
}

/*
 * thread_rc_track_amount_qlist () -
 *   return:
 *   thread_p(in):
 */
int
thread_rc_track_amount_qlist (THREAD_ENTRY * thread_p)
{
  return thread_rc_track_amount_helper (thread_p, RC_QLIST);
}

#if !defined(NDEBUG)
/*
 * thread_rc_track_meter_assert_csect_dependency () -
 *   return:
 *   meter(in):
 */
static void
thread_rc_track_meter_assert_csect_dependency (THREAD_ENTRY * thread_p, THREAD_RC_METER * meter, int amount, void *ptr)
{
  int cs_idx;

  assert (meter != NULL);
  assert (amount != 0);
  assert (ptr != NULL);

  cs_idx = *((int *) ptr);

  /* TODO - skip out too many CS */
  if (cs_idx >= ONE_K)
    {
      return;
    }

  assert (cs_idx >= 0);
  assert (cs_idx < ONE_K);

  /* check CS dependency */
  if (amount > 0)
    {
      switch (cs_idx)
	{
	  /* CSECT_CT_OID_TABLE -> CSECT_LOCATOR_SR_CLASSNAME_TABLE is NOK */
	  /* CSECT_LOCATOR_SR_CLASSNAME_TABLE -> CSECT_CT_OID_TABLE is OK */
	case CSECT_LOCATOR_SR_CLASSNAME_TABLE:
	  THREAD_RC_TRACK_METER_ASSERT (thread_p, stderr, meter, meter->m_hold_buf[CSECT_CT_OID_TABLE] == 0);
	  break;

	default:
	  break;
	}
    }
}

/*
 * thread_rc_track_meter_assert_csect_usage () -  assert enter mode of critical
 * 	section with the existed lock state for each thread.
 *   return:
 *   meter(in):
 *   enter_mode(in):
 *   ptr(in):
 */
static void
thread_rc_track_meter_assert_csect_usage (THREAD_ENTRY * thread_p, THREAD_RC_METER * meter, int enter_mode, void *ptr)
{
  int cs_idx;
  unsigned char enter_count;

  assert (meter != NULL);
  assert (ptr != NULL);

  cs_idx = *((int *) ptr);

  /* TODO - skip out too many CS */
  if (cs_idx >= ONE_K)
    {
      return;
    }

  assert (cs_idx >= 0);
  assert (cs_idx < ONE_K);

  switch (enter_mode)
    {
    case THREAD_TRACK_CSECT_ENTER_AS_READER:
      if (meter->m_rwlock_buf[cs_idx] >= 0)
	{
	  if (THREAD_METER_IS_DEMOTED_READER (meter->m_rwlock_buf[cs_idx]))
	    {
	      enter_count = THREAD_METER_STRIP_DEMOTED_READER_FLAG (meter->m_rwlock_buf[cs_idx]);
	      assert (enter_count < THREAD_METER_MAX_ENTER_COUNT);

	      /* demoted-reader can re-enter as reader again */
	      meter->m_rwlock_buf[cs_idx]++;
	    }
	  else
	    {
	      /* enter as reader first time or reader re-enter */
	      meter->m_rwlock_buf[cs_idx]++;

	      /* reader re-enter is not allowed. */
	      THREAD_RC_TRACK_METER_ASSERT (thread_p, stderr, meter, meter->m_rwlock_buf[cs_idx] <= 1);
	    }
	}
      else
	{
	  enter_count = -meter->m_rwlock_buf[cs_idx];
	  assert (enter_count < THREAD_METER_MAX_ENTER_COUNT);

	  /* I am a writer already. re-enter as reader, treat as re-enter as writer */
	  meter->m_rwlock_buf[cs_idx]--;
	}
      break;

    case THREAD_TRACK_CSECT_ENTER_AS_WRITER:
      if (meter->m_rwlock_buf[cs_idx] <= 0)
	{
	  enter_count = -meter->m_rwlock_buf[cs_idx];
	  assert (enter_count < THREAD_METER_MAX_ENTER_COUNT);

	  /* enter as writer first time or writer re-enter */
	  meter->m_rwlock_buf[cs_idx]--;
	}
      else
	{
	  /* I am a reader or demoted-reader already. re-enter as writer is not allowed */
	  THREAD_RC_TRACK_METER_ASSERT (thread_p, stderr, meter, false);
	}
      break;

    case THREAD_TRACK_CSECT_PROMOTE:
      /* If I am not a reader or demoted-reader, promote is not allowed */
      THREAD_RC_TRACK_METER_ASSERT (thread_p, stderr, meter, meter->m_rwlock_buf[cs_idx] > 0);

      enter_count = THREAD_METER_STRIP_DEMOTED_READER_FLAG (meter->m_rwlock_buf[cs_idx]);
      assert (enter_count != 0);

      if (enter_count == 1)
	{
	  /* promote reader or demoted-reader to writer */
	  meter->m_rwlock_buf[cs_idx] = -1;
	}
      else
	{
	  /* In the middle of citical session, promote is not allowed. only when demote-reader re-enter as reader can
	   * go to here. */
	  THREAD_RC_TRACK_METER_ASSERT (thread_p, stderr, meter, false);
	}
      break;

    case THREAD_TRACK_CSECT_DEMOTE:
      /* if I am not a writer, demote is not allowed */
      THREAD_RC_TRACK_METER_ASSERT (thread_p, stderr, meter, meter->m_rwlock_buf[cs_idx] < 0);

      if (meter->m_rwlock_buf[cs_idx] == -1)
	{
	  /* demote writer to demoted-reader */
	  enter_count = 1;
	  meter->m_rwlock_buf[cs_idx] = THREAD_METER_WITH_DEMOTED_READER_FLAG (enter_count);
	}
      else
	{
	  /* In the middle of citical session, demote is not allowed */
	  THREAD_RC_TRACK_METER_ASSERT (thread_p, stderr, meter, false);
	}
      break;

    case THREAD_TRACK_CSECT_EXIT:
      /* without entered before, exit is not allowed. */
      THREAD_RC_TRACK_METER_ASSERT (thread_p, stderr, meter, meter->m_rwlock_buf[cs_idx] != 0);

      if (meter->m_rwlock_buf[cs_idx] > 0)
	{
	  if (THREAD_METER_IS_DEMOTED_READER (meter->m_rwlock_buf[cs_idx]))
	    {
	      enter_count = THREAD_METER_STRIP_DEMOTED_READER_FLAG (meter->m_rwlock_buf[cs_idx]);
	      assert (enter_count != 0);

	      enter_count--;
	      if (enter_count != 0)
		{
		  /* still keep demoted-reader flag */
		  meter->m_rwlock_buf[cs_idx] = THREAD_METER_WITH_DEMOTED_READER_FLAG (enter_count);
		}
	      else
		{
		  meter->m_rwlock_buf[cs_idx] = 0;
		}
	    }
	  else
	    {
	      /* reader exit */
	      meter->m_rwlock_buf[cs_idx]--;
	    }
	}
      else
	{			/* (meter->m_rwlock_buf[cs_idx] < 0 */
	  meter->m_rwlock_buf[cs_idx]++;
	}
      break;
    default:
      assert (false);
      break;
    }
}
#endif

/*
 * thread_rc_track_meter () -
 *   return:
 *   thread_p(in):
 */
void
thread_rc_track_meter (THREAD_ENTRY * thread_p, const char *file_name, const int line_no, int amount, void *ptr,
		       int rc_idx, int mgr_idx)
{
  THREAD_RC_TRACK *track;
  THREAD_RC_METER *meter;
  int enter_mode = -1;
  static bool report_track_cs_overflow = false;

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  assert_release (thread_p != NULL);

  if (thread_p->track == NULL)
    {
      return;			/* not in track enter */
    }

  assert_release (thread_p->track != NULL);

  assert_release (0 <= rc_idx);
  assert_release (rc_idx < RC_LAST);
  assert_release (0 <= mgr_idx);
  assert_release (mgr_idx < MGR_LAST);

  assert_release (amount != 0);

  track = thread_p->track;

  if (track != NULL && 0 <= rc_idx && rc_idx < RC_LAST && 0 <= mgr_idx && mgr_idx < MGR_LAST)
    {
      if (rc_idx == RC_CS)
	{
	  enter_mode = amount;

	  /* recover the amount for RC_CS */
	  switch (enter_mode)
	    {
	    case THREAD_TRACK_CSECT_ENTER_AS_READER:
	      amount = 1;
	      break;
	    case THREAD_TRACK_CSECT_ENTER_AS_WRITER:
	      amount = 1;
	      break;
	    case THREAD_TRACK_CSECT_PROMOTE:
	      amount = 1;
	      break;
	    case THREAD_TRACK_CSECT_DEMOTE:
	      amount = -1;
	      break;
	    case THREAD_TRACK_CSECT_EXIT:
	      amount = -1;
	      break;
	    default:
	      assert_release (false);
	      break;
	    }
	}

      /* check iff is tracking one */
      if (rc_idx == RC_VMEM)
	{
	  if (track->private_heap_id != thread_p->private_heap_id)
	    {
	      return;		/* ignore */
	    }
	}

      meter = &(track->meter[rc_idx][mgr_idx]);

      /* If it reaches the threshold just stop tracking and clear */
      if (meter->m_amount + amount > meter->m_threshold)
	{
#if 0
	  assert (0);
#endif
	  thread_rc_track_finalize (thread_p);
	  return;
	}

      meter->m_amount += amount;

#if 1				/* TODO - */
      /* skip out qlist check; is checked separately */
      if (rc_idx == RC_QLIST)
	{
	  ;			/* nop */
	}
      else
	{
	  THREAD_RC_TRACK_METER_ASSERT (thread_p, stderr, meter, 0 <= meter->m_amount);
	}
#else
      THREAD_RC_TRACK_METER_ASSERT (thread_p, stderr, meter, 0 <= meter->m_amount);
#endif

#if !defined(NDEBUG)
      switch (rc_idx)
	{
	case RC_PGBUF:
	  /* check fix/unfix protocol */
	  {
	    assert (ptr != NULL);

	    if (amount > 0)
	      {
		assert_release (amount == 1);
	      }
	    else
	      {
		assert_release (amount == -1);
	      }
	  }
	  break;

	case RC_CS:
	  /* check Critical Section cycle and keep current hold info */
	  {
	    int cs_idx;

	    assert (ptr != NULL);

	    cs_idx = *((int *) ptr);

	    /* TODO - skip out too many CS */
	    if (cs_idx < ONE_K)
	      {
		assert (cs_idx >= 0);
		assert (cs_idx < ONE_K);

		THREAD_RC_TRACK_METER_ASSERT (thread_p, stderr, meter, meter->m_hold_buf[cs_idx] >= 0);
		if (amount > 0)
		  {
		    assert_release (amount == 1);
		  }
		else
		  {
		    assert_release (amount == -1);
		  }

		meter->m_hold_buf[cs_idx] += amount;

		THREAD_RC_TRACK_METER_ASSERT (thread_p, stderr, meter, meter->m_hold_buf[cs_idx] >= 0);

		/* re-set buf size */
		meter->m_hold_buf_size = MAX (meter->m_hold_buf_size, cs_idx + 1);
		assert (meter->m_hold_buf_size <= ONE_K);
	      }
	    else if (report_track_cs_overflow == false)
	      {
		report_track_cs_overflow = true;	/* report only once */
		er_log_debug (ARG_FILE_LINE, "thread_rc_track_meter: hold_buf overflow: buf_size=%d, idx=%d",
			      meter->m_hold_buf_size, cs_idx);
	      }
	  }
	  break;

	default:
	  break;
	}
#endif

      if (amount > 0)
	{
	  meter->m_add_file_name = file_name;
	  meter->m_add_line_no = line_no;
	}
      else if (amount < 0)
	{
	  meter->m_sub_file_name = file_name;
	  meter->m_sub_line_no = line_no;
	}

#if !defined(NDEBUG)
      (void) thread_rc_track_meter_at (meter, file_name, line_no, amount, ptr);

      if (rc_idx == RC_CS)
	{
	  (void) thread_rc_track_meter_assert_csect_dependency (thread_p, meter, amount, ptr);

	  (void) thread_rc_track_meter_assert_csect_usage (thread_p, meter, enter_mode, ptr);
	}
#endif
    }
}

/*
 * thread_rc_track_meter_dump () -
 *   return:
 *   thread_p(in):
 *   outfp(in):
 *   meter(in):
 */
static void
thread_rc_track_meter_dump (THREAD_ENTRY * thread_p, FILE * outfp, THREAD_RC_METER * meter)
{
  int i;

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  assert_release (thread_p != NULL);
  assert_release (meter != NULL);

  if (outfp == NULL)
    {
      outfp = stderr;
    }

  if (meter != NULL)
    {
      fprintf (outfp, "         +--- amount = %d (threshold = %d)\n", meter->m_amount, meter->m_threshold);
      fprintf (outfp, "         +--- add_file_line = %s:%d\n", meter->m_add_file_name, meter->m_add_line_no);
      fprintf (outfp, "         +--- sub_file_line = %s:%d\n", meter->m_sub_file_name, meter->m_sub_line_no);
#if !defined(NDEBUG)
      /* dump hold_buf */
      if (meter->m_hold_buf_size > 0)
	{
	  fprintf (outfp, "            +--- hold_at = ");
	  for (i = 0; i < meter->m_hold_buf_size; i++)
	    {
	      if (meter->m_hold_buf[i] != '\0')
		{
		  fprintf (outfp, "[%d]:%c ", i, meter->m_hold_buf[i]);
		}
	    }
	  fprintf (outfp, "\n");
	  fprintf (outfp, "            +--- hold_buf_size = %d\n", meter->m_hold_buf_size);

	  fprintf (outfp, "            +--- read/write lock = ");
	  for (i = 0; i < meter->m_hold_buf_size; i++)
	    {
	      if (meter->m_rwlock_buf[i] != '\0')
		{
		  fprintf (outfp, "[%d]:%d ", i, (int) meter->m_rwlock_buf[i]);
		}
	    }
	  fprintf (outfp, "\n");
	  fprintf (outfp, "            +--- read/write lock size = %d\n", meter->m_hold_buf_size);
	}

      /* dump tracked resources */
      if (meter->m_tracked_res_count > 0)
	{
	  fprintf (outfp, "            +--- tracked res = ");
	  for (i = 0; i < meter->m_tracked_res_count; i++)
	    {
	      fprintf (outfp, "res_ptr=%p amount=%d first_caller=%s:%d\n", meter->m_tracked_res[i].res_ptr,
		       meter->m_tracked_res[i].amount, meter->m_tracked_res[i].caller_file,
		       meter->m_tracked_res[i].caller_line);
	    }
	  fprintf (outfp, "            +--- tracked res count = %d\n", meter->m_tracked_res_count);
	}
#endif /* !NDEBUG */
    }
}

/*
 * thread_rc_track_dump () -
 *   return:
 *   thread_p(in):
 *   outfp(in):
 *   track(in):
 *   depth(in):
 */
static void
thread_rc_track_dump (THREAD_ENTRY * thread_p, FILE * outfp, THREAD_RC_TRACK * track, int depth)
{
  int i, j;
  const char *rcname, *mgrname;

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  assert_release (thread_p != NULL);
  assert_release (track != NULL);
  assert_release (depth >= 0);

  if (outfp == NULL)
    {
      outfp = stderr;
    }

  if (track != NULL)
    {
      fprintf (outfp, "+--- track depth = %d\n", depth);
      for (i = 0; i < RC_LAST; i++)
	{
	  rcname = thread_rc_track_rcname (i);
	  fprintf (outfp, "   +--- %s\n", rcname);

	  for (j = 0; j < MGR_LAST; j++)
	    {
	      mgrname = thread_rc_track_mgrname (j);
	      fprintf (outfp, "      +--- %s\n", mgrname);

	      (void) thread_rc_track_meter_dump (thread_p, outfp, &(track->meter[i][j]));
	    }
	}

      fflush (outfp);
    }
}





/*
 * thread_rc_track_dump_all () -
 *   return:
 *   thread_p(in):
 */
void
thread_rc_track_dump_all (THREAD_ENTRY * thread_p, FILE * outfp)
{
  THREAD_RC_TRACK *track;
  int depth;

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  assert_release (thread_p != NULL);

  if (outfp == NULL)
    {
      outfp = stderr;
    }

  fprintf (outfp, "------------ Thread[%d] Resource Track Info: ------------\n", thread_p->index);

  fprintf (outfp, "track_depth = %d\n", thread_p->track_depth);
  fprintf (outfp, "track_threshold = %d\n", thread_p->track_threshold);
  for (track = thread_p->track_free_list, depth = 0; track != NULL; track = track->prev, depth++)
    {
      ;
    }
  fprintf (outfp, "track_free_list size = %d\n", depth);

  for (track = thread_p->track, depth = thread_p->track_depth; track != NULL; track = track->prev, depth--)
    {
      (void) thread_rc_track_dump (thread_p, outfp, track, depth);
    }
  assert_release (depth == -1);

  fprintf (outfp, "\n");

  fflush (outfp);
}

#if !defined(NDEBUG)
/*
 * thread_rc_track_meter_at () -
 *   return:
 *   meter(in):
 */
static void
thread_rc_track_meter_at (THREAD_RC_METER * meter, const char *caller_file, int caller_line, int amount, void *ptr)
{
  const char *p = NULL;
  int min, max, mid, mem_size;
  bool found = false;

  assert_release (meter != NULL);
  assert_release (amount != 0);

  /* Truncate path to file name */
  p = (char *) caller_file + strlen (caller_file);
  while (p)
    {
      if (p == caller_file)
	{
	  break;
	}

      if (*p == '/' || *p == '\\')
	{
	  p++;
	  break;
	}

      p--;
    }

  /* TODO: A binary search function that could also return the rightful position for an entry that was not found is
   * really necessary. */

  /* There are three possible actions here: 1. Resource doesn't exist and must be added. 2. Resource exists and we
   * update amount to a non-zero value. 3. Resource exists and new amount is 0 and it must be removed from tracked
   * array. */
  /* The array is ordered by resource pointer and binary search is used. */
  min = 0;
  max = meter->m_tracked_res_count - 1;
  mid = 0;

  while (min <= max)
    {
      /* Get middle */
      mid = (min + max) >> 1;
      if (ptr == meter->m_tracked_res[mid].res_ptr)
	{
	  found = true;
	  break;
	}
      if (ptr < meter->m_tracked_res[mid].res_ptr)
	{
	  /* Set search range to [min, mid - 1] */
	  max = mid - 1;
	}
      else
	{
	  /* Set search range to [min + 1, max] */
	  min = ++mid;
	}
    }

  if (found)
    {
      assert_release (mid < meter->m_tracked_res_count);

      /* Update amount for resource */
      meter->m_tracked_res[mid].amount += amount;
      if (meter->m_tracked_res[mid].amount == 0)
	{
	  /* Remove tracked resource */
	  mem_size = (meter->m_tracked_res_count - 1 - mid) * sizeof (THREAD_TRACKED_RESOURCE);

	  if (mem_size > 0)
	    {
	      memmove (&meter->m_tracked_res[mid], &meter->m_tracked_res[mid + 1], mem_size);
	    }
	  meter->m_tracked_res_count--;
	}
    }
  else
    {
      /* Add new tracked resource */
      assert_release (mid <= meter->m_tracked_res_count);
      if (meter->m_tracked_res_count == meter->m_tracked_res_capacity)
	{
	  /* No more room for new resources */
	  return;
	}
      /* Try to free the memory space for new resource */
      mem_size = (meter->m_tracked_res_count - mid) * sizeof (THREAD_TRACKED_RESOURCE);
      if (mem_size > 0)
	{
	  memmove (&meter->m_tracked_res[mid + 1], &meter->m_tracked_res[mid], mem_size);
	}
      /* Save new resource */
      meter->m_tracked_res[mid].res_ptr = ptr;
      meter->m_tracked_res[mid].amount = amount;
      meter->m_tracked_res[mid].caller_line = caller_line;
      strncpy (meter->m_tracked_res[mid].caller_file, p, THREAD_TRACKED_RES_CALLER_FILE_MAX_SIZE);
      meter->m_tracked_res[mid].caller_file[THREAD_TRACKED_RES_CALLER_FILE_MAX_SIZE - 1] = '\0';
      meter->m_tracked_res_count++;
    }
}
#endif /* !NDEBUG */

// *INDENT-ON*
