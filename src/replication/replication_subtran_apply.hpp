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

//
// Replication apply sub-transactions (serial & click counter changes)
//

#ifndef _REPLICATION_SUBTRAN_APPLY_HPP_
#define _REPLICATION_SUBTRAN_APPLY_HPP_

#include <list>
#include <mutex>

// forward declarations
namespace cubreplication
{
  class log_consumer;
  class stream_entry;
}

namespace cubreplication
{
  class subtran_applier
  {
    public:
      subtran_applier () = delete;
      subtran_applier (log_consumer &lc);

      void insert_stream_entry (stream_entry *se);

    private:
      class task;

      task *alloc_task (stream_entry *se);
      void finished_task ();

      log_consumer &m_lc;
      std::mutex m_tasks_mutex;
      std::list<task *> m_tasks;
  };
} // namespace cubreplication

#endif // !_REPLICATION_SUBTRAN_APPLY_HPP_
