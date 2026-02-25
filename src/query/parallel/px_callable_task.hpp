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
 * px_callable_task.hpp
 */

#ifndef _PX_CALLABLE_TASK_HPP_
#define _PX_CALLABLE_TASK_HPP_

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Wrong module
#endif // not SERVER_MODE and not SA_MODE

#include "thread_task.hpp"
#include "thread_entry.hpp"
#include <functional>

namespace parallel_query
{
  // forward declaration
  class worker_manager;

  class callable_task : public cubthread::task<cubthread::entry>
  {
    public:
      using exec_func_type = std::function<void (cubthread::entry &)>;
      using retire_func_type = std::function<void (void)>;

      callable_task () = delete;

      // constructor with default retire (delete/do nothing)
      template <typename F>
      callable_task (worker_manager *worker_manager_p, const F &f, bool delete_on_retire = true);
      template <typename F>
      callable_task (worker_manager *worker_manager_p, F &&f, bool delete_on_retire = true);

      // constructor with custom retire
      template <typename FuncExec, typename FuncRetire>
      callable_task (worker_manager *worker_manager_p, const FuncExec &fe, const FuncRetire &fr);
      template <typename FuncExec, typename FuncRetire>
      callable_task (worker_manager *worker_manager_p, FuncExec &&fe, FuncRetire &&fr);

      void execute (cubthread::entry &context) override;
      void retire () override;

    private:
      exec_func_type m_exec_f;
      retire_func_type m_retire_f;
      worker_manager *m_worker_manager_p;
  };

  template <typename F>
  callable_task::callable_task (worker_manager *worker_manager_p, const F &f, bool delete_on_retire)
    : m_exec_f (f)
    , m_worker_manager_p (worker_manager_p)
  {
    if (delete_on_retire)
      {
	m_retire_f = [this] { delete this; };
      }
    else
      {
	m_retire_f = [] {};  // do nothing
      }
  }

  template <typename F>
  callable_task::callable_task (worker_manager *worker_manager_p, F &&f, bool delete_on_retire)
    : m_exec_f (std::move (f))
    , m_worker_manager_p (worker_manager_p)
  {
    if (delete_on_retire)
      {
	m_retire_f = [this] { delete this; };
      }
    else
      {
	m_retire_f = [] {};  // do nothing
      }
  }

  template <typename FuncExec, typename FuncRetire>
  callable_task::callable_task (worker_manager *worker_manager_p, const FuncExec &fe, const FuncRetire &fr)
    : m_exec_f (fe)
    , m_retire_f (fr)
    , m_worker_manager_p (worker_manager_p)
  {
  }

  template <typename FuncExec, typename FuncRetire>
  callable_task::callable_task (worker_manager *worker_manager_p, FuncExec &&fe, FuncRetire &&fr)
    : m_exec_f (std::move (fe))
    , m_retire_f (std::move (fr))
    , m_worker_manager_p (worker_manager_p)
  {
  }
} // namespace parallel_query

#endif // _PX_CALLABLE_TASK_HPP_
