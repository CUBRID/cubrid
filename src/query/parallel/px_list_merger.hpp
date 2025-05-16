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
 * px_list_merger.hpp - parallel list merger
 */

#ifndef _PX_LIST_MERGER_HPP_
#define _PX_LIST_MERGER_HPP_

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#include "list_file.h"

namespace parallel_query
{
  class list_merger
  {
    public:
      list_merger (THREAD_ENTRY *thread_p);
      ~list_merger ();
      void add_list_id (QFILE_LIST_ID *list_id);
      QFILE_LIST_ID *get_merged_list_id ();
      static void swap_and_destroy_list_id (THREAD_ENTRY *thread_p, QFILE_LIST_ID **orig_list, QFILE_LIST_ID **new_list);
      inline void clear()
      {
	m_head_list_id = nullptr;
	m_thread_p = nullptr;
      }

    private:
      THREAD_ENTRY *m_thread_p;
      QFILE_LIST_ID *m_head_list_id;

      list_merger (const list_merger &other) = delete;
      list_merger &operator= (const list_merger &other) = delete;
  };
}
#endif /*_PX_LIST_MERGER_HPP_ */
