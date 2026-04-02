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
 * thread_task.hpp
 */

#ifndef _THREAD_TASK_HPP_
#define _THREAD_TASK_HPP_

#include <functional>
#include <mutex>
#include <thread>

#include <cassert>

namespace cubthread
{
  // cubthread::task
  //
  //  description:
  //    abstract class for thread tasks
  //
  //  templates:
  //    Context - thread execution context, a helper/cache structure that can be passed to multiple tasks.
  //
  //  how to use:
  //     1. specialize task with Context
  //        e.g. in CUBRID we have entry_task which uses entry as context; see thread_entry_task.hpp
  //
  //     2. extend specialized task<custom_context> and override virtual functions
  //        override execute (Context &) to define task execution
  //        [optional] override retire function; by default it deletes task
  //
  //     3. execute multiple tasks using same context:
  //          custom_context context;       // this is a dummy example
  //          custom_task* task_p = NULL;   // using custom_task = task<custom_context>
  //
  //          for (task_p = get_task (); task_p != NULL; task_p = get_task ())
  //            {
  //              task_p->execute (context);
  //              task_p->retire (); // this will delete task_p
  //            }
  //
  template <typename Context>
  class task
  {
    public:
      using context_type = Context;

      task (void) = default;

      // abstract class requires virtual destructor
      virtual ~task (void) = default;

      // virtual functions to be implemented by inheritors
      virtual void execute (context_type &) = 0;

      // implementation of task's retire function.
      virtual void retire (void)
      {
	delete this;
      }
  };

  // a class based on callable execution function
  template <typename Context>
  class callable_task : public task<Context>
  {
    public:
      using exec_func_type = std::function<void (Context &)>;

      callable_task () = delete;

      // constructor with default retire (delete/do nothing)
      template <typename F>
      callable_task (const F &f, bool delete_on_retire = true);
      template <typename F>
      callable_task (F &&f, bool delete_on_retire = true);

      // constructor with custom retire
      template <typename FuncExec, typename FuncRetire>
      callable_task (const FuncExec &fe, const FuncRetire &fr);
      template <typename FuncExec, typename FuncRetire>
      callable_task (FuncExec &&fe, FuncRetire &&fr);

      void execute (Context &ctx) final
      {
	m_exec_f (ctx);
      }

      void retire () final
      {
	m_retire_f ();
      }

    private:
      std::function<void (Context &)> m_exec_f;
      std::function<void (void)> m_retire_f;
  };

  // context-less task specialization. no argument for execute function
  template<>
  class task<void>
  {
    public:
      task (void) = default;
      virtual ~task (void) = default;

      virtual void execute (void) = 0;
      virtual void retire (void)
      {
	delete this;
      }
  };
  using task_without_context = task<void>;

  template<>
  class callable_task<void> : public task_without_context
  {
    public:

      callable_task () = delete;

      // constructor with default retire (delete/do nothing)
      template <typename F>
      callable_task (const F &f, bool delete_on_retire = true);
      template <typename F>
      callable_task (F &&f, bool delete_on_retire = true);

      // constructor with custom retire
      template <typename FuncExec, typename FuncRetire>
      callable_task (const FuncExec &fe, const FuncRetire &fr);
      template <typename FuncExec, typename FuncRetire>
      callable_task (FuncExec &&fe, FuncRetire &&fr);

      void execute () final
      {
	m_exec_f ();
      }

      void retire () final
      {
	m_retire_f ();
      }

    private:
      std::function<void (void)> m_exec_f;
      std::function<void (void)> m_retire_f;
  };
} // namespace cubthread

//////////////////////////////////////////////////////////////////////////
// Implementation
//////////////////////////////////////////////////////////////////////////

namespace cubthread
{
  template<typename Context>
  template<typename F>
  callable_task<Context>::callable_task (const F &f, bool delete_on_retire)
    : m_exec_f (f)
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

  template<typename Context>
  template<typename F>
  callable_task<Context>::callable_task (F &&f, bool delete_on_retire)
    : m_exec_f (std::move (f))
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

  template<typename Context>
  template<typename FuncExec, typename FuncRetire>
  callable_task<Context>::callable_task (const FuncExec &fe, const FuncRetire &fr)
    : m_exec_f (fe)
    , m_retire_f (fr)
  {
  }

  template<typename Context>
  template<typename FuncExec, typename FuncRetire>
  callable_task<Context>::callable_task (FuncExec &&fe, FuncRetire &&fr)
    : m_exec_f (std::move (fe))
    , m_retire_f (std::move (fr))
  {
  }

  template<typename F>
  callable_task<void>::callable_task (const F &f, bool delete_on_retire)
    : m_exec_f (f)
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

  template<typename F>
  callable_task<void>::callable_task (F &&f, bool delete_on_retire)
    : m_exec_f (std::move (f))
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

  template<typename FuncExec, typename FuncRetire>
  callable_task<void>::callable_task (const FuncExec &fe, const FuncRetire &fr)
    : m_exec_f (fe)
    , m_retire_f (fr)
  {
  }

  template<typename FuncExec, typename FuncRetire>
  callable_task<void>::callable_task (FuncExec &&fe, FuncRetire &&fr)
    : m_exec_f (std::move (fe))
    , m_retire_f (std::move (fr))
  {
  }
} // namespace cubthread


#endif // _THREAD_TASK_HPP_
