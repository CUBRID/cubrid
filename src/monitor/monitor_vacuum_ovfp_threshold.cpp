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
 * monitor_vacuum_ovfp_threshold.cpp - (at Server).
 *
 */

#if !defined (SERVER_MODE)
#error Belongs to server module
#endif

#if defined (SERVER_MODE)

#include <assert.h>
#include "heap_file.h"
#include "monitor_vacuum_ovfp_threshold.hpp"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/* *INDENT-OFF* */
#define OID_COMPARE(a, b)  (((a)->volid != (b)->volid) ? ((a)->volid - (b)->volid) : \
                              (((a)->pageid != (b)->pageid) ? ((a)->pageid - (b)->pageid) : ((a)->slotid - (b)->slotid)))
#define BTID_COMPARE(a, b) (((a)->vfid.volid != (b)->vfid.volid) ? ((a)->vfid.volid - (b)->vfid.volid) : ((a)->vfid.fileid - (b)->vfid.fileid))
/* *INDENT-ON* */

// ============================================================================
// class ovfp_monitor_lock
// public
ovfp_monitor_lock::ovfp_monitor_lock()
{
  memset (m_lock_arr, 0xFF, sizeof (m_lock_arr));
}

void
ovfp_monitor_lock::lock (int lock_index, int owner_id)
{
  assert (lock_index >= 0 && lock_index < LOCK_ITEMS_SIZE);
  m_ovfp_monitor_mutex.lock();
  while (m_lock_arr[lock_index] != LOCK_FREE_OWNER_ID)
    {
      assert ((m_lock_arr[lock_index] != owner_id) || (LOCK_ALL_OWNER_ID == owner_id));
      m_ovfp_monitor_mutex.unlock();
      usleep (1);
      m_ovfp_monitor_mutex.lock();
    }
  m_lock_arr[lock_index] = owner_id;
  m_ovfp_monitor_mutex.unlock();
}

void
ovfp_monitor_lock::unlock (int lock_index, int owner_id)
{
  assert (lock_index >= 0 && lock_index < LOCK_ITEMS_SIZE);
  m_ovfp_monitor_mutex.lock();
  assert (m_lock_arr[lock_index] == owner_id);
  m_lock_arr[lock_index] = LOCK_FREE_OWNER_ID;
  m_ovfp_monitor_mutex.unlock();
}

// ============================================================================
// class ovfp_threshold
// private
void
ovfp_threshold::clear (INDEX_OVFP_INFO *root)
{
  INDEX_OVFP_INFO *pt;
  while (root)
    {
      pt = root;
      root = pt->next;
      free (pt);
    }
}

void
ovfp_threshold::free_info_mem (INDEX_OVFP_INFO *pt)
{
  assert (pt != NULL);
  pt->next = m_free_head ? m_free_head : NULL;
  m_free_head = pt;
}

INDEX_OVFP_INFO *
ovfp_threshold::add (BTID *btid, OID *class_oid, time_t time_now, int npages)
{
  INDEX_OVFP_INFO  *pt;

  pt = find (btid, class_oid);
  if (pt)
    {
      pt->event_time[RECENT_POS] = time_now;
      pt->read_pages[RECENT_POS] = npages;

      if (pt->read_pages[MAX_POS] <= npages)
	{
	  pt->event_time[MAX_POS] = time_now;
	  pt->read_pages[MAX_POS] = npages;
	}
      pt->hit_cnt++;

      return pt;
    }


  pt = alloc_info_mem();
  assert (pt != NULL);
  if (pt)
    {
      // new item
      BTID_COPY ( & (pt->btid), btid);
      COPY_OID (& (pt->class_oid), class_oid);

      pt->event_time[RECENT_POS] = time_now;
      pt->read_pages[RECENT_POS] = npages;
      pt->event_time[MAX_POS] = time_now;
      pt->read_pages[MAX_POS] = npages;
      pt->hit_cnt = 1;

      if (m_head == NULL || m_prev == NULL)
	{
	  pt->next = m_head;
	  m_head = pt;
	}
      else
	{
	  pt->next = m_prev->next;
	  m_prev->next = pt;
	}
    }

  return pt;
}

//protected
INDEX_OVFP_INFO  *
ovfp_threshold::alloc_info_mem()
{
  INDEX_OVFP_INFO *pt;

  if (m_free_head)
    {
      pt = m_free_head;
      m_free_head = m_free_head->next;
    }
  else
    {
      pt = (INDEX_OVFP_INFO *) malloc (sizeof (INDEX_OVFP_INFO));
      assert (pt != NULL);
    }
  return pt;
}

