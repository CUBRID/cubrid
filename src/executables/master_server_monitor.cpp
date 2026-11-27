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

//
// master_server_monitor.cpp - Server Revive monitoring module
//

#include <sstream>
#include <algorithm>
#include <unistd.h>
#include <signal.h>

#include "system_parameter.h"
#include "error_context.hpp"
#include "error_manager.h"
#include "master_util.h"
#include "master_server_monitor.hpp"

std::unique_ptr<server_monitor> master_Server_monitor = nullptr;
bool auto_Restart_server = false;

server_monitor::server_monitor ()
{

  m_server_entry_map = std::unordered_map<std::string, server_entry> ();

  {
    std::lock_guard<std::mutex> lock (m_server_monitor_mutex);
    m_thread_shutdown = false;
  }

  start_monitoring_thread ();
  er_log_debug (ARG_FILE_LINE, "[Server Monitor] Monitoring started.");
}

// In server_monitor destructor, it should guarentee that
// m_monitoring_thread is terminated before m_monitor_list is deleted.
server_monitor::~server_monitor ()
{
  stop_monitoring_thread ();
  er_log_debug (ARG_FILE_LINE, "[Server Monitor] Monitoring finished.");
}

void
server_monitor::start_monitoring_thread ()
{
  m_monitoring_thread = std::make_unique<std::thread> (&server_monitor::server_monitor_thread_worker, this);
}

void
server_monitor::stop_monitoring_thread ()
{
  {
    std::lock_guard<std::mutex> lock (m_server_monitor_mutex);
    m_thread_shutdown = true;
  }
  m_monitor_cv_consumer.notify_all();

  m_monitoring_thread->join();
}

void
server_monitor::server_monitor_thread_worker ()
{
  job job;
  /* this thread reports errors with er_set (MASTER_ER_SET), which needs an error context of its own */
  cuberr::context er_context (true);

  while (true)
    {
      {
	std::unique_lock<std::mutex> lock (m_server_monitor_mutex);

	m_monitor_cv_consumer.wait (lock, [this]
	{
	  return !m_job_queue.empty () || m_thread_shutdown;
	});

	if (m_thread_shutdown)
	  {
	    break;
	  }
	else
	  {
	    assert (!m_job_queue.empty ());
	    consume_job (job);
	  }
      }
      process_job (job);
    }
}

void
server_monitor::register_server_entry (int pid, const std::string &exec_path, const std::string &args,
				       const std::string &server_name
				      )
{
  auto entry = m_server_entry_map.find (server_name);

  if (entry != m_server_entry_map.end ())
    {
      entry->second.set_pid (pid);
      entry->second.set_need_revive (false);
      entry->second.set_last_revived_time (std::chrono::steady_clock::now ());
      er_log_debug (ARG_FILE_LINE,
		    "[Server Monitor] [%s] Server entry has been registered. (pid : %d)",
		    server_name.c_str(), pid);
    }
  else
    {
      m_server_entry_map.emplace (std::move (server_name), server_entry (pid, exec_path, args,
				  std::chrono::steady_clock::time_point ()));

      er_log_debug (ARG_FILE_LINE,
		    "[Server Monitor] [%s] Server entry has been registered newly. (pid : %d)",
		    server_name.c_str(), pid);
    }
}

void
server_monitor::remove_server_entry (const std::string &server_name)
{
  auto entry = m_server_entry_map.find (server_name);

  // When the stop command for the same CUBRID server is invoked concurrently,
  // the corresponding server entry may have already been removed.
  // Therefore, the assertion should be skipped to avoid unnecessary failures.
  if (entry != m_server_entry_map.end ())
    {
      er_log_debug (ARG_FILE_LINE,
		    "[Server Monitor] [%s] Server entry has been unregistered. (pid : %d)",
		    server_name.c_str(), entry->second.get_pid());

      m_server_entry_map.erase (entry);
    }
}

