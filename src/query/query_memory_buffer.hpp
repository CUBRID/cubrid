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
  MEMORY_BUFFER_ENTRY *local_next;
  MEMORY_BUFFER_ENTRY *next;
  UINT64 del_tran_id;
  FILEIO_PAGE *io_page_p;
};

extern int qmgr_initialize_memory_buffer ();
extern void qmgr_finalize_memory_buffer ();

static void *qmgr_allocate_memory_buffer_entry ();
static int qmgr_free_memory_buffer_entry (void *entry_p);
static int qmgr_initialize_temp_buffer_entry (void *entry_p);
extern MEMORY_BUFFER_ENTRY *qmgr_get_memory_buffer_entry (THREAD_ENTRY *thread_p, int count);
extern void qmgr_put_memory_buffer_entry (THREAD_ENTRY *thread_p, MEMORY_BUFFER_ENTRY *entry_p);

extern void qmgr_free_temp_file_membuf (THREAD_ENTRY *thread_p, std::vector <MEMORY_BUFFER_ENTRY *> &membuf_ref);
extern int qmgr_new_temp_file_membuf_page (THREAD_ENTRY *thread_p, std::vector <MEMORY_BUFFER_ENTRY *> &membuf_ref,
    int count);
extern PAGE_PTR qmgr_get_temp_file_membuf_page (std::vector <MEMORY_BUFFER_ENTRY *> &membuf_ref, int index);
extern PAGE_PTR qmgr_get_membuf_page (MEMORY_BUFFER_ENTRY *entry_p);

#endif /* _QUERY_MEMORY_BUFFER_H_ */
