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
 * query_memory_buffer.h
 */

#ifndef _QUERY_MEMORY_BUFFER_H_
#define _QUERY_MEMORY_BUFFER_H_

#include <vector>

#include "file_io.h"
#include "thread_entry.hpp"

typedef struct memory_buffer_entry MEMORY_BUFFER_ENTRY;
struct memory_buffer_entry
{
  MEMORY_BUFFER_ENTRY *m_local_next;
  MEMORY_BUFFER_ENTRY *m_next;
  UINT64 m_del_tran_id;
  FILEIO_PAGE *m_io_page;
};

typedef struct memory_buffer_helper MEMORY_BUFFER_HELPER;
struct memory_buffer_helper
{
  /* *INDENT-OFF* */
  std::vector <MEMORY_BUFFER_ENTRY *> m_entries;
  /* *INDENT-ON* */
  size_t m_max_size;
  int m_last;
};

/* memory buffer */
extern int qmgr_initialize_memory_buffer ();
extern void qmgr_finalize_memory_buffer ();

/* memory buffer entry */
static void *qmgr_allocate_memory_buffer_entry ();
static int qmgr_free_memory_buffer_entry (void *entry_p);
static int qmgr_initialize_memory_buffer_entry (void *entry_p);
extern void qmgr_put_memory_buffer_entry (THREAD_ENTRY *thread_p, MEMORY_BUFFER_ENTRY *entry_p);
extern MEMORY_BUFFER_ENTRY *qmgr_get_memory_buffer_entry (THREAD_ENTRY *thread_p, int count);
extern PAGE_PTR qmgr_get_memory_buffer_entry_page (MEMORY_BUFFER_ENTRY *entry_p);

/* memory buffer helper */
extern MEMORY_BUFFER_HELPER *qmgr_allocate_memory_buffer_helper (size_t max_size);
extern void qmgr_free_memory_buffer_helper (THREAD_ENTRY *thread_p, MEMORY_BUFFER_HELPER **helper_pp);
extern PAGE_PTR qmgr_new_memory_buffer_page (THREAD_ENTRY *thread_p, MEMORY_BUFFER_HELPER *helper_p);
extern bool qmgr_is_valid_memory_buffer_index (MEMORY_BUFFER_HELPER *helper_p, size_t index);
extern PAGE_PTR qmgr_get_memory_buffer_page (MEMORY_BUFFER_HELPER *helper_p, size_t index);

#endif /* _QUERY_MEMORY_BUFFER_H_ */
