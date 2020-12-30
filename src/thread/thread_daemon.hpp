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
 * thread_daemon - interface for daemon threads
 */

#ifndef _THREAD_DAEMON_HPP_
#define _THREAD_DAEMON_HPP_

#include "thread_looper.hpp"
#include "thread_task.hpp"
#include "thread_waiter.hpp"

#include "perf_def.hpp"

#include <string>
#include <thread>

#include <cinttypes>

// cubthread::daemon
//
//  description
//    defines a daemon thread using a looper and a task
//    task is executed in a loop; wait times are defined by looper
//
//  how to use
//    // define your custom task
//    class custom_task : public cubthread::task<custom_thread_context>
//    {
//      void execute (custom_thread_context & context) override { ... }
//    }
//
//    // define your custom context manager
//    class custom_thread_context_manager : public cubthread::context_manager<custom_thread_context>
//    {
//      custom_thread_context & create_context (void) override { ... }
//      void retire_context(custom_thread_context & context) override { ... }
//    }
//
//    // declare a looper
//    cubthread::looper loop_pattern;   // by default sleep until wakeup
//
//    // context manager is required
//    custom_thread_context_manager thr_ctxt_mgr;
//
//    // and finally looping task
//    custom_task *task = new_custom_task ();
//
//    cubthread::daemon my_daemon (loop_pattern, thr_ctxt_mgr, *task);    // daemon starts, executes task and sleeps
//
//    std::chrono::sleep_for (std::chrono::seconds (1));
//    my_daemon.wakeup ();    // daemon executes task again
//    std::chrono::sleep_for (std::chrono::seconds (1));
//
//    // when daemon is destroyed, its execution is stopped and thread is joined
//    // daemon will handle task deletion
//
namespace cubthread
{
  class daemon
  {
    public:

      //  daemon constructor needs:
      //    loop_pattern_arg    : loop pattern for task execution
      //    context_manager_arg : context manager to create and retire thread execution context
      //    exec                : task to execute
      //
      //  NOTE: it is recommended to use dynamic allocation for execution tasks
      //
      template <typename Context>
      daemon (const looper &loop_pattern_arg, context_manager<Context> *context_manager_arg,
	      task<Context> *exec, const char *name);
      daemon (const looper &loop_pattern_arg, task_without_context *exec_arg, const char *name);
      ~daemon();

      void wakeup (void);         // wakeup daemon thread
      void stop_execution (void); // stop_execution daemon thread from looping and join it
      // note: this must not be called concurrently

      bool was_woken_up (void);   // return true if daemon was woken up before timeout

      void reset_looper (void);   // reset looper
      // note: this applies only if looper wait pattern is of type INCREASING_PERIODS

      // statistics
      static std::size_t get_stats_value_count (void);
      static const char *get_stat_name (std::size_t stat_index);
      void get_stats (cubperf::stat_value *stats_out);
      bool is_running (void);    // true, if running

    private:

      // loop functions invoked by spawned daemon thread

      // loop_with_context - after thread is spawned, it claims context from context manager and repeatedly executes
      //                     task until stopped.
      //
      // note: context must implement interrupt_execution function
      template <typename Context>
      static void loop_with_context (daemon *daemon_arg, context_manager<Context> *context_manager_arg,
				     task<Context> *exec_arg, const char *name);

      // loop_without_context - just execute context-less task in a loop
      static void loop_without_context (daemon *daemon_arg, task_without_context *exec_arg, const char *name);

      void pause (void);                                    // pause between tasks
      // register statistics
      void register_stat_start (void);
      void register_stat_pause (void);
      void register_stat_execute (void);

      // create a set to store daemon statistic values
      static cubperf::statset &create_statset (void);

      waiter m_waiter;        // thread waiter
      looper m_looper;        // thread looper
      std::function<void (void)> m_func_on_stop;  // callback function to interrupt execution on stop request

      std::thread m_thread;   // the actual daemon thread

      std::string m_name;     // own name

      // stats
      cubperf::statset &m_stats;
  };

  /************************************************************************/
  /* Inline/template Implementation                                       */
  /************************************************************************/

  template <typename Context>
  daemon::daemon (const looper &loop_pattern_arg, context_manager<Context> *context_manager_arg,
		  task<Context> *exec, const char *name /* = "" */)
    : m_waiter ()
    , m_looper (loop_pattern_arg)
    , m_func_on_stop ()
    , m_thread ()
    , m_name (name)
    , m_stats (daemon::create_statset ())
  {
    // starts a thread to execute daemon::loop
    m_thread = std::thread (daemon::loop_with_context<Context>, this, context_manager_arg, exec, m_name.c_str ());
  }

  template <typename Context>
  void
  daemon::loop_with_context (daemon *daemon_arg, context_manager<Context> *context_manager_arg,
			     task<Context> *exec_arg, const char *name)
  {
    (void) name;  // suppress unused parameter warning
    // its purpose is to help visualize daemon thread stacks

    // create execution context
    Context &context = context_manager_arg->create_context ();

    // now that we have access to context we can set the callback function on stop
    daemon_arg->m_func_on_stop = std::bind (&context_manager<Context>::stop_execution, std::ref (*context_manager_arg),
					    std::ref (context));

    daemon_arg->register_stat_start ();

    while (!daemon_arg->m_looper.is_stopped ())
      {
	// execute task
	exec_arg->execute (context);
	daemon_arg->register_stat_execute ();

	// take a break
	daemon_arg->pause ();
	daemon_arg->register_stat_pause ();
      }

    // retire execution context
    context_manager_arg->retire_context (context);

    // retire task
    exec_arg->retire ();
  }

} // namespace cubthread

#endif // _THREAD_DAEMON_HPP_
