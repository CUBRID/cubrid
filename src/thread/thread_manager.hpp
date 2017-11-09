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
 * thread_manager.hpp - interface of tracker for all thread resources
 */

#ifndef _THREAD_MANAGER_HPP_
#define _THREAD_MANAGER_HPP_

#include <mutex>
#include <vector>

template <typename T>
class resource_shared_pool;

namespace thread
{

// forward definition
class worker_pool;
class looper;
class daemon;
class entry;
class entry_executable;

class manager
{
public:
  // TODO: remove starting_index
  manager (std::size_t max_threads, std::size_t starting_index = 0);
  ~manager ();

  worker_pool * create_worker_pool (size_t pool_size, size_t work_queue_size);
  void destroy_worker_pool (worker_pool *& worker_pool_arg);

  daemon * create_daemon (looper & looper_arg, entry_executable * exec_p);
  void destroy_daemon (daemon *& daemon_arg);

  entry *claim_entry (void);
  void retire_entry (entry & entry_p);

private:

  typedef resource_shared_pool<entry> entry_dispatcher;

  template <typename Res, typename ... CtArgs>
  Res * create_and_track_resource (std::vector<Res *> & tracker, size_t entries_count, CtArgs &&... args);
  template <typename Res>
  void destroy_and_untrack_resource (std::vector<Res *> & tracker, Res *& res);
  template <typename Res>
  void destroy_and_untrack_all_resources (std::vector<Res *> & tracker);

  std::size_t m_max_threads;

  std::mutex m_mutex;
  std::vector<worker_pool *> m_worker_pools;
  std::vector<daemon *> m_daemons;

  entry *m_all_entries;
  entry_dispatcher *m_entry_dispatcher;
  std::size_t m_available_entries_count;
};

} // namespace thread

#endif  // _THREAD_MANAGER_HPP_