INDEX_OVFP_INFO *
ovfp_threshold::find (BTID *btid, OID *class_oid)
{
  int cmp;

  if (m_head == NULL)
    {
      m_prev = NULL;
      return NULL;
    }

  for (INDEX_OVFP_INFO *cur = m_head; cur; cur = cur->next)
    {
      cmp = OID_COMPARE (& (cur->class_oid), class_oid);
      if ( cmp == 0 )
	{
	  cmp = BTID_COMPARE (& (cur->btid), btid);
	}

      if ( cmp > 0 )
	{
	  return NULL;
	}
      else if ( cmp == 0 )
	{
	  return cur;
	}

      m_prev = cur;
    }

  return NULL;
}

//public
ovfp_threshold::ovfp_threshold()
{
  m_free_head = m_head = m_prev = NULL;

  m_lock_idx = -1;
  m_lock_ptr = NULL;
}
ovfp_threshold::~ovfp_threshold()
{
  if (m_lock_ptr)
    {
      m_lock_ptr->lock (m_lock_idx, m_lock_idx);
      clear (m_head);
      clear (m_free_head);
      m_lock_ptr->unlock (m_lock_idx, m_lock_idx);
    }
  else
    {
      clear (m_head);
      clear (m_free_head);
    }
  m_head = m_free_head = NULL;
}

void
ovfp_threshold::set_worker_idx (int idx, ovfp_monitor_lock *lock_mgr_p)
{
  assert (idx >= 0 && idx <= VACUUM_MAX_WORKER_COUNT);
  m_lock_idx = idx;
  if (m_lock_idx >= 0 && m_lock_idx < VACUUM_MAX_WORKER_COUNT)
    {
      m_lock_ptr = lock_mgr_p;
    }
}

INDEX_OVFP_INFO *
ovfp_threshold::get_head ()
{
  return m_head;
}

void
ovfp_threshold::add_info (BTID *btid, OID *class_oid, int npages)
{
  time_t     time_now;

  time_now = time (NULL);
  if (m_lock_ptr)
    {
      m_lock_ptr->lock (m_lock_idx, m_lock_idx);
      add (btid, class_oid, time_now, npages);
      m_lock_ptr->unlock (m_lock_idx, m_lock_idx);
    }
  else
    {
      add (btid, class_oid, time_now, npages);
    }
}

void
ovfp_threshold::check_over_duration_times (time_t *over_tm)
{
  // Delete information that has not been updated for a long time.
  INDEX_OVFP_INFO    *cur, *new_head, *tail;

  new_head = tail = NULL;
  while (m_head)
    {
      cur = m_head;
      m_head = m_head->next;
      if (*over_tm - cur->event_time[RECENT_POS] > 0)
	{
	  free_info_mem (cur);
	}
      else
	{
	  if (tail)
	    {
	      tail->next = cur;
	    }
	  else
	    {
	      new_head = cur;
	    }
	  tail = cur;
	}
    }

  if (tail)
    {
      tail->next = NULL;
    }

  m_head = new_head;
}

// ============================================================================
// class ovfp_printer: public ovfp_threshold
// public
void
ovfp_printer::sort()
{
  INDEX_OVFP_INFO  *tmp, *prev, *cur, *new_head;

  assert (m_lock_idx == LOCK_ALL_OWNER_ID);
  new_head = tmp = NULL;
  while (m_head)
    {
      tmp = m_head;
      m_head = m_head->next;

      if (new_head == NULL)
	{
	  new_head = tmp;
	  tmp->next = NULL;
	}
      else
	{
	  prev = NULL;
	  cur = new_head;
	  while (cur)
	    {
	      if (cur->event_time[RECENT_POS] <= tmp->event_time[RECENT_POS])
		{
		  break;
		}

	      prev = cur;
	      cur = cur->next;
	    }

	  if (prev == NULL)
	    {
	      tmp->next = new_head;
	      new_head = tmp;
	    }
	  else
	    {
	      tmp->next = prev->next;
	      prev->next = tmp;
	    }
	}
    }

  m_head = new_head;
}

