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

#include "thread_worker_pool.hpp"
#include "thread_daemon.hpp"

#include <vector>
#include <mutex>

namespace thread
{

class manager
{
public:
  manager ();
  ~manager ();

  template <typename ... Args>
  worker_pool * create_worker_pool (Args &&... args);
  void destroy_worker_pool (worker_pool *& worker_pool_arg);

  template <typename Func, typename ... Args>
  daemon * create_daemon (looper & looper_arg, Func && func, Args &&... args);
  void destroy_daemon (daemon *& daemon_arg);

private:

  template <typename Res, typename ... CtArgs>
  Res * create_and_track_resource (std::vector<Res *> & tracker, CtArgs &&... args);
  template <typename Res>
  void destroy_and_untrack_resource (std::vector<Res *> & tracker, Res *& res);
  template <typename Res>
  void destroy_and_untrack_all_resources (std::vector<Res *> & tracker);

  std::vector<worker_pool *> m_worker_pools;
  std::vector<daemon *> m_daemons;
  std::mutex m_mutex;
};

} // namespace thread

#endif  // _THREAD_MANAGER_HPP_

/************************************************************************/
/* Template implementation                                              */
/************************************************************************/

namespace thread
{

template<typename Res, typename ... CtArgs>
inline Res * manager::create_and_track_resource (std::vector<Res*>& tracker, CtArgs &&... args)
{
  std::unique_lock<std::mutex> lock (m_mutex);  // safe-guard

  Res *new_res = new Res (std::forward<CtArgs> (args)...);

  tracker.push_back (new_res);

  return new_res;
}

template <typename ... Args>
worker_pool *
manager::create_worker_pool (Args &&... args)
{
  return create_and_track_resource (m_worker_pools, std::forward<Args> (args)...);
}

template<typename Func, typename ...Args>
daemon *
manager::create_daemon(looper & looper_arg, Func && func, Args && ...args)
{
  return create_and_track_resource (m_daemons, looper_arg, std::forward<Func> (func), std::forward<Args> (args)...);
}

} // namespace thread
