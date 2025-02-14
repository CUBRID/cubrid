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
 * query_memory_buffer.c
 */

#include "query_memory_buffer.hpp"

#include "error_manager.h"
#include "list_file.h"
#include "lock_free.h"
#include "page_buffer.h"
#include "thread_manager.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

static LF_ENTRY_DESCRIPTOR qmgr_memory_buffer_descriptor =
{
  offsetof (MEMORY_BUFFER_ENTRY, m_local_next),	/* of_local_next */
  offsetof (MEMORY_BUFFER_ENTRY, m_next),		/* of_next */
  offsetof (MEMORY_BUFFER_ENTRY, m_del_tran_id),	/* of_del_tran_id */
  0,	/* of_key */
  0,	/* of_mutex */
  LF_EM_NOT_USING_MUTEX,		/* using_mutex */
  LF_ENTRY_DESCRIPTOR_MAX_ALLOC,	/* max_alloc_cnt */
  qmgr_allocate_memory_buffer_entry,	/* f_alloc */
  qmgr_free_memory_buffer_entry,	/* f_free */
  qmgr_initialize_memory_buffer_entry,	/* f_init */
  NULL,	/* f_uninit */
  NULL,	/* f_key_copy */
  NULL,	/* f_key_cmp */
  NULL,	/* f_hash */
  NULL,	/* f_duplicate */
};

static LF_FREELIST qmgr_memory_buffer_freelist = LF_FREELIST_INITIALIZER;

/**
 * memory buffer
 */

int
qmgr_initialize_memory_buffer ()
{
  if (lf_freelist_init
      (&qmgr_memory_buffer_freelist, 10, 100, &qmgr_memory_buffer_descriptor, &memory_buffer_Ts) != NO_ERROR)
    {
      int error;
      ASSERT_ERROR_AND_SET (error);
      return error;
    }

  return NO_ERROR;
}

void
qmgr_finalize_memory_buffer ()
{
  lf_freelist_destroy (&qmgr_memory_buffer_freelist);
}

/**
 * memory buffer entry
 */

void *
qmgr_allocate_memory_buffer_entry ()
{
  MEMORY_BUFFER_ENTRY *entry_p = (MEMORY_BUFFER_ENTRY *) malloc (sizeof (MEMORY_BUFFER_ENTRY));
  if (entry_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (MEMORY_BUFFER_ENTRY));
      return NULL;
    }

  entry_p->m_io_page = (FILEIO_PAGE *) malloc (IO_PAGESIZE);
  if (entry_p->m_io_page == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, IO_PAGESIZE);
      free (entry_p);
      return NULL;
    }

  return entry_p;
}

int
qmgr_free_memory_buffer_entry (void *entry_p)
{
  if (entry_p == NULL)
    {
      assert (false);
      return NO_ERROR;
    }

  FILEIO_PAGE *io_page_p = ((MEMORY_BUFFER_ENTRY *) entry_p)->m_io_page;
  if (io_page_p != NULL)
    {
      free (io_page_p);
    }

  free (entry_p);

  return NO_ERROR;
}

int
qmgr_initialize_memory_buffer_entry (void *entry_p)
{
  FILEIO_PAGE *io_page_p;
  PAGE_PTR page_p;

  if (entry_p == NULL)
    {
      assert (false);
      return ER_FAILED;
    }

  page_p = ((MEMORY_BUFFER_ENTRY *) entry_p)->m_io_page->page;
  qfile_init_page_header (page_p);

  CAST_PGPTR_TO_IOPGPTR (io_page_p, page_p);
  fileio_initialize_res (NULL, io_page_p, IO_PAGESIZE);
  io_page_p->prv.ptype = PAGE_MEMORY;

  return NO_ERROR;
}

void
qmgr_put_memory_buffer_entry (THREAD_ENTRY *thread_p, MEMORY_BUFFER_ENTRY *entry_p)
{
  if (entry_p == NULL)
    {
      /* nothing to do */
      return;
    }

  LF_TRAN_ENTRY *tran_entry_p = thread_get_tran_entry (thread_p, THREAD_TS_MEMORY_BUFFER);
  if (tran_entry_p == NULL)
    {
      assert (false);
      return;
    }

  MEMORY_BUFFER_ENTRY *current = entry_p;

  while (current != NULL)
    {
      MEMORY_BUFFER_ENTRY *next = current->m_next;
      current->m_next = NULL;

      if (lf_freelist_retire (tran_entry_p, &qmgr_memory_buffer_freelist, current) != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return;
	}

      current = next;
    }
}