void
ovfp_printer::add_info (INDEX_OVFP_INFO *new_info)
{
  INDEX_OVFP_INFO *pt;
  // This function must obtain a lock and be guaranteed to be called.

  pt = find (&new_info->btid,&new_info->class_oid);
  if (pt == NULL)
    {
      pt = alloc_info_mem();
      assert (pt != NULL);
      if (pt)
	{
	  // new item
	  memcpy (pt, new_info, sizeof (INDEX_OVFP_INFO));
	  if (m_head == NULL)
	    {
	      pt->next = NULL;
	      m_head = pt;
	    }
	  else if (m_prev == NULL)
	    {
	      pt->next = m_head;
	      m_head = pt;
	    }
	  else
	    {
	      pt->next = m_prev->next;
	      m_prev->next = pt;
	    }
	}
    }
  else
    {
      pt->hit_cnt += new_info->hit_cnt;
      if (pt->read_pages[MAX_POS] <=  new_info->read_pages[MAX_POS])
	{
	  if (pt->read_pages[MAX_POS] < new_info->read_pages[MAX_POS])
	    {
	      pt->read_pages[MAX_POS] = new_info->read_pages[MAX_POS];
	      pt->event_time[MAX_POS] = new_info->event_time[MAX_POS];
	    }
	  else if (pt->event_time[MAX_POS] < new_info->event_time[MAX_POS])
	    {
	      pt->event_time[MAX_POS] = new_info->event_time[MAX_POS];
	    }
	}

      if (pt->event_time[RECENT_POS] < new_info->event_time[RECENT_POS])
	{
	  pt->read_pages[RECENT_POS] = new_info->read_pages[RECENT_POS];
	  pt->event_time[RECENT_POS] = new_info->event_time[RECENT_POS];
	}
      else if (pt->event_time[RECENT_POS] == new_info->event_time[RECENT_POS])
	{
	  if (pt->read_pages[RECENT_POS] < new_info->read_pages[RECENT_POS])
	    {
	      pt->read_pages[RECENT_POS] = new_info->read_pages[RECENT_POS];
	    }
	}
    }
}


// ============================================================================
// class ovfp_threshold_mgr
// private function
bool
ovfp_threshold_mgr::get_classoid (THREAD_ENTRY *thread_p, BTID *btid, OID *class_oid)
{
  FILE_DESCRIPTORS fdes;
  assert (btid != NULL);

  if (file_descriptor_get (thread_p, &btid->vfid, &fdes) != NO_ERROR)
    {
      //OID_SET_NULL(&class_oid);
      return false;
    }

  COPY_OID (class_oid, & (fdes.btree.class_oid));
  return true;
}

void
ovfp_threshold_mgr::get_class_name_index_name (THREAD_ENTRY *thread_p, BTID *btid, OID *class_oid, char **class_name,
    char **index_name)
{
  int ret = NO_ERROR;
  char tmp[128];

  assert (btid != NULL);
  assert (class_name != NULL && index_name != NULL);

  if (*class_name != NULL)
    {
      free_and_init (*class_name);
    }
  if (*index_name != NULL)
    {
      free_and_init (*index_name);
    }

  if (heap_get_class_name (thread_p, class_oid, class_name) == NO_ERROR)
    {
      /* get index name */
      if (heap_get_indexinfo_of_btid (thread_p, class_oid, btid, NULL, NULL, NULL, NULL, index_name, NULL) != NO_ERROR)
	{
	  sprintf (tmp, "(%d, %d|%d)", btid->root_pageid, btid->vfid.volid, btid->vfid.fileid /* BTID_AS_ARGS(btid) */);
	  *index_name = strdup (tmp);
	}
    }
  else
    {
      sprintf (tmp, "(%d|%d|%d)", class_oid->volid, class_oid->pageid, class_oid->slotid/* OID_AS_ARGS(class_oid) */);
      *class_name = strdup (tmp);
      sprintf (tmp, "(%d, %d|%d)", btid->root_pageid,  btid->vfid.volid, btid->vfid.fileid/* BTID_AS_ARGS(btid) */);
      *index_name = strdup (tmp);
    }
}

char *
ovfp_threshold_mgr::time_to_string (time_t er_time, char *buf, int size)
{
  struct tm er_tm;
  struct tm *er_tm_p = NULL;

  if (er_time != 0)
    {
      er_tm_p = localtime_r (&er_time, &er_tm);
    }

  if (er_tm_p == NULL)
    {
      strcpy (buf, "00/00/00 00:00:00");
    }
  else
    {
      strftime (buf, size, "%m/%d/%y %H:%M:%S", er_tm_p);
    }
  return buf;
}

