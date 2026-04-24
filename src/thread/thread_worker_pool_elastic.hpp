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
 * thread_worker_pool_elastic.hpp
 */

#ifndef _THREAD_WORKER_POOL_ELASTIC_HPP_
#define _THREAD_WORKER_POOL_ELASTIC_HPP_

#if !defined (SERVER_MODE)
#error Wrong module
#endif // not SERVER_MODE

// same module include
#include "thread_worker_pool_impl.hpp"

// cubrid includes
#include "error_manager.h"

// system includes
#include <chrono>

namespace cubthread
{
  // worker_pool_elastic<Stats>
  //
  // description
  //    worker pool that maintains target concurrency by spawning
  //    additional workers when existing workers enter a known wait
  //    (e.g., blocked on a transaction lock).
  //
  template <stats_t Stats>
  class worker_pool_elastic : public worker_pool_impl<Stats>
  {
      // forward definition for nested core class
      friend class manager;

    public:
      // forward definition
      class core_elastic;

      ~worker_pool_elastic ();

    protected:
      worker_pool_elastic (std::size_t pool_size, std::size_t core_count, const char *name, entry_manager &entry_mgr,
			   bool pool_threads = false, wait_seconds idle_timeout = std::chrono::seconds (5));

      std::unique_ptr<worker_pool::core> allocate_core (bool pool_threads) override;
  };

  // worker_pool_elastic<Stats>::core_elastic
  //
  // description
  //    TODO: add description
  //
  template <stats_t Stats>
  class worker_pool_elastic<Stats>::core_elastic : public worker_pool_impl<Stats>::core_impl
  {
      friend class worker_pool_elastic;

    public:
      ~core_elastic ();

    protected:
      core_elastic (bool pool_threads);
  };

} // namespace cubthread

namespace cubthread
{
  //////////////////////////////////////////////////////////////////////////
  // worker_pool_elastic<Stats>
  //////////////////////////////////////////////////////////////////////////

  template <stats_t Stats>
  worker_pool_elastic<Stats>::worker_pool_elastic (std::size_t pool_size, std::size_t core_count, const char *name,
      entry_manager &entry_mgr, bool pool_threads, wait_seconds idle_timeout)
    : worker_pool_impl<Stats> (pool_size, core_count, name, entry_mgr, pool_threads, idle_timeout)
  {
  }

  template <stats_t Stats>
  worker_pool_elastic<Stats>::~worker_pool_elastic ()
  {
  }

  template <stats_t Stats>
  std::unique_ptr<typename worker_pool::core>
  worker_pool_elastic<Stats>::allocate_core (bool pool_threads)
  {
    return std::unique_ptr<worker_pool::core> (new core_elastic (pool_threads));
  }

  //////////////////////////////////////////////////////////////////////////
  // worker_pool_elastic<Stats>::core_elastic
  //////////////////////////////////////////////////////////////////////////

  template <stats_t Stats>
  worker_pool_elastic<Stats>::core_elastic::core_elastic (bool pool_threads)
    : worker_pool_impl<Stats>::core_impl (pool_threads)
  {
  }

  template <stats_t Stats>
  worker_pool_elastic<Stats>::core_elastic::~core_elastic ()
  {
  }

} // namespace cubthread

#endif // _THREAD_WORKER_POOL_ELASTIC_HPP_