void
server_monitor::revive_server (const std::string &server_name, int pid)
{
  int error_code;

  // Unacceptable revive time difference is set to be 120 seconds
  // as the timediff of server restart mechanism of heartbeat.
  constexpr int SERVER_MONITOR_UNACCEPTABLE_REVIVE_TIMEDIFF_IN_SECS = 120;
  // give up when the revived server exits within the time difference above this many times in a row.
  // a temporary cause (e.g. another process still using the database just after the crash) may fail a few of them.
  constexpr int SERVER_MONITOR_MAX_QUICK_REVIVE_FAILURES = 3;
  std::chrono::steady_clock::time_point tv;
  int out_pid;

  auto entry = m_server_entry_map.find (server_name);

  if (entry != m_server_entry_map.end ())
    {
      if (pid != -1 && pid != entry->second.get_pid ())
	{
	  /* a request for a process that has already been replaced (registered or revived again). one exit may be
	   * reported more than once (e.g. by master and by the confirm job), and must be revived only once. */
	  er_log_debug (ARG_FILE_LINE, "[Server Monitor] [%s] Ignore the stale request to revive server. (pid : %d)",
			entry->first.c_str(), pid);
	  return;
	}

      entry->second.set_need_revive (true);

      tv = std::chrono::steady_clock::now ();
      auto timediff = std::chrono::duration_cast<std::chrono::seconds> (tv -
		      entry->second.get_last_revived_time()).count();
      bool revived_before = entry->second.get_last_revived_time () != std::chrono::steady_clock::time_point ();

      // If the server is abnormally terminated and revived within a short period of time, it is considered as a repeated failure.
      // For HA server, heartbeat handle this case as demoting the server from master to slave and keep trying to revive the server.
      // However, in this case, the server_monitor will not try to revive the server due to following reasons.
      // 1. preventing repeated creation of core files.
      // 2. The service cannot be recovered even if revived if the server abnormally terminates again within a short time.

      // The revive time is also recorded when the server is forked below, so a revived server that exits before
      // registering counts as a quick failure too. One that takes longer than the time difference to fail is
      // revived each time, as before.
      bool quick_failure = revived_before && timediff <= SERVER_MONITOR_UNACCEPTABLE_REVIVE_TIMEDIFF_IN_SECS;
      int quick_revive_count = quick_failure ? entry->second.get_quick_revive_count () + 1 : 0;

      if (quick_revive_count < SERVER_MONITOR_MAX_QUICK_REVIVE_FAILURES)
	{
	  out_pid = try_revive_server (entry->second.get_exec_path(), entry->second.get_argv());
	  if (out_pid == -1)
	    {
	      er_log_debug (ARG_FILE_LINE,
			    "[Server Monitor] [%s] Failed to fork server process. Server monitor try to revive server again.",
			    entry->first.c_str());
	      produce_job_internal (job_type::REVIVE_SERVER, pid, "", "", entry->first);
	    }
	  else
	    {
	      entry->second.set_pid (out_pid);
	      /* record the revive time now, not only when the revived server registers. otherwise a revived server
	       * that exits before registering (e.g. it cannot register to master) is revived again and again. */
	      entry->second.set_last_revived_time (std::chrono::steady_clock::now ());
	      /* counted only when forked. a fork failure above is retried without counting it. */
	      entry->second.set_quick_revive_count (quick_revive_count);
	      er_log_debug (ARG_FILE_LINE,
			    "[Server Monitor] [%s] Server monitor is waiting for server to be registered. (pid : %d)",
			    entry->first.c_str(), entry->second.get_pid());
	      produce_job_internal (job_type::CONFIRM_REVIVE_SERVER, out_pid, "", "",
				    entry->first);
	    }
	  return;
	}
      else
	{
	  /* not a debug log: in a release build an operator must be able to see why the server is not revived */
	  std::string event = "Server " + entry->first + " exited within "
			      + std::to_string (SERVER_MONITOR_UNACCEPTABLE_REVIVE_TIMEDIFF_IN_SECS)
			      + " seconds after it was revived, "
			      + std::to_string (SERVER_MONITOR_MAX_QUICK_REVIVE_FAILURES) + " times in a row."
			      + " It will no longer be revived automatically; start it manually if it is not running";
	  std::string detail = "(last pid: " + std::to_string (entry->second.get_pid ()) + ")";
	  MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_PROCESS_EVENT, 2, event.c_str (), detail.c_str ());
	  m_server_entry_map.erase (entry);
	  return;
	}
    }
}