void
ovfp_threshold_mgr::print (THREAD_ENTRY *thread_p, FILE *outfp, const INDEX_OVFP_INFO *head_ptr)
{
  INDEX_OVFP_INFO *pt;
  char *class_name, *index_name;
  char time_buf[32], line_buf[256];
#define NUM_ATTRS_CNT (5)
  const char *attr_names[NUM_ATTRS_CNT] = {"Class name", "Index name", "Count", "Num of OVFP recent read", "Max num of OVFP read"};
  int attr_lengths[NUM_ATTRS_CNT] = { 20, 20, 12, 25, 25 };
  int len;

  fprintf (outfp, "\n*** Exceeding read threshold (%d pages) for OID overflow pages (OVFP), Since %s ***\n",
	   m_threshold_pages, m_since_time);
  len = 0;
  for (int i = 0; i < NUM_ATTRS_CNT; i++)
    {
      len += fprintf (outfp, "  %-*s", attr_lengths[i], attr_names[i]);
    }

  memset (line_buf, '=', len);
  line_buf[len] = '\0';
  fprintf (outfp, "\n%s\n", line_buf);

  class_name = index_name = NULL;
  pt = (INDEX_OVFP_INFO *) head_ptr;
  while (pt)
    {
      get_class_name_index_name (thread_p, & (pt->btid), & (pt->class_oid), &class_name, &index_name);
      assert (class_name != NULL && index_name != NULL);
      if (class_name)
	{
	  fprintf (outfp, "  %-*s", MAX (attr_lengths[0], strlen (class_name)), class_name);
	  free_and_init (class_name);
	}
      if (index_name)
	{
	  fprintf (outfp, "  %-*s", MAX (attr_lengths[1], strlen (index_name)), index_name);
	  free_and_init (index_name);
	}

      fprintf (outfp, "  %*lld", attr_lengths[2], (long long) pt->hit_cnt);

      sprintf (line_buf, "  %d (%s)", pt->read_pages[RECENT_POS], time_to_string (pt->event_time[RECENT_POS], time_buf,
	       sizeof (time_buf)));
      fprintf (outfp, "  %-*s", MAX (attr_lengths[3], strlen (line_buf)), line_buf);

      sprintf (line_buf, "  %d (%s)", pt->read_pages[MAX_POS], time_to_string (pt->event_time[MAX_POS], time_buf,
	       sizeof (time_buf)));
      fprintf (outfp, "  %-*s\n", MAX (attr_lengths[4], strlen (line_buf)), line_buf);

      pt = pt->next;
    }
  fprintf (outfp, "\n");

  if (class_name)
    {
      free_and_init (class_name);
    }
  if (index_name)
    {
      free_and_init (index_name);
    }
}

// public function
ovfp_threshold_mgr::ovfp_threshold_mgr()
{
  for ( int worker_idx = 0; worker_idx < VACUUM_MAX_WORKER_COUNT; worker_idx++)
    {
      m_ovfp_threshold[worker_idx].set_worker_idx (worker_idx, &m_ovfp_lock);
    }

  m_over_secs = 45000 /* min */ * 60; // sec
  m_threshold_pages = 1000;
  m_since_time[0] = '\0';
}

void
ovfp_threshold_mgr::init()
{
  time_to_string (time (NULL), m_since_time, sizeof (m_since_time));

  m_over_secs = prm_get_integer_value (PRM_ID_VACUUM_OVFP_CHECK_DURATION);
  m_over_secs *= 60; // to second
  m_threshold_pages = prm_get_integer_value (PRM_ID_VACUUM_OVFP_CHECK_THRESHOLD);
}

void
ovfp_threshold_mgr::add_read_pages_count (THREAD_ENTRY *thread_p, int worker_idx, BTID *btid, int npages)
{
  OID class_oid;

  assert (worker_idx >= 0 && worker_idx < VACUUM_MAX_WORKER_COUNT);
  assert (npages >= m_threshold_pages);

  // BTIDs can be reused. However, CLASS_OID is not reused.
  // That's why we get the class oid immediately here.
  if (get_classoid (thread_p, btid, &class_oid))
    {
      m_ovfp_threshold[worker_idx].add_info (btid, &class_oid, npages);
    }
}

void
ovfp_threshold_mgr::dump (THREAD_ENTRY *thread_p, FILE *outfp)
{
  INDEX_OVFP_INFO *pt;
  ovfp_printer  printer;
  time_t over_tm = time (NULL) - m_over_secs;

  assert (outfp != NULL);

  printer.set_worker_idx (LOCK_ALL_OWNER_ID, NULL);

  for (int i = 0; i < VACUUM_MAX_WORKER_COUNT; i++)
    {
      m_ovfp_lock.lock (i, LOCK_ALL_OWNER_ID);

      m_ovfp_threshold[i].check_over_duration_times (&over_tm);

      pt = m_ovfp_threshold[i].get_head();
      while (pt)
	{
	  printer.add_info (pt);
	  pt = pt->next;
	}

      m_ovfp_lock.unlock (i, LOCK_ALL_OWNER_ID);
    }

  // sort by recent time
  printer.sort();
  print (thread_p, outfp, printer.get_head());
}

#endif // #if defined (SERVER_MODE)