MEMORY_BUFFER_ENTRY *
qmgr_get_memory_buffer_entry (THREAD_ENTRY *thread_p, int count)
{
  if (count <= 0)
    {
      assert (false);
      return NULL;
    }

  LF_TRAN_ENTRY *tran_entry_p = thread_get_tran_entry (thread_p, THREAD_TS_MEMORY_BUFFER);
  if (tran_entry_p == NULL)
    {
      assert (false);
      return NULL;
    }

  MEMORY_BUFFER_ENTRY *head = NULL;

  for (int i = 0; i < count; i++)
    {
      MEMORY_BUFFER_ENTRY *current = (MEMORY_BUFFER_ENTRY *) lf_freelist_claim (tran_entry_p, &qmgr_memory_buffer_freelist);
      if (current == NULL)
	{
	  ASSERT_ERROR ();
	  return NULL;
	}

      assert (current->m_next == NULL);

      current->m_next = head;
      head = current;
    }

  return head;
}

PAGE_PTR
qmgr_get_memory_buffer_entry_page (MEMORY_BUFFER_ENTRY *entry_p)
{
  if (entry_p != NULL && entry_p->m_io_page != NULL)
    {
      PAGE_PTR page_p = entry_p->m_io_page->page;

#if !defined (NDEBUG)
      if (page_p != NULL)
	{
	  FILEIO_PAGE *io_page_p;
	  CAST_PGPTR_TO_IOPGPTR (io_page_p, page_p);
	  assert (io_page_p->prv.ptype == PAGE_MEMORY);
	}
      else
	{
	  assert (false);
	}
#endif /* !NDEBUG */

      return page_p;
    }

  assert (false);
  return NULL;
}

/**
 * memory buffer helper
 */

MEMORY_BUFFER_HELPER *
qmgr_allocate_memory_buffer_helper (size_t max_size)
{
  if (max_size <= 0)
    {
      assert (false);
      return NULL;
    }

  MEMORY_BUFFER_HELPER *helper_p = new MEMORY_BUFFER_HELPER ();
  if (helper_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (MEMORY_BUFFER_HELPER));
      return NULL;
    }

  helper_p->m_max_size = max_size;
  helper_p->m_last = -1;

  return helper_p;
}

void
qmgr_free_memory_buffer_helper (THREAD_ENTRY *thread_p, MEMORY_BUFFER_HELPER **helper_pp)
{
  MEMORY_BUFFER_HELPER *help_p = *helper_pp;
  MEMORY_BUFFER_ENTRY *head = NULL;

  if (*helper_pp == NULL)
    {
      /* nothing to do */
      return;
    }

  for (int i = help_p->m_entries.size () - 1; i >= 0; i--)
    {
      MEMORY_BUFFER_ENTRY *current = help_p->m_entries[i];
      current->m_next = head;
      head = current;
    }

  qmgr_put_memory_buffer_entry (thread_p, head);

  delete help_p;
  *helper_pp = NULL;
}

PAGE_PTR
qmgr_new_memory_buffer_page (THREAD_ENTRY *thread_p, MEMORY_BUFFER_HELPER *helper_p)
{
  if (helper_p == NULL)
    {
      assert (false);
      return NULL;
    }

  helper_p->m_last++;

  if (helper_p->m_last < helper_p->m_entries.size ())
    {
      return qmgr_get_memory_buffer_entry_page (helper_p->m_entries[helper_p->m_last]);
    }

  if (helper_p->m_entries.size () >= helper_p->m_max_size)
    {
      // 최대 개수 초과
      // 에러 아님 -> Temp Volume 사용
      return NULL;
    }

  MEMORY_BUFFER_ENTRY *current = qmgr_get_memory_buffer_entry (thread_p, 4);
  if (current == NULL)
    {
      // 메모리 버퍼 부족
      // 에러 아님 -> Temp Volume 사용
      return NULL;
    }

  try
    {
      for (int i = 0; i < 1; i++)
	{
	  MEMORY_BUFFER_ENTRY *next = current->m_next;
	  current->m_next = NULL;

	  helper_p->m_entries.push_back (current);

	  current = next;
	}
    }
  catch (const std::bad_alloc &e)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (MEMORY_BUFFER_ENTRY *));
      qmgr_put_memory_buffer_entry (thread_p, current);
      // 물리 메모리 부족
      // Temp Volume 사용을 시독할 필요가 있다.
      return NULL;
    }

  return qmgr_get_memory_buffer_entry_page (helper_p->m_entries[helper_p->m_last]);
}

bool
qmgr_is_valid_memory_buffer_index (MEMORY_BUFFER_HELPER *helper_p, size_t index)
{
  return (index >= 0 && helper_p != NULL && !helper_p->m_entries.empty ()
	  && index < helper_p->m_entries.size ()) ? true : false;
}

PAGE_PTR
qmgr_get_memory_buffer_page (MEMORY_BUFFER_HELPER *helper_p, size_t index)
{
  return qmgr_is_valid_memory_buffer_index (helper_p,
	 index) ? qmgr_get_memory_buffer_entry_page (helper_p->m_entries[index]) : NULL;
}