void
server_monitor::check_server_revived (const std::string &server_name, int pid)
{
  int error_code;
  auto entry = m_server_entry_map.find (server_name);

  if (entry != m_server_entry_map.end ())
    {
      if (pid != entry->second.get_pid ())
	{
	  /* the process this job watches has been replaced. another job watches the current one. */
	  return;
	}

      error_code = kill (entry->second.get_pid (), 0);
      if (error_code)
	{
	  if (errno == ESRCH)
	    {
	      /* with the pid. if master also reports the exit, revive_server () ignores the second request. */
	      er_log_debug (ARG_FILE_LINE,
			    "[Server Monitor] [%s] Revived server process can not be found. Server monitor will try to revive server again. (pid : %d)",
			    entry->first.c_str(), entry->second.get_pid());
	      produce_job_internal (job_type::REVIVE_SERVER, pid, "", "", entry->first);
	    }
	  else
	    {
	      er_log_debug (ARG_FILE_LINE,
			    "[Server Monitor] [%s] Failed to revive server due to unknown error from kill() function. It will no longer be revived automatically. (pid : %d)",
			    entry->first.c_str(), entry->second.get_pid());
	      kill (entry->second.get_pid (), SIGKILL);
	      m_server_entry_map.erase (entry);
	    }
	}
      else if (entry->second.get_need_revive ())
	{
	  // Server revive confirm interval is set to be 1 second to avoid busy waiting.
	  constexpr int SERVER_MONITOR_CONFIRM_REVIVE_INTERVAL_IN_SECS = 1;

	  std::this_thread::sleep_for (std::chrono::seconds (SERVER_MONITOR_CONFIRM_REVIVE_INTERVAL_IN_SECS));

	  produce_job_internal (job_type::CONFIRM_REVIVE_SERVER, pid, "", "",
				entry->first);

	}
      else
	{
	  er_log_debug (ARG_FILE_LINE, "[Server Monitor] [%s] Server revive success. (pid : %d)",
			entry->first.c_str(),
			entry->second.get_pid());
	}
      return;
    }
}

int
server_monitor::try_revive_server (const std::string &exec_path, char *const *argv)
{
  pid_t pid;

  pid = fork ();
  if (pid < 0)
    {
      return -1;
    }
  else if (pid == 0)
    {
      return execv (exec_path.c_str(), argv);
    }
  else
    {
      return pid;
    }
}
void
server_monitor::shutdown_server (const std::string &server_name)
{
  int rv;
  auto entry = m_server_entry_map.find (server_name);

  if (entry != m_server_entry_map.end ())
    {

      if (entry->second.get_need_revive ())
	{
	  er_log_debug (ARG_FILE_LINE,
			"[Server Monitor] [%s] Server is shutdown. Reviving the server will not be tried.",
			server_name.c_str());
	}
      else
	{
	  css_process_start_shutdown_by_name (const_cast<char *> (server_name.c_str()));

	  er_log_debug (ARG_FILE_LINE,
			"[Server Monitor] [%s] Server is already revived. Server monitor will terminate the server. (pid : %d)",
			server_name.c_str(), entry->second.get_pid());
	}
      m_server_entry_map.erase (entry);
    }
}

void
server_monitor::produce_job_internal (job_type job_type, int pid, const std::string &exec_path,
				      const std::string &args, const std::string &server_name)
{
  std::lock_guard<std::mutex> lock (m_server_monitor_mutex);
  m_job_queue.emplace (job_type, pid, exec_path, args, server_name);
}

void
server_monitor::produce_job (job_type job_type, int pid, const std::string &exec_path,
			     const std::string &args, const std::string &server_name)
{
  produce_job_internal (job_type, pid, exec_path, args, server_name);
  m_monitor_cv_consumer.notify_all();
}

void
server_monitor::consume_job (job &consume_job)
{
  consume_job = std::move (m_job_queue.front ());
  m_job_queue.pop ();
}

void
server_monitor::process_job (job &consume_job)
{
  switch (consume_job.get_job_type ())
    {
    case job_type::REGISTER_SERVER:
      register_server_entry (consume_job.get_pid(), consume_job.get_exec_path(), consume_job.get_args(),
			     consume_job.get_server_name());

      break;
    case job_type::UNREGISTER_SERVER:
      remove_server_entry (consume_job.get_server_name());
      break;
    case job_type::REVIVE_SERVER:
      revive_server (consume_job.get_server_name(), consume_job.get_pid ());
      break;
    case job_type::CONFIRM_REVIVE_SERVER:
      check_server_revived (consume_job.get_server_name(), consume_job.get_pid ());
      break;
    case job_type::SHUTDOWN_SERVER:
      shutdown_server (consume_job.get_server_name());
      break;
    case job_type::JOB_MAX:
    default:
      assert (false);
      break;
    }
}

server_monitor::job::
job (job_type job_type, int pid, const std::string &exec_path, const std::string &args,
     const std::string &server_name)
  : m_job_type {job_type}
  , m_pid {pid}
  , m_exec_path {exec_path}
  , m_args {args}
  , m_server_name {server_name}
{
}

server_monitor::job_type
server_monitor::job::get_job_type () const
{
  return m_job_type;
}

int
server_monitor::job::get_pid () const
{
  return m_pid;
}

std::string
server_monitor::job::get_exec_path () const
{
  return m_exec_path;
}

std::string
server_monitor::job::get_args () const
{
  return m_args;
}

std::string
server_monitor::job::get_server_name () const
{
  return m_server_name;
}

server_monitor::server_entry::
server_entry (int pid, const std::string &exec_path, const std::string &args,
	      std::chrono::steady_clock::time_point revive_time)
  : m_pid {pid}
  , m_exec_path {exec_path}
  , m_need_revive {false}
  , m_last_revived_time {revive_time}
  , m_quick_revive_count {0}
{
  if (args.size() > 0)
    {
      proc_make_arg (args);
    }
}

server_monitor::server_entry::~server_entry ()
{
  if (m_argv)
    {
      for (int i = 0; m_argv[i] != nullptr; i++)
	{
	  delete[] m_argv[i];
	}
      m_argv.reset();
    }
}

int
server_monitor::server_entry::get_pid () const
{
  return m_pid;
}

std::string
server_monitor::server_entry::get_exec_path () const
{
  return m_exec_path;
}

char *const *
server_monitor::server_entry::get_argv () const
{
  return m_argv.get ();
}

bool
server_monitor::server_entry::get_need_revive () const
{
  return m_need_revive;
}

std::chrono::steady_clock::time_point
server_monitor::server_entry::get_last_revived_time () const
{
  return m_last_revived_time;
}

void
server_monitor::server_entry::set_pid (int pid)
{
  m_pid = pid;
}

void
server_monitor::server_entry::set_exec_path (const std::string &exec_path)
{
  m_exec_path = exec_path;
}

void
server_monitor::server_entry::set_need_revive (bool need_revive)
{
  m_need_revive = need_revive;
}

void
server_monitor::server_entry::set_last_revived_time (std::chrono::steady_clock::time_point revive_time)
{
  m_last_revived_time = revive_time;
}

int
server_monitor::server_entry::get_quick_revive_count () const
{
  return m_quick_revive_count;
}

void
server_monitor::server_entry::set_quick_revive_count (int count)
{
  m_quick_revive_count = count;
}

void
server_monitor::server_entry::proc_make_arg (const std::string &args)
{
  //argv is type of std::unique_ptr<const char *[]>
  m_argv = std::make_unique<char *[]> (args.size () + 1);
  std::istringstream iss (args);
  std::string arg;
  int i = 0;
  while (std::getline (iss, arg, ' '))
    {
      m_argv[i] = new char[arg.size () + 1];
      std::copy (arg.begin (), arg.end (), m_argv[i]);
      m_argv[i][arg.size()] = '\0';
      i++;
    }
  m_argv[i] = NULL;
}
